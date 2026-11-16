#include "audio.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * Bosses: one per boss room (every BOSS_EVERY rooms). Room 10 draws one of four bosses of the
 * same strength (THE OVERSEER, THE LIGHTHOUSE, THE WYRM, THE STORMCALLER); rooms 20, 30 and 40 have
 * their own, each harder than the last (THE FORGE, THE CARRIER, THE SINGULARITY); beyond, any of
 * the seven. Like every enemy a boss never scales with depth: later ones only call more minions.
 *
 * Shared rules: three phases (boss_phase_threshold), a phase change clears the screen and makes it
 * invulnerable for BOSS_INVULN_T (enemy_damage_cap), the last phase runs every timer faster.
 * Attacks are states (Enemy.state, 0 = resting), picked at random from what the phase allows, never
 * the same twice in a row (Enemy.burst = last one). What a boss keeps outside its Enemy lives in
 * Room (boss_seg, arc_mask, wells, gravity: saved with the game). Some fight in their own room
 * (room_boss_layout: always the same walls, symmetric both ways).
 *
 * Anything that hurts besides bullets and the body (lightning arcs, the wyrm's armoured body) is a
 * BossHazard: boss_hazards lists it, the boss hurts the ship with it, the bot keeps out of it and
 * boss_draw_under draws it. Like every enemy, it all stops under TIME STOP (boss_update is not run).
 */

int g_boss_override = -1;

#define BOSS_INVULN_T 1.4f
#define BOSS_MAX_MINIONS 6

/* ---- Shared ---------------------------------------------------------------- */

static int minions_alive(void) {
    int n = G.nsp;
    for (int i = 0; i < G.nen; i++)
        if (!enemy_is_boss(&G.en[i]) && G.en[i].hp > 0.0f) n++;
    return n;
}

/* Later bosses call more minions (the minions themselves never change). */
static void summon(V2 at, float dist, int base, EnemyType a, EnemyType b) {
    int n = base + G.room.depth / BOSS_EVERY;
    if (n > BOSS_MAX_MINIONS) n = BOSS_MAX_MINIONS;
    float off = frand(0.0f, TAU_F);
    for (int k = 0; k < n && G.nen + G.nsp < MAX_ENEMIES - 4; k++) {
        V2 p = v2_add(at, v2_scale(v2_angle(off + TAU_F * (float)k / (float)n + frand(-0.2f, 0.2f)), dist));
        p.x = clampf(p.x, ARENA_X0 + 60.0f, ARENA_X1 - 60.0f);
        p.y = clampf(p.y, ARENA_Y0 + 60.0f, ARENA_Y1 - 60.0f);
        room_collide_circle(&p, 30.0f, NULL);
        enemy_queue_spawn(k & 1 ? b : a, p, 0.8f);
    }
    fx_particle(at, v2(0, 0), 0.5f, 260.0f, col_hex(COLOR_BOSS, 1), 0.0f, PT_RING);
    sfx_play_at(SFX_ENEMY_APPEAR, at);
}

/* Start one of the attacks allowed (never the same twice in a row). */
static int pick(Enemy *e, const int *pool, int n) {
    int a;
    do a = pool[irand(0, n - 1)];
    while (a == e->burst && n > 1);
    e->burst = a;
    e->state = a;
    e->fire_t = 0.0f;
    e->spin = frand(0.0f, TAU_F);
    return a;
}

static void rest(Enemy *e, float t) {
    e->state = 0;
    e->state_t = t;
}

/* Ring of n slots with `gap` empty ones in a row starting at a random slot. */
static void ring_with_gap(V2 pos, int n, int gap, float speed, float radius, float offset, SDL_FColor col) {
    int start = irand(0, n - 1);
    for (int k = 0; k < n; k++) {
        if ((k - start + n) % n < gap) continue;
        ebullet_spawn(pos, offset + TAU_F * (float)k / (float)n, speed, radius, col);
    }
}

static float wrap_pi(float a) { return atan2f(sinf(a), cosf(a)); }

static float seg_dist(V2 p, V2 a, V2 b) {
    V2 ab = v2_sub(b, a);
    float l2 = v2_len2(ab);
    float t = l2 > 1e-6f ? clampf(v2_dot(v2_sub(p, a), ab) / l2, 0.0f, 1.0f) : 0.0f;
    return v2_dist(p, v2_add(a, v2_scale(ab, t)));
}

/* How far a ray from `from` goes before leaving the arena. */
static float arena_ray_len(V2 from, V2 dir) {
    float t = 4000.0f;
    if (dir.x > 1e-4f) t = minf(t, (ARENA_X1 - from.x) / dir.x);
    if (dir.x < -1e-4f) t = minf(t, (ARENA_X0 - from.x) / dir.x);
    if (dir.y > 1e-4f) t = minf(t, (ARENA_Y1 - from.y) / dir.y);
    if (dir.y < -1e-4f) t = minf(t, (ARENA_Y0 - from.y) / dir.y);
    return maxf(t, 0.0f);
}

/* Where a beam fired from `from` stops: the first wall, or the arena's edge. */
static V2 ray_end(V2 from, float ang) {
    V2 dir = v2_angle(ang);
    float hi = arena_ray_len(from, dir);
    if (room_line_clear_walls(from, v2_add(from, v2_scale(dir, hi)), 0.0f)) return v2_add(from, v2_scale(dir, hi));
    float lo = 0.0f;
    for (int i = 0; i < 11; i++) {
        float mid = (lo + hi) * 0.5f;
        if (room_line_clear_walls(from, v2_add(from, v2_scale(dir, mid)), 0.0f)) lo = mid;
        else hi = mid;
    }
    return v2_add(from, v2_scale(dir, lo));
}

/* A beam: a stream of fast bullets, stopped by the walls like any bullet. */
static void beam_shot(V2 from, float ang, float speed, float radius, SDL_FColor col) {
    ebullet_spawn(from, ang, speed, radius, col);
}

/* Phase change: true when it happens (the boss then rests while invulnerable). */
static bool phase_change(Enemy *e) {
    if (e->phase + 1 >= BOSS_PHASES || e->hp >= e->max_hp * boss_phase_threshold(e->phase + 1)) return false;
    SDL_FColor c1 = col_hex(ENEMIES[e->type].color, 1), c3 = col_hex(0xffd23d, 1);
    e->phase++;
    e->inv_t = BOSS_INVULN_T;
    e->alt = 0.0f;
    ebullets_clear(true);
    enemy_fire_ring(e->pos, 40, 210.0f, 9.0f, frand(0.0f, TAU_F), c3);
    fx_particle(e->pos, v2(0, 0), 0.6f, 420.0f, c3, 0.0f, PT_RING);
    fx_explosion(e->pos, e->radius, c1);
    fx_shake(0.35f);
    G.hitstop = fmaxf(G.hitstop, 0.12f);
    fx_popup(v2(e->pos.x, e->pos.y - e->radius - 30.0f), c3, e->phase == 2 ? "ENRAGED!" : "PHASE %d", e->phase + 1);
    sfx_play_ex(SFX_EXPLODE_BIG, 1.0f, 0.7f, 0.0f);
    G.room.arc_mask = 0; /* whatever was charging is called off */
    G.room.arc_t = 0.0f;
    rest(e, BOSS_INVULN_T);
    return true;
}

/* Drift towards e->target, a new one picked in the given box when reached. */
static void drift(Enemy *e, float x0, float y0, float x1, float y1, float speed) {
    if (v2_dist2(e->pos, e->target) < 40.0f * 40.0f || e->t < DT * 2.0f) e->target = v2(frand(x0, x1), frand(y0, y1));
    enemy_steer(e, v2_scale(v2_norm(v2_sub(e->target, e->pos)), speed), 2.0f);
}

/* ======================================================================== */
/* ---- THE OVERSEER (room 10) ------------------------------------------- */
/*
 * Hovers in the upper half and cycles through attacks, with a short rest between two.
 * Phase 1 (100-66%): spiral, aimed fans, rings with a gap.
 * Phase 2 (66-33%):  + telegraphed charge leaving a bullet trail, rotating cross of streams.
 * Phase 3 (33-0%):   everything faster, double counter-rotating spiral, calls in minions.
 */
enum { BA_REST, BA_SPIRAL, BA_FANS, BA_RING, BA_CHARGE_AIM, BA_CHARGE, BA_CROSS, BA_SUMMON };
#define BOSS_CHARGE_SPEED 1100.0f
#define BOSS_CHARGE_AIM_T 0.8f

static void overseer_pick(Enemy *e) {
    int pool[8], n = 0;
    pool[n++] = BA_SPIRAL;
    pool[n++] = BA_FANS;
    pool[n++] = BA_RING;
    if (e->phase >= 1) {
        pool[n++] = BA_CHARGE_AIM;
        pool[n++] = BA_CROSS;
    }
    if (e->phase >= 2 && minions_alive() < BOSS_MAX_MINIONS / 2) pool[n++] = BA_SUMMON;
    switch (pick(e, pool, n)) {
    case BA_SPIRAL: e->state_t = 2.8f; break;
    case BA_FANS: e->state_t = 1.3f; break;
    case BA_RING: e->state_t = 1.7f; break;
    case BA_CHARGE_AIM: e->state_t = BOSS_CHARGE_AIM_T; sfx_play_at(SFX_SNIPER_CHARGE, e->pos); break;
    case BA_CROSS: e->state_t = 3.2f; break;
    default: e->state_t = 0.6f; break;
    }
}

static void update_overseer(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    const SDL_FColor c1 = col_hex(0xff3df0, 1), c2 = col_hex(0xff9af5, 1), c3 = col_hex(0xffd23d, 1);

    /* Movement: drift between points of the upper half, nearly still while firing. */
    if (e->state != BA_CHARGE) {
        float sp = e->state == BA_REST ? d->speed * 1.6f : e->state == BA_CHARGE_AIM ? 0.0f : d->speed * 0.5f;
        drift(e, ARENA_X0 + 260.0f, ARENA_Y0 + 150.0f, ARENA_X1 - 260.0f, ARENA_CY - 60.0f, sp);
    }

    switch (e->state) {
    case BA_REST:
        e->angle += 0.6f * DT;
        if (e->state_t <= 0.0f) overseer_pick(e);
        break;

    case BA_SPIRAL: {
        e->spin += 1.7f * DT * (float)e->spin_dir;
        e->angle = e->spin;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 3; k++) {
                ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / 3.0f, 240.0f, 8.0f, c1);
                if (e->phase >= 2) ebullet_spawn(e->pos, -e->spin + TAU_F * ((float)k + 0.5f) / 3.0f, 200.0f, 8.0f, c2);
            }
            e->fire_t = 0.07f;
            sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
        }
        if (e->state_t <= 0.0f) {
            e->spin_dir = -e->spin_dir;
            rest(e, 0.9f);
        }
    } break;

    case BA_FANS:
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            enemy_fire_fan(e->pos, aim, 7, 1.0f, 360.0f, 9.0f, c2);
            if (e->phase >= 1) enemy_fire_fan(e->pos, aim, 6, 0.85f, 260.0f, 8.0f, c1); /* a slower fan in the gaps */
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = 0.42f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.8f);
        break;

    case BA_RING:
        e->angle -= 1.2f * DT;
        if (e->fire_t <= 0.0f) {
            /* 32 slots, 4 left empty: the gap drifts each ring, find it and slip through. */
            ring_with_gap(e->pos, 32, 4, 230.0f, 9.0f, e->spin, c3);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 0.55f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
        break;

    case BA_CHARGE_AIM:
        enemy_steer(e, v2(0, 0), 8.0f);
        if (e->state_t > 0.25f) e->angle = aim; /* tracks, then locks just before charging */
        if (e->state_t <= 0.0f) {
            e->state = BA_CHARGE;
            e->state_t = 0.75f;
            e->vel = v2_scale(v2_angle(e->angle), BOSS_CHARGE_SPEED);
            sfx_play_at(SFX_DASH, e->pos);
            fx_shake(0.15f);
        }
        break;

    case BA_CHARGE: {
        V2 fwd = v2_angle(e->angle);
        e->vel = v2_scale(fwd, BOSS_CHARGE_SPEED);
        if (e->fire_t <= 0.0f) { /* a trail of slow bullets drifting sideways */
            ebullet_spawn(e->pos, e->angle + PI_F * 0.5f, 70.0f, 8.0f, c1);
            ebullet_spawn(e->pos, e->angle - PI_F * 0.5f, 70.0f, 8.0f, c1);
            e->fire_t = 0.045f;
        }
        if ((G.ticks & 1) == 0) fx_particle(e->pos, v2(0, 0), 0.25f, e->radius * 1.2f, col_a(c1, 0.4f), 0.0f, PT_GLOW);
        V2 ahead = v2_add(e->pos, v2_scale(fwd, e->radius + 10.0f));
        bool wall = ahead.x < ARENA_X0 || ahead.x > ARENA_X1 || ahead.y < ARENA_Y0 || ahead.y > ARENA_Y1;
        if (e->state_t <= 0.0f || wall) {
            e->vel = v2_scale(e->vel, 0.1f);
            enemy_fire_ring(e->pos, 20, 260.0f, 9.0f, e->angle, c3);
            sfx_play_at(SFX_TANK_FIRE, e->pos);
            fx_shake(0.2f);
            rest(e, 1.0f);
        }
    } break;

    case BA_CROSS: {
        /* Mid-way the cross reverses: dodging around it in the same direction stops working. */
        float dirn = e->state_t > 1.6f ? 1.0f : -1.0f;
        e->spin += 0.95f * DT * dirn * (float)e->spin_dir;
        e->angle = e->spin;
        if (e->fire_t <= 0.0f) {
            int arms = e->phase >= 2 ? 6 : 4;
            for (int k = 0; k < arms; k++) ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / (float)arms, 290.0f, 8.0f, c2);
            e->fire_t = 0.085f;
            sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
    } break;

    case BA_SUMMON:
        e->angle += 4.0f * DT;
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            summon(e->pos, e->radius + 70.0f, 2, EN_CHASER, EN_SHOOTER);
            e->fire_t = -2.0f; /* once */
        }
        if (e->state_t <= 0.0f) rest(e, 0.5f);
        break;

    default: rest(e, 0.5f); break;
    }
}

/* ======================================================================== */
/* ---- THE LIGHTHOUSE (room 10) ----------------------------------------- */
/*
 * Bolted to the middle of a room with four pillars and two fins around it: it never moves and
 * cannot be pushed. Its lamp sweeps beams (streams of fast bullets) round the room, one per phase
 * (1, 2, 3), shown 0.9 s ahead by a thin line: the walls stop them, so the answer is to hide in
 * the shadow of a pillar and come out between two sweeps. In between: rings of flares with a gap,
 * homing gulls, and shells lobbed over the walls onto whoever hides too long.
 * Phase 3 calls minions.
 */
enum { LH_REST, LH_WARN, LH_BEAM, LH_FLARE, LH_GULLS, LH_SHELLS, LH_SUMMON };
#define LH_WARN_T 0.9f
#define LH_BEAM_T 3.0f

static int lh_beams(const Enemy *e) { return 1 + e->phase; }
static float lh_beam_rate(const Enemy *e) { return 0.55f + 0.12f * (float)e->phase; }
static float lh_beam_angle(const Enemy *e, int k) { return e->spin + TAU_F * (float)k / (float)lh_beams(e); }

static void update_lighthouse(Enemy *e, float aim) {
    const SDL_FColor beam = col_hex(0xfff2a0, 1), flare = col_hex(0xffb03d, 1), gull = col_hex(0xffffff, 1);
    e->pos = e->target; /* bolted down */
    e->vel = v2(0, 0);
    switch (e->state) {
    case LH_REST:
        e->angle += 0.4f * DT;
        if (e->state_t <= 0.0f) {
            int pool[8], n = 0;
            pool[n++] = LH_FLARE;
            pool[n++] = LH_GULLS;
            pool[n++] = LH_SHELLS; /* over the walls: hiding for too long does not work */
            if (e->phase >= 2 && minions_alive() < 3) pool[n++] = LH_SUMMON;
            pool[n++] = LH_WARN; /* the beams come back more often than the rest */
            int a = pick(e, pool, n);
            switch (a) {
            case LH_WARN:
                /* the first beam starts a little to the side of the ship and sweeps towards it */
                e->spin = aim - 0.55f * (float)e->spin_dir;
                e->state_t = LH_WARN_T;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
                break;
            case LH_FLARE: e->state_t = 1.75f; break;
            case LH_GULLS: e->state_t = 1.4f; break;
            case LH_SHELLS: e->state_t = 1.2f; break;
            default: e->state_t = 0.8f; break;
            }
        }
        break;

    case LH_WARN:
        e->angle = e->spin;
        if (e->state_t <= 0.0f) {
            e->state = LH_BEAM;
            e->state_t = LH_BEAM_T;
            sfx_play_at(SFX_SNIPER_FIRE, e->pos);
        }
        break;

    case LH_BEAM: {
        float rate = lh_beam_rate(e);
        float dirn = e->phase >= 2 && e->state_t < LH_BEAM_T * 0.5f ? -1.0f : 1.0f; /* the last phase turns back */
        e->spin += rate * DT * (float)e->spin_dir * dirn;
        e->angle = e->spin;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < lh_beams(e); k++) {
                float a = lh_beam_angle(e, k);
                beam_shot(v2_add(e->pos, v2_scale(v2_angle(a), e->radius * 0.5f)), a, 760.0f, 7.0f, beam);
            }
            e->fire_t = 0.025f;
        }
        if ((G.ticks % 24) == 0) sfx_play_at(SFX_SHOT_RAIL, e->pos);
        if (e->state_t <= 0.0f) {
            e->spin_dir = -e->spin_dir;
            rest(e, 1.0f);
        }
    } break;

    case LH_FLARE:
        e->angle -= 1.0f * DT;
        if (e->fire_t <= 0.0f) {
            ring_with_gap(e->pos, 36, 5, 210.0f, 9.0f, e->spin, flare);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 0.55f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
        break;

    case LH_GULLS:
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            int n = e->phase >= 1 ? 12 : 10;
            for (int k = 0; k < n; k++) {
                EBullet *b = ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / (float)n, 260.0f, 8.0f, gull);
                if (b) {
                    b->kind = EB_HOMING;
                    b->t1 = 1.5f;
                    b->life = 6.0f;
                }
            }
            sfx_play_at(SFX_SHOT_SEEKER, e->pos);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    case LH_SHELLS:
        /* over the walls: nowhere stays safe for long */
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            for (int k = 0; k < 5; k++) {
                V2 to = k == 0 ? G.pl.pos : v2_add(G.pl.pos, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(70.0f, 170.0f)));
                ebullet_lob(e->pos, to, frand(1.0f, 1.3f), 8, flare);
            }
            sfx_play_at(SFX_LOB, e->pos);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
        break;

    case LH_SUMMON:
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            summon(e->pos, 330.0f, 2, EN_SHOOTER, EN_ORBITER);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.6f);
        break;

    default: rest(e, 0.5f); break;
    }
}

/* ======================================================================== */
/* ---- THE WYRM (room 10) ----------------------------------------------- */
/*
 * A serpent of WYRM_SEGS armoured segments (Room.boss_seg) behind its head: only the head can be
 * hurt, the body stops shots and hurts on contact. It slithers after the ship spitting fans, makes
 * its ribs fire sideways, burrows (untouchable, a mound shows where it goes) and bursts out under
 * the ship (a ring warns 0.75 s before) in a ring of bullets. From phase 2 it also coils round the
 * ship, its tail dropping still bullets. Each phase change sheds 3 tail segments that crawl off as
 * serpents.
 */
enum { WY_SLITHER, WY_BURROW, WY_RISE, WY_COIL }; /* WY_BURROW / WY_RISE: untouchable (enemy_untouchable) */
#define WYRM_SPACING 40.0f
#define WYRM_SEG_R 26.0f
#define WYRM_RISE_T 0.75f
#define WYRM_RISE_R 120.0f
#define WYRM_REAR_T 0.55f

static bool wyrm_buried(const Enemy *e) { return e->state == WY_BURROW || e->state == WY_RISE; }

static void wyrm_turn_to(Enemy *e, V2 to, float speed, float rate) {
    float want = v2_heading(v2_sub(to, e->pos));
    e->angle += clampf(wrap_pi(want - e->angle), -rate * DT, rate * DT);
    enemy_steer(e, v2_scale(v2_angle(e->angle), speed), 6.0f);
}

static void wyrm_body(const Enemy *e) {
    Room *r = &G.room;
    V2 lead = e->pos;
    for (int i = 0; i < r->boss_nseg; i++) {
        V2 d = v2_sub(r->boss_seg[i], lead);
        float l = v2_len(d);
        if (l > WYRM_SPACING) r->boss_seg[i] = v2_add(lead, v2_scale(d, WYRM_SPACING / l));
        lead = r->boss_seg[i];
    }
}

static void update_wyrm(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    Room *r = &G.room;
    const SDL_FColor spit = col_hex(0xb6ff6a, 1), rib = col_hex(0x7dff9a, 1), dirt = col_hex(0xd8c08a, 1);
    if (e->t < DT * 2.0f && r->boss_nseg == 0) { /* laid out straight behind the head */
        r->boss_nseg = WYRM_SEGS;
        for (int i = 0; i < WYRM_SEGS; i++) r->boss_seg[i] = v2_sub(e->pos, v2_scale(v2_angle(e->angle), WYRM_SPACING * (float)(i + 1)));
        e->state_t = frand(3.5f, 5.0f);
        e->dodge_cd = 2.0f;
    }
    float speed = d->speed * (e->phase >= 2 ? 1.15f : 1.0f);
    V2 ship = G.pl.pos;

    switch (e->state) {
    case WY_SLITHER:
        if (v2_dist2(e->pos, e->target) < 60.0f * 60.0f || (G.ticks + e->uid) % 300 == 0) {
            V2 t = v2_add(ship, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(180.0f, 420.0f)));
            e->target = v2(clampf(t.x, ARENA_X0 + 140.0f, ARENA_X1 - 140.0f), clampf(t.y, ARENA_Y0 + 140.0f, ARENA_Y1 - 140.0f));
        }
        /* it rears up to spit (slowed down a moment: the time to hit its head) */
        if (e->aux.y > 0.0f) e->aux.y -= DT;
        wyrm_turn_to(e, v2_add(e->target, v2_scale(v2_perp(v2_angle(e->angle)), 60.0f * sinf(e->t * 3.0f))),
                     e->aux.y > 0.0f ? speed * 0.3f : speed, 2.6f);
        if (e->fire_t <= 0.0f) { /* spit */
            e->aux.y = WYRM_REAR_T;
            enemy_fire_fan(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), aim, 5, 0.7f, 300.0f, 8.0f, spit);
            if (e->phase >= 1) enemy_fire_fan(e->pos, aim, 4, 0.5f, 220.0f, 8.0f, spit);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = e->phase >= 1 ? 0.95f : 1.25f;
        }
        if ((e->dodge_cd -= DT) <= 0.0f) { /* the ribs fire sideways, all along the body */
            for (int i = 1; i < r->boss_nseg; i += 3) {
                V2 dir = v2_sub(r->boss_seg[i - 1], r->boss_seg[i]);
                float a = v2_heading(dir) + PI_F * 0.5f;
                ebullet_spawn(r->boss_seg[i], a, 160.0f, 7.0f, rib);
                ebullet_spawn(r->boss_seg[i], a + PI_F, 160.0f, 7.0f, rib);
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->dodge_cd = e->phase >= 1 ? 2.8f : 3.6f;
        }
        if (e->state_t <= 0.0f) {
            if (e->phase >= 1 && chance(0.4f)) {
                e->state = WY_COIL;
                e->state_t = 2.6f;
                e->aux = v2(v2_heading(v2_sub(e->pos, ship)), 0.0f);
                e->fire_t = 0.0f;
            } else {
                e->state = WY_BURROW;
                e->state_t = 1.3f;
                sfx_play_at(SFX_THUD, e->pos);
                fx_burst(e->pos, 18, 80.0f, 300.0f, 0.4f, 3.0f, dirt);
            }
        }
        break;

    case WY_BURROW: /* underground, fast, towards the ship */
        wyrm_turn_to(e, ship, speed * 1.6f, 4.0f);
        if ((G.ticks & 3) == 0) fx_particle(e->pos, v2(frand(-40, 40), frand(-40, 40)), 0.4f, 5.0f, col_a(dirt, 0.7f), 0.0f, PT_SPARK);
        if (e->state_t <= 0.0f) {
            e->state = WY_RISE;
            e->state_t = WYRM_RISE_T;
            e->aux = v2(clampf(ship.x, ARENA_X0 + 80.0f, ARENA_X1 - 80.0f), clampf(ship.y, ARENA_Y0 + 80.0f, ARENA_Y1 - 80.0f));
            sfx_play_at(SFX_BEEP, e->aux);
        }
        break;

    case WY_RISE: /* the mound stops under where the ship was: a ring warns, then it bursts out */
        e->vel = v2_scale(v2_sub(e->aux, e->pos), minf(1.0f / DT, 8.0f));
        if (e->state_t <= 0.0f) {
            e->vel = v2(0, 0);
            enemy_fire_ring(e->pos, 24, 230.0f, 9.0f, frand(0.0f, TAU_F), spit);
            enemy_fire_ring(e->pos, 12, 140.0f, 10.0f, frand(0.0f, TAU_F), rib);
            if (e->phase >= 2) enemy_fire_ring(e->pos, 18, 320.0f, 7.0f, frand(0.0f, TAU_F), spit);
            fx_particle(e->pos, v2(0, 0), 0.4f, 200.0f, dirt, 0.0f, PT_RING);
            fx_burst(e->pos, 30, 100.0f, 500.0f, 0.5f, 4.0f, dirt);
            fx_shake(0.3f);
            sfx_play_at(SFX_EXPLODE_BIG, e->pos);
            e->state = WY_SLITHER;
            e->state_t = frand(5.0f, 7.0f);
            e->angle = v2_heading(v2_sub(ship, e->pos)) + PI_F; /* comes out heading away */
            e->target = e->pos;
            e->aux = v2(0, 0);
        }
        break;

    case WY_COIL: { /* circles the ship, the tail dropping still bullets: a closing ring */
        e->aux.x += 1.25f * DT * (float)e->spin_dir;
        V2 want = v2_add(ship, v2_scale(v2_angle(e->aux.x), 270.0f));
        wyrm_turn_to(e, want, speed * 1.35f, 4.5f);
        if (e->fire_t <= 0.0f && r->boss_nseg > 0) {
            EBullet *b = ebullet_spawn(r->boss_seg[r->boss_nseg - 1], 0.0f, 0.0f, 8.0f, rib);
            if (b) b->life = 2.2f;
            e->fire_t = 0.09f;
        }
        if (e->state_t <= 0.0f) {
            e->state = WY_SLITHER;
            e->state_t = frand(4.5f, 6.0f);
            e->fire_t = 0.6f;
            e->aux = v2(0, 0);
        }
    } break;

    default: e->state = WY_SLITHER; break;
    }
    wyrm_body(e);
}

/* A phase change sheds the tail: 3 segments crawl off as serpents. */
static void wyrm_shed(void) {
    Room *r = &G.room;
    for (int k = 0; k < 3 && r->boss_nseg > 2; k++) {
        V2 p = r->boss_seg[--r->boss_nseg];
        room_collide_circle(&p, ENEMIES[EN_SERPENT].radius, NULL);
        enemy_queue_spawn(EN_SERPENT, p, 0.3f);
        fx_burst(p, 12, 80.0f, 300.0f, 0.3f, 3.0f, col_hex(0x7dff9a, 1));
    }
}

/* ======================================================================== */
/* ---- THE STORMCALLER (room 10) ---------------------------------------- */
/*
 * Floats between four pylons (its room's walls). Every few seconds lightning arcs between some of
 * them: a flickering line warns 0.9 s ahead, then the arc burns for 1.7 s and hurts on contact
 * (one or two arcs in phase 1, up to four in phase 3 - never all six). Meanwhile it throws forked
 * bolts that split, calls lightning strikes (lobbed, a ring where they land) and homing sparks; in
 * phase 3 it ties itself to a pylon with an arc and swings it round the room.
 */
enum { ST_REST, ST_FORK, ST_STRIKE, ST_SPARKS, ST_TETHER };
enum { ARC_TOP, ARC_BOTTOM, ARC_LEFT, ARC_RIGHT, ARC_DIAG1, ARC_DIAG2, ARC_COUNT };
#define STORM_WARN_T 0.9f
#define STORM_LIVE_T 1.7f
#define STORM_ARC_R 14.0f
#define STORM_TETHER_WARN 0.6f

static V2 pylon(int i) { /* top left, top right, bottom left, bottom right (STORM_PIECES in blocks.c) */
    return v2(i & 1 ? ARENA_X1 - 300.0f : ARENA_X0 + 300.0f, i & 2 ? ARENA_Y1 - 180.0f : ARENA_Y0 + 180.0f);
}

static void arc_ends(int a, V2 *p, V2 *q) {
    static const int ENDS[ARC_COUNT][2] = {{0, 1}, {2, 3}, {0, 2}, {1, 3}, {0, 3}, {1, 2}};
    *p = pylon(ENDS[a][0]);
    *q = pylon(ENDS[a][1]);
}

/* Which arcs light up together: one or two in phase 1, up to four in phase 3 (never all six). */
#define A_T (1 << ARC_TOP)
#define A_B (1 << ARC_BOTTOM)
#define A_L (1 << ARC_LEFT)
#define A_R (1 << ARC_RIGHT)
#define A_D1 (1 << ARC_DIAG1)
#define A_D2 (1 << ARC_DIAG2)
static const int STORM_P0[] = {A_T, A_B, A_L, A_R, A_T | A_B, A_L | A_R};
static const int STORM_P1[] = {A_D1, A_D2, A_T | A_B, A_L | A_R, A_T | A_D1, A_B | A_D2, A_L | A_R | A_T};
static const int STORM_P2[] = {A_D1 | A_D2, A_T | A_B | A_L | A_R, A_T | A_B | A_D1, A_L | A_R | A_D2, A_D1 | A_D2 | A_T, A_D1 | A_D2 | A_B, A_T | A_L | A_D2};

static int storm_pattern(int phase) {
    if (phase <= 0) return STORM_P0[irand(0, ARRAY_LEN(STORM_P0) - 1)];
    if (phase == 1) return STORM_P1[irand(0, ARRAY_LEN(STORM_P1) - 1)];
    return STORM_P2[irand(0, ARRAY_LEN(STORM_P2) - 1)];
}

static void update_storm(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    Room *r = &G.room;
    const SDL_FColor bolt = col_hex(0x8fd8ff, 1), spark = col_hex(0xe6f6ff, 1);

    /* The arcs run on their own clock, whatever the attack. */
    if (r->arc_mask == 0) {
        if ((e->dodge_cd -= DT) <= 0.0f && e->t > 1.5f) {
            r->arc_mask = storm_pattern(e->phase);
            r->arc_t = STORM_WARN_T;
            sfx_play_at(SFX_CRACKLE, e->pos);
        }
    } else if (r->arc_t > 0.0f) {
        if ((r->arc_t -= DT) <= 0.0f) {
            r->arc_t = -STORM_LIVE_T;
            sfx_play_at(SFX_SHOT_RAIL, e->pos);
            fx_shake(0.15f);
        }
    } else if ((r->arc_t += DT) >= 0.0f) {
        r->arc_t = 0.0f;
        r->arc_mask = 0;
        e->dodge_cd = e->phase == 0 ? 3.4f : e->phase == 1 ? 2.8f : 2.2f;
    }

    if (e->state == ST_TETHER) { /* swings round the middle, an arc tied to a pylon */
        e->aux.y += 0.75f * DT * (float)e->spin_dir;
        V2 want = v2_add(v2(ARENA_CX, ARENA_CY), v2(cosf(e->aux.y) * 330.0f, sinf(e->aux.y) * 200.0f));
        enemy_steer(e, v2_scale(v2_sub(want, e->pos), 3.0f), 4.0f);
    } else {
        drift(e, ARENA_X0 + 480.0f, ARENA_Y0 + 230.0f, ARENA_X1 - 480.0f, ARENA_Y1 - 230.0f, e->state == ST_REST ? d->speed * 1.4f : d->speed * 0.6f);
    }
    e->angle += 0.8f * DT;

    switch (e->state) {
    case ST_REST:
        if (e->state_t <= 0.0f) {
            int pool[6], n = 0;
            pool[n++] = ST_FORK;
            pool[n++] = ST_STRIKE;
            pool[n++] = ST_SPARKS;
            if (e->phase >= 2) pool[n++] = ST_TETHER;
            switch (pick(e, pool, n)) {
            case ST_FORK: e->state_t = 1.5f; break;
            case ST_STRIKE: e->state_t = 1.3f; break;
            case ST_SPARKS: e->state_t = 1.6f; break;
            default: { /* the pylon nearest to it */
                int best = 0;
                for (int i = 1; i < 4; i++)
                    if (v2_dist2(pylon(i), e->pos) < v2_dist2(pylon(best), e->pos)) best = i;
                e->aux = v2((float)best, v2_heading(v2(e->pos.x - ARENA_CX, (e->pos.y - ARENA_CY) * 1.65f)));
                e->state_t = 3.6f;
                sfx_play_at(SFX_CRACKLE, e->pos);
            } break;
            }
        }
        break;

    case ST_FORK:
        if (e->fire_t <= 0.0f) {
            for (int k = -1; k <= 1; k++) {
                EBullet *b = ebullet_spawn(e->pos, aim + 0.32f * (float)k, 320.0f, 10.0f, bolt);
                if (b) {
                    b->kind = EB_SPLIT;
                    b->t1 = frand(0.5f, 0.75f);
                    b->n = e->phase >= 1 ? 7 : 6;
                }
            }
            sfx_play_at(SFX_SHOT_RAIL, e->pos);
            e->fire_t = 0.5f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
        break;

    case ST_STRIKE:
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            int n = e->phase >= 1 ? 7 : 5;
            for (int k = 0; k < n; k++) {
                V2 to = k == 0 ? G.pl.pos : v2_add(G.pl.pos, v2_scale(v2_angle(TAU_F * (float)k / (float)(n - 1) + e->spin), 160.0f));
                ebullet_lob(e->pos, to, 0.9f, 8, spark);
            }
            sfx_play_at(SFX_LOB, e->pos);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.8f);
        break;

    case ST_SPARKS:
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 8; k++) {
                EBullet *b = ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / 8.0f, 240.0f, 7.0f, spark);
                if (b) {
                    b->kind = EB_HOMING;
                    b->t1 = 1.2f;
                    b->life = 5.0f;
                }
            }
            e->spin += 0.39f;
            sfx_play_at(SFX_SHOT_SEEKER, e->pos);
            e->fire_t = 0.8f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
        break;

    case ST_TETHER:
        if (e->fire_t <= 0.0f) {
            enemy_fire_fan(e->pos, aim, 3, 0.4f, 280.0f, 8.0f, bolt);
            e->fire_t = 0.7f;
        }
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    default: rest(e, 0.5f); break;
    }
}

/* ======================================================================== */
/* ---- THE FORGE (room 20) ---------------------------------------------- */
/*
 * A walking furnace among four anvils. It leaps onto the ship (a ring shows where it lands; in the
 * air it cannot be hurt) and slams down in a ring of bullets and a pool of fire - once, twice, three
 * times in a row by phase. It also rains molten drops (lobbed), pours a line of fire towards the
 * ship (a glowing line shows where 0.6 s before) and throws bouncing magma; enraged, it spins out
 * a spiral of heavy slugs. Fire pools never set it on fire (shots still do).
 */
enum { FG_REST, FG_JUMP, FG_RAIN, FG_FURNACE, FG_MAGMA, FG_SPIN };
#define FORGE_JUMP_T 0.95f
#define FORGE_JUMP_H 240.0f
#define FORGE_FURNACE_WARN 0.6f
#define FORGE_FURNACE_POOLS 6
#define FORGE_FURNACE_STEP 100.0f
#define FORGE_FURNACE_R 60.0f

static V2 forge_furnace_end(const Enemy *e) {
    return v2_add(e->pos, v2_scale(v2_angle(e->spin), e->radius + 60.0f + FORGE_FURNACE_STEP * (float)(FORGE_FURNACE_POOLS - 1)));
}

static void update_forge(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    const SDL_FColor molten = col_hex(0xff7a2e, 1), hot = col_hex(0xffd23d, 1), slag = col_hex(0xff4a1a, 1);
    if (e->state != FG_JUMP) {
        e->alt = 0.0f;
        float dist = v2_dist(e->pos, G.pl.pos);
        V2 to = v2_norm(v2_sub(G.pl.pos, e->pos));
        float sp = e->state == FG_REST ? d->speed : d->speed * 0.35f;
        enemy_steer(e, dist > 260.0f ? v2_scale(to, sp) : v2(0, 0), 3.0f);
        e->angle += wrap_pi(aim - e->angle) * minf(1.0f, 3.0f * DT);
    }
    switch (e->state) {
    case FG_REST:
        if (e->state_t <= 0.0f) {
            int pool[6], n = 0;
            pool[n++] = FG_JUMP;
            pool[n++] = FG_RAIN;
            pool[n++] = FG_FURNACE;
            pool[n++] = FG_MAGMA;
            if (e->phase >= 2) pool[n++] = FG_SPIN;
            switch (pick(e, pool, n)) {
            case FG_JUMP:
                e->dodge_cd = (float)(1 + e->phase); /* slams left */
                e->spin = 0.0f;                      /* on the ground */
                e->fire_t = 0.25f;
                break;
            case FG_RAIN: e->state_t = 1.2f; break;
            case FG_FURNACE:
                e->spin = aim;
                e->dodge_cd = 0.0f;           /* pools poured */
                e->fire_t = FORGE_FURNACE_WARN; /* a glowing line first */
                e->state_t = 2.0f;
                sfx_play_at(SFX_IGNITE, e->pos);
                break;
            case FG_MAGMA: e->state_t = e->phase >= 1 ? 1.3f : 0.7f; break;
            default: e->state_t = 3.0f; break;
            }
        }
        break;

    case FG_JUMP:
        if (e->spin == 0.0f) { /* crouching before the leap */
            e->alt = 0.0f;
            enemy_steer(e, v2(0, 0), 8.0f);
            if (e->fire_t <= 0.0f) {
                V2 to = v2_add(G.pl.pos, v2_scale(G.pl.vel, 0.25f));
                to.x = clampf(to.x, ARENA_X0 + e->radius + 10.0f, ARENA_X1 - e->radius - 10.0f);
                to.y = clampf(to.y, ARENA_Y0 + e->radius + 10.0f, ARENA_Y1 - e->radius - 10.0f);
                room_collide_circle(&to, e->radius, NULL); /* never into an anvil */
                e->target = e->pos;
                e->aux = to;
                e->spin = 1.0f;
                e->state_t = FORGE_JUMP_T;
                sfx_play_at(SFX_LOB, e->pos);
            }
        } else {
            float k = clampf(1.0f - e->state_t / FORGE_JUMP_T, 0.0f, 1.0f);
            e->alt = sinf(PI_F * k) * FORGE_JUMP_H;
            e->vel = v2_scale(v2_sub(v2_lerp(e->target, e->aux, k), e->pos), 1.0f / DT);
            if (e->state_t <= 0.0f) { /* slam */
                e->alt = 0.0f;
                e->vel = v2(0, 0);
                e->pos = e->aux;
                enemy_fire_ring(e->pos, e->phase >= 1 ? 22 : 18, 230.0f, 9.0f, frand(0.0f, TAU_F), molten);
                if (e->phase >= 2) enemy_fire_ring(e->pos, 12, 150.0f, 11.0f, frand(0.0f, TAU_F), hot);
                room_fire_add(e->pos, 90.0f, 2.6f);
                fx_particle(e->pos, v2(0, 0), 0.4f, 260.0f, hot, 0.0f, PT_RING);
                fx_burst(e->pos, 26, 120.0f, 520.0f, 0.45f, 4.0f, molten);
                fx_shake(0.35f);
                sfx_play_at(SFX_THUD, e->pos);
                sfx_play_at(SFX_EXPLODE_BIG, e->pos);
                e->spin = 0.0f;
                e->dodge_cd -= 1.0f;
                if (e->dodge_cd > 0.5f) e->fire_t = 0.45f; /* and again */
                else rest(e, 1.1f);
            }
        }
        break;

    case FG_RAIN:
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            int n = e->phase >= 1 ? 11 : 8;
            for (int k = 0; k < n; k++) {
                V2 to = k == 0 ? G.pl.pos : v2_add(G.pl.pos, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(80.0f, 360.0f)));
                ebullet_lob(e->pos, to, frand(0.9f, 1.4f), 6, molten);
            }
            sfx_play_at(SFX_LOB, e->pos);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.9f);
        break;

    case FG_FURNACE: /* a line of fire poured towards where the ship was */
        enemy_steer(e, v2(0, 0), 8.0f); /* stands still while it pours */
        if (e->fire_t <= 0.0f && e->dodge_cd < (float)FORGE_FURNACE_POOLS) {
            V2 p = v2_add(e->pos, v2_scale(v2_angle(e->spin), e->radius + 60.0f + FORGE_FURNACE_STEP * e->dodge_cd));
            if (p.x > ARENA_X0 + 30.0f && p.x < ARENA_X1 - 30.0f && p.y > ARENA_Y0 + 30.0f && p.y < ARENA_Y1 - 30.0f) {
                room_fire_add(p, FORGE_FURNACE_R, 2.4f);
                enemy_fire_fan(p, e->spin, 2, PI_F, 120.0f, 8.0f, slag); /* sparks thrown sideways */
            }
            e->dodge_cd += 1.0f;
            e->fire_t = 0.1f;
            sfx_play_at(SFX_SHOT_FLAMER, p);
        }
        if (e->state_t <= 0.0f) rest(e, 0.8f);
        break;

    case FG_MAGMA:
        if (e->fire_t <= 0.0f) {
            float off = frand(0.0f, TAU_F);
            for (int k = 0; k < 6; k++) {
                EBullet *b = ebullet_spawn(e->pos, off + TAU_F * (float)k / 6.0f, 260.0f, 14.0f, slag);
                if (b) {
                    b->kind = EB_BOUNCE;
                    b->n = 2;
                    b->life = 6.0f;
                }
            }
            sfx_play_at(SFX_TANK_FIRE, e->pos);
            e->fire_t = 0.6f;
        }
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    case FG_SPIN:
        e->spin += 1.4f * DT * (float)e->spin_dir;
        e->angle = e->spin;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 4; k++) {
                EBullet *b = ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / 4.0f, 130.0f, 10.0f, hot);
                if (b) b->accel = 90.0f;
            }
            e->fire_t = 0.09f;
            sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
        }
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    default: rest(e, 0.5f); break;
    }
}

/* ======================================================================== */
/* ---- THE CARRIER (room 30) -------------------------------------------- */
/*
 * A huge ship ringed by shield plates turning round it (2, then 3, then 3 faster): they stop every
 * shot, aim through the gaps. It launches drones (minions), homing missiles from its flanks,
 * broadsides of fans, flak shells that burst; from phase 2 it locks its main gun on the ship (a line
 * warns 1.1 s ahead) and fires a beam that slowly tracks it.
 */
enum { CA_REST, CA_LAUNCH, CA_MISSILES, CA_BROADSIDE, CA_FLAK, CA_BEAM_AIM, CA_BEAM };
#define CARRIER_PLATE_HALF 0.45f /* half the arc of a plate (rad) */
#define CARRIER_PLATE_IN 14.0f  /* the band of the plates, past the hull */
#define CARRIER_PLATE_OUT 40.0f
#define CARRIER_AIM_T 1.1f

static int carrier_plates(const Enemy *e) { return e->phase == 0 ? 2 : 3; }
static float carrier_plate_angle(const Enemy *e, int k) { return e->aux.x + TAU_F * (float)k / (float)carrier_plates(e); }

static bool carrier_plate_at(const Enemy *e, V2 p, float r) {
    V2 d = v2_sub(p, e->pos);
    float l = v2_len(d);
    if (l < e->radius + CARRIER_PLATE_IN - r || l > e->radius + CARRIER_PLATE_OUT + r) return false;
    float a = v2_heading(d);
    for (int k = 0; k < carrier_plates(e); k++)
        if (fabsf(wrap_pi(a - carrier_plate_angle(e, k))) < CARRIER_PLATE_HALF + r / l) return true;
    return false;
}

static void update_carrier(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    const SDL_FColor hull = col_hex(0x9ab8ff, 1), missile = col_hex(0xff8a5a, 1), flak = col_hex(0xffd23d, 1), beam = col_hex(0x6af0ff, 1);
    float rate = e->phase == 0 ? 0.7f : e->phase == 1 ? 0.9f : 1.35f;
    e->aux.x += rate * DT * (float)e->spin_dir;
    if (e->state != CA_BEAM && e->state != CA_BEAM_AIM) e->angle += wrap_pi(aim - e->angle) * minf(1.0f, 0.8f * DT);
    if (e->state != CA_BEAM) drift(e, ARENA_X0 + 300.0f, ARENA_Y0 + 200.0f, ARENA_X1 - 300.0f, ARENA_Y1 - 200.0f, e->state == CA_REST ? d->speed : d->speed * 0.5f);
    else enemy_steer(e, v2(0, 0), 4.0f);
    V2 fwd = v2_angle(e->angle), side = v2_perp(fwd);
    V2 nose = v2_add(e->pos, v2_scale(fwd, e->radius + 10.0f));

    switch (e->state) {
    case CA_REST:
        if (e->state_t <= 0.0f) {
            int pool[8], n = 0;
            if (minions_alive() < 3) pool[n++] = CA_LAUNCH;
            pool[n++] = CA_MISSILES;
            pool[n++] = CA_BROADSIDE;
            pool[n++] = CA_FLAK;
            if (e->phase >= 1) pool[n++] = CA_BEAM_AIM;
            switch (pick(e, pool, n)) {
            case CA_LAUNCH: e->state_t = 0.9f; break;
            case CA_MISSILES: e->state_t = 1.6f; break;
            case CA_BROADSIDE: e->state_t = 1.7f; break;
            case CA_FLAK: e->state_t = 1.5f; break;
            default:
                e->state_t = CARRIER_AIM_T;
                e->spin = aim;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
                break;
            }
        }
        break;

    case CA_LAUNCH: /* drones from both bays */
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            int n = G.room.depth / BOSS_EVERY; /* 3 at room 30, more beyond */
            if (n < 3) n = 3;
            if (n > BOSS_MAX_MINIONS) n = BOSS_MAX_MINIONS;
            for (int k = 0; k < n && G.nen + G.nsp < MAX_ENEMIES - 4; k++) {
                float s = k & 1 ? 1.0f : -1.0f;
                V2 p = v2_add(v2_add(e->pos, v2_scale(side, s * (e->radius + 60.0f))), v2_scale(fwd, frand(-40.0f, 40.0f)));
                p.x = clampf(p.x, ARENA_X0 + 50.0f, ARENA_X1 - 50.0f);
                p.y = clampf(p.y, ARENA_Y0 + 50.0f, ARENA_Y1 - 50.0f);
                enemy_queue_spawn(k % 3 == 2 ? EN_SHOOTER : EN_SWARM, p, 0.6f);
            }
            sfx_play_at(SFX_ENEMY_APPEAR, e->pos);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.6f);
        break;

    case CA_MISSILES:
        if (e->fire_t <= 0.0f) {
            for (int s = -1; s <= 1; s += 2)
                for (int k = 0; k < 4; k++) {
                    V2 bay = v2_add(e->pos, v2_scale(side, (float)s * e->radius * 0.8f));
                    EBullet *b = ebullet_spawn(bay, e->angle + (float)s * (1.2f + 0.25f * (float)k), 220.0f, 9.0f, missile);
                    if (b) {
                        b->kind = EB_HOMING;
                        b->t1 = 1.9f;
                        b->accel = 90.0f;
                        b->life = 6.0f;
                    }
                }
            sfx_play_at(SFX_SHOT_ROCKET, e->pos);
            e->fire_t = 0.6f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.6f);
        break;

    case CA_BROADSIDE:
        if (e->fire_t <= 0.0f) {
            for (int s = -1; s <= 1; s += 2)
                enemy_fire_fan(v2_add(e->pos, v2_scale(side, (float)s * e->radius * 0.7f)), e->angle + (float)s * PI_F * 0.5f, 7, 0.9f, 290.0f, 8.0f, hull);
            enemy_fire_fan(nose, aim, 5, 0.45f, 340.0f, 8.0f, hull);
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = 0.28f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.6f);
        break;

    case CA_FLAK:
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 3; k++) {
                EBullet *b = ebullet_spawn(nose, aim + frand(-0.45f, 0.45f), 380.0f, 11.0f, flak);
                if (b) {
                    b->kind = EB_SPLIT;
                    b->t1 = frand(0.45f, 0.9f);
                    b->n = 8;
                }
            }
            sfx_play_at(SFX_TANK_FIRE, e->pos);
            e->fire_t = 0.38f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.6f);
        break;

    case CA_BEAM_AIM:
        if (e->state_t > 0.3f) e->spin = aim;
        e->angle += wrap_pi(e->spin - e->angle) * minf(1.0f, 10.0f * DT);
        if (e->state_t <= 0.0f) {
            e->state = CA_BEAM;
            e->state_t = 2.0f;
            e->angle = e->spin;
            sfx_play_at(SFX_SNIPER_FIRE, e->pos);
            fx_shake(0.2f);
        }
        break;

    case CA_BEAM: /* tracks the ship, slowly */
        e->angle += clampf(wrap_pi(aim - e->angle), -0.42f * DT, 0.42f * DT);
        e->spin = e->angle;
        if (e->fire_t <= 0.0f) {
            beam_shot(nose, e->angle, 900.0f, 8.0f, beam);
            beam_shot(nose, e->angle + 0.035f, 900.0f, 7.0f, beam);
            beam_shot(nose, e->angle - 0.035f, 900.0f, 7.0f, beam);
            e->fire_t = 0.022f;
        }
        if ((G.ticks % 24) == 0) sfx_play_at(SFX_SHOT_RAIL, e->pos);
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    default: rest(e, 0.5f); break;
    }
}

/* ======================================================================== */
/* ---- THE SINGULARITY (room 40) ---------------------------------------- */
/*
 * A black hole drifting about the middle: it pulls the ship in all the time (Room.gravity, stronger
 * close up; the ship has to keep pushing away) and bends shots towards itself. It spins out curved
 * spirals, sends in rings that collapse onto it (find the gap), sweeps two or three jets round; from
 * phase 2 it flips its gravity to throw the ship off (with slow rings), and in phase 3 it splits off
 * two small black holes that orbit it, pull too and fire rings.
 */
enum { SG_REST, SG_SPIRAL, SG_COLLAPSE, SG_JETS_WARN, SG_JETS, SG_REVERSE, SG_WELLS };
#define SING_PULL_REF 300.0f
#define SING_JETS_WARN_T 0.9f
#define SING_WELL_ORBIT 300.0f
#define SING_WELL_PULL 70.0f

static int sing_jets(const Enemy *e) { return e->phase >= 2 ? 3 : 2; }
static float sing_jet_angle(const Enemy *e, int k) { return e->spin + TAU_F * (float)k / (float)sing_jets(e); }

static V2 sing_pull(V2 at, V2 centre, float g) {
    V2 d = v2_sub(centre, at);
    float l = v2_len(d);
    if (l < 1.0f) return v2(0, 0);
    return v2_scale(d, g * clampf(SING_PULL_REF / l, 0.45f, 1.7f) / l);
}

static void update_singularity(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    Room *r = &G.room;
    const SDL_FColor violet = col_hex(0xb06dff, 1), pink = col_hex(0xff6df0, 1), white = col_hex(0xe8d8ff, 1), orange = col_hex(0xffa04d, 1);
    drift(e, ARENA_CX - 320.0f, ARENA_CY - 160.0f, ARENA_CX + 320.0f, ARENA_CY + 160.0f, d->speed);
    e->angle += 0.9f * DT;

    /* Gravity: the ship is pulled in (pushed off while reversed), shots bend towards it. */
    r->gravity = e->state == SG_REVERSE ? -220.0f : 115.0f + 15.0f * (float)e->phase;
    if (e->inv_t > 0.0f) r->gravity *= 0.5f;
    Player *pl = &G.pl;
    V2 pull = sing_pull(pl->pos, e->pos, r->gravity);
    if (r->wells_t > 0.0f) {
        r->wells_t -= DT;
        e->aux.x += 0.9f * DT;
        for (int i = 0; i < 2; i++) {
            V2 w = v2_add(e->pos, v2_scale(v2_angle(e->aux.x + PI_F * (float)i), SING_WELL_ORBIT));
            r->wells[i] = v2(clampf(w.x, ARENA_X0 + 40.0f, ARENA_X1 - 40.0f), clampf(w.y, ARENA_Y0 + 40.0f, ARENA_Y1 - 40.0f));
            pull = v2_add(pull, sing_pull(pl->pos, r->wells[i], SING_WELL_PULL));
        }
        if ((e->dodge_cd -= DT) <= 0.0f) {
            for (int i = 0; i < 2; i++) enemy_fire_ring(r->wells[i], 10, 170.0f, 8.0f, frand(0.0f, TAU_F), pink);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->dodge_cd = 1.4f;
        }
    }
    if (G.state == GS_PLAYING && !G.transitioning) {
        pl->pos = v2_add(pl->pos, v2_scale(pull, DT));
        room_collide_circle(&pl->pos, PLAYER_BODY_R, NULL);
    }
    for (int i = 0; i < G.npb; i++) { /* shots curve round it */
        PBullet *b = &G.pb[i];
        V2 to = v2_sub(e->pos, b->pos);
        float l2 = v2_len2(to);
        if (l2 > 460.0f * 460.0f || l2 < 1.0f) continue;
        float l = sqrtf(l2), turn = 2.2f * (1.0f - l / 460.0f) * DT;
        float diff = wrap_pi(v2_heading(to) - v2_heading(b->vel));
        b->vel = v2_rotate(b->vel, clampf(diff, -turn, turn));
    }

    switch (e->state) {
    case SG_REST:
        if (e->state_t <= 0.0f) {
            int pool[8], n = 0;
            pool[n++] = SG_SPIRAL;
            pool[n++] = SG_COLLAPSE;
            pool[n++] = SG_JETS_WARN;
            if (e->phase >= 1) pool[n++] = SG_REVERSE;
            if (e->phase >= 2 && r->wells_t <= 0.0f) pool[n++] = SG_WELLS;
            switch (pick(e, pool, n)) {
            case SG_SPIRAL: e->state_t = 3.2f; break;
            case SG_COLLAPSE: e->state_t = 2.6f; break;
            case SG_JETS_WARN:
                e->spin = aim - 0.6f * (float)e->spin_dir;
                e->state_t = SING_JETS_WARN_T;
                sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
                break;
            case SG_REVERSE:
                e->state_t = 2.4f;
                sfx_play_at(SFX_TELEPORT, e->pos);
                fx_particle(e->pos, v2(0, 0), 0.5f, 500.0f, orange, 0.0f, PT_RING);
                break;
            default: e->state_t = 0.8f; break;
            }
        }
        break;

    case SG_SPIRAL: /* arms that curl as they go */
        e->spin += 1.1f * DT * (float)e->spin_dir;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < 5; k++) {
                EBullet *b = ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / 5.0f, 230.0f, 8.0f, k & 1 ? violet : white);
                if (b) {
                    b->curve = 0.55f * (float)e->spin_dir;
                    b->accel = -35.0f;
                    b->life = 7.0f;
                }
            }
            e->fire_t = 0.1f;
            sfx_play_at(SFX_ENEMY_SPIRAL, e->pos);
        }
        if (e->state_t <= 0.0f) {
            e->spin_dir = -e->spin_dir;
            rest(e, 0.9f);
        }
        break;

    case SG_COLLAPSE: /* rings closing in on it from afar, a gap in each */
        if (e->fire_t <= 0.0f) {
            const int n = 44, gap = 6;
            const float R = 640.0f, speed = 180.0f;
            int start = irand(0, n - 1);
            float off = frand(0.0f, TAU_F);
            for (int k = 0; k < n; k++) {
                if ((k - start + n) % n < gap) continue;
                float a = off + TAU_F * (float)k / (float)n;
                V2 p = v2_add(e->pos, v2_scale(v2_angle(a), R));
                if (p.x < ARENA_X0 + 12.0f || p.x > ARENA_X1 - 12.0f || p.y < ARENA_Y0 + 12.0f || p.y > ARENA_Y1 - 12.0f) continue;
                if (v2_dist2(p, pl->pos) < 120.0f * 120.0f) continue; /* never right on the ship */
                EBullet *b = ebullet_spawn(p, a + PI_F, speed, 9.0f, pink);
                if (b) b->life = (R - e->radius * 0.5f) / speed;
            }
            fx_particle(e->pos, v2(0, 0), 0.5f, R, col_a(pink, 0.5f), 0.0f, PT_RING);
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 0.8f;
        }
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    case SG_JETS_WARN:
        if (e->state_t <= 0.0f) {
            e->state = SG_JETS;
            e->state_t = 3.2f;
            sfx_play_at(SFX_SNIPER_FIRE, e->pos);
        }
        break;

    case SG_JETS:
        e->spin += 0.5f * DT * (float)e->spin_dir;
        if (e->fire_t <= 0.0f) {
            for (int k = 0; k < sing_jets(e); k++) {
                float a = sing_jet_angle(e, k);
                beam_shot(v2_add(e->pos, v2_scale(v2_angle(a), e->radius * 0.6f)), a, 700.0f, 8.0f, white);
            }
            e->fire_t = 0.03f;
        }
        if ((G.ticks % 24) == 0) sfx_play_at(SFX_SHOT_RAIL, e->pos);
        if (e->state_t <= 0.0f) {
            e->spin_dir = -e->spin_dir;
            rest(e, 1.0f);
        }
        break;

    case SG_REVERSE: /* thrown off, slow rings on the way */
        if (e->fire_t <= 0.0f) {
            enemy_fire_ring(e->pos, 24, 140.0f, 9.0f, frand(0.0f, TAU_F), orange);
            e->fire_t = 0.6f;
        }
        if (e->state_t <= 0.0f) rest(e, 1.0f);
        break;

    case SG_WELLS:
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            r->wells_t = 7.0f;
            e->aux.x = aim + PI_F * 0.5f;
            e->dodge_cd = 1.0f;
            for (int i = 0; i < 2; i++) r->wells[i] = e->pos;
            fx_particle(e->pos, v2(0, 0), 0.5f, 340.0f, violet, 0.0f, PT_RING);
            sfx_play_at(SFX_TELEPORT, e->pos);
            e->fire_t = -2.0f;
        }
        if (e->state_t <= 0.0f) rest(e, 0.6f);
        break;

    default: rest(e, 0.5f); break;
    }
}

_Static_assert((int)LH_WARN == (int)LH_STATE_WARN && (int)WY_BURROW == (int)WY_STATE_BURROW && (int)FG_JUMP == (int)FG_STATE_JUMP &&
                   (int)SG_REVERSE == (int)SG_STATE_REVERSE && (int)BA_REST == (int)BOSS_STATE_REST,
               "boss states seen by the tests (game.h)");

/* ======================================================================== */
/* ---- Dispatch ---------------------------------------------------------- */

static const EnemyType ROOM10[] = {EN_BOSS, EN_LIGHTHOUSE, EN_WYRM, EN_STORM};

EnemyType boss_for_depth(int depth) {
    if (type_is_boss(g_boss_override)) return (EnemyType)g_boss_override;
    switch (depth / BOSS_EVERY) {
    case 0:
    case 1: return ROOM10[irand(0, ARRAY_LEN(ROOM10) - 1)];
    case 2: return EN_FORGE;
    case 3: return EN_CARRIER;
    case 4: return EN_SINGULARITY;
    default: return (EnemyType)(BOSS_FIRST + irand(0, BOSS_COUNT - 1));
    }
}

const char *boss_title(EnemyType t) {
    switch (t) {
    case EN_LIGHTHOUSE: return "THE LIGHTHOUSE";
    case EN_WYRM: return "THE WYRM";
    case EN_STORM: return "THE STORMCALLER";
    case EN_FORGE: return "THE FORGE";
    case EN_CARRIER: return "THE CARRIER";
    case EN_SINGULARITY: return "THE SINGULARITY";
    default: return "THE OVERSEER";
    }
}

/* Where it shows up: the middle for those built round it, otherwise the far side from the ship. */
V2 boss_spawn_point(EnemyType t) {
    if (t == EN_LIGHTHOUSE || t == EN_FORGE || t == EN_SINGULARITY) return v2(ARENA_CX, ARENA_CY);
    switch (G.room.entry) {
    case SIDE_TOP: return v2(ARENA_CX, ARENA_Y1 - 230.0f);
    case SIDE_LEFT: return v2(ARENA_X1 - 380.0f, ARENA_CY);
    case SIDE_RIGHT: return v2(ARENA_X0 + 380.0f, ARENA_CY);
    default: return v2(ARENA_CX, ARENA_Y0 + 230.0f);
    }
}

void boss_update(Enemy *e, float aim) {
    /* Enraged: the last phase runs every timer faster. */
    if (e->phase >= 2) {
        e->fire_t -= DT * 0.3f;
        e->state_t -= DT * 0.3f;
    }
    if (e->inv_t > 0.0f) e->inv_t -= DT;
    if (phase_change(e)) {
        if (e->type == EN_WYRM) wyrm_shed();
        if (e->type == EN_LIGHTHOUSE) e->pos = e->target, e->vel = v2(0, 0);
        if (e->type == EN_WYRM) wyrm_body(e);
        return;
    }
    switch (e->type) {
    case EN_LIGHTHOUSE: update_lighthouse(e, aim); break;
    case EN_WYRM: update_wyrm(e, aim); break;
    case EN_STORM: update_storm(e, aim); break;
    case EN_FORGE: update_forge(e, aim); break;
    case EN_CARRIER: update_carrier(e, aim); break;
    case EN_SINGULARITY: update_singularity(e, aim); break;
    default: update_overseer(e, aim); break;
    }
    /* Arcs, armoured body...: touching them hurts. */
    BossHazard hz[16];
    int n = boss_hazards(hz, ARRAY_LEN(hz));
    for (int i = 0; i < n; i++)
        if (hz[i].live && seg_dist(G.pl.pos, hz[i].a, hz[i].b) < hz[i].r + G.pl.hit_radius) {
            player_hurt(1);
            break;
        }
}

void boss_on_death(Enemy *e) {
    Room *r = &G.room;
    r->arc_mask = 0;
    r->arc_t = 0.0f;
    r->wells_t = 0.0f;
    r->gravity = 0.0f;
    if (e->type == EN_WYRM) { /* the body breaks up */
        for (int i = 0; i < r->boss_nseg; i++) fx_explosion(r->boss_seg[i], WYRM_SEG_R, col_hex(0x7dff9a, 1));
        r->boss_nseg = 0;
    }
}

bool boss_armor_hit(V2 pos, float r) {
    const Enemy *e = boss_alive();
    if (!e) return false;
    if (e->type == EN_WYRM && !wyrm_buried(e)) {
        for (int i = 0; i < G.room.boss_nseg; i++) {
            float rr = WYRM_SEG_R + r;
            if (v2_dist2(pos, G.room.boss_seg[i]) < rr * rr) return true;
        }
    }
    if (e->type == EN_CARRIER) return carrier_plate_at(e, pos, r);
    return false;
}

/* WYRM: its body lies across the line a-b (the bot's line of fire). The carrier's plates turn
 * too fast to plan round: not counted. */
bool boss_armor_line(V2 a, V2 b, float r) {
    const Enemy *e = boss_alive();
    if (!e || e->type != EN_WYRM || wyrm_buried(e)) return false;
    for (int i = 0; i < G.room.boss_nseg; i++)
        if (seg_dist(G.room.boss_seg[i], a, b) < WYRM_SEG_R + r) return true;
    return false;
}

static int add_hazard(BossHazard *out, int n, int max, V2 a, V2 b, float r, bool live) {
    if (n >= max) return n;
    out[n] = (BossHazard){a, b, r, live};
    return n + 1;
}

/* live: hurts the ship on contact now (checked by boss_update); otherwise a warning, or a beam
 * whose bullets do the hurting. */
int boss_hazards(BossHazard *out, int max) {
    const Enemy *e = boss_alive();
    if (!e) return 0;
    const Room *r = &G.room;
    int n = 0;
    switch (e->type) {
    case EN_STORM:
        if (r->arc_mask)
            for (int a = 0; a < ARC_COUNT; a++)
                if (r->arc_mask & (1 << a)) {
                    V2 p, q;
                    arc_ends(a, &p, &q);
                    n = add_hazard(out, n, max, p, q, STORM_ARC_R, r->arc_t < 0.0f);
                }
        if (e->state == ST_TETHER)
            n = add_hazard(out, n, max, e->pos, pylon((int)e->aux.x), STORM_ARC_R, e->state_t < 3.6f - STORM_TETHER_WARN);
        break;
    case EN_WYRM:
        if (!wyrm_buried(e))
            for (int i = 0; i < r->boss_nseg; i++) n = add_hazard(out, n, max, r->boss_seg[i], r->boss_seg[i], WYRM_SEG_R, true);
        if (e->state == WY_RISE) n = add_hazard(out, n, max, e->aux, e->aux, WYRM_RISE_R, false);
        break;
    case EN_LIGHTHOUSE:
        if (e->state == LH_WARN || e->state == LH_BEAM)
            for (int k = 0; k < lh_beams(e); k++) n = add_hazard(out, n, max, e->pos, ray_end(e->pos, lh_beam_angle(e, k)), 12.0f, false);
        break;
    case EN_FORGE:
        if (e->state == FG_JUMP && e->spin != 0.0f) n = add_hazard(out, n, max, e->aux, e->aux, e->radius + 40.0f, false);
        if (e->state == FG_FURNACE && e->dodge_cd < (float)FORGE_FURNACE_POOLS)
            n = add_hazard(out, n, max, e->pos, forge_furnace_end(e), FORGE_FURNACE_R, false);
        break;
    case EN_CARRIER:
        if (e->state == CA_BEAM_AIM || e->state == CA_BEAM) {
            V2 nose = v2_add(e->pos, v2_scale(v2_angle(e->spin), e->radius));
            n = add_hazard(out, n, max, nose, ray_end(nose, e->spin), 22.0f, false);
        }
        break;
    case EN_SINGULARITY:
        if (e->state == SG_JETS_WARN || e->state == SG_JETS)
            for (int k = 0; k < sing_jets(e); k++) n = add_hazard(out, n, max, e->pos, ray_end(e->pos, sing_jet_angle(e, k)), 14.0f, false);
        if (r->gravity > 0.0f) n = add_hazard(out, n, max, e->pos, e->pos, e->radius + 150.0f, false); /* too close: no way out of the pull */
        if (r->wells_t > 0.0f)
            for (int i = 0; i < 2; i++) n = add_hazard(out, n, max, r->wells[i], r->wells[i], 60.0f, false);
        break;
    case EN_BOSS:
        if (e->state == BA_CHARGE_AIM || e->state == BA_CHARGE) {
            V2 a = e->pos;
            n = add_hazard(out, n, max, a, v2_add(a, v2_scale(v2_angle(e->angle), 900.0f)), e->radius + 10.0f, false);
        }
        break;
    default: break;
    }
    return n;
}

/* LIGHTHOUSE: while its beams are out, the nearest free spot in the shadow of a wall (bot). */
bool boss_cover_spot(V2 from, V2 *out) {
    const Enemy *e = boss_alive();
    if (!e || e->type != EN_LIGHTHOUSE || (e->state != LH_WARN && e->state != LH_BEAM)) return false;
    /* only when a beam is going to sweep over `from` (a beam turns about 95 degrees) */
    float rate = lh_beam_rate(e), left = (e->state == LH_WARN ? LH_BEAM_T : e->state_t) * rate, ang = v2_heading(v2_sub(from, e->pos));
    bool threatened = false;
    for (int k = 0; k < lh_beams(e) && !threatened; k++) {
        float d = wrap_pi(ang - lh_beam_angle(e, k)) * (float)e->spin_dir;
        threatened = e->phase >= 2 ? fabsf(d) < LH_BEAM_T * rate * 0.5f + 0.3f /* it turns back half-way */
                                   : d > -0.3f && d < left + 0.3f;
    }
    if (!threatened) return false;
    bool found = false;
    float best = 1e30f;
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if (o->kind != OB_WALL) continue;
        V2 c = v2(o->r.x + o->r.w * 0.5f, o->r.y + o->r.h * 0.5f);
        V2 away = v2_norm(v2_sub(c, e->pos));
        float half = 0.5f * sqrtf(o->r.w * o->r.w + o->r.h * o->r.h);
        for (int s = -1; s <= 1; s++) { /* straight behind it, and a little to each side */
            V2 dir = v2_rotate(away, 0.18f * (float)s);
            V2 p = v2_add(c, v2_scale(dir, half + 40.0f));
            V2 q = p;
            if (room_collide_circle(&q, PLAYER_BODY_R + 6.0f, NULL)) continue;
            if (p.x < ARENA_X0 + 30.0f || p.x > ARENA_X1 - 30.0f || p.y < ARENA_Y0 + 30.0f || p.y > ARENA_Y1 - 30.0f) continue;
            if (room_line_clear_walls(e->pos, p, 16.0f)) continue; /* not in the shadow */
            float d2 = v2_dist2(p, from);
            if (d2 < best) {
                best = d2;
                *out = p;
                found = true;
            }
        }
    }
    return found;
}

/* ======================================================================== */
/* ---- Drawing ----------------------------------------------------------- */

bool boss_draws_body(const Enemy *e) {
    return e->type == EN_LIGHTHOUSE || e->type == EN_WYRM || e->type == EN_CARRIER || e->type == EN_SINGULARITY;
}

/* A jagged bolt from a to b (no randomness: the shape follows the clock). */
static void draw_bolt(V2 a, V2 b, float thick, SDL_FColor col, int seed) {
    V2 d = v2_sub(b, a);
    float len = v2_len(d);
    if (len < 1.0f) return;
    V2 side = v2_scale(v2_perp(d), 1.0f / len);
    int steps = (int)(len / 40.0f) + 2;
    V2 prev = a;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float j = i == steps ? 0.0f : 16.0f * sinf((float)(G.ticks / 3) * 1.7f + (float)(i * 7 + seed * 13)) * sinf((float)i * 2.3f + (float)seed);
        V2 p = v2_add(v2_add(a, v2_scale(d, t)), v2_scale(side, j));
        draw_line(prev, p, thick, col);
        prev = p;
    }
}

static void draw_warn_line(V2 a, V2 b, float k, SDL_FColor col) {
    bool blink = (G.ticks / 6) & 1;
    draw_line(a, b, 1.5f + 4.0f * k, col_a(col, (blink ? 0.25f : 0.15f) + 0.35f * k));
}

void boss_draw_under(const Enemy *e) {
    const Room *r = &G.room;
    if (e->inv_t > 0.0f) draw_ring(e->pos, e->radius * 1.45f, 6.0f, col_hex(0xffd23d, 0.3f + 0.3f * sinf(G.time * 30.0f)));
    switch (e->type) {
    case EN_BOSS:
        if (e->state == BA_CHARGE_AIM) {
            float k = 1.0f - e->state_t / BOSS_CHARGE_AIM_T;
            bool locked = e->state_t <= 0.25f;
            V2 dir = v2_angle(e->angle);
            draw_line(e->pos, v2_add(e->pos, v2_scale(dir, 2400.0f)), e->radius * 2.0f * (0.3f + 0.7f * k),
                      locked ? col_hex(0xffffff, 0.25f) : col_hex(0xff3df0, 0.08f + 0.2f * k));
        }
        break;

    case EN_LIGHTHOUSE:
        if (e->state == LH_WARN || e->state == LH_BEAM)
            for (int k = 0; k < lh_beams(e); k++) {
                V2 end = ray_end(e->pos, lh_beam_angle(e, k));
                if (e->state == LH_WARN) draw_warn_line(e->pos, end, 1.0f - e->state_t / LH_WARN_T, col_hex(0xfff2a0, 1));
                else {
                    draw_line(e->pos, end, 46.0f, col_hex(0xfff2a0, 0.07f));
                    draw_line(e->pos, end, 18.0f, col_hex(0xfff2a0, 0.12f));
                }
            }
        break;

    case EN_WYRM: { /* the body (alpha), under everything */
        bool buried = wyrm_buried(e);
        draw_blend_alpha();
        for (int i = r->boss_nseg - 1; i >= 0; i--) {
            V2 p = r->boss_seg[i];
            float s = 1.0f - 0.35f * (float)i / (float)WYRM_SEGS;
            if (buried) {
                draw_circle(p, WYRM_SEG_R * s * 0.9f, col_hex(0x3a2e1c, 0.75f));
                draw_ring(p, WYRM_SEG_R * s * 0.9f, 2.0f, col_hex(0xd8c08a, 0.35f));
                continue;
            }
            V2 dir = v2_norm(v2_sub(i == 0 ? e->pos : r->boss_seg[i - 1], p));
            float a = v2_heading(dir);
            SDL_FColor c = col_hex(0x7dff9a, 1);
            draw_ngon(p, WYRM_SEG_R * s, 6, a, col_hex(0x12301c, 1));
            draw_ngon_outline(p, WYRM_SEG_R * s, 6, a, 3.0f, c);
            draw_ngon_outline(p, WYRM_SEG_R * s * 0.55f, 6, a, 2.0f, col_a(c, 0.5f));
            V2 sd = v2_perp(dir); /* ribs */
            for (int sgn = -1; sgn <= 1; sgn += 2)
                draw_line(v2_add(p, v2_scale(sd, WYRM_SEG_R * s * 0.8f * (float)sgn)), v2_add(p, v2_scale(sd, (WYRM_SEG_R * s + 9.0f) * (float)sgn)), 3.0f, col_a(c, 0.7f));
        }
        if (e->state == WY_RISE) { /* where it bursts out */
            float k = 1.0f - e->state_t / WYRM_RISE_T;
            draw_circle(e->aux, WYRM_RISE_R * (0.4f + 0.6f * k), col_hex(0x3a2e1c, 0.35f + 0.3f * k));
        }
        draw_blend_add();
        if (e->state == WY_RISE) {
            float k = 1.0f - e->state_t / WYRM_RISE_T;
            draw_ring(e->aux, WYRM_RISE_R * (2.0f - k), 3.0f, col_hex(0xb6ff6a, 0.3f + 0.5f * k));
        }
    } break;

    case EN_STORM: {
        for (int i = 0; i < 4; i++) { /* the pylons' coils */
            float pulse = 0.5f + 0.5f * sinf(G.time * 6.0f + (float)i);
            draw_glow(pylon(i), 90.0f, col_hex(0x8fd8ff, 0.15f + 0.15f * pulse));
        }
        if (r->arc_mask)
            for (int a = 0; a < ARC_COUNT; a++) {
                if (!(r->arc_mask & (1 << a))) continue;
                V2 p, q;
                arc_ends(a, &p, &q);
                if (r->arc_t > 0.0f) draw_warn_line(p, q, 1.0f - r->arc_t / STORM_WARN_T, col_hex(0x8fd8ff, 1));
                else {
                    draw_line(p, q, 34.0f, col_hex(0x8fd8ff, 0.1f));
                    draw_bolt(p, q, 7.0f, col_hex(0x8fd8ff, 0.7f), a);
                    draw_bolt(p, q, 3.0f, col_hex(0xffffff, 0.95f), a + 11);
                }
            }
        if (e->state == ST_TETHER) {
            V2 q = pylon((int)e->aux.x);
            if (e->state_t >= 3.6f - STORM_TETHER_WARN) draw_warn_line(e->pos, q, 0.6f, col_hex(0x8fd8ff, 1));
            else {
                draw_bolt(e->pos, q, 7.0f, col_hex(0x8fd8ff, 0.7f), 31);
                draw_bolt(e->pos, q, 3.0f, col_hex(0xffffff, 0.95f), 47);
            }
        }
    } break;

    case EN_FORGE:
        draw_glow(e->pos, e->radius * 2.6f, col_hex(0xff5a10, 0.18f)); /* heat on the floor */
        if (e->state == FG_FURNACE && e->dodge_cd < 0.5f) { /* where the fire will run */
            float k = clampf(1.0f - e->fire_t / FORGE_FURNACE_WARN, 0.0f, 1.0f);
            draw_line(e->pos, forge_furnace_end(e), FORGE_FURNACE_R * 2.0f * (0.3f + 0.7f * k), col_hex(0xff5a10, 0.08f + 0.17f * k));
            draw_warn_line(e->pos, forge_furnace_end(e), k, col_hex(0xffb03d, 1));
        }
        if (e->state == FG_JUMP && e->spin != 0.0f) { /* where it will land */
            float k = clampf(1.0f - e->state_t / FORGE_JUMP_T, 0.0f, 1.0f);
            draw_ring(e->aux, (e->radius + 40.0f) * (2.0f - k), 4.0f, col_hex(0xff7a2e, 0.3f + 0.5f * k));
            draw_circle(e->aux, (e->radius + 40.0f) * k, col_hex(0xff3a10, 0.12f + 0.15f * k));
        }
        break;

    case EN_CARRIER:
        if (e->state == CA_BEAM_AIM || e->state == CA_BEAM) {
            V2 nose = v2_add(e->pos, v2_scale(v2_angle(e->spin), e->radius));
            V2 end = ray_end(nose, e->spin);
            if (e->state == CA_BEAM_AIM) {
                float k = 1.0f - e->state_t / CARRIER_AIM_T;
                bool locked = e->state_t <= 0.3f;
                draw_line(nose, end, 2.0f + 10.0f * k, locked ? col_hex(0xffffff, 0.5f) : col_hex(0x6af0ff, 0.15f + 0.35f * k));
            } else {
                draw_line(nose, end, 60.0f, col_hex(0x6af0ff, 0.07f));
                draw_line(nose, end, 24.0f, col_hex(0x6af0ff, 0.15f));
            }
        }
        break;

    case EN_SINGULARITY: {
        /* the pull: rings falling in (or thrown out when reversed) */
        bool rev = r->gravity < 0.0f;
        for (int k = 0; k < 4; k++) {
            float t = fmodf(G.time * 0.6f + (float)k * 0.25f, 1.0f);
            float rad = rev ? e->radius + 420.0f * t : e->radius + 420.0f * (1.0f - t);
            draw_ring(e->pos, rad, 2.0f, col_hex(rev ? 0xffa04d : 0xb06dff, 0.12f * sinf(PI_F * t)));
        }
        draw_glow(e->pos, e->radius * 5.0f, col_hex(0x6a2bff, 0.18f));
        if (e->state == SG_JETS_WARN || e->state == SG_JETS)
            for (int k = 0; k < sing_jets(e); k++) {
                V2 end = ray_end(e->pos, sing_jet_angle(e, k));
                if (e->state == SG_JETS_WARN) draw_warn_line(e->pos, end, 1.0f - e->state_t / SING_JETS_WARN_T, col_hex(0xe8d8ff, 1));
                else draw_line(e->pos, end, 36.0f, col_hex(0xe8d8ff, 0.09f));
            }
        if (r->wells_t > 0.0f)
            for (int i = 0; i < 2; i++) {
                draw_glow(r->wells[i], 110.0f, col_hex(0x6a2bff, 0.25f));
                draw_arc(r->wells[i], 34.0f, 3.0f, G.time * 5.0f, G.time * 5.0f + 3.6f, col_hex(0xff6df0, 0.7f));
            }
    } break;

    default: break;
    }
}

static void draw_overseer(const Enemy *e, SDL_FColor c) {
    /* Inner counter-rotating hexagon, orbiting nodes (one per phase left), and an eye that follows the ship. */
    draw_ngon_outline(e->pos, e->radius * 0.68f, 6, -e->angle * 1.7f, 3.0f, c);
    int nodes = BOSS_PHASES - e->phase;
    for (int k = 0; k < nodes * 2; k++) {
        V2 np = v2_add(e->pos, v2_scale(v2_angle(G.time * 1.3f + TAU_F * (float)k / (float)(nodes * 2)), e->radius + 22.0f));
        draw_ngon(np, 8.0f, 4, G.time * 3.0f, c);
    }
    V2 look = v2_sub(G.pl.pos, e->pos);
    float ll = v2_len(look);
    V2 eye = v2_add(e->pos, ll > 1.0f ? v2_scale(look, e->radius * 0.22f / ll) : v2(0, 0));
    SDL_FColor iris = e->phase >= 2 ? col_hex(0xff4040, 1) : col_hex(0xffd23d, 1);
    if (e->flash > 0.0f) iris = COL_WHITE;
    draw_circle(e->pos, e->radius * 0.42f, rgba(0.05f, 0.0f, 0.06f, 1.0f));
    draw_circle(eye, e->radius * 0.2f, iris);
    draw_circle(eye, e->radius * 0.08f, rgba(0, 0, 0, 1));
}

static void draw_lighthouse(const Enemy *e, SDL_FColor c, SDL_FColor fill) {
    /* A tower seen from above: octagonal base, a ring of windows, the lamp turning on top. */
    draw_ngon(e->pos, e->radius, 8, PI_F / 8.0f, fill);
    draw_ngon_outline(e->pos, e->radius, 8, PI_F / 8.0f, 3.0f, c);
    draw_ngon_outline(e->pos, e->radius * 0.72f, 8, PI_F / 8.0f, 2.0f, col_a(c, 0.6f));
    for (int k = 0; k < 8; k++)
        draw_circle(v2_add(e->pos, v2_scale(v2_angle(TAU_F * (float)k / 8.0f), e->radius * 0.86f)), 3.0f, col_a(c, 0.8f));
    bool lit = e->state == LH_WARN || e->state == LH_BEAM;
    int beams = lit ? lh_beams(e) : 1;
    for (int k = 0; k < beams; k++) {
        float a = lit ? lh_beam_angle(e, k) : e->angle;
        V2 dir = v2_angle(a), side = v2_perp(dir);
        V2 lens = v2_add(e->pos, v2_scale(dir, e->radius * 0.35f));
        draw_quad(v2_add(lens, v2_scale(side, 10.0f)), v2_sub(lens, v2_scale(side, 10.0f)), v2_sub(e->pos, v2_scale(side, 6.0f)), v2_add(e->pos, v2_scale(side, 6.0f)), c);
    }
    SDL_FColor lamp = e->flash > 0.0f ? COL_WHITE : col_hex(lit ? 0xffffff : 0xfff2a0, 1);
    draw_circle(e->pos, e->radius * 0.28f, lamp);
    draw_blend_add();
    draw_glow(e->pos, e->radius * (lit ? 2.2f : 1.4f), col_hex(0xfff2a0, lit ? 0.4f : 0.2f));
    draw_blend_alpha();
}

static void draw_wyrm_head(const Enemy *e, SDL_FColor c, SDL_FColor fill) {
    if (wyrm_buried(e)) { /* a mound of earth moving under the floor */
        float w = 1.0f + 0.1f * sinf(G.time * 14.0f);
        draw_circle(e->pos, e->radius * w, col_hex(0x3a2e1c, 0.85f));
        draw_ring(e->pos, e->radius * w, 3.0f, col_hex(0xd8c08a, 0.6f));
        return;
    }
    V2 dir = v2_angle(e->angle), side = v2_perp(dir);
    draw_ngon(e->pos, e->radius, 5, e->angle, fill);
    draw_ngon_outline(e->pos, e->radius, 5, e->angle, 3.0f, c);
    /* jaws, opening and closing */
    float open = 0.3f + 0.2f * sinf(G.time * 8.0f);
    for (int s = -1; s <= 1; s += 2) {
        V2 base = v2_add(e->pos, v2_scale(side, (float)s * e->radius * 0.5f));
        V2 tip = v2_add(base, v2_scale(v2_angle(e->angle + (float)s * open), e->radius * 0.9f));
        draw_line(base, tip, 6.0f, c);
    }
    SDL_FColor eye = e->flash > 0.0f ? COL_WHITE : col_hex(e->phase >= 2 ? 0xff4040 : 0xfff07a, 1);
    for (int s = -1; s <= 1; s += 2) draw_circle(v2_add(v2_add(e->pos, v2_scale(dir, e->radius * 0.2f)), v2_scale(side, (float)s * e->radius * 0.38f)), 5.0f, eye);
}

static void draw_storm(const Enemy *e, SDL_FColor c) {
    /* a storm cloud of orbiting sparks round a bright core */
    for (int k = 0; k < 6; k++) {
        float a = G.time * (1.5f + 0.2f * (float)k) + TAU_F * (float)k / 6.0f;
        V2 p = v2_add(e->pos, v2_scale(v2_angle(a), e->radius + 14.0f + 6.0f * sinf(G.time * 3.0f + (float)k)));
        draw_ngon(p, 6.0f, 3, a * 2.0f, c);
    }
    draw_ngon_outline(e->pos, e->radius * 0.6f, 7, -e->angle * 2.0f, 2.0f, col_a(c, 0.7f));
    SDL_FColor core = e->flash > 0.0f ? COL_WHITE : col_hex(e->phase >= 2 ? 0xffffff : 0xcfefff, 1);
    draw_circle(e->pos, e->radius * 0.28f, core);
    draw_blend_add();
    draw_bolt(v2_add(e->pos, v2(0, -e->radius * 0.3f)), v2_add(e->pos, v2(0, e->radius * 0.3f)), 2.0f, col_hex(0x8fd8ff, 0.8f), e->uid);
    draw_blend_alpha();
}

static void draw_forge(const Enemy *e, SDL_FColor c) {
    V2 at = v2(e->pos.x, e->pos.y - e->alt);
    float s = 1.0f + 0.25f * e->alt / FORGE_JUMP_H;
    /* the furnace mouth glowing in the middle, hammer heads at the corners */
    float heat = 0.6f + 0.4f * sinf(G.time * 5.0f);
    SDL_FColor core = e->flash > 0.0f ? COL_WHITE : col_lerp(col_hex(0xff4a1a, 1), col_hex(0xffe08a, 1), heat);
    draw_ngon(at, e->radius * 0.42f * s, 4, e->angle + PI_F * 0.25f, core);
    for (int k = 0; k < 4; k++) {
        V2 p = v2_add(at, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 4.0f), e->radius * 0.95f * s));
        draw_rect(p.x - 9.0f * s, p.y - 9.0f * s, 18.0f * s, 18.0f * s, c);
    }
    draw_blend_add();
    draw_glow(at, e->radius * 1.2f * s, col_hex(0xff7a2e, 0.35f * heat));
    draw_blend_alpha();
}

static void draw_carrier(const Enemy *e, SDL_FColor c, SDL_FColor fill) {
    V2 f = v2_angle(e->angle), s = v2_perp(f);
    float L = e->radius * 1.15f, W = e->radius * 0.62f;
    /* the hull: a long hexagon, a deck stripe, the bays on the flanks */
    V2 nose = v2_add(e->pos, v2_scale(f, L)), tail = v2_sub(e->pos, v2_scale(f, L * 0.85f));
    V2 fl = v2_add(v2_add(e->pos, v2_scale(f, L * 0.45f)), v2_scale(s, W)), fr = v2_sub(v2_add(e->pos, v2_scale(f, L * 0.45f)), v2_scale(s, W));
    V2 bl = v2_add(v2_sub(e->pos, v2_scale(f, L * 0.6f)), v2_scale(s, W)), br = v2_sub(v2_sub(e->pos, v2_scale(f, L * 0.6f)), v2_scale(s, W));
    draw_tri(nose, fl, fr, fill);
    draw_quad(fl, bl, br, fr, fill);
    draw_tri(bl, tail, br, fill);
    V2 pts[6] = {nose, fl, bl, tail, br, fr};
    for (int i = 0; i < 6; i++) draw_line(pts[i], pts[(i + 1) % 6], 3.0f, c);
    draw_line(v2_add(e->pos, v2_scale(f, L * 0.7f)), v2_sub(e->pos, v2_scale(f, L * 0.6f)), 6.0f, col_a(c, 0.5f));
    bool bays = e->state == CA_LAUNCH || e->state == CA_MISSILES;
    for (int sg = -1; sg <= 1; sg += 2) {
        V2 bay = v2_add(e->pos, v2_scale(s, (float)sg * W * 0.7f));
        draw_rect(bay.x - 8.0f, bay.y - 8.0f, 16.0f, 16.0f, bays ? col_hex(0xff8a5a, 1) : col_a(c, 0.6f));
    }
    SDL_FColor bridge = e->flash > 0.0f ? COL_WHITE : col_hex(e->phase >= 2 ? 0xff4040 : 0x6af0ff, 1);
    draw_circle(v2_add(e->pos, v2_scale(f, L * 0.2f)), 9.0f, bridge);
    /* the shield plates */
    for (int k = 0; k < carrier_plates(e); k++) {
        float a = carrier_plate_angle(e, k);
        float rm = e->radius + (CARRIER_PLATE_IN + CARRIER_PLATE_OUT) * 0.5f;
        draw_arc(e->pos, rm, CARRIER_PLATE_OUT - CARRIER_PLATE_IN, a - CARRIER_PLATE_HALF, a + CARRIER_PLATE_HALF, col_hex(0x9ab8ff, 0.45f));
        draw_arc(e->pos, e->radius + CARRIER_PLATE_OUT, 3.0f, a - CARRIER_PLATE_HALF, a + CARRIER_PLATE_HALF, col_hex(0xe0ecff, 0.95f));
    }
}

static void draw_singularity(const Enemy *e) {
    const Room *r = &G.room;
    bool rev = r->gravity < 0.0f;
    /* the accretion disk round a black disc */
    draw_blend_add();
    for (int k = 0; k < 3; k++) {
        float a = G.time * (2.4f - 0.5f * (float)k) * (float)e->spin_dir + (float)k * 2.1f;
        draw_arc(e->pos, e->radius * (1.15f + 0.22f * (float)k), 5.0f - (float)k, a, a + 4.2f, col_hex(rev ? 0xffa04d : (k & 1 ? 0xff6df0 : 0xb06dff), 0.75f - 0.18f * (float)k));
    }
    draw_blend_alpha();
    draw_circle(e->pos, e->radius, rgba(0.0f, 0.0f, 0.0f, 1.0f));
    SDL_FColor rim = e->flash > 0.0f ? COL_WHITE : col_hex(e->phase >= 2 ? 0xffffff : 0xe8d8ff, 0.9f);
    draw_ring(e->pos, e->radius, 3.0f, rim);
    if (r->wells_t > 0.0f)
        for (int i = 0; i < 2; i++) {
            draw_circle(r->wells[i], 18.0f, rgba(0, 0, 0, 1));
            draw_ring(r->wells[i], 18.0f, 2.0f, col_hex(0xe8d8ff, 0.9f));
        }
}

void boss_draw(const Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    SDL_FColor c = col_hex(d->color, 1), fill = col_mul(c, 0.28f);
    fill.a = 1.0f;
    if (e->flash > 0.0f) {
        c = COL_WHITE;
        fill = COL_WHITE;
    }
    switch (e->type) {
    case EN_LIGHTHOUSE: draw_lighthouse(e, c, fill); break;
    case EN_WYRM: draw_wyrm_head(e, c, fill); break;
    case EN_STORM: draw_storm(e, c); break;
    case EN_FORGE: draw_forge(e, c); break;
    case EN_CARRIER: draw_carrier(e, c, fill); break;
    case EN_SINGULARITY: draw_singularity(e); break;
    default: draw_overseer(e, c); break;
    }
}
