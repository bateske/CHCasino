# CHBoardwalk — development notes

Agent-facing notes for continuing work on this game. Rules, controls and build steps are in [README.md](README.md); the platform is covered by the repo-root [CLAUDE.md](../../CLAUDE.md) and [docs/](../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBoardwalk at commit a99a4f8 (2026-10-01). Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 49,600 of 50,944 B (1,344 spare), static RAM 15,404 of 18,416 B (3,012 spare).
- Save pages: `../../tools/check_size.py` reports the image as 49,856 B. Both A/B save pages need the image to stay at or below 50,432 B, so the margin is only about 576 B. Treat flash as full: any feature needs a cut first. LTO inlining makes small additions cost more than they look.
- Verification as of 2026-10-01: simulator only.
  - `tools/tests/run_tests.py` checks every rule, then plays 5,000 seeded games (CPUs and random "humans") checking that the books balance, houses stay even and every game ends, and prints a tuning table.
  - The scripts in `tools/scripts` play through in the simulator.
  - There is no `tools/check.py` here, so no automated run-twice determinism check.
- It has never run on the board. Pace, render profile and sound are unchecked, as the README's status line says.

## Design decisions

- Name and IP:
  - The title is BOARDWALK, with the classic Atlantic City street names and colour groups.
  - No trademarked game name, characters or official card wording anywhere (code, art, README); the card texts are in the game's own words.
  - The README carries the "independent game" disclaimer.
- Built for one player against the CPU, or two on one handheld, quick and arcade style.
  - 2-4 seats, any human (hot-seat) and CPU mix.
  - The title offers 1 PLAYER / 2 PLAYERS; THE TABLE comes up preset with the cursor on BEGIN.
  - CPU turns always run at the quick pace. The hand-over banner between two humans does not wait for a press.
- A short game:
  - Random deeds are dealt free at the start: 4 each with two players, 2 each with three or four. Chosen over "head-start pairs".
  - The game ends at the first bankruptcy, or at closing time (10-60 rounds, default 20). The richest player wins.
- Rejected: trading. There are auctions instead: simple, automated, real time, tap to outbid, and a lot always sells.
  - Players can put their own deeds up. The bank's half-price opening bid is the floor, and only the bank's own unbid lot goes free to the next seat.
  - CPUs do not offer deeds of their own.
- Rejected: mortgages. Debts settle automatically: houses go back at half price, then deeds go to auction.
- Payday ($400 for landing on GO with the dice) was a change to the rules, and the owner kept it.
- Camera: an isometric close-up that follows the token, plus a whole-board map on SELECT.
- Tokens are all fruit: cherries P1 RED, banana P2 GOLD, apple P3 FELT_LT, strawberry P4 SKIN. Pink stands in for the strawberry so it doesn't match the cherries. Rejected: a die token, which was confusing next to the dice.
- The glove is CHChess's hand art, with a gold cuff for humans and a red cuff (`RM_CPU`) for CPUs.
- Lettering:
  - The title has its own slab-serif `LOGO`, 1 bpp, drawn through Mask like CHBlackjack's logo.
  - The same `LOGO` is printed on the board's plaque, in plain GOLD with no blink (the owner asked: no FX_B on board lettering). The title's logo still shimmers.
- The ink rule under a colour band is clipped to its own cell.
- Scaling: art is drawn at whole multiples only, because the owner found the in-between zoom steps "stretched".
  - `iso::zscale()` is 256 below tileH 8 and 512 from there. Use `iso::sized()` for sprite lengths and `iso::zoomed()` for board lengths.
  - Things painted on the board (the plaque) scale with `zoomed()` through every step; things standing on it go 1x/2x.
  - A new house drops in with a bounce rather than scaling up.
- Look-dev picks: 20x10 tiles; white/silver tiles; pink is a RED/WHITE dither and orange a RED/GOLD dither. Felt colour themes were dropped, because FELT_LT is the green group.

## Open items

- Device test: pace, render profile, sound by ear.
- `tools/scripts/save.txt` is not deterministic in the simulator: two runs of the same build draw different `title_still_continue` and `continued` frames (found while verifying the move into CHCasino; the original repo behaves the same). The other scripts repeat exactly. Find what reads host time or uninitialised state around SAVE + QUIT / CONTINUE before trusting pixel comparisons of that script.
- The owner's art redraw through `tools/sheet.py`. The sheet has 20 sprites, including the fruit, CHIPS, CARD_DECK, the corner icons and LOGO.
- Arcade rules the implementer chose and reported, not explicitly confirmed by the owner (only payday was):
  - Building on most of a group (2 of 3, or both of a pair).
  - The bank's half-price opening bid.
  - The Free Parking jackpot (taxes, card fines, bail).
  - 4 deeds each with two players.
  - Default 20 rounds.
  - CPUs not offering deeds.
- Device debug builds are always `CHBW_LEAN` (no options screen, no saving). A full debug build overflowed by about 0.8 KB.

## Gotchas

- Running the showcase script into `docs/` also writes `docs/result.png` (the script's `snap result`) and `docs/sheet.png` (chdrive's contact sheet). Delete both before committing.
- The simulator's `cal`/`perf` render estimate is host time and swings by ±50% from run to run under load. It is useless for small differences.
- Debug protocol:
  - The game's commands are listed above `debugHook()` in `src/states/Screens.cpp`.
  - `G D A $ E T H` work on the board; `J V X F Q` are simulator-only.
  - The protocol owns `? S K L N P B`, and also `T` in a `CHBW_PROFILE=1` build, where it shadows the game's token command `T`.
- `CHBW_LEAN` is `#ifndef`-guarded: `-DCHBW_LEAN=0` forces a full device debug build, which won't fit without a temporary cut.
- Art:
  - `tools/art/sprites.txt` holds the sprites as palette letters. A `tools/art/<name>.png` replaces its sprite, and `chips.png`, `icon_chest.png`, `icon_jail.png` and `token_banana.png` already do, so editing those four in `sprites.txt` has no effect.
  - `python tools/sheet.py export` / `import [SHEET]` round-trips an indexed PNG. Colours are matched by value, and FX_B cannot be used in sprites because it is the transparent colour. An edited sheet saved elsewhere imports with `python tools/sheet.py import <path>`. Wide sprites (LOGO) get a row of their own.
  - Go To Jail is `ICON_JAIL` with SILVER turned RED, and the Community Chest deck is `CARD_DECK` with GOLD turned BLUE.
  - `python tools/lookdev.py` renders a contact sheet of the board at each zoom and tile treatment.
- Board geometry: a 13x13 iso lattice with a ring 2 cells deep (tiles 1x2 cells, corners 2x2). The art is native at tileH 5 and the whip zoom goes to 10.
- `src/game` is pure logic and host-tested. A rule change needs the tests updated.
  - Tuning table at import (all CPUs, two seats): bust by closing time about 21% at cap 20 and about 42% at cap 30, with 16-19 houses on the board.
- Saves use magic "CHBW", `VERSION` 1, in `src/save/Save.cpp`. CONTINUE resumes as the turn began. The pages are shared with every other CHGame game; bump `VERSION` on any change to the `Record` layout.
- Shared tools: the simulator is `../../tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
