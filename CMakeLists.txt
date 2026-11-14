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
    src/hud.c)

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
    add_test(NAME render_stats_room
             COMMAND neon_breach --shot ${_out}/render_stats.bmp --stats --seed 4 --frames 20)
    add_test(NAME render_offscreen
             COMMAND neon_breach --shot ${_out}/render.bmp --depth 4 --seed 4 --frames 30)
    add_test(NAME sfx_synthesis COMMAND neon_breach --dump-sfx ${_out}/sfx)

    set_tests_properties(sim_god_seed1 sim_god_seed2 sim_god_seed3 sim_mortal_runs sim_deep_rooms
                         sim_stats_rooms render_stats_room render_offscreen sfx_synthesis PROPERTIES TIMEOUT 600)
endif()
