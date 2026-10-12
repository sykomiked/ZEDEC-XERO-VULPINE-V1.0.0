/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_nn.c — integer encoder blocks (T23). See zt_nn.h. */
#include "zt_nn.h"
#include "zt_kern.h"
#include "zt_nn_tables.h"

#define LIM58 ((int64_t) 1 << 58)
#define LIM62 ((int64_t) 1 << 62)

/* ---- small helpers ---- */

static uint32_t bitlen64(uint64_t v)
{
    uint32_t n = 0;
    while (v) {
        v >>= 1;
        n++;
    }
    return n;
}

static zt_fx sat32(int64_t v)
{
    return (zt_fx) (v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}

static uint32_t uabs32(int32_t v)
{
    return v < 0 ? (uint32_t) 0 - (uint32_t) v : (uint32_t) v;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* (a * b) / 2^sh, truncated toward zero, clamped to +-2^58 (as zt_model.c) */
static int64_t mul_shr(int64_t a, uint32_t b, uint32_t sh)
{
    bool neg = a < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) a : (uint64_t) a;
    uint64_t hi = (u >> 32) * b, lo = (uint64_t) (uint32_t) u * b, r;
    if (sh >= 32) {
        uint64_t t = hi + (lo >> 32);
        r = sh - 32 >= 64 ? 0 : t >> (sh - 32);
    } else if (hi >> (26 + sh) || (lo >> sh) > (uint64_t) LIM58) {
        r = (uint64_t) LIM58;
    } else {
        r = (hi << (32 - sh)) + (lo >> sh);
    }
    if (r > (uint64_t) LIM58) r = (uint64_t) LIM58;
    return neg ? -(int64_t) r : (int64_t) r;
}

/* round(n / d) for d > 0, halves away from zero */
static int64_t round_div(int64_t n, uint64_t d)
{
    bool neg = n < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t q = zt_udiv64(u + (d >> 1), d, 0);
    return neg ? -(int64_t) q : (int64_t) q;
}

int64_t zt_nn_f32_fixed(uint32_t bits, int32_t frac)
{
    uint32_t ex = (bits >> 23) & 0xFF;
    uint64_t m = bits & 0x7FFFFFu, r;
    bool neg = bits >> 31;
    int32_t e;
    if (ex == 0xFF) return m ? 0 : neg ? -LIM62 : LIM62;
    if (ex == 0) {
        e = -149;
    } else {
        m |= 0x800000u;
        e = (int32_t) ex - 150;
    }
    if (m == 0) return 0;
    int32_t sh = e + frac;
    if (sh >= 0) {
        r = sh >= 38 ? (uint64_t) LIM62 : m << sh;
        if (r > (uint64_t) LIM62) r = (uint64_t) LIM62;
    } else {
        uint32_t s = (uint32_t) -sh;
        r = s > 62 ? 0 : (m + ((1ull << s) >> 1)) >> s;
    }
    return neg ? -(int64_t) r : (int64_t) r;
}

/* f16 / bf16 bits to the f32 bits of the same value (exact) */
static uint32_t f16_to_f32(uint16_t h)
{
    uint32_t s = (uint32_t) (h >> 15) << 31, e = (h >> 10) & 31, m = h & 0x3FF;
    if (e == 31) return s | 0x7F800000u | (m << 13);
    if (e == 0) {
        if (!m) return s;
        e = 127 - 15 + 1;
        while (!(m & 0x400)) {
            m <<= 1;
            e--;
        }
        return s | (e << 23) | ((m & 0x3FF) << 13);
    }
    return s | ((e + 127 - 15) << 23) | (m << 13);
}

static void set_err(zt_model_err_t *err, int32_t code, const char *what, const char *name)
{
    if (!err) return;
    err->code = code;
    err->what = what;
    uint32_t i = 0;
    if (name)
        for (; name[i] && i < sizeof err->name - 1; i++) err->name[i] = name[i];
    err->name[i] = 0;
}

/* ---- arena ---- */

void zt_nn_arena_begin(zt_nn_arena_t *a, void *base, uint64_t cap, uint32_t cols_max,
                       zt_model_err_t *err)
{
    uint64_t pad = base ? (64 - ((uintptr_t) base & 63)) & 63 : 0;
    a->base = base ? (uint8_t *) base + pad : 0;
    a->cap = base ? (cap > pad ? cap - pad : 0) : 0;
    a->used = 0;
    a->err = err;
    a->cols_max = (cols_max + 31) & ~31u;
    a->scratch = zt_nn_take(a, (uint64_t) a->cols_max * 4);
}

uint64_t zt_nn_arena_used(const zt_nn_arena_t *a)
{
    return a->used + 64;
}

bool zt_nn_filling(const zt_nn_arena_t *a)
{
    return a->base != 0;
}

void *zt_nn_take(zt_nn_arena_t *a, uint64_t bytes)
{
    a->used = (a->used + 63) & ~63ull;
    void *p = 0;
    if (a->base && a->used + bytes <= a->cap) p = a->base + a->used;
    a->used += bytes;
    return p;
}

/* ---- tensors ---- */

bool zt_nn_has(const zt_gguf_t *g, const char *name)
{
    zt_gguf_tensor_t t;
    return zt_gguf_find_tensor(g, name, &t) == ZT_GGUF_OK;
}

int32_t zt_nn_tensor(const zt_gguf_t *g, const char *name, uint64_t cols, uint64_t rows,
                     zt_gguf_tensor_t *t, zt_model_err_t *err)
{
    if (zt_gguf_find_tensor(g, name, t) != ZT_GGUF_OK) {
        set_err(err, ZT_NN_EMISSING, "missing tensor", name);
        return ZT_NN_EMISSING;
    }
    uint64_t lead = 1, last = t->dims[t->n_dims - 1];
    for (uint32_t i = 0; i + 1 < t->n_dims; i++) lead *= t->dims[i];
    bool ok = rows ? (t->n_dims >= 2 && lead == cols && last == rows) : t->n_elems == cols;
    if (!ok) {
        set_err(err, ZT_NN_ESHAPE, "tensor has the wrong shape", name);
        return ZT_NN_ESHAPE;
    }
    return ZT_GGUF_OK;
}

/* 32 Q16 values to int8 with value = q * scale >> shift (zt_model.c's requant) */
static void requant(const zt_fx *x, int8_t *q, int32_t *scale, uint8_t *shift)
{
    uint32_t amax = 0;
    for (uint32_t i = 0; i < 32; i++)
        if (uabs32(x[i]) > amax) amax = uabs32(x[i]);
    if (!amax) {
        *scale = 0;
        *shift = 0;
        for (uint32_t i = 0; i < 32; i++) q[i] = 0;
        return;
    }
    uint32_t sh = 0;
    while (((uint64_t) amax << sh) < 0x80000000ull) sh++;
    uint32_t A = (uint32_t) ((uint64_t) amax << sh);
    uint32_t sc = A / 127u + (A % 127u >= 64u);
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t num = (uint32_t) ((uint64_t) uabs32(x[i]) << sh);
        uint32_t qq = num / sc, rem = num % sc;
        if (2 * rem >= sc) qq++;
        if (qq > 127) qq = 127;
        q[i] = (int8_t) (x[i] < 0 ? -(int32_t) qq : (int32_t) qq);
    }
    *scale = (int32_t) sc;
    *shift = (uint8_t) sh;
}

int32_t zt_nn_load_mat(zt_nn_arena_t *a, const zt_gguf_t *g, const char *name, uint32_t cols,
                       uint32_t rows, zt_mat_t *out)
{
    zt_gguf_tensor_t t;
    int32_t r = zt_nn_tensor(g, name, cols, rows, &t, a->err);
    if (r) return r;
    zt_mat_t m = {0, 0, rows, (cols + 31) & ~31u, 0};
    if (t.type == ZT_GGML_Q8_0 && cols % 32 == 0) {
        m.raw = t.data;
        m.qtype = ZT_GGML_Q8_0;
    } else if ((t.type == ZT_GGML_Q4_K || t.type == ZT_GGML_Q6_K) && cols % 256 == 0) {
        m.raw = t.data;
        m.qtype = t.type;
    } else {
        uint32_t blk = zt_ggml_block(t.type);
        if (!blk || cols % blk || m.cols > a->cols_max) {
            set_err(a->err, ZT_GGUF_EUNSUPPORTED, "unsupported tensor type or row length", name);
            return ZT_GGUF_EUNSUPPORTED;
        }
        uint32_t nb = m.cols / 32;
        zt_q8_t *q = zt_nn_take(a, (uint64_t) rows * nb * sizeof(zt_q8_t));
        if (zt_nn_filling(a)) {
            if (!q) {
                set_err(a->err, ZT_NN_ESPACE, "arena too small", name);
                return ZT_NN_ESPACE;
            }
            for (uint32_t row = 0; row < rows; row++) {
                for (uint32_t i = cols; i < m.cols; i++) a->scratch[i] = 0;
                r = zt_gguf_dequant(&t, (uint64_t) row * cols, cols, a->scratch);
                if (r) {
                    set_err(a->err, r, "cannot decode tensor", name);
                    return r;
                }
                zt_q8_t *o = q + (uint64_t) row * nb;
                for (uint32_t b = 0; b < nb; b++) {
                    o[b].phi_k = 0;
                    requant(a->scratch + b * 32, o[b].q, &o[b].scale, &o[b].shift);
                }
            }
        }
        m.q8 = q;
    }
    if (out) *out = m;
    return ZT_GGUF_OK;
}

int32_t zt_nn_load_q16(zt_nn_arena_t *a, const zt_gguf_t *g, const char *name, uint64_t n,
                       const zt_fx **out)
{
    zt_gguf_tensor_t t;
    int32_t r = zt_nn_tensor(g, name, n, 0, &t, a->err);
    if (r) return r;
    zt_fx *v = zt_nn_take(a, n * 4);
    if (!zt_nn_filling(a)) return ZT_GGUF_OK;
    if (!v) {
        set_err(a->err, ZT_NN_ESPACE, "arena too small", name);
        return ZT_NN_ESPACE;
    }
    r = zt_gguf_dequant(&t, 0, n, v);
    if (r) {
        set_err(a->err, r, "cannot decode tensor", name);
        return r;
    }
    *out = v;
    return ZT_GGUF_OK;
}

static bool elem_f32(const zt_gguf_tensor_t *t, uint64_t i, uint32_t *bits)
{
    if (t->type == ZT_GGML_F32) {
        *bits = le32(t->data + 4 * i);
        return true;
    }
    if (t->type == ZT_GGML_F16 || t->type == ZT_GGML_BF16) {
        uint16_t h = (uint16_t) (t->data[2 * i] | (t->data[2 * i + 1] << 8));
        *bits = t->type == ZT_GGML_F16 ? f16_to_f32(h) : (uint32_t) h << 16;
        return true;
    }
    return false;
}

int32_t zt_nn_load_gain(zt_nn_arena_t *a, const zt_gguf_t *g, const char *name, uint64_t n,
                        zt_gain_t *out)
{
    zt_gguf_tensor_t t;
    int32_t r = zt_nn_tensor(g, name, n, 0, &t, a->err);
    if (r) return r;
    int32_t *gv = zt_nn_take(a, n * 4);
    if (!zt_nn_filling(a)) return ZT_GGUF_OK;
    if (!gv) {
        set_err(a->err, ZT_NN_ESPACE, "arena too small", name);
        return ZT_NN_ESPACE;
    }
    uint32_t bits;
    if (elem_f32(&t, 0, &bits)) {
        int64_t mx = 0;
        for (uint64_t i = 0; i < n; i++) {
            elem_f32(&t, i, &bits);
            int64_t x = zt_nn_f32_fixed(bits, 24);
            if (x < 0) x = -x;
            if (x > mx) mx = x;
        }
        int32_t frac = 24;
        while (frac > 0 && mx > INT32_MAX) {
            frac--;
            mx >>= 1;
        }
        for (uint64_t i = 0; i < n; i++) {
            elem_f32(&t, i, &bits);
            gv[i] = sat32(zt_nn_f32_fixed(bits, frac));
        }
        out->frac = (uint32_t) frac;
    } else {
        r = zt_gguf_dequant(&t, 0, n, gv);
        if (r) {
            set_err(a->err, r, "unsupported tensor type", name);
            return r;
        }
        out->frac = 16;
    }
    out->g = gv;
    return ZT_GGUF_OK;
}

uint32_t zt_nn_f32_key(const zt_gguf_t *g, const char *key, uint32_t def)
{
    zt_gguf_val_t v;
    if (zt_gguf_find(g, key, &v) != ZT_GGUF_OK) return def;
    if (v.type == ZT_GGUF_F32) return (uint32_t) v.u;
    return def;
}

/* ---- context ---- */

typedef struct {
    uint8_t *base;
    uint64_t used;
} lay_t;

static void *lay(lay_t *l, uint64_t bytes)
{
    l->used = (l->used + 63) & ~63ull;
    void *p = l->base ? l->base + l->used : 0;
    l->used += bytes;
    return p;
}

static uint64_t nn_layout(zt_nn_t *nn, uint8_t *base, uint32_t T, uint32_t C, uint32_t K,
                          uint32_t hd, uint32_t heads)
{
    lay_t l = {base, 0};
    uint32_t nb = C / 32, ng = (T + ZT_MMB_G - 1) / ZT_MMB_G, items = heads * ZT_NN_ATT_TILES;
    nn->a16 = lay(&l, (uint64_t) T * C * 2);
    nn->ash = lay(&l, (uint64_t) T * nb);
    nn->asum = lay(&l, (uint64_t) T * nb * 4);
    nn->aT = lay(&l, (uint64_t) ng * ZT_MMB_G * C * 2);
    nn->ashT = lay(&l, (uint64_t) ng * nb * ZT_MMB_G * 8);
    nn->asumT = lay(&l, (uint64_t) ng * nb * ZT_MMB_G * 8);
    nn->ashx = lay(&l, (uint64_t) ng * nb);
    nn->scores = lay(&l, (uint64_t) items * K * 4);
    nn->acc = lay(&l, (uint64_t) items * hd * 8);
    nn->qs = lay(&l, (uint64_t) items * hd * 4);
    nn->items = items;
    return l.used + 64;
}

uint64_t zt_nn_bytes(uint32_t max_tok, uint32_t max_cols, uint32_t max_keys, uint32_t max_hd,
                     uint32_t max_heads)
{
    zt_nn_t nn;
    if (!max_tok || !max_cols) return 0;
    return nn_layout(&nn, 0, max_tok, (max_cols + 31) & ~31u, max_keys ? max_keys : 1,
                     max_hd ? max_hd : 1, max_heads ? max_heads : 1);
}

int32_t zt_nn_init(zt_nn_t *nn, uint32_t max_tok, uint32_t max_cols, uint32_t max_keys,
                   uint32_t max_hd, uint32_t max_heads, void *mem, uint64_t bytes)
{
    if (!nn || !mem || bytes < zt_nn_bytes(max_tok, max_cols, max_keys, max_hd, max_heads))
        return ZT_NN_ESPACE;
    uint8_t *base = (uint8_t *) mem + ((64 - ((uintptr_t) mem & 63)) & 63);
    nn->kern = 0;
    nn->max_tok = max_tok;
    nn->max_cols = (max_cols + 31) & ~31u;
    nn->max_keys = max_keys ? max_keys : 1;
    nn->max_hd = max_hd ? max_hd : 1;
    nn_layout(nn, base, max_tok, nn->max_cols, nn->max_keys, nn->max_hd, max_heads ? max_heads : 1);
    nn->n_sat = 0;
    return ZT_GGUF_OK;
}

static void par(const zt_nn_t *nn, zt_rows_fn fn, void *ctx, uint32_t n, uint64_t work)
{
    const zt_kern_t *k = nn->kern;
    if (k && k->par_rows)
        k->par_rows(k->pctx, fn, ctx, n, work);
    else
        fn(ctx, 0, n);
}

/* ---- matrix products ---- */

/* Q16 row (n_in values, then zeros up to n) to 16-bit blocks of 32 with a
 * power-of-two exponent and each block's exact sum: zt_model.c's act_quant. */
static void act_quant(const zt_fx *x, uint32_t n_in, uint32_t n, int16_t *q, uint8_t *sh,
                      int32_t *qsum)
{
    for (uint32_t b = 0; b < n / 32; b++) {
        uint32_t amax = 0, i0 = b * 32;
        for (uint32_t i = 0; i < 32; i++)
            if (i0 + i < n_in && uabs32(x[i0 + i]) > amax) amax = uabs32(x[i0 + i]);
        uint32_t s = 0;
        while (s < 17 && (((uint64_t) amax + ((1ull << s) >> 1)) >> s) > 32767) s++;
        uint32_t half = (1u << s) >> 1;
        int32_t t = 0;
        for (uint32_t i = 0; i < 32; i++) {
            zt_fx v = i0 + i < n_in ? x[i0 + i] : 0;
            uint32_t r = (uint32_t) (((uint64_t) uabs32(v) + half) >> s);
            q[i0 + i] = (int16_t) (v < 0 ? -(int32_t) r : (int32_t) r);
            t += q[i0 + i];
        }
        sh[b] = (uint8_t) s;
        qsum[b] = t;
    }
}

typedef struct {
    zt_nn_t *nn;
    const zt_mat_t *w;
    const zt_fx *bias, *x;
    uint32_t xstride, n_in, B, ystride;
    zt_fx *y;
} lin_job_t;

static void act_rows(void *ctx, uint32_t b0, uint32_t b1)
{
    const lin_job_t *j = ctx;
    uint32_t C = j->w->cols, nb = C / 32;
    for (uint32_t b = b0; b < b1; b++)
        act_quant(j->x + (uint64_t) b * j->xstride, j->n_in, C, j->nn->a16 + (uint64_t) b * C,
                  j->nn->ash + (uint64_t) b * nb, j->nn->asum + (uint64_t) b * nb);
}

static void pack_rows(void *ctx, uint32_t g0, uint32_t g1)
{
    const lin_job_t *j = ctx;
    zt_nn_t *nn = j->nn;
    uint32_t C = j->w->cols, nb = C / 32;
    for (uint32_t g = g0; g < g1; g++) {
        uint32_t t0 = g * ZT_MMB_G, nt = j->B - t0 < ZT_MMB_G ? j->B - t0 : ZT_MMB_G;
        uint64_t gb = (uint64_t) g * nb;
        zt_mmb_pack(nn->a16 + (uint64_t) t0 * C, nn->ash + (uint64_t) t0 * nb,
                    nn->asum + (uint64_t) t0 * nb, C, nt, C, nn->aT + gb * 32 * ZT_MMB_G,
                    nn->ashT + gb * ZT_MMB_G, nn->asumT + gb * ZT_MMB_G, nn->ashx + gb);
    }
}

static void lin_rows(void *ctx, uint32_t r0, uint32_t r1)
{
    const lin_job_t *j = ctx;
    const zt_mat_t *w = j->w;
    const zt_kern_t *k = j->nn->kern;
    uint32_t nb = w->cols / 32, nsat = 0;
    for (uint32_t r = r0; r < r1; r++) {
        int64_t bv = j->bias ? j->bias[r] : 0;
        for (uint32_t b = 0; b < j->B; b++) {
            const int16_t *a = j->nn->a16 + (uint64_t) b * w->cols;
            const uint8_t *ash = j->nn->ash + (uint64_t) b * nb;
            const int32_t *asum = j->nn->asum + (uint64_t) b * nb;
            zt_fx d;
            if (w->qtype == ZT_GGML_Q4_K) {
                const uint8_t *row = w->raw + (uint64_t) r * (nb / 8) * ZT_Q4K_BYTES;
                d = k && k->dot_q4k ? k->dot_q4k(row, a, ash, asum, nb)
                                    : zt_dot_q4k_c(row, a, ash, asum, nb);
            } else if (w->qtype == ZT_GGML_Q6_K) {
                const uint8_t *row = w->raw + (uint64_t) r * (nb / 8) * ZT_Q6K_BYTES;
                d = k && k->dot_q6k ? k->dot_q6k(row, a, ash, asum, nb)
                                    : zt_dot_q6k_c(row, a, ash, asum, nb);
            } else if (w->raw) {
                const uint8_t *row = w->raw + (uint64_t) r * nb * 34;
                d = k && k->dot_raw ? k->dot_raw(row, a, ash, nb) : zt_dot_raw_c(row, a, ash, nb);
            } else {
                const zt_q8_t *row = w->q8 + (uint64_t) r * nb;
                d = k && k->dot_q8 ? k->dot_q8(row, a, ash, nb) : zt_dot_q8_c(row, a, ash, nb);
            }
            int64_t v = (int64_t) d + bv;
            nsat += v >= INT32_MAX || v <= INT32_MIN;
            j->y[(uint64_t) b * j->ystride + r] = sat32(v);
        }
    }
    if (nsat) __atomic_fetch_add(&j->nn->n_sat, nsat, __ATOMIC_RELAXED);
}

typedef struct {
    lin_job_t j;
    zt_mmb_t b;
} linb_job_t;

static void linb_rows(void *ctx, uint32_t r0, uint32_t r1)
{
    const linb_job_t *jb = ctx;
    const lin_job_t *j = &jb->j;
    if (j->nn->kern->mm_batch(&jb->b, r0, r1)) {
        lin_rows((void *) j, r0, r1);
        return;
    }
    uint32_t nsat = 0;
    for (uint32_t b = 0; b < j->B; b++) {
        zt_fx *y = j->y + (uint64_t) b * j->ystride;
        for (uint32_t r = r0; r < r1; r++) {
            int64_t v = (int64_t) y[r] + (j->bias ? j->bias[r] : 0);
            nsat += v >= INT32_MAX || v <= INT32_MIN;
            y[r] = sat32(v);
        }
    }
    if (nsat) __atomic_fetch_add(&j->nn->n_sat, nsat, __ATOMIC_RELAXED);
}

void zt_nn_linear(zt_nn_t *nn, const zt_mat_t *w, const zt_fx *bias, const zt_fx *x,
                  uint32_t xstride, uint32_t n_in, uint32_t T, zt_fx *y, uint32_t ystride)
{
    if (w->cols > nn->max_cols || n_in > w->cols) return;
    const zt_kern_t *k = nn->kern;
    for (uint32_t t0 = 0; t0 < T; t0 += nn->max_tok) {
        uint32_t B = T - t0 < nn->max_tok ? T - t0 : nn->max_tok;
        lin_job_t j = {nn,   w, bias,    x + (uint64_t) t0 * xstride, xstride,
                       n_in, B, ystride, y + (uint64_t) t0 * ystride};
        par(nn, act_rows, &j, B, (uint64_t) B * w->cols * 4);
        uint64_t work = (uint64_t) w->rows * w->cols * B;
        if (k && k->mm_batch && B >= ZT_MMB_MIN) {
            par(nn, pack_rows, &j, (B + ZT_MMB_G - 1) / ZT_MMB_G, (uint64_t) B * w->cols * 4);
            linb_job_t jb = {j,
                             {w->raw, w->q8, w->qtype, w->cols, nn->aT, nn->ashT, nn->asumT,
                              nn->ashx, B, j.y, ystride}};
            par(nn, linb_rows, &jb, w->rows, work);
        } else {
            par(nn, lin_rows, &j, w->rows, work);
        }
    }
}

/* ---- norms ---- */

/* 1/sqrt(mean + eps) in Q32 from a sum of squares ss of n values each shifted
 * down by sh bits (zt_model.c's rmsnorm, split out). 0 when it is all zero. */
static uint64_t inv_rms(uint64_t ss, uint32_t sh, uint64_t n, uint64_t eps_q48)
{
    uint64_t M = zt_udiv64(ss, n, 0); /* mean, Q(32 - sh) */
    int32_t F = 32 - (int32_t) sh;
    M += F >= 0 && F <= 48 ? eps_q48 >> (48 - F) : 0;
    if (M == 0) return 0;
    int32_t k = 62 - (int32_t) bitlen64(M);
    if ((k + F) & 1) k--;
    uint64_t r = zt_isqrt64(M << k);
    int32_t N = 32 + (k + F) / 2;
    uint64_t q0 = zt_udiv64(1ull << 63, r, 0);
    if (N >= 63) {
        uint32_t up = (uint32_t) (N - 63);
        return (up >= 63 || (q0 >> (62 - up))) ? (uint64_t) LIM62 : q0 << up;
    }
    return N <= 0 ? 0 : q0 >> (63 - N);
}

/* the shift that keeps n * amax^2 below 2^60 */
static uint32_t ss_shift(uint32_t amax, uint64_t n)
{
    uint32_t b = 2 * bitlen64(amax) + bitlen64(n);
    return b > 60 ? b - 60 : 0;
}

static zt_fx norm_out(zt_fx c, uint64_t inv, const zt_gain_t *g, uint32_t i, const zt_fx *bias)
{
    int64_t t = ((int64_t) c * (int64_t) inv + ((int64_t) 1 << 31)) >> 32; /* Q16 */
    if (t > (int64_t) 1 << 32) t = (int64_t) 1 << 32;
    if (t < -((int64_t) 1 << 32)) t = -((int64_t) 1 << 32);
    int64_t half = g->frac ? (int64_t) 1 << (g->frac - 1) : 0;
    int64_t v = (t * g->g[i] + half) >> g->frac;
    if (bias) v += bias[i];
    return sat32(v);
}

typedef struct {
    const zt_fx *x;
    zt_fx *y;
    uint32_t xstride, ystride, n;
    const zt_gain_t *g;
    const zt_fx *b;
    uint64_t eps_q48;
    bool center;
} norm_job_t;

static void norm_rows(void *ctx, uint32_t t0, uint32_t t1)
{
    const norm_job_t *j = ctx;
    uint32_t n = j->n;
    for (uint32_t t = t0; t < t1; t++) {
        const zt_fx *x = j->x + (uint64_t) t * j->xstride;
        zt_fx *y = j->y + (uint64_t) t * j->ystride;
        int64_t mean = 0;
        if (j->center) {
            int64_t s = 0;
            for (uint32_t i = 0; i < n; i++) s += x[i];
            mean = round_div(s, n);
        }
        uint32_t amax = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t a = uabs32(sat32(x[i] - mean));
            if (a > amax) amax = a;
        }
        uint32_t sh = ss_shift(amax, n);
        uint64_t ss = 0;
        for (uint32_t i = 0; i < n; i++) {
            int64_t c = sat32(x[i] - mean);
            ss += (uint64_t) (c * c) >> sh;
        }
        uint64_t inv = inv_rms(ss, sh, n, j->eps_q48);
        for (uint32_t i = 0; i < n; i++) y[i] = norm_out(sat32(x[i] - mean), inv, j->g, i, j->b);
    }
}

void zt_nn_layernorm(zt_nn_t *nn, const zt_fx *x, uint32_t xstride, uint32_t n, uint32_t T,
                     const zt_nn_norm_t *p, uint32_t eps_f32, zt_fx *y, uint32_t ystride)
{
    norm_job_t j = {
        x, y, xstride, ystride, n, &p->g, p->b, (uint64_t) zt_nn_f32_fixed(eps_f32, 48), true};
    par(nn, norm_rows, &j, T, (uint64_t) T * n * 8);
}

void zt_nn_rmsnorm(zt_nn_t *nn, const zt_fx *x, uint32_t xstride, uint32_t n, uint32_t T,
                   const zt_gain_t *g, uint32_t eps_f32, zt_fx *y, uint32_t ystride)
{
    norm_job_t j = {x,    y, xstride, ystride, n, g, 0, (uint64_t) zt_nn_f32_fixed(eps_f32, 48),
                    false};
    par(nn, norm_rows, &j, T, (uint64_t) T * n * 8);
}

void zt_nn_groupnorm(zt_fx *x, uint32_t xstride, uint32_t T, uint32_t C, uint32_t G,
                     const zt_nn_norm_t *p, uint32_t eps_f32)
{
    uint32_t cg = C / G;
    uint64_t cnt = (uint64_t) cg * T, eps = (uint64_t) zt_nn_f32_fixed(eps_f32, 48);
    for (uint32_t gi = 0; gi < G; gi++) {
        uint32_t c0 = gi * cg;
        int64_t s = 0;
        for (uint32_t t = 0; t < T; t++)
            for (uint32_t c = c0; c < c0 + cg; c++) s += x[(uint64_t) t * xstride + c];
        int64_t mean = round_div(s, cnt);
        uint32_t amax = 0;
        for (uint32_t t = 0; t < T; t++)
            for (uint32_t c = c0; c < c0 + cg; c++) {
                uint32_t a = uabs32(sat32(x[(uint64_t) t * xstride + c] - mean));
                if (a > amax) amax = a;
            }
        uint32_t sh = ss_shift(amax, cnt);
        uint64_t ss = 0;
        for (uint32_t t = 0; t < T; t++)
            for (uint32_t c = c0; c < c0 + cg; c++) {
                int64_t v = sat32(x[(uint64_t) t * xstride + c] - mean);
                ss += (uint64_t) (v * v) >> sh;
            }
        uint64_t inv = inv_rms(ss, sh, cnt, eps);
        for (uint32_t t = 0; t < T; t++)
            for (uint32_t c = c0; c < c0 + cg; c++) {
                zt_fx *v = &x[(uint64_t) t * xstride + c];
                *v = norm_out(sat32(*v - mean), inv, &p->g, c, p->b);
            }
    }
}

/* ---- activations ---- */

/* table value at x (Q16) for a table starting at lo (Q16) with n entries at
 * steps of 2^-8, linear in between; Q30. Outside the table: the end values. */
static int64_t gate(zt_fx x, const int32_t *tab, int32_t lo, uint32_t n)
{
    int64_t d = (int64_t) x - lo;
    if (d <= 0) return tab[0];
    uint64_t idx = (uint64_t) d >> 8;
    if (idx >= n - 1) return tab[n - 1];
    int64_t fr = d & 255;
    return tab[idx] + (((int64_t) (tab[idx + 1] - tab[idx]) * fr + 128) >> 8);
}

zt_fx zt_nn_act1(zt_fx x, uint32_t kind)
{
    int64_t g;
    switch (kind) {
    case ZT_NN_GELU_ERF:
        if (x >= 8 * 65536) return x;
        g = gate(x, ZT_NN_PHI, -8 * 65536, 4097);
        break;
    case ZT_NN_GELU_TANH:
        if (x >= 8 * 65536) return x;
        g = gate(x, ZT_NN_GTANH, -8 * 65536, 4097);
        break;
    case ZT_NN_SILU:
        if (x >= 16 * 65536) return x;
        g = gate(x, ZT_NN_SIG, -16 * 65536, 8193);
        break;
    default: /* ZT_NN_SIGMOID: the gate itself, Q16 */
        g = gate(x, ZT_NN_SIG, -16 * 65536, 8193);
        return (zt_fx) ((g + (1 << 13)) >> 14);
    }
    int64_t p = (int64_t) x * g; /* Q46 */
    return sat32(p >= 0 ? (p + (1ll << 29)) >> 30 : -((-p + (1ll << 29)) >> 30));
}

typedef struct {
    zt_fx *x;
    uint32_t xstride, n, kind;
} act_job_t;

static void act_job_rows(void *ctx, uint32_t t0, uint32_t t1)
{
    const act_job_t *j = ctx;
    for (uint32_t t = t0; t < t1; t++) {
        zt_fx *x = j->x + (uint64_t) t * j->xstride;
        for (uint32_t i = 0; i < j->n; i++) x[i] = zt_nn_act1(x[i], j->kind);
    }
}

void zt_nn_act(zt_nn_t *nn, zt_fx *x, uint32_t xstride, uint32_t n, uint32_t T, uint32_t kind)
{
    act_job_t j = {x, xstride, n, kind};
    par(nn, act_job_rows, &j, T, (uint64_t) T * n * 4);
}

void zt_nn_add(zt_nn_t *nn, zt_fx *x, uint32_t xstride, const zt_fx *y, uint32_t ystride,
               uint32_t n, uint32_t T)
{
    uint32_t nsat = 0;
    for (uint32_t t = 0; t < T; t++)
        for (uint32_t i = 0; i < n; i++) {
            zt_fx *a = &x[(uint64_t) t * xstride + i];
            int64_t v = (int64_t) *a + y[(uint64_t) t * ystride + i];
            nsat += v > INT32_MAX || v < INT32_MIN;
            *a = sat32(v);
        }
    nn->n_sat += nsat;
}

/* ---- attention ---- */

typedef struct {
    zt_nn_t *nn;
    const zt_fx *q, *k, *v;
    uint32_t qstride, kstride, vstride, n_q, n_k, hd, tiles;
    bool causal;
    uint32_t inv_sqrt_q30;
    zt_fx *out;
    uint32_t ostride;
} att_job_t;

static void att_rows(void *ctx, uint32_t i0, uint32_t i1)
{
    const att_job_t *j = ctx;
    zt_nn_t *nn = j->nn;
    const zt_kern_t *K = nn->kern;
    zt_att_qk_fn qk = K && K->att_qk ? K->att_qk : zt_att_qk_c;
    zt_att_wv_fn wv = K && K->att_wv ? K->att_wv : zt_att_wv_c;
    uint32_t hd = j->hd;
    for (uint32_t it = i0; it < i1; it++) {
        uint32_t h = it / j->tiles, tile = it % j->tiles;
        zt_fx *scores = nn->scores + (uint64_t) it * nn->max_keys;
        int64_t *acc = nn->acc + (uint64_t) it * nn->max_hd;
        int32_t *qs = nn->qs + (uint64_t) it * nn->max_hd;
        for (uint32_t b = tile; b < j->n_q; b += j->tiles) {
            const zt_fx *qh = j->q + (uint64_t) b * j->qstride + h * hd;
            uint32_t nk = j->n_k;
            if (j->causal && b + 1 + (j->n_k - j->n_q) < nk) nk = b + 1 + (j->n_k - j->n_q);
            uint32_t qmax = 0, sq = 0;
            for (uint32_t d = 0; d < hd; d++)
                if (uabs32(qh[d]) > qmax) qmax = uabs32(qh[d]);
            while ((qmax >> sq) >= (1u << 22)) sq++;
            for (uint32_t d = 0; d < hd; d++)
                qs[d] = sq ? (int32_t) (((int64_t) qh[d] + (1 << (sq - 1))) >> sq) : qh[d];
            zt_fx mx = INT32_MIN;
            int64_t S[256];
            const zt_fx *kb = j->k + h * hd, *vb = j->v + h * hd;
            for (uint32_t c0 = 0; c0 < nk; c0 += 256) {
                uint32_t n = nk - c0 < 256 ? nk - c0 : 256;
                qk(qs, kb + (uint64_t) c0 * j->kstride, j->kstride, n, hd, S);
                for (uint32_t i = 0; i < n; i++) {
                    zt_fx sc = sat32(mul_shr(S[i], j->inv_sqrt_q30, 46 - sq));
                    scores[c0 + i] = sc;
                    if (sc > mx) mx = sc;
                }
            }
            uint64_t sum = 0;
            for (uint32_t i = 0; i < nk; i++) {
                int64_t d = (int64_t) scores[i] - mx;
                scores[i] = zt_exp(d < INT32_MIN ? INT32_MIN : (zt_fx) d);
                sum += (uint32_t) scores[i];
            }
            for (uint32_t d = 0; d < hd; d++) acc[d] = 0;
            wv(scores, vb, j->vstride, nk, hd, acc);
            zt_fx *o = j->out + (uint64_t) b * j->ostride + h * hd;
            for (uint32_t d = 0; d < hd; d++) o[d] = sat32(round_div(acc[d], sum));
        }
    }
}

void zt_nn_attention(zt_nn_t *nn, const zt_fx *q, uint32_t qstride, const zt_fx *k,
                     uint32_t kstride, const zt_fx *v, uint32_t vstride, uint32_t n_q, uint32_t n_k,
                     uint32_t n_head, uint32_t hd, bool causal, zt_fx *out, uint32_t ostride)
{
    if (!n_q || !n_k || n_k > nn->max_keys || hd > nn->max_hd || n_k < (causal ? n_q : 0)) return;
    uint32_t tiles = nn->items / n_head;
    if (tiles > ZT_NN_ATT_TILES) tiles = ZT_NN_ATT_TILES;
    if (tiles > n_q) tiles = n_q;
    if (!tiles) return;
    uint32_t rt = zt_isqrt64((uint64_t) hd << 40);
    att_job_t j = {nn,
                   q,
                   k,
                   v,
                   qstride,
                   kstride,
                   vstride,
                   n_q,
                   n_k,
                   hd,
                   tiles,
                   causal,
                   (uint32_t) zt_udiv64(1ull << 50, rt, 0),
                   out,
                   ostride};
    par(nn, att_rows, &j, n_head * tiles, (uint64_t) n_q * n_k * hd * 2 * n_head);
}

/* ---- convolution ---- */

uint32_t zt_nn_im2col1d(const zt_fx *x, uint32_t xstride, uint32_t T, uint32_t C, uint32_t K,
                        uint32_t stride, uint32_t pad, zt_fx *out, uint32_t ostride)
{
    if (T + 2 * pad < K || !stride) return 0;
    uint32_t To = (T + 2 * pad - K) / stride + 1;
    for (uint32_t t = 0; t < To; t++) {
        zt_fx *o = out + (uint64_t) t * ostride;
        for (uint32_t c = 0; c < C; c++)
            for (uint32_t j = 0; j < K; j++) {
                int64_t s = (int64_t) t * stride - pad + j;
                o[c * K + j] = s < 0 || s >= T ? 0 : x[(uint64_t) s * xstride + c];
            }
    }
    return To;
}

void zt_nn_dwconv1d(const zt_fx *x, uint32_t xstride, uint32_t T, uint32_t C, uint32_t K,
                    uint32_t pad, const zt_gain_t *w, const zt_fx *b, zt_fx *y, uint32_t ystride)
{
    int64_t half = w->frac ? (int64_t) 1 << (w->frac - 1) : 0;
    for (uint32_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < C; c++) {
            int64_t acc = 0;
            for (uint32_t j = 0; j < K; j++) {
                int64_t s = (int64_t) t - pad + j;
                if (s < 0 || s >= T) continue;
                acc += (int64_t) w->g[c * K + j] * x[(uint64_t) s * xstride + c];
            }
            acc = (acc + half) >> w->frac;
            if (b) acc += b[c];
            y[(uint64_t) t * ystride + c] = sat32(acc);
        }
}
