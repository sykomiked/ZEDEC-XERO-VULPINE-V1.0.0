/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_ref.c — names, places, languages and addenda resolved to slots: the
 * deterministic step of LEXIS (alx.h). */
#include "alx_int.h"

static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool lower_word(char c)
{
    return (c >= 'a' && c <= 'z') || digit(c);
}

void alx_set_roots(alx_t *t, const uint8_t (*roots)[36], uint32_t n)
{
    if (!t) return;
    t->roots = n ? roots : 0;
    t->nroots = roots ? n : 0;
}

int alx_cid_parse(const char *s, uint32_t n, uint8_t out[36])
{
    /* "b", then 58 base32 characters (RFC 4648, lower case, no padding) */
    if (!s || !out || n != 59 || s[0] != 'b') return ALX_E_SYNTAX;
    uint32_t acc = 0, bits = 0, k = 0;
    for (uint32_t i = 1; i < n; i++) {
        char c = s[i];
        uint32_t v;
        if (c >= 'a' && c <= 'z')
            v = (uint32_t) (c - 'a');
        else if (c >= '2' && c <= '7')
            v = (uint32_t) (c - '2') + 26u;
        else
            return ALX_E_SYNTAX;
        acc = (acc << 5 | v) & 0xFFFu;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out[k++] = (uint8_t) (acc >> bits);
        }
    }
    /* 290 bits: 36 bytes and two padding bits, which must be zero */
    if (k != 36 || (acc & ((1u << bits) - 1u))) return ALX_E_SYNTAX;
    if (out[0] != 0x01 || (out[1] != 0x55 && out[1] != 0x70) || out[2] != 0x12 || out[3] != 0x20)
        return ALX_E_SYNTAX;
    return ALX_OK;
}

/* "CCC.II" at s: the place as 512 and 1, or false. */
static bool place(const char *s, uint32_t n, uint32_t *cls, uint32_t *item)
{
    if (n != 6 || s[3] != '.' || s[0] < '1' || s[0] > '9') return false;
    for (uint32_t i = 0; i < 6; i++)
        if (i != 3 && !digit(s[i])) return false;
    *cls = (uint32_t) ((s[0] - '0') * 100 + (s[1] - '0') * 10 + (s[2] - '0'));
    *item = (uint32_t) ((s[4] - '0') * 10 + (s[5] - '0'));
    return *item >= 1;
}

static int hexd(char c)
{
    if (digit(c)) return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A base reference without a language: a slot (> 0) or a status. */
static int32_t base_slot(const alx_t *t, const char *s, uint32_t n)
{
    if (n >= 3 && s[0] == '@' && s[1] == 'S' && s[2] == ':') {
        if (n != 7) return ALX_E_NAME;
        uint32_t v = 0;
        for (uint32_t i = 3; i < 7; i++) {
            int d = hexd(s[i]);
            if (d < 0) return ALX_E_NAME;
            v = v * 16u + (uint32_t) d;
        }
        if (v == 0 || v > t->slots) return ALX_E_SLOT;
        return (int32_t) v;
    }
    if (n && s[0] == '@') s++, n--;
    if (n == 0) return ALX_E_NAME;
    uint32_t cls, item;
    if (digit(s[0])) {
        if (!place(s, n, &cls, &item)) return ALX_E_NAME;
        for (uint32_t i = 1; i <= t->slots; i++) {
            const uint8_t *p = alx_slot_rec(t, i);
            if (alx_be16(p + 92) == cls && alx_be16(p + 94) == item) return (int32_t) i;
        }
        return ALX_E_NAME;
    }
    if (n > 31) return ALX_E_NAME;
    for (uint32_t i = 1; i <= t->slots; i++) {
        const uint8_t *p = alx_slot_rec(t, i) + 16;
        if (p[n] == 0 && alx_same(p, (const uint8_t *) s, n)) return (int32_t) i;
    }
    return ALX_E_NAME;
}

/* "-NNN" or ":NNN" ending s: the language number, or -1 when there is none. */
static int32_t language_suffix(const char *s, uint32_t n)
{
    if (n < 5 || (s[n - 4] != '-' && s[n - 4] != ':')) return -1;
    if (!digit(s[n - 3]) || !digit(s[n - 2]) || !digit(s[n - 1])) return -1;
    return (s[n - 3] - '0') * 100 + (s[n - 2] - '0') * 10 + (s[n - 1] - '0');
}

static int addendum(const alx_t *t, const char *s, uint32_t n, uint32_t slash, alx_ref_t *r)
{
    if (alx_cid_parse(s, slash, r->parent)) return ALX_E_SYNTAX;
    const char *p = s + slash + 1u;
    uint32_t m = n - slash - 1u;
    /* the language is required, with a colon */
    int32_t lang = language_suffix(p, m);
    if (lang < 0 || p[m - 4] != ':') return ALX_E_SYNTAX;
    m -= 4;
    uint32_t cls, item;
    if (m < 8 || m > 47 || !place(p, 6, &cls, &item) || p[6] != '.') return ALX_E_SYNTAX;
    /* the addendum's own parts: 1..8 of a-z and 0-9, separated by dots */
    uint32_t run = 0;
    for (uint32_t i = 7; i < m; i++) {
        if (p[i] == '.') {
            if (run == 0) return ALX_E_SYNTAX;
            run = 0;
        } else if (lower_word(p[i]) && run < 8) {
            run++;
        } else {
            return ALX_E_SYNTAX;
        }
    }
    if (run == 0) return ALX_E_SYNTAX;
    if (!alx_language_ok((uint32_t) lang)) return ALX_E_LANG;
    for (uint32_t i = 0; i < m; i++) r->path[i] = p[i];
    r->path[m] = 0;
    r->language = (uint32_t) lang;
    for (uint32_t i = 0; i < t->nroots; i++)
        if (alx_same(t->roots[i], r->parent, 36)) return ALX_E_OVERLAY;
    return ALX_E_LINEAGE;
}

int alx_resolve(const alx_t *t, const char *s, uint32_t n, alx_ref_t *r)
{
    if (!r) return ALX_E_NAME;
    for (uint32_t i = 0; i < sizeof r->parent; i++) r->parent[i] = 0;
    r->path[0] = 0;
    r->slot = r->language = 0;
    if (!t || !t->tbl || !s || n == 0 || n > 128) return ALX_E_NAME;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == '/') return addendum(t, s, n, i, r);
    int32_t lang = language_suffix(s, n);
    if (lang >= 0) n -= 4;
    int32_t slot = base_slot(t, s, n);
    if (slot < 0) return slot;
    r->slot = (uint32_t) slot;
    if (lang >= 0) {
        if (!alx_language_ok((uint32_t) lang)) return ALX_E_LANG;
        int st = alx_target(t, r->slot, (uint32_t) lang, 0, 0);
        if (st) return st;
        r->language = (uint32_t) lang;
    }
    return ALX_OK;
}

int32_t alx_find(const alx_t *t, const char *s, uint32_t n)
{
    if (!t || !t->tbl || !s || n == 0) return ALX_E_NAME;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == '/') return ALX_E_NAME;
    if (language_suffix(s, n) >= 0) return ALX_E_NAME;
    return base_slot(t, s, n);
}
