#include "bot.h"

#include "game.h"

#define THREAT_RADIUS 150.0f
#define NAV_MARGIN (PLAYER_BODY_R + 22.0f)

/* Obstacles only (not the arena walls, which the bot must walk into to take a door). */
static bool hits_obstacle(V2 p, float r) {
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect o = G.room.obs[i];
        float dx = p.x - clampf(p.x, o.x, o.x + o.w), dy = p.y - clampf(p.y, o.y, o.y + o.h);
        if (dx * dx + dy * dy < r * r) return true;
    }
    return false;
}

void bot_input(void) {
    Input *in = &G.in;
    const Player *pl = &G.pl;
    in->up = in->down = in->left = in->right = false;
    if (G.state == GS_TITLE || G.state == GS_CHAR_SELECT) {
        in->confirm_pressed = true; /* the character was chosen beforehand (G.character, --char) */
        return;
    }
    if (G.state == GS_DEBUG) return;

    /* Aim at the nearest enemy in line of sight, otherwise remember the nearest one. */
    Enemy *t = NULL, *hidden = NULL, *over = NULL, *nearest = NULL;
    float best = 1e30f, best_hidden = 1e30f, best_any = 1e30f;
    /* Sword (Knight): melee - walk up to the nearest enemy, the blade ignores cover. The
     * bot sticks to the sword even after picking up a gun, so simulations exercise it. */
    if (pl->wlevel[WPN_SWORD] > 0 && pl->weapon != WPN_SWORD) in->weapon_select = WPN_SWORD;
    /* --weapon: same idea, the simulations keep using the weapon under test. */
    else if (G.bot_weapon >= 0 && pl->wlevel[G.bot_weapon] > 0 && pl->weapon != G.bot_weapon) in->weapon_select = G.bot_weapon;
    const bool melee = pl->weapon == WPN_SWORD;
    const bool short_range = pl->weapon == WPN_FLAMER; /* closes in, but not to contact */
    bool overlapped = false;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        float d2 = v2_dist2(e->pos, pl->pos);
        if (d2 < best_any) {
            best_any = d2;
            nearest = e;
        }
        /* An enemy sitting on top of the ship cannot be aimed at: get out instead. */
        if (d2 < (e->radius + 24.0f) * (e->radius + 24.0f)) {
            overlapped = true;
            over = e;
            continue;
        }
        if (room_line_clear(pl->pos, e->pos, 10.0f)) { /* bullets are up to ~8 px wide */
            if (d2 < best) { best = d2; t = e; }
        } else if (d2 < best_hidden) {
            best_hidden = d2;
            hidden = e;
        }
    }
    /* Point-blank shots still connect (enemy hits are resolved before walls). */
    in->mouse = melee && nearest ? nearest->pos
              : t ? t->pos : over ? over->pos : hidden ? hidden->pos : v2(ARENA_CX, ARENA_Y0 + 120.0f);
    in->fire = true;

    V2 goal;
    /* One-of-N choice (stat trades, weapon / upgrade rooms): take one (varies with depth), then leave. */
    /* Augments cost heart containers: only those that leave at least 2. */
    const Pickup *offer = NULL;
    int noffers = 0;
#define ELIGIBLE(k) ((k)->choice && !((k)->type == PK_AUGMENT && pl->max_hp - 2 * UPGRADES[(k)->sub].tier < 4))
    for (int i = 0; i < G.npk; i++) noffers += ELIGIBLE(&G.pk[i]);
    for (int i = 0, k = 0; i < G.npk && noffers > 0; i++)
        if (ELIGIBLE(&G.pk[i]) && k++ == G.room.depth % noffers) offer = &G.pk[i];
#undef ELIGIBLE
    /* Shop: buy the best affordable item (weapons, then upgrades, then trades; cheaper first). */
    int best_rank = 1 << 30;
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (k->price <= 0 || k->price > pl->coins) continue;
        int rank = (k->type == PK_WEAPON ? 0 : k->type == PK_UPGRADE ? 1 : 2) * 1000 + k->price;
        if (rank < best_rank) {
            best_rank = rank;
            offer = k;
        }
    }

    /* Free pedestals that are not a choice (secret room loot): take them all. */
    for (int i = 0; i < G.npk && !offer; i++) {
        const Pickup *k = &G.pk[i];
        if (k->price == 0 && !k->choice && (k->type == PK_WEAPON || k->type == PK_UPGRADE)) offer = k;
    }
    /* Secret room: once it has seen the corner flash, bomb it after the fight, then go in. */
    const Room *r = &G.room;
    bool secret_known = r->secret_corner >= 0 && (r->secret_flashes > 0 || upg_has(UPG_XRAY)) && !r->secret_used;

    /* Workshop machines: the supply press while healthy, a few slot machine plays while
     * it has coins. A use needs a fresh step: walk on, step off below, repeat. */
    const Machine *machine = NULL;
    for (int i = 0; i < G.nmc && G.room.cleared && !offer; i++) {
        const Machine *m = &G.mc[i];
        bool want = !m->broken && (m->type == MC_SUPPLY ? pl->hp + pl->shield >= 5
                                                         : pl->coins >= GAMBLE_COST && m->plays < 3);
        if (want) {
            machine = m;
            break;
        }
    }

    if (offer) {
        goal = offer->pos;
    } else if (machine) {
        bool wait = machine->inside || machine->spin > 0.0f;
        goal = wait ? v2_add(machine->pos, v2(0.0f, 140.0f)) : machine->pos;
    } else if (G.room.cleared && secret_known && !r->secret_open && pl->bombs > 0) {
        goal = room_secret_spot(r->secret_corner);
        if (v2_dist(pl->pos, goal) < SECRET_BOMB_RADIUS * 0.5f) in->bomb_pressed = true;
    } else if (G.room.cleared && (r->secret_open || r->type == ROOM_SECRET)) {
        goal = door_position(SIDE_BOTTOM); /* into the secret room, or back out of it */
    } else if (G.room.cleared) {
        /* Pick the most valuable door it can open. */
        int pick = 0, best_score = -1;
        for (int i = 0; i < G.room.ndoors; i++) {
            const Door *d = &G.room.doors[i];
            if (d->locked && pl->keys == 0) continue;
            int score = d->type == ROOM_SHOP ? (pl->coins >= 25 ? 7 : pl->coins >= 14 ? 4 : 0)
                      : d->type == ROOM_STATS ? 4
                      : d->type == ROOM_WORKSHOP ? (pl->coins >= GAMBLE_COST || pl->max_hp >= 6 ? 5 : 2)
                      : d->reward == RW_ACTIVE ? (pl->active == ACT_NONE ? 7 : 2)
                      : d->reward == RW_UPGRADE || d->reward == RW_WEAPON ? 5
                      : d->reward == RW_HEART && pl->hp <= pl->max_hp / 2 ? 6
                      : d->reward == RW_COINS && pl->coins < 40 ? 4
                      : d->reward == RW_KEY ? 3 : d->reward == RW_BOMBS ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                pick = i;
            }
        }
        goal = G.room.clear_t < 1.2f ? v2(ARENA_CX, ARENA_CY) : door_position(G.room.doors[pick].side);
    } else if (short_range && nearest) {
        V2 to = v2_sub(nearest->pos, pl->pos);
        float d = v2_len(to);
        goal = d > 1.0f ? v2_sub(nearest->pos, v2_scale(to, (nearest->radius + 170.0f) / d)) : pl->pos;
    } else if (melee && nearest) {
        /* Stand just inside the blade's reach, outside contact range. */
        V2 to = v2_sub(nearest->pos, pl->pos);
        float d = v2_len(to);
        goal = d > 1.0f ? v2_sub(nearest->pos, v2_scale(to, (nearest->radius + 55.0f) / d)) : pl->pos;
    } else if (!t && hidden) {
        goal = hidden->pos; /* nothing visible: go hunt */
    } else {
        float a = G.time * 1.2f;
        goal = v2(ARENA_CX + cosf(a) * 450.0f, ARENA_CY + sinf(a) * 280.0f);
    }
    V2 dir = v2_norm(v2_sub(room_navigate(pl->pos, goal, NAV_MARGIN), pl->pos));

    /* Dodge: move sideways out of the path of incoming bullets, away from enemies. */
    V2 avoid = v2(0, 0);
    int threats = 0;
    bool imminent = false;
    const EBullet *closest_threat = NULL;
    float closest_d = 1e30f;
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        V2 d = v2_sub(pl->pos, b->pos);
        float dist2 = v2_len2(d);
        if (dist2 > THREAT_RADIUS * THREAT_RADIUS || v2_dot(b->vel, d) <= 0.0f) continue;
        float dist = sqrtf(dist2);
        V2 bv = v2_norm(b->vel);
        V2 lateral = v2_sub(d, v2_scale(bv, v2_dot(d, bv))); /* offset from the bullet's line */
        float miss = v2_len(lateral);
        if (miss > 40.0f) continue;
        threats++;
        if (dist < 50.0f) imminent = true;
        if (dist < closest_d) {
            closest_d = dist;
            closest_threat = b;
        }
        V2 away = miss > 0.5f ? v2_scale(lateral, 1.0f / miss) : v2_perp(bv);
        avoid = v2_add(avoid, v2_scale(away, 1.0f - dist / THREAT_RADIUS));
    }
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        V2 d = v2_sub(pl->pos, e->pos);
        float r = e->radius + (melee ? 35.0f : 90.0f);
        float dist2 = v2_len2(d);
        if (dist2 < r * r && dist2 > 0.01f) avoid = v2_add(avoid, v2_scale(v2_norm(d), 1.5f));
    }
    if (v2_len2(avoid) > 0.0f) dir = v2_norm(v2_add(v2_scale(dir, 0.4f), v2_scale(avoid, melee ? 1.5f : 3.0f)));
    /* Nothing to slash within reach: cut the closest incoming bullet instead. */
    if (melee && closest_threat && closest_d < 110.0f &&
        (!nearest || sqrtf(best_any) > sword_reach() + nearest->radius))
        in->mouse = closest_threat->pos;

    /* Get out from under an enemy: away from it, never into a wall (the centre may be behind cover). */
    if (overlapped) {
        V2 away = v2_sub(pl->pos, over->pos);
        V2 centre = v2_norm(v2_sub(v2(ARENA_CX, ARENA_CY), pl->pos));
        dir = v2_len2(away) > 1.0f ? v2_norm(v2_add(v2_norm(away), v2_scale(centre, 0.3f))) : centre;
    }
    /* Active item, when it is ready and worth it. */
    if (pl->active != ACT_NONE && pl->act_cd <= 0.0f && pl->act_t <= 0.0f && !G.room.cleared) {
        int alive = 0;
        for (int i = 0; i < G.nen; i++) alive += G.en[i].hp > 0.0f;
        switch (pl->active) {
        case ACT_DASH: in->active_pressed = imminent || overlapped; break;
        case ACT_SHIELD: in->active_pressed = imminent || overlapped || threats >= 4; break;
        case ACT_BOOM: /* plenty of shots out near enemies, or (sword) enemies right around */
            in->active_pressed = alive > 0 && (melee ? nearest && best_any < 220.0f * 220.0f : G.npb >= 25);
            break;
        case ACT_STRENGTH: in->active_pressed = alive >= 3 && (t || melee); break;
        case ACT_CHAOS: in->active_pressed = alive >= 5; break;
        case ACT_REVERSAL: in->active_pressed = G.neb >= 40 || threats >= 5; break;
        case ACT_TURRET: in->active_pressed = alive >= 3; break;
        case ACT_BLINK:
            if (imminent || overlapped) {
                /* jump away from the danger, not onto the enemy being aimed at */
                in->mouse = v2_add(pl->pos, v2_scale(v2_len2(avoid) > 0.0f ? v2_norm(avoid) : v2_norm(v2_sub(v2(ARENA_CX, ARENA_CY), pl->pos)), 320.0f));
                in->active_pressed = true;
            }
            break;
        case ACT_NOVA: in->active_pressed = alive >= 3 && nearest && best_any < 380.0f * 380.0f; break;
        case ACT_TIME_STOP: in->active_pressed = threats >= 5 || (imminent && alive >= 3); break;
        case ACT_BLADES: in->active_pressed = threats >= 3 || (alive >= 2 && nearest && best_any < 240.0f * 240.0f); break;
        default: break;
        }
    }
    if (pl->hp <= 2 && threats >= 6 && pl->bombs > 0 && pl->invuln <= 0.0f) in->bomb_pressed = true;

    /* Keys only give 8 directions. Take the one closest to dir that does not run into an
     * obstacle: a diagonal aimed exactly at a corner would otherwise pin the ship there. */
    int best_k = -1;
    float best_dot = 0.3f;
    for (int k = 0; k < 8; k++) {
        V2 d = v2_angle((float)k * (TAU_F / 8.0f));
        float dot = v2_dot(d, dir);
        if (dot > best_dot && !hits_obstacle(v2_add(pl->pos, v2_scale(d, 6.0f)), PLAYER_BODY_R + 0.5f)) {
            best_dot = dot;
            best_k = k;
        }
    }
    V2 q = best_k >= 0 ? v2_angle((float)best_k * (TAU_F / 8.0f)) : dir;
    in->right = q.x > 0.38f;
    in->left = q.x < -0.38f;
    in->down = q.y > 0.38f;
    in->up = q.y < -0.38f;
}
