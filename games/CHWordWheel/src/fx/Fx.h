// Motion and sparkle: a particle pool, pop-up banners and floating "+$15"
// texts (easing and randomness are in Ease.h).
#pragma once
#include <stdint.h>
#include "Ease.h"

namespace fx {

enum Kind : uint8_t { CONFETTI, COIN };
void spawn(Kind k, int x, int y, int vx16, int vy16, uint8_t life, uint8_t colour);
void fountain(Kind k, int x, int y, uint8_t n);                            // confetti/coins up
bool particles();                   // any still flying

// Big centred lettering with an outline; pops in, holds, blinks out.
enum BannerStyle : uint8_t { B_RAINBOW, B_GOLD, B_RED, B_CYAN, B_WHITE, B_GREEN };
void banner(const char *text, BannerStyle s, int cy, uint8_t frames = 70);

void floatText(const char *text, int x, int y, uint8_t colour);

// Vertical extent of everything transient on screen (particles, floats,
// banner). Returns false if nothing is moving.
bool activeRows(int &lo, int &hi);

void clear();
void update();                      // once per logic tick
void drawParticles();
void drawBanner();
void drawFloats();
extern const uint8_t RAIN[5];        // the casino rainbow: red, gold, green, cyan, blue

}  // namespace fx
