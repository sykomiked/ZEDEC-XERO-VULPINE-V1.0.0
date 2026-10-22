/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvs_draw.c — zxvs_draw.h. User space is 4096 x 4096 units (path
 * coordinates in 1/16 unit, so 0..65536) mapped onto the box. */
#include "zxvs_draw.h"

#include <stdbool.h>

#define U      65536 /* user extent in 1/16 units */
#define CHUNK  192   /* filled shapes per path (fits ZUI_VEC_MAX_PTS)  */
#define SCHUNK 32    /* stroked segments per path: strokes outline both sides, joins and caps */

typedef struct {
    const uint8_t *p, *e;
    int bad;
} rr_t;
static uint32_t g8(rr_t *r)
{
    if (r->p >= r->e) {
        r->bad = 1;
        return 0;
    }
    return *r->p++;
}
static uint64_t guv(rr_t *r)
{
    uint64_t v = 0;
    for (int i = 0; i < 10; i++) {
        uint32_t b = g8(r);
        v |= (uint64_t) (b & 0x7F) << (7 * i);
        if (!(b & 0x80) || r->bad) return v;
    }
    r->bad = 1;
    return 0;
}
static int64_t gzz(rr_t *r)
{
    uint64_t u = guv(r);
    return (int64_t) (u >> 1) ^ -(int64_t) (u & 1);
}

static const uint32_t PAL[8] = {0xFF3B82F6u, 0xFFF59E0Bu, 0xFF10B981u, 0xFFEF4444u,
                                0xFF8B5CF6u, 0xFF14B8A6u, 0xFFEC4899u, 0xFF84CC16u};
static const uint32_t CLS[5] = {0xFF1F2937u, 0xFF60A5FAu, 0xFFF87171u, 0xFFFBBF24u, 0x00000000u};

static int32_t clampi(int64_t v)
{
    return v < -(int64_t) 4 * U ? -4 * U : v > (int64_t) 5 * U ? 5 * U : (int32_t) v;
}

/* the value (0..2^24) to y in 1/16 units, inside the band [y0, y0 + h) */
static int32_t ymap(int64_t yn, int32_t y0, int32_t h)
{
    return clampi(y0 + h - (yn * h >> 24));
}

/* stroke one curvebody (body after any prefix) across the band */
static void draw_curve(zui_vec_t *v, rr_t *r, const zui_vec_paint_t *p, const zui_vec_stroke_t *st,
                       int32_t y0, int32_t h)
{
    uint32_t nn = g8(r);
    for (uint32_t i = 0; i < nn; i++) g8(r);
    g8(r);  /* dec */
    gzz(r); /* vmin */
    guv(r); /* vrange */
    uint64_t npts = guv(r);
    guv(r);
    guv(r);
    guv(r);
    uint64_t nseg = guv(r);
    int64_t y = gzz(r);
    if (r->bad || npts < 2 || nseg == 0) return;
    uint64_t span = npts - 1, x0 = 0;
    zui_vec_begin(v);
    uint32_t inpath = 0;
    for (uint64_t s = 0; s < nseg && !r->bad; s++) {
        uint64_t dx = guv(r);
        int64_t d1 = gzz(r), d2 = gzz(r), d3 = gzz(r);
        int32_t xa = (int32_t) ((x0 * 3 * U) / (3 * span)),
                xb = (int32_t) (((x0 * 3 + dx) * U) / (3 * span));
        int32_t xc = (int32_t) (((x0 * 3 + 2 * dx) * U) / (3 * span)),
                xd = (int32_t) (((x0 + dx) * U) / span);
        if (inpath == 0) zui_vec_move_to(v, xa, ymap(y, y0, h));
        zui_vec_cubic_to(v, xb, ymap(y + d1, y0, h), xc, ymap(y + d2, y0, h), xd,
                         ymap(y + d3, y0, h));
        y += d3;
        x0 += dx;
        if (++inpath == SCHUNK) {
            zui_vec_stroke(v, p, st);
            zui_vec_begin(v);
            inpath = 0;
        }
    }
    if (inpath) zui_vec_stroke(v, p, st);
}

static void draw_struct(zui_vec_t *v, const uint8_t *b, uint32_t len, const zui_vec_paint_t *ink,
                        const zui_vec_stroke_t *st)
{
    rr_t r = {b, b + len, 0};
    g8(&r);
    guv(&r);
    uint32_t nk = (uint32_t) guv(&r);
    if (!nk || r.bad) return;
    /* pass 1: extents */
    uint32_t maxx = 1, maxy = 1;
    rr_t q = r;
    for (uint32_t i = 0; i < nk && !q.bad; i++) {
        guv(&q);
        g8(&q);
        guv(&q);
        uint32_t x = (uint32_t) guv(&q), y = (uint32_t) guv(&q);
        uint32_t nl = g8(&q);
        q.p += nl <= (uint32_t) (q.e - q.p) ? nl : 0;
        if (x > maxx) maxx = x;
        if (y > maxy) maxy = y;
    }
    /* positions: x across, depth down; parents found through a small ring of
     * recent nodes would not be exact, so walk twice: edges need the parent's
     * position, which is a node before this one */
    const int32_t M = U / 32; /* margin */
    int32_t W = U - 2 * M, H = U - 2 * M;
    uint32_t done = 0;
    /* edges: for node i, its parent is i - up; re-read positions by index
     * with a cursor (pre-order: a parent is always earlier) */
    zui_vec_begin(v);
    uint32_t inpath = 0;
    enum { STK = 128 };
    typedef struct {
        uint32_t i, x, y;
    } anc_t;
    anc_t stk[STK];
    uint32_t sp = 0;
    rr_t a = r;
    for (uint32_t i = 0; i < nk && !a.bad; i++) {
        uint32_t up = (uint32_t) guv(&a);
        g8(&a);
        guv(&a);
        uint32_t x = (uint32_t) guv(&a), y = (uint32_t) guv(&a);
        uint32_t nl = g8(&a);
        a.p += nl <= (uint32_t) (a.e - a.p) ? nl : 0;
        /* the ancestors of node i form a stack in pre-order: pop to the parent */
        uint32_t par = i - up, px = 0, py = 0;
        while (sp && stk[sp - 1].i > par) sp--;
        bool have = up && sp && stk[sp - 1].i == par;
        if (have) {
            px = stk[sp - 1].x;
            py = stk[sp - 1].y;
        } else if (up) { /* deeper than the stack: find it from the start */
            rr_t z = r;
            for (uint32_t j = 0; j <= par && !z.bad; j++) {
                guv(&z);
                g8(&z);
                guv(&z);
                px = (uint32_t) guv(&z);
                py = (uint32_t) guv(&z);
                uint32_t l2 = g8(&z);
                z.p += l2 <= (uint32_t) (z.e - z.p) ? l2 : 0;
            }
        }
        if (sp < STK) stk[sp++] = (anc_t){i, x, y};
        if (up) {
            zui_vec_move_to(v, M + (int32_t) ((uint64_t) px * W / maxx),
                            M + (int32_t) ((uint64_t) py * H / maxy));
            zui_vec_line_to(v, M + (int32_t) ((uint64_t) x * W / maxx),
                            M + (int32_t) ((uint64_t) y * H / maxy));
            if (++inpath == SCHUNK) {
                zui_vec_stroke(v, ink, st);
                zui_vec_begin(v);
                inpath = 0;
            }
        }
        done++;
    }
    if (inpath) zui_vec_stroke(v, ink, st);
    /* nodes */
    a = r;
    int32_t rad = W / (int32_t) (maxx + 2) / 3;
    if (rad > U / 96) rad = U / 96;
    if (rad < U / 1024) rad = U / 1024;
    for (uint32_t t = 0; t <= ZXVS_N_MAX; t++) {
        zui_vec_paint_t p = zui_vec_solid(PAL[t % 8]);
        zui_vec_begin(v);
        inpath = 0;
        a = r;
        for (uint32_t i = 0; i < nk && !a.bad; i++) {
            guv(&a);
            uint32_t ty = g8(&a);
            guv(&a);
            uint32_t x = (uint32_t) guv(&a), y = (uint32_t) guv(&a);
            uint32_t nl = g8(&a);
            a.p += nl <= (uint32_t) (a.e - a.p) ? nl : 0;
            if (ty != t) continue;
            zui_vec_circle(v, M + (int32_t) ((uint64_t) x * W / maxx),
                           M + (int32_t) ((uint64_t) y * H / maxy), rad);
            if (++inpath == CHUNK / 4) {
                zui_vec_fill(v, &p, ZUI_VEC_NONZERO);
                zui_vec_begin(v);
                inpath = 0;
            }
        }
        if (inpath) zui_vec_fill(v, &p, ZUI_VEC_NONZERO);
    }
    (void) done;
}

static void draw_hilbert(zui_vec_t *v, const uint8_t *b, uint32_t len, const zui_vec_paint_t *ink,
                         const zui_vec_stroke_t *st)
{
    rr_t r = {b, b + len, 0};
    uint32_t k = g8(&r);
    guv(&r);
    uint32_t nrun = (uint32_t) guv(&r);
    if (r.bad || k > 6) return;
    uint32_t S = 1u << k;
    int32_t cell = U / (int32_t) S;
    /* cells, one fill per class */
    const uint8_t *runs = r.p;
    for (uint32_t cl = 0; cl < 4; cl++) {
        rr_t q = {runs, b + len, 0};
        zui_vec_paint_t p = zui_vec_solid(CLS[cl]);
        zui_vec_begin(v);
        uint32_t d = 0, inpath = 0;
        for (uint32_t i = 0; i < nrun && !q.bad; i++) {
            uint32_t c = g8(&q), run = (uint32_t) guv(&q);
            for (uint32_t j = 0; j < run; j++, d++) {
                if (c != cl) continue;
                uint32_t x = 0, y = 0, t = d;
                for (uint32_t s = 1; s < S; s <<= 1) {
                    uint32_t rx = 1u & (t / 2), ry = 1u & (t ^ rx);
                    if (ry == 0) {
                        if (rx == 1) {
                            x = s - 1 - x;
                            y = s - 1 - y;
                        }
                        uint32_t tmp = x;
                        x = y;
                        y = tmp;
                    }
                    x += s * rx;
                    y += s * ry;
                    t /= 4;
                }
                zui_vec_rect(v, (int32_t) x * cell, (int32_t) y * cell, cell, cell);
                if (++inpath == CHUNK) {
                    zui_vec_fill(v, &p, ZUI_VEC_NONZERO);
                    zui_vec_begin(v);
                    inpath = 0;
                }
            }
        }
        if (inpath) zui_vec_fill(v, &p, ZUI_VEC_NONZERO);
        if (cl == 3) r.p = q.p;
    }
    /* outline */
    uint32_t ne = (uint32_t) guv(&r);
    zui_vec_begin(v);
    uint32_t inpath = 0;
    for (uint32_t i = 0; i < ne && !r.bad; i++) {
        uint32_t dir = g8(&r);
        int32_t x = (int32_t) guv(&r), y = (int32_t) guv(&r), L = (int32_t) guv(&r);
        zui_vec_move_to(v, x * cell, y * cell);
        if (dir == 0)
            zui_vec_line_to(v, (x + L) * cell, y * cell);
        else
            zui_vec_line_to(v, x * cell, (y + L) * cell);
        if (++inpath == SCHUNK) {
            zui_vec_stroke(v, ink, st);
            zui_vec_begin(v);
            inpath = 0;
        }
    }
    if (inpath) zui_vec_stroke(v, ink, st);
}

int zxvs_draw(zui_surface_t *s, zui_vec_work_t *w, const uint8_t *d, uint32_t n, zui_rect_t box,
              const zxvs_draw_opts_t *o, uint32_t *err_bits)
{
    zxvs_info_t in;
    if (!s || !w || box.w <= 0 || box.h <= 0 || zxvs_check(d, n, &in) != 0) return -1;
    zui_vec_t v;
    zui_vec_init(&v, s, w);
    v.xf = zui_vec_mul(zui_vec_translate(box.x * 65536, box.y * 65536),
                       zui_vec_scale(box.w * 16, box.h * 16));
    zui_rect_t cl = box;
    v.clip = cl;
    uint32_t inkc = o && o->ink ? o->ink : 0xFF5A6270u;
    uint32_t sq8 = o && o->stroke_q8 ? o->stroke_q8 : 384;
    zui_vec_paint_t ink = zui_vec_solid(inkc);
    zui_vec_stroke_t st;
    for (uint32_t i = 0; i < sizeof st; i++) ((uint8_t *) &st)[i] = 0;
    /* width in 1/16 user units for sq8/256 px: user unit = box / 4096 */
    int64_t avg = ((int64_t) box.w + box.h) / 2;
    st.width = (int32_t) (((int64_t) sq8 * 16 * 4096 / 256) / (avg ? avg : 1));
    if (st.width < 1) st.width = 1;
    st.join = ZUI_VEC_JOIN_ROUND;
    st.cap = ZUI_VEC_CAP_ROUND;
    st.miter_q8 = 1024;
    if (o && o->bg) {
        zui_vec_paint_t bg = zui_vec_solid(o->bg);
        zui_vec_begin(&v);
        zui_vec_rect(&v, 0, 0, U, U);
        zui_vec_fill(&v, &bg, ZUI_VEC_NONZERO);
    }
    const uint8_t *b;
    uint32_t len;
    if (in.kind == ZXVS_K_SERIES) {
        uint32_t nc = in.ncurves ? in.ncurves : 1;
        int32_t band = U / (int32_t) nc;
        for (uint32_t c = 0; c < in.ncurves; c++) {
            if (zxvs_section(d, n, ZXVS_CURVE, c, &b, &len)) continue;
            rr_t r = {b, b + len, 0};
            g8(&r); /* column */
            zui_vec_paint_t p = zui_vec_solid(PAL[c % 8]);
            draw_curve(&v, &r, &p, &st, (int32_t) c * band + band / 16, band - band / 8);
        }
    } else if (in.kind == ZXVS_K_STRUCT) {
        if (!zxvs_section(d, n, ZXVS_STRUCT, 0, &b, &len)) draw_struct(&v, b, len, &ink, &st);
    } else {
        if (!zxvs_section(d, n, ZXVS_HILBERT, 0, &b, &len)) draw_hilbert(&v, b, len, &ink, &st);
        if (!zxvs_section(d, n, ZXVS_ENTROPY, 0, &b, &len)) {
            rr_t r = {b, b + len, 0};
            guv(&r); /* win */
            zui_vec_paint_t p = zui_vec_solid(0xFFFFFFFFu);
            draw_curve(&v, &r, &p, &st, U / 16, U - U / 8);
        }
    }
    if (err_bits) *err_bits = v.err;
    return 0;
}
