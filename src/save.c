#include "save.h"

#include "bot.h"
#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static char dir[512];
static int writes;
static Game snap; /* the snapshot being written / read (1.4 MB: kept off the stack) */

typedef struct {
    char magic[4];
    uint32_t version, fingerprint, game_size;
    int32_t character, depth, rooms_cleared, kills, hp, max_hp, upgrades;
    float run_time;
    int64_t saved_at;
} SaveHeader;

/* Changes whenever the layout of the saved state does (sizes, offsets, enum counts). */
static uint32_t fingerprint(void) {
    const uint32_t parts[] = {
        (uint32_t)sizeof(Game), (uint32_t)sizeof(Player), (uint32_t)sizeof(Room), (uint32_t)sizeof(Enemy),
        (uint32_t)sizeof(PBullet), (uint32_t)sizeof(EBullet), (uint32_t)sizeof(Pickup), (uint32_t)sizeof(Machine),
        (uint32_t)sizeof(Spawn), (uint32_t)sizeof(Obstacle), (uint32_t)offsetof(Game, pl), (uint32_t)offsetof(Game, room),
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
    SaveHeader h = {
        .magic = {'N', 'B', 'S', 'V'}, .version = SAVE_VERSION, .fingerprint = fingerprint(), .game_size = (uint32_t)sizeof(Game),
        .character = G.character, .depth = G.room.depth, .rooms_cleared = G.rooms_cleared, .kills = G.kills,
        .hp = G.pl.hp, .max_hp = G.pl.max_hp, .run_time = G.run_time,
    };
    for (int u = 0; u < UPG_COUNT; u++) h.upgrades += G.pl.upg_count[u] > 0;
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    h.saved_at = now;
    /* Written next to it, then renamed over it: a crash mid-write never leaves a broken save. */
    char tmp[600], path[600];
    slot_path(tmp, sizeof tmp, slot, "tmp");
    slot_path(path, sizeof path, slot, "sav");
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(&snap, sizeof snap, 1, f) == 1;
    ok = fclose(f) == 0 && ok;
    if (!ok || !SDL_RenamePath(tmp, path)) {
        SDL_RemovePath(tmp);
        return false;
    }
    writes++;
    return true;
}

void save_autosave(void) {
    if (G.demo || G.save_slot <= 0) return;
    if (!save_write(G.save_slot)) SDL_Log("Could not save the game in %s", dir);
}

int save_list(SaveInfo *out, int max) {
    int n = 0;
    if (!save_enabled()) return 0;
    SaveHeader h;
    for (int s = 1; s <= SAVE_SLOTS && n < max; s++) {
        if (!read_header(s, &h)) continue;
        SaveInfo *i = &out[n++];
        *i = (SaveInfo){.slot = s, .ok = header_ok(&h), .character = h.character, .depth = h.depth,
                        .rooms_cleared = h.rooms_cleared, .kills = h.kills, .hp = h.hp, .max_hp = h.max_hp,
                        .upgrades = h.upgrades, .run_time = h.run_time, .saved_at = h.saved_at};
        if (i->character < 0 || i->character >= CHAR_COUNT) i->character = 0;
    }
    for (int a = 1; a < n; a++) /* most recent first */
        for (int b = a; b > 0 && out[b].saved_at > out[b - 1].saved_at; b--) {
            SaveInfo t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    return n;
}

bool save_load(int slot) {
    if (!save_enabled() || slot <= 0) return false;
    char path[600];
    slot_path(path, sizeof path, slot, "sav");
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    SaveHeader h;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && header_ok(&h) && fread(&snap, sizeof snap, 1, f) == 1;
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
