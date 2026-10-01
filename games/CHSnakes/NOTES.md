# CHSnakes — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHSnakes at commit 89bba02 (2026-10-01); develop here now, not in the old repo.
- Release build (CHGame core 0.2.4, CHGfx 1.3.0, `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 37,080 of 50,944 B (13,864 spare), static RAM 15,704 of 18,416 B (2,712 spare).
- Save pages: `../../tools/check_size.py` reports the image as 37,336 B, 256 B more than the compile's flash figure. Both A/B pages (0xF500, 0xF600) fit with 13,096 B to spare (the limit is an image of at most 50,432 B).
- Simulator-verified (as of 2026-10-01): `python tools/check.py` passes. It runs:
  - the host tests (100,000 seeded games, saves, CPUs, the SHARK table against the layout);
  - every script in `tools/scripts/` twice, with identical frames and no drawing into a frame still being sent;
  - the device compile and size check.
- Device: never run on the board. Pace, sound by ear and render time are all unmeasured.

## Design decisions

- Owner's choices:
  - A top-down flat board with a zoom camera: the close-up follows the token and whips out to the whole board.
  - Two rule sets at setup: CLASSIC (one die, 6 rolls again, exact roll or bounce back) and ARCADE (two dice and pick one, doubles roll again, landing on a rival bumps it).
  - The tokens are CHBoardwalk's fruit.
- Second pass, which the owner called fantastic:
  - The ARCADE pick shows a marching arc of dots from the token to the target square, red and flashing when a snake waits there.
  - The camera stays close and rides down a snake, zooming out only after the spit.
  - The title is slab-serif lettering after CHBlackjack's logo. `LOGO_SNAKES` is drawn at half size and doubled by `tools/assets.py`; `LOGO_LADDERS` is 1x.
- Third pass: a ladder climb also stays close with the camera following, zooming out only at the top. The chomp sets off a big blast (two rings of chunks plus the particle bursts).
- Agent choices reported to the owner but not yet confirmed. Keep them unless the owner says otherwise:
  - 2-4 seats in any mix of humans and CPUs; everyone starts on square 1, which is safe.
  - A bumped token drops straight down one row, not back to the start.
  - One hand-placed board: 8 ladders and 8 snakes in `src/game/Layout.cpp`, with the big snakes at 96 (to 46) and 99 (to 61).
  - A small bobbing arrow marks whose turn it is. The glove was too big on 11 px squares, so it only points at the die in ARCADE's pick bar.
  - CPU turns, and the QUICK pace, play at the overview.
  - The title is a 4-CPU ARCADE game playing itself.

## Open items

- Device run (pace, sound by ear, render time). It needs the owner's go-ahead.
  - The simulator estimates 5-14 ms a frame (the title about 20 ms), over the 8.3 ms budget during motion, so expect 30-60 fps.
  - Snakes and ladders are the main cost.
  - `tools/scripts/perf.txt` prints the per-moment estimates.
  - After a device run, `python tools/check.py --compare out/<sim> out/<device>` compares the frames pixel for pixel.
- The owner's feedback on the look and on the unconfirmed choices above.
- An art redraw through `tools/sheet.py`, if the owner wants one.

## Gotchas

- The board is one table: `LINK[]` in `src/game/Layout.cpp`. After changing it:
  1. Run `python tools/turns.py`.
  2. Paste the printed table into `TURNS[]` in `src/game/Cpu.cpp` (the SHARK's turns-to-go, 101 bytes). The host tests recompute it and fail if they disagree.
  3. Use `python tools/lookdev.py` to preview the board at each size.
- ADL trap: a stage helper must not share its name with an `fx::` function that takes an `fx` enum. A local `banner()` called with an `fx::BannerStyle` resolved to `fx::banner` and put banners off-screen. The helper is now `call()`; see the comment in `src/stage/Stage.cpp`.
- Snakes are procedural bead chains pushed by a travelling sine wave. Only the head (`SNAKE_HEAD`, `SNAKE_HEAD_OPEN`) is art, and its `FELT_LT`/`FELT` pixels become each snake's two colours.
- Art round trip:
  - `python tools/sheet.py export`, edit `tools/art/sheet.png` (indexed palette), then `python tools/sheet.py import`.
  - The import writes each changed sprite to `tools/art/<name>.png`, which from then on overrides that sprite's letters in `tools/art/sprites.txt`. `token_banana.png` already does.
- `CHSN_LEAN` is on for every device debug build: no saving and no options screen.
  - It is the same switch as in CHBoardwalk, whose framework this game started from, although this game has room to spare.
  - To test saving on the board, build debug with `-DCHSN_LEAN=0` (the macro is `#ifndef`-guarded).
- Debug hooks are sent with `say`; the list is in `src/states/Screens.cpp`:
  - `G` new game (seat kinds, mode, seed), `D` next dice, `M` place a token, `O` overview, `H` state;
  - simulator only: `J` screen, `V` look at a square, `X` square tones, `Q` calibration.
- The determinism check in `check.py` runs every script twice. New animation code must draw from seeded state only, never from host time.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).
