#include "headless.h"

#include "bot.h"
#include "draw.h"
#include "game.h"
#include "save.h"

#include <stdio.h>
#include <stdlib.h>

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
    if (G.ticks % 4 == 0 && !room_occupancy_ok()) return "an obstacle is missing from the collision cell map (room_nav_reset() not called when obstacles changed)";
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
    { /* Room layout: every spot reachable when only the walls count, walls never break, blocks in range. */
        static int layout_room = -1, walls = 0;
        if (G.room.layout_id != layout_room) {
            layout_room = G.room.layout_id;
            walls = room_count_obstacles(OB_WALL);
            if (!room_walls_connected()) return "room layout: walls cut off a door or a part of the room";
        }
        if (!G.room.cleared && room_count_obstacles(OB_WALL) != walls) return "a wall was broken (walls are indestructible)";
        if (G.room.nobs < 0 || G.room.nobs > MAX_OBSTACLES || G.room.nfires < 0 || G.room.nfires > MAX_FIRES) return "obstacle / fire count out of range";
        for (int i = 0; i < G.room.nobs; i++) {
            const Obstacle *o = &G.room.obs[i];
            if (o->kind >= OB_KIND_COUNT) return "obstacle of an unknown kind";
            if (obstacle_breakable(o) && (!(o->hp >= 0.0f) || o->hp > o->max_hp)) return "block hp out of range";
            if (o->r.x < ARENA_X0 - 0.5f || o->r.y < ARENA_Y0 - 0.5f || o->r.x + o->r.w > ARENA_X1 + 0.5f || o->r.y + o->r.h > ARENA_Y1 + 0.5f)
                return "obstacle outside the arena";
        }
        for (int i = 0; i < G.room.nfires; i++)
            if (!(G.room.fires[i].t > 0.0f) || G.room.fires[i].t > G.room.fires[i].life) return "burning oil pool with a bad timer";
        if (G.room.type == ROOM_BOSS && G.room.nfires) return "burning oil in the boss room (open floor)";
        if (G.room.cleared && G.room.clear_t >= 0.0f && (G.room.nobs || G.room.nfires))
            return "cleared room not empty (every obstacle shatters before the reward shows up)";
        if (G.room.cleared && G.room.clear_t < 0.0f && G.npk && !G.room.revisit) return "reward out while the room still shatters";
        if (G.room.cleared && G.neb) return "enemy bullet left in a cleared room (all wiped by the last kill)";
    }
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if ((k->type == PK_UPGRADE || k->type == PK_AUGMENT) && (k->sub < 0 || k->sub >= UPG_COUNT)) return "invalid upgrade offered";
        if (k->type == PK_UPGRADE && p->upg_count[k->sub] > 0) return "upgrade offered that the player already owns";
        if ((k->type == PK_UPGRADE || k->type == PK_AUGMENT) && !upgrade_unlocked(k->sub)) return "a locked upgrade was offered";
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
        if (G.mc[i].type == MC_GAMBLE && G.mc[i].prize == PK_UPGRADE && !G.mc[i].broken && !upgrade_unlocked(G.mc[i].prize_sub))
            return "the slot machine shows a locked upgrade";
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
    /* Boss rooms: every BOSS_EVERY rooms, reached through the single door of the room before. */
    if (G.room.type != ROOM_SECRET) {
        bool before_boss = is_boss_depth(G.room.depth + 1);
        if (before_boss && (G.room.ndoors != 1 || G.room.doors[0].type != ROOM_BOSS || G.room.doors[0].locked))
            return "the room before a boss must have a single, open door: the boss room";
        for (int i = 0; i < G.room.ndoors; i++)
            if (!before_boss && G.room.doors[i].type == ROOM_BOSS) return "boss door outside the room before a boss";
    }
    if (G.room.type == ROOM_BOSS) {
        if (!is_boss_depth(G.room.depth)) return "boss room at a depth that is not a multiple of BOSS_EVERY";
        if (G.room.reward != RW_UPGRADE || G.room.nwaves != 1 || G.room.nobs != 0 || G.room.has_secret)
            return "boss room set up wrong (upgrade reward, one wave, open floor, no secret)";
    }
    {
        int bosses = 0;
        for (int i = 0; i < G.nen; i++) {
            const Enemy *e = &G.en[i];
            if (e->type != EN_BOSS) continue;
            bosses++;
            if (G.room.type != ROOM_BOSS) return "boss outside a boss room";
            if (e->phase < 0 || e->phase >= BOSS_PHASES) return "boss phase out of range";
            if (e->hp > 0.0f && e->phase + 1 < BOSS_PHASES && e->hp < e->max_hp * boss_phase_threshold(e->phase + 1) - 1.5f)
                return "boss skipped a phase (damage must stop at each threshold)";
        }
        for (int i = 0; i < G.nsp; i++) bosses += G.sp[i].type == EN_BOSS;
        if (bosses > 1) return "more than one boss";
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
        if (!finite_v2(e->pos) || !isfinite(e->hp)) {
            static char emsg[128];
            snprintf(emsg, sizeof emsg, "enemy state is not finite (%s, state %d, hp %.1f, vel %.1f,%.1f)", ENEMIES[e->type].name, e->state,
                     (double)e->hp, (double)e->vel.x, (double)e->vel.y);
            return emsg;
        }
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
    for (int i = 0; i < G.npb; i++)
        if (!G.pb[i].armed && !(G.pb[i].flags & PB_BYPRODUCT))
            return "player shot spawned without the upgrades (every shot goes through pbullet_arm)";
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
    for (int i = 0; i < G.room.ndoors; i++)
        if (G.room.doors[i].side == G.room.entry) return "an exit door on the side the ship came in by";
    if (G.room.secret_open && !G.room.has_secret) return "secret passage open in a room without a secret";
    if (G.room.ghost_t > 0.0f) { /* the secret's ghost: only for a hidden secret, 0.5-2 s, inside the room */
        const Room *r = &G.room;
        float a = room_ghost_alpha();
        if (!r->has_secret || r->secret_open || r->secret_used) return "a secret ghost in a room with no hidden secret";
        if (r->ghost_life < GHOST_MIN_TIME || r->ghost_life > GHOST_MAX_TIME || r->ghost_t > r->ghost_life)
            return "secret ghost stays outside 0.5-2 s";
        if (!(a >= 0.0f && a <= 1.0f) || r->ghost_pos.x < ARENA_X0 || r->ghost_pos.x > ARENA_X1 || r->ghost_pos.y < ARENA_Y0 ||
            r->ghost_pos.y > ARENA_Y1)
            return "secret ghost out of the room or with a bad fade";
        /* born within GHOST_DOOR_R, drifts at most 75 px/s for 2 s */
        if (v2_dist(r->ghost_pos, room_secret_spot()) > GHOST_DOOR_R * 1.2f + 75.0f * GHOST_MAX_TIME)
            return "secret ghost far from the secret's door";
    }
    for (int i = 0; i < G.nen; i++) { /* crawlers live on the wall (separation may nudge them a little) */
        const Enemy *c = &G.en[i];
        if (c->type != EN_CRAWLER || c->t < 0.05f) continue;
        float edge = fminf(fminf(c->pos.x - ARENA_X0, ARENA_X1 - c->pos.x), fminf(c->pos.y - ARENA_Y0, ARENA_Y1 - c->pos.y));
        if (edge > 2.0f * c->radius + 4.0f) {
            static char cmsg[160];
            snprintf(cmsg, sizeof cmsg, "a crawler left the wall (at %.0f,%.0f, %.0f px in, t %.2f, hp %.0f, time stop %d)", (double)c->pos.x,
                     (double)c->pos.y, (double)edge, (double)c->t, (double)c->hp, active_time_stop_on());
            return cmsg;
        }
    }
    for (int i = 0; i < G.neb; i++)
        if (G.eb[i].kind > EB_BALL) return "enemy bullet of an unknown kind";
    if (G.room.type == ROOM_SECRET && G.room.ndoors != 0) return "secret room has regular doors (its only exit leads back)";
    for (int i = 0; i < G.npk; i++)
        if (G.pk[i].price < 0 || (G.pk[i].price > 0 && G.room.type != ROOM_SHOP && !G.room.secret_shop))
            return "priced item outside a shop";
    float stall = fminf(room_time, G.time - G.last_enemy_damage_t);
    if ((G.room.type == ROOM_COMBAT || G.room.type == ROOM_BOSS) && !G.room.cleared &&
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
        snprintf(msg, sizeof msg, "still in cleared room %d after %.0f s (bot stuck at %.0f, %.0f? vel %.0f,%.0f keys %d%d%d%d fire %d, %d shots, speed %.0f x%.2f)",
                 G.room.depth, (double)G.room.clear_t, (double)p->pos.x, (double)p->pos.y, (double)p->vel.x, (double)p->vel.y,
                 G.in.left, G.in.right, G.in.up, G.in.down, G.in.fire, G.npb, (double)p->speed, (double)upg_move_speed_mult());
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
static bool is_supply(const Pickup *k) { return k->type == PK_HEART || k->type == PK_BOMB || k->type == PK_KEY; }

/* Items for sale: upgrades / stat trades, or the supply counter's heart, bomb and key. */
static int shop_count(bool supplies) {
    int n = 0;
    for (int i = 0; i < G.npk; i++) n += G.pk[i].price > 0 && is_supply(&G.pk[i]) == supplies;
    return n;
}

static const char *check_room_reward(void) {
    static char buf[128];
    if ((G.room.type == ROOM_SHOP || G.room.secret_shop) && (shop_count(false) != SHOP_ITEMS || shop_count(true) != SHOP_SUPPLIES)) {
        snprintf(buf, sizeof buf, "shop opened with %d items + %d supplies for sale instead of %d + %d", shop_count(false),
                 shop_count(true), SHOP_ITEMS, SHOP_SUPPLIES);
        return buf;
    }
    if (G.room.type == ROOM_SHOP || G.room.secret_shop) {
        bool kinds[PK_ACTIVE + 1] = {false};
        for (int i = 0; i < G.npk; i++)
            if (G.pk[i].price > 0 && is_supply(&G.pk[i])) kinds[G.pk[i].type] = true;
        if (!kinds[PK_HEART] || !kinds[PK_BOMB] || !kinds[PK_KEY]) return "shop supply counter without its heart, bomb and key";
    }
    if (G.room.type == ROOM_BOSS) {
        if (count_choices() != REWARD_CHOICES) return "boss beaten but it did not leave a choice of 3 upgrades";
        for (int i = 0; i < G.npk; i++)
            if (G.pk[i].type != PK_UPGRADE && G.pk[i].type != PK_STAT) return "boss reward is not an upgrade choice";
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
        for (int u = 0; u < UPG_COUNT; u++) tier1_left += UPGRADES[u].tier == 1 && G.pl.upg_count[u] == 0 && upgrade_unlocked(u);
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

/* Items waiting on a pedestal or a shop counter (taken with E only). */
static int count_pedestals(void) {
    int n = 0;
    for (int i = 0; i < G.npk; i++) {
        PickupType t = G.pk[i].type;
        n += t == PK_UPGRADE || t == PK_STAT || t == PK_AUGMENT || t == PK_ACTIVE || G.pk[i].price > 0;
    }
    return n;
}

static void clear_edges(void) {
    Input *in = &G.in;
    in->active_pressed = in->bomb_pressed = in->interact_pressed = in->pause_pressed = in->restart_pressed = in->confirm_pressed = false;
    in->debug_pressed = in->char_pressed = in->menu_pressed = in->click_pressed = in->delete_pressed = false;
    in->number = -1;
    in->wheel = 0;
}

/* Debug-menu teleports: every room type, with and without enemies, must land in a
 * coherent state, and the bot must be able to finish the room and leave it. */
/* Scripted block checks: put one block (or wall) in the room. */
/* The solid block whose centre is c (indices change as blocks go), -1 if gone. */
static int block_at(V2 c) {
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if (obstacle_solid(o) && fabsf(o->r.x + o->r.w * 0.5f - c.x) < 1.0f && fabsf(o->r.y + o->r.h * 0.5f - c.y) < 1.0f) return i;
    }
    return -1;
}

static int test_block(float cx, float cy, int kind) {
    Obstacle *o = &G.room.obs[G.room.nobs];
    memset(o, 0, sizeof *o);
    o->r = (SDL_FRect){cx - BLOCK_TILE * 0.5f, cy - BLOCK_TILE * 0.5f, BLOCK_TILE, BLOCK_TILE};
    o->kind = (uint8_t)kind;
    o->hp = o->max_hp = kind == OB_WALL ? 1.0f : 20.0f;
    o->swing_id = -1;
    room_nav_reset();
    return G.room.nobs++;
}

static int count_ebullets(int kind) {
    int n = 0;
    for (int i = 0; i < G.neb; i++) n += G.eb[i].kind == kind && G.eb[i].life > 0.0f;
    return n;
}

static int debug_rooms_run(const HeadlessOptions *o) {
    static const RoomType types[] = {ROOM_START, ROOM_COMBAT, ROOM_COMBAT, ROOM_COMBAT, ROOM_STATS, ROOM_SHOP, ROOM_WORKSHOP, ROOM_BOSS, ROOM_SECRET, ROOM_SECRET};
    static const RewardType rewards[] = {RW_NONE, RW_NONE, RW_UPGRADE, RW_ACTIVE, RW_NONE, RW_NONE, RW_NONE, RW_NONE, RW_NONE, RW_NONE};
    static const int secret_shop[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const char *names[] = {"launch bay", "combat", "upgrade", "active", "stats", "shop", "workshop", "boss", "secret", "secret shop"};
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
    bool ok = G.state == GS_PLAYING && G.room.type == ROOM_SHOP && G.room.cleared && shop_count(false) == SHOP_ITEMS &&
              shop_count(true) == SHOP_SUPPLIES;
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

    /* Scripted: the boss. The room before has a single door leading to it; damage stops at
     * each phase threshold, a phase change makes it invulnerable; its death takes the minions
     * along and leaves a choice of 3 upgrades. */
    {
#define STEP() do { G.hitstop = 0.0f; game_update(); clear_edges(); } while (0) /* hitstop would skip the ticks */
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, BOSS_EVERY - 1, false);
        G.state = GS_PLAYING;
        bool door_ok = G.room.ndoors == 1 && G.room.doors[0].type == ROOM_BOSS && !G.room.doors[0].locked;
        int id = G.room.id;
        for (int t = 0; t < 120 * 20 && G.room.id == id; t++) { bot_input(); game_update(); clear_edges(); }
        bool enter_ok = G.room.type == ROOM_BOSS && G.room.depth == BOSS_EVERY && !G.room.cleared;
        for (int t = 0; t < 120 * 4 && !boss_alive(); t++) { game_update(); clear_edges(); }
        Enemy *b = (Enemy *)boss_alive();
        bool phase_ok = false, inv_ok = false, death_ok = false;
        if (b) {
            int uid = b->uid;
            enemy_damage(b, b->max_hp, v2(0, 0)); /* one huge hit: stops just under 66% */
            phase_ok = b->hp > b->max_hp * 0.6f && b->hp < b->max_hp * boss_phase_threshold(1);
            STEP();
            b = (Enemy *)boss_alive();
            if (b && b->uid == uid) {
                float hp = b->hp;
                enemy_damage(b, 500.0f, v2(0, 0));
                inv_ok = b->phase == 1 && b->inv_t > 0.0f && b->hp == hp;
                b->inv_t = 0.0f;
                enemy_damage(b, b->max_hp, v2(0, 0));
                STEP();
                b->inv_t = 0.0f;
                enemy_queue_spawn(EN_CHASER, v2(ARENA_CX - 300.0f, ARENA_CY), 0.0f);
                STEP();
                int last_phase = b->phase, minions = G.nen - 1;
                enemy_damage(b, b->max_hp, v2(0, 0));
                for (int t = 0; t < 10 + (int)(ROOM_REVEAL_T * TICK_RATE); t++) STEP(); /* the room shatters first */
                death_ok = phase_ok && last_phase == BOSS_PHASES - 1 && minions > 0 && G.room.cleared && G.nen == 0 && count_choices() == REWARD_CHOICES &&
                           check_room_reward() == NULL;
            }
        }
        printf("scripted: boss door at room %d -> %s, entered -> %s, phase threshold -> %s, phase invulnerability -> %s, "
               "death + 3 upgrades -> %s\n", BOSS_EVERY - 1, door_ok ? "OK" : "FAILED", enter_ok ? "OK" : "FAILED",
               phase_ok ? "OK" : "FAILED", inv_ok ? "OK" : "FAILED", death_ok ? "OK" : "FAILED");
        failures += !door_ok + !enter_ok + !phase_ok + !inv_ok + !death_ok;
#undef STEP
    }

    /* Scripted: every player shot carries the upgrades - tail gun shots, turret shots, enemy
     * bullets turned around by REVERSAL - not only the main gun's. */
    {
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_COINS, 3, false);
        G.state = GS_PLAYING;
        Player *pl = &G.pl;
        Input *in = &G.in;
        upgrade_apply(UPG_W_ARC);
        upgrade_apply(UPG_PIERCE);
        upgrade_apply(UPG_TAIL_GUN);
        Enemy dummy = {.type = EN_TANK, .uid = ++G.next_uid, .pos = v2(ARENA_CX, ARENA_Y0 + 120.0f), .radius = 30.0f,
                       .hp = 5000.0f, .max_hp = 5000.0f};
        G.en[G.nen++] = dummy;
#define CARRIES(b) ((b)->armed && ((b)->flags & PB_ARC) && (b)->pierce >= pl->pierce)
        int shots[3] = {0}, bad = 0;
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->mouse = v2(ARENA_CX, ARENA_Y0 + 120.0f);
        in->fire = true;
        G.hitstop = 0.0f;
        game_update();
        clear_edges();
        in->fire = false;
        for (int i = 0; i < G.npb; i++) {
            shots[0] += G.pb[i].vel.y > 0.0f; /* the tail gun fires backwards */
            bad += !CARRIES(&G.pb[i]);
        }
        G.npb = 0;
        active_give(ACT_TURRET);
        in->active_pressed = true;
        for (int t = 0; t < 60 && G.npb == 0; t++) { G.hitstop = 0.0f; game_update(); clear_edges(); }
        for (int i = 0; i < G.npb; i++) {
            shots[1]++;
            bad += !CARRIES(&G.pb[i]);
        }
        memset(pl->act, 0, sizeof pl->act);
        active_give(ACT_REVERSAL);
        G.npb = G.neb = 0;
        for (int k = 0; k < 5; k++) ebullet_spawn(v2(ARENA_CX + 200.0f, ARENA_CY), frand(0.0f, TAU_F), 50.0f, 7.0f, COL_WHITE);
        in->active_pressed = true;
        G.hitstop = 0.0f;
        game_update();
        clear_edges();
        for (int i = 0; i < G.npb; i++) {
            shots[2] += (G.pb[i].flags & PB_REFLECTED) != 0;
            bad += !CARRIES(&G.pb[i]);
        }
#undef CARRIES
        bool ok = shots[0] > 0 && shots[1] > 0 && shots[2] > 0 && bad == 0;
        printf("scripted: upgrades on every shot (tail gun %d, turret %d, reversal %d, %d without them) -> %s\n", shots[0],
               shots[1], shots[2], bad, ok ? "OK" : "FAILED");
        failures += !ok;
    }

    /* Scripted: the character select screen, driven with the keyboard like a player. */
    {
        game_init(o->seed);
        Input *k = &G.in;
        game_update(); /* the title, then its character select (everything unlocked: no save) */
        char_select_open();
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
        printf("character select: menu, right x2, enter -> knight; game over, C, 2, enter -> shotgunner -> %s\n", ok ? "OK" : "FAILED");
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
        /* a new screen recharges every item (the effect still running keeps going) */
        float strength_t = pl->act[ACT_STRENGTH].t;
        room_enter(ROOM_COMBAT, RW_COINS, 3);
        bool recharged = true;
        for (int a = ACT_NONE + 1; a < ACT_COUNT; a++) recharged = recharged && pl->act[a].cd == 0.0f;
        multi_ok = multi_ok && recharged && pl->act[ACT_STRENGTH].t == strength_t;
        in->active_pressed = true;
        TICK(); /* used at once, except STRENGTH: its effect is still running */
        multi_ok = multi_ok && pl->act[ACT_NOVA].cd > 0.0f && (strength_t > 0.0f ? pl->act[ACT_STRENGTH].cd == 0.0f : pl->act[ACT_STRENGTH].cd > 0.0f);
        G.nsp = G.nen = 0;
        room_debug_clear();
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
        const char *names[48];
        bool oks[48];
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
        /* spin-up: no shot for 0.4 s after pulling the trigger */
        FRESH();
        upgrade_apply(UPG_CONDUCTOR);
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->fire = true;
        for (int t = 0; t < 24; t++) TICK(); /* 0.2 s */
        int early = G.npb;
        for (int t = 0; t < 36; t++) TICK();
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

        /* The newer malus kinds: FRAGILE (BERSERKER: every hit costs one more half-heart), BIG BULLETS
         * (GUARDIAN ORB: +20%), NO MERCY (REACTIVE ARMOR: bullets around the ship survive a hit). */
        FRESH();
        upgrade_apply(UPG_BERSERKER);
        G.god = false;
        pl->invuln = 0.0f;
        int hp_f = pl->hp;
        player_hurt(1);
        bool mal2_ok = hp_f - pl->hp == 2;
        G.god = true;
        FRESH();
        EBullet *big = ebullet_spawn(v2(ARENA_CX, ARENA_CY - 300.0f), 0.0f, 1.0f, 10.0f, COL_WHITE);
        mal2_ok = mal2_ok && big && big->radius == 10.0f;
        upgrade_apply(UPG_ORB);
        big = ebullet_spawn(v2(ARENA_CX, ARENA_CY - 300.0f), 0.0f, 1.0f, 10.0f, COL_WHITE);
        mal2_ok = mal2_ok && big && fabsf(big->radius - 12.0f) < 1e-3f;
        FRESH();
        upgrade_apply(UPG_REACTIVE);
        G.neb = 0;
        ebullet_spawn(v2_add(pl->pos, v2(60.0f, 0.0f)), 0.0f, 0.0f, 6.0f, COL_WHITE);
        pl->invuln = 0.0f;
        player_hurt(1);
        mal2_ok = mal2_ok && G.neb == 1 && G.eb[0].life > 0.0f;
        G.neb = G.npb = 0;
        G.hitstop = 0.0f;
        names[nres] = "maluses: fragile, big bullets, no mercy", oks[nres++] = mal2_ok;

        /* PRISM: one shot becomes three mid-flight; LASER keeps a continuous beam and pierces. */
        FRESH();
        upgrade_apply(UPG_W_PRISM);
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        int before_split = G.npb;
        for (int t = 0; t < 24; t++) TICK();
        bool prism_ok = before_split == 1 && G.npb == 3;
        FRESH();
        upgrade_apply(UPG_W_LASER);
        DUMMY(ARENA_CX, ARENA_CY);
        DUMMY(ARENA_CX + 200.0f, ARENA_CY);
        in->fire = true;
        for (int t = 0; t < 60; t++) { G.hitstop = 0.0f; TICK(); }
        in->fire = false;
        prism_ok = prism_ok && G.nen == 2 && G.en[0].hp < 5000.0f && G.en[1].hp < 5000.0f && G.npb > 8;
        G.nen = G.npb = 0;
        names[nres] = "prism splits, laser pierces", oks[nres++] = prism_ok;

        /* The sword combines with the shot upgrades: LASER -> lightsaber (x2.5 damage, longer),
         * WAVE -> every swing throws a blade wave that carries the shot behaviours. */
        FRESH();
        upgrade_apply(UPG_W_SWORD);
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        float plain_dmg = pl->swing_dmg, plain_reach = pl->swing_reach;
        int plain_shots = G.npb;
        upgrade_apply(UPG_W_LASER);
        pl->fire_cd = 0.0f;
        TICK();
        bool saber_ok = plain_dmg > 0.0f && fabsf(pl->swing_dmg - plain_dmg * 2.5f) < 0.01f && pl->swing_reach > plain_reach &&
                        plain_shots == 0 && G.npb == 0;
        upgrade_apply(UPG_W_WAVE);
        pl->fire_cd = 0.0f;
        TICK();
        in->fire = false;
        saber_ok = saber_ok && G.npb == 1 && G.pb[0].style == BS_CRESCENT && (G.pb[0].flags & PB_WAVE) && G.pb[0].armed;
        G.npb = 0;
        names[nres] = "lightsaber, blade waves", oks[nres++] = saber_ok;

        /* RICOCHET: a shot without pierce hits one enemy, then jumps to the other. STICKY CHARGES:
         * the charge blows 0.7 s after the hit. FIRST BLOOD: x3 on an enemy at full health. */
        FRESH();
        upgrade_apply(UPG_RICOCHET);
        DUMMY(ARENA_CX, ARENA_CY);
        DUMMY(ARENA_CX + 150.0f, ARENA_CY + 150.0f);
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        for (int t = 0; t < 60; t++) { G.hitstop = 0.0f; TICK(); }
        bool hit_ok = G.nen == 2 && G.en[0].hp < 5000.0f && G.en[1].hp < 5000.0f;
        FRESH();
        upgrade_apply(UPG_STICKY);
        DUMMY(ARENA_CX, ARENA_CY);
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        in->fire = false;
        for (int t = 0; t < 40; t++) TICK(); /* the shot lands... */
        float after_hit = G.en[0].hp;
        for (int t = 0; t < 100; t++) TICK(); /* ...and the charge goes off */
        hit_ok = hit_ok && after_hit < 5000.0f && G.en[0].sticky_t <= 0.0f && G.en[0].hp < after_hit - (5000.0f - after_hit);
        FRESH();
        DUMMY(ARENA_CX, ARENA_CY);
        PBullet probe = {.damage = 10.0f};
        float base_hit = upg_hit_damage(&probe, &G.en[0]);
        upgrade_apply(UPG_FIRST_BLOOD);
        hit_ok = hit_ok && fabsf(upg_hit_damage(&probe, &G.en[0]) - base_hit * 3.0f) < 1e-3f;
        G.en[0].hp -= 1.0f;
        hit_ok = hit_ok && fabsf(upg_hit_damage(&probe, &G.en[0]) - base_hit) < 1e-3f;
        G.nen = G.npb = 0;
        names[nres] = "ricochet, sticky charges, first blood", oks[nres++] = hit_ok;

        /* GRAZE: a bullet brushing past counts once and hurts nobody. CHARGE SHOT: 1.5 s without
         * firing, then one volley at x6. AEGIS: a bullet coming from the front is eaten. */
        FRESH();
        upgrade_apply(UPG_GRAZE);
        G.god = false;
        pl->invuln = 0.0f;
        int hp_g = pl->hp;
        G.neb = 0;
        ebullet_spawn(v2_add(pl->pos, v2(-100.0f, 22.0f)), 0.0f, 400.0f, 7.0f, COL_WHITE);
        for (int t = 0; t < 60; t++) TICK();
        bool play_ok = pl->graze == 1 && pl->hp == hp_g;
        G.god = true;
        FRESH();
        upgrade_apply(UPG_CHARGE);
        float d0 = pl->dmg_mult * upg_volley_damage_mult() * 7.0f; /* the blaster's 7 damage */
        for (int t = 0; t < 190; t++) TICK();
        play_ok = play_ok && pl->charge == 1.0f;
        G.npb = 0;
        pl->fire_cd = 0.0f;
        in->fire = true;
        TICK();
        play_ok = play_ok && G.npb == 1 && fabsf(G.pb[0].damage - d0 * 6.0f) < 0.01f && pl->charge == 0.0f;
        for (int t = 0; t < 12; t++) TICK();
        in->fire = false;
        play_ok = play_ok && G.npb >= 2 && fabsf(G.pb[G.npb - 1].damage - d0) < 0.01f;
        FRESH();
        upgrade_apply(UPG_AEGIS);
        G.god = false;
        pl->invuln = 0.0f;
        hp_g = pl->hp;
        G.neb = 0;
        ebullet_spawn(v2_add(pl->pos, v2(200.0f, 0.0f)), PI_F, 400.0f, 7.0f, COL_WHITE); /* from where the ship aims */
        for (int t = 0; t < 90; t++) TICK();
        play_ok = play_ok && pl->hp == hp_g && G.neb == 0;
        G.god = true;
        G.npb = 0;
        names[nres] = "graze, charge shot, aegis", oks[nres++] = play_ok;

        /* Synergies: active only while every part is turned on, announced once per run. */
        FRESH();
        upgrade_apply(UPG_W_SWORD);
        bool syn_ok = !syn(SYN_PARRY);
        upgrade_apply(UPG_DEFLECTOR);
        syn_ok = syn_ok && syn(SYN_PARRY) && !pl->syn_seen[SYN_PARRY];
        upgrade_toggle(UPG_DEFLECTOR);
        syn_ok = syn_ok && !syn(SYN_PARRY);
        upgrade_toggle(UPG_DEFLECTOR);
        int act_syn[SYN_COUNT];
        syn_ok = syn_ok && synergies_active(act_syn) == 1 && act_syn[0] == SYN_PARRY && SDL_strstr(synergy_parts(SYN_PARRY), "SWORD + DEFLECTOR");
        TICK(); /* the upgrade's own banner is still up: the synergy waits */
        syn_ok = syn_ok && !pl->syn_seen[SYN_PARRY];
        G.banner_t = 0.0f;
        TICK();
        syn_ok = syn_ok && pl->syn_seen[SYN_PARRY] && SDL_strstr(G.banner, "PARRY") != NULL;
        /* PARRY: the blade sends a bullet back as one of ours. */
        G.neb = G.npb = 0;
        ebullet_spawn(v2_add(pl->pos, v2(80.0f, 0.0f)), 0.0f, 0.0f, 7.0f, COL_WHITE);
        pl->fire_cd = 0.0f;
        in->fire = true;
        for (int t = 0; t < 30; t++) TICK();
        in->fire = false;
        int sent_back = 0;
        for (int i = 0; i < G.npb; i++) sent_back += (G.pb[i].flags & PB_REFLECTED) && G.pb[i].armed;
        syn_ok = syn_ok && sent_back == 1 && G.neb == 0;
        /* MIRROR SHIELD: so does the front shield. */
        FRESH();
        upgrade_apply(UPG_AEGIS);
        upgrade_apply(UPG_DEFLECTOR);
        G.neb = G.npb = 0;
        ebullet_spawn(v2_add(pl->pos, v2(200.0f, 0.0f)), PI_F, 400.0f, 7.0f, COL_WHITE);
        for (int t = 0; t < 60; t++) TICK();
        sent_back = 0;
        for (int i = 0; i < G.npb; i++) sent_back += (G.pb[i].flags & PB_REFLECTED) != 0;
        syn_ok = syn_ok && syn(SYN_MIRROR_SHIELD) && sent_back == 1 && G.neb == 0;
        G.npb = 0;
        names[nres] = "synergies: on / off, banner, parry, mirror shield", oks[nres++] = syn_ok;

        /* Elemental reactions. THERMAL SHOCK: burning + chilled -> a burst, both states spent (and
         * nothing without a chill source). COMBUSTION: burning + poisoned -> the poison blows up,
         * the neighbour catches fire. */
        FRESH();
        upgrade_apply(UPG_THERMITE);
        DUMMY(ARENA_CX, ARENA_CY);
        G.en[0].burn_t = 2.0f, G.en[0].burn_dps = 20.0f, G.en[0].slow_t = 1.0f;
        TICK();
        bool react_ok = !syn(SYN_THERMAL_SHOCK) && G.en[0].burn_t > 0.0f && G.en[0].hp > 4999.0f;
        upgrade_apply(UPG_FROST);
        G.en[0].hp = 5000.0f, G.en[0].burn_t = 2.0f, G.en[0].burn_dps = 20.0f, G.en[0].slow_t = 1.0f;
        TICK();
        react_ok = react_ok && syn(SYN_THERMAL_SHOCK) && G.en[0].burn_t <= 0.0f && G.en[0].slow_t <= 0.0f && G.en[0].hp <= 5000.0f - 30.0f;
        FRESH();
        upgrade_apply(UPG_THERMITE);
        upgrade_apply(UPG_VENOM);
        DUMMY(ARENA_CX, ARENA_CY);
        DUMMY(ARENA_CX + 90.0f, ARENA_CY);
        G.en[0].burn_t = 2.0f, G.en[0].burn_dps = 20.0f, G.en[0].poison_t = 3.0f, G.en[0].poison_dps = 30.0f;
        TICK();
        react_ok = react_ok && syn(SYN_COMBUSTION) && G.en[0].poison_t <= 0.0f && G.en[0].hp <= 5000.0f - 60.0f &&
                   G.en[1].burn_t > 0.0f && G.en[1].hp < 5000.0f;
        G.nen = 0;
        names[nres] = "synergies: thermal shock, combustion", oks[nres++] = react_ok;

        /* DEMOLITION: a bomb sets off the sticky charges, twice as strong. EXECUTIONER: the
         * guillotine falls at 25%. BLADE DANCER: the clone's blade cuts too. */
        FRESH();
        upgrade_apply(UPG_STICKY);
        DUMMY(ARENA_CX, ARENA_CY);
        G.en[0].sticky_dmg = 100.0f, G.en[0].sticky_t = 5.0f;
        pl->bombs = 1;
        in->bomb_pressed = true;
        TICK();
        bool misc_ok = syn(SYN_DEMOLITION) && G.en[0].sticky_dmg == 0.0f && G.en[0].hp <= 5000.0f - 200.0f - 35.0f;
        G.hitstop = 0.0f;
        FRESH();
        upgrade_apply(UPG_GUILLOTINE);
        DUMMY(ARENA_CX, ARENA_CY);
        G.en[0].hp = 5000.0f * 0.2f;
        PBullet weak = {.damage = 1.0f};
        misc_ok = misc_ok && upg_hit_damage(&weak, &G.en[0]) < 10.0f;
        upgrade_apply(UPG_BULLY);
        misc_ok = misc_ok && syn(SYN_EXECUTIONER) && upg_hit_damage(&weak, &G.en[0]) > G.en[0].hp;
        FRESH();
        upgrade_apply(UPG_W_SWORD);
        upgrade_apply(UPG_CLONE);
        for (int i = 0; i < CLONE_DELAY; i++) pl->trail[i] = v2(ARENA_CX, ARENA_CY + 250.0f); /* the clone, far from the ship */
        pl->pos = v2(ARENA_CX - 500.0f, ARENA_CY - 250.0f);
        DUMMY(ARENA_CX + 70.0f, ARENA_CY + 250.0f); /* right in front of the clone only */
        pl->fire_cd = 0.0f;
        in->fire = true;
        for (int t = 0; t < 6; t++) { G.hitstop = 0.0f; TICK(); }
        in->fire = false;
        misc_ok = misc_ok && syn(SYN_BLADE_DANCER) && G.nen == 1 && G.en[0].hp < 5000.0f && G.npb == 0;
        G.nen = G.npb = 0;
        names[nres] = "synergies: demolition, executioner, blade dancer", oks[nres++] = misc_ok;

        /* Shop supply counter: a heart (refused at full health), a bomb and a key, paid in coins. */
        game_init(o->seed);
        G.god = true;
        game_new_run();
        debug_teleport(ROOM_SHOP, RW_NONE, 3, false);
        G.state = GS_PLAYING;
        pl->coins = 100;
        bool supply_ok = true;
        static const PickupType sup[] = {PK_HEART, PK_BOMB, PK_KEY};
        for (int s = 0; s < 3; s++) {
            int idx = -1;
            for (int i = 0; i < G.npk; i++)
                if (G.pk[i].type == sup[s] && G.pk[i].price > 0) idx = i;
            supply_ok = supply_ok && idx >= 0;
            if (idx < 0) break;
            int price = G.pk[idx].price, coins = pl->coins, bombs = pl->bombs, keys = pl->keys, hp = pl->hp, n0 = G.npk;
            /* Walking over it takes nothing (E only); E at full health: refused. */
            pl->pos = G.pk[idx].pos;
            for (int t = 0; t < 70; t++) TICK();
            supply_ok = supply_ok && G.npk == n0 && pl->coins == coins;
            if (sup[s] == PK_HEART) {
                in->interact_pressed = true;
                TICK();
                supply_ok = supply_ok && G.npk == n0 && pl->coins == coins;
                pl->hp = pl->max_hp - 2;
                hp = pl->hp;
            }
            in->interact_pressed = true;
            TICK();
            supply_ok = supply_ok && pl->coins == coins - price && G.npk == n0 - 1 &&
                        (sup[s] == PK_HEART ? pl->hp == hp + 2 : sup[s] == PK_BOMB ? pl->bombs == bombs + 1 : pl->keys == keys + 1);
            pl->pos = v2(ARENA_CX, ARENA_Y1 - 80.0f);
            TICK();
        }
        names[nres] = "shop supplies", oks[nres++] = supply_ok;

        /* New enemy bullets: a grenade is harmless in the air and bursts where it lands; a mine
         * blows when the ship comes close; a split orb bursts; a bouncing bullet comes back off a wall. */
        FRESH();
        G.nen = G.nsp = G.neb = 0;
        pl->invuln = 0.0f;
        V2 pp = pl->pos;
        ebullet_lob(v2(pp.x + 300.0f, pp.y), v2(pp.x - 300.0f, pp.y), 1.0f, 8, COL_WHITE);
        for (int t = 0; t < 60; t++) TICK();
        bool eb_ok = G.neb == 1 && ebullet_airborne(&G.eb[0]) && pl->invuln <= 0.0f; /* right over the ship */
        for (int t = 0; t < 80 && G.neb == 1; t++) TICK();
        eb_ok = eb_ok && G.neb == 8 && !ebullet_airborne(&G.eb[0]);
        G.neb = 0;
        EBullet *mine = ebullet_spawn(v2(pp.x + 400.0f, pp.y), 0.0f, 0.0f, 9.0f, COL_WHITE);
        mine->kind = EB_MINE;
        mine->life = 5.0f;
        mine->n = 8;
        mine->age = 1.0f;
        TICK();
        bool mine_idle = G.neb == 1 && G.eb[0].life > 4.0f;
        pl->pos = v2(pp.x + 340.0f, pp.y); /* walks up to it */
        int mine_burst = 0;
        for (int t = 0; t < 90 && !mine_burst; t++) {
            TICK();
            int mines = 0;
            for (int i = 0; i < G.neb; i++) mines += G.eb[i].kind == EB_MINE;
            if (mines == 0) mine_burst = G.neb;
        }
        eb_ok = eb_ok && mine_idle && mine_burst >= 6;
        pl->pos = pp;
        G.neb = 0;
        EBullet *orb = ebullet_spawn(v2(ARENA_CX + 300.0f, ARENA_CY), -PI_F * 0.5f, 100.0f, 14.0f, COL_WHITE);
        orb->kind = EB_SPLIT;
        orb->t1 = 0.3f;
        orb->n = 10;
        for (int t = 0; t < 48; t++) TICK();
        eb_ok = eb_ok && G.neb == 10;
        G.neb = 0;
        EBullet *bb = ebullet_spawn(v2(ARENA_X0 + 60.0f, ARENA_Y0 + 200.0f), PI_F, 400.0f, 7.0f, COL_WHITE);
        bb->kind = EB_BOUNCE;
        bb->n = 1;
        for (int t = 0; t < 40; t++) TICK();
        eb_ok = eb_ok && G.neb == 1 && G.eb[0].vel.x > 0.0f && G.eb[0].n == 0;
        G.neb = 0;
        names[nres] = "enemy bullets: grenade, mine, split, bounce", oks[nres++] = eb_ok;

        /* New enemies: the splitter splits in three, the guardian shields its allies, the mirror
         * stops shots from the front only, a faded wraith / airborne leaper cannot be hurt, a
         * kamikaze blows itself up (not a kill). */
#define SPAWN(type, x, y) do { G.nen = 0; G.nsp = 0; enemy_queue_spawn(type, v2(x, y), 0.0f); TICK(); } while (0)
        FRESH();
        SPAWN(EN_SPLITTER, ARENA_CX + 200.0f, ARENA_CY);
        bool en_ok = G.nen == 1 && G.en[0].type == EN_SPLITTER;
        G.en[0].hp = 1.0f;
        enemy_damage(&G.en[0], 10.0f, v2(0, 0));
        for (int t = 0; t < 30; t++) TICK();
        int splitlings = 0;
        for (int i = 0; i < G.nen; i++) splitlings += G.en[i].type == EN_SPLITLING;
        en_ok = en_ok && splitlings == 3 && G.nen == 3;
        bool split_ok = en_ok;

        SPAWN(EN_GUARDIAN, ARENA_CX, ARENA_CY);
        DUMMY(ARENA_CX + 100.0f, ARENA_CY);
        DUMMY(ARENA_CX + 700.0f, ARENA_CY);
        enemy_damage(&G.en[1], 100.0f, v2(0, 0));
        enemy_damage(&G.en[2], 100.0f, v2(0, 0));
        bool guard_ok = fabsf(G.en[1].hp - (5000.0f - 100.0f * GUARDIAN_SHIELD)) < 0.01f && fabsf(G.en[2].hp - 4900.0f) < 0.01f;

        FRESH();
        SPAWN(EN_MIRROR, pl->pos.x + 280.0f, pl->pos.y);
        G.en[0].angle = v2_heading(v2_sub(pl->pos, G.en[0].pos));
        bool front = enemy_blocks_shot(&G.en[0], pl->pos);
        bool back = enemy_blocks_shot(&G.en[0], v2(ARENA_CX + 600.0f, ARENA_CY));
        in->mouse = G.en[0].pos;
        in->fire = true;
        for (int t = 0; t < 90; t++) { G.hitstop = 0.0f; in->mouse = G.en[0].pos; G.en[0].fire_t = 5.0f; TICK(); } /* plate stays shut */
        in->fire = false;
        bool mirror_ok = front && !back && G.nen == 1 && G.en[0].hp >= G.en[0].max_hp;
        G.npb = 0;
        in->fire = true;
        for (int t = 0; t < 90; t++) { /* turned away: now it takes the hits */
            G.hitstop = 0.0f;
            in->mouse = G.en[0].pos;
            G.en[0].angle = v2_heading(v2_sub(G.en[0].pos, pl->pos));
            TICK();
        }
        in->fire = false;
        mirror_ok = mirror_ok && G.nen == 1 && G.en[0].hp < G.en[0].max_hp;
        G.npb = 0;

        SPAWN(EN_WRAITH, ARENA_CX + 300.0f, ARENA_CY);
        G.en[0].state = 0;
        G.en[0].fire_t = 5.0f;
        enemy_damage(&G.en[0], 10.0f, v2(0, 0));
        bool ghost_ok = G.en[0].hp == G.en[0].max_hp && enemy_untouchable(&G.en[0]);
        G.en[0].state = 2;
        G.en[0].state_t = 5.0f;
        enemy_damage(&G.en[0], 10.0f, v2(0, 0));
        ghost_ok = ghost_ok && G.en[0].hp < G.en[0].max_hp;
        SPAWN(EN_LEAPER, ARENA_CX + 300.0f, ARENA_CY);
        G.en[0].alt = 100.0f;
        enemy_damage(&G.en[0], 10.0f, v2(0, 0));
        ghost_ok = ghost_ok && G.en[0].hp == G.en[0].max_hp;

        G.neb = 0;
        int kills0 = G.kills;
        SPAWN(EN_KAMIKAZE, pl->pos.x + 90.0f, pl->pos.y);
        for (int t = 0; t < 120 && G.nen > 0; t++) TICK();
        bool kami_ok = G.nen == 0 && G.kills == kills0 && G.neb > 0;
        G.neb = 0;
        en_ok = split_ok && guard_ok && mirror_ok && ghost_ok && kami_ok;
        if (!en_ok) printf("  [split %d guard %d mirror %d ghost %d kamikaze %d] ", split_ok, guard_ok, mirror_ok, ghost_ok, kami_ok);
        names[nres] = "enemies: splitter, guardian, mirror, wraith, leaper, kamikaze", oks[nres++] = en_ok;

        /* The crawler never leaves the wall; the rook only ever moves along rows and columns. */
        FRESH();
        SPAWN(EN_CRAWLER, ARENA_CX, ARENA_Y0 + 18.0f);
        bool walls_ok = G.nen == 1;
        float s0x = G.nen ? G.en[0].pos.x : 0.0f;
        for (int t = 0; t < 360 && walls_ok; t++) {
            TICK();
            const Enemy *c = &G.en[0];
            float edge = fminf(fminf(c->pos.x - ARENA_X0, ARENA_X1 - c->pos.x), fminf(c->pos.y - ARENA_Y0, ARENA_Y1 - c->pos.y));
            walls_ok = G.nen == 1 && fabsf(edge - c->radius) < 1.0f;
        }
        walls_ok = walls_ok && fabsf(G.en[0].pos.x - s0x) > 50.0f; /* it did crawl */
        SPAWN(EN_ROOK, ARENA_CX + 300.0f, ARENA_CY - 200.0f);
        float moved = 0.0f;
        for (int t = 0; t < 600 && walls_ok; t++) {
            V2 before = G.en[0].pos;
            static const float corners[4][2] = {{0.2f, 0.25f}, {0.8f, 0.75f}, {0.25f, 0.8f}, {0.75f, 0.2f}};
            if (t % 120 == 0) /* far corners in turn: it has to slide a long way */
                pl->pos = v2(ARENA_X0 + (ARENA_X1 - ARENA_X0) * corners[t / 120 % 4][0], ARENA_Y0 + (ARENA_Y1 - ARENA_Y0) * corners[t / 120 % 4][1]);
            TICK();
            V2 d = v2_sub(G.en[0].pos, before);
            walls_ok = G.nen == 1 && (fabsf(d.x) < 0.01f || fabsf(d.y) < 0.01f);
            moved += v2_len(d);
        }
        walls_ok = walls_ok && moved > 150.0f;
        G.nen = G.neb = 0;
#undef SPAWN
        names[nres] = "crawler on the wall, rook on rows and columns", oks[nres++] = walls_ok;
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
                                   UPG_THRUSTER, UPG_LEECH, UPG_W_SEEKER, UPG_W_RAILGUN, UPG_SKELETON_KEY, UPG_DRONE};
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
                      gun_main_mod() < 0 && upg_has(UPG_LEECH) == 0 && pl->upg_off[UPG_HULL] &&
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

    {
        /* Main menu: a bot-played demo runs behind it (on its own generator: the game's stays
         * untouched), CONTINUE (no save) / OPTIONS are greyed out (skipped, clicks do nothing),
         * NEW SAVE starts a clean run at once on a new save's unlocks; the character select keeps
         * the demo going and refuses locked ships. */
        game_init(o->seed);
        Input *in = &G.in;
        uint64_t rng0 = G.rng.s;
        int kills = 0, rooms0 = -1;
        bool demo_ok = true;
        /* At least 20 s, until it moved on to another room (a cleared room or a new demo run). */
        for (int t = 0; t < 120 * 150 && !(t > 120 * 20 && rooms0 >= 0 && G.room.id != rooms0); t++) {
            game_update();
            clear_edges();
            if (rooms0 < 0) rooms0 = G.room.id;
            kills = G.kills > kills ? G.kills : kills;
            demo_ok = demo_ok && G.state == GS_TITLE && G.demo && demo_visible();
        }
        demo_ok = demo_ok && G.rng.s == rng0 && G.room.id != rooms0; /* it played on, through rooms */
        in->down = true; /* NEW SAVE -> (CONTINUE: no save; OPTIONS greyed) -> QUIT */
        game_update();
        clear_edges();
        in->down = false;
        bool grey_ok = G.title_sel == 3;
        in->mouse = v2(SCREEN_W * 0.5f, 430.0f + 2.0f * 78.0f + 30.0f); /* click on OPTIONS */
        in->click_pressed = in->confirm_pressed = true;
        game_update();
        clear_edges();
        grey_ok = grey_ok && G.state == GS_TITLE && !G.quit;
        in->up = true; /* back to NEW SAVE: nothing unlocked yet, the run starts at once */
        game_update();
        clear_edges();
        in->up = false;
        in->confirm_pressed = true;
        game_update();
        clear_edges();
        bool play_ok = G.state == GS_PLAYING && !G.demo && G.room.type == ROOM_START && G.kills == 0 && G.nen == 0 &&
                       !demo_visible() && G.character == CHAR_NORMAL && profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT &&
                       !character_unlocked(CHAR_SHOTGUNNER) && !character_unlocked(CHAR_KNIGHT);
        /* The character select behind which the demo keeps playing: a new run, locked ships refused. */
        game_init(o->seed);
        profile_fresh(&G.prof);
        title_open();
        for (int t = 0; t < 10; t++) { game_update(); clear_edges(); }
        char_select_open();
        int demo_room = G.room.id;
        float demo_t = G.run_time;
        for (int t = 0; t < 120; t++) { game_update(); clear_edges(); }
        play_ok = play_ok && G.state == GS_CHAR_SELECT && G.demo && (G.run_time > demo_t || G.room.id != demo_room);
        in->number = CHAR_KNIGHT;
        in->confirm_pressed = true;
        game_update();
        clear_edges();
        play_ok = play_ok && G.state == GS_CHAR_SELECT && G.demo;
        in->number = CHAR_NORMAL;
        in->confirm_pressed = true;
        game_update();
        clear_edges();
        play_ok = play_ok && G.state == GS_PLAYING && !G.demo && G.character == CHAR_NORMAL;
        printf("scripted main menu: demo behind it -> %s, greyed items -> %s, play -> %s\n", demo_ok ? "OK" : "FAILED",
               grey_ok ? "OK" : "FAILED", play_ok ? "OK" : "FAILED");
        failures += !demo_ok + !grey_ok + !play_ok;
    }

    {
        /* Secret hint: a ghost shows up a few seconds into the fight, drifts for 0.5-2 s, fades in
         * from nothing and out to nothing, comes back 5-10 s later; none once the secret is open. */
        game_init(o->seed);
        game_new_run();
        G.force_secret = true;
        room_enter(ROOM_COMBAT, RW_COINS, 4);
        G.force_secret = false;
        float t = 0.0f, first = -1.0f, second = -1.0f, max_a = 0.0f, first_a = -1.0f, last_a = 0.0f, travel = 0.0f;
        int seen = 0;
        V2 start = v2(0, 0);
        bool ok = G.room.has_secret;
        for (; t < 25.0f && ok && seen < 2; t += DT) {
            int before = G.room.ghosts;
            float prev_a = room_ghost_alpha();
            room_update();
            float a = room_ghost_alpha();
            if (G.room.ghosts != before) {
                seen = G.room.ghosts;
                if (seen == 1) { first = t; start = G.room.ghost_pos; first_a = a; }
                else second = t;
                ok = ok && G.room.ghost_life >= GHOST_MIN_TIME && G.room.ghost_life <= GHOST_MAX_TIME;
            }
            if (seen == 1) {
                max_a = fmaxf(max_a, a);
                if (a > 0.0f) travel = fmaxf(travel, v2_dist(G.room.ghost_pos, start));
                if (a == 0.0f && prev_a > 0.0f) last_a = prev_a;
            }
        }
        bool timing_ok = ok && first >= 2.0f && first <= 5.0f + DT && second - first >= 5.0f - DT && second - first <= 10.0f + DT;
        bool fade_ok = first_a >= 0.0f && first_a < 0.1f && max_a > 0.95f && last_a < 0.1f && travel > 10.0f;
        G.room.secret_open = true; /* found: no more ghosts */
        int n = G.room.ghosts;
        for (int k = 0; k < 120 * 12; k++) room_update();
        bool stop_ok = G.room.ghosts == n && room_ghost_alpha() == 0.0f;
        printf("scripted secret ghost: first at %.1f s, next %.1f s later -> %s, fades in / out and drifts %.0f px -> %s, "
               "none once open -> %s\n", (double)first, (double)(second - first), timing_ok ? "OK" : "FAILED", (double)travel,
               fade_ok ? "OK" : "FAILED", stop_ok ? "OK" : "FAILED");
        failures += !timing_ok + !fade_ok + !stop_ok;
    }

    {
        /* Blocks: the ship's shots / blade / bomb break them, the enemies' bullets never do; a barrel
         * blows up (enemies, the ship, the blocks around), an oil drum leaves burning oil (sets enemies
         * and the ship on fire, burns the blocks in it), a spark pod throws a ring of balls that hurt
         * the enemies too. And the layouts: every spot reachable when only the walls count. */
#define BTICK() do { game_update(); clear_edges(); } while (0)
#define BFRESH() do { game_init(o->seed); game_new_run(); debug_teleport(ROOM_COMBAT, RW_COINS, 3, false); \
                      G.state = GS_PLAYING; G.npk = 0; G.room.nobs = 0; G.room.nfires = 0; room_nav_reset(); \
                      G.room.cleared = false; G.room.wave = 0; G.room.nwaves = 1; /* a fight, no wave coming */ \
                      G.room.wave_t = 1e6f; G.room.since_wave = -1e6f; \
                      G.pl.pos = v2(ARENA_X0 + 200.0f, ARENA_Y1 - 150.0f); G.pl.invuln = 0.0f; \
                      G.in.mouse = v2(ARENA_X0 + 200.0f, ARENA_Y0 + 100.0f); } while (0)
#define BDUMMY(x, y, r) do { Enemy d = {.type = EN_TANK, .uid = ++G.next_uid, .pos = v2(x, y), .radius = r, \
                                        .hp = 5000.0f, .max_hp = 5000.0f}; G.en[G.nen++] = d; } while (0)
        Player *pl = &G.pl;
        Input *in = &G.in;

        BFRESH(); /* enemy bullets: stopped, no damage */
        test_block(ARENA_CX, ARENA_CY, OB_CRATE);
        for (int k = 0; k < 6; k++) ebullet_spawn(v2(ARENA_CX - 200.0f, ARENA_CY - 10.0f + (float)k * 4.0f), 0.0f, 600.0f, 6.0f, COL_WHITE);
        for (int t = 0; t < 90; t++) BTICK();
        bool enemy_ok = G.room.nobs == 1 && G.room.obs[0].hp == G.room.obs[0].max_hp && G.neb == 0;

        BFRESH(); /* the ship's shots */
        test_block(ARENA_CX, ARENA_CY, OB_CRATE);
        pl->pos = v2(ARENA_CX - 300.0f, ARENA_CY);
        in->mouse = v2(ARENA_CX, ARENA_CY);
        for (int t = 0; t < 180 && G.room.nobs > 0; t++) {
            in->fire = true;
            BTICK();
        }
        in->fire = false;
        bool shot_ok = G.room.nobs == 0;

        BFRESH(); /* the blade */
        upgrade_apply(UPG_W_SWORD);
        pl->pos = v2(ARENA_CX - 60.0f, ARENA_CY);
        test_block(ARENA_CX, ARENA_CY, OB_CRATE);
        in->mouse = v2(ARENA_CX, ARENA_CY);
        for (int t = 0; t < 240 && G.room.nobs > 0; t++) {
            in->fire = true;
            BTICK();
        }
        in->fire = false;
        bool sword_ok = G.room.nobs == 0;

        BFRESH(); /* a bomb breaks every block, not the walls */
        test_block(ARENA_X1 - 200.0f, ARENA_Y0 + 150.0f, OB_CRATE);
        test_block(ARENA_X1 - 400.0f, ARENA_Y0 + 150.0f, OB_BARREL);
        test_block(ARENA_X1 - 600.0f, ARENA_Y0 + 150.0f, OB_OIL);
        test_block(ARENA_X1 - 800.0f, ARENA_Y0 + 150.0f, OB_SPARK);
        test_block(ARENA_CX, ARENA_CY, OB_WALL);
        pl->bombs = 1;
        in->bomb_pressed = true;
        for (int t = 0; t < 3; t++) BTICK();
        bool bomb_ok = room_count_obstacles(-1) == 0 && room_count_obstacles(OB_WALL) == 1 && G.room.nfires == 1;

        BFRESH(); /* barrel */
        int barrel = test_block(ARENA_CX, ARENA_CY, OB_BARREL);
        test_block(ARENA_CX + 90.0f, ARENA_CY, OB_CRATE);
        test_block(ARENA_CX + 450.0f, ARENA_CY, OB_CRATE);
        BDUMMY(ARENA_CX, ARENA_CY - 100.0f, 30.0f);
        pl->pos = v2(ARENA_CX - 100.0f, ARENA_CY + 40.0f);
        int hp0 = pl->hp;
        room_block_damage(barrel, 100.0f);
        for (int t = 0; t < 3; t++) BTICK();
        bool barrel_ok = G.en[0].hp < 5000.0f && room_count_obstacles(OB_CRATE) == 1 && pl->hp < hp0;

        BFRESH(); /* oil */
        int oil = test_block(ARENA_CX, ARENA_CY, OB_OIL);
        test_block(ARENA_CX + 50.0f, ARENA_CY, OB_CRATE);
        BDUMMY(ARENA_CX + 30.0f, ARENA_CY + 60.0f, 30.0f);
        pl->pos = v2(ARENA_CX - 40.0f, ARENA_CY + 50.0f);
        hp0 = pl->hp;
        room_block_damage(oil, 100.0f);
        for (int t = 0; t < 40; t++) BTICK();
        bool oil_ok = G.room.nfires == 1 && G.en[0].burn_t > 0.0f && pl->hp < hp0 && pl->burn_t > 0.0f;
        pl->pos = v2(ARENA_X0 + 200.0f, ARENA_Y1 - 150.0f); /* out of it */
        for (int t = 0; t < 360; t++) BTICK();
        bool oil_burns_ok = room_count_obstacles(OB_CRATE) == 0 && G.en[0].hp < 5000.0f && pl->burn_t <= 0.0f;

        BFRESH(); /* spark pod */
        int spark = test_block(ARENA_CX, ARENA_CY, OB_SPARK);
        BDUMMY(ARENA_CX + 160.0f, ARENA_CY, 40.0f);
        room_block_damage(spark, 100.0f);
        BTICK();
        int balls = count_ebullets(EB_BALL);
        for (int t = 0; t < 90; t++) BTICK();
        bool spark_ok = balls == SPARK_BALLS && G.en[0].hp < 5000.0f;

        BFRESH(); /* nest: one small enemy crawls out */
        int nest = test_block(ARENA_CX, ARENA_CY, OB_NEST);
        room_block_damage(nest, 100.0f);
        for (int t = 0; t < 90; t++) BTICK();
        bool nest_ok = G.nen == 1 && G.nsp == 0 && G.en[0].type != EN_BOSS && ENEMIES[G.en[0].type].radius <= 18.0f && !G.room.cleared;

        /* Fire: crate A on fire spreads to crate B next to it, then to the barrel next to B, each going
         * off in turn; the spark pod next to A never catches, nor crate C two cells away. */
#define CELL(x, y) v2(ARENA_X0 + GRID * ((float)(x) + 0.5f), ARENA_Y0 + GRID * ((float)(y) + 0.5f))
        BFRESH();
        V2 ca = CELL(10, 7), cb = CELL(11, 7), cbar = CELL(12, 7), cspark = CELL(9, 7), cc = CELL(10, 10), cd = CELL(12, 9);
        test_block(ca.x, ca.y, OB_CRATE);
        test_block(cb.x, cb.y, OB_CRATE);
        test_block(cbar.x, cbar.y, OB_BARREL);
        test_block(cspark.x, cspark.y, OB_SPARK);
        test_block(cc.x, cc.y, OB_CRATE);
        test_block(cd.x, cd.y, OB_CRATE); /* not next to any: only the barrel's blast breaks it */
        BDUMMY(ARENA_X1 - 100.0f, ARENA_Y0 + 100.0f, 20.0f); /* keeps the fight going */
        room_block_ignite(block_at(ca));
        BTICK();
        bool fire_ok = G.room.obs[block_at(ca)].burn_t > 0.0f && G.room.obs[block_at(cb)].burn_t <= 0.0f;
        for (int t = 0; t < (int)((BLOCK_SPREAD_T + 0.1f) * TICK_RATE); t++) BTICK();
        fire_ok = fire_ok && block_at(cb) >= 0 && G.room.obs[block_at(cb)].burn_t > 0.0f && G.room.obs[block_at(cbar)].burn_t <= 0.0f;
        bool spread_ok = true;
        for (int t = 0; t < 4 * TICK_RATE; t++) {
            BTICK();
            int s = block_at(cspark), c = block_at(cc);
            int d = block_at(cd);
            spread_ok = spread_ok && s >= 0 && G.room.obs[s].burn_t <= 0.0f && c >= 0 && G.room.obs[c].burn_t <= 0.0f &&
                        (d < 0 || G.room.obs[d].burn_t <= 0.0f);
        }
        fire_ok = fire_ok && spread_ok && block_at(ca) < 0 && block_at(cb) < 0 && block_at(cbar) < 0 && block_at(cd) < 0;

        BFRESH(); /* flame shots and burning enemies set crates alight */
        upgrade_apply(UPG_W_FLAMER);
        V2 cf = CELL(8, 7), ce = CELL(20, 7);
        test_block(cf.x, cf.y, OB_CRATE);
        test_block(ce.x, ce.y, OB_CRATE);
        BDUMMY(ce.x + 52.0f, ce.y, 20.0f);
        G.en[0].burn_t = 3.0f, G.en[0].burn_dps = 1.0f;
        pl->pos = v2(cf.x - 200.0f, cf.y);
        in->mouse = cf;
        bool flame_lit = false, enemy_lit = false;
        for (int t = 0; t < 120; t++) {
            in->fire = true;
            BTICK();
            int f = block_at(cf), e = block_at(ce);
            flame_lit |= f >= 0 && G.room.obs[f].burn_t > 0.0f;
            enemy_lit |= e >= 0 && G.room.obs[e].burn_t > 0.0f;
        }
        in->fire = false;
        bool ignite_ok = flame_lit && enemy_lit;
#undef CELL

        BFRESH(); /* cleared: a wave of glass from the last kill, nothing goes off, then the reward */
        G.room.wave = G.room.nwaves;
        G.room.reward = RW_COINS;
        test_block(ARENA_CX - 280.0f, ARENA_CY, OB_CRATE);
        test_block(ARENA_X1 - 200.0f, ARENA_Y0 + 200.0f, OB_WALL);
        test_block(pl->pos.x + 70.0f, pl->pos.y, OB_BARREL);
        test_block(pl->pos.x, pl->pos.y - 70.0f, OB_OIL);
        test_block(ARENA_CX + 300.0f, ARENA_CY, OB_NEST);
        BDUMMY(ARENA_CX - 360.0f, ARENA_CY, 30.0f);
        G.en[0].hp = 1.0f;
        hp0 = pl->hp;
        enemy_damage(&G.en[0], 10.0f, v2(0, 0));
        bool order_ok = false, early_reward = false, reward = false;
        int shards = 0;
        for (int t = 0; t < 300; t++) {
            BTICK();
            for (int i = 0; i < G.npt; i++) shards += G.pt[i].shape == PT_SHARD;
            if (G.room.cleared && G.room.clear_t < 0.0f) {
                early_reward |= G.npk > 0;
                /* the crate by the last kill went first, the far wall still stands */
                if (room_count_obstacles(OB_CRATE) == 0 && room_count_obstacles(OB_WALL) == 1) order_ok = true;
            } else if (G.room.cleared) {
                reward |= G.npk > 0; /* the coins come out once it has shattered (then get vacuumed up) */
            }
        }
        bool shatter_ok = G.room.cleared && G.room.nobs == 0 && G.room.nfires == 0 && order_ok && !early_reward &&
                          reward && shards > 0 && pl->hp == hp0 && G.nen == 0 && G.nsp == 0; /* the nest: glass, nothing hatches */

        /* Layouts: many rooms of every kind, from every side. */
        int rooms = 0, bad = 0, walls = 0, blocks = 0, kinds = 0;
        game_init(o->seed);
        game_new_run();
        const RoomType types[] = {ROOM_COMBAT, ROOM_COMBAT, ROOM_STATS, ROOM_SHOP, ROOM_WORKSHOP};
        for (int k = 0; k < 400; k++) {
            G.entry_side = (Side)irand(SIDE_LEFT, SIDE_BOTTOM);
            G.force_secret = k % 3 == 0;
            RoomType ty = types[k % ARRAY_LEN(types)];
            room_enter(ty, ty == ROOM_COMBAT ? room_roll_reward() : RW_NONE, irand(1, 9));
            G.force_secret = false;
            rooms++;
            V2 q = G.pl.pos;
            if (!room_walls_connected() || room_collide_circle(&q, PLAYER_BODY_R, NULL)) bad++;
            walls += room_count_obstacles(OB_WALL);
            blocks += room_count_obstacles(-1);
            for (int i = 0; i < G.room.nobs; i++) kinds |= 1 << G.room.obs[i].kind;
        }
        bool layout_ok = bad == 0 && walls > rooms * 3 && blocks > rooms * 6 && kinds == (1 << OB_KIND_COUNT) - 1;
        printf("scripted blocks: enemy bullets -> %s, shots -> %s, blade -> %s, bomb -> %s, barrel -> %s, oil -> %s, "
               "oil burns -> %s, spark pod -> %s, nest -> %s, fire spreads -> %s, flames / burning enemies ignite -> %s, "
               "cleared room shatters -> %s, %d layouts (%.1f walls, %.1f blocks avg, %d bad) -> %s\n",
               enemy_ok ? "OK" : "FAILED", shot_ok ? "OK" : "FAILED", sword_ok ? "OK" : "FAILED", bomb_ok ? "OK" : "FAILED",
               barrel_ok ? "OK" : "FAILED", oil_ok ? "OK" : "FAILED", oil_burns_ok ? "OK" : "FAILED", spark_ok ? "OK" : "FAILED",
               nest_ok ? "OK" : "FAILED", fire_ok ? "OK" : "FAILED", ignite_ok ? "OK" : "FAILED",
               shatter_ok ? "OK" : "FAILED", rooms, (double)walls / rooms, (double)blocks / rooms, bad, layout_ok ? "OK" : "FAILED");
        failures += !enemy_ok + !shot_ok + !sword_ok + !bomb_ok + !barrel_ok + !oil_ok + !oil_burns_ok + !spark_ok + !nest_ok + !fire_ok + !ignite_ok + !shatter_ok + !layout_ok;

        /* End of a room: the ship stays where it is, the doors stay shut for DOORS_OPEN_T after the
         * last kill (the reward shows first), and an item is only taken with E. */
        BFRESH();
        G.god = true;
        G.room.wave = G.room.nwaves; /* the last wave: its death clears the room */
        int exit_door = -1;
        for (int i = 0; i < G.room.ndoors; i++)
            if (!G.room.doors[i].locked) exit_door = i;
        Side exit_side = exit_door >= 0 ? G.room.doors[exit_door].side : SIDE_TOP;
        V2 door_in = door_position(exit_side), mid = v2(ARENA_CX, ARENA_CY);
        door_in = v2_add(door_in, v2_scale(v2_norm(v2_sub(mid, door_in)), PLAYER_BODY_R + 2.0f));
        V2 stand = v2(ARENA_CX + 250.0f, ARENA_CY + 120.0f);
        pl->pos = stand;
        BDUMMY(ARENA_CX - 360.0f, ARENA_CY, 30.0f);
        G.en[0].hp = 1.0f;
        enemy_damage(&G.en[0], 10.0f, v2(0, 0));
        for (int t = 0; t < 600 && !G.room.cleared; t++) BTICK();
        bool stayed = G.room.cleared && v2_dist(pl->pos, stand) < 2.0f;
        float opened = -1.0f;
        for (int t = 0; t < 600 && exit_door >= 0 && opened < 0.0f; t++) {
            pl->pos = door_in; /* pushing into the door all along */
            pl->vel = v2(0, 0);
            float since = G.room.clear_t + ROOM_REVEAL_T;
            BTICK();
            if (G.transitioning) opened = since;
        }
        bool doors_ok = opened >= DOORS_OPEN_T - 0.05f && opened <= DOORS_OPEN_T + 0.05f;
        game_init(o->seed);
        game_new_run();
        debug_teleport(ROOM_COMBAT, RW_UPGRADE, 3, false);
        G.state = GS_PLAYING;
        for (int t = 0; t < 70; t++) BTICK();
        int offers = G.npk, owned0 = 0, owned1 = 0, tpk = -1, tmc = -1;
        for (int u = 0; u < UPG_COUNT; u++) owned0 += pl->upg_count[u] > 0;
        bool take_ok = offers == 3;
        if (take_ok) {
            pl->pos = G.pk[1].pos; /* standing on it: nothing happens... */
            int sub = G.pk[1].sub;
            for (int t = 0; t < 70; t++) BTICK();
            interact_target(&tpk, &tmc);
            take_ok = G.npk == 3 && tpk >= 0 && G.pk[tpk].sub == sub && tmc < 0; /* ...but it is the one E points at */
            in->interact_pressed = true;
            BTICK();
            for (int u = 0; u < UPG_COUNT; u++) owned1 += pl->upg_count[u] > 0;
            take_ok = take_ok && G.npk == 0 && owned1 == owned0 + 1 && pl->upg_count[sub] > 0;
        }
        printf("scripted room end: ship stays -> %s, doors open after %.2f s -> %s, items taken with E only -> %s\n",
               stayed ? "OK" : "FAILED", (double)opened, doors_ok ? "OK" : "FAILED", take_ok ? "OK" : "FAILED");
        failures += !stayed + !doors_ok + !take_ok;
#undef BTICK
#undef BFRESH
#undef BDUMMY
    }

    { /* Particles never change the game: the same fight with and without them ends in the same state. */
        Rng rng[2];
        V2 pos[2];
        int kills[2], counts[2];
        for (int k = 0; k < 2; k++) {
            g_fx_off = k == 1;
            game_init(o->seed);
            G.god = true;
            game_new_run();
            for (int u = 0; u < UPG_COUNT; u++)
                if (!upgrade_capped(u)) upgrade_apply(u);
            room_enter(ROOM_COMBAT, RW_UPGRADE, 14);
            for (int t = 0; t < 3600; t++) {
                bot_input();
                game_update();
                clear_edges();
            }
            rng[k] = G.rng, pos[k] = G.pl.pos, kills[k] = G.kills, counts[k] = G.nen * 100000 + G.neb * 1000 + G.npb;
            if (k == 1 && G.npt != 0) failures++;
        }
        g_fx_off = false;
        bool same = !memcmp(&rng[0], &rng[1], sizeof rng[0]) && pos[0].x == pos[1].x && pos[0].y == pos[1].y &&
                    kills[0] == kills[1] && counts[0] == counts[1];
        printf("particles on / off: same game after 30 s (%d kills) -> %s\n", kills[0], same && kills[0] > 0 ? "OK" : "FAILED");
        failures += !(same && kills[0] > 0);
    }

    printf("debug teleports - %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}

/* The command replaying only this run: same options, its own seed, one run (the bot starts afresh
 * every run, so it plays the same), plus a bot trace over the last 2 s before the failure. */
static void print_repro(const HeadlessOptions *o, uint64_t seed, int fail_tick) {
    if (!o->argv) return;
    printf("  replay it alone:");
    const char *scale = getenv("NEON_TICK_SCALE");
    if (scale && atof(scale) > 0.0 && atof(scale) < 1.0) printf(" NEON_TICK_SCALE=%s", scale);
    for (int i = 0; i < o->argc; i++) {
        const char *a = o->argv[i];
        if (i > 0 && (!strcmp(a, "--seed") || !strcmp(a, "--runs") || !strcmp(a, "--trace-bot")) && i + 1 < o->argc) {
            i++;
            continue;
        }
        printf(" %s", a);
    }
    printf(" --seed %llu", (unsigned long long)seed);
    if (fail_tick >= 0) printf(" --trace-bot %d", fail_tick > 240 ? fail_tick - 240 : 0);
    printf("\n");
}

/* --save-test: a save keeps its unlocks from run to run; its run is saved on entering each room and
 * only then, a loaded run puts the ship at the start of that room and plays on exactly like the run it
 * was taken from, a boss beaten unlocks upgrades at once (saved at once), death ends the run but keeps
 * the save, and the menus (NEW SAVE, CONTINUE, the save menu, unlocks, character select) work. */
static int save_test_run(const HeadlessOptions *o) {
    int failures = 0;
#define CHECK(cond, what)                                                       \
    do {                                                                       \
        bool ok_ = (cond);                                                     \
        printf("%-76s -> %s\n", what, ok_ ? "OK" : "FAILED");                  \
        failures += !ok_;                                                      \
    } while (0)
#define STICK() do { game_update(); clear_edges(); } while (0)
    if (!save_enabled()) {
        printf("--save-test needs --save-dir\n");
        return 1;
    }
    for (int s = 1; s <= SAVE_SLOTS; s++) save_delete(s);
    SaveInfo list[SAVE_SLOTS];
    Profile saved;
    const int fresh_pct = unlock_percent(UNLOCK_START_COUNT + 1, unlocks_total());

    /* NEW SAVE, like a player: a run starts at once, first ship, starting unlocks only. */
    game_init(o->seed);
    title_open();
    G.in.mouse = G.sel_mouse = v2(0, 0);
    G.in.confirm_pressed = true;
    STICK();
    G.god = true;
    int slot = G.save_slot, n = save_list(list, SAVE_SLOTS);
    CHECK(G.state == GS_PLAYING && !G.demo && G.room.type == ROOM_START && G.character == CHAR_NORMAL && slot == 1 && n == 1 &&
              list[0].ok && list[0].has_run && list[0].depth == 0 && list[0].percent == fresh_pct,
          "NEW SAVE: a new file and its first run, saved at once (launch bay)");
    bool locked_ok = profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT && character_unlocked(CHAR_NORMAL) &&
                     !character_unlocked(CHAR_SHOTGUNNER) && !character_unlocked(CHAR_KNIGHT);
    {
        Rng keep = G.rng;
        for (int k = 0; k < 400; k++) {
            int u = upgrade_roll();
            locked_ok = locked_ok && u >= 0 && upgrade_unlocked(u);
        }
        G.rng = keep;
    }
    CHECK(locked_ok, "a new save: the first ship and 24 upgrades, never a locked one offered");

    /* Play: one save per room entered, never during a room. The save of the 3rd room is kept. */
    int writes = save_writes(), room = G.room.id, entered = 0, wrong = 0, entry_tick = -1, entry_kills = 0, entry_room = -1;
    for (int t = 0; t < 120 * 400 && entry_tick < 0; t++) {
        bot_input();
        game_update();
        clear_edges();
        if (G.room.id != room) {
            room = G.room.id;
            wrong += save_writes() != writes + 1;
            if (++entered == 3) {
                save_copy(slot, 50);
                entry_tick = G.ticks, entry_kills = G.kills, entry_room = G.room.id;
            }
        } else {
            wrong += save_writes() != writes;
        }
        writes = save_writes();
    }
    n = save_list(list, SAVE_SLOTS);
    CHECK(entry_tick >= 0 && wrong == 0 && n == 2 && list[0].depth == 3, "saved on entering each room, never during a room");

    /* Go on 20 s, then load the save of the 3rd room and play the same 20 s again. */
    const int span = 120 * 20;
    for (int t = 0; G.ticks < entry_tick + span && t < span + 10; t++) {
        bot_input();
        game_update();
        clear_edges();
    }
    Rng rng0 = G.rng;
    V2 pos0 = G.pl.pos;
    int sig0[6] = {G.kills, G.room.id, G.nen, G.neb, G.pl.hp, G.ticks};
    bool loaded = save_load(50);
    CHECK(loaded && G.state == GS_PLAYING && G.ticks == entry_tick && G.room.id == entry_room && G.kills == entry_kills &&
              !G.room.cleared && G.save_slot == 50,
          "loading puts the ship at the start of the room, enemies to come");
    for (int t = 0; G.ticks < entry_tick + span && t < span + 10; t++) {
        bot_input();
        game_update();
        clear_edges();
    }
    int sig1[6] = {G.kills, G.room.id, G.nen, G.neb, G.pl.hp, G.ticks};
    bool same = !memcmp(&rng0, &G.rng, sizeof rng0) && pos0.x == G.pl.pos.x && pos0.y == G.pl.pos.y && !memcmp(sig0, sig1, sizeof sig0);
    printf("  (%d kills in the room after 20 s: %d / %d)\n", sig0[0] - entry_kills, sig0[0], sig1[0]);
    CHECK(same && sig0[0] > entry_kills, "a loaded save plays on exactly like the run it was taken from");

    /* A boss beaten: the next upgrades and the Knight at once, written at once (the run part stays
     * the start of the boss room), and they survive loading that room again. */
    G.entry_side = SIDE_BOTTOM;
    room_enter(ROOM_BOSS, RW_UPGRADE, BOSS_EVERY);
    save_autosave();
    int boss_room = G.room.id, pw = save_profile_writes();
    writes = save_writes();
    int next[UNLOCK_BATCH];
    unlocks_next(next, UNLOCK_BATCH);
    for (int t = 0; t < 120 * 180 && !G.room.cleared; t++) {
        bot_input();
        game_update();
        clear_edges();
    }
    bool batch = G.room.cleared;
    for (int i = 0; i < UNLOCK_BATCH; i++) batch = batch && upgrade_unlocked(next[i]);
    n = save_list(list, SAVE_SLOTS);
    int i50 = -1;
    for (int i = 0; i < n; i++)
        if (list[i].slot == 50) i50 = i;
    CHECK(batch && character_unlocked(CHAR_KNIGHT) && profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT + UNLOCK_BATCH &&
              G.prof.bosses == 1,
          "a boss beaten unlocks 6 upgrades and the Knight at once");
    CHECK(save_profile_writes() == pw + 1 && save_writes() == writes && save_load_profile(50, &saved) &&
              !memcmp(&saved, &G.prof, sizeof saved) && i50 >= 0 && list[i50].has_run && list[i50].depth == BOSS_EVERY,
          "...written at once, the run part still the start of the boss room");
    Profile after_boss = G.prof;
    CHECK(save_load(50) && G.room.id == boss_room && !G.room.cleared && !memcmp(&G.prof, &after_boss, sizeof after_boss),
          "...and kept when the boss room is loaded again");

    /* Death: the run is over, the save and its unlocks stay. */
    G.prof = after_boss;
    int runs = G.prof.runs;
    game_over();
    n = save_list(list, SAVE_SLOTS);
    i50 = -1;
    for (int i = 0; i < n; i++)
        if (list[i].slot == 50) i50 = i;
    CHECK(i50 >= 0 && list[i50].ok && !list[i50].has_run && save_load_profile(50, &saved) && !memcmp(&saved, &G.prof, sizeof saved) &&
              !save_load(50) && G.save_slot == 50 && G.prof.deaths == 1,
          "the ship destroyed: the save stays with its unlocks, without a run");
    G.gameover_t = 1.0f;
    G.in.restart_pressed = true;
    STICK();
    n = save_list(list, SAVE_SLOTS);
    i50 = -1;
    for (int i = 0; i < n; i++)
        if (list[i].slot == 50) i50 = i;
    CHECK(G.state == GS_PLAYING && G.save_slot == 50 && i50 >= 0 && list[i50].has_run && list[i50].depth == 0 &&
              G.prof.runs == runs + 1 && profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT + UNLOCK_BATCH,
          "R: a new run in the same save, its unlocks kept");

    /* Another save: its own file, its own (fresh) unlocks. */
    title_open();
    G.in.confirm_pressed = true;
    STICK();
    n = save_list(list, SAVE_SLOTS);
    CHECK(G.save_slot == 2 && n == 3 && list[0].slot == 2 && profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT,
          "NEW SAVE again: another file, listed first, with its own unlocks");
    save_delete(1);
    n = save_list(list, SAVE_SLOTS);
    CHECK(n == 2 && list[0].slot == 2 && list[1].slot == 50, "a save can be deleted");

    /* A file from another version: listed, never loaded. */
    char path[600];
    save_path(7, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (f) {
        const char junk[64] = "NBSV\x7f";
        fwrite(junk, sizeof junk, 1, f);
        fclose(f);
    }
    int room_before = G.room.id;
    n = save_list(list, SAVE_SLOTS);
    CHECK(n == 3 && list[2].slot == 7 && !list[2].ok && !save_load(7) && G.room.id == room_before && G.state == GS_PLAYING,
          "a save from another version is listed but never loaded");

    /* The menus, like a player: CONTINUE, delete the old one (DEL twice), open save 50. */
    memset(&G.in, 0, sizeof G.in); /* nothing held (the bot was driving) */
    G.in.number = -1;
    title_open();
    STICK();
    G.in.down = true;
    STICK();
    G.in.down = false;
    STICK();
    bool on_continue = G.title_sel == 1;
    G.in.confirm_pressed = true;
    STICK();
    bool page = G.title_page == 1 && G.load_sel == 0;
    for (int k = 0; k < 2; k++) {
        G.in.down = true;
        STICK();
        G.in.down = false;
        STICK();
    }
    G.in.delete_pressed = true;
    STICK();
    f = fopen(path, "rb");
    bool asked = f && G.load_confirm == 7;
    if (f) fclose(f);
    G.in.delete_pressed = true;
    STICK();
    f = fopen(path, "rb");
    if (f) fclose(f);
    bool deleted = !f && G.load_confirm == 0 && G.load_sel == 1;
    G.in.confirm_pressed = true; /* save 50 (a run going on) */
    STICK();
    CHECK(on_continue && page, "main menu: CONTINUE opens the list of saves");
    CHECK(asked && deleted, "saves list: DEL asks, DEL again deletes");
    bool menu_ok = G.state == GS_TITLE && G.title_page == 2 && G.menu_slot == 50 && G.title_sel == 0 &&
              profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT + UNLOCK_BATCH;
    CHECK(menu_ok, "ENTER opens the save's menu (RESUME RUN first), its unlocks loaded");
    /* down: NEW RUN, UNLOCKS, then BESTIARY (greyed) is skipped: back to RESUME RUN */
    int seen[4];
    for (int k = 0; k < 4; k++) {
        G.in.down = true;
        STICK();
        G.in.down = false;
        STICK();
        seen[k] = G.title_sel;
    }
    CHECK(seen[0] == 1 && seen[1] == 2 && seen[2] == 0 && seen[3] == 1, "save menu: NEW RUN, UNLOCKS; BESTIARY greyed (skipped)");
    G.in.down = true; /* UNLOCKS */
    STICK();
    G.in.down = false;
    G.in.confirm_pressed = true;
    STICK();
    bool unl = G.title_page == 3 && G.unl_sel == 0;
    G.in.down = true;
    STICK();
    G.in.down = false;
    STICK();
    G.in.right = true;
    STICK();
    G.in.right = false;
    STICK();
    unl = unl && G.unl_sel == 1 + (UPG_COUNT + 3) / 4;
    G.in.pause_pressed = true;
    STICK();
    CHECK(unl && G.title_page == 2 && G.title_sel == 2, "UNLOCKS: every upgrade listed, arrows move, ESC back");
    /* NEW RUN: the character select, the run goes in this save (no new file). */
    G.in.up = true;
    STICK();
    G.in.up = false;
    G.in.confirm_pressed = true;
    STICK();
    bool sel_ok = G.state == GS_CHAR_SELECT;
    G.in.number = CHAR_KNIGHT; /* unlocked by the boss */
    G.in.confirm_pressed = true;
    STICK();
    n = save_list(list, SAVE_SLOTS);
    CHECK(sel_ok && G.state == GS_PLAYING && G.character == CHAR_KNIGHT && G.save_slot == 50 && n == 2,
          "NEW RUN: the character select, the run starts in this save");
    /* Q from the pause: the save's menu, RESUME RUN puts the run back. */
    G.state = GS_PAUSED;
    title_back();
    STICK();
    bool back = G.state == GS_TITLE && G.title_page == 2 && G.menu_slot == 50 && G.title_sel == 0;
    G.in.confirm_pressed = true;
    STICK();
    CHECK(back && G.state == GS_PLAYING && G.save_slot == 50 && G.character == CHAR_KNIGHT && G.room.type == ROOM_START,
          "Q: back to the save's menu; RESUME RUN loads the run");
    /* The new save (nothing unlocked): its locked ships are refused, ESC goes back to its menu. */
    G.state = GS_PAUSED;
    title_back();
    STICK();
    G.in.pause_pressed = true; /* the saves list, on save 50 (played last: listed first) */
    STICK();
    bool on_50 = G.title_page == 1 && G.load_sel == 0;
    G.in.down = true;
    STICK();
    G.in.down = false;
    G.in.confirm_pressed = true;
    STICK();
    bool fresh_menu = on_50 && G.title_page == 2 && G.menu_slot == 2 && profile_upgrades_unlocked(&G.prof) == UNLOCK_START_COUNT;
    G.in.down = true;
    STICK();
    G.in.down = false;
    G.in.confirm_pressed = true;
    STICK();
    sel_ok = G.state == GS_CHAR_SELECT;
    G.in.number = CHAR_SHOTGUNNER;
    G.in.confirm_pressed = true;
    STICK();
    sel_ok = sel_ok && G.state == GS_CHAR_SELECT && G.char_hover == CHAR_SHOTGUNNER;
    G.in.pause_pressed = true;
    STICK();
    CHECK(fresh_menu && sel_ok && G.state == GS_TITLE && G.title_page == 2 && G.menu_slot == 2,
          "a new save: locked ships greyed and refused, ESC back to the save's menu");
#undef CHECK
#undef STICK
    printf("save files - %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}

int headless_run(const HeadlessOptions *o) {
    save_set_dir(o->save_dir);
    if (o->save_test) return save_test_run(o);
    for (int s = 1; s <= SAVE_SLOTS && save_enabled(); s++) save_delete(s); /* a clean folder */

    if (o->debug_rooms) return debug_rooms_run(o);
    g_fx_off = !o->fx;
    int failures = 0;
    int runs = o->runs > 0 ? o->runs : 1;
    long long total_ticks = 0;
    int best = 0, sum_rooms = 0;
    uint64_t t0 = SDL_GetPerformanceCounter();
    /* --fresh-profile: a new save's unlocks, kept from run to run like in a save file (and in the
     * same file with --save-dir); the runs then depend on the bosses beaten before them. */
    Profile prof;
    profile_fresh(&prof);
    int prof_slot = 0;

    for (int run = 0; run < runs; run++) {
        uint64_t seed = o->seed + (uint64_t)run;
        int failures_before = failures;
        game_init(seed);
        G.god = o->god;
        G.force_secret = o->secret;
        G.character = o->character;
        if (o->fresh_profile) {
            G.prof = prof;
            G.save_slot = prof_slot;
            if (!character_unlocked(G.character)) G.character = CHAR_NORMAL;
        }
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
        for (int u = UPG_W_SCATTER; u <= UPG_W_SWORD; u++)
            if ((o->weapons & (1u << (u - UPG_W_SCATTER))) && !upgrade_capped(u)) upgrade_apply(u);
        for (int i = 0; i < o->nupgrades; i++) upgrade_apply(o->upgrades[i]);
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
        else if (o->boss_room) room_enter(ROOM_BOSS, RW_UPGRADE, o->depth > 0 ? o->depth : BOSS_EVERY);
        else if (o->active_room) room_enter(ROOM_COMBAT, RW_ACTIVE, o->depth > 0 ? o->depth : 1);
        else if (o->depth > 0) room_enter(ROOM_COMBAT, RW_UPGRADE, o->depth);

        int room_id = G.room.id, secret_parent = -1;
        int run_slot = G.save_slot, saves_seen = save_writes(); /* --save-dir: one save per room entered */
        const char *save_err = save_enabled() && run_slot <= 0 ? "saving on but the run got no save file" : NULL;
        int stats_rooms = G.room.type == ROOM_STATS, shops = G.room.type == ROOM_SHOP, secrets = 0;
        int workshops = G.room.type == ROOM_WORKSHOP;
        int bosses = G.room.type == ROOM_BOSS, bosses_beaten = 0, boss_hits = 0, hits_at_entry = 0;
        float boss_time = 0.0f;
        bool was_cleared = G.room.cleared;
        int choices = 0; /* one-of-N pickups offered when the room was cleared */
        float room_time = 0.0f;
        const char *err = start_err ? start_err
                          : G.room.cleared != G.room.peaceful
                              ? "first room: only the launch bay / secret rooms start without enemies" : NULL;
        int tick = 0, hits = 0, life = G.pl.hp + G.pl.shield;
        int unlocked = profile_upgrades_unlocked(&G.prof); /* only a boss beaten unlocks upgrades */
        int taken_side = -1; /* side of the door being walked through (-1: none / secret passage) */
        static char entry_msg[128];
        for (; !err && tick < o->max_ticks && G.state != GS_GAMEOVER; tick++) {
            bot_input();
            /* Items on a pedestal / shop counter are only ever taken with E. */
            int pedestals = count_pedestals(), pk_room = G.room.id;
            bool pressed_e = G.in.interact_pressed;
            game_update();
            clear_edges();
            if (G.room.id == pk_room && !G.transitioning && !pressed_e && count_pedestals() < pedestals) {
                err = "an item was taken without pressing E";
                break;
            }
            if (G.transitioning && taken_side < 0 && G.trans_door >= 0 && G.trans_t < TRANS_TIME * 0.5f)
                taken_side = G.room.doors[G.trans_door].side;
            hits += G.pl.hp + G.pl.shield < life; /* hull or shield damage taken this tick */
            life = G.pl.hp + G.pl.shield;
            if (save_enabled() && G.state != GS_GAMEOVER) {
                int want = saves_seen + (G.room.id != room_id);
                if (save_writes() != want) {
                    err = G.room.id != room_id ? "a new room was not saved (exactly once)" : "the game saved during a room";
                    break;
                }
                saves_seen = save_writes();
            }
            if (G.room.id != room_id) {
                int from = room_id;
                room_id = G.room.id;
                room_time = 0.0f;
                /* In by the door opposite the one taken; the secret passage is the entry door, so the
                 * secret room is entered from the other side (back from it: the parent, as it was). */
                if (!G.room.revisit) {
                    Side want = G.room.type == ROOM_SECRET ? side_opposite(G.parent.entry) : side_opposite((Side)taken_side);
                    if (taken_side < 0 && G.room.type != ROOM_SECRET) want = G.room.entry; /* not walked in (cannot happen) */
                    if (G.room.entry != want) {
                        snprintf(entry_msg, sizeof entry_msg, "entered room %d by side %d, expected %d (door taken: %d)", G.room.depth,
                                 G.room.entry, want, taken_side);
                        err = entry_msg;
                        break;
                    }
                }
                taken_side = -1;
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
                    bosses += G.room.type == ROOM_BOSS;
                    hits_at_entry = hits;
                    was_cleared = false;
                }
            }
            /* The reward comes once the room has shattered (clear_t back to 0). */
            if (G.room.cleared && G.room.clear_t >= 0.0f && G.room.clear_t < DT * 1.5f && !G.room.revisit) {
                if ((err = check_room_reward()) != NULL) break;
                choices = count_choices();
            }
            if (G.room.cleared && !was_cleared && G.room.type == ROOM_BOSS && !G.room.revisit) {
                int want = unlocked + (UPG_COUNT - unlocked < UNLOCK_BATCH ? UPG_COUNT - unlocked : UNLOCK_BATCH);
                Profile saved;
                if (profile_upgrades_unlocked(&G.prof) != want || !character_unlocked(CHAR_KNIGHT)) {
                    err = "a boss beaten did not unlock the next upgrades (and the Knight) at once";
                    break;
                }
                if (save_enabled() && (!save_load_profile(G.save_slot, &saved) || memcmp(&saved, &G.prof, sizeof saved))) {
                    err = "the unlocks of a boss beaten were not saved at once";
                    break;
                }
                unlocked = want;
            }
            if (profile_upgrades_unlocked(&G.prof) != unlocked) {
                err = "upgrades unlocked without beating a boss";
                break;
            }
            if (G.room.depth >= UNLOCK_SHOTGUNNER_DEPTH && !character_unlocked(CHAR_SHOTGUNNER)) {
                err = "reached room 5 but the Shotgunner is still locked";
                break;
            }
            if (G.room.cleared && !was_cleared) {
                if (G.room.type == ROOM_BOSS) {
                    bosses_beaten++;
                    boss_time += room_time;
                    boss_hits += hits - hits_at_entry;
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
        if (!err && save_err) err = save_err;
        if (!err && save_enabled()) { /* died: the save stays, without a run; otherwise it holds the room the ship is in */
            SaveInfo list[SAVE_SLOTS];
            Profile saved;
            int n = save_list(list, SAVE_SLOTS), found = -1;
            for (int i = 0; i < n; i++)
                if (list[i].slot == run_slot) found = i;
            if (found < 0 || !list[found].ok) err = "the run's save file is gone";
            else if (G.state == GS_GAMEOVER ? list[found].has_run : !list[found].has_run || list[found].depth != G.room.depth)
                err = G.state == GS_GAMEOVER ? "the ship was destroyed but its save still holds the run" : "the save does not hold the room the ship is in";
            else if (!save_load_profile(run_slot, &saved) || memcmp(&saved, &G.prof, sizeof saved))
                err = "the save does not hold the unlocks";
            else if (list[found].percent != unlock_percent(profile_unlocked(&G.prof), unlocks_total()))
                err = "the save list shows a wrong unlock percentage";
        }
        prof = G.prof;
        prof_slot = G.save_slot;
        total_ticks += tick;
        if (G.room.type == ROOM_BOSS && !G.room.cleared) { /* died (or ran out of time) against the boss */
            boss_time += room_time;
            boss_hits += hits - hits_at_entry;
        }

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
        if (bosses) printf(" bosses=%d/%d (%.0fs, %d hits)", bosses_beaten, bosses, (double)boss_time, boss_hits);
        if (workshops) printf(" workshops=%d machine_uses=%d augments=%d", workshops, G.machine_uses, G.augments);
        if (start_revives > p->revives) printf(" second_winds=%d", start_revives - p->revives);
        if (o->fresh_profile)
            printf(" unlocked=%d/%d ships=%d%d%d", profile_upgrades_unlocked(&G.prof), UPG_COUNT, G.prof.chr[0], G.prof.chr[1], G.prof.chr[2]);
        if (stats_rooms)
            printf(" [dmg x%.2f rate x%.2f range x%.2f speed x%.2f recharge x%.2f hitbox %.1f luck %+d]", (double)p->dmg_mult,
                   (double)p->rate_mult, (double)p->range_mult, (double)(p->speed / 540.0f), (double)(p->dash_cooldown / DASH_BASE_CD),
                   (double)p->hit_radius, p->luck);
        printf(" -> %s\n", outcome);
        if (err) {
            printf("  invariant violated at tick %d: %s\n", tick, err);
            if (o->fail_shot && failures == 0) save_failure_shot(o->fail_shot);
            failures++;
        }
 else if (o->shop_room && o->coins >= 30 && G.items_bought == 0) {
            printf("  started in a shop with %d coins but never bought anything\n", G.pl.coins);
            failures++;
        } else if (o->boss_room && bosses_beaten == 0) {
            printf("  started in a boss room but never beat the boss\n");
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
        if (failures > failures_before) print_repro(o, seed, err ? tick : -1);
        best = G.rooms_cleared > best ? G.rooms_cleared : best;
        sum_rooms += G.rooms_cleared;
    }

    double secs = (double)(SDL_GetPerformanceCounter() - t0) / (double)SDL_GetPerformanceFrequency();
    printf("%d run(s): avg %.1f rooms, best %d, %lld ticks simulated in %.2f s (%.0fx real time) - %s\n", runs,
           (double)sum_rooms / runs, best, total_ticks, secs, (double)total_ticks / TICK_RATE / (secs > 0 ? secs : 1),
           failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
