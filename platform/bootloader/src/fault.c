/* Exception handlers, and the bench fault reporter.
 *
 * With no SWD probe, a fault that spins silently tells us nothing. These read
 * mcause and mepc and blink them out, which turns "it hangs" into an actual
 * diagnosis. Everything here is busy-loop timed and does its own GPIO clock
 * enable, so the report works no matter how early the fault happened.
 *
 * Report frame, repeating forever:
 *
 *   [ IMAGE_ID long pulses ]   1 = fault taken by the BOOTLOADER's handler
 *                              2 = fault taken by the APPLICATION's handler
 *                              (i.e. whether mtvec had been re-pointed yet)
 *   [ (mcause & 0xF) + 1   ]   1 instr-addr-misaligned  2 instr-access-fault
 *                              3 ILLEGAL INSTRUCTION    4 breakpoint
 *                              5 load-addr-misaligned   6 load-access-fault
 *                              7 store-addr-misaligned  8 store-access-fault
 *                             12 ecall-from-M
 *   [ mepc, low 16 bits    ]   four nibbles, MSB first, each as four
 *                              marks (long = 1, short = 0)
 */
#include "ch32x035.h"
#include "chgame_map.h"
#include "hal.h"
#include "spin.h"

#ifndef CHGAME_IMAGE_ID
#define CHGAME_IMAGE_ID 1
#endif

void NMI_Handler(void)       __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

static void fault_led_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    /* PB9 push-pull output. CFGHR is write-only on this part, so it is written
       whole from the board's composed value (a read-modify-write would turn
       SD_CS, LCD_RST and the buzzer pin into floating inputs). */
    GPIOB->CFGHR = HAL_GPIOB_CFGHR;
}

static void pulses(uint32_t n, uint32_t on_ms, uint32_t off_ms)
{
    while (n--) {
        GPIOB->BSHR = GPIO_Pin_9; spin_ms(on_ms);
        GPIOB->BCR  = GPIO_Pin_9; spin_ms(off_ms);
    }
}

/* Blink one hex nibble as four binary marks, most-significant bit first.
 * long mark = 1, short mark = 0. Binary is far easier to read off an LED
 * reliably than counting up to sixteen pulses. */
static void nibble(uint32_t v)
{
    for (int b = 3; b >= 0; b--) {
        GPIOB->BSHR = GPIO_Pin_9;
        spin_ms((v >> b) & 1u ? 320u : 70u);
        GPIOB->BCR  = GPIO_Pin_9;
        spin_ms(170);
    }
}

static void fault_report(void)
{
    uint32_t mcause, mepc;

    __asm volatile ("csrr %0, mcause" : "=r" (mcause));
    __asm volatile ("csrr %0, mepc"   : "=r" (mepc));

    fault_led_init();
    for (;;) {
        /* Frame start: IMAGE_ID very long pulses. */
        pulses(CHGAME_IMAGE_ID, 500, 250);
        spin_ms(300);

        /* Fault type: (mcause & 0xF) + 1 short blips. */
        pulses((mcause & 0xFu) + 1u, 70, 160);
        spin_ms(500);

        /* mepc, low 16 bits, as four nibbles MSB first. */
        for (int n = 3; n >= 0; n--) {
            nibble((mepc >> (n * 4)) & 0xFu);
            spin_ms(500);
        }

        spin_ms(1200);
    }
}

void NMI_Handler(void)       { fault_report(); }
void HardFault_Handler(void) { fault_report(); }
