#ifndef CHGAME_SPIN_H
#define CHGAME_SPIN_H
#include <stdint.h>

/* Busy-loop delay with NO dependency on SysTick, RCC or any peripheral.
 *
 * This exists so that LED diagnostics stay trustworthy even when the thing
 * being diagnosed is the timebase itself. Accuracy is irrelevant here — these
 * only drive human-visible blink patterns — so an approximate cycles-per-
 * iteration constant is fine.
 */
#ifdef CHBOOT_HOST
void spin_ms(uint32_t ms);                        /* advances the host's virtual clock */
#else
static inline void spin_ms(uint32_t ms)
{
    volatile uint32_t n = ms * (F_CPU / 4000u);   /* ~4 cycles per iteration */
    while (n--) { }
}
#endif

#endif
