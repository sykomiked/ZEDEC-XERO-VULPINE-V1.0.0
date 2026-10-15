/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_kokoro.c — Kokoro-82M speech out in integers (T27). See zt_kokoro.h. */
#include "zt_kokoro.h"
#include "zt_kern.h"
#include "zt_tts.h"

#define CHUNK      64u
#define MAX_COLS   3296u /* 1090 * 3 rounded up to 32 */
#define HID        512u
#define STY        128u
#define BERT       768u
#define BERT_FF    2048u
#define ISQRT2_Q16 46341 /* round(2^16 / sqrt 2) */
#define UP         300u  /* F0 steps to samples */
#define NFFT       20u
#define NBIN       11u
#define HOP        5u
#define GEN_C1     256u
#define GEN_C2     128u

/* ---- small integer helpers ---- */

static zt_fx sat32(int64_t v)
{
    return (zt_fx) (v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}

static int64_t rshr(int64_t v, uint32_t s)
{
    if (!s) return v;
    return (v + ((int64_t) 1 << (s - 1))) >> s;
}

static int64_t round_div(int64_t n, uint64_t d)
{
    uint64_t a = n < 0 ? (uint64_t) -n : (uint64_t) n;
    uint64_t q = zt_udiv64(a + d / 2, d, 0);
    return n < 0 ? -(int64_t) q : (int64_t) q;
}

static uint32_t bitlen64(uint64_t v)
{
    uint32_t n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static zt_fx gain_mul(zt_fx x, const zt_gain_t *g, uint32_t i)
{
    return sat32(rshr((int64_t) x * g->g[i], g->frac));
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

/* "<a><n>.<b>" with n < 0 meaning no number */
static void nm(char *dst, const char *a, int32_t n, const char *b)
{
    uint32_t o = 0;
    for (; *a && o < 95; a++) dst[o++] = *a;
    if (n >= 0) {
        char d[12];
        uint32_t k = 0;
        uint32_t u = (uint32_t) n;
        do {
            d[k++] = (char) ('0' + u % 10);
            u /= 10;
        } while (u);
        while (k && o < 95) dst[o++] = d[--k];
    }
    for (; b && *b && o < 95; b++) dst[o++] = *b;
    dst[o] = 0;
}

/* ---- loading ---- */

static int32_t lin(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, uint32_t cols,
                   uint32_t rows, bool bias, zt_kk_lin_t *L)
{
    char w[100];
    nm(w, base, -1, ".weight");
    int32_t r = zt_nn_load_mat(a, g, w, cols, rows, &L->w);
    if (r) return r;
    L->b = 0;
    if (!bias) return ZT_GGUF_OK;
    nm(w, base, -1, ".bias");
    return zt_nn_load_q16(a, g, w, rows, &L->b);
}

static int32_t vec(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, const char *suf,
                   uint32_t n, const zt_fx **out)
{
    char w[100];
    nm(w, base, -1, suf);
    return zt_nn_load_q16(a, g, w, n, out);
}

static int32_t gain(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, const char *suf,
                    uint32_t n, zt_gain_t *out)
{
    char w[100];
    nm(w, base, -1, suf);
    return zt_nn_load_gain(a, g, w, n, out);
}

static int32_t lstm(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, uint32_t in,
                    uint32_t hid, zt_kk_lstm_t *L)
{
    char w[100];
    int32_t r;
    L->in = in;
    L->hid = hid;
    for (uint32_t d = 0; d < 2; d++) {
        zt_kk_dir_t *D = d ? &L->bw : &L->fw;
        const char *tag = d ? ".bw" : ".fw";
        char p[100];
        nm(p, base, -1, tag);
        nm(w, p, -1, ".wi");
        if ((r = zt_nn_load_mat(a, g, w, in, 4 * hid, &D->wi.w))) return r;
        nm(w, p, -1, ".b");
        if ((r = zt_nn_load_q16(a, g, w, 4 * hid, &D->wi.b))) return r;
        nm(w, p, -1, ".wh");
        if ((r = zt_nn_load_mat(a, g, w, hid, 4 * hid, &D->wh))) return r;
    }
    return ZT_GGUF_OK;
}

static int32_t blk(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, uint32_t cin,
                   uint32_t cout, bool up, zt_kk_blk_t *B)
{
    char p[100];
    int32_t r;
    B->cin = cin;
    B->cout = cout;
    B->up = up;
    nm(p, base, -1, ".norm1");
    if ((r = lin(a, g, p, STY, 2 * cin, true, &B->n1))) return r;
    nm(p, base, -1, ".norm2");
    if ((r = lin(a, g, p, STY, 2 * cout, true, &B->n2))) return r;
    nm(p, base, -1, ".conv1");
    if ((r = lin(a, g, p, cin * 3, cout, true, &B->c1))) return r;
    nm(p, base, -1, ".conv2");
    if ((r = lin(a, g, p, cout * 3, cout, true, &B->c2))) return r;
    B->has_sc = cin != cout;
    if (B->has_sc) {
        nm(p, base, -1, ".conv1x1.weight");
        if ((r = zt_nn_load_mat(a, g, p, cin, cout, &B->sc))) return r;
    }
    if (up) {
        nm(p, base, -1, ".pool.weight");
        if ((r = zt_nn_load_gain(a, g, p, (uint64_t) cin * 3, &B->pool_w))) return r;
        if ((r = vec(a, g, base, ".pool.bias", cin, &B->pool_b))) return r;
    }
    return ZT_GGUF_OK;
}

/* 1/alpha in Q16 from alpha (gain with frac bits) */
static int32_t inv_alpha(zt_nn_arena_t *a, const zt_gain_t *al, uint32_t C, const zt_fx **out)
{
    zt_fx *v = zt_nn_take(a, (uint64_t) C * 4);
    *out = v;
    if (!zt_nn_filling(a)) return ZT_GGUF_OK;
    if (!v) return ZT_NN_ESPACE;
    for (uint32_t c = 0; c < C; c++) {
        int64_t gv = al->g[c];
        if (gv == 0) {
            v[c] = 0;
            continue;
        }
        uint64_t num = (uint64_t) 1 << (16 + al->frac);
        uint64_t den = (uint64_t) (gv < 0 ? -gv : gv);
        int64_t q = (int64_t) zt_udiv64(num + den / 2, den, 0);
        v[c] = sat32(gv < 0 ? -q : q);
    }
    return ZT_GGUF_OK;
}

static int32_t res1(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, uint32_t C, uint32_t K,
                    zt_kk_res_t *R)
{
    char p[100], q[100];
    int32_t r;
    R->C = C;
    R->K = K;
    for (int32_t j = 0; j < 3; j++) {
        nm(p, base, -1, ".");
        nm(q, p, j, ".adain1");
        if ((r = lin(a, g, q, STY, 2 * C, true, &R->a1[j]))) return r;
        nm(q, p, j, ".adain2");
        if ((r = lin(a, g, q, STY, 2 * C, true, &R->a2[j]))) return r;
        nm(q, p, j, ".convs1");
        if ((r = lin(a, g, q, C * K, C, true, &R->c1[j]))) return r;
        nm(q, p, j, ".convs2");
        if ((r = lin(a, g, q, C * K, C, true, &R->c2[j]))) return r;
        nm(q, p, j, ".alpha1");
        if ((r = zt_nn_load_gain(a, g, q, C, &R->al1[j]))) return r;
        nm(q, p, j, ".alpha2");
        if ((r = zt_nn_load_gain(a, g, q, C, &R->al2[j]))) return r;
        if ((r = inv_alpha(a, &R->al1[j], C, &R->inv1[j]))) return r;
        if ((r = inv_alpha(a, &R->al2[j], C, &R->inv2[j]))) return r;
    }
    return ZT_GGUF_OK;
}

static int32_t norm(zt_nn_arena_t *a, const zt_gguf_t *g, const char *base, uint32_t n,
                    zt_nn_norm_t *N)
{
    int32_t r = gain(a, g, base, ".weight", n, &N->g);
    return r ? r : vec(a, g, base, ".bias", n, &N->b);
}

typedef struct {
    zt_kokoro_t *k;
} vocab_ctx_t;

static bool vocab_fn(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    zt_kokoro_t *k = ((vocab_ctx_t *) ctx)->k;
    if (i >= 256) return false;
    uint32_t cp = 0;
    const uint8_t *p = s.p;
    if (s.len == 1 && p[0] < 0x80)
        cp = p[0];
    else if (s.len == 2 && (p[0] & 0xE0) == 0xC0)
        cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    else if (s.len == 3 && (p[0] & 0xF0) == 0xE0)
        cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    else if (s.len == 4 && (p[0] & 0xF8) == 0xF0)
        cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) |
             (p[3] & 0x3Fu);
    k->vocab_cp[i] = cp;
    return true;
}

int32_t zt_kokoro_config(const zt_gguf_t *g, zt_kokoro_cfg_t *c, zt_model_err_t *err)
{
    if (!g || !c) {
        set_err(err, ZT_NN_EARG, "null argument", 0);
        return ZT_NN_EARG;
    }
    zt_gguf_val_t v;
    if (zt_gguf_find(g, "general.architecture", &v) || !zt_gguf_str_eq(v.str, "kokoro")) {
        set_err(err, ZT_NN_ECONFIG, "not a kokoro file", "general.architecture");
        return ZT_NN_ECONFIG;
    }
    c->n_token = (uint32_t) zt_gguf_get_int(g, "kokoro.n_token", 0);
    c->style = (uint32_t) zt_gguf_get_int(g, "kokoro.style_dim", 0);
    c->hidden = (uint32_t) zt_gguf_get_int(g, "kokoro.hidden_dim", 0);
    c->max_dur = (uint32_t) zt_gguf_get_int(g, "kokoro.max_dur", 0);
    if (c->n_token == 0 || c->n_token > 256 || c->style != STY || c->hidden != HID ||
        c->max_dur == 0 || c->max_dur > 256) {
        set_err(err, ZT_NN_ECONFIG, "unsupported kokoro shape", 0);
        return ZT_NN_ECONFIG;
    }
    return ZT_GGUF_OK;
}

static int32_t plan(const zt_gguf_t *g, zt_kokoro_t *k, zt_nn_arena_t *a)
{
    int32_t r;
    char p[100];
    uint32_t NT = k->cfg.n_token;
    if ((r = vec(a, g, "bert.word_embd", "", NT * 128u, &k->word))) return r;
    if ((r = vec(a, g, "bert.pos_embd", "", ZT_KOKORO_MAX_TOK * 128u, &k->pos))) return r;
    if ((r = vec(a, g, "bert.type_embd", "", 128u, &k->type))) return r;
    if ((r = norm(a, g, "bert.emb_norm", 128, &k->emb_norm))) return r;
    if ((r = lin(a, g, "bert.map", 128, BERT, true, &k->map))) return r;
    if ((r = lin(a, g, "bert.q", BERT, BERT, true, &k->q))) return r;
    if ((r = lin(a, g, "bert.k", BERT, BERT, true, &k->k))) return r;
    if ((r = lin(a, g, "bert.v", BERT, BERT, true, &k->v))) return r;
    if ((r = lin(a, g, "bert.o", BERT, BERT, true, &k->o))) return r;
    if ((r = norm(a, g, "bert.attn_norm", BERT, &k->attn_norm))) return r;
    if ((r = lin(a, g, "bert.ffn_up", BERT, BERT_FF, true, &k->up))) return r;
    if ((r = lin(a, g, "bert.ffn_down", BERT_FF, BERT, true, &k->down))) return r;
    if ((r = norm(a, g, "bert.ffn_norm", BERT, &k->ffn_norm))) return r;
    if ((r = lin(a, g, "bert_enc", BERT, HID, true, &k->bert_enc))) return r;
    for (int32_t l = 0; l < 3; l++) {
        nm(p, "pred.dur_enc.", l, ".lstm");
        if ((r = lstm(a, g, p, HID + STY, HID / 2, &k->dur_lstm[l]))) return r;
        nm(p, "pred.dur_enc.", l, ".norm");
        if ((r = lin(a, g, p, STY, 2 * HID, true, &k->dur_norm[l]))) return r;
    }
    if ((r = lstm(a, g, "pred.lstm", HID + STY, HID / 2, &k->lstm))) return r;
    if ((r = lin(a, g, "pred.dur_proj", HID, k->cfg.max_dur, true, &k->dur_proj))) return r;
    if ((r = lstm(a, g, "pred.shared", HID + STY, HID / 2, &k->shared))) return r;
    static const uint32_t ci[3] = {HID, HID, HID / 2}, co[3] = {HID, HID / 2, HID / 2};
    for (int32_t b = 0; b < 3; b++) {
        nm(p, "pred.F0.", b, "");
        if ((r = blk(a, g, p, ci[b], co[b], b == 1, &k->f0[b]))) return r;
        nm(p, "pred.N.", b, "");
        if ((r = blk(a, g, p, ci[b], co[b], b == 1, &k->n[b]))) return r;
    }
    if ((r = lin(a, g, "pred.F0_proj", HID / 2, 1, true, &k->f0_proj))) return r;
    if ((r = lin(a, g, "pred.N_proj", HID / 2, 1, true, &k->n_proj))) return r;
    if ((r = vec(a, g, "text.embd", "", NT * HID, &k->text_embd))) return r;
    for (int32_t c = 0; c < 3; c++) {
        nm(p, "text.cnn.", c, ".conv");
        if ((r = lin(a, g, p, HID * 5, HID, true, &k->cnn[c]))) return r;
        nm(p, "text.cnn.", c, ".norm");
        if ((r = norm(a, g, p, HID, &k->cnn_norm[c]))) return r;
    }
    if ((r = lstm(a, g, "text.lstm", HID, HID / 2, &k->text_lstm))) return r;
    if ((r = gain(a, g, "dec.F0_conv", ".weight", 3, &k->f0c_w))) return r;
    if ((r = vec(a, g, "dec.F0_conv", ".bias", 1, &k->f0c_b))) return r;
    if ((r = gain(a, g, "dec.N_conv", ".weight", 3, &k->nc_w))) return r;
    if ((r = vec(a, g, "dec.N_conv", ".bias", 1, &k->nc_b))) return r;
    if ((r = lin(a, g, "dec.asr_res", HID, 64, true, &k->asr_res))) return r;
    if ((r = blk(a, g, "dec.encode", HID + 2, 1024, false, &k->encode))) return r;
    for (int32_t d = 0; d < 4; d++) {
        nm(p, "dec.decode.", d, "");
        if ((r = blk(a, g, p, 1024 + 2 + 64, d == 3 ? 512 : 1024, d == 3, &k->decode[d]))) return r;
    }
    if ((r = gain(a, g, "gen.l_linear", ".weight", 9, &k->l_lin))) return r;
    if ((r = vec(a, g, "gen.l_linear", ".bias", 1, &k->l_lin_b))) return r;
    if ((r = lin(a, g, "gen.noise_conv.0", 22 * 12, GEN_C1, true, &k->noise_conv[0]))) return r;
    if ((r = lin(a, g, "gen.noise_conv.1", 22, GEN_C2, true, &k->noise_conv[1]))) return r;
    if ((r = res1(a, g, "gen.noise_res.0", GEN_C1, 7, &k->noise_res[0]))) return r;
    if ((r = res1(a, g, "gen.noise_res.1", GEN_C2, 11, &k->noise_res[1]))) return r;
    for (int32_t ph = 0; ph < 10; ph++) {
        nm(p, "gen.ups.0.p", ph, "");
        if ((r = lin(a, g, p, 512 * 2, GEN_C1, false, &k->ups_p0[ph]))) return r;
    }
    for (int32_t ph = 0; ph < 6; ph++) {
        nm(p, "gen.ups.1.p", ph, "");
        if ((r = lin(a, g, p, GEN_C1 * 2, GEN_C2, false, &k->ups_p1[ph]))) return r;
    }
    if ((r = vec(a, g, "gen.ups.0", ".bias", GEN_C1, &k->ups_b[0]))) return r;
    if ((r = vec(a, g, "gen.ups.1", ".bias", GEN_C2, &k->ups_b[1]))) return r;
    static const uint32_t ks[3] = {3, 7, 11};
    for (int32_t i = 0; i < 6; i++) {
        nm(p, "gen.res.", i, "");
        if ((r = res1(a, g, p, i < 3 ? GEN_C1 : GEN_C2, ks[i % 3], &k->res[i]))) return r;
    }
    if ((r = lin(a, g, "gen.post", GEN_C2 * 7, 22, true, &k->post))) return r;
    return ZT_GGUF_OK;
}

uint64_t zt_kokoro_arena_bytes(const zt_gguf_t *g)
{
    zt_kokoro_t k;
    if (zt_kokoro_config(g, &k.cfg, 0)) return 0;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, MAX_COLS, 0);
    if (plan(g, &k, &a)) return 0;
    return zt_nn_arena_used(&a);
}

int32_t zt_kokoro_load(zt_kokoro_t *k, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                       zt_model_err_t *err)
{
    if (!k || !g || !arena) {
        set_err(err, ZT_NN_EARG, "null argument", 0);
        return ZT_NN_EARG;
    }
    int32_t r = zt_kokoro_config(g, &k->cfg, err);
    if (r) return r;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, MAX_COLS, err);
    if ((r = plan(g, k, &a))) return r;
    if (zt_nn_arena_used(&a) > arena_bytes) {
        set_err(err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    zt_nn_arena_begin(&a, arena, arena_bytes, MAX_COLS, err);
    if ((r = plan(g, k, &a))) return r;
    zt_gguf_val_t v;
    for (uint32_t i = 0; i < 256; i++) k->vocab_cp[i] = 0;
    if (zt_gguf_find(g, "kokoro.vocab", &v) == ZT_GGUF_OK) {
        vocab_ctx_t c = {k};
        zt_gguf_arr_strings(g, &v, vocab_fn, &c);
    }
    k->g = g;
    k->kern = 0;
    set_err(err, ZT_GGUF_OK, "ok", 0);
    return ZT_GGUF_OK;
}

/* ---- phonemes and voices ---- */

uint32_t zt_kokoro_ids(const zt_kokoro_t *k, const char *ph, uint32_t len, int32_t *ids,
                       uint32_t cap)
{
    if (!k || !ph || !ids || cap < 3) return 0;
    uint32_t n = 0;
    ids[n++] = 0;
    const uint8_t *s = (const uint8_t *) ph;
    for (uint32_t i = 0; i < len;) {
        uint32_t cp = s[i], w = 1;
        if (cp >= 0xF0 && i + 3 < len) {
            cp = ((s[i] & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) |
                 (s[i + 3] & 0x3Fu);
            w = 4;
        } else if (cp >= 0xE0 && i + 2 < len) {
            cp = ((s[i] & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
            w = 3;
        } else if (cp >= 0xC0 && i + 1 < len) {
            cp = ((s[i] & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
            w = 2;
        } else if (cp >= 0x80) {
            cp = 0; /* a stray byte */
        }
        i += w;
        if (!cp) continue;
        for (uint32_t id = 1; id < k->cfg.n_token && id < 256; id++)
            if (k->vocab_cp[id] == cp) {
                if (n + 1 >= cap || n + 1 >= ZT_KOKORO_MAX_TOK) return 0;
                ids[n++] = (int32_t) id;
                break;
            }
    }
    if (n == 1) return 0;
    ids[n++] = 0;
    return n;
}

int32_t zt_kokoro_voice(const zt_kokoro_t *k, const char *name, uint32_t n_phonemes, zt_fx *style)
{
    if (!k || !name || !style || !k->g) return ZT_NN_EARG;
    char w[100];
    nm(w, "voice.", -1, name);
    zt_gguf_tensor_t t;
    if (zt_gguf_find_tensor(k->g, w, &t) || t.n_dims != 2 || t.dims[0] != ZT_KOKORO_STYLE ||
        t.dims[1] == 0)
        return ZT_NN_EMISSING;
    uint64_t row = n_phonemes ? n_phonemes - 1 : 0;
    if (row >= t.dims[1]) row = t.dims[1] - 1;
    return zt_gguf_dequant(&t, row * ZT_KOKORO_STYLE, ZT_KOKORO_STYLE, style) ? ZT_NN_ESHAPE
                                                                              : ZT_GGUF_OK;
}

/* ---- work memory ---- */

typedef struct {
    uint32_t max_tok, max_frames;
    zt_fx *A, *B, *C, *D, *E, *Fb; /* general buffers, `big` values each */
    uint64_t big;
    zt_fx *col;         /* CHUNK * MAX_COLS */
    zt_fx *sty;         /* 2 * 2180 style gains and biases */
    zt_fx *z, *h, *c;   /* LSTM step: 4 * 256, 256, 256 */
    int64_t *s1, *s2;   /* per-channel sums: 2180 */
    zt_fx *one;         /* unit gains, 2180 */
    int32_t *dur;       /* max_tok */
    zt_fx *f0, *nn2;    /* 2 * max_frames */
    zt_fx *har;         /* (120 F + 1) * 22 */
    zt_fx *src;         /* 600 F */
    uint64_t *cum;      /* 2 F * 9: phase in turns * 2^32 */
    int64_t *acc, *env; /* iSTFT: 600 F + NFFT */
    void *nn_mem;
    uint64_t nn_bytes;
} kwork_t;

static uint64_t big_vals(uint32_t T, uint32_t F)
{
    uint64_t m = (uint64_t) T * BERT_FF;
    uint64_t s2 = ((uint64_t) 120 * F + 1) * GEN_C2;
    uint64_t s1 = (uint64_t) 20 * F * GEN_C1;
    uint64_t d = (uint64_t) 2 * F * 1090;
    if (s2 > m) m = s2;
    if (s1 > m) m = s1;
    if (d > m) m = d;
    if ((uint64_t) T * 4 * HID > m) m = (uint64_t) T * 4 * HID;
    if ((uint64_t) F * 4 * HID > m) m = (uint64_t) F * 4 * HID;
    return m;
}

static uint64_t layout(uint32_t T, uint32_t F, uint8_t *base, kwork_t *w)
{
    uint64_t o = 0;
#define KTAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    w->max_tok = T;
    w->max_frames = F;
    w->big = big_vals(T, F);
    KTAKE(w->A, w->big * 4);
    KTAKE(w->B, w->big * 4);
    KTAKE(w->C, w->big * 4);
    KTAKE(w->D, w->big * 4);
    KTAKE(w->E, w->big * 4);
    KTAKE(w->Fb, w->big * 4);
    KTAKE(w->col, (uint64_t) CHUNK * MAX_COLS * 4);
    KTAKE(w->sty, 2 * 2180 * 4);
    KTAKE(w->z, 4 * 256 * 4);
    KTAKE(w->h, 256 * 4);
    KTAKE(w->c, 256 * 4);
    KTAKE(w->s1, 2180 * 8);
    KTAKE(w->s2, 2180 * 8);
    KTAKE(w->one, 2180 * 4);
    KTAKE(w->dur, (uint64_t) T * 4);
    KTAKE(w->f0, (uint64_t) 2 * F * 4);
    KTAKE(w->nn2, (uint64_t) 2 * F * 4);
    KTAKE(w->har, ((uint64_t) 120 * F + 1) * 22 * 4);
    KTAKE(w->src, (uint64_t) 600 * F * 4);
    KTAKE(w->cum, (uint64_t) 2 * F * 9 * 8);
    KTAKE(w->acc, ((uint64_t) 600 * F + 2 * NFFT) * 8);
    KTAKE(w->env, ((uint64_t) 600 * F + 2 * NFFT) * 8);
    uint32_t keys = T > 32 ? T : 32;
    w->nn_bytes = zt_nn_bytes(CHUNK, MAX_COLS, keys, 64, 12);
    KTAKE(w->nn_mem, w->nn_bytes);
#undef KTAKE
    return o + 64;
}

uint64_t zt_kokoro_work_bytes(const zt_kokoro_t *k, uint32_t max_tok, uint32_t max_frames)
{
    (void) k;
    if (!max_tok || !max_frames || max_tok > ZT_KOKORO_MAX_TOK) return 0;
    kwork_t w;
    return layout(max_tok, max_frames, 0, &w);
}

/* ---- blocks ---- */

/* y [T][rows] = conv1d of x [T][C] (kernel K, stride, pad, dilation) */
static uint32_t conv(zt_nn_t *nn, const zt_kk_lin_t *L, const zt_fx *x, uint32_t xstride,
                     uint32_t T, uint32_t C, uint32_t K, uint32_t stride, uint32_t pad,
                     uint32_t dil, zt_fx *col, zt_fx *y, uint32_t ystride)
{
    int64_t span = (int64_t) dil * (K - 1) + 1;
    if ((int64_t) T + 2 * pad < span) return 0;
    uint32_t To = (T + 2 * pad - (uint32_t) span) / stride + 1;
    uint32_t CK = C * K;
    for (uint32_t t0 = 0; t0 < To; t0 += CHUNK) {
        uint32_t B = To - t0 < CHUNK ? To - t0 : CHUNK;
        for (uint32_t t = 0; t < B; t++) {
            zt_fx *o = col + (uint64_t) t * CK;
            int64_t s0 = (int64_t) (t0 + t) * stride - pad;
            for (uint32_t j = 0; j < K; j++) {
                int64_t s = s0 + (int64_t) j * dil;
                bool in = s >= 0 && s < T;
                const zt_fx *xr = in ? x + (uint64_t) s * xstride : 0;
                for (uint32_t c = 0; c < C; c++) o[c * K + j] = in ? xr[c] : 0;
            }
        }
        zt_nn_linear(nn, &L->w, L->b, col, CK, CK, B, y + (uint64_t) t0 * ystride, ystride);
    }
    return To;
}

static void lrelu(zt_fx *x, uint64_t n, int32_t slope_q16)
{
    for (uint64_t i = 0; i < n; i++)
        if (x[i] < 0) x[i] = (zt_fx) rshr((int64_t) x[i] * slope_q16, 16);
}

/* the voice's gamma and beta for one AdaIN / AdaLayerNorm: w->sty[0..C) is
 * 1 + gamma (Q16), w->sty[C..2C) is beta */
static void style_affine(zt_nn_t *nn, const zt_kk_lin_t *fc, const zt_fx *s, uint32_t C, kwork_t *w)
{
    zt_nn_linear(nn, &fc->w, fc->b, s, STY, STY, 1, w->sty, 2 * C);
    for (uint32_t c = 0; c < C; c++) w->sty[c] = sat32((int64_t) w->sty[c] + 65536);
}

/* 1/sqrt(mean + eps) in Q32 from a sum of squares ss of n values each shifted
 * down by sh bits (zt_nn.c's inv_rms) */
static uint64_t inv_rms(uint64_t ss, uint32_t sh, uint64_t n, uint64_t eps_q48)
{
    uint64_t M = zt_udiv64(ss, n, 0);
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
        return (up >= 63 || (q0 >> (62 - up))) ? (1ull << 62) : q0 << up;
    }
    return N <= 0 ? 0 : q0 >> (63 - N);
}

#define EPS5_Q48 2814749767ull /* round(1e-5 * 2^48) */

/* AdaIN1d: per channel over time, (1 + gamma) (x - mean) / sqrt(var + 1e-5)
 * + beta, in place on x [T][C] */
static void adain(zt_nn_t *nn, const zt_kk_lin_t *fc, const zt_fx *s, zt_fx *x, uint32_t T,
                  uint32_t C, kwork_t *w)
{
    style_affine(nn, fc, s, C, w);
    for (uint32_t c = 0; c < C; c++) w->s1[c] = 0;
    for (uint64_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < C; c++) w->s1[c] += x[t * C + c];
    for (uint32_t c = 0; c < C; c++) {
        int64_t mean = round_div(w->s1[c], T);
        uint32_t amax = 0;
        for (uint64_t t = 0; t < T; t++) {
            int64_t d = (int64_t) x[t * C + c] - mean;
            uint32_t a = (uint32_t) (d < 0 ? -d : d);
            if (d > UINT32_MAX || -d > UINT32_MAX) a = UINT32_MAX;
            if (a > amax) amax = a;
        }
        uint32_t b = 2 * bitlen64(amax) + bitlen64(T);
        uint32_t sh = b > 60 ? b - 60 : 0;
        uint64_t ss = 0;
        for (uint64_t t = 0; t < T; t++) {
            int64_t d = (int64_t) x[t * C + c] - mean;
            ss += (uint64_t) (d * d) >> sh;
        }
        uint64_t inv = inv_rms(ss, sh, T, EPS5_Q48);
        int64_t g1 = w->sty[c], be = w->sty[C + c];
        for (uint64_t t = 0; t < T; t++) {
            int64_t d = (int64_t) x[t * C + c] - mean;
            int64_t v = (d * (int64_t) inv + ((int64_t) 1 << 31)) >> 32; /* Q16 */
            v = rshr(v * g1, 16) + be;
            x[t * C + c] = sat32(v);
        }
    }
    (void) nn;
}

/* AdaLayerNorm over the channels of each row, in place on x [T][C] */
static void adaln(zt_nn_t *nn, const zt_kk_lin_t *fc, const zt_fx *s, zt_fx *x, uint32_t xstride,
                  uint32_t T, uint32_t C, kwork_t *w)
{
    style_affine(nn, fc, s, C, w);
    zt_nn_norm_t p = {{w->sty, 16}, w->sty + C};
    zt_nn_layernorm(nn, x, xstride, C, T, &p, 0x3727C5ACu /* 1e-5 */, x, xstride);
}

static zt_fx tanh_q16(zt_fx x)
{
    zt_fx s = zt_nn_act1(sat32((int64_t) x * 2), ZT_NN_SIGMOID);
    return sat32((int64_t) s * 2 - 65536);
}

/* one direction of an LSTM over x [T][in] into y (column offset applied by
 * the caller) [T][ystride] */
static void lstm_dir(zt_nn_t *nn, const zt_kk_dir_t *D, uint32_t H, const zt_fx *x,
                     uint32_t xstride, uint32_t in, uint32_t T, bool rev, zt_fx *xi, zt_fx *y,
                     uint32_t ystride, kwork_t *w)
{
    zt_nn_linear(nn, &D->wi.w, D->wi.b, x, xstride, in, T, xi, 4 * H);
    for (uint32_t i = 0; i < H; i++) w->h[i] = w->c[i] = 0;
    for (uint32_t s = 0; s < T; s++) {
        uint32_t t = rev ? T - 1 - s : s;
        zt_nn_linear(nn, &D->wh, 0, w->h, H, H, 1, w->z, 4 * H);
        const zt_fx *pre = xi + (uint64_t) t * 4 * H;
        for (uint32_t i = 0; i < H; i++) {
            zt_fx gi = zt_nn_act1(sat32((int64_t) w->z[i] + pre[i]), ZT_NN_SIGMOID);
            zt_fx gf = zt_nn_act1(sat32((int64_t) w->z[H + i] + pre[H + i]), ZT_NN_SIGMOID);
            zt_fx gg = tanh_q16(sat32((int64_t) w->z[2 * H + i] + pre[2 * H + i]));
            zt_fx go = zt_nn_act1(sat32((int64_t) w->z[3 * H + i] + pre[3 * H + i]), ZT_NN_SIGMOID);
            int64_t c = rshr((int64_t) gf * w->c[i], 16) + rshr((int64_t) gi * gg, 16);
            w->c[i] = sat32(c);
            w->h[i] = sat32(rshr((int64_t) go * tanh_q16(w->c[i]), 16));
        }
        zt_fx *yr = y + (uint64_t) t * ystride;
        for (uint32_t i = 0; i < H; i++) yr[i] = w->h[i];
    }
}

/* BiLSTM: y [T][2H] (fw then bw); xi is scratch [T][4H] */
static void bilstm(zt_nn_t *nn, const zt_kk_lstm_t *L, const zt_fx *x, uint32_t xstride, uint32_t T,
                   zt_fx *xi, zt_fx *y, uint32_t ystride, kwork_t *w)
{
    lstm_dir(nn, &L->fw, L->hid, x, xstride, L->in, T, false, xi, y, ystride, w);
    lstm_dir(nn, &L->bw, L->hid, x, xstride, L->in, T, true, xi, y + L->hid, ystride, w);
}

/* AdainResBlk1d: x [T][cin] -> y [T or 2T][cout]; uses t1, t2 as scratch */
static uint32_t resblk(zt_nn_t *nn, const zt_kk_blk_t *B, const zt_fx *s, const zt_fx *x,
                       uint32_t T, zt_fx *y, zt_fx *t1, zt_fx *t2, kwork_t *w)
{
    uint32_t ci = B->cin, co = B->cout, To = B->up ? 2 * T : T;
    for (uint64_t i = 0; i < (uint64_t) T * ci; i++) t1[i] = x[i];
    adain(nn, &B->n1, s, t1, T, ci, w);
    lrelu(t1, (uint64_t) T * ci, 13107); /* 0.2 */
    const zt_fx *r = t1;
    if (B->up) { /* depthwise ConvTranspose1d k3 s2 p1 op1 */
        for (uint64_t j = 0; j < T; j++)
            for (uint32_t c = 0; c < ci; c++) {
                int64_t xj = t1[j * ci + c], xn = j + 1 < T ? t1[(j + 1) * ci + c] : 0;
                const zt_gain_t *g = &B->pool_w;
                int64_t ev = rshr(xj * g->g[c * 3 + 1], g->frac);
                int64_t od = rshr(xn * g->g[c * 3 + 0] + xj * g->g[c * 3 + 2], g->frac);
                t2[(2 * j) * ci + c] = sat32(ev + B->pool_b[c]);
                t2[(2 * j + 1) * ci + c] = sat32(od + B->pool_b[c]);
            }
        r = t2;
    }
    /* conv1 into y (as scratch), norm2, conv2 into t1 */
    conv(nn, &B->c1, r, ci, To, ci, 3, 1, 1, 1, w->col, y, co);
    adain(nn, &B->n2, s, y, To, co, w);
    lrelu(y, (uint64_t) To * co, 13107);
    conv(nn, &B->c2, y, co, To, co, 3, 1, 1, 1, w->col, t1, co);
    /* shortcut: nearest x2, conv1x1 */
    const zt_fx *sc = x;
    if (B->up) {
        for (uint64_t j = 0; j < T; j++)
            for (uint32_t c = 0; c < ci; c++)
                t2[(2 * j) * ci + c] = t2[(2 * j + 1) * ci + c] = x[j * ci + c];
        sc = t2;
    }
    if (B->has_sc) {
        zt_kk_lin_t L = {B->sc, 0};
        zt_nn_linear(nn, &L.w, 0, sc, ci, ci, To, y, co);
        sc = y;
    }
    for (uint64_t i = 0; i < (uint64_t) To * co; i++)
        y[i] = sat32(rshr(((int64_t) t1[i] + sc[i]) * ISQRT2_Q16, 16));
    return To;
}

/* Snake: x + sin^2(a x) / a, per channel, in place */
static void snake(zt_fx *x, uint64_t T, uint32_t C, const zt_gain_t *a, const zt_fx *inv)
{
    for (uint64_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < C; c++) {
            zt_fx v = x[t * C + c];
            zt_fx ax = gain_mul(v, a, c);
            int32_t cs, sn;
            zt_tts_cossin(ax, &cs, &sn);
            int64_t s2 = rshr((int64_t) sn * sn, 30);                  /* Q30 */
            x[t * C + c] = sat32((int64_t) v + rshr(s2 * inv[c], 30)); /* Q16 */
        }
}

/* AdaINResBlock1 in place on x [T][C]; t1, t2 scratch */
static void resblock1(zt_nn_t *nn, const zt_kk_res_t *R, const zt_fx *s, zt_fx *x, uint32_t T,
                      zt_fx *t1, zt_fx *t2, kwork_t *w)
{
    static const uint32_t dil[3] = {1, 3, 5};
    uint32_t C = R->C, K = R->K;
    uint64_t n = (uint64_t) T * C;
    for (uint32_t j = 0; j < 3; j++) {
        for (uint64_t i = 0; i < n; i++) t1[i] = x[i];
        adain(nn, &R->a1[j], s, t1, T, C, w);
        snake(t1, T, C, &R->al1[j], R->inv1[j]);
        conv(nn, &R->c1[j], t1, C, T, C, K, 1, (K * dil[j] - dil[j]) / 2, dil[j], w->col, t2, C);
        adain(nn, &R->a2[j], s, t2, T, C, w);
        snake(t2, T, C, &R->al2[j], R->inv2[j]);
        conv(nn, &R->c2[j], t2, C, T, C, K, 1, (K - 1) / 2, 1, w->col, t1, C);
        for (uint64_t i = 0; i < n; i++) x[i] = sat32((int64_t) x[i] + t1[i]);
    }
}

/* ConvTranspose1d by stride phases: x [T][Cin] -> y [To][Cout] */
static uint32_t conv_t(zt_nn_t *nn, const zt_kk_lin_t *ph, const zt_fx *bias, uint32_t stride,
                       uint32_t K, uint32_t pad, const zt_fx *x, uint32_t T, uint32_t Cin,
                       uint32_t Cout, zt_fx *col, zt_fx *y)
{
    uint32_t taps = K / stride, To = (T - 1) * stride - 2 * pad + K, cols = Cin * taps;
    /* outputs of one phase are every stride-th row: gather them in chunks */
    for (uint32_t p = 0; p < stride; p++) {
        uint32_t k0 = (p + pad) % stride;
        uint32_t nout = To > p ? (To - p + stride - 1) / stride : 0;
        for (uint32_t i0 = 0; i0 < nout; i0 += CHUNK) {
            uint32_t B = nout - i0 < CHUNK ? nout - i0 : CHUNK;
            for (uint32_t b = 0; b < B; b++) {
                uint32_t t = p + (i0 + b) * stride;
                int64_t ib = (t + pad - k0) / stride; /* k0 <= p + pad <= t + pad */
                zt_fx *o = col + (uint64_t) b * cols;
                for (uint32_t m = 0; m < taps; m++) {
                    int64_t i = ib - m;
                    bool in = i >= 0 && i < T;
                    for (uint32_t c = 0; c < Cin; c++)
                        o[c * taps + m] = in ? x[(uint64_t) i * Cin + c] : 0;
                }
            }
            /* results land in the col tail, then scatter */
            zt_fx *tmp = col + (uint64_t) CHUNK * cols;
            zt_nn_linear(nn, &ph[p].w, bias, col, cols, cols, B, tmp, Cout);
            for (uint32_t b = 0; b < B; b++) {
                uint32_t t = p + (i0 + b) * stride;
                for (uint32_t c = 0; c < Cout; c++)
                    y[(uint64_t) t * Cout + c] = tmp[(uint64_t) b * Cout + c];
            }
        }
    }
    return To;
}

/* ---- the harmonic source ---- */

typedef struct {
    uint64_t s;
} rng_t;

static uint32_t u24(rng_t *r)
{
    uint64_t s = r->s;
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    r->s = s;
    return (uint32_t) ((s * 0x2545F4914F6CDD1Dull) >> 40);
}

static int32_t normal_q16(rng_t *r)
{
    uint64_t t = 0;
    for (int i = 0; i < 12; i++) t += u24(r);
    return (int32_t) ((int64_t) (t >> 8) - ((int64_t) 6 << 16));
}

/* F0 (2F steps, Hz Q16) -> 600 F source samples (Q16) */
static void source(const zt_kokoro_t *k, const zt_fx *f0, uint32_t n, kwork_t *w)
{
    /* phase steps in turns * 2^32: frac(h f0 / 24000), and their running sum */
    uint64_t *cum = w->cum;
    uint64_t run[9] = {0};
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t h = 0; h < 9; h++) {
            int64_t f = f0[i];
            /* (h + 1) f0 / 24000 turns, Q32: f in Q16 -> * 2^16 */
            int64_t num = f * (int64_t) (h + 1) * 65536;
            uint64_t a = (uint64_t) (num < 0 ? -num : num);
            uint64_t q = zt_udiv64(a + 12000, 24000, 0);
            uint64_t step = (num < 0 ? (uint64_t) 0 - q : q) & 0xFFFFFFFFull; /* % 1 */
            run[h] += step;
            cum[(uint64_t) i * 9 + h] = (run[h] & 0xFFFFFFFFull) | (step << 32);
        }
    rng_t rg = {0x9E3779B97F4A7C15ull};
    uint64_t L = (uint64_t) n * UP;
    for (uint64_t s = 0; s < L; s++) {
        /* src = (s + 0.5) / 300 - 0.5 in 600ths: 2s + 1 - 300 */
        int64_t num = (int64_t) (2 * s + 1) - (int64_t) UP;
        uint32_t i0;
        uint64_t fn;
        if (num < 0) {
            i0 = 0;
            fn = 0;
        } else {
            i0 = (uint32_t) num / 600;
            fn = (uint32_t) num % 600;
            if (i0 >= n - 1) {
                i0 = n - 1;
                fn = 0;
            }
        }
        uint32_t i1 = i0 + 1 < n ? i0 + 1 : i0;
        zt_fx f = f0[(uint32_t) s / UP];
        bool voiced = f > 10 * 65536;
        int64_t acc = 0;
        for (uint32_t h = 0; h < 9; h++) {
            uint64_t c0 = cum[(uint64_t) i0 * 9 + h] & 0xFFFFFFFFull;
            uint64_t d1 = cum[(uint64_t) i1 * 9 + h] >> 32;
            /* 300 cum[i0] + fn * down[i1] / 2, in turns * 2^32 (wrapping) */
            uint64_t ph = (300 * c0 + ((fn * d1) >> 1)) & 0xFFFFFFFFull;
            int32_t cs, sn;
            zt_tts_cossin_turns(ph << 32, &cs, &sn);
            int64_t sine = voiced ? rshr((int64_t) sn * 6554, 30) : 0; /* 0.1 sin, Q16 */
            int64_t nz = normal_q16(&rg);
            int64_t amp = voiced ? 197 : 2185; /* 0.003, 0.1 / 3 in Q16 */
            int64_t v = sine + rshr(nz * amp, 16);
            acc += v * k->l_lin.g[h];
        }
        int64_t lin = rshr(acc, k->l_lin.frac) + k->l_lin_b[0];
        w->src[s] = tanh_q16(sat32(lin));
    }
}

/* atan(2^-i) in Q30 radians */
static const int32_t ATAN_Q30[31] = {
    843314857, 497837829, 263043837, 133525159, 67021687, 33543516, 16775851, 8388437,
    4194283,   2097149,   1048576,   524288,    262144,   131072,   65536,    32768,
    16384,     8192,      4096,      2048,      1024,     512,      256,      128,
    64,        32,        16,        8,         4,        2,        1};
#define PI_Q30 3373259426ll

/* atan2(y, x) in Q16 radians (CORDIC vectoring); torch.angle(0) is 0 */
static zt_fx atan2_q16(int64_t y, int64_t x)
{
    if (x == 0 && y == 0) return 0;
    int64_t base = 0;
    if (x < 0) { /* rotate by pi into the right half */
        x = -x;
        y = -y;
        base = y > 0 ? -PI_Q30 : PI_Q30;
        if (y == 0) base = PI_Q30;
    }
    /* scale to just under 2^58: the shifts below then keep 30 bits */
    while ((x > ((int64_t) 1 << 58)) || (y > ((int64_t) 1 << 58)) || (-y > ((int64_t) 1 << 58))) {
        x >>= 1;
        y >>= 1;
    }
    while (x < ((int64_t) 1 << 57) && y < ((int64_t) 1 << 57) && -y < ((int64_t) 1 << 57)) {
        x *= 2;
        y *= 2;
    }
    int64_t z = 0;
    for (int i = 0; i < 31; i++) {
        int64_t xn, yn;
        if (y > 0) {
            xn = x + (y >> i);
            yn = y - (x >> i);
            z += ATAN_Q30[i];
        } else {
            xn = x - (y >> i);
            yn = y + (x >> i);
            z -= ATAN_Q30[i];
        }
        x = xn;
        y = yn;
    }
    return sat32(rshr(base + z, 14));
}

/* the source's STFT: frames (L / 5 + 1) of 11 magnitudes and 11 phases */
static uint32_t src_stft(const zt_fx *src, uint64_t L, zt_fx *har)
{
    int32_t cosT[NFFT], sinT[NFFT], win[NFFT];
    for (uint32_t i = 0; i < NFFT; i++) {
        int32_t c, s;
        zt_tts_cossin_turns((uint64_t) zt_udiv64((uint64_t) i << 32, NFFT, 0) << 32, &c, &s);
        cosT[i] = c;
        sinT[i] = s;
        win[i] = (int32_t) (((int64_t) (1 << 30) - c) / 2); /* 0.5 - 0.5 cos, Q30 */
    }
    uint32_t frames = (uint32_t) L / HOP + 1;
    int64_t pad = NFFT / 2;
    for (uint32_t f = 0; f < frames; f++) {
        int64_t xw[NFFT];
        for (uint32_t j = 0; j < NFFT; j++) {
            int64_t s = (int64_t) f * HOP + j - pad;
            if (s < 0) s = -s; /* reflect */
            if (s >= (int64_t) L) s = 2 * ((int64_t) L - 1) - s;
            if (s < 0) s = 0;
            xw[j] = rshr((int64_t) src[s] * win[j], 14); /* Q32 */
        }
        zt_fx *o = har + (uint64_t) f * 22;
        for (uint32_t m = 0; m < NBIN; m++) {
            int64_t re = 0, im = 0;
            for (uint32_t j = 0; j < NFFT; j++) {
                uint32_t ix = (m * j) % NFFT;
                re += rshr(xw[j] * cosT[ix], 30);
                im -= rshr(xw[j] * sinT[ix], 30);
            }
            /* Q32 sums (|X| <= 20); scale down until the squares fit */
            int64_t are = re < 0 ? -re : re, aim = im < 0 ? -im : im;
            int64_t big = are > aim ? are : aim;
            uint32_t sh = 0;
            while ((big >> sh) > ((int64_t) 1 << 31)) sh++;
            uint64_t rr = (uint64_t) (are >> sh), ii = (uint64_t) (aim >> sh);
            uint64_t mag = (uint64_t) zt_isqrt64(rr * rr + ii * ii) << sh; /* Q32 */
            o[m] = sat32(rshr((int64_t) mag, 16));
            o[NBIN + m] = atan2_q16(im, re);
        }
    }
    return frames;
}

/* ---- the inverse STFT: spec [frames][22] (log-mag, phase pre-activation) ---- */
static uint32_t istft(const zt_fx *x, uint32_t frames, int16_t *pcm, uint32_t *clipped, kwork_t *w)
{
    int32_t cosT[NFFT], sinT[NFFT], win[NFFT];
    for (uint32_t i = 0; i < NFFT; i++) {
        int32_t c, s;
        zt_tts_cossin_turns((uint64_t) zt_udiv64((uint64_t) i << 32, NFFT, 0) << 32, &c, &s);
        cosT[i] = c;
        sinT[i] = s;
        win[i] = (int32_t) (((int64_t) (1 << 30) - c) / 2);
    }
    uint64_t Lf = (uint64_t) NFFT + (uint64_t) HOP * (frames - 1);
    for (uint64_t i = 0; i < Lf; i++) w->acc[i] = w->env[i] = 0;
    for (uint32_t f = 0; f < frames; f++) {
        const zt_fx *r = x + (uint64_t) f * 22;
        int64_t re[NBIN], im[NBIN];
        for (uint32_t m = 0; m < NBIN; m++) {
            zt_fx mag = zt_tts_exp(r[m]); /* Q16 */
            int32_t cs, sn;
            zt_tts_cossin(r[NBIN + m], &cs, &sn);     /* sin of the pre-activation */
            zt_fx ph = sat32(rshr((int64_t) sn, 14)); /* phase = sin x, Q16 */
            zt_tts_cossin(ph, &cs, &sn);
            re[m] = rshr((int64_t) mag * cs, 22); /* Q24 */
            im[m] = rshr((int64_t) mag * sn, 22);
        }
        for (uint32_t j = 0; j < NFFT; j++) {
            /* irfft: (X0 + (-1)^j X10 + 2 sum Re(X_m e^{2 pi i j m / 20})) / 20 */
            int64_t acc = re[0] + ((j & 1) ? -re[NBIN - 1] : re[NBIN - 1]);
            for (uint32_t m = 1; m < NBIN - 1; m++) {
                uint32_t ix = (m * j) % NFFT;
                acc +=
                    2 * (rshr(re[m] * (cosT[ix] >> 10), 20) - rshr(im[m] * (sinT[ix] >> 10), 20));
            }
            /* acc Q24; / 20, times the window (Q16) -> Q24 */
            int64_t y = round_div(acc, NFFT);
            uint64_t at = (uint64_t) f * HOP + j;
            w->acc[at] += rshr(y * (win[j] >> 14), 16);
            w->env[at] += rshr((int64_t) win[j] * win[j], 30);
        }
    }
    uint64_t pad = NFFT / 2, n = Lf - 2 * pad;
    uint32_t clip = 0;
    for (uint64_t i = 0; i < n; i++) {
        int64_t e = w->env[pad + i];
        int64_t v = w->acc[pad + i];
        /* v Q24, e Q30: v 2^6 32767 / e */
        if (v > ((int64_t) 1 << 40)) v = (int64_t) 1 << 40;
        if (v < -((int64_t) 1 << 40)) v = -((int64_t) 1 << 40);
        int64_t q = e > 1024 ? round_div(v * 32767 * 64, (uint64_t) e) : rshr(v * 32767, 24);
        if (q > 32767) {
            q = 32767;
            clip++;
        }
        if (q < -32768) {
            q = -32768;
            clip++;
        }
        pcm[i] = (int16_t) q;
    }
    *clipped = clip;
    return (uint32_t) n;
}

/* ---- the forward pass ---- */

int32_t zt_kokoro_synth(const zt_kokoro_t *k, const int32_t *ids, uint32_t n, const zt_fx *style,
                        uint32_t speed_pct, uint32_t max_frames, int16_t *pcm, uint32_t pcm_cap,
                        void *work, uint64_t work_bytes, zt_kokoro_stats_t *st)
{
    if (!k || !ids || n < 2 || n > ZT_KOKORO_MAX_TOK || !style || !pcm || !work || !max_frames)
        return ZT_NN_EARG;
    if (speed_pct < 25) speed_pct = 25;
    if (speed_pct > 400) speed_pct = 400;
    for (uint32_t i = 0; i < n; i++)
        if (ids[i] < 0 || (uint32_t) ids[i] >= k->cfg.n_token) return ZT_NN_EARG;
    kwork_t w;
    uint64_t need = layout(n, max_frames, 0, &w);
    if (need > work_bytes) return ZT_NN_ESPACE;
    uint8_t *base = (uint8_t *) (((uintptr_t) work + 63) & ~(uintptr_t) 63);
    layout(n, max_frames, base, &w);
    zt_nn_t nn;
    uint32_t keys = n > 32 ? n : 32;
    if (zt_nn_init(&nn, CHUNK, MAX_COLS, keys, 64, 12, w.nn_mem, w.nn_bytes)) return ZT_NN_ESPACE;
    nn.kern = k->kern;
    const zt_fx *s_dec = style, *s_pro = style + STY;
    uint32_t T = n;

    /* PL-BERT: A = x [T][768] */
    zt_fx *emb = w.B;
    for (uint32_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < 128; c++)
            emb[t * 128 + c] = sat32((int64_t) k->word[(uint32_t) ids[t] * 128 + c] +
                                     k->pos[t * 128 + c] + k->type[c]);
    zt_nn_layernorm(&nn, emb, 128, 128, T, &k->emb_norm, 0x2B8CBCCCu /* 1e-12 */, emb, 128);
    zt_nn_linear(&nn, &k->map.w, k->map.b, emb, 128, 128, T, w.A, BERT);
    for (int l = 0; l < 12; l++) {
        zt_fx *q = w.B, *kk = w.C, *v = w.D, *ctx = w.E, *a = w.Fb;
        zt_nn_linear(&nn, &k->q.w, k->q.b, w.A, BERT, BERT, T, q, BERT);
        zt_nn_linear(&nn, &k->k.w, k->k.b, w.A, BERT, BERT, T, kk, BERT);
        zt_nn_linear(&nn, &k->v.w, k->v.b, w.A, BERT, BERT, T, v, BERT);
        zt_nn_attention(&nn, q, BERT, kk, BERT, v, BERT, T, T, 12, 64, false, ctx, BERT);
        zt_nn_linear(&nn, &k->o.w, k->o.b, ctx, BERT, BERT, T, a, BERT);
        zt_nn_add(&nn, a, BERT, w.A, BERT, BERT, T);
        zt_nn_layernorm(&nn, a, BERT, BERT, T, &k->attn_norm, 0x2B8CBCCCu, w.A, BERT);
        zt_nn_linear(&nn, &k->up.w, k->up.b, w.A, BERT, BERT, T, w.B, BERT_FF);
        zt_nn_act(&nn, w.B, BERT_FF, BERT_FF, T, ZT_NN_GELU_TANH);
        zt_nn_linear(&nn, &k->down.w, k->down.b, w.B, BERT_FF, BERT_FF, T, a, BERT);
        zt_nn_add(&nn, a, BERT, w.A, BERT, BERT, T);
        zt_nn_layernorm(&nn, a, BERT, BERT, T, &k->ffn_norm, 0x2B8CBCCCu, w.A, BERT);
    }
    /* d_en, then the duration encoder: x = [h | s] in B [T][640] */
    uint32_t W6 = HID + STY;
    zt_fx *x6 = w.B, *h = w.C, *xi = w.D;
    zt_nn_linear(&nn, &k->bert_enc.w, k->bert_enc.b, w.A, BERT, BERT, T, h, HID);
    for (int l = 0; l < 3; l++) {
        for (uint32_t t = 0; t < T; t++) {
            for (uint32_t c = 0; c < HID; c++) x6[t * W6 + c] = h[t * HID + c];
            for (uint32_t c = 0; c < STY; c++) x6[t * W6 + HID + c] = s_pro[c];
        }
        bilstm(&nn, &k->dur_lstm[l], x6, W6, T, xi, h, HID, &w);
        adaln(&nn, &k->dur_norm[l], s_pro, h, HID, T, HID, &w);
    }
    for (uint32_t t = 0; t < T; t++) { /* d = [h | s] stays in B */
        for (uint32_t c = 0; c < HID; c++) x6[t * W6 + c] = h[t * HID + c];
        for (uint32_t c = 0; c < STY; c++) x6[t * W6 + HID + c] = s_pro[c];
    }
    /* durations */
    bilstm(&nn, &k->lstm, x6, W6, T, xi, h, HID, &w);
    uint32_t MD = k->cfg.max_dur;
    zt_nn_linear(&nn, &k->dur_proj.w, k->dur_proj.b, h, HID, HID, T, w.E, MD);
    uint64_t frames = 0;
    for (uint32_t t = 0; t < T; t++) {
        int64_t sum = 0;
        for (uint32_t j = 0; j < MD; j++) sum += zt_nn_act1(w.E[t * MD + j], ZT_NN_SIGMOID);
        /* round(sum / speed), at least 1 */
        int64_t d = round_div(sum * 100, (uint64_t) speed_pct * 65536);
        if (d < 1) d = 1;
        w.dur[t] = (int32_t) d;
        frames += (uint64_t) d;
    }
    uint32_t Fr = (uint32_t) frames;
    if (frames > max_frames || (uint64_t) Fr * ZT_KOKORO_HOP_SAMPLES > pcm_cap) return ZT_NN_ESPACE;
    /* en [F][640] in A: each token's row repeated */
    zt_fx *en = w.A;
    {
        uint64_t f = 0;
        for (uint32_t t = 0; t < T; t++)
            for (int32_t r = 0; r < w.dur[t]; r++, f++)
                for (uint32_t c = 0; c < W6; c++) en[f * W6 + c] = x6[t * W6 + c];
    }
    /* text encoder (needs B no more): embedding, CNN, BiLSTM -> t_en [T][512] in E */
    zt_fx *te = w.C, *te2 = w.D;
    for (uint32_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < HID; c++)
            te[t * HID + c] = k->text_embd[(uint32_t) ids[t] * HID + c];
    for (int c = 0; c < 3; c++) {
        conv(&nn, &k->cnn[c], te, HID, T, HID, 5, 1, 2, 1, w.col, te2, HID);
        zt_nn_layernorm(&nn, te2, HID, HID, T, &k->cnn_norm[c], 0x3727C5ACu, te, HID);
        lrelu(te, (uint64_t) T * HID, 13107);
    }
    bilstm(&nn, &k->text_lstm, te, HID, T, te2, w.E, HID, &w);
    /* asr [F][512] in Fb */
    zt_fx *asr = w.Fb;
    {
        uint64_t f = 0;
        for (uint32_t t = 0; t < T; t++)
            for (int32_t r = 0; r < w.dur[t]; r++, f++)
                for (uint32_t c = 0; c < HID; c++) asr[f * HID + c] = w.E[t * HID + c];
    }
    /* F0 and N: shared BiLSTM over en -> B [F][512] */
    bilstm(&nn, &k->shared, en, W6, Fr, w.C, w.B, HID, &w);
    for (int br = 0; br < 2; br++) {
        const zt_kk_blk_t *bk = br ? k->n : k->f0;
        uint32_t L = Fr;
        /* en (A) is no longer needed after the shared LSTM */
        L = resblk(&nn, &bk[0], s_pro, w.B, L, w.C, w.D, w.E, &w);
        L = resblk(&nn, &bk[1], s_pro, w.C, L, w.D, w.E, w.A + 0, &w);
        L = resblk(&nn, &bk[2], s_pro, w.D, L, w.C, w.E, w.A, &w);
        const zt_kk_lin_t *pj = br ? &k->n_proj : &k->f0_proj;
        zt_fx *out = br ? w.nn2 : w.f0;
        zt_nn_linear(&nn, &pj->w, pj->b, w.C, HID / 2, HID / 2, L, out, 1);
    }
    uint32_t F2 = 2 * Fr;
    /* decoder: F0 and N at the frame rate (k3, stride 2, pad 1) */
    zt_fx *F0d = w.E, *Nd = w.E + Fr;
    for (uint32_t i = 0; i < Fr; i++) {
        int64_t a = 0, b = 0;
        for (uint32_t j = 0; j < 3; j++) {
            int64_t s = (int64_t) 2 * i - 1 + j;
            if (s < 0 || s >= F2) continue;
            a += (int64_t) w.f0[s] * k->f0c_w.g[j];
            b += (int64_t) w.nn2[s] * k->nc_w.g[j];
        }
        F0d[i] = sat32(rshr(a, k->f0c_w.frac) + k->f0c_b[0]);
        Nd[i] = sat32(rshr(b, k->nc_w.frac) + k->nc_b[0]);
    }
    /* x = [asr | F0 | N] [F][514] in A */
    uint32_t W514 = HID + 2, W1090 = 1090;
    for (uint32_t f = 0; f < Fr; f++) {
        for (uint32_t c = 0; c < HID; c++) w.A[f * W514 + c] = asr[f * HID + c];
        w.A[f * W514 + HID] = F0d[f];
        w.A[f * W514 + HID + 1] = Nd[f];
    }
    /* asr_res [F][64]: keep it after F0d/Nd in E */
    zt_fx *ares = w.E + 2 * Fr;
    zt_nn_linear(&nn, &k->asr_res.w, k->asr_res.b, asr, HID, HID, Fr, ares, 64);
    resblk(&nn, &k->encode, s_dec, w.A, Fr, w.B, w.C, w.D, &w); /* B [F][1024] */
    zt_fx *cur = w.B;
    uint32_t L = Fr;
    for (int d = 0; d < 4; d++) {
        for (uint32_t f = 0; f < Fr; f++) {
            zt_fx *o = w.A + (uint64_t) f * W1090;
            const zt_fx *xr = cur + (uint64_t) f * 1024;
            for (uint32_t c = 0; c < 1024; c++) o[c] = xr[c];
            for (uint32_t c = 0; c < 64; c++) o[1024 + c] = ares[f * 64 + c];
            o[1088] = F0d[f];
            o[1089] = Nd[f];
        }
        zt_fx *dst = cur == w.B ? w.Fb : w.B;
        L = resblk(&nn, &k->decode[d], s_dec, w.A, Fr, dst, w.C, w.D, &w);
        cur = dst;
    }
    /* cur: [2F][512] */
    /* generator: the source, its STFT */
    source(k, w.f0, F2, &w);
    uint64_t Ls = (uint64_t) F2 * UP;
    uint32_t hf = src_stft(w.src, Ls, w.har); /* 120 F + 1 */
    /* stage 1 */
    zt_fx *x = cur == w.B ? w.Fb : w.B; /* the free one of B / Fb */
    zt_fx *xin = cur;
    lrelu(xin, (uint64_t) L * 512, 6554); /* 0.1 */
    zt_fx *xs = w.A;
    uint32_t n1 = conv(&nn, &k->noise_conv[0], w.har, 22, hf, 22, 12, 6, 3, 1, w.col, xs, GEN_C1);
    resblock1(&nn, &k->noise_res[0], s_dec, xs, n1, w.C, w.D, &w);
    uint32_t L1 = conv_t(&nn, k->ups_p0, k->ups_b[0], 10, 20, 5, xin, L, 512, GEN_C1, w.col, x);
    if (n1 != L1) return ZT_NN_ESHAPE;
    for (uint64_t i = 0; i < (uint64_t) L1 * GEN_C1; i++) x[i] = sat32((int64_t) x[i] + xs[i]);
    /* three resblocks averaged: E = acc */
    zt_fx *acc = w.E;
    for (int j = 0; j < 3; j++) {
        zt_fx *r = xs; /* xs is free now */
        for (uint64_t i = 0; i < (uint64_t) L1 * GEN_C1; i++) r[i] = x[i];
        resblock1(&nn, &k->res[j], s_dec, r, L1, w.C, w.D, &w);
        for (uint64_t i = 0; i < (uint64_t) L1 * GEN_C1; i++)
            acc[i] = j ? sat32((int64_t) acc[i] + r[i]) : r[i];
    }
    for (uint64_t i = 0; i < (uint64_t) L1 * GEN_C1; i++) acc[i] = (zt_fx) round_div(acc[i], 3);
    /* stage 2 */
    lrelu(acc, (uint64_t) L1 * GEN_C1, 6554);
    xs = w.A;
    uint32_t n2 = conv(&nn, &k->noise_conv[1], w.har, 22, hf, 22, 1, 1, 0, 1, w.col, xs, GEN_C2);
    resblock1(&nn, &k->noise_res[1], s_dec, xs, n2, w.C, w.D, &w);
    zt_fx *x2 = x; /* stage-1 x is free: reuse it */
    uint32_t L2 =
        conv_t(&nn, k->ups_p1, k->ups_b[1], 6, 12, 3, acc, L1, GEN_C1, GEN_C2, w.col, x2 + GEN_C2);
    /* reflection pad (1, 0): row 0 = row 2 of the padded (row 1 of the unpadded) */
    for (uint32_t c = 0; c < GEN_C2; c++) x2[c] = x2[GEN_C2 + GEN_C2 + c];
    L2 += 1;
    if (n2 != L2) return ZT_NN_ESHAPE;
    for (uint64_t i = 0; i < (uint64_t) L2 * GEN_C2; i++) x2[i] = sat32((int64_t) x2[i] + xs[i]);
    acc = cur; /* free */
    for (int j = 0; j < 3; j++) {
        zt_fx *r = xs;
        for (uint64_t i = 0; i < (uint64_t) L2 * GEN_C2; i++) r[i] = x2[i];
        resblock1(&nn, &k->res[3 + j], s_dec, r, L2, w.C, w.D, &w);
        for (uint64_t i = 0; i < (uint64_t) L2 * GEN_C2; i++)
            acc[i] = j ? sat32((int64_t) acc[i] + r[i]) : r[i];
    }
    for (uint64_t i = 0; i < (uint64_t) L2 * GEN_C2; i++) acc[i] = (zt_fx) round_div(acc[i], 3);
    lrelu(acc, (uint64_t) L2 * GEN_C2, 655); /* 0.01 */
    zt_fx *post = w.C;
    conv(&nn, &k->post, acc, GEN_C2, L2, GEN_C2, 7, 1, 3, 1, w.col, post, 22);
    uint32_t clipped = 0;
    uint32_t ns = istft(post, L2, pcm, &clipped, &w);
    if (st) {
        st->tokens = T;
        st->frames = Fr;
        st->samples = ns;
        st->clipped = clipped;
        st->n_sat = nn.n_sat;
        st->dur = w.dur;
        st->f0 = w.f0;
    }
    return ZT_GGUF_OK;
}
