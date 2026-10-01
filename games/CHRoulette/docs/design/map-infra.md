# CHRoulette infrastructure map: what to copy from CHChess and CHBlackjack

Paths below are relative to `` (`BJ/` = CHBlackjack, `CH/` = CHChess). I read the files fully, diffed both games with `--strip-trailing-cr`, and ran `check_size.py` against the existing build maps. That command is read-only.

## 0. Environment facts that matter
- **CHGfx version.** The working CHGfx is **1.3.0**, installed at `<sketchbook>\libraries\CHGfx`. `arduino-cli config get directories.user` returns `<sketchbook>`, and `chsim.py` finds the library that way.
  - `CHGfx` is **v1.1.0 and stale**. Do not use it.
  - `tools\chsim` is an older copy of the simulator that differs from BJ's (`chgfx_host.cpp` 130 lines, `chsim.py` 66 lines). Ignore it.
- **Board package.** `arduino-cli core list` shows `CHGame:ch32v 0.2.4`. Python has Pillow 12.1.1 and pyserial.
- **C++ compiler.** None is on the PATH. Zig exists at `<zig>`.
  - Set `CHSIM_CXX="<zig> c++"`.
  - `chsim.py:58-60` does `env.split()`, so the path must contain no spaces. This one has none.
- **Line endings.** BJ files are CRLF and CH files are LF. `.gitattributes` normalises them.

## 1. Budgets and measured sizes (from `check_size.py` on the existing `build/` maps)
| Build | Image (flash) | Static RAM |
|---|---|---|
| BJ release | 45,832 B | 15,736 B |
| BJ debug | 46,532 B | 15,856 B |
| CH release | 48,884 B | 17,880 B |
| CH debug | 49,744 B | 17,912 B |

- **Limits** (`check_size.py:21-22`):
  - `FLASH_LIMIT = 50944`. That is 0xF700 − 0x3000: the app starts at 0x3000 and the metadata page is 0xF700.
  - `RAM_LIMIT = 18416`. The 2,048 B stack is separate.
  - Two save pages need image ≤ **50,432** B (`check_size.py:83-85`).
- **Fixed SRAM**, from the map:
  - `gfx_fb` 8192
  - CHGfx `s_chunk` 1024 and `s_lut` 1024
  - `wch_usbcdc_EP2_buffer` 128
  - `arduboy` 40
  - fx `parts` 480
  - BJ's save `buf` 256 (CH avoids this, see §3)
- **Flash costs documented in comments and READMEs:**
  - LTO saves ~3.8–3.9 KB (`BJ/tools/device.py:9`, `CH/docs/CH32SerialBoot-notes.md:19`).
  - `periph=game` saves ~3.4–4 KB (`BJ/config.h:5`, `CH/config.h:5`).
  - `usb=uploadonly` saves ~0.6 KB (616 B on CH).
  - The debug protocol costs ~1.8 KB (`BJ/config.h:12`) to ~2 KB (`CH/config.h:23`).
  - Saving costs ~1 KB (`CH/docs/CH32SerialBoot-notes.md:96`).
  - BJ's sequencer is 1.8 KB, against 6.5 KB for CHGameSound. Sound plus music is 2.9 KB (`BJ/README.md:106-115`).
  - `snprintf` would cost 3.5 KB; `src/gfx/Fmt.*` replaces it and is identical in both games.
  - `pinMode` would cost 2 KB of pin tables.
  - BJ's no-LTO breakdown: core+USB 5.1 KB, CHGfx 8.7 KB.
- **Performance:**
  - Flash has 3 wait states: ~5 cycles per instruction from flash against ~2 from SRAM.
  - An async full flush costs ~2.6 ms of CPU on CHGfx 1.3 (~5 ms on 1.2) (`CH/src/Frame.cpp:22-23`).
  - A full frame takes 8.37 ms on the wire at 12 bpp (`chgfx_host.cpp:39-41`).
  - A frame counts as "late" above 17,500 µs (`Debug.cpp`, BJ:202 / CH:238).

## 2. Audio (`src/audio/`)
**Common engine (copy as is).**
- TIM1 CH2 drives the piezo on PB10, partial remap.
  - `hwInit()`: `PSC=47` gives a 1 MHz clock, `CHCTLR1=0x6800` (PWM1 + preload), `BDTR=0x8000` (MOE), `CCER=0x10` (BJ:108-127, CH:101-120).
  - GPIOB CFGHR is write-only, so it goes through `extern "C" volatile uint32_t CFGHR_tmpB` (BJ:100, CH:93).
  - The status LED is on PB9.
- Effects are stepped from the core's 1 kHz SysTick weak hook, `extern "C" void osSystickHandler(void)` (BJ:206, CH:141).
  - Step format: `struct Step { uint16_t hz, endHz, ms; }` (6 B). BJ's `Audio.h:5` comment saying "8 bytes" is wrong.
  - Macros: `#define S(hz,end,ms)` and `REST(ms)`.
  - Sweeps are linear: `hz + (endHz-hz)*fxT/ms` (BJ:213, CH:147).
  - Table: `struct SfxDef { const Step *steps; uint8_t n, prio; }` with `DEF(a,p)`. `DEFS[(int)Sfx::COUNT]` must follow the enum order.
- **Priority.** A new effect is dropped if its `prio < fxPrio`; equal priority interrupts (BJ `play()` 244-250, CH `sfx()` 171-179).
- **Tone.** `period=(1e6+hz/2)/hz` with 50% duty (`tone()` BJ:133-155, CH:122-139). Keep effects in the piezo's 1–4 kHz sweet spot.
- **Simulator stub.** `#ifdef CHSIM` gives a silent stub (BJ:17-33). CH's stub also records `uint8_t audio::simLast` (CH:14-23) so scripts and tests can check which sound fired.
- **LED.** `audio::led(Led)` with `LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY`. `update()` runs once per logic tick: BLINK lasts 12 ticks, TRIPLE 48 (period 16), PARTY 240 (period 8).
- `#pragma GCC optimize("Os")` at the top of the file.

**BJ-only features worth taking.**
- `void blip(uint16_t hz, uint16_t ms)` (BJ:257-263): a prio-0 dynamic tone, refused while an effect with prio > 1 plays. It is used for typewriter text and the rolling-purse ticks (`Presenter.cpp:383,391`). Use it for the ball clicking over frets.
- Music: `music(Song, bool loop)`, `stopMusic()`, `loopMusic(bool)`, `musicPlaying()`, `mute(bool)`/`muted()` (the SELECT toggle, not saved), and `setMode(0 off / 1 arpeggio / 2 lead)`.
  - Score interpreter: `scoreTick` 164-181. Playtune bytes: `0x9c n` note on, `0x8c` note off, two bytes big-endian wait in ms, `0xE0` restart, `0xF0` stop.
  - `musicHz()` 184-204: Lead mode holds `LEAD_HOLD_MS=20`; Arpeggio rotates voices every `ARP_MS=6`.
  - `noteHz()` shifts the top octave (C8..B8) down.
  - `tone(hz, smooth=true)` for music changes only `ATRLR`/`CH2CVR`, which avoids clicks (BJ:143-147).
- `Audio.h`:
  - `enum class Sfx : uint8_t { Deal, Flip, Chip, Cursor, Select, Deny, Win, Blackjack, Bust, Push, Lose, Peek, Shuffle, Coin, Split, Double, Insurance, Broke, Reveal, Whoosh, COUNT }`
  - `enum class Song : uint8_t { Title, Victory, Broke }`
  - `bool begin(uint8_t mode)`
- Option mapping: `soundMode(opt)` maps opt 0/1/2 to lead/arpeggio/off (`Screens.cpp:49`).

**CH-only feature worth taking.**
- The `soft` flag gives a narrow pulse, duty `period/8` (CH:89, 135). It is set by `soft = s >= Sfx::Tick` (CH:177).
- The soft effects are `TICK {1100,0,3}` and `TOCK {850,0,3}`, kept last in the enum (`Audio.h:12`). This fits a quiet, ratcheting wheel or ball tick.
- CH has no music; its fanfares are effects. API: `begin(bool)`, `setOn(bool)`, `sfx`, `playing()`, `update`, `led`.

**Reusable step tables.** Several are casino-flavoured and apply directly to roulette:
- `CHIP {3100,0,12}{REST 9}{3700,0,26}`
- `COIN {2800,0,10}{3700,0,28}`
- `WIN` (C7-E7-G7-C8)
- BJ `BLACKJACK` = CH `MATE` fanfare
- `LOSE` and `BROKE` descending sweeps
- `WHOOSH {1200,3800,90}`
- `SHUFFLE` (BJ:57-60), a click train with a sweep
- `CURSOR {2100,0,10}`, `SELECT`, `DENY {900,650,70}`

**Music generator** (`BJ/tools/make_music.py`).
- `SONGS` dict: `eighth_ms` and up to 4 voice strings of `"NOTE:len"` tokens (`C6`, `F#5`, `Bb6`, `R`), lengths in eighths.
- Each note-off is placed 12 ms early (`:67`), which is why `LEAD_HOLD_MS=20`.
- The `loops` dict (`:99`) chooses `0xE0` or `0xF0`.
- It writes `src/audio/Music.cpp` with the scores inside `#if !CHBJ_DEBUG`, exposed through `music::get(song, loop, data, n)` (`Music.h`). Every debug build, the simulator included, has no music.
- Score sizes: TITLE 453 B, VICTORY 169 B, BROKE 164 B, 786 B in all.
- For a new game, rename the `CHBJ_DEBUG` guard and replace `SONGS`.

**Preview tool** (`tools/audio/preview.py` + `host/harness.cpp` + `host/Arduino.h`).
- Compiles the real `Audio.cpp` against a TIM1 register model.
- Writes 48 kHz WAVs and prints `restarts=` (audible clicks) and an FNV hash for comparing versions.
- `host/Arduino.h` is identical in both games. BJ's harness takes `MODE song|sfx INDEX MS`; CH's takes `INDEX MS`. The `SFX` name list in `preview.py` must match the enum by hand.

## 3. Save (`src/save/`)
**Common (copy as is).** Flash routines mirrored from `CH32SerialBoot/bootloader/src/flash.c`:
- `PAGE=256`, `PAGE_A=0xF500`, `PAGE_B=0xF600`, metadata page 0xF700. The bootloader erases only the pages a new image occupies, so these survive re-uploads (proved by `BJ/tools/probes/FlashProbe/FlashProbe.ino`).
- `RAMFUNC(save) static void pageWrite(uint32_t addr, const uint32_t *w)` (BJ:61-90, CH:70-99):
  - masks interrupts through CSR 0x800 (`irq & ~0x88`);
  - unlocks with `KEYR` and `MODEKEYR` = 0x45670123/0xCDEF89AB;
  - erases the page, resets the buffer, loads 64 words (`CR_BUF_LOAD`), then programs and relocks;
  - addresses are `PROG(a)=a+0x08000000`.
- `imageEnd() = &_data_lma + (&_edata - &_data_vma)`.
  - `twoPages()` requires `imageEnd() <= PAGE_A`.
  - `available()` requires `!broken && imageEnd() <= PAGE_B`.
  - If a write verifies wrong (`memcmp`), `broken = true`.
- `crc32` (poly 0xEDB88320) covers `sizeof(Record)-4`.
  - `valid()` checks magic, version and CRC.
  - `static_assert(sizeof(Record) <= PAGE)`.
- **A/B selection.** Odd `seq` goes to PAGE_B, even to PAGE_A. The newest is chosen with `(int16_t)(a->seq - b->seq) > 0`.
- **Simulator.** `static uint8_t simFlash[2][PAGE]` makes save/continue flows scriptable (BJ:106-116, CH:112-121).
- **All CHGame games share these two pages.** Only `MAGIC` separates them: BJ `0x4A424843` "CHBJ", CH `0x53434843` "CHCS". A foreign record reads as invalid, so the game starts from defaults. CHRoulette needs its own magic; for example "CHRL" would be `0x4C524843`.

**BJ variant (`Save.cpp`, 154 lines).**
- `VERSION=1` (`uint16_t`).
- `Record { u32 magic; u16 version, seq; i32 purse; u8 hasGame, pad[3]; Options opt; Stats stats; u32 crc; }`.
- API: `bool load(Round&, bool &hasGame)` (purse restored only if `hasGame && purse > 0`) and `bool store(const Round&, bool hasGame)`.
- `writePage` uses a static 256 B buffer, costing 256 B of SRAM.
- BJ `Options` (8 B, `Round.h:28-37`): rules, goal, speed, sound, theme, fourColour, totals, dealer.
- BJ `Stats` (`Round.h:39-43`): `u32 hands, won, lost, pushed, blackjacks; i32 bestPurse, biggestWin; u16 gamesWon, gamesBroke`.
- Caller (`BJ/Screens.cpp:61-65`): `persist()` returns early in demo mode, then calls `gfx_wait()` and `save::store`.

**CH variant (`Save.cpp`, 167 lines).**
- `VERSION=2` (`uint8_t`, "three opponents").
- `Record { magic; u8 version, hasGame; u16 seq; Options; Stats; match::Record game; crc }`.
- The page is built in **`gfx_chunkScratch()`** (CH:149), so it uses no SRAM. It must be called after `gfx_wait()`.
- `best()` helper. With `CHCH_LEAN` everything is stubbed (CH:11-17), because device debug builds do not fit with saving.
- `Options` and `Stats` live in `Save.h:14-27`. Spare fields are kept as `unused` so old saves keep their layout.

**Recommendation for CHRoulette:** BJ's record shape (purse/bankroll, `hasGame`, `Options`, `Stats`) combined with CH's chunk-scratch write.

## 4. Debug protocol (`src/debug/`), CHSIM glue and `config.h`
**`config.h` pattern** (identical structure; prefix CHBJ_/CHCH_):
- `X_VERSION` string.
- `X_DEBUG`: 1 under `CHSIM`, else 0. It can be overridden with `-DX_DEBUG=1`.
- `X_LEAN = X_DEBUG && !CHSIM && !X_FULL`. BJ drops credits and music; CH drops saving, options and credits.
- `X_PROFILE` 0; `X_FPS 60`.

**Line-based ASCII over USB CDC** (`Debug.h` headers document it). Unknown input is never answered, so it cannot confuse `chgame-upload`.

| Cmd | Reply | Notes |
|---|---|---|
| `?` | BJ `CHBJ 1.0 frame=N lock=N`; CH `CHCS 0.1` | handshake prefix |
| `S` | `FB <frame> 8224\n` + 8192 B fb (4 bpp, even x in low nibble, 64 B/row) + 32 B RGB565 palette from `gfx_paletteOut(i)` | fade included; calls `gfx_wait()` first |
| `K <hex>` | `OK` | `arduboy.injected`, ORed into the buttons |
| `L1`/`L0` | `OK [frame]` | lockstep on (0) / off (−1) |
| `N <k>` | `OK <frame>` | sent when lockstep reaches 0 again |
| `P` | `PERF ...` | BJ: upd, wait, rnd, max, late, frames. CH: rnd, max, late, frames, stk, fstk (painted-stack high-water mark, CH:33-52). Simulator adds `pcrnd`/`pcmax` (host ns) |
| `T` | `PROF i=us ...` | only with `X_PROFILE`; 12 slots via `dbg::profStart()` / `dbg::prof(slot)` |
| `B` | none | device only: `chgame_enter_bootloader()` |
| other | hook | BJ: `OK` if `hook` returns true, otherwise silence. CH: `OK`/`ERR`, plus `HELD` via `holdGame()` while the CPU searches |

- **Reserved letters.** Game hooks must not use `? S K L N P T B`.
- **Line buffer.** BJ `line[48]`; CH `line[100]` (FEN strings).
- **Output.** `out()` writes straight to the CDC endpoint with `CDC_write_nb`, a 25 ms timeout per byte, then `CDC_flush`, because `Serial.write` flushes every byte. In the simulator it is `sim_out` to stdout.
- **Helpers.** `dbg::parseNum(const char *&p, uint8_t base)` skips spaces and commas; `dbg::print`. The protocol uses `fmtStr`/`fmtInt` from `src/gfx/Fmt.h`, so it depends on Fmt.
- **Timing marks.** BJ: `markUpdateStart`, `markWaitStart`, `markRenderStart`, `markRenderEnd`. CH drops `markWaitStart`. Without `X_DEBUG` everything is inline no-ops.
- **Hook examples.**
  - BJ `.ino:17-36`: `R <seed>`, `D c1,c2,..` (stack the deck), `J <T|P|W|L|O|S|C>` (jump to a screen).
  - BJ `debugJump` resets `frameCount=0`, `pal::resetClock()` and `fx::reseed()` so device and simulator frames match (`Screens.cpp:116-131`).
  - CH `Screens.cpp:619-761`: `G M W Y`, and in the simulator only `J Q R H V X`. `Q` returns `CAL` host-ns timings for the cost model.
  - **A roulette equivalent would be `R <seed>`, a "force next pocket" command, and `J`.**

**`CHGame.*` (input and pacing).** Byte-identical between the games except one comment on line 1. Copy verbatim.
- Button masks `A=1, B=2, UP=4, DOWN=8, LEFT=16, RIGHT=32, START=64, SELECT=128` (`CHGame.h:16-23`).
- `chgame_readButtons()` reads INDR directly: PB1 A, PB6 B, PB4 UP, PC14 DOWN, PB3 LEFT, PC15 RIGHT, PB8 START, PB7 SELECT.
- `boot()` sets pull-ups through registers, not `pinMode`.
- `nextFrame()` uses a µs accumulator, resyncs when more than 3 periods behind, and honours lockstep.
- `repeat(b, delay=18, rate=5)`, `justPressedMask()`.
- Global instance `CHGame arduboy`.

**`RamFunc.h`.** `RAMFUNC(name)` expands to `__attribute__((section(".gnu.linkonce.r.chbj." #name), noinline))` (CH uses `chch`; use a new prefix). In the simulator it is just `noinline`. The section name saved 192–260 B.

**Main loop template** (`BJ/CHBlackjack.ino:38-69`; use this, not CH's `Frame.cpp`, which exists for the engine callback and side stack).
- `setup()`: `arduboy.boot(); gfx_begin(GFX_DIV2, GFX_12BPP); pal::init(); screens::begin(); setFrameRate(FPS); dbg::hook = ...`
- `loop()`:
  1. `dbg::poll(); if (!nextFrame()) return;`
  2. do { `pollButtons; pal::tick; screens::update` } while `++ticks < 3 && nextFrame()` (up to 3 catch-up logic ticks per drawn frame)
  3. `pal::commit(); gfx_wait(); render(frameCount); gfx_flushAsync();`
- `audio::update()` is called inside `screens::update` (BJ `Screens.cpp:683`).

## 5. `tools/`: generic vs game-specific
**Identical in both games (pure template):**
- `chsim/chsim.py`, `chsim/fbimage.py`, `chsim/host/Arduino.h`, `chsim/host/chgfx_host.cpp`
- `serialcap.py`, `requirements.txt`, `audio/host/Arduino.h`
- `.gitignore`, `.gitattributes`, `LICENSE`

**Near-identical (rename the game, prefix or handshake):**
- `host/main.cpp`: CH adds `sim_waitInput()`; take CH's.
- `host/sim.h`: same change as `main.cpp`.
- `check_size.py`: docstring only.
- `device.py`: the `-D<PFX>_DEBUG=1` flag and the FQBN variable names.
- `tests/run_tests.py`: the source list.

**Diverged (take CH's superset):**
- `chdrive.py`: 145 differing lines. CH adds `--id` (default `CHCS`; BJ hard-codes `CHBJ` at :141), `-v`, `freegif`, `rec start/stop`, `step`, `cal`, the `pcrnd`-to-device-ms perf estimate, HELD/ERR handling, and chess-only `goto`/`board`/`waitturn`.
- `gifsheet.py`: CH only.
- `audio/preview.py` and `harness.cpp`: BJ's has music; CH's is effects only.

**Fully game-specific:** `assets.py` (551 differing lines), `make_music.py`, `tests/test_*.cpp`, `scripts/*.txt`, and CH's `sheet.py`, `pieces.py` (needs numpy, which is not in `requirements.txt`) and `book.py`.

**`chsim.py build <sketch> [-D N=V]`.**
- Writes `build/<Name>/sketch_ino.cpp`, concatenating the `.ino` files with `#line`. The folder name must equal the `.ino` name.
- Compiles `sketch/src/**/*.cpp|.c`, all of CHGfx's `src/*.cpp` except `CHGfx.cpp`, and `host/*.cpp`.
- Flags: `-std=gnu++17 -O1 -g0 -w -DCHSIM -DCH32X035 -DARDUINO=10800`.
- Output: `tools/chsim/build/<Name>/sim.exe`.
- CHGfx lookup order: `$CHSIM_CHGFX`, then `<sketchbook>/libraries/CHGfx/src`, then `CHGfx*`.
- Compiler lookup order: `$CHSIM_CXX`, zig, the `ziglang` pip package, clang++, g++.

**The simulator.**
- Time is virtual; each `loop()` adds 100 µs (`main.cpp:99`).
- It starts in lockstep 0 and blocks on stdin after 2 idle loops.
- `chgfx_host.cpp` models flush time at 40 µs + 0.511 µs/px (12 bpp).
- It reports `BUG:` and exits with code 3 when the framebuffer changes during an async flush, or when `gfx_chunkScratch` is written mid-flush. `chdrive` then fails.

**Script format** (`chdrive.py` docstring, BJ:9-19 plus CH extras).
- One command per line; `#` starts a comment.
- Commands: `wait N`, `tap BTN[+BTN] [H=3]`, `hold`, `release`, `snap NAME`, `gif NAME N [EVERY]`, `say TEXT`, `free SECONDS`, `perf`, `prof`. CH adds `rec start [E]`, `rec stop NAME`, `freegif NAME S E`, `step N`, `cal`.
- `tap` is `K mask` → `N H` → `K 0` → `N 1`.
- `snap` saves at 3x and builds a 4-column `sheet.png`. `gif` saves at 2x with `duration=1000*every/60`.
- Example: `BJ/tools/scripts/showcase.txt` makes the README GIFs, using `say J P` for a fresh table and `say D` to stack the deck.
- `save1.txt`/`save2.txt` test persistence across an upload. `pace.txt` checks ~300 frames per 5 s with `late=0`.

**`assets.py` idiom** (palette identical in both games).
- `PALETTE = [0x000,0xFFF,0x042,0x173,0x4B5,0xBBC,0xE12,0x702,0xFC2,0x741,0x26E,0x125,0xFB8,0x6EF,0xF0F,0xFC2]`
- `NAMES = INK WHITE FELT_DK FELT FELT_LT SILVER RED WINE GOLD WOOD BLUE NAVY SKIN CYAN FX_A FX_B`
- Letters `k w d f g s r m y b u n p c x z`; `' '` and `'.'` are transparent (index 16).
- Loaders:
  - `load_art`: text art in `tools/art/*.txt`. CH's version allows several images separated by blank lines.
  - `load_png`: a palette-exact PNG that rejects any off-palette pixel. FX_B is excluded because it shares GOLD's RGB.
- Packers:
  - `pack_span4` produces `w,h`, then per row `n` and n bytes of `(len-1)<<4|colour` (colour 15 = skip, runs up to 16, trailing transparency implicit). This is the sprite4 format.
  - Also `pack_rows1` (MSB-first 1 bpp), `pack_span1`, `pack_cols`.
- Output: `src/assets/Assets.{h,cpp}` ("GENERATED, do not edit") plus 6x previews in `build/assets/`.
- BJ has an `Out` class (`array`/`const`/`write`); CH has `c_array`.

**Build and run commands.**
- Compile: `arduino-cli compile -b CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly <Sketch>`
- Upload: `arduino-cli upload -b CHGame:ch32v:CHGame -p COMx <Sketch>`
- `python tools/device.py build|upload [--debug] [--port]`:
  - debug builds drop `usb=uploadonly` and add `--build-property build.extra_flags=-D<PFX>_DEBUG=1`;
  - outputs go to `build/release` or `build/debug`;
  - it then runs `check_size.py --top 0`;
  - port lookup uses VID:PID `16C0:27DD` at 115200 with DTR.
- `python tools/device.py run SCRIPT OUTDIR` builds and uploads a debug build, then runs `chdrive --device`. `device.py shot OUT.png` takes one screenshot.
- `python tools/chsim/chdrive.py --sim . tools/scripts/showcase.txt docs/` (CH: add `--id CHCS`).
- `python tools/tests/run_tests.py`: zig with `-std=gnu++17 -O1|-O2 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=undefined`, compiling `test_*.cpp` plus `src/game/*.cpp` only (CH adds `-DCHTEST`). The harness is two macros, `CHECK(c)` and `CHECK_EQ(a,b)`, ending with "N checks, M failures".
- `python tools/check_size.py build/release [--symbols] [--top N]`. With LTO the per-file table shows `ltrans` partitions.
- `python tools/audio/preview.py out/audio`; `python tools/make_music.py`; `python tools/assets.py`.

## 6. Repo hygiene template
- **`.gitignore`** (identical in both):
  - `build/`, `tools/chsim/build/`, `tools/tests/build/`, `out/`
  - `tools/.cache/` (BJ's PPOT clone; the comment is stale in CH)
  - `__pycache__/`, `*.pyc`, `docs/mockups/`, `.vscode/`, `.DS_Store`, `Thumbs.db`
- **`.gitattributes`:** `* text=auto`, `*.gif binary`, `*.png binary`.
- **`LICENSE`:** Apache-2.0, identical. CH also ships `LICENSE.MPL-2.0` for the engine.
- **`NOTICE`** pattern from CH:
  - "Copyright 2026 bateske";
  - third-party code with its licence;
  - "From CHBlackjack (Apache-2.0): input/frame pacing, palette, drawing, outlined lettering, effects, sound sequencer, flash saving, debug protocol, simulator and tools", plus credit that the 3x5 font in `src/gfx/Draw.cpp` is Press Play On Tape's;
  - then what is new.
- **`README.md`** sections:
  1. Title and a pitch paragraph.
  2. A GIF table from `docs/*.gif` (2x3, `| ![x](docs/x.gif) |`), with the note "Captured from the PC simulator in `tools/chsim`".
  3. Credits.
  4. Installing: board package 0.2.4+ via the URL `https://github.com/bateske/CH32SerialBoot/releases/latest/download/package_chgame_index.json`; CHGfx 1.3.0 from `github.com/bateske/CHgfx`; folder name must match the `.ino`; Tools menu settings; the arduino-cli lines.
  5. Playing: a button table.
  6. Rules and options.
  7. "How it fits": flash, performance, saving, sound.
  8. Development: one bullet per tool command.
  9. Files tree (BJ).
  10. License.

## 7. Stale copies and comments to fix while copying
- `BJ/src/audio/Audio.h:5` says a step is 8 bytes; it is 6.
- `CH/src/debug/Debug.h:6,11` describes the `?`/`P` replies BJ's way; the actual replies are `CHCS 0.1` and `rnd/max/late/frames/stk/fstk`.
- `CH/src/debug/Debug.cpp:22` refers to `tools/chsim/perf.py`, which does not exist; the calibration lives in `chdrive.py`'s `cal`.
- `CH/tools/scripts/pace.txt` is BJ's file verbatim (`J P` and `R 7` mean nothing in chess).
- Debug hook replies differ: BJ is silent on an unhandled command; CH answers `ERR`. CH's behaviour plus its `chdrive` is the better template.

**Proposed copy set for CHRoulette:**
- `CHGame.*` and `Fmt.*` verbatim; `RamFunc.h` with a new prefix.
- BJ's `.ino` loop and `config.h` with a new prefix.
- BJ's `Audio.cpp`/`Music.*`/`make_music.py`, plus CH's `soft` flag.
- BJ's save record written into CH's chunk-scratch page, with a new MAGIC.
- CH's `Debug.*` (ERR/HELD and stack high-water mark); drop `holdGame`/`waitInput`/`frameStack` if unused.
- The whole `tools/chsim` from CH, including `gifsheet.py`, with the `--id` default changed.
- `serialcap.py`, `check_size.py`, `device.py`, `run_tests.py`, `audio/` and `requirements.txt` with names changed.
- `assets.py` rebuilt on CH's skeleton with the same palette, letters and `pack_span4`.