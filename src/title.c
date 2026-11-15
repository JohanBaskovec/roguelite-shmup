#include "audio.h"
#include "bot.h"
#include "draw.h"
#include "game.h"

#include <string.h>

/*
 * Main menu (GS_TITLE): PLAY / OPTIONS / UNLOCKS / QUIT over a real game played by the bot.
 *
 * The background demo lives in the normal world (G), flagged G.demo, and runs on its own
 * random generator (G.demo_rng, swapped in for each demo tick) with sounds suppressed, so it
 * never changes the real game's randomness. Starting a run (game_new_run) wipes it. The demo
 * keeps playing behind the character select screen opened from the menu.
 */

enum { TM_PLAY, TM_OPTIONS, TM_UNLOCKS, TM_QUIT, TM_COUNT };
static const char *const TITLE_ITEMS[TM_COUNT] = {"PLAY", "OPTIONS", "UNLOCKS", "QUIT"};
static const bool TITLE_ENABLED[TM_COUNT] = {true, false, false, true}; /* options / unlocks: later */

#define BTN_W 420.0f
#define BTN_H 64.0f
#define BTN_GAP 22.0f
#define BTN_Y0 470.0f
#define DEMO_MAX_ROOM_TIME 75.0f /* a demo stuck in a room starts over */
#define DEMO_DEATH_T 2.0f        /* the explosion plays out before the next demo run */

static float btn_y(int i) { return BTN_Y0 + (float)i * (BTN_H + BTN_GAP); }

static int item_at(V2 m) {
    float x0 = (SCREEN_W - BTN_W) * 0.5f;
    for (int i = 0; i < TM_COUNT; i++)
        if (m.x >= x0 && m.x <= x0 + BTN_W && m.y >= btn_y(i) && m.y <= btn_y(i) + BTN_H) return i;
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
    G.title_sel = TM_PLAY;
    G.sel_mouse = G.in.mouse;
    G.title_up = G.in.up; /* a key already held does not count as a press */
    G.title_down = G.in.down;
}

static void title_move(int dir) {
    int s = G.title_sel;
    do s = (s + dir + TM_COUNT) % TM_COUNT;
    while (!TITLE_ENABLED[s]);
    if (s != G.title_sel) sfx_play(SFX_WEAPON_SWITCH);
    G.title_sel = s;
}

static void title_activate(int item) {
    if (!TITLE_ENABLED[item]) {
        sfx_play(SFX_DENIED);
        return;
    }
    switch (item) {
    case TM_PLAY: char_select_open(); break;
    case TM_QUIT: G.quit = true; break;
    default: break;
    }
}

void title_update(void) {
    if (!G.demo) demo_start(); /* back from a real run: a new demo behind the menu */
    const Input *in = &G.in;
    if (in->up && !G.title_up) title_move(-1);
    if (in->down && !G.title_down) title_move(1);
    G.title_up = in->up;
    G.title_down = in->down;
    if (in->wheel) title_move(in->wheel);
    /* The mouse only takes over when it actually moves. */
    int hover = item_at(in->mouse);
    if (v2_dist2(in->mouse, G.sel_mouse) > 1.0f) {
        if (hover >= 0 && TITLE_ENABLED[hover] && hover != G.title_sel) {
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

void title_draw(void) {
    /* Darken the demo enough for the menu to read, lighter in the middle band. */
    draw_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0.01f, 0.01f, 0.03f, 0.45f));
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
        bool on = TITLE_ENABLED[i], sel = on && i == G.title_sel;
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
