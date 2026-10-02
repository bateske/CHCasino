// The simulator's SD card: src/sd/Sd2Card's interface over blocks held in
// memory (loaded from pcsession.py's card.bin; the rest of the card reads
// as zeros). Every call that would use SPI waits for a panel flush in
// flight first, as the real driver does.
#include <Arduino.h>
#include <CHGfx.h>
#include <map>
#include <array>
#include "sim.h"
#include "simpc.h"
#include "src/sd/Sd2Card.h"

static std::map<uint32_t, std::array<uint8_t, 512>> s_blocks;
static uint32_t s_size = 0;
static bool s_in = false;
static uint32_t s_rd, s_wr, s_bad = 0xFFFFFFFF;
static const uint8_t *s_src;

void card_load(const char *path, uint32_t blocks) {
    s_size = blocks;
    s_in = blocks != 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t rec[516];
    while (fread(rec, 1, sizeof rec, f) == sizeof rec) {
        uint32_t lba = rec[0] | (rec[1] << 8) | (rec[2] << 16) | ((uint32_t)rec[3] << 24);
        std::array<uint8_t, 512> b;
        memcpy(b.data(), rec + 4, 512);
        s_blocks[lba] = b;
    }
    fclose(f);
}
void card_present(bool in) { s_in = in && s_size; }
void card_badBlock(uint32_t lba) { s_bad = lba; }

static void get(uint32_t lba, uint8_t *dst) {
    auto it = s_blocks.find(lba);
    if (it == s_blocks.end()) memset(dst, 0, 512);
    else memcpy(dst, it->second.data(), 512);
}

uint8_t Sd2Card::init(uint8_t, uint8_t, unsigned int) {
    gfx_wait();
    sim_advance(s_in ? 30000 : 50000);
    if (!s_in) { errorCode_ = SD_CARD_ERROR_CMD0; return 0; }
    errorCode_ = 0;
    type_ = SD_CARD_TYPE_SDHC;
    return 1;
}
uint8_t Sd2Card::crcOn(uint8_t) { return 1; }
uint32_t Sd2Card::cardSize(void) { return s_in ? s_size : 0; }
uint8_t Sd2Card::present(void) { gfx_wait(); return s_in; }

uint8_t Sd2Card::readStart(uint32_t block) {
    gfx_wait();
    s_rd = block;
    return s_in;
}
uint8_t Sd2Card::readBlockChecked(uint8_t *dst) {
    if (!s_in) return 0;
    if (s_rd == s_bad) { s_bad = 0xFFFFFFFF; return 0; }      // a CRC mismatch, once
    get(s_rd++, dst);
    return 1;
}
uint8_t Sd2Card::readStop(void) { return 1; }
uint8_t Sd2Card::readBlock(uint32_t block, uint8_t *dst) {
    gfx_wait();
    sim_advance(600);
    if (!s_in) return 0;
    get(block, dst);
    return 1;
}
uint8_t Sd2Card::writeStart(uint32_t block, uint32_t) {
    gfx_wait();
    s_wr = block;
    return s_in;
}
uint8_t Sd2Card::writeDataStart(const uint8_t *src) { s_src = src; return s_in; }
uint8_t Sd2Card::writeDataEnd(uint16_t) {
    if (!s_in) return 0;
    std::array<uint8_t, 512> b;
    memcpy(b.data(), s_src, 512);
    s_blocks[s_wr++] = b;
    return 1;
}
uint8_t Sd2Card::writeStop(void) { return 1; }

uint16_t Sd2Card::crc16(const uint8_t *p, uint16_t n, uint16_t crc) {
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) crc = (uint16_t)(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

// CID (CMD10) of a made-up 16 GB SanDisk card; CSD is not used.
uint8_t Sd2Card::readRegister(uint8_t cmd, void *buf) {
    gfx_wait();
    static const uint8_t CID[16] = {0x03, 'S', 'D', 'S', 'C', '1', '6', 'G', 0x80,
                                    0x1A, 0x2B, 0x3C, 0x4D, 0x01, 0x54, 0x01};
    memset(buf, 0, 16);
    if (cmd == 10) memcpy(buf, CID, 16);
    return s_in;
}
