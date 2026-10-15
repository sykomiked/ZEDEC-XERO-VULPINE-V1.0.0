/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_vec.h — the native vector renderer and the ZXVG vector format.
 *
 * The owner's rule: every graphic in the system (icons, logos, boot and launch
 * screens, UI art) is vector, never PNG or JPG, so it is sharp at every size
 * and on every panel. This is the renderer those graphics are drawn with, and
 * ZXVG is the compact binary they are stored in (tools/zxvg/svg2zxvg.py turns
 * an SVG into one).
 *
 * Rules:
 *   V1  Integer only and freestanding: no libc calls, no allocation, no float.
 *       All memory is the caller's: a zui_vec_work_t (fixed caps below) and,
 *       optionally, a layer buffer for exact group opacity.
 *   V2  Same input -> same pixels on every platform and compiler. Every
 *       rounding is spelled out (floor shifts and floor divisions), sorts are
 *       deterministic, and nothing depends on the host's int width.
 *   V3  Anti-aliasing is 16 sub-scanlines per pixel row with exact horizontal
 *       coverage at 1/256 px along each one: a pixel's coverage is 0..4096
 *       (16 x 256), so a vertical edge gets 1/256 px precision and a
 *       horizontal edge 1/16 px. Fill rules are applied per sub-scanline, so
 *       nonzero and even-odd are exact (no area-accumulation approximation).
 *   V4  Geometry is flattened in device space by uniform integer subdivision
 *       (quadratics, cubics) or angle stepping (arcs) with a tolerance in
 *       1/256 px (zui_vec_t.tol); circles stay round at any size.
 *   V5  Strokes are filled outlines: one quad per segment, a join piece per
 *       vertex and a cap piece per open end, all wound the same way and filled
 *       nonzero, so overlaps never double-darken. Width scales with the square
 *       root of the transform's determinant (exact for uniform scale; skewed or
 *       stretched strokes use that mean scale). Dashes are cut along the
 *       flattened path in device space.
 *   V6  Group opacity is exact (the group is drawn into a premultiplied layer
 *       and composited once) when the caller supplies layer memory big enough
 *       for the current clip; otherwise the opacity is multiplied into each
 *       child's paint, which differs only where children overlap.
 *   V7  A shape that does not fit the work caps is skipped whole and flagged
 *       in zui_vec_t.err, never drawn in part. V7a: drawing a ZXVG file, a
 *       path too detailed for the caps at the asked size is replayed with the
 *       flattening tolerance doubled (up to 8 times) before it is given up,
 *       so a large mark gets coarser curves instead of vanishing.
 *   V8  The ZXVG reader fails closed: the whole file is validated before the
 *       first pixel is drawn, every read is bounds checked, every count and
 *       value is range checked, and a malformed file draws nothing.
 *
 * Units: path coordinates are in 1/16 user units ("q4"). A transform
 * (zui_vec_xf_t) is 16.16 fixed point and maps user units to device pixels:
 *     X = a*x + c*y + e,   Y = b*x + d*y + f
 * with e, f in 16.16 pixels. Internally device positions are 1/256 px.
 * Colours are 0xAARRGGBB with straight (not premultiplied) alpha.
 *
 * ---- ZXVG v1 ---------------------------------------------------------------
 * uv = unsigned LEB128 varint (at most 5 bytes, value < 2^32);
 * zz = zigzag-encoded signed varint ((n << 1) ^ (n >> 31)).
 * Coordinates and lengths are integers in 1/16 user units.
 *
 *   file  := "ZXVG" u8 version(=1) u8 flags(=0)
 *            zz vb_x  zz vb_y  uv vb_w  uv vb_h      viewBox; w, h in 1..2^24
 *            uv npal (<=256)  { u8 r g b a } * npal  palette, straight alpha
 *            uv ngrad (<=64)  { grad } * ngrad
 *            uv nid (<=64)    { u8 len (1..31)  len bytes [A-Za-z0-9_.:-] }
 *            { command }      0x00 (END; nothing may follow it)
 *   grad  := u8 kind (1 linear, 2 radial)  u8 spread (0 pad, 1 reflect, 2 repeat)
 *            zz a b c d e f                          16.16; maps the unit
 *                     gradient space into user units: linear runs u = 0..1
 *                     along the x axis, radial is the unit circle
 *            u8 nstop (1..8) { u8 offset (0..255, non-decreasing)  uv palette }
 *   paint := uv ref (bit 0: 0 palette colour, 1 gradient; ref >> 1 = index)
 *            u8 alpha (multiplies the paint's own alpha)
 *
 *   0x01 MOVE     zz dx dy                 deltas from the current point
 *   0x02 LINE     zz dx dy
 *   0x03 QUAD     zz cx cy  zz dx dy       control and end, both relative to
 *                                          the current point
 *   0x04 CUBIC    zz c1x c1y c2x c2y dx dy all relative to the current point
 *   0x05 ARC      uv rx  uv ry  uv rot (0..65535 per turn)
 *                 u8 flags (bit0 large-arc, bit1 sweep)  zz dx dy
 *   0x06 CLOSE    back to the subpath's start (the current point goes too)
 *   0x07 NEWPATH  clears the path; FILL and STROKE keep it, so one path can
 *                 be filled and then stroked
 *   0x10 FILL     u8 rule (0 nonzero, 1 even-odd)  paint
 *   0x11 STROKE   paint  uv width  u8 join (0 miter, 1 round, 2 bevel)
 *                 u8 cap (0 butt, 1 round, 2 square)  uv miter limit x256
 *                 (256..65536)  uv ndash (0..8, even)  { uv dash } * ndash
 *                 zz dash offset
 *   0x20 GROUP    uv id (0 none, else 1 + index into the id table)
 *                 u8 opacity (0..255)  u8 has_matrix (0/1)
 *                 [zz a b c d e f]     16.16, child units -> parent units
 *   0x21 ENDGROUP
 *   0x00 END      groups must be balanced; nesting depth <= 16
 *
 * GROUP and ENDGROUP clear the path. Drawing commands before any MOVE start a
 * subpath at the current point. The current point starts at (0, 0) and must
 * stay within +-2^23 (1/16 units). Group ids are kept from the SVG so a group
 * can be animated by name (zui_vec_override_t, see zui_kinetic).
 */
#ifndef ZXV_ZUI_VEC_H
#define ZXV_ZUI_VEC_H

#include <stdint.h>
#include <stdbool.h>
#include "zui_canvas.h"

/* ---- caps (override at build time to trade memory for capacity) -------- */
#ifndef ZUI_VEC_MAX_PTS
#    define ZUI_VEC_MAX_PTS 8192 /* flattened points in one path           */
#endif
#ifndef ZUI_VEC_MAX_EDGES
#    define ZUI_VEC_MAX_EDGES 8192 /* edges of one fill or stroke outline   */
#endif
#ifndef ZUI_VEC_MAX_W
#    define ZUI_VEC_MAX_W 1024 /* coverage band; wider clips draw in bands  */
#endif
#define ZUI_VEC_MAX_STOPS 8
#define ZUI_VEC_MAX_DASH  8
#define ZUI_VEC_MAX_DEPTH 16
#define ZUI_VEC_SS        16 /* sub-scanlines per pixel row (V3)           */

enum { ZUI_VEC_NONZERO = 0, ZUI_VEC_EVENODD = 1 };
enum { ZUI_VEC_JOIN_MITER = 0, ZUI_VEC_JOIN_ROUND = 1, ZUI_VEC_JOIN_BEVEL = 2 };
enum { ZUI_VEC_CAP_BUTT = 0, ZUI_VEC_CAP_ROUND = 1, ZUI_VEC_CAP_SQUARE = 2 };
enum { ZUI_VEC_SOLID = 0, ZUI_VEC_LINEAR = 1, ZUI_VEC_RADIAL = 2 };
enum { ZUI_VEC_PAD = 0, ZUI_VEC_REFLECT = 1, ZUI_VEC_REPEAT = 2 };

/* zui_vec_t.err bits (sticky until zui_vec_init) */
#define ZUI_VEC_EFULL 1u /* a shape exceeded a cap and was skipped (V7)  */
#define ZUI_VEC_EARG  2u /* a bad argument (bad matrix, bad paint)       */

typedef struct {
    int32_t a, b, c, d, e, f; /* 16.16, see Units above */
} zui_vec_xf_t;

typedef struct {
    uint8_t off;   /* 0..255 along the gradient */
    uint32_t argb; /* straight alpha            */
} zui_vec_stop_t;

typedef struct {
    uint8_t kind;   /* ZUI_VEC_SOLID / LINEAR / RADIAL                  */
    uint8_t spread; /* ZUI_VEC_PAD / REFLECT / REPEAT                   */
    uint8_t nstop;  /* 1..ZUI_VEC_MAX_STOPS                             */
    uint32_t argb;  /* the solid colour                                 */
    zui_vec_xf_t g; /* unit gradient space -> user units (all 16.16)    */
    zui_vec_stop_t stop[ZUI_VEC_MAX_STOPS];
} zui_vec_paint_t;

typedef struct {
    int32_t width;                  /* 1/16 user units                                  */
    uint8_t join;                   /* ZUI_VEC_JOIN_*                                   */
    uint8_t cap;                    /* ZUI_VEC_CAP_*                                    */
    int32_t miter_q8;               /* miter limit x256 (SVG default 4 = 1024)          */
    uint32_t ndash;                 /* 0 = solid; even, <= ZUI_VEC_MAX_DASH             */
    int32_t dash[ZUI_VEC_MAX_DASH]; /* 1/16 user units                    */
    int32_t dash_off;               /* 1/16 user units                    */
} zui_vec_stroke_t;

typedef struct {
    int32_t x0, y0, x1, y1;      /* 1/256 px, y0 < y1             */
    int32_t dir;                 /* +1 down, -1 up                */
    int32_t sy, cx, rem, qs, rs; /* rasteriser state (exact DDA) */
} zui_vec_edge_t;

/* The caller's working memory (V1). About 0.5 MB at the default caps: give
 * it static storage, one per drawing thread. Contents are scratch. */
typedef struct {
    int32_t px[ZUI_VEC_MAX_PTS], py[ZUI_VEC_MAX_PTS]; /* the flattened path   */
    uint8_t pf[ZUI_VEC_MAX_PTS];
    int32_t tx[ZUI_VEC_MAX_PTS], ty[ZUI_VEC_MAX_PTS]; /* stroker scratch      */
    uint8_t tf[ZUI_VEC_MAX_PTS];
    zui_vec_edge_t e[ZUI_VEC_MAX_EDGES];
    uint32_t act[ZUI_VEC_MAX_EDGES];
    int32_t xs[ZUI_VEC_MAX_EDGES];
    int8_t ws[ZUI_VEC_MAX_EDGES];
    int32_t run[ZUI_VEC_MAX_W + 2], part[ZUI_VEC_MAX_W + 2];
    uint32_t np, ne;
} zui_vec_work_t;

/* Per-group animation override, looked up by the group's id (zui_kinetic
 * fills these per frame). Start from zui_vec_override_init(): a zeroed struct
 * means opacity 0, i.e. the group is hidden. */
typedef struct {
    const char *group;  /* the SVG id, NUL-terminated                       */
    int32_t m[6];       /* extra affine a b c d e f (16.16; e, f in 16.16
                           user units) applied about the pivot, in the
                           group's own coordinates; all zero = identity     */
    int32_t px, py;     /* pivot, 1/16 user units, group coordinates        */
    uint16_t opacity;   /* 0..256 multiplier (256 = unchanged)              */
    uint32_t tint;      /* 0xRRGGBB                                         */
    uint16_t tint_amt;  /* 0..256: how far every colour moves to the tint   */
    int32_t grad_shift; /* added to every gradient's parameter, 1/65536 of
                           its length (with REPEAT or REFLECT: a flow)      */
} zui_vec_override_t;

void zui_vec_override_init(zui_vec_override_t *o, const char *group);

typedef struct {
    zui_vec_xf_t xf;
    uint32_t opacity;
    zui_rect_t clip;
    uint32_t tint, tint_amt;
    int32_t gshift;
    uint32_t *layer; /* non-null: this level draws into a layer */
    zui_rect_t lr;
    uint32_t lop; /* the layer's composite opacity 0..256 */
    uint32_t lused;
} zui_vec_level_t;

/* The drawing state. Initialise with zui_vec_init; the public fields may be
 * changed between shapes. */
typedef struct {
    zui_surface_t *s;
    zui_vec_work_t *w;
    zui_vec_xf_t xf;    /* current transform                                 */
    uint32_t opacity;   /* 0..256 multiplier for every paint                 */
    zui_rect_t clip;    /* device clip (intersected with the surface's)      */
    int32_t tol;        /* flattening tolerance, 1/256 px (default 8)        */
    uint32_t *layer;    /* optional premultiplied ARGB scratch for V6        */
    uint32_t layer_cap; /* its size in pixels                               */
    uint32_t err;       /* ZUI_VEC_E* bits                                   */
    /* private */
    int32_t cx, cy, sx, sy; /* current point and subpath start, q4 user     */
    uint32_t sub;           /* index of the subpath's first point           */
    bool open, seg, restart;
    uint32_t tint, tint_amt;
    int32_t gshift;
    uint32_t depth, lused;
    uint32_t *lpx; /* the current layer (null: the surface) */
    zui_rect_t lr;
    bool measure; /* bounding-box pass: no pixels             */
    int32_t mx0, my0, mx1, my1;
    uint32_t mdepth; /* >0 while inside the measured group     */
    zui_vec_level_t lv[ZUI_VEC_MAX_DEPTH];
} zui_vec_t;

void zui_vec_init(zui_vec_t *v, zui_surface_t *s, zui_vec_work_t *w);

/* ---- transforms (16.16) ------------------------------------------------ */
zui_vec_xf_t zui_vec_identity(void);                      /* one pixel per user unit */
zui_vec_xf_t zui_vec_mul(zui_vec_xf_t m, zui_vec_xf_t n); /* n first, then m */
zui_vec_xf_t zui_vec_translate(int32_t tx_q16, int32_t ty_q16);
zui_vec_xf_t zui_vec_scale(int32_t sx_q16, int32_t sy_q16);
zui_vec_xf_t zui_vec_rotate(uint32_t turn_q16); /* 65536 = one full turn */

/* ---- paths (coordinates in 1/16 user units) ---------------------------- */
void zui_vec_begin(zui_vec_t *v); /* clear the path */
void zui_vec_move_to(zui_vec_t *v, int32_t x, int32_t y);
void zui_vec_line_to(zui_vec_t *v, int32_t x, int32_t y);
void zui_vec_quad_to(zui_vec_t *v, int32_t cx, int32_t cy, int32_t x, int32_t y);
void zui_vec_cubic_to(zui_vec_t *v, int32_t c1x, int32_t c1y, int32_t c2x, int32_t c2y, int32_t x,
                      int32_t y);
/* SVG elliptical arc: radii, x-axis rotation (65536 per turn), flags. */
void zui_vec_arc_to(zui_vec_t *v, int32_t rx, int32_t ry, uint32_t rot, bool large, bool sweep,
                    int32_t x, int32_t y);
void zui_vec_close(zui_vec_t *v);
/* Convenience shapes (append to the path). */
void zui_vec_rect(zui_vec_t *v, int32_t x, int32_t y, int32_t w, int32_t h);
void zui_vec_circle(zui_vec_t *v, int32_t cx, int32_t cy, int32_t r);

/* Paint the path. The path is kept, so it can be filled and then stroked.
 * Return 0, or -1 when the shape was skipped (see err). */
int zui_vec_fill(zui_vec_t *v, const zui_vec_paint_t *p, int rule);
int zui_vec_stroke(zui_vec_t *v, const zui_vec_paint_t *p, const zui_vec_stroke_t *st);
zui_vec_paint_t zui_vec_solid(uint32_t argb);

/* ---- groups ------------------------------------------------------------ */
/* Push a group: m (may be null) is applied before the current transform and
 * opacity (0..256) applies to the group as a whole (V6). Return -1 when the
 * nesting is deeper than ZUI_VEC_MAX_DEPTH (the group is then not pushed).
 * Both push and pop clear the path. */
int zui_vec_push(zui_vec_t *v, const zui_vec_xf_t *m, uint32_t opacity);
void zui_vec_pop(zui_vec_t *v);

/* ---- ZXVG -------------------------------------------------------------- */
/* 0 when the file is well formed, -1 otherwise (V8). */
int zui_zxvg_check(const uint8_t *d, uint32_t n);
/* The viewBox in 1/16 user units; -1 when malformed. */
int zui_zxvg_viewbox(const uint8_t *d, uint32_t n, zui_rect_t *vb);
/* Number of group ids, and the i-th one copied into out[32] (NUL ended). */
int32_t zui_zxvg_id_count(const uint8_t *d, uint32_t n);
int zui_zxvg_id(const uint8_t *d, uint32_t n, uint32_t i, char out[32]);

/* Draw with v's current transform (user units -> device). ov/nov are the
 * optional per-group overrides. 0, or -1 when malformed (nothing drawn). */
int zui_zxvg_draw(zui_vec_t *v, const uint8_t *d, uint32_t n, const zui_vec_override_t *ov,
                  uint32_t nov);

typedef struct {
    uint32_t opacity; /* 0..256; 0 is treated as 256 (fully drawn)          */
    const zui_vec_override_t *ov;
    uint32_t nov;
    uint32_t *layer; /* optional, see V6 */
    uint32_t layer_cap;
    int32_t tol; /* 0 = default */
} zui_vec_opts_t;

/* Draw fitted into box, aspect kept and centred (SVG xMidYMid meet), clipped
 * to box. o may be null. 0, or -1 when malformed. */
int zui_vec_draw_zxvg(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n,
                      zui_rect_t box, const zui_vec_opts_t *o);

/* The simplest call, for launch and boot screens that load the bytes from
 * disk: validate, fit into box (aspect kept, centred), draw at opacity
 * strength (0..256; 0 draws nothing) and, when tint_argb is not 0, hold every
 * paint in every group to tint_argb's RGB, keeping each paint's own alpha (so
 * any non-zero value tints, black included as 0xFF000000). 0, or -1 when the
 * file is malformed (nothing drawn). */
int zui_vec_draw_simple(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n,
                        zui_rect_t box, uint32_t strength, uint32_t tint_argb);

/* Where a named group sits, in its own coordinates (the ones its children
 * use and an override's pivot is given in), 1/16 user units: the box of its
 * flattened geometry (strokes widened by half their width) and its centre as
 * the default pivot. Unanimated. 0, or -1 when absent, empty or malformed. */
typedef struct {
    int32_t x0, y0, x1, y1;
    int32_t px, py;
    uint32_t index; /* 0-based id index */
} zui_vec_group_info_t;
int zui_zxvg_group(const uint8_t *d, uint32_t n, const char *id, zui_vec_work_t *w,
                   zui_vec_group_info_t *out);

/* ---- the launch-screen mark -------------------------------------------- */
/* ctx for zui_vec_mark. The mark is drawn fitted to the launch screen's mark
 * box at opacity dim (0..256) when lit is 0, rising to full at lit 256. */
typedef struct {
    const uint8_t *data;
    uint32_t len;
    zui_vec_work_t *work;
    uint32_t dim;
    const zui_vec_override_t *ov;
    uint32_t nov;
    uint32_t *layer;
    uint32_t layer_cap;
} zui_vec_mark_t;

/* A zui_part_fn (zui_progress.h): set zui_launch_t.mark = zui_vec_mark and
 * ctx[0] = &a_zui_vec_mark_t (or use it for a part). Draws only inside box
 * and leaves the surface's clip as it found it. */
void zui_vec_mark(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx);

#endif /* ZXV_ZUI_VEC_H */
