/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hw_models.c — the golden discs' model index and its deferred check:
 * a GPT disk with an ESP (/ZXV/MODELS.SHA, /ZXV/MODELS/MDL000.GGU) and a
 * ZXV_SWARM_REASON partition (/MODELS.SHA, /MODELS/MDL001.GGU on a scattered
 * cluster chain), the same split over separate core and swarm disks, then
 * every way a manifest, a partition or a model can be wrong. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "../hw_models.h"

#define NBLK  512u
#define ESP0  40u /* ESP: LBA 40..199 */
#define ESP1  199u
#define SWM0  200u /* swarm: LBA 200..399 */
#define SWM1  399u
#define A_LEN 1500u /* clusters 6, 7, 8 of the ESP */
#define B_LEN 2000u /* clusters 4 -> 130 -> 5 -> 6 of the swarm partition */

typedef struct {
    hw_blk_t b;
    uint8_t d[NBLK * 512];
    int fail_lba;
    uint32_t reads;
} mdisk_t;

static int mread(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    mdisk_t *m = (mdisk_t *) b;
    m->reads++;
    if (lba + n > NBLK ||
        (m->fail_lba >= 0 && (uint64_t) m->fail_lba >= lba && (uint64_t) m->fail_lba < lba + n))
        return -1;
    memcpy(buf, m->d + lba * 512, n * 512);
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

static uint8_t A[A_LEN], B[B_LEN];

static void seal(mdisk_t *m)
{
    uint8_t *h = m->d + 512;
    p32(h + 88, hw_crc32(0, m->d + 2 * 512, 4 * 128));
    p32(h + 16, 0);
    p32(h + 16, hw_crc32(0, h, 92));
}

/* FAT32 volume at LBA base: 1 reserved sector, one FAT of 2 sectors (256
 * entries), 1 sector per cluster, so cluster c is sector 3 + c - 2. */
static uint8_t *vol(mdisk_t *m, uint32_t base)
{
    uint8_t *v = m->d + base * 512;
    p16(v + 11, 512);
    v[13] = 1;
    p16(v + 14, 1);
    v[16] = 1;
    p32(v + 36, 2);
    p32(v + 44, 2);
    p32(v + 512, 0x0FFFFFF8u);
    p32(v + 516, 0x0FFFFFFFu);
    return v;
}
static void fat(uint8_t *v, uint32_t c, uint32_t next)
{
    p32(v + 512 + c * 4, next);
}
static uint8_t *clu(uint8_t *v, uint32_t c)
{
    return v + (3 + c - 2) * 512;
}
static void dirent(uint8_t *d, const char *n11, uint8_t attr, uint32_t c, uint32_t size)
{
    memcpy(d, n11, 11);
    d[11] = attr;
    p16(d + 20, (uint16_t) (c >> 16));
    p16(d + 26, (uint16_t) c);
    p32(d + 28, size);
}

static void hexs(const uint8_t *h, char *out)
{
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

/* Write the core manifest (at most one 512-byte cluster). */
static void manifest(mdisk_t *m, const char *text)
{
    uint8_t *v = m->d + ESP0 * 512;
    size_t n = strlen(text);
    if (n > 512) abort();
    memset(clu(v, 4), 0, 512);
    memcpy(clu(v, 4), text, n);
    p32(clu(v, 3) + 28, (uint32_t) n);
}

/* Write the swarm partition's manifest (cluster 7 of that volume). */
static void manifest_sw(mdisk_t *m, const char *text)
{
    uint8_t *v = m->d + SWM0 * 512;
    size_t n = strlen(text);
    if (n > 512) abort();
    memset(clu(v, 7), 0, 512);
    memcpy(clu(v, 7), text, n);
    p32(clu(v, 2) + 32 + 28, (uint32_t) n);
}

static void good_manifests(mdisk_t *m)
{
    uint8_t ha[32], hb[32];
    char sa[65], sb[65], text[512];
    sha256(A, A_LEN, ha);
    sha256(B, B_LEN, hb);
    hexs(ha, sa);
    hexs(hb, sb);
    snprintf(text, sizeof text, "# core\r\n%s %u qwen-core esp MDL000.GGU\r\n", sa, A_LEN);
    manifest(m, text);
    snprintf(text, sizeof text, "\n  %s %u tiamat-swarm swarm mdl001.ggu\n", sb, B_LEN);
    manifest_sw(m, text);
}

/* GPT entry i (0 ESP, 1 swarm): its name, or retype it away. */
static void part_name(mdisk_t *m, int i, const char *name)
{
    uint8_t *e = m->d + 2 * 512 + i * 128;
    memset(e + 56, 0, 72);
    for (int k = 0; name[k] && k < 36; k++) e[56 + 2 * k] = (uint8_t) name[k];
    seal(m);
}
static void retype(mdisk_t *m, int i)
{
    memcpy(m->d + 2 * 512 + i * 128, LINUX, 16);
    seal(m);
}

static void build(mdisk_t *m)
{
    memset(m, 0, sizeof *m);
    m->fail_lba = -1;
    strcpy(m->b.name, "gold0");
    m->b.block_size = 512;
    m->b.blocks = NBLK;
    m->b.read = mread;
    for (uint32_t i = 0; i < A_LEN; i++) A[i] = (uint8_t) (i * 7 + 1);
    for (uint32_t i = 0; i < B_LEN; i++) B[i] = (uint8_t) (i * 13 + 5 + i / 256);
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
    memcpy(e, ESP, 16);
    p64(e + 32, ESP0);
    p64(e + 40, ESP1);
    memcpy(e + 128, HW_GPT_ZXV_SWARM, 16);
    p64(e + 128 + 32, SWM0);
    p64(e + 128 + 40, SWM1);
    seal(m);

    /* ESP: / -> ZXV (3) -> MODELS.SHA (4), MODELS (5) -> MDL000.GGU (6..8) */
    uint8_t *v = vol(m, ESP0);
    fat(v, 2, 0x0FFFFFFF);
    fat(v, 3, 0x0FFFFFFF);
    fat(v, 4, 0x0FFFFFFF);
    fat(v, 5, 0x0FFFFFFF);
    fat(v, 6, 7);
    fat(v, 7, 8);
    fat(v, 8, 0x0FFFFFFF);
    dirent(clu(v, 2), "ZXV        ", 0x10, 3, 0);
    dirent(clu(v, 3), "MODELS  SHA", 0x20, 4, 0);
    dirent(clu(v, 3) + 32, "MODELS     ", 0x10, 5, 0);
    dirent(clu(v, 5), "MDL000  GGU", 0x20, 6, A_LEN);
    memcpy(clu(v, 6), A, A_LEN);

    /* swarm: / -> MODELS (3) -> MDL001.GGU (4 -> 130 -> 5 -> 6), and
     * MODELS.SHA (7); cluster 130 has its FAT entry in the second FAT
     * sector */
    v = vol(m, SWM0);
    fat(v, 2, 0x0FFFFFFF);
    fat(v, 3, 0x0FFFFFFF);
    fat(v, 7, 0x0FFFFFFF);
    dirent(clu(v, 2) + 32, "MODELS  SHA", 0x20, 7, 0);
    fat(v, 4, 130);
    fat(v, 130, 5);
    fat(v, 5, 6);
    fat(v, 6, 0x0FFFFFFF);
    dirent(clu(v, 2), "MODELS     ", 0x10, 3, 0);
    dirent(clu(v, 3), "MDL001  GGU", 0x20, 4, B_LEN);
    memcpy(clu(v, 4), B, 512);
    memcpy(clu(v, 130), B + 512, 512);
    memcpy(clu(v, 5), B + 1024, 512);
    memcpy(clu(v, 6), B + 1536, B_LEN - 1536);

    good_manifests(m);
    part_name(m, 1, "ZXV_SWARM_REASON");
}

static fake_plat_t fp;
static hw_registry_t reg;
static hw_models_t s;

static uint32_t index_one(mdisk_t *m)
{
    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &m->b);
    return hw_models_index(&s, &fp.plat, &reg);
}

static void test_happy(void)
{
    static mdisk_t m;
    build(&m);
    CHECK(index_one(&m) == 2);
    CHECK(fake_log_has(&fp, "[SWARM_MOUNTED] ZXV_SWARM_REASON on gold0: 1 model"));
    CHECK(fake_log_has(&fp, "[MODELS_INDEXED] 2 listed (1 on the boot partition, 1 on 1 swarm "
                            "disc); each is verified when first used"));
    CHECK(s.m[0].verdict == HW_N && s.m[1].verdict == HW_N);
    CHECK(strcmp(s.m[1].file, "MDL001.GGU") == 0);
    /* indexing hashes nothing: no model cluster was read */
    uint32_t steps = 0;
    while (hw_models_step(&s, &fp.plat, 1)) steps++;
    /* 3 + 4 clusters, one per step; the last model's step ends it */
    CHECK(steps == 7);
    CHECK(fake_log_has(&fp, "[MODEL_OK] qwen-core 1500 bytes, sha256 matches"));
    CHECK(fake_log_has(&fp, "[MODEL_OK] tiamat-swarm 2000 bytes, sha256 matches"));
    CHECK(fake_log_has(&fp, "[MODELS_OK] 2 verified"));
    CHECK(s.m[0].verdict == HW_T && s.m[1].verdict == HW_T);
    uint32_t n = fp.nlog;
    CHECK(hw_models_step(&s, &fp.plat, 1) == 0 && fp.nlog == n); /* reported once */
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_T);
    CHECK(hw_models_require(&s, &fp.plat, "nope") == HW_F);
}

static void test_require(void)
{
    static mdisk_t m;
    build(&m);
    CHECK(index_one(&m) == 2);
    /* first use pays for its own check, by real or 8.3 name */
    CHECK(hw_models_require(&s, &fp.plat, "MDL001.GGU") == HW_T);
    CHECK(s.m[0].verdict == HW_N);
    /* the background check is part-way through model 0 when it is wanted */
    CHECK(hw_models_step(&s, &fp.plat, 1) == 1 && s.bg.idx == 0 && s.bg.done == 512);
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_T);
    CHECK(s.bg.idx == -1);
    CHECK(hw_models_step(&s, &fp.plat, 1) == 0);
    CHECK(fake_log_has(&fp, "[MODELS_OK] 2 verified"));
}

static void run_all(void)
{
    while (hw_models_step(&s, &fp.plat, 3));
}

static void test_bad_models(void)
{
    static mdisk_t m;
    /* one flipped byte in the swarm model, in the cluster past the FAT
     * sector boundary */
    build(&m);
    m.d[(SWM0 + 3 + 128) * 512 + 100] ^= 1;
    CHECK(index_one(&m) == 2);
    run_all();
    CHECK(fake_log_has(&fp, "[MODEL_OK] qwen-core"));
    CHECK(
        fake_log_has(&fp, "[MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) sha256 differs"));
    CHECK(fake_log_has(&fp, "[MODELS_FAIL] 1 of 2 models bad"));
    CHECK(s.m[1].verdict == HW_F);
    CHECK(hw_models_require(&s, &fp.plat, "tiamat-swarm") == HW_F);

    /* directory size disagrees with the manifest */
    build(&m);
    p32(m.d + (ESP0 + 3 + 3) * 512 + 28, A_LEN - 1);
    CHECK(index_one(&m) == 2);
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_F);
    CHECK(fake_log_has(&fp, "[MODEL_BAD] qwen-core (esp:MDL000.GGU) size on disc differs"));

    /* chain cut short: cluster 130 now ends the chain */
    build(&m);
    p32(m.d + (SWM0 + 1) * 512 + 130 * 4, 0x0FFFFFFF);
    CHECK(index_one(&m) == 2);
    run_all();
    CHECK(fake_log_has(&fp,
                       "[MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) cluster chain ends "
                       "before the file does"));
    /* a chain that loops back on itself (4 -> 130 -> 5 -> 4) is read only
     * as far as the size says, then fails its hash */
    build(&m);
    p32(m.d + (SWM0 + 1) * 512 + 5 * 4, 4);
    CHECK(index_one(&m) == 2);
    uint32_t before = m.reads;
    CHECK(hw_models_require(&s, &fp.plat, "tiamat-swarm") == HW_F);
    CHECK(
        fake_log_has(&fp, "[MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) sha256 differs"));
    CHECK(m.reads - before <= 8);
    /* a size no partition of this disk could hold is refused unread */
    build(&m);
    p32(m.d + (SWM0 + 3 + 1) * 512 + 28, 0xFFFFFFFFu);
    char text[512], sb[65];
    uint8_t hb[32];
    sha256(B, B_LEN, hb);
    hexs(hb, sb);
    snprintf(text, sizeof text, "%s 4294967295 tiamat-swarm swarm MDL001.GGU\n", sb);
    manifest_sw(&m, text);
    CHECK(index_one(&m) == 2);
    before = m.reads;
    CHECK(hw_models_require(&s, &fp.plat, "tiamat-swarm") == HW_F);
    CHECK(fake_log_has(&fp,
                       "[MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) is larger than its "
                       "partition"));
    CHECK(m.reads == before);
    /* the largest file the swarm volume holds, 197 clusters, gets as far as
     * reading (and fails its chain at cluster 6) */
    build(&m);
    p32(m.d + (SWM0 + 3 + 1) * 512 + 28, 197u * 512u);
    snprintf(text, sizeof text, "%s %u tiamat-swarm swarm MDL001.GGU\n", sb, 197u * 512u);
    manifest_sw(&m, text);
    CHECK(index_one(&m) == 2);
    CHECK(hw_models_require(&s, &fp.plat, "tiamat-swarm") == HW_F);
    CHECK(fake_log_has(&fp, "cluster chain ends before the file does"));

    /* a cluster the disk will not read */
    build(&m);
    m.fail_lba = ESP0 + 3 + 7 - 2;
    CHECK(index_one(&m) == 2);
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_F);
    CHECK(fake_log_has(&fp, "[MODEL_BAD] qwen-core (esp:MDL000.GGU) a cluster could not be read"));

    /* the file is missing from its directory */
    build(&m);
    memcpy(m.d + (ESP0 + 3 + 3) * 512, "OTHER   BIN", 11);
    CHECK(index_one(&m) == 2);
    run_all();
    CHECK(fake_log_has(&fp, "[MODEL_BAD] qwen-core (esp:MDL000.GGU) not found in /ZXV/MODELS"));
    CHECK(fake_log_has(&fp, "[MODEL_OK] tiamat-swarm"));
    CHECK(fake_log_has(&fp, "[MODELS_FAIL] 1 of 2 models bad"));

    /* no swarm partition: only the core is listed */
    build(&m);
    retype(&m, 1);
    CHECK(index_one(&m) == 1);
    CHECK(fake_log_has(&fp, "[MODELS_INDEXED] 1 listed (1 on the boot partition, 0 on 0 swarm "
                            "discs)"));
    run_all();
    CHECK(s.m[0].verdict == HW_T && fake_log_has(&fp, "[MODELS_OK] 1 verified"));
    /* the swarm model's file is missing from its directory */
    build(&m);
    memcpy(m.d + (SWM0 + 3 + 1) * 512, "OTHER   BIN", 11);
    CHECK(index_one(&m) == 2);
    run_all();
    CHECK(fake_log_has(&fp, "[MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) not found in "
                            "/MODELS on its swarm disc"));
}

static void test_manifests(void)
{
    static mdisk_t m;
    char sa[65];
    uint8_t ha[32];
    sha256(A, A_LEN, ha);
    hexs(ha, sa);
    struct {
        const char *fmt; /* %s = good sha */
        const char *why;
    } bad[] = {
        {"%.63s 1500 a esp A.GGU\n", "line 1: sha256 is not 64 hex digits"},
        {"%.63sg 1500 a esp A.GGU\n", "line 1: sha256 is not 64 hex digits"},
        {"%s 0 a esp A.GGU\n", "line 1: size is 0 or does not fit FAT32"},
        {"%s 4294967296 a esp A.GGU\n", "line 1: size is 0 or does not fit FAT32"},
        {"%s 15x0 a esp A.GGU\n", "line 1: size is not a number"},
        {"#\n%s 1500 a usb A.GGU\n", "line 2: partition is neither esp nor swarm"},
        {"%s 1500 a esp\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp MODELFILE.GGU\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp MODELFILE.GG\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp A.GGUF\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp .GGU\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp A.\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp A/B.GGU\n", "line 1: file is not an 8.3 name"},
        {"%s 1500 a esp A.GGU extra\n", "line 1: extra fields"},
        {"%s 1500\n", "line 1: name missing or too long"},
        {"%s 1500 a\n", "line 1: partition missing"},
        {"%s 1500 a esp A.GGU\n%s 1500 a esp B.GGU\n", "line 2: name or file listed twice"},
        {"%s 1500 a swarm A.GGU\n", "line 1: the core disc lists a swarm model"},
        {"%s 1500 a esp A.GGU\n%s 1500 b esp a.ggu\n", "line 2: name or file listed twice"},
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        build(&m);
        retype(&m, 1);
        char text[512], want[160];
        snprintf(text, sizeof text, bad[i].fmt, sa, sa);
        manifest(&m, text);
        snprintf(want, sizeof want, "[MODELS_FAIL] gold0 /ZXV/MODELS.SHA %s", bad[i].why);
        int rc = (int) index_one(&m);
        if (rc != 0 || !fake_log_has(&fp, want)) printf("  case %u: rc %d, %s\n", i, rc, fp.log[0]);
        CHECK(rc == 0 && fake_log_has(&fp, want));
        CHECK(hw_models_step(&s, &fp.plat, 8) == 0);
    }
    /* the same file name on the core and a swarm disc is fine */
    build(&m);
    {
        char text[512];
        snprintf(text, sizeof text, "%s 1500 a esp A.GGU\n", sa);
        manifest(&m, text);
        snprintf(text, sizeof text, "%s 1500 b swarm A.GGU\n", sa);
        manifest_sw(&m, text);
        CHECK(index_one(&m) == 2);
    }
    /* a name of 64 characters is one too long */
    build(&m);
    retype(&m, 1);
    {
        char text[512], name[65];
        memset(name, 'n', 64);
        name[64] = 0;
        snprintf(text, sizeof text, "%s 1500 %s esp A.GGU\n", sa, name);
        manifest(&m, text);
        CHECK(index_one(&m) == 0 && fake_log_has(&fp, "line 1: name missing or too long"));
        name[63] = 0;
        snprintf(text, sizeof text, "%s 1500 %s esp A.GGU\n", sa, name);
        manifest(&m, text);
        CHECK(index_one(&m) == 1);
    }
    /* more models than the kernel tracks */
    build(&m);
    retype(&m, 1);
    {
        static char big[HW_MODELS_SHA_MAX];
        size_t o = 0;
        for (int i = 0; i <= HW_MODELS_MAX; i++)
            o += (size_t) snprintf(big + o, sizeof big - o, "%s 1 m%d esp M%d.GGU\n", sa, i, i);
        /* longer than one cluster: give the manifest its own chain */
        uint8_t *v = m.d + ESP0 * 512;
        uint32_t clusters = (uint32_t) (o + 511) / 512;
        for (uint32_t c = 0; c < clusters; c++) {
            fat(v, (c ? 20 + c : 4), c + 1 < clusters ? 20 + c + 1 : 0x0FFFFFFF);
            memcpy(clu(v, (c ? 20 + c : 4)), big + c * 512, o - c * 512 < 512 ? o - c * 512 : 512);
        }
        p32(clu(v, 3) + 28, (uint32_t) o);
        CHECK(index_one(&m) == 0);
        CHECK(fake_log_has(&fp, "line 33: more models than the kernel tracks"));
    }
    /* comments only: no models */
    build(&m);
    retype(&m, 1);
    manifest(&m, "# empty\n");
    CHECK(index_one(&m) == 0 &&
          fake_log_has(&fp, "[MODELS_NONE] the attached discs list no usable models"));
    /* manifest larger than the kernel reads */
    build(&m);
    retype(&m, 1);
    p32(m.d + (ESP0 + 3 + 1) * 512 + 28, HW_MODELS_SHA_MAX + 1);
    CHECK(index_one(&m) == 0 && fake_log_has(&fp, "MODELS.SHA: larger than the kernel reads"));
    /* manifest unreadable */
    build(&m);
    retype(&m, 1);
    m.fail_lba = ESP0 + 3 + 2;
    CHECK(index_one(&m) == 0 && fake_log_has(&fp, "MODELS.SHA: could not be read"));
}

static void test_no_disc(void)
{
    static mdisk_t m, plain;
    /* no core manifest and no swarm partition */
    build(&m);
    retype(&m, 1);
    memcpy(m.d + (ESP0 + 3 + 1) * 512, "README  TXT", 11);
    CHECK(index_one(&m) == 0 && fake_log_has(&fp, "[MODELS_NONE] no core or swarm disc attached"));
    CHECK(hw_models_step(&s, &fp.plat, 8) == 0);
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_F);
    /* the core disc is found behind a disk without one */
    build(&plain);
    memset(plain.d + 512, 0, 512);
    strcpy(plain.b.name, "plain0");
    build(&m);
    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &plain.b);
    hw_reg_blk(&reg, &m.b);
    CHECK(hw_models_index(&s, &fp.plat, &reg) == 2 && s.vol[0].disk == &m.b);
}

/* A core disk and separate swarm disks, as the discs ship. */
static void test_swarm_discs(void)
{
    static mdisk_t core, reason, vision, dup, odd, bare, noman, espsw, many[HW_MAX_BLK];
    char text[512], sb[65];
    uint8_t hb[32];
    build(&core);
    retype(&core, 1);
    strcpy(core.b.name, "nvme0");
    build(&reason);
    retype(&reason, 0);
    strcpy(reason.b.name, "sata1");
    /* a second group: same bytes under another name and file */
    build(&vision);
    retype(&vision, 0);
    strcpy(vision.b.name, "sata2");
    part_name(&vision, 1, "ZXV_SWARM_VISION");
    sha256(B, B_LEN, hb);
    hexs(hb, sb);
    snprintf(text, sizeof text, "%s %u smol-vision swarm MDL001.GGU\n", sb, B_LEN);
    manifest_sw(&vision, text);
    /* a third disc repeating a model name another disc already listed */
    build(&dup);
    retype(&dup, 0);
    strcpy(dup.b.name, "usb0");
    part_name(&dup, 1, "ZXV_SWARM_AUDIO");
    /* partitions of the swarm type with the wrong names */
    build(&odd);
    retype(&odd, 0);
    strcpy(odd.b.name, "usb1");
    part_name(&odd, 1, "DATA");
    odd.d[2 * 512 + 128 + 56 + 2] = 0xE9; /* U+00E9 is shown as '?' */
    seal(&odd);
    build(&bare);
    retype(&bare, 0);
    strcpy(bare.b.name, "usb2");
    part_name(&bare, 1, "ZXV_SWARM_");
    /* a swarm disc without a manifest, and one listing an esp model */
    build(&noman);
    retype(&noman, 0);
    strcpy(noman.b.name, "usb3");
    part_name(&noman, 1, "ZXV_SWARM_EMPTY");
    memcpy(noman.d + (SWM0 + 3) * 512 + 32, "README  TXT", 11);
    build(&espsw);
    retype(&espsw, 0);
    strcpy(espsw.b.name, "usb4");
    part_name(&espsw, 1, "ZXV_SWARM_ODD");
    snprintf(text, sizeof text, "%s %u odd-one esp MDL001.GGU\n", sb, B_LEN);
    manifest_sw(&espsw, text);

    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &reason.b); /* swarm before core: order does not matter */
    hw_reg_blk(&reg, &core.b);
    hw_reg_blk(&reg, &vision.b);
    hw_reg_blk(&reg, &dup.b);
    hw_reg_blk(&reg, &odd.b);
    hw_reg_blk(&reg, &bare.b);
    hw_reg_blk(&reg, &noman.b);
    hw_reg_blk(&reg, &espsw.b);
    CHECK(hw_models_index(&s, &fp.plat, &reg) == 3);
    CHECK(fake_log_has(&fp, "[SWARM_MOUNTED] ZXV_SWARM_REASON on sata1: 1 model"));
    CHECK(fake_log_has(&fp, "[SWARM_MOUNTED] ZXV_SWARM_VISION on sata2: 1 model"));
    CHECK(fake_log_has(&fp, "[MODELS_FAIL] ZXV_SWARM_AUDIO on usb0 /MODELS.SHA line 2: name or "
                            "file listed twice"));
    CHECK(
        fake_log_has(&fp, "[SWARM_SKIPPED] usb1 partition 'D?TA' is not named ZXV_SWARM_<group>"));
    CHECK(fake_log_has(&fp, "[SWARM_SKIPPED] usb2 partition 'ZXV_SWARM_' is not named"));
    CHECK(fake_log_has(&fp, "[MODELS_FAIL] ZXV_SWARM_EMPTY on usb3 /MODELS.SHA: missing"));
    CHECK(fake_log_has(&fp, "[MODELS_FAIL] ZXV_SWARM_ODD on usb4 /MODELS.SHA line 1: a swarm disc "
                            "lists an esp model"));
    CHECK(fake_log_has(&fp, "[MODELS_INDEXED] 3 listed (1 on the boot partition, 2 on 2 swarm "
                            "discs)"));
    CHECK(s.vol[0].disk == &core.b);
    run_all();
    CHECK(fake_log_has(&fp, "[MODEL_OK] qwen-core"));
    CHECK(fake_log_has(&fp, "[MODEL_OK] tiamat-swarm"));
    CHECK(fake_log_has(&fp, "[MODEL_OK] smol-vision"));
    CHECK(fake_log_has(&fp, "[MODELS_OK] 3 verified"));
    /* each disc is read from its own disk */
    vision.d[(SWM0 + 3 + 128) * 512 + 7] ^= 1;
    fp.nlog = 0;
    CHECK(hw_models_index(&s, &fp.plat, &reg) == 3);
    run_all();
    CHECK(fake_log_has(&fp, "[MODEL_OK] tiamat-swarm"));
    CHECK(
        fake_log_has(&fp, "[MODEL_BAD] smol-vision (ZXV_SWARM_VISION:MDL001.GGU) sha256 differs"));
    /* swarm discs alone, with no core disc, still count */
    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &reason.b);
    CHECK(hw_models_index(&s, &fp.plat, &reg) == 1);
    CHECK(fake_log_has(&fp, "(0 on the boot partition, 1 on 1 swarm disc)"));
    /* a swarm disc in every registry slot: each gets its own volume */
    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    for (int i = 0; i < HW_MAX_BLK; i++) {
        build(&many[i]);
        retype(&many[i], 0);
        snprintf(many[i].b.name, sizeof many[i].b.name, "d%d", i);
        snprintf(text, sizeof text, "ZXV_SWARM_G%d", i);
        part_name(&many[i], 1, text);
        snprintf(text, sizeof text, "%s %u m%d swarm MDL001.GGU\n", sb, B_LEN, i);
        manifest_sw(&many[i], text);
        hw_reg_blk(&reg, &many[i].b);
    }
    CHECK(hw_models_index(&s, &fp.plat, &reg) == HW_MAX_BLK && s.nvol == HW_MODELS_VOLS);
    CHECK(fake_log_has(&fp, "[SWARM_MOUNTED] ZXV_SWARM_G7 on d7: 1 model"));
    run_all();
    CHECK(fake_log_has(&fp, "[MODELS_OK] 8 verified"));
}

/* A file in the core disc's /ZXV: directory slot `slot` (2 or 3, after
 * MODELS.SHA and MODELS), one cluster `c`, `size` (the text's length when
 * 0). NULL text removes the slot. */
static void side(mdisk_t *m, int slot, const char *n11, uint32_t c, const char *text, uint32_t size)
{
    uint8_t *v = m->d + ESP0 * 512;
    uint8_t *d = clu(v, 3) + 32 * slot;
    memset(d, 0, 32);
    if (!text) return;
    size_t n = strlen(text);
    if (n > 512) abort();
    fat(v, c, 0x0FFFFFFF);
    memset(clu(v, c), 0, 512);
    memcpy(clu(v, c), text, n);
    dirent(d, n11, 0x20, c, size ? size : (uint32_t) n);
}

#define CID  "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354"
#define IPNS "k51qzi5uqu5dlvj2baxnqndepeb86cbk3ng7n3i84ftsqs2zsxwn4s3ggzpu9b"
#define LST                                                                                        \
    "# disc GROUP image\n2 REASON zxv-swarm-disc2-reason.img\n3 VISION "                           \
    "zxv-swarm-disc3-vision.img\n"

static mdisk_t ucore, ureason, uvision;

/* Index the core disk with the reason disk and, if `all`, the vision disk. */
static uint32_t index_up(const char *up, const char *lst, int all)
{
    side(&ucore, 2, "UPSTREAMCID", 9, up, 0);
    side(&ucore, up ? 3 : 2, "SWARM   LST", 10, lst, 0);
    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &ucore.b);
    hw_reg_blk(&reg, &ureason.b);
    if (all) hw_reg_blk(&reg, &uvision.b);
    return hw_models_index(&s, &fp.plat, &reg);
}

/* /ZXV/UPSTREAM.CID and /ZXV/SWARM.LST on the core disc. */
static void test_upstream(void)
{
    char text[512], sb[65];
    uint8_t hb[32];
    build(&ucore);
    retype(&ucore, 1);
    strcpy(ucore.b.name, "sata0");
    build(&ureason);
    retype(&ureason, 0);
    strcpy(ureason.b.name, "sata1");
    build(&uvision);
    retype(&uvision, 0);
    strcpy(uvision.b.name, "sata2");
    part_name(&uvision, 1, "ZXV_SWARM_VISION");
    sha256(B, B_LEN, hb);
    hexs(hb, sb);
    snprintf(text, sizeof text, "%s %u smol-vision swarm MDL001.GGU\n", sb, B_LEN);
    manifest_sw(&uvision, text);

    /* the bucket, and one of two swarm discs attached */
    CHECK(index_up("# master bucket\r\ncid " CID " # 00_base onward\r\n", LST, 0) == 2);
    CHECK(fake_log_has(&fp, "[UPSTREAM] bucket " CID));
    CHECK(!fake_log_has(&fp, "ipns"));
    CHECK(s.nwant == 2 && s.want[0].disc == 2 && strcmp(s.want[1].group, "VISION") == 0);
    CHECK(fake_log_has(&fp, "[SWARM_MISSING] disc 3 ZXV_SWARM_VISION (zxv-swarm-disc3-vision.img)"
                            ": attach it, or zxvd fetches it from the upstream bucket"));
    CHECK(!fake_log_has(&fp, "ZXV_SWARM_REASON (zxv"));
    CHECK(!fake_log_has(&fp, "[SWARM_COMPLETE]"));
    CHECK(fake_log_has(&fp, "[MODELS_INDEXED] 2 listed"));
    /* both attached, and an IPNS name ahead of the CID */
    CHECK(index_up("ipns " IPNS "\ncid " CID "\n", LST, 1) == 3);
    CHECK(fake_log_has(&fp, "[UPSTREAM] ipns " IPNS ", fallback bucket " CID));
    CHECK(fake_log_has(&fp, "[SWARM_COMPLETE] all 2 swarm discs the core expects are attached"));
    CHECK(!fake_log_has(&fp, "[SWARM_MISSING]"));
    CHECK(index_up(0, "2 REASON zxv-swarm-disc2-reason.img\n", 0) == 2);
    CHECK(fake_log_has(&fp, "[SWARM_COMPLETE] all 1 swarm disc the core expects is attached"));
    /* no upstream file: a missing disc can only be attached */
    CHECK(index_up(0, LST, 0) == 2);
    CHECK(s.cid[0] == 0 && !fake_log_has(&fp, "[UPSTREAM]"));
    CHECK(fake_log_has(&fp, "(zxv-swarm-disc3-vision.img): attach it"));
    CHECK(!fake_log_has(&fp, "zxvd"));
    /* neither file: nothing is expected, nothing reported */
    CHECK(index_up(0, 0, 0) == 2);
    CHECK(s.nwant == 0 && !fake_log_has(&fp, "[SWARM_MISSING]"));
    CHECK(!fake_log_has(&fp, "[SWARM_COMPLETE]"));

    static const struct {
        const char *text, *log;
    } up[] = {
        {"cid\n", "UPSTREAM.CID line 1: a key without a value"},
        {"cid " CID " extra\n", "UPSTREAM.CID line 1: extra fields"},
        {"\ncid B" CID "\n", "UPSTREAM.CID line 2: cid is not a base32 CIDv1"},
        {"cid bafy1" CID "\n", "line 1: cid is not a base32 CIDv1"},
        {"cid bafybeic\n", "line 1: cid is not a base32 CIDv1"},
        {"cid " CID CID CID "\n", "line 1: cid is not a base32 CIDv1"},
        {"cid " CID "\ncid " CID "\n", "line 2: cid listed twice"},
        {"cid " CID "\nipns " IPNS "\nipns " IPNS "\n", "line 3: ipns listed twice"},
        {"cid " CID "\nipns k51_" IPNS "\n", "line 2: ipns is not a base36 name"},
        {"cid " CID "\nipns " CID "\n", "line 2: ipns is not a base36 name"},
        {"url https://example\n", "line 1: unknown key (cid|ipns)"},
        {"cids " CID "\n", "line 1: unknown key (cid|ipns)"},
        {"ipns " IPNS "\n", "sata0 /ZXV/UPSTREAM.CID: no cid line"},
        {"# empty\n", "UPSTREAM.CID: no cid line"},
    };
    for (unsigned i = 0; i < sizeof up / sizeof up[0]; i++) {
        uint32_t n = index_up(up[i].text, LST, 0);
        int ok = n == 2 && fake_log_has(&fp, up[i].log) && s.cid[0] == 0 && s.ipns[0] == 0 &&
                 !fake_log_has(&fp, "[UPSTREAM]") &&
                 fake_log_has(&fp, "(zxv-swarm-disc3-vision.img): attach it") &&
                 !fake_log_has(&fp, "zxvd");
        if (!ok) printf("  upstream case %u: %s\n", i, up[i].text);
        CHECK(ok);
    }

    static const struct {
        const char *text, *log;
    } lst[] = {
        {"1 REASON a.img\n", "SWARM.LST line 1: disc is not a number from 2"},
        {"2a REASON a.img\n", "line 1: disc is not a number from 2"},
        {"1000 REASON a.img\n", "line 1: disc is not a number from 2"},
        {"#\n\n2 reason a.img\n", "line 3: group is not 1-16 of A-Z 0-9"},
        {"2 ABCDEFGHIJKLMNOPQ a.img\n", "line 1: group is not 1-16 of A-Z 0-9"},
        {"2 REASON\n", "line 1: image name missing or too long"},
        {"2 REASON zxv-swarm-disc2-reason-with-a-name-too-long-for-it.img\n",
         "line 1: image name missing or too long"},
        {"2 REASON a.img b\n", "line 1: extra fields"},
        {"2 REASON a.img\n3 REASON b.img\n", "line 2: group listed twice"},
    };
    for (unsigned i = 0; i < sizeof lst / sizeof lst[0]; i++) {
        uint32_t n = index_up("cid " CID "\n", lst[i].text, 0);
        int ok = n == 2 && fake_log_has(&fp, lst[i].log) && s.nwant == 0 &&
                 fake_log_has(&fp, "[UPSTREAM] bucket " CID) &&
                 !fake_log_has(&fp, "[SWARM_MISSING]") && !fake_log_has(&fp, "[SWARM_COMPLETE]");
        if (!ok) printf("  swarm list case %u: %s\n", i, lst[i].text);
        CHECK(ok);
    }
    /* more discs than the kernel tracks; the last one it can hold */
    char many[512] = "";
    for (int i = 0; i < HW_SWARM_WANT_MAX; i++) {
        char l[64];
        snprintf(l, sizeof l, "%d G%d g%d.img\n", i + 2, i, i);
        strcat(many, l);
    }
    CHECK(index_up(0, many, 0) == 2 && s.nwant == HW_SWARM_WANT_MAX);
    CHECK(fake_log_has(&fp, "[SWARM_MISSING] disc 17 ZXV_SWARM_G15 (g15.img): attach it"));
    strcat(many, "18 G16 g16.img\n");
    CHECK(index_up(0, many, 0) == 2 && s.nwant == 0);
    CHECK(fake_log_has(&fp, "SWARM.LST line 17: more swarm discs than the kernel tracks"));
    /* too large to read */
    side(&ucore, 2, "UPSTREAMCID", 9, "cid " CID "\n", 4096);
    side(&ucore, 3, "SWARM   LST", 10, LST, 0);
    fp.nlog = 0;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &ucore.b);
    CHECK(hw_models_index(&s, &fp.plat, &reg) == 1 && s.cid[0] == 0 && s.nwant == 2);
    CHECK(fake_log_has(&fp, "sata0 /ZXV/UPSTREAM.CID: unreadable or larger than the kernel reads"));
}

int main(void)
{
    fake_plat_init(&fp);
    test_happy();
    test_require();
    test_bad_models();
    test_manifests();
    test_no_disc();
    test_swarm_discs();
    test_upstream();
    fake_plat_free(&fp);
    printf("test_hw_models: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
