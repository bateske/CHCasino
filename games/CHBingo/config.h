// CHBingo build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHBN_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~2 KB and needs USB Serial).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHBN_DEBUG
#ifdef CHSIM
#define CHBN_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHBN_DEBUG       0
#endif
#endif

// Device debug builds carry the protocol, so they leave out the broke
// screen's lettering unless built with -DCHBN_FULL. Saving stays. Release
// builds keep everything.
#if CHBN_DEBUG && !defined(CHSIM) && !defined(CHBN_FULL)
#define CHBN_LEAN        1
#else
#define CHBN_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHBN_PROFILE
#define CHBN_PROFILE     0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHBN_FPS         60
