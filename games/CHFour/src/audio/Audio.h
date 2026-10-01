// Sound and the status LED.
//
// CHBlackjack's piezo sequencer: effects are short step lists (3 bytes a
// step) kept inside the piezo's 1-4 kHz sweet spot. There is no music
// player here - the fanfares are effects too.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Drop, Coin, Whoosh, Win, Lose, Draw, Title,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};

namespace audio {

bool begin(bool on);
void setOn(bool on);
void sfx(Sfx s);
// One note, for sounds made up as they go (the dealer's typewriter voice,
// the four lighting up); never cuts off a fanfare.
void blip(uint16_t hz, uint8_t ms);
bool playing();                     // an effect is sounding
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
