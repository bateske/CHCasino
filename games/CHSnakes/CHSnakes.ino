// SNAKES & LADDERS - the childhood board game, casino style, for the CHGame
// handheld (CH32X035, 128x128 ST7735, piezo): roll, hop, climb, get eaten.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/Frame.h"

void setup() {
    arduboy.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    frame::begin();
    arduboy.setFrameRate(CHSN_FPS);
}

void loop() {
    frame::run();
}
