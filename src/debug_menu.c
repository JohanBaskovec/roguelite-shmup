#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * Debug menus, two pages of the same panel (the game is frozen while it is open; keyboard - arrows / WASD +
 * Enter - and mouse - hover + click - both work):
 *   F3  cheats: god mode, overlays, secrets, +coins / bombs / keys, give any upgrade / active
 *   F4  teleport: any room type, with or without its enemies, at the chosen depth / reward / secret content /
 *       boss, with the room modifiers ticked (mods.c: any mix of them; those a room type cannot have are left out)
 * F3 / F4 switch between them (the same key again, or ESC, closes).
 */

typedef enum {
    ROW_DEPTH, ROW_REWARD, ROW_SECRET_CONTENT, ROW_BOSS_KIND,
    ROW_MODS,                             /* the check box of modifier MOD_NONE + 1 + (row - ROW_MODS) */
    ROW_START = ROW_MODS + MOD_COUNT - 1, ROW_COMBAT, ROW_STATS, ROW_SHOP, ROW_WORKSHOP, ROW_BOSS, ROW_SECRET,
    ROW_GOD, ROW_FPS, ROW_ALWAYS_SECRET, ROW_REVEAL, ROW_COINS, ROW_BOMBS, ROW_KEYS, ROW_UPGRADE,
    ROW_ACTIVE, ROW_CLOSE,
    ROW_COUNT
} Row;

/* The rows of each page, in order. */
#define PAGE_MAX 24
static const int PAGE_CHEATS_ROWS[] = {ROW_GOD, ROW_FPS, ROW_ALWAYS_SECRET, ROW_REVEAL, ROW_COINS, ROW_BOMBS, ROW_KEYS, ROW_UPGRADE, ROW_ACTIVE, ROW_CLOSE};
static int page_rows[2][PAGE_MAX], page_len[2];

static void build_pages(void) {
    if (page_len[0]) return;
    for (int i = 0; i < ARRAY_LEN(PAGE_CHEATS_ROWS); i++) page_rows[0][page_len[0]++] = PAGE_CHEATS_ROWS[i];
    for (int r = ROW_DEPTH; r < ROW_GOD; r++) page_rows[1][page_len[1]++] = r;
    page_rows[1][page_len[1]++] = ROW_CLOSE;
}

static const struct {
    const char *label;
    RoomType type;
    uint32_t color;
} ROOM_ROWS[] = {
    [ROW_START] = {"LAUNCH BAY", ROOM_START, 0xffffff},
    [ROW_COMBAT] = {"COMBAT ROOM", ROOM_COMBAT, 0x66f0ff},
    [ROW_STATS] = {"STATS ROOM", ROOM_STATS, COLOR_STATS},
    [ROW_SHOP] = {"SHOP", ROOM_SHOP, COLOR_SHOP},
    [ROW_WORKSHOP] = {"WORKSHOP", ROOM_WORKSHOP, COLOR_WORKSHOP},
    [ROW_BOSS] = {"BOSS ROOM", ROOM_BOSS, COLOR_BOSS},
    [ROW_SECRET] = {"SECRET ROOM", ROOM_SECRET, COLOR_SECRET},
};

#define MAX_DEPTH 40
/* Combat reward choices: index 0 = random, then RW_COINS .. RW_UPGRADE. */
#define MENU_REWARDS (RW_COUNT - 1)

/* Layout (logical pixels). */
#define PANEL_W 940.0f
#define PANEL_X ((SCREEN_W - PANEL_W) * 0.5f)
#define ROWS_Y 226.0f
#define ROW_H 36.0f
#define BTN_W 240.0f
#define BTN_X0 (PANEL_X + 410.0f)
#define BTN_X1 (BTN_X0 + BTN_W + 20.0f)

static struct {
    int page;     /* 0 = cheats (F3), 1 = teleport (F4) */
    int sel, col; /* selected row (index in the page), and button column on room rows (0 = with enemies) */
    int depth, reward;
    int secret_content; /* 0 random, 1 item, 2 shop */
    int boss_kind;      /* boss room: 0 random (by depth), else BOSS_FIRST + boss_kind - 1 */
    int upgrade;        /* GIVE UPGRADE selection */
    int active;         /* GIVE ACTIVE selection (ActiveId) */
    unsigned mods;      /* the modifiers ticked (MOD_BIT set), for every teleport */
    bool up, down, left, right; /* previous held state, for edge detection */
    V2 mouse;
} M = {.depth = 3, .active = ACT_DASH};

static bool is_mod_row(int row) { return row >= ROW_MODS && row < ROW_START; }
static int row_mod(int row) { return MOD_NONE + 1 + (row - ROW_MODS); }
static bool is_room_row(int row) { return row >= ROW_START && row <= ROW_SECRET; }
/* The launch bay never has enemies: only its NO ENEMIES button exists. (A secret room
 * WITH ENEMIES is its rare guarded variant.) */
static bool peaceful_row(int row) { return row == ROW_START; }
static bool is_value_row(int row) { return row == ROW_DEPTH || row == ROW_REWARD || row == ROW_SECRET_CONTENT || row == ROW_BOSS_KIND || row == ROW_UPGRADE || row == ROW_ACTIVE; }
static bool is_toggle_row(int row) { return row == ROW_GOD || row == ROW_FPS || row == ROW_ALWAYS_SECRET || row == ROW_REVEAL; }
/* Rows come in groups (a gap between two): values, modifiers, rooms, toggles, the rest. */
static int row_group(int row) {
    return row < ROW_MODS ? 0 : is_mod_row(row) ? 1 : is_room_row(row) ? 2 : row <= ROW_REVEAL ? 3 : 4;
}
static int page_row(int i) { return page_rows[M.page][i]; }
static float row_y(int i) {
    float y = ROWS_Y;
    for (int k = 1; k <= i; k++) y += ROW_H + (row_group(page_row(k)) != row_group(page_row(k - 1)) ? 16.0f : 0.0f);
    return y;
}

static int row_at(V2 m) {
    if (m.x < PANEL_X || m.x > PANEL_X + PANEL_W) return -1;
    for (int i = 0; i < page_len[M.page]; i++)
        if (m.y >= row_y(i) && m.y < row_y(i) + ROW_H - 6.0f) return i;
    return -1;
}

void debug_teleport(RoomType type, RewardType reward, int depth, bool with_enemies) {
    debug_teleport_mod(type, reward, depth, with_enemies, 0);
}

void debug_teleport_mod(RoomType type, RewardType reward, int depth, bool with_enemies, unsigned mods) {
    G.transitioning = false;
    G.entry_side = SIDE_BOTTOM;
    if (type == ROOM_START) depth = 0;
    else if (depth < 1) depth = 1;
    if (type == ROOM_BOSS && !is_boss_depth(depth)) depth = (depth / BOSS_EVERY + 1) * BOSS_EVERY; /* bosses only live there */
    if (type != ROOM_COMBAT) reward = RW_NONE;
    else if (reward == RW_NONE) reward = room_roll_reward();
    if (type == ROOM_SECRET) {
        /* A secret room needs a room to return to: a cleared combat room that hid it. */
        room_enter(ROOM_COMBAT, room_roll_reward(), depth);
        room_debug_clear();
        G.room.has_secret = true;
        G.room.secret_open = true;
        int ambush = G.secret_ambush_override;
        G.secret_ambush_override = with_enemies;
        room_enter_secret();
        G.secret_ambush_override = ambush;
    } else {
        G.entry_mods = (uint8_t)mods; /* room_enter keeps those the type can have, and uses them up */
        room_enter(type, reward, depth);
    }
    if (!with_enemies) room_debug_clear();
}

int debug_menu_page(void) { return M.page; }
unsigned debug_menu_mods(void) { return M.mods; }

void debug_menu_set_page(int page) {
    build_pages();
    if (M.page != page) {
        M.page = page;
        M.sel = 0;
        for (int i = 0; i < page_len[page]; i++) /* the teleport page opens on the combat room */
            if (page_row(i) == ROW_COMBAT) M.sel = i;
        sfx_play(SFX_WEAPON_SWITCH);
    }
}

void debug_menu_open(int page) {
    build_pages();
    M.page = !page; /* (so that set_page resets the selection) */
    debug_menu_set_page(page);
    G.debug_prev = G.state;
    G.state = GS_DEBUG;
    M.mouse = G.in.mouse;
    M.up = G.in.up;
    M.down = G.in.down;
    M.left = G.in.left;
    M.right = G.in.right;
    if (G.room.depth > 0) M.depth = G.room.depth;
    sfx_play(SFX_PAUSE);
}

void debug_menu_close(void) {
    G.state = G.debug_prev;
    if (G.state == GS_PLAYING) G.pl.invuln = fmaxf(G.pl.invuln, 0.5f);
    sfx_play(SFX_PAUSE);
}

static void teleport_from_menu(int row, bool with_enemies) {
    /* No run in progress (title / game over): start one first. */
    if (G.debug_prev != GS_PLAYING && G.debug_prev != GS_PAUSED && G.debug_prev != GS_UPGRADES) game_new_run();
    RewardType rw = M.reward == 0 ? RW_NONE : (RewardType)(RW_COINS + M.reward - 1);
    G.secret_shop_override = M.secret_content - 1; /* -1 random, 0 item, 1 shop */
    int boss = g_boss_override;
    if (M.boss_kind > 0) g_boss_override = BOSS_FIRST + M.boss_kind - 1;
    debug_teleport_mod(ROOM_ROWS[row].type, rw, M.depth, with_enemies, M.mods);
    G.secret_shop_override = -1;
    g_boss_override = boss;
    G.state = GS_PLAYING;
    G.pl.invuln = fmaxf(G.pl.invuln, 0.8f);
    sfx_play(SFX_DOOR_ENTER);
}

/* Left/right on a value row (dir = -1 / +1). */
static void change_value(int row, int dir) {
    if (row == ROW_DEPTH) M.depth = (M.depth - 1 + dir + MAX_DEPTH) % MAX_DEPTH + 1;
    else if (row == ROW_REWARD) M.reward = (M.reward + dir + MENU_REWARDS) % MENU_REWARDS;
    else if (row == ROW_SECRET_CONTENT) M.secret_content = (M.secret_content + dir + 3) % 3;
    else if (row == ROW_BOSS_KIND) M.boss_kind = (M.boss_kind + dir + BOSS_COUNT + 1) % (BOSS_COUNT + 1);
    else if (row == ROW_UPGRADE) M.upgrade = (M.upgrade + dir + UPG_COUNT) % UPG_COUNT;
    else if (row == ROW_ACTIVE) M.active = (M.active - 1 + dir + ACT_COUNT - 1) % (ACT_COUNT - 1) + 1;
    else if (is_room_row(row)) M.col = dir > 0;
    else return;
    sfx_play(SFX_WEAPON_SWITCH);
}

static void activate(int row, int dir) {
    switch (row) {
    case ROW_DEPTH:
    case ROW_REWARD:
    case ROW_SECRET_CONTENT:
    case ROW_BOSS_KIND: change_value(row, dir); break;
    case ROW_UPGRADE:
        if (dir != 0) {
            change_value(row, dir);
        } else if (upgrade_capped(M.upgrade)) {
            sfx_play(SFX_DENIED);
        } else {
            upgrade_apply(M.upgrade);
            sfx_play(SFX_ITEM);
        }
        break;
    case ROW_ACTIVE:
        if (dir != 0) {
            change_value(row, dir);
        } else {
            active_give(M.active);
            sfx_play(SFX_ITEM);
        }
        break;
    case ROW_GOD: G.god = !G.god; sfx_play(SFX_WEAPON_SWITCH); break;
    case ROW_FPS: G.show_debug = !G.show_debug; sfx_play(SFX_WEAPON_SWITCH); break;
    case ROW_ALWAYS_SECRET: G.force_secret = !G.force_secret; sfx_play(SFX_WEAPON_SWITCH); break;
    case ROW_REVEAL: G.reveal_secrets = !G.reveal_secrets; sfx_play(SFX_WEAPON_SWITCH); break;
    case ROW_COINS: G.pl.coins += 50; sfx_play(SFX_COIN); break;
    case ROW_BOMBS: G.pl.bombs += 3; sfx_play(SFX_BOMB_PICKUP); break;
    case ROW_KEYS: G.pl.keys += 3; sfx_play(SFX_KEY); break;
    case ROW_CLOSE: debug_menu_close(); break;
    default:
        if (is_mod_row(row)) {
            M.mods ^= MOD_BIT(row_mod(row));
            sfx_play(SFX_WEAPON_SWITCH);
        } else if (is_room_row(row)) teleport_from_menu(row, M.col == 0 && !peaceful_row(row));
        break;
    }
}

void debug_menu_update(void) {
    const Input *in = &G.in;
    if (in->pause_pressed) {
        debug_menu_close();
        return;
    }

    /* Keyboard: edges of the held movement keys. */
    bool up = in->up && !M.up, down = in->down && !M.down;
    bool left = in->left && !M.left, right = in->right && !M.right;
    M.up = in->up;
    M.down = in->down;
    M.left = in->left;
    M.right = in->right;
    int n = page_len[M.page];
    if (up) M.sel = (M.sel + n - 1) % n;
    if (down) M.sel = (M.sel + 1) % n;
    if (up || down) sfx_play(SFX_WEAPON_SWITCH);
    int row = page_row(M.sel);
    if (left) change_value(row, -1);
    if (right) change_value(row, +1);

    /* Mouse: hovering selects (only when it moves, so it never fights the keyboard). */
    int hover = row_at(in->mouse);
    if (v2_dist2(in->mouse, M.mouse) > 1.0f) {
        M.mouse = in->mouse;
        if (hover >= 0) {
            M.sel = hover;
            if (is_room_row(page_row(hover))) M.col = in->mouse.x >= BTN_X1 - 10.0f;
        }
    }

    if (in->confirm_pressed) {
        row = page_row(M.sel);
        if (hover == M.sel && (row == ROW_UPGRADE || row == ROW_ACTIVE)) {
            /* < name > on the left (click either half to browse), GIVE button on the right. */
            float x = in->mouse.x;
            activate(row, x >= BTN_X1 + BTN_W * 0.5f ? 0 : x < BTN_X0 + BTN_W * 0.5f ? -1 : +1);
        } else if (hover == M.sel && is_value_row(row)) {
            /* Click on the left half of a value decreases it, right half increases. */
            activate(row, in->mouse.x < BTN_X1 - 10.0f ? -1 : +1);
        } else if (hover >= 0 || !in->fire) { /* ignore clicks outside the rows */
            activate(row, row == ROW_UPGRADE || row == ROW_ACTIVE ? 0 : +1); /* Enter on GIVE gives it */
        }
    }
}

static void draw_button(float x, float y, float w, const char *text, SDL_FColor c, bool selected) {
    float h = ROW_H - 12.0f;
    if (selected) draw_rect(x, y, w, h, col_a(c, 0.85f));
    else draw_rect(x, y, w, h, col_a(c, 0.08f));
    draw_rect_outline(x, y, w, h, 2.0f, col_a(c, selected ? 1.0f : 0.5f));
    draw_text_centered(x + w * 0.5f, y + h * 0.5f - 8.0f, 2.0f, selected ? col_hex(0x05060c, 1) : c, "%s", text);
}

static const char *secret_text(void) {
    static char buf[64];
    const Room *r = &G.room;
    if (!r->has_secret) return " - NO SECRET";
    SDL_snprintf(buf, sizeof buf, " - SECRET%s",
                 r->secret_used ? " (USED)" : r->secret_open ? " (OPEN)" : "");
    return buf;
}

void debug_menu_draw(void) {
    build_pages();
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0, 0, 0, 0.72f));
    int n = page_len[M.page];
    float top = 120.0f, bottom = row_y(n - 1) + ROW_H + 70.0f;
    draw_rect(PANEL_X, top, PANEL_W, bottom - top, col_hex(0x0b0e1a, 0.96f));
    draw_rect_outline(PANEL_X, top, PANEL_W, bottom - top, 3.0f, col_hex(0x9dff6a, 0.8f));
    draw_text_centered(SCREEN_W * 0.5f, top + 30.0f, 4.0f, col_hex(0x9dff6a, 1), "%s", M.page ? "TELEPORT" : "DEBUG MENU");
    draw_text_centered(SCREEN_W * 0.5f, top + 76.0f, 1.5f, col_a(COL_WHITE, 0.55f), "%s",
                       M.page ? "ARROWS / WASD + ENTER  or  MOUSE      F3  cheats      F4 / ESC  close"
                              : "ARROWS / WASD + ENTER  or  MOUSE      F4  teleport      F3 / ESC  close");

    SDL_FColor white = COL_WHITE, dimw = col_a(COL_WHITE, 0.75f);
    for (int i = 0; i < n; i++) {
        int r = page_row(i);
        float y = row_y(i);
        bool sel = i == M.sel;
        if (sel) draw_rect(PANEL_X + 8.0f, y - 4.0f, PANEL_W - 16.0f, ROW_H - 4.0f, col_a(COL_WHITE, 0.06f));
        float ty = y + (ROW_H - 12.0f) * 0.5f - 8.0f;
        float lx = PANEL_X + 40.0f;
        if (sel) draw_text(lx - 26.0f, ty, 2.0f, col_hex(0x9dff6a, 1), ">");

        if (is_mod_row(r)) { /* [x] NAME      what it does */
            int m = row_mod(r);
            bool on = M.mods & MOD_BIT(m);
            SDL_FColor c = col_hex(mod_color(m), 1);
            float bs = 20.0f, by = ty - 2.0f;
            draw_rect(lx, by, bs, bs, col_a(c, on ? 0.9f : 0.06f));
            draw_rect_outline(lx, by, bs, bs, 2.0f, col_a(c, on || sel ? 1.0f : 0.5f));
            if (on) draw_text_centered(lx + bs * 0.5f, by + 2.0f, 2.0f, col_hex(0x05060c, 1), "x");
            draw_text(lx + bs + 16.0f, ty, 2.5f, on ? c : col_a(c, sel ? 0.8f : 0.5f), "%s", mod_name(m));
            draw_text(BTN_X0, ty + 4.0f, 1.5f, col_a(COL_WHITE, on ? 0.75f : 0.4f), "%s%s", mod_desc(m),
                      m == MOD_WAGER ? " (combat only)" : "");
            continue;
        }
        if (is_room_row(r)) {
            SDL_FColor c = col_hex(ROOM_ROWS[r].color, 1);
            draw_text(lx, ty, 2.5f, c, "%s", ROOM_ROWS[r].label);
            unsigned kept = mods_allowed(ROOM_ROWS[r].type, M.mods); /* the ticked modifiers it leaves out */
            if (M.mods && kept != M.mods)
                draw_text(PANEL_X + 282.0f, ty + 5.0f, 1.25f, col_a(COL_WHITE, 0.45f), "%s", kept ? "NO WAGER" : "NO MODIFIERS");
            if (peaceful_row(r)) {
                draw_text_centered(BTN_X0 + BTN_W * 0.5f, ty + 2.0f, 1.5f, col_a(COL_WHITE, 0.3f), "never has enemies");
                draw_button(BTN_X1, y, BTN_W, "NO ENEMIES", c, sel);
            } else {
                draw_button(BTN_X0, y, BTN_W, "WITH ENEMIES", c, sel && M.col == 0);
                draw_button(BTN_X1, y, BTN_W, "NO ENEMIES", c, sel && M.col == 1);
            }
            continue;
        }
        const char *label = "";
        char value[48] = "";
        switch (r) {
        case ROW_DEPTH:
            label = "ROOM DEPTH";
            SDL_snprintf(value, sizeof value, "<  %d  >", M.depth);
            break;
        case ROW_REWARD:
            label = "COMBAT REWARD";
            SDL_snprintf(value, sizeof value, "<  %s  >", M.reward == 0 ? "RANDOM" : reward_name((RewardType)(RW_COINS + M.reward - 1)));
            break;
        case ROW_SECRET_CONTENT: {
            static const char *contents[] = {"RANDOM", "ITEM", "SHOP"};
            label = "SECRET CONTENT";
            SDL_snprintf(value, sizeof value, "<  %s  >", contents[M.secret_content]);
        } break;
        case ROW_BOSS_KIND:
            label = "BOSS";
            SDL_snprintf(value, sizeof value, "<  %s  >", M.boss_kind == 0 ? "BY DEPTH" : boss_title((EnemyType)(BOSS_FIRST + M.boss_kind - 1)));
            break;
        case ROW_UPGRADE: {
            label = "GIVE UPGRADE";
            SDL_FColor uc = col_hex(UPGRADES[M.upgrade].color, 1);
            draw_text_centered(BTN_X0 + BTN_W * 0.5f + 40.0f, ty + 2.0f, 2.0f, upgrade_capped(M.upgrade) ? col_a(uc, 0.35f) : uc,
                               "< %s >", UPGRADES[M.upgrade].name);
            draw_button(BTN_X1 + BTN_W * 0.5f, y, BTN_W * 0.5f, "GIVE", uc, sel);
        } break;
        case ROW_ACTIVE: {
            label = "GIVE ACTIVE";
            SDL_FColor ac = col_hex(ACTIVES[M.active].color, 1);
            draw_text_centered(BTN_X0 + BTN_W * 0.5f + 40.0f, ty + 2.0f, 2.0f, ac, "< %s >", ACTIVES[M.active].name);
            draw_button(BTN_X1 + BTN_W * 0.5f, y, BTN_W * 0.5f, "GIVE", ac, sel);
        } break;
        case ROW_GOD: label = "GOD MODE"; SDL_snprintf(value, sizeof value, "%s", G.god ? "ON" : "OFF"); break;
        case ROW_FPS: label = "FPS OVERLAY"; SDL_snprintf(value, sizeof value, "%s", G.show_debug ? "ON" : "OFF"); break;
        case ROW_ALWAYS_SECRET:
            label = "SECRET IN EVERY ROOM";
            SDL_snprintf(value, sizeof value, "%s", G.force_secret ? "ON" : "OFF");
            break;
        case ROW_REVEAL:
            label = "SHOW SECRETS";
            SDL_snprintf(value, sizeof value, "%s", G.reveal_secrets ? "ON" : "OFF");
            break;
        case ROW_COINS: label = "+50 COINS"; break;
        case ROW_BOMBS: label = "+3 BOMBS"; break;
        case ROW_KEYS: label = "+3 KEYS"; break;
        case ROW_CLOSE: label = "CLOSE"; break;
        default: break;
        }
        draw_text(lx, ty, 2.5f, sel ? white : dimw, "%s", label);
        if (value[0]) {
            bool on = !SDL_strcmp(value, "ON");
            SDL_FColor vc = is_toggle_row(r) ? (on ? col_hex(0x9dff6a, 1) : col_hex(0xff4d6a, 1)) : white;
            draw_text_centered(BTN_X1 - 10.0f, ty, 2.5f, vc, "%s", value);
        }
    }

    const char *names[] = {"LAUNCH BAY", "COMBAT", "STATS", "TREASURE", "SHOP", "BOSS", "SECRET", "WORKSHOP"};
    bool in_run = G.debug_prev == GS_PLAYING || G.debug_prev == GS_PAUSED || G.debug_prev == GS_UPGRADES;
    if (in_run)
        draw_text_centered(SCREEN_W * 0.5f, bottom - 46.0f, 1.5f, col_a(COL_WHITE, 0.5f),
                           "NOW: ROOM %d - %s - %s - %d ENEMIES - %d COINS%s", G.room.depth, names[G.room.type],
                           G.room.cleared ? "CLEARED" : "FIGHTING", G.nen + G.nsp, G.pl.coins,
                           secret_text());
    else
        draw_text_centered(SCREEN_W * 0.5f, bottom - 46.0f, 1.5f, col_a(COL_WHITE, 0.5f), "NO RUN IN PROGRESS - TELEPORTING STARTS ONE");
}
