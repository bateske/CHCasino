# CHSolitaire — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHSolitaire at commit 38d0309 (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 30,888 of 50,944 B (20,056 spare; the image is 31,144 B, so both save pages fit with ~19.3 KB to go), static RAM 16,540 of 18,416 B (1,876 spare). RAM, not flash, is the tight budget in this game.
- Verification: simulator and host tests only. `python tools/check.py` (host tests, every script in tools/scripts run twice with identical frames and every `expect` holding, release build) passed as of 2026-10-01; not re-run since the import.
- Never run on a CHGame: pace, frame rate, sound and card legibility on the real LCD are unchecked. The ~5 ms full-table / <1 ms cascade frame figures are simulator estimates scaled by the CHGfx benchmark (chdrive `cal`, sim hook `Q`).

## Design decisions

- Chosen: copy the Solitaire that came with Windows as closely as possible: rules, Standard/Vegas/None scoring with its numbers, timed game and bonus, draw one/three, one-level undo. The win cascade (cards bounce off the foundations and leave their trail) is the most important feature.
- Chosen: keep the casino series' glove; card backs in the Windows spirit with a choice (12 on the DECK screen: 2 weaves in code, the rest art; robot, castle, island, fish, lucky 7 and chip have small animated gags).
- Chosen (owner, second pass): the glove points UP from under the pile's top card, fingertip 2 px onto its bottom edge; the carried run rides on the fingertip. Rejected: finger resting on the selected card from above (it read as pointing at the stock).
- Chosen (owner, second pass): white space above the rank (glyphs at y+2), face-up overlap 9 px (`UP_PITCH`, src/render/Layout.h).
- Chosen (owner, second pass): no cascade behind the title menu (it was there in the first build); instead cards drift down the felt and flip, with a meteor now and then, as CHMahjong's tiles do.
- Chosen (owner, third pass): empty glove bobs 1 px slowly; a card put down on a column gives a light grey/white dust ring (CHChess's puff, toned down); carried cards get the rainbow (FX_A) border; the title menu is one row of CHBlackjack-style accordion buttons in the small font (PLAY light green hovered / dark green not; the others gold hovered / grey not).
- Approved only as part of the plan, no verdict yet:
  - 17x23 card (seven columns force it; no big card anywhere).
  - A pick/put, double-tap A to send to the foundation, B put back / deal, SELECT undo, START pause.
  - The Vegas bank carries over between games and is saved.
  - No felt themes (FELT_LT is the only green, and the card-back art uses it).
  - Title lettering drawn from Georgia Bold Italic.

## Open items

- Device run: pace, sound by ear, card legibility, real frame times. A device debug build (`tools/device.py upload --debug`) keeps saving unless built with `-DCHSL_LEAN=1`, so it writes the shared save pages like the release; put the release build back afterwards.
- The owner's art pass on the card backs (tools/art/backs/*.txt, 15x21 in palette letters; a palette-exact PNG of the same name overrides one).
- The owner's verdict on the plan-level choices listed above.
- Known issue (logged, not fixed; see ../../docs/status.md): the save magic `0x4C534843` "CHSL" in src/save/Save.cpp, the debug handshake id "CHSL" (tools/chsim/chdrive.py `--id` default) and the `CHSL_` macro prefix (CHSL_DEBUG etc.) are the same as CHSlots'. After switching between the two games one can read the other's save record. A fix needs a new magic (existing saves are then ignored) and a matching handshake/`--id`.

## Gotchas

- RAM: the title's top rail (the outlined lettering costs ~5 ms to draw) is cached as 31 framebuffer rows, 1,984 B of static RAM (`rail` in src/states/Screens.cpp). It is the first thing to give back if RAM runs short.
- The cascade depends on the table NOT being redrawn: each logic tick stamps the bouncing card into the framebuffer (up to three stamps when catching up). Anything that invalidates the table mid-cascade wipes the trail.
- The whole game state (`Klondike`, src/game/Klondike.h) plus options and stats must fit one 256 B flash page: `static_assert` in src/save/Save.cpp. Undo is a copy of that struct, so growing it costs RAM twice.
- Debug hooks beyond the ones README lists (G, W, O, C), in `debugHook` in src/states/Screens.cpp: `$ n` Vegas bank, `J <T|P|D|O|S>` jump to a screen, `H` the table as one line (what chdrive's `expect KEY=VALUE` and `waitstate` read); simulator only: `X` the tallest possible column, `Z` a power cycle (reloads the save), `Q` timing calibration.
- tools/scripts/gameplay.txt (the README reel) is a real deal (`say G 1`) whose moves were worked out by the host tests' sensible player and written out as glove moves (`say C pile depth`); the one-off generator is not in the repo. Any change to dealing or the RNG invalidates the move list.
- Showcase and gameplay scripts write straight into docs/: delete docs/sheet.png and docs/result.png afterwards.
- tools/make_logo.py renders the title once from a TrueType font (Georgia Bold Italic by default; pass another path as the first argument). tools/art/logo.txt is the source from then on; re-running overwrites hand edits. Then `python tools/assets.py`.
- The card face (ranks, pips, court busts, suit glyphs in tools/art/) and the 3x5 font come from CHBlackjack/CHPoker; assets.py does not cross-check them against those siblings.
- Simulator: `../../tools/chsim/chsim.py` (shared; this game's tools/chsim/chdrive.py imports it). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `../../tools/check_size.py` (`tools/device.py build` runs it).
