# CLAUDE.md

Neon Breach: ultra-fast top-down roguelite shmup in C17 + SDL3 (CMake, SDL3 fetched if missing).
All graphics and sounds are generated in code. See README.md for controls and architecture.

## Game design rules (must always hold)

- **Every room starts full of enemies — every room type, including shops, workshops, casinos, Baphomet rooms
  and any future treasure / boss room.** What makes a room special is only what appears once all its
  enemies are dead (reward drop, stat trades, ...). Never create a room that starts cleared or
  spawns its reward before the fight is over.
  **Only two exceptions** (`room_type_peaceful()`): the launch bay (first room, doors open at once - `clear_t` starts at `DOORS_AT`, no locked door)
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
  fall back to stat trades (every *unlocked* upgrade owned). No upgrade is reserved to a character: all of
  them, the sword included, can be found by everyone once unlocked.
- **Unlocks** (`src/unlocks.c`, `Profile` in `G.prof`, one per save file): a new save has only the first ship
  (NORMAL) and `UNLOCK_START_COUNT` = 24 upgrades (`START_UPGRADES`, simple ones for a first run). **Every boss
  beaten unlocks the next `UNLOCK_BATCH` = 6 upgrades at once** (`unlocks_boss_beaten` from `room_clear`; the first
  boss: `FIRST_BOSS`, then the rest in a fixed mixed order, `unlocks_next`), available for the rest of the run and
  every later one, and the Knight; reaching room `UNLOCK_SHOTGUNNER_DEPTH` (5) unlocks the Shotgunner
  (`unlocks_on_room`). Every roll skips locked upgrades (`upgrade_unlocked` in `upgrade_roll`, `upgrade_fresh`,
  `upgrade_roll_exclusive`, CHAOS); a ship's own starting mod is granted even if locked. Locked ships are greyed
  out and refused by the character select (`character_unlock_hint`). The demo never unlocks anything (`G.demo`
  set before its `room_enter`). Outside a save (`game_init`: headless, `--shot`, the menu before a save is
  chosen) everything is unlocked (`profile_unlock_all`); `--fresh-profile` runs on a new save's unlocks, kept
  from run to run (headless checks: never a locked upgrade offered, +6 exactly when a boss dies and only then,
  saved at once, ships unlocked). A new unlock kind: `Profile` field + its condition + `unlock_percent` total.
- **Upgrade rooms (and bosses) always offer a choice of `UPGRADE_CHOICES` = 2 upgrades** (take one, the other vanishes; a WAGER room twice that, in two rows);
  active rooms offer `ACTIVE_CHOICES` = 3 items.
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
- **Secret rooms**: some rooms (combat / shop) hide one. During the fight a small see-through ghost
  shows up near the secret's door (within `GHOST_DOOR_R` of `room_secret_spot`, headless checks it) every 5-10 s, drifts for 0.5-2 s (`GHOST_MIN_TIME` / `GHOST_MAX_TIME`), fading
  in and out (`room_ghost_alpha`), then vanishes; a bomb next to the entry door
  (`room_secret_spot`) opens it, leading to the secret room.
  The secret room has no enemies (exception above): 1 item (rarely 2) plus coins / bombs / keys
  are waiting there. Variants: 10% secret shop (5 priced items instead of the free item),
  10% ambush (enemies first, loot once cleared). Its exit leads back to the room it was found in - the only time a room is
  re-entered, already cleared (`Room.revisit`; headless tracks rooms by `Room.id`).
- **Workshop, casino, Baphomet** (`ROOM_WORKSHOP` / `ROOM_CASINO` / `ROOM_BAPHOMET`, `src/workshop.c`): start full
  of enemies like every room; once cleared (`room_reveal`) the WORKSHOP spawns a row of `WORKSHOP_TRADES` stat
  trades (one-of-N; there is no stats room any more), the CASINO the supply press (damage -> coins / bomb / key) and
  `CASINO_MACHINES` slot machines (5 coins per spin, one prize each, never the same upgrade on two; machines only ever
  stand in a cleared casino), the
  BAPHOMET room its pacts (augment pads, `PK_AUGMENT`) on a pentagram. Exclusive upgrades (`UpgradeDef.tier > 0`)
  are only ever obtained there, through pacts paid with max HP (tier N costs N hearts); `upgrade_roll` never
  offers them. Headless checks each one has exactly its own machines / offers.
- **Six more machine rooms** (`ROOM_TRADER`, `ROOM_PURIFIER`, `ROOM_VAULT`, `ROOM_FORGE`, `ROOM_KEYS`, `ROOM_INFIRMARY`, `src/special.c`): like
  every room they start full of enemies; `room_reveal` then spawns their machines (`Machine`, `MC_TRADE .. MC_FORGE`, `G.mc`, used with E through
  `special_use`, ticked by `special_tick`, drawn by `special_draw`, all called from `workshop_update` / `workshop_draw`). Their doors are
  always locked and as likely as any other room's (below), whatever the ship has: a room whose machines come out empty (a trader for a ship with
  nothing to pay with) drops a random resource (coins, keys, bombs or hearts). TRADER: up to `TRADER_OFFERS` upgrades for sale, each paid with `TRADE_COST` of the ship's own
  (`upgrade_tradable`: owned for real, not the starting weapon, not in a fusion; `upgrade_remove`). PURIFIER: `Player.malus_off[u]` (read by
  `malus()`, `malus_stat_effect()` and the descriptions; never a `MAL_CUSTOM` malus: `malus_removable`). VAULT: a safe (`MC_SAFE`, `SAFE_HP`,
  `SAFE_TIME`) that stands dead centre from the moment the enemies come (`vault_spawn` in `room_enter`, `blocks.c` bans the middle of the floor for
  `ROOM_VAULT`) and must be broken, while they attack, by the ship's shots (`machine_shot_hit`, from `update_player_bullets`) and the blade
  (`sword_sweep`); never with E or coins (`interact_target` skips it); the faster, the more coins, banked in `Room.vault_coins` and dropped by
  `vault_reveal` once the room is cleared (the reward still comes after the fight); `vault_on_clear` makes a safe still shut vanish. The bot shoots it first. FORGE: `Player.fused_into[b] = a + 1` (b stays owned, its bonus keeps working, its malus is gone, `upgrade_toggle` switches the pair as
  one, `upgrade_menu_list` lists only a). KEYS: `MC_GATE`, one key, the prize then sits on a free pedestal. INFIRMARY: `MC_HEAL` or `MC_CONTAINER`
  (one of the two, `Machine.group`). A new one of these: `RoomType` + `MachineType`, its `KINDS` line in `gen_doors`,
  `room_type_name`, spawn / use / draw in `special.c`, door colour / icon (`room.c`), HUD title and hint (`hud.c`), bot support (`bot.c`: door
  score + the machine loop), a row in the F4 page, its checks in `check_special_room` (headless), `--room NAME` (`room_type_by_name`) and the
  `sim_rooms_special_*` / `render_rooms_special_*` CTests.
- **Enemies never scale with depth**: each type always has the same HP / speed / fire pattern
  (`ENEMIES` table). Difficulty only comes from more enemies, more waves and new, harder types.
  The 60 newest types (`EN_DRIFTER .. EN_SENTINEL`, plus the helpers `EN_EGG` / `EN_MISSILE` / `EN_TURRET` made by
  another enemy, `type_is_new()`) live in `src/enemies2.c`: `enemy2_update` (from `update_enemy`'s default case),
  `enemy2_damage` (shells, guards, spin: damage changed or refused), `enemy2_untouchable`, `enemy2_on_death`,
  `enemy2_draw*`, `enemy2_alpha`, `enemy2_mass`; `enemy_resistant()` tells the bot what not to waste shots on. Their
  beams, tethers, orbs and arcs are `BossHazard`s from `enemy_hazards()` (the bot keeps off them, the live ones hurt
  through `enemies2_hazards_hurt()`). A type whose HP is not its table's (centipede segments, hydra heads, crystal
  growth, illusions) says so in `enemy_base_hp()` (headless checks it). An enemy gone without being killed (egg
  hatching, missile blowing) sets `vanish`, an illusion `fake`: neither counts as a kill nor leaves a grave
  (`Room.graves`, NECRO). `--enemy NAME` fills `g_only_enemies[]` (`g_only_count`).
  A new enemy: enum + `ENEMIES` entry (cost, min_depth), an `update_<name>` function, its drawing, its bestiary line,
  bot support if it needs a special answer (`bot.c`: hazards, charge lines, MIRROR flanking), a
  `sim_enemy_<name>` CTest (`--enemy NAME`, repeatable: waves of only those types) and, for a new
  mechanic, a scripted check in `debug_rooms_run`. Untouchable states go through `enemy_untouchable()`
  (shots fly through, `enemy_damage` ignores them), shields through `enemy_blocks_shot()`; new enemy
  bullet behaviours are an `EBulletKind` handled in `update_enemy_bullets` (a grenade in the air,
  `ebullet_airborne()`, never hurts). A kamikaze blowing itself up is not a kill (`upg_on_kill` skipped).
- **The ship enters a room by the door opposite the one it left by** (`Room.entry`, set from `G.entry_side`
  in the transition; out on the right = in on the left; the launch bay and debug teleports: bottom). The entry
  door stays closed - it only opens as the secret passage - and the other 3 sides are the exits (`gen_doors`;
  the boss door is the one straight ahead). When a room is cleared the ship stays where it is (headless
  checks the entry side).
- **Nothing on a pedestal is taken by walking over it**: items (upgrades, stat trades, actives, pacts),
  shop articles and casino machines are taken / bought / used with E (`Input.interact_pressed`), on the
  nearest one in reach (`interact_target`, framed with an "E" key cap by `interact_draw`). Only resources
  (coins, keys, bombs, reward hearts) are picked up / vacuumed automatically. Headless fails if an item
  disappears on a tick without E; the bot presses E on the item it wants. A new kind of offer goes through
  `interact_target` too.
- The run starts with 0 keys and 0 bombs (resources only come from rewards).
- **The doors open `DOORS_OPEN_T` (1.8 s) after the last kill** (`DOORS_AT` in `clear_t` terms), after the
  reward has appeared (`ROOM_REVEAL_T`), so the player never rushes out without it.
- **Active items** (`src/actives.c`, `Player.act[]` indexed by `ActiveId`): the ship starts with none
  (not even the dash). They are found in active rooms (combat rooms with `RW_ACTIVE`, a door like any
  other): choice of 3 distinct items. **Every item taken is kept**: one press (SHIFT / right
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
  workshops, shops and slot machines never offer one (headless checks). Only Baphomet pacts cost max HP.
- A shop always sells exactly 5 items (upgrades, stat trades), plus a supply counter (`SHOP_SUPPLIES`,
  `shop_supply_spot`): one heart, one bomb, one key, flat prices; all paid with coins, descriptions shown like on any pedestal.
- **Room layouts** (`src/blocks.c`, `room_gen_layout`; every room with enemies except the boss room, which has
  its boss's own fixed layout, `room_boss_layout`):
  indestructible walls (`OB_WALL`) plus breakable blocks: crate, barrel (explodes, `BARREL_BLAST_R`), oil drum
  (leaves a `FirePool` that sets the ship and enemies on fire; a dash jumps over), spark pod (ring of `EB_BALL`
  that hurt enemies too), nest (`OB_NEST`: hatches one small enemy, `enemy_hatchling`, through `enemy_queue_spawn`;
  nothing hatches on the shatter). **Fire spreads**: every breakable block (`obstacle_flammable`) catches fire
  (`room_block_ignite`, `Obstacle.burn_t`) from a fire pool, a flame shot (`PB_FLAME`, HELLFIRE) or a burning enemy
  touching them; a burning block lights every breakable block it touches after `BLOCK_SPREAD_T` and breaks (goes
  off: blast, oil, sparks, hatchling) at `BLOCK_BURN_TIME` (`blocks_burn`). Blocks only break under player shots / the blade / explosions, never enemy bullets,
  and a bomb breaks them all (`room_blocks_break_all`). A broken block goes off in `blocks_update`, outside
  the entity loops (chains on the next tick); call `room_nav_reset()` whenever obstacles change (the path finder and the collision cell map, `occ` in `room.c`, are rebuilt from it; headless checks the map). Connectivity
  counts walls only (`room_walls_connected`: every free cell and door reachable from the entry spot), so
  blocks may close a passage but can always be shot through; only the entry spot is kept clear.
  **Everything sits on the floor grid**: `GRID` = 60 px cells, arena 1800x900 (`GW` x `GH` = 30 x 15); every
  wall and block fills whole cells (blocks are 1 cell, `BLOCK_TILE`; walls 1 or 2 cells thick). Layouts are
  built on `cells[GH][GW]` and every piece goes through `place()`: never a 1-cell passage (`pinched`, the edge
  counts, diagonal slits too), never two solid cells touching only by a corner (`corner_only`), never a 3x3
  slab of wall (`slab`); blocks stay `BLOCK_EDGE` cells off the edges. Every wall goes through `add_wall`,
  taken back unless `cells_connected` still holds (a 2x2-cell "ship" flood fill from the entry must cover
  every free cell and each door mouth; `room_walls_connected` runs the same test on the final obstacles).
  Spurs jut out of the edges (never in front of a door), arms join walls, `gen_corridors` is a braided
  maze; `grid_to_obstacles` merges wall cells into big rects, one obstacle per block. Anything touching the
  arena edge must keep the crawler (stops at spurs, `crawler_blocked`) and `room_collide_obstacle` (never
  pushes out through an edge-flush side) working. The bot digs through blocks, never fires at a barrel /
  oil drum within its reach (it first goes to a safe spot it can shoot it from, `hazard_spot`, and sticks
  to it), keeps away from burning barrels / drums, closes in on targets beyond its reach (`gun_reach`), only fires at what it can hit (shots into a wall are only recoil), goes for a spot it can shoot a
  hidden enemy from (`vantage_point`), sticks to a waypoint while two ways round are as short (narrow
  corridors), slips sideways when it does not move, aims for free floor near targets on the edge
  (`free_spot_near`) and roams when it hits nothing.
- **A cleared room shatters**: when the last enemy dies every obstacle (walls too) cracks and breaks like
  glass in a wave from `Room.last_death` (`room_shatter_start`, no effects, fire put out), and only after
  `ROOM_REVEAL_T` (`Room.clear_t` goes from -1.2 to 0) does `room_reveal()` spawn the reward / shop /
  machines and the doors open. A cleared room is therefore always empty: rewards and pedestals never need
  to dodge the layout (headless checks no obstacle / fire once revealed and no reward before).
- **Every wave has a timer** (`Room.wave_t` / `wave_time`, set by `spawn_wave`: `WAVE_TIME_BASE` + `WAVE_TIME_PER_ENEMY`
  per enemy, `WAVE_TIME_MAX`; the bar at the bottom of the screen, `draw_wave_bar`). When it runs out the next wave
  comes in whatever is left alive (killing 3/4 of a wave still brings it sooner). **When the last wave's runs out
  (`WAVE_TIME_LAST` longer) the walls close in on the ship from every side** (`src/crush.c`, `Room.crushing`): one
  floor cell per `CRUSH_STEP_T`, each side until a `CRUSH_MIN_W` x `CRUSH_MIN_H` box around where the ship was.
  `Room.crush_in[]` moves the room's **live bounds** (`room_x0()` .. `room_y1()`: what `room_collide_obstacle`, the
  bullets, the crawlers' perimeter and the bot use - code that keeps something inside the room uses them, not
  `ARENA_*`); `crush_update` keeps ship, enemies and spawns inside, a closing wall hurts the ship on touch, and
  every obstacle not strictly inside the box (all of them at the last step) bursts whole (`room_crush_grind`), so
  what is left never cuts the box in two. TIME STOP holds the timer and the walls. Cleared: `crush_end` (the room
  is whole again before the reward). **Never in a boss room** (nor before the last wave, but in a COMPACTOR room), nor without `wave_time` (bestiary, scripted fights).
  The bot keeps off the closing walls (`crush_hazards`). `--wave-time S` forces every timer (tests:
  `sim_rooms_closing_walls*`); headless checks all of it (inside the box, last wave only, no boss room).
- **Room modifiers** (`src/mods.c`, `RoomMod`; a room holds a set, `MOD_BIT` mask: `Door.mods` -> `G.entry_mods` ->
  `Room.mods`, read with `room_has_mod()`): a room with one more rule, rolled with its door (`mod_roll`: every modifier a room can
  have has the same chance, `MOD_EACH_CHANCE`, whatever the depth, one at most per rolled door) and written under the door's reward; the only exception to "what makes
  a room special comes after the fight". Any room with a fight but a boss room (`mod_allowed` / `mods_allowed`: combat,
  shop, workshop, casino, Baphomet), WAGER on combat rooms only, the four difficulty levels (HARD, VERY HARD, EXTREME, IMPOSSIBLE) on combat rooms
  whose reward is coins, bombs, keys or hearts only (`mod_allowed(type, reward, mod)`); never two wager doors in a room (a ship on its last heart must
  always have a way out). Any mix must work together (the F4 menu and `--mod`, repeatable, stack them). COMPACTOR: `crush_start(true)` at the first wave
  (`crush_slow`, towards the centre), full speed when the last wave's timer runs out. BLACKOUT: drawing only
  (`mods_draw_over`). POWDER: `theme_kind` / the block budget. HORDE: one wave (`HORDE_BUDGET`), its timer is the
  last wave's. DIFFICULTY LEVELS (`MOD_HARD .. MOD_IMPOSSIBLE`, `DIFF` table in `mods.c`, `room_difficulty()` = the highest one set): `spawn_wave`
  multiplies its budget (`room_difficulty_enemies`: x1.3 / 1.6 / 2 / 2.5, never stronger enemies), `mods_on_room_enter` adds waves (+1 .. +4, none to a HORDE,
  which only grows), `reward_drop` passes coins / bombs / keys through `difficulty_reward_count` (x1.5 / 2 / 2.5 / 3, one more at least; hearts included). ELITE: `Room.elite` filters `spawn_wave`. STORM: strikes (`Room.strike_*`, `mods_update`, hazards for
  the bot, hold under TIME STOP). WAGER: `mod_door_pay` takes `WAGER_COST` hp (refused on the last heart), the reward
  drops twice (`room_reveal`; one-of-N offers have a group, `Pickup.choice` 1 / 2, one taken from each). A new one:
  enum + `MODS` entry, its effect, bot support if needed, invariants, a line in the scripted check and
  `sim_rooms_mod_<name>` CTests (`--mod NAME` forces it on every room that can have it; `sim_rooms_mod_combo*` mix them),
  a check box on the F4 page comes with the enum.
- After a room is cleared the player chooses between 3 doors; each door previews what the next
  room gives: **the exact count for coin / key / bomb / heart rooms** (`Door.count`, rolled by `doors_roll_counts` when the room is cleared, so luck and upgrades are current; carried by `G.entry_count` into `Room.reward_count`, which `reward_drop_n` drops as is: difficulty levels and a wager included, a wager shown doubled), never the upgrades / actives behind an upgrade or active door. A machine room the ship cannot use (`special_room_empty`: purifier with no upgrade to purify, trader with fewer than `TRADE_COST` tradable, forge with no pair) shows its icon struck out in red and what it will drop instead: a random resource, coins / keys / bombs / hearts, with a count rolled as for a room of that kind (`Door.alt` + `Door.count` -> `G.entry_alt` -> `Room.alt_reward`; the room drops exactly that). The 3 doors are always different (no reward or special room behind two doors).
  **Every kind of room has the same chance, whatever the depth, the ship's health or its resources**: `gen_doors` draws the 3 doors out of
  `KINDS`, 16 kinds (the 6 combat rewards: coins, key, bombs, hearts, upgrade, active; the 10 special rooms: shop, workshop, casino, Baphomet and the
  six machine rooms of `special.c`), one chance in 16 each. The only rules: none from the launch bay but the 4 free combat rewards, never the special
  room the ship is in, one door at least that costs no key (else one door, at random, becomes a free kind). Headless checks the kinds and the
  modifiers are as likely at depth 1 as at depth 33, on a full ship as on its last half heart.
- **Locked doors** (`door_needs_key`): the doors to a **shop, a casino, one of the six machine rooms, an upgrade room or an active room are always
  locked** (a key each), **no other door ever is**. One door at least asks for neither a key nor a heart (a WAGER comes
  off the only free door), so a ship without keys always goes on; the launch bay has no locked door at all (no
  upgrade / active room behind its doors: the run starts without a key). Headless checks all of it.
- **Save files** (`src/save.c`): the game saves itself **only on entering a new screen** (`save_autosave()` in the
  room transition of `game_world_step`, plus the launch bay in `game_new_run`), never during a room. One file per
  save (`save_<slot>.sav`, `G.save_slot`) in the user's data folder (`SDL_GetPrefPath`; headless / `--shot` only with
  `--save-dir`): header (unlock %, run summary), the save's `Profile`, then - while a run goes on - its run: a
  snapshot of the whole `Game` taken right after the room is set up. Loading it puts the ship at the start of that
  room, enemies about to show up, and the room then plays exactly as it did (the bot's memory is per room for
  that). Something unlocked during a room is written at once (`save_profile_update`: the profile changes, the run
  part stays the start of the room; not counted as a save). **The ship destroyed ends the run, never the save**
  (`game_over` -> `save_end_run`: profile only); R / another ship (C) start a new run in the same file; Q (pause /
  game over) goes back to that save's menu (`title_back`). **ESC in a run** opens that same save menu over the frozen
  run (`pause_open` / `pause_update` / `pause_draw`, `GS_PAUSED`): RESUME RUN / ESC go on with the run in memory (never
  reloaded), NEW RUN's character select comes back to it on ESC (`char_select_open_paused`), UNLOCKS / BESTIARY as in
  the menu (the bestiary sets the run aside like the demo), MAIN MENU (`title_open`) / QUIT (closes the game) as
  in the menu (the save menu has the same buttons in both places), TAB the upgrades menu. Main menu: NEW SAVE (new file, fresh profile, the
  first ship's run at once) / DEV MODE (its own save, `SAVE_DEV_SLOT`, never listed: everything unlocked, every
  enemy met, opened on its save menu) / CONTINUE (saves list with unlock %: ENTER / click opens a save, DEL / the X twice
  deletes) -> save menu: RESUME RUN (only while a run goes on), NEW RUN (character select, the run goes in that
  save, `G.menu_slot`), UNLOCKS (every upgrade, locked ones greyed, the selected one in full: icon, kind,
  bonus, malus, toggle, synergies), BESTIARY (`src/bestiary.c`: every enemy / boss, greyed until met in that save -
  `Profile.seen`, set by `bestiary_saw` in `enemy_create`, written at once, never by the demo; the selected one plays in
  a small screen with its description), MAIN MENU, QUIT. Headless checks one save per room entered and
  none during a room (`--save-dir`), the save still there after a death (no run, same unlocks), and
  `--save-test` the load / replay / unlock / death / delete / menu flow.
  **`Game` must stay free of pointers** (it is written as is), and every piece of run state must live in `G`
  (state kept elsewhere is lost on load). A save whose layout fingerprint differs is listed as an old version (can
  only be deleted); bump `SAVE_VERSION` when saved data changes meaning without changing its layout.
- **Boss rooms** (`ROOM_BOSS`, every `BOSS_EVERY` = 10 rooms: depth 10, 20...): the room before has a
  single, never locked door, the boss room (`gen_doors`; the only room without 3 doors). Like every room
  it starts with its fight: one wave, the boss (`Room.boss`, chosen by `boss_for_depth`: room 10 one of
  four of the same strength - THE OVERSEER `EN_BOSS`, THE LIGHTHOUSE, THE WYRM, THE STORMCALLER -, rooms
  20 / 30 / 40 THE FORGE / THE CARRIER / THE SINGULARITY, each harder than the last, beyond any of the
  seven; `--boss-kind NAME` / the F3 menu force one, `g_boss_override`). All of them live in
  `src/bosses.c` (`boss_update` dispatch, `boss_draw*`, `type_is_boss` / `enemy_is_boss` for
  `EN_BOSS .. EN_SINGULARITY`); state kept outside the `Enemy` lives in `Room` (wyrm segments, storm arcs,
  black hole wells / gravity), reset by `boss_on_death`. No secret; a boss either fights on open floor or
  in its own room (`room_boss_layout`: walls only, symmetric both ways, always the same for the same boss -
  headless checks both). Once it is dead (its minions die with it) it leaves a choice of 2 upgrades
  (`RW_UPGRADE`). Health bar at the top of the HUD (`draw_boss_bar`, named by `boss_title`). Three phases:
  a phase change clears the bullets, makes it invulnerable `BOSS_INVULN_T`; the last one is faster. A boss
  never scales either: later bosses only call more minions. Its damage goes through `enemy_damage_cap` (0
  while invulnerable after a phase change, stops at each phase threshold) - any new direct `e->hp -=` must
  use it too; execute effects (GUILLOTINE) and pulls skip it. Anything that hurts besides bullets and the
  body (arcs, the wyrm's body) is a `BossHazard` from `boss_hazards` (the boss hurts the ship with the live
  ones, the bot keeps off all of them, `boss_draw_under` shows them); shots stopped by armour go through
  `boss_armor_hit` (bullets.c), the bot's line of fire through `boss_armor_line`. Only THE FORGE leaves fire
  in its room (headless checks). A new boss: enum between `EN_BOSS` and `EN_SINGULARITY` (+ `BOSS_LAST`),
  `ENEMIES` entry (cost / min_depth 999), its update / draw / title / spawn point, its layout if any, bot
  support, a `boss_mechanics_run` check and `sim_boss_<name>` / `render_boss_<name>` CTests. A new boss
  attack must respect TIME STOP (it does as long as it lives in `boss_update`).

These rules are enforced by invariants in `src/headless.c`; keep them there when adding rooms.
Besides the two peaceful room types, the only exception is the debug menu (`src/debug_menu.c`: F3 cheats, F4 teleport), a dev tool that can teleport into a
room "without enemies" (already cleared). When adding a room type (`RoomType`, its `SPECIALS` line, `room_reveal`, its door look and label, `door_needs_key`)
or a room modifier, add it to the F4 teleport page (modifiers: a check box each) and
to the `--debug-rooms` test too.

## Build & test

```sh
cmake --preset release && cmake --build --preset release -j
ctest --preset quick              # ~2 s: label "quick", one test per area, simulations at 40%
ctest --preset release            # ~12 s
ctest --preset release -L enemies # one area: enemies, weapons, actives, synergies, upgrades, boss, characters, rooms, runs, render
cmake --preset asan && cmake --build --preset asan -j && ctest --preset asan   # ASan + UBSan, ~30 s
```

- The build must stay free of compiler warnings (`-Wall -Wextra`).
- Run both the release and asan test suites before committing gameplay changes. While iterating, run the quick
  preset, the release suite or the tests concerned; the asan suite runs once, before the commit, in the background.
  The asan build is `-O1` (Debug keeps asserts and debug info).
- Don't pass `-j` to ctest: the presets already use the 8 cores.
- The asan preset sets `NEON_TICK_SCALE=0.5` (simulations and their expected rooms halved; `sim_mortal_runs*`
  and `sim_deep_rooms` keep their full length, set in `CMakeLists.txt`). `--shot` draws one frame in 32 plus the
  last ones (`--shot-draw-every N`; drawing never changes the game state). Long batches of runs are split into
  several tests with consecutive seeds so they run side by side. Tests get their area labels from their names
  (`CMakeLists.txt`): name a new test like its neighbours (`sim_enemy_*`, `sim_weapon_*`, `render_*`...).
- The path finder is what the simulations spend their time in (`room_navigate_ex`, `line_blocked` in `room.c`):
  a line test that only needs yes / no uses `line_blocked` (stops at the first hit), never `nearest_blocker`.
  In hot loops use `minf` / `maxf` (`common.h`), not `fminf` / `fmaxf` (libm calls). Measure before keeping an
  optimisation (compare the run lines of the old and new binaries: same results, real gain). `malus()` reads an
  index of the upgrades per malus kind (not a scan of `MALUSES`); the path finder's Dijkstra runs on a heap keyed on
  (distance, node) - the tie order of the old O(n^2) scan, so the paths stay the same to the bit - and only tests the
  straight line from the start to a node when that node comes out of the heap.
- **Particles never touch the game's generator**: `fx.c` draws its randomness from `G.fx_rng`, so a run plays the
  same with or without them. Headless runs without particles (`g_fx_off`; `--fx` keeps them, e.g. for a
  `--fail-shot`); `--debug-rooms` checks that a fight ends in the same state with and without.

## Testing: never open a window

Do not launch the game in a real window to test it. Use:

- `neon_breach --headless [--god] [--seed N] [--runs N] [--depth N] [--shop | --workshop | --casino | --baphomet] [--boss [--boss-kind NAME]] [--bombs N]` — simulation only,
  driven by the autopilot in `src/bot.c`, invariants checked every tick. Add `--fail-shot f.bmp`
  to get an offscreen render of the failing frame. `--toggle-upgrades` makes the bot turn random
  upgrades off / on (the bot also turns a weapon mod off by itself when its shots hit nothing for 10 s).
  Every run is reproducible on its own (except with `--fresh-profile`, where a run inherits the unlocks of the
  runs before it): the bot keeps its state in `B` (`bot.c`), wiped by `bot_reset()` at each
  `game_init` / `game_new_run` - any new bot memory goes there, never in a `static` local. A failed run prints
  the command replaying it alone (`replay it alone: ... --seed S --trace-bot T`). `--trace-bot TICK` prints one
  line per tick from TICK on (position, goal, path waypoint, digging, keys, fire / aim, target, threats...).
- Mass runs: `--runs N --jobs 0 --no-checks` (`--jobs N`: the runs shared out between N forked workers, 0 = one per core,
  same results as one process, lines out of order; `--no-checks`: no per-tick invariants, same games). Both must keep
  the games exactly the same: compare the run lines with and without.
- `neon_breach --shot out.bmp --frames N [...]` — offscreen render (SDL offscreen driver; simulates every
  frame, draws one in 32).
- `neon_breach --dump-sfx DIR` — writes every synthesized sound as WAV.
- `neon_breach --record PREFIX --frames N [--record-from F] [--record-step S] [...]` — offscreen 1920x1080 at
  60 fps, the bot playing: raw RGB24 frames (`PREFIX.rgb`), the game's sounds mixed offline on the game clock
  (`PREFIX.wav`, `audio_offline_render`) and the ship's position per frame (`PREFIX.txt`). Deterministic for a
  given seed and options. `tools/trailer/make_trailer.sh` builds the trailer from it (no window, GStreamer).
- `--dense-blocks` fills rooms with the maximum block budget (stress test for the layouts and the bot).

When adding a room type or mechanic, teach the bot to use it (so the simulations cover it) and add
invariants / a CTest entry in `CMakeLists.txt`.

## Code conventions

- Single global `Game G` (`src/game.h`), fixed 120 Hz update (`DT`), rendering separate from update.
- Content lives in data tables: `UPGRADES` (`upgrades.c`, weapon mods included), `ENEMIES` (`enemies.c`),
  `STATS` (`stats.c`), `CHARACTERS` (`characters.c`), sound definitions (`audio.c`).
- Characters (Normal / Shotgunner / Knight) only change the starting state (`character_apply`) and the
  ship's look (`draw_ship`); `G.character` survives restarts; `CharDef.start_upgrade` is the weapon mod
  owned from the start (the Knight's sword can also be found by the others). When adding a character: table
  entry, ship drawing, bot support if it plays differently, its unlock condition (`unlocks.c`,
  `character_unlock_hint`), and a `sim_<name>` CTest (`--char NAME`).
- Upgrade effects live in `upgrades.c` as `upg_*` hooks called from bullets / enemies / player /
  room code; the 66 newest (`UPG_TWIN_BARREL .. UPG_SPLASH`, `UPG_X_ARSENAL .. UPG_X_DOOMSDAY`) live in
  `upgrades2.c` as `upg2_*` functions those hooks call (stat effect, cap, description, grant, volley, hit,
  kill, hurt, lethal hit, bomb, bullet death, block break, update, room enter / clear, draw); add new ones
  there (table entries in `UPGRADES` / `MALUSES` + the `upg2_*` hook). Their state lives in the Player
  (reset by `player_init`) and the Enemy (`acid`, `static_n`, `stun_t`: a stunned enemy skips its whole
  behaviour, never a boss - `stun()`); CHRONOSPHERE slows enemies through `upg_enemy_tempo()`, ARMOR PIERCER
  (`upg_armor_piercing()`) skips the armour of `enemy2_damage`, bubbles and the MIRROR plate (never boss armour). **Stats are derived**: the Player stat fields
  (`dmg_mult`, `max_hp`, `extra_proj`...) are always `Player.base` + the stat effects of every upgrade
  turned on (`stat_effect`), rebuilt by `player_recompute()`. Never write them directly: change
  `Player.base` (character, stat trades, pact costs) and call `player_recompute()`; stat upgrades go
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
- **Synergies** (`src/synergies.c`, `SYNERGIES` table, `SynergyId`): extra effects that exist only while a
  set of upgrades is turned on together (`need[]` all required, `any1[]` / `any2[]` one of each group, e.g. any
  fire source + any chill source). No pickup, no state: `syn(SYN_X)` is computed from `upg_has()` (so toggles and
  CHAOS loans follow), and the effect lives next to the upgrades' own code, guarded by `syn()`. A banner names
  each one once per run (`synergies_update`, `Player.syn_seen`); the TAB menu lists the active ones. A new
  synergy needs: enum + table entry, its effect, a line in the README, and a scripted check or a
  `sim_synergy_*` CTest (`--upgrade NAME` / `--weapon NAME`, both repeatable, build any combination).
  Visual effects of explosions are capped per tick (`explode_at`): damage first, fireworks second.
- **The sword combines with every weapon mod** (`sword_swing` / `sword_update` / `SWORD_WAVE_MODS` in
  `weapons.c`): a new weapon mod must say what it does to the blade (a stat change like LASER's lightsaber, an
  on-hit flag, or joining `SWORD_WAVE_MODS` so each swing throws a blade wave armed with it).
- **A malus must change how the upgrade plays**, not be a rounding error: stat maluses of 10-25%, or a
  behaviour kind (`MalusKind`: overheat, recoil, FRAGILE, REVENGE, BIG_BULLETS, NO_MERCY, MOVING_RATE...).
- Weapon mods: `UPG_W_*` entry (between `UPG_W_SCATTER` and `UPG_W_SWORD`), its block in `gun_profile`
  (multiply / add to the gun's numbers so it combines with every other mod; the look comes from the
  most striking mod, the order there and in `gun_main_mod`), option name (`--weapon NAME`, repeatable to test combos), behaviour as
  `PB_*` flags (motion in `bullets.c`, effects in `weapon_on_hit` / `weapon_bullet_death`, called for
  every bullet death so the flags also work for upgrades), a `BulletStyle` if it looks different, a
  `sim_weapon_<name>` and a `sim_sword_<name>` CTest. `gun_profile` clamps combos (cadence, shots per volley, reach): keep any
  mix playable. Reuse `explode_at` / `chain_lightning` for area effects.
- Upgrade descriptions with a random effect must show the exact chance: build them in `upgrade_desc_at`
  (respects `proc()`, i.e. LOADED DICE). A `|` splits a description over two lines (`draw_desc`).
- Shields (`Player.shield`, half-hearts, max `MAX_SHIELD`) absorb damage before hearts.
- Game code only queues sounds (`sfx_play*`); never call SDL audio directly.
- **Main menu** (`src/title.c`, `GS_TITLE`): a demo run played by the bot runs behind it in the normal world
  (`G.demo`), on its own generator (`G.demo_rng`, swapped with `G.rng` for each demo tick) with sounds
  suppressed (`audio_suppress`) and no HUD (`demo_visible()`); `game_new_run` wipes it. Any new per-run
  state must be reset by `game_new_run` (the demo leaves its own behind). `--title` renders the menu,
  `--load-menu --save-dir DIR` the saves list (sample saves written into an empty folder), `--save-menu` a save's
  menu, `--unlocks-menu` the unlocks screen, `--bestiary NAME` the bestiary on an enemy, `--char-select --fresh-profile`
  the locked ships.
- **Bestiary small screen**: while it is open, G's world (room, entities, ship, rng) IS the small world (`world_reset` /
  `world_step` in `bestiary.c`, flagged `G.demo`, sounds off): the real one (the menu's demo, paused and not drawn,
  `title_world_hidden`) is copied aside on opening and put back by `bestiary_close`, the menu's fields (input, page,
  profile) carrying on; the path finder caches are rebuilt then. Drawn scaled into its rect through `draw_set_view` /
  `draw_clip` (`draw.c`). A new enemy needs a `DESC` line there (and a spawn group / wide view if it needs one);
  `--debug-rooms` checks every type shows up and fires in it and the real world comes back untouched.
- Controls: SHIFT / right click = active items, SPACE = bomb, E = take / buy / use, TAB = upgrades menu, R held
  `RESTART_HOLD_T` in a run = a new run with the same ship (`Input.restart_held`, `G.restart_hold`: let go before the next one).
  Test key: A on AZERTY (`SDL_SCANCODE_Q`, a physical key: Q on QWERTY) finishes the room (`room_skip`: enemies gone
  without kill effects, no more waves, a real clear - shatter, reward, a boss's unlocks). The
  mouse is grabbed during a run (`SDL_SetWindowMouseGrab` in `main.c`, released in pause / menus / game over).
- In-game text is English and ASCII only (SDL's debug font has no accents). The user writes in French.
