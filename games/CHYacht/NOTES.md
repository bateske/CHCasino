# CHYacht — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHYacht at commit 751b026 (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 43,476 of 50,944 B (7,468 spare; the image is 43,732 B, so both save pages fit with 4,700 B to go), static RAM 14,996 of 18,416 B (3,420 spare).
- Verification: simulator and host tests only, all passing as of 2026-10-01 (not re-run since the import). There is no tools/check.py here; run these by hand:
  - `tools/tests/run_tests.py`: the rules against an oracle over all 7,776 rolls, the house player's self-play and the paytable's return, the 3D dice physics at every power for every set of kept dice.
  - `tools/tests/sim_save.py`: save mid-turn, reboot, continue; a finished game leaves nothing to continue but keeps the purse.
  - The scripts in tools/scripts (look, modes, perf, showcase, gameplay) through `tools/chsim/chdrive.py --sim .`.
  - `python tools/device.py build` for the release image and its size.
- Never run on a CHGame. The dice cam is estimated at 10-14 ms a frame in the simulator (about 30 fps during the throw, like CHCraps); unmeasured.

## Design decisions

- Chosen (owner): name "Yacht Dice"; the trademarked name of the commercial game is never used. Five of a kind is a "YACHT".
- Chosen (owner): three modes: solo score attack, vs the dealer (CPU), pass-and-play.
- Chosen (owner): bankroll and ante; the final score pays on a paytable; YACHT and the upper bonus pay a bonus.
- Chosen (owner): CHCraps's 3D roll engine, then a close-up tray for holds. Rejected: CHCraps's backstop ("clearly a craps table"); now a padded leather back wall with brass studs, wooden rails and an inlaid line on the baize (`Cam.cpp` tray drawing).
- Chosen (owner, after seeing it): its own slab-style title lettering (tools/art/logo.txt) and 3D dice 25% smaller (`EDGE = 12` in src/cam/Dice3D.h). The owner approved the game as built.
- Accepted without comment: pass-and-play is score-only (no purse); tray dice are 2D; one CPU strength (strong: mean ~238); kept dice wait on a plate in the cam's corner; each seat throws its own dice colour (src/render/Chips.cpp `dieColours`).
- Chosen: paytable 260/300/350/400/500 pays 1/2/3/5/10 antes; a YACHT pays the ante again, the upper bonus a fifth of it. Rejected: the plan's 5x / 1x bonuses (the return would have been far over 100%); it is now ~98.4% against the house player (tools/tests/test_yacht.cpp prints the figures).
- The roll never comes from the physics: src/game/Yacht.cpp rolls, then the cam simulates the throw ahead and repaints the pips so the dice land on the rolled numbers.

## Open items

- Fixed 2026-10-01 (with the SD game menu, which makes switching games routine): the save magic in src/save/Save.cpp was `0x52434843`, CHCraps's "CHCR"; it is now "CHYD" = `0x44594843`, as its comment always said. A save written by an older build is ignored once. The debug handshake was already CHYD.
- Device run: dice cam frame times, the feel of shaking and throwing, sounds, saving across a power cycle. Device debug builds are `CHYD_LEAN` (no saving, no Options/Stats pages; `-DCHYD_FULL` keeps them). Put the release build back afterwards.

## Gotchas

- Dice outcomes must stay in the rules: never let src/cam decide a value. For scripts, `say R seed` fixes the dice and `say F a b c d e` forces the next roll (up to 4 queued).
- src/cam (Cam.*, Dice3D.*) is a fork of CHCraps's (../CHCraps), grown from two dice to five; fixes do not flow between the two automatically.
- Drawing is CHBlackjack's band redraw (seats and card, tray, bar repainted only when they change), but the dice cam repaints the whole screen every frame: it is the frame-time hot spot.
- The house player (`ai::` in src/game/Yacht.cpp) looks one roll ahead over all 32 hold masks and is spread over a few frames while it "shakes"; `say A 1` lets it play every seat for whole-game scripts.
- tools/assets.py clones Press Play On Tape's Blackjack into tools/.cache/ppot (gitignored) pinned to `PPOT_COMMIT`: needs git and network on first run. tools/art/dealer.png (CHBlackjack's hand-painted dealer) replaces the recoloured PPOT bust; the chips are tools/art/chip_*.txt (CHBlackjack's chip, captured).
- Debug hooks (list at the end of src/states/Screens.cpp): `R`, `F`, `J <T|P|C|2|3|4|O|S|E|L>`, `M purse`, `V` reboot (reload from the save), `A 0|1`, `C focus index`, `G box`, `H` state line, `Q` (sim) timing calibration.
- tools/tests/sim_save.py drives the simulator through this game's tools/chsim/chdrive.py and the shared chsim; it builds the simulator itself.
- Simulator: `../../tools/chsim/chsim.py` (shared). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `../../tools/check_size.py` (`tools/device.py build` runs it).
