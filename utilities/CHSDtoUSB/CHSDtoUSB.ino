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
//   B held at power-up: safe mode - stay a plain USB serial device
//
// Cards can be swapped while it runs. The board has no card-detect switch,
// so once a second the card is asked whether it is still there (or, with
// the slot empty, whether one has gone in).
//
// Every block read from the card is CRC-checked, and the card is told to
// check the CRC of every command and every block written to it, so a bit
// flipped on the wire is retried - never stored, never handed to the PC.
// Any byte sent to the serial port returns a status line.
//
// The SD block driver (src/sd) is the fast DMA Sd2Card from CHStlView,
// derived from William Greiman's sdfatlib: GPL-3.0, so this sketch is too.
#include <CHGfx.h>
#include "src/sd/Sd2Card.h"
#include "src/usb/UsbMsc.h"

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
static bool devReadStart(uint32_t lba) { rdLba = lba; return card.readStart(lba); }

// A block that does not arrive, or does not match the CRC16 the card sent
// with it, is read again (the stream restarts at it), up to 4 tries, before
// the PC is told the read failed.
static bool devReadBlock(uint8_t *d) {
    for (uint8_t tries = 0; tries < 4; tries++) {
        if (tries) {
            rdRetries++;
            card.readStop();
            if (!card.readStart(rdLba)) continue;
        }
        bool ok = card.readBlockChecked(d);
#ifdef CHSD_TEST
        if (ok && readFault(tries)) ok = false;           // as if the CRC had not matched
#endif
        if (ok) { rdLba++; return true; }
    }
    rdFails++;
    return false;
}
static bool devReadStop() { return card.readStop(); }

// A block the card rejects (bad CRC, or no clean data response) ends the
// run; a new one starts at that block and it is sent again, up to 4 tries.
static bool devWriteStart(uint32_t lba, uint32_t n) {
    wrLba = lba;
    wrLeft = n;
    wrOpen = card.writeStart(lba, n);
    return wrOpen;
}
static bool devWriteBlock(const uint8_t *s) {
    uint16_t crc = 0;
    bool haveCrc = false;
    for (uint8_t tries = 0; tries < 4; tries++) {
        if (tries) {
            wrRetries++;
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
        if (card.writeDataEnd(sent)) { wrLba++; wrLeft--; return true; }
    }
    wrFails++;
    return false;
}
static bool devWriteStop() {
    if (!wrOpen) return false;
    wrOpen = false;
    return card.writeStop();
}

static const usbmsc::BlockDevice DEV = {
    devBlocks, devReadStart, devReadBlock, devReadStop, devWriteStart, devWriteBlock, devWriteStop,
};

// cmd0Timeout: how long an empty slot is given to answer (see Sd2Card::init).
static bool initCard(unsigned int cmd0Timeout = SD_INIT_TIMEOUT) {
    gfx_wait();
    cardOk = card.init(SPI_FULL_SPEED, PIN_SD_CS, cmd0Timeout);
    if (cardOk) card.crcOn();              // mandatory in SPI mode; reads are checked here either way
    cardBlocks = cardOk ? card.cardSize() : 0;
    if (!cardBlocks) cardOk = false;
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

enum : uint8_t { BG, WHITE, SILVER, DIM, GOLD, GREEN, CYAN, RED, NAVY, INK, BLUE, ORANGE };
static const uint16_t PALETTE[16] = {
    0x10A6, 0xFFFF, 0xBDF7, 0x52AA, 0xFE60, 0x4F4A, 0x6F7F, 0xE8A4,
    0x18CC, 0x0000, 0x3B7F, 0xFB40, 0, 0, 0, 0,
};

static bool safeMode = false;
static uint32_t lastUi = 0, lastRate = 0, lastBlocks = 0, rateKBs = 0, nextProbe = 0, lastButtons = 0;
static uint32_t bHeld = 0;                                 // when B went down
static bool prevA = false, prevStart = false, bDown = false;

static bool pressed(uint8_t pin) { return digitalRead(pin) == LOW; }

// A present card answers CMD0 at once, so an empty slot needn't cost 2 s.
static void pressA() { initCard(250); usbmsc::mediaChanged(); }
static void pressStart() { usbmsc::setReadOnly(!usbmsc::readOnly()); }

// ---- Screen ---------------------------------------------------------------
static void centred(int y, const char *s, uint8_t c, uint8_t scale = 1) {
    gfx_textScaled(64 - gfx_textWidthScaled(s, scale) / 2, y, s, c, scale);
}

static void sdIcon(int x, int y, uint8_t body, uint8_t label) {
    // A microSD silhouette: notched corner, contacts, a label.
    gfx_fillRect(x + 6, y, 30, 44, body);
    gfx_fillRect(x, y + 10, 36, 34, body);
    for (int i = 0; i < 6; i++) gfx_hline(x + 6 - i, y + 4 + i, i, body);
    for (int i = 0; i < 6; i++) gfx_fillRect(x + 9 + i * 4, y + 2, 2, 6, GOLD);
    gfx_fillRect(x + 4, y + 18, 28, 20, label);
    gfx_rect(x + 4, y + 18, 28, 20, INK);
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

// "123.4 MB" style, from 512-byte blocks.
static void fmtSize(char *p, uint32_t blocks) {
    uint32_t mb10 = (uint32_t)(((uint64_t)blocks * 512 * 10) >> 20);
    if (mb10 >= 10240) {                                   // >= 1 GB: show GB
        uint32_t gb10 = mb10 / 1024;
        p = fmtU(p, gb10 / 10); *p++ = '.'; p = fmtU(p, gb10 % 10);
        put(p, " GB");
        return;
    }
    p = fmtU(p, mb10 / 10); *p++ = '.'; p = fmtU(p, mb10 % 10);
    put(p, " MB");
}

static void drawUi(uint32_t now) {
    using namespace usbmsc;
    gfx_clear(BG);
    gfx_fillRect(0, 0, 128, 14, NAVY);
#ifdef CHSD_TEST
    centred(3, "SD READER - TEST", GOLD);
#else
    centred(3, "SD CARD READER", GOLD);
#endif

    State s = state();
    const char *msg;
    uint8_t body = DIM, label = SILVER;
    bool blink = (now / 150) & 1;
    if (safeMode)                  { msg = "SAFE MODE";        body = DIM; }
    else if (!cardOk)              { msg = "NO CARD";          body = DIM; label = DIM; }
    else if (s == READING)         { msg = "READING";          body = CYAN; label = blink ? WHITE : CYAN; }
    else if (s == WRITING)         { msg = "WRITING";          body = RED; label = blink ? WHITE : RED; }
    else if (s == CONFIGURED)      { msg = "CONNECTED";        body = GREEN; }
    else if (s == EJECTED)         { msg = "EJECTED";          body = GOLD; }
    else                           { msg = "WAITING FOR PC";   body = BLUE; }
    sdIcon(46, 20, body, label);
    char num[12];
    if (rdRetries + wrRetries) {                           // blocks sent or read again
        gfx_text(4, 24, "RETRY", SILVER);
        fmtU(num, rdRetries + wrRetries);
        gfx_text(4, 33, num, ORANGE);
    }
    if (rdFails + wrFails) {                               // ... and given up on
        gfx_text(4, 46, "FAIL", SILVER);
        fmtU(num, rdFails + wrFails);
        gfx_text(4, 55, num, RED);
    }
    centred(70, msg, s == WRITING && !safeMode && cardOk ? RED : WHITE, gfx_textWidthScaled(msg, 2) <= 124 ? 2 : 1);

    char buf[24];
    if (safeMode) {
        centred(90, "USB serial only.", SILVER);
        centred(100, "Reset without B", SILVER);
        centred(110, "to be a drive.", SILVER);
        return;
    }
    if (!cardOk) {
        centred(88, "Insert a card", SILVER);
    } else if (s == EJECTED) {
        centred(88, "Press A to reconnect", SILVER);
    } else {
        fmtSize(buf, cardBlocks);
        gfx_text(4, 88, "CARD", SILVER);
        gfx_text(124 - gfx_textWidth(buf), 88, buf, WHITE);
    }
    fmtSize(buf, blocksRead);
    gfx_text(4, 97, "READ", SILVER);
    gfx_text(124 - gfx_textWidth(buf), 97, buf, CYAN);
    fmtSize(buf, blocksWritten);
    gfx_text(4, 106, "WRITTEN", SILVER);
    gfx_text(124 - gfx_textWidth(buf), 106, buf, ORANGE);
    char *p = fmtU(buf, rateKBs);
    put(p, " KB/s");
    gfx_text(4, 115, readOnly() ? "READ-ONLY" : "SPEED", readOnly() ? GOLD : SILVER);
    gfx_text(124 - gfx_textWidth(buf), 115, buf, WHITE);
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

static void command(int c, uint32_t now) {
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

// ---- Arduino ----------------------------------------------------------------
void setup() {
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
    pinMode(PIN_BTN_START, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);
    gfx_begin(GFX_DIV2, GFX_12BPP);
    gfx_setPalette(PALETTE, 16);
    safeMode = pressed(PIN_BTN_B);
    initCard();
    if (!safeMode) usbmsc::begin(DEV);
    drawUi(millis());
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

    // Throughput over the last second.
    if (now - lastRate >= 1000) {
        uint32_t b = usbmsc::blocksRead + usbmsc::blocksWritten;
        rateKBs = (b - lastBlocks) / 2;
        lastBlocks = b;
        lastRate = now;
    }

    if (usbmsc::busy()) return;                  // the card holds SPI mid-command

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
            if (!cardPresent(now)) { cardOk = false; cardBlocks = 0; usbmsc::mediaChanged(); }
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
        if (start && !prevStart && !safeMode) pressStart();
        prevA = a; prevStart = start;
        if (pressed(PIN_BTN_B)) {
            if (!bDown) { bDown = true; bHeld = now; }
            if (now - bHeld > 1000 && !safeMode) {
                // Back to the SD game menu: with the menu bootloader any reset
                // without a request shows the menu. (Uploads still reach the
                // bootloader through the 1200-baud touch.)
                gfx_clear(BG);
                centred(56, "MENU", GOLD, 2);
                gfx_flush();
                usbmsc::detach();
                delay(300);
                NVIC_SystemReset();
            }
        } else bDown = false;
    }

    // Screen at ~8 Hz, only between SCSI commands (the LCD shares SPI1).
    if (now - lastUi >= 120) {
        lastUi = now;
        drawUi(now);
        gfx_flush();
    }
}
