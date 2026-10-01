/* SPI-mode SD card model (SD Physical Layer Simplified Spec, ch. 7), detailed
 * enough to exercise the bootloader's driver: power-up clocks, CMD0/8/55/41/
 * 58/16/17/12, SDSC byte vs SDHC block addressing, slow initialisation, read
 * latency, error tokens, read timeouts, and a card left mid-transfer by an
 * MCU reset (a CMD18 stream or a CMD25 waiting for data). The card's data is
 * an image file read on demand. It lives in shared memory: like the real
 * card, it keeps its state across an MCU reset. */
#ifndef SD_MODEL_H
#define SD_MODEL_H
#include <stdint.h>

enum { SD_NONE = 0, SD_SDSC_V1, SD_SDSC_V2, SD_SDHC };
enum { SDS_POWERUP = 0, SDS_IDLE, SDS_READY, SDS_MULTIREAD, SDS_WRITEWAIT, SDS_BUSY };

typedef struct {
    int      type;
    char     path[512];
    uint32_t sectors;
    int      state;
    uint32_t clocks_cs_high;
    uint8_t  cmd[6];
    int      cmd_len;
    uint8_t  out[1024];
    int      out_len, out_pos;
    int      acmd;
    int      acmd41_left;       /* ACMD41s answered "idle" before "ready" */
    int      acmd41_delay;      /* the value acmd41_left is reset to by CMD0 */
    uint32_t read_latency;      /* 0xFF bytes between R1 and the data token */
    int64_t  fail_lba;          /* this LBA answers with an error token */
    int64_t  timeout_lba;       /* this LBA never sends a token */
    uint32_t fail_after_reads;  /* every read after this many fails (0 = never) */
    uint32_t busy_left;
    uint32_t multi_lba;
    uint32_t reads;
    uint32_t init_fast;         /* identification commands seen above 400 kHz */
    uint32_t bad_crc;           /* CMD0/CMD8 sent with a wrong CRC */
    uint32_t cmds;
} sd_model_t;

void    sd_model_insert(sd_model_t *m, int type, const char *path);
void    sd_model_power(sd_model_t *m);           /* power cycle: back to POWERUP */
uint8_t sd_model_xfer(sd_model_t *m, uint8_t in, int cs_low, uint32_t spi_br);

#endif
