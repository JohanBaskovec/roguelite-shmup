# CLAUDE.md

Neon Breach: ultra-fast top-down roguelite shmup in C17 + SDL3 (CMake, SDL3 fetched if missing).
All graphics and sounds are generated in code. See README.md for controls and architecture.

## Game design rules (must always hold)

- **Every room starts full of enemies — every room type, including the launch bay, stats rooms
  and any future shop / treasure / boss room.** What makes a room special is only what appears
  once all its enemies are dead (reward drop, stat trades, ...). Never create a room that starts
  cleared or spawns its reward before the fight is over.
- After a room is cleared the player chooses between 3 doors; each door previews what the next
  room gives. One door may be locked (costs a key).

These rules are enforced by invariants in `src/headless.c`; keep them there when adding rooms.

## Build & test

```sh
cmake --preset release && cmake --build --preset release -j
ctest --preset release            # ~6 s
cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan   # ASan + UBSan
```

- The build must stay free of compiler warnings (`-Wall -Wextra`).
- Run both the release and asan test suites before committing gameplay changes.

## Testing: never open a window

Do not launch the game in a real window to test it. Use:

- `neon_breach --headless [--god] [--seed N] [--runs N] [--depth N] [--stats]` — simulation only,
  driven by the autopilot in `src/bot.c`, invariants checked every tick. Add `--fail-shot f.bmp`
  to get an offscreen render of the failing frame.
- `neon_breach --shot out.bmp --frames N [...]` — offscreen render (SDL offscreen driver).
- `neon_breach --dump-sfx DIR` — writes every synthesized sound as WAV.

When adding a room type or mechanic, teach the bot to use it (so the simulations cover it) and add
invariants / a CTest entry in `CMakeLists.txt`.

## Code conventions

- Single global `Game G` (`src/game.h`), fixed 120 Hz update (`DT`), rendering separate from update.
- Content lives in data tables: `WEAPONS` / `UPGRADES` (`weapons.c`), `ENEMIES` (`enemies.c`),
  `STATS` (`stats.c`), sound definitions (`audio.c`).
- Game code only queues sounds (`sfx_play*`); never call SDL audio directly.
- In-game text is English and ASCII only (SDL's debug font has no accents). The user writes in French.
