// CHSolitaire - Klondike for the CHGame handheld (CH32X035, 128x128 ST7735,
// piezo), after the Windows classic: draw one or three, Standard or Vegas
// scoring, a choice of card backs, and the cards bouncing away when you
// win. In the style of CHBlackjack, CHChess and CHPoker.
//
// Frame loop: logic runs at a fixed 60 Hz while the previous frame is still
// going out over DMA; drawing waits for it (one framebuffer), then the new
// frame is sent.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/gfx/Palette.h"
#include "src/states/Screens.h"
#include "src/audio/Audio.h"
#include "src/debug/Debug.h"

void setup() {
    arduboy.boot();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    arduboy.setFrameRate(CHSL_FPS);
}

void loop() {
    dbg::poll();
    if (!arduboy.nextFrame()) return;
    dbg::markUpdateStart();
    uint8_t ticks = 0;
    do {
        arduboy.pollButtons();
        pal::tick();
        audio::update();
        screens::update();
    } while (++ticks < 3 && arduboy.nextFrame());
    gfx_wait();
    pal::commit();
    dbg::markRenderStart();
    screens::render(arduboy.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
}
