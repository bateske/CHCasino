/*
 * Host test harness for the bootloader's portable code (src/ minus the
 * target-only files: startup, flash.c, jump.c, fault.c, sys.c, usb.c and the
 * vendor USB stack and SPL).
 *
 * A "board" lives in shared memory: the flash array, the retained boot block,
 * the SD card model, the panel model, the virtual clock and the button
 * script. host_boot() forks, and the child runs boot_main() from a fresh copy
 * of every global - exactly what a real reset gives the bootloader (.data
 * reloaded, .bss zeroed) - until it resets, jumps to the application, hangs,
 * or the test cuts the power. Only what survives a reset on the real board
 * survives here.
 */
#ifndef CHBOOT_HOST_H
#define CHBOOT_HOST_H
#include <stdint.h>
#include <stddef.h>
#include "chgame_map.h"
#include "sd_model.h"
#include "lcd_model.h"

/* How a simulated boot ended. */
enum {
    END_RESET = 1,     /* hal_reset(): retained block survives */
    END_JUMP,          /* jump_to_app() with a valid image */
    END_POWERCUT,      /* the test's cut point was reached */
    END_HANG,          /* virtual time limit reached (menu waiting, USB mode) */
    END_CRASH,         /* the child died (assert, signal) */
};

#define HOST_MAX_KEYS 64
#define HOST_MAX_LOG  4096

typedef struct {
    uint64_t at_us;
    uint32_t mask;      /* buttons held from at_us on */
} host_key_t;

typedef struct {
    uint8_t  flash[CHGAME_FLASH_SIZE];
    uint32_t retained[4];           /* the 16-byte block at 0x20000000 */
    uint64_t now_us;                /* virtual time since power-on */
    uint64_t limit_us;              /* END_HANG after this much time in one boot */

    /* flash operation accounting */
    uint32_t flash_ops;             /* erase or program operations so far */
    uint32_t cut_at_op;             /* cut power DURING this op (1-based); 0 = never */
    uint32_t erase_count[CHGAME_FLASH_SIZE / CHGAME_PAGE_SIZE];
    uint32_t boot_region_writes;    /* erase/program below APP_START (must stay 0) */
    uint32_t selfupdate_calls;
    uint8_t  card_dies_on_flash;     /* the card stops answering once flash is first written */
    uint8_t  soft_reset;             /* the coming boot follows a software reset (else power-on) */

    /* buttons: the mask from the last event whose time has passed */
    host_key_t keys[HOST_MAX_KEYS];
    int        nkeys;

    /* pins */
    uint8_t sd_cs, lcd_cs, lcd_dc, lcd_rst, led;
    uint32_t spi_br;
    uint32_t bus_conflicts;         /* both chip selects low during a transfer */
    uint8_t  spi_on;                /* hal_spi_speed() called since reset */
    uint32_t spi_off_xfers;         /* transfers while SPI1 is off: a hang on the chip */
    uint32_t pins_inited;

    /* USB (proto) */
    uint8_t  usb_up;
    uint8_t  rx[131072]; uint32_t rx_len, rx_pos;  /* host -> device */
    uint8_t  tx[32768];  uint32_t tx_len;          /* device -> host */

    /* last boot */
    int      end;
    uint32_t jumps;

    sd_model_t  sd;
    lcd_model_t lcd;

    char log[HOST_MAX_LOG];
    uint32_t log_len;
} host_board_t;

extern host_board_t *B;

void host_init(void);                       /* fresh board: flash erased, no card */
void host_power_cycle(void);                /* SRAM lost, card/panel power-cycled */
int  host_boot(void);                       /* one boot; returns END_* */
int  host_run(int max_boots);               /* boots until JUMP/HANG/POWERCUT/CRASH */
void host_keys(uint64_t at_ms, uint32_t mask);   /* relative to the current time */
void host_keys_clear(void);
void host_log(const char *fmt, ...);
void host_advance_us(uint64_t us);

/* Program an application image + valid metadata straight into the flash
 * array (what a completed USB upload leaves). */
void host_install_app(const uint8_t *img, uint32_t len);
uint32_t host_crc32(const void *p, size_t n);

#endif
