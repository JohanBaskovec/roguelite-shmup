#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Passive upgrades. Simple stat upgrades are applied once in upgrade_apply();
 * everything else is read from G.pl.upg_count[] by the hooks below, which the
 * systems they modify call (weapons.c, bullets.c, enemies.c, player.c, room.c).
 */

const UpgradeDef UPGRADES[UPG_COUNT] = {
    [UPG_RAPID] = {"OVERCLOCK", "+18% fire rate", 0xff5f7e, 0},
    [UPG_DAMAGE] = {"HEAVY ROUNDS", "+22% damage", 0xff8a3d, 0},
    [UPG_MULTI] = {"SPLIT SHOT", "+1 projectile on every weapon", 0x66f0ff, 4},
    [UPG_PIERCE] = {"PIERCER", "Shots pierce one more enemy", 0xf28cff, 0},
    [UPG_BOUNCE] = {"RUBBER CORE", "Shots bounce off walls twice", 0xffe14d, 0},
    [UPG_HOMING] = {"SMART ROUNDS", "Shots steer towards enemies", 0x9dff6a, 3},
    [UPG_THRUSTER] = {"THRUSTERS", "+10% speed, faster dash", 0x7da8ff, 4},
    [UPG_HULL] = {"HULL PLATING", "+1 heart container", 0xff3355, 0},
    [UPG_DRONE] = {"WING DRONE", "An auto-firing drone joins you", 0xb6ffb0, 0},
    [UPG_MAGNET] = {"MAGNET", "Bigger pickup radius", 0xffd23f, 3},
    [UPG_VELOCITY] = {"ACCELERATOR", "+20% shot speed, +8% damage", 0x9ad7ff, 0},

    [UPG_FROST] = {"CRYO ROUNDS", "Hits chill enemies: -45% speed", 0x8fe9ff, 1},
    [UPG_VENOM] = {"VENOM TIPS", "Hits poison enemies over time", 0x7dff4a, 0},
    [UPG_THERMITE] = {"THERMITE", "Hits may set enemies on fire", 0xff7a2e, 0},
    [UPG_ARC] = {"ARC EMITTER", "Hits may chain lightning", 0x9db8ff, 0},
    [UPG_PAYLOAD] = {"PAYLOAD", "Shots may explode on impact", 0xffa64d, 0},
    [UPG_SHRAPNEL] = {"SHRAPNEL", "Hits may burst into fragments", 0xd0d0e0, 0},
    [UPG_BIG_IRON] = {"BIG IRON", "Bigger shots, +20% damage", 0xc9a0ff, 3},
    [UPG_CRIT] = {"LUCKY STRIKE", "Chance of triple-damage shots", 0xfff07a, 3},
    [UPG_GUILLOTINE] = {"GUILLOTINE", "Hits execute enemies below 15%", 0xff4466, 1},
    [UPG_BOOMERANG] = {"RETURN VECTOR", "Shots fly back to you", 0x66ffd9, 1},
    [UPG_PHASE] = {"PHASE ROUNDS", "Shots pass through obstacles", 0xb08cff, 1},
    [UPG_IMPACT] = {"IMPACT DRIVER", "Huge knockback, +10% damage", 0xffb070, 2},
    [UPG_TAIL_GUN] = {"TAIL GUN", "Also fires backwards", 0x7dd3ff, 3},
    [UPG_FLANK] = {"FLANK CANNONS", "Periodic shots to both sides", 0x7dffc8, 2},
    [UPG_CAPACITOR] = {"CAPACITOR", "Every 10th volley: a mega shot", 0x66e0ff, 2},
    [UPG_WIDE_LENS] = {"WIDE LENS", "+1 projectile, wider spread", 0x99ccff, 3},
    [UPG_FOCUS_LENS] = {"FOCUS LENS", "Tighter spread, +10% damage", 0xa0ffea, 2},

    [UPG_GLASS_CANNON] = {"GLASS CANNON", "+60% damage, -1 heart container", 0xe0f4ff, 1},
    [UPG_TITAN_HULL] = {"TITAN HULL", "+2 heart containers, -8% speed", 0xff5577, 2},
    [UPG_FEATHER] = {"FEATHERWEIGHT", "+15% speed, smaller hitbox", 0xe6f0ff, 2},
    [UPG_LONG_BARREL] = {"LONG BARREL", "+40% range, faster shots", 0xb4c8ff, 3},
    [UPG_HAIR_TRIGGER] = {"HAIR TRIGGER", "+30% fire rate, -10% damage", 0xff7a9a, 3},
    [UPG_FOUR_LEAF] = {"FOUR-LEAF CHIP", "+2 luck", 0x5dff8a, 2},
    [UPG_MICRO_HULL] = {"MICRO HULL", "-25% hitbox size", 0xd8d8ff, 2},

    [UPG_PIGGY_BANK] = {"PIGGY BANK", "+12 coins, +3 per room cleared", 0xffd23f, 1},
    [UPG_BOMB_BELT] = {"BOMB BELT", "+3 bombs, bombs hit 60% harder", 0xff8a3d, 2},
    [UPG_SKELETON_KEY] = {"SKELETON KEY", "+3 keys", 0xd9e3f0, 1},
    [UPG_COUPON] = {"COUPON", "Shop prices -30%", 0xff9de0, 1},

    [UPG_REACTIVE] = {"REACTIVE ARMOR", "Getting hit fires a bullet ring", 0xff6a4d, 2},
    [UPG_SECOND_WIND] = {"SECOND WIND", "Survive one lethal hit", 0xfff5c0, 3},
    [UPG_BARRIER] = {"BARRIER CELL", "+1 shield, recharges each room", 0x66d9ff, 3},
    [UPG_ORB] = {"GUARDIAN ORB", "Orbiting orb blocks bullets", 0xffe3a0, 3},
    [UPG_TESLA] = {"TESLA FIELD", "Zaps nearby enemies", 0x80c8ff, 3},
    [UPG_LEECH] = {"LEECH CORE", "Kills slowly restore health", 0xff3a6e, 2},
    [UPG_ADRENALINE] = {"ADRENALINE", "Faster when at 1 heart or less", 0xff3355, 1},
    [UPG_BLOODLUST] = {"BLOODLUST", "Kills boost damage this room", 0xc8102e, 2},
    [UPG_RAM] = {"RAM PLATING", "Dashing through enemies hurts", 0xffc26b, 2},
    [UPG_DASH_NOVA] = {"DASH NOVA", "Dashing fires a ring of shots", 0x7dfcff, 2},
    [UPG_CHRONO] = {"CHRONO FIELD", "Enemy bullets 15% slower", 0xa8a0ff, 2},
    [UPG_VOLATILE] = {"VOLATILE TARGETS", "Enemies explode when they die", 0xff5a1f, 2},
    [UPG_MINES] = {"MINE LAYER", "Drops mines as you move", 0xffaa33, 2},

    /* '|' splits a description over two lines on pedestals (a space in banners). */
    [UPG_DEFLECTOR] = {"DEFLECTOR SHELLS", "Huge slow shots, 1/3 fire rate|they reflect enemy bullets", 0x7dffef, 1},
    [UPG_WOUNDED_FURY] = {"WOUNDED FURY", "+30% damage while not|at full health", 0xff4a4a, 2},
    [UPG_SCAR_TISSUE] = {"SCAR TISSUE", "+25% fire rate while not|at full health", 0xff8a8a, 2},
    [UPG_LAST_STAND] = {"LAST STAND", "Per missing heart: +10% damage|and +5% speed", 0xd02040, 1},
    [UPG_BLOOD_SCENT] = {"BLOOD SCENT", "Shots home in on enemies|while not at full health", 0xff6060, 1},
    [UPG_LOADED_DICE] = {"LOADED DICE", "Doubles the chance of every|upgrade effect", 0xe6ff66, 1},
    [UPG_SIGNATURE] = {"SIGNATURE WEAPON", "Weapon rooms always offer|your highest-level weapon", 0x66f0ff, 1},

    /* Workshop exclusives: tier = heart containers paid at the augment machine. */
    [UPG_X_BLOOD_ROUNDS] = {"BLOOD ROUNDS", "+30% damage; every hit makes|enemies bleed (40%/s, 3 s)", 0xff2050, 1, 1},
    [UPG_X_OVERDRIVE] = {"OVERDRIVE CORE", "+30% fire rate, +15% speed,|dash recharges 30% faster", 0xff9f1c, 1, 1},
    [UPG_X_HELLFIRE] = {"HELLFIRE ENGINE", "+40% damage; every hit sets|enemies ablaze (100%/s, 2 s)", 0xff5a00, 1, 2},
    [UPG_X_STORM] = {"STORM CORE", "+30% fire rate; hits may chain|lightning through 3 enemies", 0x9db8ff, 1, 2},
    [UPG_X_DEMON] = {"DEMON HEART", "Damage x1.8, +2 projectiles,|every kill explodes", 0xb0103a, 1, 3},
    [UPG_X_GOD_CANNON] = {"GOD CANNON", "+50% dmg, +25% rate, +1 shot,|+2 pierce, shots home", 0xfff5c0, 1, 3},
};

static int has(UpgradeId u) { return G.pl.upg_count[u]; }

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
    case UPG_CAPACITOR: SDL_snprintf(b, 96, "Every %dth volley: a mega shot", n >= 2 ? 6 : 10); break;
    default: return UPGRADES[u].desc;
    }
    return b;
}

const char *upgrade_desc(int u) {
    int n = G.pl.upg_count[u] + 1;
    if (UPGRADES[u].max > 0 && n > UPGRADES[u].max) n = UPGRADES[u].max;
    return upgrade_desc_at(u, n);
}

/* ---- Rolling & applying -------------------------------------------------- */

bool upgrade_capped(int u) {
    const Player *p = &G.pl;
    if (UPGRADES[u].max > 0 && p->upg_count[u] >= UPGRADES[u].max) return true;
    switch (u) {
    case UPG_DRONE: return p->drones >= MAX_DRONES;
    case UPG_HULL: return p->max_hp >= 24;
    case UPG_TITAN_HULL: return p->max_hp > 20;
    case UPG_MULTI:
    case UPG_WIDE_LENS: return p->extra_proj >= 4;
    case UPG_GLASS_CANNON: return p->max_hp < 4;
    case UPG_FOUR_LEAF: return p->luck >= 4;
    case UPG_MICRO_HULL: return p->hit_radius * 0.75f < 2.0f;
    default: return false;
    }
}

int upgrade_roll(void) {
    for (int tries = 0; tries < 64; tries++) {
        int u = irand(0, UPG_COUNT - 1);
        if (UPGRADES[u].tier == 0 && !upgrade_capped(u)) return u; /* exclusives: workshop only */
    }
    return UPG_DAMAGE;
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

void upgrade_apply(int u) {
    Player *p = &G.pl;
    switch (u) {
    case UPG_RAPID: p->rate_mult *= 1.18f; break;
    case UPG_DAMAGE: p->dmg_mult += 0.22f; break;
    case UPG_MULTI: p->extra_proj++; break;
    case UPG_PIERCE: p->pierce++; break;
    case UPG_BOUNCE: p->bounce += 2; break;
    case UPG_HOMING: p->homing += 3.0f; break;
    case UPG_THRUSTER: p->speed *= 1.1f; p->dash_cooldown = fmaxf(0.15f, p->dash_cooldown * 0.85f); break;
    case UPG_HULL: p->max_hp += 2; p->hp += 2; break;
    case UPG_DRONE: p->drones++; break;
    case UPG_MAGNET: p->magnet += 120.0f; break;
    case UPG_VELOCITY: p->shot_speed_mult *= 1.2f; p->dmg_mult += 0.08f; break;
    case UPG_BIG_IRON:
        p->bullet_size *= 1.35f;
        p->dmg_mult *= 1.2f;
        p->shot_speed_mult = fmaxf(0.4f, p->shot_speed_mult * 0.9f);
        break;
    case UPG_IMPACT: p->dmg_mult *= 1.1f; break;
    case UPG_WIDE_LENS: p->extra_proj++; p->spread_mult = fminf(SPREAD_MAX, p->spread_mult * 1.4f); break;
    case UPG_FOCUS_LENS: p->spread_mult = fmaxf(SPREAD_MIN, p->spread_mult * 0.5f); p->dmg_mult *= 1.1f; break;
    case UPG_GLASS_CANNON:
        p->dmg_mult *= 1.6f;
        p->max_hp -= 2;
        if (p->hp > p->max_hp) p->hp = p->max_hp;
        break;
    case UPG_TITAN_HULL:
        p->max_hp = p->max_hp + 4 > 24 ? 24 : p->max_hp + 4;
        p->hp = p->hp + 4 > p->max_hp ? p->max_hp : p->hp + 4;
        p->speed = fmaxf(330.0f, p->speed * 0.92f);
        break;
    case UPG_FEATHER:
        p->speed = fminf(900.0f, p->speed * 1.15f);
        p->hit_radius = fmaxf(2.0f, p->hit_radius * 0.85f);
        break;
    case UPG_LONG_BARREL:
        p->range_mult = fminf(3.0f, p->range_mult * 1.4f);
        p->shot_speed_mult *= 1.2f;
        break;
    case UPG_HAIR_TRIGGER: p->rate_mult *= 1.3f; p->dmg_mult = fmaxf(0.3f, p->dmg_mult * 0.9f); break;
    case UPG_FOUR_LEAF: p->luck = p->luck + 2 > 5 ? 5 : p->luck + 2; break;
    case UPG_MICRO_HULL: p->hit_radius = fmaxf(2.0f, p->hit_radius * 0.75f); break;
    case UPG_PIGGY_BANK: p->coins += 12; break;
    case UPG_BOMB_BELT: p->bombs += 3; break;
    case UPG_SKELETON_KEY: p->keys += 3; break;
    case UPG_SECOND_WIND: p->revives++; break;
    case UPG_BARRIER: player_add_shield(2); break;
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
    default: break; /* effect read from upg_count by the hooks below */
    }
    p->upg_count[u]++;
    char desc[96];
    SDL_snprintf(desc, sizeof desc, "%s", upgrade_desc_at(u, p->upg_count[u]));
    for (char *c = desc; *c; c++)
        if (*c == '|') *c = ' ';
    fx_banner(UPGRADES[u].name, desc, col_hex(UPGRADES[u].color, 1), 2.6f);
}

/* ---- Firing --------------------------------------------------------------- */

float upg_volley_damage_mult(void) {
    float m = 1.0f;
    int n = has(UPG_BLOODLUST);
    if (n) m *= 1.0f + fminf(0.02f * (float)(G.pl.room_kills * n), 0.4f * (float)n);
    if (hurt()) m *= 1.0f + 0.3f * (float)has(UPG_WOUNDED_FURY);
    if (has(UPG_LAST_STAND)) m *= 1.0f + 0.1f * missing_hearts();
    return m;
}

static bool adrenaline_on(void) { return has(UPG_ADRENALINE) && G.pl.hp <= 2; }
float upg_fire_rate_mult(void) {
    float m = adrenaline_on() ? 1.4f : 1.0f;
    if (hurt()) m *= 1.0f + 0.25f * (float)has(UPG_SCAR_TISSUE);
    if (has(UPG_DEFLECTOR)) m /= 3.0f;
    return m;
}
float upg_move_speed_mult(void) {
    float m = adrenaline_on() ? 1.15f : 1.0f;
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
    if (!has(UPG_BOOMERANG) || (b->flags & (PB_MINE | PB_FRAGMENT))) return;
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
    float dmg = b->damage;
    /* GUILLOTINE: a hit that leaves an enemy under 15% finishes it. */
    if (has(UPG_GUILLOTINE) && e->hp - dmg < e->max_hp * 0.15f) {
        fx_particle(e->pos, v2(0, 0), 0.3f, e->radius * 3.0f, col_hex(0xff4466, 1), 0.0f, PT_RING);
        return e->hp + 1.0f;
    }
    return dmg;
}

static void area_damage(V2 pos, float radius, float dmg, const Enemy *skip, uint32_t color) {
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

void upg_after_hit(PBullet *b, Enemy *e, float dmg) {
    const Player *p = &G.pl;
    int n;
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
        area_damage(b->pos, 110.0f, dmg * 0.8f, e, 0xffaa33);
        sfx_play_at(SFX_EXPLODE_SMALL, b->pos);
    }
    if (b->flags & PB_FRAGMENT) return; /* no chain reactions from fragments */

    if (has(UPG_X_STORM) && chance(proc(STORM_CHANCE))) {
        /* Jumps to the nearest enemy not hit yet, three times. */
        Enemy *from = e, *hit[3] = {e, NULL, NULL};
        for (int j = 0; j < 3; j++) {
            Enemy *best = NULL;
            float best_d2 = 360.0f * 360.0f;
            for (int i = 0; i < G.nen; i++) {
                Enemy *o = &G.en[i];
                float d2 = v2_dist2(o->pos, from->pos);
                if (o->hp > 0.0f && o != hit[0] && o != hit[1] && o != hit[2] && d2 < best_d2) {
                    best_d2 = d2;
                    best = o;
                }
            }
            if (!best) break;
            enemy_damage(best, dmg * 0.6f, v2(0, 0));
            lightning(from->pos, best->pos);
            if (j < 2) hit[j + 1] = best;
            from = best;
        }
    }
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
        area_damage(b->pos, 80.0f, dmg * 0.5f, e, 0xffa64d);
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
    if ((n = has(UPG_VOLATILE)) > 0) area_damage(e->pos, 90.0f + e->radius, 18.0f * (float)n * aura_scale(), e, 0xff5a1f);
    if (has(UPG_X_DEMON)) area_damage(e->pos, 120.0f + e->radius, 30.0f * aura_scale(), e, 0xb0103a);
}

void upg_reflect(PBullet *b) {
    if (!has(UPG_DEFLECTOR) || (b->flags & (PB_REFLECTED | PB_MINE | PB_FRAGMENT))) return;
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
        nb->weapon = -1;
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
    if (p->revives <= 0) return false;
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

void upg_on_dash(void) {
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
            e->hp -= dps * DT;
            G.last_enemy_damage_t = G.time;
            if ((G.ticks + e->uid * 7) % 18 == 0) lightning(p->pos, e->pos);
        }
    }
    /* RAM PLATING: the dash itself hurts. */
    if ((n = has(UPG_RAM)) > 0 && p->dash_t > 0.0f) {
        for (int i = 0; i < G.nen; i++) {
            Enemy *e = &G.en[i];
            float r = e->radius + PLAYER_BODY_R + 12.0f;
            if (e->hp > 0.0f && v2_dist2(e->pos, p->pos) < r * r)
                enemy_damage(e, 260.0f * (float)n * aura_scale() * DT, v2_scale(p->dash_dir, 60.0f));
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

void upg_on_room_enter(void) {
    Player *p = &G.pl;
    p->room_kills = 0;
    int n = has(UPG_BARRIER);
    if (n && !G.room.cleared && p->shield < 2 * n) player_add_shield(2 * n - p->shield);
}

void upg_on_room_clear(void) {
    if (has(UPG_PIGGY_BANK)) {
        G.pl.coins += 3;
        fx_popup(v2_add(G.pl.pos, v2(0, -36)), col_hex(0xffd23f, 1), "+3 COINS");
    }
}

float upg_enemy_bullet_speed(void) { return has(UPG_CHRONO) ? powf(0.85f, (float)has(UPG_CHRONO)) : 1.0f; }

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
    draw_blend_alpha();
}
