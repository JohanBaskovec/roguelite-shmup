#include "draw.h"
#include "game.h"
#include "audio.h"

#include <string.h>

#define ARENA_W (ARENA_X1 - ARENA_X0)
#define ARENA_H (ARENA_Y1 - ARENA_Y0)

#define WAVE_TIMER 16.0f      /* a wave forces the next one after that long (if the room is thin enough) */
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

bool room_collide_circle(V2 *p, float r, V2 *normal) {
    bool hit = false;
    V2 n = v2(0, 0);
    if (p->x < ARENA_X0 + r) { p->x = ARENA_X0 + r; n.x = 1; hit = true; }
    if (p->x > ARENA_X1 - r) { p->x = ARENA_X1 - r; n.x = -1; hit = true; }
    if (p->y < ARENA_Y0 + r) { p->y = ARENA_Y0 + r; n.y = 1; hit = true; }
    if (p->y > ARENA_Y1 - r) { p->y = ARENA_Y1 - r; n.y = -1; hit = true; }

    const Room *room = &G.room;
    for (int i = 0; i < room->nobs; i++) {
        const SDL_FRect *o = &room->obs[i];
        float cx = clampf(p->x, o->x, o->x + o->w);
        float cy = clampf(p->y, o->y, o->y + o->h);
        float dx = p->x - cx, dy = p->y - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= r * r) continue;
        hit = true;
        if (d2 > 1e-6f) {
            float d = sqrtf(d2);
            n = v2(dx / d, dy / d);
            *p = v2_add(*p, v2_scale(n, r - d));
        } else {
            /* Center inside the rectangle: push out along the shallowest axis. */
            float l = p->x - o->x, rr = o->x + o->w - p->x;
            float t = p->y - o->y, b = o->y + o->h - p->y;
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
        if (p[i] < 0.0f) t0 = fmaxf(t0, t);
        else t1 = fminf(t1, t);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

static SDL_FRect obs_inflated(int i, float m) {
    SDL_FRect o = G.room.obs[i];
    SDL_FRect r = {o.x - m, o.y - m, o.w + 2 * m, o.h + 2 * m};
    return r;
}

/* Largest inflation of r that still leaves p outside (inflated rectangles have square
 * corners, so this is the per-axis distance, not the Euclidean one). */
static float box_dist(V2 p, SDL_FRect r) {
    float dx = fmaxf(fmaxf(r.x - p.x, 0.0f), p.x - (r.x + r.w));
    float dy = fmaxf(fmaxf(r.y - p.y, 0.0f), p.y - (r.y + r.h));
    return fmaxf(dx, dy);
}

/* Nearest obstacle crossed by the segment, or -1. When an endpoint already sits
 * inside an obstacle's safety margin (a mover hugging it, or a target standing
 * next to it), that margin is shrunk so the segment is not blocked forever. */
static int nearest_blocker(V2 a, V2 b, float margin) {
    int best = -1;
    float best_t = 2.0f;
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect o = G.room.obs[i];
        float m = fminf(margin, fmaxf(0.0f, fminf(box_dist(a, o), box_dist(b, o)) - 0.5f));
        float t = segment_entry(a, b, obs_inflated(i, m));
        if (t >= 0.0f && t < best_t) {
            best_t = t;
            best = i;
        }
    }
    return best;
}

bool room_line_clear(V2 a, V2 b, float margin) { return nearest_blocker(a, b, margin) < 0; }

/* Visibility graph between obstacle corners, built lazily per mover margin and
 * cached until the next room. Shortest paths on it never oscillate. */
#define NAV_MAX_NODES (MAX_OBSTACLES * 4)
#define NAV_CACHE 8

typedef struct {
    float margin;
    int n;
    V2 pt[NAV_MAX_NODES];
    uint8_t vis[NAV_MAX_NODES][NAV_MAX_NODES];
} NavGraph;

static NavGraph nav_graphs[NAV_CACHE];
static int nav_count;

static void nav_reset(void) { nav_count = 0; }

static NavGraph *nav_graph(float margin) {
    for (int i = 0; i < nav_count; i++)
        if (nav_graphs[i].margin == margin) return &nav_graphs[i];
    NavGraph *g = &nav_graphs[nav_count < NAV_CACHE ? nav_count++ : NAV_CACHE - 1];
    g->margin = margin;
    g->n = 0;
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect r = obs_inflated(i, margin + 6.0f);
        V2 c[4] = {v2(r.x, r.y), v2(r.x + r.w, r.y), v2(r.x + r.w, r.y + r.h), v2(r.x, r.y + r.h)};
        for (int k = 0; k < 4; k++) {
            if (c[k].x < ARENA_X0 + margin || c[k].x > ARENA_X1 - margin || c[k].y < ARENA_Y0 + margin ||
                c[k].y > ARENA_Y1 - margin)
                continue;
            bool inside = false;
            for (int j = 0; j < G.room.nobs && !inside; j++) {
                SDL_FRect q = obs_inflated(j, margin);
                inside = c[k].x > q.x && c[k].x < q.x + q.w && c[k].y > q.y && c[k].y < q.y + q.h;
            }
            if (!inside) g->pt[g->n++] = c[k];
        }
    }
    for (int i = 0; i < g->n; i++) {
        g->vis[i][i] = 0;
        for (int j = i + 1; j < g->n; j++)
            g->vis[i][j] = g->vis[j][i] = (uint8_t)(nearest_blocker(g->pt[i], g->pt[j], margin) < 0);
    }
    return g;
}

V2 room_navigate(V2 from, V2 goal, float margin) {
    if (nearest_blocker(from, goal, margin) < 0) return goal;
    NavGraph *g = nav_graph(margin);
    int n = g->n, goal_node = n;
    float dist[NAV_MAX_NODES + 1];
    int prev[NAV_MAX_NODES + 1];
    bool done[NAV_MAX_NODES + 1], sees_goal[NAV_MAX_NODES];
    for (int i = 0; i <= n; i++) {
        dist[i] = 1e30f;
        prev[i] = -1;
        done[i] = false;
    }
    for (int i = 0; i < n; i++) {
        if (nearest_blocker(from, g->pt[i], margin) < 0) dist[i] = v2_dist(from, g->pt[i]);
        sees_goal[i] = nearest_blocker(g->pt[i], goal, margin) < 0;
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
        if (sees_goal[u]) {
            float d = dist[u] + v2_dist(g->pt[u], goal);
            if (d < dist[goal_node]) { dist[goal_node] = d; prev[goal_node] = u; }
        }
    }
    if (dist[goal_node] >= 1e29f) return goal; /* unreachable: push straight and slide along walls */

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

static bool rect_overlaps(SDL_FRect a, SDL_FRect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static bool rect_ok(SDL_FRect r) {
    const float margin = 80.0f;
    if (r.x < ARENA_X0 + margin || r.y < ARENA_Y0 + margin || r.x + r.w > ARENA_X1 - margin ||
        r.y + r.h > ARENA_Y1 - margin)
        return false;
    /* Keep door lanes, the entry point and the reward spot clear. */
    const SDL_FRect keep_out[] = {
        {ARENA_X0, ARENA_CY - 170, 280, 340},           /* left door */
        {ARENA_X1 - 280, ARENA_CY - 170, 280, 340},     /* right door */
        {ARENA_CX - 200, ARENA_Y0, 400, 280},           /* top door */
        {ARENA_CX - 200, ARENA_Y1 - 280, 400, 280},     /* bottom door */
        {ARENA_CX - 120, ARENA_CY - 120, 240, 240},     /* reward */
    };
    for (int i = 0; i < ARRAY_LEN(keep_out); i++)
        if (rect_overlaps(r, keep_out[i])) return false;
    /* Rooms whose reward is a row of pedestals keep that row free. */
    const SDL_FRect pedestal_row = {ARENA_X0 + 150, ARENA_CY - 150, ARENA_X1 - ARENA_X0 - 300, 300};
    /* The secret spot (next to the entry door) must stay reachable to drop a bomb there. */
    if (G.room.has_secret) {
        V2 c = room_secret_spot();
        SDL_FRect k = {c.x - 200, c.y - 200, 400, 400};
        if (rect_overlaps(r, k)) return false;
    }
    if (G.room.type == ROOM_WORKSHOP)
        for (int i = 0; i < 2; i++) {
            V2 m = workshop_machine_spot(i);
            SDL_FRect k = {m.x - 190, m.y - 210, 380, 380};
            if (rect_overlaps(r, k)) return false;
        }
    bool row = G.room.type == ROOM_STATS || G.room.type == ROOM_SHOP || G.room.type == ROOM_WORKSHOP ||
               G.room.reward == RW_UPGRADE || G.room.reward == RW_ACTIVE;
    if (row && rect_overlaps(r, pedestal_row)) return false;
    if (G.room.type == ROOM_SHOP) { /* the supply counter */
        V2 a = shop_supply_spot(0), b = shop_supply_spot(SHOP_SUPPLIES - 1);
        SDL_FRect k = {a.x - 150, a.y - 130, b.x - a.x + 300, 260};
        if (rect_overlaps(r, k)) return false;
    }
    for (int i = 0; i < G.room.nobs; i++) {
        SDL_FRect o = G.room.obs[i];
        /* Gaps of at least 100 px: wide enough to fly through, and for the bot's path finder
         * (it keeps ~42 px off each obstacle) to see the passage. */
        SDL_FRect grown = {o.x - 100, o.y - 100, o.w + 200, o.h + 200};
        if (rect_overlaps(r, grown)) return false;
    }
    return true;
}

static void add_obs(float cx, float cy, float w, float h) {
    if (G.room.nobs >= MAX_OBSTACLES) return;
    SDL_FRect r = {cx - w * 0.5f, cy - h * 0.5f, w, h};
    if (rect_ok(r)) G.room.obs[G.room.nobs++] = r;
}

static void add_mirrored(float cx, float cy, float w, float h) {
    add_obs(cx, cy, w, h);
    add_obs(2.0f * ARENA_CX - cx, cy, w, h);
}

static void gen_obstacles(void) {
    G.room.nobs = 0;
    switch (irand(0, 5)) {
    case 0: /* open arena */
        break;
    case 1: /* four pillars */
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.3f, 100, 100);
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.72f, 100, 100);
        break;
    case 2: /* two vertical walls */
        add_mirrored(ARENA_X0 + ARENA_W * 0.33f, ARENA_CY, 50, 340);
        break;
    case 3: { /* random mirrored blocks */
        int n = irand(2, 5);
        for (int i = 0; i < n; i++)
            add_mirrored(frand(ARENA_X0 + 150, ARENA_CX - 130), frand(ARENA_Y0 + 140, ARENA_Y1 - 140),
                         frand(50, 150), frand(50, 150));
    } break;
    case 4: /* horizontal bars */
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.26f, 280, 44);
        add_mirrored(ARENA_X0 + ARENA_W * 0.27f, ARENA_Y0 + ARENA_H * 0.74f, 280, 44);
        break;
    default: /* ring of pillars around the center */
        for (int i = 0; i < 8; i++) {
            float a = TAU_F * ((float)i + 0.5f) / 8.0f;
            add_obs(ARENA_CX + cosf(a) * 380.0f, ARENA_CY + sinf(a) * 260.0f, 64, 64);
        }
        break;
    }
}

/* A random combat reward, never one of the `used` ones (bit mask of RewardType). */
static RewardType roll_reward_except(unsigned used) {
    int weights[RW_COUNT] = {
        [RW_COINS] = 30, [RW_KEY] = 16, [RW_BOMBS] = 16, [RW_HEART] = 14, [RW_UPGRADE] = 24,
        [RW_ACTIVE] = 8,
    };
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
    int locked = chance(0.5f) ? irand(0, MAX_DOORS - 1) : -1;
    /* Occasionally one door leads to a Stats room (never two in a row). */
    int stats_door = r->type == ROOM_COMBAT && chance(0.2f) ? irand(0, MAX_DOORS - 1) : -1;
    /* ...or to a Shop (from room 2 on, never two in a row), on another door. */
    int shop_door = r->type != ROOM_START && r->type != ROOM_SHOP && chance(0.18f) ? irand(0, MAX_DOORS - 1) : -1;
    if (shop_door >= 0 && shop_door == stats_door) shop_door = (stats_door + irand(1, MAX_DOORS - 1)) % MAX_DOORS;
    /* ...or to a Workshop (from room 3 on, never two in a row), on a door of its own. */
    int workshop_door = -1;
    if (r->depth >= 2 && r->type != ROOM_WORKSHOP && chance(0.14f)) {
        workshop_door = irand(0, MAX_DOORS - 1);
        for (int k = 0; k < MAX_DOORS && (workshop_door == stats_door || workshop_door == shop_door); k++)
            workshop_door = (workshop_door + 1) % MAX_DOORS;
        if (workshop_door == stats_door || workshop_door == shop_door) workshop_door = -1;
    }
    r->ndoors = MAX_DOORS;
    /* Every door is unique: special rooms appear once, and no reward is behind two doors
     * (locked or not). Special and locked doors first, then the others from what is left. */
    unsigned used = 1u << RW_NONE;
    for (int i = 0; i < MAX_DOORS; i++) {
        Door *d = &r->doors[i];
        memset(d, 0, sizeof *d);
        d->side = sides[i];
        d->pos = door_position(sides[i]);
        d->type = ROOM_COMBAT; /* later: treasure rooms, bosses... */
        d->locked = (i == locked);
        if (!d->locked && stats_door == i) {
            d->type = ROOM_STATS;
            d->reward = RW_NONE;
            continue;
        }
        if (!d->locked && shop_door == i) {
            d->type = ROOM_SHOP;
            d->reward = RW_NONE;
            continue;
        }
        if (!d->locked && workshop_door == i) {
            d->type = ROOM_WORKSHOP;
            d->reward = RW_NONE;
            continue;
        }
        if (d->locked) {
            /* Locked doors always hide something good. */
            d->reward = RW_UPGRADE;
            used |= 1u << d->reward;
        }
    }
    for (int i = 0; i < MAX_DOORS; i++) {
        Door *d = &r->doors[i];
        if (d->type != ROOM_COMBAT || d->locked) continue;
        d->reward = roll_reward_except(used);
        used |= 1u << d->reward;
    }
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
    r->entry = G.entry_side;
    bool can_hide = type == ROOM_COMBAT || type == ROOM_STATS || type == ROOM_SHOP;
    if (can_hide && (G.force_secret || chance(SECRET_CHANCE))) {
        r->has_secret = true;
        r->ghost_cd = frand(2.0f, 5.0f);
    }

    G.npb = G.neb = G.nen = G.nsp = G.npk = G.nmc = 0;
    fx_clear();
    nav_reset();

    place_at_entry();

    /* Every room starts full of enemies, except the launch bay and secret rooms. What
     * makes a room special is what appears once it is cleared (see room_clear). */
    if (type == ROOM_STATS) r->hue = 165.0f;
    if (type == ROOM_SHOP) r->hue = 315.0f;
    if (type == ROOM_SECRET) r->hue = 32.0f;
    if (type == ROOM_WORKSHOP) r->hue = 80.0f;
    if (type == ROOM_BOSS) {
        r->hue = 350.0f;
        r->reward = RW_UPGRADE; /* the boss always leaves a choice of 3 upgrades */
    }
    /* Open floor for the tutorial / the loot / the boss fight. */
    if (!room_type_peaceful(type) && type != ROOM_BOSS) gen_obstacles();
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
        if (type == ROOM_SECRET) secret_loot();
    } else if (type == ROOM_SECRET) {
        r->nwaves = 1; /* ambush: a single wave guards the loot */
        r->wave_t = 0.8f;
    } else if (type == ROOM_BOSS) {
        r->nwaves = 1; /* the boss itself (it calls its own minions) */
        r->wave_t = 1.0f;
        fx_banner("THE OVERSEER", "destroy it", col_hex(COLOR_BOSS, 1), 1.8f);
    } else {
        /* Enemies never get stronger: deeper rooms have more waves (and bigger ones). */
        int extra = depth / ROOM_DEPTH_PER_WAVE;
        r->nwaves = 2 + (extra > 5 ? 5 : extra);
        r->wave_t = 0.35f;
    }
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
        enemy_queue_spawn(EN_BOSS, v2(ARENA_CX, ARENA_Y0 + 230.0f), 1.4f);
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
    int cap = (int)(20.0f + 2.0f * (float)depth);
    if (cap > MAX_ENEMIES - 20) cap = MAX_ENEMIES - 20;
    int queued = 0;
    while (budget >= 1.0f && queued < cap) {
        EnemyType cand[EN_COUNT];
        int nc = 0;
        for (int t = 0; t < EN_COUNT; t++) {
            bool ok = g_only_enemies ? (g_only_enemies >> t) & 1u : ENEMIES[t].min_depth <= depth;
            if (ok && (float)ENEMIES[t].cost <= budget) cand[nc++] = (EnemyType)t;
        }
        if (nc == 0) break;
        EnemyType t = cand[irand(0, nc - 1)];
        budget -= (float)ENEMIES[t].cost;
        V2 p = t == EN_CRAWLER ? wall_spawn_point(ENEMIES[t].radius) : find_spawn_point(ENEMIES[t].radius);
        float delay = 0.65f + frand(0.0f, 0.5f);
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
    r->wave_t = WAVE_TIMER;
    r->since_wave = 0.0f;
}

/* Stats room reward: a row of trades, take one and the others vanish. */
static void spawn_stat_offers(void) {
    int offers[STATS_ROOM_OFFERS];
    int n = stats_roll_offers(offers, STATS_ROOM_OFFERS);
    for (int i = 0; i < n; i++) {
        V2 p = v2(ARENA_CX + ((float)i - (float)(n - 1) * 0.5f) * 410.0f, ARENA_CY - 20.0f);
        pickup_spawn_choice(PK_STAT, offers[i], p);
        fx_particle(p, v2(0, 0), 0.5f, 160.0f, col_hex(COLOR_STATS, 1), 0.0f, PT_RING);
    }
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

static void room_clear(void) {
    Room *r = &G.room;
    r->cleared = true;
    r->clear_t = 0.0f;
    G.rooms_cleared++;
    sfx_play(SFX_ROOM_CLEAR);
    ebullets_clear(true);
    upg_on_room_clear();
    /* Back to the entry spot, by the entry door: the reward appears in the middle of the
     * room, never right under the ship (no item picked up by accident). */
    fx_particle(G.pl.pos, v2(0, 0), 0.3f, 90.0f, col_hex(CHARACTERS[G.character].color, 0.8f), 0.0f, PT_RING);
    G.pl.pos = room_entry_spot();
    G.pl.vel = v2(0, 0);
    G.pl.dash_t = 0.0f;
    fx_particle(G.pl.pos, v2(0, 0), 0.35f, 120.0f, col_hex(CHARACTERS[G.character].color, 0.9f), 0.0f, PT_RING);
    if (r->type == ROOM_STATS) spawn_stat_offers();
    else if (r->type == ROOM_SHOP) spawn_shop_items();
    else if (r->type == ROOM_WORKSHOP) workshop_spawn();
    else if (r->type == ROOM_SECRET) secret_loot(); /* after an ambush */
    else if (r->reward != RW_NONE) reward_drop(r->reward, v2(ARENA_CX, ARENA_CY));
    if (r->type == ROOM_BOSS) fx_banner("BOSS DEFEATED", "pick your prize", col_hex(COLOR_BOSS, 1), 2.0f);
    else fx_banner("ROOM CLEARED", "choose your next door", col_hex(0x66f0ff, 1), 1.4f);
    fx_shake(0.25f);
}

/* ---- Secret rooms ------------------------------------------------------- */

void room_bomb_at(V2 pos) {
    Room *r = &G.room;
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
    r->clear_t = 0.6f;     /* doors already open; give the player time to choose again */
    place_at_entry();      /* back through its entry door, the secret passage */
    actives_recharge_all();
}

void room_debug_clear(void) {
    Room *r = &G.room;
    if (r->cleared) return;
    r->wave = r->nwaves;
    G.nen = G.nsp = G.neb = 0;
    room_clear();
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
    if (r->msg_cd > 0.0f) r->msg_cd -= DT;
    for (int i = 0; i < r->ndoors; i++)
        if (r->doors[i].anim > 0.0f) r->doors[i].anim -= DT;

    /* A hidden secret sends a little ghost, somewhere in the room, every 5-10 s - only during
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
            V2 p = v2(frand(ARENA_X0 + 80, ARENA_X1 - 80), frand(ARENA_Y0 + 80, ARENA_Y1 - 80));
            for (int tries = 0; tries < 20; tries++) { /* not born inside an obstacle (it may drift through one) */
                V2 q = p;
                if (!room_collide_circle(&q, 20.0f, NULL)) break;
                p = v2(frand(ARENA_X0 + 80, ARENA_X1 - 80), frand(ARENA_Y0 + 80, ARENA_Y1 - 80));
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
        r->wave_t -= DT;
        r->since_wave += DT;
        int alive = G.nen + G.nsp;
        if (r->wave < r->nwaves) {
            /* The timer only forces a new wave once half of the previous one is gone,
             * so waves never pile up endlessly on a struggling player. */
            int half = r->last_wave_size / 3 > 4 ? r->last_wave_size / 3 : 4; /* a third, really */
            bool trigger = (r->wave_t <= 0.0f && (r->wave == 0 || alive <= half)) ||
                           (r->wave > 0 && r->since_wave > 1.5f && alive <= r->last_wave_size / 4);
            if (trigger) spawn_wave();
        } else if (alive == 0) {
            room_clear();
        }
        return;
    }

    float prev_clear_t = r->clear_t;
    r->clear_t += DT;
    if (prev_clear_t < 0.35f && r->clear_t >= 0.35f) sfx_play(SFX_DOOR_OPEN);

    /* Entry door: into the secret room once bombed open, or the secret room's exit. */
    Door entry = {.side = r->entry};
    if ((r->secret_open || r->type == ROOM_SECRET) && r->clear_t > 0.35f && touching_door(&entry)) {
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
    for (float x = ARENA_X0 + 60.0f; x < ARENA_X1; x += 60.0f)
        draw_line(v2(x, ARENA_Y0), v2(x, ARENA_Y1), 1.5f, grid);
    for (float y = ARENA_Y0 + 60.0f; y < ARENA_Y1; y += 60.0f)
        draw_line(v2(ARENA_X0, y), v2(ARENA_X1, y), 1.5f, grid);

    /* Soft inner shading towards the walls. */
    SDL_FColor dark = rgba(0, 0, 0, 0.45f), clear = rgba(0, 0, 0, 0);
    const float s = 90.0f;
    draw_quad_grad(v2(ARENA_X0, ARENA_Y0), v2(ARENA_X1, ARENA_Y0), v2(ARENA_X1, ARENA_Y0 + s), v2(ARENA_X0, ARENA_Y0 + s), dark, dark, clear, clear);
    draw_quad_grad(v2(ARENA_X0, ARENA_Y1 - s), v2(ARENA_X1, ARENA_Y1 - s), v2(ARENA_X1, ARENA_Y1), v2(ARENA_X0, ARENA_Y1), clear, clear, dark, dark);
    draw_quad_grad(v2(ARENA_X0, ARENA_Y0), v2(ARENA_X0 + s, ARENA_Y0), v2(ARENA_X0 + s, ARENA_Y1), v2(ARENA_X0, ARENA_Y1), dark, clear, clear, dark);
    draw_quad_grad(v2(ARENA_X1 - s, ARENA_Y0), v2(ARENA_X1, ARENA_Y0), v2(ARENA_X1, ARENA_Y1), v2(ARENA_X1 - s, ARENA_Y1), clear, dark, dark, clear);

    if (r->type == ROOM_STATS) {
        bool offers = false;
        for (int i = 0; i < G.npk; i++) offers |= G.pk[i].type == PK_STAT;
        if (offers) {
            draw_text_centered(ARENA_CX, ARENA_CY - 190.0f, 3.0f, col_hex(0x4dffd2, 0.8f), "CHOOSE ONE TRADE");
            draw_text_centered(ARENA_CX, ARENA_CY - 150.0f, 2.0f, col_a(COL_WHITE, 0.4f), "every gain has a price");
        }
    }
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
    if (r->type == ROOM_WORKSHOP && G.nmc > 0)
        draw_text_centered(ARENA_CX, ARENA_CY - 200.0f, 3.0f, col_hex(COLOR_WORKSHOP, 0.8f), "WORKSHOP");
    if (r->type == ROOM_SHOP || r->secret_shop) {
        bool items = false;
        for (int i = 0; i < G.npk; i++) items |= G.pk[i].price > 0;
        if (items) {
            draw_text_centered(ARENA_CX, ARENA_CY - 200.0f, 3.0f, col_hex(COLOR_SHOP, 0.8f), "SHOP");
            draw_text_centered(ARENA_CX, ARENA_CY - 160.0f, 2.0f, col_a(COL_WHITE, 0.4f), "walk into an item to buy it");
        }
    }
    if (r->type == ROOM_START) {
        SDL_FColor c = col_a(COL_WHITE, 0.35f);
        float y = ARENA_CY - 70.0f;
        draw_text_centered(ARENA_CX, y, 2.0f, c, "WASD / ZQSD / ARROWS - MOVE");
        draw_text_centered(ARENA_CX, y + 30, 2.0f, c, "MOUSE - AIM & SHOOT");
        draw_text_centered(ARENA_CX, y + 60, 2.0f, c, "SPACE / SHIFT / RIGHT CLICK - ACTIVE ITEM");
        draw_text_centered(ARENA_CX, y + 90, 2.0f, c, "E - BOMB    TAB - UPGRADES (TURN THEM ON / OFF)");
        draw_text_centered(ARENA_CX, y + 140, 2.0f, col_a(col_hex(0xffe066, 1), 0.5f), "LOCKED DOORS COST A KEY BUT HIDE BETTER LOOT");
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
                  : d->type == ROOM_STATS ? col_hex(COLOR_STATS, 1)
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
    else if (d->type == ROOM_BOSS) draw_boss_icon(ip, 11.0f * sc);
    else if (d->type == ROOM_SECRET) draw_text_centered(ip.x + 1.0f, ip.y - 12.0f * sc, 3.0f * sc, col_hex(COLOR_SECRET, 1), "%s", G.room.type == ROOM_SECRET ? "v" : "?");
    else draw_pickup_icon(d->type == ROOM_STATS ? PK_STAT : reward_icon(d->reward), -1, ip, 11.0f * sc, G.time);
    if (pop >= 1.0f) draw_text_centered(ip.x, ip.y + 34.0f, 1.5f, col_a(rc, 0.9f), "%s", door_label(d));
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

    for (int i = 0; i < r->nobs; i++) {
        SDL_FRect o = r->obs[i];
        draw_rect(o.x + 6, o.y + 8, o.w, o.h, rgba(0, 0, 0, 0.35f)); /* drop shadow */
        draw_rect(o.x, o.y, o.w, o.h, wall);
        draw_rect(o.x + 6, o.y + 6, o.w - 12, o.h - 12, col_mul(wall, 1.35f));
        draw_rect_outline(o.x, o.y, o.w, o.h, 3, edge);
    }

    /* Entry door: the way in, closed, unless it is (or leads to) a secret passage. */
    Door entry = {.side = r->entry, .pos = door_position(r->entry), .type = ROOM_SECRET};
    draw_door(&entry, r->cleared, !(r->secret_open || r->type == ROOM_SECRET));
    for (int i = 0; i < r->ndoors; i++) draw_door(&r->doors[i], r->cleared, false);
}
