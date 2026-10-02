/* ST7735S panel driver for the menu. The configuration bytes are CHGfx
 * 1.3.0's panel set-up (frame rate, power, gamma), in a table. */
#include "lcd.h"
#include "font5x7.h"
#include "hal.h"
#include "sys.h"

static uint32_t t_wait;          /* tick stamp the next 120 ms wait runs from */

static void wait120(void)
{
    while (sys_ticks() - t_wait < 120u * SYS_TICKS_PER_MS) { }
}

static void cmd(uint8_t c)
{
    hal_lcd_dc(0);
    hal_spi_xfer(c);
    hal_lcd_dc(1);
}

void lcd_reset(void)
{
    /* SPI1 may not be running yet (the USB-notice path never reads the card),
       and a transfer on a disabled SPI would wait forever. */
    hal_spi_speed(SPI_BR_12M);
    hal_lcd_select(0);
    hal_lcd_rst(0);
    sys_delay_ms(1);                 /* >= 10 us low */
    hal_lcd_rst(1);
    t_wait = sys_ticks();
}

void lcd_wake(void)
{
    wait120();
    hal_lcd_select(1);
    cmd(0x11);                       /* SLPOUT */
    hal_lcd_select(0);
    t_wait = sys_ticks();
}

/* {command, number of data bytes, data...}, ended by a 0 command. */
static const uint8_t init_seq[] = {
    0xB1, 3, 0x05, 0x3A, 0x3A,                       /* FRMCTR1: CHGfx's faster frame rate */
    0xB2, 3, 0x05, 0x3A, 0x3A,                       /* FRMCTR2 */
    0xB3, 6, 0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A,     /* FRMCTR3 */
    0xB4, 1, 0x07,                                   /* INVCTR */
    0xC0, 3, 0xA2, 0x02, 0x84,                       /* PWCTR1..5, VMCTR1 */
    0xC1, 1, 0xC5,
    0xC2, 2, 0x0A, 0x00,
    0xC3, 2, 0x8A, 0x2A,
    0xC4, 2, 0x8A, 0xEE,
    0xC5, 1, 0x0E,
    0x20, 0,                                         /* INVOFF */
    0x36, 1, 0xC8,                                   /* MADCTL: the 1.44" green-tab orientation */
    0x3A, 1, 0x05,                                   /* COLMOD: RGB565 */
    0xE0, 16, 0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,   /* gamma + */
              0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
    0xE1, 16, 0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,   /* gamma - */
              0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
    0x13, 0,                                         /* NORON */
    0
};

void lcd_on(uint16_t bg)
{
    const uint8_t *p = init_seq;
    wait120();
    hal_lcd_select(1);
    while (*p) {
        uint32_t n = p[1];
        cmd(p[0]);
        p += 2;
        while (n--) hal_spi_xfer(*p++);
    }
    hal_lcd_select(0);
    lcd_fill(0, 0, LCD_W, LCD_H, bg);   /* never show the RAM's power-up noise */
    hal_lcd_select(1);
    cmd(0x29);                       /* DISPON */
    hal_lcd_select(0);
}

static void window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    x += 2; y += 3;                  /* panel offsets */
    cmd(0x2A);                       /* CASET */
    hal_spi_xfer(0); hal_spi_xfer((uint8_t)x);
    hal_spi_xfer(0); hal_spi_xfer((uint8_t)(x + w - 1));
    cmd(0x2B);                       /* RASET */
    hal_spi_xfer(0); hal_spi_xfer((uint8_t)y);
    hal_spi_xfer(0); hal_spi_xfer((uint8_t)(y + h - 1));
    cmd(0x2C);                       /* RAMWR */
}

static __attribute__((noinline)) void px(uint16_t c)
{
    hal_spi_xfer((uint8_t)(c >> 8));
    hal_spi_xfer((uint8_t)c);
}

void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    hal_lcd_select(1);
    window(x, y, w, h);
    for (uint32_t n = w * h; n; n--) px(c);
    hal_lcd_select(0);
}

static __attribute__((noinline)) const uint8_t *glyph(uint32_t ch)
{
    static const uint8_t mark[5] = { 0x7E, 0x7E, 0x7E, 0x00, 0x00 };    /* LCD_MARK: a 3x6 block */
    if (ch == 0x7F) return mark;
    return font5x7 + ((ch < 32 || ch > FONT5X7_LAST ? '?' : ch) - 32) * 5;
}

uint32_t lcd_text(uint32_t x, uint32_t y, const char *s, uint32_t n, uint16_t fg, uint16_t bg, uint32_t scale)
{
    uint32_t sh = scale >> 1, pad = !scale;     /* scale 0: 6x10 cells, the glyph a row down */
    uint32_t h = (8u << sh) + 2 * pad;
    hal_lcd_select(1);
    for (; n && *s; n--, s++, x += 6 << sh) {
        const uint8_t *g = glyph((uint8_t)*s);
        window(x, y, 6 << sh, h);
        for (uint32_t r = 0; r < h; r++)
            for (uint32_t c = 0; c < 6u << sh; c++) {
                uint32_t gc = c >> sh, gr = (r - pad) >> sh;
                px(gc < 5 && gr < 7 && (g[gc] >> gr & 1) ? fg : bg);
            }
    }
    hal_lcd_select(0);
    return x;
}
