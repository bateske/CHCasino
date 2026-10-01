// CHSolitaire build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.4+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHSL_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHSL_DEBUG
#ifdef CHSIM
#define CHSL_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHSL_DEBUG       0
#endif
#endif

// A device debug build that leaves out saving, for when the ~2 KB protocol
// no longer fits beside it. Not needed so far: opt in with -DCHSL_LEAN=1.
#ifndef CHSL_LEAN
#define CHSL_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHSL_PROFILE
#define CHSL_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHSL_FPS         60
