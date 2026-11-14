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
    src/draw.c
    src/fx.c
    src/room.c
    src/player.c
    src/weapons.c
    src/bullets.c
    src/enemies.c
    src/pickups.c
    src/hud.c
    src/upgrades.c
    src/debug_menu.c
    src/characters.c
    src/workshop.c
    src/actives.c)

target_link_libraries(neon_breach PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(neon_breach PRIVATE /W4)
else()
    target_link_libraries(neon_breach PRIVATE m)
    target_compile_options(neon_breach PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

option(SHMUP_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(SHMUP_SANITIZERS AND NOT MSVC)
    target_compile_options(neon_breach PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
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
    add_test(NAME sim_mortal_runs
             COMMAND neon_breach --headless --seed 100 --runs 10 --ticks 72000 --fail-shot ${_out}/fail_sim_mortal.bmp)
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
             COMMAND neon_breach --headless --god --secret --seed 70 --runs 8 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_secret.bmp)
    # Every upgrade at its max stack: all the bullet effects / auras / triggers at once.
    add_test(NAME sim_all_upgrades
             COMMAND neon_breach --headless --god --all-upgrades --seed 80 --runs 4 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_all_upgrades.bmp)
    add_test(NAME sim_all_upgrades_mortal
             COMMAND neon_breach --headless --all-upgrades --seed 90 --runs 6 --ticks 36000
                     --fail-shot ${_out}/fail_sim_all_upgrades_mortal.bmp)
    # Characters: every one must be playable by the bot (the Knight fights in melee with its sword).
    add_test(NAME sim_shotgunner
             COMMAND neon_breach --headless --god --char shotgunner --seed 11 --runs 3 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_shotgunner.bmp)
    add_test(NAME sim_knight
             COMMAND neon_breach --headless --god --char knight --seed 21 --runs 4 --ticks 36000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_knight.bmp)
    add_test(NAME sim_knight_mortal
             COMMAND neon_breach --headless --char knight --seed 30 --runs 10
                     --fail-shot ${_out}/fail_sim_knight_mortal.bmp)
    # F3 debug menu: teleport into every room type, with and without enemies.
    add_test(NAME sim_workshop
             COMMAND neon_breach --headless --god --workshop --coins 30 --seed 3 --runs 4 --ticks 20000 --expect-rooms 3
                     --fail-shot ${_out}/fail_sim_workshop.bmp)
    add_test(NAME render_workshop
             COMMAND neon_breach --shot ${_out}/render_workshop.bmp --workshop --cleared --no-bot --depth 4 --seed 5 --frames 60)
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
             COMMAND neon_breach --shot ${_out}/render_stats_overlay.bmp --show-stats --all-upgrades --depth 6 --seed 9 --frames 600)
    add_test(NAME render_char_select
             COMMAND neon_breach --shot ${_out}/render_char_select.bmp --char-select --seed 4 --frames 30)
    add_test(NAME render_knight
             COMMAND neon_breach --shot ${_out}/render_knight.bmp --char knight --depth 3 --seed 7 --frames 413)
    add_test(NAME render_offscreen
             COMMAND neon_breach --shot ${_out}/render.bmp --depth 4 --seed 4 --frames 30)
    add_test(NAME sfx_synthesis COMMAND neon_breach --dump-sfx ${_out}/sfx)

    set_tests_properties(sim_god_seed1 sim_god_seed2 sim_god_seed3 sim_mortal_runs sim_deep_rooms
                         sim_stats_rooms sim_shop_rooms render_shop_room sim_secret_rooms sim_all_upgrades sim_all_upgrades_mortal
                         sim_shotgunner sim_knight sim_knight_mortal render_char_select render_knight
                         sim_workshop render_workshop render_deep_enemies
                         sim_active_shield sim_active_boom sim_active_strength sim_active_chaos sim_active_rooms
                         render_active_room render_chaos
                         sim_weapon_railgun sim_weapon_flamer sim_weapon_rockets sim_weapon_chakram sim_weapon_cluster
                         sim_weapon_arc sim_weapon_vulcan sim_weapon_wave sim_active_reversal sim_active_turret
                         sim_active_blink sim_active_nova sim_active_timestop sim_active_blades render_rockets
                         render_chakram
                         sim_debug_teleports render_debug_menu render_stats_room render_stats_overlay render_offscreen sfx_synthesis PROPERTIES TIMEOUT 600)
endif()
