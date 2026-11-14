#include "game.h"

#include "audio.h"
#include "draw.h"

#include <string.h>

Game G;

/* Visual-only randomness (screen shake) so render rate never affects gameplay. */
static Rng vis_rng;

static void clear_world(void) {
    G.npb = G.neb = G.nen = G.nsp = G.npk = 0;
    fx_clear();
}

void game_init(uint64_t seed) {
    memset(&G, 0, sizeof G);
    rng_seed(&G.rng, seed);
    rng_seed(&vis_rng, seed ^ 0xA5A5A5A5ull);
    G.in.weapon_select = -1;
    G.secret_shop_override = G.secret_ambush_override = -1; /* random */
    G.state = GS_TITLE;
    player_init();
    room_enter(ROOM_START, RW_NONE, 0);
}

void game_new_run(void) {
    clear_world();
    player_init();
    G.rooms_cleared = 0;
    G.kills = 0;
    G.items_bought = 0;
    G.run_time = 0.0f;
    G.shake = G.hitstop = G.hurt_flash = G.bomb_flash = 0.0f;
    G.banner_t = 0.0f;
    G.transitioning = false;
    G.gameover_t = 0.0f;
    room_enter(ROOM_START, RW_NONE, 0);
    G.state = GS_PLAYING;
    sfx_play(SFX_UI_START);
}

void game_over(void) {
    G.state = GS_GAMEOVER;
    G.gameover_t = 0.0f;
    G.hitstop = 0.0f;
    sfx_play(SFX_PLAYER_DEATH);
    fx_explosion(G.pl.pos, 60.0f, col_hex(CHARACTERS[G.character].color, 1));
    fx_explosion(G.pl.pos, 30.0f, COL_WHITE);
    fx_shake(1.0f);
}

void game_start_transition(int door) {
    G.transitioning = true;
    G.trans_t = 0.0f;
    G.trans_door = door;
    sfx_play(SFX_DOOR_ENTER);
}

static void update_playing(void) {
    if (G.in.pause_pressed) {
        G.state = GS_PAUSED;
        sfx_play(SFX_PAUSE);
        return;
    }
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
                room_enter(d.type, d.reward, G.room.depth + 1);
            }
        }
        if (G.trans_t >= TRANS_TIME) G.transitioning = false;
        fx_update();
        return;
    }

    player_update();
    weapons_update();
    upg_update();
    bullets_update();
    enemies_update();
    pickups_update();
    room_update();
    fx_update();
}

void game_update(void) {
    G.time += DT;
    G.ticks++;
    fx_timers();

    if (G.in.debug_pressed) {
        if (G.state == GS_DEBUG) debug_menu_close();
        else debug_menu_open();
        return;
    }

    switch (G.state) {
    case GS_DEBUG:
        debug_menu_update(); /* the world stays frozen behind the menu */
        break;
    case GS_TITLE:
        if (G.in.pause_pressed) G.quit = true;
        else if (G.in.confirm_pressed) char_select_open();
        fx_update();
        break;
    case GS_CHAR_SELECT:
        char_select_update();
        fx_update();
        break;
    case GS_PAUSED:
        if (G.in.pause_pressed) {
            G.state = GS_PLAYING;
            sfx_play(SFX_PAUSE);
        }
        else if (G.in.restart_pressed) game_new_run();
        else if (G.in.char_pressed) char_select_open();
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
        if (G.gameover_t > 0.8f && (G.in.restart_pressed || G.in.confirm_pressed)) game_new_run();
        else if (G.gameover_t > 0.8f && G.in.char_pressed) char_select_open();
        break;
    }
}

void game_render(void) {
    SDL_Renderer *r = draw_renderer();
    SDL_SetRenderDrawColorFloat(r, 0.015f, 0.015f, 0.035f, 1.0f);
    SDL_RenderClear(r);

    float tr = G.shake * G.shake * 18.0f;
    draw_set_offset(v2(rng_range(&vis_rng, -tr, tr), rng_range(&vis_rng, -tr, tr)));

    room_draw_floor();
    room_draw_walls();
    pickups_draw();
    enemies_draw();
    bullets_draw_player();
    if (G.state != GS_GAMEOVER) {
        drones_draw();
        sword_draw();
        upg_draw();
        player_draw();
    }
    bullets_draw_enemy();
    fx_draw();

    draw_set_offset(v2(0, 0));
    fx_draw_popups();
    hud_draw();
    overlay_draw();
    draw_flush();
}
