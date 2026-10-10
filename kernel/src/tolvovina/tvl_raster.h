/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tvl_raster.h — TOL VOVINA UPAAH LOT: the software triangle rasteriser.
 *
 * WHAT THIS IS, HONESTLY
 * ----------------------
 * A scalar, integer, freestanding triangle rasteriser. It writes into a
 * framebuffer the CALLER owns and a depth buffer the CALLER owns. It has no
 * screen-sized state of its own, it never asks for a resolution, and it never
 * assumes one: every dimension arrives as a parameter and every derived
 * quantity (clip box, gradients, spans) is computed from those parameters at
 * runtime. The only compile-time numbers here are CAPACITY and PRECISION
 * limits, never geometry — the same discipline as
 * zxv_shell_set_field_geometry(), which exists because a hardcoded 1280x720
 * tile grid corrupted 43% of a 1440p screen.
 *
 * ARITHMETIC: PLAIN INTEGER FIXED POINT. Not float, not double, and not
 * surplus_real_t either. The reasons are specific:
 *
 *   - Coverage is decided by INTEGER EDGE FUNCTIONS evaluated exactly in
 *     int64. A pixel is in or out; there is no rounding to disagree about, so
 *     the same triangle covers the same pixels on every architecture and on
 *     every replay. The AI replays runs; dimfold demands losslessness. Exact
 *     integer coverage is the only way to promise either.
 *   - Attributes (1/w, u/w, v/w, light) are interpolated in Q16.16 screen-space
 *     planes. All four are AFFINE in screen space, so a plane is not an
 *     approximation of them — it is exactly what they are.
 *   - surplus_real_t (Q32.32) was rejected deliberately: under TEST_HOST it is
 *     a `double`, so a host-verified render path would not be the target render
 *     path. A rasteriser that is only deterministic in one of its two builds is
 *     not deterministic. Q16.16 int32/int64 is the same arithmetic everywhere.
 *
 * NO 64-BIT DIVIDE INSTRUCTION IS ASSUMED. `/` never appears with a variable
 * 64-bit divisor anywhere in this module. Division goes through tvl_udiv64(),
 * a normalised restoring shift-and-subtract divider written in plain C, so no
 * __udivdi3 / __aeabi_ldivmod / __udivti3 is ever emitted and the module links
 * on 32-bit targets that have no libgcc.
 *
 * NO SIMD. Every per-pixel path is scalar, and that is a CORRECTNESS
 * requirement, not a performance choice: the arm64 IRQ vector (boot.s
 * SAVE_REGS) saves x0-x30 and NOT the FP/SIMD register file, so any live value
 * the compiler parks in a v-register is destroyed by a timer interrupt.
 *
 * TEXTURES ARE ADDRESSED IN Z-ORDER (MORTON). Real GPUs swizzle textures this
 * way because a Morton address makes 2D neighbours into near addresses, so a
 * 2x2 texel fetch touches one cache line instead of two rows. We call the
 * kernel's existing zo_encode2() from kernel/src/fractal/zorder.c rather than
 * writing another bit-interleave. Two honesty notes that must travel with this:
 *   - the "32% less data movement" figure attached to zorder is a COMPONENT
 *     PLACEMENT hop-cost result, not a texturing measurement. Nothing here has
 *     measured a texture-bandwidth win. The entitled claim is "2D cache
 *     locality on 2D access patterns", and it is unmeasured on this path.
 *   - Morton is for RUNTIME layout only. Storing texture planes in Morton order
 *     before dimfold makes compression 32.5% WORSE (dimfold's transform is a
 *     1-D Haar and Morton destroys its runs). Store row-major, swizzle at load
 *     with tvl_tex_swizzle().
 *
 * TRI-SPACE IS THE DEPTH LANGUAGE. The final per-pixel stage is holo_shade()
 * from holo.h, driven by tri_role_t:
 *     TRI_POSITIVE  -> depth_z > 0  : PROTRUDES  (available)
 *     TRI_NEGATIVE  -> depth_z < 0  : RECEDES    (withdrawable)
 *     TRI_NEUTRAL   -> depth_z == 0 : screen plane, and holo_shade is then a
 *                                     mathematical IDENTITY (held/unresolved)
 * This is complementary-CHANNEL stereo on one ordinary framebuffer. It is NOT
 * the Virtual Boy's dual-LED per-eye stereo; it needs no headset and it
 * degrades to flat. Do not conflate the two.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (TOL VOVINA rendering slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_TVL_RASTER_H
#define ZXV_TVL_RASTER_H

#include <stdint.h>
#include <stdbool.h>
#include "../trispace/trispace.h"   /* tri_role_t — the S+/S0/S- visual language */

/* ---- PRECISION AND CAPACITY (never geometry) ------------------------------
 * TVL_IZ_SHIFT   scale of the reciprocal depth: iz = 2^TVL_IZ_SHIFT / w.
 * TVL_FRAC       fractional bits of the screen-space attribute planes (Q16.16).
 * TVL_SUB_LOG2   affine span length for the perspective divide, as a power of
 *                two so the per-pixel step is a SHIFT and not a divide. The
 *                classic Doom/Quake technique: divide at span ends, interpolate
 *                between. Correct at the ends, bounded error inside.
 * TVL_TEX_MAX_LOG2  largest texture edge this module will address. zo_encode2
 *                takes uint16_t, so 16 is the absolute ceiling; 12 (4096x4096)
 *                is the capacity we actually admit, because a 2^16 square
 *                texture is a 16 GiB allocation nobody is going to make.
 * TVL_GRAD_MAX   rejection threshold for a sliver triangle whose interpolant
 *                gradient would overflow int64 across the bounding box. This is
 *                a guard, not a resolution. */
#define TVL_IZ_SHIFT       30
#define TVL_FRAC           16
#define TVL_ONE            (1 << TVL_FRAC)
#define TVL_SUB_LOG2       4
#define TVL_SUB            (1 << TVL_SUB_LOG2)
#define TVL_TEX_MAX_LOG2   12
#define TVL_GRAD_MAX       ((int64_t)1 << 40)
/* Sanity clamp on a caller-supplied surface. A panel wider than this is
 * refused rather than silently wrapped; it is a capacity, and the live
 * geometry below it is entirely the caller's. */
#define TVL_DIM_MAX        16384u

/* ---- TEXTURE ---------------------------------------------------------------
 * Square, power-of-two, stored in Z-ORDER: texel (x,y) lives at index
 * zo_encode2(x, y). For a 2^k square that index space is dense and exactly
 * fills 2^(2k) entries, with no holes. Non-square textures are not supported
 * and never will be on this path — a non-square Morton space is sparse, and
 * paying memory for holes to get locality is a bad trade. */
typedef struct {
    const uint32_t *texels;    /* 1 << (2*side_log2) entries, Morton order   */
    uint32_t        side_log2; /* edge = 1 << side_log2, <= TVL_TEX_MAX_LOG2 */
} tvl_tex_t;

/* ---- VERTEX ----------------------------------------------------------------
 * Already projected. The rasteriser owns coverage and interpolation; the caller
 * owns the camera. `iz`, `s` and `t` are the perspective-correct trio: 1/w, u/w
 * and v/w. Build one with tvl_vertex_make() rather than by hand — it is the
 * only place the reciprocal is formed, and it applies the near clamp. */
typedef struct {
    int32_t x, y;      /* screen pixel, integer                              */
    int32_t iz;        /* 1/w,  Q(TVL_IZ_SHIFT) scaled; LARGER = NEARER      */
    int32_t s, t;      /* u/w, v/w in texel units, same scale as iz          */
    int32_t light;     /* baked light 0..255, interpolated per pixel         */
} tvl_vertex_t;

/* ---- MATERIAL --------------------------------------------------------------
 * `tex` NULL means flat `rgb`. `role` selects the tri-space depth polarity.
 * `shimmer` is the holo valence (the S-..S+ interference field); 0 with
 * TRI_NEUTRAL makes the whole shading stage an exact identity, which is what
 * the self-check relies on to compare pixels bit-for-bit. */
typedef struct {
    const tvl_tex_t *tex;
    uint32_t         rgb;         /* flat colour when tex == NULL            */
    tri_role_t       role;        /* S+ protrudes / S- recedes / S0 flat     */
    int32_t          shimmer;     /* holo valence, ~[-200,200]               */
    int32_t          depth_gain;  /* chromostereopsis strength (holo depth_k)*/
} tvl_material_t;

/* ---- TARGET ----------------------------------------------------------------
 * The caller owns every byte here. `zb` may be NULL, which disables depth
 * testing entirely (useful for a background pass). Strides are in ELEMENTS,
 * not bytes, and may exceed w — a scanout surface is frequently wider than the
 * visible area. Nothing in this struct is filled from a constant. */
typedef struct {
    uint32_t *fb;          /* XRGB8888, caller-supplied                      */
    uint32_t  fb_stride;   /* pixels per row                                 */
    int32_t  *zb;          /* reciprocal-depth buffer, caller-supplied, or 0 */
    uint32_t  zb_stride;   /* entries per row                                */
    uint32_t  w, h;        /* REAL visible geometry, from the caller         */
    int32_t   cx0, cy0;    /* derived clip box, inclusive                    */
    int32_t   cx1, cy1;    /* derived clip box, inclusive                    */
    uint32_t  ready;       /* 0 until tvl_target_init() has accepted it      */
} tvl_target_t;

/* Bind a surface and DERIVE its clip geometry. Returns the number of visible
 * pixels the target now describes, or 0 if the surface was rejected (null fb,
 * zero or over-capacity dimensions, stride narrower than the width). Nothing
 * is allocated and nothing is remembered between calls. */
uint32_t tvl_target_init(tvl_target_t *t,
                         uint32_t *fb, uint32_t fb_stride,
                         int32_t  *zb, uint32_t zb_stride,
                         uint32_t w, uint32_t h);

/* Fill the visible area with `rgb` and reset the depth buffer to "infinitely
 * far" (iz == 0). Honours stride; touches nothing outside w x h. */
void tvl_target_clear(tvl_target_t *t, uint32_t rgb);

/* ---- vertex construction ---------------------------------------------------
 * w_16_16 is view-space depth in Q16.16 and must be positive; it is clamped up
 * to a near plane so the reciprocal cannot explode past int32. u/v are texel
 * coordinates in Q16.16. This is where the ONE divide per vertex happens —
 * never per pixel, and never through a float. */
void tvl_vertex_make(tvl_vertex_t *v, int32_t x, int32_t y,
                     int32_t w_16_16, int32_t u_16_16, int32_t v_16_16,
                     int32_t light);

/* ---- rasterisation ---------------------------------------------------------
 * Draws one triangle. Winding-agnostic (a negative area is re-wound rather than
 * culled, because interiors need both faces). Returns the number of pixels
 * actually written — pixels rejected by the depth test are NOT counted, which
 * is what makes occlusion testable. */
uint32_t tvl_triangle(tvl_target_t *t, const tvl_vertex_t v[3],
                      const tvl_material_t *m);

/* ---- textures --------------------------------------------------------------
 * Convert a row-major source into the Morton layout tvl_tex_sample() expects.
 * `dst` must hold 1 << (2*side_log2) entries. Returns the entry count written,
 * 0 on a rejected argument. STORE row-major, SWIZZLE at load: see the dimfold
 * note at the top of this file. */
uint32_t tvl_tex_swizzle(uint32_t *dst_morton, const uint32_t *src_rowmajor,
                         uint32_t side_log2);

/* Sample with wraparound. u/v are Q16.16 texel coordinates and may be negative;
 * the wrap is a mask, so it is exact and branch-free. */
uint32_t tvl_tex_sample(const tvl_tex_t *tex, int32_t u_16_16, int32_t v_16_16);

/* ---- integer division ------------------------------------------------------
 * Normalised restoring division, exported because it is the module's only
 * divide and the self-check verifies it against known quotients. Returns
 * UINT64_MAX for d == 0 (saturate rather than trap in a render loop). */
uint64_t tvl_udiv64(uint64_t n, uint64_t d);
int64_t  tvl_idiv64(int64_t n, int64_t d);   /* truncates toward zero */

/* ---- self-check ------------------------------------------------------------
 * Rasterises known triangles into a small caller-independent fixture and
 * verifies EXACT coverage counts, EXACT depth values, depth rejection, the
 * Morton round-trip, perspective correctness against the analytic value, and
 * the S+/S0/S- polarity. Returns 1 on pass, 0 on failure. */
int tvl_raster_selfcheck(void);

/* Which sub-test failed, for boot diagnostics: a bitmask of the checks that did
 * NOT pass. 0 means everything passed (and means nothing until the self-check
 * has been run at least once). */
#define TVL_CHK_DIV       0x01u
#define TVL_CHK_MORTON    0x02u
#define TVL_CHK_COVERAGE  0x04u
#define TVL_CHK_DEPTH     0x08u
#define TVL_CHK_OCCLUDE   0x10u
#define TVL_CHK_PERSP     0x20u
#define TVL_CHK_TRISPACE  0x40u
uint32_t tvl_raster_last_failures(void);

#endif /* ZXV_TVL_RASTER_H */
