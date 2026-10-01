# CHBingo — development notes

Agent-facing notes for continuing work on this game. Rules, controls and build steps are in [README.md](README.md); the platform is covered by the repo-root [CLAUDE.md](../../CLAUDE.md) and [docs/](../../docs).

## Snapshot

- Imported from https://github.com/bateske/CHBingo at commit dd6295b (2026-10-01). The public history was squashed to that single commit. Develop here now, not in the old repo.
- Release build (`CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 36,468 of 50,944 B (14,476 spare), static RAM 15,208 of 18,416 B (3,208 spare).
- Save pages: `../../tools/check_size.py` reports the image as 36,724 B, so both A/B save pages fit with about 13.7 KB to spare. This game has real flash room.
- Verification as of 2026-10-01: simulator only. These all passed:
  - `tools/tests/run_tests.py`: rules, races against the hall, money, power-ups, jackpot odds, round set-up against `tools/tests/ref_bingo.py`; about 1.24M checks.
  - `tools/tests/sim_save.py`: save mid-round, power-cycle, continue.
  - `tools/chsim/diffdrive.py` on `tools/scripts/diff/diff_soak.txt`: 0 stale pixels.
- There is no `tools/check.py` in this game.
- It has never run on the board. Render times (the README has simulator estimates), pacing, sound and saving on hardware are unmeasured.

## Design decisions

Made by the owner:
- The dealer (CHBlackjack's PPOT dealer) announces every ball in his speech bubble.
- The win banner is a rainbow "BINGO!". Rarely it says "IT'S A BINGO!" instead, and the dealer's bubble corrects it: "You just say bingo....".
- Up to 9 cards, each bought separately. Every card needs its own daub: move to it and press A.
- Only one and a half cards are visible, so play is swiping. A card holding a called, undaubed number gets a rainbow outline.
- The player races a hall of rivals; the twists are power-ups and a progressive jackpot.
- Depth everywhere: shaded panels, cards, daubs and pips, plus drop shadows.
  - Panels are layers of one rounded shape: the shadow follows the corners, the outline is one colour and the rims follow the curve (`panel()` in `src/gfx/Draw.cpp`).
  - The owner dislikes jagged edges and shadows that ignore the contour.
  - Ball shadows are a single shrinking line.
- Title:
  - "Bingo" in CHBlackjack-style lettering (`tools/art/logo.txt`: B from Blackjack, o from Roulette).
  - Rejected: chips on the title, and a spotlight at the top.
  - The menu sits on a FELT_DK dither band at a pitch of 12.
  - The menu glove is CHChess's glove pointing right (`HAND_R`), thumb on top, cuffed in the dauber colour.
- Title balls (the owner loves them; tuned over four rounds, see `titleBalls()` in `src/states/Screens.cpp`):
  - A continuous stream spelling BINGO travels right to left with B leading. "Left to right" was a slip in the owner's earlier request.
  - All balls are the same size and bounce at one rate and height (`BOUNCE` 48 ticks), each a beat behind the ball ahead.
  - Each ball has its own slow ±3 px swing, which can never make one overtake another (`PITCH` 27).
- The DAUBER option (red/blue/green/cyan/peach) colours the daubs, their splat and the glove's cuff.
- A daub is CHChess's DUST puff, plus GOO "gack" particles (a Nickelodeon feel), plus a small shake.
- After a daub the glove holds on the cell with a press-and-kick `RECOIL` curve (22 ticks, `src/fx/Presenter.cpp`), then slides on. A swipe cancels the hold.

## Open items

- Device run: render profile, pacing, sound by ear, saving.
- Owner feedback on the overall look and feel.
- Defaults the implementer chose, not yet confirmed by the owner:
  - The view shows the card in play plus the first three columns of the next card, as a wrapping carousel, with a pip per card (rainbow when a number is waiting).
  - A daubs every waiting number on the card in play. Completing a line is the call. A press with nothing waiting resets the streak.
  - The hall has 8/20/40 rival cards. The pot is 90% of the hall's buy-in. A tie goes to the player.
  - A call every 2 s (1.3 s on FAST, 3 s on SLOW).
  - The rare banner comes 1 win in 10 (`RARE_ONE_IN`). The jackpot pays within 10 calls (`JACKPOT_CALLS`).
  - The power-ups are WILD, FREEZE and 2X POT.
  - The caller has nicknames for 13 numbers (`LINGO` in `src/fx/Presenter.cpp`).
  - No music, and no win screen: play is endless until broke.

## Gotchas

- The skeleton was forked from CHRoulette (wheel, glove and music removed): macro prefix `CHBN_`, save magic "CHBN", and the protocol handshake is `CHBN`.
- A saved round is just its seed plus the daubs; the draw, the cards and the hall are dealt again from the seed. Any change to dealing order or random-number use breaks saved rounds, so bump `VERSION` in `src/save/Save.cpp`. The save pages are shared with every other CHGame game.
- The hall costs no RAM: each rival card is reduced to the call on which it completes, and only the earliest call is kept.
- Redraws are incremental, by band (wall, plaque, felt, bar).
  - `-DFORCE_FULL` (`src/fx/Presenter.cpp`) forces full redraws.
  - After touching render or presenter code, run `python tools/chsim/diffdrive.py tools/scripts/diff/diff_soak.txt out/diff`.
  - The title draws its felt and logo once (`titleReady`) and redraws only the ball band and the menu.
- Rainbow outlines, pips and the winning line all use one cycling palette entry. Animate through the palette, not with redraws.
- Debug protocol:
  - The game's commands are listed at the top of `CHBingo.ino`; `Q` (calibration) is simulator-only.
  - Device debug builds do not write the save pages unless a script sends `say E 1`, because the pages are shared with the release and other games.
  - `CHBN_LEAN` (device debug) drops only the broke screen's lettering; saving stays in.
- `python tools/chsim/autoplay.py docs/gameplay.gif` is a buttons-only bot. It searches for a seed the player wins, then records that seed; use it to regenerate the README reel.
- `src/audio/Music.h` is a stub with no scores, so the sequencer's music calls do nothing. Adding music costs flash, but there is room.
- `src/assets/Assets.cpp` is generated by `python tools/assets.py` from `tools/art/` (dealer, faces, hand, logo, broke lettering). Don't hand-edit it.
- Shared tools: the simulator is `../../tools/chsim/chsim.py` (game-side driver: `tools/chsim/chdrive.py`). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
