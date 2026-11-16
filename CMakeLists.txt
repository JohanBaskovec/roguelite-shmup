cmake_minimum_required(VERSION 3.25)

project(NeonBreach
    VERSION 0.1.0
    DESCRIPTION "Ultra-fast roguelite shmup written in C with SDL3"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(cmake/Dependencies.cmake)

add_executable(neon_breach
    src/main.c
    src/audio.c
    src/stats.c
    src/bot.c
    src/headless.c
    src/game.c
    src/title.c
    src/draw.c
    src/fx.c
    src/room.c
    src/blocks.c
    src/player.c
    src/weapons.c
    src/upgrade_menu.c
    src/bullets.c
    src/enemies.c
    src/bestiary.c
    src/bosses.c
    src/pickups.c
    src/hud.c
    src/upgrades.c
    src/debug_menu.c
    src/characters.c
    src/workshop.c
    src/actives.c
    src/maluses.c
    src/synergies.c
    src/save.c
    src/unlocks.c)

target_link_libraries(neon_breach PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(neon_breach PRIVATE /W4)
else()
    target_link_libraries(neon_breach PRIVATE m)
    target_compile_options(neon_breach PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

option(SHMUP_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(SHMUP_SANITIZERS AND NOT MSVC)
    # -O1: the speed ASan is meant to run at (the Debug build type keeps asserts and debug info)
    target_compile_options(neon_breach PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -O1)
    target_link_options(neon_breach PRIVATE -fsanitize=address,undefined)
endif()

# ---- Tests ------------------------------------------------------------------
# Everything runs without opening a window: the simulation tests are pure CPU
# (--headless), the render test draws into SDL's offscreen video driver.
option(SHMUP_BUILD_TESTS "Register the headless tests with CTest" ON)
if(SHMUP_BUILD_TESTS)
    enable_testing()
    set(_out ${CMAKE_CURRENT_BINARY_DIR}/test-output)
    file(MAKE_DIRECTORY ${_out}/sfx)

    # Invincible bot: checks progression and invariants over 10 min of game time.
    foreach(seed 1 2 3)
        add_test(NAME sim_god_seed${seed}
                 COMMAND neon_breach --headless --god --seed ${seed} --ticks 72000 --expect-rooms 5
                         --fail-shot ${_out}/fail_sim_god_seed${seed}.bmp)
    endforeach()
    # Mortal bot: 10 full runs until death or time limit.
    # (Long batches of runs are split in parts, run side by side: same seeds, a fraction of the wait.)
    add_test(NAME sim_mortal_runs
             COMMAND neon_breach --headless --seed 100 --runs 5 --ticks 72000 --fail-shot ${_out}/fail_sim_mortal.bmp)
    add_test(NAME sim_mortal_runs_b
             COMMAND neon_breach --headless --seed 105 --runs 5 --ticks 72000 --fail-shot ${_out}/fail_sim_mortal_b.bmp)
    # Late-game rooms (dense waves, every enemy type).
    add_test(NAME sim_deep_rooms
             COMMAND neon_breach --headless --god --depth 15 --seed 7 --runs 3 --ticks 36000 --fail-shot ${_out}/fail_sim_deep.bmp)
    # Stats rooms: the bot takes a trade on every run, then keeps playing.
    add_test(NAME sim_stats_rooms
             COMMAND neon_breach --headless --god --stats --seed 50 --runs 12 --ticks 36000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_stats.bmp)
    # Starts in a shop with 60 coins: the bot must buy items; a shop must always offer 5.
    add_test(NAME sim_shop_rooms
             COMMAND neon_breach --headless --god --shop --coins 60 --seed 60 --runs 8 --ticks 36000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_shop.bmp)
    add_test(NAME render_shop_room
             COMMAND neon_breach --shot ${_out}/render_shop.bmp --shop --coins 10 --god --seed 4 --frames 435)
    # Every room hides a secret: the bot must spot the flash, bomb the corner, clear the
    # secret room and come back to the room it was found in.
    add_test(NAME sim_secret_rooms
             COMMAND neon_breach --headless --god --secret --bombs 3 --seed 70 --runs 8 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_secret.bmp)
    # Every upgrade at its max stack: all the bullet effects / auras / triggers at once.
    add_test(NAME sim_all_upgrades
             COMMAND neon_breach --headless --god --all-upgrades --seed 80 --runs 2 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_all_upgrades.bmp)
    add_test(NAME sim_all_upgrades_b
             COMMAND neon_breach --headless --god --all-upgrades --seed 82 --runs 2 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_all_upgrades_b.bmp)
    foreach(_s 90 92 94)
        add_test(NAME sim_all_upgrades_mortal_${_s}
                 COMMAND neon_breach --headless --all-upgrades --seed ${_s} --runs 2 --ticks 36000
                         --fail-shot ${_out}/fail_sim_all_upgrades_mortal_${_s}.bmp)
    endforeach()
    # Characters: every one must be playable by the bot (the Knight fights in melee with its sword).
    add_test(NAME sim_shotgunner
             COMMAND neon_breach --headless --god --char shotgunner --seed 11 --runs 3 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_shotgunner.bmp)
    add_test(NAME sim_knight
             COMMAND neon_breach --headless --god --char knight --seed 21 --runs 4 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_knight.bmp)
    add_test(NAME sim_knight_mortal_b
             COMMAND neon_breach --headless --char knight --seed 35 --runs 5
                     --fail-shot ${_out}/fail_sim_knight_mortal_b.bmp)
    add_test(NAME sim_knight_mortal
             COMMAND neon_breach --headless --char knight --seed 30 --runs 5
                     --fail-shot ${_out}/fail_sim_knight_mortal.bmp)
    # F3 debug menu: teleport into every room type, with and without enemies.
    add_test(NAME sim_workshop
             COMMAND neon_breach --headless --god --workshop --coins 30 --seed 3 --runs 4 --ticks 20000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_workshop.bmp)
    add_test(NAME render_workshop
             COMMAND neon_breach --shot ${_out}/render_workshop.bmp --workshop --cleared --no-bot --depth 4 --seed 5 --frames 60)
    # Boss rooms (every 10 rooms): the boss must be beaten, by every character; the run
    # from room 8 must go through the single boss door of room 9.
    add_test(NAME sim_boss
             COMMAND neon_breach --headless --god --boss --seed 50 --runs 4 --ticks 36000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_boss.bmp)
    # Each boss (room 10: one of four; 20, 30, 40: their own, the bot with a few upgrades as it would
    # have by then; beyond: any of them, more minions).
    foreach(_boss overseer lighthouse wyrm stormcaller)
        add_test(NAME sim_boss_${_boss}
                 COMMAND neon_breach --headless --god --boss --boss-kind ${_boss} --seed 100 --runs 2 --ticks 36000 --expect-rooms 2
                         --fail-shot ${_out}/fail_sim_boss_${_boss}.bmp)
        add_test(NAME render_boss_${_boss}
                 COMMAND neon_breach --shot ${_out}/render_boss_${_boss}.bmp --boss --boss-kind ${_boss} --seed 5 --frames 1500)
    endforeach()
    set(_deep_upgrades --upgrade heavy-rounds --upgrade overclock --upgrade split-shot)
    foreach(_boss_depth forge:20 carrier:30 singularity:40)
        string(REPLACE ":" ";" _bd ${_boss_depth})
        list(GET _bd 0 _boss)
        list(GET _bd 1 _depth)
        add_test(NAME sim_boss_${_boss}
                 COMMAND neon_breach --headless --god --boss --boss-kind ${_boss} --depth ${_depth} ${_deep_upgrades} --seed 100
                         --runs 2 --ticks 36000 --expect-rooms 2 --fail-shot ${_out}/fail_sim_boss_${_boss}.bmp)
        add_test(NAME render_boss_${_boss}
                 COMMAND neon_breach --shot ${_out}/render_boss_${_boss}.bmp --boss --boss-kind ${_boss} --depth ${_depth} --seed 5 --frames 1500)
    endforeach()
    add_test(NAME sim_boss_any
             COMMAND neon_breach --headless --god --boss --depth 50 ${_deep_upgrades} --seed 200 --runs 4 --ticks 36000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_boss_any.bmp)
    add_test(NAME sim_boss_mortal
             COMMAND neon_breach --headless --boss --hp 40 --seed 300 --runs 4 --ticks 36000 --expect-rooms 1
                     --fail-shot ${_out}/fail_sim_boss_mortal.bmp)
    add_test(NAME sim_boss_shotgunner
             COMMAND neon_breach --headless --god --boss --char shotgunner --seed 60 --runs 3 --ticks 30000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_boss_shotgunner.bmp)
    add_test(NAME sim_boss_knight
             COMMAND neon_breach --headless --god --boss --char knight --seed 70 --runs 3 --ticks 30000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_boss_knight.bmp)
    add_test(NAME sim_boss_door
             COMMAND neon_breach --headless --god --depth 8 --seed 80 --runs 3 --ticks 40000 --expect-rooms 4
                     --fail-shot ${_out}/fail_sim_boss_door.bmp)
    add_test(NAME render_boss
             COMMAND neon_breach --shot ${_out}/render_boss.bmp --boss --seed 5 --frames 700)
    # The 20 newer enemies: rooms made of only one of them (any depth), the bot must clear
    # them (invariants: crawlers stay on the wall...); one Knight run with them all (melee).
    foreach(_e rammer grenadier crawler top kamikaze dodger rook splitter leaper minelayer
               boomerang ricochet orbiter blinker serpent mirror lancer mortar guardian wraith)
        add_test(NAME sim_enemy_${_e}
                 COMMAND neon_breach --headless --god --enemy ${_e} --depth 6 --seed 31 --runs 2 --ticks 24000 --expect-rooms 3
                         --fail-shot ${_out}/fail_sim_enemy_${_e}.bmp)
    endforeach()
    add_test(NAME sim_enemies_knight
             COMMAND neon_breach --headless --god --char knight --depth 8 --seed 32 --runs 2 --ticks 36000 --expect-rooms 3
                     --enemy rammer --enemy crawler --enemy top --enemy dodger --enemy rook --enemy leaper --enemy mirror
                     --enemy wraith --enemy guardian --enemy serpent
                     --fail-shot ${_out}/fail_sim_enemies_knight.bmp)
    add_test(NAME render_enemies_ground
             COMMAND neon_breach --shot ${_out}/render_enemies_ground.bmp --depth 12 --god --seed 7 --frames 330
                     --enemy mortar --enemy grenadier --enemy guardian --enemy leaper --enemy minelayer)
    add_test(NAME render_enemies_moves
             COMMAND neon_breach --shot ${_out}/render_enemies_moves.bmp --depth 12 --god --seed 9 --frames 520
                     --enemy lancer --enemy serpent --enemy boomerang --enemy top --enemy rammer --enemy blinker
                     --enemy crawler --enemy mirror)
    # Rooms packed with breakable blocks and gates: the bot shoots its way to the doors.
    add_test(NAME sim_dense_blocks
             COMMAND neon_breach --headless --god --dense-blocks --seed 41 --runs 2 --ticks 40000 --expect-rooms 8
                     --fail-shot ${_out}/fail_sim_dense_blocks.bmp)
    add_test(NAME sim_dense_blocks_knight
             COMMAND neon_breach --headless --god --dense-blocks --char knight --seed 42 --runs 2 --ticks 40000 --expect-rooms 8
                     --fail-shot ${_out}/fail_sim_dense_blocks_knight.bmp)
    add_test(NAME render_blocks
             COMMAND neon_breach --shot ${_out}/render_blocks.bmp --dense-blocks --depth 4 --seed 12 --frames 400)
    add_test(NAME render_boss_door
             COMMAND neon_breach --shot ${_out}/render_boss_door.bmp --depth 9 --cleared --no-bot --seed 5 --frames 120)
    add_test(NAME render_deep_enemies
             COMMAND neon_breach --shot ${_out}/render_deep.bmp --depth 15 --seed 8 --frames 300)
    add_test(NAME sim_active_shield
             COMMAND neon_breach --headless --god --active shield --seed 40 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_shield.bmp)
    add_test(NAME sim_active_boom
             COMMAND neon_breach --headless --god --active boom --seed 40 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_boom.bmp)
    add_test(NAME sim_active_strength
             COMMAND neon_breach --headless --god --active strength --seed 40 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_strength.bmp)
    add_test(NAME sim_active_chaos
             COMMAND neon_breach --headless --god --active chaos --seed 40 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_chaos.bmp)
    add_test(NAME sim_active_rooms
             COMMAND neon_breach --headless --god --active-room --seed 50 --runs 3 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_rooms.bmp)
    add_test(NAME render_active_room
             COMMAND neon_breach --shot ${_out}/render_active_room.bmp --active-room --active dash --cleared --no-bot --depth 2 --seed 5 --frames 60)
    add_test(NAME render_chaos
             COMMAND neon_breach --shot ${_out}/render_chaos.bmp --active chaos --depth 6 --seed 3 --frames 500)
    add_test(NAME sim_weapon_railgun
             COMMAND neon_breach --headless --god --weapon railgun --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_railgun.bmp)
    add_test(NAME sim_weapon_flamer
             COMMAND neon_breach --headless --god --weapon flamer --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_flamer.bmp)
    add_test(NAME sim_weapon_rockets
             COMMAND neon_breach --headless --god --weapon rockets --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_rockets.bmp)
    add_test(NAME sim_weapon_chakram
             COMMAND neon_breach --headless --god --weapon chakram --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_chakram.bmp)
    add_test(NAME sim_weapon_cluster
             COMMAND neon_breach --headless --god --weapon cluster --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_cluster.bmp)
    add_test(NAME sim_weapon_arc
             COMMAND neon_breach --headless --god --weapon arc --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_arc.bmp)
    add_test(NAME sim_weapon_vulcan
             COMMAND neon_breach --headless --god --weapon vulcan --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_vulcan.bmp)
    add_test(NAME sim_weapon_wave
             COMMAND neon_breach --headless --god --weapon wave --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_weapon_wave.bmp)
    foreach(_w laser orbit prism graviton)
        add_test(NAME sim_weapon_${_w}
                 COMMAND neon_breach --headless --god --weapon ${_w} --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                         --fail-shot ${_out}/fail_sim_weapon_${_w}.bmp)
    endforeach()
    # The sword combines with every shot upgrade (lightsaber, greatsword, blade waves, whirlwind...).
    foreach(_w laser railgun orbit prism graviton chakram rockets)
        add_test(NAME sim_sword_${_w}
                 COMMAND neon_breach --headless --god --char knight --weapon ${_w} --seed 210 --runs 2 --ticks 24000 --expect-rooms 3
                         --fail-shot ${_out}/fail_sim_sword_${_w}.bmp)
    endforeach()
    # Synergies (extra effects of upgrades owned together): a few builds played by the bot; the
    # --all-upgrades simulations run every synergy at once.
    add_test(NAME sim_synergy_parry
             COMMAND neon_breach --headless --god --char knight --upgrade deflector-shells --upgrade shadow-clone --upgrade return-vector
                     --seed 220 --runs 2 --ticks 24000 --expect-rooms 3 --fail-shot ${_out}/fail_sim_synergy_parry.bmp)
    add_test(NAME sim_synergy_elements
             COMMAND neon_breach --headless --god --upgrade cryo-rounds --upgrade thermite --upgrade venom-tips --upgrade arc-emitter
                     --upgrade voodoo-link --upgrade chrono-bubble --seed 221 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_synergy_elements.bmp)
    add_test(NAME sim_synergy_beams
             COMMAND neon_breach --headless --god --weapon laser --weapon arc --weapon flamer --seed 222 --runs 2 --ticks 24000
                     --expect-rooms 3 --fail-shot ${_out}/fail_sim_synergy_beams.bmp)
    add_test(NAME sim_synergy_saws
             COMMAND neon_breach --headless --god --weapon chakram --weapon orbit --seed 223 --runs 2 --ticks 24000
                     --expect-rooms 3 --fail-shot ${_out}/fail_sim_synergy_saws.bmp)
    add_test(NAME sim_synergy_heavy
             COMMAND neon_breach --headless --god --weapon railgun --weapon graviton --upgrade black-hole --seed 224 --runs 2
                     --ticks 24000 --expect-rooms 3 --fail-shot ${_out}/fail_sim_synergy_heavy.bmp)
    add_test(NAME sim_synergy_mirv
             COMMAND neon_breach --headless --god --weapon rockets --weapon seeker --weapon cluster --seed 225 --runs 2
                     --ticks 24000 --expect-rooms 3 --fail-shot ${_out}/fail_sim_synergy_mirv.bmp)
    add_test(NAME sim_synergy_defence
             COMMAND neon_breach --headless --upgrade aegis --upgrade deflector-shells --upgrade spiked-hull --upgrade guardian-orb
                     --upgrade tesla-field --upgrade wing-drone --upgrade point-defense --upgrade graze --seed 226 --runs 4
                     --fail-shot ${_out}/fail_sim_synergy_defence.bmp)
    add_test(NAME render_synergies_menu
             COMMAND neon_breach --shot ${_out}/render_synergies_menu.bmp --all-upgrades --upgrades-menu --seed 5 --frames 30)
    add_test(NAME render_laser
             COMMAND neon_breach --shot ${_out}/render_laser.bmp --weapon laser --depth 6 --seed 5 --frames 300)
    add_test(NAME render_lightsaber
             COMMAND neon_breach --shot ${_out}/render_lightsaber.bmp --char knight --weapon laser --depth 6 --seed 5 --frames 300)
    foreach(_w scatter needler seeker bouncer)
        add_test(NAME sim_weapon_${_w}
                 COMMAND neon_breach --headless --god --weapon ${_w} --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                         --fail-shot ${_out}/fail_sim_weapon_${_w}.bmp)
    endforeach()
    add_test(NAME sim_toggle_upgrades
             COMMAND neon_breach --headless --god --toggle-upgrades --seed 500 --runs 4 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_toggle.bmp)
    add_test(NAME sim_toggle_all_upgrades
             COMMAND neon_breach --headless --god --all-upgrades --toggle-upgrades --seed 510 --runs 2 --ticks 24000 --expect-rooms 2
                     --fail-shot ${_out}/fail_sim_toggle_all.bmp)
    add_test(NAME render_upgrades_menu
             COMMAND neon_breach --shot ${_out}/render_upgrades_menu.bmp --all-upgrades --upgrades-menu --seed 13 --frames 30)
    add_test(NAME sim_active_multi
             COMMAND neon_breach --headless --god --active dash --active nova --active shield --active boom --active turret
                     --seed 200 --runs 2 --ticks 24000 --expect-rooms 3 --fail-shot ${_out}/fail_sim_active_multi.bmp)
    add_test(NAME sim_active_multi_toggle
             COMMAND neon_breach --headless --active chaos --active blades --active timestop --active strength --toggle-upgrades
                     --seed 300 --runs 3 --ticks 24000 --fail-shot ${_out}/fail_sim_active_multi_toggle.bmp)
    add_test(NAME render_actives_hud
             COMMAND neon_breach --shot ${_out}/render_actives_hud.bmp --active dash --active shield --active nova --active chaos
                     --active turret --depth 4 --seed 14 --frames 400)
    add_test(NAME sim_active_reversal
             COMMAND neon_breach --headless --god --active reversal --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_reversal.bmp)
    add_test(NAME sim_active_turret
             COMMAND neon_breach --headless --god --active turret --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_turret.bmp)
    add_test(NAME sim_active_blink
             COMMAND neon_breach --headless --god --active blink --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_blink.bmp)
    add_test(NAME sim_active_nova
             COMMAND neon_breach --headless --god --active nova --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_nova.bmp)
    add_test(NAME sim_active_timestop
             COMMAND neon_breach --headless --god --active timestop --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_timestop.bmp)
    add_test(NAME sim_active_blades
             COMMAND neon_breach --headless --god --active blades --seed 200 --runs 2 --ticks 24000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_active_blades.bmp)
    add_test(NAME render_rockets
             COMMAND neon_breach --shot ${_out}/render_rockets.bmp --weapon rockets --active blades --depth 6 --seed 11 --frames 420)
    add_test(NAME render_chakram
             COMMAND neon_breach --shot ${_out}/render_chakram.bmp --weapon chakram --active turret --depth 6 --seed 12 --frames 420)
    add_test(NAME sim_debug_teleports
             COMMAND neon_breach --headless --debug-rooms --seed 1 --coins 40 --ticks 12000
                     --fail-shot ${_out}/fail_debug_teleports.bmp)
    add_test(NAME render_debug_menu
             COMMAND neon_breach --shot ${_out}/render_debug_menu.bmp --debug-menu --depth 4 --seed 4 --frames 3)
    add_test(NAME render_stats_room
             COMMAND neon_breach --shot ${_out}/render_stats.bmp --stats --seed 4 --frames 20)
    add_test(NAME render_stats_overlay
             COMMAND neon_breach --shot ${_out}/render_stats_overlay.bmp --show-stats --all-upgrades --depth 6 --seed 9 --frames 200)
    # Main menu over the demo run the bot plays behind it (several rooms in 1500 frames).
    add_test(NAME render_title
             COMMAND neon_breach --shot ${_out}/render_title.bmp --title --seed 4 --frames 1500)
    add_test(NAME render_char_select
             COMMAND neon_breach --shot ${_out}/render_char_select.bmp --char-select --seed 4 --frames 30)
    add_test(NAME render_knight
             COMMAND neon_breach --shot ${_out}/render_knight.bmp --char knight --depth 3 --seed 7 --frames 413)
    add_test(NAME render_offscreen
             COMMAND neon_breach --shot ${_out}/render.bmp --depth 4 --seed 4 --frames 30)
    add_test(NAME sfx_synthesis COMMAND neon_breach --dump-sfx ${_out}/sfx)
    # Save files: saved on entering each room (never during one), loading replays the room exactly,
    # death ends the run but keeps the save and its unlocks, the menus (NEW SAVE, CONTINUE, save menu,
    # unlocks, character select) work (each test its own folder).
    add_test(NAME sim_save_load COMMAND neon_breach --headless --save-test --seed 3 --save-dir ${_out}/saves_test)
    add_test(NAME sim_save_runs
             COMMAND neon_breach --headless --seed 31 --runs 4 --save-dir ${_out}/saves_runs --expect-rooms 3)
    add_test(NAME render_load_menu
             COMMAND neon_breach --shot ${_out}/render_load_menu.bmp --load-menu --save-dir ${_out}/saves_render --seed 4 --frames 30)
    # Unlocks: runs on a new save's unlocks, kept from run to run in one file; every boss beaten unlocks
    # the next upgrades at once (saved at once), never a locked upgrade offered, ships by depth / boss.
    add_test(NAME sim_unlocks
             COMMAND neon_breach --headless --fresh-profile --god --seed 40 --runs 2 --save-dir ${_out}/saves_unlocks --expect-rooms 12)
    add_test(NAME sim_unlocks_mortal
             COMMAND neon_breach --headless --fresh-profile --seed 31 --runs 6 --save-dir ${_out}/saves_unlocks_mortal --expect-rooms 4)
    add_test(NAME render_save_menu
             COMMAND neon_breach --shot ${_out}/render_save_menu.bmp --save-menu --save-dir ${_out}/saves_render_menu --seed 4 --frames 30)
    add_test(NAME render_unlocks_menu
             COMMAND neon_breach --shot ${_out}/render_unlocks_menu.bmp --unlocks-menu --seed 4 --frames 30)
    # The bestiary: an enemy, then a boss, playing in the small screen.
    add_test(NAME render_bestiary
             COMMAND neon_breach --shot ${_out}/render_bestiary.bmp --bestiary mirror --seed 4 --frames 600)
    add_test(NAME render_bestiary_boss
             COMMAND neon_breach --shot ${_out}/render_bestiary_boss.bmp --bestiary carrier --seed 4 --frames 900)
    add_test(NAME render_char_select_locked
             COMMAND neon_breach --shot ${_out}/render_char_select_locked.bmp --char-select --fresh-profile --seed 4 --frames 30)

    # The asan preset halves the simulations (NEON_TICK_SCALE=0.5): sanitizers find their bugs early, and
    # the all-upgrades / dense-blocks stress tests are at their peak from the first room. Long runs that
    # only reach their extremes by going deep keep their full length.
    set_tests_properties(sim_mortal_runs sim_mortal_runs_b sim_deep_rooms PROPERTIES ENVIRONMENT "NEON_TICK_SCALE=1")
    set_tests_properties(sim_god_seed1 sim_god_seed2 sim_god_seed3 sim_mortal_runs sim_mortal_runs_b sim_deep_rooms
                         sim_stats_rooms sim_shop_rooms render_shop_room sim_secret_rooms sim_all_upgrades sim_all_upgrades_b
                         sim_all_upgrades_mortal_90 sim_all_upgrades_mortal_92 sim_all_upgrades_mortal_94
                         sim_shotgunner sim_knight sim_knight_mortal sim_knight_mortal_b render_char_select render_knight
                         sim_workshop render_workshop render_deep_enemies
                         sim_active_shield sim_active_boom sim_active_strength sim_active_chaos sim_active_rooms
                         render_active_room render_chaos
                         sim_weapon_railgun sim_weapon_flamer sim_weapon_rockets sim_weapon_chakram sim_weapon_cluster
                         sim_weapon_arc sim_weapon_vulcan sim_weapon_wave sim_active_reversal sim_active_turret
                         sim_active_blink sim_active_nova sim_active_timestop sim_active_blades render_rockets
                         render_chakram sim_weapon_scatter sim_weapon_needler sim_weapon_seeker sim_weapon_bouncer
                         sim_toggle_upgrades sim_toggle_all_upgrades render_upgrades_menu
                         sim_debug_teleports render_debug_menu render_stats_room render_stats_overlay render_offscreen sfx_synthesis PROPERTIES TIMEOUT 600)
    # Labels by area, from the test names (`ctest --preset release -L enemies`): a new test gets its
    # labels by itself. Anything touching the bot, the rooms' flow or shared code: the whole suite.
    get_property(_all_tests DIRECTORY PROPERTY TESTS)
    foreach(_t IN LISTS _all_tests)
        set(_labels "")
        if(_t MATCHES "^render_")
            list(APPEND _labels render)
        endif()
        if(_t MATCHES "enem")
            list(APPEND _labels enemies)
        endif()
        if(_t MATCHES "weapon|sword|rockets|chakram|laser|lightsaber")
            list(APPEND _labels weapons)
        endif()
        if(_t MATCHES "active|chaos")
            list(APPEND _labels actives)
        endif()
        if(_t MATCHES "synerg")
            list(APPEND _labels synergies)
        endif()
        if(_t MATCHES "upgrades")
            list(APPEND _labels upgrades)
        endif()
        if(_t MATCHES "boss")
            list(APPEND _labels boss)
        endif()
        if(_t MATCHES "knight|shotgunner|char_select")
            list(APPEND _labels characters)
        endif()
        if(_t MATCHES "rooms|room$|shop|workshop|secret|blocks|door|teleports|debug_menu")
            list(APPEND _labels rooms)
        endif()
        if(_t MATCHES "god_seed|mortal|deep_rooms")
            list(APPEND _labels runs)
        endif()
        if(_t MATCHES "save|load_menu|unlock")
            list(APPEND _labels save)
        endif()
        if(_t MATCHES "^sfx_")
            list(APPEND _labels audio)
        endif()
        if(_labels)
            set_tests_properties(${_t} PROPERTIES LABELS "${_labels}")
        endif()
    endforeach()
    # "quick": one test per area (rooms, characters, boss, workshop, actives, weapon mods, layouts,
    # rendering, sound), a few seconds with the quick test preset (NEON_TICK_SCALE=0.4). For small edits
    # only: the full suite stays the step before a commit.
    set_property(TEST sim_debug_teleports sim_god_seed1 sim_knight_mortal sim_all_upgrades sim_stats_rooms sim_shop_rooms
                      sim_secret_rooms sim_workshop sim_boss sim_active_rooms sim_active_multi sim_knight sim_shotgunner
                      sim_toggle_upgrades sim_dense_blocks sim_enemies_knight sim_weapon_laser render_offscreen
                      render_stats_room sfx_synthesis sim_save_load sim_unlocks APPEND PROPERTY LABELS quick)
endif()
