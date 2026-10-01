// CHDominoes - casino dominoes for the CHGame handheld (CH32X035,
// 128x128 ST7735, piezo), in the look of CHBlackjack and CHChess: ALL FIVES
// and DRAW with the double-six set, against the CPU or between two players.
//
// The rules are in src/rules, the CPU in src/ai, the match's flow in
// src/game, where the tiles lie in src/table, and everything you see and
// hear in src/table, src/stage and src/states.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/Frame.h"

void setup() {
    arduboy.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    arduboy.setFrameRate(CHDM_FPS);
}

void loop() {
    frame::run();
}
