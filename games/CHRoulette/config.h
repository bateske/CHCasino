// CHRoulette build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// is built with the CHGame core 0.2.4+, Optimize "Smallest + LTO" and the
// default Peripherals setting ("Game", which compiles out
// Serial1/tone/HardwareTimer: ~3.4 KB of flash); release builds also set USB
// "Upload only" (no Serial). tools/device.py has the exact settings.
#pragma once

#define CHRL_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds (it costs ~2 KB and needs USB Serial).
// tools/device.py turns it on with --build-property build.extra_flags.
#ifndef CHRL_DEBUG
#ifdef CHSIM
#define CHRL_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHRL_DEBUG       0
#endif
#endif

// Debug builds carry the protocol, so they leave out things the tests never
// need: every CHRL_DEBUG build, the simulator included, has no music scores
// (src/audio/Music.cpp, from tools/make_music.py), and device debug builds
// also drop the credits page unless built with -DCHRL_FULL. Saving stays.
// Release builds keep everything.
#if CHRL_DEBUG && !defined(CHSIM) && !defined(CHRL_FULL)
#define CHRL_LEAN        1
#else
#define CHRL_LEAN        0
#endif

// CHBlackjack's back room, the croupier telling the credits (Stats, A).
// Off: it costs ~1.2 KB, and the game needs the flash; the credits are on
// the Options screen instead.
#ifndef CHRL_CREDITS
#define CHRL_CREDITS     0
#endif

// Attract mode: after ten idle seconds on the title (its tune played out)
// the croupier plays a few spins on his own. Off: ~0.8 KB the game needs.
#ifndef CHRL_DEMO
#define CHRL_DEMO        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHRL_PROFILE
#define CHRL_PROFILE     0
#endif

// Logic runs at a fixed 60 Hz; drawing catches up as it can.
#define CHRL_FPS         60
