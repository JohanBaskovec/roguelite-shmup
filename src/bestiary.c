#include "audio.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * Bestiary (a save's menu): every enemy and boss, in the order they show up in a run, greyed
 * until met (Profile.seen, marked by bestiary_saw when one is created, written at once). The one
 * selected plays in a small screen on the right, with what it does.
 *
 * The small screen is a world of its own, so the enemy really plays (its own update, bullets,
 * particles) against a ship flying round on its own and firing plain shots (out of reach:
 * invulnerable). While the bestiary is open, G's world (room, entities, ship, generator...) IS
 * that small world: the real one (the menu's demo, paused meanwhile, not drawn) is copied aside
 * when it opens and put back when it closes (bestiary_close), the menu's own fields (input, page,
 * profile) going on untouched. It is flagged as a demo (nothing counts for the save) and plays
 * without sound. The path finder's caches (room.c) are rebuilt for the real world on closing.
 */

static const char *const DESC[EN_COUNT] = {
    [EN_CHASER] = "Rushes straight at the ship, weaving a little.|Weak alone, dangerous in packs.",
    [EN_SWARM] = "Tiny and fast, comes in flocks that keep apart|from each other. One shot each.",
    [EN_SHOOTER] = "Keeps its distance and fires bursts of|three small fans at the ship.",
    [EN_BURSTER] = "Drifts about and blows out rings of bullets,|every other one doubled.",
    [EN_SPINNER] = "Wanders round the middle spinning a three-arm|spiral, pauses, then turns the other way.",
    [EN_SNIPER] = "Keeps far away. A line locks on the ship|just before a fast volley along it.",
    [EN_TANK] = "Heavy and slow: closes in to blast a shotgun|spray, and now and then a big ring.",
    [EN_WARDEN] = "A slow fortress: a turning cross of bullet|streams, with aimed fans in between.",
    [EN_HUNTER] = "Stalks at range, flashes a line, then dashes|right through where the ship stands.",
    [EN_HIVE] = "Breeds swarms over its life and pulses slow|rings. Kill it before the flock grows.",
    [EN_RAMMER] = "Lines up and paws the ground, then charges until|it hits a wall - and sits there stunned.",
    [EN_GRENADIER] = "Keeps away and lobs grenades where the ship is|heading. They burst into rings on landing.",
    [EN_CRAWLER] = "Glued to the arena's wall, it scuttles along it|to face the ship and spits short bursts inwards.",
    [EN_TOP] = "A spinning top: bounces off everything, faster|and faster, flinging bullets - then gets dizzy.",
    [EN_KAMIKAZE] = "Rushes in, stops next to the ship, swells up|and blows into a ring of bullets.",
    [EN_DODGER] = "Strafes at mid range and sidesteps shots aimed|at it: a spread or a second volley gets it.",
    [EN_ROOK] = "Moves like the chess piece: slides into the ship's|row or column, then fires a cross of lines.",
    [EN_SPLITTER] = "A slow wobbly blob spitting weaving bullets.|Killing it splits it in three.",
    [EN_LEAPER] = "Crouches, then jumps onto the ship (it cannot be|hit in the air) and lands with a shockwave.",
    [EN_MINELAYER] = "Roams the room sowing mines. They blow into rings|when the ship comes close or their fuse ends.",
    [EN_BOOMERANG] = "Throws a fan of three boomerangs that fly out,|stop and come back.",
    [EN_RICOCHET] = "Bank shots: bullets that bounce off the walls,|aimed to come from where nothing stands.",
    [EN_ORBITER] = "Circles the ship at a breathing radius,|firing inwards.",
    [EN_BLINKER] = "Fires a fat orb that bursts into a ring, then|teleports - where it will appear flashes first.",
    [EN_SERPENT] = "Slithers in big S curves, leaving a short-lived|wall of still bullets behind it.",
    [EN_MIRROR] = "Its front plate stops every shot and slowly|turns to face the ship: get round it.",
    [EN_LANCER] = "Charges a beam (the aim line thickens, then locks)|and fires a sweeping stream of fast bullets.",
    [EN_MORTAR] = "Heavy and nearly still: shells the area round|the ship, one grenade on it and four around.",
    [EN_GUARDIAN] = "Follows its allies and shields them (they take|40% damage while it lives): kill it first.",
    [EN_WRAITH] = "Fades out (shots go through it) and drifts|closer, then shows up to release homing wisps.",
    [EN_BOSS] = "Spirals, aimed fans, rings with a gap. Then a|charge and a turning cross; enraged, minions.",
    [EN_LIGHTHOUSE] = "Bolted in the middle, its beams sweep the room:|hide behind the pillars. Shells over the walls.",
    [EN_WYRM] = "Only its head can be hurt: the body stops shots.|Burrows and bursts out under the ship.",
    [EN_STORM] = "Lightning arcs between four pylons, forked bolts,|strikes from above and homing sparks.",
    [EN_FORGE] = "Leaps onto the ship and slams down in fire,|pours lines of fire, rains molten metal.",
    [EN_CARRIER] = "Turning shield plates stop shots: aim for the|gaps. Drones, missiles, flak, then a beam.",
    [EN_SINGULARITY] = "A black hole: pulls the ship in and bends shots.|Rings collapse onto it, its gravity flips.",
    [EN_SPLITLING] = "Born from a dying splitter: hops after|the ship in short bursts.",
    [EN_DRIFTER] = "Drifts in straight lines, bouncing off the walls,|and now and then lets go of a slow shot.",
    [EN_PEON] = "Walks up to a fair distance and fires|one plain shot at a time. The basics.",
    [EN_BUZZER] = "A tiny fly zigzagging at the ship. No gun:|it only hurts if it touches.",
    [EN_PULSAR] = "Barely moves. Every few seconds it pulses|out a ring of six slow bullets.",
    [EN_SNAIL] = "Crawls after the ship, leaving a trail of slime|that hangs there for a while. Do not cross it.",
    [EN_SKITTER] = "Timid: keeps well away. Come close and it|panics, firing backwards as it runs.",
    [EN_POPPER] = "Looks harmless and nearly is - until it dies:|it pops into a ring of bullets. Not point-blank.",
    [EN_SPRINKLER] = "Sweeps a thin stream of bullets back and forth|across the ship, then rests a moment.",
    [EN_BEETLE] = "Its shell turns to face the ship: shots on the|front barely scratch it. Go round its back.",
    [EN_CRAB] = "Only scuttles sideways. Its pincer shots|curve in from both sides to meet the ship.",
    [EN_WASP] = "Hovers, freezes, then darts in and stings|with a fast needle before backing off.",
    [EN_DICE] = "Rolls itself every two seconds: the face up|says how many bullets fly. A six is bad news.",
    [EN_PUFFER] = "Drifts close, swells up and bursts into spikes|all round, then deflates. Back off when it swells.",
    [EN_PRISM] = "Fires red, green and blue: the side shots bend|in to meet the middle one on the ship.",
    [EN_BAT] = "Swoops round the ship in figure eights,|pinging rings of small sonar bullets.",
    [EN_CENTIPEDE] = "A head and five segments: each part is its own|target. Kill the head and the next one leads.",
    [EN_BROODER] = "Lays eggs as it goes. Each egg hatches a|buzzer if it is not broken in time.",
    [EN_MEDIC] = "Keeps behind the most hurt ally and heals it|with a beam. Take it out first.",
    [EN_HEDGEHOG] = "Bristles with quills: rings of them now and|then, and one thrown back for every hit taken.",
    [EN_SAPPER] = "Runs right next to the ship, plants a bomb|and runs off. The bomb bursts into a big ring.",
    [EN_ROCKETEER] = "Keeps away and launches pairs of homing|missiles. They can be shot down.",
    [EN_JESTER] = "A different trick every time: weaving, bouncing,|homing, splitting or boomerang shots.",
    [EN_FROSTBITE] = "Its frost shots chill the ship they hit:|slower for a moment, so the next ones land.",
    [EN_GLITCH] = "Jitters from spot to spot every third of a|second, spraying glitched bullets.",
    [EN_PEEKER] = "Shy: freezes and hides its face while the ship|aims at it, creeps up and fires when it does not.",
    [EN_ECHO] = "Mirrors the ship's moves: comes at it as the|ship steps in, backs off as the ship backs off.",
    [EN_CLOCK] = "Its two hands fire on every tick,|and a full ring chimes every twelve.",
    [EN_HALO] = "Six orbs circle it and sting on contact;|it flings them out and grows them back.",
    [EN_STALKER] = "All but invisible while it closes in. It shows|up only to fire a fan, then slips away.",
    [EN_OCTOPUS] = "Swims in pulses; every pulse lets out|a ring of weaving ink.",
    [EN_BUZZSAW] = "A spinning blade bouncing round the room:|shots glance off until it stops to cool down.",
    [EN_SENTRY] = "A turret that turns slowly and streams bullets|while the ship is in its sights. Outrun its turn.",
    [EN_TORCH] = "Walks up close and pours out a cone of short|flames. Keep out of its reach.",
    [EN_ZAPPER] = "Locks a point and charges an arc to it:|step off the line before it cracks.",
    [EN_BEAMER] = "A deadly beam that sweeps across the room;|the line shows first. Blocks give cover.",
    [EN_ENGINEER] = "Builds up to three turrets and keeps away|from the fight. Get it before it digs in.",
    [EN_JELLY] = "Drifts in pulses of slow rings; its stinging|tentacles trail behind it.",
    [EN_SCORPION] = "Lobs a sting over the walls with its tail,|and snaps its pincers up close.",
    [EN_LINKER] = "Pairs up: a deadly tether hums between the two|as they circle the ship. Kill one to cut it.",
    [EN_SWAPPER] = "Marks the ship with a line, then trades places|with it - in the middle of its own fire.",
    [EN_SPOTTER] = "Paints the ship from afar. A moment later|a strike comes down where it stood.",
    [EN_LODESTONE] = "Its field pushes shots aside: get close or|shoot fast. Fires rings that turn.",
    [EN_SPONGE] = "Soaks up the shots on its front, then|squeezes them all back out at the ship.",
    [EN_PARRY] = "Guard up (white ring): hits do nothing and|are answered at once. Wait for the guard to drop.",
    [EN_BUBBLER] = "Wraps an ally in a bubble that stops three|whole hits. Pop the bubbles or the bubbler.",
    [EN_DRUMMER] = "Every beat hurries every enemy around it:|faster moves, faster fire.",
    [EN_TRICKSTER] = "Makes look-alikes and swaps places. The real|one fires; an illusion pops in one hit.",
    [EN_HYDRA] = "Every head lost grows two smaller, faster|ones - twice over.",
    [EN_MANTIS] = "Lurks half hidden and creeps up, then strikes|in a flash along a line. Exposed after.",
    [EN_FLAIL] = "Swings a spiked ball on a chain, faster|up close, letting spikes fly at each turn.",
    [EN_MOLE] = "Tunnels under the ship (out of reach), bursts|out in a ring, then stands exposed for a moment.",
    [EN_CRYSTAL] = "Grows all the time: bigger, tougher, more|shards. Break it early.",
    [EN_NECRO] = "Raises what died in the room (four at most)|and sends homing skulls.",
    [EN_BERSERKER] = "The more it is hurt, the faster it runs and|the more it fires. Finish it quickly.",
    [EN_FUNGUS] = "Puffs out spores that drift after the ship;|every hit shakes another one loose.",
    [EN_WEAVER] = "Spins webs that hang in the air: one|touch chills the ship.",
    [EN_JUGGERNAUT] = "Armoured front, stomps that ring out round it,|shells that shatter on walls. Flank it.",
    [EN_IDOL] = "Still, spiralling, all but unbreakable while|anything else stands: clear the room first.",
    [EN_BLACKSTAR] = "A small black hole: pulls the ship in and|bends its shots. Its rings turn as they fly.",
    [EN_SENTINEL] = "A searchlight sweeps the room. Caught in it,|the alarm hurries everyone and it opens fire.",
    [EN_EGG] = "Laid by a brooder: hatches a buzzer|unless it is broken in time.",
    [EN_MISSILE] = "Launched by a rocketeer: homes in and|blows into a ring next to the ship.",
    [EN_TURRET] = "Built by an engineer: turns to face the ship|and fires short bursts.",
};

const char *enemy_desc(EnemyType t) { return t >= 0 && t < EN_COUNT && DESC[t] ? DESC[t] : ""; }

const char *enemy_title(EnemyType t) {
    if (type_is_boss(t)) return boss_title(t);
    static char buf[32];
    const char *n = t >= 0 && t < EN_COUNT ? ENEMIES[t].name : "?";
    size_t i = 0;
    for (; n[i] && i + 1 < sizeof buf; i++) buf[i] = (char)(n[i] >= 'a' && n[i] <= 'z' ? n[i] - 32 : n[i]);
    buf[i] = 0;
    return buf;
}

/* ---- The list: in the order they show up in a run ---------------------------- */

static int boss_room(EnemyType t) {
    switch (t) {
    case EN_FORGE: return 20;
    case EN_CARRIER: return 30;
    case EN_SINGULARITY: return 40;
    default: return BOSS_EVERY;
    }
}

/* Room where it is first met (a splitling: with the splitter, right after it). */
static float first_room(EnemyType t) { /* recursive for the helpers */
    if (type_is_boss(t)) return 1000.0f + (float)boss_room(t) + (float)(t - BOSS_FIRST) * 0.01f;
    if (t == EN_SPLITLING) return first_room(EN_SPLITTER) + 0.0001f;
    if (t == EN_EGG) return first_room(EN_BROODER) + 0.0001f;
    if (t == EN_MISSILE) return first_room(EN_ROCKETEER) + 0.0001f;
    if (t == EN_TURRET) return first_room(EN_ENGINEER) + 0.0001f;
    int f = ENEMIES[t].min_floor;
    return (float)(f >= 1 && f <= FLOORS ? f : FLOORS + 1) * 10.0f + (float)ENEMIES[t].tier + (float)ENEMIES[t].cost * 0.01f + (float)t * 0.00001f;
}

static int order[EN_COUNT]; /* a constant, built once */

static void build_order(void) {
    if (order[0] != order[1]) return;
    for (int i = 0; i < EN_COUNT; i++) order[i] = i;
    for (int a = 1; a < EN_COUNT; a++)
        for (int b = a; b > 0 && first_room(order[b]) < first_room(order[b - 1]); b--) {
            int t = order[b];
            order[b] = order[b - 1];
            order[b - 1] = t;
        }
}

#define BES_COLS 4
#define BES_ROWS ((EN_COUNT + BES_COLS - 1) / BES_COLS)
#define BES_X0 40.0f
#define BES_Y0 190.0f
#define BES_CW 192.0f
#define BES_RH 30.0f
#define BES_PX 820.0f
#define BES_PW 1060.0f
#define BES_SCREEN_W 1000.0f /* the small screen: the whole arena, scaled down */
#define BES_SCREEN_H (BES_SCREEN_W * (ARENA_Y1 - ARENA_Y0) / (ARENA_X1 - ARENA_X0))
#define BES_RESTART_T 40.0f  /* a boss starts over (its phases go by in the meantime) */

static void cell(int i, float *x, float *y) {
    *x = BES_X0 + (float)(i / BES_ROWS) * BES_CW;
    *y = BES_Y0 + (float)(i % BES_ROWS) * BES_RH;
}

static int cell_at(V2 m) {
    for (int i = 0; i < EN_COUNT; i++) {
        float x, y;
        cell(i, &x, &y);
        if (m.x >= x && m.x < x + BES_CW - 12.0f && m.y >= y && m.y < y + BES_RH - 4.0f) return i;
    }
    return -1;
}

static bool seen(int i) { return G.prof.seen[order[i]] != 0; }

/* ---- The small world ---------------------------------------------------------- */

static Game keep;        /* the real world while the bestiary is open (1.4 MB: off the stack) */
static bool is_open;
static int world_type = -1;
static float respawn_t, fire_t;
static V2 cam; /* centre of the small screen's view, in arena pixels */

#define BES_VIEW_W 1150.0f    /* arena pixels shown across for an enemy */
#define BES_VIEW_MARGIN 40.0f /* the view may go that far past the arena's edges (what clings to them) */

/* Those that keep far away or cling to the edges are shown with the whole arena, like the bosses. */
static bool wide_view(int t) {
    return type_is_boss(t) || t == EN_SNIPER || t == EN_CRAWLER || t == EN_MORTAR || t == EN_GRENADIER || t == EN_LANCER || t == EN_HUNTER ||
           t == EN_SKITTER || t == EN_ROCKETEER || t == EN_ENGINEER || t == EN_SPOTTER || t == EN_NECRO || t == EN_BEAMER ||
           t == EN_SENTINEL || t == EN_LINKER || t == EN_ECHO;
}

static float view_w(void) { return wide_view(world_type) ? ARENA_X1 - ARENA_X0 + 2.0f * BES_VIEW_MARGIN : BES_VIEW_W; }

/* The view follows the ship and the enemies, never far past the arena's edges. */
static V2 cam_target(void) {
    if (wide_view(world_type)) return v2(ARENA_CX, ARENA_CY);
    V2 sum = v2(0, 0);
    int n = 0;
    for (int i = 0; i < G.nen; i++)
        if (G.en[i].hp > 0.0f) sum = v2_add(sum, G.en[i].pos), n++;
    V2 c = n ? v2_lerp(G.pl.pos, v2_scale(sum, 1.0f / (float)n), 0.5f) : G.pl.pos;
    float hw = view_w() * 0.5f, hh = hw * (ARENA_Y1 - ARENA_Y0) / (ARENA_X1 - ARENA_X0);
    c.x = clampf(c.x, ARENA_X0 - BES_VIEW_MARGIN + hw, ARENA_X1 + BES_VIEW_MARGIN - hw);
    c.y = clampf(c.y, ARENA_Y0 - BES_VIEW_MARGIN + hh, ARENA_Y1 + BES_VIEW_MARGIN - hh);
    return c;
}

static void spawn_group(EnemyType t) {
    V2 at = type_is_boss(t) ? boss_spawn_point(t) : v2(ARENA_CX, ARENA_Y0 + 220.0f);
    int n = t == EN_SWARM ? 5 : t == EN_SPLITLING ? 3 : 1;
    for (int k = 0; k < n; k++) enemy_queue_spawn(t, v2_add(at, v2((float)(k - n / 2) * 60.0f, 0)), 0.5f + 0.1f * (float)k);
    if (t == EN_GUARDIAN || t == EN_MEDIC || t == EN_BUBBLER || t == EN_DRUMMER || t == EN_IDOL) /* someone to shield / heal / hurry */
        for (int k = -1; k <= 1; k += 2) enemy_queue_spawn(t == EN_DRUMMER ? EN_PEON : EN_SHOOTER, v2_add(at, v2((float)k * 180.0f, 60.0f)), 0.6f);
    if (t == EN_LINKER) enemy_queue_spawn(EN_LINKER, v2_add(at, v2(300.0f, 0)), 0.6f); /* a pair, for the tether */
}

/* The small world for type t, in G (the menu's own fields - page, input, profile... - untouched). */
static void world_reset(int t) {
    audio_suppress(true);
    rng_seed(&G.rng, 0xBE57ull + (uint64_t)t * 7919u);
    G.demo = true; /* nothing it does counts for the save */
    G.god = true;
    G.transitioning = false;
    G.hitstop = G.shake = G.hurt_flash = G.bomb_flash = G.banner_t = 0.0f;
    G.run_time = 0.0f;
    G.npb = G.neb = G.nen = G.nsp = G.npk = G.nmc = 0;
    fx_clear();
    player_init();
    memset(&G.room, 0, sizeof G.room);
    bool boss = type_is_boss(t);
    G.room.type = boss ? ROOM_BOSS : ROOM_COMBAT;
    G.room.depth = boss ? boss_room((EnemyType)t) : ENEMIES[t].min_floor >= 1 && ENEMIES[t].min_floor <= FLOORS ? (ENEMIES[t].min_floor - 1) * BOSS_EVERY + 1 : 5;
    G.room.entry = SIDE_BOTTOM;
    G.room.hue = boss ? 350.0f : 200.0f;
    G.room.nwaves = G.room.wave = 1;
    G.room.boss = boss ? (EnemyType)t : EN_BOSS;
    room_boss_layout(boss ? (EnemyType)t : EN_CHASER); /* the boss's own walls, or open floor (path finder reset) */
    G.pl.pos = v2(ARENA_CX, ARENA_Y1 - 150.0f);
    world_type = t;
    spawn_group((EnemyType)t);
    respawn_t = 0.0f;
    fire_t = 1.0f;
    cam = cam_target();
    audio_suppress(false);
}

/* One tick of the small world: the ship flies round on its own, the enemy plays. */
static void world_step(void) {
    if (world_type < 0) return;
    GameState menu = G.state;
    G.state = GS_PLAYING; /* for the tick: the world's code checks it */
    audio_suppress(true);
    G.run_time += DT;
    /* The ship loops round the middle and keeps away from the enemies (it cannot be hurt anyway). */
    float t = G.run_time, big = type_is_boss(world_type) ? 1.3f : 1.0f;
    Player *p = &G.pl;
    V2 goal = v2(ARENA_CX + 420.0f * big * cosf(t * 0.45f), ARENA_CY + 60.0f + 190.0f * big * sinf(t * 0.8f));
    V2 to = v2_sub(goal, p->pos), dir = v2_scale(v2_norm(to), minf(1.0f, v2_len(to) / 120.0f));
    for (int i = 0; i < G.nen; i++) {
        const Enemy *e = &G.en[i];
        float keep_r = e->radius + 240.0f, d = v2_dist(e->pos, p->pos);
        if (e->hp > 0.0f && d < keep_r && d > 0.01f) dir = v2_add(dir, v2_scale(v2_sub(p->pos, e->pos), 1.6f * (1.0f - d / keep_r) / d));
    }
    if (v2_len2(dir) > 1.0f) dir = v2_norm(dir);
    p->vel = v2_lerp(p->vel, v2_scale(dir, 440.0f), minf(1.0f, 8.0f * DT));
    p->pos = v2_add(p->pos, v2_scale(p->vel, DT));
    room_collide_circle(&p->pos, PLAYER_BODY_R, NULL);
    p->pos.x = clampf(p->pos.x, ARENA_X0 + PLAYER_BODY_R, ARENA_X1 - PLAYER_BODY_R);
    p->pos.y = clampf(p->pos.y, ARENA_Y0 + PLAYER_BODY_R, ARENA_Y1 - PLAYER_BODY_R);
    p->invuln = 10.0f; /* out of reach: bullets fly through */
    /* It fires plain shots at the nearest enemy: what it does against them shows (plates, dodges,
     * fading...), and it dies now and then (splits, comes back). A boss drains below instead. */
    const Enemy *near = enemy_nearest(p->pos, 3000.0f);
    if (near) {
        p->aim_angle = v2_heading(v2_sub(near->pos, p->pos));
        if ((fire_t -= DT) <= 0.0f) {
            fire_t = 0.3f;
            float dmg = ENEMIES[near->type].hp / (enemy_is_boss(near) ? 800.0f : 30.0f);
            V2 d = v2_angle(p->aim_angle);
            pbullet_spawn(v2_add(p->pos, v2_scale(d, 22.0f)), v2_scale(d, 1100.0f), 5.0f, dmg, col_hex(0x66f0ff, 1));
        }
    }

    blocks_update();
    bullets_update();
    enemies_update();
    fx_update();
    /* A boss goes through its phases (and starts over); whatever died comes back. */
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (enemy_is_boss(e) && e->hp > 0.0f) e->hp -= enemy_damage_cap(e, e->max_hp * 0.85f / BES_RESTART_T * DT);
    }
    if (G.nen == 0 && G.nsp == 0 && (respawn_t += DT) > 0.8f) {
        respawn_t = 0.0f;
        spawn_group((EnemyType)world_type);
    }
    cam = v2_lerp(cam, cam_target(), minf(1.0f, 2.5f * DT));
    audio_suppress(false);
    G.state = menu;
    if (type_is_boss(world_type) && G.run_time > BES_RESTART_T) world_reset(world_type);
}

static void world_draw(SDL_FRect r) {
    if (world_type < 0) return;
    float s = r.w / view_w();
    draw_clip(&r);
    draw_set_view(v2(r.x + r.w * 0.5f - cam.x * s, r.y + r.h * 0.5f - cam.y * s), s);
    room_draw_floor();
    room_draw_walls();
    enemies_draw();
    bullets_draw_player();
    draw_ship(G.character, G.pl.pos, G.pl.aim_angle, 1.0f, false);
    draw_circle(G.pl.pos, G.pl.hit_radius + 2.0f, col_hex(0xff3355, 1));
    draw_circle(G.pl.pos, G.pl.hit_radius, COL_WHITE);
    bullets_draw_enemy();
    fx_draw();
    draw_set_view(v2(0, 0), 1.0f);
    draw_clip(NULL);
}

int bestiary_world_type(void) { return world_type; }

/* ---- The page ------------------------------------------------------------------ */

static void select_index(int i) {
    if (i < 0 || i >= EN_COUNT || !seen(i)) return; /* greyed: not met yet */
    if (i != G.bes_sel) sfx_play(SFX_WEAPON_SWITCH);
    G.bes_sel = i;
    if (world_type != order[i]) world_reset(order[i]);
}

/* The next one met from i in steps of dir (stays if none). */
static void step_select(int dir) {
    for (int i = G.bes_sel + dir; i >= 0 && i < EN_COUNT; i += dir)
        if (seen(i)) {
            select_index(i);
            return;
        }
}

void bestiary_open(int sel) {
    build_order();
    if (!is_open) keep = G; /* the real world, put back by bestiary_close */
    is_open = true;
    G.bes_sel = -1;
    world_type = -1;
    for (int i = 0; i < EN_COUNT && G.bes_sel < 0; i++)
        if (seen(i) && (sel < 0 || order[i] == sel)) select_index(i);
    for (int i = 0; i < EN_COUNT && G.bes_sel < 0; i++) /* that one was not met: the first that was */
        if (seen(i)) select_index(i);
    G.sel_mouse = G.in.mouse;
}

void bestiary_close(void) {
    if (!is_open) return;
    is_open = false;
    world_type = -1;
    /* The real world back; the menu's own fields stay as they are now. */
    Input in = G.in;
    V2 sel_mouse = G.sel_mouse;
    float time = G.time;
    int ticks = G.ticks;
    int bes_sel = G.bes_sel;
    G = keep;
    G.in = in;
    G.sel_mouse = sel_mouse;
    G.time = time;
    G.ticks = ticks;
    G.bes_sel = bes_sel;
    room_nav_reset(); /* the path finder's caches were the small world's */
}

bool bestiary_update(void) {
    const Input *in = &G.in;
    if (in->pause_pressed) {
        bestiary_close();
        return false;
    }
    if (G.bes_sel >= 0) {
        if (in->up && !G.title_up) step_select(-1);
        if (in->down && !G.title_down) step_select(+1);
        if (in->left && !G.sel_left) step_select(-BES_ROWS);
        if (in->right && !G.sel_right) step_select(+BES_ROWS);
        if (in->wheel) step_select(in->wheel > 0 ? 1 : -1);
    }
    int hover = cell_at(in->mouse);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0) select_index(hover);
        G.sel_mouse = in->mouse;
    }
    if (in->click_pressed && hover >= 0) select_index(hover);
    world_step();
    return true;
}

static void draw_lines(float cx, float y, float scale, float gap, SDL_FColor c, const char *text) {
    char line[128];
    while (*text) {
        const char *bar = strchr(text, '|');
        size_t n = bar ? (size_t)(bar - text) : strlen(text);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, text, n);
        line[n] = 0;
        draw_text_centered(cx, y, scale, c, "%s", line);
        y += gap;
        text += n + (bar ? 1 : 0);
    }
}

static void detail(void) {
    float x0 = BES_PX, y0 = BES_Y0 - 20.0f, h = 940.0f - y0, cx = BES_PX + BES_PW * 0.5f;
    draw_rect(x0, y0, BES_PW, h, rgba(0.02f, 0.03f, 0.07f, 0.92f));
    if (G.bes_sel < 0) {
        draw_rect_outline(x0, y0, BES_PW, h, 2.0f, col_a(COL_WHITE, 0.2f));
        draw_text_centered(cx, y0 + h * 0.45f, 3.0f, col_a(COL_WHITE, 0.5f), "NOTHING MET YET");
        draw_text_centered(cx, y0 + h * 0.45f + 50.0f, 2.0f, col_a(COL_WHITE, 0.4f), "Every enemy met in a run shows up here");
        return;
    }
    EnemyType t = (EnemyType)order[G.bes_sel];
    const EnemyDef *d = &ENEMIES[t];
    SDL_FColor c = col_hex(d->color, 1);
    draw_rect_outline(x0, y0, BES_PW, h, 2.0f, col_a(c, 0.6f));
    SDL_FRect scr = {cx - BES_SCREEN_W * 0.5f, BES_Y0, BES_SCREEN_W, BES_SCREEN_H};
    world_draw(scr);
    draw_rect_outline(scr.x - 2.0f, scr.y - 2.0f, scr.w + 4.0f, scr.h + 4.0f, 2.0f, col_a(c, 0.45f));
    float y = scr.y + scr.h + 28.0f;
    draw_text_centered(cx, y, 4.0f, c, "%s", enemy_title(t));
    char info[96];
    if (type_is_boss(t)) SDL_snprintf(info, sizeof info, "BOSS - ROOM %d     HP %.0f", boss_room(t), (double)d->hp);
    else if (t == EN_SPLITLING) SDL_snprintf(info, sizeof info, "BORN FROM A SPLITTER     HP %.0f     SPEED %.0f", (double)d->hp, (double)d->speed);
    else if (t == EN_EGG || t == EN_MISSILE || t == EN_TURRET)
        SDL_snprintf(info, sizeof info, "MADE BY A %s     HP %.0f", t == EN_EGG ? "BROODER" : t == EN_MISSILE ? "ROCKETEER" : "ENGINEER", (double)d->hp);
    else if (d->min_floor < 1) SDL_snprintf(info, sizeof info, "IN NO FLOOR YET     HP %.0f     SPEED %.0f", (double)d->hp, (double)d->speed);
    else SDL_snprintf(info, sizeof info, "FLOOR %d     HP %.0f     SPEED %.0f", d->min_floor, (double)d->hp, (double)d->speed);
    draw_text_centered(cx, y + 50.0f, 1.75f, col_a(COL_WHITE, 0.6f), "%s", info);
    draw_lines(cx, y + 95.0f, 2.25f, 32.0f, col_a(COL_WHITE, 0.95f), enemy_desc(t));
}

void bestiary_draw(void) {
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.9f));
    draw_text(BES_X0, 80.0f, 5.0f, col_hex(0x66f0ff, 1), "BESTIARY");
    draw_text(BES_X0, 140.0f, 2.0f, col_a(COL_WHITE, 0.75f), "MET %d / %d", profile_seen(&G.prof), EN_COUNT);
    for (int i = 0; i < EN_COUNT; i++) {
        float x, y;
        cell(i, &x, &y);
        EnemyType t = (EnemyType)order[i];
        bool met = seen(i), sel = i == G.bes_sel;
        SDL_FColor c = col_hex(ENEMIES[t].color, 1);
        if (sel) {
            draw_rect(x - 4.0f, y, BES_CW - 12.0f, BES_RH - 4.0f, col_a(c, 0.25f));
            draw_rect_outline(x - 4.0f, y, BES_CW - 12.0f, BES_RH - 4.0f, 1.5f, c);
        }
        V2 ic = v2(x + 10.0f, y + BES_RH * 0.5f - 2.0f);
        int sides = ENEMIES[t].sides;
        if (met) {
            draw_ngon(ic, 8.0f, sides, -PI_F * 0.5f, col_a(col_mul(c, 0.3f), 1.0f));
            draw_ngon_outline(ic, 8.0f, sides, -PI_F * 0.5f, 2.0f, c);
        } else {
            draw_ngon_outline(ic, 8.0f, sides, -PI_F * 0.5f, 1.5f, col_a(COL_WHITE, 0.18f));
        }
        draw_text(x + 26.0f, y + 9.0f, 1.25f,
                  met ? col_a(type_is_boss(t) ? col_hex(COLOR_BOSS, 1) : COL_WHITE, sel ? 1.0f : 0.85f) : col_a(COL_WHITE, 0.2f), "%s",
                  met ? enemy_title(t) : "???");
    }
    detail();
    draw_text_centered(SCREEN_W * 0.5f, 1030.0f, 1.5f, col_a(COL_WHITE, 0.45f), "ARROWS / MOUSE  choose     ESC  back");
}
