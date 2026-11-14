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
