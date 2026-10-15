/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarmnet.c — the parts of swarmnet that need no network: gateway URLs,
 * HTTP heads, the baked table, the blank-drive rule, the writer that commits the
 * partition table last, and the whole fetch of a swarm disc from an in-memory
 * bucket (built with the Kubo-checked test builder) onto a fake drive, with
 * every way the bucket can lie; and the seal, its flush hooks and verdict. The
 * network path itself is proven under QEMU
 * (build_system/test_swarm_fetch.sh). */
#define _POSIX_C_SOURCE 200809L
#include <sys/wait.h>
#include <unistd.h>
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
#define DRIVE_SECTORS 8192u /* 4 MiB */
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

/* ---- 2. the baked table ---- */
static void test_table(void)
{
    static const sn_baked_t t[] = {
        {2u, "REASON", 5u, {0}, {0}}, {3u, "VISION", 6u, {0}, {0}}, {4u, "REASONX", 7u, {0}, {0}}};
    CHECK(sn_baked_find(t, 3, 2, "REASON") == &t[0]);
    CHECK(sn_baked_find(t, 3, 3, "VISION") == &t[1]);
    CHECK(sn_baked_find(t, 3, 4, "REASONX") == &t[2]);
    CHECK(sn_baked_find(t, 3, 3, "REASON") == 0); /* disc and group must both match */
    CHECK(sn_baked_find(t, 3, 2, "REASONX") == 0);
    CHECK(sn_baked_find(t, 3, 4, "REASON") == 0); /* a prefix is not the group */
    CHECK(sn_baked_find(t, 3, 2, "REASO") == 0);
    CHECK(sn_baked_find(t, 2, 4, "REASONX") == 0); /* only the first n rows */
    CHECK(sn_baked_find(t, 0, 2, "REASON") == 0);
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
          strcmp(why, "sha256 differs from the table") == 0);
    sha[5] ^= 1;
    CHECK(zero(d1.d, SN_HOLD_SECTORS * 512u) && sn_drive_blank(&d1.b, sizeof img, sector));
    /* short */
    drive(&d1, "sata1", DRIVE_SECTORS);
    sn_writer_init(&w, &d1.b, sizeof img);
    CHECK(sn_writer_put(&w, img, sizeof img - 1) == 0);
    CHECK(sn_writer_commit(&w, sha, &why) != 0 && strcmp(why, "size differs from the table") == 0);
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

/* ---- 4. the fetch, by the CID in the kernel's table ---- */
static tb_store_t store;
static uint8_t image[1600000];
static uint32_t img_len = 1100000;
static uint32_t zero_leaves; /* full zero leaves the source was asked for */
static uint8_t isha[32];
static sn_baked_t row; /* the kernel's table: one swarm disc */
static hw_models_t models;
static hw_registry_t reg;
static sn_fetch_t fetch;
static int tamper_leaf;
static uint64_t fms;         /* the fake clock, ms */
static int down_calls;       /* the source fails this many more requests */
static int serve_limit = -1; /* full leaves the source serves before failing (-1: all) */
static uint32_t served;      /* full raw leaves served */
static uint32_t gets, pb_gets;
static uint32_t fresh_seen; /* steps that said a disc had just committed */

static uint64_t fake_ms(void *ctx)
{
    (void) ctx;
    return fms;
}

/* tb_get, optionally changing one byte of the image's raw leaves */
static int get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    gets++;
    fms += 10; /* every request takes a while: the rate shows */
    if (down_calls > 0) {
        down_calls--;
        return IPFSN_ERR_TRANSPORT;
    }
    if (serve_limit >= 0 && cid->codec == 0x55 && (int) served >= serve_limit)
        return IPFSN_ERR_TRANSPORT;
    int rc = tb_get(ctx, cid, buf, cap, len);
    if (rc == IPFSN_OK && cid->codec == 0x55 && *len == 262144u) served++;
    if (cid->codec == 0x70) pb_gets++;
    if (rc == IPFSN_OK && *len == 262144u && zero(buf, 262144u)) zero_leaves++;
    if (rc == IPFSN_OK && tamper_leaf && cid->codec == 0x55 && *len == 262144u) buf[100] ^= 1;
    return rc;
}

/* The image in the block store and its row in the kernel's table; with
 * cid_wrong the row holds another file's CID, with sha_wrong a SHA-256 off
 * by one bit. */
static void table_row(int cid_wrong, int sha_wrong)
{
    ipfsn_cid_t icid, other;
    uint64_t its;
    tb_free(&store);
    tb_init(&store);
    tb_file(&store, image, img_len, &icid, &its);
    tb_file(&store, (const uint8_t *) "x", 1, &other, 0);
    memset(&row, 0, sizeof row);
    row.disc = 2;
    strcpy(row.group, "TEST");
    row.bytes = img_len;
    memcpy(row.sha, isha, 32);
    if (sha_wrong) row.sha[0] ^= 1;
    CHECK(ipfsn_cid_encode(cid_wrong ? &other : &icid, row.cid, sizeof row.cid) == 36);
}

/* Steps until the fetch is done, the clock jumping over each wait. */
static uint32_t steps(uint32_t max)
{
    uint32_t n = 0;
    while (n++ < max && sn_fetch_step(&fetch, &models, &reg)) {
        fresh_seen += fetch.fresh;
        if (fms < fetch.next_try) fms = fetch.next_try;
    }
    return fetch.committed;
}

static void want_disc2(void)
{
    memset(&models, 0, sizeof models);
    models.nwant = 2;
    models.want[0] = (hw_swarm_want_t){2, "TEST", "zxv-swarm-disc2-test.img", 0};
    models.want[1] = (hw_swarm_want_t){3, "ATTACHED", "x.img", 1};
}

static void fresh_fetch(void)
{
    memset(&fetch, 0, sizeof fetch);
    fetch.p = &plat;
    fetch.get = get;
    fetch.get_ctx = &store;
    fetch.clk.ms = fake_ms;
    fetch.table = &row;
    fetch.ntable = 1;
    sn_fetch_begin(&fetch);
    nlog = 0;
}

static uint32_t run(void)
{
    want_disc2();
    fresh_fetch();
    return steps(100000);
}

static void test_fetch(void)
{
    static drive_t used, full, small;
    img_len = 1100000;
    for (uint32_t i = 0; i < sizeof image; i++) image[i] = (uint8_t) (i * 13 + (i >> 11));
    memset(image + 262144, 0, 2 * 262144); /* leaves 1 and 2: identical padding */
    image[510] = 0x55;                     /* a disc image starts with its protective MBR */
    image[511] = 0xAA;
    sha256(image, img_len, isha);
    drive(&used, "sata0", DRIVE_SECTORS); /* not blank: an MBR */
    used.d[510] = 0x55;
    used.d[511] = 0xAA;
    drive(&small, "sata1", 1000);
    drive(&d2, "sata2", DRIVE_SECTORS);
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &used.b);
    hw_reg_blk(&reg, &small.b);
    hw_reg_blk(&reg, &d2.b);

    table_row(0, 0);
    zero_leaves = 0;
    CHECK(run() == 1);
    CHECK(zero_leaves == 1); /* the second zero leaf came from the cache */
    CHECK(has("[SWARM_FETCH] disc 2 ZXV_SWARM_TEST -> sata2"));
    CHECK(has("[SWARM_FETCH] disc 2 [##--------] 20% (262144 of 1100000 bytes, "));
    CHECK(has(" KB/s)"));
    CHECK(has("[SWARM_FETCHED] disc 2 ZXV_SWARM_TEST on sata2: 1100000 bytes, sha256 matches the "
              "table; committed"));
    CHECK(!has("ATTACHED"));
    CHECK(memcmp(d2.d, image, img_len) == 0);
    if (failures) dump();

    /* the drive now holds a disc: a second run finds no blank drive */
    CHECK(run() == 0 && has("[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_TEST: no blank drive"));
    /* a drive the system already uses is never chosen, blank or not */
    drive(&d2, "sata2", DRIVE_SECTORS);
    memset(&models, 0, sizeof models);
    (void) full;

    /* a block changed in transit: never committed */
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(0, 0);
    tamper_leaf = 1;
    CHECK(run() == 0 && has("[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_TEST: every gateway sent a block "
                            "that does not match its CID; nothing committed"));
    tamper_leaf = 0;
    CHECK(zero(d2.d, SN_HOLD_SECTORS * 512u));
    /* the table's CID is another file's: what it names is fetched, and its
     * size and SHA-256 are not the table's, so nothing is committed */
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(1, 0);
    CHECK(run() == 0 && has("[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_TEST") &&
          has("nothing committed: size differs from the table"));
    CHECK(zero(d2.d, SN_HOLD_SECTORS * 512u));
    /* the table's SHA-256 is wrong: written, never committed */
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(0, 1);
    CHECK(run() == 0 && has("nothing committed: sha256 differs from the table"));
    CHECK(zero(d2.d, SN_HOLD_SECTORS * 512u) &&
          memcmp(d2.d + SN_HOLD_SECTORS * 512u, image + SN_HOLD_SECTORS * 512u, 4096) == 0);
    /* a disc the table does not list: nothing is fetched */
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(0, 0);
    want_disc2();
    models.want[0] = (hw_swarm_want_t){2, "OTHER", "x.img", 0};
    fresh_fetch();
    gets = 0;
    CHECK(steps(100) == 0 && gets == 0 &&
          has("[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_OTHER: not in the table this kernel was built "
              "with; attach it by hand"));
    /* a CID in the table that does not decode */
    row.cid[0] = 0x07;
    want_disc2();
    fresh_fetch();
    CHECK(steps(100) == 0 && has("its CID in this kernel's table does not decode"));
    /* a drive the system uses is skipped even if blank */
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(0, 0);
    memset(&models, 0, sizeof models);
    models.nwant = 1;
    models.want[0] = (hw_swarm_want_t){2, "TEST", "x.img", 0};
    models.nvol = 1;
    models.vol[0].disk = &d2.b;
    fresh_fetch();
    CHECK(steps(100) == 0 && has("no blank drive"));
    tb_free(&store);
}

/* ---- 4b. elastic: waits, resumes, stops ---- */
static void test_elastic(void)
{
    static drive_t exact;
    static uint8_t sector[512];
    char cid[121];
    uint64_t want, durable;
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &d2.b);

    /* no gateway answers three times: 1, 2, 4 s apart, then it does */
    img_len = 1100000;
    sha256(image, img_len, isha);
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(0, 0);
    want_disc2();
    fresh_fetch();
    down_calls = 3;
    uint64_t t0 = fms;
    uint32_t g = gets;
    /* finding the disc asks nobody: its CID is in the table */
    CHECK(sn_fetch_step(&fetch, &models, &reg) == 1 && fetch.phase == 2 && gets == g);
    CHECK(sn_fetch_step(&fetch, &models, &reg) == 1 && fetch.next_try == fms + 1000);
    g = gets;
    CHECK(sn_fetch_step(&fetch, &models, &reg) == 1 && gets == g); /* asks nobody while waiting */
    CHECK(steps(1000) == 1 && memcmp(d2.d, image, img_len) == 0);
    CHECK(has("  [SWARM_FETCH] disc 2: waiting for peers (no gateway served the next block); "
              "retry 1 in 1 s; 0 bytes so far"));
    CHECK(has("retry 2 in 2 s") && has("retry 3 in 4 s") && !has("retry 4"));
    CHECK(fms - t0 >= 7000 && fetch.tries == 0);
    CHECK(!sn_journal_read(&d2.b, sector, cid, &want, &durable)); /* cleared on commit */
    if (failures) dump();

    /* one step takes about SN_STEP_BYTES, then hands back; the directory and
     * file nodes are fetched once, not once per step */
    drive(&d2, "sata2", DRIVE_SECTORS);
    want_disc2();
    fresh_fetch();
    pb_gets = 0;
    fresh_seen = 0;
    CHECK(sn_fetch_step(&fetch, &models, &reg) == 1 && fetch.phase == 2 && fetch.wr.got == 0);
    CHECK(sn_fetch_step(&fetch, &models, &reg) == 1 && fetch.wr.got == SN_STEP_BYTES);
    CHECK(steps(1000) == 1 && fresh_seen == 1 && fetch.fresh == 0);
    CHECK(pb_gets == 1); /* the image's root, straight from the table's CID */

    /* longer outages: the wait doubles to a minute and stays there */
    drive(&d2, "sata2", DRIVE_SECTORS);
    want_disc2();
    fresh_fetch();
    down_calls = 9;
    CHECK(steps(1000) == 1 && has("retry 6 in 32 s") && has("retry 7 in 60 s") &&
          has("retry 9 in 60 s") && !has("in 64 s"));

    /* a download cut off after 5 leaves, then a reboot: it resumes */
    img_len = 1600000;
    sha256(image, img_len, isha);
    drive(&d2, "sata2", DRIVE_SECTORS);
    table_row(0, 0);
    want_disc2();
    fresh_fetch();
    served = 0;
    serve_limit = 5;
    CHECK(steps(40) == 0 && has("waiting for peers"));
    sn_fetch_stop(&fetch, &models);
    CHECK(has("[SWARM] Hydration stopped at your request: Attach Disc 2 USB / drive."));
    CHECK(has("  [SWARM] 1065984 bytes of disc 2 stay on sata2 and resume on a later boot with a "
              "network"));
    CHECK(sn_fetch_step(&fetch, &models, &reg) == 0); /* stopped stays stopped */
    CHECK(zero(d2.d, SN_HOLD_SECTORS * 512u) && sn_drive_blank(&d2.b, img_len, sector));
    CHECK(sn_journal_read(&d2.b, sector, cid, &want, &durable) && want == 1600000 &&
          durable == 1065984);
    static uint8_t keep[DRIVE_SECTORS * 512u];
    memcpy(keep, d2.d, sizeof keep);
    serve_limit = -1;
    served = 0;
    fresh_fetch();
    CHECK(steps(1000) == 1 && memcmp(d2.d, image, img_len) == 0);
    CHECK(has("  [SWARM_FETCH] disc 2 ZXV_SWARM_TEST: resuming on sata2 at 1065984 of 1600000 "
              "bytes; re-reading them first"));
    CHECK(has("  [SWARM_FETCH] disc 2 ZXV_SWARM_TEST: 1065984 bytes re-read from sata2 and "
              "hashed; continuing from there"));
    CHECK(served == 2); /* leaves 4 and 5 (6 is short), nothing before them */
    CHECK(!sn_journal_read(&d2.b, sector, cid, &want, &durable));
    if (failures) dump();

    /* the same, but the kept part was changed while the machine was off */
    memcpy(d2.d, keep, sizeof keep);
    d2.d[SN_HOLD_SECTORS * 512u + 5000] ^= 1;
    fresh_fetch();
    CHECK(steps(1000) == 1 && memcmp(d2.d, image, img_len) == 0);
    CHECK(has("nothing committed") == 0 && has("sha256 differs from the table") &&
          has("the resumed part was not the image; downloading it again from the start"));
    /* ... and in the held head's copy */
    memcpy(d2.d, keep, sizeof keep);
    d2.d[(DRIVE_SECTORS - 2u) * 512u + 7] ^= 1;
    fresh_fetch();
    CHECK(steps(1000) == 1 && memcmp(d2.d, image, img_len) == 0 &&
          has("downloading it again from the start"));
    /* cut off after the first leaf: the record holds just the head */
    drive(&d2, "sata2", DRIVE_SECTORS);
    fresh_fetch();
    served = 0;
    serve_limit = 1;
    CHECK(steps(40) == 0 && sn_journal_read(&d2.b, sector, cid, &want, &durable) &&
          durable == SN_HOLD_SECTORS * 512u);
    serve_limit = -1;
    fresh_fetch();
    CHECK(steps(1000) == 1 && memcmp(d2.d, image, img_len) == 0 &&
          has("resuming on sata2 at 17408 of 1600000 bytes"));
    /* two blank drives: the one with another disc's record is left for it */
    {
        static drive_t other;
        drive(&other, "sata1", DRIVE_SECTORS);
        memcpy(other.d + (DRIVE_SECTORS - 1u) * 512u, keep + (DRIVE_SECTORS - 1u) * 512u, 512);
        memcpy(other.d + (DRIVE_SECTORS - 1u) * 512u + 24, "bafkreiother", 12);
        sha256(other.d + (DRIVE_SECTORS - 1u) * 512u, 480,
               other.d + (DRIVE_SECTORS - 1u) * 512u + 480);
        CHECK(sn_journal_read(&other.b, sector, cid, &want, &durable));
        drive(&d2, "sata2", DRIVE_SECTORS);
        memset(&reg, 0, sizeof reg);
        hw_reg_blk(&reg, &other.b);
        hw_reg_blk(&reg, &d2.b);
        fresh_fetch();
        CHECK(steps(1000) == 1 && has("[SWARM_FETCH] disc 2 ZXV_SWARM_TEST -> sata2"));
        memset(&reg, 0, sizeof reg);
        hw_reg_blk(&reg, &d2.b);
    }
    /* a record that names another image is not resumed; the drive is reused */
    memcpy(d2.d, keep, sizeof keep);
    d2.d[(DRIVE_SECTORS - 1u) * 512u + 30] ^= 1; /* the CID, so the record's sum fails too */
    fresh_fetch();
    CHECK(steps(1000) == 1 && has("[SWARM_FETCH] disc 2 ZXV_SWARM_TEST -> sata2") &&
          !has("resuming"));
    /* a record whose point is not one a writer leaves is refused */
    memcpy(d2.d, keep, sizeof keep);
    {
        static sn_writer_t w;
        sn_writer_init(&w, &d2.b, img_len);
        CHECK(sn_writer_journal(&w, "x") == 1);
        CHECK(sn_writer_reread(&w, 1065985, 1u << 30) == -1 &&
              sn_writer_reread(&w, 17407, 1u << 30) == -1 &&
              sn_writer_reread(&w, img_len + 1, 1u << 30) == -1);
        CHECK(sn_writer_reread(&w, 1065984, 65536) == 0 && w.got < 1065984);
        CHECK(sn_writer_reread(&w, 1065984, 1u << 30) == 1 && w.got == 1065984 &&
              w.written == 1048576);
    }
    /* a drive with no room past the image keeps no record */
    drive(&exact, "sata3", (1600000 + 511) / 512);
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &exact.b);
    fresh_fetch();
    CHECK(steps(1000) == 1 && has("no room on the drive for a resume record; an interrupted "
                                  "download starts again"));
    CHECK(memcmp(exact.d, image, img_len) == 0);

    /* the response timeout adapts */
    {
        static sn_http_t h;
        memset(&h, 0, sizeof h);
        h.timed_out = 1;
        sn_http_adapt(&h, 0);
        CHECK(h.stall_ms == 2 * SN_STALL_MS);
        for (int i = 0; i < 10; i++) sn_http_adapt(&h, 0);
        CHECK(h.stall_ms == SN_STALL_MAX_MS);
        sn_http_adapt(&h, 1);
        CHECK(h.stall_ms == SN_STALL_MAX_MS - (SN_STALL_MAX_MS - SN_STALL_MS) / 4u);
        h.timed_out = 0;
        uint32_t t = h.stall_ms;
        sn_http_adapt(&h, 0); /* refused, not slow: patience unchanged */
        CHECK(h.stall_ms == t);
        for (int i = 0; i < 100; i++) sn_http_adapt(&h, 1);
        CHECK(h.stall_ms >= SN_STALL_MS && h.stall_ms < SN_STALL_MS + 8);
    }
    img_len = 1100000;
    sha256(image, img_len, isha);
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

/* flush hooks: each records the bus as it found it, and the order it ran in */
static uint16_t flush_cmd[2];
static int flush_rc[2], flush_order[2], nflush;

static int flush_hook(void *ctx)
{
    int i = (int) (uintptr_t) ctx;
    flush_cmd[i] = cmd_reg;
    flush_order[nflush++ % 2] = i;
    return flush_rc[i];
}

static int other_hook(void *ctx)
{
    (void) ctx;
    return 0;
}

static void test_seal(void)
{
    CHECK(!sn_sealed() && sn_seal_verdict(0) == LPRES_STATE_NEITHER);
    /* registration: bounded, named, no repeats */
    CHECK(sn_seal_on(0, 0, "agora") == -1);
    CHECK(sn_seal_on(flush_hook, 0, 0) == -1 && sn_seal_on(flush_hook, 0, "") == -1);
    CHECK(sn_seal_on(flush_hook, 0, "abcdefghijklmnopqrstuvw") == 0);
    CHECK(sn_seal_on(flush_hook, (void *) 1, "abcdefghijklmnopqrstuvwx") == -1);
    CHECK(sn_seal_on(flush_hook, 0, "again") == -1);
    CHECK(sn_seal_on(flush_hook, (void *) 1, "kin-collective") == 0);
    for (uintptr_t i = 2; i + 1 < SN_SEAL_HOOK_MAX; i++)
        CHECK(sn_seal_on(other_hook, (void *) i, "pad") == 0);
    /* the bound, in a child, so the parent keeps one free place for the
     * after-the-seal check below */
    pid_t kid = fork();
    if (kid == 0)
        _exit(sn_seal_on(other_hook, (void *) 98, "last") == 0 &&
                      sn_seal_on(other_hook, (void *) 99, "one-too-many") == -1
                  ? 0
                  : 1);
    int st = -1;
    CHECK(kid > 0 && waitpid(kid, &st, 0) == kid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    /* POWERDOWN_NIC: the exact receipt, after the bus is cut and read back */
    phy_result = 0;
    keys = 0;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == HW_SEAL_POWERDOWN_NIC);
    CHECK((cmd_reg & 6) == 0 && (cmd_reg & 1) && phy_offs == 1 && snic.link_up == 0);
    CHECK(has("  [SEAL_NIC] eth0 (00:03.0): PHY powered down; PCI Bus Master and Memory Space "
              "cleared and read back"));
    CHECK(
        nlog == 13 &&
        strcmp(logs[9], "[SEAL] Swarm Discs 2..3 successfully hydrated and verified.") == 0 &&
        strcmp(logs[10],
               "[SEAL] Master CID: bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354") ==
            0 &&
        strcmp(logs[11], "[SEAL] Network hydration channel severed: NIC bus mastering disabled.") ==
            0 &&
        strcmp(logs[12], "[SEAL] Operating system is now immutable and air-gapped.") == 0);
    CHECK(strstr(logs[8],
                 "  [SEAL_WIPE] hydration code: 4096 bytes overwritten with INT3, read back") ==
          logs[8]);
    CHECK(wiped() && sn_sealed() && sn_seal_verdict(0) == LPRES_STATE_TRUE);
    /* the hooks ran first, in order, with the bus still mastering */
    CHECK(strcmp(logs[0], "  [SEAL_FLUSH] abcdefghijklmnopqrstuvw: drained") == 0 &&
          strcmp(logs[1], "  [SEAL_FLUSH] kin-collective: drained") == 0 &&
          strncmp(logs[7], "  [SEAL_NIC] ", 13) == 0);
    CHECK(nflush == 2 && flush_order[0] == 0 && flush_order[1] == 1 && flush_cmd[0] == 7 &&
          flush_cmd[1] == 7);
    CHECK(sn_seal_on(other_hook, (void *) 98, "late") == -1);
    /* a hook that does not drain is said, and the seal goes on */
    flush_rc[1] = -1;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == HW_SEAL_POWERDOWN_NIC && (cmd_reg & 6) == 0);
    CHECK(
        has("  [SEAL_FLUSH_FAIL] kin-collective: did not drain; its buffers go with the network") &&
        has("[SEAL] Operating system is now immutable and air-gapped."));
    flush_rc[1] = 0;
    if (failures) dump();

    /* a part whose PHY the driver leaves alone: said so */
    phy_result = 1;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == HW_SEAL_POWERDOWN_NIC && (cmd_reg & 6) == 0);
    CHECK(has(": PHY left as it is (no safe PHY access on this part); PCI Bus Master"));
    CHECK(!has("PHY powered down"));
    phy_result = 0;

    /* LOCAL_MESH_ONLY: the NIC stays on the bus, the WAN state goes */
    CHECK(seal(HW_SEAL_LOCAL_MESH_ONLY, 1) == HW_SEAL_LOCAL_MESH_ONLY);
    CHECK(cmd_reg == 7 && phy_offs == 0 && wiped());
    CHECK(has("[SEAL] Network hydration channel severed: WAN routes, DHCP and the fetcher "
              "removed; eth0 kept for the local mesh."));
    CHECK(has("[SEAL] Operating system is now immutable; its network is the local mesh only."));
    CHECK(!has("air-gapped") && !has("[SEAL_NIC]"));

    /* PROMPT_USER: m, then p, then nothing for 30 seconds */
    keys = "xxm";
    CHECK(seal(HW_SEAL_PROMPT_USER, 1) == HW_SEAL_LOCAL_MESH_ONLY && cmd_reg == 7);
    CHECK(has("press p to power the NIC down") && has("[SEAL] m: keeping the NIC") &&
          has("local mesh only."));
    keys = "P";
    CHECK(seal(HW_SEAL_PROMPT_USER, 1) == HW_SEAL_POWERDOWN_NIC && (cmd_reg & 6) == 0 &&
          has("[SEAL] p: powering"));
    keys = 0;
    now_ms = 0;
    CHECK(seal(HW_SEAL_PROMPT_USER, 1) == HW_SEAL_POWERDOWN_NIC && (cmd_reg & 6) == 0);
    CHECK(has("[SEAL] No answer: powering the NIC down.") && has("air-gapped."));
    CHECK(now_ms >= SN_SEAL_PROMPT_MS && now_ms < SN_SEAL_PROMPT_MS + 200);

    /* a bus that will not let go: no receipt, said loudly; the fetcher is
     * still wiped */
    cmd_locked = 1;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 1) == -1 && (cmd_reg & 6) == 6);
    CHECK(has("  [SEAL_FAIL] eth0 (00:03.0): PCI Bus Master or Memory Space did not clear; "
              "the network is NOT severed"));
    CHECK(!has("[SEAL] Swarm Discs") && !has("[SEAL] Operating system") && wiped());
    CHECK(sn_seal_verdict(0) == LPRES_STATE_FALSE);
    cmd_locked = 0;
    CHECK(seal(HW_SEAL_POWERDOWN_NIC, 0) == -1 &&
          has("  [SEAL_FAIL] eth0: not a PCI device; the network is NOT severed") &&
          !has("[SEAL] Master CID"));
}

int main(void)
{
    test_url();
    test_head();
    test_table();
    test_writer();
    test_fetch();
    test_elastic();
    test_seal();
    printf("test_swarmnet: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
