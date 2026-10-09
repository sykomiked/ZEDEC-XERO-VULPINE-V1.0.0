/* test_curzi_shamir.c — host test for CURZI-8889-A LAYER 2 (curzi_shamir.c).
 *
 * HOST ONLY. This file uses libc (printf) on purpose; the kernel-side
 * curzi_shamir.c uses none. Build:
 *   gcc -std=c11 -Wall -Werror -Wextra -o test_curzi_shamir \
 *       kernel/src/curzi/curzi_shamir.c kernel/src/curzi/test_curzi_shamir.c
 *
 * These are not smoke tests. curzi8889a.h lists what would FALSIFY the design
 * ("If any k-1 subset of shares can be shown to constrain the secret, LAYER 2
 * is void"), and each test below is the corresponding negative check:
 *
 *   T0  field is the right field, checked against an INDEPENDENT construction
 *   T1  round trip, over many different k-subsets and many (n,k)
 *   T2  k-1 shares reconstruct a WRONG secret
 *   T3  k-1 shares are consistent with EVERY secret — all 256 exhibited
 *   T4  refusals, with the right code, and the output buffer left untouched
 *   T5  determinism: same randomness -> byte-identical shares
 *
 * T0 and T3 are the load-bearing ones. T1 alone would pass against a
 * self-consistent WRONG field and against a scheme with no secrecy at all.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (CURZI-8889-A threshold slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "curzi8889a.h"

/* Internal field primitives, exported by curzi_shamir.c for this cross-check.
 * See the "exposure note" there for why they are not static. */
uint8_t curzi_gf_mul(uint8_t a, uint8_t b);
uint8_t curzi_gf_inv(uint8_t a);

/* ------------------------------------------------------------------ harness */
static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* ------------------------------------------------- independent GF(2^8) ref */
/* Deliberately a DIFFERENT algorithm from curzi_gf_mul: full 16-bit carry-less
 * product first, then explicit polynomial long division by the literal 0x11b.
 * curzi_gf_mul interleaves multiply and reduce. If both agree on all 65536
 * inputs, the reduction polynomial and the carry handling are both right; a
 * shared bug would have to be a shared bug between two structurally different
 * routines. Not constant time and not meant to be — host reference only. */
static uint8_t ref_mul(uint8_t a, uint8_t b) {
    uint16_t p = 0;
    int i;
    for (i = 0; i < 8; i++) {
        if ((b >> i) & 1u) { p ^= (uint16_t)((uint16_t)a << i); }
    }
    for (i = 15; i >= 8; i--) {
        if ((p >> i) & 1u) { p ^= (uint16_t)(0x11bu << (i - 8)); }
    }
    return (uint8_t)p;
}

/* Brute-force inverse: search, no cleverness, so it cannot share a bug with an
 * addition chain. */
static uint8_t ref_inv(uint8_t a) {
    int b;
    if (a == 0) { return 0; }
    for (b = 1; b < 256; b++) {
        if (ref_mul(a, (uint8_t)b) == 1u) { return (uint8_t)b; }
    }
    return 0; /* unreachable in a field */
}

/* Lagrange basis at x = 0 over the reference field. Used by T3 to FORGE a
 * share, i.e. to do the attacker's arithmetic without borrowing the
 * implementation's. */
static void ref_lagrange0(const uint8_t *xs, int k, uint8_t *out_L) {
    int i, m;
    for (i = 0; i < k; i++) {
        uint8_t num = 1, den = 1;
        for (m = 0; m < k; m++) {
            if (m == i) { continue; }
            num = ref_mul(num, xs[m]);
            den = ref_mul(den, (uint8_t)(xs[i] ^ xs[m]));
        }
        out_L[i] = ref_mul(num, ref_inv(den));
    }
}

/* ------------------------------------------------------------------- utils */
static uint64_t g_rng = 0x243F6A8885A308D3ull;   /* pi, for a fixed start */
static uint8_t rnd8(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return (uint8_t)(g_rng >> 24);
}
static void rnd_fill(uint8_t *p, size_t n) { while (n--) { *p++ = rnd8(); } }

static int bytes_eq(const uint8_t *a, const uint8_t *b, size_t n) {
    size_t i; for (i = 0; i < n; i++) { if (a[i] != b[i]) { return 0; } }
    return 1;
}

#define MAXK 255
#define RANDMAX ((size_t)CURZI_SS_BYTES * (MAXK - 1))

/* ============================================================== T0: field */
static void t0_field(void) {
    unsigned a, b;
    int mismatches = 0, inv_bad = 0;

    printf("T0  field GF(2^8)/0x11b vs independent reference\n");

    for (a = 0; a < 256; a++) {
        for (b = 0; b < 256; b++) {
            if (curzi_gf_mul((uint8_t)a, (uint8_t)b) != ref_mul((uint8_t)a, (uint8_t)b)) {
                mismatches++;
            }
        }
    }
    CHECK(mismatches == 0, "curzi_gf_mul disagrees with reference on %d of 65536 products",
          mismatches);

    /* x^254 == x^-1: exhaustive, all 255 nonzero elements. */
    for (a = 1; a < 256; a++) {
        uint8_t inv = curzi_gf_inv((uint8_t)a);
        if (inv != ref_inv((uint8_t)a)) { inv_bad++; }
        if (curzi_gf_mul((uint8_t)a, inv) != 1u) { inv_bad++; }
    }
    CHECK(inv_bad == 0, "curzi_gf_inv wrong on %d checks", inv_bad);

    /* Documented fail-safe: inv(0) == 0, branchlessly, never a trap. */
    CHECK(curzi_gf_inv(0) == 0, "inv(0) must be 0 (branchless fail-safe)");

    /* Field identities that a table-driven bug would still have to satisfy,
     * kept because they localise a failure fast. */
    for (a = 0; a < 256; a++) {
        CHECK(curzi_gf_mul((uint8_t)a, 1) == (uint8_t)a, "a*1 != a at a=%u", a);
        CHECK(curzi_gf_mul((uint8_t)a, 0) == 0, "a*0 != 0 at a=%u", a);
    }
    printf("    65536 products + 255 inverses cross-validated\n");
}

/* Reference evaluation of the share polynomial, using the randomness layout
 * curzi_shamir.c documents: coefficient of degree d for byte j lives at
 * randomness[(d-1)*32 + j], and c_0 = secret[j].
 *
 * WHY THIS TEST EXISTS. Round trip, threshold and equivocation ALL still pass
 * if split reuses one random byte for every degree (f_j(x) = s_j + r_j*(x + x^2
 * + ... + x^{k-1})) — yet that scheme is catastrophically broken: two shares
 * give two equations in two unknowns and recover the secret. No black-box test
 * over the public API catches it. Checking the evaluation identity directly
 * does, and it simultaneously pins the randomness layout that a third-party
 * reimplementation has to match. */
static uint8_t ref_eval(const uint8_t *secret, const uint8_t *rand_buf,
                        int k, int j, uint8_t x) {
    uint8_t acc = rand_buf[(size_t)(k - 2) * CURZI_SS_BYTES + (size_t)j];
    int d;
    for (d = k - 2; d >= 1; d--) {
        acc = (uint8_t)(ref_mul(acc, x) ^ rand_buf[(size_t)(d - 1) * CURZI_SS_BYTES + (size_t)j]);
    }
    return (uint8_t)(ref_mul(acc, x) ^ secret[j]);
}

/* ======================================================== T1: round trip */
static void combine_subset(const curzi_share_t *all, const int *pick, int k,
                           curzi_err_t *rc, uint8_t *out) {
    curzi_share_t sub[MAXK];
    int i, j;
    for (i = 0; i < k; i++) {
        sub[i].index = all[pick[i]].index;
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) { sub[i].y[j] = all[pick[i]].y[j]; }
    }
    *rc = curzi_combine(sub, (uint8_t)k, (uint8_t)k, out);
}

static void t1_roundtrip(void) {
    uint8_t secret[CURZI_SS_BYTES], out[CURZI_SS_BYTES];
    uint8_t rand_buf[RANDMAX];
    curzi_share_t shares[CURZI_N_INSTANCES];
    curzi_err_t rc;
    int pick[MAXK];
    int i, t, ok;
    const int N = (int)CURZI_N_INSTANCES;   /* 33 */
    const int K = (int)CURZI_K_THRESHOLD;   /* 17 */

    printf("T1  round trip, n=%d k=%d, many distinct k-subsets\n", N, K);

    rnd_fill(secret, sizeof secret);
    rnd_fill(rand_buf, (size_t)CURZI_SS_BYTES * (size_t)(K - 1));
    rc = curzi_split(secret, (uint8_t)N, (uint8_t)K, rand_buf,
                     (size_t)CURZI_SS_BYTES * (size_t)(K - 1), shares);
    CHECK(rc == CURZI_OK, "split returned %d", (int)rc);

    /* Evaluation identity: every emitted y is EXACTLY f_j(index) computed in
     * the independent reference field, over the documented coefficient layout.
     * Kills any degenerate coefficient reuse; see ref_eval. */
    {
        int bad = 0;
        for (i = 0; i < N; i++) {
            CHECK(shares[i].index == (uint8_t)(i + 1), "share %d index is %u, want %d",
                  i, shares[i].index, i + 1);
            for (t = 0; t < (int)CURZI_SS_BYTES; t++) {
                if (shares[i].y[t] != ref_eval(secret, rand_buf, K, t, shares[i].index)) {
                    bad++;
                }
            }
        }
        CHECK(bad == 0, "%d share bytes do not match f_j(x) over the documented layout", bad);

        /* and the k-1 coefficient positions are genuinely distinct storage:
         * perturbing ONE randomness byte must move only the bytes it feeds. */
        {
            curzi_share_t alt[CURZI_N_INSTANCES];
            int moved = 0, bled = 0;
            rand_buf[0] ^= 0xFFu;                       /* c_1 of byte 0 only */
            CHECK(curzi_split(secret, (uint8_t)N, (uint8_t)K, rand_buf,
                              (size_t)CURZI_SS_BYTES * (size_t)(K - 1), alt) == CURZI_OK,
                  "perturbed split");
            for (i = 0; i < N; i++) {
                if (alt[i].y[0] != shares[i].y[0]) { moved++; }
                for (t = 1; t < (int)CURZI_SS_BYTES; t++) {
                    if (alt[i].y[t] != shares[i].y[t]) { bled++; }
                }
            }
            rand_buf[0] ^= 0xFFu;
            CHECK(moved == N, "flipping c_1[byte 0] moved only %d/%d shares", moved, N);
            CHECK(bled == 0, "byte 0's coefficient bled into %d other secret bytes "
                             "(the 32 polynomials must be independent)", bled);
        }
        printf("    all %d x %u share bytes equal f_j(x) in the reference field; "
               "the 32 polynomials are independent\n", N, CURZI_SS_BYTES);
    }

    /* first 17 */
    for (i = 0; i < K; i++) { pick[i] = i; }
    combine_subset(shares, pick, K, &rc, out);
    CHECK(rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES), "first-17 subset");

    /* last 17 */
    for (i = 0; i < K; i++) { pick[i] = N - K + i; }
    combine_subset(shares, pick, K, &rc, out);
    CHECK(rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES), "last-17 subset");

    /* every other share: 0,2,...,32 — exactly 17 */
    for (i = 0; i < K; i++) { pick[i] = 2 * i; }
    combine_subset(shares, pick, K, &rc, out);
    CHECK(rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES), "even-index subset");

    /* odds plus the first: 0,1,3,5,...,31 — 17 */
    pick[0] = 0;
    for (i = 1; i < K; i++) { pick[i] = 2 * i - 1; }
    combine_subset(shares, pick, K, &rc, out);
    CHECK(rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES), "odd-index subset");

    /* 500 random 17-subsets by partial Fisher-Yates */
    ok = 0;
    for (t = 0; t < 500; t++) {
        int pool[CURZI_N_INSTANCES];
        for (i = 0; i < N; i++) { pool[i] = i; }
        for (i = 0; i < K; i++) {
            int r = i + (int)(rnd8() % (unsigned)(N - i));
            int tmp = pool[i]; pool[i] = pool[r]; pool[r] = tmp;
            pick[i] = pool[i];
        }
        combine_subset(shares, pick, K, &rc, out);
        if (rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES)) { ok++; }
    }
    CHECK(ok == 500, "only %d/500 random 17-subsets reconstructed", ok);
    printf("    504 distinct 17-of-33 subsets all reconstructed\n");

    /* parameter sweep, including the degenerate-but-legal edges k==2, k==n.
     * NOTE: local secret/randomness buffers on purpose. `secret` and `shares`
     * above are a matched pair that later checks in this function still rely
     * on; a sweep that re-randomised the shared `secret` would silently
     * invalidate every subsequent comparison against `shares`. (It did, once.) */
    {
        int kk, nn, bad = 0, cases = 0;
        uint8_t sweep_secret[CURZI_SS_BYTES], sweep_rand[RANDMAX];
        for (kk = 2; kk <= 12; kk++) {
            for (nn = kk; nn <= 24; nn++) {
                curzi_share_t sh[24];
                size_t need = (size_t)CURZI_SS_BYTES * (size_t)(kk - 1);
                rnd_fill(sweep_secret, sizeof sweep_secret);
                rnd_fill(sweep_rand, need);
                if (curzi_split(sweep_secret, (uint8_t)nn, (uint8_t)kk, sweep_rand, need, sh)
                    != CURZI_OK) { bad++; continue; }
                /* the top k shares, i.e. not the ones split emitted first */
                for (i = 0; i < kk; i++) { pick[i] = nn - kk + i; }
                combine_subset(sh, pick, kk, &rc, out);
                cases++;
                if (rc != CURZI_OK || !bytes_eq(out, sweep_secret, CURZI_SS_BYTES)) { bad++; }
            }
        }
        CHECK(bad == 0, "%d failures over %d (n,k) parameter cases", bad, cases);
        printf("    %d (n,k) parameter combinations, k=2..12, n=k..24\n", cases);
    }

    /* count > k: combine consumes EXACTLY the first k shares, never all `count`.
     * The discriminating case is a share corrupted BEYOND the first k. If the
     * implementation interpolated all `count` points, that corruption would
     * poison the result; because it interpolates k, the extra share is never
     * read and the secret still comes back. A plain uncorrupted count=33 run
     * cannot tell the two apart — 33 honest points on a degree-16 polynomial
     * interpolate to the same f(0) — so the corruption is the test. */
    {
        curzi_share_t many[CURZI_N_INSTANCES];
        for (i = 0; i < N; i++) { many[i] = shares[i]; }

        rc = curzi_combine(many, (uint8_t)N, (uint8_t)K, out);
        CHECK(rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES),
              "count=33 k=17 with all-honest shares");

        many[20].y[0] ^= 0xFFu;          /* position 20 > k-1 = 16 */
        many[31].y[7] ^= 0x01u;
        rc = curzi_combine(many, (uint8_t)N, (uint8_t)K, out);
        CHECK(rc == CURZI_OK && bytes_eq(out, secret, CURZI_SS_BYTES),
              "shares beyond the first k must not affect the result");

        /* and the converse, so the above is not passing for a trivial reason:
         * corrupting a share INSIDE the first k does change the answer. */
        for (i = 0; i < N; i++) { many[i] = shares[i]; }
        many[3].y[0] ^= 0xFFu;
        rc = curzi_combine(many, (uint8_t)N, (uint8_t)K, out);
        CHECK(rc == CURZI_OK && !bytes_eq(out, secret, CURZI_SS_BYTES),
              "corrupting a share inside the first k must change the result");
        printf("    count>k uses exactly the first k (corruption beyond k ignored)\n");
    }

    /* n = 255 exercises share index 255, the top of the legal abscissa range.
     * Local buffers, same reason as the sweep above. */
    {
        static curzi_share_t big[255];
        uint8_t big_secret[CURZI_SS_BYTES], big_rand[RANDMAX];
        size_t need = (size_t)CURZI_SS_BYTES * 2u;   /* k = 3 */
        rnd_fill(big_secret, sizeof big_secret);
        rnd_fill(big_rand, need);
        rc = curzi_split(big_secret, 255u, 3u, big_rand, need, big);
        CHECK(rc == CURZI_OK, "split n=255 k=3 returned %d", (int)rc);
        CHECK(big[254].index == 255u, "top index is %u, expected 255", big[254].index);
        pick[0] = 254; pick[1] = 0; pick[2] = 128;
        combine_subset(big, pick, 3, &rc, out);
        CHECK(rc == CURZI_OK && bytes_eq(out, big_secret, CURZI_SS_BYTES),
              "n=255 reconstruction using index 255");
        printf("    n=255 (share index 255) reconstructed\n");
    }
}

/* ==================================================== T2: k-1 is not enough */
static void t2_threshold(void) {
    uint8_t secret[CURZI_SS_BYTES], out[CURZI_SS_BYTES];
    uint8_t rand_buf[RANDMAX];
    curzi_share_t shares[CURZI_N_INSTANCES];
    curzi_share_t sub[CURZI_K_THRESHOLD];
    curzi_err_t rc;
    int i, j, t, wrong = 0, trials = 0;
    const int N = (int)CURZI_N_INSTANCES;
    const int K = (int)CURZI_K_THRESHOLD;

    printf("T2  k-1 = %d shares reconstruct a WRONG secret\n", K - 1);

    rnd_fill(secret, sizeof secret);
    rnd_fill(rand_buf, (size_t)CURZI_SS_BYTES * (size_t)(K - 1));
    rc = curzi_split(secret, (uint8_t)N, (uint8_t)K, rand_buf,
                     (size_t)CURZI_SS_BYTES * (size_t)(K - 1), shares);
    CHECK(rc == CURZI_OK, "split returned %d", (int)rc);

    /* (a) asking for k=17 with only 16 shares is refused outright. */
    for (i = 0; i < K - 1; i++) { sub[i] = shares[i]; }
    for (j = 0; j < (int)CURZI_SS_BYTES; j++) { out[j] = 0xA5u; }
    rc = curzi_combine(sub, (uint8_t)(K - 1), (uint8_t)K, out);
    CHECK(rc == CURZI_E_THRESHOLD, "16 shares with k=17 gave %d, want CURZI_E_THRESHOLD",
          (int)rc);
    {
        int untouched = 1;
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) { if (out[j] != 0xA5u) { untouched = 0; } }
        CHECK(untouched, "output buffer was written on a refused combine");
    }

    /* (b) the attacker who instead GUESSES the threshold is 16 and interpolates
     * a degree-15 polynomial through 16 points of a degree-16 one. That call is
     * legal and returns CURZI_OK — and the answer is wrong. This is the honest
     * form of the claim: not "it errors", but "it yields nothing about the
     * secret". Collision would need all 32 bytes to match by chance, ~2^-256. */
    for (t = 0; t < 400; t++) {
        int pool[CURZI_N_INSTANCES];
        for (i = 0; i < N; i++) { pool[i] = i; }
        for (i = 0; i < K - 1; i++) {
            int r = i + (int)(rnd8() % (unsigned)(N - i));
            int tmp = pool[i]; pool[i] = pool[r]; pool[r] = tmp;
            sub[i] = shares[pool[i]];
        }
        rc = curzi_combine(sub, (uint8_t)(K - 1), (uint8_t)(K - 1), out);
        trials++;
        if (rc == CURZI_OK && !bytes_eq(out, secret, CURZI_SS_BYTES)) { wrong++; }
    }
    CHECK(wrong == trials, "%d of %d 16-subsets reproduced the real secret", trials - wrong,
          trials);
    printf("    %d random 16-subsets interpolated: all %d wrong\n", trials, wrong);
}

/* ======================= T3: k-1 shares are consistent with EVERY secret */
/* The strong statement from curzi8889a.h LAYER 2: "16 shares are consistent
 * with every possible secret." We do not assert it — we exhibit the witnesses.
 *
 * Take the 16 real shares at indices 1..16. Pick any unused abscissa X. For a
 * TARGET secret T, the 17-point Lagrange reconstruction is
 *     T = XOR_{i<16} L_i * y_i  ^  L_X * y_X
 * and L_X != 0 (a product of nonzero field elements), so
 *     y_X = L_X^-1 * (T ^ XOR_{i<16} L_i * y_i)
 * always exists and is unique. So for EVERY T there is a 17th share that makes
 * the same 16 shares reconstruct T. The 16 shares therefore constrain nothing.
 *
 * All the forging arithmetic uses the independent reference field, so this is
 * an outside attacker's computation, not the implementation checking itself. */
static void t3_equivocation(void) {
    uint8_t secret[CURZI_SS_BYTES];
    uint8_t rand_buf[RANDMAX];
    curzi_share_t shares[CURZI_N_INSTANCES];
    curzi_share_t forged[CURZI_K_THRESHOLD];
    uint8_t xs[CURZI_K_THRESHOLD], L[CURZI_K_THRESHOLD];
    uint8_t target[CURZI_SS_BYTES], out[CURZI_SS_BYTES];
    curzi_err_t rc;
    const int N = (int)CURZI_N_INSTANCES;
    const int K = (int)CURZI_K_THRESHOLD;
    const uint8_t X = 200u;   /* unused abscissa: shares run 1..33 */
    int i, j, v, reached;

    printf("T3  any %d shares are consistent with EVERY secret (exhibited)\n", K - 1);

    rnd_fill(secret, sizeof secret);
    rnd_fill(rand_buf, (size_t)CURZI_SS_BYTES * (size_t)(K - 1));
    rc = curzi_split(secret, (uint8_t)N, (uint8_t)K, rand_buf,
                     (size_t)CURZI_SS_BYTES * (size_t)(K - 1), shares);
    CHECK(rc == CURZI_OK, "split returned %d", (int)rc);

    for (i = 0; i < K - 1; i++) { forged[i] = shares[i]; xs[i] = shares[i].index; }
    forged[K - 1].index = X;
    xs[K - 1] = X;
    ref_lagrange0(xs, K, L);
    CHECK(L[K - 1] != 0, "L_X must be nonzero for the forgery to exist");

    /* (a) two arbitrary, completely different full 32-byte targets. */
    for (v = 0; v < 2; v++) {
        uint8_t linv = ref_inv(L[K - 1]);
        rnd_fill(target, sizeof target);
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) {
            uint8_t acc = 0;
            for (i = 0; i < K - 1; i++) { acc ^= ref_mul(L[i], forged[i].y[j]); }
            forged[K - 1].y[j] = ref_mul(linv, (uint8_t)(target[j] ^ acc));
        }
        rc = curzi_combine(forged, (uint8_t)K, (uint8_t)K, out);
        CHECK(rc == CURZI_OK && bytes_eq(out, target, CURZI_SS_BYTES),
              "target %d not reachable from the same 16 shares", v);
        CHECK(!bytes_eq(target, secret, CURZI_SS_BYTES),
              "test bug: forged target coincided with the real secret");
    }

    /* (b) exhaustive over one byte: all 256 values of secret byte 0 are
     * reachable from the SAME 16 shares. That is the whole information-theoretic
     * claim, made concrete. */
    reached = 0;
    for (v = 0; v < 256; v++) {
        uint8_t linv = ref_inv(L[K - 1]);
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) { target[j] = secret[j]; }
        target[0] = (uint8_t)v;
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) {
            uint8_t acc = 0;
            for (i = 0; i < K - 1; i++) { acc ^= ref_mul(L[i], forged[i].y[j]); }
            forged[K - 1].y[j] = ref_mul(linv, (uint8_t)(target[j] ^ acc));
        }
        rc = curzi_combine(forged, (uint8_t)K, (uint8_t)K, out);
        if (rc == CURZI_OK && bytes_eq(out, target, CURZI_SS_BYTES)) { reached++; }
    }
    CHECK(reached == 256, "only %d/256 values of secret byte 0 were reachable", reached);
    printf("    2 full-width targets + all 256 values of byte 0 reachable "
           "from one fixed 16-share set\n");
}

/* ======================================================== T4: refusals */
static void t4_refusals(void) {
    uint8_t secret[CURZI_SS_BYTES], out[CURZI_SS_BYTES];
    uint8_t rand_buf[RANDMAX];
    curzi_share_t shares[CURZI_N_INSTANCES];
    curzi_share_t sub[CURZI_K_THRESHOLD + 2];
    curzi_err_t rc;
    const int N = (int)CURZI_N_INSTANCES;
    const int K = (int)CURZI_K_THRESHOLD;
    size_t need = (size_t)CURZI_SS_BYTES * (size_t)(K - 1);
    int i, j;

    printf("T4  refusals: exact error codes, output left untouched\n");

    rnd_fill(secret, sizeof secret);
    rnd_fill(rand_buf, need);
    rc = curzi_split(secret, (uint8_t)N, (uint8_t)K, rand_buf, need, shares);
    CHECK(rc == CURZI_OK, "split returned %d", (int)rc);

    /* --- split --- */
    CHECK(curzi_split(0, 33, 17, rand_buf, need, shares) == CURZI_E_NULL, "split NULL secret");
    CHECK(curzi_split(secret, 33, 17, 0, need, shares) == CURZI_E_NULL, "split NULL randomness");
    CHECK(curzi_split(secret, 33, 17, rand_buf, need, 0) == CURZI_E_NULL, "split NULL out");
    CHECK(curzi_split(secret, 33, 1, rand_buf, need, shares) == CURZI_E_THRESHOLD,
          "split k=1 must be refused: every holder would have the secret");
    CHECK(curzi_split(secret, 33, 0, rand_buf, need, shares) == CURZI_E_THRESHOLD, "split k=0");
    CHECK(curzi_split(secret, 16, 17, rand_buf, need, shares) == CURZI_E_THRESHOLD,
          "split n<k must be refused: those shares could never combine");
    CHECK(curzi_split(secret, 0, 17, rand_buf, need, shares) == CURZI_E_THRESHOLD, "split n=0");
    CHECK(curzi_split(secret, 33, 17, rand_buf, need - 1, shares) == CURZI_E_NULL,
          "split with one byte too little randomness must be refused, not stretched");
    CHECK(curzi_split(secret, 33, 17, rand_buf, 0, shares) == CURZI_E_NULL, "split rand_len=0");
    /* boundary: exactly enough is enough */
    CHECK(curzi_split(secret, 33, 17, rand_buf, need, shares) == CURZI_OK,
          "split with exactly 32*(k-1) bytes must succeed");

    /* split must not have written anything on a refused call */
    {
        curzi_share_t canary[CURZI_N_INSTANCES];
        int untouched = 1;
        for (i = 0; i < N; i++) {
            canary[i].index = 0x5Au;
            for (j = 0; j < (int)CURZI_SS_BYTES; j++) { canary[i].y[j] = 0x5Au; }
        }
        (void)curzi_split(secret, 16, 17, rand_buf, need, canary);
        for (i = 0; i < N; i++) {
            if (canary[i].index != 0x5Au) { untouched = 0; }
            for (j = 0; j < (int)CURZI_SS_BYTES; j++) {
                if (canary[i].y[j] != 0x5Au) { untouched = 0; }
            }
        }
        CHECK(untouched, "split wrote shares on a refused call (partial result)");
    }

    /* --- combine --- */
    for (i = 0; i < K; i++) { sub[i] = shares[i]; }

    CHECK(curzi_combine(0, (uint8_t)K, (uint8_t)K, out) == CURZI_E_NULL, "combine NULL shares");
    CHECK(curzi_combine(sub, (uint8_t)K, (uint8_t)K, 0) == CURZI_E_NULL, "combine NULL out");
    CHECK(curzi_combine(sub, (uint8_t)K, 1, out) == CURZI_E_THRESHOLD, "combine k=1");
    CHECK(curzi_combine(sub, (uint8_t)K, 0, out) == CURZI_E_THRESHOLD, "combine k=0");
    CHECK(curzi_combine(sub, (uint8_t)(K - 1), (uint8_t)K, out) == CURZI_E_THRESHOLD,
          "combine count<k");

    /* index 0 IS the secret (f(0)); presenting it as a share is refused. */
    for (i = 0; i < K; i++) { sub[i] = shares[i]; }
    sub[0].index = 0;
    for (j = 0; j < (int)CURZI_SS_BYTES; j++) { out[j] = 0xC3u; }
    rc = curzi_combine(sub, (uint8_t)K, (uint8_t)K, out);
    CHECK(rc == CURZI_E_SHARE_INDEX, "index 0 at position 0 gave %d", (int)rc);
    {
        int untouched = 1;
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) { if (out[j] != 0xC3u) { untouched = 0; } }
        CHECK(untouched, "combine wrote output on a refused (index 0) call");
    }

    /* also caught when it is NOT among the first k used for interpolation */
    for (i = 0; i < K + 2; i++) { sub[i] = shares[i]; }
    sub[K + 1].index = 0;
    CHECK(curzi_combine(sub, (uint8_t)(K + 2), (uint8_t)K, out) == CURZI_E_SHARE_INDEX,
          "index 0 beyond the first k must still be refused");

    /* duplicate abscissa: the Vandermonde system is singular. */
    for (i = 0; i < K; i++) { sub[i] = shares[i]; }
    sub[5].index = sub[2].index;
    for (j = 0; j < (int)CURZI_SS_BYTES; j++) { out[j] = 0xC3u; }
    rc = curzi_combine(sub, (uint8_t)K, (uint8_t)K, out);
    CHECK(rc == CURZI_E_SHARE_DUP, "duplicate index gave %d", (int)rc);
    {
        int untouched = 1;
        for (j = 0; j < (int)CURZI_SS_BYTES; j++) { if (out[j] != 0xC3u) { untouched = 0; } }
        CHECK(untouched, "combine wrote output on a refused (duplicate) call");
    }

    /* duplicate outside the first k is still a malformed set */
    for (i = 0; i < K + 2; i++) { sub[i] = shares[i]; }
    sub[K + 1].index = sub[0].index;
    CHECK(curzi_combine(sub, (uint8_t)(K + 2), (uint8_t)K, out) == CURZI_E_SHARE_DUP,
          "duplicate beyond the first k must still be refused");

    /* documented precedence: index-0 outranks duplicate when both are present */
    for (i = 0; i < K; i++) { sub[i] = shares[i]; }
    sub[3].index = sub[1].index;   /* duplicate */
    sub[7].index = 0;              /* and an index 0 */
    CHECK(curzi_combine(sub, (uint8_t)K, (uint8_t)K, out) == CURZI_E_SHARE_INDEX,
          "index-0 must take precedence over duplicate");
    printf("    every refusal returns its documented code, output untouched\n");
}

/* ==================================================== T5: determinism */
static void t5_determinism(void) {
    uint8_t secret[CURZI_SS_BYTES], out1[CURZI_SS_BYTES], out2[CURZI_SS_BYTES];
    uint8_t r1[RANDMAX], r2[RANDMAX];
    curzi_share_t a[CURZI_N_INSTANCES], b[CURZI_N_INSTANCES], c[CURZI_N_INSTANCES];
    curzi_err_t rc;
    const int N = (int)CURZI_N_INSTANCES;
    const int K = (int)CURZI_K_THRESHOLD;
    size_t need = (size_t)CURZI_SS_BYTES * (size_t)(K - 1);
    int i, j, identical = 1, differs = 0;

    printf("T5  determinism: same randomness -> byte-identical shares\n");

    rnd_fill(secret, sizeof secret);
    rnd_fill(r1, need);
    for (i = 0; i < (int)need; i++) { r2[i] = r1[i]; }

    CHECK(curzi_split(secret, (uint8_t)N, (uint8_t)K, r1, need, a) == CURZI_OK, "split a");
    CHECK(curzi_split(secret, (uint8_t)N, (uint8_t)K, r2, need, b) == CURZI_OK, "split b");

    for (i = 0; i < N; i++) {
        if (a[i].index != b[i].index) { identical = 0; }
        if (!bytes_eq(a[i].y, b[i].y, CURZI_SS_BYTES)) { identical = 0; }
    }
    CHECK(identical, "same randomness produced different shares");

    /* Different randomness must produce different shares (else the coefficients
     * are not actually being consumed) while still recovering the same secret. */
    rnd_fill(r2, need);
    CHECK(curzi_split(secret, (uint8_t)N, (uint8_t)K, r2, need, c) == CURZI_OK, "split c");
    for (i = 0; i < N; i++) {
        if (!bytes_eq(a[i].y, c[i].y, CURZI_SS_BYTES)) { differs++; }
    }
    CHECK(differs == N, "only %d/%d shares changed when the randomness changed", differs, N);

    rc = curzi_combine(a, (uint8_t)K, (uint8_t)K, out1);
    CHECK(rc == CURZI_OK, "combine a");
    rc = curzi_combine(c, (uint8_t)K, (uint8_t)K, out2);
    CHECK(rc == CURZI_OK, "combine c");
    CHECK(bytes_eq(out1, secret, CURZI_SS_BYTES) && bytes_eq(out2, secret, CURZI_SS_BYTES),
          "independent splits of one secret must both recover it");

    /* Explicit KAT-shaped vector: all-zero secret, all-zero randomness. Every
     * coefficient is 0, so every polynomial is identically 0 and every share
     * y-value must be 0 — a fixed, hand-checkable value that pins the Horner
     * order and the randomness layout for a third-party reimplementation. */
    {
        uint8_t zs[CURZI_SS_BYTES], zr[RANDMAX];
        int allzero = 1;
        for (i = 0; i < (int)CURZI_SS_BYTES; i++) { zs[i] = 0; }
        for (i = 0; i < (int)need; i++) { zr[i] = 0; }
        CHECK(curzi_split(zs, (uint8_t)N, (uint8_t)K, zr, need, a) == CURZI_OK, "split zero");
        for (i = 0; i < N; i++) {
            if (a[i].index != (uint8_t)(i + 1)) { allzero = 0; }
            for (j = 0; j < (int)CURZI_SS_BYTES; j++) { if (a[i].y[j] != 0) { allzero = 0; } }
        }
        CHECK(allzero, "zero secret + zero randomness must give index=i+1 and y=0");
    }

    /* Second vector: secret = 0x01..0x20, randomness = 0x01 everywhere, k=2.
     * f_j(x) = secret[j] + 1*x, so y_j(x) = secret[j] ^ x. Hand-computable. */
    {
        uint8_t s2[CURZI_SS_BYTES], r3[CURZI_SS_BYTES];
        curzi_share_t d[4];
        int bad = 0;
        for (i = 0; i < (int)CURZI_SS_BYTES; i++) { s2[i] = (uint8_t)(i + 1); r3[i] = 1u; }
        CHECK(curzi_split(s2, 4, 2, r3, CURZI_SS_BYTES, d) == CURZI_OK, "split k=2 vector");
        for (i = 0; i < 4; i++) {
            for (j = 0; j < (int)CURZI_SS_BYTES; j++) {
                if (d[i].y[j] != (uint8_t)(s2[j] ^ (uint8_t)(i + 1))) { bad++; }
            }
        }
        CHECK(bad == 0, "k=2 hand-computed vector mismatched in %d bytes", bad);
    }
    printf("    identical seeds match; changed seeds move all %d shares; "
           "2 hand-computable vectors hold\n", N);
}

/* ------------------------------------------------------------------- main */
int main(void) {
    printf("CURZI-8889-A LAYER 2 — Shamir k-of-n over GF(2^8)\n");
    printf("=================================================\n");
    t0_field();
    t1_roundtrip();
    t2_threshold();
    t3_equivocation();
    t4_refusals();
    t5_determinism();
    printf("=================================================\n");
    if (g_fail == 0) {
        printf("PASS  %d checks, 0 failures\n", g_checks);
        return 0;
    }
    printf("FAIL  %d of %d checks failed\n", g_fail, g_checks);
    return 1;
}
