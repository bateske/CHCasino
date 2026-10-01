# CHCasino: guide for agents

Twenty Arduino games for the CHGame handheld, plus the platform they need.
Read [README.md](README.md) for the overview. This file is the working
manual: setup, commands, limits, rules and gotchas. Each game also has a
`NOTES.md` with its design decisions and open items. Read it before
changing that game.

## Rules

1. **Simulator first.** Everything except timing and sound can be checked on
   the PC. The real board may be in someone's hands; see "The device" below.
2. **Pixels are the test.** The simulator is deterministic. A change that
   should not alter the picture must give identical frames. Run the game's
   scripts before and after and compare (`tools/check.py --compare A B`, or
   hash the PNG/GIF frames). A change that does alter the picture must
   re-record the README GIFs it affects.
3. **Measure size after every change.** `python tools/device.py build`
   prints flash and RAM. Most games are within 1 KB of full (see Limits).
4. **Generated files are not edited by hand:**
   - `src/assets/Assets.*` comes from `tools/assets.py` and `tools/art/`.
   - Several games generate other tables, e.g. CHWords `src/dict/DictData.*`,
     CHWordWheel `src/bank/BankData.*`, CHCrossword `src/game/PuzzleData.*`,
     CHSlots `src/game/Strips.h`, CHMahjong `src/game/Layouts.cpp`. The header
     of each says what makes it.
   - **CHSd copies:** `games/{CHWords,CHCrossword,CHWordWheel}/src/sd/*`, their
     `tools/chsim/host/{VCard.h,sd_host.cpp}` and CHCrossword's
     `tools/puzzles/fatimg.py`. Edit `platform/libraries/CHSd`, run its
     `tests/run_tests.py`, then `python platform/libraries/CHSd/tools/vendor.py`.
     `--check` verifies.
5. **The shared `tools/` serve all 20 games.** After changing anything in
   `tools/chsim` or `tools/*.py`, run several games' `tools/check.py` and
   compare sim frames against a run from before the change.
6. **`platform/` is vendored.** Leave `platform/board` and
   `platform/libraries/CHGfx` as their upstream releases unless a change is
   intended. If one is, record it in [platform/README.md](platform/README.md)
   so it can go upstream.
7. **Credits stay exactly as each game's `NOTICE` and README give them.** Do
   not add names from upstream projects' credit lists.
8. **Every game needs its own save magic, debug macro prefix and debug
   handshake id.** All games share the same two flash save pages; see
   docs/status.md for the current collisions.

## Setup

**Linux, macOS or a cloud container:**

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=$HOME/.local/bin sh
arduino-cli config init --overwrite
arduino-cli config add board_manager.additional_urls https://github.com/bateske/CH32SerialBoot/releases/latest/download/package_chgame_index.json
arduino-cli core update-index && arduino-cli core install CHGame:ch32v@0.2.4
pip install -r tools/requirements.txt ziglang      # Pillow, pyserial; zig is the simulator's compiler
```

**Windows:** the same `arduino-cli` commands (or the Arduino IDE's Boards
Manager), then `pip install -r tools/requirements.txt ziglang`.

**Notes:**
- The board package brings its own RISC-V GCC 8.2 and the `chgame-upload`
  tool, so nothing else is needed for device builds.
- **CHGfx needs no install.** `tools/device.py` compiles with
  `--library <repo>/platform/libraries/CHGfx`, and the simulator uses the same
  copy. With plain `arduino-cli compile`, add
  `--library ../../platform/libraries/CHGfx` yourself.
- **The simulator's compiler:** `$CHSIM_CXX` (for example `"zig c++"` or a
  full path plus ` c++`), otherwise zig on the PATH, otherwise
  `python -m ziglang`, otherwise clang++ or g++.

## Commands

Run from a game folder, `games/<Name>/`:

| What | Command |
|---|---|
| Release build + size | `python tools/device.py build` |
| Debug build (serial debug protocol on) | `python tools/device.py build --debug` |
| Upload release / debug | `python tools/device.py upload [--debug]` |
| Everything checkable without a board | `python tools/check.py` (9 games have one; `--quick`, `--no-device`) |
| Host unit tests | `python tools/tests/run_tests.py` |
| Build the simulator | `python ../../tools/chsim/chsim.py build .` |
| Run a script in the simulator | `python tools/chsim/chdrive.py --sim . tools/scripts/<s>.txt out/<s>` |
| The same script on the board | `python tools/device.py run tools/scripts/<s>.txt out/<s>` (debug build + upload) |
| Screenshot of a running debug build | `python tools/device.py shot out/shot.png` |
| Sound effects to WAV | `python tools/audio/preview.py out/audio` (prints a hash per effect) |
| Regenerate art | `python tools/assets.py` |
| What fills the flash | `python ../../tools/check_size.py build/release --top 30` |
| Redraw check (5 games) | `python tools/chsim/diffdrive.py <script> <outdir> [ticks]` |

The release FQBN is
`CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`. A debug
build drops `usb=uploadonly` and adds
`--build-property build.extra_flags=-D<PFX>_DEBUG=1`. `<PFX>` is the game's
four-letter prefix in `config.h`, for example `CHF4` for CHFour. Pass
sketch-level defines only through `build.extra_flags`. It is empty on this
platform. Never override `compiler.cpp.extra_flags`.

## Limits

| | |
|---|---|
| Flash for the image | **50,944 B** (0x3000-0xF6FF). The bootloader takes 12 KB, and one page of metadata sits at 0xF700. |
| Save pages | Two 256 B pages at the top of the app region. Keep the image ≤ **50,432 B** for both (A/B with CRC), ≤ 50,688 B for one. Past that, saving switches itself off. |
| Static RAM | **18,416 B**: 20 KB less the 16 B boot block and the 2 KB stack. Under ~900 B free, Arduino warns. |
| Stack | 2 KB (games report the high-water mark with the debug `P` command) |
| CPU | 48 MHz, no PLL. Flash has 3 wait states and no cache: code runs at ~5 cycles per instruction from flash vs ~2 from SRAM. |
| Display | 128x128 ST7735S on SPI1 at 24 MHz. CHGfx's 4-bit framebuffer is 8 KB. A full async flush costs ~5 ms of CPU. |

**Tactics that pay:**
- `opt=oslto`.
- `RAMFUNC(name)` (each game's `src/RamFunc.h`) for per-pixel loops. Every
  RAMFUNC also costs RAM.
- `#pragma GCC optimize("Os")`.
- Word-sized loop counters.
- Drawing static layers once instead of every frame.
- Avoid newlib's `memmove`: it is a byte loop in flash.
- See [docs/platform.md](docs/platform.md) and
  [docs/performance.md](docs/performance.md).

## The simulator (`tools/chsim`)

`chsim.py build <sketch>` compiles the game's `.ino` and `src/` plus CHGfx's
portable code for the PC. `host/chgfx_host.cpp` stands in for the SPI/DMA
part, and `host/main.cpp` runs the game in lockstep on virtual time.

**How it is driven.** `chdrive.py` (one per game, because each adds its own
commands) talks to the sim, or to a debug build on the board, over the
game's serial debug protocol (`src/debug/Debug.h`):

| Command | Does |
|---|---|
| `?` | handshake |
| `S` | screenshot (8 KB framebuffer + palette) |
| `K` | hold buttons |
| `L1` / `L0` | lockstep on / off |
| `N k` | run k frames |
| `P` | perf |
| `B` | reboot into the bootloader |

Anything else goes to the game's hook.

**Scripts** in `tools/scripts/*.txt` are lists of `wait`, `tap`, `hold`,
`snap`, `gif`, `rec` and the like (see the header of each `chdrive.py`).

**What a run produces.** Screenshots and GIFs go to the output folder. A
`BUG:` line on stderr, with exit code 3, means the game drew into rows
still being sent to the panel.

**Behaviour.** The sim always runs with the debug protocol on and with fixed
work slices instead of timers, so scripted runs repeat exactly. Scripts that
use `free` / `freegif` run on wall-clock time and do not repeat.

**Extra host shims.** A game can add its own in `tools/chsim/host/`. The
three SD games keep the pretend SD card there; set `CHWD_CARD` /
`CHWW_CARD` to a file, or pass `--card <img>` for CHCrossword.

## The device

**Uploading.** `chgame-upload` (from the board package) uploads over USB
CDC. It does the 1200-baud touch, flash, verify and restart itself; no
buttons are needed. The board is USB VID:PID `16C0:27DD`, and
`tools/serialcap.py` / `device.py` find its port by that.

**The board may be in use.** Someone may be playing it or listening to it,
and other sessions may share it.

**Before a device run:**
- Say that you are about to upload a debug build or run device scripts. A
  debug build can drop music or screens to fit the protocol, and a lockstep
  script freezes the game, so both look like crashes from the outside.
- Check that no other `device.py`, `chgame-upload` or `chdrive.py --device`
  process is using the board.
- Never probe the port with a bare pyserial open: it can hang holding the
  port. `chgame-upload -port <PORT> probe` is the safe check. A crashed
  sketch (the core's HardFault handler spins, which also stops USB) and a
  board busy in its bootloader look the same.

**After a device run:** put the release build back
(`python tools/device.py upload`) and say it is ready.

**Recovery:** [platform/board/docs/recovery.md](platform/board/docs/recovery.md)
(hold BOOT across power-on for the factory ISP).

## Putting files on the SD card without removing it

[`utilities/CHSDtoUSB`](utilities/CHSDtoUSB) turns the board into a USB card
reader, with its serial port still working beside the drive:

1. Upload it from `utilities/CHSDtoUSB`:
   `arduino-cli compile -b CHGame:ch32v:CHGame --library ../../platform/libraries/CHGfx .`
   then `arduino-cli upload -b CHGame:ch32v:CHGame -p <PORT> .`.
   After it starts, the board enumerates on a new serial port.
2. A removable drive appears whose SCSI vendor is "CHGame" and product "SD
   Card Reader" (`tools/chsd_test.py`'s `find_drive()` finds it on Windows).
3. Copy the files to the card. It must be FAT16 or FAT32: CHSd cannot read
   exFAT. Then eject. Sending any byte to the serial port returns a status
   line (`... CARD <blocks> RW CONNECTED`).
4. Upload the game again (on the new port). This works while the drive is
   mounted, with no buttons. Holding B for 1 s also returns to the
   bootloader.

**What each game reads from the card:**
- CHWords: `WORDS.DIC` in the root.
- CHWordWheel: `PHRASES.BNK` in the root.
- CHCrossword: `CHCW/*.CWD`.

Each is in the game's `sdcard/` folder. See [docs/sd-card.md](docs/sd-card.md).

## Starting a new game

Copy the closest existing game; the newest ones have the most complete
tooling (`check.py`, `diffdrive.py`). Then:
1. Rename the folder, `.ino`, `config.h` prefix (`<PFX>_DEBUG`,
   `<PFX>_VERSION`), the debug handshake id (`Debug.cpp`, chdrive's `--id`
   default), the save magic (`src/save/Save.cpp`) and the RAMFUNC section
   prefix.
2. Update `device.py`'s debug define.
3. Keep `src/CHGame.*`, `debug/`, `save/` and `RamFunc.h` as they are unless
   there is a reason; they are the shared core
   ([docs/game-anatomy.md](docs/game-anatomy.md)).

## Gotchas

- **SPI is shared.** The default `SS` (PA4) is the LCD's CS; the card's CS is
  PB11 (`PIN_SD_CS`). Touch the SD card only between `gfx_wait()` and the
  next flush. CHSd and CHSDtoUSB hand SPI1 back as CHGfx left it.
- **Arduino macros clash with names:** `sq()`, `map()`, `word()`, `min`/`max`.
- **Line endings are LF everywhere** (`.gitattributes`). On Windows, write
  files with explicit `newline="\n"`/UTF-8 from Python; the default cp1252
  fails on non-ASCII.
- **Git Bash heredocs mangle backslash escapes.** Write code containing
  `\n` with a file-writing tool, not a heredoc.
- **The debug protocol costs flash** (~2 KB with USB Serial). Some games
  have a `<PFX>_LEAN` switch that drops saving or screens from debug builds
  so they fit. A device debug build is therefore not the whole game. Say so
  if someone will be playing it.
- **Sibling assets:** some `tools/assets.py` read another game's art (for
  example `../CHBlackjack/tools/art/dealer.png`) to check or share it. The
  games must stay side by side in `games/`.
- `tools/chsim/build/`, `build/` and `out/` are build output and ignored.
