// Saving options, lifetime stats and a game in progress (the purse, the
// layout on the felt and the tote board).
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (From CHBlackjack and CHChess, with its own
// magic: the CHGame games share the pages, and each ignores the others'
// records.)
#pragma once
#include <stdint.h>

class Roulette;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Roulette &r, bool &hasGame);   // options and stats; the game too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Roulette &r, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
