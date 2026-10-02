#ifndef CHGAME_BOOT_H
#define CHGAME_BOOT_H
#include <stdint.h>

/* CHBOOT_MENU 0 builds the bootloader without the SD menu: the old boot
 * decision (USB request or invalid app -> USB mode, else run), on the new
 * shared update path. It is the first hardware step (HW2a), so changes to the
 * proven USB path meet the board before the SD and panel code does. */
#ifndef CHBOOT_MENU
#define CHBOOT_MENU 1
#endif

void boot_main(void) __attribute__((noreturn));

/* Detach USB, leave `reason` (shared/chgame_bootreq.h, or 0 for the menu) in
 * the retained block and reset. The only way the bootloader starts a program
 * or returns to its menu: every start is from a real reset. */
void boot_reset(uint32_t reason) __attribute__((noreturn));

/* USB upload mode: no timeout; B (a fresh press) returns to the menu. */
void usb_mode(void) __attribute__((noreturn));

#endif
