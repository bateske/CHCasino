// Shared between the simulator's shims.
#pragma once
#include <stdint.h>
#include "src/usb/UsbMsc.h"

namespace usbmsc {
extern BlockDevice simDev;
extern State simState;
extern bool simBusy, simEjected;
}

// The pretend card (sd_host.cpp).
void card_load(const char *path, uint32_t blocks);
void card_present(bool in);
bool card_fault(uint32_t lba);          // this block's CRC fails once (a retry fixes it)
void card_badBlock(uint32_t lba);       // ... on its next read
