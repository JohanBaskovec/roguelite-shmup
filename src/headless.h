#ifndef HEADLESS_H
#define HEADLESS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Window-less simulation for automated testing: no video, no audio, the bot
 * plays as fast as the CPU allows. Invariants are checked every tick and any
 * violation makes the process exit with a non-zero status.
 */
typedef struct {
    uint64_t seed;
    int runs;          /* consecutive runs, seeds seed .. seed + runs - 1 */
    int max_ticks;     /* per run (120 ticks = 1 s of game time) */
    int depth;         /* start in a combat room of this depth (0 = normal start) */
    bool stats_room;   /* start in a Stats room instead */
    bool shop_room;    /* start in a Shop instead */
    bool workshop_room; /* start in a Workshop instead */
    bool boss_room;    /* start in a boss room (depth BOSS_EVERY unless depth is set); fail if the boss is not beaten */
    int coins;         /* extra starting coins */
    bool secret;       /* every eligible room hides a secret room; fail if the bot never finds one */
    bool all_upgrades; /* start with one of every upgrade */
    int hp;            /* > 0: starting (max) hp in half-hearts, after upgrades */
    bool debug_rooms;  /* test the debug menu teleports instead of playing normal runs */
    bool god;
    int character;     /* CharId used for every run */
    unsigned actives;  /* active items given at the start (bit per ActiveId; 0 = none, the normal start); fail if never used */
    bool active_room;  /* start in an active room */
    const int *upgrades; /* upgrades given at the start (UpgradeId) */
    int nupgrades;
    unsigned weapons;  /* shot upgrades given at the start (bit per UpgradeId - UPG_W_SCATTER) */
    bool toggle_upgrades; /* the bot keeps turning random upgrades off and on; fail if it never does */
    int expect_rooms;  /* fail if a run clears fewer rooms than this */
    const char *fail_shot; /* if set, render the failing frame offscreen into this BMP */
} HeadlessOptions;

int headless_run(const HeadlessOptions *o);

#endif
