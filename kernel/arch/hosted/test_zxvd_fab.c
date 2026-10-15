/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_fab.c — zxvd_fab.c (fab.*) behind the real RPC layer on the real
 * fabrication engine (kernel/src/fab), in a temporary folder with a
 * deterministic random source: keys kept across opens, blocks, public and
 * sealed files, the trust list, the air-gap seal in all four states, damaged
 * blocks and recipes, and both ends of a stream with replay, RESYNC,
 * checkpoints and frames that grow. Fixed replies are compared exactly. */
#define _XOPEN_SOURCE    700 /* nftw, mkdtemp */
#define _DARWIN_C_SOURCE     /* mkdtemp and friends on macOS */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zxvd_groups.h"
#include "zxvd_fab.h"
#include "fab.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[16000], WANT[4000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.900s\n    got  %.900s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *call(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *call(const char *method, const char *fmt, ...)
{
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_list ap;
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *err(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *err(int code, const char *fmt, ...)
{
    char msg[1500];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}

/* the string value of "key" in the last reply ("" when absent) */
static const char *str(const char *key)
{
    static char v[12000];
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = OUT.p ? strstr(OUT.p, pat) : NULL;
    v[0] = 0;
    if (!p) return v;
    p += strlen(pat);
    const char *e = strchr(p, '"');
    size_t n = e ? (size_t) (e - p) : 0;
    if (n >= sizeof v) n = sizeof v - 1;
    memcpy(v, p, n);
    v[n] = 0;
    return v;
}

/* the integer value of "key" in the last reply (-1 when absent) */
static long num(const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = OUT.p ? strstr(OUT.p, pat) : NULL;
    return p ? strtol(p + strlen(pat), NULL, 10) : -1;
}

static bool is_ok(void)
{
    return OUT.p && strstr(OUT.p, "\"result\":") != NULL;
}

/* ----- the folder, files and the random source ----- */

static char R[256], P[64][512];
static int NP;

static const char *path(const char *name)
{
    char *p = P[NP++ % 64];
    snprintf(p, 512, "%s/%s", R, name);
    return p;
}

static void wfile(const char *name, const uint8_t *b, uint32_t n)
{
    FILE *f = fopen(path(name), "wb");
    if (!f || fwrite(b, 1, n, f) != n) {
        printf("cannot write %s\n", name);
        exit(2);
    }
    fclose(f);
}

static uint8_t *rfile(const char *name, uint32_t *n)
{
    FILE *f = fopen(path(name), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long k = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(k ? (size_t) k : 1u);
    *n = (uint32_t) fread(b, 1, (size_t) k, f);
    fclose(f);
    return b;
}

static bool same(const char *name, const uint8_t *want, uint32_t n)
{
    uint32_t k = 0;
    uint8_t *b = rfile(name, &k);
    bool eq = b && k == n && memcmp(b, want, n) == 0;
    free(b);
    return eq;
}

static bool has(const uint8_t *b, uint32_t n, const char *s)
{
    size_t k = strlen(s);
    for (uint32_t i = 0; i + k <= n; i++)
        if (!memcmp(b + i, s, k)) return true;
    return false;
}

static bool exists(const char *name)
{
    struct stat st;
    return stat(path(name), &st) == 0;
}

static int mode_of(const char *name)
{
    struct stat st;
    return stat(path(name), &st) == 0 ? (int) (st.st_mode & 0777) : -1;
}

static uint64_t RNG = 0x2545F4914F6CDD1Dull;
static uint8_t rb(void)
{
    RNG ^= RNG << 13;
    RNG ^= RNG >> 7;
    RNG ^= RNG << 17;
    return (uint8_t) (RNG >> 24);
}
static void fill(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = rb();
}
static int RANDOM_FAIL;
static int test_random(uint8_t *p, size_t n)
{
    if (RANDOM_FAIL) return -1;
    fill(p, (uint32_t) n);
    return 0;
}

static int rm_one(const char *p, const struct stat *s, int f, struct FTW *w)
{
    (void) s;
    (void) f;
    (void) w;
    return remove(p);
}

/* ----- the seal ----- */

static lpres_state_t SEAL;
static int SEAL_CALLS;
static lpres_state_t seal(void *ctx)
{
    CHECK(ctx == &SEAL, "B4: the context comes back");
    SEAL_CALLS++;
    return SEAL;
}

/* ----- fixture: three blocks and a target that mixes two of them ----- */

#define B1 300000u
#define B2 100000u
#define BD 50000u
static uint8_t b1[B1], b2[B2], bd[BD], tgt[B1 + B2], other[64];
static uint32_t tlen;
static char c1[IPFSN_CID_STR_MAX], c2[IPFSN_CID_STR_MAX], cd[IPFSN_CID_STR_MAX];
static char tcid[IPFSN_CID_STR_MAX], sid[80], kid[80];
static char ek_hex[2 * FAB_KEM_EK + 1], xek_hex[2 * FAB_KEM_EK + 1], xid_hex[80];
static fab_work_t TW;

static void hex(const uint8_t *b, uint32_t n, char *out)
{
    for (uint32_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

static void cid_str(const uint8_t *b, uint32_t n, char *out)
{
    ipfsn_cid_t c;
    ipfsn_cid_sha256(IPFSN_MC_RAW, b, n, &c);
    ipfsn_cid_to_string(&c, out, IPFSN_CID_STR_MAX);
}

static void fixture(void)
{
    fill(b1, B1);
    fill(b2, B2);
    fill(bd, BD);
    fill(other, sizeof other);
    tlen = 0;
    memcpy(tgt, b1, 200000);
    tlen += 200000;
    memcpy(tgt + tlen, "private line one\n", 17);
    tlen += 17;
    memcpy(tgt + tlen, b2 + 40000, 60000);
    tlen += 60000;
    memcpy(tgt + tlen, "private line two\n", 17);
    tlen += 17;
    wfile("b1", b1, B1);
    wfile("b2", b2, B2);
    wfile("bd", bd, BD);
    wfile("target", tgt, tlen);
    cid_str(b1, B1, c1);
    cid_str(b2, B2, c2);
    cid_str(bd, BD, cd);
    ipfsn_cid_t t;
    fab_target_cid(tgt, tlen, &TW, &t);
    ipfsn_cid_to_string(&t, tcid, sizeof tcid);
    /* someone else's KEM key, and someone else's signing id */
    static uint8_t xek[FAB_KEM_EK], xdk[FAB_KEM_DK], xpk[FAB_SIG_PK], xsk[FAB_SIG_SK];
    uint8_t d[32], z[32], s[32], id[FAB_ID];
    memset(d, 7, 32);
    memset(z, 8, 32);
    memset(s, 9, 32);
    fab_kem_keypair(d, z, xek, xdk);
    hex(xek, FAB_KEM_EK, xek_hex);
    fab_sig_keypair(s, xpk, xsk);
    fab_key_id(xpk, FAB_SIG_PK, id);
    hex(id, FAB_ID, xid_hex);
}

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_fab_group()) == 0, "add the fab group");
}

/* ----- closed, opened, keys ----- */

static void test_open(void)
{
    fresh();
    EXPECT(call("features.get", "{}"),
           ok("{\"features\":[{\"name\":\"fab\",\"enabled\":false,\"regulated\":false,"
              "\"groups\":[{\"note\":\"file fabrication: send, receive and stream files built "
              "from public blocks plus a sealed diff\",\"methods\":15}]}],\"saved\":false}"));
    EXPECT(call("fab.status", "{}"), ok("{\"open\":false,\"sign_id\":null,\"kem_id\":null,\"seal\":"
                                        "false,\"streams\":[],\"receives\":[]}"));
    EXPECT(call("fab.keys", "{}"), err(ZXVD_E_FAB, "FILE: fabrication is not open in this zxvd"));
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("b1")),
           err(ZXVD_E_OFF, "fab is off: start zxvd with --enable fab, or call features.set"));
    CHECK(zxvd_rpc_enable("fab") == 0, "fab on");
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("b1")),
           err(ZXVD_E_FAB, "FILE: fabrication is not open in this zxvd"));
    EXPECT(call("fab.stream.close", "{\"stream\":1}"),
           err(ZXVD_E_FAB, "FILE: fabrication is not open in this zxvd"));

    CHECK(zxvd_fab_open(NULL) == -1, "B2: no folder");
    CHECK(zxvd_fab_open("") == -1, "B2: an empty folder name");
    char longp[ZXVD_FAB_PATH];
    memset(longp, 'a', sizeof longp - 1);
    longp[0] = '/';
    longp[sizeof longp - 1] = 0;
    CHECK(zxvd_fab_open(longp) == -1, "B2: a folder name too long for its files");
    CHECK(zxvd_fab_open(path("missing/fab")) == -1, "B2: a parent that does not exist");
    RANDOM_FAIL = 1;
    CHECK(zxvd_fab_open(path("fab")) == -1, "B2: no random source, no keys");
    CHECK(!exists("fab/sign.seed"), "B2: no seed written without randomness");
    RANDOM_FAIL = 0;
    CHECK(zxvd_fab_open(path("fab")) == 0, "B2: open makes the folder and keys");
    CHECK(mode_of("fab") == 0700, "B2: the folder is owner-only (%o)", (unsigned) mode_of("fab"));
    CHECK(mode_of("fab/sign.seed") == 0600 && mode_of("fab/kem.seed") == 0600,
          "B2: seeds owner-only");
    uint32_t n1 = 0, n2 = 0;
    free(rfile("fab/sign.seed", &n1));
    free(rfile("fab/kem.seed", &n2));
    CHECK(n1 == 32 && n2 == 64, "B2: seed sizes %u %u", (unsigned) n1, (unsigned) n2);
    CHECK(exists("fab/blocks"), "B2: the block store");

    rpc(call("fab.keys", "{}"));
    snprintf(sid, sizeof sid, "%s", str("sign_id"));
    snprintf(kid, sizeof kid, "%s", str("kem_id"));
    snprintf(ek_hex, sizeof ek_hex, "%s", str("kem_ek"));
    char spk[2 * FAB_SIG_PK + 1];
    snprintf(spk, sizeof spk, "%s", str("sign_pk"));
    CHECK(strlen(sid) == 64 && strlen(kid) == 64, "B5: ids are 32 bytes");
    CHECK(strlen(ek_hex) == 2 * FAB_KEM_EK && strlen(spk) == 2 * FAB_SIG_PK, "B5: key sizes");
    /* the ids are SHA-256 of the keys */
    uint8_t ek[FAB_KEM_EK], pk[FAB_SIG_PK], id[FAB_ID];
    char idh[80];
    for (uint32_t i = 0; i < FAB_KEM_EK; i++) sscanf(ek_hex + 2 * i, "%2hhx", &ek[i]);
    for (uint32_t i = 0; i < FAB_SIG_PK; i++) sscanf(spk + 2 * i, "%2hhx", &pk[i]);
    fab_key_id(ek, FAB_KEM_EK, id);
    hex(id, FAB_ID, idh);
    CHECK(!strcmp(idh, kid), "B5: kem_id is SHA-256 of kem_ek");
    fab_key_id(pk, FAB_SIG_PK, id);
    hex(id, FAB_ID, idh);
    CHECK(!strcmp(idh, sid), "B5: sign_id is SHA-256 of sign_pk");
    CHECK(!strstr(OUT.p, "seed"), "B2: no seed in the reply");
    EXPECT(call("fab.status", "{}"), ok("{\"open\":true,\"sign_id\":\"%s\",\"kem_id\":\"%s\","
                                        "\"seal\":false,\"streams\":[],\"receives\":[]}",
                                        sid, kid));

    /* B2: a second open reads the same seeds */
    zxvd_fab_close();
    EXPECT(call("fab.status", "{}"), ok("{\"open\":false,\"sign_id\":null,\"kem_id\":null,\"seal\":"
                                        "false,\"streams\":[],\"receives\":[]}"));
    CHECK(zxvd_fab_open(path("fab")) == 0, "reopen");
    rpc(call("fab.keys", "{}"));
    CHECK(!strcmp(str("sign_id"), sid) && !strcmp(str("kem_id"), kid), "B2: the same keys");
    /* a damaged seed is never replaced */
    uint8_t keep[32];
    uint32_t kn = 0;
    uint8_t *k = rfile("fab/sign.seed", &kn);
    memcpy(keep, k, 32);
    free(k);
    wfile("fab/sign.seed", keep, 31);
    CHECK(zxvd_fab_open(path("fab")) == -1, "B2: a short seed refuses to open");
    CHECK(same("fab/sign.seed", keep, 31), "B2: and stays as it was");
    EXPECT(call("fab.status", "{}"), ok("{\"open\":false,\"sign_id\":null,\"kem_id\":null,\"seal\":"
                                        "false,\"streams\":[],\"receives\":[]}"));
    wfile("fab/sign.seed", keep, 32);
    uint8_t kk[64];
    k = rfile("fab/kem.seed", &kn);
    memcpy(kk, k, 64);
    free(k);
    wfile("fab/kem.seed", kk, 65);
    CHECK(zxvd_fab_open(path("fab")) == -1, "B2: a long KEM seed refuses to open");
    wfile("fab/kem.seed", kk, 64);
    CHECK(zxvd_fab_open(path("fab")) == 0, "repaired by hand, it opens");
    rpc(call("fab.keys", "{}"));
    CHECK(!strcmp(str("sign_id"), sid) && !strcmp(str("kem_id"), kid), "B2: the same keys again");
}

/* ----- blocks ----- */

static void test_put(void)
{
    rpc(call("fab.put", "{\"path\":\"%s\"}", path("b1")));
    CHECK(!strcmp(str("cid"), c1) && num("len") == B1, "B6: put b1 %s", OUT.p);
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("b2")),
           ok("{\"cid\":\"%s\",\"len\":%u}", c2, B2));
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("bd")),
           ok("{\"cid\":\"%s\",\"len\":%u}", cd, BD));
    char sp[300];
    snprintf(sp, sizeof sp, "fab/blocks/%s", c2);
    CHECK(same(sp, b2, B2), "B2: the store holds the block under its CID");
    EXPECT(call("fab.put", "{\"path\":\"b1\"}"),
           err(ZXVD_E_PARAMS, "\\\"path\\\" must be an absolute path"));
    EXPECT(call("fab.put", "{}"), err(ZXVD_E_PARAMS, "missing string parameter \\\"path\\\""));
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("nope")),
           err(ZXVD_E_FAB, "FILE: cannot read %s", path("nope")));
    wfile("empty", other, 0);
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("empty")),
           err(ZXVD_E_PARAMS, "a block cannot be empty"));
    static uint8_t big[FAB_BLOCK_MAX + 1];
    wfile("big", big, sizeof big);
    EXPECT(call("fab.put", "{\"path\":\"%s\"}", path("big")),
           err(ZXVD_E_FAB, "FILE: too large: %s", path("big")));
    wfile("max", big, FAB_BLOCK_MAX);
    rpc(call("fab.put", "{\"path\":\"%s\"}", path("max")));
    CHECK(num("len") == (long) FAB_BLOCK_MAX, "B6: a block of exactly FAB_BLOCK_MAX");
}

/* ----- files ----- */

static const char *blocks3(void)
{
    static char b[600];
    snprintf(b, sizeof b, "[{\"cid\":\"%s\"},{\"cid\":\"%s\"},{\"cid\":\"%s\"}]", c1, c2, cd);
    return b;
}

static void test_files(void)
{
    /* B7: public */
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":%s}", path("target"),
             path("pub.fab"), blocks3()));
    CHECK(is_ok(), "send public: %.300s", OUT.p);
    CHECK(!strcmp(str("cid"), tcid) && num("len") == (long) tlen, "B7: target cid and len");
    CHECK(!strcmp(str("mode"), "public"), "B7: public without to");
    CHECK(num("used") == 2 && num("decoys") == 1, "B7: 2 used + 1 decoy");
    CHECK(num("diff") >= 30 && num("diff") <= 34, "B7: only the private bytes travel (%ld)",
          num("diff"));
    long rl = num("recipe");
    uint32_t n = 0;
    uint8_t *rec = rfile("pub.fab", &n);
    CHECK(rec && (long) n == rl && n < 8000, "B7: recipe written, %u bytes", (unsigned) n);
    CHECK(rec && !memcmp(rec, "FABR", 4) && rec[6] == FAB_MODE_PUBLIC, "B7: a public FABR recipe");
    free(rec);
    CHECK(mode_of("pub.fab") == 0600, "B3: owner-only output");

    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"), path("o1")));
    char want[600];
    snprintf(want, sizeof want,
             "{\"cid\":\"%s\",\"len\":%u,\"mode\":\"public\",\"sender\":\"%s\","
             "\"blocks\":3}",
             tcid, (unsigned) tlen, sid);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"), path("o1")),
           ok("%s", want));
    CHECK(same("o1", tgt, tlen), "B8: the file comes back");
    CHECK(mode_of("o1") == 0600, "B3: owner-only");
    CHECK(!exists("o1.tmp"), "B3: no temporary left");

    /* B7: sealed to this zxvd */
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":%s,\"to\":\"%s\"}",
             path("target"), path("sec.fab"), blocks3(), ek_hex));
    CHECK(!strcmp(str("mode"), "sealed"), "B7: sealed with to: %.200s", OUT.p);
    rec = rfile("sec.fab", &n);
    CHECK(rec && rec[6] == FAB_MODE_SEALED && !has(rec, n, "private line"),
          "B7: no private bytes in clear");
    free(rec);
    snprintf(want, sizeof want,
             "{\"cid\":\"%s\",\"len\":%u,\"mode\":\"sealed\",\"sender\":\"%s\","
             "\"blocks\":3}",
             tcid, (unsigned) tlen, sid);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("sec.fab"), path("o2")),
           ok("%s", want));
    CHECK(same("o2", tgt, tlen), "B8: the sealed file comes back");

    /* sealed to someone else: refused, nothing written */
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":%s,\"to\":\"%s\"}",
             path("target"), path("x.fab"), blocks3(), xek_hex));
    CHECK(is_ok(), "send to another key");
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("x.fab"), path("o3")),
           err(ZXVD_E_FAB, "KEY: it is not sealed to this zxvd's key"));
    CHECK(!exists("o3") && !exists("o3.tmp"), "B3: no output on failure");

    /* no blocks: everything is diff */
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\"}", path("target"), path("lit.fab")));
    CHECK(num("used") == 0 && num("decoys") == 0 && num("diff") == (long) tlen && num("ops") == 1,
          "B7: no blocks: one literal op: %.200s", OUT.p);
    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("lit.fab"), path("o4")));
    CHECK(same("o4", tgt, tlen), "B8: a recipe with no blocks");
    /* an empty file */
    wfile("e", other, 0);
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\"}", path("e"), path("e.fab")));
    CHECK(is_ok() && num("len") == 0, "B7: an empty target");
    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("e.fab"), path("o5")));
    CHECK(is_ok() && same("o5", other, 0), "B8: an empty file: %.200s", OUT.p);
    /* window: a larger window still finds the runs */
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":%s,\"window\":4096}",
             path("target"), path("w.fab"), blocks3()));
    CHECK(is_ok() && num("used") == 2, "B7: window 4096");
}

static void test_send_params(void)
{
    const char *t = path("target"), *o = path("p.fab");
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"window\":7}", t, o),
           err(ZXVD_E_PARAMS, "\\\"window\\\" must be at least 8"));
    rpc(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"window\":8}", t, path("w8.fab")));
    CHECK(is_ok(), "B7: window 8 is the least: %.200s", OUT.p);
    /* the most ops a target can need: one byte, then 8 from the block, again and again */
    static uint8_t dense[90000];
    for (uint32_t i = 0; i < 10000; i++) {
        dense[9 * i] = rb();
        memcpy(dense + 9 * i + 1, b1 + ((i * 37u) % (B1 / 8u - 2u)) * 8u, 8); /* indexed windows */
    }
    wfile("dense", dense, sizeof dense);
    rpc(call("fab.send",
             "{\"target\":\"%s\",\"out\":\"%s\",\"window\":8,\"blocks\":[{\"cid\":\"%s\"}]}",
             path("dense"), path("dense.fab"), c1));
    CHECK(is_ok() && num("ops") > 15000, "B7: a target of many short matches: %.200s", OUT.p);
    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("dense.fab"),
             path("dense.out")));
    CHECK(is_ok() && same("dense.out", dense, sizeof dense), "B8: and back: %.200s", OUT.p);
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"rel\"}", t),
           err(ZXVD_E_PARAMS, "\\\"out\\\" must be an absolute path"));
    EXPECT(call("fab.send", "{\"out\":\"%s\"}", o),
           err(ZXVD_E_PARAMS, "missing string parameter \\\"target\\\""));
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"to\":\"abcd\"}", t, o),
           err(ZXVD_E_PARAMS, "\\\"to\\\" must be an ML-KEM-1024 key of 1568 bytes"));
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":[{\"cid\":\"nope\"}]}", t,
                o),
           err(ZXVD_E_PARAMS, "\\\"blocks\\\"[0].cid is not a CID"));
    EXPECT(call("fab.send",
                "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":[{\"cid\":\"%s\"},{\"cid\":\"%s\"}]}",
                t, o, c1, c1),
           err(ZXVD_E_PARAMS, "\\\"blocks\\\"[1] repeats a block"));
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":[7]}", t, o),
           err(ZXVD_E_PARAMS, "\\\"blocks\\\"[0] must be an object"));
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":[{}]}", t, o),
           err(ZXVD_E_PARAMS, "missing string parameter \\\"cid\\\""));
    /* a block named but not in the store */
    char ox[IPFSN_CID_STR_MAX];
    cid_str(other, sizeof other, ox);
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":[{\"cid\":\"%s\"}]}", t,
                o, ox),
           err(ZXVD_E_FAB, "FETCH: a block is not in the store"));
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\"}", path("nope"), o),
           err(ZXVD_E_FAB, "FILE: cannot read %s", path("nope")));
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\"}", t, path("no/such/dir")),
           err(ZXVD_E_FAB, "FILE: cannot write %s", path("no/such/dir")));
    CHECK(!exists("p.fab"), "nothing written by a refused send");
    RANDOM_FAIL = 1;
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\"}", t, o),
           err(ZXVD_E_INTERNAL, "no random source"));
    RANDOM_FAIL = 0;
    /* a block in the store whose bytes no longer match its name */
    char sp[300];
    snprintf(sp, sizeof sp, "fab/blocks/%s", c2);
    b2[5] ^= 1;
    wfile(sp, b2, B2);
    EXPECT(call("fab.send", "{\"target\":\"%s\",\"out\":\"%s\",\"blocks\":%s}", t, o, blocks3()),
           err(ZXVD_E_FAB, "BLOCK: a block does not match its CID"));
    b2[5] ^= 1;
    wfile(sp, b2, B2);
}

static void test_receive_refusals(void)
{
    const char *o = path("r1");
    /* B8: trust */
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[{\"id\":\"%s\"}]}",
                path("pub.fab"), o, xid_hex),
           err(ZXVD_E_FAB, "TRUST: the sender is not in the trust list"));
    EXPECT(
        call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[]}", path("pub.fab"), o),
        err(ZXVD_E_FAB, "TRUST: the sender is not in the trust list"));
    CHECK(!exists("r1"), "nothing written");
    rpc(call("fab.receive",
             "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[{\"id\":\"%s\"},{\"id\":\"%s\"}]}",
             path("pub.fab"), o, xid_hex, sid));
    CHECK(is_ok() && same("r1", tgt, tlen), "B8: a trusted sender: %.200s", OUT.p);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[{\"id\":\"abcd\"}]}",
                path("pub.fab"), o),
           err(ZXVD_E_PARAMS, "\\\"trust\\\"[0].id must be 32 bytes of hex"));
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[{}]}", path("pub.fab"),
                o),
           err(ZXVD_E_PARAMS, "missing hex parameter \\\"id\\\""));
    EXPECT(
        call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":7}", path("pub.fab"), o),
        err(ZXVD_E_PARAMS, "\\\"trust\\\" must be an array"));
    char many[70 * 80 + 10];
    int k = 0;
    for (int i = 0; i < 65; i++)
        k += snprintf(many + k, sizeof many - (size_t) k, "%s{\"id\":\"%s\"}", i ? "," : "", sid);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[%s]}", path("pub.fab"),
                o, many),
           err(ZXVD_E_PARAMS, "at most 64 trusted senders"));
    k = 0;
    for (int i = 0; i < 64; i++)
        k += snprintf(many + k, sizeof many - (size_t) k, "%s{\"id\":\"%s\"}", i ? "," : "",
                      i == 63 ? sid : xid_hex);
    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[%s]}", path("pub.fab"), o,
             many));
    CHECK(is_ok() && same("r1", tgt, tlen), "B8: 64 trusted, the last one the sender: %.200s",
          OUT.p);

    /* damaged recipes */
    uint32_t n = 0;
    uint8_t *rec = rfile("sec.fab", &n);
    rec[3000] ^= 0x20;
    wfile("bad.fab", rec, n);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("bad.fab"), path("r2")),
           err(ZXVD_E_FAB, "SIGN: the signature does not verify"));
    wfile("short.fab", rec, 100);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("short.fab"), path("r2")),
           err(ZXVD_E_FAB, "FORM: not a well-formed recipe, frame or checkpoint"));
    free(rec);
    CHECK(!exists("r2"), "nothing written");
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("nope"), path("r2")),
           err(ZXVD_E_FAB, "FILE: cannot read %s", path("nope")));
    EXPECT(call("fab.receive", "{\"recipe\":\"rel\",\"out\":\"%s\"}", path("r2")),
           err(ZXVD_E_PARAMS, "\\\"recipe\\\" must be an absolute path"));
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"),
                path("no/dir/x")),
           err(ZXVD_E_FAB, "FILE: cannot write %s", path("no/dir/x")));

    /* B8: blocks missing or damaged in the store */
    char sp[300];
    snprintf(sp, sizeof sp, "fab/blocks/%s", c1);
    remove(path(sp));
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"), path("r3")),
           err(ZXVD_E_FAB, "FETCH: a block is not in the store"));
    b1[100] ^= 1;
    wfile(sp, b1, B1);
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"), path("r3")),
           err(ZXVD_E_FAB, "BLOCK: a block does not match its CID"));
    b1[100] ^= 1;
    wfile(sp, b1, B1);
    CHECK(!exists("r3"), "nothing written");
    /* a decoy that is missing is still fetched: the receiver checks every block */
    snprintf(sp, sizeof sp, "fab/blocks/%s", cd);
    remove(path(sp));
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"), path("r3")),
           err(ZXVD_E_FAB, "FETCH: a block is not in the store"));
    wfile(sp, bd, BD);
}

static void test_airgap(void)
{
    const char *r = path("sec.fab");
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"airgap\":true}", r, path("a1")),
           err(ZXVD_E_FAB, "SEAL: no air-gap seal is registered in this zxvd"));
    zxvd_fab_set_seal(seal, &SEAL);
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"seal\":true"), "B5: the seal is registered");
    static const lpres_state_t NO[] = {LPRES_STATE_FALSE, LPRES_STATE_BOTH, LPRES_STATE_NEITHER};
    for (int i = 0; i < 3; i++) {
        SEAL = NO[i];
        SEAL_CALLS = 0;
        EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"airgap\":true}", r,
                    path("a1")),
               err(ZXVD_E_FAB, "SEAL: the air-gap seal is not in place"));
        CHECK(SEAL_CALLS == 1, "B4: asked once (%d)", SEAL_CALLS);
        CHECK(!exists("a1"), "B4: nothing written while the seal is %d", (int) NO[i]);
    }
    /* B4: the blocks are checked before the seal is asked */
    char sp[300];
    snprintf(sp, sizeof sp, "fab/blocks/%s", c2);
    remove(path(sp));
    SEAL = LPRES_STATE_TRUE;
    SEAL_CALLS = 0;
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"airgap\":true}", r, path("a1")),
           err(ZXVD_E_FAB, "FETCH: a block is not in the store"));
    CHECK(SEAL_CALLS == 0, "B4: a missing block fails before the seal");
    wfile(sp, b2, B2);
    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"airgap\":true}", r, path("a1")));
    CHECK(is_ok() && SEAL_CALLS == 1 && same("a1", tgt, tlen), "B4: TRUE assembles");
    /* B4: airgap false asks nothing */
    SEAL = LPRES_STATE_FALSE;
    SEAL_CALLS = 0;
    rpc(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", r, path("a2")));
    CHECK(is_ok() && SEAL_CALLS == 0, "B4: no seal asked without airgap");
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\",\"airgap\":1}", r, path("a2")),
           err(ZXVD_E_PARAMS, "\\\"airgap\\\" must be true or false"));
    zxvd_fab_set_seal(NULL, &SEAL);
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"seal\":false"), "B4: NULL removes the seal");
}

/* ----- receiving in steps (B13) ----- */

static const char *rstart(const char *recipe, const char *out, const char *extra)
{
    return call("fab.receive.start", "{\"recipe\":\"%s\",\"out\":\"%s\"%s}", path(recipe),
                path(out), extra);
}

static const char *rstep(long job, const char *extra)
{
    return call("fab.receive.step", "{\"job\":%ld%s}", job, extra);
}

#define BYTES(b)   "\"bytes_done\":\"" b "\",\"bytes_total\":\"450000\""
#define PROG(d, b) "\"blocks_done\":" #d ",\"blocks_total\":3," BYTES(b)

static void test_jobs(void)
{
    fresh();
    CHECK(zxvd_rpc_enable("fab") == 0 && zxvd_fab_open(path("fab")) == 0, "fab on and open");
    EXPECT(rstart("pub.fab", "j1", ""),
           ok("{\"job\":1,\"cid\":\"%s\",\"len\":%u,\"mode\":\"public\",\"sender\":\"%s\"," PROG(
                  0, "0") "}",
              tcid, (unsigned) tlen, sid));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"receives\":[{\"job\":1," PROG(0, "0") "}]"), "B5: the job: %.400s",
          OUT.p);
    EXPECT(rstep(1, ",\"blocks\":1"), ok("{\"job\":1,\"phase\":\"fetch\"," PROG(1, "300000") "}"));
    EXPECT(rstep(1, ",\"blocks\":1"), ok("{\"job\":1,\"phase\":\"fetch\"," PROG(2, "400000") "}"));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"receives\":[{\"job\":1," PROG(2, "400000") "}]"), "B5: progress");
    EXPECT(rstep(1, ""), ok("{\"job\":1,\"phase\":\"fetch\"," PROG(3, "450000") "}"));
    CHECK(!exists("j1"), "B13: nothing written while fetching");
    EXPECT(rstep(1, ""),
           ok("{\"job\":1,\"phase\":\"done\"," PROG(3, "450000") ",\"cid\":\"%s\",\"len\":%u}",
              tcid, (unsigned) tlen));
    CHECK(same("j1", tgt, tlen) && mode_of("j1") == 0600, "B13: the file comes back");
    EXPECT(rstep(1, ""), err(ZXVD_E_FAB, "STREAM: no receive job 1"));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"receives\":[]}"), "B13: done ends the job");

    /* the default step takes up to 16 blocks; sealed recipes too */
    rpc(rstart("sec.fab", "j2", ""));
    CHECK(num("job") == 2 && !strcmp(str("mode"), "sealed"), "B13: sealed: %.200s", OUT.p);
    EXPECT(rstep(2, ",\"blocks\":0"), err(ZXVD_E_PARAMS, "\\\"blocks\\\" must be 1..4096"));
    EXPECT(rstep(2, ",\"blocks\":4097"), err(ZXVD_E_PARAMS, "\\\"blocks\\\" must be 1..4096"));
    rpc(rstep(2, ",\"blocks\":4096"));
    CHECK(num("blocks_done") == 3, "B13: a step of 4096: %.200s", OUT.p);
    rpc(rstep(2, ""));
    CHECK(!strcmp(str("phase"), "done") && same("j2", tgt, tlen), "B13: sealed done");

    /* no blocks: the first step builds */
    rpc(rstart("lit.fab", "j3", ""));
    CHECK(num("blocks_total") == 0 && !strcmp(str("bytes_total"), "0"), "B13: no blocks");
    rpc(rstep(num("job"), ""));
    CHECK(!strcmp(str("phase"), "done") && same("j3", tgt, tlen), "B13: built at once");

    /* cancel */
    rpc(rstart("pub.fab", "j4", ""));
    long j = num("job");
    CHECK(j == 4, "handles count up (%ld)", j);
    rpc(rstep(j, ",\"blocks\":1"));
    EXPECT(call("fab.receive.cancel", "{\"job\":%ld}", j), ok("{\"ok\":true}"));
    EXPECT(call("fab.receive.cancel", "{\"job\":%ld}", j),
           err(ZXVD_E_FAB, "STREAM: no receive job 4"));
    EXPECT(call("fab.receive.cancel", "{\"job\":0}"), err(ZXVD_E_FAB, "STREAM: no receive job 0"));
    CHECK(!exists("j4") && !exists("j4.tmp"), "B13: cancel writes nothing");

    /* at most ZXVD_FAB_JOBS */
    long h[ZXVD_FAB_JOBS];
    for (uint32_t i = 0; i < ZXVD_FAB_JOBS; i++) {
        rpc(rstart("pub.fab", "j5", ""));
        h[i] = num("job");
    }
    CHECK(h[0] == 5 && h[ZXVD_FAB_JOBS - 1] == 4 + (long) ZXVD_FAB_JOBS, "handles %ld..%ld", h[0],
          h[ZXVD_FAB_JOBS - 1]);
    EXPECT(rstart("pub.fab", "j5", ""),
           err(ZXVD_E_FAB, "STREAM: 4 receive jobs are open; finish or cancel one"));
    rpc(rstep(h[2], ",\"blocks\":2"));
    CHECK(num("blocks_done") == 2 && num("job") == h[2], "B13: jobs are apart: %.200s", OUT.p);
    for (uint32_t i = 0; i < ZXVD_FAB_JOBS; i++)
        rpc(call("fab.receive.cancel", "{\"job\":%ld}", h[i]));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"receives\":[]}"), "B13: all cancelled");

    /* refusals at the start leave no job */
    EXPECT(rstart("x.fab", "j6", ""), err(ZXVD_E_FAB, "KEY: it is not sealed to this zxvd's key"));
    EXPECT(call("fab.receive.start", "{\"recipe\":\"%s\"}", path("pub.fab")),
           err(ZXVD_E_PARAMS, "missing string parameter \\\"out\\\""));
    EXPECT(rstart("s.fab", "j6", ""),
           err(ZXVD_E_FAB, "FORM: a stream recipe; use fab.stream.open"));
    EXPECT(rstart("sec.fab", "j6", ",\"airgap\":true"),
           err(ZXVD_E_FAB, "SEAL: no air-gap seal is registered in this zxvd"));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"receives\":[]}"), "B13: no job from a refusal: %.300s", OUT.p);
    EXPECT(call("fab.receive.step", "{}"),
           err(ZXVD_E_PARAMS, "missing integer parameter \\\"job\\\""));

    /* a missing block ends the job at that step */
    char sp[300];
    snprintf(sp, sizeof sp, "fab/blocks/%s", c2);
    remove(path(sp));
    rpc(rstart("pub.fab", "j7", ""));
    j = num("job");
    EXPECT(rstep(j, ",\"blocks\":1"),
           ok("{\"job\":%ld,\"phase\":\"fetch\"," PROG(1, "300000") "}", j));
    EXPECT(rstep(j, ""), err(ZXVD_E_FAB, "FETCH: a block is not in the store"));
    EXPECT(rstep(j, ""), err(ZXVD_E_FAB, "STREAM: no receive job %ld", j));
    wfile(sp, b2, B2);

    /* the seal: asked once, after every block, and only then */
    zxvd_fab_set_seal(seal, &SEAL);
    SEAL = LPRES_STATE_BOTH;
    SEAL_CALLS = 0;
    rpc(rstart("sec.fab", "j8", ",\"airgap\":true"));
    j = num("job");
    rpc(rstep(j, ""));
    CHECK(num("blocks_done") == 3 && SEAL_CALLS == 0, "B13: fetching asks no seal");
    EXPECT(rstep(j, ""), err(ZXVD_E_FAB, "SEAL: the air-gap seal is not in place"));
    CHECK(SEAL_CALLS == 1 && !exists("j8"), "B13: refused by the seal, nothing written");
    SEAL = LPRES_STATE_TRUE;
    rpc(rstart("sec.fab", "j8", ",\"airgap\":true"));
    j = num("job");
    rpc(rstep(j, ""));
    rpc(rstep(j, ""));
    CHECK(!strcmp(str("phase"), "done") && same("j8", tgt, tlen), "B13: TRUE assembles");
    /* a seal removed after the start is refused at the build */
    rpc(rstart("sec.fab", "j9", ",\"airgap\":true"));
    j = num("job");
    rpc(rstep(j, ""));
    zxvd_fab_set_seal(NULL, &SEAL);
    EXPECT(rstep(j, ""), err(ZXVD_E_FAB, "SEAL: no air-gap seal is registered in this zxvd"));
    CHECK(!exists("j9"), "B13: nothing written");

    /* the group's close ends every job */
    rpc(rstart("pub.fab", "j10", ""));
    j = num("job");
    zxvd_fab_close();
    EXPECT(rstep(j, ""), err(ZXVD_E_FAB, "FILE: fabrication is not open in this zxvd"));
    CHECK(zxvd_fab_open(path("fab")) == 0, "reopen");
    EXPECT(rstep(j, ""), err(ZXVD_E_FAB, "STREAM: no receive job %ld", j));
}

/* ----- streams ----- */

#define FR 20000u
static uint8_t f[6][FR * 3];

static void frame_bytes(void)
{
    memcpy(f[0], b1, FR); /* frame 0 mostly from block 1 */
    for (int k = 1; k < 6; k++) {
        memcpy(f[k], f[k - 1], FR);
        for (uint32_t i = 0; i < 40; i++) f[k][(k * 977u + i * 131u) % FR] = rb();
    }
}

static long stream_start(const char *recipe)
{
    rpc(call("fab.stream.start",
             "{\"target\":\"%s\",\"out\":\"%s\",\"to\":\"%s\",\"slot_us\":16667,"
             "\"blocks\":[{\"cid\":\"%s\"}]}",
             path("f0"), path(recipe), ek_hex, c1));
    return is_ok() ? num("stream") : -1;
}

static void test_stream(void)
{
    frame_bytes();
    wfile("f0", f[0], FR);
    char f0cid[IPFSN_CID_STR_MAX];
    ipfsn_cid_t c;
    fab_target_cid(f[0], FR, &TW, &c);
    ipfsn_cid_to_string(&c, f0cid, sizeof f0cid);

    /* B9 parameters */
    EXPECT(call("fab.stream.start", "{\"target\":\"%s\",\"out\":\"%s\",\"slot_us\":1}", path("f0"),
                path("s.fab")),
           err(ZXVD_E_PARAMS, "missing hex parameter \\\"to\\\""));
    EXPECT(call("fab.stream.start", "{\"target\":\"%s\",\"out\":\"%s\",\"to\":\"%s\"}", path("f0"),
                path("s.fab"), ek_hex),
           err(ZXVD_E_PARAMS, "missing integer parameter \\\"slot_us\\\""));
    EXPECT(call("fab.stream.start",
                "{\"target\":\"%s\",\"out\":\"%s\",\"to\":\"%s\",\"slot_us\":0}", path("f0"),
                path("s.fab"), ek_hex),
           err(ZXVD_E_PARAMS, "\\\"slot_us\\\" must be at least 1"));
    EXPECT(call("fab.stream.send", "{\"stream\":1,\"target\":\"%s\",\"out\":\"%s\"}", path("f0"),
                path("x")),
           err(ZXVD_E_FAB, "STREAM: no sending stream 1"));
    EXPECT(call("fab.stream.start",
                "{\"target\":\"%s\",\"out\":\"%s\",\"to\":\"%s\",\"slot_us\":5,\"window\":2}",
                path("f0"), path("s.fab"), ek_hex),
           err(ZXVD_E_PARAMS, "\\\"window\\\" must be at least 8"));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"streams\":[]"), "a refused start holds no stream");

    /* the sender: stream 1 */
    long tx = stream_start("s.fab");
    CHECK(tx == 1, "B11: the first handle is 1 (%ld) %.200s", tx, OUT.p);
    CHECK(!strcmp(str("cid"), f0cid) && num("len") == FR, "B9: frame 0's cid");
    /* the receiver: stream 2 */
    EXPECT(call("fab.receive", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("s.fab"), path("x")),
           err(ZXVD_E_FAB, "FORM: a stream recipe; use fab.stream.open"));
    EXPECT(
        call("fab.stream.open", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("pub.fab"), path("x")),
        err(ZXVD_E_FAB, "FORM: a file recipe; use fab.receive"));
    EXPECT(call("fab.stream.open", "{\"recipe\":\"%s\",\"out\":\"%s\",\"airgap\":true}",
                path("s.fab"), path("x")),
           err(ZXVD_E_FAB, "SEAL: no air-gap seal is registered in this zxvd"));
    EXPECT(call("fab.stream.open", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("s.fab"), path("r0")),
           ok("{\"stream\":2,\"cid\":\"%s\",\"len\":%u,\"slot_us\":16667}", f0cid, FR));
    CHECK(same("r0", f[0], FR), "B10: frame 0");
    EXPECT(
        call("fab.status", "{}"),
        ok("{\"open\":true,\"sign_id\":\"%s\",\"kem_id\":\"%s\",\"seal\":false,\"streams\":["
           "{\"stream\":1,\"role\":\"send\",\"seq\":\"0\",\"checked\":\"0\"},"
           "{\"stream\":2,\"role\":\"receive\",\"seq\":\"0\",\"checked\":\"0\"}],\"receives\":[]}",
           sid, kid));
    /* roles are not interchangeable */
    EXPECT(call("fab.stream.frame", "{\"stream\":1,\"frame\":\"%s\",\"out\":\"%s\"}", path("s.fab"),
                path("x")),
           err(ZXVD_E_FAB, "STREAM: no receiving stream 1"));
    EXPECT(call("fab.stream.send", "{\"stream\":2,\"target\":\"%s\",\"out\":\"%s\"}", path("f0"),
                path("x")),
           err(ZXVD_E_FAB, "STREAM: no sending stream 2"));
    EXPECT(call("fab.stream.sign", "{\"stream\":2,\"from\":0,\"to\":0}"),
           err(ZXVD_E_FAB, "STREAM: no sending stream 2"));
    EXPECT(call("fab.stream.send", "{\"stream\":0,\"target\":\"%s\",\"out\":\"%s\"}", path("f0"),
                path("x")),
           err(ZXVD_E_FAB, "STREAM: no sending stream 0"));

    /* frames 1..4 */
    char nm[16], fn[16];
    uint32_t sizes[5] = {0};
    for (int k = 1; k <= 4; k++) {
        snprintf(nm, sizeof nm, "f%d", k);
        snprintf(fn, sizeof fn, "fr%d", k);
        wfile(nm, f[k], FR);
        rpc(call("fab.stream.send", "{\"stream\":1,\"target\":\"%s\",\"out\":\"%s\"}", path(nm),
                 path(fn)));
        CHECK(is_ok() && atol(str("seq")) == k && num("len") == FR, "B9: frame %d: %.200s", k,
              OUT.p);
        sizes[k] = (uint32_t) num("frame");
        CHECK(sizes[k] < FR / 4, "B9: frame %d travels as changes (%u bytes)", k,
              (unsigned) sizes[k]);
        CHECK(num("diff") > 0 && num("diff") <= 200, "B9: frame %d diff %ld", k, num("diff"));
    }
    rpc(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr1"),
             path("r1")));
    CHECK(is_ok() && atol(str("seq")) == 1 && same("r1", f[1], FR), "B10: frame 1: %.200s", OUT.p);
    fab_target_cid(f[1], FR, &TW, &c);
    char f1cid[IPFSN_CID_STR_MAX];
    ipfsn_cid_to_string(&c, f1cid, sizeof f1cid);
    CHECK(!strcmp(str("cid"), f1cid), "B10: frame 1's cid");
    EXPECT(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr1"),
                path("r1x")),
           err(ZXVD_E_FAB, "REPLAY: the frame is not newer than the last one"));
    /* frame 3 is built on frame 2, which this end has not seen */
    EXPECT(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr3"),
                path("r3")),
           err(ZXVD_E_FAB, "RESYNC: the frame needs a base this end does not hold"));
    CHECK(!exists("r1x") && !exists("r3"), "nothing written by refused frames");
    rpc(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr2"),
             path("r2")));
    CHECK(is_ok() && same("r2", f[2], FR), "B10: frame 2 after the RESYNC");
    rpc(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr3"),
             path("r3")));
    CHECK(is_ok() && same("r3", f[3], FR), "B10: frame 3 once 2 is held");
    /* a damaged frame */
    uint32_t n = 0;
    uint8_t *fr = rfile("fr4", &n);
    fr[n - 3] ^= 1;
    wfile("fr4bad", fr, n);
    EXPECT(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}",
                path("fr4bad"), path("r4")),
           err(ZXVD_E_FAB, "AUTH: decryption failed: tampered, or the wrong key"));
    wfile("fr4bad", fr, 20);
    EXPECT(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}",
                path("fr4bad"), path("r4")),
           err(ZXVD_E_FAB, "FORM: not a well-formed recipe, frame or checkpoint"));
    free(fr);
    rpc(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr4"),
             path("r4")));
    CHECK(is_ok() && same("r4", f[4], FR), "B10: frame 4 after the damaged copies");

    /* B9, B10: checkpoints */
    rpc(call("fab.stream.sign", "{\"stream\":1,\"from\":0,\"to\":4}"));
    char cph[2 * FAB_CP_LEN + 1];
    snprintf(cph, sizeof cph, "%s", str("checkpoint"));
    CHECK(strlen(cph) == 2 * FAB_CP_LEN, "B9: a checkpoint of %u bytes", (unsigned) FAB_CP_LEN);
    EXPECT(call("fab.stream.checkpoint", "{\"stream\":2,\"checkpoint\":\"%s\"}", cph),
           ok("{\"from\":\"0\",\"to\":\"4\"}"));
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "{\"stream\":2,\"role\":\"receive\",\"seq\":\"4\",\"checked\":\"4\"}") &&
              strstr(OUT.p, "{\"stream\":1,\"role\":\"send\",\"seq\":\"4\",\"checked\":\"0\"}"),
          "B5: the streams' state: %.400s", OUT.p);
    cph[300] = cph[300] == '0' ? '1' : '0';
    EXPECT(call("fab.stream.checkpoint", "{\"stream\":2,\"checkpoint\":\"%s\"}", cph),
           err(ZXVD_E_FAB, "SIGN: the signature does not verify"));
    EXPECT(call("fab.stream.checkpoint", "{\"stream\":2,\"checkpoint\":\"abcd\"}"),
           err(ZXVD_E_FAB, "FORM: not a well-formed recipe, frame or checkpoint"));
    EXPECT(call("fab.stream.checkpoint", "{\"stream\":1,\"checkpoint\":\"abcd\"}"),
           err(ZXVD_E_FAB, "STREAM: no receiving stream 1"));
    EXPECT(call("fab.stream.sign", "{\"stream\":1,\"from\":3,\"to\":9}"),
           err(ZXVD_E_FAB, "RESYNC: the frame needs a base this end does not hold"));
    EXPECT(call("fab.stream.sign", "{\"stream\":1,\"from\":-1,\"to\":2}"),
           err(ZXVD_E_PARAMS, "\\\"from\\\" must not be negative"));
    EXPECT(call("fab.stream.sign", "{\"stream\":1,\"from\":0,\"to\":-2}"),
           err(ZXVD_E_PARAMS, "\\\"to\\\" must not be negative"));
    RANDOM_FAIL = 1;
    EXPECT(call("fab.stream.sign", "{\"stream\":1,\"from\":0,\"to\":1}"),
           err(ZXVD_E_INTERNAL, "no random source"));
    RANDOM_FAIL = 0;

    /* a frame that grows past the receiver's buffer (B10 doubles it) */
    memcpy(f[5], f[4], FR);
    fill(f[5] + FR, FR * 2);
    wfile("f5", f[5], FR * 3);
    rpc(call("fab.stream.send", "{\"stream\":1,\"target\":\"%s\",\"out\":\"%s\"}", path("f5"),
             path("fr5")));
    CHECK(is_ok() && atol(str("seq")) == 5 && num("len") == FR * 3, "B9: a larger frame");
    rpc(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr5"),
             path("r5")));
    CHECK(is_ok() && num("len") == FR * 3 && same("r5", f[5], FR * 3), "B10: it grows: %.200s",
          OUT.p);
    /* and shrinks again, built on the large one */
    wfile("f6", f[1], FR);
    rpc(call("fab.stream.send", "{\"stream\":1,\"target\":\"%s\",\"out\":\"%s\"}", path("f6"),
             path("fr6")));
    rpc(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr6"),
             path("r6")));
    CHECK(is_ok() && same("r6", f[1], FR), "B10: it shrinks");

    /* B11: four streams at most; handles never reused */
    long h3 = stream_start("s3.fab"), h4 = stream_start("s4.fab");
    CHECK(h3 == 3 && h4 == 4, "B11: handles count up (%ld %ld)", h3, h4);
    EXPECT(call("fab.stream.start",
                "{\"target\":\"%s\",\"out\":\"%s\",\"to\":\"%s\",\"slot_us\":5}", path("f0"),
                path("s5.fab"), ek_hex),
           err(ZXVD_E_FAB, "STREAM: 4 streams are open; close one first"));
    EXPECT(call("fab.stream.open", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("s3.fab"), path("x")),
           err(ZXVD_E_FAB, "STREAM: 4 streams are open; close one first"));
    CHECK(!exists("s5.fab"), "nothing written past the limit");
    EXPECT(call("fab.stream.close", "{\"stream\":3}"), ok("{\"ok\":true}"));
    EXPECT(call("fab.stream.close", "{\"stream\":3}"), err(ZXVD_E_FAB, "STREAM: no stream 3"));
    EXPECT(call("fab.stream.close", "{\"stream\":0}"), err(ZXVD_E_FAB, "STREAM: no stream 0"));
    CHECK(stream_start("s5.fab") == 5, "B11: a closed handle is not reused");
    EXPECT(call("fab.stream.close", "{\"stream\":2}"), ok("{\"ok\":true}"));
    EXPECT(call("fab.stream.frame", "{\"stream\":2,\"frame\":\"%s\",\"out\":\"%s\"}", path("fr6"),
                path("x")),
           err(ZXVD_E_FAB, "STREAM: no receiving stream 2"));
    /* a refused open holds no slot */
    EXPECT(call("fab.stream.open", "{\"recipe\":\"%s\",\"out\":\"%s\",\"trust\":[{\"id\":\"%s\"}]}",
                path("s.fab"), path("x"), xid_hex),
           err(ZXVD_E_FAB, "TRUST: the sender is not in the trust list"));
    rpc(call("fab.stream.open", "{\"recipe\":\"%s\",\"out\":\"%s\"}", path("s.fab"), path("r0b")));
    CHECK(is_ok() && num("stream") == 6, "the free slot is still free: %.200s", OUT.p);
    /* the group's close forgets every stream */
    zxvd_rpc_groups_reset();
    fresh();
    rpc(call("fab.status", "{}"));
    CHECK(strstr(OUT.p, "\"open\":false") && strstr(OUT.p, "\"streams\":[]"),
          "B11: closed with the group: %.200s", OUT.p);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(R, sizeof R, "%s/zxvd-fab.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(R)) {
        printf("mkdtemp failed\n");
        return 2;
    }
    zxvd_fab_set_random(test_random);
    TW.loaded = -1;
    fixture();
    test_open();
    test_put();
    test_files();
    test_send_params();
    test_receive_refusals();
    test_airgap();
    test_stream();
    test_jobs();
    zxvd_rpc_groups_reset();
    zxvd_fab_set_random(NULL);
    nftw(R, rm_one, 16, FTW_DEPTH | FTW_PHYS);
    free(OUT.p);
    printf("test_zxvd_fab: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
