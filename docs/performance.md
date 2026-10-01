# Maxing out ST7735 graphics on the CH32X035

*The engineering notes behind the [CHGfx](../platform/libraries/CHGfx/) library. For how to
use it, see [its README](../platform/libraries/CHGfx/README.md). Copied into CHCasino from the
CHGfx workspace as of CHGfx 1.3.0.*

Target: **CH32X035G8U6** @ 48 MHz (QingKe V4C, 62 KB flash, 20 KB SRAM),
ST7735S 1.44" 128×128 on SPI1, CHGame board pinout.

## Short answer to "is there a fancy library for this chip?"

No. There isn't one, and there can't usefully be one — the good ST7735
libraries (`Arduino_ST7735_STM`, `STM32F1_ST7735_LL_DMA`, TFT_eSPI) are
all built around a **full RGB565 framebuffer**, and 128×128×2 = **32 KB**
against this chip's **20 KB of SRAM**. Every one of them degrades to
"Adafruit but with DMA" here.

The closest prior art is [limingjie/CH32V003-ST7735-Driver](https://github.com/limingjie/CH32V003-ST7735-Driver)
(minimal, DMA where it can, but built for the 2 KB CH32V003) and
[cnlohr/ch32fun](https://github.com/cnlohr/ch32fun) (a bare-metal stack,
not a graphics library). So this is written from the metal up.

## The ceiling, from the datasheets

| Constraint | Value | Source |
|---|---|---|
| HCLK max | 48 MHz, **no PLL** — only the 48 MHz HSI RC | CH32X035 RM §3.3 |
| SPI clock max | **HCLK/2 = 24 MHz** ("up to half of FHCLK") | CH32X035 RM §16.1 |
| SPI1_TX DMA | **DMA1 channel 3** | CH32X035 RM §9.2.3 |
| Flash wait states @48 MHz | **3** (`FLASH_ACTLR.LATENCY = 10b`) | CH32X035 RM §20.3.1 |
| SRAM | 20 KB total; 20464 B linkable, 2 KB stack | core `link_chgame_app.ld` |
| ST7735S serial write cycle | tSCYCW **66 ns min** (= 15.1 MHz) | ST7735S DS §8.4 |

24 MHz SPI = 3.0 MB/s, so:

* **16 bpp**: 32768 B/frame → 10.9 ms → **91 fps** ceiling
* **12 bpp**: 24576 B/frame → 8.2 ms → **122 fps** ceiling

Nothing on this board goes faster. There is no parallel LCD interface,
the flex is 14-pin serial-only, and there's no PLL to lift HCLK.

## What the driver does, and what each thing buys

| Change | Why it matters |
|---|---|
| Register-level SPI at HCLK/2 | The core's `SPI` class polls TXE per **byte** (`spi_com.c:387`). At 24 MHz a byte is 16 CPU cycles; the polled loop can't keep up. |
| DMA1_CH3 + 16-bit frames (DFF=1) | Zero CPU during transfer, and half the DMA bus cycles for RGB565. |
| One `setWindow` per frame, not per pixel | Adafruit's `drawPixel` sends CASET+RASET+RAMWR — **13 bytes of command per 2 bytes of pixel**. ~85% of the bus is overhead. |
| 4 bpp framebuffer (8 KB) + 16-colour palette | The only framebuffer that fits. Also makes fills 8 pixels per word store. |
| 256-entry `uint32` expansion LUT | One framebuffer byte (2 px) → one 32-bit store. ~3 cycles/px against a 32-cycle/px SPI budget, so conversion is free. |
| **12 bpp / RGB444 (COLMOD 0x03)** | 3 bytes per 2 pixels instead of 4 — **25% less wire traffic for nothing**. With a 16-colour palette you weren't using 65 K colours anyway. |
| Hot loops linked into SRAM | Flash is 3 wait states at 48 MHz; SRAM is zero. Test 5a vs 5b measures this directly on your silicon. |
| Ping-pong chunks + DMA TC interrupt | `gfx_flushAsync()` returns immediately; game logic runs during the ~11 ms transfer. |
| **`gfx_flushRect()` partial updates** | The wire is the bottleneck, so cost scales with **area**. A 128×64 dirty band is 2× the frame rate; 64×64 is 4×. This is the biggest lever left. |
| DMA fill with `MINC=0` | Solid-colour rectangles straight to the panel: the DMA re-reads one halfword N times. Zero RAM, zero CPU, full wire speed. |

### Two datasheet findings worth calling out

**1. The ST7735S colour LUT powers up undefined.** RGBSET (2Dh) is the
12-bit→18-bit and 16-bit→18-bit conversion table, and §10.1.23 lists its
default as literally **"Random"** after both power-on and hardware reset,
with "128-Bytes must be written to the LUT regardless of the color mode."
Every ST7735 driver in the wild ignores this and 16 bpp works fine, so
the silicon clearly ships a sane 5-6-5 default — but almost nobody
exercises 12 bpp, so `chgfx` programs the table explicitly when you
switch to `GFX_12BPP` (129 bytes, once). Turn it off with
`gfx_setWriteColorLut(false)` if your panel disagrees.

**2. The 24 MHz overclock is probably fine, and the datasheet hints why.**
tSCYCW is specified at 66 ns (15.1 MHz), but the same table gives
tSHW = tSLW = **15 ns each** — a high plus a low pulse only accounts for
30 ns of that 66 ns cycle. The cycle figure is a conservative
system-level number, which is why ST7735 panels commonly latch fine at
24–32 MHz on short flex. It is still out of spec, so **benchmark 12 in
the sketch sweeps 6/12/24 MHz and draws a 1-pixel vertical stripe
pattern** — the worst case for setup/hold, since every clock edge flips
MOSI. Clean stripes = good. Speckle or horizontal shear = back off to
`GFX_DIV4`.

### Dead ends I checked so you don't have to

* **Overclocking past 48 MHz** — no PLL exists; `HSITRIM` is a trim, not
  a multiplier, and USB CDC needs exactly 48 MHz anyway.
* **A second SPI** — irrelevant, the panel has one data line.
* **8 bpp framebuffer** — 16 KB of 20 KB leaves nothing for USB CDC.
* **Tearing-effect sync** — the ST7735 TE pin isn't broken out on this
  14-pin flex, so there's no vsync to lock to. `gfx_setPanelFrameRate()`
  scans the glass faster instead, which shortens the window in which a
  tear can be visible.

## Build and run

```bash
arduino-cli compile -b CHGame:ch32v:CHGame:opt=o2std,rtlib=nano CHGfx/examples/Benchmark
```

In the IDE, set **Tools ▸ Optimize ▸ Faster (-O2)**. The board default is
`-Os`. `-O3` costs roughly 8 KB more flash than `-O2` for very little in
return, which is a bad trade at 50 KB of usable application space.

Current build (1.3, with its new tests 13 and 14): **32004 B flash (62%),
15488 B RAM (84%)** at `-O2`, leaving 2.9 KB for stack and locals. The
benchmark links nearly every function in the library; a game links only
what it calls.

To upload:

```bash
arduino-cli upload -b CHGame:ch32v:CHGame -p COMx CHGfx/examples/Benchmark
```

Then open the Serial Monitor (USB CDC — the baud rate is ignored). The
sketch runs the full suite in 16 bpp, repeats it in 12 bpp, sweeps the
SPI clock, prints a summary to the panel, and then loops an animated
demo.

## Measured results

Real numbers from the board, `-O2`, SPI at 24 MHz. Theoretical
wire ceiling is 3000 KB/s.

### Transport — 16 bpp, full 128×128 frame (32768 B)

| Path | Time/frame | fps | Wire | vs baseline |
|---|---:|---:|---:|---:|
| Naive `drawPixel` (Adafruit-equivalent) | 414 ms | 2.4 | — | 1× |
| Blocking SPI stream, one window | 16.5 ms | 60 | 1986 KB/s | **25×** |
| **`gfx_flush` — DMA + LUT convert** | **11.1 ms** | **90** | **2947 KB/s** | **37×** |
| DMA wire ceiling (no pixel generation) | 11.1 ms | 89 | 2942 KB/s | — |
| DMA `MINC=0` solid fill (zero RAM, zero CPU) | 10.9 ms | 91 | 2993 KB/s | — |

`gfx_flush` is **within 0.2% of the raw DMA ceiling** and at 98% of the
theoretical 3000 KB/s. The 4 bpp framebuffer plus palette expansion is
genuinely free — the LUT converts a whole frame in 1.5 ms while the wire
needs 11.1 ms.

### 12 bpp is a straight 25% win

| Mode | Bytes/frame | `gfx_flush` | fps |
|---|---:|---:|---:|
| 16 bpp RGB565 | 32768 | 11.1 ms | 89 |
| **12 bpp RGB444** | **24576** | **8.37 ms** | **119** |

Measured 75% of the 16 bpp frame time — exactly the theoretical ratio.
With a 16-colour palette there is no visual cost at all.

### The flash wait-state penalty is real

| Conversion loop | Time/frame |
|---|---:|
| Code in SRAM | 1.52–1.62 ms |
| Code in flash | 3.41–3.51 ms |

**2.2× slower from flash**, exactly as the 3-wait-state figure predicts.
Worth knowing for any hot loop you write, not just this one.

### Partial updates scale with area, as expected

| Rect | Bytes | Time | fps |
|---|---:|---:|---:|
| 128×128 | 32768 | 11.1 ms | 89 |
| 96×96 | 18432 | 6.32 ms | 158 |
| 64×64 | 8192 | 2.82 ms | 354 |
| 32×32 | 2048 | 0.75 ms | 1336 |

### Drawing primitives (SRAM only, no SPI)

| Operation | Time | Rate |
|---|---:|---|
| `gfx_clear` (whole 8 KB buffer) | 93 µs | 87 MB/s |
| `hline` 128 px | 4 µs | 32 Mpx/s |
| `fillRect` 120×120 | 515 µs | 28 Mpx/s |
| `blit` 16×16 opaque, even x | 87 µs | 11.5 k/s |
| `blit` 16×16 transparent | 100 µs | 10 k/s |
| `text`, 24 chars | 246 µs | 4 k/s |
| `fillCircle` r=30 | 263 µs | 3.8 k/s |

### End to end: a real game frame

Tiled background + 32 transparent 16×16 sprites + HUD text, then present:

| | 16 bpp | 12 bpp |
|---|---:|---:|
| Full-frame flush | 15.1 ms → **66 fps** | 12.3 ms → **81 fps** |
| 128×64 dirty band | 9.6 ms → **103 fps** | 8.2 ms → **121 fps** |

The animated demo (40 moving transparent sprites + text, full-frame
async flush) runs at a steady **68 fps**.

### Optimisations the benchmark itself found

Running the suite exposed three bottlenecks that were not on the
transport side at all:

1. **Sprites dominated the CPU half of a frame** (4.2 ms of 18 ms). The
   transparent blit was going a pixel at a time. Rewriting it to handle
   both nibbles of a source byte with a branchless keep-mask, plus an
   early-out for fully-transparent bytes, took it 130 µs → 100 µs and
   the game frame 55 → 62 fps.
2. **Text cost 977 µs for 24 characters** because `gfx_char` called the
   flash-resident, fully-clipped `gfx_pixel` about 20 times per glyph.
   Walking the framebuffer directly took it to 246 µs — **4× faster**.
3. **The demo was capped at 29 fps by `sinf`/`cosf`.** This chip has no
   FPU; 80 soft-float transcendentals per frame cost more than the whole
   graphics pipeline. A 64-entry integer sine table took the demo to
   **68 fps and cut 8.5 KB of flash** by dropping libm entirely.

### Two honest caveats about async flush

Test 9a vs 9b shows async and blocking full-frame presents at the *same*
frame rate, and that is not a bug. With a single framebuffer you must
not draw into rows the DMA is still going to read. Double-buffering would
fix it but a second 4 bpp buffer is another 8 KB. Since 1.3 there is a
middle way: the flush goes top to bottom, and `gfx_waitRow(y)` returns as
soon as the rows above `y` are converted, so a frame drawn top to bottom
can start before the last one has finished (test 14: 10–14% faster).

And the async flush is not free for the CPU. **The August capture above
says 93–98% of the CPU stays available; that does not reproduce.** On the
current core (CHGame 0.2.4), the unmodified 1.2.0 benchmark measures 79%
(16 bpp) and 74% (12 bpp), and at the board's default `-Os` a flush cost
5.1 ms of CPU per frame - see the next section.

## 1.3: what CHChess found

CHChess (an isometric chess game on CHGfx 1.2.0) reported that its CPU
opponent lost a third of its search time to 60 fps flushing: about 5 ms
of CPU per frame, where these notes had called the conversion free.

### The converters were running from flash

The conversion loops were `static inline` helpers called from SRAM
functions. At `-O2` GCC inlined them. At `-Os` — the board's default, and
what CHChess builds with (plus LTO) — it did not: `conv565`, `conv444`,
`conv666` and `dmaStart` were emitted once, in `.text`, i.e. **flash**,
and the "SRAM" converters and the DMA interrupt called out to them for
every chunk. `nm` on a build shows it plainly (addresses `0x0000xxxx`
instead of `0x2000xxxx`). Nothing on the `-O2` benchmark could see it.

1.3 forces them inline (`always_inline`), puts the DMA interrupt itself in
SRAM with a leaner re-arm, and keeps a single copy of the converters.
`FlushCost`, a busy loop timed with and without an async full-frame flush
running under it, on the board:

| CPU per async full frame | 1.2.0, 16 bpp | 1.2.0, 12 bpp | 1.3, 16 bpp | 1.3, 12 bpp | 1.3 + ISR in SRAM, 16 / 12 bpp |
|---|---:|---:|---:|---:|---:|
| `-Os` | 5146 µs | 5159 µs | **2913 µs** | **2717 µs** | 2671 / 2492 µs |
| `-Os -flto` | 5039 µs | 5085 µs | **2729 µs** | **2574 µs** | 2462 / 2330 µs |
| `-O2` | 2728 µs | 2642 µs | **2605 µs** | **2532 µs** | 2490 / 2461 µs |

The last column is `-DCHGFX_ISR_IN_SRAM`: the interrupt handler in SRAM
as well as the converters. It is not the default because it costs ~330
bytes of SRAM, and CHChess, the game that asked for all this, has less
than that to spare.

About 1.7 ms of what remains is the conversion itself (test 5a); the
rest is the DMA sharing the bus and the interrupt's own overhead. Bigger
chunks barely help (2.49 / 2.33 / 2.29 ms at 2 / 4 / 8 rows a chunk, interrupt
in SRAM), and
the 565 loop is about seven instructions per two pixels, so ~2.5 ms is
the honest figure: **about a sixth of the CPU at 60 fps.**

### Two more linker/compiler facts worth knowing

* **Every `RAMFUNC` shared one section name**, so `--gc-sections` could
  only keep or drop them together. `HelloGraphics` never blits, yet it
  carried `gfx_blit`'s 492 bytes of SRAM. Each SRAM function now has a
  section of its own.
* **Where the SRAM code goes in `.data` matters.** 1.2 named its
  sections `.srodata.ramfunc`, which the link script places after
  `.sdata`, the small variables the global pointer reaches in one
  instruction (a 4 KB window). Every byte of SRAM code there pushes a
  sketch's variables out of the window, and every access to one of them
  gets an instruction longer. The first 1.3 build of CHChess overflowed
  flash by 1060 bytes, and most of that was this: 160 of its variables
  had fallen out of reach, against 37 on 1.2. Sections are now named
  `.gnu.linkonce.r.chgfx.*`, which the link script places at the very
  start of `.data`. (Unique names, so the linkonce merging never applies.)
* **The board compiles with `-msave-restore`.** Functions that call
  anything, or run out of scratch registers, save callee-saved registers
  through `__riscv_save_N` / `__riscv_restore_N` — in libgcc, in flash. An
  SRAM function that does so detours into flash twice per call. The hot
  per-row and per-pixel ones (`gfx_hline`, `gfx_pixel`, `gfx_clear`) are
  kept as leaves for that reason; `objdump` on the `.data` section lists
  the ones that still do it.

### The rest of the notes, measured

| Suggestion | Result |
|---|---|
| Staged `setPalette` | All palette calls are staged: the LUT is rebuilt when the next flush starts. Safe at any time; every frame shows one palette. |
| `setFade(level)` | `gfx_setFade(amount, colour)`: toward black or any colour, applied while the LUT is built. |
| Flush progress | `gfx_flushRow()` / `gfx_waitRow(y)`. Test 14: 14.1 → 12.6 ms (16 bpp), 11.4 → 10.0 ms (12 bpp) per frame. |
| Span sprites with remap and scale | `gfx_sprite4`: 81 µs for a remapped 16×12 at 1:1, 224 µs at 2×, with the 1:1 path kept separate. |
| Clip rectangle | `gfx_setClip`, honoured by every primitive. |
| Row operations in SRAM | Shaking 118 rows: `memmove` 6.3 ms, `gfx_scroll` 1.0 ms. |
| Rounded rects, ellipse, dither | 109 µs, 23 µs (15×5 shadow), 191 µs (128×32). |
| Rotated sprite | `gfx_sprite4Rot`: 614 µs for 16×12. |
| Outlined gradient text | `gfx_textFx`: 2.9 ms for a 3×, nine-letter banner with outline and shadow. |
| 3×5 font | `CHGfx_Tiny3x5`, 857 B, as a GFXfont. |
| Ship the simulator | `CHGfx/extras/sim`: sketches, screenshots, GIFs, tearing detection, 20k host tests. |

All `-O2`, from test 13 of the benchmark; `bench-1.3-O2.txt` and
`bench-1.3-Os.txt` next to this file are the raw captures.

Against the untouched 1.2.0 re-run on the same board, core and settings
(`bench-1.2.0-rerun-O2.txt`), the old primitives are within a few
percent or faster - `line` 269 → 241 µs and 24 characters of built-in
text 268 → 233 µs, `fillRect` 509 → 523 µs - except `fillCircle`,
269 → 289 µs, where the clip rectangle costs a few loads on each of its
~120 spans. The game frame (test 9) is 15138 → 15178 µs.

And CHChess, built unchanged against 1.3 (`-Os -flto`): 49700 bytes of
flash against 49444, and 18088 bytes of SRAM against 17844 - the
converters now in SRAM, as they were meant to be all along.

## What the benchmarks tell you

1. **naive drawPixel** — reproduces Adafruit's per-pixel cost exactly.
   This is your "before" number.
2. **blocking SPI stream** — window overhead removed, CPU still feeding
   bytes. Isolates how much of the loss is the polled transfer.
3. **DMA wire ceiling** — no pixel generation at all. Nothing can beat
   this; if 6 is close to 3, the framebuffer is free.
4. **DMA `MINC=0` fill** — zero-RAM solid fill.
5. **a/b: convert-only, SRAM vs flash** — the 3-wait-state penalty,
   measured rather than assumed.
6. **`gfx_flush`** — the real full-frame number.
7. **async** — how much CPU you keep during a flush; **7c** — what a
   flush costs the CPU, in microseconds per full and half frame.
8. **a–h: drawing primitives** — your CPU budget after the display is fed.
9. **a/b: a plausible game frame** (tiled background, 32 sprites, HUD),
   blocking vs async.
10. **`flushRect` at 128/96/64/32/16 px** — partial-update scaling.
11. **dirty-band game loop** — test 9 with only the middle band sent.
12. **SPI clock sweep** — the visual validation described above.
    Measured: 6 MHz → 22 fps, 12 MHz → 45 fps, 24 MHz → 90 fps, i.e.
    perfectly linear in clock, which confirms the transfer is
    wire-bound and not limited by anything on the MCU side.
13. **a–m: the 1.3 primitives** — span sprites, shapes, text effects,
    row operations, and `memmove` for comparison.
14. **racing the beam** — a frame drawn in two halves, waiting for the
    whole flush vs `gfx_waitRow()`.

## Using it in a game

```c
#include "chgfx.h"

void setup() {
    gfx_begin(GFX_DIV2, GFX_12BPP);   // 24 MHz, 12 bpp
    gfx_setPalette(myPalette, 16);
}

void loop() {
    gfx_wait();                       // last frame finished shifting out
    updateGame();
    gfx_clear(BG);
    drawEverything();                 // writes the 4 bpp buffer only
    gfx_flushAsync();                 // returns immediately
    // ...anything here overlaps the transfer
}
```

For a mostly-static scene, track what moved and call
`gfx_flushRectAsync(x, y, w, h)` instead — that's where the remaining
multiples are. Note that `x`/`w` get rounded outward to a multiple of 2
(16 bpp) or 8 (12 bpp), because the 4 bpp source has to start and end on
a byte boundary.

Sprites are 4 bpp, packed exactly like the framebuffer (2 px/byte, even
x in the low nibble, rows padded to whole bytes). **Even width blitted to
an even x with `transparent = -1` hits a byte-copy path that is roughly
8× the transparent per-pixel path** — worth designing your art around.

## Files

| File | Contents |
|---|---|
| `CHGfx/src/CHGfx.h` | Public API, with the reasoning inline |
| `CHGfx/src/CHGfx.cpp` | SPI1 / DMA1_CH3 / ST7735 transport, LUT, flush engine |
| `CHGfx/src/CHGfx_palette.cpp` | Palette, staging and fade (portable) |
| `CHGfx/src/CHGfx_draw.cpp` | 4 bpp primitives (nibble-aware, word-store fast paths), clip, text |
| `CHGfx/src/CHGfx_extras.cpp` | Rounded rects, ellipses, dither, remap, span sprites, row operations |
| `CHGfx/src/CHGfx_textfx.cpp` | Outlined, shadowed, gradient text |
| `CHGfx/src/CHGfx_internal.h` | Shared between the library's own files |
| `CHGfx/src/CHGfx_font.h` | 5×7 font, ASCII 32–126 (475 B) |
| `CHGfx/src/CHGfx_AdafruitGFX.h` | Optional Adafruit_GFX bridge |
| `CHGfx/examples/Benchmark/` | The benchmark suite and demo |
| `CHGfx/extras/sim/` | PC simulator and host tests |
| `benchmark-results.txt` | Raw serial capture from the board, 1.2.0 (August) |
| `bench-1.3-O2.txt`, `bench-1.3-Os.txt` | The same for 1.3 |
| `bench-1.2.0-rerun-O2.txt` | 1.2.0 re-run on the current core, the fair baseline for 1.3 |

Panel defaults are Adafruit's `INITR_144GREENTAB` (MADCTL `0xC8`,
colstart 2, rowstart 3) to match what your existing Adafruit_ST7735
setup uses. If the image is offset or mirrored, adjust with
`gfx_setPanelOffsets()`.
