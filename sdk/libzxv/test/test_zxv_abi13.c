/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi13.c — conformance test of ABI 1.3, the 64-state hexagram
 * tag and its quarantine log, linked against the shared library only (no
 * kernel headers). Every rule is restated here independently of the
 * kernel's tables and checked on all 64 tags (and all 64 x 64 pairs):
 *   - layout: base bits 5-4, polarity 3-2, phase 1-0;
 *   - the base <-> ZXV_LP_* mapping (F0 N1 B2 T3 vs NEITHER0 TRUE1 FALSE2
 *     BOTH3), which differs on purpose;
 *   - the action table, one-step phase moves, forbidden absorption;
 *   - and / or / not against zxv_lp_* plus worst polarity / earliest phase;
 *   - names, polarity from evidence, ZXV_E_ARG on every out-of-range input;
 *   - the quarantine log: capability, context binding, forbidden-only,
 *     full = CAPACITY, the documented SHA-256 chain recomputed here, and
 *     fail-closed once the context halts. */
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
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_));             \
    } while (0)
#define IS(call, want)                                                                         \
    do {                                                                                       \
        int32_t rc_ = (call);                                                                  \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want);         \
    } while (0)

/* ---- the rules, restated ---- */
static int oracle_action(int base, int pol)
{
    if (pol == ZXV_HEX_POL_FORBIDDEN) return ZXV_HEX_ACT_ROLLBACK;
    switch (base) {
    case ZXV_HEX_BASE_TRUE: return pol == ZXV_HEX_POL_ENTROPIC ? ZXV_HEX_ACT_WAIT : ZXV_HEX_ACT_PROCEED;
    case ZXV_HEX_BASE_FALSE: return ZXV_HEX_ACT_REFUSE;
    case ZXV_HEX_BASE_BOTH: return pol == ZXV_HEX_POL_NEGENTROPIC ? ZXV_HEX_ACT_WAIT : ZXV_HEX_ACT_ESCALATE;
    default: return ZXV_HEX_ACT_WAIT;
    }
}
/* hexagram base code <-> ZXV_LP_* value, written out by name */
static int base_to_lp(int base)
{
    switch (base) {
    case ZXV_HEX_BASE_FALSE: return ZXV_LP_FALSE;
    case ZXV_HEX_BASE_NEITHER: return ZXV_LP_NEITHER;
    case ZXV_HEX_BASE_BOTH: return ZXV_LP_BOTH;
    default: return ZXV_LP_TRUE;
    }
}
static int lp_to_base(int lp)
{
    for (int b = 0; b < 4; b++)
        if (base_to_lp(b) == lp) return b;
    return -1;
}
static int pol_badness(int p) /* forbidden > entropic > neutral > negentropic */
{
    return p == ZXV_HEX_POL_FORBIDDEN ? 3 : p == ZXV_HEX_POL_ENTROPIC ? 2 : p == ZXV_HEX_POL_NEUTRAL ? 1 : 0;
}
static int phase_order(int p) /* potential < emerging < manifest < dissolving */
{
    return p == ZXV_HEX_PHASE_POTENTIAL ? 0 : p == ZXV_HEX_PHASE_EMERGING ? 1 : p == ZXV_HEX_PHASE_MANIFEST ? 2 : 3;
}
static int next_phase(int p)
{
    static const int order[4] = {ZXV_HEX_PHASE_POTENTIAL, ZXV_HEX_PHASE_EMERGING,
                                 ZXV_HEX_PHASE_MANIFEST, ZXV_HEX_PHASE_DISSOLVING};
    return order[(phase_order(p) + 1) % 4];
}
static uint8_t mk(int b, int p, int ph)
{
    return (uint8_t) ((b << 4) | (p << 2) | ph);
}

static void test_constants_and_layout(void)
{
    uint32_t maj, min, pat, feats;
    OK(zxv_version(&maj, &min, &pat));
    CHECK(maj == 1 && min >= 3, "ABI 1.3+ (%u.%u.%u)", maj, min, pat);
    OK(zxv_features(&feats));
    CHECK(feats & ZXV_FEAT_HEX, "ZXV_FEAT_HEX is set");
    CHECK(ZXV_FEAT_HEX == 0x08000000u, "feature bit value");
    /* the user's codes: True 11, False 00, Both 10, Neither 01 ... */
    CHECK(ZXV_HEX_BASE_TRUE == 3 && ZXV_HEX_BASE_FALSE == 0 && ZXV_HEX_BASE_BOTH == 2 &&
              ZXV_HEX_BASE_NEITHER == 1, "base codes");
    CHECK(ZXV_HEX_POL_NEGENTROPIC == 3 && ZXV_HEX_POL_ENTROPIC == 0 && ZXV_HEX_POL_NEUTRAL == 1 &&
              ZXV_HEX_POL_FORBIDDEN == 2, "polarity codes");
    CHECK(ZXV_HEX_PHASE_POTENTIAL == 0 && ZXV_HEX_PHASE_EMERGING == 1 &&
              ZXV_HEX_PHASE_MANIFEST == 3 && ZXV_HEX_PHASE_DISSOLVING == 2, "phase codes");
    CHECK(ZXV_HEX_ACT_PROCEED == 0 && ZXV_HEX_ACT_WAIT == 1 && ZXV_HEX_ACT_REFUSE == 2 &&
              ZXV_HEX_ACT_ESCALATE == 3 && ZXV_HEX_ACT_ROLLBACK == 4, "action codes");
    for (int t = 0; t < 64; t++) {
        uint8_t b = 9, p = 9, ph = 9, back = 99;
        OK(zxv_hex_unpack((uint8_t) t, &b, &p, &ph));
        CHECK(b == (t >> 4) && p == ((t >> 2) & 3) && ph == (t & 3), "unpack %d", t);
        OK(zxv_hex_pack(b, p, ph, &back));
        CHECK(back == t, "pack(unpack(%d)) = %u", t, back);
    }
    OK(zxv_hex_unpack(63, NULL, NULL, NULL));
}

static void test_lp_bridge(void)
{
    /* The mapping is explicit and NOT the identity. */
    for (int lp = 0; lp < 4; lp++) {
        uint8_t base = 99;
        int32_t back = -1;
        OK(zxv_hex_from_lp(lp, &base));
        CHECK(base == lp_to_base(lp), "from_lp(%s) = %u", zxv_lp_name(lp), base);
        OK(zxv_hex_to_lp(base, &back));
        CHECK(back == lp, "round trip %s", zxv_lp_name(lp));
    }
    uint8_t b;
    int32_t l;
    OK(zxv_hex_from_lp(ZXV_LP_TRUE, &b));
    CHECK(b == ZXV_HEX_BASE_TRUE && b != ZXV_LP_TRUE, "TRUE: lp 1 -> base 3");
    OK(zxv_hex_from_lp(ZXV_LP_NEITHER, &b));
    CHECK(b == ZXV_HEX_BASE_NEITHER, "NEITHER: lp 0 -> base 1");
    OK(zxv_hex_from_lp(ZXV_LP_FALSE, &b));
    CHECK(b == ZXV_HEX_BASE_FALSE, "FALSE: lp 2 -> base 0");
    OK(zxv_hex_from_lp(ZXV_LP_BOTH, &b));
    CHECK(b == ZXV_HEX_BASE_BOTH, "BOTH: lp 3 -> base 2");
    IS(zxv_hex_from_lp(4, &b), ZXV_E_ARG);
    IS(zxv_hex_from_lp(-1, &b), ZXV_E_ARG);
    IS(zxv_hex_to_lp(4, &l), ZXV_E_ARG);
    IS(zxv_hex_from_lp(0, NULL), ZXV_E_ARG);
    IS(zxv_hex_to_lp(0, NULL), ZXV_E_ARG);
}

static void test_action_names(void)
{
    static const char *bn[4] = {"false", "neither", "both", "true"};
    static const char *pn[4] = {"entropic", "neutral", "forbidden", "negentropic"};
    static const char *phn[4] = {"potential", "emerging", "dissolving", "manifest"};
    static const char *an[5] = {"proceed", "wait", "refuse", "escalate", "rollback"};
    int counts[5] = {0};
    for (int t = 0; t < 64; t++) {
        int32_t a = -1;
        OK(zxv_hex_action((uint8_t) t, &a));
        CHECK(a == oracle_action(t >> 4, (t >> 2) & 3), "action(%d) = %d", t, a);
        if (a >= 0 && a < 5) counts[a]++;
        char want[48];
        snprintf(want, sizeof want, "%s/%s/%s", bn[t >> 4], pn[(t >> 2) & 3], phn[t & 3]);
        CHECK(strcmp(zxv_hex_name((uint8_t) t), want) == 0, "name(%d) = %s, want %s", t,
              zxv_hex_name((uint8_t) t), want);
    }
    CHECK(counts[ZXV_HEX_ACT_ROLLBACK] == 16, "16 forbidden tags roll back");
    CHECK(counts[ZXV_HEX_ACT_PROCEED] == 8, "8 tags proceed (true, neutral or negentropic)");
    CHECK(counts[ZXV_HEX_ACT_REFUSE] == 12, "12 false tags refuse");
    CHECK(counts[ZXV_HEX_ACT_ESCALATE] == 8, "8 both tags escalate");
    CHECK(counts[ZXV_HEX_ACT_WAIT] == 20, "20 tags wait");
    for (int a = 0; a < 5; a++)
        CHECK(strcmp(zxv_hex_action_name(a), an[a]) == 0, "action name %d", a);
    CHECK(strcmp(zxv_hex_action_name(5), "INVALID") == 0, "action name 5");
    CHECK(strcmp(zxv_hex_action_name(-1), "INVALID") == 0, "action name -1");
    CHECK(strcmp(zxv_hex_name(64), "INVALID") == 0, "name 64");
    CHECK(strcmp(zxv_hex_name(255), "INVALID") == 0, "name 255");
    CHECK(strcmp(zxv_hex_name(mk(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_MANIFEST)),
                 "true/negentropic/manifest") == 0, "the header's example");
}

static void test_step_set(void)
{
    for (int ph = 0; ph < 4; ph++) {
        uint8_t n = 9;
        OK(zxv_hex_phase_next((uint8_t) ph, &n));
        CHECK(n == next_phase(ph), "phase_next(%d) = %u", ph, n);
        CHECK(__builtin_popcount((unsigned) (n ^ ph)) == 1, "Gray: one bit flips %d -> %u", ph, n);
    }
    for (int t = 0; t < 64; t++) {
        for (int ph = 0; ph < 4; ph++) {
            uint8_t out = 0xEE;
            int32_t rc = zxv_hex_step((uint8_t) t, (uint8_t) ph, &out);
            int legal = ph == (t & 3) || ph == next_phase(t & 3);
            if (legal)
                CHECK(rc == ZXV_OK && out == ((t & ~3) | ph), "step(%d, %d) = %u", t, ph, out);
            else
                CHECK(rc == ZXV_E_STATE && out == 0xEE, "step(%d, %d) refused, out untouched", t, ph);
        }
        for (int b = 0; b < 4; b++) {
            for (int p = 0; p < 4; p++) {
                uint8_t out = 0xEE;
                int32_t rc = zxv_hex_set((uint8_t) t, (uint8_t) b, (uint8_t) p, &out);
                int forb = ((t >> 2) & 3) == ZXV_HEX_POL_FORBIDDEN;
                if (forb && p != ZXV_HEX_POL_FORBIDDEN)
                    CHECK(rc == ZXV_E_STATE && out == 0xEE, "set(%d,%d,%d) leaves forbidden", t, b, p);
                else
                    CHECK(rc == ZXV_OK && out == mk(b, p, t & 3), "set(%d,%d,%d) = %u", t, b, p, out);
            }
        }
        uint8_t f = 0;
        OK(zxv_hex_forbid((uint8_t) t, &f));
        CHECK(f == mk(t >> 4, ZXV_HEX_POL_FORBIDDEN, t & 3), "forbid(%d)", t);
    }
    /* A forbidden tag walks its whole lifecycle and stays forbidden. */
    uint8_t t = mk(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_POTENTIAL);
    for (int i = 0; i < 4; i++) {
        uint8_t n;
        OK(zxv_hex_phase_next(t & 3, &n));
        OK(zxv_hex_step(t, n, &t));
        CHECK(((t >> 2) & 3) == ZXV_HEX_POL_FORBIDDEN, "still forbidden after step %d", i);
    }
    CHECK((t & 3) == ZXV_HEX_PHASE_POTENTIAL, "four steps go round the cycle");
}

static void test_combine(void)
{
    int bad = 0;
    for (int a = 0; a < 64; a++) {
        uint8_t n = 0;
        OK(zxv_hex_not((uint8_t) a, &n));
        int32_t lpn;
        zxv_lp_not(base_to_lp(a >> 4), &lpn);
        CHECK(n == mk(lp_to_base(lpn), (a >> 2) & 3, a & 3), "not(%d) = %u", a, n);
        for (int b = 0; b < 64; b++) {
            uint8_t x = 0, y = 0;
            int32_t la, lo;
            if (zxv_hex_and((uint8_t) a, (uint8_t) b, &x) || zxv_hex_or((uint8_t) a, (uint8_t) b, &y)) {
                bad++;
                continue;
            }
            zxv_lp_and(base_to_lp(a >> 4), base_to_lp(b >> 4), &la);
            zxv_lp_or(base_to_lp(a >> 4), base_to_lp(b >> 4), &lo);
            int pa = (a >> 2) & 3, pb = (b >> 2) & 3;
            int pol = pol_badness(pa) >= pol_badness(pb) ? pa : pb;
            int ph = phase_order(a & 3) <= phase_order(b & 3) ? (a & 3) : (b & 3);
            if (x != mk(lp_to_base(la), pol, ph) || y != mk(lp_to_base(lo), pol, ph)) bad++;
            /* forbidden is contagious through and / or */
            if ((pa == ZXV_HEX_POL_FORBIDDEN || pb == ZXV_HEX_POL_FORBIDDEN) &&
                (((x >> 2) & 3) != ZXV_HEX_POL_FORBIDDEN || ((y >> 2) & 3) != ZXV_HEX_POL_FORBIDDEN))
                bad++;
        }
    }
    CHECK(bad == 0, "and/or over all 4096 pairs follow the rules (%d mismatches)", bad);
}

static void test_polarity(void)
{
    struct { uint32_t p0, n0, p1, n1; int stale; int want; } c[] = {
        {0, 0, 1, 0, 0, ZXV_HEX_POL_NEGENTROPIC},
        {3, 1, 5, 1, 0, ZXV_HEX_POL_NEGENTROPIC},
        {3, 1, 3, 2, 0, ZXV_HEX_POL_ENTROPIC},
        {3, 1, 4, 2, 0, ZXV_HEX_POL_NEUTRAL},
        {0, 0, 0, 0, 0, ZXV_HEX_POL_NEUTRAL},
        {3, 1, 9, 1, 1, ZXV_HEX_POL_ENTROPIC},
        {1, 4, 1, 6, 0, ZXV_HEX_POL_NEGENTROPIC},
        {UINT32_MAX, 0, 0, UINT32_MAX, 0, ZXV_HEX_POL_NEUTRAL},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        uint8_t p = 9;
        OK(zxv_hex_polarity(c[i].p0, c[i].n0, c[i].p1, c[i].n1, c[i].stale, &p));
        CHECK(p == c[i].want, "polarity case %zu = %u, want %d", i, p, c[i].want);
    }
    uint8_t p;
    OK(zxv_hex_polarity(1, 0, 2, 0, -7, &p));
    CHECK(p == ZXV_HEX_POL_ENTROPIC, "any non-zero stale is stale");
    IS(zxv_hex_polarity(0, 0, 0, 0, 0, NULL), ZXV_E_ARG);
}

static void test_arg_refusals(void)
{
    uint8_t o = 0xEE, x;
    int32_t a;
    static const uint8_t bad[] = {64, 65, 127, 128, 200, 255};
    for (size_t i = 0; i < sizeof bad; i++) {
        uint8_t t = bad[i];
        IS(zxv_hex_unpack(t, &o, &o, &o), ZXV_E_ARG);
        IS(zxv_hex_action(t, &a), ZXV_E_ARG);
        IS(zxv_hex_step(t, 0, &o), ZXV_E_ARG);
        IS(zxv_hex_set(t, 0, 0, &o), ZXV_E_ARG);
        IS(zxv_hex_forbid(t, &o), ZXV_E_ARG);
        IS(zxv_hex_and(t, 0, &o), ZXV_E_ARG);
        IS(zxv_hex_and(0, t, &o), ZXV_E_ARG);
        IS(zxv_hex_or(t, 0, &o), ZXV_E_ARG);
        IS(zxv_hex_or(0, t, &o), ZXV_E_ARG);
        IS(zxv_hex_not(t, &o), ZXV_E_ARG);
    }
    CHECK(o == 0xEE, "no out parameter written on ZXV_E_ARG");
    IS(zxv_hex_pack(4, 0, 0, &x), ZXV_E_ARG);
    IS(zxv_hex_pack(0, 4, 0, &x), ZXV_E_ARG);
    IS(zxv_hex_pack(0, 0, 4, &x), ZXV_E_ARG);
    IS(zxv_hex_pack(255, 255, 255, &x), ZXV_E_ARG);
    IS(zxv_hex_pack(0, 0, 0, NULL), ZXV_E_ARG);
    IS(zxv_hex_step(0, 4, &x), ZXV_E_ARG);
    IS(zxv_hex_set(0, 4, 0, &x), ZXV_E_ARG);
    IS(zxv_hex_set(0, 0, 4, &x), ZXV_E_ARG);
    IS(zxv_hex_phase_next(4, &x), ZXV_E_ARG);
    IS(zxv_hex_phase_next(0, NULL), ZXV_E_ARG);
    IS(zxv_hex_action(0, NULL), ZXV_E_ARG);
    IS(zxv_hex_step(0, 0, NULL), ZXV_E_ARG);
    IS(zxv_hex_set(0, 0, 0, NULL), ZXV_E_ARG);
    IS(zxv_hex_forbid(0, NULL), ZXV_E_ARG);
    IS(zxv_hex_and(0, 0, NULL), ZXV_E_ARG);
    IS(zxv_hex_or(0, 0, NULL), ZXV_E_ARG);
    IS(zxv_hex_not(0, NULL), ZXV_E_ARG);
    CHECK(zxv_status_class(ZXV_E_STATE) == ZXV_CLASS_REFUSED, "a jump is REFUSED, not USAGE");
}

/* ---- a small SHA-256 (FIPS 180-4) to recompute the chain independently ---- */
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
    uint8_t buf[256];
    size_t padded = ((len + 9 + 63) / 64) * 64;
    if (padded > sizeof buf) return;
    memset(buf, 0, padded);
    memcpy(buf, m, len);
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

static void chain_hash(uint64_t seq, uint8_t tag, uint8_t reason, uint32_t submitter,
                       const uint8_t subject[32], const uint8_t prev[32], uint8_t out[32])
{
    uint8_t b[15 + 8 + 1 + 1 + 4 + 32 + 32];
    size_t k = 0;
    memcpy(b, "zxv/lpx-qlog/v1", 15);
    k = 15;
    for (int i = 0; i < 8; i++) b[k++] = (uint8_t) (seq >> (8 * i));
    b[k++] = tag;
    b[k++] = reason;
    for (int i = 0; i < 4; i++) b[k++] = (uint8_t) (submitter >> (8 * i));
    memcpy(b + k, subject, 32);
    k += 32;
    memcpy(b + k, prev, 32);
    k += 32;
    sha256(b, k, out);
}

static void test_qlog(void)
{
    /* the SHA-256 here is right: FIPS 180-4 "abc" */
    uint8_t d[32];
    sha256((const uint8_t *) "abc", 3, d);
    CHECK(d[0] == 0xba && d[1] == 0x78 && d[31] == 0xad, "SHA-256(abc)");

    zxv_ctx *ctx, *other;
    zxv_cap *cap, *noattest, *foreign;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_ctx_create(&other));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &noattest));
    OK(zxv_cap_create(other, 2, NULL, ZXV_CAP_ALL, &foreign));

    zxv_hexlog *log = NULL, *tmp = NULL;
    IS(zxv_hexlog_create(noattest, 4, &tmp), ZXV_E_CAP);
    IS(zxv_hexlog_create(cap, 0, &tmp), ZXV_E_ARG);
    IS(zxv_hexlog_create(cap, ZXV_HEXLOG_MAX_RECORDS + 1, &tmp), ZXV_E_ARG);
    IS(zxv_hexlog_create(cap, 4, NULL), ZXV_E_ARG);
    IS(zxv_hexlog_create(NULL, 4, &tmp), ZXV_E_ARG);
    OK(zxv_hexlog_create(cap, 3, &log));

    uint32_t n = 99;
    uint8_t head[32], zero[32] = {0};
    OK(zxv_hexlog_count(log, &n));
    CHECK(n == 0, "empty");
    OK(zxv_hexlog_head(log, head));
    CHECK(memcmp(head, zero, 32) == 0, "empty head is zeros");
    OK(zxv_hexlog_verify(log));

    uint8_t subj[32];
    for (int i = 0; i < 32; i++) subj[i] = (uint8_t) i;
    uint8_t forb = mk(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST);
    uint8_t clean = mk(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_MANIFEST);
    uint64_t seq = 77;
    IS(zxv_hexlog_append(noattest, log, forb, 1, 42, subj, &seq), ZXV_E_CAP);
    IS(zxv_hexlog_append(foreign, log, forb, 1, 42, subj, &seq), ZXV_E_CAP);
    IS(zxv_hexlog_append(cap, log, clean, 1, 42, subj, &seq), ZXV_E_ARG);
    IS(zxv_hexlog_append(cap, log, 64, 1, 42, subj, &seq), ZXV_E_ARG);
    IS(zxv_hexlog_append(cap, log, 0xFA, 1, 42, subj, &seq), ZXV_E_ARG); /* 0xFA & 63 is forbidden */
    IS(zxv_hexlog_append(cap, log, forb, 1, 42, NULL, &seq), ZXV_E_ARG);
    IS(zxv_hexlog_append(cap, NULL, forb, 1, 42, subj, &seq), ZXV_E_ARG);
    CHECK(seq == 77, "seq untouched on refusal");

    uint8_t want_prev[32] = {0}, want_hash[32];
    struct { uint8_t tag, reason; uint32_t who; } in[3] = {
        {forb, 1, 42},
        {mk(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_POTENTIAL), 7, 0xDEADBEEFu},
        {mk(ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_DISSOLVING), 255, 0},
    };
    for (uint32_t k = 0; k < 3; k++) {
        subj[0] = (uint8_t) k;
        OK(zxv_hexlog_append(cap, log, in[k].tag, in[k].reason, in[k].who, subj, &seq));
        CHECK(seq == k, "seq %u", k);
        chain_hash(k, in[k].tag, in[k].reason, in[k].who, subj, want_prev, want_hash);
        uint64_t s;
        uint8_t t, r, sb[32], pv[32], hs[32];
        uint32_t w;
        OK(zxv_hexlog_at(log, k, &s, &t, &r, &w, sb, pv, hs));
        CHECK(s == k && t == in[k].tag && r == in[k].reason && w == in[k].who, "record %u fields", k);
        CHECK(memcmp(sb, subj, 32) == 0, "record %u subject", k);
        CHECK(memcmp(pv, want_prev, 32) == 0, "record %u prev links to the previous hash", k);
        CHECK(memcmp(hs, want_hash, 32) == 0, "record %u hash is the documented SHA-256", k);
        OK(zxv_hexlog_head(log, head));
        CHECK(memcmp(head, want_hash, 32) == 0, "head after %u", k);
        memcpy(want_prev, want_hash, 32);
        OK(zxv_hexlog_at(log, k, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    }
    OK(zxv_hexlog_count(log, &n));
    CHECK(n == 3, "3 records");
    OK(zxv_hexlog_verify(log));
    IS(zxv_hexlog_at(log, 3, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_hexlog_append(cap, log, forb, 1, 42, subj, &seq), ZXV_E_CAPACITY);
    OK(zxv_hexlog_count(log, &n));
    CHECK(n == 3, "a full log records nothing");
    OK(zxv_hexlog_head(log, head));
    CHECK(memcmp(head, want_prev, 32) == 0, "head unchanged by the refused append");
    IS(zxv_hexlog_count(log, NULL), ZXV_E_ARG);
    IS(zxv_hexlog_head(log, NULL), ZXV_E_ARG);
    IS(zxv_hexlog_count(NULL, &n), ZXV_E_ARG);
    IS(zxv_hexlog_verify(NULL), ZXV_E_ARG);

    /* Revocation and attestation stop appends. */
    zxv_hexlog *log2;
    OK(zxv_hexlog_create(cap, ZXV_HEXLOG_MAX_RECORDS, &log2));
    zxv_cap *child;
    OK(zxv_cap_attenuate(cap, ZXV_CAP_ATTEST, &child));
    OK(zxv_hexlog_append(child, log2, forb, 0, 0, subj, NULL));
    OK(zxv_cap_set_attestation(child, ZXV_LP_BOTH));
    IS(zxv_hexlog_append(child, log2, forb, 0, 0, subj, NULL), ZXV_E_CAP);
    OK(zxv_cap_revoke(child));
    IS(zxv_hexlog_append(child, log2, forb, 0, 0, subj, NULL), ZXV_E_CAP);
    zxv_cap_destroy(child);

    /* Fail closed. */
    OK(zxv_ctx_halt(ctx, "test"));
    IS(zxv_hexlog_append(cap, log, forb, 1, 42, subj, &seq), ZXV_E_POISONED);
    IS(zxv_hexlog_count(log, &n), ZXV_E_POISONED);
    IS(zxv_hexlog_head(log, head), ZXV_E_POISONED);
    IS(zxv_hexlog_at(log, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_hexlog_verify(log), ZXV_E_POISONED);
    IS(zxv_hexlog_create(cap, 1, &tmp), ZXV_E_POISONED);
    /* the pure functions take no context and keep working */
    uint8_t x;
    OK(zxv_hex_and(forb, clean, &x));

    zxv_hexlog_destroy(log2);
    zxv_hexlog_destroy(log);
    zxv_hexlog_destroy(NULL);
    zxv_cap_destroy(foreign);
    zxv_cap_destroy(noattest);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(other);
    zxv_ctx_destroy(ctx);
}

int main(void)
{
    test_constants_and_layout();
    test_lp_bridge();
    test_action_names();
    test_step_set();
    test_combine();
    test_polarity();
    test_arg_refusals();
    test_qlog();
    printf("test_zxv_abi13: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
