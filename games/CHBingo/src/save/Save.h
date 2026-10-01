// Saving options, lifetime stats, the jackpot and a game in progress (the
// purse, and a round if one is being played: its cards and draw come back
// from the round's seed).
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

class Bingo;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Bingo &g, bool &hasGame); // options, stats, jackpot; the game too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Bingo &g, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
