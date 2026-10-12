/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_tts.c — speech out (T26): OuteTTS prompt and codes, the WavTokenizer
 * decoder and the integer iSTFT. See zt_tts.h. */
#include "zt_tts.h"
#include "zt_kern.h"
#include "zt_tts_tables.h"

#define LN100_Q16 301804        /* round(ln 100 * 2^16) */
#define MAG_MAX   6553600       /* 100 in Q16 */
#define TWOPI_Q28 1686629713ull /* round(2 pi * 2^28) */
#define CHUNK     64u           /* rows per im2col + matrix product */
#define NFFT      ZT_TTS_NFFT
#define NBIN      ZT_TTS_NBIN
#define HOP       ZT_TTS_HOP
#define PAD       ((NFFT - HOP) / 2) /* 480 */

static zt_fx sat32(int64_t v)
{
    return (zt_fx) (v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v);
}

/* v / 2^s rounded to nearest, halves away from zero (0 < s < 63) */
static int64_t rshr(int64_t v, uint32_t s)
{
    uint64_t h = (uint64_t) 1 << (s - 1);
    return v >= 0 ? (int64_t) (((uint64_t) v + h) >> s)
                  : -(int64_t) (((uint64_t) 0 - (uint64_t) v + h) >> s);
}

/* round(n / d) for d > 0, halves away from zero */
static int64_t round_div(int64_t n, uint64_t d)
{
    bool neg = n < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t q = zt_udiv64(u + (d >> 1), d, 0);
    return neg ? -(int64_t) q : (int64_t) q;
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

/* ---- text ---- */

typedef struct {
    uint8_t *out;
    uint64_t cap, n;
    bool started, pend; /* a letter was written; a space is pending */
} tx_t;

static void put(tx_t *t, const char *s)
{
    for (; *s; s++) {
        if (t->n < t->cap) t->out[t->n] = (uint8_t) *s;
        t->n++;
    }
}

/* lower case, - _ / , . \ to space, drop all but a-z and white space,
 * collapse and trim white space, each space as <|space|> */
static void feed(tx_t *t, uint8_t c)
{
    if (c >= 'A' && c <= 'Z') c = (uint8_t) (c + 32);
    if (c == '-' || c == '_' || c == '/' || c == ',' || c == '.' || c == '\\') c = ' ';
    if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r') {
        if (t->started) t->pend = true;
        return;
    }
    if (c < 'a' || c > 'z') return;
    if (t->pend) put(t, "<|space|>");
    t->pend = false;
    t->started = true;
    if (t->n < t->cap) t->out[t->n] = c;
    t->n++;
}

static void feeds(tx_t *t, const char *s)
{
    for (; *s; s++) feed(t, (uint8_t) *s);
}

static const char *const ONES[20] = {"zero",    "one",     "two",       "three",    "four",
                                     "five",    "six",     "seven",     "eight",    "nine",
                                     "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                                     "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
static const char *const TENS[10] = {"",      "",      "twenty",  "thirty", "forty",
                                     "fifty", "sixty", "seventy", "eighty", "ninety"};

static void below1000(tx_t *t, uint32_t n)
{
    if (n >= 100) {
        feeds(t, ONES[n / 100]);
        feeds(t, " hundred ");
        n %= 100;
    }
    if (n >= 20) {
        feeds(t, TENS[n / 10]);
        if (n % 10) {
            feeds(t, "-");
            feeds(t, ONES[n % 10]);
        }
    } else if (n > 0) {
        feeds(t, ONES[n]);
    }
}

/* llama.cpp's number_to_words on digits d[0..nd) and decimals f[0..nf) */
static void number(tx_t *t, const uint8_t *d, uint64_t nd, const uint8_t *f, uint64_t nf)
{
    uint64_t v = 0;
    for (uint64_t i = 0; i < nd; i++) {
        v = v * 10 + (uint64_t) (d[i] - '0');
        if (v > 2147483647u) { /* std::stoi throws: llama.cpp writes " " */
            feeds(t, " ");
            return;
        }
    }
    uint32_t n = (uint32_t) v;
    if (n == 0) {
        feeds(t, "zero");
    } else {
        static const uint32_t scale[3] = {1000000000u, 1000000u, 1000u};
        static const char *const name[3] = {" billion ", " million ", " thousand "};
        for (int i = 0; i < 3; i++)
            if (n >= scale[i]) {
                below1000(t, n / scale[i]);
                feeds(t, name[i]);
                n %= scale[i];
            }
        if (n > 0) below1000(t, n);
    }
    if (f) {
        feeds(t, " point");
        for (uint64_t i = 0; i < nf; i++) {
            feeds(t, " ");
            feeds(t, ONES[f[i] - '0']);
        }
    }
}

static bool digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

static void normalize(tx_t *t, const uint8_t *s, uint64_t len)
{
    t->started = t->pend = false;
    for (uint64_t i = 0; i < len;) {
        if (!digit(s[i])) {
            feed(t, s[i++]);
            continue;
        }
        uint64_t j = i;
        while (j < len && digit(s[j])) j++;
        if (j + 1 < len && s[j] == '.' && digit(s[j + 1])) {
            uint64_t k = j + 1;
            while (k < len && digit(s[k])) k++;
            number(t, s + i, j - i, s + j + 1, k - j - 1);
            i = k;
        } else {
            number(t, s + i, j - i, 0, 0);
            i = j;
        }
    }
}

uint64_t zt_tts_normalize(const uint8_t *text, uint64_t len, uint8_t *out, uint64_t cap)
{
    tx_t t = {out, out ? cap : 0, 0, false, false};
    normalize(&t, text, len);
    return t.n;
}

uint64_t zt_tts_prompt(const uint8_t *text, uint64_t len, uint8_t *out, uint64_t cap)
{
    tx_t t = {out, out ? cap : 0, 0, false, false};
    put(&t, "<|im_start|>\n");
    put(&t, ZT_TTS_SPK_TEXT);
    normalize(&t, text, len);
    put(&t, "<|text_end|>\n");
    put(&t, ZT_TTS_SPK_AUDIO);
    return t.n;
}

/* ---- codes ---- */

static int32_t find(const zt_tok_t *t, const char *s)
{
    uint64_t n = 0;
    while (s[n]) n++;
    return zt_tok_find(t, (const uint8_t *) s, n);
}

int32_t zt_tts_vocab(const zt_tok_t *t, zt_tts_vocab_t *v)
{
    v->code0 = find(t, "<|0|>");
    v->audio_end = find(t, "<|audio_end|>");
    v->im_end = find(t, "<|im_end|>");
    v->eot = find(t, "<|endoftext|>");
    if (v->code0 < 0 || find(t, "<|1|>") != v->code0 + 1 || find(t, "<|4095|>") != v->code0 + 4095)
        return ZT_NN_ECONFIG;
    return ZT_GGUF_OK;
}

uint32_t zt_tts_codes(const zt_tts_vocab_t *v, const int32_t *tokens, uint32_t n, int32_t *codes)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t c = (int64_t) tokens[i] - v->code0;
        if (c >= 0 && c < (int64_t) ZT_TTS_CODES) codes[k++] = (int32_t) c;
    }
    return k;
}

/* ---- decoder: loading ---- */

static bool key_is(const zt_gguf_t *g, const char *key, const char *val)
{
    zt_gguf_val_t v;
    return zt_gguf_find(g, key, &v) == ZT_GGUF_OK && v.type == ZT_GGUF_STRING &&
           zt_gguf_str_eq(v.str, val);
}

int32_t zt_tts_config(const zt_gguf_t *g, zt_tts_cfg_t *c, zt_model_err_t *err)
{
    if (!key_is(g, "general.architecture", "wavtokenizer-dec")) {
        set_err(err, ZT_NN_ECONFIG, "not a WavTokenizer decoder", "general.architecture");
        return ZT_NN_ECONFIG;
    }
#define K(x) "wavtokenizer-dec." x
    c->n_vocab = (uint32_t) zt_gguf_get_int(g, K("vocab_size"), 0);
    c->n_feat = (uint32_t) zt_gguf_get_int(g, K("features_length"), 0);
    c->n_out = (uint32_t) zt_gguf_get_int(g, K("embedding_length"), 0);
    c->n_embd = (uint32_t) zt_gguf_get_int(g, K("posnet.embedding_length"), 0);
    c->n_posnet = (uint32_t) zt_gguf_get_int(g, K("posnet.block_count"), 0);
    c->n_convnext = (uint32_t) zt_gguf_get_int(g, K("convnext.block_count"), 0);
    c->n_ff = (uint32_t) zt_gguf_get_int(g, K("feed_forward_length"), 0);
    c->n_groups = (uint32_t) zt_gguf_get_int(g, K("attention.group_norm_groups"), 0);
    c->eps_ln_f32 = zt_nn_f32_key(g, K("attention.layer_norm_epsilon"), 0x358637BDu);
    c->eps_gn_f32 = zt_nn_f32_key(g, K("attention.group_norm_epsilon"), 0x358637BDu);
    uint32_t cnx = (uint32_t) zt_gguf_get_int(g, K("convnext.embedding_length"), 0);
#undef K
    if (c->n_vocab == 0 || c->n_vocab > 65536 || !c->n_feat || c->n_feat > 4096 || !c->n_embd ||
        c->n_embd > 4096 || cnx != c->n_embd || c->n_posnet != 6 || !c->n_convnext ||
        c->n_convnext > 64 || !c->n_ff || c->n_ff > 16384 || c->n_out != NFFT + 2 || !c->n_groups ||
        c->n_embd % c->n_groups) {
        set_err(err, ZT_NN_ECONFIG, "WavTokenizer metadata out of range", "wavtokenizer-dec");
        return ZT_NN_ECONFIG;
    }
    return ZT_GGUF_OK;
}

/* "<pre><l>.<suffix>" */
static void tname(char *dst, uint32_t cap, const char *pre, uint32_t l, const char *suffix)
{
    char num[12];
    uint32_t k = 0, n = 0;
    do {
        num[k++] = (char) ('0' + l % 10);
        l /= 10;
    } while (l);
    for (; *pre && n < cap - 1; pre++) dst[n++] = *pre;
    while (k && n < cap - 1) dst[n++] = num[--k];
    if (n < cap - 1) dst[n++] = '.';
    for (; *suffix && n < cap - 1; suffix++) dst[n++] = *suffix;
    dst[n] = 0;
}

static int32_t norm(zt_nn_arena_t *a, const zt_gguf_t *g, const char *pre, uint32_t l,
                    const char *nm, uint32_t n, zt_nn_norm_t *out)
{
    char w[64], b[64], s[48];
    uint32_t i = 0;
    for (; nm[i] && i < 32; i++) s[i] = nm[i];
    const char *ws = ".weight", *bs = ".bias";
    uint32_t j = 0;
    for (; ws[j]; j++) s[i + j] = ws[j];
    s[i + j] = 0;
    if (pre)
        tname(w, sizeof w, pre, l, s);
    else
        for (j = 0; (w[j] = s[j]); j++) {
        }
    for (j = 0; bs[j]; j++) s[i + j] = bs[j];
    s[i + j] = 0;
    if (pre)
        tname(b, sizeof b, pre, l, s);
    else
        for (j = 0; (b[j] = s[j]); j++) {
        }
    int32_t r = zt_nn_load_gain(a, g, w, n, &out->g);
    return r ? r : zt_nn_load_q16(a, g, b, n, &out->b);
}

static int32_t matb(zt_nn_arena_t *a, const zt_gguf_t *g, const char *pre, uint32_t l,
                    const char *nm, uint32_t cols, uint32_t rows, zt_mat_t *m, const zt_fx **b)
{
    char w[64], s[48];
    uint32_t i = 0, j;
    for (; nm[i] && i < 32; i++) s[i] = nm[i];
    const char *ws = ".weight", *bs = ".bias";
    for (j = 0; ws[j]; j++) s[i + j] = ws[j];
    s[i + j] = 0;
    if (pre)
        tname(w, sizeof w, pre, l, s);
    else
        for (j = 0; (w[j] = s[j]); j++) {
        }
    int32_t r = zt_nn_load_mat(a, g, w, cols, rows, m);
    if (r) return r;
    for (j = 0; bs[j]; j++) s[i + j] = bs[j];
    s[i + j] = 0;
    if (pre)
        tname(w, sizeof w, pre, l, s);
    else
        for (j = 0; (w[j] = s[j]); j++) {
        }
    return zt_nn_load_q16(a, g, w, rows, b);
}

static int32_t plan(const zt_gguf_t *g, const zt_tts_cfg_t *c, zt_tts_dec_t *d, zt_nn_arena_t *a)
{
    int32_t r;
    uint32_t E = c->n_embd, F = c->n_ff;
    zt_tts_dec_t dd;
    if (!d) d = &dd;
    zt_tts_cnx_t *L = zt_nn_take(a, (uint64_t) c->n_convnext * sizeof *L), dl;
    if ((r = zt_nn_tensor(g, "token_embd.weight", c->n_feat, c->n_vocab, &d->embd, a->err)))
        return r;
    if (!zt_ggml_block(d->embd.type) || c->n_feat % zt_ggml_block(d->embd.type)) {
        set_err(a->err, ZT_GGUF_EUNSUPPORTED, "unsupported codebook type", "token_embd.weight");
        return ZT_GGUF_EUNSUPPORTED;
    }
    if ((r = matb(a, g, 0, 0, "conv1d", 7 * c->n_feat, E, &d->conv_in, &d->conv_in_b))) return r;
    static const uint32_t rb[4] = {0, 1, 3, 4};
    for (uint32_t i = 0; i < 4; i++) {
        zt_tts_res_t *p = &d->res[i];
        if ((r = norm(a, g, "posnet.", rb[i], "norm1", E, &p->n1))) return r;
        if ((r = norm(a, g, "posnet.", rb[i], "norm2", E, &p->n2))) return r;
        if ((r = matb(a, g, "posnet.", rb[i], "conv1", 3 * E, E, &p->c1, &p->b1))) return r;
        if ((r = matb(a, g, "posnet.", rb[i], "conv2", 3 * E, E, &p->c2, &p->b2))) return r;
    }
    if ((r = norm(a, g, "posnet.", 2, "attn_norm", E, &d->att_norm))) return r;
    if ((r = matb(a, g, "posnet.", 2, "attn_q", E, E, &d->wq, &d->bq))) return r;
    if ((r = matb(a, g, "posnet.", 2, "attn_k", E, E, &d->wk, &d->bk))) return r;
    if ((r = matb(a, g, "posnet.", 2, "attn_v", E, E, &d->wv, &d->bv))) return r;
    if ((r = matb(a, g, "posnet.", 2, "attn_output", E, E, &d->wo, &d->bo))) return r;
    if ((r = norm(a, g, "posnet.", 5, "attn_norm", E, &d->pos_norm))) return r;
    if ((r = norm(a, g, 0, 0, "token_embd_norm", E, &d->tok_norm))) return r;
    char nm[64];
    for (uint32_t l = 0; l < c->n_convnext; l++) {
        zt_tts_cnx_t *y = L ? &L[l] : &dl;
        tname(nm, sizeof nm, "convnext.", l, "dw.weight");
        if ((r = zt_nn_load_gain(a, g, nm, 7 * E, &y->dw))) return r;
        tname(nm, sizeof nm, "convnext.", l, "dw.bias");
        if ((r = zt_nn_load_q16(a, g, nm, E, &y->dw_b))) return r;
        if ((r = norm(a, g, "convnext.", l, "norm", E, &y->norm))) return r;
        if ((r = matb(a, g, "convnext.", l, "pw1", E, F, &y->pw1, &y->b1))) return r;
        if ((r = matb(a, g, "convnext.", l, "pw2", F, E, &y->pw2, &y->b2))) return r;
        tname(nm, sizeof nm, "convnext.", l, "gamma.weight");
        if ((r = zt_nn_load_gain(a, g, nm, E, &y->gamma))) return r;
    }
    if ((r = norm(a, g, 0, 0, "output_norm", E, &d->out_norm))) return r;
    if ((r = matb(a, g, 0, 0, "output", E, c->n_out, &d->out, &d->out_b))) return r;
    d->cnx = L;
    return ZT_GGUF_OK;
}

static uint32_t cols_max(const zt_tts_cfg_t *c)
{
    uint32_t m = 7 * c->n_feat;
    if (3 * c->n_embd > m) m = 3 * c->n_embd;
    if (c->n_ff > m) m = c->n_ff;
    return m;
}

static void begin(zt_nn_arena_t *a, void *base, uint64_t cap, const zt_tts_cfg_t *c, uint32_t flags,
                  zt_model_err_t *err)
{
    zt_nn_arena_begin(a, base, cap, cols_max(c), err);
    if (flags & ZT_TTS_LOAD_LEAN) a->float_residual = false;
}

uint64_t zt_tts_arena_bytes_ex(const zt_gguf_t *g, uint32_t flags)
{
    zt_tts_cfg_t c;
    if (zt_tts_config(g, &c, 0)) return 0;
    zt_nn_arena_t a;
    begin(&a, 0, 0, &c, flags, 0);
    if (plan(g, &c, 0, &a)) return 0;
    return zt_nn_arena_used(&a);
}

uint64_t zt_tts_arena_bytes(const zt_gguf_t *g)
{
    return zt_tts_arena_bytes_ex(g, 0);
}

int32_t zt_tts_load(zt_tts_dec_t *d, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                    zt_model_err_t *err)
{
    return zt_tts_load_ex(d, g, arena, arena_bytes, 0, err);
}

int32_t zt_tts_load_ex(zt_tts_dec_t *d, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                       uint32_t flags, zt_model_err_t *err)
{
    if (!d || !g || !arena) {
        set_err(err, ZT_NN_EARG, "null argument", 0);
        return ZT_NN_EARG;
    }
    int32_t r = zt_tts_config(g, &d->cfg, err);
    if (r) return r;
    zt_nn_arena_t a;
    begin(&a, 0, 0, &d->cfg, flags, err);
    if ((r = plan(g, &d->cfg, 0, &a))) return r;
    if (zt_nn_arena_used(&a) > arena_bytes) {
        set_err(err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    begin(&a, arena, arena_bytes, &d->cfg, flags, err);
    if ((r = plan(g, &d->cfg, d, &a))) return r;
    d->kern = 0;
    set_err(err, ZT_GGUF_OK, "ok", 0);
    return ZT_GGUF_OK;
}

/* ---- decoder: the forward pass ---- */

typedef struct {
    zt_fx *x, *a, *b, *k, *v, *h, *col;
    void *nn_mem, *ist;
    uint64_t nn_bytes, ist_bytes;
} twork_t;

static uint32_t hcols(const zt_tts_cfg_t *c)
{
    uint32_t m = c->n_ff;
    if (c->n_out > m) m = c->n_out;
    if (c->n_feat > m) m = c->n_feat;
    return m;
}

static uint64_t work_layout(const zt_tts_cfg_t *c, uint32_t n, uint8_t *base, twork_t *w)
{
    uint64_t o = 0, TE = (uint64_t) n * c->n_embd * 4;
#define TTAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    TTAKE(w->x, TE);
    TTAKE(w->a, TE);
    TTAKE(w->b, TE);
    TTAKE(w->k, TE);
    TTAKE(w->v, TE);
    TTAKE(w->h, (uint64_t) n * hcols(c) * 4);
    TTAKE(w->col, (uint64_t) CHUNK * cols_max(c) * 4);
    w->nn_bytes = zt_nn_bytes(CHUNK, cols_max(c), n, c->n_embd, 1);
    TTAKE(w->nn_mem, w->nn_bytes);
    w->ist_bytes = zt_tts_istft_bytes(n);
    TTAKE(w->ist, w->ist_bytes);
#undef TTAKE
    return o + 64;
}

uint64_t zt_tts_work_bytes(const zt_tts_dec_t *d, uint32_t max_codes)
{
    twork_t w;
    return max_codes ? work_layout(&d->cfg, max_codes, 0, &w) : 0;
}

/* conv1d (stride 1, pad K/2) of x [T][C] into y [T][rows] via im2col chunks */
static void conv(zt_nn_t *nn, const zt_mat_t *w, const zt_fx *bias, const zt_fx *x, uint32_t T,
                 uint32_t C, uint32_t K, zt_fx *col, zt_fx *y, uint32_t ystride)
{
    uint32_t CK = C * K, pad = K / 2;
    for (uint32_t t0 = 0; t0 < T; t0 += CHUNK) {
        uint32_t B = T - t0 < CHUNK ? T - t0 : CHUNK;
        for (uint32_t t = 0; t < B; t++) {
            zt_fx *o = col + (uint64_t) t * CK;
            for (uint32_t c = 0; c < C; c++)
                for (uint32_t j = 0; j < K; j++) {
                    int64_t s = (int64_t) (t0 + t) - pad + j;
                    o[c * K + j] = s < 0 || s >= T ? 0 : x[(uint64_t) s * C + c];
                }
        }
        zt_nn_linear(nn, w, bias, col, CK, CK, B, y + (uint64_t) t0 * ystride, ystride);
    }
}

static void copy(zt_fx *dst, const zt_fx *src, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) dst[i] = src[i];
}

/* y[t][c] = round(y[t][c] * g[c] / 2^frac), saturating (counted) */
static void scale(zt_nn_t *nn, zt_fx *y, uint32_t T, uint32_t C, const zt_gain_t *g)
{
    for (uint64_t t = 0; t < T; t++)
        for (uint32_t c = 0; c < C; c++) {
            int64_t v = g->frac ? rshr((int64_t) y[t * C + c] * g->g[c], g->frac)
                                : (int64_t) y[t * C + c] * g->g[c];
            nn->n_sat += v > INT32_MAX || v < INT32_MIN;
            y[t * C + c] = sat32(v);
        }
}

/* The decoder up to the spectrogram; returns it in w->h (stride n_out). */
static int32_t run(const zt_tts_dec_t *d, const int32_t *codes, uint32_t T, twork_t *w,
                   uint32_t *n_sat)
{
    const zt_tts_cfg_t *c = &d->cfg;
    uint32_t E = c->n_embd, F = c->n_ff, G = c->n_groups, NF = c->n_feat;
    uint64_t TE = (uint64_t) T * E;
    zt_nn_t nn;
    int32_t r = zt_nn_init(&nn, CHUNK, cols_max(c), T, E, 1, w->nn_mem, w->nn_bytes);
    if (r) return r;
    nn.kern = d->kern;
    /* codebook rows into h ([T][n_feat]) */
    for (uint32_t t = 0; t < T; t++) {
        if (codes[t] < 0 || (uint32_t) codes[t] >= c->n_vocab) return ZT_NN_EARG;
        r = zt_gguf_dequant(&d->embd, (uint64_t) codes[t] * NF, NF, w->h + (uint64_t) t * NF);
        if (r) return r;
    }
    conv(&nn, &d->conv_in, d->conv_in_b, w->h, T, NF, 7, w->col, w->x, E);
    /* PosNet */
    for (uint32_t i = 0; i < 5; i++) {
        if (i == 2) {
            copy(w->a, w->x, TE);
            zt_nn_groupnorm(w->a, E, T, E, G, &d->att_norm, c->eps_gn_f32);
            zt_nn_linear(&nn, &d->wq, d->bq, w->a, E, E, T, w->b, E);
            zt_nn_linear(&nn, &d->wk, d->bk, w->a, E, E, T, w->k, E);
            zt_nn_linear(&nn, &d->wv, d->bv, w->a, E, E, T, w->v, E);
            zt_nn_attention(&nn, w->b, E, w->k, E, w->v, E, T, T, 1, E, false, w->a, E);
            zt_nn_linear(&nn, &d->wo, d->bo, w->a, E, E, T, w->b, E);
            zt_nn_add(&nn, w->x, E, w->b, E, E, T);
            continue;
        }
        const zt_tts_res_t *p = &d->res[i < 2 ? i : i - 1];
        copy(w->a, w->x, TE);
        zt_nn_groupnorm(w->a, E, T, E, G, &p->n1, c->eps_gn_f32);
        zt_nn_act(&nn, w->a, E, E, T, ZT_NN_SILU);
        conv(&nn, &p->c1, p->b1, w->a, T, E, 3, w->col, w->b, E);
        zt_nn_groupnorm(w->b, E, T, E, G, &p->n2, c->eps_gn_f32);
        zt_nn_act(&nn, w->b, E, E, T, ZT_NN_SILU);
        conv(&nn, &p->c2, p->b2, w->b, T, E, 3, w->col, w->a, E);
        zt_nn_add(&nn, w->x, E, w->a, E, E, T);
    }
    zt_nn_groupnorm(w->x, E, T, E, G, &d->pos_norm, c->eps_gn_f32);
    zt_nn_layernorm(&nn, w->x, E, E, T, &d->tok_norm, c->eps_ln_f32, w->a, E);
    copy(w->x, w->a, TE);
    /* ConvNeXt */
    for (uint32_t l = 0; l < c->n_convnext; l++) {
        const zt_tts_cnx_t *L = &d->cnx[l];
        zt_nn_dwconv1d(w->x, E, T, E, 7, 3, &L->dw, L->dw_b, w->a, E);
        zt_nn_layernorm(&nn, w->a, E, E, T, &L->norm, c->eps_ln_f32, w->b, E);
        zt_nn_linear(&nn, &L->pw1, L->b1, w->b, E, E, T, w->h, F);
        zt_nn_act(&nn, w->h, F, F, T, ZT_NN_GELU_TANH);
        zt_nn_linear(&nn, &L->pw2, L->b2, w->h, F, F, T, w->a, E);
        scale(&nn, w->a, T, E, &L->gamma);
        zt_nn_add(&nn, w->x, E, w->a, E, E, T);
    }
    zt_nn_layernorm(&nn, w->x, E, E, T, &d->out_norm, c->eps_ln_f32, w->a, E);
    zt_nn_linear(&nn, &d->out, d->out_b, w->a, E, E, T, w->h, c->n_out);
    *n_sat = nn.n_sat;
    return ZT_GGUF_OK;
}

static int32_t setup(const zt_tts_dec_t *d, const int32_t *codes, uint32_t n, void *work,
                     uint64_t work_bytes, twork_t *w)
{
    if (!d || !codes || !n || !work || work_bytes < zt_tts_work_bytes(d, n)) return ZT_NN_ESPACE;
    work_layout(&d->cfg, n, (uint8_t *) work + ((64 - ((uintptr_t) work & 63)) & 63), w);
    return ZT_GGUF_OK;
}

int32_t zt_tts_spec(const zt_tts_dec_t *d, const int32_t *codes, uint32_t n, zt_fx *spec,
                    void *work, uint64_t work_bytes, zt_tts_stats_t *st)
{
    twork_t w;
    uint32_t sat = 0;
    int32_t r = setup(d, codes, n, work, work_bytes, &w);
    if (!r) r = run(d, codes, n, &w, &sat);
    if (r) return r;
    if (spec) copy(spec, w.h, (uint64_t) n * d->cfg.n_out);
    if (st) {
        st->n_sat = sat;
        st->n_clip = 0;
        st->n_samples = 0;
    }
    return ZT_GGUF_OK;
}

int32_t zt_tts_decode(const zt_tts_dec_t *d, const int32_t *codes, uint32_t n, int16_t *pcm,
                      uint32_t skip, void *work, uint64_t work_bytes, zt_tts_stats_t *st)
{
    twork_t w;
    uint32_t sat = 0;
    int32_t r = setup(d, codes, n, work, work_bytes, &w);
    if (!r) r = run(d, codes, n, &w, &sat);
    if (r) return r;
    if (!pcm) return ZT_NN_EARG;
    uint32_t clip = zt_tts_istft(w.h, d->cfg.n_out, n, pcm, w.ist, w.ist_bytes, d->kern);
    uint64_t ns = (uint64_t) n * HOP;
    for (uint64_t i = 0; i < skip && i < ns; i++) pcm[i] = 0;
    if (st) {
        st->n_sat = sat;
        st->n_clip = clip;
        st->n_samples = (uint32_t) ns;
    }
    return ZT_GGUF_OK;
}

/* ---- iSTFT ---- */

zt_fx zt_tts_exp(zt_fx x)
{
    if (x <= 0) return zt_exp(x);
    if (x > 10 * 65536) x = 10 * 65536;
    /* x = k ln 2 + r with r in (-ln 2, 0] */
    uint32_t k = 0;
    int64_t r;
    do {
        k++;
        r = (int64_t) x - (int64_t) (((uint64_t) k * ZT_TTS_LN2_Q32 + (1u << 15)) >> 16);
    } while (r > 0);
    return sat32((int64_t) zt_exp((zt_fx) r) << k);
}

/* sin(2 pi i / 1024), Q30 */
static int64_t qsin(uint32_t i)
{
    uint32_t q = (i >> 8) & 3, r = i & 255;
    int64_t v = (q & 1) ? ZT_TTS_SIN[256 - r] : ZT_TTS_SIN[r];
    return q >= 2 ? -v : v;
}

static int32_t clamp30(int64_t v)
{
    return (int32_t) (v > (1 << 30) ? (1 << 30) : v < -(1 << 30) ? -(1 << 30) : v);
}

void zt_tts_cossin(zt_fx angle, int32_t *c, int32_t *s)
{
    /* the angle in turns * 2^64, modulo 2^64 (exact up to the rounding of
     * TURN_K: at most |angle| / 2 units of 2^-64 turn) */
    uint64_t t = (uint64_t) (int64_t) angle * ZT_TTS_TURN_K;
    uint32_t i = (uint32_t) (t >> 54), j = (uint32_t) (t >> 44) & 1023;
    uint64_t rem = t & ((1ull << 44) - 1);
    int64_t sa = qsin(i), ca = qsin((i + 256) & 1023), sf = ZT_TTS_FSIN[j], cf = ZT_TTS_FCOS[j];
    int64_t s0 = rshr(sa * cf + ca * sf, 30), c0 = rshr(ca * cf - sa * sf, 30);
    /* the rest of the angle, below 2 pi / 2^20, in Q30 radians: one Taylor
     * step (the next term is below 2e-11) */
    int64_t dl = (int64_t) (((rem >> 12) * TWOPI_Q28 + (1ull << 49)) >> 50);
    *s = clamp30(s0 + rshr(dl * c0, 30));
    *c = clamp30(c0 - rshr(dl * s0, 30));
}

typedef struct {
    int32_t *cosT, *sinT, *hann; /* [NFFT] */
    int32_t *ri;                 /* [n][2 * NBIN] re, im (Q16) */
    int32_t *y;                  /* [n][NFFT] Q24 */
    int64_t *acc;                /* [(n - 1) * HOP + NFFT] Q24 */
} iwork_t;

static uint64_t ist_layout(uint32_t n, uint8_t *base, iwork_t *w)
{
    uint64_t o = 0;
#define ITAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    ITAKE(w->cosT, NFFT * 4);
    ITAKE(w->sinT, NFFT * 4);
    ITAKE(w->hann, NFFT * 4);
    ITAKE(w->ri, (uint64_t) n * 2 * NBIN * 4);
    ITAKE(w->y, (uint64_t) n * NFFT * 4);
    ITAKE(w->acc, ((uint64_t) (n - 1) * HOP + NFFT) * 8);
#undef ITAKE
    return o + 64;
}

uint64_t zt_tts_istft_bytes(uint32_t n)
{
    iwork_t w;
    return n ? ist_layout(n, 0, &w) : 0;
}

typedef struct {
    const zt_fx *spec;
    uint32_t stride;
    const iwork_t *w;
} ijob_t;

/* frames [f0, f1): spectrum to re/im, then the inverse DFT */
static void ist_rows(void *ctx, uint32_t f0, uint32_t f1)
{
    const ijob_t *j = ctx;
    const iwork_t *w = j->w;
    for (uint32_t f = f0; f < f1; f++) {
        const zt_fx *sp = j->spec + (uint64_t) f * j->stride;
        int32_t *re = w->ri + (uint64_t) f * 2 * NBIN, *im = re + NBIN;
        for (uint32_t m = 0; m < NBIN; m++) {
            int64_t mag = sp[m] >= LN100_Q16 ? MAG_MAX : zt_tts_exp(sp[m]);
            int32_t cs, sn;
            zt_tts_cossin(sp[NBIN + m], &cs, &sn);
            re[m] = (int32_t) rshr(mag * cs, 30);
            im[m] = (int32_t) rshr(mag * sn, 30);
        }
        int32_t *y = w->y + (uint64_t) f * NFFT;
        for (uint32_t k = 0; k <= NFFT / 2; k++) {
            int64_t A = 0, B = 0;
            uint32_t idx = 0;
            for (uint32_t m = 0; m < NBIN; m++) {
                A += (int64_t) re[m] * w->cosT[idx];
                B += (int64_t) im[m] * w->sinT[idx];
                idx += k;
                if (idx >= NFFT) idx -= NFFT;
            }
            /* y = sum / 641, Q46 -> Q24 */
            y[k] = sat32(round_div(A - B, (uint64_t) NBIN << 22));
            if (k && k < NFFT / 2) y[NFFT - k] = sat32(round_div(A + B, (uint64_t) NBIN << 22));
        }
    }
}

uint32_t zt_tts_istft(const zt_fx *spec, uint32_t stride, uint32_t n, int16_t *pcm, void *work,
                      uint64_t work_bytes, const struct zt_kern *kern)
{
    if (!n || !spec || !pcm || !work || work_bytes < zt_tts_istft_bytes(n)) return 0;
    iwork_t w;
    ist_layout(n, (uint8_t *) work + ((64 - ((uintptr_t) work & 63)) & 63), &w);
    for (uint32_t j = 0; j < NFFT; j++) {
        int32_t v = j <= 320   ? ZT_TTS_TW[j]
                    : j <= 640 ? -ZT_TTS_TW[640 - j]
                    : j <= 960 ? -ZT_TTS_TW[j - 640]
                               : ZT_TTS_TW[NFFT - j];
        w.cosT[j] = v;
        w.hann[j] = (int32_t) (((int64_t) (1 << 30) - v + 1) >> 1); /* (1 - cos) / 2 */
    }
    for (uint32_t j = 0; j < NFFT; j++) w.sinT[j] = w.cosT[(j + 960) % NFFT];
    ijob_t job = {spec, stride, &w};
    if (kern && kern->par_rows)
        kern->par_rows(kern->pctx, ist_rows, &job, n, (uint64_t) n * NBIN * NBIN * 2);
    else
        ist_rows(&job, 0, n);
    /* overlap-add of y * hann */
    uint64_t L = (uint64_t) (n - 1) * HOP + NFFT;
    for (uint64_t i = 0; i < L; i++) w.acc[i] = 0;
    for (uint32_t f = 0; f < n; f++) {
        const int32_t *y = w.y + (uint64_t) f * NFFT;
        int64_t *a = w.acc + (uint64_t) f * HOP;
        for (uint32_t k = 0; k < NFFT; k++) a[k] += rshr((int64_t) y[k] * w.hann[k], 30);
    }
    /* divide by the overlap-added hann^2 and write 16-bit samples */
    uint32_t clip = 0;
    uint64_t ns = (uint64_t) n * HOP;
    for (uint64_t i = 0; i < ns; i++) {
        uint64_t p = i + PAD, env = 0;
        uint64_t f1 = zt_udiv64(p, HOP, 0); /* last frame that starts at or before p */
        for (uint64_t f = f1 + 1; f-- > 0;) {
            uint64_t k = p - f * HOP;
            if (k >= NFFT) break;
            if (f < n) env += (uint64_t) w.hann[k] * (uint64_t) w.hann[k];
        }
        int64_t e30 = (int64_t) ((env + (1ull << 29)) >> 30);
        int64_t v = e30 ? round_div(w.acc[p] * 32767 * 64, (uint64_t) e30) : 0;
        if (v > 32767 || v < -32768) {
            clip++;
            v = v > 32767 ? 32767 : -32768;
        }
        pcm[i] = (int16_t) v;
    }
    return clip;
}
