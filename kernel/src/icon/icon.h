/* icon.h — resolution-independent vector icons, themed and per-system
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHAT THIS IS
 * -----------
 * Every system, app and thing in ZXV has its own icon. An icon here is not a
 * bitmap — it is a short list of drawing ops (rect, disc, line, triangle) on a
 * 64x64 design grid, each op coloured by a THEME TOKEN rather than a raw RGB.
 * So an icon is tiny, renders crisp at any size, and RECOLOURS with the theme:
 * change the palette and every icon restyles at once (that is the customizable
 * interface, applied to iconography).
 *
 * "EVERYTHING HAS ITS OWN ICON" — LITERALLY
 * -----------------------------------------
 * Headline systems get hand-drawn icons in the house style (a dragon-eye
 * kernel, a wizard-hat for the mage suite, three overlapping fields for
 * Tri-Space, ...). Every OTHER module gets a PROCEDURAL icon deterministically
 * generated from its name, so there is no module without a distinct, on-palette
 * icon — icon_for() always returns one. Two different names produce two
 * different icons.
 *
 * Freestanding: integer only, no allocation. Renders into a caller-owned RGB
 * buffer (0x00RRGGBB per pixel).
 */
#ifndef ZXV_ICON_H
#define ZXV_ICON_H

#include <stdint.h>
#include <stdbool.h>
#include "../theme/theme.h"

#define ICON_GRID     64u
#define ICON_MAX_OPS  24u

typedef enum { ICON_RECT = 0, ICON_DISC, ICON_LINE, ICON_TRI } icon_shape_t;

/* one drawing op — coords are grid units 0..63; `color` is a theme_color_t */
typedef struct {
    uint8_t shape;
    uint8_t c[6];     /* RECT x,y,w,h | DISC cx,cy,r | LINE x0,y0,x1,y1 | TRI x0..y2 */
    uint8_t color;
} icon_op_t;

typedef struct {
    uint8_t   bg;        /* background theme slot (usually THEME_VOID)         */
    uint8_t   n_ops;
    icon_op_t op[ICON_MAX_OPS];
} icon_t;

/* Render `ic` into a size x size RGB buffer (buf[y*size + x] = 0x00RRGGBB),
 * resolving every op's colour through `theme`. */
void icon_render(const icon_t *ic, const theme_t *theme, uint32_t *buf, uint32_t size);

/* Fill `out` with the icon for `name`: a hand-drawn one if it exists, otherwise
 * a procedural one derived from the name. ALWAYS fills a valid icon. */
void icon_for(const char *name, icon_t *out);

/* True if `name` has a hand-drawn icon (vs a procedural one). */
bool icon_is_builtin(const char *name);

/* Deterministically generate a distinct on-palette icon from a name. */
void icon_procedural(const char *name, icon_t *out);

#endif /* ZXV_ICON_H */
