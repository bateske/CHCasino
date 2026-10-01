# tools/: platform-wide tools

The tools in this folder are **shared by every game** in `games/`. They know
nothing about any one game. Each game's own scripts find them by the fixed
layout `games/<Name>/tools/... -> ../../tools` (search for
`CHCasino/tools` to see every place that does).

Run them from a game's folder:

```bash
cd games/CHFour
python ../../tools/chsim/chsim.py build .          # build the PC simulator of this game
python ../../tools/check_size.py build/release     # flash/RAM report of the last release build
python ../../tools/serialcap.py --seconds 5        # print what the board says on USB serial
```

Python packages: `pip install -r tools/requirements.txt` (Pillow and
pyserial). The simulator also needs a C++ compiler: `$CHSIM_CXX`, zig,
`pip install ziglang`, clang++ or g++.

## What is here

| Tool | What it does |
|---|---|
| `chsim/chsim.py` | Builds a game for the PC: its `.ino` + `src/`, CHGfx's portable drawing code (`platform/libraries/CHGfx`, or `$CHSIM_CHGFX`), and the host shims. `build()` and `find_cxx()` are imported by the games' tools. Output: `<game>/tools/chsim/build/<Name>/sim.exe`. |
| `chsim/host/main.cpp` | The simulator's main loop: virtual time, lockstep frames, the game's serial port on stdin/stdout, a deterministic random seed. |
| `chsim/host/chgfx_host.cpp` | Stands in for `CHGfx.cpp`, the SPI/DMA part. It times the simulated panel and reports `BUG:` when a game draws into rows that are still being sent. |
| `chsim/host/Arduino.h`, `sim.h` | A minimal Arduino API for the PC, and the shims' internal declarations (including the SD card hooks some games add). |
| `chsim/fbimage.py` | Turns a framebuffer dump (8 KB of 4 bpp + 32 B palette, from the `S` debug command) into a PNG, a contact sheet, or a full-colour GIF (`save_gif`). |
| `chsim/gifsheet.py` | Tiles a GIF's frames into one image to review it: `gifsheet.py IN.gif OUT.png [--every N] [--start F] [--count N] [--cols C] [--scale S]`. |
| `check_size.py` | Flash and RAM report from the linker map. It checks the 50,944 B / 18,416 B limits and shows the space left for the save pages; `--top N` and `--symbols` list what takes the room. |
| `serialcap.py` | Finds the board's USB serial port (VID:PID 16C0:27DD), opens it with DTR set and prints its output. `find_port()` and `open_port()` are used by `device.py` and `chdrive.py`. |

A game can add its own simulator shims in `<game>/tools/chsim/host/`. They
are compiled with the shared ones, and a `.cpp` there with the same name as
a shared one replaces it. The SD games keep CHSd's pretend card there
(`VCard.h`, `sd_host.cpp`).

## Every tool in the repository, classified

### Per-game, built from a shared template

Every game has its own copy of these, adapted to that game. Copy one from
a similar game when starting a new one.

| Tool | In | What changes per game |
|---|---|---|
| `tools/chsim/chdrive.py` | all 20 | Runs a script against the simulator (`--sim`) or the board (`--device`) over the debug protocol: `wait tap hold release snap gif rec say perf free freegif`. Each game sets its handshake `--id` and adds its own commands (e.g. CHChess `goto`/`waitturn`/`board`, CHCrossword `type`/`solve`/`--card`, CHMahjong `solve`/`takehint`). |
| `tools/device.py` | all 20 | `build [--debug]`, `upload`, `run SCRIPT OUTDIR`, `shot OUT.png` through arduino-cli. Per game: the name and the debug define (`-D<PFX>_DEBUG=1`, CHCrossword also `-DCHCW_LEAN=1`). |
| `tools/check.py` | CHBackgammon, CHCheckers, CHCrossword, CHDominoes, CHFour, CHSnakes, CHSolitaire, CHWords, CHWordWheel | Everything checkable without a board: host tests, every script twice (same frames, no `BUG:`), release build + size. Some add game checks: the network's evaluation, puzzle packs, card scripts, the redraw check. |
| `tools/tests/run_tests.py` | all 20 | Builds and runs the host unit tests. The source list is per game. |
| `tools/audio/preview.py` + `audio/host/` | all 20 | Renders the game's `src/audio/Audio.cpp` to WAV through a model of the piezo timer, and prints a hash per sound. The effect and song lists are per game. |
| `tools/chsim/diffdrive.py` | CHBingo, CHRoulette, CHSlots, CHTicTacToe, CHWordWheel | The redraw check: builds the game twice, normal and forced to redraw everything every frame, and reports any pixel the incremental redraw got wrong. The forced-redraw patch is per game. |
| `tools/tests/sim_save.py` | CHBingo, CHCraps, CHYacht | Save, power off, continue, in the simulator. |
| `tools/assets.py` | all 20 | The art pipeline: `tools/art/*` → `src/assets/Assets.{h,cpp}`, with previews in `build/assets`. |
| `tools/scripts/*.txt` | all 20 | chdrive scripts: the README GIFs (`showcase`, `gameplay`), smoke tests, perf runs, device-only runs (`device_*`). |

### Game-specific

| Game | Tools |
|---|---|
| CHBackgammon | `train/` (self-play trainer for the CPU's network, the race table fit, the match equity table), `lookdev.py`, `font_preview.py` |
| CHBingo | `chsim/autoplay.py` (records a won round as a GIF), `tests/ref_bingo.py` (independent model of the set-up) |
| CHBlackjack | `make_music.py` (songs → Playtune bytes), **`probes/FlashProbe/`** (a hardware probe sketch that proves a sketch can erase and write a flash page that survives re-upload; the save-page mechanism of every game rests on it) |
| CHBoardwalk | `sheet.py` (sprites ↔ one editable PNG sheet), `lookdev.py` |
| CHCheckers | `sheet.py`, `tests/demo_line.cpp` (finds the title screen's demo game) |
| CHChess | `book.py` (trims the opening book), `pieces.py` (renders the iso pieces), `sheet.py` |
| CHCrossword | `puzzles/` (pack format, builder, grid filler, `.puz` import, card images), `tilefont.py` (anti-aliased tile font) |
| CHDominoes | `aafont.py` (derived from CHCrossword's `tilefont.py`), `tilemock.py` |
| CHFour | `font_preview.py` |
| CHMahjong | `faces.py` (tile faces), `bird.py`, `layouts.py` + `layouts/*.txt` (layout maps → `Layouts.cpp`), `sheet.py` |
| CHRoulette | `wheel.py` (wheel angle maps), `spin_preview.py`, `logo_preview.py`, `make_music.py`, `mockup.py`, `pixkit.py` (Python copy of the drawing code), `tests/run_ball_tests.py`, `tests/ref_roulette.py` |
| CHSlots | `strips.py` (reel strips → `Strips.h`) |
| CHSnakes | `turns.py` (CPU turn table), `sheet.py`, `lookdev.py` |
| CHSolitaire, CHTicTacToe | `make_logo.py`; CHTicTacToe also `pieces.py` (ray-marched iso pieces) |
| CHWords | `dict/` (word lists → flash dictionary and `sdcard/WORDS.DIC`), `tilefont.py` |
| CHWordWheel | `phrases/build_bank.py` (flash bank + `sdcard/PHRASES.BNK`), `make_music.py`, `mockup.py`, `pixkit.py` |

### Elsewhere in the repository

| Tool | What it does |
|---|---|
| `platform/libraries/CHSd/tools/vendor.py` | Copies CHSd into the SD games; `--check` verifies the copies. |
| `platform/libraries/CHSd/tools/fatimg.py` | Builds and reads FAT16/FAT32 card images: useful for any SD-card test. |
| `platform/libraries/CHSd/tests/run_tests.py` | CHSd's host tests on FAT images. |
| `platform/libraries/CHGfx/extras/fontconvert.py`, `sprite4.py` | CHGfx's font and sprite converters. |
| `platform/libraries/CHGfx/extras/sim/` | CHGfx's own simulator for testing the library and its examples. It is not the game simulator above. |
| `utilities/CHSDtoUSB/tools/chsd_test.py`, `scsi.py` | Hardware test suite for the SD-to-USB sketch (Windows, SCSI pass-through). `find_drive()` locates the board's drive. |

## Candidates to share later

These exist in several games in nearly the same form. They were left in
place so that the move into CHCasino changed no behaviour:
- **`chdrive.py`'s common core:** a shared driver plus per-game command
  plugins.
- **`device.py`, `check.py`, `tests/run_tests.py`:** each could take a small
  per-game config.
- **The audio preview harness.**
- **`make_music.py`'s composer:** CHBlackjack, CHRoulette, CHWordWheel.
- **`pixkit.py`:** CHRoulette and CHWordWheel differ by 8 lines.
- **The font tools:** `tilefont.py`, `aafont.py`, `font_preview.py`.
- **The `sheet.py` export/import pattern.**
- **Shared art:** `tools/art/hand.png`, `dealer.png`, `faces.png` and
  `font.txt` are byte-identical in several games.
