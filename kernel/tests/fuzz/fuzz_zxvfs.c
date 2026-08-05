/* fuzz_zxvfs.c — deterministic fuzzer for ZXVFS mount + read
 *
 * A hostile or corrupt disk must be REJECTED, never crash the mounter
 * or the journal-recovery path. This feeds random superblock/journal/
 * inode sectors to zxvfs_mount and, when a mount happens to succeed,
 * exercises read/list with random names — all under ASan+UBSan.
 *
 *   cc -O1 -g -fsanitize=address,undefined -DZXVFS_HOST -Iinclude -Isrc/zxvfs \
 *      tests/fuzz/fuzz_zxvfs.c src/zxvfs/zxvfs.c -o /tmp/fuzz_zxvfs && /tmp/fuzz_zxvfs
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "zxvfs.h"

#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];

static uint64_t s_state = 0xfeedface12345678ULL;
static uint64_t rng(void) {
    uint64_t x = s_state; x ^= x<<13; x ^= x>>7; x ^= x<<17; return s_state=x;
}

static int mem_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    (void)dev; if (lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE); return 0;
}
static int mem_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    (void)dev; if (lba >= DISK_SECTORS) return -1;
    memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE); return 0;
}

int main(int argc, char **argv) {
    long iters = 100000;
    if (argc > 1) iters = atol(argv[1]);

    block_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.present = true; dev.total_sectors = DISK_SECTORS;
    dev.read_sector = mem_read; dev.write_sector = mem_write;

    for (long i = 0; i < iters; i++) {
        /* Randomize only the metadata region (superblock, journal header,
         * journal staging, inode table) — the part mount/recover parse. */
        for (uint32_t s = 0; s < ZXVFS_DATA_SECTOR && s < DISK_SECTORS; s++)
            for (int b = 0; b < BLOCKDEV_SECTOR_SIZE; b++)
                g_disk[s][b] = (uint8_t)rng();

        /* Occasionally plant a valid magic so mount sometimes proceeds. */
        if ((rng() & 3) == 0) {
            uint32_t m = ZXVFS_MAGIC;
            memcpy(g_disk[ZXVFS_SB_SECTOR], &m, 4);
            uint32_t ver = ZXVFS_VERSION;
            memcpy(g_disk[ZXVFS_SB_SECTOR] + 4, &ver, 4);
        }

        zxvfs_t fs;
        int rc = zxvfs_mount(&fs, &dev);
        if (rc == 0) {
            /* If it mounted, reads/lists must also be crash-free. */
            uint8_t buf[ZXVFS_FILE_MAX_BYTES];
            char nm[ZXVFS_NAME_LEN];
            for (uint32_t k = 0; k < ZXVFS_NAME_LEN - 1; k++)
                nm[k] = (char)('a' + (rng() % 26));
            nm[ZXVFS_NAME_LEN - 1] = '\0';
            (void)zxvfs_read(&fs, nm, buf, sizeof(buf));
            char names[ZXVFS_MAX_FILES][ZXVFS_NAME_LEN];
            uint32_t sizes[ZXVFS_MAX_FILES];
            (void)zxvfs_list(&fs, names, sizes, ZXVFS_MAX_FILES);
            (void)zxvfs_count(&fs);
        }
    }

    printf("[PASS] fuzz_zxvfs: %ld random disks mounted/parsed, no crash/UB\n",
           iters);
    return 0;
}
