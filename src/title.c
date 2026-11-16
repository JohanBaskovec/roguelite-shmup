#include "audio.h"
#include "bot.h"
#include "draw.h"
#include "game.h"
#include "save.h"

#include <stdio.h>
#include <string.h>

/*
 * Main menu (GS_TITLE): NEW GAME / LOAD / OPTIONS / UNLOCKS / QUIT over a real game played by the
 * bot. LOAD opens the list of save files (one per run, saved on entering each room - save.c):
 * load one, or delete it (DEL / the X, twice).
 *
 * The background demo lives in the normal world (G), flagged G.demo, and runs on its own
 * random generator (G.demo_rng, swapped in for each demo tick) with sounds suppressed, so it
 * never changes the real game's randomness. Starting a run (game_new_run) wipes it. The demo
 * keeps playing behind the character select screen opened from the menu.
 */

enum { TM_NEW, TM_LOAD, TM_OPTIONS, TM_UNLOCKS, TM_QUIT, TM_COUNT };
static const char *const TITLE_ITEMS[TM_COUNT] = {"NEW GAME", "LOAD", "OPTIONS", "UNLOCKS", "QUIT"};

/* The save files, as listed when the menu opened (and after each deletion). */
static SaveInfo saves[SAVE_SLOTS];
static int nsaves, load_scroll;
static bool listed;

static void refresh_saves(void) {
    nsaves = save_list(saves, SAVE_SLOTS);
    listed = true;
    if (G.load_sel >= nsaves) G.load_sel = nsaves > 0 ? nsaves - 1 : 0;
}

/* Options / unlocks: later. LOAD needs a save file. */
static bool item_enabled(int i) { return i == TM_NEW || i == TM_QUIT || (i == TM_LOAD && nsaves > 0); }

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

static float btn_y(int i) { return BTN_Y0 + (float)i * (BTN_H + BTN_GAP); }

static int item_at(V2 m) {
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    for (int i = 0; i < TM_COUNT; i++)
        if (m.x >= x0 && m.x <= x0 + BTN_W && m.y >= btn_y(i) && m.y <= btn_y(i) + BTN_H) return i;
    return -1;
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
    int mod = irand(UPG_W_SCATTER, UPG_W_SWORD - 1);
    if (!upgrade_capped(mod)) upgrade_apply(mod);
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

void title_open(void) {
    G.state = GS_TITLE;
    G.title_page = 0;
    G.title_sel = TM_NEW;
    G.load_confirm = 0;
    G.save_slot = 0; /* leaving a run: its save (start of the room it was in) stays */
    G.sel_mouse = G.in.mouse;
    G.title_up = G.in.up; /* a key already held does not count as a press */
    G.title_down = G.in.down;
    refresh_saves();
}

static void title_move(int dir) {
    int s = G.title_sel;
    do s = (s + dir + TM_COUNT) % TM_COUNT;
    while (!item_enabled(s));
    if (s != G.title_sel) sfx_play(SFX_WEAPON_SWITCH);
    G.title_sel = s;
}

static void load_open(void) {
    refresh_saves();
    if (nsaves == 0) {
        sfx_play(SFX_DENIED);
        return;
    }
    G.title_page = 1;
    G.load_sel = 0;
    G.load_confirm = 0;
    load_scroll = 0;
    sfx_play(SFX_WEAPON_SWITCH);
}

static void load_close(void) {
    G.title_page = 0;
    G.load_confirm = 0;
    if (!item_enabled(G.title_sel)) G.title_sel = TM_NEW;
    sfx_play(SFX_PAUSE);
}

static void title_activate(int item) {
    if (!item_enabled(item)) {
        sfx_play(SFX_DENIED);
        return;
    }
    switch (item) {
    case TM_NEW: char_select_open(); break;
    case TM_LOAD: load_open(); break;
    case TM_QUIT: G.quit = true; break;
    default: break;
    }
}

static void load_select(int i) {
    if (i == G.load_sel) return;
    G.load_sel = i;
    G.load_confirm = 0;
    sfx_play(SFX_WEAPON_SWITCH);
}

/* DEL (or the X): a first press asks, a second one deletes. */
static void load_delete(int i) {
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
    if (nsaves == 0) load_close();
}

static void load_game(int i) {
    if (!saves[i].ok || !save_load(saves[i].slot)) {
        sfx_play(SFX_DENIED);
        refresh_saves();
        return;
    }
    sfx_play(SFX_UI_START); /* G is now the saved run, at the start of its room */
}

static void load_update(void) {
    const Input *in = &G.in;
    if (in->up && !G.title_up && G.load_sel > 0) load_select(G.load_sel - 1);
    if (in->down && !G.title_down && G.load_sel < nsaves - 1) load_select(G.load_sel + 1);
    if (in->wheel) load_select((int)clampf((float)(G.load_sel + in->wheel), 0.0f, (float)(nsaves - 1)));
    bool on_delete = false;
    int hover = row_at(in->mouse, &on_delete);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0) load_select(hover);
        G.sel_mouse = in->mouse;
    }
    if (G.load_sel < load_scroll) load_scroll = G.load_sel;
    if (G.load_sel >= load_scroll + LOAD_ROWS) load_scroll = G.load_sel - LOAD_ROWS + 1;
    if (in->pause_pressed) {
        if (G.load_confirm) G.load_confirm = 0, sfx_play(SFX_PAUSE);
        else load_close();
    } else if (in->click_pressed) {
        if (hover >= 0 && on_delete) load_delete(hover);
        else if (hover >= 0) load_game(hover);
    } else if (in->delete_pressed && nsaves > 0) {
        load_delete(G.load_sel);
    } else if (in->confirm_pressed && nsaves > 0) {
        load_game(G.load_sel);
    }
}

void title_update(void) {
    if (!G.demo) demo_start(); /* back from a real run: a new demo behind the menu */
    if (!listed) refresh_saves();
    const Input *in = &G.in;
    if (G.title_page == 1) {
        load_update();
        G.title_up = in->up;
        G.title_down = in->down;
        demo_update(); /* nothing if a save was just loaded (no demo any more) */
        return;
    }
    if (in->up && !G.title_up) title_move(-1);
    if (in->down && !G.title_down) title_move(1);
    G.title_up = in->up;
    G.title_down = in->down;
    if (in->wheel) title_move(in->wheel);
    /* The mouse only takes over when it actually moves. */
    int hover = item_at(in->mouse);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0 && item_enabled(hover) && hover != G.title_sel) {
            G.title_sel = hover;
            sfx_play(SFX_WEAPON_SWITCH);
        }
        G.sel_mouse = in->mouse;
    }
    if (in->pause_pressed) G.quit = true;
    else if (in->click_pressed) {
        if (hover >= 0) title_activate(hover);
    } else if (in->confirm_pressed) {
        title_activate(G.title_sel);
    }
    demo_update();
}

static void format_duration(char *buf, size_t n, float t) {
    int s = (int)t;
    if (s >= 3600) snprintf(buf, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void load_draw(void) {
    float x0 = (SCREEN_W - ROW_W) * 0.5f;
    draw_rect(x0 - 50.0f, 150.0f, ROW_W + 100.0f, row_y(LOAD_ROWS) + 30.0f - 150.0f, rgba(0.01f, 0.01f, 0.03f, 0.8f));
    draw_text_centered(SCREEN_W * 0.5f, 185.0f, 5.0f, col_hex(0x66f0ff, 1), "LOAD GAME");
    draw_text_centered(SCREEN_W * 0.5f, 250.0f, 1.75f, col_a(COL_WHITE, 0.5f), "each save is the start of the last room entered");
    for (int v = 0; v < LOAD_ROWS && load_scroll + v < nsaves; v++) {
        int i = load_scroll + v;
        const SaveInfo *sv = &saves[i];
        float y = row_y(v), ty = y + ROW_H * 0.5f - 9.0f;
        bool sel = i == G.load_sel, asking = G.load_confirm == sv->slot;
        SDL_FColor ship = col_hex(CHARACTERS[sv->character].color, 1), c = sv->ok ? col_hex(0x66f0ff, 1) : col_a(COL_WHITE, 0.35f);
        if (asking) c = col_hex(0xff4d6a, 1);
        draw_rect(x0, y, ROW_W, ROW_H, sel ? col_a(c, 0.18f) : rgba(0.02f, 0.03f, 0.07f, 0.9f));
        draw_rect_outline(x0, y, ROW_W, ROW_H, sel ? 3.0f : 1.5f, col_a(c, sel ? 1.0f : 0.45f));
        /* the X */
        float dx = x0 + ROW_W - DEL_W - 8.0f;
        draw_rect_outline(dx, y + 8.0f, DEL_W, ROW_H - 16.0f, 2.0f, col_hex(0xff4d6a, sel ? 1.0f : 0.4f));
        draw_text_centered(dx + DEL_W * 0.5f, ty, 2.5f, col_hex(0xff4d6a, sel ? 1.0f : 0.4f), "X");
        if (asking) {
            draw_text(x0 + 28.0f, ty, 2.25f, c, "DELETE THIS SAVE?   DEL / X again to confirm    ESC cancel");
            continue;
        }
        draw_text(x0 + 28.0f, ty, 2.5f, sv->ok ? ship : c, "%s", CHARACTERS[sv->character].name);
        if (!sv->ok) {
            draw_text(x0 + 300.0f, ty, 2.25f, c, "OLD VERSION - can only be deleted");
            continue;
        }
        char tbuf[16];
        format_duration(tbuf, sizeof tbuf, sv->run_time);
        SDL_FColor w = col_a(COL_WHITE, sel ? 1.0f : 0.8f);
        draw_text(x0 + 300.0f, ty, 2.25f, w, "ROOM %d", sv->depth);
        draw_text(x0 + 470.0f, ty, 2.25f, w, "HP %d/%d", sv->hp, sv->max_hp);
        draw_text(x0 + 620.0f, ty, 2.25f, w, "UPGRADES %d", sv->upgrades);
        draw_text(x0 + 840.0f, ty, 2.25f, w, "%s", tbuf);
        SDL_DateTime dt;
        if (SDL_TimeToDateTime(sv->saved_at, &dt, true)) {
            char date[32];
            snprintf(date, sizeof date, "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
            draw_text(dx - 24.0f - draw_text_width(date, 1.75f), ty + 3.0f, 1.75f, col_a(COL_WHITE, 0.5f), "%s", date);
        }
    }
    if (load_scroll > 0) draw_text_centered(SCREEN_W * 0.5f, ROW_Y0 - 28.0f, 2.0f, col_a(COL_WHITE, 0.6f), "...");
    if (load_scroll + LOAD_ROWS < nsaves) draw_text_centered(SCREEN_W * 0.5f, row_y(LOAD_ROWS) - 6.0f, 2.0f, col_a(COL_WHITE, 0.6f), "...");
    draw_text_centered(SCREEN_W * 0.5f, 1000.0f, 1.5f, col_a(COL_WHITE, 0.45f),
                       "ARROWS / MOUSE  choose     ENTER / CLICK  load     DEL / X  delete     ESC  back");
}

void title_draw(void) {
    /* Darken the demo enough for the menu to read, lighter in the middle band. */
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.45f));
    if (G.title_page == 1) {
        load_draw();
        return;
    }
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    draw_rect(x0 - 60.0f, 120.0f, BTN_W + 120.0f, btn_y(TM_COUNT) + 40.0f - 120.0f, rgba(0.01f, 0.01f, 0.03f, 0.7f));

    float pulse = 0.5f + 0.5f * sinf(G.time * 2.0f);
    draw_blend_add();
    draw_glow(v2(SCREEN_W * 0.5f, 245.0f), 420.0f, col_hex(0x66f0ff, 0.10f + 0.05f * pulse));
    draw_blend_alpha();
    draw_text_centered(SCREEN_W * 0.5f + 5.0f, 205.0f, 11.0f, col_hex(0xff4fd8, 0.55f), "NEON BREACH"); /* chromatic offset */
    draw_text_centered(SCREEN_W * 0.5f, 200.0f, 11.0f, col_hex(0x66f0ff, 1), "NEON BREACH");
    draw_text_centered(SCREEN_W * 0.5f, 330.0f, 2.5f, col_hex(0xff4fd8, 1), "a roguelite bullet storm");

    for (int i = 0; i < TM_COUNT; i++) {
        float y = btn_y(i);
        bool on = item_enabled(i), sel = on && i == G.title_sel;
        SDL_FColor c = on ? col_hex(0x66f0ff, 1) : col_a(COL_WHITE, 0.28f);
        if (sel) draw_rect(x0, y, BTN_W, BTN_H, col_a(c, 0.85f));
        else draw_rect(x0, y, BTN_W, BTN_H, rgba(0.02f, 0.03f, 0.07f, 0.85f));
        draw_rect_outline(x0, y, BTN_W, BTN_H, 2.0f, col_a(c, sel ? 1.0f : 0.5f));
        draw_text_centered(SCREEN_W * 0.5f, y + BTN_H * 0.5f - 12.0f, 3.0f, sel ? col_hex(0x05060c, 1) : c, "%s", TITLE_ITEMS[i]);
    }

    const char *lines[] = {
        "WASD / ZQSD / ARROWS  move     MOUSE  aim & shoot     SHIFT / RMB  active items     SPACE  bomb",
        "E  take / buy / use     TAB  upgrades on / off     ESC  pause     M  mute     F11  fullscreen",
    };
    for (int i = 0; i < ARRAY_LEN(lines); i++)
        draw_text_centered(SCREEN_W * 0.5f, 900.0f + (float)i * 32.0f, 1.75f, col_a(COL_WHITE, 0.65f), "%s", lines[i]);
    draw_text_centered(SCREEN_W * 0.5f, 1000.0f, 1.5f, col_a(COL_WHITE, 0.35f), "ARROWS / MOUSE  choose     ENTER / CLICK  confirm     ESC  quit");
}
