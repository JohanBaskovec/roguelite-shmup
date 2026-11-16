#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "audio.h"
#include "bot.h"
#include "draw.h"
#include "game.h"
#include "headless.h"
#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *dump_dir;  /* --dump-sfx DIR: write every sound effect as WAV and exit */
    const char *shot_path; /* --shot FILE: render N frames offscreen (no window), save a BMP, exit */
    const char *record;    /* --record PREFIX: render offscreen at 1920x1080, write the frames (PREFIX.rgb, raw RGB24),
                              the game's sounds mixed offline (PREFIX.wav) and the ship's position (PREFIX.txt) */
    int record_from;       /* --record-from F: first frame written (sounds too) */
    int record_step;       /* --record-step S: one frame in S (scouting) */
    int shot_draw_every;   /* --shot-draw-every N: --shot draws one frame in N (and the last ones): the
                              simulation runs every frame, drawing in software is what costs */
    bool headless;         /* --headless: simulation only, no window / audio (see headless.h) */
    int ticks, runs, expect_rooms;
    bool stats_room;       /* --stats: start in a Stats room */
    bool shop_room;        /* --shop: start in a Shop */
    int coins;             /* --coins N: extra starting coins */
    int bombs;             /* --bombs N: starting bombs (the run starts with none) */
    bool debug_rooms;      /* --debug-rooms (with --headless): test the debug menu teleports */
    bool debug_menu;       /* --debug-menu: open the F3 debug menu at startup */
    bool teleport_menu;    /* --teleport-menu: the F4 teleport menu */
    bool secret;           /* --secret: every eligible room hides a secret room */
    bool show_stats;       /* --show-stats: start with the F2 stats overlay visible */
    bool workshop_room;    /* --workshop: start in a workshop */
    bool boss_room;        /* --boss: start in a boss room (depth 10 unless --depth) */
    bool cleared;          /* --cleared: the starting room is already cleared (dev, like the F3 menu) */
    bool no_bot;           /* --no-bot: screenshots without the autopilot (the ship stays put) */
    int character;         /* --char NAME: normal / shotgunner / knight */
    unsigned actives;      /* --active NAME (repeatable): start with these active items (bit per ActiveId) */
    int upgrades[24], nupgrades; /* --upgrade NAME (repeatable): start with these upgrades ("shadow-clone", "aegis"...) */
    unsigned weapons;      /* --weapon NAME (repeatable): start with these shot upgrades (bit per UpgradeId - UPG_W_SCATTER) */
    bool toggle_upgrades;  /* --toggle-upgrades (headless): the bot keeps turning upgrades off and on */
    bool active_room;      /* --active-room: start in an active room (choice of 3 active items) */
    bool char_select;      /* --char-select: open the character select screen at startup */
    bool title;            /* --title: stay on the main menu (screenshots: the bot only plays the demo behind it) */
    bool upgrades_menu;    /* --upgrades-menu: open the upgrades menu (TAB) at startup */
    bool pause;            /* --pause: open the pause (the save's menu over the run) at startup */
    bool show_secret;      /* --show-secret: mark hidden secrets (debug menu: SHOW SECRETS) */
    bool all_upgrades;     /* --all-upgrades: start with one of every upgrade */
    int hp;                /* --hp N (headless): starting max hp in half-hearts */
    const char *fail_shot;
    int frames;            /* --frames N (with --bot: quit after N frames) */
    bool frames_given;
    int depth;             /* --depth N: start directly in a combat room of that depth */
    const char *save_dir;  /* --save-dir DIR: where the save files go (the real game: the user's data folder) */
    bool load_menu;        /* --load-menu: open the load screen (fills an empty save folder with sample saves) */
    bool save_menu;        /* --save-menu: like --load-menu, then open the most recent save's menu */
    bool unlocks_menu;     /* --unlocks-menu: open the unlocks screen (a new save's unlocks + one boss) */
    int bestiary;          /* --bestiary NAME: open the bestiary on that enemy (the first rooms' ones met, and it); -1: no */
    bool fresh_profile;    /* --fresh-profile: a new save's unlocks instead of everything unlocked */
    bool save_test;        /* --save-test (headless): saving / loading / deleting checks */
    bool fx;               /* --fx: headless keeps the particles (e.g. for a --fail-shot) */
    bool bot;              /* --bot: autopilot (also implied by --shot) */
    bool god;              /* --god: player takes no damage (testing) */
    bool novsync;          /* --novsync: uncapped frame rate (benchmarking) */
    bool has_seed;
    uint64_t seed;
} Options;

static void parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof *o);
    o->bestiary = -1;
    o->frames = 300;
    o->shot_draw_every = 32;
    o->ticks = 120 * 600;
    o->runs = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--shot") && more) o->shot_path = argv[++i];
        else if (!strcmp(a, "--record") && more) o->record = argv[++i];
        else if (!strcmp(a, "--record-from") && more) o->record_from = atoi(argv[++i]);
        else if (!strcmp(a, "--record-step") && more) o->record_step = atoi(argv[++i]);
        else if (!strcmp(a, "--shot-draw-every") && more) o->shot_draw_every = atoi(argv[++i]);
        else if (!strcmp(a, "--frames") && more) { o->frames = atoi(argv[++i]); o->frames_given = true; }
        else if (!strcmp(a, "--depth") && more) o->depth = atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && more) { o->seed = strtoull(argv[++i], NULL, 10); o->has_seed = true; }
        else if (!strcmp(a, "--dump-sfx") && more) o->dump_dir = argv[++i];
        else if (!strcmp(a, "--headless")) o->headless = true;
        else if (!strcmp(a, "--stats")) o->stats_room = true;
        else if (!strcmp(a, "--shop")) o->shop_room = true;
        else if (!strcmp(a, "--workshop")) o->workshop_room = true;
        else if (!strcmp(a, "--boss")) o->boss_room = true;
        else if (!strcmp(a, "--cleared")) o->cleared = true;
        else if (!strcmp(a, "--no-bot")) o->no_bot = true;
        else if (!strcmp(a, "--debug-rooms")) o->debug_rooms = true;
        else if (!strcmp(a, "--debug-menu")) o->debug_menu = true;
        else if (!strcmp(a, "--teleport-menu")) o->debug_menu = o->teleport_menu = true;
        else if (!strcmp(a, "--secret")) o->secret = true;
        else if (!strcmp(a, "--dense-blocks")) g_dense_blocks = true; /* rooms packed with blocks and gates */
        else if (!strcmp(a, "--show-secret")) o->show_secret = true;
        else if (!strcmp(a, "--show-stats")) o->show_stats = true;
        else if (!strcmp(a, "--char-select")) o->char_select = true;
        else if (!strcmp(a, "--title")) o->title = true;
        else if (!strcmp(a, "--upgrades-menu")) o->upgrades_menu = true;
        else if (!strcmp(a, "--pause")) o->pause = true;
        else if (!strcmp(a, "--char") && more) {
            const char *name = argv[++i];
            o->character = -1;
            for (int c = 0; c < CHAR_COUNT; c++)
                if (!strcmp(name, character_option_name(c))) o->character = c;
            if (o->character < 0) {
                fprintf(stderr, "unknown character '%s' (normal, shotgunner, knight)\n", name);
                exit(2);
            }
        }
        else if (!strcmp(a, "--active") && more) {
            const char *name = argv[++i];
            int found = -1;
            for (int c = ACT_NONE + 1; c < ACT_COUNT; c++)
                if (!strcmp(name, active_option_name(c))) found = c;
            if (found > 0) o->actives |= 1u << found;
            else {
                fprintf(stderr, "unknown active item '%s' (dash, shield, boom, strength, chaos, reversal, turret,\n"
                                "blink, nova, timestop, blades)\n", name);
                exit(2);
            }
        }
        else if (!strcmp(a, "--active-room")) o->active_room = true;
        else if (!strcmp(a, "--weapon") && more) {
            const char *name = argv[++i];
            int w = weapon_mod_by_name(name);
            if (w < 0) {
                fprintf(stderr, "unknown weapon mod '%s' (scatter, needler, seeker, bouncer, railgun, flamer, rockets,\n"
                                "chakram, cluster, arc, vulcan, wave, laser, orbit, prism, graviton, sword)\n", name);
                exit(2);
            }
            o->weapons |= 1u << (w - UPG_W_SCATTER);
        }
        else if (!strcmp(a, "--upgrade") && more) {
            int u = upgrade_by_name(argv[++i]);
            if (u < 0) {
                fprintf(stderr, "unknown upgrade '%s' (its name in the game, '-' for spaces: shadow-clone, aegis...)\n", argv[i]);
                exit(2);
            }
            if (o->nupgrades < ARRAY_LEN(o->upgrades)) o->upgrades[o->nupgrades++] = u;
        }
        else if (!strcmp(a, "--enemy") && more) {
            int t = enemy_by_name(argv[++i]);
            if (t < 0 || type_is_boss(t)) {
                fprintf(stderr, "unknown enemy '%s', one of:", argv[i]);
                for (int k = 0; k < EN_COUNT; k++)
                    if (!type_is_boss(k)) fprintf(stderr, " %s", ENEMIES[k].name);
                fprintf(stderr, "\n");
                exit(2);
            }
            if (!g_only_enemies[t]) g_only_count++;
            g_only_enemies[t] = true;
        }
        else if (!strcmp(a, "--wave-time") && more) g_wave_time_override = (float)atof(argv[++i]);
        else if (!strcmp(a, "--mod") && more) {
            int m = mod_by_name(argv[++i]); /* repeatable: several at once */
            if (m < 0) {
                fprintf(stderr, "unknown room modifier '%s' (compactor, blackout, powder-keg, horde, elite, storm, wager)\n", argv[i]);
                exit(2);
            }
            g_mod_override = (g_mod_override < 0 ? 0 : g_mod_override) | (int)MOD_BIT(m);
        }
        else if (!strcmp(a, "--boss-kind") && more) {
            int t = enemy_by_name(argv[++i]);
            if (!type_is_boss(t)) {
                fprintf(stderr, "unknown boss '%s' (overseer, lighthouse, wyrm, stormcaller, forge, carrier, singularity)\n", argv[i]);
                exit(2);
            }
            g_boss_override = t; /* every boss room: that boss */
        }
        else if (!strcmp(a, "--toggle-upgrades")) o->toggle_upgrades = true;
        else if (!strcmp(a, "--all-upgrades")) o->all_upgrades = true;
        else if (!strcmp(a, "--hp") && more) o->hp = atoi(argv[++i]);
        else if (!strcmp(a, "--coins") && more) o->coins = atoi(argv[++i]);
        else if (!strcmp(a, "--bombs") && more) o->bombs = atoi(argv[++i]);
        else if (!strcmp(a, "--fail-shot") && more) o->fail_shot = argv[++i];
        else if (!strcmp(a, "--ticks") && more) o->ticks = atoi(argv[++i]);
        else if (!strcmp(a, "--runs") && more) o->runs = atoi(argv[++i]);
        else if (!strcmp(a, "--expect-rooms") && more) o->expect_rooms = atoi(argv[++i]);
        else if (!strcmp(a, "--bot")) o->bot = true;
        else if (!strcmp(a, "--fx")) o->fx = true;
        else if (!strcmp(a, "--save-dir") && more) o->save_dir = argv[++i];
        else if (!strcmp(a, "--load-menu")) o->load_menu = true;
        else if (!strcmp(a, "--unlocks-menu")) o->unlocks_menu = true;
        else if (!strcmp(a, "--bestiary") && more) {
            o->bestiary = enemy_by_name(argv[++i]);
            if (o->bestiary < 0) {
                fprintf(stderr, "unknown enemy '%s'\n", argv[i]);
                exit(2);
            }
        }
        else if (!strcmp(a, "--save-menu")) o->save_menu = o->load_menu = true;
        else if (!strcmp(a, "--fresh-profile")) o->fresh_profile = true;
        else if (!strcmp(a, "--save-test")) o->save_test = true;
        else if (!strcmp(a, "--trace-bot") && more) bot_trace_from(atoi(argv[++i])); /* per tick, from tick N of each run */
        else if (!strcmp(a, "--god")) o->god = true;
        else if (!strcmp(a, "--novsync")) o->novsync = true;
    }
    if (o->record_step < 1) o->record_step = 1;
    /* NEON_TICK_SCALE (the asan test preset: 0.5): shorter simulations, the expected rooms scaled with them.
     * Sanitizers find their bugs in the first minutes of play; the release suite runs them in full. */
    const char *scale = getenv("NEON_TICK_SCALE");
    if (scale && o->headless && !o->debug_rooms) {
        double k = atof(scale);
        if (k > 0.0 && k < 1.0) {
            o->ticks = (int)(o->ticks * k);
            o->expect_rooms = (int)(o->expect_rooms * k);
        }
    }
    if ((o->shot_path || o->record) && !o->char_select && !o->title && !o->load_menu && !o->unlocks_menu && o->bestiary < 0 && !o->no_bot) o->bot = true; /* the bot would confirm the select screen at once */
}

static void clear_edges(void) {
    Input *in = &G.in;
    in->active_pressed = in->bomb_pressed = in->interact_pressed = in->pause_pressed = in->restart_pressed = in->confirm_pressed = false;
    in->skip_pressed = in->teleport_pressed = false;
    in->debug_pressed = in->char_pressed = in->menu_pressed = in->click_pressed = in->delete_pressed = false;
    in->number = -1;
    in->wheel = 0;
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
    in->restart_held = ks[SDL_GetScancodeFromKey(SDLK_R, NULL)]; /* the letter R, whatever the layout */
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
        case SDL_SCANCODE_SPACE: in->bomb_pressed = true; break;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: in->active_pressed = true; break;
        case SDL_SCANCODE_E: in->interact_pressed = true; break;
        case SDL_SCANCODE_Q: in->skip_pressed = true; break; /* the A key on AZERTY (test: finish the room) */
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: in->confirm_pressed = true; break;
        case SDL_SCANCODE_TAB: in->menu_pressed = true; break;
        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE: in->delete_pressed = true; break;
        case SDL_SCANCODE_F2: G.show_stats = !G.show_stats; break;
        case SDL_SCANCODE_F3: in->debug_pressed = true; break;
        case SDL_SCANCODE_F4: in->teleport_pressed = true; break;
        case SDL_SCANCODE_F11:
            SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0);
            break;
        default:
            if (e->key.scancode >= SDL_SCANCODE_1 && e->key.scancode <= SDL_SCANCODE_9)
                in->number = (int)(e->key.scancode - SDL_SCANCODE_1);
            break;
        }
        /* Menu letters follow the keyboard layout. */
        if (e->key.key == SDLK_R) in->restart_pressed = true;
        if (e->key.key == SDLK_M) audio_toggle_mute();
        if (e->key.key == SDLK_C) in->char_pressed = true;
        if (e->key.key == SDLK_Q && (G.state == GS_PAUSED || (G.state == GS_GAMEOVER && G.gameover_t > 0.8f))) title_back(); /* the save menu: the run stays saved */
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_LEFT) in->confirm_pressed = in->click_pressed = true;
        else if (e->button.button == SDL_BUTTON_RIGHT) in->active_pressed = true;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e->wheel.y > 0) in->wheel = -1;
        else if (e->wheel.y < 0) in->wheel = 1;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (G.state == GS_PLAYING && !bot) pause_open();
        break;
    default:
        break;
    }
}

/* --record: one raw RGB24 frame appended to f. */
static bool record_frame(SDL_Renderer *r, FILE *f) {
    SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
    if (!s) return false;
    SDL_Surface *rgb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(s);
    if (!rgb) return false;
    for (int y = 0; y < rgb->h; y++) fwrite((const uint8_t *)rgb->pixels + (size_t)y * (size_t)rgb->pitch, 3, (size_t)rgb->w, f);
    SDL_DestroySurface(rgb);
    return true;
}

static void wav_header(FILE *f, uint32_t frames) { /* stereo float32, 48 kHz */
    uint32_t bytes = frames * 8, v;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); h = 3; fwrite(&h, 2, 1, f); h = 2; fwrite(&h, 2, 1, f);
    v = 48000; fwrite(&v, 4, 1, f); v = 48000 * 8; fwrite(&v, 4, 1, f); h = 8; fwrite(&h, 2, 1, f); h = 32; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
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
            .shop_room = opt.shop_room, .coins = opt.coins, .bombs = opt.bombs, .debug_rooms = opt.debug_rooms, .secret = opt.secret,
            .all_upgrades = opt.all_upgrades, .hp = opt.hp, .character = opt.character, .workshop_room = opt.workshop_room, .boss_room = opt.boss_room,
            .actives = opt.actives, .active_room = opt.active_room, .weapons = opt.weapons, .upgrades = opt.upgrades, .nupgrades = opt.nupgrades, .toggle_upgrades = opt.toggle_upgrades, .god = opt.god, .expect_rooms = opt.expect_rooms, .fail_shot = opt.fail_shot,
            .fx = opt.fx, .save_dir = opt.save_dir, .save_test = opt.save_test, .fresh_profile = opt.fresh_profile, .argc = argc, .argv = (const char *const *)argv,
        };
        return headless_run(&h);
    }

    SDL_SetAppMetadata("Neon Breach", "0.1.0", "dev.neonbreach.game");
    const bool offscreen = opt.shot_path || opt.record;
    if (offscreen) {
        /* Render into an offscreen surface: never opens a window. */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"); /* plain surface, no EGL/GPU driver */
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    /* Saves: the user's data folder when playing; screenshots / recordings only with --save-dir. */
    if (opt.save_dir) save_set_dir(opt.save_dir);
    else if (!offscreen) {
        char *pref = SDL_GetPrefPath("neonbreach", "neon-breach");
        if (pref) save_set_dir(pref);
        SDL_free(pref);
    }

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!SDL_CreateWindowAndRenderer("Neon Breach", opt.record ? SCREEN_W : 1600, opt.record ? SCREEN_H : 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (!offscreen && !opt.novsync) SDL_SetRenderVSync(ren, 1);
    SDL_HideCursor();
    draw_init(ren);
    if (opt.record) audio_offline_begin();
    else if (!opt.shot_path) audio_init();
    FILE *rec_rgb = NULL, *rec_wav = NULL, *rec_txt = NULL;
    uint32_t rec_samples = 0;
    if (opt.record) {
        char path[512];
        snprintf(path, sizeof path, "%s.rgb", opt.record);
        rec_rgb = fopen(path, "wb");
        snprintf(path, sizeof path, "%s.wav", opt.record);
        rec_wav = fopen(path, "wb");
        snprintf(path, sizeof path, "%s.txt", opt.record);
        rec_txt = fopen(path, "w");
        if (!rec_rgb || !rec_wav || !rec_txt) {
            SDL_Log("Cannot write %s.*", opt.record);
            return 1;
        }
        wav_header(rec_wav, 0);
    }

    uint64_t seed = opt.has_seed ? opt.seed : SDL_GetPerformanceCounter();
    SDL_Log("Neon Breach - seed %llu (renderer: %s)", (unsigned long long)seed, SDL_GetRendererName(ren));
    game_init(seed);
    if (opt.fresh_profile) profile_fresh(&G.prof);
    G.god = opt.god;
    G.force_secret = opt.secret;
    G.reveal_secrets = opt.show_secret;
    G.show_stats = opt.show_stats;
    G.character = opt.character;
    if (opt.stats_room || opt.shop_room || opt.workshop_room || opt.boss_room || opt.active_room || opt.actives || opt.weapons || opt.nupgrades || opt.cleared || opt.depth > 0 ||
        opt.coins > 0 || opt.bombs > 0 || opt.debug_menu || opt.all_upgrades || opt.upgrades_menu || opt.pause) {
        game_new_run();
        G.pl.coins += opt.coins;
        G.pl.bombs += opt.bombs;
        for (int a = ACT_NONE + 1; a < ACT_COUNT; a++)
            if (opt.actives & (1u << a)) active_give(a);
        for (int u = UPG_W_SCATTER; u <= UPG_W_SWORD; u++)
            if ((opt.weapons & (1u << (u - UPG_W_SCATTER))) && !upgrade_capped(u)) upgrade_apply(u);
        for (int i = 0; i < opt.nupgrades; i++) upgrade_apply(opt.upgrades[i]);
        if (opt.all_upgrades)
            for (int u = 0; u < UPG_COUNT; u++)
                if (!upgrade_capped(u)) upgrade_apply(u);
        int depth = opt.depth > 0 ? opt.depth : 1;
        if (opt.stats_room) room_enter(ROOM_STATS, RW_NONE, depth);
        else if (opt.shop_room) room_enter(ROOM_SHOP, RW_NONE, depth);
        else if (opt.workshop_room) room_enter(ROOM_WORKSHOP, RW_NONE, depth);
        else if (opt.boss_room) room_enter(ROOM_BOSS, RW_UPGRADE, opt.depth > 0 ? opt.depth : BOSS_EVERY);
        else if (opt.active_room) room_enter(ROOM_COMBAT, RW_ACTIVE, depth);
        else if (opt.depth > 0) room_enter(ROOM_COMBAT, RW_UPGRADE, depth);
        if (opt.cleared) room_debug_clear();
    }

    if (opt.load_menu) {
        SaveInfo tmp[1];
        if (save_enabled() && save_list(tmp, 1) == 0) { /* sample saves to show */
            const int depths[] = {14, 6, 23};
            for (int k = 0; k < 3; k++) {
                profile_fresh(&G.prof);
                for (int b = 0; b < k * 2; b++) { /* bosses beaten */
                    int next[UNLOCK_BATCH], n = unlocks_next(next, UNLOCK_BATCH);
                    for (int i = 0; i < n; i++) G.prof.upg[next[i]] = 1;
                    G.prof.chr[CHAR_KNIGHT] = G.prof.chr[CHAR_SHOTGUNNER] = 1;
                    G.prof.bosses++;
                }
                G.prof.runs = 1 + k * 4;
                G.prof.best_depth = depths[k] + k * 5;
                G.character = k % CHAR_COUNT;
                game_new_run();
                for (int n = depths[k] / 3; n > 0; n--) {
                    int u = upgrade_roll();
                    if (u >= 0) upgrade_apply(u);
                }
                room_enter(ROOM_COMBAT, RW_UPGRADE, depths[k]);
                G.rooms_cleared = depths[k] - 1;
                G.run_time = 41.0f * (float)depths[k];
                save_write(G.save_slot);
                if (k == 1) save_end_run(); /* a save between two runs */
                G.save_slot = 0;
            }
        }
        game_init(seed);
        title_open();
        G.title_sel = 1;
        G.in.confirm_pressed = true; /* CONTINUE */
        if (opt.save_menu) {
            SaveInfo list[SAVE_SLOTS];
            if (save_list(list, SAVE_SLOTS) > 0) title_save_menu(list[0].slot);
            G.in.confirm_pressed = false;
        }
    }
    if (opt.unlocks_menu) {
        profile_fresh(&G.prof);
        int next[UNLOCK_BATCH], n = unlocks_next(next, UNLOCK_BATCH); /* + the first boss */
        for (int i = 0; i < n; i++) G.prof.upg[next[i]] = 1;
        title_open();
        title_unlocks_open();
        G.unl_sel = UPG_W_LASER;
    }
    if (opt.bestiary >= 0) {
        profile_fresh(&G.prof);
        for (int t = 0; t < EN_COUNT; t++) G.prof.seen[t] = !type_is_boss(t) && ENEMIES[t].min_depth <= 12;
        G.prof.seen[EN_BOSS] = 1;
        G.prof.seen[opt.bestiary] = 1;
        title_open();
        title_bestiary_open(opt.bestiary);
    }
    if (opt.char_select) char_select_open();
    if (opt.upgrades_menu) upgrade_menu_open();
    if (opt.pause) pause_open();
    if (opt.debug_menu) debug_menu_open(opt.teleport_menu ? 1 : 0);

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
        if (offscreen) elapsed = 2.0 * DT; /* deterministic: 2 ticks per frame */
        acc += elapsed;
        if (acc > 0.25) acc = 0.25; /* avoid the spiral of death after a stall */

        fps_t += elapsed;
        fps_frames++;
        if (fps_t >= 0.5) {
            G.fps = (float)(fps_frames / fps_t);
            if (opt.bot && !offscreen && (frame % 4) == 0)
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

        /* In a run, the cursor stays inside the window (no stray click on the desktop mid-fight);
         * it is let go in the pause menu, the main menu and on the game over screen. */
        bool grab = !offscreen && !opt.bot && (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) &&
                    (G.state == GS_PLAYING || G.state == GS_UPGRADES || G.state == GS_DEBUG);
        if (grab != SDL_GetWindowMouseGrab(win)) SDL_SetWindowMouseGrab(win, grab);

        audio_flush();
        /* --shot: the game state never depends on drawing (visual randomness has its own generator) */
        bool draw = !opt.shot_path || opt.shot_draw_every <= 1 || frame % opt.shot_draw_every == 0 || frame >= opt.frames - 4;
        if (draw) game_render();
        if (opt.record) {
            float mix[800 * 2]; /* one frame of sound: 2 ticks at 48 kHz */
            audio_offline_render(mix, 800);
            if (frame >= opt.record_from) {
                fwrite(mix, sizeof mix, 1, rec_wav);
                rec_samples += 800;
                bool keep = (frame - opt.record_from) % opt.record_step == 0;
                if (keep && !record_frame(ren, rec_rgb)) SDL_Log("Frame capture failed: %s", SDL_GetError());
                {
                    fprintf(rec_txt, "%d ", keep);
                    fprintf(rec_txt, "%d %.1f %.1f %d %d %d %d %.2f %d\n", frame, (double)G.pl.pos.x, (double)G.pl.pos.y, G.nen, G.neb,
                            G.room.depth, G.room.cleared, (double)G.room.clear_t, G.room.id);
                }
            }
            if (frame + 1 >= opt.frames) G.quit = true;
        }
        frame++;
        if (opt.shot_path && frame >= opt.frames) {
            if (!save_screenshot(ren, opt.shot_path)) SDL_Log("Screenshot failed: %s", SDL_GetError());
            else SDL_Log("Saved %s (room %d type %d, %d enemy bullets, cleared %d clear_t %.2f, secret %d open %d ghost %.2f, coins %d, doors %s/%s/%s)",
                         opt.shot_path, G.room.depth, G.room.type, G.neb, G.room.cleared, (double)G.room.clear_t, G.room.has_secret,
                         G.room.secret_open, (double)room_ghost_alpha(), G.pl.coins, door_label(&G.room.doors[0]), door_label(&G.room.doors[1]),
                         door_label(&G.room.doors[2]));
            G.quit = true;
        }
        if (opt.bot && !opt.shot_path && opt.frames_given && frame >= opt.frames) G.quit = true;
        SDL_RenderPresent(ren);
    }

    if (opt.record) {
        fseek(rec_wav, 0, SEEK_SET);
        wav_header(rec_wav, rec_samples);
        fclose(rec_wav);
        fclose(rec_rgb);
        fclose(rec_txt);
        SDL_Log("Recorded %s (%d frames)", opt.record, frame);
    }
    audio_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
