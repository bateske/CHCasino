/* Minimal test helpers for the native suites. */
#ifndef TESTLIB_H
#define TESTLIB_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "host.h"

typedef struct { int fail, pass; } t_counts_t;
extern t_counts_t *t_counts;
#define t_fail (t_counts->fail)
#define t_pass (t_counts->pass)
extern const char *t_name;

#define CHECK(cond, ...) do { \
    if (cond) t_pass++; \
    else { t_fail++; fprintf(stderr, "FAIL %s (%s:%d): ", t_name, __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

void frame_reset(void);
/* Each test runs in its own process, so code that keeps state in statics
   (as the bootloader does, from a zeroed .bss) starts fresh every time, as
   it does after a real reset. */
void t_run(const char *name, void (*fn)(void));
#define TEST(fn) t_run(#fn, fn)

/* Protocol frames (CRC-16/CCITT-FALSE over ver|cmd|len|payload). */
void frame_push(uint8_t cmd, const uint8_t *payload, uint16_t len);
int  frame_pop(uint8_t *cmd, uint8_t *payload, uint16_t *len);   /* next response */
void push_upload(const uint8_t *img, uint32_t len, int run);      /* BEGIN/WRITE.../END[/RUN] */
uint32_t le32(const uint8_t *p);
void put32(uint8_t *p, uint32_t v);
void make_image(uint8_t *img, uint32_t len, uint32_t seed);       /* app-looking bytes */
int  app_valid_now(void);                                         /* metadata + CRC check on B->flash */
int  test_summary(void);

#endif
