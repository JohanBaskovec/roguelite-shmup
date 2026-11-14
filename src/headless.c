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

/* The Player stats must always be base + the upgrades turned on (nobody writes them directly). */
static bool stats_in_sync(void) {
    const Player *p = &G.pl;
    Stats s;
    stats_compute(&s, false);
    return s.dmg_mult == p->dmg_mult && s.rate_mult == p->rate_mult && s.shot_speed_mult == p->shot_speed_mult &&
           s.speed == p->speed && s.dash_cooldown == p->dash_cooldown && s.magnet == p->magnet && s.range_mult == p->range_mult &&
           s.hit_radius == p->hit_radius && s.spread_mult == p->spread_mult && s.bullet_size == p->bullet_size &&
           s.homing == p->homing && s.max_hp == p->max_hp && s.extra_proj == p->extra_proj && s.pierce == p->pierce &&
           s.bounce == p->bounce && s.drones == p->drones && s.luck == p->luck;
}

static bool in_arena(V2 p, float r) {
    const float tol = 1.0f;
    return p.x >= ARENA_X0 + r - tol && p.x <= ARENA_X1 - r + tol && p.y >= ARENA_Y0 + r - tol &&
           p.y <= ARENA_Y1 - r + tol;
}

static bool on_floor(int u) {
    for (int i = 0; i < G.npk; i++)
        if (G.pk[i].type == PK_UPGRADE && G.pk[i].sub == u) return true;
    return false;
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
    for (int u = 0; u < UPG_COUNT; u++) {
        if (p->upg_count[u] < 0 || p->upg_count[u] > 1) return "upgrade owned twice (every upgrade is taken at most once)";
        if (p->upg_off[u] && p->upg_count[u] == 0) return "upgrade turned off without being owned";
        if (p->upg_off[u] && upg_has(u) != 0) return "upgrade turned off but still in effect";
    }
    if (!stats_in_sync()) return "player stats differ from base + upgrades turned on (stat written directly?)";
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if ((k->type == PK_UPGRADE || k->type == PK_AUGMENT) && (k->sub < 0 || k->sub >= UPG_COUNT)) return "invalid upgrade offered";
        if (k->type == PK_UPGRADE && p->upg_count[k->sub] > 0) return "upgrade offered that the player already owns";
        if (k->type == PK_STAT && (stat_offer_down(k->sub) == ST_MAX_HP || stat_offer_down(k->sub) == ST_SHIELD))
            return "stat trade offered that lowers max HP (or shields)";
        if (k->type == PK_AUGMENT && p->upg_count[k->sub] > 0) return "augment offered that the player already owns";
        for (int j = i + 1; j < G.npk; j++)
            if (k->type == PK_UPGRADE && G.pk[j].type == PK_UPGRADE && G.pk[j].sub == k->sub) return "the same upgrade offered twice";
        if (k->type == PK_UPGRADE && UPGRADES[k->sub].tier > 0) return "workshop-exclusive upgrade offered outside the augment machine";
        if (k->type == PK_AUGMENT && (G.room.type != ROOM_WORKSHOP || UPGRADES[k->sub].tier == 0 || !k->choice))
            return "augment pad outside a workshop (or not an exclusive one-of-N choice)";
    }
    for (int i = 0; i < G.nmc; i++)
        if (G.mc[i].type == MC_GAMBLE && !G.mc[i].broken && G.mc[i].prize == PK_UPGRADE &&
            (p->upg_count[G.mc[i].prize_sub] > 0 || on_floor(G.mc[i].prize_sub)))
            return "slot machine prize already owned (or also on the floor)";
    for (int i = 0; i < G.nmc; i++)
        if (G.mc[i].type == MC_GAMBLE && G.mc[i].prize == PK_STAT && stat_offer_down(G.mc[i].prize_sub) == ST_MAX_HP)
            return "slot machine prize is a stat trade that lowers max HP";
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (k->type != PK_ACTIVE) continue;
        if (k->sub <= ACT_NONE || k->sub >= ACT_COUNT || p->act[k->sub].level >= MAX_ACTIVE_LEVEL)
            return "invalid active item offered (or one owned already at max level)";
        if (!k->choice || G.room.type != ROOM_COMBAT || G.room.reward != RW_ACTIVE) return "active item outside an active room's choice";
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        const ActiveSlot *k = &p->act[a];
        if (k->level < 0 || k->level > MAX_ACTIVE_LEVEL || (a == ACT_NONE && k->level != 0)) return "active item level out of range";
        if (k->cd < 0.0f || k->t < 0.0f || !isfinite(k->cd) || !isfinite(k->t)) return "active item timers invalid";
        if (k->level == 0 && (k->off || k->t > 0.0f)) return "active item turned off / running without being owned";
        if (k->off && k->t > 0.0f) return "active item turned off but its effect still runs";
    }
    if (p->dash_t > 0.0f && !active_level_on(ACT_DASH)) return "dashing without the dash active item (or with it turned off)";
    {
        int lent = 0;
        bool chaos_on = active_running(ACT_CHAOS);
        for (int u = 0; u < UPG_COUNT; u++) {
            if (!p->chaos[u]) continue;
            lent++;
            if (!chaos_on) return "CHAOS upgrades still lent after the effect ended";
            if (p->upg_count[u] > 0) return "CHAOS lent an upgrade the player already owns";
        }
        if (lent > active_chaos_count(p->act[ACT_CHAOS].level)) return "CHAOS lent too many upgrades";
    }
    for (int i = 0; i < G.room.ndoors; i++)
        for (int j = i + 1; j < G.room.ndoors; j++) {
            const Door *a = &G.room.doors[i], *b = &G.room.doors[j];
            if (a->type == b->type && (a->type != ROOM_COMBAT || a->reward == b->reward)) {
                snprintf(msg, sizeof msg, "two identical doors (%s)", door_label(a));
                return msg;
            }
        }
    if (G.nmc < 0 || G.nmc > MAX_MACHINES) return "machine count out of range";
    if (G.nmc > 0 && (G.room.type != ROOM_WORKSHOP || !G.room.cleared)) return "workshop machines outside a cleared workshop";
    if (!(p->spread_mult >= SPREAD_MIN * 0.99f) || !(p->spread_mult <= SPREAD_MAX * 1.01f)) return "spread out of range";
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
        if (e->max_hp != ENEMIES[e->type].hp) return "enemy hp differs from its type's (enemies never scale with depth)";
        if (!in_arena(e->pos, e->radius)) {
            snprintf(msg, sizeof msg, "enemy type %d outside the arena at (%.1f, %.1f)", e->type, (double)e->pos.x, (double)e->pos.y);
            return msg;
        }
    }
    if (G.neb < 0 || G.neb > MAX_EBULLETS || G.npb < 0 || G.npb > MAX_PBULLETS || G.nen < 0 || G.nen > MAX_ENEMIES ||
        G.npk < 0 || G.npk > MAX_PICKUPS)
        return "entity count out of range (array modified while being iterated?)";
    for (int i = 0; i < G.neb; i++)
        if (!finite_v2(G.eb[i].pos)) return "enemy bullet position is not finite";
    for (int i = 0; i < G.npb; i++)
        if (!finite_v2(G.pb[i].pos)) return "player bullet position is not finite";
    for (int i = 0; i < G.npk; i++)
        if (!finite_v2(G.pk[i].pos) || !in_arena(G.pk[i].pos, 0.0f)) return "pickup outside the arena";
    /* Rules: every room starts with enemies; enemies never drop anything - every pickup
     * (resources included) is a room reward and only appears once the room is cleared. */
    bool peaceful = G.room.peaceful;
    if (peaceful && !room_type_peaceful(G.room.type)) return "a room type that must have enemies started peaceful";
    if (G.room.nwaves < 1 && !peaceful) return "room without enemy waves";
    if (peaceful && (!G.room.cleared || G.nen + G.nsp > 0)) return "enemies in a peaceful room";
    if (p->shield < 0 || p->shield > MAX_SHIELD || p->revives < 0) return "shield / revive count out of range";
    if (!(p->spread_mult > 0.0f) || !(p->bullet_size > 0.0f) || !isfinite(p->bullet_size)) return "bullet shape multiplier invalid";
    if (!G.room.cleared && G.npk > 0) {
        snprintf(msg, sizeof msg, "pickup (type %d) present before the room was cleared - enemy drop or early reward?",
                 G.pk[0].type);
        return msg;
    }
    if (G.room.secret_open && G.room.secret_corner < 0) return "secret passage open in a room without a secret";
    if (G.room.type == ROOM_SECRET && G.room.ndoors != 0) return "secret room has regular doors (its only exit leads back)";
    for (int i = 0; i < G.npk; i++)
        if (G.pk[i].price < 0 || (G.pk[i].price > 0 && G.room.type != ROOM_SHOP && !G.room.secret_shop))
            return "priced item outside a shop";
    float stall = fminf(room_time, G.time - G.last_enemy_damage_t);
    if (G.room.type == ROOM_COMBAT && !G.room.cleared &&
        (room_time > MAX_ROOM_SECONDS || (G.nen > 0 && stall > MAX_STALL_SECONDS))) {
        int len = snprintf(msg, sizeof msg, "room %d stalled (%.0f s in room, no enemy hit for %.0f s) - player@(%.0f,%.0f) %s, %d bullets - left:",
                           G.room.depth, (double)room_time, (double)stall, (double)p->pos.x, (double)p->pos.y,
                           gun_main_mod() >= 0 ? UPGRADES[gun_main_mod()].name : "plain shots", G.npb);
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

static int count_choices(void) {
    int n = 0;
    for (int i = 0; i < G.npk; i++) n += G.pk[i].choice;
    return n;
}

/* Checked on the tick a room becomes cleared: what its reward must look like. */
static const char *check_room_reward(void) {
    static char buf[128];
    int priced = 0;
    for (int i = 0; i < G.npk; i++) priced += G.pk[i].price > 0;
    if ((G.room.type == ROOM_SHOP || G.room.secret_shop) && priced != SHOP_ITEMS) {
        snprintf(buf, sizeof buf, "shop opened with %d items for sale instead of %d", priced, SHOP_ITEMS);
        return buf;
    }
    bool item_room = G.room.type == ROOM_COMBAT && (G.room.reward == RW_UPGRADE || G.room.reward == RW_ACTIVE);
    if (item_room && count_choices() != REWARD_CHOICES) {
        snprintf(buf, sizeof buf, "%s room offers %d items to choose from instead of %d",
                 reward_name(G.room.reward), count_choices(), REWARD_CHOICES);
        return buf;
    }
    if (G.room.type == ROOM_COMBAT && G.room.reward == RW_UPGRADE)
        for (int i = 0; i < G.npk; i++)
            if (G.pk[i].type != PK_UPGRADE && G.pk[i].type != PK_STAT)
                return "upgrade room offers neither an upgrade nor, once all are owned, a stat trade";
    if (G.room.type == ROOM_COMBAT && G.room.reward == RW_ACTIVE) {
        int seen = 0;
        for (int i = 0; i < G.npk; i++) {
            const Pickup *k = &G.pk[i];
            if (k->type != PK_ACTIVE) return "active room offers something else than active items";
            if (seen & (1 << k->sub)) return "active room offers the same item twice";
            seen |= 1 << k->sub;
        }
    }
    if (G.room.type == ROOM_STATS && count_choices() == 0) return "stats room cleared without any trade";
    if (G.room.type == ROOM_WORKSHOP) {
        int supply = 0, gamble = 0, pads = 0, tier1_left = 0;
        for (int i = 0; i < G.nmc; i++) {
            supply += G.mc[i].type == MC_SUPPLY && G.mc[i].uses >= SUPPLY_USES_MIN && !G.mc[i].broken;
            gamble += G.mc[i].type == MC_GAMBLE && !G.mc[i].broken;
        }
        for (int i = 0; i < G.npk; i++) pads += G.pk[i].type == PK_AUGMENT;
        for (int u = 0; u < UPG_COUNT; u++) tier1_left += UPGRADES[u].tier == 1 && G.pl.upg_count[u] == 0;
        if (supply != 1 || gamble != 1) return "workshop cleared without its supply press and slot machine";
        if (pads > 3) return "workshop offers more than 3 augments";
        if (pads == 0 && G.pl.max_hp >= 4 && tier1_left > 0) return "workshop offers no augment although the player can pay one";
        if (G.mc[1].prize == PK_UPGRADE && UPGRADES[G.mc[1].prize_sub].tier > 0) return "slot machine offers a workshop exclusive";
    }
    if (G.room.type == ROOM_SECRET) {
        int items = 0, resources = 0;
        for (int i = 0; i < G.npk; i++) {
            const Pickup *k = &G.pk[i];
            items += (k->type == PK_UPGRADE || k->type == PK_STAT) && !k->choice && k->price == 0;
            resources += k->type == PK_COIN || k->type == PK_BOMB || k->type == PK_KEY;
        }
        if (G.room.secret_shop ? items != 0 : items < 1 || items > 2) {
            snprintf(buf, sizeof buf, "secret room holds %d free items (%s)", items, G.room.secret_shop ? "shop variant: 0" : "1 or 2");
            return buf;
        }
        if (resources == 0) return "secret room holds no coins / bombs / keys";
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
    in->active_pressed = in->bomb_pressed = in->pause_pressed = in->restart_pressed = in->confirm_pressed = false;
    in->debug_pressed = in->char_pressed = in->menu_pressed = in->click_pressed = false;
    in->number = -1;
    in->wheel = 0;
}

/* Debug-menu teleports: every room type, with and without enemies, must land in a
 * coherent state, and the bot must be able to finish the room and leave it. */
static int debug_rooms_run(const HeadlessOptions *o) {
    static const RoomType types[] = {ROOM_START, ROOM_COMBAT, ROOM_COMBAT, ROOM_COMBAT, ROOM_STATS, ROOM_SHOP, ROOM_WORKSHOP, ROOM_SECRET, ROOM_SECRET};
    static const RewardType rewards[] = {RW_NONE, RW_NONE, RW_UPGRADE, RW_ACTIVE, RW_NONE, RW_NONE, RW_NONE, RW_NONE, RW_NONE};
    static const int secret_shop[] = {0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const char *names[] = {"launch bay", "combat", "upgrade", "active", "stats", "shop", "workshop", "secret", "secret shop"};
    int failures = 0;
    for (int t = 0; t < ARRAY_LEN(types); t++) {
        for (int with = types[t] == ROOM_START ? 0 : 1; with >= 0; with--) {
            game_init(o->seed + (uint64_t)t);
            G.god = true;
            game_new_run();
            G.pl.coins += o->coins;
            debug_menu_open();
            G.secret_shop_override = secret_shop[t];
            debug_teleport(types[t], rewards[t], o->depth > 0 ? o->depth : 3, with);
            G.secret_shop_override = -1;
            G.state = GS_PLAYING;

            const char *err = NULL;
            if (G.room.type != types[t]) err = "teleported into the wrong room type";
            else if (with && (G.room.cleared || G.npk > 0)) err = "room with enemies starts cleared or with its reward out";
            else if (!with && (!G.room.cleared || G.nen + G.nsp > 0)) err = "room without enemies is not cleared";
            else if (!with && (err = check_room_reward()) != NULL) {}
            else if (!with && types[t] == ROOM_COMBAT && G.npk == 0) err = "empty combat room has no reward";

            int id = G.room.id, parent = G.parent.id, tick = 0;
            bool secret = G.room.type == ROOM_SECRET;
            float room_time = 0.0f;
            for (; !err && tick < o->max_ticks && G.room.id == id; tick++) {
                bot_input();
                game_update();
                clear_edges();
                room_time += DT;
                if (G.room.id == id) err = check_invariants(room_time);
            }
            if (!err && G.room.id == id) err = "the bot never left the room";
            else if (!err && secret && (G.room.id != parent || !G.room.revisit)) err = "secret room exit did not lead back";
            printf("debug teleport %-10s %-14s -> %s (%.1f s)\n", names[t], with ? "with enemies" : "no enemies",
                   err ? err : "OK", (double)tick / TICK_RATE);
            if (err) {
                if (o->fail_shot && failures == 0) save_failure_shot(o->fail_shot);
                failures++;
            }
        }
    }
    /* Drive the menu like a player: F3, down x2 (combat -> shop), right (NO ENEMIES), Enter. */
    game_init(o->seed);
    game_new_run();
    Input *in = &G.in;
    in->mouse = v2(0, 0);
    const char *steps = "F..D.D.R.E";
    for (const char *c = steps; *c; c++) {
        in->debug_pressed = *c == 'F';
        in->down = *c == 'D';
        in->right = *c == 'R';
        in->confirm_pressed = *c == 'E';
        game_update();
        clear_edges();
    }
    in->down = in->right = false;
    int priced = 0;
    for (int i = 0; i < G.npk; i++) priced += G.pk[i].price > 0;
    bool ok = G.state == GS_PLAYING && G.room.type == ROOM_SHOP && G.room.cleared && priced == SHOP_ITEMS;
    in->debug_pressed = true; /* F3 again opens it, Escape closes it */
    game_update();
    clear_edges();
    ok = ok && G.state == GS_DEBUG;
    in->pause_pressed = true;
    game_update();
    clear_edges();
    ok = ok && G.state == GS_PLAYING;
    printf("debug menu keyboard: F3, down, down, right, enter -> %s\n", ok ? "OK (empty shop)" : "FAILED");
    if (!ok) failures++;

    /* Scripted: a shield soaks a hit; SECOND WIND survives a lethal hit, which is resolved
     * from inside the enemy bullet loop while other bullets are still in flight. */
    {
        game_init(o->seed);
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        Player *pl = &G.pl;
        pl->invuln = 0.0f;
        pl->hp = 2;
        player_add_shield(2);
        for (int k = 0; k < 40; k++) ebullet_spawn(pl->pos, frand(0.0f, TAU_F), 1.0f, 8.0f, COL_WHITE);
        for (int t = 0; t < 10; t++) { game_update(); clear_edges(); }
        bool shield_ok = pl->shield == 1 && pl->hp == 2;

        pl->invuln = 0.0f;
        pl->shield = 0;
        pl->hp = 1;
        upgrade_apply(UPG_SECOND_WIND);
        for (int k = 0; k < 40; k++) ebullet_spawn(pl->pos, frand(0.0f, TAU_F), 1.0f, 8.0f, COL_WHITE);
        for (int t = 0; t < 10; t++) { game_update(); clear_edges(); }
        bool wind_ok = G.state == GS_PLAYING && pl->hp == 2 && pl->revives == 0 && G.neb >= 0 && G.neb <= MAX_EBULLETS;
        printf("scripted: shield soaks a hit -> %s, second wind -> %s\n", shield_ok ? "OK" : "FAILED", wind_ok ? "OK" : "FAILED");
        failures += !shield_ok + !wind_ok;
    }

    /* Scripted: the character select screen, driven with the keyboard like a player. */
    {
        game_init(o->seed);
        Input *k = &G.in;
        k->confirm_pressed = true; /* title -> character select */
        game_update();
        clear_edges();
        bool ok = G.state == GS_CHAR_SELECT && G.char_hover == CHAR_NORMAL;
        for (int step = 0; step < 4; step++) { /* right, release, right, release: NORMAL -> KNIGHT */
            k->right = step % 2 == 0;
            game_update();
            clear_edges();
        }
        ok = ok && G.char_hover == CHAR_KNIGHT && G.state == GS_CHAR_SELECT;
        k->confirm_pressed = true;
        game_update();
        clear_edges();
        const Player *pl = &G.pl;
        ok = ok && G.state == GS_PLAYING && G.character == CHAR_KNIGHT && sword_on() && pl->shield == 6 &&
             pl->hp == 6 && pl->speed < 540.0f && pl->upg_count[UPG_W_SWORD] == 1;
        /* Game over -> C -> character select -> 2 -> SHOTGUNNER. */
        game_over();
        G.gameover_t = 1.0f;
        k->char_pressed = true;
        game_update();
        clear_edges();
        ok = ok && G.state == GS_CHAR_SELECT;
        k->number = 1;
        game_update();
        clear_edges();
        k->confirm_pressed = true;
        game_update();
        clear_edges();
        ok = ok && G.state == GS_PLAYING && G.character == CHAR_SHOTGUNNER && upg_has(UPG_W_SCATTER) && !sword_on() &&
             pl->dmg_mult > 1.99f && pl->hit_radius > PLAYER_HIT_R && pl->shield == 0;
        printf("character select: title, right x2, enter -> knight; game over, C, 2, enter -> shotgunner -> %s\n", ok ? "OK" : "FAILED");
        failures += !ok;
    }

    /* Scripted: the Knight's sword cuts the enemy bullets in its arc (not those behind) and hurts enemies. */
    {
        game_init(o->seed);
        G.character = CHAR_KNIGHT;
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        Player *pl = &G.pl;
        pl->pos = v2(ARENA_CX, ARENA_CY);
        G.npk = 0;
        G.in.mouse = v2_add(pl->pos, v2(300.0f, 0.0f));
        int front = 0, behind = 0;
        for (int k = 0; k < 8; k++) {
            ebullet_spawn(v2_add(pl->pos, v2(50.0f + 8.0f * (float)k, -30.0f + 8.0f * (float)k)), 0.0f, 0.1f, 6.0f, COL_WHITE);
            front++;
        }
        for (int k = 0; k < 4; k++) {
            ebullet_spawn(v2_add(pl->pos, v2(-60.0f - 10.0f * (float)k, 0.0f)), PI_F, 0.1f, 6.0f, COL_WHITE);
            behind++;
        }
        Enemy dummy = {.type = EN_TANK, .uid = ++G.next_uid, .pos = v2_add(pl->pos, v2(95.0f, 0.0f)), .radius = 30.0f,
                       .hp = 5000.0f, .max_hp = 5000.0f};
        G.en[G.nen++] = dummy;
        G.in.fire = true;
        for (int t = 0; t < 12; t++) { game_update(); clear_edges(); }
        G.in.fire = false;
        int left = G.neb;
        float hp = G.nen > 0 ? G.en[0].hp : 5000.0f;
        bool ok = left == behind && hp < 5000.0f && pl->swing_id >= 1;
        printf("scripted: sword cuts %d/%d bullets in front, keeps %d behind, enemy hp %.0f -> %s\n", front - (left - behind > 0 ? left - behind : 0),
               front, left < behind ? left : behind, (double)hp, ok ? "OK" : "FAILED");
        failures += !ok;
    }

    /* Scripted: new upgrades - exact chances in descriptions (doubled by LOADED DICE), bonuses
     * while hurt, weapon room offers, DEFLECTOR SHELLS reflections, the SPREAD trade. */
    {
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        Player *pl = &G.pl;
        bool desc_ok = SDL_strstr(upgrade_desc(UPG_THERMITE), "15%") != NULL;
        upgrade_apply(UPG_THERMITE);
        desc_ok = desc_ok && SDL_strstr(upgrade_desc_at(UPG_THERMITE, 2), "30%") && SDL_strstr(upgrade_desc(UPG_THERMITE), "15%");
        upgrade_apply(UPG_LOADED_DICE);
        desc_ok = desc_ok && SDL_strstr(upgrade_desc_at(UPG_THERMITE, 1), "30%") && SDL_strstr(upgrade_desc_at(UPG_PAYLOAD, 1), "40%");

        upgrade_apply(UPG_WOUNDED_FURY);
        float full = upg_volley_damage_mult();
        pl->hp = pl->max_hp - 1;
        float wounded = upg_volley_damage_mult();
        bool hurt_ok = fabsf(full - 1.0f) < 1e-4f && fabsf(wounded - 1.3f) < 1e-4f;
        pl->hp = pl->max_hp;

        /* Upgrade rooms: 3 distinct upgrades not owned, the ones reshaping the shots among them. */
        upgrade_apply(UPG_W_NEEDLER);
        bool sig_ok = true, shot_seen = false;
        for (int k = 0; k < 60 && sig_ok; k++) {
            G.npk = 0;
            reward_drop(RW_UPGRADE, v2(ARENA_CX, ARENA_CY));
            for (int i = 0; i < G.npk; i++)
                for (int j = i + 1; j < G.npk; j++) sig_ok = sig_ok && G.pk[i].sub != G.pk[j].sub;
            for (int i = 0; i < G.npk; i++) {
                sig_ok = sig_ok && G.pk[i].type == PK_UPGRADE && G.pk[i].sub != UPG_W_NEEDLER && UPGRADES[G.pk[i].sub].tier == 0;
                shot_seen = shot_seen || upgrade_is_weapon(G.pk[i].sub);
            }
            sig_ok = sig_ok && G.npk == REWARD_CHOICES;
        }
        sig_ok = sig_ok && shot_seen;
        G.npk = 0;
        upgrade_toggle(UPG_W_NEEDLER); /* back to the plain blaster for the deflector check */

        upgrade_apply(UPG_DEFLECTOR);
        G.npb = G.neb = 0;
        pl->pos = v2(ARENA_CX, ARENA_CY + 200.0f);
        G.in.mouse = v2(ARENA_CX, ARENA_CY - 300.0f);
        for (int k = 0; k < 6; k++) ebullet_spawn(v2(ARENA_CX + (float)(k - 3) * 6.0f, ARENA_CY - 60.0f), PI_F * 0.5f, 60.0f, 7.0f, COL_WHITE);
        G.in.fire = true;
        int reflected = 0;
        for (int t = 0; t < 120; t++) {
            game_update();
            clear_edges();
            for (int i = 0; i < G.npb; i++) reflected += (G.pb[i].flags & PB_REFLECTED) != 0;
            if (reflected) break;
        }
        G.in.fire = false;
        bool deflect_ok = reflected > 0 && upg_fire_rate_mult() < 0.34f;

        float spread = pl->spread_mult;
        stat_apply(ST_SPREAD, true);
        bool spread_ok = fabsf(pl->spread_mult - spread * 0.75f) < 1e-4f && stat_can_change(ST_SPREAD, false);
        printf("scripted: chances in descriptions -> %s, wounded fury -> %s, upgrade rooms -> %s, deflector -> %s, spread -> %s\n",
               desc_ok ? "OK" : "FAILED", hurt_ok ? "OK" : "FAILED", sig_ok ? "OK" : "FAILED", deflect_ok ? "OK" : "FAILED",
               spread_ok ? "OK" : "FAILED");
        failures += !desc_ok + !hurt_ok + !sig_ok + !deflect_ok + !spread_ok;
    }

    /* Scripted: the five active items (none at the start, then dash, shield, boom, strength, chaos). */
    {
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        G.npk = 0;
        Player *pl = &G.pl;
        Input *in = &G.in;
        G.room.nobs = 0; /* open floor: nothing for the test shots to hit */
        pl->pos = v2(ARENA_CX - 300.0f, ARENA_CY);
        in->mouse = v2(ARENA_CX + 600.0f, ARENA_CY);
#define TICK() do { game_update(); clear_edges(); } while (0)
        in->active_pressed = true;
        TICK();
        int owned_acts[ACT_COUNT];
        bool none_ok = actives_owned(owned_acts) == 0 && pl->dash_t <= 0.0f && G.active_uses == 0;
#define ONLY(a) do { memset(pl->act, 0, sizeof pl->act); active_end_chaos(); active_give(a); } while (0)

        ONLY(ACT_DASH);
        float x0 = pl->pos.x;
        in->right = true;
        in->active_pressed = true;
        TICK();
        bool dash_ok = pl->dash_t > 0.0f && fabsf(pl->act[ACT_DASH].cd - pl->dash_cooldown) < 0.02f;
        in->active_pressed = true; /* still recharging: ignored */
        TICK();
        for (int t = 0; t < 30; t++) TICK();
        in->right = false;
        dash_ok = dash_ok && G.active_uses == 1 && pl->pos.x - x0 > 200.0f;

        G.god = false;
        ONLY(ACT_SHIELD);
        in->active_pressed = true;
        TICK();
        int hp = pl->hp + pl->shield;
        for (int t = 0; t < 120; t++) {
            for (int k = 0; k < 4; k++) ebullet_spawn(pl->pos, (float)k * 1.6f, 10.0f, 6.0f, COL_WHITE);
            TICK();
        }
        bool shield_ok = pl->hp + pl->shield == hp && active_shield_on();
        for (int t = 0; t < 300; t++) TICK();
        shield_ok = shield_ok && !active_shield_on() && pl->act[ACT_SHIELD].cd > 0.0f;
        G.neb = 0;
        G.god = true;

        ONLY(ACT_BOOM);
        Enemy dummy = {.type = EN_TANK, .uid = ++G.next_uid, .pos = v2(ARENA_CX + 300.0f, ARENA_CY), .radius = 30.0f,
                       .hp = 5000.0f, .max_hp = 5000.0f};
        G.en[G.nen++] = dummy;
        G.npb = 0;
        PBullet *b = pbullet_spawn(v2(ARENA_CX + 300.0f, ARENA_CY - 60.0f), v2(0.0f, -5.0f), 5.0f, 10.0f, COL_WHITE);
        if (b) b->life = 5.0f;
        in->active_pressed = true;
        TICK();
        bool boom_ok = G.nen > 0 && G.en[0].hp < 5000.0f && G.npb == 1 && G.pb[0].life > 0.0f;
        G.nen = G.npb = 0;

        ONLY(ACT_STRENGTH);
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        int normal = G.npb;
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->active_pressed = true;
        TICK();
        int strong = G.npb;
        in->fire = false;
        bool strength_ok = normal > 0 && strong == 4 * normal;
        G.npb = 0;

        ONLY(ACT_CHAOS);
        float dmg = pl->dmg_mult, rate = pl->rate_mult, speed = pl->speed;
        int owned = 0;
        for (int u = 0; u < UPG_COUNT; u++) owned += pl->upg_count[u];
        in->active_pressed = true;
        TICK();
        int lent = 0;
        bool chaos_ok = true;
        for (int u = 0; u < UPG_COUNT; u++)
            if (pl->chaos[u]) {
                lent++;
                chaos_ok = chaos_ok && pl->upg_count[u] == 0 && upg_has(u) == 1;
            }
        chaos_ok = chaos_ok && lent == active_chaos_count(1);
        for (int t = 0; t < (int)(active_duration(ACT_CHAOS, 1) * TICK_RATE) + 240; t++) TICK(); /* + hitstops */
        int after = 0, still = 0;
        for (int u = 0; u < UPG_COUNT; u++) {
            after += pl->upg_count[u];
            still += pl->chaos[u];
        }
        chaos_ok = chaos_ok && still == 0 && after == owned && pl->dmg_mult == dmg && pl->rate_mult == rate && pl->speed == speed;

        /* Taking an item owned again levels it up: longer, stronger, faster recharge. */
        ONLY(ACT_SHIELD);
        active_give(ACT_SHIELD);
        in->active_pressed = true;
        TICK();
        const ActiveSlot *sh = &pl->act[ACT_SHIELD];
        bool level_ok = sh->level == 2 && fabsf(sh->t - (active_duration(ACT_SHIELD, 2) - DT)) < 0.02f &&
                        active_duration(ACT_SHIELD, 2) > active_duration(ACT_SHIELD, 1) &&
                        fabsf(sh->cd_max - active_cooldown(ACT_SHIELD, 2)) < 0.02f && sh->cd_max < active_cooldown(ACT_SHIELD, 1);
        bool offered_held = false, offered_max = false;
        for (int k = 0; k < 30; k++) {
            G.npk = 0;
            active_spawn_choices(v2(ARENA_CX, ARENA_CY));
            for (int i = 0; i < G.npk; i++) offered_held |= G.pk[i].sub == ACT_SHIELD;
        }
        for (int k = 0; k < 10; k++) active_give(ACT_SHIELD);
        for (int k = 0; k < 30; k++) {
            G.npk = 0;
            active_spawn_choices(v2(ARENA_CX, ARENA_CY));
            for (int i = 0; i < G.npk; i++) offered_max |= G.pk[i].sub == ACT_SHIELD;
            level_ok = level_ok && G.npk == REWARD_CHOICES;
        }
        G.npk = 0;
        level_ok = level_ok && offered_held && !offered_max && sh->level == MAX_ACTIVE_LEVEL;
        failures += !level_ok;
        printf("scripted actives: level up (offered while it can level, never at max %d) -> %s\n", MAX_ACTIVE_LEVEL, level_ok ? "OK" : "FAILED");

        /* Several items at once: one press triggers every one turned on and ready, each then
         * recharges on its own; one turned off is never triggered, and turning it off ends its
         * effect; taking a new one keeps the others. */
        memset(pl->act, 0, sizeof pl->act);
        for (int t = 0; t < 30; t++) TICK();
        G.neb = G.npb = 0;
        active_give(ACT_NOVA);
        active_give(ACT_SHIELD);
        active_give(ACT_STRENGTH);
        active_give(ACT_NOVA);
        int uses0 = G.active_uses;
        bool multi_ok = actives_owned(owned_acts) == 3 && pl->act[ACT_NOVA].level == 2 && pl->act[ACT_SHIELD].level == 1;
        in->active_pressed = true;
        TICK();
        multi_ok = multi_ok && G.active_uses == uses0 + 1 && active_shield_on() && active_strength_on() && G.npb >= 28 &&
                   pl->act[ACT_NOVA].cd > 0.0f && pl->act[ACT_SHIELD].cd > 0.0f && pl->act[ACT_NOVA].cd != pl->act[ACT_SHIELD].cd;
        multi_ok = multi_ok && active_toggle(ACT_SHIELD) && !active_shield_on() && pl->act[ACT_SHIELD].off && active_strength_on();
        multi_ok = multi_ok && !active_toggle(ACT_BLINK); /* not owned */
        for (int t = 0; t < 30 * TICK_RATE; t++) TICK(); /* everything recharged */
        in->active_pressed = true;
        TICK();
        multi_ok = multi_ok && !active_shield_on() && pl->act[ACT_SHIELD].cd == 0.0f && active_strength_on() && pl->act[ACT_NOVA].cd > 0.0f;
        active_toggle(ACT_SHIELD);
        G.npk = 0;
        for (int k = 0; k < 30; k++) { /* one owned (not at max) can still be offered, as a level up */
            G.npk = 0;
            active_spawn_choices(v2(ARENA_CX, ARENA_CY));
            for (int i = 0; i < G.npk; i++)
                for (int j = i + 1; j < G.npk; j++) multi_ok = multi_ok && G.pk[i].sub != G.pk[j].sub;
        }
        G.npk = G.npb = G.neb = 0;
        failures += !multi_ok;
        printf("scripted actives: several at once (one press, own recharges, turned off = never used) -> %s\n", multi_ok ? "OK" : "FAILED");
#undef ONLY
#undef TICK
        printf("scripted actives: none -> %s, dash -> %s, shield -> %s, boom -> %s, strength %d -> %d shots -> %s, chaos %d upgrades -> %s\n",
               none_ok ? "OK" : "FAILED", dash_ok ? "OK" : "FAILED", shield_ok ? "OK" : "FAILED", boom_ok ? "OK" : "FAILED",
               normal, strong, strength_ok ? "OK" : "FAILED", lent, chaos_ok ? "OK" : "FAILED");
        failures += !none_ok + !dash_ok + !shield_ok + !boom_ok + !strength_ok + !chaos_ok;
    }

    /* Scripted: the newer active items, special weapons and a few upgrades, one effect each. */
    {
#define TICK() do { game_update(); clear_edges(); } while (0)
#define FRESH() do { game_init(o->seed); G.god = true; game_new_run(); debug_teleport(ROOM_COMBAT, RW_COINS, 3, false); \
                     G.state = GS_PLAYING; G.npk = 0; G.room.nobs = 0; G.pl.pos = v2(ARENA_CX - 300.0f, ARENA_CY); \
                     G.in.mouse = v2(ARENA_CX + 600.0f, ARENA_CY); } while (0)
#define DUMMY(x, y) do { Enemy d = {.type = EN_TANK, .uid = ++G.next_uid, .pos = v2(x, y), .radius = 30.0f, \
                                    .hp = 5000.0f, .max_hp = 5000.0f}; G.en[G.nen++] = d; } while (0)
        Player *pl = &G.pl;
        Input *in = &G.in;
        const char *names[32];
        bool oks[32];
        int nres = 0;

        FRESH(); /* REVERSAL: every enemy bullet becomes ours */
        active_give(ACT_REVERSAL);
        for (int k = 0; k < 12; k++) ebullet_spawn(v2(ARENA_CX + (float)k * 20.0f, ARENA_CY - 200.0f), PI_F * 0.5f, 50.0f, 6.0f, COL_WHITE);
        in->active_pressed = true;
        TICK();
        int turned = 0;
        for (int i = 0; i < G.npb; i++) turned += (G.pb[i].flags & PB_REFLECTED) != 0;
        names[nres] = "reversal", oks[nres++] = G.neb == 0 && turned == 12;

        FRESH(); /* TURRET: shoots on its own */
        active_give(ACT_TURRET);
        DUMMY(ARENA_CX + 200.0f, ARENA_CY);
        in->active_pressed = true;
        for (int t = 0; t < 90; t++) TICK();
        names[nres] = "turret", oks[nres++] = G.nen > 0 && G.en[0].hp < 5000.0f && active_running(ACT_TURRET);

        FRESH(); /* BLINK: to the cursor */
        active_give(ACT_BLINK);
        V2 from = pl->pos;
        in->mouse = v2(from.x + 400.0f, from.y);
        in->active_pressed = true;
        TICK();
        names[nres] = "blink", oks[nres++] = fabsf(pl->pos.x - (from.x + 400.0f)) < 20.0f && pl->invuln > 0.0f;

        FRESH(); /* NOVA: a full ring */
        active_give(ACT_NOVA);
        G.npb = 0;
        in->active_pressed = true;
        TICK();
        names[nres] = "nova", oks[nres++] = G.npb == 24;

        FRESH(); /* TIME STOP: enemy bullets frozen in place */
        active_give(ACT_TIME_STOP);
        EBullet *eb = ebullet_spawn(v2(ARENA_CX + 300.0f, ARENA_CY - 300.0f), 0.0f, 300.0f, 6.0f, COL_WHITE);
        V2 eb0 = eb ? eb->pos : v2(0, 0);
        in->active_pressed = true;
        for (int t = 0; t < 60; t++) TICK();
        names[nres] = "time stop", oks[nres++] = G.neb == 1 && v2_dist(G.eb[0].pos, eb0) < 4.0f;

        FRESH(); /* BLADE STORM: cuts the bullets around the ship */
        active_give(ACT_BLADES);
        in->active_pressed = true;
        TICK();
        for (int k = 0; k < 16; k++) ebullet_spawn(v2_add(pl->pos, v2_scale(v2_angle(TAU_F * (float)k / 16.0f), 88.0f)), 0.0f, 0.1f, 6.0f, COL_WHITE);
        for (int t = 0; t < 120; t++) TICK();
        names[nres] = "blade storm", oks[nres++] = G.neb == 0;

        FRESH(); /* ROCKETS: the explosion hurts an enemy next to the one hit */
        upgrade_apply(UPG_W_ROCKETS);
        DUMMY(ARENA_CX + 200.0f, ARENA_CY);
        DUMMY(ARENA_CX + 200.0f, ARENA_CY + 75.0f);
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        for (int t = 0; t < 200; t++) TICK();
        names[nres] = "rockets", oks[nres++] = G.nen == 2 && G.en[0].hp < 5000.0f && G.en[1].hp < 5000.0f;

        FRESH(); /* CLUSTER: bursts into fragments */
        upgrade_apply(UPG_W_CLUSTER);
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        int frags = 0;
        for (int t = 0; t < 120 && !frags; t++) {
            TICK();
            for (int i = 0; i < G.npb; i++) frags += (G.pb[i].flags & PB_FRAGMENT) != 0;
        }
        names[nres] = "cluster", oks[nres++] = frags == 6;

        FRESH(); /* CHAKRAM: comes back to the ship */
        upgrade_apply(UPG_W_CHAKRAM);
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        bool returned = false;
        for (int t = 0; t < 240 && G.npb > 0; t++) {
            TICK();
            for (int i = 0; i < G.npb; i++) returned |= (G.pb[i].flags & PB_RETURNED) != 0;
        }
        names[nres] = "chakram", oks[nres++] = returned && G.npb == 0;

        FRESH(); /* EMERGENCY SHIELD: the first hit of the room is blocked */
        G.god = false;
        upgrade_apply(UPG_EMERGENCY);
        int hp0 = pl->hp;
        pl->invuln = 0.0f; /* the teleport's grace period */
        player_hurt(1);
        bool blocked = pl->hp == hp0;
        pl->invuln = 0.0f;
        player_hurt(1);
        names[nres] = "emergency shield", oks[nres++] = blocked && pl->hp == hp0 - 1;

        FRESH(); /* SPIKED HULL: contact damage */
        upgrade_apply(UPG_SPIKED_HULL);
        pl->invuln = 100.0f; /* no contact hit: only the hull's damage */
        DUMMY(pl->pos.x + 30.0f, pl->pos.y);
        for (int t = 0; t < 10; t++) TICK();
        names[nres] = "spiked hull", oks[nres++] = G.nen > 0 && G.en[0].hp < 5000.0f;

        /* Contact hits hurt the rammer too: a chaser dies on the spot, a tank survives, and a
         * dodged contact (invulnerable) hurts nobody. */
        FRESH();
        G.god = false;
        pl->invuln = 0.0f; /* entering a room grants a moment of invulnerability */
        int hp_before = pl->hp + pl->shield;
        Enemy rusher = {.type = EN_CHASER, .uid = ++G.next_uid, .pos = v2(pl->pos.x + 10.0f, pl->pos.y),
                        .radius = ENEMIES[EN_CHASER].radius, .hp = ENEMIES[EN_CHASER].hp, .max_hp = ENEMIES[EN_CHASER].hp};
        G.en[G.nen++] = rusher;
        for (int t = 0; t < 3; t++) TICK();
        bool contact_ok = pl->hp + pl->shield == hp_before - 1 && G.nen == 0;
        pl->invuln = G.hitstop = 0.0f; /* the hit's freeze frames would skip the next ticks */
        DUMMY(pl->pos.x + 30.0f, pl->pos.y);
        TICK();
        contact_ok = contact_ok && G.nen == 1 && G.en[0].hp <= 5000.0f - CONTACT_DAMAGE + 0.01f && G.en[0].hp > 0.0f;
        float tank_hp = G.nen ? G.en[0].hp : 0.0f;
        G.en[0].pos = pl->pos; /* back on the ship, still invulnerable from the hit */
        G.hitstop = 0.0f;
        TICK();
        contact_ok = contact_ok && G.nen == 1 && G.en[0].hp == tank_hp;
        G.god = true;
        G.nen = 0;
        names[nres] = "contact hurts the rammer", oks[nres++] = contact_ok;

        FRESH(); /* weapon mods combine: scatter x8, wave x2 -> 16 shots; scatter off -> 2 */
        int shots[4];
        shots[0] = (int)gun_volley_shots();
        upgrade_apply(UPG_W_SCATTER);
        shots[1] = (int)gun_volley_shots();
        upgrade_apply(UPG_W_WAVE);
        shots[2] = (int)gun_volley_shots();
        upgrade_toggle(UPG_W_SCATTER);
        shots[3] = (int)gun_volley_shots();
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        int waves = 0;
        for (int i = 0; i < G.npb; i++) waves += (G.pb[i].flags & PB_WAVE) != 0;
        names[nres] = "weapon mods combine", oks[nres++] = shots[0] == 1 && shots[1] == 8 && shots[2] == 16 && shots[3] == 2 &&
                                                         waves == 2 && gun_main_mod() == UPG_W_WAVE;

        FRESH(); /* every character can find the sword */
        bool sword_seen = false;
        for (int k = 0; k < 400 && !sword_seen; k++) sword_seen = upgrade_roll() == UPG_W_SWORD;
        names[nres] = "sword for everyone", oks[nres++] = sword_seen && !upgrade_capped(UPG_W_SWORD);

        /* An upgrade owned never comes back: not rolled, not offered, not taken twice; an offer
         * left on the floor turns into another one once the same upgrade is taken elsewhere. */
        FRESH();
        upgrade_apply(UPG_W_SEEKER);
        upgrade_apply(UPG_RAPID);
        upgrade_apply(UPG_RAPID);
        bool once_ok = pl->upg_count[UPG_RAPID] == 1 && upgrade_capped(UPG_RAPID) && upgrade_capped(UPG_W_SEEKER);
        for (int k = 0; k < 300; k++) once_ok = once_ok && upgrade_roll() != UPG_RAPID && upgrade_roll() != UPG_W_SEEKER;
        G.npk = 0;
        reward_drop(RW_UPGRADE, v2(ARENA_CX, ARENA_CY));
        int first = G.pk[0].sub;
        upgrade_apply(first); /* as if found elsewhere */
        pickups_refresh_owned();
        for (int i = 0; i < G.npk; i++)
            once_ok = once_ok && (G.pk[i].type != PK_UPGRADE || (G.pl.upg_count[G.pk[i].sub] == 0 && G.pk[i].sub != first));
        once_ok = once_ok && G.npk == REWARD_CHOICES;
        /* Every upgrade owned: rooms fall back to stat trades. */
        for (int u = 0; u < UPG_COUNT; u++) pl->upg_count[u] = 1;
        player_recompute();
        G.npk = 0;
        reward_drop(RW_UPGRADE, v2(ARENA_CX, ARENA_CY));
        once_ok = once_ok && upgrade_roll() < 0 && upgrade_fresh() < 0 && G.npk == REWARD_CHOICES;
        for (int i = 0; i < G.npk; i++) once_ok = once_ok && G.pk[i].type == PK_STAT;
        G.npk = 0;
        names[nres] = "owned upgrades never come back", oks[nres++] = once_ok;

        /* Every upgrade has a bonus AND a malus; a malus only applies while its upgrade is
         * turned on, and CHAOS lends upgrades without theirs. */
        bool mal_ok = true;
        for (int u = 0; u < UPG_COUNT; u++)
            mal_ok = mal_ok && MALUSES[u].kind > MAL_NONE && MALUSES[u].kind < MAL_COUNT &&
                     (MALUSES[u].kind == MAL_CUSTOM ? MALUSES[u].text != NULL : MALUSES[u].amount > 0) && upgrade_malus_desc(u)[0];
        FRESH();
        float dmg0 = pl->dmg_mult;
        int price0 = shop_price(PK_UPGRADE, UPG_RAPID);
        upgrade_apply(UPG_HAIR_TRIGGER); /* +30% fire rate, -10% damage */
        mal_ok = mal_ok && fabsf(pl->dmg_mult - dmg0 * 0.9f) < 1e-4f && stats_in_sync();
        upgrade_toggle(UPG_HAIR_TRIGGER);
        mal_ok = mal_ok && pl->dmg_mult == dmg0 && stats_in_sync();
        upgrade_apply(UPG_FOUR_LEAF); /* prices +20% */
        mal_ok = mal_ok && shop_price(PK_UPGRADE, UPG_RAPID) == (int)roundf((float)price0 * 1.2f);
        pl->chaos[UPG_GUILLOTINE] = true;
        mal_ok = mal_ok && malus(MAL_FIRST_HIT) == 0 && upg_has(UPG_GUILLOTINE) == 1;
        pl->chaos[UPG_GUILLOTINE] = false;
        /* first hit of the room: +1 half-heart, then normal */
        upgrade_apply(UPG_GUILLOTINE);
        G.god = false;
        pl->invuln = 0.0f;
        int hp_m = pl->hp;
        player_hurt(1);
        int first_hit = hp_m - pl->hp;
        pl->invuln = 0.0f;
        player_hurt(1);
        mal_ok = mal_ok && first_hit == 2 && hp_m - pl->hp == 3;
        G.god = true;
        G.hitstop = 0.0f;
        /* spin-up: no shot for 0.3 s after pulling the trigger */
        FRESH();
        upgrade_apply(UPG_MOMENTUM);
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->fire = true;
        for (int t = 0; t < 24; t++) TICK(); /* 0.2 s */
        int early = G.npb;
        for (int t = 0; t < 24; t++) TICK();
        mal_ok = mal_ok && early == 0 && G.npb > 0;
        /* overheat: OVERCLOCK jams the gun after 3 s of non-stop fire; turned off, it never does */
        upgrade_apply(UPG_RAPID);
        bool jammed = false;
        for (int t = 0; t < (int)((OVERHEAT_AFTER + 0.5f) * TICK_RATE); t++) {
            TICK();
            if (pl->jam_t > 0.0f) {
                int before = G.npb;
                TICK();
                jammed = G.npb <= before; /* nothing fired while jammed (shots only die) */
                break;
            }
        }
        upgrade_toggle(UPG_RAPID);
        mal_ok = mal_ok && jammed && malus(MAL_OVERHEAT) == 0;
        in->fire = false;
        G.npb = 0;
        names[nres] = "maluses", oks[nres++] = mal_ok;
#undef DUMMY
#undef FRESH
#undef TICK
        printf("scripted new content:");
        for (int i = 0; i < nres; i++) {
            printf(" %s -> %s%s", names[i], oks[i] ? "OK" : "FAILED", i + 1 < nres ? "," : "\n");
            failures += !oks[i];
        }
    }

    /* Scripted: turning upgrades off removes exactly their effect, turning them back on restores
     * it (never heals); the TAB menu does it with the keyboard / mouse, from a run or the pause screen. */
    {
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        G.npk = 0;
        Player *pl = &G.pl;
        Input *in = &G.in;
        Stats base;
        stats_compute(&base, false); /* nothing owned yet */
        static const int give[] = {UPG_HULL, UPG_GLASS_CANNON, UPG_RAPID, UPG_BIG_IRON, UPG_MULTI, UPG_FOUR_LEAF,
                                   UPG_THRUSTER, UPG_TESLA, UPG_W_SEEKER, UPG_W_RAILGUN, UPG_SKELETON_KEY, UPG_DRONE};
        for (int i = 0; i < ARRAY_LEN(give); i++) upgrade_apply(give[i]);
        Stats on;
        stats_compute(&on, false);
        bool sync = stats_in_sync();
        int list[UPG_COUNT];
        int n = upgrade_menu_list(list);
        bool order_ok = n == 12;
        for (int i = 1; i < n; i++) order_ok = order_ok && list[i - 1] < list[i];
        int hp0 = pl->hp;
        for (int i = 0; i < n; i++) upgrade_toggle(list[i]);
        bool off_ok = pl->dmg_mult == base.dmg_mult && pl->rate_mult == base.rate_mult && pl->max_hp == base.max_hp &&
                      pl->extra_proj == 0 && pl->drones == 0 && pl->luck == base.luck && pl->speed == base.speed &&
                      gun_main_mod() < 0 && upg_has(UPG_TESLA) == 0 && pl->upg_off[UPG_HULL] &&
                      !pl->upg_off[UPG_SKELETON_KEY] && !upgrade_toggleable(UPG_SKELETON_KEY) && stats_in_sync();
        int hp_off = pl->hp;
        for (int i = 0; i < n; i++) upgrade_toggle(list[i]);
        bool on_ok = pl->dmg_mult == on.dmg_mult && pl->rate_mult == on.rate_mult && pl->max_hp == on.max_hp &&
                     pl->extra_proj == on.extra_proj && pl->drones == on.drones && pl->hp == hp_off && hp_off <= hp0 &&
                     upg_has(UPG_W_RAILGUN) == 1 && gun_main_mod() == UPG_W_RAILGUN && stats_in_sync();
        /* A stat trade while an upgrade is off: it stays when the upgrade comes back. */
        upgrade_toggle(UPG_RAPID);
        stat_apply(ST_FIRE_RATE, true);
        upgrade_toggle(UPG_RAPID);
        bool trade_ok = fabsf(pl->rate_mult - on.rate_mult * 1.25f) < 1e-4f && stats_in_sync();

        /* The menu: TAB opens it (world frozen), right + enter turns the 2nd entry off, a click turns
         * the 1st one off, TAB closes it; from the pause screen it goes back to the pause screen. */
        float t0 = G.run_time;
        in->menu_pressed = true;
        game_update();
        clear_edges();
        bool menu_ok = G.state == GS_UPGRADES;
        in->right = true;
        game_update();
        clear_edges();
        in->right = false;
        in->confirm_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && pl->upg_off[list[1]] && !pl->upg_off[list[0]];
        /* first cell: centred grid, 4 columns of 430 px, first row at y 215 */
        in->mouse = v2((SCREEN_W - (4.0f * 430.0f + 3.0f * 12.0f)) * 0.5f + 100.0f, 215.0f + 20.0f);
        in->confirm_pressed = in->click_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && pl->upg_off[list[0]] && G.run_time == t0 && stats_in_sync();
        in->menu_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && G.state == GS_PLAYING;
        in->pause_pressed = true;
        game_update();
        clear_edges();
        in->menu_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && G.state == GS_UPGRADES;
        in->pause_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && G.state == GS_PAUSED;
        /* Active items come first in the menu and turn off / on the same way. */
        G.state = GS_PLAYING;
        active_give(ACT_NOVA);
        in->menu_pressed = true;
        game_update();
        clear_edges();
        in->mouse = v2((SCREEN_W - (4.0f * 430.0f + 3.0f * 12.0f)) * 0.5f + 100.0f, 215.0f + 20.0f);
        in->click_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && pl->act[ACT_NOVA].off;
        in->click_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && !pl->act[ACT_NOVA].off;
        in->menu_pressed = true;
        game_update();
        clear_edges();
        menu_ok = menu_ok && G.state == GS_PLAYING;
        printf("scripted upgrades menu: order -> %s, all off -> %s, all back on (no heal) -> %s, trade kept -> %s, TAB menu -> %s\n",
               order_ok ? "OK" : "FAILED", off_ok && sync ? "OK" : "FAILED", on_ok ? "OK" : "FAILED", trade_ok ? "OK" : "FAILED",
               menu_ok ? "OK" : "FAILED");
        failures += !order_ok + !(off_ok && sync) + !on_ok + !trade_ok + !menu_ok;
    }

    printf("debug teleports - %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}

int headless_run(const HeadlessOptions *o) {
    if (o->debug_rooms) return debug_rooms_run(o);
    int failures = 0;
    int runs = o->runs > 0 ? o->runs : 1;
    long long total_ticks = 0;
    int best = 0, sum_rooms = 0;
    uint64_t t0 = SDL_GetPerformanceCounter();

    for (int run = 0; run < runs; run++) {
        uint64_t seed = o->seed + (uint64_t)run;
        game_init(seed);
        G.god = o->god;
        G.force_secret = o->secret;
        G.character = o->character;
        game_new_run();
        const CharDef *cd = &CHARACTERS[G.character];
        int start_acts[ACT_COUNT];
        const char *start_err = G.pl.max_hp != cd->hearts * 2 || G.pl.hp != G.pl.max_hp || G.pl.shield != cd->armor * 2 ||
                                        (cd->start_upgrade >= 0 && G.pl.upg_count[cd->start_upgrade] != 1) ||
                                        G.pl.hit_radius != cd->hit_radius || G.pl.dmg_mult != cd->dmg_mult
                                    ? "run did not start with the character's stats / weapon mod"
                                : actives_owned(start_acts) > 0 ? "run started with an active item (they must be found)" : NULL;
        for (int a = ACT_NONE + 1; a < ACT_COUNT; a++)
            if (o->actives & (1u << a)) active_give(a);
        if (o->weapon >= 0 && !upgrade_capped(o->weapon)) upgrade_apply(o->weapon);
        G.bot_toggles = o->toggle_upgrades;
        G.pl.coins += o->coins;
        if (o->all_upgrades)
            for (int u = 0; u < UPG_COUNT; u++)
                if (!upgrade_capped(u)) upgrade_apply(u);
        if (o->hp > 0) { /* max hp (at least 2) and hp, after the upgrades */
            G.pl.base.max_hp += o->hp - G.pl.max_hp;
            player_recompute();
            G.pl.hp = o->hp < G.pl.max_hp ? o->hp : G.pl.max_hp;
        }
        int start_revives = G.pl.revives;
        if (o->stats_room) room_enter(ROOM_STATS, RW_NONE, o->depth > 0 ? o->depth : 1);
        else if (o->shop_room) room_enter(ROOM_SHOP, RW_NONE, o->depth > 0 ? o->depth : 1);
        else if (o->workshop_room) room_enter(ROOM_WORKSHOP, RW_NONE, o->depth > 0 ? o->depth : 3);
        else if (o->active_room) room_enter(ROOM_COMBAT, RW_ACTIVE, o->depth > 0 ? o->depth : 1);
        else if (o->depth > 0) room_enter(ROOM_COMBAT, RW_UPGRADE, o->depth);

        int room_id = G.room.id, secret_parent = -1;
        int stats_rooms = G.room.type == ROOM_STATS, shops = G.room.type == ROOM_SHOP, secrets = 0;
        int workshops = G.room.type == ROOM_WORKSHOP;
        bool was_cleared = G.room.cleared;
        int choices = 0; /* one-of-N pickups offered when the room was cleared */
        float room_time = 0.0f;
        const char *err = start_err ? start_err
                          : G.room.cleared != G.room.peaceful
                              ? "first room: only the launch bay / secret rooms start without enemies" : NULL;
        int tick = 0, hits = 0, life = G.pl.hp + G.pl.shield;
        for (; !err && tick < o->max_ticks && G.state != GS_GAMEOVER; tick++) {
            bot_input();
            game_update();
            clear_edges();
            hits += G.pl.hp + G.pl.shield < life; /* hull or shield damage taken this tick */
            life = G.pl.hp + G.pl.shield;
            if (G.room.id != room_id) {
                int from = room_id;
                room_id = G.room.id;
                room_time = 0.0f;
                if (G.room.revisit) {
                    /* Leaving a secret room: back in the (cleared) room it was found in. */
                    if (G.room.id != secret_parent || !G.room.cleared) {
                        err = "secret room exit did not lead back to the room it was found in";
                        break;
                    }
                    was_cleared = true;
                    choices = count_choices();
                } else {
                    if (G.room.cleared != G.room.peaceful) {
                        err = "new room: every room starts with enemies, except the launch bay and secret rooms";
                        break;
                    }
                    if (G.room.type == ROOM_SECRET) {
                        secret_parent = from;
                        secrets++;
                        if (!G.parent.cleared || G.parent.id != from) {
                            err = "entered a secret room from a room that was not cleared";
                            break;
                        }
                    }
                    stats_rooms += G.room.type == ROOM_STATS;
                    shops += G.room.type == ROOM_SHOP;
                    workshops += G.room.type == ROOM_WORKSHOP;
                    was_cleared = false;
                }
            }
            if (G.room.cleared && !was_cleared) {
                if ((err = check_room_reward()) != NULL) break;
                if (!G.room.revisit && v2_dist(G.pl.pos, v2(ARENA_CX, ARENA_Y1 - 80.0f)) > 1.0f) {
                    err = "room cleared but the ship was not moved back to the entry spot";
                    break;
                }
                choices = count_choices();
            }
            was_cleared = G.room.cleared;
            /* A one-of-N choice goes all at once: never a leftover after picking one. */
            if (G.room.cleared && count_choices() != 0 && count_choices() != choices) {
                err = "some one-of-N choices remained after one was taken";
                break;
            }
            room_time += DT;
            if ((err = check_invariants(room_time)) != NULL) break;
        }
        total_ticks += tick;

        const char *outcome = err ? "FAIL" : G.state == GS_GAMEOVER ? "died" : "time limit";
        const Player *p = &G.pl;
        int owned = 0, off = 0;
        for (int u = 0; u < UPG_COUNT; u++) {
            owned += p->upg_count[u] > 0;
            off += p->upg_count[u] > 0 && p->upg_off[u];
        }
        printf("run %s seed=%-6llu rooms=%-3d depth=%-3d kills=%-5d time=%5.0fs hp=%d/%d gun=",
               character_option_name(G.character), (unsigned long long)seed, G.rooms_cleared, G.room.depth, G.kills, (double)G.run_time, G.pl.hp, G.pl.max_hp);
        for (int u = UPG_W_SCATTER; u <= UPG_W_SWORD; u++)
            if (upg_has(u)) printf("%.4s+", UPGRADES[u].name);
        printf("%s upgrades=%d off=%d toggles=%d ", gun_main_mod() < 0 ? "BLAS" : "", owned, off, G.toggles);
        printf("hits=%d coins=%d shops=%d bought=%d secrets=%d stats_rooms=%d", hits, p->coins, shops, G.items_bought, secrets, stats_rooms);
        int acts[ACT_COUNT], na = actives_owned(acts);
        printf(" actives=");
        for (int i = 0; i < na; i++) printf("%s%d%s", active_option_name(acts[i]), p->act[acts[i]].level, i + 1 < na ? "+" : "");
        printf("%s uses=%d", na ? "" : "none", G.active_uses);
        if (workshops) printf(" workshops=%d machine_uses=%d augments=%d", workshops, G.machine_uses, G.augments);
        if (start_revives > p->revives) printf(" second_winds=%d", start_revives - p->revives);
        if (stats_rooms)
            printf(" [dmg x%.2f rate x%.2f range x%.2f speed x%.2f recharge x%.2f hitbox %.1f luck %+d]", (double)p->dmg_mult,
                   (double)p->rate_mult, (double)p->range_mult, (double)(p->speed / 540.0f), (double)(p->dash_cooldown / DASH_BASE_CD),
                   (double)p->hit_radius, p->luck);
        printf(" -> %s\n", outcome);
        if (err) {
            printf("  invariant violated at tick %d: %s\n", tick, err);
            if (o->fail_shot && failures == 0) save_failure_shot(o->fail_shot);
            failures++;
        } else if (o->shop_room && o->coins >= 30 && G.items_bought == 0) {
            printf("  started in a shop with %d coins but never bought anything\n", G.pl.coins);
            failures++;
        } else if (o->workshop_room && (G.machine_uses == 0 || G.augments == 0)) {
            printf("  started in a workshop but never used a machine (%d) or took an augment (%d)\n", G.machine_uses, G.augments);
            failures++;
        } else if ((o->actives || o->active_room) && G.active_uses == 0) {
            printf("  had an active item but never used it\n");
            failures++;
        } else if (o->active_room && na == 0) {
            printf("  started in an active room but never took an active item\n");
            failures++;
        } else if (o->toggle_upgrades && G.toggles == 0) {
            printf("  --toggle-upgrades but the bot never turned an upgrade off / on\n");
            failures++;
        } else if (o->secret && secrets == 0) {
            printf("  every room hid a secret but the bot never entered one\n");
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
