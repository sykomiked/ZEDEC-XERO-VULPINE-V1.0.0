/* script.h — multi-language text: UTF-8, Unicode scripts, and itemization
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHAT THIS IS
 * -----------
 * The hard part of multi-language support is not drawing glyphs — it is
 * deciding, for a run of mixed text, WHICH script each stretch belongs to and
 * WHICH DIRECTION it runs, so the right font is chosen and the characters are
 * laid out in the right order. A string can hold Latin, Arabic, Devanagari and
 * Han at once; "one font for everything" cannot render it and a naive
 * left-to-right layout mangles the Arabic.
 *
 * This module does exactly that, and only that: decode UTF-8, classify each
 * codepoint's Unicode script, and ITEMIZE a string into runs of a single
 * script with a single direction. Font resolution (script -> a registered
 * face) lives in font.h; glyph rasterization is a separate, staged layer. Kept
 * apart on purpose — itemization is pure, total, testable logic with no
 * dependency on any font being present.
 *
 * SCOPE, STATED HONESTLY
 * ----------------------
 * - Script coverage spans the ASCW font set (Latin, Greek, Cyrillic, Arabic,
 *   Hebrew, Syriac, Armenian, Georgian, Devanagari, Bengali, Tamil, Thai,
 *   Ethiopic, Cherokee, Canadian Aboriginal syllabics, Adlam, N'Ko, Vai,
 *   Tifinagh, Han, Kana, Hangul, and others) plus COMMON (digits, spaces,
 *   punctuation) and UNKNOWN. It is a broad, not exhaustive, table.
 * - Direction is script-level (LTR vs RTL). This is NOT the full Unicode
 *   Bidirectional Algorithm (UAX #9): it does not resolve neutrals, embeddings,
 *   or mirrored brackets across a paragraph. It is the layer beneath that, and
 *   the header says so rather than implying full bidi.
 * - Complex shaping (Indic reordering, Arabic joining) is the rasterizer's job
 *   and is not attempted here.
 *
 * Freestanding: integer only, no libc, no allocation.
 */
#ifndef ZXV_FONT_SCRIPT_H
#define ZXV_FONT_SCRIPT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SCRIPT_UNKNOWN = 0,
    SCRIPT_COMMON,          /* digits, spaces, punctuation — join their neighbours */
    SCRIPT_LATIN,
    SCRIPT_GREEK,
    SCRIPT_CYRILLIC,
    SCRIPT_ARMENIAN,
    SCRIPT_HEBREW,
    SCRIPT_ARABIC,
    SCRIPT_SYRIAC,
    SCRIPT_THAANA,
    SCRIPT_NKO,
    SCRIPT_DEVANAGARI,
    SCRIPT_BENGALI,
    SCRIPT_TAMIL,
    SCRIPT_THAI,
    SCRIPT_GEORGIAN,
    SCRIPT_HANGUL,
    SCRIPT_ETHIOPIC,
    SCRIPT_CHEROKEE,
    SCRIPT_CANADIAN_ABORIGINAL,
    SCRIPT_TIFINAGH,
    SCRIPT_VAI,
    SCRIPT_ADLAM,
    SCRIPT_HAN,
    SCRIPT_HIRAGANA,
    SCRIPT_KATAKANA,
    SCRIPT_AVESTAN,
    SCRIPT_ENOCHIAN,        /* private-use range used by the ASCW Enochian face */
    SCRIPT__COUNT
} font_script_t;

typedef enum { DIR_LTR = 0, DIR_RTL = 1 } font_dir_t;

/* One itemized run: a maximal stretch of one script and one direction. Offsets
 * are BYTE offsets into the original UTF-8 string. */
typedef struct {
    uint32_t start;         /* byte offset of the run                         */
    uint32_t len;           /* byte length of the run                         */
    font_script_t script;
    font_dir_t dir;
} font_run_t;

/* Decode one UTF-8 scalar at s[0..len). On success writes the codepoint to *cp
 * and the byte length to *adv, and returns true. On any malformed, overlong,
 * surrogate, or out-of-range sequence, writes U+FFFD to *cp, sets *adv to 1
 * (resynchronise one byte), and returns false — it never reads past `len` and
 * never stalls. */
bool font_utf8_next(const uint8_t *s, uint32_t len, uint32_t *cp, uint32_t *adv);

/* The Unicode script of a codepoint (broad table; see scope note). */
font_script_t font_script_of(uint32_t cp);

/* The writing direction of a script. */
font_dir_t font_script_dir(font_script_t s);

/* A stable human-readable name, matching the ASCW filename tokens where
 * possible ("Latin", "Arabic", "Devanagari", ...). */
const char *font_script_name(font_script_t s);

/* Reverse of font_script_name: map an ASCW filename token ("Bengali",
 * "Devanagari", ...) to a script, or SCRIPT_UNKNOWN. Case-sensitive, matching
 * the manifest the font installer produces. */
font_script_t font_script_from_name(const char *name);

/* Itemize a UTF-8 string into runs. COMMON codepoints (spaces, digits,
 * punctuation) join the current run rather than breaking it, so "abc, 123"
 * stays one Latin run and does not fragment. A COMMON codepoint that opens the
 * string is held until the first real script is seen. Returns the number of
 * runs written (capped at `max`); *truncated is set if runs were dropped. */
uint32_t font_itemize(const uint8_t *text, uint32_t len,
                      font_run_t *runs, uint32_t max, bool *truncated);

#endif /* ZXV_FONT_SCRIPT_H */
