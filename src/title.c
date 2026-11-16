#include "audio.h"
#include "bot.h"
#include "draw.h"
#include "game.h"
#include "save.h"

#include <stdio.h>
#include <string.h>

/*
 * Main menu (GS_TITLE) over a real game played by the bot:
 *   NEW SAVE   a new save file (first ship, starting unlocks) and its first run, at once;
 *   CONTINUE   the list of save files (unlock %, run in progress): ENTER / click opens one, DEL / the X
 *              (twice) deletes it. A save opens its own menu: RESUME RUN (a run is going on), NEW RUN
 *              (character select, locked ships greyed), UNLOCKS (every upgrade, locked ones greyed;
 *              the one selected shown in full), BESTIARY (every enemy met, the one selected playing in a
 *              small screen of its own: bestiary.c; the demo behind pauses meanwhile), MAIN MENU, QUIT.
 *              ESC in a run opens this same menu over it (pause_open);
 *   DEV MODE   a save of its own (SAVE_DEV_SLOT, never listed), everything unlocked and every enemy
 *              met, opened on its menu (ESC: back to the main menu).
 * The save is saved on entering each room (save.c) and keeps its unlocks when the ship is destroyed.
 *
 * The background demo lives in the normal world (G), flagged G.demo, and runs on its own
 * random generator (G.demo_rng, swapped in for each demo tick) with sounds suppressed, so it
 * never changes the real game's randomness. Starting a run (game_new_run) wipes it. The demo
 * keeps playing behind the character select screen opened from the menu.
 */

typedef enum { TP_MAIN, TP_SAVES, TP_SAVE, TP_UNLOCKS, TP_BESTIARY } TitlePage;

enum { TM_NEW, TM_CONTINUE, TM_DEV, TM_OPTIONS, TM_QUIT, TM_COUNT };
static const char *const TITLE_ITEMS[TM_COUNT] = {"NEW SAVE", "CONTINUE", "DEV MODE", "OPTIONS", "QUIT"};
enum { SM_RESUME, SM_NEW_RUN, SM_UNLOCKS, SM_BESTIARY, SM_MAIN_MENU, SM_QUIT, SM_COUNT };
static const char *const SAVE_ITEMS[SM_COUNT] = {"RESUME RUN", "NEW RUN", "UNLOCKS", "BESTIARY", "MAIN MENU", "QUIT"};

/* The save files, as listed when the menu opened (and after each deletion); the one opened. */
static SaveInfo saves[SAVE_SLOTS];
static int nsaves, load_scroll;
static bool listed;
static SaveInfo cur;

static void refresh_saves(void) {
    nsaves = save_list(saves, SAVE_SLOTS);
    listed = true;
    if (G.load_sel >= nsaves) G.load_sel = nsaves > 0 ? nsaves - 1 : 0;
}

/* Options: later. CONTINUE needs a save file. */
static bool main_enabled(int i) { return i == TM_NEW || i == TM_DEV || i == TM_QUIT || (i == TM_CONTINUE && nsaves > 0); }
/* The same buttons from the main menu and in the pause; RESUME RUN only while a run is going on. */
static bool save_item_shown(int i) { return i != SM_RESUME || cur.has_run; }
static bool save_enabled_item(int i) { return save_item_shown(i); }

#define BTN_W 420.0f
#define BTN_H 60.0f
#define BTN_GAP 18.0f
#define BTN_Y0 430.0f
#define ROW_W 1300.0f
#define ROW_H 72.0f
#define ROW_GAP 12.0f
#define ROW_Y0 290.0f
#define LOAD_ROWS 7
#define DEL_W 56.0f
#define DEMO_MAX_ROOM_TIME 75.0f /* a demo stuck in a room starts over */
#define DEMO_DEATH_T 2.0f        /* the explosion plays out before the next demo run */
/* Unlocks screen: every upgrade in columns, the selected one in full on the right. */
#define UNL_COLS 4
#define UNL_ROWS ((UPG_COUNT + UNL_COLS - 1) / UNL_COLS)
#define UNL_X0 70.0f
#define UNL_Y0 200.0f
#define UNL_CW 268.0f
#define UNL_RH 23.0f
#define UNL_PX 1190.0f
#define UNL_PW 660.0f

/* ---- Button menus (main menu, save menu) ---------------------------------- */

typedef struct {
    int count;
    const char *const *labels;
    bool (*shown)(int);
    bool (*enabled)(int);
    float y0;
} ButtonMenu;

static bool always(int i) { (void)i; return true; }

static const ButtonMenu MAIN_MENU = {TM_COUNT, TITLE_ITEMS, always, main_enabled, BTN_Y0};
static const ButtonMenu SAVE_MENU = {SM_COUNT, SAVE_ITEMS, save_item_shown, save_enabled_item, BTN_Y0};

/* Row of item i (hidden items take no room). */
static int btn_row(const ButtonMenu *m, int i) {
    int r = 0;
    for (int k = 0; k < i; k++) r += m->shown(k);
    return r;
}

static float btn_y(const ButtonMenu *m, int i) { return m->y0 + (float)btn_row(m, i) * (BTN_H + BTN_GAP); }

static int btn_at(const ButtonMenu *m, V2 p) {
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    for (int i = 0; i < m->count; i++)
        if (m->shown(i) && p.x >= x0 && p.x <= x0 + BTN_W && p.y >= btn_y(m, i) && p.y <= btn_y(m, i) + BTN_H) return i;
    return -1;
}

static int btn_first(const ButtonMenu *m) {
    for (int i = 0; i < m->count; i++)
        if (m->enabled(i)) return i;
    return 0;
}

static void btn_move(const ButtonMenu *m, int dir) {
    int s = G.title_sel;
    for (int k = 0; k < m->count; k++) {
        s = (s + dir + m->count) % m->count;
        if (m->enabled(s)) break;
    }
    if (s != G.title_sel) sfx_play(SFX_WEAPON_SWITCH);
    G.title_sel = s;
}

/* Arrows, wheel, mouse; returns the item activated (ENTER / click), -1 if none. */
static int btn_update(const ButtonMenu *m) {
    const Input *in = &G.in;
    if (in->up && !G.title_up) btn_move(m, -1);
    if (in->down && !G.title_down) btn_move(m, 1);
    if (in->wheel) btn_move(m, in->wheel);
    /* The mouse only takes over when it actually moves. */
    int hover = btn_at(m, in->mouse);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0 && m->enabled(hover) && hover != G.title_sel) {
            G.title_sel = hover;
            sfx_play(SFX_WEAPON_SWITCH);
        }
        G.sel_mouse = in->mouse;
    }
    int act = -1;
    if (in->click_pressed) act = hover;
    else if (in->confirm_pressed) act = G.title_sel;
    if (act >= 0 && !m->enabled(act)) {
        sfx_play(SFX_DENIED);
        act = -1;
    }
    return act;
}

static void btn_draw(const ButtonMenu *m) {
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    for (int i = 0; i < m->count; i++) {
        if (!m->shown(i)) continue;
        float y = btn_y(m, i);
        bool on = m->enabled(i), sel = on && i == G.title_sel;
        SDL_FColor c = on ? col_hex(0x66f0ff, 1) : col_a(COL_WHITE, 0.28f);
        if (sel) draw_rect(x0, y, BTN_W, BTN_H, col_a(c, 0.85f));
        else draw_rect(x0, y, BTN_W, BTN_H, rgba(0.02f, 0.03f, 0.07f, 0.85f));
        draw_rect_outline(x0, y, BTN_W, BTN_H, 2.0f, col_a(c, sel ? 1.0f : 0.5f));
        draw_text_centered(SCREEN_W * 0.5f, y + BTN_H * 0.5f - 12.0f, 3.0f, sel ? col_hex(0x05060c, 1) : c, "%s", m->labels[i]);
    }
}

static float row_y(int vis) { return ROW_Y0 + (float)vis * (ROW_H + ROW_GAP); }

/* Save under the mouse (index in the list), -1 if none; *on_delete: on its X. */
static int row_at(V2 m, bool *on_delete) {
    float x0 = (SCREEN_W - ROW_W) * 0.5f;
    for (int v = 0; v < LOAD_ROWS && load_scroll + v < nsaves; v++)
        if (m.x >= x0 && m.x <= x0 + ROW_W && m.y >= row_y(v) && m.y <= row_y(v) + ROW_H) {
            if (on_delete) *on_delete = m.x >= x0 + ROW_W - DEL_W - 8.0f;
            return load_scroll + v;
        }
    return -1;
}

bool demo_visible(void) { return G.demo && (G.state == GS_TITLE || G.state == GS_CHAR_SELECT); }

static void swap_rng(void) {
    Rng t = G.rng;
    G.rng = G.demo_rng;
    G.demo_rng = t;
}

/* A fresh demo run: a combat room a few rooms in, a shot upgrade and a few upgrades so it
 * shows off. Uses the demo generator (called with it swapped in). */
static void demo_new_run(void) {
    G.npb = G.neb = G.nen = G.nsp = G.npk = G.nmc = 0;
    fx_clear();
    player_init();
    G.rooms_cleared = G.kills = G.items_bought = 0;
    G.machine_uses = G.augments = G.active_uses = G.toggles = 0;
    G.run_time = 0.0f;
    G.shake = G.hitstop = G.hurt_flash = G.bomb_flash = G.banner_t = 0.0f;
    G.transitioning = false;
    G.demo = true; /* before room_enter: nothing it does counts for the save's unlocks */
    int mods[UPG_COUNT], nmods = 0;
    for (int u = UPG_W_SCATTER; u < UPG_W_SWORD; u++)
        if (upgrade_unlocked(u)) mods[nmods++] = u;
    if (nmods > 0) upgrade_apply(mods[irand(0, nmods - 1)]);
    for (int k = irand(2, 5); k > 0; k--) {
        int u = upgrade_roll();
        if (u >= 0) upgrade_apply(u);
    }
    if (chance(0.5f)) active_give(irand(ACT_NONE + 1, ACT_COUNT - 1));
    G.save_slot = 0; /* never saved */
    room_enter(ROOM_COMBAT, room_roll_reward(), irand(2, 8));
    G.demo = true;
    G.demo_dead_t = 0.0f;
    G.demo_room_id = G.room.id;
    G.demo_room_t = 0.0f;
}

static void demo_start(void) {
    swap_rng();
    audio_suppress(true);
    GameState st = G.state;
    demo_new_run();
    G.state = st;
    audio_suppress(false);
    swap_rng();
}

/* One tick of the demo: the bot drives, the world runs as in a real game. */
static void demo_step(void) {
    Input real = G.in;
    GameState st = G.state;
    swap_rng();
    audio_suppress(true);

    if (G.demo_dead_t > 0.0f) { /* the ship blew up: let it play out, then a new run */
        G.state = GS_GAMEOVER;
        bullets_update();
        enemies_update();
        fx_update();
        if ((G.demo_dead_t -= DT) <= 0.0f) demo_new_run();
    } else {
        G.state = GS_PLAYING;
        memset(&G.in, 0, sizeof G.in);
        G.in.number = -1;
        G.in.mouse = real.mouse;
        bot_input();
        game_world_step();
        if (G.room.id != G.demo_room_id) {
            G.demo_room_id = G.room.id;
            G.demo_room_t = 0.0f;
        }
        G.demo_room_t += DT;
        if (G.state == GS_GAMEOVER) G.demo_dead_t = DEMO_DEATH_T;
        else if (G.state != GS_PLAYING || G.demo_room_t > DEMO_MAX_ROOM_TIME) demo_new_run();
    }

    audio_suppress(false);
    swap_rng();
    G.state = st;
    G.in = real;
}

/* Keeps the demo going behind the menus (also behind the character select it opened). */
void demo_update(void) {
    if (!G.demo) return;
    demo_step();
}

static void page_open(TitlePage page, int sel) {
    G.title_page = page;
    G.title_sel = sel;
    G.load_confirm = 0;
    G.sel_mouse = G.in.mouse;
    G.title_up = G.in.up; /* a key already held does not count as a press */
    G.title_down = G.in.down;
    G.sel_left = G.in.left;
    G.sel_right = G.in.right;
}

void title_open(void) {
    G.state = GS_TITLE;
    G.save_slot = 0; /* leaving a run: its save (start of the room it was in) stays */
    G.menu_slot = 0;
    refresh_saves();
    page_open(TP_MAIN, TM_NEW);
}

/* The save menu of the save G.menu_slot (re-read: a run may have been played since). */
static bool save_menu_open(int sel) {
    refresh_saves();
    SaveInfo info; /* read on its own: DEV MODE's save is not in the list */
    if (!save_info(G.menu_slot, &info) || !info.ok || !save_load_profile(G.menu_slot, &G.prof)) return false;
    cur = info;
    G.state = GS_TITLE;
    page_open(TP_SAVE, sel >= 0 && save_enabled_item(sel) ? sel : btn_first(&SAVE_MENU));
    return true;
}

/* Out of a run (Q) or of the character select (ESC): the menu of the save played, if any. */
void title_back(void) {
    int slot = G.demo ? G.menu_slot : G.save_slot;
    title_open();
    if (slot > 0) {
        G.menu_slot = slot;
        if (!save_menu_open(-1)) G.menu_slot = 0;
    }
}

static void saves_open(void) {
    refresh_saves();
    if (nsaves == 0) {
        sfx_play(SFX_DENIED);
        return;
    }
    page_open(TP_SAVES, 0);
    G.load_sel = 0;
    load_scroll = 0;
    sfx_play(SFX_WEAPON_SWITCH);
}

static void saves_close(void) {
    page_open(TP_MAIN, TM_CONTINUE);
    if (!main_enabled(G.title_sel)) G.title_sel = TM_NEW;
    sfx_play(SFX_PAUSE);
}

/* DEV MODE: its own save (SAVE_DEV_SLOT, never listed), everything unlocked and every enemy met
 * (made so again each time, its runs and records kept), opened on its menu. Without saving
 * (headless, screenshots): a run at once, everything unlocked. */
static void dev_mode(void) {
    Profile old, all;
    SaveInfo info;
    if (!(save_info(SAVE_DEV_SLOT, &info) && info.ok && save_load_profile(SAVE_DEV_SLOT, &old))) profile_fresh(&old);
    profile_unlock_all(&all);
    all.runs = old.runs, all.deaths = old.deaths, all.bosses = old.bosses, all.best_depth = old.best_depth;
    if (!save_enabled()) {
        G.prof = all;
        G.save_slot = G.menu_slot = 0;
        game_new_run();
        return;
    }
    G.menu_slot = SAVE_DEV_SLOT;
    if (!save_write_profile(SAVE_DEV_SLOT, &all) || !save_menu_open(-1)) {
        G.menu_slot = 0;
        sfx_play(SFX_DENIED);
        return;
    }
    sfx_play(SFX_UI_START);
}

/* NEW SAVE: a new file, nothing unlocked yet: the first ship's run starts at once. */
static void new_save(void) {
    int slot = save_new_slot();
    if (save_enabled() && slot <= 0) { /* every slot taken */
        sfx_play(SFX_DENIED);
        return;
    }
    profile_fresh(&G.prof);
    G.character = CHAR_NORMAL;
    G.save_slot = G.menu_slot = slot;
    game_new_run();
}

static void main_activate(int item) {
    switch (item) {
    case TM_NEW: new_save(); break;
    case TM_CONTINUE: saves_open(); break;
    case TM_DEV: dev_mode(); break;
    case TM_QUIT: G.quit = true; break;
    default: break;
    }
}

static void save_select(int i) {
    if (i == G.load_sel) return;
    G.load_sel = i;
    G.load_confirm = 0;
    sfx_play(SFX_WEAPON_SWITCH);
}

/* DEL (or the X): a first press asks, a second one deletes. */
static void save_remove(int i) {
    int slot = saves[i].slot;
    if (G.load_confirm != slot) {
        G.load_confirm = slot;
        sfx_play(SFX_BEEP);
        return;
    }
    save_delete(slot);
    G.load_confirm = 0;
    sfx_play(SFX_SHATTER);
    refresh_saves();
    if (nsaves == 0) saves_close();
}

static void save_open(int i) {
    G.menu_slot = saves[i].slot;
    if (!saves[i].ok || !save_menu_open(-1)) {
        G.menu_slot = 0;
        sfx_play(SFX_DENIED);
        refresh_saves();
        return;
    }
    sfx_play(SFX_UI_START);
}

static void saves_update(void) {
    const Input *in = &G.in;
    if (in->up && !G.title_up && G.load_sel > 0) save_select(G.load_sel - 1);
    if (in->down && !G.title_down && G.load_sel < nsaves - 1) save_select(G.load_sel + 1);
    if (in->wheel) save_select((int)clampf((float)(G.load_sel + in->wheel), 0.0f, (float)(nsaves - 1)));
    bool on_delete = false;
    int hover = row_at(in->mouse, &on_delete);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0) save_select(hover);
        G.sel_mouse = in->mouse;
    }
    if (G.load_sel < load_scroll) load_scroll = G.load_sel;
    if (G.load_sel >= load_scroll + LOAD_ROWS) load_scroll = G.load_sel - LOAD_ROWS + 1;
    if (in->pause_pressed) {
        if (G.load_confirm) G.load_confirm = 0, sfx_play(SFX_PAUSE);
        else saves_close();
    } else if (in->click_pressed) {
        if (hover >= 0 && on_delete) save_remove(hover);
        else if (hover >= 0) save_open(hover);
    } else if (in->delete_pressed && nsaves > 0) {
        save_remove(G.load_sel);
    } else if (in->confirm_pressed && nsaves > 0) {
        save_open(G.load_sel);
    }
}

static void save_menu_update(void) {
    bool paused = G.state == GS_PAUSED; /* over a run going on (pause_open) */
    if (paused && G.in.pause_pressed) {
        G.state = GS_PLAYING;
        sfx_play(SFX_PAUSE);
        return;
    }
    if (G.in.pause_pressed && G.menu_slot == SAVE_DEV_SLOT) { /* DEV MODE: back to the main menu */
        G.menu_slot = 0;
        page_open(TP_MAIN, TM_DEV);
        sfx_play(SFX_PAUSE);
        return;
    }
    if (G.in.pause_pressed) { /* back to the list, on this save */
        int slot = G.menu_slot;
        G.menu_slot = 0;
        saves_open();
        for (int i = 0; i < nsaves; i++)
            if (saves[i].slot == slot) G.load_sel = i;
        load_scroll = G.load_sel >= LOAD_ROWS ? G.load_sel - LOAD_ROWS + 1 : 0;
        return;
    }
    switch (btn_update(&SAVE_MENU)) {
    case SM_RESUME:
        if (paused) { /* the run in memory goes on where it stopped */
            G.state = GS_PLAYING;
            sfx_play(SFX_PAUSE);
        } else if (save_load(G.menu_slot)) sfx_play(SFX_UI_START); /* G is now the saved run, at the start of its room */
        else sfx_play(SFX_DENIED);
        break;
    case SM_NEW_RUN: /* the run goes in this save (char_select_update); from a pause, ESC comes back to it */
        if (paused) char_select_open_paused();
        else char_select_open();
        break;
    case SM_UNLOCKS:
        page_open(TP_UNLOCKS, 0);
        G.unl_sel = 0;
        sfx_play(SFX_WEAPON_SWITCH);
        break;
    case SM_BESTIARY:
        page_open(TP_BESTIARY, 0);
        bestiary_open(-1);
        sfx_play(SFX_WEAPON_SWITCH);
        break;
    case SM_MAIN_MENU: /* from the pause, out of the run: it stays saved at the start of its room */
        title_open();
        sfx_play(SFX_PAUSE);
        break;
    case SM_QUIT: G.quit = true; break; /* the game closes (the run saved at the start of its room) */
    default: break;
    }
}

/* ---- Unlocks screen ----------------------------------------------------- */

static void unl_cell(int u, float *x, float *y) {
    *x = UNL_X0 + (float)(u / UNL_ROWS) * UNL_CW;
    *y = UNL_Y0 + (float)(u % UNL_ROWS) * UNL_RH;
}

static int unl_at(V2 m) {
    for (int u = 0; u < UPG_COUNT; u++) {
        float x, y;
        unl_cell(u, &x, &y);
        if (m.x >= x && m.x < x + UNL_CW - 8.0f && m.y >= y && m.y < y + UNL_RH) return u;
    }
    return -1;
}

bool title_save_menu(int slot) {
    G.menu_slot = slot;
    if (save_menu_open(-1)) return true;
    G.menu_slot = 0;
    return false;
}

void title_unlocks_open(void) {
    G.state = GS_TITLE;
    page_open(TP_UNLOCKS, 0);
    G.unl_sel = 0;
}

bool title_world_hidden(void) { return (G.state == GS_TITLE || G.state == GS_PAUSED) && G.title_page == TP_BESTIARY; }

void title_bestiary_open(int type) {
    G.state = GS_TITLE;
    page_open(TP_BESTIARY, 0);
    bestiary_open(type);
}

static void unl_select(int u) {
    u = u < 0 ? 0 : u >= UPG_COUNT ? UPG_COUNT - 1 : u;
    if (u != G.unl_sel) sfx_play(SFX_WEAPON_SWITCH);
    G.unl_sel = u;
}

static void unlocks_update(void) {
    const Input *in = &G.in;
    if (in->up && !G.title_up) unl_select(G.unl_sel - 1);
    if (in->down && !G.title_down) unl_select(G.unl_sel + 1);
    if (in->left && !G.sel_left && G.unl_sel >= UNL_ROWS) unl_select(G.unl_sel - UNL_ROWS);
    if (in->right && !G.sel_right && G.unl_sel + UNL_ROWS < UPG_COUNT) unl_select(G.unl_sel + UNL_ROWS);
    if (in->wheel) unl_select(G.unl_sel + in->wheel);
    int hover = unl_at(in->mouse);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0) unl_select(hover);
        G.sel_mouse = in->mouse;
    }
    if (in->click_pressed && hover >= 0) unl_select(hover);
    if (in->pause_pressed) {
        page_open(TP_SAVE, SM_UNLOCKS);
        sfx_play(SFX_PAUSE);
    }
}

/* ---- The pause: the save's menu over the run going on ---------------------- */

/* What the save's menu shows of the run: the one in memory (its save may be older: start of the room). */
static void pause_cur(void) {
    cur = (SaveInfo){.slot = G.save_slot, .ok = true, .has_run = true, .character = G.character, .depth = G.room.depth};
}

void pause_open(void) {
    G.state = GS_PAUSED;
    pause_cur();
    page_open(TP_SAVE, SM_RESUME);
    sfx_play(SFX_PAUSE);
}

void pause_open_new_run(void) {
    pause_open();
    G.title_sel = SM_NEW_RUN;
}

void pause_update(void) {
    const Input *in = &G.in;
    switch ((TitlePage)G.title_page) {
    case TP_UNLOCKS: unlocks_update(); break;
    case TP_BESTIARY:
        if (!bestiary_update()) {
            page_open(TP_SAVE, SM_BESTIARY);
            sfx_play(SFX_PAUSE);
        }
        break;
    default:
        G.title_page = TP_SAVE;
        pause_cur();
        if (in->menu_pressed) upgrade_menu_open(); /* TAB: the upgrades menu, back here after */
        else save_menu_update();
        break;
    }
    G.title_up = in->up;
    G.title_down = in->down;
    G.sel_left = in->left;
    G.sel_right = in->right;
}

void title_update(void) {
    if (!G.demo) demo_start(); /* back from a real run: a new demo behind the menu */
    if (!listed) refresh_saves();
    const Input *in = &G.in;
    switch ((TitlePage)G.title_page) {
    case TP_SAVES: saves_update(); break;
    case TP_SAVE: save_menu_update(); break;
    case TP_UNLOCKS: unlocks_update(); break;
    case TP_BESTIARY:
        if (!bestiary_update()) {
            page_open(TP_SAVE, SM_BESTIARY);
            sfx_play(SFX_PAUSE);
        }
        break;
    case TP_MAIN:
    default:
        if (in->pause_pressed) G.quit = true;
        else main_activate(btn_update(&MAIN_MENU));
        break;
    }
    G.title_up = in->up;
    G.title_down = in->down;
    G.sel_left = in->left;
    G.sel_right = in->right;
    /* Not once a run started, nor behind the bestiary (its small world owns the path finder's caches). */
    if ((G.state == GS_TITLE && G.title_page != TP_BESTIARY) || G.state == GS_CHAR_SELECT) demo_update();
}

static void format_duration(char *buf, size_t n, float t) {
    int s = (int)t;
    if (s >= 3600) snprintf(buf, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void panel(float y0, float y1) {
    float x0 = (SCREEN_W - ROW_W) * 0.5f;
    draw_rect(x0 - 50.0f, y0, ROW_W + 100.0f, y1 - y0, rgba(0.01f, 0.01f, 0.03f, 0.8f));
}

static void saves_draw(void) {
    float x0 = (SCREEN_W - ROW_W) * 0.5f;
    panel(150.0f, row_y(LOAD_ROWS) + 30.0f);
    draw_text_centered(SCREEN_W * 0.5f, 185.0f, 5.0f, col_hex(0x66f0ff, 1), "CONTINUE");
    draw_text_centered(SCREEN_W * 0.5f, 250.0f, 1.75f, col_a(COL_WHITE, 0.5f),
                       "a save keeps its unlocks from run to run; a run is saved on entering each room");
    for (int v = 0; v < LOAD_ROWS && load_scroll + v < nsaves; v++) {
        int i = load_scroll + v;
        const SaveInfo *sv = &saves[i];
        float y = row_y(v), ty = y + ROW_H * 0.5f - 9.0f;
        bool sel = i == G.load_sel, asking = G.load_confirm == sv->slot;
        SDL_FColor c = sv->ok ? col_hex(0x66f0ff, 1) : col_a(COL_WHITE, 0.35f);
        if (asking) c = col_hex(0xff4d6a, 1);
        draw_rect(x0, y, ROW_W, ROW_H, sel ? col_a(c, 0.18f) : rgba(0.02f, 0.03f, 0.07f, 0.9f));
        draw_rect_outline(x0, y, ROW_W, ROW_H, sel ? 3.0f : 1.5f, col_a(c, sel ? 1.0f : 0.45f));
        /* the X */
        float dx = x0 + ROW_W - DEL_W - 8.0f;
        draw_rect_outline(dx, y + 8.0f, DEL_W, ROW_H - 16.0f, 2.0f, col_hex(0xff4d6a, sel ? 1.0f : 0.4f));
        draw_text_centered(dx + DEL_W * 0.5f, ty, 2.5f, col_hex(0xff4d6a, sel ? 1.0f : 0.4f), "X");
        if (asking) {
            draw_text(x0 + 28.0f, ty, 2.25f, c, "DELETE THIS SAVE AND ITS UNLOCKS?   DEL / X again    ESC cancel");
            continue;
        }
        draw_text(x0 + 28.0f, ty, 2.5f, c, "SAVE %d", sv->slot);
        if (!sv->ok) {
            draw_text(x0 + 230.0f, ty, 2.25f, c, "OLD VERSION - can only be deleted");
            continue;
        }
        SDL_FColor w = col_a(COL_WHITE, sel ? 1.0f : 0.8f);
        /* unlock %: a gauge */
        float gx = x0 + 230.0f, gw = 150.0f;
        draw_rect(gx, y + 22.0f, gw, 12.0f, col_a(COL_WHITE, 0.12f));
        draw_rect(gx, y + 22.0f, gw * (float)sv->percent / 100.0f, 12.0f, col_hex(0xffd23f, 1));
        draw_text(gx, y + 42.0f, 1.75f, col_hex(0xffd23f, 1), "%d%% UNLOCKED", sv->percent);
        draw_text(x0 + 410.0f, ty + 2.0f, 1.75f, col_a(w, 0.7f), "RUNS %d  BOSSES %d  BEST ROOM %d", sv->runs, sv->bosses, sv->best_depth);
        if (sv->has_run) {
            char tbuf[16];
            format_duration(tbuf, sizeof tbuf, sv->run_time);
            int ch = sv->character >= 0 && sv->character < CHAR_COUNT ? sv->character : 0;
            draw_text(x0 + 900.0f, ty - 10.0f, 2.0f, col_hex(CHARACTERS[ch].color, 1), "%s", CHARACTERS[ch].name);
            draw_text(x0 + 900.0f, ty + 14.0f, 1.75f, w, "ROOM %d  HP %d/%d  %s", sv->depth, sv->hp, sv->max_hp, tbuf);
        } else {
            draw_text(x0 + 900.0f, ty + 2.0f, 1.75f, col_a(COL_WHITE, 0.45f), "no run going on");
        }
        SDL_DateTime dt;
        if (SDL_TimeToDateTime(sv->saved_at, &dt, true)) {
            char date[32];
            snprintf(date, sizeof date, "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
            draw_text(dx - 16.0f - draw_text_width(date, 1.5f), y + 8.0f, 1.5f, col_a(COL_WHITE, 0.45f), "%s", date);
        }
    }
    if (load_scroll > 0) draw_text_centered(SCREEN_W * 0.5f, ROW_Y0 - 28.0f, 2.0f, col_a(COL_WHITE, 0.6f), "...");
    if (load_scroll + LOAD_ROWS < nsaves) draw_text_centered(SCREEN_W * 0.5f, row_y(LOAD_ROWS) - 6.0f, 2.0f, col_a(COL_WHITE, 0.6f), "...");
    draw_text_centered(SCREEN_W * 0.5f, 1000.0f, 1.5f, col_a(COL_WHITE, 0.45f),
                       "ARROWS / MOUSE  choose     ENTER / CLICK  open     DEL / X  delete     ESC  back");
}

static void save_menu_draw(void) {
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    draw_rect(x0 - 200.0f, 150.0f, BTN_W + 400.0f, btn_y(&SAVE_MENU, SM_COUNT) - BTN_GAP + 40.0f - 150.0f,
              rgba(0.01f, 0.01f, 0.03f, 0.8f));
    bool paused = G.state == GS_PAUSED;
    if (cur.slot == SAVE_DEV_SLOT) draw_text_centered(SCREEN_W * 0.5f, 185.0f, 5.0f, col_hex(0xffa040, 1), paused ? "DEV MODE - PAUSED" : "DEV MODE");
    else if (cur.slot > 0) draw_text_centered(SCREEN_W * 0.5f, 185.0f, 5.0f, col_hex(0x66f0ff, 1), paused ? "SAVE %d - PAUSED" : "SAVE %d", cur.slot);
    else draw_text_centered(SCREEN_W * 0.5f, 185.0f, 5.0f, col_hex(0x66f0ff, 1), "PAUSED"); /* a run outside any save */
    int pct = unlock_percent(profile_unlocked(&G.prof), unlocks_total());
    float gw = 500.0f, gx = (SCREEN_W - gw) * 0.5f;
    draw_rect(gx, 255.0f, gw, 16.0f, col_a(COL_WHITE, 0.12f));
    draw_rect(gx, 255.0f, gw * (float)pct / 100.0f, 16.0f, col_hex(0xffd23f, 1));
    draw_text_centered(SCREEN_W * 0.5f, 285.0f, 2.25f, col_hex(0xffd23f, 1), "%d%% UNLOCKED", pct);
    draw_text_centered(SCREEN_W * 0.5f, 325.0f, 1.75f, col_a(COL_WHITE, 0.6f), "RUNS %d     BOSSES BEATEN %d     BEST ROOM %d",
                       G.prof.runs, G.prof.bosses, G.prof.best_depth);
    if (cur.has_run) {
        int ch = cur.character >= 0 && cur.character < CHAR_COUNT ? cur.character : 0;
        draw_text_centered(SCREEN_W * 0.5f, 365.0f, 1.75f, col_hex(CHARACTERS[ch].color, 0.9f), "%s: %s, ROOM %d",
                           paused ? "THIS RUN" : "RUN GOING ON", CHARACTERS[ch].name, cur.depth);
    }
    btn_draw(&SAVE_MENU);
    if (cur.has_run && G.title_sel == SM_NEW_RUN)
        draw_text_centered(SCREEN_W * 0.5f, btn_y(&SAVE_MENU, SM_COUNT) - BTN_GAP + 60.0f, 1.75f, col_hex(0xff6a7e, 0.9f),
                           "a new run replaces the one going on");
    if (paused)
        draw_text_centered(SCREEN_W * 0.5f, 1000.0f, 1.5f, col_a(COL_WHITE, 0.45f),
                           "ARROWS / MOUSE  choose     ENTER / CLICK  confirm     ESC  resume     TAB  upgrades");
    else draw_text_centered(SCREEN_W * 0.5f, 1000.0f, 1.5f, col_a(COL_WHITE, 0.45f), "ARROWS / MOUSE  choose     ENTER / CLICK  confirm     ESC  back");
}

/* Kind of upgrade, as the player sees it (never the word "weapon"). */
static const char *upgrade_kind(int u) {
    static char buf[64];
    if (UPGRADES[u].tier > 0) {
        SDL_snprintf(buf, sizeof buf, "WORKSHOP AUGMENT - costs %d max heart%s", UPGRADES[u].tier, UPGRADES[u].tier > 1 ? "s" : "");
        return buf;
    }
    if (upgrade_is_weapon(u)) return "SHOT MOD (combines with the others)";
    return "UPGRADE";
}

static void unlocks_detail(int u) {
    float cx = UNL_PX + UNL_PW * 0.5f, y = UNL_Y0;
    draw_rect(UNL_PX, y - 20.0f, UNL_PW, UNL_ROWS * UNL_RH + 30.0f, rgba(0.02f, 0.03f, 0.07f, 0.9f));
    bool open = upgrade_unlocked(u);
    SDL_FColor c = col_hex(UPGRADES[u].color, 1);
    draw_rect_outline(UNL_PX, y - 20.0f, UNL_PW, UNL_ROWS * UNL_RH + 30.0f, 2.0f, open ? col_a(c, 0.6f) : col_a(COL_WHITE, 0.2f));
    V2 icon = v2(cx, y + 110.0f);
    if (!open) {
        draw_ngon_outline(icon, 60.0f, 6, 0.0f, 3.0f, col_a(COL_WHITE, 0.25f));
        draw_text_centered(cx, icon.y - 20.0f, 5.0f, col_a(COL_WHITE, 0.35f), "?");
        draw_text_centered(cx, y + 230.0f, 3.5f, col_a(COL_WHITE, 0.45f), "%s", UPGRADES[u].name);
        draw_text_centered(cx, y + 300.0f, 4.0f, col_a(COL_WHITE, 0.6f), "LOCKED");
        draw_text_centered(cx, y + 370.0f, 2.0f, col_a(COL_WHITE, 0.55f), "Each boss defeated unlocks");
        draw_text_centered(cx, y + 398.0f, 2.0f, col_a(COL_WHITE, 0.55f), "%d more upgrades", UNLOCK_BATCH);
        return;
    }
    draw_blend_add();
    draw_glow(icon, 150.0f, col_a(c, 0.22f));
    draw_blend_alpha();
    draw_pickup_icon(UPGRADES[u].tier > 0 ? PK_AUGMENT : PK_UPGRADE, u, icon, 52.0f, G.time);
    draw_text_centered(cx, y + 220.0f, 4.0f, c, "%s", UPGRADES[u].name);
    draw_text_centered(cx, y + 268.0f, 1.75f, col_a(COL_WHITE, 0.55f), "%s", upgrade_kind(u));
    draw_desc(cx, y + 320.0f, 2.25f, col_a(COL_WHITE, 0.95f), upgrade_desc_at(u, 1));
    const char *mal = upgrade_malus_desc(u);
    draw_desc(cx, y + 390.0f, 2.25f, col_hex(0xff6a7e, 1), mal);
    float ly = y + 470.0f;
    draw_text_centered(cx, ly, 1.75f, col_a(COL_WHITE, 0.6f), "%s",
                       upgrade_toggleable(u) ? "Can be turned off and back on (TAB menu)" : "One-time effect: cannot be turned off");
    /* The synergies it takes part in. */
    char line[160] = "";
    int nsyn = 0;
    ly += 50.0f;
    for (int s = 0; s < SYN_COUNT; s++) {
        const SynergyDef *d = &SYNERGIES[s];
        bool in = false;
        for (int k = 0; k < 3; k++) in = in || d->need[k] == u + 1;
        for (int k = 0; k < 6; k++) in = in || d->any1[k] == u + 1 || d->any2[k] == u + 1;
        if (!in) continue;
        if (nsyn == 0) draw_text_centered(cx, ly, 2.0f, col_hex(0xffd23f, 1), "SYNERGIES");
        size_t len = strlen(line);
        if (len + strlen(d->name) + 2 > 40) { /* a full line */
            draw_text_centered(cx, ly + 34.0f, 1.75f, col_hex(d->color, 0.9f), "%s", line);
            ly += 26.0f;
            line[0] = 0;
            len = 0;
        }
        SDL_snprintf(line + len, sizeof line - len, "%s%s", len ? ", " : "", d->name);
        nsyn++;
    }
    if (line[0]) draw_text_centered(cx, ly + 34.0f, 1.75f, col_hex(0xffe9a0, 0.9f), "%s", line);
}

static void unlocks_draw(void) {
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.75f));
    int nu = profile_upgrades_unlocked(&G.prof), ns = profile_unlocked(&G.prof) - nu;
    draw_text(UNL_X0, 80.0f, 5.0f, col_hex(0x66f0ff, 1), "UNLOCKS");
    draw_text(UNL_X0, 140.0f, 2.0f, col_a(COL_WHITE, 0.75f), "UPGRADES %d / %d     SHIPS %d / %d     %d%%", nu, UPG_COUNT, ns, CHAR_COUNT,
              unlock_percent(nu + ns, unlocks_total()));
    for (int u = 0; u < UPG_COUNT; u++) {
        float x, y;
        unl_cell(u, &x, &y);
        bool open = upgrade_unlocked(u), sel = u == G.unl_sel;
        SDL_FColor c = col_hex(UPGRADES[u].color, 1);
        if (sel) draw_rect(x - 4.0f, y, UNL_CW - 8.0f, UNL_RH - 2.0f, open ? col_a(c, 0.25f) : col_a(COL_WHITE, 0.1f));
        if (sel) draw_rect_outline(x - 4.0f, y, UNL_CW - 8.0f, UNL_RH - 2.0f, 1.5f, open ? c : col_a(COL_WHITE, 0.4f));
        if (open) draw_ngon(v2(x + 8.0f, y + UNL_RH * 0.5f - 1.0f), 6.0f, 3 + u % 4, 0.0f, c);
        else draw_ngon_outline(v2(x + 8.0f, y + UNL_RH * 0.5f - 1.0f), 6.0f, 3 + u % 4, 0.0f, 1.5f, col_a(COL_WHITE, 0.2f));
        draw_text(x + 22.0f, y + 5.0f, 1.5f, open ? col_a(COL_WHITE, sel ? 1.0f : 0.85f) : col_a(COL_WHITE, sel ? 0.4f : 0.22f), "%s",
                  UPGRADES[u].name);
    }
    unlocks_detail(G.unl_sel);
    draw_text_centered(SCREEN_W * 0.5f, 1030.0f, 1.5f, col_a(COL_WHITE, 0.45f), "ARROWS / MOUSE  choose     ESC  back");
}

void pause_draw(void) {
    switch ((TitlePage)G.title_page) {
    case TP_UNLOCKS:
        draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.6f));
        unlocks_draw();
        return;
    case TP_BESTIARY: bestiary_draw(); return;
    default:
        draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.6f));
        save_menu_draw();
        return;
    }
}

void title_draw(void) {
    /* Darken the demo enough for the menu to read, lighter in the middle band. */
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.45f));
    switch ((TitlePage)G.title_page) {
    case TP_SAVES: saves_draw(); return;
    case TP_SAVE: save_menu_draw(); return;
    case TP_UNLOCKS: unlocks_draw(); return;
    case TP_BESTIARY: bestiary_draw(); return;
    default: break;
    }
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    draw_rect(x0 - 60.0f, 120.0f, BTN_W + 120.0f, btn_y(&MAIN_MENU, TM_COUNT - 1) + BTN_H + 40.0f - 120.0f, rgba(0.01f, 0.01f, 0.03f, 0.7f));

    float pulse = 0.5f + 0.5f * sinf(G.time * 2.0f);
    draw_blend_add();
    draw_glow(v2(SCREEN_W * 0.5f, 245.0f), 420.0f, col_hex(0x66f0ff, 0.10f + 0.05f * pulse));
    draw_blend_alpha();
    draw_text_centered(SCREEN_W * 0.5f + 5.0f, 205.0f, 11.0f, col_hex(0xff4fd8, 0.55f), "NEON BREACH"); /* chromatic offset */
    draw_text_centered(SCREEN_W * 0.5f, 200.0f, 11.0f, col_hex(0x66f0ff, 1), "NEON BREACH");
    draw_text_centered(SCREEN_W * 0.5f, 330.0f, 2.5f, col_hex(0xff4fd8, 1), "a roguelite bullet storm");
    btn_draw(&MAIN_MENU);

    const char *lines[] = {
        "WASD / ZQSD / ARROWS  move     MOUSE  aim & shoot     SHIFT / RMB  active items     SPACE  bomb",
        "E  take / buy / use     TAB  upgrades on / off     ESC  pause     M  mute     F11  fullscreen",
    };
    for (int i = 0; i < ARRAY_LEN(lines); i++)
        draw_text_centered(SCREEN_W * 0.5f, 900.0f + (float)i * 32.0f, 1.75f, col_a(COL_WHITE, 0.65f), "%s", lines[i]);
    draw_text_centered(SCREEN_W * 0.5f, 1000.0f, 1.5f, col_a(COL_WHITE, 0.35f), "ARROWS / MOUSE  choose     ENTER / CLICK  confirm     ESC  quit");
}
