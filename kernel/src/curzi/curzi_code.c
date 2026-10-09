/* curzi_code.c — CURZI-8889-A LAYER 3: the canonical binary codes.
 *
 * [8,4,4] extended Hamming  — the code of E8 under Construction A.
 * [24,12,8] extended Golay  — the code of Leech under Construction A.
 *
 * WHY THESE CODES AND NOT A BESPOKE ONE
 * -------------------------------------
 * curzi8889a.h, LAYER 3: these are here for ERROR CORRECTION and for CANONICAL
 * CONSTANTS, never for hardness. Both codes are unique up to coordinate
 * permutation — there is exactly one [8,4,4] and exactly one [24,12,8] binary
 * code — so a designer has no freedom to hide anything in them. That is the
 * whole point. An arbitrary S-box or an unexplained magic constant has a CHOICE
 * in it, and the choice is the hiding place. Here there is no choice: the
 * numbers below are the output of a derivation, and test_curzi_code.c re-runs
 * that derivation from the definition and compares.
 *
 * HOW THE GOLAY CONSTANTS WERE DERIVED (recomputable, not remembered)
 * ------------------------------------------------------------------
 *   1. Q = quadratic residues mod 23 = {1,2,3,4,6,8,9,12,13,16,18}.
 *      (Equivalently the cyclotomic coset of 1 under x -> 2x mod 23, since
 *      ord_23(2) = 11 and 2 is a residue mod 23.)
 *   2. theta(x) = SUM_{r in Q} x^r. The GF(2) span of the 23 cyclic shifts of
 *      theta modulo x^23+1 is the [23,12] binary quadratic-residue (perfect
 *      Golay) code. Its dimension is 12 — verified, not assumed.
 *   3. Extend by an overall parity bit in coordinate 23 -> [24,12,8].
 *   4. Row-reduce to systematic form. Coordinates 0..11 are pivots, giving
 *      G = [I_12 | B]. CURZI_GOLAY_B below is that B, bit j of row i being
 *      B[i][j] (coordinate 12+j).
 * The fingerprint that proves this is the real object is the weight enumerator
 *      0:1  8:759  12:2576  16:759  24:1
 * which the test prints. A wrong or tampered B cannot reproduce it.
 *
 * Taking the residues rather than the non-residues is a LABELLING choice, not a
 * design choice: the non-residue construction yields the same weight enumerator
 * and, by uniqueness of the [24,12,8] code, the same object under a coordinate
 * permutation. Nothing can be smuggled through that choice.
 *
 * WHY THE DECODER NEEDS ONLY B (no 4096-entry coset-leader table)
 * ---------------------------------------------------------------
 * The extended Golay is SELF-DUAL, so G*G^T = 0, so I + B*B^T = 0, so
 * B*B^T = I and B^T = B^-1. That single identity is what makes the arithmetic
 * decoder below work with 24 uint16 constants instead of a 12 KiB table — and
 * a big opaque table is exactly the kind of constant this design refuses to
 * ship. B*B^T = I is checked by the test.
 *
 * CONSTANT TIME — WHAT IS ACHIEVED, AND WHAT IS NOT
 * --------------------------------------------------
 * ACHIEVED. The secret is the transported payload. Every operation that touches
 * payload bits is a masked, fixed-trip-count loop: no data-dependent branch, no
 * data-dependent table index, no early exit. Both syndrome accumulations, the
 * encoders, and all four decoder cases run the same instruction sequence for
 * every input.
 *
 * THE PROPERTY THAT MAKES THIS CHEAP. For a linear code the syndrome of a
 * received word depends ONLY on the error pattern, never on the payload:
 *      s(c + e) = s(c) + s(e) = s(e)   because c is a codeword, so s(c) = 0.
 * So every decision the decoder makes downstream of the syndrome is a function
 * of channel noise, not of the secret. Indexing SYN[] by a loop counter and
 * comparing against s therefore leaks nothing about the payload.
 *
 * NOT ACHIEVED, DELIBERATELY. Three branches remain, and none of them can be
 * removed without removing the function's contract:
 *   - the null-pointer checks (the pointer is the caller's, not the payload);
 *   - the final "did we find a coset leader" test, whose answer IS the return
 *     value CURZI_E_CODE_UNCORRECT;
 *   - curzi_frame_decode's failed/total accounting, likewise returned.
 * All three are functions of the error pattern or of public arguments, by the
 * syndrome identity above, and all three are already disclosed to the caller in
 * the clear. Hiding their timing would protect nothing the return value does
 * not already say.
 *
 * MEASURED, not asserted. aarch64-linux-gnu-gcc 15.2.0 -O2, objdump of every
 * conditional branch in the object:
 *   curzi_hamming84_decode  4 = 1 null + 2 loop edges + 1 detect-2 exit
 *   curzi_golay_decode      6 = 1 null + 4 loop edges + 1 uncorrectable
 *   curzi_frame_decode      5 = 1 null + 2 loop edges + 1 block-parity
 *                               (public index) + 1 fail-closed
 * Every loop edge has a fixed trip count (12, 7, 6, or 9) set by the code
 * parameters, never by an operand. No branch anywhere is on a payload bit.
 *
 * FAIL CLOSED. A decoder that cannot place the error writes NOTHING to its
 * output. There is no partial result, no best-effort word, no "probably fine".
 *
 * ERROR BEHAVIOUR BEYOND THE CORRECTION RADIUS (measured, see the test)
 * ---------------------------------------------------------------------
 *   Golay,   4 errors: ALWAYS DETECTED, never miscorrected. If a weight-4
 *     pattern shared a coset with a weight-<=3 pattern their difference would
 *     be a nonzero codeword of weight <= 7 < d = 8. Impossible. Exhaustively
 *     confirmed over all C(24,4) = 10626 patterns.
 *   Hamming, 2 errors: ALWAYS DETECTED (syndrome nonzero, overall parity even).
 *   Hamming, 3 errors: MISCORRECTED SILENTLY. d = 4 gives correct-1/detect-2
 *     and nothing more; three errors land in the decoding sphere of a different
 *     codeword. This is stated because it is a real limit of [8,4,4], not a
 *     bug: use the Golay frame where three-error resilience is required.
 *
 * Freestanding: integer only, no libc, no allocation, no float, no 64-bit
 * variable divides. Every byte move below is an explicit loop for that reason.
 *
 * Author: H.M. Michael-Laurence: Curzi (c) — 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "curzi8889a.h"

/* ======================= constant-time primitives ======================== *
 * All three are branch-free and index-free. They are the only place where a
 * comparison on a secret-derived word could go wrong, so they are small enough
 * to read in full. */

/* Population count of a 16-bit word. No table (a table index is a cache-timing
 * oracle), no loop over set bits (that trip count IS the value). */
static uint32_t ct_popcount16(uint16_t v)
{
    uint32_t x = (uint32_t)v;
    x = x - ((x >> 1) & 0x5555u);
    x = (x & 0x3333u) + ((x >> 2) & 0x3333u);
    x = (x + (x >> 4)) & 0x0F0Fu;
    return (x + (x >> 8)) & 0x1Fu;
}

/* 0xFFFF if w <= k, else 0. The borrow out of (w - k - 1) is the predicate; we
 * read it from the wrapped sign bit rather than branching on it. Valid for the
 * only ranges used here: w <= 16, k <= 3. */
static uint16_t ct_le(uint32_t w, uint32_t k)
{
    return (uint16_t)(0u - ((w - k - 1u) >> 31));
}

/* 0xFFFF if a == b, else 0. (d - 1) borrows exactly when d == 0. */
static uint16_t ct_eq(uint32_t a, uint32_t b)
{
    uint32_t d = a ^ b;
    return (uint16_t)(0u - ((d - 1u) >> 31));
}

/* Broadcast bit `i` of v across a 16-bit mask: 0xFFFF if set, else 0. This is
 * how every "if this payload bit is set, XOR in that row" is expressed. */
static uint16_t ct_bitmask(uint32_t v, uint32_t i)
{
    return (uint16_t)(0u - ((v >> i) & 1u));
}

/* ============================ [8,4,4] Hamming ============================ *
 * THE PARITY-CHECK MATRIX IS THE CONSTANT, and it is forced. The [7,4] Hamming
 * code is defined by "the columns of H are the seven nonzero vectors of GF(2)^3"
 * — that is the definition of the code, there is no other. Systematic placement
 * then fixes the order with no freedom left:
 *   coordinates 0..3 (data)   take the weight->=2 columns, ascending: 3,5,6,7
 *   coordinates 4..6 (parity) take the weight-1 columns:              1,2,4
 *   coordinate  7             is the overall parity bit of coordinates 0..6.
 * Bit i of a uint8_t codeword is coordinate i.
 *
 * SYN[i] is therefore both the encoder (column i is what data bit i contributes)
 * and the decoder (a single error at coordinate i produces syndrome SYN[i]).
 * One table, two directions, derived not chosen. */
static const uint8_t CURZI_HAMMING_SYN[7] = { 3u, 5u, 6u, 7u, 1u, 2u, 4u };

/* Parity of the low 8 bits, branch-free. */
static uint32_t ct_parity8(uint32_t v)
{
    uint32_t x = v & 0xFFu;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1u;
}

uint8_t curzi_hamming84_encode(uint8_t nibble)
{
    uint32_t d = (uint32_t)nibble & 0x0Fu;
    uint32_t syn = 0u;
    uint32_t word;
    uint32_t i;

    /* MASK the input rather than reject it: this function has no error channel
     * in the contract (it returns the codeword itself), so a total function is
     * the only fail-closed shape available. Fixed 4 iterations, masked — the
     * nibble is payload. */
    for (i = 0u; i < 4u; i++)
        syn ^= (uint32_t)(ct_bitmask(d, i) & CURZI_HAMMING_SYN[i]);

    word = d | (syn << 4);                    /* coordinates 0..6            */
    word |= ct_parity8(word) << 7;            /* coordinate 7: overall parity */
    return (uint8_t)word;
}

int curzi_hamming84_decode(uint8_t codeword, uint8_t *out_nibble)
{
    uint32_t r = (uint32_t)codeword;
    uint32_t s = 0u;          /* 3-bit syndrome over coordinates 0..6        */
    uint32_t sp;              /* overall-parity check over all 8 coordinates */
    uint32_t corr = 0u;
    uint32_t i;
    uint16_t m;

    if (out_nibble == (uint8_t *)0) return -(int)CURZI_E_NULL;

    /* s = H * r. Fixed 7 iterations, masked accumulation; r carries payload. */
    for (i = 0u; i < 7u; i++)
        s ^= (uint32_t)(ct_bitmask(r, i) & CURZI_HAMMING_SYN[i]);
    sp = ct_parity8(r);

    /* Candidate correction, selected by comparing s against every column. The
     * loop index is public; only the comparison result is secret-adjacent, and
     * by the syndrome identity above s is a function of the noise alone. */
    for (i = 0u; i < 7u; i++) {
        m = ct_eq(s, (uint32_t)CURZI_HAMMING_SYN[i]);
        corr |= (uint32_t)m & (1u << i);
    }
    m = ct_eq(s, 0u);
    corr |= (uint32_t)m & 0x80u;     /* s == 0 with odd parity: the parity bit */

    /* Apply the correction only when the overall parity says the error weight
     * is odd. Even weight with a nonzero syndrome is a DETECTED double error. */
    corr &= 0u - sp;

    /* Fail closed. s != 0 with even parity cannot be one error; refuse, and
     * leave *out_nibble untouched. Branch is on the noise, not the payload. */
    if (sp == 0u && s != 0u) return -(int)CURZI_E_CODE_UNCORRECT;

    *out_nibble = (uint8_t)((r ^ corr) & 0x0Fu);
    return (int)sp;                  /* number of errors corrected: 0 or 1 */
}

/* ============================ [24,12,8] Golay ============================ *
 * G = [I_12 | B]. Bit j of CURZI_GOLAY_B[i] is B[i][j]; CURZI_GOLAY_BT[j] is
 * column j of B, i.e. row j of B^T = B^-1 (self-duality). Derived by the QR
 * procedure documented at the top of this file; regenerated and compared by
 * test_curzi_code.c, which also prints the weight enumerator. */
static const uint16_t CURZI_GOLAY_B[12] = {
    0xAE3u, 0xDC6u, 0x16Fu, 0x2DEu, 0x5BCu, 0x99Bu,
    0xB36u, 0xE6Cu, 0x63Bu, 0xE95u, 0x7C9u, 0xD71u
};
static const uint16_t CURZI_GOLAY_BT[12] = {
    0xF25u, 0x16Fu, 0x2DEu, 0x5BCu, 0xB78u, 0x9D5u,
    0xC8Fu, 0x63Bu, 0xC76u, 0x7C9u, 0xF92u, 0xAE3u
};

/* Byte layout, fixed once here and used by every caller: coordinate c of the
 * 24-bit codeword is bit (c mod 8) of byte (c / 8). Coordinates 0..11 are the
 * data half, 12..23 the parity half. Chosen because it needs no bit reversal
 * and no endian assumption. */
static void golay_pack(uint16_t dat, uint16_t par, uint8_t out[3])
{
    out[0] = (uint8_t)(dat & 0xFFu);
    out[1] = (uint8_t)(((dat >> 8) & 0x0Fu) | ((par & 0x0Fu) << 4));
    out[2] = (uint8_t)((par >> 4) & 0xFFu);
}

/* mul by B: XOR of the rows of B selected by the set bits of v. Fixed 12
 * iterations, masked — v is payload on the encode path. */
static uint16_t golay_mulB(uint16_t v, const uint16_t rows[12])
{
    uint16_t acc = 0u;
    uint32_t i;
    for (i = 0u; i < 12u; i++)
        acc = (uint16_t)(acc ^ (ct_bitmask((uint32_t)v, i) & rows[i]));
    return acc;
}

void curzi_golay_encode(uint16_t data12, uint8_t out[3])
{
    uint16_t d;
    if (out == (uint8_t *)0) return;   /* void contract: nothing to report to */
    d = (uint16_t)(data12 & 0x0FFFu);  /* total, as with the Hamming encoder  */
    golay_pack(d, golay_mulB(d, CURZI_GOLAY_B), out);
}

/* THE DECODER, DERIVED FROM B^T = B^-1 RATHER THAN MEMORISED
 * ----------------------------------------------------------
 * Write the error as e = (eL | eR) over the two halves. The syndrome is
 *      s = eL*B + eR                                                     (1)
 * Right-multiplying (1) by B^T and using B*B^T = I,
 *      q = s*B^T = eL + eR*B^T                                           (2)
 * With wt(e) <= 3 exactly four cases are possible, and each is read straight
 * off (1) or (2):
 *      wt(eL) = 0            -> s = eR,                    wt(s) <= 3
 *      wt(eL) = 1, at row i  -> s + B[i] = eR,             wt(.) <= 2
 *      wt(eR) = 0            -> q = eL,                    wt(q) <= 3
 *      wt(eR) = 1, at col j  -> q + B^T[j] = eL,           wt(.) <= 2
 * Those four are exhaustive for weight <= 3 (if wt(eL) >= 2 then wt(eR) <= 1),
 * so a failure to match is a PROOF that more than three errors occurred — not a
 * heuristic. All four cases are evaluated unconditionally and merged with
 * masks; `found` makes the first match win, which keeps the result
 * deterministic even for the >3-error inputs where several cases can fire. */
int curzi_golay_decode(const uint8_t in[3], uint16_t *out_data12)
{
    uint16_t rL, rR, s, q, t, m;
    uint16_t eL = 0u, eR = 0u, found = 0u;
    uint32_t i;

    if (in == (const uint8_t *)0 || out_data12 == (uint16_t *)0)
        return -(int)CURZI_E_NULL;

    rL = (uint16_t)((uint16_t)in[0] | (uint16_t)(((uint16_t)in[1] & 0x0Fu) << 8));
    rR = (uint16_t)((uint16_t)((in[1] >> 4) & 0x0Fu) | (uint16_t)((uint16_t)in[2] << 4));

    s = (uint16_t)(golay_mulB(rL, CURZI_GOLAY_B) ^ rR);   /* (1) */

    /* case 1: no error in the data half */
    m = (uint16_t)(ct_le(ct_popcount16(s), CURZI_GOLAY_T) & (uint16_t)~found);
    eR = (uint16_t)(eR | (m & s));
    found = (uint16_t)(found | m);

    /* case 2: exactly one error in the data half, at coordinate i */
    for (i = 0u; i < 12u; i++) {
        t = (uint16_t)(s ^ CURZI_GOLAY_B[i]);
        m = (uint16_t)(ct_le(ct_popcount16(t), 2u) & (uint16_t)~found);
        eL = (uint16_t)(eL | (m & (uint16_t)(1u << i)));
        eR = (uint16_t)(eR | (m & t));
        found = (uint16_t)(found | m);
    }

    q = golay_mulB(s, CURZI_GOLAY_BT);                    /* (2) */

    /* case 3: no error in the parity half */
    m = (uint16_t)(ct_le(ct_popcount16(q), CURZI_GOLAY_T) & (uint16_t)~found);
    eL = (uint16_t)(eL | (m & q));
    found = (uint16_t)(found | m);

    /* case 4: exactly one error in the parity half, at coordinate 12+j */
    for (i = 0u; i < 12u; i++) {
        t = (uint16_t)(q ^ CURZI_GOLAY_BT[i]);
        m = (uint16_t)(ct_le(ct_popcount16(t), 2u) & (uint16_t)~found);
        eL = (uint16_t)(eL | (m & t));
        eR = (uint16_t)(eR | (m & (uint16_t)(1u << i)));
        found = (uint16_t)(found | m);
    }

    /* Fail closed: no coset leader of weight <= 3 exists, so the word is not
     * within the correction radius. Write nothing. */
    if (found == 0u) return -(int)CURZI_E_CODE_UNCORRECT;

    *out_data12 = (uint16_t)(rL ^ eL);
    return (int)(ct_popcount16(eL) + ct_popcount16(eR));
}

/* ============================== 144-bit frame ============================ *
 * 6 x 24 = 144 bits out of 6 x 12 = 72 bits in, per curzi8889a.h LAYER 3.
 * The 72-bit input uses the same coordinate convention as a Golay block: bit b
 * is bit (b mod 8) of byte (b / 8), and block i owns bits [12i, 12i + 12).
 * Every /8 and %8 here is by a constant, so no 64-bit variable divide appears. */
static uint16_t frame_get12(const uint8_t *p, uint32_t block)
{
    /* Two blocks per three bytes; `base` is exact integer arithmetic on a
     * public loop index, never on payload. */
    uint32_t base = (block >> 1) * 3u;
    if ((block & 1u) == 0u)
        return (uint16_t)((uint16_t)p[base] | (uint16_t)(((uint16_t)p[base + 1u] & 0x0Fu) << 8));
    return (uint16_t)((uint16_t)((p[base + 1u] >> 4) & 0x0Fu) | (uint16_t)((uint16_t)p[base + 2u] << 4));
}

static void frame_put12(uint8_t *p, uint32_t block, uint16_t v)
{
    uint32_t base = (block >> 1) * 3u;
    if ((block & 1u) == 0u) {
        p[base] = (uint8_t)(v & 0xFFu);
        p[base + 1u] = (uint8_t)((p[base + 1u] & 0xF0u) | ((v >> 8) & 0x0Fu));
    } else {
        p[base + 1u] = (uint8_t)((p[base + 1u] & 0x0Fu) | (uint8_t)((v & 0x0Fu) << 4));
        p[base + 2u] = (uint8_t)((v >> 4) & 0xFFu);
    }
}

int curzi_frame_encode(const uint8_t in9[9], uint8_t out18[18])
{
    uint32_t b;
    if (in9 == (const uint8_t *)0 || out18 == (uint8_t *)0)
        return -(int)CURZI_E_NULL;
    for (b = 0u; b < CURZI_CODE_BLOCKS; b++)
        curzi_golay_encode(frame_get12(in9, b), &out18[b * 3u]);
    return (int)CURZI_OK;
}

int curzi_frame_decode(const uint8_t in18[18], uint8_t out9[9])
{
    uint8_t tmp[9];
    uint32_t b;
    int total = 0;
    int failed = 0;

    if (in18 == (const uint8_t *)0 || out9 == (uint8_t *)0)
        return -(int)CURZI_E_NULL;

    for (b = 0u; b < 9u; b++) tmp[b] = 0u;   /* frame_put12 read-modify-writes */

    /* Every block is decoded even after one fails, and the "did this block
     * fail" test is folded in with a mask rather than a branch. Uniform work is
     * the point: the caller learns "a block was uncorrectable", never WHICH
     * block, from timing. It costs six fixed iterations. */
    for (b = 0u; b < CURZI_CODE_BLOCKS; b++) {
        uint16_t d = 0u;
        int rc = curzi_golay_decode(&in18[b * 3u], &d);
        /* rc < 0 on failure; broadcast its sign bit. curzi_golay_decode leaves
         * d untouched when it fails, and the mask zeroes it regardless. */
        uint32_t bad = 0u - ((uint32_t)rc >> 31);
        failed |= (int)(bad & 1u);
        total  += (int)(~bad & (uint32_t)rc);
        frame_put12(tmp, b, (uint16_t)(~bad & d));
    }

    /* Fail closed: one bad block voids the whole frame. No partial result
     * reaches the caller — out9 is not written at all. */
    if (failed) return -(int)CURZI_E_CODE_UNCORRECT;

    for (b = 0u; b < 9u; b++) out9[b] = tmp[b];
    return total;                            /* total errors corrected */
}
