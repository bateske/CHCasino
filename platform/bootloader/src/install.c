#include "install.h"
#include "hal.h"
#include "chg.h"
#include "fat.h"
#include "sd.h"
#include "crc32.h"
#include "update.h"
#include "proto.h"
#include "menu.h"
#include "chgame_map.h"

/* The next payload sector into buf; in pass 2 a failed read is retried after
   re-initialising the card (the chain position is kept: fat_next_lba() only
   advances on success). */
static int next_sector(fat_stream_t *s, uint8_t *buf, uint32_t tries)
{
    for (;;) {
        uint32_t lba = fat_next_lba(s, buf);
        if (lba && !sd_read(lba, buf)) return 0;
        if (!tries--) return -1;
        sd_init();
    }
}

int install(uint32_t clus, uint32_t size, uint8_t *buf)
{
    fat_stream_t s;
    uint32_t n, crc, c, i, secs, len;
    int rc;

    /* ---- pass 1: nothing in flash is touched ---- */
    fat_open(&s, clus, size);
    if (next_sector(&s, buf, 0)) return INST_E_READ;
    rc = chg_check(buf, size, &n, &crc);
    if (rc) return INST_E_PKG + rc;
    secs = (n + 511) >> 9;
    c = crc32_init();
    for (i = 0; i < secs; i++) {
        if (next_sector(&s, buf, 0)) return INST_E_READ;
        if (!i && *(const uint32_t *)(const void *)(buf + CHGAME_BOOT_SIG_OFFSET) == CHGAME_BOOT_SIG)
            return INST_E_BOOT;
        len = n - (i << 9);
        c = crc32_update(c, buf, len < 512 ? len : 512);
        menu_progress(i, secs * 2);
    }
    if (crc32_final(c) != crc) return INST_E_CRC;

#if CHBOOT_APP
    return INST_OK;                  /* the dry-run build never writes flash */
#else
    /* ---- pass 2 ---- */
    fat_open(&s, clus, size);
    if (next_sector(&s, buf, 2)) return INST_E_READ;           /* (the header again) */
    if (upd_begin() != ST_OK) return INST_E_LOST;
    for (i = 0; i < secs; i++) {
        uint32_t a = CHGAME_APP_START + (i << 9), j;
        if (next_sector(&s, buf, 2)) return INST_E_LOST;
        len = n - (i << 9);
        for (j = len; j < 512; j++) buf[j] = 0xFF;      /* pad the last page as USB does */
        if (upd_page(a, buf) != ST_OK) return INST_E_LOST;
        if (len > 256 && upd_page(a + 256, buf + 256) != ST_OK) return INST_E_LOST;
        menu_progress(secs + i, secs * 2);
    }
    return upd_end(n, crc) == ST_OK ? INST_OK : INST_E_LOST;
#endif
}
