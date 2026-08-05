/* font.h — the font registry: script -> face resolution, and the glyph boundary
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHAT THIS IS
 * -----------
 * ZXV ships the ASCW font set (109 faces: the "Golden" family covers ~79
 * scripts one face each, plus the "Dragon" display faces for the house style).
 * This is the registry that installs those faces and RESOLVES text to them:
 * given the script of a run (from script.h) and a style preference, it returns
 * the face that should draw it. That resolution IS multi-language rendering —
 * an Arabic run gets the Arabic face, a Han run the Han face, the boot banner
 * the Dragon display face.
 *
 * THE GLYPH BOUNDARY, STATED HONESTLY
 * -----------------------------------
 * Turning a TrueType outline into pixels is a large subsystem of its own (a
 * glyf/cmap/loca parser and a scan-line rasterizer). It is NOT in this file,
 * and this file does not pretend it is. Instead:
 *   - a font_glyph_ops_t backend renders a real face's glyph when one is bound
 *     (this is where the TrueType rasterizer plugs in), and
 *   - a small BUILTIN 8x8 bitmap covers a diagnostic ASCII subset (the letters
 *     of ZXV, a digit, punctuation) so boot and version text render before that
 *     rasterizer exists.
 * font_glyph() returns false — never a fabricated box — for a codepoint it
 * cannot draw with the resources actually present. A caller can therefore tell
 * "rendered" from "no glyph available yet".
 *
 * Faces are installed as FILESYSTEM ASSETS by default (the loader mounts the
 * font directory and the manifest names each face); a face may instead carry
 * embedded bytes. The registry stores the handle, not the glyph data.
 *
 * Freestanding: integer only, no libc, no allocation.
 */
#ifndef ZXV_FONT_H
#define ZXV_FONT_H

#include <stdint.h>
#include <stdbool.h>
#include "script.h"

#define FONT_MAX_FACES   160u
#define FONT_FAMILY_LEN  32u
#define FONT_ASSET_LEN   96u

typedef enum {
    FONT_STYLE_REGULAR = 0,
    FONT_STYLE_DISPLAY,      /* headline / banner (the Dragon faces)   */
    FONT_STYLE_MONO,         /* terminal                                */
    FONT_STYLE_CEREMONIAL    /* Enochian / sigil display                */
} font_style_t;

typedef struct {
    bool          in_use;
    char          family[FONT_FAMILY_LEN];  /* "Golden", "Dragon", ...        */
    font_script_t script;
    font_style_t  style;
    char          asset[FONT_ASSET_LEN];    /* path on the ZXV filesystem     */
    const uint8_t *embedded;                /* or embedded bytes (else NULL)  */
    uint32_t      embedded_len;
} font_face_t;

/* A glyph rasterizer backend. raster() fills `bitmap` (w*h bytes, one byte per
 * pixel coverage 0..255) for `cp` in `face`, returns 0 on success, non-zero if
 * it cannot render that codepoint. With no backend, only the builtin ASCII
 * subset renders. This is where a TrueType rasterizer registers. */
typedef struct {
    int (*raster)(const font_face_t *face, uint32_t cp,
                  uint8_t *bitmap, uint32_t w, uint32_t h, void *ctx);
    void *ctx;
} font_glyph_ops_t;

typedef struct {
    font_face_t face[FONT_MAX_FACES];
    uint32_t    count;
    int16_t     def_for[SCRIPT__COUNT];     /* preferred face per script, -1  */
    int16_t     fallback;                   /* last-resort face, -1           */
    font_glyph_ops_t ops;
} font_registry_t;

void font_registry_init(font_registry_t *r);
void font_set_glyph_ops(font_registry_t *r, const font_glyph_ops_t *ops);

/* Install a face. Returns its index, or -1 if the table is full. The first
 * regular face registered for a script becomes that script's default; a Latin
 * face becomes the global fallback. Both can be overridden. */
int32_t font_register(font_registry_t *r, const char *family, font_script_t sc,
                      font_style_t style, const char *asset);
int32_t font_register_embedded(font_registry_t *r, const char *family, font_script_t sc,
                               font_style_t style, const uint8_t *bytes, uint32_t len);

bool font_set_default(font_registry_t *r, font_script_t sc, int32_t face);
bool font_set_fallback(font_registry_t *r, int32_t face);

/* Resolve a run's script (and a style preference) to a face index. Prefers a
 * face of the requested style for that script, then any face for that script,
 * then the script default, then the global fallback. Returns -1 only if
 * nothing at all is installed. */
int32_t font_resolve(const font_registry_t *r, font_script_t sc, font_style_t pref);

const font_face_t *font_get(const font_registry_t *r, int32_t face);

/* Render one codepoint into `bitmap` (w*h coverage bytes). Uses the bound
 * rasterizer if present; otherwise the builtin 8x8 diagnostic set when cp is in
 * it and w==h==8. Returns true iff a glyph was actually drawn; false means "no
 * glyph available" — the caller decides what to show, this never invents one. */
bool font_glyph(const font_registry_t *r, int32_t face, uint32_t cp,
                uint8_t *bitmap, uint32_t w, uint32_t h);

/* True if the builtin diagnostic set can draw this codepoint (ASCII subset). */
bool font_builtin_has(uint32_t cp);

/* Bind a parsed TrueType font (a ttf_font_t*, passed as void* to avoid a hard
 * header dependency) as the registry's glyph backend, so font_glyph() renders
 * real outlines through the TrueType rasteriser (src/font/truetype). This is
 * the single-font binding; per-face font selection (resolving each face to its
 * own file) is the boot integration on top of it. */
void font_bind_ttf(font_registry_t *r, void *ttf_font);

#endif /* ZXV_FONT_H */
