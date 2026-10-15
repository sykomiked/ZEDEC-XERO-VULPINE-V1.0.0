/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bv.c — the Babel value (bv.h). */
#include "bv.h"

void bv_reset(bv_arena_t *a)
{
    a->nn = a->nk = a->nb = 0;
}

static bool live(const bv_arena_t *a, int32_t v)
{
    return v >= 0 && (uint32_t) v < a->nn;
}

static int32_t node(bv_arena_t *a, uint8_t kind, uint8_t depth, uint32_t len, int64_t x, int64_t y)
{
    if (a->nn >= BV_MAX_NODES) return BV_E_LIMIT;
    a->node[a->nn] = (bv_node_t){kind, depth, len, x, y};
    return (int32_t) a->nn++;
}

int32_t bv_nothing(bv_arena_t *a)
{
    return node(a, BV_NOTHING, 0, 0, 0, 0);
}

int32_t bv_int(bv_arena_t *a, int64_t v)
{
    return node(a, BV_INTEGER, 0, 0, v, 0);
}

static uint64_t mag(int64_t v)
{
    return v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}

static uint64_t gcd(uint64_t x, uint64_t y)
{
    while (y) {
        uint64_t t = x % y;
        x = y;
        y = t;
    }
    return x;
}

/* A sign and a magnitude as int64, or false when it does not fit. */
static bool signed_fits(bool neg, uint64_t m, int64_t *out)
{
    if (neg) {
        if (m > (uint64_t) INT64_MAX + 1u) return false;
        *out = m == (uint64_t) INT64_MAX + 1u ? INT64_MIN : -(int64_t) m;
    } else {
        if (m > (uint64_t) INT64_MAX) return false;
        *out = (int64_t) m;
    }
    return true;
}

static int32_t ratio_mag(bv_arena_t *a, bool neg, uint64_t n, uint64_t d)
{
    if (!d) return BV_E_DOMAIN;
    uint64_t g = gcd(n, d);
    if (g) {
        n /= g;
        d /= g;
    }
    if (!n) return bv_int(a, 0);
    int64_t num;
    if (!signed_fits(neg, n, &num)) return BV_E_OVERFLOW;
    if (d == 1) return bv_int(a, num);
    if (d > (uint64_t) INT64_MAX) return BV_E_OVERFLOW;
    return node(a, BV_RATIO, 0, 0, num, (int64_t) d);
}

int32_t bv_ratio(bv_arena_t *a, int64_t num, int64_t den)
{
    if (!den) return BV_E_DOMAIN;
    return ratio_mag(a, (num < 0) != (den < 0), mag(num), mag(den));
}

int32_t bv_decimal(bv_arena_t *a, const char *s, uint32_t n)
{
    uint32_t i = 0;
    bool neg = false;
    if (i < n && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
    uint64_t m = 0, d = 1;
    uint32_t digits = 0;
    bool point = false;
    for (; i < n; i++) {
        if (s[i] == '.' && !point) {
            point = true;
            if (!digits) return BV_E_FORM; /* ".5": a digit comes first */
            continue;
        }
        if (s[i] < '0' || s[i] > '9') return BV_E_FORM;
        uint64_t dg = (uint64_t) (s[i] - '0');
        if (m > (UINT64_MAX - dg) / 10u) return BV_E_OVERFLOW;
        m = m * 10u + dg;
        if (point) {
            if (d > UINT64_MAX / 10u) return BV_E_OVERFLOW;
            d *= 10u;
        }
        digits++;
    }
    if (!digits || (point && s[n - 1] == '.')) return BV_E_FORM;
    return ratio_mag(a, neg, m, d);
}

int32_t bv_truth(bv_arena_t *a, int64_t t)
{
    if (t < BV_NEITHER || t > BV_BOTH) return BV_E_DOMAIN;
    return node(a, BV_TRUTH, 0, 0, t, 0);
}

/* Well-formed UTF-8: no overlong forms, no surrogates, at most U+10FFFF. */
static bool utf8_ok(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n;) {
        uint8_t c = p[i];
        uint32_t k;
        uint32_t cp;
        if (c < 0x80) {
            i++;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            k = 1;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            k = 2;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            k = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (n - i <= k) return false;
        for (uint32_t j = 1; j <= k; j++) {
            if ((p[i + j] & 0xC0u) != 0x80u) return false;
            cp = cp << 6 | (p[i + j] & 0x3Fu);
        }
        if ((k == 2 && cp < 0x800u) || (k == 3 && cp < 0x10000u) || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu))
            return false;
        i += k + 1;
    }
    return true;
}

static int32_t blob(bv_arena_t *a, uint8_t kind, const uint8_t *p, uint32_t n)
{
    if (kind != BV_BYTES && !utf8_ok(p, n)) return BV_E_TEXT;
    if (kind == BV_SYMBOL && !n) return BV_E_NAME;
    if (n > BV_MAX_BYTES - a->nb || a->nn >= BV_MAX_NODES) return BV_E_LIMIT;
    uint32_t at = a->nb;
    for (uint32_t i = 0; i < n; i++) a->byte[at + i] = p[i];
    a->nb += n;
    return node(a, kind, 0, n, at, 0);
}

int32_t bv_bytes(bv_arena_t *a, const uint8_t *p, uint32_t n)
{
    return blob(a, BV_BYTES, p, n);
}

int32_t bv_text(bv_arena_t *a, const char *s, uint32_t n)
{
    return blob(a, BV_TEXT, (const uint8_t *) s, n);
}

int32_t bv_symbol(bv_arena_t *a, const char *s, uint32_t n)
{
    return blob(a, BV_SYMBOL, (const uint8_t *) s, n);
}

static bool same_bytes(const bv_arena_t *a, int32_t x, const bv_arena_t *b, int32_t y)
{
    const bv_node_t *p = &a->node[x], *q = &b->node[y];
    if (p->len != q->len) return false;
    for (uint32_t i = 0; i < p->len; i++)
        if (a->byte[p->a + i] != b->byte[q->a + i]) return false;
    return true;
}

/* A list or record over kids[0..k): depth one more than the deepest. */
static int32_t parent(bv_arena_t *a, uint8_t kind, const int32_t *k1, const int32_t *k2, uint32_t n)
{
    uint32_t per = kind == BV_RECORD ? 2u : 1u;
    uint32_t depth = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!live(a, k1[i]) || (k2 && !live(a, k2[i]))) return BV_E_HANDLE;
        if (k2) {
            if (a->node[k1[i]].kind != BV_SYMBOL) return BV_E_NAME;
            for (uint32_t j = 0; j < i; j++)
                if (same_bytes(a, k1[i], a, k1[j])) return BV_E_NAME;
        }
        const bv_node_t *c = &a->node[k2 ? k2[i] : k1[i]];
        if (c->depth > depth) depth = c->depth;
    }
    if (depth + 1u > BV_MAX_DEPTH) return BV_E_LIMIT;
    if (n > (BV_MAX_KIDS - a->nk) / per || a->nn >= BV_MAX_NODES) return BV_E_LIMIT;
    uint32_t at = a->nk;
    for (uint32_t i = 0; i < n; i++) {
        a->kid[at + per * i] = (uint32_t) k1[i];
        if (k2) a->kid[at + per * i + 1u] = (uint32_t) k2[i];
    }
    a->nk += per * n;
    return node(a, kind, (uint8_t) (depth + 1u), n, at, 0);
}

int32_t bv_list(bv_arena_t *a, const int32_t *v, uint32_t n)
{
    return parent(a, BV_LIST, v, 0, n);
}

int32_t bv_record(bv_arena_t *a, const int32_t *names, const int32_t *v, uint32_t n)
{
    return parent(a, BV_RECORD, names, v, n);
}

int32_t bv_pair(bv_arena_t *a, int32_t head, int32_t tail)
{
    if (!live(a, head) || !live(a, tail)) return BV_E_HANDLE;
    const bv_node_t *t = &a->node[tail];
    if (t->kind != BV_LIST) {
        int32_t k[2] = {head, tail};
        return parent(a, BV_PAIR, k, 0, 2);
    }
    /* (h . (x y)) is (h x y): gather the new list's kids after the arena's */
    uint32_t n = t->len;
    if (n + 1u > BV_MAX_KIDS - a->nk) return BV_E_LIMIT;
    uint8_t d =
        a->node[head].depth > t->depth - 1u ? a->node[head].depth : (uint8_t) (t->depth - 1u);
    if (d + 1u > BV_MAX_DEPTH) return BV_E_LIMIT;
    if (a->nn >= BV_MAX_NODES) return BV_E_LIMIT;
    uint32_t at = a->nk;
    a->kid[at] = (uint32_t) head;
    for (uint32_t i = 0; i < n; i++) a->kid[at + 1u + i] = a->kid[t->a + i];
    a->nk += n + 1u;
    return node(a, BV_LIST, (uint8_t) (d + 1u), n + 1u, at, 0);
}

bv_kind_t bv_kind(const bv_arena_t *a, int32_t v)
{
    return live(a, v) ? (bv_kind_t) a->node[v].kind : BV_NOTHING;
}

uint32_t bv_len(const bv_arena_t *a, int32_t v)
{
    if (!live(a, v)) return 0;
    uint8_t k = a->node[v].kind;
    return k >= BV_BYTES ? a->node[v].len : 0; /* a pair's len is 2 */
}

int64_t bv_num(const bv_arena_t *a, int32_t v)
{
    if (!live(a, v)) return 0;
    uint8_t k = a->node[v].kind;
    return k == BV_INTEGER || k == BV_RATIO || k == BV_TRUTH ? a->node[v].a : 0;
}

int64_t bv_den(const bv_arena_t *a, int32_t v)
{
    if (!live(a, v)) return 0;
    uint8_t k = a->node[v].kind;
    return k == BV_RATIO ? a->node[v].b : k == BV_INTEGER ? 1 : 0;
}

const uint8_t *bv_data(const bv_arena_t *a, int32_t v)
{
    if (!live(a, v)) return 0;
    uint8_t k = a->node[v].kind;
    return k == BV_BYTES || k == BV_TEXT || k == BV_SYMBOL ? &a->byte[a->node[v].a] : 0;
}

int32_t bv_at(const bv_arena_t *a, int32_t v, uint32_t i)
{
    if (!live(a, v)) return BV_E_HANDLE;
    const bv_node_t *p = &a->node[v];
    if ((p->kind != BV_LIST && p->kind != BV_RECORD && p->kind != BV_PAIR) || i >= p->len)
        return BV_E_HANDLE;
    return (int32_t) a->kid[p->a + (p->kind == BV_RECORD ? 2u * i + 1u : i)];
}

int32_t bv_name(const bv_arena_t *a, int32_t v, uint32_t i)
{
    if (!live(a, v)) return BV_E_HANDLE;
    const bv_node_t *p = &a->node[v];
    if (p->kind != BV_RECORD || i >= p->len) return BV_E_HANDLE;
    return (int32_t) a->kid[p->a + 2u * i];
}

int32_t bv_get(const bv_arena_t *a, int32_t v, const char *name, uint32_t n)
{
    if (!live(a, v) || a->node[v].kind != BV_RECORD) return BV_E_HANDLE;
    for (uint32_t i = 0; i < a->node[v].len; i++) {
        const bv_node_t *s = &a->node[a->kid[a->node[v].a + 2u * i]];
        if (s->len != n) continue;
        uint32_t j = 0;
        while (j < n && a->byte[s->a + j] == (uint8_t) name[j]) j++;
        if (j == n) return bv_at(a, v, i);
    }
    return BV_E_NAME;
}

/* ---- canonical encoding ---------------------------------------------------- */

typedef struct {
    uint8_t *p;
    uint32_t n, cap;
    bool full;
} wr_t;

static void put(wr_t *w, uint64_t x, uint32_t bytes)
{
    if (bytes > w->cap - w->n) {
        w->full = true;
        return;
    }
    for (uint32_t i = 0; i < bytes; i++) w->p[w->n++] = (uint8_t) (x >> (8u * (bytes - 1u - i)));
}

/* Recursion depth is the value's depth, at most BV_MAX_DEPTH. */
static void enc(const bv_arena_t *a, int32_t v, wr_t *w)
{
    const bv_node_t *p = &a->node[v];
    put(w, p->kind, 1);
    switch (p->kind) {
    case BV_INTEGER:
        put(w, (uint64_t) p->a, 8);
        break;
    case BV_RATIO:
        put(w, (uint64_t) p->a, 8);
        put(w, (uint64_t) p->b, 8);
        break;
    case BV_TRUTH:
        put(w, (uint64_t) p->a, 1);
        break;
    case BV_BYTES:
    case BV_TEXT:
    case BV_SYMBOL:
        put(w, p->len, 4);
        if (p->len > w->cap - w->n) {
            w->full = true;
            break;
        }
        for (uint32_t i = 0; i < p->len; i++) w->p[w->n++] = a->byte[p->a + i];
        break;
    case BV_PAIR:
        enc(a, (int32_t) a->kid[p->a], w);
        if (!w->full) enc(a, (int32_t) a->kid[p->a + 1u], w);
        break;
    case BV_LIST:
    case BV_RECORD:
        put(w, p->len, 4);
        for (uint32_t i = 0; i < p->len * (p->kind == BV_RECORD ? 2u : 1u) && !w->full; i++)
            enc(a, (int32_t) a->kid[p->a + i], w);
        break;
    default:
        break;
    }
}

int32_t bv_encode(const bv_arena_t *a, int32_t v, uint8_t *out, uint32_t cap)
{
    if (!live(a, v)) return BV_E_HANDLE;
    if (cap > (uint32_t) INT32_MAX) cap = (uint32_t) INT32_MAX;
    wr_t w = {out, 0, cap, false};
    enc(a, v, &w);
    return w.full ? BV_E_LIMIT : (int32_t) w.n;
}

typedef struct {
    const uint8_t *p;
    uint32_t n, at;
} rd_t;

static bool get(rd_t *r, uint32_t bytes, uint64_t *x)
{
    if (bytes > r->n - r->at) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < bytes; i++) v = v << 8 | r->p[r->at++];
    *x = v;
    return true;
}

static int32_t dec(bv_arena_t *a, rd_t *r, uint32_t depth)
{
    uint64_t kind, x, y;
    if (!get(r, 1, &kind)) return BV_E_FORM;
    switch (kind) {
    case BV_NOTHING:
        return bv_nothing(a);
    case BV_INTEGER:
        if (!get(r, 8, &x)) return BV_E_FORM;
        return bv_int(a, (int64_t) x);
    case BV_RATIO: {
        if (!get(r, 8, &x) || !get(r, 8, &y)) return BV_E_FORM;
        int64_t num = (int64_t) x, den = (int64_t) y;
        if (den <= 1 || !num || gcd(mag(num), (uint64_t) den) != 1) return BV_E_FORM;
        return node(a, BV_RATIO, 0, 0, num, den);
    }
    case BV_TRUTH:
        if (!get(r, 1, &x)) return BV_E_FORM;
        return x > BV_BOTH ? BV_E_FORM : bv_truth(a, (int64_t) x);
    case BV_BYTES:
    case BV_TEXT:
    case BV_SYMBOL:
        if (!get(r, 4, &x) || x > r->n - r->at) return BV_E_FORM;
        r->at += (uint32_t) x;
        return blob(a, (uint8_t) kind, r->p + r->at - (uint32_t) x, (uint32_t) x);
    case BV_PAIR: {
        if (depth >= BV_MAX_DEPTH) return BV_E_LIMIT;
        if (BV_MAX_KIDS - a->nk < 2u) return BV_E_LIMIT;
        uint32_t base = a->nk;
        a->nk += 2u;
        for (uint32_t i = 0; i < 2u; i++) {
            int32_t c = dec(a, r, depth + 1u);
            if (c < 0) return c;
            a->kid[base + i] = (uint32_t) c;
        }
        const bv_node_t *h = &a->node[a->kid[base]], *t = &a->node[a->kid[base + 1u]];
        if (t->kind == BV_LIST) return BV_E_FORM; /* that is a list */
        uint8_t d = h->depth > t->depth ? h->depth : t->depth;
        if (d + 1u > BV_MAX_DEPTH) return BV_E_LIMIT;
        return node(a, BV_PAIR, (uint8_t) (d + 1u), 2u, base, 0);
    }
    case BV_LIST:
    case BV_RECORD: {
        if (depth >= BV_MAX_DEPTH) return BV_E_LIMIT;
        uint32_t per = kind == BV_RECORD ? 2u : 1u;
        /* each element takes at least one byte: a count past what is left is malformed */
        if (!get(r, 4, &x) || x * per > r->n - r->at) return BV_E_FORM;
        uint32_t n = (uint32_t) x;
        if (n * per > BV_MAX_KIDS - a->nk) return BV_E_LIMIT;
        /* children are built first, then gathered: hold their handles in kid space */
        uint32_t base = a->nk;
        a->nk += n * per;
        for (uint32_t i = 0; i < n * per; i++) {
            int32_t c = dec(a, r, depth + 1u);
            if (c < 0) return c;
            if (per == 2u && !(i & 1u) && a->node[c].kind != BV_SYMBOL) return BV_E_FORM;
            a->kid[base + i] = (uint32_t) c;
        }
        uint8_t d = 0;
        for (uint32_t i = 0; i < n; i++) {
            const bv_node_t *c = &a->node[a->kid[base + per * i + per - 1u]];
            if (c->depth > d) d = c->depth;
            if (per == 2u)
                for (uint32_t j = 0; j < i; j++)
                    if (same_bytes(a, (int32_t) a->kid[base + 2u * i], a,
                                   (int32_t) a->kid[base + 2u * j]))
                        return BV_E_FORM;
        }
        if (d + 1u > BV_MAX_DEPTH) return BV_E_LIMIT;
        return node(a, (uint8_t) kind, (uint8_t) (d + 1u), n, base, 0);
    }
    default:
        return BV_E_FORM;
    }
}

int32_t bv_decode(bv_arena_t *a, const uint8_t *in, uint32_t n)
{
    uint32_t nn = a->nn, nk = a->nk, nb = a->nb;
    rd_t r = {in, n, 0};
    int32_t v = dec(a, &r, 0);
    if (v >= 0 && r.at != n) v = BV_E_FORM;
    if (v < 0) { /* nothing half-built stays behind */
        a->nn = nn;
        a->nk = nk;
        a->nb = nb;
    }
    return v;
}

/* ---- comparison ---------------------------------------------------------- */

bool bv_equal(const bv_arena_t *a, int32_t x, const bv_arena_t *b, int32_t y)
{
    if (!live(a, x) || !live(b, y)) return false;
    const bv_node_t *p = &a->node[x], *q = &b->node[y];
    if (p->kind != q->kind) return false;
    switch (p->kind) {
    case BV_NOTHING:
        return true;
    case BV_INTEGER:
    case BV_TRUTH:
        return p->a == q->a;
    case BV_RATIO:
        return p->a == q->a && p->b == q->b;
    case BV_BYTES:
    case BV_TEXT:
    case BV_SYMBOL:
        return same_bytes(a, x, b, y);
    case BV_LIST:
    case BV_PAIR:
    case BV_RECORD: {
        if (p->len != q->len) return false;
        uint32_t k = p->len * (p->kind == BV_RECORD ? 2u : 1u);
        for (uint32_t i = 0; i < k; i++)
            if (!bv_equal(a, (int32_t) a->kid[p->a + i], b, (int32_t) b->kid[q->a + i]))
                return false;
        return true;
    }
    default:
        return false;
    }
}

int bv_witness(const bv_arena_t *a, int32_t x, const bv_arena_t *b, int32_t y)
{
    if (!live(a, x) || !live(b, y)) return BV_NEITHER;
    return bv_equal(a, x, b, y) ? BV_TRUE : BV_BOTH;
}

const char *bv_status_name(int32_t s)
{
    switch (s) {
    case BV_OK:
        return "OK";
    case BV_E_LIMIT:
        return "LIMIT";
    case BV_E_DOMAIN:
        return "DOMAIN";
    case BV_E_TEXT:
        return "TEXT";
    case BV_E_NAME:
        return "NAME";
    case BV_E_FORM:
        return "FORM";
    case BV_E_OVERFLOW:
        return "OVERFLOW";
    case BV_E_HANDLE:
        return "HANDLE";
    default:
        return s >= 0 ? "OK" : "?";
    }
}
