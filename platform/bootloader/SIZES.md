# Bootloader size record

All numbers are measured, not estimated. The toolchain is the board
package's riscv-none-embed-gcc 8.2.0. Flags: `-Os -flto -ffunction-sections
-fdata-sections -msmall-data-limit=8 -msave-restore -fno-jump-tables`, and
`--gc-sections` at link time. `./build.sh <mode>` prints the same table,
from `tools/size_report.py`.

**The reservation:** 12,288 B at 0x0000-0x2FFF, unchanged, so `APP_START`
stays at 0x3000 and every game keeps its 50,944 B. The size gate (gate A) is
a boot image of at most 12,288 B with at least 256 B to spare.

## Current builds (2026-10-02)

| Build | `.text` | `.ramfunc` | `.data` | Boot image | Free | `.bss` | RAM in use |
|---|---|---|---|---|---|---|---|
| **release** (menu + USB upload + self-update) | 11,276 | 596 | 76 | **11,948** | **340** | 5,668 | 8,404 |
| locked (menu + USB upload) | 11,060 | 496 | 72 | 11,628 | 660 | 5,668 | 8,300 |
| nomenu (USB upload + self-update, for HW2a) | 5,224 | 596 | 76 | 5,896 | 6,392 | 992 | 3,728 |
| app (the menu as a program at 0x3000, dry run) | 6,964 | 0 | 8 | 6,972 | - | 4,672 | 6,744 |

Notes:
- `.text` includes `.init` and the vector table. `.ramfunc` and `.data`
  count twice: in flash as their load image, and in RAM.
- RAM in use is the 16 B retained block, `.ramfunc`, `.data`, `.bss` and the
  2 KB stack, out of 20,480 B.
- The release `.bss` is mostly the menu's game table (128 x 32 B), one
  512 B sector buffer, the protocol's 512 B frame buffer and its 256 B page
  buffer.
- The binaries, with SHA-256 sums, are in [release/](release). Two
  independent builds give byte-identical files (`tools/dist.sh`).

## Milestones

| Step | Boot image | Free | What changed |
|---|---|---|---|
| Shipped 0.2.4 bootloader (CH32SerialBoot 5de3006) | 7,268 | 5,020 | Rebuilt here byte-identical to `chgame_bootloader.bin` |
| Proven trims, no menu | 6,060 | 6,228 | core 0.2.4's direct-register `USB_init`; no `atexit`/`__libc_init_array`; no SPL GPIO/RCC; LED patterns reduced to a 2 Hz blink; `memcpy` no longer pulled in; the shared update path |
| First menu build (with self-update) | 12,104 | 184 | SD + FAT + CHG + install + LCD + font + menu |
| Struct copies without `memcpy` | 11,944 | 344 | The game table is copied as words |
| `-fno-jump-tables` | 11,908 | 380 | The protocol's command switch and the menu's tables |
| Hardware-review fixes | 12,068 | 220 | CRC7 on every SD command and CMD59 to switch CRC checking off; the N_RC gap; SPI1 started on the USB-notice path; B debounced, and the B escape only at power-on (soft-reset flag); the panel field drawn as one counter |
| USB strings as `const` literals | 11,948 | 340 | Manufacturer, product and interface descriptors in flash, not built at start-up (same bytes on the wire) |

## Where the release bytes go

There is no per-object split with LTO: everything is attributed to the LTO
partitions. The split below is from `./build.sh release --nolto`, which is
about 400 B larger and does not fit the reservation. It is linked against a
16 KB analysis copy of the script and must never be flashed.

| Object | Bytes | |
|---|---|---|
| menu.c | 2,483 | the list, scan, sort, keys, messages, the INSTALLED PROGRAM entry |
| proto.c | 1,738 | USB upload protocol, self-update commands |
| lcd.c | 1,274 | ST7735 set-up table, fill, text; includes the 475 B font |
| wch_usbcdc_handler.c + _cdc.c + _descr.c | 1,968 | USB CDC device |
| fat.c | 864 | FAT16/FAT32 |
| sd.c | 770 | SD SPI driver, CRC7 on every command |
| flash.c | 694 | RAM-resident flash writer (the `.ramfunc` load image included) |
| startup | 376 | WCH startup with the CHGame changes |
| install.c | 366 | two-pass install |
| boot.c | 358 | boot decision, USB mode, B escape |
| update.c | 254 | the shared update transaction |
| fault.c | 224 | the fault reporter (mcause/mepc on the LED) |
| the rest | ~940 | sys, chg, crc32, crc16, usb, appmeta, bootreq, jump, `-msave-restore` helpers, SystemInit |

## The size audit

The release map contains:
- no `memcpy`, `memset` or `printf`;
- no libgcc division helpers (RV32IMAC divides in hardware);
- no jump tables;
- only one library routine: `-msave-restore`'s register save/restore.

## If bytes are needed later

Largest first:
- **`fault.c`, about 200 B.** It is a bench diagnostic; the menu and USB now
  show state.
- **`do_read`/`do_status` in the protocol, about 250 B.** These are used by
  `-verify`, the HIL tests and `chgame_upload.py status`.
- **The "n/N" counter in the menu footer.**
- **An uppercase-only font, 155 B.**
- **The locked build, 312 B.** It drops self-update.
