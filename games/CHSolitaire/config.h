// CHSolitaire build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.4+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHSO_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHSO_DEBUG
#ifdef CHSIM
#define CHSO_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHSO_DEBUG       0
#endif
#endif

// A device debug build that leaves out saving, for when the ~2 KB protocol
// no longer fits beside it. Not needed so far: opt in with -DCHSO_LEAN=1.
#ifndef CHSO_LEAN
#define CHSO_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHSO_PROFILE
#define CHSO_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHSO_FPS         60
