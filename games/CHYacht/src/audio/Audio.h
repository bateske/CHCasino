// Sound and the status LED.
//
// Every action has a voice: short step lists (6 bytes a step) kept inside
// the piezo's 1-4 kHz sweet spot, played by CHBlackjack's little sequencer
// (TIM1 on PB10, stepped from the core's 1 kHz SysTick hook). No music yet:
// it goes in only if flash is left once the game is complete.
#pragma once
#include <stdint.h>

enum class Sfx : uint8_t {
    Cursor, Select, Deny, Chip, ChipTake, Throw, Bounce, Wall, Clack, Point, Win, BigWin,
    SevenOut, Craps, Coin, Sweep, Hot, Whoosh, Lose, Broke, COUNT
};

namespace audio {

void begin(bool on);
void setOn(bool on);
bool on();
void sfx(Sfx s);
void blip(uint16_t hz, uint16_t ms);     // typewriter, ticks, the rattle
void update();                      // once per frame: LED patterns

// Status LED (PB9): short patterns for wins.
enum Led : uint8_t { LED_OFF, LED_BLINK, LED_TRIPLE, LED_PARTY };
void led(Led pattern);

}  // namespace audio
