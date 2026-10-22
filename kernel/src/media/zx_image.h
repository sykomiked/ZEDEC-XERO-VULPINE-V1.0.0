/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_image.h — opening and showing raster pictures (PNG, JPEG, GIF, BMP,
 * netpbm, QOI, TGA).
 *
 * The system's own graphics are vector (ZXVG, zui_vec.h) and stay that way.
 * But people have photos, screenshots and downloads in the formats everyone
 * else uses, and the system must open them. This is the decoder front end:
 * sniff the format from the bytes, report the size and the memory needed,
 * decode into 0xAARRGGBB pixels (straight alpha), and draw them scaled into
 * a box on a zui surface.
 *
 * Rules:
 *   IM1 Freestanding: no libc, no allocation, no floating point anywhere
 *       (the JPEG IDCT, colour conversion and the scaler are integer). All
 *       memory is the caller's: the output pixels and one work buffer whose
 *       size zx_image_probe reports.
 *   IM2 Fail closed. Every read is bounds checked, every count and size is
 *       range checked before it is used, and a file that is malformed,
 *       truncated or fails a checksum decodes to an error, never to a
 *       partial picture or an out-of-range access. Truncation is always
 *       detected for formats with an end marker (PNG IEND, JPEG EOI, GIF
 *       trailer, QOI end bytes) or a known size (BMP, binary netpbm, TGA,
 *       QOI); ASCII netpbm has neither, so a cut inside its last number
 *       cannot be seen.
 *   IM3 Hard caps: width and height <= ZX_IMG_MAX_DIM and width x height <=
 *       ZX_IMG_MAX_PIXELS, checked from the header before anything is
 *       decoded. Output size is fixed by the header, so a stream that tries
 *       to expand beyond it (a decompression bomb) is refused (PNG: the
 *       inflater's output is bounded to exactly the size the header implies;
 *       JPEG: at most ZUI_JPEG_MAX_SCANS scans; GIF: at most ZUI_GIF_MAX_FRAMES
 *       frames).
 *   IM4 Orientation: a JPEG's EXIF orientation (and PNG's eXIf chunk) is
 *       applied while decoding, so width and height in zx_image_info_t are
 *       the picture as it should be shown.
 *   IM5 Drawing scales with integer filters: exact area averaging along an
 *       axis that shrinks, bilinear along an axis that grows, in
 *       premultiplied alpha so edges do not fringe, then alpha-blended onto
 *       the XRGB surface.
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 *   PNG   every colour type and bit depth (1, 2, 4, 8, 16), PLTE and tRNS,
 *         Adam7 interlace, all five filters, CRC-32 on every chunk, zlib
 *         Adler-32. 16-bit samples are reduced to 8 by taking the high byte.
 *         Not done: gamma, cHRM, iCCP and sRGB colour management (samples are
 *         shown as stored); APNG animation (the default image is shown).
 *   JPEG  baseline and extended sequential Huffman (SOF0/SOF1), progressive
 *         Huffman (SOF2), 8-bit, one (grey) or three components (YCbCr, or
 *         RGB by Adobe marker or component ids) with any sampling factors
 *         1..4 (4:4:4, 4:2:2, 4:2:0, 4:4:0, 4:1:1), restart intervals, the
 *         default Motion-JPEG Huffman tables when a file has none. The
 *         IDCT is libjpeg's integer "islow", colour conversion is libjpeg's
 *         fixed point and 2x chroma uses libjpeg's "fancy" triangle
 *         upsampling, so the output is libjpeg-turbo's default output: the
 *         test finds it bit-exact on every fixture (it allows 2 levels).
 *         Refused with ZX_IMG_E_UNSUPPORTED: arithmetic coding (SOF9-15),
 *         lossless (SOF3), hierarchical (SOF5-7), 12-bit samples, 2 or 4
 *         components (CMYK/YCCK), DNL markers. No colour management.
 *   GIF   87a and 89a, LZW, global and local palettes, interlace,
 *         transparency, frame disposal 0-3; zx_image_decode gives the first
 *         frame composited on a transparent canvas, zui_gif_* iterates the
 *         rest with their delays.
 *   BMP   1, 4, 8, 16, 24 and 32 bits per pixel, RLE4 and RLE8, BITFIELDS,
 *         bottom-up and top-down, OS/2 core and V3/V4/V5 headers. 32-bit
 *         BI_RGB is opaque (its fourth byte is "reserved"); alpha comes only
 *         from an alpha mask. Not done: embedded JPEG/PNG (BI_JPEG/BI_PNG),
 *         colour profiles.
 *   PNM   P1-P6 (PBM, PGM, PPM, ASCII and binary), maxval 1..65535. PAM (P7)
 *         is refused; PFM (float) is not recognised.
 *   QOI   complete.
 *   TGA   types 1, 2, 3 and their RLE forms 9, 10, 11; 8, 15, 16, 24 and 32
 *         bits; every origin. 32-bit pixels use their fourth byte as alpha
 *         only when the header declares 8 alpha bits (16-bit: 1 bit). TGA
 *         has no magic number, so it is sniffed last and only accepted when
 *         every header field is plausible.
 *   Not supported at all: WebP, AVIF, HEIF, TIFF, JPEG 2000, JPEG XL, ICO,
 *   SVG (vector pictures go through ZXVG instead).
 */
#ifndef ZXV_ZUI_IMAGE_H
#define ZXV_ZUI_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"

#ifndef ZX_IMG_MAX_DIM
#    define ZX_IMG_MAX_DIM 16384u
#endif
#ifndef ZX_IMG_MAX_PIXELS
#    define ZX_IMG_MAX_PIXELS (64u * 1024u * 1024u) /* 256 MiB of ARGB */
#endif
#ifndef ZUI_JPEG_MAX_SCANS
#    define ZUI_JPEG_MAX_SCANS 256u
#endif
#ifndef ZUI_GIF_MAX_FRAMES
#    define ZUI_GIF_MAX_FRAMES 65535u
#endif

enum {
    ZX_IMG_OK = 0,
    ZX_IMG_E_ARG = -1,         /* NULL pointer, bad box                         */
    ZX_IMG_E_FORMAT = -2,      /* not a format we know                          */
    ZX_IMG_E_UNSUPPORTED = -3, /* known format, a feature we refuse (see why)   */
    ZX_IMG_E_DATA = -4,        /* malformed                                     */
    ZX_IMG_E_TRUNC = -5,       /* the data ends early                           */
    ZX_IMG_E_SIZE = -6,        /* beyond ZX_IMG_MAX_DIM / ZX_IMG_MAX_PIXELS   */
    ZX_IMG_E_SPACE = -7,       /* work or output buffer too small               */
    ZX_IMG_E_CHECK = -8        /* CRC-32 / Adler-32 mismatch                    */
};

typedef enum {
    ZX_IMG_NONE = 0,
    ZX_IMG_PNG,
    ZX_IMG_JPEG,
    ZX_IMG_GIF,
    ZX_IMG_BMP,
    ZX_IMG_PNM,
    ZX_IMG_QOI,
    ZX_IMG_TGA
} zui_img_format_t;

typedef struct {
    zui_img_format_t format;
    uint32_t w, h;       /* as shown (after IM4 orientation)                   */
    uint32_t work;       /* bytes of work memory zx_image_decode needs        */
    uint32_t px_bytes;   /* w * h * 4: the output                              */
    uint32_t frames;     /* GIF: frame count; otherwise 1                      */
    uint8_t orientation; /* EXIF 1..8 (1 = as stored)                          */
    bool alpha;          /* the format can carry transparency here             */
    bool progressive;    /* JPEG SOF2, PNG Adam7, GIF interlace                */
    const char *why;     /* a short reason when a call fails, else ""          */
} zx_image_info_t;

/* Which format, how big, how much work memory. Reads headers only (PNG:
 * walks the chunk list; GIF: walks the block structure). */
int zx_image_probe(const uint8_t *d, uint32_t n, zx_image_info_t *info);

/* Decode into px (w * h pixels, row-major, stride w, 0xAARRGGBB straight
 * alpha). work: at least info.work bytes (any alignment). info may be NULL;
 * when given it is filled as by zx_image_probe. */
int zx_image_decode(const uint8_t *d, uint32_t n, void *work, uint32_t worklen, uint32_t *px,
                    uint32_t pxcap, zx_image_info_t *info);

/* ---- drawing ------------------------------------------------------------ */
typedef enum {
    ZX_IMG_FIT_CONTAIN = 0, /* whole picture inside the box, centred (letterbox) */
    ZX_IMG_FIT_COVER,       /* fill the box, centred, the overflow cut off       */
    ZX_IMG_FIT_STRETCH,     /* exactly the box, aspect not kept                  */
    ZX_IMG_FIT_NONE         /* one pixel per pixel, centred, cut to the box      */
} zui_img_fit_t;

/* Where a w x h picture lands for a box and fit (may extend past the box
 * for COVER and NONE; drawing is clipped to the box). */
zui_rect_t zx_image_place(uint32_t w, uint32_t h, zui_rect_t box, zui_img_fit_t fit);

/* Draw decoded pixels (ARGB straight alpha, stride in pixels) scaled per IM5
 * into box, clipped to the box and the surface's clip, at opacity 0..255. */
int zx_image_blit(zui_surface_t *s, zui_rect_t box, const uint32_t *px, uint32_t w, uint32_t h,
                  uint32_t stride, zui_img_fit_t fit, uint32_t opacity);

/* Probe, decode and blit in one call. work holds the decoded pixels and the
 * decoder's scratch: at least info.px_bytes + info.work (+ 8 for alignment);
 * zx_image_draw_work gives the exact figure. */
uint32_t zx_image_draw_work(const zx_image_info_t *info);
int zx_image_draw(zui_surface_t *s, zui_rect_t box, const uint8_t *d, uint32_t n, void *work,
                  uint32_t worklen, zui_img_fit_t fit, uint32_t opacity);

/* ---- GIF animation ------------------------------------------------------ */
typedef struct {
    const uint8_t *d;
    uint32_t n;
    uint32_t w, h;    /* logical screen                                  */
    uint32_t pos;     /* next block                                      */
    uint32_t frame;   /* frames delivered                                */
    uint32_t loops;   /* NETSCAPE loop count (0 = forever), as stored     */
    uint32_t *canvas; /* caller's, w * h                                 */
    uint32_t *prev;   /* caller's, w * h, for disposal 3 (may be NULL)    */
    void *work;       /* LZW scratch, ZUI_GIF_WORK bytes                  */
    uint32_t worklen;
    /* the frame last delivered, for its disposal */
    uint32_t fx, fy, fw, fh;
    uint8_t dispose;
    bool restore; /* prev holds the canvas before the last frame     */
} zui_gif_t;

#define ZUI_GIF_WORK 20480u

/* Start iterating: canvas (and prev, for disposal 3 frames; without it they
 * are treated as disposal 2) must hold w * h pixels. */
int zui_gif_open(zui_gif_t *g, const uint8_t *d, uint32_t n, uint32_t *canvas, uint32_t *prev,
                 void *work, uint32_t worklen);
/* Composite the next frame onto canvas. Returns 1 with *delay_cs (the
 * frame's delay, centiseconds, as stored) when a frame was produced, 0 at
 * the end, < 0 on an error. */
int zui_gif_next(zui_gif_t *g, uint32_t *delay_cs);
void zui_gif_rewind(zui_gif_t *g);

/* ---- JPEG, exposed for Motion-JPEG video -------------------------------- */
/* Work bytes a JPEG of w x h can ever need (any sampling, one or three
 * components), so a video can size its buffer once. */
uint32_t zui_jpeg_work_max(uint32_t w, uint32_t h);

const char *zx_image_format_name(zui_img_format_t f);
const char *zx_image_error(int rc);

#endif /* ZXV_ZUI_IMAGE_H */
