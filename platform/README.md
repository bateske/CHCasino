# platform/

Everything the games build against, copied in at the versions they were
built and verified with on 2026-10-01. With these copies the repository is
self-contained: a build needs no other download except the board package's
toolchain, which the Boards Manager installs.

| Folder | What | Version | Upstream | Licence |
|---|---|---|---|---|
| `board/arduino/CHGame/` | The CHGame Arduino board package: core, variant, linker scripts, bootloader binary, `boards.txt` / `platform.txt` | 0.2.4 | [bateske/CH32SerialBoot](https://github.com/bateske/CH32SerialBoot) tag `v0.2.4` (5de3006), folder `arduino/CHGame` | MIT (`board/LICENSE`, `board/THIRD-PARTY.md`) |
| `board/docs/` | The board's docs: hardware pin map, flash/RAM map, boot flow, upload protocol, recovery, CH32X035 gotchas, building the bootloader | 0.2.4 | same tag, folder `docs` | MIT |
| `libraries/CHGfx/` | The graphics library | 1.3.0 | [bateske/CHGfx](https://github.com/bateske/CHGfx) tag `1.3.0` (838bbb0) | MIT (+ font notices in its `LICENSE`) |
| `libraries/CHSd/` | Read-only SD card + FAT16/32 library. **This is now its master copy.** | 1.0.0 | CHSd had no repository of its own before CHCasino | MIT |
| `bootloader/` | The bootloader with the SD game menu: sources, PC test suite, built binaries. **Developed here**; started as CH32SerialBoot's `bootloader/` (+ `shared/`, `host/py/`, `test/`) | 0.2.4 + the SD menu (BOOT_VERSION 2) | [bateske/CH32SerialBoot](https://github.com/bateske/CH32SerialBoot) tag `v0.2.4` (5de3006) | MIT (+ BSD font, `bootloader/NOTICE`) |
| `hardware/` | Rev 0 schematic (PDF) and netlist (EasyEDA `.tel`) | 2026-08-21 | | |

## The board package (`board/`)

**Installing.** Install the board package through the Arduino Boards
Manager. That also installs the RISC-V GCC 8.2 toolchain, `chgame-upload`
and `wchisp`:

```bash
arduino-cli config add board_manager.additional_urls https://github.com/bateske/CH32SerialBoot/releases/latest/download/package_chgame_index.json
arduino-cli core update-index
arduino-cli core install CHGame:ch32v@0.2.4
```

**What the copy here is for:**
- reading the core, variant and linker script (the pin names are in
  `variants/CH32X035/CHGame/variant_CHGame.h`; the menus and their flags in
  `boards.txt`);
- a reference for exactly what the games were built with.

It is a byte-for-byte snapshot of `arduino/CHGame` and `docs/`. The board
docs sometimes refer to `bootloader/`, `host/` or `tools/` paths: those
exist only in the CH32SerialBoot repository. The bootloader binary itself is
in `board/arduino/CHGame/bootloaders/CHGame/`.

**Gaps in the docs:**
- `board/docs/hardware-pinmap.md` leaves some ports as "—". The variant
  header has them all; the summary is in
  [../docs/platform.md](../docs/platform.md).
- The core's `README` (Tools menus, `Serial` behaviour, memory report) is
  the CH32SerialBoot README. The parts that matter for the games are in
  [../README.md](../README.md) and [../CLAUDE.md](../CLAUDE.md).

## CHGfx (`libraries/CHGfx/`)

The games compile against this copy:
- `tools/device.py` passes `--library <repo>/platform/libraries/CHGfx` to
  arduino-cli. That takes priority over a CHGfx installed in the sketchbook.
- The simulator (`tools/chsim/chsim.py`) uses its `src/` unless
  `CHSIM_CHGFX` points elsewhere.
- Arduino IDE users copy this folder into their sketchbook's `libraries/`.

**Documentation:**
- Its README documents the API, draw modes and configuration macros.
- Its README links `../PERFORMANCE.md`. In CHCasino that document is
  [../docs/performance.md](../docs/performance.md).
- `extras/sim` is CHGfx's own small simulator, used to test the library
  itself. It is a different program from CHCasino's `tools/chsim`, which
  runs whole games.

## CHSd (`libraries/CHSd/`)

The SD reader of CHWords, CHCrossword and CHWordWheel. Each of those games
carries a generated copy of it, because a sketch compiles only its own
folder.

To change it:

```bash
cd platform/libraries/CHSd
python tests/run_tests.py          # FAT16/FAT32 images, every failure mode
python tools/vendor.py             # regenerate the games' copies
python tools/vendor.py --check     # verify (CI-style)
```

Then run `tools/check.py` in each of the three games. Its `tools/fatimg.py`
builds and reads FAT16/FAT32 card images; it is useful for any SD work.

## Refreshing a vendored piece

**CHGfx or the board package:**
1. Copy the new release over the folder. CHCasino's builds used
   `git -c core.autocrlf=false -c core.eol=lf archive <tag>` so the bytes are
   exactly as committed.
2. Update the table above.
3. Rebuild every game. `python tools/device.py build` in each game must still
   fit, and the simulator frames must be unchanged or deliberately changed.
4. For a new board package version, also install it (`core install
   CHGame:ch32v@<version>`) and update the version in `CLAUDE.md` and the
   README.

## The bootloader (`bootloader/`)

The board's permanent bootloader with a game menu that installs games from
the SD card ([../docs/sd-menu.md](../docs/sd-menu.md)). Its first commit in
CHCasino is the unchanged CH32SerialBoot 5de3006 copy (with `bootloader/`
flattened into the folder); everything since is the SD menu work, ready to
go upstream. [bootloader/README.md](bootloader/README.md) has the design,
the differences from 0.2.4, building, testing and installing.

**It does not replace the board package.**
- `board/arduino/CHGame/bootloaders/CHGame/chgame_bootloader.bin` is still
  the 0.2.4 bootloader, and the IDE's *Burn Bootloader* writes that one.
- Games build against core 0.2.4 unchanged; the menu bootloader is
  compatible with it.
- To ship the menu through the Boards Manager, a board package release must
  carry `bootloader/release/chgame_sdboot.bin`.

## Problems found in the board package (for upstream)

- **Linux builds fail.** `cores/arduino/ch32/lib/ch32yyxx.h` includes
  `core_riscv_cH32yyxx.h`; the file is `core_riscv_ch32yyxx.h`. It works only
  on case-insensitive file systems (Windows, default macOS). CLAUDE.md gives
  the symlink workaround.
- **Stale comments.** Several say the bootloader is 8 KB and the app starts
  at 0x2000 (`link_chgame_app.ld`, `chgame_map.h`); the code says 12 KB and
  0x3000.

## Local changes

None. Every file here is as released upstream, except
`libraries/CHSd/tools/vendor.py` and `libraries/CHSd/tests/run_tests.py`.
Their paths now point at `games/` and the new folder depth, and their
README describes the CHCasino layout.
