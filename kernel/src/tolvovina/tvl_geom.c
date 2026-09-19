/* tvl_geom.c — the orientation basis. See tvl_geom.h for what is claimed.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include "tvl_geom.h"
#include "surplus.h"

/* This module reads the RAW BITS of surplus_real_t (integer restoring division,
 * a bit-by-bit square root). Under TEST_HOST surplus.h redefines it to `double`
 * and every SR_ macro becomes native float arithmetic — at which point the
 * shifts below are nonsense. Fail loudly rather than silently: this is exactly
 * the host/target trap that dimensional_ladder.c:76 walked into. */
#ifdef TEST_HOST
#error "tvl_geom.c requires the integer Q32.32 surplus_real_t; do not build it with TEST_HOST"
#endif

/* The arm64 link uses -ffunction-sections -fdata-sections --gc-sections, which
 * DISCARDS any section nothing references (see build_system/verify_banners.sh).
 * Nothing calls this module yet — by design, it is a staged building block for
 * the renderer, not dead code — so the entry points are marked retained. Without
 * this the module compiles, links, and is absent from the ELF. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define TVL_KEEP __attribute__((used, retain))
#else
#  define TVL_KEEP __attribute__((used))
#endif

/* ============================ integer helpers =============================
 * Everything here is written so that no construct lowers to a libgcc call on a
 * 32-bit target: no divide or modulo by a variable, no variable-count 64-bit
 * shift, no __builtin_bswap, no float. Every shift below has a constant count. */

/* Unsigned 64/64 restoring division. `d` must be < 2^63 so that `r << 1` cannot
 * wrap (r is always < d). */
static void tvl_udivmod(uint64_t n, uint64_t d, uint64_t *q_out, uint64_t *r_out)
{
    uint64_t q = 0, r = 0;
    if (d == 0) { *q_out = 0; *r_out = 0; return; }
    for (int i = 0; i < 64; i++) {
        uint64_t top = (n >> 63) & 1u;      /* constant shift count */
        n <<= 1;
        r = (r << 1) | top;
        q <<= 1;
        if (r >= d) { r -= d; q |= 1u; }
    }
    *q_out = q;
    *r_out = r;
}

/* num/den rounded half-away-from-zero. `den` must be > 0. Symmetric in the sign
 * of num, which is what makes the S+/S− depth pair exactly opposite. */
static int64_t tvl_round_div(int64_t num, int64_t den)
{
    uint64_t n, d, q, r;
    int neg = 0;
    if (den <= 0) return 0;
    if (num < 0) { neg = 1; n = (uint64_t)0 - (uint64_t)num; } else { n = (uint64_t)num; }
    d = (uint64_t)den;
    tvl_udivmod(n, d, &q, &r);
    /* 2r >= d, written so it cannot overflow: r < d always. */
    if (r >= d - r) q++;
    return neg ? -(int64_t)q : (int64_t)q;
}

/* Integer square root, bit-by-bit. No division, constant shift counts only. */
static uint64_t tvl_isqrt64(uint64_t x)
{
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= res + bit) { x -= res + bit; res = (res >> 1) + bit; }
        else                { res >>= 1; }
        bit >>= 2;
    }
    return res;
}

/* φ in Q32.32 = round(φ · 2³²). This is the ONE place a decimal-derived
 * constant enters, and it is unavoidable: it is the bridge out of ℤ[φ] into
 * pixel arithmetic. It is not trusted on faith — tvl_geom_selfcheck proves it
 * satisfies its own defining law, φ² = φ + 1, inside Q32.32 to within a
 * rounding unit. Everything else φ-shaped in this file (φ², the camera
 * distance, the focal length) is COMPUTED from it. */
#define TVL_PHI_Q32  ((int64_t)6949403065)

/* Overflow rail on a direction's ℤ[φ] coefficients. Frame directions use
 * |a|,|b| ≤ 2; 1024 leaves room for composed scene geometry while keeping every
 * intermediate below int64. Exceeding it fails closed rather than wrapping. */
#define TVL_COORD_MAX  ((int64_t)1024)

/* ℤ[φ] -> Q32.32, exactly one conversion per coordinate, integer only.
 * a + bφ  ->  a·2³² + b·round(φ·2³²). */
static bool tvl_zphi_q32(zphi_t z, surplus_real_t *out)
{
    if (!z.valid) return false;
    if (z.a > TVL_COORD_MAX || z.a < -TVL_COORD_MAX) return false;
    if (z.b > TVL_COORD_MAX || z.b < -TVL_COORD_MAX) return false;
    *out = SR_ADD(SR_FROM_INT(z.a), (surplus_real_t)(z.b * TVL_PHI_Q32));
    return true;
}

/* ======================= the frame, derived from E8 =======================
 * The 120 icosians classified by real part. Built once, in icosian index order,
 * so the frame is byte-identical every boot — the AI replays runs. */

typedef struct {
    tvl_dir_t d[TVL_DIRS_MAX];
    uint32_t  n;
} tvl_frame_t;

static tvl_frame_t g_frame[TVL_AXIS_CLASSES];
static bool        g_built;

static void tvl_build(void)
{
    if (g_built) return;
    for (uint32_t c = 0; c < (uint32_t)TVL_AXIS_CLASSES; c++) g_frame[c].n = 0;

    for (uint32_t i = 0; i < e8_icosian_count(); i++) {
        icos_t q;
        if (!e8_icosian(i, &q)) continue;

        /* The real part is the whole classifier. cos(θ/2) = h[0]/2, so
         *   h[0] = φ−1 -> θ = 72°  (five-fold)
         *   h[0] = 1   -> θ = 120° (three-fold)
         *   h[0] = 0   -> θ = 180° (two-fold)
         * and the imaginary part is the axis that rotation turns about. The
         * negative-real-part elements are the same axes traversed the other way
         * (the 2I double cover), so they are skipped here rather than counted
         * twice. h[0] = ±φ is the SAME six five-fold axes reached at 1/φ scale
         * — also skipped, so each axis system appears exactly once. */
        uint32_t cls;
        if (zphi_is_zero(q.h[0]))                        cls = (uint32_t)TVL_AXIS_2FOLD;
        else if (zphi_eq(q.h[0], zphi_int(1)))           cls = (uint32_t)TVL_AXIS_3FOLD;
        else if (zphi_eq(q.h[0], zphi_inv_golden()))     cls = (uint32_t)TVL_AXIS_5FOLD;
        else continue;

        if (g_frame[cls].n >= TVL_DIRS_MAX) continue;    /* cannot happen; fail closed */
        g_frame[cls].d[g_frame[cls].n].v[0] = q.h[1];
        g_frame[cls].d[g_frame[cls].n].v[1] = q.h[2];
        g_frame[cls].d[g_frame[cls].n].v[2] = q.h[3];
        g_frame[cls].n++;
    }
    g_built = true;
}

TVL_KEEP uint32_t tvl_frame_size(tvl_axis_class_t cls)
{
    if ((uint32_t)cls >= (uint32_t)TVL_AXIS_CLASSES) return 0;
    tvl_build();
    return g_frame[cls].n;
}

TVL_KEEP uint32_t tvl_axis_count(tvl_axis_class_t cls)
{
    /* Antipodal closure is proved in the selfcheck, so size/2 is a count of
     * axes and not a guess. */
    return tvl_frame_size(cls) / 2u;
}

TVL_KEEP bool tvl_frame_dir(tvl_axis_class_t cls, uint32_t i, tvl_dir_t *out)
{
    if (!out || (uint32_t)cls >= (uint32_t)TVL_AXIS_CLASSES) return false;
    tvl_build();
    if (i >= g_frame[cls].n) return false;
    *out = g_frame[cls].d[i];
    return true;
}

TVL_KEEP tvl_dir_t tvl_dir_make(zphi_t x, zphi_t y, zphi_t z)
{
    tvl_dir_t d;
    d.v[0] = x; d.v[1] = y; d.v[2] = z;
    return d;
}

TVL_KEEP zphi_t tvl_dir_norm2(tvl_dir_t d)
{
    zphi_t s = zphi_zero();
    for (uint32_t k = 0; k < 3; k++) s = zphi_add(s, zphi_mul(d.v[k], d.v[k]));
    return s;
}

TVL_KEEP bool tvl_dir_eq(tvl_dir_t a, tvl_dir_t b)
{
    for (uint32_t k = 0; k < 3; k++) if (!zphi_eq(a.v[k], b.v[k])) return false;
    return true;
}

TVL_KEEP tvl_dir_t tvl_dir_neg(tvl_dir_t d)
{
    tvl_dir_t r;
    for (uint32_t k = 0; k < 3; k++) r.v[k] = zphi_neg(d.v[k]);
    return r;
}

TVL_KEEP tvl_dir_t tvl_dir_even(tvl_dir_t d)
{
    tvl_dir_t r;
    for (uint32_t k = 0; k < 3; k++) r.v[k] = zphi_scale(d.v[k], 2);
    return r;
}

TVL_KEEP bool tvl_frame_index(tvl_dir_t d, tvl_axis_class_t cls, uint32_t *idx_out)
{
    if ((uint32_t)cls >= (uint32_t)TVL_AXIS_CLASSES) return false;
    tvl_build();
    for (uint32_t i = 0; i < g_frame[cls].n; i++) {
        if (tvl_dir_eq(d, g_frame[cls].d[i])) {
            if (idx_out) *idx_out = i;
            return true;
        }
    }
    return false;
}

/* ====================== orientation: exact rotation ======================
 * Hamilton product WITHOUT the ×2 renormalisation and WITHOUT icos_mul's parity
 * rejection. icos_mul halves its result because both operands carry the ×2
 * scale, and returns an all-invalid quaternion when the halving is inexact —
 * correct for ring elements, useless for a general scene point. The rotation
 * q·v·q̄ needs the raw product, so it is written out here. e8.c is untouched. */
static void tvl_qmul_raw(const zphi_t a[4], const zphi_t b[4], zphi_t r[4])
{
    const zphi_t a1 = a[0], b1 = a[1], c1 = a[2], d1 = a[3];
    const zphi_t a2 = b[0], b2 = b[1], c2 = b[2], d2 = b[3];
    r[0] = zphi_sub(zphi_sub(zphi_mul(a1,a2), zphi_mul(b1,b2)),
                    zphi_add(zphi_mul(c1,c2), zphi_mul(d1,d2)));
    r[1] = zphi_add(zphi_add(zphi_mul(a1,b2), zphi_mul(b1,a2)),
                    zphi_sub(zphi_mul(c1,d2), zphi_mul(d1,c2)));
    r[2] = zphi_add(zphi_add(zphi_mul(a1,c2), zphi_mul(c1,a2)),
                    zphi_sub(zphi_mul(d1,b2), zphi_mul(b1,d2)));
    r[3] = zphi_add(zphi_add(zphi_mul(a1,d2), zphi_mul(d1,a2)),
                    zphi_sub(zphi_mul(b1,c2), zphi_mul(c1,b2)));
}

TVL_KEEP uint32_t tvl_rot_count(void) { return e8_icosian_count(); }

TVL_KEEP bool tvl_rot(uint32_t i, icos_t *out) { return e8_icosian(i, out); }

TVL_KEEP icos_t tvl_rot_inverse(icos_t q)
{
    icos_t r;
    r.h[0] = q.h[0];
    for (uint32_t k = 1; k < 4; k++) r.h[k] = zphi_neg(q.h[k]);
    return r;
}

TVL_KEEP icos_t tvl_rot_compose(icos_t a, icos_t b) { return icos_mul(a, b); }

TVL_KEEP bool tvl_orient(icos_t q, tvl_dir_t d, tvl_dir_t *out)
{
    zphi_t p[4], qc[4], t[4], r[4];
    if (!out) return false;

    /* v as a pure quaternion. */
    p[0] = zphi_zero(); p[1] = d.v[0]; p[2] = d.v[1]; p[3] = d.v[2];

    qc[0] = q.h[0];
    for (uint32_t k = 1; k < 4; k++) qc[k] = zphi_neg(q.h[k]);

    tvl_qmul_raw(q.h, p, t);
    tvl_qmul_raw(t, qc, r);

    /* Every icosian has Σh² = 4 exactly (e8.h:78-83), so the raw sandwich
     * product scales the rotation by |q|² = 4. Divide it back out. The divisor
     * is the literal 4, so this is a shift, never a libgcc divide. */
    if (!zphi_is_zero(r[0])) return false;          /* a rotation keeps v pure  */
    for (uint32_t k = 1; k < 4; k++) {
        if (!r[k].valid) return false;
        if ((r[k].a % 4) != 0 || (r[k].b % 4) != 0) return false;   /* fail closed */
        out->v[k - 1] = zphi_make(r[k].a / 4, r[k].b / 4);
        if (!out->v[k - 1].valid) return false;
    }
    return true;
}

/* ============================== projection ===============================
 * One conversion out of ℤ[φ] per direction, then Q32.32 for the divide. The
 * viewport size is a PARAMETER: centre, sphere radius and focal length are all
 * computed from it and there is no resolution table anywhere in this file. */

TVL_KEEP bool tvl_project(tvl_dir_t d, uint32_t vw, uint32_t vh, tvl_screen_t *out)
{
    surplus_real_t X, Y, Z, l2, rho, phi2, D, f, den;
    int64_t r_px, cx, cy, px, py, dz, sx, sy;
    uint32_t minwh;

    if (!out) return false;
    out->x = 0; out->y = 0; out->depth = 0; out->scale = 0; out->valid = false;

    if (vw < TVL_VIEWPORT_MIN || vh < TVL_VIEWPORT_MIN) return false;
    if (vw > TVL_VIEWPORT_MAX || vh > TVL_VIEWPORT_MAX) return false;

    if (!tvl_zphi_q32(d.v[0], &X)) return false;
    if (!tvl_zphi_q32(d.v[1], &Y)) return false;
    if (!tvl_zphi_q32(d.v[2], &Z)) return false;

    /* ρ = |v|, in Q32.32. l2 is already Q32.32, and √(l2·2³²) = √l2 · 2¹⁶, so
     * one integer square root plus a constant 16-bit shift lands back in
     * Q32.32. Deterministic, and the truncation is bounded by 2⁻¹⁶ of a unit —
     * far below a pixel at any viewport this rails allow. */
    l2 = SR_ADD(SR_ADD(SR_MUL(X, X), SR_MUL(Y, Y)), SR_MUL(Z, Z));
    if (l2 <= 0) return false;                       /* zero or overflowed */
    rho = (surplus_real_t)(tvl_isqrt64((uint64_t)l2) << 16);
    if (rho <= 0) return false;

    /* Everything below is derived from the REAL viewport handed in. */
    minwh = (vw < vh) ? vw : vh;
    r_px  = (int64_t)((minwh - 1u) / 2u);            /* inscribed-circle radius */
    if (r_px < 1) return false;
    cx = (int64_t)(vw / 2u);
    cy = (int64_t)(vh / 2u);

    /* φ² = φ + 1 — computed from the one φ constant, not tabled beside it. */
    phi2 = SR_ADD(TVL_PHI_Q32, SR_ONE);
    D    = SR_MUL(rho, phi2);                        /* eye at φ² radii        */
    f    = SR_MUL(SR_FROM_INT(r_px), phi2);          /* equator -> r_px pixels */

    den = SR_ADD(Z, D);                              /* ≥ ρ·φ > 0 by construction */
    if (den <= 0) return false;

    px = tvl_round_div(SR_MUL(X, f), den);
    py = tvl_round_div(SR_MUL(Y, f), den);

    /* Parallax: the z coordinate expressed in the same pixel units as x/y.
     * Positive protrudes (S+), negative recedes (S−), zero is the screen plane
     * (S0) — and because tvl_round_div rounds symmetrically, the S+ and S−
     * members of an antipodal pair get exactly opposite depths. */
    dz = tvl_round_div(SR_MUL(Z, SR_FROM_INT(r_px)), rho);

    sx = cx + px;
    sy = cy - py;                                    /* screen y grows downward */
    if (sx < 0) sx = 0;
    if (sx > (int64_t)vw - 1) sx = (int64_t)vw - 1;
    if (sy < 0) sy = 0;
    if (sy > (int64_t)vh - 1) sy = (int64_t)vh - 1;

    out->x     = (int32_t)sx;
    out->y     = (int32_t)sy;
    out->depth = (int32_t)dz;
    out->scale = (int32_t)r_px;
    out->valid = true;
    return true;
}

TVL_KEEP int tvl_depth_polarity(const tvl_screen_t *s)
{
    if (!s || !s->valid || s->depth == 0) return 0;   /* S0: zero parallax */
    return (s->depth > 0) ? 1 : -1;                   /* S+ protrudes / S− recedes */
}

/* ============================== verification ==============================
 * Each block below proves a property by recomputation. Nothing is compared
 * against a table of expected coordinates except where the table is itself
 * constructed from the textbook definition (the icosahedron characterisation),
 * and even there the comparison is exact ℤ[φ] equality. */

/* Is |z| one of 0, 1, φ ? — the three coordinate magnitudes an icosahedron
 * vertex is allowed to have. */
static bool tvl_is_ico_coord(zphi_t z)
{
    if (zphi_is_zero(z)) return true;
    if (zphi_eq(z, zphi_one())    || zphi_eq(z, zphi_neg(zphi_one())))    return true;
    if (zphi_eq(z, zphi_golden()) || zphi_eq(z, zphi_neg(zphi_golden()))) return true;
    return false;
}

static bool tvl_is_zero_c(zphi_t z)   { return zphi_is_zero(z); }
static bool tvl_is_unit_c(zphi_t z)   { return zphi_eq(z, zphi_one())    || zphi_eq(z, zphi_neg(zphi_one())); }
static bool tvl_is_golden_c(zphi_t z) { return zphi_eq(z, zphi_golden()) || zphi_eq(z, zphi_neg(zphi_golden())); }

/* (x,y,z) -> (z,x,y): the cyclic coordinate rotation the icosahedron's vertex
 * set must be closed under. */
static tvl_dir_t tvl_cycle(tvl_dir_t d)
{
    tvl_dir_t r;
    r.v[0] = d.v[2]; r.v[1] = d.v[0]; r.v[2] = d.v[1];
    return r;
}

TVL_KEEP uint32_t tvl_geom_selfcheck(void)
{
    uint32_t bad = 0;
    uint32_t cls, i, j, k;

    /* 0. The two modules this one stands on must be healthy first, or every
     *    result below is meaningless. */
    if (zphi_selfcheck() != 0) bad++;
    if (e8_selfcheck()   != 0) bad++;

    tvl_build();

    /* 1. Class sizes. 12 + 20 + 30 = 62 directions from 120 icosians. */
    if (tvl_frame_size(TVL_AXIS_5FOLD) != TVL_DIRS_5FOLD) bad++;
    if (tvl_frame_size(TVL_AXIS_3FOLD) != TVL_DIRS_3FOLD) bad++;
    if (tvl_frame_size(TVL_AXIS_2FOLD) != TVL_DIRS_2FOLD) bad++;

    /* 2. 6 + 10 + 15 = 31 axes, and 1 + 6·4 + 10·2 + 15·1 = 60 = |I|. The
     *    second identity is the real proof that the classification is the
     *    icosahedral one and not an arbitrary partition. */
    {
        uint32_t a5 = tvl_axis_count(TVL_AXIS_5FOLD);
        uint32_t a3 = tvl_axis_count(TVL_AXIS_3FOLD);
        uint32_t a2 = tvl_axis_count(TVL_AXIS_2FOLD);
        if (a5 != 6u || a3 != 10u || a2 != 15u) bad++;
        if (a5 + a3 + a2 != 31u) bad++;
        if (1u + a5 * 4u + a3 * 2u + a2 * 1u != 60u) bad++;
    }

    /* 3. Every direction in a class has the IDENTICAL exact norm — compared
     *    against element 0's norm (recomputed, not tabled) and, separately,
     *    against the value the geometry predicts: 2+φ, 3, 4. */
    {
        static const int64_t expect_a[TVL_AXIS_CLASSES] = { 2, 3, 4 };
        static const int64_t expect_b[TVL_AXIS_CLASSES] = { 1, 0, 0 };
        for (cls = 0; cls < (uint32_t)TVL_AXIS_CLASSES; cls++) {
            tvl_dir_t d0;
            zphi_t n0;
            if (!tvl_frame_dir((tvl_axis_class_t)cls, 0, &d0)) { bad++; continue; }
            n0 = tvl_dir_norm2(d0);
            if (!zphi_eq(n0, zphi_make(expect_a[cls], expect_b[cls]))) bad++;
            for (i = 1; i < tvl_frame_size((tvl_axis_class_t)cls); i++) {
                tvl_dir_t di;
                if (!tvl_frame_dir((tvl_axis_class_t)cls, i, &di)) { bad++; continue; }
                if (!zphi_eq(tvl_dir_norm2(di), n0)) bad++;
            }
        }
    }

    /* 4. Distinctness and antipodal closure. Without both, "size/2 axes" is a
     *    fiction. */
    for (cls = 0; cls < (uint32_t)TVL_AXIS_CLASSES; cls++) {
        uint32_t n = tvl_frame_size((tvl_axis_class_t)cls);
        for (i = 0; i < n; i++) {
            tvl_dir_t di, dj;
            if (!tvl_frame_dir((tvl_axis_class_t)cls, i, &di)) { bad++; continue; }
            for (j = i + 1; j < n; j++) {
                if (!tvl_frame_dir((tvl_axis_class_t)cls, j, &dj)) { bad++; continue; }
                if (tvl_dir_eq(di, dj)) bad++;                    /* duplicate */
            }
            if (!tvl_frame_index(tvl_dir_neg(di), (tvl_axis_class_t)cls, 0)) bad++;
        }
    }

    /* 5. The five-fold class IS the icosahedron: 12 vectors, each a signed
     *    permutation of (0, 1, φ) with exactly one of each magnitude, the set
     *    closed under cyclic coordinate rotation and under negation. That
     *    characterisation pins it to one of the two chiral vertex orbits, both
     *    of which are genuine icosahedra. Derived here — never tabled. */
    {
        uint32_t n5 = tvl_frame_size(TVL_AXIS_5FOLD);
        for (i = 0; i < n5; i++) {
            tvl_dir_t d;
            uint32_t z = 0, u = 0, g = 0;
            if (!tvl_frame_dir(TVL_AXIS_5FOLD, i, &d)) { bad++; continue; }
            for (k = 0; k < 3; k++) {
                if (!tvl_is_ico_coord(d.v[k])) bad++;
                if (tvl_is_zero_c(d.v[k]))   z++;
                if (tvl_is_unit_c(d.v[k]))   u++;
                if (tvl_is_golden_c(d.v[k])) g++;
            }
            if (z != 1u || u != 1u || g != 1u) bad++;             /* one of each */
            if (!tvl_frame_index(tvl_cycle(d), TVL_AXIS_5FOLD, 0)) bad++;
        }
    }

    /* 6. The symmetry group acts on the frames: for all 120 group elements and
     *    all 62 directions, the rotation succeeds, preserves the norm EXACTLY
     *    (equality in ℤ[φ], not "close to"), and lands on another direction of
     *    the SAME class. Then round-trip through the inverse and demand the
     *    identical ℤ[φ] pair back — that is the determinism the AI's replay
     *    depends on. */
    {
        uint32_t nrot = tvl_rot_count();
        if (nrot != E8_ICOSIANS) bad++;
        for (i = 0; i < nrot; i++) {
            icos_t q, qi;
            if (!tvl_rot(i, &q)) { bad++; continue; }
            qi = tvl_rot_inverse(q);
            for (cls = 0; cls < (uint32_t)TVL_AXIS_CLASSES; cls++) {
                uint32_t n = tvl_frame_size((tvl_axis_class_t)cls);
                for (j = 0; j < n; j++) {
                    tvl_dir_t d, r, back;
                    if (!tvl_frame_dir((tvl_axis_class_t)cls, j, &d)) { bad++; continue; }
                    if (!tvl_orient(q, d, &r)) { bad++; continue; }
                    if (!zphi_eq(tvl_dir_norm2(r), tvl_dir_norm2(d))) bad++;
                    if (!tvl_frame_index(r, (tvl_axis_class_t)cls, 0)) bad++;
                    if (!tvl_orient(qi, r, &back)) { bad++; continue; }
                    if (!tvl_dir_eq(back, d)) bad++;              /* exact round trip */
                }
            }
        }
    }

    /* 6b. ORBIT-STABILISER. For one representative of each class, count how many
     *     of the 120 group elements fix it and how many distinct images the
     *     orbit has. |orbit| · |stabiliser| = |G| = 120 must hold exactly, and
     *     the orbit must be the WHOLE class — that is what proves the frame is
     *     a single symmetry orbit rather than a bag of vectors that happen to
     *     have the same length. */
    for (cls = 0; cls < (uint32_t)TVL_AXIS_CLASSES; cls++) {
        tvl_dir_t rep, img[TVL_DIRS_MAX];
        uint32_t nimg = 0, nfix = 0, nrot = tvl_rot_count();
        if (!tvl_frame_dir((tvl_axis_class_t)cls, 0, &rep)) { bad++; continue; }
        for (i = 0; i < nrot; i++) {
            icos_t q; tvl_dir_t r; bool dup = false;
            if (!tvl_rot(i, &q)) { bad++; continue; }
            if (!tvl_orient(q, rep, &r)) { bad++; continue; }
            if (tvl_dir_eq(r, rep)) nfix++;
            for (k = 0; k < nimg; k++) if (tvl_dir_eq(img[k], r)) { dup = true; break; }
            if (!dup) { if (nimg < TVL_DIRS_MAX) img[nimg++] = r; else bad++; }
        }
        if (nimg != tvl_frame_size((tvl_axis_class_t)cls)) bad++;   /* transitive */
        if (nfix == 0u || nimg * nfix != nrot) bad++;               /* |orbit|·|stab| */
    }

    /* 6c. An ARBITRARY direction, held at 2× so its coordinates are even, must
     *     rotate exactly under all 120 group elements with the norm preserved.
     *     This is the guarantee tvl_dir_even() advertises, measured rather than
     *     promised. The same direction at 1× is refused by most elements — that
     *     is the fail-closed behaviour, not a defect. */
    {
        tvl_dir_t g    = tvl_dir_make(zphi_int(3), zphi_make(0, 7), zphi_int(-2));
        tvl_dir_t ge   = tvl_dir_even(g);
        zphi_t    n_ge = tvl_dir_norm2(ge);
        uint32_t  nrot = tvl_rot_count(), refused_raw = 0;
        for (i = 0; i < nrot; i++) {
            icos_t q, qi; tvl_dir_t r, back;
            if (!tvl_rot(i, &q)) { bad++; continue; }
            if (!tvl_orient(q, ge, &r)) { bad++; continue; }       /* must never fail */
            if (!zphi_eq(tvl_dir_norm2(r), n_ge)) bad++;
            qi = tvl_rot_inverse(q);
            if (!tvl_orient(qi, r, &back)) { bad++; continue; }
            if (!tvl_dir_eq(back, ge)) bad++;
            if (!tvl_orient(q, g, &r)) refused_raw++;              /* expected: many */
        }
        if (refused_raw == 0u) bad++;   /* the ½·ℤ[φ] rail must actually be there */
    }

    /* 7. The fixed-point φ obeys its own law. φ² = φ + 1 must hold in Q32.32 to
     *    within one rounding unit, otherwise the bridge out of ℤ[φ] is wrong and
     *    every pixel below it is wrong. This proves the constant instead of
     *    trusting the decimal digits. */
    {
        surplus_real_t sq  = SR_MUL(TVL_PHI_Q32, TVL_PHI_Q32);
        surplus_real_t law = SR_ADD(TVL_PHI_Q32, SR_ONE);
        surplus_real_t diff = SR_SUB(sq, law);
        if (diff < 0) diff = -diff;
        if (diff > 2) bad++;                                       /* ≤ 2 ulp */
    }

    /* 8. Projection. Bounds, viewport DEPENDENCE (the anti-hardcoding test),
     *    determinism, the equator landing on the derived radius, and the
     *    S+/S0/S− depth polarity. Viewport sizes are generated by a loop rather
     *    than listed, precisely so no resolution is written down here. */
    {
        uint32_t w, h;
        for (k = 0; k < 6u; k++) {
            w = (uint32_t)(37u + k * 211u) * (k + 1u);            /* deliberately odd */
            h = (uint32_t)(23u + k * 173u) * (k + 2u);
            if (w > TVL_VIEWPORT_MAX) w = TVL_VIEWPORT_MAX;
            if (h > TVL_VIEWPORT_MAX) h = TVL_VIEWPORT_MAX;
            for (cls = 0; cls < (uint32_t)TVL_AXIS_CLASSES; cls++) {
                uint32_t n = tvl_frame_size((tvl_axis_class_t)cls);
                for (j = 0; j < n; j++) {
                    tvl_dir_t d;
                    tvl_screen_t s, s2, sneg;
                    if (!tvl_frame_dir((tvl_axis_class_t)cls, j, &d)) { bad++; continue; }
                    if (!tvl_project(d, w, h, &s)) { bad++; continue; }
                    if (!s.valid) { bad++; continue; }
                    if (s.x < 0 || s.x >= (int32_t)w) bad++;
                    if (s.y < 0 || s.y >= (int32_t)h) bad++;
                    if (s.scale != (int32_t)(((w < h ? w : h) - 1u) / 2u)) bad++;

                    /* determinism: identical inputs, identical bits */
                    if (!tvl_project(d, w, h, &s2)) { bad++; continue; }
                    if (s2.x != s.x || s2.y != s.y || s2.depth != s.depth) bad++;

                    /* S+/S− are exact opposites; S0 is exactly the screen plane */
                    if (!tvl_project(tvl_dir_neg(d), w, h, &sneg)) { bad++; continue; }
                    if (sneg.depth != -s.depth) bad++;
                    if (zphi_is_zero(d.v[2]) && s.depth != 0) bad++;
                    if (tvl_depth_polarity(&s) != (s.depth > 0 ? 1 : (s.depth < 0 ? -1 : 0))) bad++;
                }
            }
        }
    }

    /* 9. Anti-hardcoding, stated as a measurement: double the viewport and the
     *    offset from centre must roughly double. A projection carrying a baked
     *    resolution cannot pass this. The two sizes below are TEST VECTORS
     *    chosen for their exact 2:1 relation — they are inputs to a check, not
     *    a mode table consulted by the projection, which never sees a literal. */
    {
        tvl_dir_t d;
        tvl_screen_t a, b;
        if (!tvl_frame_dir(TVL_AXIS_5FOLD, 0, &d)) bad++;
        else if (!tvl_project(d, 640u, 480u, &a) || !tvl_project(d, 1280u, 960u, &b)) bad++;
        else {
            int64_t oa = (int64_t)a.x - (int64_t)(640u / 2u);
            int64_t ob = (int64_t)b.x - (int64_t)(1280u / 2u);
            if (oa < 0) oa = -oa;
            if (ob < 0) ob = -ob;
            if (oa == 0) { if (ob != 0) bad++; }
            else if (ob < 2 * oa - 4 || ob > 2 * oa + 4) bad++;
            if (b.scale <= a.scale) bad++;
        }
    }

    /* 10. The equator lands on the derived radius. A direction with z = 0 sits
     *     at exactly the inscribed-circle radius of the viewport passed in —
     *     which is the whole claim that the projection is driven by the real
     *     display size. */
    {
        /* Again TEST VECTORS, not a mode table: one landscape (min = height)
         * and one square, so both arms of the min(vw,vh) branch are exercised. */
        uint32_t sizes_w[2], sizes_h[2];
        sizes_w[0] = 800u;  sizes_h[0] = 600u;
        sizes_w[1] = 1024u; sizes_h[1] = 1024u;
        for (k = 0; k < 2u; k++) {
            uint32_t n5 = tvl_frame_size(TVL_AXIS_5FOLD);
            for (j = 0; j < n5; j++) {
                tvl_dir_t d;
                tvl_screen_t s;
                int64_t ox, oy, r2, rr;
                if (!tvl_frame_dir(TVL_AXIS_5FOLD, j, &d)) { bad++; continue; }
                if (!zphi_is_zero(d.v[2])) continue;             /* equator only */
                if (!tvl_project(d, sizes_w[k], sizes_h[k], &s)) { bad++; continue; }
                ox = (int64_t)s.x - (int64_t)(sizes_w[k] / 2u);
                oy = (int64_t)s.y - (int64_t)(sizes_h[k] / 2u);
                r2 = ox * ox + oy * oy;
                rr = (int64_t)s.scale;
                /* |offset| == scale to within a pixel of integer rounding */
                if (r2 < (rr - 2) * (rr - 2) || r2 > (rr + 2) * (rr + 2)) bad++;
            }
        }
    }

    return bad;
}
