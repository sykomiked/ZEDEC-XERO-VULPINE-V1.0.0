/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_paradox.c — the paradox engine (paradox.h, roadmap 6.6b P0.1-P0.4).
 *
 * Seeded paradoxes of each kind; the constructive bounds, proposal and
 * rollback; destructive containment and its RepairProof, recomputed here;
 * neutral deferral that never settles early; special records that stay
 * byte-identical after their classification joins the registry; forbidden
 * over everything; complex graphs (attacker nodes, transitive taint, the
 * step limit); determinism under 1,000 shuffles and a three-peer simulation;
 * and a toy ledger where A = L + E holds after every pass. */
#include <stdio.h>
#include <string.h>
#include "paradox.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static uint64_t rng;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

static void id_of(uint32_t k, uint8_t out[32])
{
    uint8_t b[4] = {(uint8_t) k, (uint8_t) (k >> 8), (uint8_t) (k >> 16), (uint8_t) (k >> 24)};
    sha256(b, 4, out);
}

static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* independent restatements of the byte layouts in paradox.c */
static void ref_params(const int64_t *v, uint32_t n, uint8_t out[32])
{
    uint8_t b[17 + 4 + 8 * 16];
    uint32_t k = 0;
    memcpy(b, "zxv/pdx-params/v1", 17);
    k = 17;
    le32(b + k, n);
    k += 4;
    for (uint32_t i = 0; i < n; i++)
        for (int j = 0; j < 8; j++) b[k++] = (uint8_t) ((uint64_t) v[i] >> (8 * j));
    sha256(b, k, out);
}

static void ref_state(const uint8_t *s, uint32_t len, uint8_t out[32])
{
    uint8_t b[16 + 4 + 64];
    memcpy(b, "zxv/pdx-state/v1", 16);
    le32(b + 16, len);
    memcpy(b + 20, s, len);
    sha256(b, 20 + len, out);
}

static void ref_sig(const uint8_t *d, uint32_t len, uint8_t out[32])
{
    uint8_t b[14 + 64];
    memcpy(b, "zxv/pdx-sig/v1", 14);
    if (len) memcpy(b + 14, d, len);
    sha256(b, 14 + len, out);
}

static void ref_rec(const pdx_rec_t *r, uint8_t out[32])
{
    uint8_t buf[14 + 8 + 4 + 4 + 96];
    uint32_t k = 14;
    memcpy(buf, "zxv/pdx-log/v1", 14);
    for (int i = 0; i < 8; i++) buf[k++] = (uint8_t) (r->seq >> (8 * i));
    buf[k++] = r->op;
    buf[k++] = r->kind;
    buf[k++] = r->tag;
    buf[k++] = r->outcome;
    le32(buf + k, r->aux);
    k += 4;
    memcpy(buf + k, r->a, 32);
    memcpy(buf + k + 32, r->b, 32);
    memcpy(buf + k + 64, r->prev, 32);
    sha256(buf, k + 96, out);
}

static pdx_class_t cls(uint8_t kind, bool forbidden)
{
    pdx_class_t c;
    c.kind = kind;
    c.forbidden = forbidden;
    c.reg_index = -1;
    uint8_t pol = kind == PDX_KIND_CONSTRUCTIVE  ? LPX_P_NEGENTROPIC
                  : kind == PDX_KIND_DESTRUCTIVE ? LPX_P_ENTROPIC
                                                 : LPX_P_NEUTRAL;
    if (forbidden) pol = LPX_P_FORBIDDEN;
    c.tag =
        lpx_make(LPX_B_BOTH, pol, kind == PDX_KIND_SPECIAL ? LPX_PH_POTENTIAL : LPX_PH_MANIFEST);
    return c;
}

static pdx_rec_t logbuf[4096];
static pdx_log_t L;
static void fresh_log(uint32_t cap)
{
    pdx_log_init(&L, logbuf, cap);
}

/* ---------------------------------------------------------------- */
static void test_basics(void)
{
    printf("[1] names, actions, the hash-chained operator log\n");
    CHECK(pdx_out_action(PDX_OUT_APPLIED) == LPX_ACT_PROCEED &&
              pdx_out_action(PDX_OUT_KEPT) == LPX_ACT_PROCEED &&
              pdx_out_action(PDX_OUT_PROPOSAL_FOR_OWNER) == LPX_ACT_ESCALATE &&
              pdx_out_action(PDX_OUT_ROLLED_BACK) == LPX_ACT_ROLLBACK &&
              pdx_out_action(PDX_OUT_CONTAINED) == LPX_ACT_ROLLBACK &&
              pdx_out_action(PDX_OUT_DEFERRED) == LPX_ACT_WAIT &&
              pdx_out_action(PDX_OUT_SETTLED) == LPX_ACT_PROCEED &&
              pdx_out_action(PDX_OUT_RECORDED) == LPX_ACT_WAIT &&
              pdx_out_action(PDX_OUT_ACCEPTED) == LPX_ACT_PROCEED &&
              pdx_out_action(PDX_OUT_PENDING) == LPX_ACT_WAIT &&
              pdx_out_action(PDX_OUT_RESOLVED) == LPX_ACT_PROCEED &&
              pdx_out_action(PDX_OUT_ESCALATE_TO_OWNER) == LPX_ACT_ESCALATE &&
              pdx_out_action(PDX_OUT_REFUSED) == LPX_ACT_REFUSE &&
              pdx_out_action(PDX_OUT_LOG_FULL) == LPX_ACT_REFUSE &&
              pdx_out_action((pdx_out_t) 14) == LPX_ACT_REFUSE &&
              pdx_out_action((pdx_out_t) 99) == LPX_ACT_REFUSE,
          "every outcome maps to an lpres_hex action; unknown ones refuse");
    static const char *const kn[5] = {"constructive", "destructive", "neutral", "special", "none"};
    static const char *const on[14] = {"applied",     "kept",      "proposal-for-owner",
                                       "rolled-back", "contained", "deferred",
                                       "settled",     "recorded",  "accepted",
                                       "pending",     "resolved",  "escalate-to-owner",
                                       "refused",     "log-full"};
    int nm = 1;
    for (uint8_t i = 0; i < 5; i++) nm &= !strcmp(pdx_kind_name(i), kn[i]);
    for (int i = 0; i < 14; i++) nm &= !strcmp(pdx_out_name((pdx_out_t) i), on[i]);
    CHECK(nm && !strcmp(pdx_kind_name(5), "?") && !strcmp(pdx_out_name((pdx_out_t) 14), "?"),
          "names, and none past the end");

    pdx_rec_t st[4];
    pdx_log_t l;
    pdx_log_init(&l, st, 4);
    uint8_t a[32], b[32];
    id_of(1, a);
    id_of(2, b);
    int ok = 1;
    for (int i = 0; i < 4; i++)
        ok &= pdx_log_append(&l, (uint8_t) (1 + i), 2, 40, 5, (uint32_t) i * 7u, a, i & 1 ? 0 : b);
    CHECK(ok && l.n == 4 && pdx_log_verify(&l), "four records chain and verify");
    CHECK(!pdx_log_append(&l, 1, 0, 0, 0, 0, 0, 0), "a full log refuses");
    uint8_t z[32] = {0};
    CHECK(!memcmp(st[0].prev, z, 32) && !memcmp(st[1].prev, st[0].hash, 32) &&
              !memcmp(l.head, st[3].hash, 32) && !memcmp(st[1].b, z, 32),
          "links: zeros, each previous hash, the head; a NULL digest is zeros");
    /* recompute record 2's hash from its fields */
    uint8_t h[32];
    ref_rec(&st[2], h);
    CHECK(!memcmp(h, st[2].hash, 32), "record hash = SHA-256 of the documented layout");
    int caught = 1;
    for (int f = 0; f < 9; f++) {
        pdx_rec_t save = st[1];
        switch (f) {
        case 0:
            st[1].seq ^= 1;
            break;
        case 1:
            st[1].op ^= 1;
            break;
        case 2:
            st[1].kind ^= 1;
            break;
        case 3:
            st[1].tag ^= 1;
            break;
        case 4:
            st[1].outcome ^= 1;
            break;
        case 5:
            st[1].aux ^= 1;
            break;
        case 6:
            st[1].a[31] ^= 1;
            break;
        case 7:
            st[1].b[0] ^= 1;
            break;
        default:
            st[1].prev[5] ^= 1;
            break;
        }
        caught &= !pdx_log_verify(&l);
        st[1] = save;
    }
    CHECK(caught && pdx_log_verify(&l), "altering any field of a record is caught");
    st[3].hash[0] ^= 1;
    CHECK(!pdx_log_verify(&l), "an altered stored hash is caught");
    st[3].hash[0] ^= 1;
    l.head[31] ^= 1;
    CHECK(!pdx_log_verify(&l), "an altered head is caught");
    l.head[31] ^= 1;
    l.n = 3;
    CHECK(!pdx_log_verify(&l), "a truncated log no longer matches its head");
    l.n = 5;
    CHECK(!pdx_log_verify(&l), "a count past the capacity is refused");
    l.n = 4;
    /* rewrite the last record consistently (hash and head) but with a wrong
     * sequence number, or a wrong link: both still caught */
    pdx_rec_t save = st[3];
    st[3].seq = 7;
    ref_rec(&st[3], st[3].hash);
    memcpy(l.head, st[3].hash, 32);
    CHECK(!pdx_log_verify(&l), "a rehashed record with the wrong sequence number is caught");
    st[3] = save;
    st[3].prev[0] ^= 1;
    ref_rec(&st[3], st[3].hash);
    memcpy(l.head, st[3].hash, 32);
    CHECK(!pdx_log_verify(&l), "a rehashed record with a broken link is caught");
    st[3] = save;
    memcpy(l.head, st[3].hash, 32);
    CHECK(pdx_log_verify(&l), "restored");
    /* sequence numbers above one byte are hashed in full */
    static pdx_rec_t bigl[300];
    pdx_log_t bl;
    pdx_log_init(&bl, bigl, 300);
    for (int i = 0; i < 300; i++) pdx_log_append(&bl, 1, 0, 0, 0, 0x01020304u, a, b);
    ref_rec(&bigl[299], h);
    CHECK(!memcmp(h, bigl[299].hash, 32) && pdx_log_verify(&bl),
          "record 299 hashes its full seq and aux");
    CHECK(!pdx_log_append(0, 1, 0, 0, 0, 0, 0, 0) && !pdx_log_verify(0), "NULL logs refused");
    pdx_log_t e;
    pdx_log_init(&e, 0, 8);
    CHECK(e.cap == 0 && !pdx_log_append(&e, 1, 0, 0, 0, 0, 0, 0) && pdx_log_verify(&e),
          "no storage, no appends; an empty log verifies");
    pdx_log_init(0, st, 4);
}

/* ---------------------------------------------------------------- */
static uint8_t oracle_kind(uint8_t base, uint8_t pol, uint8_t phase)
{
    if (pol == LPX_P_FORBIDDEN) return PDX_KIND_NONE;
    if (base == LPX_B_NEITHER) return PDX_KIND_NEUTRAL;
    if (base != LPX_B_BOTH) return PDX_KIND_NONE;
    if (pol == LPX_P_NEGENTROPIC) return PDX_KIND_CONSTRUCTIVE;
    if (pol == LPX_P_ENTROPIC) return PDX_KIND_DESTRUCTIVE;
    return phase == LPX_PH_POTENTIAL ? PDX_KIND_SPECIAL : PDX_KIND_NEUTRAL;
}

static void test_classify(void)
{
    printf("[2] P0.1 classifier: trend, phase, registry\n");
    int ok = 1;
    for (uint32_t t = 0; t < LPX_STATES; t++)
        ok &= pdx_kind_of_tag((lpx_t) t) ==
              oracle_kind(lpx_base((lpx_t) t), lpx_pol((lpx_t) t), lpx_phase((lpx_t) t));
    CHECK(ok, "the kind of each of the 64 tags matches the rule");

    lpx_t both_m = lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    lpx_t both_p = lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    pdx_evidence_t grow = {3, 3, 9, 4, false};   /* margin 0 -> 5 */
    pdx_evidence_t shrink = {9, 2, 9, 7, false}; /* margin 7 -> 2 */
    pdx_evidence_t steady = {5, 1, 7, 3, false}; /* margin 4 -> 4 */
    pdx_evidence_t stale = {3, 3, 9, 4, true};   /* growing, but stale */
    pdx_class_t c = pdx_classify(both_m, &grow, 0, 0);
    CHECK(c.kind == PDX_KIND_CONSTRUCTIVE && lpx_pol(c.tag) == LPX_P_NEGENTROPIC &&
              lpx_base(c.tag) == LPX_B_BOTH && lpx_phase(c.tag) == LPX_PH_MANIFEST &&
              !c.forbidden && c.reg_index == -1,
          "seeded constructive: Both with a growing margin");
    c = pdx_classify(both_m, &shrink, 0, 0);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE && lpx_pol(c.tag) == LPX_P_ENTROPIC,
          "seeded destructive: Both with a shrinking margin");
    c = pdx_classify(both_m, &stale, 0, 0);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE, "stale evidence is entropic: destructive");
    c = pdx_classify(both_m, &steady, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL && lpx_pol(c.tag) == LPX_P_NEUTRAL,
          "seeded neutral: Both, steady, manifest");
    c = pdx_classify(lpx_make(LPX_B_NEITHER, LPX_P_ENTROPIC, LPX_PH_EMERGING), &shrink, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL && lpx_base(c.tag) == LPX_B_NEITHER &&
              lpx_pol(c.tag) == LPX_P_NEUTRAL,
          "seeded neutral: Neither (missing information), whatever the trend");
    c = pdx_classify(both_p, &steady, 0, 0);
    CHECK(c.kind == PDX_KIND_SPECIAL && c.tag == both_p,
          "seeded special: Both, steady, potential, no signature");
    c = pdx_classify(both_p, 0, 0, 0);
    CHECK(c.kind == PDX_KIND_SPECIAL, "no evidence at potential phase: special");
    c = pdx_classify(both_m, 0, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL, "no evidence past potential: neutral");
    c = pdx_classify(lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_EMERGING), &steady, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL, "steady at emerging phase: neutral");
    c = pdx_classify(lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST), &shrink, 0, 0);
    CHECK(c.kind == PDX_KIND_NONE && lpx_pol(c.tag) == LPX_P_ENTROPIC &&
              lpx_base(c.tag) == LPX_B_TRUE,
          "True is not a paradox; its polarity still follows the evidence");
    c = pdx_classify(lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, 0), &grow, 0, 0);
    CHECK(c.kind == PDX_KIND_NONE, "False is not a paradox");
    c = pdx_classify((lpx_t) (both_m | 0xC0u), &grow, 0, 0);
    CHECK(c.tag == lpx_make(LPX_B_BOTH, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST),
          "bits above the six are dropped");

    /* registry */
    pdx_sig_t ents[4];
    pdx_registry_t r;
    pdx_reg_init(&r, ents, 4);
    uint8_t sa[32], sb[32], sc[32];
    pdx_signature((const uint8_t *) "loop:retry-storm", 16, sa);
    pdx_signature((const uint8_t *) "tension:liquidity", 17, sb);
    pdx_signature((const uint8_t *) "never-seen", 10, sc);
    uint8_t ra[32];
    ref_sig((const uint8_t *) "loop:retry-storm", 16, ra);
    CHECK(!memcmp(sa, ra, 32), "signature = SHA-256 of \"zxv/pdx-sig/v1\" || description");
    uint8_t e0[32], e1[32];
    pdx_signature(0, 0, e0);
    ref_sig(0, 0, e1);
    CHECK(!memcmp(e0, e1, 32), "an empty description has a signature too");
    CHECK(pdx_reg_add(&r, sa, PDX_KIND_DESTRUCTIVE, 7, 0, PDX_SRC_SEED) &&
              pdx_reg_add(&r, sb, PDX_KIND_CONSTRUCTIVE, 8, PDX_DRAFT_MONEY, 3),
          "two signatures registered");
    CHECK(!pdx_reg_add(&r, sa, PDX_KIND_NEUTRAL, 0, 0, 0) && r.ent[0].kind == PDX_KIND_DESTRUCTIVE,
          "an existing signature is never edited");
    CHECK(!pdx_reg_add(&r, sc, PDX_KIND_SPECIAL, 0, 0, 0) &&
              !pdx_reg_add(&r, sc, PDX_KIND_NONE, 0, 0, 0) && !pdx_reg_add(&r, sc, 9, 0, 0, 0),
          "special, none and unknown kinds are not registry kinds");
    CHECK(pdx_reg_find(&r, sa) == 0 && pdx_reg_find(&r, sb) == 1 && pdx_reg_find(&r, sc) == -1 &&
              pdx_reg_find(0, sa) == -1 && pdx_reg_find(&r, 0) == -1,
          "lookup");
    CHECK(r.ent[1].reason == 8 && r.ent[1].flags == PDX_DRAFT_MONEY && r.ent[1].src == 3 &&
              r.ent[0].src == PDX_SRC_SEED,
          "entries keep reason, flags and source");
    c = pdx_classify(both_m, &grow, &r, sa);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE && c.reg_index == 0 && lpx_pol(c.tag) == LPX_P_ENTROPIC,
          "a known signature overrides a constructive trend");
    c = pdx_classify(both_p, &steady, &r, sb);
    CHECK(c.kind == PDX_KIND_CONSTRUCTIVE && c.reg_index == 1,
          "a known signature classifies what would be special");
    c = pdx_classify(lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, 0), 0, &r, sa);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE && lpx_base(c.tag) == LPX_B_NEITHER,
          "the registry applies to Neither too");
    c = pdx_classify(both_p, &steady, &r, sc);
    CHECK(c.kind == PDX_KIND_SPECIAL && c.reg_index == -1, "an unknown signature stays special");
    c = pdx_classify(lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, 3), &steady, &r, sa);
    CHECK(c.kind == PDX_KIND_NONE && c.reg_index == -1, "the registry never makes True a paradox");
    pdx_reg_add(&r, sc, PDX_KIND_NEUTRAL, 0, 0, 0);
    uint8_t sd[32];
    id_of(77, sd);
    CHECK(pdx_reg_add(&r, sd, PDX_KIND_NEUTRAL, 0, 0, 0) && r.n == 4 &&
              !pdx_reg_add(&r, e0, PDX_KIND_NEUTRAL, 0, 0, 0) && r.n == 4,
          "a full registry refuses");
    CHECK(!pdx_reg_add(0, sa, 0, 0, 0, 0) && !pdx_reg_add(&r, 0, 0, 0, 0, 0), "NULLs refused");
    pdx_registry_t z;
    pdx_reg_init(&z, 0, 4);
    CHECK(z.cap == 0 && !pdx_reg_add(&z, sa, 0, 0, 0, 0), "no storage, no entries");
    pdx_reg_init(0, ents, 4);

    /* forbidden is separate from the kind */
    lpx_t fb = lpx_make(LPX_B_BOTH, LPX_P_FORBIDDEN, LPX_PH_MANIFEST);
    c = pdx_classify(fb, &grow, &r, 0);
    CHECK(c.forbidden && lpx_forbidden(c.tag) && c.kind == PDX_KIND_CONSTRUCTIVE &&
              lpx_action(c.tag) == LPX_ACT_ROLLBACK,
          "forbidden stays forbidden; its kind is still read, the action is rollback");
    c = pdx_classify(fb, &grow, &r, sa);
    CHECK(c.forbidden && lpx_forbidden(c.tag) && c.kind == PDX_KIND_DESTRUCTIVE,
          "a registry hit does not clear forbidden");
    c = pdx_classify(lpx_make(LPX_B_TRUE, LPX_P_FORBIDDEN, 3), 0, 0, 0);
    CHECK(c.forbidden && c.kind == PDX_KIND_NONE, "a forbidden True is still forbidden");
}

/* ---------------------------------------------------------------- */
static void test_constructive(void)
{
    printf("[3] P0.2 constructive: bounded update, proposal, rollback\n");
    int64_t val[3] = {10, 20, 30}, prev[3] = {0, 0, 0};
    const int64_t lo[3] = {0, 10, 25}, hi[3] = {50, 40, 35};
    pdx_params_t p = {val, prev, lo, hi, 3, false, 0};
    pdx_class_t c = cls(PDX_KIND_CONSTRUCTIVE, false);
    fresh_log(64);
    int64_t s1[3] = {12, 18, 31};
    CHECK(pdx_constructive(&p, s1, 100, &c, &L) == PDX_OUT_APPLIED && val[0] == 12 &&
              val[1] == 18 && val[2] == 31 && prev[0] == 10 && prev[1] == 20 && prev[2] == 30 &&
              p.armed && p.tension_at == 100,
          "in bounds: applied, previous values kept, armed");
    uint8_t h[32], hs[32];
    ref_params(val, 3, h);
    ref_params(s1, 3, hs);
    CHECK(L.n == 1 && L.rec[0].op == PDX_OP_CONSTRUCTIVE && L.rec[0].outcome == PDX_OUT_APPLIED &&
              L.rec[0].aux == 3 && !memcmp(L.rec[0].a, h, 32) && !memcmp(L.rec[0].b, hs, 32) &&
              L.rec[0].tag == c.tag && L.rec[0].kind == PDX_KIND_CONSTRUCTIVE,
          "record: live values and proposal hashed");
    uint8_t z32[32] = {0};
    CHECK(pdx_constructive(&p, 0, 100, &c, &L) == PDX_OUT_KEPT && !p.armed && val[0] == 12 &&
              !memcmp(L.rec[L.n - 1].b, z32, 32),
          "same tension on the next evaluation: kept, disarmed; no proposal hashed");
    int64_t s2[3] = {13, 17, 32};
    pdx_constructive(&p, s2, 90, &c, &L);
    CHECK(pdx_constructive(&p, 0, 91, &c, &L) == PDX_OUT_ROLLED_BACK && val[0] == 12 &&
              val[1] == 18 && val[2] == 31 && !p.armed,
          "tension grew: rolled back automatically to the previous values");
    ref_params(val, 3, h);
    CHECK(!memcmp(L.rec[L.n - 1].a, h, 32), "the rollback record hashes the restored values");
    CHECK(pdx_constructive(&p, 0, 91, &c, &L) == PDX_OUT_KEPT && val[0] == 12,
          "a second evaluation has nothing to roll back");
    int64_t out[3] = {12, 41, 30}; /* hi[1] = 40 */
    uint32_t n0 = L.n;
    CHECK(pdx_constructive(&p, out, 5, &c, &L) == PDX_OUT_PROPOSAL_FOR_OWNER && val[1] == 18 &&
              !p.armed && p.tension_at == 90 && L.n == n0 + 1 &&
              L.rec[n0].outcome == PDX_OUT_PROPOSAL_FOR_OWNER,
          "one value above its bound: a proposal for the owner, nothing applied");
    int64_t under[3] = {-1, 18, 31};
    CHECK(pdx_constructive(&p, under, 5, &c, &L) == PDX_OUT_PROPOSAL_FOR_OWNER && val[0] == 12,
          "one value below its bound: proposal");
    int64_t edge[3] = {0, 40, 25};
    CHECK(pdx_constructive(&p, edge, 5, &c, &L) == PDX_OUT_APPLIED && val[0] == 0 && val[1] == 40,
          "the bounds themselves are inside");
    int64_t edge2[3] = {50, 10, 35};
    CHECK(pdx_constructive(&p, edge2, 5, &c, &L) == PDX_OUT_APPLIED && val[2] == 35 &&
              prev[0] == 0 && prev[1] == 40,
          "an armed update that held is replaced by the next one");
    const int64_t badlo[3] = {0, 30, 25}, badhi[3] = {50, 20, 35};
    pdx_params_t q = {val, prev, badlo, badhi, 3, false, 0};
    int64_t mid[3] = {1, 25, 30};
    CHECK(pdx_constructive(&q, mid, 0, &c, &L) == PDX_OUT_PROPOSAL_FOR_OWNER,
          "inverted published bounds: nothing is inside them");
    /* the proposal never moves values */
    int64_t big[3] = {INT64_MAX, INT64_MIN, 0};
    CHECK(pdx_constructive(&p, big, 0, &c, &L) == PDX_OUT_PROPOSAL_FOR_OWNER && val[0] == 50,
          "extreme proposals stay proposals");
    /* a refusal leaves an armed update armed */
    pdx_constructive(&p, edge, 7, &c, &L);
    pdx_class_t dk = cls(PDX_KIND_DESTRUCTIVE, false);
    CHECK(pdx_constructive(&p, s1, 0, &dk, &L) == PDX_OUT_REFUSED && p.armed && p.tension_at == 7 &&
              val[2] == 25 && prev[2] == 35,
          "a refused call changes nothing, not even the armed state");
    CHECK(pdx_constructive(&p, 0, 8, &c, &L) == PDX_OUT_ROLLED_BACK && val[2] == 35,
          "so the next evaluation can still roll it back");
    /* a published bound of one exact value */
    const int64_t elo[3] = {-5, 18, 31}, ehi[3] = {-5, 18, 31};
    pdx_params_t pe = {val, prev, elo, ehi, 3, false, 0};
    int64_t ex[3] = {-5, 18, 31};
    CHECK(pdx_constructive(&pe, ex, 0, &c, &L) == PDX_OUT_APPLIED && val[0] == -5,
          "lo == hi: exactly that value applies");
    /* large and negative values are hashed in full */
    int64_t wide[3] = {-1234567890123ll, 0x0102030405060708ll, -1};
    const int64_t wlo[3] = {INT64_MIN, INT64_MIN, INT64_MIN},
                  whi[3] = {INT64_MAX, INT64_MAX, INT64_MAX};
    pdx_params_t pw = {val, prev, wlo, whi, 3, false, 0};
    CHECK(pdx_constructive(&pw, wide, 0, &c, &L) == PDX_OUT_APPLIED, "wide values apply");
    ref_params(wide, 3, h);
    CHECK(!memcmp(L.rec[L.n - 1].a, h, 32) && !memcmp(L.rec[L.n - 1].b, h, 32),
          "every byte of a 64-bit value is hashed");
    pdx_constructive(&pw, 0, 0, &c, &L);
    pdx_constructive(&p, edge2, 5, &c, &L);
    pdx_constructive(&p, 0, 5, &c, &L);
    /* wrong kinds */
    int wrong = 1;
    for (uint8_t k = 1; k <= PDX_KIND_NONE; k++) {
        pdx_class_t w = cls(k, false);
        n0 = L.n;
        wrong &= pdx_constructive(&p, s1, 0, &w, &L) == PDX_OUT_REFUSED && val[0] == 50 &&
                 L.n == n0 + 1 && L.rec[n0].outcome == PDX_OUT_REFUSED;
    }
    CHECK(wrong, "every other kind is refused (and logged)");
    /* forbidden */
    pdx_constructive(&p, s1, 50, &c, &L);
    pdx_class_t f = cls(PDX_KIND_CONSTRUCTIVE, true);
    CHECK(pdx_constructive(&p, s2, 0, &f, &L) == PDX_OUT_ROLLED_BACK && val[0] == 50 &&
              val[1] == 10 && !p.armed,
          "forbidden: the armed update is rolled back and the new one ignored");
    CHECK(pdx_constructive(&p, s2, 0, &f, &L) == PDX_OUT_ROLLED_BACK && val[0] == 50,
          "forbidden with nothing armed: still nothing applied");
    /* full log: no change */
    pdx_rec_t one[1];
    pdx_log_t full;
    pdx_log_init(&full, one, 1);
    pdx_log_append(&full, 1, 0, 0, 0, 0, 0, 0);
    CHECK(pdx_constructive(&p, s1, 0, &c, &full) == PDX_OUT_LOG_FULL && val[0] == 50 && !p.armed,
          "a full log: halt, nothing applied");
    pdx_constructive(&p, s1, 10, &c, &L);
    CHECK(pdx_constructive(&p, 0, 99, &c, &full) == PDX_OUT_LOG_FULL && val[0] == 12 && p.armed,
          "a full log: no rollback either (the caller halts)");
    CHECK(pdx_constructive(0, s1, 0, &c, &L) == PDX_OUT_REFUSED &&
              pdx_constructive(&p, s1, 0, 0, &L) == PDX_OUT_REFUSED &&
              pdx_constructive(&p, s1, 0, &c, 0) == PDX_OUT_REFUSED,
          "NULL arguments refused");
    pdx_params_t np = {0, prev, lo, hi, 3, false, 0};
    int nul = pdx_constructive(&np, s1, 0, &c, &L) == PDX_OUT_REFUSED;
    np.val = val;
    np.prev = 0;
    nul &= pdx_constructive(&np, s1, 0, &c, &L) == PDX_OUT_REFUSED;
    np.prev = prev;
    np.lo = 0;
    nul &= pdx_constructive(&np, s1, 0, &c, &L) == PDX_OUT_REFUSED;
    np.lo = lo;
    np.hi = 0;
    nul &= pdx_constructive(&np, s1, 0, &c, &L) == PDX_OUT_REFUSED;
    CHECK(nul, "NULL arrays refused");
    CHECK(pdx_log_verify(&L), "the constructive log verifies");
}

/* ---------------------------------------------------------------- */
typedef struct {
    int64_t a, l, e; /* toy balances: A = L + E */
} toy_t;

static int inv_calls;
static bool inv_ledger(const uint8_t *s, uint32_t len, void *ctx)
{
    (void) ctx;
    inv_calls++;
    toy_t t;
    if (len != sizeof t) return false;
    memcpy(&t, s, sizeof t);
    return t.a == t.l + t.e;
}

static void test_destructive(void)
{
    printf("[4] P0.2 destructive: detect, quarantine, roll back, prove\n");
    pdx_watch_t w;
    memset(&w, 0, sizeof w);
    uint8_t s[16] = {0};
    CHECK(pdx_detect(&w, s, 16) == 0 && pdx_detect(0, s, 16) == 0, "no limits, no trips");
    w.step_limit = 10;
    w.steps = 9;
    CHECK(pdx_detect(&w, s, 16) == 0, "below the step limit");
    w.steps = 10;
    CHECK(pdx_detect(&w, s, 16) == PDX_TRIP_STEPS, "at the step limit");
    w.steps = 0;
    w.budget = 5;
    w.spent = 4;
    CHECK(pdx_detect(&w, s, 16) == 0, "below the budget");
    w.spent = 5;
    CHECK(pdx_detect(&w, s, 16) == PDX_TRIP_BUDGET, "budget exhausted");
    w.spent = 0;
    uint64_t tr[6] = {1, 2, 3, 4, 5, 6};
    w.trace = tr;
    w.trace_n = 6;
    CHECK(pdx_detect(&w, s, 16) == 0, "no repeated state");
    tr[5] = 3;
    CHECK(pdx_detect(&w, s, 16) == PDX_TRIP_CYCLE, "a repeated state is a cycle");
    tr[5] = 6;
    tr[1] = 1;
    CHECK(pdx_detect(&w, s, 16) == PDX_TRIP_CYCLE, "a repeat of the first state");
    w.trace_n = 1;
    CHECK(pdx_detect(&w, s, 16) == 0, "one state is no cycle");
    w.trace_n = 6;
    tr[1] = 2;
    tr[5] = 5;
    CHECK(pdx_detect(&w, s, 16) == PDX_TRIP_CYCLE, "adjacent repeats");
    tr[5] = 6;
    tr[2] = 1;
    w.trace = 0;
    CHECK(pdx_detect(&w, s, 16) == 0, "a count with no trace is no cycle (and no read)");
    tr[2] = 3;

    toy_t ck = {100, 60, 40}, live = {100, 60, 40};
    w.invariant = inv_ledger;
    w.ctx = 0;
    w.trace = tr;
    CHECK(pdx_detect(&w, (const uint8_t *) &live, sizeof live) == 0, "the invariant holds");
    live.a = 101; /* A != L + E */
    CHECK(pdx_detect(&w, (const uint8_t *) &live, sizeof live) == PDX_TRIP_INVARIANT,
          "the invariant callback catches A != L + E");

    /* contain */
    lpx_qrec_t qs[8];
    lpx_qlog_t q;
    lpx_qlog_init(&q, qs, 8);
    fresh_log(64);
    w.steps = 12;
    w.spent = 3;
    pdx_class_t c = cls(PDX_KIND_DESTRUCTIVE, false);
    pdx_repair_t pr;
    memset(&pr, 0xAA, sizeof pr);
    uint8_t before[32], after[32];
    ref_state((const uint8_t *) &live, sizeof live, before);
    ref_state((const uint8_t *) &ck, sizeof ck, after);
    const char *pat = "posting:a-without-l";
    inv_calls = 0;
    pdx_out_t o = pdx_destructive(&c, &w, (uint8_t *) &live, (const uint8_t *) &ck, sizeof live,
                                  (const uint8_t *) pat, 19, 0xDEADBEEF, &q, &L, &pr);
    CHECK(o == PDX_OUT_CONTAINED && pdx_out_action(o) == LPX_ACT_ROLLBACK, "contained");
    CHECK(live.a == 100 && live.l == 60 && live.e == 40 && live.a == live.l + live.e,
          "rolled back to the checkpoint: A = L + E again");
    CHECK(pr.trips == (PDX_TRIP_STEPS | PDX_TRIP_INVARIANT) && pr.steps == 12 && inv_calls == 1,
          "the proof names the checks that tripped and the step count");
    CHECK(!memcmp(pr.before, before, 32) && !memcmp(pr.after, after, 32),
          "before/after = SHA-256 of the state as found and as restored");
    uint8_t sg[32];
    ref_sig((const uint8_t *) pat, 19, sg);
    CHECK(!memcmp(pr.sig, sg, 32), "the signature for peers = SHA-256 of the pattern");
    uint8_t buf[17 + 24 + 96], what[32];
    memcpy(buf, "zxv/pdx-repair/v1", 17);
    le32(buf + 17, pr.trips);
    le32(buf + 21, 12);
    le32(buf + 25, 3);
    le32(buf + 29, 6);
    le32(buf + 33, 0xDEADBEEF);
    le32(buf + 37, sizeof live);
    memcpy(buf + 41, before, 32);
    memcpy(buf + 73, after, 32);
    memcpy(buf + 105, sg, 32);
    sha256(buf, 137, what);
    CHECK(!memcmp(pr.what, what, 32), "RepairProof.what = SHA-256 of the documented layout");
    CHECK(q.n == 1 && pr.qseq == 0 && lpx_forbidden(qs[0].tag) &&
              lpx_base(qs[0].tag) == LPX_B_BOTH && qs[0].reason == (uint8_t) pr.trips &&
              qs[0].submitter == 0xDEADBEEF && !memcmp(qs[0].subject, pr.what, 32) &&
              lpx_qlog_verify(&q),
          "quarantined in lpres_hex's hashed log: forbidden tag, trips, submitter, proof");
    CHECK(L.n == 1 && L.rec[0].op == PDX_OP_DESTRUCTIVE && L.rec[0].outcome == PDX_OUT_CONTAINED &&
              L.rec[0].aux == 12 && !memcmp(L.rec[0].a, pr.what, 32) &&
              !memcmp(L.rec[0].b, pr.sig, 32),
          "operator record: steps, proof, signature");
    /* a peer learns the signature: the next one is destructive whatever its trend */
    pdx_sig_t pe[2];
    pdx_registry_t peer;
    pdx_reg_init(&peer, pe, 2);
    CHECK(pdx_reg_add(&peer, pr.sig, PDX_KIND_DESTRUCTIVE, (uint8_t) pr.trips, 0, PDX_SRC_SEED),
          "a peer registers the shared signature");
    pdx_evidence_t grow = {1, 1, 9, 1, false};
    pdx_class_t pc = pdx_classify(lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, 3), &grow, &peer, sg);
    CHECK(pc.kind == PDX_KIND_DESTRUCTIVE, "the peer recognises it although its trend looks good");

    /* no reflection path: the sender's mailbox is never touched */
    uint8_t mailbox[64];
    memset(mailbox, 0x5A, sizeof mailbox);
    pdx_watch_t w2;
    memset(&w2, 0, sizeof w2);
    w2.budget = 1;
    w2.spent = 1;
    w2.ctx = mailbox; /* a caller that wrongly hands over the sender's buffer */
    w2.invariant = inv_ledger;
    live.l = 0;
    int untouched = pdx_destructive(&c, &w2, (uint8_t *) &live, (const uint8_t *) &ck, sizeof live,
                                    0, 0, 7, &q, &L, &pr) == PDX_OUT_CONTAINED;
    for (uint32_t i = 0; i < sizeof mailbox; i++) untouched &= mailbox[i] == 0x5A;
    CHECK(untouched && live.l == 60,
          "contain only: nothing is written toward the sender (no reply, no reflection)");
    CHECK(sizeof(pdx_repair_t) == 4 * 32 + 4 + 4 + 8,
          "the proof holds hashes, counts and a quarantine index: no route or address");
    uint8_t e[32];
    ref_sig(0, 0, e);
    CHECK(!memcmp(pr.sig, e, 32) && pr.trips == (PDX_TRIP_BUDGET | PDX_TRIP_INVARIANT),
          "no pattern: the empty signature; budget trip");

    /* kind destructive with no trips is still contained */
    memset(&w2, 0, sizeof w2);
    uint8_t st8[8] = {1, 2, 3, 4, 5, 6, 7, 8}, ck8[8] = {0};
    CHECK(pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_CONTAINED &&
              st8[7] == 0 && pr.trips == 0 && pr.steps == 0,
          "classified destructive (trend or registry) is contained without a trip");
    st8[1] = 4;
    CHECK(pdx_destructive(&c, 0, st8, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_CONTAINED &&
              pr.steps == 0,
          "no watch at all: still contained");
    {
        uint8_t bb[137], wh[32], b4[32], a4[32], e4[32];
        uint8_t was[8] = {0, 4, 0, 0, 0, 0, 0, 0};
        ref_state(was, 8, b4);
        ref_state(ck8, 8, a4);
        ref_sig(0, 0, e4);
        memcpy(bb, "zxv/pdx-repair/v1", 17);
        le32(bb + 17, 0);
        le32(bb + 21, 0);
        le32(bb + 25, 0);
        le32(bb + 29, 0);
        le32(bb + 33, 1);
        le32(bb + 37, 8);
        memcpy(bb + 41, b4, 32);
        memcpy(bb + 73, a4, 32);
        memcpy(bb + 105, e4, 32);
        sha256(bb, 137, wh);
        CHECK(!memcmp(pr.what, wh, 32), "with no watch, spent and trace count hash as zero");
    }
    /* not destructive and nothing tripped: refused, untouched */
    pdx_class_t n = cls(PDX_KIND_NEUTRAL, false);
    st8[0] = 9;
    uint32_t qn = q.n, ln = L.n;
    CHECK(pdx_destructive(&n, &w2, st8, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_REFUSED &&
              st8[0] == 9 && q.n == qn && L.n == ln + 1 && L.rec[ln].outcome == PDX_OUT_REFUSED &&
              L.rec[ln].aux == 0,
          "a neutral with no trip is refused and left alone");
    /* a neutral that trips a check is contained */
    w2.step_limit = 1;
    w2.steps = 1;
    CHECK(pdx_destructive(&n, &w2, st8, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_CONTAINED &&
              st8[0] == 0 && pr.trips == PDX_TRIP_STEPS,
          "any kind that trips a check is contained");
    /* forbidden */
    pdx_class_t f = cls(PDX_KIND_CONSTRUCTIVE, true);
    memset(&w2, 0, sizeof w2);
    st8[0] = 9;
    CHECK(pdx_destructive(&f, &w2, st8, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_CONTAINED &&
              st8[0] == 0 && pr.trips == PDX_TRIP_FORBIDDEN,
          "forbidden: quarantined and rolled back whatever its kind");
    CHECK(lpx_qlog_verify(&q) && pdx_log_verify(&L), "both logs verify");
    /* full logs */
    lpx_qlog_t qf;
    lpx_qrec_t q1[1];
    lpx_qlog_init(&qf, q1, 0);
    st8[0] = 9;
    ln = L.n;
    CHECK(pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, &qf, &L, &pr) == PDX_OUT_LOG_FULL &&
              st8[0] == 0 && L.n == ln,
          "a full quarantine log: state still restored, then halt");
    pdx_rec_t r1[1];
    pdx_log_t lf;
    pdx_log_init(&lf, r1, 0);
    st8[0] = 9;
    qn = q.n;
    CHECK(pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, &q, &lf, &pr) == PDX_OUT_LOG_FULL &&
              st8[0] == 0 && q.n == qn,
          "a full operator log: state restored, nothing quarantined, halt");
    CHECK(pdx_destructive(&n, &w2, st8, ck8, 8, 0, 0, 1, &q, &lf, &pr) == PDX_OUT_LOG_FULL,
          "even a refusal needs its record");
    pdx_rec_t r2[2];
    pdx_log_init(&lf, r2, 2);
    pdx_log_append(&lf, 1, 0, 0, 0, 0, 0, 0);
    lpx_qrec_t q2[2];
    lpx_qlog_init(&qf, q2, 2);
    lpx_qlog_append(&qf, lpx_forbid(0), 0, 0, pr.what);
    st8[0] = 9;
    CHECK(pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, &qf, &lf, &pr) == PDX_OUT_CONTAINED &&
              lf.n == 2 && qf.n == 2 && pr.qseq == 1,
          "exactly one free slot in each log is enough");
    CHECK(pdx_destructive(0, &w2, st8, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_REFUSED &&
              pdx_destructive(&c, &w2, 0, ck8, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_REFUSED &&
              pdx_destructive(&c, &w2, st8, 0, 8, 0, 0, 1, &q, &L, &pr) == PDX_OUT_REFUSED &&
              pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, 0, &L, &pr) == PDX_OUT_REFUSED &&
              pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, &q, 0, &pr) == PDX_OUT_REFUSED &&
              pdx_destructive(&c, &w2, st8, ck8, 8, 0, 0, 1, &q, &L, 0) == PDX_OUT_REFUSED &&
              pdx_destructive(&c, &w2, st8, ck8, 8, 0, 3, 1, &q, &L, &pr) == PDX_OUT_REFUSED,
          "NULL arguments refused");
}

/* ---------------------------------------------------------------- */
static void test_neutral(void)
{
    printf("[5] P0.2 neutral: every branch kept, nothing settles early\n");
    pdx_branch_t bs[3];
    pdx_hold_t h;
    pdx_hold_init(&h, bs, 3, 3);
    uint8_t ta[32], tb[32], tc[32], td[32];
    id_of(100, ta);
    id_of(101, tb);
    id_of(102, tc);
    id_of(103, td);
    CHECK(pdx_hold_decisive(&h) == -1, "no candidates, nothing decisive");
    CHECK(pdx_hold_add(&h, ta) && pdx_hold_add(&h, tb) && pdx_hold_add(&h, ta) && h.n == 2,
          "two candidates (peer timestamps); a duplicate is the same one");
    pdx_class_t c = cls(PDX_KIND_NEUTRAL, false);
    fresh_log(512);
    /* seeded: interleaved evidence that never gets a 3-lead */
    rng = 0x1234567ull;
    int early = 0;
    for (int i = 0; i < 200; i++) {
        bool to_a = rndn(2) == 0;
        int32_t lead = (int32_t) h.br[0].pos - (int32_t) h.br[1].pos;
        if (lead >= 2) to_a = false;
        if (lead <= -2) to_a = true;
        pdx_hold_evidence(&h, to_a ? ta : tb, true);
        pdx_out_t o = pdx_neutral(&h, &c, -1, &L);
        early |= o != PDX_OUT_DEFERRED || h.settled != -1 || h.n != 2;
    }
    CHECK(!early, "200 rounds of close evidence: deferred every time, both branches kept");
    CHECK(L.n == 200 && L.rec[199].outcome == PDX_OUT_DEFERRED && L.rec[199].aux == 2,
          "each deferral is a record");
    /* now make a decisive */
    pdx_branch_t b2[2];
    pdx_hold_t g;
    pdx_hold_init(&g, b2, 2, 0);
    CHECK(g.decisive == 1, "a decisive margin of 0 is taken as 1");
    pdx_branch_t b3[3];
    pdx_hold_init(&g, b3, 3, 3);
    pdx_hold_add(&g, ta);
    pdx_hold_add(&g, tb);
    pdx_hold_evidence(&g, ta, true);
    pdx_hold_evidence(&g, ta, true);
    CHECK(pdx_neutral(&g, &c, -1, &L) == PDX_OUT_DEFERRED, "a lead of 2 < 3: deferred");
    pdx_hold_evidence(&g, tb, false);
    CHECK(pdx_hold_decisive(&g) == 0, "a lead of 3 (2 for, 1 against the other) is decisive");
    pdx_hold_evidence(&g, tb, true);
    CHECK(pdx_hold_decisive(&g) == -1, "evidence for the other takes it back");
    pdx_hold_evidence(&g, ta, true);
    CHECK(pdx_neutral(&g, &c, -1, &L) == PDX_OUT_SETTLED && g.settled == 0 &&
              L.rec[L.n - 1].aux == 0 && !memcmp(L.rec[L.n - 1].b, ta, 32),
          "decisive evidence settles on that branch; the record names it");
    CHECK(!pdx_hold_add(&g, tc) && g.n == 2 && !pdx_hold_evidence(&g, tb, true) &&
              pdx_neutral(&g, &c, 1, &L) == PDX_OUT_SETTLED && g.settled == 0,
          "once settled it stays settled; the owner cannot flip it silently");
    /* the second candidate can be the decisive one */
    pdx_hold_init(&g, b2, 2, 1);
    pdx_hold_add(&g, ta);
    pdx_hold_add(&g, tb);
    pdx_hold_evidence(&g, ta, false);
    CHECK(pdx_hold_decisive(&g) == 1, "against the first is a lead for the second");
    /* single candidate */
    pdx_hold_init(&g, b2, 2, 2);
    pdx_hold_add(&g, tc);
    pdx_hold_evidence(&g, tc, true);
    CHECK(pdx_hold_decisive(&g) == -1, "alone, net 1 < 2");
    pdx_hold_evidence(&g, tc, true);
    CHECK(pdx_hold_decisive(&g) == 0, "alone, net 2 is decisive");
    pdx_hold_init(&g, b2, 2, 1);
    pdx_hold_add(&g, tc);
    pdx_hold_evidence(&g, tc, false);
    CHECK(pdx_hold_decisive(&g) == -1, "alone with evidence against: never decisive");
    /* owner intent */
    pdx_hold_init(&g, b2, 2, 100);
    pdx_hold_add(&g, ta);
    pdx_hold_add(&g, tb);
    CHECK(pdx_neutral(&g, &c, 2, &L) == PDX_OUT_DEFERRED &&
              pdx_neutral(&g, &c, -5, &L) == PDX_OUT_DEFERRED,
          "an owner pick out of range is no pick");
    CHECK(pdx_neutral(&g, &c, 1, &L) == PDX_OUT_SETTLED && g.settled == 1,
          "the owner's intent settles without decisive evidence");
    pdx_hold_init(&g, b2, 2, 1);
    pdx_hold_add(&g, ta);
    pdx_hold_add(&g, tb);
    pdx_hold_evidence(&g, tb, true);
    CHECK(pdx_hold_decisive(&g) == 1 && pdx_neutral(&g, &c, 0, &L) == PDX_OUT_SETTLED &&
              g.settled == 0,
          "the owner's pick (index 0) wins over the evidence");
    /* forbidden and wrong kinds */
    pdx_hold_init(&g, b2, 2, 1);
    pdx_hold_add(&g, ta);
    pdx_hold_evidence(&g, ta, true);
    pdx_hold_evidence(&g, ta, true);
    pdx_class_t f = cls(PDX_KIND_NEUTRAL, true);
    CHECK(pdx_neutral(&g, &f, 0, &L) == PDX_OUT_ROLLED_BACK && g.settled == -1,
          "forbidden: nothing settles, not even with decisive evidence and an owner pick");
    int wrong = 1;
    for (uint8_t k = 0; k <= PDX_KIND_NONE; k++) {
        if (k == PDX_KIND_NEUTRAL) continue;
        pdx_class_t w = cls(k, false);
        wrong &= pdx_neutral(&g, &w, 0, &L) == PDX_OUT_REFUSED && g.settled == -1;
    }
    CHECK(wrong, "other kinds are refused");
    /* capacity, unknowns, saturation, log full, NULLs */
    pdx_hold_init(&g, b2, 2, 1);
    CHECK(pdx_hold_add(&g, ta) && pdx_hold_add(&g, tb) && !pdx_hold_add(&g, tc) && g.n == 2,
          "a full hold refuses a third candidate");
    CHECK(!pdx_hold_evidence(&g, td, true), "evidence for an unknown candidate is refused");
    g.br[0].pos = UINT32_MAX;
    CHECK(pdx_hold_evidence(&g, ta, true) && g.br[0].pos == UINT32_MAX, "counts saturate");
    g.br[1].neg = UINT32_MAX;
    CHECK(pdx_hold_evidence(&g, tb, false) && g.br[1].neg == UINT32_MAX, "both counts saturate");
    pdx_rec_t r1[1];
    pdx_log_t lf;
    pdx_log_init(&lf, r1, 0);
    CHECK(pdx_neutral(&g, &c, 0, &lf) == PDX_OUT_LOG_FULL && g.settled == -1,
          "a full log: nothing settles");
    CHECK(!pdx_hold_add(0, ta) && !pdx_hold_add(&g, 0) && !pdx_hold_evidence(0, ta, true) &&
              !pdx_hold_evidence(&g, 0, true) && pdx_hold_decisive(0) == -1 &&
              pdx_neutral(0, &c, 0, &L) == PDX_OUT_REFUSED &&
              pdx_neutral(&g, 0, 0, &L) == PDX_OUT_REFUSED &&
              pdx_neutral(&g, &c, 0, 0) == PDX_OUT_REFUSED,
          "NULL arguments refused");
    pdx_hold_t e;
    pdx_hold_init(&e, 0, 4, 1);
    CHECK(e.cap == 0 && !pdx_hold_add(&e, ta) && pdx_neutral(&e, &c, -1, &L) == PDX_OUT_DEFERRED,
          "no storage: no candidates, deferred");
    e.n = 1;
    CHECK(pdx_neutral(&e, &c, -1, &L) == PDX_OUT_REFUSED, "a count without storage is refused");
    pdx_hold_init(0, b2, 2, 1);
    /* the branch digest */
    pdx_hold_init(&g, b2, 2, 5);
    pdx_hold_add(&g, ta);
    pdx_hold_add(&g, tb);
    pdx_hold_evidence(&g, tb, false);
    pdx_neutral(&g, &c, -1, &L);
    uint8_t buf[19 + 4 + 2 * 40], hh[32];
    memcpy(buf, "zxv/pdx-branches/v1", 19);
    le32(buf + 19, 2);
    memcpy(buf + 23, ta, 32);
    le32(buf + 55, 0);
    le32(buf + 59, 0);
    memcpy(buf + 63, tb, 32);
    le32(buf + 95, 0);
    le32(buf + 99, 1);
    sha256(buf, 103, hh);
    uint8_t z[32] = {0};
    CHECK(!memcmp(L.rec[L.n - 1].a, hh, 32) && !memcmp(L.rec[L.n - 1].b, z, 32),
          "deferral record: the branch set hashed, no settled id");
    CHECK(pdx_log_verify(&L), "the neutral log verifies");
}

/* ---------------------------------------------------------------- */
static void test_special(void)
{
    printf("[6] P0.2 special: permanent record, draft, review, registry\n");
    pdx_rec_t fs[16];
    pdx_log_t fr;
    pdx_log_init(&fr, fs, 16);
    fresh_log(64);
    pdx_sig_t es[4];
    pdx_registry_t reg;
    pdx_reg_init(&reg, es, 4);
    uint8_t sig[32], subj[32];
    pdx_signature((const uint8_t *) "anomaly:clock-skew-burst", 24, sig);
    id_of(500, subj);
    pdx_evidence_t steady = {4, 4, 6, 6, false};
    pdx_class_t c =
        pdx_classify(lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_POTENTIAL), &steady, &reg, sig);
    CHECK(c.kind == PDX_KIND_SPECIAL, "a new anomaly with no trend and no signature is special");
    uint32_t rec = 99;
    CHECK(pdx_special(&fr, &c, sig, subj, &L, &rec) == PDX_OUT_RECORDED && rec == 0 && fr.n == 1 &&
              L.n == 1,
          "recorded on the frontier and in the operator log");
    CHECK(fs[0].op == PDX_OP_SPECIAL && !memcmp(fs[0].a, sig, 32) && !memcmp(fs[0].b, subj, 32) &&
              fs[0].tag == c.tag && fs[0].aux == 0 && L.rec[0].aux == 0 &&
              !memcmp(L.rec[0].b, fs[0].hash, 32),
          "the frontier record holds the signature and subject; the log points at it");
    pdx_rec_t orig = fs[0];
    uint8_t head0[32];
    memcpy(head0, fr.head, 32);
    pdx_draft_t d;
    CHECK(pdx_draft(&fr, 0, PDX_KIND_SPECIAL, 1, 0, &d, &L) == PDX_OUT_REFUSED &&
              pdx_draft(&fr, 0, PDX_KIND_NONE, 1, 0, &d, &L) == PDX_OUT_REFUSED &&
              pdx_draft(&fr, 1, PDX_KIND_NEUTRAL, 1, 0, &d, &L) == PDX_OUT_REFUSED,
          "a draft must name a real kind and an existing record");
    CHECK(pdx_draft(&fr, 0, PDX_KIND_DESTRUCTIVE, 42, 0, &d, &L) == PDX_OUT_RECORDED &&
              d.rec == 0 && d.kind == PDX_KIND_DESTRUCTIVE && d.reason == 42 && d.flags == 0 &&
              !memcmp(d.sig, sig, 32) && !memcmp(d.rec_hash, fs[0].hash, 32),
          "a draft classification (kind + reason code) for review");
    CHECK(L.rec[L.n - 1].op == PDX_OP_DRAFT && L.rec[L.n - 1].outcome == PDX_OUT_RECORDED &&
              L.rec[L.n - 1].aux == 0 && L.rec[L.n - 1].kind == PDX_KIND_DESTRUCTIVE,
          "the draft is logged");
    CHECK(pdx_review(&reg, &fr, &d, false, false, &L) == PDX_OUT_PROPOSAL_FOR_OWNER && reg.n == 0,
          "an unapproved draft stays a proposal");
    CHECK(pdx_review(&reg, &fr, &d, false, true, &L) == PDX_OUT_ACCEPTED && reg.n == 1 &&
              reg.ent[0].kind == PDX_KIND_DESTRUCTIVE && reg.ent[0].src == 0 &&
              reg.ent[0].reason == 42 && !memcmp(reg.ent[0].sig, sig, 32),
          "a non-money draft passes by consensus: a NEW registry entry from record 0");
    CHECK(!memcmp(&orig, &fs[0], sizeof orig) && !memcmp(head0, fr.head, 32) && fr.n == 1 &&
              pdx_log_verify(&fr),
          "the original frontier record is byte-identical after classification");
    c = pdx_classify(lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_POTENTIAL), &steady, &reg, sig);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE && c.reg_index == 0,
          "the next one like it is classified by the registry");
    CHECK(pdx_review(&reg, &fr, &d, true, true, &L) == PDX_OUT_REFUSED && reg.n == 1,
          "a second review cannot edit the entry");

    /* money / device drafts need the owner */
    uint8_t sig2[32];
    pdx_signature((const uint8_t *) "anomaly:fee-oscillation", 23, sig2);
    pdx_class_t c2 = cls(PDX_KIND_SPECIAL, false);
    pdx_special(&fr, &c2, sig2, subj, &L, &rec);
    CHECK(rec == 1, "a second frontier record");
    pdx_draft(&fr, 1, PDX_KIND_CONSTRUCTIVE, 3, PDX_DRAFT_MONEY, &d, &L);
    CHECK(pdx_review(&reg, &fr, &d, false, true, &L) == PDX_OUT_PROPOSAL_FOR_OWNER && reg.n == 1,
          "a money draft: consensus is not enough");
    CHECK(pdx_review(&reg, &fr, &d, true, false, &L) == PDX_OUT_ACCEPTED && reg.n == 2 &&
              reg.ent[1].flags == PDX_DRAFT_MONEY && reg.ent[1].src == 1,
          "the owner approves it");
    uint8_t sig3[32];
    pdx_signature((const uint8_t *) "anomaly:actuator-echo", 21, sig3);
    pdx_special(&fr, &c2, sig3, subj, &L, 0);
    pdx_draft(&fr, 2, PDX_KIND_NEUTRAL, 4, PDX_DRAFT_DEVICE, &d, &L);
    CHECK(pdx_review(&reg, &fr, &d, false, true, &L) == PDX_OUT_PROPOSAL_FOR_OWNER,
          "a device draft: consensus is not enough");
    /* tampering */
    pdx_draft_t t = d;
    t.rec_hash[0] ^= 1;
    CHECK(pdx_review(&reg, &fr, &t, true, true, &L) == PDX_OUT_REFUSED, "a forged record hash");
    t = d;
    t.sig[3] ^= 1;
    CHECK(pdx_review(&reg, &fr, &t, true, true, &L) == PDX_OUT_REFUSED, "a swapped signature");
    t = d;
    t.kind = PDX_KIND_SPECIAL;
    CHECK(pdx_review(&reg, &fr, &t, true, true, &L) == PDX_OUT_REFUSED, "a draft kind of special");
    t = d;
    t.rec = 9;
    CHECK(pdx_review(&reg, &fr, &t, true, true, &L) == PDX_OUT_REFUSED,
          "a record that is not there");
    fs[0].b[0] ^= 1; /* someone edits an old frontier record */
    CHECK(pdx_review(&reg, &fr, &d, true, true, &L) == PDX_OUT_REFUSED && !pdx_log_verify(&fr),
          "an edited frontier refuses every review");
    fs[0].b[0] ^= 1;
    CHECK(pdx_review(&reg, &fr, &d, true, false, &L) == PDX_OUT_ACCEPTED && reg.n == 3,
          "restored, the device draft passes with the owner");
    /* a frontier record that is not a special record */
    pdx_log_append(&fr, PDX_OP_GRAPH, 0, 0, 0, 0, 0, 0);
    CHECK(pdx_draft(&fr, 3, PDX_KIND_NEUTRAL, 0, 0, &d, &L) == PDX_OUT_REFUSED,
          "only special records can be drafted");
    /* registry full */
    uint8_t sig4[32];
    pdx_signature((const uint8_t *) "x", 1, sig4);
    pdx_special(&fr, &c2, sig4, subj, &L, &rec);
    pdx_draft(&fr, rec, PDX_KIND_NEUTRAL, 0, 0, &d, &L);
    pdx_reg_add(&reg, subj, PDX_KIND_NEUTRAL, 0, 0, 0);
    CHECK(reg.n == 4 && pdx_review(&reg, &fr, &d, true, true, &L) == PDX_OUT_REFUSED,
          "a full registry refuses");
    /* forbidden and wrong kinds */
    uint32_t fn = fr.n;
    pdx_class_t f = cls(PDX_KIND_SPECIAL, true);
    CHECK(pdx_special(&fr, &f, sig, subj, &L, 0) == PDX_OUT_ROLLED_BACK && fr.n == fn,
          "forbidden: nothing recorded on the frontier");
    pdx_class_t nn = cls(PDX_KIND_NEUTRAL, false);
    CHECK(pdx_special(&fr, &nn, sig, subj, &L, 0) == PDX_OUT_REFUSED && fr.n == fn &&
              L.rec[L.n - 1].aux == 0 && L.rec[L.n - 1].outcome == PDX_OUT_REFUSED,
          "other kinds are refused");
    {
        pdx_rec_t f1[1], l1[1];
        pdx_log_t ff, ll;
        pdx_log_init(&ff, f1, 1);
        pdx_log_init(&ll, l1, 1);
        CHECK(pdx_special(&ff, &c2, sig, subj, &ll, 0) == PDX_OUT_RECORDED && ff.n == 1 &&
                  ll.n == 1,
              "exactly one free slot in the frontier and in the log is enough");
    }
    /* full logs */
    pdx_rec_t r1[1];
    pdx_log_t lf;
    pdx_log_init(&lf, r1, 0);
    CHECK(pdx_special(&fr, &c2, sig, subj, &lf, 0) == PDX_OUT_LOG_FULL && fr.n == fn,
          "a full operator log: nothing on the frontier either");
    CHECK(pdx_special(&fr, &nn, sig, subj, &lf, 0) == PDX_OUT_LOG_FULL,
          "even a refusal needs room");
    CHECK(pdx_special(&lf, &c2, sig, subj, &L, 0) == PDX_OUT_LOG_FULL, "a full frontier");
    pdx_rec_t r2[3];
    pdx_log_t same;
    pdx_log_init(&same, r2, 2);
    CHECK(pdx_special(&same, &c2, sig, subj, &same, &rec) == PDX_OUT_RECORDED && same.n == 2 &&
              rec == 0 && pdx_log_verify(&same),
          "frontier and log may be one log: two records");
    same.cap = 3;
    CHECK(pdx_special(&same, &c2, sig, subj, &same, &rec) == PDX_OUT_LOG_FULL && same.n == 2,
          "one log with room for one record refuses the pair");
    pdx_log_init(&lf, r1, 0);
    CHECK(pdx_draft(&fr, 0, PDX_KIND_NEUTRAL, 0, 0, &d, &lf) == PDX_OUT_LOG_FULL,
          "a draft needs its record");
    pdx_draft(&fr, 0, PDX_KIND_NEUTRAL, 0, 0, &d, &L);
    CHECK(pdx_review(&reg, &fr, &d, true, true, &lf) == PDX_OUT_LOG_FULL,
          "a review needs its record");
    CHECK(pdx_special(0, &c2, sig, subj, &L, 0) == PDX_OUT_REFUSED &&
              pdx_special(&fr, 0, sig, subj, &L, 0) == PDX_OUT_REFUSED &&
              pdx_special(&fr, &c2, 0, subj, &L, 0) == PDX_OUT_REFUSED &&
              pdx_special(&fr, &c2, sig, 0, &L, 0) == PDX_OUT_REFUSED &&
              pdx_special(&fr, &c2, sig, subj, 0, 0) == PDX_OUT_REFUSED &&
              pdx_draft(0, 0, PDX_KIND_NEUTRAL, 0, 0, &d, &L) == PDX_OUT_REFUSED &&
              pdx_draft(&fr, 0, PDX_KIND_NEUTRAL, 0, 0, 0, &L) == PDX_OUT_REFUSED &&
              pdx_draft(&fr, 0, PDX_KIND_NEUTRAL, 0, 0, &d, 0) == PDX_OUT_REFUSED &&
              pdx_review(0, &fr, &d, true, true, &L) == PDX_OUT_REFUSED &&
              pdx_review(&reg, 0, &d, true, true, &L) == PDX_OUT_REFUSED &&
              pdx_review(&reg, &fr, 0, true, true, &L) == PDX_OUT_REFUSED &&
              pdx_review(&reg, &fr, &d, true, true, 0) == PDX_OUT_REFUSED,
          "NULL arguments refused");
    pdx_log_t big = fr;
    big.cap = 2; /* n beyond cap */
    CHECK(pdx_draft(&big, 2, PDX_KIND_NEUTRAL, 0, 0, &d, &L) == PDX_OUT_REFUSED,
          "a record index past the capacity is refused");
    pdx_log_t cut = fr;
    cut.n = 2; /* record 2 is still in storage, past the count */
    CHECK(fs[2].op == PDX_OP_SPECIAL &&
              pdx_draft(&cut, 2, PDX_KIND_NEUTRAL, 0, 0, &d, &L) == PDX_OUT_REFUSED,
          "a record index at the count is refused");
    CHECK(pdx_log_verify(&L) && pdx_log_verify(&fr), "both logs verify");
}

/* ---------------------------------------------------------------- */
static void test_forbidden(void)
{
    printf("[7] forbidden overrides every kind and every operator\n");
    int ok = 1;
    int64_t val[2] = {5, 5}, prev[2] = {0, 0};
    const int64_t lo[2] = {0, 0}, hi[2] = {10, 10};
    int64_t syn[2] = {6, 6};
    pdx_branch_t bs[2];
    pdx_rec_t fs[4];
    pdx_log_t fr;
    lpx_qrec_t qs[64];
    lpx_qlog_t q;
    lpx_qlog_init(&q, qs, 64);
    uint8_t a[32];
    id_of(1, a);
    pdx_evidence_t evs[3] = {{1, 1, 9, 1, false}, {9, 1, 1, 1, false}, {2, 1, 3, 2, false}};
    for (uint32_t t = 0; t < LPX_STATES; t++) {
        if (!lpx_forbidden((lpx_t) t)) continue;
        for (int e = 0; e < 3; e++) {
            pdx_class_t c = pdx_classify((lpx_t) t, &evs[e], 0, 0);
            ok &= c.forbidden && lpx_action(c.tag) == LPX_ACT_ROLLBACK;
            fresh_log(16);
            pdx_params_t p = {val, prev, lo, hi, 2, false, 0};
            ok &= pdx_constructive(&p, syn, 0, &c, &L) == PDX_OUT_ROLLED_BACK && val[0] == 5;
            pdx_hold_t h;
            pdx_hold_init(&h, bs, 2, 1);
            pdx_hold_add(&h, a);
            pdx_hold_evidence(&h, a, true);
            ok &= pdx_neutral(&h, &c, 0, &L) == PDX_OUT_ROLLED_BACK && h.settled == -1;
            pdx_log_init(&fr, fs, 4);
            ok &= pdx_special(&fr, &c, a, a, &L, 0) == PDX_OUT_ROLLED_BACK && fr.n == 0;
            uint8_t s = 1, ck = 0;
            pdx_repair_t pr;
            ok &= pdx_destructive(&c, 0, &s, &ck, 1, 0, 0, 0, &q, &L, &pr) == PDX_OUT_CONTAINED &&
                  s == 0 && (pr.trips & PDX_TRIP_FORBIDDEN);
            for (uint32_t i = 0; i < L.n; i++)
                ok &= pdx_out_action((pdx_out_t) L.rec[i].outcome) == LPX_ACT_ROLLBACK;
        }
    }
    CHECK(ok, "all 16 forbidden tags x 3 trends: no operator applies, settles or records");
    CHECK(q.n == 48 && lpx_qlog_verify(&q), "each one quarantined once by the destructive path");
}

/* ---------------------------------------------------------------- */
static void mknode(pdx_node_t *x, uint32_t k, uint8_t kind, uint32_t mag, int8_t dir)
{
    memset(x, 0, sizeof *x);
    id_of(k, x->id);
    x->kind = kind;
    x->t.mag = mag;
    x->t.dir = dir;
}
static void dep(pdx_node_t *x, uint32_t k)
{
    id_of(k, x->deps[x->ndeps++]);
}
static int32_t st_of(const pdx_graph_t *g, uint32_t k)
{
    uint8_t id[32];
    id_of(k, id);
    return pdx_graph_state(g, id);
}
static pdx_update_t upd(uint32_t k, uint8_t kind, bool forb, uint32_t mag, int8_t dir)
{
    pdx_update_t u;
    memset(&u, 0, sizeof u);
    id_of(k, u.id);
    u.kind = kind;
    u.forbidden = forb;
    u.t.mag = mag;
    u.t.dir = dir;
    return u;
}

/* the pass digest, restated from paradox.h */
static void ref_pass(const pdx_graph_t *g, uint8_t out[32])
{
    static uint8_t b[15 + 8 + PDX_GRAPH_MAX * 40 + 5];
    uint32_t k = 15;
    memcpy(b, "zxv/pdx-pass/v1", 15);
    le32(b + k, g->pass);
    le32(b + k + 4, g->n);
    k += 8;
    for (uint32_t p = 0; p < g->n; p++) {
        memcpy(b + k, g->node[g->ord[p]].id, 32);
        k += 32;
        b[k++] = g->kind[p];
        b[k++] = g->forb[p];
        b[k++] = g->st[p];
        b[k++] = (uint8_t) g->t[p].dir;
        le32(b + k, g->t[p].mag);
        k += 4;
    }
    le32(b + k, g->total.mag);
    b[k + 4] = (uint8_t) g->total.dir;
    sha256(b, k + 5, out);
}

static void test_graph(void)
{
    printf("[8] P0.4 complex paradox: fixed order, taint, sandbox, hold, step limit\n");
    /* the roadmap's example: a routing tension (R), an injection exploiting
     * it (D), two disagreeing nodes (N), a new anomaly (S), plus dependants */
    enum { R = 1, D = 2, N = 3, S = 4, C1 = 5, C2 = 6, C3 = 7, W = 8, T2 = 9, X = 10 };
    pdx_node_t nd[10];
    mknode(&nd[0], R, PDX_KIND_CONSTRUCTIVE, 5, 1);
    mknode(&nd[1], D, PDX_KIND_DESTRUCTIVE, 9, -1);
    dep(&nd[1], R); /* the injection exploits the routing tension */
    mknode(&nd[2], N, PDX_KIND_NEUTRAL, 3, 0);
    mknode(&nd[3], S, PDX_KIND_SPECIAL, 4, 1);
    mknode(&nd[4], C1, PDX_KIND_CONSTRUCTIVE, 2, 1);
    dep(&nd[4], R);
    mknode(&nd[5], C2, PDX_KIND_CONSTRUCTIVE, 2, 1);
    dep(&nd[5], D); /* depends on the attacker */
    mknode(&nd[6], C3, PDX_KIND_CONSTRUCTIVE, 2, 1);
    dep(&nd[6], N);
    mknode(&nd[7], W, PDX_KIND_NEUTRAL, 1, -1);
    dep(&nd[7], S);
    mknode(&nd[8], T2, PDX_KIND_NEUTRAL, 1, 1);
    dep(&nd[8], C2); /* taint is transitive: T2 -> C2 -> D */
    mknode(&nd[9], X, PDX_KIND_NONE, 0, 0);
    dep(&nd[9], N);
    pdx_graph_t g;
    fresh_log(256);
    g.total.mag = 77;
    g.total.dir = 1;
    CHECK(pdx_graph_load(&g, nd, 10, 6) && g.total.mag == 0 && g.total.dir == 0 && g.pass == 0,
          "a ten-node mixed graph loads; no tension, no passes yet");
    int sorted = 1;
    for (uint32_t p = 1; p < g.n; p++)
        sorted &= memcmp(nd[g.ord[p - 1]].id, nd[g.ord[p]].id, 32) < 0;
    CHECK(sorted, "nodes are in canonical (id) order");
    pdx_out_t o = pdx_graph_pass(&g, &L);
    CHECK(o == PDX_OUT_PENDING && g.pass == 1, "pass 1: pending");
    CHECK(st_of(&g, D) == PDX_NS_CONTAINED && st_of(&g, C2) == PDX_NS_TAINTED &&
              st_of(&g, T2) == PDX_NS_TAINTED,
          "destructive contained; its dependants tainted, transitively");
    CHECK(st_of(&g, R) == PDX_NS_APPLIED && st_of(&g, C1) == PDX_NS_APPLIED,
          "a constructive the attacker depends on is not harmed; its own dependant applies");
    CHECK(st_of(&g, S) == PDX_NS_SANDBOXED && st_of(&g, W) == PDX_NS_WAITING,
          "unclassified sandboxed; its dependant waits");
    CHECK(st_of(&g, N) == PDX_NS_HELD && st_of(&g, C3) == PDX_NS_BLOCKED &&
              st_of(&g, X) == PDX_NS_CLEAR,
          "neutral held; the constructive it reaches does not apply; a non-paradox is clear");
    /* open: S(+4) W(-1) N(0) C3(+2) -> +5 */
    CHECK(g.total.mag == 5 && g.total.dir == 1, "net tension of the open nodes: (5, +)");
    uint8_t pd[32];
    ref_pass(&g, pd);
    CHECK(!memcmp(pd, L.rec[0].a, 32), "pass digest = SHA-256 of the documented layout");
    CHECK(L.n == 1 && L.rec[0].op == PDX_OP_GRAPH && L.rec[0].aux == 1 && L.rec[0].tag == 0 &&
              L.rec[0].kind == PDX_KIND_NONE && !memcmp(L.rec[0].b, g.chain, 32) &&
              L.rec[0].outcome == PDX_OUT_PENDING,
          "the pass is a record carrying the chain");
    /* the attacker floods updates claiming to be constructive */
    pdx_update_t u[3] = {upd(D, PDX_KIND_CONSTRUCTIVE, false, 0, 1),
                         upd(C2, PDX_KIND_CONSTRUCTIVE, false, 0, 1),
                         upd(T2, PDX_KIND_CONSTRUCTIVE, false, 0, 1)};
    CHECK(pdx_graph_update(&g, u, 3), "updates accepted as input");
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, D) == PDX_NS_CONTAINED && st_of(&g, C2) == PDX_NS_TAINTED &&
              st_of(&g, T2) == PDX_NS_TAINTED,
          "containment is sticky: an attacker cannot talk its dependants into applying");
    {
        int32_t pd2 = -1;
        for (uint32_t p = 0; p < g.n; p++)
            if (!memcmp(nd[g.ord[p]].id, nd[1].id, 32)) pd2 = (int32_t) p;
        CHECK(pd2 >= 0 && g.kind[pd2] == PDX_KIND_DESTRUCTIVE,
              "and the contained node's kind is not rewritten by the update");
    }
    /* evidence arrives: N decisive -> constructive; S reviewed -> neutral */
    pdx_update_t v[2] = {upd(N, PDX_KIND_CONSTRUCTIVE, false, 3, 1),
                         upd(S, PDX_KIND_NEUTRAL, false, 4, 1)};
    pdx_graph_update(&g, v, 2);
    o = pdx_graph_pass(&g, &L);
    CHECK(o == PDX_OUT_PENDING && st_of(&g, N) == PDX_NS_APPLIED && st_of(&g, C3) == PDX_NS_APPLIED,
          "once N resolves, the constructive behind it applies in the same pass");
    CHECK(st_of(&g, S) == PDX_NS_HELD && st_of(&g, W) == PDX_NS_HELD,
          "S reviewed to neutral is held, and W, no longer waiting, is held too");
    pdx_update_t w2[2] = {upd(S, PDX_KIND_NONE, false, 0, 0),
                          upd(W, PDX_KIND_CONSTRUCTIVE, false, 1, 1)};
    pdx_graph_update(&g, w2, 2);
    o = pdx_graph_pass(&g, &L);
    CHECK(o == PDX_OUT_RESOLVED && st_of(&g, S) == PDX_NS_CLEAR && st_of(&g, W) == PDX_NS_APPLIED &&
              g.total.mag == 0 && g.total.dir == 0,
          "every node final: resolved");
    CHECK(L.n == 4 && pdx_log_verify(&L) && L.rec[3].outcome == PDX_OUT_RESOLVED &&
              L.rec[3].aux == 4,
          "four passes, four chained records");
    /* recompute the chain from the records */
    uint8_t ch[32] = {0};
    for (uint32_t i = 0; i < L.n; i++) {
        uint8_t b[16 + 64];
        memcpy(b, "zxv/pdx-chain/v1", 16);
        memcpy(b + 16, ch, 32);
        memcpy(b + 48, L.rec[i].a, 32);
        sha256(b, 80, ch);
        CHECK(!memcmp(ch, L.rec[i].b, 32), "chain link = SHA-256(prev chain || pass digest)");
    }
    /* an applied node that later turns destructive is contained; dependants tainted */
    pdx_update_t bad = upd(R, PDX_KIND_DESTRUCTIVE, false, 1, -1);
    pdx_graph_update(&g, &bad, 1);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, R) == PDX_NS_CONTAINED && st_of(&g, C1) == PDX_NS_TAINTED,
          "an applied node found destructive later is contained; what applied on it is tainted");

    /* forbidden node */
    pdx_node_t f[3];
    mknode(&f[0], 20, PDX_KIND_CONSTRUCTIVE, 1, 1);
    f[0].forbidden = true;
    mknode(&f[1], 21, PDX_KIND_CONSTRUCTIVE, 1, 1);
    dep(&f[1], 20);
    mknode(&f[2], 22, PDX_KIND_DESTRUCTIVE, 1, 1);
    f[2].forbidden = true;
    pdx_graph_load(&g, f, 3, 4);
    o = pdx_graph_pass(&g, &L);
    CHECK(o == PDX_OUT_RESOLVED && st_of(&g, 20) == PDX_NS_ROLLED_BACK &&
              st_of(&g, 21) == PDX_NS_TAINTED && st_of(&g, 22) == PDX_NS_ROLLED_BACK,
          "a forbidden node is rolled back whatever its kind; its dependants tainted");
    pdx_update_t fu = upd(21, PDX_KIND_CONSTRUCTIVE, true, 0, 0);
    pdx_graph_update(&g, &fu, 1);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, 21) == PDX_NS_TAINTED,
          "a tainted node stays tainted even when marked forbidden");
    pdx_update_t fu2 = upd(20, PDX_KIND_NEUTRAL, false, 0, 0);
    pdx_graph_update(&g, &fu2, 1);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, 20) == PDX_NS_ROLLED_BACK && g.forb[0] + g.forb[1] + g.forb[2] >= 2,
          "forbidden is absorbing: a later update cannot clear it");
    pdx_node_t f2[2];
    mknode(&f2[0], 30, PDX_KIND_NEUTRAL, 1, 1);
    mknode(&f2[1], 31, PDX_KIND_CONSTRUCTIVE, 1, 1);
    dep(&f2[1], 30);
    pdx_graph_load(&g, f2, 2, 9);
    pdx_graph_pass(&g, &L);
    pdx_update_t fu3 = upd(30, PDX_KIND_NEUTRAL, true, 1, 1);
    pdx_graph_update(&g, &fu3, 1);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, 30) == PDX_NS_ROLLED_BACK && st_of(&g, 31) == PDX_NS_TAINTED,
          "an update marking a held node forbidden rolls it back and taints its dependants");

    /* step limit */
    pdx_node_t s[2];
    mknode(&s[0], 40, PDX_KIND_NEUTRAL, 7, -1);
    mknode(&s[1], 41, PDX_KIND_CONSTRUCTIVE, 2, 1);
    dep(&s[1], 40);
    pdx_graph_load(&g, s, 2, 3);
    CHECK(pdx_graph_pass(&g, &L) == PDX_OUT_PENDING && pdx_graph_pass(&g, &L) == PDX_OUT_PENDING &&
              pdx_graph_pass(&g, &L) == PDX_OUT_ESCALATE_TO_OWNER && g.pass == 3,
          "a hold that never resolves escalates to the owner at the step limit");
    CHECK(st_of(&g, 40) == PDX_NS_HELD && st_of(&g, 41) == PDX_NS_BLOCKED && g.total.mag == 5 &&
              g.total.dir == -1,
          "the escalated graph still holds every open node; tension (5, -)");
    ref_pass(&g, pd);
    CHECK(!memcmp(pd, L.rec[L.n - 1].a, 32), "the escalation record's digest matches");
    CHECK(pdx_graph_pass(&g, &L) == PDX_OUT_ESCALATE_TO_OWNER, "and stays escalated");
    pdx_graph_load(&g, s, 2, 0);
    CHECK(pdx_graph_pass(&g, &L) == PDX_OUT_ESCALATE_TO_OWNER,
          "a step limit of 0 escalates at once");
    pdx_graph_load(&g, s, 0, 0);
    CHECK(pdx_graph_pass(&g, &L) == PDX_OUT_RESOLVED, "an empty graph is resolved");
    s[0].t.mag = 0x01020305u;
    s[1].t.mag = 0x01020304u;
    s[0].t.dir = 1;
    s[1].t.dir = -1;
    pdx_graph_load(&g, s, 2, 9);
    pdx_graph_pass(&g, &L);
    ref_pass(&g, pd);
    CHECK(g.total.mag == 1 && g.total.dir == 1 && !memcmp(pd, L.rec[L.n - 1].a, 32),
          "a net tension of exactly 1 has direction +; wide magnitudes hash in full");

    /* a cycle of dependencies and a waiting chain */
    pdx_node_t cy[4];
    mknode(&cy[0], 50, PDX_KIND_CONSTRUCTIVE, 1, 1);
    dep(&cy[0], 51);
    mknode(&cy[1], 51, PDX_KIND_CONSTRUCTIVE, 1, 1);
    dep(&cy[1], 50);
    mknode(&cy[2], 52, PDX_KIND_SPECIAL, 1, 1);
    mknode(&cy[3], 53, PDX_KIND_CONSTRUCTIVE, 1, 1);
    dep(&cy[3], 54 - 1); /* 53 depends on itself: harmless */
    dep(&cy[3], 50);
    pdx_graph_load(&g, cy, 4, 5);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, 50) == PDX_NS_APPLIED && st_of(&g, 51) == PDX_NS_APPLIED &&
              st_of(&g, 53) == PDX_NS_APPLIED && st_of(&g, 52) == PDX_NS_SANDBOXED,
          "a dependency cycle of constructives applies; a self-edge is harmless");
    cy[0].ndeps = 2;
    id_of(52, cy[0].deps[1]);
    pdx_graph_load(&g, cy, 4, 5);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, 50) == PDX_NS_WAITING && st_of(&g, 51) == PDX_NS_WAITING &&
              st_of(&g, 53) == PDX_NS_WAITING,
          "waiting spreads through the cycle and its dependants");
    pdx_update_t sd = upd(52, PDX_KIND_DESTRUCTIVE, false, 1, 1);
    pdx_graph_update(&g, &sd, 1);
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, 52) == PDX_NS_CONTAINED && st_of(&g, 50) == PDX_NS_TAINTED &&
              st_of(&g, 51) == PDX_NS_TAINTED && st_of(&g, 53) == PDX_NS_TAINTED,
          "a special later found destructive: contained, and the whole cycle tainted");
    pdx_node_t sp[2];
    mknode(&sp[0], 60, PDX_KIND_CONSTRUCTIVE, 1, 1);
    mknode(&sp[1], 61, PDX_KIND_NEUTRAL, 1, 1);
    pdx_graph_load(&g, sp, 2, 5);
    pdx_graph_pass(&g, &L);
    pdx_update_t sp2[2] = {upd(60, PDX_KIND_SPECIAL, false, 1, 1),
                           upd(61, PDX_KIND_NONE, false, 0, 0)};
    pdx_graph_update(&g, sp2, 2);
    CHECK(pdx_graph_pass(&g, &L) == PDX_OUT_RESOLVED && st_of(&g, 60) == PDX_NS_APPLIED,
          "an applied node is not pulled back into the sandbox (rollback is the operator's job)");

    /* load refusals */
    pdx_node_t bad2[3];
    mknode(&bad2[0], 70, PDX_KIND_NEUTRAL, 0, 0);
    mknode(&bad2[1], 70, PDX_KIND_NEUTRAL, 0, 0);
    CHECK(!pdx_graph_load(&g, bad2, 2, 1), "a duplicate id is refused (order would be ambiguous)");
    mknode(&bad2[1], 71, PDX_KIND_NEUTRAL, 0, 0);
    dep(&bad2[1], 72);
    CHECK(!pdx_graph_load(&g, bad2, 2, 1), "a dependency on a node not in the set is refused");
    mknode(&bad2[1], 71, PDX_KIND_NEUTRAL, 0, 0);
    bad2[1].ndeps = PDX_MAX_DEPS + 1;
    CHECK(!pdx_graph_load(&g, bad2, 2, 1), "too many dependencies refused");
    mknode(&bad2[1], 71, 5, 0, 0);
    CHECK(!pdx_graph_load(&g, bad2, 2, 1), "an unknown kind refused");
    mknode(&bad2[1], 71, PDX_KIND_NONE, 0, 0);
    bad2[1].ndeps = PDX_MAX_DEPS;
    for (uint32_t i = 0; i < PDX_MAX_DEPS; i++) id_of(70, bad2[1].deps[i]);
    CHECK(pdx_graph_load(&g, bad2, 2, 1), "the maximum number of dependencies loads");
    static pdx_node_t many[PDX_GRAPH_MAX + 1];
    for (uint32_t i = 0; i <= PDX_GRAPH_MAX; i++)
        mknode(&many[i], 1000 + i, PDX_KIND_CONSTRUCTIVE, 1, 1);
    CHECK(!pdx_graph_load(&g, many, PDX_GRAPH_MAX + 1, 1), "more than PDX_GRAPH_MAX refused");
    for (uint32_t i = 1; i < PDX_GRAPH_MAX; i++) dep(&many[i], 1000 + i - 1);
    many[0].kind = PDX_KIND_DESTRUCTIVE;
    CHECK(pdx_graph_load(&g, many, PDX_GRAPH_MAX, 1) && pdx_graph_pass(&g, &L) == PDX_OUT_RESOLVED,
          "a full 64-node chain loads and resolves");
    int all = 1;
    for (uint32_t i = 1; i < PDX_GRAPH_MAX; i++) all &= st_of(&g, 1000 + i) == PDX_NS_TAINTED;
    CHECK(all && st_of(&g, 1000) == PDX_NS_CONTAINED, "taint runs the whole 63-node chain");
    many[0].kind = PDX_KIND_NEUTRAL;
    pdx_graph_load(&g, many, PDX_GRAPH_MAX, 1);
    pdx_graph_pass(&g, &L);
    all = 1;
    for (uint32_t i = 1; i < PDX_GRAPH_MAX; i++) all &= st_of(&g, 1000 + i) == PDX_NS_BLOCKED;
    CHECK(all, "one held root blocks the whole chain, including bit 63");
    CHECK(!pdx_graph_load(0, nd, 1, 1) && !pdx_graph_load(&g, 0, 1, 1) &&
              pdx_graph_load(&g, 0, 0, 1),
          "NULL refusals; an empty set loads");
    /* update refusals */
    pdx_graph_load(&g, nd, 10, 6);
    pdx_update_t uu[2] = {upd(999, PDX_KIND_NEUTRAL, false, 0, 0),
                          upd(N, PDX_KIND_DESTRUCTIVE, false, 0, 0)};
    CHECK(!pdx_graph_update(&g, uu, 2), "an update for an unknown id is reported");
    pdx_graph_pass(&g, &L);
    CHECK(st_of(&g, N) == PDX_NS_CONTAINED, "the rest of the batch still applies");
    uu[0] = upd(R, 7, false, 0, 0);
    CHECK(!pdx_graph_update(&g, uu, 1), "an unknown kind is refused");
    CHECK(!pdx_graph_update(0, uu, 1) && !pdx_graph_update(&g, 0, 1) && pdx_graph_update(&g, 0, 0),
          "NULL refusals; an empty batch is fine");
    CHECK(pdx_graph_state(0, nd[0].id) == -1 && pdx_graph_state(&g, 0) == -1 &&
              st_of(&g, 999) == -1,
          "state lookups refuse unknowns");
    pdx_rec_t r1[1];
    pdx_log_t lf;
    pdx_log_init(&lf, r1, 0);
    uint32_t pz = g.pass;
    CHECK(pdx_graph_pass(&g, &lf) == PDX_OUT_LOG_FULL && g.pass == pz, "a full log: no pass");
    CHECK(pdx_graph_pass(0, &L) == PDX_OUT_REFUSED && pdx_graph_pass(&g, 0) == PDX_OUT_REFUSED,
          "NULL refusals");
    /* worst-wins merge inside a batch */
    pdx_graph_load(&g, nd, 10, 6);
    pdx_update_t mm[4] = {
        upd(N, PDX_KIND_CONSTRUCTIVE, false, 3, -1), upd(N, PDX_KIND_SPECIAL, false, 9, -1),
        upd(N, PDX_KIND_NEUTRAL, false, 9, 1), upd(N, PDX_KIND_NONE, false, 1, 1)};
    pdx_graph_update(&g, mm, 4);
    int32_t pn = -1;
    for (uint32_t p = 0; p < g.n; p++)
        if (nd[g.ord[p]].kind == PDX_KIND_NEUTRAL && !memcmp(nd[g.ord[p]].id, nd[2].id, 32))
            pn = (int32_t) p;
    CHECK(
        pn >= 0 && g.kind[pn] == PDX_KIND_SPECIAL && g.t[pn].mag == 9 && g.t[pn].dir == 1 &&
            !g.forb[pn],
        "a batch merges worst-wins: special beats neutral; larger tension, then larger direction");
    pdx_update_t tie[2] = {upd(N, PDX_KIND_NEUTRAL, false, 9, 1),
                           upd(N, PDX_KIND_NEUTRAL, false, 9, -1)};
    pdx_graph_update(&g, tie, 2);
    CHECK(g.t[pn].mag == 9 && g.t[pn].dir == 1,
          "a tie in magnitude keeps the larger direction, in either order");
    pdx_update_t mm2[2] = {upd(N, PDX_KIND_DESTRUCTIVE, false, 1, 0),
                           upd(N, PDX_KIND_SPECIAL, true, 1, 0)};
    pdx_graph_update(&g, mm2, 2);
    CHECK(g.kind[pn] == PDX_KIND_DESTRUCTIVE && g.forb[pn],
          "destructive beats special; forbidden is OR-ed");
}

/* ---------------------------------------------------------------- */
/* determinism */
typedef struct {
    pdx_node_t node[24];
    uint32_t n;
    pdx_update_t up[3][12];
    uint32_t nu[3];
} scen_t;

static void make_scen(scen_t *s, uint64_t seed)
{
    rng = seed * 0x9E3779B97F4A7C15ull + 1;
    s->n = 6 + rndn(19);
    for (uint32_t i = 0; i < s->n; i++) {
        mknode(&s->node[i], (uint32_t) (seed * 100 + i), (uint8_t) rndn(5), rndn(50),
               (int8_t) ((int) rndn(3) - 1));
        s->node[i].forbidden = rndn(20) == 0;
        uint32_t nd = rndn(PDX_MAX_DEPS + 1);
        for (uint32_t d = 0; d < nd; d++) dep(&s->node[i], (uint32_t) (seed * 100 + rndn(s->n)));
    }
    for (int b = 0; b < 3; b++) {
        s->nu[b] = rndn(13);
        for (uint32_t i = 0; i < s->nu[b]; i++)
            s->up[b][i] = upd((uint32_t) (seed * 100 + rndn(s->n)), (uint8_t) rndn(5),
                              rndn(25) == 0, rndn(50), (int8_t) ((int) rndn(3) - 1));
    }
}

typedef struct {
    uint8_t chain[32], head[32];
    uint8_t st[24];
    pdx_out_t last;
    uint32_t passes;
} result_t;

/* run a scenario with nodes and each batch permuted by `perm_seed` (0: as given) */
static void run_scen(const scen_t *s, uint64_t perm_seed, result_t *r)
{
    static pdx_node_t nodes[24];
    pdx_update_t ups[12];
    uint32_t ix[24];
    for (uint32_t i = 0; i < s->n; i++) ix[i] = i;
    if (perm_seed) {
        rng = perm_seed;
        for (uint32_t i = s->n; i > 1; i--) {
            uint32_t j = rndn(i), t = ix[i - 1];
            ix[i - 1] = ix[j];
            ix[j] = t;
        }
    }
    for (uint32_t i = 0; i < s->n; i++) nodes[i] = s->node[ix[i]];
    pdx_graph_t g;
    pdx_rec_t lr[8];
    pdx_log_t l;
    pdx_log_init(&l, lr, 8);
    pdx_graph_load(&g, nodes, s->n, 4);
    r->last = pdx_graph_pass(&g, &l);
    for (int b = 0; b < 3; b++) {
        for (uint32_t i = 0; i < s->nu[b]; i++) ups[i] = s->up[b][i];
        if (perm_seed)
            for (uint32_t i = s->nu[b]; i > 1; i--) {
                uint32_t j = rndn(i);
                pdx_update_t t = ups[i - 1];
                ups[i - 1] = ups[j];
                ups[j] = t;
            }
        pdx_graph_update(&g, ups, s->nu[b]);
        r->last = pdx_graph_pass(&g, &l);
    }
    r->passes = g.pass;
    memcpy(r->chain, g.chain, 32);
    memcpy(r->head, l.head, 32);
    for (uint32_t i = 0; i < s->n; i++) r->st[i] = (uint8_t) pdx_graph_state(&g, s->node[i].id);
}

static void test_determinism(void)
{
    printf("[9] determinism: 1,000 seeds of shuffled inputs; a three-peer simulation\n");
    static scen_t s;
    int same = 1, loaded = 1, kinds_seen = 0;
    uint32_t outcomes = 0;
    for (uint64_t seed = 1; seed <= 1000; seed++) {
        make_scen(&s, seed);
        pdx_graph_t g;
        loaded &= pdx_graph_load(&g, s.node, s.n, 4);
        result_t ref, sh;
        run_scen(&s, 0, &ref);
        run_scen(&s, seed * 7919u + 3u, &sh);
        same &= !memcmp(ref.chain, sh.chain, 32) && !memcmp(ref.head, sh.head, 32) &&
                !memcmp(ref.st, sh.st, s.n) && ref.last == sh.last && ref.passes == 4;
        outcomes |= 1u << ref.last;
        for (uint32_t i = 0; i < s.n; i++) kinds_seen |= 1 << ref.st[i];
    }
    CHECK(loaded, "every generated scenario loads");
    CHECK(
        same,
        "1,000 seeds: shuffled nodes and update batches give identical states, chain and log head");
    CHECK(kinds_seen == 0x3FE, "the scenarios reach every node state but OPEN");
    CHECK((outcomes & (1u << PDX_OUT_RESOLVED)) && (outcomes & (1u << PDX_OUT_ESCALATE_TO_OWNER)),
          "and both endings: resolved and escalated");
    /* different inputs give a different chain */
    make_scen(&s, 5);
    result_t a, b;
    run_scen(&s, 0, &a);
    s.node[0].t.mag ^= 1;
    run_scen(&s, 0, &b);
    CHECK(memcmp(a.chain, b.chain, 32) != 0, "a one-bit change in one input changes the chain");

    /* three peers: each classifies the same signed evidence and builds the
     * same graph from inputs that arrive in a different order */
    static const char *const desc[6] = {"route:liq",   "inject:loop", "peer:ts-a",
                                        "anomaly:new", "fees:share",  "escrow:timing"};
    pdx_evidence_t ev[6] = {{2, 2, 8, 2, false}, {9, 1, 5, 4, false}, {3, 1, 5, 3, false},
                            {1, 1, 2, 2, false}, {1, 1, 6, 1, false}, {4, 0, 9, 0, false}};
    uint8_t phase[6] = {3, 3, 3, 0, 3, 3};
    uint32_t dep_of[6] = {99, 0, 99, 99, 1, 2};
    static const uint32_t order[3][6] = {
        {0, 1, 2, 3, 4, 5}, {5, 4, 3, 2, 1, 0}, {3, 0, 5, 1, 4, 2}};
    uint8_t chains[3][32], heads[3][32];
    int32_t sts[3][6];
    for (int peer = 0; peer < 3; peer++) {
        pdx_node_t nodes[6];
        pdx_rec_t lr[8];
        pdx_log_t l;
        pdx_log_init(&l, lr, 8);
        for (int k = 0; k < 6; k++) {
            uint32_t i = order[peer][k];
            pdx_class_t c =
                pdx_classify(lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, phase[i]), &ev[i], 0, 0);
            memset(&nodes[k], 0, sizeof nodes[k]);
            pdx_signature((const uint8_t *) desc[i], strlen(desc[i]), nodes[k].id);
            nodes[k].kind = c.kind;
            nodes[k].t.mag = ev[i].pos1 + ev[i].neg1;
            nodes[k].t.dir = 1;
            if (dep_of[i] != 99) {
                pdx_signature((const uint8_t *) desc[dep_of[i]], strlen(desc[dep_of[i]]),
                              nodes[k].deps[0]);
                nodes[k].ndeps = 1;
            }
        }
        pdx_graph_t g;
        pdx_graph_load(&g, nodes, 6, 5);
        pdx_graph_pass(&g, &l);
        /* the neutral peer report gets decisive evidence; delivered with noise */
        pdx_update_t u[3];
        uint8_t id[32];
        pdx_signature((const uint8_t *) "peer:ts-a", 9, id);
        u[0] = upd(0, PDX_KIND_CONSTRUCTIVE, false, 1, 1);
        memcpy(u[0].id, id, 32);
        u[1] = u[0];
        u[1].t.mag = 4;
        pdx_signature((const uint8_t *) "anomaly:new", 11, id);
        u[2] = upd(0, PDX_KIND_NEUTRAL, false, 2, 0);
        memcpy(u[2].id, id, 32);
        pdx_update_t perm[3];
        for (int k = 0; k < 3; k++) perm[k] = u[(k + peer) % 3];
        pdx_graph_update(&g, perm, 3);
        pdx_graph_pass(&g, &l);
        memcpy(chains[peer], g.chain, 32);
        memcpy(heads[peer], l.head, 32);
        for (int i = 0; i < 6; i++) {
            pdx_signature((const uint8_t *) desc[i], strlen(desc[i]), id);
            sts[peer][i] = pdx_graph_state(&g, id);
        }
    }
    CHECK(!memcmp(chains[0], chains[1], 32) && !memcmp(chains[0], chains[2], 32) &&
              !memcmp(heads[0], heads[1], 32) && !memcmp(heads[0], heads[2], 32) &&
              !memcmp(sts[0], sts[1], sizeof sts[0]) && !memcmp(sts[0], sts[2], sizeof sts[0]),
          "three peers, three arrival orders: the same states, chain and log head");
    CHECK(sts[0][0] == PDX_NS_APPLIED && sts[0][1] == PDX_NS_CONTAINED &&
              sts[0][4] == PDX_NS_TAINTED && sts[0][2] == PDX_NS_APPLIED &&
              sts[0][5] == PDX_NS_APPLIED && sts[0][3] == PDX_NS_HELD,
          "and the right ones: injection contained, its dependant tainted, the rest resolved");
}

/* ---------------------------------------------------------------- */
static void test_conservation(void)
{
    printf("[10] conservation: A = L + E after every pass of a seeded run\n");
    toy_t book = {1000, 600, 400}, ck;
    int64_t par[3] = {30, 50, 20},
            prv[3] = {0}; /* budget shares, a routing weight, escrow timing */
    const int64_t lo[3] = {0, 0, 5}, hi[3] = {100, 100, 60};
    pdx_params_t p = {par, prv, lo, hi, 3, false, 0};
    lpx_qrec_t qs[512];
    lpx_qlog_t q;
    lpx_qlog_init(&q, qs, 512);
    fresh_log(4096);
    rng = 0xC0FFEEull;
    int holds = 1, contained = 0, applied = 0, proposals = 0, rolled = 0;
    for (int round = 0; round < 300; round++) {
        ck = book;
        /* a legitimate posting keeps A = L + E */
        int64_t x = (int64_t) rndn(50);
        book.a += x;
        if (rndn(2))
            book.l += x;
        else
            book.e += x;
        /* sometimes an attacker posts one side only */
        bool attack = rndn(4) == 0;
        if (attack) book.a += 1 + (int64_t) rndn(9);
        pdx_watch_t w;
        memset(&w, 0, sizeof w);
        w.invariant = inv_ledger;
        w.step_limit = 64;
        w.steps = rndn(70);
        pdx_class_t dc = cls(PDX_KIND_DESTRUCTIVE, false);
        dc.kind = PDX_KIND_NEUTRAL; /* not pre-classified: only the checks decide */
        pdx_repair_t pr;
        pdx_out_t o = pdx_destructive(&dc, &w, (uint8_t *) &book, (const uint8_t *) &ck,
                                      sizeof book, 0, 0, 1, &q, &L, &pr);
        if (o == PDX_OUT_CONTAINED) contained++;
        holds &= attack ? o == PDX_OUT_CONTAINED : 1;
        holds &= book.a == book.l + book.e;
        /* a constructive synthesis on the parameters, sometimes out of bounds */
        int64_t syn[3] = {(int64_t) rndn(120), (int64_t) rndn(100), (int64_t) rndn(70)};
        pdx_class_t cc = cls(PDX_KIND_CONSTRUCTIVE, false);
        toy_t before = book;
        pdx_out_t co = pdx_constructive(&p, syn, rndn(100), &cc, &L);
        applied += co == PDX_OUT_APPLIED;
        proposals += co == PDX_OUT_PROPOSAL_FOR_OWNER;
        rolled += co == PDX_OUT_ROLLED_BACK;
        holds &= !memcmp(&before, &book, sizeof book) && book.a == book.l + book.e;
        for (int i = 0; i < 3; i++) holds &= par[i] >= lo[i] && par[i] <= hi[i];
    }
    CHECK(holds, "300 rounds: A = L + E after every pass, every parameter inside its bounds");
    CHECK(contained > 0 && applied > 0 && proposals > 0 && rolled > 0,
          "the run exercised containment, applies, proposals and rollbacks");
    CHECK(lpx_qlog_verify(&q) && pdx_log_verify(&L) && q.n == (uint32_t) contained,
          "the quarantine and operator logs verify: one quarantine record per containment");
}

int main(void)
{
    test_basics();
    test_classify();
    test_constructive();
    test_destructive();
    test_neutral();
    test_special();
    test_forbidden();
    test_graph();
    test_determinism();
    test_conservation();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
