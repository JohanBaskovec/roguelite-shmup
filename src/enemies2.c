#include "audio.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * The 60 newer enemy types (EN_DRIFTER .. EN_SENTINEL), from very easy to hard, and the three
 * helpers some of them make (EN_EGG, EN_MISSILE, EN_TURRET). Like every enemy they never scale:
 * the same numbers at any depth (ENEMIES table, enemies.c). Their state lives in the Enemy
 * (state, state_t, fire_t, aux, spin, burst, k0, k1, link...), shared state in Room (graves).
 *
 * Hooks into enemies.c: enemy2_update (the dispatch's default), enemy2_damage (front armour,
 * spin, guard, sponge...), enemy2_untouchable (the mole underground), enemy2_on_death (hydra
 * heads, popper's burst), the drawing (glow pass + body) and the hazards (beams, tethers, orbs,
 * strikes: enemy_hazards, read by the bot like the bosses' ones; the live ones hurt the ship).
 */

/* ---- Small helpers ----------------------------------------------------------- */

static float wrap_pi(float a) { return atan2f(sinf(a), cosf(a)); }

static float seg_dist(V2 p, V2 a, V2 b) {
    V2 ab = v2_sub(b, a);
    float l2 = v2_len2(ab);
    float t = l2 > 1e-6f ? clampf(v2_dot(v2_sub(p, a), ab) / l2, 0.0f, 1.0f) : 0.0f;
    return v2_dist(p, v2_add(a, v2_scale(ab, t)));
}

/* Where a beam fired from `from` stops: the first obstacle, the arena's edge or max_len. */
static V2 ray_end(V2 from, float ang, float max_len) {
    V2 dir = v2_angle(ang);
    float hi = max_len;
    if (dir.x > 1e-4f) hi = minf(hi, (ARENA_X1 - from.x) / dir.x);
    if (dir.x < -1e-4f) hi = minf(hi, (ARENA_X0 - from.x) / dir.x);
    if (dir.y > 1e-4f) hi = minf(hi, (ARENA_Y1 - from.y) / dir.y);
    if (dir.y < -1e-4f) hi = minf(hi, (ARENA_Y0 - from.y) / dir.y);
    hi = maxf(hi, 0.0f);
    if (room_line_clear(from, v2_add(from, v2_scale(dir, hi)), 0.0f)) return v2_add(from, v2_scale(dir, hi));
    float lo = 0.0f;
    for (int i = 0; i < 10; i++) {
        float mid = (lo + hi) * 0.5f;
        if (room_line_clear(from, v2_add(from, v2_scale(dir, mid)), 0.0f)) lo = mid;
        else hi = mid;
    }
    return v2_add(from, v2_scale(dir, lo));
}

static float spd(const Enemy *e) { return ENEMIES[e->type].speed; }
static bool first_tick(const Enemy *e) { return e->t <= DT * 1.5f; }
static V2 reflect_v(V2 d, V2 n) { return v2_sub(d, v2_scale(n, 2.0f * v2_dot(d, n))); }
static void turn_to(float *ang, float want, float rate) { *ang += clampf(wrap_pi(want - *ang), -rate * DT, rate * DT); }

static EBullet *shot(V2 p, float ang, float speed, float r, uint32_t hex) { return ebullet_spawn(p, ang, speed, r, col_hex(hex, 1)); }
static void ring(V2 p, int n, float speed, float r, float off, uint32_t hex) { enemy_fire_ring(p, n, speed, r, off, col_hex(hex, 1)); }
static void fan(V2 p, float ang, int n, float spread, float speed, float r, uint32_t hex) {
    enemy_fire_fan(p, ang, n, spread, speed, r, col_hex(hex, 1));
}

static void halt(Enemy *e, float k) { enemy_steer(e, v2(0, 0), k); }

static Enemy *by_uid(int uid) {
    if (uid <= 0) return NULL;
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].uid == uid && G.en[i].hp > 0.0f) return &G.en[i];
    return NULL;
}

/* Roams round the ship between dmin and dmax (its own timer in dodge_cd: state_t is free for the
 * types' own cycles, unlike enemy_wander). */
static void wander(Enemy *e, float dmin, float dmax, float speed) {
    e->dodge_cd -= DT;
    if (e->dodge_cd <= 0.0f || v2_dist2(e->pos, e->target) < 30.0f * 30.0f) {
        e->target = enemy_random_point(G.pl.pos, dmin, dmax);
        e->dodge_cd = frand(1.8f, 3.2f);
    }
    enemy_steer(e, v2_scale(enemy_nav_dir(e, e->target), speed), 3.0f);
}

/* Stays between lo and hi from the ship: closes in (round obstacles), backs off, strafes. */
static void keep_range(Enemy *e, V2 dir, float dist, float lo, float hi, float speed) {
    V2 mv;
    if (dist > hi || !room_line_clear(e->pos, G.pl.pos, 6.0f)) mv = enemy_nav_dir(e, G.pl.pos);
    else if (dist < lo) mv = v2_scale(dir, -1.0f);
    else mv = v2_scale(v2_perp(dir), 0.55f * (float)e->spin_dir);
    if (chance(0.004f)) e->spin_dir = -e->spin_dir;
    enemy_steer(e, v2_scale(mv, speed), 3.0f);
}

/* Bounces in straight lines off everything (aux = heading). */
static bool bounce_move(Enemy *e, float speed) {
    if (first_tick(e) || v2_len2(e->aux) < 0.5f) e->aux = v2_angle(frand(0.0f, TAU_F));
    V2 n;
    bool hit = enemy_blocked_ahead(e, e->aux, speed, &n);
    if (hit) e->aux = v2_norm(v2_rotate(reflect_v(e->aux, n), frand(-0.2f, 0.2f)));
    e->vel = v2_scale(e->aux, speed);
    return hit;
}

static bool on_screen_live(void) { return G.state == GS_PLAYING && !G.transitioning; }

/* Front armour (BEETLE, SPONGE, JUGGERNAUT): did the hit come from inside the front arc? */
static bool from_front(const Enemy *e, V2 knock, float arc) {
    V2 src = v2_len2(knock) > 1.0f ? v2_scale(v2_norm(knock), -1.0f) : v2_norm(v2_sub(G.pl.pos, e->pos));
    return v2_dot(src, v2_angle(e->angle)) > cosf(arc);
}

static int count_type(EnemyType t) {
    int n = 0;
    for (int i = 0; i < G.nen; i++) n += G.en[i].type == t && G.en[i].hp > 0.0f;
    for (int i = 0; i < G.nsp; i++) n += G.sp[i].type == t;
    return n;
}

#define BEETLE_ARC 1.0f
#define SPONGE_ARC 0.9f
#define SPONGE_FULL 8
#define JUGG_ARC 1.1f
#define JUGG_STOMP_R 170.0f
#define HALO_ORBS 6
#define HALO_R 74.0f
#define FLAIL_LEN 150.0f
#define FLAIL_BALL_R 16.0f
#define MOLE_R 70.0f
#define SPOT_R 90.0f
#define ZAP_LEN 340.0f
#define LINK_RANGE 900.0f
#define LINK_CYCLE 4.4f
#define STAR_PULL_R 520.0f
#define STAR_KEEP 110.0f
#define SENT_CONE 0.32f
#define SENT_LEN 650.0f
#define LODE_R 230.0f
#define MANTIS_REACH 300.0f
#define CRYSTAL_GROW_T 24.0f

/* LINKER: the tether's phase, from the pair's lower uid (warning, live, off). */
static int link_phase(const Enemy *a, const Enemy *b) {
    const Enemy *lo = a->uid < b->uid ? a : b;
    float p = fmodf(lo->t, LINK_CYCLE);
    return p < 0.7f ? 0 : p < 3.4f ? 1 : 2;
}

static Enemy *linker_partner(const Enemy *e) {
    Enemy *o = by_uid(e->link);
    if (!o || o->type != EN_LINKER || o->link != e->uid || v2_dist2(o->pos, e->pos) > LINK_RANGE * LINK_RANGE) return NULL;
    return o;
}

/* HALO: where orb k is now. */
static V2 halo_orb(const Enemy *e, int k) { return v2_add(e->pos, v2_scale(v2_angle(e->spin + TAU_F * (float)k / HALO_ORBS), HALO_R)); }
static V2 flail_ball(const Enemy *e) { return v2_add(e->pos, v2_scale(v2_angle(e->k0), FLAIL_LEN)); }

/* JELLY: its three tentacles trail behind it (sting on touch). */
static void jelly_tentacle(const Enemy *e, int k, V2 *a, V2 *b) {
    V2 back = v2_len2(e->vel) > 100.0f ? v2_scale(v2_norm(e->vel), -1.0f) : v2(0, 1);
    V2 side = v2_perp(back);
    *a = v2_add(e->pos, v2_scale(side, (float)(k - 1) * 10.0f));
    *b = v2_add(v2_add(*a, v2_scale(back, 62.0f)), v2_scale(side, (float)(k - 1) * 14.0f + sinf(e->t * 4.0f + (float)k) * 8.0f));
}

/* IDOL: anything else still standing shields it. */
static bool idol_shielded(const Enemy *e) {
    for (int i = 0; i < G.nen; i++) {
        const Enemy *o = &G.en[i];
        if (o != e && o->hp > 0.0f && o->type != EN_IDOL && !o->fake) return true;
    }
    return false;
}

/* SENTINEL / SENTRY: is the ship in the cone (half-angle arc, length len) and in sight? */
static bool in_cone(const Enemy *e, float ang, float arc, float len) {
    V2 to = v2_sub(G.pl.pos, e->pos);
    float d2 = v2_len2(to);
    if (d2 > len * len || d2 < 1.0f) return false;
    if (fabsf(wrap_pi(v2_heading(to) - ang)) > arc) return false;
    return room_line_clear(e->pos, G.pl.pos, 2.0f);
}

/* ---- Behaviours ---------------------------------------------------------------- */

static void centipede_body(Enemy *head) {
    int prev = head->uid;
    V2 back = v2_scale(v2_angle(head->angle), -1.0f);
    for (int k = 1; k <= 5; k++) {
        V2 p = v2_add(head->pos, v2_scale(back, 30.0f * (float)k));
        room_collide_circle(&p, 14.0f, NULL);
        Enemy *s = enemy_spawn_now(EN_CENTIPEDE, p);
        if (!s) break;
        s->state = 1;
        s->link = prev;
        s->radius = 14.0f;
        s->max_hp = s->hp = 18.0f;
        s->k0 = 1.0f;
        s->fire_t = frand(1.0f, 3.0f) + 0.3f * (float)k;
        prev = s->uid;
    }
}

static void hydra_set_gen(Enemy *e, int gen) {
    static const float R[3] = {30.0f, 22.0f, 15.0f}, HP[3] = {100.0f, 45.0f, 20.0f};
    gen = gen < 0 ? 0 : gen > 2 ? 2 : gen;
    e->k0 = (float)gen;
    e->radius = R[gen];
    e->max_hp = e->hp = HP[gen];
}

void enemy2_update(Enemy *e, float aim, float dist, V2 dir) {
    Player *pl = &G.pl;
    const float S = spd(e);
    switch (e->type) {
    /* ---- Very easy ---- */
    case EN_DRIFTER: /* drifts in straight lines, bouncing off walls; a slow shot now and then */
        bounce_move(e, S);
        e->angle += 0.6f * DT;
        if (e->fire_t <= 0.0f) {
            shot(e->pos, aim, 190.0f, 8.0f, 0xa8e6ff);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(2.4f, 3.0f);
        }
        break;
    case EN_PEON: /* walks up, one plain shot */
        keep_range(e, dir, dist, 240.0f, 420.0f, S);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            shot(v2_add(e->pos, v2_scale(dir, e->radius)), aim, 260.0f, 7.0f, 0xffc89a);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.8f, 2.4f);
        }
        break;
    case EN_BUZZER: { /* zigzags at the ship, no gun */
        if (e->state_t <= 0.0f) {
            e->k0 = frand(-1.0f, 1.0f);
            e->state_t = frand(0.18f, 0.32f);
        }
        V2 want = v2_rotate(enemy_nav_dir(e, pl->pos), e->k0);
        enemy_steer(e, v2_scale(want, S), 7.0f);
        e->angle = v2_heading(e->vel);
    } break;
    case EN_PULSAR: /* barely moves, a ring of six every few seconds */
        wander(e, 300.0f, 560.0f, S);
        e->angle += 0.4f * DT;
        if (e->fire_t <= 0.0f) {
            e->spin += PI_F / 6.0f;
            ring(e->pos, 6, 160.0f, 8.0f, e->spin, 0x8affd8);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 2.8f;
        }
        break;
    case EN_SNAIL: /* crawls after the ship leaving a trail of still slime */
        enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 2.0f);
        if (v2_len2(e->vel) > 25.0f) e->angle = v2_heading(e->vel);
        if ((e->k0 -= DT) <= 0.0f) {
            EBullet *b = shot(v2_sub(e->pos, v2_scale(v2_angle(e->angle), e->radius)), 0.0f, 0.0f, 9.0f, 0x9cff7a);
            if (b) b->life = 3.5f;
            e->k0 = 0.45f;
        }
        if (e->fire_t <= 0.0f) {
            shot(e->pos, aim, 200.0f, 8.0f, 0xc8ff9a);
            e->fire_t = frand(2.8f, 3.4f);
        }
        break;
    case EN_SKITTER: /* timid: keeps well away, panics and fires backwards when the ship comes near */
        if (dist < 300.0f) {
            enemy_steer(e, v2_scale(v2_norm(v2_add(v2_scale(dir, -1.0f), v2_scale(enemy_nav_dir(e, v2(ARENA_CX, ARENA_CY)), 0.3f))), S), 6.0f);
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 3, 0.4f, 300.0f, 6.0f, 0xfff0a0);
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
                e->fire_t = frand(1.2f, 1.6f);
            }
        } else {
            wander(e, 460.0f, 760.0f, S * 0.6f);
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 2, 0.2f, 280.0f, 6.0f, 0xfff0a0);
                e->fire_t = frand(2.2f, 2.8f);
            }
        }
        e->angle = v2_heading(e->vel);
        break;
    case EN_POPPER: /* harmless looking, pops into a ring when it dies (on_death) */
        wander(e, 200.0f, 420.0f, S);
        e->angle += 1.0f * DT;
        if (e->fire_t <= 0.0f) {
            shot(e->pos, aim, 150.0f, 9.0f, 0xff9ad0);
            e->fire_t = frand(3.0f, 3.6f);
        }
        break;

    /* ---- Easy ---- */
    case EN_SPRINKLER: /* sweeps a stream back and forth across the ship, then rests */
        wander(e, 320.0f, 560.0f, S);
        if (e->state == 0) {
            if (first_tick(e)) e->k0 = aim;
            e->k1 += 2.6f * DT;
            e->angle = e->k0 + sinf(e->k1) * 1.1f;
            if (e->fire_t <= 0.0f) {
                shot(e->pos, e->angle, 300.0f, 6.0f, 0x8ac8ff);
                e->fire_t = 0.09f;
                if ((G.ticks + e->uid) % 4 == 0) sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
            }
            if (e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 1.4f;
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = 2.4f;
            e->k0 = aim;
            e->k1 = 0.0f;
        }
        break;
    case EN_BEETLE: /* armoured front: turns to face the ship, shots on the shell barely hurt */
        turn_to(&e->angle, aim, 1.6f);
        if (dist > 170.0f) enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 2.5f);
        else halt(e, 3.0f);
        if (e->fire_t <= 0.0f) {
            fan(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle, 2, 0.3f, 280.0f, 7.0f, 0x9affb4);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(2.0f, 2.6f);
        }
        break;
    case EN_CRAB: { /* scuttles sideways only, pincer shots that curve in */
        if (e->state_t <= 0.0f) {
            e->k0 = frand(-260.0f, 260.0f);
            e->state_t = frand(2.0f, 3.2f);
        }
        float tx = pl->pos.x + e->k0, ty = pl->pos.y + (e->pos.y < pl->pos.y ? -320.0f : 320.0f);
        e->vel = v2(clampf((tx - e->pos.x) * 3.0f, -S, S), clampf((ty - e->pos.y) * 0.6f, -50.0f, 50.0f));
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            for (int k = -1; k <= 1; k += 2) {
                EBullet *b = shot(e->pos, aim + 0.55f * (float)k, 300.0f, 7.0f, 0xff9a7a);
                if (b) b->curve = -1.1f * (float)k;
            }
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.6f, 2.1f);
        }
    } break;
    case EN_WASP: /* hovers, then darts in and stings */
        if (e->state == 0) {
            wander(e, 240.0f, 380.0f, S);
            e->angle = aim;
            if (e->fire_t <= 0.0f) {
                e->state = 1;
                e->state_t = 0.28f;
            }
        } else if (e->state == 1) {
            halt(e, 9.0f);
            if (e->state_t > 0.08f) e->angle = aim;
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 0.22f;
                shot(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle, 760.0f, 5.0f, 0xfff07a);
                sfx_play_at(SFX_DASH, e->pos);
            }
        } else {
            e->vel = v2_scale(v2_angle(e->angle), 700.0f);
            if (e->state_t <= 0.0f) {
                e->vel = v2_scale(e->vel, 0.2f);
                e->state = 0;
                e->fire_t = frand(1.8f, 2.4f);
            }
        }
        break;
    case EN_DICE: /* rolls a die: as many bullets as the face times three */
        wander(e, 300.0f, 520.0f, S);
        e->angle += (e->fire_t < 0.5f ? 14.0f : 1.0f) * DT;
        if (first_tick(e)) e->k0 = (float)irand(1, 6);
        if (e->fire_t <= 0.0f) {
            int face = irand(1, 6);
            e->k0 = (float)face;
            float off = frand(0.0f, TAU_F);
            ring(e->pos, face * 3, 210.0f, 7.0f, off, 0xffffff);
            if (face == 6) ring(e->pos, 9, 150.0f, 9.0f, off + 0.2f, 0xff5a5a);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 2.2f;
        }
        break;
    case EN_PUFFER: { /* drifts up, swells and bursts into spikes, deflates */
        float base = ENEMIES[e->type].radius;
        if (e->state == 0) {
            enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 2.0f);
            e->k0 = maxf(0.0f, e->k0 - DT);
            if (dist < 230.0f || e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 0.7f;
                sfx_play_at(SFX_BEEP, e->pos);
            }
        } else if (e->state == 1) {
            halt(e, 6.0f);
            e->k0 = minf(1.0f, e->k0 + DT / 0.7f);
            if (e->state_t <= 0.0f) {
                float off = frand(0.0f, TAU_F);
                ring(e->pos, 18, 260.0f, 6.0f, off, 0xffd8a0);
                ring(e->pos, 9, 160.0f, 8.0f, off + 0.17f, 0xffa05a);
                sfx_play_at(SFX_ENEMY_RING, e->pos);
                e->state = 2;
                e->state_t = 1.5f;
            }
        } else {
            enemy_steer(e, v2_scale(dir, -S * 0.5f), 2.0f);
            e->k0 = maxf(0.0f, e->k0 - DT / 1.5f);
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 5.0f;
            }
        }
        e->radius = base * (1.0f + 0.7f * e->k0);
        e->angle += 0.5f * DT;
    } break;
    case EN_PRISM: /* three colours bent in to meet on the ship */
        keep_range(e, dir, dist, 300.0f, 520.0f, S);
        e->angle += 1.2f * DT;
        if (e->fire_t <= 0.0f) {
            static const uint32_t C3[3] = {0xff5a5a, 0x5aff7a, 0x5a8aff};
            float bend = clampf(330.0f / maxf(dist, 120.0f), 0.4f, 1.6f);
            for (int k = -1; k <= 1; k++) {
                EBullet *b = shot(e->pos, aim + 0.5f * (float)k, 320.0f, 7.0f, C3[k + 1]);
                if (b) b->curve = -bend * (float)k;
            }
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.5f, 1.9f);
        }
        break;
    case EN_BAT: { /* swoops round the ship in figure eights, sonar rings */
        float ph = e->t * 1.1f + (float)e->uid * 1.7f;
        V2 goal = v2_add(pl->pos, v2(cosf(ph) * 330.0f, sinf(2.0f * ph) * 210.0f));
        goal.x = clampf(goal.x, ARENA_X0 + 60.0f, ARENA_X1 - 60.0f);
        goal.y = clampf(goal.y, ARENA_Y0 + 60.0f, ARENA_Y1 - 60.0f);
        enemy_steer(e, v2_scale(enemy_nav_dir(e, goal), S), 2.5f);
        e->angle = v2_heading(e->vel);
        if (e->fire_t <= 0.0f) {
            ring(e->pos, 20, 140.0f, 5.0f, frand(0.0f, TAU_F), 0xd0a8ff);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = frand(2.4f, 2.9f);
        }
    } break;
    case EN_CENTIPEDE:
        if (e->state == 0) { /* the head: grows its body, weaves at the ship */
            if (e->k0 == 0.0f) {
                e->k0 = 1.0f;
                centipede_body(e);
            }
            V2 want = v2_rotate(enemy_nav_dir(e, pl->pos), sinf(e->t * 3.0f) * 0.6f);
            enemy_steer(e, v2_scale(want, S), 3.0f);
            e->angle = v2_heading(e->vel);
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 3, 0.4f, 260.0f, 7.0f, 0xe0ff7a);
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
                e->fire_t = frand(1.8f, 2.3f);
            }
        } else { /* a segment: follows the one ahead; when that one dies it becomes a head */
            Enemy *lead = by_uid(e->link);
            if (!lead || lead->type != EN_CENTIPEDE) {
                e->state = 0;
                e->link = 0;
                break;
            }
            V2 to = v2_sub(lead->pos, e->pos);
            float d = v2_len(to), gap = lead->radius + e->radius + 2.0f;
            e->vel = d > gap ? v2_scale(to, minf(S * 2.0f, (d - gap) / DT) / d) : v2(0, 0);
            if (d > 1.0f) e->angle = v2_heading(to);
            if (e->fire_t <= 0.0f) {
                for (int k = -1; k <= 1; k += 2) shot(e->pos, e->angle + PI_F * 0.5f * (float)k, 200.0f, 6.0f, 0xc8ff3d);
                e->fire_t = frand(2.6f, 3.4f);
            }
        }
        break;
    case EN_BROODER: /* lays eggs that hatch buzzers */
        wander(e, 320.0f, 540.0f, S);
        e->angle += 0.4f * DT;
        if (e->state_t <= 0.0f) {
            e->state_t = 3.2f;
            if (e->burst < 6 && G.nen + G.nsp < MAX_ENEMIES - 8) {
                V2 p = v2_sub(e->pos, v2_scale(v2_norm(e->vel.x != 0.0f || e->vel.y != 0.0f ? e->vel : v2(0, 1)), e->radius + 8.0f));
                room_collide_circle(&p, 13.0f, NULL);
                if (enemy_spawn_now(EN_EGG, p)) e->burst++;
                sfx_play_at(SFX_SPLAT, e->pos);
            }
            fan(e->pos, aim, 3, 0.5f, 220.0f, 8.0f, 0xffc8e8);
        }
        break;
    case EN_EGG: /* still, hatches a buzzer (not a kill: it is gone) */
        halt(e, 6.0f);
        e->k0 += DT;
        if (e->k0 >= 3.5f) {
            enemy_queue_spawn(EN_BUZZER, e->pos, 0.15f);
            fx_burst(e->pos, 10, 60.0f, 220.0f, 0.3f, 2.5f, col_hex(0xfff0d0, 1));
            sfx_play_at(SFX_SPLAT, e->pos);
            e->vanish = true;
            e->hp = 0.0f;
        }
        break;
    case EN_MEDIC: { /* stays behind the most hurt ally and heals it with a beam */
        Enemy *best = NULL;
        float worst = 0.999f;
        for (int i = 0; i < G.nen; i++) {
            Enemy *o = &G.en[i];
            if (o == e || o->hp <= 0.0f || o->type == EN_MEDIC || enemy_is_boss(o) || o->fake) continue;
            float k = o->hp / o->max_hp;
            if (k < worst && v2_dist2(o->pos, e->pos) < 520.0f * 520.0f) {
                worst = k;
                best = o;
            }
        }
        e->link = best ? best->uid : 0;
        if (best) {
            V2 away = v2_norm(v2_sub(best->pos, pl->pos));
            V2 goal = v2_add(best->pos, v2_scale(away, best->radius + 90.0f));
            enemy_steer(e, v2_scale(enemy_nav_dir(e, goal), S), 3.0f);
            if (v2_dist2(best->pos, e->pos) < 340.0f * 340.0f) best->hp = minf(best->max_hp, best->hp + 14.0f * DT);
        } else {
            wander(e, 380.0f, 600.0f, S);
        }
        e->angle = 0.0f;
        if (e->fire_t <= 0.0f) {
            shot(e->pos, aim, 210.0f, 8.0f, 0xb4ffc8);
            e->fire_t = best ? frand(2.6f, 3.2f) : frand(1.6f, 2.0f);
        }
    } break;
    case EN_HEDGEHOG: /* rings of quills; every hit it takes throws one back (enemy2_damage) */
        e->k0 -= DT;
        if (dist > 200.0f) enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 2.0f);
        else halt(e, 3.0f);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            ring(e->pos, 12, 320.0f, 5.0f, frand(0.0f, TAU_F), 0xffd08a);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 3.0f;
        }
        break;

    /* ---- Medium ---- */
    case EN_SAPPER:
        if (e->state == 0) { /* runs next to the ship and plants a bomb */
            if (first_tick(e)) e->target = enemy_random_point(pl->pos, 140.0f, 220.0f);
            enemy_steer(e, v2_scale(enemy_nav_dir(e, e->target), S), 5.0f);
            e->k0 += DT;
            if (v2_dist2(e->pos, e->target) < 40.0f * 40.0f || e->k0 > 3.0f) {
                EBullet *b = shot(e->pos, 0.0f, 0.0f, 13.0f, 0xffd04d);
                if (b) {
                    b->kind = EB_MINE;
                    b->life = 1.8f;
                    b->n = 14;
                }
                sfx_play_at(SFX_BEEP, e->pos);
                e->state = 1;
                e->state_t = 2.2f;
                e->k0 = 0.0f;
            }
        } else { /* then runs off */
            wander(e, 450.0f, 700.0f, S);
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->target = enemy_random_point(pl->pos, 140.0f, 220.0f);
            }
        }
        e->angle = v2_heading(e->vel);
        break;
    case EN_ROCKETEER: /* launches homing missiles (they can be shot down) */
        keep_range(e, dir, dist, 420.0f, 650.0f, S);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            if (count_type(EN_MISSILE) < 8)
                for (int k = -1; k <= 1; k += 2) {
                    V2 p = v2_add(e->pos, v2_scale(v2_perp(dir), 30.0f * (float)k));
                    room_collide_circle(&p, 10.0f, NULL);
                    enemy_queue_spawn(EN_MISSILE, p, 0.25f);
                }
            sfx_play_at(SFX_SHOT_ROCKET, e->pos);
            e->fire_t = frand(3.2f, 3.8f);
        }
        break;
    case EN_MISSILE: { /* homes in, blows into a ring next to the ship (not a kill) */
        if (first_tick(e)) e->angle = aim;
        turn_to(&e->angle, aim, 2.4f);
        e->vel = v2_scale(v2_angle(e->angle), S);
        e->k0 += DT;
        if ((G.ticks + e->uid) % 3 == 0) fx_particle(e->pos, v2_scale(v2_angle(e->angle), -90.0f), 0.3f, 6.0f, col_hex(0xffa05a, 0.7f), 0.0f, PT_GLOW);
        if (dist < 46.0f || e->k0 > 5.0f || enemy_blocked_ahead(e, v2_angle(e->angle), S, NULL)) {
            ring(e->pos, 8, 220.0f, 7.0f, frand(0.0f, TAU_F), 0xffd0a0);
            fx_explosion(e->pos, 30.0f, col_hex(0xffa05a, 1));
            sfx_play_at(SFX_EXPLODE_SMALL, e->pos);
            e->vanish = true;
            e->hp = 0.0f;
        }
    } break;
    case EN_JESTER: /* a different trick every time */
        wander(e, 280.0f, 500.0f, S);
        e->angle += 2.0f * DT;
        if (e->fire_t <= 0.0f) {
            int trick = irand(0, 4);
            e->k0 = (float)trick;
            switch (trick) {
            case 0:
                for (int k = -1; k <= 1; k++) {
                    EBullet *b = shot(e->pos, aim + 0.35f * (float)k, 230.0f, 8.0f, 0xff5ad8);
                    if (b) b->kind = EB_WAVY;
                }
                break;
            case 1:
                for (int k = -1; k <= 1; k += 2) {
                    EBullet *b = shot(e->pos, aim + 0.7f * (float)k, 330.0f, 7.0f, 0x5affff);
                    if (b) {
                        b->kind = EB_BOUNCE;
                        b->n = 2;
                    }
                }
                break;
            case 2:
                for (int k = -1; k <= 1; k += 2) {
                    EBullet *b = shot(e->pos, aim + 1.2f * (float)k, 200.0f, 7.0f, 0xffff5a);
                    if (b) {
                        b->kind = EB_HOMING;
                        b->t1 = 1.2f;
                        b->life = 5.0f;
                    }
                }
                break;
            case 3: {
                EBullet *b = shot(e->pos, aim, 220.0f, 12.0f, 0x7aff5a);
                if (b) {
                    b->kind = EB_SPLIT;
                    b->t1 = 0.7f;
                    b->n = 8;
                }
            } break;
            default:
                for (int k = -1; k <= 1; k++) {
                    EBullet *b = shot(e->pos, aim + 0.45f * (float)k, 480.0f, 8.0f, 0xff9a3d);
                    if (!b) continue;
                    b->kind = EB_BOOMERANG;
                    b->dir = v2_norm(b->vel);
                    b->t1 = 480.0f / 0.7f * upg_enemy_bullet_speed();
                    b->life = 2.3f;
                }
                break;
            }
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.3f, 1.7f);
        }
        break;
    case EN_FROSTBITE: /* frost shots: the ship hit is chilled (slower for a moment) */
        keep_range(e, dir, dist, 300.0f, 520.0f, S);
        e->angle += 0.8f * DT;
        if (e->fire_t <= 0.0f) {
            for (int k = -1; k <= 1; k++) {
                EBullet *b = shot(e->pos, aim + 0.22f * (float)k, 330.0f, 8.0f, 0xc8f8ff);
                if (b) b->kind = EB_FROST;
            }
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.4f, 1.8f);
        }
        break;
    case EN_GLITCH: /* jitters from spot to spot, glitched sprays */
        e->vel = v2(0, 0);
        if ((e->k0 -= DT) <= 0.0f) {
            e->k0 = 0.32f;
            V2 band = dist < 300.0f ? v2_scale(dir, -40.0f) : dist > 520.0f ? v2_scale(dir, 40.0f) : v2(0, 0);
            for (int tries = 0; tries < 6; tries++) {
                V2 p = v2_add(v2_add(e->pos, band), v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(30.0f, 80.0f)));
                V2 q = p;
                if (p.x < ARENA_X0 + 40.0f || p.x > ARENA_X1 - 40.0f || p.y < ARENA_Y0 + 40.0f || p.y > ARENA_Y1 - 40.0f) continue;
                if (room_collide_circle(&q, e->radius, NULL)) continue;
                fx_particle(e->pos, v2(0, 0), 0.15f, e->radius * 2.0f, col_hex(0x5affff, 0.5f), 0.0f, PT_GLOW);
                e->pos = p;
                break;
            }
        }
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 6; k++) shot(e->pos, aim + frand(-0.5f, 0.5f), frand(250.0f, 400.0f), 6.0f, k & 1 ? 0xff5aff : 0x5affff);
            sfx_play_at(SFX_TELEPORT, e->pos);
            e->fire_t = frand(1.6f, 2.0f);
        }
        break;
    case EN_PEEKER: { /* only moves while the ship is not aiming at it */
        float off = fabsf(wrap_pi(pl->aim_angle - v2_heading(v2_sub(e->pos, pl->pos))));
        e->state = off < 0.45f; /* watched: freezes and hides its face */
        if (e->state) {
            halt(e, 12.0f);
            if (e->fire_t <= 0.0f) { /* peeks between its fingers now and then */
                shot(e->pos, aim, 220.0f, 7.0f, 0xffffff);
                e->fire_t = 2.2f;
            }
        } else {
            enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 4.0f);
            if (e->fire_t <= 0.0f) {
                shot(e->pos, aim, 300.0f, 7.0f, 0xffffff);
                e->fire_t = 0.8f;
            }
        }
        e->angle = aim;
    } break;
    case EN_ECHO: { /* mirrors the ship's moves: comes in as it comes in, backs off as it backs off */
        /* Only while it sees the ship: out of sight it comes round the walls (never hides for good). */
        bool sees = room_line_clear(e->pos, pl->pos, 6.0f);
        V2 mirror = sees ? v2_scale(pl->vel, -0.8f) : v2(0, 0);
        V2 band = !sees || dist > 450.0f ? enemy_nav_dir(e, pl->pos) : dist < 220.0f ? v2_scale(dir, -1.0f) : v2(0, 0);
        if (!sees) band = v2_scale(band, S / 120.0f);
        V2 want = v2_add(mirror, v2_scale(band, 120.0f));
        float l = v2_len(want);
        if (l > S) want = v2_scale(want, S / l);
        enemy_steer(e, want, 6.0f);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            fan(e->pos, aim, 3, 0.3f, 320.0f, 7.0f, 0xa8c0ff);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.1f, 1.4f);
        }
    } break;
    case EN_CLOCK: /* hands that fire on every tick, a chime every twelve */
        wander(e, 280.0f, 520.0f, S);
        e->k0 += 0.35f * DT;
        e->k1 += 2.4f * DT;
        e->angle = 0.0f;
        if (e->fire_t <= 0.0f) {
            shot(e->pos, e->k1, 260.0f, 6.0f, 0xfff0c0);
            shot(e->pos, e->k0, 200.0f, 9.0f, 0xffc85a);
            if (++e->burst % 12 == 0) {
                ring(e->pos, 12, 180.0f, 7.0f, e->k0, 0xffe6a0);
                sfx_play_at(SFX_ENEMY_RING, e->pos);
            } else {
                sfx_play_at(SFX_BEEP, e->pos);
            }
            e->fire_t = 0.5f;
        }
        break;
    case EN_HALO:
        wander(e, 200.0f, 380.0f, S);
        e->spin += 2.4f * DT;
        if (e->state == 0) { /* six orbs circle it (they sting), then fly off */
            e->k0 = HALO_ORBS;
            if (e->state_t <= 0.0f) {
                for (int k = 0; k < HALO_ORBS; k++) {
                    V2 p = halo_orb(e, k);
                    float a = e->spin + TAU_F * (float)k / HALO_ORBS;
                    shot(p, a + 0.6f, 280.0f, 10.0f, 0xfff07a);
                }
                sfx_play_at(SFX_ENEMY_RING, e->pos);
                e->state = 1;
                e->state_t = 2.2f;
                e->k0 = 0.0f;
            }
        } else { /* growing them back */
            e->k0 = minf((float)HALO_ORBS, e->k0 + (float)HALO_ORBS / 2.2f * DT);
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 4.0f;
            }
        }
        e->angle = e->spin * 0.3f;
        break;
    case EN_STALKER:
        if (e->state == 0) { /* all but invisible, closes in */
            enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 3.0f);
            if (dist < 260.0f || e->state_t <= -6.0f) {
                e->state = 1;
                e->state_t = 0.35f;
                sfx_play_at(SFX_TELEPORT, e->pos);
            }
        } else if (e->state == 1) { /* shows up... */
            halt(e, 8.0f);
            if (e->state_t <= 0.0f) {
                fan(e->pos, aim, 5, 0.9f, 360.0f, 7.0f, 0xa8a8ff);
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
                e->state = 2;
                e->state_t = 1.4f;
            }
        } else { /* ...and slips away before fading again */
            wander(e, 450.0f, 650.0f, S);
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 0.0f;
            }
        }
        e->angle = aim;
        break;
    case EN_OCTOPUS: /* swims in pulses, each one a ring of weaving ink */
        if (e->state_t <= 0.0f) {
            e->target = enemy_random_point(pl->pos, 320.0f, 480.0f);
            e->vel = v2_scale(enemy_nav_dir(e, e->target), 330.0f);
            e->state_t = 1.4f;
            for (int k = 0; k < 8; k++) {
                EBullet *b = shot(e->pos, e->angle + TAU_F * (float)k / 8.0f, 190.0f, 8.0f, 0xff9ae0);
                if (b) b->kind = EB_WAVY;
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
        } else {
            halt(e, 1.6f);
        }
        e->angle += 0.5f * DT;
        break;
    case EN_BUZZSAW: /* a spinning blade: shots glance off it until it stops to cool down */
        if (e->state == 0) {
            if (bounce_move(e, S)) {
                ring(e->pos, 4, 220.0f, 6.0f, frand(0.0f, TAU_F), 0xffffff);
                sfx_play_at(SFX_BOUNCE, e->pos);
            }
            e->spin += 26.0f * DT;
            if (e->state_t <= 0.0f && e->t > 1.0f) {
                e->state = 1;
                e->state_t = 1.6f;
            }
        } else {
            halt(e, 4.0f);
            e->spin += 3.0f * DT;
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 4.5f;
                e->aux = v2_norm(v2_add(dir, v2_scale(v2_perp(dir), frand(-0.5f, 0.5f))));
            }
        }
        e->angle = e->spin;
        break;
    case EN_SENTRY: /* a turret: turns slowly, streams when the ship is in its sights */
        halt(e, 10.0f);
        turn_to(&e->angle, aim, 1.1f);
        e->aux = ray_end(e->pos, e->angle, 1000.0f);
        if (e->fire_t <= 0.0f && in_cone(e, e->angle, 0.25f, 1000.0f)) {
            shot(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle, 480.0f, 6.0f, 0xff7a7a);
            if ((G.ticks + e->uid) % 3 == 0) sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = 0.11f;
        }
        break;
    case EN_TORCH:
        if (e->state == 0) { /* closes in */
            keep_range(e, dir, dist, 170.0f, 260.0f, S);
            turn_to(&e->angle, aim, 3.0f);
            if (e->state_t <= 0.0f && dist < 330.0f) {
                e->state = 1;
                e->state_t = 2.6f;
                sfx_play_at(SFX_SHOT_FLAMER, e->pos);
            }
        } else { /* a cone of short flames */
            halt(e, 3.0f);
            turn_to(&e->angle, aim, 1.3f);
            if (e->fire_t <= 0.0f) {
                EBullet *b = shot(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle + frand(-0.3f, 0.3f),
                                  frand(300.0f, 420.0f), 9.0f, chance(0.5f) ? 0xff8a2e : 0xffd23d);
                if (b) b->life = 0.6f;
                e->fire_t = 0.035f;
            }
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 2.0f;
            }
        }
        break;
    case EN_ZAPPER:
        if (e->state == 0) {
            keep_range(e, dir, dist, 160.0f, 260.0f, S);
            if (e->fire_t <= 0.0f && dist < ZAP_LEN) { /* locks a point, charges an arc to it */
                e->state = 1;
                e->state_t = 0.7f;
                e->aux = v2_add(e->pos, v2_scale(dir, minf(dist + 40.0f, ZAP_LEN)));
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
        } else if (e->state == 1) {
            halt(e, 10.0f);
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 0.2f;
                ring(e->aux, 5, 200.0f, 6.0f, frand(0.0f, TAU_F), 0xaaffff);
                sfx_play_at(SFX_CRACKLE, e->pos);
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->fire_t = frand(1.8f, 2.4f);
        }
        e->angle = aim;
        break;
    case EN_BEAMER:
        if (e->state == 0) { /* rests */
            wander(e, 380.0f, 600.0f, S);
            if (e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 0.8f;
                e->k0 = aim - 1.0f * (float)e->spin_dir; /* starts off to one side and sweeps over the ship */
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
        } else if (e->state == 1) { /* warning line */
            halt(e, 8.0f);
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 2.8f;
                sfx_play_at(SFX_SHOT_RAIL, e->pos);
            }
        } else { /* the beam sweeps */
            halt(e, 8.0f);
            e->k0 += 0.75f * DT * (float)e->spin_dir;
            if (e->fire_t <= 0.0f) {
                ring(e->aux, 3, 150.0f, 6.0f, frand(0.0f, TAU_F), 0xff7ac0);
                e->fire_t = 0.3f;
            }
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 2.4f;
                e->spin_dir = -e->spin_dir;
            }
        }
        e->angle = e->k0;
        if (e->state > 0) e->aux = ray_end(e->pos, e->k0, 2000.0f);
        break;
    case EN_ENGINEER: /* builds turrets (three at most), keeps away */
        keep_range(e, dir, dist, 380.0f, 600.0f, S);
        e->angle = aim;
        if (e->state_t <= 0.0f) {
            e->state_t = 4.5f;
            if (e->burst < 3 && G.nen + G.nsp < MAX_ENEMIES - 8) {
                V2 p = enemy_random_point(e->pos, 60.0f, 120.0f);
                enemy_queue_spawn(EN_TURRET, p, 1.2f);
                e->burst++;
                sfx_play_at(SFX_THUD, e->pos);
            }
        }
        if (e->fire_t <= 0.0f) {
            shot(e->pos, aim, 260.0f, 7.0f, 0xffd27a);
            e->fire_t = frand(1.8f, 2.2f);
        }
        break;
    case EN_TURRET:
        halt(e, 10.0f);
        turn_to(&e->angle, aim, 3.0f);
        if (e->fire_t <= 0.0f) {
            if (e->burst <= 0) e->burst = 2;
            shot(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle, 380.0f, 6.0f, 0xffe0a0);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = --e->burst > 0 ? 0.15f : 1.4f;
        }
        break;
    case EN_JELLY: /* drifts in pulses, stinging tentacles trail behind */
        if (e->state_t <= 0.0f) {
            e->vel = v2_add(e->vel, v2_scale(enemy_nav_dir(e, enemy_random_point(pl->pos, 120.0f, 300.0f)), 220.0f));
            e->state_t = 2.0f;
            for (int k = 0; k < 12; k++) {
                EBullet *b = shot(e->pos, TAU_F * (float)k / 12.0f + e->angle, 200.0f, 7.0f, 0xe0c8ff);
                if (b) b->accel = -90.0f;
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
        } else {
            halt(e, 1.2f);
        }
        e->angle += 0.3f * DT;
        break;
    case EN_SCORPION:
        keep_range(e, dir, dist, 200.0f, 340.0f, S);
        e->angle = aim;
        e->k0 -= DT;
        if (e->fire_t <= 0.0f) { /* the tail: a lobbed sting */
            ebullet_lob(e->pos, pl->pos, 0.7f, 6, col_hex(0xffc85a, 1));
            sfx_play_at(SFX_LOB, e->pos);
            e->fire_t = frand(2.2f, 2.6f);
        }
        if (dist < 170.0f && e->k0 <= 0.0f) { /* pincers */
            fan(e->pos, aim, 2, 0.5f, 360.0f, 7.0f, 0xffaa3d);
            e->k0 = 0.9f;
        }
        break;

    /* ---- Hard ---- */
    case EN_LINKER: { /* pairs up: a deadly tether between the two, swept across the ship */
        Enemy *mate = linker_partner(e);
        if (!mate) {
            e->link = 0;
            for (int i = 0; i < G.nen; i++) {
                Enemy *o = &G.en[i];
                if (o == e || o->type != EN_LINKER || o->hp <= 0.0f || (o->link && o->link != e->uid && by_uid(o->link))) continue;
                if (v2_dist2(o->pos, e->pos) > LINK_RANGE * LINK_RANGE) continue;
                e->link = o->uid;
                o->link = e->uid;
                mate = o;
                break;
            }
        }
        if (mate) {
            /* both circle the ship on opposite sides, on the lower uid's clock */
            const Enemy *lead = e->uid < mate->uid ? e : mate;
            e->k1 += 0.45f * DT;
            float ang = (float)lead->uid * 1.7f + lead->k1 + (lead == e ? 0.0f : PI_F);
            V2 goal = v2_add(pl->pos, v2_scale(v2_angle(ang), 300.0f));
            goal.x = clampf(goal.x, ARENA_X0 + 60.0f, ARENA_X1 - 60.0f);
            goal.y = clampf(goal.y, ARENA_Y0 + 60.0f, ARENA_Y1 - 60.0f);
            enemy_steer(e, v2_scale(enemy_nav_dir(e, goal), S), 2.0f);
            if (e->fire_t <= 0.0f) {
                shot(e->pos, aim, 240.0f, 7.0f, 0x8affd8);
                e->fire_t = frand(2.0f, 2.4f);
            }
        } else {
            wander(e, 300.0f, 500.0f, S);
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 3, 0.4f, 300.0f, 7.0f, 0x8affd8);
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
                e->fire_t = frand(1.4f, 1.7f);
            }
        }
        e->angle += 1.5f * DT;
    } break;
    case EN_SWAPPER:
        if (e->state == 0) {
            wander(e, 300.0f, 500.0f, S);
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 2, 0.25f, 300.0f, 7.0f, 0xff8aff);
                e->fire_t = frand(1.4f, 1.8f);
            }
            if (e->state_t <= 0.0f && dist > 160.0f) { /* marks the ship... */
                e->state = 1;
                e->state_t = 1.0f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
        } else {
            halt(e, 8.0f);
            if (e->state_t <= 0.0f) { /* ...and trades places with it */
                if (on_screen_live()) {
                    V2 a = e->pos, b = pl->pos;
                    fx_particle(a, v2(0, 0), 0.3f, 60.0f, col_hex(0xff3dff, 0.8f), 0.0f, PT_RING);
                    fx_particle(b, v2(0, 0), 0.3f, 60.0f, col_hex(0xff3dff, 0.8f), 0.0f, PT_RING);
                    pl->pos = a;
                    pl->vel = v2(0, 0);
                    room_collide_circle(&pl->pos, PLAYER_BODY_R, NULL);
                    e->pos = b;
                    room_collide_circle(&e->pos, e->radius, NULL);
                    ring(e->pos, 10, 200.0f, 7.0f, frand(0.0f, TAU_F), 0xff8aff);
                    sfx_play_at(SFX_TELEPORT, e->pos);
                }
                e->state = 0;
                e->state_t = frand(5.0f, 7.0f);
            }
        }
        e->angle += 2.0f * DT;
        break;
    case EN_SPOTTER:
        switch (e->state) {
        case 0:
            keep_range(e, dir, dist, 500.0f, 800.0f, S);
            e->angle = aim;
            if (e->fire_t <= 0.0f) { /* paints the ship */
                e->state = 1;
                e->state_t = 1.3f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
            break;
        case 1:
            halt(e, 6.0f);
            e->angle = aim;
            if (e->state_t > 0.35f) e->aux = pl->pos;
            if (e->state_t <= 0.0f) { /* the strike is coming down there */
                e->state = 2;
                e->state_t = 0.7f;
                sfx_play_at(SFX_LOB, e->aux);
            }
            break;
        case 2:
            halt(e, 6.0f);
            if (e->state_t <= 0.0f) {
                e->state = 3;
                e->state_t = 0.12f;
                ring(e->aux, 12, 200.0f, 7.0f, frand(0.0f, TAU_F), 0xff7a5a);
                fx_explosion(e->aux, SPOT_R, col_hex(0xff5a3d, 1));
                fx_shake(0.15f);
                sfx_play_at(SFX_EXPLODE_BIG, e->aux);
            }
            break;
        default:
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->fire_t = frand(2.5f, 3.2f);
            }
            break;
        }
        break;
    case EN_LODESTONE: /* its field pushes the ship's shots aside (get close or shoot fast) */
        wander(e, 300.0f, 500.0f, S);
        e->angle += 0.7f * DT;
        for (int i = 0; i < G.npb; i++) {
            PBullet *b = &G.pb[i];
            if (b->flags & (PB_MINE | PB_VORTEX | PB_ORBIT)) continue;
            V2 off = v2_sub(b->pos, e->pos);
            float d2 = v2_len2(off);
            if (d2 > LODE_R * LODE_R || d2 < 1.0f) continue;
            float d = sqrtf(d2), sp = v2_len(b->vel);
            if (sp < 50.0f) continue;
            V2 nv = v2_add(b->vel, v2_scale(off, 2400.0f * DT * (1.0f - d / LODE_R) / d));
            b->vel = v2_scale(v2_norm(nv), sp);
        }
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 10; k++) {
                EBullet *b = shot(e->pos, e->angle + TAU_F * (float)k / 10.0f, 200.0f, 7.0f, k & 1 ? 0xff5a5a : 0x5a8aff);
                if (b) b->curve = 0.7f;
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 2.6f;
        }
        break;
    case EN_SPONGE: /* soaks up shots on its front, squeezes them back out */
        turn_to(&e->angle, aim, 1.8f);
        if (dist > 300.0f) enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 2.0f);
        else halt(e, 3.0f);
        if (e->k0 >= SPONGE_FULL) {
            fan(e->pos, e->angle, SPONGE_FULL, 0.8f, 380.0f, 8.0f, 0xfff07a);
            sfx_play_at(SFX_SPLAT, e->pos);
            e->k0 = 0.0f;
        }
        if (e->fire_t <= 0.0f) {
            fan(e->pos, e->angle, 2, 0.2f, 260.0f, 7.0f, 0xffe85a);
            e->fire_t = frand(2.6f, 3.2f);
        }
        break;
    case EN_PARRY: /* guard up: hits do nothing and are answered at once */
        keep_range(e, dir, dist, 260.0f, 420.0f, S);
        e->angle = aim;
        e->k0 -= DT;
        if (e->state == 0) {
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 3, 0.35f, 320.0f, 7.0f, 0xe8e8ff);
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
                e->fire_t = 1.8f;
            }
            if (e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 1.3f;
                sfx_play_at(SFX_SHIELD, e->pos);
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = 1.1f;
        }
        break;
    case EN_BUBBLER: { /* wraps an ally in a bubble that stops whole hits */
        wander(e, 340.0f, 560.0f, S);
        e->angle += 0.5f * DT;
        if (e->state_t <= 0.0f) {
            e->state_t = 3.5f;
            Enemy *best = NULL;
            float bd = 450.0f * 450.0f;
            for (int i = 0; i < G.nen; i++) {
                Enemy *o = &G.en[i];
                if (o == e || o->hp <= 0.0f || o->bubble > 0 || enemy_is_boss(o) || o->fake || o->type == EN_BUBBLER) continue;
                float d2 = v2_dist2(o->pos, e->pos);
                if (d2 < bd) {
                    bd = d2;
                    best = o;
                }
            }
            if (!best && e->bubble <= 0) best = e; /* alone: itself */
            if (best) {
                best->bubble = 3;
                fx_particle(best->pos, v2(0, 0), 0.35f, best->radius * 3.0f, col_hex(0x7ae8ff, 0.8f), 0.0f, PT_RING);
                sfx_play_at(SFX_SHIELD, best->pos);
            }
        }
        if (e->fire_t <= 0.0f) {
            shot(e->pos, aim, 140.0f, 12.0f, 0xa8f0ff);
            e->fire_t = frand(2.2f, 2.6f);
        }
    } break;
    case EN_DRUMMER: /* every beat hurries every enemy round it */
        wander(e, 350.0f, 550.0f, S);
        if (e->fire_t <= 0.0f) {
            for (int i = 0; i < G.nen; i++) {
                Enemy *o = &G.en[i];
                if (o->hp > 0.0f && !enemy_is_boss(o) && v2_dist2(o->pos, e->pos) < 420.0f * 420.0f) o->haste_t = 2.4f;
            }
            ring(e->pos, 8, 200.0f, 8.0f, frand(0.0f, TAU_F), 0xffb45a);
            fx_particle(e->pos, v2(0, 0), 0.4f, 420.0f, col_hex(0xff9a3d, 0.5f), 0.0f, PT_RING);
            sfx_play_at(SFX_THUD, e->pos);
            e->fire_t = 3.2f;
        }
        e->angle = 0.0f;
        break;
    case EN_TRICKSTER:
        if (e->fake) { /* an illusion: walks about, pops after a while or when its maker is gone */
            wander(e, 260.0f, 480.0f, S);
            e->k0 += DT;
            if (e->k0 > 6.0f || !by_uid(e->link)) {
                fx_particle(e->pos, v2(0, 0), 0.25f, e->radius * 2.5f, col_hex(0xb43dff, 0.7f), 0.0f, PT_RING);
                e->vanish = true;
                e->hp = 0.0f;
            }
            e->angle = aim;
            break;
        }
        wander(e, 260.0f, 480.0f, S);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            fan(e->pos, aim, 3, 0.4f, 320.0f, 7.0f, 0xd08aff);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.4f, 1.8f);
        }
        if (e->state_t <= 0.0f) { /* two look-alikes, and it swaps spots with nobody knows who */
            e->state_t = 5.5f;
            int fakes = 0;
            for (int i = 0; i < G.nen; i++) fakes += G.en[i].fake && G.en[i].link == e->uid && G.en[i].hp > 0.0f;
            for (int k = 0; k < 2 && fakes < 4; k++, fakes++) {
                V2 p = enemy_random_point(e->pos, 120.0f, 220.0f);
                Enemy *f = enemy_spawn_now(EN_TRICKSTER, p);
                if (!f) break;
                f->fake = true;
                f->link = e->uid;
                f->max_hp = f->hp = 1.0f;
                f->state_t = 99.0f;
            }
            fx_particle(e->pos, v2(0, 0), 0.25f, e->radius * 2.5f, col_hex(0xb43dff, 0.7f), 0.0f, PT_RING);
            e->pos = enemy_random_point(e->pos, 100.0f, 200.0f);
            room_collide_circle(&e->pos, e->radius, NULL);
            sfx_play_at(SFX_TELEPORT, e->pos);
        }
        break;
    case EN_HYDRA: { /* each head lost grows two smaller ones (twice) */
        if (first_tick(e) && e->k0 == 0.0f) hydra_set_gen(e, 0);
        int gen = (int)e->k0;
        wander(e, 260.0f, 480.0f, S * (1.0f + 0.35f * (float)gen));
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            fan(e->pos, aim, 3 - gen, 0.45f, 280.0f + 40.0f * (float)gen, 7.0f, 0x7aff9a);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = frand(1.4f, 1.8f);
        }
    } break;
    case EN_MANTIS:
        switch (e->state) {
        case 0: /* lurks, half hidden, creeping up */
            enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S * 0.5f), 2.0f);
            e->angle = aim;
            if ((dist < 280.0f && room_line_clear(e->pos, pl->pos, e->radius)) || e->state_t <= -6.0f) {
                e->state = 1;
                e->state_t = 0.4f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
            break;
        case 1: /* rears up: the strike line shows */
            halt(e, 10.0f);
            if (e->state_t > 0.1f) e->angle = aim;
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 0.22f;
                sfx_play_at(SFX_DASH, e->pos);
            }
            break;
        case 2: /* strikes */
            e->vel = v2_scale(v2_angle(e->angle), MANTIS_REACH / 0.22f);
            if (e->state_t <= 0.0f) {
                e->vel = v2_scale(e->vel, 0.1f);
                fan(e->pos, e->angle, 5, 1.2f, 300.0f, 7.0f, 0xb4ff7a);
                sfx_play_at(SFX_SWORD, e->pos);
                e->state = 3;
                e->state_t = 1.0f;
            }
            break;
        default: /* exposed for a moment */
            halt(e, 4.0f);
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 0.0f;
            }
            break;
        }
        break;
    case EN_FLAIL: { /* swings a spiked ball on a chain, faster when close; it lets go of spikes */
        if (dist > 160.0f) enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 2.0f);
        else halt(e, 3.0f);
        float rate = (dist < 280.0f ? 3.6f : 2.6f) * (float)e->spin_dir;
        e->k0 += rate * DT;
        e->k1 += fabsf(rate) * DT;
        if (e->k1 >= TAU_F) {
            e->k1 -= TAU_F;
            shot(flail_ball(e), e->k0 + PI_F * 0.5f * (float)e->spin_dir, 300.0f, 8.0f, 0xe0e0e0);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
        }
        e->angle = aim;
    } break;
    case EN_MOLE:
        switch (e->state) {
        case 0: /* underground (untouchable): tunnels under the ship */
            enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 4.0f);
            if ((G.ticks + e->uid) % 5 == 0)
                fx_particle(e->pos, v2(frand(-40.0f, 40.0f), frand(-40.0f, 40.0f)), 0.4f, 5.0f, col_hex(0xc08a5a, 0.7f), 2.0f, PT_SPARK);
            if (dist < 50.0f || e->state_t <= -4.0f) {
                e->state = 1;
                e->state_t = 0.6f;
                sfx_play_at(SFX_THUD, e->pos);
            }
            break;
        case 1: /* the ground bulges */
            halt(e, 12.0f);
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 0.15f;
                ring(e->pos, 12, 230.0f, 7.0f, frand(0.0f, TAU_F), 0xe0b080);
                fx_burst(e->pos, 18, 100.0f, 400.0f, 0.4f, 3.0f, col_hex(0xc08a5a, 1));
                fx_shake(0.12f);
                sfx_play_at(SFX_EXPLODE_SMALL, e->pos);
            }
            break;
        case 2: /* bursts out */
            halt(e, 12.0f);
            if (e->state_t <= 0.0f) {
                e->state = 3;
                e->state_t = 2.2f;
                e->fire_t = 0.8f;
            }
            break;
        default: /* above ground: the time to hit it */
            halt(e, 4.0f);
            e->angle = aim;
            if (e->fire_t <= 0.0f) {
                fan(e->pos, aim, 3, 0.4f, 300.0f, 7.0f, 0xe0b080);
                e->fire_t = 5.0f;
            }
            if (e->state_t <= 0.0f) {
                e->state = 0;
                e->state_t = 0.0f;
                sfx_play_at(SFX_THUD, e->pos);
            }
            break;
        }
        break;
    case EN_CRYSTAL: { /* grows: bigger, tougher, more shards. Break it early */
        halt(e, 10.0f);
        e->k0 = minf(1.0f, e->k0 + DT / CRYSTAL_GROW_T);
        float full = enemy_base_hp(e); /* grows tougher: the damage taken stays */
        e->hp += full - e->max_hp;
        e->max_hp = full;
        e->radius = ENEMIES[e->type].radius * (1.0f + e->k0);
        e->angle += (0.3f + e->k0) * DT;
        if (e->fire_t <= 0.0f) {
            ring(e->pos, 4 + (int)(8.0f * e->k0), 220.0f, 7.0f, e->angle, 0xc8b0ff);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 2.2f - 0.8f * e->k0;
        }
    } break;
    case EN_NECRO: { /* raises what died in the room (four at most) */
        keep_range(e, dir, dist, 450.0f, 700.0f, S);
        e->angle += 0.6f * DT;
        Room *r = &G.room;
        if (e->fire_t <= 0.0f) {
            if (r->ngraves > 0 && e->burst < 4 && G.nen + G.nsp < MAX_ENEMIES - 8) {
                r->ngraves--;
                V2 p = r->graves[r->ngraves];
                enemy_queue_spawn((EnemyType)r->grave_type[r->ngraves], p, 1.2f);
                fx_particle(p, v2(0, 0), 0.6f, 80.0f, col_hex(0x6aff6a, 0.8f), 0.0f, PT_RING);
                e->aux = p;
                e->k1 = 1.0f;
                e->burst++;
                sfx_play_at(SFX_SECRET, p);
            } else {
                for (int k = -1; k <= 1; k += 2) {
                    EBullet *b = shot(e->pos, aim + 0.9f * (float)k, 190.0f, 9.0f, 0x9aff9a);
                    if (b) {
                        b->kind = EB_HOMING;
                        b->t1 = 1.6f;
                        b->life = 5.0f;
                    }
                }
                sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            }
            e->fire_t = frand(4.0f, 5.0f);
        }
        e->k1 = maxf(0.0f, e->k1 - DT);
    } break;
    case EN_BERSERKER: { /* the more it is hurt, the faster it runs and fires */
        float rage = 1.0f - clampf(e->hp / e->max_hp, 0.0f, 1.0f);
        enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S + 280.0f * rage), 3.0f);
        e->angle = v2_heading(e->vel);
        if (e->fire_t <= 0.0f) {
            fan(e->pos, aim, 2 + (int)(3.0f * rage), 0.5f, 300.0f + 120.0f * rage, 7.0f, 0xff5a5a);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = 1.6f - 1.0f * rage;
        }
    } break;
    case EN_FUNGUS: /* puffs out drifting spores that follow the ship */
        halt(e, 10.0f);
        e->k0 -= DT;
        e->angle += 0.2f * DT;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 6; k++) {
                EBullet *b = shot(e->pos, e->angle + TAU_F * (float)k / 6.0f, 110.0f, 8.0f, 0xd0ff8a);
                if (b) {
                    b->kind = EB_HOMING;
                    b->t1 = 2.5f;
                    b->life = 6.0f;
                }
            }
            sfx_play_at(SFX_SPLAT, e->pos);
            e->fire_t = 3.0f;
        }
        break;
    case EN_WEAVER: /* spins webs that hang in the air and chill the ship */
        keep_range(e, dir, dist, 300.0f, 480.0f, S);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            for (int k = -1; k <= 1; k++) {
                EBullet *b = shot(e->pos, aim + 0.35f * (float)k, 380.0f, 13.0f, 0xf0f0f0);
                if (b) {
                    b->kind = EB_WEB;
                    b->t1 = 300.0f;
                    b->life = 5.5f;
                }
            }
            sfx_play_at(SFX_SPLAT, e->pos);
            e->fire_t = frand(1.7f, 2.0f);
        }
        break;

    /* ---- Very hard ---- */
    case EN_JUGGERNAUT:
        turn_to(&e->angle, aim, 0.9f);
        if (e->state == 0) {
            if (dist > 200.0f) enemy_steer(e, v2_scale(enemy_nav_dir(e, pl->pos), S), 1.5f);
            else halt(e, 2.0f);
            if (e->fire_t <= 0.0f) { /* a cannon shell that shatters on the walls */
                EBullet *b = shot(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle, 300.0f, 14.0f, 0xc8c8ff);
                if (b) {
                    b->kind = EB_SHATTER;
                    b->n = 7;
                }
                sfx_play_at(SFX_TANK_FIRE, e->pos);
                e->fire_t = frand(2.0f, 2.4f);
            }
            if (e->state_t <= 0.0f && dist < 420.0f) {
                e->state = 1;
                e->state_t = 0.7f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
        } else if (e->state == 1) { /* lifts a foot: the stomp ring shows */
            halt(e, 8.0f);
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 0.12f;
                ring(e->pos, 20, 240.0f, 8.0f, frand(0.0f, TAU_F), 0xb0b0ff);
                fx_particle(e->pos, v2(0, 0), 0.4f, JUGG_STOMP_R * 2.0f, col_hex(0xb0b0ff, 0.8f), 0.0f, PT_RING);
                fx_shake(0.3f);
                sfx_play_at(SFX_EXPLODE_BIG, e->pos);
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = 5.0f;
        }
        break;
    case EN_IDOL: /* still and all but unbreakable while anything else stands; spirals */
        halt(e, 10.0f);
        e->spin += 1.3f * DT * (float)e->spin_dir;
        e->angle = PI_F * 0.25f;
        if (e->state == 0) {
            if (e->fire_t <= 0.0f) {
                for (int k = 0; k < 4; k++) shot(e->pos, e->spin + PI_F * 0.5f * (float)k, 230.0f, 7.0f, 0xffe65a);
                e->fire_t = 0.13f;
                if ((G.ticks + e->uid) % 3 == 0) sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
            }
            if (e->state_t <= 0.0f) {
                e->state = 1;
                e->state_t = 1.2f;
            }
        } else if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = frand(2.6f, 3.2f);
            e->spin_dir = -e->spin_dir;
        }
        break;
    case EN_BLACKSTAR: { /* a small black hole: pulls the ship in, bends its shots */
        wander(e, 380.0f, 600.0f, S);
        e->angle += 1.4f * DT;
        if (on_screen_live() && pl->dash_t <= 0.0f && dist < STAR_PULL_R && dist > 1.0f) {
            float pull = 150.0f * (1.0f - dist / STAR_PULL_R);
            pl->pos = v2_sub(pl->pos, v2_scale(dir, pull * DT));
            room_collide_circle(&pl->pos, PLAYER_BODY_R, NULL);
        }
        for (int i = 0; i < G.npb; i++) {
            PBullet *b = &G.pb[i];
            if (b->flags & (PB_MINE | PB_VORTEX | PB_ORBIT)) continue;
            V2 off = v2_sub(e->pos, b->pos);
            float d2 = v2_len2(off);
            if (d2 > 260.0f * 260.0f || d2 < 1.0f) continue;
            float sp = v2_len(b->vel);
            if (sp < 50.0f) continue;
            b->vel = v2_scale(v2_norm(v2_add(b->vel, v2_scale(off, 1500.0f * DT / sqrtf(d2)))), sp);
        }
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 8; k++) {
                EBullet *b = shot(e->pos, e->angle + TAU_F * (float)k / 8.0f, 210.0f, 8.0f, 0xb48aff);
                if (b) b->curve = 0.9f;
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 2.4f;
        }
    } break;
    case EN_SENTINEL:
        if (first_tick(e)) e->k0 = aim + 1.5f;
        if (e->state == 0) { /* a searchlight sweeps the room */
            wander(e, 300.0f, 520.0f, S);
            e->k0 += 0.9f * DT * (float)e->spin_dir;
            if (chance(0.003f)) e->spin_dir = -e->spin_dir;
            if (in_cone(e, e->k0, SENT_CONE, SENT_LEN)) { /* spotted: the alarm hurries everyone */
                e->state = 1;
                e->state_t = 3.0f;
                for (int i = 0; i < G.nen; i++)
                    if (G.en[i].hp > 0.0f && !enemy_is_boss(&G.en[i])) G.en[i].haste_t = 3.0f;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
            }
            if (e->fire_t <= 0.0f) {
                ring(e->pos, 10, 170.0f, 7.0f, frand(0.0f, TAU_F), 0x8ac8ff);
                e->fire_t = 3.0f;
            }
        } else if (e->state == 1) { /* alarm: locks on and streams */
            halt(e, 4.0f);
            turn_to(&e->k0, aim, 2.5f);
            if (e->fire_t <= 0.0f) {
                shot(e->pos, e->k0, 420.0f, 7.0f, 0xff5a5a);
                if ((G.ticks + e->uid) % 2 == 0) sfx_play_at(SFX_ENEMY_SHOT, e->pos);
                e->fire_t = 0.1f;
            }
            if (e->state_t <= 0.0f) {
                e->state = 2;
                e->state_t = 2.5f;
            }
        } else { /* the light cools down */
            wander(e, 300.0f, 520.0f, S);
            e->k0 += 0.4f * DT * (float)e->spin_dir;
            if (e->state_t <= 0.0f) e->state = 0;
        }
        e->angle = e->k0;
        break;
    default:
        break;
    }
}

/* ---- Damage, death ------------------------------------------------------------------ */

bool enemy2_untouchable(const Enemy *e) {
    if (e->type == EN_MOLE) return e->state <= 1; /* underground */
    return false;
}

float enemy2_mass(const Enemy *e) {
    switch (e->type) {
    case EN_BEETLE: return 1.5f;
    case EN_BUZZSAW: return e->state == 0 ? 3.0f : 1.0f;
    case EN_JUGGERNAUT: return 5.0f;
    case EN_CLOCK: case EN_EGG: return 3.0f;
    case EN_IDOL: case EN_CRYSTAL: case EN_TURRET: case EN_SENTRY: case EN_FUNGUS: return 1e6f;
    default: return 1.0f;
    }
}

static void glance(V2 p) {
    fx_burst(p, 4, 80.0f, 300.0f, 0.15f, 2.0f, col_hex(0xd8f6ff, 1));
    if (G.ticks % 4 == 0) sfx_play_at(SFX_DEFLECT, p);
}

float enemy2_damage(Enemy *e, float dmg, V2 knock) {
    bool pierce = upg_armor_piercing(); /* ARMOR PIERCER: shells, plates and spinning blades stop nothing */
    switch (e->type) {
    case EN_BEETLE:
        if (!pierce && from_front(e, knock, BEETLE_ARC)) {
            glance(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)));
            return dmg * 0.25f;
        }
        break;
    case EN_HEDGEHOG:
        if (e->k0 <= 0.0f) { /* a quill thrown back at the ship */
            shot(e->pos, v2_heading(v2_sub(G.pl.pos, e->pos)) + frand(-0.1f, 0.1f), 420.0f, 5.0f, 0xffe0a0);
            e->k0 = 0.18f;
        }
        break;
    case EN_BUZZSAW:
        if (!pierce && e->state == 0) {
            glance(e->pos);
            return dmg * 0.25f;
        }
        break;
    case EN_SPONGE:
        if (!pierce && from_front(e, knock, SPONGE_ARC)) {
            e->k0 += 1.0f;
            return dmg * 0.15f;
        }
        break;
    case EN_PARRY:
        if (e->state == 1) {
            if (e->k0 <= 0.0f) {
                shot(e->pos, v2_heading(v2_sub(G.pl.pos, e->pos)), 460.0f, 7.0f, 0xffffff);
                sfx_play_at(SFX_DEFLECT, e->pos);
                e->k0 = 0.1f;
            }
            glance(e->pos);
            return 0.0f;
        }
        break;
    case EN_FUNGUS:
        if (e->k0 <= 0.0f) {
            EBullet *b = shot(e->pos, frand(0.0f, TAU_F), 110.0f, 8.0f, 0xd0ff8a);
            if (b) {
                b->kind = EB_HOMING;
                b->t1 = 2.0f;
                b->life = 5.0f;
            }
            e->k0 = 0.5f;
        }
        break;
    case EN_JUGGERNAUT:
        if (!pierce && from_front(e, knock, JUGG_ARC)) {
            glance(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)));
            return dmg * 0.3f;
        }
        break;
    case EN_IDOL:
        if (idol_shielded(e)) {
            glance(e->pos);
            return dmg * 0.1f;
        }
        break;
    default:
        break;
    }
    return dmg;
}

float enemy_base_hp(const Enemy *e) {
    if (e->fake) return 1.0f;
    switch (e->type) {
    case EN_CENTIPEDE: return e->radius < 15.0f ? 18.0f : ENEMIES[e->type].hp; /* a body segment (head or not) */
    case EN_HYDRA: {
        static const float HP[3] = {100.0f, 45.0f, 20.0f};
        int g = (int)e->k0;
        return HP[g < 0 ? 0 : g > 2 ? 2 : g];
    }
    case EN_CRYSTAL: return ENEMIES[e->type].hp + 140.0f * e->k0;
    default: return ENEMIES[e->type].hp;
    }
}

bool enemy_resistant(const Enemy *e) {
    if (upg_armor_piercing() && e->type != EN_IDOL && e->type != EN_PARRY) return false;
    switch (e->type) {
    case EN_IDOL: return idol_shielded(e);
    case EN_BUZZSAW: return e->state == 0;
    case EN_PARRY: return e->state == 1;
    case EN_JUGGERNAUT: case EN_SPONGE: return from_front(e, v2(0, 0), JUGG_ARC);
    case EN_BEETLE: return from_front(e, v2(0, 0), BEETLE_ARC);
    default: return e->bubble > 0;
    }
}

void enemy2_on_death(Enemy *e) {
    switch (e->type) {
    case EN_POPPER:
        ring(e->pos, 10, 200.0f, 7.0f, frand(0.0f, TAU_F), 0xff9ad0);
        sfx_play_at(SFX_SPLAT, e->pos);
        break;
    case EN_HYDRA: {
        int gen = (int)e->k0;
        if (gen >= 2) break;
        for (int k = -1; k <= 1; k += 2) {
            V2 p = v2_add(e->pos, v2_scale(v2_perp(v2_angle(e->angle)), e->radius * 0.8f * (float)k));
            room_collide_circle(&p, e->radius * 0.7f, NULL);
            Enemy *c = enemy_spawn_now(EN_HYDRA, p);
            if (!c) break;
            hydra_set_gen(c, gen + 1);
            c->vel = v2_scale(v2_perp(v2_angle(e->angle)), 260.0f * (float)k);
            c->fire_t = frand(0.8f, 1.4f);
        }
        sfx_play_at(SFX_SPLAT, e->pos);
    } break;
    default:
        break;
    }
}

/* ---- Hazards -------------------------------------------------------------------- */

static int add_hz(BossHazard *out, int n, int max, V2 a, V2 b, float r, bool live) {
    if (n >= max) return n;
    out[n] = (BossHazard){a, b, r, live};
    return n + 1;
}

int enemy_hazards(BossHazard *out, int max) {
    int n = 0;
    for (int i = 0; i < G.nen && n < max; i++) {
        const Enemy *e = &G.en[i];
        if (e->hp <= 0.0f || !type_is_new(e->type)) continue;
        switch (e->type) {
        case EN_BEAMER:
            if (e->state >= 1) n = add_hz(out, n, max, e->pos, e->aux, 10.0f, e->state == 2);
            break;
        case EN_ZAPPER:
            if (e->state >= 1) n = add_hz(out, n, max, e->pos, e->aux, 12.0f, e->state == 2);
            break;
        case EN_LINKER: {
            const Enemy *m = linker_partner(e);
            if (m && e->uid < m->uid) {
                int ph = link_phase(e, m);
                if (ph < 2) n = add_hz(out, n, max, e->pos, m->pos, 8.0f, ph == 1);
            }
        } break;
        case EN_HALO:
            for (int k = 0; k < (int)e->k0; k++) {
                V2 p = halo_orb(e, k);
                n = add_hz(out, n, max, p, p, 11.0f, true);
            }
            break;
        case EN_FLAIL: {
            V2 p = flail_ball(e);
            n = add_hz(out, n, max, p, p, FLAIL_BALL_R, true);
        } break;
        case EN_SPOTTER:
            if (e->state == 2 || e->state == 3) n = add_hz(out, n, max, e->aux, e->aux, SPOT_R, e->state == 3);
            break;
        case EN_MANTIS:
            if (e->state == 1) n = add_hz(out, n, max, e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), MANTIS_REACH)), e->radius, false);
            break;
        case EN_MOLE:
            if (e->state == 1 || e->state == 2) n = add_hz(out, n, max, e->pos, e->pos, MOLE_R, e->state == 2);
            break;
        case EN_JUGGERNAUT:
            if (e->state == 1 || e->state == 2) n = add_hz(out, n, max, e->pos, e->pos, JUGG_STOMP_R, e->state == 2);
            break;
        case EN_JELLY:
            for (int k = 0; k < 3; k++) {
                V2 a, b;
                jelly_tentacle(e, k, &a, &b);
                n = add_hz(out, n, max, a, b, 6.0f, true);
            }
            break;
        case EN_BLACKSTAR:
            n = add_hz(out, n, max, e->pos, e->pos, e->radius + STAR_KEEP, false);
            break;
        default:
            break;
        }
    }
    return n;
}

void enemies2_hazards_hurt(void) {
    BossHazard hz[64];
    int n = enemy_hazards(hz, ARRAY_LEN(hz));
    for (int i = 0; i < n; i++)
        if (hz[i].live && seg_dist(G.pl.pos, hz[i].a, hz[i].b) < hz[i].r + G.pl.hit_radius) {
            player_hurt(1);
            break;
        }
}

/* ---- Drawing ------------------------------------------------------------------------ */

float enemy2_alpha(const Enemy *e) {
    switch (e->type) {
    case EN_STALKER:
        return e->state == 0 ? 0.08f : e->state == 1 ? 0.08f + 0.92f * (1.0f - e->state_t / 0.35f) : 1.0f;
    case EN_MOLE: return e->state <= 1 ? 0.0f : 1.0f; /* drawn as a mound meanwhile */
    case EN_MANTIS: return e->state == 0 ? 0.35f : 1.0f;
    default: return 1.0f;
    }
}

void enemy2_draw_glow(const Enemy *e) {
    SDL_FColor c = col_hex(ENEMIES[e->type].color, 1);
    switch (e->type) {
    case EN_BEAMER:
        if (e->state == 1) {
            float k = 1.0f - e->state_t / 0.8f;
            draw_line(e->pos, e->aux, 1.0f + 5.0f * k, col_a(c, 0.2f + 0.4f * k));
        } else if (e->state == 2) {
            draw_line(e->pos, e->aux, 26.0f, col_a(c, 0.25f));
            draw_line(e->pos, e->aux, 12.0f, col_a(c, 0.8f));
            draw_line(e->pos, e->aux, 4.0f, COL_WHITE);
            draw_glow(e->aux, 30.0f, col_a(c, 0.6f));
        }
        break;
    case EN_ZAPPER:
        if (e->state == 1) {
            float k = 1.0f - e->state_t / 0.7f;
            draw_line(e->pos, e->aux, 1.0f + 4.0f * k, col_a(c, 0.2f + 0.5f * k));
            draw_ring(e->aux, 20.0f * (1.5f - k), 2.0f, col_a(c, 0.6f));
        } else if (e->state == 2) { /* a jagged arc */
            V2 prev = e->pos, ab = v2_sub(e->aux, e->pos), side = v2_norm(v2_perp(ab));
            for (int k = 1; k <= 8; k++) {
                float jag = sinf((float)(G.ticks / 3) * 2.3f + (float)k * 1.9f) * 14.0f;
                V2 p = v2_add(v2_add(e->pos, v2_scale(ab, (float)k / 8.0f)), v2_scale(side, k < 8 ? jag : 0.0f));
                draw_line(prev, p, 5.0f, col_a(c, 0.9f));
                draw_line(prev, p, 2.0f, COL_WHITE);
                prev = p;
            }
        }
        break;
    case EN_LINKER: {
        const Enemy *m = linker_partner(e);
        if (m && e->uid < m->uid) {
            int ph = link_phase(e, m);
            if (ph == 0) draw_line(e->pos, m->pos, 2.0f, col_a(c, 0.35f));
            else if (ph == 1) {
                draw_line(e->pos, m->pos, 16.0f, col_a(c, 0.25f));
                draw_line(e->pos, m->pos, 5.0f, col_a(c, 0.9f));
                draw_line(e->pos, m->pos, 2.0f, COL_WHITE);
            }
        }
    } break;
    case EN_HALO:
        for (int k = 0; k < (int)e->k0; k++) draw_glow(halo_orb(e, k), 26.0f, col_a(c, 0.5f));
        break;
    case EN_FLAIL:
        draw_glow(flail_ball(e), 34.0f, col_a(c, 0.3f));
        break;
    case EN_SPOTTER:
        if (e->state == 1) {
            float k = 1.0f - e->state_t / 1.3f;
            draw_line(e->pos, e->aux, 1.0f + 3.0f * k, e->state_t <= 0.35f ? col_hex(0xffffff, 0.7f) : col_a(c, 0.25f + 0.4f * k));
            draw_ring(e->aux, 30.0f * (1.0f - 0.5f * k), 2.0f, col_a(c, 0.6f));
        } else if (e->state == 2) {
            float k = 1.0f - e->state_t / 0.7f;
            draw_circle(e->aux, SPOT_R * k, col_a(c, 0.18f));
            draw_ring(e->aux, SPOT_R, 3.0f, col_a(c, 0.5f + 0.4f * k));
        }
        break;
    case EN_MANTIS:
        if (e->state == 1) {
            float k = 1.0f - e->state_t / 0.4f;
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), MANTIS_REACH)), e->radius * (0.5f + 1.3f * k),
                      col_a(c, 0.12f + 0.25f * k));
        }
        break;
    case EN_MOLE:
        if (e->state == 1) {
            float k = 1.0f - e->state_t / 0.6f;
            draw_ring(e->pos, MOLE_R, 3.0f, col_a(c, 0.4f + 0.5f * k));
            draw_circle(e->pos, MOLE_R * k, col_a(c, 0.15f));
        }
        break;
    case EN_JUGGERNAUT:
        if (e->state == 1) {
            float k = 1.0f - e->state_t / 0.7f;
            draw_ring(e->pos, JUGG_STOMP_R, 4.0f, col_a(c, 0.35f + 0.5f * k));
            draw_circle(e->pos, JUGG_STOMP_R * k, col_a(c, 0.12f));
        }
        break;
    case EN_MEDIC: {
        const Enemy *o = by_uid(e->link);
        if (o && v2_dist2(o->pos, e->pos) < 340.0f * 340.0f) {
            float pulse = 0.5f + 0.5f * sinf(G.time * 10.0f);
            draw_line(e->pos, o->pos, 3.0f + 3.0f * pulse, col_a(c, 0.35f));
            draw_glow(o->pos, o->radius * 2.0f, col_a(c, 0.25f));
        }
    } break;
    case EN_IDOL:
        if (idol_shielded(e))
            for (int i = 0; i < G.nen; i++) {
                const Enemy *o = &G.en[i];
                if (o == e || o->hp <= 0.0f || o->type == EN_IDOL || o->fake) continue;
                draw_line(e->pos, o->pos, 1.5f, col_a(c, 0.08f));
            }
        break;
    case EN_SENTINEL: {
        float len = e->state == 2 ? SENT_LEN * 0.5f : SENT_LEN;
        SDL_FColor lc = e->state == 1 ? col_hex(0xff5a5a, 0.22f) : col_a(c, e->state == 2 ? 0.05f : 0.12f);
        V2 a = v2_add(e->pos, v2_scale(v2_angle(e->k0 - SENT_CONE), len)), b = v2_add(e->pos, v2_scale(v2_angle(e->k0 + SENT_CONE), len));
        draw_tri(e->pos, a, b, lc);
    } break;
    case EN_SENTRY:
        draw_line(e->pos, e->aux, 1.5f, col_a(c, 0.35f));
        break;
    case EN_SWAPPER:
        if (e->state == 1) {
            float k = 1.0f - e->state_t;
            V2 ab = v2_sub(G.pl.pos, e->pos);
            for (int s = 0; s < 12; s += 2)
                draw_line(v2_add(e->pos, v2_scale(ab, (float)s / 12.0f)), v2_add(e->pos, v2_scale(ab, (float)(s + 1) / 12.0f)), 2.0f + 3.0f * k,
                          col_a(c, 0.3f + 0.5f * k));
            draw_ring(G.pl.pos, 30.0f * (1.4f - k), 2.0f, col_a(c, 0.7f));
        }
        break;
    case EN_BLACKSTAR:
        for (int k = 0; k < 3; k++) {
            float ph = fmodf(G.time * 0.6f + (float)k / 3.0f, 1.0f);
            draw_ring(e->pos, e->radius + STAR_PULL_R * 0.4f * (1.0f - ph), 2.0f, col_a(c, 0.15f * ph));
        }
        break;
    case EN_LODESTONE:
        draw_ring(e->pos, LODE_R, 1.5f, col_a(c, 0.08f + 0.04f * sinf(G.time * 4.0f)));
        break;
    case EN_BERSERKER: {
        float rage = 1.0f - clampf(e->hp / e->max_hp, 0.0f, 1.0f);
        draw_glow(e->pos, e->radius * (2.0f + 2.0f * rage), col_hex(0xff2a2a, 0.15f + 0.4f * rage));
    } break;
    case EN_NECRO:
        if (e->k1 > 0.0f) draw_line(e->pos, e->aux, 3.0f, col_hex(0x6aff6a, 0.4f * e->k1));
        break;
    default:
        break;
    }
}

static void pips(V2 c, int face, float s, float ang, SDL_FColor col) {
    static const signed char P[7][6][2] = {
        {{0}}, {{0, 0}}, {{-1, -1}, {1, 1}}, {{-1, -1}, {0, 0}, {1, 1}}, {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
        {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}}, {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},
    };
    face = face < 1 ? 1 : face > 6 ? 6 : face;
    for (int k = 0; k < face; k++) {
        V2 o = v2_rotate(v2((float)P[face][k][0] * s, (float)P[face][k][1] * s), ang);
        draw_circle(v2_add(c, o), s * 0.45f, col);
    }
}

void enemy2_draw(const Enemy *e, V2 at, float rad, SDL_FColor c, SDL_FColor fill) {
    const EnemyDef *d = &ENEMIES[e->type];
    float ang = e->angle;
    int sides = d->sides;
    /* Under the body. */
    switch (e->type) {
    case EN_MOLE:
        if (e->state <= 1) { /* a mound of earth moving under the floor */
            float w = 1.0f + 0.1f * sinf(e->t * 18.0f);
            draw_circle(e->pos, rad * 0.9f * w, col_hex(0x3a2a1a, 0.85f));
            draw_ring(e->pos, rad * 0.9f * w, 3.0f, col_hex(0xc08a5a, 0.7f));
            for (int k = 0; k < 5; k++)
                draw_circle(v2_add(e->pos, v2_scale(v2_angle((float)k * 1.3f + e->t * 2.0f), rad * 0.5f)), 3.0f, col_hex(0xc08a5a, 0.8f));
            return;
        }
        break;
    case EN_FLAIL: {
        V2 b = flail_ball(e);
        for (int k = 1; k <= 6; k++) draw_circle(v2_lerp(e->pos, b, (float)k / 7.0f), 3.0f, col_a(c, 0.7f));
        draw_ngon(b, FLAIL_BALL_R, 8, e->k0 * 2.0f, fill);
        draw_ngon_outline(b, FLAIL_BALL_R, 8, e->k0 * 2.0f, 3.0f, c);
        for (int k = 0; k < 8; k++) {
            float a = e->k0 * 2.0f + TAU_F * (float)k / 8.0f;
            draw_line(v2_add(b, v2_scale(v2_angle(a), FLAIL_BALL_R)), v2_add(b, v2_scale(v2_angle(a), FLAIL_BALL_R + 7.0f)), 3.0f, c);
        }
    } break;
    case EN_JELLY:
        for (int k = 0; k < 3; k++) {
            V2 a, b;
            jelly_tentacle(e, k, &a, &b);
            V2 mid = v2_add(v2_lerp(a, b, 0.5f), v2_scale(v2_perp(v2_norm(v2_sub(b, a))), sinf(e->t * 5.0f + (float)k) * 6.0f));
            draw_line(a, mid, 4.0f, col_a(c, 0.8f));
            draw_line(mid, b, 3.0f, col_a(c, 0.6f));
        }
        break;
    case EN_OCTOPUS:
        for (int k = 0; k < 8; k++) {
            float a = e->angle + TAU_F * (float)k / 8.0f;
            V2 base = v2_add(at, v2_scale(v2_angle(a), rad * 0.8f));
            V2 tip = v2_add(base, v2_scale(v2_angle(a + sinf(e->t * 4.0f + (float)k) * 0.5f), 18.0f));
            draw_line(base, tip, 4.0f, col_a(c, 0.8f));
        }
        break;
    case EN_CRAB:
        for (int k = -1; k <= 1; k += 2) {
            V2 cl = v2_add(at, v2_scale(v2_angle(ang + 0.8f * (float)k), rad + 6.0f));
            draw_circle(cl, 7.0f, fill);
            draw_ring(cl, 7.0f, 2.0f, c);
            for (int j = 0; j < 3; j++) {
                V2 leg = v2_add(at, v2_scale(v2_angle(ang + PI_F * 0.5f * (float)k + (float)(j - 1) * 0.4f), rad));
                draw_line(leg, v2_add(leg, v2_scale(v2_angle(ang + PI_F * 0.5f * (float)k + (float)(j - 1) * 0.4f + sinf(e->t * 14.0f + (float)j) * 0.3f), 10.0f)),
                          2.5f, c);
            }
        }
        break;
    case EN_BUZZER:
        for (int k = -1; k <= 1; k += 2) { /* wings */
            float flap = sinf(e->t * 60.0f) * 0.4f;
            V2 w = v2_add(at, v2_scale(v2_angle(ang + (PI_F * 0.6f + flap) * (float)k), rad * 0.9f));
            draw_circle(w, rad * 0.6f, col_hex(0xffffff, 0.35f));
        }
        break;
    case EN_BAT:
        for (int k = -1; k <= 1; k += 2) {
            float flap = sinf(e->t * 16.0f) * 0.5f;
            V2 tip = v2_add(at, v2_scale(v2_angle(ang + (PI_F * 0.5f + flap) * (float)k), rad * 2.0f));
            draw_tri(at, tip, v2_add(at, v2_scale(v2_angle(ang + PI_F * 0.85f * (float)k), rad * 1.3f)), col_a(c, 0.6f));
        }
        break;
    case EN_SENTRY:
    case EN_TURRET:
        draw_line(at, v2_add(at, v2_scale(v2_angle(ang), rad + 12.0f)), 8.0f, c);
        sides = e->type == EN_SENTRY ? 8 : 4;
        ang = e->type == EN_SENTRY ? PI_F / 8.0f : PI_F * 0.25f;
        break;
    case EN_ROCKETEER:
    case EN_JUGGERNAUT:
        draw_line(at, v2_add(at, v2_scale(v2_angle(e->angle), rad + 14.0f)), e->type == EN_JUGGERNAUT ? 18.0f : 10.0f, c);
        break;
    default:
        break;
    }

    /* The body. */
    switch (e->type) {
    case EN_BUZZSAW: { /* a toothed disc */
        draw_circle(at, rad, fill);
        for (int k = 0; k < 12; k++) {
            float a = e->spin + TAU_F * (float)k / 12.0f;
            draw_tri(v2_add(at, v2_scale(v2_angle(a), rad - 2.0f)), v2_add(at, v2_scale(v2_angle(a + 0.26f), rad - 2.0f)),
                     v2_add(at, v2_scale(v2_angle(a + 0.05f), rad + 8.0f)), c);
        }
        draw_ring(at, rad, 3.0f, c);
    } break;
    case EN_EGG: {
        float wob = e->k0 > 2.0f ? sinf(e->t * (10.0f + 10.0f * (e->k0 - 2.0f))) * 0.25f * (e->k0 - 2.0f) : 0.0f;
        draw_circle(at, rad, fill);
        draw_ring(at, rad, 3.0f, c);
        if (e->k0 > 2.0f) draw_line(v2_add(at, v2_rotate(v2(-rad * 0.6f, -2.0f), wob)), v2_add(at, v2_rotate(v2(rad * 0.4f, 3.0f), wob)), 2.0f, c);
    } break;
    case EN_PUFFER:
    case EN_POPPER:
    case EN_SPONGE:
    case EN_BLACKSTAR: {
        float w = 1.0f + 0.05f * sinf(e->t * 6.0f + (float)e->uid);
        if (e->type == EN_SPONGE) w += 0.03f * e->k0;
        draw_circle(at, rad * w, e->type == EN_BLACKSTAR ? rgba(0, 0, 0, c.a) : fill);
        draw_ring(at, rad * w, 3.0f, c);
    } break;
    default:
        draw_ngon(at, rad, sides, ang, fill);
        draw_ngon_outline(at, rad, sides, ang, 3.0f, c);
        break;
    }

    /* Details on top. */
    switch (e->type) {
    case EN_DRIFTER: draw_circle(at, rad * 0.35f, c); break;
    case EN_PEON:
    case EN_WASP:
    case EN_SKITTER:
        draw_circle(v2_add(at, v2_scale(v2_angle(e->angle), rad * 0.35f)), rad * 0.25f, c);
        break;
    case EN_PULSAR: {
        float k = clampf(1.0f - e->fire_t / 2.8f, 0.0f, 1.0f);
        draw_circle(at, rad * (0.2f + 0.45f * k), col_a(c, 0.9f));
    } break;
    case EN_SNAIL: /* the shell's spiral */
        for (int k = 0; k < 10; k++) {
            float a0 = (float)k * 0.6f, a1 = (float)(k + 1) * 0.6f;
            draw_line(v2_add(at, v2_scale(v2_angle(a0 + e->angle), rad * 0.07f * (float)k)), v2_add(at, v2_scale(v2_angle(a1 + e->angle), rad * 0.07f * (float)(k + 1))),
                      2.5f, c);
        }
        break;
    case EN_POPPER:
        for (int k = 0; k < 3; k++)
            draw_circle(v2_add(at, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 3.0f), rad * 0.45f)), rad * 0.2f, col_a(c, 0.8f));
        break;
    case EN_SPRINKLER:
        draw_line(at, v2_add(at, v2_scale(v2_angle(e->angle), rad + 10.0f)), 6.0f, c);
        draw_circle(at, rad * 0.3f, c);
        break;
    case EN_BEETLE: { /* the shell over its front */
        SDL_FColor sc = e->flash > 0.0f ? COL_WHITE : col_hex(0xd8ffe0, c.a);
        draw_arc(at, rad + 6.0f, 5.0f, e->angle - BEETLE_ARC, e->angle + BEETLE_ARC, sc);
        draw_line(at, v2_add(at, v2_scale(v2_angle(e->angle + PI_F), rad * 0.8f)), 2.0f, c);
    } break;
    case EN_DICE: {
        int face = e->fire_t < 0.5f ? 1 + (G.ticks / 6 + e->uid) % 6 : (int)e->k0;
        pips(at, face, rad * 0.38f, ang + PI_F * 0.25f, c);
    } break;
    case EN_PUFFER:
        for (int k = 0; k < 9; k++) {
            float a = e->angle + TAU_F * (float)k / 9.0f;
            draw_line(v2_add(at, v2_scale(v2_angle(a), rad)), v2_add(at, v2_scale(v2_angle(a), rad + 5.0f + 6.0f * e->k0)), 2.5f, c);
        }
        draw_circle(v2_add(at, v2(-5.0f, -3.0f)), 3.0f, c);
        draw_circle(v2_add(at, v2(5.0f, -3.0f)), 3.0f, c);
        break;
    case EN_PRISM: {
        static const uint32_t C3[3] = {0xff5a5a, 0x5aff7a, 0x5a8aff};
        for (int k = 0; k < 3; k++)
            draw_circle(v2_add(at, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 3.0f), rad * 0.45f)), 4.0f, col_hex(C3[k], c.a));
    } break;
    case EN_BAT:
        draw_circle(v2_add(at, v2_scale(v2_angle(e->angle), 4.0f)), 3.0f, col_hex(0xff5a5a, c.a));
        break;
    case EN_CENTIPEDE:
        if (e->state == 0) { /* the head: mandibles */
            for (int k = -1; k <= 1; k += 2)
                draw_line(v2_add(at, v2_scale(v2_angle(e->angle + 0.5f * (float)k), rad)), v2_add(at, v2_scale(v2_angle(e->angle + 0.2f * (float)k), rad + 9.0f)), 3.0f, c);
            draw_circle(at, rad * 0.3f, c);
        } else {
            for (int k = -1; k <= 1; k += 2) { /* legs */
                V2 s = v2_scale(v2_perp(v2_angle(e->angle)), (float)k);
                draw_line(v2_add(at, v2_scale(s, rad)), v2_add(at, v2_add(v2_scale(s, rad + 6.0f), v2_scale(v2_angle(e->angle), sinf(e->t * 16.0f + (float)k) * 4.0f))), 2.0f, c);
            }
        }
        break;
    case EN_BROODER: {
        float k = clampf(1.0f - e->state_t / 3.2f, 0.0f, 1.0f);
        draw_circle(at, rad * 0.35f * k, col_hex(0xfff0d0, c.a));
    } break;
    case EN_MEDIC: /* a cross */
        draw_line(v2_add(at, v2(-rad * 0.5f, 0)), v2_add(at, v2(rad * 0.5f, 0)), 5.0f, c);
        draw_line(v2_add(at, v2(0, -rad * 0.5f)), v2_add(at, v2(0, rad * 0.5f)), 5.0f, c);
        break;
    case EN_HEDGEHOG:
        for (int k = 0; k < 12; k++) {
            float a = TAU_F * (float)k / 12.0f + e->t * 0.3f;
            draw_line(v2_add(at, v2_scale(v2_angle(a), rad)), v2_add(at, v2_scale(v2_angle(a), rad + 7.0f)), 2.0f, c);
        }
        draw_circle(v2_add(at, v2_scale(v2_angle(e->angle), rad * 0.5f)), 3.0f, c);
        break;
    case EN_SAPPER:
        draw_circle(at, rad * 0.35f, fmodf(e->t, 0.5f) < 0.25f ? col_hex(0xff3d3d, c.a) : c);
        break;
    case EN_ROCKETEER:
        draw_ngon(at, rad * 0.4f, 3, e->angle, c);
        break;
    case EN_MISSILE:
        draw_line(at, v2_sub(at, v2_scale(v2_angle(e->angle), rad + 6.0f)), 4.0f, col_hex(0xffa05a, c.a));
        break;
    case EN_JESTER: { /* a hat with bells of every colour */
        static const uint32_t B[3] = {0xff5ad8, 0x5affff, 0xffff5a};
        for (int k = 0; k < 3; k++)
            draw_circle(v2_add(at, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 3.0f), rad + 4.0f)), 4.5f, col_hex(B[k], c.a));
    } break;
    case EN_FROSTBITE:
        for (int k = 0; k < 3; k++) {
            V2 a = v2_scale(v2_angle(e->angle + PI_F * (float)k / 3.0f), rad * 0.7f);
            draw_line(v2_sub(at, a), v2_add(at, a), 2.5f, col_hex(0xffffff, c.a));
        }
        break;
    case EN_GLITCH: { /* colour-split copies */
        float j = 2.0f + 4.0f * fabsf(sinf((float)G.ticks * 1.7f + (float)e->uid));
        draw_ngon_outline(v2_add(at, v2(j, 0)), rad, 4, ang, 2.0f, col_hex(0xff3dff, 0.6f * c.a));
        draw_ngon_outline(v2_add(at, v2(-j, 0)), rad, 4, ang, 2.0f, col_hex(0x3dffff, 0.6f * c.a));
    } break;
    case EN_PEEKER:
        if (e->state) { /* hands over its eyes */
            draw_rect(at.x - rad * 0.7f, at.y - 6.0f, rad * 1.4f, 8.0f, c);
        } else {
            draw_circle(v2_add(at, v2(-6.0f, -3.0f)), 4.0f, COL_WHITE);
            draw_circle(v2_add(at, v2(6.0f, -3.0f)), 4.0f, COL_WHITE);
            draw_circle(v2_add(v2_add(at, v2(-6.0f, -3.0f)), v2_scale(v2_angle(e->angle), 1.5f)), 2.0f, rgba(0, 0, 0, 1));
            draw_circle(v2_add(v2_add(at, v2(6.0f, -3.0f)), v2_scale(v2_angle(e->angle), 1.5f)), 2.0f, rgba(0, 0, 0, 1));
        }
        break;
    case EN_ECHO:
        draw_ring(at, rad * 0.55f, 2.0f, c);
        draw_ring(at, rad * 0.25f, 2.0f, c);
        break;
    case EN_CLOCK:
        draw_line(at, v2_add(at, v2_scale(v2_angle(e->k1), rad * 0.85f)), 2.5f, c);
        draw_line(at, v2_add(at, v2_scale(v2_angle(e->k0), rad * 0.55f)), 5.0f, c);
        draw_circle(at, 4.0f, c);
        break;
    case EN_HALO:
        for (int k = 0; k < (int)e->k0; k++) {
            V2 p = halo_orb(e, k);
            draw_circle(p, 10.0f, col_hex(0xfff07a, c.a));
            draw_circle(p, 5.0f, col_hex(0xffffff, c.a));
        }
        draw_circle(at, rad * 0.3f, c);
        break;
    case EN_STALKER:
        draw_circle(v2_add(at, v2_scale(v2_angle(e->angle), rad * 0.3f)), 3.5f, col_hex(0xff3d3d, c.a));
        break;
    case EN_OCTOPUS:
        draw_circle(v2_add(at, v2(-7.0f, -2.0f)), 4.0f, c);
        draw_circle(v2_add(at, v2(7.0f, -2.0f)), 4.0f, c);
        break;
    case EN_BUZZSAW:
        draw_circle(at, 6.0f, e->state == 1 ? col_hex(0xff7a3d, c.a) : c);
        break;
    case EN_SENTRY:
    case EN_TURRET:
        draw_circle(at, rad * 0.4f, c);
        break;
    case EN_TORCH:
        if (e->state == 1) draw_glow(v2_add(at, v2_scale(v2_angle(e->angle), rad + 8.0f)), 20.0f, col_hex(0xffd23d, 0.8f));
        draw_line(at, v2_add(at, v2_scale(v2_angle(e->angle), rad + 8.0f)), 7.0f, c);
        break;
    case EN_ZAPPER:
        draw_line(v2_add(at, v2(-rad * 0.4f, -rad * 0.5f)), v2_add(at, v2(rad * 0.1f, 0)), 3.0f, c);
        draw_line(v2_add(at, v2(rad * 0.1f, 0)), v2_add(at, v2(-rad * 0.1f, rad * 0.1f)), 3.0f, c);
        draw_line(v2_add(at, v2(-rad * 0.1f, rad * 0.1f)), v2_add(at, v2(rad * 0.4f, rad * 0.5f)), 3.0f, c);
        break;
    case EN_BEAMER:
        draw_circle(v2_add(at, v2_scale(v2_angle(e->angle), rad * 0.4f)), rad * 0.3f, e->state == 2 ? COL_WHITE : c);
        break;
    case EN_ENGINEER: /* a wrench */
        draw_line(v2_add(at, v2(-rad * 0.5f, rad * 0.5f)), v2_add(at, v2(rad * 0.4f, -rad * 0.4f)), 4.0f, c);
        draw_ring(v2_add(at, v2(rad * 0.45f, -rad * 0.45f)), 5.0f, 2.5f, c);
        break;
    case EN_JELLY:
        draw_circle(at, rad * 0.45f, col_a(c, 0.5f));
        break;
    case EN_SCORPION: { /* the tail curled over its back */
        V2 back = v2_scale(v2_angle(e->angle), -1.0f);
        V2 t1 = v2_add(at, v2_scale(back, rad + 8.0f)), t2 = v2_add(t1, v2_add(v2_scale(back, -6.0f), v2(0, -14.0f)));
        draw_line(at, t1, 4.0f, c);
        draw_line(t1, t2, 4.0f, c);
        draw_circle(t2, 4.0f, e->fire_t < 0.4f ? COL_WHITE : c);
    } break;
    case EN_LINKER:
        draw_ngon_outline(at, rad * 0.5f, 4, -e->angle, 2.0f, c);
        break;
    case EN_SWAPPER:
        draw_line(v2_add(at, v2(-rad * 0.5f, -4.0f)), v2_add(at, v2(rad * 0.5f, -4.0f)), 2.5f, c);
        draw_line(v2_add(at, v2(-rad * 0.5f, 4.0f)), v2_add(at, v2(rad * 0.5f, 4.0f)), 2.5f, c);
        break;
    case EN_SPOTTER:
        draw_ring(at, rad * 0.5f, 2.0f, c);
        draw_circle(at, 3.0f, c);
        break;
    case EN_LODESTONE: /* red and blue poles */
        draw_circle(v2_add(at, v2_scale(v2_angle(e->angle), rad * 0.45f)), rad * 0.3f, col_hex(0xff5a5a, c.a));
        draw_circle(v2_sub(at, v2_scale(v2_angle(e->angle), rad * 0.45f)), rad * 0.3f, col_hex(0x5a8aff, c.a));
        break;
    case EN_SPONGE:
        draw_arc(at, rad + 6.0f, 4.0f, e->angle - SPONGE_ARC, e->angle + SPONGE_ARC, col_hex(0xffffff, 0.8f * c.a));
        for (int k = 0; k < 5; k++)
            draw_circle(v2_add(at, v2_scale(v2_angle((float)k * 2.1f + 0.4f), rad * 0.5f)), 3.0f + 0.4f * e->k0, col_mul(c, 0.6f));
        break;
    case EN_PARRY:
        if (e->state == 1) {
            draw_ring(at, rad + 8.0f, 4.0f, col_hex(0xffffff, 0.9f * c.a));
            draw_line(v2_add(at, v2(-rad, -rad)), v2_add(at, v2(rad, rad)), 3.0f, c);
            draw_line(v2_add(at, v2(-rad, rad)), v2_add(at, v2(rad, -rad)), 3.0f, c);
        } else {
            draw_line(at, v2_add(at, v2_scale(v2_angle(e->angle), rad + 10.0f)), 3.0f, c);
        }
        break;
    case EN_BUBBLER:
        for (int k = 0; k < 3; k++)
            draw_ring(v2_add(at, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 3.0f), rad * 0.45f)), 4.0f, 1.5f, c);
        break;
    case EN_DRUMMER: {
        float k = clampf(1.0f - e->fire_t / 3.2f, 0.0f, 1.0f);
        draw_ring(at, rad * 0.65f, 2.0f + 3.0f * k * k, c);
        draw_line(v2_add(at, v2(-rad, -rad * 0.9f)), v2_add(at, v2(-rad * 0.3f, -rad * 0.3f)), 3.0f, c);
        draw_line(v2_add(at, v2(rad, -rad * 0.9f)), v2_add(at, v2(rad * 0.3f, -rad * 0.3f)), 3.0f, c);
    } break;
    case EN_TRICKSTER:
        draw_circle(v2_add(at, v2(-5.0f, -3.0f)), 3.0f, c);
        draw_circle(v2_add(at, v2(5.0f, -3.0f)), 3.0f, c);
        draw_arc(v2_add(at, v2(0, 2.0f)), rad * 0.4f, 2.0f, 0.3f, PI_F - 0.3f, c);
        break;
    case EN_HYDRA:
        for (int k = 0; k < 3 - (int)e->k0; k++) {
            float a = e->angle + ((float)k - (float)(2 - (int)e->k0) * 0.5f) * 0.6f;
            V2 h = v2_add(at, v2_scale(v2_angle(a), rad + 6.0f));
            draw_line(at, h, 4.0f, c);
            draw_circle(h, 5.0f, c);
        }
        break;
    case EN_MANTIS:
        for (int k = -1; k <= 1; k += 2) {
            float a = e->angle + (e->state == 1 ? 0.9f : 0.5f) * (float)k;
            V2 j = v2_add(at, v2_scale(v2_angle(a), rad + 4.0f));
            draw_line(at, j, 3.0f, c);
            draw_line(j, v2_add(j, v2_scale(v2_angle(e->angle - 0.6f * (float)k), 12.0f)), 3.0f, c);
        }
        break;
    case EN_FLAIL:
        draw_circle(at, rad * 0.35f, c);
        break;
    case EN_MOLE:
        draw_circle(v2_add(at, v2(0, 4.0f)), 5.0f, col_hex(0xff9ab4, c.a)); /* nose */
        draw_line(v2_add(at, v2(-rad * 0.5f, -4.0f)), v2_add(at, v2(-rad * 0.2f, -4.0f)), 2.0f, c);
        draw_line(v2_add(at, v2(rad * 0.2f, -4.0f)), v2_add(at, v2(rad * 0.5f, -4.0f)), 2.0f, c);
        break;
    case EN_CRYSTAL:
        draw_ngon_outline(at, rad * 0.55f, 6, -e->angle * 1.5f, 2.0f, c);
        draw_line(v2_add(at, v2_scale(v2_angle(e->angle), rad)), v2_sub(at, v2_scale(v2_angle(e->angle), rad)), 1.5f, col_a(c, 0.5f));
        break;
    case EN_NECRO: /* a skull */
        draw_circle(v2_add(at, v2(-5.0f, -2.0f)), 4.0f, rgba(0, 0, 0, c.a));
        draw_circle(v2_add(at, v2(5.0f, -2.0f)), 4.0f, rgba(0, 0, 0, c.a));
        draw_line(v2_add(at, v2(-5.0f, 8.0f)), v2_add(at, v2(5.0f, 8.0f)), 2.0f, c);
        break;
    case EN_BERSERKER:
        for (int k = -1; k <= 1; k += 2)
            draw_line(v2_add(at, v2((float)k * 9.0f, -7.0f)), v2_add(at, v2((float)k * 3.0f, -3.0f)), 3.0f, col_hex(0xffffff, c.a));
        break;
    case EN_FUNGUS:
        for (int k = 0; k < 5; k++)
            draw_circle(v2_add(at, v2_scale(v2_angle((float)k * 1.25f + e->angle), rad * 0.55f)), 4.0f + (float)(k % 2) * 2.0f, col_hex(0xf0ffc8, c.a));
        break;
    case EN_WEAVER:
        for (int k = 0; k < 8; k++) {
            float a = TAU_F * (float)k / 8.0f + 0.2f;
            V2 knee = v2_add(at, v2_scale(v2_angle(a), rad + 6.0f));
            draw_line(at, knee, 2.0f, c);
            draw_line(knee, v2_add(knee, v2_scale(v2_angle(a + 0.8f), 8.0f + sinf(e->t * 10.0f + (float)k) * 2.0f)), 2.0f, c);
        }
        draw_circle(at, rad * 0.35f, col_hex(0xff3d3d, c.a));
        break;
    case EN_JUGGERNAUT: {
        SDL_FColor sc = e->flash > 0.0f ? COL_WHITE : col_hex(0xe0e0ff, c.a);
        draw_arc(at, rad + 7.0f, 7.0f, e->angle - JUGG_ARC, e->angle + JUGG_ARC, sc);
        draw_circle(at, rad * 0.4f, c);
        draw_circle(at, rad * 0.25f, fill);
    } break;
    case EN_IDOL: {
        bool sh = idol_shielded(e);
        draw_ngon_outline(at, rad * 0.6f, 4, PI_F * 0.25f, 2.0f, c);
        draw_circle(at, rad * 0.25f, sh ? col_hex(0xffffff, c.a) : col_hex(0xff5a3d, c.a));
        if (sh) draw_ring(at, rad + 8.0f, 2.0f, col_a(c, 0.5f + 0.3f * sinf(G.time * 5.0f)));
    } break;
    case EN_BLACKSTAR:
        for (int k = 0; k < 3; k++)
            draw_arc(at, rad * (0.4f + 0.2f * (float)k), 2.0f, e->angle * (1.0f + (float)k) + (float)k, e->angle * (1.0f + (float)k) + (float)k + 2.0f, c);
        break;
    case EN_SENTINEL:
        draw_circle(v2_add(at, v2_scale(v2_angle(e->k0), rad * 0.4f)), rad * 0.3f, e->state == 1 ? col_hex(0xff3d3d, c.a) : col_hex(0xffffff, c.a));
        break;
    default:
        break;
    }
    /* BUBBLER's bubble and DRUMMER's haste are drawn for every type (enemies_draw). */
    (void)fill;
}
