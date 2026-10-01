// CHMahjong build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+ with Optimize set to "Smallest + LTO"
// and the default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHMJ_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHMJ_DEBUG
#ifdef CHSIM
#define CHMJ_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHMJ_DEBUG       0
#endif
#endif

// Device debug builds carry the ~3 KB protocol, and with it the game no
// longer fits: they leave out the EASY tile faces (TILES is CLASSIC only).
// The simulator (not flash-bound) and release builds keep everything.
#if CHMJ_DEBUG && !defined(CHSIM) && !defined(CHMJ_FULL)
#define CHMJ_LEAN        1
#else
#define CHMJ_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHMJ_PROFILE
#define CHMJ_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHMJ_FPS         60
