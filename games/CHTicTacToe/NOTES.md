# CHTicTacToe — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHTicTacToe at commit db8274c (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 50,052 of 50,944 B (892 spare); the image (256 B more) is 50,308 B, only 124 B under the 50,432 B line that keeps both save pages), static RAM 15,176 of 18,416 B (3,240 spare). README's "50,308 B" (How it fits) and "43.9 KB" (Installing) are older figures; trust check_size.
- Verification: simulator and host tests only, as of 2026-10-01 (not re-run since the import): `tools/tests/run_tests.py` (rules, dealer, match flow); scripts smoke, endings, save, iso, hover, perf, showcase, gameplay all deterministic with no BUG lines; `tools/chsim/diffdrive.py` on tools/scripts/diff_iso.txt with 0 stale frames. There is no tools/check.py here: run those plus `python tools/device.py build` by hand.
- Never run on a CHGame: frame times (simulator estimates: full iso frame ~7 ms, glove move ~5-6 ms), the dealer's thinking time and every sound are unchecked.

## Design decisions

- Chosen (owner): title "TIC TAC TOE: ROYALE"; over the top, funny, tongue in cheek, the casino vibe. Played for money: purse, stake, odds per table, a draw is a push, streaks, broke/goal.
- Chosen (owner): "99 squares" means both ULTIMATE (9x9 of boards) and THE 99 (a literal 11x9, five in a row). As many rule sets as possible, each explained in game (pause menu; SELECT on tables without an iso view).
- Built beyond the plan's 12 tables, not yet confirmed by the owner: ALL X (notakto), DARK (phantom), WRAP (5x5 torus), MINES, DROP 4 (7x6 gravity), and 2 PLAYERS hot-seat (score only; no BLITZ/DARK/AUCTION).
- Chosen (owner): depth and physicality in CHChess's isometric style, with a top-down map as the strategy view. Iso applies to square 3x3 and 5x5 boards without ULTIMATE/gravity (`iso::fits`, src/render/Iso.cpp); DROP 4, ULTIMATE and THE 99 stay flat. SELECT toggles iso/map and it stays as left.
- Chosen (owner): flat fields use dithering and more shades, tastefully. The flat map is the iso board seen from above: WOOD frame, GOLD trim, WINE edges, INK dither shadow, recessed FELT pads (FELT_DK top/left, FELT_LT bottom/right), gold inlay when cells are >= 14 px, spotlit felt.
- Chosen (owner): X/O art 2 px smaller each way (`SHRINK` in tools/pieces.py). A held piece over a placed one must not look like it clips through it: it rises to max(`HOLD` 14, the piece's height + 4) (src/render/Stage.cpp), the piece under it is drawn with `RM_SHADE`/`RM_BLUESHADE` and no felt shadow over it, and the drop starts from that height.
- Chosen (fourth pass, pushed): held X/O on 3x3 iso tables spin (X_L1/O_L1 at 45 degrees, X_L2/O_L2 at 90, the rest mirrored via sprite4 scale -256; a step every 4 ticks).
- Chosen, not yet reviewed by the owner: when a held piece would leave the top of the screen the board glides down (`camY`/`camT` in src/render/Stage.cpp) and back on the dealer's turn.
- Removed at the owner's request: the decorative poker chips (iso stake stacks, title stacks, tables-room stake chip). Kept: GOBBLE and AUCTION chips, which are game pieces.
- Removed for flash (the owner allowed it if space was needed): the TOWER table, leaving 16 tables; save `VERSION` 2 in src/save/Save.cpp.
- No music, only a title sting: there is no flash for a score.

## Open items

- The owner's verdict on the iso look and on the extra tables.
- Device run: iso frame times (full and band redraws), dealer time on the big felts (12 cells a frame), every sound. Device debug builds are `CHTT_LEAN` (no saving, plain end-screen lettering) and were within ~50 B of the 50,944 B ceiling when the release was 50,308 B; `-DCHTT_FULL` does not fit. Put the release build back afterwards.
- Logo touch-up: tools/art/logo.txt and royale.txt (drafted by tools/make_logo.py from Arial Black and Georgia; the .txt files are the source).
- Optional music: impossible without cuts elsewhere.

## Gotchas

- Flash is effectively full. Earlier squeezes: `gfx_ellipse` dropped (fills drawn as pairs), only the bounce curve kept (`fx::bounce`, src/fx/Ease.*), unused banner styles cut. Nothing fails when the image passes 50,432 B: read check_size's "save pages free: N" line after every build (one page: saving loses its power-cut safety; none: saving switches off).
- LTO inlines almost everything into `stage::render`, so the symbol table does not show what a feature costs: measure by building a patched copy with and without it.
- Band redraw: when only the glove or cursor moved, Stage redraws just the rows they swept; `drawRows(lo, hi)` (src/gfx/Draw.cpp) clips the game's primitives and CHGfx. New play-screen drawing must respect that clip. Check with `python tools/chsim/diffdrive.py tools/scripts/diff_iso.txt out/diff 1` (0 stale frames expected). diffdrive patches the line `    wasMoving = moving;` in src/render/Stage.cpp in a temp copy: keep it or update the tool.
- Palette cycling (the `FX_A`/`FX_B` slots, src/gfx/Palette.cpp) animates the cursor, the fading VANISH mark and the winning line with no redraw; static art drawn in those slots will flicker with them.
- Iso pieces: tools/pieces.py ray-marches signed-distance models (CHChess's renderer) into tools/art/pieces/*.png + .anchor (L for 3x3, S for 5x5, L1/L2 spin frames). Re-running overwrites hand touch-ups. Then `python tools/assets.py`.
- tools/assets.py requires the dealer, faces and PPOT end lettering to come out byte-identical to ../CHBlackjack's src/assets/Assets.cpp and the glove to ../CHChess's; it stops with an error if they differ. Change shared art in the sibling first.
- Iso D-pad: the nearest cell in the pressed screen direction, scored `along + 3 * |perp|` (src/game/Match.cpp).
- Debug hooks (CHTicTacToe.ino): `R seed`, `J <T|G|P|W|L|O|S> [table]`, `C cell [arg]`, `H cell` (the dealer's next move), `M purse`, `D 0..2` dealer level, `V ticks` BLITZ clock, `E 1|0` (a non-lean device debug build writes saves only after `E 1`), `Q` simulator calibration.
- Showcase/gameplay scripts write GIFs straight into docs/; delete docs/sheet.png afterwards.
- Simulator: `../../tools/chsim/chsim.py` (shared; this game's tools/chsim/chdrive.py and diffdrive.py import it). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `../../tools/check_size.py` (`tools/device.py build` runs it).
