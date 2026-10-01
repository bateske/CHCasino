#ifndef CHBOOT_CHG_H
#define CHBOOT_CHG_H
#include <stdint.h>
#include "chg_format.h"

/* Results of checking a package header, in the order they are checked. */
#define CHG_OK          0
#define CHG_E_MAGIC     1   /* not a CHG package */
#define CHG_E_HCRC      2   /* header damaged */
#define CHG_E_FORMAT    3   /* a format version or header size this bootloader does not know */
#define CHG_E_TARGET    4   /* built for another board or memory layout */
#define CHG_E_SIZE      5   /* payload empty, too big, not word-sized, or longer than the file */

/* h: the first 512 bytes of the file (4-byte aligned). On CHG_OK, *payload
 * and *crc are the payload length and CRC. */
int chg_check(const uint8_t *h, uint32_t file_size, uint32_t *payload, uint32_t *crc);

#endif
