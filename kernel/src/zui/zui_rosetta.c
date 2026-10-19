/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_rosetta.c — Rosetta, the human-side language bridge (zui_rosetta.h). */
#include "zui_rosetta.h"

#define PART_MAX (ZUI_READER_UTTER * 2u)
#define NUMS_MAX 4u

/* ---- small helpers --------------------------------------------------------------- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool same(const char *a, uint32_t an, const char *b, uint32_t bn)
{
    if (an != bn) return false;
    for (uint32_t i = 0; i < an; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bool starts(const char *s, uint32_t n, const char *p, uint32_t k)
{
    return k <= n && same(s, k, p, k);
}

/* an output buffer cut at whole UTF-8 characters */
typedef struct {
    char *p;
    uint32_t cap, n;
} buf_t;

static buf_t buf_start(char *p, uint32_t cap)
{
    buf_t b = {p, cap, 0};
    if (cap) p[0] = 0;
    return b;
}

static void buf_n(buf_t *b, const char *s, uint32_t n)
{
    if (!b->cap) return;
    uint32_t room = b->cap - 1u - b->n;
    if (n > room) { /* back up to a character start */
        n = room;
        while (n > 0 && ((unsigned char) s[n] & 0xC0u) == 0x80u) n--;
    }
    for (uint32_t i = 0; i < n; i++) b->p[b->n + i] = s[i];
    b->n += n;
    b->p[b->n] = 0;
}

static void buf_s(buf_t *b, const char *s)
{
    buf_n(b, s, slen(s));
}

/* one code point at s[*i..n), *i moved past it */
static uint32_t next_cp(const char *s, uint32_t n, uint32_t *i)
{
    unsigned char c = (unsigned char) s[*i];
    uint32_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (uint32_t k = 1; k < len && *i + k < n; k++)
        cp = (cp << 6) | ((unsigned char) s[*i + k] & 0x3Fu);
    *i += len;
    return cp;
}

/* ---- R1: numbers in a locale's digits ---------------------------------------------- */

static int32_t digit_of(const zui_locale_t *l, uint32_t cp)
{
    static const uint16_t HANI[10] = {0x3007, 0x4E00, 0x4E8C, 0x4E09, 0x56DB,
                                      0x4E94, 0x516D, 0x4E03, 0x516B, 0x4E5D};
    if (cp >= '0' && cp <= '9') return (int32_t) (cp - '0');
    for (int d = ZUI_DIGITS_DEFAULT; d <= ZUI_DIGITS_NATIVE; d++) {
        uint32_t z = zui_locale_zero(l, (zui_digits_t) d);
        if (z == HANI[0]) {
            for (int32_t k = 0; k < 10; k++)
                if (cp == HANI[k]) return k;
        } else if (cp >= z && cp <= z + 9u) {
            return (int32_t) (cp - z);
        }
    }
    return -1;
}

/* a character that may sit between the digits of one grouped number */
static bool group_mark(uint32_t cp)
{
    return cp == ',' || cp == '.' || cp == ' ' || cp == '\'' || cp == 0x00A0 || cp == 0x202F ||
           cp == 0x066C || cp == 0x2019 || cp == 0x066B;
}

/* the numbers in s[0..n), in order; how many. grouped: a group mark between
 * two digits joins them ("1 234"); otherwise it separates them ("1 3"). */
static uint32_t read_numbers(const zui_locale_t *l, const char *s, uint32_t n, uint64_t *v,
                             bool grouped)
{
    uint32_t count = 0, i = 0;
    bool in = false;
    while (i < n && count < NUMS_MAX) {
        uint32_t at = i, cp = next_cp(s, n, &i);
        int32_t d = digit_of(l, cp);
        if (d >= 0) {
            if (!in) v[count++] = 0;
            in = true;
            if (v[count - 1] < 100000000000000000ull)
                v[count - 1] = v[count - 1] * 10u + (uint32_t) d;
            continue;
        }
        if (grouped && in && group_mark(cp) && i < n) { /* a group mark only between two digits */
            uint32_t j = i;
            if (digit_of(l, next_cp(s, n, &j)) >= 0) continue;
        }
        (void) at;
        in = false;
    }
    return count;
}

static int64_t i64(uint64_t v)
{
    return v > (uint64_t) INT64_MAX ? INT64_MAX : (int64_t) v;
}

/* v in plain ASCII digits, as the reader writes numbers in its English */
static void buf_u(buf_t *b, uint64_t v)
{
    char d[24];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) buf_n(b, &d[--n], 1);
}

/* the English reader form of a count: "1 item", "12 items", "75 percent" */
static void en_count(buf_t *b, zui_count_t noun, uint64_t v)
{
    static const char *const NOUN[ZUI_COUNT_NOUNS][2] = {{" item", " items"},
                                                         {" character", " characters"},
                                                         {" token", " tokens"},
                                                         {" percent", " percent"}};
    buf_u(b, v);
    buf_s(b, NOUN[noun][v == 1 ? 0 : 1]);
}

/* ---- R1: one part to its English reader form ------------------------------------------ */

static bool has_placeholder(const char *t)
{
    for (; *t; t++)
        if (t[0] == '{' && t[1] && t[2] == '}') return true;
    return false;
}

/* a template with one {x}: s[0..n) is its prefix, some middle and its suffix */
static bool match_x(const char *t, const char *s, uint32_t n, uint32_t *mid, uint32_t *mid_n)
{
    uint32_t tn = slen(t), x = 0;
    while (x + 2 < tn && !(t[x] == '{' && t[x + 1] == 'x' && t[x + 2] == '}')) x++;
    if (x + 2 >= tn) return false;
    uint32_t pre = x, suf = tn - x - 3;
    if (n <= pre + suf || !starts(s, n, t, pre) || !same(s + n - suf, suf, t + x + 3, suf))
        return false;
    *mid = pre;
    *mid_n = n - pre - suf;
    return true;
}

/* a count in l ("5 элементов") to English ("5 items") */
static bool count_to_en(const zui_locale_t *l, const char *s, uint32_t n, buf_t *en)
{
    uint64_t v[NUMS_MAX] = {0, 1, 2}; /* no digits: a form that names the number ("one") */
    uint32_t nv = read_numbers(l, s, n, v, true);
    if (nv > 1) return false;
    char said[PART_MAX];
    for (uint32_t c = 0; c < (nv ? 1u : 3u); c++)
        for (uint32_t noun = 0; noun < ZUI_COUNT_NOUNS; noun++) {
            uint32_t k = zui_locale_count(l, (zui_count_t) noun, i64(v[c]), said, sizeof said);
            if (same(said, k, s, n)) {
                en_count(en, (zui_count_t) noun, v[c]);
                return true;
            }
        }
    return false;
}

/* the reader's words and patterns with no free text in them */
static bool part_to_en(const zui_locale_t *l, const char *s, uint32_t n, buf_t *en)
{
    for (uint32_t w = 0; w < ZUI_WORD_COUNT; w++) { /* a word */
        const char *t = zui_locale_word(l, (zui_word_t) w);
        if (w == ZUI_WORD_ERROR_PREFIX || has_placeholder(t)) continue;
        if (same(t, slen(t), s, n)) {
            buf_s(en, zui_locale_english((zui_word_t) w));
            return true;
        }
    }
    uint64_t v[NUMS_MAX];
    uint32_t nv = read_numbers(l, s, n, v, true);
    char said[PART_MAX];
    if (nv == 1) { /* heading level n */
        uint32_t k = zui_locale_heading(l, (uint32_t) (v[0] > 0xFFFFFFFFu ? 0xFFFFFFFFu : v[0]),
                                        said, sizeof said);
        if (same(said, k, s, n)) {
            buf_s(en, "heading level ");
            buf_u(en, v[0]);
            return true;
        }
    }
    if (nv != 2) nv = read_numbers(l, s, n, v, false); /* "1 3-დან" is two numbers */
    if (nv == 2) {                                     /* k of n, the numbers in either order */
        for (uint32_t o = 0; o < 2; o++) {
            uint64_t kk = v[o], nn = v[1 - o];
            uint32_t k = zui_locale_position(l, kk, nn, said, sizeof said);
            if (same(said, k, s, n)) {
                buf_u(en, kk);
                buf_s(en, " of ");
                buf_u(en, nn);
                return true;
            }
        }
    }
    if (count_to_en(l, s, n, en)) return true;
    uint32_t m, mn;
    const char *sc = zui_locale_word(l, ZUI_WORD_SHORTCUT); /* the key names are kept */
    uint32_t scn = slen(sc);
    if (n > scn && starts(s, n, sc, scn) && s[scn] == ' ') {
        buf_s(en, "shortcut");
        buf_n(en, s + scn, n - scn);
        return true;
    }
    if (match_x(zui_locale_word(l, ZUI_WORD_ABOUT), s, n, &m, &mn)) { /* about N tokens */
        char inner[PART_MAX];
        buf_t ib = buf_start(inner, sizeof inner);
        if (count_to_en(l, s + m, mn, &ib)) {
            buf_s(en, "about ");
            buf_s(en, inner);
            return true;
        }
    }
    return false;
}

/* the patterns around free text: from X, by X (X is kept) */
static bool part_x_to_en(const zui_locale_t *l, const char *s, uint32_t n, buf_t *en)
{
    static const struct {
        zui_word_t w;
        const char *en;
    } X[] = {{ZUI_WORD_FROM, "from "}, {ZUI_WORD_BY, "by "}};
    uint32_t m, mn;
    for (uint32_t k = 0; k < sizeof X / sizeof X[0]; k++)
        if (match_x(zui_locale_word(l, X[k].w), s, n, &m, &mn)) {
            buf_s(en, X[k].en);
            buf_n(en, s + m, mn);
            return true;
        }
    return false;
}

/* ---- R2 to R4 -------------------------------------------------------------------- */

const zui_locale_t *zui_rosetta_target(const zui_locale_t *to)
{
    return zui_locale_bridge(zui_locale_find(zui_locale_tag(to)));
}

static zui_review_t weaker(zui_review_t a, zui_review_t b)
{
    /* SOURCE and NATIVE are trusted, MACHINE is not */
    return (a == ZUI_REVIEW_MACHINE || b == ZUI_REVIEW_MACHINE) ? ZUI_REVIEW_MACHINE : a;
}

#define SEGS_MAX 8u

/* The segments from s[i]: their ends (ends[k]) and the separator after each
 * (skips[k]), up to SEGS_MAX; how many. */
static uint32_t segments(const char *s, uint32_t n, uint32_t i, const char *sep, uint32_t *ends,
                         uint32_t *skips)
{
    uint32_t sn = slen(sep), k = 0, at = i;
    while (k < SEGS_MAX) {
        uint32_t skip = 0;
        while (at < n) {
            if (sn && starts(s + at, n - at, sep, sn)) {
                skip = sn;
                break;
            }
            if (starts(s + at, n - at, ", ", 2)) {
                skip = 2;
                break;
            }
            at++;
        }
        ends[k] = at;
        skips[k++] = skip;
        if (at >= n) break;
        at += skip;
    }
    return k;
}

/* The part at s[i]: the longest run of segments that is one of the reader's
 * words or patterns (some contain the separator: "第 1 项，共 3 项", a Thai
 * phrase with spaces), else the shortest that is "from X" or "by X", else
 * one segment. *en gets its English when known; returns the segment count. */
static uint32_t read_part(const zui_locale_t *l, const char *s, uint32_t n, uint32_t i,
                          const char *sep, uint32_t *end, uint32_t *skip, buf_t *en, bool *known)
{
    uint32_t ends[SEGS_MAX], skips[SEGS_MAX];
    uint32_t k = segments(s, n, i, sep, ends, skips);
    for (uint32_t j = k; j-- > 0;) {
        en->n = 0;
        if (part_to_en(l, s + i, ends[j] - i, en)) {
            *end = ends[j];
            *skip = skips[j];
            *known = true;
            return j + 1;
        }
    }
    for (uint32_t j = 0; j < k; j++) {
        en->n = 0;
        if (part_x_to_en(l, s + i, ends[j] - i, en)) {
            *end = ends[j];
            *skip = skips[j];
            *known = true;
            return j + 1;
        }
    }
    en->n = 0;
    if (en->cap) en->p[0] = 0;
    *end = ends[0];
    *skip = skips[0];
    *known = false;
    return 1;
}

uint32_t zui_rosetta_translate(const zui_rosetta_t *r, const zui_locale_t *from,
                               const zui_locale_t *to, const char *text, char *out, uint32_t cap,
                               zui_rosetta_report_t *report)
{
    zui_rosetta_report_t rep = {0, 0, 0, 0, 0, ZUI_REVIEW_SOURCE};
    const zui_locale_t *src = zui_locale_find(zui_locale_tag(from));
    const zui_locale_t *dst = zui_rosetta_target(to);
    buf_t b = buf_start(out, cap);
    rep.spoken = dst;
    rep.trust = zui_locale_review(dst);
    const char *s = text ? text : "";
    uint32_t n = slen(s), i = 0;
    const char *sep = zui_locale_list_separator(src);
    const char *ep = zui_locale_word(src, ZUI_WORD_ERROR_PREFIX);
    uint32_t epn = slen(ep);
    if (n && epn && starts(s, n, ep, epn)) { /* "Error: " and the app's own message */
        buf_s(&b, zui_locale_word(dst, ZUI_WORD_ERROR_PREFIX));
        rep.parts++;
        rep.bridged++;
        i = epn;
        if (i < n) {
            rep.parts++;
            uint32_t k = 0;
            if (r && r->provider && src != dst) {
                char t[PART_MAX];
                k = r->provider(src, dst, s + i, n - i, t, sizeof t, r->provider_ctx);
                if (k) {
                    buf_n(&b, t, k);
                    rep.provided++;
                    rep.trust = ZUI_REVIEW_MACHINE;
                }
            }
            if (!k) {
                buf_n(&b, s + i, n - i);
                rep.kept++;
            }
        }
        if (report) *report = rep;
        return b.n;
    }
    bool first = true, prev_known = false;
    while (i < n) {
        uint32_t skip, end;
        char en[PART_MAX], said[PART_MAX];
        buf_t eb = buf_start(en, sizeof en);
        bool known;
        read_part(src, s, n, i, sep, &end, &skip, &eb, &known);
        uint32_t k = 0;
        if (known) {
            k = zui_locale_phrase(dst, en, said, sizeof said);
            rep.bridged++;
        } else if (r && r->provider && src != dst) {
            k = r->provider(src, dst, s + i, end - i, said, sizeof said, r->provider_ctx);
            if (k) {
                rep.provided++;
                rep.trust = weaker(rep.trust, ZUI_REVIEW_MACHINE);
            }
        }
        if (!known && !k) rep.kept++;
        rep.parts++;
        if (!first) buf_s(&b, (known || prev_known) ? zui_locale_list_separator(dst) : ", ");
        if (k)
            buf_n(&b, said, k);
        else
            buf_n(&b, s + i, end - i);
        first = false;
        prev_known = known;
        i = end + skip;
    }
    if (report) *report = rep;
    return b.n;
}

/* ---- R5 -------------------------------------------------------------------------- */

uint32_t zui_rosetta_explain(const zui_rosetta_t *r, const zui_locale_t *to, const char *code,
                             uint32_t n, char *out, uint32_t cap, zui_rosetta_report_t *report)
{
    char en[ZUI_READER_UTTER * 4u];
    uint32_t k = (r && r->babel && code) ? r->babel(code, n, en, sizeof en, r->babel_ctx) : 0;
    if (k >= sizeof en) k = sizeof en - 1u;
    en[k] = 0;
    if (!k) {
        if (cap) out[0] = 0;
        if (report) {
            zui_rosetta_report_t z = {zui_rosetta_target(to), 0, 0, 0, 0, ZUI_REVIEW_MACHINE};
            *report = z;
        }
        return 0;
    }
    uint32_t w = zui_rosetta_translate(r, zui_locale_find("en"), to, en, out, cap, report);
    if (report) report->trust = ZUI_REVIEW_MACHINE; /* Babel's English is machine-made */
    return w;
}
