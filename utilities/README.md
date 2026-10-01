# utilities/

Helper sketches that are not games.

| Sketch | What it does | Licence |
|---|---|---|
| [CHSDtoUSB](CHSDtoUSB) | Turns the CHGame into a USB microSD card reader. A PC sees the card as a drive, and the board's serial port keeps working beside it, so uploads need no button presses. Use it to put game data on the card without taking the card out. The steps are in the root [CLAUDE.md](../CLAUDE.md). | GPL-3.0 |

CHSDtoUSB has its own SD driver: read-write, CRC-checked, with DMA. It
comes from sdfatlib, which is why the sketch is GPL. Keep that code inside
this sketch. The games use the MIT-licensed, read-only
[CHSd](../platform/libraries/CHSd) instead.

Build it from this folder against CHCasino's CHGfx:

```bash
cd utilities/CHSDtoUSB
arduino-cli compile -b CHGame:ch32v:CHGame --library ../../platform/libraries/CHGfx .
arduino-cli upload  -b CHGame:ch32v:CHGame -p <PORT> .
```

`tools/chsd_test.py` is its hardware test suite. It needs a test build
(`--build-property build.extra_flags=-DCHSD_TEST=1`) and runs on Windows;
see its README.

## Elsewhere

- **FlashProbe** is a probe sketch that proved a sketch can erase and
  program flash pages, and that those pages survive a re-upload. Every
  game's save system rests on that. It stays with the game that first
  needed it, in
  [games/CHBlackjack/tools/probes/FlashProbe](../games/CHBlackjack/tools/probes/FlashProbe),
  because that game's source refers to it.
