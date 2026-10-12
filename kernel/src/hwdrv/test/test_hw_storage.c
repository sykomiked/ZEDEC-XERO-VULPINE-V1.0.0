/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hw_storage.c — GPT parsing (CRC32 of header and partition array, ESP
 * lookup, hostile headers) and the disk -> GPT -> FAT32 -> file path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "../hw_storage.h"

#define NBLK 128u
#define PSTART 40u /* ESP: LBA 40..103 */
#define PLAST 103u

typedef struct {
    hw_blk_t b;
    uint8_t d[NBLK * 512];
    int fail_lba;
    uint32_t reads, writes;
} mdisk_t;

static int mread(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    mdisk_t *m = (mdisk_t *) b;
    m->reads++;
    if (lba + n > NBLK || (m->fail_lba >= 0 && (uint64_t) m->fail_lba >= lba &&
                           (uint64_t) m->fail_lba < lba + n))
        return -1;
    memcpy(buf, m->d + lba * 512, n * 512);
    return 0;
}

static int mwrite(hw_blk_t *b, uint64_t lba, uint32_t n, const void *buf)
{
    mdisk_t *m = (mdisk_t *) b;
    if (lba + n > NBLK) return -1;
    memcpy(m->d + lba * 512, buf, n * 512);
    m->writes++;
    return 0;
}

static void p16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void p32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void p64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static const uint8_t ESP[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
static const uint8_t LINUX[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                                  0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

/* Recompute both CRCs after a test edits the header or entries. */
static void seal(mdisk_t *m)
{
    uint8_t *h = m->d + 512;
    uint32_t n = h[80] | (h[81] << 8), esz = h[84] | (h[85] << 8);
    p32(h + 88, hw_crc32(0, m->d + 2 * 512, n * esz));
    p32(h + 16, 0);
    p32(h + 16, hw_crc32(0, h, 92));
}

/* GPT with 4 entries of 128 B at LBA 2: entry 1 a Linux partition, entry 2
 * the ESP (so the lookup must skip one). FAT32 inside the ESP with
 * MEGAROM.TVL (600 bytes over two clusters) and a directory EFI. */
static void build(mdisk_t *m)
{
    memset(m, 0, sizeof *m);
    m->fail_lba = -1;
    strcpy(m->b.name, "mdisk0");
    m->b.block_size = 512;
    m->b.blocks = NBLK;
    m->b.read = mread;
    m->b.write = mwrite;
    uint8_t *h = m->d + 512;
    memcpy(h, "EFI PART", 8);
    p32(h + 8, 0x00010000);
    p32(h + 12, 92);
    p64(h + 24, 1);
    p64(h + 32, NBLK - 1);
    p64(h + 40, 34);
    p64(h + 48, NBLK - 34);
    p64(h + 72, 2);
    p32(h + 80, 4);
    p32(h + 84, 128);
    uint8_t *e = m->d + 2 * 512;
    memcpy(e, LINUX, 16);
    p64(e + 32, 104);
    p64(e + 40, 120);
    memcpy(e + 128, ESP, 16);
    p64(e + 128 + 32, PSTART);
    p64(e + 128 + 40, PLAST);
    seal(m);
    /* FAT32 at PSTART: 1 reserved, 1 FAT of 1 sector, 1 sector/cluster */
    uint8_t *v = m->d + PSTART * 512;
    p16(v + 11, 512);
    v[13] = 1;
    p16(v + 14, 1);
    v[16] = 1;
    p32(v + 36, 1);
    p32(v + 44, 2);
    uint8_t *fat = v + 512;
    p32(fat + 0, 0x0FFFFFF8u);
    p32(fat + 4, 0x0FFFFFFFu);
    p32(fat + 8, 0x0FFFFFFFu);  /* root */
    p32(fat + 12, 4);           /* file: cluster 3 -> 4 */
    p32(fat + 16, 0x0FFFFFFFu);
    p32(fat + 20, 0x0FFFFFFFu); /* EFI dir */
    uint8_t *root = v + 2 * 512;
    memcpy(root, "EFI        ", 11);
    root[11] = 0x10;
    p16(root + 26, 5);
    memcpy(root + 32, "MEGAROM TVL", 11);
    root[32 + 11] = 0x20;
    p16(root + 32 + 26, 3);
    p32(root + 32 + 28, 600);
    for (uint32_t i = 0; i < 1024; i++) v[3 * 512 + i] = (uint8_t) ("TVUL"[i % 4] + i / 4);
}

static void test_crc(void)
{
    CHECK(hw_crc32(0, (const uint8_t *) "123456789", 9) == 0xCBF43926u);
    CHECK(hw_crc32(0, (const uint8_t *) "", 0) == 0);
    uint32_t c = hw_crc32(0, (const uint8_t *) "1234", 4);
    CHECK(hw_crc32(c, (const uint8_t *) "56789", 5) == 0xCBF43926u);
    uint8_t z[32] = {0};
    CHECK(hw_crc32(0, z, 32) == 0x190A55ADu);
}

static void test_gpt(void)
{
    static mdisk_t m;
    uint8_t s[4096];
    hw_part_t p;
    build(&m);
    memset(&p, 0, sizeof p);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_OK);
    CHECK(p.index == 2 && p.first == PSTART && p.last == PLAST);

    /* every header field the parser trusts, broken one at a time */
    struct {
        uint32_t off;
        uint32_t val;
        int reseal, want;
    } bad[] = {
        {16, 0x12345678, 0, HW_GPT_BAD}, /* header CRC */
        {88, 0x12345678, 0, HW_GPT_BAD}, /* entries CRC (header CRC resealed below) */
        {12, 91, 1, HW_GPT_BAD},         /* header too small */
        {12, 600, 1, HW_GPT_BAD},        /* header larger than a block */
        {84, 96, 1, HW_GPT_BAD},         /* entry size not a multiple of 128 */
        {84, 1024, 1, HW_GPT_BAD},       /* entry bigger than a block */
        {80, 0, 1, HW_GPT_BAD},          /* no entries */
        {80, 9000, 1, HW_GPT_BAD},       /* array past the 1 MB cap */
        {72, 1, 1, HW_GPT_BAD},          /* entries over the header */
        {72, NBLK, 1, HW_GPT_BAD},       /* entries past the end */
        {72, NBLK - 1, 1, HW_GPT_BAD},   /* array would run off the end */
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        build(&m);
        p32(m.d + 512 + bad[i].off, bad[i].val);
        if (bad[i].off == 88) { /* reseal only the header so the array CRC is wrong */
            p32(m.d + 512 + 16, 0);
            p32(m.d + 512 + 16, hw_crc32(0, m.d + 512, 92));
        } else if (bad[i].reseal) {
            uint8_t *h = m.d + 512;
            p32(h + 16, 0);
            p32(h + 16, hw_crc32(0, h, 92));
        }
        int rc = hw_gpt_find_esp(&m.b, &p, s);
        if (rc != bad[i].want) printf("  case %u: rc %d\n", i, rc);
        CHECK(rc == bad[i].want);
    }
    /* a flipped byte in an unused entry still breaks the array CRC */
    build(&m);
    m.d[2 * 512 + 3 * 128 + 50] ^= 1;
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_BAD);
    /* no ESP: retype it */
    build(&m);
    memcpy(m.d + 2 * 512 + 128, LINUX, 16);
    seal(&m);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_NO_ESP);
    /* ESP bounds hostile */
    build(&m);
    p64(m.d + 2 * 512 + 128 + 40, NBLK);
    seal(&m);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_BAD);
    build(&m);
    p64(m.d + 2 * 512 + 128 + 32, PLAST + 1);
    seal(&m);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_BAD);
    /* first ESP wins when there are two */
    build(&m);
    memcpy(m.d + 2 * 512 + 256, ESP, 16);
    p64(m.d + 2 * 512 + 256 + 32, 105);
    p64(m.d + 2 * 512 + 256 + 40, 110);
    seal(&m);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_OK && p.index == 2 && p.first == PSTART);
    /* an ESP as the last entry of a two-block array */
    build(&m);
    p32(m.d + 512 + 80, 8);
    memset(m.d + 2 * 512 + 128, 0, 16);
    memcpy(m.d + 3 * 512 + 3 * 128, ESP, 16);
    p64(m.d + 3 * 512 + 3 * 128 + 32, PSTART);
    p64(m.d + 3 * 512 + 3 * 128 + 40, PLAST);
    /* the FAT volume sits at PSTART = 40, the array now spans LBA 2..3 */
    seal(&m);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_OK && p.index == 8);
    /* not GPT, unreadable, tiny or odd-block disks */
    build(&m);
    memcpy(m.d + 512, "EFI PARX", 8);
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_NONE);
    build(&m);
    m.fail_lba = 1;
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_IO);
    build(&m);
    m.fail_lba = 2;
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_IO);
    build(&m);
    m.b.blocks = 2;
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_NONE);
    m.b.blocks = NBLK;
    m.b.block_size = 256;
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_NONE);
    m.b.block_size = 8192;
    CHECK(hw_gpt_find_esp(&m.b, &p, s) == HW_GPT_NONE);
}

static void test_bdev(void)
{
    static mdisk_t m;
    build(&m);
    hw_bdev_t d;
    CHECK(hw_bdev_init(&d, &m.b, PSTART, PLAST - PSTART + 1) == 0);
    CHECK(d.bd.present && d.bd.total_sectors == PLAST - PSTART + 1);
    CHECK(strcmp(d.bd.model, "mdisk0") == 0);
    uint8_t buf[512];
    CHECK(blockdev_read(&d.bd, 0, buf) == 0 && memcmp(buf, m.d + PSTART * 512, 512) == 0);
    CHECK(blockdev_read(&d.bd, PLAST - PSTART, buf) == 0);
    CHECK(blockdev_read(&d.bd, PLAST - PSTART + 1, buf) == -1);
    memset(buf, 0x3C, sizeof buf);
    CHECK(blockdev_write(&d.bd, 5, buf) == 0 && m.d[(PSTART + 5) * 512] == 0x3C);
    CHECK(blockdev_write(&d.bd, PLAST - PSTART + 1, buf) == -1);
    m.b.write = 0;
    CHECK(blockdev_write(&d.bd, 5, buf) == -1);
    /* refused geometries */
    CHECK(hw_bdev_init(&d, &m.b, 0, 0) == -1);
    CHECK(hw_bdev_init(&d, &m.b, NBLK, 1) == -1);
    CHECK(hw_bdev_init(&d, &m.b, 100, 29) == -1);
    CHECK(hw_bdev_init(&d, &m.b, 100, 28) == 0);
    m.b.block_size = 4096;
    CHECK(hw_bdev_init(&d, &m.b, 0, 1) == -1);
    m.b.block_size = 512;
    m.b.blocks = 0x200000000ULL;
    CHECK(hw_bdev_init(&d, &m.b, 0, 0x100000000ULL) == -1);
    CHECK(hw_bdev_init(&d, &m.b, 0, 0xFFFFFFFFULL) == 0);
}

static void test_selftest(void)
{
    static mdisk_t ok, nogpt, badgpt, notfat, nofile;
    build(&ok);
    build(&nogpt);
    memset(nogpt.d + 512, 0, 512);
    strcpy(nogpt.b.name, "blank0");
    build(&badgpt);
    badgpt.d[512 + 40] ^= 1; /* header CRC no longer matches */
    strcpy(badgpt.b.name, "bad0");
    build(&notfat);
    memset(notfat.d + PSTART * 512, 0, 512);
    strcpy(notfat.b.name, "raw0");
    build(&nofile);
    memcpy(nofile.d + PSTART * 512 + 3 * 512 - 512 + 32, "OTHER   BIN", 11);
    strcpy(nofile.b.name, "other0");

    fake_plat_t fp;
    fake_plat_init(&fp);
    hw_registry_t r;
    memset(&r, 0, sizeof r);
    hw_reg_blk(&r, &ok.b);
    hw_reg_blk(&r, &nogpt.b);
    hw_reg_blk(&r, &badgpt.b);
    hw_reg_blk(&r, &notfat.b);
    hw_reg_blk(&r, &nofile.b);
    CHECK(hw_storage_selftest(&fp.plat, &r, "MEGAROM.TVL") == 2);
    uint8_t want[600];
    for (uint32_t i = 0; i < 600; i++) want[i] = (uint8_t) ("TVUL"[i % 4] + i / 4);
    char line[160];
    snprintf(line, sizeof line,
             "[STORAGE OK] mdisk0 ESP (GPT partition 2, LBA 40..103) FAT32 mounted; read "
             "/MEGAROM.TVL 600 bytes, crc32 %08x",
             hw_crc32(0, want, 600));
    CHECK(fake_log_has(&fp, line));
    CHECK(fake_log_has(&fp, "[STORAGE NONE] blank0 no GPT"));
    CHECK(fake_log_has(&fp, "[STORAGE FAILED] bad0 GPT failed its CRC"));
    CHECK(fake_log_has(&fp, "[STORAGE FAILED] raw0 ESP (GPT partition 2, LBA 40..103) is not a "
                            "FAT32 volume"));
    CHECK(fake_log_has(&fp, "[STORAGE OK] other0 ESP (GPT partition 2, LBA 40..103) FAT32 "
                            "mounted; /MEGAROM.TVL not on it"));
    CHECK(fp.nlog == 5);
    /* a directory of that name is not a file */
    fp.nlog = 0;
    memset(&r, 0, sizeof r);
    hw_reg_blk(&r, &ok.b);
    CHECK(hw_storage_selftest(&fp.plat, &r, "EFI") == 1);
    CHECK(fake_log_has(&fp, "/EFI not on it"));
    /* a file bigger than the read buffer, and a truncated chain */
    fp.nlog = 0;
    p32(ok.d + PSTART * 512 + 2 * 512 + 32 + 28, 5000);
    CHECK(hw_storage_selftest(&fp.plat, &r, "MEGAROM.TVL") == 0);
    CHECK(fake_log_has(&fp, "reading /MEGAROM.TVL failed"));
    fp.nlog = 0;
    p32(ok.d + PSTART * 512 + 2 * 512 + 32 + 28, 1500);
    CHECK(hw_storage_selftest(&fp.plat, &r, "MEGAROM.TVL") == 0);
    CHECK(fake_log_has(&fp, "reading /MEGAROM.TVL failed"));
    fake_plat_free(&fp);
}

int main(void)
{
    test_crc();
    test_gpt();
    test_bdev();
    test_selftest();
    printf("test_hw_storage: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
