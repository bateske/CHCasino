# CHMahjong — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHMahjong at commit 481dfdf (2026-10-01); develop here now, not in the old repo.
- Release build (CHGame core 0.2.4, CHGfx 1.3.0, `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 48,108 of 50,944 B (2,836 spare), static RAM 18,084 of 18,416 B (332 spare). RAM is the tight budget, not flash.
- Save pages: `../../tools/check_size.py` reports the image as 48,364 B, 256 B more than the compile's flash figure. Both A/B pages (0xF500, 0xF600) fit while the image is at most 50,432 B, so the margin is 2,068 B. Past that, `src/save/Save.cpp` saves to page B only.
- Simulator-verified (as of 2026-10-01):
  - `python tools/tests/run_tests.py` passes: 10,000 deals per layout cleared, golden deal hashes, every free tile reachable by the cursor.
  - Scripts `ui`, `match`, `clear`, `stuck`, `save` and `showcase` run clean in `tools/chsim/chdrive.py --sim`. They also ran clean once with the simulator built under UBSan.
- Device: the owner ran an early build on the board (before the close-up, classic faces, new title and sparrow) and reported it worked well. No run of the current build on hardware is recorded. Render time, pacing and sound by ear are unmeasured.

## Design decisions

- Chosen: mahjong solitaire, not 4-player mahjong.
- Chosen: the D-pad hops between free tiles only (`src/game/Nav.cpp`):
  - UP/DOWN use CHChess's nearest-in-direction rule.
  - LEFT/RIGHT step through the free tiles in reading order.
  - Reason: the pure directional rule left some tiles unreachable. A host test proves every free tile can be reached.
- Chosen: the cursor is both the lifted tile with an FX_B outline and CHChess's glove (`tools/art/hand.png`).
- Chosen: chips with streak scoring. The constants are in `src/game/Board.h` (`PAIR_PAYS`, `STREAK_FRAMES`, `HINT_COST`, `SHUFFLE_COST`, `CLEAR_BONUS`, `PAR_SECS`, `MAX_SHUFFLES`).
- Rejected: a true isometric view (the genre uses the oblique view, and iso hides tiles at 128 px). Built instead: the hold-B 2x close-up, plus Options VIEW FULL/CLOSE.
- Rejected: grey (SILVER) blocked tiles. Every tile is white with no blocked cue, and the glove shows which tiles are free. The reference look is GNOME Mahjongg.
- Chosen: CLASSIC traditional faces by default. The old numbered faces stay as Options TILES EASY.
- Chosen: tile bodies have an ivory (SKIN) side and a WOOD backing.
- Rejected: gold sides (they looked gilded and drowned the gold cursor outline). `-DCHMJ_BODY_SIDE=GOLD` (`src/stage/Stage.cpp`) brings them back for comparison.
- Title feel: "calm, like koi swimming". Keep it calm when changing the title (`src/states/Screens.cpp`):
  - Tiles mostly do coin-flip spins. A tumbler is only 1 in 12 (tumbling looked funny but off-vibe).
  - A turn every 5-10 s, a gentle sway and a slow sink.
  - The meteor glides across in about 1 s.
- Chosen: the win banner says MAHJONG! (not JACKPOT!). Then the sparrow visits: any button shoos it, and the results screen waits for it.

## Open items

- First device run: `python tools/device.py run tools/scripts/device_render.txt out/device` measures the draw cost of a frame on the board.
  - Do it with the owner watching the screen. An early debug upload got no serial answer, and the board then dropped off USB.
  - Port contention is the likely cause, but a hardware-only crash was not ruled out.
- After that: check pacing and sound by ear, then leave the release build on the board.
- Simulator render estimates (unreliable, taken on a loaded host):
  - about 7-9 ms at 1x and about 11 ms at 2x;
  - in-between frames of the close-up whip up to about 30 ms (the whip steps one zoom level per tick, so it still takes 4 ticks).
- The owner has not yet given a verdict on the glove drawing back to the table's bottom-right corner after 50 idle frames (`IDLE_FRAMES` in `src/stage/Stage.cpp`).
- The sparrow art (`tools/art/bird.txt`) is another artist's work, and NOTICE still lacks its credit and licence. Fill them in when the owner supplies them.
- The weakest close-up drawings in `tools/art/classic2x.txt` are the 15x23 green dragon (發) and the 1-bamboo bird.

## Gotchas

- RAM has 332 B spare. The hot blitters are `RAMFUNC` (`src/gfx/Tile.cpp`, `Draw.cpp`, `Mask.cpp`, `src/fx/Fx.cpp`), so their code counts against static RAM; the tile blitters alone take about 1.26 KB. Debug builds have less spare RAM still.
- Device debug builds don't fit with everything:
  - `config.h` turns on `CHMJ_LEAN`, which drops the EASY faces; `-DCHMJ_FULL` overrides it.
  - The simulator and release builds keep everything.
- No debug write guard: a device debug run saves to the board's flash pages, the same pages every game uses.
- `save::store()` builds the page in `gfx_chunkScratch()`, so call it only between `gfx_wait()` and the next flush.
- A saved game is the seed, the layout and the pairs taken (plus shuffle markers), replayed.
  - The `GOLDEN` hashes in `tools/tests/test_board.cpp` guard the deal.
  - If a layout or the deal generator changes them, bump `VERSION` in `src/save/Save.cpp` (magic "CHMJ").
  - Deal generation runs a few pairs per frame and must give the same result however the work is split.
- Palette: felt themes swap only `FELT_DK` and `FELT`. `FELT_LT` is the bamboo ink and stays green in every theme (`src/gfx/Palette.cpp`).
- Some names clash with Arduino macros, and only on the device build: `bit` and `FLASH` here, `sq`, `map` and `word` in other games. Compile for the device early, not just the simulator.
- Simulator `perf`/`cal` numbers are host time scaled by a calibration, so they are noisy on a busy host.
- UBSan: the UBSan run above used a per-object build with `-fsanitize=undefined`. The one-shot build in the shared `../../tools/chsim/chsim.py` has no sanitizer option and would not link with it.
- Faces: `python tools/faces.py` rewrites `tools/art/classic.txt` and `classic2x.txt`, overwriting any hand finishing in them. Diff before re-running. `tools/assets.py` computes the emboss shade.
- Sparrow pipeline: `tools/bird.py` → `tools/art/bird.txt` → `tools/assets.py`.
  - The source sheet is not in the repo (default path `build/assets/Bird.gif`, gitignored), so edit `bird.txt` directly.
  - Its acts (fly in, land, hop, peck, ... fly out) are scripted in `src/stage/Stage.cpp`.
  - It is about 4.8 KB of flash.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).
