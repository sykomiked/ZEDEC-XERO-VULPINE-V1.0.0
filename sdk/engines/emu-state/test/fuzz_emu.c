/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_emu.c — the emu-state fuzz target.
 *
 * Each input is a small program. It drives a PREDICT-mode sim A whose
 * remote players' inputs arrive late, out of order and repeated, mixed with
 * rollbacks, replays, hash comparisons, state transfers and clock feeds,
 * and a STRICT reference sim R that gets every input on time. Invariants:
 *
 *   - every call returns ZXV_OK or one of the documented ZXV_E_* codes;
 *   - the same call sequence gives the same results (status determinism);
 *   - an arbitrary byte string given to zxv_emu_state_import is either
 *     refused with ZXV_E_FORMAT or round-trips through export unchanged;
 *   - once every input has been delivered and the pending rollback
 *     replayed, A is confirmed up to its tick and every kept hash equals
 *     R's (unless an input arrived too late to be applied, which the
 *     library reports with ZXV_E_RANGE).
 *
 * -DZXV_EMU_FUZZ_MAIN gives a deterministic driver (`make test`, `make
 * fuzz`, CI under ASan/UBSan; ZXV_EMU_FUZZ_SEED=n picks another stream);
 * clang -fsanitize=fuzzer drives the same entry point. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "zxv_emu.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_runs, g_compared, g_late;

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_emu: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.bin", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.bin\n");
    }
    abort();
}

static int documented(int32_t rc) {
    switch (rc) {
    case ZXV_OK: case ZXV_E_ARG: case ZXV_E_BUFFER: case ZXV_E_STATE: case ZXV_E_RANGE:
    case ZXV_E_DUPLICATE: case ZXV_E_BUDGET: case ZXV_E_CAPACITY: case ZXV_E_FORMAT:
    case ZXV_E_NOMEM: case ZXV_E_NOT_FOUND:
        return 1;
    default:
        return 0;
    }
}

static void input_for(uint8_t salt, uint32_t p, uint64_t t, uint8_t out[GAME_INPUT]) {
    uint64_t x = (t * 0x9E3779B97F4A7C15ull) ^ ((uint64_t) p << 32) ^ salt;
    x ^= x >> 29;
    out[0] = (uint8_t) ((x % 3u) == 0 ? 0 : ((x % 3u) == 1 ? 1 : 0xFF));
    out[1] = (uint8_t) ((x >> 8) % 7u == 0 ? 1u : ((x >> 8) % 61u == 0 ? 2u : 0u));
}

#define MAX_TICK 48u

typedef struct {
    const uint8_t *d;
    size_t n, i;
} reader;
static uint8_t rd(reader *r) { return r->i < r->n ? r->d[r->i++] : 0; }

/* Run one program; returns a digest of every status seen (determinism). */
static uint64_t run(const uint8_t *data, size_t size, int check) {
    reader r = {data, size, 0};
    uint32_t P = 1u + rd(&r) % 4u, H = 2u + rd(&r) % 14u;
    uint8_t salt = rd(&r);
    uint64_t digest = 1469598103934665603ull;
#define SEEN(rc)                                                                   \
    do {                                                                           \
        int32_t rc_ = (rc);                                                        \
        if (check && !documented(rc_)) die("undocumented status", data, size);     \
        digest = (digest ^ (uint64_t) (uint32_t) rc_) * 1099511628211ull;         \
    } while (0)

    zxv_emu *a = NULL, *ref = NULL;
    if (zxv_emu_create(P, game_rat_slots(P), P, GAME_INPUT, H, ZXV_EMU_F_PREDICT, &a) != ZXV_OK ||
        zxv_emu_create(P, game_rat_slots(P), P, GAME_INPUT, MAX_TICK + 2u, 0, &ref) != ZXV_OK)
        die("create", data, size);
    if (game_setup(a, P) != ZXV_OK || game_setup(ref, P) != ZXV_OK || zxv_emu_start(a) != ZXV_OK ||
        zxv_emu_start(ref) != ZXV_OK)
        die("setup", data, size);
    int late = 0;     /* an input was refused as too late: A may differ */
    SEEN(zxv_emu_clock_set_step(a, 1, 60, 1u + (uint32_t) (salt % 16u)));

    while (r.i < r.n) {
        uint8_t op = rd(&r);
        uint64_t cur = 0;
        zxv_emu_tick(a, &cur);
        switch (op % 9u) {
        case 0: case 1: case 2: { /* advance A (local player 0 on time) */
            if (cur >= MAX_TICK) break;
            uint8_t x[GAME_INPUT];
            input_for(salt, 0, cur + 1, x);
            SEEN(zxv_emu_input_submit(a, 0, cur + 1, x, sizeof x));
            SEEN(zxv_emu_advance(a, game_step, &P));
            break;
        }
        case 3: case 4: { /* deliver one remote input, maybe late or early */
            uint32_t p = P > 1 ? 1u + rd(&r) % (P - 1u) : 0u;
            int64_t off = (int64_t) (rd(&r) % 24u) - 12;
            int64_t t = (int64_t) cur + off;
            if (t < 1 || t > (int64_t) MAX_TICK) break;
            uint8_t x[GAME_INPUT];
            input_for(salt, p, (uint64_t) t, x);
            int32_t rc = zxv_emu_input_submit(a, p, (uint64_t) t, x, sizeof x);
            SEEN(rc);
            if (rc == ZXV_E_RANGE && (uint64_t) t <= cur) late = 1;
            break;
        }
        case 5: /* resolve a pending rollback */
            SEEN(zxv_emu_rollback_replay(a, game_step, &P));
            break;
        case 6: { /* manual rollback a few ticks */
            uint64_t back = rd(&r) % 6u;
            SEEN(zxv_emu_rollback(a, cur >= back ? cur - back : 0));
            break;
        }
        case 7: { /* compare a tick with the reference */
            uint64_t t = rd(&r) % (MAX_TICK + 1u), rt = 0;
            zxv_emu_tick(ref, &rt);
            while (rt < t) { /* the reference always has every input */
                for (uint32_t p = 0; p < P; p++) {
                    uint8_t x[GAME_INPUT];
                    input_for(salt, p, rt + 1, x);
                    zxv_emu_input_submit(ref, p, rt + 1, x, sizeof x);
                }
                if (zxv_emu_advance(ref, game_step, &P) != ZXV_OK) die("ref advance", data, size);
                rt++;
            }
            uint8_t h[32];
            int32_t lp = -1;
            if (zxv_emu_hash(ref, t, h, NULL) != ZXV_OK) die("ref hash", data, size);
            SEEN(zxv_emu_compare(a, t, h, &lp));
            /* A final hash can never differ: a tick whose input came too late
             * is never final. */
            if (check && lp == ZXV_LP_FALSE) die("a final hash differs from the reference", data, size);
            break;
        }
        case 8: { /* garbage import into a scratch sim, then a clock feed */
            zxv_emu *g = NULL;
            if (zxv_emu_create(P, game_rat_slots(P), P, GAME_INPUT, 2, 0, &g) != ZXV_OK)
                die("create scratch", data, size);
            size_t need = 0;
            zxv_emu_state_export(g, NULL, 0, &need);
            uint8_t *buf = malloc(need), *back = malloc(need);
            if (!buf || !back) die("oom", data, size);
            zxv_emu_state_export(a, buf, need, &need);
            for (uint32_t k = rd(&r) % 4u; k > 0; k--) buf[rd(&r) % need] ^= (uint8_t) (1u << (rd(&r) % 8u));
            int32_t rc = zxv_emu_state_import(g, buf, need);
            SEEN(rc);
            if (rc == ZXV_OK) {
                size_t m = 0;
                if (zxv_emu_state_export(g, back, need, &m) != ZXV_OK || m != need ||
                    memcmp(buf, back, need) != 0)
                    die("import/export is not a round trip", data, size);
            } else if (rc != ZXV_E_FORMAT) {
                die("import refused with a code other than FORMAT", data, size);
            }
            free(buf);
            free(back);
            zxv_emu_destroy(g);
            uint32_t due = 0;
            SEEN(zxv_emu_clock_feed(a, rd(&r), 1u + rd(&r), &due, NULL));
            int64_t an = 0, ad = 1;
            SEEN(zxv_emu_clock_alpha(a, &an, &ad));
            if (check && (an < 0 || an >= ad)) die("alpha outside [0, 1)", data, size);
            break;
        }
        }
    }

    /* Deliver everything, replay, and compare with the reference. */
    uint64_t cur = 0;
    zxv_emu_tick(a, &cur);
    for (uint64_t t = 1; t <= cur; t++)
        for (uint32_t p = 1; p < P; p++) {
            uint8_t x[GAME_INPUT];
            input_for(salt, p, t, x);
            int32_t rc = zxv_emu_input_submit(a, p, t, x, sizeof x);
            SEEN(rc);
            if (rc == ZXV_E_RANGE) late = 1;
        }
    SEEN(zxv_emu_rollback_replay(a, game_step, &P));
    uint64_t c = 0, rt = 0;
    zxv_emu_confirmed_tick(a, &c);
    zxv_emu_tick(ref, &rt);
    while (rt < cur) {
        for (uint32_t p = 0; p < P; p++) {
            uint8_t x[GAME_INPUT];
            input_for(salt, p, rt + 1, x);
            zxv_emu_input_submit(ref, p, rt + 1, x, sizeof x);
        }
        if (zxv_emu_advance(ref, game_step, &P) != ZXV_OK) die("ref advance", data, size);
        rt++;
    }
    if (check) {
        if (!late && c != cur) die("not confirmed after every input arrived", data, size);
        for (uint64_t t = cur >= H ? cur - H + 1 : 0; t <= cur; t++) {
            uint8_t ha[32], hr[32];
            int32_t fin = 0;
            if (zxv_emu_hash(a, t, ha, &fin) != ZXV_OK) continue; /* not kept after a rollback */
            if (zxv_emu_hash(ref, t, hr, NULL) != ZXV_OK) die("ref hash", data, size);
            if (!late && !fin) die("a tick is not final after every input arrived", data, size);
            if (fin && memcmp(ha, hr, 32) != 0) die("A differs from the reference", data, size);
            g_compared++;
        }
    }
    if (late && check) g_late++;
    zxv_emu_destroy(a);
    zxv_emu_destroy(ref);
    return digest;
#undef SEEN
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 4096) return 0;
    uint64_t d1 = run(data, size, 1), d2 = run(data, size, 0);
    if (d1 != d2) die("status sequence not deterministic", data, size);
    g_runs++;
    return 0;
}

#ifdef ZXV_EMU_FUZZ_MAIN

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

int main(int argc, char **argv) {
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 2000;
    const char *seed = getenv("ZXV_EMU_FUZZ_SEED");
    if (seed) g_rng ^= strtoull(seed, NULL, 10) * 0xD1B54A32D192ED03ull;
    uint8_t buf[512];
    for (unsigned long long it = 0; it < iters; it++) {
        size_t n = 3u + (size_t) (rnd() % (sizeof buf - 3u));
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t) rnd();
        /* bias towards advancing so runs reach deep ticks */
        for (size_t i = 3; i < n; i += 2u + (size_t) (rnd() % 3u)) buf[i] = (uint8_t) (rnd() % 3u);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_emu: %llu programs, %llu final hashes matched the reference, %llu with late inputs\n",
           g_runs, g_compared, g_late);
    return 0;
}

#endif
