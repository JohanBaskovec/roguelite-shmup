#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Every upgrade has a bonus AND a malus: turning an upgrade off (TAB menu) removes both, so
 * a build is shaped by choosing which trade-offs to live with. One malus per upgrade, from a
 * small set of kinds (MalusKind) with an amount; the kinds that change a stat are applied by
 * stat_effect() (so the stats stay derived), the others are read through malus() by the code
 * they affect. CHAOS lends upgrades without their malus (malus() only counts upgrades owned).
 * The text is generated from the kind and the amount, so it always shows the exact numbers;
 * MAL_CUSTOM is a trade-off built into the upgrade's own effect (weapon-like upgrades).
 */

const MalusDef MALUSES[UPG_COUNT] = {
    [UPG_RAPID] = {MAL_OVERHEAT, 8},
    [UPG_DAMAGE] = {MAL_SHOT_SPEED, 20},
    [UPG_MULTI] = {MAL_DAMAGE, 15},
    [UPG_PIERCE] = {MAL_RANGE, 20},
    [UPG_BOUNCE] = {MAL_DAMAGE, 12},
    [UPG_HOMING] = {MAL_RATE, 12},
    [UPG_THRUSTER] = {MAL_DRIFT, 40},
    [UPG_HULL] = {MAL_SPEED, 8},
    [UPG_DRONE] = {MAL_RATE, 10},
    [UPG_MAGNET] = {MAL_ENEMY_BULLETS, 12},
    [UPG_VELOCITY] = {MAL_RECOIL, 1},

    [UPG_FROST] = {MAL_RATE, 10},
    [UPG_VENOM] = {MAL_DAMAGE, 12},
    [UPG_THERMITE] = {MAL_OVERHEAT, 6},
    [UPG_ARC] = {MAL_RECHARGE, 25},
    [UPG_PAYLOAD] = {MAL_SHOT_SPEED, 20},
    [UPG_SHRAPNEL] = {MAL_RANGE, 20},
    [UPG_BIG_IRON] = {MAL_SHOT_SPEED, 15},
    [UPG_CRIT] = {MAL_SPREAD, 30},
    [UPG_GUILLOTINE] = {MAL_FIRST_HIT, 1},
    [UPG_BOOMERANG] = {MAL_RANGE, 20},
    [UPG_PHASE] = {MAL_DAMAGE, 15},
    [UPG_IMPACT] = {MAL_RECOIL, 2},
    [UPG_TAIL_GUN] = {MAL_FIRE_SLOW, 20},
    [UPG_FLANK] = {MAL_SPEED, 8},
    [UPG_CAPACITOR] = {MAL_COLD_START, 15},
    [UPG_WIDE_LENS] = {MAL_SPREAD, 40},
    [UPG_FOCUS_LENS] = {MAL_RATE, 12},

    [UPG_GLASS_CANNON] = {MAL_MAX_HP, 1},
    [UPG_TITAN_HULL] = {MAL_SPEED, 10},
    [UPG_FEATHER] = {MAL_INVULN, 35},
    [UPG_LONG_BARREL] = {MAL_MOVING_RATE, 25},
    [UPG_HAIR_TRIGGER] = {MAL_DAMAGE, 10},
    [UPG_FOUR_LEAF] = {MAL_PRICES, 20},
    [UPG_MICRO_HULL] = {MAL_FIRST_HIT, 1},

    [UPG_PIGGY_BANK] = {MAL_HIT_COINS, 5},
    [UPG_BOMB_BELT] = {MAL_HITBOX, 20},
    [UPG_SKELETON_KEY] = {MAL_PRICES, 15}, /* one-time: cannot be turned off, the malus stays */
    [UPG_COUPON] = {MAL_HEAL, 1},

    [UPG_REACTIVE] = {MAL_NO_MERCY, 1},
    [UPG_SECOND_WIND] = {MAL_DAMAGE, 12},
    [UPG_BARRIER] = {MAL_RECHARGE, 25},
    [UPG_ORB] = {MAL_BIG_BULLETS, 20},
    [UPG_TESLA] = {MAL_RANGE, 20},
    [UPG_LEECH] = {MAL_HEAL, 1},
    [UPG_ADRENALINE] = {MAL_INVULN, 30},
    [UPG_BLOODLUST] = {MAL_REVENGE, 12},
    [UPG_RAM] = {MAL_DRIFT, 30},
    [UPG_DASH_NOVA] = {MAL_RECHARGE, 20},
    [UPG_CHRONO] = {MAL_BIG_BULLETS, 20},
    [UPG_VOLATILE] = {MAL_REVENGE, 15},
    [UPG_MINES] = {MAL_DRIFT, 30},

    [UPG_DEFLECTOR] = {MAL_CUSTOM, 0, "1/3 fire rate, very slow shots"},
    [UPG_WOUNDED_FURY] = {MAL_HEAL, 1},
    [UPG_SCAR_TISSUE] = {MAL_HIT_COINS, 4},
    [UPG_LAST_STAND] = {MAL_ENEMY_BULLETS, 12},
    [UPG_BLOOD_SCENT] = {MAL_SPREAD, 25},
    [UPG_LOADED_DICE] = {MAL_LUCK, 2},
    [UPG_SCOPE] = {MAL_MOVING_RATE, 20},
    [UPG_POINT_BLANK] = {MAL_RANGE, 30},
    [UPG_BULLY] = {MAL_DAMAGE, 12},
    [UPG_GIANT_SLAYER] = {MAL_CUSTOM, 0, "-25% damage to small enemies"},
    [UPG_MOMENTUM] = {MAL_DAMAGE, 20},
    [UPG_SMART_BOUNCE] = {MAL_SHOT_SPEED, 15},
    [UPG_CHAIN_REACTION] = {MAL_CROWD, 15},
    [UPG_GROWTH] = {MAL_DAMAGE, 15},
    [UPG_TURRET_MODE] = {MAL_SPEED, 12},
    [UPG_RUN_GUN] = {MAL_CUSTOM, 0, "-25% damage while standing still"},
    [UPG_FROSTBITE] = {MAL_RECHARGE, 20},
    [UPG_PYROMANIAC] = {MAL_OVERHEAT, 6},
    [UPG_TOXIC_CLOUD] = {MAL_HITBOX, 15},
    [UPG_OVERKILL] = {MAL_RATE, 12},
    [UPG_MISSILE_POD] = {MAL_SPEED, 8},
    [UPG_ORBITAL] = {MAL_TAX, 3},
    [UPG_SPIKED_HULL] = {MAL_HITBOX, 20},
    [UPG_ECHO] = {MAL_DAMAGE, 15},
    [UPG_MAGNUM] = {MAL_RATE, 15},
    [UPG_SPRAY] = {MAL_SPREAD, 60},
    [UPG_HEAVY_CALIBER] = {MAL_SHOT_SPEED, 25},
    [UPG_EMERGENCY] = {MAL_ACTIVE_TIME, 20},
    [UPG_RAMPAGE] = {MAL_FIRST_HIT, 1},
    [UPG_BLACK_HOLE] = {MAL_ENEMY_BULLETS, 12},
    [UPG_XRAY] = {MAL_LUCK, 1},

    [UPG_RICOCHET] = {MAL_DAMAGE, 15},
    [UPG_SNOWBALL] = {MAL_SHOT_SPEED, 25},
    [UPG_STICKY] = {MAL_DAMAGE, 25},
    [UPG_FIREWORKS] = {MAL_RANGE, 25},
    [UPG_PINBALL] = {MAL_DAMAGE, 20},
    [UPG_SHATTER] = {MAL_RATE, 10},
    [UPG_CONDUCTOR] = {MAL_SPINUP, 4},
    [UPG_ALCHEMIST] = {MAL_DAMAGE, 12},
    [UPG_CRIT_MASS] = {MAL_RANGE, 15},
    [UPG_FIRST_BLOOD] = {MAL_RATE, 12},
    [UPG_HIVEMIND] = {MAL_CROWD, 15},

    [UPG_DEAD_TRIGGER] = {MAL_CUSTOM, 0, "In a fight, the gun never|stops firing"},
    [UPG_CHARGE] = {MAL_RATE, 15},
    [UPG_GRAZE] = {MAL_HITBOX, 25},
    [UPG_BULLET_TIME] = {MAL_SPEED, 12},
    [UPG_CLONE] = {MAL_DAMAGE, 20},
    [UPG_BLOOD_MONEY] = {MAL_PRICES, 25},
    [UPG_WILD_CARD] = {MAL_LUCK, 1},
    [UPG_POINT_DEFENSE] = {MAL_RATE, 10},
    [UPG_TRAIL] = {MAL_HITBOX, 15},
    [UPG_AEGIS] = {MAL_SPEED, 15},
    [UPG_AFTERBURNER] = {MAL_RECHARGE, 20},
    [UPG_BERSERKER] = {MAL_FRAGILE, 1},
    [UPG_BOMB_FACTORY] = {MAL_CROWD, 12},
    [UPG_FLAWLESS] = {MAL_FIRST_HIT, 1},
    [UPG_VENGEANCE] = {MAL_INVULN, 40},

    [UPG_TWIN_BARREL] = {MAL_RATE, 12},
    [UPG_HELIX] = {MAL_RANGE, 15},
    [UPG_BURST_FIRE] = {MAL_CUSTOM, 0, "x0.45 fire rate the rest|of the time"},
    [UPG_SCATTERBRAIN] = {MAL_SPREAD, 20},
    [UPG_REAR_GUARD] = {MAL_DAMAGE, 10},
    [UPG_STRAFE] = {MAL_CUSTOM, 0, "-15% fire rate while moving|towards or away from your aim"},
    [UPG_ESCALATION] = {MAL_SPINUP, 4},
    [UPG_OPENING_SALVO] = {MAL_COLD_START, 10},
    [UPG_ACID] = {MAL_SHOT_SPEED, 15},
    [UPG_STATIC] = {MAL_RECHARGE, 20},
    [UPG_CONCUSSION] = {MAL_RATE, 10},
    [UPG_HUNTERS_MARK] = {MAL_DAMAGE, 10},
    [UPG_CHROMA] = {MAL_DAMAGE, 12},
    [UPG_VAMPIRE] = {MAL_INVULN, 30},
    [UPG_BOWLING] = {MAL_RECOIL, 1},
    [UPG_COMBO] = {MAL_SPREAD, 25},
    [UPG_FIELD_NOTES] = {MAL_RATE, 10},
    [UPG_KILLSTREAK] = {MAL_NO_MERCY, 1},
    [UPG_WISPS] = {MAL_SHOT_SPEED, 15},
    [UPG_FRENZY] = {MAL_OVERHEAT, 6},
    [UPG_QUICK_HANDS] = {MAL_ACTIVE_TIME, 20},
    [UPG_SUPERNOVA] = {MAL_HITBOX, 15},
    [UPG_SLOW_MOTION] = {MAL_SPEED, 10},
    [UPG_NANITES] = {MAL_DAMAGE, 10},
    [UPG_REPULSOR] = {MAL_SPEED, 12},
    [UPG_SHOCKWAVE] = {MAL_RANGE, 15},
    [UPG_LAST_RESORT] = {MAL_INVULN, 30},
    [UPG_SHIELD_BATTERY] = {MAL_PRICES, 15},
    [UPG_FIREPROOF] = {MAL_HITBOX, 15},
    [UPG_PHASE_CLOAK] = {MAL_FRAGILE, 1},
    [UPG_SPARE_PARTS] = {MAL_SPEED, 10},
    [UPG_AFTERIMAGE] = {MAL_DRIFT, 30},
    [UPG_SENTRY] = {MAL_RATE, 12},
    [UPG_SMITE] = {MAL_RECHARGE, 20},
    [UPG_DEMOLISHER] = {MAL_RANGE, 15},
    [UPG_INTEREST] = {MAL_PRICES, 10},
    [UPG_LOCKSMITH] = {MAL_LUCK, 1},
    [UPG_CLUSTER_BOMBS] = {MAL_HIT_COINS, 2},
    [UPG_CRYO_BOMB] = {MAL_RANGE, 15},
    [UPG_FULL_POWER] = {MAL_FIRST_HIT, 1},
    [UPG_DESPERADO] = {MAL_SPEED, 10},
    [UPG_BUCKSHOT] = {MAL_DAMAGE, 25},
    [UPG_SLUGGER] = {MAL_RATE, 25},
    [UPG_NIMBLE] = {MAL_DAMAGE, 10},
    [UPG_BOUNTY] = {MAL_REVENGE, 15},
    [UPG_TIME_BOMB] = {MAL_RATE, 10},
    [UPG_ARMOR_PIERCER] = {MAL_DAMAGE, 15},
    [UPG_THUNDERHEAD] = {MAL_ENEMY_BULLETS, 10},
    [UPG_COLD_SNAP] = {MAL_MOVING_RATE, 15},
    [UPG_SPLASH] = {MAL_RANGE, 15},

    /* Weapon-like upgrades: the trade-off is part of how they reshape the gun (gun_profile). */
    [UPG_W_SCATTER] = {MAL_CUSTOM, 0, "Short range, much slower fire"},
    [UPG_W_NEEDLER] = {MAL_CUSTOM, 0, "Half damage, shorter range"},
    [UPG_W_SEEKER] = {MAL_CUSTOM, 0, "Slow missiles, much slower fire"},
    [UPG_W_BOUNCER] = {MAL_CUSTOM, 0, "Slow orbs, slower fire"},
    [UPG_W_RAILGUN] = {MAL_CUSTOM, 0, "Very slow fire"},
    [UPG_W_FLAMER] = {MAL_CUSTOM, 0, "Very short range, weak hits"},
    [UPG_W_ROCKETS] = {MAL_CUSTOM, 0, "Slow to launch, very slow fire"},
    [UPG_W_CHAKRAM] = {MAL_CUSTOM, 0, "Short range, very slow fire"},
    [UPG_W_CLUSTER] = {MAL_CUSTOM, 0, "Short range, very slow fire"},
    [UPG_W_ARC] = {MAL_CUSTOM, 0, "Very short range, slower fire"},
    [UPG_W_VULCAN] = {MAL_CUSTOM, 0, "Slow to spin up, -20% damage"},
    [UPG_W_WAVE] = {MAL_CUSTOM, 0, "Half fire rate, shorter range"},
    [UPG_W_LASER] = {MAL_CUSTOM, 0, "-40% move speed while firing"},
    [UPG_W_SPIRAL] = {MAL_CUSTOM, 0, "You no longer aim your shots"},
    [UPG_W_PRISM] = {MAL_CUSTOM, 0, "-35% damage, slower fire"},
    [UPG_W_GRAVITON] = {MAL_CUSTOM, 0, "Very slow orbs, very slow fire"},
    [UPG_W_SWORD] = {MAL_CUSTOM, 0, "Melee range only"},

    [UPG_X_BLOOD_ROUNDS] = {MAL_HEAL, 1},
    [UPG_X_OVERDRIVE] = {MAL_OVERHEAT, 8},
    [UPG_X_HELLFIRE] = {MAL_HITBOX, 20},
    [UPG_X_STORM] = {MAL_RECHARGE, 25},
    [UPG_X_DEMON] = {MAL_INVULN, 40},
    [UPG_X_GOD_CANNON] = {MAL_RECOIL, 2},
    [UPG_X_ARSENAL] = {MAL_SPREAD, 25},
    [UPG_X_PHOENIX] = {MAL_FRAGILE, 1},
    [UPG_X_GHOST_HULL] = {MAL_HITBOX, 25},
    [UPG_X_SUPERCHARGER] = {MAL_ACTIVE_TIME, 25},
    [UPG_X_VOID_ROUNDS] = {MAL_SHOT_SPEED, 20},
    [UPG_X_OVERSHIELD] = {MAL_SPEED, 12},
    [UPG_X_HIVE_QUEEN] = {MAL_RATE, 15},
    [UPG_X_CHRONOSPHERE] = {MAL_COLD_START, 15},
    [UPG_X_EXECUTIONER] = {MAL_NO_MERCY, 1},
    [UPG_X_ORBITAL_ARRAY] = {MAL_DRIFT, 40},
    [UPG_X_PRISM_HEART] = {MAL_DAMAGE, 15},
    [UPG_X_TEMPEST] = {MAL_ENEMY_BULLETS, 15},
    [UPG_X_RAILSTORM] = {MAL_RECOIL, 2},
    [UPG_X_ANNIHILATOR] = {MAL_REVENGE, 20},
    [UPG_X_SINGULARITY] = {MAL_BIG_BULLETS, 25},
    [UPG_X_DOOMSDAY] = {MAL_CROWD, 20},
};

int malus(MalusKind k) {
    const Player *p = &G.pl;
    int sum = 0;
    for (int u = 0; u < UPG_COUNT; u++)
        if (MALUSES[u].kind == k && p->upg_count[u] > 0 && !p->upg_off[u] && !p->malus_off[u]) sum += MALUSES[u].amount;
    return sum;
}

float malus_pct(MalusKind k) { return (float)malus(k) * 0.01f; }

void malus_stat_effect(Stats *s, int u) {
    if (G.pl.malus_off[u]) return; /* bought off (purifier) or lost in a fusion */
    float a = (float)MALUSES[u].amount * 0.01f;
    switch (MALUSES[u].kind) {
    case MAL_DAMAGE: s->dmg_mult *= 1.0f - a; break;
    case MAL_RATE: s->rate_mult *= 1.0f - a; break;
    case MAL_RANGE: s->range_mult *= 1.0f - a; break;
    case MAL_SPEED: s->speed *= 1.0f - a; break;
    case MAL_SHOT_SPEED: s->shot_speed_mult *= 1.0f - a; break;
    case MAL_HITBOX: s->hit_radius *= 1.0f + a; break;
    case MAL_SPREAD: s->spread_mult *= 1.0f + a; break;
    case MAL_RECHARGE: s->dash_cooldown *= 1.0f + a; break;
    case MAL_LUCK: s->luck -= MALUSES[u].amount; break;
    case MAL_MAX_HP: s->max_hp -= 2 * MALUSES[u].amount; break;
    default: break; /* read through malus() by the code it affects */
    }
}

bool malus_is_stat(MalusKind k) { return k >= MAL_DAMAGE && k <= MAL_MAX_HP; }

const char *upgrade_malus_desc(int u) {
    static char bufs[4][96];
    static int next;
    char *b = bufs[next++ & 3];
    int a = MALUSES[u].amount;
    switch (MALUSES[u].kind) {
    case MAL_DAMAGE: SDL_snprintf(b, 96, "-%d%% damage", a); break;
    case MAL_RATE: SDL_snprintf(b, 96, "-%d%% fire rate", a); break;
    case MAL_RANGE: SDL_snprintf(b, 96, "-%d%% range", a); break;
    case MAL_SPEED: SDL_snprintf(b, 96, "-%d%% move speed", a); break;
    case MAL_SHOT_SPEED: SDL_snprintf(b, 96, "-%d%% shot speed", a); break;
    case MAL_HITBOX: SDL_snprintf(b, 96, "+%d%% hitbox size", a); break;
    case MAL_SPREAD: SDL_snprintf(b, 96, "+%d%% spread", a); break;
    case MAL_RECHARGE: SDL_snprintf(b, 96, "Active items recharge|%d%% slower", a); break;
    case MAL_LUCK: SDL_snprintf(b, 96, "-%d luck", a); break;
    case MAL_MAX_HP: SDL_snprintf(b, 96, "-%d heart container%s", a, a > 1 ? "s" : ""); break;
    case MAL_RECOIL: SDL_snprintf(b, 96, "%s: every volley|pushes you back", a >= 2 ? "Heavy recoil" : "Recoil"); break;
    case MAL_FIRE_SLOW: SDL_snprintf(b, 96, "-%d%% move speed while firing", a); break;
    case MAL_ENEMY_BULLETS: SDL_snprintf(b, 96, "Enemy bullets %d%% faster", a); break;
    case MAL_INVULN: SDL_snprintf(b, 96, "%d%% shorter invulnerability|after a hit", a); break;
    case MAL_PRICES: SDL_snprintf(b, 96, "Shop prices +%d%%", a); break;
    case MAL_TAX: SDL_snprintf(b, 96, "-%d coin%s per room cleared", a, a > 1 ? "s" : ""); break;
    case MAL_OVERHEAT: SDL_snprintf(b, 96, "After %.0f s of non-stop fire,|the gun jams for %.1f s", (double)OVERHEAT_AFTER, (double)a * 0.1); break;
    case MAL_SPINUP: SDL_snprintf(b, 96, "Shots start %.1f s after|you pull the trigger", (double)a * 0.1); break;
    case MAL_FIRST_HIT: SDL_snprintf(b, 96, "The first hit in each room|costs %d more half-heart%s", a, a > 1 ? "s" : ""); break;
    case MAL_CROWD: SDL_snprintf(b, 96, "Enemy waves %d%% bigger", a); break;
    case MAL_DRIFT: SDL_snprintf(b, 96, "Heavier ship: %d%% slower|to start and stop", a); break;
    case MAL_COLD_START: SDL_snprintf(b, 96, "Cannot shoot for %.1f s|after entering a room", (double)a * 0.1); break;
    case MAL_HIT_COINS: SDL_snprintf(b, 96, "Getting hit costs %d coin%s", a, a > 1 ? "s" : ""); break;
    case MAL_ACTIVE_TIME: SDL_snprintf(b, 96, "Active item effects %d%% shorter", a); break;
    case MAL_HEAL: SDL_snprintf(b, 96, "Heart rewards: %d heart less", a); break;
    case MAL_FRAGILE: SDL_snprintf(b, 96, "Every hit costs %d more half-heart%s", a, a > 1 ? "s" : ""); break;
    case MAL_REVENGE: SDL_snprintf(b, 96, "Enemies killed: %d%% chance to|fire a last bullet at you", a); break;
    case MAL_BIG_BULLETS: SDL_snprintf(b, 96, "Enemy bullets %d%% bigger", a); break;
    case MAL_NO_MERCY: SDL_snprintf(b, 96, "Getting hit no longer clears|the bullets around you"); break;
    case MAL_MOVING_RATE: SDL_snprintf(b, 96, "-%d%% fire rate while moving", a); break;
    case MAL_CUSTOM: SDL_snprintf(b, 96, "%s", MALUSES[u].text ? MALUSES[u].text : "?"); break;
    default: b[0] = 0; break;
    }
    return b;
}

/* Bonus (white) then malus (red) under it; returns the height used. */
float draw_upgrade_texts(float cx, float y, float scale, float alpha, int u) {
    bool gone = G.pl.upg_count[u] > 0 && G.pl.malus_off[u]; /* owned, its malus bought off or fused away */
    const char *desc = upgrade_desc(u), *mal = gone ? "Malus removed" : upgrade_malus_desc(u);
    float line = 11.0f * scale;
    draw_desc(cx, y, scale, col_a(COL_WHITE, 0.8f * alpha), desc);
    float my = y + (SDL_strchr(desc, '|') ? 2.0f : 1.0f) * line + 3.0f * scale;
    draw_desc(cx, my, scale, gone ? col_hex(0x7dff9a, 0.95f * alpha) : col_hex(0xff6a7e, 0.95f * alpha), mal);
    return my - y + (SDL_strchr(mal, '|') ? 2.0f : 1.0f) * line;
}

/* One line for banners: "bonus - malus", without the '|' line breaks. */
void upgrade_banner_text(int u, char *out, int size) {
    SDL_snprintf(out, (size_t)size, "%s  /  %s", upgrade_desc(u), upgrade_malus_desc(u));
    for (char *c = out; *c; c++)
        if (*c == '|') *c = ' ';
}

/* ---- Behaviour maluses ---------------------------------------------------------- */

/* Firing gate: spin-up after the trigger is pulled, jam after OVERHEAT_AFTER s of non-stop
 * fire, no shooting right after entering a room. Returns whether the gun may fire now. */
bool malus_fire_gate(bool trigger) {
    Player *p = &G.pl;
    if (p->jam_t > 0.0f) p->jam_t -= DT;
    if (p->cold_t > 0.0f) p->cold_t -= DT;
    p->trig_t = trigger ? p->trig_t + DT : 0.0f;
    float jam = (float)malus(MAL_OVERHEAT) * 0.1f;
    if (jam > 0.0f && trigger && p->jam_t <= 0.0f && p->cold_t <= 0.0f) {
        p->heat += DT;
        if (p->heat >= OVERHEAT_AFTER) {
            p->heat = 0.0f;
            p->jam_t = jam;
            sfx_play_ex(SFX_DENIED, 0.8f, 0.7f, 0.0f);
            fx_popup(v2_add(p->pos, v2(0, -40)), col_hex(0xff6a7e, 1), "JAMMED");
        }
    } else if (!trigger) {
        p->heat = fmaxf(0.0f, p->heat - 2.0f * DT);
    }
    return trigger && p->jam_t <= 0.0f && p->cold_t <= 0.0f && p->trig_t >= (float)malus(MAL_SPINUP) * 0.1f;
}

void malus_on_room_enter(void) {
    Player *p = &G.pl;
    p->room_hit = false;
    p->heat = 0.0f;
    p->jam_t = 0.0f;
    p->cold_t = G.room.cleared ? 0.0f : (float)malus(MAL_COLD_START) * 0.1f;
}

void malus_on_room_clear(void) {
    Player *p = &G.pl;
    int tax = malus(MAL_TAX);
    if (tax > 0 && p->coins > 0) {
        int lost = tax < p->coins ? tax : p->coins;
        p->coins -= lost;
        fx_popup(v2_add(p->pos, v2(0, -60)), col_hex(0xff6a7e, 1), "-%d COIN%s", lost, lost > 1 ? "S" : "");
    }
}

/* A hit landed: the extra damage of the first hit in a room (returned), coins lost. */
int malus_on_hurt(void) {
    Player *p = &G.pl;
    int extra = p->room_hit ? 0 : malus(MAL_FIRST_HIT);
    p->room_hit = true;
    int c = malus(MAL_HIT_COINS);
    if (c > 0 && p->coins > 0) {
        int lost = c < p->coins ? c : p->coins;
        p->coins -= lost;
        fx_popup(v2_add(p->pos, v2(0, 50)), col_hex(0xffd23f, 1), "-%d COIN%s", lost, lost > 1 ? "S" : "");
    }
    return extra;
}
