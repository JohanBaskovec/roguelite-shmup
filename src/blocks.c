#include "audio.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * Room layouts and blocks.
 *
 * A layout is a set of indestructible walls (one of several patterns: pillars, bars, L brackets,
 * halls, a small maze, a field of posts... often mirrored) plus breakable blocks: crates, explosive
 * barrels, oil drums (a pool of burning oil when broken) and spark pods (a ring of balls).
 * Blocks often extend the walls ("gates"), closing the gaps between them: the way to a door may be
 * blocked, but only by something the ship can shoot down. Walls keep 110 px gaps between them and
 * 120 px off the arena edges, so they can never wall a part of the room off; room_walls_connected()
 * (a flood fill where only walls count) double-checks every layout.
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

#define EDGE_MARGIN 80.0f       /* blocks stay off the arena edges (crawlers live there) */
#define WALL_EDGE_MARGIN 120.0f /* walls even more: no tight corridor along the edge */
#define WALL_GAP 110.0f         /* between walls: room to dodge, and the bot's path finder sees the passage */

#define ARENA_W (ARENA_X1 - ARENA_X0)
#define ARENA_H (ARENA_Y1 - ARENA_Y0)

static const float BLOCK_HP[OB_KIND_COUNT] = {
    [OB_CRATE] = 28.0f,  /* 4 basic shots */
    [OB_BARREL] = 18.0f, /* 3 */
    [OB_OIL] = 20.0f,
    [OB_SPARK] = 20.0f,
};

static bool rect_overlaps(SDL_FRect a, SDL_FRect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

static SDL_FRect grow(SDL_FRect r, float m) { return (SDL_FRect){r.x - m, r.y - m, r.w + 2 * m, r.h + 2 * m}; }

static SDL_FRect rect_c(float cx, float cy, float w, float h) { return (SDL_FRect){cx - w * 0.5f, cy - h * 0.5f, w, h}; }

static V2 rect_center(SDL_FRect r) { return v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f); }

static float rect_dist(V2 p, SDL_FRect r) {
    float dx = p.x - clampf(p.x, r.x, r.x + r.w), dy = p.y - clampf(p.y, r.y, r.y + r.h);
    return sqrtf(dx * dx + dy * dy);
}

static bool in_arena(SDL_FRect r, float m) {
    return r.x >= ARENA_X0 + m && r.y >= ARENA_Y0 + m && r.x + r.w <= ARENA_X1 - m && r.y + r.h <= ARENA_Y1 - m;
}

/* The cleared room is emptied before its reward shows up: the layout only leaves the ship room
 * where it arrives (and comes back to after the fight), by the entry door - the secret spot too. */
static bool hits_room_spots(SDL_FRect r) {
    V2 e = room_entry_spot();
    return rect_overlaps(r, rect_c(e.x, e.y, 400, 400));
}

static bool wall_rect_ok(SDL_FRect r, int first_of_piece) {
    if (!in_arena(r, WALL_EDGE_MARGIN) || hits_room_spots(r)) return false;
    /* Pieces keep their distance; the rects of one piece may touch. */
    for (int i = 0; i < first_of_piece; i++)
        if (rect_overlaps(r, grow(G.room.obs[i].r, WALL_GAP))) return false;
    return true;
}

typedef struct {
    SDL_FRect r[4];
    int n;
} Piece;

static Piece piece1(SDL_FRect a) { return (Piece){{a}, 1}; }

/* An L: corner square at (x, y), one arm along x (sign sx), one along y (sign sy). */
static Piece piece_l(float x, float y, float sx, float sy, float a, float b, float t) {
    Piece p = {.n = 2};
    p.r[0] = (SDL_FRect){sx > 0 ? x : x - a, y - t * 0.5f, a, t};
    p.r[1] = (SDL_FRect){x - t * 0.5f, sy > 0 ? y : y - b, t, b};
    return p;
}

static Piece piece_t(float cx, float cy, float len, float stem, float t, bool vertical, float dir) {
    Piece p = {.n = 2};
    if (!vertical) {
        p.r[0] = rect_c(cx, cy, len, t);
        p.r[1] = (SDL_FRect){cx - t * 0.5f, dir > 0 ? cy : cy - stem, t, stem};
    } else {
        p.r[0] = rect_c(cx, cy, t, len);
        p.r[1] = (SDL_FRect){dir > 0 ? cx : cx - stem, cy - t * 0.5f, stem, t};
    }
    return p;
}

static Piece piece_plus(float cx, float cy, float len, float t) {
    return (Piece){{rect_c(cx, cy, len, t), rect_c(cx, cy, t, len)}, 2};
}

/* A U opening towards (ox, oy) (one of them is 0). */
static Piece piece_u(float cx, float cy, float w, float d, float t, float ox, float oy) {
    Piece p = {.n = 3};
    if (oy != 0.0f) {
        float by = oy > 0 ? cy - d * 0.5f : cy + d * 0.5f - t;
        p.r[0] = (SDL_FRect){cx - w * 0.5f, by, w, t};
        p.r[1] = (SDL_FRect){cx - w * 0.5f, cy - d * 0.5f, t, d};
        p.r[2] = (SDL_FRect){cx + w * 0.5f - t, cy - d * 0.5f, t, d};
    } else {
        float bx = ox > 0 ? cx - d * 0.5f : cx + d * 0.5f - t;
        p.r[0] = (SDL_FRect){bx, cy - w * 0.5f, t, w};
        p.r[1] = (SDL_FRect){cx - d * 0.5f, cy - w * 0.5f, d, t};
        p.r[2] = (SDL_FRect){cx - d * 0.5f, cy + w * 0.5f - t, d, t};
    }
    return p;
}

static Piece piece_mirror(Piece p, bool mx, bool my) {
    for (int i = 0; i < p.n; i++) {
        if (mx) p.r[i].x = 2.0f * ARENA_CX - p.r[i].x - p.r[i].w;
        if (my) p.r[i].y = 2.0f * ARENA_CY - p.r[i].y - p.r[i].h;
    }
    return p;
}

static bool add_piece(Piece p) {
    int first = G.room.nobs;
    if (first + p.n > MAX_OBSTACLES - 24) return false; /* room left for blocks */
    for (int i = 0; i < p.n; i++)
        if (!wall_rect_ok(p.r[i], first)) return false;
    for (int i = 0; i < p.n; i++) {
        Obstacle *o = &G.room.obs[G.room.nobs++];
        memset(o, 0, sizeof *o);
        o->r = p.r[i];
        o->kind = OB_WALL;
        o->hp = o->max_hp = 1.0f;
        o->swing_id = -1;
    }
    return true;
}

/* sym: 0 as is, 1 mirrored left / right, 2 top / bottom, 3 four ways. */
static void add_sym(Piece p, int sym) {
    add_piece(p);
    if (sym == 1 || sym == 3) add_piece(piece_mirror(p, true, false));
    if (sym == 2 || sym == 3) add_piece(piece_mirror(p, false, true));
    if (sym == 3) add_piece(piece_mirror(p, true, true));
}

static Piece random_piece(float cx, float cy) {
    float t = frand(36, 54);
    switch (irand(0, 5)) {
    case 0: return piece1(rect_c(cx, cy, frand(160, 320), t));
    case 1: return piece1(rect_c(cx, cy, t, frand(140, 280)));
    case 2: return piece_l(cx, cy, chance(0.5f) ? 1 : -1, chance(0.5f) ? 1 : -1, frand(120, 220), frand(110, 200), t);
    case 3: return piece_t(cx, cy, frand(180, 280), frand(80, 150), t, chance(0.5f), chance(0.5f) ? 1 : -1);
    case 4: return piece_plus(cx, cy, frand(130, 200), t);
    default: return piece1(rect_c(cx, cy, frand(60, 130), frand(60, 130)));
    }
}

/* Gaps left in walls on purpose, filled with blocks by gen_blocks (barricades). */
static SDL_FRect pending_gates[4];
static int npending;

/* Returns the symmetry used (blocks follow it). */
static int gen_walls(void) {
    const float W = ARENA_W, H = ARENA_H;
    int sym = 1;
    npending = 0;
    switch (irand(0, 12)) {
    case 0: /* open floor: the blocks make the room */
        break;
    case 1: /* four pillars */
        add_sym(piece1(rect_c(ARENA_X0 + W * 0.27f, ARENA_Y0 + H * 0.3f, 100, 100)), 3);
        break;
    case 2: /* two long vertical walls */
        add_sym(piece1(rect_c(ARENA_X0 + W * frand(0.28f, 0.36f), ARENA_CY, 50, frand(300, 420))), 1);
        break;
    case 3: { /* random pieces */
        sym = irand(0, 3);
        int n = irand(3, 6);
        for (int i = 0; i < n; i++) {
            float x1 = sym == 1 || sym == 3 ? ARENA_CX - 120 : ARENA_X1 - 160;
            float y1 = sym >= 2 ? ARENA_CY - 60 : ARENA_Y1 - 160;
            add_sym(random_piece(frand(ARENA_X0 + 160, x1), frand(ARENA_Y0 + 160, y1)), sym);
        }
    } break;
    case 4: /* horizontal bars */
        add_sym(piece1(rect_c(ARENA_X0 + W * 0.27f, ARENA_Y0 + H * 0.26f, frand(240, 320), 44)), 3);
        break;
    case 5: /* ring of pillars */
        for (int i = 0; i < 8; i++) {
            float a = TAU_F * ((float)i + 0.5f) / 8.0f;
            add_piece(piece1(rect_c(ARENA_CX + cosf(a) * 380.0f, ARENA_CY + sinf(a) * 260.0f, 64, 64)));
        }
        sym = 3;
        break;
    case 6: { /* four L brackets around the middle, open towards it or away from it */
        float dx = frand(300, 420), dy = frand(190, 250), s = chance(0.5f) ? 1.0f : -1.0f;
        add_sym(piece_l(ARENA_CX - dx, ARENA_CY - dy, s, s, frand(140, 220), frand(110, 170), 44), 3);
        sym = 3;
    } break;
    case 7: { /* halls: long walls splitting the room into lanes */
        float y = ARENA_CY - frand(225, 270), len = frand(380, 520);
        add_sym(piece1((SDL_FRect){ARENA_X0 + EDGE_MARGIN + frand(40, 140), y - 20, len, 40}), 3);
        sym = 3;
    } break;
    case 8: /* a small maze of short pieces */
        sym = chance(0.5f) ? 1 : 3;
        for (int cx = 0; cx < 3; cx++)
            for (int cy = 0; cy < 3; cy++) {
                if (chance(0.3f)) continue;
                float x = ARENA_X0 + 200 + (float)cx * 230 + frand(-30, 30), y = ARENA_Y0 + 180 + (float)cy * 280 + frand(-30, 30);
                if (sym == 3 && cy == 2) continue;
                add_sym(random_piece(x, y), sym);
            }
        break;
    case 9: { /* a field of posts */
        float sx = frand(180, 230), sy = frand(160, 200);
        for (float x = ARENA_X0 + 170; x < ARENA_CX - 60; x += sx)
            for (float y = ARENA_Y0 + 160; y < ARENA_Y1 - 120; y += sy)
                if (chance(0.65f)) add_sym(piece1(rect_c(x, y, 46, 46)), 1);
    } break;
    case 10: { /* crosses */
        int n = irand(2, 3);
        for (int i = 0; i < n; i++)
            add_sym(piece_plus(frand(ARENA_X0 + 220, ARENA_CX - 220), frand(ARENA_Y0 + 200, ARENA_Y1 - 200), frand(140, 210), 42), 1);
    } break;
    case 12: { /* barricades: long walls across the room, a breakable gate in each */
        float x = ARENA_X0 + W * frand(0.27f, 0.34f), t = 44.0f, gap = BLOCK_TILE * (float)irand(2, 3);
        float y0 = ARENA_Y0 + WALL_EDGE_MARGIN, y1 = ARENA_Y1 - WALL_EDGE_MARGIN, gy = frand(ARENA_CY - 160.0f, ARENA_CY + 160.0f - gap);
        Piece p = {.n = 2};
        p.r[0] = (SDL_FRect){x - t * 0.5f, y0, t, gy - y0};
        p.r[1] = (SDL_FRect){x - t * 0.5f, gy + gap, t, y1 - gy - gap};
        for (int m = 0; m < 2; m++) {
            Piece q = m ? piece_mirror(p, true, false) : p;
            if (add_piece(q) && npending < ARRAY_LEN(pending_gates))
                pending_gates[npending++] = (SDL_FRect){q.r[0].x + t * 0.5f - BLOCK_TILE * 0.5f, gy, BLOCK_TILE, gap};
        }
        sym = 1;
    } break;
    default: { /* chambers: U shapes, open to the middle or to the walls */
        float o = chance(0.5f) ? 1.0f : -1.0f;
        add_sym(piece_u(ARENA_X0 + W * 0.25f, ARENA_CY - H * 0.28f, frand(200, 280), frand(150, 200), 40, 0, o), 3);
        sym = 3;
    } break;
    }
    /* A few more pieces here and there (they keep their distance from the others). */
    int extra = irand(4, 8);
    for (int i = 0; i < 80 && extra > 0; i++) {
        bool half = sym == 1 || sym == 3;
        float x1 = half ? ARENA_CX - 120 : ARENA_X1 - 160, y1 = sym >= 2 ? ARENA_CY - 60 : ARENA_Y1 - 160;
        int before = G.room.nobs;
        add_sym(random_piece(frand(ARENA_X0 + 160, x1), frand(ARENA_Y0 + 160, y1)), sym);
        if (G.room.nobs > before) extra--;
    }
    return sym;
}

/* ---- Blocks -------------------------------------------------------------- */

/* Distance between two rects (0 when touching, < 0 when overlapping). */
static float rect_gap(SDL_FRect r, SDL_FRect o) {
    float dx = fmaxf(o.x - (r.x + r.w), r.x - (o.x + o.w)), dy = fmaxf(o.y - (r.y + r.h), r.y - (o.y + o.h));
    if (dx < -1.0f && dy < -1.0f) return -1.0f;
    return dx > 0.0f && dy > 0.0f ? sqrtf(dx * dx + dy * dy) : fmaxf(fmaxf(dx, dy), 0.0f);
}

static bool gap_ok(float gap) { return gap >= 0.0f && (gap <= 1.0f || gap >= WALL_GAP); }

static bool block_rect_ok(SDL_FRect r) {
    if (!in_arena(r, EDGE_MARGIN) || hits_room_spots(r)) return false;
    /* Blocks touch the walls / blocks next to them, or keep a real gap: a 40 px slit would be too
     * tight to fly through (and the path finder would see a way that is not there). */
    for (int i = 0; i < G.room.nobs; i++)
        if (!gap_ok(rect_gap(r, G.room.obs[i].r))) return false;
    return true;
}

typedef enum { TH_CRATES, TH_BARRELS, TH_OIL, TH_SPARK, TH_MIXED } Theme;

static Theme roll_theme(void) {
    float r = frand(0, 1);
    return r < 0.55f ? TH_CRATES : r < 0.68f ? TH_BARRELS : r < 0.80f ? TH_OIL : r < 0.88f ? TH_SPARK : TH_MIXED;
}

static int theme_kind(Theme th) {
    switch (th) {
    case TH_BARRELS: return chance(0.8f) ? OB_BARREL : OB_CRATE;
    case TH_OIL: return chance(0.8f) ? OB_OIL : OB_CRATE;
    case TH_SPARK: return chance(0.6f) ? OB_SPARK : OB_CRATE;
    case TH_MIXED: return irand(OB_CRATE, OB_SPARK);
    default: return chance(0.85f) ? OB_CRATE : irand(OB_BARREL, OB_SPARK);
    }
}

/* A group of tiles (a cluster, a gate...) and its mirror images, placed whole or not at all:
 * a half-placed row would leave slits between its pieces. */
typedef struct {
    float x[16], y[16];
    int kind[16], n;
} Group;

static int place_group(const Group *g, int sym) {
    SDL_FRect rs[64], box[4];
    int kinds[64], n = 0, copies = 0;
    for (int m = 0; m < 4; m++) {
        bool mx = m & 1, my = m & 2;
        if ((mx && sym != 1 && sym != 3) || (my && sym < 2)) continue;
        int first = n;
        for (int i = 0; i < g->n; i++) {
            float x = mx ? 2.0f * ARENA_CX - g->x[i] - BLOCK_TILE : g->x[i];
            float y = my ? 2.0f * ARENA_CY - g->y[i] - BLOCK_TILE : g->y[i];
            rs[n] = (SDL_FRect){x, y, BLOCK_TILE, BLOCK_TILE};
            kinds[n++] = g->kind[i];
        }
        SDL_FRect b = rs[first]; /* this copy's bounding box */
        for (int i = first + 1; i < n; i++) {
            float x1 = fmaxf(b.x + b.w, rs[i].x + rs[i].w), y1 = fmaxf(b.y + b.h, rs[i].y + rs[i].h);
            b.x = fminf(b.x, rs[i].x), b.y = fminf(b.y, rs[i].y);
            b.w = x1 - b.x, b.h = y1 - b.y;
        }
        box[copies++] = b;
    }
    if (n == 0 || G.room.nobs + n > MAX_OBSTACLES) return 0;
    for (int i = 0; i < n; i++)
        if (!block_rect_ok(rs[i])) return 0;
    for (int i = 0; i < copies; i++) /* the mirror images of a group near the middle: same rule */
        for (int j = 0; j < i; j++)
            if (!gap_ok(rect_gap(box[i], box[j]))) return 0;
    for (int i = 0; i < n; i++) {
        Obstacle *o = &G.room.obs[G.room.nobs++];
        memset(o, 0, sizeof *o);
        o->r = rs[i];
        o->kind = (uint8_t)kinds[i];
        o->hp = o->max_hp = BLOCK_HP[kinds[i]];
        o->swing_id = -1;
    }
    return n;
}

/* Gate: a line of blocks carrying a wall on, from one of its ends, into the gap next to it
 * (the longest line that fits). */
static int add_gate(int wall, int max_tiles) {
    SDL_FRect w = G.room.obs[wall].r;
    bool horiz = w.w >= w.h;
    float dir = chance(0.5f) ? 1.0f : -1.0f;
    Theme th = chance(0.75f) ? TH_CRATES : roll_theme();
    for (int len = max_tiles; len >= 2; len--) {
        Group g = {.n = len};
        for (int k = 0; k < len; k++) {
            if (horiz) {
                g.x[k] = dir > 0 ? w.x + w.w + (float)k * BLOCK_TILE : w.x - (float)(k + 1) * BLOCK_TILE;
                g.y[k] = w.y + w.h * 0.5f - BLOCK_TILE * 0.5f;
            } else {
                g.x[k] = w.x + w.w * 0.5f - BLOCK_TILE * 0.5f;
                g.y[k] = dir > 0 ? w.y + w.h + (float)k * BLOCK_TILE : w.y - (float)(k + 1) * BLOCK_TILE;
            }
            g.kind[k] = theme_kind(th);
        }
        int got = place_group(&g, 0);
        if (got) return got;
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
    float x, y;
    if (attached && G.room.nobs > 0) {
        SDL_FRect o = G.room.obs[irand(0, G.room.nobs - 1)].r;
        float cw = (float)w * BLOCK_TILE, ch = (float)h * BLOCK_TILE;
        switch (irand(0, 3)) {
        case 0: x = o.x - cw, y = frand(o.y - ch + BLOCK_TILE, o.y + o.h - BLOCK_TILE); break; /* left of it */
        case 1: x = o.x + o.w, y = frand(o.y - ch + BLOCK_TILE, o.y + o.h - BLOCK_TILE); break;
        case 2: x = frand(o.x - cw + BLOCK_TILE, o.x + o.w - BLOCK_TILE), y = o.y - ch; break; /* above */
        default: x = frand(o.x - cw + BLOCK_TILE, o.x + o.w - BLOCK_TILE), y = o.y + o.h; break;
        }
    } else {
        float x0 = sym == 1 || sym == 3 ? ARENA_CX - BLOCK_TILE * 2 : ARENA_X1 - 200;
        float y0 = sym >= 2 ? ARENA_CY - BLOCK_TILE * 2 : ARENA_Y1 - 200;
        x = frand(ARENA_X0 + EDGE_MARGIN, x0), y = frand(ARENA_Y0 + EDGE_MARGIN, y0);
    }
    if (th == TH_BARRELS && w * h > 4) h = 1; /* a whole depot of barrels would be a death trap */
    Group g = {.n = 0};
    for (int i = 0; i < w; i++)
        for (int j = 0; j < h; j++) {
            g.x[g.n] = x + (float)i * BLOCK_TILE;
            g.y[g.n] = y + (float)j * BLOCK_TILE;
            g.kind[g.n++] = theme_kind(th);
        }
    return place_group(&g, sym);
}

static void gen_blocks(int sym) {
    int budget = g_dense_blocks ? MAX_OBSTACLES : irand(12, 30);
    if (G.room.nobs == 0) budget += 8; /* open floor */
    int placed = 0, walls = G.room.nobs;
    for (int i = 0; i < npending; i++) { /* barricade gates first */
        Theme th = chance(0.7f) ? TH_CRATES : roll_theme();
        Group g = {.n = 0};
        for (float y = pending_gates[i].y; y < pending_gates[i].y + pending_gates[i].h - 1.0f && g.n < 16; y += BLOCK_TILE) {
            g.x[g.n] = pending_gates[i].x;
            g.y[g.n] = y;
            g.kind[g.n++] = theme_kind(th);
        }
        placed += place_group(&g, 0);
    }
    float gate_chance = g_dense_blocks ? 1.0f : 0.45f;
    for (int i = 0; i < walls && placed < budget; i++)
        if (chance(gate_chance)) placed += add_gate(i, irand(2, 6));
    for (int tries = 0; tries < 250 && placed < budget; tries++) placed += add_cluster(sym, chance(0.6f));
}

/* ---- Connectivity -------------------------------------------------------- */

#define CELL 20.0f
_Static_assert((int)(ARENA_X1 - ARENA_X0) / 20 == 89 && (int)(ARENA_Y1 - ARENA_Y0) / 20 == 46, "flood fill grid = arena / CELL");
#define GW 89 /* ARENA_W / CELL */
#define GH 46 /* ARENA_H / CELL */

static bool cell_free(int cx, int cy) {
    V2 p = v2(ARENA_X0 + ((float)cx + 0.5f) * CELL, ARENA_Y0 + ((float)cy + 0.5f) * CELL);
    const float r = PLAYER_BODY_R + 1.0f;
    if (p.x < ARENA_X0 + r || p.x > ARENA_X1 - r || p.y < ARENA_Y0 + r || p.y > ARENA_Y1 - r) return false;
    for (int i = 0; i < G.room.nobs; i++)
        if (G.room.obs[i].kind == OB_WALL && rect_dist(p, G.room.obs[i].r) < r) return false;
    return true;
}

bool room_walls_connected(void) {
    static uint8_t state[GH][GW]; /* 0 blocked, 1 free, 2 reached */
    static int queue[GW * GH];
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) state[y][x] = (uint8_t)cell_free(x, y);
    V2 e = room_entry_spot();
    int sx = (int)((e.x - ARENA_X0) / CELL), sy = (int)((e.y - ARENA_Y0) / CELL);
    if (sx < 0 || sy < 0 || sx >= GW || sy >= GH || state[sy][sx] != 1) return false;
    int head = 0, tail = 0;
    state[sy][sx] = 2;
    queue[tail++] = sy * GW + sx;
    while (head < tail) {
        int c = queue[head++], x = c % GW, y = c / GW;
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || state[ny][nx] != 1) continue;
            state[ny][nx] = 2;
            queue[tail++] = ny * GW + nx;
        }
    }
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            if (state[y][x] == 1) return false; /* a free spot the ship cannot reach */
    for (int s = SIDE_LEFT; s <= SIDE_BOTTOM; s++) { /* every door */
        V2 d = door_position((Side)s);
        V2 in = v2_add(d, v2_scale(v2_norm(v2_sub(v2(ARENA_CX, ARENA_CY), d)), 30.0f));
        int x = (int)((in.x - ARENA_X0) / CELL), y = (int)((in.y - ARENA_Y0) / CELL);
        if (x < 0 || y < 0 || x >= GW || y >= GH || state[y][x] != 2) return false;
    }
    return true;
}

void room_gen_layout(void) {
    for (int attempt = 0; attempt < 30; attempt++) {
        G.room.nobs = 0;
        int sym = attempt < 25 ? gen_walls() : 1; /* last attempts: blocks only */
        gen_blocks(sym);
        if (room_walls_connected()) break;
    }
    G.room.nfires = 0;
    room_nav_reset();
}

/* ---- Damage -------------------------------------------------------------- */

static SDL_FColor block_color(int kind) {
    switch (kind) {
    case OB_BARREL: return col_hex(0xff4a3a, 1);
    case OB_OIL: return col_hex(0xffa830, 1);
    case OB_SPARK: return col_hex(0xc27dff, 1);
    default: return col_hex(0xd89a4a, 1);
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
        /* Blocks in it slowly burn too: barrels and drums end up going off. */
        for (int k = 0; k < r->nobs; k++) {
            Obstacle *o = &r->obs[k];
            if (obstacle_breakable(o) && o->hp > 0.0f && rect_dist(f->pos, o->r) < f->r * 0.8f) {
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

void blocks_draw(SDL_FColor wall, SDL_FColor edge) {
    const Room *r = &G.room;
    /* Walls in passes, so the rects of one piece (L, T, cross...) merge under a single outline. */
    for (int pass = 0; pass < 4; pass++)
        for (int i = 0; i < r->nobs; i++) {
            const Obstacle *ob = &r->obs[i];
            if (ob->kind != OB_WALL) continue;
            SDL_FRect o = ob->r;
            if (pass == 0) draw_rect(o.x + 6, o.y + 8, o.w, o.h, rgba(0, 0, 0, 0.35f)); /* drop shadow */
            else if (pass == 1) draw_rect(o.x, o.y, o.w, o.h, edge);
            else if (pass == 2) draw_rect(o.x + 3, o.y + 3, o.w - 6, o.h - 6, wall);
            else if (o.w > 14 && o.h > 14) draw_rect(o.x + 7, o.y + 7, o.w - 14, o.h - 14, col_mul(wall, 1.35f));
        }
    for (int i = 0; i < r->nobs; i++)
        if (r->obs[i].kind != OB_WALL && obstacle_solid(&r->obs[i])) draw_block(&r->obs[i], i);
    for (int i = 0; i < r->nobs; i++) draw_shatter_cracks(&r->obs[i], i + (int)r->obs[i].r.x);
}
