# CHG game packages: the developer one-pager

What a program needs in order to be installed from the SD card by the
bootloader's game menu ([sd-menu.md](sd-menu.md)). Your sketch itself does
not change: a package is your sketch's ordinary `.bin` with a 512-byte header
in front.

## The target

| | |
|---|---|
| MCU | WCH **CH32X035G8U6** (QFN28), RISC-V RV32IMAC at 48 MHz; marking and `wchisp` report agree |
| Program link origin | **0x3000** (the CHGame board package's `link_chgame_app.ld`, unchanged) |
| Largest program | **50,944 B** (0x3000-0xF6FF). Games that save keep it at 50,432 B or less ([platform.md](platform.md)) |
| Metadata page | 0xF700, written by the bootloader **last**, after the image's CRC checks out |
| RAM | 20 KB, of which the first 16 B (0x20000000) are the retained boot-request block; the board package's linker script reserves them. Stack 2 KB. |
| Boot region | 0x0000-0x2FFF belongs to the bootloader. Nothing in a package can write there: the destination is always 0x3000, and the flash writer checks every page's address itself. |
| Layout id | `0x003000F7` (program at 0x3000, metadata at 0xF700) |

Build exactly as for an upload: the board package `CHGame:ch32v` (0.2.4 or
later); the games use
`opt=oslto,rtlib=nano,periph=game,usb=uploadonly`.

## The file (format version 1)

A package is the 512-byte header followed by the payload. The payload is the
`.bin`, padded with 0xFF to a multiple of 4, which is exactly the image
`chgame-upload` writes. Integers are little-endian.

| Offset | Size | Field | Value |
|---|---|---|---|
| 0x000 | 4 | magic | `0x31474843` ("CHG1") |
| 0x004 | 2 | format version | 1 |
| 0x006 | 2 | header bytes | 512 |
| 0x008 | 4 | target id | `0x35335843` ("CX35": CHGame, CH32X035G8U6) |
| 0x00C | 4 | layout id | `0x003000F7` |
| 0x010 | 4 | payload bytes | 4 to 50,944, a multiple of 4 |
| 0x014 | 4 | payload CRC-32 | CRC-32/ISO-HDLC (zlib's `crc32`) of the payload |
| 0x018 | 4 | app version | free for you |
| 0x01C | 4 | flags | 0 |
| 0x020 | 32 | title | ASCII, NUL-padded; the menu shows the first 19 characters |
| 0x040 | 16 | author | ASCII, optional |
| 0x050 | 8 | version | ASCII, optional (e.g. "1.2") |
| 0x060 | 12 | title image | offset, bytes, CRC-32; reserved for per-game title screens, 0 = none |
| 0x06C | ... | reserved | 0 |
| 0x1FC | 4 | header CRC-32 | CRC-32/ISO-HDLC of bytes 0x000-0x1FB |
| 0x200 | payload bytes | payload | |

**The CRC.** It is the same CRC, over the same padded bytes, that the
bootloader stores in the metadata page after a USB upload. So the menu can
tell that the program in flash is the one in a package (same length and
CRC), and marks it as installed whichever way it got there. The
`app_version` field is not used for that.

**Checks.** The bootloader checks, in this order, with nothing erased yet:
1. magic;
2. header CRC;
3. format version and header size;
4. target and layout ids;
5. payload size against the file size;
6. that the payload is not a bootloader image (signature `0x4C424843` "CHBL"
   at payload offset 8, where WCH's startup puts a reserved 0);
7. then the CRC of the whole payload.

Only then does it erase anything. [platform/bootloader](../platform/bootloader)
has the transaction and its tests.

## Making one

```
python tools/chgpack.py pack build/release/MyGame.ino.bin MYGAME.CHG --title "MY GAME" [--author ME] [--version 1.0]
python tools/chgpack.py verify MYGAME.CHG
python tools/chgpack.py info E:\        # list the packages on a card (or a folder, or a FAT image)
```

`chgpack.py` needs only Python 3. In the Arduino IDE, *Sketch > Export
compiled Binary* writes `MyGame.ino.bin` next to the sketch. Copy the
package into the card's `GAMES/` folder, with an 8.3 file name.

For CHCasino's games, `python tools/sdcard/mkcard.py` builds and packs all of
them (titles in `tools/sdcard/games.json`).

## What a program may assume

- **It always starts from a real reset**, never from a jump out of the menu.
  The bootloader leaves a RUN request and resets, so every peripheral, the
  SD card's SPI bus and the panel are in their reset state. Initialise them
  as on a cold boot (CHGfx and CHSd already do).
- **The SD card may have been left mid-command** by whatever ran before.
  CHSd's `sd::init()` starts from CMD0, which is enough after the bootloader.
- **Returning to the menu.** Call `NVIC_SystemReset()`. With no request in
  the retained block, the bootloader shows the menu.
- **Uploading.** The board package's 1200-baud touch writes the USB request
  (`0x43484742` "CHGB" and its complement at 0x20000000) and resets. That is
  unchanged.
- **Saving.** The flash pages at 0xF500 and 0xF600 survive installs unless a
  program is large enough to cover them. They are shared by every program:
  give yours a unique magic in its save records ([status.md](status.md)).
