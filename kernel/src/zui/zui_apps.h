/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_apps.h — the 407 apps and their vector icons, as every screen sees them.
 *
 * One file carries the whole catalogue: assets/icons/apps/zxv-apps.zxap,
 * written by tools/zui/gen_appicons.py from assets/icons/apps/catalog.csv and
 * one SVG per app (converted to ZXVG, so every icon is vector). The bare-metal
 * shell embeds it; the hosted desktop and the Mac app read it from disk.
 *
 *   AP1 THE PACK (little-endian):
 *         0  "ZXAP"
 *         4  u16 version (1)
 *         6  u16 count
 *         8  u32 offset of the string area
 *        12  u32 total size
 *        16  count entries of 36 bytes:
 *              u16 number (1..count, in order), u8 layer (1..3), u8 0,
 *              u32 x6 string offsets (name, category, what it does, what it
 *                  is built from, its pillar languages, how it keeps
 *                  contradictions), each into the string area, NUL ended,
 *              u32 icon offset, u32 icon length (a ZXVG file, 4-aligned)
 *   AP2 zui_apps_bind checks everything once (header, every entry, every
 *       string ends inside the area, every icon passes zui_zxvg_check); a pack
 *       that fails is refused whole, so the readers below never see a bad one.
 *   AP3 Layers: 1 base modules (apps 1..144), 2 built from layer 1, 3 built
 *       from layer 2. Layer 1 icons are gold on the ground; layers 2 and 3
 *       keep their own colours (the icons are drawn that way already).
 *   AP4 Integer only, freestanding, no allocation: the pack stays where the
 *       caller put it and the strings point into it.
 */
#ifndef ZXV_ZUI_APPS_H
#define ZXV_ZUI_APPS_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_vec.h"

#define ZUI_APPS_VERSION 1u
#define ZUI_APPS_HEAD    16u
#define ZUI_APPS_ENTRY   36u
#define ZUI_APPS_MAX     4096u

typedef struct {
    uint32_t number; /* 1-based catalogue number            */
    uint32_t layer;  /* 1, 2 or 3                           */
    const char *name, *category, *does, *built_from, *pillars, *paraconsistent;
    const uint8_t *icon; /* ZXVG */
    uint32_t icon_len;
} zui_app_t;

typedef struct {
    const uint8_t *d;
    uint32_t n;
    uint32_t count;
    uint32_t strings; /* offset of the string area */
} zui_apps_t;

/* AP2: 0 when the pack is good and bound, -1 (and an empty catalogue) when not. */
int zui_apps_bind(zui_apps_t *a, const uint8_t *pack, uint32_t n);
uint32_t zui_apps_count(const zui_apps_t *a);
/* The app at 0-based index i (number i + 1). False past the end. */
bool zui_apps_get(const zui_apps_t *a, uint32_t i, zui_app_t *out);
/* The app called name (ASCII case-insensitive); -1 when there is none. */
int32_t zui_apps_find(const zui_apps_t *a, const char *name);
/* Count of apps in a layer (1..3; 0 means all). */
uint32_t zui_apps_in_layer(const zui_apps_t *a, uint32_t layer);
/* True when needle (ASCII case-insensitive) is in the app's name, category or
 * what it does; an empty needle matches everything. */
bool zui_apps_match(const zui_app_t *app, const char *needle);

/* Draw the icon fitted into box (aspect kept, centred) at opacity 0..256
 * (0 means full). 0, or -1 when there is nothing to draw. */
int zui_app_icon(zui_surface_t *s, zui_vec_work_t *w, const zui_app_t *app, zui_rect_t box,
                 uint32_t opacity);

#endif /* ZXV_ZUI_APPS_H */
