/*
 * fat.c - read-only FAT16/FAT32 for the bootloader. A C port of CHSd 1.0.0's
 * Fat.cpp (HypeRunner's clean-room reader, MIT: from the Microsoft FAT
 * specification and the MBR layout), with these changes:
 *  - one walk over a directory with a callback, instead of a name lookup per
 *    match (the menu lists a whole folder);
 *  - files are streamed one cluster link at a time instead of through a run
 *    table, so any amount of fragmentation works and no table is needed;
 *  - the volume's extent is checked against 32-bit overflow.
 * The BPB checks and the partition handling are CHSd's.
 */
#include "fat.h"
#include "sd.h"

static struct {
    uint32_t fat;            /* LBA of the first FAT */
    uint32_t data;           /* LBA of cluster 2 */
    uint32_t root;           /* FAT16: LBA of the fixed root region; FAT32: root cluster */
    uint32_t end;            /* one past the highest cluster number; 0 = not mounted */
    uint32_t rootSecs;       /* FAT16 root region sectors; 0 means FAT32 */
    uint32_t shift;          /* log2(sectors per cluster) */
} v;

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static uint32_t u16at(const uint8_t *p) { return *(const uint16_t *)(const void *)p; }   /* p 2-aligned */
static uint32_t u32at(const uint8_t *p) { return *(const uint32_t *)(const void *)p; }   /* p 4-aligned */

static uint32_t clus_lba(uint32_t c) { return v.data + ((c - 2) << v.shift); }

/* FAT entry of cluster c: the next cluster (>= 2), 0 at the end of the chain,
   1 for a free, bad or out-of-range link, or FAT_E_READ. */
static int32_t next(uint32_t c, uint8_t *b)
{
    uint32_t f32 = !v.rootSecs;
    uint32_t off = c << (f32 ? 2 : 1), n;
    if (sd_read(v.fat + (off >> 9), b)) return FAT_E_READ;
    n = f32 ? u32at(b + (off & 511)) & 0x0FFFFFFFu : u16at(b + (off & 511));
    if (n >= (f32 ? 0x0FFFFFF8u : 0xFFF8u)) return 0;
    return n >= 2 && n < v.end ? (int32_t)n : 1;
}

/* Validates the BPB in b (the volume starts at LBA base) and fills v. Only the
   first FAT is read: mirroring is assumed, as every formatter sets it. */
static int bpb(const uint8_t *b, uint32_t base)
{
    uint32_t spc = b[13], sh = 0, nf = b[16];
    uint32_t rsvd = u16at(b + 14), rootSecs = (le16(b + 17) + 15) >> 4;
    uint32_t tot = le16(b + 19), fsz = u16at(b + 22), meta, n;
    while (sh < 8 && (1u << sh) < spc) sh++;
    if (!tot) tot = u32at(b + 32);
    if (!fsz) fsz = u32at(b + 36);
    if (le16(b + 11) != 512 || (1u << sh) != spc || !rsvd || !nf || !fsz) return 0;
    if (nf > 4 || fsz >= (1u << 24)) return 0;      /* keeps nf * fsz and fsz * 256 in range */
    if (base + tot < base) return 0;                /* the volume must fit in 32-bit LBAs */
    meta = rsvd + nf * fsz + rootSecs;
    if (meta >= tot) return 0;
    /* FAT12 (under 4,085 clusters) is refused. FAT32 is told by its empty fixed
       root region. */
    n = (tot - meta) >> sh;
    if (n < 4085) return 0;
    /* The FAT must hold an entry per cluster, and a FAT32 root must be a real
       cluster. */
    if (fsz * (rootSecs ? 256u : 128u) < n + 2) return 0;
    if (!rootSecs && (u32at(b + 44) < 2 || u32at(b + 44) >= n + 2)) return 0;
    v.fat = base + rsvd;
    v.data = base + meta;
    v.root = rootSecs ? v.fat + nf * fsz : u32at(b + 44);
    v.rootSecs = rootSecs;
    v.shift = sh;
    v.end = n + 2;
    return 1;
}

int fat_mount(uint8_t *b)
{
    uint32_t base = 0, pass;
    v.end = 0;
    for (pass = 0;; pass++) {
        if (sd_read(base, b)) return FAT_E_READ;
        if (u16at(b + 510) != 0xAA55) return FAT_E_NOFS;
        if (le32(b + 3) == 0x41465845u) return FAT_E_EXFAT;       /* OEM name "EXFAT   " */
        if ((b[0] == 0xEB || b[0] == 0xE9) && bpb(b, base)) return FAT_OK;
        if (pass) return FAT_E_NOFS;
        /* LBA 0 is not a usable boot sector: read it as an MBR and take the
           first FAT partition. With none, a type 07 one (exFAT, or NTFS) gets
           its own error. */
        uint32_t other = 0;
        for (const uint8_t *p = b + 446; p < b + 510; p += 16) {
            uint32_t t = p[4];
            if (t < 16 && (0x5852u >> t & 1)) { base = le32(p + 8); break; }   /* 01 04 06 0B 0C 0E */
            other |= t == 0x07;
        }
        if (!base) return other ? FAT_E_EXFAT : FAT_E_NOFS;
    }
}

uint32_t fat_entry_cluster(const uint8_t *d)
{
    /* The high word only exists on FAT32 (a fixed root region means FAT16). */
    return u16at(d + 26) | (v.rootSecs ? 0u : u16at(d + 20) << 16);
}

int fat_dir(uint32_t dir, uint8_t *b, fat_dir_cb cb, void *ctx)
{
    uint32_t sec = 0, lba, guard;
    if (!v.end) return FAT_E_NOFS;
    if (!dir && !v.rootSecs) dir = v.root;          /* FAT32 root: an ordinary chain */
    for (guard = 4096; guard--; ) {
        if (!dir) {
            if (sec == v.rootSecs) return FAT_OK;   /* end of the FAT16 root region */
            lba = v.root + sec;
        } else {
            if (sec >> v.shift) {                   /* this cluster is done */
                int32_t n = next(dir, b);
                if (n < 2) return n < 0 ? n : FAT_OK;
                dir = (uint32_t)n;
                sec = 0;
            }
            if (dir < 2 || dir >= v.end) return FAT_OK;
            lba = clus_lba(dir) + sec;
        }
        sec++;
        if (sd_read(lba, b)) return FAT_E_READ;
        for (const uint8_t *d = b; d < b + 512; d += 32) {
            if (!d[0]) return FAT_OK;                    /* end-of-directory mark */
            if (d[0] == 0xE5 || (d[11] & 0x0F) == 0x0F) continue;   /* deleted, long-name part */
            if (cb(d, ctx)) return 1;
        }
    }
    return FAT_OK;
}

void fat_open(fat_stream_t *s, uint32_t clus, uint32_t size)
{
    s->clus = clus;
    s->sec = 0;
    s->left = (size >> 9) + ((size & 511) != 0);
}

uint32_t fat_next_lba(fat_stream_t *s, uint8_t *b)
{
    if (!s->left) return 0;
    if (s->sec >> v.shift) {
        int32_t n = next(s->clus, b);
        if (n < 2) return 0;
        s->clus = (uint32_t)n;
        s->sec = 0;
    }
    if (s->clus < 2 || s->clus >= v.end) return 0;
    s->left--;
    return clus_lba(s->clus) + s->sec++;
}
