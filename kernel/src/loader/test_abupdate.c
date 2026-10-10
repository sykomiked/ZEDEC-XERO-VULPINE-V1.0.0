/* test_abupdate.c — host test for A/B signed update + probation + rollback.
 *
 * Uses a memory-backed ZXVFS and the real signed hello.zsp. Verifies:
 *   - fresh init: slot A active
 *   - stage a valid update -> inactive slot on probation, active unchanged
 *   - the probation slot is what runs next (proves itself first)
 *   - confirm -> probation slot becomes active (promotion)
 *   - stage a TAMPERED update -> rejected, NO state change
 *   - probation expiry with no confirm -> automatic rollback
 *   - state survives a "reboot" (remount) — journaled, consistent
 *
 *   gcc -std=c11 -Wall -DZXVFS_HOST -Isrc/loader -Isrc/zxvfs -Isrc/robin_debanks \
 *       -Iinclude -Iuserapp src/loader/test_abupdate.c src/loader/abupdate.c \
 *       src/loader/zsp.c src/zxvfs/zxvfs.c src/robin_debanks/sha256.c \
 *       src/robin_debanks/ed25519_verify.c -o /tmp/test_abupdate && /tmp/test_abupdate
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "abupdate.h"
#include "zsp.h"
#include "hello_signed.h"
#include "abv2_fixtures_hi.h" /* ZSP v2, version=10, arch=arm64 */
#include "abv2_fixtures_lo.h" /* ZSP v2, version=5,  arch=arm64 */

#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];
static int mem_read(block_device_t *d, uint32_t lba, uint8_t *b)
{
    (void) d;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(b, g_disk[lba], 512);
    return 0;
}
static int mem_write(block_device_t *d, uint32_t lba, const uint8_t *b)
{
    (void) d;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(g_disk[lba], b, 512);
    return 0;
}

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

int main(void)
{
    block_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.present = true;
    dev.total_sectors = DISK_SECTORS;
    dev.read_sector = mem_read;
    dev.write_sector = mem_write;

    printf("=== A/B signed update tests ===\n");
    zxvfs_t fs;
    CHECK(zxvfs_format(&dev) == 0, "format");
    CHECK(zxvfs_mount(&fs, &dev) == 0, "mount");

    /* seed slot A with the genuine package + init state */
    CHECK(zxvfs_write(&fs, "app.slotA", hello_zsp, hello_zsp_len) == 0, "seed slot A");
    ab_state_t st;
    CHECK(ab_init(&fs, &st) == AB_OK && st.active_slot == 0, "init: slot A active");

    /* stage a valid update -> goes to slot B, on probation */
    CHECK(ab_stage_update(&fs, &st, hello_zsp, hello_zsp_len, hello_root_pubkey) == AB_OK,
          "stage valid update");
    CHECK(st.probation_slot == 1 && st.active_slot == 0,
          "update on probation in slot B, A still active");
    CHECK(st.version[1] == st.version[0] + 1, "probation slot version bumped");

    /* the probation slot runs first (to prove itself) */
    static uint8_t buf[ZXVFS_FILE_MAX_BYTES];
    uint32_t blen;
    uint8_t slot;
    CHECK(ab_slot_to_run(&fs, &st, buf, sizeof(buf), &blen, &slot) == AB_OK && slot == 1,
          "probation slot is what runs next");

    /* confirm -> B becomes active */
    CHECK(ab_confirm(&fs, &st) == AB_OK, "confirm probation");
    CHECK(st.active_slot == 1 && st.probation_slot == AB_SLOT_NONE && st.promotions == 1,
          "slot B promoted to active");

    /* tampered update must be REJECTED with no state change */
    static uint8_t bad[ZXVFS_FILE_MAX_BYTES];
    memcpy(bad, hello_zsp, hello_zsp_len);
    bad[ZSP_HEADER_LEN + 40] ^= 0x01; /* corrupt payload */
    uint8_t active_before = st.active_slot;
    CHECK(ab_stage_update(&fs, &st, bad, hello_zsp_len, hello_root_pubkey) == AB_ERR_VERIFY,
          "tampered update rejected");
    CHECK(st.active_slot == active_before && st.probation_slot == AB_SLOT_NONE,
          "rejected update left state untouched");

    /* stage another valid update, then let probation EXPIRE -> auto-rollback */
    CHECK(ab_stage_update(&fs, &st, hello_zsp, hello_zsp_len, hello_root_pubkey) == AB_OK,
          "stage second update (to slot A)");
    CHECK(st.probation_slot == 0, "second update on probation in slot A");
    uint8_t good_active = st.active_slot;
    bool rb = false;
    CHECK(ab_boot_tick(&fs, &st, &rb) == AB_OK && rb == true,
          "probation expiry triggers auto-rollback");
    CHECK(st.active_slot == good_active && st.probation_slot == AB_SLOT_NONE && st.rollbacks == 1,
          "auto-rollback kept the known-good active slot");

    /* state survives a reboot (journaled): remount + re-init reads it back */
    zxvfs_t fs2;
    ab_state_t st2;
    CHECK(zxvfs_mount(&fs2, &dev) == 0, "remount (reboot)");
    CHECK(ab_init(&fs2, &st2) == AB_OK && st2.active_slot == good_active && st2.promotions == 1 &&
              st2.rollbacks == 1,
          "A/B state survived reboot, consistent");

    /* ---- P0-6 anti-rollback in the A/B flow (uses the remounted fs2/st2) ---- */
    /* Stage + confirm a v2 package at version 10; the floor rises to 10. */
    CHECK(ab_stage_update(&fs2, &st2, abv2hi_zsp, abv2hi_zsp_len, abv2hi_root_pubkey) == AB_OK,
          "stage v2 version=10");
    {
        uint8_t s = st2.probation_slot;
        CHECK(st2.version[s] == 10, "authenticated version 10 recorded");
    }
    CHECK(ab_confirm(&fs2, &st2) == AB_OK && st2.rollback_floor == 10,
          "confirm v10 raises the anti-rollback floor to 10");
    /* A validly-signed OLD package (version 5) is now refused. */
    CHECK(ab_stage_update(&fs2, &st2, abv2lo_zsp, abv2lo_zsp_len, abv2lo_root_pubkey) ==
              AB_ERR_ROLLBACK,
          "v2 version=5 below floor 10 is REJECTED (anti-rollback)");
    CHECK(st2.probation_slot == AB_SLOT_NONE, "rejected rollback left probation clear");
    /* Re-staging at the floor (version 10) is still allowed. */
    CHECK(ab_stage_update(&fs2, &st2, abv2hi_zsp, abv2hi_zsp_len, abv2hi_root_pubkey) == AB_OK,
          "v2 version=10 at the floor is accepted");
    /* A v1 package has no signed version: once a v2 package is ACTIVE, a v1
     * one must not be staged over it (it would get version active+1 and pass
     * the floor). Promote the v10 package first so slot state is v2. */
    CHECK(ab_confirm(&fs2, &st2) == AB_OK, "confirm the re-staged v10 package");
    CHECK(ab_stage_update(&fs2, &st2, hello_zsp, hello_zsp_len, hello_root_pubkey) ==
              AB_ERR_ROLLBACK,
          "v1 package over an active v2 package is REJECTED (no format downgrade)");
    CHECK(st2.probation_slot == AB_SLOT_NONE, "rejected downgrade left probation clear");
    /* the floor persists across another reboot */
    {
        zxvfs_t fs3;
        ab_state_t st3;
        CHECK(zxvfs_mount(&fs3, &dev) == 0, "remount again");
        CHECK(ab_init(&fs3, &st3) == AB_OK && st3.rollback_floor == 10,
              "anti-rollback floor survived reboot");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
