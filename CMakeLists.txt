cmake_minimum_required(VERSION 3.20)
project(BlubberBlasen C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- raylib (platform layer only) -------------------------------------------
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

if(MSVC)
    target_compile_options(game PRIVATE /W4)
else()
    target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Platform layer + executable --------------------------------------------
add_executable(blubberblasen src/platform/platform_raylib.c)
target_link_libraries(blubberblasen PRIVATE game raylib)
# Fallback when there is no assets folder next to the executable.
target_compile_definitions(blubberblasen PRIVATE BB_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")

if(WIN32)
    # No console window in release builds
    set_target_properties(blubberblasen PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        target_link_options(blubberblasen PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()

# --- Tests (game library only, headless) ----------------------------------------
enable_testing()
add_executable(test_game tests/test_game.c)
target_link_libraries(test_game PRIVATE game)
add_test(NAME game COMMAND test_game)

# map2c must reject broken settings files
foreach(bad bad_delay bad_key)
    add_test(NAME map2c_${bad}
             COMMAND map2c -o ${PROJECT_BINARY_DIR}/generated/${bad}.c ${PROJECT_SOURCE_DIR}/tests/map2c/${bad}.map)
    set_tests_properties(map2c_${bad} PROPERTIES WILL_FAIL TRUE)
endforeach()
