/*
 * Boot decision (docs/sd-menu.md, platform/bootloader/README.md):
 *
 *   reset -> read and clear the boot request
 *    |- RUN and the installed program is valid  -> jump to it (nothing touched yet)
 *    |- USB request, or B held at power-on      -> USB upload mode
 *    '- otherwise                               -> the SD game menu (menu.c)
 *
 * Neither USB mode nor the menu has a timeout. The program is only ever
 * entered through a RUN reset, so it always starts from real reset state.
 */
#include "boot.h"
#include "bootreq.h"
#include "appmeta.h"
#include "flash.h"
#include "jump.h"
#include "proto.h"
#include "sys.h"
#include "hal.h"
#if CHBOOT_MENU
#include "menu.h"
#endif

void boot_reset(uint32_t reason)
{
    proto_shutdown();
    bootreq_set(reason);
    hal_reset();
}

#if CHBOOT_MENU
/* B held while switching on skips the card and the panel altogether: the way
 * out if a card ever upsets the menu. The pull-ups need a moment to charge the
 * lines, and the key must read pressed on every sample over ~8 ms. */
static int b_held(void)
{
    uint32_t n;

    sys_delay_ms(1);
    for (n = 0; n < 8; n++) {
        if (!(hal_buttons() & BTN_B))
            return 0;
        sys_delay_ms(1);
    }
    return 1;
}
#endif

void usb_mode(void)
{
#if CHBOOT_MENU
    uint32_t prev = hal_buttons(), now;
#endif

    proto_init();
    for (;;) {
        proto_task();
        hal_led((sys_ticks() / (SYS_TICKS_PER_MS * 250u)) & 1u);   /* 2 Hz */
#if CHBOOT_MENU
        /* Only a fresh press counts: a B still held from power-on (or from
           CHSDtoUSB's hold-B) must not bounce straight back to the menu. */
        now = hal_buttons();
        if (now & ~prev & BTN_B)
            boot_reset(0);
        prev = now;
#endif
    }
}

void boot_main(void)
{
    uint32_t req = bootreq_take();
    int app = appmeta_check();

#if CHBOOT_MENU
    if (req == CHGAME_BOOTREQ_RUN && app == APP_VALID)
        jump_to_app();
#else
    if (req != CHGAME_BOOTREQ_USB && app == APP_VALID)
        jump_to_app();
#endif

    ramfunc_init();   /* flash routines must be resident in SRAM before first use */
    sys_init();
    hal_pins_init();

#if CHBOOT_MENU
    if (req == CHGAME_BOOTREQ_USB)
        menu_usb_notice();
    else if (!b_held())
        menu_main(app);   /* returns only to hand over to USB mode */
#endif
    usb_mode();
}
