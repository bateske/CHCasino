#include "jump.h"
#include "chgame_map.h"
#include "ch32x035.h"

/*
 * Called only at the very top of the boot (the RUN request path), before the
 * bootloader has touched any peripheral: the program arrives straight from a
 * real reset, so there is nothing to put back. The application's own startup
 * (handle_reset) re-establishes gp, sp and mtvec.
 */
void jump_to_app(void)
{
    __disable_irq();

    /* Mask and clear every interrupt source, both NVIC words. Nothing should
     * be pending this early, but a latched source firing after the jump and
     * before the app installs its own mtvec would vector into the bootloader. */
    NVIC->IRER[0] = 0xFFFFFFFFu;
    NVIC->IRER[1] = 0xFFFFFFFFu;
    NVIC->IPRR[0] = 0xFFFFFFFFu;
    NVIC->IPRR[1] = 0xFFFFFFFFu;

    __asm volatile (
        "fence.i\n"
        "jr %0\n"
        :
        : "r" (CHGAME_APP_START)
    );

    __builtin_unreachable();
}
