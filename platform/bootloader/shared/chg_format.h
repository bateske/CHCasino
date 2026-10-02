/*
 * CHGame package (.CHG), format version 1 - the file the SD game menu
 * installs. docs/chg-format.md is the normative description; tools/chgpack.py
 * (repository root) writes and checks these files and mirrors every constant
 * here.
 *
 *   0x000  512-byte header (all fields little-endian)
 *   0x200  payload: the program image exactly as chgame-upload would write it
 *          at CHGAME_APP_START (the sketch .bin padded with 0xFF to a
 *          multiple of 4)
 *   ...    optional extras the bootloader ignores (title image)
 *
 * The package never names a flash address: the destination is always
 * CHGAME_APP_START, and layout_id says which memory map it was built for.
 */
#ifndef CHG_FORMAT_H
#define CHG_FORMAT_H

#define CHG_MAGIC            0x31474843u   /* "CHG1" */
#define CHG_FORMAT_VERSION   1u
#define CHG_HEADER_BYTES     512u
#define CHG_TARGET_ID        0x35335843u   /* "CX35": CHGame, CH32X035G8U6 */
#define CHG_LAYOUT_ID        0x003000F7u   /* app at 0x3000, metadata page at 0xF700 */

/* Field offsets. */
#define CHG_OFF_MAGIC        0x000u   /* u32 */
#define CHG_OFF_VERSION      0x004u   /* u16 format_version */
#define CHG_OFF_HDRBYTES     0x006u   /* u16 header_bytes */
#define CHG_OFF_TARGET       0x008u   /* u32 */
#define CHG_OFF_LAYOUT       0x00Cu   /* u32 */
#define CHG_OFF_PAYLOAD      0x010u   /* u32 payload_bytes: 4..CHGAME_APP_MAX_SIZE, a multiple of 4 */
#define CHG_OFF_PCRC         0x014u   /* u32 payload_crc32: CRC-32/ISO-HDLC of the payload */
#define CHG_OFF_APPVER       0x018u   /* u32 app_version (free for the author) */
#define CHG_OFF_FLAGS        0x01Cu   /* u32, 0 */
#define CHG_OFF_TITLE        0x020u   /* char[32], ASCII, NUL-padded: shown by the menu */
#define CHG_OFF_AUTHOR       0x040u   /* char[16] */
#define CHG_OFF_VERSTR       0x050u   /* char[8], e.g. "1.2" */
#define CHG_OFF_IMAGE        0x060u   /* u32 offset, u32 bytes, u32 crc32: optional 128x128 title image, 0 = none */
#define CHG_OFF_HCRC         0x1FCu   /* u32 header_crc32: CRC-32/ISO-HDLC of bytes 0x000..0x1FB */

#define CHG_TITLE_LEN        32u

#endif
