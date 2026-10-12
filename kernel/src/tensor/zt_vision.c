/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_vision.c — the SigLIP image encoder and Idefics3 projector (T24). See
 * zt_vision.h. */
#include "zt_vision.h"

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

/* "v.blk.<l>.<suffix>" */
static void vblk(char *dst, uint32_t cap, uint32_t l, const char *suffix)
{
    char num[12];
    uint32_t k = 0, n = 0;
    do {
        num[k++] = (char) ('0' + l % 10);
        l /= 10;
    } while (l);
    const char *pre = "v.blk.";
    for (; *pre && n < cap - 1; pre++) dst[n++] = *pre;
    while (k && n < cap - 1) dst[n++] = num[--k];
    if (n < cap - 1) dst[n++] = '.';
    for (; *suffix && n < cap - 1; suffix++) dst[n++] = *suffix;
    dst[n] = 0;
}

/* "v.blk.<l>.<a><b>" */
static void vblk2(char *dst, uint32_t cap, uint32_t l, const char *a, const char *b)
{
    char s[48];
    uint32_t n = 0;
    for (; *a && n < sizeof s - 1; a++) s[n++] = *a;
    for (; *b && n < sizeof s - 1; b++) s[n++] = *b;
    s[n] = 0;
    vblk(dst, cap, l, s);
}

static bool key_is(const zt_gguf_t *g, const char *key, const char *val)
{
    zt_gguf_val_t v;
    return zt_gguf_find(g, key, &v) == ZT_GGUF_OK && v.type == ZT_GGUF_STRING &&
           zt_gguf_str_eq(v.str, val);
}

int32_t zt_vision_config(const zt_gguf_t *g, zt_vision_cfg_t *c, zt_model_err_t *err)
{
    if (!key_is(g, "general.architecture", "clip") ||
        !key_is(g, "clip.projector_type", "idefics3")) {
        set_err(err, ZT_NN_ECONFIG, "not an Idefics3 (SmolVLM) vision projector",
                "clip.projector_type");
        return ZT_NN_ECONFIG;
    }
    c->image_size = (uint32_t) zt_gguf_get_int(g, "clip.vision.image_size", 0);
    c->patch = (uint32_t) zt_gguf_get_int(g, "clip.vision.patch_size", 0);
    c->n_embd = (uint32_t) zt_gguf_get_int(g, "clip.vision.embedding_length", 0);
    c->n_ff = (uint32_t) zt_gguf_get_int(g, "clip.vision.feed_forward_length", 0);
    c->n_head = (uint32_t) zt_gguf_get_int(g, "clip.vision.attention.head_count", 0);
    c->n_layer = (uint32_t) zt_gguf_get_int(g, "clip.vision.block_count", 0);
    c->proj_dim = (uint32_t) zt_gguf_get_int(g, "clip.vision.projection_dim", 0);
    c->scale = (uint32_t) zt_gguf_get_int(g, "clip.vision.projector.scale_factor", 0);
    c->eps_f32 = zt_nn_f32_key(g, "clip.vision.attention.layer_norm_epsilon", 0x358637BDu);
    if (!c->image_size || c->image_size > 4096 || !c->patch || c->image_size % c->patch ||
        !c->n_embd || c->n_embd > 8192 || !c->n_ff || c->n_ff > 32768 || !c->n_head ||
        c->n_embd % c->n_head || c->n_embd / c->n_head > 512 || !c->n_layer || c->n_layer > 256 ||
        !c->proj_dim || c->proj_dim > 32768 || !c->scale || (c->image_size / c->patch) % c->scale) {
        set_err(err, ZT_NN_ECONFIG, "vision metadata out of range", "clip.vision");
        return ZT_NN_ECONFIG;
    }
    uint32_t side = c->image_size / c->patch;
    c->n_patch = side * side;
    c->n_out = c->n_patch / (c->scale * c->scale);
    if (c->n_patch > 65535) {
        set_err(err, ZT_NN_ECONFIG, "too many patches", "clip.vision.image_size");
        return ZT_NN_ECONFIG;
    }
    for (uint32_t i = 0; i < 3; i++) {
        c->mean[i] = 32768;
        c->inv_std[i] = 131072; /* defaults: mean 0.5, std 0.5 */
    }
    zt_gguf_val_t a;
    if (zt_gguf_find(g, "clip.vision.image_mean", &a) == ZT_GGUF_OK && a.type == ZT_GGUF_ARRAY &&
        a.count == 3)
        for (uint32_t i = 0; i < 3; i++) zt_gguf_arr_q16(g, &a, i, &c->mean[i]);
    if (zt_gguf_find(g, "clip.vision.image_std", &a) == ZT_GGUF_OK && a.type == ZT_GGUF_ARRAY &&
        a.count == 3)
        for (uint32_t i = 0; i < 3; i++) {
            zt_fx sd = 0;
            zt_gguf_arr_q16(g, &a, i, &sd);
            if (sd <= 0) {
                set_err(err, ZT_NN_ECONFIG, "bad image_std", "clip.vision.image_std");
                return ZT_NN_ECONFIG;
            }
            c->inv_std[i] = (zt_fx) zt_udiv64((1ull << 32) + (uint64_t) (sd / 2), (uint64_t) sd, 0);
        }
    return ZT_GGUF_OK;
}

static int32_t plan(const zt_gguf_t *g, const zt_vision_cfg_t *c, zt_vision_t *v, zt_nn_arena_t *a)
{
    char nm[64];
    int32_t r;
    uint32_t ne = c->n_embd, nf = c->n_ff, pc = 3 * c->patch * c->patch;
    zt_vision_t dv;
    if (!v) v = &dv;
    zt_vision_layer_t *L = zt_nn_take(a, (uint64_t) c->n_layer * sizeof *L), dl;
    if ((r = zt_nn_load_mat(a, g, "v.patch_embd.weight", pc, ne, &v->patch_w))) return r;
    if ((r = zt_nn_load_q16(a, g, "v.patch_embd.bias", ne, &v->patch_b))) return r;
    if ((r = zt_nn_load_q16(a, g, "v.position_embd.weight", (uint64_t) ne * c->n_patch, &v->pos)))
        return r;
    for (uint32_t l = 0; l < c->n_layer; l++) {
        zt_vision_layer_t *y = L ? &L[l] : &dl;
        /* llama.cpp's converter stores SigLIP's fc1 as ffn_down and fc2 as
         * ffn_up in some files: pick by shape, as llama.cpp does */
        zt_gguf_tensor_t t;
        vblk(nm, sizeof nm, l, "ffn_down.weight");
        bool swapped = zt_gguf_find_tensor(g, nm, &t) == ZT_GGUF_OK && t.dims[0] == ne;
        const char *f1 = swapped ? "ffn_down" : "ffn_up", *f2 = swapped ? "ffn_up" : "ffn_down";
        static const char *const mn[4] = {"attn_q", "attn_k", "attn_v", "attn_out"};
        zt_mat_t *mp[4] = {&y->wq, &y->wk, &y->wv, &y->wo};
        const zt_fx **bp[4] = {&y->bq, &y->bk, &y->bv, &y->bo};
        for (int i = 0; i < 4; i++) {
            vblk2(nm, sizeof nm, l, mn[i], ".weight");
            if ((r = zt_nn_load_mat(a, g, nm, ne, ne, mp[i]))) return r;
            vblk2(nm, sizeof nm, l, mn[i], ".bias");
            if ((r = zt_nn_load_q16(a, g, nm, ne, bp[i]))) return r;
        }
        vblk2(nm, sizeof nm, l, f1, ".weight");
        if ((r = zt_nn_load_mat(a, g, nm, ne, nf, &y->w1))) return r;
        vblk2(nm, sizeof nm, l, f1, ".bias");
        if ((r = zt_nn_load_q16(a, g, nm, nf, &y->b1))) return r;
        vblk2(nm, sizeof nm, l, f2, ".weight");
        if ((r = zt_nn_load_mat(a, g, nm, nf, ne, &y->w2))) return r;
        vblk2(nm, sizeof nm, l, f2, ".bias");
        if ((r = zt_nn_load_q16(a, g, nm, ne, &y->b2))) return r;
        vblk(nm, sizeof nm, l, "ln1.weight");
        if ((r = zt_nn_load_gain(a, g, nm, ne, &y->ln1.g))) return r;
        vblk(nm, sizeof nm, l, "ln1.bias");
        if ((r = zt_nn_load_q16(a, g, nm, ne, &y->ln1.b))) return r;
        vblk(nm, sizeof nm, l, "ln2.weight");
        if ((r = zt_nn_load_gain(a, g, nm, ne, &y->ln2.g))) return r;
        vblk(nm, sizeof nm, l, "ln2.bias");
        if ((r = zt_nn_load_q16(a, g, nm, ne, &y->ln2.b))) return r;
    }
    if ((r = zt_nn_load_gain(a, g, "v.post_ln.weight", ne, &v->post_ln.g))) return r;
    if ((r = zt_nn_load_q16(a, g, "v.post_ln.bias", ne, &v->post_ln.b))) return r;
    uint32_t pin = ne * c->scale * c->scale;
    if ((r = zt_nn_load_mat(a, g, "mm.model.fc.weight", pin, c->proj_dim, &v->proj))) return r;
    v->layers = L;
    return ZT_GGUF_OK;
}

static uint32_t cols_max(const zt_vision_cfg_t *c)
{
    uint32_t m = c->n_embd * c->scale * c->scale;
    if (c->n_ff > m) m = c->n_ff;
    if (3 * c->patch * c->patch > m) m = 3 * c->patch * c->patch;
    return m;
}

uint64_t zt_vision_arena_bytes(const zt_gguf_t *g)
{
    zt_vision_cfg_t c;
    if (zt_vision_config(g, &c, 0)) return 0;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, cols_max(&c), 0);
    if (plan(g, &c, 0, &a)) return 0;
    return zt_nn_arena_used(&a);
}

int32_t zt_vision_load(zt_vision_t *v, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                       zt_model_err_t *err)
{
    if (!v || !g || !arena) {
        set_err(err, ZT_NN_EARG, "null argument", 0);
        return ZT_NN_EARG;
    }
    int32_t r = zt_vision_config(g, &v->cfg, err);
    if (r) return r;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, cols_max(&v->cfg), err);
    if ((r = plan(g, &v->cfg, 0, &a))) return r;
    if (zt_nn_arena_used(&a) > arena_bytes) {
        set_err(err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    zt_nn_arena_begin(&a, arena, arena_bytes, cols_max(&v->cfg), err);
    if ((r = plan(g, &v->cfg, v, &a))) return r;
    v->kern = 0;
    set_err(err, ZT_GGUF_OK, "ok", 0);
    return ZT_GGUF_OK;
}

/* ---- the forward pass ---- */

typedef struct {
    zt_fx *x, *xn, *q, *k, *v, *att, *h, *patches;
    void *nn_mem;
    uint64_t nn_bytes;
} vwork_t;

static uint64_t work_layout(const zt_vision_cfg_t *c, uint8_t *base, vwork_t *w)
{
    uint64_t o = 0, P = c->n_patch, ne = c->n_embd;
    uint32_t pc = 3 * c->patch * c->patch;
#define VTAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    VTAKE(w->x, P * ne * 4);
    VTAKE(w->xn, P * ne * 4);
    VTAKE(w->q, P * ne * 4);
    VTAKE(w->k, P * ne * 4);
    VTAKE(w->v, P * ne * 4);
    VTAKE(w->att, P * ne * 4);
    VTAKE(w->h, P * (c->n_ff > pc ? c->n_ff : pc) * 4);
    w->patches = w->h; /* the patch rows are consumed before the first fc1 */
    w->nn_bytes = zt_nn_bytes(256, cols_max(c), (uint32_t) P, ne / c->n_head, c->n_head);
    VTAKE(w->nn_mem, w->nn_bytes);
#undef VTAKE
    return o + 64;
}

uint64_t zt_vision_work_bytes(const zt_vision_t *v)
{
    vwork_t w;
    return work_layout(&v->cfg, 0, &w);
}

int32_t zt_vision_encode(const zt_vision_t *v, const uint8_t *rgb, zt_fx *out, void *work,
                         uint64_t work_bytes, uint32_t *n_sat)
{
    const zt_vision_cfg_t *c = &v->cfg;
    if (!v || !rgb || !out || !work || work_bytes < zt_vision_work_bytes(v)) return ZT_NN_ESPACE;
    vwork_t w;
    work_layout(c, (uint8_t *) work + ((64 - ((uintptr_t) work & 63)) & 63), &w);
    zt_nn_t nn;
    uint32_t ne = c->n_embd, nf = c->n_ff, P = c->n_patch, ps = c->patch, S = c->image_size;
    uint32_t side = S / ps, pc = 3 * ps * ps, hd = ne / c->n_head;
    int32_t r = zt_nn_init(&nn, 256, cols_max(c), P, hd, c->n_head, w.nn_mem, w.nn_bytes);
    if (r) return r;
    nn.kern = v->kern;
    /* patches: column ch * ps * ps + y * ps + x (GGUF [ps, ps, 3, n_embd]) */
    for (uint32_t py = 0; py < side; py++)
        for (uint32_t px = 0; px < side; px++) {
            zt_fx *row = w.patches + (uint64_t) (py * side + px) * pc;
            for (uint32_t ch = 0; ch < 3; ch++)
                for (uint32_t y = 0; y < ps; y++)
                    for (uint32_t x = 0; x < ps; x++) {
                        uint32_t p = rgb[((uint64_t) (py * ps + y) * S + (uint64_t) px * ps + x) * 3 + ch];
                        /* p / 255 in Q16, rounded, then (that - mean) / std */
                        int64_t u = (int64_t) ((p * 65536u + 127u) / 255u) - c->mean[ch];
                        int64_t q = u * c->inv_std[ch];
                        row[ch * ps * ps + y * ps + x] =
                            (zt_fx) (q >= 0 ? (q + 32768) >> 16 : -((-q + 32768) >> 16));
                    }
        }
    zt_nn_linear(&nn, &v->patch_w, v->patch_b, w.patches, pc, pc, P, w.x, ne);
    zt_nn_add(&nn, w.x, ne, v->pos, ne, ne, P);
    for (uint32_t l = 0; l < c->n_layer; l++) {
        const zt_vision_layer_t *L = &v->layers[l];
        zt_nn_layernorm(&nn, w.x, ne, ne, P, &L->ln1, c->eps_f32, w.xn, ne);
        zt_nn_linear(&nn, &L->wq, L->bq, w.xn, ne, ne, P, w.q, ne);
        zt_nn_linear(&nn, &L->wk, L->bk, w.xn, ne, ne, P, w.k, ne);
        zt_nn_linear(&nn, &L->wv, L->bv, w.xn, ne, ne, P, w.v, ne);
        zt_nn_attention(&nn, w.q, ne, w.k, ne, w.v, ne, P, P, c->n_head, hd, false, w.att, ne);
        zt_nn_linear(&nn, &L->wo, L->bo, w.att, ne, ne, P, w.xn, ne);
        zt_nn_add(&nn, w.x, ne, w.xn, ne, ne, P);
        zt_nn_layernorm(&nn, w.x, ne, ne, P, &L->ln2, c->eps_f32, w.xn, ne);
        zt_nn_linear(&nn, &L->w1, L->b1, w.xn, ne, ne, P, w.h, nf);
        zt_nn_act(&nn, w.h, nf, nf, P, ZT_NN_GELU_TANH);
        zt_nn_linear(&nn, &L->w2, L->b2, w.h, nf, nf, P, w.xn, ne);
        zt_nn_add(&nn, w.x, ne, w.xn, ne, ne, P);
    }
    zt_nn_layernorm(&nn, w.x, ne, ne, P, &v->post_ln, c->eps_f32, w.xn, ne);
    /* pixel shuffle: output (hb, wb) takes patch (hb*s + hi, wb*s + wi) at
     * column (hi * s + wi) * ne */
    uint32_t s = c->scale, ob = side / s, pin = ne * s * s;
    zt_fx *sh = w.att; /* n_out x pin == P x ne values */
    for (uint32_t hb = 0; hb < ob; hb++)
        for (uint32_t wb = 0; wb < ob; wb++)
            for (uint32_t hi = 0; hi < s; hi++)
                for (uint32_t wi = 0; wi < s; wi++) {
                    const zt_fx *src = w.xn + (uint64_t) ((hb * s + hi) * side + wb * s + wi) * ne;
                    zt_fx *dst = sh + (uint64_t) (hb * ob + wb) * pin + (hi * s + wi) * ne;
                    for (uint32_t i = 0; i < ne; i++) dst[i] = src[i];
                }
    zt_nn_linear(&nn, &v->proj, 0, sh, pin, pin, c->n_out, out, c->proj_dim);
    if (n_sat) *n_sat = nn.n_sat;
    return ZT_GGUF_OK;
}

/* ---- resize ---- */

/* one axis: dst[i] = sum over source pixels of overlap * src / n_src, where
 * output pixel i covers [i * n_src, (i + 1) * n_src) and source pixel j
 * covers [j * n_dst, (j + 1) * n_dst) (both in units of 1 / (n_src n_dst)) */
void zt_vision_resize(const uint8_t *src, uint32_t w, uint32_t h, uint32_t stride, uint8_t *dst,
                      uint32_t size)
{
    for (uint32_t oy = 0; oy < size; oy++) {
        uint64_t y0 = (uint64_t) oy * h, y1 = y0 + h; /* in 1/size units of source rows */
        for (uint32_t ox = 0; ox < size; ox++) {
            uint64_t x0 = (uint64_t) ox * w, x1 = x0 + w;
            uint64_t acc[3] = {0, 0, 0};
            for (uint64_t sy = zt_udiv64(y0, size, 0); sy * size < y1 && sy < h; sy++) {
                uint64_t a = sy * size > y0 ? sy * size : y0;
                uint64_t b = (sy + 1) * size < y1 ? (sy + 1) * size : y1;
                uint64_t wy = b - a;
                for (uint64_t sx = zt_udiv64(x0, size, 0); sx * size < x1 && sx < w; sx++) {
                    uint64_t c = sx * size > x0 ? sx * size : x0;
                    uint64_t d = (sx + 1) * size < x1 ? (sx + 1) * size : x1;
                    uint64_t wt = wy * (d - c);
                    const uint8_t *p = src + sy * stride + sx * 3;
                    for (int ch = 0; ch < 3; ch++) acc[ch] += wt * p[ch];
                }
            }
            uint64_t tot = (uint64_t) w * h; /* the weights of one output pixel sum to w * h */
            for (int ch = 0; ch < 3; ch++)
                dst[((uint64_t) oy * size + ox) * 3 + ch] =
                    (uint8_t) zt_udiv64(acc[ch] + tot / 2, tot, 0);
        }
    }
}
