/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kitling_rules.h — Kitling's rules in C, the reference its KRON, Eve,
 * Trebago, FORTRAN 77, BASIC, Lingua IR and MYRA blocks are checked against
 * (blocks/kitling). Pure integer functions, no state: the model
 * (kitling_model.c) and the language test (tests/kitling_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   KR1 GROWTH STAGE. One step for each threshold a generation reaches, 1,
 *       2, 5, 13 and 34 (every other Fibonacci number): 0 newborn, 1 kit,
 *       2 young, 3 grown, 4 elder, 5 sage. A generation is
 *       0..KITLING_GEN_MAX (swarm_dna.h D5 keeps it in 32 bits); anything
 *       else is refused (KITLING_REFUSED).
 *   KR2 GROWN. The generation reaches the grown stage (KR1 stage 3 or
 *       more: a generation of 5..KITLING_GEN_MAX).
 *   KR3 ZECKENDORF FORM. A token amount as the tiers that hold a token in
 *       its canonical form (swarm_denom.h D1, D3): bit k set when tier k,
 *       of face F(k+2), holds one, found greedily from the top, no two
 *       adjacent bits set. The amount is 0..KITLING_ZECK_MAX, every amount
 *       below F(65), the 64th face, so the mask fits 63 bits; anything
 *       else is refused. FORTRAN 77 (and its Lingua IR) keep 32-bit
 *       integers and refuse past KITLING_ZECK32_MAX (31 tiers).
 *   KR4 TRAIT STRENGTH. A gene byte 0..255 as a strength in permille,
 *       rounded to nearest: floor((gene * 1000 + 127) / 255), as
 *       swarm_dna.c expresses a trait (D2); anything else is refused.
 *   KR5 A MEMORY RECORD of ^ZXKITLING holds integers of at most the digits
 *       its pieces declare, sign aside: seq 9, stamp 15 (ms since 1970),
 *       emotion 1, intensity 1, tokens 9. */
#ifndef KITLING_RULES_H
#define KITLING_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define KITLING_REFUSED    (-1)
#define KITLING_GEN_MAX    INT64_C(4294967295)
#define KITLING_GROWN_GEN  5
#define KITLING_STAGES     6
#define KITLING_ZECK_MAX   INT64_C(17167680177564) /* F(65) - 1 */
#define KITLING_ZECK32_MAX INT64_C(3524577)        /* F(33) - 1: a 31-bit mask */
#define KITLING_ZECK_TIERS 63u
#define KITLING_GENE_MAX   255

/* KR1 */
static inline int64_t kitling_stage(int64_t generation)
{
    static const int64_t STEP[KITLING_STAGES - 1] = {1, 2, 5, 13, 34};
    if (generation < 0 || generation > KITLING_GEN_MAX) return KITLING_REFUSED;
    int64_t s = 0;
    for (uint32_t i = 0; i < KITLING_STAGES - 1; i++) s += generation >= STEP[i];
    return s;
}

/* KR2 */
static inline bool kitling_grown(int64_t generation)
{
    return generation >= KITLING_GROWN_GEN && generation <= KITLING_GEN_MAX;
}

/* KR3 */
static inline int64_t kitling_zeck(int64_t amount)
{
    if (amount < 0 || amount > KITLING_ZECK_MAX) return KITLING_REFUSED;
    int64_t face[KITLING_ZECK_TIERS];
    face[0] = 1;
    face[1] = 2;
    for (uint32_t k = 2; k < KITLING_ZECK_TIERS; k++) face[k] = face[k - 1] + face[k - 2];
    int64_t mask = 0, left = amount;
    for (uint32_t k = KITLING_ZECK_TIERS; k-- > 0;)
        if (face[k] <= left) {
            left -= face[k];
            mask |= INT64_C(1) << k;
        }
    return mask;
}

/* KR4 */
static inline int64_t kitling_strength(int64_t gene)
{
    if (gene < 0 || gene > KITLING_GENE_MAX) return KITLING_REFUSED;
    return (gene * 1000 + 127) / 255;
}

/* KR5: does x have at most `digits` digits, sign aside? */
static inline bool kitling_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool kitling_memory_ok(int64_t seq, int64_t stamp, int64_t emotion, int64_t intensity,
                                     int64_t tokens)
{
    return kitling_digits_fit(seq, 9) && kitling_digits_fit(stamp, 15) &&
           kitling_digits_fit(emotion, 1) && kitling_digits_fit(intensity, 1) &&
           kitling_digits_fit(tokens, 9);
}

#endif
