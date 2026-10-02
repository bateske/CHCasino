// A USB mass-storage device, built entirely inside a sketch.
//
// The CHGame core brings USB up as a CDC serial port and always links its
// own USBFS interrupt handler, so a sketch cannot simply install another.
// Instead this module takes the peripheral over: it disables the core's
// interrupt, tells the core's Serial that it is no longer enumerated, drops
// off the bus and comes back as a composite device - the same CDC serial
// function (so the 1200-baud "reboot to bootloader" touch, and therefore
// arduino-cli upload, keep working) plus a Mass Storage (Bulk-Only
// Transport, SCSI) function backed by a block device.
//
// Everything is polled from the main loop: while an event is pending the
// USBFS hardware NAKs the host (INT_BUSY), so nothing is lost if the loop is
// busy for a while - it just runs slower.
#pragma once
#include <stdint.h>

namespace usbmsc {

// The storage behind the drive, in 512-byte blocks. A READ is one
// readStart, one readBlock per block, readStop; a WRITE likewise. readStop
// and writeStop are called exactly once after every readStart / writeStart,
// even if that start or a block failed, and also when the host abandons a
// command halfway (bus reset, BOT reset, upload request).
struct BlockDevice {
    uint32_t (*blocks)();                                  // 0 = no medium
    bool (*readStart)(uint32_t lba);
    bool (*readBlock)(uint8_t *dst);                       // next block of the run
    bool (*readStop)();
    bool (*writeStart)(uint32_t lba, uint32_t count);
    bool (*writeBlock)(const uint8_t *src);                // next block of the run
    bool (*writeStop)();                                   // false: the run did not complete
};

// WAITING covers "not enumerated yet" and "bus suspended" (PC asleep, or the
// cable pulled while running on battery); EJECTED is CONFIGURED after the
// host ejected the medium.
enum State : uint8_t { OFF, WAITING, CONFIGURED, READING, WRITING, EJECTED };

void begin(const BlockDevice &dev);   // take the USB peripheral over from the core
void poll();                          // service it; call as often as possible
State state();
bool busy();                          // a SCSI command is in progress (keep SPI free)
void setReadOnly(bool ro);
bool readOnly();
void mediaChanged();                  // new medium (or none): un-eject, tell the host
void detach();                        // drop off the bus
uint8_t *buffer();                    // 512 B the sketch may use while not busy()

// The serial function carries no data stream, just a way to ask for status:
int cdcRead();                                // last byte the host sent, or -1
bool cdcWrite(const char *s, uint8_t n);      // up to 144 bytes; false if busy

extern volatile uint32_t blocksRead, blocksWritten;

}  // namespace usbmsc
