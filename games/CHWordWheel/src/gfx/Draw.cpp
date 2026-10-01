#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")
#include <string.h>
#include "Draw.h"
#include "../RamFunc.h"     // hot loops run from SRAM

static inline void plot(uint8_t *p, int x, uint8_t c) {
    if (x & 1) *p = (uint8_t)((*p & 0x0F) | (c << 4));
    else       *p = (uint8_t)((*p & 0xF0) | c);
}

// Corner insets per row for radius 1..4 (pixel-art circles, not chamfers).
static const uint8_t INSET[4][4] = { {1}, {2, 1}, {3, 1, 1}, {4, 2, 1, 1} };

void fillRound(int x, int y, int w, int h, uint8_t r, uint8_t c) {
    if (r > 4) r = 4;
    if (r * 2 > h) r = (uint8_t)(h / 2);
    const uint8_t *in = INSET[r ? r - 1 : 0];
    for (int i = 0; i < r; i++) {
        gfx_hline(x + in[i], y + i, w - 2 * in[i], c);
        gfx_hline(x + in[i], y + h - 1 - i, w - 2 * in[i], c);
    }
    gfx_fillRect(x, y + r, w, h - 2 * r, c);
}

void bevel(int x, int y, int w, int h, uint8_t light, uint8_t dark) {
    gfx_hline(x, y, w, light);
    gfx_vline(x, y + 1, h - 1, light);
    gfx_hline(x + 1, y + h - 1, w - 1, dark);
    gfx_vline(x + w - 1, y + 1, h - 2, dark);
}

void dropShadow(int x, int y, int w, int h) {
    dither(x + 2, y + h, w, 2, 0, 0);
    dither(x + w, y + 2, 2, h - 2, 0, 0);
}

// A filled shape in the edge colour with the fill inside it.
void panel(int x, int y, int w, int h, uint8_t r, uint8_t fill, uint8_t edge) {
    fillRound(x, y, w, h, r, edge);
    fillRound(x + 1, y + 1, w - 2, h - 2, (uint8_t)(r > 1 ? r - 1 : 1), fill);
}

// A run of one colour within a row: odd nibble, whole bytes, odd nibble.
// pair is the colour in both nibbles.
static inline __attribute__((always_inline)) void pairRun(uint8_t *row, int a, int len, uint8_t pair) {
    if (a < 0) { len += a; a = 0; }
    if (a + len > GFX_W) len = GFX_W - a;
    if (len <= 0) return;
    uint8_t *p = row + (a >> 1);
    if (a & 1) { *p = (uint8_t)((*p & 0x0F) | (pair & 0xF0)); p++; len--; }
    uint8_t *e = p + (len >> 1);
    while (p < e) *p++ = pair;
    if (len & 1) *p = (uint8_t)((*p & 0xF0) | (pair & 0x0F));
}

RAMFUNC(sprite4) void sprite4(const uint8_t *d, int x, int y, const uint8_t *remap, int scale) {
    uint8_t h = d[1];
    d += 2;
    uint8_t pair[15];                       // remapped colours, doubled (remap < 16)
    for (int i = 0; i < 15; i++) pair[i] = (uint8_t)(remap[i] * 0x11);
    for (int j = 0; j < h; j++) {
        uint8_t n = *d++;
        const uint8_t *runs = d;
        d += n;
        if (scale == 256) {
            // 1:1 (nearly always): a running x.
            if ((unsigned)(y + j) >= GFX_H) continue;
            uint8_t *row = gfx_fb + (y + j) * GFX_FB_STRIDE;
            int q = x;
            for (uint8_t i = 0; i < n; i++) {
                uint8_t b = runs[i];
                int len = (b >> 4) + 1;
                if ((b & 15) != 15) pairRun(row, q, len, pair[b & 15]);
                q += len;
            }
            continue;
        }
        // Scaled: source row j covers screen rows [j * scale,
        // (j + 1) * scale) >> 8, and a run [px, px + len) the columns scaled
        // the same way (trailing transparency is implicit).
        for (int yy = y + ((j * scale) >> 8); yy < y + (((j + 1) * scale) >> 8); yy++) {
            if ((unsigned)yy >= GFX_H) continue;
            uint8_t *row = gfx_fb + yy * GFX_FB_STRIDE;
            int px = 0;
            for (uint8_t i = 0; i < n; i++) {
                uint8_t b = runs[i];
                int len = (b >> 4) + 1;
                if ((b & 15) != 15) {
                    int a = x + ((px * scale) >> 8);
                    pairRun(row, a, x + (((px + len) * scale) >> 8) - a, pair[b & 15]);
                }
                px += len;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Shapes and effects
// ---------------------------------------------------------------------------
void dither(int x, int y, int w, int h, uint8_t c, uint8_t phase) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0) return;
    uint8_t cc = (uint8_t)(c | (c << 4));
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        uint8_t *row = gfx_fb + yy * GFX_FB_STRIDE;
        // Pixels where (px + yy + phase) is even get the colour.
        uint8_t m = ((yy + phase) & 1) ? 0xF0 : 0x0F;
        int i = x;
        if (i & 1) { if (m == 0xF0) row[i >> 1] = (uint8_t)((row[i >> 1] & 0x0F) | (c << 4)); i++; }
        // Whole bytes [p, e): a word (8 px) at a time from each aligned one.
        uint8_t *p = row + (i >> 1), *e = row + ((x + w) >> 1);
        uint32_t m32 = m * 0x01010101u, c32 = (cc & m) * 0x01010101u;
        while (p < e) {
            if (!((uintptr_t)p & 3))
                for (; p + 4 <= e; p += 4) *(uint32_t *)p = (*(uint32_t *)p & ~m32) | c32;
            if (p < e) { *p = (uint8_t)((*p & ~m) | (cc & m)); p++; }
        }
        if (((x + w) & 1) && m == 0x0F) *e = (uint8_t)((*e & 0xF0) | c);
    }
}

// ---------------------------------------------------------------------------
// PPOT Font3x5 (Press Play On Tape, Apache-2.0). Column bytes, bit 0 = top,
// bit 5 = descender. Extended here with $ , / ' * ( ) < > = % #.
// ---------------------------------------------------------------------------
const uint8_t FONT35[][3] = {
    {0x1F,0x05,0x1F},{0x1F,0x15,0x1B},{0x1F,0x11,0x11},{0x1F,0x11,0x0E},{0x1F,0x15,0x11},  // A-E
    {0x1F,0x05,0x01},{0x1F,0x11,0x1D},{0x1F,0x04,0x1F},{0x00,0x1F,0x00},{0x10,0x10,0x1F},  // F-J
    {0x1F,0x04,0x1B},{0x1F,0x10,0x10},{0x1F,0x06,0x1F},{0x1F,0x01,0x1F},{0x1F,0x11,0x1F},  // K-O
    {0x1F,0x05,0x07},{0x1F,0x31,0x1F},{0x1F,0x05,0x1B},{0x17,0x15,0x1D},{0x01,0x1F,0x01},  // P-T
    {0x1F,0x10,0x1F},{0x0F,0x10,0x0F},{0x1F,0x0C,0x1F},{0x1B,0x04,0x1B},{0x07,0x1C,0x07},  // U-Y
    {0x19,0x15,0x13},{0x1F,0x11,0x1F},{0x12,0x1F,0x10},{0x1D,0x15,0x17},{0x11,0x15,0x1F},  // Z 0-3
    {0x07,0x04,0x1F},{0x17,0x15,0x1D},{0x1F,0x15,0x1D},{0x01,0x01,0x1F},{0x1F,0x15,0x1F},  // 4-8
    {0x17,0x15,0x1F},{0x00,0x17,0x00},{0x00,0x10,0x00},{0x04,0x04,0x04},{0x04,0x0E,0x04},  // 9 ! . - +
    {0x02,0x29,0x06},{0x0A,0x00,0x00},{0x12,0x1F,0x09},{0x20,0x10,0x00},{0x18,0x06,0x01},  // ? : $ , /
    {0x00,0x03,0x00},{0x0A,0x04,0x0A},{0x00,0x0E,0x11},{0x11,0x0E,0x00},{0x04,0x0A,0x11},  // ' * ( ) <
    {0x11,0x0A,0x04},{0x0A,0x0A,0x0A},{0x19,0x04,0x13},{0x1F,0x0A,0x1F},{0x0A,0x15,0x1A},  // > = % # &
};

// ASCII 32..90 -> FONT35 index, -1 = no glyph (lower case is drawn as capitals).
static const int8_t IDX35[59] = {
    -1, 36, -1, 53, 42, 52, 54, 45, 47, 48, 46, 39, 43, 38, 37, 44,   // space ! " # $ % & ' ( ) * + , - . /
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 41, -1, 49, 51, 50, 40,   // 0-9 : ; < = > ?
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,             // @ A-O
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,                       // P-Z
};

int glyph35(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    return (ch >= 32 && ch <= 90) ? IDX35[ch - 32] : -1;
}

// Column-major glyph, bit 0 = top row: the layout of both PPOT's font and
// CHGfx's built-in one.
RAMFUNC(glyph) void glyph(int x, int y, const uint8_t *cols, uint8_t ncols, uint8_t c) {
    c &= 0x0F;
    for (uint8_t i = 0; i < ncols; i++, x++) {
        uint8_t bits = cols[i];
        if (!bits || (unsigned)x >= GFX_W) continue;
        int yy = y;
        uint8_t *p = gfx_fb + yy * GFX_FB_STRIDE + (x >> 1);
        for (; bits; bits >>= 1, yy++, p += GFX_FB_STRIDE)
            if ((bits & 1) && (unsigned)yy < GFX_H) plot(p, x, c);
    }
}

RAMFUNC(text35) int text35(int x, int y, const char *str, uint8_t c) {
    int x0 = x;
    for (; *str; str++) {
        char ch = *str;
        if (ch == '\n') { x = x0; y += 7; continue; }
        if (ch == '~') { x += 2; continue; }
        int g = glyph35(ch);
        if (g >= 0) glyph(x, y, FONT35[g], 3, c);
        x += 4;
    }
    return x - x0;
}

// Each font pixel as a 2x2 block: two rows of one byte (even x) or two
// nibble pairs (odd x).
RAMFUNC(text35x2) void text35x2(int x, int y, const char *str, uint8_t c) {
    c &= 0x0F;
    int x0 = x;
    for (; *str; str++) {
        char ch = *str;
        if (ch == '\n') { x = x0; y += 14; continue; }
        if (ch == '~') { x += 4; continue; }
        int g = glyph35(ch);
        if (g >= 0) {
            for (int col = 0; col < 3; col++) {
                uint8_t bits = FONT35[g][col];
                int px = x + col * 2;
                if ((unsigned)px > GFX_W - 2) continue;
                for (int row = 0; bits; row++, bits >>= 1) {
                    if (!(bits & 1)) continue;
                    int py = y + row * 2;
                    if ((unsigned)py > GFX_H - 2) continue;
                    uint8_t *p = gfx_fb + py * GFX_FB_STRIDE + (px >> 1);
                    for (int k = 0; k < 2; k++, p += GFX_FB_STRIDE) {
                        if (!(px & 1)) *p = (uint8_t)(c | (c << 4));
                        else { p[0] = (uint8_t)((p[0] & 0x0F) | (c << 4)); p[1] = (uint8_t)((p[1] & 0xF0) | c); }
                    }
                }
            }
        }
        x += 8;
    }
}

int text35Width(const char *str) {
    int w = 0, best = 0;
    for (; *str; str++) {
        if (*str == '\n') { if (w > best) best = w; w = 0; }
        else w += (*str == '~') ? 2 : 4;
    }
    if (w > best) best = w;
    return best ? best - 1 : 0;
}
