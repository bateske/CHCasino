/* SysTick_Handler is in the vector table but can never run: the bootloader
 * uses SysTick as a free-running counter with its interrupt disabled.
 * ch32x035_misc.c supplies weak stubs for every other IRQ handler except NMI
 * and HardFault, which live in fault.c. */
void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void) { }
