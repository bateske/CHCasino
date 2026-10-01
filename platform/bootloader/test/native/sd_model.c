#include "sd_model.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static int fd = -1;            /* per boot (process): reopened after each fork */
static char fd_path[512];

static int read_sector(sd_model_t *m, uint32_t lba, uint8_t *dst)
{
    if (fd < 0 || strcmp(fd_path, m->path)) {
        if (fd >= 0) close(fd);
        fd = open(m->path, O_RDONLY);
        snprintf(fd_path, sizeof fd_path, "%s", m->path);
    }
    if (fd < 0) return -1;
    if (pread(fd, dst, 512, (off_t)lba * 512) != 512) memset(dst, 0, 512);
    return 0;
}

void sd_model_insert(sd_model_t *m, int type, const char *path)
{
    struct stat st;
    memset(m, 0, sizeof *m);
    m->type = type;
    m->fail_lba = m->timeout_lba = -1;
    m->read_latency = 3;
    m->acmd41_delay = 3;
    if (path) {
        snprintf(m->path, sizeof m->path, "%s", path);
        if (stat(path, &st) == 0) m->sectors = (uint32_t)(st.st_size / 512);
    }
    sd_model_power(m);
}

void sd_model_power(sd_model_t *m)
{
    m->state = SDS_POWERUP;
    m->clocks_cs_high = 0;
    m->cmd_len = 0;
    m->out_len = m->out_pos = 0;
    m->acmd = 0;
    m->acmd41_left = m->acmd41_delay;
}

static void q(sd_model_t *m, uint8_t b)
{
    if (m->out_len < (int)sizeof m->out) m->out[m->out_len++] = b;
}

static void q32(sd_model_t *m, uint32_t v)
{
    q(m, (uint8_t)(v >> 24)); q(m, (uint8_t)(v >> 16)); q(m, (uint8_t)(v >> 8)); q(m, (uint8_t)v);
}

static void command(sd_model_t *m, uint32_t spi_br)
{
    uint8_t c = m->cmd[0] & 0x3F, crc = m->cmd[5];
    uint32_t arg = ((uint32_t)m->cmd[1] << 24) | ((uint32_t)m->cmd[2] << 16) | ((uint32_t)m->cmd[3] << 8) | m->cmd[4];
    int acmd = m->acmd;
    uint8_t idle = m->state == SDS_READY ? 0x00 : 0x01;

    m->acmd = 0;
    m->cmds++;
    m->out_len = m->out_pos = 0;

    if (m->state == SDS_MULTIREAD) {            /* streaming: only CMD12 is heard */
        if (c != 12) return;
        q(m, 0xFF);                             /* stuff byte */
        q(m, 0x00);
        for (int i = 0; i < 4; i++) q(m, 0x00); /* busy */
        m->state = SDS_READY;
        return;
    }
    if (m->state != SDS_READY && spi_br < 6) m->init_fast++;
    if (m->state == SDS_POWERUP) {
        if (c != 0 || m->clocks_cs_high < 74) return;   /* not yet in SPI mode: silence */
    }
    if ((c == 0 && crc != 0x95) || (c == 8 && crc != 0x87)) { m->bad_crc++; q(m, 0xFF); q(m, idle | 0x08); return; }

    q(m, 0xFF);                                 /* NCR: at least one byte */
    if (acmd && c == 41) {
        if (m->state == SDS_POWERUP) return;
        if (m->acmd41_left > 0) { m->acmd41_left--; q(m, 0x01); }
        else { m->state = SDS_READY; q(m, 0x00); }
        return;
    }
    switch (c) {
    case 0:
        m->state = SDS_IDLE;
        m->acmd41_left = m->acmd41_delay;
        q(m, 0x01);
        break;
    case 8:
        if (m->type == SD_SDSC_V1) { q(m, 0x05); break; }
        q(m, idle); q32(m, arg & 0xFFF);
        break;
    case 55:
        m->acmd = 1; q(m, idle);
        break;
    case 58:
        q(m, idle);
        q32(m, (m->state == SDS_READY ? 0x80000000u : 0) | (m->type == SD_SDHC ? 0x40000000u : 0) | 0x00FF8000u);
        break;
    case 16:
        q(m, idle);
        break;
    case 12:
        q(m, 0xFF); q(m, idle);
        break;
    case 17: {
        uint32_t lba;
        if (m->state != SDS_READY) { q(m, idle | 0x04); break; }
        if (m->type == SD_SDHC) lba = arg;
        else { if (arg & 511) { q(m, 0x20); break; } lba = arg >> 9; }   /* address error */
        if (lba >= m->sectors) { q(m, 0x40); break; }                    /* parameter error */
        m->reads++;
        q(m, 0x00);
        for (uint32_t i = 0; i < m->read_latency; i++) q(m, 0xFF);
        if ((int64_t)lba == m->timeout_lba) return;                      /* token never comes */
        if ((int64_t)lba == m->fail_lba || (m->fail_after_reads && m->reads > m->fail_after_reads)) { q(m, 0x08); break; }
        q(m, 0xFE);
        read_sector(m, lba, m->out + m->out_len);
        m->out_len += 512;
        q(m, 0x12); q(m, 0x34);                 /* CRC16 (not checked) */
        break;
    }
    default:
        q(m, idle | 0x04);                      /* illegal command */
        break;
    }
}

uint8_t sd_model_xfer(sd_model_t *m, uint8_t in, int cs_low, uint32_t spi_br)
{
    uint8_t out = 0xFF;

    if (m->type == SD_NONE) return 0xFF;
    if (!cs_low) {
        if (m->clocks_cs_high < 1000) m->clocks_cs_high += 8;
        return 0xFF;                            /* DO released: the pull-up reads 1s */
    }
    switch (m->state) {
    case SDS_MULTIREAD: {
        /* A CMD18 left running by the reset: the card streams data blocks
           whatever arrives on DI, until it sees CMD12. */
        static uint32_t pos;
        out = (pos % 515) == 0 ? 0xFE : (uint8_t)(pos * 7);
        pos++;
        break;
    }
    case SDS_WRITEWAIT:
        /* A CMD25 left waiting for its next data token: everything that is
           not a token is ignored. 0xFD (stop transmission) ends it. */
        if (in == 0xFD) { m->state = SDS_BUSY; m->busy_left = 40; }
        return 0xFF;
    case SDS_BUSY:
        if (m->busy_left) { m->busy_left--; return 0x00; }
        m->state = SDS_READY;
        return 0xFF;
    default:
        if (m->out_pos < m->out_len) out = m->out[m->out_pos++];
        break;
    }
    if (m->cmd_len == 0) {
        if ((in & 0xC0) == 0x40) m->cmd[m->cmd_len++] = in;
    } else {
        m->cmd[m->cmd_len++] = in;
        if (m->cmd_len == 6) {
            m->cmd_len = 0;
            command(m, spi_br);
        }
    }
    return out;
}
