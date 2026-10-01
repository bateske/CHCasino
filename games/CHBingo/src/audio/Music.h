// Music scores for the sequencer in Audio.cpp (Playtune format). This game
// ships none: the flash goes to the game, and the hall is loud enough.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace music {

inline void get(uint8_t, bool, const uint8_t *&data, size_t &n) { data = nullptr; n = 0; }

}  // namespace music
