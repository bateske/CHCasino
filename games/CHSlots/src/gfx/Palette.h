// The one 16-colour palette, and the tricks it allows.
//
// CHGfx applies the palette while it converts the framebuffer for the panel,
// so recolouring an index recolours every pixel that uses it for free.
// All edits land in a staging copy and pal::commit() works out the final
// colours (flash, desaturate, fade) once per frame. CHGfx stages
// gfx_setPalette() itself (it takes effect when the next flush starts), so
// commit() may run while a frame is still going out.
//
// The colours are CHBlackjack's and CHChess's, so the games share one casino. FX_A and FX_B animate by themselves (the mode picks how).
#pragma once
#include <stdint.h>

enum : uint8_t {
    INK = 0, WHITE, FELT_DK, FELT, FELT_LT, SILVER, RED, WINE,
    GOLD, WOOD, BLUE, NAVY, SKIN, CYAN, FX_A, FX_B,
};

namespace pal {

// The three felt indices: the casino's green, DRAGON FORTUNE's maroon, jade
// and orange, or SWEET's pink, mint and lilac (each machine's symbols are
// drawn for its own).
enum Theme : uint8_t { GREEN, FORTUNE, SWEET, THEME_COUNT };      // in Machine's order (game/Slots.h)

// What FX_A does (FX_B always shimmers gold to white):
//   CASINO  a rainbow (banner outlines, sparkle)
//   FIRE    red - gold - white flicker (the free games)
enum Mode : uint8_t { CASINO, FIRE };

void init();                                // defaults + immediate commit
void setTheme(uint8_t theme);
uint8_t theme();
void setMode(Mode m);
void setFade(uint8_t level);                // 0 = black .. 16 = full colour
uint8_t fade();
void setDesaturate(uint8_t amount);         // 0 = colour .. 16 = grey
void flash(uint8_t index, uint16_t rgb444, uint8_t frames);  // override briefly
void tick();                                // once per frame, before commit
void resetClock();                          // debug: restart the FX_A/FX_B cycle
void commit();                              // once per frame, before the flush
uint16_t rgb444(uint8_t index);             // current staged colour

}  // namespace pal
