# Host tools. They run on the build machine and feed generated sources into
# the game. Each tool lives in its own folder; see tools/README.md.

add_executable(map2c map2c/map2c.c)
target_include_directories(map2c PRIVATE ${PROJECT_SOURCE_DIR}/src)
