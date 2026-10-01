/* The host side of src/hal.h, sys.h, flash.h, jump.h, usb.h and spin.h, plus
 * the fork-per-boot runner. See host.h. */
#define _GNU_SOURCE
#include "host.h"
#include "hal.h"
#include "sys.h"
#include "flash.h"
#include "jump.h"
#include "usb.h"
#include "spin.h"
#include "proto.h"
#include "boot.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

host_board_t *B;
static int in_child;
static uint64_t boot_start_us;
static uint64_t ns_acc;
jmp_buf host_jmp;               /* in-process unit tests catch reset/jump here */
int host_jmp_armed;

static void finish(int how) __attribute__((noreturn));
static void finish(int how)
{
    if (in_child) {
        fflush(stdout); fflush(stderr);
        _exit(how);
    }
    if (host_jmp_armed) longjmp(host_jmp, how);
    fprintf(stderr, "host: boot ended (%d) outside a boot\n", how);
    abort();
}

void host_log(const char *fmt, ...)
{
    va_list ap;
    char line[512];
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (getenv("CHBOOT_VERBOSE")) fprintf(stderr, "[%8.3f ms] %s\n", B->now_us / 1000.0, line);
    if (B->log_len + (uint32_t)n + 2 < HOST_MAX_LOG) {
        memcpy(B->log + B->log_len, line, (size_t)n);
        B->log_len += (uint32_t)n;
        B->log[B->log_len++] = '\n';
        B->log[B->log_len] = 0;
    }
}

void host_advance_us(uint64_t us)
{
    B->now_us += us;
    if (in_child && B->limit_us && B->now_us - boot_start_us > B->limit_us)
        finish(END_HANG);
}

/* ---- board ---------------------------------------------------------------- */

void host_init(void)
{
    if (!B) {
        B = mmap(NULL, sizeof *B, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (B == MAP_FAILED) { perror("mmap"); exit(1); }
    }
    memset(B, 0, sizeof *B);
    memset(B->flash, 0xFF, sizeof B->flash);
    B->limit_us = 20000000;     /* 20 s of virtual time per boot */
    B->sd_cs = B->lcd_cs = B->lcd_rst = 1;
    sd_model_insert(&B->sd, SD_NONE, NULL);
    lcd_model_power(&B->lcd);
    host_power_cycle();
}

void host_power_cycle(void)
{
    /* SRAM comes up as noise; a forged request is astronomically unlikely
       but the noise must at least not look like a valid one. */
    B->retained[0] = 0x5A5AA5A5u ^ (uint32_t)B->now_us;
    B->retained[1] = 0x12345678u;
    B->retained[2] = 0xDEADBEEFu;
    B->retained[3] = 0x0BADF00Du;
    sd_model_power(&B->sd);
    lcd_model_power(&B->lcd);
    B->usb_up = 0;
}

void host_keys(uint64_t at_ms, uint32_t mask)
{
    if (B->nkeys < HOST_MAX_KEYS) {
        B->keys[B->nkeys].at_us = B->now_us + at_ms * 1000u;
        B->keys[B->nkeys].mask = mask;
        B->nkeys++;
    }
}

void host_keys_clear(void) { B->nkeys = 0; }

int host_boot(void)
{
    fflush(stdout); fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {
        in_child = 1;
        boot_start_us = B->now_us;
        B->pins_inited = 0;
        B->usb_up = 0;
        boot_main();
        _exit(END_CRASH);
    }
    int st;
    waitpid(pid, &st, 0);
    int end = WIFEXITED(st) ? WEXITSTATUS(st) : END_CRASH;
    if (end < END_RESET || end > END_CRASH) end = END_CRASH;
    B->end = end;
    if (end == END_JUMP) B->jumps++;
    if (end == END_POWERCUT) host_power_cycle();
    /* After any reset the pins float: the panel's RST line drifts low
       within about a second (no pull-up on the board). */
    B->sd_cs = B->lcd_cs = 1;
    return end;
}

int host_run(int max_boots)
{
    int end = END_RESET;
    while (max_boots-- > 0) {
        end = host_boot();
        if (end != END_RESET) break;
    }
    return end;
}

/* ---- CRC (zlib) and app images --------------------------------------------- */

uint32_t host_crc32(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

void host_install_app(const uint8_t *img, uint32_t len)
{
    uint32_t m[8];
    memset(B->flash + CHGAME_APP_START, 0xFF, CHGAME_META_ADDR + CHGAME_PAGE_SIZE - CHGAME_APP_START);
    memcpy(B->flash + CHGAME_APP_START, img, len);
    m[0] = CHGAME_META_MAGIC; m[1] = CHGAME_META_VERSION; m[2] = len;
    m[3] = host_crc32(img, len); m[4] = 0; m[5] = m[6] = m[7] = 0xFFFFFFFFu;
    memcpy(B->flash + CHGAME_META_ADDR, m, sizeof m);
}

/* ---- sys.h / spin.h ---------------------------------------------------------- */

void sys_init(void) { }
uint32_t sys_ticks(void) { host_advance_us(1); return (uint32_t)(B->now_us * (SYS_TICKS_PER_MS / 1000u)); }
uint32_t sys_millis(void) { return (uint32_t)(B->now_us / 1000u); }
void sys_delay_ms(uint32_t ms) { host_advance_us((uint64_t)ms * 1000u); }
uint32_t sys_counts_down(void) { return 0; }
void spin_ms(uint32_t ms) { host_advance_us((uint64_t)ms * 1000u); }

/* ---- flash.h ------------------------------------------------------------------ */

void ramfunc_init(void) { }

/* One erase or program operation. A cut lands in the middle of it: half the
   page is done, the rest untouched, and the power is gone. */
static void flash_op(uint32_t addr, const uint8_t *data, int erase)
{
    uint32_t n = CHGAME_PAGE_SIZE;
    B->flash_ops++;
    if (addr < CHGAME_APP_START) B->boot_region_writes++;
    if (B->cut_at_op && B->flash_ops == B->cut_at_op) n = CHGAME_PAGE_SIZE / 2;
    for (uint32_t i = 0; i < n; i++)
        B->flash[addr + i] = erase ? 0xFF : (uint8_t)(B->flash[addr + i] & data[i]);
    if (erase) B->erase_count[addr / CHGAME_PAGE_SIZE]++;
    if (n != CHGAME_PAGE_SIZE) {
        host_log("power cut during %s of 0x%04x (op %u)", erase ? "erase" : "program", addr, B->flash_ops);
        finish(END_POWERCUT);
    }
    host_advance_us(erase ? 2500 : 1500);
}

int flash_erase_page(uint32_t addr, flash_region_t region)
{
    if (addr % CHGAME_PAGE_SIZE) return FLASH_ERR_ALIGN;
    if (!range_ok(addr, CHGAME_PAGE_SIZE, region)) return FLASH_ERR_RANGE;
    flash_op(addr, NULL, 1);
    return FLASH_OK;
}

static int write_page(uint32_t addr, const uint8_t *data, flash_region_t region, int erase)
{
    if (addr % CHGAME_PAGE_SIZE) return FLASH_ERR_ALIGN;
    if (!range_ok(addr, CHGAME_PAGE_SIZE, region)) return FLASH_ERR_RANGE;
    if (erase) flash_op(addr, NULL, 1);
    flash_op(addr, data, 0);
    return memcmp(B->flash + addr, data, CHGAME_PAGE_SIZE) ? FLASH_ERR_VERIFY : FLASH_OK;
}

int flash_write_page(uint32_t addr, const uint8_t *data, flash_region_t region)   { return write_page(addr, data, region, 1); }
int flash_program_page(uint32_t addr, const uint8_t *data, flash_region_t region) { return write_page(addr, data, region, 0); }

void flash_selfupdate(uint32_t src, uint32_t len)
{
    B->selfupdate_calls++;
    memset(B->flash, 0xFF, CHGAME_BOOT_SIZE);
    memcpy(B->flash, B->flash + src, len);
    finish(END_RESET);
}

/* ---- jump.h --------------------------------------------------------------------- */

void jump_to_app(void)
{
    host_log("jump to application");
    finish(END_JUMP);
}

/* ---- hal.h ------------------------------------------------------------------------ */

void hal_pins_init(void)
{
    B->pins_inited++;
    B->sd_cs = B->lcd_cs = 1;
    B->lcd_rst = 1;
    lcd_model_rst(&B->lcd, 1, B->now_us);
}

void hal_spi_speed(uint32_t br) { B->spi_br = br; }

uint8_t hal_spi_xfer(uint8_t b)
{
    uint8_t r;
    /* 8 bits at 48 MHz / 2^(BR+1) */
    ns_acc += 8000ull * (2u << B->spi_br) / 48u;
    if (ns_acc >= 1000) { host_advance_us(ns_acc / 1000); ns_acc %= 1000; }
    if (!B->sd_cs && !B->lcd_cs) B->bus_conflicts++;
    r = sd_model_xfer(&B->sd, b, !B->sd_cs, B->spi_br);
    if (!B->lcd_cs) lcd_model_byte(&B->lcd, b, B->lcd_dc, B->now_us);
    return B->sd_cs ? 0xFF : r;
}

void hal_sd_select(int on)  { B->sd_cs = !on; }
void hal_lcd_select(int on) { B->lcd_cs = !on; }
void hal_lcd_dc(int data)   { B->lcd_dc = (uint8_t)!!data; }
void hal_lcd_rst(int high)  { B->lcd_rst = (uint8_t)!!high; lcd_model_rst(&B->lcd, high, B->now_us); }
void hal_led(int on)        { B->led = (uint8_t)!!on; }

uint32_t hal_buttons(void)
{
    uint32_t m = 0;
    for (int i = 0; i < B->nkeys; i++)
        if (B->keys[i].at_us <= B->now_us) m = B->keys[i].mask;
    return m & BTN_ALL;
}

uint32_t hal_uid(uint32_t word) { return 0x11223344u * (word + 1); }

const uint8_t *hal_flash(uint32_t addr)
{
    if (addr >= CHGAME_FLASH_SIZE) { fprintf(stderr, "host: flash read at 0x%x\n", addr); abort(); }
    return B->flash + addr;
}

void hal_reset(void)
{
    finish(END_RESET);
}

volatile chgame_bootreq_t *host_retained(void) { return (volatile chgame_bootreq_t *)B->retained; }

/* ---- usb.h ----------------------------------------------------------------------- */

void usb_start(void) { B->usb_up = 1; }
void proto_shutdown(void) { B->usb_up = 0; }
int16_t CDC_read_nb(void) { return B->rx_pos < B->rx_len ? B->rx[B->rx_pos++] : -1; }
uint8_t CDC_write_nb(char c) { if (B->tx_len < sizeof B->tx) B->tx[B->tx_len++] = (uint8_t)c; return 1; }
void CDC_flush(void) { }
