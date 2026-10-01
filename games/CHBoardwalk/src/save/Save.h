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
#include "../game/Game.h"

struct Options {
    uint8_t sound;                  // 0 off, 1 on
    uint8_t pace;                   // 0 fun, 1 quick
    uint8_t seat[game::SEATS];      // the last table: game::Kind (+ the CPU's level) per seat
    uint8_t rounds;                 // closing time, in tens of rounds
    uint8_t pad;
};

struct Stats {
    uint16_t games, humanWins, cpuWins, busts;
    int32_t best;                   // the richest winner yet
};

namespace save {

enum Game : uint8_t { NO_GAME, THIS_GAME, SAVED_GAME };

bool available();                   // false: image too big, or a write failed
bool load(Options &o, Stats &s, bool &hasGame);
bool loadGame();                    // the saved game, as its turn began
// With no game, the game being played (as its turn began), or the game
// already saved kept as it is. Call after gfx_wait(): the page is built in
// CHGfx's chunk scratch.
bool store(const Options &o, const Stats &s, Game game);

}  // namespace save
