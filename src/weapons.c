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
        .cooldown = 0.24f, .count = 6, .count_per_level = 2,
        .spread = 0.55f, .spread_per = 0.04f, .jitter = 0.05f, .parallel = 0.0f,
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
};

const UpgradeDef UPGRADES[UPG_COUNT] = {
    [UPG_RAPID] = {"OVERCLOCK", "+18% fire rate", 0xff5f7e},
    [UPG_DAMAGE] = {"HEAVY ROUNDS", "+22% damage", 0xff8a3d},
    [UPG_MULTI] = {"SPLIT SHOT", "+1 projectile on every weapon", 0x66f0ff},
    [UPG_PIERCE] = {"PIERCER", "Shots pierce one more enemy", 0xf28cff},
    [UPG_BOUNCE] = {"RUBBER CORE", "Shots bounce off walls twice", 0xffe14d},
    [UPG_HOMING] = {"SMART ROUNDS", "Shots steer towards enemies", 0x9dff6a},
    [UPG_THRUSTER] = {"THRUSTERS", "+10% speed, faster dash", 0x7da8ff},
    [UPG_HULL] = {"HULL PLATING", "+1 heart container", 0xff3355},
    [UPG_DRONE] = {"WING DRONE", "An auto-firing drone joins you", 0xb6ffb0},
    [UPG_MAGNET] = {"MAGNET", "Bigger pickup radius", 0xffd23f},
    [UPG_VELOCITY] = {"ACCELERATOR", "+20% shot speed, +8% damage", 0x9ad7ff},
};

static void fire_volley(void) {
    Player *p = &G.pl;
    const WeaponDef *w = &WEAPONS[p->weapon];
    int lvl = p->wlevel[p->weapon];
    int n = w->count + w->count_per_level * (lvl - 1) + p->extra_proj;
    float cd = w->cooldown * powf(w->level_rate, (float)(lvl - 1)) / p->rate_mult;
    p->fire_cd += cd;

    float spread = w->spread + w->spread_per * (float)(n - 1);
    float dmg = w->damage * (1.0f + 0.15f * (float)(lvl - 1)) * p->dmg_mult;
    SDL_FColor col = col_hex(w->color, 1);
    V2 fwd = v2_angle(p->aim_angle), side = v2_perp(fwd);
    V2 muzzle = v2_add(p->pos, v2_scale(fwd, 22.0f));

    for (int i = 0; i < n; i++) {
        float t = n > 1 ? (float)i / (float)(n - 1) - 0.5f : 0.0f;
        float a = p->aim_angle + t * spread + frand(-w->jitter, w->jitter);
        V2 pos = v2_add(muzzle, v2_scale(side, w->parallel * t * (float)(n - 1)));
        float spd = (w->speed + frand(0.0f, w->speed_var)) * p->shot_speed_mult;
        PBullet *b = pbullet_spawn(pos, v2_scale(v2_angle(a), spd), w->radius, dmg, col);
        if (!b) break;
        b->life = w->life;
        b->pierce = w->pierce + p->pierce;
        b->bounces = w->bounce + p->bounce;
        b->homing = w->homing + p->homing;
        b->knockback = w->knockback;
        b->weapon = p->weapon;
    }

    fx_particle(muzzle, v2(0, 0), 0.05f, 18.0f + (float)n * 2.0f, col_a(col, 0.8f), 0.0f, PT_GLOW);
    static const SfxId shot_sfx[WPN_COUNT] = {
        [WPN_BLASTER] = SFX_SHOT_BLASTER, [WPN_SCATTER] = SFX_SHOT_SCATTER, [WPN_NEEDLER] = SFX_SHOT_NEEDLER,
        [WPN_SEEKER] = SFX_SHOT_SEEKER, [WPN_BOUNCER] = SFX_SHOT_BOUNCER,
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
        b->life = 1.0f;
        b->pierce = p->pierce;
        b->bounces = p->bounce / 2;
        b->homing = p->homing;
        b->knockback = 20.0f;
        b->weapon = -1;
        sfx_play_at(SFX_DRONE_SHOT, dp);
    }
}

void weapons_update(void) {
    Player *p = &G.pl;
    p->fire_cd -= DT;
    if (G.in.fire) {
        while (p->fire_cd <= 0.0f) fire_volley();
    } else if (p->fire_cd < 0.0f) {
        p->fire_cd = 0.0f;
    }
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
        snprintf(sub, sizeof sub, "LEVEL %d - more projectiles, faster fire", p->wlevel[w]);
        fx_banner(WEAPONS[w].name, sub, col_hex(WEAPONS[w].color, 1), 2.6f);
    }
    p->weapon = w;
}

int weapon_roll(void) {
    const Player *p = &G.pl;
    int unowned[WPN_COUNT], upgradable[WPN_COUNT], nu = 0, nup = 0;
    for (int w = 0; w < WPN_COUNT; w++) {
        if (p->wlevel[w] == 0) unowned[nu++] = w;
        else if (p->wlevel[w] < MAX_WEAPON_LEVEL) upgradable[nup++] = w;
    }
    if (nu > 0 && (nup == 0 || chance(0.65f))) return unowned[irand(0, nu - 1)];
    if (nup > 0) return upgradable[irand(0, nup - 1)];
    return -1;
}

static bool upgrade_capped(int u) {
    const Player *p = &G.pl;
    switch (u) {
    case UPG_DRONE: return p->drones >= MAX_DRONES;
    case UPG_HULL: return p->max_hp >= 24;
    case UPG_MULTI: return p->extra_proj >= 4;
    case UPG_HOMING: return p->upg_count[UPG_HOMING] >= 3;
    case UPG_MAGNET: return p->upg_count[UPG_MAGNET] >= 3;
    case UPG_THRUSTER: return p->upg_count[UPG_THRUSTER] >= 4;
    default: return false;
    }
}

int upgrade_roll(void) {
    for (int tries = 0; tries < 32; tries++) {
        int u = irand(0, UPG_COUNT - 1);
        if (!upgrade_capped(u)) return u;
    }
    return UPG_DAMAGE;
}

void upgrade_apply(int u) {
    Player *p = &G.pl;
    switch (u) {
    case UPG_RAPID: p->rate_mult *= 1.18f; break;
    case UPG_DAMAGE: p->dmg_mult += 0.22f; break;
    case UPG_MULTI: p->extra_proj++; break;
    case UPG_PIERCE: p->pierce++; break;
    case UPG_BOUNCE: p->bounce += 2; break;
    case UPG_HOMING: p->homing += 3.0f; break;
    case UPG_THRUSTER: p->speed *= 1.1f; p->dash_cooldown *= 0.85f; break;
    case UPG_HULL: p->max_hp += 2; p->hp += 2; break;
    case UPG_DRONE: p->drones++; break;
    case UPG_MAGNET: p->magnet += 120.0f; break;
    case UPG_VELOCITY: p->shot_speed_mult *= 1.2f; p->dmg_mult += 0.08f; break;
    default: return;
    }
    p->upg_count[u]++;
    fx_banner(UPGRADES[u].name, UPGRADES[u].desc, col_hex(UPGRADES[u].color, 1), 2.6f);
}
