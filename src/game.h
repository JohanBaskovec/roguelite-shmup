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

typedef enum { WPN_BLASTER, WPN_SCATTER, WPN_NEEDLER, WPN_SEEKER, WPN_BOUNCER, WPN_COUNT } WeaponId;

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
    UPG_THRUSTER, UPG_HULL, UPG_DRONE, UPG_MAGNET, UPG_VELOCITY, UPG_COUNT
} UpgradeId;

typedef struct {
    const char *name;
    const char *desc;
    uint32_t color;
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
    SDL_FColor col;
} PBullet;

typedef struct {
    V2 pos, vel;
    float radius, life, age;
    float accel;    /* px/s^2 along velocity */
    float curve;    /* rad/s */
    SDL_FColor col;
} EBullet;

/* ---- Enemies ------------------------------------------------------------- */

typedef enum { EN_CHASER, EN_SWARM, EN_SHOOTER, EN_BURSTER, EN_SPINNER, EN_SNIPER, EN_TANK, EN_COUNT } EnemyType;

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

typedef enum { PK_COIN, PK_KEY, PK_BOMB, PK_HEART, PK_WEAPON, PK_UPGRADE, PK_STAT } PickupType;

typedef struct {
    PickupType type;
    int sub;        /* coin value, WeaponId, UpgradeId or encoded stat offer */
    V2 pos, vel;
    float t;
} Pickup;

/* ---- Rooms --------------------------------------------------------------- */

typedef enum { ROOM_START, ROOM_COMBAT, ROOM_STATS, ROOM_TREASURE, ROOM_SHOP, ROOM_BOSS } RoomType;
typedef enum { RW_NONE, RW_COINS, RW_KEY, RW_BOMBS, RW_HEART, RW_WEAPON, RW_UPGRADE, RW_COUNT } RewardType;
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
} Room;

/* ---- Player -------------------------------------------------------------- */

typedef struct {
    V2 pos, vel;
    float aim_angle;
    int hp, max_hp;             /* in half-hearts */
    float invuln;
    float fire_cd;
    float dash_cd, dash_t, dash_cooldown;
    V2 dash_dir;
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
    int luck;                   /* drop chance modifier */
    int upg_count[UPG_COUNT];
} Player;

/* ---- Game ---------------------------------------------------------------- */

typedef enum { GS_TITLE, GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GameState;

typedef struct {
    bool up, down, left, right, fire;
    /* edge-triggered, cleared after each simulation tick */
    bool dash_pressed, bomb_pressed, pause_pressed, restart_pressed, confirm_pressed;
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
    float gameover_t;
    float last_enemy_damage_t; /* G.time of the last hit on an enemy (stall detection in tests) */

    bool quit, show_debug, god;
    float fps;
} Game;

extern Game G;

static inline float frand(float lo, float hi) { return rng_range(&G.rng, lo, hi); }
static inline int irand(int lo, int hi) { return rng_int(&G.rng, lo, hi); }
static inline bool chance(float p) { return rng_chance(&G.rng, p); }

/* ---- Stats (trades offered in Stats rooms) ------------------------------ */

typedef enum {
    ST_DAMAGE, ST_FIRE_RATE, ST_RANGE, ST_MAX_HP, ST_MOVE_SPEED, ST_DASH,
    ST_SHOT_SPEED, ST_HITBOX, ST_LUCK, ST_COUNT
} StatId;

typedef struct {
    const char *name;
    const char *up_text;
    const char *down_text;
} StatDef;

extern const StatDef STATS[ST_COUNT];

#define STATS_ROOM_OFFERS 4
static inline int stat_offer_encode(int up, int down) { return up * ST_COUNT + down; }
static inline int stat_offer_up(int offer) { return offer / ST_COUNT; }
static inline int stat_offer_down(int offer) { return offer % ST_COUNT; }

/* stats.c */
bool stat_can_change(int s, bool up);
void stat_apply(int s, bool up);
int stats_roll_offers(int *out, int max);
void stat_offer_take(int offer);
float luck_drop_factor(void);

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

/* player.c */
void player_init(void);
void player_update(void);
void player_draw(void);
void player_hurt(int dmg);

/* weapons.c */
void weapons_update(void);
void drones_draw(void);
void weapon_give(int w);
int weapon_roll(void);
int upgrade_roll(void);
void upgrade_apply(int u);

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
float enemy_hp_scale(void);

/* pickups.c */
void pickup_spawn(PickupType type, int sub, V2 pos, V2 vel);
void pickups_update(void);
void pickups_draw(void);
void reward_drop(RewardType r, V2 pos);
void draw_pickup_icon(PickupType type, int sub, V2 pos, float s, float t);
void draw_heart(V2 p, float s, SDL_FColor col, bool left_half_only);
PickupType reward_icon(RewardType r);
const char *reward_name(RewardType r);

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

/* hud.c */
void hud_draw(void);
void overlay_draw(void);

#endif
