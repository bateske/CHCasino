/* ST7735S model: decodes the command/data byte stream the bootloader sends
 * (CS, DC and RST included) into the 128x128 picture on the glass. With
 * MADCTL 0xC8 the 1.44" panel's visible area starts at column 2, row 3 (the
 * offsets CHGfx uses), and RGB565 data appears as RGB565. It also flags what
 * would look wrong on a real panel: commands too soon after reset or sleep
 * out, and the display switched on over pixels never written since reset. */
#ifndef LCD_MODEL_H
#define LCD_MODEL_H
#include <stdint.h>

#define LCD_W 128
#define LCD_H 128

typedef struct {
    uint16_t fb[LCD_W * LCD_H];
    uint8_t  touched[LCD_W * LCD_H];
    int      on, asleep, in_reset;
    uint64_t reset_release_us, slpout_us;
    int      cmd, nparam;
    uint8_t  param[8];
    uint16_t xs, xe, ys, ye, x, y;
    uint8_t  colmod, madctl;
    int      have_hi;
    uint8_t  hi;
    uint32_t timing_violations;
    uint32_t garbage_shown;
    uint32_t pixels;
    uint32_t dispon_count;
} lcd_model_t;

void lcd_model_power(lcd_model_t *m);
void lcd_model_rst(lcd_model_t *m, int level, uint64_t now_us);
void lcd_model_byte(lcd_model_t *m, uint8_t b, int dc, uint64_t now_us);
int  lcd_model_dump_ppm(const lcd_model_t *m, const char *path);

#endif
