/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_zx_upcheck.c — host test for the update-bucket checker.
 *
 * Build and run (from kernel/):
 *
 *   export CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus
 *   gcc -std=c11 -Wall -Werror -Wextra -O2 -Isrc/ipfs_node -Isrc/update -Isrc/pqsec \
 *       -Isrc/mlkem -Isrc/lpres -Isrc/surplus -Isrc/edp_risk \
 *       src/update/test_zx_upcheck.c src/update/zx_upcheck.c src/update/zx_ipns.c \
 *       src/update/zx_upmanifest.c src/update/zx_upcheck_mldsa.c src/update/update.c \
 *       src/ipfs_node/ipfsn_dir.c src/ipfs_node/ipfsn_multiformats.c \
 *       src/ipfs_node/ipfsn_unixfs.c src/ipfs_node/ipfsn_store.c src/ipfs_node/ipfsn_net.c \
 *       src/ipfs_node/ipfsn_ubh.c src/ipfs_node/ipfsn_fidx.c \
 *       src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c \
 *       src/tls/aead.c src/tls/hkdf.c src/ubh/ubh.c src/event_space/event_envelope.c \
 *       src/pqsec/pq_mldsa65.c src/pqsec/mldsa/[a-z]*.c \
 *       -o /tmp/test_zx_upcheck && /tmp/test_zx_upcheck
 *
 * (Add -fsanitize=address,undefined -g for the sanitizer run. The vendored orlp
 * ed25519 fe.c left-shifts negative values, so build ed25519_verify.c alone with
 * -fno-sanitize=shift-base -c and link the object instead of the source.)
 *
 * Optional live check against a real Kubo gateway (e.g. `ipfs daemon --offline`
 * holding bucket.car and the REC_C_SEQ2 record): set ZXU_LIVE_GATEWAY to its
 * base URL (http://127.0.0.1:8080; set NO_PROXY=* behind a proxy); the test then
 * fetches over HTTP with curl.
 *
 * Fixtures: test_zx_upcheck_vectors.h (Kubo 0.32.1 CAR and IPNS records, the
 * IPNS spec's six vectors, and a manifest signed by zxv_publish_update.py
 * with a deleted throwaway key). Every other ML-DSA key here is generated at
 * run time from /dev/urandom and never leaves the process.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zx_upcheck.h"
#include "../pqsec/pq_security.h"
#include "../robin_debanks/sha256.h"
#include "../ipfs_node/test_ipfsn_dirbuild.h"
#include "../ipfs_node/test_ipfsn_dir_vectors.h"
#include "test_zx_upcheck_vectors.h"

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

#define NOW 1791544448ull /* when the fixture records were made (2026-10-09) */

static ipfsn_cid_t parse_cid(const char *s)
{
    ipfsn_cid_t c;
    memset(&c, 0, sizeof c);
    ipfsn_cid_parse(s, (uint32_t) strlen(s), &c);
    return c;
}

static bool cid_is(const ipfsn_cid_t *a, const ipfsn_cid_t *b)
{
    ipfsn_cid_t x, y;
    ipfsn_cid_to_v1(a, &x);
    ipfsn_cid_to_v1(b, &y);
    return ipfsn_cid_equal(&x, &y);
}

static void urandom(uint8_t *b, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(b, 1, n, f) != n) {
        printf("no /dev/urandom\n");
        exit(2);
    }
    fclose(f);
}

/* ===== the in-memory gateway (and its spies) ================================= */

typedef struct {
    tb_store_t blocks;
    struct {
        uint8_t pk[32];
        const uint8_t *rec;
        uint32_t len;
    } names[8];
    uint32_t nnames;
    bool down;            /* every request fails (network failure)          */
    bool tamper;          /* flip a byte of every block served              */
    int budget;           /* >= 0: requests left before going down          */
    uint32_t requests;    /* every https_get                                */
    uint32_t ipns_reqs;   /* of which /ipns/                                */
    char last_accept[64]; /* Accept of the last request                     */
} gw_t;

static gw_t g_gw;
static uint32_t g_peer_wants;
static uint32_t g_verifies;
static struct {
    int kind;
    char title[64], body[192];
    uint32_t calls;
} g_note;

static const char *live_base;

static int live_get(const char *url, const char *accept, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    char cmd[1024];
    FILE *p;
    size_t n;
    snprintf(cmd, sizeof cmd, "curl -sS -f -m 20 -H 'Accept: %s' '%s'", accept, url);
    if (!(p = popen(cmd, "r"))) return -1;
    n = fread(buf, 1, cap, p);
    if (pclose(p) != 0) return -1;
    *len = (uint32_t) n;
    return 0;
}

static int gw_get(void *ctx, const char *url, const char *accept, uint8_t *buf, uint32_t cap,
                  uint32_t *len)
{
    gw_t *g = (gw_t *) ctx;
    const char *p;
    g->requests++;
    snprintf(g->last_accept, sizeof g->last_accept, "%s", accept);
    if (live_base) return live_get(url, accept, buf, cap, len);
    if (g->down) return -1;
    if (g->budget == 0) return -1;
    if (g->budget > 0) g->budget--;
    if ((p = strstr(url, "/ipns/")) != NULL) {
        const char *q = strchr(p + 6, '?');
        uint8_t pk[32];
        g->ipns_reqs++;
        if (!q || strcmp(q, "?format=ipns-record") ||
            strcmp(accept, "application/vnd.ipfs.ipns-record"))
            return -1;
        if (zxu_ipns_name_parse(p + 6, (uint32_t) (q - p - 6), pk)) return -1;
        for (uint32_t i = 0; i < g->nnames; i++)
            if (!memcmp(g->names[i].pk, pk, 32) && g->names[i].rec) {
                if (g->names[i].len > cap) return -1;
                memcpy(buf, g->names[i].rec, g->names[i].len);
                *len = g->names[i].len;
                return 0;
            }
        return -1;
    }
    if ((p = strstr(url, "/ipfs/")) != NULL) {
        const char *q = strchr(p + 6, '?');
        ipfsn_cid_t cid;
        tb_block_t *b;
        if (!q || strcmp(q, "?format=raw") || strcmp(accept, "application/vnd.ipld.raw")) return -1;
        if (ipfsn_cid_parse(p + 6, (uint32_t) (q - p - 6), &cid)) return -1;
        if (!(b = tb_find(&g->blocks, &cid)) || b->len > cap) return -1;
        memcpy(buf, b->data, b->len);
        *len = b->len;
        if (g->tamper && b->len) buf[b->len / 2] ^= 0x40;
        return 0;
    }
    return -1;
}

static void gw_set_name(const char *name, const uint8_t *rec, uint32_t len)
{
    uint8_t pk[32];
    zxu_ipns_name_parse(name, (uint32_t) strlen(name), pk);
    for (uint32_t i = 0; i < g_gw.nnames; i++)
        if (!memcmp(g_gw.names[i].pk, pk, 32)) {
            g_gw.names[i].rec = rec;
            g_gw.names[i].len = len;
            return;
        }
    memcpy(g_gw.names[g_gw.nnames].pk, pk, 32);
    g_gw.names[g_gw.nnames].rec = rec;
    g_gw.names[g_gw.nnames].len = len;
    g_gw.nnames++;
}

static int peer_want(void *ctx, const uint8_t *cid, uint32_t cl, uint8_t *buf, uint32_t cap,
                     uint32_t *len)
{
    (void) ctx, (void) cid, (void) cl, (void) buf, (void) cap, (void) len;
    g_peer_wants++;
    return -1; /* no ZXV peer has it: fall through to the gateway */
}

static bool spy_verify(void *ctx, const uint8_t pk[ZXU_PK_BYTES], const uint8_t *msg, uint32_t len,
                       const uint8_t *dom, uint32_t dom_len, const uint8_t sig[ZXU_SIG_BYTES])
{
    g_verifies++;
    return zxu_mldsa65_verify(ctx, pk, msg, len, dom, dom_len, sig);
}

static void spy_notify(void *ctx, int kind, const char *title, const char *body)
{
    (void) ctx;
    g_note.kind = kind;
    snprintf(g_note.title, sizeof g_note.title, "%s", title);
    snprintf(g_note.body, sizeof g_note.body, "%s", body);
    g_note.calls++;
}

/* ===== a node with an empty blockstore ======================================== */

typedef struct {
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_t bs;
    ipfsn_bs_entry_t *tab;
    ipfsn_pin_t pins[8];
    ipfsn_node_t node;
    uint8_t *scratch, *net;
    ipfsn_walk_t walk;
    zxu_work_t work;
    zxu_t u;
} rig_t;

static uint8_t g_leaf[IPFSN_BLOCK_MAX];
static uint8_t g_blk[IPFSN_BLOCK_MAX * 2];

static rig_t *rig_new(zxu_config_t *cfg)
{
    rig_t *r = (rig_t *) calloc(1, sizeof *r);
    r->ms.cap = 64u << 20;
    r->ms.buf = (uint8_t *) malloc(r->ms.cap);
    ipfsn_memstore_ops(&r->ms, &r->ops);
    r->tab = (ipfsn_bs_entry_t *) calloc(8192, sizeof(ipfsn_bs_entry_t));
    r->scratch = (uint8_t *) malloc(IPFSN_SCRATCH_MIN);
    r->net = (uint8_t *) malloc(IPFSN_BLOCK_MAX + 64);
    ipfsn_bs_init(&r->bs, &r->ops, r->tab, 8192, r->scratch, IPFSN_SCRATCH_MIN);
    ipfsn_node_init(&r->node, &r->bs, r->pins, 8, r->net, IPFSN_BLOCK_MAX + 64);
    r->node.gw.ctx = &g_gw;
    r->node.gw.https_get = gw_get;
    r->node.gw.base = live_base ? live_base : "https://gw.test";
    r->node.peer.want = peer_want;
    r->node.peer.wire = IPFSN_WIRE_PLAIN;
    r->walk.leaf = g_leaf;
    r->walk.leaf_cap = sizeof g_leaf;
    r->work.blk = g_blk;
    r->work.blk_cap = sizeof g_blk;
    r->work.walk = &r->walk;
    zxu_init(&r->u, cfg, &r->node, &r->work, spy_verify, NULL, spy_notify, NULL);
    return r;
}

static void rig_free(rig_t *r)
{
    free(r->ms.buf);
    free(r->tab);
    free(r->scratch);
    free(r->net);
    free(r);
}

/* ===== fixture files out of the Kubo bucket CAR ================================ */

static tb_store_t g_kubo; /* blocks of the bucket, dirA and dirB */
static ipfsn_cid_t g_bucket;
static uint8_t g_manifest[ZXU_MANIFEST_MAX];
static uint32_t g_manifest_len;
static uint8_t g_sig[ZXU_SIG_BYTES];
static uint8_t g_kernel[4096], g_app[1024], g_readme[256];
static uint32_t g_kernel_len, g_app_len, g_readme_len;

typedef struct {
    uint8_t *p;
    uint32_t cap, n;
} out_t;

static int out_w(void *ctx, const uint8_t *d, uint32_t len)
{
    out_t *o = (out_t *) ctx;
    if (o->n + len > o->cap) return 1;
    memcpy(o->p + o->n, d, len);
    o->n += len;
    return 0;
}

static uint32_t read_path(tb_store_t *s, const ipfsn_cid_t *root, const char *path, uint8_t *buf,
                          uint32_t cap)
{
    ipfsn_dirent_t e;
    static ipfsn_walk_t w;
    out_t o = {buf, cap, 0};
    uint64_t size;
    w.leaf = g_leaf;
    w.leaf_cap = sizeof g_leaf;
    if (ipfsn_dir_resolve(root, tb_get, s, path, (uint32_t) strlen(path), g_blk, sizeof g_blk, &e))
        return 0xFFFFFFFFu;
    if (ipfsn_cat(&w, &e.cid, tb_get, s, out_w, &o, &size)) return 0xFFFFFFFFu;
    return o.n;
}

static void load_car_into(tb_store_t *s, const uint8_t *car, uint32_t len, ipfsn_cid_t *root)
{
    ipfsn_car_t c;
    ipfsn_cid_t cid;
    const uint8_t *b;
    uint32_t bl;
    ipfsn_car_open(&c, car, len);
    if (root) *root = c.roots[0];
    while (ipfsn_car_next(&c, &cid, &b, &bl) == IPFSN_OK) tb_put(s, &cid, b, bl);
}

static void load_fixtures(void)
{
    ipfsn_cid_t want = parse_cid(KUBO_BUCKET_ROOT);
    tb_init(&g_kubo);
    load_car_into(&g_kubo, KUBO_BUCKET_CAR, sizeof KUBO_BUCKET_CAR, &g_bucket);
    load_car_into(&g_kubo, KUBO_DIR_A_CAR, sizeof KUBO_DIR_A_CAR, NULL);
    load_car_into(&g_kubo, KUBO_DIR_B_CAR, sizeof KUBO_DIR_B_CAR, NULL);
    CHECK(cid_is(&g_bucket, &want), "bucket CAR root is Kubo's root");
    g_manifest_len =
        read_path(&g_kubo, &g_bucket, ZXU_MANIFEST_NAME, g_manifest, sizeof g_manifest);
    CHECK(g_manifest_len > 0 && g_manifest_len < sizeof g_manifest, "manifest read from the CAR");
    CHECK(read_path(&g_kubo, &g_bucket, ZXU_SIG_NAME, g_sig, sizeof g_sig) == ZXU_SIG_BYTES,
          "signature read from the CAR");
    g_kernel_len =
        read_path(&g_kubo, &g_bucket, "zxv-kernel-x86_64.bin", g_kernel, sizeof g_kernel);
    g_app_len = read_path(&g_kubo, &g_bucket, "apps/notes.zapp", g_app, sizeof g_app);
    g_readme_len = read_path(&g_kubo, &g_bucket, "README.txt", g_readme, sizeof g_readme);
    CHECK(g_kernel_len == 3000 && g_app_len == 360 && g_readme_len == 45, "bucket files read");
}

/* ===== runtime keys and buckets ================================================ */

typedef struct {
    uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES];
} key_t;

static void key_new(key_t *k)
{
    uint8_t seed[32];
    urandom(seed, sizeof seed);
    pq_mldsa65_keygen(seed, k->pk, k->sk);
    memset(seed, 0, sizeof seed);
}

static void sign_buf(const key_t *k, const uint8_t *m, uint32_t len, uint8_t sig[ZXU_SIG_BYTES])
{
    uint8_t rnd[32];
    urandom(rnd, sizeof rnd);
    pq_mldsa65_sign(k->sk, m, len, (const uint8_t *) ZXU_SIG_CONTEXT,
                    (uint32_t) strlen(ZXU_SIG_CONTEXT), rnd, sig);
}

typedef struct {
    const char *path; /* top-level or "dir/name" (one level) */
    const uint8_t *data;
    uint32_t len;
    uint32_t chunk; /* 0 = Kubo default */
} bfile_t;

/* A bucket directory built at run time into `s` (and the gateway store). */
static ipfsn_cid_t build_bucket(tb_store_t *s, const bfile_t *f, uint32_t n)
{
    tb_ent_t top[16], sub[8];
    uint32_t nt = 0, ns = 0;
    char subname[64] = "";
    ipfsn_cid_t root, c;
    uint64_t t;
    for (uint32_t i = 0; i < n; i++) {
        const char *sl = strchr(f[i].path, '/');
        tb_file_chunked(s, f[i].data, f[i].len, f[i].chunk, &c, &t);
        if (sl) {
            snprintf(subname, sizeof subname, "%.*s", (int) (sl - f[i].path), f[i].path);
            tb_ent(&sub[ns++], sl + 1, &c, t);
        } else {
            tb_ent(&top[nt++], f[i].path, &c, t);
        }
    }
    if (ns) {
        tb_dir(s, sub, ns, &c, &t);
        tb_ent(&top[nt++], subname, &c, t);
    }
    tb_dir(s, top, nt, &root, &t);
    return root;
}

static uint32_t make_manifest(char *out, uint32_t cap, const char *release, const char *minv,
                              uint64_t issued, const bfile_t *f, const char **kinds,
                              const char **arches, const char **vers, uint32_t n)
{
    uint32_t pos = 0;
    int w;
    tb_store_t tmp;

    tb_init(&tmp);

    w = snprintf(out + pos, cap - pos,
                 "zxv-update-manifest 1\nrelease %s\nmin-version %s\nissued %llu\n", release, minv,
                 (unsigned long long) issued);
    if (w < 0 || (uint32_t) w >= cap - pos) {
        pos = cap;
        goto done;
    }
    pos += (uint32_t) w;

    for (uint32_t i = 0; i < n; i++) {
        ipfsn_cid_t c;
        uint64_t t;
        char cs[IPFSN_CID_STR_MAX];
        uint8_t h[32];
        char hx[65];
        tb_file_chunked(&tmp, f[i].data, f[i].len, f[i].chunk, &c, &t);
        ipfsn_cid_to_string(&c, cs, sizeof cs);
        sha256(f[i].data, f[i].len, h);
        for (int j = 0; j < 32; j++) snprintf(hx + 2 * j, 3, "%02x", h[j]);

        w = snprintf(out + pos, cap - pos, "entry %s %s %s %u %s %s %s\n", kinds[i], arches[i],
                     vers[i], f[i].len, cs, hx, f[i].path);
        if (w < 0 || (uint32_t) w >= cap - pos) {
            pos = cap;
            goto done;
        }
        pos += (uint32_t) w;
    }

    w = snprintf(out + pos, cap - pos, "end\n");
    if (w < 0 || (uint32_t) w >= cap - pos) {
        pos = cap;
        goto done;
    }
    pos += (uint32_t) w;

done:
    tb_free(&tmp);
    return pos;
}

static void gw_add_store(tb_store_t *s)
{
    for (uint32_t i = 0; i < s->n; i++)
        tb_put(&g_gw.blocks, &s->b[i].cid, s->b[i].data, s->b[i].len);
}

/* ===== tests: versions and manifests ============================================ */

static void test_versions(void)
{
    uint32_t v;
    char s[16];
    CHECK(zxu_version_parse("1.2.3", 5, &v) == 0 && v == ZXU_VERSION(1, 2, 3), "version 1.2.3");
    CHECK(zxu_version_parse("999.999.999", 11, &v) == 0 && v == 999999999u, "version max");
    CHECK(zxu_version_parse("01.2.3", 6, &v) != 0, "leading zero refused");
    CHECK(zxu_version_parse("1.2", 3, &v) != 0, "two parts refused");
    CHECK(zxu_version_parse("1.2.3.4", 7, &v) != 0, "four parts refused");
    CHECK(zxu_version_parse("1000.0.0", 8, &v) != 0, "1000 refused");
    CHECK(zxu_version_parse("1..3", 4, &v) != 0, "empty part refused");
    CHECK(zxu_version_format(ZXU_VERSION(10, 0, 7), s, sizeof s) == 6 && !strcmp(s, "10.0.7"),
          "version format");
    CHECK(zxu_version_format(ZXU_VERSION(999, 999, 999), s, sizeof s) == 11, "format max");
}

static void test_manifest_parse(void)
{
    zxu_manifest_t m;
    ipfsn_cid_t kc = parse_cid("bafkreiell7aotnkzvtmgusibpfbxa7ct4kb7e25wfhfsbphjco5mtf24ee");
    uint8_t h[32];
    int r = zxu_manifest_parse(g_manifest, g_manifest_len, &m);
    CHECK(r == 0, "the tool-made manifest parses");
    CHECK(m.release == ZXU_VERSION(1, 2, 0) && m.min_version == ZXU_VERSION(1, 0, 0) &&
              m.issued == 1791504000ull && m.n == 2,
          "release, min-version, issued, entries");
    CHECK(!strcmp(m.e[0].path, "apps/notes.zapp") && m.e[0].kind == ZXU_KIND_APP &&
              m.e[0].arch == ZXU_ARCH_ANY && m.e[0].version == ZXU_VERSION(1, 1, 0) &&
              m.e[0].size == 360,
          "entry 0 (sorted by path)");
    sha256(g_kernel, g_kernel_len, h);
    CHECK(!strcmp(m.e[1].path, "zxv-kernel-x86_64.bin") && m.e[1].kind == ZXU_KIND_KERNEL &&
              m.e[1].arch == ZXU_ARCH_X86_64 && m.e[1].size == 3000 && cid_is(&m.e[1].cid, &kc) &&
              !memcmp(m.e[1].sha256, h, 32),
          "entry 1: CID is Kubo's, sha256 is the file's");

    /* negative cases: each is a small edit of a good manifest */
    {
        static const char *good =
            "zxv-update-manifest 1\nrelease 1.2.0\nmin-version 1.0.0\nissued 5\n"
            "entry kernel x86_64 1.2.0 3000 "
            "bafkreiell7aotnkzvtmgusibpfbxa7ct4kb7e25wfhfsbphjco5mtf24ee "
            "8b5fc0e9b559acd86a49017943707c53e283f26bb629cb20bce913bac9975c21 a/k.bin\n"
            "end\n";
        struct {
            const char *from, *to, *what;
        } ed[] = {
            {"\n", "\r\n", "CRLF"},
            {"end\n", "end\nx", "bytes after end"},
            {"end\n", "end", "no final LF"},
            {"end\n", "", "no end line"},
            {"zxv-update-manifest 1", "zxv-update-manifest 2", "unknown format version"},
            {"release 1.2.0", "release  1.2.0", "double space"},
            {"release 1.2.0", "release 1.2.0 ", "trailing space"},
            {"release 1.2.0", "release 1.02.0", "leading zero version"},
            {"min-version 1.0.0", "min-version 1.3.0", "min-version above release"},
            {"issued 5", "issued 05", "leading zero issued"},
            {"issued 5", "issued 99999999999999999999", "issued overflow"},
            {"issued 5", "issued -5", "negative issued"},
            {"entry kernel", "entry kernal", "unknown kind"},
            {"x86_64", "x86-64", "unknown arch"},
            {"1.2.0 3000", "1.3.0 3000", "entry version above release"},
            {" 3000 ", " 4294967296 ", "size >= 2^32"},
            {" 3000 ", " 03000 ", "size leading zero"},
            {"bafkreiell7", "Bafkreiell7", "CID not base32 lower"},
            {"bafkreiell7aotnkzvtmgusibpfbxa7ct4kb7e25wfhfsbphjco5mtf24ee",
             "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5o", "CIDv0"},
            {"8b5fc0e9", "8B5FC0E9", "uppercase sha256 hex"},
            {"9975c21 ", "9975c2 ", "short sha256"},
            {" a/k.bin", " a/../k.bin", "path with .."},
            {" a/k.bin", " /a/k.bin", "absolute path"},
            {" a/k.bin", " a//k.bin", "empty path component"},
            {" a/k.bin", " a/k bin", "space in path"},
            {" a/k.bin", " zxv-update.manifest", "manifest lists itself"},
            {" a/k.bin", " a/k\x01.bin", "control byte"},
            {"entry kernel", "entri kernel", "bad keyword"},
        };
        zxu_manifest_t mm;
        char buf[2048];
        CHECK(zxu_manifest_parse((const uint8_t *) good, (uint32_t) strlen(good), &mm) == 0,
              "negative-case base is good");
        for (uint32_t i = 0; i < sizeof ed / sizeof ed[0]; i++) {
            const char *p = strstr(good, ed[i].from);
            uint32_t n;
            if (!p) {
                printf("  (edit %s not applicable)\n", ed[i].what);
                failures++;
                continue;
            }
            if (!strcmp(ed[i].from, "\n")) { /* replace every LF */
                n = 0;
                for (const char *q = good; *q; q++) {
                    if (*q == '\n') buf[n++] = '\r';
                    buf[n++] = *q;
                }
                buf[n] = 0;
            } else {
                n = (uint32_t) snprintf(buf, sizeof buf, "%.*s%s%s", (int) (p - good), good,
                                        ed[i].to, p + strlen(ed[i].from));
            }
            if (zxu_manifest_parse((const uint8_t *) buf, n, &mm) != ZXU_ERR_MALFORMED) {
                printf("  manifest edit accepted: %s\n", ed[i].what);
                failures++;
            } else {
                passes++;
            }
        }
        /* unsorted / duplicate entries, and the 32-entry limit */
        {
            const char *line = "entry data any 1.0.0 1 "
                               "bafkreiell7aotnkzvtmgusibpfbxa7ct4kb7e25wfhfsbphjco5mtf24ee "
                               "8b5fc0e9b559acd86a49017943707c53e283f26bb629cb20bce913bac9975c21 ";
            char big[16384];
            uint32_t n;
            n = (uint32_t) snprintf(big, sizeof big,
                                    "zxv-update-manifest 1\nrelease 1.0.0\nmin-version 1.0.0\n"
                                    "issued 1\n%sb\n%sa\nend\n",
                                    line, line);
            CHECK(zxu_manifest_parse((const uint8_t *) big, n, &mm) == ZXU_ERR_MALFORMED,
                  "unsorted entries refused");
            n = (uint32_t) snprintf(big, sizeof big,
                                    "zxv-update-manifest 1\nrelease 1.0.0\nmin-version 1.0.0\n"
                                    "issued 1\n%sa\n%sa\nend\n",
                                    line, line);
            CHECK(zxu_manifest_parse((const uint8_t *) big, n, &mm) == ZXU_ERR_MALFORMED,
                  "duplicate entries refused");
            for (uint32_t cnt = 32; cnt <= 33; cnt++) {
                int w;
                int ok = 1;
                n = 0;
                w = snprintf(big, sizeof big,
                             "zxv-update-manifest 1\nrelease 1.0.0\nmin-version "
                             "1.0.0\nissued 1\n");
                if (w < 0 || (size_t) w >= sizeof big) {
                    ok = 0;
                } else {
                    n = (uint32_t) w;
                }
                for (uint32_t i = 0; ok && i < cnt; i++) {
                    size_t rem = sizeof big - (size_t) n;
                    w = snprintf(big + n, rem, "%sf%03u\n", line, i);
                    if (w < 0 || (size_t) w >= rem) {
                        ok = 0;
                        break;
                    }
                    n += (uint32_t) w;
                }
                if (ok) {
                    size_t rem = sizeof big - (size_t) n;
                    w = snprintf(big + n, rem, "end\n");
                    if (w < 0 || (size_t) w >= rem)
                        ok = 0;
                    else
                        n += (uint32_t) w;
                }
                r = ok ? zxu_manifest_parse((const uint8_t *) big, n, &mm) : ZXU_ERR_MALFORMED;
                CHECK(cnt == 32 ? r == 0 && mm.n == 32 : r == ZXU_ERR_MALFORMED,
                      cnt == 32 ? "32 entries accepted" : "33 entries refused");
            }
            n = (uint32_t) snprintf(big, sizeof big,
                                    "zxv-update-manifest 1\nrelease 1.0.0\nmin-version 1.0.0\n"
                                    "issued 1\nend\n");
            CHECK(zxu_manifest_parse((const uint8_t *) big, n, &mm) == ZXU_ERR_MALFORMED,
                  "zero entries refused");
        }
    }
}

static void test_manifest_sig(key_t *other)
{
    uint8_t(*keys)[ZXU_PK_BYTES] = (uint8_t(*)[ZXU_PK_BYTES]) malloc(2 * ZXU_PK_BYTES);
    uint8_t *m = (uint8_t *) malloc(g_manifest_len);
    uint8_t sig2[ZXU_SIG_BYTES];
    uint32_t which = 99;
    memcpy(keys[0], TEST_RELEASE_PK, ZXU_PK_BYTES);
    memcpy(keys[1], other->pk, ZXU_PK_BYTES);
    CHECK(sizeof TEST_RELEASE_PK == ZXU_PK_BYTES, "tool public key is ML-DSA-65 sized");
    CHECK(zxu_manifest_verify(spy_verify, NULL, (const uint8_t(*)[ZXU_PK_BYTES]) keys, 1,
                              g_manifest, g_manifest_len, g_sig, &which) == ZXU_OK &&
              which == 0,
          "signature from zxv_publish_update.py verifies in the kernel (interop)");
    memcpy(m, g_manifest, g_manifest_len);
    m[g_manifest_len / 2] ^= 1;
    CHECK(zxu_manifest_verify(spy_verify, NULL, (const uint8_t(*)[ZXU_PK_BYTES]) keys, 2, m,
                              g_manifest_len, g_sig, &which) == ZXU_ERR_SIG,
          "tampered manifest: no key verifies");
    CHECK(zxu_manifest_verify(spy_verify, NULL, (const uint8_t(*)[ZXU_PK_BYTES])(keys + 1), 1,
                              g_manifest, g_manifest_len, g_sig, &which) == ZXU_ERR_SIG,
          "release signature under an untrusted key set: refused");
    sign_buf(other, g_manifest, g_manifest_len, sig2);
    CHECK(zxu_manifest_verify(spy_verify, NULL, (const uint8_t(*)[ZXU_PK_BYTES]) keys, 1,
                              g_manifest, g_manifest_len, sig2, &which) == ZXU_ERR_SIG,
          "signed by a key the bucket does not trust: refused");
    CHECK(zxu_manifest_verify(spy_verify, NULL, (const uint8_t(*)[ZXU_PK_BYTES]) keys, 2,
                              g_manifest, g_manifest_len, sig2, &which) == ZXU_OK &&
              which == 1,
          "the same signature once that key is trusted");
    CHECK(!pq_mldsa65_verify(TEST_RELEASE_PK, g_manifest, g_manifest_len, (const uint8_t *) "x", 1,
                             g_sig),
          "the context string is part of the signature");
    free(keys);
    free(m);
}

/* ===== tests: IPNS ==================================================================== */

static void test_ipns_names(void)
{
    uint8_t a[32], b[32], c[32], mh[38];
    char s[160], pid[80];
    int n;
    CHECK(zxu_ipns_name_parse(NAME_C, (uint32_t) strlen(NAME_C), a) == 0, "k51 name parses");
    CHECK(zxu_ipns_name_parse(NAME_C_B32, (uint32_t) strlen(NAME_C_B32), b) == 0 &&
              !memcmp(a, b, 32),
          "base32 libp2p-key form gives the same key (Kubo --ipns-base=base32)");
    n = zxu_ipns_name_string(a, s, sizeof s);
    CHECK(n > 0 && !strcmp(s, NAME_C), "canonical k51 string round-trips");
    mh[0] = 0x00;
    mh[1] = 36;
    mh[2] = 0x08;
    mh[3] = 0x01;
    mh[4] = 0x12;
    mh[5] = 0x20;
    memcpy(mh + 6, a, 32);
    ipfsn_base58_encode(mh, 38, pid, sizeof pid);
    CHECK(!strncmp(pid, "12D3KooW", 8), "peer-ID form is 12D3KooW...");
    CHECK(zxu_ipns_name_parse(pid, (uint32_t) strlen(pid), c) == 0 && !memcmp(a, c, 32),
          "12D3KooW peer ID gives the same key");
    {
        uint32_t ok = 0;
        for (uint32_t i = 0; i < 2000; i++) {
            uint8_t k[32], k2[32];
            urandom(k, 32);
            if (i == 0) memset(k, 0, 32);
            if (i == 1) memset(k, 0xff, 32);
            n = zxu_ipns_name_string(k, s, sizeof s);
            if (n > 0 && zxu_ipns_name_parse(s, (uint32_t) n, k2) == 0 && !memcmp(k, k2, 32)) ok++;
        }
        CHECK(ok == 2000, "base36 name encode/parse round-trips 2000 keys");
    }
    CHECK(zxu_ipns_name_parse("K51qzi5uqu5dgjhmolgw2n5scq5gimlj8nk1mf0dx8oz5ex0kzeb73vadsr6pg", 62,
                              a) != 0,
          "uppercase multibase prefix refused");
    CHECK(zxu_ipns_name_parse(KUBO_DIR_A_ROOT_S, (uint32_t) strlen(KUBO_DIR_A_ROOT_S), a) != 0,
          "a dag-pb CID is not an IPNS name");
    CHECK(zxu_ipns_name_parse("QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5o", 46, a) != 0,
          "an RSA-style (sha256) peer ID is refused");
}

static void test_rfc3339(void)
{
    uint64_t t;
    uint32_t ns;
#define RFC(s) zxu_rfc3339_parse((const uint8_t *) (s), (uint32_t) strlen(s), &t, &ns)
    CHECK(RFC("1970-01-01T00:00:00Z") == 0 && t == 0 && ns == 0, "epoch");
    CHECK(RFC("1999-12-31T23:59:59Z") == 0 && t == 946684799ull, "1999-12-31");
    CHECK(RFC("2000-02-29T12:00:00+01:00") == 0 && t == 951822000ull, "leap day with offset");
    CHECK(RFC("2036-10-06T11:15:36.865617636Z") == 0 && t == 2106904536ull && ns == 865617636u,
          "Kubo EOL with nanoseconds");
    CHECK(RFC("2036-10-06T11:15:36.5Z") == 0 && ns == 500000000u, "short fraction");
    CHECK(RFC("2001-02-29T00:00:00Z") != 0, "2001-02-29 refused");
    CHECK(RFC("2100-02-29T00:00:00Z") != 0, "2100 is not a leap year");
    CHECK(RFC("2000-02-30T00:00:00Z") != 0, "Feb 30 refused");
    CHECK(RFC("1969-12-31T23:59:59Z") != 0, "before 1970 refused");
    CHECK(RFC("2026-10-09t00:00:00Z") != 0, "lowercase t refused");
    CHECK(RFC("2026-10-09T00:00:00") != 0, "no zone refused");
    CHECK(RFC("2026-10-09T00:00:00.Z") != 0, "empty fraction refused");
    CHECK(RFC("2026-10-09T00:00:00.1234567890Z") != 0, "10 fraction digits refused");
    CHECK(RFC("2026-10-09T24:00:00Z") != 0, "hour 24 refused");
    CHECK(RFC("2026-10-09T00:00:60Z") != 0, "leap second refused");
#undef RFC
}

typedef struct {
    const uint8_t *rec;
    uint32_t len;
    const char *name;
    int want;
    const char *value; /* expected Value (NULL = do not check) */
    const char *what;
} recv_t;

static uint8_t g_msg[ZXU_IPNS_RECORD_MAX + 16];

static void test_ipns_records(void)
{
    zxu_ipns_t o;
    uint8_t pk[32];
    char vs[160];
    recv_t v[] = {
        {REC_C_SEQ2, sizeof REC_C_SEQ2, NAME_C, ZXU_OK, "/ipfs/" KUBO_BUCKET_ROOT,
         "Kubo V1+V2 record, seq 2"},
        {REC_C_SEQ1_V2, sizeof REC_C_SEQ1_V2, NAME_C, ZXU_OK, "/ipfs/" KUBO_DIR_B_ROOT_S,
         "Kubo V2-only record, seq 1"},
        {REC_C_SEQ0, sizeof REC_C_SEQ0, NAME_C, ZXU_OK, "/ipfs/" KUBO_DIR_A_ROOT_S,
         "Kubo record, seq 0"},
        {REC_A_V2, sizeof REC_A_V2, NAME_A, ZXU_OK, NULL, "Kubo V2-only record (name A)"},
        {REC_A_SEQ0, sizeof REC_A_SEQ0, NAME_C, ZXU_ERR_SIG, NULL,
         "record of name A presented for name C (wrong key)"},
        {REC_B_SHORT, sizeof REC_B_SHORT, NAME_A, ZXU_ERR_SIG, NULL, "name B under name A"},
        {SPEC_REC_V1, sizeof SPEC_REC_V1, SPEC_NAME_V1, ZXU_ERR_MALFORMED, NULL,
         "spec: V1-only -> invalid"},
        {SPEC_REC_V1V2, sizeof SPEC_REC_V1V2, SPEC_NAME_V1V2, ZXU_OK, SPEC_VALUE_V1V2,
         "spec: V1+V2 -> valid"},
        {SPEC_REC_BADV1VALUE, sizeof SPEC_REC_BADV1VALUE, SPEC_NAME_BADV1VALUE, ZXU_ERR_MALFORMED,
         NULL, "spec: V1 value differs from V2 -> invalid"},
        {SPEC_REC_BADSIG2, sizeof SPEC_REC_BADSIG2, SPEC_NAME_BADSIG2, ZXU_ERR_SIG, NULL,
         "spec: broken signatureV2 -> invalid"},
        {SPEC_REC_BADSIG1, sizeof SPEC_REC_BADSIG1, SPEC_NAME_BADSIG1, ZXU_OK, SPEC_VALUE_BADSIG1,
         "spec: broken signatureV1 only -> valid"},
        {SPEC_REC_V2, sizeof SPEC_REC_V2, SPEC_NAME_V2, ZXU_OK, NULL, "spec: V2-only -> valid"},
    };
    for (uint32_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        int r;
        zxu_ipns_name_parse(v[i].name, (uint32_t) strlen(v[i].name), pk);
        r = zxu_ipns_verify(v[i].rec, v[i].len, pk, NOW, g_msg, sizeof g_msg, &o);
        if (r == v[i].want && v[i].value && r == 0) {
            int n = ipfsn_cid_to_string(&o.value, vs + 6, sizeof vs - 6);
            memcpy(vs, "/ipfs/", 6);
            if (n < 0 || strcmp(vs, v[i].value)) r = 999;
        }
        if (r != v[i].want) printf("  got %d want %d\n", r, v[i].want);
        CHECK(r == v[i].want, v[i].what);
    }
    zxu_ipns_name_parse(NAME_C, (uint32_t) strlen(NAME_C), pk);
    CHECK(zxu_ipns_verify(REC_C_SEQ2, sizeof REC_C_SEQ2, pk, NOW, g_msg, sizeof g_msg, &o) == 0 &&
              o.sequence == 2 && o.has_v1 && o.ttl_ns == 3600000000000ull,
          "seq 2, V1 fields matched, TTL 1h");
    CHECK(zxu_ipns_verify(REC_C_SEQ1_V2, sizeof REC_C_SEQ1_V2, pk, NOW, g_msg, sizeof g_msg, &o) ==
                  0 &&
              o.sequence == 1 && !o.has_v1,
          "seq 1, V2 only");
    /* expiry */
    CHECK(zxu_ipns_verify(REC_C_SEQ2, sizeof REC_C_SEQ2, pk, 4102444800ull, g_msg, sizeof g_msg,
                          &o) == ZXU_ERR_EXPIRED,
          "Kubo record checked in 2100: expired");
    {
        zxu_ipns_name_parse(NAME_B, (uint32_t) strlen(NAME_B), pk);
        int r0 = zxu_ipns_verify(REC_B_SHORT, sizeof REC_B_SHORT, pk, NOW, g_msg, sizeof g_msg, &o);
        uint64_t eol = o.eol;
        CHECK(r0 == 0 && eol > NOW && eol < NOW + 600, "2-minute record valid when made");
        CHECK(zxu_ipns_verify(REC_B_SHORT, sizeof REC_B_SHORT, pk, eol, g_msg, sizeof g_msg, &o) ==
                  0,
              "valid at its EOL second (nanoseconds left)");
        CHECK(zxu_ipns_verify(REC_B_SHORT, sizeof REC_B_SHORT, pk, eol + 1, g_msg, sizeof g_msg,
                              &o) == ZXU_ERR_EXPIRED,
              "expired one second later");
    }
    /* tampering: every bit of the signed data and of signatureV2 */
    {
        uint8_t *t = (uint8_t *) malloc(sizeof REC_C_SEQ2);
        uint32_t bad = 0, tried = 0, v1_ok = 0;
        zxu_ipns_name_parse(NAME_C, (uint32_t) strlen(NAME_C), pk);
        for (uint32_t i = 0; i < sizeof REC_C_SEQ2; i++) {
            for (uint32_t bit = 0; bit < 8; bit++) {
                int r;
                memcpy(t, REC_C_SEQ2, sizeof REC_C_SEQ2);
                t[i] ^= (uint8_t) (1u << bit);
                r = zxu_ipns_verify(t, sizeof REC_C_SEQ2, pk, NOW, g_msg, sizeof g_msg, &o);
                tried++;
                if (r == 0) {
                    /* only a flip inside the unverified legacy signatureV1 may pass, and then
                     * the signed content is unchanged */
                    ipfsn_cid_t want = parse_cid(KUBO_BUCKET_ROOT);
                    /* offsets 67..134: signatureV1 (tag, length, 64 bytes) and the
                     * validityType=0 field after it, whose absence also reads as 0 */
                    if (cid_is(&o.value, &want) && o.sequence == 2 && i >= 67 && i <= 134)
                        v1_ok++;
                    else
                        bad++;
                }
            }
        }
        printf("  ipns: %u single-bit tamperings, %u accepted (signatureV1 / zero validityType)\n",
               tried, v1_ok);
        CHECK(bad == 0, "no tampering changes what a valid record says");
        CHECK(v1_ok >= 64 * 8 && v1_ok <= 64 * 8 + 8, "only signatureV1 is unverified");
        free(t);
    }
    {
        uint8_t big[ZXU_IPNS_RECORD_MAX + 1];
        memset(big, 0, sizeof big);
        CHECK(zxu_ipns_verify(big, sizeof big, pk, NOW, g_msg, sizeof g_msg, &o) ==
                  ZXU_ERR_MALFORMED,
              "record over 10 KiB refused");
    }
}

/* ===== tests: fuzz ====================================================================== */

static uint32_t g_rng = 0xC0FFEEu;
static uint32_t rnd32(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static uint32_t mutate(uint8_t *m, uint32_t len, uint32_t cap)
{
    uint32_t k = 1 + rnd32() % 6;
    for (uint32_t j = 0; j < k; j++) {
        uint32_t op = rnd32() % 5, at = len ? rnd32() % len : 0;
        if (op == 0 && len)
            m[at] ^= (uint8_t) (1u << (rnd32() % 8));
        else if (op == 1 && len)
            m[at] = (uint8_t) rnd32();
        else if (op == 2 && len > 1)
            len = at + 1;
        else if (op == 3 && len > 2) {
            memmove(m + at, m + at + 1, len - at - 1);
            len--;
        } else if (len + 1 < cap) {
            memmove(m + at + 1, m + at, len - at);
            m[at] = (uint8_t) (rnd32() % 3 == 0 ? ' ' : (rnd32() % 3 == 0 ? '\n' : rnd32()));
            len++;
        }
    }
    return len;
}

static void test_fuzz(void)
{
    static uint8_t m[ZXU_MANIFEST_MAX + 64];
    zxu_manifest_t mm;
    zxu_ipns_t o;
    uint8_t pk[32];
    uint32_t ok = 0, i;
    for (i = 0; i < 30000; i++) {
        uint32_t len = g_manifest_len;
        memcpy(m, g_manifest, len);
        len = mutate(m, len, sizeof m);
        if (zxu_manifest_parse(m, len, &mm) == 0) ok++;
    }
    printf("  fuzz: %u mutated manifests, %u still well-formed\n", i, ok);
    CHECK(i >= 20000, "manifest parser fuzzed >= 20000 inputs");
    {
        const uint8_t *recs[] = {REC_C_SEQ2, REC_C_SEQ1_V2, SPEC_REC_V1V2, SPEC_REC_V2};
        const uint32_t lens[] = {sizeof REC_C_SEQ2, sizeof REC_C_SEQ1_V2, sizeof SPEC_REC_V1V2,
                                 sizeof SPEC_REC_V2};
        const char *names[] = {NAME_C, NAME_C, SPEC_NAME_V1V2, SPEC_NAME_V2};
        uint32_t valid = 0, changed = 0;
        for (i = 0; i < 30000; i++) {
            uint32_t w = rnd32() % 4, len = lens[w];
            zxu_ipns_t ref;
            memcpy(m, recs[w], len);
            len = mutate(m, len, ZXU_IPNS_RECORD_MAX + 1);
            zxu_ipns_name_parse(names[w], (uint32_t) strlen(names[w]), pk);
            if (zxu_ipns_verify(m, len, pk, NOW, g_msg, sizeof g_msg, &o) == 0) {
                valid++;
                zxu_ipns_verify(recs[w], lens[w], pk, NOW, g_msg, sizeof g_msg, &ref);
                if (!cid_is(&o.value, &ref.value) || o.sequence != ref.sequence || o.eol != ref.eol)
                    changed++;
            }
        }
        printf("  fuzz: %u mutated IPNS records, %u still valid, %u with changed content\n", i,
               valid, changed);
        CHECK(i >= 20000, "IPNS verifier fuzzed >= 20000 inputs");
        CHECK(changed == 0, "no mutation changes the content of a valid record");
    }
    for (i = 0; i < 25000; i++) { /* names and timestamps from noise and mutation */
        char s[96];
        uint64_t t;
        uint32_t ns, len;
        memcpy(s, NAME_C, strlen(NAME_C));
        len = mutate((uint8_t *) s, (uint32_t) strlen(NAME_C), sizeof s);
        zxu_ipns_name_parse(s, len, pk);
        memcpy(s, "2036-10-06T11:15:36.865617636Z", 30);
        len = mutate((uint8_t *) s, 30, sizeof s);
        zxu_rfc3339_parse((const uint8_t *) s, len, &t, &ns);
    }
    CHECK(i >= 20000, "name and RFC 3339 parsers fuzzed >= 20000 inputs");
}

/* ===== tests: the checker =============================================================== */

static zxu_config_t *cfg_new(bool enabled, uint32_t installed)
{
    zxu_config_t *c = (zxu_config_t *) calloc(1, sizeof *c);
    zxu_config_init(c, enabled, ZXU_ARCH_X86_64, installed, NULL);
    return c;
}

static zxu_result_t *g_res; /* big: allocated once */

static void gw_reset(void)
{
    g_gw.down = false;
    g_gw.tamper = false;
    g_gw.budget = -1;
    g_gw.requests = 0;
    g_gw.ipns_reqs = 0;
    g_peer_wants = 0;
}

static void test_bucket_api(void)
{
    zxu_config_t *c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    uint32_t idx, i2;
    key_t *k = (key_t *) malloc(sizeof *k);
    memset(k->pk, 7, sizeof k->pk);
    CHECK(zxu_bucket_count(c) == 1 && c->b[0].builtin && c->b[0].used && c->b[0].enabled &&
              c->b[0].source == ZXU_SRC_CID && c->b[0].nkeys == 0,
          "built-in bucket present, enabled, no keys");
    {
        ipfsn_cid_t want = parse_cid(ZXU_BUILTIN_CID);
        CHECK(cid_is(&c->b[0].cid, &want), "built-in bucket is the owner's CID");
    }
    CHECK(zxu_bucket_remove(c, 0) == ZXU_ERR_BUILTIN, "built-in bucket cannot be removed");
    CHECK(zxu_bucket_enable(c, 0, false) == 0 && !c->b[0].enabled, "built-in bucket can be off");
    CHECK(zxu_bucket_add_cid(c, "dup", ZXU_BUILTIN_CID, &idx) == ZXU_ERR_EXISTS,
          "same source twice refused");
    CHECK(zxu_bucket_add_cid(c, "raw",
                             "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku",
                             &idx) == ZXU_ERR_UNSUPP,
          "a raw (file) CID is not a bucket");
    CHECK(zxu_bucket_add_cid(c, "junk", "bafyjunk", &idx) == ZXU_ERR_MALFORMED, "junk CID refused");
    CHECK(zxu_bucket_add_ipns(c, "friend\x01", NAME_C, &idx) == 0 && idx == 1 &&
              c->b[1].source == ZXU_SRC_IPNS && !strcmp(c->b[1].label, "friend?"),
          "IPNS bucket added, label sanitised");
    CHECK(zxu_bucket_add_ipns(c, "again", NAME_C_B32, &i2) == ZXU_ERR_EXISTS,
          "same IPNS key in another spelling is the same bucket");
    CHECK(zxu_bucket_trust_key(c, 1, k->pk) == 0 && c->b[1].nkeys == 1 &&
              zxu_bucket_trust_key(c, 1, k->pk) == 0 && c->b[1].nkeys == 1,
          "trusting a key twice is idempotent");
    CHECK(c->b[0].nkeys == 0, "trust is per bucket");
    for (uint32_t j = 0; j < 3; j++) {
        k->pk[0] = (uint8_t) (100 + j);
        zxu_bucket_trust_key(c, 1, k->pk);
    }
    k->pk[0] = 200;
    CHECK(zxu_bucket_trust_key(c, 1, k->pk) == ZXU_ERR_FULL, "at most 4 keys per bucket");
    k->pk[0] = 101;
    CHECK(zxu_bucket_untrust_key(c, 1, k->pk) == 0 && c->b[1].nkeys == 3, "untrust a key");
    CHECK(zxu_bucket_untrust_key(c, 1, k->pk) == ZXU_ERR_NOTFOUND, "untrust twice");
    c->b[1].have_seq = true;
    c->b[1].ipns_seq = 9;
    CHECK(zxu_bucket_set_cid(c, 1, KUBO_BUCKET_ROOT) == 0 && c->b[1].source == ZXU_SRC_CID &&
              !c->b[1].have_seq,
          "re-pointing a bucket resets its anti-rollback state");
    CHECK(zxu_bucket_set_ipns(c, 0, NAME_A) == 0 && c->b[0].source == ZXU_SRC_IPNS &&
              c->b[0].builtin,
          "the built-in bucket can follow an IPNS name");
    for (uint32_t j = 0; j < 10; j++) {
        char cs[80];
        uint8_t d[4] = {(uint8_t) j, 1, 2, 3};
        ipfsn_cid_t cc;
        ipfsn_cid_sha256(IPFSN_MC_DAG_PB, d, 4, &cc);
        ipfsn_cid_to_string(&cc, cs, sizeof cs);
        zxu_bucket_add_cid(c, "x", cs, &i2);
    }
    CHECK(zxu_bucket_count(c) == ZXU_MAX_BUCKETS, "bucket table fills to ZXU_MAX_BUCKETS");
    CHECK(zxu_bucket_add_ipns(c, "more", NAME_B, &i2) == ZXU_ERR_FULL, "then refuses more");
    CHECK(zxu_bucket_remove(c, 1) == 0 && !c->b[1].used &&
              zxu_bucket_remove(c, 1) == ZXU_ERR_NOTFOUND,
          "remove a user bucket");
    free(k);
    free(c);
}

static void test_disabled(void)
{
    zxu_config_t *c = cfg_new(false, ZXU_VERSION(1, 0, 0));
    rig_t *rig = rig_new(c);
    uint32_t idx, n, got = 0, verifies0 = g_verifies;
    ipfsn_cid_t list[4];
    zxu_bucket_add_ipns(c, "ipns", NAME_C, &idx);
    zxu_bucket_trust_key(c, idx, TEST_RELEASE_PK);
    gw_reset();
    n = zxu_check_all(&rig->u, NOW, true, g_res, 4);
    CHECK(n == 2 && g_res[0].status == ZXU_ST_DISABLED && g_res[1].status == ZXU_ST_DISABLED,
          "master switch off: every bucket DISABLED");
    CHECK(g_gw.requests == 0 && g_peer_wants == 0 && rig->u.requests == 0 &&
              g_verifies == verifies0 && rig->bs.count == 0,
          "master switch off: zero gateway, peer, verify and blockstore activity");
    list[0] = g_bucket;
    CHECK(zxu_fetch(&rig->u, list, 1, &got) == ZXU_ERR_DISABLED && g_gw.requests == 0,
          "downloads refused while off");
    c->enabled = true;
    zxu_bucket_enable(c, 0, false);
    zxu_bucket_enable(c, idx, false);
    n = zxu_check_all(&rig->u, NOW, true, g_res, 4);
    CHECK(n == 2 && g_res[0].status == ZXU_ST_DISABLED && g_res[1].status == ZXU_ST_DISABLED &&
              g_gw.requests == 0 && g_peer_wants == 0,
          "each bucket off: zero fetches");
    /* interval policy */
    zxu_bucket_enable(c, idx, true);
    gw_set_name(NAME_C, REC_C_SEQ2, sizeof REC_C_SEQ2);
    c->interval_s = 0;
    CHECK(zxu_check(&rig->u, idx, NOW, false, &g_res[0]) == ZXU_ST_NOT_DUE && g_gw.requests == 0,
          "interval 0: only checked when asked");
    c->interval_s = 60; /* clamped to 15 minutes */
    CHECK(zxu_check(&rig->u, idx, NOW, false, &g_res[0]) == ZXU_ST_UPDATE_AVAILABLE,
          "due: checked");
    CHECK(c->b[idx].next_due == NOW + ZXU_MIN_INTERVAL, "interval clamped to 15 minutes");
    gw_reset();
    CHECK(zxu_check(&rig->u, idx, NOW + 10, false, &g_res[0]) == ZXU_ST_NOT_DUE &&
              g_gw.requests == 0,
          "not due: zero fetches");
    CHECK(zxu_check(&rig->u, idx, NOW + ZXU_MIN_INTERVAL, false, &g_res[0]) ==
              ZXU_ST_UPDATE_AVAILABLE,
          "due again after the interval");
    rig_free(rig);
    free(c);
}

static void test_fixed_cid_e2e(void)
{
    zxu_config_t *c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    rig_t *rig = rig_new(c);
    upd_catalog_t *cat = (upd_catalog_t *) calloc(1, sizeof *cat);
    ipfsn_cid_t list[64];
    uint32_t idx, pub = 0;
    zxu_result_t *r = &g_res[0];
    zxu_bucket_enable(c, 0, false); /* the owner's real bucket is not in the test gateway */
    CHECK(zxu_bucket_add_cid(c, "Kubo test bucket", KUBO_BUCKET_ROOT, &idx) == 0, "add bucket");
    CHECK(zxu_bucket_trust_key(c, idx, TEST_RELEASE_PK) == 0, "trust the test release key");
    gw_reset();
    g_note.calls = 0;
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE,
          "fixed-CID bucket: update available");
    CHECK(zxu_installable(r) && r->have_manifest && r->n_applicable == 2 &&
              r->manifest.e[0].in_bucket && r->manifest.e[1].in_bucket,
          "both entries apply (x86_64 + any) and sit where the manifest says");
    CHECK(r->n_list == 5 && !r->list_truncated, "listing: 5 root entries");
    {
        bool readme = false;
        for (uint32_t i = 0; i < r->n_list; i++)
            if (!strcmp(r->list[i].name, "README.txt")) readme = true;
        CHECK(readme, "listing shows files the manifest does not list");
    }
    CHECK(g_gw.ipns_reqs == 0 && g_gw.requests > 0, "fixed CID: no IPNS request");
    CHECK(g_note.calls == 1 && g_note.kind == ZXU_NOTIFY_UPDATE && strstr(g_note.body, "1.2.0") &&
              strstr(g_note.body, "2 files"),
          "notification: update available");
    printf("  notify: \"%s\" / \"%s\"\n", g_note.title, g_note.body);
    CHECK(c->b[idx].have_issued && c->b[idx].issued == 1791504000ull, "issued recorded");
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE && g_note.calls == 1,
          "same root again: no second notification");
    /* downloads */
    for (uint32_t k = 0; k < r->n_applicable; k++) {
        uint32_t e = r->applicable[k];
        CHECK(zxu_download(&rig->u, r, e, list, 64, 4) == ZXU_OK, "download + verify an entry");
    }
    {
        uint32_t n;
        bool done;
        CHECK(zxu_plan(&rig->u, &r->manifest.e[1].cid, list, 64, &n, &done) == 0 && done && n == 0,
              "plan: complete after download");
    }
    /* into update.h: opt-in, self-certifying, trusted */
    upd_init(cat);
    zxu_catalog_reset();
    CHECK(zxu_to_catalog(r, c, cat, &pub) == ZXU_OK && pub == 2, "two catalog entries published");
    {
        int32_t plan[8];
        uint32_t pn = 99;
        CHECK(upd_resolve(cat, plan, 8, &pn) == UPD_OK && pn == 0,
              "nothing selected: the install plan is empty (opt-in)");
    }
    upd_set_verifier(cat, zxu_catalog_verify);
    {
        upd_transport_t t = zxu_catalog_transport(&rig->u);
        static uint8_t buf[8192];
        uint32_t got = 0;
        upd_set_transport(cat, &t);
        const char *id = cat->upd[1].id;
        CHECK(upd_select(cat, id), "user selects the kernel update");
        CHECK(upd_fetch_verify(cat, id, buf, sizeof buf, &got) == UPD_OK && got == cat->upd[1].size,
              "upd_fetch_verify accepts it (sha256 + trusted author + attestation)");
        CHECK((got == g_kernel_len && !memcmp(buf, g_kernel, got)) ||
                  (got == g_app_len && !memcmp(buf, g_app, got)),
              "and the bytes are the bucket's file");
    }
    {
        uint8_t fake[32], sig[64];
        memset(fake, 0x5a, 32);
        memcpy(sig, cat->upd[0].sig, 64);
        CHECK(!zxu_catalog_verify(fake, 32, sig, cat->upd[0].author),
              "attestation for content no manifest listed: refused");
        uint8_t md[32];
        CHECK(upd_manifest_digest(&cat->upd[0], md) &&
                  zxu_catalog_verify(md, 32, sig, cat->upd[0].author),
              "attestation covers the entry's complete-manifest digest");
        CHECK(!zxu_catalog_verify(cat->upd[0].cid, 32, sig, cat->upd[0].author),
              "attestation over the bare CID (old format): refused");
        {
            upd_entry_t e2 = cat->upd[0];
            uint8_t md2[32];
            e2.version += 1;
            CHECK(upd_manifest_digest(&e2, md2) &&
                      !zxu_catalog_verify(md2, 32, sig, cat->upd[0].author),
                  "catalog entry with a bumped version: attestation refused");
            e2 = cat->upd[0];
            e2.arch[0] = e2.arch[0] == 'x' ? 'y' : 'x';
            CHECK(upd_manifest_digest(&e2, md2) &&
                      !zxu_catalog_verify(md2, 32, sig, cat->upd[0].author),
                  "catalog entry with another arch: attestation refused");
        }
        sig[0] ^= 1;
        CHECK(!zxu_catalog_verify(md, 32, sig, cat->upd[0].author), "altered attestation: refused");
    }
    /* other system states */
    c->installed = ZXU_VERSION(1, 2, 0);
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UP_TO_DATE && !zxu_installable(r),
          "installed 1.2.0: up to date");
    c->installed = ZXU_VERSION(0, 9, 0);
    c->b[idx].have_notified = false;
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_INCOMPATIBLE && !zxu_installable(r) &&
              g_note.kind == ZXU_NOTIFY_INCOMPATIBLE,
          "installed 0.9.0 < min-version: incompatible, notified");
    c->installed = ZXU_VERSION(1, 0, 0);
    c->arch = ZXU_ARCH_AARCH64;
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE &&
              r->n_applicable == 1 && r->manifest.e[r->applicable[0]].arch == ZXU_ARCH_ANY,
          "aarch64: only the arch-independent app applies");
    c->installed = ZXU_VERSION(1, 1, 0);
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UP_TO_DATE,
          "aarch64 at 1.1.0: the app is not newer, nothing applies");
    free(cat);
    rig_free(rig);
    free(c);
}

static void test_ipns_e2e(void)
{
    zxu_config_t *c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    rig_t *rig = rig_new(c);
    zxu_result_t *r = &g_res[0];
    zxu_bucket_t snap;
    CHECK(zxu_bucket_set_ipns(c, 0, NAME_C) == 0, "built-in bucket follows IPNS name C");
    zxu_bucket_trust_key(c, 0, TEST_RELEASE_PK);
    gw_reset();
    /* seq 0 -> plain dirA: no manifest */
    gw_set_name(NAME_C, REC_C_SEQ0, sizeof REC_C_SEQ0);
    g_note.calls = 0;
    CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_UNSIGNED &&
              r->unsigned_reason == ZXU_UNS_NO_MANIFEST && !zxu_installable(r),
          "seq 0 -> a directory without a manifest: UNSIGNED");
    CHECK(r->n_list == 7 && !strcmp(r->list[0].name, "Z"), "listing for the UI: dirA's 7 entries");
    CHECK(g_note.calls == 1 && g_note.kind == ZXU_NOTIFY_UNSIGNED, "notified: unsigned");
    CHECK(!strcmp(g_gw.last_accept, "application/vnd.ipld.raw") && g_gw.ipns_reqs == 1,
          "one IPNS record request, then raw blocks");
    CHECK(c->b[0].have_seq && c->b[0].ipns_seq == 0, "seq 0 recorded");
    /* seq 1 -> HAMT dirB: no manifest, listing truncated */
    gw_set_name(NAME_C, REC_C_SEQ1_V2, sizeof REC_C_SEQ1_V2);
    CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_UNSIGNED && r->n_list == ZXU_LIST_MAX &&
              r->list_truncated,
          "seq 1 -> 150-entry HAMT: UNSIGNED, listing truncated at 64");
    /* seq 2 -> the signed bucket */
    gw_set_name(NAME_C, REC_C_SEQ2, sizeof REC_C_SEQ2);
    CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE && r->ipns_seq == 2 &&
              zxu_installable(r),
          "seq 2 -> signed bucket: update available over IPNS");
    CHECK(c->b[0].ipns_seq == 2, "seq 2 recorded");
    /* rollbacks */
    memcpy(&snap, &c->b[0], sizeof snap);
    gw_set_name(NAME_C, REC_C_SEQ1_V2, sizeof REC_C_SEQ1_V2);
    CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_IPNS_ROLLBACK && !zxu_installable(r),
          "seq 1 after seq 2: rollback refused");
    gw_set_name(NAME_C, REC_C_SEQ0, sizeof REC_C_SEQ0);
    CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_IPNS_ROLLBACK, "seq 0: rollback refused");
    CHECK(c->b[0].ipns_seq == 2 && cid_is(&c->b[0].ipns_value, &g_bucket),
          "the highest sequence is kept");
    /* a bad record of any kind */
    {
        static uint8_t t[sizeof REC_C_SEQ2];
        memcpy(t, REC_C_SEQ2, sizeof t);
        t[sizeof t - 20] ^= 1; /* inside the signed data */
        gw_set_name(NAME_C, t, sizeof t);
        CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_IPNS_INVALID, "tampered record");
        gw_set_name(NAME_C, REC_A_SEQ0, sizeof REC_A_SEQ0);
        CHECK(zxu_check(&rig->u, 0, NOW, true, r) == ZXU_ST_IPNS_INVALID,
              "a record signed by another key");
        gw_set_name(NAME_C, REC_C_SEQ2, sizeof REC_C_SEQ2);
        CHECK(zxu_check(&rig->u, 0, 4102444800ull, true, r) == ZXU_ST_IPNS_EXPIRED,
              "the record in 2100: expired");
        gw_reset();
        CHECK(zxu_check(&rig->u, 0, 0, true, r) == ZXU_ST_NO_CLOCK && g_gw.requests == 0,
              "no clock: IPNS refused, nothing fetched");
    }
    /* network failure: state unchanged */
    {
        zxu_config_t *before = (zxu_config_t *) malloc(sizeof *before);
        memcpy(before, c, sizeof *c);
        g_gw.down = true;
        CHECK(zxu_check(&rig->u, 0, NOW + 99999, true, r) == ZXU_ST_NETWORK,
              "gateway down: NETWORK");
        CHECK(!memcmp(before, c, sizeof *c), "gateway down: configuration and state unchanged");
        g_gw.down = false;
        free(before);
    }
    rig_free(rig);
    free(c);
}

static void test_unsigned_variants(key_t *other)
{
    zxu_config_t *c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    rig_t *rig = rig_new(c);
    zxu_result_t *r = &g_res[0];
    tb_store_t s;
    ipfsn_cid_t root, list[8];
    char cs[IPFSN_CID_STR_MAX];
    uint32_t idx;
    static uint8_t tm[ZXU_MANIFEST_MAX];
    static uint8_t sig[ZXU_SIG_BYTES];
    zxu_bucket_enable(c, 0, false);
    tb_init(&s);
    gw_reset();
#define USE_BUCKET(files, nf)                                                                      \
    do {                                                                                           \
        root = build_bucket(&s, files, nf);                                                        \
        gw_add_store(&s);                                                                          \
        ipfsn_cid_to_string(&root, cs, sizeof cs);                                                 \
        zxu_bucket_set_cid(c, idx, cs);                                                            \
    } while (0)
    zxu_bucket_add_cid(c, "variants", KUBO_BUCKET_ROOT, &idx);
    zxu_bucket_trust_key(c, idx, TEST_RELEASE_PK);
    {
        bfile_t f[] = {{"zxv-kernel-x86_64.bin", g_kernel, g_kernel_len, 0},
                       {ZXU_MANIFEST_NAME, g_manifest, g_manifest_len, 0}};
        USE_BUCKET(f, 2);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UNSIGNED &&
                  r->unsigned_reason == ZXU_UNS_NO_SIG,
              "manifest without signature: UNSIGNED");
    }
    {
        memcpy(tm, g_manifest, g_manifest_len);
        tm[40] ^= 1;
        bfile_t f[] = {{"zxv-kernel-x86_64.bin", g_kernel, g_kernel_len, 0},
                       {ZXU_MANIFEST_NAME, tm, g_manifest_len, 0},
                       {ZXU_SIG_NAME, g_sig, ZXU_SIG_BYTES, 0}};
        USE_BUCKET(f, 3);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UNSIGNED &&
                  r->unsigned_reason == ZXU_UNS_BAD_SIG && !zxu_installable(r),
              "tampered manifest: UNSIGNED (bad signature)");
        CHECK(zxu_download(&rig->u, r, 0, list, 8, 2) == ZXU_ERR_NOTSIGNED,
              "unsigned: download refused");
        {
            upd_catalog_t *cat = (upd_catalog_t *) calloc(1, sizeof *cat);
            uint32_t pub = 9;
            upd_init(cat);
            CHECK(zxu_to_catalog(r, c, cat, &pub) == ZXU_ERR_NOTSIGNED && pub == 0 && cat->n == 0,
                  "unsigned: nothing reaches the catalog");
            free(cat);
        }
    }
    {
        sign_buf(other, g_manifest, g_manifest_len, sig);
        bfile_t f[] = {{"apps/notes.zapp", g_app, g_app_len, 0},
                       {"zxv-kernel-x86_64.bin", g_kernel, g_kernel_len, 0},
                       {ZXU_MANIFEST_NAME, g_manifest, g_manifest_len, 0},
                       {ZXU_SIG_NAME, sig, ZXU_SIG_BYTES, 0}};
        USE_BUCKET(f, 4);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UNSIGNED &&
                  r->unsigned_reason == ZXU_UNS_BAD_SIG,
              "signed by an untrusted key: UNSIGNED");
        zxu_bucket_untrust_key(c, idx, TEST_RELEASE_PK);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UNSIGNED &&
                  r->unsigned_reason == ZXU_UNS_NO_KEYS,
              "bucket with no trusted key: UNSIGNED");
        zxu_bucket_trust_key(c, idx, other->pk);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE,
              "the user trusts that key for this bucket: available");
    }
    {
        static const char bad[] = "zxv-update-manifest 1\nrelease 2.0.0\n";
        sign_buf(other, (const uint8_t *) bad, sizeof bad - 1, sig);
        bfile_t f[] = {{ZXU_MANIFEST_NAME, (const uint8_t *) bad, sizeof bad - 1, 0},
                       {ZXU_SIG_NAME, sig, ZXU_SIG_BYTES, 0}};
        USE_BUCKET(f, 2);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_MANIFEST_INVALID &&
                  !zxu_installable(r),
              "signed but malformed manifest: refused");
    }
    {
        /* a newer manifest, then the old one again: rollback */
        bfile_t pay[] = {{"zxv-kernel-x86_64.bin", g_kernel, g_kernel_len, 0}};
        const char *kinds[] = {"kernel"}, *arches[] = {"x86_64"}, *vers[] = {"1.3.0"};
        char mt[2048];
        uint32_t ml = make_manifest(mt, sizeof mt, "1.3.0", "1.0.0", 1791600000ull, pay, kinds,
                                    arches, vers, 1);
        sign_buf(other, (const uint8_t *) mt, ml, sig);
        bfile_t f[] = {{"zxv-kernel-x86_64.bin", g_kernel, g_kernel_len, 0},
                       {ZXU_MANIFEST_NAME, (const uint8_t *) mt, ml, 0},
                       {ZXU_SIG_NAME, sig, ZXU_SIG_BYTES, 0}};
        USE_BUCKET(f, 3);
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE &&
                  r->manifest.release == ZXU_VERSION(1, 3, 0),
              "newer manifest (issued 1791600000) accepted");
        /* the old bucket content signed by the same key, without re-pointing the bucket */
        sign_buf(other, g_manifest, g_manifest_len, sig);
        bfile_t g[] = {{"zxv-kernel-x86_64.bin", g_kernel, g_kernel_len, 0},
                       {ZXU_MANIFEST_NAME, g_manifest, g_manifest_len, 0},
                       {ZXU_SIG_NAME, sig, ZXU_SIG_BYTES, 0}};
        root = build_bucket(&s, g, 3);
        gw_add_store(&s);
        c->b[idx].cid = root; /* same bucket, older content (as an IPNS rollback would be) */
        CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_MANIFEST_ROLLBACK &&
                  !zxu_installable(r),
              "older issued than already seen: rollback refused");
        CHECK(c->b[idx].issued == 1791600000ull, "the newest issued is kept");
    }
#undef USE_BUCKET
    tb_free(&s);
    rig_free(rig);
    free(c);
}

static void test_resumable(key_t *k)
{
    zxu_config_t *c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    rig_t *rig = rig_new(c);
    zxu_result_t *r = &g_res[0];
    tb_store_t s;
    static uint8_t big[60000];
    static uint8_t sig[ZXU_SIG_BYTES];
    char mt[2048], cs[IPFSN_CID_STR_MAX];
    ipfsn_cid_t root, *list = (ipfsn_cid_t *) malloc(16 * sizeof(ipfsn_cid_t));
    uint32_t idx, n, ml;
    bool done;
    for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t) (i * 13 + (i >> 8));
    {
        bfile_t pay[] = {{"zxv-kernel-x86_64.bin", big, sizeof big, 1024}}; /* 59 leaves */
        const char *kinds[] = {"kernel"}, *arches[] = {"x86_64"}, *vers[] = {"1.1.0"};
        ml = make_manifest(mt, sizeof mt, "1.1.0", "1.0.0", 1, pay, kinds, arches, vers, 1);
        sign_buf(k, (const uint8_t *) mt, ml, sig);
        bfile_t f[] = {{"zxv-kernel-x86_64.bin", big, sizeof big, 1024},
                       {ZXU_MANIFEST_NAME, (const uint8_t *) mt, ml, 0},
                       {ZXU_SIG_NAME, sig, ZXU_SIG_BYTES, 0}};
        tb_init(&s);
        root = build_bucket(&s, f, 3);
        gw_add_store(&s);
    }
    ipfsn_cid_to_string(&root, cs, sizeof cs);
    zxu_bucket_enable(c, 0, false);
    zxu_bucket_add_cid(c, "big", cs, &idx);
    zxu_bucket_trust_key(c, idx, k->pk);
    gw_reset();
    CHECK(zxu_check(&rig->u, idx, NOW, true, r) == ZXU_ST_UPDATE_AVAILABLE, "multi-block update");
    CHECK(zxu_plan(&rig->u, &r->manifest.e[0].cid, list, 16, &n, &done) == 0 && !done && n == 1,
          "plan: only the root is known to be missing");
    g_gw.budget = 10; /* the network drops after 10 requests */
    CHECK(zxu_download(&rig->u, r, 0, list, 16, 50) == ZXU_ERR_NETWORK, "interrupted download");
    {
        uint32_t n2;
        zxu_plan(&rig->u, &r->manifest.e[0].cid, list, 16, &n2, &done);
        CHECK(!done && n2 > 0, "the plan lists what is still missing");
    }
    {
        uint32_t before = g_gw.requests;
        g_gw.budget = -1;
        CHECK(zxu_download(&rig->u, r, 0, list, 16, 50) == ZXU_OK, "resumed download completes");
        CHECK(g_gw.requests - before <= 60 - 9, "resumption does not refetch what arrived");
    }
    /* corrupt a stored block's bytes on the medium: the final check catches it */
    {
        uint32_t found = 0;
        for (uint64_t off = 0; off + 1024 <= rig->ms.len && !found; off++)
            if (!memcmp(rig->ms.buf + off, big + 1024 * 7, 64)) {
                rig->ms.buf[off + 10] ^= 1;
                found = 1;
            }
        CHECK(found && zxu_download(&rig->u, r, 0, list, 16, 0) != ZXU_OK,
              "a corrupted stored block fails the final verification");
    }
    tb_free(&s);
    free(list);
    rig_free(rig);
    free(c);
}

static void test_independence(void)
{
    zxu_config_t *c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    rig_t *rig = rig_new(c);
    uint32_t a, b, d, e, n;
    zxu_bucket_t sb, se;
    static zxu_result_t alone;
    zxu_bucket_enable(c, 0, false);
    zxu_bucket_add_cid(c, "good", KUBO_BUCKET_ROOT, &a);
    zxu_bucket_trust_key(c, a, TEST_RELEASE_PK);
    zxu_bucket_add_cid(c, "unreachable",
                       "bafybeigdyrzt5sfp7udm7hu76uh7y26nf3efuylqabf3oclgtqy55fbzdi", &b);
    zxu_bucket_add_cid(c, "plain dirA", KUBO_DIR_A_ROOT_S, &d);
    zxu_bucket_add_ipns(c, "bad ipns", NAME_A, &e);
    gw_set_name(NAME_A, REC_C_SEQ2, sizeof REC_C_SEQ2); /* someone else's record */
    gw_reset();
    memcpy(&sb, &c->b[b], sizeof sb);
    memcpy(&se, &c->b[e], sizeof se);
    n = zxu_check_all(&rig->u, NOW, true, g_res, 8);
    CHECK(n == 5, "five buckets reported");
    CHECK(g_res[0].status == ZXU_ST_DISABLED, "built-in (off): disabled");
    CHECK(g_res[a].status == ZXU_ST_UPDATE_AVAILABLE, "good bucket: update available");
    CHECK(g_res[b].status == ZXU_ST_NETWORK, "unreachable bucket: network");
    CHECK(g_res[d].status == ZXU_ST_UNSIGNED, "plain dir: unsigned");
    CHECK(g_res[e].status == ZXU_ST_IPNS_INVALID, "wrong-key IPNS: invalid");
    CHECK(!memcmp(&sb, &c->b[b], sizeof sb), "the failed bucket's state is unchanged");
    CHECK(c->b[e].have_seq == false, "the bad IPNS bucket learnt no sequence");
    zxu_check(&rig->u, a, NOW, true, &alone);
    CHECK(alone.status == g_res[a].status && alone.n_applicable == g_res[a].n_applicable &&
              cid_is(&alone.root, &g_res[a].root),
          "the good bucket's result does not depend on the others");
    /* a lying gateway: every block tampered */
    {
        rig_t *r2 = rig_new(c);
        zxu_config_t *before = (zxu_config_t *) malloc(sizeof *before);
        g_gw.tamper = true;
        c->b[a].have_issued = false;
        memcpy(before, c, sizeof *c);
        CHECK(zxu_check(&r2->u, a, NOW, true, &alone) == ZXU_ST_NETWORK &&
                  alone.detail == IPFSN_ERR_HASH,
              "tampering gateway: caught by the CID check, reported as network");
        CHECK(!memcmp(before, c, sizeof *c), "tampering gateway: state unchanged");
        g_gw.tamper = false;
        free(before);
        rig_free(r2);
    }
    rig_free(rig);
    free(c);
}

static void test_live(void)
{
    zxu_config_t *c;
    rig_t *rig;
    if (!live_base) return;
    c = cfg_new(true, ZXU_VERSION(1, 0, 0));
    rig = rig_new(c);
    zxu_bucket_set_ipns(c, 0, NAME_C);
    zxu_bucket_trust_key(c, 0, TEST_RELEASE_PK);
    printf("  live gateway %s\n", live_base);
    CHECK(zxu_check(&rig->u, 0, NOW, true, &g_res[0]) == ZXU_ST_UPDATE_AVAILABLE,
          "LIVE: IPNS name C through a real Kubo gateway: update available");
    printf("  live: %s, seq %llu\n", zxu_status_str(g_res[0].status),
           (unsigned long long) g_res[0].ipns_seq);
    rig_free(rig);
    free(c);
}

int main(void)
{
    key_t *other = (key_t *) malloc(sizeof *other);
    const char *lb = getenv("ZXU_LIVE_GATEWAY");
    g_res = (zxu_result_t *) calloc(8, sizeof(zxu_result_t));
    tb_init(&g_gw.blocks);
    load_fixtures();
    for (uint32_t i = 0; i < g_kubo.n; i++)
        tb_put(&g_gw.blocks, &g_kubo.b[i].cid, g_kubo.b[i].data, g_kubo.b[i].len);
    key_new(other);
    test_versions();
    test_manifest_parse();
    test_manifest_sig(other);
    test_ipns_names();
    test_rfc3339();
    test_ipns_records();
    test_fuzz();
    test_bucket_api();
    test_disabled();
    test_fixed_cid_e2e();
    test_ipns_e2e();
    test_unsigned_variants(other);
    test_resumable(other);
    test_independence();
    if (lb && *lb) {
        live_base = lb;
        test_live();
    }
    printf("test_zx_upcheck: %d passed, %d failed\n", passes, failures);
    memset(other, 0, sizeof *other);
    free(other);
    free(g_res);
    tb_free(&g_gw.blocks);
    tb_free(&g_kubo);
    return failures ? 1 : 0;
}
