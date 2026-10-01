// Saving options, lifetime stats and an episode in progress (which step it
// has reached, who is playing, what each has banked, and where the deal of
// puzzles stands).
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

class Show;
namespace bank { struct Deck; }

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Show &g, bank::Deck &deck, bool &hasGame);   // options, stats, the deal; the episode too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Show &g, const bank::Deck &deck, bool withGame);
void allowWrites(bool on);          // debug builds on the board: off until a script turns it on

}  // namespace save
