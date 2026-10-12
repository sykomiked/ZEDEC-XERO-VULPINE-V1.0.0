/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_lpres_hex.c — the 64-state hexagram tag (lpres_hex.h X1-X6).
 *
 * Every one of the 64 tags is checked: its fields, its action against a rule
 * oracle written independently here, the one-step phase rule, forbidden
 * absorption, the combination laws against lpres's own tables, and the
 * quarantine log's chain under tampering. */
#include <stdio.h>
#include <string.h>
#include "lpres_hex.h"
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

/* the rules of X2, restated from the header without the table */
static lpx_action_t oracle(uint8_t base, uint8_t pol)
{
    if (pol == LPX_P_FORBIDDEN) return LPX_ACT_ROLLBACK;
    switch (base) {
    case LPX_B_TRUE:
        return pol == LPX_P_ENTROPIC ? LPX_ACT_WAIT : LPX_ACT_PROCEED;
    case LPX_B_FALSE:
        return LPX_ACT_REFUSE;
    case LPX_B_BOTH:
        return pol == LPX_P_NEGENTROPIC ? LPX_ACT_WAIT : LPX_ACT_ESCALATE;
    default:
        return LPX_ACT_WAIT;
    }
}

static void test_layout(void)
{
    printf("[1] layout: 64 tags, three 2-bit fields, the user's bit codes\n");
    int ok = 1;
    for (uint32_t t = 0; t < LPX_STATES; t++) {
        lpx_t x = (lpx_t) t;
        ok &= lpx_make(lpx_base(x), lpx_pol(x), lpx_phase(x)) == x;
        ok &= lpx_base(x) == (t >> 4) && lpx_pol(x) == ((t >> 2) & 3u) && lpx_phase(x) == (t & 3u);
    }
    CHECK(ok, "every tag splits into its three fields and back");
    CHECK(lpx_make(7, 6, 5) == lpx_make(3, 2, 1), "out-of-range fields are masked to 2 bits");
    /* the codes as given: True 11, False 00, Both 10, Neither 01; negentropic
     * 11, entropic 00, neutral 01, forbidden 10; potential 00, emerging 01,
     * manifest 11, dissolving 10 */
    CHECK(LPX_B_TRUE == 3 && LPX_B_FALSE == 0 && LPX_B_BOTH == 2 && LPX_B_NEITHER == 1,
          "base codes");
    CHECK(LPX_P_NEGENTROPIC == 3 && LPX_P_ENTROPIC == 0 && LPX_P_NEUTRAL == 1 &&
              LPX_P_FORBIDDEN == 2,
          "polarity codes");
    CHECK(LPX_PH_POTENTIAL == 0 && LPX_PH_EMERGING == 1 && LPX_PH_MANIFEST == 3 &&
              LPX_PH_DISSOLVING == 2,
          "phase codes");
    CHECK(lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST) == 63, "11 11 11 is 63");
    CHECK(lpx_make(LPX_B_FALSE, LPX_P_ENTROPIC, LPX_PH_POTENTIAL) == 0, "00 00 00 is 0");
    CHECK(lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_POTENTIAL) == 60, "11 11 00 is 60");
    const lpres_state_t all[4] = {LPRES_STATE_NEITHER, LPRES_STATE_TRUE, LPRES_STATE_FALSE,
                                  LPRES_STATE_BOTH};
    int rt = 1;
    for (int i = 0; i < 4; i++) rt &= lpx_base_to_lpres(lpx_base_from_lpres(all[i])) == all[i];
    CHECK(rt, "lpres states convert to base codes and back");
    CHECK(lpx_base_from_lpres((lpres_state_t) 9) == LPX_B_NEITHER,
          "an out-of-range lpres value is unknown, never true");
    CHECK(lpx_base_from_lpres(LPRES_STATE_TRUE) == LPX_B_TRUE &&
              lpx_base_from_lpres(LPRES_STATE_FALSE) == LPX_B_FALSE &&
              lpx_base_from_lpres(LPRES_STATE_BOTH) == LPX_B_BOTH &&
              lpx_base_from_lpres(LPRES_STATE_NEITHER) == LPX_B_NEITHER,
          "each lpres state has its own code");
}

static void test_actions(void)
{
    printf("[2] the action table, all 64 entries\n");
    int ok = 1, forb = 0, phase_free = 1;
    for (uint32_t t = 0; t < LPX_STATES; t++) {
        lpx_t x = (lpx_t) t;
        ok &= lpx_action(x) == oracle(lpx_base(x), lpx_pol(x));
        forb += lpx_action(x) == LPX_ACT_ROLLBACK;
        for (uint8_t p = 0; p < 4; p++)
            phase_free &= lpx_action(lpx_make(lpx_base(x), lpx_pol(x), p)) == lpx_action(x);
    }
    CHECK(ok, "every entry matches the rules");
    CHECK(forb == 16, "exactly the 16 forbidden tags roll back");
    CHECK(phase_free, "the phase never changes the action");
    CHECK(lpx_action(63) == LPX_ACT_PROCEED, "true, negentropic, manifest proceeds");
    CHECK(lpx_action(0) == LPX_ACT_REFUSE, "false, entropic, potential is refused");
    CHECK(lpx_action(lpx_make(LPX_B_TRUE, LPX_P_ENTROPIC, LPX_PH_MANIFEST)) == LPX_ACT_WAIT,
          "a weakening truth waits");
    CHECK(lpx_action(lpx_make(LPX_B_BOTH, LPX_P_NEGENTROPIC, 0)) == LPX_ACT_WAIT &&
              lpx_action(lpx_make(LPX_B_BOTH, LPX_P_ENTROPIC, 0)) == LPX_ACT_ESCALATE &&
              lpx_action(lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, 0)) == LPX_ACT_ESCALATE,
          "a converging dispute waits; a stuck or diverging one goes to a person");
    CHECK(lpx_action(lpx_make(LPX_B_NEITHER, LPX_P_NEGENTROPIC, 3)) == LPX_ACT_WAIT,
          "unknown never acts");
    CHECK(lpx_action(64 + 63) == lpx_action(63), "bits above the six are ignored");
    static const char *const bn[4] = {"false", "neither", "both", "true"};
    static const char *const pn[4] = {"entropic", "neutral", "forbidden", "negentropic"};
    static const char *const hn[4] = {"potential", "emerging", "dissolving", "manifest"};
    static const char *const an[5] = {"proceed", "wait", "refuse", "escalate", "rollback"};
    int nm = 1;
    for (uint8_t i = 0; i < 4; i++)
        nm &= !strcmp(lpx_base_name(i), bn[i]) && !strcmp(lpx_pol_name(i), pn[i]) &&
              !strcmp(lpx_phase_name(i), hn[i]);
    for (int i = 0; i < 5; i++) nm &= !strcmp(lpx_action_name((lpx_action_t) i), an[i]);
    CHECK(nm && !strcmp(lpx_action_name((lpx_action_t) 5), "?"),
          "every name, and none past the end");
    CHECK(!strcmp(lpx_action_name(LPX_ACT_ROLLBACK), "rollback") &&
              !strcmp(lpx_action_name((lpx_action_t) 9), "?") &&
              !strcmp(lpx_base_name(LPX_B_BOTH), "both") &&
              !strcmp(lpx_pol_name(LPX_P_FORBIDDEN), "forbidden") &&
              !strcmp(lpx_phase_name(LPX_PH_DISSOLVING), "dissolving"),
          "names");
}

static void test_phase(void)
{
    printf("[3] phases: one Gray step at a time\n");
    uint8_t p = LPX_PH_POTENTIAL;
    const uint8_t want[5] = {LPX_PH_EMERGING, LPX_PH_MANIFEST, LPX_PH_DISSOLVING, LPX_PH_POTENTIAL,
                             LPX_PH_EMERGING};
    int ok = 1, onebit = 1;
    for (int i = 0; i < 5; i++) {
        uint8_t n = lpx_phase_next(p);
        ok &= n == want[i];
        uint8_t d = (uint8_t) (n ^ p);
        onebit &= d == 1u || d == 2u;
        p = n;
    }
    CHECK(ok, "potential, emerging, manifest, dissolving, potential");
    CHECK(onebit, "each step flips exactly one bit");
    int adv = 1;
    for (uint32_t t = 0; t < LPX_STATES; t++) {
        for (uint8_t to = 0; to < 4; to++) {
            lpx_t x = (lpx_t) t;
            bool legal = to == lpx_phase(x) || to == lpx_phase_next(lpx_phase(x));
            bool got = lpx_advance(&x, to);
            adv &= got == legal;
            adv &= got ? x == lpx_make(lpx_base((lpx_t) t), lpx_pol((lpx_t) t), to) : x == t;
        }
    }
    CHECK(adv, "advance takes the same or the next phase only, and keeps base and polarity");
    lpx_t x = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    CHECK(!lpx_advance(&x, LPX_PH_MANIFEST) && lpx_phase(x) == LPX_PH_POTENTIAL,
          "potential cannot jump to manifest");
    CHECK(!lpx_advance(&x, 4) && !lpx_advance(0, 1), "bad arguments refused");
}

static void test_forbidden(void)
{
    printf("[4] forbidden is absorbing\n");
    int ok = 1;
    for (uint32_t t = 0; t < LPX_STATES; t++) {
        lpx_t f = lpx_forbid((lpx_t) t);
        ok &= lpx_forbidden(f) && lpx_base(f) == lpx_base((lpx_t) t) &&
              lpx_phase(f) == lpx_phase((lpx_t) t);
        for (uint8_t b = 0; b < 4; b++)
            for (uint8_t p = 0; p < 4; p++) {
                lpx_t s = lpx_set(f, b, p);
                ok &= lpx_forbidden(s) && lpx_base(s) == b && lpx_phase(s) == lpx_phase(f);
            }
        lpx_t y = f;
        ok &= lpx_advance(&y, lpx_phase_next(lpx_phase(f))) && lpx_forbidden(y);
        ok &= lpx_forbidden(lpx_not(f));
        for (uint32_t u = 0; u < LPX_STATES; u++)
            ok &= lpx_forbidden(lpx_and(f, (lpx_t) u)) && lpx_forbidden(lpx_or((lpx_t) u, f));
    }
    CHECK(ok, "no change, step or combination leaves forbidden");
    lpx_t c = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, 1);
    CHECK(lpx_set(c, LPX_B_BOTH, LPX_P_NEGENTROPIC) == lpx_make(LPX_B_BOTH, LPX_P_NEGENTROPIC, 1),
          "a clean tag changes freely");
}

static void test_polarity(void)
{
    printf("[5] polarity from evidence\n");
    CHECK(lpx_polarity(3, 1, 5, 1, false) == LPX_P_NEGENTROPIC, "margin grows: negentropic");
    CHECK(lpx_polarity(5, 1, 5, 4, false) == LPX_P_ENTROPIC, "margin shrinks: entropic");
    CHECK(lpx_polarity(5, 1, 6, 2, false) == LPX_P_NEUTRAL, "margin steady: neutral");
    CHECK(lpx_polarity(1, 5, 1, 9, false) == LPX_P_NEGENTROPIC, "growing the other way counts too");
    CHECK(lpx_polarity(2, 5, 3, 5, false) == LPX_P_ENTROPIC &&
              lpx_polarity(5, 2, 5, 3, false) == LPX_P_ENTROPIC,
          "the margin is a difference, not a sum, on either side");
    CHECK(lpx_polarity(3, 1, 9, 1, true) == LPX_P_ENTROPIC, "stale evidence is entropic");
    CHECK(lpx_polarity(0, 0, 0, 0, false) == LPX_P_NEUTRAL, "no evidence, no trend");
    CHECK(lpx_polarity(0xFFFFFFFFu, 0, 0, 0xFFFFFFFFu, false) == LPX_P_NEUTRAL,
          "full-range counts do not overflow");
    int orc = 1;
    for (uint32_t a = 0; a < 6; a++)
        for (uint32_t b = 0; b < 6; b++)
            for (uint32_t c = 0; c < 6; c++)
                for (uint32_t d = 0; d < 6; d++) {
                    int64_t m0 = (int64_t) a - (int64_t) b, m1 = (int64_t) c - (int64_t) d;
                    m0 = m0 < 0 ? -m0 : m0;
                    m1 = m1 < 0 ? -m1 : m1;
                    uint8_t want = m1 > m0   ? LPX_P_NEGENTROPIC
                                   : m1 < m0 ? LPX_P_ENTROPIC
                                             : LPX_P_NEUTRAL;
                    orc &= lpx_polarity(a, b, c, d, false) == want;
                }
    CHECK(orc, "polarity matches the margin rule for every small count");
    int never = 1;
    for (uint32_t a = 0; a < 4; a++)
        for (uint32_t b = 0; b < 4; b++)
            for (uint32_t c = 0; c < 4; c++)
                for (uint32_t d = 0; d < 4; d++)
                    for (int s = 0; s < 2; s++)
                        never &= lpx_polarity(a, b, c, d, s) != LPX_P_FORBIDDEN;
    CHECK(never, "evidence alone never makes a tag forbidden");
}

static uint8_t pol_bad(uint8_t p)
{
    return p == LPX_P_FORBIDDEN ? 3 : p == LPX_P_ENTROPIC ? 2 : p == LPX_P_NEUTRAL ? 1 : 0;
}
static uint8_t phase_ord(uint8_t p)
{
    return p == LPX_PH_POTENTIAL ? 0 : p == LPX_PH_EMERGING ? 1 : p == LPX_PH_MANIFEST ? 2 : 3;
}

static void test_combine(void)
{
    printf("[6] combining: lpres's tables, worst polarity, earliest phase\n");
    int base = 1, pol = 1, ph = 1, neg = 1, comm = 1, dm = 1;
    for (uint32_t a = 0; a < LPX_STATES; a++)
        for (uint32_t b = 0; b < LPX_STATES; b++) {
            lpx_t x = (lpx_t) a, y = (lpx_t) b, n = lpx_and(x, y), o = lpx_or(x, y);
            lpres_state_t la = lpx_base_to_lpres(lpx_base(x)), lb = lpx_base_to_lpres(lpx_base(y));
            base &= lpx_base_to_lpres(lpx_base(n)) == lpres_conjoin(la, lb) &&
                    lpx_base_to_lpres(lpx_base(o)) == lpres_disjoin(la, lb);
            uint8_t w = pol_bad(lpx_pol(x)) >= pol_bad(lpx_pol(y)) ? lpx_pol(x) : lpx_pol(y);
            pol &= lpx_pol(n) == w && lpx_pol(o) == w;
            uint8_t e =
                phase_ord(lpx_phase(x)) <= phase_ord(lpx_phase(y)) ? lpx_phase(x) : lpx_phase(y);
            ph &= lpx_phase(n) == e && lpx_phase(o) == e;
            comm &= n == lpx_and(y, x) && o == lpx_or(y, x);
            /* De Morgan on the base. lpres's tables break it for the pair
             * {Both, Neither} (Both AND Neither = Neither, Both OR Neither =
             * Both). This is deliberate (owner decision 2026-10-12): with
             * Belnap's tables (False / True there) a contradiction plus a gap
             * could authorise. Every other pair must satisfy it. */
            bool bn = (lpx_base(x) == LPX_B_BOTH && lpx_base(y) == LPX_B_NEITHER) ||
                      (lpx_base(x) == LPX_B_NEITHER && lpx_base(y) == LPX_B_BOTH);
            if (!bn) dm &= lpx_base(lpx_not(n)) == lpx_base(lpx_or(lpx_not(x), lpx_not(y)));
        }
    for (uint32_t a = 0; a < LPX_STATES; a++) {
        lpx_t x = (lpx_t) a, nx = lpx_not(x);
        neg &= lpx_pol(nx) == lpx_pol(x) && lpx_phase(nx) == lpx_phase(x) && lpx_not(nx) == x;
        neg &= lpx_base_to_lpres(lpx_base(nx)) == lpres_negate(lpx_base_to_lpres(lpx_base(x)));
    }
    CHECK(base, "the base follows lpres_conjoin / lpres_disjoin for all 4096 pairs");
    CHECK(pol, "the worst polarity wins (forbidden > entropic > neutral > negentropic)");
    CHECK(ph, "the least advanced phase wins");
    CHECK(comm, "and / or commute");
    CHECK(neg, "not follows lpres_negate, keeps polarity and phase, and is an involution");
    CHECK(dm, "De Morgan holds on every base pair except {Both, Neither}");
    lpx_t t = lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST),
          f = lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_EMERGING);
    CHECK(lpx_and(t, f) == lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_EMERGING) &&
              lpx_or(t, f) == lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_EMERGING),
          "a worked example");
}

/* the record hash, built here byte by byte from the header's description */
static void ref_hash(const lpx_qrec_t *r, uint8_t out[32])
{
    uint8_t b[128];
    size_t k = 0;
    memcpy(b, "zxv/lpx-qlog/v1", 15);
    k = 15;
    for (int i = 0; i < 8; i++) b[k++] = (uint8_t) (r->seq >> (8 * i));
    b[k++] = r->tag;
    b[k++] = r->reason;
    for (int i = 0; i < 4; i++) b[k++] = (uint8_t) (r->submitter >> (8 * i));
    memcpy(b + k, r->subject, 32);
    k += 32;
    memcpy(b + k, r->prev, 32);
    k += 32;
    sha256(b, k, out);
}

static void test_qlog(void)
{
    printf("[7] the quarantine log: forbidden only, append-only, chained\n");
    static lpx_qrec_t st[4];
    lpx_qlog_t q;
    memset(&q, 0xFF, sizeof q);
    lpx_qlog_init(&q, st, 4);
    uint8_t subj[32], zero[32] = {0};
    memset(subj, 0xA5, sizeof subj);
    CHECK(q.n == 0 && !memcmp(q.head, zero, 32) && lpx_qlog_verify(&q), "empty and valid");
    CHECK(!lpx_qlog_append(&q, lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, 3), 1, 7, subj) && q.n == 0,
          "a tag that is not forbidden is not quarantined");
    lpx_t f = lpx_forbid(lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST));
    CHECK(lpx_qlog_append(&q, f, 1, 7, subj), "a forbidden tag is quarantined");
    uint8_t h1[32];
    memcpy(h1, q.head, 32);
    subj[0] ^= 1;
    CHECK(lpx_qlog_append(&q, lpx_forbid(0), 2, 8, subj) && q.n == 2, "and another");
    CHECK(!memcmp(st[1].prev, h1, 32) && !memcmp(q.head, st[1].hash, 32) && memcmp(h1, q.head, 32),
          "each record links to the one before");
    CHECK(st[0].seq == 0 && st[1].seq == 1 && st[1].tag == lpx_forbid(0) && st[1].reason == 2 &&
              st[1].submitter == 8 && st[1].subject[0] == (0xA5 ^ 1),
          "the record keeps what was refused and who sent it");
    CHECK(!memcmp(st[1].subject, subj, 32), "the whole subject digest is kept");
    uint8_t rh[32];
    int kat = 1;
    for (int k = 0; k < 2; k++) {
        ref_hash(&st[k], rh);
        kat &= !memcmp(rh, st[k].hash, 32);
    }
    CHECK(kat, "each record hash is SHA-256 of exactly the documented bytes");
    st[1].seq = 0x0102030405060708ull;
    st[1].submitter = 0xA1B2C3D4u;
    lpx_qrec_t probe = st[1];
    ref_hash(&probe, rh);
    st[1].seq = 1;
    st[1].submitter = 8;
    {
        /* the encoder must use every byte of seq and submitter */
        lpx_qrec_t a = probe, b2 = probe;
        uint8_t ha[32], hb[32];
        b2.seq ^= 1;
        ref_hash(&a, ha);
        ref_hash(&b2, hb);
        CHECK(memcmp(ha, hb, 32) && !memcmp(ha, rh, 32), "the reference itself sees seq");
    }
    CHECK(lpx_qlog_verify(&q), "the chain verifies");
    CHECK(lpx_qlog_append(&q, f, 3, 0xA1B2C3D4u, subj) &&
              lpx_qlog_append(&q, f, 4, 0x8899AABBu, subj) && q.n == 4,
          "filled to capacity");
    CHECK(!lpx_qlog_append(&q, f, 5, 9, subj) && q.n == 4,
          "a full log refuses (the caller must halt)");
    CHECK(lpx_qlog_verify(&q), "still valid when full");
    /* tamper with each field of each record in turn */
    int caught = 1;
    for (uint32_t k = 0; k < 4; k++) {
        lpx_qrec_t keep = st[k];
        st[k].reason ^= 1;
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].submitter ^= 1;
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].subject[31] ^= 1;
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].tag = lpx_forbid((lpx_t) (st[k].tag ^ 16u));
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].tag = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, 0);
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].tag = (lpx_t) (keep.tag | 64u);
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].seq ^= 1;
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].prev[0] ^= 1;
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        st[k].hash[5] ^= 1;
        caught &= !lpx_qlog_verify(&q);
        st[k] = keep;
        caught &= lpx_qlog_verify(&q);
    }
    CHECK(caught, "any altered field breaks the chain");
    {
        /* a forger rewrites the last record's link and re-hashes it, and
         * the head: only the link check notices */
        lpx_qrec_t keep = st[3];
        uint8_t hk[32];
        memcpy(hk, q.head, 32);
        st[3].prev[0] ^= 1;
        ref_hash(&st[3], st[3].hash);
        memcpy(q.head, st[3].hash, 32);
        CHECK(!lpx_qlog_verify(&q), "a re-hashed record with a broken link is caught");
        st[3] = keep;
        memcpy(q.head, hk, 32);
        CHECK(lpx_qlog_verify(&q), "restored");
        /* the same forgery on the last byte of the link, and on the tag and
         * the sequence number: each re-hashed, head updated */
        for (int which = 0; which < 4; which++) {
            st[3] = keep;
            if (which == 0) st[3].prev[31] ^= 1;
            if (which == 1) st[3].tag = lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, 0);
            if (which == 2) st[3].seq = 7;
            if (which == 3) st[3].tag = (lpx_t) (keep.tag | 64u);
            ref_hash(&st[3], st[3].hash);
            memcpy(q.head, st[3].hash, 32);
            CHECK(!lpx_qlog_verify(&q), "a re-hashed forgery is caught (link, tag or seq)");
        }
        st[3] = keep;
        memcpy(q.head, hk, 32);
        /* altering the stored hash's last byte, with the head following it */
        st[3].hash[31] ^= 1;
        memcpy(q.head, st[3].hash, 32);
        CHECK(!lpx_qlog_verify(&q), "a stored hash that does not match its bytes is caught");
        st[3] = keep;
        memcpy(q.head, hk, 32);
        /* a middle record re-hashed with a changed link: the next one no
         * longer links to it */
        lpx_qrec_t k1 = st[1];
        st[1].prev[31] ^= 1;
        ref_hash(&st[1], st[1].hash);
        CHECK(!lpx_qlog_verify(&q), "a re-hashed middle record breaks the next link");
        st[1] = k1;
        CHECK(lpx_qlog_verify(&q), "restored again");
        /* every record verifies against the reference after appends */
        int all = 1;
        for (int k = 0; k < 4; k++) {
            ref_hash(&st[k], rh);
            all &= !memcmp(rh, st[k].hash, 32);
        }
        CHECK(all, "all four records match the reference hash");
    }
    q.head[0] ^= 1;
    CHECK(!lpx_qlog_verify(&q), "an altered head is caught");
    q.head[0] ^= 1;
    q.head[31] ^= 1;
    CHECK(!lpx_qlog_verify(&q), "an altered last byte of the head is caught");
    q.head[31] ^= 1;
    q.n = 3;
    CHECK(!lpx_qlog_verify(&q), "a truncated log no longer matches its head");
    q.n = 4;
    CHECK(!lpx_qlog_append(0, f, 0, 0, subj) && !lpx_qlog_append(&q, f, 0, 0, 0) &&
              !lpx_qlog_verify(0),
          "NULL arguments refused");
    lpx_qlog_t z;
    lpx_qlog_init(&z, 0, 8);
    CHECK(z.cap == 0 && !lpx_qlog_append(&z, f, 0, 0, subj), "no storage, no appends");
    lpx_qlog_init(0, st, 4);
    lpx_qlog_t o;
    lpx_qlog_init(&o, st, 1);
    CHECK(!lpx_qlog_append(&o, (lpx_t) (f | 64u), 0, 0, subj) && o.n == 0,
          "a tag above six bits is refused");
}

int main(void)
{
    test_layout();
    test_actions();
    test_phase();
    test_forbidden();
    test_polarity();
    test_combine();
    test_qlog();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
