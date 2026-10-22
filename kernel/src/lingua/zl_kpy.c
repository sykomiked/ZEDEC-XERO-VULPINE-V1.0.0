/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_kpy.c — Kaṭapayādi numbers and digital-root labels (zl_kpy.h). */
#include "zl_kpy.h"

enum { K_BAD = -1, K_VOWEL = -2, K_SKIP = -3 };

/* one UTF-8 code point at s, its length in *n; -1 if malformed */
static int32_t cp(const unsigned char *s, uint32_t *n)
{
    if (s[0] < 0x80u) return *n = 1, (int32_t) s[0];
    if ((s[0] & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u)
        return *n = 2, (int32_t) ((s[0] & 0x1Fu) << 6 | (s[1] & 0x3Fu));
    if ((s[0] & 0xF0u) == 0xE0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u)
        return *n = 3, (int32_t) ((s[0] & 0x0Fu) << 12 | (s[1] & 0x3Fu) << 6 | (s[2] & 0x3Fu));
    return -1;
}

/* the digit of a consonant (aspirated when h follows), K_VOWEL, K_SKIP or
 * K_BAD */
static int32_t digit(int32_t c, int aspirated)
{
    switch (c) {
    case 'k':
        return aspirated ? 2 : 1;
    case 'g':
        return aspirated ? 4 : 3;
    case 0x1E45:
        return 5; /* ṅ */
    case 'c':
        return aspirated ? 7 : 6;
    case 'j':
        return aspirated ? 9 : 8;
    case 0x00F1:
        return 0; /* ñ */
    case 0x1E6D:
        return aspirated ? 2 : 1; /* ṭ */
    case 0x1E0D:
        return aspirated ? 4 : 3; /* ḍ */
    case 0x1E47:
        return 5; /* ṇ */
    case 't':
        return aspirated ? 7 : 6;
    case 'd':
        return aspirated ? 9 : 8;
    case 'n':
        return 0;
    case 'p':
        return aspirated ? 2 : 1;
    case 'b':
        return aspirated ? 4 : 3;
    case 'm':
        return 5;
    case 'y':
        return 1;
    case 'r':
        return 2;
    case 'l':
        return 3;
    case 'v':
        return 4;
    case 0x015B:
        return 5; /* ś */
    case 0x1E63:
        return 6; /* ṣ */
    case 's':
        return 7;
    case 'h':
        return 8;
    case 'a':
    case 'i':
    case 'u':
    case 'e':
    case 'o':
    case 0x0101:
    case 0x012B:
    case 0x016B: /* ā ī ū */
    case 0x1E5B:
    case 0x1E5D:
    case 0x1E37:
    case 0x1E39: /* ṛ ṝ ḷ ḹ */
        return K_VOWEL;
    case 0x1E43:
    case 0x1E25:
        return K_SKIP; /* ṃ ḥ */
    default:
        return K_BAD;
    }
}

static int aspirable(int32_t c)
{
    return c == 'k' || c == 'g' || c == 'c' || c == 'j' || c == 0x1E6D || c == 0x1E0D || c == 't' ||
           c == 'd' || c == 'p' || c == 'b';
}

int32_t zl_kpy_digits(const char *iast, uint8_t *d, uint32_t cap)
{
    if (!iast) return -1;
    const unsigned char *s = (const unsigned char *) iast;
    uint32_t nd = 0;
    int32_t pend = -1; /* the last consonant of the group being read */
    while (*s) {
        uint32_t n;
        int32_t c = cp(s, &n);
        if (c < 0) return -1;
        s += n;
        int asp = 0;
        if (aspirable(c) && *s == 'h') asp = 1, s++;
        int32_t k = digit(c, asp);
        if (k == K_BAD) return -1;
        if (k == K_SKIP) continue;
        if (k == K_VOWEL) {
            if (nd >= cap) return -1;
            d[nd++] = (uint8_t) (pend >= 0 ? pend : 0); /* a bare vowel counts 0 */
            pend = -1;
            continue;
        }
        pend = k;
    }
    if (pend >= 0) { /* a final consonant: counted (the bare root form) */
        if (nd >= cap) return -1;
        d[nd++] = (uint8_t) pend;
    }
    return (int32_t) nd;
}

int32_t zl_kpy_value(const char *iast, uint64_t *v)
{
    uint8_t d[ZL_KPY_MAXDIG];
    int32_t n = zl_kpy_digits(iast, d, ZL_KPY_MAXDIG);
    if (n < 0 || !v) return -1;
    uint64_t x = 0;
    for (int32_t i = n; i-- > 0;) x = x * 10u + d[i];
    *v = x;
    return n;
}

int32_t zl_kpy_spell(uint64_t v, uint32_t n, char *out, uint32_t cap)
{
    static const char *const SYL[10] = {"na", "ka", "kha", "ga", "gha",
                                        "ma", "ca", "cha", "ja", "jha"};
    if (!out || n < 1u || n > ZL_KPY_MAXDIG) return -1;
    uint32_t at = 0;
    for (uint32_t i = 0; i < n; i++, v /= 10u) {
        const char *p = SYL[v % 10u];
        while (*p) {
            if (at + 1u >= cap) return -1;
            out[at++] = *p++;
        }
    }
    if (v || at >= cap) return -1;
    out[at] = 0;
    return (int32_t) at;
}

uint32_t zl_dr9(uint64_t n)
{
    return n ? 1u + (uint32_t) ((n - 1u) % 9u) : 0u;
}

uint32_t zl_mod_be(const uint8_t *b, uint32_t len, uint32_t m)
{
    if (!b || m < 1u || m > (1u << 24)) return 0;
    uint32_t r = 0;
    for (uint32_t i = 0; i < len; i++) r = (r * 256u + b[i]) % m;
    return r;
}

uint32_t zl_dr9_be(const uint8_t *b, uint32_t len)
{
    uint32_t any = 0;
    for (uint32_t i = 0; b && i < len; i++) any |= b[i];
    if (!any) return 0;
    uint32_t r = zl_mod_be(b, len, 9u);
    return r ? r : 9u;
}
