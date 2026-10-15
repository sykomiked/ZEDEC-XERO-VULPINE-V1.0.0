/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarmnet.c — the parts of swarmnet that need no network: gateway URLs,
 * HTTP heads, manifest.txt, the blank-drive rule, the writer that commits the
 * partition table last, and the whole fetch of a swarm disc from an in-memory
 * bucket (built with the Kubo-checked test builder) onto a fake drive, with
 * every way the bucket can lie. The network path itself is proven under QEMU
 * (build_system/test_swarm_fetch.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swarmnet.h"
#include "../ipfs_node/test_ipfsn_dirbuild.h"

static int checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

/* ---- a console that remembers ---- */
static char logs[256][160];
static int nlog;
static void log_line(const char *s)
{
    if (nlog < 256) snprintf(logs[nlog++], sizeof logs[0], "%s", s);
}
static int has(const char *s)
{
    for (int i = 0; i < nlog; i++)
        if (strstr(logs[i], s)) return 1;
    return 0;
}
static void dump(void)
{
    for (int i = 0; i < nlog; i++) printf("  | %s\n", logs[i]);
}
static hw_plat_t plat = {.log = log_line};

/* ---- a drive in memory ---- */
#define DRIVE_SECTORS 4096u /* 2 MiB */
typedef struct {
    hw_blk_t b;
    uint8_t d[DRIVE_SECTORS * 512u];
    int fail_write_lba;
    uint32_t writes, last_write_lba;
} drive_t;

static int dread(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    drive_t *d = (drive_t *) b;
    if (lba + n > b->blocks) return -1;
    memcpy(buf, d->d + lba * 512u, n * 512u);
    return 0;
}
static int dwrite(hw_blk_t *b, uint64_t lba, uint32_t n, const void *buf)
{
    drive_t *d = (drive_t *) b;
    if (lba + n > b->blocks) return -1;
    if (d->fail_write_lba >= 0 && (uint64_t) d->fail_write_lba >= lba &&
        (uint64_t) d->fail_write_lba < lba + n)
        return -1;
    memcpy(d->d + lba * 512u, buf, n * 512u);
    d->writes++;
    d->last_write_lba = (uint32_t) lba;
    return 0;
}
static void drive(drive_t *d, const char *name, uint32_t sectors)
{
    memset(d, 0, sizeof *d);
    snprintf(d->b.name, sizeof d->b.name, "%s", name);
    d->b.block_size = 512;
    d->b.blocks = sectors;
    d->b.read = dread;
    d->b.write = dwrite;
    d->fail_write_lba = -1;
}
static int zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return 0;
    return 1;
}

/* ---- 1. URLs and HTTP heads ---- */
static void test_url(void)
{
    char host[64];
    uint16_t port = 0;
    CHECK(sn_url_parse("http://10.0.2.2:8080", host, sizeof host, &port) && port == 8080 &&
          strcmp(host, "10.0.2.2") == 0);
    CHECK(sn_url_parse("http://gw.example/", host, sizeof host, &port) && port == 80 &&
          strcmp(host, "gw.example") == 0);
    CHECK(!sn_url_parse("https://trustless-gateway.link", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://a:0", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://a:65536", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://a:", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://a/ipfs", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://a_b", host, sizeof host, &port));
    CHECK(!sn_url_parse("http://abc", host, 3, &port));
    CHECK(sn_url_parse("http://abc", host, 4, &port));
}

static int32_t head(const char *s, sn_head_t *h)
{
    return sn_http_head((const uint8_t *) s, (uint32_t) strlen(s), h);
}

static void test_head(void)
{
    sn_head_t h;
    const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.ipld.raw\r\n"
                     "content-length:  1234 \r\n\r\nBODY";
    CHECK(head(ok, &h) == (int32_t) strlen(ok) - 4 && h.status == 200 && h.has_length &&
          h.length == 1234 && !h.chunked && !h.close);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n", &h) == 0);
    CHECK(head("HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\n", &h) > 0 && h.status == 404 &&
          h.close && !h.has_length);
    CHECK(head("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n", &h) > 0 && h.chunked);
    CHECK(head("HTTP/1.1 200\r\nContent-Length: 1\r\n\r\n", &h) > 0 && h.status == 200);
    CHECK(head("HTTP/2 200 OK\r\n\r\n", &h) == -1);
    CHECK(head("HTTP/1.1 2x0 OK\r\n\r\n", &h) == -1);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 1a\r\n\r\n", &h) == -1);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: \r\n\r\n", &h) == -1);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n", &h) == -1);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n", &h) > 0);
    CHECK(head("HTTP/1.1 200 OK\r\nContent-Length: 1234567890123\r\n\r\n", &h) == -1);
    CHECK(head("HTTP/1.1 200 OK\nX: y\r\n\r\n", &h) > 0); /* a bare LF ends the status line */
    CHECK(head("HTTP/1.1 200 OK\r\nX: y\rz\r\n\r\n", &h) == -1);
}

/* ---- 2. manifest.txt ---- */
#define SHA_A "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
#define CID_A "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354"

static void test_manifest(void)
{
    sn_entry_t e;
    uint32_t line;
    char t[2048];
    snprintf(t, sizeof t,
             "# zxv-bucket 1\ncore x86_64 1 - 546308096 " SHA_A " " CID_A
             " 00_base/zxv-x86_64-disc1-core.img\r\n"
             "swarm all 2 REASON 1138753536 " SHA_A " " CID_A
             " 01_swarm_reason/zxv-swarm-disc2-reason.img\n"
             "cartridge all - - 5000 " SHA_A " " CID_A " 03_cartridges/MEGAROM.TVL\n");
    CHECK(sn_manifest_find(t, (uint32_t) strlen(t), 2, "REASON", &e, &line) == 0 && e.disc == 2 &&
          e.bytes == 1138753536u && e.sha[0] == 0x00 && e.sha[31] == 0xff &&
          strcmp(e.cid, CID_A) == 0 &&
          strcmp(e.path, "01_swarm_reason/zxv-swarm-disc2-reason.img") == 0);
    CHECK(sn_manifest_find(t, (uint32_t) strlen(t), 3, "REASON", &e, &line) != 0);
    CHECK(sn_manifest_find(t, (uint32_t) strlen(t), 2, "VISION", &e, &line) != 0);
    static const struct {
        const char *line, *why;
    } bad[] = {
        {"blob all 2 R 1 " SHA_A " " CID_A " a/b\n", "unknown kind"},
        {"swarm all 2 R 1 " SHA_A " " CID_A "\n", "a swarm line needs 8 fields"},
        {"swarm all 2 R 1 " SHA_A " " CID_A " a/b x\n", "extra fields"},
        {"swarm all 2x R 1 " SHA_A " " CID_A " a/b\n", "disc is not a number"},
        {"swarm x86_64 2 R 1 " SHA_A " " CID_A " a/b\n", "a swarm disc must be for arch all"},
        {"swarm all 2 R 0 " SHA_A " " CID_A " a/b\n", "bytes is not a size up to 1.8 GB"},
        {"swarm all 2 R 1800000001 " SHA_A " " CID_A " a/b\n", "bytes is not a size up to 1.8 GB"},
        {"swarm all 2 R 1k " SHA_A " " CID_A " a/b\n", "bytes is not a size"},
        {"swarm all 2 R 1 00 " CID_A " a/b\n", "sha256 is not 64 lower-case hex digits"},
        {"swarm all 2 R 1 " SHA_A "" CID_A " a/b x\n", "sha256 is not 64 lower-case hex digits"},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const char *why =
            sn_manifest_find(bad[i].line, (uint32_t) strlen(bad[i].line), 2, "R", &e, &line);
        int ok = why && strcmp(why, bad[i].why) == 0;
        if (!ok) printf("  manifest case %u: %s\n", i, why ? why : "(accepted)");
        CHECK(ok);
    }
    char up[sizeof SHA_A];
    strcpy(up, SHA_A);
    up[0] = 'A';
    snprintf(t, sizeof t, "swarm all 2 R 1 %s " CID_A " a/b\n", up);
    CHECK(sn_manifest_find(t, (uint32_t) strlen(t), 2, "R", &e, &line) != 0);
}

/* ---- 3. blank drives and the writer ---- */
static drive_t d1, d2;

static void test_writer(void)
{
    static uint8_t img[300000], sector[512], sha[32];
    for (uint32_t i = 0; i < sizeof img; i++) img[i] = (uint8_t) (i * 7 + (i >> 9) + 1);
    sha256(img, sizeof img, sha);
    drive(&d1, "sata1", DRIVE_SECTORS);
    CHECK(sn_drive_blank(&d1.b, sizeof img, sector));
    CHECK(!sn_drive_blank(&d1.b, (uint64_t) DRIVE_SECTORS * 512u + 1, sector));
    d1.b.block_size = 4096;
    CHECK(!sn_drive_blank(&d1.b, sizeof img, sector));
    d1.b.block_size = 512;
    d1.d[510] = 0x55;
    d1.d[511] = 0xAA;
    CHECK(!sn_drive_blank(&d1.b, sizeof img, sector));
    d1.d[510] = d1.d[511] = 0;
    memcpy(d1.d + 512, "EFI PART", 8);
    CHECK(!sn_drive_blank(&d1.b, sizeof img, sector));
    memset(d1.d + 512, 0, 8);

    /* streamed in uneven pieces: nothing in the first 34 sectors until commit */
    static sn_writer_t w;
    const char *why = 0;
    sn_writer_init(&w, &d1.b, sizeof img);
    for (uint32_t off = 0; off < sizeof img;) {
        uint32_t k = 1 + (off * 31u) % 70001u;
        if (k > sizeof img - off) k = (uint32_t) sizeof img - off;
        CHECK(sn_writer_put(&w, img + off, k) == 0);
        off += k;
    }
    CHECK(zero(d1.d, SN_HOLD_SECTORS * 512u));
    CHECK(sn_writer_commit(&w, sha, &why) == 0 && memcmp(d1.d, img, sizeof img) == 0);
    CHECK(d1.last_write_lba == 0); /* the head went last */

    /* a wrong SHA-256: the rest is on the drive, the head never is */
    drive(&d1, "sata1", DRIVE_SECTORS);
    sn_writer_init(&w, &d1.b, sizeof img);
    CHECK(sn_writer_put(&w, img, sizeof img) == 0);
    sha[5] ^= 1;
    CHECK(sn_writer_commit(&w, sha, &why) != 0 &&
          strcmp(why, "sha256 differs from the manifest") == 0);
    sha[5] ^= 1;
    CHECK(zero(d1.d, SN_HOLD_SECTORS * 512u) && sn_drive_blank(&d1.b, sizeof img, sector));
    /* short */
    drive(&d1, "sata1", DRIVE_SECTORS);
    sn_writer_init(&w, &d1.b, sizeof img);
    CHECK(sn_writer_put(&w, img, sizeof img - 1) == 0);
    CHECK(sn_writer_commit(&w, sha, &why) != 0 &&
          strcmp(why, "size differs from the manifest") == 0);
    CHECK(zero(d1.d, SN_HOLD_SECTORS * 512u));
    /* long: refused at the byte that overruns */
    drive(&d1, "sata1", DRIVE_SECTORS);
    sn_writer_init(&w, &d1.b, sizeof img - 1);
    CHECK(sn_writer_put(&w, img, sizeof img) != 0 && sn_writer_commit(&w, sha, &why) != 0);
    CHECK(zero(d1.d, SN_HOLD_SECTORS * 512u));
    /* a failed drive write */
    drive(&d1, "sata1", DRIVE_SECTORS);
    d1.fail_write_lba = 200;
    sn_writer_init(&w, &d1.b, sizeof img);
    CHECK(sn_writer_put(&w, img, sizeof img) != 0 && sn_writer_commit(&w, sha, &why) != 0 &&
          strcmp(why, "a drive write failed") == 0);
    CHECK(zero(d1.d, SN_HOLD_SECTORS * 512u));
    /* an image smaller than the held head */
    drive(&d1, "sata1", DRIVE_SECTORS);
    sha256(img, 1000, sha);
    sn_writer_init(&w, &d1.b, 1000);
    CHECK(sn_writer_put(&w, img, 1000) == 0 && sn_writer_commit(&w, sha, &why) == 0 &&
          memcmp(d1.d, img, 1000) == 0 && zero(d1.d + 1000, 24));
}

/* ---- 4. the fetch, from an in-memory bucket ---- */
static tb_store_t store;
static uint8_t image[1100000];
static uint32_t zero_leaves; /* full zero leaves the source was asked for */
static uint8_t isha[32];
static ipfsn_cid_t root;
static hw_models_t models;
static hw_registry_t reg;
static sn_fetch_t fetch;
static int tamper_leaf;

/* tb_get, optionally changing one byte of the image's raw leaves */
static int get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    int rc = tb_get(ctx, cid, buf, cap, len);
    if (rc == IPFSN_OK && *len == 262144u && zero(buf, 262144u)) zero_leaves++;
    if (rc == IPFSN_OK && tamper_leaf && cid->codec == 0x55 && *len == 262144u) buf[100] ^= 1;
    return rc;
}

/* A bucket: manifest.txt and 01_swarm_test/zxv-swarm-disc2-test.img, with
 * the manifest line given (NULL for the true one). */
static void bucket(const char *line_override, int image_cid_wrong)
{
    char man[1024], cs[IPFSN_CID_STR_MAX], hex[65];
    ipfsn_cid_t icid, mcid, scid, other;
    uint64_t its, mts, sts;
    tb_free(&store);
    tb_init(&store);
    tb_file(&store, image, sizeof image, &icid, &its);
    tb_file(&store, (const uint8_t *) "x", 1, &other, 0);
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", isha[i]);
    ipfsn_cid_to_string(image_cid_wrong ? &other : &icid, cs, sizeof cs);
    if (line_override)
        snprintf(man, sizeof man, "%s", line_override);
    else
        snprintf(
            man, sizeof man,
            "# zxv-bucket 1\nswarm all 2 TEST %u %s %s 01_swarm_test/zxv-swarm-disc2-test.img\n",
            (unsigned) sizeof image, hex, cs);
    tb_file(&store, (const uint8_t *) man, (uint32_t) strlen(man), &mcid, &mts);
    tb_ent_t sub[1], top[2];
    tb_ent(&sub[0], "zxv-swarm-disc2-test.img", &icid, its);
    tb_dir(&store, sub, 1, &scid, &sts);
    tb_ent(&top[0], "manifest.txt", &mcid, mts);
    tb_ent(&top[1], "01_swarm_test", &scid, sts);
    tb_dir(&store, top, 2, &root, 0);
}

static uint32_t run(void)
{
    memset(&models, 0, sizeof models);
    ipfsn_cid_to_string(&root, models.cid, sizeof models.cid);
    models.nwant = 2;
    models.want[0] = (hw_swarm_want_t){2, "TEST", "zxv-swarm-disc2-test.img", 0};
    models.want[1] = (hw_swarm_want_t){3, "ATTACHED", "x.img", 1};
    memset(&fetch, 0, sizeof fetch);
    fetch.p = &plat;
    fetch.get = get;
    fetch.get_ctx = &store;
    nlog = 0;
    return sn_fetch_missing(&fetch, &models, &reg);
}

static void test_fetch(void)
{
    static drive_t used, full, small;
    for (uint32_t i = 0; i < sizeof image; i++) image[i] = (uint8_t) (i * 13 + (i >> 11));
    memset(image + 262144, 0, 2 * 262144); /* leaves 1 and 2: identical padding */
    image[510] = 0x55;                     /* a disc image starts with its protective MBR */
    image[511] = 0xAA;
    sha256(image, sizeof image, isha);
    drive(&used, "sata0", DRIVE_SECTORS); /* not blank: an MBR */
    used.d[510] = 0x55;
    used.d[511] = 0xAA;
    drive(&small, "sata1", 1000);
    drive(&d2, "sata2", DRIVE_SECTORS);
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &used.b);
    hw_reg_blk(&reg, &small.b);
    hw_reg_blk(&reg, &d2.b);

    bucket(0, 0);
    zero_leaves = 0;
    CHECK(run() == 1);
    CHECK(zero_leaves == 1); /* the second zero leaf came from the cache */
    CHECK(has("[SWARM_FETCH] disc 2 ZXV_SWARM_TEST -> sata2"));
    CHECK(has("[SWARM_FETCH] disc 2: 20% (262144 of 1100000 bytes)"));
    CHECK(has("[SWARM_FETCHED] disc 2 ZXV_SWARM_TEST on sata2: 1100000 bytes, sha256 matches the "
              "manifest; committed"));
    CHECK(!has("ATTACHED"));
    CHECK(memcmp(d2.d, image, sizeof image) == 0);
    if (failures) dump();

    /* the drive now holds a disc: a second run finds no blank drive */
    CHECK(run() == 0 && has("[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_TEST: no blank drive"));
    /* a drive the system already uses is never chosen, blank or not */
    drive(&d2, "sata2", DRIVE_SECTORS);
    memset(&models, 0, sizeof models);
    (void) full;

    static const struct {
        const char *what;
        const char *line;
        int cid_wrong, tamper;
        const char *log;
    } bad[] = {
        {"a block changed in transit", 0, 0, 1, "the download failed; nothing committed"},
        {"the manifest's CID is another file", 0, 1, 0,
         "the bucket's directory and manifest.txt disagree on its CID"},
        {"no line for the disc", "core x86_64 1 - 5 " SHA_A " " CID_A " a/b\n", 0, 0,
         "manifest.txt: the bucket has no such swarm disc"},
        {"a malformed line", "swarm all 2 TEST 5\n", 0, 0, "manifest.txt: a swarm line needs 8"},
        {"a path that is not in the bucket",
         "swarm all 2 TEST 1100000 " SHA_A " " CID_A " 01_swarm_test/other.img\n", 0, 0,
         "the bucket has no file at manifest.txt's path"},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        drive(&d2, "sata2", DRIVE_SECTORS);
        bucket(bad[i].line, bad[i].cid_wrong);
        tamper_leaf = bad[i].tamper;
        uint32_t n = run();
        tamper_leaf = 0;
        int ok = n == 0 && has(bad[i].log) && has("[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_TEST") &&
                 zero(d2.d, SN_HOLD_SECTORS * 512u);
        if (!ok) {
            printf("  fetch case %u (%s)\n", i, bad[i].what);
            dump();
        }
        CHECK(ok);
    }
    /* the manifest's SHA-256 is wrong: written, never committed */
    {
        char line[512], cs[IPFSN_CID_STR_MAX];
        ipfsn_cid_t icid;
        uint64_t ts;
        tb_file(&store, image, sizeof image, &icid, &ts);
        ipfsn_cid_to_string(&icid, cs, sizeof cs);
        snprintf(line, sizeof line,
                 "swarm all 2 TEST 1100000 " SHA_A " %s 01_swarm_test/zxv-swarm-disc2-test.img\n",
                 cs);
        drive(&d2, "sata2", DRIVE_SECTORS);
        bucket(line, 0);
        CHECK(run() == 0 && has("nothing committed: sha256 differs from the manifest"));
        CHECK(zero(d2.d, SN_HOLD_SECTORS * 512u) &&
              memcmp(d2.d + SN_HOLD_SECTORS * 512u, image + SN_HOLD_SECTORS * 512u, 4096) == 0);
    }
    /* the bucket CID itself is not a CID */
    drive(&d2, "sata2", DRIVE_SECTORS);
    bucket(0, 0);
    memset(&models, 0, sizeof models);
    strcpy(models.cid, "bnotacid");
    models.nwant = 1;
    models.want[0] = (hw_swarm_want_t){2, "TEST", "x.img", 0};
    fetch.get = get;
    fetch.get_ctx = &store;
    fetch.p = &plat;
    nlog = 0;
    CHECK(sn_fetch_missing(&fetch, &models, &reg) == 0 && has("the bucket CID does not parse"));
    /* a drive the system uses is skipped even if blank */
    drive(&d2, "sata2", DRIVE_SECTORS);
    memset(&models, 0, sizeof models);
    ipfsn_cid_to_string(&root, models.cid, sizeof models.cid);
    models.nwant = 1;
    models.want[0] = (hw_swarm_want_t){2, "TEST", "x.img", 0};
    models.nvol = 1;
    models.vol[0].disk = &d2.b;
    nlog = 0;
    CHECK(sn_fetch_missing(&fetch, &models, &reg) == 0 && has("no blank drive"));
    tb_free(&store);
}

/* ---- 5. the post-hydration seal ---- */
static uint32_t cmd_reg, cmd_locked, phy_offs, now_ms;
static int phy_result;
static const char *keys; /* typed on the console, one per poll */
static uint32_t seal_cfg_read(void *ctx, uint32_t bdf, uint32_t off)
{
    (void) ctx;
    return bdf == HW_BDF(0, 3, 0) && off == 4 ? cmd_reg | 0x02100000u : 0xFFFFFFFFu;
}
static void seal_cfg_write(void *ctx, uint32_t bdf, uint32_t off, uint32_t v)
{
    (void) ctx;
    if (bdf == HW_BDF(0, 3, 0) && off == 4 && !cmd_locked) cmd_reg = v & 0xFFFF;
}
static int seal_phy_off(hw_nic_t *n)
{
    (void) n;
    phy_offs++;
    return phy_result;
}
static uint64_t seal_ms(void *ctx)
{
    (void) ctx;
    return now_ms += 50;
}
static int seal_key(void *ctx)
{
    (void) ctx;
    return keys && *keys ? *keys++ : 0;
}

static sn_link_t slink;
static hw_nic_t snic;
static uint8_t code[4096];

static int seal(uint8_t mode, int on_pci)
{
    static hw_plat_t sp;
    sp = (hw_plat_t){.cfg_read = seal_cfg_read, .cfg_write = seal_cfg_write, .log = log_line};
    memset(&snic, 0, sizeof snic);
    strcpy(snic.name, "eth0");
    snic.on_pci = (uint8_t) on_pci;
    snic.bdf = HW_BDF(0, 3, 0);
    snic.phy_off = seal_phy_off;
    snic.link_up = 1;
    memset(&slink, 0x5A, sizeof slink);
    slink.p = &sp;
    slink.nic = &snic;
    slink.clk = (sn_clock_t){seal_ms, 0, 0};
    memset(&fetch, 0x5A, sizeof fetch);
    memset(code, 0x90, sizeof code);
    memset(&models, 0, sizeof models);
    strcpy(models.cid, "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354");
    models.seal = mode;
    models.nwant = 2;
    models.want[0] = (hw_swarm_want_t){2, "REASON", "a.img", 1};
    models.want[1] = (hw_swarm_want_t){3, "VISION", "b.img", 1};
    cmd_reg = 0x0007;
    phy_offs = 0;
    nlog = 0;
    sn_seal_io_t io = {seal_key, 0, code, sizeof code};
    return sn_seal(&slink, &fetch, &models, &io);
}

static int wiped(void)
{
    for (uint32_t i = 0; i < sizeof code; i++)
        if (code[i] != 0xCC) return 0;
    return zero((const uint8_t *) &fetch, sizeof fetch) &&
           zero((const uint8_t *) &slink.net, sizeof slink.net) && slink.nic == 0 &&
           slink.clk.ms == seal_ms;
}

static void test_seal(void)
{
    CHECK(!sn_sealed());
    /* POWERDOWN_NIC: the exact receipt, after the bus is cut and read back */
    phy_result = 0;
    keys = 0;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == 0);
    CHECK((cmd_reg & 6) == 0 && (cmd_reg & 1) && phy_offs == 1 && snic.link_up == 0);
    CHECK(has("  [SEAL_NIC] eth0 (00:03.0): PHY powered down; PCI Bus Master and Memory Space "
              "cleared and read back"));
    CHECK(
        nlog == 6 &&
        strcmp(logs[2], "[SEAL] Swarm Discs 2..3 successfully hydrated and verified.") == 0 &&
        strcmp(logs[3],
               "[SEAL] Master CID: bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354") ==
            0 &&
        strcmp(logs[4], "[SEAL] Network hydration channel severed: NIC bus mastering disabled.") ==
            0 &&
        strcmp(logs[5], "[SEAL] Operating system is now immutable and air-gapped.") == 0);
    CHECK(strstr(logs[1],
                 "  [SEAL_WIPE] hydration code: 4096 bytes overwritten with INT3, read back") ==
          logs[1]);
    CHECK(wiped() && sn_sealed());
    if (failures) dump();

    /* a part whose PHY the driver leaves alone: said so */
    phy_result = 1;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == 0 && (cmd_reg & 6) == 0);
    CHECK(has(": PHY left as it is (no safe PHY access on this part); PCI Bus Master"));
    CHECK(!has("PHY powered down"));
    phy_result = 0;

    /* LOCAL_MESH_ONLY: the NIC stays on the bus, the WAN state goes */
    CHECK(seal(HW_SEAL_LOCAL_MESH_ONLY, 1) == 0);
    CHECK(cmd_reg == 7 && phy_offs == 0 && wiped());
    CHECK(has("[SEAL] Network hydration channel severed: WAN routes, DHCP and the fetcher "
              "removed; eth0 kept for the local mesh."));
    CHECK(has("[SEAL] Operating system is now immutable; its network is the local mesh only."));
    CHECK(!has("air-gapped") && !has("[SEAL_NIC]"));

    /* PROMPT_USER: m, then p, then nothing for 30 seconds */
    keys = "xxm";
    CHECK(seal(HW_SEAL_PROMPT_USER, 1) == 0 && cmd_reg == 7);
    CHECK(has("press p to power the NIC down") && has("[SEAL] m: keeping the NIC") &&
          has("local mesh only."));
    keys = "P";
    CHECK(seal(HW_SEAL_PROMPT_USER, 1) == 0 && (cmd_reg & 6) == 0 && has("[SEAL] p: powering"));
    keys = 0;
    now_ms = 0;
    CHECK(seal(HW_SEAL_PROMPT_USER, 1) == 0 && (cmd_reg & 6) == 0);
    CHECK(has("[SEAL] No answer: powering the NIC down.") && has("air-gapped."));
    CHECK(now_ms >= SN_SEAL_PROMPT_MS && now_ms < SN_SEAL_PROMPT_MS + 200);

    /* a bus that will not let go: no receipt, said loudly; the fetcher is
     * still wiped */
    cmd_locked = 1;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == -1 && (cmd_reg & 6) == 6);
    CHECK(has("  [SEAL_FAIL] eth0 (00:03.0): PCI Bus Master or Memory Space did not clear; "
              "the network is NOT severed"));
    CHECK(!has("[SEAL] Swarm Discs") && !has("[SEAL] Operating system") && wiped());
    cmd_locked = 0;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 0) == -1 &&
          has("  [SEAL_FAIL] eth0: not a PCI device; the network is NOT severed") &&
          !has("[SEAL] Master CID"));
}

int main(void)
{
    test_url();
    test_head();
    test_manifest();
    test_writer();
    test_fetch();
    test_seal();
    printf("test_swarmnet: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
