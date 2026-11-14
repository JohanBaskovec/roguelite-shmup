#include "headless.h"

#include "bot.h"
#include "draw.h"
#include "game.h"

#include <stdio.h>

/* Soft-lock detection: enemies alive but none of them hurt for this long... */
#define MAX_STALL_SECONDS 60.0f
/* ...or a combat room that simply never ends. */
#define MAX_ROOM_SECONDS 600.0f
/* The bot always walks to a door once a room is cleared; staying longer means it is stuck. */
#define MAX_CLEARED_SECONDS 30.0f

static bool finite_v2(V2 p) { return isfinite(p.x) && isfinite(p.y); }

static bool in_arena(V2 p, float r) {
    const float tol = 1.0f;
    return p.x >= ARENA_X0 + r - tol && p.x <= ARENA_X1 - r + tol && p.y >= ARENA_Y0 + r - tol &&
           p.y <= ARENA_Y1 - r + tol;
}

/* Returns NULL when everything is fine, otherwise a description of the problem. */
static const char *check_invariants(float room_time) {
    static char msg[512];
    const Player *p = &G.pl;
    if (!finite_v2(p->pos) || !finite_v2(p->vel)) return "player position/velocity is not finite";
    if (G.state == GS_PLAYING && !G.transitioning && !in_arena(p->pos, PLAYER_BODY_R)) {
        snprintf(msg, sizeof msg, "player outside the arena at (%.1f, %.1f)", (double)p->pos.x, (double)p->pos.y);
        return msg;
    }
    if (p->hp < 0 || p->hp > p->max_hp) return "player hp out of range";
    if (p->coins < 0 || p->keys < 0 || p->bombs < 0) return "negative resource count";
    if (p->weapon < 0 || p->weapon >= WPN_COUNT || p->wlevel[p->weapon] <= 0) return "player holds a weapon it does not own";
    if (p->max_hp < 2 || p->max_hp > 24 || (p->max_hp & 1)) return "max hp out of range";
    if (!(p->dmg_mult >= 0.3f * 0.99f) || !(p->rate_mult >= 0.3f * 0.99f) || !(p->range_mult >= 0.35f * 0.99f) ||
        !(p->shot_speed_mult >= 0.4f * 0.99f))
        return "weapon stat multiplier below its floor";
    if (!(p->speed >= 330.0f * 0.99f) || !(p->dash_cooldown >= 0.15f * 0.99f) || !(p->dash_cooldown <= 2.0f * 1.01f))
        return "movement stat out of range";
    if (!(p->hit_radius >= 2.0f * 0.99f) || !(p->hit_radius <= 9.0f * 1.01f) || p->luck < -3 || p->luck > 5)
        return "hitbox / luck out of range";
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        if (!finite_v2(e->pos) || !isfinite(e->hp)) return "enemy state is not finite";
        if (!in_arena(e->pos, e->radius)) {
            snprintf(msg, sizeof msg, "enemy type %d outside the arena at (%.1f, %.1f)", e->type, (double)e->pos.x, (double)e->pos.y);
            return msg;
        }
    }
    for (int i = 0; i < G.neb; i++)
        if (!finite_v2(G.eb[i].pos)) return "enemy bullet position is not finite";
    for (int i = 0; i < G.npb; i++)
        if (!finite_v2(G.pb[i].pos)) return "player bullet position is not finite";
    for (int i = 0; i < G.npk; i++)
        if (!finite_v2(G.pk[i].pos) || !in_arena(G.pk[i].pos, 0.0f)) return "pickup outside the arena";
    /* Rules: every room starts with enemies; enemies never drop anything - every pickup
     * (resources included) is a room reward and only appears once the room is cleared. */
    if (G.room.nwaves < 1) return "room without enemy waves";
    if (!G.room.cleared && G.npk > 0) {
        snprintf(msg, sizeof msg, "pickup (type %d) present before the room was cleared - enemy drop or early reward?",
                 G.pk[0].type);
        return msg;
    }
    for (int i = 0; i < G.npk; i++)
        if (G.pk[i].price < 0 || (G.pk[i].price > 0 && G.room.type != ROOM_SHOP)) return "priced item outside a shop";
    float stall = fminf(room_time, G.time - G.last_enemy_damage_t);
    if (G.room.type == ROOM_COMBAT && !G.room.cleared &&
        (room_time > MAX_ROOM_SECONDS || (G.nen > 0 && stall > MAX_STALL_SECONDS))) {
        int len = snprintf(msg, sizeof msg, "room %d stalled (%.0f s in room, no enemy hit for %.0f s) - player@(%.0f,%.0f) %s, %d bullets - left:",
                           G.room.depth, (double)room_time, (double)stall, (double)p->pos.x, (double)p->pos.y,
                           WEAPONS[p->weapon].name, G.npb);
        for (int i = 0; i < G.nen && i < 3 && len < (int)sizeof msg - 40; i++)
            len += snprintf(msg + len, sizeof msg - (size_t)len, " %s@(%.0f,%.0f) hp %.0f/%.0f", ENEMIES[G.en[i].type].name,
                            (double)G.en[i].pos.x, (double)G.en[i].pos.y, (double)G.en[i].hp, (double)G.en[i].max_hp);
        return msg;
    }
    if (G.room.cleared && G.room.clear_t > MAX_CLEARED_SECONDS) {
        snprintf(msg, sizeof msg, "still in cleared room %d after %.0f s (bot stuck at %.0f, %.0f?)", G.room.depth,
                 (double)G.room.clear_t, (double)p->pos.x, (double)p->pos.y);
        return msg;
    }
    return NULL;
}

/* Render the current frame offscreen (no window is shown) and save it, for post-mortem debugging. */
static void save_failure_shot(const char *path) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (SDL_CreateWindowAndRenderer("Neon Breach (headless)", SCREEN_W, SCREEN_H, 0, &win, &ren)) {
        SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        draw_init(ren);
        game_render();
        SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
        if (s && SDL_SaveBMP(s, path)) printf("  failure frame saved to %s\n", path);
        SDL_DestroySurface(s);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static void clear_edges(void) {
    Input *in = &G.in;
    in->dash_pressed = in->bomb_pressed = in->pause_pressed = in->restart_pressed = in->confirm_pressed = false;
    in->weapon_select = -1;
    in->weapon_cycle = 0;
}

int headless_run(const HeadlessOptions *o) {
    int failures = 0;
    int runs = o->runs > 0 ? o->runs : 1;
    long long total_ticks = 0;
    int best = 0, sum_rooms = 0;
    uint64_t t0 = SDL_GetPerformanceCounter();

    for (int run = 0; run < runs; run++) {
        uint64_t seed = o->seed + (uint64_t)run;
        game_init(seed);
        G.god = o->god;
        game_new_run();
        G.pl.coins += o->coins;
        if (o->stats_room) room_enter(ROOM_STATS, RW_NONE, o->depth > 0 ? o->depth : 1);
        else if (o->shop_room) room_enter(ROOM_SHOP, RW_NONE, o->depth > 0 ? o->depth : 1);
        else if (o->depth > 0) room_enter(ROOM_COMBAT, RW_UPGRADE, o->depth);

        int depth = G.room.depth;
        int stats_rooms = G.room.type == ROOM_STATS, shops = G.room.type == ROOM_SHOP;
        bool was_cleared = G.room.cleared;
        float room_time = 0.0f;
        const char *err = G.room.cleared ? "first room starts already cleared (every room must start with enemies)" : NULL;
        int tick = 0;
        for (; !err && tick < o->max_ticks && G.state != GS_GAMEOVER; tick++) {
            bot_input();
            game_update();
            clear_edges();
            if (G.room.depth != depth) {
                depth = G.room.depth;
                if (G.room.cleared) {
                    err = "entered a room that was already cleared (every room must start with enemies)";
                    break;
                }
                stats_rooms += G.room.type == ROOM_STATS;
                shops += G.room.type == ROOM_SHOP;
                room_time = 0.0f;
                was_cleared = false;
            }
            if (G.room.cleared && !was_cleared && G.room.type == ROOM_SHOP) {
                int items = 0;
                for (int i = 0; i < G.npk; i++) items += G.pk[i].price > 0;
                if (items != SHOP_ITEMS) {
                    static char buf[96];
                    snprintf(buf, sizeof buf, "shop opened with %d items for sale instead of %d", items, SHOP_ITEMS);
                    err = buf;
                    break;
                }
            }
            was_cleared = G.room.cleared;
            room_time += DT;
            if ((err = check_invariants(room_time)) != NULL) break;
        }
        total_ticks += tick;

        const char *outcome = err ? "FAIL" : G.state == GS_GAMEOVER ? "died" : "time limit";
        printf("run seed=%-6llu rooms=%-3d depth=%-3d kills=%-5d time=%5.0fs hp=%d/%d weapons=",
               (unsigned long long)seed, G.rooms_cleared, G.room.depth, G.kills, (double)G.run_time, G.pl.hp, G.pl.max_hp);
        for (int w = 0; w < WPN_COUNT; w++)
            if (G.pl.wlevel[w]) printf("%.3s%d ", WEAPONS[w].name, G.pl.wlevel[w]);
        const Player *p = &G.pl;
        printf("coins=%d shops=%d bought=%d stats_rooms=%d", p->coins, shops, G.items_bought, stats_rooms);
        if (stats_rooms)
            printf(" [dmg x%.2f rate x%.2f range x%.2f speed x%.2f dash %.2fs hitbox %.1f luck %+d]", (double)p->dmg_mult,
                   (double)p->rate_mult, (double)p->range_mult, (double)(p->speed / 540.0f), (double)p->dash_cooldown,
                   (double)p->hit_radius, p->luck);
        printf(" -> %s\n", outcome);
        if (err) {
            printf("  invariant violated at tick %d: %s\n", tick, err);
            if (o->fail_shot && failures == 0) save_failure_shot(o->fail_shot);
            failures++;
        } else if (o->shop_room && o->coins >= 30 && G.items_bought == 0) {
            printf("  started in a shop with %d coins but never bought anything\n", G.pl.coins);
            failures++;
        } else if (G.rooms_cleared < o->expect_rooms) {
            printf("  expected at least %d cleared rooms\n", o->expect_rooms);
            failures++;
        }
        best = G.rooms_cleared > best ? G.rooms_cleared : best;
        sum_rooms += G.rooms_cleared;
    }

    double secs = (double)(SDL_GetPerformanceCounter() - t0) / (double)SDL_GetPerformanceFrequency();
    printf("%d run(s): avg %.1f rooms, best %d, %lld ticks simulated in %.2f s (%.0fx real time) - %s\n", runs,
           (double)sum_rooms / runs, best, total_ticks, secs, (double)total_ticks / TICK_RATE / (secs > 0 ? secs : 1),
           failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
