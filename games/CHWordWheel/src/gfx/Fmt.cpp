#pragma GCC optimize("Os", "no-ipa-sra", "no-inline-functions-called-once", "no-jump-tables", "no-guess-branch-probability")
#include "Fmt.h"

char *fmtInt(char *p, int32_t v) {
    uint32_t u = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
    if (v < 0) *p++ = '-';
    char tmp[10];
    int n = 0;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

char *fmtMoney(char *p, int32_t v) {
    if (v < 0) { *p++ = '-'; v = -v; }
    *p++ = '$';
    return fmtInt(p, v);
}

char *fmtStr(char *p, const char *s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

// "$1,850": money with thousands separators.
char *fmtCash(char *p, int32_t v) {
    char tmp[12];
    char *e = fmtInt(tmp, v < 0 ? -v : v);
    int n = (int)(e - tmp);
    if (v < 0) *p++ = '-';
    *p++ = '$';
    for (int i = 0; i < n; i++) {
        *p++ = tmp[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) *p++ = ',';
    }
    *p = 0;
    return p;
}
