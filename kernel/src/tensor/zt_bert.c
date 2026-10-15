/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_bert.c — BERT and ELECTRA encoders and their WordPiece tokenizer (T26).
 * See zt_bert.h. */
#include "zt_bert.h"
#include "zt_wp_tables.h"

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

static uint32_t cat(char *dst, uint32_t n, uint32_t cap, const char *s)
{
    for (; *s && n < cap - 1; s++) dst[n++] = *s;
    dst[n] = 0;
    return n;
}

/* "<arch>.<suffix>" */
static void akey(char *dst, uint32_t cap, uint32_t arch, const char *suffix)
{
    uint32_t n = cat(dst, 0, cap, arch == ZT_BERT_ELECTRA ? "electra." : "bert.");
    cat(dst, n, cap, suffix);
}

/* "blk.<l>.<a><b>" */
static void blk(char *dst, uint32_t cap, uint32_t l, const char *a, const char *b)
{
    char num[12];
    uint32_t k = 0, n = cat(dst, 0, cap, "blk.");
    do {
        num[k++] = (char) ('0' + l % 10);
        l /= 10;
    } while (l);
    while (k && n < cap - 1) dst[n++] = num[--k];
    dst[n] = 0;
    n = cat(dst, n, cap, ".");
    n = cat(dst, n, cap, a);
    cat(dst, n, cap, b);
}

static bool key_is(const zt_gguf_t *g, const char *key, const char *val)
{
    zt_gguf_val_t v;
    return zt_gguf_find(g, key, &v) == ZT_GGUF_OK && v.type == ZT_GGUF_STRING &&
           zt_gguf_str_eq(v.str, val);
}

static int32_t need_flag(const zt_gguf_t *g, const char *key, bool *out, zt_model_err_t *err)
{
    int64_t v = zt_gguf_get_int(g, key, -1);
    if (v != 0 && v != 1) {
        set_err(err, ZT_NN_EMISSING, "tokenizer flag missing (convert with convert_bert.py)", key);
        return ZT_NN_EMISSING;
    }
    *out = v == 1;
    return ZT_GGUF_OK;
}

int32_t zt_bert_config(const zt_gguf_t *g, zt_bert_cfg_t *c, zt_model_err_t *err)
{
    char k[96];
    int32_t r;
    if (key_is(g, "general.architecture", "bert"))
        c->arch = ZT_BERT_BERT;
    else if (key_is(g, "general.architecture", "electra"))
        c->arch = ZT_BERT_ELECTRA;
    else {
        set_err(err, ZT_NN_ECONFIG, "not a BERT or ELECTRA encoder", "general.architecture");
        return ZT_NN_ECONFIG;
    }
    if (!key_is(g, "tokenizer.ggml.model", "bert")) {
        set_err(err, ZT_NN_ECONFIG, "not a WordPiece vocabulary", "tokenizer.ggml.model");
        return ZT_NN_ECONFIG;
    }
    akey(k, sizeof k, c->arch, "context_length");
    c->n_ctx = (uint32_t) zt_gguf_get_int(g, k, 0);
    akey(k, sizeof k, c->arch, "embedding_length");
    c->n_embd = (uint32_t) zt_gguf_get_int(g, k, 0);
    akey(k, sizeof k, c->arch, "feed_forward_length");
    c->n_ff = (uint32_t) zt_gguf_get_int(g, k, 0);
    akey(k, sizeof k, c->arch, "attention.head_count");
    c->n_head = (uint32_t) zt_gguf_get_int(g, k, 0);
    akey(k, sizeof k, c->arch, "block_count");
    c->n_layer = (uint32_t) zt_gguf_get_int(g, k, 0);
    akey(k, sizeof k, c->arch, "pooling_type");
    c->pooling = (uint32_t) zt_gguf_get_int(g, k, 0);
    akey(k, sizeof k, c->arch, "attention.layer_norm_epsilon");
    c->eps_f32 = zt_nn_f32_key(g, k, 0x2B8CBCCCu); /* 1e-12 */
    c->n_types = (uint32_t) zt_gguf_get_int(g, "tokenizer.ggml.token_type_count", 2);
    c->max_chars = (uint32_t) zt_gguf_get_int(g, "zxv.wordpiece.max_chars", 0);
    c->max_tokens = (uint32_t) zt_gguf_get_int(g, "zxv.wordpiece.max_tokens", 0);
    c->cls_id = (int32_t) zt_gguf_get_int(g, "tokenizer.ggml.bos_token_id", -1); /* [CLS] */
    c->sep_id = (int32_t) zt_gguf_get_int(g, "tokenizer.ggml.seperator_token_id", -1);
    c->unk_id = (int32_t) zt_gguf_get_int(g, "tokenizer.ggml.unknown_token_id", -1);
    if ((r = need_flag(g, "zxv.wordpiece.clean_text", &c->clean_text, err)) ||
        (r = need_flag(g, "zxv.wordpiece.chinese_chars", &c->chinese_chars, err)) ||
        (r = need_flag(g, "zxv.wordpiece.strip_accents", &c->strip_accents, err)) ||
        (r = need_flag(g, "zxv.wordpiece.lowercase", &c->lowercase, err)) ||
        (r = need_flag(g, "zxv.pooling.normalize", &c->normalize, err)))
        return r;
    zt_gguf_val_t v;
    if (zt_gguf_find(g, "tokenizer.ggml.tokens", &v) != ZT_GGUF_OK || v.type != ZT_GGUF_ARRAY ||
        v.elem_type != ZT_GGUF_STRING || !v.count || v.count > 0x1000000u) {
        set_err(err, ZT_NN_EMISSING, "no vocabulary", "tokenizer.ggml.tokens");
        return ZT_NN_EMISSING;
    }
    c->n_vocab = (uint32_t) v.count;
    c->n_labels = 0;
    zt_gguf_tensor_t t;
    if (zt_gguf_find_tensor(g, "cls.output.weight", &t) == ZT_GGUF_OK) {
        c->n_labels = t.n_dims == 2 ? (uint32_t) t.dims[1] : 0;
        akey(k, sizeof k, c->arch, "classifier.output_labels");
        if (!c->n_labels || c->n_labels > 4096 || zt_gguf_find(g, k, &v) != ZT_GGUF_OK ||
            v.type != ZT_GGUF_ARRAY || v.elem_type != ZT_GGUF_STRING || v.count != c->n_labels) {
            set_err(err, ZT_NN_ECONFIG, "labels do not match the classifier", k);
            return ZT_NN_ECONFIG;
        }
    }
    if (!c->n_ctx || c->n_ctx > 8192 || !c->n_embd || c->n_embd > 8192 || !c->n_ff ||
        c->n_ff > 32768 || !c->n_head || c->n_embd % c->n_head || c->n_embd / c->n_head > 512 ||
        !c->n_layer || c->n_layer > 256 || !c->n_types || c->n_types > 16 ||
        (c->pooling != ZT_BERT_POOL_MEAN && c->pooling != ZT_BERT_POOL_CLS) ||
        (c->n_labels && c->pooling != ZT_BERT_POOL_CLS) || !c->max_chars ||
        c->max_chars > ZT_BERT_MAX_CHARS || c->max_tokens < 3 || c->max_tokens > c->n_ctx ||
        c->cls_id < 0 || (uint32_t) c->cls_id >= c->n_vocab || c->sep_id < 0 ||
        (uint32_t) c->sep_id >= c->n_vocab || c->unk_id < 0 || (uint32_t) c->unk_id >= c->n_vocab) {
        set_err(err, ZT_NN_ECONFIG, "encoder metadata out of range", "bert");
        return ZT_NN_ECONFIG;
    }
    if (zt_gguf_find_tensor(g, "token_embd.weight", &t) != ZT_GGUF_OK || t.n_dims != 2 ||
        t.dims[0] != c->n_embd || t.dims[1] != c->n_vocab || !zt_ggml_block(t.type) ||
        c->n_embd % zt_ggml_block(t.type)) {
        set_err(err, ZT_NN_ESHAPE, "token embedding missing or of the wrong shape or type",
                "token_embd.weight");
        return ZT_NN_ESHAPE;
    }
    return ZT_GGUF_OK;
}

/* ---- the vocabulary: strings in place, an open-addressed hash of them ---- */

static uint32_t fnv(uint32_t h, const uint8_t *s, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) h = (h ^ s[i]) * 16777619u;
    return h;
}

#define FNV0 2166136261u

static bool str_is(zt_gguf_str_t s, const uint8_t *p, uint64_t n)
{
    if (s.len != n) return false;
    for (uint64_t i = 0; i < n; i++)
        if (s.p[i] != p[i]) return false;
    return true;
}

typedef struct {
    zt_bert_t *b;
    bool dup;
} vfill_t;

static bool vocab_put(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    vfill_t *f = ctx;
    zt_bert_t *b = f->b;
    b->vocab[i] = s;
    uint32_t h = fnv(FNV0, s.p, s.len) & b->hash_mask;
    while (b->hash[h]) {
        if (str_is(b->vocab[b->hash[h] - 1], s.p, s.len)) {
            f->dup = true;
            return false;
        }
        h = (h + 1) & b->hash_mask;
    }
    b->hash[h] = (uint32_t) i + 1;
    return true;
}

static int32_t lookup2(const zt_bert_t *b, const uint8_t *pre, uint32_t npre, const uint8_t *s,
                       uint64_t n)
{
    uint32_t h = fnv(fnv(FNV0, pre, npre), s, n) & b->hash_mask;
    for (uint32_t id; (id = b->hash[h]) != 0; h = (h + 1) & b->hash_mask) {
        zt_gguf_str_t v = b->vocab[id - 1];
        if (v.len != npre + n) continue;
        bool same = true;
        for (uint32_t i = 0; i < npre && same; i++) same = v.p[i] == pre[i];
        for (uint64_t i = 0; i < n && same; i++) same = v.p[npre + i] == s[i];
        if (same) return (int32_t) id - 1;
    }
    return -1;
}

int32_t zt_bert_find(const zt_bert_t *b, const uint8_t *s, uint64_t len)
{
    return lookup2(b, 0, 0, s, len);
}

int32_t zt_bert_label(const zt_bert_t *b, uint32_t i, zt_gguf_str_t *out)
{
    char k[96];
    zt_gguf_val_t v;
    akey(k, sizeof k, b->cfg.arch, "classifier.output_labels");
    if (i >= b->cfg.n_labels || zt_gguf_find(b->g, k, &v) != ZT_GGUF_OK) return ZT_NN_EARG;
    return zt_gguf_arr_str(b->g, &v, i, out);
}

/* ---- loading ---- */

static int32_t plan(const zt_gguf_t *g, const zt_bert_cfg_t *c, zt_bert_t *b, zt_nn_arena_t *a)
{
    char nm[64];
    int32_t r;
    uint32_t ne = c->n_embd, nf = c->n_ff;
    zt_bert_t db;
    if (!b) b = &db;
    uint64_t hcap = 1;
    while (hcap < 2ull * c->n_vocab) hcap <<= 1;
    zt_gguf_str_t *vocab = zt_nn_take(a, (uint64_t) c->n_vocab * sizeof *vocab);
    /* the tokens whose text parse_special turns into them: llama.cpp's
     * CONTROL (3) and UNKNOWN (2) types */
    uint32_t n_special = 0;
    zt_gguf_val_t tt;
    bool has_tt = zt_gguf_find(g, "tokenizer.ggml.token_type", &tt) == ZT_GGUF_OK &&
                  tt.type == ZT_GGUF_ARRAY && tt.count == c->n_vocab;
    for (uint32_t i = 0; has_tt && i < c->n_vocab; i++) {
        int64_t ty = 0;
        if (zt_gguf_arr_int(g, &tt, i, &ty) == ZT_GGUF_OK && (ty == 2 || ty == 3)) n_special++;
    }
    uint32_t *special = zt_nn_take(a, (uint64_t) n_special * 4 + 4);
    uint32_t *hash = zt_nn_take(a, hcap * 4);
    zt_bert_layer_t *L = zt_nn_take(a, (uint64_t) c->n_layer * sizeof *L), dl;
    if (zt_nn_filling(a) && (!vocab || !special || !hash || !L)) {
        set_err(a->err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    if ((r = zt_nn_load_q16(a, g, "position_embd.weight", (uint64_t) ne * c->n_ctx, &b->pos)))
        return r;
    if ((r = zt_nn_load_q16(a, g, "token_types.weight", (uint64_t) ne * c->n_types, &b->types)))
        return r;
    if ((r = zt_nn_load_gain(a, g, "token_embd_norm.weight", ne, &b->ln_embd.g))) return r;
    if ((r = zt_nn_load_q16(a, g, "token_embd_norm.bias", ne, &b->ln_embd.b))) return r;
    for (uint32_t l = 0; l < c->n_layer; l++) {
        zt_bert_layer_t *y = L ? &L[l] : &dl;
        static const char *const mn[6] = {"attn_q",      "attn_k", "attn_v",
                                          "attn_output", "ffn_up", "ffn_down"};
        zt_mat_t *mp[6] = {&y->wq, &y->wk, &y->wv, &y->wo, &y->wu, &y->wd};
        const zt_fx **bp[6] = {&y->bq, &y->bk, &y->bv, &y->bo, &y->bu, &y->bd};
        for (int i = 0; i < 6; i++) {
            uint32_t cols = i == 5 ? nf : ne, rows = i == 4 ? nf : ne;
            blk(nm, sizeof nm, l, mn[i], ".weight");
            if ((r = zt_nn_load_mat(a, g, nm, cols, rows, mp[i]))) return r;
            blk(nm, sizeof nm, l, mn[i], ".bias");
            if ((r = zt_nn_load_q16(a, g, nm, rows, bp[i]))) return r;
        }
        zt_nn_norm_t *np[2] = {&y->ln_att, &y->ln_out};
        static const char *const nn[2] = {"attn_output_norm", "layer_output_norm"};
        for (int i = 0; i < 2; i++) {
            blk(nm, sizeof nm, l, nn[i], ".weight");
            if ((r = zt_nn_load_gain(a, g, nm, ne, &np[i]->g))) return r;
            blk(nm, sizeof nm, l, nn[i], ".bias");
            if ((r = zt_nn_load_q16(a, g, nm, ne, &np[i]->b))) return r;
        }
    }
    if (c->n_labels) {
        if ((r = zt_nn_load_mat(a, g, "cls.weight", ne, ne, &b->cls_w))) return r;
        if ((r = zt_nn_load_q16(a, g, "cls.bias", ne, &b->cls_b))) return r;
        if ((r = zt_nn_load_mat(a, g, "cls.output.weight", ne, c->n_labels, &b->out_w))) return r;
        if ((r = zt_nn_load_q16(a, g, "cls.output.bias", c->n_labels, &b->out_b))) return r;
    }
    if (zt_nn_filling(a)) {
        b->vocab = vocab;
        b->hash = hash;
        b->hash_mask = (uint32_t) (hcap - 1);
        for (uint64_t i = 0; i < hcap; i++) hash[i] = 0;
        zt_gguf_val_t v;
        vfill_t f = {b, false};
        zt_gguf_find(g, "tokenizer.ggml.tokens", &v);
        if (zt_gguf_arr_strings(g, &v, vocab_put, &f) != ZT_GGUF_OK || f.dup) {
            set_err(a->err, ZT_NN_ECONFIG,
                    f.dup ? "a vocabulary entry is repeated" : "bad vocabulary",
                    "tokenizer.ggml.tokens");
            return ZT_NN_ECONFIG;
        }
        b->special = special;
        b->n_special = 0;
        for (uint32_t i = 0; has_tt && i < c->n_vocab; i++) {
            int64_t ty = 0;
            if (zt_gguf_arr_int(g, &tt, i, &ty) == ZT_GGUF_OK && (ty == 2 || ty == 3) &&
                vocab[i].len)
                special[b->n_special++] = i;
        }
    }
    b->layers = L;
    return ZT_GGUF_OK;
}

static uint32_t cols_max(const zt_bert_cfg_t *c)
{
    return c->n_ff > c->n_embd ? c->n_ff : c->n_embd;
}

uint64_t zt_bert_arena_bytes(const zt_gguf_t *g)
{
    zt_bert_cfg_t c;
    if (zt_bert_config(g, &c, 0)) return 0;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, cols_max(&c), 0);
    if (plan(g, &c, 0, &a)) return 0;
    return zt_nn_arena_used(&a);
}

int32_t zt_bert_load(zt_bert_t *b, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                     zt_model_err_t *err)
{
    if (!b || !g || !arena) {
        set_err(err, ZT_NN_EARG, "null argument", 0);
        return ZT_NN_EARG;
    }
    int32_t r = zt_bert_config(g, &b->cfg, err);
    if (r) return r;
    zt_nn_arena_t a;
    zt_nn_arena_begin(&a, 0, 0, cols_max(&b->cfg), err);
    if ((r = plan(g, &b->cfg, 0, &a))) return r;
    if (zt_nn_arena_used(&a) > arena_bytes) {
        set_err(err, ZT_NN_ESPACE, "arena too small", 0);
        return ZT_NN_ESPACE;
    }
    zt_nn_arena_begin(&a, arena, arena_bytes, cols_max(&b->cfg), err);
    if ((r = plan(g, &b->cfg, b, &a))) return r;
    zt_gguf_find_tensor(g, "token_embd.weight", &b->tok_embd);
    b->g = g;
    b->kern = 0;
    set_err(err, ZT_GGUF_OK, "ok", 0);
    return ZT_GGUF_OK;
}

/* ---- the tokenizer ---- */

static bool in_ranges(const uint32_t (*r)[2], uint32_t n, uint32_t c)
{
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (c < r[mid][0])
            hi = mid;
        else if (c > r[mid][1])
            lo = mid + 1;
        else
            return true;
    }
    return false;
}
#define IN(tab, c) in_ranges(tab, sizeof tab / sizeof tab[0], c)

static uint32_t ccc(uint32_t c)
{
    uint32_t lo = 0, hi = sizeof ZT_WP_CCC / sizeof ZT_WP_CCC[0];
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if (c < ZT_WP_CCC[mid][0])
            hi = mid;
        else if (c > ZT_WP_CCC[mid][1])
            lo = mid + 1;
        else
            return ZT_WP_CCC[mid][2];
    }
    return 0;
}

/* the row of a map whose first entry is c, or null */
static const uint32_t *map_row(const uint32_t *tab, uint32_t rows, uint32_t width, uint32_t c)
{
    uint32_t lo = 0, hi = rows;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        const uint32_t *r = tab + (uint64_t) mid * width;
        if (c < r[0])
            hi = mid;
        else if (c > r[0])
            lo = mid + 1;
        else
            return r + 1;
    }
    return 0;
}

/* Decode one character; a sequence that is not UTF-8 is one byte, read as
 * U+FFFD. Returns the length. */
static uint32_t utf8_dec(const uint8_t *s, uint64_t n, uint64_t i, uint32_t *cp)
{
    uint8_t b0 = s[i];
    uint32_t len, c, min;
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    if (b0 >= 0xC2 && b0 <= 0xDF)
        len = 2, c = b0 & 0x1F, min = 0x80;
    else if (b0 >= 0xE0 && b0 <= 0xEF)
        len = 3, c = b0 & 0x0F, min = 0x800;
    else if (b0 >= 0xF0 && b0 <= 0xF4)
        len = 4, c = b0 & 0x07, min = 0x10000;
    else
        goto bad;
    if (len > n - i) goto bad;
    for (uint32_t k = 1; k < len; k++) {
        if ((s[i + k] & 0xC0) != 0x80) goto bad;
        c = (c << 6) | (s[i + k] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) goto bad;
    *cp = c;
    return len;
bad:
    *cp = 0xFFFD;
    return 1;
}

static uint32_t utf8_enc(uint32_t c, uint8_t *out)
{
    if (c < 0x80) return out[0] = (uint8_t) c, 1;
    if (c < 0x800)
        return out[0] = (uint8_t) (0xC0 | (c >> 6)), out[1] = (uint8_t) (0x80 | (c & 0x3F)), 2;
    if (c < 0x10000) {
        out[0] = (uint8_t) (0xE0 | (c >> 12));
        out[1] = (uint8_t) (0x80 | ((c >> 6) & 0x3F));
        out[2] = (uint8_t) (0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (uint8_t) (0xF0 | (c >> 18));
    out[1] = (uint8_t) (0x80 | ((c >> 12) & 0x3F));
    out[2] = (uint8_t) (0x80 | ((c >> 6) & 0x3F));
    out[3] = (uint8_t) (0x80 | (c & 0x3F));
    return 4;
}

/* append c, moving it left past code points of a higher nonzero class
 * (canonical ordering is a stable sort of each run of nonzero classes) */
static void put_ordered(uint32_t *buf, uint64_t *n, uint32_t c)
{
    uint64_t i = (*n)++;
    uint32_t cc = ccc(c);
    while (cc && i > 0 && ccc(buf[i - 1]) > cc) {
        buf[i] = buf[i - 1];
        i--;
    }
    buf[i] = c;
}

typedef struct {
    uint32_t *c1, *c2; /* code points after stage 1 and stage 2 */
    int32_t *ids;
    uint64_t n_ids;
    uint8_t *word; /* one piece in UTF-8 */
} tokw_t;

/* the per-byte expansion: stage 1 gives at most 4 code points per input
 * byte (3 around an ideograph, 4 from a decomposition), stage 2 at most 3
 * per stage 1 code point, and every token takes at least one code point */
#define S1 4u
#define S2 12u

static uint64_t tok_layout(uint64_t len, uint8_t *base, tokw_t *w)
{
    uint64_t o = 0;
#define TTAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    TTAKE(w->c1, (len * S1 + 1) * 4);
    TTAKE(w->c2, (len * S2 + 1) * 4);
    TTAKE(w->ids, (len * S2 + 1) * 4);
    TTAKE(w->word, 4ull * ZT_BERT_MAX_CHARS + 8);
#undef TTAKE
    return o + 64;
}

uint64_t zt_bert_tok_work_bytes(uint64_t len_a, uint64_t len_b)
{
    tokw_t w;
    return tok_layout(len_a + len_b, 0, &w);
}

/* WordPiece on one word of n code points */
static void wordpiece(const zt_bert_t *b, const uint32_t *cp, uint64_t n, tokw_t *w)
{
    static const uint8_t ws[3] = {0xE2, 0x96, 0x81}; /* U+2581 */
    if (n > b->cfg.max_chars) {
        w->ids[w->n_ids++] = b->cfg.unk_id;
        return;
    }
    uint64_t first = w->n_ids, start = 0;
    while (start < n) {
        int32_t id = -1;
        uint64_t end = n;
        for (; end > start; end--) {
            uint32_t len = 0;
            for (uint64_t i = start; i < end; i++) len += utf8_enc(cp[i], w->word + len);
            id = lookup2(b, ws, start ? 0 : 3, w->word, len);
            if (id >= 0) break;
        }
        if (id < 0) {
            w->n_ids = first;
            w->ids[w->n_ids++] = b->cfg.unk_id;
            return;
        }
        w->ids[w->n_ids++] = id;
        start = end;
    }
}

/* normalise, split and WordPiece one stretch of text with no control token */
static void segment(const zt_bert_t *b, const uint8_t *s, uint64_t len, tokw_t *w)
{
    const zt_bert_cfg_t *c = &b->cfg;
    uint64_t n1 = 0, n2 = 0;
    for (uint64_t i = 0; i < len;) {
        uint32_t cp;
        i += utf8_dec(s, len, i, &cp);
        if (c->clean_text) {
            if (IN(ZT_WP_CTRL, cp)) continue;
            if (IN(ZT_WP_CLEANWS, cp)) cp = ' ';
        }
        bool cjk = c->chinese_chars && IN(ZT_WP_CJK, cp);
        if (cjk) w->c1[n1++] = ' ';
        if (!c->strip_accents)
            w->c1[n1++] = cp;
        else if (cp >= 0xAC00 && cp <= 0xD7A3) {
            uint32_t si = cp - 0xAC00; /* Hangul: leading, vowel, trailing jamo */
            w->c1[n1++] = 0x1100 + si / 588;
            w->c1[n1++] = 0x1161 + (si % 588) / 28;
            if (si % 28) w->c1[n1++] = 0x11A7 + si % 28;
        } else {
            const uint32_t *d = map_row(&ZT_WP_NFD[0][0], sizeof ZT_WP_NFD / sizeof ZT_WP_NFD[0],
                                        ZT_WP_NFD_K + 1, cp);
            if (!d)
                put_ordered(w->c1, &n1, cp);
            else
                for (uint32_t k = 0; k < ZT_WP_NFD_K && d[k]; k++) put_ordered(w->c1, &n1, d[k]);
        }
        if (cjk) w->c1[n1++] = ' ';
    }
    for (uint64_t i = 0; i < n1; i++) {
        uint32_t cp = w->c1[i];
        if (c->strip_accents && IN(ZT_WP_MN, cp)) continue;
        const uint32_t *l =
            c->lowercase ? map_row(&ZT_WP_LOWER[0][0], sizeof ZT_WP_LOWER / sizeof ZT_WP_LOWER[0],
                                   ZT_WP_LOWER_K + 1, cp)
                         : 0;
        if (!l)
            w->c2[n2++] = cp;
        else
            for (uint32_t k = 0; k < ZT_WP_LOWER_K && l[k]; k++) w->c2[n2++] = l[k];
    }
    for (uint64_t i = 0; i < n2;) {
        uint32_t cp = w->c2[i];
        if (IN(ZT_WP_PREWS, cp)) {
            i++;
            continue;
        }
        if (IN(ZT_WP_PUNCT, cp)) {
            wordpiece(b, w->c2 + i, 1, w);
            i++;
            continue;
        }
        uint64_t j = i + 1;
        while (j < n2 && !IN(ZT_WP_PREWS, w->c2[j]) && !IN(ZT_WP_PUNCT, w->c2[j])) j++;
        wordpiece(b, w->c2 + i, j - i, w);
        i = j;
    }
}

/* the longest special token spelled at s[i], or -1 */
static int32_t special_at(const zt_bert_t *b, const uint8_t *s, uint64_t len, uint64_t i,
                          uint64_t *n)
{
    int32_t best = -1;
    *n = 0;
    for (uint32_t k = 0; k < b->n_special; k++) {
        zt_gguf_str_t v = b->vocab[b->special[k]];
        if (v.len > len - i || v.len <= *n || !str_is(v, s + i, v.len)) continue;
        best = (int32_t) b->special[k];
        *n = v.len;
    }
    return best;
}

static void text(const zt_bert_t *b, const uint8_t *s, uint64_t len, bool parse_special, tokw_t *w)
{
    uint64_t from = 0;
    if (parse_special)
        for (uint64_t i = 0; i < len;) {
            uint64_t n;
            int32_t id = special_at(b, s, len, i, &n);
            if (id < 0) {
                i++;
                continue;
            }
            segment(b, s + from, i - from, w);
            w->ids[w->n_ids++] = id;
            i += n;
            from = i;
        }
    segment(b, s + from, len - from, w);
}

int32_t zt_bert_tokenize(const zt_bert_t *b, const uint8_t *a, uint64_t len_a, const uint8_t *bt,
                         uint64_t len_b, bool parse_special, uint32_t max_tokens, int32_t *ids,
                         uint8_t *types, uint32_t *n, void *work, uint64_t work_bytes)
{
    if (!b || (!a && len_a) || (!bt && len_b) || !ids || !types || !n || !work) return ZT_NN_EARG;
    bool pair = bt != 0;
    uint32_t specials = pair ? 3 : 2;
    if (max_tokens < specials || max_tokens > b->cfg.n_ctx || (pair && b->cfg.n_types < 2))
        return ZT_NN_EARG;
    if (work_bytes < zt_bert_tok_work_bytes(len_a, len_b)) return ZT_NN_ESPACE;
    tokw_t w;
    tok_layout(len_a + len_b, (uint8_t *) work + ((64 - ((uintptr_t) work & 63)) & 63), &w);
    w.n_ids = 0;
    text(b, a, len_a, parse_special, &w);
    uint64_t na = w.n_ids, nb = 0;
    if (pair) {
        text(b, bt, len_b, parse_special, &w);
        nb = w.n_ids - na;
    }
    /* longest_first, as the library does it: the shorter text (A on a tie)
     * keeps up to half the room, the longer takes the rest */
    uint64_t room = max_tokens - specials, ka = na, kb = nb;
    if (na + nb > room) {
        if (na > nb) {
            kb = nb < room / 2 ? nb : room / 2;
            ka = room - kb;
        } else {
            ka = na < room / 2 ? na : room / 2;
            kb = room - ka;
        }
    }
    uint32_t k = 0;
    ids[k] = b->cfg.cls_id, types[k++] = 0;
    for (uint64_t i = 0; i < ka; i++) ids[k] = w.ids[i], types[k++] = 0;
    ids[k] = b->cfg.sep_id, types[k++] = 0;
    if (pair) {
        for (uint64_t i = 0; i < kb; i++) ids[k] = w.ids[na + i], types[k++] = 1;
        ids[k] = b->cfg.sep_id, types[k++] = 1;
    }
    *n = k;
    return ZT_GGUF_OK;
}

/* ---- the encoder ---- */

typedef struct {
    zt_fx *x, *xn, *q, *k, *v, *att, *h;
    void *nn_mem;
    uint64_t nn_bytes;
} bwork_t;

static uint32_t chunk(uint32_t n)
{
    return n < 256 ? n : 256;
}

static uint64_t work_layout(const zt_bert_cfg_t *c, uint32_t n, uint8_t *base, bwork_t *w)
{
    uint64_t o = 0, ne = c->n_embd;
#define BTAKE(ptr, bytes)                                                                          \
    do {                                                                                           \
        o = (o + 63) & ~63ull;                                                                     \
        ptr = base ? (void *) (base + o) : 0;                                                      \
        o += (bytes);                                                                              \
    } while (0)
    BTAKE(w->x, n * ne * 4);
    BTAKE(w->xn, n * ne * 4);
    BTAKE(w->q, n * ne * 4);
    BTAKE(w->k, n * ne * 4);
    BTAKE(w->v, n * ne * 4);
    BTAKE(w->att, n * ne * 4);
    BTAKE(w->h, (uint64_t) n * c->n_ff * 4);
    w->nn_bytes = zt_nn_bytes(chunk(n), cols_max(c), n, c->n_embd / c->n_head, c->n_head);
    BTAKE(w->nn_mem, w->nn_bytes);
#undef BTAKE
    return o + 64;
}

uint64_t zt_bert_work_bytes(const zt_bert_t *b, uint32_t n)
{
    bwork_t w;
    if (!b || !n || n > b->cfg.n_ctx) return 0;
    return work_layout(&b->cfg, n, 0, &w);
}

/* the final hidden states, in w->x; nn ready for the heads */
static int32_t encode(const zt_bert_t *b, const int32_t *ids, const uint8_t *types, uint32_t n,
                      void *work, uint64_t work_bytes, bwork_t *w, zt_nn_t *nn)
{
    const zt_bert_cfg_t *c = &b->cfg;
    if (!ids || !types || !work || !n || n > c->n_ctx) return ZT_NN_EARG;
    if (work_bytes < zt_bert_work_bytes(b, n)) return ZT_NN_ESPACE;
    work_layout(c, n, (uint8_t *) work + ((64 - ((uintptr_t) work & 63)) & 63), w);
    uint32_t ne = c->n_embd, nf = c->n_ff, hd = ne / c->n_head;
    int32_t r = zt_nn_init(nn, chunk(n), cols_max(c), n, hd, c->n_head, w->nn_mem, w->nn_bytes);
    if (r) return r;
    nn->kern = b->kern;
    for (uint32_t i = 0; i < n; i++) {
        if (ids[i] < 0 || (uint32_t) ids[i] >= c->n_vocab || types[i] >= c->n_types)
            return ZT_NN_EARG;
        r = zt_gguf_dequant(&b->tok_embd, (uint64_t) ids[i] * ne, ne, w->x + (uint64_t) i * ne);
        if (r) return r;
        zt_nn_add(nn, w->x + (uint64_t) i * ne, ne, b->types + (uint64_t) types[i] * ne, ne, ne, 1);
    }
    zt_nn_add(nn, w->x, ne, b->pos, ne, ne, n);
    zt_nn_layernorm(nn, w->x, ne, ne, n, &b->ln_embd, c->eps_f32, w->x, ne);
    for (uint32_t l = 0; l < c->n_layer; l++) {
        const zt_bert_layer_t *L = &b->layers[l];
        zt_nn_linear(nn, &L->wq, L->bq, w->x, ne, ne, n, w->q, ne);
        zt_nn_linear(nn, &L->wk, L->bk, w->x, ne, ne, n, w->k, ne);
        zt_nn_linear(nn, &L->wv, L->bv, w->x, ne, ne, n, w->v, ne);
        zt_nn_attention(nn, w->q, ne, w->k, ne, w->v, ne, n, n, c->n_head, hd, false, w->att, ne);
        zt_nn_linear(nn, &L->wo, L->bo, w->att, ne, ne, n, w->xn, ne);
        zt_nn_add(nn, w->xn, ne, w->x, ne, ne, n);
        zt_nn_layernorm(nn, w->xn, ne, ne, n, &L->ln_att, c->eps_f32, w->x, ne);
        zt_nn_linear(nn, &L->wu, L->bu, w->x, ne, ne, n, w->h, nf);
        zt_nn_act(nn, w->h, nf, nf, n, ZT_NN_GELU_ERF);
        zt_nn_linear(nn, &L->wd, L->bd, w->h, nf, nf, n, w->xn, ne);
        zt_nn_add(nn, w->xn, ne, w->x, ne, ne, n);
        zt_nn_layernorm(nn, w->xn, ne, ne, n, &L->ln_out, c->eps_f32, w->x, ne);
    }
    return ZT_GGUF_OK;
}

int32_t zt_bert_classify(const zt_bert_t *b, const int32_t *ids, const uint8_t *types, uint32_t n,
                         zt_fx *logits, void *work, uint64_t work_bytes, uint32_t *n_sat)
{
    if (!b || !logits) return ZT_NN_EARG;
    if (!b->cfg.n_labels) return ZT_NN_ECONFIG;
    bwork_t w;
    zt_nn_t nn;
    int32_t r = encode(b, ids, types, n, work, work_bytes, &w, &nn);
    if (r) return r;
    uint32_t ne = b->cfg.n_embd;
    zt_nn_linear(&nn, &b->cls_w, b->cls_b, w.x, ne, ne, 1, w.q, ne);
    if (b->cfg.arch == ZT_BERT_ELECTRA)
        zt_nn_act(&nn, w.q, ne, ne, 1, ZT_NN_GELU_ERF);
    else
        for (uint32_t i = 0; i < ne; i++) { /* tanh x = 2 sigmoid(2x) - 1 */
            int64_t x2 = 2 * (int64_t) w.q[i];
            if (x2 > INT32_MAX) x2 = INT32_MAX;
            if (x2 < INT32_MIN) x2 = INT32_MIN;
            w.q[i] = 2 * zt_nn_act1((zt_fx) x2, ZT_NN_SIGMOID) - ZT_ONE;
        }
    zt_nn_linear(&nn, &b->out_w, b->out_b, w.q, ne, ne, 1, logits, b->cfg.n_labels);
    if (n_sat) *n_sat = nn.n_sat;
    return ZT_GGUF_OK;
}

/* round(a / d) for d > 0 */
static int64_t div_round(int64_t a, uint64_t d)
{
    uint64_t m = a < 0 ? (uint64_t) -a : (uint64_t) a;
    uint64_t q = zt_udiv64(m + d / 2, d, 0);
    return a < 0 ? -(int64_t) q : (int64_t) q;
}

int32_t zt_bert_embed(const zt_bert_t *b, const int32_t *ids, const uint8_t *types, uint32_t n,
                      zt_fx *out, void *work, uint64_t work_bytes, uint32_t *n_sat)
{
    if (!b || !out) return ZT_NN_EARG;
    bwork_t w;
    zt_nn_t nn;
    int32_t r = encode(b, ids, types, n, work, work_bytes, &w, &nn);
    if (r) return r;
    uint32_t ne = b->cfg.n_embd;
    uint32_t m = 0;
    for (uint32_t i = 0; i < ne; i++) {
        int64_t s = 0;
        for (uint32_t t = 0; t < n; t++) s += w.x[(uint64_t) t * ne + i];
        out[i] = (zt_fx) div_round(s, n);
        uint32_t a = out[i] < 0 ? (uint32_t) - (int64_t) out[i] : (uint32_t) out[i];
        if (a > m) m = a;
    }
    if (b->cfg.normalize && m) {
        /* |v| = sqrt(sum v^2): with each value shifted right by sh so that a
         * square stays below 2^40 and the sum (at most 8192 of them) below
         * 2^53; the shift is 0 for |v| < 16, so the sum is exact there */
        uint32_t sh = 0;
        while ((m >> sh) >= (1u << 20)) sh++;
        uint64_t ss = 0;
        for (uint32_t i = 0; i < ne; i++) {
            int64_t v = out[i] >> sh;
            ss += (uint64_t) (v * v);
        }
        uint64_t len = (uint64_t) zt_isqrt64(ss) << sh; /* Q16 */
        if (len)
            for (uint32_t i = 0; i < ne; i++)
                out[i] = (zt_fx) div_round((int64_t) out[i] * ZT_ONE, len);
    }
    if (n_sat) *n_sat = nn.n_sat;
    return ZT_GGUF_OK;
}
