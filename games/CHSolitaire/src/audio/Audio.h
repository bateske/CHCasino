// Sound and the status LED.
//
// CHBlackjack's piezo sequencer (via CHChess): effects are short step lists
// (6 bytes a step) kept inside the piezo's 1-4 kHz sweet spot. There is no
// music player - the fanfares are effects too (flash goes to the games).
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Deal, Flip, Pick, Back, Suit, BigWin, Shuffle, Coin,
    Bust, Title, Whoosh,
    COUNT
};

namespace audio {

bool begin(bool on);
void setOn(bool on);
void sfx(Sfx s);
void blip(uint16_t hz, uint16_t ms);  // a one-step effect (a card landing, a bounce)
bool playing();                     // an effect is sounding
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
