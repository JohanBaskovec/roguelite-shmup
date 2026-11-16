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
    [EN_RAMMER]    = {"rammer",    22.0f,  60.0f, 140.0f, 3,  2, 0xff6b3d, 4},
    [EN_GRENADIER] = {"grenadier", 20.0f,  40.0f, 120.0f, 3,  2, 0x9cff3d, 5},
    [EN_CRAWLER]   = {"crawler",   18.0f,  45.0f, 190.0f, 2,  3, 0x3dffd8, 4},
    [EN_TOP]       = {"top",       20.0f,  70.0f, 420.0f, 4,  3, 0xff3dd0, 4},
    [EN_KAMIKAZE]  = {"kamikaze",  14.0f,  20.0f, 300.0f, 2,  3, 0xffe03d, 3},
    [EN_DODGER]    = {"dodger",    16.0f,  30.0f, 230.0f, 3,  4, 0x8affff, 4},
    [EN_ROOK]      = {"rook",      24.0f,  90.0f, 650.0f, 4,  4, 0xc8c8ff, 4},
    [EN_SPLITTER]  = {"splitter",  30.0f,  80.0f,  80.0f, 4,  5, 0x6dff6d, 9},
    [EN_LEAPER]    = {"leaper",    20.0f,  55.0f,   0.0f, 3,  5, 0x3dff8a, 5},
    [EN_MINELAYER] = {"minelayer", 22.0f,  60.0f, 160.0f, 4,  6, 0xff9a3d, 6},
    [EN_BOOMERANG] = {"boomerang", 20.0f,  50.0f, 130.0f, 3,  6, 0xffc23d, 3},
    [EN_RICOCHET]  = {"ricochet",  20.0f,  45.0f, 140.0f, 3,  7, 0x3db4ff, 4},
    [EN_ORBITER]   = {"orbiter",   18.0f,  40.0f, 260.0f, 3,  7, 0xb07cff, 5},
    [EN_BLINKER]   = {"blinker",   20.0f,  50.0f,   0.0f, 4,  8, 0xff5ad2, 4},
    [EN_SERPENT]   = {"serpent",   16.0f,  70.0f, 270.0f, 4,  8, 0xa0ff3d, 6},
    [EN_MIRROR]    = {"mirror",    24.0f, 110.0f, 100.0f, 5,  9, 0xd8f6ff, 6},
    [EN_LANCER]    = {"lancer",    22.0f,  80.0f, 110.0f, 5, 11, 0xff3d3d, 3},
    [EN_MORTAR]    = {"mortar",    30.0f, 120.0f,  40.0f, 6, 12, 0xffaa55, 8},
    [EN_GUARDIAN]  = {"guardian",  26.0f, 100.0f, 130.0f, 6, 12, 0x6dd2ff, 6},
    [EN_WRAITH]    = {"wraith",    20.0f,  70.0f, 200.0f, 5, 14, 0xc0a8ff, 3},
    /* Boss rooms only (cost / min_depth keep it out of normal waves). */
    [EN_BOSS]    = {"overseer", 64.0f, 4800.0f, 120.0f, 999, 999, 0xff3df0, 6},
    /* Born from a dying splitter only. */
    [EN_SPLITLING] = {"splitling", 13.0f, 18.0f, 280.0f, 999, 999, 0x6dff6d, 5},
};

uint64_t g_only_enemies;

int enemy_by_name(const char *name) {
    for (int t = 0; t < EN_COUNT; t++)
        if (!strcmp(name, ENEMIES[t].name)) return t;
    return -1;
}

/*
 * Every enemy type always has the same attributes (hp, speed, bullet patterns), whatever
 * the depth. Difficulty comes from the rooms instead: more enemies, more waves, and new,
 * tougher types unlocking deeper (min_depth) - see spawn_wave() in room.c.
 */
#define HUNTER_DASH_SPEED 950.0f
#define HIVE_MAX_BROOD 9
#define RAMMER_AIM_T 0.7f
#define RAMMER_SPEED 900.0f
#define KAMIKAZE_FUSE 0.55f
#define LEAPER_JUMP_T 0.8f
#define BLINK_WARN_T 0.5f
#define LANCER_AIM_T 1.0f
#define LANCER_BEAM_T 0.7f
#define MIRROR_ARC 1.0f      /* half-angle of the mirror's front plate */
#define MIRROR_OPEN_T 0.5f   /* the plate opens that long each time it fires */
#define MAX_MINES 24

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
    if (type != EN_CRAWLER) room_collide_circle(&e->pos, e->radius, NULL); /* the crawler finds its own spot on the wall */
    sfx_play_at(SFX_ENEMY_APPEAR, e->pos);
    fx_particle(e->pos, v2(0, 0), 0.25f, e->radius * 3.0f, col_hex(d->color, 1), 0.0f, PT_RING);
}

/* What a nest (OB_NEST) lets out when it breaks: one small enemy of those met at this depth (with
 * --enemy, one of those). Never scaled, like every enemy: the nest only adds one more. */
EnemyType enemy_hatchling(int depth) {
    static const EnemyType SMALL[] = {EN_CHASER, EN_SWARM, EN_KAMIKAZE, EN_DODGER, EN_ORBITER, EN_SERPENT};
    EnemyType cand[EN_COUNT];
    int n = 0;
    if (g_only_enemies) {
        for (int t = 0; t < EN_COUNT; t++)
            if (((g_only_enemies >> t) & 1u) && t != EN_BOSS) cand[n++] = (EnemyType)t;
    } else {
        for (int i = 0; i < ARRAY_LEN(SMALL); i++)
            if (ENEMIES[SMALL[i]].min_depth <= depth) cand[n++] = SMALL[i];
    }
    return n ? cand[irand(0, n - 1)] : EN_CHASER;
}

Enemy *enemy_nearest(V2 pos, float max_dist) {
    Enemy *best = NULL;
    float best_d2 = max_dist * max_dist;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f || enemy_untouchable(e)) continue;
        float d2 = v2_dist2(e->pos, pos);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = e;
        }
    }
    return best;
}

bool is_boss_depth(int depth) { return depth > 0 && depth % BOSS_EVERY == 0; }

float boss_phase_threshold(int phase) { return phase <= 0 ? 1.0f : phase == 1 ? 0.66f : 0.33f; }

const Enemy *boss_alive(void) {
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].type == EN_BOSS && G.en[i].hp > 0.0f) return &G.en[i];
    return NULL;
}

/* The boss shrugs off everything during a phase change, and one huge hit cannot skip a
 * phase: damage stops just under the next threshold (the phase change then triggers). */
float enemy_damage_cap(const Enemy *e, float dmg) {
    if (e->type != EN_BOSS) return dmg;
    if (e->inv_t > 0.0f) return 0.0f;
    if (e->phase + 1 < BOSS_PHASES) {
        float floor_hp = e->max_hp * boss_phase_threshold(e->phase + 1) - 1.0f;
        if (e->hp - dmg < floor_hp) dmg = fmaxf(e->hp - floor_hp, 0.0f);
    }
    return dmg;
}

bool enemy_untouchable(const Enemy *e) {
    if (e->type == EN_WRAITH) return e->state == 0;  /* faded out */
    if (e->type == EN_LEAPER) return e->alt > 20.0f; /* in the air */
    return false;
}

bool enemy_blocks_shot(const Enemy *e, V2 from) {
    if (e->type != EN_MIRROR || e->dodge_cd > 0.0f) return false; /* plate open while it fires */
    V2 d = v2_sub(from, e->pos);
    if (v2_len2(d) < 1.0f) return false;
    return v2_dot(v2_norm(d), v2_angle(e->angle)) > cosf(MIRROR_ARC);
}

/* GUARDIAN: is a guardian (other than e) shielding it? */
static const Enemy *guarded_by(const Enemy *e) {
    if (e->type == EN_GUARDIAN || e->type == EN_BOSS) return NULL;
    for (int i = 0; i < G.nen; i++) {
        const Enemy *g = &G.en[i];
        if (g->type == EN_GUARDIAN && g->hp > 0.0f && v2_dist2(g->pos, e->pos) < GUARDIAN_RANGE * GUARDIAN_RANGE) return g;
    }
    return NULL;
}

void enemy_damage(Enemy *e, float dmg, V2 knock) {
    if (e->hp <= 0.0f || enemy_untouchable(e)) return;
    if (guarded_by(e)) dmg *= GUARDIAN_SHIELD;
    dmg = enemy_damage_cap(e, dmg);
    e->hp -= dmg;
    e->flash = e->type == EN_BOSS && e->inv_t > 0.0f ? 0.0f : 0.07f;
    G.last_enemy_damage_t = G.time;
    float mass = e->radius / 16.0f;
    if (e->type == EN_TANK || e->type == EN_MORTAR) mass *= 2.5f;
    if (e->type == EN_RAMMER && e->state == 2) mass *= 6.0f; /* a charging ram does not stop */
    if (e->type == EN_BOSS) mass *= 12.0f; /* barely budges */
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
static V2 nav_dir(Enemy *e, V2 goal) {
    /* The path is worked out again every few ticks (staggered), or when the waypoint is reached /
     * the goal moved: rooms full of blocks make it costly, and a waypoint stays good for a while. */
    if (!e->nav_ok || (G.ticks + e->uid) % 6 == 0 || v2_dist2(e->pos, e->nav_pt) < 12.0f * 12.0f ||
        v2_dist2(goal, e->nav_goal) > 50.0f * 50.0f) {
        e->nav_pt = room_navigate(e->pos, goal, e->radius + 6.0f);
        e->nav_goal = goal;
        e->nav_ok = true;
    }
    return v2_norm(v2_sub(e->nav_pt, e->pos));
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

/* ---- THE OVERSEER (boss rooms) ------------------------------------------- */
/*
 * Hovers in the upper half and cycles through attacks, with a short rest between two.
 * Phase 1 (100-66%): spiral, aimed fans, rings with a gap.
 * Phase 2 (66-33%):  + telegraphed charge leaving a bullet trail, rotating cross of streams.
 * Phase 3 (33-0%):   everything faster, double counter-rotating spiral, calls in minions.
 * A phase change clears the screen, makes it invulnerable for a moment and sends a shockwave.
 * Like every enemy, it never scales with depth: later bosses only bring more minions.
 */
enum { BA_REST, BA_SPIRAL, BA_FANS, BA_RING, BA_CHARGE_AIM, BA_CHARGE, BA_CROSS, BA_SUMMON };
#define BOSS_CHARGE_SPEED 1100.0f
#define BOSS_CHARGE_AIM_T 0.8f
#define BOSS_INVULN_T 1.4f
#define BOSS_MAX_MINIONS 6

static int boss_minions(void) {
    int n = G.nsp;
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].type != EN_BOSS && G.en[i].hp > 0.0f) n++;
    return n;
}

static void boss_pick_attack(Enemy *e) {
    int pool[8], n = 0;
    pool[n++] = BA_SPIRAL;
    pool[n++] = BA_FANS;
    pool[n++] = BA_RING;
    if (e->phase >= 1) {
        pool[n++] = BA_CHARGE_AIM;
        pool[n++] = BA_CROSS;
    }
    if (e->phase >= 2 && boss_minions() < BOSS_MAX_MINIONS / 2) pool[n++] = BA_SUMMON;
    int a;
    do a = pool[irand(0, n - 1)];
    while (a == e->burst && n > 1); /* never the same attack twice in a row */
    e->burst = a;
    e->state = a;
    e->fire_t = 0.0f;
    e->spin = frand(0.0f, TAU_F);
    switch (a) {
    case BA_SPIRAL: e->state_t = 2.8f; break;
    case BA_FANS: e->state_t = 1.3f; break;
    case BA_RING: e->state_t = 1.7f; break;
    case BA_CHARGE_AIM: e->state_t = BOSS_CHARGE_AIM_T; sfx_play_at(SFX_SNIPER_CHARGE, e->pos); break;
    case BA_CROSS: e->state_t = 3.2f; break;
    default: e->state_t = 0.6f; break;
    }
}

static void update_boss(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    const SDL_FColor c1 = col_hex(0xff3df0, 1), c2 = col_hex(0xff9af5, 1), c3 = col_hex(0xffd23d, 1);
    /* Enraged: the last phase runs every timer faster. */
    if (e->phase >= 2) {
        e->fire_t -= DT * 0.3f;
        e->state_t -= DT * 0.3f;
    }
    if (e->inv_t > 0.0f) e->inv_t -= DT;

    /* Phase change. */
    if (e->phase + 1 < BOSS_PHASES && e->hp < e->max_hp * boss_phase_threshold(e->phase + 1)) {
        e->phase++;
        e->inv_t = BOSS_INVULN_T;
        ebullets_clear(true);
        fire_ring(e->pos, 40, 210.0f, 9.0f, frand(0.0f, TAU_F), c3);
        fx_particle(e->pos, v2(0, 0), 0.6f, 420.0f, c3, 0.0f, PT_RING);
        fx_explosion(e->pos, e->radius, c1);
        fx_shake(0.35f);
        G.hitstop = fmaxf(G.hitstop, 0.12f);
        fx_popup(v2(e->pos.x, e->pos.y - e->radius - 30.0f), c3, e->phase == 2 ? "ENRAGED!" : "PHASE %d", e->phase + 1);
        sfx_play_ex(SFX_EXPLODE_BIG, 1.0f, 0.7f, 0.0f);
        e->state = BA_REST;
        e->state_t = BOSS_INVULN_T;
        return;
    }

    /* Movement: drift between points of the upper half, nearly still while firing. */
    if (e->state != BA_CHARGE) {
        if (v2_dist2(e->pos, e->target) < 40.0f * 40.0f || e->t < DT * 2.0f)
            e->target = v2(frand(ARENA_X0 + 260.0f, ARENA_X1 - 260.0f), frand(ARENA_Y0 + 150.0f, ARENA_CY - 60.0f));
        float sp = e->state == BA_REST ? d->speed * 1.6f : e->state == BA_CHARGE_AIM ? 0.0f : d->speed * 0.5f;
        move_towards(e, v2_scale(v2_norm(v2_sub(e->target, e->pos)), sp), 2.0f);
    }

    switch (e->state) {
    case BA_REST:
        e->angle += 0.6f * DT;
        if (e->state_t <= 0.0f) boss_pick_attack(e);
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
            e->state = BA_REST;
            e->state_t = 0.9f;
        }
    } break;

    case BA_FANS:
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            fire_fan(e->pos, aim, 7, 1.0f, 360.0f, 9.0f, c2);
            if (e->phase >= 1) fire_fan(e->pos, aim, 6, 0.85f, 260.0f, 8.0f, c1); /* a slower fan in the gaps */
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->fire_t = 0.42f;
        }
        if (e->state_t <= 0.0f) {
            e->state = BA_REST;
            e->state_t = 0.8f;
        }
        break;

    case BA_RING:
        e->angle -= 1.2f * DT;
        if (e->fire_t <= 0.0f) {
            /* 32 slots, 4 left empty: the gap drifts each ring, find it and slip through. */
            const int n = 32, gap = 4;
            int start = irand(0, n - 1);
            for (int k = 0; k < n; k++) {
                int slot = (k - start + n) % n;
                if (slot < gap) continue;
                ebullet_spawn(e->pos, e->spin + TAU_F * (float)k / (float)n, 230.0f, 9.0f, c3);
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->fire_t = 0.55f;
        }
        if (e->state_t <= 0.0f) {
            e->state = BA_REST;
            e->state_t = 0.9f;
        }
        break;

    case BA_CHARGE_AIM:
        move_towards(e, v2(0, 0), 8.0f);
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
            fire_ring(e->pos, 20, 260.0f, 9.0f, e->angle, c3);
            sfx_play_at(SFX_TANK_FIRE, e->pos);
            fx_shake(0.2f);
            e->state = BA_REST;
            e->state_t = 1.0f;
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
        if (e->state_t <= 0.0f) {
            e->state = BA_REST;
            e->state_t = 0.9f;
        }
    } break;

    case BA_SUMMON:
        e->angle += 4.0f * DT;
        if (e->fire_t <= 0.0f && e->fire_t > -1.0f) {
            /* Later bosses bring more minions (the minions themselves never change). */
            int n = 2 + G.room.depth / BOSS_EVERY;
            if (n > BOSS_MAX_MINIONS) n = BOSS_MAX_MINIONS;
            for (int k = 0; k < n && G.nen + G.nsp < MAX_ENEMIES - 4; k++) {
                V2 p = v2_add(e->pos, v2_scale(v2_angle(TAU_F * (float)k / (float)n + frand(-0.2f, 0.2f)), e->radius + 70.0f));
                p.x = clampf(p.x, ARENA_X0 + 60.0f, ARENA_X1 - 60.0f);
                p.y = clampf(p.y, ARENA_Y0 + 60.0f, ARENA_Y1 - 60.0f);
                enemy_queue_spawn(k & 1 ? EN_SHOOTER : EN_CHASER, p, 0.8f);
            }
            fx_particle(e->pos, v2(0, 0), 0.5f, 260.0f, c1, 0.0f, PT_RING);
            sfx_play_at(SFX_ENEMY_APPEAR, e->pos);
            e->fire_t = -2.0f; /* once */
        }
        if (e->state_t <= 0.0f) {
            e->state = BA_REST;
            e->state_t = 0.5f;
        }
        break;

    default:
        e->state = BA_REST;
        break;
    }
}

/* ---- The 20 newer types --------------------------------------------------- */
/*
 * Each one plays differently: melee charges, lobbed grenades, wall crawling, bouncing tops,
 * dodging, chess-like slides, splitting, jumping, mines, boomerangs, bank shots, orbits,
 * teleports, bullet trails, a shield plate, a beam, artillery, support, phasing. Shared
 * state lives in Enemy (state, state_t, fire_t, aux, alt...), bullets in EBulletKind.
 */

/* Perimeter of the arena (inset by r), as a loop: s in [0, perimeter). */
static float perim_len(float r) { return 2.0f * ((ARENA_X1 - ARENA_X0 - 2.0f * r) + (ARENA_Y1 - ARENA_Y0 - 2.0f * r)); }

static V2 perim_point(float s, float r, V2 *inward) {
    float w = ARENA_X1 - ARENA_X0 - 2.0f * r, h = ARENA_Y1 - ARENA_Y0 - 2.0f * r;
    float x0 = ARENA_X0 + r, y0 = ARENA_Y0 + r;
    float L = perim_len(r);
    s = fmodf(s, L);
    if (s < 0.0f) s += L;
    if (s < w) { *inward = v2(0, 1); return v2(x0 + s, y0); }                       /* top, left to right */
    s -= w;
    if (s < h) { *inward = v2(-1, 0); return v2(x0 + w, y0 + s); }                  /* right, downwards */
    s -= h;
    if (s < w) { *inward = v2(0, -1); return v2(x0 + w - s, y0 + h); }              /* bottom, right to left */
    s -= w;
    *inward = v2(1, 0);
    return v2(x0, y0 + h - s);                                                        /* left, upwards */
}

/* Closest perimeter coordinate to p. */
static float perim_coord(V2 p, float r) {
    float w = ARENA_X1 - ARENA_X0 - 2.0f * r, h = ARENA_Y1 - ARENA_Y0 - 2.0f * r;
    float x0 = ARENA_X0 + r, y0 = ARENA_Y0 + r;
    float px = clampf(p.x, x0, x0 + w), py = clampf(p.y, y0, y0 + h);
    float dt = p.y - y0, db = y0 + h - p.y, dl = p.x - x0, dr = x0 + w - p.x;
    float m = fminf(fminf(dt, db), fminf(dl, dr));
    if (m == dt) return px - x0;
    if (m == dr) return w + (py - y0);
    if (m == db) return w + h + (x0 + w - px);
    return 2.0f * w + h + (y0 + h - py);
}

/* Moving in a straight line: would it hit something next tick? Returns the normal. */
static bool blocked_ahead(const Enemy *e, V2 dir, float speed, V2 *n) {
    V2 next = v2_add(e->pos, v2_scale(dir, speed * DT + 2.0f));
    V2 nn = v2(0, 0);
    if (!room_collide_circle(&next, e->radius, &nn)) return false;
    if (n) *n = v2_len2(nn) > 0.0f ? v2_norm(nn) : v2_scale(dir, -1.0f);
    return true;
}

static V2 reflect_dir(V2 d, V2 n) { return v2_sub(d, v2_scale(n, 2.0f * v2_dot(d, n))); }

/* Rammer: lines up, paws the ground (telegraph), charges in a straight line until it hits a
 * wall, then sits stunned - the moment to punish it. */
static void update_rammer(Enemy *e, float aim, float dist) {
    const EnemyDef *d = &ENEMIES[e->type];
    switch (e->state) {
    case 0:
        wander(e, 280.0f, 480.0f, d->speed);
        e->angle = aim;
        if (e->fire_t <= 0.0f && dist < 900.0f && room_line_clear(e->pos, G.pl.pos, e->radius)) {
            e->state = 1;
            e->state_t = RAMMER_AIM_T;
            sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
        }
        break;
    case 1:
        move_towards(e, v2(0, 0), 8.0f);
        if (e->state_t > 0.2f) e->angle = aim;
        if ((G.ticks + e->uid) % 6 == 0) /* pawing: dust behind it */
            fx_particle(v2_sub(e->pos, v2_scale(v2_angle(e->angle), e->radius)), v2_scale(v2_angle(e->angle + frand(2.6f, 3.6f)), 120.0f),
                        0.3f, 5.0f, col_hex(d->color, 0.6f), 3.0f, PT_GLOW);
        if (e->state_t <= 0.0f) {
            e->state = 2;
            e->state_t = 1.6f;
            e->aux = v2_angle(e->angle);
            sfx_play_at(SFX_DASH, e->pos);
        }
        break;
    case 2: {
        V2 n = v2_scale(e->aux, -1.0f); /* out of time without hitting anything: just stops */
        e->vel = v2_scale(e->aux, RAMMER_SPEED);
        if ((G.ticks + e->uid) % 2 == 0) fx_particle(e->pos, v2(0, 0), 0.2f, e->radius, col_hex(d->color, 0.4f), 0.0f, PT_GLOW);
        if (blocked_ahead(e, e->aux, RAMMER_SPEED, &n) || e->state_t <= 0.0f) {
            e->vel = v2_scale(n, 120.0f);
            e->state = 3;
            e->state_t = 1.1f;
            fire_ring(e->pos, 8, 170.0f, 7.0f, frand(0.0f, TAU_F), col_hex(0xffa070, 1)); /* debris */
            fx_shake(0.18f);
            sfx_play_at(SFX_TANK_FIRE, e->pos);
        }
    } break;
    default: /* stunned: stars spin over its head */
        move_towards(e, v2(0, 0), 4.0f);
        if (e->state_t <= 0.0f) {
            e->state = 0;
            e->fire_t = frand(1.4f, 2.2f);
        }
        break;
    }
}

/* Grenadier: keeps away and lobs grenades where the ship is heading. */
static void update_grenadier(Enemy *e, float aim) {
    wander(e, 380.0f, 650.0f, ENEMIES[e->type].speed);
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        V2 to = v2_add(G.pl.pos, v2_scale(G.pl.vel, 0.45f));
        ebullet_lob(e->pos, to, 1.0f, 8, col_hex(0xb8ff5a, 1));
        sfx_play_at(SFX_LOB, e->pos);
        e->fire_t = frand(2.2f, 2.8f);
    }
}

/* Crawler: glued to the arena wall, scuttles along it (around corners) to face the ship,
 * then spits short bursts inwards. */
/* A wall jutting out of the arena edge stands in the crawler's way along it. */
static bool crawler_blocked(float s, float r) {
    V2 in, q = perim_point(s, r, &in);
    return room_collide_circle(&q, r, NULL);
}

static void update_crawler(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    float L = perim_len(e->radius);
    if (e->t <= DT * 1.5f) { /* spin = position along the wall, off any wall jutting out of it */
        e->spin = perim_coord(e->pos, e->radius);
        for (float k = 0.0f; k < L * 0.5f && crawler_blocked(e->spin, e->radius); k += 10.0f) {
            if (!crawler_blocked(e->spin + k, e->radius)) e->spin = fmodf(e->spin + k, L);
            else if (!crawler_blocked(e->spin - k, e->radius)) e->spin = fmodf(e->spin - k + L, L);
        }
    }
    /* Each one keeps its own spot around the ship's side, so a pack spreads along the wall. */
    float want = perim_coord(G.pl.pos, e->radius) + (float)((int)(e->uid % 5) - 2) * 90.0f;
    float diff = fmodf(want - e->spin + L * 1.5f, L) - L * 0.5f; /* shortest way round */
    bool firing = e->burst > 0;
    float step = firing ? 0.0f : clampf(diff, -d->speed * DT, d->speed * DT);
    if (step != 0.0f && crawler_blocked(e->spin + step, e->radius) && !crawler_blocked(e->spin, e->radius))
        step = 0.0f; /* waits against a spur (the ship has to come round) */
    e->spin = fmodf(e->spin + step + L, L);
    V2 in;
    e->pos = perim_point(e->spin, e->radius, &in);
    e->aux = in;
    e->vel = v2(0, 0);
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        if (e->burst <= 0) e->burst = 3;
        fire_fan(v2_add(e->pos, v2_scale(in, e->radius)), aim, 2, 0.22f, 380.0f, 7.0f, col_hex(0x7dffe6, 1));
        sfx_play_at(SFX_ENEMY_SHOT, e->pos);
        e->burst--;
        e->fire_t = e->burst > 0 ? 0.14f : frand(1.4f, 1.9f);
    }
}

/* Top: a spinning top. Runs in straight lines, bouncing off everything, faster and faster,
 * flinging bullets from its spin - then gets dizzy, wobbles and slows down for a moment. */
static void update_top(Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    if (e->t <= DT * 1.5f) e->aux = v2_angle(frand(0.0f, TAU_F));
    if (e->state == 0) {
        float k = clampf(1.0f - e->state_t / 6.0f, 0.0f, 1.0f); /* ramps up over a run */
        float speed = d->speed * (0.45f + 0.55f * k);
        e->spin += (8.0f + 18.0f * k) * DT;
        V2 n;
        if (blocked_ahead(e, e->aux, speed, &n)) {
            e->aux = v2_norm(reflect_dir(e->aux, n));
            e->aux = v2_rotate(e->aux, frand(-0.25f, 0.25f)); /* never stuck in a loop */
            fire_ring(e->pos, 6, 200.0f, 7.0f, e->spin, col_hex(0xff7ae0, 1));
            sfx_play_at(SFX_BOUNCE, e->pos);
        }
        e->vel = v2_scale(e->aux, speed);
        if (e->fire_t <= 0.0f) {
            ebullet_spawn(e->pos, e->spin, 170.0f, 6.0f, col_hex(0xff9ae8, 1));
            e->fire_t = 0.28f;
        }
        if (e->state_t <= 0.0f) {
            e->state = 1;
            e->state_t = 1.3f;
        }
    } else { /* dizzy */
        e->spin += 4.0f * DT;
        e->vel = v2_scale(v2_angle(e->t * 9.0f), 60.0f);
        if (e->state_t <= 0.0f) {
            e->state = 0;
            e->state_t = 6.0f;
            e->aux = v2_angle(v2_heading(v2_sub(G.pl.pos, e->pos)) + frand(-0.6f, 0.6f));
        }
    }
    e->angle = e->spin;
}

/* Kamikaze: rushes in, stops next to the ship, swells up and blows into a ring of bullets. */
static void update_kamikaze(Enemy *e, float dist) {
    const EnemyDef *d = &ENEMIES[e->type];
    if (e->state == 0) {
        move_towards(e, v2_scale(nav_dir(e, G.pl.pos), d->speed), 4.0f);
        e->angle = v2_heading(e->vel);
        if (dist < 120.0f) {
            e->state = 1;
            e->state_t = KAMIKAZE_FUSE;
            sfx_play_at(SFX_BEEP, e->pos);
        }
    } else {
        move_towards(e, v2(0, 0), 10.0f);
        if ((G.ticks + e->uid) % 12 == 0) sfx_play_at(SFX_BEEP, e->pos);
        if (e->state_t <= 0.0f) {
            fire_ring(e->pos, 14, 240.0f, 8.0f, frand(0.0f, TAU_F), col_hex(0xffe85a, 1));
            fire_ring(e->pos, 7, 150.0f, 8.0f, frand(0.0f, TAU_F), col_hex(0xffa03d, 1));
            fx_explosion(e->pos, 50.0f, col_hex(0xffe03d, 1));
            e->burst = -1; /* self-destructed: not a kill */
            e->hp = 0.0f;
        }
    }
}

/* Dodger: strafes at mid range and sidesteps shots aimed at it (with a cooldown, so a
 * spread or a second volley gets it). */
static void update_dodger(Enemy *e, V2 dir, float aim, float dist) {
    const EnemyDef *d = &ENEMIES[e->type];
    e->dodge_cd -= DT;
    if (e->state_t > 0.0f && e->state == 1) {
        /* mid-dodge: keep sliding */
        if ((G.ticks + e->uid) % 3 == 0) fx_particle(e->pos, v2(0, 0), 0.25f, e->radius, col_hex(d->color, 0.35f), 0.0f, PT_GLOW);
    } else {
        e->state = 0;
        V2 mv = dist < 330.0f ? v2_scale(dir, -1.0f) : dist > 600.0f ? dir : v2_scale(v2_perp(dir), (float)e->spin_dir * 0.6f);
        if (chance(0.006f)) e->spin_dir = -e->spin_dir;
        move_towards(e, v2_scale(mv, d->speed), 3.0f);
        if (e->dodge_cd <= 0.0f) {
            for (int i = 0; i < G.npb; i++) {
                const PBullet *b = &G.pb[i];
                V2 rel = v2_sub(e->pos, b->pos);
                float d2 = v2_len2(rel);
                if (d2 > 170.0f * 170.0f || v2_dot(rel, b->vel) <= 0.0f || v2_len2(b->vel) < 1.0f) continue;
                V2 bv = v2_norm(b->vel);
                V2 lat = v2_sub(rel, v2_scale(bv, v2_dot(rel, bv)));
                if (v2_len(lat) > e->radius + b->radius + 10.0f) continue;
                V2 side = v2_len2(lat) > 1.0f ? v2_norm(lat) : v2_perp(bv);
                V2 test = v2_add(e->pos, v2_scale(side, 60.0f));
                if (room_collide_circle(&test, e->radius, NULL)) side = v2_scale(side, -1.0f);
                e->vel = v2_scale(side, 820.0f);
                e->state = 1;
                e->state_t = 0.16f;
                e->dodge_cd = 1.2f;
                fx_particle(e->pos, v2(0, 0), 0.3f, e->radius * 2.0f, col_hex(d->color, 0.6f), 0.0f, PT_RING);
                sfx_play_at(SFX_DASH, e->pos);
                break;
            }
        }
    }
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        V2 side = v2_scale(v2_perp(dir), 9.0f);
        ebullet_spawn(v2_add(e->pos, side), aim, 420.0f, 6.0f, col_hex(0xbfffff, 1));
        ebullet_spawn(v2_sub(e->pos, side), aim, 420.0f, 6.0f, col_hex(0xbfffff, 1));
        sfx_play_at(SFX_ENEMY_SHOT, e->pos);
        e->fire_t = frand(1.2f, 1.6f);
    }
}

/* Rook: like the chess piece, only moves along rows and columns. It slides fast into the
 * ship's row or column, then fires a cross of bullet lines along it. */
static void update_rook(Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    e->angle = 0.0f;
    switch (e->state) {
    case 0: /* planted */
        move_towards(e, v2(0, 0), 10.0f);
        if (e->state_t <= 0.0f) {
            V2 to = v2_sub(G.pl.pos, e->pos);
            /* Slide along the axis that lines it up with the ship (the smaller offset). */
            e->aux = fabsf(to.x) < fabsf(to.y) ? v2(to.x > 0.0f ? 1.0f : -1.0f, 0) : v2(0, to.y > 0.0f ? 1.0f : -1.0f);
            e->target = fabsf(to.x) < fabsf(to.y) ? v2(G.pl.pos.x, e->pos.y) : v2(e->pos.x, G.pl.pos.y);
            e->state = 1;
            e->state_t = 0.3f;
        }
        break;
    case 1: /* telegraph */
        if (e->state_t <= 0.0f) {
            e->state = 2;
            e->state_t = 1.4f;
            sfx_play_at(SFX_DASH, e->pos);
        }
        break;
    case 2: { /* slide */
        float left = v2_dot(v2_sub(e->target, e->pos), e->aux);
        bool stop = left <= 4.0f || e->state_t <= 0.0f || blocked_ahead(e, e->aux, d->speed, NULL);
        e->vel = stop ? v2(0, 0) : v2_scale(e->aux, fminf(d->speed, left / DT));
        if (stop) {
            for (int k = 0; k < 4; k++)
                for (int j = 0; j < 3; j++)
                    ebullet_spawn(e->pos, PI_F * 0.5f * (float)k, 260.0f + 70.0f * (float)j, 7.0f, col_hex(0xe0e0ff, 1));
            sfx_play_at(SFX_ENEMY_SHOT, e->pos);
            e->state = 0;
            e->state_t = frand(0.9f, 1.3f);
        }
    } break;
    }
}

/* Splitter: a slow wobbly blob spitting weaving bullets; killing it splits it in three. */
static void update_splitter(Enemy *e, float aim) {
    wander(e, 260.0f, 520.0f, ENEMIES[e->type].speed);
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        for (int k = -1; k <= 1; k++) {
            EBullet *b = ebullet_spawn(e->pos, aim + 0.35f * (float)k, 210.0f, 8.0f, col_hex(0x9dff7a, 1));
            if (b) b->kind = EB_WAVY;
        }
        sfx_play_at(SFX_SPLAT, e->pos);
        e->fire_t = frand(1.9f, 2.4f);
    }
}

/* Splitling: hops after the ship in short bursts. */
static void update_splitling(Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    float hop = fmodf(e->t + (float)(e->uid % 7) * 0.1f, 0.5f) < 0.3f ? 1.0f : 0.25f;
    move_towards(e, v2_scale(nav_dir(e, G.pl.pos), d->speed * hop), 6.0f);
    e->angle = v2_heading(e->vel);
}

/* Leaper: crouches, jumps onto the ship (untouchable while airborne, shadow on the ground)
 * and lands with a shockwave. */
static void update_leaper(Enemy *e) {
    if (e->state == 0) {
        e->alt = 0.0f;
        move_towards(e, v2(0, 0), 8.0f);
        e->angle += 0.5f * DT;
        if (e->state_t <= 0.0f) {
            V2 to = v2_add(G.pl.pos, v2_scale(v2_angle(frand(0.0f, TAU_F)), frand(0.0f, 70.0f)));
            e->target = e->pos;
            e->aux = to;
            e->state = 1;
            e->state_t = LEAPER_JUMP_T;
            sfx_play_at(SFX_LOB, e->pos);
        }
    } else {
        float k = clampf(1.0f - e->state_t / LEAPER_JUMP_T, 0.0f, 1.0f);
        e->alt = sinf(PI_F * k) * 150.0f;
        V2 next = v2_lerp(e->target, e->aux, k);
        e->vel = v2_scale(v2_sub(next, e->pos), 1.0f / DT); /* the shadow glides to the landing spot */
        if (e->state_t <= 0.0f) {
            e->alt = 0.0f;
            e->vel = v2(0, 0);
            fire_ring(e->pos, 10, 200.0f, 8.0f, frand(0.0f, TAU_F), col_hex(0x7dffad, 1));
            fx_particle(e->pos, v2(0, 0), 0.35f, 130.0f, col_hex(0x3dff8a, 1), 0.0f, PT_RING);
            fx_shake(0.12f);
            sfx_play_at(SFX_SPLAT, e->pos);
            e->state = 0;
            e->state_t = frand(0.7f, 1.2f);
        }
    }
}

static int mines_out(void) {
    int n = 0;
    for (int i = 0; i < G.neb; i++) n += G.eb[i].kind == EB_MINE;
    return n;
}

/* Minelayer: roams the room sowing proximity mines. */
static void update_minelayer(Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    if (v2_dist2(e->pos, e->target) < 40.0f * 40.0f || e->t < DT * 2.0f)
        e->target = random_point_around(v2(ARENA_CX, ARENA_CY), 0.0f, 700.0f);
    move_towards(e, v2_scale(nav_dir(e, e->target), d->speed), 2.5f);
    e->angle = v2_heading(e->vel);
    if (e->fire_t <= 0.0f) {
        if (mines_out() < MAX_MINES && v2_dist2(e->pos, G.pl.pos) > 160.0f * 160.0f) {
            EBullet *b = ebullet_spawn(v2_sub(e->pos, v2_scale(v2_angle(e->angle), e->radius)), 0.0f, 0.0f, 9.0f, col_hex(0xffb050, 1));
            if (b) {
                b->kind = EB_MINE;
                b->life = 5.0f;
                b->n = 8;
                sfx_play_at(SFX_BEEP, e->pos);
            }
        }
        e->fire_t = frand(1.2f, 1.6f);
    }
}

/* Boomerang: throws a fan of three boomerangs that fly out, stop and come back. */
static void update_boomerang(Enemy *e, float aim) {
    wander(e, 300.0f, 550.0f, ENEMIES[e->type].speed);
    e->angle += 3.0f * DT;
    if (e->fire_t <= 0.0f) {
        for (int k = -1; k <= 1; k++) {
            float a = aim + 0.45f * (float)k;
            EBullet *b = ebullet_spawn(e->pos, a, 560.0f, 8.0f, col_hex(0xffd36a, 1));
            if (!b) continue;
            b->kind = EB_BOOMERANG;
            b->dir = v2_norm(b->vel);
            b->t1 = 560.0f / 0.75f * upg_enemy_bullet_speed(); /* turns back after 0.75 s */
            b->curve = -0.6f * (float)k;
            b->life = 2.4f;
        }
        sfx_play_at(SFX_SWORD, e->pos);
        e->fire_t = frand(2.3f, 2.8f);
    }
}

/* Ricochet: bank shots - aims at the ship's mirror image behind a wall, with bullets that
 * bounce, so they come from where nothing stands. */
static void update_ricochet(Enemy *e, float aim) {
    wander(e, 320.0f, 600.0f, ENEMIES[e->type].speed);
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        V2 p = G.pl.pos, img = p;
        switch (irand(0, 4)) {
        case 0: img.x = 2.0f * ARENA_X0 - p.x; break;
        case 1: img.x = 2.0f * ARENA_X1 - p.x; break;
        case 2: img.y = 2.0f * ARENA_Y0 - p.y; break;
        case 3: img.y = 2.0f * ARENA_Y1 - p.y; break;
        default: break; /* straight at it */
        }
        float a = v2_heading(v2_sub(img, e->pos));
        for (int k = -1; k <= 1; k += 2) {
            EBullet *b = ebullet_spawn(e->pos, a + 0.06f * (float)k, 400.0f, 7.0f, col_hex(0x7dcbff, 1));
            if (b) {
                b->kind = EB_BOUNCE;
                b->n = 2;
            }
        }
        sfx_play_at(SFX_BOUNCE, e->pos);
        e->fire_t = frand(1.3f, 1.7f);
    }
}

/* Orbiter: circles the ship at a breathing radius, firing inwards. */
static void update_orbiter(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    float r = 320.0f + 70.0f * sinf(e->t * 0.9f);
    float a = v2_heading(v2_sub(e->pos, G.pl.pos)) + 0.5f * (float)e->spin_dir;
    V2 goal = v2_add(G.pl.pos, v2_scale(v2_angle(a), r));
    goal.x = clampf(goal.x, ARENA_X0 + 40.0f, ARENA_X1 - 40.0f);
    goal.y = clampf(goal.y, ARENA_Y0 + 40.0f, ARENA_Y1 - 40.0f);
    /* Holds still while it fires its burst (the moment to hit it), turns back now and then. */
    move_towards(e, e->burst > 0 ? v2(0, 0) : v2_scale(nav_dir(e, goal), d->speed), 3.0f);
    if ((blocked_ahead(e, v2_norm(e->vel), d->speed, NULL) && chance(0.05f)) || e->state_t <= 0.0f) {
        e->spin_dir = -e->spin_dir;
        e->state_t = frand(3.0f, 5.0f);
    }
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        if (e->burst <= 0) e->burst = 3;
        ebullet_spawn(e->pos, aim, 330.0f, 7.0f, col_hex(0xcfa8ff, 1));
        sfx_play_at(SFX_ENEMY_SHOT, e->pos);
        e->burst--;
        e->fire_t = e->burst > 0 ? 0.22f : frand(1.4f, 1.8f);
    }
}

/* Blinker: fires a fat orb that bursts into a ring, then teleports (its destination flashes
 * first). */
static void update_blinker(Enemy *e, float aim) {
    move_towards(e, v2(0, 0), 6.0f);
    e->angle += 2.0f * DT;
    if (e->state == 0) {
        if (e->fire_t <= 0.0f && e->burst == 0) {
            EBullet *b = ebullet_spawn(e->pos, aim, 170.0f, 14.0f, col_hex(0xff8ae0, 1));
            if (b) {
                b->kind = EB_SPLIT;
                b->t1 = 1.0f;
                b->n = 10;
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->burst = 1;
        }
        if (e->state_t <= 0.0f) {
            e->aux = random_point_around(G.pl.pos, 260.0f, 520.0f);
            e->state = 1;
            e->state_t = BLINK_WARN_T;
        }
    } else if (e->state_t <= 0.0f) {
        fx_particle(e->pos, v2(0, 0), 0.25f, 60.0f, col_hex(0xff5ad2, 1), 0.0f, PT_RING);
        e->pos = e->aux;
        fx_particle(e->pos, v2(0, 0), 0.25f, 60.0f, col_hex(0xff5ad2, 1), 0.0f, PT_RING);
        sfx_play_at(SFX_TELEPORT, e->pos);
        e->state = 0;
        e->state_t = frand(1.6f, 2.0f);
        e->fire_t = 0.35f;
        e->burst = 0;
    }
}

/* Serpent: slithers in big S curves, leaving a short-lived wall of still bullets behind it. */
static void update_serpent(Enemy *e) {
    const EnemyDef *d = &ENEMIES[e->type];
    if (v2_dist2(e->pos, e->target) < 50.0f * 50.0f || e->state_t <= 0.0f || e->t < DT * 2.0f) {
        e->target = random_point_around(G.pl.pos, 150.0f, 450.0f);
        e->state_t = frand(2.0f, 3.0f);
    }
    V2 fwd = nav_dir(e, e->target);
    V2 want = v2_norm(v2_add(fwd, v2_scale(v2_perp(fwd), 1.1f * sinf(e->t * 4.0f))));
    move_towards(e, v2_scale(want, d->speed), 4.0f);
    e->angle = v2_heading(e->vel);
    if (e->fire_t <= 0.0f && v2_dist2(e->pos, G.pl.pos) > 90.0f * 90.0f) {
        EBullet *b = ebullet_spawn(e->pos, 0.0f, 0.0f, 6.0f, col_hex(0xc8ff6a, 1));
        if (b) b->life = 1.5f;
        e->fire_t = 0.1f;
    }
}

/* Mirror: turns its front plate (which stops every shot) slowly towards the ship: get around
 * it. Fires weaving bullets from behind the plate. */
static void update_mirror(Enemy *e, float aim) {
    wander(e, 260.0f, 460.0f, ENEMIES[e->type].speed);
    float diff = atan2f(sinf(aim - e->angle), cosf(aim - e->angle));
    e->angle += clampf(diff, -0.85f * DT, 0.85f * DT);
    e->dodge_cd -= DT;
    if (e->fire_t <= 0.0f) {
        e->dodge_cd = MIRROR_OPEN_T; /* it opens its plate to fire: the moment to hit it */
        for (int k = -1; k <= 1; k++) {
            EBullet *b = ebullet_spawn(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius)), e->angle + 0.3f * (float)k, 250.0f, 7.0f,
                                       col_hex(0xeefcff, 1));
            if (b) b->kind = EB_WAVY;
        }
        sfx_play_at(SFX_ENEMY_SHOT, e->pos);
        e->fire_t = frand(1.5f, 1.9f);
    }
}

/* Lancer: charges a beam (the aim line thickens, then locks), then fires a sweeping stream
 * of very fast bullets. */
static void update_lancer(Enemy *e, float aim) {
    switch (e->state) {
    case 0:
        wander(e, 380.0f, 700.0f, ENEMIES[e->type].speed);
        e->angle = aim;
        if (e->fire_t <= 0.0f) {
            e->state = 1;
            e->state_t = LANCER_AIM_T;
            sfx_play_at(SFX_SNIPER_CHARGE, e->pos);
        }
        break;
    case 1:
        move_towards(e, v2(0, 0), 8.0f);
        if (e->state_t > 0.3f) e->angle = aim;
        if (e->state_t <= 0.0f) {
            e->state = 2;
            e->state_t = LANCER_BEAM_T;
            float diff = atan2f(sinf(aim - e->angle), cosf(aim - e->angle));
            e->spin = diff >= 0.0f ? 1.0f : -1.0f; /* sweeps towards where the ship went */
            sfx_play_at(SFX_SNIPER_FIRE, e->pos);
        }
        break;
    default:
        move_towards(e, v2(0, 0), 8.0f);
        e->angle += 0.45f * e->spin * DT;
        if ((G.ticks + e->uid) % 2 == 0) {
            V2 muzzle = v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius));
            ebullet_spawn(muzzle, e->angle, 1100.0f, 6.0f, col_hex(0xff7070, 1));
        }
        if (e->state_t <= 0.0f) {
            e->state = 0;
            e->fire_t = frand(2.2f, 2.8f);
        }
        break;
    }
}

/* Mortar: heavy and nearly still; shells the area around the ship with a salvo of grenades
 * (one on the ship, four around it). */
static void update_mortar(Enemy *e, float aim) {
    if (v2_dist2(e->pos, e->target) < 30.0f * 30.0f || e->t < DT * 2.0f)
        e->target = random_point_around(e->pos, 0.0f, 160.0f);
    move_towards(e, v2_scale(nav_dir(e, e->target), ENEMIES[e->type].speed), 1.0f);
    e->angle = aim;
    if (e->fire_t <= 0.0f) {
        float rot = frand(0.0f, TAU_F);
        ebullet_lob(e->pos, G.pl.pos, 1.3f, 5, col_hex(0xffc080, 1));
        for (int k = 0; k < 4; k++)
            ebullet_lob(e->pos, v2_add(G.pl.pos, v2_scale(v2_angle(rot + PI_F * 0.5f * (float)k), 130.0f)), 1.3f + 0.08f * (float)k, 5,
                        col_hex(0xffaa55, 1));
        fx_shake(0.08f);
        sfx_play_at(SFX_TANK_FIRE, e->pos);
        e->fire_t = frand(3.0f, 3.5f);
    }
}

/* Guardian: follows its allies and shields them (they take 40% damage while it lives);
 * kill it first. Pulses slow rings. */
static void update_guardian(Enemy *e, float dist, V2 dir) {
    const EnemyDef *d = &ENEMIES[e->type];
    V2 sum = v2(0, 0);
    int n = 0;
    for (int i = 0; i < G.nen; i++) {
        const Enemy *o = &G.en[i];
        if (o == e || o->hp <= 0.0f || o->type == EN_GUARDIAN || o->type == EN_BOSS) continue;
        if (v2_dist2(o->pos, e->pos) > 700.0f * 700.0f) continue;
        sum = v2_add(sum, o->pos);
        n++;
    }
    V2 goal;
    if (n > 0) {
        goal = v2_scale(sum, 1.0f / (float)n);
        goal = v2_add(goal, v2_scale(dir, -120.0f)); /* behind its flock, from the ship's view */
    } else {
        goal = dist < 300.0f ? v2_sub(e->pos, v2_scale(dir, 100.0f)) : G.pl.pos;
    }
    if (v2_dist2(goal, e->pos) > 40.0f * 40.0f) move_towards(e, v2_scale(nav_dir(e, goal), d->speed), 2.0f);
    else move_towards(e, v2(0, 0), 3.0f);
    e->angle += 0.7f * DT;
    if (e->fire_t <= 0.0f) {
        fire_ring(e->pos, 8, 150.0f, 8.0f, e->angle, col_hex(0x9de2ff, 1));
        sfx_play_at(SFX_ENEMY_RING, e->pos);
        e->fire_t = frand(2.8f, 3.4f);
    }
}

/* Wraith: fades out (shots go through it) and drifts closer, then shows up for a moment to
 * release homing wisps. */
static void update_wraith(Enemy *e, float aim) {
    const EnemyDef *d = &ENEMIES[e->type];
    switch (e->state) {
    case 0: /* faded (timer in fire_t: wander() uses state_t) */
        wander(e, 220.0f, 420.0f, d->speed);
        if (e->fire_t <= 0.0f) {
            e->state = 1;
            e->state_t = 0.4f;
            sfx_play_at(SFX_TELEPORT, e->pos);
        }
        break;
    case 1: /* materializing */
        move_towards(e, v2(0, 0), 5.0f);
        if (e->state_t <= 0.0f) {
            for (int k = 0; k < 4; k++) {
                EBullet *b = ebullet_spawn(e->pos, aim + PI_F * 0.5f + PI_F * (float)k / 3.0f, 210.0f, 7.0f, col_hex(0xd8c8ff, 1));
                if (b) {
                    b->kind = EB_HOMING;
                    b->t1 = 1.3f;
                    b->life = 5.0f;
                }
            }
            sfx_play_at(SFX_ENEMY_RING, e->pos);
            e->state = 2;
            e->state_t = 1.6f;
        }
        break;
    default: /* visible, vulnerable */
        move_towards(e, v2(0, 0), 3.0f);
        if (e->state_t <= 0.0f) {
            e->state = 0;
            e->fire_t = frand(1.8f, 2.4f);
        }
        break;
    }
    e->angle = aim;
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
        e->hp -= enemy_damage_cap(e, dot * DT);
        G.last_enemy_damage_t = G.time;
        if ((G.ticks + e->uid) % 9 == 0)
            fx_particle(v2_add(e->pos, v2(frand(-e->radius, e->radius), frand(-e->radius, e->radius))), v2(0, -60), 0.4f, 6.0f,
                        col_hex(e->burn_t > 0.0f ? 0xff7a2e : 0x7dff4a, 0.9f), 0.0f, PT_GLOW);
    }

    upg_enemy_reactions(e); /* synergies: THERMAL SHOCK, COMBUSTION */
    /* STICKY CHARGES: the charge planted by the last hits goes off. */
    if (e->sticky_t > 0.0f && (e->sticky_t -= DT) <= 0.0f) {
        float boom = e->sticky_dmg;
        e->sticky_dmg = 0.0f;
        explode_at(e->pos, 90.0f + e->radius, boom, e, 0xffc23d);
        enemy_damage(e, boom, v2(0, 0));
        sfx_play_at(SFX_EXPLODE_SMALL, e->pos);
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

    case EN_BOSS:
        update_boss(e, aim);
        break;

    case EN_RAMMER: update_rammer(e, aim, dist); break;
    case EN_GRENADIER: update_grenadier(e, aim); break;
    case EN_CRAWLER: update_crawler(e, aim); break;
    case EN_TOP: update_top(e); break;
    case EN_KAMIKAZE: update_kamikaze(e, dist); break;
    case EN_DODGER: update_dodger(e, dir, aim, dist); break;
    case EN_ROOK: update_rook(e); break;
    case EN_SPLITTER: update_splitter(e, aim); break;
    case EN_SPLITLING: update_splitling(e); break;
    case EN_LEAPER: update_leaper(e); break;
    case EN_MINELAYER: update_minelayer(e); break;
    case EN_BOOMERANG: update_boomerang(e, aim); break;
    case EN_RICOCHET: update_ricochet(e, aim); break;
    case EN_ORBITER: update_orbiter(e, aim); break;
    case EN_BLINKER: update_blinker(e, aim); break;
    case EN_SERPENT: update_serpent(e); break;
    case EN_MIRROR: update_mirror(e, aim); break;
    case EN_LANCER: update_lancer(e, aim); break;
    case EN_MORTAR: update_mortar(e, aim); break;
    case EN_GUARDIAN: update_guardian(e, dist, dir); break;
    case EN_WRAITH: update_wraith(e, aim); break;

    default:
        break;
    }
    if (e->hp <= 0.0f) return; /* self-destructed (kamikaze) */

    if (e->type != EN_CRAWLER) { /* the crawler is glued to the wall (placed by update_crawler) */
        /* A leaper in the air flies over obstacles; it lands back outside them. */
        e->pos = v2_add(e->pos, v2_scale(e->vel, DT * (e->type == EN_LEAPER ? 1.0f : tempo)));
        V2 n;
        if (e->alt <= 0.0f && room_collide_circle(&e->pos, e->radius, &n)) {
            float into = v2_dot(e->vel, n);
            if (into < 0.0f) e->vel = v2_sub(e->vel, v2_scale(n, into));
        }
    }

    float cr = e->radius + pl->hit_radius + 4.0f;
    if (!enemy_untouchable(e) && v2_dist2(e->pos, pl->pos) < cr * cr && player_hurt(1)) {
        /* Rammed: the hit hurts the enemy too, enough to kill the small rushers outright. */
        V2 away = v2_norm(v2_sub(e->pos, pl->pos));
        enemy_damage(e, CONTACT_DAMAGE * fmaxf(pl->dmg_mult, 1.0f), v2_scale(away, 700.0f));
        fx_burst(e->pos, 14, 120.0f, 500.0f, 0.3f, 2.5f, col_hex(ENEMIES[e->type].color, 1));
    }
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
            /* Crawlers are part of the wall: they push, they are never pushed off it. */
            bool ca = a->type == EN_CRAWLER, cb = b->type == EN_CRAWLER;
            if (ca && cb) continue;
            float ka = ca ? 0.0f : cb ? 1.0f : 0.5f;
            V2 push = v2_scale(d, (rr - dist) / dist);
            a->pos = v2_sub(a->pos, v2_scale(push, ka));
            b->pos = v2_add(b->pos, v2_scale(push, 1.0f - ka));
        }
    }
    /* Separation can push enemies into walls or obstacles: resolve again. */
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].type != EN_CRAWLER) room_collide_circle(&G.en[i].pos, G.en[i].radius, NULL);
}

static void on_death(Enemy *e) {
    SDL_FColor c = col_hex(ENEMIES[e->type].color, 1);
    fx_explosion(e->pos, e->radius, c);
    fx_shake(0.06f + e->radius * 0.004f);
    G.room.last_death = e->pos; /* the shatter wave starts from the last one */
    G.kills++;
    if (e->type == EN_TANK) G.hitstop = fmaxf(G.hitstop, 0.05f);
    if (e->type == EN_BOSS) {
        /* The boss takes its minions and every bullet with it. */
        G.hitstop = fmaxf(G.hitstop, 0.25f);
        fx_shake(0.6f);
        for (int k = 0; k < 6; k++)
            fx_explosion(v2_add(e->pos, v2(frand(-e->radius, e->radius), frand(-e->radius, e->radius))), e->radius * 0.7f, k & 1 ? col_hex(0xffd23d, 1) : c);
        fx_particle(e->pos, v2(0, 0), 0.8f, 700.0f, c, 0.0f, PT_RING);
        ebullets_clear(true);
        G.nsp = 0;
        for (int i = 0; i < G.nen; i++)
            if (&G.en[i] != e && G.en[i].hp > 0.0f) G.en[i].hp = 0.0f;
    }
    if (e->type == EN_TANK || e->type == EN_BOSS) sfx_play_at(SFX_EXPLODE_BIG, e->pos);
    else if (e->type == EN_SWARM) sfx_play_ex(SFX_EXPLODE_SMALL, 0.7f, 1.5f, (e->pos.x - ARENA_CX) / (ARENA_X1 - ARENA_CX) * 0.7f);
    else sfx_play_at(SFX_EXPLODE_SMALL, e->pos);

    /* SPLITTER: three splitlings burst out (they never split again). */
    if (e->type == EN_SPLITTER) {
        for (int k = 0; k < 3 && G.nsp < MAX_SPAWNS; k++) {
            V2 p = v2_add(e->pos, v2_scale(v2_angle(e->angle + TAU_F * (float)k / 3.0f), e->radius * 0.6f));
            room_collide_circle(&p, ENEMIES[EN_SPLITLING].radius, NULL);
            enemy_queue_spawn(EN_SPLITLING, p, 0.12f);
        }
        sfx_play_at(SFX_SPLAT, e->pos);
    }
    /* REVENGE malus: a last bullet for the ship (never point-blank). */
    if (e->type != EN_BOSS && !G.room.cleared && v2_dist2(e->pos, G.pl.pos) > 160.0f * 160.0f && chance(malus_pct(MAL_REVENGE)))
        ebullet_spawn(e->pos, v2_heading(v2_sub(G.pl.pos, e->pos)), 300.0f, 8.0f, col_hex(0xff5a6e, 1));
    /* No drops: resources only ever come from room rewards. A kamikaze that blew itself up
     * was not killed: no kill effects. */
    if (e->type == EN_KAMIKAZE && e->burst == -1) {
        G.kills--;
        return;
    }
    upg_on_kill(e);
}

void enemies_update(void) {
    /* TIME STOP: nothing moves, shoots or even spawns; deaths are still resolved. */
    bool frozen = active_time_stop_on() && G.state == GS_PLAYING;
    for (int i = 0; i < G.nsp && !frozen; i++) {
        Spawn *s = &G.sp[i];
        s->t -= DT;
        if (s->t <= 0.0f) {
            enemy_create(s->type, s->pos);
            G.sp[i] = G.sp[--G.nsp];
            i--;
        }
    }
    for (int i = 0; i < G.nen && !frozen; i++)
        if (G.en[i].hp > 0.0f) update_enemy(&G.en[i]);
    if (frozen) /* knockback and pulls still slide them a little */
        for (int i = 0; i < G.nen; i++) {
            Enemy *en = &G.en[i];
            if (en->type == EN_CRAWLER || en->type == EN_LEAPER) en->vel = v2(0, 0); /* glued to the wall / frozen mid-jump */
            en->pos = v2_add(en->pos, v2_scale(en->vel, DT));
            en->vel = v2_scale(en->vel, fmaxf(0.0f, 1.0f - 8.0f * DT));
            if (en->type != EN_CRAWLER) room_collide_circle(&en->pos, en->radius, NULL);
            en->pos.x = clampf(en->pos.x, ARENA_X0 + en->radius, ARENA_X1 - en->radius);
            en->pos.y = clampf(en->pos.y, ARENA_Y0 + en->radius, ARENA_Y1 - en->radius);
            en->flash -= DT;
        }
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
        V2 gp = v2(e->pos.x, e->pos.y - e->alt);
        float ga = e->type == EN_WRAITH && e->state == 0 ? 0.04f : 0.22f;
        draw_glow(gp, e->radius * 2.3f, col_hex(ENEMIES[e->type].color, ga));
        if (e->type == EN_SNIPER && e->state == 1) {
            float k = 1.0f - e->state_t / 0.7f;
            bool locked = e->state_t <= 0.25f;
            SDL_FColor c = locked ? col_hex(0xffffff, 0.8f) : col_hex(0x7dff7a, 0.25f + 0.4f * k);
            V2 dir = v2_angle(e->angle);
            draw_line(e->pos, v2_add(e->pos, v2_scale(dir, 2400.0f)), 1.0f + 3.0f * k, c);
        }
        if (e->type == EN_BOSS && e->state == BA_CHARGE_AIM) {
            float k = 1.0f - e->state_t / BOSS_CHARGE_AIM_T;
            bool locked = e->state_t <= 0.25f;
            V2 dir = v2_angle(e->angle);
            draw_line(e->pos, v2_add(e->pos, v2_scale(dir, 2400.0f)), e->radius * 2.0f * (0.3f + 0.7f * k),
                      locked ? col_hex(0xffffff, 0.25f) : col_hex(0xff3df0, 0.08f + 0.2f * k));
        }
        if (e->type == EN_BOSS && e->inv_t > 0.0f)
            draw_ring(e->pos, e->radius * 1.45f, 6.0f, col_hex(0xffd23d, 0.3f + 0.3f * sinf(G.time * 30.0f)));
        if (e->type == EN_HUNTER && e->state == 1) {
            float k = 1.0f - e->state_t / 0.45f;
            V2 dir = v2_angle(e->angle);
            draw_line(e->pos, v2_add(e->pos, v2_scale(dir, HUNTER_DASH_SPEED * 0.38f)), 2.0f + 6.0f * k, col_hex(0xff3d6e, 0.2f + 0.4f * k));
        }
        if (e->type == EN_RAMMER && e->state == 1) { /* where it will charge */
            float k = 1.0f - e->state_t / RAMMER_AIM_T;
            bool locked = e->state_t <= 0.2f;
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), 1400.0f)), e->radius * (0.4f + 1.2f * k),
                      locked ? col_hex(0xffffff, 0.22f) : col_hex(0xff6b3d, 0.1f + 0.25f * k));
        }
        if (e->type == EN_LANCER && e->state >= 1) {
            V2 dir = v2_angle(e->angle);
            if (e->state == 1) {
                float k = 1.0f - e->state_t / LANCER_AIM_T;
                bool locked = e->state_t <= 0.3f;
                draw_line(e->pos, v2_add(e->pos, v2_scale(dir, 2400.0f)), 1.0f + 7.0f * k,
                          locked ? col_hex(0xffffff, 0.75f) : col_hex(0xff3d3d, 0.2f + 0.4f * k));
            } else {
                draw_line(e->pos, v2_add(e->pos, v2_scale(dir, 2400.0f)), 16.0f, col_hex(0xff3d3d, 0.18f));
            }
        }
        if (e->type == EN_ROOK && e->state >= 1)
            draw_line(e->pos, e->target, e->state == 1 ? 10.0f : 4.0f, col_hex(0xc8c8ff, e->state == 1 ? 0.35f : 0.15f));
        if (e->type == EN_BLINKER && e->state == 1) { /* where it will appear */
            float k = 1.0f - e->state_t / BLINK_WARN_T;
            SDL_FColor c = col_hex(0xff5ad2, 0.3f + 0.5f * k);
            draw_ring(e->aux, e->radius * (3.0f - 2.0f * k), 3.0f, c);
            draw_ngon_outline(e->aux, e->radius, 4, G.time * 6.0f, 2.0f, c);
        }
        if (e->type == EN_KAMIKAZE && e->state == 1) {
            float k = 1.0f - e->state_t / KAMIKAZE_FUSE;
            draw_glow(e->pos, e->radius * (3.0f + 4.0f * k), col_hex(0xffe03d, 0.25f + 0.35f * k));
        }
        if (e->type == EN_GUARDIAN) { /* tethers to every ally it shields */
            float pulse = 0.5f + 0.5f * sinf(G.time * 5.0f + (float)e->uid);
            draw_ring(e->pos, GUARDIAN_RANGE, 1.5f, col_hex(0x6dd2ff, 0.06f + 0.05f * pulse));
            for (int j = 0; j < G.nen; j++) {
                const Enemy *o = &G.en[j];
                if (o == e || o->hp <= 0.0f || o->type == EN_GUARDIAN || o->type == EN_BOSS) continue;
                if (v2_dist2(o->pos, e->pos) > GUARDIAN_RANGE * GUARDIAN_RANGE) continue;
                draw_line(e->pos, o->pos, 2.0f, col_hex(0x6dd2ff, 0.2f + 0.15f * pulse));
                draw_ring(o->pos, o->radius + 7.0f, 2.0f, col_hex(0x6dd2ff, 0.35f + 0.2f * pulse));
            }
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
        if (e->type == EN_ROOK) ang = PI_F * 0.25f; /* a square standing straight */
        float rad = e->radius;
        /* WRAITH: barely visible while faded out. */
        if (e->type == EN_WRAITH) {
            float a = e->state == 0 ? 0.13f : e->state == 1 ? 0.13f + 0.87f * (1.0f - e->state_t / 0.4f) : 1.0f;
            c.a *= a;
            fill.a *= a;
        }
        if (e->type == EN_KAMIKAZE && e->state == 1) { /* swells and blinks before blowing */
            float k = 1.0f - e->state_t / KAMIKAZE_FUSE;
            rad *= 1.0f + 0.5f * k;
            if (fmodf(e->state_t, 0.12f) < 0.06f) fill = col_hex(0xffe03d, 1);
        }
        /* LEAPER: shadow on the ground, body up in the air. */
        V2 at = e->pos;
        if (e->alt > 0.0f) {
            draw_circle(e->pos, rad * (1.0f - 0.3f * e->alt / 150.0f), rgba(0, 0, 0, 0.45f));
            at = v2(e->pos.x, e->pos.y - e->alt);
            rad *= 1.0f + 0.25f * e->alt / 150.0f;
        }

        switch (e->type) {
        case EN_SHOOTER:
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 10.0f)), 8.0f, c);
            break;
        case EN_TANK:
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 16.0f)), 16.0f, c);
            break;
        case EN_GRENADIER: /* the launcher tube */
        case EN_MORTAR:
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 12.0f)), e->type == EN_MORTAR ? 18.0f : 9.0f, c);
            break;
        case EN_RAMMER: /* horns */
            for (int k = -1; k <= 1; k += 2) {
                V2 base = v2_add(e->pos, v2_scale(v2_angle(e->angle + 0.7f * (float)k), e->radius * 0.8f));
                draw_line(base, v2_add(base, v2_scale(v2_angle(e->angle + 0.25f * (float)k), 16.0f)), 5.0f, c);
            }
            break;
        case EN_CRAWLER: /* legs gripping the wall */
            for (int k = -2; k <= 2; k++) {
                V2 side = v2_scale(v2_perp(e->aux), (float)k * 7.0f);
                float wig = sinf(G.time * 20.0f + (float)k) * 3.0f;
                V2 foot = v2_add(v2_add(e->pos, v2_scale(e->aux, -e->radius - 6.0f)), v2_scale(side, 1.6f));
                draw_line(v2_add(e->pos, side), v2_add(foot, v2_scale(v2_perp(e->aux), wig)), 3.0f, c);
            }
            break;
        case EN_SERPENT: /* tail */
            for (int k = 4; k >= 1; k--) {
                float back = (float)k * 13.0f;
                V2 dir = v2_angle(e->angle);
                V2 tp = v2_add(v2_sub(e->pos, v2_scale(dir, back)), v2_scale(v2_perp(dir), sinf(e->t * 4.0f - (float)k * 0.8f) * 9.0f));
                draw_circle(tp, e->radius * (1.0f - 0.15f * (float)k), col_a(c, 0.6f));
            }
            break;
        default:
            break;
        }

        if (e->type == EN_SPLITTER || e->type == EN_SPLITLING) { /* blobs wobble */
            float w = 1.0f + 0.08f * sinf(e->t * 7.0f + (float)e->uid);
            draw_circle(at, rad * w, fill);
            draw_ring(at, rad * w, 3.0f, c);
        } else {
            draw_ngon(at, rad, d->sides, ang, fill);
            draw_ngon_outline(at, rad, d->sides, ang, 3.0f, c);
        }

        switch (e->type) {
        case EN_RAMMER:
            if (e->state == 3) /* stunned */
                for (int k = 0; k < 3; k++)
                    draw_ngon(v2_add(v2(e->pos.x, e->pos.y - e->radius - 10.0f), v2_scale(v2_angle(G.time * 6.0f + TAU_F * (float)k / 3.0f), 16.0f)),
                              4.0f, 4, G.time * 8.0f, col_hex(0xfff07a, 1));
            break;
        case EN_GRENADIER:
            draw_circle(e->pos, e->radius * 0.35f * clampf(1.0f - e->fire_t / 2.5f, 0.2f, 1.0f), c);
            break;
        case EN_TOP:
            draw_ngon_outline(e->pos, e->radius * 0.55f, 4, -e->spin * 1.5f, 2.0f, c);
            draw_circle(e->pos, 4.0f, e->state == 1 ? col_hex(0xffffff, 1) : c);
            if (e->state == 1) /* dizzy */
                draw_ring(v2(e->pos.x, e->pos.y - e->radius - 10.0f), 7.0f + 2.0f * sinf(G.time * 12.0f), 2.0f, col_hex(0xfff07a, 1));
            break;
        case EN_DODGER:
            draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->angle), 5.0f)), 4.0f, c);
            if (e->dodge_cd <= 0.0f) draw_ring(e->pos, e->radius + 5.0f, 1.5f, col_a(c, 0.5f)); /* ready to dodge */
            break;
        case EN_ROOK:
            draw_line(v2(e->pos.x - 10.0f, e->pos.y), v2(e->pos.x + 10.0f, e->pos.y), 3.0f, c);
            draw_line(v2(e->pos.x, e->pos.y - 10.0f), v2(e->pos.x, e->pos.y + 10.0f), 3.0f, c);
            break;
        case EN_SPLITTER:
            for (int k = 0; k < 3; k++)
                draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->t * 1.3f + TAU_F * (float)k / 3.0f), e->radius * 0.4f)), e->radius * 0.22f, c);
            break;
        case EN_SPLITLING:
            draw_circle(e->pos, e->radius * 0.35f, c);
            break;
        case EN_LEAPER:
            draw_circle(v2_add(at, v2(-6.0f, -4.0f)), 3.5f, c); /* eyes */
            draw_circle(v2_add(at, v2(6.0f, -4.0f)), 3.5f, c);
            break;
        case EN_MINELAYER:
            draw_ngon(e->pos, e->radius * 0.4f, 8, -e->t, col_a(c, clampf(1.0f - e->fire_t / 1.5f, 0.2f, 1.0f)));
            break;
        case EN_BOOMERANG:
            for (int k = 0; k < 3; k++)
                draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(-e->angle * 2.0f + TAU_F * (float)k / 3.0f), e->radius * 0.6f)), 3.0f, c);
            break;
        case EN_RICOCHET:
            draw_ring(e->pos, e->radius * 0.45f, 2.0f, c);
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 6.0f)), 4.0f, c);
            break;
        case EN_ORBITER:
            draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->angle), 6.0f)), 5.0f, c);
            draw_ring(e->pos, e->radius + 6.0f, 1.5f, col_a(c, 0.4f));
            break;
        case EN_BLINKER:
            draw_ngon_outline(e->pos, e->radius * 0.5f, 4, -e->angle * 2.0f, 2.0f, c);
            break;
        case EN_SERPENT:
            draw_circle(v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius * 0.4f)), 4.0f, c);
            break;
        case EN_MIRROR: { /* the front plate */
            SDL_FColor pc = e->flash > 0.0f ? COL_WHITE : col_hex(0xffffff, e->dodge_cd > 0.0f ? 0.25f : 0.95f);
            V2 prev = v2_add(e->pos, v2_scale(v2_angle(e->angle - MIRROR_ARC), e->radius + 9.0f));
            for (int k = 1; k <= 8; k++) {
                float a = e->angle - MIRROR_ARC + 2.0f * MIRROR_ARC * (float)k / 8.0f;
                V2 p = v2_add(e->pos, v2_scale(v2_angle(a), e->radius + 9.0f));
                draw_line(prev, p, 6.0f, pc);
                prev = p;
            }
            draw_circle(e->pos, e->radius * 0.3f, c);
        } break;
        case EN_LANCER:
            draw_line(e->pos, v2_add(e->pos, v2_scale(v2_angle(e->angle), e->radius + 14.0f)), 6.0f, c);
            if (e->state == 1) draw_circle(e->pos, e->radius * 0.5f * (1.0f - e->state_t / LANCER_AIM_T), COL_WHITE);
            break;
        case EN_MORTAR:
            draw_circle(e->pos, e->radius * 0.45f, c);
            draw_circle(e->pos, e->radius * 0.3f, fill);
            break;
        case EN_GUARDIAN:
            draw_ring(e->pos, e->radius * 0.6f, 3.0f, c);
            draw_ngon(e->pos, e->radius * 0.3f, 6, -e->angle, c);
            break;
        case EN_WRAITH:
            draw_circle(v2_add(e->pos, v2(-5.0f, -2.0f)), 3.0f, col_a(COL_WHITE, c.a));
            draw_circle(v2_add(e->pos, v2(5.0f, -2.0f)), 3.0f, col_a(COL_WHITE, c.a));
            break;
        case EN_KAMIKAZE:
            draw_circle(e->pos, rad * 0.35f, c);
            break;
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
        case EN_BOSS: {
            /* Inner counter-rotating hexagon, orbiting nodes (one per phase left), and an
             * eye that follows the ship. */
            draw_ngon_outline(e->pos, e->radius * 0.68f, 6, -ang * 1.7f, 3.0f, c);
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
        } break;
        default:
            break;
        }

        if (e->sticky_t > 0.0f) { /* STICKY CHARGES: a blinking charge about to blow */
            float blink = fmodf(e->sticky_t, 0.16f) < 0.08f ? 1.0f : 0.35f;
            draw_ngon(v2_add(e->pos, v2(e->radius * 0.5f, -e->radius * 0.5f)), 6.0f, 4, G.time * 6.0f, col_hex(0xffc23d, blink));
        }
        if (e->hp < e->max_hp && e->radius >= 18.0f && e->type != EN_BOSS && !(e->type == EN_WRAITH && e->state == 0)) {
            float w = e->radius * 2.0f, k = clampf(e->hp / e->max_hp, 0.0f, 1.0f);
            float x = at.x - w * 0.5f, y = at.y - e->radius - 12.0f;
            draw_rect(x, y, w, 4.0f, rgba(0, 0, 0, 0.6f));
            draw_rect(x, y, w * k, 4.0f, col_hex(d->color, 1));
        }
    }
}
