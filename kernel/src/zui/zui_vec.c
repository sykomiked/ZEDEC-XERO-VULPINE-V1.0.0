/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_vec.c — the native vector renderer and ZXVG reader. See zui_vec.h. */
#include "zui_vec.h"

#define COORD_MAX  (1 << 24) /* |device coordinate| in 1/256 px           */
#define USER_MAX   (1 << 23) /* |path coordinate| in 1/16 user units      */
#define FIX_MAX    ((1 << 30) - 1)
#define TURN       (1 << 24) /* binary angle: one turn                     */
#define PF_START   1u
#define PF_CLOSED  2u
#define PF_SMOOTH  4u
#define FULL_COVER (ZUI_VEC_SS * 256)

/* ---- integer helpers (V2: every rounding spelled out) ------------------ */
static int64_t sar(int64_t v, int s) /* floor(v / 2^s) */
{
    return v >= 0 ? v >> s : -((-v + ((int64_t) 1 << s) - 1) >> s);
}
static int64_t fdiv(int64_t a, int64_t b) /* floor(a / b), b != 0 */
{
    int64_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}
static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}
static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}
static int64_t iabs64(int64_t v)
{
    return v < 0 ? -v : v;
}
static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, b = (uint64_t) 1 << 62;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}
/* floor(a * 2^sh / b), saturated to +-2^62; 128-bit long division by hand. */
static int64_t mdiv(int64_t a, int sh, int64_t b)
{
    const int64_t LIM = (int64_t) 1 << 62;
    if (b == 0) return a >= 0 ? LIM : -LIM;
    int neg = (a < 0) != (b < 0);
    uint64_t ua = (uint64_t) iabs64(a), ub = (uint64_t) iabs64(b);
    uint64_t hi = sh >= 64 ? ua << (sh - 64) : sh ? ua >> (64 - sh) : 0;
    uint64_t lo = sh >= 64 ? 0 : ua << sh;
    uint64_t r = 0, q = 0;
    int sat = 0;
    for (int i = 127; i >= 0; i--) {
        uint64_t bit = i >= 64 ? (hi >> (i - 64)) & 1u : (lo >> i) & 1u;
        r = (r << 1) | bit;
        if (r >= ub) {
            r -= ub;
            if (i >= 62)
                sat = 1;
            else
                q |= (uint64_t) 1 << i;
        }
    }
    if (sat) return neg ? -LIM : LIM;
    int64_t s = (int64_t) q;
    if (neg) s = r ? -s - 1 : -s;
    return s;
}

/* sine of a binary angle (TURN per turn), Q30; Taylor series to x^9 on a
 * quarter wave (error < 4e-6), integer only. */
static int32_t isin(int64_t a)
{
    uint32_t t = (uint32_t) (a & (TURN - 1));
    uint32_t q = t >> 22, r = t & ((1u << 22) - 1);
    if (q & 1u) r = (1u << 22) - r;
    int64_t x = ((int64_t) r * 1686629713) >> 22; /* radians, Q30 */
    int64_t x2 = (x * x) >> 30, one = (int64_t) 1 << 30;
    int64_t s = one - x2 / 72;
    s = one - ((x2 * s) >> 30) / 42;
    s = one - ((x2 * s) >> 30) / 20;
    s = one - ((x2 * s) >> 30) / 6;
    s = (x * s) >> 30;
    if (s > one) s = one;
    return (int32_t) (q >= 2 ? -s : s);
}
static int32_t icos(int64_t a)
{
    return isin(a + TURN / 4);
}
/* atan2 by CORDIC, binary angle 0..TURN-1. */
static const int32_t ATAN_T[24] = {2097152, 1238021, 654136, 332050, 166669, 83416, 41718, 20860,
                                   10430,   5215,    2608,   1304,   652,    326,   163,   81,
                                   41,      20,      10,     5,      3,      1,     1,     0};
static int32_t iatan2(int64_t y, int64_t x)
{
    if (x == 0 && y == 0) return 0;
    while (iabs64(x) >= ((int64_t) 1 << 29) || iabs64(y) >= ((int64_t) 1 << 29)) {
        x = sar(x, 1);
        y = sar(y, 1);
    }
    int64_t ang = 0;
    if (x < 0) {
        x = -x;
        y = -y;
        ang = TURN / 2;
    }
    x *= 4; /* headroom for precision */
    y *= 4;
    for (int i = 0; i < 24; i++) {
        int64_t nx, ny;
        if (y > 0) {
            nx = x + sar(y, i);
            ny = y - sar(x, i);
            ang += ATAN_T[i];
        } else {
            nx = x - sar(y, i);
            ny = y + sar(x, i);
            ang -= ATAN_T[i];
        }
        x = nx;
        y = ny;
    }
    return (int32_t) (((ang % TURN) + TURN) % TURN);
}

/* ---- transforms -------------------------------------------------------- */
static int32_t fixc(int64_t v)
{
    return (int32_t) clamp64(v, -FIX_MAX, FIX_MAX);
}
zui_vec_xf_t zui_vec_identity(void)
{
    zui_vec_xf_t m = {65536, 0, 0, 65536, 0, 0};
    return m;
}
zui_vec_xf_t zui_vec_mul(zui_vec_xf_t m, zui_vec_xf_t n)
{
    zui_vec_xf_t r;
    r.a = fixc(sar((int64_t) m.a * n.a + (int64_t) m.c * n.b + 32768, 16));
    r.b = fixc(sar((int64_t) m.b * n.a + (int64_t) m.d * n.b + 32768, 16));
    r.c = fixc(sar((int64_t) m.a * n.c + (int64_t) m.c * n.d + 32768, 16));
    r.d = fixc(sar((int64_t) m.b * n.c + (int64_t) m.d * n.d + 32768, 16));
    r.e = fixc(sar((int64_t) m.a * n.e + (int64_t) m.c * n.f + 32768, 16) + m.e);
    r.f = fixc(sar((int64_t) m.b * n.e + (int64_t) m.d * n.f + 32768, 16) + m.f);
    return r;
}
zui_vec_xf_t zui_vec_translate(int32_t tx, int32_t ty)
{
    zui_vec_xf_t m = {65536, 0, 0, 65536, tx, ty};
    return m;
}
zui_vec_xf_t zui_vec_scale(int32_t sx, int32_t sy)
{
    zui_vec_xf_t m = {sx, 0, 0, sy, 0, 0};
    return m;
}
zui_vec_xf_t zui_vec_rotate(uint32_t turn_q16)
{
    int64_t a = (int64_t) (turn_q16 & 0xFFFFu) << 8;
    int32_t c = (int32_t) sar(icos(a), 14), s = (int32_t) sar(isin(a), 14);
    zui_vec_xf_t m = {c, s, -s, c, 0, 0};
    return m;
}
/* user position with `fr` fraction bits -> device 1/256 px, rounded */
static void xf_ptf(const zui_vec_xf_t *m, int64_t x, int64_t y, int fr, int32_t *ox, int32_t *oy)
{
    int s = 8 + fr; /* 16.16 x q(fr) -> q8 */
    int64_t h = (int64_t) 1 << (s - 1);
    int64_t X =
        sar((int64_t) m->a * x + (int64_t) m->c * y + (int64_t) m->e * ((int64_t) 1 << fr) + h, s);
    int64_t Y =
        sar((int64_t) m->b * x + (int64_t) m->d * y + (int64_t) m->f * ((int64_t) 1 << fr) + h, s);
    *ox = (int32_t) clamp64(X, -COORD_MAX, COORD_MAX);
    *oy = (int32_t) clamp64(Y, -COORD_MAX, COORD_MAX);
}
/* user q4 -> device 1/256 px */
static void xf_pt(const zui_vec_xf_t *m, int32_t x, int32_t y, int32_t *ox, int32_t *oy)
{
    xf_ptf(m, x, y, 4, ox, oy);
}
/* sqrt(|det|), 16.16: the transform's mean scale (V5) */
static int64_t xf_scale(const zui_vec_xf_t *m)
{
    int64_t det = iabs64((int64_t) m->a * m->d - (int64_t) m->b * m->c);
    return (int64_t) isqrt64((uint64_t) det);
}

/* ---- setup ------------------------------------------------------------- */
void zui_vec_override_init(zui_vec_override_t *o, const char *group)
{
    uint8_t *p = (uint8_t *) o;
    for (uint32_t i = 0; i < (uint32_t) sizeof *o; i++) p[i] = 0;
    o->group = group;
    o->m[0] = 65536;
    o->m[3] = 65536;
    o->opacity = 256;
}

void zui_vec_init(zui_vec_t *v, zui_surface_t *s, zui_vec_work_t *w)
{
    uint8_t *p = (uint8_t *) v;
    for (uint32_t i = 0; i < (uint32_t) sizeof *v; i++) p[i] = 0;
    v->s = s;
    v->w = w;
    v->xf = zui_vec_identity();
    v->opacity = 256;
    if (s) v->clip = s->clip;
    v->tol = 8;
    if (w) {
        w->np = 0;
        w->ne = 0;
    }
}

zui_vec_paint_t zui_vec_solid(uint32_t argb)
{
    zui_vec_paint_t p;
    uint8_t *b = (uint8_t *) &p;
    for (uint32_t i = 0; i < (uint32_t) sizeof p; i++) b[i] = 0;
    p.kind = ZUI_VEC_SOLID;
    p.argb = argb;
    p.nstop = 1;
    return p;
}

/* ---- path building: flattened into device points (V4) ----------------- */
void zui_vec_begin(zui_vec_t *v)
{
    v->w->np = 0;
    v->open = false;
    v->seg = false;
    v->restart = false;
}

static void push_pt(zui_vec_t *v, int32_t X, int32_t Y, uint8_t f)
{
    zui_vec_work_t *w = v->w;
    if (w->np >= ZUI_VEC_MAX_PTS) {
        v->err |= ZUI_VEC_EFULL;
        w->np = ZUI_VEC_MAX_PTS + 1u; /* poisoned until zui_vec_begin */
        return;
    }
    w->px[w->np] = X;
    w->py[w->np] = Y;
    w->pf[w->np] = f;
    w->np++;
}
static bool poisoned(const zui_vec_t *v)
{
    return v->w->np > ZUI_VEC_MAX_PTS;
}
/* points that draw: a trailing lone move does not */
static uint32_t path_end(const zui_vec_t *v)
{
    return v->open && !v->seg ? v->sub : v->w->np;
}

void zui_vec_move_to(zui_vec_t *v, int32_t x, int32_t y)
{
    if (poisoned(v)) return;
    if (v->open && !v->seg) v->w->np = v->sub; /* a lone move draws nothing */
    int32_t X, Y;
    xf_pt(&v->xf, x, y, &X, &Y);
    v->sub = v->w->np;
    push_pt(v, X, Y, PF_START);
    v->open = true;
    v->seg = false;
    v->restart = false;
    v->cx = v->sx = x;
    v->cy = v->sy = y;
}
static void ensure_open(zui_vec_t *v)
{
    if (!v->open || v->restart) zui_vec_move_to(v, v->cx, v->cy);
}
static void add_dev(zui_vec_t *v, int32_t X, int32_t Y, uint8_t f)
{
    push_pt(v, X, Y, f);
    v->seg = true;
}
void zui_vec_line_to(zui_vec_t *v, int32_t x, int32_t y)
{
    if (poisoned(v)) return;
    ensure_open(v);
    int32_t X, Y;
    xf_pt(&v->xf, x, y, &X, &Y);
    add_dev(v, X, Y, 0);
    v->cx = x;
    v->cy = y;
}

static int32_t steps_for(int64_t dd, int64_t k, int32_t tol)
{
    /* n = ceil(sqrt(k * dd / (4 tol))) for a second-difference magnitude dd */
    if (tol < 1) tol = 1;
    int64_t q = fdiv(k * dd + 4 * (int64_t) tol - 1, 4 * (int64_t) tol);
    int64_t n = (int64_t) isqrt64((uint64_t) q);
    if (n * n < q) n++;
    return (int32_t) clamp64(n, 1, 128);
}

void zui_vec_quad_to(zui_vec_t *v, int32_t cx, int32_t cy, int32_t x, int32_t y)
{
    if (poisoned(v)) return;
    ensure_open(v);
    zui_vec_work_t *w = v->w;
    int64_t x0 = w->px[w->np - 1], y0 = w->py[w->np - 1];
    int32_t X1, Y1, X2, Y2;
    xf_pt(&v->xf, cx, cy, &X1, &Y1);
    xf_pt(&v->xf, x, y, &X2, &Y2);
    int64_t ddx = x0 - 2 * (int64_t) X1 + X2, ddy = y0 - 2 * (int64_t) Y1 + Y2;
    int64_t dd = (int64_t) isqrt64((uint64_t) (ddx * ddx + ddy * ddy));
    int32_t n = steps_for(dd, 1, v->tol);
    int64_t nn = (int64_t) n * n;
    for (int32_t i = 1; i < n; i++) {
        int64_t t = i, u = n - i;
        int64_t px = u * u * x0 + 2 * u * t * X1 + t * t * X2;
        int64_t py = u * u * y0 + 2 * u * t * Y1 + t * t * Y2;
        add_dev(v, (int32_t) fdiv(px + nn / 2, nn), (int32_t) fdiv(py + nn / 2, nn), PF_SMOOTH);
    }
    add_dev(v, X2, Y2, 0);
    v->cx = x;
    v->cy = y;
}

void zui_vec_cubic_to(zui_vec_t *v, int32_t c1x, int32_t c1y, int32_t c2x, int32_t c2y, int32_t x,
                      int32_t y)
{
    if (poisoned(v)) return;
    ensure_open(v);
    zui_vec_work_t *w = v->w;
    int64_t x0 = w->px[w->np - 1], y0 = w->py[w->np - 1];
    int32_t X1, Y1, X2, Y2, X3, Y3;
    xf_pt(&v->xf, c1x, c1y, &X1, &Y1);
    xf_pt(&v->xf, c2x, c2y, &X2, &Y2);
    xf_pt(&v->xf, x, y, &X3, &Y3);
    int64_t ax = x0 - 2 * (int64_t) X1 + X2, ay = y0 - 2 * (int64_t) Y1 + Y2;
    int64_t bx = (int64_t) X1 - 2 * (int64_t) X2 + X3, by = (int64_t) Y1 - 2 * (int64_t) Y2 + Y3;
    int64_t da = (int64_t) isqrt64((uint64_t) (ax * ax + ay * ay));
    int64_t db = (int64_t) isqrt64((uint64_t) (bx * bx + by * by));
    int32_t n = steps_for(da > db ? da : db, 3, v->tol);
    int64_t n3 = (int64_t) n * n * n;
    for (int32_t i = 1; i < n; i++) {
        int64_t t = i, u = n - i;
        int64_t k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
        int64_t px = k0 * x0 + k1 * X1 + k2 * X2 + k3 * X3;
        int64_t py = k0 * y0 + k1 * Y1 + k2 * Y2 + k3 * Y3;
        add_dev(v, (int32_t) fdiv(px + n3 / 2, n3), (int32_t) fdiv(py + n3 / 2, n3), PF_SMOOTH);
    }
    add_dev(v, X3, Y3, 0);
    v->cx = x;
    v->cy = y;
}

/* SVG F.6.5 endpoint -> centre conversion in a radius-normalised space (Q16),
 * then angle stepping. All integer. */
void zui_vec_arc_to(zui_vec_t *v, int32_t rx, int32_t ry, uint32_t rot, bool large, bool sweep,
                    int32_t x, int32_t y)
{
    if (poisoned(v)) return;
    ensure_open(v);
    int64_t x1 = v->cx, y1 = v->cy, x2 = x, y2 = y;
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (x1 == x2 && y1 == y2) return; /* SVG: omitted */
    if (rx == 0 || ry == 0) {
        zui_vec_line_to(v, x, y);
        return;
    }
    int64_t ang = (int64_t) (rot & 0xFFFFu) << 8;
    int64_t cs = icos(ang), sn = isin(ang);
    int64_t dx = x1 - x2, dy = y1 - y2;
    int64_t xp = sar(cs * dx + sn * dy, 31), yp = sar(-sn * dx + cs * dy, 31); /* q4 */
    int64_t R = rx, Ry = ry;
    int64_t X = fdiv(xp * 65536, R), Y = fdiv(yp * 65536, Ry); /* Q16 */
    int64_t Cx = 0, Cy = 0;
    int64_t sxv = X, syv = Y;
    int64_t xs = X, ys = Y;
    int sh = 0;
    while (iabs64(xs) >= ((int64_t) 1 << 30) || iabs64(ys) >= ((int64_t) 1 << 30)) {
        xs = sar(xs, 1);
        ys = sar(ys, 1);
        sh++;
    }
    int64_t d2 = xs * xs + ys * ys;
    if (sh > 0 || d2 > ((int64_t) 1 << 32)) {
        /* radii too small: scale them up, the centre is the midpoint */
        int64_t d = (int64_t) isqrt64((uint64_t) d2) << sh; /* Q16 */
        R = (R * d + 65535) >> 16;
        Ry = (Ry * d + 65535) >> 16;
        if (R < 1) R = 1;
        if (Ry < 1) Ry = 1;
    } else {
        int64_t num = ((int64_t) 1 << 32) - d2;
        int64_t sq = (int64_t) isqrt64((uint64_t) num); /* Q16 */
        int64_t sd = (int64_t) isqrt64((uint64_t) d2);  /* Q16 */
        int64_t coef = sd ? fdiv(sq * 65536, sd) : 0;
        if (large == sweep) coef = -coef;
        Cx = sar(coef * Y, 16);
        Cy = sar(-coef * X, 16);
        sxv = X - Cx;
        syv = Y - Cy;
    }
    int64_t exv = -X - Cx, eyv = -Y - Cy;
    int64_t t1 = iatan2(syv, sxv), t2 = iatan2(eyv, exv);
    int64_t dt = ((t2 - t1) % TURN + TURN) % TURN;
    if (!sweep && dt) dt -= TURN;
    if (sweep && dt == 0) dt = TURN / 2; /* degenerate: endpoints opposite */
    int64_t cxp = sar(Cx * R, 16), cyp = sar(Cy * Ry, 16);
    /* steps: chord error r(1 - cos(th/2)) <= tol  ->  th = sqrt(8 tol / r) */
    int64_t r_dev = sar((R > Ry ? R : Ry) * xf_scale(&v->xf), 12); /* 1/256 px */
    if (r_dev < 1) r_dev = 1;
    if (r_dev > COORD_MAX) r_dev = COORD_MAX;
    int64_t tol = v->tol < 1 ? 1 : v->tol;
    int64_t k = (int64_t) isqrt64((uint64_t) fdiv(r_dev << 32, 8 * tol)); /* Q16 */
    int64_t rad = (iabs64(dt) * 411775) >> 16;                            /* radians, Q24 */
    int64_t n = ((rad * k) >> 40) + 1;
    n = clamp64(n, 1, 512);
    /* points at 1/256 user unit (q4 + 4 bits), rounded, for symmetry */
    int64_t cx8 = sar(cs * cxp - sn * cyp + ((int64_t) 1 << 25), 26) + (x1 + x2) * 8;
    int64_t cy8 = sar(sn * cxp + cs * cyp + ((int64_t) 1 << 25), 26) + (y1 + y2) * 8;
    for (int64_t i = 1; i < n; i++) {
        int64_t th = t1 + fdiv(dt * i, n);
        int64_t ux = sar(R * icos(th) + ((int64_t) 1 << 25), 26);
        int64_t uy = sar(Ry * isin(th) + ((int64_t) 1 << 25), 26);
        int64_t px = sar(cs * ux - sn * uy + ((int64_t) 1 << 29), 30) + cx8;
        int64_t py = sar(sn * ux + cs * uy + ((int64_t) 1 << 29), 30) + cy8;
        int32_t X1, Y1;
        xf_ptf(&v->xf, clamp64(px, -((int64_t) USER_MAX << 5), (int64_t) USER_MAX << 5),
               clamp64(py, -((int64_t) USER_MAX << 5), (int64_t) USER_MAX << 5), 8, &X1, &Y1);
        add_dev(v, X1, Y1, PF_SMOOTH);
    }
    int32_t X2, Y2;
    xf_pt(&v->xf, x, y, &X2, &Y2);
    add_dev(v, X2, Y2, 0);
    v->cx = x;
    v->cy = y;
}

void zui_vec_close(zui_vec_t *v)
{
    if (poisoned(v) || !v->open || v->restart) return;
    v->w->pf[v->sub] |= PF_CLOSED;
    v->seg = true; /* "M x y Z" is a zero-length subpath: a dot with round caps */
    v->cx = v->sx;
    v->cy = v->sy;
    v->restart = true;
}

void zui_vec_rect(zui_vec_t *v, int32_t x, int32_t y, int32_t w, int32_t h)
{
    zui_vec_move_to(v, x, y);
    zui_vec_line_to(v, x + w, y);
    zui_vec_line_to(v, x + w, y + h);
    zui_vec_line_to(v, x, y + h);
    zui_vec_close(v);
}

void zui_vec_circle(zui_vec_t *v, int32_t cx, int32_t cy, int32_t r)
{
    zui_vec_move_to(v, cx + r, cy);
    zui_vec_arc_to(v, r, r, 0, false, true, cx - r, cy);
    zui_vec_arc_to(v, r, r, 0, false, true, cx + r, cy);
    zui_vec_close(v);
}

/* ---- edges ------------------------------------------------------------- */
static void add_edge(zui_vec_t *v, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (y0 == y1) return;
    zui_vec_work_t *w = v->w;
    if (w->ne >= ZUI_VEC_MAX_EDGES) {
        v->err |= ZUI_VEC_EFULL;
        w->ne = ZUI_VEC_MAX_EDGES + 1u;
        return;
    }
    zui_vec_edge_t *e = &w->e[w->ne++];
    if (y0 < y1) {
        e->x0 = x0, e->y0 = y0, e->x1 = x1, e->y1 = y1, e->dir = 1;
    } else {
        e->x0 = x1, e->y0 = y1, e->x1 = x0, e->y1 = y0, e->dir = -1;
    }
}
static bool edges_full(const zui_vec_t *v)
{
    return v->w->ne > ZUI_VEC_MAX_EDGES;
}

/* A closed polygon, wound so its signed area is positive (V5). */
static void add_poly(zui_vec_t *v, const int32_t *xs, const int32_t *ys, int n)
{
    int64_t a = 0;
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        a += (int64_t) xs[i] * ys[j] - (int64_t) xs[j] * ys[i];
    }
    if (a == 0) return;
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        if (a > 0)
            add_edge(v, xs[i], ys[i], xs[j], ys[j]);
        else
            add_edge(v, xs[j], ys[j], xs[i], ys[i]);
    }
}

/* ---- paint ------------------------------------------------------------- */
typedef struct {
    uint8_t kind, spread, n;
    uint32_t argb;
    int32_t off[ZUI_VEC_MAX_STOPS]; /* Q16 */
    uint32_t col[ZUI_VEC_MAX_STOPS];
    int64_t ux, uy, u0, vx, vy, v0; /* gradient coords per 1/256 px, Q32 */
    bool degenerate;
    int32_t gshift;
} paint_t;

static uint32_t mix_rgb(uint32_t a, uint32_t b, uint32_t t) /* t 0..255, as zui_mix */
{
    if (t > 255u) t = 255u;
    uint32_t u = 255u - t;
    uint32_t r = (((a >> 16) & 0xFF) * u + ((b >> 16) & 0xFF) * t + 127u) / 255u;
    uint32_t g = (((a >> 8) & 0xFF) * u + ((b >> 8) & 0xFF) * t + 127u) / 255u;
    uint32_t c = ((a & 0xFF) * u + (b & 0xFF) * t + 127u) / 255u;
    return (r << 16) | (g << 8) | c;
}

static uint32_t tinted(const zui_vec_t *v, uint32_t argb)
{
    uint32_t rgb = argb & 0xFFFFFFu;
    if (v->tint_amt) rgb = mix_rgb(rgb, v->tint, (v->tint_amt * 255u + 128u) >> 8);
    for (uint32_t i = v->depth; i-- > 0;)
        if (v->lv[i].tint_amt)
            rgb = mix_rgb(rgb, v->lv[i].tint, (v->lv[i].tint_amt * 255u + 128u) >> 8);
    return (argb & 0xFF000000u) | rgb;
}

static int setup_paint(const zui_vec_t *v, const zui_vec_paint_t *p, paint_t *o)
{
    o->kind = p->kind;
    o->spread = p->spread;
    o->gshift = v->gshift;
    o->degenerate = false;
    if (p->kind == ZUI_VEC_SOLID) {
        o->argb = tinted(v, p->argb);
        o->n = 1;
        return 0;
    }
    if (p->kind > ZUI_VEC_RADIAL || p->spread > ZUI_VEC_REPEAT || p->nstop < 1 ||
        p->nstop > ZUI_VEC_MAX_STOPS)
        return -1;
    o->n = p->nstop;
    for (uint32_t i = 0; i < p->nstop; i++) {
        o->off[i] = (int32_t) (((uint32_t) p->stop[i].off * 65536u + 127u) / 255u);
        o->col[i] = tinted(v, p->stop[i].argb);
    }
    /* G_dev = xf * g; invert it to map device positions to gradient space */
    zui_vec_xf_t m = zui_vec_mul(v->xf, p->g);
    int64_t det = (int64_t) m.a * m.d - (int64_t) m.b * m.c; /* 32.32 */
    if (det == 0) {
        o->degenerate = true; /* SVG: a zero-size gradient paints its last stop */
        return 0;
    }
    /* u = (d (X - e) - c (Y - f)) / det, v = (-b (X - e) + a (Y - f)) / det,
     * X in px; per 1/256 px the slopes are d/(256 det) etc. Q32 results. */
    const int64_t SL = (int64_t) 1 << 36, OL = (int64_t) 1 << 60;
    o->ux = clamp64(mdiv(m.d, 40, det), -SL, SL);
    o->uy = clamp64(mdiv(-(int64_t) m.c, 40, det), -SL, SL);
    o->u0 = clamp64(mdiv((int64_t) m.c * m.f - (int64_t) m.d * m.e, 32, det), -OL, OL);
    o->vx = clamp64(mdiv(-(int64_t) m.b, 40, det), -SL, SL);
    o->vy = clamp64(mdiv(m.a, 40, det), -SL, SL);
    o->v0 = clamp64(mdiv((int64_t) m.b * m.e - (int64_t) m.a * m.f, 32, det), -OL, OL);
    return 0;
}

static uint32_t lerp_argb(uint32_t a, uint32_t b, int64_t t, int64_t span)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int64_t ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        int64_t c = ca + fdiv((cb - ca) * t + span / 2, span);
        out |= (uint32_t) clamp64(c, 0, 255) << sh;
    }
    return out;
}

static uint32_t paint_at(const paint_t *p, int32_t X, int32_t Y) /* 1/256 px */
{
    if (p->kind == ZUI_VEC_SOLID) return p->argb;
    if (p->degenerate) return p->col[p->n - 1];
    int64_t t;
    int64_t u = sar(p->ux * X + p->uy * Y + p->u0, 16); /* Q16 */
    if (p->kind == ZUI_VEC_LINEAR) {
        t = u;
    } else {
        int64_t w = sar(p->vx * X + p->vy * Y + p->v0, 16);
        u = clamp64(u, -((int64_t) 1 << 23), (int64_t) 1 << 23);
        w = clamp64(w, -((int64_t) 1 << 23), (int64_t) 1 << 23);
        t = (int64_t) isqrt64((uint64_t) (u * u + w * w));
    }
    t += p->gshift;
    t = clamp64(t, -((int64_t) 1 << 40), (int64_t) 1 << 40);
    if (p->spread == ZUI_VEC_REPEAT) {
        t &= 0xFFFF;
    } else if (p->spread == ZUI_VEC_REFLECT) {
        t &= 0x1FFFF;
        if (t > 65536) t = 131072 - t;
    } else {
        t = clamp64(t, 0, 65536);
    }
    if (t <= p->off[0]) return p->col[0];
    for (uint32_t i = 1; i < p->n; i++) {
        if (t <= p->off[i]) {
            int64_t span = p->off[i] - p->off[i - 1];
            if (span <= 0) return p->col[i];
            return lerp_argb(p->col[i - 1], p->col[i], t - p->off[i - 1], span);
        }
    }
    return p->col[p->n - 1];
}

/* ---- the rasteriser (V3) ----------------------------------------------- */
static zui_rect_t isect(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = imax(a.x, b.x), y0 = imax(a.y, b.y);
    int32_t x1 = imin(a.x + a.w, b.x + b.w), y1 = imin(a.y + a.h, b.y + b.h);
    zui_rect_t o = {x0, y0, imax(0, x1 - x0), imax(0, y1 - y0)};
    return o;
}

static zui_rect_t eff_clip(const zui_vec_t *v)
{
    zui_rect_t c = v->clip;
    if (v->s) c = isect(c, v->s->clip);
    if (v->s) c = isect(c, (zui_rect_t){0, 0, v->s->w, v->s->h});
    if (v->lpx) c = isect(c, v->lr);
    return c;
}

static void put_px(zui_vec_t *v, int32_t x, int32_t y, uint32_t argb, uint32_t a)
{
    if (a == 0) return;
    if (v->lpx) {
        uint32_t *p = &v->lpx[(int64_t) (y - v->lr.y) * v->lr.w + (x - v->lr.x)];
        uint32_t d = *p, inv = 255u - a, out = 0;
        for (int sh = 0; sh < 24; sh += 8) {
            uint32_t sc = (((argb >> sh) & 0xFF) * a + 127u) / 255u;
            uint32_t dc = (((d >> sh) & 0xFF) * inv + 127u) / 255u;
            out |= (sc + dc > 255u ? 255u : sc + dc) << sh;
        }
        uint32_t da = ((d >> 24) * inv + 127u) / 255u;
        out |= (a + da > 255u ? 255u : a + da) << 24;
        *p = out;
        return;
    }
    uint32_t *p = &v->s->px[(int64_t) y * v->s->stride + x];
    *p = a >= 255u ? (argb & 0xFFFFFFu) : mix_rgb(*p, argb, a);
}

static void sort_edges(zui_vec_work_t *w)
{
    /* shell sort by (y0, x0): deterministic */
    static const uint32_t gaps[] = {1750, 701, 301, 132, 57, 23, 10, 4, 1};
    for (uint32_t g = 0; g < sizeof gaps / sizeof gaps[0]; g++) {
        uint32_t gap = gaps[g];
        for (uint32_t i = gap; i < w->ne; i++) {
            zui_vec_edge_t t = w->e[i];
            uint32_t j = i;
            while (j >= gap && (w->e[j - gap].y0 > t.y0 ||
                                (w->e[j - gap].y0 == t.y0 && w->e[j - gap].x0 > t.x0))) {
                w->e[j] = w->e[j - gap];
                j -= gap;
            }
            w->e[j] = t;
        }
    }
}

static void span(zui_vec_work_t *w, int32_t xa, int32_t xb, int32_t *lo, int32_t *hi)
{
    if (xa >= xb) return;
    int32_t pa = xa >> 8, pb = xb >> 8;
    if (pa == pb) {
        w->part[pa] += xb - xa;
    } else {
        w->part[pa] += 256 - (xa & 255);
        w->run[pa + 1] += 256;
        w->run[pb] -= 256;
        w->part[pb] += xb & 255;
    }
    if (pa < *lo) *lo = pa;
    if (pb > *hi) *hi = pb;
}

static void raster(zui_vec_t *v, const paint_t *p, int rule)
{
    zui_vec_work_t *w = v->w;
    if (w->ne == 0) return;
    zui_rect_t c = eff_clip(v);
    if (c.w <= 0 || c.h <= 0) return;
    sort_edges(w);
    int32_t ymax = w->e[0].y1;
    for (uint32_t i = 1; i < w->ne; i++) ymax = imax(ymax, w->e[i].y1);
    int32_t r0 = imax(c.y, (int32_t) sar(w->e[0].y0, 8));
    int32_t r1 = imin(c.y + c.h - 1, (int32_t) sar(ymax - 1, 8));
    uint32_t op = v->opacity > 256u ? 256u : v->opacity;
    for (int32_t bx0 = c.x; bx0 < c.x + c.w; bx0 += ZUI_VEC_MAX_W) {
        int32_t bw = imin(ZUI_VEC_MAX_W, c.x + c.w - bx0);
        int32_t L = bx0 * 256, Rr = (bx0 + bw) * 256;
        for (int32_t i = 0; i < bw + 2; i++) w->run[i] = w->part[i] = 0;
        uint32_t ai = 0, nact = 0;
        for (int32_t y = r0; y <= r1; y++) {
            int32_t top = y * 256, bot = top + 256;
            while (ai < w->ne && w->e[ai].y0 < bot) {
                zui_vec_edge_t *e = &w->e[ai];
                if (e->y1 > top) {
                    /* exact DDA on the sample grid 16k + 8: x = x0 + floor((sy - y0) dx / dy),
                     * stepped with quotient and remainder (the same as dividing each time) */
                    int64_t sy0 = 16 * fdiv((int64_t) e->y0 - 8 + 15, 16) + 8;
                    if (sy0 < top + 8) sy0 = top + 8;
                    int64_t dx = (int64_t) e->x1 - e->x0, dy = (int64_t) e->y1 - e->y0;
                    int64_t num = (sy0 - e->y0) * dx, q = fdiv(num, dy);
                    e->sy = (int32_t) sy0;
                    e->cx = (int32_t) clamp64(e->x0 + q, -2 * (int64_t) COORD_MAX,
                                              2 * (int64_t) COORD_MAX);
                    e->rem = (int32_t) (num - q * dy);
                    e->qs = (int32_t) fdiv(16 * dx, dy);
                    e->rs = (int32_t) (16 * dx - (int64_t) e->qs * dy);
                    w->act[nact++] = ai;
                }
                ai++;
            }
            uint32_t k = 0;
            for (uint32_t i = 0; i < nact; i++)
                if (w->e[w->act[i]].y1 > top) w->act[k++] = w->act[i];
            nact = k;
            if (!nact) {
                if (ai >= w->ne) break;
                continue;
            }
            int32_t lo = bw + 1, hi = -1;
            for (int32_t s = 0; s < ZUI_VEC_SS; s++) {
                int32_t sy = top + (s * 256 + 128) / ZUI_VEC_SS;
                uint32_t nx = 0;
                for (uint32_t i = 0; i < nact; i++) {
                    zui_vec_edge_t *e = &w->e[w->act[i]];
                    if (sy < e->sy || sy >= e->y1) continue;
                    int32_t xi = (int32_t) clamp64(e->cx, L, Rr) - L;
                    e->cx += e->qs;
                    e->rem += e->rs;
                    if (e->rem >= e->y1 - e->y0) {
                        e->cx++;
                        e->rem -= e->y1 - e->y0;
                    }
                    e->sy += 16;
                    /* insertion into sorted order */
                    uint32_t j = nx;
                    while (j > 0 && w->xs[j - 1] > xi) {
                        w->xs[j] = w->xs[j - 1];
                        w->ws[j] = w->ws[j - 1];
                        j--;
                    }
                    w->xs[j] = xi;
                    w->ws[j] = (int8_t) e->dir;
                    nx++;
                }
                int32_t wind = 0;
                for (uint32_t i = 0; i + 1 < nx; i++) {
                    wind += w->ws[i];
                    bool in = rule == ZUI_VEC_EVENODD ? (wind & 1) != 0 : wind != 0;
                    if (in) span(w, w->xs[i], w->xs[i + 1], &lo, &hi);
                }
            }
            if (hi < 0) continue;
            int32_t acc = w->run[lo]; /* run[] is zero left of lo */
            for (int32_t x = lo; x <= hi && x < bw; x++) {
                if (x > lo) acc += w->run[x];
                int32_t cov = acc + w->part[x];
                if (cov > 0) {
                    if (cov > FULL_COVER) cov = FULL_COVER;
                    int32_t px = bx0 + x;
                    uint32_t col = paint_at(p, px * 256 + 128, top + 128);
                    /* cov 0..4096 x alpha 0..255 x opacity 0..256 -> 0..255 */
                    uint32_t a = ((uint32_t) cov * (col >> 24) * op + (1u << 19)) >> 20;
                    put_px(v, px, y, col, a);
                }
            }
            for (int32_t i = lo; i <= hi + 1 && i < bw + 2; i++) w->run[i] = w->part[i] = 0;
        }
    }
}

/* Bounding-box pass (zui_zxvg_group): accumulate instead of drawing. */
static void measure_edges(zui_vec_t *v, int32_t grow)
{
    zui_vec_work_t *w = v->w;
    if (!v->mdepth) return;
    for (uint32_t i = 0; i < path_end(v); i++) {
        v->mx0 = imin(v->mx0, w->px[i] - grow);
        v->my0 = imin(v->my0, w->py[i] - grow);
        v->mx1 = imax(v->mx1, w->px[i] + grow);
        v->my1 = imax(v->my1, w->py[i] + grow);
    }
}

int zui_vec_fill(zui_vec_t *v, const zui_vec_paint_t *pp, int rule)
{
    zui_vec_work_t *w = v->w;
    if (poisoned(v)) return -1;
    if (v->measure) {
        measure_edges(v, 0);
        return 0;
    }
    paint_t p;
    if (setup_paint(v, pp, &p) < 0) {
        v->err |= ZUI_VEC_EARG;
        return -1;
    }
    w->ne = 0;
    uint32_t np = path_end(v);
    for (uint32_t i = 0; i < np;) {
        uint32_t j = i + 1;
        while (j < np && !(w->pf[j] & PF_START)) j++;
        for (uint32_t k = i; k + 1 < j; k++)
            add_edge(v, w->px[k], w->py[k], w->px[k + 1], w->py[k + 1]);
        add_edge(v, w->px[j - 1], w->py[j - 1], w->px[i], w->py[i]);
        i = j;
    }
    if (edges_full(v)) {
        w->ne = 0;
        return -1;
    }
    raster(v, &p, rule);
    w->ne = 0;
    return 0;
}

/* ---- strokes (V5) ------------------------------------------------------ */
static void disc(zui_vec_t *v, int32_t cx, int32_t cy, int32_t r)
{
    int32_t xs[256], ys[256];
    int64_t tol = v->tol < 1 ? 1 : v->tol;
    int64_t n = (804 * (int64_t) isqrt64((uint64_t) fdiv((int64_t) r << 16, 2 * tol))) >> 16;
    n = clamp64((n + 3) & ~(int64_t) 3, 8, 256);
    for (int64_t i = 0; i < n; i++) {
        int64_t a = fdiv(i * TURN, n);
        xs[i] = cx + (int32_t) sar((int64_t) r * icos(a) + ((int64_t) 1 << 29), 30);
        ys[i] = cy + (int32_t) sar((int64_t) r * isin(a) + ((int64_t) 1 << 29), 30);
    }
    add_poly(v, xs, ys, (int) n);
}

static void quad(zui_vec_t *v, int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx,
                 int32_t cy, int32_t dx, int32_t dy)
{
    int32_t xs[4] = {ax, bx, cx, dx}, ys[4] = {ay, by, cy, dy};
    add_poly(v, xs, ys, 4);
}

typedef struct {
    int32_t hw;
    uint8_t join, cap;
    int64_t miter_q8;
} pen_t;

/* normal of a->b scaled to hw */
static void normal(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t hw, int32_t *nx,
                   int32_t *ny)
{
    int64_t dx = (int64_t) bx - ax, dy = (int64_t) by - ay;
    int64_t len = (int64_t) isqrt64((uint64_t) (dx * dx + dy * dy));
    if (len == 0) len = 1;
    *nx = (int32_t) fdiv(-dy * hw + len / 2, len);
    *ny = (int32_t) fdiv(dx * hw + len / 2, len);
}

static void join(zui_vec_t *v, const pen_t *pen, int32_t px, int32_t py, int32_t ax, int32_t ay,
                 int32_t bx, int32_t by, bool smooth)
{
    /* segment a->p then p->b */
    int64_t d0x = (int64_t) px - ax, d0y = (int64_t) py - ay;
    int64_t d1x = (int64_t) bx - px, d1y = (int64_t) by - py;
    int64_t cross = d0x * d1y - d0y * d1x, dot = d0x * d1x + d0y * d1y;
    if (cross == 0 && dot >= 0) return; /* straight on */
    if (pen->join == ZUI_VEC_JOIN_ROUND && !smooth) {
        disc(v, px, py, pen->hw);
        return;
    }
    int32_t n0x, n0y, n1x, n1y;
    normal(ax, ay, px, py, pen->hw, &n0x, &n0y);
    normal(px, py, bx, by, pen->hw, &n1x, &n1y);
    if (cross > 0) { /* the outer side is opposite the turn */
        n0x = -n0x, n0y = -n0y, n1x = -n1x, n1y = -n1y;
    }
    int32_t xs[4] = {px, px + n0x, 0, px + n1x}, ys[4] = {py, py + n0y, 0, py + n1y};
    if (pen->join == ZUI_VEC_JOIN_MITER && !smooth && cross != 0) {
        int64_t hw2 = (int64_t) pen->hw * pen->hw;
        int64_t o = (int64_t) n0x * n1x + (int64_t) n0y * n1y;
        /* cos of the turn from the exact directions (the normals are rounded) */
        int64_t l0 = (int64_t) isqrt64((uint64_t) (d0x * d0x + d0y * d0y));
        int64_t l1 = (int64_t) isqrt64((uint64_t) (d1x * d1x + d1y * d1y));
        int64_t c16 = l0 && l1 ? mdiv(dot, 16, l0 * l1) : 0; /* Q16 */
        /* ratio^2 = 2 / (1 + c) <= limit^2 */
        int64_t lim = pen->miter_q8 * pen->miter_q8 * (65536 + c16);
        if (65536 + c16 > 0 && ((int64_t) 2 << 32) <= lim) {
            int64_t k = fdiv(hw2 * 65536, hw2 + o); /* Q16 */
            xs[2] = px + (int32_t) sar((int64_t) (n0x + n1x) * k, 16);
            ys[2] = py + (int32_t) sar((int64_t) (n0y + n1y) * k, 16);
            add_poly(v, xs, ys, 4);
            return;
        }
    }
    int32_t tx[3] = {px, px + n0x, px + n1x}, ty[3] = {py, py + n0y, py + n1y};
    add_poly(v, tx, ty, 3);
}

static void cap(zui_vec_t *v, const pen_t *pen, int32_t px, int32_t py, int32_t qx, int32_t qy)
{
    /* the end p of a segment q->p */
    if (pen->cap == ZUI_VEC_CAP_ROUND) {
        disc(v, px, py, pen->hw);
    } else if (pen->cap == ZUI_VEC_CAP_SQUARE) {
        int32_t nx, ny;
        normal(qx, qy, px, py, pen->hw, &nx, &ny);
        int32_t ex = ny, ey = -nx; /* along the segment, length hw */
        quad(v, px + nx, py + ny, px + nx + ex, py + ny + ey, px - nx + ex, py - ny + ey, px - nx,
             py - ny);
    }
}

/* Stroke the polyline in w->tx/ty/tf[0..n). */
static void stroke_poly(zui_vec_t *v, const pen_t *pen, uint32_t n, bool closed)
{
    zui_vec_work_t *w = v->w;
    int32_t *X = w->tx, *Y = w->ty;
    if (n == 0) return;
    if (n == 1) { /* zero length: a dot for round and square caps */
        if (pen->cap == ZUI_VEC_CAP_ROUND) disc(v, X[0], Y[0], pen->hw);
        if (pen->cap == ZUI_VEC_CAP_SQUARE) {
            int32_t h = pen->hw;
            quad(v, X[0] - h, Y[0] - h, X[0] + h, Y[0] - h, X[0] + h, Y[0] + h, X[0] - h, Y[0] + h);
        }
        return;
    }
    if (closed && X[n - 1] == X[0] && Y[n - 1] == Y[0]) n--;
    if (n < 2) return;
    uint32_t segs = closed ? n : n - 1;
    for (uint32_t i = 0; i < segs && !edges_full(v); i++) {
        uint32_t j = i + 1 == n ? 0 : i + 1;
        int32_t nx, ny;
        normal(X[i], Y[i], X[j], Y[j], pen->hw, &nx, &ny);
        quad(v, X[i] + nx, Y[i] + ny, X[j] + nx, Y[j] + ny, X[j] - nx, Y[j] - ny, X[i] - nx,
             Y[i] - ny);
    }
    for (uint32_t i = closed ? 0 : 1; i < (closed ? n : n - 1) && !edges_full(v); i++) {
        uint32_t a = i == 0 ? n - 1 : i - 1, b = i + 1 == n ? 0 : i + 1;
        join(v, pen, X[i], Y[i], X[a], Y[a], X[b], Y[b], (w->tf[i] & PF_SMOOTH) != 0);
    }
    if (!closed) {
        cap(v, pen, X[0], Y[0], X[1], Y[1]);
        cap(v, pen, X[n - 1], Y[n - 1], X[n - 2], Y[n - 2]);
    }
}

static uint32_t tpush(zui_vec_work_t *w, uint32_t k, int32_t x, int32_t y, uint8_t f)
{
    if (k > 0 && w->tx[k - 1] == x && w->ty[k - 1] == y) return k;
    if (k >= ZUI_VEC_MAX_PTS) return k;
    w->tx[k] = x;
    w->ty[k] = y;
    w->tf[k] = f;
    return k + 1;
}

int zui_vec_stroke(zui_vec_t *v, const zui_vec_paint_t *pp, const zui_vec_stroke_t *st)
{
    zui_vec_work_t *w = v->w;
    if (poisoned(v)) return -1;
    if (!st || st->width <= 0) return 0;
    int64_t sc = xf_scale(&v->xf);
    pen_t pen;
    int64_t hw = sar((int64_t) st->width * sc, 13);
    pen.hw = (int32_t) clamp64(hw, 1, 1 << 22);
    pen.join = st->join;
    pen.cap = st->cap;
    pen.miter_q8 = clamp64(st->miter_q8, 256, 65536);
    if (v->measure) {
        measure_edges(v, pen.hw);
        return 0;
    }
    paint_t p;
    if (setup_paint(v, pp, &p) < 0 || st->ndash > ZUI_VEC_MAX_DASH || (st->ndash & 1u)) {
        v->err |= ZUI_VEC_EARG;
        return -1;
    }
    /* dashes in device units */
    int64_t dl[ZUI_VEC_MAX_DASH], total = 0;
    uint32_t nd = st->ndash;
    for (uint32_t i = 0; i < nd; i++) {
        dl[i] = sar((int64_t) (st->dash[i] < 0 ? 0 : st->dash[i]) * sc, 12);
        total += dl[i];
    }
    if (total < 4) nd = 0; /* too fine to see (or zero): solid, as SVG */
    w->ne = 0;
    uint32_t iters = 0, np = path_end(v);
    for (uint32_t i = 0; i < np && !edges_full(v);) {
        uint32_t j = i + 1;
        while (j < np && !(w->pf[j] & PF_START)) j++;
        bool closed = (w->pf[i] & PF_CLOSED) != 0;
        if (!nd) {
            uint32_t k = 0;
            for (uint32_t q = i; q < j; q++) k = tpush(w, k, w->px[q], w->py[q], w->pf[q]);
            stroke_poly(v, &pen, k, closed);
        } else {
            int64_t ph = sar((int64_t) st->dash_off * sc, 12);
            ph = ((ph % total) + total) % total;
            uint32_t di = 0;
            while (ph > 0 && ph >= dl[di]) {
                ph -= dl[di];
                di = (di + 1) % nd;
            }
            int64_t rem = dl[di] - ph; /* left in the current dash */
            bool on = (di & 1u) == 0;
            uint32_t k = 0;
            if (on) k = tpush(w, 0, w->px[i], w->py[i], 0);
            uint32_t last = closed ? j : j - 1;
            for (uint32_t q = i; q < last && !edges_full(v); q++) {
                uint32_t r = q + 1 == j ? i : q + 1;
                int64_t ax = w->px[q], ay = w->py[q], bx = w->px[r], by = w->py[r];
                int64_t len =
                    (int64_t) isqrt64((uint64_t) ((bx - ax) * (bx - ax) + (by - ay) * (by - ay)));
                int64_t t = 0;
                while (len - t > rem && !edges_full(v)) {
                    if (++iters > (1u << 20)) {
                        v->err |= ZUI_VEC_EFULL;
                        w->ne = ZUI_VEC_MAX_EDGES + 1u;
                        break;
                    }
                    t += rem;
                    int32_t x = (int32_t) (ax + fdiv((bx - ax) * t, len));
                    int32_t y = (int32_t) (ay + fdiv((by - ay) * t, len));
                    if (on) {
                        k = tpush(w, k, x, y, 0);
                        stroke_poly(v, &pen, k, false);
                        k = 0;
                    } else {
                        k = tpush(w, 0, x, y, 0);
                    }
                    on = !on;
                    di = (di + 1) % nd;
                    rem = dl[di];
                }
                rem -= len - t;
                if (on) k = tpush(w, k, (int32_t) bx, (int32_t) by, w->pf[r] & PF_SMOOTH);
            }
            if (on && k) stroke_poly(v, &pen, k, false);
        }
        i = j;
    }
    if (edges_full(v)) {
        w->ne = 0;
        return -1;
    }
    raster(v, &p, ZUI_VEC_NONZERO);
    w->ne = 0;
    return 0;
}

/* ---- groups (V6) ------------------------------------------------------- */
int zui_vec_push(zui_vec_t *v, const zui_vec_xf_t *m, uint32_t opacity)
{
    if (v->depth >= ZUI_VEC_MAX_DEPTH) {
        v->err |= ZUI_VEC_EARG;
        return -1;
    }
    zui_vec_level_t *l = &v->lv[v->depth++];
    l->xf = v->xf;
    l->opacity = v->opacity;
    l->clip = v->clip;
    l->tint = v->tint;
    l->tint_amt = v->tint_amt;
    l->gshift = v->gshift;
    l->layer = v->lpx;
    l->lr = v->lr;
    l->lop = 0;
    l->lused = v->lused;
    v->tint_amt = 0; /* the saved level keeps applying it (see tinted) */
    if (m) v->xf = zui_vec_mul(v->xf, *m);
    if (opacity > 256u) opacity = 256u;
    zui_vec_begin(v);
    if (opacity < 256u && !v->measure && v->layer && v->s) {
        zui_rect_t r = eff_clip(v);
        uint64_t need = (uint64_t) (uint32_t) r.w * (uint32_t) r.h;
        if (r.w > 0 && r.h > 0 && v->lused + need <= v->layer_cap) {
            uint32_t *px = v->layer + v->lused;
            for (uint64_t i = 0; i < need; i++) px[i] = 0;
            v->lused += (uint32_t) need;
            l->lop = (opacity * v->opacity + 128u) >> 8;
            v->lpx = px;
            v->lr = r;
            v->opacity = 256;
            return 0;
        }
    }
    v->opacity = (v->opacity * opacity + 128u) >> 8;
    return 0;
}

void zui_vec_pop(zui_vec_t *v)
{
    if (v->depth == 0) return;
    zui_vec_level_t *l = &v->lv[--v->depth];
    uint32_t *lpx = v->lpx;
    zui_rect_t lr = v->lr;
    bool had_layer = lpx && lpx != l->layer;
    v->xf = l->xf;
    v->opacity = l->opacity;
    v->clip = l->clip;
    v->tint = l->tint;
    v->tint_amt = l->tint_amt;
    v->gshift = l->gshift;
    v->lpx = l->layer;
    v->lr = l->lr;
    v->lused = l->lused;
    zui_vec_begin(v);
    if (!had_layer) return;
    /* composite the premultiplied layer once at the group's opacity */
    uint32_t op = l->lop > 256u ? 256u : l->lop;
    for (int32_t y = 0; y < lr.h; y++)
        for (int32_t x = 0; x < lr.w; x++) {
            uint32_t s = lpx[(int64_t) y * lr.w + x];
            uint32_t sa = ((s >> 24) * op + 128u) >> 8;
            if (!sa) continue;
            int32_t dx = lr.x + x, dy = lr.y + y;
            if (v->lpx) {
                uint32_t *p = &v->lpx[(int64_t) (dy - v->lr.y) * v->lr.w + (dx - v->lr.x)];
                uint32_t d = *p, inv = 255u - sa, out = 0;
                for (int sh = 0; sh < 24; sh += 8) {
                    uint32_t sc = ((((s >> sh) & 0xFF) * op + 128u) >> 8);
                    uint32_t dc = (((d >> sh) & 0xFF) * inv + 127u) / 255u;
                    out |= (sc + dc > 255u ? 255u : sc + dc) << sh;
                }
                uint32_t da = ((d >> 24) * inv + 127u) / 255u;
                *p = out | ((sa + da > 255u ? 255u : sa + da) << 24);
            } else {
                uint32_t *p = &v->s->px[(int64_t) dy * v->s->stride + dx];
                uint32_t d = *p, inv = 255u - sa, out = 0;
                for (int sh = 0; sh < 24; sh += 8) {
                    uint32_t sc = ((((s >> sh) & 0xFF) * op + 128u) >> 8);
                    uint32_t dc = (((d >> sh) & 0xFF) * inv + 127u) / 255u;
                    out |= (sc + dc > 255u ? 255u : sc + dc) << sh;
                }
                *p = out;
            }
        }
}

/* ---- ZXVG reader (V8) -------------------------------------------------- */
#define ZX_MAX_PAL  256u
#define ZX_MAX_GRAD 64u
#define ZX_MAX_IDS  64u

typedef struct {
    const uint8_t *p, *e;
    bool bad;
} rd_t;

static uint32_t rd_u8(rd_t *r)
{
    if (r->bad || r->p >= r->e) {
        r->bad = true;
        return 0;
    }
    return *r->p++;
}
static uint32_t rd_uv(rd_t *r)
{
    uint32_t v = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t b = rd_u8(r);
        if (r->bad) return 0;
        if (i == 4 && (b & 0xF0u)) {
            r->bad = true;
            return 0;
        }
        v |= (b & 0x7Fu) << (7 * i);
        if (!(b & 0x80u)) return v;
    }
    r->bad = true;
    return 0;
}
static int32_t rd_zz(rd_t *r)
{
    uint32_t u = rd_uv(r);
    return (u & 1u) ? -(int32_t) (u >> 1) - 1 : (int32_t) (u >> 1);
}
static int32_t rd_fix(rd_t *r)
{
    int32_t v = rd_zz(r);
    if (v > FIX_MAX || v < -FIX_MAX) r->bad = true;
    return v;
}

typedef struct {
    int32_t vx, vy, vw, vh;
    const uint8_t *pal;
    uint32_t npal, ngrad, nid;
    const uint8_t *grad[ZX_MAX_GRAD];
    const uint8_t *id[ZX_MAX_IDS];
    uint8_t idlen[ZX_MAX_IDS];
    const uint8_t *cmd;
} zx_doc_t;

static bool id_char(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.' || c == ':' || c == '-';
}

static void rd_grad(rd_t *r, uint32_t npal, zui_vec_paint_t *o)
{
    uint32_t kind = rd_u8(r), spread = rd_u8(r);
    if (kind < 1 || kind > 2 || spread > 2) r->bad = true;
    int32_t m[6];
    for (int i = 0; i < 6; i++) m[i] = rd_fix(r);
    uint32_t n = rd_u8(r);
    if (n < 1 || n > ZUI_VEC_MAX_STOPS) r->bad = true;
    uint32_t last = 0;
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        uint32_t off = rd_u8(r), pi = rd_uv(r);
        if (off < last || pi >= npal) r->bad = true;
        last = off;
        if (o) {
            o->stop[i].off = (uint8_t) off;
            o->stop[i].argb = pi; /* resolved by the caller */
        }
    }
    if (o && !r->bad) {
        o->kind = (uint8_t) kind;
        o->spread = (uint8_t) spread;
        o->nstop = (uint8_t) n;
        o->g = (zui_vec_xf_t){m[0], m[1], m[2], m[3], m[4], m[5]};
    }
}

static int zx_head(const uint8_t *d, uint32_t n, zx_doc_t *doc, rd_t *r)
{
    if (!d || n < 6) return -1;
    r->p = d;
    r->e = d + n;
    r->bad = false;
    if (rd_u8(r) != 'Z' || rd_u8(r) != 'X' || rd_u8(r) != 'V' || rd_u8(r) != 'G') return -1;
    if (rd_u8(r) != 1u || rd_u8(r) != 0u) return -1;
    doc->vx = rd_zz(r);
    doc->vy = rd_zz(r);
    uint32_t vw = rd_uv(r), vh = rd_uv(r);
    if (r->bad || vw < 1 || vh < 1 || vw > (1u << 24) || vh > (1u << 24)) return -1;
    if (doc->vx <= -USER_MAX || doc->vx >= USER_MAX || doc->vy <= -USER_MAX || doc->vy >= USER_MAX)
        return -1;
    doc->vw = (int32_t) vw;
    doc->vh = (int32_t) vh;
    doc->npal = rd_uv(r);
    if (r->bad || doc->npal > ZX_MAX_PAL || (uint32_t) (r->e - r->p) < doc->npal * 4u) return -1;
    doc->pal = r->p;
    r->p += doc->npal * 4u;
    doc->ngrad = rd_uv(r);
    if (r->bad || doc->ngrad > ZX_MAX_GRAD) return -1;
    for (uint32_t i = 0; i < doc->ngrad && !r->bad; i++) {
        doc->grad[i] = r->p;
        rd_grad(r, doc->npal, 0);
    }
    doc->nid = rd_uv(r);
    if (r->bad || doc->nid > ZX_MAX_IDS) return -1;
    for (uint32_t i = 0; i < doc->nid && !r->bad; i++) {
        uint32_t len = rd_u8(r);
        if (r->bad || len < 1 || len > 31 || (uint32_t) (r->e - r->p) < len) return -1;
        doc->id[i] = r->p;
        doc->idlen[i] = (uint8_t) len;
        for (uint32_t k = 0; k < len; k++)
            if (!id_char(r->p[k])) return -1;
        for (uint32_t j = 0; j < i; j++) { /* ids are unique */
            if (doc->idlen[j] != len) continue;
            uint32_t k = 0;
            while (k < len && doc->id[j][k] == r->p[k]) k++;
            if (k == len) return -1;
        }
        r->p += len;
    }
    if (r->bad) return -1;
    doc->cmd = r->p;
    return 0;
}

static uint32_t pal_argb(const zx_doc_t *doc, uint32_t i, uint32_t alpha)
{
    const uint8_t *c = doc->pal + 4u * i;
    uint32_t a = ((uint32_t) c[3] * alpha + 127u) / 255u;
    return (a << 24) | ((uint32_t) c[0] << 16) | ((uint32_t) c[1] << 8) | c[2];
}

static void rd_paint(rd_t *r, const zx_doc_t *doc, zui_vec_paint_t *o)
{
    uint32_t ref = rd_uv(r), alpha = rd_u8(r);
    uint32_t idx = ref >> 1;
    if (r->bad) return;
    if (ref & 1u) {
        if (idx >= doc->ngrad) {
            r->bad = true;
            return;
        }
        if (!o) return;
        rd_t g = {doc->grad[idx], r->e, false};
        rd_grad(&g, doc->npal, o);
        if (g.bad) {
            r->bad = true;
            return;
        }
        for (uint32_t i = 0; i < o->nstop; i++)
            o->stop[i].argb = pal_argb(doc, o->stop[i].argb, alpha);
    } else {
        if (idx >= doc->npal) {
            r->bad = true;
            return;
        }
        if (o) *o = zui_vec_solid(pal_argb(doc, idx, alpha));
    }
}

static bool id_eq(const zx_doc_t *doc, uint32_t i, const char *s)
{
    if (!s) return false;
    uint32_t k = 0;
    while (k < doc->idlen[i] && s[k] && (uint8_t) s[k] == doc->id[i][k]) k++;
    return k == doc->idlen[i] && s[k] == 0;
}

static bool coord_ok(int64_t c)
{
    return c > -USER_MAX && c < USER_MAX;
}

/* Run the command stream. v null: validate only. measure_id >= 0: the
 * bounding-box pass for that id index. */
/* V7a: the current path overflowed a cap (and nothing of it is drawn yet):
 * clear that, double the tolerance and say to replay it, up to 8 times. */
static bool retry(zui_vec_t *v, uint32_t perr, uint32_t *tries, bool drawn)
{
    if (!(v->err & ZUI_VEC_EFULL) || (perr & ZUI_VEC_EFULL) || drawn || *tries >= 8) return false;
    (*tries)++;
    v->err &= ~ZUI_VEC_EFULL;
    v->tol = v->tol < 1 ? 2 : v->tol * 2;
    zui_vec_begin(v);
    return true;
}

static int zx_run(const uint8_t *d, uint32_t n, zui_vec_t *v, const zui_vec_override_t *ov,
                  uint32_t nov, int32_t measure_id)
{
    zx_doc_t doc;
    rd_t r;
    if (zx_head(d, n, &doc, &r) < 0) return -1;
    int64_t cx = 0, cy = 0, sx = 0, sy = 0;
    uint32_t depth = 0;
    uint32_t mstart[ZUI_VEC_MAX_DEPTH + 1];
    zui_vec_paint_t paint;
    zui_vec_paint_t *pp = v ? &paint : 0;
    if (v) zui_vec_begin(v);
    /* V7a: where the current path began, so a path too detailed for the caps
     * at this size is replayed coarser instead of vanishing. */
    const uint8_t *path_at = r.p;
    int64_t pcx = 0, pcy = 0, psx = 0, psy = 0;
    uint32_t perr = v ? v->err : 0, tries = 0;
    int32_t tol0 = v ? v->tol : 0;
    bool drawn = false;
    for (;;) {
        uint32_t op = rd_u8(&r);
        if (r.bad) return -1;
        if (op == 0x00) break;
        switch (op) {
        case 0x01:
        case 0x02: {
            int64_t nx = cx + rd_zz(&r), ny = cy + rd_zz(&r);
            if (r.bad || !coord_ok(nx) || !coord_ok(ny)) return -1;
            cx = nx, cy = ny;
            if (op == 0x01) sx = cx, sy = cy;
            if (v && op == 0x01) zui_vec_move_to(v, (int32_t) cx, (int32_t) cy);
            if (v && op == 0x02) zui_vec_line_to(v, (int32_t) cx, (int32_t) cy);
            break;
        }
        case 0x03:
        case 0x04: {
            int64_t c[6];
            int k = op == 0x03 ? 4 : 6;
            for (int i = 0; i < k; i++) {
                c[i] = (i & 1 ? cy : cx) + rd_zz(&r);
                if (!coord_ok(c[i])) return -1;
            }
            if (r.bad) return -1;
            if (v && op == 0x03)
                zui_vec_quad_to(v, (int32_t) c[0], (int32_t) c[1], (int32_t) c[2], (int32_t) c[3]);
            if (v && op == 0x04)
                zui_vec_cubic_to(v, (int32_t) c[0], (int32_t) c[1], (int32_t) c[2], (int32_t) c[3],
                                 (int32_t) c[4], (int32_t) c[5]);
            cx = c[k - 2], cy = c[k - 1];
            break;
        }
        case 0x05: {
            uint32_t rx = rd_uv(&r), ry = rd_uv(&r), rot = rd_uv(&r), fl = rd_u8(&r);
            int64_t nx = cx + rd_zz(&r), ny = cy + rd_zz(&r);
            if (r.bad || rx > (1u << 24) || ry > (1u << 24) || rot > 0xFFFFu || fl > 3u ||
                !coord_ok(nx) || !coord_ok(ny))
                return -1;
            if (v)
                zui_vec_arc_to(v, (int32_t) rx, (int32_t) ry, rot, (fl & 1u) != 0, (fl & 2u) != 0,
                               (int32_t) nx, (int32_t) ny);
            cx = nx, cy = ny;
            break;
        }
        case 0x06:
            if (v) zui_vec_close(v);
            cx = sx, cy = sy;
            break;
        case 0x07:
            if (v) {
                zui_vec_begin(v);
                v->tol = tol0;
                perr = v->err;
            }
            path_at = r.p, pcx = cx, pcy = cy, psx = sx, psy = sy;
            tries = 0, drawn = false;
            break;
        case 0x10: {
            uint32_t rule = rd_u8(&r);
            rd_paint(&r, &doc, pp);
            if (r.bad || rule > 1u) return -1;
            if (v && retry(v, perr, &tries, drawn)) {
                r.p = path_at, cx = pcx, cy = pcy, sx = psx, sy = psy;
                break;
            }
            if (v) zui_vec_fill(v, pp, (int) rule);
            if (v && retry(v, perr, &tries, drawn)) {
                r.p = path_at, cx = pcx, cy = pcy, sx = psx, sy = psy;
                break;
            }
            drawn = true;
            break;
        }
        case 0x11: {
            zui_vec_stroke_t st;
            rd_paint(&r, &doc, pp);
            uint32_t wd = rd_uv(&r), jn = rd_u8(&r), cp = rd_u8(&r), ml = rd_uv(&r);
            uint32_t nd = rd_uv(&r);
            if (r.bad || wd > (1u << 24) || jn > 2u || cp > 2u || ml < 256u || ml > 65536u ||
                nd > ZUI_VEC_MAX_DASH || (nd & 1u))
                return -1;
            st.width = (int32_t) wd;
            st.join = (uint8_t) jn;
            st.cap = (uint8_t) cp;
            st.miter_q8 = (int32_t) ml;
            st.ndash = nd;
            uint32_t sum = 0;
            for (uint32_t i = 0; i < nd; i++) {
                uint32_t dv = rd_uv(&r);
                if (dv > (1u << 24)) return -1;
                st.dash[i] = (int32_t) dv;
                sum += dv;
            }
            st.dash_off = rd_zz(&r);
            if (r.bad || (nd && sum == 0) || st.dash_off > (1 << 24) || st.dash_off < -(1 << 24))
                return -1;
            if (v && retry(v, perr, &tries, drawn)) {
                r.p = path_at, cx = pcx, cy = pcy, sx = psx, sy = psy;
                break;
            }
            if (v) zui_vec_stroke(v, pp, &st);
            if (v && retry(v, perr, &tries, drawn)) {
                r.p = path_at, cx = pcx, cy = pcy, sx = psx, sy = psy;
                break;
            }
            drawn = true;
            break;
        }
        case 0x20: {
            uint32_t id = rd_uv(&r), opa = rd_u8(&r), hm = rd_u8(&r);
            if (r.bad || id > doc.nid || hm > 1u || depth >= ZUI_VEC_MAX_DEPTH) return -1;
            zui_vec_xf_t m = zui_vec_identity();
            if (hm) {
                int32_t e[6];
                for (int i = 0; i < 6; i++) e[i] = rd_fix(&r);
                if (r.bad) return -1;
                m = (zui_vec_xf_t){e[0], e[1], e[2], e[3], e[4], e[5]};
            }
            depth++;
            if (!v) break;
            uint32_t o256 = opa == 255u ? 256u : opa;
            const zui_vec_override_t *o = 0;
            if (id && !v->measure)
                for (uint32_t i = 0; i < nov && ov; i++)
                    if (id_eq(&doc, id - 1u, ov[i].group)) {
                        o = &ov[i];
                        break;
                    }
            if (o) {
                int32_t z = o->m[0] | o->m[1] | o->m[2] | o->m[3] | o->m[4] | o->m[5];
                if (z) {
                    zui_vec_xf_t om = {o->m[0], o->m[1], o->m[2], o->m[3], o->m[4], o->m[5]};
                    int32_t px = (int32_t) clamp64((int64_t) o->px * 4096, -FIX_MAX, FIX_MAX);
                    int32_t py = (int32_t) clamp64((int64_t) o->py * 4096, -FIX_MAX, FIX_MAX);
                    m = zui_vec_mul(m, zui_vec_mul(zui_vec_translate(px, py),
                                                   zui_vec_mul(om, zui_vec_translate(-px, -py))));
                }
                o256 = (o256 * (o->opacity > 256u ? 256u : o->opacity) + 128u) >> 8;
            }
            if (zui_vec_push(v, &m, o256) < 0) return -1;
            if (o) {
                v->tint = o->tint & 0xFFFFFFu;
                v->tint_amt = o->tint_amt > 256u ? 256u : o->tint_amt;
                v->gshift += o->grad_shift;
            }
            mstart[depth] = 0;
            if (v->measure && !v->mdepth && measure_id >= 0 && id == (uint32_t) measure_id + 1u) {
                v->xf = zui_vec_identity(); /* measure in the group's own units */
                v->mdepth = depth;
                mstart[depth] = 1;
            }
            break;
        }
        case 0x21:
            if (depth == 0) return -1;
            if (v) {
                if (v->measure && v->mdepth == depth && mstart[depth]) v->mdepth = 0;
                zui_vec_pop(v);
            }
            depth--;
            break;
        default:
            return -1;
        }
    }
    if (v) v->tol = tol0;
    if (depth != 0 || r.p != r.e) return -1;
    return 0;
}

int zui_zxvg_check(const uint8_t *d, uint32_t n)
{
    return zx_run(d, n, 0, 0, 0, -1);
}

int zui_zxvg_viewbox(const uint8_t *d, uint32_t n, zui_rect_t *vb)
{
    zx_doc_t doc;
    rd_t r;
    if (zx_head(d, n, &doc, &r) < 0) return -1;
    if (vb) *vb = (zui_rect_t){doc.vx, doc.vy, doc.vw, doc.vh};
    return 0;
}

int32_t zui_zxvg_id_count(const uint8_t *d, uint32_t n)
{
    zx_doc_t doc;
    rd_t r;
    if (zx_head(d, n, &doc, &r) < 0) return -1;
    return (int32_t) doc.nid;
}

int zui_zxvg_id(const uint8_t *d, uint32_t n, uint32_t i, char out[32])
{
    zx_doc_t doc;
    rd_t r;
    if (zx_head(d, n, &doc, &r) < 0 || i >= doc.nid) return -1;
    uint32_t k = 0;
    for (; k < doc.idlen[i]; k++) out[k] = (char) doc.id[i][k];
    out[k] = 0;
    return 0;
}

int zui_zxvg_draw(zui_vec_t *v, const uint8_t *d, uint32_t n, const zui_vec_override_t *ov,
                  uint32_t nov)
{
    if (!v || !v->w || zui_zxvg_check(d, n) < 0) return -1; /* V8: whole file first */
    uint32_t depth = v->depth;
    int rc = zx_run(d, n, v, ov, nov, -1);
    while (v->depth > depth) zui_vec_pop(v);
    zui_vec_begin(v);
    return rc;
}

static int fit_draw(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n,
                    zui_rect_t box, const zui_vec_opts_t *o, uint32_t tint, uint32_t tint_amt)
{
    zui_rect_t vb;
    if (!s || !w || zui_zxvg_viewbox(d, n, &vb) < 0) return -1;
    if (box.w <= 0 || box.h <= 0) return 0;
    zui_vec_t v;
    zui_vec_init(&v, s, w);
    v.clip = isect(s->clip, box);
    if (o && o->tol > 0) v.tol = o->tol;
    if (o) {
        v.layer = o->layer;
        v.layer_cap = o->layer_cap;
    }
    /* scale (16.16 px per user unit) = box / viewBox, the smaller axis */
    int64_t sx = fdiv((int64_t) box.w << 20, vb.w), sy = fdiv((int64_t) box.h << 20, vb.h);
    int64_t sc = clamp64(sx < sy ? sx : sy, 1, FIX_MAX);
    int64_t cw = sar(sc * vb.w, 4), ch = sar(sc * vb.h, 4); /* content size, 16.16 px */
    int64_t e = (int64_t) box.x * 65536 + sar(((int64_t) box.w << 16) - cw, 1) - sar(sc * vb.x, 4);
    int64_t f = (int64_t) box.y * 65536 + sar(((int64_t) box.h << 16) - ch, 1) - sar(sc * vb.y, 4);
    v.xf = (zui_vec_xf_t){(int32_t) sc, 0, 0, (int32_t) sc, fixc(e), fixc(f)};
    v.tint = tint & 0xFFFFFFu; /* held through every group (see tinted) */
    v.tint_amt = tint_amt;
    uint32_t op = o && o->opacity ? (o->opacity > 256u ? 256u : o->opacity) : 256u;
    bool grouped = op < 256u;
    if (grouped) zui_vec_push(&v, 0, op);
    int rc = zui_zxvg_draw(&v, d, n, o ? o->ov : 0, o ? o->nov : 0);
    if (grouped) zui_vec_pop(&v);
    return rc;
}

int zui_vec_draw_zxvg(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n,
                      zui_rect_t box, const zui_vec_opts_t *o)
{
    return fit_draw(s, w, d, n, box, o, 0, 0);
}

int zui_vec_draw_simple(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n,
                        zui_rect_t box, uint32_t strength, uint32_t tint_argb)
{
    if (!s || !w || zui_zxvg_check(d, n) < 0) return -1;
    if (strength == 0) return 0;
    zui_vec_opts_t o = {strength > 256u ? 256u : strength, 0, 0, 0, 0, 0};
    return fit_draw(s, w, d, n, box, &o, tint_argb, tint_argb ? 256u : 0u);
}

int zui_zxvg_group(const uint8_t *d, uint32_t n, const char *id, zui_vec_work_t *w,
                   zui_vec_group_info_t *out)
{
    zx_doc_t doc;
    rd_t r;
    if (!w || !out || zx_head(d, n, &doc, &r) < 0 || zui_zxvg_check(d, n) < 0) return -1;
    int32_t idx = -1;
    for (uint32_t i = 0; i < doc.nid; i++)
        if (id_eq(&doc, i, id)) idx = (int32_t) i;
    if (idx < 0) return -1;
    zui_vec_t v;
    zui_vec_init(&v, 0, w);
    v.measure = true;
    v.tol = 16;
    v.mx0 = v.my0 = COORD_MAX;
    v.mx1 = v.my1 = -COORD_MAX;
    if (zx_run(d, n, &v, 0, 0, idx) < 0) return -1;
    if (v.mx0 > v.mx1) return -1;
    /* identity maps 1 user unit to 1 px, i.e. q4 * 16 = 1/256 px */
    out->x0 = (int32_t) sar(v.mx0, 4);
    out->y0 = (int32_t) sar(v.my0, 4);
    out->x1 = (int32_t) -sar(-(int64_t) v.mx1, 4);
    out->y1 = (int32_t) -sar(-(int64_t) v.my1, 4);
    out->px = (int32_t) sar((int64_t) out->x0 + out->x1, 1);
    out->py = (int32_t) sar((int64_t) out->y0 + out->y1, 1);
    out->index = (uint32_t) idx;
    return 0;
}

/* ---- the launch-screen mark -------------------------------------------- */
void zui_vec_mark(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    const zui_vec_mark_t *m = (const zui_vec_mark_t *) ctx;
    if (!s || !m || !m->work) return;
    if (lit > 256u) lit = 256u;
    uint32_t dim = m->dim > 256u ? 256u : m->dim;
    zui_vec_opts_t o;
    o.opacity = dim + (((256u - dim) * lit + 128u) >> 8);
    if (o.opacity == 0) return; /* fully dim: nothing to draw */
    o.ov = m->ov;
    o.nov = m->nov;
    o.layer = m->layer;
    o.layer_cap = m->layer_cap;
    o.tol = 0;
    zui_rect_t clip = s->clip;
    zui_vec_draw_zxvg(s, m->work, m->data, m->len, box, &o);
    s->clip = clip;
}
