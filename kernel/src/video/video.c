/* video.c — ZEDEC XERO pqOS Video/Graphics Driver Implementation
 *
 * Pure-software 2D rasterisation into a caller-supplied framebuffer. The
 * split here is deliberate and mirrors src/virtio/vring.c + arch/arm64:
 * everything that is arithmetic lives in this file and is tested against
 * literal pixel values; everything that must touch a display controller goes
 * through video_ops_t and returns VIDEO_ENODEV when no backend is bound.
 * Read the LIMITATIONS block at the top of video.h before trusting any
 * capability claimed by a function name.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "video.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ===================================================================
 * Local helpers — no libc, matching the house style of desktop.c/pterm.c
 * =================================================================== */

static void vid_memset(void *dst, int v, uint32_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void vid_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (max == 0) return;
    for (; i + 1 < max && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int64_t vid_abs64(int64_t v) { return v < 0 ? -v : v; }

/* dev->num_displays is a plain public field, so it is UNTRUSTED (LIMITATIONS
 * 12). This is the ONLY thing any loop over dev->displays[] may bound itself
 * by: two loops here once used the raw count and wrote past displays[3]. */
static uint32_t vid_ndisp(const video_device_t *dev) {
    uint32_t n = dev->num_displays;
    return n > VIDEO_MAX_DISPLAYS ? (uint32_t)VIDEO_MAX_DISPLAYS : n;
}

/* ===================================================================
 * 8x16 bitmap font
 *
 * THE INDEXING BUG THIS FILE REFUSES TO REPEAT: src/vbe/vbe.c holds a glyph
 * table whose entry 0 is SPACE (ASCII 32) but indexes it with the raw byte,
 * so 'A' (65) renders whatever sits at table row 65 — which in that table is
 * the '/' glyph. Here the table is explicitly ASCII-32 indexed and the ONLY
 * place that converts a character to a row is vid_glyph(), which subtracts
 * VIDEO_FONT_FIRST and falls back to a .notdef box for everything outside
 * 32..126. test_video.c draws 'A', 'a' and a control byte and asserts the
 * exact bit patterns, so a re-introduced off-by-32 fails the suite.
 *
 * Each row covers cell rows 1..12 of the 16-row cell (rows 0 and 13..15 are
 * always blank). Bit 0x80 is the leftmost of the 8 columns.
 * Glyphs 32..96 are the project's existing shapes (from vbe.c, which is
 * correct in that range); 97..126 are authored here because the donor table
 * has no lowercase at all — it duplicates the punctuation block instead.
 * =================================================================== */

#define VIDEO_FONT_ROWS      12   /* stored rows per glyph */
#define VIDEO_FONT_ROW0       1   /* first cell row a stored row maps to */
#define VIDEO_FONT_NOTDEF    (VIDEO_FONT_GLYPHS - 1)

static const uint8_t g_font[VIDEO_FONT_GLYPHS][VIDEO_FONT_ROWS] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /*  32  space */
    {0x00,0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00},  /*  33  ! */
    {0x00,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /*  34  quote */
    {0x00,0x66,0xFF,0xFF,0x66,0x66,0xFF,0xFF,0x66,0x00,0x00,0x00},  /*  35  # */
    {0x00,0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00,0x00,0x00,0x00},  /*  36  $ */
    {0x00,0x62,0x66,0x0C,0x18,0x30,0x66,0x46,0x00,0x00,0x00,0x00},  /*  37  % */
    {0x00,0x3C,0x66,0x3C,0x38,0x67,0x66,0x3B,0x00,0x00,0x00,0x00},  /*  38  & */
    {0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /*  39  apostrophe */
    {0x00,0x0E,0x1C,0x18,0x18,0x18,0x18,0x1C,0x0E,0x00,0x00,0x00},  /*  40  ( */
    {0x00,0x70,0x38,0x18,0x18,0x18,0x18,0x38,0x70,0x00,0x00,0x00},  /*  41  ) */
    {0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00},  /*  42  star */
    {0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00},  /*  43  + */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x0C,0x00},  /*  44  , */
    {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00},  /*  45  - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00},  /*  46  . */
    {0x00,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00},  /*  47  / */
    {0x00,0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  48  0 */
    {0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},  /*  49  1 */
    {0x00,0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00,0x00,0x00,0x00},  /*  50  2 */
    {0x00,0x7E,0x0C,0x18,0x0C,0x06,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  51  3 */
    {0x00,0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00,0x00,0x00,0x00},  /*  52  4 */
    {0x00,0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  53  5 */
    {0x00,0x3C,0x60,0x7C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  54  6 */
    {0x00,0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00,0x00,0x00,0x00},  /*  55  7 */
    {0x00,0x3C,0x66,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  56  8 */
    {0x00,0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00,0x00,0x00,0x00},  /*  57  9 */
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},  /*  58  : */
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x0C,0x00,0x00,0x00},  /*  59  ; */
    {0x00,0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},  /*  60  < */
    {0x00,0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00},  /*  61  = */
    {0x00,0x00,0x60,0x30,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00},  /*  62  > */
    {0x00,0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00,0x00,0x00,0x00},  /*  63  ? */
    {0x00,0x3C,0x66,0x6E,0x6E,0x60,0x60,0x3C,0x00,0x00,0x00,0x00},  /*  64  @ */
    {0x00,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x00,0x00,0x00,0x00},  /*  65  A */
    {0x00,0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},  /*  66  B */
    {0x00,0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  67  C */
    {0x00,0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00,0x00,0x00,0x00},  /*  68  D */
    {0x00,0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00,0x00,0x00,0x00},  /*  69  E */
    {0x00,0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00,0x00,0x00,0x00},  /*  70  F */
    {0x00,0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  71  G */
    {0x00,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00,0x00,0x00,0x00},  /*  72  H */
    {0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},  /*  73  I */
    {0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00,0x00,0x00,0x00},  /*  74  J */
    {0x00,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00,0x00,0x00,0x00},  /*  75  K */
    {0x00,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00,0x00,0x00},  /*  76  L */
    {0x00,0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00,0x00,0x00,0x00},  /*  77  M */
    {0x00,0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00,0x00,0x00,0x00},  /*  78  N */
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  79  O */
    {0x00,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00,0x00,0x00,0x00},  /*  80  P */
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x0E,0x00,0x00,0x00,0x00},  /*  81  Q */
    {0x00,0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00,0x00,0x00,0x00},  /*  82  R */
    {0x00,0x3C,0x60,0x60,0x3C,0x06,0x06,0x3C,0x00,0x00,0x00,0x00},  /*  83  S */
    {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},  /*  84  T */
    {0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  85  U */
    {0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},  /*  86  V */
    {0x00,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00,0x00,0x00,0x00},  /*  87  W */
    {0x00,0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00,0x00,0x00,0x00},  /*  88  X */
    {0x00,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00,0x00,0x00,0x00},  /*  89  Y */
    {0x00,0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00,0x00,0x00,0x00},  /*  90  Z */
    {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},  /*  91  [ */
    {0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x00,0x00,0x00},  /*  92  backslash */
    {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},  /*  93  ] */
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /*  94  ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00},  /*  95  _ */
    {0x00,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /*  96  ` */
    {0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x3B,0x00,0x00,0x00,0x00},  /*  97  a */
    {0x00,0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},  /*  98  b */
    {0x00,0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00,0x00,0x00,0x00},  /*  99  c */
    {0x00,0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00,0x00,0x00,0x00},  /* 100  d */
    {0x00,0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00,0x00,0x00,0x00},  /* 101  e */
    {0x00,0x1C,0x36,0x30,0x7C,0x30,0x30,0x30,0x00,0x00,0x00,0x00},  /* 102  f */
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x3C,0x00,0x00},  /* 103  g */
    {0x00,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},  /* 104  h */
    {0x00,0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},  /* 105  i */
    {0x00,0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00,0x00},  /* 106  j */
    {0x00,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00,0x00,0x00,0x00},  /* 107  k */
    {0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},  /* 108  l */
    {0x00,0x00,0x00,0x76,0x7F,0x6B,0x6B,0x63,0x00,0x00,0x00,0x00},  /* 109  m */
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},  /* 110  n */
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00},  /* 111  o */
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x00,0x00},  /* 112  p */
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x06,0x00,0x00},  /* 113  q */
    {0x00,0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x00,0x00,0x00,0x00},  /* 114  r */
    {0x00,0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00,0x00,0x00,0x00},  /* 115  s */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x1A,0x0C,0x00,0x00,0x00,0x00},  /* 116  t */
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00,0x00,0x00,0x00},  /* 117  u */
    {0x00,0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00},  /* 118  v */
    {0x00,0x00,0x00,0x63,0x6B,0x6B,0x7F,0x36,0x00,0x00,0x00,0x00},  /* 119  w */
    {0x00,0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00,0x00,0x00,0x00},  /* 120  x */
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x06,0x3C,0x00,0x00},  /* 121  y */
    {0x00,0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00,0x00,0x00,0x00},  /* 122  z */
    {0x00,0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},  /* 123  { */
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},  /* 124  | */
    {0x00,0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00,0x00,0x00,0x00},  /* 125  } */
    {0x00,0x00,0x00,0x00,0x3B,0x6E,0x00,0x00,0x00,0x00,0x00,0x00},  /* 126  ~ */
    {0x00,0x7E,0x42,0x42,0x42,0x42,0x42,0x7E,0x00,0x00,0x00,0x00},  /* .notdef */
};

/* The one and only character -> glyph mapping. */
static const uint8_t *vid_glyph(uint8_t c) {
    if (c < VIDEO_FONT_FIRST || c > VIDEO_FONT_LAST)
        return g_font[VIDEO_FONT_NOTDEF];
    return g_font[c - VIDEO_FONT_FIRST];
}

/* ===================================================================
 * Pixel formats
 * =================================================================== */

static uint32_t vid_fmt_bytes(pixel_format_t f) {
    switch (f) {
        case PIXEL_RGB332:   return 1;
        case PIXEL_RGB565:   return 2;
        case PIXEL_RGB888:   return 3;
        case PIXEL_XRGB8888: return 4;
        case PIXEL_ARGB8888: return 4;
        default:             return 0;
    }
}

static bool vid_fmt_valid(pixel_format_t f) { return vid_fmt_bytes(f) != 0; }

static bool vid_fmt_for_bpp(uint32_t bpp, pixel_format_t *out) {
    switch (bpp) {
        case 8:  *out = PIXEL_RGB332;   return true;
        case 16: *out = PIXEL_RGB565;   return true;
        case 24: *out = PIXEL_RGB888;   return true;
        case 32: *out = PIXEL_XRGB8888; return true;
        default: return false;
    }
}

/* canonical 0xAARRGGBB -> the format's raw little-endian value */
static uint32_t vid_pack(pixel_format_t f, uint32_t argb) {
    uint32_t r = (argb >> 16) & 0xFFu;
    uint32_t g = (argb >> 8) & 0xFFu;
    uint32_t b = argb & 0xFFu;
    switch (f) {
        case PIXEL_RGB332:   return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
        case PIXEL_RGB565:   return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        case PIXEL_RGB888:   return (r << 16) | (g << 8) | b;
        case PIXEL_XRGB8888: return argb & 0x00FFFFFFu;
        case PIXEL_ARGB8888: return argb;
        default:             return 0;
    }
}

/* raw -> canonical 0xAARRGGBB. Low bits are replicated from the high bits so
 * that full-scale stays full-scale (0x1F -> 0xFF, not 0xF8) and so that
 * vid_pack(vid_unpack(raw)) == raw for every raw value: a blit through the
 * canonical form never drifts. */
static uint32_t vid_unpack(pixel_format_t f, uint32_t raw) {
    switch (f) {
        case PIXEL_RGB332: {
            uint32_t r = (raw >> 5) & 0x7u, g = (raw >> 2) & 0x7u, b = raw & 0x3u;
            uint32_t r8 = (r << 5) | (r << 2) | (r >> 1);
            uint32_t g8 = (g << 5) | (g << 2) | (g >> 1);
            uint32_t b8 = (b << 6) | (b << 4) | (b << 2) | b;
            return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
        }
        case PIXEL_RGB565: {
            uint32_t r = (raw >> 11) & 0x1Fu, g = (raw >> 5) & 0x3Fu, b = raw & 0x1Fu;
            uint32_t r8 = (r << 3) | (r >> 2);
            uint32_t g8 = (g << 2) | (g >> 4);
            uint32_t b8 = (b << 3) | (b >> 2);
            return 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
        }
        case PIXEL_RGB888:   return 0xFF000000u | (raw & 0x00FFFFFFu);
        case PIXEL_XRGB8888: return 0xFF000000u | (raw & 0x00FFFFFFu);
        case PIXEL_ARGB8888: return raw;
        default:             return 0;
    }
}

static void vid_store(uint8_t *p, uint32_t bytes, uint32_t raw) {
    for (uint32_t i = 0; i < bytes; i++) p[i] = (uint8_t)(raw >> (8u * i));
}

static uint32_t vid_load(const uint8_t *p, uint32_t bytes) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < bytes; i++) v |= (uint32_t)p[i] << (8u * i);
    return v;
}

/* src over dst with a constant coverage `a` in 0..255, rounded to nearest.
 * a == 0 keeps the destination exactly; a == 255 replaces it exactly. */
static uint32_t vid_blend(uint32_t src, uint32_t dst, uint32_t a) {
    if (a >= 255u) return 0xFF000000u | (src & 0x00FFFFFFu);
    if (a == 0u)   return dst;
    uint32_t ia = 255u - a;
    uint32_t r = (((src >> 16) & 0xFFu) * a + ((dst >> 16) & 0xFFu) * ia + 127u) / 255u;
    uint32_t g = (((src >> 8) & 0xFFu) * a + ((dst >> 8) & 0xFFu) * ia + 127u) / 255u;
    uint32_t b = ((src & 0xFFu) * a + (dst & 0xFFu) * ia + 127u) / 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* ===================================================================
 * Surfaces
 * =================================================================== */

typedef struct {
    uint8_t *base;
    uint32_t pitch;
    uint32_t limit;                 /* addressable bytes from base */
    int32_t  w, h;
    pixel_format_t fmt;
    uint32_t bytes;
    int32_t  cx0, cy0, cx1, cy1;    /* clip, half-open, already inside [0,w)x[0,h) */
    bool     blend;
    uint32_t alpha;
} vsurf_t;

/* Resolve the draw target of the 2D context. Returns false — and therefore
 * draws nothing at all — when there is no display, no framebuffer, or the
 * device is in text mode (LIMITATIONS 9). */
static bool vid_target(video_device_t *dev, vsurf_t *s) {
    if (!dev) return false;
    if (dev->mode == VIDEO_MODE_TEXT_80x25) return false;

    video_display_t *d = video_get_display(dev, dev->ctx.display_id);
    if (!d || !d->active) return false;
    if (d->width == 0 || d->height == 0 || d->pitch == 0) return false;
    if (!vid_fmt_valid(d->format)) return false;

    uint8_t *base = (uint8_t *)((d->double_buffered && d->backbuffer)
                                ? d->backbuffer : d->framebuffer);
    if (!base) return false;

    uint32_t bytes = vid_fmt_bytes(d->format);
    /* 64-bit throughout: pitch and width are public fields, so their product
     * must not be allowed to wrap into a value that passes this test. */
    if ((uint64_t)d->pitch < (uint64_t)d->width * bytes) return false;
    uint64_t page = (uint64_t)d->pitch * d->height;
    if (page == 0 || page > (uint64_t)d->fb_size) return false;

    s->base  = base;
    s->pitch = d->pitch;
    s->limit = (uint32_t)page;      /* one page; the other page starts after it */
    s->w     = (int32_t)d->width;
    s->h     = (int32_t)d->height;
    s->fmt   = d->format;
    s->bytes = bytes;
    s->blend = dev->ctx.alpha_blending;
    s->alpha = dev->ctx.alpha;

    /* Intersect the requested clip with the surface. A negative or zero
     * extent yields an empty box, and every loop below then writes nothing. */
    int64_t x0 = dev->ctx.clip_x;
    int64_t y0 = dev->ctx.clip_y;
    int64_t x1 = x0 + (dev->ctx.clip_w > 0 ? dev->ctx.clip_w : 0);
    int64_t y1 = y0 + (dev->ctx.clip_h > 0 ? dev->ctx.clip_h : 0);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    s->cx0 = (int32_t)x0; s->cy0 = (int32_t)y0;
    s->cx1 = (int32_t)x1; s->cy1 = (int32_t)y1;
    return true;
}

/* Read without regard to the clip box (clipping restricts writes only). */
static bool vid_peek(const vsurf_t *s, int32_t x, int32_t y, uint32_t *out) {
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return false;
    uint32_t off = (uint32_t)y * s->pitch + (uint32_t)x * s->bytes;
    if (off + s->bytes > s->limit) return false;
    *out = vid_unpack(s->fmt, vid_load(s->base + off, s->bytes));
    return true;
}

static void vid_poke(video_device_t *dev, const vsurf_t *s,
                     int32_t x, int32_t y, uint32_t argb) {
    if (x < s->cx0 || x >= s->cx1 || y < s->cy0 || y >= s->cy1) {
        dev->stat_pixels_clipped++;
        return;
    }
    uint32_t off = (uint32_t)y * s->pitch + (uint32_t)x * s->bytes;
    if (off + s->bytes > s->limit) {   /* belt and braces: never leave the page */
        dev->stat_pixels_clipped++;
        return;
    }
    uint8_t *p = s->base + off;
    if (s->blend) {
        uint32_t dstc = vid_unpack(s->fmt, vid_load(p, s->bytes));
        argb = vid_blend(argb, dstc, s->alpha);
    }
    vid_store(p, s->bytes, vid_pack(s->fmt, argb));
    dev->stat_pixels_written++;
}

/* Reset the clip to the whole of the context display. */
static void vid_reset_clip(video_device_t *dev) {
    video_display_t *d = video_get_display(dev, dev->ctx.display_id);
    dev->ctx.clip_x = 0;
    dev->ctx.clip_y = 0;
    dev->ctx.clip_w = d ? (int32_t)d->width : 0;
    dev->ctx.clip_h = d ? (int32_t)d->height : 0;
}

/* ===================================================================
 * Init / capabilities
 * =================================================================== */

void video_init(video_device_t *dev, const char *name) {
    if (!dev) return;                     /* arch/arm32 calls video_init(0, ...) */
    vid_memset(dev, 0, (uint32_t)sizeof(*dev));

    dev->device_id = 1;
    vid_strcpy(dev->name, name ? name : "video0", (uint32_t)sizeof(dev->name));

    /* HEADLESS is the honest default: nothing has been programmed, and a
     * framebuffer in RAM is exactly what this file can drive by itself. */
    dev->mode = VIDEO_MODE_HEADLESS;
    dev->reg_mode = (uint32_t)VIDEO_MODE_HEADLESS;

    dev->supports_2d_accel     = true;    /* this software rasteriser */
    dev->supports_3d           = false;   /* LIMITATIONS 2: no 3D pipeline */
    dev->supports_multi_display = true;
    dev->max_width  = VIDEO_DEFAULT_MAX_W;
    dev->max_height = VIDEO_DEFAULT_MAX_H;

    dev->ctx.display_id = 0;
    dev->ctx.fg_color = 0xFFFFFFFFu;
    dev->ctx.bg_color = 0xFF000000u;
    dev->ctx.alpha = 255;
    dev->ctx.alpha_blending = false;

    dev->m5.omega = 1;
    dev->m5.r   = SR_ONE;
    dev->m5.ell = SR_ONE;
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_r = 0.0;
    dev->coverage_l = 0.0;
}

int video_bind_ops(video_device_t *dev, const video_ops_t *ops) {
    if (!dev || !ops) return VIDEO_EINVAL;
    /* An ops struct with no callbacks is not a backend. Accepting it would
     * turn every honest VIDEO_ENODEV into a silent success. */
    if (!ops->set_mode && !ops->flip && !ops->vsync_wait && !ops->probe)
        return VIDEO_EINVAL;
    dev->ops = *ops;
    dev->ops_bound = true;
    return VIDEO_OK;
}

void video_unbind_ops(video_device_t *dev) {
    if (!dev) return;
    vid_memset(&dev->ops, 0, (uint32_t)sizeof(dev->ops));
    dev->ops_bound = false;
    uint32_t n = vid_ndisp(dev);
    for (uint32_t i = 0; i < n; i++)
        dev->displays[i].scanout_live = false;   /* nothing drives them now */
}

bool video_has_backend(const video_device_t *dev) {
    return dev && dev->ops_bound;
}

/* ===================================================================
 * Displays
 * =================================================================== */

video_display_t *video_get_display(video_device_t *dev, uint32_t display_id) {
    if (!dev || display_id == 0) return NULL;
    uint32_t n = vid_ndisp(dev);                          /* bound a corrupt count */
    for (uint32_t i = 0; i < n; i++)
        if (dev->displays[i].display_id == display_id) return &dev->displays[i];
    return NULL;
}

uint32_t video_add_display(video_device_t *dev, uint32_t w, uint32_t h, uint32_t bpp) {
    if (!dev) return 0;
    if (dev->num_displays >= VIDEO_MAX_DISPLAYS) return 0;
    if (dev->num_displays > 0 && !dev->supports_multi_display) return 0;
    if (w == 0 || h == 0) return 0;
    if (w > dev->max_width || h > dev->max_height) return 0;
    /* max_width/max_height are public fields a caller can raise; this one is
     * not, and it is what keeps pitch and pitch*height inside 32 bits. */
    if (w > VIDEO_HARD_MAX_DIM || h > VIDEO_HARD_MAX_DIM) return 0;

    pixel_format_t fmt;
    if (!vid_fmt_for_bpp(bpp, &fmt)) return 0;

    uint32_t idx = dev->num_displays++;
    video_display_t *d = &dev->displays[idx];
    vid_memset(d, 0, (uint32_t)sizeof(*d));
    d->display_id = idx + 1;
    d->active = true;
    d->width = w;
    d->height = h;
    d->bpp = bpp;
    d->format = fmt;
    d->pitch = w * vid_fmt_bytes(fmt);
    d->page_bytes = d->pitch * h;
    d->dpi = 96;
    d->refresh_hz = 60;
    d->scanout_live = false;      /* nothing has programmed a CRTC */

    if (dev->primary_display == 0) {
        dev->primary_display = d->display_id;
        dev->ctx.display_id = d->display_id;
        vid_reset_clip(dev);
    }
    return d->display_id;
}

int video_set_primary(video_device_t *dev, uint32_t display_id) {
    if (!dev) return VIDEO_EINVAL;
    video_display_t *d = video_get_display(dev, display_id);
    if (!d) return VIDEO_ENODISPLAY;
    if (!d->active) return VIDEO_EINVAL;
    dev->primary_display = display_id;
    dev->ctx.display_id = display_id;
    vid_reset_clip(dev);
    return VIDEO_OK;
}

/* ===================================================================
 * Mode / geometry / buffers
 * =================================================================== */

int video_set_mode(video_device_t *dev, video_mode_t mode) {
    if (!dev) return VIDEO_EINVAL;
    switch (mode) {
        case VIDEO_MODE_TEXT_80x25:
        case VIDEO_MODE_VBE:
        case VIDEO_MODE_FRAMEBUFFER:
        case VIDEO_MODE_HEADLESS:
            break;
        default:
            return VIDEO_EINVAL;
    }

    dev->mode = mode;
    dev->reg_mode = (uint32_t)mode;
    /* A mode change invalidates any previous scanout claim. */
    uint32_t n = vid_ndisp(dev);
    for (uint32_t i = 0; i < n; i++)
        dev->displays[i].scanout_live = false;

    video_display_t *d = video_get_display(dev, dev->primary_display);

    /* Every mode but HEADLESS is a claim about a panel, and a claim about a
     * panel needs a panel. Returning VIDEO_OK here just because a backend
     * happened to be bound — while skipping the callback entirely, which is
     * what used to happen with zero displays — is exactly the hollow success
     * this subsystem is supposed to refuse. */
    if (mode != VIDEO_MODE_HEADLESS && !d) return VIDEO_ENODISPLAY;

    if (dev->ops_bound && dev->ops.set_mode && d) {
        int r = dev->ops.set_mode(dev->ops.ctx, d->display_id, d->width,
                                  d->height, d->bpp, d->format);
        if (r != 0) return VIDEO_EBACKEND;
        d->scanout_live = true;
        return VIDEO_OK;
    }

    /* No backend (or nothing to program). The software mode is recorded —
     * that part is real — but no CRTC was touched, and only HEADLESS can
     * honestly call that complete. */
    return (mode == VIDEO_MODE_HEADLESS) ? VIDEO_OK : VIDEO_ENOSCANOUT;
}

int video_set_resolution(video_device_t *dev, uint32_t display_id,
                         uint32_t w, uint32_t h, uint32_t bpp, pixel_format_t fmt) {
    if (!dev) return VIDEO_EINVAL;
    video_display_t *d = video_get_display(dev, display_id);
    if (!d) return VIDEO_ENODISPLAY;
    if (w == 0 || h == 0) return VIDEO_EINVAL;
    if (w > dev->max_width || h > dev->max_height) return VIDEO_EINVAL;
    if (w > VIDEO_HARD_MAX_DIM || h > VIDEO_HARD_MAX_DIM) return VIDEO_EINVAL;
    if (!vid_fmt_valid(fmt)) return VIDEO_EINVAL;
    if (bpp != vid_fmt_bytes(fmt) * 8u) return VIDEO_EINVAL;

    uint32_t pitch = w * vid_fmt_bytes(fmt);
    uint64_t page  = (uint64_t)pitch * h;

    /* If a buffer is already bound it must still fit, otherwise the first
     * put_pixel would run off the end. Refuse and keep the old geometry. */
    if (d->fb_base) {
        uint64_t need = d->double_buffered ? page * 2u : page;
        if (need > (uint64_t)d->fb_size) return VIDEO_ENOSPC;
    }

    d->width = w;
    d->height = h;
    d->bpp = bpp;
    d->format = fmt;
    d->pitch = pitch;
    d->page_bytes = (uint32_t)page;
    d->scanout_live = false;
    if (d->fb_base) {
        d->framebuffer = d->fb_base;                              /* un-flip */
        d->backbuffer = d->double_buffered
                        ? (void *)((uint8_t *)d->fb_base + page) : NULL;
    }

    dev->reg_resolution = (w << 16) | (h & 0xFFFFu);
    dev->reg_scanline = pitch;
    if (dev->ctx.display_id == d->display_id) vid_reset_clip(dev);

    if (dev->ops_bound && dev->ops.set_mode) {
        int r = dev->ops.set_mode(dev->ops.ctx, display_id, w, h, bpp, fmt);
        if (r != 0) return VIDEO_EBACKEND;
        d->scanout_live = true;
    }
    /* VIDEO_OK here means "the surface description is valid and drawing will
     * use it". It does NOT mean a monitor changed mode — see LIMITATIONS 1. */
    return VIDEO_OK;
}

int video_set_framebuffer(video_device_t *dev, uint32_t display_id, void *fb, uint32_t size) {
    if (!dev) return VIDEO_EINVAL;
    video_display_t *d = video_get_display(dev, display_id);
    if (!d) return VIDEO_ENODISPLAY;

    if (!fb || size == 0) {                 /* unbind */
        d->fb_base = NULL;
        d->framebuffer = NULL;
        d->backbuffer = NULL;
        d->fb_size = 0;
        d->double_buffered = false;
        d->scanout_live = false;
        return VIDEO_OK;
    }

    if (d->width == 0 || d->height == 0 || d->pitch == 0) return VIDEO_EINVAL;
    uint64_t page = (uint64_t)d->pitch * d->height;
    if (page == 0 || (uint64_t)size < page) return VIDEO_ENOSPC;

    d->fb_base = fb;
    d->framebuffer = fb;
    d->fb_size = size;
    d->page_bytes = (uint32_t)page;
    if ((uint64_t)size >= page * 2u) {
        /* Two full pages were supplied, so double buffering is real here. */
        d->backbuffer = (void *)((uint8_t *)fb + page);
        d->double_buffered = true;
    } else {
        d->backbuffer = NULL;
        d->double_buffered = false;
    }
    return VIDEO_OK;
}

void *video_get_framebuffer(video_device_t *dev, uint32_t display_id) {
    video_display_t *d = video_get_display(dev, display_id);
    return d ? d->framebuffer : NULL;
}

int video_flip(video_device_t *dev, uint32_t display_id) {
    if (!dev) return VIDEO_EINVAL;
    video_display_t *d = video_get_display(dev, display_id);
    if (!d) return VIDEO_ENODISPLAY;
    /* A connector a probe already reported gone must not be able to re-acquire
     * a scanout claim through a flip acknowledgement. */
    if (!d->active) return VIDEO_ENODISPLAY;
    if (!d->double_buffered || !d->backbuffer || !d->framebuffer) return VIDEO_ENOBUF;

    /* The page swap is real, happens here, and is observable through
     * video_get_framebuffer(). Nothing below can undo it. */
    void *t = d->framebuffer;
    d->framebuffer = d->backbuffer;
    d->backbuffer = t;
    dev->stat_page_flips++;

    if (dev->ops_bound && dev->ops.flip) {
        int r = dev->ops.flip(dev->ops.ctx, display_id, d->framebuffer, d->pitch);
        if (r != 0) {
            d->scanout_live = false;
            return VIDEO_EBACKEND;
        }
        d->scanout_live = true;
        dev->stat_scanout_flips++;      /* only a backend ack counts here */
        return VIDEO_OK;
    }

    if (dev->mode == VIDEO_MODE_HEADLESS) return VIDEO_OK;
    return VIDEO_ENOSCANOUT;            /* swapped in RAM; no panel involved */
}

int video_vsync_wait(video_device_t *dev, uint32_t display_id) {
    if (!dev) return VIDEO_EINVAL;
    video_display_t *d = video_get_display(dev, display_id);
    if (!d) return VIDEO_ENODISPLAY;
    if (!d->active) return VIDEO_ENODISPLAY;   /* nothing there to retrace */

    /* There is no software substitute for a scanline retrace. Without a
     * backend this returns an error rather than a busy-wait that pretends. */
    if (!dev->ops_bound || !dev->ops.vsync_wait) return VIDEO_ENODEV;

    int r = dev->ops.vsync_wait(dev->ops.ctx, display_id);
    if (r != 0) return VIDEO_EBACKEND;
    dev->stat_vsync_waits++;
    d->vsync = true;
    return VIDEO_OK;
}

/* ===================================================================
 * 2D state
 * =================================================================== */

void video_set_color(video_device_t *dev, uint32_t fg, uint32_t bg) {
    if (!dev) return;
    dev->ctx.fg_color = fg;
    dev->ctx.bg_color = bg;
}

void video_set_clip(video_device_t *dev, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!dev) return;
    dev->ctx.clip_x = x;
    dev->ctx.clip_y = y;
    dev->ctx.clip_w = w > 0 ? w : 0;
    dev->ctx.clip_h = h > 0 ? h : 0;
}

bool video_read_pixel(video_device_t *dev, uint32_t display_id,
                      int32_t x, int32_t y, uint32_t *out) {
    if (!dev || !out) return false;
    /* LIMITATIONS 9: in text mode the buffer holds character cells. Reporting
     * a "pixel" out of it would be fiction, and it is the same fiction
     * vid_target() already refuses to write. */
    if (dev->mode == VIDEO_MODE_TEXT_80x25) return false;
    video_display_t *d = video_get_display(dev, display_id);
    if (!d || !d->active) return false;
    if (!vid_fmt_valid(d->format) || d->pitch == 0) return false;
    if (d->width == 0 || d->height == 0) return false;
    uint8_t *base = (uint8_t *)((d->double_buffered && d->backbuffer)
                                ? d->backbuffer : d->framebuffer);
    if (!base) return false;

    uint32_t bytes = vid_fmt_bytes(d->format);
    if ((uint64_t)d->pitch < (uint64_t)d->width * bytes) return false;
    uint64_t page = (uint64_t)d->pitch * d->height;
    if (page == 0 || page > (uint64_t)d->fb_size) return false;

    vsurf_t s;
    s.base = base;
    s.pitch = d->pitch;
    s.limit = (uint32_t)page;
    s.w = (int32_t)d->width;
    s.h = (int32_t)d->height;
    s.fmt = d->format;
    s.bytes = bytes;
    s.cx0 = s.cy0 = 0; s.cx1 = s.w; s.cy1 = s.h;
    s.blend = false; s.alpha = 255;
    return vid_peek(&s, x, y, out);
}

/* ===================================================================
 * Primitives
 * =================================================================== */

void video_put_pixel(video_device_t *dev, int32_t x, int32_t y, uint32_t color) {
    vsurf_t s;
    if (!vid_target(dev, &s)) return;
    vid_poke(dev, &s, x, y, color);
}

void video_fill_rect(video_device_t *dev, int32_t x, int32_t y,
                     int32_t w, int32_t h, uint32_t color) {
    vsurf_t s;
    if (!vid_target(dev, &s)) return;
    if (w <= 0 || h <= 0) return;

    /* int64 throughout: x + w overflows int32 for hostile inputs. */
    int64_t x0 = x, y0 = y;
    int64_t x1 = (int64_t)x + w, y1 = (int64_t)y + h;
    int64_t requested = (int64_t)w * (int64_t)h;

    if (x0 < s.cx0) x0 = s.cx0;
    if (y0 < s.cy0) y0 = s.cy0;
    if (x1 > s.cx1) x1 = s.cx1;
    if (y1 > s.cy1) y1 = s.cy1;

    int64_t drawn = 0;
    for (int64_t py = y0; py < y1; py++)
        for (int64_t px = x0; px < x1; px++) {
            vid_poke(dev, &s, (int32_t)px, (int32_t)py, color);
            drawn++;
        }
    /* Everything asked for that did not land was clipped away. */
    if (requested > drawn) dev->stat_pixels_clipped += (uint64_t)(requested - drawn);
}

void video_draw_line(video_device_t *dev, int32_t x1, int32_t y1,
                     int32_t x2, int32_t y2, uint32_t color) {
    vsurf_t s;
    if (!vid_target(dev, &s)) return;

    int64_t dx = vid_abs64((int64_t)x2 - (int64_t)x1);
    int64_t dy = vid_abs64((int64_t)y2 - (int64_t)y1);
    int64_t total = (dx > dy ? dx : dy) + 1;

    /* Trivial reject: if the segment's bounding box misses the clip box, no
     * pixel of it can land, and there is no point stepping the DDA at all. */
    int64_t bx0 = x1 < x2 ? x1 : x2, bx1 = x1 < x2 ? x2 : x1;
    int64_t by0 = y1 < y2 ? y1 : y2, by1 = y1 < y2 ? y2 : y1;
    if (bx1 < s.cx0 || bx0 >= s.cx1 || by1 < s.cy0 || by0 >= s.cy1) {
        dev->stat_pixels_clipped += (uint64_t)total;
        return;
    }

    int64_t sx = (x2 >= x1) ? 1 : -1;
    int64_t sy = (y2 >= y1) ? 1 : -1;
    int64_t err = dx - dy;
    int64_t px = x1, py = y1;
    uint64_t steps = 0;

    for (;;) {
        vid_poke(dev, &s, (int32_t)px, (int32_t)py, color);
        if (px == x2 && py == y2) break;
        if (++steps > (uint64_t)VIDEO_MAX_LINE_STEPS) break;  /* LIMITATIONS 8 */
        int64_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; px += sx; }
        if (e2 <  dx) { err += dx; py += sy; }
    }
}

void video_draw_circle(video_device_t *dev, int32_t cx, int32_t cy,
                       int32_t r, uint32_t color) {
    vsurf_t s;
    if (!vid_target(dev, &s)) return;
    if (r < 0) return;
    if (r > VIDEO_MAX_RADIUS) return;          /* LIMITATIONS 8 */
    if (r == 0) { vid_poke(dev, &s, cx, cy, color); return; }

    /* Trivial reject on the bounding box. */
    int64_t bx0 = (int64_t)cx - r, bx1 = (int64_t)cx + r;
    int64_t by0 = (int64_t)cy - r, by1 = (int64_t)cy + r;
    if (bx1 < s.cx0 || bx0 >= s.cx1 || by1 < s.cy0 || by0 >= s.cy1) return;

    /* Integer midpoint circle. err is the midpoint decision variable. */
    int64_t x = r, y = 0, err = 1 - r;
    while (x >= y) {
        int64_t ox[8] = {  x,  y, -y, -x, -x, -y,  y,  x };
        int64_t oy[8] = {  y,  x,  x,  y, -y, -x, -x, -y };
        for (int k = 0; k < 8; k++) {
            int64_t px = (int64_t)cx + ox[k];
            int64_t py = (int64_t)cy + oy[k];
            if (px < -0x40000000LL || px > 0x40000000LL) continue;
            if (py < -0x40000000LL || py > 0x40000000LL) continue;
            vid_poke(dev, &s, (int32_t)px, (int32_t)py, color);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void video_draw_text(video_device_t *dev, int32_t x, int32_t y,
                     const char *text, uint32_t color) {
    vsurf_t s;
    if (!vid_target(dev, &s)) return;
    if (!text) return;

    int64_t cx = x, cy = y;
    for (uint32_t n = 0; n < VIDEO_TEXT_MAX_CHARS && text[n]; n++) {
        uint8_t c = (uint8_t)text[n];
        if (c == '\n') { cx = x; cy += VIDEO_FONT_H; continue; }
        if (c == '\r') { cx = x; continue; }
        if (c == '\t') { cx += 4 * VIDEO_FONT_W; continue; }

        const uint8_t *gl = vid_glyph(c);
        for (uint32_t row = 0; row < VIDEO_FONT_ROWS; row++) {
            uint8_t bits = gl[row];
            if (bits == 0) continue;                 /* nothing to draw */
            int64_t py = cy + VIDEO_FONT_ROW0 + row;
            if (py < s.cy0 || py >= s.cy1) continue; /* whole row is clipped */
            for (uint32_t col = 0; col < VIDEO_FONT_W; col++) {
                if (!(bits & (0x80u >> col))) continue;   /* background: untouched */
                int64_t px = cx + col;
                if (px < -0x40000000LL || px > 0x40000000LL) continue;
                vid_poke(dev, &s, (int32_t)px, (int32_t)py, color);
            }
        }
        cx += VIDEO_FONT_W;
        if (cx > 0x40000000LL) break;                /* runaway x, stop */
    }
}

/* ===================================================================
 * Blit
 *
 * One module-static row buffer (LIMITATIONS 6). Buffering the whole source
 * row before writing the destination row is what makes overlapping COPY /
 * ALPHA / FLIPPED correct; the row ORDER is chosen the way memmove chooses
 * its direction, so a downward overlapping copy does not smear.
 * =================================================================== */

static uint32_t g_blit_row[VIDEO_BLIT_MAX_W];
static uint8_t  g_blit_ok[VIDEO_BLIT_MAX_W];

static bool vid_rects_overlap(int64_t ax, int64_t ay, int64_t aw, int64_t ah,
                              int64_t bx, int64_t by, int64_t bw, int64_t bh) {
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return false;
    return !(ax + aw <= bx || bx + bw <= ax || ay + ah <= by || by + bh <= ay);
}

void video_blit(video_device_t *dev, int32_t dst_x, int32_t dst_y,
                int32_t src_x, int32_t src_y, int32_t w, int32_t h, blit_op_t op) {
    vsurf_t s;
    if (!vid_target(dev, &s)) return;
    if (w <= 0 || h <= 0) return;
    /* LIMITATIONS 6: BOTH extents are bounded. The width bound protects the
     * static row buffer; the height bound protects the clock — these loops are
     * O(w*h), and h was previously unbounded, so h = INT32_MAX turned one call
     * into hours of spinning. It also kept `h * 2` below inside int32, which
     * UBSan flagged as signed overflow for h >= 0x40000000. */
    if (w > VIDEO_BLIT_MAX_W || h > VIDEO_BLIT_MAX_H) return;

    switch (op) {
        case BLIT_SOLID_FILL:
            /* No source is read; the destination rect becomes fg_color. */
            video_fill_rect(dev, dst_x, dst_y, w, h, dev->ctx.fg_color);
            return;

        case BLIT_COPY:
        case BLIT_ALPHA:
        case BLIT_FLIPPED: {
            bool overlap = vid_rects_overlap(dst_x, dst_y, w, h, src_x, src_y, w, h);
            bool down = overlap && (dst_y > src_y);
            for (int32_t k = 0; k < h; k++) {
                int32_t j = down ? (h - 1 - k) : k;
                int64_t sy = (int64_t)src_y + j;
                int64_t dy = (int64_t)dst_y + j;
                /* Read the entire source row first — this is what makes an
                 * overlapping horizontal copy or flip correct. */
                for (int32_t i = 0; i < w; i++) {
                    int64_t sx = (int64_t)src_x + i;
                    uint32_t v = 0;
                    bool ok = (sx >= INT32_MIN && sx <= INT32_MAX &&
                               sy >= INT32_MIN && sy <= INT32_MAX)
                              ? vid_peek(&s, (int32_t)sx, (int32_t)sy, &v) : false;
                    g_blit_row[i] = v;
                    g_blit_ok[i] = ok ? 1u : 0u;
                }
                if (dy < INT32_MIN || dy > INT32_MAX) continue;
                for (int32_t i = 0; i < w; i++) {
                    int32_t si = (op == BLIT_FLIPPED) ? (w - 1 - i) : i;
                    if (!g_blit_ok[si]) continue;     /* source was off-surface */
                    int64_t dx = (int64_t)dst_x + i;
                    if (dx < INT32_MIN || dx > INT32_MAX) continue;
                    uint32_t src = g_blit_row[si];
                    if (op == BLIT_ALPHA) {
                        uint32_t a = dev->ctx.alpha;
                        if (s.fmt == PIXEL_ARGB8888)
                            a = (((src >> 24) & 0xFFu) * a + 127u) / 255u;
                        uint32_t dstc = 0;
                        if (!vid_peek(&s, (int32_t)dx, (int32_t)dy, &dstc)) {
                            dev->stat_pixels_clipped++;
                            continue;
                        }
                        /* Blend once, here. Writing through a surface with
                         * blend disabled so ctx.alpha_blending cannot apply a
                         * second, silent blend on top of this one. */
                        vsurf_t sd = s;
                        sd.blend = false;
                        vid_poke(dev, &sd, (int32_t)dx, (int32_t)dy,
                                 vid_blend(src, dstc, a));
                        continue;
                    }
                    vid_poke(dev, &s, (int32_t)dx, (int32_t)dy, src);
                }
            }
            return;
        }

        case BLIT_SCALED: {
            /* dst is 2w x 2h. A full-surface temporary would be needed to do
             * this safely in place, and this file allocates nothing. */
            if (vid_rects_overlap(dst_x, dst_y, (int64_t)w * 2, (int64_t)h * 2,
                                  src_x, src_y, w, h))
                return;
            /* int64 loop bounds: 2*h and 2*w must not depend on the cap above
             * staying where it is to avoid signed overflow. */
            const int64_t h2 = (int64_t)h * 2, w2 = (int64_t)w * 2;
            for (int64_t j = 0; j < h2; j++) {
                int64_t sy = (int64_t)src_y + (j / 2);
                int64_t dy = (int64_t)dst_y + j;
                if (sy < INT32_MIN || sy > INT32_MAX) continue;
                if (dy < INT32_MIN || dy > INT32_MAX) continue;
                for (int32_t i = 0; i < w; i++) {
                    uint32_t v = 0;
                    int64_t sx = (int64_t)src_x + i;
                    g_blit_ok[i] = (sx >= INT32_MIN && sx <= INT32_MAX &&
                                    vid_peek(&s, (int32_t)sx, (int32_t)sy, &v)) ? 1u : 0u;
                    g_blit_row[i] = v;
                }
                for (int64_t i = 0; i < w2; i++) {
                    if (!g_blit_ok[i / 2]) continue;
                    int64_t dx = (int64_t)dst_x + i;
                    if (dx < INT32_MIN || dx > INT32_MAX) continue;
                    vid_poke(dev, &s, (int32_t)dx, (int32_t)dy, g_blit_row[i / 2]);
                }
            }
            return;
        }

        case BLIT_ROTATED: {
            /* 90 degrees clockwise: dst is h x w. Same overlap restriction. */
            if (vid_rects_overlap(dst_x, dst_y, h, w, src_x, src_y, w, h))
                return;
            for (int32_t j = 0; j < h; j++) {
                int64_t sy = (int64_t)src_y + j;
                int64_t dx = (int64_t)dst_x + (h - 1 - j);
                if (sy < INT32_MIN || sy > INT32_MAX) continue;
                if (dx < INT32_MIN || dx > INT32_MAX) continue;
                for (int32_t i = 0; i < w; i++) {
                    int64_t sx = (int64_t)src_x + i;
                    int64_t dy = (int64_t)dst_y + i;
                    if (sx < INT32_MIN || sx > INT32_MAX) continue;
                    if (dy < INT32_MIN || dy > INT32_MAX) continue;
                    uint32_t v = 0;
                    if (!vid_peek(&s, (int32_t)sx, (int32_t)sy, &v)) continue;
                    vid_poke(dev, &s, (int32_t)dx, (int32_t)dy, v);
                }
            }
            return;
        }

        default:
            return;   /* unknown op: draw nothing rather than guess */
    }
}

/* ===================================================================
 * IRQ
 * =================================================================== */

void video_handle_irq(video_device_t *dev) {
    if (!dev) return;

    if (dev->irq_vsync) {
        dev->irq_vsync = false;
        dev->stat_irq_vsync++;
        video_display_t *d = video_get_display(dev, dev->primary_display);
        if (d) d->vsync = true;
    }

    if (dev->irq_flip_done) {
        dev->irq_flip_done = false;
        dev->stat_irq_flip++;
        video_display_t *d = video_get_display(dev, dev->primary_display);
        /* scanout_live means "pixels are physically being scanned out". A
         * latch on its own is not evidence of that: it takes a backend that
         * actually has a flip path (otherwise this file never handed a page to
         * anything) AND a connector that is still present. */
        if (d && d->active && dev->ops_bound && dev->ops.flip)
            d->scanout_live = true;
    }

    if (dev->irq_display_change) {
        dev->irq_display_change = false;
        dev->stat_irq_hotplug++;
        /* Re-reading a connector is hardware work. Without a probe callback
         * there is nothing honest to do but record that it happened. */
        if (dev->ops_bound && dev->ops.probe) {
            uint32_t n = vid_ndisp(dev);
            for (uint32_t i = 0; i < n; i++) {
                video_display_t *d = &dev->displays[i];
                uint32_t w = 0, h = 0;
                int r = dev->ops.probe(dev->ops.ctx, d->display_id, &w, &h);
                if (r != 0 || w == 0 || h == 0 ||
                    w > dev->max_width || h > dev->max_height ||
                    w > VIDEO_HARD_MAX_DIM || h > VIDEO_HARD_MAX_DIM) {
                    d->active = false;          /* connector is gone */
                    d->scanout_live = false;
                    continue;
                }
                d->active = true;
                if (w == d->width && h == d->height) continue;
                /* Geometry changed. Adopt it only if the bound buffer still
                 * holds it; otherwise keep the old surface so drawing stays
                 * inside memory we know we own. */
                d->scanout_live = false;
                if (!vid_fmt_valid(d->format)) continue;   /* pitch 0 is not a surface */
                uint64_t pitch = (uint64_t)w * vid_fmt_bytes(d->format);
                uint64_t page = pitch * h;
                uint64_t need = d->double_buffered ? page * 2u : page;
                if (d->fb_base && need > (uint64_t)d->fb_size) continue;
                d->width = w;
                d->height = h;
                d->pitch = (uint32_t)pitch;
                d->page_bytes = (uint32_t)page;
                if (d->fb_base) {
                    d->framebuffer = d->fb_base;
                    d->backbuffer = d->double_buffered
                                    ? (void *)((uint8_t *)d->fb_base + page) : NULL;
                }
                if (dev->ctx.display_id == d->display_id) vid_reset_clip(dev);
            }
        }
    }
}

/* ===================================================================
 * Coverage
 *
 * This must be able to FAIL, so it checks facts that can be false:
 *   r = (active displays whose surface description is internally consistent
 *        AND whose bound framebuffer is large enough) / (active displays)
 *   l = (primary display resolves and is active) + (the 2D context points at
 *        a live display), over 2
 * Both are fractions in [0,1]; demanding r*l >= 1.0 demands both be exactly
 * 1. A device with no displays covers nothing and returns false outright.
 * =================================================================== */

bool video_verify_coverage(video_device_t *dev) {
    if (!dev) return false;

    uint32_t n = vid_ndisp(dev);

    uint32_t active = 0, sane = 0;
    for (uint32_t i = 0; i < n; i++) {
        video_display_t *d = &dev->displays[i];
        if (!d->active) continue;
        active++;

        bool ok = true;
        if (!vid_fmt_valid(d->format)) ok = false;
        else {
            uint64_t bytes = vid_fmt_bytes(d->format);
            if (d->bpp != (uint32_t)(bytes * 8u)) ok = false;
            if (d->width == 0 || d->height == 0) ok = false;
            if (d->width > dev->max_width || d->height > dev->max_height) ok = false;
            if ((uint64_t)d->pitch < (uint64_t)d->width * bytes) ok = false;
            if (ok) {
                uint64_t page = (uint64_t)d->pitch * d->height;
                uint64_t need = d->double_buffered ? page * 2u : page;
                if (!d->framebuffer) ok = false;
                else if ((uint64_t)d->fb_size < need) ok = false;
                if (d->double_buffered && !d->backbuffer) ok = false;
            }
        }
        if (ok) sane++;
    }

    dev->coverage_r = (active == 0) ? 0.0 : (double)sane / (double)active;

    video_display_t *p = video_get_display(dev, dev->primary_display);
    video_display_t *c = video_get_display(dev, dev->ctx.display_id);
    uint32_t pts = 0;
    if (p && p->active) pts++;
    if (c && c->active) pts++;
    dev->coverage_l = (double)pts / 2.0;

    if (active == 0) return false;
    return (dev->coverage_r * dev->coverage_l) >= VIDEO_COVERAGE_FLOOR;
}
