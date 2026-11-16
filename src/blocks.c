#include "audio.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * Room layouts and blocks.
 *
 * A layout lives on the floor's grid (the faint lines under the room): GRID px cells, GW x GH.
 * Every wall and every block fills whole cells. Walls are 1 or 2 cells thick, in one of several
 * patterns (pillars, bars, L brackets, halls, a small maze, a field of posts, corridors...), often
 * mirrored, plus pieces jutting out of the edges (spurs, never in front of a door) and arms joining
 * the walls up. Breakable blocks: crates, explosive barrels, oil drums (a pool of burning oil when
 * broken) and spark pods (a ring of balls); they often extend the walls ("gates"), closing the gaps
 * between them: a way may be blocked, but only by something the ship can shoot down.
 * Spacing rules, on the cells: never a passage one cell wide (a free cell with something solid on
 * both sides, the arena edge included), never two solid cells touching only by a corner; so things
 * touch (walls merge into bigger shapes) or leave two cells (120 px) between them. A piece that
 * would close a pocket is taken back: room_walls_connected() (a flood fill where only walls
 * count) checks every layout. The cells then become obstacles: walls merged into big rects,
 * one obstacle per block.
 *
 * Breaking: only the ship's shots, blade and blasts damage blocks (never the enemies' bullets), a
 * bomb breaks them all. A broken block goes off at the next blocks_update(), outside every entity
 * loop (blasts can chain: a barrel breaks the blocks around it, which go off on the next tick).
 *
 * Once the room is cleared, everything goes: a wave of cracks runs from the last enemy killed and
 * every obstacle, walls included, bursts like glass (no blast, no oil, no balls) and fades away.
 * The reward, the shop, the machines come after, in an empty room: layouts only have to make a
 * good fight, and leave the ship room where it arrives.
 */

bool g_dense_blocks; /* --dense-blocks: as many blocks and gates as possible (test) */

#define ARENA_W (ARENA_X1 - ARENA_X0)
#define ARENA_H (ARENA_Y1 - ARENA_Y0)
#define GW 30 /* ARENA_W / GRID */
#define GH 15 /* ARENA_H / GRID */
_Static_assert((int)((ARENA_X1 - ARENA_X0) / GRID) == GW && (int)((ARENA_Y1 - ARENA_Y0) / GRID) == GH &&
                   GW * GRID == ARENA_X1 - ARENA_X0 && GH * GRID == ARENA_Y1 - ARENA_Y0,
               "the arena is a whole number of grid cells");
#define DOOR_CLEAR 150.0f /* no wall jutting out of the edge this close to a door's frame */
#define BLOCK_EDGE 2      /* blocks stay this many cells off the edges (crawlers live there) */

static const float BLOCK_HP[OB_KIND_COUNT] = {
    [OB_CRATE] = 28.0f,  /* 4 basic shots */
    [OB_BARREL] = 18.0f, /* 3 */
    [OB_OIL] = 20.0f,
    [OB_SPARK] = 20.0f,
    [OB_NEST] = 24.0f,
};

static V2 rect_center(SDL_FRect r) { return v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f); }

static float rect_dist(V2 p, SDL_FRect r) {
    float dx = p.x - clampf(p.x, r.x, r.x + r.w), dy = p.y - clampf(p.y, r.y, r.y + r.h);
    return sqrtf(dx * dx + dy * dy);
}

/* ---- The grid ------------------------------------------------------------ */

enum { C_FREE, C_WALL, C_BLOCK };
static uint8_t cells[GH][GW];     /* the layout being drawn up */
static uint8_t cell_kind[GH][GW]; /* C_BLOCK: its ObstacleKind */
static uint8_t banned[GH][GW];    /* kept free: in front of the doors, around the entry spot */

static int cell_x(float x) { return (int)floorf((x - ARENA_X0) / GRID); }
static int cell_y(float y) { return (int)floorf((y - ARENA_Y0) / GRID); }
static bool in_grid(int x, int y) { return x >= 0 && y >= 0 && x < GW && y < GH; }
static bool solid(int x, int y) { return !in_grid(x, y) || cells[y][x] != C_FREE; } /* the edge too */

static bool cell_solid(int x, int y) { return in_grid(x, y) && cells[y][x] != C_FREE; }

/* Free floor squeezed into a one-cell passage (the edge counts), or between two solid cells
 * on its diagonals with nothing else around (an 85 px slit). */
static bool pinched(int x, int y) {
    if (solid(x, y)) return false;
    if ((solid(x - 1, y) && solid(x + 1, y)) || (solid(x, y - 1) && solid(x, y + 1))) return true;
    bool ortho = cell_solid(x - 1, y) || cell_solid(x + 1, y) || cell_solid(x, y - 1) || cell_solid(x, y + 1);
    return !ortho && ((cell_solid(x - 1, y - 1) && cell_solid(x + 1, y + 1)) || (cell_solid(x + 1, y - 1) && cell_solid(x - 1, y + 1)));
}

static bool corner_only(int x, int y) { /* the 2x2 window whose top left cell is (x, y) */
    bool a = solid(x, y), b = solid(x + 1, y), c = solid(x, y + 1), d = solid(x + 1, y + 1);
    return (a && d && !b && !c) || (b && c && !a && !d);
}

/* Walls are 1 or 2 cells thick: never a 3x3 square of wall (pieces side by side make no slabs). */
static bool slab(int x, int y) { /* the 3x3 window whose top left cell is (x, y) */
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            if (!in_grid(x + i, y + j) || cells[y + j][x + i] != C_WALL) return false;
    return true;
}

static void ban_rect(float x0, float y0, float x1, float y1) {
    for (int y = cell_y(y0); y <= cell_y(y1 - 0.01f); y++)
        for (int x = cell_x(x0); x <= cell_x(x1 - 0.01f); x++)
            if (in_grid(x, y)) banned[y][x] = 1;
}

static void grid_reset(void) {
    memset(cells, 0, sizeof cells);
    memset(banned, 0, sizeof banned);
    /* The ship arrives (and comes back after the fight) by the entry door: room to fly in. */
    V2 e = room_entry_spot();
    ban_rect(e.x - 200.0f, e.y - 200.0f, e.x + 200.0f, e.y + 200.0f);
    /* Nothing jutting out of the edge next to a door. */
    float hx = DOOR_HALF + DOOR_CLEAR, d = 2.0f * GRID;
    ban_rect(ARENA_CX - hx, ARENA_Y0, ARENA_CX + hx, ARENA_Y0 + d);
    ban_rect(ARENA_CX - hx, ARENA_Y1 - d, ARENA_CX + hx, ARENA_Y1);
    ban_rect(ARENA_X0, ARENA_CY - hx, ARENA_X0 + d, ARENA_CY + hx);
    ban_rect(ARENA_X1 - d, ARENA_CY - hx, ARENA_X1, ARENA_CY + hx);
}

/* ---- Shapes: sets of cells ------------------------------------------------ */

#define SHAPE_MAX 160
typedef struct {
    int8_t x[SHAPE_MAX], y[SHAPE_MAX];
    uint8_t kind[SHAPE_MAX]; /* blocks */
    int n;
} Shape;

static void sh_cell(Shape *s, int x, int y, int kind) {
    for (int i = 0; i < s->n; i++)
        if (s->x[i] == x && s->y[i] == y) return;
    if (s->n >= SHAPE_MAX || x < -1 || y < -1 || x > GW || y > GH) return; /* off the grid: rejected when placed */
    s->x[s->n] = (int8_t)x, s->y[s->n] = (int8_t)y, s->kind[s->n++] = (uint8_t)kind;
}

static void sh_rect(Shape *s, int x, int y, int w, int h) {
    if (w < 0) x += w + 1, w = -w; /* negative sizes: towards the left / the top */
    if (h < 0) y += h + 1, h = -h;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) sh_cell(s, x + i, y + j, 0);
}

/* The shape and its mirror images (sym: 0 as is, 1 left / right, 2 top / bottom, 3 four ways),
 * as one shape: a piece across the axis merges with its image. */
static Shape sh_sym(const Shape *s, int sym) {
    Shape o = *s;
    for (int i = 0; i < s->n; i++) {
        int mx = GW - 1 - s->x[i], my = GH - 1 - s->y[i];
        if (sym == 1 || sym == 3) sh_cell(&o, mx, s->y[i], s->kind[i]);
        if (sym == 2 || sym == 3) sh_cell(&o, s->x[i], my, s->kind[i]);
        if (sym == 3) sh_cell(&o, mx, my, s->kind[i]);
    }
    return o;
}

/* Puts the shape down (C_WALL, or C_BLOCK with its kinds) if every cell is free and allowed and
 * the spacing rules still hold around it. */
static bool place(const Shape *s, int what) {
    if (s->n == 0) return false;
    for (int i = 0; i < s->n; i++) {
        int x = s->x[i], y = s->y[i];
        if (!in_grid(x, y) || cells[y][x] != C_FREE || banned[y][x]) return false;
        if (what == C_BLOCK && (x < BLOCK_EDGE || y < BLOCK_EDGE || x >= GW - BLOCK_EDGE || y >= GH - BLOCK_EDGE)) return false;
    }
    for (int i = 0; i < s->n; i++) {
        cells[s->y[i]][s->x[i]] = (uint8_t)what;
        cell_kind[s->y[i]][s->x[i]] = s->kind[i];
    }
    bool ok = true;
    for (int i = 0; i < s->n && ok; i++)
        for (int dy = -1; dy <= 1 && ok; dy++)
            for (int dx = -1; dx <= 1 && ok; dx++) {
                int x = s->x[i] + dx, y = s->y[i] + dy;
                ok = !pinched(x, y) && !corner_only(x, y);
            }
    for (int i = 0; i < s->n && ok && what == C_WALL; i++)
        for (int dy = -2; dy <= 0 && ok; dy++)
            for (int dx = -2; dx <= 0 && ok; dx++) ok = !slab(s->x[i] + dx, s->y[i] + dy);
    if (!ok)
        for (int i = 0; i < s->n; i++) cells[s->y[i]][s->x[i]] = C_FREE;
    return ok;
}

static void unplace(const Shape *s) {
    for (int i = 0; i < s->n; i++) cells[s->y[i]][s->x[i]] = C_FREE;
}

/* ---- Connectivity -------------------------------------------------------- */

/* Every free cell (blocks count as free: they can be shot down) and the floor in front of every
 * door can be reached from the entry spot - by something two cells wide: a 2x2 square moving
 * cell by cell must be able to sit on each of them (no passage narrower than 120 px, even
 * diagonally between two corners). walls[y][x]: the cell is a wall. */
static bool cells_connected(uint8_t walls[GH][GW]) {
    static uint8_t seen[GH][GW], covered[GH][GW];
    static int queue[GW * GH];
    memset(seen, 0, sizeof seen);
    memset(covered, 0, sizeof covered);
#define FITS(x, y) ((x) >= 0 && (y) >= 0 && (x) + 1 < GW && (y) + 1 < GH && !walls[y][x] && !walls[y][(x) + 1] && \
                    !walls[(y) + 1][x] && !walls[(y) + 1][(x) + 1])
    V2 e = room_entry_spot();
    int ex = cell_x(e.x), ey = cell_y(e.y), head = 0, tail = 0;
    for (int k = 0; k < 4 && tail == 0; k++) { /* a square on the entry cell */
        int x = ex - (k & 1), y = ey - (k >> 1);
        if (FITS(x, y)) {
            seen[y][x] = 1;
            queue[tail++] = y * GW + x;
        }
    }
    while (head < tail) {
        int c = queue[head++], x = c % GW, y = c / GW;
        covered[y][x] = covered[y][x + 1] = covered[y + 1][x] = covered[y + 1][x + 1] = 1;
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (!FITS(nx, ny) || seen[ny][nx]) continue;
            seen[ny][nx] = 1;
            queue[tail++] = ny * GW + nx;
        }
    }
#undef FITS
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            if (!walls[y][x] && !covered[y][x]) return false; /* a spot the ship cannot reach, or a slit */
    for (int s = SIDE_LEFT; s <= SIDE_BOTTOM; s++) { /* the floor in front of every door */
        V2 d = door_position((Side)s);
        V2 in = v2_add(d, v2_scale(v2_norm(v2_sub(v2(ARENA_CX, ARENA_CY), d)), GRID * 0.5f));
        int x = cell_x(in.x), y = cell_y(in.y);
        if (!in_grid(x, y) || !covered[y][x]) return false;
    }
    return true;
}

static bool grid_connected(void) {
    static uint8_t walls[GH][GW];
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) walls[y][x] = cells[y][x] == C_WALL;
    return cells_connected(walls);
}

/* The room as it is (walls are whole cells): a cell is a wall when its centre is in one. */
bool room_walls_connected(void) {
    static uint8_t walls[GH][GW];
    memset(walls, 0, sizeof walls);
    for (int i = 0; i < G.room.nobs; i++) {
        if (G.room.obs[i].kind != OB_WALL) continue;
        SDL_FRect w = G.room.obs[i].r;
        for (int y = cell_y(w.y + 1.0f); y <= cell_y(w.y + w.h - 1.0f); y++)
            for (int x = cell_x(w.x + 1.0f); x <= cell_x(w.x + w.w - 1.0f); x++)
                if (in_grid(x, y)) walls[y][x] = 1;
    }
    return cells_connected(walls);
}

/* ---- Walls --------------------------------------------------------------- */

/* A wall piece (and its mirror images), taken back if it walls a part of the room off or leaves
 * a passage too narrow. */
static bool add_wall(const Shape *s, int sym) {
    Shape m = sh_sym(s, sym);
    if (!place(&m, C_WALL)) return false;
    if (grid_connected()) return true;
    unplace(&m);
    return false;
}

static int thickness(void) { return chance(0.3f) ? 2 : 1; }

static Shape shape_l(int x, int y, int sx, int sy, int a, int b, int t) { /* corner (x, y), arms a along x, b along y */
    Shape s = {.n = 0};
    sh_rect(&s, x, y, sx * a, sy * t);
    sh_rect(&s, x, y, sx * t, sy * b);
    return s;
}

static Shape shape_t(int cx, int cy, int len, int stem, int t, bool vertical, int dir) {
    Shape s = {.n = 0};
    if (!vertical) {
        sh_rect(&s, cx - len / 2, cy, len, t);
        sh_rect(&s, cx - t / 2, dir > 0 ? cy + t : cy - 1, t, dir * stem);
    } else {
        sh_rect(&s, cx, cy - len / 2, t, len);
        sh_rect(&s, dir > 0 ? cx + t : cx - 1, cy - t / 2, dir * stem, t);
    }
    return s;
}

static Shape shape_plus(int cx, int cy, int arm, int t) {
    Shape s = {.n = 0};
    sh_rect(&s, cx - arm, cy, 2 * arm + t, t);
    sh_rect(&s, cx, cy - arm, t, 2 * arm + t);
    return s;
}

static Shape shape_u(int x, int y, int w, int d, int t, int open_up) { /* box (x, y, w, d) open on one side */
    Shape s = {.n = 0};
    sh_rect(&s, x, y, t, d);
    sh_rect(&s, x + w - t, y, t, d);
    sh_rect(&s, x, open_up ? y + d - t : y, w, t);
    return s;
}

static Shape random_piece(int cx, int cy) {
    int t = thickness();
    Shape s = {.n = 0};
    switch (irand(0, 5)) {
    case 0: sh_rect(&s, cx, cy, irand(3, 6), t); break;
    case 1: sh_rect(&s, cx, cy, t, irand(3, 5)); break;
    case 2: s = shape_l(cx, cy, chance(0.5f) ? 1 : -1, chance(0.5f) ? 1 : -1, irand(2, 4), irand(2, 4), t); break;
    case 3: s = shape_t(cx, cy, irand(3, 5), irand(1, 3), t, chance(0.5f), chance(0.5f) ? 1 : -1); break;
    case 4: s = shape_plus(cx, cy, irand(1, 2), t); break;
    default: sh_rect(&s, cx, cy, irand(1, 2), irand(1, 2)); break; /* a pillar */
    }
    return s;
}

/* A wall jutting out of an arena edge at cell `at` along it, straight or turning at its tip into
 * an L or a T: corridors and dead ends along the sides. */
static Shape spur(Side side, int at, int len, int t) {
    Shape s = {.n = 0};
    int tip_x = 0, tip_y = 0;
    switch (side) {
    case SIDE_LEFT: sh_rect(&s, 0, at, len, t), tip_x = len - t, tip_y = at; break;
    case SIDE_RIGHT: sh_rect(&s, GW - len, at, len, t), tip_x = GW - len, tip_y = at; break;
    case SIDE_TOP: sh_rect(&s, at, 0, t, len), tip_x = at, tip_y = len - t; break;
    default: sh_rect(&s, at, GH - len, t, len), tip_x = at, tip_y = GH - len; break;
    }
    int turn = irand(0, 2), cap = irand(2, 4); /* 0 straight, 1 L, 2 T */
    if (turn == 0) return s;
    bool along_x = side == SIDE_LEFT || side == SIDE_RIGHT;
    int dir = chance(0.5f) ? 1 : -1;
    if (along_x) {
        if (turn == 2) sh_rect(&s, tip_x, tip_y - cap, t, 2 * cap + t);
        else sh_rect(&s, tip_x, dir > 0 ? tip_y : tip_y + t - 1, t, dir * (cap + t));
    } else {
        if (turn == 2) sh_rect(&s, tip_x - cap, tip_y, 2 * cap + t, t);
        else sh_rect(&s, dir > 0 ? tip_x : tip_x + t - 1, tip_y, dir * (cap + t), t);
    }
    return s;
}

/* An arm branching off a wall cell already there, sometimes turning at its end: walls join up
 * into corridors, chambers, dead ends. `bridge`: it runs to the mirror axis, where its image meets it. */
static Shape arm(bool bridge) {
    Shape s = {.n = 0};
    for (int tries = 0; tries < 40; tries++) {
        int x = irand(0, GW - 1), y = irand(0, GH - 1);
        if (cells[y][x] != C_WALL) continue;
        int k = irand(0, 3), dx = (k == 0) - (k == 1), dy = (k == 2) - (k == 3);
        if (bridge) dx = x < GW / 2 ? 1 : -1, dy = 0;
        if (solid(x + dx, y + dy)) continue;
        int len = bridge ? (dx > 0 ? GW / 2 - 1 - x : x - GW / 2) : irand(2, 5);
        if (len < 1) continue;
        for (int i = 1; i <= len; i++) sh_cell(&s, x + dx * i, y + dy * i, 0);
        if (!bridge && chance(0.4f)) { /* a turn at its tip */
            int tx = x + dx * len, ty = y + dy * len, turn = irand(2, 4), side = chance(0.5f) ? 1 : -1;
            for (int i = 1; i <= turn; i++) sh_cell(&s, tx + dy * i * side, ty + dx * i * side, 0);
        }
        return s;
    }
    return s;
}

/* Corridors: walls on a coarse grid (lines every 5 cells), what is left of a maze once a fair part
 * of its walls are knocked down (loops, a few dead ends, never a puzzle). */
static void gen_corridors(void) {
    enum { MC = 6, MR = 3 };
    static const int xs[MC + 1] = {-1, 4, 9, 14, 19, 24, GW}, ys[MR + 1] = {-1, 4, 9, GH}; /* wall lines */
    bool open_e[MC][MR] = {{false}}, open_s[MC][MR] = {{false}}, seen[MC][MR] = {{false}};
    int stack[MC * MR], top = 0, start = irand(0, MC * MR - 1);
    stack[top++] = start;
    seen[start % MC][start / MC] = true;
    while (top > 0) { /* depth-first maze */
        int c = stack[top - 1], x = c % MC, y = c / MC, opts[4], n = 0;
        if (x + 1 < MC && !seen[x + 1][y]) opts[n++] = 0;
        if (x > 0 && !seen[x - 1][y]) opts[n++] = 1;
        if (y + 1 < MR && !seen[x][y + 1]) opts[n++] = 2;
        if (y > 0 && !seen[x][y - 1]) opts[n++] = 3;
        if (n == 0) {
            top--;
            continue;
        }
        int k = opts[irand(0, n - 1)], nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
        if (k == 0) open_e[x][y] = true;
        else if (k == 1) open_e[nx][y] = true;
        else if (k == 2) open_s[x][y] = true;
        else open_s[x][ny] = true;
        seen[nx][ny] = true;
        stack[top++] = ny * MC + nx;
    }
    const float keep = frand(0.5f, 0.7f);
    for (int x = 0; x < MC; x++)
        for (int y = 0; y < MR; y++) {
            if (x + 1 < MC && !open_e[x][y] && chance(keep)) { /* wall on its right, with the joints */
                Shape s = {.n = 0};
                sh_rect(&s, xs[x + 1], ys[y] + (y > 0 ? 0 : 1), 1, ys[y + 1] - ys[y] + (y > 0 ? 1 : 0) - (y + 1 < MR ? 0 : 1));
                add_wall(&s, 0);
            }
            if (y + 1 < MR && !open_s[x][y] && chance(keep)) { /* wall below it */
                Shape s = {.n = 0};
                sh_rect(&s, xs[x] + (x > 0 ? 0 : 1), ys[y + 1], xs[x + 1] - xs[x] + (x > 0 ? 1 : 0) - (x + 1 < MC ? 0 : 1), 1);
                add_wall(&s, 0);
            }
        }
}

/* Gaps left in walls on purpose, filled with blocks by gen_blocks (barricades). */
static Shape pending_gates[2];
static int npending;

/* Returns the symmetry used (blocks follow it). */
static int gen_walls(void) {
    int sym = 1;
    npending = 0;
    switch (irand(0, 14)) {
    case 13:
    case 14: /* corridors (no extra pieces: it has its share of walls) */
        gen_corridors();
        return 0;
    case 0: /* open floor: the blocks make the room */
        break;
    case 1: { /* four pillars */
        Shape s = {.n = 0};
        sh_rect(&s, irand(6, 9), irand(3, 4), 2, 2);
        add_wall(&s, sym = 3);
    } break;
    case 2: { /* two long vertical walls */
        Shape s = {.n = 0};
        int len = irand(5, 9);
        sh_rect(&s, irand(7, 10), GH / 2 - len / 2, thickness(), len);
        add_wall(&s, 1);
    } break;
    case 3: /* random pieces */
        sym = irand(0, 3);
        for (int i = 0, n = irand(3, 6); i < n; i++) {
            Shape s = random_piece(irand(2, sym == 1 || sym == 3 ? GW / 2 - 2 : GW - 4), irand(2, sym >= 2 ? GH / 2 - 1 : GH - 4));
            add_wall(&s, sym);
        }
        break;
    case 4: { /* horizontal bars */
        Shape s = {.n = 0};
        sh_rect(&s, irand(4, 7), irand(3, 4), irand(4, 6), thickness());
        add_wall(&s, sym = 3);
    } break;
    case 5: /* ring of pillars */
        for (int i = 0; i < 4; i++) {
            float a = TAU_F * ((float)i + 0.5f) / 8.0f;
            Shape s = {.n = 0};
            sh_rect(&s, GW / 2 - 1 + (int)lroundf(cosf(a) * 7.0f) - (i == 0 || i == 3 ? 0 : 1), GH / 2 + (int)lroundf(sinf(a) * 4.5f) - 1, 1, 1);
            add_wall(&s, sym = 3);
        }
        break;
    case 6: { /* four L brackets around the middle, open towards it or away from it */
        int s1 = chance(0.5f) ? 1 : -1;
        Shape s = shape_l(irand(7, 9), irand(3, 4), s1, s1, irand(3, 4), irand(2, 3), thickness());
        add_wall(&s, sym = 3);
    } break;
    case 7: { /* halls: long walls splitting the room into lanes */
        Shape s = {.n = 0};
        sh_rect(&s, irand(2, 4), irand(3, 4), irand(7, 10), 1);
        add_wall(&s, sym = 3);
    } break;
    case 8: /* a small maze of short pieces */
        sym = chance(0.5f) ? 1 : 3;
        for (int cx = 0; cx < 3; cx++)
            for (int cy = 0; cy < (sym == 3 ? 2 : 3); cy++) {
                if (chance(0.3f)) continue;
                Shape s = random_piece(3 + cx * 4 + irand(0, 1), 2 + cy * 4 + irand(0, 1));
                add_wall(&s, sym);
            }
        break;
    case 9: { /* a field of posts */
        int sx = irand(3, 4), sy = irand(3, 4);
        for (int x = 2 + irand(0, 1); x < GW / 2 - 1; x += sx)
            for (int y = 2 + irand(0, 1); y < GH - 2; y += sy)
                if (chance(0.65f)) {
                    Shape s = {.n = 0};
                    sh_rect(&s, x, y, 1, 1);
                    add_wall(&s, 1);
                }
    } break;
    case 10: /* crosses */
        for (int i = 0, n = irand(2, 3); i < n; i++) {
            Shape s = shape_plus(irand(4, GW / 2 - 4), irand(3, GH - 5), irand(1, 2), thickness());
            add_wall(&s, 1);
        }
        break;
    case 12: { /* barricades: long walls across the room, a breakable gate in each */
        int x = irand(7, 10), gap = irand(2, 3), gy = irand(GH / 2 - 3, GH / 2 + 1);
        Shape s = {.n = 0};
        sh_rect(&s, x, 2, 1, gy - 2);
        sh_rect(&s, x, gy + gap, 1, GH - 2 - gy - gap);
        if (add_wall(&s, 1)) {
            Shape g = {.n = 0};
            sh_rect(&g, x, gy, 1, gap);
            pending_gates[0] = g;
            for (int i = 0; i < g.n; i++) g.x[i] = (int8_t)(GW - 1 - g.x[i]);
            pending_gates[1] = g;
            npending = 2;
        }
    } break;
    default: { /* chambers: U shapes, open to the middle or to the walls */
        int w = irand(4, 5), d = irand(3, 4);
        Shape s = shape_u(irand(4, 7), irand(2, 3), w, d, 1, chance(0.5f));
        add_wall(&s, sym = 3);
    } break;
    }
    /* The rest is often left unmirrored (the blocks follow). */
    if (chance(0.55f)) sym = 0;
    bool half_x = sym == 1 || sym == 3, half_y = sym >= 2;
    /* A few more pieces here and there. */
    for (int i = 0, extra = irand(3, 6); i < 80 && extra > 0; i++) {
        Shape s = random_piece(irand(2, half_x ? GW / 2 - 2 : GW - 4), irand(2, half_y ? GH / 2 - 1 : GH - 4));
        extra -= add_wall(&s, sym);
    }
    /* Walls jutting out of the sides. */
    for (int i = 0, spurs = chance(0.8f) ? irand(1, 4) : 0; i < 60 && spurs > 0; i++) {
        Side side = (Side)irand(SIDE_LEFT, SIDE_BOTTOM);
        bool along_x = side == SIDE_LEFT || side == SIDE_RIGHT;
        Shape s = spur(side, along_x ? irand(2, GH - 4) : irand(2, GW - 4), along_x ? irand(3, 8) : irand(3, 6), thickness());
        spurs -= add_wall(&s, sym);
    }
    /* Arms joining the walls up; in a mirrored room some reach the axis and meet their image. */
    for (int i = 0, arms = irand(2, 5); i < 60 && arms > 0; i++) {
        Shape s = arm(half_x && chance(0.3f));
        arms -= add_wall(&s, sym);
    }
    return sym;
}

/* ---- Blocks -------------------------------------------------------------- */

typedef enum { TH_CRATES, TH_BARRELS, TH_OIL, TH_SPARK, TH_NESTS, TH_MIXED } Theme;

static Theme roll_theme(void) {
    float r = frand(0, 1);
    return r < 0.52f ? TH_CRATES : r < 0.64f ? TH_BARRELS : r < 0.75f ? TH_OIL : r < 0.83f ? TH_SPARK : r < 0.90f ? TH_NESTS : TH_MIXED;
}

static int theme_kind(Theme th) {
    switch (th) {
    case TH_BARRELS: return chance(0.8f) ? OB_BARREL : OB_CRATE;
    case TH_OIL: return chance(0.8f) ? OB_OIL : OB_CRATE;
    case TH_SPARK: return chance(0.6f) ? OB_SPARK : OB_CRATE;
    case TH_NESTS: return chance(0.35f) ? OB_NEST : OB_CRATE; /* each one more enemy: a few at most */
    case TH_MIXED: return irand(OB_CRATE, OB_NEST);
    default: return chance(0.85f) ? OB_CRATE : irand(OB_BARREL, OB_SPARK);
    }
}

/* A group of blocks and its mirror images, placed whole or not at all. */
static int add_blocks(Shape *s, Theme th, int sym) {
    for (int i = 0; i < s->n; i++) s->kind[i] = (uint8_t)theme_kind(th);
    Shape m = sh_sym(s, sym);
    return place(&m, C_BLOCK) ? m.n : 0;
}

/* Gate: a line of blocks carrying a wall on, from one of its ends, into the gap next to it. */
static int add_gate(int max_tiles) {
    for (int tries = 0; tries < 30; tries++) {
        int x = irand(0, GW - 1), y = irand(0, GH - 1), k = irand(0, 3);
        int dx = (k == 0) - (k == 1), dy = (k == 2) - (k == 3);
        /* the end of a wall running in direction (dx, dy) */
        if (cells[y][x] != C_WALL || !in_grid(x - dx, y - dy) || cells[y - dy][x - dx] != C_WALL || solid(x + dx, y + dy)) continue;
        Theme th = chance(0.75f) ? TH_CRATES : roll_theme();
        for (int len = max_tiles; len >= 2; len--) {
            Shape s = {.n = 0};
            for (int i = 1; i <= len; i++) sh_cell(&s, x + dx * i, y + dy * i, 0);
            int got = add_blocks(&s, th, 0);
            if (got) return got;
        }
    }
    return 0;
}

/* A cluster: somewhere free, or (attached) right against a wall / block already there. */
static int add_cluster(int sym, bool attached) {
    Theme th = roll_theme();
    int w, h;
    switch (irand(0, attached ? 3 : 4)) {
    case 0: w = irand(3, 6), h = 1; break;           /* row */
    case 1: w = 1, h = irand(3, 5); break;           /* column */
    case 2: w = irand(2, 3), h = irand(2, 3); break; /* blob */
    case 3: w = irand(2, 4), h = 2; break;           /* thick row */
    default: w = h = 1; break;                       /* single */
    }
    if (th == TH_BARRELS && w * h > 4) h = 1; /* a whole depot of barrels would be a death trap */
    int x = irand(BLOCK_EDGE, GW - BLOCK_EDGE - w), y = irand(BLOCK_EDGE, GH - BLOCK_EDGE - h);
    if (attached) { /* slide it until it leans on something solid */
        bool found = false;
        for (int tries = 0; tries < 30 && !found; tries++) {
            int ox = irand(0, GW - 1), oy = irand(0, GH - 1), k = irand(0, 3);
            if (cells[oy][ox] == C_FREE) continue;
            if (k == 0) x = ox + 1, y = oy - irand(0, h - 1);
            else if (k == 1) x = ox - w, y = oy - irand(0, h - 1);
            else if (k == 2) x = ox - irand(0, w - 1), y = oy + 1;
            else x = ox - irand(0, w - 1), y = oy - h;
            found = true;
        }
    }
    Shape s = {.n = 0};
    sh_rect(&s, x, y, w, h);
    return add_blocks(&s, th, sym);
}

static void gen_blocks(int sym) {
    int budget = g_dense_blocks ? 200 : irand(6, 16);
    int walls = 0;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) walls += cells[y][x] == C_WALL;
    if (walls == 0) budget += 8; /* open floor */
    int placed = 0;
    for (int i = 0; i < npending; i++) /* barricade gates first */
        placed += add_blocks(&pending_gates[i], chance(0.7f) ? TH_CRATES : roll_theme(), 0);
    int gates = g_dense_blocks ? 40 : irand(2, 6);
    for (int i = 0; i < gates && placed < budget; i++) placed += add_gate(irand(2, 5));
    for (int tries = 0; tries < 400 && placed < budget; tries++) placed += add_cluster(sym, chance(0.6f));
}

/* ---- From cells to obstacles ---------------------------------------------- */

static void add_obstacle(int x, int y, int w, int h, int kind) {
    if (G.room.nobs >= MAX_OBSTACLES) return;
    Obstacle *o = &G.room.obs[G.room.nobs++];
    memset(o, 0, sizeof *o);
    o->r = (SDL_FRect){ARENA_X0 + (float)x * GRID, ARENA_Y0 + (float)y * GRID, (float)w * GRID, (float)h * GRID};
    o->kind = (uint8_t)kind;
    o->hp = o->max_hp = kind == OB_WALL ? 1.0f : BLOCK_HP[kind];
    o->swing_id = -1;
}

/* Walls: the cells merged into big rects (as wide, then as tall as they go). Blocks: one each. */
static void grid_to_obstacles(void) {
    static uint8_t used[GH][GW];
    memset(used, 0, sizeof used);
    G.room.nobs = 0;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            if (cells[y][x] != C_WALL || used[y][x]) continue;
            int w = 1, h = 1;
            while (x + w < GW && cells[y][x + w] == C_WALL && !used[y][x + w]) w++;
            for (bool grow = true; grow && y + h < GH;) {
                for (int i = 0; i < w && grow; i++) grow = cells[y + h][x + i] == C_WALL && !used[y + h][x + i];
                if (grow) h++;
            }
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++) used[y + j][x + i] = 1;
            add_obstacle(x, y, w, h, OB_WALL);
        }
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            if (cells[y][x] == C_BLOCK) add_obstacle(x, y, 1, 1, cell_kind[y][x]);
}

void room_gen_layout(void) {
    for (int attempt = 0; attempt < 30; attempt++) {
        grid_reset();
        int sym = attempt < 25 ? gen_walls() : 1; /* last attempts: blocks only */
        gen_blocks(sym);
        if (grid_connected()) break;
    }
    grid_to_obstacles();
    G.room.nfires = 0;
    room_nav_reset();
}

/* ---- Damage -------------------------------------------------------------- */

static SDL_FColor block_color(int kind) {
    switch (kind) {
    case OB_BARREL: return col_hex(0xff4a3a, 1);
    case OB_OIL: return col_hex(0xffa830, 1);
    case OB_SPARK: return col_hex(0xc27dff, 1);
    case OB_NEST: return col_hex(0x8cff4a, 1);
    default: return col_hex(0xd89a4a, 1);
    }
}

void room_block_ignite(int i) {
    if (i < 0 || i >= G.room.nobs || G.room.cleared) return;
    Obstacle *o = &G.room.obs[i];
    if (!obstacle_flammable(o) || o->hp <= 0.0f || o->burn_t > 0.0f) return;
    o->burn_t = 1e-4f;
    sfx_play_at(SFX_IGNITE, rect_center(o->r));
}

/* Edge to edge (blocks are grid cells: never touching by a corner only). */
static bool blocks_touch(const Obstacle *a, const Obstacle *b) { return rect_dist(rect_center(a->r), b->r) <= a->r.w * 0.5f + 1.0f; }

/* Blocks on fire burn on, set their flammable neighbours on fire, then go off; burning enemies
 * light up the flammable blocks they brush against. */
static void blocks_burn(void) {
    Room *r = &G.room;
    if (r->cleared) return;
    int spread[MAX_OBSTACLES], n = 0;
    for (int i = 0; i < r->nobs; i++) {
        Obstacle *o = &r->obs[i];
        if (o->burn_t <= 0.0f || o->hp <= 0.0f) continue;
        float before = o->burn_t;
        o->burn_t += DT;
        if (before < BLOCK_SPREAD_T && o->burn_t >= BLOCK_SPREAD_T) spread[n++] = i;
        if (o->burn_t >= BLOCK_BURN_TIME) o->hp = 0.0f; /* goes off with the others below */
        if ((G.ticks + i) % 3 == 0) {
            V2 p = v2(frand(o->r.x + 8.0f, o->r.x + o->r.w - 8.0f), frand(o->r.y + 8.0f, o->r.y + o->r.h - 8.0f));
            fx_particle(p, v2(frand(-25, 25), frand(-150, -70)), frand(0.3f, 0.55f), frand(7, 13),
                        col_hex(chance(0.5f) ? 0xffb030 : 0xff5a10, 0.85f), 0.0f, PT_GLOW);
        }
    }
    for (int k = 0; k < n; k++)
        for (int j = 0; j < r->nobs; j++)
            if (j != spread[k] && obstacle_flammable(&r->obs[j]) && blocks_touch(&r->obs[spread[k]], &r->obs[j])) room_block_ignite(j);
    if (G.ticks % 4 == 0)
        for (int e = 0; e < G.nen; e++) {
            const Enemy *en = &G.en[e];
            if (en->burn_t <= 0.0f || en->hp <= 0.0f) continue;
            for (int j = 0; j < r->nobs; j++)
                if (obstacle_flammable(&r->obs[j]) && rect_dist(en->pos, r->obs[j].r) < en->radius + 4.0f) room_block_ignite(j);
        }
}

void room_block_damage(int i, float dmg) {
    if (i < 0 || i >= G.room.nobs) return;
    Obstacle *o = &G.room.obs[i];
    if (G.room.cleared || !obstacle_breakable(o) || o->hp <= 0.0f || !(dmg > 0.0f)) return; /* shattering: no more blasts */
    o->hp -= dmg;
    o->flash = 0.08f;
    if (o->hp <= 0.0f) o->hp = 0.0f; /* goes off at the next blocks_update() */
    else if ((G.ticks + i) % 3 == 0) sfx_play_at(SFX_THUD, rect_center(o->r));
}

void room_blocks_blast(V2 pos, float radius, float dmg) {
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if (obstacle_breakable(o) && o->hp > 0.0f && rect_dist(pos, o->r) < radius) room_block_damage(i, dmg);
    }
}

void room_blocks_break_all(void) {
    for (int i = 0; i < G.room.nobs; i++)
        if (obstacle_breakable(&G.room.obs[i])) G.room.obs[i].hp = 0.0f;
}

int room_count_obstacles(int kind) {
    int n = 0;
    for (int i = 0; i < G.room.nobs; i++) {
        const Obstacle *o = &G.room.obs[i];
        if (obstacle_solid(o) && (kind < 0 ? obstacle_breakable(o) : o->kind == kind)) n++;
    }
    return n;
}

/* ---- Burning oil --------------------------------------------------------- */

static float fire_strength(const FirePool *f) {
    float in = fminf(1.0f, (f->life - f->t) / 0.25f), out = fminf(1.0f, f->t / 1.2f);
    return fmaxf(0.0f, fminf(in, out));
}

static void fire_add(V2 c) {
    Room *r = &G.room;
    FirePool *f = NULL;
    if (r->nfires < MAX_FIRES) f = &r->fires[r->nfires++];
    else { /* replace the one closest to dying out */
        f = &r->fires[0];
        for (int i = 1; i < r->nfires; i++)
            if (r->fires[i].t < f->t) f = &r->fires[i];
    }
    f->pos = c;
    f->r = FIRE_POOL_R;
    f->t = f->life = FIRE_POOL_TIME;
}

bool fire_pool_at(V2 p, float r) {
    for (int i = 0; i < G.room.nfires; i++) {
        const FirePool *f = &G.room.fires[i];
        if (fire_strength(f) > 0.3f && v2_dist(p, f->pos) < f->r * 0.85f + r) return true;
    }
    return false;
}

static void fires_update(void) {
    Room *r = &G.room;
    Player *pl = &G.pl;
    for (int i = 0; i < r->nfires; i++) {
        FirePool *f = &r->fires[i];
        if ((f->t -= DT) <= 0.0f) {
            r->fires[i--] = r->fires[--r->nfires];
            continue;
        }
        float s = fire_strength(f);
        if ((G.ticks + i) % 3 == 0) { /* embers */
            V2 p = v2_add(f->pos, v2_scale(v2_angle(frand(0, TAU_F)), frand(0, f->r * 0.85f)));
            fx_particle(p, v2(frand(-20, 20), frand(-120, -50)), frand(0.3f, 0.6f), frand(5, 10),
                        col_hex(chance(0.5f) ? 0xffb030 : 0xff5a10, 0.8f * s), 0.0f, PT_GLOW);
        }
        if (s < 0.3f || r->cleared) continue;
        /* Enemies standing in it catch fire (not the ones in the air / faded out). */
        for (int k = 0; k < G.nen; k++) {
            Enemy *e = &G.en[k];
            if (e->hp <= 0.0f || enemy_untouchable(e) || v2_dist(e->pos, f->pos) > f->r * 0.85f + e->radius * 0.5f) continue;
            e->burn_dps = fmaxf(e->burn_t > 0.0f ? e->burn_dps : 0.0f, 14.0f);
            e->burn_t = fmaxf(e->burn_t, 2.0f);
        }
        /* Crates, barrels and drums in it catch fire; the other blocks slowly burn down. */
        for (int k = 0; k < r->nobs; k++) {
            Obstacle *o = &r->obs[k];
            if (!obstacle_breakable(o) || o->hp <= 0.0f || rect_dist(f->pos, o->r) >= f->r * 0.8f) continue;
            if (obstacle_flammable(o)) {
                room_block_ignite(k);
            } else {
                o->hp -= 10.0f * DT;
                if (o->hp <= 0.0f) o->hp = 0.0f;
            }
        }
    }
    /* The ship catches fire walking in (a dash jumps over), and burns on for a moment. */
    if (G.state == GS_PLAYING && !r->cleared && pl->dash_t <= 0.0f && fire_pool_at(pl->pos, PLAYER_BODY_R * 0.4f)) {
        if (pl->burn_t <= 0.0f) {
            pl->burn_tick = 0.0f;
            sfx_play_at(SFX_IGNITE, pl->pos);
        }
        pl->burn_t = 1.4f;
    }
    if (pl->burn_t > 0.0f) {
        pl->burn_t -= DT;
        if ((pl->burn_tick -= DT) <= 0.0f) {
            player_hurt(1);
            pl->burn_tick = 1.0f;
        }
        if (G.ticks % 2 == 0)
            fx_particle(v2_add(pl->pos, v2(frand(-10, 10), frand(-8, 8))), v2(frand(-30, 30), frand(-160, -80)), 0.35f,
                        frand(6, 11), col_hex(chance(0.5f) ? 0xffb030 : 0xff5a10, 0.9f), 0.0f, PT_GLOW);
    }
}

/* ---- Breaking ------------------------------------------------------------ */

static void block_goes_off(V2 c, int kind) {
    SDL_FColor col = block_color(kind);
    fx_burst(c, 16, 80.0f, 420.0f, 0.45f, 3.5f, col);
    fx_particle(c, v2(0, 0), 0.2f, 60.0f, col_a(col, 0.6f), 0.0f, PT_GLOW);
    switch (kind) {
    case OB_BARREL: {
        explode_at(c, BARREL_BLAST_R, 60.0f, NULL, 0xff6a2a); /* enemies, and the blocks around: chains */
        fx_explosion(c, BARREL_BLAST_R, col_hex(0xff6a2a, 1));
        fx_shake(0.45f);
        sfx_play_at(SFX_EXPLODE_BIG, c);
        if (v2_dist(G.pl.pos, c) < BARREL_BLAST_R + PLAYER_BODY_R * 0.5f) player_hurt(1);
    } break;
    case OB_OIL:
        fire_add(c);
        fx_burst(c, 24, 100.0f, 300.0f, 0.5f, 5.0f, col_hex(0xff7a20, 1));
        sfx_play_at(SFX_IGNITE, c);
        break;
    case OB_SPARK: {
        float off = frand(0, TAU_F);
        for (int k = 0; k < SPARK_BALLS; k++) {
            EBullet *b = ebullet_spawn(c, off + TAU_F * (float)k / (float)SPARK_BALLS, 250.0f, 9.0f, col_hex(0xd9a6ff, 1));
            if (!b) break;
            b->kind = EB_BALL;
            b->life = 2.4f;
        }
        fx_particle(c, v2(0, 0), 0.3f, 120.0f, col_hex(0xc27dff, 0.9f), 0.0f, PT_RING);
        sfx_play_at(SFX_ENEMY_RING, c);
    } break;
    case OB_NEST: /* something crawls out (a short warning first, like any spawn) */
        enemy_queue_spawn(enemy_hatchling(G.room.depth), c, 0.35f);
        fx_burst(c, 18, 60.0f, 260.0f, 0.5f, 4.0f, col_hex(0x8cff4a, 1));
        sfx_play_at(SFX_SPLAT, c);
        break;
    default:
        sfx_play_at(SFX_CRATE, c);
        break;
    }
}

/* ---- The room shatters --------------------------------------------------- */

static V2 shatter_origin;

void room_shatter_start(V2 origin) {
    Room *r = &G.room;
    shatter_origin = origin;
    float far = 1.0f;
    for (int i = 0; i < r->nobs; i++) far = fmaxf(far, rect_dist(origin, r->obs[i].r));
    for (int i = 0; i < r->nobs; i++) /* the closest crack first: a wave running out from the last kill */
        r->obs[i].shatter_t = CRACK_TIME + SHATTER_WAVE_T * rect_dist(origin, r->obs[i].r) / far;
    for (int i = 0; i < r->nfires; i++) r->fires[i].t = fminf(r->fires[i].t, 0.5f); /* the fire dies down */
    G.pl.burn_t = 0.0f;
    if (r->nobs) sfx_play(SFX_CRACKLE);
}

void room_obstacles_vanish(void) {
    G.room.nobs = 0;
    G.room.nfires = 0;
    room_nav_reset();
}

/* Bursts into shards of glass that spin away and fade (no blast, no oil, no balls). */
static void shatter(const Obstacle *o) {
    SDL_FRect r = o->r;
    V2 c = rect_center(r), push = v2_sub(c, shatter_origin);
    push = v2_len2(push) > 1.0f ? v2_norm(push) : v2(0, -1);
    SDL_FColor col = o->kind == OB_WALL ? col_hsv(G.room.hue, 0.6f, 1.0f, 0.95f) : block_color(o->kind);
    int n = (int)clampf(r.w * r.h / (15.0f * 15.0f), 10.0f, 44.0f);
    for (int k = 0; k < n; k++) {
        V2 p = v2(frand(r.x, r.x + r.w), frand(r.y, r.y + r.h));
        V2 out = v2_sub(p, c);
        out = v2_len2(out) > 1.0f ? v2_norm(out) : v2_angle(frand(0, TAU_F));
        V2 vel = v2_add(v2_scale(out, frand(110.0f, 420.0f)), v2_scale(push, frand(60.0f, 240.0f)));
        fx_shard(p, vel, frand(0.32f, 0.44f), frand(7.0f, 18.0f), k % 3 == 0 ? col_a(COL_WHITE, 0.95f) : col, frand(-16.0f, 16.0f));
    }
    fx_burst(c, n / 2, 150.0f, 520.0f, 0.3f, 2.2f, col_a(COL_WHITE, 0.9f)); /* glints */
    fx_particle(c, v2(0, 0), 0.22f, fmaxf(r.w, r.h) * 1.2f + 30.0f, col_a(COL_WHITE, 0.7f), 0.0f, PT_GLOW);
    fx_particle(c, v2(0, 0), 0.32f, fmaxf(r.w, r.h) * 1.6f + 40.0f, col_a(col, 0.45f), 0.0f, PT_GLOW);
    sfx_play_at(SFX_SHATTER, c);
}

void blocks_update(void) {
    Room *r = &G.room;
    struct {
        V2 c;
        int kind;
    } gone[MAX_OBSTACLES];
    int ngone = 0, w = 0, burst = 0;
    blocks_burn();
    for (int i = 0; i < r->nobs; i++) {
        Obstacle *o = &r->obs[i];
        if (o->flash > 0.0f) o->flash -= DT;
        if (o->shatter_t > 0.0f && (o->shatter_t -= DT) <= 0.0f) { /* the room is cleared: glass */
            shatter(o);
            burst++;
            continue;
        }
        if (obstacle_breakable(o) && o->hp <= 0.0f && !r->cleared) {
            gone[ngone].c = rect_center(o->r);
            gone[ngone++].kind = o->kind;
            continue;
        }
        r->obs[w++] = *o;
    }
    if (ngone || burst) {
        r->nobs = w;
        room_nav_reset(); /* the paths changed */
        for (int i = 0; i < ngone; i++) block_goes_off(gone[i].c, gone[i].kind);
    }
    fires_update();
}

/* ---- Drawing ------------------------------------------------------------- */

void fires_draw(void) {
    for (int i = 0; i < G.room.nfires; i++) {
        const FirePool *f = &G.room.fires[i];
        float s = fire_strength(f);
        draw_circle(f->pos, f->r, col_hex(0x1a0800, 0.6f * fmaxf(s, 0.4f)));
        draw_blend_add();
        draw_glow(f->pos, f->r * 1.25f, col_hex(0xff5a10, 0.35f * s));
        for (int k = 0; k < 10; k++) {
            float a = (float)k * 0.63f + G.time * 0.7f * ((k & 1) ? -1.0f : 1.0f) + (float)i;
            float rr = f->r * (0.15f + 0.6f * fmodf((float)k * 0.37f + (float)i * 0.11f, 1.0f));
            float flick = 0.5f + 0.5f * sinf(G.time * 9.0f + (float)k * 1.7f + (float)i);
            V2 p = v2_add(f->pos, v2_scale(v2_angle(a), rr));
            draw_glow(p, 14.0f + 12.0f * flick, col_hex((k & 1) ? 0xffb030 : 0xff6a10, (0.25f + 0.45f * flick) * s));
        }
        draw_blend_alpha();
    }
}

static void draw_cracks(const Obstacle *o, int seed) {
    float dmg = 1.0f - o->hp / o->max_hp;
    if (dmg < 0.2f) return;
    int n = 1 + (int)(dmg * 4.0f);
    SDL_FRect r = o->r;
    V2 c = rect_center(r);
    for (int k = 0; k < n; k++) {
        float a = (float)((seed * 7 + k * 13) % 17) / 17.0f * TAU_F;
        V2 m = v2_add(c, v2_scale(v2_angle(a), r.w * 0.22f));
        V2 e = v2_add(c, v2_scale(v2_angle(a + 0.4f), r.w * 0.48f));
        draw_line(c, m, 2.0f, rgba(0, 0, 0, 0.7f));
        draw_line(m, e, 2.0f, rgba(0, 0, 0, 0.7f));
    }
}

static void draw_block(const Obstacle *o, int idx) {
    SDL_FRect r = o->r;
    V2 c = rect_center(r);
    float s = r.w, dmg = 1.0f - o->hp / o->max_hp;
    SDL_FColor edge = block_color(o->kind);
    draw_rect(r.x + 5, r.y + 7, r.w, r.h, rgba(0, 0, 0, 0.35f)); /* drop shadow */
    switch (o->kind) {
    case OB_CRATE:
        draw_rect(r.x, r.y, r.w, r.h, col_hex(0x3a2410, 1));
        draw_rect(r.x + 5, r.y + 5, r.w - 10, r.h - 10, col_hex(0x5c3a1a, 1));
        draw_line(v2(r.x + 6, r.y + 6), v2(r.x + r.w - 6, r.y + r.h - 6), 4.0f, col_hex(0x9a6a35, 1));
        draw_line(v2(r.x + r.w - 6, r.y + 6), v2(r.x + 6, r.y + r.h - 6), 4.0f, col_hex(0x9a6a35, 1));
        break;
    case OB_BARREL: {
        draw_rect(r.x, r.y, r.w, r.h, col_hex(0x2a0808, 1));
        draw_circle(c, s * 0.42f, col_hex(0xb02020, 1));
        draw_ring(c, s * 0.3f, 2.0f, col_hex(0x5a0a0a, 1));
        V2 a = v2(c.x, c.y - s * 0.2f), b = v2(c.x - s * 0.18f, c.y + s * 0.14f), d = v2(c.x + s * 0.18f, c.y + s * 0.14f);
        draw_tri(a, b, d, col_hex(0xffd23d, 1));
        draw_rect(c.x - 1.5f, c.y - s * 0.08f, 3.0f, s * 0.12f, col_hex(0x2a0808, 1));
        if (dmg > 0.0f) { /* about to blow: it glows */
            draw_blend_add();
            draw_glow(c, s * (0.6f + 0.3f * sinf(G.time * 18.0f)), col_hex(0xff3a1a, 0.5f * dmg));
            draw_blend_alpha();
        }
    } break;
    case OB_OIL:
        draw_rect(r.x, r.y, r.w, r.h, col_hex(0x241605, 1));
        draw_circle(c, s * 0.42f, col_hex(0x5a3c0c, 1));
        draw_circle(c, s * 0.26f, col_hex(0xffa020, 0.9f));
        draw_circle(v2(c.x - s * 0.07f, c.y - s * 0.07f), s * 0.08f, col_hex(0xffe08a, 0.9f)); /* shine */
        break;
    case OB_NEST: { /* a pulsing egg sac, something moving inside */
        float beat = 0.5f + 0.5f * sinf(G.time * 4.0f + (float)idx);
        draw_rect(r.x, r.y, r.w, r.h, col_hex(0x0e1a08, 1));
        draw_circle(c, s * (0.38f + 0.03f * beat), col_hex(0x2f5a14, 1));
        draw_circle(c, s * 0.28f, col_hex(0x5a9a22, 1));
        draw_blend_add();
        draw_glow(c, s * (0.45f + 0.1f * beat), col_hex(0x8cff4a, 0.18f + 0.2f * beat + 0.3f * dmg));
        draw_blend_alpha();
        V2 eye = v2_add(c, v2_scale(v2_angle(G.time * 1.7f + (float)idx), s * 0.08f)); /* the thing inside */
        draw_circle(eye, s * 0.09f, col_hex(0x0e1a08, 0.9f));
        draw_circle(v2_add(eye, v2(s * 0.03f, -s * 0.03f)), s * 0.03f, col_hex(0xe8ffb0, 1));
        for (int k = 0; k < 4; k++) { /* veins */
            float a = (float)k * TAU_F / 4.0f + 0.4f;
            draw_line(v2_add(c, v2_scale(v2_angle(a), s * 0.2f)), v2_add(c, v2_scale(v2_angle(a + 0.3f), s * 0.38f)), 2.0f, col_hex(0x1e3a0c, 1));
        }
    } break;
    default: { /* spark pod */
        draw_rect(r.x, r.y, r.w, r.h, col_hex(0x180a28, 1));
        draw_blend_add();
        draw_glow(c, s * 0.5f, col_hex(0xb070ff, 0.35f + 0.25f * dmg));
        draw_blend_alpha();
        draw_circle(c, s * 0.13f, col_hex(0xe6c8ff, 1));
        for (int k = 0; k < 8; k++)
            draw_circle(v2_add(c, v2_scale(v2_angle(G.time * 1.2f + (float)k * TAU_F / 8.0f), s * 0.32f)), 3.2f, col_hex(0xd9a6ff, 1));
    } break;
    }
    draw_cracks(o, idx + (int)r.x);
    if (o->burn_t > 0.0f) { /* on fire: charring, flames licking up */
        float k = clampf(o->burn_t / BLOCK_BURN_TIME, 0.0f, 1.0f);
        draw_rect(r.x, r.y, r.w, r.h, rgba(0.05f, 0.02f, 0.0f, 0.55f * k));
        draw_blend_add();
        draw_glow(c, s * (0.75f + 0.15f * sinf(G.time * 13.0f + (float)idx)), col_hex(0xff5a10, 0.35f + 0.3f * k));
        for (int f = 0; f < 5; f++) {
            float flick = 0.5f + 0.5f * sinf(G.time * (9.0f + (float)f) + (float)(idx * 3 + f));
            V2 p = v2(r.x + r.w * (0.15f + 0.175f * (float)f), r.y + r.h * (0.75f - 0.45f * flick));
            draw_glow(p, s * (0.16f + 0.1f * flick), col_hex((f & 1) ? 0xffb030 : 0xff6a10, 0.5f + 0.4f * flick));
        }
        draw_blend_alpha();
    }
    draw_rect_outline(r.x, r.y, r.w, r.h, 2.0f, edge);
    if (o->flash > 0.0f) draw_rect(r.x, r.y, r.w, r.h, col_a(COL_WHITE, 0.55f * o->flash / 0.08f));
}

/* Cracking before it bursts: white cracks running across it, a growing glare. */
static void draw_shatter_cracks(const Obstacle *o, int seed) {
    if (o->shatter_t <= 0.0f || o->shatter_t > CRACK_TIME) return;
    float k = 1.0f - o->shatter_t / CRACK_TIME;
    SDL_FRect r = o->r;
    V2 c = rect_center(r);
    float reach = 0.5f * sqrtf(r.w * r.w + r.h * r.h) * k;
    draw_blend_add();
    draw_rect(r.x, r.y, r.w, r.h, col_a(COL_WHITE, 0.35f * k * k));
    for (int i = 0; i < 6; i++) {
        float a = (float)((seed * 5 + i * 11) % 23) / 23.0f * TAU_F;
        V2 m = v2_add(c, v2_scale(v2_angle(a), reach * 0.5f));
        V2 e = v2_add(c, v2_scale(v2_angle(a + ((i & 1) ? 0.5f : -0.5f)), reach));
        e.x = clampf(e.x, r.x, r.x + r.w), e.y = clampf(e.y, r.y, r.y + r.h);
        m.x = clampf(m.x, r.x, r.x + r.w), m.y = clampf(m.y, r.y, r.y + r.h);
        draw_line(c, m, 2.0f, col_a(COL_WHITE, 0.9f));
        draw_line(m, e, 1.5f, col_a(COL_WHITE, 0.8f));
    }
    draw_blend_alpha();
}

/* A wall rect on whole grid cells (every generated wall): its cells, false otherwise. */
static bool wall_cells(SDL_FRect o, int *x0, int *y0, int *x1, int *y1) {
    float fx = (o.x - ARENA_X0) / GRID, fy = (o.y - ARENA_Y0) / GRID, fw = o.w / GRID, fh = o.h / GRID;
    *x0 = (int)lroundf(fx), *y0 = (int)lroundf(fy), *x1 = *x0 + (int)lroundf(fw), *y1 = *y0 + (int)lroundf(fh);
    return fabsf(fx - (float)*x0) < 0.01f && fabsf(fy - (float)*y0) < 0.01f && fabsf(fw - roundf(fw)) < 0.01f &&
           fabsf(fh - roundf(fh)) < 0.01f && *x0 >= 0 && *y0 >= 0 && *x1 <= GW && *y1 <= GH && *x1 > *x0 && *y1 > *y0;
}

void blocks_draw(SDL_FColor wall, SDL_FColor edge) {
    const Room *r = &G.room;
    SDL_FColor panel = col_mul(wall, 1.35f);
    /* Walls drawn cell by cell from one map of the wall cells, so walls side by side (an L, two
     * pieces that merged) read as a single shape: the rim only runs along the sides facing the
     * floor, and the inner corners get their bit of rim. Off-grid walls (tests) are drawn alone. */
    static uint8_t cell[GH][GW];
    memset(cell, 0, sizeof cell);
    for (int i = 0; i < r->nobs; i++) {
        const Obstacle *ob = &r->obs[i];
        if (ob->kind != OB_WALL) continue;
        SDL_FRect o = ob->r;
        int x0, y0, x1, y1;
        draw_rect(o.x + 6, o.y + 8, o.w, o.h, rgba(0, 0, 0, 0.35f)); /* drop shadow (wall rects never overlap) */
        if (wall_cells(o, &x0, &y0, &x1, &y1)) {
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) cell[y][x] = 1;
        } else {
            draw_rect(o.x, o.y, o.w, o.h, edge);
            draw_rect(o.x + 3, o.y + 3, o.w - 6, o.h - 6, wall);
            if (o.w > 14 && o.h > 14) draw_rect(o.x + 7, o.y + 7, o.w - 14, o.h - 14, panel);
        }
    }
#define WC(x, y) ((x) >= 0 && (y) >= 0 && (x) < GW && (y) < GH && cell[y][x])
    /* rim, then the dark band (inset 3 px from the floor), then the lighter panel (inset 7 px) */
    const float inset[3] = {0.0f, 3.0f, 7.0f};
    const SDL_FColor fill[3] = {edge, wall, panel};
    for (int pass = 0; pass < 3; pass++)
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) {
                if (!cell[y][x]) continue;
                float k = inset[pass], cx = ARENA_X0 + (float)x * GRID, cy = ARENA_Y0 + (float)y * GRID;
                /* a pixel over into a wall neighbour: no seam once scaled to the window */
                float l = WC(x - 1, y) ? -1.0f : k, t = WC(x, y - 1) ? -1.0f : k;
                float rr = WC(x + 1, y) ? -1.0f : k, b = WC(x, y + 1) ? -1.0f : k;
                draw_rect(cx + l, cy + t, GRID - l - rr, GRID - t - b, fill[pass]);
            }
    /* Inner corners (two wall neighbours, the diagonal one free): the rim turns the corner. */
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            if (!cell[y][x]) continue;
            float cx = ARENA_X0 + (float)x * GRID, cy = ARENA_Y0 + (float)y * GRID;
            for (int dy = -1; dy <= 1; dy += 2)
                for (int dx = -1; dx <= 1; dx += 2) {
                    if (!WC(x + dx, y) || !WC(x, y + dy) || WC(x + dx, y + dy)) continue;
                    float px = dx < 0 ? cx : cx + GRID, py = dy < 0 ? cy : cy + GRID;
                    draw_rect(dx < 0 ? px : px - 7.0f, dy < 0 ? py : py - 7.0f, 7.0f, 7.0f, wall);
                    draw_rect(dx < 0 ? px : px - 3.0f, dy < 0 ? py : py - 3.0f, 3.0f, 3.0f, edge);
                }
        }
#undef WC
    for (int i = 0; i < r->nobs; i++)
        if (r->obs[i].kind != OB_WALL && obstacle_solid(&r->obs[i])) draw_block(&r->obs[i], i);
    for (int i = 0; i < r->nobs; i++) draw_shatter_cracks(&r->obs[i], i + (int)r->obs[i].r.x);
}
