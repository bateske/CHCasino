#include "testlib.h"
#include "proto.h"
#include "crc16.h"

int t_fail, t_pass;
const char *t_name;

uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void frame_push(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t hdr[4] = { PROTO_VERSION, cmd, (uint8_t)len, (uint8_t)(len >> 8) };
    uint16_t crc = crc16_update(0xFFFF, hdr, 4);
    if (len) crc = crc16_update(crc, payload, len);
    if (B->rx_len + len + 8u > sizeof B->rx) { fprintf(stderr, "rx queue full\n"); abort(); }
    uint8_t *q = B->rx + B->rx_len;
    *q++ = PROTO_SOF0; *q++ = PROTO_SOF1;
    memcpy(q, hdr, 4); q += 4;
    if (len) { memcpy(q, payload, len); q += len; }
    *q++ = (uint8_t)crc; *q++ = (uint8_t)(crc >> 8);
    B->rx_len = (uint32_t)(q - B->rx);
}

static uint32_t tx_rd;
void frame_reset(void) { tx_rd = 0; }
int frame_pop(uint8_t *cmd, uint8_t *payload, uint16_t *len)
{
    while (tx_rd + 8 <= B->tx_len) {
        uint8_t *p = B->tx + tx_rd;
        if (p[0] != PROTO_SOF0 || p[1] != PROTO_SOF1) { tx_rd++; continue; }
        uint16_t n = (uint16_t)(p[4] | p[5] << 8);
        if (tx_rd + 8u + n > B->tx_len) return 0;
        *cmd = p[3];
        if (payload) memcpy(payload, p + 6, n);
        if (len) *len = n;
        tx_rd += 8u + n;
        return 1;
    }
    return 0;
}

void push_upload(const uint8_t *img, uint32_t len, int run)
{
    uint8_t b[8 + 256];
    put32(b, len); put32(b + 4, host_crc32(img, len));
    frame_push(CMD_BEGIN, b, 8);
    for (uint32_t off = 0; off < len; off += 256) {
        uint32_t n = len - off < 256 ? len - off : 256;
        put32(b, off);
        memcpy(b + 4, img + off, n);
        frame_push(CMD_WRITE, b, (uint16_t)(4 + n));
    }
    frame_push(CMD_END, NULL, 0);
    if (run) frame_push(CMD_RUN, NULL, 0);
}

void make_image(uint8_t *img, uint32_t len, uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 1;
    for (uint32_t i = 0; i < len; i++) { x = x * 1103515245u + 12345u; img[i] = (uint8_t)(x >> 16); }
    /* WCH-startup shape: j handle_reset, vector[0] = _start, vector[1] = 0 */
    if (len >= 12) { put32(img, 0x0000006Fu | 0x100u << 20); put32(img + 4, CHGAME_APP_START); put32(img + 8, 0); }
}

int app_valid_now(void)
{
    const uint8_t *m = B->flash + CHGAME_META_ADDR;
    uint32_t len = le32(m + 8);
    if (le32(m) != CHGAME_META_MAGIC || le32(m + 4) != CHGAME_META_VERSION) return 0;
    if (!len || len > CHGAME_APP_MAX_SIZE || (len & 3)) return 0;
    if (host_crc32(B->flash + CHGAME_APP_START, len) != le32(m + 12)) return 0;
    return le32(B->flash + CHGAME_APP_START + 8) != CHGAME_BOOT_SIG;
}

int test_summary(void)
{
    printf("%d passed, %d failed\n", t_pass, t_fail);
    return t_fail ? 1 : 0;
}
