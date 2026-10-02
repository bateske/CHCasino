#include "appmeta.h"
#include "crc32.h"

int appmeta_check(void)
{
    const chgame_meta_t *m = appmeta();

    if (m->magic != CHGAME_META_MAGIC || m->meta_version != CHGAME_META_VERSION)
        return APP_INVALID_NO_META;

    /* Length must be non-zero, within the region, and a whole number of words.
     * Checked before it is ever used as a CRC span. */
    if (m->length == 0 || m->length > CHGAME_APP_MAX_SIZE || (m->length & 3u) != 0)
        return APP_INVALID_LENGTH;

    if (crc32_buf(FLASH_AT(CHGAME_APP_START), m->length) != m->crc32)
        return APP_INVALID_CRC;

    /* After the CRC, so that only a complete, intact copy of a staged
       bootloader is told apart from an application. */
    if (FLASH_W(CHGAME_APP_START + CHGAME_BOOT_SIG_OFFSET) == CHGAME_BOOT_SIG)
        return APP_INVALID_BOOTIMAGE;

    return APP_VALID;
}
