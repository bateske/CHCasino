# The SD card

The microSD slot shares SPI1 with the display. Three games read data from
it, and one utility exposes it over USB.

## Who uses it, and with what

| | Driver | Reads | Licence |
|---|---|---|---|
| CHWords | CHSd (generated copy in `src/sd`) | `WORDS.DIC` in the card's root: the full ENABLE word list (4 MB) | MIT |
| CHWordWheel | CHSd | `PHRASES.BNK` in the root: the phrase bank (39 KB) | MIT |
| CHCrossword | CHSd | `CHCW/*.CWD`: puzzle packs | MIT |
| CHSDtoUSB | its own read-write driver (from sdfatlib, CRC-checked, DMA) | the whole card, block by block, for the PC | GPL-3.0 |

The **bootloader** reads the card too: its game menu lists and installs
`GAMES/*.CHG` packages ([sd-menu.md](sd-menu.md)). It uses its own C port of
CHSd (`platform/bootloader/src/sd.c`, `fat.c`), which follows any amount of
fragmentation and recovers a card that a reset left mid-transfer.

The other 17 games never touch the card. Each SD game also works without
one, using its built-in data in flash. The card adds to that.

**CHSd** ([../platform/libraries/CHSd](../platform/libraries/CHSd)):
- **What it is.** A polled SPI block driver and a FAT16/FAT32 reader, about
  1.7 KB of flash and 24 B of RAM.
- **How a game reads a file.** `fat::open("WORDS   DIC", runs, max, buf)`
  finds the file by its 8.3 name once and returns a run count (0 = any
  failure). `fat::read(runs, n, k, buf)` then reads block `k`.
- **The buffer.** Each call borrows a 512 B buffer; the games lend CHGfx's
  chunk scratch, which is idle between `gfx_wait()` and the next flush.
- **Limits.** No writing, no cache, no file object. Its README covers the
  API, the bus rule, the error codes and why it is slow on purpose.

## Rules for code that touches the card

- **Use the card only between `gfx_wait()` and the next flush.** CHGfx
  streams the framebuffer by DMA on the same bus. Hand SPI1 back exactly as
  CHGfx left it.
- **Select the card by its own pin.** The card's chip select is `PIN_SD_CS`
  (PB11). The default `SS` (PA4) is the LCD.
- **Card formats.** Players' cards are FAT16 or FAT32; cards up to 32 GB
  come that way. exFAT (64 GB and up) is reported as `E_EXFAT`, so a game
  can say "reformat as FAT32". A file in more pieces than the run list holds
  is refused (`E_FRAG`).
- **After a failed read, re-initialise** (`sd::init()` or `fat::open`) before
  the next read.
- **Some cards are slow.** They take 300-800 ms to deliver a block that has
  never been written, so CHSd waits up to 1 s per block and CHSDtoUSB up to
  1.5 s.
- **Never edit a game's `src/sd/`.** Change CHSd and run its `vendor.py`.

## In the simulator

The SD games' simulator shims (`tools/chsim/host/VCard.h`, `sd_host.cpp`,
generated from CHSd's `host/`) give the game a pretend card:
- A `.img` file is served as the whole card. CHSd's `tools/fatimg.py` and
  CHCrossword's `tools/puzzles/mkcard.py` make them.
- Any other file (e.g. `sdcard/WORDS.DIC`) is put on a pretend FAT16 card
  built on the fly, in two pieces, so the real FAT code runs.
- Choose the card with `CHWD_CARD=<file>` (CHWords), `CHWW_CARD=<file>`
  (CHWordWheel), or `chdrive.py --card <img>` (CHCrossword). Unset means
  no card.

## Putting files on a real card

**Everything at once:** `python tools/sdcard/mkcard.py` builds every game,
packs them for the menu and lays out the whole card in `out/sdcard/`:
`GAMES/*.CHG`, `WORDS.DIC`, `PHRASES.BNK` and `CHCW/`. Copy its contents to
the card's root.

**From a PC with a card reader:** copy the game's files from its `sdcard/`
folder to the card. Follow the paths above exactly: the root for
`WORDS.DIC` and `PHRASES.BNK`, `CHCW/` for crossword packs.

**Without removing the card:** upload
[CHSDtoUSB](../utilities/CHSDtoUSB). The board becomes a USB drive with its
serial port still working:
1. Copy the files to the drive and eject it.
2. Upload the game again. Upload works while the drive is mounted.

The exact steps for an agent are in [../CLAUDE.md](../CLAUDE.md).

## Status

CHSd has been tested extensively on the PC against FAT16/FAT32 images, and
in all three games' simulators. **It has not yet read a real card on a
board.** The first device session should check:
- a FAT32 SDHC card;
- a small FAT16 card;
- an exFAT card (the error message);
- pulling the card mid-game;
- the time `fat::open` and one `fat::read` take.

CHSDtoUSB has been tested on hardware: 78 checks pass on a test build, raw
reads at about 490 KB/s and writes at about 400 KB/s.
