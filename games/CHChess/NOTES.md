# CHChess — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHChess at commit 34e4382 (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 48,628 of 50,944 B (2,316 spare), static RAM 17,872 of 18,416 B (544 spare). The image (`../../tools/check_size.py`'s `image:` line) is 48,884 B, 1,548 B under the 50,432 B that keeps both A/B save pages.
- Without LTO it overflows (~350 B over). Device debug build (`-DCHCH_DEBUG=1`, which turns on `CHCH_LEAN`: no saving, no Options screen) was 49,484 B / 17,904 B.
- Verification: simulator - `tools/tests/run_tests.py` (perft, draw rules, book, snapshots, every CPU level, fuzzed games with undo and save/load) and the chdrive scripts in `tools/scripts`. There is no `tools/check.py` in this game. Device: the CHGfx 1.3 release has run on the board, and the render times, think times and stack peaks under Gotchas were measured there.

## Design decisions

Board, glove, highlights:
- Chosen: 20x10 iso tiles, half-size pieces; SELECT switches board / MAP. Rejected: the 2x CLOSE view toggle (removed).
- The D-pad visits all own pieces, blocked ones included (useful for navigation); nearest in the screen direction, wrapping to the farthest the other way. With a piece up it visits the targets.
- A blocked piece shows NO MOVES; A on it (`stage::deny`) buzzes and flashes NO MOVES and the glove red (`RM_ALERT`) 3x over 24 frames. That is the only use of the red glove for the player.
- Hovered piece: outline fades black/white (palette mode `HOVER` animates `FX_A` as a grey ramp). Rejected: pick-up sparkles (distracting).
- Picked-up piece: outline cycles `fx::RAIN`. Prey under the glove flashes red (`RM_PREY`); other capturable pieces flash white.
- Chosen square blinks dithered/solid: CYAN for a move, RED for a capture. No ghosting. Last move is lit only during the reply to it.
- A plate at the foot names the piece or target and calls out moves in words. HUD is the name only. Rejected: chips, material count, trays.
- Hints and coordinates are always on (both options removed; `Save.h` keeps their bytes as `unused`/`unused2` so saves keep their layout, the menu maps past them with `optByte`). Options: SOUND, BOARD, PACE (FUN/QUICK). Setup button reads BEGIN. No title subtitle.
- Knights are never mirrored (mirroring broke the shading); `sprite4`/`spriteRot` have no mirror.

Check and mate:
- Vs a human, CHECK! stays up (`fx::holdBanner`, a blinking PRESS A plate, call-out frozen) until any button (`stage::waiting`/`acknowledge`, flag `waitPress`); `busy()` holds the turn. The CPU in check never waits. CHECKMATE! always waits the same way before the result panel.
- The checked king's body beats `RM_PREY` (`((frame>>3)+5)&5 == 0`, phased to land as the HOVER outline is light), under the glove too, but not once picked up. `spots()` offers only pieces that can move (cached per position).
- Rejected: a red glove in check, and gold-to-red trim/UI in check (wrong vibe).

Inspect (B held, `stage::inspect`):
- Zoom 10; a straight push goes to a diamond corner, a diagonal to an edge middle. A, START and SELECT are ignored while B is held. Rejected: spring-back (removed).
- Holding a piece, a tap is < `HOLD_B` (32 frames) and a 2 px cyan bar grows at y 10-11 meanwhile (`holdBar`); otherwise the threshold is 8 frames.

CPU turn (it should act like a player):
- `onTurn` glides the red glove to the CPU's own king (`holdT` 24) before the search starts (it used to start over the player's piece).
- Search in bursts (`src/Frame.cpp`): every `SEARCH_MS` 2000 a `BURST_MS` 450 full-rate burst (`stage::thinkPick` moves the glove); in between a frame every `BOB_MS` 133 with `frameCount |= 7` so the glove keeps bobbing. Buttons or a menu force frames (`screens::holdFrames`).
- The pick: glove rests on the piece (24/8 frames), taps, the piece lifts (its target lit, prey flashing), glove glides to the square, stays 60/16, taps, moves. The CPU's piece gets the HOVER outline too; `stage::selected()` returns the player's selection only.
- A soft clock (`Sfx::Tick`/`Tock`, every 2 s) plays while it searches: slows perceived time, hides hiccups. Sfx >= Tick play at 1/8 duty. Keep Tick/Tock last in the enum.
- Vs the CPU the camera stays on the human's side; the hand-over spin is for 2P only.

Camera and effects:
- Whip zoom starts at once, `iso::tileH` 5 to 10, one step per drawn frame (`zoomDrawn`). After landing it holds until all particles are gone (`outWait`), then zooms out snapping to the framing. Stays in on mate. QUICK pace or the map disables it.
- Captures play at half speed (`CAPTURE_SLOW` 2); the fly is parametric in its tick. Capture sound: impact plus ~0.5 s falling swoops.
- Dust puffs are sized to the zoom and exactly the landing square's colour (they must blend in). Particles are screen space and do not scroll (`fx::scroll` was removed: it broke the map); `busy()` waits for `fx::particles()` so the camera never moves under them.
- The carpet is plain (the dot lattice was dropped).

Opponents and flash priorities:
- Three opponents (cut from five for flash; save `VERSION` 2): BEGINNER {400,150}, EXPERT {3000,25}, GRANDMASTER {12000,0} in `src/game/Match.cpp`.
- When cutting flash: sound effects over tunes, no CPU-vs-CPU mode, a minimal opening book is fine, gameplay first.

## Open items

- Awaiting the owner: building with `-DCHGFX_ISR_IN_SRAM` (its SRAM paid for by moving the `text35x2` RAMFUNC to flash); the pacing constants (`SEARCH_MS`, `BURST_MS`, `BOB_MS`) were deliberately left unchanged after the faster CHGfx 1.3 build.
- Twice a scripted device move was refused because the game had gone to the title mid-script. Not reproducible; possibly buttons pressed on the board during the run.

## Gotchas

- RAM is the scarcer budget (544 B). Every RAMFUNC costs SRAM as well as flash. Game RAMFUNCs go through `src/RamFunc.h` (`.gnu.linkonce.r.chch.<name>`); each needs its own name.
- CHGfx 1.3: only `gfx_fillEllipse` (and the staged palette) were adopted. Rounded rects, dither, `sprite4`, `spriteRot`, shake, `copyRow`, the 3x5 font / `text35` / `text35x2` / `Mask.cpp` stay game code because CHGfx's versions measured bigger or slower. Per-item table in `docs/CHGfx-notes.md`; measure before swapping any of them.
- Measured on the board (CHGfx 1.3 debug builds): full redraw 6.0 ms normal, 5.6 map, 7.8 zoomed; whip-zoom frames 6.1 ms average, 8.6 worst; GRANDMASTER 9.2-9.3 s a move with bursts and bobs (raw search ~7 s, ~1,700 nodes/s). Scripts: `tools/scripts/device_render.txt`, `device_think.txt`.
- Stack: main peaks ~1,552 of 2,048 B. Frames drawn from inside the search run on `Frame.cpp`'s own 1 KB stack (peak ~640 B).
- Chip facts found here (more in `../../docs/performance.md`): flash code ~5 cycles an instruction, SRAM code ~2; newlib's `memmove` is a byte loop in flash; an async full flush costs ~5 ms of CPU; pieces take ~5 ms (~1.2 us per sprite run); `sprite4` needs separate 1:1 and scaled loops (register pressure); a packed-nibble sprite format was tried and reverted.
- Engine: `src/engine/ch2k.hpp` is ch2k from ArduChess, MPL-2.0, patched. Keep it under MPL-2.0 with its change list at the top current; everything else is Apache-2.0. Credits are exactly those in NOTICE (Peter Brown / tiberiusbrown for the engine, Press Play On Tape for the 3x5 font); add no others.
- `src/engine/Engine.cpp` sets `CH2K_POLL_NODES` 8 (~5 ms between callbacks) and `CH2K_MAX_PLY` 10 (stack). The opening book is 4 plies; `tools/book.py` can only cut it, so a deeper book has to come from upstream ch2k or the old repo's history.
- Debug protocol: `Y` prints the render profile per section, `W` the last think's time and nodes. `Y`, `W`, `R`, `H` answer at once even while the CPU searches; any other scripted game command acknowledges a pending CHECK! (scripts `tap A` through mate). Simulator only: `X <fen>` (2P), `V <fen>` (vs the CPU, you to move), `R <sq>` (D-pad route), `H` (board, your-turn and waiting flags), `J`, `Q`.
- chdrive extras: `goto SQ`, `waitturn` (answers CHECK! with A after 90 frames), `board`, `rec start/stop`, `freegif` (device only: the simulator's free mode is not real time).
- `tools/scripts/gameplay.txt` (the README reel): the CPU's replies follow the seed set by the title/setup press timing, so change nothing before 1.e4. To plan a game vs the CPU: replay it in the simulator (deterministic), read `H`, and pick moves with a host build of the engine (`Engine.cpp` + `load_fen` + `benchThink` at ~200k nodes).
- Art: `tools/sheet.py export/import` is the owner's Photoshop workflow; keep it working. Import reads colours by RGB value (Photoshop reorders the colour table on save). If MASTER's pieces are missing or the side rows are reshaped, the art is rebuilt from the White/Black colour pairs (up to 16, INK kept for the outline). The source of truth is `tools/art/pieces/*.png` and `tools/art/hand.png` (hand-finished); `tools/pieces.py` only writes `tools/art/gen/`. `tools/art/sides.txt` generates `SIDE_REMAP`; `HAND_TIP` is generated from the art. Black is silver/blue/navy.
- Don't name things `sq`, `map` or `word`: Arduino macros.
- Check any drawing change pixel-for-pixel against a baseline simulator run of the same scripts.
- Paths moved with the monorepo: the simulator is `../../tools/chsim/chsim.py`, the size report `../../tools/check_size.py` (per-game tools stay in `tools/`). For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- Device debug builds leave out saving; the board may be in use, so announce a debug upload and put the release back afterwards.
