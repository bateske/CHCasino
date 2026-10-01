# CHCraps — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHCraps at commit fa1fd77 (2026-10-01); develop here now, not in the old repo.
- Release build (`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 49,776 of 50,944 B (1,168 spare), static RAM 15,520 of 18,416 B (2,896 spare).
- The image (`../../tools/check_size.py`'s `image:` line; README quotes 50,032 B) is only ~400 B under the 50,432 B that keeps both A/B save pages (`PAGE_A` 0xF500, `PAGE_B` 0xF600 in `src/save/Save.cpp`). Past 0xF500 saving drops to one page; past 0xF600 it switches off.
- Verification: simulator only. `tools/tests/run_tests.py` (every bet against an independent oracle, exact house edges, dice physics, zone reachability), `tools/tests/sim_save.py` (save mid-hand, debug `V` reboot and continue, the broke case), and the chdrive scripts in `tools/scripts`. There is no `tools/check.py` in this game.
- As of 2026-10-01 it has never run on the device: frame times unmeasured, sound unheard.

## Design decisions

- Chosen: TABLE option CLASSIC / BEGINNER (Beginner keeps line, don't, odds, field, place 6/8 on a roomier layout).
- Chosen: hold A on ROLL to shake, release to throw.
- Chosen: CHBlackjack's split layout (wall, felt, bar) plus the 3D "dice cam".
- Chosen: CHBlackjack's dealer works the table as the stickman and calls every roll.
- Chosen: keep the lit ON puck - a white disc showing the point number in the 3x5 font, pinned to the number box.
- Chosen: the dice show pips whenever they are on screen. The result is repainted at the back-wall hit, choosing the labelling that changes the fewest faces.
- Chosen: music only if flash is left once the game is complete; there is none (`src/audio/Audio.h` says so).
- Architecture to keep: the rules settle the whole roll in `Craps::throwDice()` (a result per spot); the presenter (`src/fx/Presenter.cpp`) only replays it - call, losers swept, pays, home, come moves. Money has already moved, so a save mid-show is always consistent.

## Open items

- First device run. Simulator estimates put the dice cam's shake and the result + banner at 9-15 ms a frame, over the ~8 ms drawing budget for 60 fps. Measure on the board (`python tools/device.py run tools/scripts/perf.txt OUTDIR`) before optimising.
- Listen to the sound effects on the piezo (`tools/audio/preview.py` renders them to WAV meanwhile).
- Known issue (logged, not fixed; see ../../docs/status.md): CHYacht uses this game's save magic `0x52434843` "CHCR" with the same
  version 1, so after playing one, the other accepts its save. The fix is a new magic in CHYacht.
- Music: deferred until flash allows (it does not now).
- `config.h`'s comment says `CHCR_LEAN` leaves out "music, the attract demo"; the code actually stubs saving (`src/save/Save.cpp`) and the Options and Stats pages (`src/states/Screens.cpp`). Fix the comment when next touching it.

## Gotchas

- Flash is effectively full while both save pages are kept. Tactics already in use: chips and pucks are `gfx_sprite4` span sprites recoloured by remap tables (drawn from CHBlackjack's chip, made by `tools/assets.py`); all 24 die orientations come from one walk of quarter turns stored in a 24-bit constant (`labelDie`, `WALK` 0x288A28 in `src/cam/Dice3D.cpp`); the dice share the game's sine table; no music.
- Device debug builds get `CHCR_LEAN` automatically (`config.h`): no saving, no Options or Stats. `-DCHCR_FULL` forces the whole game into a debug build (check it fits).
- Dice3D: integer Euler-angle cubes and a tilting pinhole camera; physics steps once per 60 Hz tick, so the dice keep their speed if a frame is slow. The throw is pre-simulated deterministically and the dice relabelled afterwards: any physics change moves where they land, so rerun `run_tests.py` after touching it.
- Drawing: the table redraws only the bands (wall, felt, bar) that changed or that something moving touched; the dice cam redraws everything each frame, so that is where frame time goes.
- Debug hooks (listed above the hook in `src/states/Screens.cpp`): `R` seed, `F` force rolls, `J` jump to a screen, `M` purse, `V` reboot (reload from flash), `E` set a bet, `X` point, `C` cursor zone, `Z` D-pad route, `H` state, `Q` (simulator) calibration. New hook letters must avoid the protocol's own: `? S K L N P T B`.
- chdrive extras: `goto ZONE` walks the cursor with real D-pad taps on the game's planned route; `idle` waits out the dice cam and the payout.
- Credits: the dealer's art and the 3x5 font are Press Play On Tape's (Simon Holmes / filmote, Stephane C / vampirics) by way of CHBlackjack. Credits are exactly those in NOTICE; add no others.
- Paths moved with the monorepo: the size report is `../../tools/check_size.py`, the simulator `../../tools/chsim/chsim.py`; per-game tools stay in `tools/`. For host builds set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md).
- Device debug builds leave out saving; the board may be in use, so announce a debug upload and put the release back afterwards.
