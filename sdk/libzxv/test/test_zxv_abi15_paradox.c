/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_paradox.c — conformance test of the ABI 1.5 paradox group,
 * linked against the shared library only (no kernel headers). The rules
 * are restated here independently (kind of a tag, the class pair, the
 * action and outcome tables, classification) and checked on all 64 tags;
 * every byte layout the header documents (log records, quarantine
 * records, RepairProof, draft, proposal, the parameter hash) is rebuilt
 * with an independent SHA-256; each operator is run through each outcome;
 * every error path, capability, context binding and fail-closed rule is
 * exercised; and graph resolution is checked for determinism: the same
 * inputs resolved twice, and in shuffled node, edge and update orders,
 * give identical chains, states and log bytes. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)
#define OK(call)                                                                   \
    do {                                                                           \
        int32_t rc_ = (call);                                                      \
        CHECK(rc_ == ZXV_OK, "%s -> %s (%d)", #call, zxv_status_name(rc_), rc_);   \
    } while (0)
#define IS(call, want)                                                                         \
    do {                                                                                       \
        int32_t rc_ = (call);                                                                  \
        CHECK(rc_ == (want), "%s -> %s (%d), want %s", #call, zxv_status_name(rc_), rc_,       \
              #want);                                                                          \
    } while (0)

/* ---- a small SHA-256 (FIPS 180-4), independent of the library ---- */
static uint32_t ror(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}
static void sha256(const uint8_t *m, size_t len, uint8_t out[32])
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    static uint8_t buf[2048];
    size_t padded = ((len + 9 + 63) / 64) * 64;
    if (padded > sizeof buf) {
        memset(out, 0, 32);
        return;
    }
    memset(buf, 0, padded);
    if (len) memcpy(buf, m, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t) len * 8;
    for (int i = 0; i < 8; i++) buf[padded - 1 - i] = (uint8_t) (bits >> (8 * i));
    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t) buf[off + 4 * i] << 24 | (uint32_t) buf[off + 4 * i + 1] << 16 |
                   (uint32_t) buf[off + 4 * i + 2] << 8 | buf[off + 4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t) (h[i] >> (24 - 8 * j));
}
/* SHA-256(prefix || data) */
static void dsha(const char *prefix, const uint8_t *data, size_t len, uint8_t out[32])
{
    uint8_t b[1024];
    size_t k = strlen(prefix);
    memcpy(b, prefix, k);
    if (len) memcpy(b + k, data, len);
    sha256(b, k + len, out);
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t) rd32(p) | (uint64_t) rd32(p + 4) << 32;
}
static void le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static int zero32(const uint8_t *p)
{
    for (int i = 0; i < 32; i++)
        if (p[i]) return 0;
    return 1;
}

/* ---- the rules, restated ---- */
#define B_F ZXV_HEX_BASE_FALSE
#define B_N ZXV_HEX_BASE_NEITHER
#define B_B ZXV_HEX_BASE_BOTH
#define B_T ZXV_HEX_BASE_TRUE
#define P_E ZXV_HEX_POL_ENTROPIC
#define P_0 ZXV_HEX_POL_NEUTRAL
#define P_X ZXV_HEX_POL_FORBIDDEN
#define P_G ZXV_HEX_POL_NEGENTROPIC
#define PH_POT ZXV_HEX_PHASE_POTENTIAL
#define PH_MAN ZXV_HEX_PHASE_MANIFEST
static uint8_t T(int base, int pol, int phase)
{
    return (uint8_t) (base << 4 | pol << 2 | phase);
}
static int tb(uint8_t t) { return t >> 4; }
static int tp(uint8_t t) { return (t >> 2) & 3; }
static int tph(uint8_t t) { return t & 3; }

static int oracle_kind_of_tag(uint8_t t)
{
    if (tp(t) == P_X) return ZXV_PDX_KIND_NONE;
    if (tb(t) == B_N) return ZXV_PDX_KIND_NEUTRAL;
    if (tb(t) != B_B) return ZXV_PDX_KIND_NONE;
    if (tp(t) == P_G) return ZXV_PDX_KIND_CONSTRUCTIVE;
    if (tp(t) == P_E) return ZXV_PDX_KIND_DESTRUCTIVE;
    return tph(t) == PH_POT ? ZXV_PDX_KIND_SPECIAL : ZXV_PDX_KIND_NEUTRAL;
}
static int kpol(int k)
{
    return k == ZXV_PDX_KIND_CONSTRUCTIVE ? P_G : k == ZXV_PDX_KIND_DESTRUCTIVE ? P_E : P_0;
}
static int oracle_pair_ok(int k, uint8_t t)
{
    if (k < 0 || k > 4 || t > 63) return 0;
    if (tp(t) == P_X) return 1;
    if (k == ZXV_PDX_KIND_NONE) return tb(t) == B_T || tb(t) == B_F;
    if (tb(t) != B_B && tb(t) != B_N) return 0;
    if (tp(t) != kpol(k)) return 0;
    if (k == ZXV_PDX_KIND_SPECIAL) return tb(t) == B_B && tph(t) == PH_POT;
    return 1;
}
static int oracle_polarity(const uint32_t e[4], int stale)
{
    if (stale) return P_E;
    uint32_t m0 = e[0] > e[1] ? e[0] - e[1] : e[1] - e[0];
    uint32_t m1 = e[2] > e[3] ? e[2] - e[3] : e[3] - e[2];
    return m1 > m0 ? P_G : m1 < m0 ? P_E : P_0;
}

/* the class pairs used below */
static const uint8_t TAG_CONS = 2 << 4 | 3 << 2 | 3;  /* both/negentropic/manifest */
static const uint8_t TAG_DEST = 2 << 4 | 0 << 2 | 3;  /* both/entropic/manifest    */
static const uint8_t TAG_NEUT = 2 << 4 | 1 << 2 | 3;  /* both/neutral/manifest     */
static const uint8_t TAG_SPEC = 2 << 4 | 1 << 2 | 0;  /* both/neutral/potential    */
static const uint8_t TAG_NONE = 3 << 4 | 1 << 2 | 3;  /* true/neutral/manifest     */
static const uint8_t TAG_FORB = 2 << 4 | 2 << 2 | 3;  /* both/forbidden/manifest   */

static void id_of(uint32_t i, uint8_t out[32])
{
    uint8_t b[4];
    le32(b, i);
    dsha("test-node", b, 4, out);
}

/* ---- log record bytes ---- */
static int rec_bytes(zxv_paradox *b, int which, uint32_t i, uint8_t *out)
{
    size_t n = 0;
    return zxv_paradox_log_record(b, which, i, out, 200, &n);
}
/* recompute one operator/frontier record's hash and its link */
static int rec_hash_ok(const uint8_t r[144], const uint8_t prev[32])
{
    uint8_t h[32];
    dsha("zxv/pdx-log/v1", r, 112, h);
    return !memcmp(h, r + 112, 32) && !memcmp(r + 80, prev, 32);
}
static int chain_ok(zxv_paradox *b, int which)
{
    uint32_t n = 0;
    if (zxv_paradox_log_count(b, which, &n)) return 0;
    uint8_t prev[32] = {0}, r[200], head[32];
    for (uint32_t i = 0; i < n; i++) {
        if (rec_bytes(b, which, i, r)) return 0;
        if (rd64(r) != i) return 0;
        uint8_t h[32];
        if (which == ZXV_PDX_LOG_QUARANTINE) {
            dsha("zxv/lpx-qlog/v1", r, 78, h);
            if (memcmp(h, r + 78, 32) || memcmp(r + 46, prev, 32)) return 0;
            memcpy(prev, r + 78, 32);
        } else {
            if (!rec_hash_ok(r, prev)) return 0;
            memcpy(prev, r + 112, 32);
        }
    }
    if (zxv_paradox_log_head(b, which, head)) return 0;
    return !memcmp(head, prev, 32);
}
static void params_hash(const int64_t *v, uint32_t n, uint8_t out[32])
{
    uint8_t b[4 + 8 * 64];
    le32(b, n);
    for (uint32_t i = 0; i < n; i++) le64(b + 4 + 8 * i, (uint64_t) v[i]);
    dsha("zxv/pdx-params/v1", b, 4 + 8 * n, out);
}
static void state_hash(const uint8_t *s, uint32_t len, uint8_t out[32])
{
    uint8_t b[300];
    le32(b, len);
    memcpy(b + 4, s, len);
    dsha("zxv/pdx-state/v1", b, 4 + len, out);
}

static zxv_ctx *ctx, *other;
static zxv_cap *cap, *nocap, *foreign;

/* ===================================================================== */
static void test_pure(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    CHECK(m & ZXV_FEAT2_PARADOX, "ZXV_FEAT2_PARADOX is live");

    uint8_t d[32], s[32];
    sha256((const uint8_t *) "abc", 3, d);
    CHECK(d[0] == 0xba && d[1] == 0x78 && d[31] == 0xad, "SHA-256(abc)");
    OK(zxv_paradox_signature((const uint8_t *) "loop-trap", 9, s));
    dsha("zxv/pdx-sig/v1", (const uint8_t *) "loop-trap", 9, d);
    CHECK(!memcmp(s, d, 32), "signature = SHA-256(zxv/pdx-sig/v1 || data)");
    OK(zxv_paradox_signature(NULL, 0, s));
    dsha("zxv/pdx-sig/v1", NULL, 0, d);
    CHECK(!memcmp(s, d, 32), "empty signature");
    IS(zxv_paradox_signature(NULL, 1, s), ZXV_E_ARG);
    IS(zxv_paradox_signature((const uint8_t *) "x", 1, NULL), ZXV_E_ARG);

    int bad_kind = 0, bad_do = 0;
    for (int t = 0; t < 64; t++) {
        int32_t k = -1;
        OK(zxv_paradox_kind_of_tag((uint8_t) t, &k));
        if (k != oracle_kind_of_tag((uint8_t) t)) bad_kind++;
        for (int kk = -1; kk <= 5; kk++) {
            int32_t act = -7;
            int32_t rc = zxv_paradox_action(kk, (uint8_t) t, &act);
            if (oracle_pair_ok(kk, (uint8_t) t)) {
                int want = tp((uint8_t) t) == P_X           ? ZXV_PDX_DO_ROLLBACK
                           : kk == ZXV_PDX_KIND_NONE        ? ZXV_PDX_DO_NONE
                           : kk == ZXV_PDX_KIND_CONSTRUCTIVE ? ZXV_PDX_DO_SYNTHESIZE
                           : kk == ZXV_PDX_KIND_DESTRUCTIVE  ? ZXV_PDX_DO_CONTAIN
                           : kk == ZXV_PDX_KIND_NEUTRAL      ? ZXV_PDX_DO_HOLD
                                                             : ZXV_PDX_DO_RECORD;
                if (rc != ZXV_OK || act != want) bad_do++;
            } else if (rc != ZXV_E_ARG || act != -7) {
                bad_do++;
            }
        }
    }
    CHECK(bad_kind == 0, "kind_of_tag on all 64 tags (%d wrong)", bad_kind);
    CHECK(bad_do == 0, "action on all 64 x 7 pairs (%d wrong)", bad_do);
    int32_t x;
    IS(zxv_paradox_kind_of_tag(64, &x), ZXV_E_ARG);
    IS(zxv_paradox_kind_of_tag(0, NULL), ZXV_E_ARG);
    IS(zxv_paradox_action(0, 64, &x), ZXV_E_ARG);
    IS(zxv_paradox_action(0, TAG_CONS, NULL), ZXV_E_ARG);

    /* outcome -> hex action */
    static const int want_act[14] = {
        ZXV_HEX_ACT_PROCEED,  ZXV_HEX_ACT_PROCEED, ZXV_HEX_ACT_ESCALATE, ZXV_HEX_ACT_ROLLBACK,
        ZXV_HEX_ACT_ROLLBACK, ZXV_HEX_ACT_WAIT,    ZXV_HEX_ACT_PROCEED,  ZXV_HEX_ACT_WAIT,
        ZXV_HEX_ACT_PROCEED,  ZXV_HEX_ACT_WAIT,    ZXV_HEX_ACT_PROCEED,  ZXV_HEX_ACT_ESCALATE,
        ZXV_HEX_ACT_REFUSE,   ZXV_HEX_ACT_REFUSE};
    for (int o = 0; o < 14; o++) {
        int32_t a = -1;
        OK(zxv_paradox_outcome_action(o, &a));
        CHECK(a == want_act[o], "outcome %d -> action %d, want %d", o, a, want_act[o]);
    }
    IS(zxv_paradox_outcome_action(14, &x), ZXV_E_ARG);
    IS(zxv_paradox_outcome_action(-1, &x), ZXV_E_ARG);
    IS(zxv_paradox_outcome_action(0, NULL), ZXV_E_ARG);

    CHECK(!strcmp(zxv_paradox_kind_name(0), "constructive") &&
              !strcmp(zxv_paradox_kind_name(3), "special") &&
              !strcmp(zxv_paradox_kind_name(4), "none") &&
              !strcmp(zxv_paradox_kind_name(5), "INVALID") &&
              !strcmp(zxv_paradox_kind_name(-1), "INVALID"),
          "kind names");
    CHECK(!strcmp(zxv_paradox_outcome_name(2), "proposal-for-owner") &&
              !strcmp(zxv_paradox_outcome_name(11), "escalate-to-owner") &&
              !strcmp(zxv_paradox_outcome_name(13), "log-full") &&
              !strcmp(zxv_paradox_outcome_name(14), "INVALID"),
          "outcome names");
    CHECK(!strcmp(zxv_paradox_node_state_name(0), "open") &&
              !strcmp(zxv_paradox_node_state_name(5), "sandboxed") &&
              !strcmp(zxv_paradox_node_state_name(9), "applied") &&
              !strcmp(zxv_paradox_node_state_name(10), "INVALID") &&
              !strcmp(zxv_paradox_node_state_name(-1), "INVALID"),
          "node state names");

    /* repair_verify on a hand-built proof */
    uint8_t pr[160];
    for (int i = 0; i < 160; i++) pr[i] = (uint8_t) (i * 7);
    dsha("zxv/pdx-repair/v1", pr, 120, pr + 120);
    OK(zxv_paradox_repair_verify(pr));
    pr[5] ^= 1;
    IS(zxv_paradox_repair_verify(pr), ZXV_E_VERIFY);
    pr[5] ^= 1;
    pr[130] ^= 1;
    IS(zxv_paradox_repair_verify(pr), ZXV_E_VERIFY);
    pr[130] ^= 1;
    pr[155] ^= 1; /* qseq is not bound by the hash */
    OK(zxv_paradox_repair_verify(pr));
    IS(zxv_paradox_repair_verify(NULL), ZXV_E_ARG);

    /* classification without a book, all tags x evidence shapes */
    static const uint32_t evs[5][4] = {
        {1, 1, 5, 1}, {5, 1, 3, 3}, {2, 2, 4, 4}, {0, 0, 0, 0}, {9, 0, 1, 0}};
    int bad = 0;
    for (int t = 0; t < 64; t++)
        for (int e = -1; e < 5; e++)
            for (int st = 0; st < 2; st++) {
                int32_t k = -1, ri = 9;
                uint8_t ot = 99;
                OK(zxv_paradox_classify(NULL, (uint8_t) t, e < 0 ? NULL : evs[e], st, NULL, &k,
                                        &ot, &ri));
                int pol = e < 0 ? P_0 : oracle_polarity(evs[e], st);
                int base = tb((uint8_t) t), wk, wp;
                if (base != B_B && base != B_N) {
                    wk = ZXV_PDX_KIND_NONE;
                    wp = pol;
                } else {
                    wk = oracle_kind_of_tag(T(base, pol, tph((uint8_t) t)));
                    wp = kpol(wk);
                }
                if (tp((uint8_t) t) == P_X) wp = P_X;
                uint8_t wt = T(base, wp, tph((uint8_t) t));
                if (k != wk || ot != wt || ri != -1) bad++;
                if (!oracle_pair_ok(k, ot)) bad++; /* classify's output is a valid pair */
            }
    CHECK(bad == 0, "classify on all tags x evidence (%d wrong)", bad);
    IS(zxv_paradox_classify(NULL, 64, NULL, 0, NULL, &x, NULL, NULL), ZXV_E_ARG);
    OK(zxv_paradox_classify(NULL, TAG_CONS, NULL, 0, NULL, NULL, NULL, NULL));
}

/* ===================================================================== */
static void test_book_registry(void)
{
    zxv_paradox *b = NULL;
    IS(zxv_paradox_create(NULL, 8, 8, &b), ZXV_E_ARG);
    IS(zxv_paradox_create(nocap, 8, 8, &b), ZXV_E_CAP);
    IS(zxv_paradox_create(cap, 0, 8, &b), ZXV_E_ARG);
    IS(zxv_paradox_create(cap, ZXV_PDX_LOG_MAX + 1, 8, &b), ZXV_E_ARG);
    IS(zxv_paradox_create(cap, 8, 0, &b), ZXV_E_ARG);
    IS(zxv_paradox_create(cap, 8, ZXV_PDX_REGISTRY_MAX + 1, &b), ZXV_E_ARG);
    IS(zxv_paradox_create(cap, 8, 8, NULL), ZXV_E_ARG);
    CHECK(b == NULL, "no book on failure");
    OK(zxv_paradox_create(cap, 8, 2, &b));
    zxv_paradox_destroy(NULL);

    uint8_t s1[32], s2[32], s3[32], got[32];
    zxv_paradox_signature((const uint8_t *) "a", 1, s1);
    zxv_paradox_signature((const uint8_t *) "b", 1, s2);
    zxv_paradox_signature((const uint8_t *) "c", 1, s3);
    uint32_t idx = 77, n = 9;
    IS(zxv_paradox_registry_add(nocap, b, s1, 1, 0, 0, 0, &idx), ZXV_E_CAP);
    IS(zxv_paradox_registry_add(foreign, b, s1, 1, 0, 0, 0, &idx), ZXV_E_CAP);
    IS(zxv_paradox_registry_add(cap, NULL, s1, 1, 0, 0, 0, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_add(cap, b, NULL, 1, 0, 0, 0, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_add(cap, b, s1, ZXV_PDX_KIND_SPECIAL, 0, 0, 0, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_add(cap, b, s1, ZXV_PDX_KIND_NONE, 0, 0, 0, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_add(cap, b, s1, -1, 0, 0, 0, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_add(cap, b, s1, 1, 0, 4, 0, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_add(cap, b, s1, 1, 0, ZXV_PDX_FLAG_MONEY, 0, &idx), ZXV_E_PARADOX_OWNER);
    IS(zxv_paradox_registry_add(cap, b, s1, 1, 0, ZXV_PDX_FLAG_DEVICE, 0, &idx), ZXV_E_PARADOX_OWNER);
    CHECK(idx == 77, "out_index untouched on failure");
    OK(zxv_paradox_registry_count(b, &n));
    CHECK(n == 0, "nothing added by refused calls");
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &n));
    CHECK(n == 0, "nothing logged by refused calls");

    OK(zxv_paradox_registry_add(cap, b, s1, ZXV_PDX_KIND_DESTRUCTIVE, 42, ZXV_PDX_FLAG_MONEY, 1, &idx));
    CHECK(idx == 0, "first index 0");
    IS(zxv_paradox_registry_add(cap, b, s1, ZXV_PDX_KIND_NEUTRAL, 0, 0, 0, &idx), ZXV_E_DUPLICATE);
    OK(zxv_paradox_registry_add(cap, b, s2, ZXV_PDX_KIND_CONSTRUCTIVE, 7, 0, 0, NULL));
    IS(zxv_paradox_registry_add(cap, b, s3, ZXV_PDX_KIND_NEUTRAL, 0, 0, 0, &idx), ZXV_E_CAPACITY);
    OK(zxv_paradox_registry_count(b, &n));
    CHECK(n == 2, "two entries");
    OK(zxv_paradox_registry_find(b, s2, &idx));
    CHECK(idx == 1, "find s2 -> 1");
    IS(zxv_paradox_registry_find(b, s3, &idx), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_registry_find(b, NULL, &idx), ZXV_E_ARG);
    IS(zxv_paradox_registry_find(b, s3, NULL), ZXV_E_ARG);
    IS(zxv_paradox_registry_count(b, NULL), ZXV_E_ARG);
    IS(zxv_paradox_registry_count(NULL, &n), ZXV_E_ARG);
    int32_t k = -1;
    uint8_t reason = 0, flags = 0;
    uint64_t src = 0;
    OK(zxv_paradox_registry_at(b, 0, got, &k, &reason, &flags, &src));
    CHECK(!memcmp(got, s1, 32) && k == 1 && reason == 42 && flags == ZXV_PDX_FLAG_MONEY &&
              src == ZXV_PDX_SRC_SEED,
          "entry 0 fields");
    OK(zxv_paradox_registry_at(b, 1, NULL, NULL, NULL, NULL, NULL));
    IS(zxv_paradox_registry_at(b, 2, got, &k, &reason, &flags, &src), ZXV_E_NOT_FOUND);

    /* the registry add records */
    uint8_t r[200];
    size_t len = 0;
    IS(zxv_paradox_log_record(b, ZXV_PDX_LOG_OPS, 0, NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len == ZXV_PDX_REC_BYTES, "two-call length %zu", len);
    IS(zxv_paradox_log_record(b, ZXV_PDX_LOG_OPS, 0, r, ZXV_PDX_REC_BYTES - 1, &len), ZXV_E_BUFFER);
    IS(zxv_paradox_log_record(b, ZXV_PDX_LOG_OPS, 0, r, 200, NULL), ZXV_E_ARG);
    IS(zxv_paradox_log_record(b, 3, 0, r, 200, &len), ZXV_E_ARG);
    IS(zxv_paradox_log_record(b, -1, 0, r, 200, &len), ZXV_E_ARG);
    IS(zxv_paradox_log_record(b, ZXV_PDX_LOG_OPS, 2, r, 200, &len), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_log_record(b, ZXV_PDX_LOG_FRONTIER, 0, r, 200, &len), ZXV_E_NOT_FOUND);
    OK(zxv_paradox_log_record(b, ZXV_PDX_LOG_OPS, 1, r, 200, &len));
    CHECK(rd64(r) == 1 && r[8] == 6 && r[9] == ZXV_PDX_KIND_CONSTRUCTIVE &&
              r[11] == ZXV_PDX_OUT_ACCEPTED && rd32(r + 12) == 1 && zero32(r + 16) &&
              !memcmp(r + 48, s2, 32),
          "registry add record: op 6, accepted, aux = index, a = 0, b = sig");
    CHECK(chain_ok(b, ZXV_PDX_LOG_OPS), "operator chain recomputed");
    CHECK(chain_ok(b, ZXV_PDX_LOG_FRONTIER) && chain_ok(b, ZXV_PDX_LOG_QUARANTINE),
          "empty chains");
    uint8_t head[32];
    OK(zxv_paradox_log_head(b, ZXV_PDX_LOG_FRONTIER, head));
    CHECK(zero32(head), "empty head is zeros");
    IS(zxv_paradox_log_head(b, ZXV_PDX_LOG_OPS, NULL), ZXV_E_ARG);
    IS(zxv_paradox_log_head(b, 5, head), ZXV_E_ARG);
    IS(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, NULL), ZXV_E_ARG);
    IS(zxv_paradox_log_count(b, 7, &n), ZXV_E_ARG);
    OK(zxv_paradox_log_verify(b, ZXV_PDX_LOG_OPS));
    OK(zxv_paradox_log_verify(b, ZXV_PDX_LOG_FRONTIER));
    OK(zxv_paradox_log_verify(b, ZXV_PDX_LOG_QUARANTINE));
    IS(zxv_paradox_log_verify(b, 3), ZXV_E_ARG);
    IS(zxv_paradox_log_verify(NULL, 0), ZXV_E_ARG);

    /* classify with the registry: a signature overrides the trend */
    uint32_t grow[4] = {1, 1, 5, 1};
    int32_t ri = -5;
    uint8_t ot = 0;
    OK(zxv_paradox_classify(b, T(B_B, P_0, PH_MAN), grow, 0, s1, &k, &ot, &ri));
    CHECK(k == ZXV_PDX_KIND_DESTRUCTIVE && ri == 0 && ot == T(B_B, P_E, PH_MAN),
          "registry overrides a growing trend: destructive (k %d, ri %d, tag %u)", k, ri, ot);
    OK(zxv_paradox_classify(b, T(B_N, P_0, PH_POT), NULL, 0, s2, &k, &ot, &ri));
    CHECK(k == ZXV_PDX_KIND_CONSTRUCTIVE && ri == 1 && ot == T(B_N, P_G, PH_POT),
          "registry decides a Neither too");
    OK(zxv_paradox_classify(b, T(B_B, P_0, PH_POT), NULL, 0, s3, &k, &ot, &ri));
    CHECK(k == ZXV_PDX_KIND_SPECIAL && ri == -1, "unknown signature, no trend: special");
    OK(zxv_paradox_classify(b, T(B_T, P_0, PH_MAN), NULL, 0, s1, &k, &ot, &ri));
    CHECK(k == ZXV_PDX_KIND_NONE && ri == -1, "True is never a paradox, signature or not");
    OK(zxv_paradox_classify(b, T(B_B, P_X, PH_MAN), NULL, 0, s1, &k, &ot, &ri));
    CHECK(tp(ot) == P_X, "a forbidden tag stays forbidden");
    IS(zxv_paradox_classify((const zxv_paradox *) cap, TAG_CONS, NULL, 0, NULL, &k, NULL, NULL),
       ZXV_E_ARG);
    zxv_paradox_destroy(b);
}

/* ===================================================================== */
static void test_constructive(void)
{
    zxv_paradox *b;
    OK(zxv_paradox_create(cap, 64, 4, &b));
    const int64_t lo[3] = {0, -100, 10}, hi[3] = {100, 100, 20}, init[3] = {50, 0, 15};
    zxv_paradox_policy *p = NULL, *pm = NULL;
    IS(zxv_paradox_policy_create(nocap, 3, lo, hi, init, 0, &p), ZXV_E_CAP);
    IS(zxv_paradox_policy_create(cap, 0, lo, hi, init, 0, &p), ZXV_E_ARG);
    IS(zxv_paradox_policy_create(cap, ZXV_PDX_POLICY_MAX + 1, lo, hi, init, 0, &p), ZXV_E_ARG);
    IS(zxv_paradox_policy_create(cap, 3, NULL, hi, init, 0, &p), ZXV_E_ARG);
    IS(zxv_paradox_policy_create(cap, 3, lo, NULL, init, 0, &p), ZXV_E_ARG);
    IS(zxv_paradox_policy_create(cap, 3, lo, hi, NULL, 0, &p), ZXV_E_ARG);
    IS(zxv_paradox_policy_create(cap, 3, lo, hi, init, 0, NULL), ZXV_E_ARG);
    IS(zxv_paradox_policy_create(cap, 3, lo, hi, init, 8, &p), ZXV_E_ARG);
    const int64_t bad_init[3] = {50, 0, 21}, bad_lo[3] = {0, 101, 10};
    IS(zxv_paradox_policy_create(cap, 3, lo, hi, bad_init, 0, &p), ZXV_E_RANGE);
    IS(zxv_paradox_policy_create(cap, 3, bad_lo, hi, init, 0, &p), ZXV_E_RANGE);
    CHECK(p == NULL, "no policy on failure");
    OK(zxv_paradox_policy_create(cap, 3, lo, hi, init, 0, &p));
    OK(zxv_paradox_policy_create(cap, 3, lo, hi, init, ZXV_PDX_FLAG_MONEY, &pm));
    zxv_paradox_policy_destroy(NULL);

    int64_t v[3], pv[3], blo[3], bhi[3];
    int32_t armed = -1, o = -1;
    uint32_t ta = 9, fl = 9;
    OK(zxv_paradox_policy_bounds(p, blo, bhi, 3, &fl));
    CHECK(!memcmp(blo, lo, sizeof lo) && !memcmp(bhi, hi, sizeof hi) && fl == 0, "bounds");
    IS(zxv_paradox_policy_bounds(p, blo, NULL, 2, NULL), ZXV_E_ARG);
    OK(zxv_paradox_policy_bounds(p, NULL, NULL, 0, NULL));
    IS(zxv_paradox_policy_values(p, v, NULL, 4, NULL, NULL), ZXV_E_ARG);
    IS(zxv_paradox_policy_values(NULL, v, NULL, 3, NULL, NULL), ZXV_E_ARG);

    const int64_t s_in[3] = {60, -10, 12}, s_out[3] = {60, -10, 21}, s_in2[3] = {70, 5, 20};
    /* argument errors */
    IS(zxv_paradox_constructive(nocap, b, p, 0, TAG_CONS, s_in, 3, 5, &o), ZXV_E_CAP);
    IS(zxv_paradox_constructive(foreign, b, p, 0, TAG_CONS, s_in, 3, 5, &o), ZXV_E_CAP);
    IS(zxv_paradox_constructive(cap, NULL, p, 0, TAG_CONS, s_in, 3, 5, &o), ZXV_E_ARG);
    IS(zxv_paradox_constructive(cap, b, NULL, 0, TAG_CONS, s_in, 3, 5, &o), ZXV_E_ARG);
    IS(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_in, 3, 5, NULL), ZXV_E_ARG);
    IS(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_in, 2, 5, &o), ZXV_E_ARG);
    IS(zxv_paradox_constructive(cap, b, p, 0, TAG_DEST, s_in, 3, 5, &o), ZXV_E_ARG);
    IS(zxv_paradox_constructive(cap, b, p, 5, TAG_CONS, s_in, 3, 5, &o), ZXV_E_ARG);
    CHECK(o == -1, "outcome untouched on failure");
    uint32_t n0;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &n0));
    CHECK(n0 == 0, "nothing logged");

    /* out of bounds: proposal for the owner, nothing changes */
    OK(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_out, 3, 5, &o));
    CHECK(o == ZXV_PDX_OUT_PROPOSAL_FOR_OWNER, "out of bounds -> proposal (%d)", o);
    OK(zxv_paradox_policy_values(p, v, pv, 3, &armed, &ta));
    CHECK(!memcmp(v, init, sizeof v) && armed == 0, "no change on a proposal");
    uint8_t r[200], hv[32], hs[32];
    rec_bytes(b, ZXV_PDX_LOG_OPS, 0, r);
    params_hash(init, 3, hv);
    params_hash(s_out, 3, hs);
    CHECK(r[8] == 1 && r[9] == 0 && r[10] == TAG_CONS && r[11] == ZXV_PDX_OUT_PROPOSAL_FOR_OWNER &&
              rd32(r + 12) == 3 && !memcmp(r + 16, hv, 32) && !memcmp(r + 48, hs, 32),
          "proposal record: a = live values hash, b = proposed hash");

    /* the proposal as data */
    uint8_t prop[12 + 32 * 3];
    size_t plen = 0;
    IS(zxv_paradox_policy_proposal(p, s_out, 3, NULL, 0, &plen), ZXV_E_BUFFER);
    CHECK(plen == ZXV_PDX_PROPOSAL_BYTES(3) && plen == 108, "proposal size %zu", plen);
    IS(zxv_paradox_policy_proposal(p, s_out, 3, prop, plen - 1, &plen), ZXV_E_BUFFER);
    IS(zxv_paradox_policy_proposal(p, NULL, 3, prop, sizeof prop, &plen), ZXV_E_ARG);
    IS(zxv_paradox_policy_proposal(p, s_out, 2, prop, sizeof prop, &plen), ZXV_E_ARG);
    IS(zxv_paradox_policy_proposal(p, s_out, 3, prop, sizeof prop, NULL), ZXV_E_ARG);
    OK(zxv_paradox_policy_proposal(p, s_out, 3, prop, sizeof prop, &plen));
    int ok = rd32(prop) == 3 && rd32(prop + 4) == 1 && rd32(prop + 8) == 0;
    for (int i = 0; i < 3; i++)
        ok = ok && (int64_t) rd64(prop + 12 + 32 * i) == lo[i] &&
             (int64_t) rd64(prop + 20 + 32 * i) == hi[i] &&
             (int64_t) rd64(prop + 28 + 32 * i) == init[i] &&
             (int64_t) rd64(prop + 36 + 32 * i) == s_out[i];
    CHECK(ok, "proposal bytes: n, out-of-bounds count, flags, lo/hi/current/proposed");

    /* in bounds: applied and armed */
    OK(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_in, 3, 5, &o));
    CHECK(o == ZXV_PDX_OUT_APPLIED, "in bounds -> applied (%d)", o);
    OK(zxv_paradox_policy_values(p, v, pv, 3, &armed, &ta));
    CHECK(!memcmp(v, s_in, sizeof v) && !memcmp(pv, init, sizeof pv) && armed == 1 && ta == 5,
          "applied: val = synth, prev = old, armed at tension 5");
    /* evaluation with tension not grown: kept */
    OK(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, NULL, 0, 5, &o));
    CHECK(o == ZXV_PDX_OUT_KEPT, "same tension -> kept (%d)", o);
    OK(zxv_paradox_policy_values(p, v, NULL, 3, &armed, NULL));
    CHECK(!memcmp(v, s_in, sizeof v) && armed == 0, "kept and disarmed");
    /* apply again, then tension grows: rolled back */
    OK(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_in2, 3, 4, &o));
    CHECK(o == ZXV_PDX_OUT_APPLIED, "second apply");
    OK(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_in, 3, 5, &o));
    CHECK(o == ZXV_PDX_OUT_ROLLED_BACK, "tension grew -> rolled back (%d)", o);
    OK(zxv_paradox_policy_values(p, v, NULL, 3, &armed, NULL));
    CHECK(!memcmp(v, s_in, sizeof v) && armed == 0, "previous values restored");
    /* forbidden: an armed update is restored */
    OK(zxv_paradox_constructive(cap, b, p, 0, TAG_CONS, s_in2, 3, 4, &o));
    OK(zxv_paradox_constructive(cap, b, p, ZXV_PDX_KIND_NEUTRAL, TAG_FORB, s_in, 3, 0, &o));
    CHECK(o == ZXV_PDX_OUT_ROLLED_BACK, "forbidden -> rolled back");
    OK(zxv_paradox_policy_values(p, v, NULL, 3, &armed, NULL));
    CHECK(!memcmp(v, s_in, sizeof v) && armed == 0, "forbidden restores the armed update");
    /* wrong kind: refused, logged, nothing changes */
    OK(zxv_paradox_constructive(cap, b, p, ZXV_PDX_KIND_NEUTRAL, TAG_NEUT, s_in2, 3, 0, &o));
    CHECK(o == ZXV_PDX_OUT_REFUSED, "neutral pair -> refused");
    OK(zxv_paradox_policy_values(p, v, NULL, 3, NULL, NULL));
    CHECK(!memcmp(v, s_in, sizeof v), "refused changes nothing");

    /* a money policy: never automatic, even in bounds */
    OK(zxv_paradox_constructive(cap, b, pm, 0, TAG_CONS, s_in, 3, 0, &o));
    CHECK(o == ZXV_PDX_OUT_PROPOSAL_FOR_OWNER, "money policy -> proposal (%d)", o);
    OK(zxv_paradox_policy_values(pm, v, NULL, 3, &armed, NULL));
    CHECK(!memcmp(v, init, sizeof v) && armed == 0, "money policy unchanged");
    uint32_t nops;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    rec_bytes(b, ZXV_PDX_LOG_OPS, nops - 1, r);
    params_hash(init, 3, hv);
    params_hash(s_in, 3, hs);
    CHECK(r[11] == ZXV_PDX_OUT_PROPOSAL_FOR_OWNER && !memcmp(r + 16, hv, 32) &&
              !memcmp(r + 48, hs, 32),
          "money proposal record has the real hashes");
    OK(zxv_paradox_policy_bounds(pm, NULL, NULL, 0, &fl));
    CHECK(fl == ZXV_PDX_FLAG_MONEY, "flags kept");
    OK(zxv_paradox_policy_proposal(pm, s_in, 3, prop, sizeof prop, &plen));
    CHECK(rd32(prop + 4) == 0 && rd32(prop + 8) == ZXV_PDX_FLAG_MONEY, "money proposal bytes");
    OK(zxv_paradox_constructive(cap, b, pm, 0, TAG_CONS, NULL, 0, 0, &o));
    CHECK(o == ZXV_PDX_OUT_KEPT, "money policy evaluation only -> kept");
    CHECK(chain_ok(b, ZXV_PDX_LOG_OPS), "operator chain after constructive");

    /* a handle from another context */
    zxv_paradox_policy *pf;
    OK(zxv_paradox_policy_create(foreign, 3, lo, hi, init, 0, &pf));
    IS(zxv_paradox_constructive(cap, b, pf, 0, TAG_CONS, s_in, 3, 0, &o), ZXV_E_ARG);
    zxv_paradox_policy_destroy(pf);

    /* a full operator log: nothing changes */
    zxv_paradox *tiny;
    OK(zxv_paradox_create(cap, 1, 1, &tiny));
    OK(zxv_paradox_constructive(cap, tiny, p, 0, TAG_CONS, s_in2, 3, 0, &o));
    CHECK(o == ZXV_PDX_OUT_APPLIED, "tiny: applied");
    OK(zxv_paradox_policy_values(p, v, NULL, 3, NULL, NULL));
    IS(zxv_paradox_constructive(cap, tiny, p, 0, TAG_CONS, s_in, 3, 9, &o), ZXV_E_CAPACITY);
    int64_t v2[3];
    OK(zxv_paradox_policy_values(p, v2, NULL, 3, &armed, NULL));
    CHECK(!memcmp(v, v2, sizeof v) && armed == 1, "log full: no rollback either");
    zxv_paradox_destroy(tiny);

    zxv_paradox_policy_destroy(p);
    zxv_paradox_policy_destroy(pm);
    zxv_paradox_destroy(b);
}

/* ===================================================================== */
static void test_destructive(void)
{
    zxv_paradox *b;
    OK(zxv_paradox_create(cap, 16, 4, &b));
    uint8_t state[8] = {9, 9, 9, 9, 9, 9, 9, 9}, ckpt[8] = {1, 2, 3, 4, 5, 6, 7, 8}, found[8];
    memcpy(found, state, 8);
    const uint64_t trace[4] = {11, 22, 33, 22};
    const uint8_t pat[] = "spin-loop-at-0x40";
    uint8_t proof[160];
    memset(proof, 0xEE, sizeof proof);
    int32_t o = -1;

    IS(zxv_paradox_destructive(nocap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, state, ckpt, 8, pat, 17, 5, &o, proof), ZXV_E_CAP);
    IS(zxv_paradox_destructive(cap, NULL, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, state, ckpt, 8, pat, 17, 5, &o, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, NULL, ckpt, 8, pat, 17, 5, &o, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, state, NULL, 8, pat, 17, 5, &o, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, state, ckpt, 8, NULL, 17, 5, &o, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, NULL, 4, 1, state, ckpt, 8, pat, 17, 5, &o, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, state, ckpt, 8, pat, 17, 5, NULL, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, 4, 1, state, ckpt, 8, pat, 17, 5, &o, NULL), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_CONS, 0, 0, 0, 0, trace, 4, 1, state, ckpt, 8, pat, 17, 5, &o, proof), ZXV_E_ARG);
    IS(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 0, 0, 0, 0, trace, ZXV_PDX_TRACE_MAX + 1, 1, state, ckpt, 8, pat, 17, 5, &o, proof), ZXV_E_RANGE);
    CHECK(!memcmp(state, found, 8) && o == -1, "argument errors change nothing");

    /* no check trips and not destructive: refused, logged, untouched */
    const uint64_t calm[3] = {1, 2, 3};
    OK(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 3, 10, 4, 10, calm, 3, 1, state, ckpt, 8, pat, 17, 5, &o, proof));
    CHECK(o == ZXV_PDX_OUT_REFUSED && !memcmp(state, found, 8) && proof[0] == 0xEE,
          "nothing tripped -> refused, state and proof untouched");
    uint32_t nq = 9;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_QUARANTINE, &nq));
    CHECK(nq == 0, "nothing quarantined");

    /* a cycle in a neutral paradox: contained */
    OK(zxv_paradox_destructive(cap, b, 2, TAG_NEUT, 3, 10, 4, 10, trace, 4, 1, state, ckpt, 8, pat, 17, 77, &o, proof));
    CHECK(o == ZXV_PDX_OUT_CONTAINED, "cycle -> contained (%d)", o);
    CHECK(!memcmp(state, ckpt, 8), "state restored from the checkpoint");
    uint8_t hb[32], ha[32], sg[32];
    state_hash(found, 8, hb);
    state_hash(ckpt, 8, ha);
    dsha("zxv/pdx-sig/v1", pat, 17, sg);
    CHECK(rd32(proof) == ZXV_PDX_TRIP_CYCLE && rd32(proof + 4) == 3 && rd32(proof + 8) == 4 &&
              rd32(proof + 12) == 4 && rd32(proof + 16) == 77 && rd32(proof + 20) == 8,
          "proof scalars");
    CHECK(!memcmp(proof + 24, hb, 32) && !memcmp(proof + 56, ha, 32) && !memcmp(proof + 88, sg, 32),
          "proof before / after / signature");
    uint8_t what[32];
    dsha("zxv/pdx-repair/v1", proof, 120, what);
    CHECK(!memcmp(proof + 120, what, 32) && rd64(proof + 152) == 0, "proof hash and qseq");
    OK(zxv_paradox_repair_verify(proof));
    uint8_t q[200];
    OK(rec_bytes(b, ZXV_PDX_LOG_QUARANTINE, 0, q));
    CHECK(rd64(q) == 0 && q[8] == T(B_B, P_X, PH_MAN) && q[9] == ZXV_PDX_TRIP_CYCLE &&
              rd32(q + 10) == 77 && !memcmp(q + 14, what, 32),
          "quarantine record: forbidden tag, trips, submitter, proof hash");
    size_t qlen = 0;
    IS(zxv_paradox_log_record(b, ZXV_PDX_LOG_QUARANTINE, 0, q, ZXV_PDX_QREC_BYTES - 1, &qlen), ZXV_E_BUFFER);
    CHECK(qlen == ZXV_PDX_QREC_BYTES, "quarantine record size");
    uint8_t r[200];
    uint32_t nops;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    rec_bytes(b, ZXV_PDX_LOG_OPS, nops - 1, r);
    CHECK(r[8] == 2 && r[11] == ZXV_PDX_OUT_CONTAINED && rd32(r + 12) == 3 &&
              !memcmp(r + 16, what, 32) && !memcmp(r + 48, sg, 32),
          "operator record: aux steps, a = proof hash, b = signature");

    /* each check on its own */
    struct {
        uint32_t steps, limit, spent, budget, tn;
        int32_t inv;
        int kind;
        uint8_t tag;
        uint32_t want;
    } cases[] = {
        {10, 10, 0, 0, 0, 1, 2, TAG_NEUT, ZXV_PDX_TRIP_STEPS},
        {9, 10, 0, 0, 0, 1, 2, TAG_NEUT, 0},
        {0, 0, 5, 5, 0, 1, 2, TAG_NEUT, ZXV_PDX_TRIP_BUDGET},
        {0, 0, 4, 5, 0, 1, 2, TAG_NEUT, 0},
        {0, 0, 0, 0, 0, 0, 2, TAG_NEUT, ZXV_PDX_TRIP_INVARIANT},
        {0, 0, 0, 0, 0, 1, 1, TAG_FORB, ZXV_PDX_TRIP_FORBIDDEN},
        {0, 0, 0, 0, 0, 1, 1, TAG_DEST, 0}, /* destructive kind: contained, no trips */
        {7, 7, 9, 9, 4, 0, 4, TAG_NONE, 1 | 2 | 4 | 8},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memcpy(state, found, 8);
        o = -1;
        OK(zxv_paradox_destructive(cap, b, cases[i].kind, cases[i].tag, cases[i].steps, cases[i].limit,
                                   cases[i].spent, cases[i].budget, trace, cases[i].tn, cases[i].inv,
                                   state, ckpt, 8, NULL, 0, 1, &o, proof));
        int contained = cases[i].want != 0 || cases[i].kind == 1;
        CHECK(o == (contained ? ZXV_PDX_OUT_CONTAINED : ZXV_PDX_OUT_REFUSED), "case %u outcome %d", i, o);
        if (contained)
            CHECK(rd32(proof) == cases[i].want && !memcmp(state, ckpt, 8),
                  "case %u trips %u want %u", i, rd32(proof), cases[i].want);
        else
            CHECK(!memcmp(state, found, 8), "case %u untouched", i);
    }
    /* zero-length state */
    OK(zxv_paradox_destructive(cap, b, 1, TAG_DEST, 0, 0, 0, 0, NULL, 0, 1, NULL, NULL, 0, NULL, 0, 2, &o, proof));
    CHECK(o == ZXV_PDX_OUT_CONTAINED && rd32(proof + 20) == 0, "empty state contained");
    CHECK(chain_ok(b, ZXV_PDX_LOG_QUARANTINE) && chain_ok(b, ZXV_PDX_LOG_OPS), "chains after destructive");
    OK(zxv_paradox_log_verify(b, ZXV_PDX_LOG_QUARANTINE));
    /* a peer learns the trap from the proof's signature */
    OK(zxv_paradox_registry_add(cap, b, sg, ZXV_PDX_KIND_DESTRUCTIVE, 0, 0, 0, NULL));
    int32_t k = -1;
    OK(zxv_paradox_classify(b, T(B_B, P_0, PH_POT), NULL, 0, sg, &k, NULL, NULL));
    CHECK(k == ZXV_PDX_KIND_DESTRUCTIVE, "the shared signature is recognised");
    zxv_paradox_destroy(b);

    /* full logs: the rollback still happens, then CAPACITY */
    zxv_paradox *tiny;
    OK(zxv_paradox_create(cap, 1, 1, &tiny));
    memcpy(state, found, 8);
    OK(zxv_paradox_destructive(cap, tiny, 1, TAG_DEST, 0, 0, 0, 0, NULL, 0, 1, state, ckpt, 8, NULL, 0, 1, &o, proof));
    memcpy(state, found, 8);
    memset(proof, 0xEE, sizeof proof);
    o = -1;
    IS(zxv_paradox_destructive(cap, tiny, 1, TAG_DEST, 0, 0, 0, 0, NULL, 0, 1, state, ckpt, 8, NULL, 0, 1, &o, proof), ZXV_E_CAPACITY);
    CHECK(!memcmp(state, ckpt, 8), "log full: the state is still rolled back");
    CHECK(o == -1 && proof[0] == 0xEE, "log full: outcome and proof not written");
    OK(zxv_paradox_log_count(tiny, ZXV_PDX_LOG_QUARANTINE, &nq));
    CHECK(nq == 1, "nothing more quarantined");
    zxv_paradox_destroy(tiny);
}

/* ===================================================================== */
static void test_neutral(void)
{
    zxv_paradox *b;
    OK(zxv_paradox_create(cap, 16, 4, &b));
    zxv_paradox_hold *h = NULL, *h2, *hf;
    IS(zxv_paradox_hold_create(nocap, 3, 2, &h), ZXV_E_CAP);
    IS(zxv_paradox_hold_create(cap, 0, 2, &h), ZXV_E_ARG);
    IS(zxv_paradox_hold_create(cap, ZXV_PDX_HOLD_MAX + 1, 2, &h), ZXV_E_ARG);
    IS(zxv_paradox_hold_create(cap, 3, 2, NULL), ZXV_E_ARG);
    OK(zxv_paradox_hold_create(cap, 2, 2, &h));
    zxv_paradox_hold_destroy(NULL);
    uint8_t a[32], c[32], d[32], got[32];
    id_of(100, a);
    id_of(101, c);
    id_of(102, d);
    OK(zxv_paradox_hold_add(h, a));
    OK(zxv_paradox_hold_add(h, a)); /* the same candidate */
    OK(zxv_paradox_hold_add(h, c));
    IS(zxv_paradox_hold_add(h, d), ZXV_E_CAPACITY);
    IS(zxv_paradox_hold_add(h, NULL), ZXV_E_ARG);
    IS(zxv_paradox_hold_add(NULL, d), ZXV_E_ARG);
    IS(zxv_paradox_hold_evidence(h, d, 1), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_hold_evidence(h, NULL, 1), ZXV_E_ARG);
    uint32_t cnt = 0, pos = 9, neg = 9;
    int32_t settled = 7, dec = 7, o = -1;
    OK(zxv_paradox_hold_info(h, &cnt, &settled, &dec));
    CHECK(cnt == 2 && settled == -1 && dec == -1, "two branches, deferred");
    OK(zxv_paradox_hold_info(h, NULL, NULL, NULL));

    OK(zxv_paradox_neutral(cap, b, h, 2, TAG_NEUT, -1, &o, &settled));
    CHECK(o == ZXV_PDX_OUT_DEFERRED && settled == -1, "no evidence -> deferred");
    OK(zxv_paradox_hold_evidence(h, a, 1));
    OK(zxv_paradox_hold_evidence(h, c, 0));
    OK(zxv_paradox_hold_info(h, NULL, NULL, &dec));
    CHECK(dec == 0, "a leads by 2 = decisive");
    OK(zxv_paradox_hold_branch(h, 1, got, &pos, &neg));
    CHECK(!memcmp(got, c, 32) && pos == 0 && neg == 1, "branch 1");
    IS(zxv_paradox_hold_branch(h, 2, got, &pos, &neg), ZXV_E_NOT_FOUND);
    OK(zxv_paradox_hold_branch(h, 0, NULL, NULL, NULL));

    IS(zxv_paradox_neutral(nocap, b, h, 2, TAG_NEUT, -1, &o, &settled), ZXV_E_CAP);
    IS(zxv_paradox_neutral(cap, b, h, 2, TAG_NEUT, -2, &o, &settled), ZXV_E_ARG);
    IS(zxv_paradox_neutral(cap, b, h, 2, TAG_NEUT, 2, &o, &settled), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_neutral(cap, b, NULL, 2, TAG_NEUT, -1, &o, &settled), ZXV_E_ARG);
    IS(zxv_paradox_neutral(cap, b, h, 2, TAG_NEUT, -1, NULL, &settled), ZXV_E_ARG);
    IS(zxv_paradox_neutral(cap, b, h, 2, TAG_CONS, -1, &o, &settled), ZXV_E_ARG);
    /* forbidden and wrong kind settle nothing */
    OK(zxv_paradox_neutral(cap, b, h, 0, TAG_FORB, 0, &o, &settled));
    CHECK(o == ZXV_PDX_OUT_ROLLED_BACK && settled == -1, "forbidden -> rolled back, nothing settles");
    OK(zxv_paradox_neutral(cap, b, h, 0, TAG_CONS, 0, &o, &settled));
    CHECK(o == ZXV_PDX_OUT_REFUSED && settled == -1, "constructive pair -> refused");
    OK(zxv_paradox_neutral(cap, b, h, 2, TAG_NEUT, -1, &o, NULL));
    CHECK(o == ZXV_PDX_OUT_SETTLED, "decisive evidence -> settled");
    OK(zxv_paradox_hold_info(h, NULL, &settled, NULL));
    CHECK(settled == 0, "settled on a");
    uint8_t r[200];
    uint32_t nops;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    rec_bytes(b, ZXV_PDX_LOG_OPS, nops - 1, r);
    CHECK(r[8] == 3 && r[11] == ZXV_PDX_OUT_SETTLED && rd32(r + 12) == 0 && !memcmp(r + 48, a, 32),
          "neutral record: aux settled index, b = settled id");
    IS(zxv_paradox_hold_add(h, d), ZXV_E_STATE);
    IS(zxv_paradox_hold_evidence(h, a, 1), ZXV_E_STATE);

    /* the owner picks */
    OK(zxv_paradox_hold_create(cap, 3, 0, &h2));
    OK(zxv_paradox_hold_add(h2, a));
    OK(zxv_paradox_hold_add(h2, c));
    OK(zxv_paradox_neutral(cap, b, h2, 2, TAG_NEUT, 1, &o, &settled));
    CHECK(o == ZXV_PDX_OUT_SETTLED && settled == 1, "owner pick settles branch 1");
    /* Neither is neutral too */
    OK(zxv_paradox_hold_create(foreign, 3, 0, &hf));
    IS(zxv_paradox_neutral(cap, b, hf, 2, T(B_N, P_0, PH_POT), -1, &o, &settled), ZXV_E_ARG);
    zxv_paradox_hold_destroy(hf);
    CHECK(chain_ok(b, ZXV_PDX_LOG_OPS), "chain after neutral");
    zxv_paradox_hold_destroy(h);
    zxv_paradox_hold_destroy(h2);
    zxv_paradox_destroy(b);
}

/* ===================================================================== */
static void test_special(void)
{
    zxv_paradox *b, *peer;
    OK(zxv_paradox_create(cap, 32, 4, &b));
    OK(zxv_paradox_create(cap, 32, 4, &peer));
    uint8_t sig[32], sig2[32], subj[32];
    zxv_paradox_signature((const uint8_t *) "new-anomaly", 11, sig);
    zxv_paradox_signature((const uint8_t *) "money-anomaly", 13, sig2);
    id_of(7, subj);
    int32_t o = -1;
    uint32_t rec = 5;
    IS(zxv_paradox_special(nocap, b, 3, TAG_SPEC, sig, subj, &o, &rec), ZXV_E_CAP);
    IS(zxv_paradox_special(cap, b, 3, TAG_SPEC, NULL, subj, &o, &rec), ZXV_E_ARG);
    IS(zxv_paradox_special(cap, b, 3, TAG_SPEC, sig, NULL, &o, &rec), ZXV_E_ARG);
    IS(zxv_paradox_special(cap, b, 3, TAG_SPEC, sig, subj, NULL, &rec), ZXV_E_ARG);
    IS(zxv_paradox_special(cap, b, 3, TAG_NEUT, sig, subj, &o, &rec), ZXV_E_ARG);
    OK(zxv_paradox_special(cap, b, 2, TAG_NEUT, sig, subj, &o, &rec));
    CHECK(o == ZXV_PDX_OUT_REFUSED && rec == UINT32_MAX, "neutral -> refused");
    OK(zxv_paradox_special(cap, b, 3, TAG_FORB, sig, subj, &o, &rec));
    CHECK(o == ZXV_PDX_OUT_ROLLED_BACK && rec == UINT32_MAX, "forbidden -> rolled back");
    OK(zxv_paradox_special(cap, b, 3, TAG_SPEC, sig, subj, &o, &rec));
    CHECK(o == ZXV_PDX_OUT_RECORDED && rec == 0, "recorded at frontier 0");
    OK(zxv_paradox_special(cap, b, 3, TAG_SPEC, sig2, subj, &o, NULL));
    uint8_t fr[200], r[200];
    OK(rec_bytes(b, ZXV_PDX_LOG_FRONTIER, 0, fr));
    CHECK(fr[8] == 4 && fr[9] == 3 && fr[10] == TAG_SPEC && fr[11] == ZXV_PDX_OUT_RECORDED &&
              !memcmp(fr + 16, sig, 32) && !memcmp(fr + 48, subj, 32),
          "frontier record: a = sig, b = subject");
    uint32_t nops;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    rec_bytes(b, ZXV_PDX_LOG_OPS, 2, r);
    CHECK(r[8] == 4 && rd32(r + 12) == 0 && !memcmp(r + 48, fr + 112, 32),
          "operator record points at the frontier record");
    CHECK(chain_ok(b, ZXV_PDX_LOG_FRONTIER), "frontier chain");

    /* drafts */
    uint8_t dr[71], dm[71];
    IS(zxv_paradox_draft(nocap, b, 0, 0, 1, 0, dr), ZXV_E_CAP);
    IS(zxv_paradox_draft(cap, b, 0, 3, 1, 0, dr), ZXV_E_ARG);
    IS(zxv_paradox_draft(cap, b, 0, 4, 1, 0, dr), ZXV_E_ARG);
    IS(zxv_paradox_draft(cap, b, 0, 0, 1, 4, dr), ZXV_E_ARG);
    IS(zxv_paradox_draft(cap, b, 0, 0, 1, 0, NULL), ZXV_E_ARG);
    IS(zxv_paradox_draft(cap, b, 2, 0, 1, 0, dr), ZXV_E_NOT_FOUND);
    OK(zxv_paradox_draft(cap, b, 0, ZXV_PDX_KIND_CONSTRUCTIVE, 9, 0, dr));
    CHECK(rd32(dr) == 0 && !memcmp(dr + 4, fr + 112, 32) && !memcmp(dr + 36, sig, 32) &&
              dr[68] == 0 && dr[69] == 9 && dr[70] == 0,
          "draft bytes");
    OK(zxv_paradox_draft(cap, b, 1, ZXV_PDX_KIND_DESTRUCTIVE, 3, ZXV_PDX_FLAG_MONEY, dm));

    /* review */
    IS(zxv_paradox_review(nocap, b, dr, 0, 1, &o), ZXV_E_CAP);
    IS(zxv_paradox_review(cap, b, NULL, 0, 1, &o), ZXV_E_ARG);
    IS(zxv_paradox_review(cap, b, dr, 0, 1, NULL), ZXV_E_ARG);
    uint8_t bad[71];
    memcpy(bad, dr, 71);
    bad[68] = 3;
    IS(zxv_paradox_review(cap, b, bad, 1, 1, &o), ZXV_E_FORMAT);
    memcpy(bad, dr, 71);
    bad[70] = 0x10;
    IS(zxv_paradox_review(cap, b, bad, 1, 1, &o), ZXV_E_FORMAT);
    memcpy(bad, dr, 71);
    bad[0] = 9;
    IS(zxv_paradox_review(cap, b, bad, 1, 1, &o), ZXV_E_NOT_FOUND);
    memcpy(bad, dr, 71);
    bad[10] ^= 1;
    IS(zxv_paradox_review(cap, b, bad, 1, 1, &o), ZXV_E_VERIFY);
    memcpy(bad, dr, 71);
    bad[40] ^= 1;
    IS(zxv_paradox_review(cap, b, bad, 1, 1, &o), ZXV_E_VERIFY);
    IS(zxv_paradox_review(cap, peer, dr, 1, 1, &o), ZXV_E_NOT_FOUND); /* peer has no record 0 */
    uint32_t nreg = 9;
    OK(zxv_paradox_registry_count(b, &nreg));
    CHECK(nreg == 0, "nothing registered by refused reviews");

    OK(zxv_paradox_review(cap, b, dr, 0, 0, &o));
    CHECK(o == ZXV_PDX_OUT_PROPOSAL_FOR_OWNER, "no approval -> proposal");
    OK(zxv_paradox_review(cap, b, dm, 0, 1, &o));
    CHECK(o == ZXV_PDX_OUT_PROPOSAL_FOR_OWNER, "money draft: consensus is not enough");
    OK(zxv_paradox_review(cap, b, dr, 0, 1, &o));
    CHECK(o == ZXV_PDX_OUT_ACCEPTED, "consensus accepts an ordinary draft");
    OK(zxv_paradox_review(cap, b, dr, 1, 1, &o));
    CHECK(o == ZXV_PDX_OUT_REFUSED, "already registered -> refused");
    OK(zxv_paradox_review(cap, b, dm, 1, 0, &o));
    CHECK(o == ZXV_PDX_OUT_ACCEPTED, "the owner accepts the money draft");
    OK(zxv_paradox_registry_count(b, &nreg));
    CHECK(nreg == 2, "two learned entries");
    int32_t k;
    uint8_t fl;
    uint64_t src;
    OK(zxv_paradox_registry_at(b, 1, NULL, &k, NULL, &fl, &src));
    CHECK(k == ZXV_PDX_KIND_DESTRUCTIVE && fl == ZXV_PDX_FLAG_MONEY && src == 1,
          "learned entry: src = frontier record");
    OK(zxv_paradox_classify(b, TAG_SPEC, NULL, 0, sig, &k, NULL, NULL));
    CHECK(k == ZXV_PDX_KIND_CONSTRUCTIVE, "the next case like it is recognised");
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    rec_bytes(b, ZXV_PDX_LOG_OPS, nops - 1, r);
    CHECK(r[8] == 6 && r[11] == ZXV_PDX_OUT_ACCEPTED && !memcmp(r + 16, dm + 4, 32) &&
              !memcmp(r + 48, sig2, 32),
          "review record");
    /* a full registry refuses (logged) */
    zxv_paradox *one;
    OK(zxv_paradox_create(cap, 32, 1, &one));
    OK(zxv_paradox_special(cap, one, 3, TAG_SPEC, sig, subj, &o, &rec));
    OK(zxv_paradox_special(cap, one, 3, TAG_SPEC, sig2, subj, &o, &rec));
    OK(zxv_paradox_draft(cap, one, 0, 0, 0, 0, dr));
    OK(zxv_paradox_draft(cap, one, 1, 0, 0, 0, dm));
    OK(zxv_paradox_review(cap, one, dr, 1, 0, &o));
    OK(zxv_paradox_review(cap, one, dm, 1, 0, &o));
    CHECK(o == ZXV_PDX_OUT_REFUSED, "full registry -> refused");
    zxv_paradox_destroy(one);
    /* a full frontier / log */
    zxv_paradox *tiny;
    OK(zxv_paradox_create(cap, 1, 1, &tiny));
    OK(zxv_paradox_special(cap, tiny, 3, TAG_SPEC, sig, subj, &o, &rec));
    IS(zxv_paradox_special(cap, tiny, 3, TAG_SPEC, sig2, subj, &o, &rec), ZXV_E_CAPACITY);
    IS(zxv_paradox_draft(cap, tiny, 0, 0, 0, 0, dr), ZXV_E_CAPACITY);
    zxv_paradox_destroy(tiny);
    CHECK(chain_ok(b, ZXV_PDX_LOG_OPS) && chain_ok(b, ZXV_PDX_LOG_FRONTIER), "chains after special");
    zxv_paradox_destroy(b);
    zxv_paradox_destroy(peer);
}

/* ===================================================================== */
/* graphs                                                                */
/* ===================================================================== */

enum { G_D, G_T, G_S, G_W, G_N, G_C1, G_C2, G_X, G_COUNT };

static void add_scenario(zxv_paradox_graph *g, const int *order)
{
    static const int kinds[G_COUNT] = {1, 0, 3, 0, 2, 0, 0, 4};
    const uint8_t tags[G_COUNT] = {TAG_DEST, TAG_CONS, TAG_SPEC, TAG_CONS,
                                   TAG_NEUT, TAG_CONS, TAG_CONS, TAG_NONE};
    uint8_t id[32];
    for (int i = 0; i < G_COUNT; i++) {
        int n = order ? order[i] : i;
        id_of((uint32_t) n, id);
        OK(zxv_paradox_graph_add_node(g, id, kinds[n], tags[n], (uint32_t) (n + 1), n % 3 - 1));
    }
}
static void add_scenario_deps(zxv_paradox_graph *g, int flip)
{
    static const int e[3][2] = {{G_T, G_D}, {G_W, G_S}, {G_C1, G_N}};
    uint8_t a[32], c[32];
    for (int i = 0; i < 3; i++) {
        int k = flip ? 2 - i : i;
        id_of((uint32_t) e[k][0], a);
        id_of((uint32_t) e[k][1], c);
        OK(zxv_paradox_graph_add_dep(g, a, c));
    }
}
static int gstate(zxv_paradox_graph *g, int n)
{
    uint8_t id[32];
    int32_t s = -1;
    id_of((uint32_t) n, id);
    if (zxv_paradox_graph_state(g, id, &s)) return -1;
    return s;
}

static void test_graph_basic(void)
{
    zxv_paradox *b;
    OK(zxv_paradox_create(cap, 64, 4, &b));
    zxv_paradox_graph *g = NULL;
    IS(zxv_paradox_graph_create(nocap, 3, &g), ZXV_E_CAP);
    IS(zxv_paradox_graph_create(cap, 0, &g), ZXV_E_ARG);
    IS(zxv_paradox_graph_create(cap, 3, NULL), ZXV_E_ARG);
    OK(zxv_paradox_graph_create(cap, 2, &g));
    zxv_paradox_graph_destroy(NULL);
    uint8_t id[32], id2[32];
    int32_t o = -1, s = -1;
    id_of(0, id);
    IS(zxv_paradox_graph_add_node(g, NULL, 1, TAG_DEST, 0, 0), ZXV_E_ARG);
    IS(zxv_paradox_graph_add_node(g, id, 1, TAG_CONS, 0, 0), ZXV_E_ARG);
    IS(zxv_paradox_graph_add_node(g, id, 1, TAG_DEST, 0, 2), ZXV_E_ARG);
    IS(zxv_paradox_graph_add_node(g, id, 1, TAG_DEST, 0, -2), ZXV_E_ARG);
    IS(zxv_paradox_graph_add_node(NULL, id, 1, TAG_DEST, 0, 0), ZXV_E_ARG);
    IS(zxv_paradox_graph_state(g, id, &s), ZXV_E_NOT_FOUND);
    add_scenario(g, NULL);
    IS(zxv_paradox_graph_add_node(g, id, 1, TAG_DEST, 0, 0), ZXV_E_DUPLICATE);
    OK(zxv_paradox_graph_state(g, id, &s));
    CHECK(s == ZXV_PDX_NS_OPEN, "open before the first pass");
    IS(zxv_paradox_graph_state(g, NULL, &s), ZXV_E_ARG);
    IS(zxv_paradox_graph_state(g, id, NULL), ZXV_E_ARG);
    IS(zxv_paradox_graph_node_at(g, 0, NULL, NULL, NULL), ZXV_E_STATE);
    add_scenario_deps(g, 0);
    add_scenario_deps(g, 1); /* the same edges again: no change */
    IS(zxv_paradox_graph_add_dep(g, id, id), ZXV_E_ARG);
    IS(zxv_paradox_graph_add_dep(g, id, NULL), ZXV_E_ARG);
    id_of(999, id2);
    IS(zxv_paradox_graph_add_dep(g, id2, id), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_graph_update(g, id2, 1, TAG_DEST, 0, 0), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_graph_update(g, id, 1, TAG_CONS, 0, 0), ZXV_E_ARG);
    IS(zxv_paradox_graph_update(g, id, 1, TAG_DEST, 0, 3), ZXV_E_ARG);
    IS(zxv_paradox_graph_update(g, NULL, 1, TAG_DEST, 0, 0), ZXV_E_ARG);

    uint32_t nodes = 0, pass = 9, mag = 9;
    int32_t dir = 9, esc = 9;
    uint8_t chain[32];
    memset(chain, 1, 32);
    OK(zxv_paradox_graph_info(g, &nodes, &pass, &mag, &dir, &esc, chain));
    CHECK(nodes == G_COUNT && pass == 0 && mag == 0 && dir == 0 && esc == 0 && zero32(chain),
          "info before the first pass");

    IS(zxv_paradox_graph_pass(nocap, b, g, &o), ZXV_E_CAP);
    IS(zxv_paradox_graph_pass(foreign, b, g, &o), ZXV_E_CAP);
    IS(zxv_paradox_graph_pass(cap, NULL, g, &o), ZXV_E_ARG);
    IS(zxv_paradox_graph_pass(cap, b, NULL, &o), ZXV_E_ARG);
    IS(zxv_paradox_graph_pass(cap, b, g, NULL), ZXV_E_ARG);
    OK(zxv_paradox_graph_pass(cap, b, g, &o));
    CHECK(o == ZXV_PDX_OUT_PENDING, "pass 1 pending (%d)", o);
    static const int want[G_COUNT] = {ZXV_PDX_NS_CONTAINED, ZXV_PDX_NS_TAINTED, ZXV_PDX_NS_SANDBOXED,
                                      ZXV_PDX_NS_WAITING,   ZXV_PDX_NS_HELD,    ZXV_PDX_NS_BLOCKED,
                                      ZXV_PDX_NS_APPLIED,   ZXV_PDX_NS_CLEAR};
    for (int i = 0; i < G_COUNT; i++)
        CHECK(gstate(g, i) == want[i], "node %d state %d want %d", i, gstate(g, i), want[i]);
    /* open: S(3,+1) W(4,-1) N(5,0) C1(6,+1) -> net 3 - 4 + 6 = 5, +1 */
    OK(zxv_paradox_graph_info(g, &nodes, &pass, &mag, &dir, &esc, chain));
    CHECK(pass == 1 && mag == 5 && dir == 1 && esc == 0 && !zero32(chain),
          "info after pass 1 (mag %u dir %d)", mag, dir);
    /* node_at walks the id order */
    uint8_t prev[32] = {0}, cur[32];
    int sorted = 1;
    for (uint32_t i = 0; i < G_COUNT; i++) {
        int32_t k, st;
        OK(zxv_paradox_graph_node_at(g, i, cur, &k, &st));
        if (i && memcmp(prev, cur, 32) >= 0) sorted = 0;
        memcpy(prev, cur, 32);
    }
    CHECK(sorted, "node_at is in id order");
    IS(zxv_paradox_graph_node_at(g, G_COUNT, cur, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_paradox_graph_add_node(g, id2, 1, TAG_DEST, 0, 0), ZXV_E_STATE);
    IS(zxv_paradox_graph_add_dep(g, id, id2), ZXV_E_STATE);
    /* the pass record: aux pass, b = chain */
    uint8_t r[200];
    rec_bytes(b, ZXV_PDX_LOG_OPS, 0, r);
    CHECK(r[8] == 7 && r[9] == ZXV_PDX_KIND_NONE && r[11] == ZXV_PDX_OUT_PENDING &&
              rd32(r + 12) == 1 && !memcmp(r + 48, chain, 32),
          "graph pass record");
    /* the chain = SHA-256("zxv/pdx-chain/v1" || 0^32 || pass digest) */
    uint8_t cb[64], ch[32];
    memset(cb, 0, 32);
    memcpy(cb + 32, r + 16, 32);
    dsha("zxv/pdx-chain/v1", cb, 64, ch);
    CHECK(!memcmp(ch, chain, 32), "chain link recomputed");

    /* pass 2 reaches the step limit unresolved: escalated */
    o = -1;
    IS(zxv_paradox_graph_pass(cap, b, g, &o), ZXV_E_PARADOX_STEP);
    CHECK(o == -1, "outcome not written on escalation");
    OK(zxv_paradox_graph_info(g, NULL, &pass, NULL, NULL, &esc, NULL));
    CHECK(pass == 2 && esc == 1, "escalated after 2 passes");
    uint32_t nops;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    rec_bytes(b, ZXV_PDX_LOG_OPS, nops - 1, r);
    CHECK(nops == 2 && r[11] == ZXV_PDX_OUT_ESCALATE_TO_OWNER, "escalation is logged");
    CHECK(gstate(g, G_D) == ZXV_PDX_NS_CONTAINED, "the graph stays readable");

    /* new evidence: worst wins in a batch; containment is sticky */
    uint8_t sid[32], nid[32], did[32];
    id_of(G_S, sid);
    id_of(G_N, nid);
    id_of(G_D, did);
    OK(zxv_paradox_graph_update(g, sid, 0, TAG_CONS, 1, 0));
    OK(zxv_paradox_graph_update(g, sid, 1, TAG_DEST, 1, 0));
    OK(zxv_paradox_graph_update(g, nid, 0, TAG_CONS, 1, 0));
    OK(zxv_paradox_graph_update(g, did, 0, TAG_CONS, 1, 0));
    OK(zxv_paradox_graph_pass(cap, b, g, &o)); /* every node final: resolved beats the limit */
    CHECK(o == ZXV_PDX_OUT_RESOLVED, "pass 3 resolves (%d)", o);
    CHECK(gstate(g, G_S) == ZXV_PDX_NS_CONTAINED && gstate(g, G_W) == ZXV_PDX_NS_TAINTED,
          "S destructive (worst wins): contained, W tainted");
    CHECK(gstate(g, G_N) == ZXV_PDX_NS_APPLIED && gstate(g, G_C1) == ZXV_PDX_NS_APPLIED,
          "N constructive applied, C1 unblocked");
    CHECK(gstate(g, G_D) == ZXV_PDX_NS_CONTAINED, "contained stays contained");
    zxv_paradox_graph_destroy(g);

    /* a graph that resolves */
    OK(zxv_paradox_graph_create(cap, 5, &g));
    id_of(1, id);
    id_of(2, id2);
    OK(zxv_paradox_graph_add_node(g, id, 0, TAG_CONS, 2, 1));
    OK(zxv_paradox_graph_add_node(g, id2, 0, TAG_CONS, 3, -1));
    OK(zxv_paradox_graph_add_dep(g, id2, id));
    OK(zxv_paradox_graph_pass(cap, b, g, &o));
    CHECK(o == ZXV_PDX_OUT_RESOLVED && gstate(g, 1) == ZXV_PDX_NS_APPLIED &&
              gstate(g, 2) == ZXV_PDX_NS_APPLIED,
          "all constructive -> resolved");
    zxv_paradox_graph_destroy(g);

    /* a dependency on a node never added */
    OK(zxv_paradox_graph_create(cap, 5, &g));
    OK(zxv_paradox_graph_add_node(g, id, 0, TAG_CONS, 2, 1));
    OK(zxv_paradox_graph_add_dep(g, id, id2));
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops));
    IS(zxv_paradox_graph_pass(cap, b, g, &o), ZXV_E_NOT_FOUND);
    uint32_t nops2;
    OK(zxv_paradox_log_count(b, ZXV_PDX_LOG_OPS, &nops2));
    CHECK(nops2 == nops, "nothing logged");
    zxv_paradox_graph_destroy(g);

    /* limits: 64 nodes, 4 deps, 256 queued updates, an empty graph */
    OK(zxv_paradox_graph_create(cap, 5, &g));
    for (uint32_t i = 0; i < ZXV_PDX_GRAPH_MAX; i++) {
        id_of(1000 + i, id);
        OK(zxv_paradox_graph_add_node(g, id, 4, TAG_NONE, 0, 0));
    }
    id_of(5000, id);
    IS(zxv_paradox_graph_add_node(g, id, 4, TAG_NONE, 0, 0), ZXV_E_CAPACITY);
    id_of(1000, id);
    for (uint32_t i = 1; i <= ZXV_PDX_MAX_DEPS; i++) {
        id_of(1000 + i, id2);
        OK(zxv_paradox_graph_add_dep(g, id, id2));
    }
    id_of(1010, id2);
    IS(zxv_paradox_graph_add_dep(g, id, id2), ZXV_E_CAPACITY);
    for (uint32_t i = 0; i < ZXV_PDX_UPDATES_MAX; i++) OK(zxv_paradox_graph_update(g, id, 4, TAG_NONE, i, 0));
    IS(zxv_paradox_graph_update(g, id, 4, TAG_NONE, 0, 0), ZXV_E_CAPACITY);
    OK(zxv_paradox_graph_pass(cap, b, g, &o));
    CHECK(o == ZXV_PDX_OUT_RESOLVED, "64 clear nodes resolve");
    OK(zxv_paradox_graph_update(g, id, 4, TAG_NONE, 0, 0)); /* the queue was drained */
    zxv_paradox_graph_destroy(g);
    OK(zxv_paradox_graph_create(cap, 1, &g));
    OK(zxv_paradox_graph_pass(cap, b, g, &o));
    CHECK(o == ZXV_PDX_OUT_RESOLVED, "an empty graph resolves");
    zxv_paradox_graph_destroy(g);

    /* context binding and a full log */
    zxv_paradox_graph *gf;
    OK(zxv_paradox_graph_create(foreign, 3, &gf));
    IS(zxv_paradox_graph_pass(cap, b, gf, &o), ZXV_E_ARG);
    zxv_paradox_graph_destroy(gf);
    zxv_paradox *tiny;
    OK(zxv_paradox_create(cap, 1, 1, &tiny));
    OK(zxv_paradox_graph_create(cap, 3, &g));
    add_scenario(g, NULL);
    OK(zxv_paradox_graph_pass(cap, tiny, g, &o));
    OK(zxv_paradox_graph_update(g, sid, 1, TAG_DEST, 0, 0));
    IS(zxv_paradox_graph_pass(cap, tiny, g, &o), ZXV_E_CAPACITY);
    CHECK(gstate(g, G_S) == ZXV_PDX_NS_SANDBOXED, "log full: the queued update did not apply");
    OK(zxv_paradox_graph_info(g, NULL, &pass, NULL, NULL, NULL, NULL));
    CHECK(pass == 1, "log full: no pass");
    zxv_paradox_graph_destroy(g);
    zxv_paradox_destroy(tiny);
    zxv_paradox_destroy(b);
}

/* ---- determinism ---- */
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) (rng % n);
}
static void shuffle(int *a, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int) rnd((uint32_t) i + 1), t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

#define DN 24
typedef struct {
    int kind[DN];
    uint8_t tag[DN];
    uint32_t mag[DN];
    int dir[DN];
    int ndep[DN], dep[DN][4];
    int nup, up_node[40], up_kind[40];
    uint8_t up_tag[40];
    uint32_t up_mag[40];
    int up_dir[40];
} scen_t;

static uint8_t tag_for(int k, int forb)
{
    if (forb) return TAG_FORB;
    static const uint8_t t[5] = {2 << 4 | 3 << 2 | 3, 2 << 4 | 0 << 2 | 3, 2 << 4 | 1 << 2 | 3,
                                 2 << 4 | 1 << 2 | 0, 3 << 4 | 1 << 2 | 3};
    return t[k];
}
static void make_scen(scen_t *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < DN; i++) {
        s->kind[i] = (int) rnd(5);
        s->tag[i] = tag_for(s->kind[i], rnd(12) == 0);
        s->mag[i] = rnd(100);
        s->dir[i] = (int) rnd(3) - 1;
        int nd = (int) rnd(3);
        for (int d = 0; d < nd; d++) {
            int t = (int) rnd(DN);
            int dup = t == i;
            for (int e = 0; e < s->ndep[i]; e++) dup |= s->dep[i][e] == t;
            if (!dup) s->dep[i][s->ndep[i]++] = t;
        }
    }
    s->nup = 10 + (int) rnd(20);
    for (int u = 0; u < s->nup; u++) {
        s->up_node[u] = (int) rnd(DN);
        s->up_kind[u] = (int) rnd(5);
        s->up_tag[u] = tag_for(s->up_kind[u], rnd(15) == 0);
        s->up_mag[u] = rnd(100);
        s->up_dir[u] = (int) rnd(3) - 1;
    }
}

/* Run the scenario on a fresh book: nodes, edges and updates in shuffled
 * orders (or not), two passes with the update batch in between. Writes the
 * graph chain, every node's state in id order, and the operator log head. */
static int run_scen(const scen_t *s, int shuffled, uint8_t chain[32], uint8_t states[DN],
                    uint8_t head[32], int32_t outs[2])
{
    zxv_paradox *b;
    zxv_paradox_graph *g;
    if (zxv_paradox_create(cap, 8, 1, &b)) return 0;
    if (zxv_paradox_graph_create(cap, 8, &g)) return 0;
    int no[DN], eo[DN * 4][2], ne = 0, uo[40];
    for (int i = 0; i < DN; i++) no[i] = i;
    for (int i = 0; i < DN; i++)
        for (int d = 0; d < s->ndep[i]; d++) {
            eo[ne][0] = i;
            eo[ne][1] = s->dep[i][d];
            ne++;
        }
    for (int u = 0; u < s->nup; u++) uo[u] = u;
    if (shuffled) {
        shuffle(no, DN);
        for (int i = ne - 1; i > 0; i--) {
            int j = (int) rnd((uint32_t) i + 1), t0 = eo[i][0], t1 = eo[i][1];
            eo[i][0] = eo[j][0];
            eo[i][1] = eo[j][1];
            eo[j][0] = t0;
            eo[j][1] = t1;
        }
        shuffle(uo, s->nup);
    }
    int ok = 1;
    uint8_t id[32], id2[32];
    for (int i = 0; i < DN; i++) {
        int n = no[i];
        id_of((uint32_t) n + 500, id);
        ok &= zxv_paradox_graph_add_node(g, id, s->kind[n], s->tag[n], s->mag[n], s->dir[n]) == ZXV_OK;
    }
    for (int e = 0; e < ne; e++) {
        id_of((uint32_t) eo[e][0] + 500, id);
        id_of((uint32_t) eo[e][1] + 500, id2);
        ok &= zxv_paradox_graph_add_dep(g, id, id2) == ZXV_OK;
    }
    int32_t rc = zxv_paradox_graph_pass(cap, b, g, &outs[0]);
    if (rc == ZXV_E_PARADOX_STEP) outs[0] = 100;
    else ok &= rc == ZXV_OK;
    for (int u = 0; u < s->nup; u++) {
        int k = uo[u];
        id_of((uint32_t) s->up_node[k] + 500, id);
        ok &= zxv_paradox_graph_update(g, id, s->up_kind[k], s->up_tag[k], s->up_mag[k],
                                       s->up_dir[k]) == ZXV_OK;
    }
    rc = zxv_paradox_graph_pass(cap, b, g, &outs[1]);
    if (rc == ZXV_E_PARADOX_STEP) outs[1] = 100;
    else ok &= rc == ZXV_OK;
    ok &= zxv_paradox_graph_info(g, NULL, NULL, NULL, NULL, NULL, chain) == ZXV_OK;
    for (uint32_t i = 0; i < DN; i++) {
        int32_t st = -1;
        ok &= zxv_paradox_graph_node_at(g, i, NULL, NULL, &st) == ZXV_OK;
        states[i] = (uint8_t) st;
    }
    ok &= zxv_paradox_log_head(b, ZXV_PDX_LOG_OPS, head) == ZXV_OK;
    ok &= chain_ok(b, ZXV_PDX_LOG_OPS);
    zxv_paradox_graph_destroy(g);
    zxv_paradox_destroy(b);
    return ok;
}

static void test_graph_determinism(void)
{
    int bad = 0, runs = 0, nonfinal = 0, contained = 0;
    for (int sc = 0; sc < 60; sc++) {
        scen_t s;
        make_scen(&s);
        uint8_t c0[32], st0[DN], h0[32];
        int32_t o0[2];
        if (!run_scen(&s, 0, c0, st0, h0, o0)) bad++;
        for (int i = 0; i < DN; i++) {
            nonfinal += st0[i] == ZXV_PDX_NS_HELD || st0[i] == ZXV_PDX_NS_WAITING;
            contained += st0[i] == ZXV_PDX_NS_CONTAINED || st0[i] == ZXV_PDX_NS_TAINTED;
        }
        for (int rep = 0; rep < 6; rep++) {
            uint8_t c1[32], st1[DN], h1[32];
            int32_t o1[2];
            runs++;
            if (!run_scen(&s, rep > 0, c1, st1, h1, o1) || memcmp(c0, c1, 32) ||
                memcmp(st0, st1, DN) || memcmp(h0, h1, 32) || o0[0] != o1[0] || o0[1] != o1[1])
                bad++;
        }
    }
    CHECK(bad == 0, "determinism: %d of %d runs differ (same inputs, twice and shuffled)", bad, runs);
    CHECK(nonfinal > 0 && contained > 0, "the scenarios exercise held/waiting and contained nodes");

    /* the record bytes themselves are identical across peers */
    scen_t s;
    make_scen(&s);
    uint8_t c[32], st[DN], h[32];
    int32_t o[2];
    CHECK(run_scen(&s, 1, c, st, h, o), "peer A");
    uint8_t c2[32], st2[DN], h2[32];
    int32_t o2[2];
    CHECK(run_scen(&s, 1, c2, st2, h2, o2), "peer B");
    CHECK(!memcmp(h, h2, 32) && !memcmp(c, c2, 32), "two peers, two orders: one head, one chain");
}

/* ===================================================================== */
static void test_fail_closed(void)
{
    zxv_ctx *c2;
    zxv_cap *k2;
    OK(zxv_ctx_create(&c2));
    OK(zxv_cap_create(c2, 3, NULL, ZXV_CAP_PARADOX, &k2));
    zxv_paradox *b;
    zxv_paradox_policy *p;
    zxv_paradox_hold *h;
    zxv_paradox_graph *g;
    const int64_t lo[1] = {0}, hi[1] = {9}, in[1] = {1};
    OK(zxv_paradox_create(k2, 4, 4, &b));
    OK(zxv_paradox_policy_create(k2, 1, lo, hi, in, 0, &p));
    OK(zxv_paradox_hold_create(k2, 2, 1, &h));
    OK(zxv_paradox_graph_create(k2, 2, &g));
    uint8_t id[32], buf[200], dr[71];
    id_of(1, id);
    OK(zxv_paradox_graph_add_node(g, id, 0, TAG_CONS, 0, 0));
    OK(zxv_ctx_halt(c2, "test"));
    uint32_t n;
    int32_t o;
    size_t len;
    int64_t v[1];
    IS(zxv_paradox_create(k2, 4, 4, &b), ZXV_E_POISONED);
    IS(zxv_paradox_classify(b, TAG_CONS, NULL, 0, NULL, &o, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_registry_add(k2, b, id, 0, 0, 0, 0, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_registry_find(b, id, &n), ZXV_E_POISONED);
    IS(zxv_paradox_registry_count(b, &n), ZXV_E_POISONED);
    IS(zxv_paradox_registry_at(b, 0, NULL, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_log_count(b, 0, &n), ZXV_E_POISONED);
    IS(zxv_paradox_log_head(b, 0, buf), ZXV_E_POISONED);
    IS(zxv_paradox_log_record(b, 0, 0, buf, sizeof buf, &len), ZXV_E_POISONED);
    IS(zxv_paradox_log_verify(b, 0), ZXV_E_POISONED);
    IS(zxv_paradox_constructive(k2, b, p, 0, TAG_CONS, in, 1, 0, &o), ZXV_E_POISONED);
    IS(zxv_paradox_policy_values(p, v, NULL, 1, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_policy_bounds(p, NULL, NULL, 0, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_policy_proposal(p, in, 1, buf, sizeof buf, &len), ZXV_E_POISONED);
    IS(zxv_paradox_destructive(k2, b, 1, TAG_DEST, 0, 0, 0, 0, NULL, 0, 1, NULL, NULL, 0, NULL, 0, 0, &o, buf), ZXV_E_POISONED);
    IS(zxv_paradox_hold_add(h, id), ZXV_E_POISONED);
    IS(zxv_paradox_hold_evidence(h, id, 1), ZXV_E_POISONED);
    IS(zxv_paradox_hold_info(h, &n, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_hold_branch(h, 0, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_neutral(k2, b, h, 2, TAG_NEUT, -1, &o, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_special(k2, b, 3, TAG_SPEC, id, id, &o, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_draft(k2, b, 0, 0, 0, 0, dr), ZXV_E_POISONED);
    IS(zxv_paradox_review(k2, b, dr, 1, 1, &o), ZXV_E_POISONED);
    IS(zxv_paradox_graph_add_node(g, id, 0, TAG_CONS, 0, 0), ZXV_E_POISONED);
    IS(zxv_paradox_graph_add_dep(g, id, buf), ZXV_E_POISONED);
    IS(zxv_paradox_graph_update(g, id, 0, TAG_CONS, 0, 0), ZXV_E_POISONED);
    IS(zxv_paradox_graph_pass(k2, b, g, &o), ZXV_E_POISONED);
    IS(zxv_paradox_graph_state(g, id, &o), ZXV_E_POISONED);
    IS(zxv_paradox_graph_node_at(g, 0, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_graph_info(g, &n, NULL, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_paradox_policy_create(k2, 1, lo, hi, in, 0, &p), ZXV_E_POISONED);
    IS(zxv_paradox_hold_create(k2, 1, 1, &h), ZXV_E_POISONED);
    IS(zxv_paradox_graph_create(k2, 1, &g), ZXV_E_POISONED);
    /* pure functions keep working */
    OK(zxv_paradox_kind_of_tag(TAG_CONS, &o));
    zxv_paradox_graph_destroy(g);
    zxv_paradox_hold_destroy(h);
    zxv_paradox_policy_destroy(p);
    zxv_paradox_destroy(b);
    zxv_cap_destroy(k2);
    zxv_ctx_destroy(c2);
}

int main(void)
{
    OK(zxv_ctx_create(&ctx));
    OK(zxv_ctx_create(&other));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_5, &cap));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_ALL_1_5 & ~ZXV_CAP_PARADOX, &nocap));
    OK(zxv_cap_create(other, 2, NULL, ZXV_CAP_ALL_1_5, &foreign));
    if (!cap || !nocap || !foreign) {
        printf("test_zxv_abi15_paradox: cannot create capabilities, %d checks, %d failures\n",
               checks, failures + 1);
        return 1;
    }
    test_pure();
    test_book_registry();
    test_constructive();
    test_destructive();
    test_neutral();
    test_special();
    test_graph_basic();
    test_graph_determinism();
    test_fail_closed();
    zxv_cap_destroy(nocap);
    zxv_cap_destroy(cap);
    zxv_cap_destroy(foreign);
    zxv_ctx_destroy(ctx);
    zxv_ctx_destroy(other);
    printf("test_zxv_abi15_paradox: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
