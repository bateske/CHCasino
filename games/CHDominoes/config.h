// CHDominoes build switches.
//
// Keep feature switches here rather than in --build-property flags. The game
// needs the CHGame core 0.2.4+ and its default Peripherals setting ("Game",
// which compiles out Serial1/tone/HardwareTimer: ~4 KB of flash). It is
// built with Optimize set to "Smallest + LTO" and, for release, USB set to
// "Upload only" (no Serial: ~0.6 KB); it fits without either.
#pragma once

#define CHDM_VERSION     "0.1"

// Serial debug protocol: screenshots, input injection, lockstep, perf.
// Off in normal builds. tools/device.py turns it on with
// --build-property build.extra_flags, and leaves USB at "Serial" for it.
#ifndef CHDM_DEBUG
#ifdef CHSIM
#define CHDM_DEBUG       1       // the simulator is driven through the protocol
#else
#define CHDM_DEBUG       0
#endif
#endif

// A build without saving, the options and setup screens and the hint
// (games are started with the protocol's G command): for a device debug
// build if the game should ever outgrow it. Off: everything fits.
#ifndef CHDM_LEAN
#define CHDM_LEAN        0
#endif

// Section profiler (dbg::prof + the T command). Opt-in: costs flash.
#ifndef CHDM_PROFILE
#define CHDM_PROFILE     0
#endif

// Frame rate the game logic is paced for.
#define CHDM_FPS         60
