#include "bootreq.h"

#ifndef CHBOOT_HOST
volatile chgame_bootreq_t chgame_bootreq __attribute__((section(".boot_magic"), used));
#endif

uint32_t bootreq_take(void)
{
    uint32_t m = chgame_bootreq.magic;
    uint32_t ok = chgame_bootreq.inverse == ~m;

    chgame_bootreq.magic   = 0;
    chgame_bootreq.inverse = 0;
    return ok ? m : 0;
}

void bootreq_set(uint32_t reason)
{
    chgame_bootreq.magic   = reason;
    chgame_bootreq.inverse = ~reason;
}
