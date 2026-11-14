# SDL3 is taken from the system when available, otherwise it is downloaded and
# built from source with FetchContent (static library).

include(FetchContent)

set(SHMUP_SDL3_VERSION "3.4.20" CACHE STRING "SDL3 release fetched when not found on the system")

find_package(SDL3 3.2 CONFIG QUIET)
if(SDL3_FOUND)
    message(STATUS "Using system SDL3 ${SDL3_VERSION}")
else()
    message(STATUS "SDL3 not found on the system: fetching release-${SHMUP_SDL3_VERSION}")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-${SHMUP_SDL3_VERSION}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
endif()
