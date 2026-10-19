/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_emu.c — unit and scenario tests for emu-state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "zxv_emu.h"

static int g_checks, g_fail;

static const char *last_err(void) {
    static char b[300];
    size_t n = 0;
    if (zxv_emu_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_fail++;                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);               \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, " [last error: %s]\n", last_err());                \
        }                                                                      \
    } while (0)
#define OK(expr) CHECK((expr) == ZXV_OK, "%s", #expr)
#define IS(expr, st) CHECK((expr) == (st), "%s != %s", #expr, #st)

static zxv_emu *new_game(uint32_t players, uint32_t history, uint32_t flags) {
    zxv_emu *s = NULL;
    if (zxv_emu_create(players, game_rat_slots(players), players, GAME_INPUT, history, flags, &s) !=
        ZXV_OK)
        return NULL;
    if (game_setup(s, players) != ZXV_OK || zxv_emu_start(s) != ZXV_OK) {
        zxv_emu_destroy(s);
        return NULL;
    }
    return s;
}

/* The deterministic input of player p for tick t in the scenarios. */
static void input_for(uint32_t p, uint64_t t, uint8_t out[GAME_INPUT]) {
    uint64_t x = (t * 2654435761u) ^ ((uint64_t) p * 40503u);
    out[0] = (uint8_t) ((x % 3u) == 0 ? 0 : ((x % 3u) == 1 ? 1 : 0xFF));
    out[1] = (uint8_t) ((x >> 3) % 5u == 0 ? 1u : 0u);
    if (p == 1 && t == 9) out[1] = 2; /* one cheat, forbidden for good */
}

static int submit_all(zxv_emu *s, uint32_t players, uint64_t t) {
    for (uint32_t p = 0; p < players; p++) {
        uint8_t in[GAME_INPUT];
        input_for(p, t, in);
        if (zxv_emu_input_submit(s, p, t, in, sizeof in) != ZXV_OK) return 0;
    }
    return 1;
}

static void test_create(void) {
    zxv_emu *s = NULL;
    IS(zxv_emu_create(0, 1, 0, 1, 8, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(65, 1, 0, 1, 8, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 0, 0, 1, 8, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 1, 0, 0, 8, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 1, 0, 257, 8, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 1, 0, 1, 1, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 1, 0, 1, 4097, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 1, 0, 1, 8, 0x80, &s), ZXV_E_ARG);
    IS(zxv_emu_create(1, 1, 0, 1, 8, 0, NULL), ZXV_E_ARG);
    IS(zxv_emu_create(64, 65536, 65536, 256, 4096, 0, &s), ZXV_E_CAPACITY);
    CHECK(s == NULL, "no handle on failure");
    OK(zxv_emu_create(3, 4, 2, 2, 16, ZXV_EMU_F_PREDICT, &s));
    uint32_t a, b, c, d, e, f;
    OK(zxv_emu_info(s, &a, &b, &c, &d, &e, &f));
    CHECK(a == 3 && b == 4 && c == 2 && d == 2 && e == 16 && f == ZXV_EMU_F_PREDICT, "info");
    int64_t n, dd;
    OK(zxv_emu_rat_get(s, 3, &n, &dd));
    CHECK(n == 0 && dd == 1, "slots start at 0/1");
    IS(zxv_emu_rat_get(s, 4, &n, &dd), ZXV_E_ARG);
    zxv_emu_destroy(s);
    zxv_emu_destroy(NULL);

    char buf[4];
    size_t len = 0;
    IS(zxv_emu_create(0, 1, 0, 1, 8, 0, &s), ZXV_E_ARG);
    IS(zxv_emu_last_error(NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len > 4, "last error has text");
    IS(zxv_emu_last_error(buf, sizeof buf, &len), ZXV_E_BUFFER);
    IS(zxv_emu_last_error(buf, 0, NULL), ZXV_E_ARG);
}

static void test_encoding_and_hash(void) {
    zxv_emu *s = NULL;
    OK(zxv_emu_create(1, 2, 1, 1, 4, 0, &s));
    OK(zxv_emu_rat_set(s, 0, 6, -4)); /* stored reduced: -3/2 */
    OK(zxv_emu_rat_set(s, 1, 7, 1));
    OK(zxv_emu_hex_set(s, 0, 63));
    IS(zxv_emu_hex_set(s, 0, 64), ZXV_E_ARG);
    IS(zxv_emu_rat_set(s, 0, 1, 0), ZXV_E_ARG);
    int64_t n, d;
    OK(zxv_emu_rat_get(s, 0, &n, &d));
    CHECK(n == -3 && d == 2, "reduced with positive denominator, got %lld/%lld", (long long) n,
          (long long) d);
    uint8_t h[32];
    IS(zxv_emu_hash(s, 0, h, NULL), ZXV_E_NOT_FOUND); /* not started */
    OK(zxv_emu_start(s));
    IS(zxv_emu_start(s), ZXV_E_STATE);
    IS(zxv_emu_rat_set(s, 0, 1, 1), ZXV_E_STATE); /* setters only in ticks now */

    /* Recompute the canonical encoding by hand and hash it with libzxv. */
    uint8_t enc[16 + 16 + 32 + 1], want[32];
    size_t k = 0;
    memcpy(enc, "zxv/emu-state/v1", 16);
    k = 16;
    memset(enc + k, 0, 8); /* tick 0 */
    k += 8;
    enc[k++] = 2; enc[k++] = 0; enc[k++] = 0; enc[k++] = 0; /* 2 rat slots */
    enc[k++] = 1; enc[k++] = 0; enc[k++] = 0; enc[k++] = 0; /* 1 hex slot  */
    const uint8_t m3[8] = {0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; /* -3 */
    memcpy(enc + k, m3, 8); k += 8;
    memset(enc + k, 0, 8); enc[k] = 2; k += 8;
    memset(enc + k, 0, 8); enc[k] = 7; k += 8;
    memset(enc + k, 0, 8); enc[k] = 1; k += 8;
    enc[k++] = 63;
    CHECK(k == sizeof enc, "encoding size");
    OK(zxv_sha3_256(enc, sizeof enc, want));
    int32_t fin = -1;
    OK(zxv_emu_hash(s, 0, h, &fin));
    CHECK(memcmp(h, want, 32) == 0, "hash = SHA3-256(canonical encoding)");
    CHECK(fin == 1, "tick 0 is final");

    uint8_t out[sizeof enc];
    size_t len = 0;
    IS(zxv_emu_state_export(s, NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len == sizeof enc, "export size %zu", len);
    OK(zxv_emu_state_export(s, out, sizeof out, &len));
    CHECK(memcmp(out, enc, sizeof enc) == 0, "export = canonical encoding");
    zxv_emu_destroy(s);
}

static void test_strict_lockstep(void) {
    uint32_t P = 3;
    zxv_emu *a = new_game(P, 8, 0), *b = new_game(P, 8, 0);
    CHECK(a && b, "create");
    if (!a || !b) return;
    int32_t ready = -1;
    OK(zxv_emu_ready(a, &ready));
    CHECK(ready == 0, "not ready without inputs");
    IS(zxv_emu_begin(a), ZXV_E_STATE);
    IS(zxv_emu_advance(a, game_step, &P), ZXV_E_STATE);

    uint8_t in[GAME_INPUT];
    input_for(0, 8, in);
    IS(zxv_emu_input_submit(a, P, 1, in, sizeof in), ZXV_E_ARG);
    IS(zxv_emu_input_submit(a, 0, 0, in, sizeof in), ZXV_E_ARG);
    IS(zxv_emu_input_submit(a, 0, 1, in, 1), ZXV_E_ARG);
    IS(zxv_emu_input_submit(a, 0, 1, NULL, sizeof in), ZXV_E_ARG);
    IS(zxv_emu_input_submit(a, 0, 9, in, sizeof in), ZXV_E_RANGE); /* window = 8 */
    OK(zxv_emu_input_submit(a, 0, 8, in, sizeof in));
    OK(zxv_emu_input_submit(a, 0, 8, in, sizeof in)); /* identical: idempotent */
    in[0] ^= 1;
    IS(zxv_emu_input_submit(a, 0, 8, in, sizeof in), ZXV_E_DUPLICATE);

    for (uint64_t t = 1; t <= 20; t++) {
        /* a gets them in player order, b in reverse order */
        CHECK(submit_all(a, P, t), "submit a %llu", (unsigned long long) t);
        for (uint32_t p = P; p-- > 0;) {
            uint8_t x[GAME_INPUT];
            input_for(p, t, x);
            OK(zxv_emu_input_submit(b, p, t, x, sizeof x));
        }
        OK(zxv_emu_ready(a, &ready));
        CHECK(ready == 1, "ready");
        OK(zxv_emu_advance(a, game_step, &P));
        OK(zxv_emu_advance(b, game_step, &P));
        uint8_t ha[32], hb[32];
        int32_t fa = 0, lp = -1;
        OK(zxv_emu_hash(a, t, ha, &fa));
        OK(zxv_emu_hash(b, t, hb, NULL));
        CHECK(memcmp(ha, hb, 32) == 0 && fa == 1, "tick %llu hashes match and final",
              (unsigned long long) t);
        OK(zxv_emu_compare(a, t, hb, &lp));
        CHECK(lp == ZXV_LP_TRUE, "compare TRUE");
    }
    uint64_t tick = 0, c = 0;
    OK(zxv_emu_tick(a, &tick));
    OK(zxv_emu_confirmed_tick(a, &c));
    CHECK(tick == 20 && c == 20, "tick %llu confirmed %llu", (unsigned long long) tick,
          (unsigned long long) c);
    uint8_t h[32];
    IS(zxv_emu_hash(a, 12, h, NULL), ZXV_E_NOT_FOUND); /* history 8 keeps 13..20 */
    OK(zxv_emu_hash(a, 13, h, NULL));

    /* The cheat at tick 9 forbade player 1's claim; forbidden is absorbing. */
    uint8_t tag, pol;
    OK(zxv_emu_hex_get(a, 1, &tag));
    OK(zxv_hex_unpack(tag, NULL, &pol, NULL));
    CHECK(pol == ZXV_HEX_POL_FORBIDDEN, "player 1 stays forbidden");
    int32_t act = -1;
    OK(zxv_hex_action(tag, &act));
    CHECK(act == ZXV_HEX_ACT_ROLLBACK, "forbidden -> rollback action");

    /* A desync: c differs from tick 1 on (one changed input). */
    zxv_emu *c3 = new_game(P, 8, 0);
    for (uint64_t t = 1; t <= 3; t++) {
        for (uint32_t p = 0; p < P; p++) {
            uint8_t x[GAME_INPUT];
            input_for(p, t, x);
            if (t == 2 && p == 2) x[0] ^= 1;
            OK(zxv_emu_input_submit(c3, p, t, x, sizeof x));
        }
        OK(zxv_emu_advance(c3, game_step, &P));
    }
    zxv_emu *ref = new_game(P, 8, 0);
    for (uint64_t t = 1; t <= 3; t++) {
        submit_all(ref, P, t);
        OK(zxv_emu_advance(ref, game_step, &P));
    }
    int32_t lp = -1, has = -1;
    uint64_t dt = 0;
    OK(zxv_emu_desync(c3, &dt, &has));
    CHECK(has == 0, "no desync yet");
    for (uint64_t t = 3; t >= 1; t--) {
        uint8_t hr[32];
        OK(zxv_emu_hash(ref, t, hr, NULL));
        OK(zxv_emu_compare(c3, t, hr, &lp));
        CHECK(lp == (t == 1 ? ZXV_LP_TRUE : ZXV_LP_FALSE), "tick %llu lp %d",
              (unsigned long long) t, lp);
    }
    OK(zxv_emu_desync(c3, &dt, &has));
    CHECK(has == 1 && dt == 2, "first desync at tick 2, got %d %llu", has, (unsigned long long) dt);
    uint8_t hr[32] = {0};
    OK(zxv_emu_compare(c3, 99, hr, &lp));
    CHECK(lp == ZXV_LP_NEITHER, "unknown tick -> NEITHER");

    zxv_emu_destroy(a);
    zxv_emu_destroy(b);
    zxv_emu_destroy(c3);
    zxv_emu_destroy(ref);
}

static int32_t fail_step(zxv_emu *sim, uint64_t tick, void *user) {
    (void) tick;
    (void) user;
    zxv_emu_rat_set(sim, 0, 999, 1);
    return ZXV_E_INVARIANT;
}

static void test_tick_phases(void) {
    zxv_emu *s = NULL;
    OK(zxv_emu_create(1, 1, 1, 1, 4, 0, &s));
    IS(zxv_emu_begin(s), ZXV_E_STATE); /* not started */
    OK(zxv_emu_start(s));
    uint8_t in = 0;
    OK(zxv_emu_input_submit(s, 0, 1, &in, 1));
    IS(zxv_emu_commit(s), ZXV_E_STATE);
    IS(zxv_emu_abort(s), ZXV_E_STATE);
    IS(zxv_emu_charge(s, 1, 1), ZXV_E_STATE);
    size_t n;
    IS(zxv_emu_input_get(s, 0, &in, 1, &n, NULL), ZXV_E_STATE);
    IS(zxv_emu_advance(s, fail_step, NULL), ZXV_E_INVARIANT);
    uint64_t t = 9;
    OK(zxv_emu_tick(s, &t));
    int64_t num, den;
    OK(zxv_emu_rat_get(s, 0, &num, &den));
    CHECK(t == 0 && num == 0, "failed step leaves no trace");

    OK(zxv_emu_begin(s));
    IS(zxv_emu_begin(s), ZXV_E_STATE);
    IS(zxv_emu_input_submit(s, 0, 2, &in, 1), ZXV_E_STATE);
    IS(zxv_emu_rollback(s, 0), ZXV_E_STATE);
    IS(zxv_emu_state_export(s, NULL, 0, &n), ZXV_E_STATE);
    OK(zxv_emu_charge(s, 5, 1)); /* no budget: no-op */
    int32_t pred = -1;
    uint8_t got = 7;
    IS(zxv_emu_input_get(s, 0, NULL, 0, &n, NULL), ZXV_E_BUFFER);
    CHECK(n == 1, "input size");
    OK(zxv_emu_input_get(s, 0, &got, 1, &n, &pred));
    CHECK(got == 0 && pred == 0, "confirmed input");
    /* exact arithmetic: 1/3 + 1/3 + 1/3 == 1, never 0.999... */
    for (int i = 0; i < 3; i++) OK(zxv_emu_rat_add(s, 0, 1, 3));
    OK(zxv_emu_rat_get(s, 0, &num, &den));
    CHECK(num == 1 && den == 1, "three thirds are one");
    OK(zxv_emu_rat_set(s, 0, INT64_MAX, 1));
    IS(zxv_emu_rat_add(s, 0, 1, 1), ZXV_E_RANGE);
    IS(zxv_emu_rat_mul(s, 0, 2, 1), ZXV_E_RANGE);
    OK(zxv_emu_rat_get(s, 0, &num, &den));
    CHECK(num == INT64_MAX && den == 1, "overflow leaves the slot unchanged");
    OK(zxv_emu_rat_mul(s, 0, 0, 1));
    OK(zxv_emu_abort(s));
    OK(zxv_emu_rat_get(s, 0, &num, &den));
    CHECK(num == 0 && den == 1, "abort restores");
    OK(zxv_emu_tick(s, &t));
    CHECK(t == 0, "abort does not advance");
    zxv_emu_destroy(s);
}

static int32_t charge_step(zxv_emu *sim, uint64_t tick, void *user) {
    (void) user;
    int32_t rc = zxv_emu_charge(sim, 6, 1);
    if (rc != ZXV_OK) return rc;
    if (tick == 2) return zxv_emu_charge(sim, 6, 1); /* 12 > 10 */
    return zxv_emu_charge(sim, 4, 1);                 /* exactly 10 */
}

static void test_budget(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *nobudget = NULL;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    OK(zxv_cap_set_attestation(root, ZXV_LP_TRUE));
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &nobudget));
    zxv_emu *s = NULL;
    OK(zxv_emu_create(1, 1, 0, 1, 4, 0, &s));
    IS(zxv_emu_set_tick_budget(s, NULL, 10, 1), ZXV_E_ARG);
    IS(zxv_emu_set_tick_budget(s, nobudget, 10, 1), ZXV_E_CAP);
    OK(zxv_emu_set_tick_budget(s, root, 10, 1));
    OK(zxv_emu_start(s));
    uint8_t in = 0;
    for (uint64_t t = 1; t <= 4; t++) OK(zxv_emu_input_submit(s, 0, t, &in, 1));
    OK(zxv_emu_advance(s, charge_step, NULL));
    IS(zxv_emu_advance(s, charge_step, NULL), ZXV_E_BUDGET);
    uint64_t t = 0;
    OK(zxv_emu_tick(s, &t));
    CHECK(t == 1, "tick over budget was refused");
    OK(zxv_emu_set_tick_budget(s, root, 12, 1)); /* raise the limit */
    OK(zxv_emu_advance(s, charge_step, NULL));
    OK(zxv_emu_advance(s, charge_step, NULL)); /* budget reset each tick */
    zxv_emu_destroy(s);
    zxv_cap_destroy(nobudget);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

/* Predict mode: peer A runs ahead with predicted inputs for player 1 and
 * receives them late (out of order); it must end bit-identical to a strict
 * reference that had every input on time. */
static void test_predict_rollback(void) {
    uint32_t P = 2;
    zxv_emu *a = new_game(P, 16, ZXV_EMU_F_PREDICT), *ref = new_game(P, 16, 0);
    CHECK(a && ref, "create");
    if (!a || !ref) return;
    for (uint64_t t = 1; t <= 12; t++) {
        uint8_t x[GAME_INPUT];
        input_for(0, t, x);
        OK(zxv_emu_input_submit(a, 0, t, x, sizeof x)); /* local player on time */
        int32_t ready = 0;
        OK(zxv_emu_ready(a, &ready));
        CHECK(ready == 1, "predict mode is ready without remote inputs");
        OK(zxv_emu_advance(a, game_step, &P));
        submit_all(ref, P, t);
        OK(zxv_emu_advance(ref, game_step, &P));
    }
    uint64_t c = 99;
    OK(zxv_emu_confirmed_tick(a, &c));
    CHECK(c == 0, "nothing confirmed while player 1 is predicted, got %llu", (unsigned long long) c);
    int32_t fin = -1, lp = -1;
    uint8_t ha[32], hr[32];
    OK(zxv_emu_hash(a, 5, ha, &fin));
    CHECK(fin == 0, "predicted ticks are not final");
    OK(zxv_emu_hash(ref, 5, hr, NULL));
    OK(zxv_emu_compare(a, 5, hr, &lp));
    CHECK(lp == ZXV_LP_NEITHER, "no verdict on a predicted tick");

    /* player 1's real inputs arrive late and out of order: 7..12 then 1..6 */
    for (uint64_t t = 7; t <= 12; t++) {
        uint8_t x[GAME_INPUT];
        input_for(1, t, x);
        OK(zxv_emu_input_submit(a, 1, t, x, sizeof x));
    }
    for (uint64_t t = 1; t <= 6; t++) {
        uint8_t x[GAME_INPUT];
        input_for(1, t, x);
        OK(zxv_emu_input_submit(a, 1, t, x, sizeof x));
    }
    int32_t pending = 0;
    uint64_t pt = 0;
    OK(zxv_emu_rollback_pending(a, &pt, &pending));
    CHECK(pending == 1 && pt >= 1 && pt <= 12, "rollback pending at %llu", (unsigned long long) pt);
    IS(zxv_emu_begin(a), ZXV_E_STATE); /* must resolve the rollback first */
    int32_t ready = 1;
    OK(zxv_emu_ready(a, &ready));
    CHECK(ready == 0, "not ready while a rollback is pending");
    OK(zxv_emu_rollback_replay(a, game_step, &P));
    OK(zxv_emu_rollback_pending(a, &pt, &pending));
    CHECK(pending == 0, "rollback resolved");
    uint64_t t = 0;
    OK(zxv_emu_tick(a, &t));
    CHECK(t == 12, "replayed back to tick 12");
    OK(zxv_emu_confirmed_tick(a, &c));
    CHECK(c == 12, "everything confirmed now, got %llu", (unsigned long long) c);
    for (uint64_t k = 1; k <= 12; k++) {
        OK(zxv_emu_hash(a, k, ha, &fin));
        OK(zxv_emu_hash(ref, k, hr, NULL));
        CHECK(memcmp(ha, hr, 32) == 0 && fin == 1, "tick %llu identical to the reference",
              (unsigned long long) k);
        OK(zxv_emu_compare(a, k, hr, &lp));
        CHECK(lp == ZXV_LP_TRUE, "compare");
    }
    OK(zxv_emu_rollback_replay(a, game_step, &P)); /* no-op */

    /* A confirmed input resubmitted differently is refused. */
    uint8_t x[GAME_INPUT];
    input_for(1, 3, x);
    x[1] ^= 1;
    IS(zxv_emu_input_submit(a, 1, 3, x, sizeof x), ZXV_E_DUPLICATE);

    /* Too late: with history 16, run on to tick 30 and send tick 5's input. */
    zxv_emu *b = new_game(P, 4, ZXV_EMU_F_PREDICT);
    for (uint64_t k = 1; k <= 10; k++) OK(zxv_emu_advance(b, game_step, &P));
    input_for(1, 5, x);
    IS(zxv_emu_input_submit(b, 1, 5, x, sizeof x), ZXV_E_RANGE);
    input_for(1, 8, x); /* rollback point 7 is kept (7..10) */
    OK(zxv_emu_input_submit(b, 1, 8, x, sizeof x));

    /* Manual rollback and the kept window. */
    IS(zxv_emu_rollback(b, 6), ZXV_E_NOT_FOUND);
    IS(zxv_emu_rollback(b, 11), ZXV_E_NOT_FOUND);
    OK(zxv_emu_rollback(b, 7));
    OK(zxv_emu_rollback_pending(b, &pt, &pending));
    CHECK(pending == 0, "rolling back below the pending tick clears it");
    OK(zxv_emu_tick(b, &t));
    CHECK(t == 7, "rolled back to 7");
    IS(zxv_emu_hash(b, 8, ha, NULL), ZXV_E_NOT_FOUND);
    zxv_emu_destroy(b);
    zxv_emu_destroy(a);
    zxv_emu_destroy(ref);
}

static void test_export_import(void) {
    uint32_t P = 2;
    zxv_emu *a = new_game(P, 8, 0), *b = NULL;
    for (uint64_t t = 1; t <= 6; t++) {
        submit_all(a, P, t);
        OK(zxv_emu_advance(a, game_step, &P));
    }
    size_t len = 0;
    IS(zxv_emu_state_export(a, NULL, 0, &len), ZXV_E_BUFFER);
    uint8_t *buf = malloc(len);
    OK(zxv_emu_state_export(a, buf, len, &len));

    OK(zxv_emu_create(P, game_rat_slots(P), P, GAME_INPUT, 8, 0, &b));
    IS(zxv_emu_state_import(b, buf, len - 1), ZXV_E_FORMAT);
    buf[0] ^= 1;
    IS(zxv_emu_state_import(b, buf, len), ZXV_E_FORMAT);
    buf[0] ^= 1;
    buf[16 + 8] ^= 1; /* rat count */
    IS(zxv_emu_state_import(b, buf, len), ZXV_E_FORMAT);
    buf[16 + 8] ^= 1;
    size_t den0 = 16 + 16 + 8;
    uint8_t keep = buf[den0];
    buf[den0] = 0; /* den 0 */
    memset(buf + den0, 0, 8);
    IS(zxv_emu_state_import(b, buf, len), ZXV_E_FORMAT);
    buf[den0] = keep;
    OK(zxv_emu_state_export(a, buf, len, &len));
    uint8_t hb = buf[len - 1];
    buf[len - 1] = 64;
    IS(zxv_emu_state_import(b, buf, len), ZXV_E_FORMAT);
    buf[len - 1] = hb;
    /* non-reduced rational 2/2 */
    zxv_emu *one = NULL;
    OK(zxv_emu_create(1, 1, 0, 1, 2, 0, &one));
    uint8_t e1[16 + 16 + 16];
    size_t l1;
    OK(zxv_emu_state_export(one, e1, sizeof e1, &l1));
    e1[32] = 2;
    e1[40] = 2;
    IS(zxv_emu_state_import(one, e1, l1), ZXV_E_FORMAT);
    zxv_emu_destroy(one);

    OK(zxv_emu_state_import(b, buf, len));
    uint64_t t = 0;
    OK(zxv_emu_tick(b, &t));
    CHECK(t == 6, "imported at tick 6");
    uint8_t ha[32], hbb[32];
    int32_t fin = 0;
    OK(zxv_emu_hash(a, 6, ha, NULL));
    OK(zxv_emu_hash(b, 6, hbb, &fin));
    CHECK(memcmp(ha, hbb, 32) == 0 && fin == 1, "same hash after transfer");
    for (uint64_t k = 7; k <= 15; k++) {
        submit_all(a, P, k);
        submit_all(b, P, k);
        OK(zxv_emu_advance(a, game_step, &P));
        OK(zxv_emu_advance(b, game_step, &P));
        OK(zxv_emu_hash(a, k, ha, NULL));
        OK(zxv_emu_hash(b, k, hbb, NULL));
        CHECK(memcmp(ha, hbb, 32) == 0, "tick %llu identical after transfer", (unsigned long long) k);
    }
    free(buf);
    zxv_emu_destroy(a);
    zxv_emu_destroy(b);
}

static void test_clock(void) {
    zxv_emu *s = NULL;
    OK(zxv_emu_create(1, 1, 0, 1, 2, 0, &s));
    uint32_t due = 99;
    uint64_t dropped = 99;
    int64_t n, d;
    IS(zxv_emu_clock_feed(s, 1, 60, &due, NULL), ZXV_E_STATE);
    IS(zxv_emu_clock_alpha(s, &n, &d), ZXV_E_STATE);
    IS(zxv_emu_clock_set_step(s, 0, 1, 4), ZXV_E_ARG);
    IS(zxv_emu_clock_set_step(s, -1, 60, 4), ZXV_E_ARG);
    IS(zxv_emu_clock_set_step(s, 1, 60, 0), ZXV_E_ARG);
    IS(zxv_emu_clock_set_step(s, 1, 0, 4), ZXV_E_ARG);
    OK(zxv_emu_clock_set_step(s, 2, 120, 8)); /* 1/60 s */
    OK(zxv_emu_clock_feed(s, 1, 30, &due, &dropped));
    CHECK(due == 2 && dropped == 0, "1/30 s = 2 ticks");
    OK(zxv_emu_clock_feed(s, 1, 120, &due, NULL));
    CHECK(due == 0, "half a tick");
    OK(zxv_emu_clock_alpha(s, &n, &d));
    CHECK(n == 1 && d == 2, "alpha 1/2, got %lld/%lld", (long long) n, (long long) d);
    /* 16,666,667 ns is a hair over 1/60 s + the half already there */
    OK(zxv_emu_clock_feed(s, 16666667, 1000000000, &due, NULL));
    CHECK(due == 1, "one more tick");
    OK(zxv_emu_clock_alpha(s, &n, &d));
    CHECK(n > 0 && n * 2 > d, "carry is exact, a bit over 1/2: %lld/%lld", (long long) n,
          (long long) d);
    OK(zxv_emu_clock_feed(s, 1, 1, &due, &dropped));
    CHECK(due == 8 && dropped == 52, "catch-up capped: due %u dropped %llu", due,
          (unsigned long long) dropped);
    IS(zxv_emu_clock_feed(s, -1, 1, &due, NULL), ZXV_E_ARG);
    IS(zxv_emu_clock_feed(s, 1, 1, NULL, NULL), ZXV_E_ARG);
    zxv_emu_destroy(s);
}

/* The same simulation twice from scratch gives the same hashes (no hidden
 * state: no clocks, no addresses, no uninitialised bytes). */
static void test_replay_determinism(void) {
    uint32_t P = 4;
    uint8_t first[32] = {0}, h[32];
    for (int run = 0; run < 2; run++) {
        zxv_emu *s = new_game(P, 64, 0);
        for (uint64_t t = 1; t <= 200; t++) {
            submit_all(s, P, t);
            OK(zxv_emu_advance(s, game_step, &P));
        }
        OK(zxv_emu_hash(s, 200, h, NULL));
        if (run == 0)
            memcpy(first, h, 32);
        else
            CHECK(memcmp(first, h, 32) == 0, "two runs agree");
        int64_t n, d;
        OK(zxv_emu_rat_get(s, P, &n, &d));
        /* tide = sum over t=1..200 of (t mod 7)/5 = (28 * 21 + 1+2+3+4)/5 = 598/5 */
        CHECK(n == 598 && d == 5, "tide exact: %lld/%lld", (long long) n, (long long) d);
        zxv_emu_destroy(s);
    }
}

/* The confirmed mark keeps up even when nobody asks for it for longer than
 * the input ring holds (regression found by the fuzzer). */
static void test_confirmed_mark(void) {
    uint32_t P = 2;
    zxv_emu *s = new_game(P, 2, ZXV_EMU_F_PREDICT);
    for (uint64_t t = 1; t <= 50; t++) {
        submit_all(s, P, t);
        OK(zxv_emu_advance(s, game_step, &P));
    }
    uint64_t c = 0;
    OK(zxv_emu_confirmed_tick(s, &c));
    CHECK(c == 50, "confirmed 50, got %llu", (unsigned long long) c);
    zxv_emu_destroy(s);
}

int main(void) {
    uint32_t mj = 0, mn = 0, pt = 0;
    if (zxv_version(&mj, &mn, &pt) != ZXV_OK || mj != 1 || mn < 3) {
        fprintf(stderr, "libzxv 1.3+ required\n");
        return 1;
    }
    test_create();
    test_encoding_and_hash();
    test_strict_lockstep();
    test_tick_phases();
    test_budget();
    test_predict_rollback();
    test_export_import();
    test_clock();
    test_replay_determinism();
    test_confirmed_mark();
    printf("test_emu: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
