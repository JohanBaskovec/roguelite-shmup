#include "bot.h"

#include "game.h"

#define THREAT_RADIUS 150.0f
#define NAV_MARGIN (PLAYER_BODY_R + 22.0f)

void bot_input(void) {
    Input *in = &G.in;
    const Player *pl = &G.pl;
    in->up = in->down = in->left = in->right = false;
    if (G.state == GS_TITLE) {
        in->confirm_pressed = true;
        return;
    }

    /* Aim at the nearest enemy in line of sight, otherwise remember the nearest one. */
    Enemy *t = NULL, *hidden = NULL, *over = NULL;
    float best = 1e30f, best_hidden = 1e30f;
    bool overlapped = false;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        float d2 = v2_dist2(e->pos, pl->pos);
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
    in->mouse = t ? t->pos : over ? over->pos : hidden ? hidden->pos : v2(ARENA_CX, ARENA_Y0 + 120.0f);
    in->fire = true;

    V2 goal;
    /* Stats room: take one of the trades (varies with depth), then leave. */
    const Pickup *offer = NULL;
    int noffers = 0;
    for (int i = 0; i < G.npk; i++) noffers += G.pk[i].type == PK_STAT && G.pk[i].price == 0;
    for (int i = 0, k = 0; i < G.npk && noffers > 0; i++)
        if (G.pk[i].type == PK_STAT && G.pk[i].price == 0 && k++ == G.room.depth % noffers) offer = &G.pk[i];
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

    if (offer) {
        goal = offer->pos;
    } else if (G.room.cleared) {
        /* Pick the most valuable door it can open. */
        int pick = 0, best_score = -1;
        for (int i = 0; i < G.room.ndoors; i++) {
            const Door *d = &G.room.doors[i];
            if (d->locked && pl->keys == 0) continue;
            int score = d->type == ROOM_SHOP ? (pl->coins >= 25 ? 7 : pl->coins >= 14 ? 4 : 0)
                      : d->type == ROOM_STATS ? 4
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
        V2 away = miss > 0.5f ? v2_scale(lateral, 1.0f / miss) : v2_perp(bv);
        avoid = v2_add(avoid, v2_scale(away, 1.0f - dist / THREAT_RADIUS));
    }
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        V2 d = v2_sub(pl->pos, e->pos);
        float r = e->radius + 90.0f;
        float dist2 = v2_len2(d);
        if (dist2 < r * r && dist2 > 0.01f) avoid = v2_add(avoid, v2_scale(v2_norm(d), 1.5f));
    }
    if (v2_len2(avoid) > 0.0f) dir = v2_norm(v2_add(v2_scale(dir, 0.4f), v2_scale(avoid, 3.0f)));

    /* Get out from under an enemy: away from it, never into a wall (the centre may be behind cover). */
    if (overlapped) {
        V2 away = v2_sub(pl->pos, over->pos);
        V2 centre = v2_norm(v2_sub(v2(ARENA_CX, ARENA_CY), pl->pos));
        dir = v2_len2(away) > 1.0f ? v2_norm(v2_add(v2_norm(away), v2_scale(centre, 0.3f))) : centre;
    }
    if ((imminent || overlapped) && pl->dash_cd <= 0.0f) in->dash_pressed = true;
    if (pl->hp <= 2 && threats >= 6 && pl->bombs > 0 && pl->invuln <= 0.0f) in->bomb_pressed = true;

    in->right = dir.x > 0.38f;
    in->left = dir.x < -0.38f;
    in->down = dir.y > 0.38f;
    in->up = dir.y < -0.38f;
}
