// CHCraps - casino craps for the CHGame handheld (CH32X035, 128x128 ST7735,
// piezo), in the style of CHBlackjack and CHChess: the same felt, the same
// lettering, and Blackjack's dealer working the table as the stickman.
//
// The dealer's art and the 3x5 font come from Press Play On Tape's Arduboy
// Blackjack (Apache-2.0) by Simon Holmes (filmote) and Stephane C
// (vampirics), by way of CHBlackjack; see NOTICE.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/gfx/Palette.h"
#include "src/states/Screens.h"
#include "src/debug/Debug.h"

void setup() {
    arduboy.boot();
    dbg::paintStack();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    arduboy.setFrameRate(CHCR_FPS);
#if CHCR_DEBUG
    dbg::hook = screens::debugCommand;
#endif
}

void loop() {
    dbg::poll();
    if (!arduboy.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the dice
    // and the chips never slow down.
    uint8_t ticks = 0;
    do {
        arduboy.pollButtons();
        pal::tick();
        screens::update();
    } while (++ticks < 3 && arduboy.nextFrame());
    pal::commit();                  // staged by CHGfx: lands with the next flush
    gfx_wait();
    dbg::markRenderStart();
    screens::render(arduboy.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
}
