/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ticker_model.c — Ticker (see ticker_model.h). */
#include <string.h>

#include "ticker_model.h"

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static bool alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* xlate.translate's `to`: ^[A-Za-z0-9]+(-[A-Za-z0-9]+)*$, 2..15 bytes */
static bool tag_ok(const char *s)
{
    uint32_t n = len_of(s, TICKER_TAG_MAX + 1u);
    if (n < 2u || n > TICKER_TAG_MAX) return false;
    bool after_dash = true;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] == '-') {
            if (after_dash) return false;
            after_dash = true;
        } else if (alnum(s[i])) {
            after_dash = false;
        } else {
            return false;
        }
    }
    return !after_dash;
}

/* the same language: the primary subtags agree, case aside ("en", "EN-gb") */
static bool same_language(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] != '-' && b[i] && b[i] != '-'; i++)
        if (lower(a[i]) != lower(b[i])) return false;
    bool ea = !a[i] || a[i] == '-', eb = !b[i] || b[i] == '-';
    return ea && eb;
}

static bool same_tag(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && b[i]; i++)
        if (lower(a[i]) != lower(b[i])) return false;
    return !a[i] && !b[i];
}

int ticker_set_language(ticker_t *t, const char *to)
{
    if (!t || !tag_ok(to)) return ZXM_E_ARG;
    memcpy(t->to, to, len_of(to, TICKER_TAG_MAX) + 1u);
    return ZXM_OK;
}

int ticker_init(ticker_t *t, zxm_link_t *link, const char *to)
{
    if (!t || !link || !tag_ok(to)) return ZXM_E_ARG;
    memset(t, 0, sizeof *t);
    t->link = link;
    return ticker_set_language(t, to);
}

int ticker_languages(ticker_t *t)
{
    if (!t || !t->link) return ZXM_E_ARG;
    zxm_link_t *l = t->link;
    zxm_begin(l, "xlate.languages");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "languages");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    static char got[TICKER_LANGS][TICKER_TAG_MAX + 1];
    uint32_t k = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr) && k < TICKER_LANGS; i++) {
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "tag", got[k], sizeof got[k])) continue;
        if (tag_ok(got[k])) k++;
    }
    memcpy(t->langs, got, sizeof got);
    t->nlangs = k;
    return ZXM_OK;
}

uint32_t ticker_lang_place(const ticker_t *t, const char *tag)
{
    if (!t || !tag || !tag_ok(tag)) return 0;
    for (uint32_t i = 0; i < t->nlangs; i++)
        if (same_tag(t->langs[i], tag)) return i + 1u;
    for (uint32_t i = 0; i < t->nlangs; i++)
        if (same_language(t->langs[i], tag)) return i + 1u;
    return 0;
}

/* the bytes of the UTF-8 character at s[0..n), or 0 when it is not one */
static uint32_t char_len(const unsigned char *s, uint32_t n)
{
    unsigned char c = s[0];
    uint32_t k;
    uint32_t cp;
    if (c < 0x80u) return 1u;
    if (c >= 0xC2u && c <= 0xDFu)
        k = 2u, cp = c & 0x1Fu;
    else if (c >= 0xE0u && c <= 0xEFu)
        k = 3u, cp = c & 0x0Fu;
    else if (c >= 0xF0u && c <= 0xF4u)
        k = 4u, cp = c & 0x07u;
    else
        return 0;
    if (k > n) return 0;
    for (uint32_t i = 1; i < k; i++) {
        if ((s[i] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    if ((k == 3u && cp < 0x800u) || (k == 4u && (cp < 0x10000u || cp > 0x10FFFFu))) return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
    return k;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

int64_t ticker_chars(const char *s, uint32_t n)
{
    if (!s) return -1;
    int64_t chars = 0;
    for (uint32_t i = 0; i < n;) {
        unsigned char c = (unsigned char) s[i];
        if ((c < 0x20u && !is_space((char) c)) || c == 0x7Fu) return -1;
        uint32_t k = char_len((const unsigned char *) s + i, n - i);
        if (!k) return -1;
        i += k;
        chars++;
    }
    return chars;
}

typedef struct {
    ticker_caption_t *out;
    uint32_t cap, n; /* captions made */
    uint32_t bytes;  /* of the line being filled */
    uint32_t chars;
    bool over;
} breaker_t;

static ticker_caption_t *current(breaker_t *b)
{
    if (b->n >= b->cap) {
        b->over = true;
        return NULL;
    }
    return &b->out[b->n];
}

/* close the line being filled; a caption with two lines is done */
static void end_line(breaker_t *b)
{
    if (!b->chars) return;
    ticker_caption_t *c = current(b);
    if (!c) return;
    c->line[c->nlines][b->bytes] = 0;
    c->chars += b->chars;
    if (b->chars > c->longest) c->longest = b->chars;
    c->nlines++;
    b->bytes = b->chars = 0;
    if (c->nlines == TICKER_LINES) b->n++;
}

static void put(breaker_t *b, const char *s, uint32_t bytes, uint32_t chars)
{
    ticker_caption_t *c = current(b);
    if (!c) return;
    if (b->chars) c->line[c->nlines][b->bytes++] = ' ';
    memcpy(c->line[c->nlines] + b->bytes, s, bytes);
    b->bytes += bytes;
    b->chars += chars + (b->chars ? 1u : 0u);
}

int32_t ticker_break(const char *s, uint32_t n, ticker_caption_t *out, uint32_t cap)
{
    if (!s || !out || ticker_chars(s, n) < 0) return -1;
    breaker_t b = {out, cap, 0, 0, 0, false};
    memset(out, 0, sizeof *out * cap);
    uint32_t i = 0;
    while (i < n && !b.over) {
        while (i < n && is_space(s[i])) i++;
        uint32_t ws = i, wchars = 0;
        while (i < n && !is_space(s[i])) {
            i += char_len((const unsigned char *) s + i, n - i);
            wchars++;
        }
        while (wchars && !b.over) {
            uint32_t room = TICKER_LINE_CHARS - b.chars - (b.chars ? 1u : 0u);
            if (b.chars && b.chars >= TICKER_LINE_CHARS) room = 0;
            if (wchars <= room) {
                put(&b, s + ws, i - ws, wchars);
                wchars = 0;
            } else if (b.chars) {
                end_line(&b);
            } else { /* a word longer than a line: cut at a character */
                uint32_t take = 0, bytes = 0;
                while (take < TICKER_LINE_CHARS) {
                    bytes += char_len((const unsigned char *) s + ws + bytes, n - ws - bytes);
                    take++;
                }
                put(&b, s + ws, bytes, take);
                end_line(&b);
                ws += bytes;
                wchars -= take;
            }
        }
    }
    end_line(&b);
    if (!b.over && b.n < cap && out[b.n].nlines) b.n++;
    if (b.over) return -1;
    return (int32_t) b.n;
}

uint32_t ticker_waiting(const ticker_t *t)
{
    return t ? t->head - t->tail : 0;
}

/* TM2: the language of the words; "und" when zxvd does not know */
static int detect(ticker_t *t, const char *text, uint32_t n, char *tag)
{
    zxm_link_t *l = t->link;
    memcpy(tag, "und", 4u);
    zxm_begin(l, "xlate.detect");
    zxm_p_strn(l, "text", text, n);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) return ZXM_OK;
    if (rc) return rc;
    char got[TICKER_TAG_MAX + 1];
    if (!zxm_get_str(l, l->result, "language", got, sizeof got) && tag_ok(got))
        memcpy(tag, got, len_of(got, TICKER_TAG_MAX) + 1u);
    return ZXM_OK;
}

/* TM3: the words in the person's language, into t->text */
static int translate(ticker_t *t, const char *text, uint32_t n, const char *from, bool *done,
                     bool *machine)
{
    zxm_link_t *l = t->link;
    *done = *machine = false;
    zxm_begin(l, "xlate.translate");
    zxm_p_strn(l, "text", text, n);
    zxm_p_str(l, "to", t->to);
    zxm_p_str(l, "from", from);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == -32031) { /* no model: shown as heard */
        t->no_model = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "text", t->text, sizeof t->text)) return ZXM_E_PROTOCOL;
    uint32_t m = len_of(t->text, TICKER_OUT_MAX + 1u);
    if (m == 0 || m > TICKER_OUT_MAX || ticker_chars(t->text, m) < 1) return ZXM_E_PROTOCOL;
    bool tr = false, ma = false;
    if (zxm_get(l, l->result, "translated") >= 0 && zxm_get_bool(l, l->result, "translated", &tr))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "machine") >= 0 && zxm_get_bool(l, l->result, "machine", &ma))
        return ZXM_E_PROTOCOL;
    t->no_model = false;
    *done = true;
    *machine = tr && ma;
    return ZXM_OK;
}

int ticker_heard(ticker_t *t, int64_t at_ms, uint32_t speaker, const char *text, uint32_t n)
{
    /* TM1: checked before anything is sent */
    if (!t || !t->link || !text || n == 0 || n > TICKER_TEXT_MAX) return ZXM_E_ARG;
    if (at_ms < 0 || speaker > TICKER_SPEAKER_MAX || !ticker_record_fits(at_ms, speaker, 0, 0))
        return ZXM_E_ARG;
    int64_t chars = ticker_chars(text, n);
    if (chars < 0) return ZXM_E_ARG;
    uint32_t room = TICKER_QUEUE - (t->head - t->tail);
    if (room == 0) return ZXM_E_STATE; /* TM5: full */
    int32_t k = ticker_break(text, n, t->scratch, TICKER_QUEUE);
    if (k == 0) return ZXM_E_ARG; /* nothing but spaces */
    /* TM2, TM3 */
    char from[TICKER_TAG_MAX + 1];
    int rc = detect(t, text, n, from);
    if (rc) return rc;
    bool done = false, machine = false;
    if (strcmp(from, "und") && !same_language(from, t->to)) {
        rc = translate(t, text, n, from, &done, &machine);
        if (rc) return rc;
    }
    /* TM4: the words shown, broken into captions */
    if (done) {
        k = ticker_break(t->text, len_of(t->text, TICKER_OUT_MAX), t->scratch, TICKER_QUEUE);
        if (k == 0) return ZXM_E_PROTOCOL;
    }
    if (k < 0 || (uint32_t) k > room) return ZXM_E_STATE; /* TM5: none is added */
    uint32_t lang = ticker_lang_place(t, from);
    for (int32_t i = 0; i < k; i++) {
        int64_t least = ticker_least_ms(t->scratch[i].chars);
        if (least < 0 || !ticker_record_fits(at_ms, speaker, lang, t->scratch[i].chars))
            return ZXM_E_STATE;
    }
    /* commit */
    for (int32_t i = 0; i < k; i++) {
        ticker_item_t *it = &t->ring[t->head % TICKER_QUEUE];
        it->cap = t->scratch[i];
        it->cap.at_ms = at_ms;
        it->cap.speaker = speaker;
        it->cap.lang = lang;
        memcpy(it->cap.from, from, len_of(from, TICKER_TAG_MAX) + 1u);
        it->cap.translated = done;
        it->cap.machine = machine;
        it->least_ms = ticker_least_ms(it->cap.chars);
        t->head++;
    }
    return ZXM_OK;
}

static void show_next(ticker_t *t, int64_t now_ms)
{
    t->screen = t->ring[t->tail % TICKER_QUEUE];
    memset(&t->ring[t->tail % TICKER_QUEUE], 0, sizeof t->ring[0]); /* TM7: not kept */
    t->tail++;
    t->showing = true;
    t->shown_at = now_ms;
}

int ticker_tick(ticker_t *t, int64_t now_ms, bool *moved)
{
    if (!t || !moved || now_ms < 0 || now_ms < t->now_ms) return ZXM_E_ARG;
    *moved = false;
    t->now_ms = now_ms;
    if (t->head == t->tail) return ZXM_OK;
    if (!t->showing) {
        show_next(t, now_ms);
        *moved = true;
        return ZXM_OK;
    }
    /* TM6: it leaves only when it has stayed long enough (TK2, TK3) */
    int64_t shown = now_ms - t->shown_at;
    if (shown > TICKER_SHOWN_MAX) shown = TICKER_SHOWN_MAX;
    const ticker_caption_t *c = &t->screen.cap;
    int64_t pace = ticker_pace(shown, c->chars);
    if (pace < 0 || !ticker_hold_ok(shown, pace, c->nlines, c->longest)) return ZXM_OK;
    show_next(t, now_ms);
    *moved = true;
    return ZXM_OK;
}

static bool same_caption(const ticker_caption_t *a, const ticker_caption_t *b)
{
    if (a->at_ms != b->at_ms || a->speaker != b->speaker || a->nlines != b->nlines) return false;
    for (uint32_t i = 0; i < a->nlines; i++)
        if (strcmp(a->line[i], b->line[i])) return false;
    return true;
}

int ticker_save(ticker_t *t)
{
    if (!t) return ZXM_E_ARG;
    if (!t->showing) return ZXM_E_STATE;
    const ticker_caption_t *c = &t->screen.cap;
    if (!ticker_record_fits(c->at_ms, c->speaker, c->lang, c->chars)) return ZXM_E_ARG; /* TK4 */
    for (uint32_t i = 0; i < t->nsaved; i++)
        if (same_caption(&t->saved[i], c)) return ZXM_OK;
    if (t->nsaved >= TICKER_SAVED) return ZXM_E_STATE;
    t->saved[t->nsaved++] = *c;
    return ZXM_OK;
}

void ticker_clear(ticker_t *t)
{
    if (!t) return;
    memset(t->ring, 0, sizeof t->ring);
    memset(&t->screen, 0, sizeof t->screen);
    memset(t->scratch, 0, sizeof t->scratch);
    memset(t->text, 0, sizeof t->text);
    t->head = t->tail = 0;
    t->showing = false;
}

void ticker_clear_saved(ticker_t *t)
{
    if (!t) return;
    memset(t->saved, 0, sizeof t->saved);
    t->nsaved = 0;
}
