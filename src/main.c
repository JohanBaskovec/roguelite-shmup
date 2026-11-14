#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "draw.h"
#include "game.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *shot_path; /* --shot FILE: render N frames headless-friendly, save a BMP, exit */
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
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--shot") && more) o->shot_path = argv[++i];
        else if (!strcmp(a, "--frames") && more) { o->frames = atoi(argv[++i]); o->frames_given = true; }
        else if (!strcmp(a, "--depth") && more) o->depth = atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && more) { o->seed = strtoull(argv[++i], NULL, 10); o->has_seed = true; }
        else if (!strcmp(a, "--bot")) o->bot = true;
        else if (!strcmp(a, "--god")) o->god = true;
        else if (!strcmp(a, "--novsync")) o->novsync = true;
    }
    if (o->shot_path) o->bot = true;
}

static void clear_edges(void) {
    Input *in = &G.in;
    in->dash_pressed = in->bomb_pressed = in->pause_pressed = in->restart_pressed = in->confirm_pressed = false;
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

/* Simple autopilot used for automated screenshots and smoke tests. */
static void bot_input(void) {
    Input *in = &G.in;
    in->up = in->down = in->left = in->right = false;
    if (G.state == GS_TITLE) {
        in->confirm_pressed = true;
        return;
    }
    Enemy *t = enemy_nearest(G.pl.pos, 1e9f);
    in->mouse = t ? t->pos : v2(ARENA_CX, ARENA_Y0 + 120.0f);
    in->fire = true;
    V2 goal;
    if (G.room.cleared) {
        int pick = 0;
        for (int i = 0; i < G.room.ndoors; i++)
            if (!G.room.doors[i].locked || G.pl.keys > 0) { pick = i; break; }
        goal = G.room.clear_t < 1.2f ? v2(ARENA_CX, ARENA_CY) : door_position(G.room.doors[pick].side);
    } else {
        float a = G.time * 1.2f;
        goal = v2(ARENA_CX + cosf(a) * 450.0f, ARENA_CY + sinf(a) * 280.0f);
    }
    V2 d = v2_sub(goal, G.pl.pos);
    in->right = d.x > 12.0f;
    in->left = d.x < -12.0f;
    in->down = d.y > 12.0f;
    in->up = d.y < -12.0f;
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
        case SDL_SCANCODE_F3: G.show_debug = !G.show_debug; break;
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

    SDL_SetAppMetadata("Neon Breach", "0.1.0", "dev.neonbreach.game");
    if (opt.shot_path) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
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

    uint64_t seed = opt.has_seed ? opt.seed : SDL_GetPerformanceCounter();
    SDL_Log("Neon Breach - seed %llu (renderer: %s)", (unsigned long long)seed, SDL_GetRendererName(ren));
    game_init(seed);
    G.god = opt.god;
    if (opt.depth > 0) {
        game_new_run();
        room_enter(ROOM_COMBAT, RW_UPGRADE, opt.depth);
    }

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

        game_render();
        frame++;
        if (opt.shot_path && frame >= opt.frames) {
            if (!save_screenshot(ren, opt.shot_path)) SDL_Log("Screenshot failed: %s", SDL_GetError());
            else SDL_Log("Saved %s (room %d, %d enemy bullets)", opt.shot_path, G.room.depth, G.neb);
            G.quit = true;
        }
        if (opt.bot && !opt.shot_path && opt.frames_given && frame >= opt.frames) G.quit = true;
        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
