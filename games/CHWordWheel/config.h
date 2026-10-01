// CHWordWheel build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHWW_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~2 KB and needs USB Serial).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHWW_DEBUG
#ifdef CHSIM
#define CHWW_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHWW_DEBUG       0
#endif
#endif

// Debug builds carry the protocol, so they leave out things the tests never
// need: every CHWW_DEBUG build, the simulator included, has no music score
// (src/audio/Music.cpp, from tools/make_music.py), and device debug builds
// (CHWW_LEAN) also drop the Setup, Options and Stats screens - the podiums
// are set with the protocol's W command - unless built with -DCHWW_FULL,
// which does not fit. Saving and the SD bank stay. Release builds keep
// everything.
#if CHWW_DEBUG && !defined(CHSIM) && !defined(CHWW_FULL)
#define CHWW_LEAN        1
#else
#define CHWW_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHWW_PROFILE
#define CHWW_PROFILE     0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHWW_FPS         60
