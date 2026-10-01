#include "update.h"
#include "proto.h"
#include "flash.h"
#include "appmeta.h"
#include "crc32.h"
#include "hal.h"
#include "chgame_map.h"

uint8_t upd_begin(void)
{
    return flash_erase_page(CHGAME_META_ADDR, FLASH_REGION_APP) == FLASH_OK ? ST_OK : ST_ERR_FLASH;
}

uint8_t upd_page(uint32_t addr, const uint8_t *buf)
{
    const uint8_t *f = FLASH_AT(addr);
    uint32_t i;
    int rc;

    if (addr < CHGAME_APP_START || addr >= CHGAME_META_ADDR)
        return ST_ERR_RANGE;
    for (i = 0; i < CHGAME_PAGE_SIZE && f[i] == buf[i]; i++) { }
    if (i == CHGAME_PAGE_SIZE)
        return ST_OK;
    rc = flash_write_page(addr, buf, FLASH_REGION_APP);
    if (rc == FLASH_OK)        return ST_OK;
    if (rc == FLASH_ERR_RANGE) return ST_ERR_RANGE;
    return ST_ERR_FLASH;
}

uint8_t upd_end(uint32_t len, uint32_t crc)
{
    uint32_t page[CHGAME_PAGE_SIZE / 4u];
    chgame_meta_t *m = (chgame_meta_t *)page;
    uint32_t i, erased = 1;
    int rc;

    /* Verify from FLASH, not from anything buffered: the only claim worth
       making is about what is actually stored. */
    if (crc32_buf(FLASH_AT(CHGAME_APP_START), len) != crc)
        return ST_ERR_CRC;

    for (i = 0; i < CHGAME_PAGE_SIZE / 4u; i++) {
        page[i] = 0xFFFFFFFFu;
        erased &= FLASH_W(CHGAME_META_ADDR + i * 4u) == 0xFFFFFFFFu;
    }
    m->magic        = CHGAME_META_MAGIC;
    m->meta_version = CHGAME_META_VERSION;
    m->length       = len;
    m->crc32        = crc;
    m->app_version  = 0u;

    /* upd_begin() erased this page, so normally it is only programmed: the
       metadata page is the one erased on every update, and this halves its
       wear. Anything else in it (which no path should leave) gets a full
       erase first. */
    rc = erased ? flash_program_page(CHGAME_META_ADDR, (const uint8_t *)page, FLASH_REGION_APP)
                : flash_write_page(CHGAME_META_ADDR, (const uint8_t *)page, FLASH_REGION_APP);
    if (rc != FLASH_OK)
        return ST_ERR_FLASH;

    /* Re-read through the same path the boot decision uses, so success means
       exactly "the next boot will launch this" - or, for a bootloader image
       staged for DEV_WRITE_BOOT, "stored intact" (it is never launched). */
    rc = appmeta_check();
    return rc == APP_VALID || rc == APP_INVALID_BOOTIMAGE ? ST_OK : ST_ERR_CRC;
}
