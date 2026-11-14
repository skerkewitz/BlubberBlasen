cmake_minimum_required(VERSION 3.20)
project(BlubberBlasen C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Strict C99 (no compiler extensions) for the game code only, see below;
# raylib's web platform and our platform layer need the GNU extensions (EM_ASM).

# --- raylib (platform layer only) -------------------------------------------
# Web build: configure with emcmake (see README); raylib then uses its web platform.
if(EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "raylib platform" FORCE)
endif()
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# --- Generated level data: assets/levels/*.map -> level_data.c ----------------
file(GLOB LEVEL_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/levels/*.map)
file(GLOB LEVEL_SETTINGS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/levels/*.txt)
list(SORT LEVEL_FILES)

# --- Host tools (they use LEVEL_FILES) ------------------------------------------
add_subdirectory(tools)
set(LEVEL_DATA_C ${PROJECT_BINARY_DIR}/generated/level_data.c)
add_custom_command(
    OUTPUT  ${LEVEL_DATA_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/generated
    COMMAND map2c -o ${LEVEL_DATA_C} ${LEVEL_FILES}
    DEPENDS map2c ${LEVEL_FILES} ${LEVEL_SETTINGS}
    COMMENT "map2c: converting levels"
    VERBATIM
)

# --- Portable game code: must not depend on raylib ---------------------------
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)
add_library(game STATIC ${GAME_SOURCES} ${LEVEL_DATA_C})
target_include_directories(game PUBLIC src)
set_target_properties(game PROPERTIES C_EXTENSIONS OFF)

# Debug switches (src/game/debug.c, dev command-line options). On for
# development; public builds (web, release, SNES) configure with
# -DBB_DEBUG_SWITCHES=OFF and contain none of it.
option(BB_DEBUG_SWITCHES "Compile the debug switches and developer options" ON)
if(BB_DEBUG_SWITCHES)
    target_compile_definitions(game PUBLIC BB_DEBUG=1)
endif()

if(MSVC)
    target_compile_options(game PRIVATE /W4)
else()
    target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Platform layer + executable --------------------------------------------
add_executable(blubberblasen src/platform/platform_raylib.c)
target_link_libraries(blubberblasen PRIVATE game raylib)
if(EMSCRIPTEN)
    # Web: a page with the game, the assets packed into blubberblasen.data at /assets.
    # Level sources are compiled in, so they stay out; so does the unused ambient track.
    set_target_properties(blubberblasen PROPERTIES SUFFIX ".html")
    target_compile_definitions(blubberblasen PRIVATE BB_ASSET_DIR="/assets")
    target_link_options(blubberblasen PRIVATE
        -sUSE_GLFW=3
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        -lidbfs.js
        # raylib's audio library (miniaudio) reads Module.HEAPF32 in its audio
        # callback; newer Emscripten versions no longer export it by default.
        -sEXPORTED_RUNTIME_METHODS=HEAPF32
        "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/assets@/assets"
        "SHELL:--exclude-file *.map"
        "SHELL:--exclude-file *.txt"
        "SHELL:--exclude-file *.md"
        "SHELL:--exclude-file *guarded-stillness*"
        "SHELL:--shell-file ${PROJECT_SOURCE_DIR}/web/shell.html")
    set_property(TARGET blubberblasen APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/shell.html)
else()
    # Fallback when there is no assets folder next to the executable.
    target_compile_definitions(blubberblasen PRIVATE BB_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()

if(WIN32)
    # No console window in release builds
    set_target_properties(blubberblasen PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        target_link_options(blubberblasen PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()

# --- Tests (game library only, headless; they use the debug switches) -------------
enable_testing()
if(BB_DEBUG_SWITCHES)
    add_executable(test_game tests/test_game.c)
    target_link_libraries(test_game PRIVATE game)
    add_test(NAME game COMMAND test_game)
endif()

# map2c must reject broken settings files
foreach(bad bad_delay bad_key)
    add_test(NAME map2c_${bad}
             COMMAND map2c -o ${PROJECT_BINARY_DIR}/generated/${bad}.c ${PROJECT_SOURCE_DIR}/tests/map2c/${bad}.map)
    set_tests_properties(map2c_${bad} PROPERTIES WILL_FAIL TRUE)
endforeach()

# every room must be playable (reachability check)
add_test(NAME rooms_playable COMMAND mapview --check ${LEVEL_FILES})
add_test(NAME rooms_check_detects_problems COMMAND mapview --check ${PROJECT_SOURCE_DIR}/tests/rooms/unreachable.map)
set_tests_properties(rooms_check_detects_problems PROPERTIES WILL_FAIL TRUE)
