// BOARDWALK - a casino property game for the CHGame handheld (CH32X035,
// 128x128 ST7735, piezo): roll, buy, build, and tap to outbid the table.
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
    arduboy.setFrameRate(CHBW_FPS);
}

void loop() {
    frame::run();
}
