/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_vtrace.c — raster to ZXVG. See zui_vtrace.h. */
#include "zui_vtrace.h"

#include "sha256.h"
#include "zui_vec.h"

/* ---- the shared per-pixel kernels, compiled as C (zvt_kern.h) ---------- */
typedef uint32_t zu;
typedef int32_t zi;
typedef uint8_t zb;
#define ZK_DEV
#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_kern.h"
#undef ZVT_KSRC

#define OUT       0xFFFFFFFFu /* the label of transparent pixels and the outside */
#define NONE      0xFFFFFFFFu
#define TRANSP    255u
#define GRAD_MIN  256u  /* smallest region considered for a gradient          */
#define SPAN_MAX  2048  /* points per least-squares fit (keeps sums in range) */
#define CAP_FIT   6000u /* path budget: lines count 1, curves 2 (zui_vec V7)  */
#define CAP_LINE  8000u /* pixel-exact paths: lines only                       */
#define MAX_REFIT 5u

enum { E_ = 0, S_ = 1, W_ = 2, N_ = 3 }; /* headings, y down */
static const int32_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};

/* ---- integer helpers ---------------------------------------------------- */
static int64_t iabs64(int64_t v)
{
    return v < 0 ? -v : v;
}
static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
static int64_t fdiv(int64_t a, int64_t b) /* floor(a / b), b != 0 */
{
    int64_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}
static int64_t rdiv(int64_t a, int64_t b) /* round(a / b), b > 0 */
{
    return fdiv(2 * a + b, 2 * b);
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
/* floor(a * b / c) with a 128-bit intermediate, c != 0; saturates at 2^62. */
static int64_t muldiv(int64_t a, int64_t b, int64_t c)
{
    const int64_t LIM = (int64_t) 1 << 62;
    int neg = (a < 0) != (b < 0);
    if (c < 0) neg = !neg;
    uint64_t ua = (uint64_t) iabs64(a), ub = (uint64_t) iabs64(b), uc = (uint64_t) iabs64(c);
    uint64_t a0 = ua & 0xFFFFFFFFu, a1 = ua >> 32, b0 = ub & 0xFFFFFFFFu, b1 = ub >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    uint64_t lo = (p00 & 0xFFFFFFFFu) | (mid << 32);
    uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    uint64_t r = 0, q = 0;
    int sat = 0;
    for (int i = 127; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = (r << 1) | (i >= 64 ? (hi >> (i - 64)) & 1u : (lo >> i) & 1u);
        if (top || r >= uc) {
            r -= uc;
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
/* log2(v) in Q16, v >= 1 */
static int64_t log2_q16(uint64_t v)
{
    int k = 63;
    while (!(v >> k)) k--;
    uint64_t m = k >= 31 ? v >> (k - 31) : v << (31 - k); /* Q31, [1, 2) */
    int64_t f = 0;
    for (int i = 1; i <= 16; i++) {
        m = (m * m) >> 31;
        if (m >= ((uint64_t) 1 << 32)) {
            m >>= 1;
            f |= (int64_t) 1 << (16 - i);
        }
    }
    return (int64_t) k * 65536 + f;
}
static void mzero(void *p, uint64_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
}
static void mcopy(void *d, const void *s, uint64_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (uint64_t i = 0; i < n; i++) a[i] = b[i];
}
static uint32_t cdist(uint32_t a, uint32_t b) /* zk_quant_px's metric */
{
    uint32_t dr = zk_absd((a >> 16) & 255u, (b >> 16) & 255u);
    uint32_t dg = zk_absd((a >> 8) & 255u, (b >> 8) & 255u);
    uint32_t db = zk_absd(a & 255u, b & 255u);
    return 2u * dr * dr + 4u * dg * dg + 3u * db * db;
}

static uint64_t rgb_d2(uint32_t a, uint32_t b) /* squared RGB distance */
{
    int64_t dr = (int64_t) ((a >> 16) & 255u) - ((b >> 16) & 255u);
    int64_t dg = (int64_t) ((a >> 8) & 255u) - ((b >> 8) & 255u);
    int64_t db = (int64_t) (a & 255u) - (b & 255u);
    return (uint64_t) (dr * dr + dg * dg + db * db);
}

/* ---- the CPU backend (the reference) ------------------------------------ */
void zvt_cpu_smooth(const uint32_t *in, uint32_t *out, uint32_t w, uint32_t h, uint32_t thr)
{
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) out[y * w + x] = zk_smooth_px(in, w, h, x, y, thr);
}
void zvt_cpu_quant(const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t npal, uint8_t *idx)
{
    for (uint32_t i = 0; i < n; i++) idx[i] = (uint8_t) zk_quant_px(px[i], pal, npal);
}
void zvt_cpu_edges(const uint8_t *idx, uint32_t w, uint32_t h, uint8_t *edge)
{
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = y * w + x;
            uint32_t r = x + 1 < w ? idx[i + 1] : 0u, d = y + 1 < h ? idx[i + w] : 0u;
            edge[i] = (uint8_t) zk_edge_px(idx[i], r, d, x + 1 < w, y + 1 < h);
        }
}
void zvt_cpu_metric(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h, uint32_t wx,
                    uint32_t wy, uint32_t step, uint32_t *row_sse, uint32_t *win)
{
    for (uint32_t y = 0; y < h; y++) row_sse[y] = zk_sse_row(a, b, w, y);
    uint32_t nwx = (w - wx) / step + 1, nwy = (h - wy) / step + 1;
    for (uint32_t j = 0; j < nwy; j++)
        for (uint32_t i = 0; i < nwx; i++)
            zk_win(a, b, w, i * step, j * step, wx, wy, win + 5u * (j * nwx + i));
}
void zvt_cpu_diff(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h, uint32_t thr,
                  uint8_t *mask, uint32_t *row_count)
{
    for (uint32_t y = 0; y < h; y++) row_count[y] = zk_diff_row(a, b, w, y, thr, mask);
}

static int cpu_smooth(void *c, const uint32_t *in, uint32_t *out, uint32_t w, uint32_t h,
                      uint32_t thr)
{
    (void) c;
    zvt_cpu_smooth(in, out, w, h, thr);
    return 0;
}
static int cpu_quant(void *c, const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t npal,
                     uint8_t *idx)
{
    (void) c;
    zvt_cpu_quant(px, n, pal, npal, idx);
    return 0;
}
static int cpu_edges(void *c, const uint8_t *idx, uint32_t w, uint32_t h, uint8_t *edge)
{
    (void) c;
    zvt_cpu_edges(idx, w, h, edge);
    return 0;
}
static int cpu_metric(void *c, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                      uint32_t wx, uint32_t wy, uint32_t step, uint32_t *row_sse, uint32_t *win)
{
    (void) c;
    zvt_cpu_metric(a, b, w, h, wx, wy, step, row_sse, win);
    return 0;
}
static int cpu_diff(void *c, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                    uint32_t thr, uint8_t *mask, uint32_t *row_count)
{
    (void) c;
    zvt_cpu_diff(a, b, w, h, thr, mask, row_count);
    return 0;
}

const zvt_backend_t zvt_cpu_backend = {"cpu",     0,          cpu_smooth, cpu_quant,
                                       cpu_edges, cpu_metric, cpu_diff};

/* ---- presets ------------------------------------------------------------ */
void zvt_preset(zvt_params_t *p, zvt_preset_t preset)
{
    if (!p) return;
    mzero(p, sizeof *p);
    p->preset = preset;
    p->bg = 0xFFFFFFu;
    p->kmeans = 4;
    switch (preset) {
    case ZVT_PRESET_PIXEL:
        p->colours = 256, p->min_area = 1, p->tol_q4 = 0, p->corner_run = 1;
        p->max_depth = 8, p->pixel_exact = true, p->kmeans = 0;
        break;
    case ZVT_PRESET_ICON:
        p->colours = 24, p->min_area = 8, p->tol_q4 = 6, p->corner_run = 3;
        p->max_depth = 8, p->smooth = 0, p->smooth_thr = 0, p->absorb_thin = true;
        break;
    case ZVT_PRESET_ILLUSTRATION:
        p->colours = 48, p->min_area = 12, p->tol_q4 = 10, p->corner_run = 4;
        p->max_depth = 4, p->smooth = 1, p->smooth_thr = 12, p->gradients = true;
        p->absorb_thin = true;
        break;
    default:
        p->preset = ZVT_PRESET_PHOTO;
        p->colours = 64, p->min_area = 16, p->tol_q4 = 14, p->corner_run = 6;
        p->max_depth = 2, p->smooth = 2, p->smooth_thr = 20, p->gradients = true;
        p->absorb_thin = true;
        p->kmeans = 6;
        break;
    }
}

const char *zvt_preset_name(zvt_preset_t p)
{
    static const char *n[] = {"pixel", "icon", "illustration", "photo"};
    return (unsigned) p < ZVT_PRESET_COUNT ? n[p] : "?";
}

/* ---- the work arena ----------------------------------------------------- */
typedef struct {
    uint8_t *base;
    uint64_t cap, used, peak;
    bool full;
} arena_t;

static void *ar_get(arena_t *a, uint64_t bytes)
{
    uint64_t at = (a->used + 7u) & ~(uint64_t) 7u;
    if (a->full || bytes > a->cap || at > a->cap - bytes) {
        a->full = true;
        return 0;
    }
    a->used = at + bytes;
    if (a->used > a->peak) a->peak = a->used;
    return a->base + at;
}

/* ---- the trace context -------------------------------------------------- */
typedef struct {
    uint32_t w, h, n, nc, hc;
    const zvt_params_t *p;
    const zvt_backend_t *be;
    zvt_result_t *res;
    arena_t ar;
    zui_vec_work_t *vw;
    uint32_t *ref, *wpx, *rnd; /* source ARGB; smoothed / flattened; render */
    uint8_t *idx, *edge;
    uint32_t *lab;
    uint32_t npal;
    uint32_t pal[256];
    bool transp; /* any transparent pixel                        */
    /* regions */
    uint32_t R;
    uint32_t *area, *rpal, *par, *depth, *tin, *tout, *order, *bytin, *stamp, *acc, *grad;
    uint32_t *cadj0, *cadj; /* chains per region (CSR)                  */
    /* chains */
    int32_t *crack;     /* 2 * chain + reversed, or -1                 */
    uint32_t A, nch;    /* active cracks, chains                       */
    uint32_t *poff;     /* nch + 1 offsets into pool                   */
    uint32_t *pool;     /* packed vertices y * (w + 1) + x             */
    uint32_t *cl, *cr;  /* left / right label (forward)                */
    uint8_t *cf;        /* bit0 closed, bits 2-3 d0, bits 4-5 d1       */
    uint8_t *lvl;       /* refit level                                 */
    uint32_t *cst;      /* per-chain stamp while emitting              */
    int32_t *csx, *csy; /* where the fitted chain starts, q4 (a vertex, or for
                           a smooth closed chain its smoothed start)   */
    uint32_t *soff;     /* nch + 1 offsets into seg                    */
    int32_t *seg;
    uint64_t segcap;
    uint32_t lmax; /* longest chain                               */
    /* fit scratch */
    int32_t *qx, *qy;
    uint8_t *qa;
    int32_t *qt;
    int32_t *stk;
    /* gradients */
    uint32_t ngrad;
    int32_t gm[ZVT_MAX_GRAD][6];
    uint8_t gpal[ZVT_MAX_GRAD][2];
} ctx_t;

static uint32_t lab_at(const ctx_t *c, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= (int32_t) c->w || y >= (int32_t) c->h) return OUT;
    return c->lab[(uint32_t) y * c->w + (uint32_t) x];
}

static void be_quant(ctx_t *c, const uint32_t *px, const uint32_t *pal, uint32_t npal)
{
    if (!c->be->quant || c->be->quant(c->be->ctx, px, c->n, pal, npal, c->idx)) {
        if (c->be != &zvt_cpu_backend) c->res->fallbacks++;
        zvt_cpu_quant(px, c->n, pal, npal, c->idx);
    }
}

/* ---- 3: the palette ----------------------------------------------------- */
typedef struct {
    uint8_t lo[3], hi[3];
    uint64_t cnt;
} box_t;

static void box_shrink(const uint32_t *hist, box_t *b)
{
    uint8_t lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
    uint64_t cnt = 0;
    for (uint32_t r = b->lo[0]; r <= b->hi[0]; r++)
        for (uint32_t g = b->lo[1]; g <= b->hi[1]; g++)
            for (uint32_t bl = b->lo[2]; bl <= b->hi[2]; bl++) {
                uint32_t k = hist[(r << 10) | (g << 5) | bl];
                if (!k) continue;
                cnt += k;
                uint32_t v[3] = {r, g, bl};
                for (int i = 0; i < 3; i++) {
                    if (v[i] < lo[i]) lo[i] = (uint8_t) v[i];
                    if (v[i] > hi[i]) hi[i] = (uint8_t) v[i];
                }
            }
    b->cnt = cnt;
    if (cnt)
        for (int i = 0; i < 3; i++) b->lo[i] = lo[i], b->hi[i] = hi[i];
}

static uint32_t median_cut(ctx_t *c, const uint32_t *px, uint32_t want, uint32_t *pal)
{
    uint32_t *hist = (uint32_t *) ar_get(&c->ar, 32768u * 4u);
    if (!hist) return 0;
    mzero(hist, 32768u * 4u);
    for (uint32_t i = 0; i < c->n; i++) {
        uint32_t q = px[i];
        if ((q >> 24) < 128u) continue;
        hist[((q >> 9) & 0x7C00u) | ((q >> 6) & 0x3E0u) | ((q >> 3) & 0x1Fu)]++;
    }
    box_t box[256];
    uint32_t nb = 1;
    box[0] = (box_t){{0, 0, 0}, {31, 31, 31}, 0};
    box_shrink(hist, &box[0]);
    if (!box[0].cnt) return 0;
    while (nb < want) {
        /* the box with the most pixels times its longest side that can split */
        uint32_t best = NONE;
        uint64_t bscore = 0;
        for (uint32_t i = 0; i < nb; i++) {
            uint32_t side = 0;
            for (int k = 0; k < 3; k++)
                if ((uint32_t) (box[i].hi[k] - box[i].lo[k]) > side)
                    side = (uint32_t) (box[i].hi[k] - box[i].lo[k]);
            uint64_t s = box[i].cnt * side;
            if (side && s > bscore) bscore = s, best = i;
        }
        if (best == NONE) break;
        box_t *b = &box[best];
        int ax = 0;
        for (int k = 1; k < 3; k++)
            if (b->hi[k] - b->lo[k] > b->hi[ax] - b->lo[ax]) ax = k;
        /* cumulative count along ax: split where it passes half */
        uint64_t half = b->cnt / 2, run = 0;
        uint32_t cut = b->lo[ax];
        for (uint32_t v = b->lo[ax]; v < b->hi[ax]; v++) {
            uint8_t lo[3] = {b->lo[0], b->lo[1], b->lo[2]}, hi[3] = {b->hi[0], b->hi[1], b->hi[2]};
            lo[ax] = hi[ax] = (uint8_t) v;
            for (uint32_t r = lo[0]; r <= hi[0]; r++)
                for (uint32_t g = lo[1]; g <= hi[1]; g++)
                    for (uint32_t bl = lo[2]; bl <= hi[2]; bl++)
                        run += hist[(r << 10) | (g << 5) | bl];
            cut = v;
            if (run >= half) break;
        }
        box_t nbx = *b;
        b->hi[ax] = (uint8_t) cut;
        nbx.lo[ax] = (uint8_t) (cut + 1u);
        box_shrink(hist, b);
        box_shrink(hist, &nbx);
        box[nb++] = nbx;
    }
    uint32_t np = 0;
    for (uint32_t i = 0; i < nb; i++) {
        uint64_t s[3] = {0, 0, 0}, cnt = 0;
        for (uint32_t r = box[i].lo[0]; r <= box[i].hi[0]; r++)
            for (uint32_t g = box[i].lo[1]; g <= box[i].hi[1]; g++)
                for (uint32_t bl = box[i].lo[2]; bl <= box[i].hi[2]; bl++) {
                    uint64_t k = hist[(r << 10) | (g << 5) | bl];
                    s[0] += k * (r * 8u + 4u), s[1] += k * (g * 8u + 4u),
                        s[2] += k * (bl * 8u + 4u);
                    cnt += k;
                }
        if (!cnt) continue;
        pal[np++] = 0xFF000000u | (uint32_t) ((s[0] + cnt / 2) / cnt) << 16 |
                    (uint32_t) ((s[1] + cnt / 2) / cnt) << 8 | (uint32_t) ((s[2] + cnt / 2) / cnt);
    }
    return np;
}

static void kmeans(ctx_t *c, const uint32_t *px, uint32_t *pal, uint32_t np, uint32_t rounds)
{
    uint64_t s[256][4];
    for (uint32_t it = 0; it < rounds; it++) {
        be_quant(c, px, pal, np);
        mzero(s, sizeof s);
        for (uint32_t i = 0; i < c->n; i++) {
            uint32_t k = c->idx[i];
            if (k == TRANSP) continue;
            uint32_t q = px[i];
            s[k][0] += (q >> 16) & 255u, s[k][1] += (q >> 8) & 255u, s[k][2] += q & 255u;
            s[k][3]++;
        }
        bool moved = false;
        for (uint32_t k = 0; k < np; k++) {
            uint64_t m = s[k][3];
            if (!m) continue;
            uint32_t v = 0xFF000000u | (uint32_t) ((s[k][0] + m / 2) / m) << 16 |
                         (uint32_t) ((s[k][1] + m / 2) / m) << 8 |
                         (uint32_t) ((s[k][2] + m / 2) / m);
            if (v != pal[k]) moved = true;
            pal[k] = v;
        }
        if (!moved) break;
    }
}

/* the picture's own colours when there are few enough and no partial alpha */
static bool exact_palette(ctx_t *c, uint32_t *pal, uint32_t *np)
{
    uint32_t tab[1024];
    mzero(tab, sizeof tab);
    uint32_t cap = c->transp ? 255u : 256u, k = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        uint32_t q = c->ref[i], a = q >> 24;
        if (a != 0u && a != 255u) return false;
        if (a == 0u) continue;
        uint32_t key = (q & 0xFFFFFFu) + 1u, hsh = (key * 2654435761u) >> 22;
        while (tab[hsh] && tab[hsh] != key) hsh = (hsh + 1u) & 1023u;
        if (tab[hsh]) continue;
        if (k == cap) return false;
        tab[hsh] = key;
        pal[k++] = 0xFF000000u | (key - 1u);
    }
    if (!k) pal[k++] = 0xFF000000u | c->p->bg;
    *np = k;
    return true;
}

/* ---- 4: regions ----------------------------------------------------------- */
static uint32_t uf_find(uint32_t *u, uint32_t x)
{
    while (u[x] != x) {
        u[x] = u[u[x]];
        x = u[x];
    }
    return x;
}
static void uf_union(uint32_t *u, uint32_t a, uint32_t b) /* the smaller index is the root */
{
    a = uf_find(u, a), b = uf_find(u, b);
    if (a < b)
        u[b] = a;
    else if (b < a)
        u[a] = b;
}

/* transparent specks: a 4-connected transparent component smaller than
 * min_area that does not touch the picture's edge is filled with the colour
 * of an opaque neighbour (a hole of one or two pixels is a loop in every
 * shape around it, and thousands of them cannot fit the renderer's caps).
 * Only when min_area > 1, so pixel-exact mode keeps every hole until it has
 * to give up exactness anyway. Uses lab and rnd as scratch. */
static void fill_specks(ctx_t *c, uint32_t min_area)
{
    if (min_area < 2 || !c->transp) return;
    uint32_t w = c->w, h = c->h, n = c->n, *seen = c->lab, *list = c->rnd;
    for (uint32_t i = 0; i < n; i++) seen[i] = 0;
    for (uint32_t s = 0; s < n; s++) {
        if (c->idx[s] != TRANSP || seen[s]) continue;
        uint32_t k = 0, head = 0, fill = TRANSP;
        bool edge = false;
        seen[s] = 1, list[k++] = s;
        while (head < k) {
            uint32_t i = list[head++], x = i % w, y = i / w;
            if (x == 0 || y == 0 || x + 1 == w || y + 1 == h) edge = true;
            for (int d = 0; d < 4; d++) {
                int32_t nx = (int32_t) x + DX[d], ny = (int32_t) y + DY[d];
                if (nx < 0 || ny < 0 || nx >= (int32_t) w || ny >= (int32_t) h) continue;
                uint32_t j = (uint32_t) ny * w + (uint32_t) nx;
                if (c->idx[j] != TRANSP) {
                    if (fill == TRANSP) fill = c->idx[j];
                } else if (!seen[j]) {
                    seen[j] = 1, list[k++] = j;
                }
            }
        }
        if (k < min_area && !edge && fill != TRANSP)
            for (uint32_t q = 0; q < k; q++) c->idx[list[q]] = (uint8_t) fill;
    }
}

static int label(ctx_t *c)
{
    uint32_t w = c->w, n = c->n, *u = c->lab;
    if (!c->be->edges || c->be->edges(c->be->ctx, c->idx, c->w, c->h, c->edge)) {
        if (c->be != &zvt_cpu_backend) c->res->fallbacks++;
        zvt_cpu_edges(c->idx, c->w, c->h, c->edge);
    }
    for (uint32_t i = 0; i < n; i++) u[i] = i;
    for (uint32_t i = 0; i < n; i++) {
        if (c->idx[i] == TRANSP) continue;
        if (!(c->edge[i] & 1u) && (i % w) + 1 < w) uf_union(u, i, i + 1);
        if (!(c->edge[i] & 2u) && i + w < n) uf_union(u, i, i + w);
    }
    /* roots are the smallest index of their set: one ascending pass
     * compresses every path, a second numbers the roots */
    uint32_t *id = c->rnd, R = 0;
    for (uint32_t i = 0; i < n; i++) u[i] = u[u[i]];
    for (uint32_t i = 0; i < n; i++) {
        if (c->idx[i] == TRANSP) {
            id[i] = OUT;
            continue;
        }
        id[i] = u[i] == i ? R++ : id[u[i]];
    }
    for (uint32_t i = 0; i < n; i++) u[i] = id[i];
    c->R = R;
    return 0;
}

static void region_stats(ctx_t *c)
{
    for (uint32_t r = 0; r < c->R; r++) c->area[r] = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        uint32_t l = c->lab[i];
        if (l == OUT) continue;
        c->area[l]++;
        c->rpal[l] = c->idx[i];
    }
}

/* regions under min_area go into the adjacent region of nearest colour */
static int merge_small(ctx_t *c, uint32_t min_area)
{
    if (min_area <= 1 || c->R < 2) return 0;
    uint64_t mark = c->ar.used;
    uint32_t R = c->R, w = c->w, n = c->n;
    uint32_t *deg = (uint32_t *) ar_get(&c->ar, ((uint64_t) R + 1) * 4u);
    uint32_t *uf = (uint32_t *) ar_get(&c->ar, (uint64_t) R * 4u);
    uint32_t *ord = (uint32_t *) ar_get(&c->ar, (uint64_t) R * 4u);
    if (!deg || !uf || !ord) return ZVT_E_SPACE;
    mzero(deg, ((uint64_t) R + 1) * 4u);
    uint32_t small = 0;
    for (uint32_t r = 0; r < R; r++) small += c->area[r] < min_area;
    if (!small) {
        c->ar.used = mark;
        return 0;
    }
    /* adjacency lists of small regions only */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = c->lab[i];
        if (a == OUT) continue;
        uint32_t nb[2] = {(i % w) + 1 < w ? c->lab[i + 1] : OUT, i + w < n ? c->lab[i + w] : OUT};
        for (int k = 0; k < 2; k++) {
            uint32_t b = nb[k];
            if (b == OUT || b == a) continue;
            if (c->area[a] < min_area) deg[a + 1]++;
            if (c->area[b] < min_area) deg[b + 1]++;
        }
    }
    for (uint32_t r = 0; r < R; r++) deg[r + 1] += deg[r];
    uint32_t *adj = (uint32_t *) ar_get(&c->ar, (uint64_t) deg[R] * 4u + 4u);
    uint32_t *fill = (uint32_t *) ar_get(&c->ar, (uint64_t) R * 4u + 4u);
    if (!adj || !fill) return ZVT_E_SPACE;
    for (uint32_t r = 0; r < R; r++) fill[r] = deg[r];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = c->lab[i];
        if (a == OUT) continue;
        uint32_t nb[2] = {(i % w) + 1 < w ? c->lab[i + 1] : OUT, i + w < n ? c->lab[i + w] : OUT};
        for (int k = 0; k < 2; k++) {
            uint32_t b = nb[k];
            if (b == OUT || b == a) continue;
            if (c->area[a] < min_area) adj[fill[a]++] = b;
            if (c->area[b] < min_area) adj[fill[b]++] = a;
        }
    }
    /* small regions by ascending area (counting sort), then merge */
    uint32_t *bk = (uint32_t *) ar_get(&c->ar, ((uint64_t) min_area + 1) * 4u);
    if (!bk) return ZVT_E_SPACE;
    mzero(bk, ((uint64_t) min_area + 1) * 4u);
    for (uint32_t r = 0; r < R; r++)
        if (c->area[r] < min_area) bk[c->area[r] + 1]++;
    for (uint32_t a = 0; a < min_area; a++) bk[a + 1] += bk[a];
    for (uint32_t r = 0; r < R; r++)
        if (c->area[r] < min_area) ord[bk[c->area[r]]++] = r;
    uint32_t k = small;
    for (uint32_t i = 0; i < R; i++) uf[i] = i;
    uint32_t *ar2 = c->acc; /* running areas of the merged sets */
    for (uint32_t r = 0; r < R; r++) ar2[r] = c->area[r];
    for (int pass = 0; pass < 3; pass++) {
        bool any = false;
        for (uint32_t q = 0; q < k; q++) {
            uint32_t r = ord[q], s = uf_find(uf, r);
            if (ar2[s] >= min_area) continue;
            uint32_t best = NONE, bd = 0;
            for (uint32_t e = deg[r]; e < deg[r + 1]; e++) {
                uint32_t t = uf_find(uf, adj[e]);
                if (t == s) continue;
                uint32_t d = cdist(c->pal[c->rpal[s]], c->pal[c->rpal[t]]);
                if (best == NONE || d < bd || (d == bd && ar2[t] > ar2[best]) ||
                    (d == bd && ar2[t] == ar2[best] && t < best))
                    best = t, bd = d;
            }
            if (best == NONE) continue;
            uf[s] = best;
            ar2[best] += ar2[s];
            any = true;
        }
        if (!any) break;
    }
    /* renumber */
    uint32_t *nid = ord, R2 = 0;
    for (uint32_t r = 0; r < R; r++) nid[r] = NONE;
    for (uint32_t r = 0; r < R; r++) {
        uint32_t s = uf_find(uf, r);
        if (nid[s] == NONE) nid[s] = R2++;
    }
    uint32_t *colour = deg; /* reuse: palette index of each new region */
    for (uint32_t r = 0; r < R; r++)
        if (uf[r] == r) colour[nid[r]] = c->rpal[r];
    for (uint32_t i = 0; i < n; i++)
        if (c->lab[i] != OUT) c->lab[i] = nid[uf_find(uf, c->lab[i])];
    for (uint32_t r = 0; r < R2; r++) c->rpal[r] = colour[r];
    c->R = R2;
    for (uint32_t r = 0; r < R2; r++) c->area[r] = 0;
    for (uint32_t i = 0; i < n; i++)
        if (c->lab[i] != OUT) c->area[c->lab[i]]++;
    c->ar.used = mark;
    return 0;
}

/* anti-aliasing fringes: regions about one pixel thick go, pixel by pixel,
 * to the adjacent thick region nearest the pixel's own colour; then the
 * index map is rewritten so the caller can label again. 1 when changed. */
static int absorb_thin(ctx_t *c)
{
    uint32_t R = c->R, w = c->w, n = c->n, *per = c->acc, *nl = c->rnd;
    uint8_t *thin = c->edge; /* per region would need R bytes: mark via per[] */
    (void) thin;
    for (uint32_t r = 0; r < R; r++) per[r] = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l = c->lab[i];
        if (l == OUT) continue;
        uint32_t x = i % w;
        per[l] += (x == 0 || c->lab[i - 1] != l) + (x + 1 == w || c->lab[i + 1] != l) +
                  (i < w || c->lab[i - w] != l) + (i + w >= n || c->lab[i + w] != l);
    }
    uint32_t nthin = 0;
    for (uint32_t r = 0; r < R; r++) {
        bool t = (uint64_t) c->area[r] * 4u < (uint64_t) per[r] * 3u;
        per[r] = t; /* per[] now says thin */
        nthin += t;
    }
    /* thin is not enough: a fringe sits BETWEEN two colours (its colour on
     * the way from one neighbour's to the other's) and touches both along
     * its length (each at least a fifth of its contacts). A thin line with
     * the same colour on both sides, or touching another colour only at an
     * end, is a line, and stays. Per thin region: up to 4 neighbouring
     * colours (packed in par[]) and their contact counts (depth, tin, tout,
     * order): none of them in use before the forest. */
    uint32_t *nbc = c->par, *cnt[4] = {c->depth, c->tin, c->tout, c->order};
    for (uint32_t r = 0; r < R; r++) {
        nbc[r] = 0xFFFFFFFFu;
        for (int k = 0; k < 4; k++) cnt[k][r] = 0;
    }
    for (uint32_t i = 0; i < n && nthin; i++) {
        uint32_t l = c->lab[i];
        if (l == OUT || !per[l]) continue;
        uint32_t x = i % w;
        uint32_t nb[4] = {x ? c->lab[i - 1] : OUT, x + 1 < w ? c->lab[i + 1] : OUT,
                          i >= w ? c->lab[i - w] : OUT, i + w < n ? c->lab[i + w] : OUT};
        for (int k = 0; k < 4; k++) {
            if (nb[k] == OUT || per[nb[k]]) continue;
            uint32_t col = c->rpal[nb[k]];
            for (uint32_t s2 = 0; s2 < 4; s2++) {
                uint32_t v = (nbc[l] >> (8u * s2)) & 255u;
                if (v == 255u)
                    nbc[l] = (nbc[l] & ~(255u << (8u * s2))) | (col << (8u * s2)), v = col;
                if (v == col) {
                    cnt[s2][l]++;
                    break;
                }
            }
        }
    }
    for (uint32_t r = 0; r < R && nthin; r++) {
        if (!per[r]) continue;
        uint32_t f = c->pal[c->rpal[r]], all = 0;
        for (int k = 0; k < 4; k++) all += cnt[k][r];
        bool between = false;
        for (uint32_t a = 0; a < 4 && !between; a++)
            for (uint32_t b = a + 1; b < 4 && !between; b++) {
                uint32_t ia = (nbc[r] >> (8u * a)) & 255u, ib = (nbc[r] >> (8u * b)) & 255u;
                if (ia == 255u || ib == 255u) continue;
                if ((uint64_t) cnt[a][r] * 5u < all || (uint64_t) cnt[b][r] * 5u < all) continue;
                uint32_t A = c->pal[ia], B = c->pal[ib];
                uint64_t dab = isqrt64(rgb_d2(A, B)), daf = isqrt64(rgb_d2(A, f)),
                         dfb = isqrt64(rgb_d2(f, B));
                between = daf + dfb <= dab + dab / 4u + 16u;
            }
        if (!between) per[r] = 0, nthin--;
    }
    if (!nthin || nthin == R) {
        for (uint32_t r = 0; r < R; r++) per[r] = 0;
        return 0;
    }
    for (int pass = 0; pass < 6; pass++) {
        uint32_t moved = 0;
        for (uint32_t i = 0; i < n; i++) nl[i] = c->lab[i];
        for (uint32_t i = 0; i < n; i++) {
            uint32_t l = c->lab[i];
            if (l == OUT || !per[l]) continue;
            uint32_t x = i % w, best = NONE, bd = 0;
            uint32_t nb[4] = {x ? c->lab[i - 1] : OUT, x + 1 < w ? c->lab[i + 1] : OUT,
                              i >= w ? c->lab[i - w] : OUT, i + w < n ? c->lab[i + w] : OUT};
            for (int k = 0; k < 4; k++) {
                uint32_t q = nb[k];
                if (q == OUT || per[q]) continue;
                uint32_t d = cdist(c->wpx[i], c->pal[c->rpal[q]]);
                if (best == NONE || d < bd || (d == bd && q < best)) best = q, bd = d;
            }
            if (best != NONE) nl[i] = best, moved++;
        }
        for (uint32_t i = 0; i < n; i++) c->lab[i] = nl[i];
        if (!moved) break;
    }
    for (uint32_t i = 0; i < n; i++)
        if (c->lab[i] != OUT) c->idx[i] = (uint8_t) c->rpal[c->lab[i]];
    for (uint32_t r = 0; r < R; r++) per[r] = 0;
    return 1;
}

/* ---- 5: the crack graph ---------------------------------------------------- */
static int32_t crack_at(const ctx_t *c, int32_t x, int32_t y, int d) /* from vertex (x, y) */
{
    int32_t w = (int32_t) c->w, h = (int32_t) c->h;
    switch (d) {
    case E_:
        return x < w ? y * w + x : -1;
    case S_:
        return y < h ? (int32_t) c->hc + y * (w + 1) + x : -1;
    case W_:
        return x > 0 ? y * w + x - 1 : -1;
    default:
        return y > 0 ? (int32_t) c->hc + (y - 1) * (w + 1) + x : -1;
    }
}
static bool crack_active(const ctx_t *c, int32_t id)
{
    if (id < 0) return false;
    uint32_t u = (uint32_t) id;
    if (u < c->hc) {
        int32_t y = (int32_t) (u / c->w), x = (int32_t) (u % c->w);
        return lab_at(c, x, y - 1) != lab_at(c, x, y);
    }
    u -= c->hc;
    int32_t y = (int32_t) (u / (c->w + 1)), x = (int32_t) (u % (c->w + 1));
    return lab_at(c, x - 1, y) != lab_at(c, x, y);
}
static bool step_ok(const ctx_t *c, int32_t x, int32_t y, int d)
{
    return crack_active(c, crack_at(c, x, y, d));
}
static int vdeg(const ctx_t *c, int32_t x, int32_t y)
{
    int k = 0;
    for (int d = 0; d < 4; d++) k += step_ok(c, x, y, d);
    return k;
}
/* left and right label of the crack walked from (x, y) heading d */
static void sides(const ctx_t *c, int32_t x, int32_t y, int d, uint32_t *l, uint32_t *r)
{
    switch (d) {
    case E_:
        *l = lab_at(c, x, y - 1), *r = lab_at(c, x, y);
        break;
    case W_:
        *l = lab_at(c, x - 1, y), *r = lab_at(c, x - 1, y - 1);
        break;
    case S_:
        *l = lab_at(c, x, y), *r = lab_at(c, x - 1, y);
        break;
    default:
        *l = lab_at(c, x - 1, y - 1), *r = lab_at(c, x, y - 1);
        break;
    }
}

static int chains(ctx_t *c)
{
    uint32_t nc = c->nc, W1 = c->w + 1;
    c->A = 0;
    for (uint32_t i = 0; i < nc; i++) {
        c->crack[i] = -1;
        c->A += crack_active(c, (int32_t) i);
    }
    uint32_t A = c->A;
    c->poff = (uint32_t *) ar_get(&c->ar, ((uint64_t) A + 1) * 4u);
    c->pool = (uint32_t *) ar_get(&c->ar, ((uint64_t) A * 2u + 1) * 4u);
    c->cl = (uint32_t *) ar_get(&c->ar, (uint64_t) A * 4u + 4u);
    c->cr = (uint32_t *) ar_get(&c->ar, (uint64_t) A * 4u + 4u);
    c->cf = (uint8_t *) ar_get(&c->ar, (uint64_t) A + 1u);
    c->lvl = (uint8_t *) ar_get(&c->ar, (uint64_t) A + 1u);
    c->cst = (uint32_t *) ar_get(&c->ar, (uint64_t) A * 4u + 4u);
    c->csx = (int32_t *) ar_get(&c->ar, (uint64_t) A * 4u + 4u);
    c->csy = (int32_t *) ar_get(&c->ar, (uint64_t) A * 4u + 4u);
    if (!c->csx || !c->csy || !c->cst || !c->poff || !c->pool || !c->cl || !c->cr || !c->cf ||
        !c->lvl)
        return ZVT_E_SPACE;
    uint32_t k = 0, pn = 0;
    c->lmax = 0;
    for (uint32_t i = 0; i < nc; i++) {
        if (c->crack[i] >= 0 || !crack_active(c, (int32_t) i)) continue;
        int32_t ax, ay;
        int hd;
        if (i < c->hc)
            ay = (int32_t) (i / c->w), ax = (int32_t) (i % c->w), hd = E_;
        else
            ay = (int32_t) ((i - c->hc) / W1), ax = (int32_t) ((i - c->hc) % W1), hd = S_;
        /* walk back to a junction, or round a closed loop */
        int32_t vx = ax, vy = ay, sx = ax, sy = ay;
        int hin = hd, sh = hd;
        bool closed = false;
        for (uint32_t guard = 0;; guard++) {
            if (guard > A) return ZVT_E_CHECK;
            if (vdeg(c, vx, vy) != 2) {
                sx = vx, sy = vy, sh = hin;
                break;
            }
            int d = -1;
            for (int q = 0; q < 4; q++)
                if (q != hin && step_ok(c, vx, vy, q)) d = q;
            if (crack_at(c, vx, vy, d) == (int32_t) i) {
                closed = true;
                sx = ax, sy = ay, sh = hd;
                break;
            }
            vx += DX[d], vy += DY[d];
            hin = (d + 2) & 3;
        }
        /* walk forward, assigning */
        c->poff[k] = pn;
        c->pool[pn++] = (uint32_t) sy * W1 + (uint32_t) sx;
        sides(c, sx, sy, sh, &c->cl[k], &c->cr[k]);
        vx = sx, vy = sy;
        int h = sh, last = sh;
        uint32_t len = 0;
        for (;;) {
            int32_t e = crack_at(c, vx, vy, h);
            if (e < 0 || c->crack[e] >= 0) return ZVT_E_CHECK;
            c->crack[e] = (int32_t) (2u * k + ((h == E_ || h == S_) ? 0u : 1u));
            vx += DX[h], vy += DY[h];
            c->pool[pn++] = (uint32_t) vy * W1 + (uint32_t) vx;
            len++;
            last = h;
            if (closed ? (vx == sx && vy == sy) : vdeg(c, vx, vy) != 2) break;
            int nh = -1;
            for (int q = 0; q < 4; q++)
                if (q != ((h + 2) & 3) && step_ok(c, vx, vy, q)) nh = q;
            if (nh < 0) return ZVT_E_CHECK;
            h = nh;
        }
        if (len > c->lmax) c->lmax = len;
        c->cf[k] = (uint8_t) ((closed ? 1u : 0u) | ((uint32_t) sh << 2) | ((uint32_t) last << 4));
        c->lvl[k] = 0;
        k++;
    }
    c->poff[k] = pn;
    c->nch = k;
    /* chains of each region */
    uint32_t R = c->R;
    c->cadj0 = (uint32_t *) ar_get(&c->ar, ((uint64_t) R + 1) * 4u);
    c->cadj = (uint32_t *) ar_get(&c->ar, (uint64_t) k * 8u + 4u);
    if (!c->cadj0 || !c->cadj) return ZVT_E_SPACE;
    mzero(c->cadj0, ((uint64_t) R + 1) * 4u);
    for (uint32_t q = 0; q < k; q++) {
        if (c->cl[q] != OUT) c->cadj0[c->cl[q] + 1]++;
        if (c->cr[q] != OUT) c->cadj0[c->cr[q] + 1]++;
    }
    for (uint32_t r = 0; r < R; r++) c->cadj0[r + 1] += c->cadj0[r];
    for (uint32_t r = 0; r < R; r++) c->acc[r] = c->cadj0[r];
    for (uint32_t q = 0; q < k; q++) {
        if (c->cl[q] != OUT) c->cadj[c->acc[c->cl[q]]++] = q;
        if (c->cr[q] != OUT) c->cadj[c->acc[c->cr[q]]++] = q;
    }
    return 0;
}

/* ---- 7: the layering forest ------------------------------------------------ */
static bool heap_less(const ctx_t *c, uint32_t a, uint32_t b) /* a after b */
{
    return c->area[a] < c->area[b] || (c->area[a] == c->area[b] && a > b);
}
static void heap_push(const ctx_t *c, uint32_t *hp, uint32_t *n, uint32_t v)
{
    uint32_t i = (*n)++;
    hp[i] = v;
    while (i && heap_less(c, hp[(i - 1) / 2], hp[i])) {
        uint32_t t = hp[i];
        hp[i] = hp[(i - 1) / 2];
        hp[(i - 1) / 2] = t;
        i = (i - 1) / 2;
    }
}
static uint32_t heap_pop(const ctx_t *c, uint32_t *hp, uint32_t *n)
{
    uint32_t top = hp[0];
    hp[0] = hp[--(*n)];
    uint32_t i = 0;
    for (;;) {
        uint32_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < *n && heap_less(c, hp[m], hp[l])) m = l;
        if (r < *n && heap_less(c, hp[m], hp[r])) m = r;
        if (m == i) break;
        uint32_t t = hp[i];
        hp[i] = hp[m];
        hp[m] = t;
        i = m;
    }
    return top;
}

static uint32_t chain_len(const ctx_t *c, uint32_t q)
{
    return c->poff[q + 1] - c->poff[q] - 1u;
}
static uint32_t other(const ctx_t *c, uint32_t q, uint32_t r)
{
    return c->cl[q] == r ? c->cr[q] : c->cl[q];
}

static int forest(ctx_t *c)
{
    uint32_t R = c->R;
    uint64_t mark = c->ar.used;
    uint32_t hcap = R + 2u * c->nch + 1u, nh = 0;
    uint32_t *hp = (uint32_t *) ar_get(&c->ar, (uint64_t) hcap * 4u);
    uint32_t *sorted = (uint32_t *) ar_get(&c->ar, (uint64_t) R * 4u + 4u);
    uint32_t *tmp = (uint32_t *) ar_get(&c->ar, (uint64_t) R * 4u + 4u);
    if (!hp || !sorted || !tmp) return ZVT_E_SPACE;
    /* regions by descending area, then ascending id: LSD radix on ~area */
    for (uint32_t r = 0; r < R; r++) sorted[r] = r;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[257];
        mzero(cnt, sizeof cnt);
        for (uint32_t i = 0; i < R; i++) cnt[((~c->area[sorted[i]]) >> sh & 255u) + 1]++;
        for (int b = 0; b < 256; b++) cnt[b + 1] += cnt[b];
        for (uint32_t i = 0; i < R; i++) tmp[cnt[(~c->area[sorted[i]]) >> sh & 255u]++] = sorted[i];
        for (uint32_t i = 0; i < R; i++) sorted[i] = tmp[i];
    }
    uint32_t *seen = c->stamp; /* 1 once visited */
    for (uint32_t r = 0; r < R; r++) seen[r] = 0, c->acc[r] = 0, c->par[r] = NONE, c->depth[r] = 0;
    uint32_t no = 0, maxd = c->p->max_depth < 1 ? 1 : c->p->max_depth;
    for (uint32_t si = 0; si < R; si++) {
        if (seen[sorted[si]]) continue;
        heap_push(c, hp, &nh, sorted[si]);
        while (nh) {
            uint32_t u = heap_pop(c, hp, &nh);
            if (seen[u]) continue;
            seen[u] = 1;
            uint32_t best = NONE;
            for (uint32_t e = c->cadj0[u]; e < c->cadj0[u + 1]; e++) {
                uint32_t q = c->cadj[e], o = other(c, q, u);
                if (o == OUT || !seen[o] || o == u || c->depth[o] >= maxd) continue;
                c->acc[o] += chain_len(c, q);
                if (best == NONE || c->acc[o] > c->acc[best] ||
                    (c->acc[o] == c->acc[best] && c->tin[o] < c->tin[best]))
                    best = o;
            }
            for (uint32_t e = c->cadj0[u]; e < c->cadj0[u + 1]; e++) {
                uint32_t o = other(c, c->cadj[e], u);
                if (o != OUT) c->acc[o] = 0;
            }
            c->par[u] = best;
            c->depth[u] = best == NONE ? 0 : c->depth[best] + 1;
            c->tin[u] = no; /* Prim rank for now (ties above) */
            c->order[no++] = u;
            for (uint32_t e = c->cadj0[u]; e < c->cadj0[u + 1]; e++) {
                uint32_t o = other(c, c->cadj[e], u);
                if (o != OUT && !seen[o]) {
                    if (nh >= hcap) return ZVT_E_CHECK;
                    heap_push(c, hp, &nh, o);
                }
            }
        }
    }
    /* children lists, then DFS numbering: tin..tout is a subtree */
    uint32_t *k0 = tmp, *kids = sorted; /* reuse */
    mzero(k0, (uint64_t) R * 4u + 4u);
    for (uint32_t r = 0; r < R; r++)
        if (c->par[r] != NONE) k0[c->par[r] + 1]++;
    for (uint32_t r = 0; r < R; r++) k0[r + 1] += k0[r];
    for (uint32_t r = 0; r < R; r++) c->acc[r] = k0[r];
    for (uint32_t i = 0; i < R; i++) {
        uint32_t u = c->order[i];
        if (c->par[u] != NONE) kids[c->acc[c->par[u]]++] = u;
    }
    uint32_t *stk = hp, t = 0;
    for (uint32_t i = 0; i < R; i++) {
        uint32_t root = c->order[i];
        if (c->par[root] != NONE) continue;
        uint32_t sp = 0;
        stk[sp++] = root;
        c->acc[root] = k0[root];
        c->tin[root] = t;
        c->bytin[t++] = root;
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (c->acc[u] < k0[u + 1]) {
                uint32_t v = kids[c->acc[u]++];
                c->acc[v] = k0[v];
                c->tin[v] = t;
                c->bytin[t++] = v;
                stk[sp++] = v;
            } else {
                c->tout[u] = t;
                sp--;
            }
        }
    }
    for (uint32_t r = 0; r < R; r++) c->acc[r] = 0;
    c->ar.used = mark;
    return t == R ? 0 : ZVT_E_CHECK;
}

/* ---- 6: curves ----------------------------------------------------------- */
static void vxy(const ctx_t *c, uint32_t v, int32_t *x, int32_t *y)
{
    *x = (int32_t) (v % (c->w + 1)), *y = (int32_t) (v / (c->w + 1));
}

static bool seg_room(ctx_t *c, uint64_t at, uint32_t n)
{
    return at + n <= c->segcap;
}

typedef struct {
    int64_t x, y;
} p2_t;

static void unit_q14(int64_t dx, int64_t dy, int64_t *tx, int64_t *ty)
{
    int64_t l = (int64_t) isqrt64((uint64_t) (dx * dx + dy * dy));
    if (!l) {
        *tx = 16384, *ty = 0;
        return;
    }
    *tx = fdiv(dx * 16384, l), *ty = fdiv(dy * 16384, l);
}

/* Bernstein weights, Q16 */
static void bern(int64_t u, int64_t b[4])
{
    int64_t v = 65536 - u;
    int64_t uu = (u * u) >> 16, vv = (v * v) >> 16;
    b[0] = (vv * v) >> 16;
    b[1] = (3 * vv * u) >> 16;
    b[2] = (3 * uu * v) >> 16;
    b[3] = (uu * u) >> 16;
}
static p2_t bez(const p2_t P[4], int64_t u)
{
    int64_t b[4];
    bern(u, b);
    p2_t r;
    r.x = (b[0] * P[0].x + b[1] * P[1].x + b[2] * P[2].x + b[3] * P[3].x + 32768) >> 16;
    r.y = (b[0] * P[0].y + b[1] * P[1].y + b[2] * P[2].y + b[3] * P[3].y + 32768) >> 16;
    return r;
}

/* max squared error of the cubic at the points' parameters, and where */
static int64_t fit_err(const ctx_t *c, int s, int e, const p2_t P[4], int *at)
{
    int64_t m = -1;
    *at = (s + e) / 2;
    for (int i = s + 1; i < e; i++) {
        p2_t b = bez(P, c->qt[i]);
        int64_t dx = b.x - c->qx[i], dy = b.y - c->qy[i], d = dx * dx + dy * dy;
        if (d > m) m = d, *at = i;
    }
    return m;
}

static void lsq(const ctx_t *c, int s, int e, int64_t t1x, int64_t t1y, int64_t t2x, int64_t t2y,
                p2_t P[4])
{
    P[0] = (p2_t){c->qx[s], c->qy[s]};
    P[3] = (p2_t){c->qx[e], c->qy[e]};
    int64_t c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
    for (int i = s; i <= e; i++) {
        int64_t b[4];
        bern(c->qt[i], b);
        int64_t a1x = (t1x * b[1]) >> 16, a1y = (t1y * b[1]) >> 16;
        int64_t a2x = (t2x * b[2]) >> 16, a2y = (t2y * b[2]) >> 16;
        c00 += (a1x * a1x + a1y * a1y) >> 14;
        c01 += (a1x * a2x + a1y * a2y) >> 14;
        c11 += (a2x * a2x + a2y * a2y) >> 14;
        int64_t tx = c->qx[i] - ((P[0].x * (b[0] + b[1]) + P[3].x * (b[2] + b[3]) + 32768) >> 16);
        int64_t ty = c->qy[i] - ((P[0].y * (b[0] + b[1]) + P[3].y * (b[2] + b[3]) + 32768) >> 16);
        x0 += (a1x * tx + a1y * ty) >> 14;
        x1 += (a2x * tx + a2y * ty) >> 14;
    }
    int64_t chord = (int64_t) isqrt64(
        (uint64_t) ((P[3].x - P[0].x) * (P[3].x - P[0].x) + (P[3].y - P[0].y) * (P[3].y - P[0].y)));
    int64_t det = (c00 * c11 - c01 * c01) >> 14, al1 = 0, al2 = 0;
    if (det > 0) {
        al1 = fdiv(x0 * c11 - x1 * c01, det);
        al2 = fdiv(c00 * x1 - c01 * x0, det);
    }
    int64_t eps = chord / 64 + 1;
    if (det <= 0 || al1 < eps || al2 < eps || al1 > 2 * chord + 16 || al2 > 2 * chord + 16)
        al1 = al2 = chord / 3;
    P[1] = (p2_t){P[0].x + ((t1x * al1) >> 14), P[0].y + ((t1y * al1) >> 14)};
    P[2] = (p2_t){P[3].x + ((t2x * al2) >> 14), P[3].y + ((t2y * al2) >> 14)};
}

static void reparam(ctx_t *c, int s, int e, const p2_t P[4])
{
    for (int i = s + 1; i < e; i++) {
        int64_t u = c->qt[i], v = 65536 - u;
        p2_t b = bez(P, u);
        int64_t ex = b.x - c->qx[i], ey = b.y - c->qy[i];
        /* B' and B'' (q4 per unit parameter) */
        int64_t k0 = (v * v) >> 16, k1 = (2 * u * v) >> 16, k2 = (u * u) >> 16;
        int64_t d1x =
            3 * (((P[1].x - P[0].x) * k0 + (P[2].x - P[1].x) * k1 + (P[3].x - P[2].x) * k2) >> 16);
        int64_t d1y =
            3 * (((P[1].y - P[0].y) * k0 + (P[2].y - P[1].y) * k1 + (P[3].y - P[2].y) * k2) >> 16);
        int64_t d2x =
            6 * (((P[2].x - 2 * P[1].x + P[0].x) * v + (P[3].x - 2 * P[2].x + P[1].x) * u) >> 16);
        int64_t d2y =
            6 * (((P[2].y - 2 * P[1].y + P[0].y) * v + (P[3].y - 2 * P[2].y + P[1].y) * u) >> 16);
        int64_t f = ex * d1x + ey * d1y, fp = d1x * d1x + d1y * d1y + ex * d2x + ey * d2y;
        if (fp <= 0) continue;
        int64_t nu = u - muldiv(f, 65536, fp);
        c->qt[i] = (int32_t) clamp64(nu, 0, 65536);
    }
}

static void chord_params(ctx_t *c, int s, int e)
{
    int64_t tot = 0;
    c->qt[s] = 0;
    for (int i = s + 1; i <= e; i++) {
        int64_t dx = c->qx[i] - c->qx[i - 1], dy = c->qy[i] - c->qy[i - 1];
        tot += (int64_t) isqrt64((uint64_t) (dx * dx + dy * dy));
        c->qt[i] = (int32_t) clamp64(tot, 0, 0x7FFFFFFF); /* running length for now */
    }
    for (int i = s + 1; i <= e; i++)
        c->qt[i] = tot ? (int32_t) (((int64_t) c->qt[i] * 65536) / tot) : 65536;
}

/* all points within tol of the chord? */
static bool near_line(const ctx_t *c, int s, int e, int64_t tol)
{
    int64_t ax = c->qx[s], ay = c->qy[s], dx = c->qx[e] - ax, dy = c->qy[e] - ay;
    int64_t l2 = dx * dx + dy * dy;
    for (int i = s + 1; i < e; i++) {
        int64_t px = c->qx[i] - ax, py = c->qy[i] - ay;
        int64_t d2;
        if (!l2) {
            d2 = px * px + py * py;
        } else {
            int64_t cr = px * dy - py * dx;
            int64_t dot = px * dx + py * dy;
            if (dot < 0)
                d2 = px * px + py * py;
            else if (dot > l2)
                d2 = (px - dx) * (px - dx) + (py - dy) * (py - dy);
            else
                d2 = muldiv(cr, cr, l2);
        }
        if (d2 > tol * tol) return false;
    }
    return true;
}

static int put_seg(ctx_t *c, uint64_t *at, int type, const int64_t *v)
{
    uint32_t k = type == 1 ? 2u : type == 2 ? 4u : 6u;
    if (!seg_room(c, *at, k + 2u)) return ZVT_E_SPACE;
    c->seg[(*at)++] = type;
    for (uint32_t i = 0; i < k; i++) c->seg[(*at)++] = (int32_t) v[i];
    c->seg[(*at)++] = type;
    return 0;
}

/* fit points s..e (anchors at both ends) with end tangents (Q14; t2 points
 * back into the curve) */
static int fit_span(ctx_t *c, uint64_t *at, int s0, int e0, int64_t t1x0, int64_t t1y0,
                    int64_t t2x0, int64_t t2y0, int64_t tol)
{
    int32_t *stk = c->stk;
    int sp = 0;
    stk[sp++] = e0, stk[sp++] = s0;
    int64_t tan[4] = {t1x0, t1y0, t2x0, t2y0};
    /* the stack holds span ends; the start tangent of each span is carried in
     * tan[0..1] (the previous span's end tangent negated) and its end tangent
     * is recomputed from the split point, or t2 at the true end */
    int64_t t1x = tan[0], t1y = tan[1];
    while (sp) {
        int s = stk[--sp], e = stk[--sp];
        int64_t t2x, t2y;
        if (e == e0) {
            t2x = t2x0, t2y = t2y0;
        } else { /* a split point: the centred tangent over up to 3 points each side */
            int a = e - 3 < s0 ? s0 : e - 3, b = e + 3 > e0 ? e0 : e + 3;
            unit_q14(c->qx[a] - c->qx[b], c->qy[a] - c->qy[b], &t2x, &t2y);
        }
        int64_t v[6];
        if (e - s == 1 || near_line(c, s, e, tol / 4 > 2 ? tol / 4 : 2)) {
            v[0] = c->qx[e], v[1] = c->qy[e];
            if (put_seg(c, at, 1, v)) return ZVT_E_SPACE;
        } else {
            p2_t P[4];
            int split = 0;
            chord_params(c, s, e);
            lsq(c, s, e, t1x, t1y, t2x, t2y, P);
            int64_t err = fit_err(c, s, e, P, &split);
            for (int it = 0; it < 4 && err > tol * tol; it++) {
                reparam(c, s, e, P);
                lsq(c, s, e, t1x, t1y, t2x, t2y, P);
                err = fit_err(c, s, e, P, &split);
            }
            if (err > tol * tol && e - s > 2) {
                if (split <= s) split = s + 1;
                if (split >= e) split = e - 1;
                if (sp + 4 > 4 * (int) c->lmax + 32) return ZVT_E_CHECK;
                stk[sp++] = e, stk[sp++] = split; /* right half, later */
                stk[sp++] = split, stk[sp++] = s; /* left half, next   */
                continue;                         /* t1 unchanged for the left half */
            }
            int64_t q1x = (3 * P[1].x - P[0].x), q1y = (3 * P[1].y - P[0].y);
            int64_t q2x = (3 * P[2].x - P[3].x), q2y = (3 * P[2].y - P[3].y);
            if (iabs64(q1x - q2x) <= 4 && iabs64(q1y - q2y) <= 4) { /* x2: within 2 q4 */
                v[0] = fdiv(q1x + q2x + 2, 4), v[1] = fdiv(q1y + q2y + 2, 4);
                v[2] = P[3].x, v[3] = P[3].y;
                if (put_seg(c, at, 2, v)) return ZVT_E_SPACE;
            } else {
                v[0] = P[1].x, v[1] = P[1].y, v[2] = P[2].x, v[3] = P[2].y;
                v[4] = P[3].x, v[5] = P[3].y;
                if (put_seg(c, at, 3, v)) return ZVT_E_SPACE;
            }
        }
        /* the next span starts here, continuing this span's end tangent */
        t1x = -t2x, t1y = -t2y;
    }
    return 0;
}

/* corner test at interior vertex j of a chain's vertex list */
static int heading(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    return bx > ax ? E_ : bx < ax ? W_ : by > ay ? S_ : N_;
}

/* sub-pixel edge position across one crack: for an anti-aliased edge
 * between colours A (first pixel) and B (second), the coverage of A in a
 * pixel p is <p - B, A - B> / |A - B|^2; along the line across the crack
 * the edge sits at (first pixel's start) + a(first) + a(second). Returns the
 * offset from the crack, q4, clamped to half a pixel (0 when the two colours
 * are too close to tell apart or a side is transparent). */
static int32_t sub_off(const ctx_t *c, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    uint32_t l1 = lab_at(c, x1, y1), l2 = lab_at(c, x2, y2);
    if (l1 == OUT || l2 == OUT || l1 == l2) return 0;
    uint32_t A = c->pal[c->rpal[l1]], B = c->pal[c->rpal[l2]];
    int64_t ar = (A >> 16) & 255, ag = (A >> 8) & 255, ab = A & 255;
    int64_t br = (B >> 16) & 255, bg = (B >> 8) & 255, bb = B & 255;
    int64_t dr = ar - br, dg = ag - bg, db = ab - bb, dd = dr * dr + dg * dg + db * db;
    if (dd < 1200) return 0;
    int64_t sum = 0;
    for (int k = 0; k < 2; k++) {
        uint32_t p = c->wpx[(uint32_t) (k ? y2 : y1) * c->w + (uint32_t) (k ? x2 : x1)];
        int64_t v =
            (((p >> 16) & 255) - br) * dr + (((p >> 8) & 255) - bg) * dg + ((p & 255) - bb) * db;
        sum += clamp64(v, 0, dd);
    }
    return (int32_t) clamp64(rdiv(sum * 16, dd) - 16, -8, 8);
}

/* a crack's point: its midpoint, moved across it to the sub-pixel edge */
static void crack_pt(const ctx_t *c, int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t *x,
                     int32_t *y)
{
    *x = (ax + bx) * 8, *y = (ay + by) * 8;
    if (ay == by) { /* horizontal crack: the pixels above and below */
        int32_t px = ax < bx ? ax : bx;
        *y += sub_off(c, px, ay - 1, px, ay);
    } else { /* vertical crack: the pixels left and right */
        int32_t py = ay < by ? ay : by;
        *x += sub_off(c, ax - 1, py, ax, py);
    }
}

static int fit_chain(ctx_t *c, uint32_t q, uint64_t *at, int64_t tol)
{
    uint32_t p0 = c->poff[q], m = chain_len(c, q);
    bool closed = c->cf[q] & 1u;
    int32_t *X = c->qx, *Y = c->qy;
    uint8_t *F = c->qa;
    int32_t *hx = c->qt; /* headings of cracks 1..m (qt is free until chord_params) */
    {
        int32_t sx, sy;
        vxy(c, c->pool[p0], &sx, &sy);
        c->csx[q] = sx * 16, c->csy[q] = sy * 16;
    }
    for (uint32_t i = 1; i <= m; i++) {
        int32_t ax, ay, bx, by;
        vxy(c, c->pool[p0 + i - 1], &ax, &ay);
        vxy(c, c->pool[p0 + i], &bx, &by);
        hx[i] = heading(ax, ay, bx, by);
    }
    if (c->p->pixel_exact && !c->lvl[q]) {
        /* rectilinear: one LINE per straight run */
        for (uint32_t i = 1; i <= m; i++) {
            if (i < m && hx[i + 1] == hx[i]) continue;
            int32_t x, y;
            vxy(c, c->pool[p0 + i], &x, &y);
            int64_t v[2] = {(int64_t) x * 16, (int64_t) y * 16};
            if (put_seg(c, at, 1, v)) return ZVT_E_SPACE;
        }
        return 0;
    }
    uint32_t cr = c->p->corner_run < 1 ? 1 : c->p->corner_run;
    if (c->p->pixel_exact) cr += 2u + c->lvl[q]; /* a pixel-exact path over the caps: relaxed */
    /* run lengths: rin[i] = same-heading cracks ending at crack i */
    int32_t *rin = c->stk, *rout = c->stk + m + 2;
    for (uint32_t i = 1; i <= m; i++) rin[i] = (i > 1 && hx[i - 1] == hx[i]) ? rin[i - 1] + 1 : 1;
    for (uint32_t i = m; i >= 1; i--) rout[i] = (i < m && hx[i + 1] == hx[i]) ? rout[i + 1] + 1 : 1;
    if (closed && m > 1 && hx[1] == hx[m]) { /* wrap the runs through the start */
        int32_t a = rin[m], b = rout[1];
        for (uint32_t i = 1; i <= m && hx[i] == hx[1] && rin[i] < (int32_t) m; i++) rin[i] += a;
        for (uint32_t i = m; i >= 1 && hx[i] == hx[m] && rout[i] < (int32_t) m; i--) rout[i] += b;
    }
    /* the points: every crack's midpoint, and the anchors: the ends,
     * corners and image corners */
    int n = 0;
    int32_t x0, y0;
    vxy(c, c->pool[p0], &x0, &y0);
    X[n] = x0 * 16, Y[n] = y0 * 16, F[n++] = 1;
    for (uint32_t i = 1; i <= m; i++) {
        int32_t ax, ay, bx, by;
        vxy(c, c->pool[p0 + i - 1], &ax, &ay);
        vxy(c, c->pool[p0 + i], &bx, &by);
        crack_pt(c, ax, ay, bx, by, &X[n], &Y[n]);
        F[n++] = 0;
        if (i == m) {
            X[n] = bx * 16, Y[n] = by * 16, F[n++] = 1;
            break;
        }
        bool turn = hx[i + 1] != hx[i];
        bool corner = turn && (uint32_t) rin[i] >= cr && (uint32_t) rout[i + 1] >= cr;
        bool imgc = (bx == 0 || bx == (int32_t) c->w) && (by == 0 || by == (int32_t) c->h);
        if (imgc) {
            X[n] = bx * 16, Y[n] = by * 16, F[n++] = 1;
        } else if (corner) {
            /* a corner no other chain shares: where the two sub-pixel edges
             * meet (the across-offset of each neighbouring crack) */
            int32_t nx, ny, cx2, cy2;
            vxy(c, c->pool[p0 + i + 1], &cx2, &cy2);
            crack_pt(c, bx, by, cx2, cy2, &nx, &ny);
            X[n] = ay == by ? nx - (cx2 - bx) * 8 : X[n - 1];
            Y[n] = ay == by ? Y[n - 1] : ny - (cy2 - by) * 8;
            F[n++] = 1;
        }
    }
    int last = n - 1;
    if (closed && last > 2 &&
        !((x0 == 0 || x0 == (int32_t) c->w) && (y0 == 0 || y0 == (int32_t) c->h))) {
        /* a closed chain's start is shared with no one: put it on the edge
         * too (from the across-offsets of its two cracks) */
        int32_t ax, ay, bx, by;
        vxy(c, c->pool[p0 + 1], &bx, &by);
        vxy(c, c->pool[p0 + m - 1], &ax, &ay);
        int32_t ox = 0, oy = 0, k = 0;
        if (by == y0)
            oy += Y[1] - y0 * 16, k |= 1;
        else
            ox += X[1] - x0 * 16, k |= 2;
        if (ay == y0)
            oy += Y[last - 1] - y0 * 16, k |= 4;
        else
            ox += X[last - 1] - x0 * 16, k |= 8;
        if ((k & 5) == 5) oy /= 2;
        if ((k & 10) == 10) ox /= 2;
        X[0] = X[last] = x0 * 16 + ox, Y[0] = Y[last] = y0 * 16 + oy;
    }
    /* sharp turns between anchors (interior angle under ~110 degrees) */
    const int K = 4;
    for (int i = K; i + K <= last; i++) {
        if (F[i]) continue;
        int64_t ax = X[i - K] - X[i], ay = Y[i - K] - Y[i], bx = X[i + K] - X[i],
                by = Y[i + K] - Y[i];
        int64_t dot = ax * bx + ay * by;
        int64_t ll = (int64_t) isqrt64((uint64_t) ((ax * ax + ay * ay) * (bx * bx + by * by)));
        if (dot * 1000 <= -342 * ll) continue;
        /* local maximum of dot / ll among i-1, i+1 (cross-multiplied) */
        bool mx = true;
        for (int j = i - 1; j <= i + 1; j += 2) {
            int64_t cx = X[j - K < 0 ? 0 : j - K] - X[j], cy = Y[j - K < 0 ? 0 : j - K] - Y[j];
            int64_t ex = X[j + K > last ? last : j + K] - X[j],
                    ey = Y[j + K > last ? last : j + K] - Y[j];
            int64_t d2 = cx * ex + cy * ey;
            int64_t l2 = (int64_t) isqrt64((uint64_t) ((cx * cx + cy * cy) * (ex * ex + ey * ey)));
            if (l2 && muldiv(d2, ll, l2) > dot) mx = false;
        }
        if (mx) F[i] = 2;
    }
    /* force anchors so no span exceeds SPAN_MAX points */
    for (int i = 0, run = 0; i < last; i++) {
        run = F[i] ? 0 : run + 1;
        if (run >= SPAN_MAX) F[i] = 3, run = 0;
    }
    bool smooth0 = false; /* a closed chain's start is arbitrary unless a corner */
    if (closed && m > 2) {
        /* was the start vertex a real corner? its run lengths decide */
        bool turn = hx[1] != hx[m];
        smooth0 = !(turn && (uint32_t) rin[m] >= cr && (uint32_t) rout[1] >= cr) &&
                  !((x0 == 0 || x0 == (int32_t) c->w) && (y0 == 0 || y0 == (int32_t) c->h));
    }
    /* staircase ripple: two [1 2 1] passes (the points already sit at the sub-pixel edge;
     * the shrinkage on a curve of radius r px is about 1 / r px) over the points between anchors
     * (anchors stay put: they are shared with the neighbouring chains; the
     * arbitrary start of a smooth closed chain is shared with no one) */
    for (int pass = 0; pass < 2; pass++) {
        int32_t *tx = c->qt, *ty = c->stk;
        for (int i = 0; i <= last; i++) tx[i] = X[i], ty[i] = Y[i];
        for (int i = 1; i < last; i++)
            if (!F[i]) {
                X[i] = (int32_t) fdiv((int64_t) tx[i - 1] + 2 * tx[i] + tx[i + 1] + 2, 4);
                Y[i] = (int32_t) fdiv((int64_t) ty[i - 1] + 2 * ty[i] + ty[i + 1] + 2, 4);
            }
        if (smooth0 && last > 2) { /* nobody else ends here: smooth it too */
            X[0] = X[last] = (int32_t) fdiv((int64_t) tx[last - 1] + 2 * tx[0] + tx[1] + 2, 4);
            Y[0] = Y[last] = (int32_t) fdiv((int64_t) ty[last - 1] + 2 * ty[0] + ty[1] + 2, 4);
        }
    }
    c->csx[q] = X[0], c->csy[q] = Y[0];
    for (int s = 0; s < last;) {
        int e = s + 1;
        while (e < last && !F[e]) e++;
        int64_t t1x, t1y, t2x, t2y;
        int k3 = last > 7 ? 3 : 1;
        if (s == 0 && smooth0) /* the arbitrary start of a closed chain: smooth */
            unit_q14(X[k3] - X[last - k3], Y[k3] - Y[last - k3], &t1x, &t1y);
        else if (F[s] == 3) /* a forced split: smooth */
            unit_q14(X[s + 1] - X[s - 1], Y[s + 1] - Y[s - 1], &t1x, &t1y);
        else /* a corner or a junction: one-sided */
            unit_q14(X[s + 1] - X[s], Y[s + 1] - Y[s], &t1x, &t1y);
        if (e == last && smooth0)
            unit_q14(X[last - k3] - X[k3], Y[last - k3] - Y[k3], &t2x, &t2y);
        else if (F[e] == 3)
            unit_q14(X[e - 1] - X[e + 1], Y[e - 1] - Y[e + 1], &t2x, &t2y);
        else
            unit_q14(X[e - 1] - X[e], Y[e - 1] - Y[e], &t2x, &t2y);
        int rc = fit_span(c, at, s, e, t1x, t1y, t2x, t2y, tol);
        if (rc) return rc;
        s = e;
    }
    return 0;
}

static int fit_all(ctx_t *c)
{
    uint64_t at = 0;
    for (uint32_t q = 0; q < c->nch; q++) {
        c->soff[q] = (uint32_t) at;
        int64_t tol = (int64_t) (c->p->tol_q4 < 1 ? 1 : c->p->tol_q4) << c->lvl[q];
        int rc = fit_chain(c, q, &at, tol);
        if (rc) return rc;
        if (at > 0xFFFFFFF0u) return ZVT_E_SPACE;
    }
    c->soff[c->nch] = (uint32_t) at;
    return 0;
}

/* ---- 8: gradients ------------------------------------------------------- */
typedef struct {
    int64_t n, sx, sy, sc[3];
    int64_t vxx, vxy, vyy, cx[3], cy[3], cc[3];
} gst_t;

static void gradients(ctx_t *c)
{
    c->ngrad = 0;
    for (uint32_t r = 0; r < c->R; r++) c->grad[r] = NONE;
    if (!c->p->gradients || c->p->pixel_exact) return;
    uint32_t room = (256u - c->npal) / 2u;
    if (room > ZVT_MAX_GRAD) room = ZVT_MAX_GRAD;
    if (!room) return;
    uint32_t *slot = c->acc, ns = 0;
    for (uint32_t r = 0; r < c->R; r++) slot[r] = c->area[r] >= GRAD_MIN ? ns++ : NONE;
    uint64_t mark = c->ar.used;
    gst_t *g = ns ? (gst_t *) ar_get(&c->ar, (uint64_t) ns * sizeof(gst_t)) : 0;
    int32_t *bb = ns ? (int32_t *) ar_get(&c->ar, (uint64_t) ns * 16u) : 0;
    if (!g || !bb) { /* none worth it, or no room: gradients are optional */
        c->ar.full = false;
        c->ar.used = mark;
        for (uint32_t r = 0; r < c->R; r++) slot[r] = 0;
        return;
    }
    mzero(g, (uint64_t) ns * sizeof(gst_t));
    for (uint32_t s = 0; s < ns; s++)
        bb[4 * s] = bb[4 * s + 1] = 0x7FFFFFFF, bb[4 * s + 2] = bb[4 * s + 3] = -1;
    for (uint32_t i = 0; i < c->n; i++) {
        uint32_t l = c->lab[i];
        if (l == OUT || slot[l] == NONE) continue;
        gst_t *s = &g[slot[l]];
        int32_t x = (int32_t) (i % c->w), y = (int32_t) (i / c->w), *b = &bb[4 * slot[l]];
        uint32_t q = c->ref[i];
        s->n++, s->sx += 2 * x + 1, s->sy += 2 * y + 1;
        s->sc[0] += (q >> 16) & 255u, s->sc[1] += (q >> 8) & 255u, s->sc[2] += q & 255u;
        if (x < b[0]) b[0] = x;
        if (y < b[1]) b[1] = y;
        if (x > b[2]) b[2] = x;
        if (y > b[3]) b[3] = y;
    }
    for (uint32_t i = 0; i < c->n; i++) { /* centred moments, half-pixel units */
        uint32_t l = c->lab[i];
        if (l == OUT || slot[l] == NONE) continue;
        gst_t *s = &g[slot[l]];
        int64_t dx = rdiv((int64_t) (2 * (i % c->w) + 1) * s->n - s->sx, s->n);
        int64_t dy = rdiv((int64_t) (2 * (i / c->w) + 1) * s->n - s->sy, s->n);
        uint32_t q = c->ref[i];
        int64_t cv[3] = {(q >> 16) & 255u, (q >> 8) & 255u, q & 255u};
        s->vxx += dx * dx, s->vxy += dx * dy, s->vyy += dy * dy;
        for (int k = 0; k < 3; k++) {
            int64_t d = rdiv(cv[k] * s->n - s->sc[k], s->n);
            s->cx[k] += dx * cv[k], s->cy[k] += dy * cv[k], s->cc[k] += d * d;
        }
    }
    /* candidates: the SSE a plane explains (all channels), best `room` kept */
    int64_t cg[ZVT_MAX_GRAD];
    uint32_t cr[ZVT_MAX_GRAD], col[ZVT_MAX_GRAD][2], nc = 0;
    int32_t cm[ZVT_MAX_GRAD][6];
    for (uint32_t r = 0; r < c->R; r++) {
        if (slot[r] == NONE) continue;
        const gst_t *s = &g[slot[r]];
        const int32_t *b = &bb[4 * slot[r]];
        int64_t vxx = s->vxx / s->n, vxy = s->vxy / s->n, vyy = s->vyy / s->n;
        int64_t det = vxx * vyy - vxy * vxy;
        if (det <= 0) continue;
        int64_t ex = 0, flat = 0, gx[3], gy[3];
        for (int k = 0; k < 3; k++) {
            int64_t cx = s->cx[k] / s->n, cy = s->cy[k] / s->n;
            gx[k] = muldiv(cx * vyy - cy * vxy, 65536, det); /* Q16 per half pixel */
            gy[k] = muldiv(cy * vxx - cx * vxy, 65536, det);
            ex += muldiv(gx[k], s->cx[k], 65536) + muldiv(gy[k], s->cy[k], 65536);
            flat += s->cc[k];
        }
        if (ex * 3 < flat || ex < 16 * s->n) continue;
        int64_t lx = (77 * gx[0] + 150 * gx[1] + 29 * gx[2]) >> 8;
        int64_t ly = (77 * gy[0] + 150 * gy[1] + 29 * gy[2]) >> 8;
        if (!lx && !ly) continue;
        int64_t ux, uy, tmin = 0, tmax = 0;
        unit_q14(lx, ly, &ux, &uy);
        int64_t mx = rdiv(s->sx, s->n), my = rdiv(s->sy, s->n);
        for (int k = 0; k < 4; k++) { /* the box's corners along the gradient */
            int64_t px = 2 * (int64_t) (k & 1 ? b[2] + 1 : b[0]) - mx;
            int64_t py = 2 * (int64_t) (k & 2 ? b[3] + 1 : b[1]) - my;
            int64_t t = (px * ux + py * uy) >> 14;
            if (k == 0 || t < tmin) tmin = t;
            if (k == 0 || t > tmax) tmax = t;
        }
        if (tmax - tmin < 4) continue;
        uint32_t pos = nc;
        if (nc == room) {
            pos = 0;
            for (uint32_t j = 1; j < room; j++)
                if (cg[j] < cg[pos]) pos = j;
            if (cg[pos] >= ex) continue;
        } else {
            nc++;
        }
        cg[pos] = ex, cr[pos] = r;
        for (int e = 0; e < 2; e++) {
            int64_t t = e ? tmax : tmin;
            uint32_t o = 0xFF000000u;
            for (int k = 0; k < 3; k++) {
                int64_t slope = (gx[k] * ux + gy[k] * uy) >> 14;
                int64_t v = rdiv(s->sc[k], s->n) + fdiv(slope * t, 65536);
                o |= (uint32_t) clamp64(v, 0, 255) << (16 - 8 * k);
            }
            col[pos][e] = o;
        }
        const int64_t L = (1 << 30) - 1;
        int64_t ax = mx + ((tmin * ux) >> 14), ay = my + ((tmin * uy) >> 14);
        int64_t bx = mx + ((tmax * ux) >> 14), by = my + ((tmax * uy) >> 14);
        cm[pos][0] = (int32_t) clamp64((bx - ax) * 32768, -L, L);
        cm[pos][1] = (int32_t) clamp64((by - ay) * 32768, -L, L);
        cm[pos][2] = -cm[pos][1], cm[pos][3] = cm[pos][0];
        cm[pos][4] = (int32_t) clamp64(ax * 32768, -L, L);
        cm[pos][5] = (int32_t) clamp64(ay * 32768, -L, L);
    }
    for (uint32_t j = 0; j < nc; j++) {
        uint32_t gi = c->ngrad++;
        for (int k = 0; k < 6; k++) c->gm[gi][k] = cm[j][k];
        c->pal[c->npal + 2 * gi] = col[j][0];
        c->pal[c->npal + 2 * gi + 1] = col[j][1];
        c->gpal[gi][0] = (uint8_t) (c->npal + 2 * gi);
        c->gpal[gi][1] = (uint8_t) (c->npal + 2 * gi + 1);
        c->grad[cr[j]] = gi;
    }
    for (uint32_t r = 0; r < c->R; r++) slot[r] = 0;
    c->ar.used = mark;
}

/* ---- 9: emit ZXVG ---------------------------------------------------------- */
typedef struct {
    uint8_t *p;
    uint32_t n, cap;
    bool full;
    int64_t cx, cy; /* current point, q4 */
} wr_t;

static void w_u8(wr_t *w, uint32_t v)
{
    if (w->n >= w->cap) {
        w->full = true;
        return;
    }
    w->p[w->n++] = (uint8_t) v;
}
static void w_uv(wr_t *w, uint32_t v)
{
    while (v >= 0x80u) {
        w_u8(w, (v & 0x7Fu) | 0x80u);
        v >>= 7;
    }
    w_u8(w, v);
}
static void w_zz(wr_t *w, int64_t v)
{
    int32_t s = (int32_t) clamp64(v, -((1 << 30) - 1), (1 << 30) - 1);
    w_uv(w, s >= 0 ? (uint32_t) s << 1 : ((uint32_t) (-(s + 1)) << 1) | 1u);
}
static void w_pt(wr_t *w, int64_t x, int64_t y) /* relative, updating the current point */
{
    w_zz(w, x - w->cx);
    w_zz(w, y - w->cy);
    w->cx = x, w->cy = y;
}

static void emit_head(ctx_t *c, wr_t *w)
{
    w_u8(w, 'Z'), w_u8(w, 'X'), w_u8(w, 'V'), w_u8(w, 'G'), w_u8(w, 1), w_u8(w, 0);
    w_zz(w, 0), w_zz(w, 0), w_uv(w, c->w * 16u), w_uv(w, c->h * 16u);
    uint32_t np = c->npal + 2u * c->ngrad;
    w_uv(w, np);
    for (uint32_t i = 0; i < np; i++) {
        uint32_t q = c->pal[i];
        w_u8(w, (q >> 16) & 255u), w_u8(w, (q >> 8) & 255u), w_u8(w, q & 255u), w_u8(w, 255);
    }
    w_uv(w, c->ngrad);
    for (uint32_t g = 0; g < c->ngrad; g++) {
        w_u8(w, 1), w_u8(w, 0);
        for (int k = 0; k < 6; k++) w_zz(w, c->gm[g][k]);
        w_u8(w, 2);
        w_u8(w, 0), w_uv(w, c->gpal[g][0]);
        w_u8(w, 255), w_uv(w, c->gpal[g][1]);
    }
    w_uv(w, 0); /* no ids */
}

/* the chain's geometry, forward or reversed (the current point is at its
 * start already); returns its budget cost */
static uint32_t emit_chain(ctx_t *c, wr_t *w, uint32_t q, bool rev)
{
    const int32_t *s = c->seg + c->soff[q], *e = c->seg + c->soff[q + 1];
    uint32_t cost = 0;
    if (!rev) {
        while (s < e) {
            int t = s[0];
            if (t == 1) {
                w_u8(w, 0x02), w_pt(w, s[1], s[2]);
                s += 4, cost += 1;
            } else if (t == 2) {
                w_u8(w, 0x03);
                w_zz(w, s[1] - w->cx), w_zz(w, s[2] - w->cy);
                w_pt(w, s[3], s[4]);
                s += 6, cost += 2;
            } else {
                w_u8(w, 0x04);
                w_zz(w, s[1] - w->cx), w_zz(w, s[2] - w->cy);
                w_zz(w, s[3] - w->cx), w_zz(w, s[4] - w->cy);
                w_pt(w, s[5], s[6]);
                s += 8, cost += 2;
            }
        }
        return cost;
    }
    while (e > s) {
        int t = e[-1];
        const int32_t *r = e - (t == 1 ? 4 : t == 2 ? 6 : 8);
        int64_t px, py; /* the end of the record before r: where this one goes */
        if (r == s)
            px = c->csx[q], py = c->csy[q];
        else
            px = r[-3], py = r[-2];
        if (t == 1) {
            w_u8(w, 0x02), w_pt(w, px, py);
            cost += 1;
        } else if (t == 2) {
            w_u8(w, 0x03);
            w_zz(w, r[1] - w->cx), w_zz(w, r[2] - w->cy);
            w_pt(w, px, py);
            cost += 2;
        } else {
            w_u8(w, 0x04);
            w_zz(w, r[3] - w->cx), w_zz(w, r[4] - w->cy);
            w_zz(w, r[1] - w->cx), w_zz(w, r[2] - w->cy);
            w_pt(w, px, py);
            cost += 2;
        }
        e = r;
    }
    return cost;
}

static bool inside(const ctx_t *c, bool all, uint32_t k, uint32_t l)
{
    if (l == OUT) return false;
    return all || (c->tin[l] >= c->tin[k] && c->tin[l] < c->tout[k]);
}

/* the turning rule at a junction (inside 4-connected): the heading out */
static int turn(const ctx_t *c, bool all, uint32_t k, int32_t x, int32_t y, int d)
{
    /* front-left and front-right pixels for each heading */
    static const int8_t FL[4][2] = {{0, -1}, {0, 0}, {-1, 0}, {-1, -1}};
    static const int8_t FR[4][2] = {{0, 0}, {-1, 0}, {-1, -1}, {0, -1}};
    if (!inside(c, all, k, lab_at(c, x + FL[d][0], y + FL[d][1]))) return (d + 3) & 3;
    if (!inside(c, all, k, lab_at(c, x + FR[d][0], y + FR[d][1]))) return d;
    return (d + 1) & 3;
}

/* one shape: every loop of node k's region set; *cost gets its budget */
static int emit_shape(ctx_t *c, wr_t *w, uint32_t i, uint32_t *cost, zvt_shape_t *sh)
{
    uint32_t k = c->order[i], tag = i + 1u;
    bool all = i == 0;
    uint32_t lo = all ? 0 : c->tin[k], hi = all ? c->R : c->tout[k];
    bool any = false;
    *cost = 0;
    for (uint32_t t = lo; t < hi; t++) {
        uint32_t r = all ? t : c->bytin[t];
        for (uint32_t e = c->cadj0[r]; e < c->cadj0[r + 1]; e++) {
            uint32_t q0 = c->cadj[e];
            if (c->cst[q0] == tag) continue;
            if (inside(c, all, k, other(c, q0, r))) continue;
            bool rev0 = !inside(c, all, k, c->cl[q0]);
            /* trace the loop through q0 */
            int64_t lx = c->csx[q0], ly = c->csy[q0];
            if (rev0 && !(c->cf[q0] & 1u)) {
                int32_t x, y;
                vxy(c, c->pool[c->poff[q0 + 1] - 1], &x, &y);
                lx = (int64_t) x * 16, ly = (int64_t) y * 16;
            }
            if (!any) {
                w_u8(w, 0x07);
                any = true;
                w_u8(w, 0x01);
                w_pt(w, lx, ly);
                if (sh) sh->off = w->n, sh->x0 = (int32_t) lx, sh->y0 = (int32_t) ly;
            } else {
                w_u8(w, 0x01);
                w_pt(w, lx, ly);
            }
            uint32_t q = q0;
            bool rev = rev0;
            for (uint32_t guard = 0;; guard++) {
                if (guard > c->nch) return ZVT_E_CHECK;
                c->cst[q] = tag;
                *cost += emit_chain(c, w, q, rev);
                /* the end vertex and the heading we arrive with */
                uint32_t cf = c->cf[q];
                int32_t ex, ey;
                int d;
                if (!rev) {
                    vxy(c, c->pool[c->poff[q + 1] - 1], &ex, &ey);
                    d = (int) ((cf >> 4) & 3u);
                } else {
                    vxy(c, c->pool[c->poff[q]], &ex, &ey);
                    d = (int) (((cf >> 2) & 3u) + 2u) & 3;
                }
                int nd = turn(c, all, k, ex, ey, d);
                int32_t cid = crack_at(c, ex, ey, nd);
                if (cid < 0 || c->crack[cid] < 0) return ZVT_E_CHECK;
                uint32_t nq = (uint32_t) c->crack[cid] >> 1;
                bool canon = nd == E_ || nd == S_;
                bool nrev = canon != (((uint32_t) c->crack[cid] & 1u) == 0u);
                if (nq == q0 && nrev == rev0) break;
                if (c->cst[nq] == tag) return ZVT_E_CHECK;
                q = nq, rev = nrev;
            }
            w_u8(w, 0x06);
            w->cx = lx, w->cy = ly;
        }
    }
    if (!any) return 0;
    w_u8(w, 0x10), w_u8(w, 0); /* FILL nonzero */
    uint32_t g = c->grad[k];
    if (g != NONE)
        w_uv(w, (g << 1) | 1u);
    else
        w_uv(w, c->rpal[k] << 1);
    w_u8(w, 255);
    if (sh) sh->len = w->n - sh->off, sh->ex = (int32_t) w->cx, sh->ey = (int32_t) w->cy;
    return 1;
}

static int32_t emit(ctx_t *c, uint8_t *out, uint32_t cap, const zvt_extra_t *ex, bool *redo)
{
    wr_t w = {out, 0, cap, false, 0, 0};
    emit_head(c, &w);
    for (uint32_t q = 0; q < c->nch; q++) c->cst[q] = 0;
    uint32_t paths = 0, segs = 0, ns = 0, capb = c->p->pixel_exact ? CAP_LINE : CAP_FIT;
    *redo = false;
    for (uint32_t i = 0; i < c->R; i++) {
        uint32_t cost = 0;
        zvt_shape_t tmp, *sh = ex && ex->shapes && ns < ex->shape_cap ? &ex->shapes[ns] : &tmp;
        uint32_t before = w.n;
        int rc = emit_shape(c, &w, i, &cost, sh);
        if (rc < 0) return rc;
        if (!rc) continue;
        paths++, segs += cost;
        if (ex && ex->shapes && ns < ex->shape_cap) ns++;
        if (cost > capb) {
            /* too detailed for the renderer: refit this shape's chains coarser */
            uint32_t tag = i + 1u;
            for (uint32_t q = 0; q < c->nch; q++)
                if (c->cst[q] == tag && c->lvl[q] < 4) c->lvl[q]++, c->res->refits++;
            *redo = true;
        }
        (void) before;
    }
    w_u8(&w, 0x00);
    if (w.full) return ZVT_E_SPACE;
    c->res->paths = paths;
    c->res->segments = segs;
    c->res->nshapes = ns;
    return (int32_t) w.n;
}

/* ---- 10: fidelity --------------------------------------------------------- */
int32_t zvt_psnr_cdb(uint64_t sse, uint64_t n)
{
    if (!sse) return ZVT_PSNR_INF;
    if (!n) return 0;
    int64_t r = log2_q16(65025u * 3u * n) - log2_q16(sse);
    return (int32_t) fdiv(r * 30103 + 100 * 32768, 100 * 65536);
}

static void win_geom(uint32_t w, uint32_t h, uint32_t *wx, uint32_t *wy, uint32_t *step,
                     uint32_t *nwx, uint32_t *nwy)
{
    *wx = w < 8 ? w : 8, *wy = h < 8 ? h : 8, *step = 4;
    *nwx = (w - *wx) / *step + 1, *nwy = (h - *wy) / *step + 1;
}

uint32_t zvt_fidelity_scratch(uint32_t w, uint32_t h)
{
    uint32_t wx, wy, st, nx, ny;
    if (!w || !h) return 0;
    win_geom(w, h, &wx, &wy, &st, &nx, &ny);
    return h + 5u * nx * ny;
}

int zvt_fidelity(const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                 const zvt_backend_t *be, uint32_t *scratch, zvt_fidelity_t *out)
{
    if (!a || !b || !scratch || !out || !w || !h || w > ZVT_MAX_DIM || h > ZVT_MAX_DIM)
        return ZVT_E_ARG;
    uint32_t wx, wy, st, nx, ny;
    win_geom(w, h, &wx, &wy, &st, &nx, &ny);
    uint32_t *rows = scratch, *win = scratch + h;
    if (!be) be = &zvt_cpu_backend;
    if (!be->metric || be->metric(be->ctx, a, b, w, h, wx, wy, st, rows, win))
        zvt_cpu_metric(a, b, w, h, wx, wy, st, rows, win);
    uint64_t sse = 0;
    for (uint32_t y = 0; y < h; y++) sse += rows[y];
    int64_t n = (int64_t) wx * wy, c1 = (65025 * n * n + 5000) / 10000,
            c2 = (585225 * n * n + 5000) / 10000, sum = 0;
    for (uint32_t i = 0; i < nx * ny; i++) {
        const uint32_t *s = win + 5u * i;
        int64_t sx = s[0], sy = s[1], sxx = s[2], syy = s[3], sxy = s[4];
        int64_t num1 = 2 * sx * sy + c1, num2 = 2 * (n * sxy - sx * sy) + c2;
        int64_t den1 = sx * sx + sy * sy + c1, den2 = n * sxx - sx * sx + n * syy - sy * sy + c2;
        sum += muldiv(muldiv(num1, num2, den1), 10000, den2);
    }
    out->sse = sse;
    out->psnr_cdb = zvt_psnr_cdb(sse, (uint64_t) w * h);
    out->ssim = (int32_t) clamp64(fdiv(sum, (int64_t) nx * ny), -10000, 10000);
    return 0;
}

static uint32_t over(uint32_t q, uint32_t bg) /* straight alpha over an opaque colour */
{
    uint32_t a = q >> 24, o = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        uint32_t s = (q >> sh) & 255u, d = (bg >> sh) & 255u;
        o |= ((s * a + d * (255u - a) + 127u) / 255u) << sh;
    }
    return o;
}

static int render(ctx_t *c, const uint8_t *z, uint32_t n, uint32_t bg)
{
    zui_surface_t s;
    s.px = c->rnd, s.w = (int32_t) c->w, s.h = (int32_t) c->h, s.stride = (int32_t) c->w;
    s.clip = (zui_rect_t){0, 0, (int32_t) c->w, (int32_t) c->h};
    for (uint32_t i = 0; i < c->n; i++) c->rnd[i] = bg & 0xFFFFFFu;
    zui_rect_t box = {0, 0, (int32_t) c->w, (int32_t) c->h};
    if (zui_vec_draw_zxvg(&s, c->vw, z, n, box, 0) < 0) return ZVT_E_CHECK;
    for (uint32_t i = 0; i < c->n; i++) c->wpx[i] = over(c->ref[i], bg);
    return 0;
}

/* ---- provenance ----------------------------------------------------------- */
void zvt_cid(const uint8_t *d, uint32_t n, uint8_t out[ZVT_CID_LEN])
{
    out[0] = 0x01, out[1] = 0x55, out[2] = 0x12, out[3] = 0x20;
    sha256(d, n, out + 4);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v, p[1] = (uint8_t) (v >> 8), p[2] = (uint8_t) (v >> 16),
    p[3] = (uint8_t) (v >> 24);
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

void zvt_record_write(const zvt_result_t *r, const uint8_t *zxvg, uint32_t zxvg_len,
                      uint8_t out[ZVT_REC_LEN])
{
    mzero(out, ZVT_REC_LEN);
    out[0] = 'Z', out[1] = 'X', out[2] = 'V', out[3] = 'T', out[4] = 1;
    out[5] = (uint8_t) ((r->exact ? 1u : 0u) | (r->cid_of_pixels ? 2u : 0u));
    out[6] = (uint8_t) r->preset;
    put32(out + 8, r->w), put32(out + 12, r->h);
    mcopy(out + 16, r->cid, ZVT_CID_LEN);
    sha256(zxvg, zxvg_len, out + 52);
    put32(out + 84, (uint32_t) r->psnr_cdb), put32(out + 88, (uint32_t) r->ssim);
    put32(out + 92, r->paths), put32(out + 96, r->colours), put32(out + 100, zxvg_len);
    uint8_t h[32];
    sha256(out, 104, h);
    mcopy(out + 104, h, 8);
}

int zvt_record_read(const uint8_t *d, uint32_t n, const uint8_t *zxvg, uint32_t zxvg_len,
                    zvt_record_t *o)
{
    if (!d || !o || n != ZVT_REC_LEN) return ZVT_E_DATA;
    if (d[0] != 'Z' || d[1] != 'X' || d[2] != 'V' || d[3] != 'T' || d[4] != 1 || (d[5] & ~3u) ||
        d[6] >= ZVT_PRESET_COUNT || d[7])
        return ZVT_E_DATA;
    uint8_t h[32];
    sha256(d, 104, h);
    for (int i = 0; i < 8; i++)
        if (h[i] != d[104 + i]) return ZVT_E_DATA;
    if (d[16] != 0x01 || d[17] != 0x55 || d[18] != 0x12 || d[19] != 0x20) return ZVT_E_DATA;
    o->exact = d[5] & 1u, o->cid_of_pixels = (d[5] & 2u) != 0;
    o->preset = (zvt_preset_t) d[6];
    o->w = get32(d + 8), o->h = get32(d + 12);
    mcopy(o->cid, d + 16, ZVT_CID_LEN);
    mcopy(o->zxvg_sha256, d + 52, 32);
    o->psnr_cdb = (int32_t) get32(d + 84), o->ssim = (int32_t) get32(d + 88);
    o->paths = get32(d + 92), o->colours = get32(d + 96), o->zxvg_bytes = get32(d + 100);
    if (!o->w || !o->h || o->w > ZVT_MAX_DIM || o->h > ZVT_MAX_DIM || o->colours > 256 ||
        o->ssim < -10000 || o->ssim > 10000 || (o->exact && o->psnr_cdb != ZVT_PSNR_INF))
        return ZVT_E_DATA;
    if (zxvg) {
        if (zxvg_len != o->zxvg_bytes) return ZVT_E_DATA;
        sha256(zxvg, zxvg_len, h);
        for (int i = 0; i < 32; i++)
            if (h[i] != o->zxvg_sha256[i]) return ZVT_E_DATA;
    }
    return 0;
}

/* ---- sizes ------------------------------------------------------------------ */
static uint64_t al8(uint64_t v)
{
    return (v + 7u) & ~(uint64_t) 7u;
}

uint64_t zvt_work_size(uint32_t w, uint32_t h)
{
    if (!w || !h || w > ZVT_MAX_DIM || h > ZVT_MAX_DIM || (uint64_t) w * h > ZVT_MAX_PIXELS)
        return 0;
    uint64_t n = (uint64_t) w * h, nc = (uint64_t) (h + 1) * w + (uint64_t) h * (w + 1);
    uint64_t s = al8(sizeof(zui_vec_work_t)) + 3 * al8(n * 4) + 2 * al8(n) + al8(n * 4);
    s += 11 * al8((n + 1) * 4); /* region arrays (R <= n)            */
    s += al8(nc * 4);           /* crack -> chain                    */
    /* chains (A <= nc): offsets, pool, sides, flags, level, seg offsets    */
    s += al8((nc + 1) * 4) + al8((nc * 2 + 1) * 4) + 2 * al8(nc * 4 + 4) + 2 * al8(nc + 1);
    s += al8((nc + 1) * 4);
    s += al8((n + 1) * 4) + al8(nc * 8 + 4); /* chains per region             */
    /* fit scratch: points (2L + 4) x (x, y, t, flag) and a split stack     */
    uint64_t pts = 2 * nc + 8;
    s += 3 * al8(pts * 4) + al8(pts) + al8((4 * nc + 32) * 4);
    s += al8((nc * 12 + 64) * 4); /* segments                            */
    /* phase scratch, the larger of: merge (5 n + adjacency 4 n), forest
     * (n + 2 nc heap, 2 n), palette histogram, gradients */
    uint64_t ph1 = 6 * al8((n + 1) * 4) + al8(4 * n * 4 + 4) + 65536;
    uint64_t ph2 = al8((n + 2 * nc + 1) * 4) + 2 * al8(n * 4 + 4);
    uint64_t ph3 = 32768u * 4u + 64;
    uint64_t ph4 = (n / GRAD_MIN + 1) * (sizeof(gst_t) + 16) + 64;
    uint64_t ph = ph1 > ph2 ? ph1 : ph2;
    if (ph3 > ph) ph = ph3;
    if (ph4 > ph) ph = ph4;
    return s + ph + 256;
}

uint64_t zvt_out_max(uint32_t w, uint32_t h)
{
    /* enough for every picture met in testing by a wide margin (the worst
     * case, a chain emitted once per tree level on both sides, is far larger
     * and only reachable by adversarial pictures): on ZVT_E_SPACE, double it */
    return 4096u + 256u * 4u + 64u * (uint64_t) w * h;
}

/* ---- the trace ---------------------------------------------------------------- */
static int carve(ctx_t *c)
{
    uint64_t n = c->n;
    c->vw = (zui_vec_work_t *) ar_get(&c->ar, sizeof(zui_vec_work_t));
    c->ref = (uint32_t *) ar_get(&c->ar, n * 4);
    c->wpx = (uint32_t *) ar_get(&c->ar, n * 4);
    c->rnd = (uint32_t *) ar_get(&c->ar, n * 4);
    c->idx = (uint8_t *) ar_get(&c->ar, n);
    c->edge = (uint8_t *) ar_get(&c->ar, n);
    c->lab = (uint32_t *) ar_get(&c->ar, n * 4);
    uint32_t **ra[] = {&c->area,  &c->rpal,  &c->par,   &c->depth, &c->tin, &c->tout,
                       &c->order, &c->bytin, &c->stamp, &c->acc,   &c->grad};
    for (unsigned i = 0; i < sizeof ra / sizeof ra[0]; i++)
        *ra[i] = (uint32_t *) ar_get(&c->ar, (n + 1) * 4);
    c->crack = (int32_t *) ar_get(&c->ar, (uint64_t) c->nc * 4);
    return c->ar.full ? ZVT_E_SPACE : 0;
}

int32_t zvt_trace(const uint32_t *px, uint32_t w, uint32_t h, uint32_t stride, const uint8_t *src,
                  uint32_t src_len, const zvt_params_t *p, const zvt_backend_t *be,
                  const zvt_extra_t *extra, void *work, uint64_t worklen, uint8_t *out,
                  uint32_t outcap, zvt_result_t *res)
{
    if (!px || !p || !work || !out || !res || stride < w) return ZVT_E_ARG;
    if (!w || !h || w > ZVT_MAX_DIM || h > ZVT_MAX_DIM || (uint64_t) w * h > ZVT_MAX_PIXELS)
        return ZVT_E_SIZE;
    if (extra && extra->pal && (extra->npal < 1 || extra->npal > 256)) return ZVT_E_ARG;
    mzero(res, sizeof *res);
    ctx_t cx, *c = &cx;
    mzero(c, sizeof *c);
    c->w = w, c->h = h, c->n = w * h, c->p = p, c->be = be ? be : &zvt_cpu_backend, c->res = res;
    c->hc = (h + 1) * w, c->nc = c->hc + h * (w + 1);
    c->ar.base = (uint8_t *) work, c->ar.cap = worklen;
    res->w = w, res->h = h, res->preset = p->preset;
    if (carve(c)) return ZVT_E_SPACE;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t q = px[(uint64_t) y * stride + x];
            c->ref[y * w + x] = q;
            if ((q >> 24) < 128u) c->transp = true;
        }
    if (src) {
        zvt_cid(src, src_len, res->cid);
    } else {
        /* the pixels as little-endian bytes: hash ref in place */
        uint8_t *b = (uint8_t *) c->rnd;
        uint32_t *tmp = c->rnd;
        for (uint32_t i = 0; i < c->n; i++) {
            uint32_t q = c->ref[i];
            uint8_t *o = b + 4u * i;
            o[0] = (uint8_t) q, o[1] = (uint8_t) (q >> 8), o[2] = (uint8_t) (q >> 16),
            o[3] = (uint8_t) (q >> 24);
        }
        (void) tmp;
        zvt_cid(b, c->n * 4u, res->cid);
        res->cid_of_pixels = true;
    }
    /* 2: smoothing */
    mcopy(c->wpx, c->ref, (uint64_t) c->n * 4);
    if (!p->pixel_exact)
        for (uint32_t i = 0; i < p->smooth && i < 4; i++) {
            if (!c->be->smooth || c->be->smooth(c->be->ctx, c->wpx, c->rnd, w, h, p->smooth_thr)) {
                if (c->be != &zvt_cpu_backend) res->fallbacks++;
                zvt_cpu_smooth(c->wpx, c->rnd, w, h, p->smooth_thr);
            }
            mcopy(c->wpx, c->rnd, (uint64_t) c->n * 4);
        }
    /* 3: the palette */
    uint32_t want = p->colours < 2 ? 2 : p->colours > 256 ? 256 : p->colours;
    if (c->transp && want > 255) want = 255;
    if (extra && extra->pal) {
        c->npal = extra->npal > want ? want : extra->npal;
        for (uint32_t i = 0; i < c->npal; i++) c->pal[i] = extra->pal[i] | 0xFF000000u;
    } else if (p->pixel_exact && exact_palette(c, c->pal, &c->npal)) {
        res->exact_possible = true;
    } else {
        uint64_t mark = c->ar.used;
        c->npal = median_cut(c, c->wpx, want, c->pal);
        c->ar.used = mark;
        if (c->ar.full) return ZVT_E_SPACE;
        if (!c->npal) c->pal[0] = 0xFF000000u | p->bg, c->npal = 1;
        kmeans(c, c->wpx, c->pal, c->npal, p->kmeans > 16 ? 16 : p->kmeans);
    }
    be_quant(c, c->wpx, c->pal, c->npal);
    res->npal = c->npal;
    for (uint32_t i = 0; i < c->npal; i++) res->pal[i] = c->pal[i];
    uint64_t base = c->ar.used;
    uint32_t min_area = p->pixel_exact ? 1u : p->min_area > c->n ? c->n : p->min_area;
    int32_t len = 0;
    for (uint32_t attempt = 0; attempt < 4; attempt++) {
        c->ar.used = base;
        c->ar.full = false;
        fill_specks(c, min_area);
        label(c);
        region_stats(c);
        int rc = merge_small(c, min_area);
        if (rc) return rc;
        if (p->absorb_thin && !p->pixel_exact && absorb_thin(c)) {
            label(c);
            region_stats(c);
            if ((rc = merge_small(c, min_area))) return rc;
        }
        res->regions = c->R;
        if ((rc = chains(c))) return rc;
        res->chains = c->nch;
        if ((rc = forest(c))) return rc;
        gradients(c);
        res->gradients = c->ngrad;
        c->soff = (uint32_t *) ar_get(&c->ar, ((uint64_t) c->nch + 1) * 4u);
        uint64_t pts = 2ull * c->lmax + 8u;
        c->qx = (int32_t *) ar_get(&c->ar, pts * 4u);
        c->qy = (int32_t *) ar_get(&c->ar, pts * 4u);
        c->qt = (int32_t *) ar_get(&c->ar, pts * 4u);
        c->qa = (uint8_t *) ar_get(&c->ar, pts);
        c->stk = (int32_t *) ar_get(&c->ar, (4ull * c->lmax + 32u) * 4u);
        if (c->ar.full) return ZVT_E_SPACE;
        /* the segment pool takes what is left, up to its worst case */
        uint64_t left = c->ar.cap - ((c->ar.used + 7u) & ~(uint64_t) 7u);
        if (c->ar.used + 8u > c->ar.cap) return ZVT_E_SPACE;
        uint64_t worst = ((uint64_t) c->A * 12u + 64u) * 4u;
        c->segcap = (left < worst ? left : worst) / 4u;
        c->seg = (int32_t *) ar_get(&c->ar, c->segcap * 4u);
        if (!c->seg) return ZVT_E_SPACE;
        bool redo = true;
        for (uint32_t pass = 0; redo && pass <= MAX_REFIT; pass++) {
            if ((rc = fit_all(c))) return rc;
            len = emit(c, out, outcap, extra, &redo);
            if (len < 0) return len;
        }
        if (!redo) break;
        if (attempt == 3) return ZVT_E_LIMIT;
        min_area = min_area < 1 ? 4 : min_area * 4u; /* fewer, larger regions */
        res->exact_possible = false;
    }
    if (zui_zxvg_check(out, (uint32_t) len) < 0) return ZVT_E_CHECK;
    res->bytes = (uint32_t) len;
    res->colours = c->npal;
    /* 10: measure, and decide EXACT honestly */
    uint32_t fs = zvt_fidelity_scratch(w, h);
    uint32_t *scr = (uint32_t *) ar_get(&c->ar, (uint64_t) fs * 4u);
    if (!scr) {
        /* reuse the label map: no longer needed */
        if ((uint64_t) fs > c->n) return ZVT_E_SPACE;
        scr = c->lab;
    }
    if (render(c, out, (uint32_t) len, p->bg) < 0) return ZVT_E_CHECK;
    zvt_fidelity_t f;
    zvt_fidelity(c->rnd, c->wpx, w, h, c->be, scr, &f);
    res->psnr_cdb = f.psnr_cdb;
    res->ssim = f.ssim;
    bool exact = f.sse == 0;
    if (exact && c->transp) {
        uint32_t bg2 = (p->bg ^ 0xFFFFFFu) & 0xFFFFFFu;
        if (render(c, out, (uint32_t) len, bg2) < 0) return ZVT_E_CHECK;
        for (uint32_t i = 0; i < c->n && exact; i++) exact = c->rnd[i] == c->wpx[i];
    }
    res->exact = exact;
    return len;
}
