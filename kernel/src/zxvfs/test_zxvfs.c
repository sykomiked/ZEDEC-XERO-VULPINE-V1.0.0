/* test_zxvfs.c — host unit + crash-recovery tests for ZXVFS
 *
 * Uses a memory-backed block_device_t. The crash test aborts a
 * transaction after the journal commit but before checkpoint, then
 * "reboots" (re-mounts a fresh fs over the same memory) and asserts
 * the committed write is recovered — proving the redo journal.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -DZXVFS_HOST -Iinclude -Isrc/zxvfs \
 *       src/zxvfs/test_zxvfs.c src/zxvfs/zxvfs.c -o /tmp/test_zxvfs && /tmp/test_zxvfs
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zxvfs.h"

/* ---- memory-backed block device ---- */
#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];

/* Crash injection: after this many sector writes, the next write is
 * dropped and every subsequent write fails (simulates power loss). */
static long g_crash_after = -1;
static long g_writes = 0;
static int  g_crashed = 0;

static int mem_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    (void)dev;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE);
    return 0;
}
static int mem_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    (void)dev;
    if (lba >= DISK_SECTORS) return -1;
    if (g_crashed) return -1;
    if (g_crash_after >= 0 && g_writes >= g_crash_after) {
        g_crashed = 1;                 /* power lost right before this write */
        return -1;
    }
    g_writes++;
    memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE);
    return 0;
}
static void dev_init(block_device_t *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->present = true;
    dev->total_sectors = DISK_SECTORS;
    dev->read_sector = mem_read;
    dev->write_sector = mem_write;
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s\n", msg); failures++; } \
    else         { printf("[PASS] %s\n", msg); } } while (0)

int main(void) {
    block_device_t dev;
    dev_init(&dev);
    zxvfs_t fs;

    printf("=== ZXVFS host tests ===\n");
    printf("total_sectors=%u (%.1f KB disk)\n",
           (unsigned)ZXVFS_TOTAL_SECTORS,
           ZXVFS_TOTAL_SECTORS * 512 / 1024.0);

    /* format + mount */
    CHECK(zxvfs_format(&dev) == 0, "format");
    CHECK(zxvfs_mount(&fs, &dev) == 0, "mount");
    CHECK(zxvfs_count(&fs) == 0, "empty after format");

    /* write + read back */
    const char *msg = "ZXV persistent storage online.\n";
    CHECK(zxvfs_write(&fs, "readme.txt", (const uint8_t *)msg,
                      (uint32_t)strlen(msg)) == 0, "write readme.txt");
    uint8_t buf[ZXVFS_FILE_MAX_BYTES];
    int n = zxvfs_read(&fs, "readme.txt", buf, sizeof(buf));
    CHECK(n == (int)strlen(msg) && memcmp(buf, msg, n) == 0,
          "read back readme.txt matches");
    CHECK(zxvfs_count(&fs) == 1, "one file");

    /* replace (overwrite) */
    const char *msg2 = "second version, longer than the first one!!\n";
    CHECK(zxvfs_write(&fs, "readme.txt", (const uint8_t *)msg2,
                      (uint32_t)strlen(msg2)) == 0, "overwrite readme.txt");
    n = zxvfs_read(&fs, "readme.txt", buf, sizeof(buf));
    CHECK(n == (int)strlen(msg2) && memcmp(buf, msg2, n) == 0,
          "read back overwritten content");
    CHECK(zxvfs_count(&fs) == 1, "still one file after overwrite");

    /* second file + list + unlink */
    CHECK(zxvfs_write(&fs, "config.ini",
                      (const uint8_t *)"k=v\n", 4) == 0, "write config.ini");
    char names[ZXVFS_MAX_FILES][ZXVFS_NAME_LEN];
    uint32_t sizes[ZXVFS_MAX_FILES];
    int c = zxvfs_list(&fs, names, sizes, ZXVFS_MAX_FILES);
    CHECK(c == 2, "list shows two files");
    CHECK(zxvfs_unlink(&fs, "config.ini") == 0, "unlink config.ini");
    CHECK(zxvfs_count(&fs) == 1, "one file after unlink");
    CHECK(zxvfs_read(&fs, "config.ini", buf, sizeof(buf)) < 0,
          "removed file is gone");

    /* multi-sector file (spans several data sectors) */
    uint8_t big[3000];
    for (int i = 0; i < 3000; i++) big[i] = (uint8_t)(i * 7 + 1);
    CHECK(zxvfs_write(&fs, "big.bin", big, sizeof(big)) == 0,
          "write 3000-byte file");
    uint8_t big_rb[3000];
    n = zxvfs_read(&fs, "big.bin", big_rb, sizeof(big_rb));
    CHECK(n == 3000 && memcmp(big, big_rb, 3000) == 0,
          "read back multi-sector file matches");

    /* --- persistence across "reboot" (re-mount over same disk) --- */
    zxvfs_t fs2;
    CHECK(zxvfs_mount(&fs2, &dev) == 0, "remount (reboot)");
    n = zxvfs_read(&fs2, "readme.txt", buf, sizeof(buf));
    CHECK(n == (int)strlen(msg2) && memcmp(buf, msg2, n) == 0,
          "data survived reboot");
    CHECK(zxvfs_read(&fs2, "config.ini", buf, sizeof(buf)) < 0,
          "unlink survived reboot");

    /* --- crash recovery: power loss during checkpoint --- */
    /* Establish a known baseline value on disk. */
    CHECK(zxvfs_write(&fs2, "crash.txt",
                      (const uint8_t *)"OLD", 3) == 0, "crash baseline OLD");

    /* Count how many writes a clean txn takes, so we can crash the NEXT
     * one after its journal commit but before checkpoint completes. */
    g_writes = 0; g_crash_after = -1; g_crashed = 0;
    zxvfs_write(&fs2, "crash.txt", (const uint8_t *)"MID", 3);
    long clean_writes = g_writes;
    /* txn write order: [stage data][stage inode][commit hdr][ckpt data]
     *                  [ckpt inode][clear hdr]. Crash right after commit
     * = after (count staged + 1 commit) writes, before checkpoints. */
    long commit_point = 2 /*staged*/ + 1 /*commit hdr*/;
    CHECK(clean_writes > commit_point, "txn has a post-commit phase");

    /* Now perform the "NEW" write but crash just after commit. */
    g_writes = 0; g_crashed = 0; g_crash_after = commit_point;
    int wrc = zxvfs_write(&fs2, "crash.txt", (const uint8_t *)"NEW", 3);
    CHECK(wrc != 0 || g_crashed, "write interrupted by simulated power loss");

    /* Disk now holds: committed journal + stale 'MID' in the data home.
     * Reboot: mount must replay the journal and yield 'NEW'. */
    g_crash_after = -1; g_crashed = 0;
    zxvfs_t fs3;
    CHECK(zxvfs_mount(&fs3, &dev) == 0, "remount after crash");
    CHECK(fs3.journal_replays == 1, "journal was replayed on mount");
    n = zxvfs_read(&fs3, "crash.txt", buf, sizeof(buf));
    CHECK(n == 3 && memcmp(buf, "NEW", 3) == 0,
          "committed txn recovered as NEW (crash consistency)");

    /* ---- red-team regression: untrusted on-disk structures (2026-08-04) ---- */
    g_crash_after = -1; g_crashed = 0; g_writes = 0;

    /* (a) a superblock with tampered geometry must be REJECTED at mount —
     * trusting data_sector would redirect writes onto metadata. */
    {
        zxvfs_superblock_t sb;
        memcpy(&sb, g_disk[ZXVFS_SB_SECTOR], sizeof(sb));
        uint32_t good_ds = sb.data_sector;
        sb.data_sector = good_ds + 3;              /* lie about the layout */
        memcpy(g_disk[ZXVFS_SB_SECTOR], &sb, sizeof(sb));
        zxvfs_t bad;
        CHECK(zxvfs_mount(&bad, &dev) != 0,
              "red-team: superblock with wrong data_sector is REJECTED");
        sb.data_sector = good_ds;                  /* restore for later checks */
        memcpy(g_disk[ZXVFS_SB_SECTOR], &sb, sizeof(sb));
        zxvfs_t okfs;
        CHECK(zxvfs_mount(&okfs, &dev) == 0, "genuine superblock still mounts");
    }

    /* (b) a journal with a VALID checksum but an OUT-OF-RANGE target must be
     * discarded, never replayed — otherwise it is a write-what-where. */
    {
        uint32_t evil_lba = 0;                     /* the superblock sector — off-limits */
        uint8_t before[BLOCKDEV_SECTOR_SIZE];
        memcpy(before, g_disk[evil_lba], BLOCKDEV_SECTOR_SIZE);

        zxvfs_journal_hdr_t h; memset(&h, 0, sizeof(h));
        h.magic = ZXVFS_JRNL_MAGIC; h.committed = 1; h.txn_id = 999; h.count = 1;
        h.target_lba[0] = evil_lba;
        /* reproduce jchecksum so the header looks intact */
        uint32_t c = 0x9E3779B9u ^ h.txn_id ^ (h.count * 2654435761u);
        for (uint32_t i = 0; i < h.count; i++) c = (c * 16777619u) ^ h.target_lba[i];
        h.checksum = c;
        /* stage a poison payload the attacker wants written to sector 0 */
        memset(g_disk[ZXVFS_JSTAGE_SECTOR], 0xEE, BLOCKDEV_SECTOR_SIZE);
        memcpy(g_disk[ZXVFS_JHDR_SECTOR], &h, sizeof(h));

        zxvfs_t rf;
        CHECK(zxvfs_mount(&rf, &dev) == 0,
              "red-team: a malicious journal is discarded, mount still succeeds");
        CHECK(memcmp(g_disk[evil_lba], before, BLOCKDEV_SECTOR_SIZE) == 0,
              "red-team: OUT-OF-RANGE journal target was NOT written (no write-what-where)");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS",
           failures);
    return failures ? 1 : 0;
}
