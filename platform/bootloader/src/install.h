#ifndef CHBOOT_INSTALL_H
#define CHBOOT_INSTALL_H
#include <stdint.h>

#define INST_OK        0
#define INST_E_READ    1   /* card read error or broken cluster chain: nothing erased */
#define INST_E_CRC     2   /* payload damaged (CRC): nothing erased */
#define INST_E_BOOT    3   /* a bootloader image, not a program: nothing erased */
#define INST_E_LOST    4   /* failed after the erase began: no program is installed */
#define INST_E_PKG     8   /* + CHG_E_* : bad header, nothing erased */

/* Installs the package in the file whose first cluster is clus (size bytes).
 *   pass 1: header checks, then the whole payload read and CRC-checked -
 *           nothing in flash is touched
 *   pass 2: upd_begin (the installed program is invalid from here), the
 *           payload written page by page (unchanged pages skipped), the CRC
 *           of what is IN FLASH checked against the header, metadata last.
 * A read error in pass 2 re-initialises the card and retries twice.
 * Reports progress through menu_progress(). buf: 512 B, 4-byte aligned. */
int install(uint32_t clus, uint32_t size, uint8_t *buf);

#endif
