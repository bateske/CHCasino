// Sound and the status LED.
//
// CHBlackjack's piezo sequencer: effects are short step lists (6 bytes a
// step) kept inside the piezo's 1-4 kHz sweet spot. There is no music
// player here - the fanfares are effects too.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Dice, Land, Hop, Coin, Pay, Buy, Gavel, Flip, Jail, Build, Doubles,
    Whoosh, Win, Lose, Turn, Title,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};

namespace audio {

bool begin(bool on);
void setOn(bool on);
void sfx(Sfx s);
void blip(uint16_t hz, uint16_t ms); // a one-off tick: bids climbing, counters rolling
bool playing();                     // an effect is sounding
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
