#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "audio.h"
#include "bot.h"
#include "draw.h"
#include "game.h"
#include "headless.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *dump_dir;  /* --dump-sfx DIR: write every sound effect as WAV and exit */
    const char *shot_path; /* --shot FILE: render N frames offscreen (no window), save a BMP, exit */
    bool headless;         /* --headless: simulation only, no window / audio (see headless.h) */
    int ticks, runs, expect_rooms;
    bool stats_room;       /* --stats: start in a Stats room */
    bool shop_room;        /* --shop: start in a Shop */
    int coins;             /* --coins N: extra starting coins */
    bool debug_rooms;      /* --debug-rooms (with --headless): test the debug menu teleports */
    bool debug_menu;       /* --debug-menu: open the F3 debug menu at startup */
    bool secret;           /* --secret: every eligible room hides a secret room */
    bool show_secret;      /* --show-secret: mark secret corners (debug menu: SHOW SECRET CORNER) */
    const char *fail_shot;
    int frames;            /* --frames N (with --bot: quit after N frames) */
    bool frames_given;
    int depth;             /* --depth N: start directly in a combat room of that depth */
    bool bot;              /* --bot: autopilot (also implied by --shot) */
    bool god;              /* --god: player takes no damage (testing) */
    bool novsync;          /* --novsync: uncapped frame rate (benchmarking) */
    bool has_seed;
    uint64_t seed;
} Options;

static void parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof *o);
    o->frames = 300;
    o->ticks = 120 * 600;
    o->runs = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--shot") && more) o->shot_path = argv[++i];
        else if (!strcmp(a, "--frames") && more) { o->frames = atoi(argv[++i]); o->frames_given = true; }
        else if (!strcmp(a, "--depth") && more) o->depth = atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && more) { o->seed = strtoull(argv[++i], NULL, 10); o->has_seed = true; }
        else if (!strcmp(a, "--dump-sfx") && more) o->dump_dir = argv[++i];
        else if (!strcmp(a, "--headless")) o->headless = true;
        else if (!strcmp(a, "--stats")) o->stats_room = true;
        else if (!strcmp(a, "--shop")) o->shop_room = true;
        else if (!strcmp(a, "--debug-rooms")) o->debug_rooms = true;
        else if (!strcmp(a, "--debug-menu")) o->debug_menu = true;
        else if (!strcmp(a, "--secret")) o->secret = true;
        else if (!strcmp(a, "--show-secret")) o->show_secret = true;
        else if (!strcmp(a, "--coins") && more) o->coins = atoi(argv[++i]);
        else if (!strcmp(a, "--fail-shot") && more) o->fail_shot = argv[++i];
        else if (!strcmp(a, "--ticks") && more) o->ticks = atoi(argv[++i]);
        else if (!strcmp(a, "--runs") && more) o->runs = atoi(argv[++i]);
        else if (!strcmp(a, "--expect-rooms") && more) o->expect_rooms = atoi(argv[++i]);
        else if (!strcmp(a, "--bot")) o->bot = true;
        else if (!strcmp(a, "--god")) o->god = true;
        else if (!strcmp(a, "--novsync")) o->novsync = true;
    }
    if (o->shot_path) o->bot = true;
}

static void clear_edges(void) {
    Input *in = &G.in;
    in->dash_pressed = in->bomb_pressed = in->pause_pressed = in->restart_pressed = in->confirm_pressed = false;
    in->debug_pressed = false;
    in->weapon_select = -1;
    in->weapon_cycle = 0;
}

static void sample_held_input(SDL_Renderer *r) {
    const bool *ks = SDL_GetKeyboardState(NULL);
    Input *in = &G.in;
    /* Scancodes are positional: ZQSD on AZERTY maps to WASD automatically. */
    in->up = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP];
    in->down = ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN];
    in->left = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT];
    in->right = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
    float wx, wy, lx, ly;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&wx, &wy);
    SDL_RenderCoordinatesFromWindow(r, wx, wy, &lx, &ly);
    in->mouse = v2(lx, ly);
    in->fire = (buttons & SDL_BUTTON_LMASK) != 0;
}

static void handle_event(const SDL_Event *e, SDL_Window *win, bool bot) {
    Input *in = &G.in;
    switch (e->type) {
    case SDL_EVENT_QUIT:
        G.quit = true;
        break;
    case SDL_EVENT_KEY_DOWN:
        if (e->key.repeat) break;
        switch (e->key.scancode) {
        case SDL_SCANCODE_ESCAPE: in->pause_pressed = true; break;
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: in->dash_pressed = true; break;
        case SDL_SCANCODE_E: in->bomb_pressed = true; break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: in->confirm_pressed = true; break;
        case SDL_SCANCODE_F3: in->debug_pressed = true; break;
        case SDL_SCANCODE_F11:
            SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0);
            break;
        default:
            if (e->key.scancode >= SDL_SCANCODE_1 && e->key.scancode < SDL_SCANCODE_1 + WPN_COUNT)
                in->weapon_select = (int)(e->key.scancode - SDL_SCANCODE_1);
            break;
        }
        /* Menu letters follow the keyboard layout. */
        if (e->key.key == SDLK_R) in->restart_pressed = true;
        if (e->key.key == SDLK_M) audio_toggle_mute();
        if (e->key.key == SDLK_Q && G.state == GS_PAUSED) G.quit = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_LEFT) in->confirm_pressed = true;
        else if (e->button.button == SDL_BUTTON_RIGHT) in->dash_pressed = true;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e->wheel.y > 0) in->weapon_cycle = -1;
        else if (e->wheel.y < 0) in->weapon_cycle = 1;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (G.state == GS_PLAYING && !bot) G.state = GS_PAUSED;
        break;
    default:
        break;
    }
}

static bool save_screenshot(SDL_Renderer *r, const char *path) {
    SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

int main(int argc, char **argv) {
    Options opt;
    parse_args(argc, argv, &opt);

    if (opt.dump_dir) {
        bool ok = audio_dump_wavs(opt.dump_dir);
        SDL_Log(ok ? "Sound effects written to %s" : "Could not write to %s", opt.dump_dir);
        return ok ? 0 : 1;
    }

    if (opt.headless) {
        HeadlessOptions h = {
            .seed = opt.has_seed ? opt.seed : SDL_GetPerformanceCounter(),
            .runs = opt.runs, .max_ticks = opt.ticks, .depth = opt.depth, .stats_room = opt.stats_room,
            .shop_room = opt.shop_room, .coins = opt.coins, .debug_rooms = opt.debug_rooms, .secret = opt.secret,
            .god = opt.god, .expect_rooms = opt.expect_rooms, .fail_shot = opt.fail_shot,
        };
        return headless_run(&h);
    }

    SDL_SetAppMetadata("Neon Breach", "0.1.0", "dev.neonbreach.game");
    if (opt.shot_path) {
        /* Render into an offscreen surface: never opens a window. */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"); /* plain surface, no EGL/GPU driver */
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!SDL_CreateWindowAndRenderer("Neon Breach", 1600, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (!opt.shot_path && !opt.novsync) SDL_SetRenderVSync(ren, 1);
    SDL_HideCursor();
    draw_init(ren);
    if (!opt.shot_path) audio_init();

    uint64_t seed = opt.has_seed ? opt.seed : SDL_GetPerformanceCounter();
    SDL_Log("Neon Breach - seed %llu (renderer: %s)", (unsigned long long)seed, SDL_GetRendererName(ren));
    game_init(seed);
    G.god = opt.god;
    G.force_secret = opt.secret;
    G.reveal_secrets = opt.show_secret;
    if (opt.stats_room || opt.shop_room || opt.depth > 0 || opt.coins > 0 || opt.debug_menu) {
        game_new_run();
        G.pl.coins += opt.coins;
        int depth = opt.depth > 0 ? opt.depth : 1;
        if (opt.stats_room) room_enter(ROOM_STATS, RW_NONE, depth);
        else if (opt.shop_room) room_enter(ROOM_SHOP, RW_NONE, depth);
        else if (opt.depth > 0) room_enter(ROOM_COMBAT, RW_UPGRADE, depth);
    }

    if (opt.debug_menu) debug_menu_open();

    const double freq = (double)SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0.0, fps_t = 0.0;
    int fps_frames = 0, frame = 0;

    while (!G.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle_event(&e, win, opt.bot);

        Uint64 now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - last) / freq;
        last = now;
        if (opt.shot_path) elapsed = 2.0 * DT; /* deterministic: 2 ticks per frame */
        acc += elapsed;
        if (acc > 0.25) acc = 0.25; /* avoid the spiral of death after a stall */

        fps_t += elapsed;
        fps_frames++;
        if (fps_t >= 0.5) {
            G.fps = (float)(fps_frames / fps_t);
            if (opt.bot && !opt.shot_path && (frame % 4) == 0)
                SDL_Log("fps %.0f  room %d  eb %d  pb %d  en %d  pt %d", (double)G.fps, G.room.depth, G.neb, G.npb, G.nen, G.npt);
            fps_t = 0.0;
            fps_frames = 0;
        }

        while (acc >= DT) {
            sample_held_input(ren);
            if (opt.bot) bot_input();
            game_update();
            clear_edges();
            acc -= DT;
        }

        audio_flush();
        game_render();
        frame++;
        if (opt.shot_path && frame >= opt.frames) {
            if (!save_screenshot(ren, opt.shot_path)) SDL_Log("Screenshot failed: %s", SDL_GetError());
            else SDL_Log("Saved %s (room %d type %d, %d enemy bullets, cleared %d, secret corner %d open %d, coins %d, doors %s/%s/%s)",
                         opt.shot_path, G.room.depth, G.room.type, G.neb, G.room.cleared, G.room.secret_corner,
                         G.room.secret_open, G.pl.coins, door_label(&G.room.doors[0]), door_label(&G.room.doors[1]),
                         door_label(&G.room.doors[2]));
            G.quit = true;
        }
        if (opt.bot && !opt.shot_path && opt.frames_given && frame >= opt.frames) G.quit = true;
        SDL_RenderPresent(ren);
    }

    audio_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
