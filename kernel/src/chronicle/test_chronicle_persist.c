/* test_chronicle_persist.c — the Chronicle survives reboot and power loss.
 *
 * Uses a memory-backed block device with crash injection, exactly like the
 * ZXVFS crash test: a save is a journaled write, so after a simulated power
 * loss the disk holds either the old ledger or the new one — never a torn
 * one — and either way the loaded chain verifies.
 */
#include <stdio.h>
#include <string.h>
#include "chronicle_persist.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* ---- memory-backed block device with crash injection ---- */
#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];
static long g_crash_after = -1, g_writes = 0; static int g_crashed = 0;
static int mem_read(block_device_t *d, uint32_t lba, uint8_t *buf) {
    (void)d; if (lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE); return 0;
}
static int mem_write(block_device_t *d, uint32_t lba, const uint8_t *buf) {
    (void)d; if (lba >= DISK_SECTORS) return -1;
    if (g_crashed) return -1;
    if (g_crash_after >= 0 && g_writes >= g_crash_after) { g_crashed = 1; return -1; }
    g_writes++; memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE); return 0;
}
static void dev_init(block_device_t *d) {
    memset(d, 0, sizeof(*d)); d->present = true; d->total_sectors = DISK_SECTORS;
    d->read_sector = mem_read; d->write_sector = mem_write;
}

static void good_event(wyrm_event_t *e, uint64_t ord) {
    memset(e, 0, sizeof(*e));
    e->ordinal = ord;
    e->delta[0] = rmag_rational_from_frac(5, 1, false);
    e->delta[1] = rmag_rational_from_frac(5, 1, true);
    e->n_deltas = 2;
    e->evidence = LPRES_STATE_TRUE;
    e->ev[0][0] = SR_ONE; e->ev[1][1] = SR_ONE; e->n_ev = 2;
    e->r_min = SR_FROM_FLOAT(1.5);
    e->route_available = true; e->phases_healthy = true;
}

int main(void) {
    printf("=== the Chronicle survives reboot and power loss ===\n");
    block_device_t dev; dev_init(&dev);
    zxvfs_t fs;
    CHECK(zxvfs_format(&dev) == 0 && zxvfs_mount(&fs, &dev) == 0, "fs formatted + mounted");

    /* build a ledger with a few judged events + one pending */
    chronicle_t c; chronicle_init(&c);
    wyrm_event_t e;
    for (uint64_t k = 0; k < 5; k++) { good_event(&e, 10 + k); chronicle_submit(&c, &e); }
    good_event(&e, 99); e.delta[1] = rmag_rational_from_frac(4,1,true); /* not conserved */
    chronicle_submit(&c, &e);                                /* a chained REJECT */
    uint8_t head_before[CHRON_HASH_LEN];
    memcpy(head_before, chronicle_head(&c), CHRON_HASH_LEN);
    uint32_t len_before = chronicle_length(&c);
    printf("       built ledger: %u entries\n", len_before);
    CHECK(len_before == 6, "6 resolved entries (5 commit + 1 reject)");

    /* ---- save, then "reboot": fresh chronicle + remount over the same disk ---- */
    CHECK(chronicle_save(&c, &fs, "chronicle.log"), "ledger saved (journaled write)");
    zxvfs_t fs2;
    CHECK(zxvfs_mount(&fs2, &dev) == 0, "remount (reboot)");
    chronicle_t loaded;
    CHECK(chronicle_load(&loaded, &fs2, "chronicle.log"), "ledger loaded back");
    CHECK(chronicle_length(&loaded) == len_before, "same number of entries");
    CHECK(memcmp(chronicle_head(&loaded), head_before, CHRON_HASH_LEN) == 0,
          "the chain head survives a reboot BYTE-FOR-BYTE");
    CHECK(chronicle_verify(&loaded), "the loaded chain verifies end to end");
    CHECK(loaded.next_seq == c.next_seq, "sequence continues where it left off");

    /* ---- the loaded ledger keeps working: append + still verifies ---- */
    good_event(&e, 200); chronicle_result_t r = chronicle_submit(&loaded, &e);
    CHECK(r.chained && r.seq == c.next_seq,
          "a reloaded ledger appends the next event at the right seq");
    CHECK(chronicle_verify(&loaded), "and still verifies after appending");

    /* ---- a tampered on-disk image is REFUSED ---- */
    {
        /* corrupt the persisted file's bytes, then try to load */
        uint8_t bad[4096]; int n = zxvfs_read(&fs2, "chronicle.log", bad, sizeof(bad));
        CHECK(n > 60, "read the saved image");
        bad[60] ^= 0xFF;                                   /* flip a byte in entry 0 */
        zxvfs_write(&fs2, "chronicle.bad", bad, (uint32_t)n);
        chronicle_t t;
        CHECK(!chronicle_load(&t, &fs2, "chronicle.bad"),
              "a tampered ledger image is REFUSED, not trusted");
        CHECK(chronicle_length(&t) == 0, "and the target is left cleanly empty");
    }

    /* ---- power loss during save: old OR new ledger, never torn ---- */
    {
        block_device_t d2; dev_init(&d2);
        g_crash_after = -1; g_crashed = 0; g_writes = 0;
        zxvfs_t f; zxvfs_format(&d2); zxvfs_mount(&f, &d2);
        chronicle_t base; chronicle_init(&base);
        for (uint64_t k = 0; k < 3; k++) { good_event(&e, 10 + k); chronicle_submit(&base, &e); }
        chronicle_save(&base, &f, "chronicle.log");         /* durable baseline */

        /* now grow it and save again, but lose power mid-write */
        for (uint64_t k = 0; k < 2; k++) { good_event(&e, 20 + k); chronicle_submit(&base, &e); }
        g_writes = 0; g_crash_after = 2;                    /* die after 2 sector writes */
        chronicle_save(&base, &f, "chronicle.log");         /* interrupted */

        /* reboot: mount replays the journal, load whichever ledger is on disk */
        g_crash_after = -1; g_crashed = 0;
        zxvfs_t f3; CHECK(zxvfs_mount(&f3, &d2) == 0, "remount after power loss");
        chronicle_t rec;
        bool ok = chronicle_load(&rec, &f3, "chronicle.log");
        printf("       after power loss: load=%s, entries=%u\n",
               ok ? "ok" : "none", chronicle_length(&rec));
        CHECK(ok && chronicle_verify(&rec),
              "after power loss the ledger loads and VERIFIES (not torn)");
        CHECK(chronicle_length(&rec) == 3 || chronicle_length(&rec) == 5,
              "it is either the old (3) or the new (5) ledger — never a mix");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
