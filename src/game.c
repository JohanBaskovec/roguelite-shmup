#include "game.h"

#include "audio.h"
#include "bot.h"
#include "save.h"
#include "draw.h"

#include <string.h>

Game G;

/* Visual-only randomness (screen shake) so render rate never affects gameplay. */
static Rng vis_rng;

static void clear_world(void) {
    G.npb = G.neb = G.nen = G.nsp = G.npk = G.nmc = 0;
    fx_clear();
}

void game_init(uint64_t seed) {
    memset(&G, 0, sizeof G);
    rng_seed(&G.rng, seed);
    rng_seed(&vis_rng, seed ^ 0xA5A5A5A5ull);
    rng_seed(&G.demo_rng, seed ^ 0x5EED5EEDull);
    rng_seed(&G.fx_rng, seed ^ 0xF00DF00Dull);
    G.in.number = -1;
    G.secret_shop_override = G.secret_ambush_override = -1; /* random */
    G.state = GS_TITLE;
    profile_unlock_all(&G.prof); /* no save chosen yet (tests, the demo): everything unlocked */
    player_init();
    G.entry_side = SIDE_BOTTOM;
    room_enter(ROOM_START, RW_NONE, 0);
    bot_reset();
}

void game_new_run(void) {
    clear_world();
    player_init();
    G.rooms_cleared = 0;
    G.kills = 0;
    G.items_bought = 0;
    G.machine_uses = G.augments = G.trades = G.active_uses = G.toggles = 0;
    G.run_time = 0.0f;
    G.shake = G.hitstop = G.hurt_flash = G.bomb_flash = 0.0f;
    G.banner_t = 0.0f;
    G.transitioning = false;
    G.gameover_t = 0.0f;
    G.victory = false;
    G.demo = false; /* the menu's demo run is wiped */
    G.prof.runs++;
    bot_reset();     /* the autopilot too (headless, --shot): a run plays the same alone or in a batch */
    G.entry_side = SIDE_BOTTOM; /* the launch bay: in from the bottom */
    room_enter(ROOM_START, RW_NONE, 0);
    G.state = GS_PLAYING;
    /* A restart (or another ship) replaces the run it abandons, in the same save file; without
     * one (tests with --save-dir) a new file. */
    if (G.save_slot <= 0) G.save_slot = save_new_slot();
    save_autosave();
    sfx_play(SFX_UI_START);
}

void game_over(void) {
    if (!G.demo) { /* the run is over: the save stays, with its unlocks and no run */
        G.prof.deaths++;
        save_end_run();
    }
    G.state = GS_GAMEOVER;
    G.gameover_t = 0.0f;
    G.hitstop = 0.0f;
    sfx_play(SFX_PLAYER_DEATH);
    fx_explosion(G.pl.pos, 60.0f, col_hex(CHARACTERS[G.character].color, 1));
    fx_explosion(G.pl.pos, 30.0f, COL_WHITE);
    fx_shake(1.0f);
}

void game_victory(void) {
    if (!G.demo) { /* like a death for the save: the run ends, the profile stays */
        save_end_run();
    }
    G.victory = true;
    G.state = GS_GAMEOVER;
    G.gameover_t = 0.0f;
    G.hitstop = 0.0f;
    sfx_play(SFX_POWER_UP);
    fx_shake(0.5f);
}

void game_start_transition(int door) {
    G.transitioning = true;
    G.trans_t = 0.0f;
    G.trans_door = door;
    sfx_play(SFX_DOOR_ENTER);
}

static void update_playing(void) {
    if (G.in.pause_pressed) {
        pause_open();
        return;
    }
    if (G.in.menu_pressed && !G.transitioning) {
        upgrade_menu_open();
        return;
    }
    /* R held down: a quick new run with the same ship (in the same save file, like R after a game over). */
    if (G.in.skip_pressed && !G.demo) room_skip(); /* test key: the room finished at once */
    /* After a restart R must be let go first (< 0), or holding it would restart again and again. */
    if (!G.in.restart_held || G.demo) {
        G.restart_hold = 0.0f;
    } else if (G.restart_hold >= 0.0f && (G.restart_hold += DT) >= RESTART_HOLD_T) {
        game_new_run();
        G.restart_hold = -1.0f;
        return;
    }
    game_world_step();
}

void game_world_step(void) {
    if (G.hitstop > 0.0f) {
        G.hitstop -= DT;
        return;
    }
    G.run_time += DT;

    if (G.transitioning) {
        float prev = G.trans_t;
        G.trans_t += DT;
        if (prev < TRANS_TIME * 0.5f && G.trans_t >= TRANS_TIME * 0.5f) {
            if (G.trans_door == DOOR_SECRET) {
                room_enter_secret();
            } else if (G.trans_door == DOOR_RETURN) {
                room_return_from_secret();
            } else {
                Door d = G.room.doors[G.trans_door];
                G.entry_side = side_opposite(d.side); /* out on the right: in on the left */
                G.entry_mods = d.mods;
                G.entry_count = d.count;
                G.entry_alt = d.alt;
                room_enter(d.type, d.reward, G.room.depth + 1);
            }
            save_autosave(); /* a new screen, its enemies about to show up: the only time the game saves */
        }
        if (G.trans_t >= TRANS_TIME) G.transitioning = false;
        fx_update();
        return;
    }

    synergies_update();
    player_update();
    weapons_update();
    upg_update();
    bullets_update();
    enemies_update();
    pickups_update();
    workshop_update();
    room_update();
    fx_update();
}

void game_update(void) {
    G.time += DT;
    G.ticks++;
    fx_timers();

    int page = G.in.debug_pressed ? 0 : G.in.teleport_pressed ? 1 : -1; /* F3 cheats, F4 teleport */
    if (page >= 0 && !title_world_hidden()) { /* not over the bestiary's small world */
        if (G.state == GS_DEBUG && debug_menu_page() == page) debug_menu_close();
        else if (G.state == GS_DEBUG) debug_menu_set_page(page);
        else debug_menu_open(page);
        return;
    }

    switch (G.state) {
    case GS_DEBUG:
        debug_menu_update(); /* the world stays frozen behind the menu */
        break;
    case GS_TITLE:
        title_update(); /* runs the demo behind it (fx included) */
        break;
    case GS_CHAR_SELECT:
        char_select_update();
        if (G.demo) demo_update(); /* opened from the menu: the demo keeps playing behind */
        else fx_update();
        break;
    case GS_UPGRADES:
        upgrade_menu_update(); /* the world stays frozen behind the menu */
        break;
    case GS_PAUSED:
        pause_update(); /* the save's menu; the world stays frozen behind it */
        break;
    case GS_PLAYING:
        update_playing();
        break;
    case GS_GAMEOVER:
        /* The world keeps going behind the game over screen. */
        G.gameover_t += DT;
        bullets_update();
        enemies_update();
        fx_update();
        if (G.gameover_t > 0.8f && (G.in.restart_pressed || G.in.confirm_pressed)) {
            game_new_run();
            G.restart_hold = -1.0f; /* R still down: no second restart until it is let go */
        }
        else if (G.gameover_t > 0.8f && G.in.char_pressed) char_select_open();
        break;
    }
}

/* The world, full screen (shaken). */
static void render_world(void) {
    float tr = G.shake * G.shake * 18.0f;
    draw_set_offset(v2(rng_range(&vis_rng, -tr, tr), rng_range(&vis_rng, -tr, tr)));

    room_draw_floor();
    room_draw_walls();
    workshop_draw();
    pickups_draw();
    interact_draw();
    enemies_draw();
    bullets_draw_player();
    if (G.state != GS_GAMEOVER) {
        drones_draw();
        sword_draw();
        actives_draw();
        upg_draw();
        player_draw();
    }
    bullets_draw_enemy();
    fx_draw();
    mods_draw_over(); /* the blackout's darkness */

    draw_set_offset(v2(0, 0));
    if (!demo_visible()) { /* the demo behind the menu shows the world only */
        fx_draw_popups();
        hud_draw();
    }
}

void game_render(void) {
    SDL_Renderer *r = draw_renderer();
    SDL_SetRenderDrawColorFloat(r, 0.015f, 0.015f, 0.035f, 1.0f);
    SDL_RenderClear(r);
    if (!title_world_hidden()) render_world(); /* the bestiary: G holds its small world, drawn in its screen */
    overlay_draw();
    draw_flush();
}
