#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Save files: the game saves itself each time the ship enters a new room (never during a room),
 * one file per run (save_<slot>.sav in the user's data folder). A save is a snapshot of the whole
 * game state (Game has no pointers) taken right after the room is set up, during the fade in:
 * loading it puts the ship at the start of that room, the enemies about to show up, and the room
 * then plays exactly as it did (same generator state).
 *
 * The header carries a fingerprint of the layout of Game (sizes, enum counts): a save written by
 * a build where it differs is listed as an old version and can only be deleted. Bump SAVE_VERSION
 * whenever the meaning of saved data changes without changing its layout (enum reordered...).
 */

#define SAVE_VERSION 1
#define SAVE_SLOTS 99 /* save_1.sav .. save_99.sav */

typedef struct {
    int slot;
    bool ok; /* written by a compatible build: can be loaded */
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
/* Every save file, most recent first. */
int save_list(SaveInfo *out, int max);
/* Replaces the game with the save (false, game untouched, if missing / incompatible). */
bool save_load(int slot);
bool save_delete(int slot);
/* Tests: the file of a slot, a copy of a save under another slot. */
void save_path(int slot, char *out, int size);
bool save_copy(int from, int to);
/* Saves written since start (headless checks one per room entry). */
int save_writes(void);

#endif
