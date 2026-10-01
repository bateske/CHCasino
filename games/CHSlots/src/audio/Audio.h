// Sound and the status LED.
//
// Every action has a voice: short step lists (6 bytes a step) kept inside
// the piezo's 1-4 kHz sweet spot, played by CHBlackjack's little sequencer
// (TIM1 on PB10, stepped from the core's 1 kHz SysTick hook). Music is one
// voice of two-byte notes that loops underneath; an effect takes the piezo
// while it plays and the tune carries on in time behind it.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Chip, Lever, Thunk, Antic, Lock, Coin, Win, BigWin, Jackpot,
    Gong, Roar, Broke, COUNT
};

enum class Song : uint8_t { None, Title, Free, Wheel, COUNT };

namespace audio {

void music(Song s);                 // loops until another (or None) is asked for
void setMusicOn(bool on);

void begin(bool on);
void setOn(bool on);
bool on();
void sfx(Sfx s);
void blip(uint16_t hz, uint16_t ms);     // reel ticks, the win counting up
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
