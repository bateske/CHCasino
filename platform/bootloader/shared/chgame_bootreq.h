/*
 * Reasons a program can leave in the retained boot-request block
 * (chgame_bootreq_t at CHGAME_MAGIC_ADDR, see src/chgame_map.h) before it
 * resets into the bootloader. Each is stored as {magic, ~magic}, so
 * uninitialised SRAM cannot forge one, and the bootloader clears the block as
 * soon as it has read it.
 *
 *   CHGAME_BOOTREQ_USB   stay in the bootloader's USB upload mode. Written by
 *                        the Arduino core's 1200-baud touch
 *                        (chgame_enter_bootloader(), core 0.2.4) and by the
 *                        games' debug 'B' command. Unchanged since 0.1.0.
 *   CHGAME_BOOTREQ_RUN   start the installed program without showing the
 *                        menu. Written by the bootloader itself (the menu,
 *                        the USB RUN command) just before it resets, so the
 *                        program always starts from a real reset.
 *
 * Anything else, including no request at all, shows the game menu. So a
 * plain NVIC_SystemReset() is how a game returns to the menu.
 */
#ifndef CHGAME_BOOTREQ_REASONS_H
#define CHGAME_BOOTREQ_REASONS_H

#define CHGAME_BOOTREQ_USB    0x43484742u   /* "CHGB" */
#define CHGAME_BOOTREQ_RUN    0x43484752u   /* "CHGR" */

#endif
