/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* tvl_raster.c — TOL VOVINA UPAAH LOT software triangle rasteriser.
 * See tvl_raster.h for the arithmetic choice and the honesty notes. */
#include "tvl_raster.h"
/* Explicit relative paths, not include-path lookups: there are TWO holo.h in
 * this tree (kernel/src/emu and kernel/src/holographic) and only one of them
 * declares holo_shade. Resolving that by CFLAGS ordering would be a silent
 * time bomb. Same precedent as fusion.c -> ../robin_debanks/sha256.h. */
#include "../fractal/zorder.h"  /* zo_encode2 — the Morton texture address. Not reimplemented. */
#include "../emu/holo.h"        /* holo_shade — the tri-space depth language. Not reimplemented. */

/* ===========================================================================
 * DIVISION WITHOUT A DIVIDE INSTRUCTION
 *
 * `/` on a variable 64-bit divisor lowers to __udivdi3 / __aeabi_ldivmod on a
 * 32-bit target, and those do not exist in a kernel with no libgcc. So the
 * whole module routes through this: count the leading zeros, align the divisor
 * to the dividend, then restoring shift-and-subtract for exactly as many bits
 * as the quotient can occupy. Shifts and compares only.
 * =========================================================================== */
static int tvl_clz64(uint64_t v) {
    int n = 0;
    if (v == 0u) return 64;
    while ((v & 0x8000000000000000ull) == 0u) { v <<= 1; n++; }
    return n;
}

uint64_t tvl_udiv64(uint64_t n, uint64_t d) {
    if (d == 0u) return ~(uint64_t)0;       /* saturate; a render loop must not trap */
    if (n < d)   return 0u;
    int shift = tvl_clz64(d) - tvl_clz64(n);   /* >= 0 here, since n >= d */
    uint64_t dd = d << shift;                  /* cannot overflow: shift <= clz(d) */
    uint64_t q = 0u;
    for (int i = shift; i >= 0; i--) {
        q <<= 1;
        if (n >= dd) { n -= dd; q |= 1u; }
        dd >>= 1;
    }
    return q;
}

int64_t tvl_idiv64(int64_t n, int64_t d) {
    int neg = 0;
    uint64_t un, ud, q;
    /* Negate through unsigned so INT64_MIN is not signed-overflow UB. */
    if (n < 0) { un = (uint64_t)0 - (uint64_t)n; neg ^= 1; } else un = (uint64_t)n;
    if (d < 0) { ud = (uint64_t)0 - (uint64_t)d; neg ^= 1; } else ud = (uint64_t)d;
    if (ud == 0u) return 0;
    q = tvl_udiv64(un, ud);
    return neg ? -(int64_t)q : (int64_t)q;
}

/* ===========================================================================
 * TARGET — geometry is DERIVED, never fixed
 * =========================================================================== */
uint32_t tvl_target_init(tvl_target_t *t,
                         uint32_t *fb, uint32_t fb_stride,
                         int32_t  *zb, uint32_t zb_stride,
                         uint32_t w, uint32_t h) {
    if (!t) return 0u;
    t->ready = 0u;
    if (!fb || w == 0u || h == 0u) return 0u;
    if (w > TVL_DIM_MAX || h > TVL_DIM_MAX) return 0u;   /* capacity, not geometry */
    if (fb_stride == 0u) fb_stride = w;                  /* the common packed case */
    if (fb_stride < w) return 0u;                        /* refuse, never wrap      */
    if (zb) {
        if (zb_stride == 0u) zb_stride = w;
        if (zb_stride < w) return 0u;
    } else {
        zb_stride = 0u;
    }
    t->fb = fb; t->fb_stride = fb_stride;
    t->zb = zb; t->zb_stride = zb_stride;
    t->w = w;   t->h = h;
    /* The clip box is a FUNCTION of the surface it was handed. Nothing here
     * knows or cares what a "standard mode" is. */
    t->cx0 = 0; t->cy0 = 0;
    t->cx1 = (int32_t)w - 1;
    t->cy1 = (int32_t)h - 1;
    t->ready = 1u;
    return w * h;
}

void tvl_target_clear(tvl_target_t *t, uint32_t rgb) {
    if (!t || !t->ready) return;
    uint32_t px = 0xFF000000u | (rgb & 0x00FFFFFFu);
    for (uint32_t y = 0u; y < t->h; y++) {
        uint32_t *row = t->fb + (uint64_t)y * t->fb_stride;
        for (uint32_t x = 0u; x < t->w; x++) row[x] = px;
        if (t->zb) {
            int32_t *zrow = t->zb + (uint64_t)y * t->zb_stride;
            for (uint32_t x = 0u; x < t->w; x++) zrow[x] = 0;   /* 1/w == 0 = infinitely far */
        }
    }
}

/* ===========================================================================
 * VERTEX — the one reciprocal per vertex
 * =========================================================================== */
/* Near plane. Below this the reciprocal grows until s = u*iz overflows int32,
 * so it is clamped rather than left to wrap. 0.1 in Q16.16, matching the
 * near clip the other renderers in this tree already use. */
#define TVL_W_MIN  6553

void tvl_vertex_make(tvl_vertex_t *v, int32_t x, int32_t y,
                     int32_t w_16_16, int32_t u_16_16, int32_t v_16_16,
                     int32_t light) {
    if (!v) return;
    if (w_16_16 < TVL_W_MIN) w_16_16 = TVL_W_MIN;
    int32_t iz = (int32_t)tvl_udiv64((uint64_t)1 << TVL_IZ_SHIFT, (uint64_t)w_16_16);
    v->x = x; v->y = y;
    v->iz = iz;
    /* s = u/w, t = v/w — carried in the SAME scale as iz so the perspective
     * divide later is a plain (s << 16) / iz with no rescale. */
    v->s = (int32_t)((((int64_t)u_16_16 * (int64_t)iz)) >> TVL_FRAC);
    v->t = (int32_t)((((int64_t)v_16_16 * (int64_t)iz)) >> TVL_FRAC);
    if (light < 0)   light = 0;
    if (light > 255) light = 255;
    v->light = light;
}

/* ===========================================================================
 * TEXTURE — Z-ORDER ADDRESSING via the existing zorder.c
 *
 * The API fits directly: zo_encode2() takes two uint16_t and returns the
 * interleaved code. All this module adds is the power-of-two wrap mask, so the
 * coordinates handed to zo_encode2 are always inside the texture and the code
 * is always inside the 2^(2k) allocation. No Morton arithmetic is duplicated
 * here — spread16/compact16 stay where they are.
 * =========================================================================== */
uint32_t tvl_tex_swizzle(uint32_t *dst_morton, const uint32_t *src_rowmajor,
                         uint32_t side_log2) {
    if (!dst_morton || !src_rowmajor) return 0u;
    if (side_log2 == 0u || side_log2 > TVL_TEX_MAX_LOG2) return 0u;
    uint32_t side = 1u << side_log2;
    for (uint32_t y = 0u; y < side; y++) {
        for (uint32_t x = 0u; x < side; x++) {
            dst_morton[zo_encode2((uint16_t)x, (uint16_t)y)] = src_rowmajor[y * side + x];
        }
    }
    return side * side;
}

uint32_t tvl_tex_sample(const tvl_tex_t *tex, int32_t u_16_16, int32_t v_16_16) {
    if (!tex || !tex->texels) return 0u;
    uint32_t sl = tex->side_log2;
    if (sl == 0u || sl > TVL_TEX_MAX_LOG2) return 0u;
    int32_t mask = (int32_t)((1u << sl) - 1u);
    /* Arithmetic shift then mask: exact wrap for negative coordinates too. */
    int32_t tx = (u_16_16 >> TVL_FRAC) & mask;
    int32_t ty = (v_16_16 >> TVL_FRAC) & mask;
    return tex->texels[zo_encode2((uint16_t)tx, (uint16_t)ty)];
}

/* ===========================================================================
 * SCREEN-SPACE ATTRIBUTE PLANES
 *
 * 1/w, u/w, v/w and light are all AFFINE in screen space, so each is exactly
 * a plane: A(x,y) = A0 + gx*(x-x0) + gy*(y-y0). Solving the two-point system
 * against the doubled area gives the gradients below. They are formed ONCE per
 * triangle in Q16.16 and then accumulated by addition, which is bit-identical
 * to evaluating the plane directly at every pixel — the accumulation
 * introduces no drift of its own, so a pixel's value does not depend on where
 * the bounding box started. That is what makes a replay reproduce a frame.
 * =========================================================================== */
typedef struct { int64_t gx, gy; } tvl_grad_t;

static bool grad_setup(tvl_grad_t *g,
                       int32_t a0, int32_t a1, int32_t a2,
                       int64_t dx1, int64_t dy1, int64_t dx2, int64_t dy2,
                       int64_t area2) {
    int64_t da1 = (int64_t)a1 - (int64_t)a0;
    int64_t da2 = (int64_t)a2 - (int64_t)a0;
    int64_t nx = da1 * dy2 - da2 * dy1;
    int64_t ny = da2 * dx1 - da1 * dx2;
    g->gx = tvl_idiv64(nx << TVL_FRAC, area2);
    g->gy = tvl_idiv64(ny << TVL_FRAC, area2);
    /* Sliver guard: a near-degenerate triangle can produce a gradient so steep
     * that gx * (bounding box width) leaves int64. Refuse the triangle rather
     * than render wrapped garbage. This is a numeric guard, not a resolution. */
    if (g->gx > TVL_GRAD_MAX || g->gx < -TVL_GRAD_MAX) return false;
    if (g->gy > TVL_GRAD_MAX || g->gy < -TVL_GRAD_MAX) return false;
    return true;
}

/* Perspective divide: recover a Q16.16 texel coordinate from (u/w, 1/w). */
static int64_t persp(int64_t s_int, int32_t iz_int) {
    if (iz_int <= 0) return 0;
    return tvl_idiv64(s_int << TVL_FRAC, (int64_t)iz_int);
}

/* ===========================================================================
 * THE TRIANGLE
 * =========================================================================== */
uint32_t tvl_triangle(tvl_target_t *t, const tvl_vertex_t vin[3],
                      const tvl_material_t *m) {
    if (!t || !t->ready || !vin || !m) return 0u;

    tvl_vertex_t v[3];
    v[0] = vin[0]; v[1] = vin[1]; v[2] = vin[2];

    int64_t dx1 = (int64_t)v[1].x - v[0].x, dy1 = (int64_t)v[1].y - v[0].y;
    int64_t dx2 = (int64_t)v[2].x - v[0].x, dy2 = (int64_t)v[2].y - v[0].y;
    int64_t area2 = dx1 * dy2 - dx2 * dy1;
    if (area2 == 0) return 0u;                       /* degenerate */
    if (area2 < 0) {                                 /* re-wind, do not cull:  */
        tvl_vertex_t sw = v[1]; v[1] = v[2]; v[2] = sw;   /* interiors need both faces */
        dx1 = (int64_t)v[1].x - v[0].x; dy1 = (int64_t)v[1].y - v[0].y;
        dx2 = (int64_t)v[2].x - v[0].x; dy2 = (int64_t)v[2].y - v[0].y;
        area2 = -area2;
    }

    /* Bounding box, clipped to the box the TARGET derived from its own size. */
    int32_t minx = v[0].x, maxx = v[0].x, miny = v[0].y, maxy = v[0].y;
    for (int i = 1; i < 3; i++) {
        if (v[i].x < minx) minx = v[i].x;
        if (v[i].x > maxx) maxx = v[i].x;
        if (v[i].y < miny) miny = v[i].y;
        if (v[i].y > maxy) maxy = v[i].y;
    }
    if (minx < t->cx0) minx = t->cx0;
    if (miny < t->cy0) miny = t->cy0;
    if (maxx > t->cx1) maxx = t->cx1;
    if (maxy > t->cy1) maxy = t->cy1;
    if (minx > maxx || miny > maxy) return 0u;

    /* Edge functions. E_ab(x,y) = (bx-ax)(y-ay) - (by-ay)(x-ax); it is linear,
     * so its per-step deltas are constants and the inner loop is pure addition.
     * Coverage is therefore decided by EXACT int64 signs — no rounding exists
     * to disagree with, on any architecture or any replay. */
    int64_t e0x = -((int64_t)v[2].y - v[1].y), e0y = ((int64_t)v[2].x - v[1].x);
    int64_t e1x = -((int64_t)v[0].y - v[2].y), e1y = ((int64_t)v[0].x - v[2].x);
    int64_t e2x = -((int64_t)v[1].y - v[0].y), e2y = ((int64_t)v[1].x - v[0].x);
    int64_t e0row = ((int64_t)v[2].x - v[1].x) * ((int64_t)miny - v[1].y)
                  - ((int64_t)v[2].y - v[1].y) * ((int64_t)minx - v[1].x);
    int64_t e1row = ((int64_t)v[0].x - v[2].x) * ((int64_t)miny - v[2].y)
                  - ((int64_t)v[0].y - v[2].y) * ((int64_t)minx - v[2].x);
    int64_t e2row = ((int64_t)v[1].x - v[0].x) * ((int64_t)miny - v[0].y)
                  - ((int64_t)v[1].y - v[0].y) * ((int64_t)minx - v[0].x);

    tvl_grad_t giz, gs, gt, gl;
    if (!grad_setup(&giz, v[0].iz,    v[1].iz,    v[2].iz,    dx1, dy1, dx2, dy2, area2)) return 0u;
    if (!grad_setup(&gs,  v[0].s,     v[1].s,     v[2].s,     dx1, dy1, dx2, dy2, area2)) return 0u;
    if (!grad_setup(&gt,  v[0].t,     v[1].t,     v[2].t,     dx1, dy1, dx2, dy2, area2)) return 0u;
    if (!grad_setup(&gl,  v[0].light, v[1].light, v[2].light, dx1, dy1, dx2, dy2, area2)) return 0u;

    int64_t ox = (int64_t)minx - v[0].x, oy = (int64_t)miny - v[0].y;
    int64_t iz_row = ((int64_t)v[0].iz    << TVL_FRAC) + giz.gx * ox + giz.gy * oy;
    int64_t s_row  = ((int64_t)v[0].s     << TVL_FRAC) + gs.gx  * ox + gs.gy  * oy;
    int64_t t_row  = ((int64_t)v[0].t     << TVL_FRAC) + gt.gx  * ox + gt.gy  * oy;
    int64_t l_row  = ((int64_t)v[0].light << TVL_FRAC) + gl.gx  * ox + gl.gy  * oy;

    const tvl_tex_t *tex = m->tex;
    int32_t depth_k = m->depth_gain;
    int32_t shim    = m->shimmer;
    uint32_t written = 0u;
    int32_t span_w = maxx - minx + 1;

    for (int32_t y = miny; y <= maxy; y++) {
        int64_t w0 = e0row, w1 = e1row, w2 = e2row;
        int64_t iz_v = iz_row, s_v = s_row, t_v = t_row, l_v = l_row;
        uint32_t *fbrow = t->fb + (uint64_t)(uint32_t)y * t->fb_stride;
        int32_t  *zbrow = t->zb ? (t->zb + (uint64_t)(uint32_t)y * t->zb_stride) : (int32_t *)0;

        /* Affine span state for the perspective divide. */
        int32_t span_left = 0;
        int     span_exact = 0;
        int64_t u_cur = 0, v_cur = 0, du = 0, dv = 0;

        for (int32_t x = minx; x <= maxx; x++) {
            if (tex && span_left == 0) {
                /* Divide at the span ends, step linearly between: the classic
                 * technique, and the reason there is no divide in the common
                 * per-pixel path. If the far end has crossed the horizon
                 * (1/w <= 0) the linear step would be meaningless, so that one
                 * span falls back to an exact per-pixel divide. */
                int32_t o = x - minx;
                int64_t izA = iz_v;
                int64_t izB = iz_row + giz.gx * (int64_t)(o + TVL_SUB);
                int32_t iA = (int32_t)(izA >> TVL_FRAC);
                int32_t iB = (int32_t)(izB >> TVL_FRAC);
                if (iA > 0 && iB > 0) {
                    int64_t sB = s_row + gs.gx * (int64_t)(o + TVL_SUB);
                    int64_t tB = t_row + gt.gx * (int64_t)(o + TVL_SUB);
                    u_cur = persp(s_v >> TVL_FRAC, iA);
                    v_cur = persp(t_v >> TVL_FRAC, iA);
                    du = (persp(sB >> TVL_FRAC, iB) - u_cur) >> TVL_SUB_LOG2;
                    dv = (persp(tB >> TVL_FRAC, iB) - v_cur) >> TVL_SUB_LOG2;
                    span_exact = 0;
                } else {
                    span_exact = 1;
                    du = dv = 0;
                }
                span_left = TVL_SUB;
                if (span_left > span_w - o) span_left = span_w - o;
            }

            if ((w0 | w1 | w2) >= 0) {                 /* inside: all edges >= 0 */
                int32_t iz = (int32_t)(iz_v >> TVL_FRAC);
                int ok = 1;
                if (zbrow) {
                    if (iz <= zbrow[x]) ok = 0;        /* larger iz = nearer     */
                    else zbrow[x] = iz;
                }
                if (ok) {
                    uint32_t base;
                    if (tex) {
                        int64_t uu = u_cur, vv = v_cur;
                        if (span_exact) {
                            uu = persp(s_v >> TVL_FRAC, iz);
                            vv = persp(t_v >> TVL_FRAC, iz);
                        }
                        base = tvl_tex_sample(tex, (int32_t)uu, (int32_t)vv);
                    } else {
                        base = m->rgb & 0x00FFFFFFu;
                    }
                    int32_t lum = (int32_t)(l_v >> TVL_FRAC);
                    if (lum < 0) lum = 0; else if (lum > 255) lum = 255;
                    int32_t r = (int32_t)((base >> 16) & 0xFFu) * lum / 255;
                    int32_t g = (int32_t)((base >>  8) & 0xFFu) * lum / 255;
                    int32_t b = (int32_t)( base        & 0xFFu) * lum / 255;
                    uint32_t lit = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

                    /* TRI-SPACE DEPTH. The magnitude comes from 1/w (near
                     * things protrude further); the SIGN comes from the role,
                     * which is what makes depth carry meaning rather than
                     * decoration. TRI_NEUTRAL gives depth_z == 0, and with
                     * shimmer 0 holo_shade is then an exact identity — the S0
                     * "held, zero parallax" case, verified in the self-check. */
                    int32_t mag = iz >> 9;
                    if (mag < 0)  mag = 0;
                    if (mag > 44) mag = 44;
                    int32_t depth_z = (m->role == TRI_POSITIVE) ?  mag
                                    : (m->role == TRI_NEGATIVE) ? -mag : 0;
                    fbrow[x] = 0xFF000000u | holo_shade(lit, shim, depth_z, 8, depth_k);
                    written++;
                }
            }

            w0 += e0x; w1 += e1x; w2 += e2x;
            iz_v += giz.gx; s_v += gs.gx; t_v += gt.gx; l_v += gl.gx;
            u_cur += du; v_cur += dv;
            if (span_left > 0) span_left--;
        }

        e0row += e0y; e1row += e1y; e2row += e2y;
        iz_row += giz.gy; s_row += gs.gy; t_row += gt.gy; l_row += gl.gy;
    }
    return written;
}

/* ===========================================================================
 * SELF-CHECK
 *
 * Not a smoke test. Every expectation below is derived independently of the
 * implementation — by counting lattice points, or from the analytic
 * perspective formula — so the check can actually fail.
 *
 * The fixtures are small and static ON PURPOSE: they are the TEST's memory,
 * not the module's. tvl_triangle() itself still owns no screen-sized state,
 * which is the property being protected.
 * =========================================================================== */
#define CK_W   64
#define CK_H   16
#define CK_TL  4                      /* test texture edge = 16 */
#define CK_TN  (1u << (2 * CK_TL))    /* 256 texels */

static uint32_t g_ck_fb[CK_W * CK_H];
static int32_t  g_ck_zb[CK_W * CK_H];
static uint32_t g_ck_src[CK_TN];
static uint32_t g_ck_mor[CK_TN];
static uint32_t g_ck_fail;

uint32_t tvl_raster_last_failures(void) { return g_ck_fail; }

int tvl_raster_selfcheck(void) {
    uint32_t fail = 0u;
    tvl_target_t tgt;
    tvl_vertex_t v[3];
    tvl_material_t mat;

    /* ---- 1. the divider ---------------------------------------------------
     * Known quotients, computed by hand. If restoring division is wrong every
     * gradient in the module is wrong, so this is checked first. */
    if (tvl_udiv64(1000u, 7u) != 142u)                       fail |= TVL_CHK_DIV;
    if (tvl_udiv64((uint64_t)1 << 40, 3u) != 366503875925ull) fail |= TVL_CHK_DIV;
    if (tvl_udiv64(5u, 10u) != 0u)                            fail |= TVL_CHK_DIV;
    if (tvl_udiv64(0u, 9u) != 0u)                             fail |= TVL_CHK_DIV;
    if (tvl_udiv64(7u, 0u) != ~(uint64_t)0)                   fail |= TVL_CHK_DIV;
    if (tvl_idiv64(-1000, 7) != -142)                         fail |= TVL_CHK_DIV;
    if (tvl_idiv64(-1000, -7) != 142)                         fail |= TVL_CHK_DIV;
    if (tvl_idiv64((int64_t)1 << 62, (int64_t)1 << 31) != ((int64_t)1 << 31))
                                                              fail |= TVL_CHK_DIV;

    /* ---- 2. Morton texture addressing -------------------------------------
     * Build a row-major 16x16 whose texel encodes its own coordinates, swizzle
     * it through zo_encode2, then sample every texel back and require an EXACT
     * match. Also assert the layout really was permuted: row-major would place
     * (0,1) at index 16, Morton places it at index 2. A memcpy would pass the
     * round-trip and fail this. */
    {
        uint32_t side = 1u << CK_TL;
        for (uint32_t yy = 0u; yy < side; yy++)
            for (uint32_t xx = 0u; xx < side; xx++)
                g_ck_src[yy * side + xx] = (yy << 8) | xx;
        if (tvl_tex_swizzle(g_ck_mor, g_ck_src, CK_TL) != side * side) fail |= TVL_CHK_MORTON;
        if (g_ck_mor[2] != g_ck_src[1u * side + 0u])                   fail |= TVL_CHK_MORTON;
        if (g_ck_mor[1] != g_ck_src[0u * side + 1u])                   fail |= TVL_CHK_MORTON;
        if (g_ck_mor[16] == g_ck_src[1u * side + 0u])                  fail |= TVL_CHK_MORTON;
        tvl_tex_t tx = { g_ck_mor, CK_TL };
        for (uint32_t yy = 0u; yy < side; yy++)
            for (uint32_t xx = 0u; xx < side; xx++)
                if (tvl_tex_sample(&tx, (int32_t)(xx << TVL_FRAC), (int32_t)(yy << TVL_FRAC))
                    != g_ck_src[yy * side + xx]) fail |= TVL_CHK_MORTON;
        /* negative coordinates must wrap exactly, not clamp */
        if (tvl_tex_sample(&tx, -(1 << TVL_FRAC), 0) != g_ck_src[0u * side + (side - 1u)])
            fail |= TVL_CHK_MORTON;
    }

    /* ---- 3. exact coverage ------------------------------------------------
     * Triangle (0,0),(8,0),(0,8) sampled at integer pixel centres with an
     * inclusive all-edges>=0 rule covers exactly { x>=0, y>=0, x+y<=8 }, which
     * is 9+8+...+1 = 45 lattice points. That number comes from counting, not
     * from running the rasteriser.
     * (Note the rule is inclusive on every edge, so two triangles sharing an
     * edge both draw it. That matches the other renderers in this tree and is
     * correct for opaque interiors; it is not a top-left fill rule.) */
    if (tvl_target_init(&tgt, g_ck_fb, CK_W, g_ck_zb, CK_W, CK_W, CK_H) != (uint32_t)(CK_W * CK_H))
        fail |= TVL_CHK_COVERAGE;
    tvl_target_clear(&tgt, 0x000000u);

    mat.tex = (const tvl_tex_t *)0;
    mat.rgb = 0x804020u;
    mat.role = TRI_NEUTRAL;
    mat.shimmer = 0;
    mat.depth_gain = 128;

    tvl_vertex_make(&v[0], 0, 0, 4 << TVL_FRAC, 0, 0, 255);
    tvl_vertex_make(&v[1], 8, 0, 4 << TVL_FRAC, 0, 0, 255);
    tvl_vertex_make(&v[2], 0, 8, 4 << TVL_FRAC, 0, 0, 255);
    uint32_t drawn = tvl_triangle(&tgt, v, &mat);
    if (drawn != 45u) fail |= TVL_CHK_COVERAGE;
    {
        uint32_t covered = 0u, exact_colour = 0u;
        int32_t izc = v[0].iz;                       /* constant across the face */
        uint32_t exact_depth = 0u;
        for (int32_t yy = 0; yy < CK_H; yy++)
            for (int32_t xx = 0; xx < CK_W; xx++) {
                int32_t idx = yy * CK_W + xx;
                int inside = (xx >= 0 && yy >= 0 && (xx + yy) <= 8);
                if (g_ck_zb[idx] != 0) covered++;
                if (inside) {
                    /* TRI_NEUTRAL + shimmer 0 + light 255 => holo_shade is an
                     * identity, so the pixel must be the material colour to
                     * the bit. */
                    if (g_ck_fb[idx] == (0xFF000000u | 0x804020u)) exact_colour++;
                    /* all three vertices share 1/w, so the plane is flat and
                     * the depth must be that value EXACTLY, not approximately */
                    if (g_ck_zb[idx] == izc) exact_depth++;
                }
            }
        if (covered != 45u)      fail |= TVL_CHK_COVERAGE;
        if (exact_colour != 45u) fail |= TVL_CHK_COVERAGE;
        if (exact_depth != 45u)  fail |= TVL_CHK_DEPTH;
        if (izc != (int32_t)((1u << TVL_IZ_SHIFT) / (4u << TVL_FRAC))) fail |= TVL_CHK_DEPTH;
    }

    /* ---- 4. occlusion -----------------------------------------------------
     * The same face pushed FARTHER must write nothing at all; pushed NEARER it
     * must write all 45 and replace the depth. This is the property that makes
     * the z-buffer real rather than decorative. */
    {
        tvl_material_t far_m = mat;
        far_m.rgb = 0x00FF00u;
        tvl_vertex_make(&v[0], 0, 0, 8 << TVL_FRAC, 0, 0, 255);
        tvl_vertex_make(&v[1], 8, 0, 8 << TVL_FRAC, 0, 0, 255);
        tvl_vertex_make(&v[2], 0, 8, 8 << TVL_FRAC, 0, 0, 255);
        if (tvl_triangle(&tgt, v, &far_m) != 0u) fail |= TVL_CHK_OCCLUDE;
        if (g_ck_fb[0] != (0xFF000000u | 0x804020u)) fail |= TVL_CHK_OCCLUDE;

        tvl_vertex_make(&v[0], 0, 0, 2 << TVL_FRAC, 0, 0, 255);
        tvl_vertex_make(&v[1], 8, 0, 2 << TVL_FRAC, 0, 0, 255);
        tvl_vertex_make(&v[2], 0, 8, 2 << TVL_FRAC, 0, 0, 255);
        if (tvl_triangle(&tgt, v, &far_m) != 45u) fail |= TVL_CHK_OCCLUDE;
        if (g_ck_fb[0] != (0xFF000000u | 0x00FF00u)) fail |= TVL_CHK_OCCLUDE;
        if (g_ck_zb[0] != v[0].iz) fail |= TVL_CHK_OCCLUDE;
    }

    /* ---- 5. perspective correctness ---------------------------------------
     * A triangle with v0 at w=1 (u=0) and v1 at w=4 (u=15 texels), both on row
     * y=0 of a 64-wide target. At screen fraction a along that edge the
     * PERSPECTIVE-correct texture coordinate is
     *        u(a) = a*U / (4 - 3a)        [ from (a*U/4) / ((1-a) + a/4) ]
     * while a naive affine interpolation would give a*U. At x=32,
     * a = 32/63 = 0.50794, so u = 0.50794*15 / 2.4762 = 3.08 texels — versus
     * 7.6 for the affine answer. The texture's blue channel holds its own x,
     * so the drawn pixel reports the texel that was really fetched.
     * Passing this cannot happen by accident: affine lands outside the band. */
    {
        tvl_tex_t tx = { g_ck_mor, CK_TL };
        tvl_material_t tm;
        tm.tex = &tx; tm.rgb = 0u; tm.role = TRI_NEUTRAL;
        tm.shimmer = 0; tm.depth_gain = 128;
        tvl_target_clear(&tgt, 0x000000u);
        tvl_vertex_make(&v[0],  0, 0, 1 << TVL_FRAC,  0,               0, 255);
        tvl_vertex_make(&v[1], 63, 0, 4 << TVL_FRAC, 15 << TVL_FRAC,   0, 255);
        tvl_vertex_make(&v[2],  0, 3, 1 << TVL_FRAC,  0,               0, 255);
        if (tvl_triangle(&tgt, v, &tm) == 0u) fail |= TVL_CHK_PERSP;
        uint32_t px = g_ck_fb[0 * CK_W + 32] & 0xFFu;   /* blue channel = texel x */
        if (px < 2u || px > 5u) fail |= TVL_CHK_PERSP;  /* analytic 3.08, affine 7.6 */
        /* and the two endpoints must be exact, since a span boundary sits on
         * an exact divide there */
        if ((g_ck_fb[0 * CK_W + 0] & 0xFFu) != 0u) fail |= TVL_CHK_PERSP;
    }

    /* ---- 6. tri-space polarity --------------------------------------------
     * S+ must protrude (warmer: red up, blue down), S- must recede (the exact
     * mirror), S0 must be the identity. Same triangle, same colour, three
     * roles — the ONLY difference is the tri_role_t. */
    {
        uint32_t pos, neg, neu;
        tvl_material_t pm = mat;
        pm.rgb = 0x804020u;
        tvl_vertex_make(&v[0], 0, 0, 4 << TVL_FRAC, 0, 0, 255);
        tvl_vertex_make(&v[1], 8, 0, 4 << TVL_FRAC, 0, 0, 255);
        tvl_vertex_make(&v[2], 0, 8, 4 << TVL_FRAC, 0, 0, 255);

        pm.role = TRI_NEUTRAL;
        tvl_target_clear(&tgt, 0x000000u);
        (void)tvl_triangle(&tgt, v, &pm); neu = g_ck_fb[0] & 0x00FFFFFFu;
        pm.role = TRI_POSITIVE;
        tvl_target_clear(&tgt, 0x000000u);
        (void)tvl_triangle(&tgt, v, &pm); pos = g_ck_fb[0] & 0x00FFFFFFu;
        pm.role = TRI_NEGATIVE;
        tvl_target_clear(&tgt, 0x000000u);
        (void)tvl_triangle(&tgt, v, &pm); neg = g_ck_fb[0] & 0x00FFFFFFu;

        if (neu != 0x804020u) fail |= TVL_CHK_TRISPACE;             /* S0 = identity */
        int32_t rp = (int32_t)((pos >> 16) & 0xFFu), bp = (int32_t)(pos & 0xFFu);
        int32_t rn = (int32_t)((neg >> 16) & 0xFFu), bn = (int32_t)(neg & 0xFFu);
        int32_t r0 = (int32_t)((neu >> 16) & 0xFFu), b0 = (int32_t)(neu & 0xFFu);
        if (!(rp > r0 && r0 > rn)) fail |= TVL_CHK_TRISPACE;        /* S+ warm, S- cool */
        if (!(bp < b0 && b0 < bn)) fail |= TVL_CHK_TRISPACE;
        if ((rp - r0) != (r0 - rn)) fail |= TVL_CHK_TRISPACE;       /* exact mirror     */
        if (((pos >> 8) & 0xFFu) != ((neu >> 8) & 0xFFu)) fail |= TVL_CHK_TRISPACE;
    }

    g_ck_fail = fail;
    return fail == 0u ? 1 : 0;
}
