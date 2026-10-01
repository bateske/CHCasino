// CHSnakes build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.4+ with Optimize set to "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~4 KB of flash). Release builds also set USB
// to "Upload only" (no Serial: ~0.6 KB).
#pragma once

#define CHSN_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHSN_DEBUG
#ifdef CHSIM
#define CHSN_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHSN_DEBUG       0
#endif
#endif

// A build that leaves out saving and the options screen (about 2 KB). On
// for device debug builds, which carry the ~2 KB protocol and no longer fit
// beside everything; release builds and the simulator have it all.
#ifndef CHSN_LEAN
#if CHSN_DEBUG && !defined(CHSIM)
#define CHSN_LEAN        1
#else
#define CHSN_LEAN        0
#endif
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHSN_PROFILE
#define CHSN_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHSN_FPS         60
