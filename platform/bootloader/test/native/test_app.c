/* The dry-run build (build.sh app, CHBOOT_APP=1): the menu as a program under
 * the old bootloader. It must never write flash, must check packages, and
 * SELECT must leave through the old bootloader's USB request. */
#include "testlib.h"
#include "hal.h"
#include "chgame_bootreq.h"

static char card_path[512];

static void t_dry_run(void)
{
    sd_model_insert(&B->sd, SD_SDHC, card_path);
    B->limit_us = 6000000;
    host_keys(800, BTN_A);   host_keys(900, 0);      /* check the first package */
    int end = host_boot();
    CHECK(end == END_HANG, "dry run waits (end %d)", end);
    CHECK(B->flash_ops == 0, "no flash writes (%u)", B->flash_ops);
    CHECK(B->sd.reads > 40, "the package was read (%u reads)", B->sd.reads);
    CHECK(!B->usb_up, "no USB in the dry-run build");
    lcd_model_dump_ppm(&B->lcd, "build/frames/dryrun_ok.ppm");
    host_keys_clear();
    host_keys(800, BTN_START); host_keys(900, 0);   /* SD clock */
    host_keys(1200, BTN_A);    host_keys(1300, 0);  /* dismiss */
    host_keys(1600, BTN_SELECT); host_keys(1700, 0);
    end = host_boot();
    CHECK(end == END_RESET, "SELECT resets (end %d)", end);
    CHECK(B->retained[0] == CHGAME_BOOTREQ_USB && B->retained[1] == ~CHGAME_BOOTREQ_USB, "with the old bootloader's USB request");
}

int main(int argc, char **argv)
{
    snprintf(card_path, sizeof card_path, "%s", argv[1]);
    TEST(t_dry_run);
    return test_summary();
}
