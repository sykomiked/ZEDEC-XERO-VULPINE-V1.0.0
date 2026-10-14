/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_diskcrypt.c — Adiantum against its published vectors, and encrypted
 * volumes on a memory disk: key slots, tampering, torn header writes, erase.
 *
 * The memory disk can refuse writes after a set count, which is how a write
 * cut short by power loss is modelled. */
#include <stdio.h>
#include <string.h>
#include "adiantum.h"
#include "diskcrypt.h"
#include "test_diskcrypt_vectors.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint32_t unhex(const char *h, uint8_t *out)
{
    uint32_t n = (uint32_t) strlen(h) / 2;
    for (uint32_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
    return n;
}

#define NVEC(a) (sizeof(a) / sizeof((a)[0]))

/* ---- the memory disk */
#define DISK_SECTORS 128u
static uint8_t g_disk[DISK_SECTORS][BLOCKDEV_SECTOR_SIZE];
static int g_writes_left = -1; /* -1: unlimited */

static int mem_read(block_device_t *d, uint32_t lba, uint8_t *buf)
{
    (void) d;
    if (lba >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk[lba], BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static int mem_write(block_device_t *d, uint32_t lba, const uint8_t *buf)
{
    (void) d;
    if (lba >= DISK_SECTORS || g_writes_left == 0) return -1;
    if (g_writes_left > 0) g_writes_left--;
    memcpy(g_disk[lba], buf, BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static block_device_t g_dev = {true, DISK_SECTORS, "mem", 0, mem_read, mem_write};

/* ---- deterministic randomness */
static uint64_t g_rng_state = 0x9E3779B97F4A7C15ull;
static void test_fill(void *ctx, uint8_t *out, uint32_t len)
{
    (void) ctx;
    for (uint32_t i = 0; i < len; i++) {
        g_rng_state ^= g_rng_state << 13;
        g_rng_state ^= g_rng_state >> 7;
        g_rng_state ^= g_rng_state << 17;
        out[i] = (uint8_t) (g_rng_state >> 24);
    }
}
static const diskcrypt_rng_t g_rng = {test_fill, 0};

/* 8 MiB of Argon2id work area: the passphrase floor */
static uint64_t g_mem[DISKCRYPT_MIN_M_KIB * 128u];
static const diskcrypt_work_t g_work = {g_mem, DISKCRYPT_MIN_M_KIB};

static const uint8_t PASS[] = "correct horse battery staple";
#define PASS_LEN (sizeof PASS - 1)
#define BASE     4u
#define COUNT    100u

/* ====================================================================== */

static void test_vectors(void)
{
    static uint8_t k[2048], iv[64], p[8192], c[8192], o[8192];
    int bad = 0;
    for (uint32_t i = 0; i < NVEC(xchacha12_vec); i++) {
        unhex(xchacha12_vec[i].key, k);
        unhex(xchacha12_vec[i].iv, iv);
        uint32_t n = unhex(xchacha12_vec[i].ptext, p);
        unhex(xchacha12_vec[i].ctext, c);
        uint64_t ctr = 0;
        for (int j = 0; j < 8; j++) ctr |= (uint64_t) iv[24 + j] << (8 * j);
        xchacha12_xor(k, iv, ctr, p, o, n);
        CHECK(!memcmp(o, c, n), "XChaCha12 vector %u (%u bytes)", i, n);
    }
    for (uint32_t i = 0; i < NVEC(nhpoly_vec); i++) {
        static nhpoly1305_key_t nk;
        unhex(nhpoly_vec[i].key, k);
        uint32_t n = unhex(nhpoly_vec[i].msg, p);
        unhex(nhpoly_vec[i].digest, c);
        nhpoly1305_setkey(&nk, k);
        nhpoly1305(&nk, p, n, o);
        CHECK(!memcmp(o, c, 16), "NHPoly1305 vector %u (%u bytes)", i, n);
    }
    for (uint32_t i = 0; i < NVEC(adiantum_vec); i++) {
        static adiantum_t a;
        unhex(adiantum_vec[i].key, k);
        unhex(adiantum_vec[i].iv, iv);
        uint32_t n = unhex(adiantum_vec[i].ptext, p);
        unhex(adiantum_vec[i].ctext, c);
        adiantum_init(&a, k);
        CHECK(adiantum_encrypt(&a, iv, p, o, n) && !memcmp(o, c, n),
              "Adiantum vector %u encrypts (%u bytes)", i, n);
        CHECK(adiantum_decrypt(&a, iv, c, o, n) && !memcmp(o, p, n),
              "Adiantum vector %u decrypts (%u bytes)", i, n);
        memcpy(o, p, n);
        adiantum_encrypt(&a, iv, o, o, n);
        bad = memcmp(o, c, n) != 0;
        CHECK(!bad, "Adiantum vector %u in place", i);
    }

    /* FIPS-197 appendix C.3 */
    uint8_t key[32], pt[16], ct[16];
    aes256_ct_t ae;
    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
    unhex("00112233445566778899aabbccddeeff", pt);
    unhex("8ea2b7ca516745bfeafc49904b496089", ct);
    aes256_ct_init(&ae, key);
    aes256_ct_encrypt(&ae, pt, o);
    CHECK(!memcmp(o, ct, 16), "AES-256 FIPS-197 C.3 encrypts");
    aes256_ct_decrypt(&ae, ct, o);
    CHECK(!memcmp(o, pt, 16), "AES-256 FIPS-197 C.3 decrypts");
    bad = 0;
    for (int i = 0; i < 300; i++) {
        uint8_t b[16], e[16], d[16];
        test_fill(0, key, 32);
        test_fill(0, b, 16);
        aes256_ct_init(&ae, key);
        aes256_ct_encrypt(&ae, b, e);
        aes256_ct_decrypt(&ae, e, d);
        bad |= memcmp(b, d, 16) != 0 || memcmp(b, e, 16) == 0;
    }
    CHECK(!bad, "AES-256 decrypt inverts encrypt for 300 random keys and blocks");
}

static void test_wide_block(void)
{
    static adiantum_t a;
    uint8_t key[32], tw[32], p[512], c1[512], c2[512];
    test_fill(0, key, 32);
    test_fill(0, p, sizeof p);
    memset(tw, 0, sizeof tw);
    adiantum_init(&a, key);
    adiantum_encrypt(&a, tw, p, c1, 512);
    p[0] ^= 1;
    adiantum_encrypt(&a, tw, p, c2, 512);
    int same = 0;
    for (int i = 0; i < 512; i += 16) same += !memcmp(c1 + i, c2 + i, 16);
    CHECK(same == 0, "one flipped bit changes every 16-byte block of the sector");
    p[0] ^= 1;
    p[511] ^= 0x80;
    adiantum_encrypt(&a, tw, p, c2, 512);
    same = 0;
    for (int i = 0; i < 512; i += 16) same += !memcmp(c1 + i, c2 + i, 16);
    CHECK(same == 0, "so does a flipped last bit");
    p[511] ^= 0x80;
    tw[0] = 1;
    adiantum_encrypt(&a, tw, p, c2, 512);
    CHECK(memcmp(c1, c2, 512) != 0, "another tweak (sector) gives another ciphertext");
    CHECK(!adiantum_encrypt(&a, tw, p, c2, 15), "under 16 bytes refused");
    adiantum_wipe(&a);
    uint8_t zero[sizeof a];
    memset(zero, 0, sizeof zero);
    CHECK(!memcmp(&a, zero, sizeof a), "wipe clears every key byte");
}

static void test_header(void)
{
    static diskcrypt_header_t h, g;
    static uint8_t buf[DISKCRYPT_HDR_BYTES];
    memset(&h, 0, sizeof h);
    h.data_sectors = 1000;
    h.generation = 7;
    test_fill(0, h.uuid, 16);
    h.slots[0].type = DISKCRYPT_SLOT_PASSPHRASE;
    h.slots[0].m_kib = 65536;
    h.slots[0].t = 3;
    h.slots[0].p = 1;
    test_fill(0, h.slots[0].salt, 32);
    test_fill(0, h.slots[0].wrapped, 32);
    h.slots[5].type = DISKCRYPT_SLOT_RECOVERY;
    h.slots[5].m_kib = 8;
    h.slots[5].t = 1;
    h.slots[5].p = 1;
    test_fill(0, h.slots[5].tag, 16);
    diskcrypt_header_encode(&h, buf);
    CHECK(diskcrypt_header_decode(buf, &g) && !memcmp(&h, &g, sizeof h),
          "header round-trips every field");
    int caught = 1;
    for (uint32_t i = 0; i < DISKCRYPT_HDR_BYTES; i += 5) {
        buf[i] ^= 0x10;
        caught &= !diskcrypt_header_decode(buf, &g);
        buf[i] ^= 0x10;
    }
    CHECK(caught, "a flipped byte anywhere in the header is caught");
    h.slots[2].type = 9;
    diskcrypt_header_encode(&h, buf);
    CHECK(!diskcrypt_header_decode(buf, &g), "an unknown slot type is refused");
    memset(buf, 0, sizeof buf);
    CHECK(!diskcrypt_header_decode(buf, &g), "a blank sector is no header");
}

static void fill_pattern(void)
{
    memset(g_disk, 0xA5, sizeof g_disk);
}

static int outside_untouched(void)
{
    for (uint32_t s = 0; s < DISK_SECTORS; s++) {
        if (s >= BASE && s < BASE + COUNT) continue;
        for (uint32_t i = 0; i < BLOCKDEV_SECTOR_SIZE; i++)
            if (g_disk[s][i] != 0xA5) return 0;
    }
    return 1;
}

static void test_volume(void)
{
    static diskcrypt_volume_t v;
    static uint8_t plain[8][BLOCKDEV_SECTOR_SIZE], back[BLOCKDEV_SECTOR_SIZE];
    uint32_t slot = 99;

    fill_pattern();
    CHECK(diskcrypt_format(&v, &g_dev, BASE, 16, DISKCRYPT_SLOT_PASSPHRASE, PASS, PASS_LEN,
                           DISKCRYPT_MIN_M_KIB, 1, &g_rng, &g_work) == DISKCRYPT_ERR_ARG,
          "a volume with no data sectors is refused");
    CHECK(diskcrypt_format(&v, &g_dev, BASE, DISK_SECTORS, DISKCRYPT_SLOT_PASSPHRASE, PASS,
                           PASS_LEN, DISKCRYPT_MIN_M_KIB, 1, &g_rng, &g_work) == DISKCRYPT_ERR_ARG,
          "a volume past the end of the disk is refused");
    CHECK(diskcrypt_format(&v, &g_dev, BASE, COUNT, DISKCRYPT_SLOT_PASSPHRASE, PASS, PASS_LEN, 1024,
                           1, &g_rng, &g_work) == DISKCRYPT_ERR_WEAK_KDF,
          "a passphrase slot below 8 MiB of Argon2id is refused");
    diskcrypt_work_t small = {g_mem, 1024};
    CHECK(diskcrypt_format(&v, &g_dev, BASE, COUNT, DISKCRYPT_SLOT_PASSPHRASE, PASS, PASS_LEN,
                           DISKCRYPT_MIN_M_KIB, 1, &g_rng, &small) == DISKCRYPT_ERR_MEMORY,
          "a work area smaller than the KDF needs is refused");
    CHECK(!diskcrypt_probe(&g_dev, BASE, 0), "nothing was written by the refused formats");

    CHECK(diskcrypt_format(&v, &g_dev, BASE, COUNT, DISKCRYPT_SLOT_PASSPHRASE, PASS, PASS_LEN,
                           DISKCRYPT_MIN_M_KIB, 1, &g_rng, &g_work) == DISKCRYPT_OK,
          "format a volume with a passphrase slot");
    CHECK(v.unlocked && v.dev.present && v.dev.total_sectors == COUNT - DISKCRYPT_DATA_OFFSET,
          "the decrypted device has the data sectors");
    uint32_t ds = 0;
    CHECK(diskcrypt_probe(&g_dev, BASE, &ds) && ds == COUNT - DISKCRYPT_DATA_OFFSET,
          "probe finds it and its size, without a key");

    int ok = 1;
    for (uint32_t s = 0; s < 8; s++) {
        memset(plain[s], (int) ('A' + s), BLOCKDEV_SECTOR_SIZE);
        ok &= blockdev_write(&v.dev, s * 9, plain[s]) == 0;
    }
    CHECK(ok, "write eight sectors through the volume");
    ok = 1;
    for (uint32_t s = 0; s < 8; s++)
        ok &= blockdev_read(&v.dev, s * 9, back) == 0 && !memcmp(back, plain[s], sizeof back);
    CHECK(ok, "read them back");
    int leaked = 0;
    for (uint32_t s = 0; s < DISK_SECTORS; s++)
        for (uint32_t i = 0; i + 16 <= BLOCKDEV_SECTOR_SIZE; i++)
            if (!memcmp(g_disk[s] + i, plain[0], 16)) leaked = 1;
    CHECK(!leaked, "no plaintext run reaches the disk");

    /* The on-disk sector is adiantum(volume key, tweak = sector number),
     * the same layout as Linux dm-crypt's adiantum with plain64 IVs. */
    uint8_t tweak[32], expect[BLOCKDEV_SECTOR_SIZE];
    memset(tweak, 0, sizeof tweak);
    tweak[0] = 9;
    adiantum_encrypt(&v.cipher, tweak, plain[1], expect, sizeof expect);
    CHECK(!memcmp(g_disk[BASE + DISKCRYPT_DATA_OFFSET + 9], expect, sizeof expect),
          "a data sector is Adiantum with its sector number as the tweak");
    CHECK(blockdev_read(&v.dev, COUNT - DISKCRYPT_DATA_OFFSET, back) != 0 &&
              blockdev_write(&v.dev, COUNT - DISKCRYPT_DATA_OFFSET, back) != 0,
          "sectors past the volume are refused");
    CHECK(outside_untouched(), "nothing outside the volume was touched");

    diskcrypt_lock(&v);
    uint8_t zero[32];
    memset(zero, 0, sizeof zero);
    CHECK(!v.unlocked && !memcmp(v.vkey, zero, 32) && blockdev_read(&v.dev, 0, back) != 0,
          "lock forgets the key and the device stops answering");

    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, (const uint8_t *) "wrong", 5, &g_work, &slot) ==
              DISKCRYPT_ERR_WRONG_KEY,
          "a wrong passphrase does not open it");
    CHECK(!v.unlocked && blockdev_read(&v.dev, 0, back) != 0, "and leaves it locked");
    slot = 99;
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &g_work, &slot) == DISKCRYPT_OK &&
              slot == 0,
          "the passphrase opens slot 0");
    CHECK(blockdev_read(&v.dev, 9, back) == 0 && !memcmp(back, plain[1], sizeof back),
          "the data is intact after unlock");
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &small, &slot) == DISKCRYPT_ERR_MEMORY,
          "unlock with too little memory says so rather than 'wrong key'");

    /* a recovery key in a second slot */
    diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &g_work, &slot);
    uint8_t rk[DISKCRYPT_RECOVERY_LEN], rk2[DISKCRYPT_RECOVERY_LEN];
    char text[DISKCRYPT_RECOVERY_TEXT];
    diskcrypt_recovery_key(&g_rng, rk, text);
    CHECK(strlen(text) == 71 && text[8] == '-', "recovery key prints as 8 groups of 8");
    CHECK(diskcrypt_recovery_parse(text, rk2) && !memcmp(rk, rk2, 32),
          "the printed recovery key parses back");
    char lower[DISKCRYPT_RECOVERY_TEXT];
    for (uint32_t i = 0; i <= strlen(text); i++)
        lower[i] = (text[i] >= 'A' && text[i] <= 'F') ? (char) (text[i] + 32) : text[i];
    CHECK(diskcrypt_recovery_parse(lower, rk2) && !memcmp(rk, rk2, 32), "lower case parses too");
    CHECK(!diskcrypt_recovery_parse("ABCD-12", rk2), "a short recovery key is refused");
    lower[3] = 'g';
    CHECK(!diskcrypt_recovery_parse(lower, rk2), "a non-hex digit is refused");

    CHECK(diskcrypt_add_slot(&v, DISKCRYPT_SLOT_RECOVERY, rk, 32, 8, 1, &g_rng, &g_work, &slot) ==
                  DISKCRYPT_OK &&
              slot == 1,
          "add a recovery-key slot");
    CHECK(diskcrypt_slots_used(&v) == 2, "two slots in use");
    diskcrypt_lock(&v);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_OK && slot == 1,
          "the recovery key opens slot 1");
    CHECK(blockdev_read(&v.dev, 63, back) == 0 && !memcmp(back, plain[7], sizeof back),
          "with the same data");

    /* change the passphrase: add the new one, remove the old */
    static const uint8_t NEWPASS[] = "a new passphrase";
    CHECK(diskcrypt_add_slot(&v, DISKCRYPT_SLOT_PASSPHRASE, NEWPASS, sizeof NEWPASS - 1,
                             DISKCRYPT_MIN_M_KIB, 1, &g_rng, &g_work, &slot) == DISKCRYPT_OK &&
              slot == 2,
          "add a new passphrase in slot 2");
    CHECK(diskcrypt_remove_slot(&v, 0) == DISKCRYPT_OK, "remove the old passphrase");
    diskcrypt_lock(&v);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &g_work, &slot) ==
              DISKCRYPT_ERR_WRONG_KEY,
          "the old passphrase no longer opens it");
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, NEWPASS, sizeof NEWPASS - 1, &g_work, &slot) ==
                  DISKCRYPT_OK &&
              slot == 2,
          "the new one does");
    CHECK(diskcrypt_remove_slot(&v, 1) == DISKCRYPT_OK, "remove the recovery slot");
    CHECK(diskcrypt_remove_slot(&v, 2) == DISKCRYPT_ERR_LAST_SLOT,
          "the last slot can not be removed");
    CHECK(diskcrypt_remove_slot(&v, 1) == DISKCRYPT_ERR_ARG, "an empty slot can not be removed");
    diskcrypt_lock(&v);
    CHECK(diskcrypt_add_slot(&v, DISKCRYPT_SLOT_RECOVERY, rk, 32, 8, 1, &g_rng, &g_work, 0) ==
              DISKCRYPT_ERR_LOCKED,
          "a locked volume takes no new slot");
    CHECK(diskcrypt_remove_slot(&v, 2) == DISKCRYPT_ERR_LOCKED, "or loses one");
    CHECK(outside_untouched(), "still nothing outside the volume touched");
}

/* Rewrite both header copies with `h` (sealed, as an attacker with the disk
 * could). */
static void write_raw_header(const diskcrypt_header_t *h)
{
    static uint8_t buf[DISKCRYPT_HDR_BYTES];
    diskcrypt_header_encode(h, buf);
    for (uint32_t c = 0; c < 2; c++)
        for (uint32_t i = 0; i < DISKCRYPT_HDR_SECTORS; i++)
            memcpy(g_disk[BASE + c * DISKCRYPT_HDR_SECTORS + i], buf + BLOCKDEV_SECTOR_SIZE * i,
                   BLOCKDEV_SECTOR_SIZE);
}

static void test_tamper(void)
{
    static diskcrypt_volume_t v;
    static diskcrypt_header_t good, h;
    uint8_t rk[32];
    uint32_t slot;
    test_fill(0, rk, 32);
    fill_pattern();
    diskcrypt_format(&v, &g_dev, BASE, COUNT, DISKCRYPT_SLOT_PASSPHRASE, PASS, PASS_LEN,
                     DISKCRYPT_MIN_M_KIB, 1, &g_rng, &g_work);
    diskcrypt_add_slot(&v, DISKCRYPT_SLOT_RECOVERY, rk, 32, 8, 1, &g_rng, &g_work, &slot);
    memcpy(&good, &v.hdr, sizeof good);
    diskcrypt_lock(&v);

    memcpy(&h, &good, sizeof h);
    h.slots[0].m_kib = 4096;
    write_raw_header(&h);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &g_work, &slot) ==
              DISKCRYPT_ERR_WRONG_KEY,
          "a passphrase slot rewritten below the floor is not tried");

    memcpy(&h, &good, sizeof h);
    h.slots[0].t = 2;
    write_raw_header(&h);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &g_work, &slot) ==
              DISKCRYPT_ERR_WRONG_KEY,
          "a slot whose KDF settings were changed does not open");

    memcpy(&h, &good, sizeof h);
    memcpy(&h.slots[3], &h.slots[1], sizeof h.slots[1]);
    memset(&h.slots[1], 0, sizeof h.slots[1]);
    write_raw_header(&h);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_ERR_WRONG_KEY,
          "a slot moved to another index does not open");

    memcpy(&h, &good, sizeof h);
    h.slots[1].type = DISKCRYPT_SLOT_PASSPHRASE;
    h.slots[1].m_kib = DISKCRYPT_MIN_M_KIB;
    write_raw_header(&h);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_ERR_WRONG_KEY,
          "a slot whose type was changed does not open");

    memcpy(&h, &good, sizeof h);
    h.uuid[0] ^= 1;
    write_raw_header(&h);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_ERR_WRONG_KEY,
          "a header with another uuid does not open");

    memcpy(&h, &good, sizeof h);
    h.data_sectors = DISK_SECTORS - BASE - DISKCRYPT_DATA_OFFSET + 1;
    write_raw_header(&h);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_ERR_NO_VOLUME,
          "a header claiming more sectors than the disk holds is refused");

    write_raw_header(&good);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_OK,
          "the untouched header opens again");

    /* copy A damaged: copy B still opens */
    diskcrypt_lock(&v);
    g_disk[BASE + 3][100] ^= 0xFF;
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_OK,
          "a damaged copy A falls back to copy B");
    g_disk[BASE + DISKCRYPT_HDR_SECTORS + 3][100] ^= 0xFF;
    diskcrypt_lock(&v);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, rk, 32, &g_work, &slot) == DISKCRYPT_ERR_NO_VOLUME,
          "both copies damaged: no volume");
    write_raw_header(&good);

    /* generation wraparound: 0 is newer than 0xFFFFFFFF */
    static uint8_t buf[DISKCRYPT_HDR_BYTES];
    memcpy(&h, &good, sizeof h);
    h.generation = 0xFFFFFFFFu;
    diskcrypt_header_encode(&h, buf);
    for (uint32_t i = 0; i < DISKCRYPT_HDR_SECTORS; i++)
        memcpy(g_disk[BASE + DISKCRYPT_HDR_SECTORS + i], buf + BLOCKDEV_SECTOR_SIZE * i,
               BLOCKDEV_SECTOR_SIZE);
    h.generation = 0;
    memset(&h.slots[0], 0, sizeof h.slots[0]); /* only A lacks slot 0 */
    diskcrypt_header_encode(&h, buf);
    for (uint32_t i = 0; i < DISKCRYPT_HDR_SECTORS; i++)
        memcpy(g_disk[BASE + i], buf + BLOCKDEV_SECTOR_SIZE * i, BLOCKDEV_SECTOR_SIZE);
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, PASS, PASS_LEN, &g_work, &slot) ==
              DISKCRYPT_ERR_WRONG_KEY,
          "generation 0 is newer than 0xFFFFFFFF (wraparound)");
    CHECK(outside_untouched(), "tampering tests stayed inside the volume");
}

static void test_torn(void)
{
    static diskcrypt_volume_t v;
    uint8_t k1[32], k2[32];
    uint32_t slot;
    int old_ok = 1, new_ok = 1, consistent = 1;
    test_fill(0, k1, 32);
    test_fill(0, k2, 32);
    for (int cut = 0; cut <= (int) (2 * DISKCRYPT_HDR_SECTORS); cut++) {
        fill_pattern();
        g_writes_left = -1;
        diskcrypt_format(&v, &g_dev, BASE, COUNT, DISKCRYPT_SLOT_RECOVERY, k1, 32, 8, 1, &g_rng,
                         &g_work);
        g_writes_left = cut;
        diskcrypt_status_t st =
            diskcrypt_add_slot(&v, DISKCRYPT_SLOT_RECOVERY, k2, 32, 8, 1, &g_rng, &g_work, 0);
        g_writes_left = -1;
        diskcrypt_lock(&v);
        old_ok &= diskcrypt_unlock(&v, &g_dev, BASE, k1, 32, &g_work, &slot) == DISKCRYPT_OK;
        bool opened = diskcrypt_unlock(&v, &g_dev, BASE, k2, 32, &g_work, &slot) == DISKCRYPT_OK;
        /* once copy A is whole the new slot is there; a full write reports ok */
        if (cut >= (int) DISKCRYPT_HDR_SECTORS) new_ok &= opened;
        if (cut < (int) DISKCRYPT_HDR_SECTORS) consistent &= !opened;
        consistent &= (st == DISKCRYPT_OK) == (cut == (int) (2 * DISKCRYPT_HDR_SECTORS));
    }
    CHECK(old_ok, "a header write cut after any sector leaves the old key working");
    CHECK(new_ok, "once copy A is written the new slot works");
    CHECK(consistent, "a cut before copy A completes leaves the old slots; only a full write "
                      "reports success");
}

static void test_erase(void)
{
    static diskcrypt_volume_t v;
    uint8_t k[32], back[BLOCKDEV_SECTOR_SIZE];
    uint32_t slot;
    test_fill(0, k, 32);
    fill_pattern();
    diskcrypt_format(&v, &g_dev, BASE, COUNT, DISKCRYPT_SLOT_RECOVERY, k, 32, 8, 1, &g_rng,
                     &g_work);
    memset(back, 'Z', sizeof back);
    blockdev_write(&v.dev, 0, back);
    CHECK(diskcrypt_erase(&v, &g_dev, BASE) == DISKCRYPT_OK, "erase");
    CHECK(!v.unlocked && blockdev_read(&v.dev, 0, back) != 0, "erase locks the open volume");
    uint32_t ds = 1;
    CHECK(!diskcrypt_probe(&g_dev, BASE, &ds) && ds == 0, "no header copy survives");
    CHECK(diskcrypt_unlock(&v, &g_dev, BASE, k, 32, &g_work, &slot) == DISKCRYPT_ERR_NO_VOLUME,
          "the key no longer opens anything");
    int zero = 1;
    for (uint32_t s = BASE; s < BASE + DISKCRYPT_DATA_OFFSET; s++)
        for (uint32_t i = 0; i < BLOCKDEV_SECTOR_SIZE; i++) zero &= g_disk[s][i] == 0;
    CHECK(zero, "both header copies are zeroed");
    CHECK(diskcrypt_erase(0, &g_dev, DISK_SECTORS - 3) == DISKCRYPT_ERR_ARG,
          "erase refuses an area past the end of the disk");
    CHECK(outside_untouched(), "erase stayed inside the volume");
    CHECK(!strcmp(diskcrypt_status_str(DISKCRYPT_ERR_WRONG_KEY),
                  "no key slot opens with that secret"),
          "status names");
}

int main(void)
{
    test_vectors();
    test_wide_block();
    test_header();
    test_volume();
    test_tamper();
    test_torn();
    test_erase();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
