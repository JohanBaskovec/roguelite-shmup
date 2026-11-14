#include "draw.h"
#include "game.h"
#include "audio.h"

#include <string.h>

PBullet *pbullet_spawn(V2 pos, V2 vel, float radius, float damage, SDL_FColor col) {
    if (G.npb >= MAX_PBULLETS) return NULL;
    PBullet *b = &G.pb[G.npb++];
    memset(b, 0, sizeof *b);
    b->pos = pos;
    b->vel = vel;
    b->radius = radius;
    b->damage = damage;
    b->col = col;
    b->life = 1.0f;
    b->last_hit = -1;
    b->weapon = -1;
    return b;
}

EBullet *ebullet_spawn(V2 pos, float angle, float speed, float radius, SDL_FColor col) {
    if (G.neb >= MAX_EBULLETS) return NULL;
    EBullet *b = &G.eb[G.neb++];
    b->pos = pos;
    b->vel = v2_scale(v2_angle(angle), speed * upg_enemy_bullet_speed());
    b->radius = radius;
    b->life = 12.0f;
    b->age = 0.0f;
    b->accel = 0.0f;
    b->curve = 0.0f;
    b->col = col;
    return b;
}

void ebullets_clear(bool with_fx) {
    if (with_fx) {
        for (int i = 0; i < G.neb; i++) {
            EBullet *b = &G.eb[i];
            fx_particle(b->pos, v2_scale(b->vel, 0.15f), 0.35f, b->radius * 2.5f, b->col, 2.0f, PT_GLOW);
        }
    }
    G.neb = 0;
}

static bool arena_collide(V2 *p, float r, V2 *normal) {
    V2 n = v2(0, 0);
    if (p->x < ARENA_X0 + r) { p->x = ARENA_X0 + r; n.x = 1; }
    if (p->x > ARENA_X1 - r) { p->x = ARENA_X1 - r; n.x = -1; }
    if (p->y < ARENA_Y0 + r) { p->y = ARENA_Y0 + r; n.y = 1; }
    if (p->y > ARENA_Y1 - r) { p->y = ARENA_Y1 - r; n.y = -1; }
    *normal = v2_norm(n);
    return n.x != 0.0f || n.y != 0.0f;
}

static void reflect(V2 *v, V2 n) {
    float d = v2_dot(*v, n);
    if (d < 0.0f) *v = v2_sub(*v, v2_scale(n, 2.0f * d));
}

static void steer(PBullet *b) {
    Enemy *t = enemy_nearest(b->pos, 650.0f);
    if (!t) return;
    float cur = v2_heading(b->vel);
    float want = v2_heading(v2_sub(t->pos, b->pos));
    float max_turn = b->homing * DT;
    float diff = clampf(angle_wrap(want - cur), -max_turn, max_turn);
    b->vel = v2_scale(v2_angle(cur + diff), v2_len(b->vel));
}

static void update_player_bullets(void) {
    for (int i = 0; i < G.npb; i++) {
        PBullet *b = &G.pb[i];
        bool dead = false, wall = false;
        V2 n = v2(0, 0);
        b->life -= DT;
        b->age += DT;
        if (b->life <= 0.0f) dead = true;

        if (!dead) {
            if (b->homing > 0.0f && G.nen > 0 && !(b->flags & PB_MINE)) steer(b);
            upg_bullet_update(b);
            if (b->weapon == WPN_SEEKER) {
                if (v2_len2(b->vel) < 1500.0f * 1500.0f) b->vel = v2_scale(b->vel, 1.0f + 2.4f * DT);
                if ((G.ticks + i) % 3 == 0)
                    fx_particle(b->pos, v2(0, 0), 0.22f, 6.0f, col_a(b->col, 0.5f), 0.0f, PT_GLOW);
            }
            b->pos = v2_add(b->pos, v2_scale(b->vel, DT));
            /* PHASE ROUNDS fly through obstacles; only the arena walls stop them. */
            wall = G.pl.upg_count[UPG_PHASE] ? arena_collide(&b->pos, b->radius, &n) : room_collide_circle(&b->pos, b->radius, &n);
        }

        if (!dead) {
            for (int j = 0; j < G.nen; j++) {
                Enemy *e = &G.en[j];
                if (e->hp <= 0.0f || e->uid == b->last_hit) continue;
                float rr = e->radius + b->radius;
                if (v2_dist2(b->pos, e->pos) >= rr * rr) continue;
                V2 dir = b->flags & PB_MINE ? v2_norm(v2_sub(e->pos, b->pos)) : v2_norm(b->vel);
                float dmg = upg_hit_damage(b, e);
                enemy_damage(e, dmg, v2_scale(dir, b->knockback));
                upg_after_hit(b, e, dmg);
                sfx_play_at(SFX_HIT, e->pos);
                fx_particle(b->pos, v2_scale(dir, -120.0f), 0.1f, b->radius * 3.0f, col_a(b->col, 0.8f), 0.0f, PT_GLOW);
                if ((G.ticks + i) & 1)
                    fx_particle(b->pos, v2_scale(v2_rotate(dir, PI_F + frand(-0.7f, 0.7f)), frand(150, 400)), 0.15f, 2.0f, b->col, 4.0f, PT_SPARK);
                b->last_hit = e->uid;
                if (b->pierce > 0) b->pierce--;
                else dead = true;
                break;
            }
        }

        /* Walls are resolved after enemy hits, so point-blank shots at an enemy
         * pressed against a wall still connect. */
        if (!dead && wall) {
            if (b->bounces > 0) {
                b->bounces--;
                reflect(&b->vel, n);
                sfx_play_at(SFX_BOUNCE, b->pos);
                b->last_hit = -1;
            } else {
                fx_burst(b->pos, 2, 60.0f, 220.0f, 0.15f, 2.0f, b->col);
                dead = true;
            }
        }

        if (dead) {
            G.pb[i] = G.pb[--G.npb];
            i--;
        }
    }
}

static void update_enemy_bullets(void) {
    Player *pl = &G.pl;
    for (int i = 0; i < G.neb; i++) {
        EBullet *b = &G.eb[i];
        bool dead = false;
        b->life -= DT;
        b->age += DT;
        if (b->life <= 0.0f) dead = true;

        if (!dead) {
            if (b->accel != 0.0f) {
                float spd = v2_len(b->vel);
                if (spd > 1e-3f) {
                    float ns = clampf(spd + b->accel * DT, 60.0f, 1400.0f);
                    b->vel = v2_scale(b->vel, ns / spd);
                }
            }
            if (b->curve != 0.0f) b->vel = v2_rotate(b->vel, b->curve * DT);
            b->pos = v2_add(b->pos, v2_scale(b->vel, DT));
            if (room_collide_circle(&b->pos, b->radius * 0.5f, NULL)) dead = true;
        }

        if (!dead && G.state == GS_PLAYING && pl->invuln <= 0.0f && pl->dash_t <= 0.0f) {
            float rr = b->radius * 0.75f + pl->hit_radius;
            if (v2_dist2(b->pos, pl->pos) < rr * rr) {
                player_hurt(1);
                dead = true;
            }
        }

        if (dead) {
            G.eb[i] = G.eb[--G.neb];
            i--;
        }
    }
}

void bullets_update(void) {
    update_player_bullets();
    update_enemy_bullets();
}

void bullets_draw_player(void) {
    draw_blend_add();
    for (int i = 0; i < G.npb; i++) {
        const PBullet *b = &G.pb[i];
        float spd = v2_len(b->vel);
        V2 dir = spd > 1e-3f ? v2_scale(b->vel, 1.0f / spd) : v2(1, 0);
        if (b->flags & PB_MINE) {
            float blink = 0.5f + 0.5f * sinf(G.time * 10.0f + (float)i);
            draw_glow(b->pos, 26.0f, col_a(b->col, 0.2f + 0.25f * blink));
            draw_ngon(b->pos, b->radius, 6, G.time, col_a(b->col, 0.9f));
            draw_circle(b->pos, 3.5f, col_a(COL_WHITE, blink));
        } else if (b->weapon == WPN_BOUNCER) {
            draw_glow(b->pos, b->radius * 2.6f, col_a(b->col, 0.45f));
            draw_circle(b->pos, b->radius * 0.9f, col_a(b->col, 0.85f));
            draw_circle(b->pos, b->radius * 0.45f, col_a(COL_WHITE, 0.8f));
        } else if (b->weapon == WPN_SEEKER) {
            draw_glow(b->pos, b->radius * 2.5f, col_a(b->col, 0.4f));
            draw_ngon(b->pos, b->radius * 1.2f, 3, v2_heading(b->vel), col_a(b->col, 0.9f));
        } else {
            float len = fminf(spd * 0.02f, 34.0f) + b->radius;
            V2 tail = v2_sub(b->pos, v2_scale(dir, len));
            draw_line(tail, b->pos, b->radius * 2.2f, col_a(b->col, 0.28f));
            draw_line(v2_sub(b->pos, v2_scale(dir, len * 0.7f)), b->pos, b->radius * 0.9f,
                      col_a(col_lerp(b->col, COL_WHITE, 0.5f), 0.85f));
        }
    }
    draw_blend_alpha();
}

void bullets_draw_enemy(void) {
    draw_blend_add();
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        float s = fminf(1.0f, b->age / 0.08f);
        draw_glow(b->pos, b->radius * 2.4f * s, col_a(b->col, 0.5f));
    }
    draw_blend_alpha();
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        float s = fminf(1.0f, b->age / 0.08f);
        draw_circle(b->pos, b->radius * s, b->col);
        draw_circle(b->pos, b->radius * 0.55f * s, COL_WHITE);
    }
}
