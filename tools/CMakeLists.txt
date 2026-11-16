# Host tools. They run on the build machine and feed generated sources into
# the game, or help with reviewing assets. Each tool lives in its own folder;
# code shared between tools lives in common/. See tools/README.md.

add_library(toolcommon STATIC common/mapfile.c common/reach.c common/pngread.c)
target_include_directories(toolcommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)

add_executable(map2c map2c/map2c.c)
target_link_libraries(map2c PRIVATE toolcommon)

add_executable(mapview mapview/mapview.c)
target_link_libraries(mapview PRIVATE toolcommon)

add_executable(gfx2c gfx2c/gfx2c.c)
target_link_libraries(gfx2c PRIVATE toolcommon)

# In a web build the tools are compiled to JavaScript too and run under
# Node.js during the build (CMake's cross-compiling emulator); they need
# the real file system for that.
if(EMSCRIPTEN)
    foreach(tool map2c mapview gfx2c)
        target_link_options(${tool} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
    endforeach()
endif()

# Contact sheet of all rooms: cmake --build build --target rooms_sheet
add_custom_target(rooms_sheet
    COMMAND mapview -o ${PROJECT_BINARY_DIR}/rooms.png ${LEVEL_FILES}
    DEPENDS mapview
    COMMENT "mapview: rendering all rooms to build/rooms.png"
    VERBATIM
)
