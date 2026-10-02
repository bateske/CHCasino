#ifndef CHGAME_UPDATE_H
#define CHGAME_UPDATE_H
#include <stdint.h>

/* The one flash-update transaction, shared by the USB protocol (proto.c) and
 * the SD installer (install.c), so both get the same safety order:
 *
 *   upd_begin()      erase the metadata page: from here the installed program
 *                    is invalid, so a power cut leaves the bootloader in charge
 *   upd_page() ...   write the image page by page (each one read back)
 *   upd_end()        CRC the image AS STORED IN FLASH against the expected
 *                    CRC, and only then write the metadata page
 *
 * Results are protocol status codes (ST_OK, ST_ERR_RANGE, ST_ERR_CRC,
 * ST_ERR_FLASH from proto.h), so proto.c passes them straight through. */

uint8_t upd_begin(void);

/* Writes one 256-byte page at addr (inside the application region, below the
 * metadata page). A page whose flash already holds exactly these bytes is
 * left alone: reinstalling a game, or a newer build of it, then erases only
 * the pages that changed. */
uint8_t upd_page(uint32_t addr, const uint8_t *buf);

/* len is the image length (a multiple of 4), crc its CRC-32/ISO-HDLC. */
uint8_t upd_end(uint32_t len, uint32_t crc);

#endif
