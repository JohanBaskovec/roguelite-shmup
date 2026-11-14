# CLAUDE.md

Neon Breach: ultra-fast top-down roguelite shmup in C17 + SDL3 (CMake, SDL3 fetched if missing).
All graphics and sounds are generated in code. See README.md for controls and architecture.

## Game design rules (must always hold)

- **Every room starts full of enemies — every room type, including stats rooms, shops and any
  future treasure / boss room.** What makes a room special is only what appears once all its
  enemies are dead (reward drop, stat trades, ...). Never create a room that starts cleared or
  spawns its reward before the fight is over.
  **Only two exceptions** (`room_type_peaceful()`): the launch bay (first room, doors open at once)
  and secret rooms (loot already there). They start without enemies; the only twist is the
  secret room's 10% ambush variant (`Room.peaceful` false: one wave, loot after it).
- **Enemies never drop anything.** Resources (coins, keys, bombs, hearts) only come from room
  rewards, after the room is cleared. Luck affects reward quantities, not drops.
- **Weapon and upgrade rooms always offer a choice of 3 items** (take one, the other two vanish).
  A weapon room tops up with upgrades when fewer than 3 weapons can still be gained.
- **Secret rooms**: some rooms (combat / stats / shop) hide one. During the fight one corner flashes
  very briefly (small, faint square); a bomb in that corner opens the bottom door to the secret room.
  The secret room has no enemies (exception above): 1 item (rarely 2) plus coins / bombs / keys
  are waiting there. Variants: 10% secret shop (5 priced items instead of the free item),
  10% ambush (enemies first, loot once cleared). Its exit leads back to the room it was found in - the only time a room is
  re-entered, already cleared (`Room.revisit`; headless tracks rooms by `Room.id`).
- A shop always sells exactly 5 items (weapons, upgrades, stat trades), paid with coins.
- After a room is cleared the player chooses between 3 doors; each door previews what the next
  room gives. One door may be locked (costs a key).

These rules are enforced by invariants in `src/headless.c`; keep them there when adding rooms.
Besides the two peaceful room types, the only exception is the F3 debug menu (`src/debug_menu.c`), a dev tool that can teleport into a
room "without enemies" (already cleared). When adding a room type, add it to the debug menu and to
the `--debug-rooms` test too.

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
- Content lives in data tables: `WEAPONS` (`weapons.c`), `UPGRADES` (`upgrades.c`), `ENEMIES` (`enemies.c`),
  `STATS` (`stats.c`), `CHARACTERS` (`characters.c`), sound definitions (`audio.c`).
- Characters (Normal / Shotgunner / Knight) only change the starting state (`character_apply`) and the
  ship's look (`draw_ship`); `G.character` survives restarts. The sword (`WPN_SWORD`) is Knight-only:
  everything that offers weapons goes through `weapon_roll`, which respects `weapon_available`
  (headless checks no other character ever owns or is offered it). When adding a character: table
  entry, ship drawing, bot support if it plays differently, and a `sim_<name>` CTest (`--char NAME`).
- Upgrade effects live in `upgrades.c` as `upg_*` hooks called from bullets / enemies / player /
  room code; add new upgrades there (table entry + hook), and stat upgrades must clamp to the
  invariant floors. `--all-upgrades` tests them all at once.
- Hooks can run inside an entity loop (e.g. `upg_on_lethal_hit` runs inside the enemy bullet loop):
  never clear or compact an array from there, mark entities dead (`life = -1`) instead.
- Shields (`Player.shield`, half-hearts, max `MAX_SHIELD`) absorb damage before hearts.
- Game code only queues sounds (`sfx_play*`); never call SDL audio directly.
- In-game text is English and ASCII only (SDL's debug font has no accents). The user writes in French.
