#ifndef GAME_H
#define GAME_H

#include "common.h"
#include "rng.h"

enum {
    MAX_PBULLETS = 4096,
    MAX_EBULLETS = 6144,
    MAX_ENEMIES = 160,
    MAX_SPAWNS = 96,
    MAX_PARTICLES = 8192,
    MAX_PICKUPS = 256,
    MAX_OBSTACLES = 96,
    MAX_FIRES = 24,
    MAX_POPUPS = 48,
    MAX_DRONES = 6,
    MAX_DOORS = 3,
};

#define PLAYER_BODY_R 14.0f /* collision with walls / pickups */
#define PLAYER_HIT_R 4.0f   /* tiny shmup hitbox for bullets */
#define DOOR_HALF 60.0f
#define TRANS_TIME 0.34f
#define RESTART_HOLD_T 1.0f /* hold R that long in a run: a new run with the same ship */

/* ---- Weapons & upgrades -------------------------------------------------- */

/* There is a single gun. Weapon mods are upgrades (UPG_W_*) that change how it fires and
 * combine freely (weapons.c, gun_profile); the sword (Knight only) replaces the gun. */
typedef enum {
    UPG_RAPID, UPG_DAMAGE, UPG_MULTI, UPG_PIERCE, UPG_BOUNCE, UPG_HOMING,
    UPG_THRUSTER, UPG_HULL, UPG_DRONE, UPG_MAGNET, UPG_VELOCITY,
    /* bullet effects */
    UPG_FROST, UPG_VENOM, UPG_THERMITE, UPG_ARC, UPG_PAYLOAD, UPG_SHRAPNEL, UPG_BIG_IRON, UPG_CRIT,
    UPG_GUILLOTINE, UPG_BOOMERANG, UPG_PHASE, UPG_IMPACT, UPG_TAIL_GUN, UPG_FLANK, UPG_CAPACITOR,
    UPG_WIDE_LENS, UPG_FOCUS_LENS,
    /* stat changes */
    UPG_GLASS_CANNON, UPG_TITAN_HULL, UPG_FEATHER, UPG_LONG_BARREL, UPG_HAIR_TRIGGER, UPG_FOUR_LEAF,
    UPG_MICRO_HULL,
    /* economy */
    UPG_PIGGY_BANK, UPG_BOMB_BELT, UPG_SKELETON_KEY, UPG_COUPON,
    /* defence, auras, triggers */
    UPG_REACTIVE, UPG_SECOND_WIND, UPG_BARRIER, UPG_ORB, UPG_TESLA, UPG_LEECH, UPG_ADRENALINE,
    UPG_BLOODLUST, UPG_RAM, UPG_DASH_NOVA, UPG_CHRONO, UPG_VOLATILE, UPG_MINES,
    /* big slow shots that reflect enemy bullets; bonuses while hurt; luck */
    UPG_DEFLECTOR, UPG_WOUNDED_FURY, UPG_SCAR_TISSUE, UPG_LAST_STAND, UPG_BLOOD_SCENT, UPG_LOADED_DICE,
    /* conditional damage, kill triggers, timed strikes, stat trades, utility */
    UPG_SCOPE, UPG_POINT_BLANK, UPG_BULLY, UPG_GIANT_SLAYER, UPG_MOMENTUM, UPG_SMART_BOUNCE,
    UPG_CHAIN_REACTION, UPG_GROWTH, UPG_TURRET_MODE, UPG_RUN_GUN, UPG_FROSTBITE, UPG_PYROMANIAC,
    UPG_TOXIC_CLOUD, UPG_OVERKILL, UPG_MISSILE_POD, UPG_ORBITAL, UPG_SPIKED_HULL, UPG_ECHO,
    UPG_MAGNUM, UPG_SPRAY, UPG_HEAVY_CALIBER, UPG_EMERGENCY, UPG_RAMPAGE, UPG_BLACK_HOLE, UPG_XRAY,
    /* shot behaviours (hooks), elemental combos */
    UPG_RICOCHET, UPG_SNOWBALL, UPG_STICKY, UPG_FIREWORKS, UPG_PINBALL, UPG_SHATTER, UPG_CONDUCTOR,
    UPG_ALCHEMIST, UPG_CRIT_MASS, UPG_FIRST_BLOOD, UPG_HIVEMIND,
    /* ways of playing */
    UPG_DEAD_TRIGGER, UPG_CHARGE, UPG_GRAZE, UPG_BULLET_TIME, UPG_CLONE, UPG_BLOOD_MONEY, UPG_WILD_CARD,
    UPG_POINT_DEFENSE, UPG_TRAIL, UPG_AEGIS, UPG_AFTERBURNER, UPG_BERSERKER, UPG_BOMB_FACTORY, UPG_FLAWLESS,
    UPG_VENGEANCE,
    /* the 50 newer ones (upgrades2.c): volleys, hits, kills, defence, utility, conditions */
    UPG_TWIN_BARREL, UPG_HELIX, UPG_BURST_FIRE, UPG_SCATTERBRAIN, UPG_REAR_GUARD, UPG_STRAFE, UPG_ESCALATION,
    UPG_OPENING_SALVO, UPG_ACID, UPG_STATIC, UPG_CONCUSSION, UPG_HUNTERS_MARK, UPG_CHROMA, UPG_VAMPIRE, UPG_BOWLING,
    UPG_COMBO, UPG_FIELD_NOTES, UPG_KILLSTREAK, UPG_WISPS, UPG_FRENZY, UPG_QUICK_HANDS, UPG_SUPERNOVA, UPG_SLOW_MOTION,
    UPG_NANITES, UPG_REPULSOR, UPG_SHOCKWAVE, UPG_LAST_RESORT, UPG_SHIELD_BATTERY, UPG_FIREPROOF, UPG_PHASE_CLOAK,
    UPG_SPARE_PARTS, UPG_AFTERIMAGE, UPG_SENTRY, UPG_SMITE, UPG_DEMOLISHER, UPG_INTEREST, UPG_LOCKSMITH,
    UPG_CLUSTER_BOMBS, UPG_CRYO_BOMB, UPG_FULL_POWER, UPG_DESPERADO, UPG_BUCKSHOT, UPG_SLUGGER, UPG_NIMBLE, UPG_BOUNTY,
    UPG_TIME_BOMB, UPG_ARMOR_PIERCER, UPG_THUNDERHEAD, UPG_COLD_SNAP, UPG_SPLASH,
    /* weapon mods (offered by weapon rooms); UPG_W_SWORD replaces the gun with a sword */
    UPG_W_SCATTER, UPG_W_NEEDLER, UPG_W_SEEKER, UPG_W_BOUNCER, UPG_W_RAILGUN, UPG_W_FLAMER, UPG_W_ROCKETS,
    UPG_W_CHAKRAM, UPG_W_CLUSTER, UPG_W_ARC, UPG_W_VULCAN, UPG_W_WAVE,
    UPG_W_LASER, UPG_W_SPIRAL, UPG_W_PRISM, UPG_W_GRAVITON, UPG_W_SWORD,
    /* Baphomet exclusives (UpgradeDef.tier = max hearts paid), never offered anywhere else */
    UPG_X_BLOOD_ROUNDS, UPG_X_OVERDRIVE, UPG_X_HELLFIRE, UPG_X_STORM, UPG_X_DEMON, UPG_X_GOD_CANNON,
    /* the 16 newer exclusives (upgrades2.c): six of tier 1, six of tier 2, four of tier 3 */
    UPG_X_ARSENAL, UPG_X_PHOENIX, UPG_X_GHOST_HULL, UPG_X_SUPERCHARGER, UPG_X_VOID_ROUNDS, UPG_X_OVERSHIELD,
    UPG_X_HIVE_QUEEN, UPG_X_CHRONOSPHERE, UPG_X_EXECUTIONER, UPG_X_ORBITAL_ARRAY, UPG_X_PRISM_HEART, UPG_X_TEMPEST,
    UPG_X_RAILSTORM, UPG_X_ANNIHILATOR, UPG_X_SINGULARITY, UPG_X_DOOMSDAY,
    UPG_COUNT
} UpgradeId;

/* Active items: several can be owned at once; SPACE / SHIFT / right click triggers every one that
 * is turned on and ready, then each recharges on its own. None at the start: they come from active
 * rooms (RW_ACTIVE, choice of 3); taking one already owned levels it up. */
typedef enum {
    ACT_NONE, ACT_DASH, ACT_SHIELD, ACT_BOOM, ACT_STRENGTH, ACT_CHAOS,
    ACT_REVERSAL, ACT_TURRET, ACT_BLINK, ACT_NOVA, ACT_TIME_STOP, ACT_BLADES,
    ACT_COUNT
} ActiveId;

/* Numbers per level (duration, power, recharge) live in actives.c: active_duration / active_desc... */
typedef struct {
    const char *name;
    uint32_t color;
    float cooldown;       /* seconds at level 1, scaled by the RECHARGE stat (Player.dash_cooldown / DASH_BASE_CD) */
} ActiveDef;

extern const ActiveDef ACTIVES[ACT_COUNT];

#define DASH_BASE_CD 0.6f    /* Player.dash_cooldown at the start: the dash's own recharge */
#define MAX_ACTIVE_LEVEL 5   /* taking an active item owned again levels it up */

typedef struct {
    int level;        /* 0: not owned, else 1..MAX_ACTIVE_LEVEL */
    bool off;         /* turned off in the TAB menu: never triggered, no effect */
    float cd, cd_max; /* recharge left / total of the last use */
    float t;          /* effect time left (shield, strength, chaos...) */
} ActiveSlot;

typedef struct {
    const char *name;
    const char *desc;
    uint32_t color;
    int tier;       /* > 0: Baphomet exclusive, costs this many heart containers */
} UpgradeDef;
/* Every upgrade is taken at most once: one owned (even turned off) is never offered again. */

extern const UpgradeDef UPGRADES[UPG_COUNT];

static inline bool upgrade_is_weapon(int u) { return u >= UPG_W_SCATTER && u <= UPG_W_SWORD; }

/* ---- Bullets ------------------------------------------------------------- */

typedef struct {
    V2 pos, vel;
    float radius, damage, life, homing, knockback;
    int pierce, bounces;
    int last_hit;   /* uid of the last enemy hit, avoids multi-hits while piercing */
    int style;      /* BulletStyle: how it is drawn (BS_BOLT for drones, extra shots...) */
    int flags;      /* PB_* */
    float age;
    SDL_FColor col;
    int ricochets;  /* RICOCHET: jumps to another enemy already made */
    float grown;    /* SNOWBALL: size multiplier applied so far (0 = none yet) */
    bool armed;     /* went through pbullet_arm (or is the main gun's): carries every upgrade */
    bool hit_any;   /* hit an enemy at least once (COMBO: a shot that dies without a hit is a miss) */
} PBullet;

enum {
    PB_FRAGMENT = 1, PB_CRIT = 2, PB_RETURNED = 4, PB_MINE = 8, PB_CHARGED = 16, PB_REFLECTED = 32,
    PB_EXPLOSIVE = 64,  /* explodes when it dies (hit, wall, end of range): rockets, missiles */
    PB_RETURNS = 128,   /* flies back to the ship halfway (chakram); a wall turns it around too */
    PB_CLUSTER = 256,   /* bursts into fragments when it dies */
    PB_FLIP = 512,      /* wave gun: weaves the other way */
    PB_VORTEX = 1024,   /* BLACK HOLE: a still vortex that pulls and grinds enemies */
    PB_FLAME = 2048,    /* flamer: slows down, swells, sets enemies on fire */
    PB_ACCEL = 4096,    /* rockets, seeker: speeds up after launch */
    PB_WAVE = 8192,     /* wave gun: weaves around its line of fire */
    PB_ARC = 16384,     /* arc caster: hits chain lightning */
    PB_ORBIT = 1 << 15, /* orbit: circles around the ship in a widening spiral */
    PB_SPLIT = 1 << 16, /* prism: splits in three mid-flight (once) */
    PB_PULL = 1 << 17,  /* graviton: drags enemies along */
    PB_PRISMED = 1 << 18, /* PRISM HEART: already split on a hit (and its copies) */
};

/* Look of a player shot: the most striking weapon mod owned decides (gun_profile). */
typedef enum { BS_BOLT, BS_SEEKER, BS_BOUNCER, BS_RAIL, BS_FLAME, BS_ROCKET, BS_CHAKRAM, BS_CLUSTER, BS_ARC, BS_WAVE,
               BS_LASER, BS_GRAVITON, BS_CRESCENT } BulletStyle;

/* Enemy bullet behaviours (EBullet.kind), handled in update_enemy_bullets (bullets.c). */
typedef enum {
    EB_NORMAL,
    EB_GRENADE,   /* lobbed: flies over everything, harmless in the air, bursts into a ring (n) where it lands */
    EB_MINE,      /* still: arms, then blows into a ring (n) when its fuse ends or the ship comes close */
    EB_BOOMERANG, /* slows down along dir, comes back */
    EB_BOUNCE,    /* bounces off walls and obstacles (n times) */
    EB_SPLIT,     /* bursts into a ring of n bullets after t1 seconds */
    EB_HOMING,    /* turns towards the ship for t1 seconds */
    EB_WAVY,      /* weaves around its line */
    EB_BALL,      /* thrown by a SPARK POD block: hurts the ship AND the enemies it touches */
    EB_FROST,     /* FROSTBITE: chills the ship it hits (slower for a moment) */
    EB_WEB,       /* WEAVER: brakes to a stop (t1 = braking) and stays, chills the ship it hits */
    EB_SHATTER,   /* JUGGERNAUT: bursts into a ring of n where it hits a wall */
} EBulletKind;

typedef struct {
    V2 pos, vel;
    float radius, life, age;
    bool grazed;    /* GRAZE: already counted */
    float accel;    /* px/s^2 along velocity */
    float curve;    /* rad/s */
    SDL_FColor col;
    uint8_t kind;   /* EBulletKind */
    int n;          /* EB_GRENADE / EB_MINE / EB_SPLIT: bullets released; EB_BOUNCE: bounces left */
    float t1;       /* EB_GRENADE: flight time; EB_SPLIT: split time; EB_HOMING: homing time; EB_BOOMERANG: braking */
    V2 dir;         /* EB_BOOMERANG: initial direction */
} EBullet;

/* An enemy bullet that cannot hit (a grenade still in the air). */
static inline bool ebullet_airborne(const EBullet *b) { return b->kind == EB_GRENADE && b->life > 0.0f; }

/* ---- Enemies ------------------------------------------------------------- */

typedef enum {
    EN_CHASER, EN_SWARM, EN_SHOOTER, EN_BURSTER, EN_SPINNER, EN_SNIPER, EN_TANK, EN_WARDEN, EN_HUNTER, EN_HIVE,
    EN_RAMMER, EN_GRENADIER, EN_CRAWLER, EN_TOP, EN_KAMIKAZE, EN_DODGER, EN_ROOK, EN_SPLITTER, EN_LEAPER, EN_MINELAYER,
    EN_BOOMERANG, EN_RICOCHET, EN_ORBITER, EN_BLINKER, EN_SERPENT, EN_MIRROR, EN_LANCER, EN_MORTAR, EN_GUARDIAN, EN_WRAITH,
    /* Bosses (bosses.c), EN_BOSS .. EN_SINGULARITY: one per boss room. */
    EN_BOSS,        /* THE OVERSEER (room 10) */
    EN_LIGHTHOUSE,  /* room 10 alternative: beams swept from the centre of a room with pillars */
    EN_WYRM,        /* room 10 alternative: a burrowing serpent, its armoured body stops shots */
    EN_STORM,       /* room 10 alternative: arcs of lightning between four pylons */
    EN_FORGE,       /* room 20: hammer slams that leave fire, molten rain */
    EN_CARRIER,     /* room 30: rotating shield plates, drones, missiles */
    EN_SINGULARITY, /* room 40: a black hole that pulls the ship in */
    EN_SPLITLING, /* only born from a dying splitter (min_depth keeps it out of waves) */
    /* The 60 newer types (enemies2.c), from very easy to hard, then the 3 helpers they make. */
    EN_DRIFTER, EN_PEON, EN_BUZZER, EN_PULSAR, EN_SNAIL, EN_SKITTER, EN_POPPER,
    EN_SPRINKLER, EN_BEETLE, EN_CRAB, EN_WASP, EN_DICE, EN_PUFFER, EN_PRISM, EN_BAT, EN_CENTIPEDE, EN_BROODER,
    EN_MEDIC, EN_HEDGEHOG,
    EN_SAPPER, EN_ROCKETEER, EN_JESTER, EN_FROSTBITE, EN_GLITCH, EN_PEEKER, EN_ECHO, EN_CLOCK, EN_HALO,
    EN_STALKER, EN_OCTOPUS, EN_BUZZSAW, EN_SENTRY, EN_TORCH, EN_ZAPPER, EN_BEAMER, EN_ENGINEER, EN_JELLY, EN_SCORPION,
    EN_LINKER, EN_SWAPPER, EN_SPOTTER, EN_LODESTONE, EN_SPONGE, EN_PARRY, EN_BUBBLER, EN_DRUMMER, EN_TRICKSTER,
    EN_HYDRA, EN_MANTIS, EN_FLAIL, EN_MOLE, EN_CRYSTAL, EN_NECRO, EN_BERSERKER, EN_FUNGUS, EN_WEAVER,
    EN_JUGGERNAUT, EN_IDOL, EN_BLACKSTAR, EN_SENTINEL,
    EN_EGG,     /* laid by a brooder, hatches into a buzzer */
    EN_MISSILE, /* launched by a rocketeer */
    EN_TURRET,  /* built by an engineer */
    EN_COUNT
} EnemyType;

typedef struct {
    const char *name;
    float radius, hp, speed;
    int cost;       /* wave budget cost */
    int min_floor;  /* the floor it belongs to (1..FLOORS; 0: in no wave; 999: made by something else / a boss) */
    int tier;       /* 0: unlocked on a new save; 1 / 2: unlocked when the floor's boss is beaten once / twice (5 each) */
    uint32_t color;
    int sides;
} EnemyDef;

extern const EnemyDef ENEMIES[EN_COUNT];

typedef struct {
    EnemyType type;
    int uid;
    V2 pos, vel, target;
    float radius, hp, max_hp;
    float t, fire_t, state_t, angle, spin, flash;
    int state, burst, spin_dir;
    float slow_t;                 /* chilled: moves and shoots slower */
    float poison_t, poison_dps, burn_t, burn_dps;
    V2 nav_pt, nav_goal;        /* nav_dir(): waypoint, worked out every few ticks */
    bool nav_ok;
    float sticky_t, sticky_dmg;   /* STICKY CHARGES: time before the charge blows, its damage */
    float react_cd;               /* elemental reactions (synergies): time before the next one */
    int clone_swing;              /* BLADE DANCER: last swing of the clone's blade that hit it */
    int sword_swing;              /* id of the last sword swing that hit it (one hit per swing) */
    int phase;                    /* EN_BOSS: current phase, 0 .. BOSS_PHASES - 1 */
    float inv_t;                  /* EN_BOSS: invulnerable while > 0 (phase change) */
    V2 aux;                       /* per type: charge / slide direction, blink target, wall normal... */
    float alt;                    /* EN_LEAPER: height of the jump (> 0: in the air, untouchable) */
    float dodge_cd;               /* EN_DODGER: time before it can sidestep again */
    /* The newer types (enemies2.c): */
    int link;                     /* uid of the one it follows / is tied to (CENTIPEDE, LINKER) */
    float k0, k1;                 /* per type: generation, growth, charge, counters... */
    float haste_t;                /* DRUMMER / SENTINEL: moves and fires faster meanwhile */
    int bubble;                   /* BUBBLER: hits its bubble still stops (whole hits) */
    bool fake;                    /* TRICKSTER: an illusion (one hit, no shots of its own, no kill) */
    bool vanish;                  /* gone without being killed (an egg hatching, a missile blowing): no kill */
    /* Upgrades' marks on it (upgrades2.c): */
    float acid;                   /* ACID ROUNDS: extra damage it takes (0 .. 0.4) */
    int static_n;                 /* STATIC CHARGE: hits so far towards the next discharge */
    float stun_t;                 /* CONCUSSION, COLD SNAP, CRYO BOMB: frozen in place meanwhile (never a boss) */
} Enemy;

#define EN_NEW_FIRST EN_DRIFTER
static inline bool type_is_new(int t) { return t >= EN_NEW_FIRST && t < EN_COUNT; }

#define GUARDIAN_RANGE 260.0f
#define GUARDIAN_SHIELD 0.4f /* allies near a guardian take 40% damage */

/* --enemy NAME (repeatable): waves only use these types (g_only_count 0 = normal waves). */
extern bool g_only_enemies[EN_COUNT];
extern int g_only_count;

/* Boss rooms: every BOSS_EVERY rooms (depth 10, 20...); the room before has a single door. */
#define BOSS_EVERY 10
#define BOSS_PHASES 3
#define BOSS_FIRST EN_BOSS
#define BOSS_LAST EN_SINGULARITY
#define BOSS_COUNT (BOSS_LAST - BOSS_FIRST + 1)
#define WYRM_SEGS 14 /* the wyrm's body (it sheds 3 per phase) */
static inline bool type_is_boss(int t) { return t >= BOSS_FIRST && t <= BOSS_LAST; }
static inline bool enemy_is_boss(const Enemy *e) { return type_is_boss(e->type); }

typedef struct {
    EnemyType type;
    V2 pos;
    float t, max_t;
} Spawn;

/* ---- FX ------------------------------------------------------------------ */

enum { PT_GLOW, PT_SPARK, PT_RING, PT_SHARD };

typedef struct {
    V2 pos, vel;
    float life, max_life, size, drag;
    SDL_FColor col;
    uint8_t shape;
    float rot, spin;  /* PT_SHARD: a spinning shard of glass */
} Particle;

typedef struct {
    V2 pos;
    float t;
    char text[48];
    SDL_FColor col;
} Popup;

/* ---- Pickups ------------------------------------------------------------- */

/* PK_UPGRADE: any upgrade, weapon mods included (upgrade_is_weapon). */
/* PK_ACTIVE: an active item (sub = ActiveId), added to the ones owned (or levels one up). */
/* PK_AUGMENT: a Baphomet pact, a tier-N exclusive upgrade paid with N heart containers. */
typedef enum { PK_COIN, PK_KEY, PK_BOMB, PK_HEART, PK_UPGRADE, PK_STAT, PK_AUGMENT, PK_ACTIVE } PickupType;

typedef struct {
    PickupType type;
    int sub;        /* coin value, UpgradeId or encoded stat offer */
    int price;      /* shop price in coins, 0 = free */
    uint8_t choice; /* one-of-N pick (its group, 1..): taking it makes the other pickups of its group vanish */
    V2 pos, vel;
    float t;
} Pickup;

/* ---- Machines (workshop, casino) ---------------------------------------------------- */

typedef enum {
    MC_SUPPLY, MC_GAMBLE,       /* casino */
    MC_TRADE,                   /* trader: an upgrade for sale, paid with two of the ship's own */
    MC_PURIFY,                  /* purifier: an owned upgrade loses its malus, for coins */
    MC_SAFE,                    /* vault: a safe to break open before the countdown ends */
    MC_GATE,                    /* key room: a locked gate in front of an upgrade */
    MC_HEAL, MC_CONTAINER,      /* infirmary: a full heal, or a heart container for coins (one of the two) */
    MC_FORGE                    /* fusion forge: two owned upgrades become one (both bonuses, one malus) */
} MachineType;

typedef struct {
    MachineType type;
    V2 pos;
    int uses;          /* SUPPLY: payouts left before it breaks */
    bool broken;       /* SUPPLY: used up; GAMBLE: prize already won */
    bool inside;       /* the player stood on it last tick (a use needs a fresh step in) */
    PickupType prize;  /* GAMBLE: PK_UPGRADE (not owned), or PK_STAT / PK_COIN once every upgrade is owned */
    int prize_sub;
    float anim, spin;  /* feedback timers; GAMBLE: spin > 0 while the reels turn */
    int plays;
    int arg[2];        /* TRADE: the two upgrades paid; PURIFY: arg[0] the upgrade; FORGE: arg[0] keeps its malus, arg[1] is absorbed into it */
    int price;         /* PURIFY / CONTAINER: coins (follows COUPON / the PRICES malus) */
    uint8_t group;     /* one-of-N (infirmary, forge): using one of the group puts the others out of use (0: none) */
    float hp, timer;   /* SAFE: hit points left, seconds left (the faster it breaks, the more it pays) */
    int swing_id;      /* SAFE: the blade's last swing that hit it (once per swing) */
    float shatter_t;   /* SAFE: broken, cracking for CRACK_TIME before it bursts into glass (then `gone`) */
    bool gone;         /* SAFE: burst, nothing left to draw or hit */
} Machine;

#define MAX_MACHINES 4 /* a casino: the supply press + CASINO_MACHINES slot machines; the new rooms: at most 3 */
#define GAMBLE_COST 5
#define SUPPLY_USES_MIN 3
#define SUPPLY_USES_MAX 5

/* ---- Rooms --------------------------------------------------------------- */

/* WORKSHOP: a row of stat trades; CASINO: slot machines + supply press; BAPHOMET: pacts (exclusive upgrades paid with
 * heart containers). Every one of them starts with its fight. */
typedef enum {
    ROOM_START, ROOM_COMBAT, ROOM_SHOP, ROOM_BOSS, ROOM_SECRET, ROOM_WORKSHOP, ROOM_CASINO, ROOM_BAPHOMET,
    ROOM_TRADER, ROOM_PURIFIER, ROOM_VAULT, ROOM_FORGE, ROOM_KEYS, ROOM_INFIRMARY, ROOM_TYPE_COUNT
} RoomType;
/* RW_UPGRADE: a choice of UPGRADE_CHOICES (2) upgrades not owned (any of them, those reshaping the shots included). */
typedef enum { RW_NONE, RW_COINS, RW_KEY, RW_BOMBS, RW_HEART, RW_UPGRADE, RW_ACTIVE, RW_COUNT } RewardType;
typedef enum { SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM } Side;
/* Room modifiers (mods.c): one more rule for a room (any but a boss room; WAGER: combat rooms only), shown on its
 * door. A room holds a set of them (a bit mask, MOD_BIT): rolled doors carry one at most, the F4 menu any mix. */
typedef enum {
    MOD_NONE,
    MOD_COMPACTOR, /* the walls close in slowly from the first wave on (fast once the last wave's timer runs out) */
    MOD_BLACKOUT,  /* dark: only a circle of light round the ship, the shots and the enemies' glow */
    MOD_POWDER,    /* powder keg: packed with barrels and oil drums */
    MOD_HORDE,     /* a single huge wave (its timer is the last wave's: then the walls) */
    MOD_ELITE,     /* a single enemy type (Room.elite) */
    MOD_STORM,     /* lightning strikes the floor, on spots shown a moment before (ship and enemies alike) */
    MOD_WAGER,     /* the door costs a whole heart to go through; the room gives its reward twice */
    /* Difficulty levels (coin / bomb / key / heart combat rooms only): more enemies, more waves, a bigger reward. The highest set wins. */
    MOD_HARD,
    MOD_VERY_HARD,
    MOD_EXTREME,
    MOD_IMPOSSIBLE,
    MOD_COUNT
} RoomMod;
#define MOD_BIT(m) (1u << (m))
#define MOD_ALL ((1u << MOD_COUNT) - 2u) /* every modifier */

typedef struct {
    Side side;
    RoomType type;       /* room behind the door */
    RewardType reward;   /* reward granted when that room is cleared */
    uint16_t mods;       /* MOD_BIT set of RoomMods of the room behind it (none behind the boss door) */
    uint8_t count;       /* coins / keys / bombs / hearts the room behind gives (shown on the door; machine rooms: what they give if the ship cannot use them) */
    RewardType alt;      /* machine rooms: the resource (coins, keys, bombs or hearts) they give then (RW_NONE: other doors) */
    bool locked;
    V2 pos;
    float anim;
} Door;

/* Room blocks. Walls never break; the others are shot down by the ship's shots (never the
 * enemies'), its blasts and blade, and a bomb breaks every one of them at once. */
typedef enum {
    OB_WALL,   /* indestructible */
    OB_CRATE,  /* just breaks */
    OB_BARREL, /* explodes: hurts everything around (the ship too), breaks the blocks nearby */
    OB_OIL,    /* leaves a pool of burning oil that sets the ship and the enemies on fire */
    OB_SPARK,  /* throws a ring of balls that hurt the ship and the enemies */
    OB_NEST,   /* lets out one small enemy (enemy_hatchling) */
    OB_KIND_COUNT
} ObstacleKind;

typedef struct {
    SDL_FRect r;
    uint8_t kind;      /* ObstacleKind */
    float hp, max_hp;  /* blocks: <= 0 = broken (removed at the end of the tick) */
    float flash;       /* hit flash */
    int swing_id;      /* last sword swing that cut it */
    float shatter_t;   /* room cleared: > 0 = cracking, bursts into glass when it reaches 0 */
    float burn_t;      /* block on fire: seconds burning (0 = not), goes off at BLOCK_BURN_TIME */
} Obstacle;

static inline bool obstacle_breakable(const Obstacle *o) { return o->kind != OB_WALL; }
/* Catches fire when fire touches it (burning oil, flame shots, a burning enemy, a burning neighbour):
 * every breakable block, so fire runs along any row of them. */
static inline bool obstacle_flammable(const Obstacle *o) { return obstacle_breakable(o); }
static inline bool obstacle_solid(const Obstacle *o) { return o->kind == OB_WALL || o->hp > 0.0f; }

typedef struct {
    V2 pos;
    float r, t, life;  /* radius, time left, total time */
} FirePool;

#define FIRE_POOL_R 80.0f
#define FIRE_POOL_TIME 7.0f
#define BLOCK_BURN_TIME 1.6f /* a block on fire burns that long, then breaks / explodes / spills its oil */
#define BLOCK_SPREAD_T 0.6f  /* ...and sets the flammable blocks next to it on fire after this long */
#define BARREL_BLAST_R 135.0f
#define SPARK_BALLS 12
#define BLOCK_TILE GRID /* a block fills one grid cell */
/* Room cleared: a wave of cracks runs from the last enemy killed, every obstacle (walls too) bursts
 * like glass and fades away; the reward and the doors come ROOM_REVEAL_T later (clear_t < 0 until
 * then). The cleared room is always empty: rewards need no room left for them in the layout. */
#define ROOM_REVEAL_T 1.2f
/* The doors open this long after the last kill (the reward shows first: no rushing out without it). */
#define DOORS_OPEN_T 1.8f
#define DOORS_AT (DOORS_OPEN_T - ROOM_REVEAL_T) /* the clear_t at which they open */
#define SHATTER_WAVE_T 0.5f /* the cracks reach the farthest obstacle */
#define CRACK_TIME 0.28f    /* an obstacle cracks for that long before it bursts */

typedef struct {
    RoomType type;
    RewardType reward;
    int depth;
    Obstacle obs[MAX_OBSTACLES];
    int nobs;
    FirePool fires[MAX_FIRES];
    int nfires;
    Door doors[MAX_DOORS];
    int ndoors;
    int wave, nwaves, last_wave_size;
    float boss_trail;             /* boss bar: hp fraction shown by the white damage trail */
    float wave_t, since_wave;
    float wave_time;     /* length of the current wave's timer (the bar at the bottom: wave_t / wave_time); 0 = none */
    /* The last wave's timer ran out (never a boss room): the walls close in on the ship, one cell per
     * CRUSH_STEP_T (crush.c). crush_in = how far each side (left, top, right, bottom) has come, in px;
     * the room's live bounds (room_x0()...) follow it. */
    bool crushing;
    float crush_in[4];
    int crush_from[4], crush_cell[4], crush_goal[4]; /* cells each side has come: before the step, now, at the end */
    float crush_t, crush_slide;                      /* time to the next step; 0..1 along the current one */
    bool cleared;
    float win_t;         /* > 0: the last boss is down, the victory comes when it runs out */
    float clear_t;       /* time since cleared; < 0 while the room shatters (reward, doors at 0) */
    V2 last_death;       /* where the last enemy died: the shatter wave starts there */
    float msg_cd;
    float hue;
    int id;              /* unique per room_enter; kept when returning from a secret room */
    int layout_id;       /* unique per room_enter, never reset (not even by game_init): headless tracks layouts by it */
    bool revisit;        /* restored after leaving its secret room (the only re-entered room) */
    /* Secret room: during combat a small see-through ghost shows up somewhere in the room every
     * 5-10 s, drifts for 0.5-2 s (fading in and out) and vanishes; a bomb next to the entry door
     * (room_secret_spot) then opens it. */
    bool has_secret;
    V2 ghost_pos, ghost_vel; /* the current ghost (anywhere in the room) */
    bool secret_open, secret_used;
    Side entry;          /* door the ship came in by (the side opposite the one it left by); closed, or the secret passage */
    float ghost_t, ghost_life, ghost_cd; /* time left / total time on screen, time to the next one */
    int ghosts;          /* how many showed up (the bot only "knows" after seeing one) */
    bool peaceful;       /* starts without enemies: launch bay, secret room without an ambush */
    bool secret_shop;    /* secret room variant: a shop instead of the bonus item */
    /* Boss room: which boss (bosses.c), and what it keeps outside its Enemy. */
    EnemyType boss;
    V2 boss_seg[WYRM_SEGS]; /* WYRM: body segments, head first */
    int boss_nseg;
    int arc_mask;           /* STORM: arcs between pylons (bit per StormArc) */
    float arc_t;            /* > 0 warning, then < 0 live (counting up to 0) */
    V2 wells[2];            /* SINGULARITY: small black holes orbiting it (phase 3) */
    float wells_t;          /* time they have left */
    float gravity;          /* SINGULARITY: pull on the ship (px/s at 300 px; < 0 pushes) */
    V2 graves[8];           /* NECRO: where the last enemies died (and what they were), to raise them */
    uint8_t grave_type[8];
    int ngraves;
    uint16_t mods;          /* MOD_BIT set of RoomMods (mods.c) */
    RewardType alt_reward;  /* machine room: the resource it drops when the ship cannot use it (what its door said) */
    int reward_count;       /* what the door said: coins / keys / bombs / hearts to drop (0: rolled when the room is cleared) */
    int elite;              /* MOD_ELITE: the only enemy type of the waves + 1 (0 = none) */
    int vault_coins;        /* VAULT: what the safe broke open pays, banked until the room is cleared */
    bool crush_slow;        /* MOD_COMPACTOR: the walls crawl until the last wave's timer runs out */
    /* MOD_STORM: strikes shown at `pos` STORM_WARN before they land (t counts down to 0, live down to -STORM_LIVE). */
    V2 strike_pos[8];
    float strike_t[8];
    int nstrikes;
    float storm_cd;
} Room;

/* Something a boss is about to do / doing that the ship must stay out of (the bot reads them,
 * the boss draws them): a segment a-b (a circle of radius r if a == b), live or still a warning. */
typedef struct {
    V2 a, b;
    float r;
    bool live;
} BossHazard;

/* Pseudo door indices for game_start_transition(). */
#define DOOR_SECRET (-1) /* entry door, bombed open: into the secret room */
#define DOOR_RETURN (-2) /* secret room exit, back to the room it was found in */
#define SECRET_CHANCE 0.15f
#define KEY_REWARD_2_CHANCE 0.14f /* key rooms: 2 keys */
#define KEY_REWARD_3_CHANCE 0.04f /* key rooms: 3 keys */
#define SECRET_BOMB_RADIUS 170.0f /* around room_secret_spot(), just inside the entry door */
#define GHOST_MIN_TIME 0.5f /* a secret's ghost stays 0.5-2 s on screen */
#define GHOST_MAX_TIME 2.0f
#define GHOST_DOOR_R 260.0f  /* ...and only shows up this close to the secret's door (room_secret_spot) */
#define GHOST_FADE 0.35f    /* fade in / out (at most a third of its time each) */
float room_ghost_alpha(void); /* 0..1, how visible the secret's ghost is right now */

/* ---- Player -------------------------------------------------------------- */

/* Stats that upgrades change. Player.base holds the character's values with stat trades and
 * augment costs; the Player fields of the same name are base + every upgrade turned on,
 * rebuilt by player_recompute() (the upgrades menu turns upgrades on and off). */
typedef struct {
    float dmg_mult, rate_mult, shot_speed_mult, speed, dash_cooldown, magnet, range_mult, hit_radius;
    float spread_mult, bullet_size, homing;
    int max_hp, extra_proj, pierce, bounce, drones, luck;
} Stats;

#define CLONE_DELAY 66 /* ticks the SHADOW CLONE lags behind the ship */

/* Synergies (synergies.c): extra effects that only exist while a given set of upgrades is turned on
 * together. Nothing to pick up: owning the parts is enough; a banner names each one the first time. */
typedef enum {
    /* elemental reactions */
    SYN_THERMAL_SHOCK, SYN_COMBUSTION, SYN_FLASH_FREEZE, SYN_PLAGUE,
    /* the sword */
    SYN_PARRY, SYN_BLADE_DANCER, SYN_THROWN_SWORD,
    /* pairs of shot upgrades */
    SYN_TESLA_BEAM, SYN_PLASMA, SYN_EVENT_HORIZON, SYN_SAW_HALO, SYN_MIRV,
    /* defence */
    SYN_MIRROR_SHIELD, SYN_SHIELD_BASH, SYN_STORM_ORBS, SYN_FROZEN_TIME, SYN_PHOENIX, SYN_FAILSAFE,
    /* drones, bombs, mines */
    SYN_INTERCEPTORS, SYN_LIGHTNING_ROD, SYN_DEMOLITION, SYN_SCORCHED_EARTH, SYN_SEEKER_MINES,
    /* extra shots, kill triggers */
    SYN_STARBURST, SYN_ECHO_CHAMBER, SYN_OVERLOAD, SYN_RAIN_OF_FIRE, SYN_CHAIN_EXPLOSION, SYN_SINGULARITY,
    /* damage rules, ways of playing */
    SYN_CLOSE_CALL, SYN_UNSTOPPABLE, SYN_EXECUTIONER, SYN_DEADEYE, SYN_COMPOUND, SYN_TRICK_SHOT, SYN_GRAND_FINALE,
    SYN_STACKED_DECK, SYN_STEADY_AIM,
    SYN_COUNT
} SynergyId;

typedef struct {
    const char *name;
    const char *desc;  /* '|' splits it over two lines, like upgrade descriptions */
    int need[3];       /* upgrades all required (UpgradeId + 1, 0 = unused) */
    int any1[6];       /* ...plus at least one of these, if any is listed */
    int any2[6];       /* ...and one of these */
    uint32_t color;
} SynergyDef;

extern const SynergyDef SYNERGIES[SYN_COUNT];

typedef struct {
    V2 pos, vel;
    float aim_angle;
    int hp, max_hp;             /* in half-hearts */
    float invuln;
    float chill_t;              /* hit by frost / a web (FROSTBITE, WEAVER): moves slower meanwhile */
    /* The newer upgrades' state (upgrades2.c), reset with the Player at every run. */
    int mark_uid, bounty_uid;   /* HUNTER'S MARK: the enemy marked; BOUNTY BOARD: this room's bounty */
    bool bounty_done, salvo_ready, resort_used, resort_pending, phoenix_used, sentry_on;
    float vamp;                 /* VAMPIRE FANGS: damage dealt towards the next half-heart */
    int combo, streak, nova_kills, exec_kills, lock_rooms;
    uint8_t notes[EN_COUNT];    /* FIELD NOTES: kills per enemy type this run */
    float hold_t, frenzy_t, slowmo_t, regen_t, shock_cd, rear_cd, afterimage_cd, sentry_cd, smite_cd, thunder_cd;
    V2 sentry_pos;              /* SENTRY GUN: where it stands this room */
    float rail_cd, sing_cd, doom_cd, nuke_t; /* RAILSTORM, SINGULARITY, DOOMSDAY, ANNIHILATOR */
    V2 nuke_pos;
    int nuke_volleys, vamp_room;  /* ANNIHILATOR volleys counted; VAMPIRE FANGS heals this room */
    float nuke_dmg;
    float fire_cd;
    float dash_t, dash_cooldown; /* dash_cooldown: the RECHARGE stat (dash recharge, scales every active) */
    V2 dash_dir;
    ActiveSlot act[ACT_COUNT];  /* indexed by ActiveId (ACT_NONE unused); none owned at the start */
    bool chaos[UPG_COUNT];      /* CHAOS: temporary upgrades (only ones not owned), removed when it ends */
    V2 turret_pos;              /* TURRET active: where it stands (alive while its effect runs) */
    float turret_cd, blade_a;   /* TURRET fire timer, BLADE STORM rotation */
    float spin;                 /* VULCAN: 0..1, spins up while firing */
    float momentum, momentum_t; /* MOMENTUM: damage bonus, time since the last hit */
    int growth;                 /* GROWTH: rooms cleared while owning it */
    float missile_cd, orbital_cd, echo_t, echo_aim, echo_dmg;
    bool emergency_used;        /* EMERGENCY SHIELD: spent for this room */
    bool room_hit;              /* hit in this room already (FIRST HIT malus) */
    float heat, jam_t, trig_t, cold_t; /* maluses: overheat, jam left, trigger held, no-fire time at room start */
    float charge;               /* CHARGE SHOT: 0..1, builds while not firing */
    int graze;                  /* GRAZE: bullets brushed this room */
    int wild;                   /* WILD CARD: this room's bonus (0 damage, 1 fire rate, 2 speed) */
    int flawless;               /* FLAWLESS: rooms cleared in a row without a hit */
    float vengeance_t, afterburn_t, pd_cd, trail_cd;
    float burn_t, burn_tick;    /* set on fire by burning oil: hurt now, then every second while it burns */
    V2 pd_from, pd_to;          /* POINT DEFENSE: last beam (drawn while pd_show > 0) */
    float pd_show;
    float bolt_cd, orb_zap_cd;  /* TESLA BEAM / STORM ORBS synergies */
    int pd_drone;               /* INTERCEPTORS: the drone that fires next */
    bool syn_seen[SYN_COUNT];   /* synergies already announced this run */
    V2 trail[CLONE_DELAY];      /* SHADOW CLONE: where the ship was, one entry per tick (ring buffer) */
    int trail_i;
    float speed;
    int coins, keys, bombs;
    Stats base;                 /* before upgrades (see Stats) */
    bool upg_off[UPG_COUNT];    /* turned off in the upgrades menu: no effect at all until turned back on */
    bool malus_off[UPG_COUNT];  /* the upgrade's malus is gone for good (purifier, fusion forge) */
    int16_t fused_into[UPG_COUNT]; /* fusion forge: B -> A + 1 when B was fused into A (B stays owned, its bonus keeps working, the menu shows one entry) */
    float dmg_mult, rate_mult, shot_speed_mult;
    int extra_proj, pierce, bounce;
    float homing;
    int drones, drone_idx;
    float drone_cd;
    float magnet;
    float range_mult;           /* bullet lifetime multiplier */
    float hit_radius;           /* bullet hitbox radius */
    int luck;                   /* room reward modifier */
    int upg_count[UPG_COUNT];
    int shield;                 /* temporary hearts, in half-hearts, lost before hp */
    int revives;                /* SECOND WIND charges */
    float spread_mult, bullet_size;
    int volleys;                /* volleys fired (CAPACITOR / FLANK cadence) */
    int room_kills, leech_kills;
    float mine_cd;
    /* Sword (Knight): one swing sweeps an arc from one side of the aim to the other. */
    float swing_t, swing_len;   /* time left / total duration of the current swing */
    float swing_a0, swing_half; /* centre angle and half-width of the arc */
    float swing_reach, swing_prev; /* blade length, blade angle (relative to a0) last tick */
    float swing_dmg;
    int swing_dir, swing_id;
} Player;

/* ---- Characters ---------------------------------------------------------- */

typedef enum { CHAR_NORMAL, CHAR_SHOTGUNNER, CHAR_KNIGHT, CHAR_COUNT } CharId;

typedef struct {
    const char *name;
    const char *desc[3];  /* lines shown on the character select screen */
    uint32_t color;
    int hearts, armor;    /* armor = shields (temporary hearts) at the start */
    float dmg_mult, speed_mult, hit_radius;
    int start_upgrade;    /* weapon mod owned from the start (-1 = none: the plain blaster) */
} CharDef;

extern const CharDef CHARACTERS[CHAR_COUNT];

/* ---- Unlocks (unlocks.c) -------------------------------------------------- */

/* What a save file has unlocked, kept from run to run (G.prof, written with every save and
 * rewritten at once when something is unlocked). A new save: the first ship and
 * UNLOCK_START_COUNT upgrades; each boss beaten unlocks UNLOCK_BATCH more (fixed order).
 * Outside a save (tests, the menu's demo before a save is chosen) everything is unlocked. */
#define FLOORS 4              /* floors of BOSS_EVERY rooms, each ending with its boss: the 4th boss ends the game */
#define ENEMY_TIERS 2         /* floor bosses that each unlock 5 more enemies of their floor (10 at the start + 5 + 5 = 20) */
#define ROOM_LOCKS 5          /* forge, vault, purifier, Baphomet (the floors' bosses) and trader (the first boss with the second ship) start locked */
#define UNLOCK_START_COUNT 24
#define UNLOCK_BATCH 6
typedef struct {
    uint8_t upg[UPG_COUNT], chr[CHAR_COUNT];
    uint8_t rooms[ROOM_TYPE_COUNT]; /* room kinds this save may meet behind a door (ROOM_LOCKS of them start locked, see unlocks.c) */
    uint8_t boss_floor[FLOORS]; /* times the boss of each floor was beaten, at most ENEMY_TIERS: each unlocks 5 enemies of that floor */
    uint8_t seen[EN_COUNT]; /* bestiary: every enemy type met at least once (written at once) */
    int32_t runs, deaths, bosses, best_depth;
} Profile;

#define MAX_SHIELD 12 /* half-hearts: 6 shields */
#define SPREAD_MIN 0.2f /* bounds of Player.spread_mult (trades, lenses) */
#define SPREAD_MAX 3.0f

/* ---- Game ---------------------------------------------------------------- */

/* GS_UPGRADES: the upgrades menu (TAB), the world is frozen behind it. */
typedef enum { GS_TITLE, GS_CHAR_SELECT, GS_PLAYING, GS_PAUSED, GS_GAMEOVER, GS_DEBUG, GS_UPGRADES } GameState;

typedef struct {
    bool up, down, left, right, fire;
    bool restart_held; /* R kept down: held RESTART_HOLD_T in a run, a new run with the same ship */
    bool teleport_pressed; /* F4: the teleport page of the debug menu */
    bool skip_pressed; /* test key (A on AZERTY, Q on QWERTY: the same key): the room is finished at once */
    /* edge-triggered, cleared after each simulation tick */
    bool active_pressed, bomb_pressed, interact_pressed /* E: take / buy / use */, pause_pressed, restart_pressed, confirm_pressed, debug_pressed;
    bool char_pressed; /* C: back to the character select screen */
    bool menu_pressed; /* TAB: upgrades menu */
    bool delete_pressed; /* DEL / BACKSPACE: delete (load screen) */
    bool click_pressed; /* left click (also sets confirm_pressed) */
    int number;        /* number key pressed, 0-based (-1 = none) */
    int wheel;         /* mouse wheel: -1 up, +1 down */
    V2 mouse;          /* logical coordinates */
} Input;

typedef struct {
    GameState state;
    Rng rng;
    Input in;
    Side entry_side; /* entry door of the next room_enter (opposite the door taken) */
    uint16_t entry_mods; /* MOD_BIT set of the next room_enter (the door taken), used up by it */
    int entry_count;     /* the door taken: its resource count, used up by room_enter */
    RewardType entry_alt; /* ...and its machine room's fallback resource */
    /* Main menu (title.c): a demo run played by the bot behind it, on its own generator. */
    bool demo;
    Rng demo_rng;
    Rng fx_rng;          /* particles: never the game's generator (see fx.c) */
    float demo_dead_t, demo_room_t;
    int demo_room_id;
    int title_sel;
    bool title_up, title_down;
    int title_page;   /* TitlePage: main menu, saves list, save menu, unlocks */
    int load_sel;     /* load screen: highlighted save (index in the list) */
    int load_confirm; /* load screen: slot waiting for a second press to be deleted (0: none) */
    int menu_slot;    /* save menu (CONTINUE): the save file chosen, its run started / resumed from there */
    int unl_sel;      /* unlocks screen: highlighted upgrade */
    int bes_sel;      /* bestiary: highlighted entry (index in its list, -1: none met) */
    Profile prof;     /* unlocks of the current save (unlocks.c) */
    Player pl;
    Room room;

    PBullet pb[MAX_PBULLETS]; int npb;
    EBullet eb[MAX_EBULLETS]; int neb;
    Enemy en[MAX_ENEMIES]; int nen;
    Spawn sp[MAX_SPAWNS]; int nsp;
    Particle pt[MAX_PARTICLES]; int npt;
    Pickup pk[MAX_PICKUPS]; int npk;
    Popup pop[MAX_POPUPS]; int npop;
    Machine mc[MAX_MACHINES]; int nmc; /* machines (only once a casino is cleared) */
    int next_uid;

    float time, run_time;
    int ticks;
    float shake, hitstop, hurt_flash, bomb_flash;
    bool transitioning;
    float trans_t;
    int trans_door;
    char banner[96], banner_sub[320];
    float banner_t, banner_max;
    SDL_FColor banner_col;
    int rooms_cleared, kills;
    int items_bought; /* shop purchases this run */
    int active_uses;  /* active items used this run */
    bool bot_toggles; /* dev (--toggle-upgrades): the bot keeps turning random upgrades off and on */
    int toggles;      /* upgrades turned on / off this run */
    int machine_uses, augments, trades; /* supply press / slot machine uses, pacts taken, stat trades taken */
    int room_counter; /* source of Room.id */
    Room parent;      /* room the current secret room was found in, restored on exit */
    Pickup parent_pk[MAX_PICKUPS]; int parent_npk;
    bool force_secret;   /* dev: every eligible room hides a secret */
    bool reveal_secrets; /* dev: mark hidden secrets permanently */
    int secret_shop_override, secret_ambush_override; /* dev (debug menu / tests): -1 random, 0 no, 1 yes */
    float gameover_t;
    bool victory;       /* the game-over screen is the victory one: the last floor's boss fell (game_victory) */
    float restart_hold; /* how long R has been held in this run (RESTART_HOLD_T: a new run); < 0: let it go first */
    float last_enemy_damage_t; /* G.time of the last hit on an enemy (stall detection in tests) */

    bool quit, show_debug, god;
    bool show_stats; /* F2: semi-transparent stats overlay (hidden by default) */
    int character;   /* CharId of the current / next run (kept across restarts) */
    int save_slot;   /* save file of this run (save.c; 0: none - the demo, tests, saving off) */
    int char_hover;  /* character select screen: highlighted card */
    bool sel_left, sel_right; /* character select: previous held state, for edge detection */
    V2 sel_mouse;
    GameState debug_prev; /* state to return to when the debug menu closes */
    float fps;
} Game;

extern Game G;
static inline bool room_has_mod(int m) { return G.room.mods & MOD_BIT(m); }

/* The room's live bounds: the arena, or less while its walls close in (crush.c). */
static inline float room_x0(void) { return ARENA_X0 + G.room.crush_in[0]; }
static inline float room_y0(void) { return ARENA_Y0 + G.room.crush_in[1]; }
static inline float room_x1(void) { return ARENA_X1 - G.room.crush_in[2]; }
static inline float room_y1(void) { return ARENA_Y1 - G.room.crush_in[3]; }

static inline float frand(float lo, float hi) { return rng_range(&G.rng, lo, hi); }
static inline int irand(int lo, int hi) { return rng_int(&G.rng, lo, hi); }
static inline bool chance(float p) { return rng_chance(&G.rng, p); }

/* ---- Stats (trades offered in workshops and shops) ------------------------------ */

typedef enum {
    ST_DAMAGE, ST_FIRE_RATE, ST_RANGE, ST_MAX_HP, ST_MOVE_SPEED, ST_DASH,
    ST_SHOT_SPEED, ST_HITBOX, ST_LUCK, ST_SPREAD,
    ST_SHIELD, /* gain-only: +3 shields (temporary hearts) */
    ST_COUNT
} StatId;

typedef struct {
    const char *name;
    const char *up_text;
    const char *down_text;
} StatDef;

extern const StatDef STATS[ST_COUNT];

#define WORKSHOP_TRADES 4
#define TRADER_OFFERS 3
#define TRADE_COST 2          /* upgrades paid per upgrade bought at the trader */
#define PURIFY_OFFERS 3
#define PURIFY_PRICE_K 1.5f   /* a purification costs that many shop upgrades (shop_price) */
#define FORGE_OFFERS 3
#define FORGE_PRICE_K 1.0f    /* a fusion costs that many shop upgrades (shop_price) in coins */
#define CONTAINER_PRICE_K 2.0f /* the infirmary's heart container: that many shop upgrades */
#define SAFE_HP 520.0f        /* damage to break the safe (a bare ship: ~9 s) */
#define SAFE_TIME 18.0f       /* seconds from the first enemies for the bonus to melt away */
#define SAFE_MIN_COINS 6      /* paid at the last second... */
#define SAFE_MAX_COINS 48     /* ...and at the first (linear in between) */
#define SHOP_MIN_COINS 6 /* a shop is only worth entering with that many coins (its cheapest supply): with fewer it drops a random resource instead */
#define SHOP_ITEMS 5 /* a shop always sells exactly this many items (upgrades, stat trades) */
#define SHOP_SUPPLIES 3 /* ...plus a supply counter: a heart, a bomb, a key (shop_supply_spot) */
V2 shop_supply_spot(int i);
#define UPGRADE_CHOICES 2 /* upgrade rooms and bosses offer this many upgrades to pick from */
#define ACTIVE_CHOICES 3  /* active rooms offer this many items to pick from */
#define COLOR_STATS 0x4dffd2
#define COLOR_SHOP 0xff6bd6
#define COLOR_SECRET 0xffb347
#define COLOR_WORKSHOP 0xc8ff4d
#define COLOR_CASINO 0xffd23f
#define COLOR_BAPHOMET 0xff2a2a
#define COLOR_BOSS 0xff3d5a
#define COLOR_TRADER 0x5dc8ff
#define COLOR_PURIFIER 0x9dffea
#define COLOR_VAULT 0xe8d27a
#define COLOR_FORGE 0xff7a3d
#define COLOR_KEYS 0xc9d6e8
#define COLOR_INFIRMARY 0xff8aa8
static inline int stat_offer_encode(int up, int down) { return up * ST_COUNT + down; }
static inline int stat_offer_up(int offer) { return offer / ST_COUNT; }
static inline int stat_offer_down(int offer) { return offer % ST_COUNT; }

/* upgrades.c - passive upgrades and their effects (hooks called by the systems they modify) */
int upgrade_roll(void);               /* not owned, never a weapon mod nor a Baphomet exclusive; -1: none left */
void upgrade_apply(int u);            /* take it (with its banner); does nothing if already owned */
void upgrade_grant(int u);            /* same, silently (starting weapon mod) */
bool upgrade_capped(int u);           /* cannot be offered: owned, or useless right now */
/* ---- Maluses (maluses.c): every upgrade has one, removed with it when turned off ---- */
typedef enum {
    MAL_NONE,
    /* stat maluses (applied by stat_effect, so the stats stay derived) */
    MAL_DAMAGE, MAL_RATE, MAL_RANGE, MAL_SPEED, MAL_SHOT_SPEED, MAL_HITBOX, MAL_SPREAD, MAL_RECHARGE, MAL_LUCK, MAL_MAX_HP,
    /* behaviour maluses (read through malus()) */
    MAL_RECOIL, MAL_FIRE_SLOW, MAL_ENEMY_BULLETS, MAL_INVULN, MAL_PRICES, MAL_TAX, MAL_OVERHEAT, MAL_SPINUP,
    MAL_FIRST_HIT, MAL_CROWD, MAL_DRIFT, MAL_COLD_START, MAL_HIT_COINS, MAL_ACTIVE_TIME, MAL_HEAL,
    MAL_FRAGILE,     /* every hit costs `amount` more half-hearts */
    MAL_REVENGE,     /* % chance that an enemy killed fires a last bullet at the ship */
    MAL_BIG_BULLETS, /* enemy bullets % bigger */
    MAL_NO_MERCY,    /* getting hit no longer clears the bullets around the ship */
    MAL_MOVING_RATE, /* -% fire rate while moving */
    MAL_CUSTOM, /* part of the upgrade's own effect (text) */
    MAL_COUNT
} MalusKind;
typedef struct {
    MalusKind kind;
    int amount; /* % for most kinds, tenths of a second for OVERHEAT / SPINUP / COLD_START */
    const char *text; /* MAL_CUSTOM only */
} MalusDef;
extern const MalusDef MALUSES[UPG_COUNT];
#define OVERHEAT_AFTER 3.0f
int malus(MalusKind k);                /* sum over upgrades owned and turned on (CHAOS loans carry none) */
float malus_pct(MalusKind k);          /* malus(k) / 100 */
bool malus_is_stat(MalusKind k);
void malus_stat_effect(Stats *s, int u);
const char *upgrade_malus_desc(int u); /* '|' splits it over two lines */
float draw_upgrade_texts(float cx, float y, float scale, float alpha, int u); /* bonus + malus, returns height */
void upgrade_banner_text(int u, char *out, int size);
bool malus_fire_gate(bool trigger);    /* spin-up, overheat, room-start delay: may the gun fire now? */
void malus_on_room_enter(void);
void malus_on_room_clear(void);
int malus_on_hurt(void);               /* a hit landed: extra damage (first hit of the room), coins lost */

/* Upgrades menu: turn an owned upgrade off / back on (false for one-time ones, see upgrade_toggleable). */
bool upgrade_toggle(int u);
bool upgrade_toggleable(int u);
/* Player.base + the upgrades turned on (all owned ones if ignore_off), clamped to the stat floors. */
void stats_compute(Stats *out, bool ignore_off);
void player_recompute(void);          /* derived Player stats from stats_compute(); clamps hp */
void player_add_shield(int half_hearts);
float upg_volley_damage_mult(void);   /* BLOODLUST, WOUNDED FURY, LAST STAND */
float upg_fire_rate_mult(void);       /* ADRENALINE, SCAR TISSUE, DEFLECTOR */
float upg_move_speed_mult(void);      /* ADRENALINE, LAST STAND */
float upg_shot_speed_mult(void);      /* DEFLECTOR */
/* upgrades2.c: the 50 newer upgrades and 16 newer exclusives, called from the hooks of upgrades.c. */
void upg2_stat_effect(Stats *p, int u);
const char *upg2_desc_at(int u, char *b, int size); /* NULL: the table's text */
bool upg2_capped(int u, const Stats *all);
void upg2_grant(int u);
float upg2_volley_damage_mult(void);
float upg2_fire_rate_mult(void);
void upg2_on_bullet_spawn(PBullet *b);
void upg2_on_volley(V2 muzzle, float aim, float dmg, SDL_FColor col);
float upg2_hit_damage(const PBullet *b, const Enemy *e, float dmg);
void upg2_after_hit(PBullet *b, Enemy *e, float dmg);
void upg2_on_kill(const Enemy *e);
void upg2_on_hurt(void);
bool upg2_lethal_hit(void);           /* PHOENIX FEATHER */
void upg2_on_active(void);
void upg2_on_bomb(void);
void upg2_bullet_death(const PBullet *b); /* COMBO: a shot that missed */
void upg2_on_block_break(V2 at);      /* DEMOLISHER */
void upg2_update(void);
void upg2_on_room_enter(void);
void upg2_on_room_clear(void);
void upg2_draw(void);
float upg_enemy_tempo(void);          /* CHRONOSPHERE: enemies move and fire slower */
bool upg_armor_piercing(void);        /* ARMOR PIERCER: armour, plates and bubbles do not count */
bool upg_fireproof(void);             /* FIREPROOF: fire and the blocks' blasts never hurt the ship */
bool upg_ghost_hull(void);            /* GHOST HULL: touching enemies does not hurt */
void player_bomb_blast(void);         /* a bomb's blast (without spending one: LAST RESORT) */
/* Description with the exact numbers (chances...) once the upgrade has n stacks. */
const char *upgrade_desc_at(int u, int n);
/* What taking it now would give (current stacks + 1). */
const char *upgrade_desc(int u);
int upgrade_by_name(const char *name);
/* Baphomet: a random exclusive upgrade of this tier not owned yet, or -1. */
int upgrade_roll_exclusive(int tier);
bool baphomet_pact_possible(void);
/* DEFLECTOR SHELLS: a big player bullet turns the enemy bullets it touches. */
void upg_reflect(PBullet *b);
void upg_on_volley(V2 muzzle, float aim, float dmg, SDL_FColor col);
void upg_on_bullet_spawn(PBullet *b);
void upg_bullet_update(PBullet *b);   /* BOOMERANG */
float upg_hit_damage(const PBullet *b, const Enemy *e);
void upg_after_hit(PBullet *b, Enemy *e, float dmg);
void upg_on_kill(const Enemy *e);
bool upg_on_lethal_hit(void);         /* SECOND WIND: true = survived */
void upg_on_hurt(void);
void upg_on_active(void);            /* NOVA TRIGGER: any active item used */
bool upg_block_hit(void);             /* EMERGENCY SHIELD: true = this hit is ignored */
void upg_on_bounce(PBullet *b);       /* SMART BOUNCE, PINBALL */
bool upg_ricochet(PBullet *b, const Enemy *hit);
void upg_on_bullet_expire(const PBullet *b); /* FIREWORKS */
bool upg_graze_on(void);
void upg_on_graze(V2 at);
float upg_enemy_bullet_tempo(V2 pos); /* CHRONO BUBBLE */
V2 upg_clone_pos(void);               /* SHADOW CLONE */
void upg_return_bullet(EBullet *e, V2 from); /* an enemy bullet turned into one of ours (marked dead) */
void upg_enemy_reactions(Enemy *e);   /* elemental reaction synergies, once per enemy tick */
void upg_bolt(V2 from, Enemy *t, float dmg, int jumps);
void upg_fire_patch(V2 pos, float dmg, float life);
void upg_vortex(V2 pos, float life, float dmg);
void upg_mega_shot(V2 muzzle, float aim, float dmg);
void upg_on_bomb(void);

/* synergies.c */
bool syn(int s);                      /* is this synergy active right now? */
void synergies_update(void);          /* announces the ones just completed */
int synergies_active(int *out);
const char *synergy_parts(int s);     /* "A + B", the parts owned that make it */
/* Area damage with its fx (explosions: payload, rockets, boom...). */
void explode_at(V2 pos, float radius, float dmg, const Enemy *skip, uint32_t color);
/* Lightning from an enemy to the nearest ones not hit yet. */
void chain_lightning(Enemy *from, float dmg, int jumps, float range);
int upg_has(int u);                   /* stacks owned + 1 while CHAOS lends it */
void upg_update(void);
void upg_on_room_enter(void);
void upg_on_room_clear(void);
float upg_enemy_bullet_speed(void);   /* CHRONO */
void upg_draw(void);

/* stats.c */
bool stat_can_change(int s, bool up);
void stat_apply(int s, bool up);
int stats_roll_offers(int *out, int max);
void stat_offer_take(int offer);
/* Multiplier applied to room rewards (coin amount); >= 0.25. */
float luck_factor(void);

/* game.c */
void game_init(uint64_t seed);
void game_new_run(void);
void game_update(void);
void game_render(void);
void game_over(void);
void game_victory(void); /* the last floor's boss is beaten: the run is won (the game-over screen, as a victory) */
void game_start_transition(int door);

/* room.c */
void room_enter(RoomType type, RewardType reward, int depth);
void room_update(void);
void room_draw_floor(void);
void room_draw_walls(void);
bool room_collide_circle(V2 *pos, float r, V2 *normal);
/* Same, also tells which obstacle was hit last (-1: an arena wall / nothing). */
bool room_collide_obstacle(V2 *pos, float r, V2 *normal, int *which);
/* True when a circle of radius margin can travel from a to b without touching an obstacle. */
bool room_line_clear(V2 a, V2 b, float margin);
bool room_line_clear_walls(V2 a, V2 b, float margin); /* only the indestructible walls count */
/* Next point to walk towards to reach goal, going around obstacles. */
V2 room_navigate(V2 from, V2 goal, float margin);
/* Same; walls_only: plans through the breakable blocks. *reached: false when goal cannot be reached. */
V2 room_navigate_ex(V2 from, V2 goal, float margin, bool walls_only, bool *reached);
int room_block_between(V2 a, V2 b, float margin); /* first breakable block on the way, or -1 */
/* Blocks: damage from the ship's shots / blade / blasts; broken ones go off at the end of the tick. */
void room_block_damage(int i, float dmg);
void room_blocks_blast(V2 pos, float radius, float dmg); /* every block touched by the blast */
void room_block_ignite(int i); /* sets a crate / barrel / oil drum on fire (no effect on the others) */
void room_blocks_break_all(void);                          /* a bomb: every breakable block */
int room_count_obstacles(int kind);
/* Every free spot of the room (and every door) can be reached by the ship from the entry when only
 * the walls count: whatever stands in the way can be broken. */
bool room_walls_connected(void);
bool fire_pool_at(V2 p, float r); /* is a circle touching burning oil? */
/* blocks.c: room layouts (walls + breakable blocks), what breaking a block does, burning oil. */
void room_gen_layout(void);
/* Boss rooms: the boss's own fixed layout (symmetric; nothing for an open-floor boss). */
void room_boss_layout(EnemyType boss);
/* A pool of burning oil (blocks.c: oil drums, the FORGE). */
void room_fire_add(V2 c, float r, float life);
void blocks_update(void);
void blocks_draw(SDL_FColor wall, SDL_FColor edge);
void fires_draw(void);
void room_nav_reset(void);
/* The cell map used to skip collision tests in the open still covers every solid obstacle (headless). */
bool room_occupancy_ok(void);
void room_shatter_rect(SDL_FRect r, SDL_FColor col, V2 origin); /* the glass burst of a shattering obstacle, for something else */
void room_draw_cracks(SDL_FRect r, float k, int seed);           /* its cracks before it bursts, k 0..1 */
void room_shatter_start(V2 origin); /* the room is cleared: crack every obstacle, in a wave from origin */
void room_obstacles_vanish(void);   /* every obstacle gone at once, no effect (debug clear) */
void room_crush_grind(SDL_FRect box, bool all); /* the closing walls: obstacles they reach (all: every one) burst into glass */

/* Closing walls (crush.c): the last wave's timer ran out. */
#define CRUSH_STEP_T 0.9f  /* one cell closer every... */
#define CRUSH_SLIDE_T 0.3f /* ...sliding in this long */
#define CRUSH_MIN_W 10     /* the walls stop at a box this many cells wide... */
#define CRUSH_MIN_H 8      /* ...and high, around where the ship was */
#define CRUSH_SLOW_STEP_T 3.2f /* MOD_COMPACTOR: one cell every... until the last wave's timer runs out */
void crush_start(bool slow);
void crush_update(void);  /* a step every CRUSH_STEP_T, then everything is kept inside (room_update) */
void crush_end(void);     /* the room is cleared: the closing walls burst too */
int crush_hazards(BossHazard *out, int max); /* their inner edges, for the bot */
void crush_draw(void);
void draw_wave_bar(void); /* hud: the wave timer at the bottom */

/* Room modifiers (mods.c). */
#define WAGER_COST 2      /* MOD_WAGER: hp (a whole heart) to go through its door */
#define STORM_R 74.0f     /* a lightning strike's radius */
#define STORM_WARN 1.1f   /* shown that long before it lands */
#define STORM_LIVE 0.12f  /* hurts that long */
#define STORM_ENEMY_DMG 40.0f
#define POWDER_BLOCKS_MIN 26 /* MOD_POWDER: at least that many blocks (barrels, oil drums, a few crates) */
extern int g_mod_override; /* tests (--mod NAME, repeatable): MOD_BIT set every room gets (where allowed), -1 = random */
const char *mod_name(int mod);
const char *mod_desc(int mod);
uint32_t mod_color(int mod);
int mod_by_name(const char *name);
bool mod_allowed(RoomType type, RewardType reward, int mod); /* any room with a fight but a boss room; WAGER: combat rooms only;
                                                              * difficulty levels: combat rooms giving coins / bombs / keys / hearts */
unsigned mods_allowed(RoomType type, RewardType reward, unsigned mods); /* the part of a MOD_BIT set that room can have */
unsigned mod_roll(RoomType type, RewardType reward); /* a door's MOD_BIT set: usually none, one at most; every modifier the room can have has the same chance */
float room_difficulty_enemies(void);      /* the size of every wave: x1 .. x2.5 */
int room_difficulty(void);                 /* 0 = none, 1..4 = HARD .. IMPOSSIBLE (the highest set) */
int difficulty_reward_count_mods(uint16_t mods, int n);
int difficulty_reward_count(int n);        /* a drop of n coins / bombs / keys / hearts in this room: x1.5 .. x3, one more at least */
void mods_on_room_enter(void); /* after the room is set up (banner, elite type, horde) */
void mods_update(void);        /* the storm (room_update, while fighting) */
int mods_hazards(BossHazard *out, int max); /* the strikes, for the bot */
void mods_draw_under(void);    /* strikes on the floor */
void mods_draw_over(void);     /* the blackout's darkness, over the world */
bool mod_door_pay(const Door *d); /* going through a door: pays a wager (false = refused) */
void pickups_set_choice_group(int group); /* the group of the next one-of-N offers (default 1) */
extern bool g_fx_off; /* no particles (headless): the game plays the same without them */
extern bool g_dense_blocks; /* --dense-blocks: as many blocks and gates as possible (test) */
V2 door_position(Side side);
RewardType room_roll_reward(void);
V2 room_secret_spot(void);
V2 room_entry_spot(void);       /* where the ship arrives: just inside the entry door */
Side side_opposite(Side s);
/* The launch bay and secret rooms are the exceptions to "every room starts full of enemies". */
static inline bool room_type_peaceful(RoomType t) { return t == ROOM_START || t == ROOM_SECRET; }
/* The doors that cost a key: shops, casinos, the six machine rooms of special.c, upgrade and active rooms - always, and no other. */
static inline bool door_needs_key(RoomType t, RewardType rw) {
    return t == ROOM_SHOP || t == ROOM_CASINO || (t >= ROOM_TRADER && t <= ROOM_INFIRMARY) || (t == ROOM_COMBAT && (rw == RW_UPGRADE || rw == RW_ACTIVE));
}
#define SECRET_SHOP_CHANCE 0.10f   /* a secret room holds a shop instead of its bonus item */
#define SECRET_AMBUSH_CHANCE 0.10f /* a secret room is guarded by enemies before its loot */
/* A bomb went off at pos: opens the secret passage if pos is next to the bottom door of a room hiding one. */
void room_bomb_at(V2 pos);
void room_enter_secret(void);
void room_return_from_secret(void);
/* Dev only (debug menu): kill the pending waves and clear the room at once. */
void room_debug_clear(void);
/* Test key: every enemy gone (no kill effects), no more waves, the room cleared like a real fight's end (shatter,
 * reward, a boss's unlocks). */
void room_skip(void);

/* player.c */
void player_init(void);
void player_update(void);
void player_draw(void);
bool player_hurt(int dmg); /* true when the hit lands (not dodged: invulnerable, dashing, shielded) */
#define CONTACT_DAMAGE 30.0f   /* an enemy ramming the ship takes this much (x damage stat): small rushers die */
/* Ship of a character, also used by the character select screen. */
void draw_ship(int character, V2 pos, float angle, float scale, bool blink);

/* actives.c */
void actives_update(void);            /* input, recharge, effects (from player_update) */
void actives_draw(void);              /* shield bubble, strength / chaos auras */
void active_give(int a);              /* a new item (level 1), or a level up of one owned */
void actives_recharge_all(void);     /* a new screen: every active item ready again */
bool active_toggle(int a);            /* TAB menu: turn an owned item off / on (ends its effect); false if not owned */
int actives_owned(int *out);          /* ActiveIds owned, in table order; returns the count */
bool active_running(int a);           /* owned, turned on, effect running */
int active_level_on(int a);           /* level if owned and turned on, else 0 */
bool active_strength_on(void);        /* STRENGTH: x4 shots */
bool active_shield_on(void);          /* SHIELD: invincible */
bool active_time_stop_on(void);       /* TIME STOP: enemies and their bullets frozen */
float active_cooldown(int a, int level); /* with the RECHARGE stat */
float active_duration(int a, int level); /* effect time (dash: invulnerable dash time; 0 = instant) */
int active_chaos_count(int level);       /* CHAOS: upgrades lent */
const char *active_desc(int a, int level); /* '|' splits it over two lines */
void active_end_chaos(void);
const char *active_option_name(int a); /* lowercase name for --active */
void draw_active_icon(int a, V2 pos, float s, float t);
/* Active room: ACTIVE_CHOICES distinct actives; ones owned only while they can still level up. */
void active_spawn_choices(V2 center);
#define COLOR_ACTIVE 0xffe94d

/* characters.c */
void character_apply(void); /* starting stats / weapon of G.character, after player_init's defaults */
const char *character_option_name(int c); /* lowercase name for --char */
void char_select_open(void);
void char_select_open_paused(void); /* NEW RUN from the pause: ESC goes back to it */
/* unlocks.c */
void profile_fresh(Profile *p);       /* a new save: the first ship and the starting upgrades */
void profile_unlock_all(Profile *p);  /* tests, demo: everything */
bool upgrade_unlocked(int u);         /* in G.prof: may be offered (rolls never offer a locked one) */
bool character_unlocked(int c);
bool room_unlocked(RoomType t);       /* in G.prof: may be behind a door (gen_doors) */
int profile_unlocked(const Profile *p); /* upgrades + ships unlocked */
int profile_upgrades_unlocked(const Profile *p);
int unlocks_total(void);              /* UPG_COUNT + CHAR_COUNT */
int unlock_percent(int unlocked, int total);
const char *character_unlock_hint(int c); /* how a locked ship is unlocked */
void unlocks_on_room(int depth);      /* room_enter: ships unlocked by depth, best depth */
void unlocks_boss_beaten(void);       /* the next UNLOCK_BATCH upgrades (+ ships), saved at once */
void bestiary_saw(EnemyType t);       /* an enemy shows up: met for the first time, marked and saved at once */
int profile_seen(const Profile *p);   /* enemy types met */

/* bestiary.c: every enemy, greyed until met; the one selected plays in a small screen (its own world
 * in G while it is open, the real one - menu's demo or paused run - set aside and put back). */
const char *enemy_title(EnemyType t); /* "CHASER", "THE OVERSEER"... */
const char *enemy_desc(EnemyType t);  /* what it does, '|' between lines */
void bestiary_open(int sel);          /* sel: the type to show (-1: the first met) */
bool bestiary_update(void);           /* false once it is left (ESC) */
void bestiary_draw(void);
void bestiary_close(void);            /* the real world back as it was (path finder caches rebuilt) */
int bestiary_world_type(void);        /* the type playing in the small screen (-1: closed) */
bool title_world_hidden(void);        /* the bestiary: G holds its small world, not drawn full screen */
int floor_of_depth(int depth);        /* 1..FLOORS (rooms 1-10: 1, 11-20: 2...; deeper: the last) */
bool enemy_unlocked(int t);           /* in G.prof: tier 0, or its floor's boss beaten often enough */
float enemy_wave_weight(int t, int floor); /* how likely it is in a wave of that floor: 0 locked / later floor, 1 its own floor, halved per floor since */
int unlocks_next(int *out, int max);  /* the upgrades the next boss will unlock (tests) */
/* Main menu (title.c) with the bot-played demo run behind it. */
void title_open(void);
void title_back(void);          /* Q / character select ESC: the menu of the save played, else the main menu */
void title_unlocks_open(void);   /* the unlocks screen of G.prof (--unlocks-menu) */
void title_bestiary_open(int type); /* the bestiary on that enemy type (-1: the first met; --bestiary-menu) */
bool title_save_menu(int slot);  /* the menu of a save (--save-menu) */
void title_update(void);
void title_draw(void);
/* ESC in a run (GS_PAUSED): the save's menu over the frozen run - RESUME RUN goes on with it, NEW RUN,
 * UNLOCKS, BESTIARY; TAB the upgrades menu, Q leaves the run (title_back). */
void pause_open(void);
void pause_open_new_run(void); /* back from the character select, on NEW RUN */
void pause_update(void);
void pause_draw(void);
void demo_update(void);
bool demo_visible(void); /* the world on screen is the demo (menu / character select over it) */
void game_world_step(void); /* one tick of the world in a run (hitstop, transitions, every system) */
void char_select_update(void);
void char_select_draw(void);

/* weapons.c - the gun (shaped by the weapon mods turned on), the Knight's sword, drones */
void weapons_update(void);
void drones_draw(void);
void sword_draw(void);
void weapon_on_hit(PBullet *b, Enemy *e, float dmg); /* flamer burn, arc chains */
void weapon_bullet_death(PBullet *b);  /* explosive / cluster shots */
void weapon_echo(V2 from, float aim, float dmg); /* ECHO, SHADOW CLONE: a weaker copy of the volley */
bool weapon_trigger(void); /* the fire button, or DEAD MAN'S TRIGGER holding it down in a fight */
/* Every player shot (turret, tail / side shots, echo, drones, nova, enemy bullets turned around...)
 * carries the upgrades like the main gun's: shot-shaping behaviours, pierce, bounces, homing, size,
 * on-spawn effects (crit...), and - unless `scaled` (damage / range / speed already derived from a
 * volley) - the damage, range and shot speed bonuses. Only upgrade by-products (fragments, mines, vortex) skip it. */
void pbullet_arm(PBullet *b, bool scaled);
#define PB_BYPRODUCT (PB_FRAGMENT | PB_MINE | PB_VORTEX)
int weapon_mod_by_name(const char *name); /* --weapon NAME: UpgradeId of that weapon mod, or -1 */
const char *weapon_option_name(int u);    /* lowercase name of a weapon mod */
int gun_main_mod(void);                  /* that weapon mod (UpgradeId), -1 for the plain blaster */
bool sword_on(void);
V2 weapon_drone_pos(int i);                     /* the sword replaces the gun */
float gun_volley_shots(void);            /* projectiles per volley right now (HUD, tests) */
float sword_reach(void);
float gun_reach(void); /* px a main gun shot flies */
#define SWORD_INNER 18.0f /* the blade starts this far from the ship's centre */

/* bullets.c */
PBullet *pbullet_spawn(V2 pos, V2 vel, float radius, float damage, SDL_FColor col);
EBullet *ebullet_spawn(V2 pos, float angle, float speed, float radius, SDL_FColor col);
void bullets_update(void);
void bullets_draw_player(void);
void bullets_draw_enemy(void);
void ebullets_clear(bool with_fx);

/* enemies.c */
void enemy_queue_spawn(EnemyType type, V2 pos, float delay);
int enemy_by_name(const char *name);
/* Cannot be hit right now (WRAITH faded out, LEAPER in the air): shots fly through, no damage. */
bool enemy_untouchable(const Enemy *e);
/* MIRROR: a shot coming from this point hits its front plate (absorbed, no damage). */
bool enemy_blocks_shot(const Enemy *e, V2 from);
/* Lob a grenade (EB_GRENADE) from -> to; it bursts into a ring of n bullets on landing. */
EBullet *ebullet_lob(V2 from, V2 to, float flight, int n, SDL_FColor col);
void enemies_update(void);
void enemies_draw(void);
void enemy_damage(Enemy *e, float dmg, V2 knock);
float enemy_damage_cap(const Enemy *e, float dmg); /* the boss: 0 while invulnerable, stops at each phase threshold */
float boss_phase_threshold(int phase);             /* hp fraction where phase `phase` starts */
const Enemy *boss_alive(void);
bool is_boss_depth(int depth);
/* Shared helpers for bosses.c. */
void enemy_steer(Enemy *e, V2 desired_vel, float responsiveness);
void enemy_fire_ring(V2 pos, int n, float speed, float radius, float offset, SDL_FColor col);
void enemy_fire_fan(V2 pos, float angle, int n, float spread, float speed, float radius, SDL_FColor col);
V2 enemy_nav_dir(Enemy *e, V2 goal);
void enemy_wander(Enemy *e, float dmin, float dmax, float speed);
V2 enemy_random_point(V2 center, float dmin, float dmax);
bool enemy_blocked_ahead(const Enemy *e, V2 dir, float speed, V2 *n);
Enemy *enemy_spawn_now(EnemyType type, V2 pos); /* at once, no telegraph (NULL when full) */
/* enemies2.c: the 60 newer types (EN_DRIFTER .. EN_TURRET). */
void enemy2_update(Enemy *e, float aim, float dist, V2 dir);
bool enemy2_untouchable(const Enemy *e);
float enemy2_damage(Enemy *e, float dmg, V2 knock); /* front armour, bubbles, parries...: what gets through */
void enemy2_on_death(Enemy *e);
void enemy2_draw_glow(const Enemy *e);              /* additive pass: telegraphs, beams, auras */
void enemy2_draw(const Enemy *e, V2 at, float rad, SDL_FColor c, SDL_FColor fill); /* body and details */
float enemy2_alpha(const Enemy *e);                 /* see-through (stalker, mole underground...) */
float enemy2_mass(const Enemy *e);                  /* knockback divider on top of the size */
bool enemy_resistant(const Enemy *e);
float enemy_base_hp(const Enemy *e); /* max hp: its type's, or the fixed one of its form (body segment, hydra head, crystal's growth, illusion) */               /* shots barely hurt it right now (the bot aims elsewhere) */
/* Beams, tethers, orbs, strikes about to land...: the same as the bosses' (the bot keeps off them,
 * the live ones hurt the ship, enemies2_hazards_hurt). */
int enemy_hazards(BossHazard *out, int max);
void enemies2_hazards_hurt(void);
void enemies2_tick(void);                           /* once per tick before the enemies: shared state */
/* bosses.c */
EnemyType boss_for_depth(int depth);      /* 10: one of four, 20 / 30 / 40: their own, beyond: any */
const char *boss_title(EnemyType t);      /* "THE OVERSEER"... */
V2 boss_spawn_point(EnemyType t);
void boss_update(Enemy *e, float aim);
void boss_draw_under(const Enemy *e);     /* telegraphs, hazards, body (before every enemy) */
void boss_draw(const Enemy *e);           /* details over its shape */
bool boss_draws_body(const Enemy *e);     /* its own shape instead of the generic polygon */
bool boss_armor_hit(V2 pos, float r);     /* a player shot there is stopped (WYRM body, CARRIER plates) */
bool boss_armor_line(V2 a, V2 b, float r); /* the WYRM's body lies across a-b (bot: line of fire) */
int boss_hazards(BossHazard *out, int max);
bool boss_cover_spot(V2 from, V2 *out);   /* LIGHTHOUSE beams: a spot hidden from them (bot) */
void boss_on_death(Enemy *e);
extern float g_wave_time_override;        /* tests (--wave-time S): every wave's timer that long, 0 = normal */
extern int g_boss_override;               /* dev / tests (--boss-kind, debug menu): -1 random, else the EnemyType */
/* A few boss states the tests set (bosses.c has the full lists, checked against these). */
enum { BOSS_STATE_REST = 0, LH_STATE_WARN = 1, WY_STATE_BURROW = 1, FG_STATE_JUMP = 1, SG_STATE_REVERSE = 5 };
Enemy *enemy_nearest(V2 pos, float max_dist);
EnemyType enemy_hatchling(int depth); /* the small enemy a nest lets out (one of the --enemy ones if set) */

/* pickups.c */
void pickup_spawn(PickupType type, int sub, V2 pos, V2 vel);
void pickups_update(void);
void pickups_draw(void);
#define INTERACT_R 80.0f          /* E reaches an item this close */
#define INTERACT_R_MACHINE 100.0f /* ...or a machine */
void interact_target(int *pk, int *mc); /* what E would act on (-1: none) */
void interact_draw(void);
void reward_drop(RewardType r, V2 pos);
void reward_drop_n(RewardType r, V2 pos, int n); /* n coins / keys / bombs / hearts (0: rolled now) */
int reward_roll_count(RewardType r, uint16_t mods); /* what a coin / key / bomb / heart room with these modifiers gives (0: another reward) */
bool special_room_empty(RoomType t);  /* the machine room has nothing for this ship: it drops coins instead */
void doors_roll_counts(void);         /* fills Door.count of the room's doors (luck and upgrades as they are now) */
/* Secret room loot: 1 item (rarely 2) plus coins / bombs / keys. */
void secret_reward_drop(V2 center, bool items);
/* Spawn a pedestal that belongs to the room's one-of-N choice. */
void pickup_spawn_choice(PickupType type, int sub, V2 pos);
void draw_pickup_icon(PickupType type, int sub, V2 pos, float s, float t);
void draw_heart(V2 p, float s, SDL_FColor col, bool left_half_only);
/* Centered text; a '|' in desc splits it over two lines. */
void draw_desc(float cx, float y, float scale, SDL_FColor col, const char *desc);
PickupType reward_icon(RewardType r);
const char *reward_name(RewardType r);
/* Label shown on a door: STATS, SHOP or the reward name. */
const char *door_label(const Door *d);
int shop_price(PickupType type, int sub);
/* An upgrade not owned and not already on the floor (weapon mods first if asked), -1: none left. */
int upgrade_fresh(void); /* an upgrade not owned nor already on offer (floor, slot machine), -1: none */
/* Pickups / prizes showing an upgrade the player now owns get another one (or a stat trade). */
void pickups_refresh_owned(void);

/* fx.c */
void fx_particle(V2 pos, V2 vel, float life, float size, SDL_FColor col, float drag, int shape);
void fx_shard(V2 pos, V2 vel, float life, float size, SDL_FColor col, float spin);
void fx_burst(V2 pos, int n, float smin, float smax, float life, float size, SDL_FColor col);
void fx_explosion(V2 pos, float radius, SDL_FColor col);
void fx_shake(float amount);
void fx_popup(V2 pos, SDL_FColor col, const char *fmt, ...);
void fx_banner(const char *title, const char *sub, SDL_FColor col, float duration);
void fx_timers(void);
void fx_update(void);
void fx_draw(void);
void fx_draw_popups(void);
void fx_clear(void);

/* debug_menu.c - F3 dev menu (teleport to any room type, cheats) */
void debug_menu_open(int page); /* 0 = cheats (F3), 1 = teleport (F4) */
int debug_menu_page(void);
unsigned debug_menu_mods(void); /* the modifiers ticked on the F4 page */
void debug_menu_set_page(int page);
void debug_menu_close(void);
void debug_menu_update(void);
void debug_menu_draw(void);
/* Dev only: jump straight into a room. RW_NONE as a combat reward rolls one at random.
 * Without enemies the room starts cleared with its reward already out - the one sanctioned
 * exception to the "every room starts full of enemies" rule, never reachable in normal play. */
void debug_teleport(RoomType type, RewardType reward, int depth, bool with_enemies);
void debug_teleport_mod(RoomType type, RewardType reward, int depth, bool with_enemies, unsigned mods); /* with a MOD_BIT set */

/* workshop.c - what a workshop (stat trades), a casino (slot machines + supply press) and a Baphomet room (pacts:
 * augment pads) offer once cleared, and their machines */
#define CASINO_MACHINES 3
void workshop_spawn(void);
/* The six rooms of special.c, each full of enemies first like any other; once cleared (room_reveal) they spawn their machines. */
void trader_spawn(void);    /* up to TRADER_OFFERS upgrades for sale, each paid with TRADE_COST upgrades of the ship's (random, never twice the same) */
void purifier_spawn(void);  /* up to PURIFY_OFFERS owned upgrades whose malus can be bought off with coins */
void vault_spawn(void);     /* with the enemies: a safe in the middle of the floor, to break while they attack (the faster the more coins, paid after the fight) */
void vault_on_clear(void);  /* the last enemy is dead: a safe still shut vanishes */
void vault_reveal(void);    /* the room has shattered: the coins the safe paid come out */
void forge_spawn(void);     /* up to FORGE_OFFERS pairs of owned upgrades to fuse (one of them) */
void keys_room_spawn(void); /* a locked gate (a key) and an upgrade behind it */
void infirmary_spawn(void); /* a full heal, or a heart container for coins (one of the two) */
bool room_type_machines(RoomType t);    /* the rooms whose reward is machines (casino and the six above) */
int room_type_by_name(const char *name); /* --room NAME: trader, purifier, vault, forge, keys, infirmary (-1: none) */
const char *room_type_name(RoomType t); /* door label / HUD: "TRADER"... (special rooms only, "" otherwise) */
bool upgrade_tradable(int u);           /* owned for real, not the ship's own weapon, not part of a fusion */
bool malus_removable(int u);            /* its malus is not built into its effect (MAL_CUSTOM) */
bool upgrade_in_fusion(int u);
void upgrade_remove(int u);             /* the trader takes it back (never the one-time effects it already gave) */
void machine_damage(int i, float dmg);  /* the safe takes a hit (shots, the blade) */
bool machine_shot_hit(PBullet *b);      /* a player shot against the safe: true if it stopped there */
bool machine_in_reach(int i, V2 p, float r); /* a point within r of the machine (the safe's body) */
V2 machine_safe_pos(void);
void fused_draw_texts(float cx, float y, float scale, int u); /* a fused upgrade in full: its bonus, its partners' bonuses, its malus */
void casino_spawn(void);
void machine_roll_prize(Machine *m);    /* an upgrade not owned, a stat trade once every upgrade is owned, coins if even that is impossible */
void special_refresh_prize(void);       /* a prize the ship now owns is swapped for a fresh one */
void special_tick(int i);               /* a machine of the six rooms, each tick */
void special_use(Machine *m);           /* E on it */
void special_draw(const Machine *m);
const char *machine_used_text(const Machine *m); /* what it says once used up */
void baphomet_spawn(void);
void baphomet_draw_floor(void); /* the pentagram under the pacts */
void workshop_refresh_prize(void); /* a prize the player now owns gets replaced */
void workshop_update(void);
void workshop_draw(void);
float gamble_win_chance(void);

/* hud.c */
void hud_draw(void);
void overlay_draw(void);

/* upgrade_menu.c - TAB: every upgrade found, each one can be turned off and back on */
void upgrade_menu_open(void);
void upgrade_menu_update(void);
void upgrade_menu_draw(void);
int upgrade_menu_list(int *out);         /* owned upgrades, in menu order */

#endif
