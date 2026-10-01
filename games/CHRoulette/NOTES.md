# CHRoulette — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHRoulette at commit d8c2f67 (2026-10-01); develop here now, not in the old repo.
- Release build (CHGame core 0.2.4, CHGfx 1.3.0, `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 49,540 of 50,944 B (1,404 spare), static RAM 16,084 of 18,416 B (2,332 spare).
- Save pages: `../../tools/check_size.py` reports the image as 49,796 B, 256 B more than the compile's flash figure. Both A/B pages (0xF500, 0xF600) fit while the image is at most 50,432 B, so only 636 B of headroom remain. Past that, `src/save/Save.cpp` saves to page B only. Flash is the wall.
- Simulator-verified (as of 2026-10-01):
  - The game plays end to end: betting, whip, spin, payout, save/continue.
  - `python tools/tests/run_tests.py` passes: rules against the independent `ref_roulette.py` model, navigation, limits, the spin flow, a money-conservation fuzz.
  - `python tools/tests/run_ball_tests.py` passes: about 1.2M cases, every pocket, wheel and pace.
  - An adversarial review found 13 integration bugs, and all are fixed.
  - `tools/chsim/diffdrive.py` reports 0 stale pixels.
- Device: never run on the board. Phase P7 (device bring-up) is pending.

## Design decisions

The design specs are in `docs/design/*.md`. Where they conflict, `critique.md` decides; `architecture.md` has the phases P0-P8, the budget and the cut list. The owner overrode some spec recommendations, and the choices below win over the docs.

- Chosen: a European wheel by default, American in Options.
- Chosen: CHChess's glove on the felt layout:
  - a tap moves it half a cell (onto lines and corners), a held direction moves it whole cells;
  - taps wrap round the edges as in Chess, held runs stop at them.
- Chosen: CHBlackjack's dealer is the croupier.
- Chosen: the camera whips to a tilted wheel for the spin.
- Chosen: green felt only, with no felt themes. The zero, "0 GREEN" and the green banner rely on it.
- Chosen: winning stakes stay up, and only the winnings travel.
- Chosen: a new musette waltz as the title tune (over reusing CHBlackjack's). It was cut to 8 bars, about 304 B, to save flash; `tools/make_music.py` writes `src/audio/Music.cpp`.
- Chosen: about 4 s of wheel time on FUN (shorter than the spec's 5 s). The tuning is the `PACE` table in `src/wheel/Ball.cpp`; that file's header comment still says "about 5 s".
- Chosen: logo A, the chunky 1 bpp "Roulette" in `tools/art/logo.txt`, over the spec's recommended `title35` lettering.
- Plan defaults, never contested:
  - limits of $100 inside, $250 outside and $1,000 on the table;
  - no American 0/2 or 00/2 splits (their spots would sit 2.5 px apart).
- Cut to fit: the credits back room (`CHRL_CREDITS=0`; the credits are in the Options footer) and the attract demo (`CHRL_DEMO=0`).
- If more must go, follow `critique.md` §8.9 / `architecture.md` §4.4. Never cut the American wheel or the chip art.

## Open items

- P7 device bring-up, as `architecture.md` §5 describes:
  1. Announce the debug upload.
  2. Check pacing (`late=0`; the `P` max under 8.3 ms) on the worst frames: whip, landing, big win. Check stack use.
  3. Test save persistence across a re-upload only with the owner's permission.
  4. Upload the release build for the owner to play.
- Simulator render estimates after the incremental wheel and band-split redraws: 6-7.5 ms. They are unmeasured on the device; `tools/scripts/perf.txt` prints them.
- Deferred features: by the estimates in `config.h`, neither `CHRL_DEMO` (about 0.8 KB) nor `CHRL_CREDITS` (about 1.2 KB) fits in the 636 B margin while both save pages are kept.

## Gotchas

- Every `CHRL_DEBUG` build has no music scores (`Music.cpp`), and that includes the simulator. Audition the tunes with `python tools/audio/preview.py out/`.
- Device debug builds (`CHRL_LEAN`) also drop the credits page and use `title35` lettering on the win/broke screens instead of PPOT's bitmaps. `-DCHRL_FULL` forces a full device debug build.
- Device debug builds write flash only after a script sends `say E 1`, because the save pages are shared with the release build and the other games.
  - `architecture.md` §1.2 calls this hook `Y`; the code uses `E`.
  - The other hooks are in `CHRoulette.ino`: R seed, F force pockets (37 = 00), J screen, G glove, W bet, M purse, and `Q` calibration (simulator only).
- `diffdrive.py` lives in this game's `tools/chsim/`, not in the shared tools. Its scripts are in `tools/scripts/diff/`. Rerun it after touching any band redraw.
- Tools that read sibling games in `../` (`games/`):
  - `tools/assets.py` checks that the dealer, faces, end-screen lettering and glove come out byte-identical to `../CHBlackjack` and `../CHChess` `src/assets/Assets.cpp`. If a sibling is missing it prints "unchecked" instead of failing.
  - `tools/pixkit.py` (mockups) and `tools/logo_preview.py` load art from the same siblings.
- The ball:
  - The rules pick the number at SPIN.
  - The solver dry-runs the spin and turns the rotor by whole pockets so the ball lands there.
  - After any change to `Ball.cpp` or `PACE`, rerun `run_ball_tests.py`, which checks every pocket lands within its time window. `tools/spin_preview.py` traces one spin to a GIF.
- The wheel ring's map comes from `tools/wheel.py` (`src/assets/WheelMap.*`).
- `gfx_chunkScratch()` (1 KB) is shared by three users: the wheel's per-frame colour table (`src/wheel/WheelArt.cpp`), Mask lettering (`src/gfx/Mask.cpp`) and `save::store()`. Only one may hold it at a time, and only between `gfx_wait()` and the next flush.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).
