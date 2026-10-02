#ifndef CHBOOT_LCD_H
#define CHBOOT_LCD_H
#include <stdint.h>

/* The ST7735S panel, driven directly in RGB565 with no framebuffer: filled
 * rectangles and 5x7 text are all the menu needs. Bring-up is split so the
 * card can be read during the panel's two 120 ms waits:
 *
 *   lcd_reset()   reset pulse; starts the 120 ms reset wait
 *   ...           (read the card)
 *   lcd_wake()    finish the wait, Sleep Out; starts the next 120 ms
 *   ...
 *   lcd_on(bg)    finish the wait, configure, clear to bg, display on
 *
 * 128x128 visible; MADCTL 0xC8 and the 1.44" panel's column/row offsets
 * (2, 3), as CHGfx uses. */

#define LCD_W 128
#define LCD_H 128

/* RGB565 */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void lcd_reset(void);
void lcd_wake(void);
void lcd_on(uint16_t bg);
void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
/* Draws up to n characters of s (stops at NUL) in cells of 6x8 pixels times
 * scale (1 or 2), glyph plus one column and one row of background. Returns
 * the x after the last cell. */
uint32_t lcd_text(uint32_t x, uint32_t y, const char *s, uint32_t n, uint16_t fg, uint16_t bg, uint32_t scale);

#endif
