/* curzi_shamir.c — CURZI-8889-A LAYER 2: Shamir k-of-n over GF(2^8).
 * See curzi8889a.h. The header is the contract; this file does not extend it.
 *
 * WHAT LAYER 2 HAS TO BE TRUE FOR, OR IT IS WORTHLESS
 * ---------------------------------------------------
 * The header stakes a falsifiable claim: "any k-1 = 16 compromised instances
 * learn NOTHING. Not 'less'; nothing." That is an INFORMATION-THEORETIC claim,
 * and it survives only if two things hold in the code, not just in the prose:
 *
 *   1. The k-1 non-constant coefficients are UNIFORM and INDEPENDENT of the
 *      secret. Then for any k-1 distinct nonzero abscissae, the system of k-1
 *      equations in the k-1 unknown coefficients has a nonsingular
 *      Vandermonde-type matrix (determinant = (prod x_i) * Vandermonde(x_i),
 *      nonzero because the x_i are distinct AND nonzero). So for EVERY one of
 *      the 256 candidate values of a secret byte there is EXACTLY ONE
 *      coefficient vector consistent with the observed k-1 shares. Uniform
 *      coefficients therefore induce a uniform posterior on the secret.
 *      test_curzi_shamir.c T3 exhibits all 256 witnesses rather than asserting
 *      this.
 *
 *   2. Nothing else leaks the secret through a side channel. Hence the
 *      constant-time discipline below.
 *
 * THE "FIX" THAT WOULD BREAK IT. It is a standing temptation to force the
 * LEADING coefficient c_{k-1} nonzero, on the reasoning that a zero leading
 * coefficient drops the degree and lets k-1 shares suffice. That reasoning is
 * wrong and the fix is a real leak: rejecting c_{k-1} = 0 makes the coefficient
 * distribution non-uniform, which destroys premise 1 above and hands the
 * attacker a genuine bias. The degenerate case is not a hole; it is one of the
 * 256^(k-1) equally likely draws, and it is already counted in the argument.
 * We therefore consume randomness verbatim. Do not "harden" this.
 *
 * CONSTANT TIME — WHERE, AND WHERE NOT (stated so it can be audited)
 * ------------------------------------------------------------------
 * ACHIEVED, on everything secret:
 *   - curzi_gf_mul: 8 fixed iterations, branchless 0x00/0xFF masks, no memory
 *     indexed by an operand. Specifically NOT a log/antilog table: those index
 *     256-byte tables with secret bytes, which is a cache-timing oracle on the
 *     secret and on every share value. That is the single most common way this
 *     exact primitive is broken.
 *   - curzi_gf_inv: fixed 11-step addition chain, no branch, no table.
 *   - curzi_split / curzi_combine: every loop bound is n, k, count or
 *     CURZI_SS_BYTES; every array index is a loop counter. No branch anywhere
 *     depends on a secret byte or on a coefficient.
 *
 * NOT ACHIEVED, and it does not need to be:
 *   - Validation branches on SHARE INDICES and on n/k/count. Share indices are
 *     public metadata in Shamir (a shareholder's identity, not its secret), and
 *     n/k/count are public parameters. This is an explicit assumption: a caller
 *     that treats an index as secret is outside this file's model.
 *   - curzi_gf_inv is invoked only on a denominator built from indices, i.e. on
 *     public data. It is written constant-time regardless, because a primitive
 *     that is only conditionally safe eventually gets called from the other
 *     condition.
 *
 * FAIL CLOSED. 0 is the only success. On any error the caller's output buffer
 * is left BYTE-FOR-BYTE UNTOUCHED: split validates fully before its first
 * write, and combine interpolates into a stack buffer and copies out only after
 * the last check passes. A half-written share set is indistinguishable from a
 * real one at the type level, so there must never be one.
 *
 * Freestanding: integer only, no libc, no allocation, no float, no 64-bit
 * variable divide.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (CURZI-8889-A threshold slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "curzi8889a.h"

/* ---- exposure note ------------------------------------------------------ *
 * curzi_gf_mul / curzi_gf_inv are non-static and deliberately absent from
 * curzi8889a.h: the header is the standard's surface and stays untouched.
 * They carry external linkage for exactly one reason — test_curzi_shamir.c
 * cross-checks them EXHAUSTIVELY (all 65536 products, all 256 inverses)
 * against an independently written reference (full carry-less product, then
 * explicit polynomial long division by 0x11b). A field defect that is
 * self-consistent — a wrong-but-uniform reduction polynomial, say — round-trips
 * through split/combine perfectly and is caught by NOTHING at the public API.
 * The only way to catch it is to compare the field against a second
 * construction, and that requires a name. */
uint8_t curzi_gf_mul(uint8_t a, uint8_t b);
uint8_t curzi_gf_inv(uint8_t a);

/* GF(2^8) reduction polynomial x^8 + x^4 + x^3 + x + 1 == 0x11b (AES / FIPS
 * 197 §4.2). Canonical, not chosen here: the no-backdoor argument in
 * curzi8889a.h requires every constant to be recomputable from a published
 * definition, and this one is. The low 8 bits, 0x1b, are what remains after the
 * x^8 term is consumed by the shift-out. */
#define CURZI_GF_REDUCE 0x1bu

/* ---- helpers ------------------------------------------------------------ */

/* 0x00 / 0xFF mask from a 0/1 bit. Unsigned wraparound is defined behaviour;
 * this is the branch that is not there. */
static uint8_t curzi_mask8(uint8_t bit) {
    return (uint8_t)(0u - (unsigned)(bit & 1u));
}

/* No libc. Wipe through a volatile pointer so the store cannot be elided as
 * dead: the buffers being cleared hold reconstructed secret material and
 * Lagrange intermediates. */
static void curzi_wipe(uint8_t *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n-- > 0u) { *v++ = 0u; }
}

/* ---- field -------------------------------------------------------------- */

/* Carry-less shift-and-reduce ("Russian peasant") multiply in GF(2^8).
 *
 * Constant time by construction: exactly 8 iterations always, both conditionals
 * replaced by masks, no array is touched at all — so there is no cache line
 * whose residency depends on a or b, and no branch whose direction does.
 * Both operands may be secret (share y-values are). */
uint8_t curzi_gf_mul(uint8_t a, uint8_t b) {
    uint8_t p = 0u;
    unsigned i;

    for (i = 0u; i < 8u; i++) {
        /* accumulate a * (bit i of b) */
        p ^= (uint8_t)(a & curzi_mask8((uint8_t)(b >> i)));

        /* a <<= 1 in the field: shift, then conditionally fold in 0x1b when the
         * x^7 term was promoted to x^8 and shifted out. */
        {
            uint8_t carry = curzi_mask8((uint8_t)(a >> 7));
            a = (uint8_t)((unsigned)a << 1);
            a ^= (uint8_t)(CURZI_GF_REDUCE & carry);
        }
    }
    return p;
}

/* Multiplicative inverse by exponentiation: x^254 == x^-1 in GF(2^8).
 *
 * WHY 254. The nonzero elements form a cyclic group of order 2^8 - 1 = 255, so
 * by Lagrange's theorem x^255 == 1 for every x != 0, hence x^254 == x^-1.
 * The exponent is a fixed public constant, so exponentiation leaks nothing
 * about x, unlike a table lookup at index x.
 *
 * FAIL-SAFE AT ZERO, WITHOUT A BRANCH. 0^254 == 0, so this returns 0 for 0
 * rather than trapping or taking a different path. That is deliberate: a
 * `if (a == 0)` guard would be a data-dependent branch, and callers here never
 * invert zero anyway (curzi_combine has already proved every difference of
 * indices is nonzero by rejecting duplicates before it gets here). Returning 0
 * makes the degenerate case loud-but-safe: it yields a wrong answer, never a
 * secret-dependent timing signal.
 *
 * Addition chain (11 multiplies, fixed schedule — the standard AES inverter):
 *   x^2   = x*x            x^3  = x^2 * x        x^12 = (x^3)^4
 *   x^14  = x^12 * x^2     x^15 = x^12 * x^3     x^240 = (x^15)^16
 *   x^254 = x^240 * x^14
 * Check: 240 + 14 = 254. */
uint8_t curzi_gf_inv(uint8_t a) {
    uint8_t x2, x3, x12, x14, x15, x240;

    x2   = curzi_gf_mul(a, a);            /* x^2                     */
    x3   = curzi_gf_mul(x2, a);           /* x^3                     */
    x12  = curzi_gf_mul(x3, x3);          /* x^6                     */
    x12  = curzi_gf_mul(x12, x12);        /* x^12                    */
    x14  = curzi_gf_mul(x12, x2);         /* x^14                    */
    x15  = curzi_gf_mul(x12, x3);         /* x^15                    */
    x240 = curzi_gf_mul(x15, x15);        /* x^30                    */
    x240 = curzi_gf_mul(x240, x240);      /* x^60                    */
    x240 = curzi_gf_mul(x240, x240);      /* x^120                   */
    x240 = curzi_gf_mul(x240, x240);      /* x^240                   */
    return curzi_gf_mul(x240, x14);       /* x^254 == x^-1           */
}

/* ---- LAYER 2: split ----------------------------------------------------- */

/* Randomness layout, fixed so that split is a pure function of its inputs
 * (curzi8889a.h: "every derivation is deterministic and covered by test
 * vectors"). Coefficient of degree d for secret byte j is
 *     randomness[(d-1) * CURZI_SS_BYTES + j],   d = 1 .. k-1
 * so exactly CURZI_SS_BYTES * (k-1) bytes are consumed, in order, and a third
 * party recomputes the share set byte-for-byte from the same seed. */
#define CURZI_RAND_NEED(k) ((size_t)CURZI_SS_BYTES * (size_t)((k) - 1u))

curzi_err_t curzi_split(const uint8_t secret[CURZI_SS_BYTES],
                        uint8_t n, uint8_t k,
                        const uint8_t *randomness, size_t rand_len,
                        curzi_share_t *out_shares) {
    unsigned i, j, d;

    /* --- validate EVERYTHING before the first write. Fail closed. --- */
    if (secret == 0 || out_shares == 0 || randomness == 0) {
        return CURZI_E_NULL;
    }
    /* k < 2 is not a threshold scheme: k == 1 hands the secret to every single
     * holder, k == 0 is meaningless. Refused rather than clamped, because a
     * clamp would silently produce a share set that looks like a real one.
     *
     * THIS GUARD IS ALSO MEMORY SAFETY, NOT ONLY POLICY. The Horner seed below
     * indexes randomness at ((size_t)k - 2u) * CURZI_SS_BYTES. For k < 2 that
     * subtraction wraps to a huge size_t and the read runs off the buffer —
     * measured: deleting this check crashes the test suite with SIGBUS rather
     * than failing an assertion. Do not reorder it after the randomness-length
     * check, and do not relax it to k < 1. */
    if (k < 2u) {
        return CURZI_E_THRESHOLD;
    }
    /* n < k is unrecoverable by construction: the shares could never be
     * combined. Issuing them anyway is a guaranteed loss of the secret. */
    if (n < k) {
        return CURZI_E_THRESHOLD;
    }
    /* Short randomness is refused, not stretched, not padded, not re-used.
     * The enum has no dedicated code for it; CURZI_E_NULL is the conservative
     * reading — a buffer that cannot supply the required entropy is, for this
     * purpose, no randomness at all. Silently reusing bytes would correlate
     * coefficients across degrees and void the secrecy argument above. */
    if (rand_len < CURZI_RAND_NEED(k)) {
        return CURZI_E_NULL;
    }

    /* --- emit --- */
    for (i = 0u; i < (unsigned)n; i++) {
        /* Index i+1, never 0: f(0) IS the secret (curzi8889a.h). n <= 255 is
         * enforced by the uint8_t parameter, so x stays in 1..255 and every
         * index is distinct by construction. */
        uint8_t x = (uint8_t)(i + 1u);
        out_shares[i].index = x;

        for (j = 0u; j < CURZI_SS_BYTES; j++) {
            /* Horner from the top coefficient down. Fixed trip count k-1 for
             * every byte and every share; no operand steers control flow. */
            uint8_t acc = randomness[((size_t)k - 2u) * CURZI_SS_BYTES + j];

            for (d = (unsigned)k - 2u; d >= 1u; d--) {
                acc = (uint8_t)(curzi_gf_mul(acc, x)
                                ^ randomness[((size_t)d - 1u) * CURZI_SS_BYTES + j]);
            }
            /* final step folds in the constant term c_0 = secret[j] */
            acc = (uint8_t)(curzi_gf_mul(acc, x) ^ secret[j]);
            out_shares[i].y[j] = acc;
        }
    }
    return CURZI_OK;
}

/* ---- LAYER 2: combine --------------------------------------------------- */

curzi_err_t curzi_combine(const curzi_share_t *shares, uint8_t count,
                          uint8_t k, uint8_t out_secret[CURZI_SS_BYTES]) {
    /* Bounded by the uint8_t index space; no allocation. 255 * 1 byte of
     * Lagrange coefficients plus a 32-byte staging buffer and a 256-bit
     * duplicate bitmap is the entire working set. */
    uint8_t basis[255];
    uint8_t tmp[CURZI_SS_BYTES];
    uint32_t seen[8];
    unsigned i, m, j;
    curzi_err_t rc;

    if (shares == 0 || out_secret == 0) {
        return CURZI_E_NULL;
    }
    if (k < 2u) {
        return CURZI_E_THRESHOLD;
    }
    if (count < k) {
        return CURZI_E_THRESHOLD;
    }

    /* Validation order is FIXED and part of the contract's observable
     * behaviour: index-0 is scanned across the whole supplied set first, then
     * duplicates. A set containing both faults reports CURZI_E_SHARE_INDEX,
     * because presenting f(0) is the graver claim — it is the secret itself
     * being passed off as a share, not merely a malformed set.
     *
     * Both scans run over ALL `count` supplied shares, not only the k that will
     * be used. A malformed share the caller happened to place at position k+1
     * is still a malformed share, and accepting it teaches the caller that its
     * share plumbing is sound when it is not. */
    for (i = 0u; i < (unsigned)count; i++) {
        if (shares[i].index == 0u) {
            return CURZI_E_SHARE_INDEX;
        }
    }

    /* Duplicate detection via a 256-bit bitmap. Indexing by share INDEX is
     * safe here and only here: indices are public metadata (see the header
     * comment). Nothing in this function ever indexes memory with a y-byte. */
    for (i = 0u; i < 8u; i++) { seen[i] = 0u; }
    for (i = 0u; i < (unsigned)count; i++) {
        unsigned x  = (unsigned)shares[i].index;
        uint32_t bit = (uint32_t)1u << (x & 31u);
        if ((seen[x >> 5] & bit) != 0u) {
            /* Duplicated abscissa: the Vandermonde system is singular, so the
             * "reconstruction" would divide by zero (x_m ^ x_i == 0) and return
             * confident garbage. Refuse instead. */
            return CURZI_E_SHARE_DUP;
        }
        seen[x >> 5] |= bit;
    }

    /* Lagrange basis at x = 0, over exactly the FIRST k shares. Using k and not
     * `count` is deliberate: with count > k the extra points are redundant only
     * if every share is honest, and interpolating them in would let one corrupt
     * share silently poison a set that otherwise had a valid k-subset. k-of-n
     * means k.
     *
     *   L_i = prod_{m != i} (0 - x_m) / (x_i - x_m)
     *       = prod_{m != i}  x_m / (x_i ^ x_m)     (characteristic 2: -a == a,
     *                                               and subtraction IS xor)
     * x_i ^ x_m is nonzero for every m != i because duplicates were rejected
     * above, so curzi_gf_inv is never asked for the inverse of 0. */
    for (i = 0u; i < (unsigned)k; i++) {
        uint8_t xi  = shares[i].index;
        uint8_t num = 1u;
        uint8_t den = 1u;

        for (m = 0u; m < (unsigned)k; m++) {
            uint8_t xm;
            if (m == i) { continue; }   /* branch on loop counters, not data */
            xm  = shares[m].index;
            num = curzi_gf_mul(num, xm);
            den = curzi_gf_mul(den, (uint8_t)(xi ^ xm));
        }
        basis[i] = curzi_gf_mul(num, curzi_gf_inv(den));
    }

    /* 32 independent polynomials share one basis: the basis depends only on the
     * abscissae, which are the same for all byte positions. */
    for (j = 0u; j < CURZI_SS_BYTES; j++) {
        uint8_t acc = 0u;
        for (i = 0u; i < (unsigned)k; i++) {
            acc ^= curzi_gf_mul(basis[i], shares[i].y[j]);
        }
        tmp[j] = acc;
    }

    /* Only now, with every check passed, does the caller's buffer change.
     * No libc: hand-rolled copy. */
    for (j = 0u; j < CURZI_SS_BYTES; j++) {
        out_secret[j] = tmp[j];
    }
    rc = CURZI_OK;

    /* Reconstructed material must not outlive this frame. basis[] is derived
     * from public indices but is wiped with it for uniformity. */
    curzi_wipe(tmp, sizeof tmp);
    curzi_wipe(basis, sizeof basis);
    return rc;
}
