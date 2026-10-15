/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This module shows an agreement and records that a person
 * accepted it. It does not provide legal advice, make legal determinations,
 * or decide whether an agreement is enforceable anywhere. The agreement text
 * itself is prepared and reviewed outside the code.
 */
/* terms.c — see terms.h. */
#include "terms.h"
#include "sha256.h"

#include <string.h>

/* ---------------------------------------------------------------- UTF-8 */

/* Decode one codepoint at s[i]; returns its byte length, or 0 if the bytes
 * there are not well-formed UTF-8 (overlong, surrogate, > U+10FFFF, cut). */
static size_t utf8_next(const char *s, size_t n, size_t i, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *) s;
    unsigned char c = u[i];
    size_t len, k;
    uint32_t v, min;

    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
        v = c & 0x1Fu;
        min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        v = c & 0x0Fu;
        min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        v = c & 0x07u;
        min = 0x10000;
    } else
        return 0;
    if (len > n - i) return 0;
    for (k = 1; k < len; k++) {
        if ((u[i + k] & 0xC0u) != 0x80u) return 0;
        v = (v << 6) | (u[i + k] & 0x3Fu);
    }
    if (v < min || v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) return 0;
    *cp = v;
    return len;
}

int terms_utf8_valid(const char *s, size_t n)
{
    size_t i = 0;
    uint32_t cp;
    if (!s && n) return 0;
    while (i < n) {
        size_t l = utf8_next(s, n, i, &cp);
        if (!l) return 0;
        i += l;
    }
    return 1;
}

static int in(uint32_t cp, uint32_t lo, uint32_t hi)
{
    return cp >= lo && cp <= hi;
}

int terms_cp_width(uint32_t cp)
{
    if (in(cp, 0x0300, 0x036F) || in(cp, 0x1AB0, 0x1AFF) || in(cp, 0x1DC0, 0x1DFF) ||
        in(cp, 0x200B, 0x200F) || in(cp, 0x20D0, 0x20FF) || in(cp, 0xFE00, 0xFE0F) ||
        in(cp, 0xFE20, 0xFE2F) || in(cp, 0x0610, 0x061A) || in(cp, 0x064B, 0x065F) ||
        in(cp, 0x093C, 0x093C) || in(cp, 0x0941, 0x0948) || in(cp, 0x094D, 0x094D))
        return 0;
    if (in(cp, 0x1100, 0x115F) || in(cp, 0x2E80, 0x303E) || in(cp, 0x3041, 0x33FF) ||
        in(cp, 0x3400, 0x4DBF) || in(cp, 0x4E00, 0x9FFF) || in(cp, 0xA000, 0xA4CF) ||
        in(cp, 0xAC00, 0xD7A3) || in(cp, 0xF900, 0xFAFF) || in(cp, 0xFE30, 0xFE4F) ||
        in(cp, 0xFF00, 0xFF60) || in(cp, 0xFFE0, 0xFFE6) || in(cp, 0x1F300, 0x1F64F) ||
        in(cp, 0x1F900, 0x1F9FF) || in(cp, 0x20000, 0x3FFFD))
        return 2;
    return 1;
}

/* Scripts written without spaces may break between any two characters. */
static int breaks_anywhere(uint32_t cp)
{
    return terms_cp_width(cp) == 2 && !in(cp, 0x1F300, 0x1F9FF);
}

void terms_hash(const terms_doc_t *doc, uint8_t out[TERMS_HASH_LEN])
{
    sha256((const uint8_t *) doc->text, doc->len, out);
}

static int doc_matches(const terms_doc_t *doc)
{
    uint8_t h[TERMS_HASH_LEN];
    uint8_t diff = 0;
    size_t i;
    if (!doc->sha256) return 1;
    terms_hash(doc, h);
    for (i = 0; i < TERMS_HASH_LEN; i++) diff |= (uint8_t) (h[i] ^ doc->sha256[i]);
    return diff == 0;
}

int terms_doc_check(const terms_doc_t *doc)
{
    if (!doc || !doc->text) return TERMS_ERR_ARG;
    if (!doc_matches(doc)) return TERMS_ERR_TEXT;
    if (doc->reference && (!doc->reference->text || !doc_matches(doc->reference)))
        return TERMS_ERR_TEXT;
    return TERMS_OK;
}

/* ------------------------------------------------------------- wrapping */

static const char KEY_MARK[] = " [KEY]";
#define KEY_MARK_LEN (sizeof KEY_MARK - 1)

static uint16_t cp_w(const terms_reader_t *r, uint32_t cp)
{
    return r->measure ? r->measure(r->measure_ctx, cp) : (uint16_t) terms_cp_width(cp);
}

/* "12. TITLE": up to three digits, ". ", then an ASCII capital. */
static int heading_no(const char *p, size_t n, uint16_t *no)
{
    size_t i = 0;
    unsigned v = 0;
    while (i < n && i < 3 && p[i] >= '0' && p[i] <= '9') v = v * 10 + (unsigned) (p[i++] - '0');
    if (i == 0 || i + 2 >= n || p[i] != '.' || p[i + 1] != ' ') return 0;
    /* ASCII capital, or any non-ASCII letter for scripts without case */
    if ((p[i + 2] < 'A' || p[i + 2] > 'Z') && !((unsigned char) p[i + 2] & 0x80u)) return 0;
    *no = (uint16_t) v;
    return 1;
}

static uint16_t span_width(const terms_reader_t *r, size_t a, size_t b)
{
    const char *t = r->doc->text;
    uint32_t w = 0, cp;
    while (a < b) {
        size_t l = utf8_next(t, b, a, &cp);
        if (!l) break;
        w += cp_w(r, cp);
        a += l;
    }
    return (uint16_t) (w > 0xFFFF ? 0xFFFF : w);
}

static int emit(terms_reader_t *r, size_t a, size_t b, uint8_t kind, uint8_t key, uint16_t section)
{
    terms_line_t *ln;
    if (r->nlines >= TERMS_MAX_LINES) return TERMS_ERR_FULL;
    if (b - a > 0xFFFF) return TERMS_ERR_FULL;
    while (b > a && r->doc->text[b - 1] == ' ') b--; /* a break leaves no trailing space */
    ln = &r->lines[r->nlines++];
    ln->off = (uint32_t) a;
    ln->len = (uint16_t) (b - a);
    ln->cols = span_width(r, a, b);
    ln->kind = kind;
    ln->key = key;
    ln->section = section;
    return TERMS_OK;
}

/* Wrap paragraph text[p, e) to r->cols. */
static int wrap_para(terms_reader_t *r, size_t p, size_t e, uint8_t kind, uint8_t key,
                     uint16_t section)
{
    const char *t = r->doc->text;
    size_t i = p, s = p;
    uint32_t w = 0, cp;
    size_t brk_end = 0, brk_next = 0;
    int have_brk = 0, rc;

    while (i < e) {
        size_t l = utf8_next(t, e, i, &cp);
        uint16_t cw = cp_w(r, cp);
        if (cp == ' ') {
            brk_end = i;
            brk_next = i + 1;
            have_brk = 1;
        } else if (breaks_anywhere(cp) && w > 0) {
            brk_end = i;
            brk_next = i;
            have_brk = 1;
        }
        if (cp != ' ' && w > 0 && w + cw > r->cols) {
            if (have_brk && brk_end > s) {
                rc = emit(r, s, brk_end, kind, key, section);
                i = brk_next;
            } else {
                rc = emit(r, s, i, kind, key, section);
            }
            if (rc) return rc;
            while (i < e && t[i] == ' ') i++;
            s = i;
            w = 0;
            have_brk = 0;
            continue;
        }
        w += cw;
        i += l;
        if (cp == '-' || cp == '/' || breaks_anywhere(cp)) {
            brk_end = i;
            brk_next = i;
            have_brk = 1;
        }
    }
    while (e > s && t[e - 1] == ' ') e--;
    return emit(r, s, e, kind, key, section);
}

static int rewrap(terms_reader_t *r)
{
    const char *t = r->doc->text;
    size_t n = r->doc->len, p = 0;
    uint16_t section = 0;
    uint8_t key = 0, titled = 0;
    int rc;

    r->nlines = 0;
    r->nsections = 0;
    if (n && t[n - 1] == '\n') n--; /* a final newline ends the text */
    while (p <= n) {
        size_t e = p, ce;
        uint16_t no;
        uint8_t kind = TERMS_LINE_BODY, hkey = key;
        while (e < n && t[e] != '\n') e++;
        ce = e;
        if (e > p && t[e - 1] == '\r') ce = e - 1;
        if (ce == p) {
            rc = emit(r, p, p, TERMS_LINE_BLANK, key, section);
        } else {
            if (!titled) {
                kind = TERMS_LINE_TITLE;
                titled = 1;
            } else if (heading_no(t + p, ce - p, &no)) {
                kind = TERMS_LINE_HEADING;
                hkey = (ce - p > KEY_MARK_LEN &&
                        !memcmp(t + ce - KEY_MARK_LEN, KEY_MARK, KEY_MARK_LEN));
                if (hkey) ce -= KEY_MARK_LEN;
                section = no;
                key = hkey;
                if (r->nsections >= TERMS_MAX_SECTIONS) return TERMS_ERR_FULL;
                r->section_no[r->nsections] = no;
                r->section_line[r->nsections] = r->nlines;
                r->section_key[r->nsections] = key;
                r->nsections++;
            }
            rc = wrap_para(r, p, ce, kind, key, section);
        }
        if (rc) return rc;
        p = e + 1;
    }
    return TERMS_OK;
}

/* ---------------------------------------------------------------- reading */

static uint32_t max_top(const terms_reader_t *r)
{
    return r->nlines > r->rows ? r->nlines - r->rows : 0;
}

static uint32_t line_end(const terms_reader_t *r, uint32_t i)
{
    return r->lines[i].off + r->lines[i].len;
}

/* Everything from top to the bottom of the screen has now been shown. */
static void note_seen(terms_reader_t *r, unsigned via)
{
    uint32_t last;
    if (!r->nlines) return;
    last = r->top + r->rows;
    if (last > r->nlines) last = r->nlines;
    last--;
    if (line_end(r, last) > r->seen) r->seen = line_end(r, last);
    if (!r->reached_end && last == r->nlines - 1) {
        r->reached_end = 1;
        r->via = (uint8_t) via;
    }
}

int terms_reader_init_ex(terms_reader_t *r, const terms_doc_t *doc, uint16_t width, uint16_t rows,
                         terms_measure_fn measure, void *ctx)
{
    int rc;
    if (!r) return TERMS_ERR_ARG;
    memset(r, 0, sizeof *r); /* a failed init leaves nothing to accept */
    if (!doc || !doc->text || !doc->len || !rows || !width) return TERMS_ERR_ARG;
    if (!measure && width < TERMS_MIN_COLS) return TERMS_ERR_ARG;
    if (doc->len > 0xFFFFFFFFu) return TERMS_ERR_ARG;
    if (!terms_utf8_valid(doc->text, doc->len)) return TERMS_ERR_UTF8;
    if (terms_doc_check(doc)) return TERMS_ERR_TEXT;
    r->doc = doc;
    r->measure = measure;
    r->measure_ctx = ctx;
    r->cols = width;
    r->rows = rows;
    rc = rewrap(r);
    if (rc) {
        memset(r, 0, sizeof *r);
        return rc;
    }
    note_seen(r, TERMS_VIA_FIT);
    return TERMS_OK;
}

int terms_reader_init(terms_reader_t *r, const terms_doc_t *doc, uint16_t cols, uint16_t rows)
{
    return terms_reader_init_ex(r, doc, cols, rows, 0, 0);
}

int terms_reader_resize(terms_reader_t *r, uint16_t cols, uint16_t rows)
{
    uint32_t anchor, i;
    uint16_t oc = r->cols, orow = r->rows;
    int rc;
    if (!r || !r->doc || !rows || !cols) return TERMS_ERR_ARG;
    if (!r->measure && cols < TERMS_MIN_COLS) return TERMS_ERR_ARG;
    anchor = r->nlines ? r->lines[r->top].off : 0;
    r->cols = cols;
    r->rows = rows;
    rc = rewrap(r);
    if (rc) { /* keep the old layout usable */
        r->cols = oc;
        r->rows = orow;
        rewrap(r);
        return rc;
    }
    r->top = 0;
    for (i = 0; i < r->nlines && r->lines[i].off <= anchor; i++) r->top = i;
    if (r->top > max_top(r)) r->top = max_top(r);
    note_seen(r, TERMS_VIA_FIT);
    return TERMS_OK;
}

void terms_scroll(terms_reader_t *r, int32_t lines, unsigned via)
{
    int64_t t;
    if (!r || !r->nlines) return;
    t = (int64_t) r->top + lines;
    if (t < 0) t = 0;
    if (t > (int64_t) max_top(r)) t = max_top(r);
    r->top = (uint32_t) t;
    note_seen(r, via);
}

void terms_page(terms_reader_t *r, int dir)
{
    int32_t step;
    if (!r) return;
    step = r->rows > 1 ? r->rows - 1 : 1; /* keep one line of context */
    terms_scroll(r, dir < 0 ? -step : step, TERMS_VIA_KEYBOARD);
}

void terms_home(terms_reader_t *r)
{
    if (!r) return;
    r->top = 0;
    note_seen(r, TERMS_VIA_KEYBOARD);
}

void terms_end(terms_reader_t *r)
{
    if (!r) return;
    r->top = max_top(r);
    note_seen(r, TERMS_VIA_KEYBOARD);
}

int terms_goto_section(terms_reader_t *r, uint16_t no)
{
    uint16_t k;
    if (!r) return TERMS_ERR_ARG;
    for (k = 0; k < r->nsections; k++) {
        if (r->section_no[k] == no) {
            r->top = r->section_line[k];
            if (r->top > max_top(r)) r->top = max_top(r);
            note_seen(r, TERMS_VIA_SECTION);
            return TERMS_OK;
        }
    }
    return TERMS_ERR_ARG;
}

void terms_reader_spoke(terms_reader_t *r, uint32_t off)
{
    uint32_t i, at = 0;
    if (!r || !r->nlines) return;
    for (i = 0; i < r->nlines && r->lines[i].off <= off; i++) at = i;
    if (at < r->top || at >= r->top + r->rows) { /* follow the voice */
        r->top = at > max_top(r) ? max_top(r) : at;
        note_seen(r, TERMS_VIA_READER);
    }
    if (off > r->seen) r->seen = off;
    if (!r->reached_end && off >= line_end(r, r->nlines - 1)) {
        r->reached_end = 1;
        r->via = TERMS_VIA_READER;
    }
}

void terms_decline(terms_reader_t *r)
{
    if (r) r->declined = 1;
}

int terms_can_accept(const terms_reader_t *r)
{
    return r && r->nlines && r->reached_end && !r->declined;
}

terms_view_t terms_view(const terms_reader_t *r)
{
    terms_view_t v;
    uint32_t mt;
    memset(&v, 0, sizeof v);
    if (!r || !r->nlines) return v;
    mt = max_top(r);
    v.first = r->top;
    v.count = r->nlines - r->top < r->rows ? r->nlines - r->top : r->rows;
    if (r->nlines <= r->rows) {
        v.thumb256 = 256;
    } else {
        v.thumb256 = (uint16_t) ((uint64_t) r->rows * 256u / r->nlines);
        if (!v.thumb256) v.thumb256 = 1;
    }
    v.scroll256 = mt ? (uint16_t) ((uint64_t) r->top * 256u / mt) : 0;
    v.can_accept = (uint8_t) terms_can_accept(r);
    v.hint = r->declined      ? TERMS_HINT_DECLINED
             : r->reached_end ? TERMS_HINT_READY
                              : TERMS_HINT_READ;
    return v;
}

size_t terms_line_text(const terms_reader_t *r, uint32_t i, char *out, size_t cap)
{
    size_t n;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!r || i >= r->nlines) return 0;
    n = r->lines[i].len;
    if (n >= cap) { /* never cut a codepoint */
        n = cap - 1;
        while (n && ((unsigned char) r->doc->text[r->lines[i].off + n] & 0xC0u) == 0x80u) n--;
    }
    memcpy(out, r->doc->text + r->lines[i].off, n);
    out[n] = 0;
    return n;
}

static size_t put(char *out, size_t cap, size_t at, const char *s, size_t n)
{
    if (at < cap) {
        size_t room = cap - 1 - at;
        memcpy(out + at, s, n < room ? n : room);
        out[at + (n < room ? n : room)] = 0;
    }
    return at + n;
}

static const char *const HINTS[] = {"Read to the end to continue.", "You've reached the end.",
                                    "You declined. Nothing was recorded."};

size_t terms_render_text(const terms_reader_t *r, char *out, size_t cap)
{
    terms_view_t v = terms_view(r);
    size_t at = 0;
    uint32_t i;
    const char *h;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!r || !r->nlines) return 0;
    for (i = v.first; i < v.first + v.count; i++) {
        const terms_line_t *ln = &r->lines[i];
        if (ln->key && ln->kind != TERMS_LINE_BLANK) at = put(out, cap, at, "\xE2\x96\x8C ", 4);
        at = put(out, cap, at, r->doc->text + ln->off, ln->len);
        at = put(out, cap, at, "\n", 1);
    }
    h = HINTS[v.hint];
    at = put(out, cap, at, "\n", 1);
    at = put(out, cap, at, h, strlen(h));
    at = put(out, cap, at, "\n", 1);
    if (v.can_accept)
        at = put(out, cap, at, "[ Decline ]   [ I agree ]\n", 26);
    else
        at = put(out, cap, at, "[ Decline ]   ( I agree )\n", 26);
    return at;
}

/* ---------------------------------------------------------------- records */

static int tag_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
}

static int tag_valid(const char *s, size_t cap)
{
    size_t i;
    if (!s || !s[0]) return 0;
    for (i = 0; s[i]; i++)
        if (i + 1 >= cap || !tag_char(s[i])) return 0;
    return 1;
}

int terms_seat_valid(const char *seat)
{
    return tag_valid(seat, TERMS_SEAT_MAX);
}

static const char HEX[] = "0123456789abcdef";

static size_t put_hex(char *out, size_t at, const uint8_t *h)
{
    int k;
    for (k = 0; k < TERMS_HASH_LEN; k++) {
        out[at++] = HEX[h[k] >> 4];
        out[at++] = HEX[h[k] & 15];
    }
    return at;
}

static size_t put_str(char *out, size_t at, const char *s)
{
    size_t n = strlen(s);
    memcpy(out + at, s, n);
    return at + n;
}

static size_t put_u64(char *out, size_t at, uint64_t v)
{
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) out[at++] = tmp[--n];
    return at;
}

/* Everything before " rec=": what the record's own hash covers. Buffer must
 * hold TERMS_RECORD_MAX. */
static size_t record_prefix(const terms_record_t *rec, char *b)
{
    size_t at = 0;
    at = put_str(b, at, "ZXVTERMS1 seat=");
    at = put_str(b, at, rec->seat);
    at = put_str(b, at, " ver=");
    at = put_str(b, at, rec->version);
    at = put_str(b, at, " lang=");
    at = put_str(b, at, rec->lang);
    at = put_str(b, at, rec->controlling ? " ctl=1" : " ctl=0");
    at = put_str(b, at, " via=");
    at = put_u64(b, at, rec->via);
    at = put_str(b, at, " t=");
    at = put_u64(b, at, rec->time);
    at = put_str(b, at, " shown=");
    at = put_hex(b, at, rec->shown);
    at = put_str(b, at, " ref=");
    at = put_hex(b, at, rec->ref);
    at = put_str(b, at, " prev=");
    at = put_hex(b, at, rec->prev);
    return at;
}

static int record_fields_valid(const terms_record_t *rec)
{
    return terms_seat_valid(rec->seat) && tag_valid(rec->version, TERMS_TAG_MAX) &&
           tag_valid(rec->lang, TERMS_TAG_MAX) && rec->controlling <= 1 && rec->via <= 31;
}

int terms_accept(const terms_reader_t *r, const char *seat, uint64_t time,
                 const uint8_t prev[TERMS_HASH_LEN], terms_record_t *rec)
{
    char b[TERMS_RECORD_MAX];
    size_t n;
    const terms_doc_t *d;
    if (!r || !rec || !r->doc) return TERMS_ERR_ARG;
    if (!terms_can_accept(r)) return TERMS_ERR_GATE;
    d = r->doc;
    if (terms_doc_check(d)) return TERMS_ERR_TEXT;
    if (!terms_seat_valid(seat) || !tag_valid(d->version, TERMS_TAG_MAX) ||
        !tag_valid(d->lang, TERMS_TAG_MAX))
        return TERMS_ERR_ARG;
    memset(rec, 0, sizeof *rec);
    memcpy(rec->seat, seat, strlen(seat) + 1);
    memcpy(rec->version, d->version, strlen(d->version) + 1);
    memcpy(rec->lang, d->lang, strlen(d->lang) + 1);
    terms_hash(d, rec->shown);
    if (d->reference)
        terms_hash(d->reference, rec->ref);
    else
        memcpy(rec->ref, rec->shown, TERMS_HASH_LEN);
    rec->controlling = (uint8_t) (d->controlling ? 1 : 0);
    rec->via = r->via;
    rec->time = time;
    if (prev) memcpy(rec->prev, prev, TERMS_HASH_LEN);
    n = record_prefix(rec, b);
    sha256((const uint8_t *) b, n, rec->self);
    return TERMS_OK;
}

size_t terms_record_format(const terms_record_t *rec, char *out, size_t cap)
{
    char b[TERMS_RECORD_MAX];
    size_t n;
    if (!rec || !out || !record_fields_valid(rec)) return 0;
    n = record_prefix(rec, b);
    n = put_str(b, n, " rec=");
    n = put_hex(b, n, rec->self);
    b[n++] = '\n';
    if (n + 1 > cap) return 0;
    memcpy(out, b, n);
    out[n] = 0;
    return n;
}

/* ---- parsing: strict, one canonical spelling per record */

typedef struct {
    const char *p, *e;
} cur_t;

static int lit(cur_t *c, const char *s)
{
    size_t n = strlen(s);
    if ((size_t) (c->e - c->p) < n || memcmp(c->p, s, n)) return 0;
    c->p += n;
    return 1;
}

static int tag_field(cur_t *c, char *dst, size_t cap)
{
    size_t n = 0;
    while (c->p + n < c->e && c->p[n] != ' ') {
        if (!tag_char(c->p[n]) || n + 1 >= cap) return 0;
        n++;
    }
    if (!n) return 0;
    memcpy(dst, c->p, n);
    dst[n] = 0;
    c->p += n;
    return 1;
}

static int u64_field(cur_t *c, uint64_t *v, uint64_t max)
{
    uint64_t x = 0;
    const char *s = c->p;
    while (c->p < c->e && *c->p >= '0' && *c->p <= '9') {
        unsigned d = (unsigned) (*c->p - '0');
        if (d > max || x > (max - d) / 10) return 0;
        x = x * 10 + d;
        c->p++;
    }
    if (c->p == s || (c->p - s > 1 && *s == '0')) return 0; /* no leading zeros */
    *v = x;
    return 1;
}

static int hex_field(cur_t *c, uint8_t *h)
{
    int k;
    if (c->e - c->p < 2 * TERMS_HASH_LEN) return 0;
    for (k = 0; k < 2 * TERMS_HASH_LEN; k++) {
        char ch = c->p[k];
        int v = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1;
        if (v < 0) return 0;
        if (k & 1)
            h[k / 2] = (uint8_t) (h[k / 2] | v);
        else
            h[k / 2] = (uint8_t) (v << 4);
    }
    c->p += 2 * TERMS_HASH_LEN;
    return 1;
}

int terms_record_parse(const char *line, size_t len, terms_record_t *rec)
{
    cur_t c;
    uint64_t v;
    uint8_t want[TERMS_HASH_LEN];
    const char *rec_at;
    if (!line || !rec) return TERMS_ERR_ARG;
    if (len && line[len - 1] == '\n') len--;
    c.p = line;
    c.e = line + len;
    memset(rec, 0, sizeof *rec);
    if (!lit(&c, "ZXVTERMS1 seat=") || !tag_field(&c, rec->seat, TERMS_SEAT_MAX) ||
        !lit(&c, " ver=") || !tag_field(&c, rec->version, TERMS_TAG_MAX) || !lit(&c, " lang=") ||
        !tag_field(&c, rec->lang, TERMS_TAG_MAX) || !lit(&c, " ctl=") || !u64_field(&c, &v, 1))
        return TERMS_ERR_PARSE;
    rec->controlling = (uint8_t) v;
    if (!lit(&c, " via=") || !u64_field(&c, &v, 31)) return TERMS_ERR_PARSE;
    rec->via = (uint8_t) v;
    if (!lit(&c, " t=") || !u64_field(&c, &rec->time, UINT64_MAX) || !lit(&c, " shown=") ||
        !hex_field(&c, rec->shown) || !lit(&c, " ref=") || !hex_field(&c, rec->ref) ||
        !lit(&c, " prev=") || !hex_field(&c, rec->prev))
        return TERMS_ERR_PARSE;
    rec_at = c.p;
    if (!lit(&c, " rec=") || !hex_field(&c, rec->self) || c.p != c.e) return TERMS_ERR_PARSE;
    sha256((const uint8_t *) line, (size_t) (rec_at - line), want);
    if (memcmp(want, rec->self, TERMS_HASH_LEN)) return TERMS_ERR_CHAIN;
    return TERMS_OK;
}

/* Walk the log line by line; calls `fn` for every record in order. */
static int walk(const char *log, size_t len, uint8_t tip[TERMS_HASH_LEN], uint32_t *bad_line,
                void (*fn)(void *, const terms_record_t *), void *ctx)
{
    size_t p = 0;
    uint32_t lineno = 0;
    int count = 0;
    uint8_t at[TERMS_HASH_LEN];
    terms_record_t rec;
    memset(at, 0, sizeof at);
    if (bad_line) *bad_line = 0;
    if (!log && len) return TERMS_ERR_ARG;
    while (p < len) {
        size_t e = p;
        int rc;
        while (e < len && log[e] != '\n') e++;
        lineno++;
        if (e > p) {
            rc = terms_record_parse(log + p, e - p, &rec);
            if (rc == TERMS_OK && memcmp(rec.prev, at, TERMS_HASH_LEN)) rc = TERMS_ERR_CHAIN;
            if (rc) {
                if (bad_line) *bad_line = lineno;
                return rc;
            }
            memcpy(at, rec.self, TERMS_HASH_LEN);
            if (fn) fn(ctx, &rec);
            count++;
        }
        p = e + 1;
    }
    if (tip) memcpy(tip, at, TERMS_HASH_LEN);
    return count;
}

int terms_log_verify(const char *log, size_t len, uint8_t tip[TERMS_HASH_LEN], uint32_t *bad_line)
{
    return walk(log, len, tip, bad_line, 0, 0);
}

typedef struct {
    const char *seat;
    uint8_t ref[TERMS_HASH_LEN];
    int status;
} status_ctx_t;

static void status_fn(void *vp, const terms_record_t *rec)
{
    status_ctx_t *s = (status_ctx_t *) vp;
    if (strcmp(rec->seat, s->seat)) return;
    if (!memcmp(rec->ref, s->ref, TERMS_HASH_LEN))
        s->status = TERMS_STATUS_CURRENT;
    else if (s->status != TERMS_STATUS_CURRENT)
        s->status = TERMS_STATUS_OUTDATED;
}

int terms_log_status(const char *log, size_t len, const char *seat, const terms_doc_t *doc)
{
    status_ctx_t s;
    int rc;
    if (!doc || !terms_seat_valid(seat)) return TERMS_ERR_ARG;
    s.seat = seat;
    s.status = TERMS_STATUS_NEEDED;
    terms_hash(doc->reference ? doc->reference : doc, s.ref);
    rc = walk(log, len, 0, 0, status_fn, &s);
    return rc < 0 ? rc : s.status;
}
