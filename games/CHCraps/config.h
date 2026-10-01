// CHCraps build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer); release builds also set USB "Upload only" (no
// Serial). tools/device.py has the exact settings.
#pragma once

#define CHCR_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~2 KB and needs USB Serial).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHCR_DEBUG
#ifdef CHSIM
#define CHCR_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHCR_DEBUG       0
#endif
#endif

// Device debug builds carry the protocol, so they may leave out things the
// tests never need (music, the attract demo) to fit. Release builds and the
// simulator keep everything; -DCHCR_FULL forces a full device debug build.
#if CHCR_DEBUG && !defined(CHSIM) && !defined(CHCR_FULL)
#define CHCR_LEAN        1
#else
#define CHCR_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHCR_PROFILE
#define CHCR_PROFILE     0
#endif

#define CHCR_FPS         60
