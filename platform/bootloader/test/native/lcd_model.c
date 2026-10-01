#include "lcd_model.h"
#include <stdio.h>
#include <string.h>

void lcd_model_power(lcd_model_t *m)
{
    memset(m, 0, sizeof *m);
    for (int i = 0; i < LCD_W * LCD_H; i++) m->fb[i] = (uint16_t)(i * 2654435761u >> 16);   /* noise */
    m->asleep = 1;
}

static void hw_reset(lcd_model_t *m)
{
    m->on = 0;
    m->asleep = 1;
    memset(m->touched, 0, sizeof m->touched);
    for (int i = 0; i < LCD_W * LCD_H; i++) m->fb[i] = (uint16_t)(i * 2246822519u >> 16);
}

void lcd_model_rst(lcd_model_t *m, int level, uint64_t now_us)
{
    if (!level) { m->in_reset = 1; hw_reset(m); }
    else if (m->in_reset) { m->in_reset = 0; m->reset_release_us = now_us; }
}

static void command(lcd_model_t *m, uint8_t c, uint64_t now)
{
    m->cmd = c;
    m->nparam = 0;
    m->have_hi = 0;
    if (m->in_reset) { m->timing_violations++; return; }
    if (now - m->reset_release_us < 120000 && c != 0x00) {
        /* ST7735S: 120 ms after a reset before Sleep Out (and anything else) */
        m->timing_violations++;
    }
    switch (c) {
    case 0x01: hw_reset(m); m->reset_release_us = now; break;   /* SWRESET */
    case 0x11: m->asleep = 0; m->slpout_us = now; break;        /* SLPOUT */
    case 0x10: m->asleep = 1; break;
    case 0x28: m->on = 0; break;
    case 0x29:                                                   /* DISPON */
        if (now - m->slpout_us < 5000) m->timing_violations++;
        m->on = 1;
        m->dispon_count++;
        for (int i = 0; i < LCD_W * LCD_H; i++)
            if (!m->touched[i]) { m->garbage_shown++; break; }
        break;
    case 0x2C: m->x = m->xs; m->y = m->ys; break;                /* RAMWR */
    default: break;
    }
}

void lcd_model_byte(lcd_model_t *m, uint8_t b, int dc, uint64_t now)
{
    if (!dc) { command(m, b, now); return; }
    if (m->in_reset) return;
    if (m->cmd == 0x2C) {
        if (!m->have_hi) { m->hi = b; m->have_hi = 1; return; }
        m->have_hi = 0;
        int lx = (int)m->x - 2, ly = (int)m->y - 3;
        if (lx >= 0 && lx < LCD_W && ly >= 0 && ly < LCD_H) {
            m->fb[ly * LCD_W + lx] = (uint16_t)(m->hi << 8 | b);
            m->touched[ly * LCD_W + lx] = 1;
        }
        m->pixels++;
        if (++m->x > m->xe) { m->x = m->xs; if (++m->y > m->ye) m->y = m->ys; }
        return;
    }
    if (m->nparam < 8) m->param[m->nparam] = b;
    m->nparam++;
    switch (m->cmd) {
    case 0x2A: if (m->nparam == 4) { m->xs = (uint16_t)(m->param[0] << 8 | m->param[1]); m->xe = (uint16_t)(m->param[2] << 8 | m->param[3]); } break;
    case 0x2B: if (m->nparam == 4) { m->ys = (uint16_t)(m->param[0] << 8 | m->param[1]); m->ye = (uint16_t)(m->param[2] << 8 | m->param[3]); } break;
    case 0x36: m->madctl = b; break;
    case 0x3A: m->colmod = b; break;
    default: break;
    }
}

int lcd_model_dump_ppm(const lcd_model_t *m, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        uint16_t c = m->on ? m->fb[i] : 0xFFFF;   /* panel off: white backlight */
        uint8_t px[3] = { (uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 0;
}
