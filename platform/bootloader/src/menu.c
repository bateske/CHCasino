/*
 * The SD game menu (docs/sd-menu.md).
 *
 *   power-on -> card read during the panel's wake-up waits -> list of
 *   GAMES/ *.CHG titles, sorted, the installed one preselected
 *     A/START on the installed game -> RUN reset (no flash write)
 *     A/START on another game       -> install (install.c) -> RUN reset
 *     UP/DOWN, LEFT/RIGHT           -> move, page
 *
 * No card, no FAT volume or no packages: the installed program just runs,
 * as before there was a menu. USB stays alive while the menu is up, so an
 * upload can start at any time: HELLO/STATUS/READ leave the menu alone, any
 * other command hands over to the upload screen.
 *
 * Drawn straight to the panel (lcd.c); the game table lives in RAM.
 */
#include "menu.h"
#include "lcd.h"
#include "sd.h"
#include "fat.h"
#include "chg.h"
#include "install.h"
#include "appmeta.h"
#include "boot.h"
#include "proto.h"
#include "hal.h"
#include "sys.h"
#include "chgame_bootreq.h"

#define FELT      RGB565(0, 104, 52)
#define FELT_DK   RGB565(0, 52, 26)
#define GOLD      RGB565(255, 200, 40)
#define CREAM     RGB565(255, 244, 214)
#define GREY      RGB565(120, 140, 128)
#define CHIP      RGB565(214, 32, 32)

#define TITLE_COLS  19          /* x 8..121 */
#define ROWS        10
#define ROW_H       10
#define LIST_Y      20

#define G_INSTALLED 0x01
#define G_BAD       0x02        /* err holds the reason */

typedef struct {
    uint32_t clus, size;
    uint8_t  flags, err;
    char     title[TITLE_COLS + 3];     /* space-padded, NUL at TITLE_COLS */
} __attribute__((aligned(4))) game_t;
_Static_assert(sizeof(game_t) == 32, "keep game_t at 32 bytes");

static game_t games[MENU_MAX_GAMES];
static uint32_t ngames, sel, top;
static uint8_t buf[512] __attribute__((aligned(4)));

/* ---- the card --------------------------------------------------------------- */

static uint32_t games_dir;

static int find_games(const uint8_t *d, void *ctx)
{
    (void)ctx;
    static const char name[11] = "GAMES      ";
    for (uint32_t i = 0; i < 11; i++) if (d[i] != (uint8_t)name[i]) return 0;
    if ((d[11] & (FAT_ATTR_DIR | FAT_ATTR_LABEL)) != FAT_ATTR_DIR) return 0;   /* a folder, not a label */
    games_dir = fat_entry_cluster(d);
    return 1;
}

static int add_game(const uint8_t *d, void *ctx)
{
    game_t *g = &games[ngames];
    (void)ctx;
    if (ngames == MENU_MAX_GAMES) return 1;
    if (d[11] & (FAT_ATTR_DIR | FAT_ATTR_LABEL | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM)) return 0;
    if (d[8] != 'C' || d[9] != 'H' || d[10] != 'G') return 0;
    if (d[0] == '_') return 0;              /* macOS "._NAME" resource forks */
    g->clus = fat_entry_cluster(d);
    g->size = fat_entry_size(d);
    /* the 8.3 name stands in as the title until the header is read */
    for (uint32_t i = 0; i < TITLE_COLS; i++) g->title[i] = i < 8 ? (char)d[i] : ' ';
    g->title[TITLE_COLS] = 0;
    g->flags = 0;
    ngames++;
    return 0;
}

/* Struct assignment would call newlib's memcpy (a byte loop in flash, and
   178 B of it): entries are 8 words, copied as words. */
static void copy(game_t *d, const game_t *s)
{
    for (uint32_t i = 0; i < sizeof(game_t) / 4; i++)
        ((uint32_t *)(void *)d)[i] = ((const uint32_t *)(const void *)s)[i];
}

static int title_less(const game_t *a, const game_t *b)
{
    for (uint32_t i = 0; i < TITLE_COLS; i++)
        if (a->title[i] != b->title[i]) return a->title[i] < b->title[i];
    return 0;
}

/* Lists the packages and reads their headers. Returns the number of entries
   (with "INSTALLED PROGRAM" when the program in flash is not on the card). */
static uint32_t scan(int app)
{
    const chgame_meta_t *m = appmeta();
    uint32_t i, installed = 0;

    ngames = 0;
    if (sd_init() || fat_mount(buf) || fat_dir(0, buf, find_games, 0) != 1)
        return 0;
    fat_dir(games_dir, buf, add_game, 0);
    for (i = 0; i < ngames; i++) {
        game_t *g = &games[i];
        fat_stream_t s;
        uint32_t lba, n, crc;
        int rc;
        fat_open(&s, g->clus, g->size);
        lba = fat_next_lba(&s, buf);
        rc = lba && !sd_read(lba, buf) ? chg_check(buf, g->size, &n, &crc) : -1;
        if (rc) {
            g->flags = G_BAD;
            g->err = (uint8_t)(rc < 0 ? INST_E_READ : INST_E_PKG + rc);
            continue;
        }
        for (uint32_t k = 0, end = 0; k < TITLE_COLS; k++) {
            char ch = (char)buf[CHG_OFF_TITLE + k];
            if (!ch) end = 1;
            g->title[k] = end ? ' ' : (ch < 32 || ch > 126 ? '?' : ch);
        }
        if (app == APP_VALID && m->length == n && m->crc32 == crc) {
            g->flags = G_INSTALLED;
            installed = 1;
        }
    }
    /* insertion sort by title */
    for (i = 1; i < ngames; i++) {
        game_t t;
        uint32_t j = i;
        copy(&t, &games[i]);
        for (; j && title_less(&t, &games[j - 1]); j--) copy(&games[j], &games[j - 1]);
        copy(&games[j], &t);
    }
    if (!ngames) return 0;
    if (app == APP_VALID && !installed && ngames < MENU_MAX_GAMES) {
        static const char t[] = "INSTALLED PROGRAM  ";
        for (i = ngames; i; i--) copy(&games[i], &games[i - 1]);
        games[0].clus = 0;
        games[0].flags = G_INSTALLED;
        for (i = 0; i <= TITLE_COLS; i++) games[0].title[i] = t[i];
        ngames++;
    }
    return ngames;
}

/* ---- drawing ------------------------------------------------------------------- */

static void text_c(uint32_t y, const char *s, uint16_t fg, uint16_t bg, uint32_t scale)
{
    uint32_t n = 0;
    while (s[n]) n++;
    lcd_text((LCD_W - n * 6 * scale) / 2, y, s, n, fg, bg, scale);
}

static void draw_row(uint32_t i)
{
    uint32_t y = LIST_Y + (i - top) * ROW_H;
    uint16_t bg = FELT, fg = CREAM;
    game_t *g = &games[i];
    if (i == sel) { bg = GOLD; fg = FELT_DK; }
    else if (g->flags & G_BAD) fg = GREY;
    lcd_fill(0, y, LCD_W, ROW_H, bg);
    if (g->flags & G_INSTALLED) lcd_fill(2, y + 2, 3, 6, CHIP);
    lcd_text(8, y + 1, g->title, TITLE_COLS, fg, bg, 1);
}

static void draw_footer(void)
{
    char s[8];
    uint32_t a = sel + 1, b = ngames, k = 8;
    s[--k] = 0;
    do s[--k] = (char)('0' + b % 10); while (b /= 10);
    s[--k] = '/';
    do s[--k] = (char)('0' + a % 10); while (a /= 10);
    lcd_fill(0, 120, LCD_W, 8, FELT_DK);
    lcd_text(2, 120, "A:PLAY", 6, GOLD, FELT_DK, 1);
    lcd_text(LCD_W - 2 - (7 - k) * 6, 120, s + k, 8, CREAM, FELT_DK, 1);
}

static void draw_list(void)
{
    uint32_t i;
    lcd_fill(0, 0, LCD_W, LIST_Y, FELT_DK);
    text_c(2, "CHGAME", GOLD, FELT_DK, 2);
    for (i = top; i < top + ROWS; i++) {
        if (i < ngames) draw_row(i);
        else lcd_fill(0, LIST_Y + (i - top) * ROW_H, LCD_W, ROW_H, FELT);
    }
    draw_footer();
}

static void box(const char *l1, const char *l2)
{
    lcd_fill(6, 36, LCD_W - 12, 52, GOLD);
    lcd_fill(8, 38, LCD_W - 16, 48, FELT_DK);
    text_c(46, l1, GOLD, FELT_DK, 1);
    if (l2) text_c(60, l2, CREAM, FELT_DK, 1);
}

void menu_progress(uint32_t done, uint32_t total)
{
    lcd_fill(16, 72, ((LCD_W - 32) * (done + 1)) / total, 6, GOLD);
}

/* ---- keys ------------------------------------------------------------------------- */

static uint32_t k_prev, k_last, k_rep;

/* Newly pressed keys, sampled every 15 ms (debounce), with UP/DOWN repeating
   after 400 ms every 80 ms. Keys held when the menu starts count only after a
   release. */
static uint32_t keys(void)
{
    uint32_t t = sys_ticks(), now, out;
    if (t - k_last < 15u * SYS_TICKS_PER_MS) return 0;
    k_last = t;
    now = hal_buttons();
    out = now & ~k_prev;
    if (out) k_rep = t + 400u * SYS_TICKS_PER_MS;
    else if ((now & (BTN_UP | BTN_DOWN)) && (int32_t)(t - k_rep) >= 0) {
        out = now & (BTN_UP | BTN_DOWN);
        k_rep = t + 80u * SYS_TICKS_PER_MS;
    }
    k_prev = now;
    return out;
}

/* Waits for every key to be up (so a game never starts with A still held). */
static void release(void)
{
    uint32_t t = sys_ticks();
    while (sys_ticks() - t < 30u * SYS_TICKS_PER_MS)
        if (hal_buttons()) t = sys_ticks();
}

static void wait_key(void)
{
    while (!(keys() & (BTN_A | BTN_B | BTN_START))) { }
}

static const char *const why[] = {
    "", "CARD READ ERROR", "FILE DAMAGED", "NOT A GAME", "NO GAME INSTALLED",
    "", "", "", "",
    "NOT A GAME FILE", "FILE DAMAGED", "NEWER FORMAT", "WRONG DEVICE", "BAD FILE SIZE",
};

/* ---- the menu ------------------------------------------------------------------------ */

#if CHBOOT_APP
/* Dry run (build.sh app): START cycles the SD clock, A checks a package
   without installing it and shows the time per block, SELECT leaves through
   the old bootloader's USB mode. */
static uint32_t dry_br = SD_SPI_BR;
#endif

void menu_main(int app)
{
    uint32_t k, n;

    lcd_reset();
    n = scan(app);                          /* the card is read during the panel's waits */
#if !CHBOOT_APP
    if (!n && app == APP_VALID)
        boot_reset(CHGAME_BOOTREQ_RUN);     /* nothing on the card: run what is installed */
#endif
    lcd_wake();
    lcd_on(FELT);
    if (!n) {
        box("NO GAMES FOUND", "SD CARD: /GAMES");
        return;
    }
#if !CHBOOT_APP
    proto_init();
#endif
    for (sel = 0; sel < ngames && !(games[sel].flags & G_INSTALLED); sel++) { }
    if (sel == ngames) sel = 0;
    top = sel >= ROWS ? sel - ROWS + 1 : 0;
    k_prev = hal_buttons();
    draw_list();

    for (;;) {
        uint32_t old = sel, old_top = top;
#if !CHBOOT_APP
        proto_task();
        if (proto_claimed) {
            box("USB UPLOAD", "B: MENU");
            return;
        }
#endif
        k = keys();
        if (k & BTN_UP)    sel = sel ? sel - 1 : ngames - 1;
        if (k & BTN_DOWN)  sel = sel + 1 < ngames ? sel + 1 : 0;
        if (k & BTN_LEFT)  sel = sel >= ROWS ? sel - ROWS : 0;
        if (k & BTN_RIGHT) sel = sel + ROWS < ngames ? sel + ROWS : ngames - 1;
        if (sel < top) top = sel;
        if (sel >= top + ROWS) top = sel - ROWS + 1;
#if CHBOOT_APP
        if (k & BTN_SELECT) boot_reset(CHGAME_BOOTREQ_USB);
        if (k & BTN_START) {
            static const char *const mhz[] = { "SD 24 MHZ", "SD 12 MHZ", "SD 6 MHZ" };
            dry_br = dry_br >= SPI_BR_6M ? SPI_BR_24M : dry_br + 1;
            hal_spi_speed(dry_br);
            box(mhz[dry_br], "A: CHECK A GAME");
            wait_key();
            draw_list();
        }
#endif
        if (k & (BTN_A
#if !CHBOOT_APP
                 | BTN_START
#endif
                 )) {
            game_t *g = &games[sel];
            int rc = INST_OK;
            if (g->flags & G_BAD) {
                rc = g->err;
            } else if (!(g->flags & G_INSTALLED) || CHBOOT_APP) {
#if CHBOOT_APP
                uint32_t t0 = sys_ticks(), ms;
#endif
                box("INSTALLING", g->title);
                rc = install(g->clus, g->size, buf);
#if CHBOOT_APP
                if (rc == INST_OK) {
                    char s[] = "     MS PER BLOCK";
                    ms = (sys_ticks() - t0) / SYS_TICKS_PER_MS * 100u / ((g->size >> 9) + 1u);
                    s[3] = (char)('0' + ms % 10); s[2] = '.'; ms /= 10;
                    s[1] = (char)('0' + ms % 10); ms /= 10;
                    s[0] = ms ? (char)('0' + ms % 10) : ' ';
                    box("DRY RUN OK", s);
                    wait_key();
                    draw_list();
                    continue;
                }
#endif
            }
            if (rc == INST_OK) {
                release();
                boot_reset(CHGAME_BOOTREQ_RUN);
            }
            if (rc == INST_E_LOST)
                for (n = 0; n < ngames; n++) games[n].flags &= (uint8_t)~G_INSTALLED;
            box(rc == INST_E_LOST ? "INSTALL FAILED" : "CAN'T INSTALL", why[rc < (int)(sizeof why / sizeof why[0]) ? rc : 1]);
            wait_key();
            draw_list();
            continue;
        }
        if (top != old_top) draw_list();
        else if (sel != old) { draw_row(old); draw_row(sel); draw_footer(); }
    }
}

void menu_usb_notice(void)
{
#if !CHBOOT_APP
    proto_init();                           /* enumerate first; the panel can wait */
#endif
    lcd_reset();
    lcd_wake();
    lcd_on(FELT);
    box("USB UPLOAD", "B: MENU");
}
