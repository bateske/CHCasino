// Saving options, lifetime stats and the table in progress.
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (CHBlackjack's and CHChess's scheme, with its
// own magic: the games share the pages and each ignores the others' records.)
//
// A saved game is the whole table - purse, every chip, the point, the
// shooter's hand - so CONTINUE picks up exactly where SAVE & QUIT left off,
// even in the middle of a hand (contract bets and all).
#pragma once
#include <stdint.h>

class Craps;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Craps &g, bool &hasGame); // options and stats; the table too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Craps &g, bool withGame);

}  // namespace save
