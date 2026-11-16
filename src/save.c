#include "save.h"

#include "bot.h"
#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static char dir[512];
static int writes, profile_writes;
static Game snap; /* the snapshot being written / read (1.4 MB: kept off the stack) */

/* A save file: this header, the save's Profile (unlocks), then - while a run is going on -
 * the snapshot of the whole Game at the start of the room the ship is in. */
typedef struct {
    char magic[4];
    uint32_t version, fingerprint, game_size;
    int32_t has_run, unlocked, unlock_total, runs, bosses, best_depth; /* the save */
    int32_t character, depth, rooms_cleared, kills, hp, max_hp, upgrades; /* its run (has_run) */
    float run_time;
    int64_t saved_at;
} SaveHeader;

/* Changes whenever the layout of the saved state does (sizes, offsets, enum counts). */
static uint32_t fingerprint(void) {
    const uint32_t parts[] = {
        (uint32_t)sizeof(Game), (uint32_t)sizeof(Player), (uint32_t)sizeof(Room), (uint32_t)sizeof(Enemy),
        (uint32_t)sizeof(PBullet), (uint32_t)sizeof(EBullet), (uint32_t)sizeof(Pickup), (uint32_t)sizeof(Machine),
        (uint32_t)sizeof(Spawn), (uint32_t)sizeof(Obstacle), (uint32_t)sizeof(Profile), (uint32_t)offsetof(Game, pl), (uint32_t)offsetof(Game, room),
        (uint32_t)offsetof(Game, pb), (uint32_t)offsetof(Game, save_slot), (uint32_t)offsetof(Player, act),
        UPG_COUNT, ACT_COUNT, EN_COUNT, ST_COUNT, SYN_COUNT, RW_COUNT, CHAR_COUNT, MAL_COUNT, OB_KIND_COUNT,
    };
    uint32_t h = 2166136261u; /* FNV-1a */
    for (size_t i = 0; i < ARRAY_LEN(parts); i++)
        for (int b = 0; b < 4; b++) h = (h ^ ((parts[i] >> (8 * b)) & 0xffu)) * 16777619u;
    return h;
}

void save_set_dir(const char *d) {
    dir[0] = 0;
    if (!d || !*d) return;
    size_t n = strlen(d);
    snprintf(dir, sizeof dir, "%s%s", d, d[n - 1] == '/' || d[n - 1] == '\\' ? "" : "/");
    SDL_CreateDirectory(d);
}

bool save_enabled(void) { return dir[0] != 0; }

int save_writes(void) { return writes; }

static void slot_path(char *out, size_t size, int slot, const char *ext) { snprintf(out, size, "%ssave_%d.%s", dir, slot, ext); }

static bool read_header(int slot, SaveHeader *h) {
    char path[600];
    slot_path(path, sizeof path, slot, "sav");
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(h, sizeof *h, 1, f);
    fclose(f);
    if (n != 1 || memcmp(h->magic, "NBSV", 4) != 0) memset(h, 0, sizeof *h); /* not a save: listed as unreadable */
    return true;
}

static bool header_ok(const SaveHeader *h) {
    if (h->has_run != 0 && h->has_run != 1) return false;
    return !memcmp(h->magic, "NBSV", 4) && h->version == SAVE_VERSION && h->fingerprint == fingerprint() &&
           h->game_size == (uint32_t)sizeof(Game);
}

int save_new_slot(void) {
    if (!save_enabled()) return 0;
    SaveHeader h;
    for (int s = 1; s <= SAVE_SLOTS; s++)
        if (!read_header(s, &h)) return s;
    return 0;
}

static void header_profile(SaveHeader *h, const Profile *p) {
    *h = (SaveHeader){.magic = {'N', 'B', 'S', 'V'}, .version = SAVE_VERSION, .fingerprint = fingerprint(),
                      .game_size = (uint32_t)sizeof(Game), .unlocked = profile_unlocked(p), .unlock_total = unlocks_total(),
                      .runs = p->runs, .bosses = p->bosses, .best_depth = p->best_depth};
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    h->saved_at = now;
}

static void header_run(SaveHeader *h, const Game *g) {
    h->has_run = 1;
    h->character = g->character, h->depth = g->room.depth, h->rooms_cleared = g->rooms_cleared, h->kills = g->kills;
    h->hp = g->pl.hp, h->max_hp = g->pl.max_hp, h->run_time = g->run_time;
    for (int u = 0; u < UPG_COUNT; u++) h->upgrades += g->pl.upg_count[u] > 0;
}

/* Written next to it, then renamed over it: a crash mid-write never leaves a broken save. */
static bool write_file(int slot, const SaveHeader *h, const Profile *p, const void *game) {
    char tmp[600], path[600];
    slot_path(tmp, sizeof tmp, slot, "tmp");
    slot_path(path, sizeof path, slot, "sav");
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(h, sizeof *h, 1, f) == 1 && fwrite(p, sizeof *p, 1, f) == 1 && (!game || fwrite(game, sizeof(Game), 1, f) == 1);
    ok = fclose(f) == 0 && ok;
    if (!ok || !SDL_RenamePath(tmp, path)) {
        SDL_RemovePath(tmp);
        return false;
    }
    return true;
}

bool save_write(int slot) {
    if (!save_enabled() || slot <= 0) return false;
    /* Only the run itself: no menu, input or particles (they would come back frozen). */
    snap = G;
    snap.state = GS_PLAYING;
    snap.demo = false;
    memset(&snap.in, 0, sizeof snap.in);
    snap.in.number = -1;
    snap.npt = snap.npop = 0;
    snap.quit = false;
    snap.save_slot = slot;
    SaveHeader h;
    header_profile(&h, &G.prof);
    header_run(&h, &G);
    if (!write_file(slot, &h, &G.prof, &snap)) return false;
    writes++;
    return true;
}

void save_autosave(void) {
    if (G.demo || G.save_slot <= 0) return;
    if (!save_write(G.save_slot)) SDL_Log("Could not save the game in %s", dir);
}

void save_end_run(void) {
    if (G.demo || G.save_slot <= 0 || !save_enabled()) return;
    SaveHeader h;
    header_profile(&h, &G.prof);
    if (write_file(G.save_slot, &h, &G.prof, NULL)) profile_writes++;
    else SDL_Log("Could not save the game in %s", dir);
}

/* The save's unlocks written, its run part (if any) left as it was: still the start of its room. */
static bool write_profile(int slot, const Profile *p) {
    char path[600];
    slot_path(path, sizeof path, slot, "sav");
    size_t n = 0;
    uint8_t *data = SDL_LoadFile(path, &n);
    SaveHeader old, h;
    bool run = data && n == sizeof old + sizeof(Profile) + sizeof(Game);
    if (run) memcpy(&old, data, sizeof old);
    run = run && header_ok(&old) && old.has_run;
    header_profile(&h, p);
    if (run) {
        int64_t at = h.saved_at;
        h = old;
        h.unlocked = profile_unlocked(p), h.unlock_total = unlocks_total();
        h.runs = p->runs, h.bosses = p->bosses, h.best_depth = p->best_depth;
        h.saved_at = at;
    }
    bool ok = write_file(slot, &h, p, run ? data + sizeof old + sizeof(Profile) : NULL);
    if (ok) profile_writes++;
    SDL_free(data);
    return ok;
}

void save_profile_update(void) {
    if (G.demo || G.save_slot <= 0 || !save_enabled()) return;
    if (!write_profile(G.save_slot, &G.prof)) SDL_Log("Could not save the game in %s", dir);
}

bool save_write_profile(int slot, const Profile *p) { return save_enabled() && slot > 0 && write_profile(slot, p); }

int save_profile_writes(void) { return profile_writes; }

bool save_info(int slot, SaveInfo *i) {
    SaveHeader h;
    if (!save_enabled() || slot <= 0 || !read_header(slot, &h)) return false;
    *i = (SaveInfo){.slot = slot, .ok = header_ok(&h), .has_run = h.has_run == 1, .unlocked = h.unlocked,
                    .unlock_total = h.unlock_total, .runs = h.runs, .bosses = h.bosses, .best_depth = h.best_depth,
                    .character = h.character, .depth = h.depth, .rooms_cleared = h.rooms_cleared, .kills = h.kills,
                    .hp = h.hp, .max_hp = h.max_hp, .upgrades = h.upgrades, .run_time = h.run_time, .saved_at = h.saved_at};
    i->percent = unlock_percent(i->unlocked, i->unlock_total);
    if (!i->ok) i->has_run = false;
    if (i->character < 0 || i->character >= CHAR_COUNT) i->character = 0;
    return true;
}

int save_list(SaveInfo *out, int max) {
    int n = 0;
    for (int s = 1; s <= SAVE_SLOTS && n < max; s++) /* the dev mode's save (SAVE_DEV_SLOT) is never listed */
        n += save_info(s, &out[n]);
    for (int a = 1; a < n; a++) /* most recent first */
        for (int b = a; b > 0 && out[b].saved_at > out[b - 1].saved_at; b--) {
            SaveInfo t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    return n;
}

bool save_load_profile(int slot, Profile *out) {
    if (!save_enabled() || slot <= 0) return false;
    char path[600];
    slot_path(path, sizeof path, slot, "sav");
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    SaveHeader h;
    Profile p;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && header_ok(&h) && fread(&p, sizeof p, 1, f) == 1;
    fclose(f);
    if (ok) *out = p;
    return ok;
}

bool save_load(int slot) {
    if (!save_enabled() || slot <= 0) return false;
    char path[600];
    slot_path(path, sizeof path, slot, "sav");
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    SaveHeader h;
    Profile prof;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && header_ok(&h) && h.has_run && fread(&prof, sizeof prof, 1, f) == 1 &&
              fread(&snap, sizeof snap, 1, f) == 1;
    fclose(f);
    if (!ok) return false;
    /* Settings of this session stay as they are. */
    Input in = G.in;
    bool show_debug = G.show_debug, show_stats = G.show_stats, god = G.god;
    float fps = G.fps;
    G = snap;
    G.in = in;
    G.show_debug = show_debug, G.show_stats = show_stats, G.god = god, G.fps = fps;
    G.state = GS_PLAYING;
    G.demo = false;
    G.quit = false;
    G.save_slot = slot;
    G.prof = prof; /* may be newer than the snapshot's (unlocked during that room) */
    room_nav_reset(); /* caches built for whatever room was loaded before */
    bot_reset();
    return true;
}

void save_path(int slot, char *out, int size) { slot_path(out, (size_t)size, slot, "sav"); }

bool save_copy(int from, int to) {
    char a[600], b[600];
    slot_path(a, sizeof a, from, "sav");
    slot_path(b, sizeof b, to, "sav");
    size_t n = 0;
    void *data = SDL_LoadFile(a, &n);
    if (!data) return false;
    bool ok = SDL_SaveFile(b, data, n);
    SDL_free(data);
    return ok;
}

bool save_delete(int slot) {
    if (!save_enabled() || slot <= 0) return false;
    char path[600];
    slot_path(path, sizeof path, slot, "sav");
    return SDL_RemovePath(path);
}
