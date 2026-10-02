// Composite CDC-ACM + Mass Storage (Bulk-Only Transport, SCSI) device for
// the CH32X035 USBFS peripheral, polled from the sketch. See UsbMsc.h.
//
// Endpoints: EP0 control (64), EP1 IN interrupt (CDC notifications, never
// used), EP2 IN/OUT bulk (CDC data), EP3 IN/OUT bulk (MSC).
// With both directions enabled an endpoint's buffer is 128 bytes: OUT data
// arrives at the DMA address, IN data is sent from DMA + 64.
#include <Arduino.h>
#include <string.h>
#include "UsbMsc.h"

extern "C" {
extern volatile uint8_t USB_ENUM_OK;        // core CDC: nonzero = its Serial is live
void chgame_enter_bootloader(void);
}

namespace usbmsc {

volatile uint32_t blocksRead = 0, blocksWritten = 0;

// ---- USBFS register bits (values from the core's wch_usbfs_compat.h and
// ---- WCH's ch32x035_usb.h) --------------------------------------------------
enum : uint8_t { UC_DMA_EN = 0x01, UC_CLR_ALL = 0x02, UC_RESET_SIE = 0x04, UC_INT_BUSY = 0x08, UC_DEV_PU_EN = 0x20 };
enum : uint8_t { UD_PORT_EN = 0x01, UD_PD_DIS = 0x80 };
enum : uint8_t { UIF_BUS_RST = 0x01, UIF_TRANSFER = 0x02, UIF_SUSPEND = 0x04, U_TOG_OK = 0x40 };
enum : uint8_t { UMS_SUSPEND = 0x04 };      // MIS_ST: the bus is suspended right now
enum : uint8_t { UIS_ENDP = 0x0F, UIS_TOKEN = 0x30, TOK_OUT = 0x00, TOK_IN = 0x20, TOK_SETUP = 0x30 };
enum : uint16_t {
    T_ACK = 0x00, T_NAK = 0x02, T_STALL = 0x03, T_MASK = 0x03,
    R_ACK = 0x00, R_NAK = 0x08, R_STALL = 0x0C, R_MASK = 0x0C,
    AUTO_TOG = 0x10, T_TOG = 0x40, R_TOG = 0x80,
};
enum : uint8_t { UEP1_TX_EN = 0x40, UEP2_TX_EN = 0x04, UEP2_RX_EN = 0x08, UEP3_TX_EN = 0x40, UEP3_RX_EN = 0x80 };
// The D+ pull-up that tells the host "something is plugged in" is set in
// AFIO->CTLR on this part (UDP_PUE), not by USBFS BASE_CTRL. Clearing
// BASE_CTRL alone stops the device answering without the host ever seeing
// it leave - it keeps talking to an address that no longer exists.
static const uint32_t AFIO_UDP_PUE = 0x0000000C;

alignas(4) static uint8_t ep0[64];
alignas(4) static uint8_t ep1[64];
alignas(4) static uint8_t ep2[128];
alignas(4) static uint8_t ep3[128];
alignas(4) static uint8_t sector[512];
static int cdcRxByte = -1;                   // last byte the host sent on the serial port
static uint8_t cdcTx[144];                   // text queued for the serial port
static uint8_t cdcTxLen = 0, cdcTxPos = 0;
static bool cdcTxBusy = false;               // EP2 IN armed, not yet collected
static bool cdcTxZlp = false;                // last packet was full: end with a zero-length one

// ---- Descriptors ----------------------------------------------------------
// Same VID:PID as the CHGame core and bootloader, so chgame-upload finds the
// port; a different serial string ("CGM" + chip UID) so Windows enumerates
// this composite device fresh instead of reusing the serial-only setup.
static const uint8_t DEV_DESC[18] = {
    18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, 64,        // USB 2.0, IAD composite, EP0 64
    0xC0, 0x16, 0xDD, 0x27, 0x01, 0x02,             // 16C0:27DD, bcdDevice 2.01
    1, 2, 3, 1,
};

static const uint8_t CFG_DESC[98] = {
    9, 2, 98, 0, 3, 1, 0, 0x80, 250,                // 3 interfaces, 500 mA
    8, 0x0B, 0, 2, 0x02, 0x02, 0x01, 4,             // IAD: CDC-ACM on interfaces 0-1
    9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 4,             // interface 0: CDC control
    5, 0x24, 0x00, 0x10, 0x01,                      //   header
    5, 0x24, 0x01, 0x00, 0x01,                      //   call management
    4, 0x24, 0x02, 0x02,                            //   ACM: line coding + state
    5, 0x24, 0x06, 0x00, 0x01,                      //   union 0 -> 1
    7, 5, 0x81, 0x03, 8, 0, 16,                     //   EP1 IN interrupt
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,             // interface 1: CDC data
    7, 5, 0x02, 0x02, 64, 0, 0,                     //   EP2 OUT bulk
    7, 5, 0x82, 0x02, 64, 0, 0,                     //   EP2 IN bulk
    9, 4, 2, 0, 2, 0x08, 0x06, 0x50, 5,             // interface 2: mass storage, SCSI, BOT
    7, 5, 0x83, 0x02, 64, 0, 0,                     //   EP3 IN bulk
    7, 5, 0x03, 0x02, 64, 0, 0,                     //   EP3 OUT bulk
};

static const char *const STRINGS[] = {nullptr, "CHGame", "CHGame SD Card Reader", nullptr,
                                      "CHGame Serial", "CHGame SD Card"};
static char serial[28];                             // "CGM" + 24 hex digits of the UID
static uint8_t strBuf[2 + 2 * 28];

static const uint8_t *stringDesc(uint8_t idx, uint16_t &len) {
    if (idx == 0) { static const uint8_t LANG[4] = {4, 3, 0x09, 0x04}; len = 4; return LANG; }
    const char *s = idx == 3 ? serial : (idx < 6 ? STRINGS[idx] : nullptr);
    if (!s) return nullptr;
    uint8_t n = 0;
    while (s[n] && n < 28) { strBuf[2 + 2 * n] = (uint8_t)s[n]; strBuf[3 + 2 * n] = 0; n++; }
    strBuf[0] = (uint8_t)(2 + 2 * n);
    strBuf[1] = 3;
    len = strBuf[0];
    return strBuf;
}

// ---- State ----------------------------------------------------------------
static BlockDevice dev;
static State st = OFF;                   // OFF, WAITING, CONFIGURED, READING, WRITING
static bool ro = false, ejected = false, attention = false;

// EP0
static const uint8_t *txPtr = nullptr;
static uint16_t txLeft = 0;
static uint8_t reqType = 0, req = 0, newAddr = 0, config = 0;
static bool lineCodingOut = false, bootRequest = false;
static uint8_t lineCoding[7] = {0x00, 0xC2, 0x01, 0x00, 0, 0, 8};   // 115200 8N1

// Bulk-Only Transport
enum Bot : uint8_t { B_CBW, B_DATA_IN, B_DATA_OUT, B_CSW };
static Bot bot = B_CBW;
static uint32_t tag = 0, residue = 0;
static uint32_t cbwLen = 0;              // data-phase length the host announced in the CBW
static bool cbwOut = false;              // ... and its direction: true = host sends
static uint8_t cswStatus = 0;
static const uint8_t *inPtr = nullptr;
static uint16_t inLeft = 0;
static bool readRun = false;             // a READ has the card streaming (readStart .. readStop)
static bool writeRun = false;            // a WRITE has a run open (writeStart .. writeStop)
static bool writeFailed = false;         // a block of this WRITE failed: swallow the rest
static uint32_t runLeft = 0;             // blocks still to move in a READ/WRITE run
static uint32_t discardLeft = 0;         // bytes of a refused OUT data phase still to swallow
static uint16_t sectorPos = 0;
static uint8_t senseKey = 0, senseAsc = 0, senseAscq = 0;

State state() {
    if (st <= WAITING) return st;
    if (USBFSD->MIS_ST & UMS_SUSPEND) return WAITING;          // PC asleep, or the cable is out
    if (st == CONFIGURED && ejected) return EJECTED;
    return st;
}
bool busy() { return bot != B_CBW; }
bool readOnly() { return ro; }
void setReadOnly(bool r) { ro = r; attention = true; }
void mediaChanged() { ejected = false; attention = true; }

// ---- Endpoint plumbing ----------------------------------------------------
// Finish the card's side of the command in progress (close a read stream or
// a write run) and forget its data phase. For when the host abandons a
// command - bus reset, BOT reset, reconfiguration, suspend - and before
// rebooting for an upload: the card must never be left mid-transfer, or the
// next command (or the next sketch) finds it still streaming.
static void abortCommand() {
    if (readRun) { readRun = false; dev.readStop(); }
    if (writeRun) { writeRun = false; dev.writeStop(); }
    bot = B_CBW;
    inLeft = 0;
    discardLeft = 0;
    if (st == READING || st == WRITING) st = CONFIGURED;
    USBFSD->UEP3_CTRL_H = (uint16_t)((USBFSD->UEP3_CTRL_H & ~T_MASK) | T_NAK);
}

static void cdcReset() {
    USBFSD->UEP2_CTRL_H = (uint16_t)((USBFSD->UEP2_CTRL_H & ~T_MASK) | T_NAK);
    cdcTxBusy = cdcTxZlp = false;
    cdcTxLen = cdcTxPos = 0;
}

static void endpointsInit() {
    abortCommand();
    USBFSD->UEP0_DMA = (uint32_t)ep0;
    USBFSD->UEP1_DMA = (uint32_t)ep1;
    USBFSD->UEP2_DMA = (uint32_t)ep2;
    USBFSD->UEP3_DMA = (uint32_t)ep3;
    USBFSD->UEP4_1_MOD = UEP1_TX_EN;
    USBFSD->UEP2_3_MOD = UEP2_RX_EN | UEP2_TX_EN | UEP3_RX_EN | UEP3_TX_EN;
    USBFSD->UEP0_CTRL_H = R_ACK | T_NAK;
    USBFSD->UEP0_TX_LEN = 0;
    USBFSD->UEP1_CTRL_H = AUTO_TOG | T_NAK;
    USBFSD->UEP1_TX_LEN = 0;
    USBFSD->UEP2_CTRL_H = AUTO_TOG | R_ACK | T_NAK;
    USBFSD->UEP2_TX_LEN = 0;
    USBFSD->UEP3_CTRL_H = AUTO_TOG | R_ACK | T_NAK;
    USBFSD->UEP3_TX_LEN = 0;
    cdcReset();
}

static void ep3Send(uint16_t n) {
    USBFSD->UEP3_TX_LEN = n;
    USBFSD->UEP3_CTRL_H = (uint16_t)((USBFSD->UEP3_CTRL_H & ~T_MASK) | T_ACK);
}

static void sendCsw(uint8_t status) {
    uint8_t *c = ep3 + 64;
    c[0] = 'U'; c[1] = 'S'; c[2] = 'B'; c[3] = 'S';
    memcpy(c + 4, &tag, 4);
    memcpy(c + 8, &residue, 4);
    c[12] = status;
    bot = B_CSW;
    ep3Send(13);
}

static void sendChunk() {
    uint16_t n = inLeft > 64 ? 64 : inLeft;
    memcpy(ep3 + 64, inPtr, n);
    inPtr += n;
    inLeft -= n;
    ep3Send(n);
}

static void sense(uint8_t key, uint8_t asc, uint8_t ascq) { senseKey = key; senseAsc = asc; senseAscq = ascq; }

// End a command that moves no (more) data. Whatever data phase the host
// announced still has to happen: swallow what it sends, or end what it
// expects to receive with a zero-length packet. Then the CSW. Never a
// STALL, so the host never needs its reset-recovery path.
static void finish(uint8_t status) {
    cswStatus = status;
    residue = cbwLen;
    if (!cbwLen) { sendCsw(status); return; }
    if (cbwOut) {
        discardLeft = cbwLen;
        bot = B_DATA_OUT;
    } else {
        inLeft = 0;
        bot = B_DATA_IN;
        ep3Send(0);
    }
}

static void fail(uint8_t key, uint8_t asc, uint8_t ascq) {
    sense(key, asc, ascq);
    finish(1);
}

// Reply with `n` bytes from `sector` (fewer if the host asked for fewer).
static void replyData(uint16_t n) {
    if (cbwOut) { fail(0x05, 0x24, 0x00); return; }            // host meant to send, not receive
    if (n > cbwLen) n = (uint16_t)cbwLen;
    if (!n) { finish(0); return; }
    residue = cbwLen - n;
    cswStatus = 0;
    inPtr = sector;
    inLeft = n;
    bot = B_DATA_IN;
    sendChunk();
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void putBe32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

// ---- SCSI -----------------------------------------------------------------
static void scsi(const uint8_t *cb) {
    uint32_t blocks = ejected ? 0 : dev.blocks();
    uint8_t op = cb[0];
    if (op != 0x03 && op != 0x12 && attention && blocks) {       // medium changed: say so once
        attention = false;
        fail(0x06, 0x28, 0x00);
        return;
    }
    switch (op) {
        case 0x00:                                              // TEST UNIT READY
            if (!blocks) { fail(0x02, 0x3A, 0x00); return; }
            finish(0);
            return;
        case 0x03:                                              // REQUEST SENSE
            memset(sector, 0, 18);
            sector[0] = 0x70; sector[2] = senseKey; sector[7] = 10;
            sector[12] = senseAsc; sector[13] = senseAscq;
            sense(0, 0, 0);
            replyData(18);
            return;
        case 0x12:                                              // INQUIRY
            if (cb[1] & 1) { fail(0x05, 0x24, 0x00); return; }  // no VPD pages
            memset(sector, 0, 36);
            sector[1] = 0x80;                                   // removable
            sector[2] = 0x02; sector[3] = 0x02; sector[4] = 31;
            memcpy(sector + 8, "CHGame  SD Card Reader  1.00", 28);
            replyData(36);
            return;
        case 0x1A: case 0x5A: {                                 // MODE SENSE (6) / (10)
            bool ten = op == 0x5A;
            memset(sector, 0, 8);
            if (ten) { sector[1] = 6; sector[3] = ro ? 0x80 : 0; }
            else { sector[0] = 3; sector[2] = ro ? 0x80 : 0; }
            replyData(ten ? 8 : 4);
            return;
        }
        case 0x1B:                                              // START STOP UNIT
            if ((cb[4] & 0x03) == 0x02) ejected = true;          // eject
            else if ((cb[4] & 0x03) == 0x03) ejected = false;    // load
            finish(0);
            return;
        case 0x1E:                                              // PREVENT/ALLOW MEDIUM REMOVAL
            finish(0);
            return;
        case 0x2F: case 0x35:                                   // VERIFY (10), SYNCHRONIZE CACHE (10)
            if (!blocks) { fail(0x02, 0x3A, 0x00); return; }    // (nothing is cached; writes are through)
            finish(0);
            return;
        case 0x23:                                              // READ FORMAT CAPACITIES
            if (!blocks) { fail(0x02, 0x3A, 0x00); return; }
            memset(sector, 0, 12);
            sector[3] = 8;
            putBe32(sector + 4, blocks);
            sector[8] = 0x02;                                   // formatted media
            sector[10] = 0x02;                                  // 512-byte blocks
            replyData(12);
            return;
        case 0x25:                                              // READ CAPACITY (10)
            if (!blocks) { fail(0x02, 0x3A, 0x00); return; }
            putBe32(sector, blocks - 1);
            putBe32(sector + 4, 512);
            replyData(8);
            return;
        case 0x28: case 0x2A: {                                 // READ (10) / WRITE (10)
            uint32_t lba = be32(cb + 2);
            uint32_t count = ((uint32_t)cb[7] << 8) | cb[8];
            bool write = op == 0x2A;
            if (!blocks) { fail(0x02, 0x3A, 0x00); return; }
            if (lba >= blocks || count > blocks - lba) { fail(0x05, 0x21, 0x00); return; }
            if (write && ro) { fail(0x07, 0x27, 0x00); return; }
            if (!count) { finish(0); return; }
            // The data phase must be exactly the blocks, in the right direction
            // (every real host does that); anything else is refused cleanly.
            if (cbwLen != count * 512 || cbwOut != write) { fail(0x05, 0x24, 0x00); return; }
            residue = 0;
            cswStatus = 0;
            runLeft = count;
            if (write) {
                if (!dev.writeStart(lba, count)) {
                    dev.writeStop();
                    fail(0x03, 0x0C, 0x00);                     // write error
                    return;
                }
                writeRun = true;
                writeFailed = false;
                sectorPos = 0;
                st = WRITING;
                bot = B_DATA_OUT;
            } else {
                readRun = true;
                if (!dev.readStart(lba) || !dev.readBlock(sector)) {
                    readRun = false;
                    dev.readStop();
                    fail(0x03, 0x11, 0x00);                     // unrecovered read error
                    return;
                }
                blocksRead++;
                runLeft--;
                inPtr = sector;
                inLeft = 512;
                st = READING;
                bot = B_DATA_IN;
                sendChunk();
            }
            return;
        }
        default:
            fail(0x05, 0x20, 0x00);                             // invalid command
            return;
    }
}

static void botIn() {                                           // EP3 IN packet delivered
    USBFSD->UEP3_CTRL_H = (uint16_t)((USBFSD->UEP3_CTRL_H & ~T_MASK) | T_NAK);
    if (bot == B_CSW) { bot = B_CBW; if (st == READING || st == WRITING) st = CONFIGURED; return; }
    if (bot != B_DATA_IN) return;
    if (inLeft) { sendChunk(); return; }
    if (readRun && runLeft) {
        if (!dev.readBlock(sector)) {                           // unreadable block mid-run
            readRun = false;
            dev.readStop();
            sense(0x03, 0x11, 0x00);
            residue = runLeft * 512;
            cswStatus = 1;
            ep3Send(0);                                         // short packet: the data ends here
            return;                                             // (the CSW follows it)
        }
        blocksRead++;
        runLeft--;
        inPtr = sector;
        inLeft = 512;
        sendChunk();
        return;
    }
    if (readRun) { readRun = false; dev.readStop(); }
    sendCsw(cswStatus);
}

static void botOut(uint16_t n) {                                // EP3 OUT packet received
    if (bot == B_CBW) {
        if (n != 31 || memcmp(ep3, "USBC", 4)) return;          // not a CBW: ignore it
        memcpy(&tag, ep3 + 4, 4);
        memcpy(&cbwLen, ep3 + 8, 4);
        cbwOut = !(ep3[12] & 0x80) && cbwLen;
        scsi(ep3 + 15);
        return;
    }
    if (bot != B_DATA_OUT) return;
    if (discardLeft) {
        discardLeft = n >= discardLeft ? 0 : discardLeft - n;
        if (!discardLeft) sendCsw(cswStatus);
        return;
    }
    if (sectorPos + n > 512) n = (uint16_t)(512 - sectorPos);
    memcpy(sector + sectorPos, ep3, n);
    sectorPos += n;
    if (sectorPos < 512) return;
    sectorPos = 0;
    if (!writeFailed) {
        if (dev.writeBlock(sector)) blocksWritten++;
        else {                                                  // swallow the rest, then report it
            writeFailed = true;
            sense(0x03, 0x0C, 0x00);
            cswStatus = 1;
            residue = runLeft * 512;
        }
    }
    if (--runLeft) return;
    writeRun = false;
    if (!dev.writeStop() && !cswStatus) { sense(0x03, 0x0C, 0x00); cswStatus = 1; }
    sendCsw(cswStatus);
}

// ---- Serial function --------------------------------------------------------
static void cdcPump() {                                         // EP2 IN is free: next packet
    uint8_t n = (uint8_t)(cdcTxLen - cdcTxPos);
    if (!n && !cdcTxZlp) { cdcTxBusy = false; cdcTxLen = cdcTxPos = 0; return; }
    if (n > 64) n = 64;
    cdcTxZlp = n == 64 && cdcTxPos + n == cdcTxLen;
    memcpy(ep2 + 64, cdcTx + cdcTxPos, n);
    cdcTxPos += n;
    USBFSD->UEP2_TX_LEN = n;
    USBFSD->UEP2_CTRL_H = (uint16_t)((USBFSD->UEP2_CTRL_H & ~T_MASK) | T_ACK);
    cdcTxBusy = true;
}

// ---- Control transfers ----------------------------------------------------
static void ep0Send() {
    uint16_t n = txLeft > 64 ? 64 : txLeft;
    if (n) memcpy(ep0, txPtr, n);
    txPtr += n;
    txLeft -= n;
    USBFSD->UEP0_TX_LEN = n;
}

static void setup() {
    reqType = ep0[0];
    req = ep0[1];
    uint16_t wValue = (uint16_t)(ep0[2] | (ep0[3] << 8));
    uint16_t wIndex = (uint16_t)(ep0[4] | (ep0[5] << 8));
    uint16_t wLength = (uint16_t)(ep0[6] | (ep0[7] << 8));
    const uint8_t *data = nullptr;
    int len = -1;                                               // -1 = stall
    static uint8_t reply[2];
    lineCodingOut = false;

    if ((reqType & 0x60) == 0x00) {                             // standard
        switch (req) {
            case 0x06: {                                        // GET_DESCRIPTOR
                uint16_t l = 0;
                switch (wValue >> 8) {
                    case 1: data = DEV_DESC; l = sizeof DEV_DESC; break;
                    case 2: data = CFG_DESC; l = sizeof CFG_DESC; break;
                    case 3: data = stringDesc((uint8_t)wValue, l); break;
                }
                if (data) len = l;
                break;
            }
            case 0x05: newAddr = (uint8_t)(wValue & 0x7F); len = 0; break;          // SET_ADDRESS
            case 0x09:                                                               // SET_CONFIGURATION
                config = (uint8_t)wValue;
                endpointsInit();
                st = config ? CONFIGURED : WAITING;
                len = 0;
                break;
            case 0x08: reply[0] = config; data = reply; len = 1; break;             // GET_CONFIGURATION
            case 0x0A: reply[0] = 0; data = reply; len = 1; break;                  // GET_INTERFACE
            case 0x0B: len = 0; break;                                               // SET_INTERFACE
            case 0x00:                                                               // GET_STATUS
                reply[0] = reply[1] = 0;
                if ((reqType & 0x1F) == 2 && (wIndex & 0x0F) == 3) {
                    uint16_t c = USBFSD->UEP3_CTRL_H;
                    reply[0] = (wIndex & 0x80) ? ((c & T_MASK) == T_STALL) : ((c & R_MASK) == R_STALL);
                }
                data = reply; len = 2;
                break;
            case 0x01: case 0x03:                                                    // CLEAR/SET_FEATURE
                if ((reqType & 0x1F) == 2 && wValue == 0) {                          // ENDPOINT_HALT
                    uint8_t ep = (uint8_t)(wIndex & 0x0F);
                    volatile uint16_t *ctrl = ep == 1 ? &USBFSD->UEP1_CTRL_H : ep == 2 ? &USBFSD->UEP2_CTRL_H :
                                              ep == 3 ? &USBFSD->UEP3_CTRL_H : nullptr;
                    if (ctrl) {
                        bool in = wIndex & 0x80;
                        uint16_t c = *ctrl;
                        if (req == 0x01) c = in ? (uint16_t)((c & ~(T_MASK | T_TOG)) | T_NAK) : (uint16_t)(c & ~(R_MASK | R_TOG));
                        else c = in ? (uint16_t)(c | T_STALL) : (uint16_t)(c | R_STALL);
                        *ctrl = c;
                    }
                }
                len = 0;
                break;
        }
    } else if ((reqType & 0x60) == 0x20) {                      // class
        uint8_t itf = (uint8_t)wIndex;
        if (itf == 0) {                                          // CDC
            switch (req) {
                case 0x20: lineCodingOut = true; len = 0; break;                     // SET_LINE_CODING
                case 0x21: data = lineCoding; len = 7; break;                         // GET_LINE_CODING
                case 0x22: {                                                           // SET_CONTROL_LINE_STATE
                    uint32_t baud = lineCoding[0] | (lineCoding[1] << 8) | ((uint32_t)lineCoding[2] << 16);
                    if (!(wValue & 1)) {                         // DTR low: port closed
                        cdcReset();                              // drop any reply nobody will read
                        if (baud == 1200) bootRequest = true;    // the upload touch
                    }
                    len = 0;
                    break;
                }
            }
        } else if (itf == 2) {                                   // mass storage
            if (req == 0xFE) { reply[0] = 0; data = reply; len = 1; }               // GET_MAX_LUN
            if (req == 0xFF) {                                                        // BOT reset
                abortCommand();
                USBFSD->UEP3_CTRL_H = AUTO_TOG | R_ACK | T_NAK;  // both toggles back to DATA0
                len = 0;
            }
        }
    }

    if (len < 0) {
        USBFSD->UEP0_CTRL_H = T_TOG | T_STALL | R_TOG | R_STALL;
        return;
    }
    if (len > wLength) len = wLength;
    txPtr = data;
    txLeft = (uint16_t)len;
    ep0Send();
    USBFSD->UEP0_CTRL_H = T_TOG | T_ACK | R_TOG | R_ACK;       // data (or status) stage, DATA1
}

static void ep0In() {
    if ((reqType & 0x60) == 0 && req == 0x05) {                 // SET_ADDRESS: now it takes effect
        USBFSD->DEV_ADDR = (uint8_t)((USBFSD->DEV_ADDR & 0x80) | newAddr);
        USBFSD->UEP0_CTRL_H = T_NAK | R_TOG | R_ACK;
        return;
    }
    if (txPtr && (reqType & 0x80)) {                             // more IN data
        ep0Send();
        USBFSD->UEP0_CTRL_H ^= T_TOG;
        return;
    }
    USBFSD->UEP0_CTRL_H = T_NAK | R_TOG | R_ACK;
}

static void ep0Out() {
    if (lineCodingOut) {
        uint8_t n = USBFSD->RX_LEN;
        memcpy(lineCoding, ep0, n < 7 ? n : 7);
        lineCodingOut = false;
    }
    USBFSD->UEP0_TX_LEN = 0;
    USBFSD->UEP0_CTRL_H = T_TOG | T_ACK | R_ACK;
}

// ---- Public ---------------------------------------------------------------
void begin(const BlockDevice &d) {
    dev = d;
    // Serial number from the chip's 96-bit unique ID.
    static const char DIGITS[] = "0123456789ABCDEF";
    serial[0] = 'C'; serial[1] = 'G'; serial[2] = 'M';
    for (int w = 0; w < 3; w++) {
        uint32_t v = *(volatile uint32_t *)(0x1FFFF7E8 + 4 * w);
        for (int k = 0; k < 8; k++) serial[3 + w * 8 + k] = DIGITS[(v >> (28 - 4 * k)) & 15];
    }
    serial[27] = 0;

    // Take over from the core: no more core interrupt, core Serial silent,
    // then drop off the bus long enough for the host to notice.
    NVIC_DisableIRQ(USBFS_IRQn);
    USB_ENUM_OK = 0;
    uint32_t afio = AFIO->CTLR;
    USBFSD->BASE_CTRL = 0;
    AFIO->CTLR = afio & ~AFIO_UDP_PUE;                           // unplugged, as far as the host knows
    delay(400);
    USBFSD->BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    delayMicroseconds(50);
    USBFSD->BASE_CTRL = 0;
    endpointsInit();
    USBFSD->DEV_ADDR = 0;
    USBFSD->INT_FG = 0xFF;
    USBFSD->UDEV_CTRL = UD_PD_DIS | UD_PORT_EN;
    USBFSD->INT_EN = UIF_BUS_RST | UIF_TRANSFER | UIF_SUSPEND;   // flags only: IRQ stays off
    USBFSD->BASE_CTRL = UC_DEV_PU_EN | UC_INT_BUSY | UC_DMA_EN;
    AFIO->CTLR = afio | AFIO_UDP_PUE;                            // plugged in again: re-enumerate
    st = WAITING;
}

void detach() {
    if (st == OFF) return;
    abortCommand();
    USBFSD->BASE_CTRL = 0;
    AFIO->CTLR &= ~AFIO_UDP_PUE;
    st = OFF;
}

uint8_t *buffer() { return sector; }

int cdcRead() {
    int b = cdcRxByte;
    cdcRxByte = -1;
    return b;
}

bool cdcWrite(const char *s, uint8_t n) {
    if (st < CONFIGURED || cdcTxBusy || n > sizeof cdcTx) return false;
    memcpy(cdcTx, s, n);
    cdcTxLen = n;
    cdcTxPos = 0;
    cdcPump();
    return true;
}

void poll() {
    if (st == OFF) return;
    for (int guard = 0; guard < 8; guard++) {
        uint8_t f = USBFSD->INT_FG;
        if (!(f & (UIF_TRANSFER | UIF_BUS_RST | UIF_SUSPEND))) break;
        if (f & UIF_TRANSFER) {
            uint8_t s = USBFSD->INT_ST;
            uint8_t ep = s & UIS_ENDP;
            switch (s & UIS_TOKEN) {
                case TOK_SETUP: setup(); break;
                case TOK_IN:
                    if (ep == 0) ep0In();
                    else if (ep == 3) botIn();
                    else if (ep == 2) {
                        USBFSD->UEP2_CTRL_H = (uint16_t)((USBFSD->UEP2_CTRL_H & ~T_MASK) | T_NAK);
                        cdcPump();
                    }
                    break;
                case TOK_OUT:
                    if (ep == 0) ep0Out();
                    else if (ep == 3 && (f & U_TOG_OK)) botOut(USBFSD->RX_LEN);
                    else if (ep == 2 && (f & U_TOG_OK) && USBFSD->RX_LEN) cdcRxByte = ep2[0];   // keep one byte
                    break;
            }
            USBFSD->INT_FG = UIF_TRANSFER;
        }
        if (f & UIF_BUS_RST) {
            endpointsInit();
            USBFSD->DEV_ADDR = 0;
            config = 0;
            st = WAITING;
            USBFSD->INT_FG = UIF_BUS_RST;
        }
        if (f & UIF_SUSPEND) {
            // Fires on suspend and on resume. A host only suspends an idle
            // device, so a command still open here was abandoned with the cable.
            USBFSD->INT_FG = UIF_SUSPEND;
            if (USBFSD->MIS_ST & UMS_SUSPEND) abortCommand();
        }
    }
    if (bootRequest) {
        // Upload handshake (1200 baud, DTR low): let the status stage go out,
        // drop off the bus, reboot into the CHGame bootloader.
        uint32_t t0 = millis();
        while (millis() - t0 < 10) {
            if (USBFSD->INT_FG & UIF_TRANSFER) { USBFSD->INT_FG = UIF_TRANSFER; }
        }
        detach();                                                // also closes any card transfer
        delay(200);                                              // let the host see us go
        chgame_enter_bootloader();
    }
}

}  // namespace usbmsc
