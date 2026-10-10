/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_ipfsn_dir.c — host test for src/ipfs_node/ipfsn_dir.c.
 *
 * Build and run (from kernel/):
 *
 *   export CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus
 *   gcc -std=c11 -Wall -Werror -Wextra -O2 -Isrc/ipfs_node \
 *       src/ipfs_node/test_ipfsn_dir.c src/ipfs_node/ipfsn_dir.c \
 *       src/ipfs_node/ipfsn_multiformats.c src/ipfs_node/ipfsn_unixfs.c \
 *       src/ipfs_node/ipfsn_store.c src/ipfs_node/ipfsn_net.c src/ipfs_node/ipfsn_ubh.c \
 *       src/ipfs_node/ipfsn_fidx.c src/robin_debanks/sha256.c src/tls/aead.c src/tls/hkdf.c \
 *       src/ubh/ubh.c src/event_space/event_envelope.c \
 *       -o /tmp/test_ipfsn_dir && /tmp/test_ipfsn_dir
 *
 * (Add -fsanitize=address,undefined -g for the sanitizer run.)
 *
 * WHERE THE VECTORS COME FROM
 *   test_ipfsn_dir_vectors.h: Kubo 0.32.1 root CIDs and `ipfs dag export`
 *   CARs (see that header for the exact commands). murmur3 values: the
 *   `mmh3` Python package, mmh3.hash64(s, 0, signed=False)[0].
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ipfs_node.h"
#include "ipfsn_dir.h"
#include "test_ipfsn_dirbuild.h"
#include "test_ipfsn_dir_vectors.h"

static int failures = 0, passes = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, m);                                    \
            failures++;                                                                            \
        } else {                                                                                   \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)

static uint8_t g_scratch[IPFSN_BLOCK_MAX * 2];
static uint8_t g_buf[IPFSN_BLOCK_MAX];
static ipfsn_dir_walk_t g_walk;

static ipfsn_cid_t parse_cid(const char *s)
{
    ipfsn_cid_t c;
    memset(&c, 0, sizeof c);
    if (ipfsn_cid_parse(s, (uint32_t) strlen(s), &c)) printf("bad cid %s\n", s);
    return c;
}

static ipfsn_cid_t raw_cid(const void *d, uint32_t n)
{
    ipfsn_cid_t c;
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) d, n, &c);
    return c;
}

/* ---- a CAR loaded into a tb_store (so a source can be spied on / tampered) ---- */

static void load_car(tb_store_t *s, const uint8_t *car, uint32_t len, ipfsn_cid_t *root)
{
    ipfsn_car_t c;
    ipfsn_cid_t cid;
    const uint8_t *b;
    uint32_t bl;
    int r = ipfsn_car_open(&c, car, len);
    CHECK(r == 0 && c.nroots == 1, "Kubo CAR opens with one root");
    *root = c.roots[0];
    while ((r = ipfsn_car_next(&c, &cid, &b, &bl)) == IPFSN_OK) tb_put(s, &cid, b, bl);
    CHECK(r == IPFSN_ERR_NOTFOUND, "Kubo CAR: every block verifies");
}

/* ---- list collector ---- */

typedef struct {
    tb_ent_t *e;
    uint32_t n, cap;
} coll_t;

static int collect(void *ctx, const ipfsn_dirent_t *d)
{
    coll_t *c = (coll_t *) ctx;
    if (c->n == c->cap) return 99;
    memset(&c->e[c->n], 0, sizeof c->e[0]);
    memcpy(c->e[c->n].name, d->name, d->name_len);
    c->e[c->n].name_len = d->name_len;
    c->e[c->n].cid = d->cid;
    c->e[c->n].tsize = d->tsize;
    c->n++;
    return 0;
}

static bool cid_is(const ipfsn_cid_t *a, const ipfsn_cid_t *b)
{
    ipfsn_cid_t x, y;
    ipfsn_cid_to_v1(a, &x);
    ipfsn_cid_to_v1(b, &y);
    return ipfsn_cid_equal(&x, &y);
}

/* ---- directory contents (must match gen_dirs.py, see the vectors header) ---- */

static const char *UNI = "\xc3\xbc"
                         "n"
                         "\xc3\xaf"
                         "c"
                         "\xc3\xb6"
                         "d"
                         "\xc3\xa9"
                         ".txt";

static void make_dir_a(tb_store_t *s, ipfsn_cid_t *root, ipfsn_cid_t *sub_cid)
{
    static uint8_t data[1000];
    tb_ent_t top[8], sub[2], deeper[1];
    ipfsn_cid_t c;
    uint64_t t;
    for (uint32_t i = 0; i < 1000; i++) data[i] = (uint8_t) ((i * 7 + 3) & 255);
    tb_file(s, (const uint8_t *) "x", 1, &c, &t);
    tb_ent(&deeper[0], "x", &c, t);
    tb_dir(s, deeper, 1, &c, &t);
    tb_ent(&sub[0], "deeper", &c, t);
    tb_file(s, (const uint8_t *) "inner\n", 6, &c, &t);
    tb_ent(&sub[1], "inner.txt", &c, t);
    tb_dir(s, sub, 2, sub_cid, &t);
    tb_ent(&top[0], "sub", sub_cid, t);
    tb_file(s, (const uint8_t *) "hello world\n", 12, &c, &t);
    tb_ent(&top[1], "hello.txt", &c, t);
    tb_file(s, (const uint8_t *) "", 0, &c, &t);
    tb_ent(&top[2], "empty", &c, t);
    tb_file(s, (const uint8_t *) "Z", 1, &c, &t);
    tb_ent(&top[3], "Z", &c, t);
    tb_file(s, (const uint8_t *) "space\n", 6, &c, &t);
    tb_ent(&top[4], "a b.txt", &c, t);
    tb_file(s, data, 1000, &c, &t);
    tb_ent(&top[5], "data.bin", &c, t);
    tb_file(s, (const uint8_t *) "u\n", 2, &c, &t);
    tb_ent(&top[6], UNI, &c, t);
    tb_dir(s, top, 7, root, &t);
}

static tb_ent_t *make_ents_b(tb_store_t *s, uint32_t n)
{
    tb_ent_t *e = (tb_ent_t *) calloc(n, sizeof *e);
    for (uint32_t i = 0; i < n; i++) {
        char nm[16], ct[16];
        ipfsn_cid_t c;
        uint64_t t;
        snprintf(nm, sizeof nm, "f%04u", i);
        snprintf(ct, sizeof ct, "%u\n", i);
        tb_file(s, (const uint8_t *) ct, (uint32_t) strlen(ct), &c, &t);
        tb_ent(&e[i], nm, &c, t);
    }
    return e;
}

static tb_ent_t *make_ents_c(tb_store_t *s, uint32_t n)
{
    tb_ent_t *e = (tb_ent_t *) calloc(n, sizeof *e);
    ipfsn_cid_t c;
    uint64_t t;
    tb_file(s, (const uint8_t *) "", 0, &c, &t);
    for (uint32_t i = 0; i < n; i++) {
        char nm[260];
        snprintf(nm, sizeof nm, "n%04u", i);
        memset(nm + 5, 'x', 195);
        nm[200] = 0;
        tb_ent(&e[i], nm, &c, t);
    }
    return e;
}

/* ---- tests ---- */

static void test_murmur(void)
{
    struct {
        const char *s;
        uint64_t h;
    } v[] = {
        {"", 0x0ull},
        {"a", 0x85555565f6597889ull},
        {"hello", 0xcbd8a7b341bd9b02ull},
        {"f0042", 0xe297f452dd7952b3ull},
        {"0123456789abcdef", 0x4be06d94cf4ad1a7ull},
        {"0123456789abcdefXYZ", 0x99d375026c4a901dull},
        {"zxv-update.manifest", 0x1a466ce0e146e766ull},
    };
    for (uint32_t i = 0; i < sizeof v / sizeof v[0]; i++)
        CHECK(ipfsn_murmur3_64((const uint8_t *) v[i].s, (uint32_t) strlen(v[i].s)) == v[i].h,
              "murmur3-x64-64 matches mmh3");
}

static void test_kubo_plain(void)
{
    tb_store_t s, b;
    ipfsn_cid_t root, sub, built_root, built_sub, want = parse_cid(KUBO_DIR_A_ROOT);
    coll_t c = {0, 0, 32};
    ipfsn_dirent_t e;
    uint32_t n = 0;
    int r;
    tb_init(&s);
    tb_init(&b);
    c.e = (tb_ent_t *) calloc(32, sizeof(tb_ent_t));
    load_car(&s, KUBO_DIR_A_CAR, sizeof KUBO_DIR_A_CAR, &root);
    CHECK(cid_is(&root, &want), "dirA CAR root is Kubo's root CID");

    r = ipfsn_dir_list(&g_walk, &root, tb_get, &s, g_scratch, sizeof g_scratch, collect, &c, &n);
    CHECK(r == 0 && n == 7 && c.n == 7, "dirA lists 7 entries");
    {
        const char *order[7] = {"Z", "a b.txt", "data.bin", "empty", "hello.txt", "sub", UNI};
        uint64_t sizes[7] = {1, 6, 1000, 0, 12, 0, 2};
        bool ok = true;
        for (uint32_t i = 0; i < 7 && i < c.n; i++) {
            if (c.e[i].name_len != strlen(order[i]) ||
                memcmp(c.e[i].name, order[i], c.e[i].name_len))
                ok = false;
            if (i != 5 && c.e[i].tsize != sizes[i]) ok = false;
        }
        CHECK(ok, "dirA entries in Kubo's (byte-sorted) order with file Tsizes");
    }
    sub = parse_cid(KUBO_DIR_A_SUB);
    CHECK(c.n == 7 && cid_is(&c.e[5].cid, &sub), "dirA/sub CID matches `ipfs ls`");
    {
        ipfsn_cid_t h = raw_cid("hello world\n", 12);
        r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) "hello.txt", 9, g_buf,
                             sizeof g_buf, &e);
        CHECK(r == 0 && cid_is(&e.cid, &h) && e.tsize == 12, "lookup hello.txt");
        r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) UNI, (uint32_t) strlen(UNI),
                             g_buf, sizeof g_buf, &e);
        CHECK(r == 0, "lookup a UTF-8 name");
        r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) "hello.tx", 8, g_buf,
                             sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_NOTFOUND, "lookup of a prefix is not found");
        r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) "zzz", 3, g_buf, sizeof g_buf,
                             &e);
        CHECK(r == IPFSN_ERR_NOTFOUND, "lookup past the end is not found");
        r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) "a/b", 3, g_buf, sizeof g_buf,
                             &e);
        CHECK(r == IPFSN_ERR_NOTFOUND, "a name with '/' cannot exist");
        h = raw_cid("x", 1);
        r = ipfsn_dir_resolve(&root, tb_get, &s, "sub/deeper/x", 12, g_buf, sizeof g_buf, &e);
        CHECK(r == 0 && cid_is(&e.cid, &h), "resolve sub/deeper/x");
        r = ipfsn_dir_resolve(&root, tb_get, &s, "sub/nope", 8, g_buf, sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_NOTFOUND, "resolve a missing path");
        r = ipfsn_dir_resolve(&root, tb_get, &s, "sub//x", 6, g_buf, sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_ARG, "resolve refuses an empty component");
        r = ipfsn_dir_resolve(&root, tb_get, &s, "sub/", 4, g_buf, sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_ARG, "resolve refuses a trailing slash");
        r = ipfsn_dir_resolve(&root, tb_get, &s, "../x", 4, g_buf, sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_ARG, "resolve refuses ..");
        r = ipfsn_dir_resolve(&root, tb_get, &s, "hello.txt/x", 11, g_buf, sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_UNSUPP, "a file is not a directory");
    }
    /* the test-only builder reproduces Kubo's bytes */
    make_dir_a(&b, &built_root, &built_sub);
    CHECK(cid_is(&built_root, &want), "builder: dirA root == Kubo");
    CHECK(cid_is(&built_sub, &sub), "builder: dirA/sub == Kubo");

    /* a tampered block behind the source is caught by the hash check */
    {
        tb_block_t *blk = tb_find(&s, &root);
        blk->data[blk->len - 1] ^= 1;
        r = ipfsn_dir_list(&g_walk, &root, tb_get, &s, g_scratch, sizeof g_scratch, collect, &c,
                           &n);
        CHECK(r == IPFSN_ERR_HASH, "tampered directory block: IPFSN_ERR_HASH");
        r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) "Z", 1, g_buf, sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_HASH, "tampered directory block: lookup refuses too");
        blk->data[blk->len - 1] ^= 1;
    }
    {
        ipfsn_cid_t rc = raw_cid("Z", 1);
        r = ipfsn_dir_list(&g_walk, &rc, tb_get, &s, g_scratch, sizeof g_scratch, collect, &c, &n);
        CHECK(r == IPFSN_ERR_UNSUPP, "a raw CID is not a directory");
    }
    free(c.e);
    tb_free(&s);
    tb_free(&b);
}

static void check_hamt_listing(tb_store_t *s, const ipfsn_cid_t *root, tb_ent_t *want, uint32_t n,
                               const char *what)
{
    coll_t c = {0, 0, 0};
    uint32_t cnt = 0, found = 0;
    ipfsn_dirent_t e;
    bool ok = true;
    int r;
    c.cap = n + 4;
    c.e = (tb_ent_t *) calloc(c.cap, sizeof(tb_ent_t));
    r = ipfsn_dir_list(&g_walk, root, tb_get, s, g_scratch, sizeof g_scratch, collect, &c, &cnt);
    CHECK(r == 0 && cnt == n, what);
    for (uint32_t i = 0; i < c.n; i++) {
        bool hit = false;
        for (uint32_t j = 0; j < n; j++)
            if (want[j].name_len == c.e[i].name_len &&
                !memcmp(want[j].name, c.e[i].name, c.e[i].name_len) &&
                cid_is(&want[j].cid, &c.e[i].cid) && want[j].tsize == c.e[i].tsize)
                hit = true;
        if (!hit) ok = false;
    }
    CHECK(ok, "every listed HAMT entry is a real entry with its CID and Tsize");
    for (uint32_t j = 0; j < n; j++) {
        r = ipfsn_dir_lookup(root, tb_get, s, (const uint8_t *) want[j].name, want[j].name_len,
                             g_buf, sizeof g_buf, &e);
        if (r == 0 && cid_is(&e.cid, &want[j].cid)) found++;
    }
    CHECK(found == n, "every HAMT entry is found by lookup (hash path)");
    {
        uint32_t gets0 = s->gets;
        r = ipfsn_dir_lookup(root, tb_get, s, (const uint8_t *) "no-such-name", 12, g_buf,
                             sizeof g_buf, &e);
        CHECK(r == IPFSN_ERR_NOTFOUND, "HAMT lookup of a missing name");
        CHECK(s->gets - gets0 <= 8, "HAMT lookup fetches only the shards on its path");
    }
    free(c.e);
}

static void test_kubo_hamt(void)
{
    tb_store_t s, b;
    ipfsn_cid_t root, built, want = parse_cid(KUBO_DIR_B_ROOT);
    ipfsn_dirnode_t d;
    tb_ent_t *eb;
    uint64_t t;
    tb_init(&s);
    tb_init(&b);
    load_car(&s, KUBO_DIR_B_CAR, sizeof KUBO_DIR_B_CAR, &root);
    CHECK(cid_is(&root, &want), "dirB CAR root is Kubo's root CID");
    {
        tb_block_t *rb = tb_find(&s, &root);
        int r = ipfsn_dir_parse(rb->data, rb->len, &d);
        CHECK(r == 0 && d.kind == IPFSN_UFS_HAMT && d.fanout == 256 && d.padlen == 2 &&
                  d.bits == 8 && d.shards > 0 && d.entries > 0,
              "dirB root is a fanout-256 HAMT shard with sub-shards and entries");
    }
    eb = make_ents_b(&b, 150);
    check_hamt_listing(&s, &root, eb, 150, "dirB (Kubo CAR) lists 150 entries");
    tb_hamt(&b, eb, 150, &built, &t);
    CHECK(cid_is(&built, &want), "builder: dirB HAMT root == Kubo");
    free(eb);
    tb_free(&s);
    tb_free(&b);

    /* dirC: sharded by Kubo at its DEFAULT threshold */
    tb_init(&b);
    want = parse_cid(KUBO_DIR_C_ROOT);
    eb = make_ents_c(&b, 1200);
    tb_hamt(&b, eb, 1200, &built, &t);
    CHECK(cid_is(&built, &want), "builder: dirC (default-threshold HAMT) root == Kubo");
    check_hamt_listing(&b, &built, eb, 1200, "dirC lists 1200 entries");
    {
        /* the depth reached: at least two levels below the root */
        uint32_t deepest = 0;
        for (uint32_t i = 0; i < b.n; i++) {
            if (ipfsn_dir_parse(b.b[i].data, b.b[i].len, &d) == 0 && d.kind == IPFSN_UFS_HAMT &&
                d.shards)
                deepest++;
        }
        CHECK(deepest >= 2, "dirC has nested sub-shards");
    }
    free(eb);
    tb_free(&b);
}

/* ---- malformed nodes ---- */

static uint32_t mk_node(uint8_t *out, const char **names, const ipfsn_cid_t *cids, uint32_t n,
                        const uint8_t *data, uint32_t dlen, bool tsize)
{
    tb_pb_t w = {0, 0, 0};
    uint32_t len;
    for (uint32_t i = 0; i < n; i++) {
        if (tsize) {
            tb_link(&w, &cids[i], (const uint8_t *) names[i], (uint32_t) strlen(names[i]), 1);
        } else {
            uint8_t cb[IPFSN_CID_BIN_MAX];
            tb_pb_t l = {0, 0, 0};
            int cl = ipfsn_cid_encode(&cids[i], cb, sizeof cb);
            tb_varint(&l, 0x0a);
            tb_varint(&l, (uint32_t) cl);
            tb_raw(&l, cb, (uint32_t) cl);
            tb_varint(&l, 0x12);
            tb_varint(&l, (uint32_t) strlen(names[i]));
            tb_raw(&l, (const uint8_t *) names[i], (uint32_t) strlen(names[i]));
            tb_varint(&w, 0x12);
            tb_varint(&w, l.n);
            tb_raw(&w, l.p, l.n);
            free(l.p);
        }
    }
    tb_varint(&w, 0x0a);
    tb_varint(&w, dlen);
    tb_raw(&w, data, dlen);
    memcpy(out, w.p, w.n);
    len = w.n;
    free(w.p);
    return len;
}

static void test_malformed(void)
{
    static uint8_t blk[8192];
    ipfsn_dirnode_t d;
    ipfsn_cid_t c[4];
    const uint8_t dir[] = {0x08, 0x01};
    uint32_t n;
    for (uint32_t i = 0; i < 4; i++) c[i] = raw_cid(&i, sizeof i);
    {
        const char *nm[] = {"a", "b", "c"};
        n = mk_node(blk, nm, c, 3, dir, 2, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == 0 && d.entries == 3, "well-formed plain dir");
        n = mk_node(blk, nm, c, 3, dir, 2, false);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "link without Tsize");
    }
    {
        const char *nm[] = {"b", "a"};
        n = mk_node(blk, nm, c, 2, dir, 2, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "unsorted links");
    }
    {
        const char *nm[] = {"a", "a"};
        n = mk_node(blk, nm, c, 2, dir, 2, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "duplicate names");
    }
    {
        const char *bad[] = {"", ".", "..", "a/b"};
        for (uint32_t i = 0; i < 4; i++) {
            const char *nm[1] = {bad[i]};
            n = mk_node(blk, nm, c, 1, dir, 2, true);
            CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "bad entry name refused");
        }
    }
    {
        const char *nm[] = {"a"};
        const uint8_t withsize[] = {0x08, 0x01, 0x18, 0x05};
        const uint8_t withdata[] = {0x08, 0x01, 0x12, 0x01, 0x00};
        const uint8_t hash_in_dir[] = {0x08, 0x01, 0x28, 0x22};
        const uint8_t mode_ok[] = {0x08, 0x01, 0x38, 0xa4, 0x03};
        const uint8_t file[] = {0x08, 0x02, 0x18, 0x00};
        const uint8_t dup_type[] = {0x08, 0x01, 0x08, 0x01};
        n = mk_node(blk, nm, c, 1, withsize, sizeof withsize, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "filesize in a directory");
        n = mk_node(blk, nm, c, 1, withdata, sizeof withdata, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "Data bytes in a plain dir");
        n = mk_node(blk, nm, c, 1, hash_in_dir, sizeof hash_in_dir, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "hashType in a plain dir");
        n = mk_node(blk, nm, c, 1, mode_ok, sizeof mode_ok, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == 0, "mode is allowed and skipped");
        n = mk_node(blk, nm, c, 0, file, sizeof file, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_UNSUPP, "a UnixFS file is not a dir");
        n = mk_node(blk, nm, c, 1, dup_type, sizeof dup_type, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "duplicate Type");
    }
    /* HAMT shards: bitfield has bits 0 and 1 set (byte 0x03) */
    {
        const uint8_t good[] = {0x08, 0x05, 0x12, 0x01, 0x03, 0x28, 0x22, 0x30, 0x80, 0x02};
        const uint8_t sha[] = {0x08, 0x05, 0x12, 0x01, 0x03, 0x28, 0x12, 0x30, 0x80, 0x02};
        const uint8_t nohash[] = {0x08, 0x05, 0x12, 0x01, 0x03, 0x30, 0x80, 0x02};
        const uint8_t f300[] = {0x08, 0x05, 0x12, 0x01, 0x03, 0x28, 0x22, 0x30, 0xac, 0x02};
        const uint8_t f4096[] = {0x08, 0x05, 0x12, 0x01, 0x03, 0x28, 0x22, 0x30, 0x80, 0x20};
        const uint8_t pop1[] = {0x08, 0x05, 0x12, 0x01, 0x01, 0x28, 0x22, 0x30, 0x80, 0x02};
        const uint8_t f16[] = {0x08, 0x05, 0x12, 0x01, 0x03, 0x28, 0x22, 0x30, 0x10};
        uint8_t big[48] = {0x08, 0x05, 0x12, 33};
        const char *ok_names[] = {"00a", "01b"};
        const char *lower[] = {"00a", "0b"};
        const char *wrong[] = {"00a", "02b"};
        const char *sw[] = {"01a", "00b"};
        const char *f16n[] = {"0a", "1b"};
        n = mk_node(blk, ok_names, c, 2, good, sizeof good, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == 0 && d.entries == 2, "well-formed HAMT shard");
        n = mk_node(blk, ok_names, c, 2, sha, sizeof sha, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_UNSUPP, "HAMT with another hash");
        n = mk_node(blk, ok_names, c, 2, nohash, sizeof nohash, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_UNSUPP, "HAMT without hashType");
        n = mk_node(blk, ok_names, c, 2, f300, sizeof f300, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "fanout not a power of two");
        n = mk_node(blk, ok_names, c, 2, f4096, sizeof f4096, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "fanout over 1024");
        n = mk_node(blk, ok_names, c, 2, pop1, sizeof pop1, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "bitfield popcount != links");
        n = mk_node(blk, lower, c, 2, good, sizeof good, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "short prefix");
        n = mk_node(blk, wrong, c, 2, good, sizeof good, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "prefix is not the set bit");
        n = mk_node(blk, sw, c, 2, good, sizeof good, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "links out of slot order");
        n = mk_node(blk, f16n, c, 2, f16, sizeof f16, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == 0 && d.padlen == 1 && d.bits == 4,
              "fanout 16: one hex digit");
        memcpy(big + 4 + 33, (const uint8_t[]){0x28, 0x22, 0x30, 0x80, 0x02}, 5);
        big[4 + 32] = 0x03;
        n = mk_node(blk, ok_names, c, 2, big, 4 + 33 + 5, true);
        CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "bitfield longer than fanout/8");
        {
            const char *lc[] = {"00a", "0Ab"};
            const uint8_t bf10[] = {0x08, 0x05, 0x12, 0x02, 0x04, 0x01,
                                    0x28, 0x22, 0x30, 0x80, 0x02};
            const char *lc2[] = {"00a", "0ab"};
            n = mk_node(blk, lc, c, 2, bf10, sizeof bf10, true);
            CHECK(ipfsn_dir_parse(blk, n, &d) == 0, "slot 0x0A uppercase");
            n = mk_node(blk, lc2, c, 2, bf10, sizeof bf10, true);
            CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "lowercase hex prefix");
        }
        {
            const char *sub[] = {"00", "01b"};
            n = mk_node(blk, sub, c, 2, good, sizeof good, true);
            CHECK(ipfsn_dir_parse(blk, n, &d) == IPFSN_ERR_MALFORMED, "sub-shard that is raw");
        }
    }
    /* an entry filed under a slot its hash does not give */
    {
        tb_store_t s;
        ipfsn_cid_t root;
        coll_t cl = {0, 0, 8};
        uint32_t slot, cnt;
        char nm[8];
        const char *names[1];
        uint8_t data[64] = {0x08, 0x05, 0x12, 0x20};
        uint32_t dl;
        int r;
        tb_init(&s);
        cl.e = (tb_ent_t *) calloc(8, sizeof(tb_ent_t));
        slot = (uint32_t) (ipfsn_murmur3_64((const uint8_t *) "hello", 5) >> 56);
        slot = (slot + 1u) & 255u; /* deliberately wrong */
        snprintf(nm, sizeof nm, "%02Xhello", slot);
        names[0] = nm;
        memset(data + 4, 0, 32);
        data[4 + 31 - slot / 8] = (uint8_t) (1u << (slot % 8));
        dl = 4 + 32;
        memcpy(data + dl, (const uint8_t[]){0x28, 0x22, 0x30, 0x80, 0x02}, 5);
        dl += 5;
        {
            uint8_t full[64];
            memcpy(full, data, dl);
            n = mk_node(blk, names, c, 1, full, dl, true);
        }
        CHECK(ipfsn_dir_parse(blk, n, &d) == 0, "misfiled entry parses as a node");
        ipfsn_cid_sha256(IPFSN_MC_DAG_PB, blk, n, &root);
        tb_put(&s, &root, blk, n);
        r = ipfsn_dir_list(&g_walk, &root, tb_get, &s, g_scratch, sizeof g_scratch, collect, &cl,
                           &cnt);
        CHECK(r == IPFSN_ERR_MALFORMED, "listing refuses an entry under the wrong slot");
        {
            ipfsn_dirent_t e;
            r = ipfsn_dir_lookup(&root, tb_get, &s, (const uint8_t *) "hello", 5, g_buf,
                                 sizeof g_buf, &e);
            CHECK(r == IPFSN_ERR_NOTFOUND, "lookup does not find a misfiled entry");
        }
        free(cl.e);
        tb_free(&s);
    }
}

/* ---- fuzz ---- */

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int count_only(void *ctx, const ipfsn_dirent_t *e)
{
    (void) e;
    (*(uint32_t *) ctx)++;
    return 0;
}

static void test_fuzz(void)
{
    tb_store_t s;
    ipfsn_cid_t ra, rb;
    static uint8_t m[65536];
    uint32_t seeds_n = 0, iters = 0, accepted = 0, base_n;
    static uint8_t *seed_d[64];
    static uint32_t seed_l[64];
    tb_init(&s);
    load_car(&s, KUBO_DIR_A_CAR, sizeof KUBO_DIR_A_CAR, &ra);
    load_car(&s, KUBO_DIR_B_CAR, sizeof KUBO_DIR_B_CAR, &rb);
    for (uint32_t i = 0; i < s.n && seeds_n < 64; i++)
        if (s.b[i].cid.codec == IPFSN_MC_DAG_PB) {
            seed_d[seeds_n] = s.b[i].data; /* stable: the store frees data only in tb_free */
            seed_l[seeds_n++] = s.b[i].len;
        }
    base_n = s.n;
    CHECK(seeds_n >= 4, "fuzz seeds: Kubo dag-pb directory nodes");
    for (iters = 0; iters < 40000; iters++) {
        uint32_t si = rnd() % seeds_n, len = seed_l[si], k = 1 + rnd() % 8;
        ipfsn_dirnode_t d;
        ipfsn_cid_t mc;
        ipfsn_dirent_t e;
        uint32_t cnt = 0;
        int r;
        if (iters % 10 == 9) { /* pure noise */
            len = rnd() % 512;
            for (uint32_t i = 0; i < len; i++) m[i] = (uint8_t) rnd();
        } else {
            memcpy(m, seed_d[si], len);
            for (uint32_t j = 0; j < k; j++) {
                uint32_t op = rnd() % 4, at = len ? rnd() % len : 0;
                if (op == 0 && len)
                    m[at] ^= (uint8_t) (1u << (rnd() % 8));
                else if (op == 1 && len)
                    m[at] = (uint8_t) rnd();
                else if (op == 2 && len > 1)
                    len = at + 1; /* truncate */
                else if (len + 1 < sizeof m) {
                    memmove(m + at + 1, m + at, len - at);
                    m[at] = (uint8_t) rnd();
                    len++;
                }
            }
        }
        r = ipfsn_dir_parse(m, len, &d);
        if (r == 0) accepted++;
        /* the mutant under its own CID, children from the real store */
        ipfsn_cid_sha256(IPFSN_MC_DAG_PB, m, len, &mc);
        tb_put(&s, &mc, m, len);
        r = ipfsn_dir_list(&g_walk, &mc, tb_get, &s, g_scratch, sizeof g_scratch, count_only, &cnt,
                           NULL);
        (void) r;
        r = ipfsn_dir_lookup(&mc, tb_get, &s, (const uint8_t *) "f0042", 5, g_buf, sizeof g_buf,
                             &e);
        (void) r;
        while (s.n > base_n) free(s.b[--s.n].data);
    }
    printf("  fuzz: %u mutated directory nodes, %u parsed as valid\n", iters, accepted);
    CHECK(iters >= 20000, "fuzzed at least 20000 inputs");
    tb_free(&s);
}

int main(void)
{
    test_murmur();
    test_kubo_plain();
    test_kubo_hamt();
    test_malformed();
    test_fuzz();
    printf("test_ipfsn_dir: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
