#include "draw.h"
#include "game.h"
#include "audio.h"

#include <string.h>

const EnemyDef ENEMIES[EN_COUNT] = {
    /*            name        radius  hp   speed cost depth color     sides */
    [EN_CHASER]  = {"chaser",  16.0f,  14.0f, 360.0f, 1, 1, 0xff4d5e, 3},
    [EN_SWARM]   = {"swarm",   10.0f,   5.0f, 430.0f, 3, 1, 0xff9a3c, 3},
    [EN_SHOOTER] = {"shooter", 20.0f,  32.0f, 140.0f, 2, 1, 0xffe04d, 4},
    [EN_BURSTER] = {"burster", 24.0f,  50.0f,  90.0f, 4, 3, 0xc05cff, 5},
    [EN_SPINNER] = {"spinner", 28.0f,  75.0f,  50.0f, 5, 4, 0xff4fd8, 6},
    [EN_SNIPER]  = {"sniper",  18.0f,  28.0f, 200.0f, 3, 5, 0x5dff9d, 4},
    [EN_TANK]    = {"tank",    42.0f, 240.0f,  70.0f, 8, 7, 0xff8a3d, 8},
    [EN_WARDEN]  = {"warden",  36.0f, 320.0f,  55.0f, 9, 9, 0x4dd2ff, 7},
    [EN_HUNTER]  = {"hunter",  18.0f,  90.0f, 260.0f, 6, 11, 0xff3d6e, 3},
    [EN_HIVE]    = {"hive",    34.0f, 260.0f,  40.0f, 10, 13, 0xffd84d, 6},
};

/*
 * Every enemy type always has the same attributes (hp, speed, bullet patterns), whatever
 * the depth. Difficulty comes from the rooms instead: more enemies, more waves, and new,
 * tougher types unlocking deeper (min_depth) - see spawn_wave() in room.c.
 */
#define HUNTER_DASH_SPEED 950.0f
#define HIVE_MAX_BROOD 9

void enemy_queue_spawn(EnemyType type, V2 pos, float delay) {
    if (G.nsp >= MAX_SPAWNS) return;
    Spawn *s = &G.sp[G.nsp++];
    s->type = type;
    s->pos = pos;
    s->t = s->max_t = delay;
}

static void enemy_create(EnemyType type, V2 pos) {
    if (G.nen >= MAX_ENEMIES) return;
    const EnemyDef *d = &ENEMIES[type];
    Enemy *e = &G.en[G.nen++];
    memset(e, 0, sizeof *e);
    e->type = type;
    e->uid = ++G.next_uid;
    e->pos = pos;
    e->target = pos;
    e->radius = d->radius;
    e->max_hp = e->hp = d->hp;
    e->fire_t = frand(0.6f, 1.6f);
    e->state_t = frand(1.0f, 2.0f);
    e->angle = frand(0.0f, TAU_F);
    e->spin_dir = chance(0.5f) ? 1 : -1;
    room_collide_circle(&e->pos, e->radius, NULL);
    sfx_play_at(SFX_ENEMY_APPEAR, e->pos);
    fx_particle(e->pos, v2(0, 0), 0.25f, e->radius * 3.0f, col_hex(d->color, 1), 0.0f, PT_RING);
}

Enemy *enemy_nearest(V2 pos, float max_dist) {
    Enemy *best = NULL;
    float best_d2 = max_dist * max_dist;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        float d2 = v2_dist2(e->pos, pos);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = e;
        }
    }
    return best;
}

void enemy_damage(Enemy *e, float dmg, V2 knock) {
    if (e->hp <= 0.0f) return;
    e->hp -= dmg;
    e->flash = 0.07f;
    G.last_enemy_damage_t = G.time;
    float mass = e->radius / 16.0f;
    if (e->type == EN_TANK) mass *= 2.5f;
    e->vel = v2_add(e->vel, v2_scale(knock, 1.0f / mass));
}

/* ---- Behaviour helpers --------------------------------------------------- */

static V2 random_point_around(V2 center, float dmin, float dmax) {
    for (int tries = 0; tries < 12; tries++) {
        V2 p = v2_add(center, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(dmin, dmax)));
        if (p.x < ARENA_X0 + 80 || p.x > ARENA_X1 - 80 || p.y < ARENA_Y0 + 80 || p.y > ARENA_Y1 - 80) continue;
        V2 q = p;
        if (room_collide_circle(&q, 40.0f, NULL)) continue;
        return p;
    }
    return v2(frand(ARENA_X0 + 200, ARENA_X1 - 200), frand(ARENA_Y0 + 150, ARENA_Y1 - 150));
}

static void move_towards(Enemy *e, V2 desired_vel, float responsiveness) {
    e->vel = v2_lerp(e->vel, desired_vel, smooth_k(responsiveness));
}

/* Direction towards goal that goes around obstacles instead of grinding into them. */
static V2 nav_dir(const Enemy *e, V2 goal) {
    return v2_norm(v2_sub(room_navigate(e->pos, goal, e->radius + 6.0f), e->pos));
}

static void wander(Enemy *e, float dmin, float dmax, float speed) {
    if (e->state_t <= 0.0f || v2_dist2(e->pos, e->target) < 30.0f * 30.0f) {
        e->target = random_point_around(G.pl.pos, dmin, dmax);
        e->state_t = frand(1.8f, 3.2f);
    }
    move_towards(e, v2_scale(nav_dir(e, e->target), speed), 3.0f);
}

static void fire_ring(V2 pos, int n, float speed, float radius, float offset, SDL_FColor col) {
    for (int i = 0; i < n; i++) ebullet_spawn(pos, offset + TAU_F * (float)i / (float)n, speed, radius, col);
}

static void fire_fan(V2 pos, float angle, int n, float spread, float speed, float radius, SDL_FColor col) {
    for (int i = 0; i < n; i++) {
        float t = n > 1 ? (float)i / (float)(n - 1) - 0.5f : 0.0f;
        ebullet_spawn(pos, angle + t * spread, speed, radius, col);
    }
}

static void update_enemy(Enemy *e) {
    Player *pl = &G.pl;
    const EnemyDef *d = &ENEMIES[e->type];
    V2 to = v2_sub(pl->pos, e->pos);
    float dist = v2_len(to);
    V2 dir = dist > 0.01f ? v2_scale(to, 1.0f / dist) : v2(1, 0);
    float aim = v2_heading(to);

    e->t += DT;
    e->flash -= DT;
    /* CRYO ROUNDS: chilled enemies move and shoot slower. */
    float tempo = e->slow_t > 0.0f ? 0.6f : 1.0f;
    e->slow_t -= DT;
    e->fire_t -= DT * tempo;
    e->state_t -= DT * tempo;
    /* VENOM TIPS / THERMITE: damage over time. */
    float dot = 0.0f;
    if (e->poison_t > 0.0f) { e->poison_t -= DT; dot += e->poison_dps; }
    if (e->burn_t > 0.0f) { e->burn_t -= DT; dot += e->burn_dps; }
    if (dot > 0.0f) {
        e->hp -= dot * DT;
        G.last_enemy_damage_t = G.time;
        if ((G.ticks + e->uid) % 9 == 0)
            fx_particle(v2_add(e->pos, v2(frand(-e->radius, e->radius), frand(-e->radius, e->radius))), v2(0, -60), 0.4f, 6.0f,
                        col_hex(e->burn_t > 0.0f ? 0xff7a2e : 0x7dff4a, 0.9f), 0.0f, PT_GLOW);
    }

    switch (e->type) {
    case EN_CHASER: {
        V2 chase = nav_dir(e, pl->pos);
        V2 side = v2_perp(chase);
        V2 want = v2_add(chase, v2_scale(side, sinf(e->t * 5.0f + (float)e->uid) * 0.35f));
        move_towards(e, v2_scale(want, d->speed), 3.5f);
        e->angle = v2_heading(e->vel);
    } break;

    case EN_SWARM: {
        V2 sep = v2(0, 0);
        for (int j = 0; j < G.nen; j++) {
            Enemy *o = &G.en[j];
            if (o == e || o->type != EN_SWARM) continue;
            V2 dd = v2_sub(e->pos, o->pos);
            float l2 = v2_len2(dd);
            if (l2 < 34.0f * 34.0f && l2 > 0.01f) sep = v2_add(sep, v2_scale(dd, 30.0f / l2));
        }
        V2 want = v2_norm(v2_add(nav_dir(e, pl->pos), sep));
        move_towards(e, v2_scale(want, d->speed), 5.0f);
        e->angle = v2_heading(e->vel);
    } break;

    case EN_SHOOTER: {
        wander(e, 260.0f, 560.0f, d->speed);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            if (e->burst <= 0) e->burst = 3;
            fire_fan(v2_add(e->pos, v2_scale(dir, e->radius)), aim, 3, 0.36f, 390.0f, 7.0f, col_hex(0xffa040, 1));
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->burst--;
            e->fire_t = e->burst > 0 ? 0.1f : frand(1.1f, 1.5f);
        }
    } break;

    case EN_BURSTER: {
        wander(e, 300.0f, 700.0f, d->speed);
        e->angle += 0.8f * DT;
        if (e->fire_t <= 0.0f) {
            const int n = 18;
            e->spin += PI_F / (float)n;
            fire_ring(e->pos, n, 250.0f, 8.0f, e->spin, col_hex(0xd070ff, 1));
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            if (e->state & 1) fire_ring(e->pos, n, 170.0f, 8.0f, e->spin + PI_F / (float)n, col_hex(0xff70e0, 1));
            e->state++;
            e->fire_t = 1.9f;
        }
    } break;

    case EN_SPINNER: {
        if (v2_dist2(e->pos, e->target) < 30.0f * 30.0f || e->t < DT * 2.0f)
            e->target = random_point_around(v2(ARENA_CX, ARENA_CY), 0.0f, 400.0f);
        move_towards(e, v2_scale(nav_dir(e, e->target), d->speed), 2.0f);
        if (e->state == 0) {
            e->spin += 1.9f * DT * (float)e->spin_dir;
            if (e->fire_t <= 0.0f) {
                const int arms = 3;
                for (int k = 0; k < arms; k++)
                    ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / (float)arms, 230.0f, 7.0f, col_hex(0xff4fd8, 1));
                e->fire_t = 0.075f;
                sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
            }
            if (e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 1.1f;
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = frand(2.8f, 3.6f);
            e->spin_dir = -e->spin_dir;
        }
        e->angle = e->spin;
    } break;

    case EN_SNIPER: {
        V2 mv;
        if (dist < 420.0f) mv = v2_scale(dir, -1.0f);
        else if (dist > 800.0f) mv = dir;
        else mv = v2_scale(v2_perp(dir), (float)e->spin_dir);
        if (e->state == 1) mv = v2(0, 0);
        if (chance(0.004f)) e->spin_dir = -e->spin_dir;
        move_towards(e, v2_scale(mv, d->speed), 3.0f);
        if (e->state == 0) {
            e->angle = aim;
            if (e->fire_t <= 0.0f) {
                e->state = 1;
                e->state_t = 0.7f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
        } else {
            if (e->state_t > 0.25f) e->angle = aim; /* tracks, then locks just before firing */
            if (e->state_t <= 0.0f) {
                V2 muzzle = v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius));
                for (int k = 0; k < 6; k++)
                    ebullet_spawn(muzzle, e->angle, (620.0f + (float)k * 90.0f), 6.0f, col_hex(0x7dff7a, 1));
                fx_particle(muzzle, v2(0, 0), 0.12f, 40.0f, col_hex(0x7dff7a, 0.9f), 0.0f, PT_GLOW);
                sfx_play_at(SFX_SNIPER_FIRE, e->pos);
                e->state = 0;
                e->fire_t = frand(1.8f, 2.6f);
            }
        }
    } break;

    case EN_TANK: {
        if (dist > 320.0f || !room_line_clear(e->pos, pl->pos, 8.0f)) move_towards(e, v2_scale(nav_dir(e, pl->pos), d->speed), 1.5f);
        else move_towards(e, v2(0, 0), 2.0f);
        e->angle = aim;
        e->spin += 0.6f * DT;
        if (e->fire_t <= 0.0f) {
            V2 muzzle = v2_add(e->pos, v2_scale(dir, e->radius));
            for (int k = 0; k < 14; k++)
                ebullet_spawn(muzzle, aim + frand(-0.5f, 0.5f), frand(220.0f, 470.0f), 9.0f, col_hex(0xff7a3d, 1));
            e->fire_t = frand(1.5f, 1.9f);
            fx_shake(0.05f);
            sfx_play_at(SFX_TANK_FIRE, e->pos);
        }
        if (e->state_t <= 0.0f) {
            fire_ring(e->pos, 28, 160.0f, 10.0f, frand(0.0f, TAU_F), col_hex(0xff4d3d, 1));
            sfx_play_ex(SFX_ENEMY_RING, 1.2f, 0.6f, 0.0f);
            e->state_t = 4.5f;
        }
    } break;

    case EN_WARDEN: {
        /* Slow fortress: a rotating cross of bullet streams with short pauses, plus aimed fans. */
        if (v2_dist2(e->pos, e->target) < 30.0f * 30.0f || e->t < DT * 2.0f)
            e->target = random_point_around(v2(ARENA_CX, ARENA_CY), 0.0f, 380.0f);
        move_towards(e, v2_scale(nav_dir(e, e->target), d->speed), 2.0f);
        e->spin += 0.85f * DT * (float)e->spin_dir;
        if (e->state == 0) {
            if (e->fire_t <= 0.0f) {
                for (int k = 0; k < 4; k++)
                    ebullet_spawn(e->pos, e->spin + PI_F * 0.5f * (float)k, 260.0f, 7.0f, col_hex(0x4dd2ff, 1));
                e->fire_t = 0.12f;
                sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
            }
            if (e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 1.3f;
                fire_fan(e->pos, aim, 7, 0.7f, 330.0f, 8.0f, col_hex(0x9de8ff, 1));
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = frand(2.6f, 3.4f);
            if (chance(0.4f)) e->spin_dir = -e->spin_dir;
        }
        e->angle = e->spin;
    } break;

    case EN_HUNTER: {
        /* Stalks at range, telegraphs (state 1), dashes through the player's position
         * (state 2), then bursts into a ring of bullets. */
        if (e->state == 0) {
            wander(e, 280.0f, 480.0f, d->speed);
            e->angle = aim;
            if (e->fire_t <= 0.0f) {
                e->state = 1;
                e->state_t = 0.45f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
        } else if (e->state == 1) {
            move_towards(e, v2(0, 0), 8.0f);
            if (e->state_t > 0.15f) e->angle = aim;
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 0.38f;
                e->vel = v2_scale(v2_angle(e->angle), HUNTER_DASH_SPEED);
                sfx_play_at(SFX_DASH, e->pos);
            }
        } else {
            e->vel = v2_scale(v2_angle(e->angle), HUNTER_DASH_SPEED);
            if ((G.ticks + e->uid) % 2 == 0)
                fx_particle(e->pos, v2(0, 0), 0.2f, 18.0f, col_hex(0xff3d6e, 0.5f), 0.0f, PT_GLOW);
            if (e->state_t <= 0.0f) {
                e->vel = v2_scale(e->vel, 0.15f);
                fire_ring(e->pos, 10, 230.0f, 7.0f, e->angle, col_hex(0xff6a8e, 1));
                sfx_play_at(SFX_ENEMY_RING, e->pos);
                e->state = 0;
                e->fire_t = frand(1.6f, 2.4f);
            }
        }
    } break;

    case EN_HIVE: {
        /* Breeds swarms (up to HIVE_MAX_BROOD over its life) and pulses slow rings. */
        wander(e, 350.0f, 650.0f, d->speed);
        e->angle += 0.5f * DT;
        if (e->state_t <= 0.0f) {
            e->state_t = 3.4f;
            if (e->burst < HIVE_MAX_BROOD && G.nen + G.nsp < MAX_ENEMIES - 8) {
                for (int k = 0; k < 3; k++)
                    enemy_queue_spawn(EN_SWARM, v2_add(e->pos, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 3.0f), e->radius + 22.0f)), 0.5f);
                e->burst += 3;
            }
        }
        if (e->fire_t <= 0.0f) {
            fire_ring(e->pos, 12, 150.0f, 9.0f, e->angle, col_hex(0xffd84d, 1));
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 2.6f;
        }
    } break;

    default:
        break;
    }

    e->pos = v2_add(e->pos, v2_scale(e->vel, DT * tempo));
    V2 n;
    if (room_collide_circle(&e->pos, e->radius, &n)) {
        float into = v2_dot(e->vel, n);
        if (into < 0.0f) e->vel = v2_sub(e->vel, v2_scale(n, into));
    }

    float cr = e->radius + pl->hit_radius + 4.0f;
    if (v2_dist2(e->pos, pl->pos) < cr * cr) player_hurt(1);
}

static void separate_enemies(void) {
    for (int i = 0; i < G.nen; i++) {
        Enemy *a = &G.en[i];
        for (int j = i + 1; j < G.nen; j++) {
            Enemy *b = &G.en[j];
            V2 d = v2_sub(b->pos, a->pos);
            float rr = a->radius + b->radius;
            float d2 = v2_len2(d);
            if (d2 >= rr * rr || d2 < 0.01f) continue;
            float dist = sqrtf(d2);
            V2 push = v2_scale(d, (rr - dist) * 0.5f / dist);
            a->pos = v2_sub(a->pos, push);
            b->pos = v2_add(b->pos, push);
        }
    }
    /* Separation can push enemies into walls or obstacles: resolve again. */
    for (int i = 0; i < G.nen; i++) room_collide_circle(&G.en[i].pos, G.en[i].radius, NULL);
}

static void on_death(Enemy *e) {
    SDL_FColor c = col_hex(ENEMIES[e->type].color, 1);
    fx_explosion(e->pos, e->radius, c);
    fx_shake(0.06f + e->radius * 0.004f);
    G.kills++;
    if (e->type == EN_TANK) G.hitstop = fmaxf(G.hitstop, 0.05f);
    if (e->type == EN_TANK) sfx_play_at(SFX_EXPLODE_BIG, e->pos);
    else if (e->type == EN_SWARM) sfx_play_ex(SFX_EXPLODE_SMALL, 0.7f, 1.5f, (e->pos.x - ARENA_CX) / (ARENA_X1 - ARENA_CX) * 0.7f);
    else sfx_play_at(SFX_EXPLODE_SMALL, e->pos);

    /* No drops: resources only ever come from room rewards. */
    upg_on_kill(e);
}

void enemies_update(void) {
    for (int i = 0; i < G.nsp; i++) {
        Spawn *s = &G.sp[i];
        s->t -= DT;
        if (s->t <= 0.0f) {
            enemy_create(s->type, s->pos);
            G.sp[i] = G.sp[--G.nsp];
            i--;
        }
    }
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].hp > 0.0f) update_enemy(&G.en[i]);
    separate_enemies();
    for (int i = 0; i < G.nen; i++) {
        if (G.en[i].hp > 0.0f) continue;
        on_death(&G.en[i]);
        G.en[i] = G.en[--G.nen];
        i--;
    }
}

/* ---- Drawing ------------------------------------------------------------- */

void enemies_draw(void) {
    /* Spawn telegraphs: shrinking ring + cross so nothing pops in unfairly. */
    for (int i = 0; i < G.nsp; i++) {
        const Spawn *s = &G.sp[i];
        float k = 1.0f - s->t / s->max_t;
        float r = ENEMIES[s->type].radius;
        SDL_FColor c = col_hex(ENEMIES[s->type].color, 0.25f + 0.6f * k);
        draw_ring(s->pos, r * (3.0f - 2.0f * k), 3.0f, c);
        float l = r * 0.8f;
        draw_line(v2(s->pos.x - l, s->pos.y - l), v2(s->pos.x + l, s->pos.y + l), 3.0f, c);
        draw_line(v2(s->pos.x - l, s->pos.y + l), v2(s->pos.x + l, s->pos.y - l), 3.0f, c);
    }

    draw_blend_add();
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        draw_glow(e->pos, e->radius * 2.3f, col_hex(ENEMIES[e->type].color, 0.22f));
        if (e->type == EN_SNIPER && e->state == 1) {
            float k = 1.0f - e->state_t / 0.7f;
            bool locked = e->state_t <= 0.25f;
            SDL_FColor c = locked ? col_hex(0xffffff, 0.8f) : col_hex(0x7dff7a, 0.25f + 0.4f * k);
            V2 dir = v2_angle(e->angle);
            draw_line(e->pos, v2_add(e->pos, v2_scale(dir, 2400.0f)), 1.0f + 3.0f * k, c);
        }
        if (e->type == EN_HUNTER && e->state == 1) {
            float k = 1.0f - e->state_t / 0.45f;
            V2 dir = v2_angle(e->angle);
            draw_line(e->pos, v2_add(e->pos, v2_scale(dir, HUNTER_DASH_SPEED * 0.38f)), 2.0f + 6.0f * k, col_hex(0xff3d6e, 0.2f + 0.4f * k));
        }
    }
    draw_blend_alpha();

    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        if (e->hp <= 0.0f) continue;
        const EnemyDef *d = &ENEMIES[e->type];
        SDL_FColor c = col_hex(d->color, 1);
        SDL_FColor fill = col_mul(c, 0.28f);
        fill.a = 1.0f;
        if (e->flash > 0.0f) {
            fill = COL_WHITE;
            c = COL_WHITE;
        }
        float ang = e->angle;
        if (e->type == EN_SHOOTER || e->type == EN_SNIPER) ang = e->t * 1.5f;
        if (e->type == EN_TANK) ang = e->spin;

        switch (e->type) {
        case EN_SHOOTER:
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 10.0f)), 8.0f, c);
            break;
        case EN_TANK:
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 16.0f)), 16.0f, c);
            break;
        default:
            break;
        }

        draw_ngon(e->pos, e->radius, d->sides, ang, fill);
        draw_ngon_outline(e->pos, e->radius, d->sides, ang, 3.0f, c);

        switch (e->type) {
        case EN_BURSTER: {
            float k = clampf(1.0f - e->fire_t / 1.9f, 0.0f, 1.0f);
            draw_circle(e->pos, e->radius * 0.6f * k, col_a(c, 0.85f));
        } break;
        case EN_SPINNER:
            draw_ngon_outline(e->pos, e->radius * 0.5f, 3, -e->spin * 2.0f, 3.0f, c);
            break;
        case EN_SNIPER:
            draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->angle), 5.0f)), 5.0f, c);
            break;
        case EN_TANK:
            draw_circle(e->pos, e->radius * 0.45f, c);
            draw_circle(e->pos, e->radius * 0.3f, fill);
            break;
        case EN_WARDEN:
            for (int k = 0; k < 4; k++)
                draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->spin + PI_F * 0.5f * (float)k), e->radius * 0.8f)), 4.0f, c);
            draw_circle(e->pos, e->radius * 0.25f, c);
            break;
        case EN_HUNTER:
            draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius * 0.3f)), e->radius * 0.28f, c);
            break;
        case EN_HIVE: {
            float k = clampf(1.0f - e->state_t / 3.4f, 0.0f, 1.0f);
            draw_ngon_outline(e->pos, e->radius * (0.35f + 0.25f * k), 6, -e->angle, 3.0f, c);
        } break;
        case EN_CHASER:
        case EN_SWARM:
            draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius * 0.25f)), e->radius * 0.25f, c);
            break;
        default:
            break;
        }

        if (e->hp < e->max_hp && e->radius >= 18.0f) {
            float w = e->radius * 2.0f, k = clampf(e->hp / e->max_hp, 0.0f, 1.0f);
            float x = e->pos.x - w * 0.5f, y = e->pos.y - e->radius - 12.0f;
            draw_rect(x, y, w, 4.0f, rgba(0, 0, 0, 0.6f));
            draw_rect(x, y, w * k, 4.0f, col_hex(d->color, 1));
        }
    }
}
