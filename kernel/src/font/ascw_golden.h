/* ascw_golden.h — ASCW Golden, ZXV's UI and system font: which face draws what
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root. The font files are not Apache: they are under the SIL
 * Open Font License 1.1 (assets/fonts/ascw-golden/OFL.txt).
 *
 * WHAT THIS IS
 * -----------
 * The faces live in assets/fonts/ascw-golden/ (installed as /fonts on a ZXV
 * system, Contents/Resources/Fonts in the Mac app). This module is the map
 * from text to those faces, generated from the fonts' own character maps by
 * tools/fonts/gen_ascw_golden.py (tables in ascw_golden_tables.c):
 *
 *   - ASCW Golden Latin II is the primary face: English, every Latin-script
 *     language, and digits, punctuation and symbols wherever it has them.
 *   - Every other script goes to the Golden member made for it. Han follows
 *     CLDR: zh-Hans Chinese Simplified, zh-Hant and yue Chinese Traditional,
 *     ja Japanese, ko Korean. Persian (fa) uses Golden Persian.
 *   - Adlam, N'Ko, Vai, Tifinagh and Osmanya have no Golden member; the ASCW
 *     sibling faces draw them. Anything else is left to the platform's fonts
 *     (docs/FONTS_COVERAGE.md lists what that is).
 *
 * Two ways in, for two kinds of caller:
 *   - by language: ascw_face_chain() gives a locale's face then Latin II, the
 *     order a renderer should try each character in;
 *   - by character: ascw_face_for_codepoint() gives the one face that draws a
 *     code point by default, for text whose language is not known.
 *
 * Freestanding: integer only, no libc, no allocation.
 */
#ifndef ZXV_ASCW_GOLDEN_H
#define ZXV_ASCW_GOLDEN_H

#include <stdint.h>
#include <stdbool.h>
#include "font.h"

typedef enum {
    ASCW_ROLE_PRIMARY = 0,   /* Latin II: the system face                     */
    ASCW_ROLE_SCRIPT = 1,    /* the Golden member for a script                */
    ASCW_ROLE_ALTERNATE = 2, /* a second member (Latin, Persian), by choice   */
    ASCW_ROLE_FALLBACK = 3,  /* an ASCW sibling where Golden has no member    */
    ASCW_ROLE_MONO = 4       /* the fixed-width member (code, terminals)      */
} ascw_role_t;

typedef struct {
    const char *file;    /* "ASCW-Golden-Latin2.ttf"                          */
    const char *family;  /* "ASCW Golden Latin II" (the font's name ID 1)     */
    const char *scripts; /* ISO 15924 codes it is THE face for, "Hans Hani"   */
    uint8_t role;        /* ascw_role_t                                       */
} ascw_face_t;

uint32_t ascw_face_count(void);
const ascw_face_t *ascw_face_at(int32_t i); /* NULL when out of range        */
int32_t ascw_face_find(const char *file);   /* by file name, or -1           */
int32_t ascw_primary(void);                 /* Latin II                      */

/* The face for an ISO 15924 script code ("Deva", "Hant"), or -1 when the set
 * has none. Case-sensitive, as ISO 15924 writes them. */
int32_t ascw_face_for_script(const char *iso15924);

/* The face for a locale: an exact-tag choice from the generated table (fa ->
 * Persian), trying shorter tags (fa-AF-x -> fa-AF -> fa), else the face for
 * `script` (the locale's ISO 15924 script, from i18n_locale_script()). -1 when
 * neither gives one. Tags are matched ignoring case, '_' as '-'. */
int32_t ascw_face_for_locale(const char *tag, const char *script);

/* The order to try faces in for a locale's text: its face, then Latin II.
 * Writes up to `max` indices to out and returns how many. */
uint32_t ascw_face_chain(const char *tag, const char *script, int32_t *out, uint32_t max);

/* The face that draws a code point by default, or -1 when no ASCW face has
 * it. Binary search over the generated range table. */
int32_t ascw_face_for_codepoint(uint32_t cp);

/* Register every face in a font registry, files under `dir` ("/fonts"), Latin
 * II first so it becomes the registry's fallback. Faces of a script that
 * script.h names are registered for it; the rest as SCRIPT_UNKNOWN, so their
 * assets are still listed. Returns the number registered, or -1 if `dir` is
 * too long for FONT_ASSET_LEN or the registry fills up. */
int32_t ascw_install(font_registry_t *r, const char *dir);

#endif /* ZXV_ASCW_GOLDEN_H */
