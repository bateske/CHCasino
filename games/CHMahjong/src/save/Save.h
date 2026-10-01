// Saving options, the best results and a game in progress.
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
#include "../game/Board.h"

struct Options {
    uint8_t sound;      // 0 off, 1 on
    uint8_t felt;       // table colour (pal::Theme)
    uint8_t faces;      // 0 classic, 1 easy (numbers)
    uint8_t speed;      // 0 normal, 1 quick (no deal animation, faster matches)
    uint8_t layout;     // last layout chosen
    uint8_t view;       // 0 the whole table, 1 close up (B held: the other)
    uint8_t pad[2];
};

struct Stats {          // per layout
    uint16_t bestChips[board::LAYOUTS], bestSecs[board::LAYOUTS], cleared[board::LAYOUTS];
};

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game onto the board (replayed)
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Options &o, const Stats &s, bool withGame);

}  // namespace save
