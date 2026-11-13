# Host tools. They run on the build machine and feed generated sources into
# the game, or help with reviewing assets. Each tool lives in its own folder;
# code shared between tools lives in common/. See tools/README.md.

add_library(toolcommon STATIC common/mapfile.c)
target_include_directories(toolcommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)

add_executable(map2c map2c/map2c.c)
target_link_libraries(map2c PRIVATE toolcommon)

add_executable(mapview mapview/mapview.c)
target_link_libraries(mapview PRIVATE toolcommon)

# Contact sheet of all rooms: cmake --build build --target rooms_sheet
add_custom_target(rooms_sheet
    COMMAND mapview -o ${PROJECT_BINARY_DIR}/rooms.png ${LEVEL_FILES}
    DEPENDS mapview
    COMMENT "mapview: rendering all rooms to build/rooms.png"
    VERBATIM
)
