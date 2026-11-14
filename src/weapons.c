#include "draw.h"
#include "game.h"
#include "audio.h"

#include <stdio.h>

const WeaponDef WEAPONS[WPN_COUNT] = {
    [WPN_BLASTER] = {
        .name = "BLASTER", .desc = "Rapid twin-linked bolts",
        .cooldown = 0.08f, .count = 1, .count_per_level = 1,
        .spread = 0.0f, .spread_per = 0.03f, .jitter = 0.03f, .parallel = 10.0f,
        .damage = 7.0f, .speed = 1500.0f, .speed_var = 0.0f, .life = 1.1f, .radius = 5.0f,
        .pierce = 0, .bounce = 0, .homing = 0.0f, .knockback = 50.0f,
        .color = 0x66f0ff, .level_rate = 0.93f,
    },
    [WPN_SCATTER] = {
        .name = "SCATTER", .desc = "Wide shotgun blasts, short range",
        .cooldown = 0.24f, .count = 12, .count_per_level = 4,
        .spread = 0.55f, .spread_per = 0.02f, .jitter = 0.05f, .parallel = 0.0f,
        .damage = 6.0f, .speed = 1150.0f, .speed_var = 400.0f, .life = 0.4f, .radius = 5.0f,
        .pierce = 0, .bounce = 0, .homing = 0.0f, .knockback = 110.0f,
        .color = 0xffa94d, .level_rate = 0.94f,
    },
    [WPN_NEEDLER] = {
        .name = "NEEDLER", .desc = "Piercing needle storm",
        .cooldown = 0.04f, .count = 1, .count_per_level = 1,
        .spread = 0.0f, .spread_per = 0.07f, .jitter = 0.06f, .parallel = 0.0f,
        .damage = 3.5f, .speed = 1900.0f, .speed_var = 200.0f, .life = 0.75f, .radius = 3.0f,
        .pierce = 2, .bounce = 0, .homing = 0.0f, .knockback = 15.0f,
        .color = 0xf28cff, .level_rate = 0.94f,
    },
    [WPN_SEEKER] = {
        .name = "SEEKER", .desc = "Homing micro-missiles",
        .cooldown = 0.2f, .count = 2, .count_per_level = 1,
        .spread = 0.7f, .spread_per = 0.12f, .jitter = 0.1f, .parallel = 0.0f,
        .damage = 12.0f, .speed = 500.0f, .speed_var = 100.0f, .life = 2.0f, .radius = 6.0f,
        .pierce = 0, .bounce = 0, .homing = 7.0f, .knockback = 60.0f,
        .color = 0x9dff6a, .level_rate = 0.93f,
    },
    [WPN_BOUNCER] = {
        .name = "BOUNCER", .desc = "Orbs ricochet off walls",
        .cooldown = 0.15f, .count = 1, .count_per_level = 1,
        .spread = 0.0f, .spread_per = 0.14f, .jitter = 0.04f, .parallel = 0.0f,
        .damage = 10.0f, .speed = 950.0f, .speed_var = 0.0f, .life = 2.4f, .radius = 8.0f,
        .pierce = 1, .bounce = 4, .homing = 0.0f, .knockback = 80.0f,
        .color = 0xffe14d, .level_rate = 0.93f,
    },
    /* Melee: no projectiles. damage = per hit, cooldown = between swings (see sword_swing). */
    [WPN_SWORD] = {
        .name = "SWORD", .desc = "Arc slash that cuts bullets",
        .cooldown = 0.36f, .damage = 34.0f, .knockback = 260.0f,
        .color = 0xc9d4ff, .level_rate = 0.93f,
    },
};

bool weapon_available(int w) { return w != WPN_SWORD || G.character == CHAR_KNIGHT; }

/* ---- Sword ---------------------------------------------------------------- */

#define SWORD_SWING_TIME 0.13f

float sword_reach(void) {
    const Player *p = &G.pl;
    int lvl = p->wlevel[WPN_SWORD] > 0 ? p->wlevel[WPN_SWORD] : 1;
    /* Capped: a huge blade would clear every bullet around the ship. */
    return fminf((115.0f + 8.0f * (float)(lvl - 1)) * sqrtf(p->range_mult) * sqrtf(p->bullet_size), 170.0f);
}

static float weapon_cooldown(const WeaponDef *w, int lvl) {
    return w->cooldown * powf(w->level_rate, (float)(lvl - 1)) / (G.pl.rate_mult * upg_fire_rate_mult());
}

static void sword_swing(void) {
    Player *p = &G.pl;
    const WeaponDef *w = &WEAPONS[WPN_SWORD];
    int lvl = p->wlevel[WPN_SWORD];
    /* Fire rate counts less for the sword (sqrt), with a floor: a blade swinging non-stop
     * would be a permanent bullet shield. */
    float cd = w->cooldown * powf(w->level_rate, (float)(lvl - 1)) / sqrtf(p->rate_mult * upg_fire_rate_mult());
    cd = fmaxf(cd, 0.16f);
    p->fire_cd += cd;
    /* Back and forth: every swing goes the other way. Extra projectiles widen the arc. */
    p->swing_dir = p->swing_dir > 0 ? -1 : 1;
    p->swing_len = fminf(SWORD_SWING_TIME, cd * 0.8f);
    p->swing_t = p->swing_len;
    p->swing_a0 = p->aim_angle;
    p->swing_half = fminf(0.95f + 0.07f * (float)(lvl - 1) + 0.1f * (float)p->extra_proj, 1.4f);
    p->swing_reach = sword_reach();
    p->swing_prev = -(float)p->swing_dir * p->swing_half;
    p->swing_dmg = w->damage * (1.0f + 0.15f * (float)(lvl - 1)) * p->dmg_mult * upg_volley_damage_mult();
    if (active_strength_on()) p->swing_dmg *= 4.0f; /* STRENGTH: one blade, four times the bite */
    p->swing_id++;
    upg_on_volley(p->pos, p->aim_angle, p->swing_dmg * 0.35f, col_hex(w->color, 1));
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
    SDL_FColor col = col_hex(WEAPONS[WPN_SWORD].color, 1);

    /* Everything on the blade's path: enemies (once per swing) and enemy bullets. */
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp <= 0.0f || e->sword_swing == p->swing_id || !in_sweep(e->pos, e->radius, lo, hi)) continue;
        e->sword_swing = p->swing_id;
        V2 dir = v2_norm(v2_sub(e->pos, p->pos));
        PBullet hit = {.pos = e->pos, .vel = v2_scale(dir, 900.0f), .radius = 6.0f, .damage = p->swing_dmg,
                       .knockback = WEAPONS[WPN_SWORD].knockback, .last_hit = e->uid, .weapon = WPN_SWORD, .col = col};
        float dmg = upg_hit_damage(&hit, e);
        enemy_damage(e, dmg, v2_scale(dir, hit.knockback));
        upg_after_hit(&hit, e, dmg);
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
    if (p->wlevel[WPN_SWORD] == 0 || p->weapon != WPN_SWORD) return;
    SDL_FColor col = col_hex(WEAPONS[WPN_SWORD].color, 1);
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
    const WeaponDef *w = &WEAPONS[p->weapon];
    int lvl = p->wlevel[p->weapon];
    int n = w->count + w->count_per_level * (lvl - 1) + p->extra_proj;
    p->fire_cd += weapon_cooldown(w, lvl);

    float spread = (w->spread + w->spread_per * (float)(n - 1)) * p->spread_mult;
    float dmg = w->damage * (1.0f + 0.15f * (float)(lvl - 1)) * p->dmg_mult * upg_volley_damage_mult();
    SDL_FColor col = col_hex(w->color, 1);
    V2 fwd = v2_angle(p->aim_angle), side = v2_perp(fwd);
    V2 muzzle = v2_add(p->pos, v2_scale(fwd, 22.0f));

    /* STRENGTH: the whole volley four times, fanned slightly. */
    int copies = active_strength_on() ? 4 : 1;
    for (int i = 0; i < n * copies; i++) {
        int c = i / n;
        float off = copies > 1 ? ((float)c - (float)(copies - 1) * 0.5f) * 0.07f : 0.0f;
        float t = n > 1 ? (float)(i % n) / (float)(n - 1) - 0.5f : 0.0f;
        float a = p->aim_angle + off + t * spread + frand(-w->jitter, w->jitter) * fminf(p->spread_mult, 1.0f);
        V2 pos = v2_add(muzzle, v2_scale(side, w->parallel * t * (float)(n - 1)));
        float spd = (w->speed + frand(0.0f, w->speed_var)) * p->shot_speed_mult;
        PBullet *b = pbullet_spawn(pos, v2_scale(v2_angle(a), spd), w->radius, dmg, col);
        if (!b) break;
        b->life = w->life * p->range_mult;
        b->pierce = w->pierce + p->pierce;
        b->bounces = w->bounce + p->bounce;
        b->homing = w->homing + p->homing;
        b->knockback = w->knockback;
        b->weapon = p->weapon;
        upg_on_bullet_spawn(b);
    }
    upg_on_volley(muzzle, p->aim_angle, dmg, col);

    fx_particle(muzzle, v2(0, 0), 0.05f, 18.0f + (float)(n * copies) * 2.0f, col_a(col, 0.8f), 0.0f, PT_GLOW);
    static const SfxId shot_sfx[WPN_COUNT] = {
        [WPN_BLASTER] = SFX_SHOT_BLASTER, [WPN_SCATTER] = SFX_SHOT_SCATTER, [WPN_NEEDLER] = SFX_SHOT_NEEDLER,
        [WPN_SEEKER] = SFX_SHOT_SEEKER, [WPN_BOUNCER] = SFX_SHOT_BOUNCER, [WPN_SWORD] = SFX_SWORD,
    };
    sfx_play_at(shot_sfx[p->weapon], p->pos);
    if (p->weapon == WPN_SCATTER) {
        p->vel = v2_sub(p->vel, v2_scale(fwd, 90.0f));
        fx_shake(0.04f);
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
        b->weapon = -1;
        upg_on_bullet_spawn(b);
        sfx_play_at(SFX_DRONE_SHOT, dp);
    }
}

void weapons_update(void) {
    Player *p = &G.pl;
    p->fire_cd -= DT;
    if (G.in.fire) {
        while (p->fire_cd <= 0.0f) {
            if (p->weapon == WPN_SWORD) sword_swing();
            else fire_volley();
        }
    } else if (p->fire_cd < 0.0f) {
        p->fire_cd = 0.0f;
    }
    if (p->weapon != WPN_SWORD) p->swing_t = 0.0f;
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

void weapon_give(int w) {
    Player *p = &G.pl;
    char sub[96];
    if (p->wlevel[w] == 0) {
        p->wlevel[w] = 1;
        fx_banner(WEAPONS[w].name, WEAPONS[w].desc, col_hex(WEAPONS[w].color, 1), 2.6f);
    } else {
        if (p->wlevel[w] < MAX_WEAPON_LEVEL) p->wlevel[w]++;
        snprintf(sub, sizeof sub, w == WPN_SWORD ? "LEVEL %d - wider, longer, faster swings" : "LEVEL %d - more projectiles, faster fire",
                 p->wlevel[w]);
        fx_banner(WEAPONS[w].name, sub, col_hex(WEAPONS[w].color, 1), 2.6f);
    }
    p->weapon = w;
}

int weapon_roll(void) {
    const Player *p = &G.pl;
    int unowned[WPN_COUNT], upgradable[WPN_COUNT], nu = 0, nup = 0;
    for (int w = 0; w < WPN_COUNT; w++) {
        if (!weapon_available(w)) continue;
        if (p->wlevel[w] == 0) unowned[nu++] = w;
        else if (p->wlevel[w] < MAX_WEAPON_LEVEL) upgradable[nup++] = w;
    }
    if (nu > 0 && (nup == 0 || chance(0.65f))) return unowned[irand(0, nu - 1)];
    if (nup > 0) return upgradable[irand(0, nup - 1)];
    return -1;
}

