// CHSDtoUSB - turn the CHGame into a USB SD card reader.
//
// Plug it in and the microSD card appears as a drive (USB Mass Storage),
// next to the usual CHGame serial port - so arduino-cli / the IDE can still
// upload a new sketch at any time, no button presses needed.
//
// Everything happens in this sketch: src/usb/UsbMsc takes the USB
// peripheral over from the core and re-enumerates as a composite device.
// Nothing in the core or the bootloader changes.
//
//   A (tap)        rescan the card (brings the drive back after an eject)
//   START          toggle read-only (the host is told the medium changed)
//   B (hold 1 s)   eject and reboot into the bootloader
//   LEFT / RIGHT   the screen's panel: events, stats, the card
//   UP / DOWN      scroll the event log
//   B held at power-up: safe mode - stay a plain USB serial device
//
// The screen (src/ui) graphs every command the PC sends and names the
// files it creates, deletes, renames and moves: src/mon works that out from
// the card's layout and the directory blocks going past.
//
// Cards can be swapped while it runs. The board has no card-detect switch,
// so once a second the card is asked whether it is still there (or, with
// the slot empty, whether one has gone in).
//
// Every block read from the card is CRC-checked, and the card is told to
// check the CRC of every command and every block written to it, so a bit
// flipped on the wire is retried - never stored, never handed to the PC.
// Any byte sent to the serial port returns a status line ('U': the
// screen's drawing times instead).
//
// The SD block driver (src/sd) is the fast DMA Sd2Card from CHStlView,
// derived from William Greiman's sdfatlib: GPL-3.0, so this sketch is too.
#include <CHGfx.h>
#include "src/sd/Sd2Card.h"
#include "src/usb/UsbMsc.h"
#include "src/mon/Monitor.h"
#include "src/ui/Ui.h"

extern "C" void chgame_enter_bootloader(void);

static Sd2Card card;
static bool cardOk = false;
static uint32_t cardBlocks = 0;

static uint32_t rdLba;                                     // block devReadBlock() reads next
static uint32_t wrLba, wrLeft;                             // block devWriteBlock() writes next; blocks left
static bool wrOpen = false;                                // the card is in a write run (CMD25)
static uint32_t rdRetries = 0, rdFails = 0, wrRetries = 0, wrFails = 0;

#ifdef CHSD_TEST
// Test build (-DCHSD_TEST): serial commands stand in for the buttons and
// inject faults, so tools/chsd_test.py can drive every path from the PC.
// Soft faults garble 1 block in 50 on its first try (a retry fixes it);
// hard ones make one block, n blocks from now, fail every try (the PC sees
// the error).
static bool tReadSoft = false, tWriteSoft = false, tReadHard = false, tWriteHard = false;
static uint32_t tReads = 0, tWrites = 0, tReadHardIn = 0, tWriteHardIn = 0, tNoCardUntil = 0;
static bool fault(uint8_t tries, bool soft, uint32_t &count, uint32_t &hardIn, bool &hard) {
    if (!tries && hardIn && !--hardIn) hard = true;
    if (hard) { if (tries == 3) hard = false; return true; }
    return soft && !tries && ++count % 50 == 0;
}
static bool readFault(uint8_t tries) { return fault(tries, tReadSoft, tReads, tReadHardIn, tReadHard); }
static bool writeFault(uint8_t tries) { return fault(tries, tWriteSoft, tWrites, tWriteHardIn, tWriteHard); }
#endif

// ---- The card as the USB side sees it ------------------------------------
static uint32_t devBlocks() { return cardOk ? cardBlocks : 0; }
static bool devReadStart(uint32_t lba) { rdLba = lba; mon::cmdStart(false, lba); return card.readStart(lba); }

// A block that does not arrive, or does not match the CRC16 the card sent
// with it, is read again (the stream restarts at it), up to 4 tries, before
// the PC is told the read failed.
static bool devReadBlock(uint8_t *d) {
    for (uint8_t tries = 0; tries < 4; tries++) {
        if (tries) {
            rdRetries++;
            mon::cmdRetry();
            card.readStop();
            if (!card.readStart(rdLba)) continue;
        }
        bool ok = card.readBlockChecked(d);
#ifdef CHSD_TEST
        if (ok && readFault(tries)) ok = false;           // as if the CRC had not matched
#endif
        if (ok) { mon::cmdBlock(rdLba++, d); return true; }
    }
    rdFails++;
    mon::cmdFail();
    return false;
}
static bool devReadStop() { bool ok = card.readStop(); mon::cmdEnd(); return ok; }

// A block the card rejects (bad CRC, or no clean data response) ends the
// run; a new one starts at that block and it is sent again, up to 4 tries.
static bool devWriteStart(uint32_t lba, uint32_t n) {
    wrLba = lba;
    wrLeft = n;
    mon::cmdStart(true, lba);
    wrOpen = card.writeStart(lba, n);
    return wrOpen;
}
static bool devWriteBlock(const uint8_t *s) {
    uint16_t crc = 0;
    bool haveCrc = false;
    for (uint8_t tries = 0; tries < 4; tries++) {
        if (tries) {
            wrRetries++;
            mon::cmdRetry();
            if (wrOpen) card.writeStop();
            wrOpen = card.writeStart(wrLba, wrLeft);
            if (!wrOpen) continue;
        }
        if (!card.writeDataStart(s)) continue;            // the block goes out by DMA...
        if (!haveCrc) { crc = Sd2Card::crc16(s, 512); haveCrc = true; }   // ...while its CRC is worked out
        uint16_t sent = crc;
#ifdef CHSD_TEST
        if (writeFault(tries)) sent ^= 0x0101;
#endif
        if (card.writeDataEnd(sent)) { mon::cmdBlock(wrLba++, s); wrLeft--; return true; }
    }
    wrFails++;
    mon::cmdFail();
    return false;
}
static bool devWriteStop() {
    bool ok = wrOpen && card.writeStop();
    wrOpen = false;
    mon::cmdEnd();
    return ok;
}

static const usbmsc::BlockDevice DEV = {
    devBlocks, devReadStart, devReadBlock, devReadStop, devWriteStart, devWriteBlock, devWriteStop,
};

static bool monRead(uint32_t lba, uint8_t *dst) { return card.readBlock(lba, dst); }

// The monitor reads the new card's layout from its first blocks, the screen
// gets its identity (CID).
static void cardChanged() {
    mon::mount(cardBlocks, monRead, usbmsc::buffer());
    uint8_t cid[16];
    ui::setCard(cardOk && card.readCID((cid_t *)cid) ? cid : nullptr, cardOk ? card.type() : 0);
    if (cardOk) mon::event(mon::EV_CARDIN, mon::vol.label, cardBlocks);
}

// cmd0Timeout: how long an empty slot is given to answer (see Sd2Card::init).
static bool initCard(unsigned int cmd0Timeout = SD_INIT_TIMEOUT) {
    gfx_wait();
    cardOk = card.init(SPI_FULL_SPEED, PIN_SD_CS, cmd0Timeout);
    if (cardOk) card.crcOn();              // mandatory in SPI mode; reads are checked here either way
    cardBlocks = cardOk ? card.cardSize() : 0;
    if (!cardBlocks) cardOk = false;
    if (cardOk) cardChanged();
    return cardOk;
}

static bool cardPresent(uint32_t now) {
#ifdef CHSD_TEST
    if ((int32_t)(tNoCardUntil - now) > 0) return false;
#endif
    (void)now;
    gfx_wait();
    return card.present();
}

static bool safeMode = false;
static uint32_t nextProbe = 0, lastButtons = 0;
static uint32_t bHeld = 0;                                 // when B went down
static uint32_t sHeld = 0;                                 // when START went down
static bool prevA = false, prevStart = false, bDown = false;
static uint8_t prevPad = 0;                                // d-pad, one bit a button
static usbmsc::State prevUsb = usbmsc::OFF;

static bool pressed(uint8_t pin) { return digitalRead(pin) == LOW; }

// A present card answers CMD0 at once, so an empty slot needn't cost 2 s.
static void pressA() { initCard(250); usbmsc::mediaChanged(); }
static void pressStart() {
    usbmsc::setReadOnly(!usbmsc::readOnly());
    mon::event(usbmsc::readOnly() ? mon::EV_RO : mon::EV_RW, "", 0);
}

static char *fmtU(char *p, uint32_t v) {
    char t[10];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static char *put(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

// ---- Serial status ----------------------------------------------------------
// One line: blocks read and written, read retries and failures, write
// retries and failures, card size in blocks (0 = none), RO/RW, and state.
static void sendStatus() {
    static const char *const LABEL[6] = {"R ", " W ", " RETRY ", " FAIL ", " WRETRY ", " WFAIL "};
    static const char *const STATE[6] = {"OFF", "WAITING", "CONNECTED", "READING", "WRITING", "EJECTED"};
    uint32_t vals[6] = {usbmsc::blocksRead, usbmsc::blocksWritten, rdRetries, rdFails, wrRetries, wrFails};
    char line[144], *p = line;                             // <= 129 characters + NUL
    for (int i = 0; i < 6; i++) { p = put(p, LABEL[i]); p = fmtU(p, vals[i]); }
    p = put(p, " CARD ");
    p = fmtU(p, devBlocks());
    p = put(p, usbmsc::readOnly() ? " RO " : " RW ");
    p = put(p, STATE[usbmsc::state()]);
#ifdef CHSD_TEST
    p = put(p, " TEST");                                   // so the test tool knows
#endif
    p = put(p, "\r\n");
    usbmsc::cdcWrite(line, (uint8_t)(p - line));
}

// 'U': how long the screen takes. Frames drawn, average and longest drawing
// time in microseconds, average rows flushed a frame.
static void sendPerf() {
    char line[80], *p = put(line, "UI FRAMES ");
    uint32_t f = ui::perf.frames ? ui::perf.frames : 1;
    p = fmtU(p, ui::perf.frames);
    p = fmtU(put(p, " AVG "), ui::perf.totalUs / f);
    p = fmtU(put(p, " MAX "), ui::perf.maxUs);
    p = fmtU(put(p, " ROWS "), ui::perf.rows / f);
    p = put(p, "\r\n");
    usbmsc::cdcWrite(line, (uint8_t)(p - line));
}

static void command(int c, uint32_t now) {
    if (c == 'U') { sendPerf(); return; }
#ifdef CHSD_TEST
    switch (c) {
        case 'a': pressA(); break;
        case 's': pressStart(); break;
        case 'x': tNoCardUntil = now + 4000; nextProbe = now; break;   // "pull the card" for 4 s
        case 'r': tReadSoft = !tReadSoft; break;
        case 'R': tReadHardIn = 1; break;                       // the next block read fails
        case 'M': tReadHardIn = 21; break;                      // ... the 21st, mid-run
        case 'w': tWriteSoft = !tWriteSoft; break;
        case 'W': tWriteHardIn = 1; break;                      // the next block written fails
        case 'V': tWriteHardIn = 21; break;                     // ... the 21st, mid-run
        case 'z': usbmsc::blocksRead = usbmsc::blocksWritten = rdRetries = rdFails = wrRetries = wrFails = 0; break;
        case 'c': case 'n': card.crcOn(c == 'c'); break;       // card-side CRC checking on / off
    }
#endif
    (void)c;
    (void)now;
    sendStatus();
}

static ui::Status status() {
    using namespace usbmsc;
    State u = state();
    ui::Status s;
    s.mode = safeMode ? ui::M_SAFE : !cardOk ? ui::M_NOCARD : u == EJECTED ? ui::M_EJECTED
           : u < CONFIGURED ? ui::M_WAITING : ui::M_READY;
    s.ro = readOnly();
    return s;
}

// ---- Arduino ----------------------------------------------------------------
void setup() {
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
    pinMode(PIN_BTN_START, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    gfx_begin(GFX_DIV2, GFX_12BPP);
    ui::begin();
    safeMode = pressed(PIN_BTN_B);
    initCard();
    if (!safeMode) usbmsc::begin(DEV);
    int y0, y1;
    ui::frame(millis(), status(), y0, y1);
    gfx_flush();
}

void loop() {
    usbmsc::poll();
    uint32_t now = millis();

#ifdef CHSD_AUTOBOOT_MS
    // Bring-up safety net (build flag only): whatever state USB is in, go
    // back to the bootloader after a while so the board stays uploadable.
    if (now > CHSD_AUTOBOOT_MS && !usbmsc::busy()) {
        usbmsc::detach();
        delay(300);
        chgame_enter_bootloader();
    }
#endif

    // Activity LED.
    digitalWrite(LED_BUILTIN, usbmsc::busy() ? HIGH : LOW);

    if (usbmsc::busy()) return;                  // the card holds SPI mid-command

    // What the PC did to the medium.
    usbmsc::State u = usbmsc::state();
    if (u != prevUsb) {
        if (u == usbmsc::EJECTED) mon::event(mon::EV_EJECT, "", 0);
        else if (u == usbmsc::CONFIGURED && prevUsb < usbmsc::CONFIGURED) mon::event(mon::EV_PC, "", 0);
        prevUsb = u;
    }
    mon::tick();
    if (mon::remountWanted && cardOk) mon::mount(cardBlocks, monRead, usbmsc::buffer());   // repartitioned / formatted

    // Any byte on the serial port asks for a status line.
    int c = usbmsc::cdcRead();
    if (c >= 0) command(c, now);

    // No card-detect switch: once a second, a card we have must still answer,
    // and an empty slot is checked for a new one (CMD0 gets no answer from
    // an empty slot within 50 ms). A card that answers but will not start
    // is retried every 5 s instead, as each attempt takes up to 2 s.
    if (!safeMode && (int32_t)(now - nextProbe) >= 0) {
        nextProbe = now + 1000;
        if (cardOk) {
            if (!cardPresent(now)) {
                cardOk = false;
                cardBlocks = 0;
                usbmsc::mediaChanged();
                cardChanged();
                mon::event(mon::EV_CARDOUT, "", 0);
            }
        }
#ifdef CHSD_TEST
        else if ((int32_t)(tNoCardUntil - now) > 0) { }
#endif
        else if (initCard(50)) usbmsc::mediaChanged();
        else if (card.errorCode() != SD_CARD_ERROR_CMD0) nextProbe = now + 5000;
    }

    // Buttons, sampled every 20 ms: slower than contact bounce, so one press
    // is one edge (START toggles; a bounce would toggle it straight back).
    if (now - lastButtons >= 20) {
        lastButtons = now;
        bool a = pressed(PIN_BTN_A), start = pressed(PIN_BTN_START);
        if (a && !prevA && !safeMode) pressA();
        if (start && !prevStart) {
            sHeld = now;
            if (!safeMode) pressStart();
        }
        prevA = a; prevStart = start;
        static const uint8_t PAD[4] = {PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_UP, PIN_BTN_DOWN};
        for (int i = 0; i < 4; i++) {
            bool p = pressed(PAD[i]);
            if (p && !(prevPad & (1 << i))) ui::button(PAD[i]);
            prevPad = (uint8_t)(p ? prevPad | (1 << i) : prevPad & ~(1 << i));
        }
        if (pressed(PIN_BTN_B)) {
            if (!bDown) { bDown = true; bHeld = now; }
        } else bDown = false;
        // B held 1 s, or START held 3 s as in every CHCasino game: back to the
        // SD game menu. With the menu bootloader any reset without a request
        // shows the menu. (Uploads still reach the bootloader through the
        // 1200-baud touch.)
        if (!safeMode && ((bDown && now - bHeld > 1000) || (start && now - sHeld >= 3000))) {
            ui::goodbye();
            gfx_flush();
            usbmsc::detach();
            delay(300);
            NVIC_SystemReset();
        }
    }

    // The screen, only between SCSI commands (the LCD shares SPI1): up to
    // 5 frames a second while commands come, 25 while something animates,
    // none while nothing changes, and only the rows that changed are sent.
    // The flush runs on by DMA; a command that arrives meanwhile waits for
    // it in the card driver.
    int y0, y1;
    if (ui::frame(now, status(), y0, y1)) gfx_flushRectAsync(0, y0, GFX_W, y1 - y0);
}
