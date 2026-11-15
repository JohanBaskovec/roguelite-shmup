#include "bot.h"

#include "game.h"

#define THREAT_RADIUS 150.0f
#define NAV_MARGIN (PLAYER_BODY_R + 22.0f)

/* Obstacles only (not the arena walls, which the bot must walk into to take a door). */
static bool hits_obstacle(V2 p, float r) {
    for (int i = 0; i < G.room.nobs; i++) {
        if (!obstacle_solid(&G.room.obs[i])) continue;
        SDL_FRect o = G.room.obs[i].r;
        float dx = p.x - clampf(p.x, o.x, o.x + o.w), dy = p.y - clampf(p.y, o.y, o.y + o.h);
        if (dx * dx + dy * dy < r * r) return true;
    }
    return false;
}

/* How far a barrel's blast / an oil drum's burning oil reaches (plus a margin). */
static float hazard_reach(const Obstacle *o) { return (o->kind == OB_BARREL ? BARREL_BLAST_R : FIRE_POOL_R) + 45.0f; }

/* A barrel / oil drum close to the ship, in the direction it aims at: shooting would hit it. */
static int hazard_in_aim(V2 pos, V2 aim_at) {
    V2 aim = v2_sub(aim_at, pos);
    if (v2_len2(aim) < 1.0f) return -1;
    aim = v2_norm(aim);
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if ((o->kind != OB_BARREL && o->kind != OB_OIL) || !obstacle_solid(o)) continue;
        V2 to = v2_sub(v2(o->r.x + o->r.w * 0.5f, o->r.y + o->r.h * 0.5f), pos);
        float dd = v2_len(to), along = v2_dot(aim, to), off = fabsf(aim.x * to.y - aim.y * to.x);
        /* on the line of fire (shots spread a little), within reach of its blast */
        if (dd < hazard_reach(o) + 50.0f && along > 0.0f && off < BLOCK_TILE * 0.7f + 10.0f + along * 0.12f && dd < best_d) {
            best_d = dd;
            best = i;
        }
    }
    return best;
}

void bot_input(void) {
    Input *in = &G.in;
    const Player *pl = &G.pl;
    in->up = in->down = in->left = in->right = false;
    if (G.state == GS_TITLE || G.state == GS_CHAR_SELECT) {
        in->confirm_pressed = true; /* the character was chosen beforehand (G.character, --char) */
        return;
    }
    if (G.state == GS_DEBUG || G.state == GS_UPGRADES) return;
    /* --toggle-upgrades: every 4 s, turn a random upgrade or active item off (or back on), like the TAB menu. */
    if (G.bot_toggles && G.state == GS_PLAYING && G.ticks % (4 * TICK_RATE) == 0) {
        int list[UPG_COUNT], acts[ACT_COUNT];
        int n = upgrade_menu_list(list), na = actives_owned(acts);
        int r = n + na > 0 ? irand(0, n + na - 1) : -1;
        if (r >= n) active_toggle(acts[r - n]);
        else if (r >= 0) upgrade_toggle(list[r]);
    }
    /* Like a player in the TAB menu: when its shots have hit nothing for a while (a weapon mod
     * combo that does not work here), turn one of its weapon mods off, every 10 s. */
    if (G.state == GS_PLAYING && !G.room.cleared && G.nen > 0 && G.room.type != ROOM_START &&
        G.time - G.last_enemy_damage_t > 10.0f && G.ticks % (10 * TICK_RATE) == 0) {
        int mods[UPG_COUNT], n = 0;
        for (int u = UPG_W_SCATTER; u < UPG_W_SWORD; u++)
            if (upg_has(u) && !pl->upg_off[u] && pl->upg_count[u] > 0) mods[n++] = u;
        if (n > 0) upgrade_toggle(mods[irand(0, n - 1)]);
    }

    /* Aim at the nearest enemy in line of sight, otherwise remember the nearest one. */
    Enemy *t = NULL, *hidden = NULL, *over = NULL, *nearest = NULL;
    float best = 1e30f, best_hidden = 1e30f, best_any = 1e30f;
    /* Sword (Knight): melee - walk up to the nearest enemy, the blade ignores cover. */
    const bool melee = sword_on();
    const bool short_range = !melee && upg_has(UPG_W_FLAMER); /* closes in, but not to contact */
    bool overlapped = false;
    int masked_by = -1; /* the hazard hiding the nearest enemy otherwise in sight */
    float best_masked = 1e30f;
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
        if (enemy_untouchable(e)) d2 += 600.0f * 600.0f; /* rather shoot at something it can hit */
        /* Bullets are up to ~8 px wide. Breakable blocks in the way get shot down on the way to it
         * (but not a barrel about to blow up in its face). */
        bool open = room_line_clear(pl->pos, e->pos, 10.0f) || room_line_clear_walls(pl->pos, e->pos, 10.0f);
        int hz = open && !melee ? hazard_in_aim(pl->pos, e->pos) : -1;
        if (hz >= 0 && d2 < best_masked) { /* only a barrel / oil drum too close in the way */
            best_masked = d2;
            masked_by = hz;
        }
        if (open && hz < 0) {
            if (d2 < best) { best = d2; t = e; }
        } else if (d2 < best_hidden) {
            best_hidden = d2;
            hidden = e;
        }
    }
    /* Point-blank shots still connect (enemy hits are resolved before walls). */
    in->mouse = melee && nearest ? nearest->pos
              : t ? t->pos : over ? over->pos : hidden ? hidden->pos : v2(ARENA_CX, ARENA_Y0 + 120.0f);
    /* Nothing left to shoot (cleared room): stop firing, recoil would only get in the way of the doors. */
    in->fire = !(G.room.cleared && G.nen == 0);

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
    /* Shop: buy the best affordable item (weapon mods, then upgrades, then trades; cheaper first). */
    int best_rank = 1 << 30;
    for (int i = 0; i < G.npk; i++) {
        const Pickup *k = &G.pk[i];
        if (k->price <= 0 || k->price > pl->coins) continue;
        int rank = (k->type == PK_UPGRADE ? (upgrade_is_weapon(k->sub) ? 0 : 1) : 2) * 1000 + k->price;
        /* Supplies only when needed: a heart when hurt (first when low), a bomb / key when out. */
        if (k->type == PK_HEART) {
            if (pl->hp > pl->max_hp - 2) continue;
            rank = (pl->hp <= pl->max_hp / 2 ? -1 : 3) * 1000 + k->price;
        } else if (k->type == PK_BOMB || k->type == PK_KEY) {
            if (k->type == PK_BOMB ? pl->bombs >= 2 : pl->keys >= 1) continue;
            rank = 4000 + k->price;
        }
        if (rank < best_rank) {
            best_rank = rank;
            offer = k;
        }
    }

    /* Free pedestals that are not a choice (secret room loot): take them all. */
    for (int i = 0; i < G.npk && !offer; i++) {
        const Pickup *k = &G.pk[i];
        if (k->price == 0 && !k->choice && (k->type == PK_UPGRADE || k->type == PK_STAT)) offer = k;
    }
    /* Secret room: once it has seen the flash, bomb next to the bottom door after the fight, then go in. */
    const Room *r = &G.room;
    bool secret_known = r->has_secret && (r->ghosts > 0 || upg_has(UPG_XRAY)) && !r->secret_used;

    /* Workshop machines: the supply press while healthy, a few slot machine plays while
     * it has coins (E on it, one press at a time). */
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

    /* E takes / buys what is in reach: only press once the chosen one is the nearest. */
    int reach_pk, reach_mc;
    interact_target(&reach_pk, &reach_mc);
    if (offer) {
        goal = offer->pos;
        in->interact_pressed = reach_pk >= 0 && &G.pk[reach_pk] == offer;
    } else if (machine) {
        goal = machine->pos;
        in->interact_pressed = reach_mc >= 0 && &G.mc[reach_mc] == machine && machine->spin <= 0.0f &&
                               machine->anim <= 0.0f && G.ticks % 30 == 0;
    } else if (G.room.cleared && secret_known && !r->secret_open && pl->bombs > 0) {
        goal = room_secret_spot();
        if (v2_dist(pl->pos, goal) < SECRET_BOMB_RADIUS * 0.5f) in->bomb_pressed = true;
    } else if (G.room.cleared && (r->secret_open || r->type == ROOM_SECRET)) {
        goal = door_position(r->entry); /* into the secret room, or back out of it */
    } else if (G.room.cleared) {
        /* Pick the most valuable door it can open. A door it cannot reach (no progress for
         * 3 s, e.g. a path that flips back and forth through a tight gap) is given up. */
        static int stuck_room = -1;
        static float stuck_t, last_clear_t;
        static V2 stuck_ref;
        static int give_ups;
        if (G.room.id != stuck_room || G.room.clear_t < last_clear_t) {
            stuck_room = G.room.id;
            stuck_t = 0.0f;
            stuck_ref = pl->pos;
            give_ups = 0;
        }
        last_clear_t = G.room.clear_t;
        if (v2_dist2(pl->pos, stuck_ref) > 60.0f * 60.0f) {
            stuck_ref = pl->pos;
            stuck_t = 0.0f;
        } else if (G.room.clear_t > DOORS_AT + 0.6f && (stuck_t += DT) > 3.0f) {
            stuck_t = 0.0f;
            give_ups++; /* next best door */
        }
        int pick = 0, scores[MAX_DOORS];
        for (int i = 0; i < G.room.ndoors; i++) {
            scores[i] = -1;
            const Door *d = &G.room.doors[i];
            if (d->locked && pl->keys == 0) continue;
            int score = d->type == ROOM_SHOP ? (pl->coins >= 25 ? 7 : pl->coins >= 14 ? 4 : 0)
                      : d->type == ROOM_STATS ? 4
                      : d->type == ROOM_WORKSHOP ? (pl->coins >= GAMBLE_COST || pl->max_hp >= 6 ? 5 : 2)
                      : d->reward == RW_ACTIVE ? (G.active_uses == 0 && !pl->act[ACT_DASH].level ? 7 : 4)
                      : d->reward == RW_UPGRADE ? 5
                      : d->reward == RW_HEART && pl->hp <= pl->max_hp / 2 ? 6
                      : d->reward == RW_COINS && pl->coins < 40 ? 4
                      : d->reward == RW_KEY ? 3 : d->reward == RW_BOMBS ? 2 : 1;
            scores[i] = score * MAX_DOORS + (MAX_DOORS - 1 - i); /* ties: first door first */
        }
        /* The (give_ups)-th best door it can open. */
        int eligible = 0;
        for (int i = 0; i < G.room.ndoors; i++) eligible += scores[i] >= 0;
        for (int rank = 0, skip = eligible ? give_ups % eligible : 0, taken = 0; rank <= skip; rank++) {
            int best = -1;
            for (int i = 0; i < G.room.ndoors; i++)
                if (scores[i] >= 0 && !(taken & (1 << i)) && (best < 0 || scores[i] > scores[best])) best = i;
            if (best < 0) break;
            taken |= 1 << best;
            pick = best;
        }
        goal = G.room.clear_t < DOORS_AT ? v2(ARENA_CX, ARENA_CY) : door_position(G.room.doors[pick].side);
    } else if (short_range && nearest) {
        V2 to = v2_sub(nearest->pos, pl->pos);
        float d = v2_len(to);
        goal = d > 1.0f ? v2_sub(nearest->pos, v2_scale(to, (nearest->radius + 170.0f) / d)) : pl->pos;
    } else if (melee && nearest) {
        /* Stand just inside the blade's reach, outside contact range. */
        V2 to = v2_sub(nearest->pos, pl->pos);
        float d = v2_len(to);
        goal = d > 1.0f ? v2_sub(nearest->pos, v2_scale(to, (nearest->radius + 55.0f) / d)) : pl->pos;
    } else if (t && t->type == EN_MIRROR && enemy_blocks_shot(t, pl->pos)) {
        /* Its front plate stops the shots: go around it. */
        float side = (t->uid & 1) ? 2.3f : -2.3f;
        goal = v2_add(t->pos, v2_scale(v2_angle(t->angle + side), 320.0f));
        goal.x = clampf(goal.x, ARENA_X0 + 60.0f, ARENA_X1 - 60.0f);
        goal.y = clampf(goal.y, ARENA_Y0 + 60.0f, ARENA_Y1 - 60.0f);
    } else if (!t && hidden) {
        goal = hidden->pos; /* nothing visible: go hunt */
    } else {
        float a = G.time * 1.2f;
        goal = v2(ARENA_CX + cosf(a) * 450.0f, ARENA_CY + sinf(a) * 280.0f);
    }
    /* Its shots have hit nothing for a long while (an odd corner of the layout): roam somewhere else. */
    if (!G.room.cleared && G.nen > 0 && G.time - G.last_enemy_damage_t > 15.0f && !melee) {
        static V2 roam;
        static float roam_t = -1.0f;
        if (G.time - roam_t > 3.0f || G.time < roam_t) {
            roam_t = G.time;
            roam = v2(frand(ARENA_X0 + 100.0f, ARENA_X1 - 100.0f), frand(ARENA_Y0 + 100.0f, ARENA_Y1 - 100.0f));
        }
        goal = roam;
        masked_by = -1;
    }
    /* Nothing to shoot but behind a barrel / oil drum close by: step back out of its reach, then fire. */
    if (!t && masked_by >= 0 && !G.room.cleared) {
        const Obstacle *o = &G.room.obs[masked_by];
        V2 hc = v2(o->r.x + o->r.w * 0.5f, o->r.y + o->r.h * 0.5f), away = v2_sub(pl->pos, hc);
        away = v2_len2(away) > 1.0f ? v2_norm(away) : v2(0, 1);
        goal = v2_add(hc, v2_scale(away, hazard_reach(o) + 90.0f));
        goal.x = clampf(goal.x, ARENA_X0 + 60.0f, ARENA_X1 - 60.0f);
        goal.y = clampf(goal.y, ARENA_Y0 + 60.0f, ARENA_Y1 - 60.0f);
    }
    /* A stand-off spot inside an obstacle cannot be reached (the path finder would push straight
     * into the wall): go for the enemy itself, the path then leads around the obstacle. */
    if ((melee || short_range) && nearest && hits_obstacle(goal, NAV_MARGIN)) goal = nearest->pos;
    /* The way is blocked: plan through the breakable blocks and shoot (or slash) the first one on it. */
    /* Worked out again every few ticks, or when the goal moved / the waypoint is reached (rooms
     * full of blocks make the path finder costly). */
    static V2 nav_next, nav_goal;
    static bool nav_reach;
    static int nav_room = -1, nav_nobs = -1;
    if (G.room.id != nav_room || G.room.nobs != nav_nobs || G.ticks % 4 == 0 || v2_dist2(goal, nav_goal) > 30.0f * 30.0f ||
        v2_dist2(pl->pos, nav_next) < 10.0f * 10.0f) {
        nav_reach = true;
        nav_next = room_navigate_ex(pl->pos, goal, NAV_MARGIN, false, &nav_reach);
        if (!nav_reach) nav_next = room_navigate_ex(pl->pos, goal, NAV_MARGIN, true, NULL);
        nav_goal = goal;
        nav_room = G.room.id;
        nav_nobs = G.room.nobs;
    }
    V2 next = nav_next;
    int dig = nav_reach ? -1 : room_block_between(pl->pos, next, NAV_MARGIN);
    V2 dir = v2_norm(v2_sub(next, pl->pos));
    if (dig >= 0 && (!t || G.room.cleared)) {
        /* A barrel / oil drum on the way or right next to the block: that one first, from afar. */
        SDL_FRect br = G.room.obs[dig].r;
        int h = melee ? -1 : hazard_in_aim(pl->pos, v2(br.x + br.w * 0.5f, br.y + br.h * 0.5f));
        if (h >= 0) dig = h;
        const Obstacle *bo = &G.room.obs[dig];
        br = bo->r;
        V2 bc = v2(br.x + br.w * 0.5f, br.y + br.h * 0.5f);
        float bd = v2_dist(pl->pos, bc);
        in->mouse = bc;
        in->fire = true;
        if ((bo->kind == OB_BARREL || bo->kind == OB_OIL) && bd < hazard_reach(bo) + 60.0f) {
            if (melee && pl->bombs > 0 && G.room.cleared) in->bomb_pressed = true; /* a bomb breaks it from afar */
            else if (!melee) {
                dir = v2_norm(v2_sub(pl->pos, bc)); /* back off, then shoot it */
                in->fire = false;
            }
        }
    }
    /* Never shoot at a barrel / oil drum close by. */
    if (!melee && in->fire && hazard_in_aim(pl->pos, in->mouse) >= 0) in->fire = false;

    /* Dodge: move sideways out of the path of incoming bullets, away from enemies. */
    V2 avoid = v2(0, 0);
    int threats = 0;
    bool imminent = false;
    const EBullet *closest_threat = NULL;
    float closest_d = 1e30f;
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        /* Ground hazards: a grenade about to land, a mine, a still bullet (serpent trail). */
        bool still = b->kind == EB_MINE || v2_len2(b->vel) < 30.0f * 30.0f;
        if (b->kind == EB_GRENADE || still) {
            V2 spot = b->kind == EB_GRENADE ? v2_add(b->pos, v2_scale(b->vel, fmaxf(b->life, 0.0f))) : b->pos;
            float keep = b->kind == EB_GRENADE ? 90.0f : b->kind == EB_MINE ? 125.0f : 45.0f;
            V2 d = v2_sub(pl->pos, spot);
            float dd = v2_len(d);
            if (dd < keep && (b->kind != EB_GRENADE || b->life < 0.8f)) {
                avoid = v2_add(avoid, v2_scale(dd > 0.5f ? v2_scale(d, 1.0f / dd) : v2(0, -1), 1.5f * (1.0f - dd / keep) + 0.3f));
                if (b->kind != EB_GRENADE) threats++;
            }
            if (ebullet_airborne(b)) continue; /* harmless until it lands */
            if (still) continue;
        }
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
    /* Barrels and oil drums: keep a little away from them during a fight. Only a nudge: the part
     * pushing back against the way to go is dropped, or two drums side by side pin the ship. */
    V2 shy = v2(0, 0);
    for (int i = 0; i < G.room.nobs && !G.room.cleared; i++) {
        const Obstacle *o = &G.room.obs[i];
        if ((o->kind != OB_BARREL && o->kind != OB_OIL) || !obstacle_solid(o)) continue;
        V2 d = v2_sub(pl->pos, v2(o->r.x + o->r.w * 0.5f, o->r.y + o->r.h * 0.5f));
        float dd = v2_len(d), danger = hazard_reach(o);
        if (dd < danger) shy = v2_add(shy, v2_scale(dd > 0.5f ? v2_scale(d, 1.0f / dd) : v2(0, -1), 0.25f * (1.0f - dd / danger) + 0.05f));
    }
    float back = v2_dot(shy, dir);
    if (back < 0.0f) shy = v2_sub(shy, v2_scale(dir, back));
    avoid = v2_add(avoid, shy);
    /* Burning oil: keep out of it. */
    for (int i = 0; i < G.room.nfires; i++) {
        const FirePool *f = &G.room.fires[i];
        if (f->t < 0.4f) continue;
        V2 d = v2_sub(pl->pos, f->pos);
        float dd = v2_len(d), keep = f->r + 45.0f;
        if (dd < keep) {
            avoid = v2_add(avoid, v2_scale(dd > 0.5f ? v2_scale(d, 1.0f / dd) : v2(0, -1), 2.0f * (1.0f - dd / keep) + 0.4f));
            threats++;
        }
    }
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        V2 d = v2_sub(pl->pos, e->pos);
        /* Something about to ram it (a rammer lining up / charging, a top, a hunter): get off its line. */
        V2 line = v2(0, 0);
        if ((e->type == EN_RAMMER || e->type == EN_HUNTER) && (e->state == 1 || e->state == 2)) line = v2_angle(e->angle);
        else if (e->type != EN_LEAPER && v2_len2(e->vel) > 330.0f * 330.0f) line = v2_norm(e->vel);
        if (v2_len2(line) > 0.0f && v2_dot(line, d) > 0.0f && v2_len2(d) < 700.0f * 700.0f) {
            V2 lat = v2_sub(d, v2_scale(line, v2_dot(d, line)));
            float miss = v2_len(lat);
            if (miss < e->radius + 60.0f) {
                avoid = v2_add(avoid, v2_scale(miss > 0.5f ? v2_scale(lat, 1.0f / miss) : v2_perp(line), 2.0f));
                threats++;
            }
        }
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
    /* Active items: one press triggers every item ready, so press when any of them is worth it. */
    if (!G.room.cleared) {
        int alive = 0;
        for (int i = 0; i < G.nen; i++) alive += G.en[i].hp > 0.0f;
        for (int a = ACT_NONE + 1; a < ACT_COUNT && !in->active_pressed; a++) {
            const ActiveSlot *k = &pl->act[a];
            if (k->level == 0 || k->off || k->cd > 0.0f || k->t > 0.0f) continue;
            switch (a) {
            case ACT_DASH: in->active_pressed = imminent || overlapped; break;
            case ACT_SHIELD: in->active_pressed = imminent || overlapped || threats >= 4; break;
            case ACT_BOOM: /* plenty of shots out near enemies, or (sword) enemies right around */
                in->active_pressed = alive > 0 && (melee ? nearest && best_any < 220.0f * 220.0f : G.npb >= 10);
                break;
            case ACT_STRENGTH: in->active_pressed = alive >= 3 && (t || melee); break;
            case ACT_CHAOS: in->active_pressed = alive >= 5; break;
            case ACT_REVERSAL: in->active_pressed = G.neb >= 40 || threats >= 5; break;
            case ACT_TURRET: in->active_pressed = alive >= 3; break;
            case ACT_BLINK: in->active_pressed = imminent || overlapped; break;
            case ACT_NOVA: in->active_pressed = alive >= 3 && nearest && best_any < 380.0f * 380.0f; break;
            case ACT_TIME_STOP: in->active_pressed = threats >= 5 || (imminent && alive >= 3); break;
            case ACT_BLADES: in->active_pressed = threats >= 3 || (alive >= 2 && nearest && best_any < 240.0f * 240.0f); break;
            default: break;
            }
        }
        /* BLINK jumps to the cursor: away from the danger, not onto the enemy being aimed at. */
        if (in->active_pressed && active_level_on(ACT_BLINK) && pl->act[ACT_BLINK].cd <= 0.0f && (imminent || overlapped))
            in->mouse = v2_add(pl->pos, v2_scale(v2_len2(avoid) > 0.0f ? v2_norm(avoid) : v2_norm(v2_sub(v2(ARENA_CX, ARENA_CY), pl->pos)), 320.0f));
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
