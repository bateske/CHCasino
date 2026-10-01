/* SD driver, FAT reader and package header checks against the SD card model
 * and FAT images built by run_tests.py (CHSd's fatimg.py). Reads a case list
 * from argv[1]; see run_tests.py for the format. */
#include "testlib.h"
#include "hal.h"
#include "sd.h"
#include "fat.h"
#include "chg.h"
#include <ctype.h>

static uint8_t buf[512] __attribute__((aligned(4)));
static uint8_t data[1 << 20];

typedef struct { const char *name11; uint32_t clus, size; int found; int count; uint8_t want; } find_t;

static int find_cb(const uint8_t *d, void *ctx)
{
    find_t *f = ctx;
    if (f->name11) {
        /* a folder for GAMES, a plain file otherwise: never a volume label (the
           images carry decoy labels spelling real names) */
        if ((d[11] & (FAT_ATTR_DIR | FAT_ATTR_LABEL)) != f->want) return 0;
        if (!memcmp(d, f->name11, 11)) { f->clus = fat_entry_cluster(d); f->size = fat_entry_size(d); f->found = 1; return 1; }
        return 0;
    }
    /* count packages: files named *.CHG, not folders, labels, hidden or system entries */
    if (!(d[11] & (FAT_ATTR_DIR | FAT_ATTR_LABEL | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM)) && !memcmp(d + 8, "CHG", 3))
        f->count++;
    return 0;
}

static void to83(const char *name, char out[11])
{
    memset(out, ' ', 11);
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) out[j++] = (char)toupper((unsigned char)name[i++]);
    if (name[i] == '.') { i++; j = 8; while (name[i] && j < 11) out[j++] = (char)toupper((unsigned char)name[i++]); }
}

static int games_dir(uint32_t *clus)
{
    find_t f = { "GAMES      ", 0, 0, 0, 0, FAT_ATTR_DIR };
    if (fat_dir(0, buf, find_cb, &f) < 0 || !f.found) return -1;
    *clus = f.clus;
    return 0;
}

static int lookup(const char *name, uint32_t *clus, uint32_t *size)
{
    char n11[11];
    uint32_t g;
    to83(name, n11);
    if (games_dir(&g)) return -1;
    find_t f = { n11, 0, 0, 0, 0, 0 };
    if (fat_dir(g, buf, find_cb, &f) < 0 || !f.found) return -1;
    *clus = f.clus; *size = f.size;
    return 0;
}

/* Streams a whole file; returns its length read, or -1 on a broken chain or read error. */
static long stream(uint32_t clus, uint32_t size)
{
    fat_stream_t s;
    uint32_t got = 0;
    fat_open(&s, clus, size);
    while (got < size) {
        uint32_t lba = fat_next_lba(&s, buf);
        if (!lba || sd_read(lba, buf)) return -1;
        uint32_t n = size - got < 512 ? size - got : 512;
        if (got + n <= sizeof data) memcpy(data + got, buf, n);
        got += n;
    }
    return (long)got;
}

int main(int argc, char **argv)
{
    char line[1024], name[256] = "?";
    FILE *f = fopen(argv[1], "r");
    int cases = 0;
    if (!f) { perror(argv[1]); return 2; }
    host_init();
    while (fgets(line, sizeof line, f)) {
        char a[256] = "", b[512] = "", c[512] = "";
        long n1 = 0;
        unsigned long u = 0;
        int k = sscanf(line, "%255s %511s %511s", a, b, c);
        if (k < 1 || a[0] == '#') continue;
        if (!strcmp(a, "case")) {
            snprintf(name, sizeof name, "%s", b);
            t_name = name;
            host_init();
            hal_pins_init();
            cases++;
        } else if (!strcmp(a, "card")) {
            int type = !strcmp(b, "sdsc1") ? SD_SDSC_V1 : !strcmp(b, "sdsc2") ? SD_SDSC_V2 : !strcmp(b, "sdhc") ? SD_SDHC : SD_NONE;
            sd_model_insert(&B->sd, type, k > 2 ? c : NULL);
        } else if (!strcmp(a, "set")) {
            n1 = strtol(c, NULL, 0);
            if (!strcmp(b, "latency")) B->sd.read_latency = (uint32_t)n1;
            else if (!strcmp(b, "timeout_lba")) B->sd.timeout_lba = n1;
            else if (!strcmp(b, "fail_lba")) B->sd.fail_lba = n1;
            else if (!strcmp(b, "fail_after")) B->sd.fail_after_reads = (uint32_t)n1;
            else if (!strcmp(b, "acmd41")) B->sd.acmd41_left = B->sd.acmd41_delay = (int)n1;
            else if (!strcmp(b, "state")) {
                /* the card as a program left it: initialised, then cut off mid-transfer */
                B->sd.state = !strcmp(c, "multiread") ? SDS_MULTIREAD : SDS_WRITEWAIT;
            }
        } else if (!strcmp(a, "init")) {
            uint64_t t0 = B->now_us;
            int rc = sd_init();
            CHECK((rc == 0) == !strcmp(b, "OK"), "sd_init %d, expected %s", rc, b);
            CHECK(B->sd.init_fast == 0, "identification above 400 kHz");
            CHECK(B->sd.bad_crc == 0, "CMD0/CMD8 CRC");
            CHECK(B->bus_conflicts == 0, "both chip selects low");
            if (k > 2) CHECK((long)((B->now_us - t0) / 1000) <= strtol(c, NULL, 0), "init took %llu ms (limit %s)",
                             (unsigned long long)((B->now_us - t0) / 1000), c);
        } else if (!strcmp(a, "mount")) {
            int rc = fat_mount(buf);
            CHECK(rc == atoi(b), "fat_mount %d, expected %s", rc, b);
        } else if (!strcmp(a, "games")) {
            uint32_t g;
            if (!strcmp(b, "MISSING")) { CHECK(games_dir(&g) != 0, "GAMES should be missing"); continue; }
            find_t fc = { NULL, 0, 0, 0, 0, 0 };
            CHECK(games_dir(&g) == 0, "GAMES folder found");
            fat_dir(g, buf, find_cb, &fc);
            CHECK(fc.count == atoi(b), "%d packages listed, expected %s", fc.count, b);
        } else if (!strcmp(a, "file")) {
            uint32_t clus, size;
            sscanf(line, "%*s %*s %ld %lx", &n1, &u);
            CHECK(lookup(b, &clus, &size) == 0 && size == (uint32_t)n1, "%s found with size %ld", b, n1);
            long got = stream(clus, size);
            CHECK(got == n1 && host_crc32(data, (size_t)got) == u, "%s streamed intact (%ld B)", b, got);
        } else if (!strcmp(a, "filebad")) {
            uint32_t clus, size;
            CHECK(lookup(b, &clus, &size) == 0, "%s found", b);
            CHECK(stream(clus, size) < 0, "%s: broken chain detected", b);
        } else if (!strcmp(a, "chg")) {
            uint32_t clus, size, pl, crc;
            fat_stream_t s;
            CHECK(lookup(b, &clus, &size) == 0, "%s found", b);
            fat_open(&s, clus, size);
            uint32_t lba = fat_next_lba(&s, buf);
            int rc = lba && !sd_read(lba, buf) ? chg_check(buf, size, &pl, &crc) : -1;
            CHECK(rc == atoi(c), "%s: chg_check %d, expected %s", b, rc, c);
        } else if (!strcmp(a, "end")) {
        } else {
            fprintf(stderr, "unknown spec line: %s", line);
            return 2;
        }
    }
    printf("%d cases: ", cases);
    return test_summary();
}
