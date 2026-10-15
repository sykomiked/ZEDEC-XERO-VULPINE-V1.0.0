/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_search.c — see zxv_search.h */
#include "zxv_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPACES    4u      /* indexes kept at once; the least used goes */
#define DEPTH_MAX 32u     /* folders deep */
#define HASH_MAX  65536u  /* distinct words and prefixes in one file */
#define PEND_MAX  262144u /* gathered before they are merged */

typedef struct {
    char *path;
    uint64_t size;
    int64_t mtime;
    uint64_t *h; /* sorted, distinct */
    uint16_t *c; /* how often each occurs, at most 65535 */
    uint32_t nh; /* 0 when the contents are not indexed */
    bool text;
    bool moved;    /* during a refresh: old, now owned by the new index; new, from the old */
    bool replaced; /* during a refresh: old, read again into a new record */
} rec_t;

typedef struct {
    bool live;
    char space[ZXF_SPACE_ID_MAX + 1];
    rec_t *r; /* sorted by path */
    uint32_t n;
    uint64_t used;
} index_t;

static index_t IDX[SPACES];
static uint64_t TICK;

static void rec_free(rec_t *r)
{
    free(r->path);
    free(r->h);
    free(r->c);
    memset(r, 0, sizeof *r);
}

static void index_free(index_t *x)
{
    for (uint32_t i = 0; i < x->n; i++) rec_free(&x->r[i]);
    free(x->r);
    memset(x, 0, sizeof *x);
}

void zsr_reset(void)
{
    for (uint32_t i = 0; i < SPACES; i++) index_free(&IDX[i]);
    TICK = 0;
}

/* the index of the space in use; with make, a new one (the least used goes) */
static index_t *index_of(bool make)
{
    const char *sp = zxf_space();
    index_t *old = &IDX[0];
    for (uint32_t i = 0; i < SPACES; i++)
        if (IDX[i].live && !strcmp(IDX[i].space, sp)) {
            IDX[i].used = ++TICK;
            return &IDX[i];
        }
    if (!make) return NULL;
    for (uint32_t i = 1; i < SPACES; i++) /* a free one, else the least used */
        if (old->live && (!IDX[i].live || IDX[i].used < old->used)) old = &IDX[i];
    index_free(old);
    old->live = true;
    snprintf(old->space, sizeof old->space, "%s", sp);
    old->used = ++TICK;
    return old;
}

/* ===================== words ===================== */

static bool wordch(uint8_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

static uint8_t lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint8_t) (c + 32) : c;
}

#define FNV_BASIS 14695981039346656037ull
#define FNV_PRIME 1099511628211ull

typedef struct {
    uint64_t h;
    uint32_t c;
} pair_t;

typedef struct {
    uint8_t w[ZSR_WORD_MAX];
    uint32_t len;
    bool over; /* longer than ZSR_WORD_MAX */
    pair_t *p;
    uint32_t n, cap;
    bool full; /* over HASH_MAX distinct: contents not indexed */
} words_t;

static int pair_cmp(const void *a, const void *b)
{
    uint64_t x = ((const pair_t *) a)->h, y = ((const pair_t *) b)->h;
    return x < y ? -1 : x > y;
}

/* sort and merge: each hash once, with its count */
static void merge(words_t *t)
{
    if (!t->n) return;
    qsort(t->p, t->n, sizeof *t->p, pair_cmp);
    uint32_t k = 0;
    for (uint32_t i = 1; i < t->n; i++)
        if (t->p[i].h == t->p[k].h)
            t->p[k].c += t->p[i].c;
        else
            t->p[++k] = t->p[i];
    t->n = k + 1;
    if (t->n > HASH_MAX) t->full = true;
}

static void add(words_t *t, uint64_t h)
{
    if (t->full) return;
    if (t->n == t->cap) {
        if (t->cap >= PEND_MAX) {
            merge(t);
            if (t->full || t->n * 2u > t->cap) {
                t->full = true;
                return;
            }
        } else {
            uint32_t nc = t->cap ? t->cap * 2u : 1024u;
            pair_t *np = realloc(t->p, nc * sizeof *np);
            if (!np) {
                t->full = true;
                return;
            }
            t->p = np;
            t->cap = nc;
        }
    }
    t->p[t->n].h = h;
    t->p[t->n++].c = 1;
}

/* the word just read: each prefix from 2 bytes to ZSR_PREFIX_MAX, and the
 * whole word when it is longer than that (S3, S4) */
static void word_end(words_t *t)
{
    if (t->len >= 2) {
        uint64_t h = FNV_BASIS;
        for (uint32_t i = 0; i < t->len; i++) {
            h = (h ^ t->w[i]) * FNV_PRIME;
            if (i + 1 >= 2 && (i + 1 <= ZSR_PREFIX_MAX || (i + 1 == t->len && !t->over))) add(t, h);
        }
    }
    t->len = 0;
    t->over = false;
}

static void feed(words_t *t, const uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!wordch(d[i])) {
            word_end(t);
            continue;
        }
        if (t->len < ZSR_WORD_MAX)
            t->w[t->len++] = lower(d[i]);
        else
            t->over = true;
    }
}

/* ===================== one file ===================== */

static uint8_t BUF[ZXF_READ_MAX];

/* read and index a file's contents (S3); false leaves it name only */
static bool index_text(const char *path, rec_t *r)
{
    static words_t t;
    memset(&t, 0, sizeof t);
    bool ok = true;
    uint64_t off = 0, size = 0;
    for (;;) {
        uint32_t got = 0, want = ZXF_READ_MAX;
        if (ZSR_TEXT_MAX - off < want) want = (uint32_t) (ZSR_TEXT_MAX - off);
        if (zxf_read(path, off, BUF, want, true, &got, &size) != ZXF_OK || memchr(BUF, 0, got)) {
            ok = false;
            break;
        }
        feed(&t, BUF, got);
        off += got;
        if (!got || off >= size || off >= ZSR_TEXT_MAX) break;
    }
    word_end(&t);
    merge(&t);
    if (ok && !t.full && t.n) {
        r->h = malloc(t.n * sizeof *r->h);
        r->c = malloc(t.n * sizeof *r->c);
        if (r->h && r->c) {
            for (uint32_t i = 0; i < t.n; i++) {
                r->h[i] = t.p[i].h;
                r->c[i] = t.p[i].c > 65535u ? 65535u : (uint16_t) t.p[i].c;
            }
            r->nh = t.n;
        } else {
            free(r->h);
            free(r->c);
            r->h = NULL;
            r->c = NULL;
            ok = false;
        }
    }
    free(t.p);
    r->text = ok && !t.full;
    return r->text;
}

/* ===================== the walk ===================== */

typedef struct {
    index_t *old;
    rec_t *r;
    uint32_t n, cap;
    zsr_stats_t *st;
} walk_t;

static rec_t *find(index_t *x, const char *path)
{
    uint32_t lo = 0, hi = x->n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = strcmp(x->r[mid].path, path);
        if (!c) return &x->r[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

static int file(walk_t *w, const char *path, const zxf_entry_t *e)
{
    if (w->n == ZSR_FILES_MAX) {
        w->st->left_out++;
        return 0;
    }
    if (w->n == w->cap) {
        uint32_t nc = w->cap ? w->cap * 2u : 256u;
        rec_t *np = realloc(w->r, nc * sizeof *np);
        if (!np) return ZXF_E_IO;
        w->r = np;
        w->cap = nc;
    }
    rec_t *o = find(w->old, path), *r = &w->r[w->n];
    memset(r, 0, sizeof *r);
    if (o && o->size == e->size && o->mtime == e->mtime) {
        *r = *o; /* unchanged: the new index takes it over */
        o->moved = r->moved = true;
    } else {
        r->path = malloc(strlen(path) + 1);
        if (!r->path) return ZXF_E_IO;
        strcpy(r->path, path);
        r->size = e->size;
        r->mtime = e->mtime;
        index_text(path, r);
        if (o) {
            o->replaced = true;
            w->st->changed++;
        } else
            w->st->added++;
    }
    w->n++;
    return 0;
}

static int walk(walk_t *w, const char *dir, uint32_t depth)
{
    zxf_entry_t *v;
    uint32_t n;
    int rc = zxf_list(dir, &v, &n);
    if (rc) return rc;
    static char path[DEPTH_MAX + 1][ZXF_PATH_MAX + 1];
    for (uint32_t i = 0; i < n && !rc; i++) {
        char *p = path[depth];
        int k = snprintf(p, ZXF_PATH_MAX + 1, "%s%s%s", dir, dir[0] ? "/" : "", v[i].name);
        if (k < 0 || (size_t) k > ZXF_PATH_MAX) {
            w->st->left_out++;
            continue;
        }
        if (!v[i].dir)
            rc = file(w, p, &v[i]);
        else if (depth + 1 >= DEPTH_MAX)
            w->st->left_out++;
        else {
            rc = walk(w, p, depth + 1);
            if (rc == ZXF_E_NOTFOUND || rc == ZXF_E_PATH) rc = 0; /* gone, or a link */
        }
    }
    free(v);
    return rc;
}

static int rec_cmp(const void *a, const void *b)
{
    return strcmp(((const rec_t *) a)->path, ((const rec_t *) b)->path);
}

int zsr_refresh(zsr_stats_t *st)
{
    static zsr_stats_t dummy;
    if (!st) st = &dummy;
    memset(st, 0, sizeof *st);
    index_t *x = index_of(true);
    walk_t w = {x, NULL, 0, 0, st};
    int rc = walk(&w, "", 0);
    if (rc) { /* keep the old index whole */
        for (uint32_t i = 0; i < w.n; i++)
            if (!w.r[i].moved) rec_free(&w.r[i]);
        for (uint32_t i = 0; i < x->n; i++) x->r[i].moved = x->r[i].replaced = false;
        free(w.r);
        memset(st, 0, sizeof *st);
        return rc;
    }
    for (uint32_t i = 0; i < x->n; i++)
        if (!x->r[i].moved) {
            st->removed += !x->r[i].replaced;
            rec_free(&x->r[i]);
        }
    for (uint32_t i = 0; i < w.n; i++) w.r[i].moved = false;
    free(x->r);
    if (w.n) qsort(w.r, w.n, sizeof *w.r, rec_cmp);
    x->r = w.r;
    x->n = w.n;
    st->files = w.n;
    for (uint32_t i = 0; i < w.n; i++) st->text_files += w.r[i].text;
    return 0;
}

/* ===================== queries ===================== */

typedef struct {
    char s[ZSR_QUERY_MAX + 1];
    uint32_t len;
    uint64_t h;
    bool hashed; /* 2 bytes or more: can match contents */
} term_t;

static uint32_t parse(const char *q, term_t *t)
{
    uint32_t n = 0;
    for (const uint8_t *p = (const uint8_t *) q; *p;) {
        if (!wordch(*p)) {
            p++;
            continue;
        }
        if (n == ZSR_TERMS_MAX) return ZSR_TERMS_MAX + 1;
        term_t *x = &t[n++];
        x->len = 0;
        while (*p && wordch(*p)) x->s[x->len++] = (char) lower(*p++);
        x->s[x->len] = 0;
        /* the hash the index holds for a word starting with this term */
        uint32_t upto = x->len <= ZSR_WORD_MAX ? x->len : ZSR_PREFIX_MAX;
        x->h = FNV_BASIS;
        for (uint32_t i = 0; i < upto; i++) x->h = (x->h ^ (uint8_t) x->s[i]) * FNV_PRIME;
        x->hashed = x->len >= 2;
    }
    return n;
}

/* ASCII-folded substring */
static bool has(const char *hay, size_t hn, const char *needle, size_t nn)
{
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t k = 0;
        while (k < nn && lower((uint8_t) hay[i + k]) == (uint8_t) needle[k]) k++;
        if (k == nn) return true;
    }
    return false;
}

static uint32_t count_of(const rec_t *r, uint64_t h)
{
    uint32_t lo = 0, hi = r->nh;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (r->h[mid] == h) return r->c[mid];
        if (r->h[mid] < h)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

/* the text around the first place `t` starts a word, made safe to show */
static void snippet(const char *path, const term_t *t, char *out)
{
    out[0] = 0;
    uint64_t off = 0, size = 0;
    while (off < ZSR_TEXT_MAX) {
        uint32_t got = 0;
        if (zxf_read(path, off, BUF, ZXF_READ_MAX, true, &got, &size) != ZXF_OK || !got) return;
        for (uint32_t i = 0; i + t->len <= got; i++) {
            if (i && wordch(BUF[i - 1])) continue;
            uint32_t k = 0;
            while (k < t->len && lower(BUF[i + k]) == (uint8_t) t->s[k]) k++;
            if (k < t->len) continue;
            uint32_t a = i > 40 ? i - 40 : 0;
            while (a < i && (BUF[a] & 0xC0) == 0x80) a++; /* start on a character */
            uint32_t b = a + ZSR_SNIPPET < got ? a + ZSR_SNIPPET : got;
            if (b < got)
                while (b > a && (BUF[b] & 0xC0) == 0x80) b--; /* end before one */
            for (uint32_t j = a; j < b; j++)
                out[j - a] = BUF[j] < 0x20 || BUF[j] == 0x7f ? ' ' : (char) BUF[j];
            out[b - a] = 0;
            return;
        }
        off += got;
        if (off >= size) return;
        if (got > t->len) off -= t->len; /* a word across the cut */
    }
}

int zsr_query(const char *q, zsr_hit_t *hits, uint32_t cap, uint32_t *n, uint32_t *total)
{
    static term_t t[ZSR_TERMS_MAX];
    static const rec_t *src[ZSR_HITS_MAX];
    static int8_t tt[ZSR_HITS_MAX];
    *n = 0;
    *total = 0;
    if (cap > ZSR_HITS_MAX) cap = ZSR_HITS_MAX;
    if (strlen(q) > ZSR_QUERY_MAX) return ZXF_E_PATH;
    uint32_t nt = parse(q, t);
    if (!nt || nt > ZSR_TERMS_MAX) return ZXF_E_PATH;
    index_t *x = index_of(false);
    if (!x) return 0;
    for (uint32_t i = 0; i < x->n; i++) {
        const rec_t *r = &x->r[i];
        const char *base = strrchr(r->path, '/');
        base = base ? base + 1 : r->path;
        size_t bl = strlen(base), dl = (size_t) (base - r->path);
        uint32_t score = 0, in_base = 0;
        int text_term = -1;
        bool all = true;
        for (uint32_t k = 0; k < nt && all; k++) {
            uint32_t c = t[k].hashed && r->nh ? count_of(r, t[k].h) : 0;
            if (has(base, bl, t[k].s, t[k].len)) {
                score += 1000;
                in_base++;
            } else if (dl && has(r->path, dl, t[k].s, t[k].len))
                score += 100;
            else if (!c)
                all = false;
            if (c) {
                score += c > 99 ? 99 : c;
                if (text_term < 0) text_term = (int) k;
            }
        }
        if (!all) continue;
        (*total)++;
        /* keep the best cap, best first; ties by path (the index order) */
        uint32_t at = *n;
        while (at > 0 && hits[at - 1].score < score) at--;
        if (at >= cap) continue;
        uint32_t last = *n < cap ? *n : cap - 1;
        memmove(&hits[at + 1], &hits[at], (last - at) * sizeof *hits);
        memmove(&src[at + 1], &src[at], (last - at) * sizeof *src);
        memmove(&tt[at + 1], &tt[at], (last - at) * sizeof *tt);
        if (*n < cap) (*n)++;
        src[at] = r;
        tt[at] = (int8_t) text_term;
        zsr_hit_t *h = &hits[at];
        snprintf(h->path, sizeof h->path, "%s", r->path);
        h->size = r->size;
        h->mtime = r->mtime;
        h->score = score;
        h->in_name = in_base == nt;
    }
    for (uint32_t i = 0; i < *n; i++) { /* only for the hits kept */
        hits[i].snippet[0] = 0;
        if (tt[i] >= 0) snippet(src[i]->path, &t[tt[i]], hits[i].snippet);
    }
    return 0;
}
