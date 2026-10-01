#pragma GCC optimize("Os")   // cold code: size over speed (hot pixel loops live in Draw/Mask)
#include <string.h>
#include <CHGfx.h>
#include "Palette.h"

namespace pal {

// Authored on the RGB444 grid so 12 bpp output, fades and the simulator agree.
static const uint16_t BASE[16] = {
    0x000,  // INK
    0xFFF,  // WHITE
    0x042,  // FELT_DK
    0x173,  // FELT
    0x4B5,  // FELT_LT
    0xBBC,  // SILVER
    0xE12,  // RED
    0x702,  // WINE
    0xFC2,  // GOLD
    0x741,  // WOOD
    0x26E,  // BLUE
    0x125,  // NAVY
    0xFB8,  // SKIN
    0x6EF,  // CYAN
    0xF0F,  // FX_A
    0xFC2,  // FX_B
};

static const uint16_t RAINBOW[12] = {
    0xF22, 0xF82, 0xFE2, 0x8F2, 0x2F4, 0x2FC, 0x2EF, 0x28F, 0x42F, 0xA2F, 0xF2E, 0xF28,
};

static uint16_t staged[16];
static bool     dirty = true;
static uint8_t  fadeLevel = 16;
static uint32_t ticks = 0;
static uint8_t  flashIdx, flashFrames;
static uint16_t flashColour;

void init() {
    memcpy(staged, BASE, sizeof staged);
    dirty = true;
    commit();
}

void setFade(uint8_t level) { if (level > 16) level = 16; if (level != fadeLevel) { fadeLevel = level; dirty = true; } }

void flash(uint8_t index, uint16_t c, uint8_t frames) {
    flashIdx = index; flashColour = c; flashFrames = frames;
    dirty = true;
}

void tick() {
    ticks++;
    uint16_t a = RAINBOW[(ticks / 3) % 12];
    // FX_B: triangle wave GOLD <-> WHITE over 32 frames.
    uint8_t t = (uint8_t)(ticks & 31);
    if (t > 15) t = (uint8_t)(31 - t);
    uint16_t b = (uint16_t)(0xF00 | ((12 + (t * 3) / 15) << 4) | (2 + (t * 13) / 15));
    // Only a tick that moves a colour marks the palette dirty: each commit
    // costs CHGfx a LUT rebuild at the next flush.
    if (a != staged[FX_A] || b != staged[FX_B]) {
        staged[FX_A] = a; staged[FX_B] = b;
        dirty = true;
    }
    if (flashFrames && !--flashFrames) dirty = true;
}

void resetClock() { ticks = 0; }

void commit() {
    if (!dirty) return;
    dirty = false;
    uint16_t out[16];
    for (uint8_t i = 0; i < 16; i++) {
        uint16_t c = flashFrames && i == flashIdx ? flashColour : staged[i];
        uint16_t r = (uint16_t)((((c >> 8) & 15) * fadeLevel) >> 4);
        uint16_t g = (uint16_t)((((c >> 4) & 15) * fadeLevel) >> 4);
        uint16_t b = (uint16_t)(((c & 15) * fadeLevel) >> 4);
        out[i] = (uint16_t)((((r << 1) | (r >> 3)) << 11) | (((g << 2) | (g >> 2)) << 5) | ((b << 1) | (b >> 3)));
    }
    gfx_setPalette(out, 16);
}

}  // namespace pal
