#ifndef CHBOOT_SD_H
#define CHBOOT_SD_H
#include <stdint.h>

/* SPI-mode SD card, polled single-block reads (CMD17). The card shares SPI1
 * with the panel; every call selects the card itself and leaves it
 * deselected, with the panel's chip select untouched (high). */

int sd_init(void);                          /* 0 = card ready, then SPI at the run clock */
int sd_read(uint32_t lba, uint8_t *dst);    /* 0 = 512 bytes read */

#ifndef SD_SPI_BR
#define SD_SPI_BR SPI_BR_12M                /* data clock; HW1 confirms the choice */
#endif

#endif
