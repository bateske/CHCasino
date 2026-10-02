#ifndef CHGAME_BOOTREQ_H
#define CHGAME_BOOTREQ_H
#include "chgame_map.h"
#include "chgame_bootreq.h"

/* The retained boot-request block. Lives in .boot_magic, which both the
 * bootloader and the application linker scripts pin to CHGAME_MAGIC_ADDR and
 * mark NOLOAD, so startup neither loads nor zeroes it and it survives
 * NVIC_SystemReset() (verified on hardware: SRAM survives a warm reset but not
 * a power cycle). Reasons are in shared/chgame_bootreq.h. */
#ifdef CHBOOT_HOST
volatile chgame_bootreq_t *host_retained(void);   /* the harness's shared copy */
#define chgame_bootreq (*host_retained())
#else
extern volatile chgame_bootreq_t chgame_bootreq;
#endif

/* Returns the pending reason (0 if none, or if the magic/inverse pair is not
 * intact) and clears the block, so a request is acted on exactly once. */
uint32_t bootreq_take(void);
void     bootreq_set(uint32_t reason);

#endif
