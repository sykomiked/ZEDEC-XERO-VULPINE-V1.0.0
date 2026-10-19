/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_tidy.c — what a person would say. See speech_tidy.h. */
#include "speech_tidy.h"

const speech_tidy_words_t speech_tidy_en = {
    "en",
    "a hash",
    "%u hashes",
    "an ID",
    "%u IDs",
    "a code",
    "%u codes",
    "a long number",
    "%u long numbers",
    "a link",
    "a link to %s",
    "the file %s",
    "the folder %s",
    "an email address",
    "at",
    "dot",
    "number",
    "There is some code here.",
    "capital",
    "dash",
    "underscore",
    "slash",
    "colon",
    "plus",
    "equals",
    "dot",
};

/* ---- output ------------------------------------------------------------------ */

typedef struct {
    char *p;
    uint32_t cap, n;
    bool full;
} ob_t;

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    if (s)
        while (s[n]) n++;
    return n;
}

static void put(ob_t *o, const char *s, uint32_t k)
{
    if (o->full || !k) return;
    if (o->n + k > o->cap - 1u) { /* cut at a character */
        uint32_t j = o->cap - 1u - o->n;
        while (j && ((uint8_t) s[j] & 0xC0u) == 0x80u) j--;
        for (uint32_t i = 0; i < j; i++) o->p[o->n + i] = s[i];
        o->n += j;
        o->full = true;
    } else {
        for (uint32_t i = 0; i < k; i++) o->p[o->n + i] = s[i];
        o->n += k;
    }
    o->p[o->n] = 0;
}

static void puts_(ob_t *o, const char *s)
{
    put(o, s, slen(s));
}

static void putu(ob_t *o, uint32_t u)
{
    char b[10];
    uint32_t k = 0;
    do b[k++] = (char) ('0' + u % 10u);
    while ((u /= 10u) && k < sizeof b);
    char r[10];
    for (uint32_t i = 0; i < k; i++) r[i] = b[k - 1 - i];
    put(o, r, k);
}

/* a pattern with one %s (s, k bytes) and/or one %u */
static void putf(ob_t *o, const char *pat, const char *s, uint32_t k, uint32_t u)
{
    if (!pat) return;
    for (const char *q = pat; *q; q++) {
        if (q[0] == '%' && q[1] == 's') {
            put(o, s, k);
            q++;
        } else if (q[0] == '%' && q[1] == 'u') {
            putu(o, u);
            q++;
        } else {
            put(o, q, 1);
        }
    }
}

/* a space before the next word, unless at the start or after one */
static void sep(ob_t *o)
{
    if (o->n && o->p[o->n - 1] != ' ') put(o, " ", 1);
}

/* ---- what a token is ---------------------------------------------------------- */

static bool digit(char c)
{
    return c >= '0' && c <= '9';
}
static bool upper(char c)
{
    return c >= 'A' && c <= 'Z';
}
static bool lower(char c)
{
    return c >= 'a' && c <= 'z';
}
static bool alpha(char c)
{
    return upper(c) || lower(c);
}
static bool hexd(char c)
{
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static bool in(char c, const char *set)
{
    for (; *set; set++)
        if (c == *set) return true;
    return false;
}
static char low(char c)
{
    return upper(c) ? (char) (c - 'A' + 'a') : c;
}

static bool ci_starts(const char *t, uint32_t n, const char *p)
{
    uint32_t k = slen(p);
    if (n < k) return false;
    for (uint32_t i = 0; i < k; i++)
        if (low(t[i]) != p[i]) return false;
    return true;
}

/* TD1: a part of a token that looks random rather than like a word */
static bool random_part(const char *t, uint32_t n)
{
    uint32_t l = 0, d = 0, trans = 0, cases = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (digit(t[i])) d++;
        if (alpha(t[i])) l++;
        if (i && (digit(t[i]) != digit(t[i - 1])) && (digit(t[i]) || alpha(t[i])) &&
            (digit(t[i - 1]) || alpha(t[i - 1])))
            trans++;
        if (i && alpha(t[i]) && alpha(t[i - 1]) && upper(t[i]) != upper(t[i - 1])) cases++;
    }
    return (n >= 10 && d >= 2 && l >= 1 && trans >= 3) || (n >= 16 && cases >= 7) ||
           (n >= 24 && l && d);
}

speech_tk_t speech_tidy_kind(const char *t, uint32_t n)
{
    if (!t || !n) return SPEECH_TK_JUNK;
    bool junk = true, any_alpha = false, all_digit = true;
    uint32_t seps = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = t[i];
        if (!in(c, n == 1 ? "-=*_#~|`^:" : "-=*_#~|`<>+^.:")) junk = false;
        if (alpha(c)) any_alpha = true;
        if (!digit(c)) all_digit = false;
        if (c == '/' || c == '\\') seps++;
    }
    if (junk) return SPEECH_TK_JUNK;
    if (ci_starts(t, n, "http://") || ci_starts(t, n, "https://") || ci_starts(t, n, "ftp://") ||
        ci_starts(t, n, "file://") || (n > 4 && ci_starts(t, n, "www.")))
        return SPEECH_TK_LINK;
    for (uint32_t i = 1; i + 1 < n; i++)
        if (t[i] == '@') {
            bool dot = false, slash = false;
            for (uint32_t j = i + 1; j < n; j++) {
                if (t[j] == '.' && j > i + 1 && j + 1 < n) dot = true;
                if (t[j] == '/' || t[j] == '@') slash = true;
            }
            if (dot && !slash && !seps) return SPEECH_TK_EMAIL;
            break;
        }
    if (n == 36 && t[8] == '-' && t[13] == '-' && t[18] == '-' && t[23] == '-') {
        bool ok = true;
        for (uint32_t i = 0; i < 36; i++)
            if (i != 8 && i != 13 && i != 18 && i != 23 && !hexd(t[i])) ok = false;
        if (ok) return SPEECH_TK_ID;
    }
    uint32_t off = n > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X') ? 2u : 0u;
    {
        uint32_t d = 0, l = 0;
        bool hex = true;
        for (uint32_t i = off; i < n; i++) {
            if (!hexd(t[i])) hex = false;
            if (digit(t[i]))
                d++;
            else
                l++;
        }
        uint32_t m = n - off;
        if (hex && ((off && m >= 9) || (!off && m >= 7 && d && l))) return SPEECH_TK_HASH;
    }
    if (all_digit) return n >= 13 ? SPEECH_TK_NUMBER : SPEECH_TK_WORD;
    /* random parts, split at - _ . / + = */
    bool rnd = false;
    for (uint32_t s = 0; s < n && !rnd;) {
        uint32_t e = s;
        while (e < n && !in(t[e], "-_./+=\\")) e++;
        if (random_part(t + s, e - s)) rnd = true;
        s = e + 1;
    }
    if (rnd) {
        /* a word in front ("cmsg_", "sk-") makes it an id */
        uint32_t k = 0;
        while (k < n && alpha(t[k])) k++;
        return k && k < n && (t[k] == '_' || t[k] == '-') ? SPEECH_TK_ID : SPEECH_TK_CODE;
    }
    if (any_alpha && (t[0] == '/' || seps >= 2 || (t[0] == '~' && n > 1 && t[1] == '/') ||
                      (t[0] == '.' && seps) || (n > 2 && alpha(t[0]) && t[1] == ':' && seps)))
        return SPEECH_TK_PATH;
    return SPEECH_TK_WORD;
}

/* ---- saying one token ---------------------------------------------------------- */

typedef struct {
    const speech_tidy_words_t *w;
    speech_tk_t run_kind;
    uint32_t run_count, run_at;
    bool run_open;
} st_t;

#define KEEP_LEAD  "([{\"'<"
#define DROP_EDGE  "`*_"
#define KEEP_TRAIL ")]}\"'>.,;:!?"

static bool countable(speech_tk_t k)
{
    return k == SPEECH_TK_HASH || k == SPEECH_TK_ID || k == SPEECH_TK_CODE || k == SPEECH_TK_NUMBER;
}

static void counted(ob_t *o, const speech_tidy_words_t *w, speech_tk_t k, uint32_t c)
{
    const char *one = k == SPEECH_TK_HASH   ? w->hash
                      : k == SPEECH_TK_ID   ? w->id
                      : k == SPEECH_TK_CODE ? w->code
                                            : w->number;
    const char *many = k == SPEECH_TK_HASH   ? w->hashes
                       : k == SPEECH_TK_ID   ? w->ids
                       : k == SPEECH_TK_CODE ? w->codes
                                             : w->numbers;
    putf(o, c == 1 ? one : many, 0, 0, c);
}

/* letters joined by spaces where the token had underscores, when every part
 * is a word ("zxv_policy" -> "zxv policy") */
static void put_name(ob_t *o, const char *t, uint32_t n)
{
    bool words = false, ok = true;
    for (uint32_t i = 0; i < n; i++) {
        if (t[i] == '_') {
            words = true;
            if (!i || i + 1 == n || t[i + 1] == '_') ok = false;
        } else if (!alpha(t[i]) && t[i] != '.') {
            ok = false;
        }
    }
    if (!words || !ok) {
        put(o, t, n);
        return;
    }
    for (uint32_t i = 0; i < n; i++) put(o, t[i] == '_' ? " " : t + i, 1);
}

static void say_link(ob_t *o, const speech_tidy_words_t *w, const char *t, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i + 2 < n; i++)
        if (t[i] == ':' && t[i + 1] == '/' && t[i + 2] == '/') {
            s = i + 3;
            break;
        }
    if (ci_starts(t + s, n - s, "www.")) s += 4;
    uint32_t e = s;
    while (e < n && !in(t[e], "/?#:@")) e++;
    if (e < n && t[e] == '@') { /* user@host: the host */
        s = e + 1;
        for (e = s; e < n && !in(t[e], "/?#:"); e++) {
        }
    }
    if (e > s && speech_tidy_kind(t + s, e - s) == SPEECH_TK_WORD)
        putf(o, w->link_to, t + s, e - s, 0);
    else
        puts_(o, w->link);
}

static void say_path(ob_t *o, const speech_tidy_words_t *w, const char *t, uint32_t n)
{
    bool folder = false;
    while (n > 1 && (t[n - 1] == '/' || t[n - 1] == '\\')) n--, folder = true;
    uint32_t s = n;
    while (s && t[s - 1] != '/' && t[s - 1] != '\\') s--;
    if (s == n) {
        put(o, t, n);
        return;
    }
    char name[96];
    ob_t nb = {name, sizeof name, 0, false};
    name[0] = 0;
    put_name(&nb, t + s, n - s);
    putf(o, folder ? w->folder : w->file, name, nb.n, 0);
}

static void say_email(ob_t *o, const speech_tidy_words_t *w, const char *t, uint32_t n)
{
    uint32_t a = 0;
    while (a < n && t[a] != '@') a++;
    if (countable(speech_tidy_kind(t, a))) {
        puts_(o, w->email);
        return;
    }
    put_name(o, t, a);
    put(o, " ", 1);
    puts_(o, w->at);
    put(o, " ", 1);
    for (uint32_t i = a + 1; i < n; i++) {
        if (t[i] == '.') {
            put(o, " ", 1);
            puts_(o, w->dot);
            put(o, " ", 1);
        } else {
            put(o, t + i, 1);
        }
    }
}

static void say_token(ob_t *o, st_t *st, const char *t, uint32_t n)
{
    const speech_tidy_words_t *w = st->w;
    uint32_t i = 0, j = n;
    char lead[16];
    uint32_t nl = 0;
    while (i < n && (in(t[i], KEEP_LEAD) || in(t[i], DROP_EDGE))) {
        if (in(t[i], KEEP_LEAD) && nl < sizeof lead) lead[nl++] = t[i];
        i++;
    }
    while (j > i && (in(t[j - 1], KEEP_TRAIL) || in(t[j - 1], DROP_EDGE))) j--;
    const char *c = t + i;
    uint32_t m = j - i;
    speech_tk_t k = speech_tidy_kind(c, m);
    if (k == SPEECH_TK_JUNK) return;                         /* TD5: with its punctuation */
    if (countable(k) && st->run_open && st->run_kind == k) { /* TD2 */
        o->n = st->run_at;
        o->p[o->n] = 0;
        o->full = false;
        counted(o, w, k, ++st->run_count);
    } else {
        sep(o);
        put(o, lead, nl);
        if (countable(k)) {
            st->run_at = o->n;
            st->run_kind = k;
            st->run_count = 1;
            st->run_open = true;
            counted(o, w, k, 1);
        } else {
            st->run_open = false;
            if (k == SPEECH_TK_LINK)
                say_link(o, w, c, m);
            else if (k == SPEECH_TK_PATH)
                say_path(o, w, c, m);
            else if (k == SPEECH_TK_EMAIL)
                say_email(o, w, c, m);
            else if (m > 1 && c[0] == '#' && digit(c[1])) { /* "#153" */
                puts_(o, w->number_sign);
                put(o, " ", 1);
                put(o, c + 1, m - 1);
            } else
                put_name(o, c, m);
        }
    }
    for (uint32_t q = j; q < n; q++) {
        if (in(t[q], DROP_EDGE)) continue;
        put(o, t + q, 1);
        if (t[q] != ',' && st->run_open) st->run_open = false; /* a sentence ended */
    }
}

/* ---- lines ---------------------------------------------------------------------- */

static void pause_(ob_t *o)
{
    if (o->n && !in(o->p[o->n - 1], ".!?:;")) {
        while (o->n && o->p[o->n - 1] == ' ') o->n--;
        o->p[o->n] = 0;
        put(o, ".", 1);
    }
}

typedef enum { L_PLAIN = 0, L_HEAD, L_BULLET } line_t;

uint32_t speech_tidy(const char *in_, uint32_t n, char *out, uint32_t cap,
                     const speech_tidy_words_t *w)
{
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!in_) return 0;
    if (!w) w = &speech_tidy_en;
    ob_t o = {out, cap, 0, false};
    st_t st = {w, SPEECH_TK_WORD, 0, 0, false};
    bool fence = false, pend = false;
    for (uint32_t ls = 0; ls < n;) {
        uint32_t le = ls;
        while (le < n && in_[le] != '\n') le++;
        uint32_t p = ls;
        while (p < le && (in_[p] == ' ' || in_[p] == '\t' || in_[p] == '\r')) p++;
        uint32_t next = le + 1;
        if (le - p >= 3 && ((in_[p] == '`' && in_[p + 1] == '`' && in_[p + 2] == '`') ||
                            (in_[p] == '~' && in_[p + 1] == '~' && in_[p + 2] == '~'))) {
            if (!fence) { /* TD4 */
                if (pend) pause_(&o);
                sep(&o);
                puts_(&o, w->code_block);
                pend = false;
                st.run_open = false;
            }
            fence = !fence;
            ls = next;
            continue;
        }
        if (fence) {
            ls = next;
            continue;
        }
        if (p == le) { /* a blank line ends a paragraph */
            if (pend) pause_(&o);
            pend = false;
            st.run_open = false;
            ls = next;
            continue;
        }
        line_t kind = L_PLAIN;
        uint32_t h = p;
        while (h < le && in_[h] == '#') h++;
        if (h > p && h - p <= 6 && (h == le || in_[h] == ' ')) {
            kind = L_HEAD;
            p = h;
        }
        while (p < le && (in_[p] == '>' || in_[p] == ' ')) p++;
        if (kind == L_PLAIN && p + 1 < le && in(in_[p], "-*+") && in_[p + 1] == ' ') {
            kind = L_BULLET;
            p += 2;
        }
        if (pend && kind != L_PLAIN) pause_(&o);
        uint32_t before = o.n;
        for (uint32_t t = p; t < le;) {
            while (t < le &&
                   (in_[t] == ' ' || in_[t] == '\t' || in_[t] == '\r' || (uint8_t) in_[t] < 0x20u))
                t++;
            uint32_t e = t;
            while (e < le && in_[e] != ' ' && in_[e] != '\t' && in_[e] != '\r' &&
                   (uint8_t) in_[e] >= 0x20u)
                e++;
            if (e > t) say_token(&o, &st, in_ + t, e - t);
            t = e;
        }
        if (o.n > before) {
            pend = !in(o.p[o.n - 1], ".!?:;");
            if (kind == L_HEAD) { /* one line: pause now (a bullet may wrap) */
                pause_(&o);
                pend = false;
            }
        }
        ls = next;
    }
    while (o.n && o.p[o.n - 1] == ' ') o.n--;
    o.p[o.n] = 0;
    return o.n;
}

/* ---- TD6: spelled ---------------------------------------------------------------- */

static void spell_core(ob_t *o, const speech_tidy_words_t *w, const char *t, uint32_t n)
{
    bool up = false, lo = false;
    for (uint32_t i = 0; i < n; i++) {
        if (upper(t[i])) up = true;
        if (lower(t[i])) lo = true;
    }
    uint32_t g = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = t[i];
        const char *name = c == '-'   ? w->dash
                           : c == '_' ? w->underscore
                           : c == '/' ? w->slash
                           : c == ':' ? w->colon
                           : c == '+' ? w->plus
                           : c == '=' ? w->equals
                           : c == '.' ? w->period
                                      : 0;
        bool group = g && g % 4u == 0 && (digit(c) || alpha(c));
        if (i) puts_(o, group ? ", " : " ");
        if (name) {
            puts_(o, name);
            g = 0;
            continue;
        }
        if (upper(c) && up && lo) {
            puts_(o, w->capital);
            put(o, " ", 1);
        }
        put(o, t + i, 1);
        if (digit(c) || alpha(c)) g++;
    }
}

uint32_t speech_spell(const char *in_, uint32_t n, char *out, uint32_t cap,
                      const speech_tidy_words_t *w)
{
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!in_) return 0;
    if (!w) w = &speech_tidy_en;
    ob_t o = {out, cap, 0, false};
    for (uint32_t t = 0; t < n;) {
        while (t < n && ((uint8_t) in_[t] <= 0x20u)) t++;
        uint32_t e = t;
        while (e < n && (uint8_t) in_[e] > 0x20u) e++;
        if (e == t) break;
        uint32_t i = t, j = e;
        while (i < e && (in(in_[i], KEEP_LEAD) || in(in_[i], DROP_EDGE))) i++;
        while (j > i && (in(in_[j - 1], KEEP_TRAIL) || in(in_[j - 1], DROP_EDGE))) j--;
        sep(&o);
        if (countable(speech_tidy_kind(in_ + i, j - i))) {
            for (uint32_t q = t; q < i; q++)
                if (!in(in_[q], DROP_EDGE)) put(&o, in_ + q, 1);
            spell_core(&o, w, in_ + i, j - i);
            for (uint32_t q = j; q < e; q++)
                if (!in(in_[q], DROP_EDGE)) put(&o, in_ + q, 1);
        } else {
            put(&o, in_ + t, e - t);
        }
        t = e;
    }
    return o.n;
}

uint32_t speech_tidy_fn(const char *in_, uint32_t n, char *out, uint32_t cap, const void *words)
{
    return speech_tidy(in_, n, out, cap, (const speech_tidy_words_t *) words);
}
