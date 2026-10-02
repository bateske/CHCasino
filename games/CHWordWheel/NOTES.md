# CHWordWheel — development notes

Agent-facing notes for continuing work here; rules, controls and build are in README.md, the platform in the repo-root CLAUDE.md and docs/.

## Snapshot

- Imported from https://github.com/bateske/CHWordWheel at commit 7c97419 (2026-10-01); develop here now, not in the old repo.
- Release build (FQBN `CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly`, core 0.2.4, CHGfx 1.3.0): flash 50,092 of 50,944 B (852 spare); the image (check_size's `image:` line, 256 B more) is 50,348 B, 84 B under the 50,432 B line that keeps both save pages (2026-10-02, after the core's hold-START exit took 36 B), static RAM 15,244 of 18,416 B (3,172 spare). README's "50,408 of 50,432 bytes" is an older figure; trust check_size.
- Puzzle banks: 111 built in (`FLASH_BYTES = 1376` in tools/phrases/build_bank.py), 606 on the card (sdcard/PHRASES.BNK, magic `WWPB`, version 1).
- Verification: `python tools/check.py` (banks rebuilt, host tests incl. thousands of CPU episodes and both bank readers, every script twice with identical frames, card.txt with PHRASES.BNK in the simulator's slot, the redraw diff check over tools/scripts/diff, release build) passed as of 2026-10-01; not re-run since the import. The whole episode is playable in the simulator.
- Never run on a board: first run at all, render times (simulator estimate 3-6 ms at the busiest), the wheel's feel, sounds, saving across a power cycle, and the SD reader.

## Design decisions

- Chosen (owner): titled WORD WHEEL. The TV show's trademarked name never appears anywhere in the repo, puzzles included. tools/phrases/blocked.txt lists what must never appear and is kept in ROT13 (build_bank.py decodes it) so the repo never spells those names out; the old repo's history was rewritten so it is ROT13 in every commit. Keep it that way.
- Forked from CHRoulette: prefix `CHWW_`, debug handshake CHWW, save magic `0x57574843` "CHWW".
- Chosen (owner): three podiums, each HUMAN or CPU (default You, Dot, Ace), pass-and-play between humans.
- Chosen (owner): the wheel is shown only as the pointer close-up (wedges with printed values sweep past a flipper); no full wheel. The rules draw the stop and the spin is solved to land there.
- Chosen (owner): the series dealer hosts, with bubble patter; board panels reveal themselves (no letter-turner).
- Chosen (owner): a full episode (toss-up, three rounds, final spin, bonus round) plus Quick Play; career total and stats. As many of the show's tropes as possible.
- Chosen (owner): engine first with a built-in flash bank, then an SD bank using the shared SD reader (now CHSd).
- Chosen (owner liked the game, asked for depth): the shading/depth pass, with palette shades and 50% dithers lit from the top left.
- Cut for flash: screen shake, the win-screen sunburst, the victory/broke songs (only the title tune is left), the PPOT end lettering.

## Open items

- Asked of the owner, unanswered:
  - Panel lettering A (bold 5x7, double-struck text: `bold57` in src/render/Board.cpp; built) vs B (doubled 3x5).
  - Converging wedges (built, src/render/WheelStrip.cpp) vs flat wedges.
  - Built-in bank size trade: dropping the mystery/split/wild/prize wedges (~1.6 KB) or the 5x7 lettering via B (~0.85 KB) would each buy roughly 80-100 puzzles per KB.
- The 84 B now under the two-page line could raise `FLASH_BYTES` a little (about 6 more built-in puzzles); the owner's call, alongside the trade above.
- Device bring-up: first run, render times, wheel feel, sounds (audition the WAVs from `python tools/audio/preview.py out/audio`), save across a power cycle, the SD reader incl. pulling the card mid-game. Device debug builds are `CHWW_LEAN` (no Setup/Options/Stats screens; set podiums with `W`; saving and the SD bank stay) and write save pages only after `say E 1`. Put the release build back afterwards.
- Grow tools/phrases/phrases.txt toward thousands of puzzles (the card bank has room; 64 B a puzzle).

## Gotchas

- Size pragma: every size-optimised file begins `#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")`, ~1 KB smaller than plain Os + LTO here. Measured dead ends: `no-ipa-cp` broke the build; making `Sig::add` (src/fx/Presenter.cpp) noinline made it bigger. Hot pixel loops are `RAMFUNC` (SRAM) and unaffected. Probably worth trying in sibling games.
- The game keeps its own primitives and 3x5 font (src/gfx/Draw.*) instead of CHGfx 1.3's versions: CHGfx's text would cost ~2 KB more.
- Nothing fails when the image passes 50,432 B: read check_size's "save pages free: N" after every build.
- Banks: edit tools/phrases/phrases.txt (`CATEGORY|PUZZLE` lines), then `python tools/phrases/build_bank.py` (checks every puzzle fits and wraps it; writes src/bank/BankData.* and sdcard/PHRASES.BNK; `--curve` prints bytes against puzzles kept). check.py rebuilds both in place and only notes a change: commit the regenerated files with phrases.txt.
- In a fresh clone run `python tools/phrases/build_bank.py` (or `tools/check.py`) before `tools/tests/run_tests.py`: the bank test reads tools/phrases/build/bank_ref.txt, which build_bank.py writes and git ignores.
- No-repeat dealing is a Feistel permutation per section fixed by a seed, so the save holds a seed and three counters, not a list.
- SD: `src/sd/*` and `tools/chsim/host/{VCard.h,sd_host.cpp}` are generated copies of CHSd (`../../platform/libraries/CHSd`). Never edit them here: edit CHSd, run its tests (`python platform/libraries/CHSd/tests/run_tests.py`), then `python platform/libraries/CHSd/tools/vendor.py` (`--check` verifies the copies), then this game's tools/check.py (paths from the repo root). SPI1 is shared with the LCD: SD calls only after `gfx_wait()` and before the next flush.
- Simulator card: `CHWW_CARD=sdcard/PHRASES.BNK` (a non-.img file goes on a pretend FAT16 card, so the FAT code runs); `say X 1|0` (sim only) puts the card in or pulls it out.
- Music: the title tune is generated by tools/make_music.py into src/audio/Music.cpp, and every `CHWW_DEBUG` build, the simulator included, leaves the score out. Hear it with tools/audio/preview.py.
- Redraw diff: tools/chsim/diffdrive.py patches the line `bool render(const Show &s, uint32_t frame) {` in src/fx/Presenter.cpp in a temp copy (forcing a full redraw); renaming it breaks the tool.
- Mock-ups: tools/mockup.py with tools/pixkit.py, which reads ../CHBlackjack/tools/art/dealer.png, ../CHChess/tools/art/hand.png and (as a fallback) ../CHChess/src/gfx/Draw.cpp. tools/assets.py checks the dealer and faces come out byte-identical to ../CHBlackjack's src/assets/Assets.cpp.
- Debug hooks (CHWordWheel.ino): `R seed`, `F stops` (0..71 = wedge*3 + peg slot), `U section i`, `C letter`, `V 1|0` solve right/wrong, `M player cash`, `G step`, `W k0 k1 k2`, `J <T|U|P|E|O|S>`, `H` state line, `E 1|0`, `X 1|0` (sim).
- The Arduino core defines `bit` and `DEFAULT` as macros: don't use them as identifiers.
- Simulator: `../../tools/chsim/chsim.py` (shared). Set `CHSIM_CXX` or have zig/clang++/g++ on PATH (see root CLAUDE.md). Size report: `../../tools/check_size.py`.
