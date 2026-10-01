# CHDominoes — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHDominoes at commit ddace41 (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 42,100 of 50,944 B (8,844 spare), static RAM 17,296 of 18,416 B (1,120 spare). The image (~42.4 KB) leaves both A/B save pages free with ~8 KB to go: RAM is the tight budget here, not flash.
- Verification: simulator only. `python tools/check.py` passes: host tests (rules vs a naive reference, 20,000 matches laid out with no overlap, save/reload mid-round, CPU levels against each other), every script twice with identical frames, device compile and size.
- As of 2026-10-01 it has never run on the device: frame times unmeasured, sound unheard.

## Design decisions

- Rules: ALL FIVES and DRAW, chosen on the setup screen; double-six, two seats, play to a target.
- Players: 1 vs the CPU (three levels) or 2 hot-seat (the rack turns face down until the next player presses A).
- Top-down only. Rejected: an isometric view and a 3D falling-domino win effect - keep it top down, save memory, and put the effort into the end-of-round effect.
- The finale matters most: a chain of firecrackers using the sprite rotation (CHChess's capture), tile by tile along the line with particles. Built as: fuse from the last tile played back to the first and out along the other arms (arms burn in parallel), gaps shortening, each tile flung spinning (`rotRaw`) with sparks, smoke and a crack rising in pitch, a boom on the last; a match win adds fireworks until PRESS A; a CPU-won round is just swept away.
- View: on this 128 px screen, favour big readable art and a following camera over fitting everything in view. The first build (whole table at 1x, 7x13 tiles) was unreadable; play is now close up at 2x (13x25 tiles) with a camera gliding to the play, and hold B shows the whole table. No whip zoom.
- Tile style follows a reference image from the owner: bevelled BONE face (WHITE top/left, SILVER bottom/right), engraved bar, 2x2 SLATE pips with a 2-px shadow (no corner pixel, or diagonals merge), a pixel of face between pips and bevel. Per-number pip colours were dropped to match it.
- Depth pass: 3 px near edge plus drop shadow, drawn back to front; brass pin on the bar; felt vignette and printed double line; wooden racks; score plaques with a pulsing edge for the side to play. Rejected in look-dev: pips with silver corners (read as plus signs), a dithered red rack channel (noisy).
- Tile sets: Options TILES = WHITE/BLACK/IVORY/RED/BLUE/JADE/GRAPE/PINK (GRAPE because PURPLE overflowed the row). One set for everything in play; the title always uses white + black.
- Title: falling tiles as in CHMahjong/CHSolitaire; all fallers tumble end over end (planar `rotRaw`, 1:1) with a slow spin at the original fall pace; the name hand-drawn at full size in `tools/art/logo.txt`, straight on black (no plaque, no shadow); menu selector radius 2.
- Title rejections: any scaled art (it reads as crunched / pixel-doubled; keep art 1:1), turning about the vertical axis and tiles lying on their side (uncomfortable to watch), half-speed falling, grey depth-shaded tiles, a radius-3 selector (jaggies).
- Lettering: CHCrossword's anti-aliased DejaVu Serif Bold at its 12 px size, everywhere. Rejected: 14 px (looks horizontally stretched) and any rescaling - adapt the layout to the font, not the font to the layout.

## Open items

- Not yet confirmed by the owner (README describes them as built):
  - whether 2x is big enough (3x would leave only ~3 tiles across);
  - spinner = only a double set as the first tile (keeps four arms at the centre so the pinwheel never dead-ends);
  - round 1's heaviest double set automatically, later the round's winner leads freely; the whole boneyard can be drawn; the match is decided at a round's end;
  - levels ROOKIE / REGULAR / SHARK, targets 100/150/200 (fives) and 50/100/150 (draw), default ALL FIVES to 100 vs REGULAR;
  - the "YOU/CPU: LAST TILE!" call; SELECT hint = the SHARK's play.
- Deferred: per-number pip colours as an option, if the owner misses them.
- First device run: `python tools/device.py run tools/scripts/perf.txt OUTDIR` for frame times, `say Y` for the cost by section (felt, line, HUD, rack, rest), and `python tools/check.py --compare` against the simulator's run. Earlier simulator estimates: 6-8 ms in play, ~7-10 ms on the title. Listen to the sound.
- Flash is spare: more sizzle in the finale is affordable if asked.

## Gotchas

- RAM: the title's fallers are cached as images (`fallerImg`, 9 x 175 B, ~1.6 KB); new RAM has to come from somewhere like that.
- `tilePx` (`src/table/Table.cpp`) uses `CLEAR` = 0xFF for "no pixel", because colour 15 (`FX_B`) is a real face colour.
- `fx::applyShake(10, GFX_H - 1)` shakes everything under the scoreboard. Shaking only the felt rows split anything crossing them (the revealed CPU hand, a raised rack tile), which looked like partial renders.
- Palette: two slots were given to the tiles (SKIN to BONE 0xEEE, CYAN to SLATE 0x445), so the rainbow and confetti have no cyan. A tile set is a swap of BONE/SLATE (`table::useSet`, `SETS`).
- `Options.tiles` took the old pad byte (no save `VERSION` bump); one `pad` byte is left. Another new option needs it or a version bump.
- `fontText` picks the half-ink tone from the ink (WHITE to SILVER, GOLD/FX_B to WOOD, FELT_LT to FELT; any other ink gets no half ink): a new ink colour needs a mapping. Selection boxes are 15 px tall (y - 3) for the 9-px caps.
- Speed: close-up tiles use an SRAM row-pattern renderer (`tileFast`); `text35x2` writes the framebuffer directly. The simulator cannot show gains like these (host calls are cheap; the device pays flash wait states): measure on the board.
- Simulator perf estimates swing with host load (about 2x seen): compare against a clean copy of HEAD run at the same time before blaming a change.
- `CHDM_LEAN` exists but is off: debug builds carry the whole game. Turn it on only if the game outgrows the debug build.
- Debug hooks (above the hook in `src/states/Screens.cpp`): `G` start a match, `D` stack the deal, `C` score, `W` end the round, `Y` render profile, `J` jump, `A` play for the human, `H` state, `Q` (simulator) calibration. chdrive extras: `waitturn`, `auto`, `round`.
- Look-dev: `tools/tilemock.py` draws the pip-treatment sheet; `tools/aafont.py` regenerates `tools/art/aafont.txt`; `tools/scripts/gameplay.txt` makes the README reel.
- Credits are exactly those in NOTICE (Press Play On Tape's 3x5 font, DejaVu for the serif); add no others.
- The simulator is `../../tools/chsim/chsim.py` (shared); per-game tools stay in `tools/`. For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- The board may be in use: announce a debug upload and put the release back afterwards.
