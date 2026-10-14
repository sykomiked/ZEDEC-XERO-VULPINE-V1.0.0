/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_caption.c — live captions: segments, line breaking, drawing and the
 * transcript (zui_caption.h). */
#include "zui_caption.h"
#include "zui_motion.h"
#include "zui_palette.h"
#include "zui_tokens.h"

/* ---- small helpers ------------------------------------------------------ */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void mset(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void mcopy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) a[i] = b[i];
}

/* Copy at most cap - 1 bytes on a character boundary; controls become spaces. */
static void scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t n = s ? zui_utf8_fit(s, slen(s), cap - 1u) : 0;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t) s[i] < 0x20u ? ' ' : s[i];
    d[n] = 0;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The code point at s[i..n), and the index after it. Bad bytes are U+FFFD. */
static uint32_t decode(const char *s, uint32_t n, uint32_t i, uint32_t *next)
{
    uint32_t k = zui_utf8_next(s, n, i);
    if (k <= i) k = i + 1u;
    *next = k;
    const uint8_t *b = (const uint8_t *) s + i;
    uint32_t len = k - i;
    if (len == 1) return b[0] < 0x80u ? b[0] : 0xFFFDu;
    if (len == 2) return ((b[0] & 0x1Fu) << 6) | (b[1] & 0x3Fu);
    if (len == 3) return ((b[0] & 0x0Fu) << 12) | ((b[1] & 0x3Fu) << 6) | (b[2] & 0x3Fu);
    if (len == 4)
        return ((b[0] & 0x07u) << 18) | ((b[1] & 0x3Fu) << 12) | ((b[2] & 0x3Fu) << 6) |
               (b[3] & 0x3Fu);
    return 0xFFFDu;
}

/* CA4: characters that may be broken between without a space. */
static bool ideographic(uint32_t cp)
{
    return (cp >= 0x2E80u && cp <= 0x9FFFu) || (cp >= 0xF900u && cp <= 0xFAFFu) ||
           (cp >= 0xFF00u && cp <= 0xFFEFu) || (cp >= 0x20000u && cp <= 0x3FFFFu);
}

/* Never at the start of a line (closing punctuation). */
static bool no_break_before(uint32_t cp)
{
    switch (cp) {
    case ',':
    case '.':
    case '!':
    case '?':
    case ':':
    case ';':
    case ')':
    case 0x3001u: /* 、 */
    case 0x3002u: /* 。 */
    case 0x300Du: /* 」 */
    case 0x300Fu: /* 』 */
    case 0x3009u: /* 〉 */
    case 0xFF01u: /* ！ */
    case 0xFF09u: /* ） */
    case 0xFF0Cu: /* ， */
    case 0xFF0Eu: /* ． */
    case 0xFF1Au: /* ： */
    case 0xFF1Bu: /* ； */
    case 0xFF1Fu: /* ？ */
    case 0x30FCu: /* ー */
        return true;
    default:
        return false;
    }
}

/* Marks that belong to the character before them: never break before one. */
static bool extends(uint32_t cp)
{
    return (cp >= 0x0300u && cp <= 0x036Fu) || (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
           (cp >= 0x20D0u && cp <= 0x20FFu) || (cp >= 0xFE00u && cp <= 0xFE0Fu) ||
           (cp >= 0xFE20u && cp <= 0xFE2Fu) || cp == 0x200Du || (cp >= 0x0E31u && cp <= 0x0E3Au) ||
           (cp >= 0x0900u && cp <= 0x0903u) || (cp >= 0x093Au && cp <= 0x094Fu);
}

/* ---- setup -------------------------------------------------------------- */

void zui_cap_style_default(zui_cap_style_t *st)
{
    if (!st) return;
    st->size_px = 24;
    st->bg_q8 = 192;
    st->position = ZUI_CAP_BOTTOM;
    st->high_contrast = false;
    st->max_lines = 2;
    st->width_pct = 80;
    st->margin_px = ZUI_S5;
    st->pad_px = ZUI_S2;
}

void zui_caption_init(zui_caption_t *c, const zui_text_t *text)
{
    mset(c, sizeof *c);
    c->text = text;
    c->hold_ms = ZUI_CAP_HOLD_MS;
    zui_cap_style_default(&c->style);
    c->width_px = -1;
    c->dirty = true;
}

void zui_caption_set_style(zui_caption_t *c, const zui_cap_style_t *st)
{
    if (!st) return;
    c->style = *st;
    c->style.size_px = clampi(st->size_px, ZUI_CAP_SIZE_MIN, ZUI_CAP_SIZE_MAX);
    c->style.bg_q8 = st->bg_q8 > 256u ? 256u : st->bg_q8;
    c->style.position = st->position == ZUI_CAP_TOP ? ZUI_CAP_TOP : ZUI_CAP_BOTTOM;
    c->style.max_lines = (uint8_t) clampi(st->max_lines, 1, (int32_t) ZUI_CAP_LINES_MAX);
    c->style.width_pct = (uint8_t) clampi(st->width_pct, 20, 100);
    c->style.margin_px = imax(0, st->margin_px);
    c->style.pad_px = imax(0, st->pad_px);
    c->dirty = true;
}

bool zui_caption_set_speaker(zui_caption_t *c, uint16_t speaker, const char *name)
{
    if (speaker >= ZUI_CAP_SPEAKERS) return false;
    scopy(c->speaker[speaker], ZUI_CAP_NAME_MAX, name);
    c->dirty = true;
    return true;
}

/* ---- CA1, CA2: segments ------------------------------------------------- */

static void live_remove(zui_caption_t *c, uint32_t i)
{
    for (uint32_t k = i; k + 1u < c->n_live; k++) c->live[k] = c->live[k + 1u];
    c->n_live--;
    c->dirty = true;
}

/* Insert in start order, after any equal starts. */
static void live_insert(zui_caption_t *c, const zui_cap_seg_t *g)
{
    if (c->n_live == ZUI_CAP_LIVE) { /* full: the oldest final goes, else the oldest */
        uint32_t drop = 0;
        for (uint32_t i = 0; i < c->n_live; i++)
            if (c->live[i].final) {
                drop = i;
                break;
            }
        live_remove(c, drop);
        c->live_dropped++;
    }
    uint32_t at = c->n_live;
    while (at > 0 && c->live[at - 1u].start_ms > g->start_ms) at--;
    for (uint32_t k = c->n_live; k > at; k--) c->live[k] = c->live[k - 1u];
    c->live[at] = *g;
    c->n_live++;
    c->dirty = true;
}

static void transcript_put(zui_caption_t *c, const zui_cap_seg_t *g)
{
    /* a correction of a final already kept replaces it */
    for (uint32_t i = 0; i < c->ring_n; i++) {
        uint32_t k = (c->ring_head + ZUI_CAP_TRANSCRIPT - c->ring_n + i) % ZUI_CAP_TRANSCRIPT;
        if (c->ring[k].speaker == g->speaker && c->ring[k].start_ms == g->start_ms) {
            c->ring[k] = *g;
            return;
        }
    }
    c->ring[c->ring_head] = *g;
    c->ring_head = (c->ring_head + 1u) % ZUI_CAP_TRANSCRIPT;
    if (c->ring_n < ZUI_CAP_TRANSCRIPT)
        c->ring_n++;
    else
        c->ring_dropped++;
}

zui_cap_result_t zui_caption_feed(zui_caption_t *c, uint32_t start_ms, uint32_t end_ms,
                                  uint16_t speaker, bool final, const char *utf8)
{
    if (!c || !utf8) return ZUI_CAP_IGNORED;
    if (end_ms < start_ms) end_ms = start_ms;
    zui_cap_seg_t g;
    g.start_ms = start_ms;
    g.end_ms = end_ms;
    g.speaker = speaker;
    g.final = final;
    scopy(g.text, sizeof g.text, utf8);
    bool empty = true;
    for (uint32_t i = 0; g.text[i]; i++)
        if (g.text[i] != ' ') empty = false;

    /* the same speaker and start: a partial being replaced, or a correction */
    for (uint32_t i = 0; i < c->n_live; i++) {
        zui_cap_seg_t *l = &c->live[i];
        if (l->speaker != speaker || l->start_ms != start_ms) continue;
        if (l->final && !final) return ZUI_CAP_IGNORED; /* a late partial for a final */
        if (empty) {
            if (final) return ZUI_CAP_IGNORED;
            live_remove(c, i);
            return ZUI_CAP_WITHDRAWN;
        }
        *l = g;
        c->dirty = true;
        if (final) transcript_put(c, &g);
        return ZUI_CAP_REPLACED;
    }
    if (empty) return ZUI_CAP_IGNORED;
    /* drop what this one supersedes (CA2) */
    for (uint32_t i = c->n_live; i-- > 0;) {
        const zui_cap_seg_t *l = &c->live[i];
        if (l->speaker != speaker || l->final) continue;
        if (!final || (l->start_ms >= start_ms && l->start_ms <= end_ms)) live_remove(c, i);
    }
    live_insert(c, &g);
    if (final) transcript_put(c, &g);
    return ZUI_CAP_ADDED;
}

void zui_caption_tick(zui_caption_t *c, uint32_t now_ms)
{
    c->now_ms = now_ms;
    for (uint32_t i = c->n_live; i-- > 0;) {
        const zui_cap_seg_t *l = &c->live[i];
        uint64_t until = (uint64_t) l->end_ms + (uint64_t) c->hold_ms * (l->final ? 1u : 2u);
        if ((uint64_t) now_ms >= until) live_remove(c, i);
    }
}

void zui_caption_clear(zui_caption_t *c)
{
    c->n_live = 0;
    c->dirty = true;
}

/* ---- CA4: measuring ------------------------------------------------------- */

static bool have_font(const zui_caption_t *c)
{
    return c->text && c->text->font && c->text->cache;
}

static zui_text_t sized(const zui_caption_t *c)
{
    zui_text_t t = *c->text;
    t.size_px = (uint32_t) c->style.size_px;
    return t;
}

/* One code point's advance, 1/256 px. */
static int32_t cp_w_q8(const zui_caption_t *c, const zui_text_t *t, const char *s, uint32_t i,
                       uint32_t k, uint32_t cp)
{
    if (!t) {
        if (extends(cp) || cp == 0x200Bu) return 0;
        return c->style.size_px * 128;
    }
    char one[5];
    uint32_t n = k - i > 4u ? 4u : k - i;
    for (uint32_t b = 0; b < n; b++) one[b] = s[i + b];
    one[n] = 0;
    return zui_text_width_q8(t, one);
}

int32_t zui_caption_measure(const zui_caption_t *c, const char *s, uint32_t n)
{
    zui_text_t t;
    const zui_text_t *tp = 0;
    if (have_font(c)) {
        t = sized(c);
        tp = &t;
    }
    int32_t w = 0;
    for (uint32_t i = 0, k; i < n; i = k) {
        uint32_t cp = decode(s, n, i, &k);
        w += cp_w_q8(c, tp, s, i, k, cp);
    }
    return (w + 255) >> 8;
}

int32_t zui_caption_line_h(const zui_caption_t *c)
{
    if (have_font(c)) {
        zui_text_t t = sized(c);
        return zui_text_ascent(&t) - zui_text_descent(&t) + c->style.size_px / 4;
    }
    return (c->style.size_px * 5 + 3) / 4;
}

/* ---- CA4, CA5: lines ---------------------------------------------------------- */

static void emit(zui_caption_t *c, const char *s, uint32_t a, uint32_t b)
{
    while (b > a && s[b - 1u] == ' ') b--;
    uint32_t keep = c->style.max_lines;
    if (c->n_lines == keep) { /* scroll: the oldest line goes */
        for (uint32_t i = 0; i + 1u < keep; i++) {
            mcopy(c->line[i], c->line[i + 1u], ZUI_CAP_LINE_MAX);
            c->line_w[i] = c->line_w[i + 1u];
        }
        c->n_lines--;
    }
    char *d = c->line[c->n_lines];
    uint32_t n = b - a;
    if (n > ZUI_CAP_LINE_MAX - 1u) n = zui_utf8_fit(s + a, n, ZUI_CAP_LINE_MAX - 1u);
    mcopy(d, s + a, n);
    d[n] = 0;
    c->line_w[c->n_lines] = zui_caption_measure(c, d, n);
    c->n_lines++;
    c->total_lines++;
}

static void wrap(zui_caption_t *c, const char *p, uint32_t n, int32_t maxw)
{
    zui_text_t t;
    const zui_text_t *tp = 0;
    if (have_font(c)) {
        t = sized(c);
        tp = &t;
    }
    int32_t max_q8 = imax(1, maxw) * 256;
    uint32_t i = 0;
    while (i < n) {
        while (i < n && p[i] == ' ') i++;
        if (i >= n) break;
        uint32_t ls = i, j = i, brk_end = 0, brk_res = 0, pcp = 0;
        bool have_brk = false, done = false;
        int32_t w = 0;
        while (j < n) {
            uint32_t k, cp = decode(p, n, j, &k);
            if (j > ls && cp != ' ' && !no_break_before(cp) && !extends(cp) &&
                (ideographic(cp) || ideographic(pcp))) {
                brk_end = j;
                brk_res = j;
                have_brk = true;
            }
            if (cp == ' ') { /* a space never overflows; it is trimmed at the break */
                brk_end = j;
                brk_res = k;
                have_brk = true;
                w += cp_w_q8(c, tp, p, j, k, cp);
            } else {
                int32_t cw = cp_w_q8(c, tp, p, j, k, cp);
                bool too_wide = w + cw > max_q8;
                bool too_long = k - ls > ZUI_CAP_LINE_MAX - 1u;
                if (j > ls && (too_wide || too_long)) {
                    if (have_brk && brk_end > ls) {
                        emit(c, p, ls, brk_end);
                        i = brk_res;
                    } else { /* a word longer than the line: between characters */
                        uint32_t cut = j;
                        while (cut > ls) {
                            uint32_t k2, here = decode(p, n, cut, &k2);
                            if (!extends(here)) break;
                            uint32_t prev = zui_utf8_prev(p, cut);
                            if (prev <= ls) break;
                            cut = prev;
                        }
                        if (cut <= ls) cut = k; /* one unbreakable character: let it stand */
                        emit(c, p, ls, cut);
                        i = cut;
                    }
                    done = true;
                    break;
                }
                w += cw;
            }
            pcp = cp;
            j = k;
        }
        if (!done) {
            emit(c, p, ls, n);
            i = n;
        }
    }
}

static void put(char *d, uint32_t cap, uint32_t *n, const char *s)
{
    uint32_t m = slen(s);
    if (*n + m > cap - 1u) m = zui_utf8_fit(s, m, cap - 1u - *n);
    mcopy(d + *n, s, m);
    *n += m;
    d[*n] = 0;
}

uint32_t zui_caption_layout(zui_caption_t *c, int32_t width_px)
{
    if (!c->dirty && width_px == c->width_px) return c->n_lines;
    c->n_lines = 0;
    c->total_lines = 0;
    c->width_px = width_px;
    c->dirty = false;
    uint32_t i = 0;
    while (i < c->n_live) {
        uint16_t who = c->live[i].speaker;
        uint32_t n = 0;
        c->scratch[0] = 0;
        bool lead = i > 0 || (who < ZUI_CAP_SPEAKERS && c->speaker[who][0]);
        if (lead) { /* CA3 */
            if (who < ZUI_CAP_SPEAKERS && c->speaker[who][0]) {
                put(c->scratch, sizeof c->scratch, &n, c->speaker[who]);
                put(c->scratch, sizeof c->scratch, &n, ": ");
            } else {
                put(c->scratch, sizeof c->scratch, &n, ">> ");
            }
        }
        for (bool first = true; i < c->n_live && c->live[i].speaker == who; i++, first = false) {
            if (!first) put(c->scratch, sizeof c->scratch, &n, " ");
            put(c->scratch, sizeof c->scratch, &n, c->live[i].text);
        }
        wrap(c, c->scratch, n, width_px);
    }
    return c->n_lines;
}

const char *zui_caption_line(const zui_caption_t *c, uint32_t i)
{
    return i < c->n_lines ? c->line[i] : "";
}

/* ---- CA6: drawing --------------------------------------------------------------- */

static int32_t text_room(const zui_caption_t *c, zui_rect_t area)
{
    return area.w * c->style.width_pct / 100 - 2 * c->style.margin_px - 2 * c->style.pad_px;
}

zui_rect_t zui_caption_box(zui_caption_t *c, zui_rect_t area)
{
    zui_rect_t r = {0, 0, 0, 0};
    uint32_t n = zui_caption_layout(c, text_room(c, area));
    if (!n) return r;
    int32_t wmax = 0;
    for (uint32_t i = 0; i < n; i++) wmax = imax(wmax, c->line_w[i]);
    r.w = wmax + 2 * c->style.pad_px;
    r.h = (int32_t) n * zui_caption_line_h(c) + 2 * c->style.pad_px;
    r.x = area.x + (area.w - r.w) / 2;
    r.y = c->style.position == ZUI_CAP_TOP ? area.y + c->style.margin_px
                                           : area.y + area.h - c->style.margin_px - r.h;
    return r;
}

void zui_caption_draw(zui_caption_t *c, zui_surface_t *s, zui_rect_t area)
{
    zui_rect_t r = zui_caption_box(c, area);
    if (r.w <= 0) return;
    bool hc = c->style.high_contrast;
    uint32_t ground = hc ? zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_VOID) : ZUI_VOID;
    uint32_t ink = hc ? zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_TEXT) : ZUI_TEXT;
    uint32_t a = c->style.bg_q8 >= 256u ? 255u : c->style.bg_q8 * 255u / 256u;
    a = hc ? 255u : zui_motion_alpha(a);
    if (a) {
        zui_fill_rrect(s, r, ZUI_R_CONTROL, ground, a);
        if (hc)
            zui_stroke_rrect(s, r, ZUI_R_CONTROL, 2,
                             zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_EDGE), 255u);
        else
            zui_engrave(s, r, ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40u);
    }
    if (!have_font(c)) return;
    zui_text_t t = sized(c);
    int32_t lh = zui_caption_line_h(c), asc = zui_text_ascent(&t);
    for (uint32_t i = 0; i < c->n_lines; i++)
        zui_text(s, &t, r.x + c->style.pad_px, r.y + c->style.pad_px + (int32_t) i * lh + asc, ink,
                 c->line[i]);
}

/* ---- CA7: the transcript ------------------------------------------------------------ */

uint32_t zui_caption_transcript_count(const zui_caption_t *c)
{
    return c->ring_n;
}

const zui_cap_seg_t *zui_caption_transcript_get(const zui_caption_t *c, uint32_t i)
{
    if (i >= c->ring_n) return 0;
    return &c->ring[(c->ring_head + ZUI_CAP_TRANSCRIPT - c->ring_n + i) % ZUI_CAP_TRANSCRIPT];
}

static void put_u(char *d, uint32_t cap, uint32_t *n, uint32_t v, uint32_t min_digits)
{
    char b[12];
    uint32_t k = 0;
    do {
        b[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && k < sizeof b);
    while (k < min_digits && k < sizeof b) b[k++] = '0';
    char o[13];
    for (uint32_t i = 0; i < k; i++) o[i] = b[k - 1u - i];
    o[k] = 0;
    put(d, cap, n, o);
}

uint32_t zui_caption_transcript_text(const zui_caption_t *c, char *out, uint32_t cap)
{
    if (!out || !cap) return 0;
    uint32_t n = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < c->ring_n; i++) {
        const zui_cap_seg_t *g = zui_caption_transcript_get(c, i);
        uint32_t sec = g->start_ms / 1000u;
        put(out, cap, &n, "[");
        if (sec >= 3600u) {
            put_u(out, cap, &n, sec / 3600u, 1);
            put(out, cap, &n, ":");
            put_u(out, cap, &n, sec / 60u % 60u, 2);
        } else {
            put_u(out, cap, &n, sec / 60u, 1);
        }
        put(out, cap, &n, ":");
        put_u(out, cap, &n, sec % 60u, 2);
        put(out, cap, &n, "] ");
        if (g->speaker < ZUI_CAP_SPEAKERS && c->speaker[g->speaker][0]) {
            put(out, cap, &n, c->speaker[g->speaker]);
            put(out, cap, &n, ": ");
        }
        put(out, cap, &n, g->text);
        put(out, cap, &n, "\n");
    }
    return n;
}
