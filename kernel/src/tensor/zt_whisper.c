/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_whisper.c — Whisper speech to text in integers (T25). See zt_whisper.h. */
#include "zt_whisper.h"
#include "zt_kern.h"
#include "zt_whisper_tables.h"

#define Q24       ((int64_t) 1 << 24)
#define LOG_FLOOR (-10 * Q24) /* log10(1e-10) */

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

static uint32_t bitlen64(uint64_t v)
{
    uint32_t n = 0;
    while (v) {
        v >>= 1;
        n++;
    }
    return n;
}

static bool key_is(const zt_gguf_t *g, const char *key, const char *val)
{
    zt_gguf_val_t v;
    return zt_gguf_find(g, key, &v) == ZT_GGUF_OK && v.type == ZT_GGUF_STRING &&
           zt_gguf_str_eq(v.str, val);
}

/* "<pre><l>.<suffix>" */
static void bname(char *dst, uint32_t cap, const char *pre, uint32_t l, const char *suffix)
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

static uint32_t geti(const zt_gguf_t *g, const char *key)
{
    int64_t v = zt_gguf_get_int(g, key, -1);
    return v < 0 || v > 0x7FFFFFFF ? 0 : (uint32_t) v;
}

int32_t zt_whisper_config(const zt_gguf_t *g, zt_whisper_cfg_t *c, zt_model_err_t *err)
{
    if (!key_is(g, "general.architecture", "whisper")) {
        set_err(err, ZT_NN_ECONFIG, "not a whisper GGUF", "general.architecture");
        return ZT_NN_ECONFIG;
    }
    c->sample_rate = geti(g, "whisper.sample_rate");
    c->n_fft = geti(g, "whisper.n_fft");
    c->hop = geti(g, "whisper.hop_length");
    c->n_mels = geti(g, "whisper.n_mels");
    c->n_vocab = geti(g, "whisper.vocab_size");
    c->n_audio_ctx = geti(g, "whisper.audio.context_length");
    c->n_state = geti(g, "whisper.audio.embedding_length");
    c->n_audio_ff = geti(g, "whisper.audio.feed_forward_length");
    c->n_audio_head = geti(g, "whisper.audio.head_count");
    c->n_audio_layer = geti(g, "whisper.audio.block_count");
    c->n_text_ctx = geti(g, "whisper.text.context_length");
    c->n_text_ff = geti(g, "whisper.text.feed_forward_length");
    c->n_text_head = geti(g, "whisper.text.head_count");
    c->n_text_layer = geti(g, "whisper.text.block_count");
    c->eps_f32 = zt_nn_f32_key(g, "whisper.attention.layer_norm_epsilon", 0x3727C5ACu);
    uint32_t tstate = geti(g, "whisper.text.embedding_length");
    const char *tk[10] = {
        "eot",      "sot",       "translate",     "transcribe",      "sot_lm",
        "sot_prev", "no_speech", "no_timestamps", "timestamp_begin", "lang_first"};
    int32_t *tv[10] = {
        &c->eot,      &c->sot,       &c->translate,     &c->transcribe,      &c->sot_lm,
        &c->sot_prev, &c->no_speech, &c->no_timestamps, &c->timestamp_begin, &c->lang_first};
    for (int i = 0; i < 10; i++) {
        char key[48];
        const char *pre = "whisper.token.";
        uint32_t n = 0;
        for (; *pre; pre++) key[n++] = *pre;
        for (const char *s = tk[i]; *s && n < sizeof key - 1; s++) key[n++] = *s;
        key[n] = 0;
        int64_t v = zt_gguf_get_int(g, key, -1);
        if (v < 0 || v >= (int64_t) c->n_vocab) {
            set_err(err, ZT_NN_ECONFIG, "special token id missing or out of range", key);
            return ZT_NN_ECONFIG;
        }
        *tv[i] = (int32_t) v;
    }
    c->n_lang = geti(g, "whisper.token.lang_count");
    c->n_bins = c->n_fft / 2 + 1;
    c->n_frames = 2 * c->n_audio_ctx;
    c->n_samples = c->n_frames * c->hop;
    if (c->sample_rate != 16000 || c->n_fft != 400 || c->hop != 160) {
        set_err(err, ZT_NN_ECONFIG, "only 16 kHz, n_fft 400, hop 160 are supported", "whisper");
        return ZT_NN_ECONFIG;
    }
    if (!c->n_mels || c->n_mels > 256 || !c->n_state || c->n_state > 8192 || tstate != c->n_state ||
        !c->n_audio_ctx || c->n_audio_ctx > 4096 || !c->n_text_ctx || c->n_text_ctx > 4096 ||
        !c->n_audio_head || c->n_state % c->n_audio_head || c->n_state / c->n_audio_head > 512 ||
        !c->n_text_head || c->n_state % c->n_text_head || c->n_state / c->n_text_head > 512 ||
        !c->n_audio_layer || c->n_audio_layer > ZT_WHISPER_MAX_LAYERS || !c->n_text_layer ||
        c->n_text_layer > ZT_WHISPER_MAX_LAYERS || !c->n_audio_ff || c->n_audio_ff > 65536 ||
        !c->n_text_ff || c->n_text_ff > 65536 || !c->n_vocab || c->n_vocab > 1000000 ||
        c->lang_first + c->n_lang > c->n_vocab) {
        set_err(err, ZT_NN_ECONFIG, "whisper metadata out of range", "whisper");
        return ZT_NN_ECONFIG;
    }
    return ZT_GGUF_OK;
}

static uint32_t cols_max(const zt_whisper_cfg_t *c)
{
    uint32_t m = (3 * c->n_mels + 31) & ~31u;
    if (3 * c->n_state > m) m = 3 * c->n_state;
    if (c->n_audio_ff > m) m = c->n_audio_ff;
    if (c->n_text_ff > m) m = c->n_text_ff;
    return m;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* The mel filterbank: F32, non-negative; fraction bits so that the largest
 * value fits 31 bits and every band's sum stays below 2^32. */
static int32_t load_filters(zt_nn_arena_t *a, const zt_gguf_t *g, const zt_whisper_cfg_t *c,
                            zt_whisper_t *w)
{
    zt_gguf_tensor_t t;
    int32_t r = zt_nn_tensor(g, "mel_filters", c->n_bins, c->n_mels, &t, a->err);
    if (r) return r;
    if (t.type != ZT_GGML_F32) {
        set_err(a->err, ZT_GGUF_EUNSUPPORTED, "the mel filterbank must be F32", "mel_filters");
        return ZT_GGUF_EUNSUPPORTED;
    }
    uint64_t n = (uint64_t) c->n_mels * c->n_bins;
    uint32_t *f = zt_nn_take(a, n * 4);
    uint16_t *lo = zt_nn_take(a, c->n_mels * 2ull), *hi = zt_nn_take(a, c->n_mels * 2ull);
    int64_t mx = 0;
    for (uint64_t i = 0; i < n; i++) {
        uint32_t bits = le32(t.data + 4 * i);
        if ((bits >> 31) && (bits << 1)) {
            set_err(a->err, ZT_NN_ECONFIG, "negative mel filter value", "mel_filters");
            return ZT_NN_ECONFIG;
        }
        int64_t v = zt_nn_f32_fixed(bits & 0x7FFFFFFFu, 24);
        if (v > mx) mx = v;
    }
    int32_t frac = 24;
    while (frac < 48 && mx < ((int64_t) 1 << 30)) {
        frac++;
        mx <<= 1;
    }
    while (frac > 0 && mx > INT32_MAX) {
        frac--;
        mx >>= 1;
    }
    for (uint32_t m = 0; m < c->n_mels; m++) {
        uint64_t sum = 0;
        uint32_t l = c->n_bins, h = 0;
        for (uint32_t k = 0; k < c->n_bins; k++) {
            uint64_t i = (uint64_t) m * c->n_bins + k;
            int64_t v = zt_nn_f32_fixed(le32(t.data + 4 * i) & 0x7FFFFFFFu, frac);
            if (v > INT32_MAX) v = INT32_MAX;
            if (v) {
                if (k < l) l = k;
                h = k + 1;
            }
            sum += (uint64_t) v;
            if (f) f[i] = (uint32_t) v;
        }
        if (sum >> 32) {
            set_err(a->err, ZT_NN_ECONFIG, "a mel band's weights sum too large", "mel_filters");
            return ZT_NN_ECONFIG;
        }
        if (lo) lo[m] = (uint16_t) (l < h ? l : 0);
        if (hi) hi[m] = (uint16_t) (l < h ? h : 0);
    }
    if (zt_nn_filling(a)) {
        if (!f || !lo || !hi) {
            set_err(a->err, ZT_NN_ESPACE, "arena too small", "mel_filters");
            return ZT_NN_ESPACE;
        }
        w->mel_fil = f;
        w->fil_lo = lo;
        w->fil_hi = hi;
        w->fil_frac = (uint32_t) frac;
    }
    return ZT_GGUF_OK;
}

static int32_t load_bitmap(zt_nn_arena_t *a, const zt_gguf_t *g, const char *key, uint32_t n_vocab,
                           const uint8_t **out)
{
    uint8_t *b = zt_nn_take(a, (n_vocab + 7) / 8);
    zt_gguf_val_t v;
    bool have = zt_gguf_find(g, key, &v) == ZT_GGUF_OK && v.type == ZT_GGUF_ARRAY;
    if (!zt_nn_filling(a)) return ZT_GGUF_OK;
    if (!b) {
        set_err(a->err, ZT_NN_ESPACE, "arena too small", key);
        return ZT_NN_ESPACE;
    }
    for (uint32_t i = 0; i < (n_vocab + 7) / 8; i++) b[i] = 0;
    for (uint64_t i = 0; have && i < v.count; i++) {
        int64_t id;
        if (zt_gguf_arr_int(g, &v, i, &id) != ZT_GGUF_OK) {
            set_err(a->err, ZT_NN_ECONFIG, "not an integer array", key);
            return ZT_NN_ECONFIG;
        }
        if (id >= 0 && id < (int64_t) n_vocab) b[id >> 3] |= (uint8_t) (1u << (id & 7));
    }
    *out = b;
    return ZT_GGUF_OK;
}

static int32_t load_norm(zt_nn_arena_t *a, const zt_gguf_t *g, const char *pre, uint32_t l,
                         const char *nm, uint32_t n, zt_nn_norm_t *p)
{
    char s[64], full[96];
    uint32_t k = 0;
    for (const char *q = nm; *q && k < 40; q++) s[k++] = *q;
    const char *wsuf = ".weight", *bsuf = ".bias";
    uint32_t k0 = k;
    for (; *wsuf; wsuf++) s[k++] = *wsuf;
    s[k] = 0;
    int32_t r;
    if (pre)
        bname(full, sizeof full, pre, l, s);
    else
        for (k = 0; (full[k] = s[k]) != 0; k++) {
        }
    if ((r = zt_nn_load_gain(a, g, full, n, &p->g))) return r;
    k = k0;
    for (; *bsuf; bsuf++) s[k++] = *bsuf;
    s[k] = 0;
    if (pre)
        bname(full, sizeof full, pre, l, s);
    else
        for (k = 0; (full[k] = s[k]) != 0; k++) {
        }
    return zt_nn_load_q16(a, g, full, n, &p->b);
}

/* "<pre><l>.<p>q.weight" etc. */
static int32_t load_att(zt_nn_arena_t *a, const zt_gguf_t *g, const char *pre, uint32_t l,
                        const char *p, uint32_t d, zt_whisper_att_t *at)
{
    static const char *const part[7] = {"q.weight", "q.bias",     "k.weight", "v.weight",
                                        "v.bias",   "out.weight", "out.bias"};
    char s[48], nm[96];
    int32_t r;
    for (int i = 0; i < 7; i++) {
        uint32_t k = 0;
        for (const char *q = p; *q && k < 20; q++) s[k++] = *q;
        for (const char *q = part[i]; *q && k < sizeof s - 1; q++) s[k++] = *q;
        s[k] = 0;
        bname(nm, sizeof nm, pre, l, s);
        switch (i) {
        case 0:
            r = zt_nn_load_mat(a, g, nm, d, d, &at->wq);
            break;
        case 1:
            r = zt_nn_load_q16(a, g, nm, d, &at->bq);
            break;
        case 2:
            r = zt_nn_load_mat(a, g, nm, d, d, &at->wk);
            break;
        case 3:
            r = zt_nn_load_mat(a, g, nm, d, d, &at->wv);
            break;
        case 4:
            r = zt_nn_load_q16(a, g, nm, d, &at->bv);
            break;
        case 5:
            r = zt_nn_load_mat(a, g, nm, d, d, &at->wo);
            break;
        default:
            r = zt_nn_load_q16(a, g, nm, d, &at->bo);
            break;
        }
        if (r) return r;
    }
    return ZT_GGUF_OK;
}

static int32_t load_layer(zt_nn_arena_t *a, const zt_gguf_t *g, const char *pre, uint32_t l,
                          uint32_t d, uint32_t ff, bool cross, zt_whisper_layer_t *y)
{
    char nm[96];
    int32_t r;
    if ((r = load_norm(a, g, pre, l, "attn_ln", d, &y->ln_att))) return r;
    if ((r = load_att(a, g, pre, l, "attn_", d, &y->att))) return r;
    if (cross) {
        if ((r = load_norm(a, g, pre, l, "cross_ln", d, &y->ln_cross))) return r;
        if ((r = load_att(a, g, pre, l, "cross_", d, &y->cross))) return r;
    }
    if ((r = load_norm(a, g, pre, l, "mlp_ln", d, &y->ln_mlp))) return r;
    bname(nm, sizeof nm, pre, l, "fc1.weight");
    if ((r = zt_nn_load_mat(a, g, nm, d, ff, &y->w1))) return r;
    bname(nm, sizeof nm, pre, l, "fc1.bias");
    if ((r = zt_nn_load_q16(a, g, nm, ff, &y->b1))) return r;
    bname(nm, sizeof nm, pre, l, "fc2.weight");
    if ((r = zt_nn_load_mat(a, g, nm, ff, d, &y->w2))) return r;
    bname(nm, sizeof nm, pre, l, "fc2.bias");
    return zt_nn_load_q16(a, g, nm, d, &y->b2);
}

static int32_t plan(const zt_gguf_t *g, const zt_whisper_cfg_t *c, zt_whisper_t *w,
                    zt_nn_arena_t *a)
{
    int32_t r;
    uint32_t d = c->n_state;
    zt_whisper_t dw;
    if (!w) w = &dw;
    zt_whisper_layer_t *E = zt_nn_take(a, (uint64_t) c->n_audio_layer * sizeof *E);
    zt_whisper_layer_t *D = zt_nn_take(a, (uint64_t) c->n_text_layer * sizeof *D), dl;
    if (zt_nn_filling(a) && (!E || !D)) {
        set_err(a->err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    if ((r = load_filters(a, g, c, w))) return r;
    if ((r = zt_nn_load_mat(a, g, "enc.conv1.weight", 3 * c->n_mels, d, &w->conv1))) return r;
    if ((r = zt_nn_load_q16(a, g, "enc.conv1.bias", d, &w->conv1_b))) return r;
    if ((r = zt_nn_load_mat(a, g, "enc.conv2.weight", 3 * d, d, &w->conv2))) return r;
    if ((r = zt_nn_load_q16(a, g, "enc.conv2.bias", d, &w->conv2_b))) return r;
    if ((r = zt_nn_load_q16(a, g, "enc.pos", (uint64_t) c->n_audio_ctx * d, &w->enc_pos))) return r;
    for (uint32_t l = 0; l < c->n_audio_layer; l++)
        if ((r = load_layer(a, g, "enc.blk.", l, d, c->n_audio_ff, false, E ? &E[l] : &dl)))
            return r;
    if ((r = load_norm(a, g, 0, 0, "enc.ln", d, &w->enc_ln))) return r;
    if ((r = zt_nn_load_mat(a, g, "dec.token_embd.weight", d, c->n_vocab, &w->tok))) return r;
    if ((r = zt_nn_tensor(g, "dec.token_embd.weight", d, c->n_vocab, &w->tok_t, a->err))) return r;
    if (!zt_ggml_block(w->tok_t.type) || d % zt_ggml_block(w->tok_t.type)) {
        set_err(a->err, ZT_GGUF_EUNSUPPORTED, "token embedding type", "dec.token_embd.weight");
        return ZT_GGUF_EUNSUPPORTED;
    }
    if ((r = zt_nn_load_q16(a, g, "dec.pos", (uint64_t) c->n_text_ctx * d, &w->dec_pos))) return r;
    for (uint32_t l = 0; l < c->n_text_layer; l++)
        if ((r = load_layer(a, g, "dec.blk.", l, d, c->n_text_ff, true, D ? &D[l] : &dl))) return r;
    if ((r = load_norm(a, g, 0, 0, "dec.ln", d, &w->dec_ln))) return r;
    if ((r = load_bitmap(a, g, "whisper.suppress_tokens", c->n_vocab, &w->suppress))) return r;
    if ((r = load_bitmap(a, g, "whisper.begin_suppress_tokens", c->n_vocab, &w->suppress0)))
        return r;
    w->enc = E;
    w->dec = D;
    return ZT_GGUF_OK;
}

uint64_t zt_whisper_arena_bytes(const zt_gguf_t *g)
{
    zt_whisper_cfg_t c;
    if (zt_whisper_config(g, &c, 0)) return 0;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, cols_max(&c), 0);
    if (plan(g, &c, 0, &a)) return 0;
    return zt_nn_arena_used(&a);
}

int32_t zt_whisper_load(zt_whisper_t *w, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                        zt_model_err_t *err)
{
    if (!w || !g || !arena) {
        set_err(err, ZT_NN_EARG, "null argument", 0);
        return ZT_NN_EARG;
    }
    int32_t r = zt_whisper_config(g, &w->cfg, err);
    if (r) return r;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, cols_max(&w->cfg), err);
    if ((r = plan(g, &w->cfg, 0, &a))) return r;
    if (zt_nn_arena_used(&a) > arena_bytes) {
        set_err(err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    zt_nn_arena_begin(&a, arena, arena_bytes, cols_max(&w->cfg), err);
    if ((r = plan(g, &w->cfg, w, &a))) return r;
    w->kern = 0;
    set_err(err, ZT_GGUF_OK, "ok", 0);
    return ZT_GGUF_OK;
}

/* ---- state ---- */

static uint32_t max_hd(const zt_whisper_cfg_t *c)
{
    uint32_t a = c->n_state / c->n_audio_head, b = c->n_state / c->n_text_head;
    return a > b ? a : b;
}

static uint32_t max_heads(const zt_whisper_cfg_t *c)
{
    return c->n_audio_head > c->n_text_head ? c->n_audio_head : c->n_text_head;
}

#define NN_TOK 256u

static uint64_t state_layout(const zt_whisper_cfg_t *c, uint8_t *base, zt_whisper_state_t *s,
                             void **nn_mem, uint64_t *nn_bytes)
{
    uint64_t o = 0, d = c->n_state, A = c->n_audio_ctx, Tc = c->n_text_ctx, L = c->n_text_layer;
    uint64_t rows = A > Tc ? A : Tc;
    uint64_t hsz = (uint64_t) c->n_frames * d; /* conv1 output */
    if (A * c->n_audio_ff > hsz) hsz = A * c->n_audio_ff;
    if (Tc * c->n_text_ff > hsz) hsz = Tc * c->n_text_ff;
    uint64_t csz = (uint64_t) c->n_frames * 3 * c->n_mels; /* conv1 im2col */
    if (A * 3 * d > csz) csz = A * 3 * d;
    uint32_t keys = c->n_audio_ctx > c->n_text_ctx ? c->n_audio_ctx : c->n_text_ctx;
#define WTAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    WTAKE(s->mel, (uint64_t) c->n_frames * c->n_mels * 4);
    WTAKE(s->enc, A * d * 4);
    WTAKE(s->ck, L * A * d * 4);
    WTAKE(s->cv, L * A * d * 4);
    WTAKE(s->sk, L * Tc * d * 4);
    WTAKE(s->sv, L * Tc * d * 4);
    WTAKE(s->x, rows * d * 4);
    WTAKE(s->xn, rows * d * 4);
    WTAKE(s->q, rows * d * 4);
    WTAKE(s->k, rows * d * 4);
    WTAKE(s->v, rows * d * 4);
    WTAKE(s->att, rows * d * 4);
    WTAKE(s->h, hsz * 4);
    WTAKE(s->col, csz * 4);
    WTAKE(s->logits, (uint64_t) c->n_vocab * 4);
    *nn_bytes = zt_nn_bytes(NN_TOK, cols_max(c), keys, max_hd(c), max_heads(c));
    WTAKE(*nn_mem, *nn_bytes);
#undef WTAKE
    return o + 64;
}

uint64_t zt_whisper_state_bytes(const zt_whisper_t *w)
{
    zt_whisper_state_t s;
    void *m;
    uint64_t nb;
    return state_layout(&w->cfg, 0, &s, &m, &nb);
}

int32_t zt_whisper_state_init(zt_whisper_state_t *st, const zt_whisper_t *w, void *mem,
                              uint64_t bytes)
{
    if (!st || !w || !mem || bytes < zt_whisper_state_bytes(w)) return ZT_NN_ESPACE;
    const zt_whisper_cfg_t *c = &w->cfg;
    void *nn_mem;
    uint64_t nn_bytes;
    state_layout(c, (uint8_t *) mem + ((64 - ((uintptr_t) mem & 63)) & 63), st, &nn_mem, &nn_bytes);
    uint32_t keys = c->n_audio_ctx > c->n_text_ctx ? c->n_audio_ctx : c->n_text_ctx;
    int32_t r =
        zt_nn_init(&st->nn, NN_TOK, cols_max(c), keys, max_hd(c), max_heads(c), nn_mem, nn_bytes);
    if (r) return r;
    st->n_past = 0;
    st->n_sat = 0;
    return ZT_GGUF_OK;
}

/* ---- log-mel ---- */

/* log2(v) in Q24 for v >= 1, by repeated squaring of a Q30 mantissa */
static int64_t log2_q24(uint64_t v)
{
    uint32_t b = bitlen64(v);
    uint64_t m = b > 31 ? v >> (b - 31) : v << (31 - b); /* [2^30, 2^31) */
    uint32_t frac = 0;
    for (int i = 0; i < 25; i++) {
        m = (m * m + (1ull << 29)) >> 30;
        frac <<= 1;
        if (m >= (1ull << 31)) {
            m >>= 1;
            frac |= 1;
        }
    }
    return ((int64_t) (b - 1) << 24) + ((frac + 1) >> 1);
}

/* signed a / 2^sh, rounded half away from zero */
static int64_t rshift_round(int64_t a, uint32_t sh)
{
    if (!sh) return a;
    uint64_t u = a < 0 ? (uint64_t) 0 - (uint64_t) a : (uint64_t) a;
    u = (u + (1ull << (sh - 1))) >> sh;
    return a < 0 ? -(int64_t) u : (int64_t) u;
}

typedef struct {
    const zt_whisper_t *w;
    const int16_t *pcm;
    uint64_t n;   /* samples given (the rest of n_samples is zero) */
    int32_t *out; /* [n_frames][n_mels] log10 in Q24 */
} mel_job_t;

static int32_t sample_at(const mel_job_t *j, int64_t p)
{
    /* p in the reflection-padded signal; the signal is n_samples long */
    int64_t L = j->w->cfg.n_samples, i = p - (int64_t) (j->w->cfg.n_fft / 2);
    if (i < 0) i = -i;
    if (i >= L) i = 2 * (L - 1) - i;
    return (uint64_t) i < j->n ? j->pcm[i] : 0;
}

static void mel_rows(void *ctx, uint32_t f0, uint32_t f1)
{
    const mel_job_t *j = ctx;
    const zt_whisper_t *w = j->w;
    const zt_whisper_cfg_t *c = &w->cfg;
    int32_t xw[400];
    uint64_t P[201];
    for (uint32_t f = f0; f < f1; f++) {
        int32_t *o = j->out + (uint64_t) f * c->n_mels;
        uint64_t S = 0;
        int64_t p0 = (int64_t) f * c->hop;
        for (uint32_t n = 0; n < 400; n++) {
            int32_t s = sample_at(j, p0 + n);
            uint64_t wn = (uint64_t) ((int64_t) (1 << 30) - ZT_WH_COS400[n]); /* window * 2^31 */
            xw[n] = s;
            S += (uint64_t) (s < 0 ? -s : s) * wn;
        }
        if (!S) {
            for (uint32_t m = 0; m < c->n_mels; m++) o[m] = (int32_t) LOG_FLOOR;
            continue;
        }
        /* the fewest bits that keep sum |x * w| below 2^31 (with rounding) */
        uint32_t sh = 0;
        while ((S >> sh) + 400 >= (1ull << 31)) sh++;
        for (uint32_t n = 0; n < 400; n++) {
            int64_t wn = (int64_t) (1 << 30) - ZT_WH_COS400[n];
            xw[n] = (int32_t) rshift_round((int64_t) xw[n] * wn, sh);
        }
        /* X_k = sum x_n e^{-2 pi i k n / 400}; units 2^-(31 - sh) of a sample */
        for (uint32_t k = 0; k < 201; k++) {
            int64_t re = 0, im = 0;
            uint32_t ic = 0, is = 300; /* cos index k n, sine index k n + 300 (mod 400) */
            for (uint32_t n = 0; n < 400; n++) {
                re += (int64_t) xw[n] * ZT_WH_COS400[ic];
                im += (int64_t) xw[n] * ZT_WH_COS400[is];
                ic += k;
                if (ic >= 400) ic -= 400;
                is += k;
                if (is >= 400) is -= 400;
            }
            int64_t xr = rshift_round(re, 30), xi = rshift_round(im, 30);
            P[k] = (uint64_t) (xr * xr) + (uint64_t) (xi * xi);
        }
        /* mel = filters . P as hi * 2^32 + lo (exact), then log10 */
        for (uint32_t m = 0; m < c->n_mels; m++) {
            const uint32_t *fm = w->mel_fil + (uint64_t) m * c->n_bins;
            uint64_t hi = 0, lo = 0;
            for (uint32_t k = w->fil_lo[m]; k < w->fil_hi[m]; k++) {
                hi += (uint64_t) fm[k] * (P[k] >> 32);
                lo += (uint64_t) fm[k] * (P[k] & 0xFFFFFFFFu);
            }
            uint64_t top = hi + (lo >> 32), low = lo & 0xFFFFFFFFu;
            if (!top && !low) {
                o[m] = (int32_t) LOG_FLOOR;
                continue;
            }
            int64_t l2;
            uint32_t b = bitlen64(top);
            if (b <= 32)
                l2 = log2_q24((top << 32) | low);
            else
                l2 = log2_q24((top << (64 - b)) | (low >> (b - 32))) + ((int64_t) (b - 32) << 24);
            /* mel = value * 2^-fil_frac * 2^-2(31 - sh) * 2^-30 (sample^2 -> (s/32768)^2) */
            l2 -= (int64_t) (w->fil_frac + 2 * (31 - sh) + 30) << 24;
            int64_t l10 = rshift_round(l2 * ZT_WH_LOG10_2_Q32, 32);
            o[m] = (int32_t) (l10 < LOG_FLOOR ? LOG_FLOOR : l10);
        }
    }
}

int32_t zt_whisper_mel(const zt_whisper_t *w, zt_whisper_state_t *st, const int16_t *pcm,
                       uint64_t n)
{
    if (!w || !st || (!pcm && n)) return ZT_NN_EARG;
    const zt_whisper_cfg_t *c = &w->cfg;
    uint64_t N = (uint64_t) c->n_frames * c->n_mels;
    int32_t *out = st->mel;
    st->n_sat = 0;
    mel_job_t j = {w, pcm, n < c->n_samples ? n : c->n_samples, out};
    const zt_kern_t *k = w->kern;
    if (k && k->par_rows)
        k->par_rows(k->pctx, mel_rows, &j, c->n_frames, (uint64_t) c->n_frames * 201 * 800);
    else
        mel_rows(&j, 0, c->n_frames);
    int64_t mx = LOG_FLOOR;
    for (uint64_t i = 0; i < N; i++)
        if (out[i] > mx) mx = out[i];
    int64_t floor = mx - 8 * Q24;
    for (uint64_t i = 0; i < N; i++) {
        int64_t v = out[i] < floor ? floor : out[i];
        out[i] = (zt_fx) rshift_round(v + 4 * Q24, 10); /* (x + 4) / 4, Q24 -> Q16 */
    }
    return ZT_GGUF_OK;
}

/* ---- encoder ---- */

int32_t zt_whisper_encode(const zt_whisper_t *w, zt_whisper_state_t *st)
{
    if (!w || !st) return ZT_NN_EARG;
    const zt_whisper_cfg_t *c = &w->cfg;
    zt_nn_t *nn = &st->nn;
    nn->kern = w->kern;
    nn->n_sat = 0;
    uint32_t d = c->n_state, A = c->n_audio_ctx, F = c->n_frames, ff = c->n_audio_ff;
    uint32_t c1 = 3 * c->n_mels, hd = d / c->n_audio_head;
    /* conv1 + GELU: [F][n_mels] -> [F][d] (in h) */
    zt_nn_im2col1d(st->mel, c->n_mels, F, c->n_mels, 3, 1, 1, st->col, c1);
    zt_nn_linear(nn, &w->conv1, w->conv1_b, st->col, c1, c1, F, st->h, d);
    zt_nn_act(nn, st->h, d, d, F, ZT_NN_GELU_ERF);
    /* conv2 (stride 2) + GELU: [F][d] -> [A][d] (in x), + positions */
    if (zt_nn_im2col1d(st->h, d, F, d, 3, 2, 1, st->col, 3 * d) != A) return ZT_NN_ESHAPE;
    zt_nn_linear(nn, &w->conv2, w->conv2_b, st->col, 3 * d, 3 * d, A, st->x, d);
    zt_nn_act(nn, st->x, d, d, A, ZT_NN_GELU_ERF);
    zt_nn_add(nn, st->x, d, w->enc_pos, d, d, A);
    for (uint32_t l = 0; l < c->n_audio_layer; l++) {
        const zt_whisper_layer_t *L = &w->enc[l];
        zt_nn_layernorm(nn, st->x, d, d, A, &L->ln_att, c->eps_f32, st->xn, d);
        zt_nn_linear(nn, &L->att.wq, L->att.bq, st->xn, d, d, A, st->q, d);
        zt_nn_linear(nn, &L->att.wk, 0, st->xn, d, d, A, st->k, d);
        zt_nn_linear(nn, &L->att.wv, L->att.bv, st->xn, d, d, A, st->v, d);
        zt_nn_attention(nn, st->q, d, st->k, d, st->v, d, A, A, c->n_audio_head, hd, false, st->att,
                        d);
        zt_nn_linear(nn, &L->att.wo, L->att.bo, st->att, d, d, A, st->xn, d);
        zt_nn_add(nn, st->x, d, st->xn, d, d, A);
        zt_nn_layernorm(nn, st->x, d, d, A, &L->ln_mlp, c->eps_f32, st->xn, d);
        zt_nn_linear(nn, &L->w1, L->b1, st->xn, d, d, A, st->h, ff);
        zt_nn_act(nn, st->h, ff, ff, A, ZT_NN_GELU_ERF);
        zt_nn_linear(nn, &L->w2, L->b2, st->h, ff, ff, A, st->xn, d);
        zt_nn_add(nn, st->x, d, st->xn, d, d, A);
    }
    zt_nn_layernorm(nn, st->x, d, d, A, &w->enc_ln, c->eps_f32, st->enc, d);
    /* every decoder layer's cross-attention K and V, once per utterance */
    for (uint32_t l = 0; l < c->n_text_layer; l++) {
        const zt_whisper_att_t *X = &w->dec[l].cross;
        zt_fx *ck = st->ck + (uint64_t) l * A * d, *cv = st->cv + (uint64_t) l * A * d;
        zt_nn_linear(nn, &X->wk, 0, st->enc, d, d, A, ck, d);
        zt_nn_linear(nn, &X->wv, X->bv, st->enc, d, d, A, cv, d);
    }
    st->n_sat += nn->n_sat;
    st->n_past = 0;
    return ZT_GGUF_OK;
}

/* ---- decoder ---- */

int32_t zt_whisper_decode(const zt_whisper_t *w, zt_whisper_state_t *st, const int32_t *ids,
                          uint32_t n, zt_fx *logits, bool all)
{
    if (!w || !st || !ids || !n) return ZT_NN_EARG;
    const zt_whisper_cfg_t *c = &w->cfg;
    if (st->n_past + n > c->n_text_ctx || n > c->n_audio_ctx) return ZT_NN_ESPACE;
    zt_nn_t *nn = &st->nn;
    nn->kern = w->kern;
    nn->n_sat = 0;
    uint32_t d = c->n_state, A = c->n_audio_ctx, Tc = c->n_text_ctx, ff = c->n_text_ff;
    uint32_t hd = d / c->n_text_head, p0 = st->n_past;
    for (uint32_t i = 0; i < n; i++) {
        if (ids[i] < 0 || (uint32_t) ids[i] >= c->n_vocab) return ZT_NN_EARG;
        zt_fx *x = st->x + (uint64_t) i * d;
        int32_t r = zt_gguf_dequant(&w->tok_t, (uint64_t) ids[i] * d, d, x);
        if (r) return r;
    }
    zt_nn_add(nn, st->x, d, w->dec_pos + (uint64_t) p0 * d, d, d, n);
    for (uint32_t l = 0; l < c->n_text_layer; l++) {
        const zt_whisper_layer_t *L = &w->dec[l];
        zt_fx *sk = st->sk + (uint64_t) l * Tc * d, *sv = st->sv + (uint64_t) l * Tc * d;
        zt_nn_layernorm(nn, st->x, d, d, n, &L->ln_att, c->eps_f32, st->xn, d);
        zt_nn_linear(nn, &L->att.wq, L->att.bq, st->xn, d, d, n, st->q, d);
        zt_nn_linear(nn, &L->att.wk, 0, st->xn, d, d, n, sk + (uint64_t) p0 * d, d);
        zt_nn_linear(nn, &L->att.wv, L->att.bv, st->xn, d, d, n, sv + (uint64_t) p0 * d, d);
        zt_nn_attention(nn, st->q, d, sk, d, sv, d, n, p0 + n, c->n_text_head, hd, true, st->att,
                        d);
        zt_nn_linear(nn, &L->att.wo, L->att.bo, st->att, d, d, n, st->xn, d);
        zt_nn_add(nn, st->x, d, st->xn, d, d, n);
        zt_nn_layernorm(nn, st->x, d, d, n, &L->ln_cross, c->eps_f32, st->xn, d);
        zt_nn_linear(nn, &L->cross.wq, L->cross.bq, st->xn, d, d, n, st->q, d);
        zt_nn_attention(nn, st->q, d, st->ck + (uint64_t) l * A * d, d,
                        st->cv + (uint64_t) l * A * d, d, n, A, c->n_text_head, hd, false, st->att,
                        d);
        zt_nn_linear(nn, &L->cross.wo, L->cross.bo, st->att, d, d, n, st->xn, d);
        zt_nn_add(nn, st->x, d, st->xn, d, d, n);
        zt_nn_layernorm(nn, st->x, d, d, n, &L->ln_mlp, c->eps_f32, st->xn, d);
        zt_nn_linear(nn, &L->w1, L->b1, st->xn, d, d, n, st->h, ff);
        zt_nn_act(nn, st->h, ff, ff, n, ZT_NN_GELU_ERF);
        zt_nn_linear(nn, &L->w2, L->b2, st->h, ff, ff, n, st->xn, d);
        zt_nn_add(nn, st->x, d, st->xn, d, d, n);
    }
    st->n_past += n;
    if (logits) {
        uint32_t first = all ? 0 : n - 1, rows = n - first;
        zt_nn_layernorm(nn, st->x + (uint64_t) first * d, d, d, rows, &w->dec_ln, c->eps_f32,
                        st->xn, d);
        zt_nn_linear(nn, &w->tok, 0, st->xn, d, d, rows, logits, c->n_vocab);
    }
    st->n_sat += nn->n_sat;
    return ZT_GGUF_OK;
}

static int32_t argmax_masked(const zt_fx *l, uint32_t n, const uint8_t *m0, const uint8_t *m1,
                             uint32_t lo, uint32_t hi)
{
    int32_t best = -1;
    zt_fx bv = INT32_MIN;
    for (uint32_t i = 0; i < n; i++) {
        if (i >= lo && i < hi) continue;
        if (m0 && (m0[i >> 3] >> (i & 7)) & 1) continue;
        if (m1 && (m1[i >> 3] >> (i & 7)) & 1) continue;
        if (best < 0 || l[i] > bv) {
            best = (int32_t) i;
            bv = l[i];
        }
    }
    return best;
}

int32_t zt_whisper_detect_lang(const zt_whisper_t *w, zt_whisper_state_t *st, uint32_t *lang)
{
    const zt_whisper_cfg_t *c = &w->cfg;
    if (!c->n_lang || !lang) return ZT_NN_ECONFIG;
    st->n_past = 0;
    int32_t r = zt_whisper_decode(w, st, &c->sot, 1, st->logits, false);
    if (r) return r;
    uint32_t best = 0;
    for (uint32_t i = 1; i < c->n_lang; i++)
        if (st->logits[c->lang_first + i] > st->logits[c->lang_first + best]) best = i;
    *lang = best;
    st->n_past = 0;
    return ZT_GGUF_OK;
}

int32_t zt_whisper_greedy(const zt_whisper_t *w, zt_whisper_state_t *st, uint32_t lang,
                          bool translate, uint32_t max_tokens, int32_t *out, uint32_t cap,
                          uint32_t *n_out, zt_whisper_step_fn fn, void *ctx)
{
    const zt_whisper_cfg_t *c = &w->cfg;
    if (!out || !n_out || (c->n_lang && lang >= c->n_lang)) return ZT_NN_EARG;
    *n_out = 0;
    int32_t prompt[4], np = 0;
    prompt[np++] = c->sot;
    if (c->n_lang) prompt[np++] = c->lang_first + (int32_t) lang;
    prompt[np++] = translate ? c->translate : c->transcribe;
    prompt[np++] = c->no_timestamps;
    st->n_past = 0;
    int32_t r = zt_whisper_decode(w, st, prompt, (uint32_t) np, st->logits, false);
    if (r) return r;
    /* every special token after <|endoftext|> is excluded (no timestamps) */
    uint32_t lo = (uint32_t) c->eot + 1, hi = c->n_vocab;
    for (uint32_t step = 0;; step++) {
        int32_t t =
            argmax_masked(st->logits, c->n_vocab, w->suppress, step ? 0 : w->suppress0, lo, hi);
        if (fn && !fn(ctx, step, st->logits, t)) break;
        if (t < 0 || t == c->eot) break;
        out[(*n_out)++] = t;
        if (*n_out >= cap || *n_out >= max_tokens || st->n_past >= c->n_text_ctx) break;
        if ((r = zt_whisper_decode(w, st, &t, 1, st->logits, false))) return r;
    }
    return ZT_GGUF_OK;
}

int32_t zt_whisper_transcribe(const zt_whisper_t *w, zt_whisper_state_t *st, const int16_t *pcm,
                              uint64_t n, int32_t lang, int32_t *out, uint32_t cap, uint32_t *n_out)
{
    int32_t r = zt_whisper_mel(w, st, pcm, n);
    if (r) return r;
    if ((r = zt_whisper_encode(w, st))) return r;
    uint32_t li = lang < 0 ? 0 : (uint32_t) lang;
    if (lang < 0 && w->cfg.n_lang && (r = zt_whisper_detect_lang(w, st, &li))) return r;
    return zt_whisper_greedy(w, st, li, false, w->cfg.n_text_ctx / 2, out, cap, n_out, 0, 0);
}

/* ---- language codes ---- */

typedef struct {
    uint64_t want;
    const char *code;
    zt_gguf_str_t s;
    int64_t found;
} lang_walk_t;

static bool lang_visit(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    lang_walk_t *lw = ctx;
    if (lw->code ? zt_gguf_str_eq(s, lw->code) : i == lw->want) {
        lw->s = s;
        lw->found = (int64_t) i;
        return false;
    }
    return true;
}

zt_gguf_str_t zt_whisper_lang_code(const zt_gguf_t *g, uint32_t lang)
{
    zt_gguf_val_t v;
    lang_walk_t lw = {lang, 0, {0, 0}, -1};
    if (zt_gguf_find(g, "whisper.languages", &v) == ZT_GGUF_OK && v.type == ZT_GGUF_ARRAY &&
        v.elem_type == ZT_GGUF_STRING)
        zt_gguf_arr_strings(g, &v, lang_visit, &lw);
    return lw.s;
}

int32_t zt_whisper_lang_find(const zt_gguf_t *g, const char *code)
{
    zt_gguf_val_t v;
    lang_walk_t lw = {0, code, {0, 0}, -1};
    if (zt_gguf_find(g, "whisper.languages", &v) == ZT_GGUF_OK && v.type == ZT_GGUF_ARRAY &&
        v.elem_type == ZT_GGUF_STRING)
        zt_gguf_arr_strings(g, &v, lang_visit, &lw);
    return (int32_t) lw.found;
}
