// Saving options, lifetime stats, the jackpot meters and the purse.
//
// CHGame has no EEPROM, but its bootloader only erases the flash pages a new
// sketch occupies, so the last pages of the application region survive
// re-uploads. Two pages are used in turn, each record carrying a sequence
// number and a CRC, so a power cut mid-write can only lose the newest save.
// If the sketch ever grows into those pages, saving switches itself off
// rather than overwrite code. (CHBlackjack's and CHChess's scheme, with its
// own magic: the games share the pages and each ignores the others' records.)
//
// A saved game is the purse, the machine you were sitting at and your bets.
// The game only saves between spins, never inside the free games or Hold and
// Spin: a feature is settled spin by spin, and a save in the middle of one
// would let a power cycle take it back. The MAJOR and GRAND meters are kept
// whether or not a game is in progress: they belong to the machine.
#pragma once
#include <stdint.h>

class Slots;

namespace save {

bool available();                   // false: image too big, or a write failed
bool load(Slots &g, bool &hasGame); // options, stats, meters; the purse too if hasGame
// Call after gfx_wait(): the page is built in CHGfx's chunk scratch.
bool store(const Slots &g, bool withGame);

}  // namespace save
