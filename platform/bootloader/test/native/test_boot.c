/* Whole-boot scenarios with the SD menu: each boot is a fresh process (see
 * host.h). argv[1]: the manifest written by boot_cases.py (card images,
 * packages); argv[2]: the folder for frame dumps. */
#include "testlib.h"
#include "hal.h"
#include "proto.h"
#include "bootreq.h"
#include "chgame_bootreq.h"
#include "chg_format.h"

#define MAXP 32
static struct { char name[16]; int code; uint32_t len, crc; char path[512]; } pk[MAXP];
static int npk;
static char img_fat32[512], img_fat16[512], img_nogames[512], img_boot[512];
static const char *frames;
static uint8_t payload[CHGAME_APP_MAX_SIZE];

static int pkg(const char *name)
{
    for (int i = 0; i < npk; i++) if (!strcmp(pk[i].name, name)) return i;
    fprintf(stderr, "no package %s\n", name); exit(2);
}

/* Installs a package's payload straight into flash, as a finished USB upload
   of the same .bin would leave it. */
static void preinstall(const char *name)
{
    int i = pkg(name);
    FILE *f = fopen(pk[i].path, "rb");
    fseek(f, CHG_HEADER_BYTES, SEEK_SET);
    size_t n = fread(payload, 1, pk[i].len, f);
    fclose(f);
    host_install_app(payload, (uint32_t)n);
}

static int installed_is(const char *name)
{
    int i = pkg(name);
    return app_valid_now() && le32(B->flash + CHGAME_META_ADDR + 8) == pk[i].len &&
           le32(B->flash + CHGAME_META_ADDR + 12) == pk[i].crc;
}

static void snap(const char *tag)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s.ppm", frames, tag);
    lcd_model_dump_ppm(&B->lcd, p);
}

static void card(const char *path) { sd_model_insert(&B->sd, path ? SD_SDHC : SD_NONE, path); }

/* press keys `mask` at +at_ms for 100 ms */
static void press(uint32_t at_ms, uint32_t mask)
{
    host_keys(at_ms, mask);
    host_keys(at_ms + 100, 0);
}

static void lcd_sane(const char *what)
{
    CHECK(B->lcd.timing_violations == 0, "%s: panel timing violations %u", what, B->lcd.timing_violations);
    CHECK(B->lcd.garbage_shown == 0, "%s: display switched on over unwritten pixels", what);
    CHECK(B->bus_conflicts == 0, "%s: both chip selects low", what);
    CHECK(B->sd.init_fast == 0, "%s: card identified above 400 kHz", what);
}

/* Index of a title in the menu's sorted order (the zoo is sorted by title in
   boot_cases.py and written in that order). */
static int menu_index(const char *name) { return pkg(name); }

static void go_to(uint32_t at_ms, int from, int to)
{
    for (int i = from; i < to; i++) press(at_ms + 150u * (uint32_t)(i - from), BTN_DOWN);
    for (int i = from; i > to; i--) press(at_ms + 150u * (uint32_t)(from - i), BTN_UP);
}

/* ---- scenarios ------------------------------------------------------------------- */

static void t_no_card_runs_app(void)
{
    preinstall("BRAVO.CHG");
    card(NULL);
    CHECK(host_boot() == END_RESET, "no card: RUN reset");
    CHECK(B->retained[0] == CHGAME_BOOTREQ_RUN, "RUN requested");
    CHECK(host_boot() == END_JUMP, "then the app starts");
    CHECK(B->now_us < 400000, "power-on to app in %llu ms without a card", (unsigned long long)(B->now_us / 1000));
    CHECK(B->flash_ops == 0, "no flash writes");
}

static void t_no_card_no_app(void)
{
    card(NULL);
    B->limit_us = 1500000;
    CHECK(host_boot() == END_HANG, "no card, no app: waits");
    CHECK(B->usb_up, "in USB mode");
    CHECK(B->lcd.on, "and says so on the panel");
    lcd_sane("no card");
    snap("no_games");
}

static void t_empty_card_runs_app(void)
{
    preinstall("BRAVO.CHG");
    card(img_nogames);
    CHECK(host_boot() == END_RESET && B->retained[0] == CHGAME_BOOTREQ_RUN, "a card without GAMES: the app runs");
}

static void t_menu_waits(void)
{
    card(img_fat32);
    B->limit_us = 2000000;
    CHECK(host_boot() == END_HANG, "menu waits for a key");
    CHECK(B->usb_up, "USB alive at the menu");
    CHECK(B->lcd.on, "panel on");
    lcd_sane("menu");
    CHECK(B->flash_ops == 0, "showing the menu writes nothing");
    snap("menu_fresh");
}

static void t_install_then_run(void)
{
    card(img_fat32);
    press(800, BTN_A);                       /* ALPHA is first and preselected */
    CHECK(host_boot() == END_RESET, "install ends in a reset");
    CHECK(B->retained[0] == CHGAME_BOOTREQ_RUN, "with a RUN request");
    CHECK(installed_is("ALPHA.CHG"), "ALPHA installed");
    CHECK(B->boot_region_writes == 0, "boot region untouched");
    lcd_sane("install");
    CHECK(host_boot() == END_JUMP, "ALPHA starts");
    /* power cycle: menu again, ALPHA preselected, A starts it with no writes */
    host_power_cycle();
    host_keys_clear();
    uint32_t ops = B->flash_ops;
    press(800, BTN_A);
    CHECK(host_boot() == END_RESET && B->retained[0] == CHGAME_BOOTREQ_RUN, "installed game: A runs it");
    CHECK(B->flash_ops == ops, "without writing flash (%u ops)", B->flash_ops - ops);
    CHECK(host_boot() == END_JUMP, "and it starts");
}

static void t_installed_marked(void)
{
    preinstall("BRAVO.CHG");
    card(img_fat32);
    B->limit_us = 2000000;
    CHECK(host_boot() == END_HANG, "menu");
    snap("menu_installed");
}

static void t_switch_game(void)
{
    preinstall("ALPHA.CHG");
    card(img_fat32);
    go_to(800, menu_index("ALPHA.CHG"), menu_index("BRAVO.CHG"));
    press(800 + 150 * 4, BTN_A);
    CHECK(host_boot() == END_RESET && installed_is("BRAVO.CHG"), "switched to BRAVO");
    uint32_t pages = (pk[pkg("BRAVO.CHG")].len + 255) / 256;
    CHECK(B->erase_count[CHGAME_META_ADDR / 256] == 1, "metadata page erased once (%u)", B->erase_count[CHGAME_META_ADDR / 256]);
    uint32_t max = 0;
    for (uint32_t p = CHGAME_APP_START / 256; p < CHGAME_APP_START / 256 + pages; p++) if (B->erase_count[p] > max) max = B->erase_count[p];
    CHECK(max <= 1, "no page erased twice");
    CHECK(B->erase_count[0xF500 / 256] == 0 && B->erase_count[0xF600 / 256] == 0, "game save pages untouched");
}

static void t_bad_packages(void)
{
    static const char *bad[] = { "BADHCRC.CHG", "NOTCHG.CHG", "WRONGTGT.CHG", "WRONGLAY.CHG", "WRONGVER.CHG",
                                 "ZEROLEN.CHG", "ODDLEN.CHG", "TOOBIG.CHG", "TRUNC.CHG", "BOOTIMG.CHG", "BADPCRC.CHG" };
    /* BRAVO installed (BADPCRC's header describes ALPHA's payload: with ALPHA
       installed it would rightly count as installed and just run ALPHA) */
    int from = menu_index("BRAVO.CHG");
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        host_init();
        preinstall("BRAVO.CHG");
        card(img_fat32);
        B->limit_us = 4000000;
        int idx = menu_index(bad[i]);
        go_to(800, from, idx);
        press(800 + 150u * (uint32_t)(idx > from ? idx - from : from - idx) + 200, BTN_A);
        int end = host_boot();
        CHECK(end == END_HANG, "%s: stays in the menu (end %d)", bad[i], end);
        CHECK(B->flash_ops == 0, "%s: nothing written (%u ops)", bad[i], B->flash_ops);
        CHECK(installed_is("BRAVO.CHG"), "%s: BRAVO still installed", bad[i]);
        if (i == 0) snap("error_box");
    }
}

static void t_usb_notice(void)
{
    preinstall("ALPHA.CHG");
    card(img_fat32);
    bootreq_set(CHGAME_BOOTREQ_USB);
    B->limit_us = 1500000;
    CHECK(host_boot() == END_HANG && B->usb_up, "USB request: upload mode");
    CHECK(B->sd.cmds == 0, "card not touched");
    lcd_sane("usb notice");
    snap("usb_notice");
    /* B (a fresh press) returns to the menu */
    bootreq_set(CHGAME_BOOTREQ_USB);
    press(600, BTN_B);
    CHECK(host_boot() == END_RESET, "B leaves USB mode");
    CHECK(B->retained[0] == 0, "with no request: the menu");
}

static void t_b_held_escape(void)
{
    preinstall("ALPHA.CHG");
    card(img_fat32);
    host_keys(0, BTN_B);                     /* held from power-on... */
    host_keys(1200, 0);                      /* ...released after 1.2 s */
    B->limit_us = 2000000;
    CHECK(host_boot() == END_HANG && B->usb_up, "B held at power-on: USB mode");
    CHECK(B->sd.cmds == 0 && B->lcd.pixels == 0, "card and panel untouched");
    host_keys_clear();
    host_keys(0, BTN_B);
    host_keys(1200, 0);
    host_keys(1500, BTN_B);                  /* a fresh press */
    host_keys(1600, 0);
    CHECK(host_boot() == END_RESET, "only a fresh press of B leaves USB mode");
}

static void t_hello_at_menu(void)
{
    uint8_t rd[6] = { 0, 0x30, 0, 0, 32, 0 };
    card(img_fat32);
    B->limit_us = 2000000;
    host_boot();
    snap("menu_quiet");
    uint16_t quiet[LCD_W * LCD_H];
    memcpy(quiet, B->lcd.fb, sizeof quiet);
    host_init();
    card(img_fat32);
    B->limit_us = 2000000;
    for (int i = 0; i < 5; i++) { frame_push(CMD_HELLO, NULL, 0); frame_push(CMD_STATUS, NULL, 0); frame_push(CMD_READ, rd, 6); }
    const char junk[] = "AT\r\nATE0V1\r\n";
    memcpy(B->rx + B->rx_len, junk, sizeof junk - 1); B->rx_len += sizeof junk - 1;
    CHECK(host_boot() == END_HANG, "probing host: menu stays");
    CHECK(!memcmp(quiet, B->lcd.fb, sizeof quiet), "menu unchanged by HELLO/STATUS/READ and modem noise");
    CHECK(B->tx_len > 0, "probes answered");
}

static void t_upload_at_menu(void)
{
    uint8_t img[8192];
    preinstall("ALPHA.CHG");
    card(img_fat32);
    make_image(img, sizeof img, 77);
    push_upload(img, sizeof img, 1);
    CHECK(host_boot() == END_RESET && B->retained[0] == CHGAME_BOOTREQ_RUN, "upload at the menu, then RUN");
    CHECK(app_valid_now() && le32(B->flash + CHGAME_META_ADDR + 8) == sizeof img, "uploaded sketch installed");
    host_power_cycle();
    B->limit_us = 2000000;
    CHECK(host_boot() == END_HANG, "next power-on: menu");
    snap("menu_usb_sketch");                 /* shows INSTALLED PROGRAM */
}

static void t_card_dies_mid_install(void)
{
    preinstall("ALPHA.CHG");
    card(img_fat32);
    go_to(800, 0, menu_index("BRAVO.CHG"));
    press(800 + 150 * 4, BTN_A);
    B->card_dies_on_flash = 1;
    B->limit_us = 8000000;
    CHECK(host_boot() == END_HANG, "card dies in pass 2: menu shows the failure");
    CHECK(!app_valid_now(), "no program left installed (never a partial one)");
    snap("install_failed");
    host_power_cycle();
    host_keys_clear();
    B->card_dies_on_flash = 0;
    B->limit_us = 8000000;
    go_to(800, 0, menu_index("BRAVO.CHG"));
    press(800 + 150 * 4, BTN_A);
    CHECK(host_boot() == END_RESET && installed_is("BRAVO.CHG"), "next power-on: BRAVO installs");
}

static void t_powercut_sweep(void)
{
    /* reference: install BRAVO over ALPHA, count flash operations */
    preinstall("ALPHA.CHG");
    card(img_fat32);
    go_to(800, 0, menu_index("BRAVO.CHG"));
    press(800 + 150 * 4, BTN_A);
    host_boot();
    uint32_t total = B->flash_ops;
    CHECK(installed_is("BRAVO.CHG") && total > 4, "reference install (%u ops)", total);
    int bad = 0;
    for (uint32_t k = 1; k <= total; k++) {
        host_init();
        preinstall("ALPHA.CHG");
        card(img_fat32);
        go_to(800, 0, menu_index("BRAVO.CHG"));
        press(800 + 150 * 4, BTN_A);
        B->cut_at_op = k;
        if (host_boot() != END_POWERCUT) { bad++; continue; }
        B->cut_at_op = 0;
        host_keys_clear();
        int complete = installed_is("BRAVO.CHG");
        int alpha = installed_is("ALPHA.CHG");
        if (app_valid_now() && !complete && !alpha) { bad++; fprintf(stderr, "cut %u: a partial image is launchable\n", k); }
        if (alpha && k > 1) { bad++; fprintf(stderr, "cut %u: ALPHA survived after the erase began\n", k); }
        /* next power-on: the menu (or BRAVO if it completed); pressing A
           (re)installs BRAVO, which must succeed */
        B->limit_us = 6000000;
        go_to(800, 0, menu_index("BRAVO.CHG") - (app_valid_now() ? 0 : 0));
        press(800 + 150 * 4, BTN_A);
        int end = host_boot();
        if (end == END_JUMP) { bad++; fprintf(stderr, "cut %u: jumped without a RUN request\n", k); }
        if (!installed_is("BRAVO.CHG")) { bad++; fprintf(stderr, "cut %u: reinstall failed (end %d)\n", k, end); }
        if (B->boot_region_writes) { bad++; fprintf(stderr, "cut %u: boot region written\n", k); }
    }
    CHECK(bad == 0, "power cut at each of %u flash operations of an SD install", total);
}

/* The real card from tools/sdcard/mkcard.py: install every program in menu
   order, each over the one before, and check what is installed each time. */
static void t_real_card(void)
{
    card(img_fat32);
    B->limit_us = 2000000;
    CHECK(host_boot() == END_HANG, "real card: menu");
    lcd_sane("real menu");
    snap("real_menu");
    host_keys_clear();
    int sel = 0;
    for (int i = 0; i < npk; i++) {
        B->limit_us = 8000000;
        go_to(800, sel, i);
        press(800 + 150u * (uint32_t)(i > sel ? i - sel : sel - i) + 200, BTN_A);
        int end = host_boot();
        CHECK(end == END_RESET && installed_is(pk[i].name), "%s installs (end %d)", pk[i].name, end);
        CHECK(host_boot() == END_JUMP, "%s starts", pk[i].name);
        CHECK(B->boot_region_writes == 0, "boot region untouched");
        host_power_cycle();
        host_keys_clear();
        sel = i;                             /* preselected next time */
        if (i == 9) { B->limit_us = 2000000; host_boot(); snap("real_menu_installed"); host_power_cycle(); }
    }
}

int main(int argc, char **argv)
{
    char line[1024];
    FILE *f = fopen(argv[1], "r");
    frames = argv[2];
    if (!f) { perror(argv[1]); return 2; }
    while (fgets(line, sizeof line, f)) {
        char a[32], b[512], c[512];
        if (sscanf(line, "%31s", a) != 1) continue;
        if (!strcmp(a, "img")) {
            sscanf(line, "%*s %511s %511s", b, c);
            if (!strcmp(b, "fat32")) snprintf(img_fat32, sizeof img_fat32, "%s", c);
            if (!strcmp(b, "fat16")) snprintf(img_fat16, sizeof img_fat16, "%s", c);
            if (!strcmp(b, "nogames")) snprintf(img_nogames, sizeof img_nogames, "%s", c);
            if (!strcmp(b, "boot")) snprintf(img_boot, sizeof img_boot, "%s", c);
        } else if (!strcmp(a, "pkg") && npk < MAXP) {
            sscanf(line, "%*s %15s %d %u %x %511s", pk[npk].name, &pk[npk].code, &pk[npk].len, &pk[npk].crc, pk[npk].path);
            npk++;
        }
    }
    fclose(f);
    if (argc > 3 && !strcmp(argv[3], "real")) {
        TEST(t_real_card);
        return test_summary();
    }
    TEST(t_no_card_runs_app);
    TEST(t_no_card_no_app);
    TEST(t_empty_card_runs_app);
    TEST(t_menu_waits);
    TEST(t_install_then_run);
    TEST(t_installed_marked);
    TEST(t_switch_game);
    TEST(t_bad_packages);
    TEST(t_usb_notice);
    TEST(t_b_held_escape);
    TEST(t_hello_at_menu);
    TEST(t_upload_at_menu);
    TEST(t_card_dies_mid_install);
    TEST(t_powercut_sweep);
    return test_summary();
}
