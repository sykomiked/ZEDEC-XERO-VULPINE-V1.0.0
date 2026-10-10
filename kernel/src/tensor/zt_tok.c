/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_tok.c — byte-level BPE tokenizer (T19). See zt_tok.h. */
#include "zt_tok.h"
#include "zt_tok_unicode.h"

#define NONE 0xFFFFFFFFu
/* Vocabulary and merge counts above this are refused: the hash tables are
 * sized 2 * count rounded up to a power of two, which must fit 32 bits.
 * Real vocabularies are below 2^18. */
#define ZT_TOK_MAX_COUNT (1u << 28)

/* ---- small helpers (freestanding: no libc) ---- */

static bool bytes_eq(const uint8_t *a, const uint8_t *b, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static uint32_t fnv_step(uint32_t h, const uint8_t *s, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) h = (h ^ s[i]) * 16777619u;
    return h;
}

#define FNV0 2166136261u

static uint32_t pair_hash(uint32_t l, uint32_t r)
{
    uint32_t h = l * 0x9E3779B1u ^ (r + 0x7F4A7C15u) * 0x85EBCA77u;
    return h ^ (h >> 15);
}

static uint32_t pow2_at_least(uint64_t n)
{
    uint32_t c = 16;
    while (c < n && c < (1u << 31)) c <<= 1; /* counts are capped well below 2^30 */
    return c;
}

/* ---- UTF-8 and character classes ---- */

enum { C_OTHER = 0, C_LETTER = 1, C_NUMBER = 2, C_SPACE = 3 };

/* Decode one character at s[i]; returns its length. An invalid or truncated
 * sequence is one byte, reported as code point 0x110000 + byte (class other). */
static uint32_t utf8_dec(const uint8_t *s, uint64_t n, uint64_t i, uint32_t *cp)
{
    uint8_t b = s[i];
    uint32_t len, c, min;
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if (b >= 0xC2 && b <= 0xDF)
        len = 2, c = b & 0x1F, min = 0x80;
    else if (b >= 0xE0 && b <= 0xEF)
        len = 3, c = b & 0x0F, min = 0x800;
    else if (b >= 0xF0 && b <= 0xF4)
        len = 4, c = b & 0x07, min = 0x10000;
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
    *cp = 0x110000u + b;
    return 1;
}

static uint32_t utf8_enc(uint32_t c, uint8_t out[4])
{
    if (c < 0x80) return out[0] = (uint8_t) c, 1;
    if (c < 0x800)
        return out[0] = (uint8_t) (0xC0 | (c >> 6)), out[1] = (uint8_t) (0x80 | (c & 0x3F)), 2;
    out[0] = (uint8_t) (0xE0 | (c >> 12));
    out[1] = (uint8_t) (0x80 | ((c >> 6) & 0x3F));
    out[2] = (uint8_t) (0x80 | (c & 0x3F));
    return 3; /* the byte alphabet never needs four bytes */
}

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

#define NRANGES(a) ((uint32_t) (sizeof(a) / sizeof((a)[0])))

static uint32_t char_class(uint32_t c)
{
    if (c < 0x80) {
        if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') return C_LETTER;
        if (c >= '0' && c <= '9') return C_NUMBER;
        if (c == ' ' || (c >= 0x09 && c <= 0x0D)) return C_SPACE;
        return C_OTHER;
    }
    if (c > 0x10FFFF) return C_OTHER;
    if (in_ranges(ZT_UNI_L, NRANGES(ZT_UNI_L), c)) return C_LETTER;
    if (in_ranges(ZT_UNI_N, NRANGES(ZT_UNI_N), c)) return C_NUMBER;
    if (in_ranges(ZT_UNI_S, NRANGES(ZT_UNI_S), c)) return C_SPACE;
    return C_OTHER;
}

/* ---- the pre-split: one regex match starting at i, returns its end ----
 * (?i:'s|'t|'re|'ve|'m|'ll|'d)|[^\r\n\p{L}\p{N}]?\p{L}+|\p{N}{1,D}
 *   | ?[^\s\p{L}\p{N}]+[\r\n]*|\s*[\r\n]+|\s+(?!\S)|\s+
 * with D = 1 (qwen2) or 3 (llama-bpe). Every character starts some
 * alternative, so matches tile the text. */

static uint64_t run_of(const uint8_t *s, uint64_t n, uint64_t i, uint32_t cls)
{
    while (i < n) {
        uint32_t c, l = utf8_dec(s, n, i, &c);
        if (char_class(c) != cls) break;
        i += l;
    }
    return i;
}

static bool is_nl(uint32_t c)
{
    return c == '\r' || c == '\n';
}

static uint64_t piece_end(const uint8_t *s, uint64_t n, uint64_t i, uint32_t max_digits)
{
    uint32_t c0, c1 = 0;
    uint32_t l0 = utf8_dec(s, n, i, &c0);
    uint64_t i1 = i + l0;
    uint32_t k0 = char_class(c0), k1 = 0xFF;
    if (i1 < n) {
        utf8_dec(s, n, i1, &c1);
        k1 = char_class(c1);
    }

    /* contractions, case-insensitive ('ſ' U+017F folds to 's') */
    if (c0 == '\'' && i1 < n) {
        uint8_t a = s[i1] | 0x20;
        if (s[i1] < 0x80 && (a == 's' || a == 't' || a == 'm' || a == 'd')) return i1 + 1;
        if (s[i1] == 0xC5 && i1 + 1 < n && s[i1 + 1] == 0xBF) return i1 + 2;
        if (s[i1] < 0x80 && i1 + 1 < n && s[i1 + 1] < 0x80) {
            uint8_t b = s[i1 + 1] | 0x20;
            if ((a == 'r' && b == 'e') || (a == 'v' && b == 'e') || (a == 'l' && b == 'l'))
                return i1 + 2;
        }
    }
    /* [^\r\n\p{L}\p{N}]?\p{L}+ */
    if (k0 == C_LETTER) return run_of(s, n, i, C_LETTER);
    if (k0 != C_NUMBER && !is_nl(c0) && k1 == C_LETTER) return run_of(s, n, i1, C_LETTER);
    /* \p{N}{1,D} */
    if (k0 == C_NUMBER) {
        uint64_t j = i;
        for (uint32_t d = 0; d < max_digits && j < n; d++) {
            uint32_t c, l = utf8_dec(s, n, j, &c);
            if (char_class(c) != C_NUMBER) break;
            j += l;
        }
        return j;
    }
    /*  ?[^\s\p{L}\p{N}]+[\r\n]* */
    {
        uint64_t j = NONE;
        if (k0 == C_OTHER)
            j = i;
        else if (c0 == ' ' && k1 == C_OTHER)
            j = i1;
        if (j != NONE) {
            j = run_of(s, n, j, C_OTHER);
            while (j < n && (s[j] == '\r' || s[j] == '\n')) j++;
            return j;
        }
    }
    /* k0 is white space: \s*[\r\n]+ | \s+(?!\S) | \s+ */
    uint64_t j = i, after_nl = NONE, last = i;
    uint32_t count = 0;
    while (j < n) {
        uint32_t c, l = utf8_dec(s, n, j, &c);
        if (char_class(c) != C_SPACE) break;
        last = j;
        j += l;
        count++;
        if (is_nl(c)) after_nl = j;
    }
    if (after_nl != NONE) return after_nl;
    if (j == n || count < 2) return j;
    return last;
}

/* ---- tables ---- */

static int32_t find_tok(const zt_tok_t *t, const uint8_t *s, uint64_t len)
{
    uint32_t h = fnv_step(FNV0, s, len);
    for (uint32_t k = h & t->vmask;; k = (k + 1) & t->vmask) {
        uint32_t e = t->vhash[k];
        if (!e) return -1;
        zt_gguf_str_t v = t->tok[e - 1];
        if (v.len == len && bytes_eq(v.p, s, len)) return (int32_t) (e - 1);
    }
}

int32_t zt_tok_find(const zt_tok_t *t, const uint8_t *s, uint64_t len)
{
    return find_tok(t, s, len);
}

/* the concatenation a ++ b, without building it */
static int32_t find_tok2(const zt_tok_t *t, const uint8_t *a, uint64_t la, const uint8_t *b,
                         uint64_t lb)
{
    uint32_t h = fnv_step(fnv_step(FNV0, a, la), b, lb);
    for (uint32_t k = h & t->vmask;; k = (k + 1) & t->vmask) {
        uint32_t e = t->vhash[k];
        if (!e) return -1;
        zt_gguf_str_t v = t->tok[e - 1];
        if (v.len == la + lb && bytes_eq(v.p, a, la) && bytes_eq(v.p + la, b, lb))
            return (int32_t) (e - 1);
    }
}

static uint32_t find_merge(const zt_tok_t *t, uint32_t l, uint32_t r)
{
    for (uint32_t k = pair_hash(l, r) & t->mmask;; k = (k + 1) & t->mmask) {
        uint32_t e = t->mhash[k];
        if (!e) return NONE;
        const uint32_t *m = t->merge + 3 * (e - 1);
        if (m[0] == l && m[1] == r) return e - 1;
    }
}

/* GPT-2 byte alphabet: printable Latin-1 bytes stand for themselves, the
 * other 68 bytes take code points 256.. in byte order. */
static bool byte_is_self(uint32_t b)
{
    return (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || b >= 174;
}

typedef struct {
    zt_gguf_str_t *tok;
    uint32_t n;
} tok_fill_t;

static bool fill_tok(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    tok_fill_t *f = ctx;
    if (i < f->n) f->tok[i] = s;
    return true;
}

typedef struct {
    zt_tok_t *t;
    uint32_t *merge, *mhash;
    uint32_t n;
    bool bad;
} merge_fill_t;

static bool fill_merge(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    merge_fill_t *f = ctx;
    (void) i;
    uint64_t sp = 0;
    while (sp < s.len && s.p[sp] != ' ') sp++;
    if (sp == 0 || sp + 1 >= s.len) {
        f->bad = true;
        return false;
    }
    int32_t l = find_tok(f->t, s.p, sp), r = find_tok(f->t, s.p + sp + 1, s.len - sp - 1);
    int32_t res = find_tok2(f->t, s.p, sp, s.p + sp + 1, s.len - sp - 1);
    if (l < 0 || r < 0 || res < 0) {
        f->bad = true;
        return false;
    }
    uint32_t k = pair_hash((uint32_t) l, (uint32_t) r) & f->t->mmask;
    for (;; k = (k + 1) & f->t->mmask) {
        uint32_t e = f->mhash[k];
        if (!e) break;
        const uint32_t *m = f->merge + 3 * (e - 1);
        if (m[0] == (uint32_t) l && m[1] == (uint32_t) r)
            return true; /* duplicate: first rank wins */
    }
    uint32_t *m = f->merge + 3 * f->n;
    m[0] = (uint32_t) l, m[1] = (uint32_t) r, m[2] = (uint32_t) res;
    f->mhash[k] = ++f->n;
    return true;
}

static uint64_t align8(uint64_t x)
{
    return (x + 7) & ~(uint64_t) 7;
}

static bool counts(const zt_gguf_t *g, zt_gguf_val_t *toks, zt_gguf_val_t *merges)
{
    if (zt_gguf_find(g, "tokenizer.ggml.tokens", toks) != ZT_GGUF_OK ||
        toks->type != ZT_GGUF_ARRAY || toks->elem_type != ZT_GGUF_STRING || !toks->count ||
        toks->count > ZT_TOK_MAX_COUNT)
        return false;
    if (zt_gguf_find(g, "tokenizer.ggml.merges", merges) != ZT_GGUF_OK ||
        merges->type != ZT_GGUF_ARRAY || merges->elem_type != ZT_GGUF_STRING ||
        merges->count > ZT_TOK_MAX_COUNT)
        return false;
    return true;
}

static uint64_t layout(uint64_t nv, uint64_t nm, uint64_t off[6])
{
    uint64_t o = 0;
    off[0] = o, o = align8(o + nv * sizeof(zt_gguf_str_t));   /* tok */
    off[1] = o, o = align8(o + nv);                           /* ttype */
    off[2] = o, o = align8(o + 4ull * pow2_at_least(2 * nv)); /* vhash */
    off[3] = o, o = align8(o + 12ull * nm);                   /* merge */
    off[4] = o, o = align8(o + 4ull * pow2_at_least(2 * nm)); /* mhash */
    off[5] = o, o = align8(o + 4ull * nv);                    /* special */
    return o;
}

uint64_t zt_tok_arena_bytes(const zt_gguf_t *g)
{
    zt_gguf_val_t tv, mv;
    uint64_t off[6];
    if (!counts(g, &tv, &mv)) return 0;
    return layout(tv.count, mv.count, off) + 8;
}

void zt_tok_set_pre(zt_tok_t *t, uint32_t pre)
{
    t->pre = pre;
}

int32_t zt_tok_load(zt_tok_t *t, const zt_gguf_t *g, void *arena, uint64_t arena_bytes)
{
    zt_gguf_val_t tv, mv, v;
    if (zt_gguf_find(g, "tokenizer.ggml.model", &v) != ZT_GGUF_OK || v.type != ZT_GGUF_STRING)
        return ZT_GGUF_ENOTFOUND;
    if (!zt_gguf_str_eq(v.str, "gpt2")) return ZT_GGUF_EUNSUPPORTED;
    if (!counts(g, &tv, &mv)) return ZT_GGUF_ETYPE;

    t->pre = 0;
    if (zt_gguf_find(g, "tokenizer.ggml.pre", &v) == ZT_GGUF_OK && v.type == ZT_GGUF_STRING) {
        if (zt_gguf_str_eq(v.str, "qwen2") || zt_gguf_str_eq(v.str, "deepseek-r1-qwen"))
            t->pre = ZT_TOK_PRE_QWEN2;
        else if (zt_gguf_str_eq(v.str, "llama-bpe") || zt_gguf_str_eq(v.str, "llama3"))
            t->pre = ZT_TOK_PRE_LLAMA3;
    }
    if (!t->pre) return ZT_GGUF_EUNSUPPORTED;

    uint64_t off[6];
    uint64_t need = layout(tv.count, mv.count, off);
    uint8_t *base = (uint8_t *) (((uintptr_t) arena + 7) & ~(uintptr_t) 7);
    if (!arena || need + (uint64_t) (base - (uint8_t *) arena) > arena_bytes) return ZT_TOK_ESPACE;

    uint32_t nv = (uint32_t) tv.count;
    zt_gguf_str_t *tok = (zt_gguf_str_t *) (base + off[0]);
    uint8_t *ttype = base + off[1];
    uint32_t *vhash = (uint32_t *) (base + off[2]);
    uint32_t *merge = (uint32_t *) (base + off[3]);
    uint32_t *mhash = (uint32_t *) (base + off[4]);
    uint32_t *special = (uint32_t *) (base + off[5]);
    t->n_vocab = nv;
    t->tok = tok, t->ttype = ttype, t->vhash = vhash, t->merge = merge, t->mhash = mhash,
    t->special = special;
    t->vmask = pow2_at_least(2ull * nv) - 1;
    t->mmask = pow2_at_least(2 * mv.count) - 1;
    for (uint32_t k = 0; k <= t->vmask; k++) vhash[k] = 0;
    for (uint32_t k = 0; k <= t->mmask; k++) mhash[k] = 0;

    tok_fill_t tf = {tok, nv};
    int32_t r = zt_gguf_arr_strings(g, &tv, fill_tok, &tf);
    if (r != ZT_GGUF_OK) return r;
    for (uint32_t i = 0; i < nv; i++) {
        uint32_t k = fnv_step(FNV0, tok[i].p, tok[i].len) & t->vmask;
        bool dup = false;
        for (; vhash[k]; k = (k + 1) & t->vmask) {
            zt_gguf_str_t o = tok[vhash[k] - 1];
            if (o.len == tok[i].len && bytes_eq(o.p, tok[i].p, o.len)) dup = true;
        }
        if (!dup) vhash[k] = i + 1; /* a repeated string resolves to its first id */
    }

    zt_gguf_val_t ty;
    bool have_types = zt_gguf_find(g, "tokenizer.ggml.token_type", &ty) == ZT_GGUF_OK &&
                      ty.type == ZT_GGUF_ARRAY && ty.count == nv;
    for (uint32_t i = 0; i < nv; i++) {
        int64_t x = ZT_TOK_NORMAL;
        if (have_types && zt_gguf_arr_int(g, &ty, i, &x) != ZT_GGUF_OK) return ZT_GGUF_ETYPE;
        ttype[i] = (uint8_t) x;
    }

    merge_fill_t mf = {t, merge, mhash, 0, false};
    r = zt_gguf_arr_strings(g, &mv, fill_merge, &mf);
    if (r != ZT_GGUF_OK) return r;
    if (mf.bad) return ZT_GGUF_ETYPE;
    t->n_merges = mf.n;

    /* special tokens, longest first (insertion sort; there are few) */
    for (uint32_t k = 0; k < 32; k++) t->special_first[k] = 0;
    uint32_t ns = 0;
    for (uint32_t i = 0; i < nv; i++) {
        if ((ttype[i] != ZT_TOK_CONTROL && ttype[i] != ZT_TOK_USER) || !tok[i].len) continue;
        uint32_t j = ns++;
        while (j && tok[special[j - 1]].len < tok[i].len) special[j] = special[j - 1], j--;
        special[j] = i;
        t->special_first[tok[i].p[0] >> 3] |= (uint8_t) (1u << (tok[i].p[0] & 7));
    }
    t->n_special = ns;

    uint32_t extra = 0;
    for (uint32_t b = 0; b < 256; b++) {
        uint32_t cp = byte_is_self(b) ? b : 256 + extra;
        if (!byte_is_self(b)) t->unbyte[extra++] = (uint8_t) b;
        t->byte_cp[b] = (uint16_t) cp;
        uint8_t u[4];
        t->byte_tok[b] = find_tok(t, u, utf8_enc(cp, u));
        if (t->byte_tok[b] < 0) return ZT_GGUF_ETYPE; /* not a byte-level vocabulary */
    }

    t->bos = (int32_t) zt_gguf_get_int(g, "tokenizer.ggml.bos_token_id", -1);
    t->eos = (int32_t) zt_gguf_get_int(g, "tokenizer.ggml.eos_token_id", -1);
    if (t->bos >= (int32_t) nv) t->bos = -1;
    if (t->eos >= (int32_t) nv) t->eos = -1;
    t->add_bos = zt_gguf_get_int(g, "tokenizer.ggml.add_bos_token", 0) != 0;
    return ZT_GGUF_OK;
}

/* ---- BPE over one piece ---- */

typedef struct {
    uint32_t rank, pos;
} hent_t;

static bool hless(hent_t a, hent_t b)
{
    return a.rank < b.rank || (a.rank == b.rank && a.pos < b.pos);
}

static void hpush(hent_t *h, uint32_t *n, hent_t e)
{
    uint32_t i = (*n)++;
    while (i) {
        uint32_t p = (i - 1) >> 1;
        if (!hless(e, h[p])) break;
        h[i] = h[p];
        i = p;
    }
    h[i] = e;
}

static hent_t hpop(hent_t *h, uint32_t *n)
{
    hent_t top = h[0], e = h[--(*n)];
    uint32_t i = 0;
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= *n) break;
        if (c + 1 < *n && hless(h[c + 1], h[c])) c++;
        if (!hless(h[c], e)) break;
        h[i] = h[c];
        i = c;
    }
    if (*n) h[i] = e;
    return top;
}

typedef struct {
    int32_t *ids;
    uint64_t cap, n;
    bool full;
} emit_t;

static void emit(emit_t *o, int32_t id)
{
    if (o->n < o->cap)
        o->ids[o->n++] = id;
    else
        o->full = true;
}

/* work: id, next, prev (uint32 each) per byte, then the heap (3 entries per byte) */
uint64_t zt_tok_work_bytes(uint64_t len)
{
    return 36 * len + 64;
}

static void bpe_piece(const zt_tok_t *t, const uint8_t *s, uint32_t n, uint8_t *work, emit_t *o)
{
    if (t->pre == ZT_TOK_PRE_LLAMA3 && n > 1) {
        /* ignore_merges: a piece that is already a token is taken whole.
         * Its spelling in the byte alphabet is built in the work area (at
         * most two bytes per input byte). */
        uint8_t *spell = work;
        uint64_t sl = 0;
        for (uint32_t i = 0; i < n; i++) sl += utf8_enc(t->byte_cp[s[i]], spell + sl);
        int32_t whole = find_tok(t, spell, sl);
        if (whole >= 0) {
            emit(o, whole);
            return;
        }
    }
    uint32_t *id = (uint32_t *) work, *nx = id + n, *pv = nx + n;
    hent_t *heap = (hent_t *) (pv + n);
    uint32_t hn = 0;
    for (uint32_t i = 0; i < n; i++) {
        id[i] = (uint32_t) t->byte_tok[s[i]];
        nx[i] = i + 1 < n ? i + 1 : NONE;
        pv[i] = i ? i - 1 : NONE;
    }
    for (uint32_t i = 0; i + 1 < n; i++) {
        uint32_t m = find_merge(t, id[i], id[i + 1]);
        if (m != NONE) hpush(heap, &hn, (hent_t){m, i});
    }
    while (hn) {
        hent_t e = hpop(heap, &hn);
        uint32_t a = e.pos, b = nx[a];
        if (id[a] == NONE || b == NONE || find_merge(t, id[a], id[b]) != e.rank)
            continue; /* stale */
        id[a] = t->merge[3 * e.rank + 2];
        id[b] = NONE;
        nx[a] = nx[b];
        if (nx[b] != NONE) pv[nx[b]] = a;
        if (pv[a] != NONE) {
            uint32_t m = find_merge(t, id[pv[a]], id[a]);
            if (m != NONE) hpush(heap, &hn, (hent_t){m, pv[a]});
        }
        if (nx[a] != NONE) {
            uint32_t m = find_merge(t, id[a], id[nx[a]]);
            if (m != NONE) hpush(heap, &hn, (hent_t){m, a});
        }
    }
    for (uint32_t i = 0; i != NONE; i = nx[i]) emit(o, (int32_t) id[i]);
}

static void encode_plain(const zt_tok_t *t, const uint8_t *s, uint64_t n, uint8_t *work, emit_t *o)
{
    uint32_t digits = t->pre == ZT_TOK_PRE_LLAMA3 ? 3 : 1;
    for (uint64_t i = 0; i < n && !o->full;) {
        uint64_t j = piece_end(s, n, i, digits);
        bpe_piece(t, s + i, (uint32_t) (j - i), work, o);
        i = j;
    }
}

int32_t zt_tok_encode(const zt_tok_t *t, const uint8_t *text, uint64_t len, bool parse_special,
                      int32_t *ids, uint64_t cap, uint64_t *n_ids, void *work, uint64_t work_bytes)
{
    emit_t o = {ids, cap, 0, false};
    *n_ids = 0;
    if (len > 0x7FFFFFF0u) return ZT_GGUF_ERANGE;
    uint8_t *w = (uint8_t *) (((uintptr_t) work + 7) & ~(uintptr_t) 7);
    if (len && (!work || 36 * len + (uint64_t) (w - (uint8_t *) work) > work_bytes))
        return ZT_TOK_ESPACE;
    uint64_t seg = 0, i = 0;
    while (i < len && parse_special && t->n_special) {
        uint8_t b = text[i];
        int32_t hit = -1;
        if (t->special_first[b >> 3] & (1u << (b & 7))) {
            for (uint32_t k = 0; k < t->n_special; k++) {
                zt_gguf_str_t sp = t->tok[t->special[k]];
                if (sp.len <= len - i && bytes_eq(sp.p, text + i, sp.len)) {
                    hit = (int32_t) t->special[k];
                    break;
                }
            }
        }
        if (hit < 0) {
            i++;
            continue;
        }
        encode_plain(t, text + seg, i - seg, w, &o);
        emit(&o, hit);
        i += t->tok[hit].len;
        seg = i;
    }
    encode_plain(t, text + seg, len - seg, w, &o);
    *n_ids = o.n;
    return o.full ? ZT_TOK_ESPACE : ZT_GGUF_OK;
}

int32_t zt_tok_decode(const zt_tok_t *t, const int32_t *ids, uint64_t n, uint8_t *out, uint64_t cap,
                      uint64_t *n_out)
{
    uint64_t w = 0;
    int32_t r = ZT_GGUF_OK;
    for (uint64_t k = 0; k < n; k++) {
        if (ids[k] < 0 || (uint32_t) ids[k] >= t->n_vocab) {
            r = ZT_TOK_EBADID;
            break;
        }
        zt_gguf_str_t s = t->tok[ids[k]];
        bool raw = t->ttype[ids[k]] == ZT_TOK_CONTROL || t->ttype[ids[k]] == ZT_TOK_USER;
        for (uint64_t i = 0; i < s.len;) {
            uint32_t c, l = utf8_dec(s.p, s.len, i, &c);
            uint8_t one[4];
            uint32_t nb = 0;
            if (raw) {
                for (uint32_t q = 0; q < l; q++) one[nb++] = s.p[i + q];
            } else if (c < 256 && byte_is_self(c)) {
                one[nb++] = (uint8_t) c;
            } else if (c >= 256 && c < 256 + 68) {
                one[nb++] = t->unbyte[c - 256];
            } else {
                for (uint32_t q = 0; q < l; q++)
                    one[nb++] = s.p[i + q]; /* not in the byte alphabet: as is */
            }
            if (w + nb > cap) {
                *n_out = w;
                return ZT_TOK_ESPACE;
            }
            for (uint32_t q = 0; q < nb; q++) out[w++] = one[q];
            i += l;
        }
    }
    *n_out = w;
    return r;
}
