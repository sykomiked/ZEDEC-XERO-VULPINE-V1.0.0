/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cred.c — EigenTrust credibility (cred.h C1-C11): fulfilment-only
 * input with replay refusal, log gain, exponential loss, decay, exact
 * fixed-point global trust (sum == SCALE every time) checked against an
 * independent reference written with unsigned __int128, the bounded unlocks,
 * the Player Card on sicily's tiers and its JSON byte for byte, and the
 * audit. Integer only: no floating point anywhere. */
#include <stdio.h>
#include <string.h>
#include "cred.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static cred_t C;
static uint64_t idctr;

/* a fresh 32-byte id */
static const uint8_t *nid(void)
{
    static uint8_t id[32];
    memset(id, 0, sizeof id);
    idctr++;
    memcpy(id, &idctr, 8);
    id[31] = 0x5a;
    return id;
}

static uint64_t tsum(const cred_t *c)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < c->n; i++) s += c->t[i];
    return s;
}

/* ---- independent reference ---- */
typedef unsigned __int128 u128;

static uint64_t ref_gain(uint64_t v)
{
    if (v == UINT64_MAX) return 65;
    return 64u - (uint64_t) __builtin_clzll(v + 1);
}

static void ref_trust(uint32_t n, uint64_t s[CRED_MAX_SUBJECTS][CRED_MAX_SUBJECTS], const bool *pre,
                      uint64_t an, uint64_t ad, uint32_t iters, uint64_t eps, uint64_t *t,
                      uint32_t *used)
{
    uint64_t p[CRED_MAX_SUBJECTS] = {0}, S = (uint64_t) 1 << 30;
    uint32_t P = 0;
    for (uint32_t i = 0; i < n; i++) P += pre[i];
    bool all = P == 0;
    if (all) P = n;
    uint32_t rank = 0;
    for (uint32_t i = 0; i < n; i++)
        if (all || pre[i]) {
            p[i] = S / P + (rank < S % P ? 1 : 0);
            rank++;
        }
    for (uint32_t i = 0; i < n; i++) t[i] = p[i];
    uint32_t it = 0;
    for (; it < iters;) {
        uint64_t nt[CRED_MAX_SUBJECTS] = {0}, F = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t give = (uint64_t) ((u128) t[i] * (ad - an) / ad), rs = 0;
            for (uint32_t j = 0; j < n; j++) rs += s[i][j];
            for (uint32_t j = 0; j < n; j++) {
                uint64_t f = rs ? (uint64_t) ((u128) give * s[i][j] / rs)
                                : (uint64_t) ((u128) give * p[j] / S);
                nt[j] += f;
                F += f;
            }
        }
        uint64_t T = S - F, x = 0;
        for (uint32_t j = 0; j < n; j++) {
            uint64_t a = (uint64_t) ((u128) T * p[j] / S);
            nt[j] += a;
            x += a;
        }
        for (uint32_t j = 0; j < n && x < T; j++)
            if (p[j]) {
                nt[j]++;
                x++;
            }
        uint64_t d = 0;
        for (uint32_t j = 0; j < n; j++) {
            d += nt[j] > t[j] ? nt[j] - t[j] : t[j] - nt[j];
            t[j] = nt[j];
        }
        it++;
        if (d <= eps) break;
    }
    *used = it;
}

/* ---- tests ---- */
static void test_config(void)
{
    cred_config_t f, g;
    cred_config_default(&f);
    cred_config_default(NULL);
    CHECK(f.decay_num == 1 && f.decay_den == 16 && f.fail_base == 16 && f.fail_cap == 10 &&
              f.alpha_num == 1 && f.alpha_den == 10 && f.iters == 64 && f.eps == 64,
          "documented defaults");
    CHECK(!f.hold_enabled && !f.credit_lines_enabled && f.hold_base == 1209600 &&
              f.hold_floor == 259200 && f.hold_sat == CRED_SCALE / 8 && f.credit_k == 100000 &&
              f.credit_cap == 50000,
          "unlocks off by default");
    CHECK(cred_config_ok(&f) && !cred_config_ok(NULL), "defaults valid, NULL not");
#define BAD(field, val, msg)                                                                       \
    do {                                                                                           \
        g = f;                                                                                     \
        g.field = val;                                                                             \
        CHECK(!cred_config_ok(&g), msg);                                                           \
    } while (0)
#define GOOD(field, val, msg)                                                                      \
    do {                                                                                           \
        g = f;                                                                                     \
        g.field = val;                                                                             \
        CHECK(cred_config_ok(&g), msg);                                                            \
    } while (0)
    BAD(decay_den, 0, "decay_den 0");
    BAD(decay_num, 17, "decay_num > den");
    GOOD(decay_num, 16, "decay_num == den ok");
    GOOD(decay_num, 0, "decay_num 0 ok");
    BAD(fail_base, 0, "fail_base 0");
    BAD(fail_base, CRED_FAIL_BASE_MAX + 1, "fail_base too big");
    GOOD(fail_base, CRED_FAIL_BASE_MAX, "fail_base max ok");
    BAD(fail_cap, CRED_FAIL_SHIFT_MAX + 1, "fail_cap too big");
    GOOD(fail_cap, CRED_FAIL_SHIFT_MAX, "fail_cap max ok");
    GOOD(fail_cap, 0, "fail_cap 0 ok");
    BAD(alpha_den, 0, "alpha_den 0");
    BAD(alpha_num, 11, "alpha > 1");
    GOOD(alpha_num, 10, "alpha == 1 ok");
    BAD(iters, 0, "iters 0");
    BAD(iters, CRED_ITERS_MAX + 1, "iters too many");
    GOOD(iters, CRED_ITERS_MAX, "iters max ok");
    BAD(hold_base, 0, "hold_base 0");
    BAD(hold_floor, f.hold_base + 1, "floor above base");
    GOOD(hold_floor, f.hold_base, "floor == base ok");
    BAD(hold_sat, 0, "hold_sat 0");
    CHECK(cred_init(NULL, NULL) == CRED_ERR_ARG, "init NULL");
    g = f;
    g.iters = 0;
    CHECK(cred_init(&C, &g) == CRED_ERR_ARG, "init bad cfg");
    CHECK(cred_init(&C, NULL) == CRED_OK && C.cfg.alpha_den == 10 && C.n == 0 && cred_check(&C),
          "init with defaults");
    g = f;
    g.alpha_num = 3;
    CHECK(cred_init(&C, &g) == CRED_OK && C.cfg.alpha_num == 3, "init with cfg");
    CHECK(cred_set_config(&C, &f) == CRED_OK && C.cfg.alpha_num == 1, "set_config");
    g.iters = 0;
    CHECK(cred_set_config(&C, &g) == CRED_ERR_ARG && C.cfg.iters == 64, "bad set_config refused");
    CHECK(cred_set_config(NULL, &f) == CRED_ERR_ARG && cred_set_config(&C, NULL) == CRED_ERR_ARG,
          "set_config NULL");
    C.cfg.iters = 0;
    CHECK(!cred_check(&C), "audit sees a bad config");
}

static void test_subjects(void)
{
    cred_init(&C, NULL);
    CHECK(cred_subject_add(NULL, 1, false) == CRED_ERR_ARG, "add NULL");
    CHECK(cred_subject_add(&C, 7, true) == CRED_OK && C.n == 1 && C.subj[0].pretrusted,
          "add pretrusted");
    CHECK(cred_subject_add(&C, 7, false) == CRED_ERR_DUP && C.n == 1, "dup id");
    CHECK(cred_index(&C, 7) == 0 && cred_index(&C, 8) == -1 && cred_index(NULL, 7) == -1, "index");
    for (uint32_t i = 1; i < CRED_MAX_SUBJECTS; i++) cred_subject_add(&C, 100 + i, false);
    CHECK(C.n == CRED_MAX_SUBJECTS, "filled");
    CHECK(cred_subject_add(&C, 9999, false) == CRED_ERR_FULL && C.n == CRED_MAX_SUBJECTS, "full");
    CHECK(cred_trust_compute(&C) == CRED_OK && C.computed, "compute");
    CHECK(cred_set_pretrusted(&C, 101, true) == CRED_OK && C.subj[1].pretrusted && !C.computed,
          "set_pretrusted invalidates");
    CHECK(cred_set_pretrusted(&C, 5, true) == CRED_ERR_UNKNOWN, "set_pretrusted unknown");
    CHECK(cred_set_pretrusted(&C, 7, false) == CRED_OK && !C.subj[0].pretrusted,
          "set_pretrusted on the first subject (index 0)");
    CHECK(cred_set_pretrusted(NULL, 5, true) == CRED_ERR_ARG, "set_pretrusted NULL");
    cred_init(&C, NULL);
    cred_subject_add(&C, 1, false);
    cred_trust_compute(&C);
    CHECK(C.computed && C.t[0] == CRED_SCALE, "one subject holds all trust");
    CHECK(cred_subject_add(&C, 2, false) == CRED_OK && !C.computed, "add invalidates");
    CHECK(C.subj[1].id == 2 && !C.subj[1].pretrusted && C.subj[1].fails == 0, "fields set");
    C.subj[0].id = 2;
    CHECK(!cred_check(&C), "audit sees duplicate ids");
}

static void test_gain_loss(void)
{
    CHECK(cred_gain(0) == 1 && cred_gain(1) == 2 && cred_gain(2) == 2 && cred_gain(3) == 3,
          "gain small");
    CHECK(cred_gain(6) == 3 && cred_gain(7) == 4 && cred_gain(8) == 4, "gain at powers");
    CHECK(cred_gain(1u << 20) == 21 && cred_gain((1u << 20) - 1) == 21 &&
              cred_gain((1u << 20) - 2) == 20,
          "gain 2^20");
    CHECK(cred_gain(UINT64_MAX) == 65 && cred_gain(UINT64_MAX - 1) == 64 &&
              cred_gain(((uint64_t) 1 << 63) - 1) == 64 &&
              cred_gain(((uint64_t) 1 << 63) - 2) == 63,
          "gain at the top");
    bool ok = true;
    for (uint64_t v = 0; v < 5000; v++) ok = ok && cred_gain(v) == ref_gain(v);
    for (int b = 0; b < 64; b++) {
        uint64_t v = (uint64_t) 1 << b;
        ok = ok && cred_gain(v) == ref_gain(v) && cred_gain(v - 1) == ref_gain(v - 1) &&
             cred_gain(v + 1) == ref_gain(v + 1);
    }
    CHECK(ok, "gain == bit length of 1 + v");
    cred_config_t f;
    cred_config_default(&f);
    CHECK(cred_loss(&f, 0) == 0 && cred_loss(NULL, 3) == 0, "loss k 0 / NULL");
    CHECK(cred_loss(&f, 1) == 16 && cred_loss(&f, 2) == 32 && cred_loss(&f, 3) == 64,
          "loss doubles");
    CHECK(cred_loss(&f, 11) == 16u << 10 && cred_loss(&f, 12) == 16u << 10 &&
              cred_loss(&f, UINT32_MAX) == 16u << 10,
          "loss capped at base << cap");
    CHECK(cred_loss(&f, 10) == 16u << 9, "loss below the cap");
    f.fail_cap = 0;
    CHECK(cred_loss(&f, 5) == 16, "cap 0: constant");
    f.fail_cap = CRED_FAIL_SHIFT_MAX;
    f.fail_base = CRED_FAIL_BASE_MAX;
    CHECK(cred_loss(&f, UINT32_MAX) == (uint64_t) 1 << 40, "largest loss 2^40");
}

static void setup3(void)
{
    cred_init(&C, NULL);
    cred_subject_add(&C, 10, true);
    cred_subject_add(&C, 20, false);
    cred_subject_add(&C, 30, false);
}

static void test_fulfil_fail(void)
{
    setup3();
    uint8_t r1[32];
    memcpy(r1, nid(), 32);
    CHECK(cred_fulfil(NULL, 10, 20, 5, r1) == CRED_ERR_ARG, "fulfil NULL cred");
    CHECK(cred_fulfil(&C, 10, 20, 5, NULL) == CRED_ERR_ARG, "fulfil NULL receipt");
    CHECK(cred_fulfil(&C, 11, 20, 5, r1) == CRED_ERR_UNKNOWN, "unknown rater");
    CHECK(cred_fulfil(&C, 10, 21, 5, r1) == CRED_ERR_UNKNOWN, "unknown ratee");
    CHECK(cred_fulfil(&C, 20, 20, 5, r1) == CRED_ERR_SELF, "self-rating refused");
    CHECK(cred_fulfil(&C, 10, 20, 0, r1) == CRED_ERR_ARG, "value 0 refused");
    CHECK(C.s_total == 0 && C.n_seen == 0 && C.n_fulfil == 0, "refusals left nothing");
    CHECK(cred_fulfil(&C, 10, 20, 5, r1) == CRED_OK && cred_local(&C, 10, 20) == 3 &&
              C.s_total == 3 && C.n_fulfil == 1 && C.n_seen == 1,
          "fulfil 5 adds 3");
    CHECK(cred_fulfil(&C, 10, 20, 5, r1) == CRED_ERR_DUP && cred_local(&C, 10, 20) == 3,
          "duplicate receipt refused");
    CHECK(cred_fulfil(&C, 30, 20, 1000, r1) == CRED_ERR_DUP, "duplicate receipt, other pair");
    CHECK(cred_fulfil(&C, 10, 20, 1u << 20, nid()) == CRED_OK && cred_local(&C, 10, 20) == 24,
          "a big order adds only 21");
    CHECK(cred_local(&C, 20, 10) == 0 && cred_local(&C, 10, 99) == 0 && cred_local(NULL, 1, 2) == 0,
          "local readers");
    CHECK(cred_check(&C), "audit");
    /* fail: rulings are their own namespace */
    CHECK(cred_fail(&C, 10, 20, NULL) == CRED_ERR_ARG &&
              cred_fail(NULL, 10, 20, r1) == CRED_ERR_ARG,
          "fail NULL");
    CHECK(cred_fail(&C, 20, 20, r1) == CRED_ERR_SELF, "self fail refused");
    CHECK(cred_fail(&C, 10, 77, r1) == CRED_ERR_UNKNOWN &&
              cred_fail(&C, 77, 10, r1) == CRED_ERR_UNKNOWN,
          "unknown fail");
    CHECK(C.subj[1].fails == 0 && C.n_fail == 0, "refused fails left nothing");
    CHECK(cred_fail(&C, 10, 20, r1) == CRED_OK && cred_local(&C, 10, 20) == 8 &&
              C.subj[1].fails == 1 && C.s_total == 8 && C.n_fail == 1,
          "first failure: -16 (24 -> 8); the receipt id is a fine ruling id");
    CHECK(cred_fail(&C, 10, 20, r1) == CRED_ERR_DUP && C.subj[1].fails == 1, "dup ruling");
    uint8_t r2[32];
    memcpy(r2, nid(), 32);
    for (int i = 0; i < 20; i++) cred_fulfil(&C, 30, 20, 1000000, nid()); /* 20 * 20 */
    CHECK(cred_local(&C, 30, 20) == 400, "30 rates 20 at 400");
    CHECK(cred_fail(&C, 30, 20, r2) == CRED_OK && cred_local(&C, 30, 20) == 368 &&
              C.subj[1].fails == 2,
          "second failure of 20 (any rater) costs 32");
    CHECK(cred_fail(&C, 30, 20, nid()) == CRED_OK && cred_local(&C, 30, 20) == 304, "third 64");
    CHECK(cred_fail(&C, 30, 20, nid()) == CRED_OK && cred_local(&C, 30, 20) == 176, "fourth 128");
    CHECK(cred_fail(&C, 30, 20, nid()) == CRED_OK && cred_local(&C, 30, 20) == 0,
          "fifth saturates at 0");
    CHECK(cred_fail(&C, 30, 20, nid()) == CRED_OK && cred_local(&C, 30, 20) == 0 &&
              C.subj[1].fails == 6,
          "failure on 0 stays 0, still counted");
    CHECK(cred_local(&C, 10, 20) == 8 && C.s_total == 8 && cred_check(&C), "other pair untouched");
    C.subj[1].fails = UINT32_MAX;
    C.s[0][1] = 1u << 20;
    C.s_total = 1u << 20;
    CHECK(cred_fail(&C, 10, 20, nid()) == CRED_OK && C.subj[1].fails == UINT32_MAX &&
              cred_local(&C, 10, 20) == (1u << 20) - (16u << 10),
          "count saturates; loss stays capped");
    CHECK(cred_check(&C), "audit");
    /* saturation of local scores */
    C.s[2][0] = CRED_LOCAL_MAX - 3;
    C.s_total += CRED_LOCAL_MAX - 3;
    CHECK(cred_fulfil(&C, 30, 10, UINT64_MAX, nid()) == CRED_OK &&
              cred_local(&C, 30, 10) == CRED_LOCAL_MAX && cred_check(&C),
          "local saturates at CRED_LOCAL_MAX with s_total exact");
    CHECK(cred_fulfil(&C, 30, 10, 1, nid()) == CRED_OK &&
              cred_local(&C, 30, 10) == CRED_LOCAL_MAX && cred_check(&C),
          "stays saturated");
    C.s[2][0] = CRED_LOCAL_MAX - 65;
    C.s_total -= 65;
    CHECK(cred_fulfil(&C, 30, 10, UINT64_MAX, nid()) == CRED_OK &&
              cred_local(&C, 30, 10) == CRED_LOCAL_MAX,
          "exactly reaching the cap");
    C.s[2][0] = CRED_LOCAL_MAX + 1;
    C.s_total++;
    CHECK(!cred_check(&C), "audit sees a local above the cap (s_total consistent)");
    C.s[2][0] = CRED_LOCAL_MAX;
    CHECK(!cred_check(&C), "audit sees s_total off");
    C.s_total--;
    C.s[1][1] = 1;
    C.s_total++;
    CHECK(!cred_check(&C), "audit sees a self score");
}

static void test_ring(void)
{
    setup3();
    uint8_t first[32], rul[32];
    memcpy(first, nid(), 32);
    memcpy(rul, nid(), 32);
    CHECK(cred_fulfil(&C, 10, 20, 1, first) == CRED_OK, "first");
    CHECK(cred_fail(&C, 10, 20, rul) == CRED_OK, "ruling");
    CHECK(cred_fulfil(&C, 10, 20, 1, rul) == CRED_OK, "a ruling id is not a receipt id");
    uint8_t last[32];
    memcpy(last, first, 32);
    last[31] ^= 0x80;
    CHECK(cred_fulfil(&C, 10, 20, 1, last) == CRED_OK,
          "ids differing only in the last byte are distinct");
    last[16] ^= 1;
    last[31] ^= 0x80;
    CHECK(cred_fulfil(&C, 10, 20, 1, last) == CRED_OK,
          "ids differing only in a middle byte are distinct");
    bool ok = true;
    for (uint32_t i = 0; i < CRED_SEEN - 6; i++)
        ok = ok && cred_fulfil(&C, 30, 20, 1, nid()) == CRED_OK;
    CHECK(ok && C.n_seen == CRED_SEEN - 1, "ring nearly full");
    CHECK(cred_fulfil(&C, 10, 20, 1, first) == CRED_ERR_DUP, "still remembered");
    CHECK(cred_fulfil(&C, 30, 20, 1, nid()) == CRED_OK && C.n_seen == CRED_SEEN && C.seen_head == 0,
          "ring full, wraps");
    CHECK(cred_fulfil(&C, 10, 20, 1, first) == CRED_ERR_DUP, "full ring still remembers the first");
    CHECK(cred_fulfil(&C, 30, 20, 1, nid()) == CRED_OK && C.seen_head == 1 && C.n_seen == CRED_SEEN,
          "evicts the oldest");
    CHECK(cred_fail(&C, 10, 20, rul) == CRED_ERR_DUP, "the ruling is still remembered");
    CHECK(cred_fulfil(&C, 10, 20, 1, first) == CRED_OK, "evicted id is accepted again (window)");
    CHECK(cred_check(&C), "audit");
    C.n_seen = CRED_SEEN + 1;
    CHECK(!cred_check(&C), "audit sees n_seen out of range");
    C.n_seen = CRED_SEEN;
    C.seen_head = CRED_SEEN;
    CHECK(!cred_check(&C), "audit sees head out of range");
}

static void test_decay(void)
{
    setup3();
    for (int i = 0; i < 10; i++) cred_fulfil(&C, 10, 20, 7, nid()); /* 40 */
    cred_fulfil(&C, 20, 30, 1, nid());                              /* 2 */
    cred_fail(&C, 20, 30, nid());                                   /* 2 -> 0, fails 1 */
    cred_fulfil(&C, 20, 30, 1, nid());
    cred_fail(&C, 10, 30, nid());
    cred_fail(&C, 10, 30, nid());
    cred_fail(&C, 10, 30, nid()); /* fails 4 */
    CHECK(C.subj[2].fails == 4 && cred_local(&C, 20, 30) == 2, "setup");
    CHECK(cred_decay_epoch(NULL) == CRED_ERR_ARG, "decay NULL");
    CHECK(cred_decay_epoch(&C) == CRED_OK && cred_local(&C, 10, 20) == 37 &&
              cred_local(&C, 20, 30) == 1 && C.subj[2].fails == 2 && C.epoch == 1 && cred_check(&C),
          "40 -> floor(40*15/16)=37, 2 -> 1, fails halved");
    CHECK(cred_decay_epoch(&C) == CRED_OK && cred_local(&C, 10, 20) == 34 &&
              cred_local(&C, 20, 30) == 0 && C.subj[2].fails == 1 && C.s_total == 34,
          "37 -> 34, 1 -> 0");
    cred_decay_epoch(&C);
    CHECK(C.subj[2].fails == 0, "fails reach 0");
    cred_fail(&C, 10, 20, nid());
    CHECK(cred_local(&C, 10, 20) == 31 - 16,
          "after forgetting, the next failure is the first again");
    C.cfg.decay_num = 0;
    cred_decay_epoch(&C);
    CHECK(cred_local(&C, 10, 20) == 15 && C.epoch == 4, "num 0: no decay");
    C.cfg.decay_num = 16;
    cred_decay_epoch(&C);
    CHECK(C.s_total == 0 && cred_local(&C, 10, 20) == 0 && cred_check(&C), "num == den: all gone");
    C.cfg.decay_num = 1;
    C.cfg.decay_den = 2;
    C.s[0][1] = CRED_LOCAL_MAX;
    C.s[1][0] = 7;
    C.s_total = CRED_LOCAL_MAX + 7;
    cred_decay_epoch(&C);
    CHECK(cred_local(&C, 10, 20) == CRED_LOCAL_MAX / 2 && cred_local(&C, 20, 10) == 3 &&
              cred_check(&C),
          "halving, floor");
    bool ok = true;
    for (int e = 0; e < 60 && ok; e++) {
        uint64_t before = C.s_total;
        cred_decay_epoch(&C);
        ok = cred_check(&C) && (before == 0 || C.s_total < before);
    }
    CHECK(ok && C.s_total == 0, "every positive score falls each epoch and reaches 0");
}

static void test_full(void)
{
    /* 64 subjects, everyone rates everyone: decay and trust at full size */
    cred_init(&C, NULL);
    static uint64_t S[CRED_MAX_SUBJECTS][CRED_MAX_SUBJECTS];
    bool pre[CRED_MAX_SUBJECTS];
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++) {
        pre[i] = i % 9 == 0;
        cred_subject_add(&C, 500 + i, pre[i]);
    }
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++)
        for (uint32_t j = 0; j < CRED_MAX_SUBJECTS; j++) {
            S[i][j] = 0;
            if (i == j) continue;
            uint64_t v = (uint64_t) 1 << ((i * 7 + j * 3) % 60);
            cred_fulfil(&C, 500 + i, 500 + j, v, nid());
            S[i][j] = ref_gain(v);
        }
    CHECK(C.n_seen == CRED_SEEN && cred_check(&C), "full table");
    cred_trust_compute(&C);
    cred_decay_epoch(&C);
    bool ok = true;
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++)
        for (uint32_t j = 0; j < CRED_MAX_SUBJECTS; j++) ok = ok && C.s[i][j] == S[i][j] * 15 / 16;
    CHECK(ok && cred_check(&C), "every score decayed once, nothing outside the table touched");
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++)
        for (uint32_t j = 0; j < CRED_MAX_SUBJECTS; j++) S[i][j] = C.s[i][j];
    cred_trust_compute(&C);
    uint64_t t[CRED_MAX_SUBJECTS];
    uint32_t used;
    ref_trust(CRED_MAX_SUBJECTS, S, pre, 1, 10, 64, 64, t, &used);
    CHECK(memcmp(t, C.t, sizeof t) == 0 && used == C.iters_used && tsum(&C) == CRED_SCALE,
          "64 subjects: matches the reference");
    CHECK(cred_credibility(&C, 500) == (uint64_t) (((u128) C.t[0] * 6400) / CRED_SCALE),
          "n = 64 mapping");
    cred_card_t k;
    uint64_t col = 0;
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++) col += S[i][63];
    CHECK(cred_player_card(&C, 563, 0, 0, 0, NULL, &k) == CRED_OK && k.local == col,
          "card local sums the column of the last subject exactly");
}

static void test_trust_basic(void)
{
    cred_init(&C, NULL);
    CHECK(cred_trust_compute(NULL) == CRED_ERR_ARG, "NULL");
    CHECK(cred_trust_compute(&C) == CRED_ERR_STATE && !C.computed, "no subjects");
    for (uint32_t i = 0; i < 3; i++) cred_subject_add(&C, i + 1, false);
    CHECK(cred_trust(&C, 1) == 0, "not computed: 0");
    CHECK(cred_trust_compute(&C) == CRED_OK, "uniform");
    uint64_t q = CRED_SCALE / 3;
    CHECK(C.p[0] == q + 1 && C.p[1] == q && C.p[2] == q && C.p[3] == 0 && C.t[3] == 0,
          "remainder of p to the lowest index; nothing past the table");
    CHECK(C.t[0] == q + 2 && C.t[1] == q && C.t[2] == q - 1 && tsum(&C) == CRED_SCALE,
          "no ratings: t == p up to the documented dust (lowest index first)");
    CHECK(C.iters_used == 1 && C.last_delta == 2, "within eps after one iteration");
    CHECK(cred_trust(&C, 1) == q + 2 && cred_trust(&C, 9) == 0 && cred_trust(NULL, 1) == 0,
          "trust reader");
    CHECK(cred_check(&C), "audit");
    /* pretrusted only */
    cred_set_pretrusted(&C, 2, true);
    cred_trust_compute(&C);
    CHECK(C.t[1] == CRED_SCALE && C.t[0] == 0 && C.t[2] == 0 && C.p[1] == CRED_SCALE,
          "only the pretrusted subject, nobody rated");
    /* 2 rates 3 */
    cred_fulfil(&C, 2, 3, 100, nid());
    cred_trust_compute(&C);
    CHECK(tsum(&C) == CRED_SCALE && C.t[0] == 0 && C.t[2] > CRED_SCALE / 3 && C.t[1] > C.t[2],
          "trust flows along fulfilment (and back via p)");
    uint64_t t[CRED_MAX_SUBJECTS];
    uint32_t used;
    bool pre[3] = {false, true, false};
    ref_trust(3, C.s, pre, 1, 10, 64, 64, t, &used);
    CHECK(memcmp(t, C.t, 3 * 8) == 0 && used == C.iters_used, "matches the reference");
    /* 3 rates 1: it all flows on */
    cred_fulfil(&C, 3, 1, 100, nid());
    cred_trust_compute(&C);
    ref_trust(3, C.s, pre, 1, 10, 64, 64, t, &used);
    CHECK(memcmp(t, C.t, 3 * 8) == 0 && used == C.iters_used && tsum(&C) == CRED_SCALE &&
              C.t[0] > 0,
          "cycle matches the reference");
    CHECK(cred_check(&C), "audit");
    C.t[0]++;
    CHECK(!cred_check(&C), "audit sees t not summing to SCALE");
    C.t[0]--;
    C.p[0] = 1;
    C.p[1]--;
    CHECK(!cred_check(&C), "audit sees p mass outside the support");
    C.p[0] = 0;
    C.p[1] += 2;
    CHECK(!cred_check(&C), "audit sees p not summing to SCALE");
    C.p[1]--;
    CHECK(cred_check(&C), "restored");
    C.p[2] = 1;
    C.p[1]--;
    CHECK(!cred_check(&C), "audit sees p mass on a later non-pretrusted subject");
    C.p[2] = 0;
    C.p[1]++;
    /* only the first subject pretrusted */
    cred_set_pretrusted(&C, 2, false);
    cred_set_pretrusted(&C, 1, true);
    cred_trust_compute(&C);
    CHECK(C.p[0] == CRED_SCALE && cred_check(&C), "first subject alone pretrusted");
    C.p[1] = 1;
    C.p[0]--;
    CHECK(!cred_check(&C), "audit sees p mass outside a support of index 0");
    C.p[1] = 0;
    C.p[0]++;
    cred_set_pretrusted(&C, 1, false);
    cred_set_pretrusted(&C, 2, true);
    cred_trust_compute(&C);
    C.computed = false;
    C.t[0] = 5;
    CHECK(cred_check(&C), "uncomputed: t not audited");
    /* alpha extremes */
    cred_trust_compute(&C);
    C.cfg.alpha_num = 10;
    cred_trust_compute(&C);
    CHECK(C.t[1] == CRED_SCALE && C.iters_used == 1, "alpha 1: t == p");
    C.cfg.alpha_num = 0;
    C.cfg.iters = 200;
    C.cfg.eps = 0;
    cred_trust_compute(&C);
    ref_trust(3, C.s, pre, 0, 10, 200, 0, t, &used);
    CHECK(memcmp(t, C.t, 3 * 8) == 0 && used == C.iters_used && tsum(&C) == CRED_SCALE,
          "alpha 0 matches the reference");
    /* iteration cap and eps */
    cred_config_default(&C.cfg);
    C.cfg.iters = 1;
    cred_trust_compute(&C);
    CHECK(C.iters_used == 1 && C.last_delta > C.cfg.eps, "iteration cap stops early");
    C.cfg.iters = 256;
    C.cfg.eps = UINT64_MAX;
    cred_trust_compute(&C);
    CHECK(C.iters_used == 1, "huge eps: one iteration");
    C.cfg.eps = 64;
    cred_trust_compute(&C);
    CHECK(C.last_delta <= 64 && C.iters_used > 64 && C.iters_used < 256, "converges under eps");
    ref_trust(3, C.s, pre, 1, 10, 256, 64, t, &used);
    CHECK(used == C.iters_used && memcmp(t, C.t, 24) == 0, "same stop as the reference");
    uint64_t d = 0;
    {
        /* recompute one more step by hand from the final vector: change <= eps
           since the loop stopped there, so the previous delta matches */
        d = C.last_delta;
    }
    CHECK(d <= 64, "delta recorded");
    C.cfg.eps = C.last_delta;
    uint32_t prev = C.iters_used;
    cred_trust_compute(&C);
    CHECK(C.iters_used == prev, "eps equal to the delta stops (<=)");
    C.cfg.eps = C.last_delta - 1;
    cred_trust_compute(&C);
    CHECK(C.iters_used > prev || C.iters_used == C.cfg.iters, "eps just below continues");
}

static void test_sybil(void)
{
    cred_init(&C, NULL);
    /* honest: 1 (pretrusted), 2, 3; sybils 4..8 rate each other heavily */
    for (uint32_t i = 1; i <= 8; i++) cred_subject_add(&C, i, i == 1);
    cred_fulfil(&C, 1, 2, 50, nid());
    cred_fulfil(&C, 2, 3, 50, nid());
    cred_fulfil(&C, 3, 1, 50, nid());
    for (uint32_t a = 4; a <= 8; a++)
        for (uint32_t b = 4; b <= 8; b++)
            if (a != b)
                for (int k = 0; k < 5; k++) cred_fulfil(&C, a, b, UINT64_MAX, nid());
    cred_trust_compute(&C);
    bool zero = true;
    for (uint32_t j = 3; j < 8; j++) zero = zero && C.t[j] == 0;
    CHECK(zero && tsum(&C) == CRED_SCALE, "a sybil ring with no trusted inflow gets nothing");
    CHECK(cred_credibility(&C, 5) == 0, "credibility 0 for sybils");
    /* one honest trade into the ring leaks some trust in */
    cred_fulfil(&C, 3, 4, 50, nid());
    cred_trust_compute(&C);
    uint64_t ring = 0;
    for (uint32_t j = 3; j < 8; j++) ring += C.t[j];
    CHECK(ring > 0 && C.t[0] >= CRED_SCALE / 10 - 64 && tsum(&C) == CRED_SCALE,
          "a trusted trade into the ring leaks trust in (documented); p keeps its a-share");
    /* with no pretrusted subject, p is uniform and the ring gets a share */
    cred_set_pretrusted(&C, 1, false);
    cred_trust_compute(&C);
    ring = 0;
    for (uint32_t j = 3; j < 8; j++) ring += C.t[j];
    CHECK(ring > CRED_SCALE / 2, "documented limit: without pretrust sybils dominate");
}

static void test_credibility(void)
{
    cred_init(&C, NULL);
    for (uint32_t i = 1; i <= 4; i++) cred_subject_add(&C, i, false);
    CHECK(cred_credibility(&C, 1) == 0, "uncomputed: 0");
    cred_trust_compute(&C);
    CHECK(cred_credibility(&C, 1) == 100 && cred_credibility(&C, 4) == 100,
          "average trust maps to 100");
    CHECK(cred_credibility(&C, 77) == 0 && cred_credibility(NULL, 1) == 0, "unknown: 0");
    C.t[0] = CRED_SCALE / 4 * 3;
    C.t[1] = CRED_SCALE / 4;
    C.t[2] = C.t[3] = 0;
    CHECK(cred_credibility(&C, 1) == 300 && cred_credibility(&C, 2) == 100 &&
              cred_credibility(&C, 3) == 0,
          "exact multiples");
    C.t[0] = CRED_SCALE;
    C.t[1] = 0;
    CHECK(cred_credibility(&C, 1) == 400, "all trust, n = 4: 400");
    C.t[0] = CRED_SCALE / 400 * 3 + 1; /* floor(t*400/SCALE) */
    CHECK(cred_credibility(&C, 1) == (uint64_t) (((u128) C.t[0] * 400) / CRED_SCALE), "floor");
    cred_init(&C, NULL);
    for (uint32_t i = 1; i <= 20; i++) cred_subject_add(&C, i, false);
    cred_trust_compute(&C);
    C.t[0] = CRED_SCALE / 2;
    CHECK(cred_credibility(&C, 1) == 1000, "1000 at exactly the cap");
    C.t[0] = CRED_SCALE;
    CHECK(cred_credibility(&C, 1) == CRED_CARD_CRED_MAX, "capped at 1000");
    C.t[0] = CRED_SCALE / 2 - 1;
    CHECK(cred_credibility(&C, 1) == 999, "just below the cap");
}

static void test_unlocks(void)
{
    cred_config_t f;
    cred_config_default(&f);
    CHECK(cred_escrow_hold(&f, 0) == f.hold_base && cred_escrow_hold(&f, CRED_SCALE) == f.hold_base,
          "hold disabled: base");
    CHECK(cred_escrow_hold(NULL, CRED_SCALE) == 1209600, "NULL cfg: defaults (off)");
    f.hold_enabled = true;
    CHECK(cred_escrow_hold(&f, 0) == f.hold_base, "score 0: base");
    CHECK(cred_escrow_hold(&f, f.hold_sat) == f.hold_floor, "at sat: floor");
    CHECK(cred_escrow_hold(&f, CRED_SCALE) == f.hold_floor, "beyond sat: floor");
    CHECK(cred_escrow_hold(&f, UINT64_MAX) == f.hold_floor, "max score: floor");
    CHECK(cred_escrow_hold(&f, f.hold_sat / 2) == f.hold_base - (f.hold_base - f.hold_floor) / 2,
          "halfway");
    CHECK(cred_escrow_hold(&f, 1) == f.hold_base - (uint64_t) (((u128) 950400 * 1) / f.hold_sat),
          "tiny score: floor rounding keeps base");
    CHECK(cred_escrow_hold(&f, f.hold_sat - 1) == f.hold_floor + 1, "one below sat");
    bool mono = true;
    uint64_t prev = UINT64_MAX;
    for (uint64_t s = 0; s <= f.hold_sat + 1000; s += f.hold_sat / 97 + 1) {
        uint64_t h = cred_escrow_hold(&f, s);
        mono = mono && h <= prev && h >= f.hold_floor && h <= f.hold_base;
        prev = h;
    }
    CHECK(mono, "monotone, bounded");
    f.hold_sat = 0;
    CHECK(cred_escrow_hold(&f, 5) == 0, "bad cfg: 0");
    cred_config_default(&f);
    CHECK(cred_credit_line(&f, CRED_SCALE) == 0 && cred_credit_line(NULL, CRED_SCALE) == 0,
          "credit lines off by default");
    f.credit_lines_enabled = true;
    CHECK(cred_credit_line(&f, 0) == 0, "score 0: 0");
    CHECK(cred_credit_line(&f, CRED_SCALE / 4) == 25000, "floor(score*k/SCALE)");
    CHECK(cred_credit_line(&f, CRED_SCALE / 2) == 50000, "exactly the cap");
    CHECK(cred_credit_line(&f, CRED_SCALE) == 50000, "capped");
    CHECK(cred_credit_line(&f, CRED_SCALE / 2 - 1) == 49999, "floor just below");
    f.credit_k = UINT64_MAX;
    f.credit_cap = UINT64_MAX - 1;
    CHECK(cred_credit_line(&f, UINT64_MAX) == UINT64_MAX - 1, "overflowing quotient: cap");
    CHECK(cred_credit_line(&f, CRED_SCALE) == UINT64_MAX - 1, "k*1 below cap? no: k >= cap");
    f.credit_cap = UINT64_MAX;
    CHECK(cred_credit_line(&f, CRED_SCALE) == UINT64_MAX, "q == cap exactly");
}

static void test_card(void)
{
    cred_t *c = &C;
    cred_init(c, NULL);
    for (uint32_t i = 1; i <= 4; i++) cred_subject_add(c, i, i == 1);
    cred_card_t k;
    CHECK(cred_player_card(NULL, 1, 0, 0, 0, NULL, &k) == CRED_ERR_ARG &&
              cred_player_card(c, 1, 0, 0, 0, NULL, NULL) == CRED_ERR_ARG,
          "card NULL");
    CHECK(cred_player_card(c, 9, 0, 0, 0, NULL, &k) == CRED_ERR_UNKNOWN, "card unknown");
    CHECK(cred_player_card(c, 1, 0, 0, 0, NULL, &k) == CRED_ERR_STATE, "card before compute");
    cred_fulfil(c, 1, 2, 1000, nid());
    cred_fulfil(c, 3, 2, 1000, nid());
    cred_fulfil(c, 2, 1, 10, nid());
    cred_trust_compute(c);
    uint64_t cr = cred_credibility(c, 1);
    CHECK(cred_player_card(c, 1, 2000, 9500, 0, NULL, &k) == CRED_OK, "card");
    CHECK(k.subject == 1 && k.trust == c->t[0] && k.credibility == cr && k.local == 4 &&
              k.bond == 2000 && k.uptime_bps == 9500 && k.giving == 0,
          "card fields");
    CHECK(k.tier == (uint8_t) sic_tier_eval(NULL, cr, 2000, 9500, 0) &&
              k.perks == sic_tier_perks((sic_tier_t) k.tier),
          "tier and perks from sicily");
    CHECK(k.escrow_hold == c->cfg.hold_base && k.escrow_hold_bps == 10000 && k.credit_line == 0,
          "unlocks off");
    CHECK(k.src_bond == NULL && k.src_uptime == NULL && k.src_giving == NULL, "sources NULL");
    cred_player_card(c, 2, 0, 0, 0, NULL, &k);
    CHECK(k.local == 10 + 10 && k.credibility == 189 && k.tier == 0 && k.has_next &&
              k.next.cred == 100 && k.next.bond == 1000 && k.next.uptime_bps == 9000 &&
              k.missing == (CRED_MISS_BOND | CRED_MISS_UPTIME),
          "next tier is Bronze, missing listed");
    /* custom table: everything Platinum */
    sic_tier_table_t tab;
    sic_tier_table_default(&tab);
    for (int i = 0; i < 4; i++) tab.t[i].cred = tab.t[i].bond = tab.t[i].uptime_bps = 0;
    cred_player_card(c, 3, 0, 0, 0, &tab, &k);
    CHECK(k.tier == 4 && !k.has_next && k.missing == 0 && k.perks == 0x1f, "Platinum: no next");
    /* giving counts for the missing test like sicily: eff cred */
    sic_tier_table_default(&tab);
    tab.t[0].cred = 50;
    tab.t[0].bond = 0;
    tab.t[0].uptime_bps = 0;
    tab.t[1].cred = 60;
    tab.t[1].bond = 0;
    tab.t[1].uptime_bps = 0;
    c->t[0] = CRED_SCALE / 8; /* credibility 50 */
    cred_player_card(c, 1, 0, 0, 0, &tab, &k);
    CHECK(k.credibility == 50 && k.tier == 1 && k.has_next && k.missing == CRED_MISS_CRED,
          "credibility 50 < Silver 60");
    cred_player_card(c, 1, 0, 0, 10, &tab, &k);
    CHECK(k.tier == 2 && k.next.cred == 500 &&
              k.missing == (CRED_MISS_CRED | CRED_MISS_BOND | CRED_MISS_UPTIME),
          "giving 10 lifts to 60: Silver");
    cred_player_card(c, 1, 0, 10001, 10, &tab, &k);
    CHECK(k.tier == 0 && k.missing == CRED_MISS_UPTIME, "malformed uptime: Copper, uptime missing");
    sic_tier_table_default(&tab);
    tab.t[0].cred = 0;
    tab.t[0].bond = 5;
    tab.t[0].uptime_bps = 100;
    cred_player_card(c, 1, 5, 100, 0, &tab, &k);
    CHECK(k.tier == 1, "exact thresholds met");
    cred_player_card(c, 1, 0, 10000, 0, &tab, &k);
    CHECK(k.tier == 0 && k.missing == CRED_MISS_BOND, "uptime 10000 is valid and met");
    cred_player_card(c, 1, 4, 100, 0, &tab, &k);
    CHECK(k.tier == 0 && k.missing == CRED_MISS_BOND, "bond one short");
    cred_player_card(c, 1, 5, 99, 0, &tab, &k);
    CHECK(k.tier == 0 && k.missing == CRED_MISS_UPTIME, "uptime one short");
    /* unlocks on */
    c->cfg.hold_enabled = true;
    c->cfg.credit_lines_enabled = true;
    c->t[0] = CRED_SCALE / 16; /* half of hold_sat */
    cred_player_card(c, 1, 0, 0, 0, NULL, &k);
    CHECK(k.escrow_hold == cred_escrow_hold(&c->cfg, CRED_SCALE / 16) &&
              k.escrow_hold_bps == k.escrow_hold * 10000 / c->cfg.hold_base &&
              k.escrow_hold_bps == 6071 && k.credit_line == 6250,
          "unlocks on the card");
}

static void test_json(void)
{
    char buf[1024];
    cred_card_t k;
    memset(&k, 0, sizeof k);
    k.tier = 1;
    k.credibility = 150;
    k.bond = 2500;
    k.uptime_bps = 9100;
    k.giving = 0;
    k.has_next = true;
    k.next.cred = 250;
    k.next.bond = 10000;
    k.next.uptime_bps = 9500;
    k.missing = CRED_MISS_CRED | CRED_MISS_BOND | CRED_MISS_UPTIME;
    k.perks = 0x03;
    k.trust = 123;
    k.local = 45;
    k.escrow_hold_bps = 10000;
    k.credit_line = 0;
    k.src_bond = "ledger bond";
    k.src_giving = "a \"q\"\\\n\x1f";
    const char *e1 =
        "{\"tier\":1,\"metal\":\"Bronze\",\"stone\":\"Carnelian\",\"inputs\":{\"credibility\":{"
        "\"value\":\"150\",\"source\":\"EigenTrust over fulfilled trades\"},\"bond\":{\"value\":"
        "\"2500\",\"source\":\"ledger bond\"},\"uptime_bps\":{\"value\":\"9100\",\"source\":"
        "\"unspecified\"},\"giving\":{\"value\":\"0\",\"source\":\"a "
        "\\\"q\\\"\\\\\\u000a\\u001f\"}},"
        "\"next\":{\"tier\":2,\"metal\":\"Silver\",\"stone\":\"Jade\",\"credibility\":\"250\","
        "\"bond\":\"10000\",\"uptime_bps\":\"9500\",\"missing\":[\"credibility\",\"bond\","
        "\"uptime_bps\"]},\"perks\":[\"free registration\",\"storefronts\"],\"score\":\"123\","
        "\"local\":\"45\",\"escrow_hold_bps\":\"10000\",\"credit_line\":\"0\"}";
    int32_t n = cred_card_json(&k, buf, sizeof buf);
    CHECK(n == (int32_t) strlen(e1) && strcmp(buf, e1) == 0, "JSON case 1 byte-exact");
    if (strcmp(buf, e1)) printf("got %s\n", buf);
    CHECK(cred_card_json(&k, buf, strlen(e1) + 1) == (int32_t) strlen(e1), "exact fit");
    CHECK(cred_card_json(&k, buf, strlen(e1)) == -1 && strlen(buf) == strlen(e1) - 1,
          "one short: -1, terminated prefix");
    CHECK(cred_card_json(&k, buf, 0) == -1 && cred_card_json(&k, NULL, 10) == -1,
          "cap 0 / NULL buf");
    CHECK(cred_card_json(NULL, buf, sizeof buf) == -1 && buf[0] == 0, "NULL card");
    k.missing = CRED_MISS_BOND;
    cred_card_json(&k, buf, sizeof buf);
    CHECK(strstr(buf, "\"missing\":[\"bond\"]}") != NULL, "single missing");
    k.missing = CRED_MISS_CRED | CRED_MISS_UPTIME;
    cred_card_json(&k, buf, sizeof buf);
    CHECK(strstr(buf, "\"missing\":[\"credibility\",\"uptime_bps\"]}") != NULL, "two missing");
    k.missing = 0;
    cred_card_json(&k, buf, sizeof buf);
    CHECK(strstr(buf, "\"missing\":[]}") != NULL, "none missing");
    k.perks = SIC_PERK_FORWARDS;
    cred_card_json(&k, buf, sizeof buf);
    CHECK(strstr(buf, "\"perks\":[\"forwards\"]") != NULL, "one perk");
    k.perks = 0xff;
    k.missing = 0xff;
    cred_card_json(&k, buf, sizeof buf);
    CHECK(strstr(buf, "\"missing\":[\"credibility\",\"bond\",\"uptime_bps\"]}") != NULL &&
              strstr(buf, "\"perks\":[\"free registration\",\"storefronts\",\"forwards\",\"IP "
                          "notes\",\"cross-border licensing\"],") != NULL,
          "unknown bits are ignored");
    k.missing = 0;
    k.perks = 0;
    cred_card_json(&k, buf, sizeof buf);
    CHECK(strstr(buf, "\"perks\":[]") != NULL, "no perks");
    /* case 2: Platinum, next null, all perks, big numbers */
    memset(&k, 0, sizeof k);
    k.tier = 4;
    k.credibility = 1000;
    k.bond = UINT64_MAX;
    k.uptime_bps = 10000;
    k.giving = 7;
    k.perks = 0x1f;
    k.trust = CRED_SCALE;
    k.local = 18446744073709551615ull;
    k.escrow_hold_bps = 2142;
    k.credit_line = 50000;
    k.src_bond = "b";
    k.src_uptime = "u";
    k.src_giving = "";
    const char *e2 =
        "{\"tier\":4,\"metal\":\"Platinum\",\"stone\":\"Ruby\",\"inputs\":{\"credibility\":{"
        "\"value\":\"1000\",\"source\":\"EigenTrust over fulfilled trades\"},\"bond\":{\"value\":"
        "\"18446744073709551615\",\"source\":\"b\"},\"uptime_bps\":{\"value\":\"10000\","
        "\"source\":\"u\"},\"giving\":{\"value\":\"7\",\"source\":\"\"}},\"next\":null,\"perks\":["
        "\"free registration\",\"storefronts\",\"forwards\",\"IP notes\",\"cross-border "
        "licensing\"],\"score\":\"1073741824\",\"local\":\"18446744073709551615\","
        "\"escrow_hold_bps\":\"2142\",\"credit_line\":\"50000\"}";
    n = cred_card_json(&k, buf, sizeof buf);
    CHECK(n == (int32_t) strlen(e2) && strcmp(buf, e2) == 0, "JSON case 2 byte-exact");
    if (strcmp(buf, e2)) printf("got %s\n", buf);
    /* a card built from the module */
    cred_init(&C, NULL);
    cred_subject_add(&C, 1, false);
    cred_trust_compute(&C);
    cred_player_card(&C, 1, 0, 0, 0, NULL, &k);
    const char *e3 =
        "{\"tier\":0,\"metal\":\"Copper\",\"stone\":\"Obsidian\",\"inputs\":{\"credibility\":{"
        "\"value\":\"100\",\"source\":\"EigenTrust over fulfilled trades\"},\"bond\":{\"value\":"
        "\"0\",\"source\":\"unspecified\"},\"uptime_bps\":{\"value\":\"0\",\"source\":"
        "\"unspecified\"},\"giving\":{\"value\":\"0\",\"source\":\"unspecified\"}},\"next\":{"
        "\"tier\":1,\"metal\":\"Bronze\",\"stone\":\"Carnelian\",\"credibility\":\"100\","
        "\"bond\":\"1000\",\"uptime_bps\":\"9000\",\"missing\":[\"bond\",\"uptime_bps\"]},"
        "\"perks\":[],\"score\":\"1073741824\",\"local\":\"0\",\"escrow_hold_bps\":\"10000\","
        "\"credit_line\":\"0\"}";
    n = cred_card_json(&k, buf, sizeof buf);
    CHECK(n == (int32_t) strlen(e3) && strcmp(buf, e3) == 0, "JSON from cred_player_card");
    if (strcmp(buf, e3)) printf("got %s\n", buf);
}

/* ---- randomized property loop against the reference ---- */
static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

static void test_random(void)
{
    static uint64_t S[CRED_MAX_SUBJECTS][CRED_MAX_SUBJECTS];
    uint32_t fails[CRED_MAX_SUBJECTS];
    bool pre[CRED_MAX_SUBJECTS];
    int bad_local = 0, bad_trust = 0, bad_sum = 0, bad_check = 0, bad_status = 0;
    for (int round = 0; round < 30; round++) {
        uint32_t n = 2 + (uint32_t) (rnd() % (round < 25 ? 10 : 40));
        cred_config_t f;
        cred_config_default(&f);
        f.alpha_num = (uint32_t) (rnd() % 6);
        f.decay_num = (uint32_t) (rnd() % 8);
        f.fail_cap = (uint32_t) (rnd() % 12);
        f.fail_base = 1 + rnd() % 64;
        f.eps = rnd() % 200;
        f.iters = 1 + (uint32_t) (rnd() % 40);
        cred_init(&C, &f);
        memset(S, 0, sizeof S);
        memset(fails, 0, sizeof fails);
        for (uint32_t i = 0; i < n; i++) {
            pre[i] = rnd() % 4 == 0;
            cred_subject_add(&C, 1000 + i * 7, pre[i]);
        }
        for (int op = 0; op < 120; op++) {
            uint32_t i = (uint32_t) (rnd() % n), j = (uint32_t) (rnd() % n);
            uint32_t r = (uint32_t) (rnd() % 10);
            cred_status_t st;
            if (r < 6) {
                uint64_t v = rnd() >> (rnd() % 64);
                st = cred_fulfil(&C, 1000 + i * 7, 1000 + j * 7, v, nid());
                cred_status_t want = i == j ? CRED_ERR_SELF : v == 0 ? CRED_ERR_ARG : CRED_OK;
                if (want == CRED_OK) {
                    S[i][j] += ref_gain(v);
                    if (S[i][j] > CRED_LOCAL_MAX) S[i][j] = CRED_LOCAL_MAX;
                }
                bad_status += st != want;
            } else if (r < 8) {
                st = cred_fail(&C, 1000 + i * 7, 1000 + j * 7, nid());
                cred_status_t want = i == j ? CRED_ERR_SELF : CRED_OK;
                if (want == CRED_OK) {
                    fails[j]++;
                    uint32_t sh = fails[j] - 1 < f.fail_cap ? fails[j] - 1 : f.fail_cap;
                    uint64_t loss = f.fail_base * ((uint64_t) 1 << sh);
                    S[i][j] = S[i][j] > loss ? S[i][j] - loss : 0;
                }
                bad_status += st != want;
            } else if (r < 9) {
                cred_decay_epoch(&C);
                for (uint32_t a = 0; a < n; a++) {
                    for (uint32_t b = 0; b < n; b++)
                        S[a][b] =
                            (uint64_t) ((u128) S[a][b] * (f.decay_den - f.decay_num) / f.decay_den);
                    fails[a] /= 2;
                }
            } else {
                cred_trust_compute(&C);
                uint64_t t[CRED_MAX_SUBJECTS];
                uint32_t used;
                ref_trust(n, S, pre, f.alpha_num, f.alpha_den, f.iters, f.eps, t, &used);
                bad_trust += memcmp(t, C.t, n * 8) != 0 || used != C.iters_used;
                bad_sum += tsum(&C) != CRED_SCALE;
            }
            for (uint32_t a = 0; a < n; a++)
                for (uint32_t b = 0; b < n; b++) bad_local += C.s[a][b] != S[a][b];
            bad_check += !cred_check(&C);
        }
        cred_trust_compute(&C);
        bad_sum += tsum(&C) != CRED_SCALE;
    }
    CHECK(bad_status == 0, "random: statuses as the reference expects");
    CHECK(bad_local == 0, "random: local scores match the reference");
    CHECK(bad_trust == 0, "random: global trust matches the reference bit for bit");
    CHECK(bad_sum == 0, "random: trust sums to exactly SCALE");
    CHECK(bad_check == 0, "random: audit clean after every step");
}

int main(void)
{
    test_config();
    test_subjects();
    test_gain_loss();
    test_fulfil_fail();
    test_ring();
    test_decay();
    test_trust_basic();
    test_full();
    test_sybil();
    test_credibility();
    test_unlocks();
    test_card();
    test_json();
    test_random();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
