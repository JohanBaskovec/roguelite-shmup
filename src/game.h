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
    MAX_OBSTACLES = 24,
    MAX_POPUPS = 48,
    MAX_DRONES = 6,
    MAX_DOORS = 3,
    MAX_WEAPON_LEVEL = 5,
};

#define PLAYER_BODY_R 14.0f /* collision with walls / pickups */
#define PLAYER_HIT_R 4.0f   /* tiny shmup hitbox for bullets */
#define DOOR_HALF 60.0f
#define TRANS_TIME 0.34f

/* ---- Weapons & upgrades -------------------------------------------------- */

/* WPN_SWORD is the Knight's melee weapon: never offered to the other characters (weapon_available). */
typedef enum { WPN_BLASTER, WPN_SCATTER, WPN_NEEDLER, WPN_SEEKER, WPN_BOUNCER, WPN_SWORD, WPN_COUNT } WeaponId;

typedef struct {
    const char *name;
    const char *desc;
    float cooldown;      /* seconds between volleys at level 1 */
    int count;           /* projectiles per volley at level 1 */
    int count_per_level;
    float spread;        /* base fan angle (radians) */
    float spread_per;    /* extra fan angle per additional projectile */
    float jitter;        /* random angle noise */
    float parallel;      /* lateral spacing between projectiles */
    float damage;
    float speed, speed_var;
    float life;
    float radius;
    int pierce, bounce;
    float homing;        /* turn rate (rad/s) */
    float knockback;
    uint32_t color;
    float level_rate;    /* cooldown multiplier per level */
} WeaponDef;

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
    /* big slow shots that reflect enemy bullets; bonuses while hurt; luck; weapon rooms */
    UPG_DEFLECTOR, UPG_WOUNDED_FURY, UPG_SCAR_TISSUE, UPG_LAST_STAND, UPG_BLOOD_SCENT, UPG_LOADED_DICE,
    UPG_SIGNATURE,
    /* workshop exclusives (UpgradeDef.tier = max hearts paid), never offered anywhere else */
    UPG_X_BLOOD_ROUNDS, UPG_X_OVERDRIVE, UPG_X_HELLFIRE, UPG_X_STORM, UPG_X_DEMON, UPG_X_GOD_CANNON,
    UPG_COUNT
} UpgradeId;

/* Active items: one slot, used with SPACE / SHIFT / right click, then recharges. None at the start:
 * they come from active rooms (RW_ACTIVE, choice of 3). */
typedef enum { ACT_NONE, ACT_DASH, ACT_SHIELD, ACT_BOOM, ACT_STRENGTH, ACT_CHAOS, ACT_COUNT } ActiveId;

/* Numbers per level (duration, power, recharge) live in actives.c: active_duration / active_desc... */
typedef struct {
    const char *name;
    uint32_t color;
    float cooldown;       /* seconds at level 1, scaled by the RECHARGE stat (Player.dash_cooldown / DASH_BASE_CD) */
} ActiveDef;

extern const ActiveDef ACTIVES[ACT_COUNT];

#define DASH_BASE_CD 0.6f    /* Player.dash_cooldown at the start: the dash's own recharge */
#define MAX_ACTIVE_LEVEL 5   /* taking the active item held again levels it up, like weapons */

typedef struct {
    const char *name;
    const char *desc;
    uint32_t color;
    int max;        /* how many times it can be taken (0 = unlimited) */
    int tier;       /* > 0: workshop exclusive, costs this many heart containers */
} UpgradeDef;

extern const WeaponDef WEAPONS[WPN_COUNT];
extern const UpgradeDef UPGRADES[UPG_COUNT];

/* ---- Bullets ------------------------------------------------------------- */

typedef struct {
    V2 pos, vel;
    float radius, damage, life, homing, knockback;
    int pierce, bounces;
    int last_hit;   /* uid of the last enemy hit, avoids multi-hits while piercing */
    int weapon;     /* WeaponId, or -1 for drones */
    int flags;      /* PB_* */
    float age;
    SDL_FColor col;
} PBullet;

enum { PB_FRAGMENT = 1, PB_CRIT = 2, PB_RETURNED = 4, PB_MINE = 8, PB_CHARGED = 16, PB_REFLECTED = 32 };

typedef struct {
    V2 pos, vel;
    float radius, life, age;
    float accel;    /* px/s^2 along velocity */
    float curve;    /* rad/s */
    SDL_FColor col;
} EBullet;

/* ---- Enemies ------------------------------------------------------------- */

typedef enum { EN_CHASER, EN_SWARM, EN_SHOOTER, EN_BURSTER, EN_SPINNER, EN_SNIPER, EN_TANK, EN_WARDEN, EN_HUNTER, EN_HIVE, EN_COUNT } EnemyType;

typedef struct {
    const char *name;
    float radius, hp, speed;
    int cost;       /* wave budget cost */
    int min_depth;  /* first room depth where it can appear */
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
    int sword_swing;              /* id of the last sword swing that hit it (one hit per swing) */
} Enemy;

typedef struct {
    EnemyType type;
    V2 pos;
    float t, max_t;
} Spawn;

/* ---- FX ------------------------------------------------------------------ */

enum { PT_GLOW, PT_SPARK, PT_RING };

typedef struct {
    V2 pos, vel;
    float life, max_life, size, drag;
    SDL_FColor col;
    uint8_t shape;
} Particle;

typedef struct {
    V2 pos;
    float t;
    char text[48];
    SDL_FColor col;
} Popup;

/* ---- Pickups ------------------------------------------------------------- */

/* PK_ACTIVE: an active item (sub = ActiveId), replaces the one held. */
/* PK_AUGMENT: workshop pad, a tier-N exclusive upgrade paid with N heart containers. */
typedef enum { PK_COIN, PK_KEY, PK_BOMB, PK_HEART, PK_WEAPON, PK_UPGRADE, PK_STAT, PK_AUGMENT, PK_ACTIVE } PickupType;

typedef struct {
    PickupType type;
    int sub;        /* coin value, WeaponId, UpgradeId or encoded stat offer */
    int price;      /* shop price in coins, 0 = free */
    bool choice;    /* one-of-N pick: taking it makes the other choice pickups vanish */
    V2 pos, vel;
    float t;
} Pickup;

/* ---- Workshop machines ---------------------------------------------------- */

typedef enum { MC_SUPPLY, MC_GAMBLE } MachineType;

typedef struct {
    MachineType type;
    V2 pos;
    int uses;          /* SUPPLY: payouts left before it breaks */
    bool broken;       /* SUPPLY: used up; GAMBLE: prize already won */
    bool inside;       /* the player stood on it last tick (a use needs a fresh step in) */
    PickupType prize;  /* GAMBLE: PK_WEAPON or PK_UPGRADE */
    int prize_sub;
    float anim, spin;  /* feedback timers; GAMBLE: spin > 0 while the reels turn */
    int plays;
} Machine;

#define MAX_MACHINES 4
#define GAMBLE_COST 5
#define SUPPLY_USES_MIN 3
#define SUPPLY_USES_MAX 5

/* ---- Rooms --------------------------------------------------------------- */

typedef enum { ROOM_START, ROOM_COMBAT, ROOM_STATS, ROOM_TREASURE, ROOM_SHOP, ROOM_BOSS, ROOM_SECRET, ROOM_WORKSHOP } RoomType;
typedef enum { RW_NONE, RW_COINS, RW_KEY, RW_BOMBS, RW_HEART, RW_WEAPON, RW_UPGRADE, RW_ACTIVE, RW_COUNT } RewardType;
typedef enum { SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM } Side;

typedef struct {
    Side side;
    RoomType type;       /* room behind the door */
    RewardType reward;   /* reward granted when that room is cleared */
    bool locked;
    V2 pos;
    float anim;
} Door;

typedef struct {
    RoomType type;
    RewardType reward;
    int depth;
    SDL_FRect obs[MAX_OBSTACLES];
    int nobs;
    Door doors[MAX_DOORS];
    int ndoors;
    int wave, nwaves, last_wave_size;
    float wave_t, since_wave;
    bool cleared;
    float clear_t;
    float msg_cd;
    float hue;
    int id;              /* unique per room_enter; kept when returning from a secret room */
    bool revisit;        /* restored after leaving its secret room (the only re-entered room) */
    /* Secret room: a corner flashes faintly during combat; a bomb there opens the bottom door. */
    int secret_corner;   /* -1 = none, else 0..3 (top-left, top-right, bottom-right, bottom-left) */
    bool secret_open, secret_used;
    float secret_flash_t, secret_flash_cd;
    int secret_flashes;  /* how many times it flashed (the bot only "knows" after seeing one) */
    bool peaceful;       /* starts without enemies: launch bay, secret room without an ambush */
    bool secret_shop;    /* secret room variant: a shop instead of the bonus item */
} Room;

/* Pseudo door indices for game_start_transition(). */
#define DOOR_SECRET (-1) /* bottom door into the secret room */
#define DOOR_RETURN (-2) /* secret room exit, back to the room it was found in */
#define SECRET_CHANCE 0.15f
#define SECRET_BOMB_RADIUS 220.0f

/* ---- Player -------------------------------------------------------------- */

typedef struct {
    V2 pos, vel;
    float aim_angle;
    int hp, max_hp;             /* in half-hearts */
    float invuln;
    float fire_cd;
    float dash_t, dash_cooldown; /* dash_cooldown: the RECHARGE stat (dash recharge, scales every active) */
    V2 dash_dir;
    int active;                 /* ActiveId held (ACT_NONE at the start) */
    int act_level;              /* 1..MAX_ACTIVE_LEVEL (0 without an active item) */
    float act_cd, act_cd_max;   /* recharge left / total of the last use */
    float act_t;                /* effect time left (shield, strength, chaos) */
    bool chaos[UPG_COUNT];      /* CHAOS: temporary upgrades (only ones not owned), removed when it ends */
    float speed;
    int coins, keys, bombs;
    int weapon;
    int wlevel[WPN_COUNT];      /* 0 = not owned */
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
    int weapon;           /* starting weapon */
} CharDef;

extern const CharDef CHARACTERS[CHAR_COUNT];

#define MAX_SHIELD 12 /* half-hearts: 6 shields */
#define SPREAD_MIN 0.2f /* bounds of Player.spread_mult (trades, lenses) */
#define SPREAD_MAX 3.0f

/* ---- Game ---------------------------------------------------------------- */

typedef enum { GS_TITLE, GS_CHAR_SELECT, GS_PLAYING, GS_PAUSED, GS_GAMEOVER, GS_DEBUG } GameState;

typedef struct {
    bool up, down, left, right, fire;
    /* edge-triggered, cleared after each simulation tick */
    bool active_pressed, bomb_pressed, pause_pressed, restart_pressed, confirm_pressed, debug_pressed;
    bool char_pressed; /* C: back to the character select screen */
    int weapon_select; /* -1 = none */
    int weapon_cycle;
    V2 mouse;          /* logical coordinates */
} Input;

typedef struct {
    GameState state;
    Rng rng;
    Input in;
    Player pl;
    Room room;

    PBullet pb[MAX_PBULLETS]; int npb;
    EBullet eb[MAX_EBULLETS]; int neb;
    Enemy en[MAX_ENEMIES]; int nen;
    Spawn sp[MAX_SPAWNS]; int nsp;
    Particle pt[MAX_PARTICLES]; int npt;
    Pickup pk[MAX_PICKUPS]; int npk;
    Popup pop[MAX_POPUPS]; int npop;
    Machine mc[MAX_MACHINES]; int nmc; /* workshop machines (only once a workshop is cleared) */
    int next_uid;

    float time, run_time;
    int ticks;
    float shake, hitstop, hurt_flash, bomb_flash;
    bool transitioning;
    float trans_t;
    int trans_door;
    char banner[64], banner_sub[96];
    float banner_t, banner_max;
    SDL_FColor banner_col;
    int rooms_cleared, kills;
    int items_bought; /* shop purchases this run */
    int active_uses;  /* active items used this run */
    int machine_uses, augments; /* workshop: supply press / slot machine uses, augments taken */
    int room_counter; /* source of Room.id */
    Room parent;      /* room the current secret room was found in, restored on exit */
    Pickup parent_pk[MAX_PICKUPS]; int parent_npk;
    bool force_secret;   /* dev: every eligible room hides a secret */
    bool reveal_secrets; /* dev: draw secret corners permanently */
    int secret_shop_override, secret_ambush_override; /* dev (debug menu / tests): -1 random, 0 no, 1 yes */
    float gameover_t;
    float last_enemy_damage_t; /* G.time of the last hit on an enemy (stall detection in tests) */

    bool quit, show_debug, god;
    bool show_stats; /* F2: semi-transparent stats overlay (hidden by default) */
    int character;   /* CharId of the current / next run (kept across restarts) */
    int char_hover;  /* character select screen: highlighted card */
    bool sel_left, sel_right; /* character select: previous held state, for edge detection */
    V2 sel_mouse;
    GameState debug_prev; /* state to return to when the debug menu closes */
    float fps;
} Game;

extern Game G;

static inline float frand(float lo, float hi) { return rng_range(&G.rng, lo, hi); }
static inline int irand(int lo, int hi) { return rng_int(&G.rng, lo, hi); }
static inline bool chance(float p) { return rng_chance(&G.rng, p); }

/* ---- Stats (trades offered in Stats rooms) ------------------------------ */

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

#define STATS_ROOM_OFFERS 4
#define SHOP_ITEMS 5 /* a shop always sells exactly this many items */
#define REWARD_CHOICES 3 /* weapon / upgrade rooms always offer this many items to pick from */
#define COLOR_STATS 0x4dffd2
#define COLOR_SHOP 0xff6bd6
#define COLOR_SECRET 0xffb347
#define COLOR_WORKSHOP 0xc8ff4d
static inline int stat_offer_encode(int up, int down) { return up * ST_COUNT + down; }
static inline int stat_offer_up(int offer) { return offer / ST_COUNT; }
static inline int stat_offer_down(int offer) { return offer % ST_COUNT; }

/* upgrades.c - passive upgrades and their effects (hooks called by the systems they modify) */
int upgrade_roll(void);
void upgrade_apply(int u);
bool upgrade_capped(int u);
void player_add_shield(int half_hearts);
float upg_volley_damage_mult(void);   /* BLOODLUST, WOUNDED FURY, LAST STAND */
float upg_fire_rate_mult(void);       /* ADRENALINE, SCAR TISSUE, DEFLECTOR */
float upg_move_speed_mult(void);      /* ADRENALINE, LAST STAND */
float upg_shot_speed_mult(void);      /* DEFLECTOR */
/* Description with the exact numbers (chances...) once the upgrade has n stacks. */
const char *upgrade_desc_at(int u, int n);
/* What taking it now would give (current stacks + 1). */
const char *upgrade_desc(int u);
/* Workshop: a random exclusive upgrade of this tier not owned yet, or -1. */
int upgrade_roll_exclusive(int tier);
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
void game_start_transition(int door);

/* room.c */
void room_enter(RoomType type, RewardType reward, int depth);
void room_update(void);
void room_draw_floor(void);
void room_draw_walls(void);
bool room_collide_circle(V2 *pos, float r, V2 *normal);
/* True when a circle of radius margin can travel from a to b without touching an obstacle. */
bool room_line_clear(V2 a, V2 b, float margin);
/* Next point to walk towards to reach goal, going around obstacles. */
V2 room_navigate(V2 from, V2 goal, float margin);
V2 door_position(Side side);
RewardType room_roll_reward(void);
V2 room_secret_spot(int corner);
/* The launch bay and secret rooms are the exceptions to "every room starts full of enemies". */
static inline bool room_type_peaceful(RoomType t) { return t == ROOM_START || t == ROOM_SECRET; }
#define SECRET_SHOP_CHANCE 0.10f   /* a secret room holds a shop instead of its bonus item */
#define SECRET_AMBUSH_CHANCE 0.10f /* a secret room is guarded by enemies before its loot */
/* A bomb went off at pos: opens the secret passage if pos is near the secret corner. */
void room_bomb_at(V2 pos);
void room_enter_secret(void);
void room_return_from_secret(void);
/* Dev only (debug menu): kill the pending waves and clear the room at once. */
void room_debug_clear(void);

/* player.c */
void player_init(void);
void player_update(void);
void player_draw(void);
void player_hurt(int dmg);
/* Ship of a character, also used by the character select screen. */
void draw_ship(int character, V2 pos, float angle, float scale, bool blink);

/* actives.c */
void actives_update(void);            /* input, recharge, effects (from player_update) */
void actives_draw(void);              /* shield bubble, strength / chaos auras */
void active_give(int a);
bool active_strength_on(void);        /* STRENGTH: x4 shots */
bool active_shield_on(void);          /* SHIELD: invincible */
float active_cooldown(int a, int level); /* with the RECHARGE stat */
float active_duration(int a, int level); /* effect time (dash: invulnerable dash time; 0 = instant) */
int active_chaos_count(int level);       /* CHAOS: upgrades lent */
const char *active_desc(int a, int level); /* '|' splits it over two lines */
void active_end_chaos(void);
const char *active_option_name(int a); /* lowercase name for --active */
void draw_active_icon(int a, V2 pos, float s, float t);
/* Active room: REWARD_CHOICES distinct actives; the one held only while it can still level up. */
void active_spawn_choices(V2 center);
#define COLOR_ACTIVE 0xffe94d

/* characters.c */
void character_apply(void); /* starting stats / weapon of G.character, after player_init's defaults */
const char *character_option_name(int c); /* lowercase name for --char */
void char_select_open(void);
void char_select_update(void);
void char_select_draw(void);

/* weapons.c */
void weapons_update(void);
void drones_draw(void);
void sword_draw(void);
void weapon_give(int w);
int weapon_roll(void);
/* The sword only exists for the Knight. */
bool weapon_available(int w);
float sword_reach(void);
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
void enemies_update(void);
void enemies_draw(void);
void enemy_damage(Enemy *e, float dmg, V2 knock);
Enemy *enemy_nearest(V2 pos, float max_dist);

/* pickups.c */
void pickup_spawn(PickupType type, int sub, V2 pos, V2 vel);
void pickups_update(void);
void pickups_draw(void);
void reward_drop(RewardType r, V2 pos);
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

/* fx.c */
void fx_particle(V2 pos, V2 vel, float life, float size, SDL_FColor col, float drag, int shape);
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
void debug_menu_open(void);
void debug_menu_close(void);
void debug_menu_update(void);
void debug_menu_draw(void);
/* Dev only: jump straight into a room. RW_NONE as a combat reward rolls one at random.
 * Without enemies the room starts cleared with its reward already out - the one sanctioned
 * exception to the "every room starts full of enemies" rule, never reachable in normal play. */
void debug_teleport(RoomType type, RewardType reward, int depth, bool with_enemies);

/* workshop.c - machines that appear once a workshop is cleared */
#define WORKSHOP_MACHINE_X 470.0f
#define WORKSHOP_MACHINE_DY 250.0f
V2 workshop_machine_spot(int i); /* 0 = supply press, 1 = slot machine */
void workshop_spawn(void);
void workshop_update(void);
void workshop_draw(void);
float gamble_win_chance(void);

/* hud.c */
void hud_draw(void);
void overlay_draw(void);

#endif
