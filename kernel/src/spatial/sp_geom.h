/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sp_geom.h — exact integer geometry for the spatial engine (roadmap S.1).
 *
 * WHAT IT DOES
 *   All positions live in a LOCAL Cartesian frame, in whole millimetres
 *   (int64). x = east, y = north, z = up. Every predicate is computed with
 *   exact integer arithmetic, so there is no rounding drift and no
 *   epsilon: a point is on an edge, or it is not.
 *     - orientation of three points (sign of a cross product);
 *     - segment intersection (none / proper crossing / touching);
 *     - point in polygon with a separate ON-EDGE answer;
 *     - "is the squared distance from a point (or segment) to a segment
 *       below, equal to or above margin^2?" compared exactly as
 *       cross^2 <= margin^2 * len^2 in 128-bit unsigned arithmetic;
 *     - polygon validation (simple polygon, no zero-length edge);
 *     - altitude bands;
 *     - exact planar rotations by Pythagorean headings (a,b,c with
 *       a^2+b^2=c^2), returned as homogeneous (projective) coordinates
 *       X/W, Y/W so a rotated point is never rounded;
 *     - small helpers: 64x64->128 multiply, checked int64 arithmetic,
 *       64-bit long division and integer square root (no libgcc calls,
 *       so it links freestanding on i386 as well as on 64-bit hosts), and
 *       a fixed 512-bit signed integer (sp_big) used by the deconfliction
 *       code in sp_fleet.c.
 *   Geodetic positions (micro-degrees, mm altitude) can be stored and
 *   validated. Projection to the local frame uses scale factors the CALLER
 *   supplies (mm per micro-degree of latitude and of longitude, as exact
 *   fractions). The caller computes them at the edge, for example from
 *   cos(latitude of the origin) in floating point.
 *
 * LIMITS ON INPUT (checked, refused with SP_ERR_RANGE)
 *   |coordinate| <= SP_COORD_MAX = 2^29 mm (about 536.9 km from the frame
 *   origin). Then any coordinate difference is <= 2^30, any product of two
 *   differences is <= 2^60 and every sum of three such products fits in
 *   int64; squares of those fit in 128 bits. Intermediate overflow is
 *   still checked everywhere and reported as SP_ERR_OVERFLOW.
 *
 * HONEST LIMITS
 *   Exact arithmetic removes ARITHMETIC drift only. Sensor drift (IMU,
 *   odometry, GPS) is untouched and still needs normal filtering and
 *   ground truth. The local frame is a flat approximation: projecting
 *   latitude/longitude needs cos(latitude), which is irrational, so the
 *   projection is NOT exact and is done by the caller; over hundreds of
 *   kilometres the flat-frame error is large and a proper geodesy layer
 *   (Eve pillar, roadmap S.6, not built) is needed. A millimetre grid
 *   cannot represent sub-millimetre positions. Polygons are 2D; zones are
 *   treated as vertical columns over all altitudes. Nothing here moves a
 *   vehicle: this is a library of predicates for advisory planning.
 */
#ifndef ZXV_SP_GEOM_H
#define ZXV_SP_GEOM_H

#include <stdint.h>

/* ---------- status codes shared by every sp_ module ---------- */
typedef enum {
    SP_OK = 0,
    SP_ERR_NULL = -1,       /* a required pointer was NULL */
    SP_ERR_RANGE = -2,      /* an input is outside its documented range */
    SP_ERR_OVERFLOW = -3,   /* an intermediate result would not fit */
    SP_ERR_CAPACITY = -4,   /* more items than the fixed capacity */
    SP_ERR_DEGENERATE = -5, /* polygon/segment/path shape is invalid */
    SP_ERR_ORDER = -6,      /* times not strictly increasing */
    SP_ERR_PURPOSE = -7,    /* mission purpose not in the allowed list */
    SP_ERR_PARAM = -8,      /* inconsistent parameters */
} sp_status_t;

#define SP_COORD_MAX ((int64_t) 1 << 29) /* mm; |x|,|y|,|z| <= this */
#define SP_POLY_MAX  32                  /* vertices per polygon */

/* ---------- 128-bit unsigned helper (two uint64 halves) ---------- */
typedef struct {
    uint64_t hi;
    uint64_t lo;
} sp_u128;

void sp_u128_mul64(uint64_t a, uint64_t b, sp_u128 *r);
/* r = a + b; returns SP_ERR_OVERFLOW on carry out of bit 127. */
int sp_u128_add(const sp_u128 *a, const sp_u128 *b, sp_u128 *r);
int sp_u128_cmp(const sp_u128 *a, const sp_u128 *b); /* -1, 0, +1 */

/* ---------- checked int64 arithmetic (no libgcc helpers) ---------- */
int sp_add_i64(int64_t a, int64_t b, int64_t *r);
int sp_sub_i64(int64_t a, int64_t b, int64_t *r);
int sp_mul_i64(int64_t a, int64_t b, int64_t *r);
/* Unsigned 64-bit long division by shift/subtract; d == 0 -> SP_ERR_PARAM. */
int sp_udiv64(uint64_t n, uint64_t d, uint64_t *q, uint64_t *rem);
/* ceil(n / d) for n >= 0, d > 0. */
int sp_ceil_div_i64(int64_t n, int64_t d, int64_t *q);
/* floor(n / d) for any n and d > 0. */
int sp_floor_div_i64(int64_t n, int64_t d, int64_t *q);
uint64_t sp_isqrt_floor(uint64_t v);
uint64_t sp_isqrt_ceil(uint64_t v);

/* ---------- 512-bit signed integer (sign + magnitude) ---------- */
#define SP_BIG_LIMBS 16
typedef struct {
    uint32_t l[SP_BIG_LIMBS]; /* little-endian 32-bit limbs */
    int neg;                  /* 1 if negative; zero is never negative */
} sp_big;

void sp_big_from_i64(sp_big *r, int64_t v);
void sp_big_copy(sp_big *r, const sp_big *a);
int sp_big_add(sp_big *r, const sp_big *a, const sp_big *b); /* r may alias */
int sp_big_sub(sp_big *r, const sp_big *a, const sp_big *b);
int sp_big_mul(sp_big *r, const sp_big *a, const sp_big *b);
int sp_big_cmp(const sp_big *a, const sp_big *b); /* -1, 0, +1 */
int sp_big_sign(const sp_big *a);                 /* -1, 0, +1 */

/* ---------- vectors ---------- */
typedef struct {
    int64_t x, y;
} sp_vec2;

typedef struct {
    int64_t x, y, z;
} sp_vec3;

int sp_vec2_check(const sp_vec2 *v); /* SP_OK or SP_ERR_RANGE */
int sp_vec3_check(const sp_vec3 *v);

/* Orientation of (a,b,c): +1 counter-clockwise, -1 clockwise, 0 collinear. */
int sp_orient2d(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, int *out);

typedef enum {
    SP_ISECT_NONE = 0,   /* closed segments share no point */
    SP_ISECT_PROPER = 1, /* they cross at one interior point of both */
    SP_ISECT_TOUCH = 2,  /* they share a point but do not properly cross
                            (endpoint contact or collinear overlap) */
} sp_isect_t;

int sp_seg_intersect(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, const sp_vec2 *d,
                     sp_isect_t *out);

/* *out = sign(dist(p, segment ab)^2 - m^2): -1 closer than m, 0 exactly m,
 * +1 farther. a == b is allowed (distance to a point). 0 <= m <= 2^30. */
int sp_dist_cmp_pt_seg3(const sp_vec3 *p, const sp_vec3 *a, const sp_vec3 *b, int64_t m, int *out);
int sp_dist_cmp_pt_seg2(const sp_vec2 *p, const sp_vec2 *a, const sp_vec2 *b, int64_t m, int *out);
/* *out = sign(dist(segment ab, segment cd)^2 - m^2), planar. */
int sp_dist_cmp_seg_seg2(const sp_vec2 *a, const sp_vec2 *b, const sp_vec2 *c, const sp_vec2 *d,
                         int64_t m, int *out);

/* Exact 3D length, rounded UP to a whole mm (conservative for budgets). */
int sp_len3_ceil(const sp_vec3 *a, const sp_vec3 *b, int64_t *out);

/* ---------- polygons ---------- */
typedef struct {
    uint32_t n;             /* number of vertices, 3..SP_POLY_MAX */
    sp_vec2 v[SP_POLY_MAX]; /* in order (either winding); closed implicitly */
} sp_poly;

typedef enum {
    SP_LOC_OUT = 0,
    SP_LOC_IN = 1,
    SP_LOC_ON = 2, /* exactly on an edge or vertex: both-ish; callers that
                      check safety treat it the cautious way */
} sp_loc_t;

/* Valid = 3..SP_POLY_MAX vertices in range, no zero-length edge, no two
 * non-adjacent edges touching, adjacent edges not folding back, and
 * non-zero area. Returns SP_ERR_DEGENERATE otherwise. */
int sp_poly_validate(const sp_poly *pg);
/* Assumes a polygon that passed sp_poly_validate (re-checks vertex range). */
int sp_point_in_poly(const sp_poly *pg, const sp_vec2 *p, sp_loc_t *out);

/* ---------- altitude bands ---------- */
typedef struct {
    int64_t z_min, z_max; /* mm, inclusive */
} sp_alt_band;

int sp_alt_band_validate(const sp_alt_band *b);
/* *out = 1 if z_min <= z <= z_max. */
int sp_alt_band_contains(const sp_alt_band *b, int64_t z, int *out);

/* ---------- geodetic input (stored exactly, projected by the caller) ---------- */
typedef struct {
    int64_t lat_udeg; /* micro-degrees, [-90e6, 90e6] */
    int64_t lon_udeg; /* micro-degrees, [-180e6, 180e6] */
    int64_t alt_mm;   /* mm above the frame's reference */
} sp_geo;

/* A local frame: origin plus CALLER-SUPPLIED scales (mm per micro-degree,
 * as fractions num/den). The caller computes lon scale ~ cos(lat0) at the
 * edge; it is not exact and this module does not pretend it is. */
typedef struct {
    sp_geo origin;
    int64_t lat_mm_num, lat_mm_den;
    int64_t lon_mm_num, lon_mm_den;
} sp_frame;

int sp_geo_validate(const sp_geo *g);
/* x = floor(dlon * lon_num / lon_den), y likewise, z = alt - alt0.
 * dlon wraps into (-180e6, 180e6]. Result must be within SP_COORD_MAX. */
int sp_geo_to_local(const sp_frame *f, const sp_geo *g, sp_vec3 *out);

/* ---------- exact rotations (Pythagorean headings) ---------- */
typedef struct {
    int64_t a, b, c; /* cos = a/c, sin = b/c, a^2 + b^2 = c^2, c > 0 */
} sp_heading;

/* Homogeneous planar point: the real point is (X/W, Y/W). */
typedef struct {
    int64_t X, Y, W;
} sp_hvec2;

#define SP_HEADING_MAX ((int64_t) 1 << 20) /* |a|,|b|,c <= this */
int sp_heading_validate(const sp_heading *h);
/* Pose = rotate by h, then translate by t: returns exact homogeneous
 * point (a x - b y + c tx, b x + a y + c ty, c). */
int sp_pose2_apply(const sp_heading *h, const sp_vec2 *t, const sp_vec2 *p, sp_hvec2 *out);

#endif /* ZXV_SP_GEOM_H */
