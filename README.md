# Blubber Blasen

A non-commercial fan remake of the arcade classic *Bubble Bobble*, with the long-term goal of running on real Super Nintendo hardware.

> This is a hobby project. It is not affiliated with or endorsed by Taito or Nintendo. No original assets are distributed in this repository.

## Roadmap

The project is built in three stages:

| Stage | Goal | Status |
|-------|------|--------|
| 1 | Playable prototype on PC (C99 + raylib) | In progress: first playable version (5 rounds, title, game over, high scores) |
| 2 | Split into game code and a "pseudo SNES" layer with a SNES-like API (sprites, tilemaps, palettes, memory layout) | Planned |
| 3 | Port to real SNES hardware (65816 C compiler and/or assembly) | Planned |

The game code is written from day one so that it survives all three stages with minimal rewriting. See the architecture decisions below.

## Architecture in short

- **Language:** C99.
- **Platform layer:** raylib, used **only** in `src/platform/`. Game code never includes `raylib.h` ([ADR-0001](docs/adr/0001-stage1-platform-c-raylib.md)).
- **SNES-shaped interface** between game and platform:
  - Input: one 16-bit button word per player per frame (SNES joypad bit layout).
  - Output: a shadow sprite table (OAM-like, 16 × 16 sprites) and two 8 × 8 tilemaps, rendered into a 256 × 224 framebuffer and scaled to the window. Layer 0 (level, below the sprites) is 32 × 64 tiles, scrolls vertically and has a window line so it never shows behind the HUD; layer 1 (text, above the sprites) is fixed.
  - Graphics (stage 1): 24-bit PNGs, magenta = transparent. A "palette" is a tint for now. See [assets/README.md](assets/README.md).
  - Audio: sound effects requested by ID (one bit per effect per tick).
  - Save RAM: 64 bytes (high score table) that the platform keeps in a file, like a battery-backed cartridge.
  - Time: the game is ticked exactly once per frame at 60 Hz.
- **Old-school game loop** ([ADR-0002](docs/adr/0002-oldschool-game-loop-no-ecs.md)):
  - One main loop, top-level state machine (`enum` + `switch`).
  - Fixed-size, statically allocated object tables (`players[2]`, `enemies[8]`, ...). No ECS, no heap objects.
  - Fixed-point math only (e.g. 8.8 in `int16_t`). No `float`/`double`, no `malloc`/`free` in game code.
  - All timers count frames. Levels and tables are `const` data ("ROM").
  - 16-bit LFSR random numbers; same seed + same input = same game (replays, regression tests).

## Repository layout

```
BlubberBlasen/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── adr/                 # Architecture Decision Records
├── src/
│   ├── game/                # Portable game code (no raylib, no float, no malloc)
│   │   ├── game.h           #   interface to the platform (pad in; OAM, tilemaps, sound, SRAM out)
│   │   ├── internal.h       #   shared state and object tables
│   │   ├── game.c           #   state machine, screens, HUD
│   │   ├── level.c          #   tile collision, airflow, auto-tiling
│   │   ├── player.c enemy.c bubble.c item.c
│   │   ├── hiscore.c text.c
│   │   └── level_data.h     #   level format shared with tools/map2c
│   └── platform/            # platform_raylib.c: window, input, rendering, audio, 60 Hz tick
├── assets/                  # Graphics, sound and level files (see assets/README.md)
├── tools/                   # Host tools, e.g. map2c: level files -> const C arrays (see tools/README.md)
└── tests/                   # Headless tests of the game library
```

Level data is generated at build time: CMake builds `map2c`, runs it over `assets/levels/*.map` and compiles the result (`build/generated/level_data.c`) into the game library.

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| C compiler | C99 capable | MSVC (Visual Studio 2022), MinGW-w64/GCC, or Clang |
| CMake | 3.20+ | |
| Git | any recent | |
| Ninja | optional | Faster builds |

raylib does **not** need to be installed. CMake fetches it automatically via `FetchContent` on the first configure (internet access required once).

### Windows

Option A – Visual Studio 2022:
1. Install Visual Studio 2022 with the workload **"Desktop development with C++"** (includes MSVC and CMake).
2. Install [Git for Windows](https://git-scm.com/download/win).

Option B – MSYS2 / MinGW-w64:
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja git
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build git \
    libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev \
    libglu1-mesa-dev libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev
```

### macOS

```bash
xcode-select --install
brew install cmake ninja git
```

## Getting started

```bash
git clone https://github.com/<your-account>/BlubberBlasen.git
cd BlubberBlasen

# Configure (fetches raylib on first run)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Run
./build/blubberblasen          # Linux / macOS / MinGW
.\build\Debug\blubberblasen.exe  # Visual Studio generator
```

With Visual Studio you can also simply open the folder (*File → Open → Folder*); it picks up `CMakeLists.txt` automatically.

The game finds its `assets/` folder next to the executable or, failing that, in the source tree it was built from. High scores are saved to `blubberblasen.sav` next to the executable. `F11` toggles full screen.

### Tests

```bash
ctest --test-dir build -C Debug --output-on-failure
```

`tests/test_game.c` runs the game library without a window: score arithmetic, level data checks, a determinism check (same seed and input must give the same frames) and a bot that plays 30 minutes of game time and must trap, defeat, die, clear rounds and reach game over. `BB_TRACE=600` prints the game state every 600 frames.

### Developer options

```bash
blubberblasen --autoplay --seed 7 --shot 300 --shot 900 --quit 1000
```

| Option | Effect |
|---|---|
| `--autoplay` | A simple built-in bot plays (sound off) |
| `--autoplay2` | Two bots play, one per player (sound off) |
| `--mute` | No sound |
| `--seed <n>` | Fixed random seed |
| `--round <n>` | New games start at round n |
| `--clear <tick>` | Remove all enemies at that tick, to see the end of a round |
| `--shot <tick>` | Save the framebuffer as `shot_<tick>.png` in the working directory (repeatable) |
| `--quit <tick>` | Exit after that many game ticks |

### Minimal `CMakeLists.txt`

If you are setting up the repository from scratch, this is the starting point:

```cmake
cmake_minimum_required(VERSION 3.20)
project(BlubberBlasen C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# Portable game code: must not depend on raylib
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)
add_library(game STATIC ${GAME_SOURCES})
target_include_directories(game PUBLIC src)

# Platform layer + executable
add_executable(blubberblasen src/platform/platform_raylib.c)
target_link_libraries(blubberblasen PRIVATE game raylib)

if(MSVC)
    target_compile_options(game PRIVATE /W4)
else()
    target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

Keeping `game` as a separate library without a raylib link dependency makes accidental raylib usage in game code fail at build time.

## Controls (default)

| SNES button | Keyboard player 1 | Keyboard player 2 | Gamepad |
|-------------|-------------------|-------------------|---------|
| D-Pad | `WASD` | Arrow keys | D-Pad / left stick |
| B (jump) | `K` | `.` | South button |
| Y (bubble) | `J` | `,` | West button |
| A | `C` | | East button |
| X | `V` | | North button |
| L / R | `Q` / `E` | | LB / RB |
| Start | `Enter` | `Right Ctrl` | Start |
| Select | `Right Shift` | | Back / Select |

The first gamepad belongs to player 1, the second to player 2. The game starts on the title screen: press Start to begin. The other player can join at any time with their Start button. A player with no lives left can continue the same way (the score is kept). During play, Start pauses.

Mappings live in `src/platform/platform_raylib.c` and may change. `Esc` quits, `F11` toggles full screen.

## How to play

Shoot bubbles (Y) at the enemies to trap them, then touch the trapped enemy to defeat it: it flies through the room and lands as food. Touching an enemy that is not in a bubble costs a life. A trapped enemy breaks free after a few seconds, angry and faster. Floating bubbles pop when you touch them, but if you fall onto one while holding jump (B), you bounce off it. Two players play at the same time and share the room. Every bubble a popped bubble touches pops too: defeat several enemies in one chain and each one's food is worth double the one before (1,000, 2,000, 4,000 ... 64,000). There are three enemy types: walkers, throwers that stop and throw rocks, and bouncers that fly diagonally through the platforms. The last enemy in a round gets angry, and when the round's time runs out all of them do ("HURRY UP!"); ten seconds later an invincible storm cloud comes for you. Losing a life calms everything down again. Some rounds send in special bubbles: water washes enemies away (all of them one combo), fire drops onto a platform and burns (it stuns players too), lightning flies across the room. A chain of three or more enemies earns EXTEND letter bubbles; collect E-X-T-E-N-D (shown on the side walls) for an extra life and an instant round skip. Hidden counters (bubbles blown, bubbles popped, jumps, distance walked, water bubbles popped) unlock power-ups that appear on a platform in a later round: three candies (bubbles fly farther, faster, more often), shoes (run faster) and an umbrella (skip 3 rounds). Candies and shoes are lost with a life. Every round also puts out one bonus food (500 to 2,000 points) after ten seconds. Clear all enemies to get to the next round, which scrolls in from below. You have 3 lives (the spare ones are shown in the bottom corners), plus one at 30,000 and one at 100,000 points; when the last player is out, a countdown gives you a few seconds to continue with Start. A good score gets you into the high score table.

## Coding rules for `src/game/`

These rules keep the code portable to the 65816. CI enforces the first three.

1. No `#include <raylib.h>` (or any platform header).
2. No `float` or `double`.
3. No `malloc`, `calloc`, `realloc`, `free`.
4. Prefer `int8_t`/`uint8_t`/`int16_t`/`uint16_t`; avoid 32-bit math, multiplication and division in hot paths (use shifts and lookup tables).
5. All game state is static; keep an eye on the RAM budget.
6. Timers count frames, never wall-clock time.

Quick local check:

```bash
grep -rnE 'raylib\.h|\bfloat\b|\bdouble\b|\b(malloc|calloc|realloc|free)\s*\(' src/game/ \
  && echo "Rule violation found" || echo "OK"
```

## Documentation

- [ADR-0001: Stage 1 prototype in C99 with raylib behind a thin platform layer](docs/adr/0001-stage1-platform-c-raylib.md)
- [ADR-0002: Old-school game loop and data layout, structured like the later SNES version](docs/adr/0002-oldschool-game-loop-no-ecs.md)
- Game Design Guide (arcade mechanics reference for the first playable version)

Project language is **English**: code, comments, commit messages, issues and documentation.

## License

To be decided. Source code will be published on GitHub. Original *Bubble Bobble* graphics, music and other assets are not part of this repository and must not be committed.
