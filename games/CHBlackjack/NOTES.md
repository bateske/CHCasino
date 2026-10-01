# CHBlackjack — development notes

Agent-facing notes for continuing work on this game. Rules, controls and build steps are in [README.md](README.md); the platform is covered by the repo-root [CLAUDE.md](../../CLAUDE.md) and [docs/](../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBlackjack at commit 88d8fc7 (2026-10-01). Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 45,556 of 50,944 B (5,388 spare), static RAM 15,736 of 18,416 B (2,680 spare).
- Save pages: `../../tools/check_size.py` reports the image as 45,812 B, so both A/B save pages fit with about 4.6 KB to spare.
  - The README also says that a build with the IDE defaults (no LTO, USB Serial) still clears both pages, by about 60 B. Any growth breaks that claim, so re-measure and update the README when the game grows.
- Verification as of 2026-10-01: this is the most device-proven game here.
  - Host tests: `tools/tests/run_tests.py` covers the rules, every payout, PPOT bug regressions and a 16,000-hand fuzz.
  - Simulator scripts in `tools/scripts`.
  - On the board, `device.py run` scripts run in lockstep, and the screenshots match the simulator's.
  - `pace.txt` gave about 300 frames per 5 s with no late frames.
  - Render times measured on the board, before and after the CHGfx 1.3 migration: showcase average 9.8 → 5.8 ms, worst frame 28.2 → 14.0 ms, bust hand worst 25 → 11 ms.
  - `tools/probes/FlashProbe` proved that a flash page above the image survives a re-upload.
- The last commit (court portraits without the grey frame) changed art only and was re-recorded in the simulator. No device run is recorded for it.

## Design decisions

- Chosen: a faithful port of Press Play On Tape's game flow and screens. `src/game/Round.cpp` keeps PPOT's ViewState flow; the additions are presentation.
  - Casino rules are the default; the Classic preset keeps PPOT's rules.
  - PPOT's money bugs are fixed in both presets (listed in `NOTICE`).
- Chosen: the credits are a cozy casino "back room" page, reached with A from Stats. The dealer tells the credits in his speech bubble, under a neon sign and cigarette smoke. Rejected: a demoscene-style credits screen.
- Credits are exactly as in `NOTICE`, the README and the credits page: PPOT, filmote (code) and vampirics (art). Don't add or change names without the owner.
- Rejected: the original's "Mario" dealer (Nintendo fan art). It is not included; don't bring it back.
- Chosen by the owner: court-card portraits without the grey SILVER frame. CHPoker (`../CHPoker`) made the same change, so keep the two decks' court art in step.
- The release build is Smallest + LTO with USB "Upload only" (`tools/device.py` has the exact settings).

## Open items

- `CHBJ_LEAN` is no longer needed to fit: a full debug build fits (image about 47.5 KB). Whether to retire it is the owner's call.
- Known slow frames, pre-existing and not fixed:
  - The still screens (title, options, stats) redraw every frame for their first 90 frames, about 1.5 s (`step` in `render()`, `src/states/Screens.cpp`).
  - That measured about 13-15 ms a frame on CHGfx 1.2 and has not been re-measured since the migration.
- Not yet checked on the board: the frameless court art (88d8fc7).

## Gotchas

- Flash:
  - Every hand-written `.cpp` uses `#pragma GCC optimize("Os")`.
  - No `snprintf`: it is 3.5 KB with 64-bit division; use `src/gfx/Fmt.*`.
  - No `pinMode`: its pin tables are about 2 KB; write the registers.
  - The game has its own 1.8 KB sound sequencer, not CHGameSound.
  - The README's "How it fits" has the budget breakdown. Measure with `python ../../tools/check_size.py build/release` after every change.
- Big outlined lettering (Mask: a 1 bpp mask, grown for the outline, painted in up to three layers) costs about 5-10 ms per word on the board. Draw it once, on still screens or static layers, never every frame.
- The credits page draws its felt once and redraws only the wall band: 3.3 ms a frame measured on CHGfx 1.2.
- CHGfx 1.3 usage:
  - From the library: `sprite4` spans (court and face art are converted in `tools/assets.py`), dither, ellipses, `gfx_scroll` for the shake, and `copyRow`.
  - Kept local: the rounded rects plus `panel()`, `remapRect` (CHGfx's `gfx_remapRect` costs more SRAM, see `src/gfx/Draw.cpp`), the 3x5 font/`text35`, and the Mask banners.
  - The only pixel difference from the 1.2 build is the shake's uncovered edges.
- Generated files, don't hand-edit:
  - `src/assets/Assets.cpp` comes from `python tools/assets.py`. The first run clones PPOT's repository into `tools/.cache/ppot` (gitignored), pinned to a commit, so it needs git and network. `tools/art/dealer.png` must use palette colours only.
  - `src/audio/Music.cpp` comes from `python tools/make_music.py`.
- Debug builds:
  - Every `CHBJ_DEBUG` build, the simulator included, has no music scores (`Music.cpp` is under `#if !CHBJ_DEBUG`). Listen with `python tools/audio/preview.py out/audio` or a release build.
  - Device debug builds (`CHBJ_LEAN`) also drop the credits page; `-DCHBJ_FULL` forces it back in.
  - Announce device uploads, and put a release build back afterwards: a debug build looks like a game without its music.
- Profiling:
  - chdrive's `prof` sends the protocol's `T`, which answers only in a build made with `CHBJ_PROFILE=1`. In the simulator, use `chdrive.py --sim . -D CHBJ_PROFILE=1 ...`; `device.py` has no switch for it.
  - The simulator's PERF line reports `pcrnd`, the host-measured render time.
  - Scripts: `prof.txt`, `prof_hand.txt`, `perf_free.txt` and `pace.txt` (device).
- Saves use magic "CHBJ" in pages shared with every other CHGame game. A build that grows into the pages falls back to one page, then to none (Stats says SAVING UNAVAILABLE).
- Shared tools: the simulator is `../../tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
