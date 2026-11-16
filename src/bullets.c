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
    b->radius = radius * (1.0f + malus_pct(MAL_BIG_BULLETS));
    b->life = 12.0f;
    b->age = 0.0f;
    b->grazed = false;
    b->accel = 0.0f;
    b->curve = 0.0f;
    b->col = col;
    b->kind = EB_NORMAL;
    b->n = 0;
    b->t1 = 0.0f;
    b->dir = v2(0, 0);
    return b;
}

EBullet *ebullet_lob(V2 from, V2 to, float flight, int n, SDL_FColor col) {
    to.x = clampf(to.x, ARENA_X0 + 30.0f, ARENA_X1 - 30.0f);
    to.y = clampf(to.y, ARENA_Y0 + 30.0f, ARENA_Y1 - 30.0f);
    EBullet *b = ebullet_spawn(from, 0.0f, 0.0f, 9.0f, col);
    if (!b) return NULL;
    b->kind = EB_GRENADE;
    b->vel = v2_scale(v2_sub(to, from), 1.0f / flight); /* ground speed: lands right on target */
    b->life = b->t1 = flight;
    b->n = n;
    return b;
}

/* A grenade landing / a mine going off: a ring of bullets and a flash. */
static void ebullet_burst(const EBullet *b) {
    V2 p = b->pos;
    SDL_FColor c = b->col;
    int n = b->n > 0 ? b->n : 8;
    float off = frand(0.0f, TAU_F);
    for (int k = 0; k < n; k++) ebullet_spawn(p, off + TAU_F * (float)k / (float)n, 210.0f, 7.0f, c);
    fx_particle(p, v2(0, 0), 0.3f, 90.0f, c, 0.0f, PT_RING);
    fx_burst(p, 10, 80.0f, 300.0f, 0.3f, 2.5f, c);
    sfx_play_at(SFX_EXPLODE_SMALL, p);
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
        int block = -1; /* breakable block hit (its wall stops the shot) */
        V2 n = v2(0, 0);
        b->life -= DT;
        b->age += DT;
        if (b->life <= 0.0f) {
            dead = true;
            if (b->life > -0.5f) upg_on_bullet_expire(b); /* out of range (not caught back): FIREWORKS, may append bullets */
        }

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
            if ((b->flags & PB_SPLIT) && b->age > 0.14f) { /* prism: two more shots fan out, once */
                b->flags &= ~PB_SPLIT;
                for (int k = -1; k <= 1 && G.npb < MAX_PBULLETS; k += 2) {
                    PBullet *c = &G.pb[G.npb++];
                    *c = *b;
                    c->vel = v2_rotate(b->vel, 0.3f * (float)k);
                    if (k > 0) c->flags ^= PB_FLIP;
                }
                fx_particle(b->pos, v2(0, 0), 0.12f, b->radius * 4.0f, col_a(b->col, 0.7f), 0.0f, PT_RING);
            }
            if (b->flags & PB_PULL) { /* graviton: enemies around are dragged along */
                for (int j = 0; j < G.nen; j++) {
                    Enemy *e = &G.en[j];
                    V2 d = v2_sub(b->pos, e->pos);
                    float dist = v2_len(d);
                    if (e->hp > 0.0f && !enemy_is_boss(e) && dist < 190.0f && dist > 1.0f)
                        e->vel = v2_add(e->vel, v2_scale(d, 1400.0f * DT / dist));
                }
            }
            const Player *pl = &G.pl;
            V2 rel = v2_sub(b->pos, pl->pos);
            float rd = v2_len(rel);
            if ((b->flags & PB_ORBIT) && !(b->flags & PB_RETURNED) && rd > 1.0f) {
                /* Orbit: around the ship (which it follows), drifting outwards. Slower close to the
                 * ship so the spiral stays readable. */
                V2 radial = v2_scale(rel, 1.0f / rd), tang = v2_perp(radial);
                if (b->flags & PB_FLIP) tang = v2_scale(tang, -1.0f);
                bool halo = syn(SYN_SAW_HALO); /* CHAKRAM + ORBIT: the saws stop at a ring and keep cutting */
                if (halo && fmodf(b->age, 0.35f) < DT) b->last_hit = -1;
                V2 dir = v2_norm(v2_add(tang, v2_scale(radial, halo && rd > 150.0f ? 0.0f : 0.2f)));
                float spd = v2_len(b->vel), step = fminf(spd, 220.0f + 3.2f * rd);
                b->vel = v2_scale(dir, spd);
                b->pos = v2_add(b->pos, v2_add(v2_scale(dir, step * DT), v2_scale(pl->vel, DT)));
            } else {
                b->pos = v2_add(b->pos, v2_scale(b->vel, DT));
            }
            /* PHASE ROUNDS fly through obstacles; only the arena walls stop them. */
            wall = upg_has(UPG_PHASE) ? arena_collide(&b->pos, b->radius, &n) : room_collide_obstacle(&b->pos, b->radius, &n, &block);
            if (upg_has(UPG_PHASE) && !wall) { /* through the walls, but a breakable block stops it (and takes the hit) */
                V2 q = b->pos;
                int k;
                if (room_collide_obstacle(&q, b->radius, &n, &k) && k >= 0 && obstacle_breakable(&G.room.obs[k])) {
                    wall = true;
                    block = k;
                    b->pos = q;
                }
            }
            if (block >= 0 && !obstacle_breakable(&G.room.obs[block])) block = -1;
            upg_reflect(b); /* may append bullets to G.pb: b stays valid (fixed array) */
        }

        /* The WYRM's armoured body, the CARRIER's shield plates: the shot stops there. */
        if (!dead && !(b->flags & PB_VORTEX) && boss_armor_hit(b->pos, b->radius)) {
            fx_burst(b->pos, 4, 80.0f, 300.0f, 0.15f, 2.0f, col_hex(0xd8f6ff, 1));
            if ((G.ticks + i) % 4 == 0) sfx_play_at(SFX_DEFLECT, b->pos);
            dead = true;
        }
        if (!dead && !(b->flags & PB_VORTEX)) {
            for (int j = 0; j < G.nen; j++) {
                Enemy *e = &G.en[j];
                if (e->hp <= 0.0f || e->uid == b->last_hit || enemy_untouchable(e)) continue;
                float rr = e->radius + b->radius;
                if (v2_dist2(b->pos, e->pos) >= rr * rr) continue;
                V2 dir = b->flags & PB_MINE ? v2_norm(v2_sub(e->pos, b->pos)) : v2_norm(b->vel);
                if (enemy_blocks_shot(e, v2_sub(b->pos, v2_scale(b->vel, DT)))) {
                    /* MIRROR: its front plate stops the shot cold (go around it) */
                    fx_burst(b->pos, 4, 80.0f, 300.0f, 0.15f, 2.0f, col_hex(0xd8f6ff, 1));
                    if ((G.ticks + i) % 4 == 0) sfx_play_at(SFX_DEFLECT, b->pos);
                    b->last_hit = e->uid;
                    if (b->pierce > 0) b->pierce--;
                    else dead = true;
                    break;
                }
                float dmg = upg_hit_damage(b, e);
                enemy_damage(e, dmg, v2_scale(dir, b->knockback));
                upg_after_hit(b, e, dmg);
                weapon_on_hit(b, e, dmg);
                sfx_play_at(SFX_HIT, e->pos);
                fx_particle(b->pos, v2_scale(dir, -120.0f), 0.1f, b->radius * 3.0f, col_a(b->col, 0.8f), 0.0f, PT_GLOW);
                if ((G.ticks + i) & 1)
                    fx_particle(b->pos, v2_scale(v2_rotate(dir, PI_F + frand(-0.7f, 0.7f)), frand(150, 400)), 0.15f, 2.0f, b->col, 4.0f, PT_SPARK);
                b->last_hit = e->uid;
                bool jumped = upg_ricochet(b, e); /* RICOCHET: off to another enemy instead of dying */
                if (b->pierce > 0) b->pierce--;
                else if (!jumped) dead = true;
                break;
            }
        }

        /* Walls are resolved after enemy hits, so point-blank shots at an enemy
         * pressed against a wall still connect. */
        if (!dead && wall) {
            if (block >= 0) { /* the ship's shots break blocks (never the enemies') */
                if ((b->flags & PB_FLAME) || upg_has(UPG_X_HELLFIRE)) room_block_ignite(block); /* fire sets crates, barrels and drums alight */
                room_block_damage(block, b->damage);
                fx_particle(b->pos, v2(0, 0), 0.1f, b->radius * 3.0f, col_a(b->col, 0.7f), 0.0f, PT_GLOW);
            }
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
                b->life > 0.0f && !ebullet_airborne(b) && v2_dist2(b->pos, pl->pos) < rr * rr) {
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
        if (b->life <= 0.0f) {
            dead = true;
            /* Fuse ran out (not marked dead by a sword / shield: those set life to -1). */
            if ((b->kind == EB_GRENADE || b->kind == EB_MINE) && b->life > -0.5f) ebullet_burst(b);
        }
        b = &G.eb[i]; /* the burst appended bullets: same slot, still this one */

        if (!dead) {
            float tempo = upg_enemy_bullet_tempo(b->pos);
            switch (b->kind) {
            case EB_MINE:
                b->vel = v2(0, 0);
                /* Armed after a moment: the ship coming close shortens the fuse to a blink. */
                if (b->age > 0.6f && b->life > 0.45f && v2_dist2(b->pos, pl->pos) < 95.0f * 95.0f) {
                    b->life = 0.45f;
                    sfx_play_at(SFX_BEEP, b->pos);
                }
                break;
            case EB_BOOMERANG:
                b->vel = v2_sub(b->vel, v2_scale(b->dir, b->t1 * DT * tempo));
                break;
            case EB_SPLIT:
                if (b->age >= b->t1) {
                    EBullet split = *b;
                    float off = v2_heading(b->vel);
                    for (int k = 0; k < split.n; k++)
                        ebullet_spawn(split.pos, off + TAU_F * ((float)k + 0.5f) / (float)split.n, 230.0f, split.radius * 0.6f, split.col);
                    b = &G.eb[i];
                    fx_particle(split.pos, v2(0, 0), 0.2f, split.radius * 5.0f, split.col, 0.0f, PT_RING);
                    dead = true;
                }
                break;
            case EB_HOMING:
                if (b->age < b->t1) {
                    float want = v2_heading(v2_sub(pl->pos, b->pos)), have = v2_heading(b->vel);
                    float diff = atan2f(sinf(want - have), cosf(want - have));
                    float turn = clampf(diff, -2.2f * DT, 2.2f * DT);
                    b->vel = v2_rotate(b->vel, turn * tempo);
                }
                break;
            case EB_BALL: /* a spark pod's ball hurts the enemies too */
                for (int k = 0; k < G.nen; k++) {
                    Enemy *e = &G.en[k];
                    float rr = e->radius + b->radius;
                    if (e->hp <= 0.0f || enemy_untouchable(e) || v2_dist2(e->pos, b->pos) >= rr * rr) continue;
                    enemy_damage(e, 20.0f, v2_scale(v2_norm(b->vel), 200.0f));
                    fx_particle(b->pos, v2(0, 0), 0.2f, 40.0f, col_a(b->col, 0.8f), 0.0f, PT_RING);
                    dead = true;
                    break;
                }
                break;
            case EB_WAVY:
                if (v2_len2(b->vel) > 1.0f) {
                    float dy = 34.0f * (sinf(7.0f * b->age) - sinf(7.0f * (b->age - DT)));
                    b->pos = v2_add(b->pos, v2_scale(v2_perp(v2_norm(b->vel)), dy * tempo));
                }
                break;
            default:
                break;
            }
        }

        if (!dead) {
            if (b->accel != 0.0f) {
                float spd = v2_len(b->vel);
                if (spd > 1e-3f) {
                    float ns = clampf(spd + b->accel * DT, 60.0f, 1400.0f);
                    b->vel = v2_scale(b->vel, ns / spd);
                }
            }
            if (b->curve != 0.0f) b->vel = v2_rotate(b->vel, b->curve * DT);
            b->pos = v2_add(b->pos, v2_scale(b->vel, DT * upg_enemy_bullet_tempo(b->pos)));
            V2 n;
            if (b->kind == EB_GRENADE) {
                /* flies over obstacles */
            } else if (room_collide_circle(&b->pos, b->radius * 0.5f, &n)) {
                if (b->kind == EB_BOUNCE && b->n > 0) {
                    b->n--;
                    reflect(&b->vel, n);
                } else {
                    dead = true;
                }
            }
        }

        if (!dead && !ebullet_airborne(b) && G.state == GS_PLAYING && pl->invuln <= 0.0f && pl->dash_t <= 0.0f && !active_shield_on()) {
            float rr = b->radius * 0.75f + pl->hit_radius, gr = rr + 30.0f;
            float d2 = v2_dist2(b->pos, pl->pos);
            if (d2 < rr * rr) {
                player_hurt(1);
                dead = true;
            } else if (!b->grazed && d2 < gr * gr && upg_graze_on()) { /* GRAZE: a near miss */
                b->grazed = true;
                upg_on_graze(b->pos);
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
        } else if (b->style == BS_LASER) { /* beam segments long enough to touch: one continuous ray */
            float len = fminf(fmaxf(spd * 0.036f, 20.0f), spd * b->age) + b->radius; /* never behind the muzzle */
            V2 tail = v2_sub(b->pos, v2_scale(dir, len));
            float w = b->radius * (1.0f + 0.25f * sinf(G.time * 50.0f + (float)i));
            draw_line(tail, b->pos, w * 4.0f, col_a(b->col, 0.22f));
            draw_line(tail, b->pos, w * 1.8f, col_a(b->col, 0.7f));
            draw_line(tail, b->pos, w * 0.7f, col_a(COL_WHITE, 0.95f));
        } else if (b->style == BS_GRAVITON) {
            draw_glow(b->pos, b->radius * 4.0f, col_hex(0x6a2bff, 0.35f));
            for (int r = 0; r < 2; r++)
                draw_arc(b->pos, b->radius * (1.5f + 0.7f * (float)r), 2.5f, -G.time * (7.0f - 2.0f * (float)r) + (float)i,
                         -G.time * (7.0f - 2.0f * (float)r) + (float)i + 3.4f, col_a(b->col, 0.7f - 0.25f * (float)r));
            draw_circle(b->pos, b->radius, col_hex(0x05000f, 0.95f));
            draw_ring(b->pos, b->radius, 2.0f, col_a(b->col, 0.9f));
        } else if (b->style == BS_CRESCENT) { /* the sword's blade wave */
            float a = v2_heading(b->vel), r = b->radius * 1.6f;
            V2 c = v2_sub(b->pos, v2_scale(dir, r * 0.6f));
            draw_arc(c, r, 7.0f, a - 1.0f, a + 1.0f, col_a(b->col, 0.35f));
            draw_arc(c, r, 3.0f, a - 0.9f, a + 0.9f, col_a(COL_WHITE, 0.9f));
            draw_glow(b->pos, r * 1.5f, col_a(b->col, 0.3f));
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

/* A grenade's height above its shadow (an arc over its flight). */
static float grenade_height(const EBullet *b) {
    float k = b->t1 > 0.0f ? clampf(b->age / b->t1, 0.0f, 1.0f) : 1.0f;
    return sinf(PI_F * k) * (60.0f + 70.0f * b->t1);
}

void bullets_draw_enemy(void) {
    /* Ground marks first: grenade shadows and where they will land, mine blast zones. */
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        if (b->kind == EB_GRENADE) {
            V2 land = v2_add(b->pos, v2_scale(b->vel, fmaxf(b->life, 0.0f)));
            float k = clampf(b->age / fmaxf(b->t1, 0.01f), 0.0f, 1.0f);
            draw_ring(land, 22.0f + 18.0f * (1.0f - k), 2.0f, col_a(b->col, 0.25f + 0.45f * k));
            draw_circle(b->pos, b->radius * 0.9f, rgba(0, 0, 0, 0.45f));
        } else if (b->kind == EB_MINE && b->life < 0.45f) {
            bool on = fmodf(b->life, 0.1f) < 0.05f;
            draw_ring(b->pos, 95.0f, 2.0f, col_a(b->col, on ? 0.6f : 0.2f));
        }
    }
    draw_blend_add();
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        float s = fminf(1.0f, b->age / 0.08f);
        V2 p = b->kind == EB_GRENADE ? v2(b->pos.x, b->pos.y - grenade_height(b)) : b->pos;
        draw_glow(p, b->radius * 2.4f * s, col_a(b->col, 0.5f));
    }
    draw_blend_alpha();
    for (int i = 0; i < G.neb; i++) {
        const EBullet *b = &G.eb[i];
        float s = fminf(1.0f, b->age / 0.08f);
        switch (b->kind) {
        case EB_GRENADE: {
            V2 p = v2(b->pos.x, b->pos.y - grenade_height(b));
            draw_circle(p, b->radius * 1.15f, b->col);
            draw_ngon_outline(p, b->radius * 1.15f, 6, b->age * 9.0f, 2.0f, COL_WHITE);
        } break;
        case EB_MINE: {
            bool armed = b->age > 0.6f;
            bool blink = b->life < 0.45f ? fmodf(b->life, 0.1f) < 0.05f : fmodf(b->age, 0.8f) < 0.12f;
            draw_ngon(b->pos, b->radius * 1.3f, 8, b->age * 1.5f, col_mul(b->col, 0.45f));
            draw_ngon_outline(b->pos, b->radius * 1.3f, 8, b->age * 1.5f, 2.0f, b->col);
            draw_circle(b->pos, b->radius * 0.45f, armed && blink ? COL_WHITE : b->col);
        } break;
        case EB_BOOMERANG: {
            float a = b->age * 14.0f;
            for (int k = 0; k < 3; k++)
                draw_line(b->pos, v2_add(b->pos, v2_scale(v2_angle(a + TAU_F * (float)k / 3.0f), b->radius * 1.5f)), 4.0f, b->col);
            draw_circle(b->pos, b->radius * 0.5f, COL_WHITE);
        } break;
        case EB_BALL: { /* a crackling violet orb with a white core */
            float wob = 1.0f + 0.15f * sinf(b->age * 40.0f + (float)i);
            draw_circle(b->pos, b->radius * wob * s, b->col);
            draw_ring(b->pos, b->radius * 1.45f * s, 1.5f, col_a(COL_WHITE, 0.6f));
            draw_circle(b->pos, b->radius * 0.45f * s, COL_WHITE);
        } break;
        case EB_SPLIT: {
            float k = clampf(b->age / fmaxf(b->t1, 0.01f), 0.0f, 1.0f);
            float wob = 1.0f + 0.12f * sinf(b->age * (10.0f + 30.0f * k));
            draw_circle(b->pos, b->radius * wob * s, b->col);
            draw_circle(b->pos, b->radius * (0.3f + 0.4f * k) * s, COL_WHITE);
        } break;
        default:
            draw_circle(b->pos, b->radius * s, b->col);
            draw_circle(b->pos, b->radius * 0.55f * s, COL_WHITE);
            if (b->kind == EB_HOMING && b->age < b->t1) draw_ring(b->pos, b->radius * 1.5f, 1.5f, col_a(b->col, 0.6f));
            if (b->kind == EB_BOUNCE && b->n > 0) draw_ring(b->pos, b->radius * 1.4f, 1.5f, col_a(COL_WHITE, 0.5f));
            break;
        }
    }
}
