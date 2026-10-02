/* USB bring-up and teardown for the protocol (target only). */
#include "usb.h"
#include "proto.h"
#include "spin.h"
#include "wch_usbcdc_internal.h"
#include "ch32x035.h"

static uint8_t usb_started;

void usb_start(void)
{
    if (usb_started)
        return;
    CDC_init();
    usb_started = 1;
}

void proto_shutdown(void)
{
    if (!usb_started)
        return;

    USBFSD->INT_EN   = 0;
    USBFSD->INT_FG   = 0xFF;
    USBFSD->BASE_CTRL = 0;          /* drops the D+ pull-up: host sees a detach */
    USBFSD->UDEV_CTRL = 0;

    /* Release the PHY and stop clocking the peripheral. */
    AFIO->CTLR &= ~(uint32_t)(AFIO_CTLR_USB_IOEN | AFIO_CTLR_UDP_PUE | AFIO_CTLR_UDM_PUE);
    RCC->AHBPCENR &= ~RCC_AHBPeriph_USBFS;

    /* Give the host time to notice the disconnect before the application
       potentially re-attaches. Busy-loop: SysTick is about to be torn down. */
    spin_ms(30);

    usb_started = 0;
}
