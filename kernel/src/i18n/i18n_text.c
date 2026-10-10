/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* i18n_text.c — UTF-8 validation, grapheme clusters, truncation, the bounded
 * writer and message formatting. See i18n.h. */
#include "i18n_internal.h"

/* ===== small helpers ===== */

uint32_t i18n__strlen(const char *s)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

bool i18n__streq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

uint32_t i18n__mod_u64(uint64_t x, uint32_t m)
{
    uint64_t r = 0;
    if (m == 0) return 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((x >> i) & 1u);
        if (r >= m) r -= m;
    }
    return (uint32_t) r;
}

/* ===== UTF-8 ===== */

int32_t i18n_utf8_decode(const uint8_t *s, uint32_t len, uint32_t *cp)
{
    uint32_t c, need, min;
    if (len == 0 || !s) {
        if (cp) *cp = 0xFFFD;
        return 0;
    }
    uint8_t b = s[0];
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if ((b & 0xE0) == 0xC0) {
        need = 1;
        c = b & 0x1Fu;
        min = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
        need = 2;
        c = b & 0x0Fu;
        min = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
        need = 3;
        c = b & 0x07u;
        min = 0x10000;
    } else {
        *cp = 0xFFFD;
        return -1;
    }
    if (len < need + 1) {
        *cp = 0xFFFD;
        return -1;
    }
    for (uint32_t i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return -1;
        }
        c = (c << 6) | (uint32_t) (s[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        *cp = 0xFFFD;
        return -1;
    }
    *cp = c;
    return (int32_t) (need + 1);
}

uint32_t i18n_utf8_encode(uint32_t cp, uint8_t out[4])
{
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    if (cp < 0x80) {
        out[0] = (uint8_t) cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t) (0xC0 | (cp >> 6));
        out[1] = (uint8_t) (0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t) (0xE0 | (cp >> 12));
        out[1] = (uint8_t) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t) (0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (uint8_t) (0xF0 | (cp >> 18));
        out[1] = (uint8_t) (0x80 | ((cp >> 12) & 0x3F));
        out[2] = (uint8_t) (0x80 | ((cp >> 6) & 0x3F));
        out[3] = (uint8_t) (0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

bool i18n_utf8_valid(const uint8_t *s, uint32_t len, uint32_t *bad)
{
    uint32_t i = 0, cp;
    while (i < len) {
        int32_t n = i18n_utf8_decode(s + i, len - i, &cp);
        if (n < 0) {
            if (bad) *bad = i;
            return false;
        }
        i += (uint32_t) n;
    }
    if (bad) *bad = len;
    return true;
}

/* ===== bounded writer ===== */

void i18n__w_init(i18n_w_t *w, char *buf, uint32_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = (buf == 0 || cap == 0);
    w->next_n = 0;
    if (buf && cap) buf[0] = 0;
}

void i18n__w_bytes(i18n_w_t *w, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (w->len + 1 >= w->cap) {
            if (!w->overflow)
                for (uint32_t j = i; j < n && w->next_n < 4; j++)
                    w->next[w->next_n++] = (uint8_t) s[j];
            w->overflow = true;
            return;
        }
        w->buf[w->len++] = s[i];
    }
}

void i18n__w_str(i18n_w_t *w, const char *s)
{
    i18n__w_bytes(w, s, i18n__strlen(s));
}

void i18n__w_cp(i18n_w_t *w, uint32_t cp)
{
    uint8_t b[4];
    uint32_t n = i18n_utf8_encode(cp, b);
    if (w->overflow) return;
    if (w->len + n + 1 > w->cap) {
        for (uint32_t j = 0; j < n; j++) w->next[j] = b[j];
        w->next_n = (uint8_t) n;
        w->overflow = true;
        return;
    }
    i18n__w_bytes(w, (const char *) b, n);
}

int32_t i18n__w_end(i18n_w_t *w)
{
    if (!w->buf || w->cap == 0) return -1;
    if (w->overflow) {
        const uint8_t *b = (const uint8_t *) w->buf;
        uint32_t n = w->len, k = n, start = 0, off = 0;
        /* drop a multi-byte sequence cut short at the end */
        while (k > 0 && (b[k - 1] & 0xC0) == 0x80 && n - k < 3) k--;
        if (k > 0 && b[k - 1] >= 0xC0) {
            uint32_t need = b[k - 1] >= 0xF0 ? 4u : b[k - 1] >= 0xE0 ? 3u : 2u;
            if (n - (k - 1) < need) n = k - 1;
        }
        /* Find the last cluster, then keep it only if the text that did not fit
         * would have started a new cluster (so no mark or conjunct is lost). */
        while (off < n) {
            uint32_t nx = i18n_grapheme_next(b, n, off);
            start = off;
            off = nx;
        }
        if (start < n) {
            uint8_t tmp[64];
            uint32_t cl = n - start, t = 0, tail = w->len - n;
            if (cl + tail + w->next_n <= sizeof tmp) {
                for (uint32_t j = 0; j < cl + tail; j++) tmp[t++] = b[start + j];
                for (uint32_t j = 0; j < w->next_n; j++) tmp[t++] = w->next[j];
                if (tail + w->next_n > 0 && i18n_grapheme_next(tmp, t, 0) == cl) start = n;
            }
        }
        w->len = start;
        w->buf[start] = 0;
        return -1;
    }
    w->buf[w->len] = 0;
    return (int32_t) w->len;
}

/* ===== grapheme clusters (UAX #29, extended) ===== */

bool i18n_is_grapheme_extend(uint32_t cp)
{
    uint32_t lo = 0, hi = i18n_extend_count;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        if (cp < i18n_extend[mid].lo)
            hi = mid;
        else if (cp > i18n_extend[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

static bool is_ri(uint32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

static bool is_prepend(uint32_t cp)
{
    return (cp >= 0x0600 && cp <= 0x0605) || cp == 0x06DD || cp == 0x070F || cp == 0x0890 ||
           cp == 0x0891 || cp == 0x08E2 || cp == 0x0D4E || cp == 0x110BD || cp == 0x110CD;
}

static bool is_ext_pict(uint32_t cp)
{
    return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x2B00 && cp <= 0x2BFF) || cp == 0x00A9 || cp == 0x00AE || cp == 0x203C ||
           cp == 0x2049 || cp == 0x2122;
}

/* Hangul syllable types: 1 L, 2 V, 3 T, 4 LV, 5 LVT, 0 none. */
static int hangul(uint32_t cp)
{
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0xA960 && cp <= 0xA97C)) return 1;
    if ((cp >= 0x1160 && cp <= 0x11A7) || (cp >= 0xD7B0 && cp <= 0xD7C6)) return 2;
    if ((cp >= 0x11A8 && cp <= 0x11FF) || (cp >= 0xD7CB && cp <= 0xD7FB)) return 3;
    if (cp >= 0xAC00 && cp <= 0xD7A3) {
        uint32_t s = cp - 0xAC00;
        uint32_t t = s - (s / 28u) * 28u; /* 32-bit division only */
        return t == 0 ? 4 : 5;
    }
    return 0;
}

/* Indic conjunct break (GB9c, Unicode 15.1): virama linker + consonant. */
static bool is_linker(uint32_t cp)
{
    return cp == 0x094D || cp == 0x09CD || cp == 0x0ACD || cp == 0x0B4D || cp == 0x0C4D ||
           cp == 0x0D4D;
}

static bool is_incb_consonant(uint32_t cp)
{
    return (cp >= 0x0915 && cp <= 0x0939) || (cp >= 0x0958 && cp <= 0x095F) ||
           (cp >= 0x0978 && cp <= 0x097F) || (cp >= 0x0995 && cp <= 0x09B9) ||
           (cp >= 0x09DC && cp <= 0x09DF) || cp == 0x09F0 || cp == 0x09F1 ||
           (cp >= 0x0A95 && cp <= 0x0AB9) || cp == 0x0AF9 || (cp >= 0x0B15 && cp <= 0x0B39) ||
           (cp >= 0x0B5C && cp <= 0x0B5F) || cp == 0x0B71 || (cp >= 0x0C15 && cp <= 0x0C39) ||
           (cp >= 0x0C58 && cp <= 0x0C5A) || (cp >= 0x0D15 && cp <= 0x0D3A);
}

static uint32_t peek(const uint8_t *s, uint32_t len, uint32_t off, uint32_t *cp)
{
    int32_t n = i18n_utf8_decode(s + off, len - off, cp);
    return n <= 0 ? 1u : (uint32_t) n;
}

uint32_t i18n_grapheme_next(const uint8_t *s, uint32_t len, uint32_t off)
{
    uint32_t cp, nx, prev;
    if (!s || off >= len) return len;
    int32_t first = i18n_utf8_decode(s + off, len - off, &cp);
    if (first < 0) return off + 1; /* a malformed byte is its own cluster */
    off += (uint32_t) first;
    if (cp == 0x0D) {
        if (off < len && s[off] == 0x0A) off++;
        return off;
    }
    if (cp == 0x0A || cp < 0x20 || cp == 0x7F) return off;
    /* GB9b: Prepend joins the following character */
    while (is_prepend(cp) && off < len) {
        uint32_t n = peek(s, len, off, &nx);
        if (nx < 0x20) break;
        off += n;
        cp = nx;
    }
    prev = cp;
    bool ri_open = is_ri(cp);
    bool pict = is_ext_pict(cp);
    bool linker_seen = false;
    bool consonant = is_incb_consonant(cp);
    while (off < len) {
        uint32_t n = peek(s, len, off, &nx);
        if (nx == 0xFFFD && n == 1 && s[off] >= 0x80) break; /* malformed: boundary */
        int hp = hangul(prev), hn = hangul(nx);
        bool join = false;
        if (i18n_is_grapheme_extend(nx) || nx == 0x200D)
            join = true; /* GB9, GB9a */
        else if (ri_open && is_ri(nx))
            join = true; /* GB12/13: pair */
        else if (hp == 1 && (hn == 1 || hn == 2 || hn == 4 || hn == 5))
            join = true; /* GB6 */
        else if ((hp == 2 || hp == 4) && (hn == 2 || hn == 3))
            join = true; /* GB7 */
        else if ((hp == 3 || hp == 5) && hn == 3)
            join = true; /* GB8 */
        else if (prev == 0x200D && pict && is_ext_pict(nx))
            join = true; /* GB11 */
        else if (consonant && linker_seen && is_incb_consonant(nx))
            join = true; /* GB9c */
        if (!join) break;
        if (ri_open && is_ri(nx)) ri_open = false;
        if (is_linker(nx)) linker_seen = true;
        if (is_incb_consonant(nx)) linker_seen = false;
        off += n;
        prev = nx;
    }
    return off;
}

uint32_t i18n_grapheme_count(const uint8_t *s, uint32_t len)
{
    uint32_t n = 0, off = 0;
    while (off < len) {
        off = i18n_grapheme_next(s, len, off);
        n++;
    }
    return n;
}

uint32_t i18n_truncate(const uint8_t *s, uint32_t len, uint32_t max_bytes)
{
    uint32_t off = 0, last = 0;
    if (max_bytes >= len) {
        /* still make sure the string itself does not end mid-cluster */
        max_bytes = len;
    }
    while (off < len) {
        uint32_t nx = i18n_grapheme_next(s, len, off);
        if (nx > max_bytes) break;
        last = nx;
        off = nx;
    }
    return last;
}

int32_t i18n_truncate_ellipsis(const uint8_t *s, uint32_t len, uint32_t max_bytes, char *out,
                               uint32_t cap)
{
    i18n_w_t w;
    i18n__w_init(&w, out, cap);
    if (i18n_truncate(s, len, max_bytes) == len) {
        i18n__w_bytes(&w, (const char *) s, len);
    } else if (max_bytes >= 3) {
        uint32_t keep = i18n_truncate(s, len, max_bytes - 3);
        i18n__w_bytes(&w, (const char *) s, keep);
        i18n__w_str(&w, "\xE2\x80\xA6");
    }
    return i18n__w_end(&w);
}

int32_t i18n_utf8_sanitize(const uint8_t *s, uint32_t len, char *out, uint32_t cap)
{
    i18n_w_t w;
    uint32_t i = 0, cp;
    i18n__w_init(&w, out, cap);
    while (i < len && !w.overflow) {
        int32_t n = i18n_utf8_decode(s + i, len - i, &cp);
        if (n < 0) {
            i18n__w_cp(&w, 0xFFFD);
            i++;
        } else {
            i18n__w_bytes(&w, (const char *) s + i, (uint32_t) n);
            i += (uint32_t) n;
        }
    }
    return i18n__w_end(&w);
}

/* ===== message formatting ===== */

static bool strong_rtl(uint32_t cp)
{
    return (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
           (cp >= 0xFE70 && cp <= 0xFEFF) || (cp >= 0x10800 && cp <= 0x10FFF) ||
           (cp >= 0x1E800 && cp <= 0x1EFFF);
}

static bool has_rtl(const char *s)
{
    uint32_t len = i18n__strlen(s), i = 0, cp;
    while (i < len) {
        int32_t n = i18n_utf8_decode((const uint8_t *) s + i, len - i, &cp);
        if (n <= 0) {
            i++;
            continue;
        }
        if (strong_rtl(cp)) return true;
        i += (uint32_t) n;
    }
    return false;
}

int32_t i18n_format_msg(const i18n_locale_t *l, const char *tmpl, const char *const *args,
                        uint32_t nargs, char *out, uint32_t cap)
{
    i18n_w_t w;
    bool rtl = l && i18n_locale_rtl(l);
    i18n__w_init(&w, out, cap);
    if (!tmpl) return i18n__w_end(&w);
    for (uint32_t i = 0; tmpl[i] && !w.overflow; i++) {
        char c = tmpl[i];
        if (c == '{' && tmpl[i + 1] >= '0' && tmpl[i + 1] <= '9' && tmpl[i + 2] == '}') {
            uint32_t k = (uint32_t) (tmpl[i + 1] - '0');
            const char *a = (k < nargs && args && args[k]) ? args[k] : "";
            bool iso = rtl || has_rtl(a);
            if (iso) i18n__w_cp(&w, 0x2068); /* FSI */
            i18n__w_str(&w, a);
            if (iso) i18n__w_cp(&w, 0x2069); /* PDI */
            i += 2;
            continue;
        }
        i18n__w_bytes(&w, &c, 1);
    }
    return i18n__w_end(&w);
}
