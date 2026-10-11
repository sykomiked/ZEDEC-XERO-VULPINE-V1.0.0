/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_emu.c — emu-state: deterministic lockstep simulation over libzxv.
 * See include/zxv_emu.h for the model. No floating point in this file
 * (checked by `make check-nofloat` and -mgeneral-regs-only where the
 * compiler has it). */
#include "zxv_emu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- last error ---------------------------------------------------- */

static _Thread_local char g_err[256];

static int32_t fail(int32_t status, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return status;
}

int32_t zxv_emu_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (!out_len) return ZXV_E_ARG;
    if (!buf || cap < n + 1) {
        *out_len = n;
        return ZXV_E_BUFFER;
    }
    memcpy(buf, g_err, n + 1);
    *out_len = n;
    return ZXV_OK;
}

/* ---- the handle ---------------------------------------------------- */

enum { IN_NONE = 0, IN_CONFIRMED = 1, IN_PREDICTED = 2 };

struct zxv_emu {
    uint32_t players, nrat, nhex, inb, hist, flags, ring;
    int started, ticking;
    uint64_t cur;    /* last committed tick                         */
    uint64_t oldest; /* oldest kept snapshot                        */

    int64_t *rat; /* working state: nrat * 2 (num, den)            */
    uint8_t *hex; /* working state: nhex                           */

    uint64_t *snap_tick; /* hist                                   */
    uint8_t *snap_hash;  /* hist * 32                              */
    int64_t *snap_rat;   /* hist * nrat * 2                        */
    uint8_t *snap_hex;   /* hist * nhex                            */

    uint64_t *in_tick;  /* ring * players                          */
    uint8_t *in_state;  /* ring * players                          */
    uint8_t *in_bytes;  /* ring * players * inb                    */

    uint8_t *enc; /* canonical encoding scratch                    */
    size_t enc_len;

    int pending;
    uint64_t pending_tick;
    int desync;
    uint64_t desync_tick;
    uint64_t confirmed;

    const zxv_cap *cap;
    zxv_rmag *rmag;
    uint32_t budget;

    int has_clock;
    int64_t step_n, step_d, acc_n, acc_d;
    uint32_t max_catchup;
};

#define CHECK_SIM(s) \
    do { if (!(s)) return fail(ZXV_E_ARG, "sim is NULL"); } while (0)

static int mul_ok(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) return 0;
    *out = a * b;
    return 1;
}

int32_t zxv_emu_create(uint32_t players, uint32_t rat_slots, uint32_t hex_slots,
                       uint32_t input_bytes, uint32_t history, uint32_t flags,
                       zxv_emu **out_sim) {
    if (!out_sim) return fail(ZXV_E_ARG, "out_sim is NULL");
    if (players < 1 || players > ZXV_EMU_MAX_PLAYERS)
        return fail(ZXV_E_ARG, "players must be 1..%u", ZXV_EMU_MAX_PLAYERS);
    if (rat_slots > ZXV_EMU_MAX_SLOTS || hex_slots > ZXV_EMU_MAX_SLOTS)
        return fail(ZXV_E_ARG, "slots must be 0..%u", ZXV_EMU_MAX_SLOTS);
    if (rat_slots == 0 && hex_slots == 0) return fail(ZXV_E_ARG, "no state slots");
    if (input_bytes < 1 || input_bytes > ZXV_EMU_MAX_INPUT)
        return fail(ZXV_E_ARG, "input_bytes must be 1..%u", ZXV_EMU_MAX_INPUT);
    if (history < ZXV_EMU_MIN_HISTORY || history > ZXV_EMU_MAX_HISTORY)
        return fail(ZXV_E_ARG, "history must be %u..%u", ZXV_EMU_MIN_HISTORY,
                    ZXV_EMU_MAX_HISTORY);
    if (flags & ~(uint32_t) ZXV_EMU_F_PREDICT) return fail(ZXV_E_ARG, "unknown flag bits");

    /* Every size below fits easily in 64 bits; the memory cap keeps it sane. */
    size_t ring = (size_t) history * 2u;
    size_t per_snap = (size_t) rat_slots * 16u + hex_slots + 32u + 8u;
    size_t per_in = (size_t) players * (input_bytes + 9u);
    size_t total, a, b;
    if (!mul_ok(per_snap, history, &a) || !mul_ok(per_in, ring, &b) || a > SIZE_MAX - b)
        return fail(ZXV_E_CAPACITY, "configuration too large");
    total = a + b;
    if (total > ZXV_EMU_MAX_MEMORY)
        return fail(ZXV_E_CAPACITY, "configuration needs %zu bytes, the limit is %u", total,
                    ZXV_EMU_MAX_MEMORY);

    zxv_emu *s = calloc(1, sizeof *s);
    if (!s) return fail(ZXV_E_NOMEM, "out of memory");
    s->players = players;
    s->nrat = rat_slots;
    s->nhex = hex_slots;
    s->inb = input_bytes;
    s->hist = history;
    s->flags = flags;
    s->ring = (uint32_t) ring;
    s->enc_len = ZXV_EMU_DOMAIN_BYTES + 16u + (size_t) rat_slots * 16u + hex_slots;

    s->rat = calloc((size_t) rat_slots * 2u + 1u, sizeof *s->rat);
    s->hex = calloc((size_t) hex_slots + 1u, 1);
    s->snap_tick = calloc(history, sizeof *s->snap_tick);
    s->snap_hash = calloc(history, 32);
    s->snap_rat = calloc((size_t) history * rat_slots * 2u + 1u, sizeof *s->snap_rat);
    s->snap_hex = calloc((size_t) history * hex_slots + 1u, 1);
    s->in_tick = calloc(ring * players, sizeof *s->in_tick);
    s->in_state = calloc(ring * players, 1);
    s->in_bytes = calloc(ring * players * input_bytes, 1);
    s->enc = malloc(s->enc_len);
    if (!s->rat || !s->hex || !s->snap_tick || !s->snap_hash || !s->snap_rat || !s->snap_hex ||
        !s->in_tick || !s->in_state || !s->in_bytes || !s->enc) {
        zxv_emu_destroy(s);
        return fail(ZXV_E_NOMEM, "out of memory");
    }
    for (uint32_t i = 0; i < rat_slots; i++) s->rat[2u * i + 1u] = 1; /* 0/1 */
    for (uint32_t k = 0; k < history; k++) s->snap_tick[k] = UINT64_MAX;
    /* hex slots start at tag 0 (false/entropic/potential); set them in setup. */
    *out_sim = s;
    return ZXV_OK;
}

void zxv_emu_destroy(zxv_emu *s) {
    if (!s) return;
    if (s->rmag) zxv_rmag_destroy(s->rmag);
    free(s->rat);
    free(s->hex);
    free(s->snap_tick);
    free(s->snap_hash);
    free(s->snap_rat);
    free(s->snap_hex);
    free(s->in_tick);
    free(s->in_state);
    free(s->in_bytes);
    free(s->enc);
    free(s);
}

int32_t zxv_emu_info(const zxv_emu *s, uint32_t *players, uint32_t *rats, uint32_t *hexes,
                     uint32_t *inb, uint32_t *hist, uint32_t *flags) {
    CHECK_SIM(s);
    if (players) *players = s->players;
    if (rats) *rats = s->nrat;
    if (hexes) *hexes = s->nhex;
    if (inb) *inb = s->inb;
    if (hist) *hist = s->hist;
    if (flags) *flags = s->flags;
    return ZXV_OK;
}

/* ---- encoding and snapshots ---------------------------------------- */

static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint32_t get_u32(const uint8_t *p) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* Two's complement int64 <-> uint64 without implementation-defined casts. */
static uint64_t i2u(int64_t v) { return (uint64_t) v; }
static int64_t u2i(uint64_t v) {
    if (v <= (uint64_t) INT64_MAX) return (int64_t) v;
    return -(int64_t) (~v) - 1;
}

static void encode(zxv_emu *s, uint64_t tick, const int64_t *rat, const uint8_t *hex) {
    uint8_t *p = s->enc;
    memcpy(p, ZXV_EMU_DOMAIN, ZXV_EMU_DOMAIN_BYTES);
    p += ZXV_EMU_DOMAIN_BYTES;
    put_u64(p, tick);
    put_u32(p + 8, s->nrat);
    put_u32(p + 12, s->nhex);
    p += 16;
    for (uint32_t i = 0; i < s->nrat; i++) {
        put_u64(p, i2u(rat[2u * i]));
        put_u64(p + 8, i2u(rat[2u * i + 1u]));
        p += 16;
    }
    if (s->nhex) memcpy(p, hex, s->nhex);
}

static uint32_t sidx(const zxv_emu *s, uint64_t tick) { return (uint32_t) (tick % s->hist); }

/* Store the working state as the snapshot of `tick` and hash it. */
static int32_t snapshot(zxv_emu *s, uint64_t tick) {
    uint32_t k = sidx(s, tick);
    encode(s, tick, s->rat, s->hex);
    int32_t rc = zxv_sha3_256(s->enc, s->enc_len, s->snap_hash + (size_t) k * 32u);
    if (rc != ZXV_OK) return fail(rc, "zxv_sha3_256 failed (%d)", rc);
    s->snap_tick[k] = tick;
    if (s->nrat)
        memcpy(s->snap_rat + (size_t) k * s->nrat * 2u, s->rat, (size_t) s->nrat * 16u);
    if (s->nhex) memcpy(s->snap_hex + (size_t) k * s->nhex, s->hex, s->nhex);
    return ZXV_OK;
}

static int kept(const zxv_emu *s, uint64_t tick) {
    return s->started && tick >= s->oldest && tick <= s->cur && s->snap_tick[sidx(s, tick)] == tick;
}

static void restore(zxv_emu *s, uint64_t tick) {
    uint32_t k = sidx(s, tick);
    if (s->nrat)
        memcpy(s->rat, s->snap_rat + (size_t) k * s->nrat * 2u, (size_t) s->nrat * 16u);
    if (s->nhex) memcpy(s->hex, s->snap_hex + (size_t) k * s->nhex, s->nhex);
}

/* ---- setup ---------------------------------------------------------- */

int32_t zxv_emu_set_tick_budget(zxv_emu *s, const zxv_cap *cap, int64_t num, int64_t den) {
    CHECK_SIM(s);
    if (!cap) return fail(ZXV_E_ARG, "cap is NULL");
    if (s->ticking) return fail(ZXV_E_STATE, "cannot change the budget inside a tick");
    int32_t rc;
    if (!s->rmag) {
        zxv_rmag *r = NULL;
        uint32_t id = 0;
        rc = zxv_rmag_create_cap(cap, &r);
        if (rc != ZXV_OK) return fail(rc, "zxv_rmag_create_cap: %s", zxv_status_name(rc));
        rc = zxv_rmag_budget(cap, r, "emu.tick", ZXV_RES_COMPUTE_CYCLES, num, den, 1, &id);
        if (rc != ZXV_OK) {
            zxv_rmag_destroy(r);
            return fail(rc, "zxv_rmag_budget: %s", zxv_status_name(rc));
        }
        s->rmag = r;
        s->budget = id;
    } else {
        rc = zxv_rmag_set_limit(cap, s->rmag, s->budget, num, den);
        if (rc != ZXV_OK) return fail(rc, "zxv_rmag_set_limit: %s", zxv_status_name(rc));
    }
    s->cap = cap;
    return ZXV_OK;
}

int32_t zxv_emu_start(zxv_emu *s) {
    CHECK_SIM(s);
    if (s->started) return fail(ZXV_E_STATE, "already started");
    s->cur = 0;
    s->oldest = 0;
    s->confirmed = 0;
    int32_t rc = snapshot(s, 0);
    if (rc != ZXV_OK) return rc;
    s->started = 1;
    return ZXV_OK;
}

/* ---- clock ---------------------------------------------------------- */

int32_t zxv_emu_clock_set_step(zxv_emu *s, int64_t n, int64_t d, uint32_t max_catchup) {
    CHECK_SIM(s);
    int64_t rn, rd;
    int32_t rc = zxv_rat_normalize(n, d, &rn, &rd);
    if (rc != ZXV_OK) return fail(rc, "step: %s", zxv_status_name(rc));
    if (rn <= 0) return fail(ZXV_E_ARG, "the step must be positive");
    if (max_catchup < 1 || max_catchup > 1024) return fail(ZXV_E_ARG, "max_catchup must be 1..1024");
    s->step_n = rn;
    s->step_d = rd;
    s->acc_n = 0;
    s->acc_d = 1;
    s->max_catchup = max_catchup;
    s->has_clock = 1;
    return ZXV_OK;
}

int32_t zxv_emu_clock_feed(zxv_emu *s, int64_t en, int64_t ed, uint32_t *out_due,
                           uint64_t *out_dropped) {
    CHECK_SIM(s);
    if (!out_due) return fail(ZXV_E_ARG, "out_due is NULL");
    if (!s->has_clock) return fail(ZXV_E_STATE, "call zxv_emu_clock_set_step first");
    int64_t n, d, qn, qd, fn, fd, an, ad;
    int32_t rc = zxv_rat_normalize(en, ed, &n, &d);
    if (rc != ZXV_OK) return fail(rc, "elapsed: %s", zxv_status_name(rc));
    if (n < 0) return fail(ZXV_E_ARG, "elapsed time cannot be negative");
    rc = zxv_rat_add(s->acc_n, s->acc_d, n, d, &an, &ad);
    if (rc != ZXV_OK) return fail(rc, "accumulator: %s", zxv_status_name(rc));
    rc = zxv_rat_div(an, ad, s->step_n, s->step_d, &qn, &qd);
    if (rc != ZXV_OK) return fail(rc, "accumulator / step: %s", zxv_status_name(rc));
    uint64_t k = (uint64_t) (qn / qd); /* qn >= 0, qd > 0 */
    /* acc - k*step = step * frac(acc/step); never larger than one step. */
    rc = zxv_rat_mul(s->step_n, s->step_d, qn % qd, qd, &fn, &fd);
    if (rc != ZXV_OK) return fail(rc, "accumulator: %s", zxv_status_name(rc));
    s->acc_n = fn;
    s->acc_d = fd;
    uint32_t due = k > s->max_catchup ? s->max_catchup : (uint32_t) k;
    *out_due = due;
    if (out_dropped) *out_dropped = k - due;
    return ZXV_OK;
}

int32_t zxv_emu_clock_alpha(const zxv_emu *s, int64_t *out_n, int64_t *out_d) {
    CHECK_SIM(s);
    if (!out_n || !out_d) return fail(ZXV_E_ARG, "NULL out pointer");
    if (!s->has_clock) return fail(ZXV_E_STATE, "call zxv_emu_clock_set_step first");
    int64_t n, d;
    int32_t rc = zxv_rat_div(s->acc_n, s->acc_d, s->step_n, s->step_d, &n, &d);
    if (rc != ZXV_OK) return fail(rc, "alpha: %s", zxv_status_name(rc));
    *out_n = n;
    *out_d = d;
    return ZXV_OK;
}

/* ---- inputs --------------------------------------------------------- */

static size_t iidx(const zxv_emu *s, uint64_t tick, uint32_t player) {
    return (size_t) (tick % s->ring) * s->players + player;
}
static uint8_t *ibytes(zxv_emu *s, size_t i) { return s->in_bytes + i * s->inb; }

/* Earliest tick whose state is not final yet, minus one, scanning forward
 * from the cached value. Does not modify `s`. */
static uint64_t scan_confirmed(const zxv_emu *s) {
    uint64_t c = s->confirmed;
    if (c > s->cur) c = s->cur;
    for (uint64_t t = c + 1; t <= s->cur; t++) {
        if (s->pending && t >= s->pending_tick) break;
        uint32_t p;
        for (p = 0; p < s->players; p++) {
            size_t i = iidx(s, t, p);
            if (s->in_tick[i] != t || s->in_state[i] != IN_CONFIRMED) break;
        }
        if (p < s->players) break;
        c = t;
    }
    return c;
}

int32_t zxv_emu_input_submit(zxv_emu *s, uint32_t player, uint64_t tick, const uint8_t *bytes,
                             size_t len) {
    CHECK_SIM(s);
    if (player >= s->players) return fail(ZXV_E_ARG, "player %u out of range", player);
    if (!bytes || len != s->inb) return fail(ZXV_E_ARG, "an input is exactly %u bytes", s->inb);
    if (tick < 1) return fail(ZXV_E_ARG, "inputs start at tick 1");
    if (s->ticking) return fail(ZXV_E_STATE, "cannot submit inputs inside a tick");
    if (tick > s->cur + s->hist)
        return fail(ZXV_E_RANGE, "tick %llu is beyond the input window (current %llu + %u)",
                    (unsigned long long) tick, (unsigned long long) s->cur, s->hist);
    size_t i = iidx(s, tick, player);
    uint8_t *dst = ibytes(s, i);
    int have = s->in_tick[i] == tick && s->in_state[i] != IN_NONE;

    if (have && s->in_state[i] == IN_CONFIRMED) { /* a resend: fine if identical */
        if (memcmp(dst, bytes, len) == 0) return ZXV_OK;
        return fail(ZXV_E_DUPLICATE, "player %u already has a different input for tick %llu",
                    player, (unsigned long long) tick);
    }
    if (s->started && tick <= s->cur) {
        if (tick - 1 < s->oldest || !have)
            return fail(ZXV_E_RANGE, "tick %llu is too late: its rollback point is gone",
                        (unsigned long long) tick);
        /* predicted, already simulated */
        if (memcmp(dst, bytes, len) != 0) {
            memcpy(dst, bytes, len);
            if (!s->pending || tick < s->pending_tick) {
                s->pending = 1;
                s->pending_tick = tick;
            }
        }
        s->in_state[i] = IN_CONFIRMED;
        s->confirmed = scan_confirmed(s);
        return ZXV_OK;
    }
    memcpy(dst, bytes, len);
    s->in_tick[i] = tick;
    s->in_state[i] = IN_CONFIRMED;
    return ZXV_OK;
}

int32_t zxv_emu_input_get(const zxv_emu *s, uint32_t player, uint8_t *buf, size_t cap,
                          size_t *out_len, int32_t *out_predicted) {
    CHECK_SIM(s);
    if (!out_len) return fail(ZXV_E_ARG, "out_len is NULL");
    if (player >= s->players) return fail(ZXV_E_ARG, "player %u out of range", player);
    if (!s->ticking) return fail(ZXV_E_STATE, "inputs are read inside a tick");
    if (!buf || cap < s->inb) {
        *out_len = s->inb;
        return ZXV_E_BUFFER;
    }
    size_t i = iidx(s, s->cur + 1, player);
    memcpy(buf, s->in_bytes + i * s->inb, s->inb);
    *out_len = s->inb;
    if (out_predicted) *out_predicted = s->in_state[i] == IN_PREDICTED;
    return ZXV_OK;
}

static int inputs_complete(const zxv_emu *s, uint64_t t) {
    for (uint32_t p = 0; p < s->players; p++) {
        size_t i = iidx(s, t, p);
        if (s->in_tick[i] != t || s->in_state[i] != IN_CONFIRMED) return 0;
    }
    return 1;
}

int32_t zxv_emu_ready(const zxv_emu *s, int32_t *out_ready) {
    CHECK_SIM(s);
    if (!out_ready) return fail(ZXV_E_ARG, "out_ready is NULL");
    int r = s->started && !s->ticking;
    if (r) r = (s->flags & ZXV_EMU_F_PREDICT) ? !s->pending : inputs_complete(s, s->cur + 1);
    *out_ready = r;
    return ZXV_OK;
}

/* ---- ticks ---------------------------------------------------------- */

int32_t zxv_emu_tick(const zxv_emu *s, uint64_t *out_tick) {
    CHECK_SIM(s);
    if (!out_tick) return fail(ZXV_E_ARG, "out_tick is NULL");
    *out_tick = s->cur;
    return ZXV_OK;
}

int32_t zxv_emu_begin(zxv_emu *s) {
    CHECK_SIM(s);
    if (!s->started) return fail(ZXV_E_STATE, "call zxv_emu_start first");
    if (s->ticking) return fail(ZXV_E_STATE, "already inside a tick");
    if (s->cur == UINT64_MAX - s->hist) return fail(ZXV_E_RANGE, "tick counter exhausted");
    uint64_t t = s->cur + 1;
    int predict = (s->flags & ZXV_EMU_F_PREDICT) != 0;
    if (predict && s->pending)
        return fail(ZXV_E_STATE, "a rollback to tick %llu is pending",
                    (unsigned long long) (s->pending_tick - 1));
    if (!predict && !inputs_complete(s, t))
        return fail(ZXV_E_STATE, "inputs for tick %llu have not all arrived",
                    (unsigned long long) t);
    if (s->rmag) {
        int64_t un = 0, ud = 1;
        int32_t rc = zxv_rmag_status(s->rmag, s->budget, NULL, NULL, &un, &ud, NULL);
        if (rc == ZXV_OK && un > 0) rc = zxv_rmag_release(s->cap, s->rmag, s->budget, un, ud);
        if (rc != ZXV_OK) return fail(rc, "resetting the tick budget: %s", zxv_status_name(rc));
    }
    if (predict) {
        for (uint32_t p = 0; p < s->players; p++) {
            size_t i = iidx(s, t, p);
            if (s->in_tick[i] == t && s->in_state[i] == IN_CONFIRMED) continue;
            size_t j = iidx(s, t - 1, p);
            if (t > 1 && s->in_tick[j] == t - 1 && s->in_state[j] != IN_NONE)
                memcpy(ibytes(s, i), ibytes(s, j), s->inb);
            else
                memset(ibytes(s, i), 0, s->inb);
            s->in_tick[i] = t;
            s->in_state[i] = IN_PREDICTED;
        }
    }
    s->ticking = 1;
    return ZXV_OK;
}

int32_t zxv_emu_commit(zxv_emu *s) {
    CHECK_SIM(s);
    if (!s->ticking) return fail(ZXV_E_STATE, "not inside a tick");
    int32_t rc = snapshot(s, s->cur + 1);
    if (rc != ZXV_OK) return rc;
    s->cur++;
    if (s->cur >= s->hist && s->cur - s->hist + 1 > s->oldest) s->oldest = s->cur - s->hist + 1;
    s->ticking = 0;
    /* Keep the confirmed mark current while the inputs it scans are still
     * in the ring (the scan resumes from it). */
    s->confirmed = scan_confirmed(s);
    return ZXV_OK;
}

int32_t zxv_emu_abort(zxv_emu *s) {
    CHECK_SIM(s);
    if (!s->ticking) return fail(ZXV_E_STATE, "not inside a tick");
    restore(s, s->cur);
    s->ticking = 0;
    return ZXV_OK;
}

int32_t zxv_emu_advance(zxv_emu *s, zxv_emu_step_fn fn, void *user) {
    CHECK_SIM(s);
    if (!fn) return fail(ZXV_E_ARG, "fn is NULL");
    int32_t rc = zxv_emu_begin(s);
    if (rc != ZXV_OK) return rc;
    rc = fn(s, s->cur + 1, user);
    if (rc != ZXV_OK) {
        restore(s, s->cur);
        s->ticking = 0;
        return rc;
    }
    return zxv_emu_commit(s);
}

/* ---- state ---------------------------------------------------------- */

static int32_t writable(const zxv_emu *s) {
    if (s->started && !s->ticking)
        return fail(ZXV_E_STATE, "state changes only during setup or inside a tick");
    return ZXV_OK;
}

int32_t zxv_emu_rat_get(const zxv_emu *s, uint32_t slot, int64_t *n, int64_t *d) {
    CHECK_SIM(s);
    if (!n || !d) return fail(ZXV_E_ARG, "NULL out pointer");
    if (slot >= s->nrat) return fail(ZXV_E_ARG, "rat slot %u out of range", slot);
    *n = s->rat[2u * slot];
    *d = s->rat[2u * slot + 1u];
    return ZXV_OK;
}

int32_t zxv_emu_rat_set(zxv_emu *s, uint32_t slot, int64_t num, int64_t den) {
    CHECK_SIM(s);
    if (slot >= s->nrat) return fail(ZXV_E_ARG, "rat slot %u out of range", slot);
    int32_t rc = writable(s);
    if (rc != ZXV_OK) return rc;
    int64_t n, d;
    rc = zxv_rat_normalize(num, den, &n, &d);
    if (rc != ZXV_OK) return fail(rc, "rat slot %u: %s", slot, zxv_status_name(rc));
    s->rat[2u * slot] = n;
    s->rat[2u * slot + 1u] = d;
    return ZXV_OK;
}

int32_t zxv_emu_rat_add(zxv_emu *s, uint32_t slot, int64_t num, int64_t den) {
    CHECK_SIM(s);
    if (slot >= s->nrat) return fail(ZXV_E_ARG, "rat slot %u out of range", slot);
    int32_t rc = writable(s);
    if (rc != ZXV_OK) return rc;
    int64_t n, d;
    rc = zxv_rat_add(s->rat[2u * slot], s->rat[2u * slot + 1u], num, den, &n, &d);
    if (rc != ZXV_OK) return fail(rc, "rat slot %u add: %s", slot, zxv_status_name(rc));
    s->rat[2u * slot] = n;
    s->rat[2u * slot + 1u] = d;
    return ZXV_OK;
}

int32_t zxv_emu_rat_mul(zxv_emu *s, uint32_t slot, int64_t num, int64_t den) {
    CHECK_SIM(s);
    if (slot >= s->nrat) return fail(ZXV_E_ARG, "rat slot %u out of range", slot);
    int32_t rc = writable(s);
    if (rc != ZXV_OK) return rc;
    int64_t n, d;
    rc = zxv_rat_mul(s->rat[2u * slot], s->rat[2u * slot + 1u], num, den, &n, &d);
    if (rc != ZXV_OK) return fail(rc, "rat slot %u mul: %s", slot, zxv_status_name(rc));
    s->rat[2u * slot] = n;
    s->rat[2u * slot + 1u] = d;
    return ZXV_OK;
}

int32_t zxv_emu_hex_get(const zxv_emu *s, uint32_t slot, uint8_t *out_tag) {
    CHECK_SIM(s);
    if (!out_tag) return fail(ZXV_E_ARG, "out_tag is NULL");
    if (slot >= s->nhex) return fail(ZXV_E_ARG, "hex slot %u out of range", slot);
    *out_tag = s->hex[slot];
    return ZXV_OK;
}

int32_t zxv_emu_hex_set(zxv_emu *s, uint32_t slot, uint8_t tag) {
    CHECK_SIM(s);
    if (slot >= s->nhex) return fail(ZXV_E_ARG, "hex slot %u out of range", slot);
    if (zxv_hex_unpack(tag, NULL, NULL, NULL) != ZXV_OK)
        return fail(ZXV_E_ARG, "tag %u is above 63", tag);
    int32_t rc = writable(s);
    if (rc != ZXV_OK) return rc;
    s->hex[slot] = tag;
    return ZXV_OK;
}

int32_t zxv_emu_charge(zxv_emu *s, int64_t num, int64_t den) {
    CHECK_SIM(s);
    if (!s->ticking) return fail(ZXV_E_STATE, "charges are made inside a tick");
    if (!s->rmag) return ZXV_OK;
    int32_t rc = zxv_rmag_consume(s->cap, s->rmag, s->budget, num, den);
    if (rc != ZXV_OK)
        return fail(rc, "tick %llu budget: %s", (unsigned long long) (s->cur + 1),
                    zxv_status_name(rc));
    return ZXV_OK;
}

/* ---- hashes, desync, rollback --------------------------------------- */

int32_t zxv_emu_hash(const zxv_emu *s, uint64_t tick, uint8_t out[32], int32_t *out_final) {
    CHECK_SIM(s);
    if (!out) return fail(ZXV_E_ARG, "out is NULL");
    if (!kept(s, tick)) return fail(ZXV_E_NOT_FOUND, "tick %llu is not kept", (unsigned long long) tick);
    memcpy(out, s->snap_hash + (size_t) sidx(s, tick) * 32u, 32);
    if (out_final) *out_final = tick <= scan_confirmed(s);
    return ZXV_OK;
}

int32_t zxv_emu_confirmed_tick(zxv_emu *s, uint64_t *out_tick) {
    CHECK_SIM(s);
    if (!out_tick) return fail(ZXV_E_ARG, "out_tick is NULL");
    if (!s->started) return fail(ZXV_E_STATE, "not started");
    s->confirmed = scan_confirmed(s);
    *out_tick = s->confirmed;
    return ZXV_OK;
}

int32_t zxv_emu_compare(zxv_emu *s, uint64_t tick, const uint8_t remote[32], int32_t *out_lp) {
    CHECK_SIM(s);
    if (!remote || !out_lp) return fail(ZXV_E_ARG, "NULL pointer");
    if (!kept(s, tick)) {
        *out_lp = ZXV_LP_NEITHER;
        return ZXV_OK;
    }
    s->confirmed = scan_confirmed(s);
    if (tick > s->confirmed) {
        *out_lp = ZXV_LP_NEITHER;
        return ZXV_OK;
    }
    if (memcmp(remote, s->snap_hash + (size_t) sidx(s, tick) * 32u, 32) == 0) {
        *out_lp = ZXV_LP_TRUE;
        return ZXV_OK;
    }
    if (!s->desync || tick < s->desync_tick) {
        s->desync = 1;
        s->desync_tick = tick;
    }
    *out_lp = ZXV_LP_FALSE;
    return ZXV_OK;
}

int32_t zxv_emu_desync(const zxv_emu *s, uint64_t *out_tick, int32_t *out_has) {
    CHECK_SIM(s);
    if (!out_tick || !out_has) return fail(ZXV_E_ARG, "NULL out pointer");
    *out_has = s->desync;
    *out_tick = s->desync ? s->desync_tick : 0;
    return ZXV_OK;
}

int32_t zxv_emu_rollback_pending(const zxv_emu *s, uint64_t *out_tick, int32_t *out_pending) {
    CHECK_SIM(s);
    if (!out_tick || !out_pending) return fail(ZXV_E_ARG, "NULL out pointer");
    *out_pending = s->pending;
    *out_tick = s->pending ? s->pending_tick : 0;
    return ZXV_OK;
}

int32_t zxv_emu_rollback(zxv_emu *s, uint64_t to_tick) {
    CHECK_SIM(s);
    if (!s->started) return fail(ZXV_E_STATE, "not started");
    if (s->ticking) return fail(ZXV_E_STATE, "cannot roll back inside a tick");
    if (!kept(s, to_tick))
        return fail(ZXV_E_NOT_FOUND, "tick %llu is not kept (oldest %llu, current %llu)",
                    (unsigned long long) to_tick, (unsigned long long) s->oldest,
                    (unsigned long long) s->cur);
    s->confirmed = scan_confirmed(s);
    restore(s, to_tick);
    s->cur = to_tick;
    if (s->confirmed > to_tick) s->confirmed = to_tick;
    if (s->pending && to_tick < s->pending_tick) s->pending = 0;
    return ZXV_OK;
}

int32_t zxv_emu_rollback_replay(zxv_emu *s, zxv_emu_step_fn fn, void *user) {
    CHECK_SIM(s);
    if (!fn) return fail(ZXV_E_ARG, "fn is NULL");
    if (!s->pending) return ZXV_OK;
    uint64_t target = s->cur;
    int32_t rc = zxv_emu_rollback(s, s->pending_tick - 1);
    if (rc != ZXV_OK) return rc;
    while (s->cur < target) {
        rc = zxv_emu_advance(s, fn, user);
        if (rc != ZXV_OK) return rc;
    }
    return ZXV_OK;
}

/* ---- state transfer ------------------------------------------------- */

int32_t zxv_emu_state_export(const zxv_emu *s, uint8_t *buf, size_t cap, size_t *out_len) {
    CHECK_SIM(s);
    if (!out_len) return fail(ZXV_E_ARG, "out_len is NULL");
    if (s->ticking) return fail(ZXV_E_STATE, "cannot export inside a tick");
    if (!buf || cap < s->enc_len) {
        *out_len = s->enc_len;
        return ZXV_E_BUFFER;
    }
    /* The encoding scratch belongs to the handle; write straight to buf. */
    uint8_t *p = buf;
    memcpy(p, ZXV_EMU_DOMAIN, ZXV_EMU_DOMAIN_BYTES);
    p += ZXV_EMU_DOMAIN_BYTES;
    put_u64(p, s->cur);
    put_u32(p + 8, s->nrat);
    put_u32(p + 12, s->nhex);
    p += 16;
    for (uint32_t i = 0; i < s->nrat; i++) {
        put_u64(p, i2u(s->rat[2u * i]));
        put_u64(p + 8, i2u(s->rat[2u * i + 1u]));
        p += 16;
    }
    if (s->nhex) memcpy(p, s->hex, s->nhex);
    *out_len = s->enc_len;
    return ZXV_OK;
}

int32_t zxv_emu_state_import(zxv_emu *s, const uint8_t *buf, size_t len) {
    CHECK_SIM(s);
    if (!buf) return fail(ZXV_E_ARG, "buf is NULL");
    if (s->ticking) return fail(ZXV_E_STATE, "cannot import inside a tick");
    if (len != s->enc_len) return fail(ZXV_E_FORMAT, "expected %zu bytes, got %zu", s->enc_len, len);
    if (memcmp(buf, ZXV_EMU_DOMAIN, ZXV_EMU_DOMAIN_BYTES) != 0)
        return fail(ZXV_E_FORMAT, "not an emu-state v1 encoding");
    const uint8_t *p = buf + ZXV_EMU_DOMAIN_BYTES;
    uint64_t tick = get_u64(p);
    if (get_u32(p + 8) != s->nrat || get_u32(p + 12) != s->nhex)
        return fail(ZXV_E_FORMAT, "slot counts differ from this sim");
    if (tick > UINT64_MAX / 2) return fail(ZXV_E_FORMAT, "tick out of range");
    p += 16;
    /* Validate everything before touching the state. */
    for (uint32_t i = 0; i < s->nrat; i++) {
        int64_t n = u2i(get_u64(p + 16u * i)), d = u2i(get_u64(p + 16u * i + 8u)), rn, rd;
        if (d <= 0 || zxv_rat_normalize(n, d, &rn, &rd) != ZXV_OK || rn != n || rd != d)
            return fail(ZXV_E_FORMAT, "rat slot %u is not a reduced rational", i);
    }
    const uint8_t *h = p + (size_t) s->nrat * 16u;
    for (uint32_t i = 0; i < s->nhex; i++)
        if (h[i] > 63) return fail(ZXV_E_FORMAT, "hex slot %u holds %u (> 63)", i, h[i]);
    for (uint32_t i = 0; i < s->nrat; i++) {
        s->rat[2u * i] = u2i(get_u64(p + 16u * i));
        s->rat[2u * i + 1u] = u2i(get_u64(p + 16u * i + 8u));
    }
    if (s->nhex) memcpy(s->hex, h, s->nhex);
    for (uint32_t k = 0; k < s->hist; k++) s->snap_tick[k] = UINT64_MAX;
    int32_t rc = snapshot(s, tick);
    if (rc != ZXV_OK) return rc;
    s->cur = tick;
    s->oldest = tick;
    s->confirmed = tick;
    s->started = 1;
    s->pending = 0;
    s->desync = 0;
    return ZXV_OK;
}
