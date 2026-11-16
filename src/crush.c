/*
 * Closing walls: every wave has a timer (the bar at the bottom of the screen); when it runs out the
 * next wave comes in, and when the last wave's runs out the walls of the room close in on the ship
 * from every side - one floor cell per CRUSH_STEP_T, each side until a CRUSH_MIN_W x CRUSH_MIN_H box
 * around where the ship was. Never in a boss room. Everything is kept inside (the live bounds,
 * room_x0()..., are what room_collide_obstacle, the bullets and the crawlers use), touching a closing
 * wall hurts, and the obstacles they reach burst into glass. Cleared: they burst too.
 */
#include "audio.h"
#include "draw.h"
#include "game.h"

#define ARENA_H (ARENA_Y1 - ARENA_Y0)

static float ease(float k) { return k * k * (3.0f - 2.0f * k); }

static void set_insets(void) {
    Room *r = &G.room;
    float k = ease(clampf(r->crush_slide, 0.0f, 1.0f));
    for (int s = 0; s < 4; s++)
        r->crush_in[s] = ((float)r->crush_from[s] + (float)(r->crush_cell[s] - r->crush_from[s]) * k) * GRID;
}

/* The box the walls are heading for this step, in px. */
static SDL_FRect cell_box(void) {
    const Room *r = &G.room;
    float x0 = ARENA_X0 + (float)r->crush_cell[0] * GRID, y0 = ARENA_Y0 + (float)r->crush_cell[1] * GRID;
    float x1 = ARENA_X1 - (float)r->crush_cell[2] * GRID, y1 = ARENA_Y1 - (float)r->crush_cell[3] * GRID;
    return (SDL_FRect){x0, y0, x1 - x0, y1 - y0};
}

/* slow (MOD_COMPACTOR, from the first wave): towards the middle of the room, one cell every CRUSH_SLOW_STEP_T
 * until the last wave's timer runs out; otherwise towards the ship, at full speed. */
void crush_start(bool slow) {
    Room *r = &G.room;
    if (r->crushing || r->type == ROOM_BOSS || r->cleared) return;
    V2 focus = slow ? v2(ARENA_CX, ARENA_CY) : G.pl.pos;
    int fx = (int)((focus.x - ARENA_X0) / GRID), fy = (int)((focus.y - ARENA_Y0) / GRID);
    int x0 = fx - CRUSH_MIN_W / 2, y0 = fy - CRUSH_MIN_H / 2;
    x0 = x0 < 0 ? 0 : x0 > GW - CRUSH_MIN_W ? GW - CRUSH_MIN_W : x0;
    y0 = y0 < 0 ? 0 : y0 > GH - CRUSH_MIN_H ? GH - CRUSH_MIN_H : y0;
    r->crushing = true;
    r->crush_goal[0] = x0;
    r->crush_goal[1] = y0;
    r->crush_goal[2] = GW - (x0 + CRUSH_MIN_W);
    r->crush_goal[3] = GH - (y0 + CRUSH_MIN_H);
    for (int s = 0; s < 4; s++) r->crush_from[s] = r->crush_cell[s] = 0;
    r->crush_slide = 1.0f;
    r->crush_slow = slow;
    r->crush_t = slow ? CRUSH_SLOW_STEP_T : 0.6f; /* the first step right after the warning */
    if (slow) return; /* the compactor's own banner said it */
    fx_banner("THE WALLS CLOSE IN", "finish them!", col_hex(0xff3b5c, 1), 1.4f);
    sfx_play(SFX_WAVE);
    fx_shake(0.4f);
}

/* Keeps p (radius rad) inside the live bounds; true if it was pushed. */
static bool keep_inside(V2 *p, float rad) {
    bool pushed = false;
    float x0 = room_x0() + rad, x1 = room_x1() - rad, y0 = room_y0() + rad, y1 = room_y1() - rad;
    if (x0 > x1) x0 = x1 = (x0 + x1) * 0.5f;
    if (y0 > y1) y0 = y1 = (y0 + y1) * 0.5f;
    if (p->x < x0) { p->x = x0; pushed = true; }
    if (p->x > x1) { p->x = x1; pushed = true; }
    if (p->y < y0) { p->y = y0; pushed = true; }
    if (p->y > y1) { p->y = y1; pushed = true; }
    return pushed;
}

void crush_update(void) {
    Room *r = &G.room;
    if (!r->crushing || r->cleared) return;
    if (!active_time_stop_on()) { /* TIME STOP holds the walls too */
        if (r->crush_slide < 1.0f) r->crush_slide = minf(1.0f, r->crush_slide + DT / CRUSH_SLIDE_T);
        r->crush_t -= DT;
        if (!r->crush_slow && r->crush_t > CRUSH_STEP_T) r->crush_t = CRUSH_STEP_T; /* sped up */
        bool moving = false;
        for (int s = 0; s < 4; s++) moving |= r->crush_cell[s] < r->crush_goal[s];
        if (r->crush_t <= 0.0f && moving) {
            for (int s = 0; s < 4; s++) {
                r->crush_from[s] = r->crush_cell[s];
                if (r->crush_cell[s] < r->crush_goal[s]) r->crush_cell[s]++;
            }
            r->crush_slide = 0.0f;
            r->crush_t = r->crush_slow ? CRUSH_SLOW_STEP_T : CRUSH_STEP_T;
            bool last = true;
            for (int s = 0; s < 4; s++) last = last && r->crush_cell[s] >= r->crush_goal[s];
            room_crush_grind(cell_box(), last); /* what the walls reach crumbles as they start to move */
            sfx_play(SFX_CRACKLE);
            fx_shake(0.18f);
        }
        set_insets();
    }

    /* Everything stays inside; the ship touching a closing wall gets hurt (and pushed back in). */
    Player *pl = &G.pl;
    V2 before = pl->pos;
    bool pushed = keep_inside(&pl->pos, PLAYER_BODY_R);
    float x0 = room_x0(), x1 = room_x1(), y0 = room_y0(), y1 = room_y1(), m = PLAYER_BODY_R + 2.0f;
    bool touching = (r->crush_in[0] > 0.0f && pl->pos.x - x0 < m) || (r->crush_in[2] > 0.0f && x1 - pl->pos.x < m) ||
                    (r->crush_in[1] > 0.0f && pl->pos.y - y0 < m) || (r->crush_in[3] > 0.0f && y1 - pl->pos.y < m);
    if (pushed || touching) {
        V2 in = v2_sub(v2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f), pl->pos);
        if (touching && player_hurt(1)) pl->vel = v2_scale(v2_norm(in), 420.0f);
        else if (pushed) pl->vel = v2_add(pl->vel, v2_sub(pl->pos, before));
    }
    for (int i = 0; i < G.nen; i++) {
        Enemy *e = &G.en[i];
        if (e->hp > 0.0f && !enemy_is_boss(e)) keep_inside(&e->pos, e->radius);
    }
    for (int i = 0; i < G.nsp; i++) keep_inside(&G.sp[i].pos, ENEMIES[G.sp[i].type].radius);
    for (int i = 0; i < G.neb; i++) /* bullets left behind the walls fizzle */
        if (G.eb[i].pos.x < x0 || G.eb[i].pos.x > x1 || G.eb[i].pos.y < y0 || G.eb[i].pos.y > y1) G.eb[i].life = -1.0f;
}

/* The panels between the arena edge and the live bounds, left, top, right, bottom. */
static int panels(SDL_FRect out[4]) {
    float x0 = room_x0(), x1 = room_x1(), y0 = room_y0(), y1 = room_y1();
    int n = 0;
    if (x0 > ARENA_X0) out[n++] = (SDL_FRect){ARENA_X0, ARENA_Y0, x0 - ARENA_X0, ARENA_H};
    if (y0 > ARENA_Y0) out[n++] = (SDL_FRect){x0, ARENA_Y0, x1 - x0, y0 - ARENA_Y0};
    if (x1 < ARENA_X1) out[n++] = (SDL_FRect){x1, ARENA_Y0, ARENA_X1 - x1, ARENA_H};
    if (y1 < ARENA_Y1) out[n++] = (SDL_FRect){x0, y1, x1 - x0, ARENA_Y1 - y1};
    return n;
}

void crush_end(void) {
    Room *r = &G.room;
    if (!r->crushing) return;
    SDL_FRect p[4];
    int n = panels(p);
    for (int i = 0; i < n; i++) {
        int k = (int)clampf(p[i].w * p[i].h / (40.0f * 40.0f), 8.0f, 40.0f);
        for (int j = 0; j < k; j++) {
            V2 q = v2(frand(p[i].x, p[i].x + p[i].w), frand(p[i].y, p[i].y + p[i].h));
            fx_shard(q, v2_scale(v2_angle(frand(0, TAU_F)), frand(80.0f, 360.0f)), frand(0.3f, 0.5f), frand(8.0f, 20.0f),
                     j % 3 ? col_hex(0xff3b5c, 0.9f) : col_a(COL_WHITE, 0.9f), frand(-14.0f, 14.0f));
        }
    }
    r->crushing = r->crush_slow = false;
    memset(r->crush_in, 0, sizeof r->crush_in);
}

int crush_hazards(BossHazard *out, int max) {
    const Room *r = &G.room;
    if (!r->crushing || r->cleared) return 0;
    float x0 = room_x0(), x1 = room_x1(), y0 = room_y0(), y1 = room_y1();
    V2 c[4] = {v2(x0, y0), v2(x1, y0), v2(x1, y1), v2(x0, y1)};
    int n = 0;
    /* left, top, right, bottom edges, each only once it has started to move */
    const int a[4] = {3, 0, 1, 2}, b[4] = {0, 1, 2, 3};
    for (int s = 0; s < 4 && n < max; s++)
        if (r->crush_in[s] > 0.0f) out[n++] = (BossHazard){c[a[s]], c[b[s]], 18.0f, true};
    return n;
}

void crush_draw(void) {
    const Room *r = &G.room;
    if (!r->crushing) return;
    SDL_FRect p[4];
    int n = panels(p);
    float pulse = 0.5f + 0.5f * sinf(G.time * 9.0f);
    for (int i = 0; i < n; i++) {
        SDL_FRect q = p[i];
        draw_rect(q.x, q.y, q.w, q.h, col_hex(0x1a0309, 0.96f));
        /* hazard stripes, 45 degrees, clipped to the panel by drawing short pieces */
        for (float s = -q.h; s < q.w; s += 34.0f) {
            float ax = q.x + s, ay = q.y, bx = q.x + s + q.h, by = q.y + q.h;
            if (ax < q.x) { ay += q.x - ax; ax = q.x; }
            if (bx > q.x + q.w) { by -= bx - (q.x + q.w); bx = q.x + q.w; }
            if (ay < by) draw_line(v2(ax, ay), v2(bx, by), 9.0f, col_hex(0x5a0d1c, 0.85f));
        }
    }
    float x0 = room_x0(), x1 = room_x1(), y0 = room_y0(), y1 = room_y1();
    draw_blend_add();
    SDL_FColor edge = col_hex(0xff3b5c, 0.55f + 0.4f * pulse);
    if (r->crush_in[0] > 0.0f) draw_line(v2(x0, y0), v2(x0, y1), 4.0f, edge);
    if (r->crush_in[1] > 0.0f) draw_line(v2(x0, y0), v2(x1, y0), 4.0f, edge);
    if (r->crush_in[2] > 0.0f) draw_line(v2(x1, y0), v2(x1, y1), 4.0f, edge);
    if (r->crush_in[3] > 0.0f) draw_line(v2(x0, y1), v2(x1, y1), 4.0f, edge);
    draw_rect_outline(x0 - 8.0f, y0 - 8.0f, x1 - x0 + 16.0f, y1 - y0 + 16.0f, 6.0f, col_hex(0xff3b5c, 0.12f + 0.12f * pulse));
    draw_blend_alpha();
}

/* The wave timer, a bar at the bottom of the screen (never in a boss room, nor before the first wave). */
void draw_wave_bar(void) {
    const Room *r = &G.room;
    if (r->cleared || r->type == ROOM_BOSS || r->wave < 1 || r->wave_time <= 0.0f) return;
    /* under the bottom wall, centred: the label on its left, the seconds left on its right */
    const float w = 900.0f, h = 10.0f, x = SCREEN_W * 0.5f - w * 0.5f, y = ARENA_Y1 + WALL_T + 6.0f;
    bool last = r->wave >= r->nwaves;
    float pulse = 0.5f + 0.5f * sinf(G.time * 8.0f);
    float k;
    SDL_FColor col;
    const char *label;
    if (r->crushing) {
        int done = 0, total = 0;
        for (int s = 0; s < 4; s++) done += r->crush_cell[s], total += r->crush_goal[s];
        k = total > 0 ? 1.0f - (float)done / (float)total : 0.0f;
        col = col_hex(0xff3b5c, 0.6f + 0.4f * pulse);
        label = r->crush_slow ? "COMPACTOR" : "WALLS CLOSING IN";
        if (r->crush_slow) { /* the waves' timer still runs: show it, the walls crawl meanwhile */
            k = clampf(r->wave_t / r->wave_time, 0.0f, 1.0f);
            label = last ? "COMPACTOR - LAST WAVE" : "COMPACTOR - NEXT WAVE";
        }
    } else {
        k = clampf(r->wave_t / r->wave_time, 0.0f, 1.0f);
        col = last ? col_hex(0xff5f7e, k < 0.3f ? 0.6f + 0.4f * pulse : 1.0f) : col_hex(0xb57cff, 1);
        label = last ? "LAST WAVE - THEN THE WALLS" : "NEXT WAVE";
    }
    draw_rect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, col_hex(0x000000, 0.6f));
    draw_rect(x, y, w * k, h, col);
    draw_rect_outline(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 1.0f, col_a(col, 0.6f));
    draw_text(x - 14.0f - draw_text_width(label, 1.5f), y - 1.0f, 1.5f, col, "%s", label);
    if (!r->crushing || r->crush_slow) draw_text(x + w + 14.0f, y - 1.0f, 1.5f, col, "%.0fs", (double)ceilf(r->wave_t > 0.0f ? r->wave_t : 0.0f));
}
