/* test_abupdate.c — host test for A/B signed update + probation + rollback.
 *
 * Uses a memory-backed ZXVFS. Packages: the real signed hello.zsp (ZSP v1),
 * the generated abv2 fixtures (ZSP v2, versions 10 and 5, their own root key),
 * and ZSP v2 packages built and signed HERE at run time with a test-only key
 * derived from a fixed public seed (src/provenance/test_signer.h), so every
 * version can be exercised without any private key in the tree. Verifies:
 *   - fresh init: slot A active
 *   - a v1 package (no signed version) is refused with AB_ERR_ROLLBACK, a
 *     tampered one with AB_ERR_VERIFY
 *   - stage a newer v2 update -> inactive slot on probation, active unchanged
 *   - the probation slot is what runs next; confirm promotes it
 *   - strict monotonicity on STAGE: version <= active, or below the floor,
 *     is refused with AB_ERR_ROLLBACK and changes nothing
 *   - strict monotonicity on PROMOTE: a probation record whose version was
 *     rewritten, or that is no longer above the active version, is refused
 *   - another package identity cannot be staged over the active one
 *   - probation expiry with no confirm -> automatic rollback
 *   - state and the floor survive a "reboot" (remount)
 *
 * Build line: see the verify-all recipe in kernel/Makefile.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "abupdate.h"
#include "zsp.h"
#include "sha256.h"
#include "hello_signed.h"
#include "abv2_fixtures_hi.h" /* ZSP v2, version=10, arch=arm64 */
#include "abv2_fixtures_lo.h" /* ZSP v2, version=5,  arch=arm64 */
#include "../provenance/test_signer.h"

#define DISK_SECTORS ZXVFS_TOTAL_SECTORS
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];
static int mem_read(block_device_t *d, uint32_t lba, uint8_t *b)
{
    (void) d;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(b, g_disk[lba], 512);
    return 0;
}
/* Power-loss injection (as in test_zxvfs.c): once g_crash_after sector writes
 * have landed, that write and every later one fail. -1 = no crash. */
static long g_crash_after = -1;
static long g_writes = 0;
static int g_crashed = 0;
static int mem_write(block_device_t *d, uint32_t lba, const uint8_t *b)
{
    (void) d;
    if (lba >= DISK_SECTORS) return -1;
    if (g_crashed) return -1;
    if (g_crash_after >= 0 && g_writes >= g_crash_after) {
        g_crashed = 1;
        return -1;
    }
    g_writes++;
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

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* Build a ZSP v2 package (layout in zsp.h) for package `name` at `version`,
 * signed by `s`. Returns its length. */
static uint32_t make_zsp2(const test_signer_t *s, const char *name, uint32_t version, uint8_t *out)
{
    uint8_t payload[48];
    for (uint32_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t) (version * 7u + i);
    memset(out, 0, ZSP2_HEADER_LEN);
    out[0] = 'Z';
    out[1] = 'S';
    out[2] = 'P';
    out[3] = '2';
    le16(out + 4, ZSP2_HEADER_LEN);
    le16(out + 6, 1);
    le32(out + 8, version);
    le16(out + 12, ZSP_ARCH_ARM64);
    le32(out + 16, 0);
    le32(out + 20, sizeof payload);
    uint8_t kd[32];
    sha256(s->pk, 32, kd);
    memcpy(out + 24, kd, 8);
    char idn[64];
    int n = snprintf(idn, sizeof idn, "zxv-package:%s", name);
    sha256((const uint8_t *) idn, (size_t) n, out + 32);
    sha256(payload, sizeof payload, out + 64);
    test_signer_sign(s, out, ZSP2_PREIMAGE_LEN, out + ZSP2_PREIMAGE_LEN);
    memcpy(out + ZSP2_HEADER_LEN, payload, sizeof payload);
    return ZSP2_HEADER_LEN + (uint32_t) sizeof payload;
}

/* ---- power loss at every write of an update cycle ----
 *
 * The header promises that an update interrupted by power loss recovers to
 * a consistent A-or-B state, never a half-applied one. For each cycle below,
 * the disk is reset to the same baseline (slot A active with version 2) and
 * the cycle is cut before its k-th sector write, for every k up to the
 * cycle's clean write count. After a "reboot" (remount, journal replay,
 * ab_init) the state must be well formed, the active slot must hold a
 * package that verifies, the outcome must be exactly the old one or exactly
 * the new one, a pending probation must point at the verified new package,
 * and finishing the cycle must then succeed. */
enum { CYCLE_CONFIRM, CYCLE_EXPIRE };
static uint8_t g_base[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];

/* 0 when slot `s` holds a ZSP v2 package that verifies under `pk`. */
static int slot_version(zxvfs_t *fs, uint8_t s, const uint8_t pk[32], uint32_t *ver)
{
    static uint8_t b[ZXVFS_FILE_MAX_BYTES];
    int n = zxvfs_read(fs, s ? "app.slotB" : "app.slotA", b, sizeof b);
    if (n <= 0) return -1;
    zsp_meta_t m;
    const uint8_t *p;
    uint32_t pl;
    if (zsp_verify2(b, (uint32_t) n, pk, 0, ZSP_ARCH_ANY, &m, &p, &pl) != ZSP_OK) return -1;
    *ver = m.version;
    return 0;
}

/* Runs the cycle; errors after a crash are expected and ignored. */
static void run_cycle(zxvfs_t *fs, ab_state_t *st, int cycle, const uint8_t *pkg, uint32_t len,
                      const uint8_t pk[32])
{
    bool rb;
    if (ab_stage_update(fs, st, pkg, len, pk) != AB_OK) return;
    if (cycle == CYCLE_CONFIRM)
        (void) ab_confirm(fs, st);
    else
        (void) ab_boot_tick(fs, st, &rb);
}

/* One cut at write k: returns 0 when every invariant holds, else prints why. */
static int crash_at(block_device_t *dev, int cycle, long k, const uint8_t *pkg, uint32_t len,
                    const uint8_t pk[32])
{
    memcpy(g_disk, g_base, sizeof g_disk);
    zxvfs_t fs;
    ab_state_t st;
    g_crash_after = -1;
    g_crashed = 0;
    if (zxvfs_mount(&fs, dev) != 0 || ab_init(&fs, &st) != AB_OK) return -1;
    g_writes = 0;
    g_crash_after = k;
    run_cycle(&fs, &st, cycle, pkg, len, pk);

    /* reboot */
    g_crash_after = -1;
    g_crashed = 0;
    zxvfs_t f2;
    ab_state_t s2;
    uint32_t va = 0, vp = 0;
    const char *why = 0;
    if (zxvfs_mount(&f2, dev) != 0)
        why = "remount failed";
    else if (ab_init(&f2, &s2) != AB_OK)
        why = "ab_init failed";
    else if (s2.magic != AB_STATE_MAGIC || s2.active_slot > 1 ||
             (s2.probation_slot != AB_SLOT_NONE && s2.probation_slot > 1))
        why = "state not well formed";
    else if (slot_version(&f2, s2.active_slot, pk, &va) != 0)
        why = "active slot does not hold a verified package";
    else if (!(s2.active_slot == 0 && va == 2 && s2.rollback_floor == 0) &&
             !(cycle == CYCLE_CONFIRM && s2.active_slot == 1 && va == 3 &&
               s2.version[1] == 3 && s2.rollback_floor == 3))
        why = "outcome is neither the old state nor the new one";
    else if (s2.probation_slot != AB_SLOT_NONE &&
             (s2.probation_slot == s2.active_slot ||
              slot_version(&f2, s2.probation_slot, pk, &vp) != 0 || vp != 3 ||
              s2.version[s2.probation_slot] != 3))
        why = "probation points at something other than the verified new package";
    else if (cycle == CYCLE_EXPIRE && s2.active_slot != 0)
        why = "an expired probation was promoted";
    if (!why && cycle == CYCLE_CONFIRM && s2.active_slot == 0) {
        /* finish the update after recovery */
        if (s2.probation_slot == AB_SLOT_NONE &&
            ab_stage_update(&f2, &s2, pkg, len, pk) != AB_OK)
            why = "could not re-stage after recovery";
        else if (ab_confirm(&f2, &s2) != AB_OK || s2.active_slot != 1 ||
                 slot_version(&f2, 1, pk, &va) != 0 || va != 3)
            why = "could not complete the update after recovery";
    }
    if (why) printf("  cut before write %ld of the %s cycle: %s\n", k,
                    cycle == CYCLE_CONFIRM ? "confirm" : "expire", why);
    return why ? -1 : 0;
}

static void crash_sweep(block_device_t *dev)
{
    test_signer_t root;
    test_signer_init(&root, 0x73);
    static uint8_t v2[512], v3[512];
    uint32_t l2 = make_zsp2(&root, "app", 2, v2), l3 = make_zsp2(&root, "app", 3, v3);

    /* baseline: slot A holds version 2, state initialized */
    g_crash_after = -1;
    g_crashed = 0;
    memset(g_disk, 0, sizeof g_disk);
    zxvfs_t fs;
    ab_state_t st;
    CHECK(zxvfs_format(dev) == 0 && zxvfs_mount(&fs, dev) == 0 &&
              zxvfs_write(&fs, "app.slotA", v2, l2) == 0 && ab_init(&fs, &st) == AB_OK,
          "crash sweep: baseline disk (slot A holds version 2)");
    memcpy(g_base, g_disk, sizeof g_disk);

    for (int cycle = CYCLE_CONFIRM; cycle <= CYCLE_EXPIRE; cycle++) {
        /* count the sector writes of an uninterrupted cycle */
        memcpy(g_disk, g_base, sizeof g_disk);
        CHECK(zxvfs_mount(&fs, dev) == 0 && ab_init(&fs, &st) == AB_OK,
              "crash sweep: baseline mounts");
        g_writes = 0;
        run_cycle(&fs, &st, cycle, v3, l3, root.pk);
        long total = g_writes;
        int bad = 0;
        for (long k = 0; k <= total; k++) bad += crash_at(dev, cycle, k, v3, l3, root.pk) != 0;
        char msg[256];
        snprintf(msg, sizeof msg,
                 "power cut before each of the %ld writes of the %s cycle: recovers to the old "
                 "or the new state, never a mix, and the update can finish",
                 total, cycle == CYCLE_CONFIRM ? "stage+confirm" : "stage+expire");
        CHECK(total > 0 && bad == 0, msg);
    }
}

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

    /* seed slot A with the genuine v1 package + init state */
    CHECK(zxvfs_write(&fs, "app.slotA", hello_zsp, hello_zsp_len) == 0, "seed slot A");
    ab_state_t st;
    CHECK(ab_init(&fs, &st) == AB_OK && st.active_slot == 0, "init: slot A active");

    test_signer_t root, other;
    test_signer_init(&root, 0x71);
    test_signer_init(&other, 0x72);
    static uint8_t pkg[512], pkg2[512];
    uint32_t plen, plen2;

    /* a v1 package carries no signed version: it can never be staged */
    {
        ab_state_t before = st;
        CHECK(ab_stage_update(&fs, &st, hello_zsp, hello_zsp_len, hello_root_pubkey) ==
                  AB_ERR_ROLLBACK,
              "v1 package (unversioned) is REFUSED with AB_ERR_ROLLBACK");
        CHECK(memcmp(&before, &st, sizeof st) == 0, "refused v1 package left state untouched");
        static uint8_t bad[ZXVFS_FILE_MAX_BYTES];
        memcpy(bad, hello_zsp, hello_zsp_len);
        bad[ZSP_HEADER_LEN + 40] ^= 0x01; /* corrupt payload */
        CHECK(ab_stage_update(&fs, &st, bad, hello_zsp_len, hello_root_pubkey) == AB_ERR_VERIFY,
              "tampered v1 package rejected by signature first (AB_ERR_VERIFY)");
    }

    /* stage a valid v2 update (version 3) -> slot B, on probation */
    plen = make_zsp2(&root, "app", 3, pkg);
    CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_OK, "stage v2 version=3");
    CHECK(st.probation_slot == 1 && st.active_slot == 0,
          "update on probation in slot B, A still active");
    CHECK(st.version[1] == 3, "probation slot carries the signed version 3");

    static uint8_t buf[ZXVFS_FILE_MAX_BYTES];
    uint32_t blen;
    uint8_t slot;
    CHECK(ab_slot_to_run(&fs, &st, buf, sizeof(buf), &blen, &slot) == AB_OK && slot == 1,
          "probation slot is what runs next");
    CHECK(ab_confirm(&fs, &st) == AB_OK, "confirm probation");
    CHECK(st.active_slot == 1 && st.probation_slot == AB_SLOT_NONE && st.promotions == 1 &&
              st.rollback_floor == 3,
          "slot B promoted to active, floor raised to 3");

    /* tampered v2 update must be REJECTED with no state change */
    {
        plen = make_zsp2(&root, "app", 4, pkg);
        pkg[ZSP2_HEADER_LEN + 5] ^= 0x01;
        ab_state_t before = st;
        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_ERR_VERIFY,
              "tampered v2 update rejected");
        CHECK(memcmp(&before, &st, sizeof st) == 0, "rejected update left state untouched");
        plen = make_zsp2(&other, "app", 4, pkg);
        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_ERR_VERIFY,
              "v2 update signed by another key rejected");
        plen = make_zsp2(&root, "not-the-app", 4, pkg);
        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_ERR_VERIFY,
              "a different package identity cannot replace the active one");
    }

    /* strict monotonicity on STAGE */
    {
        ab_state_t before = st;
        plen = make_zsp2(&root, "app", 3, pkg);
        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_ERR_ROLLBACK,
              "re-staging the ACTIVE version (3 == 3) is refused");
        plen = make_zsp2(&root, "app", 2, pkg);
        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_ERR_ROLLBACK,
              "an OLDER validly signed version (2 < 3) is refused");
        CHECK(memcmp(&before, &st, sizeof st) == 0, "refused rollbacks left state untouched");
    }

    /* stage another valid update, then let probation EXPIRE -> auto-rollback */
    plen = make_zsp2(&root, "app", 4, pkg);
    CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_OK, "stage version 4 (to slot A)");
    CHECK(st.probation_slot == 0, "version 4 on probation in slot A");
    uint8_t good_active = st.active_slot;
    bool rb = false;
    CHECK(ab_boot_tick(&fs, &st, &rb) == AB_OK && rb == true,
          "probation expiry triggers auto-rollback");
    CHECK(st.active_slot == good_active && st.probation_slot == AB_SLOT_NONE && st.rollbacks == 1,
          "auto-rollback kept the known-good active slot");

    /* strict monotonicity on PROMOTE: the state record is on untrusted media */
    {
        plen = make_zsp2(&root, "app", 5, pkg);
        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_OK, "stage version 5");
        uint8_t ps = st.probation_slot;
        st.version[ps] = 9; /* record no longer matches the signed header */
        CHECK(ab_confirm(&fs, &st) == AB_ERR_ROLLBACK && st.probation_slot == AB_SLOT_NONE &&
                  st.active_slot == good_active && st.rollback_floor == 3,
              "promote refused when the recorded version disagrees with the package");

        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_OK, "re-stage version 5");
        st.version[st.active_slot] = 7; /* active now claims to be newer than probation */
        CHECK(ab_confirm(&fs, &st) == AB_ERR_ROLLBACK && st.probation_slot == AB_SLOT_NONE,
              "promote refused when probation is not above the active version");
        st.version[st.active_slot] = 3;

        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_OK, "stage version 5 again");
        st.rollback_floor = 6; /* floor raised in between (e.g. by another path) */
        CHECK(ab_confirm(&fs, &st) == AB_ERR_ROLLBACK && st.probation_slot == AB_SLOT_NONE,
              "promote refused below the anti-rollback floor");
        st.rollback_floor = 3;

        CHECK(ab_stage_update(&fs, &st, pkg, plen, root.pk) == AB_OK &&
                  ab_confirm(&fs, &st) == AB_OK && st.version[st.active_slot] == 5 &&
                  st.rollback_floor == 5,
              "a genuine newer version 5 is promoted; floor 5");
        plen2 = make_zsp2(&root, "app", 4, pkg2);
        CHECK(ab_stage_update(&fs, &st, pkg2, plen2, root.pk) == AB_ERR_ROLLBACK,
              "after promotion the previous version 4 can no longer be staged");
    }

    /* state survives a reboot (journaled): remount + re-init reads it back */
    zxvfs_t fs2;
    ab_state_t st2;
    CHECK(zxvfs_mount(&fs2, &dev) == 0, "remount (reboot)");
    CHECK(ab_init(&fs2, &st2) == AB_OK && st2.active_slot == st.active_slot &&
              st2.promotions == 2 && st2.rollback_floor == 5,
          "A/B state and floor survived reboot, consistent");

    /* ---- generated fixtures (their own root key): a fresh disk ---- */
    {
        memset(g_disk, 0, sizeof g_disk);
        zxvfs_t f3;
        ab_state_t s3;
        CHECK(zxvfs_format(&dev) == 0 && zxvfs_mount(&f3, &dev) == 0, "fresh disk");
        CHECK(ab_init(&f3, &s3) == AB_OK, "fresh state");
        CHECK(ab_stage_update(&f3, &s3, abv2hi_zsp, abv2hi_zsp_len, abv2hi_root_pubkey) == AB_OK,
              "stage fixture v2 version=10");
        CHECK(s3.version[s3.probation_slot] == 10, "authenticated version 10 recorded");
        CHECK(ab_confirm(&f3, &s3) == AB_OK && s3.rollback_floor == 10,
              "confirm v10 raises the anti-rollback floor to 10");
        CHECK(ab_stage_update(&f3, &s3, abv2lo_zsp, abv2lo_zsp_len, abv2lo_root_pubkey) ==
                  AB_ERR_ROLLBACK,
              "fixture v2 version=5 below floor 10 is REJECTED (anti-rollback)");
        CHECK(ab_stage_update(&f3, &s3, abv2hi_zsp, abv2hi_zsp_len, abv2hi_root_pubkey) ==
                  AB_ERR_ROLLBACK,
              "re-staging the active version 10 is REJECTED (strictly monotonic)");
        CHECK(ab_stage_update(&f3, &s3, hello_zsp, hello_zsp_len, hello_root_pubkey) ==
                  AB_ERR_ROLLBACK,
              "v1 package over an active v2 package is REJECTED (no format downgrade)");
        CHECK(s3.probation_slot == AB_SLOT_NONE, "rejected attempts left probation clear");
        zxvfs_t f4;
        ab_state_t s4;
        CHECK(zxvfs_mount(&f4, &dev) == 0 && ab_init(&f4, &s4) == AB_OK && s4.rollback_floor == 10,
              "anti-rollback floor survived reboot");
    }

    crash_sweep(&dev);

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
