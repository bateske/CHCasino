#ifndef CHGAME_USB_H
#define CHGAME_USB_H
#include <stdint.h>

/* The USB CDC device under the protocol (target: vendor/usbcdc; host tests:
 * a byte queue). proto.c only reads and writes bytes through these. */
void    usb_start(void);
int16_t CDC_read_nb(void);          /* -1 if no data */
uint8_t CDC_write_nb(char c);       /* 0 if the endpoint is busy */
void    CDC_flush(void);

#endif
