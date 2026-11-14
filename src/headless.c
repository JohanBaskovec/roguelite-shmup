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
    for (int w = 0; w < WPN_COUNT; w++)
        if (p->wlevel[w] > 0 && !weapon_available(w)) return "player owns a weapon its character cannot have (sword?)";
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (k->type == PK_WEAPON && !weapon_available(k->sub)) return "weapon offered that this character cannot have";
        if (k->type == PK_UPGRADE && UPGRADES[k->sub].tier > 0) return "workshop-exclusive upgrade offered outside the augment machine";
        if (k->type == PK_AUGMENT && (G.room.type != ROOM_WORKSHOP || UPGRADES[k->sub].tier == 0 || !k->choice))
            return "augment pad outside a workshop (or not an exclusive one-of-N choice)";
    }
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (k->type != PK_ACTIVE) continue;
        if (k->sub <= ACT_NONE || k->sub >= ACT_COUNT || (k->sub == p->active && p->act_level >= MAX_ACTIVE_LEVEL))
            return "invalid active item offered (or the one held, already at max level)";
        if (!k->choice || G.room.type != ROOM_COMBAT || G.room.reward != RW_ACTIVE) return "active item outside an active room's choice";
    }
    if (p->active < ACT_NONE || p->active >= ACT_COUNT) return "active item id out of range";
    if (p->active == ACT_NONE ? p->act_level != 0 : p->act_level < 1 || p->act_level > MAX_ACTIVE_LEVEL) return "active item level out of range";
    if (p->act_cd < 0.0f || p->act_t < 0.0f || !isfinite(p->act_cd) || !isfinite(p->act_t)) return "active item timers invalid";
    if (p->dash_t > 0.0f && p->active != ACT_DASH) return "dashing without the dash active item";
    {
        int lent = 0;
        bool chaos_on = p->active == ACT_CHAOS && p->act_t > 0.0f;
        for (int u = 0; u < UPG_COUNT; u++) {
            if (!p->chaos[u]) continue;
            lent++;
            if (!chaos_on) return "CHAOS upgrades still lent after the effect ended";
            if (p->upg_count[u] > 0) return "CHAOS lent an upgrade the player already owns";
        }
        if (lent > active_chaos_count(p->act_level)) return "CHAOS lent too many upgrades";
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
    bool item_room = G.room.type == ROOM_COMBAT && (G.room.reward == RW_WEAPON || G.room.reward == RW_UPGRADE || G.room.reward == RW_ACTIVE);
    if (item_room && count_choices() != REWARD_CHOICES) {
        snprintf(buf, sizeof buf, "%s room offers %d items to choose from instead of %d",
                 reward_name(G.room.reward), count_choices(), REWARD_CHOICES);
        return buf;
    }
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
            items += (k->type == PK_WEAPON || k->type == PK_UPGRADE) && !k->choice && k->price == 0;
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
    in->debug_pressed = in->char_pressed = false;
    in->weapon_select = -1;
    in->weapon_cycle = 0;
}

/* Debug-menu teleports: every room type, with and without enemies, must land in a
 * coherent state, and the bot must be able to finish the room and leave it. */
static int debug_rooms_run(const HeadlessOptions *o) {
    static const RoomType types[] = {ROOM_START, ROOM_COMBAT, ROOM_COMBAT, ROOM_COMBAT, ROOM_COMBAT, ROOM_STATS, ROOM_SHOP, ROOM_WORKSHOP, ROOM_SECRET, ROOM_SECRET};
    static const RewardType rewards[] = {RW_NONE, RW_NONE, RW_WEAPON, RW_UPGRADE, RW_ACTIVE, RW_NONE, RW_NONE, RW_NONE, RW_NONE, RW_NONE};
    static const int secret_shop[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const char *names[] = {"launch bay", "combat", "weapon", "upgrade", "active", "stats", "shop", "workshop", "secret", "secret shop"};
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
        ok = ok && G.state == GS_PLAYING && G.character == CHAR_KNIGHT && pl->weapon == WPN_SWORD && pl->shield == 6 &&
             pl->hp == 6 && pl->speed < 540.0f && pl->wlevel[WPN_BLASTER] == 0;
        /* Game over -> C -> character select -> 2 -> SHOTGUNNER. */
        game_over();
        G.gameover_t = 1.0f;
        k->char_pressed = true;
        game_update();
        clear_edges();
        ok = ok && G.state == GS_CHAR_SELECT;
        k->weapon_select = 1;
        game_update();
        clear_edges();
        k->confirm_pressed = true;
        game_update();
        clear_edges();
        ok = ok && G.state == GS_PLAYING && G.character == CHAR_SHOTGUNNER && pl->weapon == WPN_SCATTER &&
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
     * while hurt, SIGNATURE WEAPON offers, DEFLECTOR SHELLS reflections, the SPREAD trade. */
    {
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        Player *pl = &G.pl;
        bool desc_ok = SDL_strstr(upgrade_desc(UPG_THERMITE), "15%") != NULL;
        upgrade_apply(UPG_THERMITE);
        desc_ok = desc_ok && SDL_strstr(upgrade_desc(UPG_THERMITE), "30%") && SDL_strstr(upgrade_desc_at(UPG_THERMITE, 1), "15%");
        upgrade_apply(UPG_LOADED_DICE);
        desc_ok = desc_ok && SDL_strstr(upgrade_desc_at(UPG_THERMITE, 1), "30%") && SDL_strstr(upgrade_desc_at(UPG_PAYLOAD, 1), "40%");

        upgrade_apply(UPG_WOUNDED_FURY);
        float full = upg_volley_damage_mult();
        pl->hp = pl->max_hp - 1;
        float wounded = upg_volley_damage_mult();
        bool hurt_ok = fabsf(full - 1.0f) < 1e-4f && fabsf(wounded - 1.3f) < 1e-4f;
        pl->hp = pl->max_hp;

        pl->wlevel[WPN_SCATTER] = 1;
        pl->wlevel[WPN_NEEDLER] = 3; /* highest level: always offered with SIGNATURE WEAPON */
        upgrade_apply(UPG_SIGNATURE);
        bool sig_ok = true;
        for (int k = 0; k < 5 && sig_ok; k++) {
            G.npk = 0;
            reward_drop(RW_WEAPON, v2(ARENA_CX, ARENA_CY));
            bool found = false;
            for (int i = 0; i < G.npk; i++) found |= G.pk[i].type == PK_WEAPON && G.pk[i].sub == WPN_NEEDLER && G.pk[i].choice;
            sig_ok = found && G.npk == REWARD_CHOICES;
        }
        G.npk = 0;

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
        printf("scripted: chances in descriptions -> %s, wounded fury -> %s, signature weapon -> %s, deflector -> %s, spread -> %s\n",
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
        bool none_ok = pl->active == ACT_NONE && pl->dash_t <= 0.0f && G.active_uses == 0;

        active_give(ACT_DASH);
        float x0 = pl->pos.x;
        in->right = true;
        in->active_pressed = true;
        TICK();
        bool dash_ok = pl->dash_t > 0.0f && fabsf(pl->act_cd - pl->dash_cooldown) < 0.02f;
        in->active_pressed = true; /* still recharging: ignored */
        TICK();
        for (int t = 0; t < 30; t++) TICK();
        in->right = false;
        dash_ok = dash_ok && G.active_uses == 1 && pl->pos.x - x0 > 200.0f;

        G.god = false;
        active_give(ACT_SHIELD);
        in->active_pressed = true;
        TICK();
        int hp = pl->hp + pl->shield;
        for (int t = 0; t < 120; t++) {
            for (int k = 0; k < 4; k++) ebullet_spawn(pl->pos, (float)k * 1.6f, 10.0f, 6.0f, COL_WHITE);
            TICK();
        }
        bool shield_ok = pl->hp + pl->shield == hp && active_shield_on();
        for (int t = 0; t < 300; t++) TICK();
        shield_ok = shield_ok && !active_shield_on() && pl->act_cd > 0.0f;
        G.neb = 0;
        G.god = true;

        active_give(ACT_BOOM);
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

        active_give(ACT_STRENGTH);
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

        active_give(ACT_CHAOS);
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

        /* Taking the item held again levels it up: longer, stronger, faster recharge. */
        active_give(ACT_SHIELD);
        active_give(ACT_SHIELD);
        in->active_pressed = true;
        TICK();
        bool level_ok = pl->act_level == 2 && fabsf(pl->act_t - (active_duration(ACT_SHIELD, 2) - DT)) < 0.02f &&
                        active_duration(ACT_SHIELD, 2) > active_duration(ACT_SHIELD, 1) &&
                        fabsf(pl->act_cd_max - active_cooldown(ACT_SHIELD, 2)) < 0.02f && pl->act_cd_max < active_cooldown(ACT_SHIELD, 1);
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
        level_ok = level_ok && offered_held && !offered_max && pl->act_level == MAX_ACTIVE_LEVEL;
        failures += !level_ok;
        printf("scripted actives: level up (offered while it can level, never at max %d) -> %s\n", MAX_ACTIVE_LEVEL, level_ok ? "OK" : "FAILED");
#undef TICK
        printf("scripted actives: none -> %s, dash -> %s, shield -> %s, boom -> %s, strength %d -> %d shots -> %s, chaos %d upgrades -> %s\n",
               none_ok ? "OK" : "FAILED", dash_ok ? "OK" : "FAILED", shield_ok ? "OK" : "FAILED", boom_ok ? "OK" : "FAILED",
               normal, strong, strength_ok ? "OK" : "FAILED", lent, chaos_ok ? "OK" : "FAILED");
        failures += !none_ok + !dash_ok + !shield_ok + !boom_ok + !strength_ok + !chaos_ok;
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
        const char *start_err = G.pl.max_hp != cd->hearts * 2 || G.pl.hp != G.pl.max_hp || G.pl.shield != cd->armor * 2 ||
                                        G.pl.weapon != cd->weapon || G.pl.wlevel[cd->weapon] != 1 || G.pl.hit_radius != cd->hit_radius ||
                                        G.pl.dmg_mult != cd->dmg_mult
                                    ? "run did not start with the character's stats / weapon"
                                : G.pl.active != ACT_NONE ? "run started with an active item (they must be found)" : NULL;
        if (o->active) active_give(o->active);
        G.pl.coins += o->coins;
        if (o->all_upgrades)
            for (int u = 0; u < UPG_COUNT; u++)
                if (!upgrade_capped(u)) upgrade_apply(u);
        if (o->hp > 0) G.pl.hp = G.pl.max_hp = o->hp;
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
        printf("run %s seed=%-6llu rooms=%-3d depth=%-3d kills=%-5d time=%5.0fs hp=%d/%d weapons=",
               character_option_name(G.character), (unsigned long long)seed, G.rooms_cleared, G.room.depth, G.kills, (double)G.run_time, G.pl.hp, G.pl.max_hp);
        for (int w = 0; w < WPN_COUNT; w++)
            if (G.pl.wlevel[w]) printf("%.3s%d ", WEAPONS[w].name, G.pl.wlevel[w]);
        const Player *p = &G.pl;
        printf("hits=%d coins=%d shops=%d bought=%d secrets=%d stats_rooms=%d", hits, p->coins, shops, G.items_bought, secrets, stats_rooms);
        printf(" active=%s%d uses=%d", active_option_name(p->active), p->act_level, G.active_uses);
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
        } else if ((o->active || o->active_room) && G.active_uses == 0) {
            printf("  had an active item but never used it\n");
            failures++;
        } else if (o->active_room && p->active == ACT_NONE) {
            printf("  started in an active room but never took an active item\n");
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
