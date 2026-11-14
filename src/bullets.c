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
            if (b->homing > 0.0f && G.nen > 0 && !(b->flags & (PB_MINE | PB_VORTEX))) steer(b);
            upg_bullet_update(b);
            /* Weapon mods combine: each flag adds its own motion. */
            if (b->flags & PB_FLAME) { /* the flame slows down and swells */
                b->vel = v2_scale(b->vel, fmaxf(0.0f, 1.0f - 2.2f * DT));
                b->radius += 22.0f * DT;
            }
            if (b->flags & PB_ACCEL) { /* rockets, seekers */
                if (v2_len2(b->vel) < 1700.0f * 1700.0f) b->vel = v2_scale(b->vel, 1.0f + 3.0f * DT);
                if (b->style == BS_ROCKET && (G.ticks + i) % 2 == 0)
                    fx_particle(b->pos, v2_scale(b->vel, -0.15f), 0.25f, 9.0f, col_hex(0xff8a3d, 0.6f), 2.0f, PT_GLOW);
                else if (b->style != BS_ROCKET && (G.ticks + i) % 3 == 0)
                    fx_particle(b->pos, v2(0, 0), 0.22f, 6.0f, col_a(b->col, 0.5f), 0.0f, PT_GLOW);
            }
            if ((b->flags & PB_WAVE) && v2_len2(b->vel) > 1.0f) { /* weaves around its line of fire, centred on the aim */
                float sgn = (b->flags & PB_FLIP) ? -1.0f : 1.0f;
                float dy = 28.0f * (sinf(12.0f * b->age) - sinf(12.0f * (b->age - DT))) * sgn;
                b->pos = v2_add(b->pos, v2_scale(v2_perp(v2_norm(b->vel)), dy));
            }
            b->pos = v2_add(b->pos, v2_scale(b->vel, DT));
            /* PHASE ROUNDS fly through obstacles; only the arena walls stop them. */
            wall = upg_has(UPG_PHASE) ? arena_collide(&b->pos, b->radius, &n) : room_collide_circle(&b->pos, b->radius, &n);
            upg_reflect(b); /* may append bullets to G.pb: b stays valid (fixed array) */
        }

        if (!dead && !(b->flags & PB_VORTEX)) {
            for (int j = 0; j < G.nen; j++) {
                Enemy *e = &G.en[j];
                if (e->hp <= 0.0f || e->uid == b->last_hit) continue;
                float rr = e->radius + b->radius;
                if (v2_dist2(b->pos, e->pos) >= rr * rr) continue;
                V2 dir = b->flags & PB_MINE ? v2_norm(v2_sub(e->pos, b->pos)) : v2_norm(b->vel);
                float dmg = upg_hit_damage(b, e);
                enemy_damage(e, dmg, v2_scale(dir, b->knockback));
                upg_after_hit(b, e, dmg);
                weapon_on_hit(b, e, dmg);
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
            if ((b->flags & PB_RETURNS) && !(b->flags & PB_RETURNED)) {
                /* chakram: a wall sends it straight back */
                b->flags |= PB_RETURNED;
                b->last_hit = -1;
                reflect(&b->vel, n);
            } else if (b->bounces > 0) {
                b->bounces--;
                reflect(&b->vel, n);
                sfx_play_at(SFX_BOUNCE, b->pos);
                b->last_hit = -1;
                upg_on_bounce(b);
            } else {
                fx_burst(b->pos, 2, 60.0f, 220.0f, 0.15f, 2.0f, b->col);
                dead = true;
            }
        }

        if (dead) {
            weapon_bullet_death(b); /* may append bullets: b is copied over only below */
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
        if (active_time_stop_on()) { /* TIME STOP: frozen in place, still dangerous to touch */
            float rr = b->radius * 0.75f + pl->hit_radius;
            if (G.state == GS_PLAYING && pl->invuln <= 0.0f && pl->dash_t <= 0.0f && !active_shield_on() &&
                b->life > 0.0f && v2_dist2(b->pos, pl->pos) < rr * rr) {
                player_hurt(1);
                G.eb[i] = G.eb[--G.neb];
                i--;
            } else if (b->life <= 0.0f) { /* marked dead (sword, orb, shield...) */
                G.eb[i] = G.eb[--G.neb];
                i--;
            }
            continue;
        }
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

        if (!dead && G.state == GS_PLAYING && pl->invuln <= 0.0f && pl->dash_t <= 0.0f && !active_shield_on()) {
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
        if (b->flags & PB_VORTEX) {
            float k = fminf(1.0f, b->life / 0.4f);
            for (int r = 0; r < 3; r++)
                draw_arc(b->pos, (40.0f + 25.0f * (float)r) * k, 3.0f, G.time * (6.0f - (float)r) + (float)r * 2.0f,
                         G.time * (6.0f - (float)r) + (float)r * 2.0f + 3.6f, col_a(b->col, 0.55f - 0.12f * (float)r));
            draw_glow(b->pos, 70.0f * k, col_hex(0x6a2bff, 0.35f));
            draw_circle(b->pos, 12.0f * k, col_hex(0x05000f, 0.9f));
        } else if (b->style == BS_FLAME) {
            float t = clampf(b->age / (b->age + b->life), 0.0f, 1.0f); /* 0 fresh -> 1 dying */
            SDL_FColor c = col_lerp(col_hex(0xffe08a, 1), col_hex(0xff3a1a, 1), t);
            draw_glow(b->pos, b->radius * 2.2f, col_a(c, 0.35f * (1.0f - t)));
            draw_circle(b->pos, b->radius * 0.7f, col_a(c, 0.5f * (1.0f - t)));
        } else if (b->style == BS_ROCKET || (b->flags & PB_EXPLOSIVE)) {
            V2 side = v2_perp(dir);
            draw_glow(b->pos, 22.0f, col_a(b->col, 0.35f));
            draw_tri(v2_add(b->pos, v2_scale(dir, 11.0f)), v2_add(b->pos, v2_scale(side, 4.5f)), v2_sub(b->pos, v2_scale(side, 4.5f)), col_hex(0xffe9c0, 1));
            draw_line(b->pos, v2_sub(b->pos, v2_scale(dir, 12.0f)), 6.0f, col_a(b->col, 0.95f));
            draw_line(v2_sub(b->pos, v2_scale(dir, 12.0f)), v2_sub(b->pos, v2_scale(dir, 22.0f + 6.0f * sinf(G.time * 60.0f))), 5.0f, col_hex(0xff6a1a, 0.8f));
        } else if (b->style == BS_CHAKRAM) {
            float a = G.time * 22.0f + (float)i;
            draw_glow(b->pos, b->radius * 2.2f, col_a(b->col, 0.35f));
            draw_ngon(b->pos, b->radius, 4, a, col_a(b->col, 0.9f));
            draw_ngon(b->pos, b->radius, 4, a + PI_F * 0.25f, col_a(b->col, 0.6f));
            draw_circle(b->pos, b->radius * 0.3f, col_a(COL_WHITE, 0.9f));
        } else if (b->style == BS_RAIL) {
            V2 tail = v2_sub(b->pos, v2_scale(dir, 150.0f));
            draw_line(tail, b->pos, b->radius * 3.0f, col_a(b->col, 0.25f));
            draw_line(tail, b->pos, b->radius * 1.0f, col_a(COL_WHITE, 0.9f));
        } else if (b->style == BS_CLUSTER) {
            float pulse = 0.7f + 0.3f * sinf(G.time * 30.0f + (float)i);
            draw_glow(b->pos, b->radius * 3.0f, col_a(b->col, 0.4f));
            draw_ngon(b->pos, b->radius * 1.2f * pulse, 6, G.time * 8.0f, col_a(b->col, 0.9f));
            draw_circle(b->pos, b->radius * 0.4f, col_a(COL_WHITE, 0.85f));
        } else if (b->style == BS_WAVE) {
            draw_glow(b->pos, b->radius * 2.6f, col_a(b->col, 0.4f));
            draw_ring(b->pos, b->radius, 2.5f, col_a(b->col, 0.95f));
            draw_circle(b->pos, b->radius * 0.4f, col_a(COL_WHITE, 0.8f));
        } else if (b->style == BS_ARC) {
            V2 side = v2_perp(dir);
            V2 a0 = v2_sub(b->pos, v2_scale(dir, 22.0f)), a1 = v2_add(v2_sub(b->pos, v2_scale(dir, 11.0f)), v2_scale(side, 5.0f));
            draw_line(a0, a1, 3.0f, col_a(b->col, 0.8f));
            draw_line(a1, b->pos, 3.0f, col_a(COL_WHITE, 0.9f));
            draw_glow(b->pos, 16.0f, col_a(b->col, 0.45f));
        } else if (b->flags & PB_MINE) {
            float blink = 0.5f + 0.5f * sinf(G.time * 10.0f + (float)i);
            draw_glow(b->pos, 26.0f, col_a(b->col, 0.2f + 0.25f * blink));
            draw_ngon(b->pos, b->radius, 6, G.time, col_a(b->col, 0.9f));
            draw_circle(b->pos, 3.5f, col_a(COL_WHITE, blink));
        } else if (b->style == BS_BOUNCER) {
            draw_glow(b->pos, b->radius * 2.6f, col_a(b->col, 0.45f));
            draw_circle(b->pos, b->radius * 0.9f, col_a(b->col, 0.85f));
            draw_circle(b->pos, b->radius * 0.45f, col_a(COL_WHITE, 0.8f));
        } else if (b->style == BS_SEEKER) {
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
