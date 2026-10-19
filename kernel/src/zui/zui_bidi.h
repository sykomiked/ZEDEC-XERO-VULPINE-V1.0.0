/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_bidi.h — the Unicode Bidirectional Algorithm (UAX #9) for zui text:
 * Hebrew, Arabic and every other right-to-left script laid out next to
 * left-to-right text, numbers and punctuation the way a reader expects.
 *
 *   B1 CLASSES. zui_bidi_class gives every code point its Bidi_Class from a
 *      compact range table generated from the Unicode 15.1 DerivedBidiClass
 *      data (unassigned code points take their block's default: R in the
 *      Hebrew blocks, AL in the Arabic ones, ...). The whole code space is
 *      covered: Latin, Greek, Cyrillic, Hebrew, Arabic, Syriac, Thaana, NKo,
 *      the Arabic presentation forms, digits, punctuation and every control.
 *   B2 ONE PARAGRAPH. zui_bidi_paragraph takes UTF-8 up to and including the
 *      first paragraph separator (P1) and returns the bytes it used, so a
 *      caller walks a text paragraph by paragraph. Bad UTF-8 is read as
 *      U+FFFD. More than ZUI_BIDI_MAX code points: the rest of the paragraph
 *      is left out and `truncated` is set.
 *   B3 PARAGRAPH LEVEL. ZUI_BIDI_LTR / ZUI_BIDI_RTL force it; ZUI_BIDI_AUTO
 *      takes it from the first strong character outside isolates (P2, P3).
 *   B4 EXPLICIT LEVELS (X1..X10): embeddings LRE/RLE, overrides LRO/RLO,
 *      PDF, isolates LRI/RLI/FSI and PDI, to a depth of 125, with the
 *      overflow counters of the spec. Characters X9 removes (the embedding
 *      controls and BN) keep a level for drawing (the one before them) and
 *      are marked in `removed`.
 *   B5 IMPLICIT LEVELS per isolating run sequence: weak types W1..W7,
 *      bracket pairs N0 (BD16, at most 63 open brackets deep, with the
 *      canonical equivalents of U+2329/U+232A), neutrals N1, N2, then I1, I2.
 *   B6 A LINE. zui_bidi_line applies L1 (separators, and whitespace and
 *      isolate controls before them or at the end of the line, return to
 *      the paragraph level) to a range of the paragraph and L2 (reverse
 *      each run from the highest level down to the lowest odd one) to give
 *      the visual order: order[v] is the logical index drawn v-th.
 *   B7 MIRRORING (L4). zui_bidi_mirror gives the mirrored glyph of a
 *      Bidi_Mirrored character (every paired bracket, the comparison signs,
 *      ...) from the BidiMirroring data; a character drawn at an odd level
 *      is drawn with it. zui_bidi_bracket gives a bracket's pair and kind.
 *   B8 zui_bidi_visual writes a line in visual order as UTF-8 with
 *      mirroring applied and the removed controls left out: what a plain
 *      left-to-right text drawer can show directly.
 *   B9 Integer only, no allocation, deterministic. The caller owns the
 *      zui_bidi_t (it is large: keep it static or in a long-lived struct).
 */
#ifndef ZXV_ZUI_BIDI_H
#define ZXV_ZUI_BIDI_H

#include <stdbool.h>
#include <stdint.h>

#ifndef ZUI_BIDI_MAX
#    define ZUI_BIDI_MAX 1024u /* code points in one paragraph (<= 65535) */
#endif
#define ZUI_BIDI_DEPTH    125u /* max_depth of the spec */
#define ZUI_BIDI_BRACKETS 63u  /* BD16 stack */

/* B1: Bidi_Class values. */
typedef enum {
    ZUI_BIDI_L = 0,
    ZUI_BIDI_R,
    ZUI_BIDI_AL,
    ZUI_BIDI_EN,
    ZUI_BIDI_ES,
    ZUI_BIDI_ET,
    ZUI_BIDI_AN,
    ZUI_BIDI_CS,
    ZUI_BIDI_NSM,
    ZUI_BIDI_BN,
    ZUI_BIDI_B,
    ZUI_BIDI_S,
    ZUI_BIDI_WS,
    ZUI_BIDI_ON,
    ZUI_BIDI_LRE,
    ZUI_BIDI_LRO,
    ZUI_BIDI_RLE,
    ZUI_BIDI_RLO,
    ZUI_BIDI_PDF,
    ZUI_BIDI_LRI,
    ZUI_BIDI_RLI,
    ZUI_BIDI_FSI,
    ZUI_BIDI_PDI,
    ZUI_BIDI_CLASS_COUNT
} zui_bidi_class_t;

/* B3 */
typedef enum { ZUI_BIDI_LTR = 0, ZUI_BIDI_RTL = 1, ZUI_BIDI_AUTO = 2 } zui_bidi_dir_t;

typedef struct {
    uint32_t n;         /* code points read                          */
    uint32_t bytes;     /* bytes read (B2)                           */
    uint8_t para_level; /* 0 or 1                                    */
    bool truncated;     /* B2                                        */
    uint32_t cp[ZUI_BIDI_MAX];
    uint32_t off[ZUI_BIDI_MAX + 1]; /* byte offset of each code point; off[n] = bytes */
    uint8_t cls[ZUI_BIDI_MAX];      /* B1: the original class         */
    uint8_t type[ZUI_BIDI_MAX];     /* the resolved type              */
    uint8_t level[ZUI_BIDI_MAX];    /* the resolved level (before L1) */
    bool removed[ZUI_BIDI_MAX];     /* B4: removed by X9              */
    /* working space */
    uint16_t match[ZUI_BIDI_MAX]; /* BD9: matching PDI / initiator (0xFFFF none) */
    uint16_t run[ZUI_BIDI_MAX];   /* level run of each character    */
    uint16_t seq[ZUI_BIDI_MAX];   /* one isolating run sequence     */
    uint16_t run_lo[ZUI_BIDI_MAX], run_hi[ZUI_BIDI_MAX]; /* first, last of a run */
    uint16_t pair_o[ZUI_BIDI_MAX / 2u + 1u], pair_c[ZUI_BIDI_MAX / 2u + 1u]; /* N0 */
} zui_bidi_t;

/* B1 */
zui_bidi_class_t zui_bidi_class(uint32_t cp);
const char *zui_bidi_class_name(zui_bidi_class_t c); /* "L", "AL", ... */
/* B7: the mirrored glyph, or cp itself when it has none. */
uint32_t zui_bidi_mirror(uint32_t cp);
/* B7: 1 = opening bracket, 2 = closing bracket, 0 = not one. *pair (may be
 * NULL) gets the other half. */
int zui_bidi_bracket(uint32_t cp, uint32_t *pair);

/* B2..B5: returns the bytes used (b->bytes). */
uint32_t zui_bidi_paragraph(zui_bidi_t *b, const char *utf8, uint32_t len, zui_bidi_dir_t dir);

/* B6: code points [start, end) of the paragraph as one line. levels[] (may
 * be NULL) gets each code point's level after L1, indexed from start;
 * order[] gets the logical indices (start-based, i.e. absolute) in visual
 * order. With keep_removed false the X9-removed characters are left out of
 * order[]. Returns the entries written to order[]. */
uint32_t zui_bidi_line(const zui_bidi_t *b, uint32_t start, uint32_t end, uint8_t *levels,
                       uint16_t *order, bool keep_removed);

/* B8: returns the bytes written (cut at a whole character, always
 * NUL-terminated when cap > 0). */
uint32_t zui_bidi_visual(const zui_bidi_t *b, uint32_t start, uint32_t end, char *out,
                         uint32_t cap);

/* UTF-8 code point helpers shared by zui_bidi, zui_shape and zui_locale
 * (zui_input.h has the byte-stepping ones). zui_utf8_decode reads one code
 * point at s[*i] (U+FFFD for a bad, overlong or surrogate sequence,
 * consuming one byte) and moves *i past it. zui_utf8_encode writes cp (at
 * most 4 bytes) and returns the length, 0 when it does not fit in cap. */
uint32_t zui_utf8_decode(const char *s, uint32_t len, uint32_t *i);
uint32_t zui_utf8_encode(uint32_t cp, char *out, uint32_t cap);

#endif /* ZXV_ZUI_BIDI_H */
