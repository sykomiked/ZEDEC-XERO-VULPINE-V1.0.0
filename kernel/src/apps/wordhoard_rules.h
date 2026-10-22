/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wordhoard_rules.h — Wordhoard's rules in C, the reference its C++
 * (wasm), BASIC, KRON, Trebago and MYRA blocks are checked against
 * (blocks/wordhoard), with the rules it uses from nearer the core. Pure
 * integer functions, no state: the model (wordhoard_model.c) and the
 * language test (tests/wordhoard_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   WH1 A WORD AS NUMBERS. A word is a run of Unicode scalar values (code
 *       points 1..0x10FFFF, the surrogates 0xD800..0xDFFF never one). For
 *       a block it travels as four numbers of three code points each, 21
 *       bits a code point, the first in the highest field:
 *       c0 * 2^42 + c1 * 2^21 + c2. A field of 0 is no code point: the word
 *       ends at its first 0 field, so a word of 0..12 code points fits.
 *       A negative number, a field past 0x10FFFF, a surrogate or a code
 *       point after the end is refused.
 *   WH2 EDIT DISTANCE (edit_distance). The least count of insertions,
 *       deletions and substitutions of one code point each that turns one
 *       word into the other (Levenshtein), over code points, never bytes
 *       and never case-folded: 0 when they are equal. The model compares
 *       words of up to WORDHOARD_WORD_CPS code points; the witnessed block
 *       takes WH1's twelve.
 *   WH3 DID YOU MEAN. A headword is offered for a typed word of n code
 *       points when its distance is 1..limit, limit 1 for n up to 4, 2 up
 *       to 8 and 3 past that; at most WORDHOARD_SUGGEST are offered,
 *       nearest first, a tie in the index's order. Advice only: nothing
 *       is looked up until the owner picks one.
 *   WH4 SHARED SENSES (shared_senses). How many of a candidate's senses
 *       (sense, its synset numbers) are among a headword's (head): the
 *       count of pairs with sense(i) = head(j), each list holding a synset
 *       once, as a wordnet does (the model drops a repeat before it asks).
 *       A synset number is 1..999999999 (OEWN's 8-digit offset times 10
 *       plus its part of speech: 1 noun, 2 verb, 3 adjective, 4 adverb, 5
 *       adjective satellite); anything else is refused. Up to
 *       WORDHOARD_SENSES on each side.
 *   WH5 SYNONYM ORDER. The synonyms of a headword are the other headwords
 *       that share a sense with it, placed by assay.score_rank of their
 *       WH4 counts (assay_rules.h AY3): the most shared senses first,
 *       candidates of one count sharing a place, shown in the index's
 *       order within it.
 *   WH6 HEADWORD RECORD (headword_row_ok) fits ^ZXWORDHOARD(word, sense):
 *       word (the headword's name code, kindred_rules.h KR2) at most 18
 *       digits, sense (a synset number) 9, lang (its place in
 *       xlate.languages, 0 not known) 3, list (0 the owner's own, 1 the
 *       core pack, 2 the optional pack) 1 and stamp (its list's mtime,
 *       seconds since 1970) 12, sign aside.
 *   WH7 PREFIX. A prefix search walks the index in name-code order and
 *       keeps a headword when kindred.name_prefix_hit (KR3) says its code
 *       begins with the typed code; the model then holds the bytes to the
 *       typed prefix (ASCII case folded), since KR2 passes over spaces
 *       and codes every character past ASCII alike.
 *   WH8 A DEFINITION FROM THE MODEL is held (zxm.h M3) and shown as the
 *       swarm's, never as the dictionary's, until the owner accepts it
 *       with warren.approval_ok (warren_rules.h WR3): the owner's own
 *       session and the owner's approval, both exactly 1. */
#ifndef WORDHOARD_RULES_H
#define WORDHOARD_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "assay_rules.h"
#include "kindred_rules.h"
#include "warren_rules.h"

#define WORDHOARD_REFUSED    (-1)
#define WORDHOARD_CP_LAST    INT64_C(0x10FFFF)     /* WH1: the last code point */
#define WORDHOARD_FIELD_BITS 21u                   /* WH1: bits of one code point */
#define WORDHOARD_FIELD      (INT64_C(1) << 21)    /* WH1: 2^21 */
#define WORDHOARD_PACK_CPS   3u                    /* WH1: code points in one number */
#define WORDHOARD_PACKS      4u                    /* WH1: numbers for one word */
#define WORDHOARD_PACKED_CPS 12u                   /* WH1: 4 * 3 */
#define WORDHOARD_WORD_CPS   32u                   /* WH2: code points the model compares */
#define WORDHOARD_SUGGEST    5u                    /* WH3 */
#define WORDHOARD_SENSE_MAX  INT64_C(999999999)    /* WH4, WH6: 9 digits */
#define WORDHOARD_SENSES     64u                   /* WH4: senses on one side */
#define WORDHOARD_LANG_MAX   INT64_C(999)          /* WH6: 3 digits */
#define WORDHOARD_LIST_MAX   INT64_C(9)            /* WH6: 1 digit */
#define WORDHOARD_STAMP_MAX  INT64_C(999999999999) /* WH6: 12 digits */

/* WH1: a code point a word may hold */
static inline bool wordhoard_cp_ok(int64_t c)
{
    return c >= 1 && c <= WORDHOARD_CP_LAST && !(c >= 0xD800 && c <= 0xDFFF);
}

/* WH1: the code points of four packed numbers into cp (12 places); their
 * count 0..12, or WORDHOARD_REFUSED. */
static inline int64_t wordhoard_unpack(const int64_t *packed, uint32_t *cp)
{
    uint32_t n = 0;
    bool ended = false;
    if (!packed || !cp) return WORDHOARD_REFUSED;
    for (uint32_t k = 0; k < WORDHOARD_PACKS; k++) {
        int64_t v = packed[k];
        if (v < 0) return WORDHOARD_REFUSED;
        for (uint32_t f = 0; f < WORDHOARD_PACK_CPS; f++) {
            uint32_t shift = WORDHOARD_FIELD_BITS * (WORDHOARD_PACK_CPS - 1u - f);
            int64_t c = (v >> shift) & (WORDHOARD_FIELD - 1);
            if (c == 0) {
                ended = true;
                continue;
            }
            if (ended || !wordhoard_cp_ok(c)) return WORDHOARD_REFUSED;
            cp[n++] = (uint32_t) c;
        }
    }
    return n;
}

/* WH1: n code points (0..12) as four packed numbers; false when one is no
 * code point or there are too many. */
static inline bool wordhoard_pack(const uint32_t *cp, uint32_t n, int64_t *packed)
{
    if (!packed || n > WORDHOARD_PACKED_CPS || (!cp && n)) return false;
    for (uint32_t k = 0; k < WORDHOARD_PACKS; k++) packed[k] = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!wordhoard_cp_ok(cp[i])) return false;
        uint32_t shift = WORDHOARD_FIELD_BITS * (WORDHOARD_PACK_CPS - 1u - i % WORDHOARD_PACK_CPS);
        packed[i / WORDHOARD_PACK_CPS] |= (int64_t) cp[i] << shift;
    }
    return true;
}

/* WH2: the edit distance of a[0..na) and b[0..nb), each at most
 * WORDHOARD_WORD_CPS code points; WORDHOARD_REFUSED otherwise. */
static inline int64_t wordhoard_distance(const uint32_t *a, uint32_t na, const uint32_t *b,
                                         uint32_t nb)
{
    uint32_t row[WORDHOARD_WORD_CPS + 1u];
    if ((!a && na) || (!b && nb) || na > WORDHOARD_WORD_CPS || nb > WORDHOARD_WORD_CPS)
        return WORDHOARD_REFUSED;
    for (uint32_t i = 0; i < na; i++)
        if (!wordhoard_cp_ok(a[i])) return WORDHOARD_REFUSED;
    for (uint32_t j = 0; j < nb; j++)
        if (!wordhoard_cp_ok(b[j])) return WORDHOARD_REFUSED;
    for (uint32_t j = 0; j <= nb; j++) row[j] = j;
    for (uint32_t i = 1; i <= na; i++) {
        uint32_t diag = row[0];
        row[0] = i;
        for (uint32_t j = 1; j <= nb; j++) {
            uint32_t up = row[j];
            uint32_t best = diag + (a[i - 1u] != b[j - 1u]);
            if (up + 1u < best) best = up + 1u;
            if (row[j - 1u] + 1u < best) best = row[j - 1u] + 1u;
            row[j] = best;
            diag = up;
        }
    }
    return row[nb];
}

/* WH1 + WH2: the distance of two packed words (four numbers each), as the
 * block answers it. */
static inline int64_t wordhoard_packed_distance(const int64_t *a, const int64_t *b)
{
    uint32_t ca[WORDHOARD_PACKED_CPS], cb[WORDHOARD_PACKED_CPS];
    int64_t na = wordhoard_unpack(a, ca), nb = wordhoard_unpack(b, cb);
    if (na < 0 || nb < 0) return WORDHOARD_REFUSED;
    return wordhoard_distance(ca, (uint32_t) na, cb, (uint32_t) nb);
}

/* WH3: the farthest a headword may be from a word of n code points */
static inline int64_t wordhoard_suggest_limit(uint32_t n)
{
    if (n == 0u) return 0;
    if (n <= 4u) return 1;
    if (n <= 8u) return 2;
    return 3;
}

/* WH4 */
static inline int64_t wordhoard_shared_senses(const int64_t *head, uint32_t m, const int64_t *sense,
                                              uint32_t n)
{
    int64_t shared = 0;
    if ((!head && m) || (!sense && n) || m > WORDHOARD_SENSES || n > WORDHOARD_SENSES)
        return WORDHOARD_REFUSED;
    for (uint32_t j = 0; j < m; j++)
        if (head[j] < 1 || head[j] > WORDHOARD_SENSE_MAX) return WORDHOARD_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (sense[i] < 1 || sense[i] > WORDHOARD_SENSE_MAX) return WORDHOARD_REFUSED;
        for (uint32_t j = 0; j < m; j++) shared += sense[i] == head[j];
    }
    return shared;
}

/* WH6 */
static inline bool wordhoard_record_ok(int64_t word, int64_t sense, int64_t lang, int64_t list,
                                       int64_t stamp)
{
    return word >= -KINDRED_NAME_CODE_MAX && word <= KINDRED_NAME_CODE_MAX &&
           sense >= -WORDHOARD_SENSE_MAX && sense <= WORDHOARD_SENSE_MAX &&
           lang >= -WORDHOARD_LANG_MAX && lang <= WORDHOARD_LANG_MAX &&
           list >= -WORDHOARD_LIST_MAX && list <= WORDHOARD_LIST_MAX &&
           stamp >= -WORDHOARD_STAMP_MAX && stamp <= WORDHOARD_STAMP_MAX;
}

#endif
