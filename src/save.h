#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>
#include <stdint.h>

#include "game.h"

/*
 * Save files (save_<slot>.sav in the user's data folder): a save holds its unlocks (Profile, kept
 * from run to run) and, while a run is going on, that run. The run is saved each time the ship
 * enters a new room (never during a room): a snapshot of the whole game state (Game has no
 * pointers) taken right after the room is set up, during the fade in. Loading it puts the ship at
 * the start of that room, the enemies about to show up, and the room then plays exactly as it did
 * (same generator state). Something unlocked during a room is written at once, the run part staying
 * the start of the room; the ship destroyed ends the run, the save (and its unlocks) stays.
 *
 * The header carries a fingerprint of the layout of Game (sizes, enum counts): a save written by
 * a build where it differs is listed as an old version and can only be deleted. Bump SAVE_VERSION
 * whenever the meaning of saved data changes without changing its layout (enum reordered...).
 */

#define SAVE_VERSION 4
#define SAVE_SLOTS 99     /* save_1.sav .. save_99.sav */
#define SAVE_DEV_SLOT 100 /* save_100.sav: DEV MODE's save (everything unlocked, every enemy met), never listed */

typedef struct {
    int slot;
    bool ok;      /* written by a compatible build: can be loaded */
    bool has_run; /* a run is going on (character ... run_time) */
    int unlocked, unlock_total, percent, runs, bosses, best_depth;
    int character, depth, rooms_cleared, kills, hp, max_hp, upgrades;
    float run_time;
    int64_t saved_at; /* SDL_Time (ns since the epoch) */
} SaveInfo;

/* Folder of the save files (NULL / "": saving off - headless and screenshots unless --save-dir). */
void save_set_dir(const char *dir);
bool save_enabled(void);
/* A free slot for a new run (0 if saving is off or every slot is taken). */
int save_new_slot(void);
/* Room entry: saves the run (G.save_slot) if saving is on and this is not the menu's demo. */
void save_autosave(void);
bool save_write(int slot);
/* The ship destroyed: the save keeps its unlocks, without a run. */
void save_end_run(void);
/* Something unlocked during a room: written at once, the run part left as it was. */
void save_profile_update(void);
/* Unlocks of a save (save menu, before its run is resumed or a new one started). */
bool save_load_profile(int slot, Profile *out);
/* Every save file, most recent first (DEV MODE's left out). */
int save_list(SaveInfo *out, int max);
/* The header of one save (false: no such file). */
bool save_info(int slot, SaveInfo *out);
/* Writes these unlocks into a save (created without a run if missing; its run, if any, kept). */
bool save_write_profile(int slot, const Profile *p);
/* Replaces the game with the save's run (false, game untouched, if missing / incompatible / no run). */
bool save_load(int slot);
bool save_delete(int slot);
/* Tests: the file of a slot, a copy of a save under another slot. */
void save_path(int slot, char *out, int size);
bool save_copy(int from, int to);
/* Runs saved since start (headless checks one per room entry); profile-only writes apart. */
int save_writes(void);
int save_profile_writes(void);

#endif
