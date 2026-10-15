/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_image_int.h — shared between the zx_image decoders; not an API. */
#ifndef ZXV_ZUI_IMAGE_INT_H
#define ZXV_ZUI_IMAGE_INT_H

#include <stddef.h>

#include "zx_image.h"

static inline uint32_t zi_be16(const uint8_t *p)
{
    return (uint32_t) p[0] << 8 | p[1];
}
static inline uint32_t zi_be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static inline uint32_t zi_le16(const uint8_t *p)
{
    return p[0] | (uint32_t) p[1] << 8;
}
static inline uint32_t zi_le32(const uint8_t *p)
{
    return p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static inline uint32_t zi_argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    return a << 24 | r << 16 | g << 8 | b;
}

/* Zero n bytes (no memset: IM1). */
static inline void zi_zero(void *p, size_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

/* Fail with a reason: sets info->why and returns rc. */
static inline int zi_fail(zx_image_info_t *info, int rc, const char *why)
{
    info->why = why;
    return rc;
}

/* IM3: dimensions within the caps (w, h >= 1). */
int zi_dims(zx_image_info_t *info, uint32_t w, uint32_t h);

/* IM4: where stored pixel (x, y) of a sw x sh picture lands in the shown
 * one: index = base + x * step, for the row's base and step. */
typedef struct {
    uint32_t *px;
    uint32_t sw, sh;
    uint8_t orient;
} zi_out_t;
void zi_row(const zi_out_t *o, uint32_t y, int64_t *base, int64_t *step);

/* EXIF orientation from a TIFF block (the bytes after "Exif\0\0"); 1 when
 * absent or malformed. */
uint8_t zi_exif_orientation(const uint8_t *t, uint32_t n);

int zi_png_probe(const uint8_t *d, uint32_t n, zx_image_info_t *info);
int zi_png_decode(const uint8_t *d, uint32_t n, uint8_t *work, uint32_t worklen, uint32_t *px,
                  zx_image_info_t *info);
int zi_jpeg_probe(const uint8_t *d, uint32_t n, zx_image_info_t *info);
int zi_jpeg_decode(const uint8_t *d, uint32_t n, uint8_t *work, uint32_t worklen, uint32_t *px,
                   zx_image_info_t *info);

#endif /* ZXV_ZUI_IMAGE_INT_H */
