/*
 * The few hardware operations the portable parts of the bootloader need: the
 * pins, SPI1, the buttons and the LED. On the board these are static inline
 * register accesses, so the layer costs nothing; the host test harness
 * (test/native) provides the same names as functions over its models of the
 * card, the panel and the flash.
 *
 * Pins (platform/board/docs/hardware-pinmap.md, variant_CHGame.h):
 *   SPI1   SCK PA5, MISO PA6, MOSI PA7 - shared by the panel and the card
 *   panel  CS PA4, DC PB0, RST PB12
 *   card   CS PB11
 *   LED    PB9 (active high), buzzer PB10 (held low)
 *   keys   A PB1, LEFT PB3, UP PB4, B PB6, SELECT PB7, START PB8,
 *          DOWN PC14, RIGHT PC15 - all to GND, internal pull-ups
 *
 * None of MISO, the two chip selects or LCD_RST has a pull-up on the board,
 * so hal_pins_init() drives every one of them before anything else runs.
 */
#ifndef CHBOOT_HAL_H
#define CHBOOT_HAL_H
#include <stdint.h>

/* CHBOOT_APP 1: the dry-run build that runs as a program (build.sh app). */
#ifndef CHBOOT_APP
#define CHBOOT_APP 0
#endif

/* Button bits are the port bits themselves: GPIOB 1/3/4/6/7/8 and GPIOC 14/15
 * do not overlap, so one 16-bit mask holds them all. */
#define BTN_A       (1u << 1)
#define BTN_LEFT    (1u << 3)
#define BTN_UP      (1u << 4)
#define BTN_B       (1u << 6)
#define BTN_SELECT  (1u << 7)
#define BTN_START   (1u << 8)
#define BTN_DOWN    (1u << 14)
#define BTN_RIGHT   (1u << 15)
#define BTN_PORTB   (BTN_A | BTN_LEFT | BTN_UP | BTN_B | BTN_SELECT | BTN_START)
#define BTN_PORTC   (BTN_DOWN | BTN_RIGHT)
#define BTN_ALL     (BTN_PORTB | BTN_PORTC)

/* SPI1 baud-rate field (CTLR1.BR): SCK = 48 MHz / 2^(BR+1). */
#define SPI_BR_187K5  7u
#define SPI_BR_6M     2u
#define SPI_BR_12M    1u
#define SPI_BR_24M    0u

#ifdef CHBOOT_HOST

void     hal_pins_init(void);
void     hal_spi_speed(uint32_t br);
uint8_t  hal_spi_xfer(uint8_t b);
void     hal_sd_select(int on);
void     hal_lcd_select(int on);
void     hal_lcd_dc(int data);
void     hal_lcd_rst(int high);
uint32_t hal_buttons(void);
void     hal_led(int on);
uint32_t hal_uid(uint32_t word);
int      hal_soft_reset(void);
const uint8_t *hal_flash(uint32_t addr);
void     hal_reset(void) __attribute__((noreturn));
#define FLASH_AT(a)   hal_flash(a)

#else

#include "ch32x035.h"

/* Composed pin configuration, one nibble per pin (CNF:MODE).
 *   0x3 push-pull output, 0xB alternate-function push-pull,
 *   0x8 input with pull-up/down (OUTDR selects up), 0x4 floating input.
 * GPIOB/GPIOC CFGHR are WRITE-ONLY on this part (reading returns garbage), so
 * they are only ever written whole, from these constants.
 * PB5 shares pin 16 with BTN_A: it stays a floating input, never an output. */
#define HAL_GPIOA_CFGLR  0xB8B34444u   /* PA7 MOSI, PA6 MISO pu, PA5 SCK, PA4 LCD_CS */
#define HAL_GPIOB_CFGLR  0x88488483u   /* PB7 SEL, PB6 B, PB5 -, PB4 UP, PB3 LEFT, PB2 -, PB1 A, PB0 DC */
#define HAL_GPIOB_CFGHR  0x44433338u   /* PB12 LCD_RST, PB11 SD_CS, PB10 buzzer, PB9 LED, PB8 START */
#define HAL_GPIOC_CFGHR  0x88444444u   /* PC15 RIGHT, PC14 DOWN */

static inline void hal_pins_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB
                    | RCC_APB2Periph_GPIOC | RCC_APB2Periph_SPI1;
    /* Levels first, so no pin glitches when it becomes an output: both chip
     * selects, LCD_RST and DC high, MISO and the key pull-ups up, LED and
     * buzzer low. */
    GPIOA->BSHR = (1u << 4) | (1u << 6);
    GPIOB->BSHR = (1u << 0) | (1u << 11) | (1u << 12) | BTN_PORTB;
    GPIOB->BCR  = (1u << 9) | (1u << 10);
    GPIOC->BSHR = BTN_PORTC;
    GPIOA->CFGLR = HAL_GPIOA_CFGLR;
    GPIOB->CFGLR = HAL_GPIOB_CFGLR;
    GPIOB->CFGHR = HAL_GPIOB_CFGHR;
    GPIOC->CFGHR = HAL_GPIOC_CFGHR;
}

/* Mode 0, 8-bit, MSB first, master with software NSS. BR changes only with
 * SPE clear. */
static inline void hal_spi_speed(uint32_t br)
{
    while (SPI1->STATR & SPI_STATR_BSY) { }
    SPI1->CTLR1 = (uint16_t)(SPI_CTLR1_MSTR | SPI_CTLR1_SSI | SPI_CTLR1_SSM | (br << 3));
    SPI1->CTLR1 = (uint16_t)(SPI_CTLR1_MSTR | SPI_CTLR1_SSI | SPI_CTLR1_SSM | (br << 3) | SPI_CTLR1_SPE);
}

/* Every byte out is a byte in: always reading DATAR keeps RXNE and OVR from
 * leaving stale bytes for the card reads. */
static inline uint8_t hal_spi_xfer(uint8_t b)
{
    SPI1->DATAR = b;
    while (!(SPI1->STATR & SPI_STATR_RXNE)) { }
    return (uint8_t)SPI1->DATAR;
}

static inline void hal_sd_select(int on)  { if (on) GPIOB->BCR = 1u << 11; else GPIOB->BSHR = 1u << 11; }
static inline void hal_lcd_select(int on) { if (on) GPIOA->BCR = 1u << 4;  else GPIOA->BSHR = 1u << 4; }
static inline void hal_lcd_dc(int data)   { if (data) GPIOB->BSHR = 1u << 0; else GPIOB->BCR = 1u << 0; }
static inline void hal_lcd_rst(int high)  { if (high) GPIOB->BSHR = 1u << 12; else GPIOB->BCR = 1u << 12; }
static inline void hal_led(int on)        { if (on) GPIOB->BSHR = 1u << 9; else GPIOB->BCR = 1u << 9; }

/* Pressed keys read low. */
static inline uint32_t hal_buttons(void)
{
    return (~GPIOB->INDR & BTN_PORTB) | (~GPIOC->INDR & BTN_PORTC);
}

#ifndef CH32X035_ESIG_UNIID1
#define CH32X035_ESIG_UNIID1  0x1FFFF7E8u   /* 96-bit unique ID */
#endif
static inline uint32_t hal_uid(uint32_t word)
{
    return ((const volatile uint32_t *)CH32X035_ESIG_UNIID1)[word];
}

/* 1 if this boot follows a software reset (NVIC_SystemReset: a program going
   back to the menu, an upload request), 0 after power-on. Clears the reset
   flags, so the next boot sees only its own cause. */
static inline int hal_soft_reset(void)
{
    uint32_t f = RCC->RSTSCKR;
    RCC->RSTSCKR |= RCC_RMVF;
    return (f & RCC_SFTRSTF) != 0;
}

/* A full system reset (PFIC CFGR SYSRESET); SRAM survives it. */
static inline __attribute__((noreturn)) void hal_reset(void)
{
    NVIC->CFGR = NVIC_KEY3 | (1u << 7);
    for (;;) { }
}

#define FLASH_AT(a)   ((const uint8_t *)(uintptr_t)(a))

#endif /* CHBOOT_HOST */

/* An aligned word of flash. */
#define FLASH_W(a)    (*(const uint32_t *)(const void *)FLASH_AT(a))

#endif
