# The CHGame platform

What a game developer needs to know about the hardware and the core. The
deeper references are in [../platform/board/docs](../platform/board/docs):
`memory-map.md`, `boot-flow.md`, `protocol.md`, `recovery.md` and
`ch32x035-gotchas.md`. For the graphics side, see
[performance.md](performance.md).

## Hardware (Rev 0)

| | |
|---|---|
| MCU | WCH **CH32X035G8U6**, QingKe V4C RISC-V (no FPU), **48 MHz** from the internal RC (there is no PLL) |
| Flash | 62 KB user flash in 256 B pages: 12 KB bootloader, **50,944 B** for the sketch, 256 B metadata |
| SRAM | 20 KB: 16 B boot block, **18,416 B** for statics and heap, a fixed **2 KB** stack |
| Display | 1.44" **128x128 ST7735S** on SPI1, 12 or 16 bpp, up to 24 MHz SPI |
| Storage | **microSD** on the same SPI1 bus as the display |
| Input | D-pad, A, B, SELECT, START (active low, internal pull-ups) |
| Sound | piezo on PB10 (TIM1 CH2) |
| LED | one status LED on PB9, active high |
| USB | full-speed USB-C: CDC serial and the upload path. VID:PID 16C0:27DD (shared test IDs for now) |
| Expansion | header H1: 4 GPIO and a UART (Serial1, *Peripherals: Full* only) |
| Schematic | [../platform/hardware](../platform/hardware) (PDF + EasyEDA netlist) |

### Pins

These are the names in the variant (`variant_CHGame.h`). Sketches use the
names, not the port numbers.

| Name | Pin | | Name | Pin |
|---|---|---|---|---|
| `PIN_BTN_UP` | PB4 | | `PIN_LCD_CS` | PA4 (also SPI1 NSS / default `SS`) |
| `PIN_BTN_DOWN` | PC14 | | `PIN_LCD_DC` | PB0 |
| `PIN_BTN_LEFT` | PB3 | | `PIN_LCD_RST` | PB12 |
| `PIN_BTN_RIGHT` | PC15 | | `PIN_SD_CS` | PB11 |
| `PIN_BTN_A` | PB1 | | SPI1 SCK / MISO / MOSI | PA5 / PA6 / PA7 |
| `PIN_BTN_B` | PB6 | | `LED_BUILTIN` / `PIN_LED` | PB9 |
| `PIN_BTN_SELECT` | PB7 | | `PIN_BUZZER` | PB10 |
| `PIN_BTN_START` | PB8 | | `PIN_GPIO1..4` | PC0, PC3, PA0, PA1 |
| Serial1 TX / RX | PA2 / PA3 | | Wire SDA / SCL | PC18 / PC19 |

**The default `SS` is the LCD's chip select.** A plain `SD.begin()`
selects the panel, not the card. The card's CS is `PIN_SD_CS` (PB11).

There is no reset pin. PB7 is SELECT. The only resets are the power switch
and `NVIC_SystemReset()`.

## Boot, upload and USB

**Startup.**
- **With the 0.2.4 bootloader**, it checks the sketch's CRC and jumps to it
  within a few milliseconds of power-on.
- **With the SD menu bootloader** ([sd-menu.md](sd-menu.md)), power-on shows
  the game menu. The sketch starts when it is picked, after an upload, or at
  once when there is no card. It always starts from a real reset, never
  from a jump out of the menu.
- **USB** enumerates in the background while `setup()` runs.

**`Serial` is native USB CDC:**
- Writes to a port that no host has opened are dropped, never blocked, so a
  game on batteries never stalls on a `println`.
- `while (!Serial);` or `Serial.waitForPC(ms)` waits for a terminal.

**Upload.** The 1200-baud touch reboots into the bootloader on the same
port; then flash, verify, restart in about half a second. It works with
`usb=uploadonly` (no `Serial` in the sketch), and with a sketch stuck in a
loop, because the handshake lives in the USB interrupt.

**Recovery.** A sketch that kills USB (for example a HardFault: the core's
handler spins) needs a power cycle. As the last resort, hold BOOT across
power-on for the factory ISP; see `recovery.md`.

## The core's Tools menus

| Menu | Choices | Notes |
|---|---|---|
| Optimize `opt` | `osstd` (default), `oslto`, `o1std`, `o2std`, `o3std`, `ogstd` | The games use `oslto` (`-Os -flto`). It is 1-5 KB smaller; CHChess and others only fit with it. |
| C library `rtlib` | `nano` (default), `nanofp`, `full` | |
| Peripherals `periph` | `game` (default), `full` | `game` adds `-DUART_MODULE_ONLY -DTIM_MODULE_ONLY`: no Serial1, `tone()`, PWM or HardwareTimer, which saves about 4 KB. |
| USB `usb` | `serial` (default), `uploadonly` | `uploadonly` saves 0.6-0.7 KB. Upload still works. A sketch that uses `Serial` then fails to build, on purpose. |

Defines for a sketch go in `--build-property build.extra_flags=...`. It is
empty on this platform. The `*_MODULE_ONLY` flags travel in
`build.flags.periph`; do not override `compiler.cpp.extra_flags`.

## Saving: flash pages instead of EEPROM

There is no usable EEPROM. The core's `EEPROM` library emulates 26 bytes in
the option bytes and is untested here. Instead:

- **Why it works.** The bootloader erases only the pages a new image covers.
  Pages between the end of the image and the metadata page at 0xF700
  survive re-uploads. This was proven on hardware by
  `games/CHBlackjack/tools/probes/FlashProbe`.
- **Writing a page.** A sketch can erase and program a 256 B page from user
  mode in about 1.4 ms:
  - the code must run from SRAM (a RAMFUNC);
  - unlock with KEYR + MODEKEYR;
  - the controller wants the 0x08000000 alias of the address;
  - mask interrupts through CSR 0x800;
  - call `gfx_wait()` first, so no DMA flush is running.
- **What every game does** (`src/save/Save.cpp`):
  - It uses two pages, **0xF500 and 0xF600**, in turn. Each record carries a
    magic, a version, a sequence number and a CRC, so a power cut can only
    lose the newest save.
  - Both pages fit only while the image is ≤ **50,432 B**. A bigger image
    switches saving off rather than overwrite code. The check uses
    `_data_lma + (_edata - _data_vma)`.
  - **All games use the same two pages.** Each game must recognise only its
    own records by magic and version. See [status.md](status.md) for the
    magics that currently collide.

## Speed facts measured on this chip

- **Flash vs SRAM.** Code from flash runs at ~5 cycles per instruction (3
  wait states, no cache); code from SRAM at ~2. A function call per pixel
  from flash costs 2-3 µs. Put hot loops in SRAM. Each game's `RAMFUNC(name)`
  uses the `.gnu.linkonce.r.<prefix>.<name>` section trick from CHGfx 1.3.
  Every RAMFUNC costs its size in RAM as well.
- **newlib's `memmove`** is a byte loop in flash; write your own for big
  copies.
- **Frames.** A full-screen redraw of a busy game frame is 6-17 ms. The DMA
  flush itself (8.4 ms at 12 bpp) runs beside the CPU, but an async full
  flush still costs about 5 ms of CPU for the chunk conversion. Redraw only
  what changed (band-level dirty tracking), and draw static layers once.
- **Big outlined lettering** (mask + outline + shadow) costs 5-10 ms a word.
  Draw it once onto still screens, never per frame.
- **Logic and drawing.** Run logic at a fixed rate and let drawing catch up.
  A slow frame then costs smoothness, never game speed.
- **Flash budget.** The core with USB is about 6 KB and CHGfx about 7 KB.
  Sizeable wins from CHBlackjack:
  - `-Os` everywhere;
  - a 1.8 KB sound sequencer instead of a 6.5 KB library;
  - no `snprintf` (3.5 KB);
  - register GPIO instead of `pinMode` (2 KB);
  - no static constructors.

## C++ on this chip (coming from AVR/Arduboy)

| Assumption | Here |
|---|---|
| `int` is 16 bits | 32 bits |
| `double` is cheap | 64-bit software float, slow and ~8 KB. Use `float` or fixed point. |
| `PROGMEM`, `pgm_read_*`, `F()` | Flash is memory-mapped: plain reads |
| Unaligned access is free | Casting a byte array to `uint16_t*` gives unaligned loads; use `memcpy` |
| `min`/`max` are macros | `std::min`/`std::max`: both arguments must have the same type |
| `snprintf` is cheap-ish | It costs ~3.5 KB, has no `%l`, and its return value is unusable |
| `pinMode` is cheap | It pulls in ~2 KB of tables. GPIOB/GPIOC `CFGHR` is write-only: go through the core's shadows. |
| A static object costs nothing | Default member initialisers generate constructor code |
| `sq`, `map`, `word` are free names | They are Arduino macros and clash with identifiers |

## The SD card

See [sd-card.md](sd-card.md).
