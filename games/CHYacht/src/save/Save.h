// Saving options, lifetime stats, the purse and the game in progress.
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (CHBlackjack's and CHChess's scheme, with its
// own magic: the games share the pages and each ignores the others' records.)
//
// A saved game is every card, the dice on the table and the rolls left, so
// CONTINUE picks up exactly where SAVE & QUIT left off, even mid-turn.
#pragma once
#include <stdint.h>

class Yacht;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Yacht &g, bool &hasGame); // options, stats, purse; the game too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Yacht &g, bool withGame);

}  // namespace save
