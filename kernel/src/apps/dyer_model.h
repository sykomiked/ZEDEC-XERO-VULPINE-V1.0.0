/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dyer_model.h — Dyer, a colour picker and palettes, as a model (zxm.h):
 * colours by number (RGB, HSL, hex text), conversions between them, the
 * contrast of two colours against the 450 and 300 pass marks, and palettes
 * kept in the Hoard. The rules are dyer_rules.h (DY1-DY6), the same ones
 * the app's KRON, Trebago, Eve and MYRA blocks run. No drawing, no
 * allocation, integers only.
 *
 * A colour is one number 0xRRGGBB (0..DYER_COLOR_MAX), 8 bits a channel.
 * zxvd has no method that reads a pixel off the screen, so the model takes
 * the colours it is given and picks none itself.
 *
 *   DM1 HEX TEXT. dyer_hex_parse reads "#rrggbb" or "#rgb" (each digit
 *       doubled), the '#' optional, the digits in either case; anything
 *       else is refused (ZXM_E_ARG). dyer_hex_text writes "#rrggbb" in
 *       lower case.
 *   DM2 A PALETTE is one UTF-8 text file of the Hoard,
 *       Palettes/<name>.palette: the line "dyer 1", then one line "#rrggbb"
 *       (lower case) for each swatch, in place order, every line ending in
 *       "\n". A name is 1..DYER_NAME_MAX letters, digits, '-' and '_'.
 *   DM3 READ BEFORE CHANGED. dyer_open reads a palette with files.read; one
 *       not made yet is an empty palette. A file that is not a palette
 *       (DM2), or has more than DYER_PLACES swatches, is refused
 *       (ZXM_E_PROTOCOL) and the palette is not open, so it is never
 *       written over. Swatches are added and removed only on an open
 *       palette (ZXM_E_STATE otherwise), in memory, each swatch checked
 *       against DY6 first.
 *   DM4 WRITTEN ON THE OWNER'S WORD. dyer_save checks every swatch and the
 *       whole text first, then asks for the owner's word: without it,
 *       ZXM_E_STATE and nothing is sent. With it, the Palettes folder is
 *       made when it is missing (files.mkdir) and the palette is written
 *       whole (files.write); a write zxvd refuses leaves the palette
 *       unsaved (dirty).
 *   DM5 PALETTES. dyer_list names the palettes in Palettes/ (files.list),
 *       in zxvd's order; none when the folder is not made yet. Names that
 *       are not DM2 names, and folders, are left out.
 *   DM6 CONTRAST (dyer_contrast): the luminance of each colour (DY3), the
 *       ratio x100 (DY4) and the pass marks met (DY5). Advice on
 *       legibility only: the owner chooses the colours. */
#ifndef DYER_MODEL_H
#define DYER_MODEL_H

#include "dyer_rules.h"
#include "zxm.h"

#define DYER_DIR      "Palettes"
#define DYER_EXT      ".palette"
#define DYER_NAME_MAX 32u
/* "Palettes/<name>.palette": the two sizeofs count the '/' and the NUL */
#define DYER_PATH_MAX (sizeof DYER_DIR + DYER_NAME_MAX + sizeof DYER_EXT)
#define DYER_HEX_LEN  7u    /* "#rrggbb" */
#define DYER_TEXT_MAX 1024u /* "dyer 1\n" and 64 lines of 8 bytes, with room */
#define DYER_LIST_MAX 64u
#define DYER_E_FILES  (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

typedef struct {
    int64_t h; /* 0..359 degrees */
    int64_t s; /* 0..1000 thousandths */
    int64_t l; /* 0..1000 thousandths */
} dyer_hsl_t;

typedef struct {
    int64_t fore_lum, back_lum; /* DY3, millionths */
    int64_t ratio;              /* DY4, x100 */
    int64_t grade;              /* DY5: 2 body text, 1 large text only, 0 neither */
} dyer_contrast_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;                                  /* a palette is open (DM3) */
    bool dirty;                                   /* changed since it was opened or saved */
    char name[DYER_NAME_MAX + 1];                 /* the open palette's name */
    int64_t color[DYER_PLACES];                   /* its swatches, place 0 first */
    uint32_t n;                                   /* swatches */
    char names[DYER_LIST_MAX][DYER_NAME_MAX + 1]; /* dyer_list (DM5) */
    uint32_t nnames;
} dyer_t;

/* Colours. dyer_join is DYER_REFUSED for a channel outside 0..255;
 * dyer_split refuses (ZXM_E_ARG) a colour outside 0..DYER_COLOR_MAX. */
int64_t dyer_join(int64_t r, int64_t g, int64_t b);
int dyer_split(int64_t color, int64_t *r, int64_t *g, int64_t *b);
/* DM1; out holds DYER_HEX_LEN + 1 bytes. */
int dyer_hex_parse(const char *text, int64_t *color);
int dyer_hex_text(int64_t color, char *out);
/* DY1 and DY2 over a whole colour. */
int dyer_to_hsl(int64_t color, dyer_hsl_t *hsl);
int dyer_from_hsl(const dyer_hsl_t *hsl, int64_t *color);
/* DM6: fore on back. */
int dyer_contrast(int64_t fore, int64_t back, dyer_contrast_t *c);
/* DM2: a palette's name. */
bool dyer_name_ok(const char *name);

/* Palettes (DM2-DM5). */
void dyer_init(dyer_t *d, zxm_link_t *link);
int dyer_list(dyer_t *d);
int dyer_open(dyer_t *d, const char *name);
int dyer_add(dyer_t *d, int64_t color);
int dyer_remove(dyer_t *d, uint32_t place);
int dyer_save(dyer_t *d, bool owner_confirmed);

#endif
