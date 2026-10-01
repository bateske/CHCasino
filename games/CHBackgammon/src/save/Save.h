// Saving options, lifetime stats and a game in progress.
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (From CHBlackjack, with its own magic: the
// games share the pages, and each ignores the others' records.)
#pragma once
#include <stdint.h>
#include "../game/Match.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // board colour theme (pal::Theme)
    uint8_t speed;      // 0 fun, 1 quick (no close-ups, shorter pauses)
    uint8_t level;      // last opponent chosen
    uint8_t mirror;     // 1: the home boards on the left
    uint8_t coach;      // 1: the coach has its say on each of your plays
    uint8_t length;     // last match length chosen (an index: single game, 3, 5, 7)
    uint8_t pad;
};

// Against the CPU: games, and matches longer than one point.
struct Stats {
    uint16_t won[match::LEVELS], lost[match::LEVELS];
    uint16_t mwon[match::LEVELS], mlost[match::LEVELS];
};

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game into match
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
