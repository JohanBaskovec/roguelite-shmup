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
    "scatter", "needler", "seeker", "bouncer", "railgun", "flamer", "rockets", "chakram", "cluster", "arc", "vulcan", "wave", "sword",
};

const char *weapon_option_name(int u) { return upgrade_is_weapon(u) ? OPTION_NAMES[u - UPG_W_SCATTER] : "?"; }

int weapon_mod_by_name(const char *name) {
    for (int u = UPG_W_SCATTER; u <= UPG_W_SWORD; u++)
        if (!SDL_strcmp(name, weapon_option_name(u))) return u;
    return -1;
}

bool sword_on(void) { return upg_has(UPG_W_SWORD) > 0; }

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
    if (upg_has(UPG_W_ROCKETS)) { /* slow start, then fast, and boom (weapon_bullet_death) */
        g->flags |= PB_EXPLOSIVE | PB_ACCEL, g->damage *= 3.6f, g->cooldown *= 6.0f, g->speed *= 0.25f, g->speed_var += 40.0f;
        g->life *= 1.45f, g->homing += 1.2f, g->radius *= 1.4f, g->spread_per += 0.15f;
        LOOK(0xffb04d, BS_ROCKET, SFX_SHOT_ROCKET, 1.0f);
    }
    if (upg_has(UPG_W_RAILGUN)) { /* one devastating rail through everything */
        g->pierce = PIERCE_ALL, g->damage *= 9.0f, g->cooldown *= 7.0f, g->speed *= 2.8f, g->life *= 0.55f;
        g->knockback *= 3.2f, g->jitter = 0.0f, g->parallel = 12.0f, g->spread_per += 0.02f;
        LOOK(0xe0f4ff, BS_RAIL, SFX_SHOT_RAIL, 1.0f);
    }
    float cd = clampf(g->cooldown, GUN_CD_MIN, GUN_CD_MAX); /* the vulcan's spin included */
    g->damage *= cd / g->cooldown;
    g->cooldown = cd;
    g->speed = clampf(g->speed, 120.0f, 6000.0f);
    /* Short-range mods stacked on each other would leave shots that reach nothing. */
    float reach = MIN_REACH * ((g->flags & PB_RETURNS) ? 2.0f : 1.0f);
    if (g->speed * g->life < reach) g->life = reach / g->speed;
    g->life = clampf(g->life, 0.12f, 2.5f);
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

int gun_main_mod(void) {
    if (sword_on()) return UPG_W_SWORD;
    /* most striking first, as in gun_profile */
    static const int order[] = {UPG_W_RAILGUN, UPG_W_ROCKETS, UPG_W_CHAKRAM, UPG_W_FLAMER, UPG_W_CLUSTER, UPG_W_ARC,
                                UPG_W_WAVE, UPG_W_VULCAN, UPG_W_SCATTER, UPG_W_NEEDLER, UPG_W_SEEKER, UPG_W_BOUNCER};
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

float sword_reach(void) {
    const Player *p = &G.pl;
    /* Capped: a huge blade would clear every bullet around the ship. */
    return fminf(115.0f * sqrtf(p->range_mult) * sqrtf(p->bullet_size), 170.0f);
}

static void sword_swing(void) {
    Player *p = &G.pl;
    /* Fire rate counts less for the sword (sqrt), with a floor: a blade swinging non-stop
     * would be a permanent bullet shield. */
    float cd = SWORD_COOLDOWN / sqrtf(p->rate_mult * upg_fire_rate_mult());
    cd = fmaxf(cd, 0.16f);
    p->fire_cd += cd;
    /* Back and forth: every swing goes the other way. Extra projectiles widen the arc. */
    p->swing_dir = p->swing_dir > 0 ? -1 : 1;
    p->swing_len = fminf(SWORD_SWING_TIME, cd * 0.8f);
    p->swing_t = p->swing_len;
    p->swing_a0 = p->aim_angle;
    p->swing_half = fminf(0.95f + 0.1f * (float)p->extra_proj, 1.4f);
    p->swing_reach = sword_reach();
    p->swing_prev = -(float)p->swing_dir * p->swing_half;
    p->swing_dmg = SWORD_DAMAGE * p->dmg_mult * upg_volley_damage_mult();
    if (active_strength_on()) p->swing_dmg *= 4.0f; /* STRENGTH: one blade, four times the bite */
    p->swing_id++;
    upg_on_volley(p->pos, p->aim_angle, p->swing_dmg * 0.35f, col_hex(SWORD_COLOR, 1));
    sfx_play_at(SFX_SWORD, p->pos);
}

/* Is a circle (pos, r) inside the arc swept by the blade between relative angles lo and hi? */
static bool in_sweep(V2 pos, float r, float lo, float hi) {
    const Player *p = &G.pl;
    V2 d = v2_sub(pos, p->pos);
    float dist = v2_len(d);
    if (dist - r > p->swing_reach) return false;
    if (dist <= r + SWORD_INNER) return true; /* right on top of the ship */
    float rel = angle_wrap(v2_heading(d) - p->swing_a0);
    float tol = asinf(fminf(1.0f, r / dist));
    return rel >= lo - tol && rel <= hi + tol;
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
    SDL_FColor col = col_hex(SWORD_COLOR, 1);
    Gun g;
    gun_profile(&g); /* other weapon mods lend the blade their burn / lightning */

    /* Everything on the blade's path: enemies (once per swing) and enemy bullets. */
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f || e->sword_swing == p->swing_id || !in_sweep(e->pos, e->radius, lo, hi)) continue;
        e->sword_swing = p->swing_id;
        V2 dir = v2_norm(v2_sub(e->pos, p->pos));
        PBullet hit = {.pos = e->pos, .vel = v2_scale(dir, 900.0f), .radius = 6.0f, .damage = p->swing_dmg,
                       .knockback = SWORD_KNOCKBACK, .last_hit = e->uid, .flags = g.flags & (PB_FLAME | PB_ARC), .col = col};
        float dmg = upg_hit_damage(&hit, e);
        enemy_damage(e, dmg, v2_scale(dir, hit.knockback));
        upg_after_hit(&hit, e, dmg);
        weapon_on_hit(&hit, e, dmg);
        sfx_play_at(SFX_HIT, e->pos);
        fx_particle(e->pos, v2(0, 0), 0.12f, e->radius * 2.5f, col_a(col, 0.8f), 0.0f, PT_GLOW);
        for (int s = 0; s < 4; s++)
            fx_particle(e->pos, v2_scale(v2_rotate(dir, frand(-0.9f, 0.9f)), frand(200, 500)), 0.18f, 2.5f, col, 4.0f, PT_SPARK);
    }
    int cut = 0;
    for (int i = 0; i < G.neb; i++) {
        EBullet *b = &G.eb[i];
        if (b->life <= 0.0f || !in_sweep(b->pos, b->radius + 4.0f, lo, hi)) continue;
        b->life = -1.0f; /* removed by the bullet loop */
        cut++;
        fx_particle(b->pos, v2(0, 0), 0.15f, b->radius * 3.0f, col_a(b->col, 0.8f), 0.0f, PT_GLOW);
        if (cut <= 12) fx_particle(b->pos, v2_scale(v2_angle(frand(0, TAU_F)), frand(100, 300)), 0.15f, 2.0f, col, 4.0f, PT_SPARK);
    }
    if (cut) sfx_play_at(SFX_DEFLECT, p->pos);
}

void sword_draw(void) {
    const Player *p = &G.pl;
    if (!sword_on()) return;
    SDL_FColor col = col_hex(SWORD_COLOR, 1);
    if (p->swing_t <= 0.0f) {
        /* Resting blade, held at the side and pointing where the knight aims. */
        V2 f = v2_angle(p->aim_angle), s = v2_perp(f);
        V2 hilt = v2_add(p->pos, v2_scale(s, 20.0f));
        draw_line(hilt, v2_add(hilt, v2_scale(f, 46.0f)), 4.0f, col_a(col, 0.85f));
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
    draw_glow(tip, 16.0f, col_a(col, 0.4f));
    draw_blend_alpha();
    draw_line(v2_add(p->pos, v2_scale(dir, r0)), tip, 6.0f, COL_WHITE);
    draw_line(v2_add(p->pos, v2_scale(dir, r0)), tip, 2.5f, col);
}


static void fire_volley(void) {
    Player *p = &G.pl;
    Gun g;
    gun_profile(&g);
    float dmg_scale;
    int n = volley_size(&g, &dmg_scale);
    p->fire_cd += g.cooldown / (p->rate_mult * upg_fire_rate_mult());

    float spread = fminf((g.spread + g.spread_per * (float)(n - 1)) * p->spread_mult, 2.6f);
    float dmg = g.damage * dmg_scale * p->dmg_mult * upg_volley_damage_mult();
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
        upg_on_bullet_spawn(b);
    }
    upg_on_volley(muzzle, p->aim_angle, dmg, col);

    fx_particle(muzzle, v2(0, 0), 0.05f, 18.0f + (float)(n * copies) * 2.0f, col_a(col, 0.8f), 0.0f, PT_GLOW);
    float pan = clampf((p->pos.x - ARENA_CX) / (ARENA_X1 - ARENA_CX), -1.0f, 1.0f) * 0.6f;
    sfx_play_ex(g.sfx, 1.0f, g.pitch, pan);
    if (g.recoil) {
        p->vel = v2_sub(p->vel, v2_scale(fwd, 90.0f));
        fx_shake(0.04f);
    }
}

void weapon_echo(float aim, float dmg) {
    const Player *p = &G.pl;
    Gun g;
    float k;
    gun_profile(&g);
    int count = volley_size(&g, &k);
    count = count > 4 ? 4 : count;
    for (int i = 0; i < count; i++) {
        float a = aim + ((float)i - (float)(count - 1) * 0.5f) * 0.08f;
        PBullet *b = pbullet_spawn(p->pos, v2_scale(v2_angle(a), g.speed * p->shot_speed_mult), 4.0f, dmg, col_hex(0xb0a0ff, 1));
        if (!b) return;
        b->life = fmaxf(g.life, 0.4f) * p->range_mult;
        b->pierce = p->pierce;
        b->bounces = p->bounce / 2;
        b->homing = p->homing;
        b->knockback = 30.0f;
        upg_on_bullet_spawn(b);
    }
}

static V2 drone_pos(int i) {
    const Player *p = &G.pl;
    float a = G.time * 2.4f + TAU_F * (float)i / (float)p->drones;
    return v2_add(p->pos, v2_scale(v2_angle(a), 52.0f));
}

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
        PBullet *b = pbullet_spawn(dp, v2_scale(dir, 1300.0f * p->shot_speed_mult), 4.0f, 4.5f * p->dmg_mult,
                                   col_hex(0xb6ffb0, 1));
        if (!b) return;
        b->life = 1.0f * p->range_mult;
        b->pierce = p->pierce;
        b->bounces = p->bounce / 2;
        b->homing = p->homing;
        b->knockback = 20.0f;
        upg_on_bullet_spawn(b);
        sfx_play_at(SFX_DRONE_SHOT, dp);
    }
}

void weapons_update(void) {
    Player *p = &G.pl;
    bool sword = sword_on();
    p->fire_cd -= DT;
    /* VULCAN: spins up over ~1 s of firing, winds down when released. */
    bool spinning = G.in.fire && !sword && upg_has(UPG_W_VULCAN);
    p->spin = clampf(p->spin + (spinning ? DT / 1.0f : -DT / 0.6f), 0.0f, 1.0f);
    if (G.in.fire) {
        while (p->fire_cd <= 0.0f) {
            if (sword) sword_swing();
            else fire_volley();
        }
    } else if (p->fire_cd < 0.0f) {
        p->fire_cd = 0.0f;
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
