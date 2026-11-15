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
- **There are no weapons in the game, only upgrades.** The upgrades that reshape the shots
  (`UPG_W_*`, `upgrade_is_weapon`; "weapon mods" is only an internal code name) all combine
  (`gun_profile` in `weapons.c`) and are offered like any other upgrade (`upgrade_roll`,
  `upgrade_fresh`). No weapon rooms, no weapon switching / levels, and the player never sees the
  word "weapon" (no gun name in the HUD, the TAB menu or the character select).
- **Every upgrade is taken at most once, and an upgrade owned never shows up again** (no stacks):
  `upgrade_capped` is true once owned, so `upgrade_roll` / `upgrade_fresh` never offer
  it; an offer still on the floor (or the slot machine's prize) is swapped for a fresh one as soon as
  the same upgrade is taken elsewhere (`pickups_refresh_owned`). Once every upgrade is owned, rooms
  fall back to stat trades. No upgrade is reserved to a character: all of them, the sword included,
  can be found by everyone (character / upgrade unlocks may come later).
- **Upgrade rooms always offer a choice of 3 items** (take one, the other two vanish).
- **Every upgrade has a bonus AND a malus** (`MALUSES` table in `src/maluses.c`, one `MalusKind` + amount
  per upgrade, or `MAL_CUSTOM` text when the trade-off is built into the effect, e.g. weapon-like ones).
  Stat maluses go through `stat_effect` (`malus_stat_effect`), the others are read with `malus(kind)`
  where they apply (fire gate, `player_hurt`, prices, waves...). Texts are generated
  (`upgrade_malus_desc`, shown in red under the bonus by `draw_upgrade_texts`). A malus only applies
  while its upgrade is owned and turned on; CHAOS lends upgrades without their malus. A new upgrade
  needs a `MALUSES` entry (headless checks every upgrade has one).
- **Every upgrade found can be turned off and back on, one by one** (TAB menu, `src/upgrade_menu.c`,
  `upgrade_toggle`; only purely one-time ones like SKELETON KEY cannot, `upgrade_toggleable`). Turned
  off = no effect at all (`Player.upg_off`, `has()` returns 0, stats left out); turning a heart
  container back on never heals; stat trades stay.
- **Secret rooms**: some rooms (combat / stats / shop) hide one. During the fight one corner flashes
  very briefly (small, faint square); a bomb in that corner opens the bottom door to the secret room.
  The secret room has no enemies (exception above): 1 item (rarely 2) plus coins / bombs / keys
  are waiting there. Variants: 10% secret shop (5 priced items instead of the free item),
  10% ambush (enemies first, loot once cleared). Its exit leads back to the room it was found in - the only time a room is
  re-entered, already cleared (`Room.revisit`; headless tracks rooms by `Room.id`).
- **Workshop** (`ROOM_WORKSHOP`, `src/workshop.c`): starts full of enemies like every room; once cleared
  it spawns its machines (supply press: damage -> coins/bomb/key; slot machine: 5 coins per spin for a
  displayed weapon/upgrade) and augment pads. Exclusive upgrades (`UpgradeDef.tier > 0`) are only ever
  obtained there, through augment pads paid with max HP (tier N costs N hearts); `upgrade_roll` never
  offers them.
- **Enemies never scale with depth**: each type always has the same HP / speed / fire pattern
  (`ENEMIES` table). Difficulty only comes from more enemies, more waves and new, harder types.
- When a room is cleared the ship is teleported back to the entry spot at the bottom (headless checks it).
- **Active items** (`src/actives.c`, `Player.act[]` indexed by `ActiveId`): the ship starts with none
  (not even the dash). They are found in active rooms (combat rooms with `RW_ACTIVE`, a door like any
  other): choice of 3 distinct items. **Every item taken is kept**: one press (SPACE / SHIFT / right
  click) triggers all the ones turned on and ready, each then recharges on its own (`ActiveSlot.cd`).
  An item can be offered and taken again: it levels up (`ActiveSlot.level`, max `MAX_ACTIVE_LEVEL`,
  numbers in `active_duration` / `active_desc` / `active_cooldown`); an item at max level is no longer
  offered. Like upgrades, each one can be turned off / on in the TAB menu (`active_toggle`, listed first):
  off = never triggered, a running effect stops; effects are read through `active_running()` /
  `active_level_on()`, never `act[].t` alone. Per-press hooks (NOVA TRIGGER, `G.active_uses`) run once per press. CHAOS only lends hook-only upgrades the player does not own (`Player.chaos`, read by `has()` /
  `upg_has()`) and takes them all back when it ends. Recharges scale with the RECHARGE stat
  (`Player.dash_cooldown`). Every new screen (`room_enter`, `room_return_from_secret`) recharges them all
  (`actives_recharge_all`; running effects keep going).
- A stat trade never lowers max HP (`stat_can_change(ST_MAX_HP, false)` is false; raising it is fine):
  stats rooms, shops and the slot machine never offer one (headless checks). Only workshop augments cost max HP.
- A shop always sells exactly 5 items (upgrades, stat trades), plus a supply counter (`SHOP_SUPPLIES`,
  `shop_supply_spot`): one heart, one bomb, one key, flat prices; all paid with coins, descriptions shown like on any pedestal.
- After a room is cleared the player chooses between 3 doors; each door previews what the next
  room gives. One door may be locked (costs a key). The 3 doors are always different (no reward or
  special room behind two doors, locked or not).
- **Boss rooms** (`ROOM_BOSS`, every `BOSS_EVERY` = 10 rooms: depth 10, 20...): the room before has a
  single, never locked door, the boss room (`gen_doors`; the only room without 3 doors). Like every room
  it starts with its fight: one wave, the boss (`EN_BOSS`, THE OVERSEER, `update_boss` in `enemies.c`),
  open floor, no secret; once it is dead (its minions die with it) it leaves a choice of 3 upgrades
  (`RW_UPGRADE`). Health bar at the top of the HUD (`draw_boss_bar`). The boss never scales either: later
  bosses only call more minions. Its damage goes through `enemy_damage_cap` (0 while invulnerable after a
  phase change, stops at each phase threshold) - any new direct `e->hp -=` must use it too; execute
  effects (GUILLOTINE) and pulls skip it. A new boss attack must respect TIME STOP (it does as long as it
  lives in `update_boss`).

These rules are enforced by invariants in `src/headless.c`; keep them there when adding rooms.
Besides the two peaceful room types, the only exception is the F3 debug menu (`src/debug_menu.c`), a dev tool that can teleport into a
room "without enemies" (already cleared). When adding a room type, add it to the debug menu and to
the `--debug-rooms` test too.

## Build & test

```sh
cmake --preset release && cmake --build --preset release -j
ctest --preset release            # ~50 s
cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan   # ASan + UBSan
```

- The build must stay free of compiler warnings (`-Wall -Wextra`).
- Run both the release and asan test suites before committing gameplay changes.

## Testing: never open a window

Do not launch the game in a real window to test it. Use:

- `neon_breach --headless [--god] [--seed N] [--runs N] [--depth N] [--stats] [--boss]` — simulation only,
  driven by the autopilot in `src/bot.c`, invariants checked every tick. Add `--fail-shot f.bmp`
  to get an offscreen render of the failing frame. `--toggle-upgrades` makes the bot turn random
  upgrades off / on (the bot also turns a weapon mod off by itself when its shots hit nothing for 10 s).
- `neon_breach --shot out.bmp --frames N [...]` — offscreen render (SDL offscreen driver).
- `neon_breach --dump-sfx DIR` — writes every synthesized sound as WAV.

When adding a room type or mechanic, teach the bot to use it (so the simulations cover it) and add
invariants / a CTest entry in `CMakeLists.txt`.

## Code conventions

- Single global `Game G` (`src/game.h`), fixed 120 Hz update (`DT`), rendering separate from update.
- Content lives in data tables: `UPGRADES` (`upgrades.c`, weapon mods included), `ENEMIES` (`enemies.c`),
  `STATS` (`stats.c`), `CHARACTERS` (`characters.c`), sound definitions (`audio.c`).
- Characters (Normal / Shotgunner / Knight) only change the starting state (`character_apply`) and the
  ship's look (`draw_ship`); `G.character` survives restarts; `CharDef.start_upgrade` is the weapon mod
  owned from the start (the Knight's sword can also be found by the others). When adding a character: table
  entry, ship drawing, bot support if it plays differently, and a `sim_<name>` CTest (`--char NAME`).
- Upgrade effects live in `upgrades.c` as `upg_*` hooks called from bullets / enemies / player /
  room code; add new upgrades there (table entry + hook). **Stats are derived**: the Player stat fields
  (`dmg_mult`, `max_hp`, `extra_proj`...) are always `Player.base` + the stat effects of every upgrade
  turned on (`stat_effect`), rebuilt by `player_recompute()`. Never write them directly: change
  `Player.base` (character, stat trades, augment costs) and call `player_recompute()`; stat upgrades go
  in `stat_effect`, one-time effects of taking an upgrade (coins, heal...) in `upgrade_grant`. Floors /
  caps live in `stats_clamp` (headless checks the stats stay in sync). `--all-upgrades` tests them all at once.
- Hooks can run inside an entity loop (e.g. `upg_on_lethal_hit` runs inside the enemy bullet loop):
  never clear or compact an array from there, mark entities dead (`life = -1`) instead.
- Upgrade hooks read ownership through `has()` (upgrades.c) / `upg_has()` elsewhere, never `upg_count[]`
  directly, so CHAOS's temporary upgrades work. When adding an active item: `ACTIVES` entry, effect in
  `use_active`, icon, option name (`--active NAME`), bot usage, a scripted check in `debug_rooms_run`
  and a `sim_active_<name>` CTest. CHAOS may only lend upgrades listed in `CHAOS_POOL` (hook-only, so
  removal is exact). TIME STOP (`active_time_stop_on()`) freezes enemies and enemy bullets: new enemy
  or enemy-bullet behaviour must respect it.
- **Upgrades apply to every player shot**, not just the main gun's: any new source of player shots
  (turret, extra shots, drones, converted enemy bullets...) calls `pbullet_arm(b, scaled)` after setting
  its base numbers (weapon-mod behaviours, pierce, bounces, homing, size, on-spawn effects, and the damage /
  range / shot speed bonuses unless already derived from a volley). Only upgrade by-products
  (`PB_BYPRODUCT`: fragments, mines, vortex) skip it; headless fails on any other unarmed shot.
- Weapon mods: `UPG_W_*` entry (between `UPG_W_SCATTER` and `UPG_W_SWORD`), its block in `gun_profile`
  (multiply / add to the gun's numbers so it combines with every other mod; the look comes from the
  most striking mod, the order there and in `gun_main_mod`), option name (`--weapon NAME`), behaviour as
  `PB_*` flags (motion in `bullets.c`, effects in `weapon_on_hit` / `weapon_bullet_death`, called for
  every bullet death so the flags also work for upgrades), a `BulletStyle` if it looks different, and a
  `sim_weapon_<name>` CTest. `gun_profile` clamps combos (cadence, shots per volley, reach): keep any
  mix playable. Reuse `explode_at` / `chain_lightning` for area effects.
- Upgrade descriptions with a random effect must show the exact chance: build them in `upgrade_desc_at`
  (respects `proc()`, i.e. LOADED DICE). A `|` splits a description over two lines (`draw_desc`).
- Shields (`Player.shield`, half-hearts, max `MAX_SHIELD`) absorb damage before hearts.
- Game code only queues sounds (`sfx_play*`); never call SDL audio directly.
- In-game text is English and ASCII only (SDL's debug font has no accents). The user writes in French.
