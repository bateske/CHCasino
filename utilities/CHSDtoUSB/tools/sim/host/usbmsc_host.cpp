// The simulator's USB side: the sketch's UsbMsc API, driven by main.cpp's
// pretend PC instead of the USBFS peripheral.
#include <Arduino.h>
#include "src/usb/UsbMsc.h"
#include "simpc.h"

namespace usbmsc {

volatile uint32_t blocksRead = 0, blocksWritten = 0;
BlockDevice simDev;
State simState = OFF;
bool simBusy = false, simEjected = false;
static bool ro = false;
alignas(4) static uint8_t sector[512];

void begin(const BlockDevice &d) { simDev = d; simState = WAITING; }
void poll() {}
State state() {
    if (simState == CONFIGURED && simEjected) return EJECTED;
    return simState;
}
bool busy() { return simBusy; }
void setReadOnly(bool r) { ro = r; }
bool readOnly() { return ro; }
void mediaChanged() { simEjected = false; }
void detach() { simState = OFF; }
int cdcRead() { return -1; }
bool cdcWrite(const char *, uint8_t) { return true; }
uint8_t *buffer() { return sector; }

}  // namespace usbmsc
