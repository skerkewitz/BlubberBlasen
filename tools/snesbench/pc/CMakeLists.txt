# The compiler benchmark (../bench.c) on the PC: prints the reference
# checksums the SNES builds must reproduce. Needs build/bench/gen from
# ../build.sh. Build and run from the repository root:
#   cmake -S tools/snesbench/pc -B build/bench/pc && cmake --build build/bench/pc --config Release
#   build/bench/pc/Release/benchpc > build/bench/pc.txt
cmake_minimum_required(VERSION 3.16)
project(benchpc C)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
file(GLOB GAME ${ROOT}/src/game/*.c)
add_executable(benchpc ${GAME} ${ROOT}/build/bench/gen/level_data.c ${ROOT}/build/bench/gen/gfx_data.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../bench.c)
target_include_directories(benchpc PRIVATE ${ROOT}/src ${ROOT}/build/bench/gen)
target_compile_definitions(benchpc PRIVATE BENCH_PC
    setMode=bb_setMode setBrightness=bb_setBrightness setScreenOn=bb_setScreenOn setScreenOff=bb_setScreenOff
    setColorEffect=bb_setColorEffect setColorIntensity=bb_setColorIntensity setPaletteColor=bb_setPaletteColor
    dmaCopyVram=bb_dmaCopyVram dmaCopyCGram=bb_dmaCopyCGram dmaClearVram=bb_dmaClearVram
    bgSetGfxPtr=bb_bgSetGfxPtr bgSetMapPtr=bb_bgSetMapPtr bgSetScroll=bb_bgSetScroll bgSetEnable=bb_bgSetEnable
    bgSetDisable=bb_bgSetDisable bgSetEnableSub=bb_bgSetEnableSub bgSetDisableSub=bb_bgSetDisableSub
    bgInitTileSet=bb_bgInitTileSet bgInitMapSet=bb_bgInitMapSet oamInit=bb_oamInit oamInitGfxSet=bb_oamInitGfxSet
    oamInitGfxAttr=bb_oamInitGfxAttr oamSet=bb_oamSet oamSetXY=bb_oamSetXY oamSetEx=bb_oamSetEx
    oamSetVisible=bb_oamSetVisible oamClear=bb_oamClear snesRegWrite=bb_snesRegWrite snesHdmaSet=bb_snesHdmaSet
    snesHdmaEnable=bb_snesHdmaEnable snesDmaFits=bb_snesDmaFits oamMemory=bb_oamMemory)
