#include "audio.h"
#include "draw.h"
#include "game.h"

/*
 * F3 debug menu: teleport to any room type, with or without its enemies, plus a
 * few cheats. The game is frozen while it is open. Keyboard (arrows / WASD +
 * Enter) and mouse (hover + click) both work.
 */

typedef enum {
    ROW_DEPTH, ROW_REWARD, ROW_SECRET_CONTENT,
    ROW_START, ROW_COMBAT, ROW_STATS, ROW_SHOP, ROW_WORKSHOP, ROW_SECRET,
    ROW_GOD, ROW_FPS, ROW_ALWAYS_SECRET, ROW_REVEAL, ROW_COINS, ROW_BOMBS, ROW_UPGRADE, ROW_ACTIVE, ROW_CLOSE,
    ROW_COUNT
} Row;

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
    [ROW_SECRET] = {"SECRET ROOM", ROOM_SECRET, COLOR_SECRET},
};

#define MAX_DEPTH 40
/* Combat reward choices: index 0 = random, then RW_COINS .. RW_UPGRADE. */
#define MENU_REWARDS (RW_COUNT - 1)

/* Layout (logical pixels). */
#define PANEL_W 940.0f
#define PANEL_X ((SCREEN_W - PANEL_W) * 0.5f)
#define ROWS_Y 214.0f
#define ROW_H 40.0f
#define BTN_W 240.0f
#define BTN_X0 (PANEL_X + 410.0f)
#define BTN_X1 (BTN_X0 + BTN_W + 20.0f)

static struct {
    int sel, col; /* selected row, and button column on room rows (0 = with enemies) */
    int depth, reward;
    int secret_content; /* 0 random, 1 item, 2 shop */
    int upgrade;        /* GIVE UPGRADE selection */
    int active;         /* GIVE ACTIVE selection (ActiveId) */
    bool up, down, left, right; /* previous held state, for edge detection */
    V2 mouse;
} M = {.sel = ROW_COMBAT, .depth = 3, .active = ACT_DASH};

static bool is_room_row(int row) { return row >= ROW_START && row <= ROW_SECRET; }
/* The launch bay never has enemies: only its NO ENEMIES button exists. (A secret room
 * WITH ENEMIES is its rare guarded variant.) */
static bool peaceful_row(int row) { return row == ROW_START; }
static bool is_value_row(int row) { return row == ROW_DEPTH || row == ROW_REWARD || row == ROW_SECRET_CONTENT || row == ROW_UPGRADE || row == ROW_ACTIVE; }
static bool is_toggle_row(int row) { return row == ROW_GOD || row == ROW_FPS || row == ROW_ALWAYS_SECRET || row == ROW_REVEAL; }
static float row_y(int row) { return ROWS_Y + (float)row * ROW_H + (row >= ROW_START ? 16.0f : 0.0f) + (row >= ROW_GOD ? 16.0f : 0.0f); }

static int row_at(V2 m) {
    if (m.x < PANEL_X || m.x > PANEL_X + PANEL_W) return -1;
    for (int r = 0; r < ROW_COUNT; r++)
        if (m.y >= row_y(r) && m.y < row_y(r) + ROW_H - 6.0f) return r;
    return -1;
}

void debug_teleport(RoomType type, RewardType reward, int depth, bool with_enemies) {
    G.transitioning = false;
    if (type == ROOM_START) depth = 0;
    else if (depth < 1) depth = 1;
    if (type != ROOM_COMBAT) reward = RW_NONE;
    else if (reward == RW_NONE) reward = room_roll_reward();
    if (type == ROOM_SECRET) {
        /* A secret room needs a room to return to: a cleared combat room that hid it. */
        room_enter(ROOM_COMBAT, room_roll_reward(), depth);
        room_debug_clear();
        if (G.room.secret_corner < 0) G.room.secret_corner = irand(0, 3);
        G.room.secret_open = true;
        int ambush = G.secret_ambush_override;
        G.secret_ambush_override = with_enemies;
        room_enter_secret();
        G.secret_ambush_override = ambush;
    } else {
        room_enter(type, reward, depth);
    }
    if (!with_enemies) room_debug_clear();
}

void debug_menu_open(void) {
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
    debug_teleport(ROOM_ROWS[row].type, rw, M.depth, with_enemies);
    G.secret_shop_override = -1;
    G.state = GS_PLAYING;
    G.pl.invuln = fmaxf(G.pl.invuln, 0.8f);
    sfx_play(SFX_DOOR_ENTER);
}

/* Left/right on a value row (dir = -1 / +1). */
static void change_value(int row, int dir) {
    if (row == ROW_DEPTH) M.depth = (M.depth - 1 + dir + MAX_DEPTH) % MAX_DEPTH + 1;
    else if (row == ROW_REWARD) M.reward = (M.reward + dir + MENU_REWARDS) % MENU_REWARDS;
    else if (row == ROW_SECRET_CONTENT) M.secret_content = (M.secret_content + dir + 3) % 3;
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
    case ROW_SECRET_CONTENT: change_value(row, dir); break;
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
    case ROW_CLOSE: debug_menu_close(); break;
    default:
        if (is_room_row(row)) teleport_from_menu(row, M.col == 0 && !peaceful_row(row));
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
    if (up) M.sel = (M.sel + ROW_COUNT - 1) % ROW_COUNT;
    if (down) M.sel = (M.sel + 1) % ROW_COUNT;
    if (up || down) sfx_play(SFX_WEAPON_SWITCH);
    if (left) change_value(M.sel, -1);
    if (right) change_value(M.sel, +1);

    /* Mouse: hovering selects (only when it moves, so it never fights the keyboard). */
    int hover = row_at(in->mouse);
    if (v2_dist2(in->mouse, M.mouse) > 1.0f) {
        M.mouse = in->mouse;
        if (hover >= 0) {
            M.sel = hover;
            if (is_room_row(hover)) M.col = in->mouse.x >= BTN_X1 - 10.0f;
        }
    }

    if (in->confirm_pressed) {
        if (hover == M.sel && (M.sel == ROW_UPGRADE || M.sel == ROW_ACTIVE)) {
            /* < name > on the left (click either half to browse), GIVE button on the right. */
            float x = in->mouse.x;
            activate(M.sel, x >= BTN_X1 + BTN_W * 0.5f ? 0 : x < BTN_X0 + BTN_W * 0.5f ? -1 : +1);
        } else if (hover == M.sel && is_value_row(M.sel)) {
            /* Click on the left half of a value decreases it, right half increases. */
            activate(M.sel, in->mouse.x < BTN_X1 - 10.0f ? -1 : +1);
        } else if (hover >= 0 || !in->fire) { /* ignore clicks outside the rows */
            activate(M.sel, M.sel == ROW_UPGRADE || M.sel == ROW_ACTIVE ? 0 : +1); /* Enter on GIVE gives it */
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
    static const char *corners[] = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-RIGHT", "BOTTOM-LEFT"};
    static char buf[64];
    const Room *r = &G.room;
    if (r->secret_corner < 0) return " - NO SECRET";
    SDL_snprintf(buf, sizeof buf, " - SECRET: %s%s", corners[r->secret_corner],
                 r->secret_used ? " (USED)" : r->secret_open ? " (OPEN)" : "");
    return buf;
}

void debug_menu_draw(void) {
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0, 0, 0, 0.72f));
    float top = 120.0f, bottom = row_y(ROW_COUNT - 1) + ROW_H + 70.0f;
    draw_rect(PANEL_X, top, PANEL_W, bottom - top, col_hex(0x0b0e1a, 0.96f));
    draw_rect_outline(PANEL_X, top, PANEL_W, bottom - top, 3.0f, col_hex(0x9dff6a, 0.8f));
    draw_text_centered(SCREEN_W * 0.5f, top + 30.0f, 4.0f, col_hex(0x9dff6a, 1), "DEBUG MENU");
    draw_text_centered(SCREEN_W * 0.5f, top + 76.0f, 1.5f, col_a(COL_WHITE, 0.55f),
                       "ARROWS / WASD + ENTER  or  MOUSE      F3 / ESC  close");

    SDL_FColor white = COL_WHITE, dimw = col_a(COL_WHITE, 0.75f);
    for (int r = 0; r < ROW_COUNT; r++) {
        float y = row_y(r);
        bool sel = r == M.sel;
        if (sel) draw_rect(PANEL_X + 8.0f, y - 4.0f, PANEL_W - 16.0f, ROW_H - 4.0f, col_a(COL_WHITE, 0.06f));
        float ty = y + (ROW_H - 12.0f) * 0.5f - 8.0f;
        float lx = PANEL_X + 40.0f;
        if (sel) draw_text(lx - 26.0f, ty, 2.0f, col_hex(0x9dff6a, 1), ">");

        if (is_room_row(r)) {
            SDL_FColor c = col_hex(ROOM_ROWS[r].color, 1);
            draw_text(lx, ty, 2.5f, c, "%s", ROOM_ROWS[r].label);
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
            label = "SHOW SECRET CORNER";
            SDL_snprintf(value, sizeof value, "%s", G.reveal_secrets ? "ON" : "OFF");
            break;
        case ROW_COINS: label = "+50 COINS"; break;
        case ROW_BOMBS: label = "+3 BOMBS"; break;
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
