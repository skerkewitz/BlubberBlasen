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

# --- superdevkit: the console API and Rachael, the SNES rebuilt in C (ADR-0007) ------
# SUPERDEVKIT_HOME is a superdevkit home (the folder with bin/dampf and lib/). From
# the environment, or -DSUPERDEVKIT_HOME=...; for MSVC on Windows a WSL home is
# reached as \\wsl.localhost\<distribution>\... (README, "Building").
set(SUPERDEVKIT_HOME "$ENV{SUPERDEVKIT_HOME}" CACHE PATH "superdevkit home (contains bin/dampf and lib/)")
if(NOT EXISTS "${SUPERDEVKIT_HOME}/lib/include/sdevkit/snes.h")
    message(FATAL_ERROR "SUPERDEVKIT_HOME (\"${SUPERDEVKIT_HOME}\") is not a superdevkit home: set it to the folder with bin/dampf and lib/")
endif()
file(TO_CMAKE_PATH "${SUPERDEVKIT_HOME}" SDK)
# The game sees only the console API (lib/include); the frontend and the tests
# also reach Rachael's own interface (rachael.h) through RACHAEL_INCLUDE.
add_library(rachael STATIC ${SDK}/lib/rachael/ppu.c ${SDK}/lib/rachael/render.c)
target_include_directories(rachael PUBLIC ${SDK}/lib/include)
set(RACHAEL_INCLUDE ${SDK}/lib/rachael)
set_target_properties(rachael PROPERTIES C_EXTENSIONS OFF)

# --- Generated graphics: assets/gfx -> gfx_data.c (ADR-0006) -------------------
# gfx2c checks the SNES limits and fails the build when the art breaks one.
set(GFX_DATA_C ${PROJECT_BINARY_DIR}/generated/gfx_data.c)
set(GFX_DATA_H ${PROJECT_BINARY_DIR}/generated/gfx_data.h)
file(GLOB GFX_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/gfx/*.png)
add_custom_command(
    OUTPUT  ${GFX_DATA_C} ${GFX_DATA_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/generated
    COMMAND gfx2c -o ${GFX_DATA_C} -H ${GFX_DATA_H} ${PROJECT_SOURCE_DIR}/assets/gfx/gfx.txt ${LEVEL_FILES}
    DEPENDS gfx2c ${PROJECT_SOURCE_DIR}/assets/gfx/gfx.txt ${GFX_FILES} ${LEVEL_FILES}
    COMMENT "gfx2c: converting graphics"
    VERBATIM
)
add_library(gfxdata STATIC ${GFX_DATA_C})
target_include_directories(gfxdata PUBLIC ${PROJECT_BINARY_DIR}/generated)
set_target_properties(gfxdata PROPERTIES C_EXTENSIONS OFF)

# --- Portable game code: must not depend on raylib ---------------------------
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)
add_library(game STATIC ${GAME_SOURCES} ${LEVEL_DATA_C})
target_include_directories(game PUBLIC src)
target_link_libraries(game PUBLIC rachael gfxdata)   # the game draws through the console API, on Rachael (ADR-0007)
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

# --- The game as on the SNES (no debug switches) and its check (tools/snescheck) ------
# snescheck plays the game on Rachael as the kit's main loop plays the ROM and writes a
# recording with the checksum of every tick; <home>/dev/replay.sh compares the ROM with it.
if(NOT EMSCRIPTEN)
    add_library(game_console STATIC ${GAME_SOURCES} ${LEVEL_DATA_C})
    target_include_directories(game_console PUBLIC src)
    target_link_libraries(game_console PUBLIC rachael gfxdata)
    set_target_properties(game_console PROPERTIES C_EXTENSIONS OFF)
    add_executable(snescheck tools/snescheck/snescheck.c)
    target_link_libraries(snescheck PRIVATE game_console)
    target_include_directories(snescheck PRIVATE ${RACHAEL_INCLUDE})
    if(MSVC)
        target_compile_definitions(snescheck PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

# --- Recordings (src/replay): development builds only, host code ------------------
if(BB_DEBUG_SWITCHES)
    add_library(replay STATIC src/replay/replay.c)
    target_link_libraries(replay PUBLIC game)
    if(MSVC)
        target_compile_definitions(replay PUBLIC _CRT_SECURE_NO_WARNINGS)
        target_compile_options(replay PRIVATE /W4)
    else()
        target_compile_options(replay PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# --- Platform layer + executable --------------------------------------------
add_executable(blubberblasen src/platform/platform_raylib.c)
target_link_libraries(blubberblasen PRIVATE game raylib)
target_include_directories(blubberblasen PRIVATE ${RACHAEL_INCLUDE})
if(BB_DEBUG_SWITCHES AND NOT EMSCRIPTEN)
    # recording every run, --replay (see README, "Recordings")
    target_link_libraries(blubberblasen PRIVATE replay)
    target_compile_definitions(blubberblasen PRIVATE BB_REPLAY=1)
endif()
# --- Debug view (Dear ImGui, ADR-0005): desktop debug builds only -----------------
# C++ is confined to this one library; the game code stays C99.
if(BB_DEBUG_SWITCHES AND NOT EMSCRIPTEN)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.7-docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_Declare(
        rlimgui
        GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
        GIT_TAG        41cc11a2db4b0262d39ba7c27ed3c6aba58b95c5   # branch RL55ImGui19207: raylib 5.5, ImGui 1.92.7
    )
    FetchContent_MakeAvailable(imgui rlimgui)   # neither has a CMakeLists.txt: sources only
    add_library(debug_ui STATIC
        src/platform/debug_ui.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
    target_include_directories(debug_ui PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
    target_include_directories(debug_ui PRIVATE ${RACHAEL_INCLUDE})
    target_link_libraries(debug_ui PUBLIC game raylib)
    target_compile_definitions(debug_ui PUBLIC BB_DEBUG_UI=1)
    target_link_libraries(blubberblasen PRIVATE debug_ui)
endif()

if(EMSCRIPTEN)
    # Web: a page with the game, the assets packed into blubberblasen.data at /assets.
    # Levels and graphics are compiled in, so their sources stay out; so does the unused ambient track.
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
        "SHELL:--exclude-file *.png"   # graphics are compiled in (gfx2c)
        "SHELL:--exclude-file *guarded-stillness*"
        "SHELL:--shell-file ${PROJECT_SOURCE_DIR}/web/shell.html")
    set_property(TARGET blubberblasen APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/shell.html)
    # The packed assets live in blubberblasen.data, which is written at link
    # time: a changed (or added) asset must relink, or the old .data ships.
    file(GLOB_RECURSE BB_WEB_ASSETS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/*)
    set_property(TARGET blubberblasen APPEND PROPERTY LINK_DEPENDS ${BB_WEB_ASSETS})
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
    target_include_directories(test_game PRIVATE ${RACHAEL_INCLUDE})
    add_test(NAME game COMMAND test_game)


    add_executable(test_gfx tests/test_gfx.c)
    target_link_libraries(test_gfx PRIVATE rachael gfxdata toolcommon)
    target_include_directories(test_gfx PRIVATE ${RACHAEL_INCLUDE})
    add_test(NAME gfx COMMAND test_gfx ${PROJECT_SOURCE_DIR}/assets/gfx)

    add_executable(test_replay tests/test_replay.c)
    target_link_libraries(test_replay PRIVATE replay)
    add_test(NAME replay COMMAND test_replay)
    # Stored recordings must still play the same game. One fails after a
    # change to the game's rules: record it again (see README, "Recordings").
    file(GLOB BB_REPLAYS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/replays/*.bbr)
    foreach(rec ${BB_REPLAYS})
        get_filename_component(rec_name ${rec} NAME_WE)
        add_test(NAME replay_${rec_name} COMMAND test_replay ${rec})
    endforeach()
endif()

# map2c must reject broken settings files, for the right reason: the tests
# match the expected message, so a missing fixture ("cannot open file")
# fails instead of passing like any other non-zero exit would.
set(BB_BAD_bad_delay "bad_delay\\.txt:2: fewer delay values than enemies")
set(BB_BAD_bad_key   "bad_key\\.txt:1: unknown setting")
foreach(bad bad_delay bad_key)
    add_test(NAME map2c_${bad}
             COMMAND map2c -o ${PROJECT_BINARY_DIR}/generated/${bad}.c ${PROJECT_SOURCE_DIR}/tests/map2c/${bad}.map)
    set_tests_properties(map2c_${bad} PROPERTIES PASS_REGULAR_EXPRESSION "${BB_BAD_${bad}}")
endforeach()

# gfx2c must reject art that breaks a SNES limit, with the reason (ADR-0004).
set(BB_GFX_bad_grid    "pixel 0,0: colour 0D0D0D is not a SNES colour")
set(BB_GFX_bad_colours "tileset_16\\.png: more than 15 colours")
set(BB_GFX_bad_sheet   "need more than 128 cells")
foreach(bad bad_grid bad_colours bad_sheet)
    add_test(NAME gfx2c_${bad}
             COMMAND gfx2c -o ${PROJECT_BINARY_DIR}/generated/${bad}.c -H ${PROJECT_BINARY_DIR}/generated/${bad}.h
                     ${PROJECT_SOURCE_DIR}/tests/gfx2c/${bad}.txt)
    set_tests_properties(gfx2c_${bad} PROPERTIES PASS_REGULAR_EXPRESSION "${BB_GFX_${bad}}")
endforeach()

# every room must be playable (reachability check)
add_test(NAME rooms_playable COMMAND mapview --check ${LEVEL_FILES})
add_test(NAME rooms_check_detects_problems COMMAND mapview --check ${PROJECT_SOURCE_DIR}/tests/rooms/unreachable.map)
set_tests_properties(rooms_check_detects_problems PROPERTIES
                     PASS_REGULAR_EXPRESSION "enemy 0 \\(x=15 y=13\\) cannot be reached")
