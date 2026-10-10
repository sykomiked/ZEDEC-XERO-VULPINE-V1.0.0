/* test_curzi_code.c — host test for CURZI-8889-A LAYER 3 (canonical codes).
 *
 * THIS IS THE ANTI-BACKDOOR CHECK, NOT A SMOKE TEST.
 * curzi8889a.h says: "If any constant in this system is not recomputable from
 * the Hamming or Golay definition, the no-backdoor argument is void." This file
 * is where that is settled. It does not read the constants out of curzi_code.c
 * and agree with them. It REBUILDS both codes from their mathematical
 * definitions — the quadratic residues mod 23 for the Golay, the nonzero
 * columns of GF(2)^3 for the Hamming — and then demands that the shipped
 * implementation produce the same objects, coordinate for coordinate.
 *
 * Two independent handles on the Golay, both required to agree:
 *   1. STRUCTURE. The span of the 23 cyclic shifts of the QR idempotent,
 *      extended by overall parity, row-reduced to [I|B]. Compared against the
 *      B that curzi_golay_encode actually uses (read back through the public
 *      API, so there is nowhere for a second table to hide).
 *   2. SET. The same span, enumerated WITHOUT row reduction, as a set of 4096
 *      24-bit words. Every implementation codeword must be in it. A convention
 *      slip that survived check 1 would not survive this.
 *
 * And the fingerprint that no wrong table can fake: the weight enumerator
 *      0:1  8:759  12:2576  16:759  24:1
 * printed below. 759 octads is the Steiner system S(5,8,24); it is not a number
 * you can arrive at with a tampered generator.
 *
 * Host-only: stdio is used here and NOWHERE in curzi_code.c.
 *
 * Build (exactly as run):
 *   gcc -std=c11 -Wall -Werror -Wextra -O2 -o test_curzi_code \
 *       test_curzi_code.c curzi_code.c
 *
 * Author: H.M. Michael-Laurence: Curzi (c) — 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdint.h>
#include "curzi8889a.h"

static int g_fail = 0;

static void ok(const char *what, int cond)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_fail++;
}

static int popc32(uint32_t v)
{
    int c = 0;
    while (v) {
        c += (int) (v & 1u);
        v >>= 1;
    }
    return c;
}

/* ------------------------------------------------------------------------ *
 * GF(2) row reduction. Returns rank; `basis` gets the reduced rows and
 * `pivots` their pivot columns (scanned low to high).
 * ------------------------------------------------------------------------ */
static int rref(const uint32_t *rows, int nrows, int nbits, uint32_t *basis, int *pivots)
{
    uint32_t work[32];
    int i, r = 0, c;
    for (i = 0; i < nrows; i++) work[i] = rows[i];
    for (c = 0; c < nbits && r < nrows; c++) {
        int p = -1;
        for (i = r; i < nrows; i++)
            if ((work[i] >> c) & 1u) {
                p = i;
                break;
            }
        if (p < 0) continue;
        {
            uint32_t t = work[r];
            work[r] = work[p];
            work[p] = t;
        }
        for (i = 0; i < nrows; i++)
            if (i != r && ((work[i] >> c) & 1u)) work[i] ^= work[r];
        pivots[r] = c;
        r++;
    }
    for (i = 0; i < r; i++) basis[i] = work[i];
    return r;
}

/* ------------------------------------------------------------------------ *
 * REFERENCE CONSTRUCTION 1 — the [24,12,8] Golay from quadratic residues
 * mod 23. Nothing here is remembered; Q is computed, the span is computed, the
 * dimension is checked rather than assumed.
 * ------------------------------------------------------------------------ */
static int golay_reference(uint16_t B_ref[12], uint32_t span_basis[12])
{
    uint32_t Q = 0u, shifts[23], basis[23], ext[12], g[12];
    int piv[23], gpiv[12];
    int a, i, j, rk;

    for (a = 1; a < 23; a++) Q |= 1u << ((a * a) % 23); /* residues mod 23 */
    if (popc32(Q) != 11) return -1;

    /* theta(x) = SUM x^r over r in Q, and its 23 cyclic shifts mod x^23+1 */
    for (i = 0; i < 23; i++) {
        uint32_t v = 0u;
        for (j = 0; j < 23; j++)
            if ((Q >> j) & 1u) v |= 1u << ((j + i) % 23);
        shifts[i] = v;
    }
    rk = rref(shifts, 23, 23, basis, piv);
    if (rk != 12) return -2; /* must be the [23,12] QR code */

    /* extend: overall parity into coordinate 23 -> [24,12] */
    for (i = 0; i < 12; i++) {
        uint32_t v = basis[i];
        if (popc32(v) & 1) v |= 1u << 23;
        ext[i] = v;
        span_basis[i] = v; /* kept un-reduced for check 2 */
    }

    /* systematic form; coordinates 0..11 must be the pivots */
    if (rref(ext, 12, 24, g, gpiv) != 12) return -3;
    for (i = 0; i < 12; i++)
        if (gpiv[i] != i) return -4;
    for (i = 0; i < 12; i++) B_ref[i] = (uint16_t) (g[i] >> 12);
    return 0;
}

/* ------------------------------------------------------------------------ *
 * REFERENCE CONSTRUCTION 2 — the [8,4,4] extended Hamming from the definition
 * "the columns of H are the seven nonzero vectors of GF(2)^3".
 * ------------------------------------------------------------------------ */
static void hamming_reference(uint8_t cw_ref[16])
{
    uint8_t datacol[4]; /* weight->=2 columns, ascending: the data positions */
    int n = 0, v, m, j;

    for (v = 1; v < 8; v++)
        if (popc32((uint32_t) v) >= 2) datacol[n++] = (uint8_t) v;
    /* n == 4 by construction: 3,5,6,7 */

    for (m = 0; m < 16; m++) {
        uint32_t syn = 0u, word;
        for (j = 0; j < 4; j++)
            if ((m >> j) & 1) syn ^= datacol[j];
        word = (uint32_t) m | (syn << 4);
        if (popc32(word) & 1) word |= 0x80u;
        cw_ref[m] = (uint8_t) word;
    }
}

/* pack/unpack matching curzi_code.c: coordinate c is bit (c%8) of byte (c/8) */
static uint32_t cw24(const uint8_t b[3])
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16);
}
static void cw24_put(uint32_t w, uint8_t b[3])
{
    b[0] = (uint8_t) (w & 0xFFu);
    b[1] = (uint8_t) ((w >> 8) & 0xFFu);
    b[2] = (uint8_t) ((w >> 16) & 0xFFu);
}

/* 2^24-bit membership bitmap for the reference codeword SET (2 MiB, host only) */
static uint32_t g_member[1u << 19];

int main(void)
{
    uint16_t B_ref[12], B_impl[12];
    uint32_t span[12];
    uint8_t cw_ref[16];
    uint8_t buf[3], frame_in[9], frame_enc[18], frame_out[9];
    long we[25];
    uint32_t m, i, j, k, l;
    int rc;

    printf("CURZI-8889-A LAYER 3 — canonical binary codes\n");

    /* =================================================================== *
     * 0. Rebuild both codes from their definitions.
     * =================================================================== */
    printf("\n[0] independent reconstruction from first principles\n");
    rc = golay_reference(B_ref, span);
    ok("QR-mod-23 construction yields a [23,12] code, extended to [24,12]", rc == 0);
    if (rc != 0) {
        printf("  reference construction failed rc=%d\n", rc);
        return 1;
    }
    hamming_reference(cw_ref);

    /* Read the implementation's B back through the PUBLIC API only. */
    for (i = 0; i < 12u; i++) {
        curzi_golay_encode((uint16_t) (1u << i), buf);
        B_impl[i] = (uint16_t) (((uint32_t) buf[1] >> 4) | ((uint32_t) buf[2] << 4));
    }
    {
        int same = 1;
        for (i = 0; i < 12u; i++)
            if (B_ref[i] != B_impl[i]) same = 0;
        ok("shipped B == B re-derived from quadratic residues mod 23", same);
        printf("       derived B: ");
        for (i = 0; i < 12u; i++) printf("%03X ", B_ref[i]);
        printf("\n       shipped B: ");
        for (i = 0; i < 12u; i++) printf("%03X ", B_impl[i]);
        printf("\n");
    }

    /* B*B^T == I: the self-duality the decoder's algebra rests on. */
    {
        int selfdual = 1;
        for (i = 0; i < 12u; i++)
            for (j = 0; j < 12u; j++) {
                int bit = popc32((uint32_t) (B_impl[i] & B_impl[j])) & 1;
                if (bit != (i == j)) selfdual = 0;
            }
        ok("B*B^T == I  (extended Golay is self-dual; B^T = B^-1)", selfdual);
    }

    /* Set equality against the UN-reduced span: an independent second path. */
    {
        long count = 0;
        int all_in = 1;
        for (i = 0; i < (1u << 19); i++) g_member[i] = 0u;
        for (m = 0; m < 4096u; m++) {
            uint32_t w = 0u;
            for (i = 0; i < 12u; i++)
                if ((m >> i) & 1u) w ^= span[i];
            if (!((g_member[w >> 5] >> (w & 31u)) & 1u)) count++;
            g_member[w >> 5] |= 1u << (w & 31u);
        }
        ok("QR span has exactly 4096 distinct codewords", count == 4096);
        for (m = 0; m < 4096u; m++) {
            uint32_t w;
            curzi_golay_encode((uint16_t) m, buf);
            w = cw24(buf);
            if (!((g_member[w >> 5] >> (w & 31u)) & 1u)) all_in = 0;
        }
        ok("every curzi_golay_encode output lies in the QR-derived code", all_in);
    }

    /* =================================================================== *
     * 1. THE FINGERPRINT: Golay weight enumerator.
     * =================================================================== */
    printf("\n[1] Golay weight enumerator (the anti-backdoor fingerprint)\n");
    for (i = 0; i <= 24u; i++) we[i] = 0;
    for (m = 0; m < 4096u; m++) {
        curzi_golay_encode((uint16_t) m, buf);
        we[popc32(cw24(buf))]++;
    }
    printf("       ");
    for (i = 0; i <= 24u; i++)
        if (we[i]) printf("%u:%ld  ", i, we[i]);
    printf("\n");
    ok("4096 codewords", we[0] + we[8] + we[12] + we[16] + we[24] == 4096);
    ok("w=0 -> 1", we[0] == 1);
    ok("w=8 -> 759    (the 759 octads of the Steiner system S(5,8,24))", we[8] == 759);
    ok("w=12 -> 2576  (the dodecads)", we[12] == 2576);
    ok("w=16 -> 759", we[16] == 759);
    ok("w=24 -> 1     (the all-ones vector)", we[24] == 1);
    {
        int clean = 1;
        for (i = 1; i <= 24u; i++)
            if (i != 8u && i != 12u && i != 16u && i != 24u && we[i] != 0) clean = 0;
        ok("no codeword of any other weight; minimum distance is exactly 8", clean);
    }

    /* =================================================================== *
     * 2. Golay correction: EXHAUSTIVE over every error pattern of weight
     *    0..3 AND every one of the 4096 codewords. 2325 * 4096 decodes.
     * =================================================================== */
    printf("\n[2] Golay: all weight-0/1/2/3 error patterns x all 4096 codewords\n");
    {
        /* 1 + 24 + C(24,2) + C(24,3) = 1 + 24 + 276 + 2024 = 2325 patterns */
        uint32_t pat[2325];
        long npat = 0, tried = 0, bad = 0, badcount = 0;
        pat[npat++] = 0u;
        for (i = 0; i < 24u; i++) pat[npat++] = 1u << i;
        for (i = 0; i < 24u; i++)
            for (j = i + 1u; j < 24u; j++) pat[npat++] = (1u << i) | (1u << j);
        for (i = 0; i < 24u; i++)
            for (j = i + 1u; j < 24u; j++)
                for (k = j + 1u; k < 24u; k++) pat[npat++] = (1u << i) | (1u << j) | (1u << k);
        ok("enumerated 2325 = 1 + 24 + C(24,2) + C(24,3) error patterns", npat == 2325);

        for (m = 0; m < 4096u; m++) {
            uint32_t base;
            long p;
            curzi_golay_encode((uint16_t) m, buf);
            base = cw24(buf);
            for (p = 0; p < npat; p++) {
                uint16_t got = 0xFFFFu;
                uint8_t r[3];
                cw24_put(base ^ pat[p], r);
                rc = curzi_golay_decode(r, &got);
                tried++;
                if (rc < 0 || got != (uint16_t) m)
                    bad++;
                else if (rc != popc32(pat[p]))
                    badcount++;
            }
        }
        printf("       %ld decodes attempted (2325 patterns x 4096 codewords)\n", tried);
        ok("every one recovered the original 12-bit message", bad == 0);
        ok("every one reported the exact number of errors corrected", badcount == 0);
    }

    /* =================================================================== *
     * 3. Four errors. Claim: ALWAYS DETECTED, NEVER miscorrected — because a
     *    weight-4 and a weight-<=3 pattern in one coset would differ by a
     *    nonzero codeword of weight <= 7 < 8. Exhaustive over C(24,4)=10626.
     * =================================================================== */
    printf("\n[3] Golay: all C(24,4) = 10626 four-error patterns x 256 codewords\n");
    {
        long tried = 0, flagged = 0, miscorrected = 0;
        for (m = 0; m < 4096u; m += 16u) { /* 256 codewords */
            uint32_t base;
            curzi_golay_encode((uint16_t) m, buf);
            base = cw24(buf);
            for (i = 0; i < 24u; i++)
                for (j = i + 1u; j < 24u; j++)
                    for (k = j + 1u; k < 24u; k++)
                        for (l = k + 1u; l < 24u; l++) {
                            uint32_t e = (1u << i) | (1u << j) | (1u << k) | (1u << l);
                            uint16_t got = 0xFFFFu;
                            uint8_t r[3];
                            cw24_put(base ^ e, r);
                            rc = curzi_golay_decode(r, &got);
                            tried++;
                            if (rc < 0)
                                flagged++;
                            else if (got != (uint16_t) m)
                                miscorrected++;
                        }
        }
        printf("       %ld patterns: %ld flagged CURZI_E_CODE_UNCORRECT, "
               "%ld silently miscorrected\n",
               tried, flagged, miscorrected);
        ok("4 errors are ALWAYS flagged, never silently miscorrected",
           flagged == tried && miscorrected == 0);
    }

    /* =================================================================== *
     * 4. Hamming.
     * =================================================================== */
    printf("\n[4] [8,4,4] extended Hamming\n");
    {
        int same = 1;
        for (m = 0; m < 16u; m++)
            if (curzi_hamming84_encode((uint8_t) m) != cw_ref[m]) same = 0;
        ok("shipped codewords == codewords re-derived from the columns of GF(2)^3", same);
        printf("       codewords: ");
        for (m = 0; m < 16u; m++) printf("%02X ", cw_ref[m]);
        printf("\n");
    }
    for (i = 0; i <= 8u; i++) we[i] = 0;
    for (m = 0; m < 16u; m++) we[popc32(curzi_hamming84_encode((uint8_t) m))]++;
    printf("       weight enumerator: ");
    for (i = 0; i <= 8u; i++)
        if (we[i]) printf("%u:%ld  ", i, we[i]);
    printf("\n");
    ok("16 codewords", we[0] + we[4] + we[8] == 16);
    ok("weight enumerator is 0:1 4:14 8:1; minimum distance is exactly 4",
       we[0] == 1 && we[4] == 14 && we[8] == 1);
    {
        int bad = 0, badcount = 0;
        for (m = 0; m < 16u; m++) {
            uint8_t c = curzi_hamming84_encode((uint8_t) m);
            for (i = 0; i < 8u; i++) {
                uint8_t got = 0xFFu;
                rc = curzi_hamming84_decode((uint8_t) (c ^ (uint8_t) (1u << i)), &got);
                if (rc < 0 || got != (uint8_t) m)
                    bad++;
                else if (rc != 1)
                    badcount++;
            }
        }
        ok("all 16*8 = 128 single-error patterns corrected exhaustively", bad == 0);
        ok("each reported exactly 1 corrected error", badcount == 0);
    }
    {
        long tried = 0, flagged = 0;
        for (m = 0; m < 16u; m++) {
            uint8_t c = curzi_hamming84_encode((uint8_t) m);
            for (i = 0; i < 8u; i++)
                for (j = i + 1u; j < 8u; j++) {
                    uint8_t got = 0xFFu;
                    rc = curzi_hamming84_decode((uint8_t) (c ^ (uint8_t) ((1u << i) | (1u << j))),
                                                &got);
                    tried++;
                    if (rc < 0) flagged++;
                }
        }
        printf("       %ld double-error patterns: %ld flagged\n", tried, flagged);
        ok("all 2-error patterns DETECTED (correct-1/detect-2 as [8,4,4] promises)",
           flagged == tried);
    }
    {
        /* Honest measurement, not a promise: d=4 cannot survive 3 errors. */
        long tried = 0, flagged = 0, miscorrected = 0;
        for (m = 0; m < 16u; m++) {
            uint8_t c = curzi_hamming84_encode((uint8_t) m);
            for (i = 0; i < 8u; i++)
                for (j = i + 1u; j < 8u; j++)
                    for (k = j + 1u; k < 8u; k++) {
                        uint8_t got = 0xFFu;
                        rc = curzi_hamming84_decode(
                            (uint8_t) (c ^ (uint8_t) ((1u << i) | (1u << j) | (1u << k))), &got);
                        tried++;
                        if (rc < 0)
                            flagged++;
                        else if (got != (uint8_t) m)
                            miscorrected++;
                    }
        }
        printf("       %ld triple-error patterns: %ld flagged, %ld silently "
               "miscorrected\n",
               tried, flagged, miscorrected);
        printf("       ^ DOCUMENTED LIMIT, not a defect: [8,4,4] is a "
               "correct-1/detect-2 code.\n");
        ok("triple errors behave as the code's distance dictates (no crash, "
           "total function)",
           tried == 16 * 56);
    }

    /* =================================================================== *
     * 5. 144-bit frame: 6 blocks x 24 bits, 3 errors injected in EVERY block.
     * =================================================================== */
    printf("\n[5] 144-bit frame: 6 Golay blocks, 3 errors injected per block\n");
    {
        uint32_t seed = 0x8889A001u;
        long trials = 0, bad = 0;
        int corrected_total_ok = 1;
        for (trials = 0; trials < 20000; trials++) {
            int same = 1;
            /* xorshift32: deterministic, reproducible, no libc rand */
            for (i = 0; i < 9u; i++) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                frame_in[i] = (uint8_t) (seed & 0xFFu);
            }
            rc = curzi_frame_encode(frame_in, frame_enc);
            if (rc != CURZI_OK) {
                bad++;
                continue;
            }
            for (i = 0; i < CURZI_CODE_BLOCKS; i++) {
                uint32_t bits = 0u;
                int placed = 0;
                while (placed < 3) {
                    uint32_t b;
                    seed ^= seed << 13;
                    seed ^= seed >> 17;
                    seed ^= seed << 5;
                    b = seed % 24u;
                    if ((bits >> b) & 1u) continue;
                    bits |= 1u << b;
                    placed++;
                }
                for (j = 0; j < 24u; j++)
                    if ((bits >> j) & 1u)
                        frame_enc[i * 3u + (j >> 3)] ^= (uint8_t) (1u << (j & 7u));
            }
            rc = curzi_frame_decode(frame_enc, frame_out);
            if (rc != 18) corrected_total_ok = 0; /* 6 blocks x 3 errors */
            for (i = 0; i < 9u; i++)
                if (frame_in[i] != frame_out[i]) same = 0;
            if (rc < 0 || !same) bad++;
        }
        printf("       %ld frames, 18 bit errors each (3 per block, "
               "%ld corrupted bits total)\n",
               trials, trials * 18);
        ok("every frame round-tripped byte-for-byte", bad == 0);
        ok("curzi_frame_decode reported exactly 18 corrected errors each time", corrected_total_ok);
    }
    {
        /* Clean frame, and a fail-closed frame. */
        int same = 1;
        for (i = 0; i < 9u; i++) frame_in[i] = (uint8_t) (0xA5u ^ (i * 31u));
        for (i = 0; i < 9u; i++) frame_out[i] = 0u;
        ok("curzi_frame_encode returns CURZI_OK",
           curzi_frame_encode(frame_in, frame_enc) == (int) CURZI_OK);
        rc = curzi_frame_decode(frame_enc, frame_out);
        for (i = 0; i < 9u; i++)
            if (frame_in[i] != frame_out[i]) same = 0;
        ok("clean frame decodes with 0 corrections", rc == 0 && same);

        /* 4 errors in block 3 -> whole frame must be refused, and out9 must
         * be left EXACTLY as it was: fail closed means no partial result. */
        for (i = 0; i < 9u; i++) frame_out[i] = 0x5Cu;
        frame_enc[9] ^= 0x0Fu; /* 4 bit errors inside block 3 */
        rc = curzi_frame_decode(frame_enc, frame_out);
        ok("a 4-error block voids the whole frame (-CURZI_E_CODE_UNCORRECT)",
           rc == -(int) CURZI_E_CODE_UNCORRECT);
        same = 1;
        for (i = 0; i < 9u; i++)
            if (frame_out[i] != 0x5Cu) same = 0;
        ok("output buffer untouched on failure — no partial result escapes", same);
    }

    /* =================================================================== *
     * 6. Fail closed on null arguments.
     * =================================================================== */
    printf("\n[6] fail closed\n");
    {
        uint16_t d16 = 0u;
        uint8_t n8 = 0u;
        ok("hamming decode(NULL out)",
           curzi_hamming84_decode(0u, (uint8_t *) 0) == -(int) CURZI_E_NULL);
        ok("golay decode(NULL in)",
           curzi_golay_decode((const uint8_t *) 0, &d16) == -(int) CURZI_E_NULL);
        ok("golay decode(NULL out)",
           curzi_golay_decode(frame_enc, (uint16_t *) 0) == -(int) CURZI_E_NULL);
        ok("frame encode(NULL in)",
           curzi_frame_encode((const uint8_t *) 0, frame_enc) == -(int) CURZI_E_NULL);
        ok("frame encode(NULL out)",
           curzi_frame_encode(frame_in, (uint8_t *) 0) == -(int) CURZI_E_NULL);
        ok("frame decode(NULL in)",
           curzi_frame_decode((const uint8_t *) 0, frame_out) == -(int) CURZI_E_NULL);
        ok("frame decode(NULL out)",
           curzi_frame_decode(frame_enc, (uint8_t *) 0) == -(int) CURZI_E_NULL);
        (void) n8;
    }

    /* =================================================================== *
     * 7. Contract constants actually match the code that was built.
     * =================================================================== */
    printf("\n[7] header contract\n");
    ok("CURZI_GOLAY_N/K/T = 24/12/3",
       CURZI_GOLAY_N == 24u && CURZI_GOLAY_K == 12u && CURZI_GOLAY_T == 3u);
    ok("CURZI_HAMMING_N/K = 8/4", CURZI_HAMMING_N == 8u && CURZI_HAMMING_K == 4u);
    ok("CURZI_CODE_BITS = 6 * 24 = 144", CURZI_CODE_BITS == 144u && CURZI_CODE_BLOCKS == 6u);

    printf("\n%s — %d failure(s)\n", g_fail ? "*** FAILED ***" : "ALL TESTS PASSED", g_fail);
    return g_fail ? 1 : 0;
}
