#include "draw.h"
#include "game.h"
#include "audio.h"

#include <stdio.h>

/*
 * The gun. There is only one: weapon mods (UPG_W_* upgrades) reshape it and combine freely.
 * gun_profile() starts from the plain blaster and applies every mod turned on, each one
 * multiplying or adding to its numbers. The sword (Knight) replaces the gun altogether.
 */

typedef struct {
    float cooldown;          /* seconds between volleys, before the fire rate stats */
    int count, mult;         /* projectiles per volley: (count + extra_proj) * mult */
    float spread, spread_per, jitter, parallel;
    float damage, speed, speed_var, life, radius, knockback, homing;
    int pierce, bounce, flags, style;
    uint32_t color;
    SfxId sfx;
    float pitch;
    bool recoil;
} Gun;

#define GUN_CD_MIN 0.03f /* combined mods never fire faster / slower than this... */
#define GUN_CD_MAX 0.7f  /* ...the damage makes up for it (same damage per second) */
#define MIN_REACH 320.0f /* px a shot flies at least (twice that for shots that come back) */
#define MAX_VOLLEY 40    /* projectiles per volley (more: fewer, stronger ones) */
#define PIERCE_ALL 999

static const char *const OPTION_NAMES[] = {
    "scatter", "needler", "seeker", "bouncer", "railgun", "flamer", "rockets", "chakram", "cluster", "arc", "vulcan", "wave", "laser", "orbit", "prism", "graviton", "sword",
};

const char *weapon_option_name(int u) { return upgrade_is_weapon(u) ? OPTION_NAMES[u - UPG_W_SCATTER] : "?"; }

int weapon_mod_by_name(const char *name) {
    for (int u = UPG_W_SCATTER; u <= UPG_W_SWORD; u++)
        if (!SDL_strcmp(name, weapon_option_name(u))) return u;
    return -1;
}

bool sword_on(void) { return upg_has(UPG_W_SWORD) > 0; }

bool weapon_trigger(void) {
    return G.in.fire || (upg_has(UPG_DEAD_TRIGGER) && !G.room.cleared && G.state == GS_PLAYING && !G.transitioning);
}

/* The look (colour, bullet style, sound) comes from the most striking mod: mods are applied
 * from the least to the most striking one, each one overriding the look. */
#define LOOK(col, st, snd, pt) (g->color = (col), g->style = (st), g->sfx = (snd), g->pitch = (pt))

static void gun_profile(Gun *g) {
    *g = (Gun){.cooldown = 0.08f, .count = 1, .mult = 1, .spread_per = 0.03f, .jitter = 0.03f, .parallel = 10.0f,
               .damage = 7.0f, .speed = 1500.0f, .life = 1.1f, .radius = 5.0f, .knockback = 50.0f,
               .style = BS_BOLT, .color = 0x66f0ff, .sfx = SFX_SHOT_BLASTER, .pitch = 1.0f};
    if (upg_has(UPG_W_BOUNCER)) { /* big orbs, many bounces */
        g->bounce += 4, g->pierce += 1, g->radius *= 1.6f, g->speed *= 0.63f, g->life *= 2.2f;
        g->damage *= 1.45f, g->cooldown *= 1.8f, g->spread_per += 0.1f, g->knockback = fmaxf(g->knockback, 80.0f);
        LOOK(0xffe14d, BS_BOUNCER, SFX_SHOT_BOUNCER, 1.0f);
    }
    if (upg_has(UPG_W_SEEKER)) { /* homing missiles that speed up */
        g->homing += 7.0f, g->count += 1, g->speed *= 0.33f, g->speed_var += 100.0f, g->life *= 1.8f, g->radius *= 1.2f;
        g->damage *= 1.6f, g->cooldown *= 2.4f, g->spread += 0.7f, g->spread_per += 0.09f, g->jitter += 0.07f;
        g->flags |= PB_ACCEL;
        LOOK(0x9dff6a, BS_SEEKER, SFX_SHOT_SEEKER, 1.0f);
    }
    if (upg_has(UPG_W_NEEDLER)) { /* twice the rate, half the damage, piercing needles */
        g->cooldown *= 0.5f, g->damage *= 0.52f, g->pierce += 2, g->speed *= 1.27f, g->speed_var += 200.0f;
        g->radius *= 0.6f, g->life *= 0.7f, g->jitter += 0.03f, g->spread_per += 0.04f, g->knockback *= 0.3f;
        LOOK(0xf28cff, g->style, SFX_SHOT_NEEDLER, 1.0f);
    }
    if (upg_has(UPG_W_PRISM)) { /* every shot splits in three mid-flight (bullets.c) */
        g->flags |= PB_SPLIT, g->damage *= 0.65f, g->cooldown *= 1.5f;
        LOOK(0xd8a0ff, g->style, SFX_SHOT_NEEDLER, 1.3f);
    }
    if (upg_has(UPG_W_SPIRAL)) { /* shots circle the ship in a widening spiral: no aiming, a wall of shots */
        g->flags |= PB_ORBIT, g->damage *= 1.5f, g->life *= 2.2f, g->pierce += 1;
        LOOK(0x8affc0, g->style, g->sfx, g->pitch);
    }
    if (upg_has(UPG_W_SCATTER)) { /* a shotgun: many pellets, short range */
        g->mult *= 8, g->damage *= 0.8f, g->cooldown *= 2.8f, g->life *= 0.36f, g->speed *= 0.77f, g->speed_var += 400.0f;
        g->spread += 0.55f, g->spread_per = fminf(g->spread_per, 0.02f), g->jitter += 0.02f, g->knockback *= 2.2f;
        g->parallel = 0.0f, g->recoil = true;
        LOOK(0xffa94d, g->style, SFX_SHOT_SCATTER, 1.0f);
    }
    if (upg_has(UPG_W_VULCAN)) { /* spins up while firing (weapons_update) */
        g->cooldown *= 1.5f / (1.0f + 3.0f * G.pl.spin), /* up to 4x faster once spun up */ g->damage *= 0.8f, g->jitter += 0.06f, g->parallel = 8.0f, g->speed_var += 100.0f;
        LOOK(0xffe08a, g->style, SFX_SHOT_BLASTER, 0.8f);
    }
    if (upg_has(UPG_W_WAVE)) { /* twin shots weaving in mirror */
        g->mult *= 2, g->flags |= PB_WAVE, g->pierce += 1, g->damage *= 1.15f, g->cooldown *= 2.0f;
        g->radius *= 1.4f, g->speed *= 0.8f, g->life *= 0.77f, g->parallel = 0.0f;
        LOOK(0x5dffd8, BS_WAVE, SFX_SHOT_NEEDLER, 0.7f);
    }
    if (upg_has(UPG_W_ARC)) { /* hits chain lightning (weapon_on_hit) */
        g->flags |= PB_ARC, g->damage *= 1.3f, g->cooldown *= 1.6f, g->speed *= 1.2f, g->radius *= 0.8f, g->life *= 0.45f;
        LOOK(0x9db8ff, BS_ARC, SFX_DEFLECT, 0.55f);
    }
    if (upg_has(UPG_W_CLUSTER)) { /* shells burst into fragments (weapon_bullet_death) */
        g->flags |= PB_CLUSTER, g->damage *= 1.8f, g->cooldown *= 3.5f, g->life *= 0.5f, g->speed *= 0.6f, g->radius *= 1.4f;
        g->spread_per += 0.13f;
        LOOK(0xff5fd2, BS_CLUSTER, SFX_SHOT_BOUNCER, 0.75f);
    }
    if (upg_has(UPG_W_FLAMER)) { /* short-range fire stream that burns */
        g->flags |= PB_FLAME, g->cooldown *= 0.4f, g->count += 1, g->damage *= 0.3f, g->life *= 0.38f, g->speed *= 0.63f;
        g->speed_var += 300.0f, g->spread += 0.3f, g->jitter += 0.12f, g->pierce += 2, g->radius *= 1.4f;
        g->knockback *= 0.12f, g->parallel = 0.0f;
        LOOK(0xff7a1a, BS_FLAME, SFX_SHOT_FLAMER, 1.0f);
    }
    if (upg_has(UPG_W_CHAKRAM)) { /* spinning blades that come back */
        g->flags |= PB_RETURNS, g->pierce = PIERCE_ALL, g->damage *= 2.4f, g->cooldown *= 4.0f, g->radius *= 2.2f;
        g->life *= 0.4f, g->speed *= 0.67f, g->spread_per += 0.27f, g->parallel = 0.0f;
        LOOK(0xa8ff5a, BS_CHAKRAM, SFX_SWORD, 1.5f);
    }
    if (upg_has(UPG_W_GRAVITON)) { /* heavy orbs dragging enemies along, imploding at the end */
        g->flags |= PB_PULL | PB_EXPLOSIVE, g->damage *= 2.4f, g->cooldown *= 5.0f, g->speed *= 0.28f, g->radius *= 2.4f;
        g->life *= 1.7f, g->pierce += 3, g->knockback = 0.0f, g->spread_per += 0.2f;
        LOOK(0x8a5aff, BS_GRAVITON, SFX_SHOT_BOUNCER, 0.5f);
    }
    if (upg_has(UPG_W_ROCKETS)) { /* slow start, then fast, and boom (weapon_bullet_death) */
        g->flags |= PB_EXPLOSIVE | PB_ACCEL, g->damage *= 3.6f, g->cooldown *= 6.0f, g->speed *= 0.25f, g->speed_var += 40.0f;
        g->life *= 1.45f, g->homing += 1.2f, g->radius *= 1.4f, g->spread_per += 0.15f;
        LOOK(0xffb04d, BS_ROCKET, SFX_SHOT_ROCKET, 1.0f);
    }
    if (upg_has(UPG_W_LASER)) { /* a stream of beam segments: a continuous ray through everything */
        g->pierce = PIERCE_ALL, g->cooldown *= 0.4f, g->damage *= 0.42f, g->speed *= 2.6f, g->life *= 0.6f, g->radius *= 0.8f;
        g->jitter = 0.0f, g->speed_var = 0.0f, g->knockback *= 0.15f, g->parallel = fminf(g->parallel, 6.0f);
        LOOK(0xff4060, BS_LASER, SFX_SHOT_FLAMER, 1.7f);
    }
    if (upg_has(UPG_W_RAILGUN)) { /* one devastating rail through everything */
        g->pierce = PIERCE_ALL, g->damage *= 9.0f, g->cooldown *= 7.0f, g->speed *= 2.8f, g->life *= 0.55f;
        g->knockback *= 3.2f, g->jitter = 0.0f, g->parallel = 12.0f, g->spread_per += 0.02f;
        LOOK(0xe0f4ff, BS_RAIL, SFX_SHOT_RAIL, 1.0f);
    }
    float cd = clampf(g->cooldown, GUN_CD_MIN, GUN_CD_MAX); /* the vulcan's spin included */
    if (upg_has(UPG_W_LASER)) cd = fminf(cd, 0.04f); /* a beam stays continuous whatever it is mixed with (same damage per second) */
    g->damage *= cd / g->cooldown;
    g->cooldown = cd;
    g->speed = clampf(g->speed, 120.0f, 6000.0f);
    /* Short-range mods stacked on each other would leave shots that reach nothing. */
    float reach = MIN_REACH * ((g->flags & PB_RETURNS) ? 2.0f : 1.0f);
    if (g->speed * g->life < reach) g->life = reach / g->speed;
    g->life = clampf(g->life, 0.12f, 2.5f);
    if (syn(SYN_SAW_HALO)) g->life = 4.0f; /* the saws stay around the ship (bullets.c keeps them in a ring) */
    g->radius = clampf(g->radius, 2.0f, 30.0f);
    if (g->pierce > PIERCE_ALL) g->pierce = PIERCE_ALL;
}
#undef LOOK

static int volley_size(const Gun *g, float *dmg_scale) {
    int n = (g->count + G.pl.extra_proj) * g->mult;
    *dmg_scale = 1.0f;
    if (n > MAX_VOLLEY) {
        *dmg_scale = (float)n / (float)MAX_VOLLEY;
        n = MAX_VOLLEY;
    }
    return n < 1 ? 1 : n;
}

float gun_volley_shots(void) {
    Gun g;
    float k;
    gun_profile(&g);
    return (float)volley_size(&g, &k);
}

/* How far a main gun shot flies (the bot closes in on what it cannot reach). */
float gun_reach(void) {
    Gun g;
    gun_profile(&g);
    return g.speed * G.pl.shot_speed_mult * g.life * G.pl.range_mult;
}

int gun_main_mod(void) {
    if (sword_on()) return UPG_W_SWORD;
    /* most striking first, as in gun_profile */
    static const int order[] = {UPG_W_RAILGUN, UPG_W_LASER, UPG_W_ROCKETS, UPG_W_GRAVITON, UPG_W_CHAKRAM, UPG_W_FLAMER, UPG_W_CLUSTER, UPG_W_ARC,
                                UPG_W_WAVE, UPG_W_VULCAN, UPG_W_SCATTER, UPG_W_SPIRAL, UPG_W_PRISM, UPG_W_NEEDLER, UPG_W_SEEKER, UPG_W_BOUNCER};
    for (int i = 0; i < ARRAY_LEN(order); i++)
        if (upg_has(order[i])) return order[i];
    return -1;
}

/* ---- Special shots -------------------------------------------------------- */

void weapon_on_hit(PBullet *b, Enemy *e, float dmg) {
    if (b->flags & PB_FLAME) {
        e->burn_dps = fmaxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, dmg * 4.0f);
        e->burn_t = fmaxf(e->burn_t, 1.5f);
    }
    if ((b->flags & PB_ARC) && !(b->flags & PB_FRAGMENT)) chain_lightning(e, dmg * 0.7f, 3, 300.0f);
}

/* Called once when a shot dies, whatever killed it (enemy, wall, end of range). */
void weapon_bullet_death(PBullet *b) {
    /* PLASMA CUTTER: where the beam ends, the ground burns. EVENT HORIZON: a rail-graviton shot
     * collapses into a vortex. (The main gun's shots only: by-products and extra shots do not.) */
    if (b->style == BS_LASER && !(b->flags & PB_BYPRODUCT) && (G.ticks % 3) == 0 && syn(SYN_PLASMA))
        upg_fire_patch(b->pos, 6.0f * G.pl.dmg_mult, 1.2f);
    if ((b->style == BS_RAIL || b->style == BS_GRAVITON) && (b->flags & PB_PULL) && !(b->flags & PB_BYPRODUCT) && syn(SYN_EVENT_HORIZON))
        upg_vortex(b->pos, 1.0f, 50.0f * G.pl.dmg_mult);
    if (b->flags & PB_EXPLOSIVE) {
        explode_at(b->pos, 105.0f * sqrtf(G.pl.bullet_size), b->damage, NULL, 0xffb04d);
        sfx_play_at(SFX_EXPLODE_SMALL, b->pos);
    }
    if (b->flags & PB_CLUSTER) {
        float base = frand(0.0f, TAU_F);
        for (int k = 0; k < 6; k++) {
            PBullet *f = pbullet_spawn(b->pos, v2_scale(v2_angle(base + TAU_F * (float)k / 6.0f), 850.0f), 3.5f,
                                       b->damage * 0.45f, b->col);
            if (!f) break;
            f->life = 0.25f * G.pl.range_mult;
            f->last_hit = b->last_hit;
            f->flags = PB_FRAGMENT;
            f->knockback = 15.0f;
            if (syn(SYN_MIRV)) { /* each fragment is a small homing rocket */
                f->flags |= PB_EXPLOSIVE | PB_ACCEL;
                f->vel = v2_scale(f->vel, 0.45f);
                f->homing = 9.0f;
                f->life = 0.7f;
                f->last_hit = -1;
            }
        }
        fx_particle(b->pos, v2(0, 0), 0.2f, 60.0f, col_a(b->col, 0.7f), 0.0f, PT_RING);
    }
}

/* ---- Sword ---------------------------------------------------------------- */

#define SWORD_SWING_TIME 0.13f
#define SWORD_COOLDOWN 0.36f
#define SWORD_DAMAGE 34.0f
#define SWORD_KNOCKBACK 260.0f
#define SWORD_COLOR 0xc9d4ff

/*
 * The sword combines with every shot-shaping upgrade owned:
 *  LASER -> lightsaber (x2.5 damage, longer, burns), RAILGUN -> greatsword (x2.4 damage, longer, slower),
 *  NEEDLER -> rapier (faster, lighter), SCATTER -> wider arc, ORBIT -> full-circle whirlwind,
 *  VULCAN -> swings speed up while attacking, BOUNCER -> huge knockback, GRAVITON -> pulls enemies in,
 *  FLAMER / ARC / ROCKETS / CLUSTER -> the blade burns / chains lightning / explodes / bursts,
 *  and any mod that shapes projectiles (see SWORD_WAVE_MODS) makes every swing launch a blade wave
 *  carrying all of them (it homes, bounces, splits, comes back...).
 */
static const int SWORD_WAVE_MODS[] = {UPG_W_SEEKER, UPG_W_BOUNCER, UPG_W_ROCKETS, UPG_W_CHAKRAM, UPG_W_CLUSTER,
                                      UPG_W_WAVE, UPG_W_PRISM, UPG_W_GRAVITON, UPG_W_SPIRAL};

static uint32_t sword_color(void) {
    return upg_has(UPG_W_LASER) ? 0xff3050 : upg_has(UPG_W_FLAMER) ? 0xff9a4a : upg_has(UPG_W_ARC) ? 0x9db8ff : SWORD_COLOR;
}

float sword_reach(void) {
    const Player *p = &G.pl;
    /* Capped: a huge blade would clear every bullet around the ship. */
    float r = fminf(115.0f * sqrtf(p->range_mult) * sqrtf(p->bullet_size), 170.0f);
    if (upg_has(UPG_W_LASER)) r *= 1.25f;
    if (upg_has(UPG_W_RAILGUN)) r *= 1.3f;
    return r;
}

/* A crescent of energy thrown by the swing; pbullet_arm gives it every shot behaviour owned. */
static void sword_wave(float aim, float dmg) {
    const Player *p = &G.pl;
    V2 f = v2_angle(aim);
    PBullet *b = pbullet_spawn(v2_add(p->pos, v2_scale(f, 34.0f)), v2_scale(f, 950.0f * p->shot_speed_mult), 15.0f, dmg,
                               col_hex(sword_color(), 1));
    if (!b) return;
    b->life = 0.5f * p->range_mult;
    b->pierce = 2;
    b->knockback = 120.0f;
    b->style = BS_CRESCENT;
    pbullet_arm(b, true); /* damage and range come from the swing */
}

static void sword_swing(void) {
    Player *p = &G.pl;
    /* Fire rate counts less for the sword (sqrt), with a floor: a blade swinging non-stop
     * would be a permanent bullet shield. */
    float cd = SWORD_COOLDOWN / sqrtf(p->rate_mult * upg_fire_rate_mult());
    float dmg_k = 1.0f;
    if (upg_has(UPG_W_NEEDLER)) cd *= 0.7f, dmg_k *= 0.75f;  /* rapier */
    if (upg_has(UPG_W_RAILGUN)) cd *= 1.7f, dmg_k *= 2.4f;   /* greatsword */
    if (upg_has(UPG_W_LASER)) dmg_k *= 2.5f;                 /* lightsaber */
    if (upg_has(UPG_W_BOUNCER)) dmg_k *= 1.3f;
    if (upg_has(UPG_W_GRAVITON)) cd *= 1.3f, dmg_k *= 1.4f;
    if (upg_has(UPG_W_VULCAN)) cd /= 1.0f + p->spin;         /* a flurry once spun up */
    cd = fmaxf(cd, 0.16f);
    p->fire_cd += cd;
    float charge = upg_has(UPG_CHARGE) ? p->charge : 0.0f;   /* CHARGE SHOT: one mighty blow */
    p->charge = 0.0f;
    /* Back and forth: every swing goes the other way. Extra projectiles widen the arc. */
    p->swing_dir = p->swing_dir > 0 ? -1 : 1;
    p->swing_len = fminf(SWORD_SWING_TIME, cd * 0.8f);
    p->swing_t = p->swing_len;
    p->swing_a0 = p->aim_angle;
    p->swing_half = fminf(0.95f + 0.1f * (float)p->extra_proj, 1.4f) + (upg_has(UPG_W_SCATTER) ? 0.5f : 0.0f);
    if (upg_has(UPG_W_SPIRAL)) p->swing_half = PI_F; /* whirlwind: all the way around */
    p->swing_reach = sword_reach();
    p->swing_prev = -(float)p->swing_dir * p->swing_half;
    p->swing_dmg = SWORD_DAMAGE * p->dmg_mult * upg_volley_damage_mult() * dmg_k;
    if (active_strength_on()) p->swing_dmg *= 4.0f; /* STRENGTH: one blade, four times the bite */
    p->swing_id++;
    upg_on_volley(p->pos, p->aim_angle, p->swing_dmg * 0.35f, col_hex(sword_color(), 1));
    for (int i = 0; i < ARRAY_LEN(SWORD_WAVE_MODS); i++)
        if (upg_has(SWORD_WAVE_MODS[i])) {
            sword_wave(p->aim_angle, p->swing_dmg * 0.45f);
            break;
        }
    if (syn(SYN_THROWN_SWORD) && (p->swing_id & 1)) { /* every other swing: a blade flies out and comes back */
        V2 f = v2_angle(p->aim_angle);
        PBullet *b = pbullet_spawn(v2_add(p->pos, v2_scale(f, 30.0f)), v2_scale(f, 1000.0f * p->shot_speed_mult), 18.0f,
                                   p->swing_dmg * 0.6f, col_hex(sword_color(), 1));
        if (b) {
            b->life = 0.45f * p->range_mult;
            b->pierce = PIERCE_ALL;
            b->knockback = 150.0f;
            b->style = BS_CHAKRAM;
            b->flags = PB_RETURNS;
            pbullet_arm(b, true);
        }
    }
    p->swing_dmg *= 1.0f + 5.0f * charge;
    if (charge > 0.3f) {
        fx_particle(p->pos, v2(0, 0), 0.25f, p->swing_reach * 2.2f, col_hex(0x9ff0ff, 0.9f), 0.0f, PT_RING);
        fx_shake(0.2f * charge);
    }
    if (upg_has(UPG_W_GRAVITON)) /* the swing sucks everything nearby towards the blade */
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            V2 d = v2_sub(p->pos, e->pos);
            float dist = v2_len(d);
            if (e->hp > 0.0f && !enemy_is_boss(e) && dist > 60.0f && dist < 360.0f) e->vel = v2_add(e->vel, v2_scale(d, 520.0f / dist));
        }
    sfx_play_ex(SFX_SWORD, 1.0f, upg_has(UPG_W_LASER) ? 0.6f : upg_has(UPG_W_RAILGUN) ? 0.75f : 1.0f, 0.0f);
}

/* Is a circle (pos, r) inside the arc swept by the blade between relative angles lo and hi? */
static bool in_sweep(V2 center, V2 pos, float r, float lo, float hi) {
    const Player *p = &G.pl;
    V2 d = v2_sub(pos, center);
    float dist = v2_len(d);
    if (dist - r > p->swing_reach) return false;
    if (dist <= r + SWORD_INNER) return true; /* right on top of the ship */
    float rel = angle_wrap(v2_heading(d) - p->swing_a0);
    float tol = asinf(fminf(1.0f, r / dist));
    return rel >= lo - tol && rel <= hi + tol;
}

/* Everything on the path of a blade held at `center`: enemies (once per swing) and enemy bullets.
 * `clone`: the SHADOW CLONE's blade (BLADE DANCER), at dmg_k of the real one. */
static void sword_sweep(V2 center, float lo, float hi, float dmg_k, bool clone) {
    Player *p = &G.pl;
    SDL_FColor col = col_hex(clone ? 0x9aa8ff : sword_color(), 1);
    Gun g;
    gun_profile(&g); /* the other shot upgrades lend the blade their burn / lightning / explosions */
    int blade_flags = g.flags & (PB_FLAME | PB_ARC | PB_EXPLOSIVE | PB_CLUSTER);
    if (upg_has(UPG_W_LASER)) blade_flags |= PB_FLAME; /* a lightsaber burns what it cuts */
    float knock = SWORD_KNOCKBACK * (upg_has(UPG_W_BOUNCER) ? 2.2f : 1.0f);

    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        int *seen = clone ? &e->clone_swing : &e->sword_swing;
        if (e->hp <= 0.0f || *seen == p->swing_id || !in_sweep(center, e->pos, e->radius, lo, hi)) continue;
        *seen = p->swing_id;
        V2 dir = v2_norm(v2_sub(e->pos, center));
        PBullet hit = {.pos = e->pos, .vel = v2_scale(dir, 900.0f), .radius = 6.0f, .damage = p->swing_dmg * dmg_k,
                       .knockback = knock, .last_hit = e->uid, .flags = blade_flags & (PB_FLAME | PB_ARC), .col = col};
        upg_on_bullet_spawn(&hit); /* critical hits, IMPACT... like any shot */
        hit.flags |= blade_flags & (PB_EXPLOSIVE | PB_CLUSTER);
        hit.radius = 6.0f;
        float dmg = upg_hit_damage(&hit, e);
        enemy_damage(e, dmg, v2_scale(dir, hit.knockback));
        upg_after_hit(&hit, e, dmg);
        weapon_on_hit(&hit, e, dmg);
        if (hit.flags & (PB_EXPLOSIVE | PB_CLUSTER)) { /* the cut explodes / bursts, at half strength */
            hit.damage = dmg * 0.5f;
            weapon_bullet_death(&hit);
        }
        sfx_play_at(SFX_HIT, e->pos);
        fx_particle(e->pos, v2(0, 0), 0.12f, e->radius * 2.5f, col_a(col, 0.8f), 0.0f, PT_GLOW);
        for (int s = 0; s < 4; s++)
            fx_particle(e->pos, v2_scale(v2_rotate(dir, frand(-0.9f, 0.9f)), frand(200, 500)), 0.18f, 2.5f, col, 4.0f, PT_SPARK);
    }
    /* Blocks: the blade breaks them like shots do (once per swing). */
    for (int i = 0; i < G.room.nobs && !clone; i++) {
        Obstacle *o = &G.room.obs[i];
        if (!obstacle_breakable(o) || o->hp <= 0.0f || o->swing_id == p->swing_id) continue;
        V2 c = v2(o->r.x + o->r.w * 0.5f, o->r.y + o->r.h * 0.5f);
        if (!in_sweep(center, c, o->r.w * 0.6f, lo, hi)) continue;
        o->swing_id = p->swing_id;
        room_block_damage(i, p->swing_dmg * dmg_k);
        for (int s = 0; s < 3; s++)
            fx_particle(c, v2_scale(v2_angle(frand(0, TAU_F)), frand(150, 350)), 0.18f, 2.5f, col, 4.0f, PT_SPARK);
    }
    bool parry = syn(SYN_PARRY); /* with DEFLECTOR SHELLS the blade sends the bullets back */
    int cut = 0;
    for (int i = 0; i < G.neb; i++) {
        EBullet *b = &G.eb[i];
        if (b->life <= 0.0f || !in_sweep(center, b->pos, b->radius + 4.0f, lo, hi)) continue;
        fx_particle(b->pos, v2(0, 0), 0.15f, b->radius * 3.0f, col_a(b->col, 0.8f), 0.0f, PT_GLOW);
        if (parry) upg_return_bullet(b, center);
        else b->life = -1.0f; /* removed by the bullet loop */
        cut++;
        if (cut <= 12) fx_particle(b->pos, v2_scale(v2_angle(frand(0, TAU_F)), frand(100, 300)), 0.15f, 2.0f, col, 4.0f, PT_SPARK);
    }
    if (cut) sfx_play_at(SFX_DEFLECT, center);
}

static void sword_update(void) {
    Player *p = &G.pl;
    if (p->swing_t <= 0.0f) return;
    p->swing_t -= DT;
    float k = 1.0f - fmaxf(p->swing_t, 0.0f) / p->swing_len;
    k = 1.0f - (1.0f - k) * (1.0f - k); /* fast start, eases out */
    float cur = (float)p->swing_dir * p->swing_half * (2.0f * k - 1.0f);
    float lo = fminf(p->swing_prev, cur), hi = fmaxf(p->swing_prev, cur);
    p->swing_prev = cur;
    sword_sweep(p->pos, lo, hi, 1.0f, false);
    if (syn(SYN_BLADE_DANCER)) sword_sweep(upg_clone_pos(), lo, hi, 0.5f, true);
}

void sword_draw(void) {
    const Player *p = &G.pl;
    if (!sword_on()) return;
    SDL_FColor col = col_hex(sword_color(), 1);
    float thick = upg_has(UPG_W_LASER) || upg_has(UPG_W_RAILGUN) ? 1.6f : 1.0f; /* lightsaber / greatsword */
    if (p->swing_t <= 0.0f) {
        /* Resting blade, held at the side and pointing where the knight aims. */
        V2 f = v2_angle(p->aim_angle), s = v2_perp(f);
        V2 hilt = v2_add(p->pos, v2_scale(s, 20.0f));
        if (upg_has(UPG_W_LASER)) { /* the lightsaber hums even at rest */
            draw_blend_add();
            draw_line(hilt, v2_add(hilt, v2_scale(f, 52.0f)), 12.0f + 2.0f * sinf(G.time * 40.0f), col_a(col, 0.3f));
            draw_blend_alpha();
            draw_line(hilt, v2_add(hilt, v2_scale(f, 52.0f)), 3.0f, COL_WHITE);
        }
        draw_line(hilt, v2_add(hilt, v2_scale(f, 46.0f)), 4.0f * thick, col_a(col, 0.85f));
        draw_line(v2_sub(hilt, v2_scale(s, 7.0f)), v2_add(hilt, v2_scale(s, 7.0f)), 3.0f, col_hex(0xffd36b, 1));
        return;
    }
    /* Trail: the part of the arc already swept, fading towards its start. */
    float start = -(float)p->swing_dir * p->swing_half, cur = p->swing_prev;
    const int segs = 14;
    float r0 = SWORD_INNER, r1 = p->swing_reach;
    draw_blend_add();
    for (int i = 0; i < segs; i++) {
        float t0 = (float)i / (float)segs, t1 = (float)(i + 1) / (float)segs;
        float a0 = p->swing_a0 + start + (cur - start) * t0, a1 = p->swing_a0 + start + (cur - start) * t1;
        V2 d0 = v2_angle(a0), d1 = v2_angle(a1);
        SDL_FColor c0 = col_a(col, 0.7f * t0), c1 = col_a(col, 0.7f * t1);
        draw_quad_grad(v2_add(p->pos, v2_scale(d0, r0)), v2_add(p->pos, v2_scale(d0, r1)),
                       v2_add(p->pos, v2_scale(d1, r1)), v2_add(p->pos, v2_scale(d1, r0)),
                       col_a(c0, c0.a * 0.3f), c0, c1, col_a(c1, c1.a * 0.3f));
    }
    V2 dir = v2_angle(p->swing_a0 + cur);
    V2 tip = v2_add(p->pos, v2_scale(dir, r1));
    if (syn(SYN_BLADE_DANCER)) { /* the clone's blade */
        V2 c = upg_clone_pos();
        draw_line(v2_add(c, v2_scale(dir, r0)), v2_add(c, v2_scale(dir, r1)), 5.0f, col_hex(0x9aa8ff, 0.5f));
        draw_glow(v2_add(c, v2_scale(dir, r1)), 14.0f, col_hex(0x9aa8ff, 0.4f));
    }
    draw_glow(tip, 16.0f, col_a(col, 0.4f));
    draw_blend_alpha();
    draw_line(v2_add(p->pos, v2_scale(dir, r0)), tip, 6.0f * thick, COL_WHITE);
    draw_line(v2_add(p->pos, v2_scale(dir, r0)), tip, 2.5f * thick, col);
}


static void fire_volley(void) {
    Player *p = &G.pl;
    Gun g;
    gun_profile(&g);
    float dmg_scale;
    int n = volley_size(&g, &dmg_scale);
    p->fire_cd += g.cooldown / (p->rate_mult * upg_fire_rate_mult());

    float spread = fminf((g.spread + g.spread_per * (float)(n - 1)) * p->spread_mult, 2.6f);
    float charge = upg_has(UPG_CHARGE) ? p->charge : 0.0f; /* CHARGE SHOT: spent on this volley */
    p->charge = 0.0f;
    float dmg = g.damage * dmg_scale * p->dmg_mult * upg_volley_damage_mult() * (1.0f + 5.0f * charge);
    SDL_FColor col = col_hex(g.color, 1);
    V2 fwd = v2_angle(p->aim_angle), side = v2_perp(fwd);
    V2 muzzle = v2_add(p->pos, v2_scale(fwd, 22.0f));

    /* STRENGTH: the whole volley four times, fanned slightly. */
    int copies = active_strength_on() ? 4 : 1;
    for (int i = 0; i < n * copies; i++) {
        int c = i / n;
        float off = copies > 1 ? ((float)c - (float)(copies - 1) * 0.5f) * 0.07f : 0.0f;
        float t = n > 1 ? (float)(i % n) / (float)(n - 1) - 0.5f : 0.0f;
        float a = p->aim_angle + off + t * spread + frand(-g.jitter, g.jitter) * fminf(p->spread_mult, 1.0f);
        V2 pos = v2_add(muzzle, v2_scale(side, g.parallel * t * (float)(n - 1)));
        float spd = (g.speed + frand(0.0f, g.speed_var)) * p->shot_speed_mult;
        PBullet *b = pbullet_spawn(pos, v2_scale(v2_angle(a), spd), g.radius, dmg, col);
        if (!b) break;
        b->life = g.life * p->range_mult;
        b->pierce = g.pierce + p->pierce;
        b->bounces = g.bounce + p->bounce;
        b->homing = g.homing + p->homing;
        b->knockback = g.knockback;
        b->style = g.style;
        b->flags |= g.flags;
        if ((g.flags & PB_WAVE) && (i & 1)) b->flags |= PB_FLIP;
        b->armed = true;
        b->radius *= 1.0f + 0.8f * charge;
        upg_on_bullet_spawn(b);
    }
    if (charge > 0.3f) {
        fx_particle(muzzle, v2(0, 0), 0.25f, 60.0f + 140.0f * charge, col_hex(0x9ff0ff, 0.9f), 0.0f, PT_RING);
        fx_shake(0.15f * charge);
        sfx_play_ex(SFX_SNIPER_FIRE, 0.7f, 1.6f - 0.6f * charge, 0.0f);
    }
    upg_on_volley(muzzle, p->aim_angle, dmg / (1.0f + 5.0f * charge), col);
    if (charge >= 0.99f && syn(SYN_OVERLOAD)) upg_mega_shot(muzzle, p->aim_angle, dmg * 3.0f); /* a full charge fires the capacitor too */

    fx_particle(muzzle, v2(0, 0), 0.05f, 18.0f + (float)(n * copies) * 2.0f, col_a(col, 0.8f), 0.0f, PT_GLOW);
    float pan = clampf((p->pos.x - ARENA_CX) / (ARENA_X1 - ARENA_CX), -1.0f, 1.0f) * 0.6f;
    sfx_play_ex(g.sfx, 1.0f, g.pitch, pan);
    /* Recoil: a push back per volley, smaller for fast guns (so it stays the same per second) and
     * for a heavy ship (DRIFT malus: slower to stop, so pushed less) - it never beats the engines. */
    float heavy = fmaxf(0.25f, 1.0f - malus_pct(MAL_DRIFT));
    if (g.recoil) {
        p->vel = v2_sub(p->vel, v2_scale(fwd, 90.0f * fminf(1.0f, g.cooldown / 0.22f) * heavy));
        fx_shake(0.04f);
    }
    int kick = malus(MAL_RECOIL);
    if (kick > 3) kick = 3;
    if (kick > 0) p->vel = v2_sub(p->vel, v2_scale(fwd, 70.0f * (float)kick * fminf(1.0f, g.cooldown / 0.08f) * heavy));
}

void pbullet_arm(PBullet *b, bool scaled) {
    const Player *p = &G.pl;
    if (!scaled) {
        b->damage *= p->dmg_mult * upg_volley_damage_mult();
        b->life *= p->range_mult;
        b->vel = v2_scale(b->vel, p->shot_speed_mult);
    }
    /* The shot-shaping upgrades: their behaviours, pierce, bounces and homing. */
    Gun g;
    gun_profile(&g);
    b->flags |= g.flags & ~(PB_FLIP | PB_ORBIT);
    if ((g.flags & PB_WAVE) && chance(0.5f)) b->flags |= PB_FLIP;
    /* Only shots leaving the ship circle around it (a turret's would never reach its target). */
    if ((g.flags & PB_ORBIT) && v2_dist2(b->pos, p->pos) < 90.0f * 90.0f) b->flags |= PB_ORBIT;
    b->pierce += p->pierce + g.pierce;
    if (b->pierce > PIERCE_ALL) b->pierce = PIERCE_ALL;
    b->bounces += p->bounce + g.bounce;
    b->homing += p->homing + g.homing;
    b->armed = true;
    upg_on_bullet_spawn(b);
}

void weapon_echo(V2 from, float aim, float dmg) {
    const Player *p = &G.pl;
    Gun g;
    float k;
    gun_profile(&g);
    int count = volley_size(&g, &k);
    count = count > 4 ? 4 : count;
    for (int i = 0; i < count; i++) {
        float a = aim + ((float)i - (float)(count - 1) * 0.5f) * 0.08f;
        PBullet *b = pbullet_spawn(from, v2_scale(v2_angle(a), g.speed * p->shot_speed_mult), 4.0f, dmg, col_hex(0xb0a0ff, 1));
        if (!b) return;
        b->life = fmaxf(g.life, 0.4f) * p->range_mult;
        b->knockback = 30.0f;
        pbullet_arm(b, true); /* damage and range come from the volley */
    }
}

static V2 drone_pos(int i) {
    const Player *p = &G.pl;
    float a = G.time * 2.4f + TAU_F * (float)i / (float)(p->drones > 0 ? p->drones : 1);
    return v2_add(p->pos, v2_scale(v2_angle(a), 52.0f));
}
V2 weapon_drone_pos(int i) { return drone_pos(i); }

static void update_drones(void) {
    Player *p = &G.pl;
    if (p->drones <= 0) return;
    p->drone_cd -= DT;
    while (p->drone_cd <= 0.0f) {
        p->drone_cd += 0.2f / (float)p->drones; /* each drone fires every 0.2 s */
        p->drone_idx = (p->drone_idx + 1) % p->drones;
        V2 dp = drone_pos(p->drone_idx);
        Enemy *t = enemy_nearest(dp, 900.0f);
        if (!t) continue;
        V2 dir = v2_norm(v2_sub(t->pos, dp));
        PBullet *b = pbullet_spawn(dp, v2_scale(dir, 1300.0f), 4.0f, 4.5f, col_hex(0xb6ffb0, 1));
        if (!b) return;
        b->life = 1.0f;
        b->knockback = 20.0f;
        if (syn(SYN_LIGHTNING_ROD)) b->flags |= PB_ARC; /* with TESLA FIELD: drone shots chain lightning */
        pbullet_arm(b, false);
        sfx_play_at(SFX_DRONE_SHOT, dp);
    }
}

void weapons_update(void) {
    Player *p = &G.pl;
    bool sword = sword_on();
    p->fire_cd -= DT;
    /* VULCAN: spins up over ~1 s of firing, winds down when released. */
    bool trigger = weapon_trigger();
    bool spinning = trigger && upg_has(UPG_W_VULCAN);
    p->spin = clampf(p->spin + (spinning ? DT / 1.0f : -DT / 0.6f), 0.0f, 1.0f);
    if (malus_fire_gate(trigger)) {
        while (p->fire_cd <= 0.0f) {
            if (sword) sword_swing();
            else fire_volley();
        }
    } else if (p->fire_cd < 0.0f) {
        p->fire_cd = 0.0f;
    }
    /* TESLA BEAM (LASER + ARC CASTER): while the trigger is held, bolts leap to the nearest enemy. */
    if (trigger && syn(SYN_TESLA_BEAM) && p->jam_t <= 0.0f && p->cold_t <= 0.0f) {
        p->bolt_cd -= DT;
        Enemy *t = p->bolt_cd <= 0.0f ? enemy_nearest(p->pos, 440.0f) : NULL;
        if (t) {
            p->bolt_cd = 0.25f;
            upg_bolt(p->pos, t, 9.0f * p->dmg_mult, 2);
        }
    }
    if (!sword) p->swing_t = 0.0f;
    sword_update();
    update_drones();
}

void drones_draw(void) {
    const Player *p = &G.pl;
    SDL_FColor c = col_hex(0xb6ffb0, 1);
    for (int i = 0; i < p->drones; i++) {
        V2 dp = drone_pos(i);
        draw_blend_add();
        draw_glow(dp, 20.0f, col_a(c, 0.35f));
        draw_blend_alpha();
        draw_ngon(dp, 8.0f, 4, G.time * 5.0f, col_hex(0x1d4a1d, 1));
        draw_ngon_outline(dp, 8.0f, 4, G.time * 5.0f, 2.0f, c);
    }
}
