// CHFour build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.4+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHF4_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHF4_DEBUG
#ifdef CHSIM
#define CHF4_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHF4_DEBUG       0
#endif
#endif

// A build without saving and the options and setup screens (the sister
// games' device debug builds need it to fit the ~2 KB protocol and USB
// Serial; this one has the room, so debug builds are the whole game).
#ifndef CHF4_LEAN
#define CHF4_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHF4_PROFILE
#define CHF4_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHF4_FPS         60
