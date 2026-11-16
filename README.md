# Blubber Blasen

A non-commercial fan remake of the arcade classic *Bubble Bobble*, with the long-term goal of running on real Super Nintendo hardware.

> This is a hobby project. It is not affiliated with or endorsed by Taito or Nintendo. No original assets are distributed in this repository.

## Roadmap

The project is built in three stages:

| Stage | Goal | Status |
|-------|------|--------|
| 1 | Playable prototype on PC (C99 + raylib) | In progress: playable version (100 rounds, boss, secret room, title, game over, high scores) |
| 2 | Split into game code and a "pseudo SNES" layer with a SNES-like API (sprites, tilemaps, palettes, memory layout) | Planned |
| 3 | Port to real SNES hardware (65816 C compiler and/or assembly) | Planned |

The game code is written from day one so that it survives all three stages with minimal rewriting. See the architecture decisions below.

### Version

The game has a version number `major.minor.revision`. It is set in one place, `GAME_VERSION` in `src/game/game.h`, and shown on the title screen. The roadmap stages above are separate from it.

## Architecture in short

- **Language:** C99.
- **Platform layer:** raylib, used **only** in `src/platform/`. Game code never includes `raylib.h` (ADR-0001, not written yet).
- **SNES-shaped interface** between game and platform:
  - Input: one 16-bit button word per player per frame (SNES joypad bit layout).
  - Output: a shadow sprite table (OAM-like, 16 × 16 sprites), two colour windows (a left and right edge per scanline with a colour added inside, like SNES windows with colour math) and two 8 × 8 tilemaps, rendered into a 256 × 224 framebuffer and scaled to the window. Layer 0 (level, below the sprites) is 32 × 64 tiles, scrolls vertically and has a window line so it never shows behind the HUD; layer 1 (text, above the sprites) is fixed.
  - Graphics (stage 1): 24-bit PNGs, magenta = transparent. A "palette" is a tint for now. See [assets/README.md](assets/README.md).
  - Audio: sound effects requested by ID (one bit per effect per tick) and one music track ID; the platform streams the track.
  - Save RAM: 128 bytes (high score table in the first 64, options from byte 64) that the platform keeps in a file, like a battery-backed cartridge.
  - Time: the game is ticked exactly once per frame at 60 Hz.
- **Old-school game loop** (ADR-0002, not written yet):
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
│   │   ├── player.c enemy.c bubble.c item.c boss.c effect.c
│   │   ├── cpu.c            #   CPU partner (navigation, look-ahead dodging)
│   │   ├── debug.c          #   developer switches (BB_DEBUG builds only)
│   │   ├── hiscore.c text.c
│   │   ├── fixed.h          #   8.8 fixed-point helpers
│   │   └── level_data.h     #   level format shared with tools/map2c
│   ├── platform/            # platform_raylib.c: window, input, rendering, audio, 60 Hz tick
│   └── replay/              # recordings of games (development builds, PC only; uses stdio and malloc)
├── assets/                  # Graphics, sound and level files (see assets/README.md)
├── tools/                   # Host tools: map2c (levels -> const C arrays), mapview (room previews); see tools/README.md
└── tests/                   # Headless tests of the game library; replays/: stored recordings
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

`rungame.bat` (Windows) and `rungame.sh` (Linux, macOS, WSL) start the built game from the project folder and pass all arguments on, for example `rungame --replay build\Debug\recordings\<file>.bbr`. They take the Debug build if there is one, else Release. `./replay.sh <recording> [options]` plays a recording by its file name alone, for example `./replay.sh 20261115-165418-marked --jump 9300`; without arguments it lists the newest recordings.

With Visual Studio you can also simply open the folder (*File → Open → Folder*); it picks up `CMakeLists.txt` automatically.

The game finds its `assets/` folder next to the executable or, failing that, in the source tree it was built from. High scores are saved to `blubberblasen.sav` next to the executable. `F11` toggles full screen.

The picture goes through a retro TV filter when it is scaled into the window (platform layer only, the 256x224 framebuffer and `--shot` stay clean): SNES 15-bit colour, composite-style colour bleed, scanlines, an RGB aperture-grille mask (RGB phosphor stripes on the tube), a soft glow, a light vignette and curved glass (barrel distortion; the edges bulge outward and nothing of the picture is cut off), softer edges with slight colour fringes, like a real tube. Scanlines and mask fade out below 3x scale. `Tab` toggles the filter, `--no-crt` starts without it. [docs/RETRO-EFFECT.md](docs/RETRO-EFFECT.md) explains each effect and how to adjust it.

### Web build (WebAssembly)

Needs Emscripten (Arch: `sudo pacman -Syu emscripten`, then a new shell or `source /etc/profile.d/emscripten.sh`):

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBB_DEBUG_SWITCHES=OFF
cmake --build build-web -j8
cd build-web && python3 -m http.server 8000      # then open http://localhost:8000/blubberblasen.html
```

The result is `blubberblasen.html`, `.js`, `.wasm` and `.data` (the assets, about 13 MB, mostly music). The page needs a web server (opening the file directly does not work), shows a "click to start" layer (browsers only allow sound and keyboard focus after a click) and keeps high scores and options in the browser (IndexedDB). The page is `web/shell.html`. `#nostart` at the end of the address skips the click layer and runs the game on a plain timer, for automated checks in headless browsers. The tools (`map2c`, `mapview`) are compiled to JavaScript in this build and run under Node.js during the build.

### Tests

```bash
ctest --test-dir build -C Debug --output-on-failure
```

`tests/test_game.c` runs the game library without a window: score arithmetic, level data checks, a determinism check (same seed and input must give the same frames), a random bot that plays 30 minutes of game time, unit tests for the mechanics, and a room test in which the CPU partner plays both players in every round from 1 to 99, in Arcade and in Plus with co-op rescue, with endless lives. A room fails only if no try out of 15 clears it within 3 minutes of game time; each try uses its own seed, because the same seed would play the same game again. Deaths are printed with their causes as information, not checked: they depend on the CPU partner and on chance as much as on the room. Arcade rounds 16 and 34 are skipped: their start pocket can only be left by bouncing on a bubble, which the CPU partner cannot do yet. `BB_TRACE=600` prints the game state every 600 frames; `BB_DEATHS=1` prints the circumstances of the first CPU deaths in Arcade; `BB_ROOMS=1` prints what is left in a room after a try that did not clear it.

`tests/test_replay.c` records a game, plays the recording back and checks that it gives the same game, and that a changed input or seed is noticed. It also plays every recording in `tests/replays/` (one test each, `replay_<name>`); see *Recordings*.

### Developer options

```bash
blubberblasen --autoplay --seed 7 --shot 300 --shot 900 --quit 1000
```

| Option | Effect |
|---|---|
| `--autoplay` | A simple built-in bot plays (sound off) |
| `--autoplay2` | Two bots play, one per player (sound off) |
| `--mute` | No sound |
| `--no-crt` | Start without the retro TV filter (`Tab` toggles it) |
| `--seed <n>` | Fixed random seed |
| `--round <n>` | New games start at round n |
| `--clear <tick>` | Remove all enemies at that tick, to see the end of a round |
| `--skip-intro` | No story pages after the title (`--autoplay` and `--round` imply it) |
| `--intro` | Show the story pages even with `--autoplay` or `--round` |
| `--ending <happy\|sad>` | Start with that ending, to look at it |
| `--invincible`, `--infinite-lives`, `--hitboxes`, `--air`, `--cpu-view` | Switch on those debug switches |
| `--plus`, `--rescue`, `--cpu2` | Set those options for this run (not saved) |
| `--options` | Start on the options screen |
| `--replay <file>` | Play a recording instead of the keyboard (see *Recordings*) |
| `--jump <tick>` | With `--replay`: run fast to that tick, then pause |
| `--record`, `--no-record` | Record a bot run too (normally only runs without `--autoplay` are recorded) / don't record this run |

All developer options and debug switches exist only in builds with the CMake option `BB_DEBUG_SWITCHES` (on by default). Public builds configure with `-DBB_DEBUG_SWITCHES=OFF` and contain none of it; the tests need it on.

### Debug keys (development builds)

| Key | Effect |
|---|---|
| `F1` | CPU partner view: predicted danger positions (red dots, brighter = sooner) and the action it chose |
| `F2` | Invincibility |
| `F3` | Infinite lives |
| `F4` | Show hitboxes |
| `F5` | Show air currents |
| `F6` | No sticky bubbles: floating bubbles don't cling together (to compare) |
| `F7` / `F8` | Pause and step one frame at a time / next frame (`Shift`+`F8`: one frame back, `Ctrl`+`Shift`+`F8`: one second back, while replaying) |
| `F9` | Slow motion (quarter speed) |
| `F10` | Spawn the next item or special bubble in front of player 1 |
| `F12` | Vacuum walls: walls stop bubbles inside a vacuum (to compare with the default pass-through) |
| `Backspace` | Mark this moment in the recording ("that was weird!"); while replaying: jump to 3 s before the next mark |
| `--shot <tick>` | Save the framebuffer as `shot_<tick>.png` in the working directory (repeatable) |
| `--shot-crt <n>` | Also save each shot through the retro TV filter at n x scale, as `shot_<tick>_crt.png` |
| `--quit <tick>` | Exit after that many game ticks |

### Recordings

Step by step, with all debug keys: [docs/DEBUG-REPLAY.md](docs/DEBUG-REPLAY.md).

The game is deterministic: the same start and the same buttons give the same game. Development builds on the PC use this to record every run, so a bug that has just happened can be watched again, frame by frame.

- Each run writes `recordings/<date>-<time>.bbr` next to the executable. The file holds the start (game version, seed, save RAM, developer options) and then only the buttons of both players when they change, the debug keys that change the game (`F2`, `F3`, `F6`, `F10`, `F12`), and every second a checksum of the picture. That is a few kilobytes per minute, and the file is plain text.
- Saw something odd? Press `Backspace`. The moment is marked, and the recording is kept as `...-marked.bbr`. Of the unmarked recordings only the newest 20 are kept.
- `blubberblasen --replay recordings/<file>.bbr` plays it back. The keyboard does nothing then; the window shows the tick and the buttons each player is holding. The game pauses 3 s before and at each mark (`Backspace` jumps to the next one); `F7`, `F8` and `F9` pause, step and slow down as usual, `Shift`+`F8` steps one frame back and `Ctrl`+`Shift`+`F8` one second (the replay runs again from the start, fast, to the frame before), and the switches that only show something (`F1`, `F4`, `F5`) work too. `--jump <tick>` runs fast to a tick first.
- A recording only fits the game it was made with. After a change to the rules (a speed, a timer, a new mechanic) an old recording drifts apart: the checksums stop matching, and the replay says at which tick. Bug recordings are short-lived anyway. The recordings in `tests/replays/` are tests: when one fails after an intended change, record it again (`--autoplay --record --plus --seed 42 --quit 3000` made `plus_bot_seed42.bbr`) and copy it over, or delete it.

Recordings exist only in development builds (`BB_DEBUG_SWITCHES`) and not in the web build.

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
| A (vacuum, Plus mode) | `U` or `C` | `N` | East button (Xbox B, PlayStation ○) |
| X (push bubble, Plus mode) | `H` or `V` | `M` | North button (Xbox Y, PlayStation △) |
| L / R (dash left / right, Plus mode) | `Q` / `E` | | |
| Dash (held direction, else facing) | `L` | `/` | LB or RB |
| Start | `Enter` | `Right Ctrl` | Start |
| Select | `Shift` (either) | | Back / Select |

On the title screen, Select opens the options: **mode** (Arcade, or Plus with dash, push bubble and vacuum), **co-op rescue** (on/off) and **player 2** (human or CPU partner). They are saved with the high scores.

The first gamepad belongs to player 1, the second to player 2. The game starts on the title screen: press Start to begin. The other player can join at any time with their Start button. A player with no lives left can continue the same way (the score is kept). During play, Start pauses.

On a phone or tablet the web page shows touch controls for player 1: a D-pad, Jump, Bubble, Push, Suck (vacuum), Dash, Start and Options. They are part of `web/shell.html` and only send the matching keyboard keys, so the game itself has no touch code. Player 2 needs a keyboard, a gamepad or the CPU partner.

Mappings live in `src/platform/platform_raylib.c` and may change. `Esc` quits, `F11` toggles full screen, `Tab` toggles the retro TV filter.

## How to play

Shoot bubbles (Y) at the enemies to trap them, then touch the trapped enemy to defeat it: it flies through the room and lands as food. Touching an enemy that is not in a bubble costs a life. A trapped enemy breaks free after a few seconds, angry and faster. Floating bubbles pop when you touch them, but if you fall onto one while holding jump (B), you bounce off it. Two players play at the same time and share the room. Every bubble a popped bubble touches pops too: defeat several enemies in one chain and each one's food is worth double the one before (1,000, 2,000, 4,000 ... 64,000). There are seven enemy types, introduced over the rounds: walkers, throwers that stop and throw rocks, bouncers that fly diagonally through the platforms, flutters that fly around and change direction often, hoppers on springs, spitters that run fast and spit fireballs, and droppers that patrol and shoot straight down. The last enemy in a round gets angry, and when the round's time runs out all of them do ("HURRY UP!"); ten seconds later an invincible storm cloud comes for you. Losing a life calms everything down again. Some rounds send in special bubbles: water washes enemies away (all of them one combo), fire drops onto a platform and burns (it stuns players too), lightning flies across the room. Three or more enemies defeated in one go (a chain, a dash, one water, fire or lightning bubble, an item) are a multikill: a special sound, and letter bubbles for whoever did it (1 for 3 enemies, 2 for 4, 3 for 5 or more), always letters that player still misses. Whoever pops a letter gets it; one you already have is wasted. Collect B-L-A-S-E-N (shown on the side walls) for an extra life; the round ends at once, with a message saying so. Hidden counters (bubbles blown, bubbles popped, jumps, distance walked, water bubbles popped) unlock power-ups that appear on a platform in a later round: three candies (bubbles fly farther, faster, more often), shoes (run faster) and an umbrella (skip 3 rounds). Candies and shoes are lost with a life. Every round also puts out one bonus food (500 to 2,000 points) after ten seconds. Clear all enemies to get to the next round, which scrolls in from below. There are 100 rounds on ten factory floors (see [the story](docs/story.md)); round 100 is the Grumble Boiler: bubbles bounce off it, but in that round every bubble you blow turns into a lightning bubble, and only lightning hurts it. Beat it, pop the giant bubble it ends up in, and the ending depends on whether two players made it. Reach round 20, 40, 50 or 60 without anybody losing a life and a door appears on the floor (press Up in front of it): it leads to a secret room full of gems with a clue, and in round 50 to round 70. More items are unlocked by hidden counters: the Soap Star and the Rinse Wave turn every enemy into a gem, the Steam Whistle freezes them (touch a frozen enemy to defeat it), the Fire Glove shoots fireballs for one round and the Bubble Ring gives 100 points per bubble. You have 3 lives (the spare ones are shown in the bottom corners), plus one at 30,000 and one at 100,000 points; when the last player is out, a countdown gives you a few seconds to continue with Start. A good score gets you into the high score table.

**Plus mode:** L / R dash about seven tiles left or right without turning around (the dash key, `L` on the keyboard or either shoulder button on a gamepad, dashes the way you hold or, if you hold nothing, the way you face). A dash pops every bubble it sweeps through, passes through inner walls and platforms, never ends inside a wall, keeps you invulnerable for the whole dash and half a second after it (you blink), and a press up to six frames too early (for example just before landing) still counts. The dash is free: once per jump, and one second after a dash before the next; a short yellow glow says it is ready again. Everything one dash pops counts as one chain, so two enemies in one dash are a combo even if their bubbles don't touch. A dash through a Riveter (the enemy with the steel plate, from round 11) knocks its plate off; bubbles fired at the plate burst. Some rooms hide an enemy in a pocket behind walls that only a dash gets through.

**Walls and aiming** (Plus mode): a fired bubble stops at walls and platforms and starts floating in front of them; blown into a wall at point-blank, it pops at once. Hold Up to shoot a bubble or a push bubble upward. Only a push gets a bubble through a wall: a push bubble shot upward always shoves the bubble it hits up, through the platform above, and an empty shoved bubble traps the first enemy it meets. (In Arcade mode bubbles keep passing inner walls, and Up does not aim.)

**Push bubble** (Plus mode): X, the top face button (Y on an Xbox pad, △ on PlayStation), blows a small, fast bubble that traps nothing. It works like a cue ball: it bursts on the first floating bubble it meets and shoves it, empty or with an enemy inside. A hit on the bubble's upper third pushes it down, on the lower third up, in the middle along the push bubble's way. The shoved bubble slides, ignoring the air, slows down and stops at the walls; an empty one traps the first enemy it slides into, like a fired bubble. A special bubble (water, fire, lightning, a BLASEN letter) is set off as if you touched it. The push bubble destroys enemy shots and bursts without effect on enemies, players and the boss; it collects nothing. Each player has one at a time.

**Vacuum** (Plus mode): A, the right face button (B on an Xbox pad, ○ on PlayStation), puts a vacuum where you are; a softly pulsing glow shows exactly how far it reaches. For 1.5 seconds the air inside a circle of 5 tiles around it points to its centre, so floating bubbles (empty, with an enemy, special, letters) and a partner's rescue bubble gather there, faster the closer they get, and form a cluster ready for a chain. Then the room's air takes over again. The vacuum stays where you placed it, also when you press A mid-jump; pressing again moves it. Bubbles pass through walls on their way in. It moves no enemies or items, pops nothing by itself.

**Co-op rescue** (two players): an enemy that touches you traps you in a rescue bubble instead of killing you. It drifts with the air; your partner frees you by touching it, with no life lost. If it runs out, you lose a life. Shots, the storm cloud and the boss still kill.

**CPU partner:** with player 2 set to CPU, a computer partner plays player 2. It frees you from rescue bubbles, avoids danger, pops trapped enemies (preferably several at once), hunts enemies, fetches BLASEN letters it still needs (and doesn't dash through ones it has) and stays near you. To avoid danger it looks ahead every frame: it predicts where enemies, shots, storm clouds and the boss will be over the next half second, tries out its own moves (stay, walk, jump, dash, turn and shoot) and only follows its goal if that stays safe; it leaves the food to you and earns no score. Press Start on controller 2 to take over from it; once it has lost its last life, Start on controller 2 lets a human join in its place. A game finished with the CPU partner counts as a two-player game.

## Coding rules for `src/game/`

These rules keep the code portable to the 65816. There is no CI yet, so nothing enforces them automatically: run the check below before committing.

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

- ADR-0001: Stage 1 prototype in C99 with raylib behind a thin platform layer (not written yet)
- ADR-0002: Old-school game loop and data layout, structured like the later SNES version (not written yet)
- [ADR-0003](docs/adr/0003-multikills-and-blasen.md): multikills and BLASEN letters as a bonus
- [Game Design Guide](docs/Blubber%20Blasen%20–%20Game%20Design%20Guide.md): the game we are building, Plus and Classic mode, and where it differs from the original
- [Bubble Bobble (Arcade) – Game Design Reference Guide](docs/Bubble%20Bobble%20(Arcade)%20–%20Game%20Design%20Reference%20Guide.md): how the arcade original works, as reference
- [Story](docs/story.md): the Blubber Factory, heroes, floors, secret rooms, boss and endings

Project language is **English**: code, comments, commit messages, issues and documentation.

## License

To be decided. Source code will be published on GitHub. Original *Bubble Bobble* graphics, music and other assets are not part of this repository and must not be committed.
