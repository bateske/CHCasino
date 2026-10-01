// Sound and the status LED.
//
// CHBlackjack's piezo sequencer: effects are short step lists (6 bytes a
// step) kept inside the piezo's 1-4 kHz sweet spot, and can be played a few
// semitones up (each jump of a combo rings higher). A tune (two bytes a
// note) loops underneath: an effect sounds over it and the tune keeps time.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Land, Hop, Capture, Coin, Chip, Crown,
    Whoosh, Sweep, Win, Lose, Draw, Turn,
    Tick, Tock,                      // soft (quieter than the rest): keep them last
    COUNT
};

namespace audio {

bool begin(bool on);
void setOn(bool on);
void sfx(Sfx s, uint8_t semitones = 0);     // up to 12 higher
bool playing();                     // an effect is sounding
// The title's tune, looping until stopped (silent if music is off).
void setMusic(bool on);
void tune(bool play);
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
