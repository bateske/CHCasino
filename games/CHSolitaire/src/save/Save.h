// Saving the options, the lifetime stats and the game on the table.
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (From CHBlackjack via CHChess, with its own
// magic: the CHGame games share the pages, and each ignores the others'
// records - so saving here replaces another game's save.)
#pragma once
#include <stdint.h>
#include "../game/Klondike.h"

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Options &o, Stats &s, Klondike &game);
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Options &o, const Stats &s, const Klondike &game);

}  // namespace save
