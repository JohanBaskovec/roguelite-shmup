#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Upgrades menu (TAB, from a run or the pause screen): every upgrade found, weapon mods
 * first. Each one can be turned off and back on (upgrade_toggle): a build can be shaped,
 * e.g. a weapon mod that does not fit the others. The world is frozen while it is open.
 */

#define COLS 4
#define CELL_W 430.0f
#define CELL_H 48.0f
#define GAP 12.0f
#define GRID_Y 215.0f
#define ROWS_SHOWN 11

static struct {
    GameState back;      /* state to return to (playing or paused) */
    int sel, scroll;     /* selected entry, first row shown */
    bool up, down, left, right; /* held last tick: keys act on the press only */
    V2 mouse;
} M;

int upgrade_menu_list(int *out) {
    int n = 0;
    for (int u = UPG_W_SCATTER; u <= UPG_W_SWORD; u++)
        if (G.pl.upg_count[u] > 0) out[n++] = u;
    for (int u = 0; u < UPG_COUNT; u++)
        if (G.pl.upg_count[u] > 0 && !upgrade_is_weapon(u)) out[n++] = u;
    return n;
}

static float grid_x(void) { return (SCREEN_W - (CELL_W * COLS + GAP * (COLS - 1))) * 0.5f; }

static void cell_pos(int i, float *x, float *y) {
    *x = grid_x() + (float)(i % COLS) * (CELL_W + GAP);
    *y = GRID_Y + (float)(i / COLS - M.scroll) * (CELL_H + GAP);
}

static int cell_at(V2 m, int n) {
    for (int i = M.scroll * COLS; i < n && i < (M.scroll + ROWS_SHOWN) * COLS; i++) {
        float x, y;
        cell_pos(i, &x, &y);
        if (m.x >= x && m.x <= x + CELL_W && m.y >= y && m.y <= y + CELL_H) return i;
    }
    return -1;
}

void upgrade_menu_open(void) {
    M.back = G.state == GS_PAUSED ? GS_PAUSED : GS_PLAYING;
    G.state = GS_UPGRADES;
    M.up = G.in.up;
    M.down = G.in.down;
    M.left = G.in.left;
    M.right = G.in.right;
    M.mouse = G.in.mouse;
    int list[UPG_COUNT];
    int n = upgrade_menu_list(list);
    if (M.sel >= n) M.sel = n > 0 ? n - 1 : 0;
    sfx_play(SFX_PAUSE);
}

static void toggle(int u) {
    if (upgrade_toggle(u)) sfx_play_ex(SFX_WEAPON_SWITCH, 1.0f, G.pl.upg_off[u] ? 0.8f : 1.2f, 0.0f);
    else sfx_play(SFX_DENIED);
}

void upgrade_menu_update(void) {
    const Input *in = &G.in;
    if (in->menu_pressed || in->pause_pressed) {
        G.state = M.back;
        sfx_play(SFX_PAUSE);
        return;
    }
    int list[UPG_COUNT];
    int n = upgrade_menu_list(list);
    int prev = M.sel;
    if (n > 0) {
        if (in->left && !M.left && M.sel > 0) M.sel--;
        if (in->right && !M.right && M.sel < n - 1) M.sel++;
        if (in->up && !M.up && M.sel >= COLS) M.sel -= COLS;
        if (in->down && !M.down && M.sel + COLS < n) M.sel += COLS;
    }
    M.up = in->up;
    M.down = in->down;
    M.left = in->left;
    M.right = in->right;
    int rows = (n + COLS - 1) / COLS;
    int max_scroll = rows > ROWS_SHOWN ? rows - ROWS_SHOWN : 0;
    if (in->wheel) M.scroll = M.scroll + in->wheel < 0 ? 0 : M.scroll + in->wheel > max_scroll ? max_scroll : M.scroll + in->wheel;
    /* The mouse only takes over when it moves. */
    if (v2_dist2(in->mouse, M.mouse) > 1.0f || in->wheel) {
        int c = cell_at(in->mouse, n);
        if (c >= 0) M.sel = c;
        M.mouse = in->mouse;
    }
    /* Keep the selection in view. */
    if (M.sel / COLS < M.scroll) M.scroll = M.sel / COLS;
    if (M.sel / COLS >= M.scroll + ROWS_SHOWN) M.scroll = M.sel / COLS - ROWS_SHOWN + 1;
    if (M.sel != prev) sfx_play_ex(SFX_WEAPON_SWITCH, 0.5f, 1.5f, 0.0f);

    if (n == 0) return;
    if (in->click_pressed) {
        int c = cell_at(in->mouse, n);
        if (c >= 0) {
            M.sel = c;
            toggle(list[c]);
        }
    } else if (in->confirm_pressed || in->active_pressed) {
        toggle(list[M.sel]);
    }
}

void upgrade_menu_draw(void) {
    const Player *p = &G.pl;
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.88f));
    draw_text_centered(SCREEN_W * 0.5f, 60.0f, 5.0f, col_hex(0xb57cff, 1), "UPGRADES");
    draw_text_centered(SCREEN_W * 0.5f, 122.0f, 1.75f, col_a(COL_WHITE, 0.6f),
                       "CLICK / ENTER / SPACE  turn on - off      ARROWS / WHEEL  move      TAB / ESC  back");
    if (sword_on()) draw_text_centered(SCREEN_W * 0.5f, 160.0f, 2.0f, col_hex(0x66f0ff, 0.9f), "WEAPON: SWORD");
    else
        draw_text_centered(SCREEN_W * 0.5f, 160.0f, 2.0f, col_hex(0x66f0ff, 0.9f), "WEAPON: %s  -  %.0f SHOT%s PER VOLLEY", gun_name(),
                           (double)gun_volley_shots(), gun_volley_shots() > 1.5f ? "S" : "");

    int list[UPG_COUNT];
    int n = upgrade_menu_list(list);
    if (n == 0) {
        draw_text_centered(SCREEN_W * 0.5f, 480.0f, 3.0f, col_a(COL_WHITE, 0.5f), "NO UPGRADE FOUND YET");
        return;
    }
    int off = 0;
    for (int i = 0; i < n; i++) off += p->upg_off[list[i]];
    for (int i = M.scroll * COLS; i < n && i < (M.scroll + ROWS_SHOWN) * COLS; i++) {
        int u = list[i];
        float x, y;
        cell_pos(i, &x, &y);
        bool on = !p->upg_off[u], sel = i == M.sel, fixed = !upgrade_toggleable(u);
        SDL_FColor c = col_hex(UPGRADES[u].color, 1);
        draw_rect(x, y, CELL_W, CELL_H, on ? col_a(col_mul(c, 0.16f), 1.0f) : col_hex(0x07080e, 1));
        draw_rect_outline(x, y, CELL_W, CELL_H, sel ? 3.0f : 1.5f, sel ? COL_WHITE : on ? col_a(c, 0.55f) : col_hex(0x2a3040, 1));
        draw_pickup_icon(PK_UPGRADE, u, v2(x + 26.0f, y + CELL_H * 0.5f), 11.0f, G.time);
        if (!on) draw_rect(x + 8.0f, y + 6.0f, 38.0f, CELL_H - 12.0f, rgba(0.03f, 0.03f, 0.06f, 0.7f));
        SDL_FColor tc = on ? c : col_hex(0x4a5470, 1);
        draw_text(x + 54.0f, y + (upgrade_is_weapon(u) ? 8.0f : 16.0f), 2.0f, tc, "%s", UPGRADES[u].name);
        if (upgrade_is_weapon(u)) draw_text(x + 54.0f, y + 31.0f, 1.0f, col_a(tc, 0.7f), "WEAPON MOD");
        if (p->upg_count[u] > 1)
            draw_text(x + 60.0f + draw_text_width(UPGRADES[u].name, 2.0f), y + 20.0f, 1.5f, col_a(tc, 0.8f), "x%d", p->upg_count[u]);
        const char *tag = fixed ? "ONE-TIME" : on ? "ON" : "OFF";
        SDL_FColor gc = fixed ? col_hex(0x6a7490, 1) : on ? col_hex(0x7dff9a, 1) : col_hex(0xff5f7e, 1);
        float ts = fixed ? 1.25f : 2.0f;
        draw_text(x + CELL_W - 14.0f - draw_text_width(tag, ts), y + (CELL_H - 8.0f * ts) * 0.5f, ts, gc, "%s", tag);
    }
    int rows = (n + COLS - 1) / COLS;
    if (rows > ROWS_SHOWN) { /* scroll bar */
        float top = GRID_Y, h = ROWS_SHOWN * (CELL_H + GAP) - GAP, bx = grid_x() + COLS * (CELL_W + GAP);
        draw_rect(bx, top, 6.0f, h, col_hex(0x1a2035, 1));
        draw_rect(bx, top + h * (float)M.scroll / (float)rows, 6.0f, h * (float)ROWS_SHOWN / (float)rows, col_hex(0xb57cff, 0.8f));
    }

    /* The selected upgrade, in full. */
    int u = list[M.sel < n ? M.sel : n - 1];
    float y = GRID_Y + ROWS_SHOWN * (CELL_H + GAP) + 18.0f;
    draw_text_centered(SCREEN_W * 0.5f, y, 3.0f, col_hex(UPGRADES[u].color, 1), "%s", UPGRADES[u].name);
    draw_desc(SCREEN_W * 0.5f, y + 42.0f, 2.0f, col_a(COL_WHITE, 0.8f), upgrade_desc_at(u, p->upg_count[u]));
    draw_text_centered(SCREEN_W * 0.5f, y + 100.0f, 1.5f, col_a(COL_WHITE, 0.45f), "%d UPGRADE%s FOUND, %d TURNED OFF", n, n > 1 ? "S" : "", off);
}
