// CHBackgammon - casino backgammon for the CHGame handheld (CH32X035,
// 128x128 ST7735, piezo), in the look of CHBlackjack and CHChess.
//
// The rules are in src/rules, the CPU (a small neural network that learned
// the game by playing itself, see tools/train) in src/ai, the game's flow in
// src/game, and everything you see and hear in src/table, src/stage and
// src/states.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/Frame.h"

void setup() {
    arduboy.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    arduboy.setFrameRate(CHBG_FPS);
}

void loop() {
    frame::run();
}
