/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_shape.h — complex scripts at the character level: Arabic letters that
 * join, Devanagari syllables that reorder, and the grapheme clusters a caret
 * must never split.
 *
 * zui draws text with a plain font (src/font/truetype.c: one glyph per code
 * point, no glyph layout tables). These rules turn logical text into code points
 * such a font can show correctly, and tell the editor where a character
 * begins and ends as a reader sees it.
 *
 *   S1 JOINING TYPES. zui_shape_joining gives every code point its
 *      Joining_Type from the Unicode 15.1 ArabicShaping data: U (does not
 *      join), R (joins on its right, to the letter before it), L, D (both
 *      sides), C (join-causing: ZWJ, tatweel) or T (transparent: harakat and
 *      every other unlisted Mn, Me, Cf). Arabic, Syriac, NKo, Mandaic,
 *      Mongolian and the other joining scripts are all in the table.
 *   S2 FORMS. zui_shape_forms gives each character its contextual form:
 *      a D letter joined on both sides is medial, joined only to the letter
 *      before it final, only to the one after it initial, else isolated; an R
 *      letter is final or isolated; an L letter initial or isolated.
 *      Transparent characters are skipped when looking for neighbours and take
 *      no form. ZWNJ (U) breaks a join; ZWJ (C) makes one.
 *   S3 PRESENTATION FORMS. zui_shape_presentation maps an Arabic letter and a
 *      form to its code point in the Arabic Presentation Forms-A/B blocks
 *      (from the UnicodeData compatibility mappings); a letter with no such
 *      form keeps its own code point (a final-only letter asked for its
 *      initial form keeps the final one, ...).
 *   S4 LAM-ALEF. Lam directly followed by alef, alef with madda, hamza above
 *      or hamza below becomes one ligature (U+FEF5..U+FEFC), isolated when the
 *      lam does not join the letter before it, final when it does.
 *   S5 zui_shape_arabic applies S2..S4 to code points in logical order and
 *      writes the result, with map[k] the input index each output code point
 *      came from (the ligature maps to the lam), so the caret can go back.
 *      Shape first, then reorder with zui_bidi (presentation forms are AL).
 *      zui_shape_utf8 does the same for UTF-8 text.
 *   S6 GRAPHEME CLUSTERS (UAX #29 extended grapheme clusters, Unicode 15.1,
 *      rules GB1..GB999): CR LF stays together; Hangul syllable sequences;
 *      combining marks, ZWJ and spacing marks stay with their base; prepend
 *      characters with what follows; Indic conjuncts (GB9c: consonant,
 *      virama, consonant); emoji ZWJ sequences (GB11) and regional indicator
 *      flags in pairs (GB12, GB13). zui_grapheme_next / _prev give the next or
 *      previous boundary from a byte offset, so a caret never lands inside a
 *      cluster, and a delete or selection takes whole clusters.
 *   S7 DEVANAGARI SYLLABLES. zui_indic_syllable finds the syllable at an
 *      index: a consonant cluster (C [N] (H [ZWJ|ZWNJ] C [N])*) with its
 *      matras, halant and modifiers, an independent vowel with its signs, or
 *      one other character. It reports the base consonant (the last
 *      consonant of the cluster), a reph (RA HALANT at the start of a cluster
 *      with a consonant after it: drawn as a mark above the end of the
 *      syllable), a pre-base matra (I, U+093F, or prishthamatra E, U+094E)
 *      and whether the cluster ends in a halant (a dead consonant).
 *   S8 zui_indic_visual reorders each syllable for a plain font: the
 *      pre-base matra is moved in front of the consonant cluster it belongs
 *      to (in front of the reph too, which is drawn above). map[] as in S5.
 *   S9 Integer only, no allocation, deterministic. Code point arrays are the
 *      caller's; UTF-8 helpers are zui_bidi.h's.
 */
#ifndef ZXV_ZUI_SHAPE_H
#define ZXV_ZUI_SHAPE_H

#include <stdbool.h>
#include <stdint.h>

/* S1 */
typedef enum {
    ZUI_JOIN_U = 0,
    ZUI_JOIN_R,
    ZUI_JOIN_L,
    ZUI_JOIN_D,
    ZUI_JOIN_C,
    ZUI_JOIN_T
} zui_joining_t;

/* S2 */
typedef enum {
    ZUI_FORM_NONE = 0, /* does not join, or transparent */
    ZUI_FORM_ISOL,
    ZUI_FORM_FINA,
    ZUI_FORM_INIT,
    ZUI_FORM_MEDI
} zui_form_t;

zui_joining_t zui_shape_joining(uint32_t cp);
/* form[i] (zui_form_t) for each of n code points. Joining letters with no
 * neighbour to join are ZUI_FORM_ISOL; U, C and T characters NONE. */
void zui_shape_forms(const uint32_t *cp, uint32_t n, uint8_t *form);
/* S3 */
uint32_t zui_shape_presentation(uint32_t cp, zui_form_t f);

/* S5: returns the code points written to out (at most cap; map may be
 * NULL). in and out must not overlap. */
uint32_t zui_shape_arabic(const uint32_t *in, uint32_t n, uint32_t *out, uint16_t *map,
                          uint32_t cap);
/* S5 for UTF-8, streamed (no length limit). Returns the bytes written, cut
 * at a whole character; out is always NUL-terminated when cap > 0. */
uint32_t zui_shape_utf8(const char *in, uint32_t len, char *out, uint32_t cap);

/* ---- S6: grapheme clusters --------------------------------------------- */

typedef enum {
    ZUI_GCB_OTHER = 0,
    ZUI_GCB_CR,
    ZUI_GCB_LF,
    ZUI_GCB_CONTROL,
    ZUI_GCB_EXTEND,
    ZUI_GCB_ZWJ,
    ZUI_GCB_RI,
    ZUI_GCB_PREPEND,
    ZUI_GCB_SPACINGMARK,
    ZUI_GCB_L,
    ZUI_GCB_V,
    ZUI_GCB_T,
    ZUI_GCB_LV,
    ZUI_GCB_LVT
} zui_gcb_t;

typedef enum { ZUI_INCB_NONE = 0, ZUI_INCB_LINKER, ZUI_INCB_CONSONANT, ZUI_INCB_EXTEND } zui_incb_t;

zui_gcb_t zui_grapheme_break(uint32_t cp);
bool zui_extended_pictographic(uint32_t cp);
zui_incb_t zui_indic_conjunct_break(uint32_t cp);

/* Byte offset of the first boundary after `at` (len when at >= len). */
uint32_t zui_grapheme_next(const char *s, uint32_t len, uint32_t at);
/* Byte offset of the last boundary before `at` (0 when at == 0). Reads from
 * the start of s: RI pairs and emoji sequences need the context. */
uint32_t zui_grapheme_prev(const char *s, uint32_t len, uint32_t at);
bool zui_grapheme_is_boundary(const char *s, uint32_t len, uint32_t at);
uint32_t zui_grapheme_count(const char *s, uint32_t len);

/* ---- S7, S8: Devanagari --------------------------------------------------- */

typedef enum {
    ZUI_INDIC_X = 0, /* other                               */
    ZUI_INDIC_C,     /* consonant                           */
    ZUI_INDIC_V,     /* independent vowel                   */
    ZUI_INDIC_N,     /* nukta                               */
    ZUI_INDIC_H,     /* halant (virama)                     */
    ZUI_INDIC_M,     /* dependent vowel (matra)             */
    ZUI_INDIC_MPRE,  /* pre-base matra: I, prishthamatra E  */
    ZUI_INDIC_VM,    /* candrabindu, anusvara, visarga      */
    ZUI_INDIC_A,     /* stress and accent marks             */
    ZUI_INDIC_ZWJ,
    ZUI_INDIC_ZWNJ
} zui_indic_cat_t;

typedef enum { ZUI_SYL_OTHER = 0, ZUI_SYL_CONSONANT, ZUI_SYL_VOWEL, ZUI_SYL_BROKEN } zui_syl_kind_t;

typedef struct {
    uint32_t start, end; /* code point indices [start, end)               */
    uint8_t kind;        /* zui_syl_kind_t (BROKEN: a mark with no base)  */
    bool reph;           /* RA HALANT at start + C after it               */
    bool dead;           /* the cluster ends in a halant                  */
    int32_t base;        /* index of the base consonant, -1 none          */
    int32_t pre_matra;   /* index of the pre-base matra, -1 none          */
    uint32_t consonants; /* consonants in the cluster                     */
} zui_indic_syllable_t;

zui_indic_cat_t zui_indic_category(uint32_t cp);
/* The syllable starting at index `at` (at < n). Returns its end. */
uint32_t zui_indic_syllable(const uint32_t *cp, uint32_t n, uint32_t at, zui_indic_syllable_t *s);
/* S8: returns the code points written (at most cap; map may be NULL). */
uint32_t zui_indic_visual(const uint32_t *in, uint32_t n, uint32_t *out, uint16_t *map,
                          uint32_t cap);

#endif /* ZXV_ZUI_SHAPE_H */
