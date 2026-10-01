// The one 16-colour palette, and the tricks it allows.
//
// CHGfx applies the palette while it converts the framebuffer for the panel,
// so recolouring an index recolours every pixel that uses it for free. All
// edits land in a staging copy (RGB444; cycling, the flash and the fade are
// worked out here) and pal::commit() pushes them once per frame. CHGfx 1.3
// stages gfx_setPalette() itself and rebuilds its conversion LUT when the
// next flush starts, so a commit is safe at any time; it still costs that
// rebuild, so commit() does nothing on a frame where nothing changed.
#pragma once
#include <stdint.h>

// CHBlackjack's and CHChess's sixteen. The property groups borrow them:
// WOOD, CYAN, RED, GOLD, FELT_LT and BLUE, with pink and orange dithered.
enum : uint8_t {
    INK = 0, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE,
    GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B,
};

namespace pal {

void init();                                // defaults + immediate commit
void setFade(uint8_t level);                // 0 = black .. 16 = full colour
// One index shown as another colour for a few frames (a hit, a payment).
void flash(uint8_t index, uint16_t rgb444, uint8_t frames);
void tick();                                // once per frame: FX_A the rainbow, FX_B the gold/white pulse
void resetClock();                          // debug: restart the FX_A/FX_B cycle
void commit();                              // once per frame, if anything changed

}  // namespace pal
