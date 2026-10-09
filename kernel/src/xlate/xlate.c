/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xlate.c — the auto-translation pipeline, cache, reader settings and
 * markers. See xlate.h. */
#include "xlate.h"
#include "sha256.h"

/* ---- bounded helpers (no libc) ---- */
static uint32_t x_len(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

static void x_tag(char *dst, const char *src)
{
    uint32_t i = 0;
    if (src)
        for (; i + 1 < XLATE_TAG_MAX && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static char x_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static bool x_tag_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] && b[i] && x_lower(a[i]) == x_lower(b[i])) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static bool x_memeq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

/* Copy n bytes and NUL-terminate; returns false if truncated. Never splits a
 * UTF-8 sequence when it has to cut. */
static bool x_out(char *out, uint32_t cap, const char *src, uint32_t n, uint32_t *written)
{
    bool fit = n + 1 <= cap;
    uint32_t k = fit ? n : cap - 1;
    if (!fit)
        while (k > 0 && ((uint8_t) src[k] & 0xC0) == 0x80) k--;
    for (uint32_t i = 0; i < k; i++) out[i] = src[i];
    out[k] = '\0';
    *written = k;
    return fit;
}

static bool is_auto(const char *src)
{
    return !src || src[0] == '\0' || x_tag_eq(src, "auto");
}

/* Primary subtag ("pt" of "pt-BR"), and for Chinese the script. */
static void x_norm_lang(const char *t, char *prim, char *script)
{
    uint32_t i = 0, k = 0;
    while (t[i] && t[i] != '-' && t[i] != '_' && k + 1 < XLATE_TAG_MAX) prim[k++] = x_lower(t[i++]);
    prim[k] = '\0';
    script[0] = '\0';
    if (!x_tag_eq(prim, "zh")) return;
    /* zh, zh-CN, zh-SG, zh-Hans -> Hans; zh-TW, zh-HK, zh-MO, zh-Hant -> Hant */
    const char *rest = t + i;
    const char *s = "hans";
    for (uint32_t j = 0; rest[j]; j++) {
        if (rest[j] != '-' && rest[j] != '_') continue;
        const char *sub = rest + j + 1;
        char b[5] = {0};
        uint32_t m = 0;
        while (sub[m] && sub[m] != '-' && sub[m] != '_' && m < 4) {
            b[m] = x_lower(sub[m]);
            m++;
        }
        if (x_tag_eq(b, "hant") || x_tag_eq(b, "tw") || x_tag_eq(b, "hk") || x_tag_eq(b, "mo"))
            s = "hant";
    }
    for (k = 0; s[k]; k++) script[k] = s[k];
    script[k] = '\0';
}

bool xlate_same_language(const char *a, const char *b)
{
    if (!a || !b) return false;
    char pa[XLATE_TAG_MAX], sa[XLATE_TAG_MAX], pb[XLATE_TAG_MAX], sb[XLATE_TAG_MAX];
    x_norm_lang(a, pa, sa);
    x_norm_lang(b, pb, sb);
    return x_tag_eq(pa, pb) && x_tag_eq(sa, sb);
}

/* ---- context and cache ---- */
void xlate_init(xlate_t *x)
{
    if (!x) return;
    x->backend.translate = 0;
    x->backend.ctx = 0;
    x->backend.model_id = "";
    x->backend.tmpl = 0;
    x->clock = 0;
    x->hits = x->misses = x->backend_calls = x->backend_failures = 0;
    xlate_cache_clear(x);
}

void xlate_cache_clear(xlate_t *x)
{
    if (!x) return;
    for (uint32_t i = 0; i < XLATE_CACHE_SLOTS; i++) {
        x->slot[i].used = false;
        x->slot[i].len = 0;
        x->slot[i].stamp = 0;
    }
}

void xlate_set_backend(xlate_t *x, const xlate_backend_t *b)
{
    if (!x) return;
    if (b) {
        x->backend = *b;
        if (!x->backend.model_id) x->backend.model_id = "";
    } else {
        x->backend.translate = 0;
        x->backend.ctx = 0;
        x->backend.model_id = "";
        x->backend.tmpl = 0;
    }
    xlate_cache_clear(x);
}

static xlate_cache_slot_t *cache_find(xlate_t *x, const uint8_t *key, const char *src,
                                      const char *tgt)
{
    for (uint32_t i = 0; i < XLATE_CACHE_SLOTS; i++) {
        xlate_cache_slot_t *s = &x->slot[i];
        if (s->used && x_memeq(s->key, key, XLATE_KEY_LEN) && x_tag_eq(s->src, src) &&
            x_tag_eq(s->tgt, tgt))
            return s;
    }
    return 0;
}

static void cache_put(xlate_t *x, const uint8_t *key, const char *src, const char *tgt,
                      const char *text, uint32_t len)
{
    if (len + 1 > XLATE_CACHE_TEXT) return;
    xlate_cache_slot_t *v = cache_find(x, key, src, tgt);
    if (!v) {
        v = &x->slot[0];
        for (uint32_t i = 0; i < XLATE_CACHE_SLOTS; i++) {
            if (!x->slot[i].used) {
                v = &x->slot[i];
                break;
            }
            if (x->slot[i].stamp < v->stamp) v = &x->slot[i];
        }
    }
    v->used = true;
    for (uint32_t i = 0; i < XLATE_KEY_LEN; i++) v->key[i] = key[i];
    x_tag(v->src, src);
    x_tag(v->tgt, tgt);
    for (uint32_t i = 0; i < len; i++) v->text[i] = text[i];
    v->text[len] = '\0';
    v->len = len;
    v->stamp = ++x->clock;
}

static void make_key(const xlate_request_t *req, uint8_t key[XLATE_KEY_LEN])
{
    /* Domain-separated so a text can never collide with a content id. */
    sha256_ctx_t c;
    sha256_init(&c);
    if (req->content_id && req->content_id_len) {
        sha256_update(&c, (const uint8_t *) "zxv-xlate-cid", 13);
        sha256_update(&c, req->content_id, req->content_id_len);
    } else {
        sha256_update(&c, (const uint8_t *) "zxv-xlate-txt", 13);
        sha256_update(&c, (const uint8_t *) req->text, req->len);
    }
    sha256_final(&c, key);
}

/* Cut the model output at the stop sequence (and at any chat control token),
 * trim whitespace, drop a "[MT ..]" tag the model may have echoed. */
static uint32_t clean_output(char *s, uint32_t n, const char *stop)
{
    static const char *const cut[] = {"<|im_end|>", "<|im_start|>", "<|endoftext|>", "</s>",
                                      "[INST]"};
    uint32_t stop_len = x_len(stop, 64);
    for (uint32_t i = 0; i < n; i++) {
        bool hit = false;
        if (stop_len && i + stop_len <= n) {
            hit = true;
            for (uint32_t k = 0; k < stop_len; k++)
                if (s[i + k] != stop[k]) hit = false;
        }
        for (uint32_t c = 0; !hit && c < sizeof cut / sizeof cut[0]; c++) {
            uint32_t L = x_len(cut[c], 32);
            if (i + L > n) continue;
            hit = true;
            for (uint32_t k = 0; k < L; k++)
                if (s[i + k] != cut[c][k]) hit = false;
        }
        if (hit) {
            n = i;
            break;
        }
    }
    uint32_t a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\n' || s[a] == '\r' || s[a] == '\t')) a++;
    while (n > a && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t'))
        n--;
    for (uint32_t i = a; i < n; i++) s[i - a] = s[i];
    n -= a;
    s[n] = '\0';
    return n;
}

static void resp_reset(xlate_response_t *r)
{
    r->status = XLATE_OK;
    r->flags = 0;
    r->len = 0;
    r->src[0] = r->tgt[0] = '\0';
    r->src_confidence = 0;
    r->model_id = "";
    for (uint32_t i = 0; i < XLATE_KEY_LEN; i++) r->key[i] = 0;
}

static int give_original(const xlate_request_t *req, char *out, uint32_t cap, xlate_response_t *r,
                         int status)
{
    uint32_t w;
    if (!x_out(out, cap, req->text, req->len, &w)) r->flags |= XLATE_F_TRUNCATED;
    r->len = w;
    r->flags |= XLATE_F_ORIGINAL;
    r->model_id = "";
    r->status = status;
    return status;
}

int xlate_translate(xlate_t *x, const xlate_request_t *req, char *out, uint32_t cap,
                    xlate_response_t *resp)
{
    xlate_response_t dummy;
    xlate_response_t *r = resp ? resp : &dummy;
    resp_reset(r);
    if (!x || !req || !req->text || !req->tgt || !out || cap == 0) {
        r->status = XLATE_ERR_ARG;
        return XLATE_ERR_ARG;
    }
    uint32_t len = x_len(req->text, req->len);
    xlate_request_t q = *req;
    q.len = len;
    x_tag(r->tgt, req->tgt);
    make_key(&q, r->key);

    if (is_auto(req->src)) {
        xlate_langid_result_t li;
        xlate_langid(q.text, len, &li);
        x_tag(r->src, li.tag);
        r->src_confidence = li.confidence;
        r->flags |= XLATE_F_SRC_DETECTED;
        if (!li.reliable) r->flags |= XLATE_F_SRC_UNSURE;
        if (li.lang < 0) return give_original(&q, out, cap, r, XLATE_ERR_UNKNOWN_LANG);
    } else {
        x_tag(r->src, req->src);
        r->src_confidence = 100;
    }
    if (xlate_same_language(r->src, r->tgt) || xlate_has_marker_tag(q.text, len))
        return give_original(&q, out, cap, r, XLATE_OK);
    if (len > XLATE_TEXT_MAX) return give_original(&q, out, cap, r, XLATE_ERR_TOO_LONG);

    xlate_cache_slot_t *hit = cache_find(x, r->key, r->src, r->tgt);
    if (hit) {
        uint32_t w;
        x->hits++;
        hit->stamp = ++x->clock;
        if (!x_out(out, cap, hit->text, hit->len, &w)) r->flags |= XLATE_F_TRUNCATED;
        r->len = w;
        r->flags |= XLATE_F_MACHINE | XLATE_F_CACHED;
        r->model_id = x->backend.model_id;
        return XLATE_OK;
    }
    x->misses++;
    if (!x->backend.translate) return give_original(&q, out, cap, r, XLATE_ERR_NO_BACKEND);

    xlate_backend_request_t br;
    br.prompt = "";
    br.prompt_len = 0;
    br.stop = "";
    if (x->backend.tmpl) {
        int32_t pl = xlate_build_prompt(x->backend.tmpl, r->src, r->tgt, q.kind, q.text, len,
                                        x->prompt, XLATE_PROMPT_MAX);
        if (pl < 0) return give_original(&q, out, cap, r, XLATE_ERR_TOO_LONG);
        br.prompt = x->prompt;
        br.prompt_len = (uint32_t) pl;
        br.stop = x->backend.tmpl->stop;
    }
    br.text = q.text;
    br.text_len = len;
    br.src = r->src;
    br.tgt = r->tgt;
    br.kind = q.kind;
    br.max_out = XLATE_TEXT_MAX - 1;

    uint32_t got = 0;
    x->backend_calls++;
    int rc = x->backend.translate(&br, x->work, XLATE_TEXT_MAX - 1, &got, x->backend.ctx);
    if (got > XLATE_TEXT_MAX - 1) got = XLATE_TEXT_MAX - 1; /* untrusted */
    if (rc >= 0) got = clean_output(x->work, got, br.stop);
    if (rc < 0 || got == 0) {
        x->backend_failures++;
        return give_original(&q, out, cap, r, XLATE_ERR_BACKEND);
    }
    cache_put(x, r->key, r->src, r->tgt, x->work, got);
    uint32_t w;
    if (!x_out(out, cap, x->work, got, &w)) r->flags |= XLATE_F_TRUNCATED;
    r->len = w;
    r->flags |= XLATE_F_MACHINE;
    r->model_id = x->backend.model_id;
    return XLATE_OK;
}

/* ---- reader settings ---- */
void xlate_prefs_init(xlate_prefs_t *p, const char *native)
{
    if (!p) return;
    x_tag(p->native, native ? native : "en");
    p->auto_translate = true;
    p->translate_captions = true;
    p->known_count = 0;
    p->original_count = 0;
    p->original_next = 0;
}

int xlate_prefs_add_known(xlate_prefs_t *p, const char *tag)
{
    if (!p || !tag || !tag[0]) return XLATE_ERR_ARG;
    for (uint32_t i = 0; i < p->known_count; i++)
        if (xlate_same_language(p->known[i], tag)) return XLATE_OK;
    if (p->known_count >= XLATE_KNOWN_MAX) return XLATE_ERR_TOO_LONG;
    x_tag(p->known[p->known_count++], tag);
    return XLATE_OK;
}

static int32_t orig_index(const xlate_prefs_t *p, const uint8_t *key)
{
    for (uint32_t i = 0; i < p->original_count; i++)
        if (x_memeq(p->original[i], key, 8)) return (int32_t) i;
    return -1;
}

bool xlate_prefs_shows_original(const xlate_prefs_t *p, const uint8_t key[XLATE_KEY_LEN])
{
    return p && key && orig_index(p, key) >= 0;
}

bool xlate_prefs_toggle_original(xlate_prefs_t *p, const uint8_t key[XLATE_KEY_LEN])
{
    if (!p || !key) return false;
    int32_t at = orig_index(p, key);
    if (at >= 0) {
        /* Remove by moving the last entry into its place. */
        uint32_t last = p->original_count - 1;
        for (uint32_t i = 0; i < 8; i++) p->original[at][i] = p->original[last][i];
        p->original_count--;
        if (p->original_next > p->original_count) p->original_next = p->original_count;
        return false;
    }
    uint32_t slot;
    if (p->original_count < XLATE_ORIGINAL_TOGGLES) {
        slot = p->original_count++;
    } else {
        slot = p->original_next;
        p->original_next = (p->original_next + 1) % XLATE_ORIGINAL_TOGGLES;
    }
    for (uint32_t i = 0; i < 8; i++) p->original[slot][i] = key[i];
    return true;
}

bool xlate_prefs_wants(const xlate_prefs_t *p, const char *lang)
{
    if (!p || !lang || !p->auto_translate) return false;
    if (xlate_same_language(p->native, lang)) return false;
    for (uint32_t i = 0; i < p->known_count; i++)
        if (xlate_same_language(p->known[i], lang)) return false;
    return true;
}

int xlate_view(xlate_t *x, const xlate_prefs_t *p, const xlate_request_t *item, char *out,
               uint32_t cap, xlate_response_t *resp)
{
    xlate_response_t dummy;
    xlate_response_t *r = resp ? resp : &dummy;
    resp_reset(r);
    if (!x || !p || !item || !item->text || !out || cap == 0) {
        r->status = XLATE_ERR_ARG;
        return XLATE_ERR_ARG;
    }
    xlate_request_t q = *item;
    q.len = x_len(item->text, item->len);
    q.tgt = p->native;
    if (!p->auto_translate || (q.kind == XLATE_KIND_CAPTION && !p->translate_captions)) {
        make_key(&q, r->key);
        x_tag(r->tgt, p->native);
        return give_original(&q, out, cap, r, XLATE_OK);
    }
    /* Language first (cheap), so readers of the source language never pay
     * for a cache lookup or a model call. */
    xlate_langid_result_t li;
    bool detected = is_auto(q.src);
    if (detected) {
        xlate_langid(q.text, q.len, &li);
        q.src = li.tag;
    }
    if (!xlate_prefs_wants(p, q.src) || (detected && li.lang < 0)) {
        int st = (detected && li.lang < 0) ? XLATE_ERR_UNKNOWN_LANG : XLATE_OK;
        make_key(&q, r->key);
        x_tag(r->src, q.src);
        x_tag(r->tgt, p->native);
        if (detected) {
            r->flags |= XLATE_F_SRC_DETECTED;
            r->src_confidence = li.confidence;
            if (!li.reliable) r->flags |= XLATE_F_SRC_UNSURE;
        }
        return give_original(&q, out, cap, r, st);
    }
    uint8_t key[XLATE_KEY_LEN];
    make_key(&q, key);
    if (xlate_prefs_shows_original(p, key)) {
        for (uint32_t i = 0; i < XLATE_KEY_LEN; i++) r->key[i] = key[i];
        x_tag(r->src, q.src);
        x_tag(r->tgt, p->native);
        r->flags |= XLATE_F_CAN_TRANSLATE;
        return give_original(&q, out, cap, r, XLATE_OK);
    }
    int rc = xlate_translate(x, &q, out, cap, r);
    if (detected) {
        r->flags |= XLATE_F_SRC_DETECTED;
        r->src_confidence = li.confidence;
        if (!li.reliable) r->flags |= XLATE_F_SRC_UNSURE;
    }
    return rc;
}

/* ---- markers ---- */
typedef struct {
    char *p;
    uint32_t cap, n;
} mw_t;
static void mw_str(mw_t *w, const char *s)
{
    while (s && *s) {
        if (w->n + 1 < w->cap) w->p[w->n++] = *s;
        s++;
    }
    w->p[w->n] = '\0';
}

uint32_t xlate_marker_label(const xlate_response_t *r, char *out, uint32_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!r || !(r->flags & XLATE_F_MACHINE)) return 0;
    mw_t w = {out, cap, 0};
    int32_t i = xlate_langid_find(r->src);
    mw_str(&w, "Translated from ");
    mw_str(&w, i >= 0 ? xlate_langid_name((uint32_t) i) : r->src);
    mw_str(&w, " by machine");
    if (r->model_id && r->model_id[0]) {
        mw_str(&w, " (");
        mw_str(&w, r->model_id);
        mw_str(&w, ")");
    }
    return w.n;
}

uint32_t xlate_marker_tag(const xlate_response_t *r, char *out, uint32_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!r || !(r->flags & XLATE_F_MACHINE)) return 0;
    mw_t w = {out, cap, 0};
    mw_str(&w, "[MT ");
    mw_str(&w, r->src);
    mw_str(&w, ">");
    mw_str(&w, r->tgt);
    mw_str(&w, "]");
    return w.n;
}

bool xlate_has_marker_tag(const char *t, uint32_t len)
{
    if (!t || len < 8) return false;
    if (!(t[0] == '[' && t[1] == 'M' && t[2] == 'T' && t[3] == ' ')) return false;
    bool gt = false;
    for (uint32_t i = 4; i < len && i < 4 + 2 * XLATE_TAG_MAX + 1 && t[i]; i++) {
        if (t[i] == '>') gt = true;
        if (t[i] == ']') return gt;
        if (t[i] == ' ') return false;
    }
    return false;
}

/* ---- live captions ---- */
int xlate_caption_filter(const char *in, const char *src_lang, char *out, uint32_t cap, void *ctx)
{
    const xlate_caption_ctx_t *c = (const xlate_caption_ctx_t *) ctx;
    if (!in || !out || cap == 0) return XLATE_ERR_ARG;
    uint32_t n = x_len(in, XLATE_TEXT_MAX + 1), w;
    if (!c || !c->x || !c->prefs) {
        x_out(out, cap, in, n, &w);
        return XLATE_ERR_ARG;
    }
    xlate_request_t q;
    q.text = in;
    q.len = n;
    q.content_id = 0;
    q.content_id_len = 0;
    q.src = src_lang;
    q.tgt = c->prefs->native;
    q.kind = XLATE_KIND_CAPTION;
    xlate_response_t r;
    int rc = xlate_view(c->x, c->prefs, &q, out, cap, &r);
    if (rc < 0) return rc;
    return (r.flags & XLATE_F_MACHINE) ? 1 : 0;
}
