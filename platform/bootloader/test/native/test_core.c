/* Update path, USB protocol, boot decision and power cuts during USB uploads.
 * Built twice: with the SD menu (CHBOOT_MENU=1) and without (0). */
#include "testlib.h"
#include "update.h"
#include "proto.h"
#include "appmeta.h"
#include "boot.h"
#include "bootreq.h"
#include "chgame_bootreq.h"
#include <setjmp.h>

extern jmp_buf host_jmp;
extern int host_jmp_armed;

static uint8_t img[CHGAME_APP_MAX_SIZE], img2[CHGAME_APP_MAX_SIZE];

static void t_update_basic(void)
{
    uint32_t len = 3000, pages = (len + 255) / 256;
    uint8_t page[256];
    make_image(img, len, 1);
    CHECK(upd_begin() == ST_OK, "begin");
    for (uint32_t p = 0; p < pages; p++) {
        memset(page, 0xFF, sizeof page);
        memcpy(page, img + p * 256, len - p * 256 < 256 ? len - p * 256 : 256);
        CHECK(upd_page(CHGAME_APP_START + p * 256, page) == ST_OK, "page %u", p);
    }
    CHECK(upd_end(len, host_crc32(img, len)) == ST_OK, "end");
    CHECK(app_valid_now(), "app valid after update");
    CHECK(appmeta_check() == APP_VALID, "appmeta agrees");
    CHECK(B->erase_count[CHGAME_META_ADDR / 256] == 1, "metadata erased once, then programmed (got %u)", B->erase_count[CHGAME_META_ADDR / 256]);
    CHECK(B->erase_count[CHGAME_APP_START / 256] == 1, "first page erased once");
    /* the same image again: only the metadata page is erased */
    uint32_t ops = B->flash_ops;
    CHECK(upd_begin() == ST_OK, "begin 2");
    for (uint32_t p = 0; p < pages; p++) {
        memset(page, 0xFF, sizeof page);
        memcpy(page, img + p * 256, len - p * 256 < 256 ? len - p * 256 : 256);
        upd_page(CHGAME_APP_START + p * 256, page);
    }
    CHECK(upd_end(len, host_crc32(img, len)) == ST_OK, "end 2");
    CHECK(B->flash_ops - ops == 2, "reinstall costs one erase + one program (got %u ops)", B->flash_ops - ops);
    CHECK(B->erase_count[CHGAME_APP_START / 256] == 1, "identical page not erased again");
    CHECK(B->boot_region_writes == 0, "boot region untouched");
}

static void t_update_errors(void)
{
    uint8_t page[256];
    memset(page, 0x55, sizeof page);
    CHECK(upd_page(CHGAME_APP_START - 256, page) == ST_ERR_RANGE, "page below app region refused");
    CHECK(upd_page(CHGAME_META_ADDR, page) == ST_ERR_RANGE, "metadata page refused as image page");
    CHECK(upd_page(0, page) == ST_ERR_RANGE, "page 0 refused");
    CHECK(B->boot_region_writes == 0, "boot region untouched");
    make_image(img, 1024, 2);
    host_install_app(img, 1024);
    CHECK(app_valid_now(), "installed");
    upd_begin();
    CHECK(!app_valid_now(), "begin invalidates");
    CHECK(upd_end(1024, 0x12345678u) == ST_ERR_CRC, "wrong CRC refused");
    CHECK(!app_valid_now(), "still invalid after a failed end");
}

static void t_signature(void)
{
    make_image(img, 2048, 3);
    put32(img + CHGAME_BOOT_SIG_OFFSET, CHGAME_BOOT_SIG);
    host_install_app(img, 2048);
    CHECK(appmeta_check() == APP_INVALID_BOOTIMAGE, "a staged bootloader image is never an app");
}

static void t_proto_claim(void)
{
    uint8_t cmd, pl[64]; uint16_t n;
    proto_init();
    frame_push(CMD_HELLO, NULL, 0);
    frame_push(CMD_STATUS, NULL, 0);
    uint8_t rd[6] = {0, 0x30, 0, 0, 16, 0};
    frame_push(CMD_READ, rd, 6);
    for (int i = 0; i < 20; i++) proto_task();
    CHECK(!proto_claimed, "HELLO/STATUS/READ do not claim the bootloader");
    CHECK(frame_pop(&cmd, pl, &n) && cmd == (CMD_HELLO | 0x80) && pl[0] == ST_OK, "HELLO answered");
    CHECK(pl[2] == MODE_BOOTLOADER, "mode bootloader");
    CHECK((pl[4] | pl[5] << 8) == BOOT_VERSION, "boot version");
    /* garbage, then a valid frame: resynchronises */
    const char junk[] = "AT+GMM\r\nCGx\x01";
    memcpy(B->rx + B->rx_len, junk, sizeof junk - 1); B->rx_len += sizeof junk - 1;
    frame_push(CMD_ABORT, NULL, 0);
    for (int i = 0; i < 40; i++) proto_task();
    CHECK(proto_claimed, "ABORT claims");
}

static void t_usb_upload_run(void)
{
    uint8_t cmd, pl[64]; uint16_t n;
    uint32_t len = 20000;
    make_image(img, len, 4);
    bootreq_set(CHGAME_BOOTREQ_USB);
    push_upload(img, len, 1);
    int end = host_boot();
    CHECK(end == END_RESET, "upload + RUN ends in a reset (got %d)", end);
    CHECK(B->retained[0] == CHGAME_BOOTREQ_RUN && B->retained[1] == ~CHGAME_BOOTREQ_RUN, "RUN request left for the next boot");
    int all_ok = 1, count = 0;
    while (frame_pop(&cmd, pl, &n)) { count++; if (pl[0] != ST_OK) { all_ok = 0; fprintf(stderr, "cmd %02x st %u\n", cmd, pl[0]); } }
    CHECK(all_ok && count == 2 + (int)((len + 255) / 256) + 1, "every response OK (%d responses)", count);
    CHECK(app_valid_now(), "uploaded app valid");
    for (uint32_t p = CHGAME_APP_START / 256; p < (CHGAME_APP_START + len + 255) / 256; p++)
        if (B->erase_count[p] != 1) { CHECK(0, "page %u erased %u times", p, B->erase_count[p]); break; }
    end = host_boot();
    CHECK(end == END_JUMP, "RUN request starts the app (got %d)", end);
    CHECK(B->retained[0] == 0 && B->retained[1] == 0, "request consumed");
    CHECK(B->boot_region_writes == 0, "boot region untouched");
}

static void t_usb_powercut_sweep(void)
{
    uint32_t len = 3000;          /* 12 pages: a complete sweep stays quick */
    make_image(img, len, 5);
    make_image(img2, 6000, 6);
    /* reference run: how many flash operations does the upload take? */
    host_install_app(img2, 6000);
    bootreq_set(CHGAME_BOOTREQ_USB);
    push_upload(img, len, 0);
    B->limit_us = 3000000;
    host_boot();
    uint32_t total = B->flash_ops;
    CHECK(app_valid_now(), "reference upload valid");
    int bad = 0;
    for (uint32_t k = 1; k <= total; k++) {
        host_init();
        B->limit_us = 3000000;
        host_install_app(img2, 6000);
        bootreq_set(CHGAME_BOOTREQ_USB);
        push_upload(img, len, 0);
        B->cut_at_op = k;
        int end = host_boot();
        if (end != END_POWERCUT) { bad++; fprintf(stderr, "cut %u: no cut (%d)\n", k, end); continue; }
        B->cut_at_op = 0;
        B->rx_len = B->rx_pos = 0;
        int valid = app_valid_now();
        /* Nothing partial may ever be launchable. The metadata is written
           last, so the only launchable state after a cut is the complete new
           image (a cut during the metadata program itself can leave the whole
           record written). The old app can never come back: BEGIN erases its
           metadata first. */
        int complete = le32(B->flash + CHGAME_META_ADDR + 8) == len && !memcmp(B->flash + CHGAME_APP_START, img, len);
        if (valid && !complete) { bad++; fprintf(stderr, "cut at op %u/%u left a launchable partial image\n", k, total); }
        if (!valid && k == total - 1) { /* fine either way */ }
        end = host_run(3);
        if (end == END_JUMP && !complete) { bad++; fprintf(stderr, "cut %u: jumped into a partial image\n", k); }
        if (B->boot_region_writes) { bad++; fprintf(stderr, "cut %u: boot region written\n", k); }
    }
    CHECK(bad == 0, "power cut at each of %u flash operations of an upload", total);
}

#if CHGAME_ALLOW_SELFUPDATE
static void t_selfupdate(void)
{
    uint8_t cmd, pl[64], key[4], wb[8]; uint16_t n;
    uint32_t len = 9000;
    make_image(img, len, 8);
    put32(img + CHGAME_BOOT_SIG_OFFSET, CHGAME_BOOT_SIG);     /* a bootloader image */
    bootreq_set(CHGAME_BOOTREQ_USB);
    put32(key, CHGAME_DEV_KEY);
    frame_push(CMD_DEV_UNLOCK, key, 4);                       /* unlock first (host fix) */
    push_upload(img, len, 0);
    put32(wb, len); put32(wb + 4, host_crc32(img, len));
    frame_push(CMD_DEV_WRITE_BOOT, wb, 8);
    int end = host_boot();
    CHECK(end == END_RESET && B->selfupdate_calls == 1, "self-update ran (end %d)", end);
    int ok = 1;
    while (frame_pop(&cmd, pl, &n)) if (pl[0] != ST_OK) { ok = 0; fprintf(stderr, "cmd %02x -> %u\n", cmd, pl[0]); }
    CHECK(ok, "staging a bootloader image is accepted (END ok), and so is DEV_WRITE_BOOT");
    CHECK(!memcmp(B->flash, img, len), "boot region holds the new image");
    CHECK(!app_valid_now(), "the staged copy is not an app (metadata erased, signature)");
}
#endif

#if !CHBOOT_MENU
static void t_boot_nomenu(void)
{
    make_image(img, 4096, 7);
    host_install_app(img, 4096);
    CHECK(host_boot() == END_JUMP, "valid app, no request: runs");
    bootreq_set(CHGAME_BOOTREQ_USB);
    B->limit_us = 500000;
    CHECK(host_boot() == END_HANG && B->usb_up, "USB request: USB mode");
    CHECK(host_boot() == END_JUMP, "request was consumed");
    upd_begin();
    CHECK(host_boot() == END_HANG && B->usb_up, "invalid app: USB mode");
}
#endif

int main(void)
{
    TEST(t_update_basic);
    TEST(t_update_errors);
    TEST(t_signature);
    TEST(t_proto_claim);
    TEST(t_usb_upload_run);
    TEST(t_usb_powercut_sweep);
#if CHGAME_ALLOW_SELFUPDATE
    TEST(t_selfupdate);
#endif
#if !CHBOOT_MENU
    TEST(t_boot_nomenu);
#endif
    return test_summary();
}
