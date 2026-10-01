// CHFour - FOUR IN A ROW for the CHGame handheld (CH32X035, 128x128 ST7735,
// piezo), in the look of CHBlackjack and its sister tables, against that
// game's dealer - who talks.
//
// The rules are in src/rules, the CPU's search in src/ai, the game's flow and
// the dealer's lines in src/game, and everything you see and hear in
// src/render, src/stage and src/states.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/Frame.h"

void setup() {
    arduboy.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    arduboy.setFrameRate(CHF4_FPS);
}

void loop() {
    frame::run();
}
