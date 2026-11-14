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
    bool god;
    int expect_rooms;  /* fail if a run clears fewer rooms than this */
    const char *fail_shot; /* if set, render the failing frame offscreen into this BMP */
} HeadlessOptions;

int headless_run(const HeadlessOptions *o);

#endif
