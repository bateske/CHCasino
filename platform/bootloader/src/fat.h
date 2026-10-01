#ifndef CHBOOT_FAT_H
#define CHBOOT_FAT_H
#include <stdint.h>

/* Read-only FAT16/FAT32 on an SD card: one volume (MBR-partitioned or a
 * superfloppy), 512-byte sectors, 8.3 names. Every call borrows a 512-byte,
 * 4-byte-aligned buffer and leaves it overwritten. */

#define FAT_OK          0
#define FAT_E_READ    (-1)
#define FAT_E_NOFS    (-2)     /* no usable FAT16/FAT32 volume */
#define FAT_E_EXFAT   (-3)     /* exFAT (or NTFS): needs reformatting as FAT32 */

#define FAT_ATTR_HIDDEN  0x02
#define FAT_ATTR_SYSTEM  0x04
#define FAT_ATTR_LABEL   0x08
#define FAT_ATTR_DIR     0x10

int fat_mount(uint8_t *b);

/* Calls cb for every live entry of a directory (deleted entries and long-name
 * parts skipped), with d pointing at the 32-byte entry inside b. dir is a
 * folder's first cluster, or 0 for the root. cb returns non-zero to stop the
 * walk (fat_dir then returns 1). cb must not use b. A directory is read for at
 * most 65,536 entries, which bounds a looping chain. */
typedef int (*fat_dir_cb)(const uint8_t *d, void *ctx);
int fat_dir(uint32_t dir, uint8_t *b, fat_dir_cb cb, void *ctx);

uint32_t fat_entry_cluster(const uint8_t *d);
static inline uint32_t fat_entry_size(const uint8_t *d)
{
    return d[28] | (uint32_t)d[29] << 8 | (uint32_t)d[30] << 16 | (uint32_t)d[31] << 24;
}

/* A file read sector by sector, following its cluster chain one link at a
 * time (any fragmentation). fat_next_lba() returns the next sector's LBA, or 0
 * when the file has no sectors left or its chain is broken: a free, bad,
 * reserved or out-of-range link, or an end of chain before the file's size.
 * It may read a FAT sector into b. */
typedef struct {
    uint32_t clus;      /* current cluster */
    uint32_t sec;       /* next sector within it */
    uint32_t left;      /* sectors still to come */
} fat_stream_t;

void     fat_open(fat_stream_t *s, uint32_t clus, uint32_t size);
uint32_t fat_next_lba(fat_stream_t *s, uint8_t *b);

#endif
