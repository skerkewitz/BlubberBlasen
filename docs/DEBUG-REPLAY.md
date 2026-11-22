# Recording and replaying a game for debugging

Something went wrong in a game: an enemy died for no reason, a bubble vanished, a player was hit by
nothing. This page explains how to catch that moment and watch it again, frame by frame.

It works because the game is deterministic. The same start and the same buttons always give the
same game. A recording stores only those two things, so a replay shows exactly what happened, down
to the frame.

Everything here needs a **development build** (CMake option `BB_DEBUG_SWITCHES`, on by default) on
the **PC**. Release builds and the web version have no recordings and no debug keys.

## 1. Record

You don't need to do anything to start recording. Every run of a development build is recorded.

1. Start the game as usual, for example `./rungame.sh` (or `rungame.bat` on Windows).
2. Play.
3. When something odd happens, press **Backspace** right away. This marks the moment, and the
   window shows `MARKED AT TICK <n>`. You can mark more than one moment in the same run.
4. Quit the game (Esc or close the window).

The recording is in the `recordings` folder next to the executable, normally
`build/Debug/recordings/`. The file name is the date and time the game started:

- `20261115-165418.bbr`: a run without marks. Only the newest 20 of these are kept.
- `20261115-165418-marked.bbr`: a run with at least one mark. These are never deleted
  automatically.

Some notes:

- If the game crashes, the recording is still there, up to about the last second. A mark is
  written to disk the moment you press Backspace.
- Runs with `--autoplay` are not recorded unless you add `--record`. `--no-record` switches recording
  off for one run.
- The debug keys that change the game (F2, F3, F6, F10, F12) are recorded too, so a replay
  stays correct even if you used them. So are the same actions in the debug view, and its
  "Go to round" button.
- A file is a few kilobytes per minute and plain text, so it can be attached to a bug report or sent
  to someone else.

## 2. Replay

```sh
./replay.sh 20261115-165418-marked.bbr
```

You only need the file name; `replay.sh` looks in the recordings folders and the `.bbr` can be left
out. Without any argument it lists the newest recordings. Further options are passed on to the
game, for example:

```sh
./replay.sh 20261115-165418-marked --jump 9300        # run fast to tick 9300, then pause
./replay.sh 20261115-165418-marked --hitboxes --mute  # show hitboxes, no sound
```

On Windows, or with a recording somewhere else, use the game directly:

```
rungame.bat --replay build\Debug\recordings\20261115-165418-marked.bbr
```

### What you see

- At the bottom of the window: `REPLAY <tick> / <last tick>`, and for each player the buttons they
  are holding at this moment (`< ^ v >`, JUMP, BUBBLE, PUSH, VACUUM, L, R, START, SELECT). Held
  buttons are yellow.
- The game pauses on its own 3 seconds before each mark and again at the mark itself.
- At the end of the recording the game pauses with `END OF THE RECORDING`.

### Keys while replaying

| Key | Effect |
|---|---|
| `Backspace` | Jump to 3 seconds before the next mark (runs fast, then pauses) |
| `F7` | Pause / go on |
| `F8` | One frame forward (while paused) |
| `Shift`+`F8` | One frame back (while paused) |
| `Ctrl`+`Shift`+`F8` | One second (60 frames) back (while paused) |
| `F9` | Slow motion (quarter speed) on / off |
| `F1`, `F4`, `F5` | CPU view, hitboxes, air currents (they only show something, so they are allowed) |
| `Tab`, `F11` | Retro TV filter, full screen |
| Esc | Quit |

The keyboard and the gamepad don't control the players during a replay. The keys that would change
the game (F2, F3, F6, F10, F12) are refused with `NOT WHILE REPLAYING`, because they would make the
replay go wrong.

Going back works by playing the recording again from the start, fast, up to the frame wanted. A
few minutes in, that is instant; the replay runs about 20,000 ticks a second, so an hour in, one
step back takes about 10 seconds. To go back further than a few seconds, start the replay again
with `--jump`.

### Finding the moment

1. Note the tick of the mark: the game prints `REPLAY: mark at tick 9485` to the console when the
   replay starts, and the file has a line `9485 mark`.
2. The mark is where you *reacted*, usually a second or two after the event. Start a little earlier:
   `./replay.sh <file> --jump 9300`.
3. Step with F8 and back with Shift+F8 (Ctrl+Shift+F8: a second back), with hitboxes (F4) on if it is about collisions.

### Screenshots from a replay

Replays work with the screenshot options, without a window to watch:

```sh
./replay.sh 20261115-165418-marked --jump 9400 --shot 9420 --shot 9450 --shot 9480 --quit 9490
```

This writes `shot_9420.png` and the others to the project folder. With `--quit` the replay never
pauses on its own and ends at the end of the recording.

## 3. When a replay goes wrong

A recording only fits the game it was made with. If the rules changed after recording (a speed, a
timer, a new mechanic, a changed level), the replay drifts apart: the players press the same buttons,
but the game reacts differently, and from then on it shows something that never happened.

The game notices this. Every second the recording holds a checksum of the picture, and the replay
compares it. When they differ, the window shows `DRIFTED APART AT <tick>` in red and the console
prints a warning. Everything before that tick is correct; everything after it isn't.

So: replay a bug recording with the same version of the game it was recorded with. The version is
in the second line of the file (`version 1.1.0`). For a bug that is still open, record it again after
a change.

The recordings in `tests/replays/` are tests: each one is replayed by `ctest` and fails when it
drifts apart. After an intended rule change, record it again and copy it over:

```sh
./rungame.sh --autoplay --record --seed 42 --quit 3000
cp build/Debug/recordings/<newest>.bbr tests/replays/plus_bot_seed42.bbr
```

## 4. All debug keys

These work in every development build, while playing and (where noted above) while replaying.

| Key | Effect |
|---|---|
| `F1` | CPU partner view: where it expects danger (red, brighter = sooner) and the action it chose |
| `F2` | Invincible: nothing hurts the players |
| `F3` | Infinite lives |
| `F4` | Show hitboxes (green players, red enemies, blue bubbles, yellow items, pink boss) |
| `F5` | Show the air currents of the room |
| `F6` | No sticky bubbles: floating bubbles don't cling together (to compare) |
| `F7` | Pause / go on (frame step mode) |
| `F8` | One frame forward while paused (`Shift`+`F8`: one frame back, `Ctrl`+`Shift`+`F8`: one second back, only while replaying) |
| `F9` | Slow motion (quarter speed) |
| `F10` | Spawn the next item or special bubble in front of player 1 (each press: the next one) |
| `F11` | Full screen (all builds) |
| `F12` | Vacuum walls: walls stop bubbles inside a vacuum (to compare) |
| `Backspace` | Mark this moment in the recording; while replaying: jump to the next mark |
| `Tab` | Retro TV filter on / off (all builds) |
| `` ` `` (`^` on a German keyboard) | Debug view on / off (opening it pauses the game): the game in a panel, with panels for the switches, the round, all objects and the CPU partner (desktop only; see the README, *Debug view*) |

The top left of the window lists the switches that are on (`DEBUG INV LIVES BOXES ...`), and each key
shows a short message under it.

## 5. Command line options

Pass them to `rungame.sh`, `rungame.bat` or `replay.sh`.

| Option | Effect |
|---|---|
| `--replay <file>` | Play a recording (`replay.sh` adds this for you) |
| `--jump <tick>` | With `--replay`: run fast to that tick, then pause |
| `--record` / `--no-record` | Record a bot run too / don't record this run |
| `--autoplay` | A simple bot plays player 1 (sound off) |
| `--autoplay2` | Bots play both players (sound off) |
| `--seed <n>` | Fixed random seed |
| `--round <n>` | New games start at round n |
| `--rescue`, `--cpu2` | Switch those game options on for this run (not saved) |
| `--options` | Start on the options screen |
| `--skip-intro` / `--intro` | Leave out / show the story pages |
| `--ending <happy\|sad>` | Start with that ending |
| `--clear <tick>` | Remove all enemies at that tick |
| `--invincible`, `--infinite-lives`, `--hitboxes`, `--air`, `--cpu-view` | Start with that debug switch on |
| `--shot <tick>` | Save the picture as `shot_<tick>.png` (repeatable) |
| `--shot-crt <n>` | Also save each shot through the retro TV filter at n x scale, as `shot_<tick>_crt.png` |
| `--quit <tick>` | Quit after that many ticks |
| `--debug-ui` | Start with the debug view |
| `--ui-shot <tick>` | With the debug view: save the whole window as `ui_shot.png` at that tick |
| `--mute` | No sound |
| `--no-crt` | Start without the retro TV filter |

A tick is one frame of the game: 60 ticks are one second.
