/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_recover.c — the recovery framework (recover.h, audit gap B5, P3.2).
 *
 * Each action and each refusal path, with the digests recomputed here from
 * the documented layouts; the A/B shadow (newest slot first, a tampered
 * slot dropped for the older one, a broken checkpoint never restored);
 * rollback restoring bytes exactly; isolation that sticks; full logs and a
 * full isolation table that change nothing and halt; a failing signer;
 * every proof field tampered and rejected; ML-DSA-87 proofs end to end;
 * and 2,000 random faults after which a hard invariant is never left
 * broken in a live component. */
#include <stdio.h>
#include <string.h>
#include "recover.h"
#include "recover_pq.h"
#include "../pqsec/pq_matrix_algs.h"
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

static uint64_t rng = 0x9E3779B97F4A7C15ull;
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

static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* ---- independent restatements of the byte layouts ---- */
static void ref_state(const uint8_t *s, uint32_t len, uint8_t out[32])
{
    uint8_t b[16 + 4 + 256];
    memcpy(b, "zxv/pdx-state/v1", 16);
    le32(b + 16, len);
    if (len) memcpy(b + 20, s, len);
    sha256(b, 20 + len, out);
}

static void ref_pattern_sig(uint32_t component, uint32_t code, uint8_t out[32])
{
    uint8_t b[14 + 8];
    memcpy(b, "zxv/pdx-sig/v1", 14);
    le32(b + 14, component);
    le32(b + 18, code);
    sha256(b, sizeof b, out);
}

/* paradox.c: "zxv/pdx-repair/v1" || trips || steps || spent || trace_n ||
 * submitter || len || before || after || sig */
static void ref_pdx_what(const pdx_repair_t *r, uint32_t submitter, uint32_t len, uint8_t out[32])
{
    uint8_t b[17 + 24 + 96];
    memcpy(b, "zxv/pdx-repair/v1", 17);
    le32(b + 17, r->trips);
    le32(b + 21, r->steps);
    le32(b + 25, 0);
    le32(b + 29, 0);
    le32(b + 33, submitter);
    le32(b + 37, len);
    memcpy(b + 41, r->before, 32);
    memcpy(b + 73, r->after, 32);
    memcpy(b + 105, r->sig, 32);
    sha256(b, sizeof b, out);
}

/* recover.c: "zxv/rcv-what/v1" || action || trips || component || len ||
 * before || after || sig */
static void ref_rcv_what(uint8_t action, const pdx_repair_t *r, uint32_t component, uint32_t len,
                         uint8_t out[32])
{
    uint8_t b[15 + 1 + 12 + 96];
    memcpy(b, "zxv/rcv-what/v1", 15);
    b[15] = action;
    le32(b + 16, r->trips);
    le32(b + 20, component);
    le32(b + 24, len);
    memcpy(b + 28, r->before, 32);
    memcpy(b + 60, r->after, 32);
    memcpy(b + 92, r->sig, 32);
    sha256(b, sizeof b, out);
}

static void ref_msg(const rcv_proof_t *p, uint8_t out[32])
{
    uint8_t b[16 + 4 + 16 + 24 + 32 * 5];
    uint32_t k = 16;
    memcpy(b, "zxv/rcv-proof/v1", 16);
    b[k++] = p->action;
    b[k++] = p->outcome;
    b[k++] = p->why;
    b[k++] = p->tag;
    le32(b + k, p->component);
    k += 4;
    le32(b + k, p->code);
    k += 4;
    le32(b + k, p->repair.trips);
    k += 4;
    le32(b + k, p->repair.steps);
    k += 4;
    le64(b + k, p->gen);
    k += 8;
    le64(b + k, p->repair.qseq);
    k += 8;
    le64(b + k, p->seq);
    k += 8;
    memcpy(b + k, p->prev, 32);
    memcpy(b + k + 32, p->repair.what, 32);
    memcpy(b + k + 64, p->repair.before, 32);
    memcpy(b + k + 96, p->repair.after, 32);
    memcpy(b + k + 128, p->repair.sig, 32);
    sha256(b, k + 160, out);
}

/* ---- a fast stand-in signer (ML-DSA-87 is tested in its own section) ---- */
typedef struct {
    uint8_t key[32];
    int mode; /* 0 good, 1 fail (0), 2 oversized length */
    int calls;
} fake_t;

static void fake_mac(const uint8_t key[32], const uint8_t msg[32], uint8_t out[32])
{
    uint8_t b[4 + 64];
    memcpy(b, "fake", 4);
    memcpy(b + 4, key, 32);
    memcpy(b + 36, msg, 32);
    sha256(b, sizeof b, out);
}

static uint32_t fake_sign(void *ctx, const uint8_t msg[32], uint8_t *sig, uint32_t cap)
{
    fake_t *f = (fake_t *) ctx;
    f->calls++;
    if (f->mode == 1 || cap < 32) return 0;
    fake_mac(f->key, msg, sig);
    return f->mode == 2 ? RCV_SIG_MAX + 1u : 32u;
}

static bool fake_verify(void *ctx, const uint8_t *pk, uint32_t pk_len, const uint8_t msg[32],
                        const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    if (pk_len != 32 || sig_len != 32) return false;
    uint8_t m[32];
    fake_mac(pk, msg, m);
    return !memcmp(m, sig, 32);
}

/* ---- the hard invariant: conservation over four u32 balances ---- */
#define SLEN 16u
typedef struct {
    uint32_t total;
    int calls;
} cons_t;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static bool conserve(const uint8_t *s, uint32_t len, void *ctx)
{
    cons_t *c = (cons_t *) ctx;
    c->calls++;
    if (!s || len != SLEN) return false;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 4; i++) sum += rd32(s + 4 * i);
    return sum == c->total;
}

static bool first_byte_not_ff(const uint8_t *s, uint32_t len, void *ctx)
{
    (void) ctx;
    return len && s[0] != 0xFF;
}

static void balances(uint8_t s[SLEN], uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    le32(s, a);
    le32(s + 4, b);
    le32(s + 8, c);
    le32(s + 12, d);
}

/* ---- a fixture ---- */
static pdx_rec_t logbuf[8192];
static lpx_qrec_t qbuf[8192];
static uint32_t isobuf[64];
static uint8_t slot_a[SLEN], slot_b[SLEN];

typedef struct {
    pdx_log_t log;
    lpx_qlog_t q;
    rcv_shadow_t sh;
    rcv_t rc;
    fake_t f;
    cons_t cons;
} fx_t;

static fx_t X;
static rcv_proof_t P, P2;

static void fx_init(uint32_t log_cap, uint32_t q_cap, uint32_t iso_cap, bool with_shadow)
{
    memset(&X, 0, sizeof X);
    memset(slot_a, 0, sizeof slot_a);
    memset(slot_b, 0, sizeof slot_b);
    pdx_log_init(&X.log, logbuf, log_cap);
    lpx_qlog_init(&X.q, qbuf, q_cap);
    rcv_shadow_init(&X.sh, slot_a, slot_b, SLEN);
    memset(X.f.key, 0x5A, 32);
    X.cons.total = 1000;
    rcv_init(&X.rc, &X.log, &X.q, with_shadow ? &X.sh : 0, isobuf, iso_cap, conserve, &X.cons,
             fake_sign, &X.f);
}

static rcv_case_t kase(uint32_t component, lpx_t tag, uint32_t code)
{
    rcv_case_t k = {component, tag, code, 0, 0};
    return k;
}

#define T_OK   (lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST))
#define T_BOTH (lpx_make(LPX_B_BOTH, LPX_P_ENTROPIC, LPX_PH_MANIFEST))
#define T_FORB (lpx_make(LPX_B_BOTH, LPX_P_FORBIDDEN, LPX_PH_EMERGING))

static bool verified(const rcv_proof_t *p)
{
    return rcv_proof_verify(p, fake_verify, 0, X.f.key, 32) && rcv_proof_in_log(&X.log, p);
}

/* pdx_log_append's record hash, restated: "zxv/pdx-log/v1" || seq || op ||
 * kind || tag || outcome || aux || a || b || prev. Re-chains from `from`. */
static void rechain(pdx_log_t *l, uint32_t from)
{
    for (uint32_t k = from; k < l->n; k++) {
        pdx_rec_t *r = &l->rec[k];
        if (k) memcpy(r->prev, l->rec[k - 1].hash, 32);
        uint8_t b[14 + 8 + 4 + 4 + 96];
        memcpy(b, "zxv/pdx-log/v1", 14);
        le64(b + 14, r->seq);
        b[22] = r->op;
        b[23] = r->kind;
        b[24] = r->tag;
        b[25] = r->outcome;
        le32(b + 26, r->aux);
        memcpy(b + 30, r->a, 32);
        memcpy(b + 62, r->b, 32);
        memcpy(b + 94, r->prev, 32);
        sha256(b, sizeof b, r->hash);
    }
    memcpy(l->head, l->rec[l->n - 1].hash, 32);
}

/* ---------------------------------------------------------------- */
static void test_basics(void)
{
    printf("[1] names, init, shadow init, bad input\n");
    static const char *const an[3] = {"refuse", "roll-back", "isolate"};
    static const char *const on[7] = {"refused", "rolled-back", "isolated", "unsigned",
                                      "full",    "halted",      "bad-input"};
    int nm = 1;
    for (uint8_t i = 0; i < 3; i++) nm &= !strcmp(rcv_action_name(i), an[i]);
    for (int i = 0; i < 7; i++) nm &= !strcmp(rcv_out_name((rcv_out_t) i), on[i]);
    CHECK(nm && !strcmp(rcv_action_name(3), "?") && !strcmp(rcv_out_name((rcv_out_t) 7), "?"),
          "names, and none past the end");
    CHECK(RCV_OUT_REFUSED == (rcv_out_t) RCV_ACT_REFUSE &&
              RCV_OUT_ROLLED_BACK == (rcv_out_t) RCV_ACT_ROLLBACK &&
              RCV_OUT_ISOLATED == (rcv_out_t) RCV_ACT_ISOLATE,
          "each action's outcome has the action's number");

    rcv_shadow_t sh;
    uint8_t a[4], b[4];
    CHECK(!rcv_shadow_init(0, a, b, 4) && !rcv_shadow_init(&sh, 0, b, 4) &&
              !rcv_shadow_init(&sh, a, 0, 4) && !rcv_shadow_init(&sh, a, a, 4) &&
              !rcv_shadow_init(&sh, a, b, 0),
          "shadow init refuses NULL, one buffer twice, length 0");
    CHECK(rcv_shadow_init(&sh, a, b, 4) && sh.good == RCV_NO_SLOT && sh.next_gen == 1 &&
              !sh.valid[0] && !sh.valid[1] && sh.len == 4,
          "a fresh shadow has no known-good slot and starts at generation 1");

    pdx_log_t l;
    lpx_qlog_t q;
    pdx_log_init(&l, logbuf, 8);
    lpx_qlog_init(&q, qbuf, 8);
    fake_t f = {{0}, 0, 0};
    rcv_t rc;
    uint32_t iso[2];
    CHECK(!rcv_init(0, &l, &q, 0, iso, 2, 0, 0, fake_sign, &f), "init: NULL framework");
    CHECK(!rcv_init(&rc, 0, &q, 0, iso, 2, 0, 0, fake_sign, &f) && rc.halted, "init: no log");
    CHECK(!rcv_init(&rc, &l, 0, 0, iso, 2, 0, 0, fake_sign, &f) && rc.halted,
          "init: no quarantine log");
    CHECK(!rcv_init(&rc, &l, &q, 0, 0, 2, 0, 0, fake_sign, &f) && rc.halted && rc.iso_cap == 0,
          "init: no isolation table");
    CHECK(!rcv_init(&rc, &l, &q, 0, iso, 2, 0, 0, 0, 0) && rc.halted, "init: no signer");
    rcv_case_t k = kase(1, T_OK, 0);
    rcv_proof_t *pp = &P;
    uint8_t st[4] = {0};
    CHECK(rcv_refuse(&rc, &k, 0, 0, pp) == RCV_OUT_HALTED && l.n == 0,
          "a framework that could not be initialised is halted");
    CHECK(rcv_init(&rc, &l, &q, 0, iso, 2, 0, 0, fake_sign, &f) && !rc.halted && rc.iso_n == 0,
          "init with logs, table and signer");

    CHECK(rcv_refuse(0, &k, 0, 0, pp) == RCV_OUT_BAD_INPUT &&
              rcv_refuse(&rc, 0, 0, 0, pp) == RCV_OUT_BAD_INPUT &&
              rcv_refuse(&rc, &k, 0, 0, 0) == RCV_OUT_BAD_INPUT &&
              rcv_refuse(&rc, &k, 0, 4, pp) == RCV_OUT_BAD_INPUT,
          "refuse: NULL arguments, or a length without a state");
    CHECK(rcv_rollback(0, &k, st, 4, pp) == RCV_OUT_BAD_INPUT &&
              rcv_rollback(&rc, 0, st, 4, pp) == RCV_OUT_BAD_INPUT &&
              rcv_rollback(&rc, &k, 0, 4, pp) == RCV_OUT_BAD_INPUT &&
              rcv_rollback(&rc, &k, st, 0, pp) == RCV_OUT_BAD_INPUT &&
              rcv_rollback(&rc, &k, st, 4, 0) == RCV_OUT_BAD_INPUT,
          "rollback: NULL arguments or no state");
    CHECK(rcv_isolate(0, &k, pp) == RCV_OUT_BAD_INPUT &&
              rcv_isolate(&rc, 0, pp) == RCV_OUT_BAD_INPUT &&
              rcv_isolate(&rc, &k, 0) == RCV_OUT_BAD_INPUT,
          "isolate: NULL arguments");
    CHECK(rcv_recover(0, &k, st, 4, pp) == RCV_OUT_BAD_INPUT &&
              rcv_recover(&rc, 0, st, 4, pp) == RCV_OUT_BAD_INPUT &&
              rcv_recover(&rc, &k, st, 4, 0) == RCV_OUT_BAD_INPUT &&
              rcv_recover(&rc, &k, 0, 4, pp) == RCV_OUT_BAD_INPUT,
          "recover: NULL arguments, or a length without a state");
    CHECK(!rcv_checkpoint(0, &k, st, 4, 0) && !rcv_checkpoint(&rc, 0, st, 4, 0) &&
              !rcv_checkpoint(&rc, &k, 0, 4, 0) && !rcv_checkpoint(&rc, &k, st, 0, 0),
          "checkpoint: NULL arguments");
    CHECK(l.n == 0 && q.n == 0 && f.calls == 0 && !rc.halted,
          "bad input logs nothing, signs nothing and does not halt");
    CHECK(!rcv_isolated(0, 1) && !rcv_isolated(&rc, 1), "nothing isolated yet");
}

static void test_checkpoint(void)
{
    printf("[2] checkpoints: the A/B shadow\n");
    fx_init(64, 64, 8, true);
    rcv_case_t k = kase(7, T_OK, 0);
    uint8_t s[SLEN], d[32];
    balances(s, 400, 300, 200, 100);
    uint64_t g = 0;

    rcv_case_t kf = kase(7, T_FORB, 0);
    CHECK(!rcv_checkpoint(&X.rc, &kf, s, SLEN, &g) && X.log.n == 0 && !X.sh.valid[0],
          "a forbidden tag is never checkpointed");
    uint8_t bad[SLEN];
    balances(bad, 400, 300, 200, 101);
    CHECK(!rcv_checkpoint(&X.rc, &k, bad, SLEN, &g) && X.log.n == 0 && !X.sh.valid[0],
          "a state that breaks conservation is never checkpointed");
    rcv_case_t ki = k;
    ki.inv = first_byte_not_ff;
    uint8_t ff[SLEN];
    balances(ff, 0x1FF, 200, 100, 1000 - 0x1FF - 300); /* conserves; first byte 0xFF */
    CHECK(conserve(ff, SLEN, &X.cons) && !rcv_checkpoint(&X.rc, &ki, ff, SLEN, &g) && X.log.n == 0,
          "the case's own invariant also guards checkpoints");
    CHECK(!rcv_checkpoint(&X.rc, &k, s, SLEN - 1, &g) && X.log.n == 0,
          "a length other than the shadow's is refused");

    CHECK(rcv_checkpoint(&X.rc, &k, s, SLEN, &g) && g == 1 && X.sh.good == 0 &&
              X.sh.valid[0] == 1 && !X.sh.valid[1] && !memcmp(slot_a, s, SLEN),
          "first checkpoint: slot A, generation 1, known-good");
    ref_state(s, SLEN, d);
    CHECK(!memcmp(X.sh.digest[0], d, 32), "slot digest = pdx_state_hash, recomputed");
    CHECK(X.log.n == 1 && logbuf[0].op == RCV_OP_CHECKPOINT && logbuf[0].aux == 0 &&
              !memcmp(logbuf[0].a, d, 32) && logbuf[0].tag == T_OK && logbuf[0].kind == 0 &&
              logbuf[0].outcome == 0,
          "checkpoint record: op, slot, digest");

    uint8_t s2[SLEN];
    balances(s2, 250, 250, 250, 250);
    CHECK(rcv_checkpoint(&X.rc, &k, s2, SLEN, &g) && g == 2 && X.sh.good == 1 &&
              !memcmp(slot_b, s2, SLEN) && !memcmp(slot_a, s, SLEN) && X.sh.gen[0] == 1 &&
              X.sh.gen[1] == 2,
          "second checkpoint: into the inactive slot B; A untouched");
    uint8_t s3[SLEN];
    balances(s3, 1000, 0, 0, 0);
    CHECK(rcv_checkpoint(&X.rc, &k, s3, SLEN, 0) && X.sh.good == 0 && X.sh.gen[0] == 3 &&
              !memcmp(slot_a, s3, SLEN) && !memcmp(slot_b, s2, SLEN),
          "third: back to A, generation 3 (NULL gen_out allowed); B is the previous good");
    CHECK(X.log.n == 3 && pdx_log_verify(&X.log) && X.q.n == 0 && X.f.calls == 0,
          "checkpoints are logged, never quarantined or signed");

    /* A slot that overlaps the state it copies: the read-back differs. */
    {
        static uint8_t big[SLEN + 1], other[SLEN];
        rcv_shadow_t sh2;
        rcv_t rc2 = X.rc;
        CHECK(rcv_shadow_init(&sh2, big + 1, other, SLEN), "overlapping shadow");
        rc2.shadow = &sh2;
        balances(big, 400, 300, 200, 100);
        uint32_t n0 = X.log.n;
        CHECK(!rcv_checkpoint(&rc2, &k, big, SLEN, &g) && !sh2.valid[0] &&
                  sh2.good == RCV_NO_SLOT && X.log.n == n0,
              "a torn checkpoint (read-back mismatch) is not made known-good, not logged");
    }
    fx_init(64, 64, 8, false);
    CHECK(!rcv_checkpoint(&X.rc, &k, s, SLEN, &g) && X.log.n == 0, "no shadow: no checkpoint");

    fx_init(1, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, s, SLEN, &g) && !rcv_checkpoint(&X.rc, &k, s2, SLEN, &g) &&
              X.rc.halted && X.sh.good == 0 && !memcmp(slot_a, s, SLEN) && !X.sh.valid[1] &&
              !memcmp(slot_b, (uint8_t[SLEN]){0}, SLEN),
          "a full log: no checkpoint written, halted, the good slot kept");
    CHECK(!rcv_checkpoint(&X.rc, &k, s2, SLEN, &g), "halted: no more checkpoints");
}

static void test_rollback(void)
{
    printf("[3] roll back: bytes restored exactly, the RepairProof is paradox's\n");
    fx_init(1024, 1024, 8, true);
    rcv_case_t k = kase(42, T_BOTH, 0xC0DE);
    uint8_t good[SLEN], s[SLEN], d[32], w[32], m[32];
    balances(good, 100, 200, 300, 400);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    memcpy(s, good, SLEN);
    s[0] = 99; /* breaks conservation */
    s[13] = 7;
    uint8_t before[32];
    ref_state(s, SLEN, before);
    uint32_t n0 = X.log.n, q0 = X.q.n;
    rcv_out_t o = rcv_rollback(&X.rc, &k, s, SLEN, &P);
    CHECK(o == RCV_OUT_ROLLED_BACK && !memcmp(s, good, SLEN),
          "rolled back to the checkpoint bytes");
    CHECK(P.action == RCV_ACT_ROLLBACK && P.outcome == RCV_OUT_ROLLED_BACK &&
              P.why == RCV_WHY_ASKED && P.component == 42 && P.code == 0xC0DE && P.tag == T_BOTH &&
              P.gen == 1,
          "proof header: action, outcome, why, component, code, tag, generation");
    ref_state(good, SLEN, d);
    CHECK(!memcmp(P.repair.before, before, 32) && !memcmp(P.repair.after, d, 32),
          "before = digest of the broken state, after = digest of the checkpoint");
    CHECK(P.repair.trips == PDX_TRIP_INVARIANT && P.repair.steps == 0,
          "trips: the hard invariant (paradox's PDX_TRIP_INVARIANT)");
    ref_pattern_sig(42, 0xC0DE, w);
    CHECK(!memcmp(P.repair.sig, w, 32), "anomaly signature = pdx_signature(component || code)");
    ref_pdx_what(&P.repair, 42, SLEN, w);
    CHECK(!memcmp(P.repair.what, w, 32), "repair.what = paradox's RepairProof layout");
    CHECK(X.q.n == q0 + 1 && P.repair.qseq == q0 && qbuf[q0].submitter == 42 &&
              lpx_forbidden(qbuf[q0].tag) && lpx_base(qbuf[q0].tag) == LPX_B_BOTH &&
              qbuf[q0].reason == PDX_TRIP_INVARIANT && !memcmp(qbuf[q0].subject, P.repair.what, 32),
          "one quarantine record: forbidden tag, submitter, trips, the proof's what");
    CHECK(X.log.n == n0 + 2 && logbuf[n0].op == PDX_OP_DESTRUCTIVE &&
              logbuf[n0].outcome == PDX_OUT_CONTAINED && !memcmp(logbuf[n0].a, P.repair.what, 32),
          "two records: paradox's containment, then the recovery");
    CHECK(P.seq == n0 + 1 && !memcmp(P.prev, logbuf[n0].hash, 32) &&
              !memcmp(P.head, logbuf[n0 + 1].hash, 32) && logbuf[n0 + 1].op == RCV_OP_ROLLBACK &&
              logbuf[n0 + 1].aux == 42 && logbuf[n0 + 1].kind == RCV_WHY_ASKED &&
              logbuf[n0 + 1].outcome == RCV_OUT_ROLLED_BACK && logbuf[n0 + 1].tag == T_BOTH,
          "recovery record: op, component, why, outcome, tag, linked after paradox's");
    ref_msg(&P, m);
    CHECK(!memcmp(P.msg, m, 32) && !memcmp(logbuf[n0 + 1].a, m, 32),
          "signed digest = the documented layout, and it is in the record");
    uint8_t sd[32];
    sha256(P.sig, P.sig_len, sd);
    CHECK(P.sig_len == 32 && !memcmp(logbuf[n0 + 1].b, sd, 32),
          "record carries the signature's digest");
    CHECK(verified(&P) && pdx_log_verify(&X.log) && lpx_qlog_verify(&X.q),
          "proof verifies, is in the log; both logs verify");

    /* A forbidden tag on a state that still conserves: trips FORBIDDEN only. */
    rcv_case_t kf = kase(43, T_FORB, 1);
    balances(s, 500, 500, 0, 0);
    CHECK(rcv_rollback(&X.rc, &kf, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, good, SLEN) &&
              P.repair.trips == PDX_TRIP_FORBIDDEN && verified(&P),
          "forbidden tag: rolled back, trips FORBIDDEN");

    /* The newest checkpoint wins. */
    uint8_t newer[SLEN];
    balances(newer, 1, 2, 3, 994);
    CHECK(rcv_checkpoint(&X.rc, &k, newer, SLEN, 0), "newer checkpoint into slot B");
    memset(s, 0xEE, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, newer, SLEN) &&
              P.gen == 2,
          "rollback uses the newest known-good checkpoint");
    /* Rolling back a healthy state is allowed (the caller asked) and still proven. */
    balances(s, 250, 250, 250, 250);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, newer, SLEN) &&
              P.repair.trips == 0 && verified(&P),
          "an asked-for rollback of a healthy state: no trips, still proven");

    /* Many random corruptions: always the exact bytes. */
    int exact = 1;
    for (int i = 0; i < 200; i++) {
        for (uint32_t j = 0; j < SLEN; j++) s[j] = (uint8_t) rnd();
        if (X.log.cap - X.log.n < 4) break;
        exact &= rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK &&
                 !memcmp(s, newer, SLEN) && !memcmp(slot_b, newer, SLEN);
    }
    CHECK(exact, "random corruptions: every rollback restores the exact bytes, the slot unchanged");
}

static void test_shadow_failures(void)
{
    printf("[4] roll back refused: no checkpoint, tampered, broken, isolated\n");
    rcv_case_t k = kase(5, T_BOTH, 0);
    uint8_t s[SLEN], orig[SLEN], a[SLEN], b[SLEN];
    balances(a, 10, 20, 30, 940);
    balances(b, 40, 50, 60, 850);

    fx_init(64, 64, 8, false);
    memset(s, 3, SLEN);
    memcpy(orig, s, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED &&
              P.why == RCV_WHY_NO_CHECKPOINT && P.action == RCV_ACT_REFUSE &&
              !memcmp(s, orig, SLEN) && verified(&P),
          "no shadow: a signed REFUSE (why NO_CHECKPOINT), state untouched");
    fx_init(64, 64, 8, true);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED &&
              P.why == RCV_WHY_NO_CHECKPOINT && !memcmp(s, orig, SLEN),
          "a shadow with no checkpoint yet: refused");
    CHECK(rcv_checkpoint(&X.rc, &k, a, SLEN, 0) && rcv_checkpoint(&X.rc, &k, b, SLEN, 0),
          "two checkpoints (A older, B newer)");
    uint8_t s15[SLEN - 1];
    memset(s15, 1, sizeof s15);
    CHECK(rcv_rollback(&X.rc, &k, s15, SLEN - 1, &P) == RCV_OUT_REFUSED &&
              P.why == RCV_WHY_NO_CHECKPOINT,
          "a length mismatch: refused");

    /* Tamper with the newer slot: the older known-good one is used. */
    slot_b[2] ^= 0x40;
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, a, SLEN) &&
              P.gen == 1 && !X.sh.valid[1] && X.sh.good == RCV_NO_SLOT,
          "tampered newer slot: dropped for good, the older slot restored");
    slot_b[2] ^= 0x40; /* restoring the bytes does not restore trust */
    memset(s, 3, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, a, SLEN) &&
              !X.sh.valid[1],
          "a slot found bad stays bad");
    /* A new checkpoint goes into the bad slot, not over the last good one. */
    CHECK(rcv_checkpoint(&X.rc, &k, b, SLEN, 0) && X.sh.good == 1 && X.sh.valid[0] &&
              !memcmp(slot_a, a, SLEN),
          "with no known-good slot, a checkpoint fills the bad slot and keeps the good one");
    slot_a[0] ^= 1;
    slot_b[0] ^= 1;
    memset(s, 3, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_TAMPERED &&
              !memcmp(s, orig, SLEN) && !X.sh.valid[0] && !X.sh.valid[1] && verified(&P),
          "both slots tampered: refused (why TAMPERED), state untouched");

    /* A checkpoint that no longer passes the hard invariant is never restored. */
    fx_init(64, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, a, SLEN, 0), "checkpoint");
    X.cons.total = 999; /* the books moved: the checkpoint is now broken */
    memset(s, 3, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_BROKEN &&
              !memcmp(s, orig, SLEN) && X.sh.valid[0],
          "broken checkpoint: refused (why BROKEN), kept but not used");
    X.cons.total = 1000;
    rcv_case_t ki = k;
    ki.inv = first_byte_not_ff;
    slot_a[0] = 0xFF; /* tampered AND would break the case invariant */
    CHECK(rcv_rollback(&X.rc, &ki, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_TAMPERED,
          "a digest mismatch is caught before the invariant runs");
    fx_init(64, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, a, SLEN, 0), "checkpoint");
    CHECK(rcv_rollback(&X.rc, &ki, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, a, SLEN),
          "the case invariant passes on an intact good checkpoint");

    /* Isolated components are not rolled back. */
    fx_init(64, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, a, SLEN, 0), "checkpoint");
    CHECK(rcv_isolate(&X.rc, &k, &P) == RCV_OUT_ISOLATED, "isolate the component");
    memset(s, 3, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_ISOLATED &&
              !memcmp(s, orig, SLEN) && verified(&P),
          "an isolated component's rollback: refused (why ISOLATED)");
    rcv_case_t other = kase(6, T_BOTH, 0);
    CHECK(rcv_rollback(&X.rc, &other, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s, a, SLEN),
          "another component still rolls back");
}

static void test_refuse(void)
{
    printf("[5] refuse: nothing changes, proven; forbidden work is quarantined\n");
    fx_init(64, 64, 8, true);
    rcv_case_t k = kase(9, T_OK, 77);
    uint8_t s[SLEN], orig[SLEN], d[32], w[32];
    balances(s, 250, 250, 250, 250);
    memcpy(orig, s, SLEN);
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && !memcmp(s, orig, SLEN),
          "refused; state unchanged");
    ref_state(s, SLEN, d);
    CHECK(!memcmp(P.repair.before, d, 32) && !memcmp(P.repair.after, d, 32) &&
              P.repair.trips == 0 && P.repair.qseq == RCV_NO_QSEQ && X.q.n == 0 &&
              P.why == RCV_WHY_ASKED && P.gen == 0 && P.repair.steps == 0,
          "before = after = the state; no trips, no quarantine");
    ref_pattern_sig(9, 77, w);
    CHECK(!memcmp(P.repair.sig, w, 32), "anomaly signature");
    ref_rcv_what(RCV_ACT_REFUSE, &P.repair, 9, SLEN, w);
    CHECK(!memcmp(P.repair.what, w, 32), "what = the documented rcv layout");
    CHECK(X.log.n == 1 && logbuf[0].op == RCV_OP_REFUSE && verified(&P), "one record; proven");

    CHECK(rcv_refuse(&X.rc, &k, 0, 0, &P) == RCV_OUT_REFUSED && P.repair.trips == 0,
          "a refusal without a state");
    ref_state(0, 0, d);
    ref_rcv_what(RCV_ACT_REFUSE, &P.repair, 9, 0, w);
    CHECK(!memcmp(P.repair.before, d, 32) && !memcmp(P.repair.what, w, 32) && verified(&P),
          "  hashes the empty state, len 0");
    int calls = X.cons.calls;
    CHECK(calls >= 1, "the invariant ran on the given state");

    rcv_case_t kf = kase(10, T_FORB, 1);
    CHECK(rcv_refuse(&X.rc, &kf, s, SLEN, &P) == RCV_OUT_REFUSED && X.q.n == 1 &&
              P.repair.qseq == 0 && P.repair.trips == PDX_TRIP_FORBIDDEN &&
              qbuf[0].submitter == 10 && lpx_forbidden(qbuf[0].tag) &&
              !memcmp(qbuf[0].subject, P.repair.what, 32) && !memcmp(s, orig, SLEN) && verified(&P),
          "forbidden tag: refused AND quarantined (X6)");
    s[0] ^= 1;
    memcpy(orig, s, SLEN);
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && X.q.n == 2 &&
              P.repair.trips == PDX_TRIP_INVARIANT && !memcmp(s, orig, SLEN),
          "a state breaking the hard invariant: quarantined; still nothing patched");
    CHECK(!rcv_isolated(&X.rc, 9) && !rcv_isolated(&X.rc, 10), "refusal isolates nobody");
}

static void test_isolate(void)
{
    printf("[6] isolate: sticky, quarantined, idempotent\n");
    fx_init(64, 64, 2, true);
    rcv_case_t k = kase(100, T_BOTH, 5);
    uint8_t w[32];
    CHECK(rcv_isolate(&X.rc, &k, &P) == RCV_OUT_ISOLATED && rcv_isolated(&X.rc, 100) &&
              X.rc.iso_n == 1 && isobuf[0] == 100,
          "isolated: in the table");
    CHECK(X.q.n == 1 && P.repair.qseq == 0 && qbuf[0].submitter == 100 &&
              lpx_forbidden(qbuf[0].tag) && P.repair.trips == 0 && qbuf[0].reason == 0,
          "quarantined with a forbidden tag (a clean tag: no trips)");
    ref_rcv_what(RCV_ACT_ISOLATE, &P.repair, 100, 0, w);
    uint8_t z[32] = {0};
    CHECK(!memcmp(P.repair.what, w, 32) && !memcmp(P.repair.before, z, 32) &&
              !memcmp(P.repair.after, z, 32) && verified(&P) && logbuf[0].op == RCV_OP_ISOLATE,
          "what = the rcv layout over zero digests; proven");
    CHECK(rcv_isolate(&X.rc, &k, &P2) == RCV_OUT_ISOLATED && X.rc.iso_n == 1 && X.q.n == 2 &&
              verified(&P2) && P2.seq == P.seq + 1,
          "isolating again: a fresh proof, no second table entry");
    rcv_case_t kf = kase(101, T_FORB, 0);
    CHECK(rcv_isolate(&X.rc, &kf, &P) == RCV_OUT_ISOLATED && P.repair.trips == PDX_TRIP_FORBIDDEN &&
              X.rc.iso_n == 2,
          "a forbidden tag: trips FORBIDDEN");
    uint8_t s[SLEN];
    balances(s, 1, 1, 1, 1);
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_ISOLATED &&
              rcv_isolated(&X.rc, 100),
          "recover on an isolated component: refused, it stays isolated");
    rcv_case_t k3 = kase(102, T_BOTH, 0);
    uint32_t n0 = X.log.n, q0 = X.q.n;
    CHECK(rcv_isolate(&X.rc, &k3, &P) == RCV_OUT_FULL && X.rc.halted && !rcv_isolated(&X.rc, 102) &&
              X.rc.iso_n == 2 && X.log.n == n0 && X.q.n == q0,
          "a full isolation table: nothing changes, halted");
    CHECK(rcv_isolate(&X.rc, &k, &P) == RCV_OUT_HALTED && rcv_isolated(&X.rc, 100),
          "halted: even re-isolation refuses; isolation still holds");
}

static void test_recover_policy(void)
{
    printf("[7] rcv_recover: the policy\n");
    uint8_t s[SLEN], good[SLEN], orig[SLEN];
    balances(good, 600, 200, 100, 100);

    fx_init(64, 64, 8, true);
    rcv_case_t k = kase(1, T_OK, 0);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    balances(s, 250, 250, 250, 250);
    memcpy(orig, s, SLEN);
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_SOFT &&
              !memcmp(s, orig, SLEN) && X.q.n == 0 && verified(&P),
          "no hard break: REFUSE (why SOFT), not even a rollback");
    rcv_case_t kb = kase(1, T_BOTH, 0);
    CHECK(rcv_recover(&X.rc, &kb, s, SLEN, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_SOFT,
          "a Both (destructive-looking) tag alone is not a hard break");
    rcv_case_t kf = kase(1, T_FORB, 0);
    CHECK(rcv_recover(&X.rc, &kf, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && P.why == RCV_WHY_HARD &&
              !memcmp(s, good, SLEN) && verified(&P),
          "forbidden tag: ROLL BACK (why HARD)");
    s[3] = 1;
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && P.why == RCV_WHY_HARD &&
              !memcmp(s, good, SLEN),
          "conservation broken: ROLL BACK");
    rcv_case_t ki = k;
    ki.inv = first_byte_not_ff;
    uint8_t s2[SLEN];
    balances(s2, 0xFF, 1000 - 0xFF, 0, 0);
    CHECK(rcv_recover(&X.rc, &ki, s2, SLEN, &P) == RCV_OUT_ROLLED_BACK && !memcmp(s2, good, SLEN),
          "the case's invariant broken: ROLL BACK");

    slot_a[1] ^= 1;
    s[3] = 1;
    memcpy(orig, s, SLEN);
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ISOLATED && P.why == RCV_WHY_TAMPERED &&
              rcv_isolated(&X.rc, 1) && !memcmp(s, orig, SLEN) && verified(&P),
          "hard break, only a tampered checkpoint: ISOLATE (why TAMPERED)");

    fx_init(64, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    X.cons.total = 7;
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ISOLATED && P.why == RCV_WHY_BROKEN,
          "hard break, only a broken checkpoint: ISOLATE (why BROKEN)");
    fx_init(64, 64, 8, false);
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ISOLATED &&
              P.why == RCV_WHY_NO_CHECKPOINT && rcv_isolated(&X.rc, 1),
          "hard break, no shadow: ISOLATE (why NO_CHECKPOINT)");
    fx_init(64, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    CHECK(rcv_recover(&X.rc, &kf, 0, 0, &P) == RCV_OUT_ISOLATED && P.why == RCV_WHY_NO_CHECKPOINT,
          "forbidden work with no state given: nothing to restore, ISOLATE");
    rcv_case_t k2 = kase(2, T_OK, 0);
    CHECK(rcv_recover(&X.rc, &k2, 0, 0, &P) == RCV_OUT_REFUSED && P.why == RCV_WHY_SOFT,
          "clean work with no state: REFUSE");
}

static void test_full_and_signer(void)
{
    printf("[8] full logs change nothing and halt; a failing signer halts\n");
    uint8_t s[SLEN], good[SLEN], orig[SLEN];
    balances(good, 100, 200, 300, 400);
    rcv_case_t k = kase(3, T_BOTH, 0);

    /* Operator log with room for the checkpoint and one record only. */
    fx_init(2, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    memset(s, 9, SLEN);
    memcpy(orig, s, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_FULL && !memcmp(s, orig, SLEN) &&
              X.log.n == 1 && X.q.n == 0 && X.rc.halted && X.f.calls == 0,
          "rollback needs two records: with one free, nothing changes, halted");
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_HALTED &&
              rcv_isolate(&X.rc, &k, &P) == RCV_OUT_HALTED &&
              rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_HALTED &&
              rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_HALTED && X.log.n == 1 &&
              !memcmp(s, orig, SLEN),
          "halted: every action refuses without logging");

    fx_init(1, 64, 8, true);
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_REFUSED && X.log.n == 1,
          "one refusal fits");
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_FULL && X.rc.halted && X.log.n == 1,
          "the next refusal: FULL");

    /* Quarantine log full. */
    fx_init(64, 0, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_FULL && !memcmp(s, orig, SLEN) &&
              X.log.n == 1,
          "no quarantine room: rollback changes nothing, FULL");
    fx_init(64, 0, 8, true);
    CHECK(rcv_isolate(&X.rc, &k, &P) == RCV_OUT_FULL && !rcv_isolated(&X.rc, 3) && X.log.n == 0,
          "no quarantine room: isolation changes nothing, FULL");
    fx_init(64, 0, 8, true);
    rcv_case_t ko = kase(3, T_OK, 0);
    balances(s, 250, 250, 250, 250);
    CHECK(rcv_refuse(&X.rc, &ko, s, SLEN, &P) == RCV_OUT_REFUSED,
          "a clean refusal needs no quarantine room");
    rcv_case_t kf = kase(3, T_FORB, 0);
    CHECK(rcv_refuse(&X.rc, &kf, s, SLEN, &P) == RCV_OUT_FULL && X.log.n == 1,
          "a forbidden refusal does (X6): FULL");

    /* The signer fails. */
    fx_init(64, 64, 8, true);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    X.f.mode = 1;
    memset(s, 9, SLEN);
    CHECK(rcv_rollback(&X.rc, &k, s, SLEN, &P) == RCV_OUT_UNSIGNED && !memcmp(s, good, SLEN) &&
              X.rc.halted && P.sig_len == 0 && P.outcome == RCV_OUT_UNSIGNED,
          "signer fails: the restore (safe direction) stands, UNSIGNED, halted");
    CHECK(logbuf[X.log.n - 1].outcome == RCV_OUT_UNSIGNED && pdx_log_verify(&X.log) &&
              !rcv_proof_verify(&P, fake_verify, 0, X.f.key, 32) && !rcv_proof_in_log(&X.log, &P),
          "the log says UNSIGNED; the proof does not verify");
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_HALTED, "then halted");
    fx_init(64, 64, 8, true);
    X.f.mode = 2;
    CHECK(rcv_isolate(&X.rc, &k, &P) == RCV_OUT_UNSIGNED && rcv_isolated(&X.rc, 3) &&
              P.sig_len == 0 && X.rc.halted,
          "an oversized signature length counts as a failure; the isolation stands");
    fx_init(64, 64, 8, true);
    X.f.mode = 1;
    CHECK(rcv_refuse(&X.rc, &k, s, SLEN, &P) == RCV_OUT_UNSIGNED && X.rc.halted,
          "an unsigned refusal halts too");
}

static void test_tamper(void)
{
    printf("[9] tampered proofs and logs are rejected\n");
    fx_init(64, 64, 8, true);
    rcv_case_t k = kase(11, T_FORB, 3);
    uint8_t s[SLEN], good[SLEN];
    balances(good, 100, 200, 300, 400);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0) == false, "forbidden: no checkpoint");
    rcv_case_t kc = kase(11, T_OK, 3);
    CHECK(rcv_checkpoint(&X.rc, &kc, good, SLEN, 0), "checkpoint");
    memset(s, 1, SLEN);
    CHECK(rcv_recover(&X.rc, &k, s, SLEN, &P) == RCV_OUT_ROLLED_BACK && verified(&P),
          "a genuine rollback proof");

    int caught = 1, caught_log = 1;
    for (int f = 0; f < 22; f++) {
        rcv_proof_t t = P;
        switch (f) {
        case 0:
            t.action ^= 1;
            break;
        case 1:
            t.outcome ^= 1;
            break;
        case 2:
            t.why ^= 1;
            break;
        case 3:
            t.tag ^= 1;
            break;
        case 4:
            t.component ^= 1;
            break;
        case 5:
            t.code ^= 1;
            break;
        case 6:
            t.gen ^= 1;
            break;
        case 7:
            t.seq ^= 1;
            break;
        case 8:
            t.prev[5] ^= 1;
            break;
        case 9:
            t.repair.what[0] ^= 1;
            break;
        case 10:
            t.repair.before[31] ^= 1;
            break;
        case 11:
            t.repair.after[7] ^= 1;
            break;
        case 12:
            t.repair.sig[9] ^= 1;
            break;
        case 13:
            t.repair.trips ^= 1;
            break;
        case 14:
            t.repair.steps ^= 1;
            break;
        case 15:
            t.repair.qseq ^= 1;
            break;
        case 16:
            t.msg[0] ^= 1;
            break;
        case 17:
            t.sig[0] ^= 1;
            break;
        case 18:
            t.sig[31] ^= 0x80;
            break;
        case 19:
            t.sig_len = 0;
            break;
        case 20:
            t.sig_len = RCV_SIG_MAX + 1;
            break;
        case 21:
            t.sig_len = 31;
            break;
        }
        bool v = rcv_proof_verify(&t, fake_verify, 0, X.f.key, 32);
        if (v) printf("  field %d not caught by verify\n", f);
        caught &= !v;
        bool l = rcv_proof_in_log(&X.log, &t);
        /* head is not part of the signed digest but is checked against the log */
        if (l) printf("  field %d not caught by the log check\n", f);
        caught_log &= !l;
    }
    CHECK(caught, "every proof field changed: signature verification fails");
    CHECK(caught_log, "every proof field changed: the log check fails");
    rcv_proof_t t = P;
    t.head[0] ^= 1;
    CHECK(!rcv_proof_in_log(&X.log, &t), "a wrong head: the log check fails");
    uint8_t wrong[32];
    memset(wrong, 0x5B, 32);
    CHECK(!rcv_proof_verify(&P, fake_verify, 0, wrong, 32) &&
              !rcv_proof_verify(&P, fake_verify, 0, X.f.key, 31),
          "the wrong key, or a wrong key length: rejected");
    CHECK(!rcv_proof_verify(&P, 0, 0, X.f.key, 32) &&
              !rcv_proof_verify(0, fake_verify, 0, X.f.key, 32),
          "no verifier, no proof: rejected (fail closed)");
    CHECK(!rcv_proof_in_log(0, &P) && !rcv_proof_in_log(&X.log, 0), "log check: NULLs");

    /* Recompute the record so the chain still verifies but the paradox
     * record in front of it no longer matches. */
    pdx_rec_t save = logbuf[P.seq - 1];
    logbuf[P.seq - 1].a[0] ^= 1;
    CHECK(!pdx_log_verify(&X.log) && !rcv_proof_in_log(&X.log, &P),
          "an edited paradox record breaks the chain");
    logbuf[P.seq - 1] = save;
    save = logbuf[P.seq];
    logbuf[P.seq].aux ^= 1;
    CHECK(!rcv_proof_in_log(&X.log, &P), "an edited recovery record: rejected");
    logbuf[P.seq] = save;
    CHECK(rcv_proof_in_log(&X.log, &P), "restored log: accepted again");

    /* A refusal proof presented as a rollback. */
    CHECK(rcv_refuse(&X.rc, &kc, s, SLEN, &P2) == RCV_OUT_REFUSED && verified(&P2),
          "a genuine refusal proof");
    t = P2;
    t.action = RCV_ACT_ROLLBACK;
    t.outcome = RCV_OUT_ROLLED_BACK;
    rcv_proof_digest(&t, t.msg);
    fake_mac(X.f.key, t.msg, t.sig); /* even re-signed by the key holder */
    CHECK(rcv_proof_verify(&t, fake_verify, 0, X.f.key, 32) && !rcv_proof_in_log(&X.log, &t),
          "a re-signed proof whose record says otherwise: the log check rejects it");
    t = P2;
    t.outcome = RCV_OUT_UNSIGNED;
    CHECK(!rcv_proof_verify(&t, fake_verify, 0, X.f.key, 32) && !rcv_proof_in_log(&X.log, &t),
          "outcome UNSIGNED never verifies");
    t = P2;
    t.action = 3;
    t.outcome = 3;
    CHECK(!rcv_proof_verify(&t, fake_verify, 0, X.f.key, 32) && !rcv_proof_in_log(&X.log, &t),
          "an unknown action never verifies");
    t = P;
    t.seq = X.log.n;
    CHECK(!rcv_proof_in_log(&X.log, &t), "a seq past the log: rejected");
    /* A rollback proof claimed for record 0 (nothing before it). */
    t = P;
    t.seq = 0;
    CHECK(!rcv_proof_in_log(&X.log, &t), "a rollback at record 0: rejected");

    /* A forger who rewrites one record's metadata and re-chains the whole
     * log (the head is not signed): the record must still match the proof. */
    int meta = 1;
    for (int f = 0; f < 4; f++) {
        uint32_t n = X.log.n;
        static pdx_rec_t keep[64];
        memcpy(keep, logbuf, n * sizeof keep[0]);
        uint8_t keep_head[32];
        memcpy(keep_head, X.log.head, 32);
        switch (f) {
        case 0:
            logbuf[P.seq].kind ^= 1;
            break;
        case 1:
            logbuf[P.seq].aux ^= 1;
            break;
        case 2:
            logbuf[P.seq].tag ^= 1;
            break;
        case 3:
            logbuf[P.seq].outcome ^= 1;
            break;
        }
        rechain(&X.log, (uint32_t) P.seq);
        t = P;
        memcpy(t.head, logbuf[P.seq].hash, 32);
        meta &= pdx_log_verify(&X.log) && !rcv_proof_in_log(&X.log, &t);
        memcpy(logbuf, keep, n * sizeof keep[0]);
        memcpy(X.log.head, keep_head, 32);
    }
    CHECK(meta, "re-chained log with a changed why, component, tag or outcome: rejected");
    CHECK(rcv_proof_in_log(&X.log, &P), "the original log still carries the proof");

    /* A rollback proof whose record is not preceded by paradox's own
     * containment record for the same RepairProof. */
    static pdx_rec_t fb[4];
    for (int good_front = 0; good_front < 2; good_front++) {
        pdx_log_t fl;
        pdx_log_init(&fl, fb, 4);
        pdx_log_append(&fl, PDX_OP_DESTRUCTIVE, PDX_KIND_DESTRUCTIVE, P.tag, PDX_OUT_CONTAINED, 0,
                       good_front ? P.repair.what : P.repair.before, P.repair.sig);
        t = P;
        t.seq = 1;
        memcpy(t.prev, fl.head, 32);
        rcv_proof_digest(&t, t.msg);
        fake_mac(X.f.key, t.msg, t.sig);
        uint8_t sd[32];
        sha256(t.sig, t.sig_len, sd);
        pdx_log_append(&fl, RCV_OP_ROLLBACK, t.why, t.tag, t.outcome, t.component, t.msg, sd);
        memcpy(t.head, fl.head, 32);
        bool in = rcv_proof_in_log(&fl, &t);
        CHECK(rcv_proof_verify(&t, fake_verify, 0, X.f.key, 32) && in == (good_front == 1),
              good_front ? "  control: behind its own containment record it is accepted"
                         : "a rollback record behind another containment record: rejected");
    }
}

static void test_mldsa(void)
{
    printf("[10] ML-DSA-87 RecoveryProofs\n");
    static uint8_t pk[PQM_MLDSA87_PK_BYTES], sk[PQM_MLDSA87_SK_BYTES];
    static uint8_t pk2[PQM_MLDSA87_PK_BYTES], sk2[PQM_MLDSA87_SK_BYTES];
    uint8_t seed[32] = {7}, seed2[32] = {8};
    pqm_mldsa87_keygen(seed, pk, sk);
    pqm_mldsa87_keygen(seed2, pk2, sk2);
    rcv_mldsa87_key_t key = {sk, 0};

    fx_init(64, 64, 8, true);
    X.rc.sign = rcv_mldsa87_sign;
    X.rc.sign_ctx = &key;
    rcv_case_t k = kase(21, T_OK, 4);
    uint8_t s[SLEN], good[SLEN];
    balances(good, 100, 200, 300, 400);
    CHECK(rcv_checkpoint(&X.rc, &k, good, SLEN, 0), "checkpoint");
    memset(s, 2, SLEN);
    rcv_out_t o1 = rcv_recover(&X.rc, &k, s, SLEN, &P);
    CHECK(o1 == RCV_OUT_ROLLED_BACK && P.sig_len == RCV_MLDSA87_SIG &&
              rcv_proof_verify(&P, rcv_mldsa87_verify, 0, pk, sizeof pk) &&
              rcv_proof_in_log(&X.log, &P),
          "rollback: ML-DSA-87 signed and verified");
    rcv_out_t o2 = rcv_refuse(&X.rc, &k, s, SLEN, &P2);
    CHECK(o2 == RCV_OUT_REFUSED && rcv_proof_verify(&P2, rcv_mldsa87_verify, 0, pk, sizeof pk),
          "refusal: signed and verified");
    rcv_proof_t p3;
    CHECK(rcv_isolate(&X.rc, &k, &p3) == RCV_OUT_ISOLATED &&
              rcv_proof_verify(&p3, rcv_mldsa87_verify, 0, pk, sizeof pk) &&
              rcv_proof_in_log(&X.log, &p3),
          "isolation: signed and verified");
    CHECK(!rcv_proof_verify(&P, rcv_mldsa87_verify, 0, pk2, sizeof pk2), "another key: rejected");
    CHECK(!rcv_proof_verify(&P, rcv_mldsa87_verify, 0, pk, sizeof pk - 1),
          "a short public key: rejected");
    rcv_proof_t t = P;
    t.sig[100] ^= 1;
    CHECK(!rcv_proof_verify(&t, rcv_mldsa87_verify, 0, pk, sizeof pk), "a flipped signature bit");
    t = P;
    t.sig_len = RCV_MLDSA87_SIG - 1;
    CHECK(!rcv_proof_verify(&t, rcv_mldsa87_verify, 0, pk, sizeof pk), "a short signature");
    t = P;
    t.repair.after[0] ^= 1;
    rcv_proof_digest(&t, t.msg);
    CHECK(!rcv_proof_verify(&t, rcv_mldsa87_verify, 0, pk, sizeof pk),
          "a changed field with a recomputed digest: the signature no longer matches");
    uint8_t m[32] = {1}, sig[RCV_MLDSA87_SIG];
    CHECK(rcv_mldsa87_sign(&key, m, sig, RCV_MLDSA87_SIG - 1) == 0 &&
              rcv_mldsa87_sign(0, m, sig, sizeof sig) == 0,
          "signer: too little room or no key -> 0");
    rcv_mldsa87_key_t nokey = {0, 0};
    CHECK(rcv_mldsa87_sign(&nokey, m, sig, sizeof sig) == 0, "signer: a NULL secret key -> 0");
    CHECK(!rcv_mldsa87_verify(0, 0, sizeof pk, m, sig, sizeof sig) &&
              !rcv_mldsa87_verify(0, pk, sizeof pk, m, 0, sizeof sig),
          "verifier: NULLs -> false");
    uint8_t m2[32] = {2};
    CHECK(rcv_mldsa87_sign(&key, m, sig, sizeof sig) == RCV_MLDSA87_SIG &&
              rcv_mldsa87_verify(0, pk, sizeof pk, m, sig, sizeof sig) &&
              !rcv_mldsa87_verify(0, pk, sizeof pk, m2, sig, sizeof sig),
          "the binding signs exactly the 32-byte digest");
    /* The domain context is bound: a plain ML-DSA-87 signature without it fails. */
    pqm_mldsa87_sign(sk, m, 32, 0, 0, 0, sig);
    CHECK(!rcv_mldsa87_verify(0, pk, sizeof pk, m, sig, sizeof sig),
          "a signature without the zxv-recover-v1 context: rejected");
}

/* 2,000 random faults: after every recovery a component is either isolated
 * or its live state passes the hard invariant with a non-forbidden tag. */
static void test_property(void)
{
    printf("[11] property: a hard break never stays live\n");
    fx_init(8192, 8192, 64, true);
    enum { NC = 32 };
    uint8_t st[NC][SLEN];
    for (int c = 0; c < NC; c++) balances(st[c], 250, 250, 250, 250);
    rcv_case_t k0 = kase(0, T_OK, 0);
    CHECK(rcv_checkpoint(&X.rc, &k0, st[0], SLEN, 0), "first checkpoint");
    int ok = 1, rolled = 0, isolated = 0, refused = 0, checkpoints = 0;
    for (int i = 0; i < 2000 && ok; i++) {
        uint32_t c = rndn(NC);
        if (X.log.cap - X.log.n < 4 || X.q.cap - X.q.n < 2) break;
        uint32_t r = rndn(10);
        if (r == 0) { /* a fresh healthy checkpoint (any component's books) */
            uint8_t h[SLEN];
            uint32_t a = rndn(1001), b = rndn(1001 - a);
            balances(h, a, b, 0, 1000 - a - b);
            rcv_case_t kc = kase(c, T_OK, 0);
            checkpoints += rcv_checkpoint(&X.rc, &kc, h, SLEN, 0);
            continue;
        }
        if (r == 1) slot_a[rndn(SLEN)] ^= (uint8_t) (1u << rndn(8)); /* bit rot */
        if (r == 2)
            X.cons.total = 1000 + rndn(2); /* the books move */
        else
            X.cons.total = 1000;
        uint8_t *s = st[c];
        if (rndn(2)) s[rndn(SLEN)] = (uint8_t) rnd(); /* a fault */
        lpx_t tag = (lpx_t) rndn(64);
        rcv_case_t k = kase(c, tag, (uint32_t) i);
        bool was_iso = rcv_isolated(&X.rc, c);
        rcv_out_t o = rcv_recover(&X.rc, &k, s, SLEN, &P);
        /* the full log check is O(log length): sample it, verify every signature */
        bool proven = rcv_proof_verify(&P, fake_verify, 0, X.f.key, 32) &&
                      (i % 64 || rcv_proof_in_log(&X.log, &P));
        bool live_ok = !lpx_forbidden(tag) && conserve(s, SLEN, &X.cons);
        if (o == RCV_OUT_ROLLED_BACK) {
            rolled++;
            ok &= conserve(s, SLEN, &X.cons) && proven && P.why == RCV_WHY_HARD;
        } else if (o == RCV_OUT_ISOLATED) {
            isolated++;
            ok &= rcv_isolated(&X.rc, c) && proven && !live_ok;
        } else if (o == RCV_OUT_REFUSED) {
            refused++;
            ok &= proven &&
                  (was_iso ? P.why == RCV_WHY_ISOLATED : (live_ok && P.why == RCV_WHY_SOFT));
        } else {
            ok = 0;
        }
        ok &= rcv_isolated(&X.rc, c) ||
              (conserve(s, SLEN, &X.cons) && (o != RCV_OUT_REFUSED || !lpx_forbidden(tag)));
        if (was_iso) ok &= rcv_isolated(&X.rc, c);
        if (!ok)
            printf("  step %d: component %u, tag %u, outcome %s\n", i, c, tag, rcv_out_name(o));
    }
    CHECK(ok, "every outcome is proven; isolation sticks; no hard break left live");
    CHECK(rolled > 0 && isolated > 0 && refused > 0 && checkpoints > 0,
          "the run exercised rollback, isolation, refusal and checkpoints");
    CHECK(pdx_log_verify(&X.log) && lpx_qlog_verify(&X.q), "both logs verify at the end");
    printf("  (%d rolled back, %d isolated, %d refused, %d checkpoints)\n", rolled, isolated,
           refused, checkpoints);
}

int main(void)
{
    test_basics();
    test_checkpoint();
    test_rollback();
    test_shadow_failures();
    test_refuse();
    test_isolate();
    test_recover_policy();
    test_full_and_signer();
    test_tamper();
    test_mldsa();
    test_property();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
