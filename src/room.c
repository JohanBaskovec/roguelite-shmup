#include "draw.h"
#include "game.h"
#include "audio.h"

#include <string.h>

#define ARENA_W (ARENA_X1 - ARENA_X0)
#define ARENA_H (ARENA_Y1 - ARENA_Y0)

#define WAVE_TIMER 16.0f      /* boss rooms: unused (a single wave) */
/* Every wave has a timer (the bar at the bottom): when it runs out the next wave comes in, whatever is
 * left of this one; the last wave's timer brings in the walls (crush.c). Longer for bigger waves. */
#define WAVE_TIME_BASE 9.0f
#define WAVE_TIME_PER_ENEMY 0.45f
#define WAVE_TIME_MAX 22.0f
#define WAVE_TIME_LAST 5.0f   /* the last wave: that much longer before the walls move */
#define WAVE_TIME_HORDE_MAX 40.0f /* MOD_HORDE: its single wave's timer can be that long */
#define HORDE_BUDGET 2.6f     /* MOD_HORDE: its wave is that much bigger than a normal one */
float g_wave_time_override = 0.0f;
#define ROOM_DEPTH_PER_WAVE 5 /* one more wave every 5 rooms */

static V2 side_inward(Side side);

Side side_opposite(Side s) {
    switch (s) {
    case SIDE_LEFT: return SIDE_RIGHT;
    case SIDE_RIGHT: return SIDE_LEFT;
    case SIDE_TOP: return SIDE_BOTTOM;
    default: return SIDE_TOP;
    }
}

/* Where a bomb opens the secret passage: right next to the entry door. */
V2 room_secret_spot(void) { return v2_add(door_position(G.room.entry), v2_scale(side_inward(G.room.entry), 60.0f)); }

V2 room_entry_spot(void) { return v2_add(door_position(G.room.entry), v2_scale(side_inward(G.room.entry), 80.0f)); }

static void place_at_entry(void) {
    G.pl.pos = room_entry_spot();
    G.pl.vel = v2_scale(side_inward(G.room.entry), 250.0f);
    G.pl.dash_t = 0.0f;
    G.pl.invuln = fmaxf(G.pl.invuln, 0.6f);
}

V2 door_position(Side side) {
    switch (side) {
    case SIDE_LEFT: return v2(ARENA_X0, ARENA_CY);
    case SIDE_TOP: return v2(ARENA_CX, ARENA_Y0);
    case SIDE_RIGHT: return v2(ARENA_X1, ARENA_CY);
    default: return v2(ARENA_CX, ARENA_Y1);
    }
}

static V2 side_inward(Side side) {
    switch (side) {
    case SIDE_LEFT: return v2(1, 0);
    case SIDE_TOP: return v2(0, 1);
    case SIDE_RIGHT: return v2(-1, 0);
    default: return v2(0, -1);
    }
}

/* ---- Collision ----------------------------------------------------------- */

/* Floor cells with an obstacle on them: lets a circle in the open (most bullets, most of the time)
 * skip the obstacle list. Always a superset of the solid obstacles: built from them, a block that
 * breaks only leaves its cell marked until the next room_nav_reset() (headless checks it). */
#define OCC_W 30 /* ARENA_W / GRID */
#define OCC_H 15 /* ARENA_H / GRID */
static uint8_t occ[OCC_H][OCC_W];
static int occ_layout = -1, occ_nobs = -1;

/* (no floorf: a libm call; left of the arena everything lands in cell 0 anyway) */
static int occ_cx(float x) {
    int c = (int)((x - ARENA_X0) * (1.0f / GRID));
    return c < 0 ? 0 : c >= OCC_W ? OCC_W - 1 : c;
}
static int occ_cy(float y) {
    int c = (int)((y - ARENA_Y0) * (1.0f / GRID));
    return c < 0 ? 0 : c >= OCC_H ? OCC_H - 1 : c;
}

static void occ_build(void) {
    memset(occ, 0, sizeof occ);
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if (!obstacle_solid(o)) continue;
        for (int y = occ_cy(o->r.y), y1 = occ_cy(o->r.y + o->r.h - 0.5f); y <= y1; y++)
            for (int x = occ_cx(o->r.x), x1 = occ_cx(o->r.x + o->r.w - 0.5f); x <= x1; x++) occ[y][x] = 1;
    }
    occ_layout = G.room.layout_id;
    occ_nobs = G.room.nobs;
}

/* An obstacle may lie within r of p (1 px to spare). */
static bool occ_near(V2 p, float r) {
    if (occ_layout != G.room.layout_id || occ_nobs != G.room.nobs) occ_build();
    r += 1.0f;
    for (int y = occ_cy(p.y - r), y1 = occ_cy(p.y + r); y <= y1; y++)
        for (int x = occ_cx(p.x - r), x1 = occ_cx(p.x + r); x <= x1; x++)
            if (occ[y][x]) return true;
    return false;
}

bool room_occupancy_ok(void) {
    if (occ_layout != G.room.layout_id || occ_nobs != G.room.nobs) return true; /* rebuilt on the next use */
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if (!obstacle_solid(o)) continue;
        for (int y = occ_cy(o->r.y), y1 = occ_cy(o->r.y + o->r.h - 0.5f); y <= y1; y++)
            for (int x = occ_cx(o->r.x), x1 = occ_cx(o->r.x + o->r.w - 0.5f); x <= x1; x++)
                if (!occ[y][x]) return false;
    }
    return true;
}

bool room_collide_circle(V2 *p, float r, V2 *normal) { return room_collide_obstacle(p, r, normal, NULL); }

bool room_collide_obstacle(V2 *p, float r, V2 *normal, int *which) {
    bool hit = false;
    if (which) *which = -1;
    V2 n = v2(0, 0);
    float bx0 = room_x0(), bx1 = room_x1(), by0 = room_y0(), by1 = room_y1(); /* less while the walls close in */
    if (p->x < bx0 + r) { p->x = bx0 + r; n.x = 1; hit = true; }
    if (p->x > bx1 - r) { p->x = bx1 - r; n.x = -1; hit = true; }
    if (p->y < by0 + r) { p->y = by0 + r; n.y = 1; hit = true; }
    if (p->y > by1 - r) { p->y = by1 - r; n.y = -1; hit = true; }

    const Room *room = &G.room;
    if (!occ_near(*p, r)) { /* open floor all around */
        if (normal) *normal = v2_norm(n);
        return hit;
    }
    for (int i = 0; i < room->nobs; i++) {
        if (!obstacle_solid(&room->obs[i])) continue;
        const SDL_FRect *o = &room->obs[i].r;
        float cx = clampf(p->x, o->x, o->x + o->w);
        float cy = clampf(p->y, o->y, o->y + o->h);
        float dx = p->x - cx, dy = p->y - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= r * r) continue;
        hit = true;
        if (which) *which = i;
        if (d2 > 1e-6f) {
            float d = sqrtf(d2);
            n = v2(dx / d, dy / d);
            *p = v2_add(*p, v2_scale(n, r - d));
        } else {
            /* Center inside the rectangle: push out along the shallowest axis - never through a
             * side flush with the arena edge (a wall jutting out of it), that would be outside. */
            float l = p->x - o->x, rr = o->x + o->w - p->x;
            float t = p->y - o->y, b = o->y + o->h - p->y;
            if (o->x <= bx0 + r) l = 1e9f;
            if (o->x + o->w >= bx1 - r) rr = 1e9f;
            if (o->y <= by0 + r) t = 1e9f;
            if (o->y + o->h >= by1 - r) b = 1e9f;
            float m = fminf(fminf(l, rr), fminf(t, b));
            if (m == l) { p->x = o->x - r; n = v2(-1, 0); }
            else if (m == rr) { p->x = o->x + o->w + r; n = v2(1, 0); }
            else if (m == t) { p->y = o->y - r; n = v2(0, -1); }
            else { p->y = o->y + o->h + r; n = v2(0, 1); }
        }
    }
    if (normal) *normal = v2_norm(n);
    return hit;
}

/* ---- Navigation ---------------------------------------------------------- */

/* Liang-Barsky clipping of segment a->b against rectangle r.
 * Returns the entry parameter in [0, 1], or -1 when the segment misses. */
static float segment_entry(V2 a, V2 b, SDL_FRect r) {
    float t0 = 0.0f, t1 = 1.0f, dx = b.x - a.x, dy = b.y - a.y;
    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {a.x - r.x, r.x + r.w - a.x, a.y - r.y, r.y + r.h - a.y};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return -1.0f;
            continue;
        }
        float t = q[i] / p[i];
        if (p[i] < 0.0f) t0 = maxf(t0, t);
        else t1 = minf(t1, t);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

static SDL_FRect obs_inflated(int i, float m) {
    SDL_FRect o = G.room.obs[i].r;
    SDL_FRect r = {o.x - m, o.y - m, o.w + 2 * m, o.h + 2 * m};
    return r;
}

/* Largest inflation of r that still leaves p outside (inflated rectangles have square
 * corners, so this is the per-axis distance, not the Euclidean one). */
static float box_dist(V2 p, SDL_FRect r) {
    float dx = maxf(maxf(r.x - p.x, 0.0f), p.x - (r.x + r.w));
    float dy = maxf(maxf(r.y - p.y, 0.0f), p.y - (r.y + r.h));
    return maxf(dx, dy);
}

/* Nearest obstacle crossed by the segment, or -1. When an endpoint already sits
 * inside an obstacle's safety margin (a mover hugging it, or a target standing
 * next to it), that margin is shrunk so the segment is not blocked forever. */
static bool nav_counts(int i, bool walls_only) {
    const Obstacle *o = &G.room.obs[i];
    return obstacle_solid(o) && (!walls_only || o->kind == OB_WALL);
}

static int nearest_blocker_ex(V2 a, V2 b, float margin, bool walls_only) {
    int best = -1;
    float best_t = 2.0f;
    /* Quick reject: obstacles out of the segment's bounding box (grown by the margin). */
    float x0 = minf(a.x, b.x) - margin, x1 = maxf(a.x, b.x) + margin;
    float y0 = minf(a.y, b.y) - margin, y1 = maxf(a.y, b.y) + margin;
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect o = G.room.obs[i].r;
        if (o.x > x1 || o.x + o.w < x0 || o.y > y1 || o.y + o.h < y0 || !nav_counts(i, walls_only)) continue;
        float m = minf(margin, maxf(0.0f, minf(box_dist(a, o), box_dist(b, o)) - 0.5f));
        float t = segment_entry(a, b, obs_inflated(i, m));
        if (t >= 0.0f && t < best_t) {
            best_t = t;
            best = i;
        }
    }
    return best;
}

/* Same test, but only whether anything is crossed: stops at the first obstacle hit (most callers
 * - lines of sight, the path finder - never need to know which one comes first). */
static bool line_blocked(V2 a, V2 b, float margin, bool walls_only) {
    float x0 = minf(a.x, b.x) - margin, x1 = maxf(a.x, b.x) + margin;
    float y0 = minf(a.y, b.y) - margin, y1 = maxf(a.y, b.y) + margin;
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect o = G.room.obs[i].r;
        if (o.x > x1 || o.x + o.w < x0 || o.y > y1 || o.y + o.h < y0 || !nav_counts(i, walls_only)) continue;
        float m = minf(margin, maxf(0.0f, minf(box_dist(a, o), box_dist(b, o)) - 0.5f));
        if (segment_entry(a, b, obs_inflated(i, m)) >= 0.0f) return true;
    }
    return false;
}

static int nearest_blocker(V2 a, V2 b, float margin) { return nearest_blocker_ex(a, b, margin, false); }

bool room_line_clear(V2 a, V2 b, float margin) { return !line_blocked(a, b, margin, false); }
bool room_line_clear_walls(V2 a, V2 b, float margin) { return !line_blocked(a, b, margin, true); }

int room_block_between(V2 a, V2 b, float margin) {
    int i = nearest_blocker(a, b, margin);
    return i >= 0 && obstacle_breakable(&G.room.obs[i]) ? i : -1;
}

/* Visibility graph between obstacle corners, built lazily per mover margin and
 * cached until the next room. Shortest paths on it never oscillate. */
#define NAV_MAX_NODES (MAX_OBSTACLES * 4)
#define NAV_CACHE 8

typedef struct {
    float margin;
    bool walls_only;
    int n;
    V2 pt[NAV_MAX_NODES];
    uint8_t vis[NAV_MAX_NODES][NAV_MAX_NODES];
} NavGraph;

static NavGraph nav_graphs[NAV_CACHE];
static int nav_count;

static void nav_reset(void) { nav_count = 0, occ_layout = -1; }
void room_nav_reset(void) { nav_reset(); }

static NavGraph *nav_graph(float margin, bool walls_only) {
    for (int i = 0; i < nav_count; i++)
        if (nav_graphs[i].margin == margin && nav_graphs[i].walls_only == walls_only) return &nav_graphs[i];
    NavGraph *g = &nav_graphs[nav_count < NAV_CACHE ? nav_count++ : NAV_CACHE - 1];
    g->margin = margin;
    g->walls_only = walls_only;
    g->n = 0;
    for (int i = 0; i < G.room.nobs; i++) {
        if (!nav_counts(i, walls_only)) continue;
        SDL_FRect r = obs_inflated(i, margin + 6.0f);
        V2 c[4] = {v2(r.x, r.y), v2(r.x + r.w, r.y), v2(r.x + r.w, r.y + r.h), v2(r.x, r.y + r.h)};
        for (int k = 0; k < 4; k++) {
            if (c[k].x < ARENA_X0 + margin || c[k].x > ARENA_X1 - margin || c[k].y < ARENA_Y0 + margin ||
                c[k].y > ARENA_Y1 - margin)
                continue;
            /* Grown like the corners: a corner inside another obstacle, or on its edge (blocks side
             * by side, walls merged into one shape), is not a corner of the whole and would only
             * make the graph huge. On one of its corners (an L made of two rects), it is: kept once. */
            bool useless = false;
            for (int j = 0; j < G.room.nobs && !useless; j++) {
                if (j == i || !nav_counts(j, walls_only)) continue;
                SDL_FRect q = obs_inflated(j, margin + 6.0f);
                if (!(c[k].x > q.x - 0.5f && c[k].x < q.x + q.w + 0.5f && c[k].y > q.y - 0.5f && c[k].y < q.y + q.h + 0.5f)) continue;
                bool on_x = fabsf(c[k].x - q.x) < 0.5f || fabsf(c[k].x - (q.x + q.w)) < 0.5f;
                bool on_y = fabsf(c[k].y - q.y) < 0.5f || fabsf(c[k].y - (q.y + q.h)) < 0.5f;
                useless = !(on_x && on_y) || j < i; /* a shared corner: the first obstacle keeps it */
            }
            if (!useless) g->pt[g->n++] = c[k];
        }
    }
    for (int i = 0; i < g->n; i++) {
        g->vis[i][i] = 0;
        for (int j = i + 1; j < g->n; j++)
            g->vis[i][j] = g->vis[j][i] = (uint8_t)!line_blocked(g->pt[i], g->pt[j], margin, walls_only);
    }
    return g;
}

V2 room_navigate(V2 from, V2 goal, float margin) { return room_navigate_ex(from, goal, margin, false, NULL); }

V2 room_navigate_ex(V2 from, V2 goal, float margin, bool walls_only, bool *reached) {
    if (reached) *reached = true;
    if (!line_blocked(from, goal, margin, walls_only)) return goal;
    NavGraph *g = nav_graph(margin, walls_only);
    int n = g->n, goal_node = n;
    float dist[NAV_MAX_NODES + 1];
    int prev[NAV_MAX_NODES + 1];
    bool done[NAV_MAX_NODES + 1];
    for (int i = 0; i <= n; i++) {
        dist[i] = 1e30f;
        prev[i] = -1;
        done[i] = false;
    }
    for (int i = 0; i < n; i++) {
        if (!line_blocked(from, g->pt[i], margin, walls_only)) dist[i] = v2_dist(from, g->pt[i]);
    }
    /* Dijkstra (dense, O(n^2) with n <= ~100). */
    for (;;) {
        int u = -1;
        for (int i = 0; i <= n; i++)
            if (!done[i] && dist[i] < 1e29f && (u < 0 || dist[i] < dist[u])) u = i;
        if (u < 0 || u == goal_node) break;
        done[u] = true;
        for (int v = 0; v < n; v++) {
            if (done[v] || !g->vis[u][v]) continue;
            float d = dist[u] + v2_dist(g->pt[u], g->pt[v]);
            if (d < dist[v]) { dist[v] = d; prev[v] = u; }
        }
        /* Whether a node sees the goal is only worked out for the nodes actually visited. */
        if (!line_blocked(g->pt[u], goal, margin, walls_only)) {
            float d = dist[u] + v2_dist(g->pt[u], goal);
            if (d < dist[goal_node]) { dist[goal_node] = d; prev[goal_node] = u; }
        }
    }
    if (dist[goal_node] >= 1e29f) { /* unreachable: push straight and slide along walls */
        if (reached) *reached = false;
        return goal;
    }

    /* Walk back to find the first waypoints of the path. */
    int path[NAV_MAX_NODES + 1], len = 0;
    for (int node = prev[goal_node]; node >= 0 && len < NAV_MAX_NODES; node = prev[node]) path[len++] = node;
    for (int i = len - 1; i >= 0; i--) {
        V2 c = g->pt[path[i]];
        if (v2_dist2(from, c) > 6.0f * 6.0f) return c; /* skip the corner we are standing on */
    }
    return goal;
}

/* ---- Generation ---------------------------------------------------------- */

/* A random combat reward, never one of the `used` ones (bit mask of RewardType). */
static RewardType roll_reward_except(unsigned used) {
    int weights[RW_COUNT] = {[RW_COINS] = 1, [RW_KEY] = 1, [RW_BOMBS] = 1, [RW_HEART] = 1, [RW_UPGRADE] = 1, [RW_ACTIVE] = 1}; /* all alike */
    for (int i = 0; i < RW_COUNT; i++)
        if (used & (1u << i)) weights[i] = 0;
    int total = 0;
    for (int i = 0; i < RW_COUNT; i++) total += weights[i];
    int roll = irand(0, total - 1);
    for (int i = 0; i < RW_COUNT; i++) {
        if (roll < weights[i]) return (RewardType)i;
        roll -= weights[i];
    }
    return RW_COINS;
}

RewardType room_roll_reward(void) { return roll_reward_except(0); }

static void gen_doors(void) {
    Room *r = &G.room;
    /* The exits: every side but the one the ship came in by. */
    Side sides[MAX_DOORS];
    for (int s = SIDE_LEFT, n = 0; s <= SIDE_BOTTOM; s++)
        if (s != (int)r->entry) sides[n++] = (Side)s;
    /* The room before a boss has a single door, straight ahead: the boss room. */
    if (is_boss_depth(r->depth + 1)) {
        r->ndoors = 1;
        Door *d = &r->doors[0];
        memset(d, 0, sizeof *d);
        d->side = side_opposite(r->entry);
        d->pos = door_position(d->side);
        d->type = ROOM_BOSS;
        d->reward = RW_UPGRADE;
        return;
    }
    /* Every kind of room has the same chance, whatever the depth, the hearts or the keys of the ship: the 3 doors are 3 different
     * kinds out of the 6 combat rewards and the 10 special rooms (one chance in 16 each), with these rules:
     * at least one door costs no key (a ship without keys always goes on: else one door, at random, becomes a free kind); no special room and no upgrade / active
     * room from the launch bay (the run starts without a key); never the special room the ship is in. */
    typedef struct { RoomType type; RewardType reward; } Kind;
    static const Kind KINDS[] = {
        {ROOM_COMBAT, RW_COINS}, {ROOM_COMBAT, RW_KEY}, {ROOM_COMBAT, RW_BOMBS}, {ROOM_COMBAT, RW_HEART}, {ROOM_COMBAT, RW_UPGRADE},
        {ROOM_COMBAT, RW_ACTIVE}, {ROOM_SHOP, RW_NONE}, {ROOM_WORKSHOP, RW_NONE}, {ROOM_CASINO, RW_NONE}, {ROOM_BAPHOMET, RW_NONE},
        {ROOM_TRADER, RW_NONE}, {ROOM_PURIFIER, RW_NONE}, {ROOM_VAULT, RW_NONE}, {ROOM_FORGE, RW_NONE}, {ROOM_KEYS, RW_NONE},
        {ROOM_INFIRMARY, RW_NONE},
    };
    int pool[ARRAY_LEN(KINDS)], npool = 0;
    for (int k = 0; k < (int)ARRAY_LEN(KINDS); k++) {
        bool special = KINDS[k].type != ROOM_COMBAT, locked = door_needs_key(KINDS[k].type, KINDS[k].reward);
        if (r->type == ROOM_START && (special || locked)) continue;
        if (special && KINDS[k].type == r->type) continue;
        pool[npool++] = k;
    }
    int pick[MAX_DOORS] = {0}, left[ARRAY_LEN(KINDS)], n = npool;
    memcpy(left, pool, sizeof left);
    bool open = false;
    for (int i = 0; i < MAX_DOORS; i++) { /* a random sample without replacement */
        int j = irand(0, n - 1);
        pick[i] = left[j];
        left[j] = left[--n];
        open |= !door_needs_key(KINDS[pick[i]].type, KINDS[pick[i]].reward);
    }
    if (!open) { /* every door costs a key: one of them (at random) becomes a free kind (at random) */
        int free[ARRAY_LEN(KINDS)], nfree = 0;
        for (int j = 0; j < n; j++)
            if (!door_needs_key(KINDS[left[j]].type, KINDS[left[j]].reward)) free[nfree++] = left[j];
        pick[irand(0, MAX_DOORS - 1)] = free[irand(0, nfree - 1)];
    }
    r->ndoors = MAX_DOORS;
    for (int i = 0; i < MAX_DOORS; i++) {
        Door *d = &r->doors[i];
        memset(d, 0, sizeof *d);
        d->side = sides[i];
        d->pos = door_position(sides[i]);
        d->type = KINDS[pick[i]].type;
        d->reward = KINDS[pick[i]].reward;
    }
    for (int i = 0; i < MAX_DOORS; i++) r->doors[i].locked = door_needs_key(r->doors[i].type, r->doors[i].reward);
    /* Room modifiers: any room behind a door but the boss's (WAGER: combat rooms only), shown on the door (mods.c).
     * One wager door at most: a ship down to its last heart can always leave by another one. */
    bool wager = false;
    for (int i = 0; i < MAX_DOORS; i++) {
        Door *d = &r->doors[i];
        d->mods = (uint16_t)(g_mod_override >= 0 ? mods_allowed(d->type, d->reward, (unsigned)g_mod_override) : mod_roll(d->type, d->reward));
        if (wager) d->mods &= (uint16_t)~MOD_BIT(MOD_WAGER);
        wager |= (d->mods & MOD_BIT(MOD_WAGER)) != 0;
    }
    /* ...and one door at least asks for neither a key nor a heart: the wager comes off the open doors otherwise. */
    bool free_way = false;
    for (int i = 0; i < r->ndoors; i++) free_way |= !r->doors[i].locked && !(r->doors[i].mods & MOD_BIT(MOD_WAGER));
    for (int i = 0; i < r->ndoors && !free_way; i++)
        if (!r->doors[i].locked) r->doors[i].mods &= (uint16_t)~MOD_BIT(MOD_WAGER);
}

static void spawn_shop_items(void);
static void draw_boss_icon(V2 c, float s);

/* Secret room content: 1-2 free items, or (rarely) a full shop, plus resources. */
static void secret_loot(void) {
    if (G.room.secret_shop) spawn_shop_items();
    secret_reward_drop(v2(ARENA_CX, ARENA_CY), !G.room.secret_shop);
}

void room_enter(RoomType type, RewardType reward, int depth) {
    Room *r = &G.room;
    memset(r, 0, sizeof *r);
    r->type = type;
    r->reward = reward;
    r->depth = depth;
    r->hue = 200.0f + (float)depth * 37.0f;
    r->id = ++G.room_counter;
    static int layouts;
    r->layout_id = ++layouts;
    r->entry = G.entry_side;
    r->mods = (uint16_t)mods_allowed(type, reward, g_mod_override >= 0 ? (unsigned)g_mod_override : G.entry_mods);
    G.entry_mods = 0; /* used up */
    unlocks_on_room(depth); /* ships unlocked by depth (not in the demo) */
    bool can_hide = type == ROOM_COMBAT || type == ROOM_SHOP;
    if (can_hide && (G.force_secret || chance(SECRET_CHANCE))) {
        r->has_secret = true;
        r->ghost_cd = frand(2.0f, 5.0f);
    }

    G.npb = G.neb = G.nen = G.nsp = G.npk = G.nmc = 0;
    fx_clear();
    nav_reset();
    G.pl.burn_t = 0.0f; /* a new room puts the fire out */

    place_at_entry();

    /* Every room starts full of enemies, except the launch bay and secret rooms. What
     * makes a room special is what appears once it is cleared (see room_clear). */
    if (type == ROOM_SHOP) r->hue = 315.0f;
    if (type == ROOM_SECRET) r->hue = 32.0f;
    if (type == ROOM_WORKSHOP) r->hue = 80.0f;
    if (type == ROOM_CASINO) r->hue = 48.0f;
    if (type == ROOM_BAPHOMET) r->hue = 0.0f;
    if (type == ROOM_TRADER) r->hue = 200.0f;
    if (type == ROOM_PURIFIER) r->hue = 165.0f;
    if (type == ROOM_VAULT) r->hue = 50.0f;
    if (type == ROOM_FORGE) r->hue = 20.0f;
    if (type == ROOM_KEYS) r->hue = 215.0f;
    if (type == ROOM_INFIRMARY) r->hue = 345.0f;
    if (type == ROOM_BOSS) {
        r->hue = 350.0f;
        r->reward = RW_UPGRADE; /* the boss always leaves a choice of 2 upgrades */
        r->boss = boss_for_depth(depth);
    }
    /* Open floor for the tutorial / the loot; the boss fights in its own room (always the same). */
    if (type == ROOM_BOSS) room_boss_layout(r->boss);
    else if (!room_type_peaceful(type)) room_gen_layout();
    if (type != ROOM_SECRET) gen_doors();          /* the secret room's only exit leads back */
    r->peaceful = type == ROOM_START;
    if (type == ROOM_SECRET) {
        int shop = G.secret_shop_override, ambush = G.secret_ambush_override;
        r->secret_shop = shop >= 0 ? shop == 1 : chance(SECRET_SHOP_CHANCE);
        r->peaceful = !(ambush >= 0 ? ambush == 1 : chance(SECRET_AMBUSH_CHANCE));
        fx_banner(r->secret_shop ? "SECRET SHOP" : "SECRET ROOM", r->peaceful ? "help yourself" : "it was guarded!",
                  col_hex(COLOR_SECRET, 1), 1.6f);
    }
    if (r->peaceful) {
        r->cleared = true; /* no fight: doors (or loot) are there right away */
        r->clear_t = DOORS_AT; /* open from the start, no wait */
        if (type == ROOM_SECRET) secret_loot();
    } else if (type == ROOM_SECRET) {
        r->nwaves = 1; /* ambush: a single wave guards the loot */
        r->wave_t = 0.8f;
    } else if (type == ROOM_BOSS) {
        r->nwaves = 1; /* the boss itself (it calls its own minions) */
        r->wave_t = 1.0f;
        fx_banner(boss_title(r->boss), "destroy it", col_hex(COLOR_BOSS, 1), 1.8f);
    } else {
        /* Enemies never get stronger: deeper rooms have more waves (and bigger ones). */
        int extra = depth / ROOM_DEPTH_PER_WAVE;
        r->nwaves = 2 + (extra > 5 ? 5 : extra);
        r->wave_t = 0.35f;
    }
    if (type == ROOM_VAULT) vault_spawn(); /* the safe is there from the first enemies on */
    mods_on_room_enter(); /* the horde's single wave, the elite's type, the banner */
    upg_on_room_enter();
    actives_recharge_all();
}

/* ---- Waves --------------------------------------------------------------- */

static V2 find_spawn_point(float rad) {
    for (int tries = 0; tries < 40; tries++) {
        V2 p = v2(frand(ARENA_X0 + 90, ARENA_X1 - 90), frand(ARENA_Y0 + 90, ARENA_Y1 - 90));
        if (v2_dist2(p, G.pl.pos) < 400.0f * 400.0f) continue;
        V2 q = p;
        if (room_collide_circle(&q, rad + 12.0f, NULL)) continue;
        return p;
    }
    return v2(ARENA_CX, ARENA_Y0 + 160.0f);
}

/* Crawlers appear on the arena wall, away from the ship. */
static V2 wall_spawn_point(float r) {
    for (int tries = 0; tries < 20; tries++) {
        V2 p;
        switch (irand(0, 3)) {
        case 0: p = v2(frand(ARENA_X0 + 120, ARENA_X1 - 120), ARENA_Y0 + r); break;
        case 1: p = v2(frand(ARENA_X0 + 120, ARENA_X1 - 120), ARENA_Y1 - r); break;
        case 2: p = v2(ARENA_X0 + r, frand(ARENA_Y0 + 120, ARENA_Y1 - 120)); break;
        default: p = v2(ARENA_X1 - r, frand(ARENA_Y0 + 120, ARENA_Y1 - 120)); break;
        }
        if (v2_dist2(p, G.pl.pos) > 450.0f * 450.0f) return p;
    }
    return v2(ARENA_CX, ARENA_Y0 + r);
}

/* Wave size grows with depth - the main difficulty curve, with the number of waves and
 * the enemy types unlocking deeper (ENEMIES[].min_depth). */
#define WAVE_BUDGET_BASE 4.0f
#define WAVE_BUDGET_SCALE 1.1f
#define WAVE_BUDGET_EXP 1.15f
#define WAVE_BUDGET_PER_WAVE 1.0f

static void spawn_wave(void) {
    Room *r = &G.room;
    if (r->type == ROOM_BOSS) {
        enemy_queue_spawn(r->boss, boss_spawn_point(r->boss), 1.4f);
        r->last_wave_size = 1;
        sfx_play(SFX_WAVE);
        fx_shake(0.3f);
        r->wave++;
        r->wave_t = WAVE_TIMER;
        r->since_wave = 0.0f;
        return;
    }
    int depth = r->depth > 1 ? r->depth : 1;
    /* Slightly faster than linear, and the size cap grows too: fixed-strength enemies must
     * keep up with a player who only gets stronger. */
    float budget = WAVE_BUDGET_BASE + WAVE_BUDGET_SCALE * powf((float)depth, WAVE_BUDGET_EXP) +
                   (float)r->wave * WAVE_BUDGET_PER_WAVE;
    budget *= 1.0f + malus_pct(MAL_CROWD); /* CROWD malus: bigger waves (more enemies, never stronger ones) */
    budget *= room_difficulty_enemies();   /* HARD .. IMPOSSIBLE: more enemies, never stronger ones */
    bool horde = room_has_mod(MOD_HORDE);
    if (horde) budget *= HORDE_BUDGET; /* the waves of a room all at once (a bit less) */
    int cap = (int)((horde ? 40.0f : 20.0f) + 2.0f * (float)depth);
    if (cap > MAX_ENEMIES - 20) cap = MAX_ENEMIES - 20;
    int queued = 0;
    while (budget >= 1.0f && queued < cap) {
        EnemyType cand[EN_COUNT];
        int nc = 0;
        for (int t = 0; t < EN_COUNT; t++) {
            bool ok = r->elite ? t == r->elite - 1 : g_only_count ? g_only_enemies[t] : ENEMIES[t].min_depth <= depth;
            if (ok && (float)ENEMIES[t].cost <= budget) cand[nc++] = (EnemyType)t;
        }
        if (nc == 0) break;
        EnemyType t = cand[irand(0, nc - 1)];
        budget -= (float)ENEMIES[t].cost;
        V2 p = t == EN_CRAWLER ? wall_spawn_point(ENEMIES[t].radius) : find_spawn_point(ENEMIES[t].radius);
        float delay = 0.65f + frand(0.0f, horde ? 2.5f : 0.5f); /* a horde pours in */
        if (t == EN_SWARM) {
            for (int k = 0; k < 5; k++)
                enemy_queue_spawn(EN_SWARM, v2_add(p, v2_scale(v2_angle(TAU_F * (float)k / 5.0f), 34.0f)), delay);
            queued += 5;
        } else {
            enemy_queue_spawn(t, p, delay);
            queued++;
        }
    }
    r->last_wave_size = queued;
    sfx_play(SFX_WAVE);
    r->wave++;
    float time = minf(WAVE_TIME_BASE + WAVE_TIME_PER_ENEMY * (float)queued, horde ? WAVE_TIME_HORDE_MAX : WAVE_TIME_MAX);
    if (r->wave >= r->nwaves) time += WAVE_TIME_LAST;
    if (g_wave_time_override > 0.0f) time = g_wave_time_override;
    r->wave_t = r->wave_time = time;
    r->since_wave = 0.0f;
    if (room_has_mod(MOD_COMPACTOR) && r->wave == 1) crush_start(true); /* the walls start to crawl */
}

/* Shop: always SHOP_ITEMS priced items (upgrades, stat trades) + the supply counter. */
static void spawn_shop_items(void) {
    /* One of each category, plus two random slots so shops vary. Sorted left to right. */
    enum { K_UPGRADE, K_STAT };
    static const int extra[] = {K_UPGRADE, K_UPGRADE, K_UPGRADE, K_STAT, K_STAT};
    int kinds[SHOP_ITEMS] = {K_UPGRADE, K_UPGRADE, K_STAT, extra[irand(0, 4)], extra[irand(0, 4)]};
    for (int i = 1; i < SHOP_ITEMS; i++)
        for (int j = i; j > 0 && kinds[j - 1] > kinds[j]; j--) {
            int t = kinds[j];
            kinds[j] = kinds[j - 1];
            kinds[j - 1] = t;
        }
    int stat_offers[SHOP_ITEMS];
    int nstat = stats_roll_offers(stat_offers, SHOP_ITEMS), used_stat = 0;

    for (int i = 0; i < SHOP_ITEMS; i++) {
        PickupType type = PK_UPGRADE;
        int sub = -1;
        if (kinds[i] == K_STAT && used_stat < nstat) {
            type = PK_STAT;
            sub = stat_offers[used_stat++];
        } else {
            sub = upgrade_fresh();
        }
        if (sub < 0) { /* every upgrade owned: a stat trade */
            type = PK_STAT;
            sub = used_stat < nstat ? stat_offers[used_stat++] : -1;
            if (sub < 0) continue;
        }
        V2 p = v2(ARENA_CX + ((float)i - (float)(SHOP_ITEMS - 1) * 0.5f) * 320.0f, ARENA_CY - 20.0f);
        int before = G.npk;
        pickup_spawn(type, sub, p, v2(0, 0));
        if (G.npk > before) G.pk[G.npk - 1].price = shop_price(type, sub);
        fx_particle(p, v2(0, 0), 0.5f, 160.0f, col_hex(COLOR_SHOP, 1), 0.0f, PT_RING);
    }
    /* The supply counter: a heart, a bomb and a key, always. */
    static const PickupType supplies[SHOP_SUPPLIES] = {PK_HEART, PK_BOMB, PK_KEY};
    for (int i = 0; i < SHOP_SUPPLIES; i++) {
        V2 p = shop_supply_spot(i);
        int before = G.npk;
        pickup_spawn(supplies[i], 1, p, v2(0, 0));
        if (G.npk > before) G.pk[G.npk - 1].price = shop_price(supplies[i], 1);
        fx_particle(p, v2(0, 0), 0.4f, 120.0f, col_hex(COLOR_SHOP, 1), 0.0f, PT_RING);
    }
}

/* Bottom right, away from the entry spot and the path to the doors. */
V2 shop_supply_spot(int i) { return v2(ARENA_CX + 330.0f + 230.0f * (float)i, ARENA_CY + 270.0f); }

/* What the cleared room gives, once the obstacles are gone (clear_t reaching 0). */
static void room_reveal(void) {
    Room *r = &G.room;
    if (r->type == ROOM_SHOP) spawn_shop_items();
    else if (r->type == ROOM_WORKSHOP) workshop_spawn();
    else if (r->type == ROOM_CASINO) casino_spawn();
    else if (r->type == ROOM_BAPHOMET) baphomet_spawn();
    else if (r->type == ROOM_TRADER) trader_spawn();
    else if (r->type == ROOM_PURIFIER) purifier_spawn();
    else if (r->type == ROOM_VAULT) vault_reveal();
    else if (r->type == ROOM_FORGE) forge_spawn();
    else if (r->type == ROOM_KEYS) keys_room_spawn();
    else if (r->type == ROOM_INFIRMARY) infirmary_spawn();
    if (r->type >= ROOM_TRADER && r->type <= ROOM_INFIRMARY && r->type != ROOM_VAULT && G.nmc == 0) /* the ship cannot use it any more (its door was rolled for another state) */
        reward_drop(RW_COINS, v2(ARENA_CX, ARENA_CY));
    else if (r->type == ROOM_SECRET) secret_loot(); /* after an ambush */
    else if (room_has_mod(MOD_WAGER) && r->reward != RW_NONE) { /* the reward twice: two choices / twice the loot */
        bool offers = r->reward == RW_UPGRADE || r->reward == RW_ACTIVE;
        reward_drop(r->reward, v2(ARENA_CX, ARENA_CY - (offers ? 150.0f : 0.0f)));
        pickups_set_choice_group(2);
        reward_drop(r->reward, v2(ARENA_CX, ARENA_CY + (offers ? 150.0f : 0.0f)));
        pickups_set_choice_group(1);
    } else if (r->reward != RW_NONE) reward_drop(r->reward, v2(ARENA_CX, ARENA_CY));
}

/* instant (debug clear): no shatter, the room is empty and its reward out at once. */
static void room_clear(bool instant) {
    Room *r = &G.room;
    crush_end(); /* the closing walls burst, the room is whole again */
    r->cleared = true;
    r->clear_t = instant ? 0.0f : -ROOM_REVEAL_T;
    G.rooms_cleared++;
    vault_on_clear(); /* a safe not broken in time vanishes */
    sfx_play(SFX_ROOM_CLEAR);
    ebullets_clear(true);
    upg_on_room_clear();
    /* The ship stays where it is: items are only taken with E, never by walking over them. */
    if (instant) {
        room_obstacles_vanish();
        room_reveal();
    } else {
        room_shatter_start(r->last_death); /* walls and blocks burst like glass, reward after */
    }
    if (r->type == ROOM_BOSS) fx_banner("BOSS DEFEATED", "pick your prize", col_hex(COLOR_BOSS, 1), 2.0f);
    else fx_banner("ROOM CLEARED", "choose your next door", col_hex(0x66f0ff, 1), 1.4f);
    if (r->type == ROOM_BOSS && !instant) unlocks_boss_beaten(); /* its banner names them, saved at once */
    fx_shake(0.25f);
}

/* ---- Secret rooms ------------------------------------------------------- */

void room_bomb_at(V2 pos) {
    Room *r = &G.room;
    room_blocks_break_all(); /* a bomb breaks every breakable block in the room */
    if (!r->has_secret || r->secret_open || r->secret_used) return;
    V2 spot = room_secret_spot();
    if (v2_dist(pos, spot) > SECRET_BOMB_RADIUS) return;
    r->secret_open = true;
    V2 door = door_position(r->entry);
    sfx_play(SFX_SECRET);
    fx_explosion(v2_add(door, v2_scale(side_inward(r->entry), 10.0f)), 40.0f, col_hex(COLOR_SECRET, 1));
    fx_burst(spot, 40, 100.0f, 500.0f, 0.6f, 3.0f, col_hex(COLOR_SECRET, 1));
    fx_particle(spot, v2(0, 0), 0.6f, 260.0f, col_hex(COLOR_SECRET, 1), 0.0f, PT_RING);
    fx_popup(v2_add(door, v2_scale(side_inward(r->entry), 110.0f)), col_hex(COLOR_SECRET, 1), "SECRET PASSAGE!");
    fx_shake(0.5f);
}

void room_enter_secret(void) {
    G.parent = G.room;
    G.parent_npk = G.npk;
    memcpy(G.parent_pk, G.pk, sizeof(Pickup) * (size_t)G.npk);
    G.entry_side = side_opposite(G.room.entry); /* out by the entry door: in on the other side */
    room_enter(ROOM_SECRET, RW_NONE, G.room.depth);
}

void room_return_from_secret(void) {
    G.room = G.parent;
    G.npk = G.parent_npk;
    memcpy(G.pk, G.parent_pk, sizeof(Pickup) * (size_t)G.npk);
    pickups_refresh_owned(); /* an offer left here may have been taken in the secret room */
    G.npb = G.neb = G.nen = G.nsp = 0;
    fx_clear();
    nav_reset();
    Room *r = &G.room;
    r->secret_open = false;
    r->secret_used = true; /* one visit per secret */
    r->revisit = true;
    r->clear_t = DOORS_AT + 0.25f; /* doors already open; give the player time to choose again */
    place_at_entry();      /* back through its entry door, the secret passage */
    actives_recharge_all();
}

void room_debug_clear(void) {
    Room *r = &G.room;
    if (r->cleared) return;
    r->wave = r->nwaves;
    G.nen = G.nsp = G.neb = 0;
    room_clear(true);
}

void room_skip(void) {
    Room *r = &G.room;
    if (r->cleared || r->peaceful || G.transitioning) return;
    r->wave = r->nwaves;
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (enemy_is_boss(e)) boss_on_death(e);
        fx_explosion(e->pos, e->radius, col_hex(ENEMIES[e->type].color, 1));
        r->last_death = e->pos;
    }
    if (G.nen == 0) r->last_death = G.pl.pos;
    G.nen = G.nsp = 0;
    ebullets_clear(true);
    room_clear(false);
}

static bool touching_door(const Door *d) {
    V2 p = G.pl.pos;
    float reach = PLAYER_BODY_R + 6.0f;
    switch (d->side) {
    case SIDE_LEFT: return p.x <= ARENA_X0 + reach && fabsf(p.y - ARENA_CY) < DOOR_HALF;
    case SIDE_RIGHT: return p.x >= ARENA_X1 - reach && fabsf(p.y - ARENA_CY) < DOOR_HALF;
    case SIDE_TOP: return p.y <= ARENA_Y0 + reach && fabsf(p.x - ARENA_CX) < DOOR_HALF;
    case SIDE_BOTTOM: return p.y >= ARENA_Y1 - reach && fabsf(p.x - ARENA_CX) < DOOR_HALF;
    default: return false;
    }
}

void room_update(void) {
    Room *r = &G.room;
    blocks_update(); /* broken blocks go off, burning oil */
    if (r->msg_cd > 0.0f) r->msg_cd -= DT;
    for (int i = 0; i < r->ndoors; i++)
        if (r->doors[i].anim > 0.0f) r->doors[i].anim -= DT;

    /* A hidden secret sends a little ghost near its door (the entry door) every 5-10 s - only during
     * combat. It drifts and wobbles for 0.5-2 s, fading in and out, then vanishes. */
    if (r->ghost_t > 0.0f) {
        r->ghost_t -= DT;
        r->ghost_pos = v2_add(r->ghost_pos, v2_scale(r->ghost_vel, DT));
        const float m = 40.0f; /* drifts off a wall like it bumped into it */
        if ((r->ghost_pos.x < ARENA_X0 + m && r->ghost_vel.x < 0) || (r->ghost_pos.x > ARENA_X1 - m && r->ghost_vel.x > 0)) r->ghost_vel.x = -r->ghost_vel.x;
        if ((r->ghost_pos.y < ARENA_Y0 + m && r->ghost_vel.y < 0) || (r->ghost_pos.y > ARENA_Y1 - m && r->ghost_vel.y > 0)) r->ghost_vel.y = -r->ghost_vel.y;
    }
    if (r->has_secret && !r->cleared && !r->secret_open && !r->secret_used) {
        r->ghost_cd -= DT;
        if (r->ghost_cd <= 0.0f) {
            V2 spot = room_secret_spot(), in = side_inward(r->entry), across = v2(-in.y, in.x), p = spot;
            for (int tries = 0; tries < 20; tries++) { /* not born inside an obstacle (it may drift through one) */
                p = v2_add(spot, v2_add(v2_scale(in, frand(30.0f, GHOST_DOOR_R * 0.8f)), v2_scale(across, frand(-GHOST_DOOR_R, GHOST_DOOR_R) * 0.8f)));
                p.x = clampf(p.x, ARENA_X0 + 50.0f, ARENA_X1 - 50.0f);
                p.y = clampf(p.y, ARENA_Y0 + 50.0f, ARENA_Y1 - 50.0f);
                V2 q = p;
                if (!room_collide_circle(&q, 20.0f, NULL)) break;
            }
            float a = frand(0.0f, TAU_F);
            r->ghost_pos = p;
            r->ghost_vel = v2_scale(v2(cosf(a), sinf(a)), frand(35.0f, 75.0f));
            r->ghost_life = r->ghost_t = frand(GHOST_MIN_TIME, GHOST_MAX_TIME);
            r->ghost_cd = frand(5.0f, 10.0f);
            r->ghosts++;
        }
    } else if (r->secret_open || r->secret_used) {
        r->ghost_t = 0.0f; /* found: no more hints */
    }

    if (!r->cleared) {
        if (r->type == ROOM_BOSS) { /* the bar's damage trail catches up with the hp */
            const Enemy *b = boss_alive();
            float k = b ? b->hp / b->max_hp : 1.0f;
            r->boss_trail = b ? fmaxf(k, r->boss_trail - 0.3f * DT) : 1.0f;
            if (b && r->boss_trail - k > 0.25f) r->boss_trail = k + 0.25f;
        }
        if (!active_time_stop_on()) r->wave_t -= DT; /* TIME STOP holds the timer too */
        r->since_wave += DT;
        int alive = G.nen + G.nsp;
        if (r->wave < r->nwaves) {
            /* The timer brings the next wave whatever is left; clearing most of a wave brings it sooner. */
            bool trigger = r->wave_t <= 0.0f || (r->wave > 0 && r->since_wave > 1.5f && alive <= r->last_wave_size / 4);
            if (trigger) spawn_wave();
        } else if (alive == 0) {
            room_clear(false);
        } else if (r->wave_t <= 0.0f && r->wave_time > 0.0f && r->type != ROOM_BOSS) {
            if (!r->crushing) crush_start(false); /* too slow: the walls close in */
            else r->crush_slow = false;           /* the compactor speeds up */
        }
        if (!r->cleared) {
            crush_update();
            mods_update();
        }
        return;
    }

    float prev_clear_t = r->clear_t;
    r->clear_t += DT;
    if (prev_clear_t < 0.0f && r->clear_t >= 0.0f) room_reveal(); /* the room has shattered */
    if (r->clear_t < DOORS_AT) return;                               /* doors still shut */
    if (prev_clear_t < DOORS_AT) sfx_play(SFX_DOOR_OPEN);

    /* Entry door: into the secret room once bombed open, or the secret room's exit. */
    Door entry = {.side = r->entry};
    if ((r->secret_open || r->type == ROOM_SECRET) && touching_door(&entry)) {
        game_start_transition(r->type == ROOM_SECRET ? DOOR_RETURN : DOOR_SECRET);
        return;
    }
    for (int i = 0; i < r->ndoors; i++) {
        Door *d = &r->doors[i];
        if (!touching_door(d)) continue;
        if (d->locked) {
            if (G.pl.keys > 0) {
                G.pl.keys--;
                d->locked = false;
                d->anim = 0.3f;
                sfx_play_at(SFX_UNLOCK, d->pos);
                fx_popup(v2_add(d->pos, v2_scale(side_inward(d->side), 90)), col_hex(0xffe066, 1), "UNLOCKED");
                fx_burst(d->pos, 30, 100, 400, 0.4f, 3, col_hex(0xffe066, 1));
                fx_shake(0.2f);
            } else if (r->msg_cd <= 0.0f) {
                fx_popup(v2_add(d->pos, v2_scale(side_inward(d->side), 90)), col_hex(0xff5f7e, 1), "NEED A KEY");
                r->msg_cd = 1.0f;
                sfx_play_at(SFX_DENIED, d->pos);
            }
            continue;
        }
        if (d->anim > 0.0f) continue;
        if (!mod_door_pay(d)) continue; /* a wager the ship cannot pay */
        game_start_transition(i);
        break;
    }
}

/* ---- Drawing ------------------------------------------------------------- */

static SDL_FColor reward_color(RewardType rw) {
    switch (rw) {
    case RW_COINS: return col_hex(0xffd23f, 1);
    case RW_KEY: return col_hex(0xd9e3f0, 1);
    case RW_BOMBS: return col_hex(0xff8a3d, 1);
    case RW_HEART: return col_hex(0xff3355, 1);
    case RW_UPGRADE: return col_hex(0xb57cff, 1);
    case RW_ACTIVE: return col_hex(COLOR_ACTIVE, 1);
    default: return col_hex(0x8899aa, 1);
    }
}

float room_ghost_alpha(void) {
    const Room *r = &G.room;
    if (r->ghost_t <= 0.0f || r->ghost_life <= 0.0f) return 0.0f;
    float fade = fminf(GHOST_FADE, r->ghost_life / 3.0f), age = r->ghost_life - r->ghost_t;
    float k = fminf(1.0f, fminf(age / fade, r->ghost_t / fade));
    return k * k * (3.0f - 2.0f * k); /* smooth in and out */
}

/* The secret's hint: a little see-through ghost, round head, wavy hem, two eyes looking where
 * it drifts. Built from shapes that never overlap so it stays evenly transparent. */
static void draw_secret_ghost(const Room *r) {
    float a = room_ghost_alpha();
    if (a <= 0.0f) return;
    float age = r->ghost_life - r->ghost_t;
    V2 c = v2_add(r->ghost_pos, v2(sinf(age * 5.0f) * 6.0f, sinf(age * 3.3f) * 4.0f)); /* wobbles as it floats */
    const float R = 15.0f, H = 16.0f; /* head radius, body height below the head's center */
    SDL_FColor body = col_hex(0xd8ecff, 0.55f * a);
    draw_blend_add();
    draw_glow(v2(c.x, c.y + 4.0f), 46.0f, col_hex(0x9fd0ff, 0.22f * a));
    draw_blend_alpha();
    for (int i = 0; i < 12; i++) { /* head: the top half of a disc */
        float a0 = PI_F + PI_F * (float)i / 12.0f, a1 = PI_F + PI_F * (float)(i + 1) / 12.0f;
        draw_tri(c, v2_add(c, v2(cosf(a0) * R, sinf(a0) * R)), v2_add(c, v2(cosf(a1) * R, sinf(a1) * R)), body);
    }
    draw_rect(c.x - R, c.y, 2.0f * R, H, body);
    /* Wavy hem: 3 points hanging down, swaying. */
    float sway = sinf(age * 9.0f) * 3.0f;
    for (int i = 0; i < 3; i++) {
        float x0 = c.x - R + (float)i * (2.0f * R / 3.0f), x1 = x0 + 2.0f * R / 3.0f;
        float drop = 8.0f + (i == 1 ? sinf(age * 9.0f + 1.5f) * 2.0f : 0.0f);
        draw_tri(v2(x0, c.y + H), v2(x1, c.y + H), v2((x0 + x1) * 0.5f + sway, c.y + H + drop), body);
    }
    /* Eyes looking the way it drifts, blinking now and then. */
    V2 look = v2_scale(v2_norm(r->ghost_vel), 3.0f);
    float eh = fmodf(age, 1.3f) > 1.18f ? 1.0f : 5.0f;
    SDL_FColor eye = col_hex(0x10162a, 0.7f * a);
    for (int s = -1; s <= 1; s += 2)
        draw_rect(c.x + (float)s * 5.5f - 2.0f + look.x, c.y - 4.0f - eh * 0.5f + look.y, 4.0f, eh, eye);
}

void room_draw_floor(void) {
    const Room *r = &G.room;
    SDL_FColor floor = col_hsv(r->hue, 0.55f, 0.075f, 1.0f);
    SDL_FColor grid = col_hsv(r->hue, 0.6f, 0.9f, 0.05f);
    draw_rect(ARENA_X0, ARENA_Y0, ARENA_W, ARENA_H, floor);
    for (float x = ARENA_X0 + GRID; x < ARENA_X1 - 1.0f; x += GRID)
        draw_line(v2(x, ARENA_Y0), v2(x, ARENA_Y1), 1.5f, grid);
    for (float y = ARENA_Y0 + GRID; y < ARENA_Y1 - 1.0f; y += GRID)
        draw_line(v2(ARENA_X0, y), v2(ARENA_X1, y), 1.5f, grid);

    /* Soft inner shading towards the walls. */
    SDL_FColor dark = rgba(0, 0, 0, 0.45f), clear = rgba(0, 0, 0, 0);
    const float s = 90.0f;
    draw_quad_grad(v2(ARENA_X0, ARENA_Y0), v2(ARENA_X1, ARENA_Y0), v2(ARENA_X1, ARENA_Y0 + s), v2(ARENA_X0, ARENA_Y0 + s), dark, dark, clear, clear);
    draw_quad_grad(v2(ARENA_X0, ARENA_Y1 - s), v2(ARENA_X1, ARENA_Y1 - s), v2(ARENA_X1, ARENA_Y1), v2(ARENA_X0, ARENA_Y1), clear, clear, dark, dark);
    draw_quad_grad(v2(ARENA_X0, ARENA_Y0), v2(ARENA_X0 + s, ARENA_Y0), v2(ARENA_X0 + s, ARENA_Y1), v2(ARENA_X0, ARENA_Y1), dark, clear, clear, dark);
    draw_quad_grad(v2(ARENA_X1 - s, ARENA_Y0), v2(ARENA_X1, ARENA_Y0), v2(ARENA_X1, ARENA_Y1), v2(ARENA_X1 - s, ARENA_Y1), clear, dark, dark, clear);

    baphomet_draw_floor();
    fires_draw();
    if (r->has_secret && !r->secret_used) {
        V2 c = room_secret_spot();
        draw_secret_ghost(r);
        if ((G.reveal_secrets || upg_has(UPG_XRAY)) && !r->secret_open) { /* debug menu: SHOW SECRETS, X-RAY VISOR */
            float pulse = 0.5f + 0.5f * sinf(G.time * 6.0f);
            SDL_FColor sc = col_hex(COLOR_SECRET, 1);
            draw_ring(c, SECRET_BOMB_RADIUS, 2.0f, col_a(sc, 0.25f)); /* where a bomb opens it */
            draw_rect(c.x - 14.0f, c.y - 14.0f, 28.0f, 28.0f, col_a(sc, 0.35f + 0.35f * pulse));
            draw_rect_outline(c.x - 18.0f, c.y - 18.0f, 36.0f, 36.0f, 3.0f, sc);
            draw_text_centered(c.x, c.y - 50.0f, 2.0f, sc, "SECRET");
        }
    }
    if (r->type == ROOM_WORKSHOP) {
        bool trades = false;
        for (int i = 0; i < G.npk; i++) trades |= G.pk[i].type == PK_STAT;
        if (trades) {
            draw_text_centered(ARENA_CX, ARENA_CY - 200.0f, 3.0f, col_hex(COLOR_WORKSHOP, 0.8f), "WORKSHOP");
            draw_text_centered(ARENA_CX, ARENA_CY - 160.0f, 2.0f, col_a(COL_WHITE, 0.4f), "one trade - every gain has a price");
        }
    }
    if (r->type == ROOM_CASINO && G.nmc > 0) {
        draw_text_centered(ARENA_CX, ARENA_CY - 200.0f, 3.0f, col_hex(COLOR_CASINO, 0.8f), "CASINO");
        draw_text_centered(ARENA_CX, ARENA_CY - 160.0f, 2.0f, col_a(COL_WHITE, 0.4f), "one prize per machine");
    }
    if (r->type == ROOM_BAPHOMET && r->cleared && r->clear_t >= 0.0f) {
        bool pacts = false;
        for (int i = 0; i < G.npk; i++) pacts |= G.pk[i].type == PK_AUGMENT;
        draw_text_centered(ARENA_CX, ARENA_CY + 300.0f, 3.0f, col_hex(COLOR_BAPHOMET, 0.85f), "BAPHOMET");
        draw_text_centered(ARENA_CX, ARENA_CY + 340.0f, 2.0f, col_a(COL_WHITE, 0.4f),
                           pacts ? "one pact - paid in heart containers" : "no pact you could pay for");
    }
    if (r->type == ROOM_SHOP || r->secret_shop) {
        bool items = false;
        for (int i = 0; i < G.npk; i++) items |= G.pk[i].price > 0;
        if (items) {
            draw_text_centered(ARENA_CX, ARENA_CY - 200.0f, 3.0f, col_hex(COLOR_SHOP, 0.8f), "SHOP");
            draw_text_centered(ARENA_CX, ARENA_CY - 160.0f, 2.0f, col_a(COL_WHITE, 0.4f), "press E next to an item to buy it");
        }
    }
    if (r->type == ROOM_START) {
        SDL_FColor c = col_a(COL_WHITE, 0.35f);
        float y = ARENA_CY - 70.0f;
        draw_text_centered(ARENA_CX, y, 2.0f, c, "WASD / ZQSD / ARROWS - MOVE");
        draw_text_centered(ARENA_CX, y + 30, 2.0f, c, "MOUSE - AIM & SHOOT");
        draw_text_centered(ARENA_CX, y + 60, 2.0f, c, "SHIFT / RIGHT CLICK - ACTIVE ITEMS    SPACE - BOMB");
        draw_text_centered(ARENA_CX, y + 90, 2.0f, c, "E - TAKE / BUY / USE    TAB - UPGRADES (ON / OFF)");
        draw_text_centered(ARENA_CX, y + 140, 2.0f, col_a(col_hex(0xffe066, 1), 0.5f), "UPGRADES, ACTIVES AND MOST SPECIAL ROOMS ARE LOCKED: A KEY EACH");
    }
}

static void draw_padlock(V2 p, float s) {
    SDL_FColor gold = col_hex(0xffd23f, 1);
    draw_arc(v2(p.x, p.y - s * 0.35f), s * 0.55f, s * 0.25f, PI_F, TAU_F, gold);
    draw_rect(p.x - s * 0.8f, p.y - s * 0.4f, s * 1.6f, s * 1.2f, gold);
    draw_circle(v2(p.x, p.y + s * 0.1f), s * 0.2f, col_hex(0x553300, 1));
}

/* Shopping bag with a coin on it. */
static void draw_shop_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_SHOP, 1);
    draw_arc(v2(p.x, p.y - s * 0.45f), s * 0.45f, s * 0.18f, PI_F, TAU_F, c);
    draw_rect(p.x - s * 0.9f, p.y - s * 0.5f, s * 1.8f, s * 1.45f, c);
    draw_pickup_icon(PK_COIN, 1, v2(p.x, p.y + s * 0.22f), s * 0.5f, 0.0f);
}

/* A die showing five. */
static void draw_casino_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_CASINO, 1), dot = col_hex(0x0a0c14, 1);
    draw_rect(p.x - s * 0.85f, p.y - s * 0.85f, s * 1.7f, s * 1.7f, c);
    for (int k = 0; k < 5; k++) {
        float dx = k == 4 ? 0.0f : (k & 1 ? 0.45f : -0.45f), dy = k == 4 ? 0.0f : (k & 2 ? 0.45f : -0.45f);
        draw_circle(v2(p.x + dx * s, p.y + dy * s), s * 0.17f, dot);
    }
}

/* A pentagram, point down, in its circle. */
static void draw_baphomet_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_BAPHOMET, 1);
    draw_ring(p, s * 1.0f, s * 0.14f, c);
    V2 pt[5];
    for (int k = 0; k < 5; k++) pt[k] = v2_add(p, v2_scale(v2_angle(PI_F * 0.5f + TAU_F * (float)k / 5.0f), s * 0.92f));
    for (int k = 0; k < 5; k++) draw_line(pt[k], pt[(k + 2) % 5], s * 0.14f, c);
}

/* Two arrows swapping. */
static void draw_trader_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_TRADER, 1);
    draw_rect(p.x - s * 0.9f, p.y - s * 0.5f, s * 1.5f, s * 0.22f, c);
    draw_tri(v2(p.x + s * 0.5f, p.y - s * 0.95f), v2(p.x + s * 0.5f, p.y), v2(p.x + s * 1.05f, p.y - s * 0.48f), c);
    draw_rect(p.x - s * 0.6f, p.y + s * 0.3f, s * 1.5f, s * 0.22f, c);
    draw_tri(v2(p.x - s * 0.5f, p.y + s * 0.95f), v2(p.x - s * 0.5f, p.y - s * 0.1f), v2(p.x - s * 1.05f, p.y + s * 0.4f), c);
}

/* A drop with a spark. */
static void draw_purifier_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_PURIFIER, 1);
    draw_circle(v2(p.x, p.y + s * 0.35f), s * 0.7f, c);
    draw_tri(v2(p.x - s * 0.62f, p.y + s * 0.15f), v2(p.x + s * 0.62f, p.y + s * 0.15f), v2(p.x, p.y - s * 1.05f), c);
    draw_circle(v2(p.x - s * 0.2f, p.y + s * 0.4f), s * 0.16f, col_hex(0x0a0c14, 1));
}

/* A safe: a square with a dial. */
static void draw_vault_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_VAULT, 1), dark = col_hex(0x0a0c14, 1);
    draw_rect(p.x - s * 0.9f, p.y - s * 0.9f, s * 1.8f, s * 1.8f, c);
    draw_circle(p, s * 0.55f, dark);
    draw_ring(p, s * 0.55f, s * 0.12f, c);
    draw_line(p, v2(p.x + s * 0.35f, p.y - s * 0.25f), s * 0.14f, c);
}

/* An anvil. */
static void draw_forge_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_FORGE, 1);
    draw_rect(p.x - s * 1.0f, p.y - s * 0.75f, s * 2.0f, s * 0.6f, c);
    draw_tri(v2(p.x - s * 1.0f, p.y - s * 0.75f), v2(p.x - s * 1.0f, p.y - s * 0.15f), v2(p.x - s * 1.6f, p.y - s * 0.75f), c);
    draw_rect(p.x - s * 0.4f, p.y - s * 0.15f, s * 0.8f, s * 0.55f, c);
    draw_rect(p.x - s * 0.9f, p.y + s * 0.4f, s * 1.8f, s * 0.4f, c);
}

/* A key. */
static void draw_keys_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_KEYS, 1);
    draw_ring(v2(p.x - s * 0.5f, p.y), s * 0.45f, s * 0.2f, c);
    draw_rect(p.x - s * 0.1f, p.y - s * 0.1f, s * 1.2f, s * 0.2f, c);
    draw_rect(p.x + s * 0.7f, p.y, s * 0.2f, s * 0.45f, c);
    draw_rect(p.x + s * 0.35f, p.y, s * 0.2f, s * 0.35f, c);
}

/* A heart with a cross. */
static void draw_infirmary_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_INFIRMARY, 1);
    draw_heart(p, s * 1.1f, c, false);
    draw_rect(p.x - s * 0.1f, p.y - s * 0.45f, s * 0.2f, s * 0.9f, col_hex(0x0a0c14, 1));
    draw_rect(p.x - s * 0.45f, p.y - s * 0.1f, s * 0.9f, s * 0.2f, col_hex(0x0a0c14, 1));
}

/* A gear. */
static void draw_workshop_icon(V2 p, float s) {
    SDL_FColor c = col_hex(COLOR_WORKSHOP, 1);
    for (int k = 0; k < 8; k++) {
        V2 d = v2_angle(TAU_F * (float)k / 8.0f + G.time * 0.8f);
        draw_line(v2_add(p, v2_scale(d, s * 0.6f)), v2_add(p, v2_scale(d, s * 1.05f)), s * 0.35f, c);
    }
    draw_ring(p, s * 0.62f, s * 0.3f, c);
}

static void draw_door(const Door *d, bool cleared, bool entry) {
    Side side = d->side;
    V2 p = d->pos;
    bool horizontal_wall = (side == SIDE_TOP || side == SIDE_BOTTOM);
    float gx, gy, gw, gh;
    V2 in = side_inward(side);
    if (horizontal_wall) {
        gx = p.x - DOOR_HALF; gw = DOOR_HALF * 2;
        gy = side == SIDE_TOP ? p.y - WALL_T : p.y; gh = WALL_T;
    } else {
        gy = p.y - DOOR_HALF; gh = DOOR_HALF * 2;
        gx = side == SIDE_LEFT ? p.x - WALL_T : p.x; gw = WALL_T;
    }
    SDL_FColor rc = entry ? col_hex(0x445566, 1)
                  : d->type == ROOM_CASINO ? col_hex(COLOR_CASINO, 1)
                  : d->type == ROOM_BAPHOMET ? col_hex(COLOR_BAPHOMET, 1)
                  : d->type == ROOM_TRADER ? col_hex(COLOR_TRADER, 1)
                  : d->type == ROOM_PURIFIER ? col_hex(COLOR_PURIFIER, 1)
                  : d->type == ROOM_VAULT ? col_hex(COLOR_VAULT, 1)
                  : d->type == ROOM_FORGE ? col_hex(COLOR_FORGE, 1)
                  : d->type == ROOM_KEYS ? col_hex(COLOR_KEYS, 1)
                  : d->type == ROOM_INFIRMARY ? col_hex(COLOR_INFIRMARY, 1)
                  : d->type == ROOM_SHOP ? col_hex(COLOR_SHOP, 1)
                  : d->type == ROOM_SECRET ? col_hex(COLOR_SECRET, 1)
                  : d->type == ROOM_WORKSHOP ? col_hex(COLOR_WORKSHOP, 1)
                  : d->type == ROOM_BOSS ? col_hex(COLOR_BOSS, 1)
                  : reward_color(d->reward);
    bool open = cleared && !d->locked && !entry;
    float pulse = 0.5f + 0.5f * sinf(G.time * 6.0f);

    if (open) {
        draw_blend_add();
        draw_glow(v2_add(p, v2_scale(in, 10)), 130.0f, col_a(rc, 0.25f + 0.15f * pulse));
        draw_blend_alpha();
    }
    draw_rect(gx, gy, gw, gh, open ? rgba(0.0f, 0.0f, 0.0f, 1.0f) : col_hex(0x10131c, 1));

    if (!open) {
        /* Energy bars across the doorway. */
        SDL_FColor bar = entry ? col_hex(0x334455, 1)
                       : d->locked && cleared ? col_hex(0xffd23f, 0.9f)
                       : col_hex(0xff3355, 0.6f + 0.4f * pulse);
        for (int i = 0; i < 4; i++) {
            float t = ((float)i + 0.5f) / 4.0f;
            if (horizontal_wall) draw_rect(gx + 2, gy + gh * t - 2, gw - 4, 4, bar);
            else draw_rect(gx + gw * t - 2, gy + 2, 4, gh - 4, bar);
        }
    } else {
        /* Animated chevrons pulling the player in. */
        V2 out = v2_scale(in, -1.0f);
        V2 side_v = v2_perp(out);
        for (int i = 0; i < 3; i++) {
            float k = fmodf(G.time * 1.6f + (float)i / 3.0f, 1.0f);
            V2 c = v2_add(p, v2_scale(in, 70.0f - k * 60.0f));
            SDL_FColor cc = col_a(rc, 0.7f * sinf(k * PI_F));
            draw_line(v2_add(c, v2_scale(side_v, 18)), v2_add(c, v2_scale(out, 12)), 4, cc);
            draw_line(v2_sub(c, v2_scale(side_v, 18)), v2_add(c, v2_scale(out, 12)), 4, cc);
        }
    }

    /* Frame posts. */
    SDL_FColor post = entry ? col_hex(0x334455, 1) : rc;
    if (horizontal_wall) {
        draw_rect(gx - 8, gy - 4, 8, gh + 8, post);
        draw_rect(gx + gw, gy - 4, 8, gh + 8, post);
    } else {
        draw_rect(gx - 4, gy - 8, gw + 8, 8, post);
        draw_rect(gx - 4, gy + gh, gw + 8, 8, post);
    }

    if (entry) return;

    if (d->locked) draw_padlock(v2_add(p, v2_scale(in, -WALL_T * 0.5f)), 16.0f);

    /* Reward preview: hidden during combat so it never gets confused with bullets. */
    if (!cleared) return;
    float pop = clampf((G.room.clear_t - 0.35f) / 0.2f, 0.0f, 1.0f);
    if (pop <= 0.0f) return;
    float sc = pop * (1.0f + 0.3f * sinf(pop * PI_F));
    V2 ip = v2_add(p, v2_scale(in, 78.0f));
    ip.y += sinf(G.time * 3.0f + p.x) * 3.0f;
    draw_blend_add();
    draw_glow(ip, 34.0f * sc, col_a(rc, 0.35f));
    draw_blend_alpha();
    draw_ngon(ip, 24.0f * sc, 6, PI_F / 6.0f, col_hex(0x0a0c14, 0.85f));
    draw_ngon_outline(ip, 24.0f * sc, 6, PI_F / 6.0f, 2.0f, col_a(rc, 0.8f));
    if (d->type == ROOM_SHOP) draw_shop_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_WORKSHOP) draw_workshop_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_CASINO) draw_casino_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_BAPHOMET) draw_baphomet_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_TRADER) draw_trader_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_PURIFIER) draw_purifier_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_VAULT) draw_vault_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_FORGE) draw_forge_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_KEYS) draw_keys_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_INFIRMARY) draw_infirmary_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_BOSS) draw_boss_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_SECRET) draw_text_centered(ip.x + 1.0f, ip.y - 12.0f * sc, 3.0f * sc, col_hex(COLOR_SECRET, 1), "%s", G.room.type == ROOM_SECRET ? "v" : "?");
    else draw_pickup_icon(reward_icon(d->reward), -1, ip, 11.0f * sc, G.time);
    if (pop >= 1.0f) draw_text_centered(ip.x, ip.y + 34.0f, 1.5f, col_a(rc, 0.9f), "%s", door_label(d));
    if (pop >= 1.0f) { /* the room's modifiers, under its reward */
        float y = ip.y + 50.0f;
        for (int m = MOD_NONE + 1; m < MOD_COUNT; m++) {
            if (!(d->mods & MOD_BIT(m))) continue;
            draw_text_centered(ip.x, y, 1.5f, col_hex(mod_color(m), 0.75f + 0.25f * pulse), "+ %s", mod_name(m));
            y += 16.0f;
        }
    }
}

/* Boss door: a watching eye. */
static void draw_boss_icon(V2 c, float s) {
    V2 look = v2_sub(G.pl.pos, c);
    float l = v2_len(look);
    V2 eye = v2_add(c, l > 1.0f ? v2_scale(look, s * 0.3f / l) : v2(0, 0));
    draw_ngon_outline(c, s * 1.15f, 6, G.time, 2.0f, col_hex(COLOR_BOSS, 1));
    draw_circle(c, s * 0.75f, col_hex(0x1a0508, 1));
    draw_circle(eye, s * 0.42f, col_hex(0xffd23d, 1));
    draw_circle(eye, s * 0.18f, rgba(0, 0, 0, 1));
}

void room_draw_walls(void) {
    const Room *r = &G.room;
    SDL_FColor wall = col_hsv(r->hue, 0.35f, 0.16f, 1.0f);
    SDL_FColor edge = col_hsv(r->hue, 0.7f, 1.0f, 0.75f);
    draw_rect(ARENA_X0 - WALL_T, ARENA_Y0 - WALL_T, ARENA_W + WALL_T * 2, WALL_T, wall);
    draw_rect(ARENA_X0 - WALL_T, ARENA_Y1, ARENA_W + WALL_T * 2, WALL_T, wall);
    draw_rect(ARENA_X0 - WALL_T, ARENA_Y0, WALL_T, ARENA_H, wall);
    draw_rect(ARENA_X1, ARENA_Y0, WALL_T, ARENA_H, wall);
    draw_rect_outline(ARENA_X0 - 3, ARENA_Y0 - 3, ARENA_W + 6, ARENA_H + 6, 3, edge);
    draw_rect_outline(ARENA_X0 - WALL_T, ARENA_Y0 - WALL_T, ARENA_W + WALL_T * 2, ARENA_H + WALL_T * 2, 2, col_a(edge, 0.3f));

    blocks_draw(wall, edge);

    /* Entry door: the way in, closed, unless it is (or leads to) a secret passage. */
    Door entry = {.side = r->entry, .pos = door_position(r->entry), .type = ROOM_SECRET};
    bool open = r->cleared && r->clear_t >= DOORS_AT; /* not while the room shatters, nor right after */
    draw_door(&entry, open, !(r->secret_open || r->type == ROOM_SECRET));
    for (int i = 0; i < r->ndoors; i++) draw_door(&r->doors[i], open, false);
    mods_draw_under(); /* the storm's strikes */
    crush_draw(); /* the closing walls, over everything they cover */
}
