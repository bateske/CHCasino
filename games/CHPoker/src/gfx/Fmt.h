// Number formatting without printf: the SDK's snprintf pulls in ~3.5 KB
// (and has no %l). Each call writes at p and returns the new end, so calls
// chain: p = fmtStr(p, "BET "); p = fmtMoney(p, bet);
#pragma once
#include <stdint.h>

char *fmtInt(char *p, int32_t v);
char *fmtMoney(char *p, int32_t v);         // "$1234", "-$5"
char *fmtShort(char *p, int32_t v);         // as fmtMoney, but "$12K" from $10,000
char *fmtStr(char *p, const char *s);
