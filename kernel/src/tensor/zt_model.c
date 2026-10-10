/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_model.c — the integer forward pass, sampling and generation (T21). See
 * zt_model.h for the arithmetic and its limits. */
#include "zt_model.h"
#include "zt_kern.h"

#define LIM52 ((int64_t) 1 << 52)
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

/* (a * b) / 2^sh, truncated toward zero, clamped to +-2^58; |a| < 2^63. The
 * 96-bit product is formed from 32-bit halves (no __int128). */
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
        r = (hi << (32 - sh)) + (lo >> sh); /* < 2^59 */
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

/* round(v * 2^frac) for an f32, saturating at +-2^62; NaN is 0. */
static int64_t f32_fixed(uint32_t bits, int32_t frac)
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

/* dst = a + b, NUL-terminated, truncated to cap */
static void cat2(char *dst, uint32_t cap, const char *a, const char *b)
{
    uint32_t n = 0;
    for (; *a && n < cap - 1; a++) dst[n++] = *a;
    for (; *b && n < cap - 1; b++) dst[n++] = *b;
    dst[n] = 0;
}

/* "blk.<l>.<suffix>" */
static void blk_name(char *dst, uint32_t cap, uint32_t l, const char *suffix)
{
    char num[12];
    uint32_t k = 0, n = 0;
    do {
        num[k++] = (char) ('0' + l % 10);
        l /= 10;
    } while (l);
    const char *pre = "blk.";
    for (; *pre && n < cap - 1; pre++) dst[n++] = *pre;
    while (k && n < cap - 1) dst[n++] = num[--k];
    if (n < cap - 1) dst[n++] = '.';
    for (; *suffix && n < cap - 1; suffix++) dst[n++] = *suffix;
    dst[n] = 0;
}

/* ---- configuration ---- */

static int64_t arch_int(const zt_gguf_t *g, const char *arch, const char *key, int64_t def)
{
    char k[96];
    cat2(k, sizeof k, arch, key);
    return zt_gguf_get_int(g, k, def);
}

/* f32 metadata: 0 = ok and *bits set, 1 = absent (def used), <0 bad */
static int32_t arch_f32(const zt_gguf_t *g, const char *arch, const char *key, uint32_t def,
                        uint32_t *bits, char *name, uint32_t cap)
{
    zt_gguf_val_t v;
    cat2(name, cap, arch, key);
    int32_t r = zt_gguf_find(g, name, &v);
    if (r == ZT_GGUF_ENOTFOUND) {
        *bits = def;
        return 1;
    }
    if (r) return r;
    if (v.type != ZT_GGUF_F32) return ZT_MODEL_ECONFIG;
    *bits = (uint32_t) v.u;
    return 0;
}

static bool f32_finite_pos(uint32_t bits, bool allow_zero)
{
    if (bits >> 31) return false;
    if (((bits >> 23) & 0xFF) == 0xFF) return false;
    return allow_zero || (bits & 0x7FFFFFFFu) != 0;
}

/* Find a tensor and check its shape: d1 == 0 means 1-D of d0. */
static int32_t get_tensor(const zt_gguf_t *g, const char *name, uint64_t d0, uint64_t d1,
                          zt_gguf_tensor_t *t, zt_model_err_t *err)
{
    int32_t r = zt_gguf_find_tensor(g, name, t);
    if (r == ZT_GGUF_ENOTFOUND) {
        set_err(err, ZT_MODEL_EMISSING, "missing tensor", name);
        return ZT_MODEL_EMISSING;
    }
    if (r == ZT_GGUF_EUNSUPPORTED) {
        set_err(err, r, "unsupported tensor type", name);
        return r;
    }
    if (r) {
        set_err(err, r, "bad tensor", name);
        return r;
    }
    bool ok = d1 ? (t->n_dims == 2 && t->dims[0] == d0 && t->dims[1] == d1)
                 : (t->n_dims == 1 && t->dims[0] == d0);
    if (!ok) {
        set_err(err, ZT_MODEL_ESHAPE, "tensor shape mismatch", name);
        return ZT_MODEL_ESHAPE;
    }
    return ZT_GGUF_OK;
}

static bool has_tensor(const zt_gguf_t *g, const char *name)
{
    zt_gguf_tensor_t t;
    int32_t r = zt_gguf_find_tensor(g, name, &t);
    return r != ZT_GGUF_ENOTFOUND && r != ZT_GGUF_ETRUNC;
}

int32_t zt_model_config(const zt_gguf_t *g, zt_model_cfg_t *c, zt_model_err_t *err)
{
    zt_gguf_val_t v;
    char name[96];
    set_err(err, ZT_GGUF_OK, "ok", 0);
    if (!g || !c) {
        set_err(err, ZT_MODEL_EARG, "null argument", 0);
        return ZT_MODEL_EARG;
    }
    if (zt_gguf_find(g, "general.architecture", &v) != ZT_GGUF_OK || v.type != ZT_GGUF_STRING) {
        set_err(err, ZT_MODEL_EMISSING, "missing metadata", "general.architecture");
        return ZT_MODEL_EMISSING;
    }
    const char *arch;
    if (zt_gguf_str_eq(v.str, "qwen2")) {
        c->arch = ZT_ARCH_QWEN2;
        arch = "qwen2";
    } else if (zt_gguf_str_eq(v.str, "qwen3")) {
        c->arch = ZT_ARCH_QWEN3;
        arch = "qwen3";
    } else if (zt_gguf_str_eq(v.str, "llama")) {
        c->arch = ZT_ARCH_LLAMA;
        arch = "llama";
    } else {
        uint32_t i = 0;
        for (; i < v.str.len && i < sizeof name - 1; i++) name[i] = (char) v.str.p[i];
        name[i] = 0;
        set_err(err, ZT_MODEL_EARCH, "unsupported architecture", name);
        return ZT_MODEL_EARCH;
    }
    static const char *const req[4] = {".block_count", ".embedding_length", ".feed_forward_length",
                                       ".attention.head_count"};
    int64_t rv[4];
    for (int i = 0; i < 4; i++) {
        rv[i] = arch_int(g, arch, req[i], -1);
        if (rv[i] < 0) {
            cat2(name, sizeof name, arch, req[i]);
            set_err(err, ZT_MODEL_EMISSING, "missing metadata", name);
            return ZT_MODEL_EMISSING;
        }
    }
    int64_t nl = rv[0], ne = rv[1], nf = rv[2], nh = rv[3];
    int64_t nkv = arch_int(g, arch, ".attention.head_count_kv", nh);
    /* 32-bit divisions only (no libgcc on i386), on values range-checked first */
    bool dims_ok = nh >= 1 && nh <= ZT_MODEL_MAX_DIM && ne >= 0 && ne <= ZT_MODEL_MAX_DIM;
    int64_t hd = arch_int(g, arch, ".attention.key_length",
                          dims_ok ? (int64_t) ((uint32_t) ne / (uint32_t) nh) : 0);
    int64_t hdv = arch_int(g, arch, ".attention.value_length", hd);
    int64_t nrot = arch_int(g, arch, ".rope.dimension_count", hd);
    int64_t nctx = arch_int(g, arch, ".context_length", 0);
    const char *bad = 0;
    if (nl < 1 || nl > ZT_MODEL_MAX_LAYERS)
        bad = ".block_count";
    else if (ne < 32 || ne > ZT_MODEL_MAX_DIM || (ne & 31))
        bad = ".embedding_length";
    else if (nf < 32 || nf > ZT_MODEL_MAX_DIM || (nf & 31))
        bad = ".feed_forward_length";
    else if (nh < 1 || nh > ZT_MODEL_MAX_DIM)
        bad = ".attention.head_count";
    else if (nkv < 1 || nkv > nh || (uint32_t) nh % (uint32_t) nkv)
        bad = ".attention.head_count_kv";
    else if (hd < 2 || hd > ZT_MODEL_MAX_HEAD || (hd & 1) || nh * hd > ZT_MODEL_MAX_DIM ||
             ((nh * hd) & 31))
        bad = ".attention.key_length";
    else if (hdv != hd)
        bad = ".attention.value_length";
    else if (nrot < 2 || nrot > hd || (nrot & 1) || nrot > ZT_ROPE_MAX_ROT)
        bad = ".rope.dimension_count";
    else if (nctx < 0 || nctx > 0xFFFFFFFFll)
        bad = ".context_length";
    if (bad) {
        cat2(name, sizeof name, arch, bad);
        set_err(err, ZT_MODEL_ECONFIG, "metadata out of range", name);
        return ZT_MODEL_ECONFIG;
    }
    c->n_layer = (uint32_t) nl;
    c->n_embd = (uint32_t) ne;
    c->n_ff = (uint32_t) nf;
    c->n_head = (uint32_t) nh;
    c->n_head_kv = (uint32_t) nkv;
    c->head_dim = (uint32_t) hd;
    c->n_rot = (uint32_t) nrot;
    c->n_ctx_train = (uint32_t) nctx;
    c->rope_neox = c->arch != ZT_ARCH_LLAMA;

    int32_t r =
        arch_f32(g, arch, ".rope.freq_base", 0x461C4000u, &c->rope_base_f32, name, sizeof name);
    if (r < 0 || !f32_finite_pos(c->rope_base_f32, false)) {
        set_err(err, ZT_MODEL_ECONFIG, "bad metadata value", name);
        return ZT_MODEL_ECONFIG;
    }
    r = arch_f32(g, arch, ".attention.layer_norm_rms_epsilon", 0, &c->rms_eps_f32, name,
                 sizeof name);
    if (r == 1) {
        set_err(err, ZT_MODEL_EMISSING, "missing metadata", name);
        return ZT_MODEL_EMISSING;
    }
    if (r < 0 || !f32_finite_pos(c->rms_eps_f32, true) || f32_fixed(c->rms_eps_f32, 0) > 1) {
        set_err(err, ZT_MODEL_ECONFIG, "bad metadata value", name);
        return ZT_MODEL_ECONFIG;
    }
    c->rope_linear_f32 = 0;
    cat2(name, sizeof name, arch, ".rope.scaling.type");
    if (zt_gguf_find(g, name, &v) == ZT_GGUF_OK) {
        if (v.type != ZT_GGUF_STRING) {
            set_err(err, ZT_MODEL_ECONFIG, "bad metadata value", name);
            return ZT_MODEL_ECONFIG;
        }
        if (zt_gguf_str_eq(v.str, "linear")) {
            r = arch_f32(g, arch, ".rope.scaling.factor", 0x3F800000u, &c->rope_linear_f32, name,
                         sizeof name);
            if (r < 0 || !f32_finite_pos(c->rope_linear_f32, false)) {
                set_err(err, ZT_MODEL_ECONFIG, "bad metadata value", name);
                return ZT_MODEL_ECONFIG;
            }
        } else if (!zt_gguf_str_eq(v.str, "none")) {
            set_err(err, ZT_GGUF_EUNSUPPORTED, "unsupported rope scaling", name);
            return ZT_GGUF_EUNSUPPORTED;
        }
    }

    zt_gguf_tensor_t t;
    r = zt_gguf_find_tensor(g, "token_embd.weight", &t);
    if (r == ZT_GGUF_OK &&
        (t.n_dims != 2 || t.dims[0] != c->n_embd || t.dims[1] < 1 || t.dims[1] > 0x7FFFFFFFull))
        r = ZT_MODEL_ESHAPE;
    if (r) {
        int32_t code = r == ZT_GGUF_ENOTFOUND ? ZT_MODEL_EMISSING : r;
        set_err(err, code,
                r == ZT_GGUF_ENOTFOUND      ? "missing tensor"
                : r == ZT_MODEL_ESHAPE      ? "tensor shape mismatch"
                : r == ZT_GGUF_EUNSUPPORTED ? "unsupported tensor type"
                                            : "bad tensor",
                "token_embd.weight");
        return code;
    }
    c->n_vocab = (uint32_t) t.dims[1];
    c->tied_output = !has_tensor(g, "output.weight");
    c->has_qkv_bias = has_tensor(g, "blk.0.attn_q.bias");
    c->has_qk_norm = has_tensor(g, "blk.0.attn_q_norm.weight");
    c->has_rope_freqs = has_tensor(g, "rope_freqs.weight");
    if (c->arch == ZT_ARCH_QWEN3 && !c->has_qk_norm) {
        set_err(err, ZT_MODEL_EMISSING, "missing tensor", "blk.0.attn_q_norm.weight");
        return ZT_MODEL_EMISSING;
    }
    return ZT_GGUF_OK;
}

/* ---- the arena plan: one walk sizes it, a second fills it ---- */

typedef struct {
    uint8_t *base;
    uint64_t used;
    bool fill;
    zt_fx *scratch;
} arena_t;

static void *take(arena_t *a, uint64_t bytes)
{
    a->used = (a->used + 7) & ~7ull;
    void *p = a->fill ? a->base + a->used : 0;
    a->used += bytes;
    return p;
}

/* 32 Q16 values (or len < 32) to int8 with value = q * scale >> shift, scale in
 * [2^24, 2^25]: error at most half a step, amax / 254. 32-bit division only. */
static void requant(const zt_fx *x, uint32_t len, int8_t *q, int32_t *scale, uint8_t *shift)
{
    uint32_t amax = 0;
    for (uint32_t i = 0; i < len; i++)
        if (uabs32(x[i]) > amax) amax = uabs32(x[i]);
    if (!amax) {
        *scale = 0;
        *shift = 0;
        for (uint32_t i = 0; i < len; i++) q[i] = 0;
        return;
    }
    uint32_t sh = 0;
    while (((uint64_t) amax << sh) < 0x80000000ull) sh++;
    uint32_t A = (uint32_t) ((uint64_t) amax << sh);
    uint32_t sc = A / 127u + (A % 127u >= 64u);
    for (uint32_t i = 0; i < len; i++) {
        uint32_t num = (uint32_t) ((uint64_t) uabs32(x[i]) << sh);
        uint32_t qq = num / sc, rem = num % sc;
        if (2 * rem >= sc) qq++;
        if (qq > 127) qq = 127;
        q[i] = (int8_t) (x[i] < 0 ? -(int32_t) qq : (int32_t) qq);
    }
    *scale = (int32_t) sc;
    *shift = (uint8_t) sh;
}

static int32_t plan_mat(arena_t *a, const zt_gguf_t *g, const char *name, uint32_t cols,
                        uint32_t rows, zt_mat_t *out, zt_model_err_t *err)
{
    zt_gguf_tensor_t t;
    int32_t r = get_tensor(g, name, cols, rows, &t, err);
    if (r) return r;
    zt_mat_t m = {0, 0, rows, cols};
    if (t.type == ZT_GGML_Q8_0) {
        m.raw = t.data;
    } else {
        uint32_t blk = zt_ggml_block(t.type);
        if (!blk || cols % blk) {
            set_err(err, ZT_GGUF_EUNSUPPORTED, "unsupported tensor type", name);
            return ZT_GGUF_EUNSUPPORTED;
        }
        zt_q8_t *q = take(a, (uint64_t) rows * (cols / 32) * sizeof(zt_q8_t));
        if (a->fill) {
            for (uint32_t row = 0; row < rows; row++) {
                r = zt_gguf_dequant(&t, (uint64_t) row * cols, cols, a->scratch);
                if (r) {
                    set_err(err, r, "cannot decode tensor", name);
                    return r;
                }
                zt_q8_t *o = q + (uint64_t) row * (cols / 32);
                for (uint32_t b = 0; b < cols / 32; b++) {
                    o[b].phi_k = 0;
                    requant(a->scratch + b * 32, 32, o[b].q, &o[b].scale, &o[b].shift);
                }
            }
        }
        m.q8 = q;
    }
    if (out) *out = m;
    return ZT_GGUF_OK;
}

static int32_t plan_gain(arena_t *a, const zt_gguf_t *g, const char *name, uint32_t n,
                         zt_gain_t *out, zt_model_err_t *err)
{
    zt_gguf_tensor_t t;
    int32_t r = get_tensor(g, name, n, 0, &t, err);
    if (r) return r;
    int32_t *gv = take(a, (uint64_t) n * 4);
    if (!a->fill) return ZT_GGUF_OK;
    if (t.type == ZT_GGML_F32) {
        int64_t mx = 0;
        for (uint32_t i = 0; i < n; i++) {
            int64_t x = f32_fixed(le32(t.data + 4 * i), 24);
            if (x < 0) x = -x;
            if (x > mx) mx = x;
        }
        int32_t frac = 24;
        while (frac > 0 && mx > INT32_MAX) {
            frac--;
            mx >>= 1;
        }
        for (uint32_t i = 0; i < n; i++) gv[i] = sat32(f32_fixed(le32(t.data + 4 * i), frac));
        out->frac = (uint32_t) frac;
    } else {
        r = zt_gguf_dequant(&t, 0, n, gv);
        if (r) {
            set_err(err, r, "unsupported tensor type", name);
            return r;
        }
        out->frac = 16;
    }
    out->g = gv;
    return ZT_GGUF_OK;
}

static int32_t plan_bias(arena_t *a, const zt_gguf_t *g, const char *name, uint32_t n,
                         const zt_fx **out, zt_model_err_t *err)
{
    zt_gguf_tensor_t t;
    int32_t r = get_tensor(g, name, n, 0, &t, err);
    if (r) return r;
    zt_fx *b = take(a, (uint64_t) n * 4);
    if (a->fill) {
        r = zt_gguf_dequant(&t, 0, n, b);
        if (r) {
            set_err(err, r, "unsupported tensor type", name);
            return r;
        }
        *out = b;
    }
    return ZT_GGUF_OK;
}

static int32_t plan(const zt_gguf_t *g, const zt_model_cfg_t *c, zt_model_t *m, arena_t *a,
                    zt_model_err_t *err)
{
    char nm[96];
    uint32_t ne = c->n_embd, qd = c->n_head * c->head_dim, kvd = c->n_head_kv * c->head_dim;
    uint32_t mx = ne > c->n_ff ? ne : c->n_ff;
    if (qd > mx) mx = qd;
    if (mx < 256) mx = 256;
    a->scratch = take(a, (uint64_t) mx * 4);
    zt_layer_t *L = take(a, (uint64_t) c->n_layer * sizeof(zt_layer_t));
    zt_layer_t dummy;
    int32_t r;
    zt_gguf_tensor_t t;
    r = get_tensor(g, "token_embd.weight", ne, c->n_vocab, &t, err);
    if (r) return r;
    if (!zt_ggml_block(t.type) || ne % zt_ggml_block(t.type)) {
        set_err(err, ZT_GGUF_EUNSUPPORTED, "unsupported tensor type", "token_embd.weight");
        return ZT_GGUF_EUNSUPPORTED;
    }
    if (m) m->embd = t;
    for (uint32_t l = 0; l < c->n_layer; l++) {
        zt_layer_t *y = a->fill ? &L[l] : &dummy;
        static const char *const mn[7] = {"attn_q.weight",      "attn_k.weight",   "attn_v.weight",
                                          "attn_output.weight", "ffn_gate.weight", "ffn_up.weight",
                                          "ffn_down.weight"};
        zt_mat_t *mp[7] = {&y->wq, &y->wk, &y->wv, &y->wo, &y->wg, &y->wu, &y->wd};
        uint32_t cols[7] = {ne, ne, ne, qd, ne, ne, c->n_ff};
        uint32_t rows[7] = {qd, kvd, kvd, ne, c->n_ff, c->n_ff, ne};
        for (int i = 0; i < 7; i++) {
            blk_name(nm, sizeof nm, l, mn[i]);
            r = plan_mat(a, g, nm, cols[i], rows[i], mp[i], err);
            if (r) return r;
        }
        blk_name(nm, sizeof nm, l, "attn_norm.weight");
        if ((r = plan_gain(a, g, nm, ne, &y->attn_norm, err))) return r;
        blk_name(nm, sizeof nm, l, "ffn_norm.weight");
        if ((r = plan_gain(a, g, nm, ne, &y->ffn_norm, err))) return r;
        y->q_norm.g = y->k_norm.g = 0;
        y->q_norm.frac = y->k_norm.frac = 0;
        if (c->has_qk_norm) {
            blk_name(nm, sizeof nm, l, "attn_q_norm.weight");
            if ((r = plan_gain(a, g, nm, c->head_dim, &y->q_norm, err))) return r;
            blk_name(nm, sizeof nm, l, "attn_k_norm.weight");
            if ((r = plan_gain(a, g, nm, c->head_dim, &y->k_norm, err))) return r;
        }
        y->bq = y->bk = y->bv = 0;
        if (c->has_qkv_bias) {
            blk_name(nm, sizeof nm, l, "attn_q.bias");
            if ((r = plan_bias(a, g, nm, qd, &y->bq, err))) return r;
            blk_name(nm, sizeof nm, l, "attn_k.bias");
            if ((r = plan_bias(a, g, nm, kvd, &y->bk, err))) return r;
            blk_name(nm, sizeof nm, l, "attn_v.bias");
            if ((r = plan_bias(a, g, nm, kvd, &y->bv, err))) return r;
        }
    }
    zt_gain_t on = {0, 0};
    if ((r = plan_gain(a, g, "output_norm.weight", ne, &on, err))) return r;
    zt_mat_t out;
    r = plan_mat(a, g, c->tied_output ? "token_embd.weight" : "output.weight", ne, c->n_vocab, &out,
                 err);
    if (r) return r;
    if (m) {
        m->layers = L;
        m->out_norm = on;
        m->out = out;
    }
    return ZT_GGUF_OK;
}

uint64_t zt_model_arena_bytes(const zt_gguf_t *g)
{
    zt_model_cfg_t c;
    if (zt_model_config(g, &c, 0)) return 0;
    arena_t a = {0, 0, false, 0};
    if (plan(g, &c, 0, &a, 0)) return 0;
    return a.used + 64;
}

int32_t zt_model_load(zt_model_t *m, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                      zt_model_err_t *err)
{
    if (!m || !g) {
        set_err(err, ZT_MODEL_EARG, "null argument", 0);
        return ZT_MODEL_EARG;
    }
    int32_t r = zt_model_config(g, &m->cfg, err);
    if (r) return r;
    const zt_model_cfg_t *c = &m->cfg;
    arena_t a = {0, 0, false, 0};
    if ((r = plan(g, c, 0, &a, err))) return r;
    uint64_t pad = (64 - ((uintptr_t) arena & 63)) & 63;
    if (!arena || arena_bytes < pad || a.used > arena_bytes - pad) {
        set_err(err, ZT_MODEL_ESPACE, "arena too small", 0);
        return ZT_MODEL_ESPACE;
    }
    a.base = (uint8_t *) arena + pad;
    a.used = 0;
    a.fill = true;
    if ((r = plan(g, c, m, &a, err))) return r;
    m->q_dim = c->n_head * c->head_dim;
    m->kv_dim = c->n_head_kv * c->head_dim;
    if (!zt_rope_init(&m->rope, c->n_rot, c->rope_base_f32, c->rope_neox)) {
        set_err(err, ZT_MODEL_ECONFIG, "bad rope parameters", "rope.freq_base");
        return ZT_MODEL_ECONFIG;
    }
    if (c->rope_linear_f32 && !zt_rope_divide(&m->rope, &c->rope_linear_f32, 1)) {
        set_err(err, ZT_MODEL_ECONFIG, "bad rope parameters", "rope.scaling.factor");
        return ZT_MODEL_ECONFIG;
    }
    if (c->has_rope_freqs) {
        zt_gguf_tensor_t t;
        uint32_t f[ZT_ROPE_MAX_ROT / 2];
        if ((r = get_tensor(g, "rope_freqs.weight", c->n_rot / 2, 0, &t, err))) return r;
        if (t.type != ZT_GGML_F32) {
            set_err(err, ZT_GGUF_EUNSUPPORTED, "unsupported tensor type", "rope_freqs.weight");
            return ZT_GGUF_EUNSUPPORTED;
        }
        for (uint32_t i = 0; i < c->n_rot / 2; i++) f[i] = le32(t.data + 4 * i);
        if (!zt_rope_divide(&m->rope, f, c->n_rot / 2)) {
            set_err(err, ZT_MODEL_ECONFIG, "bad rope parameters", "rope_freqs.weight");
            return ZT_MODEL_ECONFIG;
        }
    }
    m->kern = 0;
    m->eps_q48 = (uint64_t) f32_fixed(c->rms_eps_f32, 48);
    uint32_t rt = zt_isqrt64((uint64_t) c->head_dim << 40); /* sqrt(hd) * 2^20 */
    m->inv_sqrt_q30 = (uint32_t) zt_udiv64(1ull << 50, rt, 0);
    set_err(err, ZT_GGUF_OK, "ok", 0);
    return ZT_GGUF_OK;
}

/* ---- kernels ---- */

/* Q16 to 16-bit blocks of 32 with a power-of-two exponent: x ~ q * 2^sh. */
static void act_quant(const zt_fx *x, uint32_t n, int16_t *q, uint8_t *sh)
{
    for (uint32_t b = 0; b < n / 32; b++) {
        const zt_fx *v = x + b * 32;
        uint32_t amax = 0;
        for (uint32_t i = 0; i < 32; i++)
            if (uabs32(v[i]) > amax) amax = uabs32(v[i]);
        uint32_t s = 0;
        while (s < 17 && (((uint64_t) amax + ((1ull << s) >> 1)) >> s) > 32767) s++;
        uint32_t half = (1u << s) >> 1;
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t r = (uint32_t) (((uint64_t) uabs32(v[i]) + half) >> s);
            q[b * 32 + i] = (int16_t) (v[i] < 0 ? -(int32_t) r : (int32_t) r);
        }
        sh[b] = (uint8_t) s;
    }
}

/* y[b][r] = W[r] . a[b] (+ bias[r]) for B inputs; each weight row is read
 * once for the whole batch. The dot products are zt_kern.h's C reference, or
 * the model's kernel set (m->kern, hosted builds only), which gives the same
 * bits; rows may be split across threads by m->kern->par_rows. */
typedef struct {
    const zt_mat_t *w;
    const int16_t *a;
    const uint8_t *ash;
    uint32_t astride, B, ystride;
    zt_fx *y;
    const zt_fx *bias;
    zt_dot_raw_fn dot_raw;
    zt_dot_q8_fn dot_q8;
} mm_job_t;

static void mm_rows(void *ctx, uint32_t r0, uint32_t r1)
{
    const mm_job_t *j = ctx;
    const zt_mat_t *w = j->w;
    uint32_t nb = w->cols / 32, as = j->astride;
    for (uint32_t r = r0; r < r1; r++) {
        int64_t bv = j->bias ? j->bias[r] : 0;
        for (uint32_t b = 0; b < j->B; b++) {
            const int16_t *a = j->a + (uint64_t) b * as;
            const uint8_t *ash = j->ash + b * (as / 32);
            zt_fx d;
            if (w->raw) {
                const uint8_t *row = w->raw + (uint64_t) r * nb * 34;
                d = j->dot_raw ? j->dot_raw(row, a, ash, nb) : zt_dot_raw_c(row, a, ash, nb);
            } else {
                const zt_q8_t *row = w->q8 + (uint64_t) r * nb;
                d = j->dot_q8 ? j->dot_q8(row, a, ash, nb) : zt_dot_q8_c(row, a, ash, nb);
            }
            j->y[(uint64_t) b * j->ystride + r] = sat32(d + bv);
        }
    }
}

static void matmul(const zt_model_t *m, const zt_mat_t *w, const int16_t *a, const uint8_t *ash,
                   uint32_t astride, uint32_t B, zt_fx *y, uint32_t ystride, const zt_fx *bias)
{
    const zt_kern_t *k = m->kern;
    mm_job_t j = {w, a, ash, astride, B, ystride, y, bias, k ? k->dot_raw : 0, k ? k->dot_q8 : 0};
    if (k && k->par_rows)
        k->par_rows(k->pctx, mm_rows, &j, w->rows, (uint64_t) w->rows * w->cols * B);
    else
        mm_rows(&j, 0, w->rows);
}

/* y = x / sqrt(mean(x^2) + eps) * gain, n <= 65536. In place is allowed. */
static void rmsnorm(const zt_fx *x, uint32_t n, const zt_gain_t *gn, uint64_t eps_q48, zt_fx *y)
{
    uint32_t amax = 0;
    for (uint32_t i = 0; i < n; i++)
        if (uabs32(x[i]) > amax) amax = uabs32(x[i]);
    /* pre-shift so n * amax^2 < 2^60 (64-bit sum of squares) */
    uint32_t b = 2 * bitlen64(amax) + bitlen64(n), sh = b > 60 ? b - 60 : 0;
    uint64_t ss = 0;
    for (uint32_t i = 0; i < n; i++) ss += (uint64_t) ((int64_t) x[i] * x[i]) >> sh;
    uint64_t M = zt_udiv64(ss, n, 0); /* mean, Q(32 - sh) */
    int32_t F = 32 - (int32_t) sh;    /* >= 13 */
    M += eps_q48 >> (48 - F);         /* eps in the same units */
    if (M == 0) {
        for (uint32_t i = 0; i < n; i++) y[i] = 0;
        return;
    }
    int32_t k = 62 - (int32_t) bitlen64(M); /* M < 2^61, so k >= 1 */
    if ((k + F) & 1) k--;
    uint64_t r = zt_isqrt64(M << k); /* sqrt(M) 2^(k/2), >= 2^30 */
    int32_t N = 32 + (k + F) / 2;    /* inv = 2^N / r is 1/rms in Q32 */
    uint64_t q0 = zt_udiv64(1ull << 63, r, 0), inv;
    if (N >= 63) {
        uint32_t up = (uint32_t) (N - 63);
        inv = (up >= 63 || (q0 >> (62 - up))) ? (uint64_t) LIM62 : q0 << up;
    } else {
        inv = q0 >> (63 - N);
    }
    int64_t half = gn->frac ? (int64_t) 1 << (gn->frac - 1) : 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t t = ((int64_t) x[i] * (int64_t) inv + ((int64_t) 1 << 31)) >> 32; /* Q16 */
        if (t > (int64_t) 1 << 32) t = (int64_t) 1 << 32; /* so t * gain fits 64 bits */
        if (t < -((int64_t) 1 << 32)) t = -((int64_t) 1 << 32);
        y[i] = sat32((t * gn->g[i] + half) >> gn->frac);
    }
}

static zt_fx silu_mul(zt_fx g, zt_fx u)
{
    if (g == 0) return 0;
    uint32_t e = (uint32_t) zt_exp(g < 0 ? g : -g); /* e^-|g| in (0, 1), Q16 */
    uint32_t sig = 0xFFFFFFFFu / (65536u + e);      /* 1 / (1 + e), Q16; d never divides 2^32 */
    if (g < 0) sig = 65536u - sig;
    int64_t s = ((int64_t) g * sig + 32768) >> 16;
    return sat32((s * u + 32768) >> 16);
}

/* ---- state ---- */

typedef struct {
    uint8_t *base;
    uint64_t used;
} lay_t;

static void *lay(lay_t *l, uint64_t bytes)
{
    l->used = (l->used + 7) & ~7ull;
    void *p = l->base ? l->base + l->used : 0;
    l->used += bytes;
    return p;
}

static void state_layout(const zt_model_t *m, zt_model_state_t *s, uint32_t n_ctx, uint32_t B,
                         uint32_t kv_type, lay_t *L)
{
    const zt_model_cfg_t *c = &m->cfg;
    uint64_t cells = (uint64_t) c->n_layer * n_ctx * m->kv_dim;
    uint32_t nbh = (c->head_dim + 31) / 32;
    uint64_t nsc = (uint64_t) c->n_layer * n_ctx * c->n_head_kv * nbh;
    uint32_t as = c->n_embd;
    if (m->q_dim > as) as = m->q_dim;
    if (c->n_ff > as) as = c->n_ff;
    s->nbh = nbh;
    s->a_stride = as;
    s->k16 = s->v16 = 0;
    s->k8 = s->v8 = 0;
    s->ks = s->vs = 0;
    s->ksh = s->vsh = 0;
    if (kv_type == ZT_KV_Q16) {
        s->k16 = lay(L, cells * 4);
        s->v16 = lay(L, cells * 4);
    } else {
        s->k8 = lay(L, cells);
        s->v8 = lay(L, cells);
        s->ks = lay(L, nsc * 4);
        s->vs = lay(L, nsc * 4);
        s->ksh = lay(L, nsc);
        s->vsh = lay(L, nsc);
    }
    s->x = lay(L, (uint64_t) B * c->n_embd * 4);
    s->xn = lay(L, (uint64_t) B * c->n_embd * 4);
    s->q = lay(L, (uint64_t) B * m->q_dim * 4);
    s->k = lay(L, (uint64_t) B * m->kv_dim * 4);
    s->v = lay(L, (uint64_t) B * m->kv_dim * 4);
    s->att = lay(L, (uint64_t) B * m->q_dim * 4);
    s->gt = lay(L, (uint64_t) B * c->n_ff * 4);
    s->up = lay(L, (uint64_t) B * c->n_ff * 4);
    s->a16 = lay(L, (uint64_t) B * as * 2);
    s->ash = lay(L, (uint64_t) B * (as / 32));
    s->scores = lay(L, (uint64_t) n_ctx * 4);
    s->acc = lay(L, (uint64_t) c->head_dim * 8);
    s->qs = lay(L, (uint64_t) c->head_dim * 4);
}

uint64_t zt_model_state_bytes(const zt_model_t *m, uint32_t n_ctx, uint32_t n_batch,
                              uint32_t kv_type)
{
    if (!m || !n_ctx || n_ctx > ZT_MODEL_MAX_CTX || !n_batch || kv_type > ZT_KV_Q8) return 0;
    zt_model_state_t s;
    lay_t L = {0, 0};
    state_layout(m, &s, n_ctx, n_batch, kv_type, &L);
    return L.used + 64;
}

int32_t zt_model_state_init(zt_model_state_t *s, const zt_model_t *m, uint32_t n_ctx,
                            uint32_t n_batch, uint32_t kv_type, void *mem, uint64_t bytes)
{
    uint64_t need = zt_model_state_bytes(m, n_ctx, n_batch, kv_type);
    if (!s || !need) return ZT_MODEL_EARG;
    if (!mem || bytes < need) return ZT_MODEL_ESPACE;
    lay_t L = {(uint8_t *) mem + ((64 - ((uintptr_t) mem & 63)) & 63), 0};
    state_layout(m, s, n_ctx, n_batch, kv_type, &L);
    s->kv_type = kv_type;
    s->n_ctx = n_ctx;
    s->n_batch = n_batch;
    s->pos = 0;
    return ZT_GGUF_OK;
}

void zt_model_state_seek(zt_model_state_t *s, uint32_t pos)
{
    if (pos < s->pos) s->pos = pos;
}

/* ---- attention ---- */

static void kv_store(const zt_model_t *m, zt_model_state_t *s, uint32_t l, uint32_t p,
                     const zt_fx *k, const zt_fx *v)
{
    uint32_t kvd = m->kv_dim, hd = m->cfg.head_dim;
    uint64_t cell = ((uint64_t) l * s->n_ctx + p) * kvd;
    if (s->kv_type == ZT_KV_Q16) {
        for (uint32_t j = 0; j < kvd; j++) {
            s->k16[cell + j] = k[j];
            s->v16[cell + j] = v[j];
        }
        return;
    }
    uint64_t sc = ((uint64_t) l * s->n_ctx + p) * m->cfg.n_head_kv * s->nbh;
    for (uint32_t h = 0; h < m->cfg.n_head_kv; h++)
        for (uint32_t b = 0; b < s->nbh; b++) {
            uint32_t o = h * hd + b * 32, len = hd - b * 32 < 32 ? hd - b * 32 : 32;
            uint64_t si = sc + (uint64_t) h * s->nbh + b;
            requant(k + o, len, s->k8 + cell + o, &s->ks[si], &s->ksh[si]);
            requant(v + o, len, s->v8 + cell + o, &s->vs[si], &s->vsh[si]);
        }
}

/* Attention of one token at position p (all heads), out has q_dim values. */
static void attend(const zt_model_t *m, zt_model_state_t *s, uint32_t l, const zt_fx *q, uint32_t p,
                   zt_fx *out)
{
    const zt_model_cfg_t *c = &m->cfg;
    uint32_t hd = c->head_dim, kvd = m->kv_dim, grp = c->n_head / c->n_head_kv;
    uint64_t base = (uint64_t) l * s->n_ctx;
    for (uint32_t h = 0; h < c->n_head; h++) {
        uint32_t kvh = h / grp;
        const zt_fx *qh = q + h * hd;
        /* scale the query so |q| < 2^22: q.k over head_dim <= 512 values of
         * |k| < 2^31 then stays below 2^62 */
        uint32_t qmax = 0, sq = 0;
        for (uint32_t j = 0; j < hd; j++)
            if (uabs32(qh[j]) > qmax) qmax = uabs32(qh[j]);
        while ((qmax >> sq) >= (1u << 22)) sq++;
        for (uint32_t j = 0; j < hd; j++)
            s->qs[j] = sq ? (int32_t) (((int64_t) qh[j] + (1 << (sq - 1))) >> sq) : qh[j];
        zt_fx mx = INT32_MIN;
        for (uint32_t i = 0; i <= p; i++) {
            int64_t S = 0; /* q.k in Q32 / 2^sq, 64 bits */
            uint64_t cell = (base + i) * kvd + (uint64_t) kvh * hd;
            if (s->kv_type == ZT_KV_Q16) {
                const int32_t *kr = s->k16 + cell;
                for (uint32_t j = 0; j < hd; j++) S += (int64_t) s->qs[j] * kr[j];
            } else {
                const int8_t *kr = s->k8 + cell;
                uint64_t si = (base + i) * c->n_head_kv * s->nbh + (uint64_t) kvh * s->nbh;
                for (uint32_t b = 0; b < s->nbh; b++) {
                    uint32_t o = b * 32, len = hd - o < 32 ? hd - o : 32;
                    int64_t Sb = 0;
                    for (uint32_t j = 0; j < len; j++) Sb += (int64_t) s->qs[o + j] * kr[o + j];
                    S += mul_shr(Sb, (uint32_t) s->ks[si + b], s->ksh[si + b]);
                }
            }
            zt_fx sc = sat32(mul_shr(S, m->inv_sqrt_q30, 46 - sq));
            s->scores[i] = sc;
            if (sc > mx) mx = sc;
        }
        uint64_t sum = 0;
        for (uint32_t i = 0; i <= p; i++) {
            int64_t d = (int64_t) s->scores[i] - mx;
            s->scores[i] = zt_exp(d < INT32_MIN ? INT32_MIN : (zt_fx) d);
            sum += (uint32_t) s->scores[i];
        }
        for (uint32_t j = 0; j < hd; j++) s->acc[j] = 0;
        for (uint32_t i = 0; i <= p; i++) {
            int64_t w = s->scores[i];
            if (!w) continue;
            uint64_t cell = (base + i) * kvd + (uint64_t) kvh * hd;
            if (s->kv_type == ZT_KV_Q16) {
                const int32_t *vr = s->v16 + cell;
                for (uint32_t j = 0; j < hd; j++) s->acc[j] += w * vr[j];
            } else {
                const int8_t *vr = s->v8 + cell;
                uint64_t si = (base + i) * c->n_head_kv * s->nbh + (uint64_t) kvh * s->nbh;
                for (uint32_t j = 0; j < hd; j++) {
                    uint32_t b = j / 32, sh = s->vsh[si + b];
                    int64_t pv = (int64_t) vr[j] * s->vs[si + b];
                    int64_t dv = sh ? (pv + ((int64_t) 1 << (sh - 1))) >> sh : pv;
                    s->acc[j] += w * sat32(dv);
                }
            }
        }
        for (uint32_t j = 0; j < hd; j++) out[h * hd + j] = sat32(round_div(s->acc[j], sum));
    }
}

/* ---- the forward pass ---- */

static int32_t forward(const zt_model_t *m, zt_model_state_t *s, const int32_t *tok, uint32_t B,
                       zt_fx *logits, bool all)
{
    const zt_model_cfg_t *c = &m->cfg;
    uint32_t ne = c->n_embd, nf = c->n_ff, qd = m->q_dim, kvd = m->kv_dim, hd = c->head_dim;
    uint32_t as = s->a_stride, p0 = s->pos;
    for (uint32_t b = 0; b < B; b++) {
        int32_t r = zt_gguf_dequant(&m->embd, (uint64_t) tok[b] * ne, ne, s->x + b * ne);
        if (r) return r;
    }
    for (uint32_t l = 0; l < c->n_layer; l++) {
        const zt_layer_t *L = &m->layers[l];
        for (uint32_t b = 0; b < B; b++) {
            rmsnorm(s->x + b * ne, ne, &L->attn_norm, m->eps_q48, s->xn + b * ne);
            act_quant(s->xn + b * ne, ne, s->a16 + b * as, s->ash + b * (as / 32));
        }
        matmul(m, &L->wq, s->a16, s->ash, as, B, s->q, qd, L->bq);
        matmul(m, &L->wk, s->a16, s->ash, as, B, s->k, kvd, L->bk);
        matmul(m, &L->wv, s->a16, s->ash, as, B, s->v, kvd, L->bv);
        for (uint32_t b = 0; b < B; b++) {
            zt_fx *q = s->q + b * qd, *k = s->k + b * kvd;
            if (L->q_norm.g) {
                for (uint32_t h = 0; h < c->n_head; h++)
                    rmsnorm(q + h * hd, hd, &L->q_norm, m->eps_q48, q + h * hd);
                for (uint32_t h = 0; h < c->n_head_kv; h++)
                    rmsnorm(k + h * hd, hd, &L->k_norm, m->eps_q48, k + h * hd);
            }
            zt_rope_apply_heads(&m->rope, q, c->n_head, hd, p0 + b);
            zt_rope_apply_heads(&m->rope, k, c->n_head_kv, hd, p0 + b);
            kv_store(m, s, l, p0 + b, k, s->v + b * kvd);
        }
        for (uint32_t b = 0; b < B; b++) {
            attend(m, s, l, s->q + b * qd, p0 + b, s->att + b * qd);
            act_quant(s->att + b * qd, qd, s->a16 + b * as, s->ash + b * (as / 32));
        }
        matmul(m, &L->wo, s->a16, s->ash, as, B, s->xn, ne, 0);
        for (uint32_t b = 0; b < B; b++) {
            zt_fx *x = s->x + b * ne, *o = s->xn + b * ne;
            for (uint32_t i = 0; i < ne; i++) x[i] = sat32((int64_t) x[i] + o[i]);
            rmsnorm(x, ne, &L->ffn_norm, m->eps_q48, o);
            act_quant(o, ne, s->a16 + b * as, s->ash + b * (as / 32));
        }
        matmul(m, &L->wg, s->a16, s->ash, as, B, s->gt, nf, 0);
        matmul(m, &L->wu, s->a16, s->ash, as, B, s->up, nf, 0);
        for (uint32_t b = 0; b < B; b++) {
            zt_fx *g = s->gt + b * nf, *u = s->up + b * nf;
            for (uint32_t i = 0; i < nf; i++) g[i] = silu_mul(g[i], u[i]);
            act_quant(g, nf, s->a16 + b * as, s->ash + b * (as / 32));
        }
        matmul(m, &L->wd, s->a16, s->ash, as, B, s->xn, ne, 0);
        for (uint32_t b = 0; b < B; b++) {
            zt_fx *x = s->x + b * ne, *o = s->xn + b * ne;
            for (uint32_t i = 0; i < ne; i++) x[i] = sat32((int64_t) x[i] + o[i]);
        }
    }
    s->pos = p0 + B;
    if (!logits) return ZT_GGUF_OK;
    uint32_t first = all ? 0 : B - 1;
    for (uint32_t b = first; b < B; b++) {
        rmsnorm(s->x + b * ne, ne, &m->out_norm, m->eps_q48, s->xn + b * ne);
        act_quant(s->xn + b * ne, ne, s->a16 + (b - first) * as, s->ash + (b - first) * (as / 32));
    }
    matmul(m, &m->out, s->a16, s->ash, as, B - first, logits, c->n_vocab, 0);
    return ZT_GGUF_OK;
}

int32_t zt_model_eval(const zt_model_t *m, zt_model_state_t *s, const int32_t *tokens, uint32_t n,
                      zt_fx *logits, uint32_t flags)
{
    if (!m || !s || !tokens || !n) return ZT_MODEL_EARG;
    if (n > s->n_ctx - s->pos) return ZT_MODEL_ECTX;
    for (uint32_t i = 0; i < n; i++)
        if (tokens[i] < 0 || (uint32_t) tokens[i] >= m->cfg.n_vocab) return ZT_MODEL_EARG;
    bool all = flags & ZT_EVAL_ALL_LOGITS;
    for (uint32_t off = 0; off < n; off += s->n_batch) {
        uint32_t B = n - off < s->n_batch ? n - off : s->n_batch;
        zt_fx *lg = 0;
        if (logits && all) lg = logits + (uint64_t) off * m->cfg.n_vocab;
        if (logits && !all && off + B == n) lg = logits;
        int32_t r = forward(m, s, tokens + off, B, lg, all);
        if (r) return r;
    }
    return ZT_GGUF_OK;
}

int32_t zt_model_decode(const zt_model_t *m, zt_model_state_t *s, int32_t token, zt_fx *logits)
{
    return zt_model_eval(m, s, &token, 1, logits, 0);
}

/* ---- sampling ---- */

typedef struct {
    int32_t id;
    zt_fx l;
    uint64_t w;
} cand_t;

static bool better(const cand_t *a, const cand_t *b)
{
    return a->l > b->l || (a->l == b->l && a->id < b->id);
}

/* heap with the worst candidate at the root */
static void sift(cand_t *c, uint32_t n, uint32_t i)
{
    for (;;) {
        uint32_t w = i, l = 2 * i + 1, r = l + 1;
        if (l < n && better(&c[w], &c[l])) w = l;
        if (r < n && better(&c[w], &c[r])) w = r;
        if (w == i) return;
        cand_t t = c[i];
        c[i] = c[w];
        c[w] = t;
        i = w;
    }
}

static uint64_t sm_next(uint64_t *st)
{
    uint64_t z = (*st += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t mulhi64(uint64_t a, uint64_t b)
{
    uint64_t a0 = (uint32_t) a, a1 = a >> 32, b0 = (uint32_t) b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t) p01 + (uint32_t) p10;
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* e^d for d <= 0 (Q16) in Q32: two Q16 halves multiplied, to about e^-22 */
static uint64_t exp_q32(int64_t d)
{
    if (d >= 0) return 1ull << 32;
    if (d < -(int64_t) 23 * 65536) return 0;
    zt_fx h = (zt_fx) (d >> 1);
    return (uint64_t) (uint32_t) zt_exp(h) * (uint32_t) zt_exp((zt_fx) (d - h));
}

void zt_sampler_init(zt_sampler_t *sp, const zt_sampler_params_t *p)
{
    sp->p = *p;
    sp->rng = p->seed;
}

uint64_t zt_sample_work_bytes(uint32_t n_vocab)
{
    return (uint64_t) n_vocab * sizeof(cand_t) + 8;
}

int32_t zt_argmax(const zt_fx *logits, uint32_t n)
{
    if (!logits || !n) return ZT_MODEL_EARG;
    uint32_t best = 0;
    for (uint32_t i = 1; i < n; i++)
        if (logits[i] > logits[best]) best = i;
    return (int32_t) best;
}

int32_t zt_sample(zt_sampler_t *sp, const zt_fx *logits, uint32_t V, const int32_t *hist,
                  uint32_t n_hist, void *work, uint64_t work_bytes)
{
    if (!sp || !logits || !V || V > 0x7FFFFFFFu) return ZT_MODEL_EARG;
    if (!work || work_bytes < zt_sample_work_bytes(V)) return ZT_MODEL_ESPACE;
    cand_t *c = (cand_t *) ((uint8_t *) work + ((8 - ((uintptr_t) work & 7)) & 7));
    const zt_sampler_params_t *p = &sp->p;
    for (uint32_t i = 0; i < V; i++) {
        c[i].id = (int32_t) i;
        c[i].l = logits[i];
        c[i].w = 0;
    }
    /* repetition penalty, once per distinct recent token */
    if (p->repeat_penalty > 0 && p->repeat_penalty != ZT_ONE && hist && p->repeat_last_n) {
        uint64_t inv = zt_udiv64(1ull << 32, (uint64_t) p->repeat_penalty, 0);
        uint32_t from = n_hist > p->repeat_last_n ? n_hist - p->repeat_last_n : 0;
        for (uint32_t j = from; j < n_hist; j++) {
            int32_t id = hist[j];
            if (id < 0 || (uint32_t) id >= V || c[id].w) continue;
            c[id].w = 1;
            int64_t l = c[id].l;
            c[id].l = l <= 0 ? sat32((l * p->repeat_penalty) >> 16)
                             : sat32((l * (int64_t) inv + 32768) >> 16);
        }
    }
    if (p->temperature <= 0) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < V; i++)
            if (better(&c[i], &c[best])) best = i;
        return c[best].id;
    }
    /* top-k: keep the k best in a heap, then sort them best first */
    uint32_t k = p->top_k && p->top_k < V ? p->top_k : V;
    for (uint32_t i = k / 2; i-- > 0;) sift(c, k, i);
    for (uint32_t i = k; i < V; i++)
        if (better(&c[i], &c[0])) {
            c[0] = c[i];
            sift(c, k, 0);
        }
    for (uint32_t end = k - 1; end > 0; end--) {
        cand_t t = c[0];
        c[0] = c[end];
        c[end] = t;
        sift(c, end, 0);
    }
    zt_fx lmax = c[0].l;
    /* top-p on the temperature-1 probabilities */
    if (p->top_p > 0 && p->top_p < ZT_ONE) {
        uint64_t tot = 0;
        for (uint32_t i = 0; i < k; i++) tot += c[i].w = exp_q32((int64_t) c[i].l - lmax);
        uint64_t thr =
            (tot >> 16) * (uint32_t) p->top_p + (((tot & 0xFFFF) * (uint32_t) p->top_p) >> 16);
        uint64_t cum = 0;
        uint32_t keep = 0;
        while (keep < k) {
            cum += c[keep++].w;
            if (cum >= thr) break;
        }
        k = keep ? keep : 1;
    }
    /* temperature, then the draw */
    uint64_t it = zt_udiv64(1ull << 32, (uint64_t) p->temperature, 0);
    if (it > 0xFFFFFFFFull) it = 0xFFFFFFFFull;
    uint64_t tot = 0;
    for (uint32_t i = 0; i < k; i++) {
        int64_t d = mul_shr((int64_t) c[i].l - lmax, (uint32_t) it, 16);
        tot += c[i].w = exp_q32(d);
    }
    uint64_t r = mulhi64(sm_next(&sp->rng), tot), cum = 0;
    for (uint32_t i = 0; i < k; i++) {
        cum += c[i].w;
        if (r < cum) return c[i].id;
    }
    return c[0].id;
}

/* ---- generate ---- */

static uint32_t max_piece(const zt_tok_t *t)
{
    uint64_t mx = 1;
    for (uint32_t i = 0; i < t->n_vocab; i++)
        if (t->tok[i].len > mx) mx = t->tok[i].len;
    return mx > 0xFFFFFFull ? 0xFFFFFFu : (uint32_t) mx;
}

typedef struct {
    int32_t *ids;
    uint64_t cap;
    uint8_t *enc;
    uint64_t enc_bytes;
    zt_fx *logits;
    uint8_t *samp;
    uint64_t samp_bytes;
    uint8_t *piece;
    uint32_t piece_cap;
    uint8_t *tail;
} gen_lay_t;

static uint64_t gen_layout(const zt_model_t *m, const zt_tok_t *t, uint64_t plen, uint32_t max_tok,
                           uint8_t *base, gen_lay_t *g)
{
    lay_t L = {base, 0};
    g->cap = plen + 2 + max_tok;
    g->ids = lay(&L, g->cap * 4);
    g->enc_bytes = zt_tok_work_bytes(plen);
    g->enc = lay(&L, g->enc_bytes);
    g->logits = lay(&L, (uint64_t) m->cfg.n_vocab * 4);
    g->samp_bytes = zt_sample_work_bytes(m->cfg.n_vocab);
    g->samp = lay(&L, g->samp_bytes);
    g->piece_cap = max_piece(t) + 8;
    g->piece = lay(&L, g->piece_cap);
    g->tail = lay(&L, 64);
    return L.used + 8;
}

uint64_t zt_model_gen_work_bytes(const zt_model_t *m, const zt_tok_t *t, uint64_t prompt_len,
                                 uint32_t max_tokens)
{
    gen_lay_t g;
    if (!m || !t || prompt_len > (1ull << 40)) return 0;
    return gen_layout(m, t, prompt_len, max_tokens, 0, &g);
}

int32_t zt_model_generate(const zt_model_t *m, zt_model_state_t *s, const zt_tok_t *t,
                          const uint8_t *prompt, uint64_t plen, const zt_gen_params_t *gp,
                          void *work, uint64_t work_bytes, zt_gen_result_t *res)
{
    if (!m || !s || !t || !gp || !res || (plen && !prompt)) return ZT_MODEL_EARG;
    res->n_prompt = res->n_gen = res->stop = res->stop_trim = 0;
    for (uint32_t i = 0; i < gp->n_stop_strs; i++)
        if (!gp->stop_strs[i].s || gp->stop_strs[i].len == 0 || gp->stop_strs[i].len > 64)
            return ZT_MODEL_EARG;
    uint64_t need = zt_model_gen_work_bytes(m, t, plen, gp->max_tokens);
    if (!work || !need || work_bytes < need) return ZT_MODEL_ESPACE;
    gen_lay_t g;
    gen_layout(m, t, plen, gp->max_tokens, (uint8_t *) work + ((8 - ((uintptr_t) work & 7)) & 7),
               &g);
    uint64_t n = 0, ne = 0;
    if (t->add_bos && t->bos >= 0 && s->pos == 0) g.ids[n++] = t->bos;
    if (plen) {
        int32_t r = zt_tok_encode(t, prompt, plen, gp->parse_special, g.ids + n, g.cap - n, &ne,
                                  g.enc, g.enc_bytes);
        if (r) return r;
        n += ne;
    }
    if (!n) return ZT_MODEL_EARG;
    if (n > s->n_ctx - s->pos) return ZT_MODEL_ECTX;
    res->n_prompt = (uint32_t) n;
    int32_t r = zt_model_eval(m, s, g.ids, (uint32_t) n, g.logits, 0);
    if (r) return r;
    uint32_t tail_len = 0;
    for (;;) {
        if (res->n_gen >= gp->max_tokens) {
            res->stop = ZT_GEN_MAX;
            break;
        }
        int32_t tok = gp->sampler ? zt_sample(gp->sampler, g.logits, m->cfg.n_vocab, g.ids,
                                              (uint32_t) n, g.samp, g.samp_bytes)
                                  : zt_argmax(g.logits, m->cfg.n_vocab);
        if (tok < 0) return tok;
        bool end = tok == t->eos;
        for (uint32_t i = 0; i < gp->n_stop_ids; i++) end = end || tok == gp->stop_ids[i];
        if (end) {
            res->stop = ZT_GEN_EOS;
            break;
        }
        g.ids[n++] = tok;
        res->n_gen++;
        uint64_t pl = 0;
        if ((uint32_t) tok < t->n_vocab) {
            r = zt_tok_decode(t, &tok, 1, g.piece, g.piece_cap, &pl);
            if (r) return r;
        }
        for (uint64_t i = 0; i < pl; i++) {
            if (tail_len == 64) {
                for (uint32_t j = 1; j < 64; j++) g.tail[j - 1] = g.tail[j];
                tail_len--;
            }
            g.tail[tail_len++] = g.piece[i];
        }
        bool more = gp->on_token ? gp->on_token(gp->ctx, tok, g.piece, (uint32_t) pl) : true;
        for (uint32_t i = 0; i < gp->n_stop_strs && !res->stop; i++) {
            uint32_t sl = gp->stop_strs[i].len;
            if (sl > tail_len) continue;
            bool eq = true;
            for (uint32_t j = 0; j < sl && eq; j++)
                eq = g.tail[tail_len - sl + j] == gp->stop_strs[i].s[j];
            if (eq) {
                res->stop = ZT_GEN_STOP_STR;
                res->stop_trim = sl;
            }
        }
        if (res->stop) break;
        if (!more) {
            res->stop = ZT_GEN_CALLBACK;
            break;
        }
        if (res->n_gen >= gp->max_tokens) {
            res->stop = ZT_GEN_MAX;
            break;
        }
        if (s->pos >= s->n_ctx) {
            res->stop = ZT_GEN_CTX;
            break;
        }
        r = zt_model_decode(m, s, tok, g.logits);
        if (r) return r;
    }
    return ZT_GGUF_OK;
}
