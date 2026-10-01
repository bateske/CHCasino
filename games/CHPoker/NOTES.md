# CHPoker — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHPoker at commit 0840afd (2026-10-01); develop here now, not in the old repo.
- Release build (CHGame core 0.2.4, CHGfx 1.3.0, `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`): flash 48,652 of 50,944 B (2,292 spare), static RAM 15,868 of 18,416 B (2,548 spare).
- Save pages: `../../tools/check_size.py` reports the image as 48,908 B, 256 B more than the compile's flash figure. Both A/B pages (0xF500, 0xF600) fit while the image is at most 50,432 B, so the real headroom is 1,524 B. Past that, `src/save/Save.cpp` saves to page B only.
- Simulator-verified (as of 2026-10-01):
  - `python tools/tests/run_tests.py` passes. It builds under UBSan; `--long` adds the exhaustive 7-card enumeration.
  - It covers exact hand-category counts, betting spots, side pots, stud order, a fuzz of thousands of hands, CPU equity and honesty, and stats.
  - The scripts in `tools/scripts/` run in the simulator, and `showcase.txt` makes `docs/*.gif`.
- Device: never run on the board.

## Design decisions

- Chosen: all four games in ONE image: NL Hold'em, PL Omaha, FL Five Card Draw and FL Seven Card Stud.
  - Hold'em and Draw were the first priority.
  - Separate editions built from one source (a `CHPK_EDITION` switch) were the fallback. They were removed once all four games fit; don't reintroduce them unless flash forces it.
- Chosen: a cash game with a saved purse and a Blackjack-style goal / broke ending. You play against 3 CPUs.
- Chosen: difficulty is tied to the stakes: ROOKIE $1/$2, PRO $5/$10, SHARK $25/$50 (`UNIT` and the variant table in `src/game/Variant.cpp`).
- Chosen: CPU seats are anonymous plates named by colour, with no portraits.
- Chosen: the CPUs judge their hands by Monte Carlo play-outs over the cards they can't see, and never peek at hidden cards. The tests check that changing hidden cards never changes a decision; keep that property.
- Defaults the owner has not yet confirmed or vetoed:
  - the draw takes up to 3 cards (4 when keeping an ace);
  - stud: ante = bring-in = one unit, bets of 2 and 4 units;
  - every live hand is shown at the showdown;
  - a $500 starting purse, with goals of $10K, $50K or endless;
  - a NEXT HAND / LEAVE bar after each hand;
  - sound effects only, with no music player (the fanfares are effects too, see `src/audio/Audio.h`).

## Open items

- Device checks: pace, CPU think time, mini-card legibility and sound by ear. Then leave the release build on the board.
  - `tools/scripts/device_perf.txt` runs each game at SHARK, free-running, and prints the frame `perf` and the CPUs' think time (`say W`).
- The owner's verdict on the unconfirmed defaults above.

## Gotchas

- Flash: 1,524 B before the image reaches save page A. Measure with `python tools/device.py build`, which runs the shared `../../tools/check_size.py`.
- Device debug builds (`CHPK_LEAN`, set in `config.h`) leave saving out entirely (stubs in `src/save/Save.cpp`), so a debug run on the board never touches its save pages. The simulator and release builds keep saving. `-DCHPK_FULL` forces a full device debug build, which may not fit.
- Debug hooks are sent with `say` in chdrive scripts; the list is in `src/states/Screens.cpp`:
  - `G` sit down (game, table, buy-in, seed), `D` stack the deck (card = rank*4 + suit), `$` set the purse;
  - `J` jump to a screen (T L O S W B), `H` table state, `W` CPU think time (last/max);
  - simulator only: `Z` "power cycle" (reload the save, back to the title) and `Q` (calibration for `cal`).
- This game's `tools/chsim/chdrive.py` adds the ops `waitturn`, `playto P` and `table`. Its handshake id is `CHPK`.
- CPU tuning is the `Level` table at the top of `src/game/Ai.cpp`: samples, noise, slack, bet/raise thresholds, first-street fold floor, bluff, slow-play, fear, position.
  - Tuned targets: ROOKIE calls about 60%, PRO folds about 2/3, SHARK raises about as often as it calls.
  - Rerun `run_tests.py` after any change.
- CPU thinking runs a fixed number of play-outs, only on a frame's first logic tick. Keep it that way: it keeps the table animating and lockstep scripts deterministic.
- The showcase GIFs (royal flush, four sevens) come from stacked decks in `tools/scripts/showcase.txt`. Everything else in them is the CPUs playing, so a CPU tuning change alters the GIFs.
- Art: `tools/assets.py` packs local copies in `tools/art/` (CHBlackjack's card art, CHChess's glove, and `logo.txt`, "Poker" in the letters of PPOT's BlackJack logo); it does not read the sibling games. Credit Press Play On Tape as NOTICE does.
- Compiler: the simulator and tests need `CHSIM_CXX` set, or zig/clang++/g++ on PATH (see the root CLAUDE.md).
