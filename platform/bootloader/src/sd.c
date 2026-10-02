/*
 * sd.c - SPI-mode SD card driver for the bootloader. A C port of CHSd 1.0.0's
 * SdSpi.cpp (HypeRunner's clean-room driver, MIT: written from the SD
 * Physical Layer Simplified Specification, chapter 7, and the CH32X035
 * reference manual), with these changes for a bootloader:
 *
 *  - It owns SPI1 and the pins (hal_pins_init) instead of borrowing CHGfx's
 *    setup.
 *  - Recovery when the card does not answer CMD0. The card stays powered
 *    across an MCU reset, and a program (CHSDtoUSB uses multi-block
 *    transfers) can be reset in the middle of a CMD18 read stream or a CMD25
 *    write. In those states the card ignores CMD0. recover() ends either:
 *    CMD12 stops a read stream; 520 clocks finish a write block cut off part
 *    way (that block was lost with the reset anyway); the stop-transmission
 *    token ends a write between blocks; each followed by a busy wait. It is
 *    harmless to an idle card, and an empty slot costs it about 25 ms.
 *  - A real CRC7 on every command (see cmd()).
 *  - The timings of the driver that has run on this board (CHSDtoUSB):
 *    ACMD41 up to 2 s, a read token up to 1.5 s (a block never read since
 *    power-up can take a slow card most of a second), and CMD58's busy bit
 *    checked with CCS.
 */
#include "sd.h"
#include "hal.h"
#include "sys.h"

#define WAIT_BUSY_MS   500u
#define WAIT_TOKEN_MS  1500u
#define WAIT_INIT_MS   2000u

static uint8_t hc;                  /* block addressing (SDHC/SDXC) */

static uint8_t x(uint8_t b) { return hal_spi_xfer(b); }

/* Clocks 0xFF until a token arrives (tok: a byte other than 0xFF) or the busy
   period ends (!tok: 0xFF), or the time runs out. Returns the last byte. */
static uint8_t wait(int tok, uint32_t ms)
{
    uint32_t t0 = sys_ticks();
    uint8_t b;
    do b = x(0xFF); while ((b == 0xFF) == tok && sys_ticks() - t0 < ms * SYS_TICKS_PER_MS);
    return b;
}

/* 48-bit command frame: 01 + index, the argument MSB first, CRC7 and the end
   bit. The CRC is computed for every command, not just the CMD0/CMD8
   constants: CHSDtoUSB turns the card's CRC checking on (CMD59), the card
   stays powered across an MCU reset, and with CRC on a card refuses any
   command with a wrong one. One 0xFF first gives the card its N_RC gap.
   R1 comes within 8 bytes (one more for CMD12's stuff byte); bit 7 set means
   nothing answered. */
static uint8_t cmd(uint32_t c, uint32_t arg)
{
    uint8_t r, crc = 0;
    uint32_t k = 10;
    x(0xFF);
    for (int sh = 32; sh >= 0; sh -= 8) {
        uint8_t d = sh == 32 ? (uint8_t)(0x40 | c) : (uint8_t)(arg >> sh);
        x(d);
        for (uint32_t b = 8; b; b--, d <<= 1) {
            crc <<= 1;
            if ((d ^ crc) & 0x80) crc ^= 0x09;
        }
    }
    x((uint8_t)(crc << 1 | 1));
    do r = x(0xFF); while ((r & 0x80) && --k);
    return r;
}

static uint32_t rd32(void)
{
    uint32_t r = 0;
    for (uint32_t i = 4; i; i--) r = (r << 8) | x(0xFF);
    return r;
}

static void recover(void)
{
    uint32_t k;
    cmd(12, 0);                                 /* stops a CMD18 stream */
    wait(0, WAIT_BUSY_MS);
    for (k = 520; k; k--) x(0xFF);              /* completes a CMD25 block cut short */
    wait(0, WAIT_BUSY_MS);
    x(0xFD);                                    /* stop token: ends a CMD25 between blocks */
    wait(0, WAIT_BUSY_MS);
}

static int ident(void)
{
    uint8_t r;
    uint32_t k, t0, v2 = 0;

    for (k = 0; cmd(0, 0) != 0x01; k++) {       /* GO_IDLE_STATE: enter SPI mode */
        if (k == 16) return -1;
        if (k == 6) recover();
    }
    /* CRC_ON_OFF off: the card's default, which the games' own driver (CHSd,
       fixed CRC bytes) relies on. CHSDtoUSB turns it on, and it may survive
       CMD0. */
    cmd(59, 0);
    r = cmd(8, 0x1AA);                          /* SEND_IF_COND 2.7-3.6 V, check pattern 0xAA */
    if (!(r & 0x04)) {                          /* not an illegal command: v2.00 or later */
        if (r != 0x01 || (rd32() & 0xFFF) != 0x1AA) return -1;
        v2 = 1;
    }
    t0 = sys_ticks();
    do {                                        /* ACMD41 (HCS on v2) until idle clears */
        r = cmd(55, 0);
        if (r <= 1) r = cmd(41, v2 << 30);
        if (r > 1 || sys_ticks() - t0 > WAIT_INIT_MS * SYS_TICKS_PER_MS) return -1;
    } while (r);
    hc = 0;
    if (v2) {                                   /* READ_OCR: powered up, CCS = block addressing */
        if (cmd(58, 0)) return -1;
        k = rd32();
        if (!(k >> 31)) return -1;
        hc = (uint8_t)((k >> 30) & 1);
    }
    return hc || cmd(16, 512) == 0 ? 0 : -1;    /* SDSC: 512-byte blocks */
}

int sd_init(void)
{
    int rc;
    hal_spi_speed(SPI_BR_187K5);                /* identification at <= 400 kHz */
    hal_sd_select(0);
    for (uint32_t k = 10; k; k--) x(0xFF);      /* >= 74 clocks with CS high */
    hal_sd_select(1);
    rc = ident();
    hal_sd_select(0);
    x(0xFF);                                    /* the card releases DO on a clock with CS high */
    hal_spi_speed(SD_SPI_BR);
    return rc;
}

int sd_read(uint32_t lba, uint8_t *dst)
{
    int ok;
    hal_sd_select(1);
    ok = wait(0, WAIT_BUSY_MS) == 0xFF && cmd(17, hc ? lba : lba << 9) == 0 && wait(1, WAIT_TOKEN_MS) == 0xFE;
    if (ok) {
        for (uint32_t i = 0; i < 512; i++) dst[i] = x(0xFF);
        x(0xFF);                                /* the CRC16, unused */
        x(0xFF);
    }
    hal_sd_select(0);
    x(0xFF);
    return ok ? 0 : -1;
}
