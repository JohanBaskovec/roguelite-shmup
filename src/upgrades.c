#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Passive upgrades. Stat upgrades change Player.base through stat_effect() when the
 * stats are rebuilt (player_recompute); everything else is read through has() by the
 * hooks below, which the systems they modify call (weapons.c, bullets.c, enemies.c,
 * player.c, room.c). Any upgrade can be turned off in the upgrades menu (Player.upg_off):
 * has() then returns 0 and its stat effect is left out.
 */

const UpgradeDef UPGRADES[UPG_COUNT] = {
    [UPG_RAPID] = {"OVERCLOCK", "+18% fire rate", 0xff5f7e},
    [UPG_DAMAGE] = {"HEAVY ROUNDS", "+22% damage", 0xff8a3d},
    [UPG_MULTI] = {"SPLIT SHOT", "+1 projectile per volley", 0x66f0ff},
    [UPG_PIERCE] = {"PIERCER", "Shots pierce one more enemy", 0xf28cff},
    [UPG_BOUNCE] = {"RUBBER CORE", "Shots bounce off walls twice", 0xffe14d},
    [UPG_HOMING] = {"SMART ROUNDS", "Shots steer towards enemies", 0x9dff6a},
    [UPG_THRUSTER] = {"THRUSTERS", "+10% speed, faster recharge|of your active item", 0x7da8ff},
    [UPG_HULL] = {"HULL PLATING", "+1 heart container", 0xff3355},
    [UPG_DRONE] = {"WING DRONE", "An auto-firing drone joins you", 0xb6ffb0},
    [UPG_MAGNET] = {"MAGNET", "Bigger pickup radius", 0xffd23f},
    [UPG_VELOCITY] = {"ACCELERATOR", "+20% shot speed, +8% damage", 0x9ad7ff},

    [UPG_FROST] = {"CRYO ROUNDS", "Hits chill enemies: -45% speed", 0x8fe9ff},
    [UPG_VENOM] = {"VENOM TIPS", "Hits poison enemies over time", 0x7dff4a},
    [UPG_THERMITE] = {"THERMITE", "Hits may set enemies on fire", 0xff7a2e},
    [UPG_ARC] = {"ARC EMITTER", "Hits may chain lightning", 0x9db8ff},
    [UPG_PAYLOAD] = {"PAYLOAD", "Shots may explode on impact", 0xffa64d},
    [UPG_SHRAPNEL] = {"SHRAPNEL", "Hits may burst into fragments", 0xd0d0e0},
    [UPG_BIG_IRON] = {"BIG IRON", "Bigger shots, +20% damage", 0xc9a0ff},
    [UPG_CRIT] = {"LUCKY STRIKE", "Chance of triple-damage shots", 0xfff07a},
    [UPG_GUILLOTINE] = {"GUILLOTINE", "Hits execute enemies below 15%", 0xff4466},
    [UPG_BOOMERANG] = {"RETURN VECTOR", "Shots fly back to you", 0x66ffd9},
    [UPG_PHASE] = {"PHASE ROUNDS", "Shots pass through obstacles", 0xb08cff},
    [UPG_IMPACT] = {"IMPACT DRIVER", "Huge knockback, +10% damage", 0xffb070},
    [UPG_TAIL_GUN] = {"TAIL GUN", "Also fires backwards", 0x7dd3ff},
    [UPG_FLANK] = {"FLANK CANNONS", "Periodic shots to both sides", 0x7dffc8},
    [UPG_CAPACITOR] = {"CAPACITOR", "Every 10th volley: a mega shot", 0x66e0ff},
    [UPG_WIDE_LENS] = {"WIDE LENS", "+1 projectile", 0x99ccff},
    [UPG_FOCUS_LENS] = {"FOCUS LENS", "Tighter spread, +10% damage", 0xa0ffea},

    [UPG_GLASS_CANNON] = {"GLASS CANNON", "+60% damage", 0xe0f4ff},
    [UPG_TITAN_HULL] = {"TITAN HULL", "+2 heart containers", 0xff5577},
    [UPG_FEATHER] = {"FEATHERWEIGHT", "+15% speed, smaller hitbox", 0xe6f0ff},
    [UPG_LONG_BARREL] = {"LONG BARREL", "+40% range, faster shots", 0xb4c8ff},
    [UPG_HAIR_TRIGGER] = {"HAIR TRIGGER", "+30% fire rate", 0xff7a9a},
    [UPG_FOUR_LEAF] = {"FOUR-LEAF CHIP", "+2 luck", 0x5dff8a},
    [UPG_MICRO_HULL] = {"MICRO HULL", "-25% hitbox size", 0xd8d8ff},

    [UPG_PIGGY_BANK] = {"PIGGY BANK", "+12 coins, +3 per room cleared", 0xffd23f},
    [UPG_BOMB_BELT] = {"BOMB BELT", "+3 bombs, bombs hit 60% harder", 0xff8a3d},
    [UPG_SKELETON_KEY] = {"SKELETON KEY", "+3 keys", 0xd9e3f0},
    [UPG_COUPON] = {"COUPON", "Shop prices -30%", 0xff9de0},

    [UPG_REACTIVE] = {"REACTIVE ARMOR", "Getting hit fires a bullet ring", 0xff6a4d},
    [UPG_SECOND_WIND] = {"SECOND WIND", "Survive one lethal hit", 0xfff5c0},
    [UPG_BARRIER] = {"BARRIER CELL", "+1 shield, recharges each room", 0x66d9ff},
    [UPG_ORB] = {"GUARDIAN ORB", "Orbiting orb blocks bullets", 0xffe3a0},
    [UPG_TESLA] = {"TESLA FIELD", "Zaps nearby enemies", 0x80c8ff},
    [UPG_LEECH] = {"LEECH CORE", "Kills slowly restore health", 0xff3a6e},
    [UPG_ADRENALINE] = {"ADRENALINE", "Faster when at 1 heart or less", 0xff3355},
    [UPG_BLOODLUST] = {"BLOODLUST", "Kills boost damage this room", 0xc8102e},
    [UPG_RAM] = {"RAM PLATING", "Ramming enemies hurts them|while dashing or shielded", 0xffc26b},
    [UPG_DASH_NOVA] = {"NOVA TRIGGER", "Using your active item|fires a ring of shots", 0x7dfcff},
    [UPG_CHRONO] = {"CHRONO FIELD", "Enemy bullets 15% slower", 0xa8a0ff},
    [UPG_VOLATILE] = {"VOLATILE TARGETS", "Enemies explode when they die", 0xff5a1f},
    [UPG_MINES] = {"MINE LAYER", "Drops mines as you move", 0xffaa33},

    /* '|' splits a description over two lines on pedestals (a space in banners). */
    [UPG_DEFLECTOR] = {"DEFLECTOR SHELLS", "Huge shots that reflect|enemy bullets", 0x7dffef},
    [UPG_WOUNDED_FURY] = {"WOUNDED FURY", "+30% damage while not|at full health", 0xff4a4a},
    [UPG_SCAR_TISSUE] = {"SCAR TISSUE", "+25% fire rate while not|at full health", 0xff8a8a},
    [UPG_LAST_STAND] = {"LAST STAND", "Per missing heart: +10% damage|and +5% speed", 0xd02040},
    [UPG_BLOOD_SCENT] = {"BLOOD SCENT", "Shots home in on enemies|while not at full health", 0xff6060},
    [UPG_LOADED_DICE] = {"LOADED DICE", "Doubles the chance of every|upgrade effect", 0xe6ff66},
    [UPG_SCOPE] = {"SNIPER SCOPE", "Shots gain up to +60% damage|the longer they fly", 0xd8f6ff},
    [UPG_POINT_BLANK] = {"POINT BLANK", "+50% damage to enemies|close to you", 0xff9a5a},
    [UPG_BULLY] = {"BULLY", "+40% damage to enemies|under half health", 0xff6a8a},
    [UPG_GIANT_SLAYER] = {"GIANT SLAYER", "+60% damage to big enemies|(200+ health)", 0xc0a0ff},
    [UPG_MOMENTUM] = {"MOMENTUM", "Each hit: +1% damage (max +40%)|lost after 1 s without a hit", 0xffd36b},
    [UPG_SMART_BOUNCE] = {"SMART BOUNCE", "+1 bounce; every bounce aims|at the nearest enemy", 0xfff08a},
    [UPG_CHAIN_REACTION] = {"CHAIN REACTION", "Every kill fires 6 shots|outward", 0xff7ad9},
    [UPG_GROWTH] = {"GROWTH", "+3% damage per room cleared|from now on (max +60%)", 0x7dff8a},
    [UPG_TURRET_MODE] = {"TURRET MODE", "+40% fire rate while|standing still", 0x8ab8ff},
    [UPG_RUN_GUN] = {"RUN AND GUN", "+20% damage while moving", 0x8affd8},
    [UPG_FROSTBITE] = {"FROSTBITE", "Chilled enemies take +35%|damage; hits may chill", 0xa8f0ff},
    [UPG_PYROMANIAC] = {"PYROMANIAC", "Burning enemies explode on|death; hits may ignite", 0xff6a2a},
    [UPG_TOXIC_CLOUD] = {"TOXIC CLOUD", "Poisoned enemies spread it on|death; hits may poison", 0x9aff3a},
    [UPG_OVERKILL] = {"OVERKILL", "Excess damage on a kill|spills into the nearest enemy", 0xff4a6a},
    [UPG_MISSILE_POD] = {"MISSILE POD", "Fires a homing missile|every 1.5 s", 0xffb04d},
    [UPG_ORBITAL] = {"ORBITAL STRIKE", "A strike from orbit hits|an enemy every 3 s", 0x9fe8ff},
    [UPG_SPIKED_HULL] = {"SPIKED HULL", "Enemies touching your hull|take heavy damage", 0xd0d0e0},
    [UPG_ECHO] = {"ECHO", "Every volley is echoed|0.12 s later (50% damage)", 0xb0a0ff},
    [UPG_MAGNUM] = {"MAGNUM ROUNDS", "+2 pierce, +15% damage", 0xffc080},
    [UPG_SPRAY] = {"SPRAY AND PRAY", "+45% fire rate", 0xff8ad0},
    [UPG_HEAVY_CALIBER] = {"HEAVY CALIBER", "+40% damage, bigger shots", 0xc08a5a},
    [UPG_EMERGENCY] = {"EMERGENCY SHIELD", "Once per room, the first|hit is blocked", 0x66d9ff},
    [UPG_RAMPAGE] = {"RAMPAGE", "+3% fire rate per kill in the|room (max +45%)", 0xff5a3a},
    [UPG_BLACK_HOLE] = {"BLACK HOLE", "Kills may open a vortex|that pulls enemies in", 0x8a5aff},
    [UPG_XRAY] = {"X-RAY VISOR", "Secret corners are|always visible", 0xb0ffb0},

    /* Weapon mods: they change the gun itself and stack with each other (weapons.c, gun_profile). */
    [UPG_W_SCATTER] = {"SCATTER", "x8 pellets in a wide fan,|big knockback", 0xffa94d},
    [UPG_W_NEEDLER] = {"NEEDLER", "Double fire rate, shots|pierce 2 more enemies", 0xf28cff},
    [UPG_W_SEEKER] = {"SEEKER", "Homing missiles: +1 shot,|+60% damage", 0x9dff6a},
    [UPG_W_BOUNCER] = {"BOUNCER", "Big orbs that bounce 4 times|and pierce, +45% damage", 0xffe14d},
    [UPG_W_RAILGUN] = {"RAILGUN", "Rails: x9 damage, pierce|everything", 0xe0f4ff},
    [UPG_W_FLAMER] = {"FLAMER", "Fast flames that pierce|and set enemies ablaze", 0xff7a1a},
    [UPG_W_ROCKETS] = {"ROCKETS", "Accelerating rockets that|explode (x3.6 damage)", 0xffb04d},
    [UPG_W_CHAKRAM] = {"CHAKRAM", "Spinning blades that pierce|everything and fly back", 0xa8ff5a},
    [UPG_W_CLUSTER] = {"CLUSTER", "Shells burst into 6|fragments, +80% damage", 0xff5fd2},
    [UPG_W_ARC] = {"ARC CASTER", "Hits chain lightning through|3 more enemies, +30% damage", 0x9db8ff},
    [UPG_W_VULCAN] = {"VULCAN", "Minigun: the fire rate climbs|up to x4 while firing", 0xffe08a},
    [UPG_W_WAVE] = {"WAVE GUN", "Twin shots that weave|and pierce one more enemy", 0x5dffd8},
    [UPG_W_SWORD] = {"SWORD", "Replaces the gun: arc slashes|that cut enemy bullets", 0xc9d4ff},

    /* Workshop exclusives: tier = heart containers paid at the augment machine. */
    [UPG_X_BLOOD_ROUNDS] = {"BLOOD ROUNDS", "+30% damage; every hit makes|enemies bleed (40%/s, 3 s)", 0xff2050, 1},
    [UPG_X_OVERDRIVE] = {"OVERDRIVE CORE", "+30% fire rate, +15% speed,|active item recharges 30% faster", 0xff9f1c, 1},
    [UPG_X_HELLFIRE] = {"HELLFIRE ENGINE", "+40% damage; every hit sets|enemies ablaze (100%/s, 2 s)", 0xff5a00, 2},
    [UPG_X_STORM] = {"STORM CORE", "+30% fire rate; hits may chain|lightning through 3 enemies", 0x9db8ff, 2},
    [UPG_X_DEMON] = {"DEMON HEART", "Damage x1.8, +2 projectiles,|every kill explodes", 0xb0103a, 3},
    [UPG_X_GOD_CANNON] = {"GOD CANNON", "+50% dmg, +25% rate, +1 shot,|+2 pierce, shots home", 0xfff5c0, 3},
};

/* Stacks owned (0 while turned off), +1 while CHAOS lends it (only lent when not owned). */
static int has(UpgradeId u) { return (G.pl.upg_off[u] ? 0 : G.pl.upg_count[u]) + (G.pl.chaos[u] ? 1 : 0); }
int upg_has(int u) { return has((UpgradeId)u); }

/* Flat damage of auras / explosions follows the player's damage (enemies never scale). */
static float aura_scale(void) { return G.pl.dmg_mult; }

/* Chance of an upgrade effect: LOADED DICE doubles every one of them. */
static float proc(float p) { return fminf(1.0f, p * (has(UPG_LOADED_DICE) ? 2.0f : 1.0f)); }

static bool hurt(void) { return G.pl.hp < G.pl.max_hp; }
static float missing_hearts(void) { return (float)(G.pl.max_hp - G.pl.hp) * 0.5f; }

/* Effect chances as a function of the stack count (shared by the effects and the descriptions). */
static float thermite_chance(int n) { return proc(fminf(0.15f * (float)n, 0.6f)); }
static float arc_chance(int n) { return proc(fminf(0.2f * (float)n, 0.6f)); }
static float payload_chance(int n) { return proc(fminf(0.2f + 0.15f * (float)(n - 1), 0.7f)); }
static float shrapnel_chance(int n) { return proc(fminf(0.25f * (float)n, 0.75f)); }
static float crit_chance(int n) { return proc(clampf(0.08f * (float)n + 0.02f * (float)G.pl.luck, 0.02f, 0.6f)); }
#define STORM_CHANCE 0.35f
#define FROSTBITE_CHANCE 0.3f
#define IGNITE_CHANCE 0.15f /* PYROMANIAC, TOXIC CLOUD (poison) */
static float black_hole_chance(int n) { return proc(0.08f * (float)n); }

static int pct(float p) { return (int)roundf(p * 100.0f); }

const char *upgrade_desc_at(int u, int n) {
    static char bufs[4][96];
    static int next;
    char *b = bufs[next++ & 3];
    if (n < 1) n = 1;
    switch (u) {
    case UPG_THERMITE: SDL_snprintf(b, 96, "Hits: %d%% chance to set|enemies on fire (2 s)", pct(thermite_chance(n))); break;
    case UPG_ARC: SDL_snprintf(b, 96, "Hits: %d%% chance to chain|lightning to a nearby enemy", pct(arc_chance(n))); break;
    case UPG_PAYLOAD: SDL_snprintf(b, 96, "Hits: %d%% chance to explode", pct(payload_chance(n))); break;
    case UPG_SHRAPNEL: SDL_snprintf(b, 96, "Hits: %d%% chance to burst|into 3 fragments", pct(shrapnel_chance(n))); break;
    case UPG_CRIT: SDL_snprintf(b, 96, "Shots: %d%% chance to deal|triple damage", pct(crit_chance(n))); break;
    case UPG_X_STORM:
        SDL_snprintf(b, 96, "+30%% fire rate; hits: %d%% chance|of lightning through 3 enemies", pct(proc(STORM_CHANCE)));
        break;
    case UPG_LEECH: SDL_snprintf(b, 96, "Heal 1/2 heart every %d kills", n >= 2 ? 25 : 40); break;
    case UPG_FROSTBITE: SDL_snprintf(b, 96, "Chilled enemies take +35%%|damage; hits: %d%% to chill", pct(proc(FROSTBITE_CHANCE))); break;
    case UPG_PYROMANIAC: SDL_snprintf(b, 96, "Burning enemies explode on|death; hits: %d%% to ignite", pct(proc(IGNITE_CHANCE))); break;
    case UPG_TOXIC_CLOUD: SDL_snprintf(b, 96, "Poisoned enemies spread it on|death; hits: %d%% to poison", pct(proc(IGNITE_CHANCE))); break;
    case UPG_BLACK_HOLE: SDL_snprintf(b, 96, "Kills: %d%% chance to open a|vortex that pulls enemies in", pct(black_hole_chance(n))); break;
    case UPG_MISSILE_POD: SDL_snprintf(b, 96, "Fires a homing missile|every %.2g s", (double)(1.5f / (float)n)); break;
    case UPG_ORBITAL: SDL_snprintf(b, 96, "A strike from orbit hits|an enemy every %.2g s", (double)(3.0f / (float)n)); break;
    case UPG_CAPACITOR: SDL_snprintf(b, 96, "Every %dth volley: a mega shot", n >= 2 ? 6 : 10); break;
    default: return UPGRADES[u].desc;
    }
    return b;
}

const char *upgrade_desc(int u) { return upgrade_desc_at(u, G.pl.upg_count[u] > 0 ? G.pl.upg_count[u] : 1); }

/* ---- Rolling & applying -------------------------------------------------- */

bool upgrade_capped(int u) {
    const Player *p = &G.pl;
    /* Every upgrade is taken once: owned (even turned off) = never offered again. Every
     * character can get every upgrade (the Knight's sword included). */
    if (p->upg_count[u] > 0) return true;
    /* Caps count every upgrade owned, even those turned off for now. */
    Stats all;
    stats_compute(&all, true);
    switch (u) {
    case UPG_DRONE: return all.drones >= MAX_DRONES;
    case UPG_HULL: return all.max_hp >= 24;
    case UPG_TITAN_HULL: return all.max_hp > 20;
    case UPG_MULTI:
    case UPG_WIDE_LENS: return all.extra_proj >= 4;
    case UPG_GLASS_CANNON: return all.max_hp < 4;
    case UPG_FOUR_LEAF: return all.luck >= 4;
    case UPG_MICRO_HULL: return all.hit_radius * 0.75f < 2.0f;
    /* Useless without an active item that triggers them. */
    case UPG_DASH_NOVA: {
        int owned[ACT_COUNT];
        return actives_owned(owned) == 0;
    }
    case UPG_RAM: return p->act[ACT_DASH].level == 0 && p->act[ACT_SHIELD].level == 0;
    default: return false;
    }
}

int upgrade_roll(void) {
    /* exclusives: workshop only */
    int cand[UPG_COUNT], n = 0;
    for (int u = 0; u < UPG_COUNT; u++)
        if (UPGRADES[u].tier == 0 && !upgrade_capped(u)) cand[n++] = u;
    return n ? cand[irand(0, n - 1)] : -1;
}

int upgrade_roll_exclusive(int tier) {
    int cand[UPG_COUNT], n = 0;
    for (int u = 0; u < UPG_COUNT; u++)
        if (UPGRADES[u].tier == tier && !upgrade_capped(u)) cand[n++] = u;
    return n ? cand[irand(0, n - 1)] : -1;
}

void player_add_shield(int half_hearts) {
    Player *p = &G.pl;
    p->shield = p->shield + half_hearts > MAX_SHIELD ? MAX_SHIELD : p->shield + half_hearts;
}

/* What one stack of u does to the stats (nothing for hook-only upgrades). */
static void stat_effect(Stats *p, int u) {
    switch (u) {
    case UPG_RAPID: p->rate_mult *= 1.18f; break;
    case UPG_DAMAGE: p->dmg_mult += 0.22f; break;
    case UPG_MULTI: p->extra_proj++; break;
    case UPG_PIERCE: p->pierce++; break;
    case UPG_BOUNCE: p->bounce += 2; break;
    case UPG_HOMING: p->homing += 3.0f; break;
    case UPG_THRUSTER: p->speed *= 1.1f; p->dash_cooldown = fmaxf(0.15f, p->dash_cooldown * 0.85f); break;
    case UPG_HULL: p->max_hp += 2; break;
    case UPG_DRONE: p->drones++; break;
    case UPG_MAGNET: p->magnet += 120.0f; break;
    case UPG_VELOCITY: p->shot_speed_mult *= 1.2f; p->dmg_mult += 0.08f; break;
    case UPG_BIG_IRON:
        p->bullet_size *= 1.35f;
        p->dmg_mult *= 1.2f;
        break;
    case UPG_IMPACT: p->dmg_mult *= 1.1f; break;
    case UPG_WIDE_LENS: p->extra_proj++; break;
    case UPG_FOCUS_LENS: p->spread_mult = fmaxf(SPREAD_MIN, p->spread_mult * 0.5f); p->dmg_mult *= 1.1f; break;
    case UPG_GLASS_CANNON: p->dmg_mult *= 1.6f; break;
    case UPG_TITAN_HULL: p->max_hp = p->max_hp + 4 > 24 ? 24 : p->max_hp + 4; break;
    case UPG_FEATHER:
        p->speed = fminf(900.0f, p->speed * 1.15f);
        p->hit_radius = fmaxf(2.0f, p->hit_radius * 0.85f);
        break;
    case UPG_LONG_BARREL:
        p->range_mult = fminf(3.0f, p->range_mult * 1.4f);
        p->shot_speed_mult *= 1.2f;
        break;
    case UPG_HAIR_TRIGGER: p->rate_mult *= 1.3f; break;
    case UPG_FOUR_LEAF: p->luck = p->luck + 2 > 5 ? 5 : p->luck + 2; break;
    case UPG_MICRO_HULL: p->hit_radius = fmaxf(2.0f, p->hit_radius * 0.75f); break;
    case UPG_SMART_BOUNCE: p->bounce += 1; break;
    case UPG_MAGNUM: p->pierce += 2; p->dmg_mult *= 1.15f; break;
    case UPG_SPRAY: p->rate_mult *= 1.45f; break;
    case UPG_HEAVY_CALIBER:
        p->dmg_mult *= 1.4f;
        p->bullet_size *= 1.25f;
        break;
    case UPG_X_BLOOD_ROUNDS: p->dmg_mult *= 1.3f; break;
    case UPG_X_OVERDRIVE:
        p->rate_mult *= 1.3f;
        p->speed = fminf(900.0f, p->speed * 1.15f);
        p->dash_cooldown = fmaxf(0.15f, p->dash_cooldown * 0.7f);
        break;
    case UPG_X_HELLFIRE: p->dmg_mult *= 1.4f; break;
    case UPG_X_STORM: p->rate_mult *= 1.3f; break;
    case UPG_X_DEMON: p->dmg_mult *= 1.8f; p->extra_proj += 2; break;
    case UPG_X_GOD_CANNON:
        p->dmg_mult *= 1.5f;
        p->rate_mult *= 1.25f;
        p->extra_proj++;
        p->pierce += 2;
        p->homing += 3.0f;
        break;
    default: break; /* effect read through has() by the hooks below */
    }
    malus_stat_effect(p, u); /* every upgrade's malus (only the stat ones do something here) */
}

/* The stat floors and caps (also those of the stat trades): they hold whatever is turned on. */
static void stats_clamp(Stats *s) {
    s->dmg_mult = fmaxf(s->dmg_mult, 0.3f);
    s->rate_mult = fmaxf(s->rate_mult, 0.3f);
    s->range_mult = clampf(s->range_mult, 0.35f, 3.0f * 1.3f);
    s->shot_speed_mult = fmaxf(s->shot_speed_mult, 0.4f);
    s->speed = clampf(s->speed, 330.0f, 900.0f);
    s->dash_cooldown = clampf(s->dash_cooldown, 0.15f, 2.0f);
    s->hit_radius = clampf(s->hit_radius, 2.0f, 9.0f);
    s->luck = s->luck < -3 ? -3 : s->luck > 5 ? 5 : s->luck;
    s->spread_mult = clampf(s->spread_mult, SPREAD_MIN, SPREAD_MAX);
    s->max_hp = s->max_hp < 2 ? 2 : s->max_hp > 24 ? 24 : s->max_hp;
    if (s->drones > MAX_DRONES) s->drones = MAX_DRONES;
}

void stats_compute(Stats *out, bool ignore_off) {
    const Player *p = &G.pl;
    *out = p->base;
    for (int u = 0; u < UPG_COUNT; u++) {
        int n = ignore_off || !p->upg_off[u] ? p->upg_count[u] : 0;
        for (int k = 0; k < n; k++) stat_effect(out, u);
    }
    stats_clamp(out);
}

void player_recompute(void) {
    Player *p = &G.pl;
    Stats s;
    stats_compute(&s, false);
    p->dmg_mult = s.dmg_mult;
    p->rate_mult = s.rate_mult;
    p->shot_speed_mult = s.shot_speed_mult;
    p->speed = s.speed;
    p->dash_cooldown = s.dash_cooldown;
    p->magnet = s.magnet;
    p->range_mult = s.range_mult;
    p->hit_radius = s.hit_radius;
    p->spread_mult = s.spread_mult;
    p->bullet_size = s.bullet_size;
    p->homing = s.homing;
    p->max_hp = s.max_hp;
    p->extra_proj = s.extra_proj;
    p->pierce = s.pierce;
    p->bounce = s.bounce;
    p->drones = s.drones;
    p->luck = s.luck;
    if (p->hp > p->max_hp) p->hp = p->max_hp; /* turning a heart container back on never heals */
}

void upgrade_grant(int u) {
    Player *p = &G.pl;
    if (u < 0 || u >= UPG_COUNT || p->upg_count[u] > 0) return; /* never twice */
    p->chaos[u] = false; /* owned for real now: CHAOS no longer lends it */
    p->upg_count[u]++;
    player_recompute();
    /* One-time effects of taking it. */
    switch (u) {
    case UPG_HULL: p->hp = p->hp + 2 > p->max_hp ? p->max_hp : p->hp + 2; break;
    case UPG_TITAN_HULL: p->hp = p->hp + 4 > p->max_hp ? p->max_hp : p->hp + 4; break;
    case UPG_PIGGY_BANK: p->coins += 12; break;
    case UPG_BOMB_BELT: p->bombs += 3; break;
    case UPG_SKELETON_KEY: p->keys += 3; break;
    case UPG_SECOND_WIND: p->revives++; break;
    case UPG_BARRIER: player_add_shield(2); break;
    default: break;
    }
}

void upgrade_apply(int u) {
    if (u < 0 || u >= UPG_COUNT || G.pl.upg_count[u] > 0) return;
    upgrade_grant(u);
    char desc[200];
    upgrade_banner_text(u, desc, sizeof desc);
    fx_banner(UPGRADES[u].name, desc, col_hex(UPGRADES[u].color, 1), 2.6f);
}

/* Only the purely one-time ones cannot be turned off (their effect is already spent). */
bool upgrade_toggleable(int u) { return u >= 0 && u < UPG_COUNT && G.pl.upg_count[u] > 0 && u != UPG_SKELETON_KEY; }

bool upgrade_toggle(int u) {
    if (!upgrade_toggleable(u)) return false;
    G.pl.upg_off[u] = !G.pl.upg_off[u];
    player_recompute();
    G.toggles++;
    return true;
}

/* ---- Firing --------------------------------------------------------------- */

float upg_volley_damage_mult(void) {
    float m = 1.0f;
    int n = has(UPG_BLOODLUST);
    if (n) m *= 1.0f + fminf(0.02f * (float)(G.pl.room_kills * n), 0.4f * (float)n);
    if (hurt()) m *= 1.0f + 0.3f * (float)has(UPG_WOUNDED_FURY);
    if (has(UPG_LAST_STAND)) m *= 1.0f + 0.1f * missing_hearts();
    if (has(UPG_RUN_GUN) && v2_len2(G.pl.vel) > 80.0f * 80.0f) m *= 1.2f;
    if (has(UPG_GROWTH)) m *= 1.0f + fminf(0.03f * (float)G.pl.growth, 0.6f);
    return m;
}

static bool adrenaline_on(void) { return has(UPG_ADRENALINE) && G.pl.hp <= 2; }
float upg_fire_rate_mult(void) {
    float m = adrenaline_on() ? 1.4f : 1.0f;
    if (hurt()) m *= 1.0f + 0.25f * (float)has(UPG_SCAR_TISSUE);
    if (has(UPG_TURRET_MODE) && v2_len2(G.pl.vel) < 60.0f * 60.0f) m *= 1.4f;
    if (has(UPG_RAMPAGE)) m *= 1.0f + fminf(0.03f * (float)G.pl.room_kills, 0.45f);
    if (has(UPG_DEFLECTOR)) m /= 3.0f;
    return m;
}
float upg_move_speed_mult(void) {
    float m = adrenaline_on() ? 1.15f : 1.0f;
    if (G.in.fire) m *= fmaxf(0.4f, 1.0f - malus_pct(MAL_FIRE_SLOW));
    if (has(UPG_LAST_STAND)) m *= 1.0f + 0.05f * missing_hearts();
    return m;
}
float upg_shot_speed_mult(void) { return has(UPG_DEFLECTOR) ? 0.4f : 1.0f; }

void upg_on_bullet_spawn(PBullet *b) {
    const Player *p = &G.pl;
    b->radius *= p->bullet_size;
    if (has(UPG_IMPACT)) b->knockback *= 1.0f + 1.5f * (float)has(UPG_IMPACT);
    if (has(UPG_DEFLECTOR)) { /* huge and slow; lives longer so the range stays similar */
        b->radius *= 2.2f;
        b->vel = v2_scale(b->vel, upg_shot_speed_mult());
        b->life *= 2.2f;
    }
    if (has(UPG_BLOOD_SCENT) && hurt()) b->homing += 4.0f;
    int n = has(UPG_CRIT);
    if (n && chance(crit_chance(n))) {
        b->damage *= 3.0f;
        b->radius *= 1.3f;
        b->flags |= PB_CRIT;
        b->col = col_hex(0xfff07a, 1);
    }
}

static PBullet *extra_shot(V2 pos, float angle, float speed, float radius, float dmg, float life, SDL_FColor col) {
    const Player *p = &G.pl;
    PBullet *b = pbullet_spawn(pos, v2_scale(v2_angle(angle), speed * p->shot_speed_mult), radius, dmg, col);
    if (!b) return NULL;
    b->life = life * p->range_mult;
    b->pierce = p->pierce;
    b->bounces = p->bounce / 2;
    b->homing = p->homing;
    b->knockback = 30.0f;
    upg_on_bullet_spawn(b);
    return b;
}

void upg_on_volley(V2 muzzle, float aim, float dmg, SDL_FColor col) {
    Player *p = &G.pl;
    p->volleys++;
    (void)col;
    if (has(UPG_ECHO) && p->echo_t <= 0.0f) {
        p->echo_t = 0.12f;
        p->echo_aim = aim;
        p->echo_dmg = dmg * 0.5f;
    }
    int n = has(UPG_TAIL_GUN);
    for (int i = 0; i < n; i++)
        extra_shot(p->pos, aim + PI_F + ((float)i - (float)(n - 1) * 0.5f) * 0.22f, 1100.0f, 4.0f, dmg * 0.8f, 0.7f, col);
    n = has(UPG_FLANK);
    if (n && p->volleys % (n >= 2 ? 1 : 3) == 0) {
        extra_shot(p->pos, aim + PI_F * 0.5f, 1100.0f, 4.0f, dmg * 0.8f, 0.6f, col);
        extra_shot(p->pos, aim - PI_F * 0.5f, 1100.0f, 4.0f, dmg * 0.8f, 0.6f, col);
    }
    n = has(UPG_CAPACITOR);
    if (n && p->volleys % (n >= 2 ? 6 : 10) == 0) {
        PBullet *b = extra_shot(muzzle, aim, 1300.0f, 14.0f, dmg * 6.0f, 1.0f, col_hex(0xd8f6ff, 1));
        if (b) {
            b->pierce += 6;
            b->knockback = 300.0f;
            b->flags |= PB_CHARGED;
        }
        sfx_play_ex(SFX_SNIPER_FIRE, 0.6f, 1.5f, 0.0f);
        fx_particle(muzzle, v2(0, 0), 0.15f, 70.0f, col_hex(0xd8f6ff, 0.9f), 0.0f, PT_GLOW);
    }
}

void upg_bullet_update(PBullet *b) {
    /* RETURN VECTOR: halfway through its life a shot turns around and homes back. */
    if (b->flags & PB_VORTEX) { /* BLACK HOLE: pulls enemies in and grinds them */
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            V2 d = v2_sub(b->pos, e->pos);
            float dist = v2_len(d);
            if (e->hp <= 0.0f || dist > 260.0f || dist < 1.0f) continue;
            if (e->type != EN_BOSS) e->vel = v2_add(e->vel, v2_scale(d, 900.0f * DT / dist));
            if (dist < 70.0f + e->radius) enemy_damage(e, b->damage * DT, v2(0, 0));
        }
        return;
    }
    if ((!has(UPG_BOOMERANG) && !(b->flags & PB_RETURNS)) || (b->flags & (PB_MINE | PB_FRAGMENT))) return;
    const Player *p = &G.pl;
    if (!(b->flags & PB_RETURNED)) {
        if (b->age < b->life) return;
        b->flags |= PB_RETURNED;
        b->last_hit = -1;
    }
    V2 to = v2_sub(p->pos, b->pos);
    float d = v2_len(to);
    if (d < 24.0f) {
        b->life = -1.0f;
        return;
    }
    float spd = fmaxf(v2_len(b->vel), 700.0f);
    b->vel = v2_lerp(b->vel, v2_scale(to, spd / d), smooth_k(10.0f));
}

/* ---- Hits ----------------------------------------------------------------- */

float upg_hit_damage(const PBullet *b, const Enemy *e) {
    const Player *p = &G.pl;
    float dmg = b->damage;
    int n;
    if ((n = has(UPG_SCOPE)) > 0) dmg *= 1.0f + 0.6f * (float)n * fminf(b->age / 0.6f, 1.0f);
    if ((n = has(UPG_POINT_BLANK)) > 0 && v2_dist2(e->pos, p->pos) < 220.0f * 220.0f) dmg *= 1.0f + 0.5f * (float)n;
    if ((n = has(UPG_BULLY)) > 0 && e->hp < e->max_hp * 0.5f) dmg *= 1.0f + 0.4f * (float)n;
    if ((n = has(UPG_GIANT_SLAYER)) > 0 && e->max_hp >= 200.0f) dmg *= 1.0f + 0.6f * (float)n;
    if (has(UPG_FROSTBITE) && e->slow_t > 0.0f) dmg *= 1.35f;
    if (has(UPG_MOMENTUM)) dmg *= 1.0f + p->momentum;
    /* GUILLOTINE: a hit that leaves an enemy under 15% finishes it (never the boss). */
    if (has(UPG_GUILLOTINE) && e->type != EN_BOSS && e->hp - dmg < e->max_hp * 0.15f) {
        fx_particle(e->pos, v2(0, 0), 0.3f, e->radius * 3.0f, col_hex(0xff4466, 1), 0.0f, PT_RING);
        return e->hp + 1.0f;
    }
    return dmg;
}

void explode_at(V2 pos, float radius, float dmg, const Enemy *skip, uint32_t color) {
    for (int i = 0; i < G.nen; i++) {
        Enemy *o = &G.en[i];
        if (o == skip || o->hp <= 0.0f) continue;
        float r = radius + o->radius;
        if (v2_dist2(o->pos, pos) < r * r) enemy_damage(o, dmg, v2_scale(v2_norm(v2_sub(o->pos, pos)), 250.0f));
    }
    SDL_FColor c = col_hex(color, 1);
    fx_particle(pos, v2(0, 0), 0.3f, radius * 2.0f, col_a(c, 0.8f), 0.0f, PT_RING);
    fx_particle(pos, v2(0, 0), 0.15f, radius * 1.2f, col_a(c, 0.6f), 0.0f, PT_GLOW);
    fx_burst(pos, 10, 80.0f, 400.0f, 0.3f, 2.5f, c);
}

static void lightning(V2 a, V2 b) {
    SDL_FColor c = col_hex(0xb8d0ff, 1);
    V2 d = v2_sub(b, a);
    for (int i = 0; i <= 8; i++) {
        V2 p = v2_add(a, v2_scale(d, (float)i / 8.0f));
        p = v2_add(p, v2(frand(-10, 10), frand(-10, 10)));
        fx_particle(p, v2(0, 0), 0.12f, 10.0f, c, 0.0f, PT_GLOW);
    }
}

void chain_lightning(Enemy *from, float dmg, int jumps, float range) {
    enum { MAX_JUMPS = 8 };
    Enemy *hit[MAX_JUMPS + 1] = {from};
    int nhit = 1;
    if (jumps > MAX_JUMPS) jumps = MAX_JUMPS;
    for (int j = 0; j < jumps; j++) {
        Enemy *best = NULL;
        float best_d2 = range * range;
        for (int i = 0; i < G.nen; i++) {
            Enemy *o = &G.en[i];
            bool seen = false;
            for (int k = 0; k < nhit; k++) seen |= hit[k] == o;
            float d2 = v2_dist2(o->pos, from->pos);
            if (o->hp > 0.0f && !seen && d2 < best_d2) {
                best_d2 = d2;
                best = o;
            }
        }
        if (!best) break;
        enemy_damage(best, dmg, v2(0, 0));
        lightning(from->pos, best->pos);
        hit[nhit++] = best;
        from = best;
    }
}

void upg_after_hit(PBullet *b, Enemy *e, float dmg) {
    Player *p = &G.pl;
    int n;
    if (has(UPG_MOMENTUM)) {
        p->momentum = fminf(p->momentum + 0.01f, 0.4f);
        p->momentum_t = 0.0f;
    }
    if (has(UPG_FROSTBITE) && chance(proc(FROSTBITE_CHANCE))) e->slow_t = fmaxf(e->slow_t, 1.5f);
    if (has(UPG_PYROMANIAC) && chance(proc(IGNITE_CHANCE))) {
        e->burn_dps = fmaxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, dmg * 1.2f);
        e->burn_t = fmaxf(e->burn_t, 2.0f);
    }
    if (has(UPG_TOXIC_CLOUD) && chance(proc(IGNITE_CHANCE))) {
        e->poison_dps = fmaxf(e->poison_t > 0.0f ? e->poison_dps : 0.0f, dmg * 0.5f);
        e->poison_t = fmaxf(e->poison_t, 3.0f);
    }
    /* OVERKILL: what the killing blow had left over goes to the closest enemy. */
    if (has(UPG_OVERKILL) && e->hp < 0.0f && !(b->flags & PB_FRAGMENT)) {
        Enemy *o = enemy_nearest(e->pos, 400.0f);
        if (o == e) o = NULL;
        if (o && o->hp > 0.0f) {
            enemy_damage(o, -e->hp, v2(0, 0));
            lightning(e->pos, o->pos);
        }
    }
    if (has(UPG_FROST)) e->slow_t = 1.5f;
    if ((n = has(UPG_VENOM)) > 0) {
        float dps = dmg * 0.35f * (float)n;
        e->poison_dps = e->poison_t > 0.0f ? fmaxf(e->poison_dps, dps) : dps;
        e->poison_t = 3.0f;
    }
    if ((n = has(UPG_THERMITE)) > 0 && chance(thermite_chance(n))) {
        e->burn_dps = fmaxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, dmg * 1.2f);
        e->burn_t = 2.0f;
    }
    if (has(UPG_X_HELLFIRE)) {
        e->burn_dps = fmaxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, dmg);
        e->burn_t = 2.0f;
    }
    if (has(UPG_X_BLOOD_ROUNDS)) {
        float dps = dmg * 0.4f;
        e->poison_dps = e->poison_t > 0.0f ? fmaxf(e->poison_dps, dps) : dps;
        e->poison_t = 3.0f;
    }
    if (b->flags & PB_MINE) {
        explode_at(b->pos, 110.0f, dmg * 0.8f, e, 0xffaa33);
        sfx_play_at(SFX_EXPLODE_SMALL, b->pos);
    }
    if (b->flags & PB_FRAGMENT) return; /* no chain reactions from fragments */

    if (has(UPG_X_STORM) && chance(proc(STORM_CHANCE))) chain_lightning(e, dmg * 0.6f, 3, 360.0f);
    if ((n = has(UPG_ARC)) > 0 && chance(arc_chance(n))) {
        Enemy *best = NULL;
        float best_d2 = 280.0f * 280.0f;
        for (int i = 0; i < G.nen; i++) {
            Enemy *o = &G.en[i];
            float d2 = v2_dist2(o->pos, e->pos);
            if (o != e && o->hp > 0.0f && d2 < best_d2) {
                best_d2 = d2;
                best = o;
            }
        }
        if (best) {
            enemy_damage(best, dmg * 0.6f, v2(0, 0));
            lightning(e->pos, best->pos);
        }
    }
    if ((n = has(UPG_PAYLOAD)) > 0 && chance(payload_chance(n)))
        explode_at(b->pos, 80.0f, dmg * 0.5f, e, 0xffa64d);
    if ((n = has(UPG_SHRAPNEL)) > 0 && chance(shrapnel_chance(n))) {
        float base = v2_heading(b->vel);
        for (int k = 0; k < 3; k++) {
            float a = base + frand(-1.2f, 1.2f);
            PBullet *f = pbullet_spawn(b->pos, v2_scale(v2_angle(a), 900.0f), 3.0f, dmg * 0.4f, col_hex(0xd0d0e0, 1));
            if (!f) break;
            f->life = 0.25f * p->range_mult;
            f->last_hit = e->uid;
            f->flags = PB_FRAGMENT;
            f->knockback = 10.0f;
        }
    }
    if (b->flags & PB_CHARGED) fx_burst(b->pos, 8, 100.0f, 400.0f, 0.2f, 3.0f, col_hex(0xd8f6ff, 1));
}

void upg_on_kill(const Enemy *e) {
    Player *p = &G.pl;
    p->room_kills++;
    int n = has(UPG_LEECH);
    if (n && ++p->leech_kills >= (n >= 2 ? 25 : 40)) {
        p->leech_kills = 0;
        if (p->hp < p->max_hp) {
            p->hp++;
            sfx_play(SFX_HEART);
            fx_popup(v2_add(p->pos, v2(0, -36)), col_hex(0xff3a6e, 1), "+HP");
        }
    }
    if ((n = has(UPG_VOLATILE)) > 0) explode_at(e->pos, 90.0f + e->radius, 18.0f * (float)n * aura_scale(), e, 0xff5a1f);
    if (has(UPG_X_DEMON)) explode_at(e->pos, 120.0f + e->radius, 30.0f * aura_scale(), e, 0xb0103a);
    if ((n = has(UPG_CHAIN_REACTION)) > 0) {
        float off = frand(0.0f, TAU_F);
        for (int k = 0; k < 6; k++) {
            PBullet *b = pbullet_spawn(e->pos, v2_scale(v2_angle(off + TAU_F * (float)k / 6.0f), 1000.0f), 4.0f,
                                       7.0f * (float)n * aura_scale(), col_hex(0xff7ad9, 1));
            if (!b) break;
            b->life = 0.45f * p->range_mult;
            b->last_hit = e->uid;
            b->knockback = 20.0f;
            b->flags = PB_FRAGMENT;
        }
    }
    if (has(UPG_PYROMANIAC) && e->burn_t > 0.0f) explode_at(e->pos, 100.0f + e->radius, 25.0f * aura_scale(), e, 0xff6a2a);
    if (has(UPG_TOXIC_CLOUD) && e->poison_t > 0.0f) {
        for (int i = 0; i < G.nen; i++) {
            Enemy *o = &G.en[i];
            if (o == e || o->hp <= 0.0f || v2_dist2(o->pos, e->pos) > 160.0f * 160.0f) continue;
            o->poison_dps = fmaxf(o->poison_t > 0.0f ? o->poison_dps : 0.0f, e->poison_dps);
            o->poison_t = fmaxf(o->poison_t, 3.0f);
        }
        fx_particle(e->pos, v2(0, 0), 0.5f, 320.0f, col_hex(0x9aff3a, 0.35f), 0.0f, PT_GLOW);
    }
    if ((n = has(UPG_BLACK_HOLE)) > 0 && chance(black_hole_chance(n))) {
        PBullet *b = pbullet_spawn(e->pos, v2(0, 0), 20.0f, 60.0f * aura_scale(), col_hex(0x8a5aff, 1));
        if (b) {
            b->life = 1.6f;
            b->flags = PB_VORTEX;
            b->pierce = 999;
            sfx_play_at(SFX_TIME_STOP, e->pos);
        }
    }
}

void upg_reflect(PBullet *b) {
    if (!has(UPG_DEFLECTOR) || (b->flags & (PB_REFLECTED | PB_MINE | PB_FRAGMENT | PB_VORTEX))) return;
    int turned = 0;
    for (int i = 0; i < G.neb; i++) {
        EBullet *e = &G.eb[i];
        float r = b->radius + e->radius;
        if (e->life <= 0.0f || v2_dist2(e->pos, b->pos) > r * r) continue;
        /* Sent back the way it came, as one of ours. Marked dead: removed by its own loop. */
        e->life = -1.0f;
        float spd = fmaxf(v2_len(e->vel) * 1.3f, 700.0f);
        V2 vel = v2_len2(e->vel) > 1.0f ? v2_scale(v2_norm(e->vel), -spd) : v2_scale(v2_norm(v2_sub(e->pos, b->pos)), spd);
        PBullet *nb = pbullet_spawn(e->pos, vel, fmaxf(e->radius, 5.0f), 10.0f * G.pl.dmg_mult, col_hex(0x7dffef, 1));
        if (!nb) break;
        nb->life = 1.2f;
        nb->knockback = 40.0f;
        nb->flags = PB_REFLECTED;
        turned++;
    }
    if (turned) {
        sfx_play_at(SFX_DEFLECT, b->pos);
        fx_particle(b->pos, v2(0, 0), 0.15f, b->radius * 2.5f, col_hex(0x7dffef, 0.7f), 0.0f, PT_RING);
    }
}

/* ---- Player events --------------------------------------------------------- */

bool upg_on_lethal_hit(void) {
    Player *p = &G.pl;
    if (p->revives <= 0 || !has(UPG_SECOND_WIND)) return false; /* turned off: the charge waits */
    p->revives--;
    p->hp = 2;
    p->invuln = 2.0f;
    /* Called from inside the enemy bullet loop: mark bullets dead, the loop removes them. */
    for (int i = 0; i < G.neb; i++) {
        G.eb[i].life = -1.0f;
        fx_particle(G.eb[i].pos, v2(0, 0), 0.25f, G.eb[i].radius * 2.5f, G.eb[i].col, 0.0f, PT_GLOW);
    }
    sfx_play(SFX_BOMB);
    fx_particle(p->pos, v2(0, 0), 0.8f, 900.0f, col_hex(0xfff5c0, 0.9f), 0.0f, PT_RING);
    fx_banner("SECOND WIND", "you were saved - once", col_hex(0xfff5c0, 1), 2.0f);
    return true;
}

static void ring_of_shots(int count, float dmg, float speed, float life, uint32_t color) {
    const Player *p = &G.pl;
    float off = frand(0.0f, TAU_F);
    for (int i = 0; i < count; i++)
        extra_shot(p->pos, off + TAU_F * (float)i / (float)count, speed, 5.0f, dmg, life, col_hex(color, 1));
}

void upg_on_hurt(void) {
    int n = has(UPG_REACTIVE);
    if (n) ring_of_shots(16 + 8 * (n - 1), 8.0f * aura_scale(), 900.0f, 0.6f, 0xff6a4d);
}

void upg_on_active(void) {
    int n = has(UPG_DASH_NOVA);
    if (n) ring_of_shots(10 * n, 6.0f * aura_scale(), 1000.0f, 0.5f, 0x7dfcff);
}

static V2 orb_pos(int i, int n) {
    return v2_add(G.pl.pos, v2_scale(v2_angle(G.time * 2.6f + TAU_F * (float)i / (float)n), 78.0f));
}

void upg_update(void) {
    Player *p = &G.pl;
    int n;
    /* GUARDIAN ORB: blocks enemy bullets, grinds enemies it touches. */
    if ((n = has(UPG_ORB)) > 0) {
        for (int k = 0; k < n; k++) {
            V2 o = orb_pos(k, n);
            for (int i = 0; i < G.neb; i++) {
                EBullet *b = &G.eb[i];
                float r = 13.0f + b->radius * 0.6f;
                if (b->life > 0.0f && v2_dist2(b->pos, o) < r * r) {
                    b->life = -1.0f;
                    fx_particle(b->pos, v2(0, 0), 0.15f, 14.0f, col_hex(0xffe3a0, 1), 0.0f, PT_GLOW);
                }
            }
            for (int i = 0; i < G.nen; i++) {
                Enemy *e = &G.en[i];
                float r = e->radius + 13.0f;
                if (e->hp > 0.0f && v2_dist2(e->pos, o) < r * r) enemy_damage(e, 90.0f * aura_scale() * DT, v2(0, 0));
            }
        }
    }
    /* TESLA FIELD: constant damage to everything close. */
    if ((n = has(UPG_TESLA)) > 0) {
        float radius = 150.0f + 30.0f * (float)(n - 1), dps = 14.0f * (float)n * aura_scale();
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            if (e->hp <= 0.0f || v2_dist2(e->pos, p->pos) > (radius + e->radius) * (radius + e->radius)) continue;
            e->hp -= enemy_damage_cap(e, dps * DT);
            G.last_enemy_damage_t = G.time;
            if ((G.ticks + e->uid * 7) % 18 == 0) lightning(p->pos, e->pos);
        }
    }
    /* RAM PLATING: the dash (or the shield bubble) itself hurts. */
    if ((n = has(UPG_RAM)) > 0 && (p->dash_t > 0.0f || active_shield_on())) {
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            float r = e->radius + PLAYER_BODY_R + 12.0f;
            if (e->hp > 0.0f && v2_dist2(e->pos, p->pos) < r * r)
                enemy_damage(e, 260.0f * (float)n * aura_scale() * DT,
                             p->dash_t > 0.0f ? v2_scale(p->dash_dir, 60.0f) : v2_scale(v2_norm(v2_sub(e->pos, p->pos)), 60.0f));
        }
    }
    /* MOMENTUM fades after a second without hitting anything. */
    p->momentum_t += DT;
    if (p->momentum_t > 1.0f) p->momentum = fmaxf(0.0f, p->momentum - 0.4f * DT);
    /* ECHO: the last volley again, weaker, a beat later. */
    if (p->echo_t > 0.0f) {
        p->echo_t -= DT;
        if (p->echo_t <= 0.0f && has(UPG_ECHO) && !sword_on()) weapon_echo(p->echo_aim, p->echo_dmg);
    }
    bool fighting = G.nen > 0 && !G.room.cleared;
    /* MISSILE POD: a homing missile at the nearest enemy. */
    if ((n = has(UPG_MISSILE_POD)) > 0 && fighting) {
        p->missile_cd -= DT;
        Enemy *t = enemy_nearest(p->pos, 1400.0f);
        if (p->missile_cd <= 0.0f && t) {
            p->missile_cd = 1.5f / (float)n;
            V2 dir = v2_norm(v2_sub(t->pos, p->pos));
            PBullet *b = pbullet_spawn(p->pos, v2_scale(v2_rotate(dir, frand(-0.6f, 0.6f)), 650.0f), 6.0f, 22.0f * aura_scale(),
                                       col_hex(0xffb04d, 1));
            if (b) {
                b->life = 2.5f;
                b->homing = 6.0f;
                b->knockback = 60.0f;
                b->flags = PB_EXPLOSIVE;
                sfx_play_ex(SFX_SHOT_ROCKET, 0.6f, 1.3f, 0.0f);
            }
        }
    }
    /* ORBITAL STRIKE: a beam from above on a random enemy. */
    if ((n = has(UPG_ORBITAL)) > 0 && fighting) {
        p->orbital_cd -= DT;
        if (p->orbital_cd <= 0.0f) {
            p->orbital_cd = 3.0f / (float)n;
            int alive[MAX_ENEMIES], na = 0;
            for (int i = 0; i < G.nen; i++)
                if (G.en[i].hp > 0.0f) alive[na++] = i;
            if (na > 0) {
                V2 at = G.en[alive[irand(0, na - 1)]].pos;
                for (int k = 0; k < 10; k++)
                    fx_particle(v2(at.x + frand(-6, 6), at.y - (float)k * 60.0f), v2(0, 0), 0.25f, 18.0f, col_hex(0x9fe8ff, 0.7f), 0.0f, PT_GLOW);
                explode_at(at, 95.0f, 70.0f * aura_scale(), NULL, 0x9fe8ff);
                sfx_play_at(SFX_EXPLODE_SMALL, at);
            }
        }
    }
    /* SPIKED HULL: contact hurts them, a lot. */
    if ((n = has(UPG_SPIKED_HULL)) > 0) {
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            float r = e->radius + PLAYER_BODY_R + 8.0f;
            if (e->hp > 0.0f && v2_dist2(e->pos, p->pos) < r * r) enemy_damage(e, 180.0f * (float)n * aura_scale() * DT, v2(0, 0));
        }
    }
    /* MINE LAYER: a mine behind the ship every so often while moving and fighting. */
    if ((n = has(UPG_MINES)) > 0 && G.nen > 0 && v2_len2(p->vel) > 100.0f * 100.0f) {
        p->mine_cd -= DT;
        if (p->mine_cd <= 0.0f) {
            p->mine_cd = n >= 2 ? 0.9f : 1.4f;
            PBullet *b = pbullet_spawn(p->pos, v2(0, 0), 9.0f, 30.0f * aura_scale(), col_hex(0xffaa33, 1));
            if (b) {
                b->life = 7.0f;
                b->knockback = 300.0f;
                b->flags = PB_MINE;
            }
        }
    }
}

bool upg_block_hit(void) {
    Player *p = &G.pl;
    if (!has(UPG_EMERGENCY) || p->emergency_used) return false;
    p->emergency_used = true;
    p->invuln = 1.0f;
    sfx_play(SFX_SHIELD);
    fx_particle(p->pos, v2(0, 0), 0.4f, 160.0f, col_hex(0x66d9ff, 1), 0.0f, PT_RING);
    fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(0x66d9ff, 1), "BLOCKED");
    return true;
}

void upg_on_bounce(PBullet *b) {
    if (!has(UPG_SMART_BOUNCE)) return;
    Enemy *t = enemy_nearest(b->pos, 1200.0f);
    if (t) b->vel = v2_scale(v2_norm(v2_sub(t->pos, b->pos)), v2_len(b->vel));
}

void upg_on_room_enter(void) {
    Player *p = &G.pl;
    p->room_kills = 0;
    p->emergency_used = false;
    p->momentum = 0.0f;
    malus_on_room_enter();
    int n = has(UPG_BARRIER);
    if (n && !G.room.cleared && p->shield < 2 * n) player_add_shield(2 * n - p->shield);
}

void upg_on_room_clear(void) {
    malus_on_room_clear();
    if (has(UPG_GROWTH)) G.pl.growth++;
    if (has(UPG_PIGGY_BANK)) {
        G.pl.coins += 3;
        fx_popup(v2_add(G.pl.pos, v2(0, -36)), col_hex(0xffd23f, 1), "+3 COINS");
    }
}

float upg_enemy_bullet_speed(void) {
    return (has(UPG_CHRONO) ? powf(0.85f, (float)has(UPG_CHRONO)) : 1.0f) * (1.0f + malus_pct(MAL_ENEMY_BULLETS));
}

void upg_draw(void) {
    const Player *p = &G.pl;
    int n;
    draw_blend_add();
    if ((n = has(UPG_TESLA)) > 0) {
        float radius = 150.0f + 30.0f * (float)(n - 1);
        draw_ring(p->pos, radius, 2.0f, col_hex(0x80c8ff, 0.10f + 0.05f * sinf(G.time * 9.0f)));
    }
    if ((n = has(UPG_ORB)) > 0) {
        for (int k = 0; k < n; k++) {
            V2 o = orb_pos(k, n);
            draw_glow(o, 26.0f, col_hex(0xffe3a0, 0.35f));
            draw_circle(o, 9.0f, col_hex(0xffe3a0, 1));
            draw_circle(o, 4.5f, COL_WHITE);
        }
    }
    if (p->shield > 0) draw_ring(p->pos, 24.0f + 1.5f * sinf(G.time * 5.0f), 2.0f, col_hex(0x66d9ff, 0.55f));
    if (has(UPG_EMERGENCY) && !p->emergency_used) /* the spare block, ready */
        draw_ngon_outline(p->pos, 28.0f, 6, G.time, 1.5f, col_hex(0x66d9ff, 0.35f));
    if (has(UPG_SPIKED_HULL))
        for (int k = 0; k < 8; k++) {
            V2 d = v2_angle(G.time * 2.0f + TAU_F * (float)k / 8.0f);
            draw_line(v2_add(p->pos, v2_scale(d, 19.0f)), v2_add(p->pos, v2_scale(d, 27.0f)), 2.0f, col_hex(0xd0d0e0, 0.6f));
        }
    draw_blend_alpha();
}
