/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_vtrace.h — any raster picture to a ZXVG vector rendition.
 *
 * The owner's rule: the system draws vector natively (zui_vec.h, ZXVG), and
 * anything visual that arrives as pixels (a PNG, a JPEG, a GIF, a frame of a
 * film) is turned into vector so it scales like everything else. This file
 * does that for one picture; zui_vtrace_video.h does it for a film.
 *
 * Pipeline (every stage integer; the per-pixel ones behind zvt_backend_t, so
 * a GPU runs them with the CPU's bits, see zvt_kern.h):
 *   1 flatten   alpha < 128 is transparent (never drawn), the rest opaque;
 *   2 smooth    optional edge-preserving 3x3 passes (zk_smooth_px);
 *   3 palette   median cut on a 5-5-5 histogram, then k-means rounds in the
 *               full 8-bit space (zk_quant_px assigns, the CPU averages); in
 *               pixel-exact mode the picture's own colours, unchanged;
 *   4 regions   4-connected components of equal index (zk_edge_px feeds the
 *               union-find), regions under min_area merged into the
 *               neighbour of nearest colour; thin regions (area under 3/4
 *               of their perimeter: the one-pixel fringes anti-aliasing
 *               leaves between two colours) handed out pixel by pixel to
 *               the adjacent region nearest in colour, which puts the edge
 *               where the coverage says it is;
 *   5 boundary  the crack graph of the label map, traced ONCE into chains
 *               between junctions (a chain separates exactly two labels);
 *   6 curves    each chain fitted once: corners kept (long straight runs on
 *               both sides, sharp turns, image corners); every crack's
 *               point moved across the crack to where the anti-aliasing
 *               says the edge is (the two pixels' coverage of the two
 *               colours, at most half a pixel), lightly smoothed, then a
 *               Schneider least-squares
 *               cubic fit with Newton reparameterisation, split until every
 *               point is within tol; near-straight spans become lines and
 *               cubics that are really quadratics become QUAD;
 *   7 layering  regions in a forest by Prim's order (largest first): each
 *               region's parent is an earlier, adjacent region (longest
 *               shared border, depth < max_depth). A region is drawn as the
 *               union of its subtree, so it is painted UNDER its children:
 *               a child's anti-aliased edge blends with the parent's colour,
 *               never with the background. The very first region is drawn as
 *               every opaque pixel, so no shape's edge can show the surface.
 *               Chains are shared, so neighbouring shapes meet on identical
 *               curves: no hairline gaps;
 *   8 gradients optional: a big region whose colour is well fitted by a
 *               plane (least squares) gets a two-stop linear gradient;
 *   9 emit      ZXVG v1, back to front, one NEWPATH..FILL per region, every
 *               path within the renderer's caps (zui_vec.h V7): a path that
 *               would be too detailed has its chains refitted coarser
 *               (in pixel-exact mode too: such a picture comes out
 *               APPROXIMATE instead of being refused), and if that is not
 *               enough, min_area grows (x4, three times) and transparent
 *               specks smaller than min_area are filled;
 *  10 measure   the ZXVG is drawn back through zui_vec at the source size and
 *               compared with the source: PSNR (RGB, centi-dB) and SSIM (luma,
 *               8x8 windows at a stride of 4, x10000), both integer.
 *
 * PROVENANCE. ZXVG has no metadata block and its reader rejects bytes after
 * END, so the trace's provenance travels in a fixed 112-byte sidecar record,
 * ZXVT (layout below), bound to the ZXVG by its SHA-256: the CID of the
 * original raster bytes (CIDv1 raw sha2-256, the tripartite_fs/ipfs_node
 * encoding), the measured PSNR and SSIM, the preset, and the EXACT flag.
 * EXACT is set only when the re-render is pixel-identical to the source
 * (checked over two backgrounds when the source has transparency); every
 * other trace says APPROXIMATE. Nothing is ever called lossless otherwise:
 * the original raster stays addressable by its CID, so nothing is lost.
 *
 *   ZXVT := "ZXVT" u8 version(=1) u8 flags (bit 0 EXACT, bit 1 CID of pixels
 *           because no source bytes were given) u8 preset u8 0
 *           u32 width  u32 height                       (little endian)
 *           u8 cid[36]           CIDv1: 01 55 12 20 || SHA-256(source)
 *           u8 zxvg_sha256[32]   SHA-256 of the ZXVG this record describes
 *           i32 psnr_cdb (ZVT_PSNR_INF when identical)  i32 ssim_x10000
 *           u32 paths  u32 colours  u32 zxvg_bytes
 *           u8 check[8]          first 8 bytes of SHA-256 of the 104 above
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 *   Done: everything above, integer only, freestanding, no allocation (the
 *   caller's work buffer, sized by zvt_work_size); pixel-exact mode for
 *   flat-colour pictures of at most 256 colours with no partial alpha
 *   (rectilinear polygons along pixel edges, verified identical, else the
 *   result is honestly APPROXIMATE); seams only on tree edges are perfectly
 *   clean: two regions that are neighbours but not parent and child blend at
 *   their shared edge with the colour painted beneath both (their nearest
 *   common ancestor, or the first region), not with the background.
 *   Approximations: photographs, film, fine texture, noise and grain are
 *   represented by flat regions (plus a few gradients); detail below
 *   min_area is merged away (transparent specks too, once min_area > 1),
 *   lines about one pixel thick are absorbed as fringe outside pixel mode
 *   (a thin light line between dark outlines can lose a pixel of width),
 *   and the palette is at most 256 colours, so the
 *   scores are what they are and are reported, never rounded up. Partial
 *   alpha is reduced to opaque / transparent. Strokes, arcs and radial
 *   gradients are never emitted (fills only). The smoothing, the k-means
 *   averaging, labelling, fitting and the zui_vec re-render run on the CPU.
 *   Limits: width, height <= ZVT_MAX_DIM, pixels <= ZVT_MAX_PIXELS.
 */
#ifndef ZXV_ZUI_VTRACE_H
#define ZXV_ZUI_VTRACE_H

#include <stdbool.h>
#include <stdint.h>

#define ZVT_MAX_DIM    8192u
#define ZVT_MAX_PIXELS (16u * 1024u * 1024u)
#define ZVT_PSNR_INF   999999 /* centi-dB reported for a pixel-identical render */
#define ZVT_CID_LEN    36u
#define ZVT_REC_LEN    112u /* the ZXVT record */
#define ZVT_MAX_GRAD   64u

enum {
    ZVT_OK = 0,
    ZVT_E_ARG = -1,   /* bad argument                                        */
    ZVT_E_SIZE = -2,  /* picture beyond ZVT_MAX_DIM / ZVT_MAX_PIXELS          */
    ZVT_E_SPACE = -3, /* work or output buffer too small                      */
    ZVT_E_LIMIT = -4, /* a path cannot be brought within the renderer's caps  */
    ZVT_E_CHECK = -5, /* internal: the emitted ZXVG failed zui_zxvg_check     */
    ZVT_E_DATA = -6   /* a record or container is malformed                   */
};

typedef enum {
    ZVT_PRESET_PIXEL = 0, /* pixel art, flat colours: exact rectilinear polygons */
    ZVT_PRESET_ICON,      /* logos, icons: few colours, sharp corners            */
    ZVT_PRESET_ILLUSTRATION,
    ZVT_PRESET_PHOTO,
    ZVT_PRESET_COUNT
} zvt_preset_t;

typedef struct {
    uint32_t colours;    /* palette size 2..256 (255 when transparency exists)    */
    uint32_t min_area;   /* regions smaller than this many pixels are merged       */
    int32_t tol_q4;      /* curve tolerance, 1/16 px                               */
    uint32_t corner_run; /* straight run (px) on both sides that makes a corner    */
    uint32_t smooth;     /* edge-preserving passes 0..4                            */
    uint32_t smooth_thr; /* their per-channel threshold                            */
    uint32_t kmeans;     /* k-means rounds after median cut 0..16                  */
    uint32_t max_depth;  /* layering forest depth 1..64                            */
    bool gradients;      /* linear gradients for big smooth regions                */
    bool absorb_thin;    /* hand anti-aliasing fringes (regions about one pixel
                            thick) pixel by pixel to the nearest-coloured
                            neighbour region                                   */
    bool pixel_exact;    /* exact colours, no smoothing, no merging, no curves     */
    uint32_t bg;         /* 0xRRGGBB behind transparent pixels when measuring      */
    zvt_preset_t preset; /* recorded in the provenance                            */
} zvt_params_t;

void zvt_preset(zvt_params_t *p, zvt_preset_t preset);
const char *zvt_preset_name(zvt_preset_t p);

/* ---- the per-pixel backend (zvt_kern.h) ---------------------------------
 * Each function returns 0 when it produced the result, non-zero when it
 * could not (the tracer then runs the CPU reference for that call and counts
 * a fallback). A GPU backend must give the CPU's bits; test_zvt_gpu.c checks. */
typedef struct {
    const char *name;
    void *ctx;
    int (*smooth)(void *ctx, const uint32_t *in, uint32_t *out, uint32_t w, uint32_t h,
                  uint32_t thr);
    int (*quant)(void *ctx, const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t npal,
                 uint8_t *idx);
    int (*edges)(void *ctx, const uint8_t *idx, uint32_t w, uint32_t h, uint8_t *edge);
    /* row_sse[h]; win[5 * nwx * nwy], window (i, j) at (i * step, j * step) */
    int (*metric)(void *ctx, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                  uint32_t wx, uint32_t wy, uint32_t step, uint32_t *row_sse, uint32_t *win);
    int (*diff)(void *ctx, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                uint32_t thr, uint8_t *mask, uint32_t *row_count);
} zvt_backend_t;

extern const zvt_backend_t zvt_cpu_backend;

/* The CPU reference stages, for backends and tests. */
void zvt_cpu_smooth(const uint32_t *in, uint32_t *out, uint32_t w, uint32_t h, uint32_t thr);
void zvt_cpu_quant(const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t npal,
                   uint8_t *idx);
void zvt_cpu_edges(const uint8_t *idx, uint32_t w, uint32_t h, uint8_t *edge);
void zvt_cpu_metric(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h, uint32_t wx,
                    uint32_t wy, uint32_t step, uint32_t *row_sse, uint32_t *win);
void zvt_cpu_diff(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h, uint32_t thr,
                  uint8_t *mask, uint32_t *row_count);

/* ---- tracing ------------------------------------------------------------ */
typedef struct {
    uint32_t off, len;      /* the shape's bytes in the ZXVG: after its first MOVE's
                               operands, through its FILL                       */
    int32_t x0, y0, ex, ey; /* absolute start (the MOVE) and end point, q4      */
} zvt_shape_t;

typedef struct {
    uint32_t bytes;      /* ZXVG length                                      */
    uint32_t paths;      /* NEWPATH..FILL count                              */
    uint32_t colours;    /* palette entries used for regions                 */
    uint32_t gradients;  /* gradient fills                                   */
    uint32_t regions;    /* after merging                                    */
    uint32_t chains;     /* boundary chains fitted                           */
    uint32_t segments;   /* LINE/QUAD/CUBIC emitted                          */
    int32_t psnr_cdb;    /* RGB PSNR, centi-dB; ZVT_PSNR_INF when identical  */
    int32_t ssim;        /* luma SSIM x10000 (-10000..10000)                 */
    bool exact;          /* re-render pixel-identical (both backgrounds)     */
    bool exact_possible; /* pixel-exact mode found <= 256 flat colours       */
    bool cid_of_pixels;  /* no source bytes given: CID is of the ARGB pixels  */
    uint32_t refits;     /* chains refitted coarser to meet the path caps    */
    uint32_t fallbacks;  /* backend calls that fell back to the CPU          */
    uint8_t cid[ZVT_CID_LEN];
    zvt_preset_t preset;
    uint32_t w, h;
    uint32_t npal; /* the palette (ARGB, opaque) used, for video reuse */
    uint32_t pal[256];
    uint32_t nshapes; /* entries written to the caller's shape table      */
} zvt_result_t;

/* Work bytes zvt_trace needs for a w x h picture (0 when the size is out of
 * range). Generous by design: about 200 bytes a pixel. */
uint64_t zvt_work_size(uint32_t w, uint32_t h);
/* An output size that is always enough for a w x h trace. */
uint64_t zvt_out_max(uint32_t w, uint32_t h);

/* Optional inputs for video: a fixed palette (no quantisation search) and a
 * table to receive where each shape sits in the output. */
typedef struct {
    const uint32_t *pal; /* NULL: search a palette            */
    uint32_t npal;
    zvt_shape_t *shapes; /* NULL: not wanted                  */
    uint32_t shape_cap;
} zvt_extra_t;

/* Trace px (w x h, stride in pixels, 0xAARRGGBB straight alpha) into ZXVG at
 * out. src/src_len are the original file's bytes for the CID (NULL: the CID
 * is of the pixels, little-endian ARGB rows, and the result says so). be NULL
 * means the CPU backend. Returns the ZXVG length (> 0) or ZVT_E_*. */
int32_t zvt_trace(const uint32_t *px, uint32_t w, uint32_t h, uint32_t stride, const uint8_t *src,
                  uint32_t src_len, const zvt_params_t *p, const zvt_backend_t *be,
                  const zvt_extra_t *extra, void *work, uint64_t worklen, uint8_t *out,
                  uint32_t outcap, zvt_result_t *res);

/* ---- fidelity, for any two pictures ------------------------------------- */
typedef struct {
    uint64_t sse;
    int32_t psnr_cdb;
    int32_t ssim;
} zvt_fidelity_t;
/* a, b: w x h opaque XRGB (alpha ignored). scratch: zvt_fidelity_scratch(w, h)
 * 32-bit words. */
uint32_t zvt_fidelity_scratch(uint32_t w, uint32_t h);
int zvt_fidelity(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                 const zvt_backend_t *be, uint32_t *scratch, zvt_fidelity_t *out);
/* 10 * log10(255^2 * 3n / sse) in centi-dB (ZVT_PSNR_INF when sse is 0). */
int32_t zvt_psnr_cdb(uint64_t sse, uint64_t n);

/* ---- provenance (ZXVT) ---------------------------------------------------- */
void zvt_cid(const uint8_t *d, uint32_t n, uint8_t out[ZVT_CID_LEN]);
/* Write the record for a result and the ZXVG it describes. */
void zvt_record_write(const zvt_result_t *r, const uint8_t *zxvg, uint32_t zxvg_len,
                      uint8_t out[ZVT_REC_LEN]);
typedef struct {
    bool exact, cid_of_pixels;
    zvt_preset_t preset;
    uint32_t w, h;
    uint8_t cid[ZVT_CID_LEN];
    uint8_t zxvg_sha256[32];
    int32_t psnr_cdb, ssim;
    uint32_t paths, colours, zxvg_bytes;
} zvt_record_t;
/* Parse and check a record (fail closed: ZVT_E_DATA on any defect). When
 * zxvg is not NULL it must be the very bytes the record describes. */
int zvt_record_read(const uint8_t *d, uint32_t n, const uint8_t *zxvg, uint32_t zxvg_len,
                    zvt_record_t *out);

#endif /* ZXV_ZUI_VTRACE_H */
