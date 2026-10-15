/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx.c — the Library of Alexandria's index (alx.h). */
#include "alx.h"
#include "../robin_debanks/sha256.h"

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t) (p[0] << 8 | p[1]);
}

static bool zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return false;
    return true;
}

static bool word(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/* A NUL-padded "a.b" field of cap bytes: its length, or 0 when malformed.
 * dots: the most dots allowed after the first (block.entry may nest). */
static uint32_t name_field(const uint8_t *p, uint32_t cap, bool more_dots)
{
    uint32_t n = 0;
    while (n < cap && p[n]) n++;
    if (n == 0 || n == cap || !zero(p + n, cap - n)) return 0;
    if (!(p[0] >= 'a' && p[0] <= 'z')) return 0;
    uint32_t dots = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] == '.') {
            if (i == 0 || i + 1 == n || p[i - 1] == '.') return 0;
            dots++;
        } else if (!word((char) p[i])) {
            return 0;
        }
    }
    return dots == 1 || (more_dots && dots > 1) ? n : 0;
}

static bool same_bytes(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static int check_entry(const uint8_t *p, uint32_t slot)
{
    if (be32(p) != slot) return ALX_E_FORM;
    uint8_t kind = p[4], inputs = p[5], flags = p[6], langs = p[7];
    if (flags & ~(ALX_F_RETIRED | ALX_F_WITNESSED)) return ALX_E_FORM;
    if (!zero(p + 14, 2)) return ALX_E_FORM;
    if (!name_field(p + 16, 32, false) || !name_field(p + 48, 44, true)) return ALX_E_FORM;
    const uint8_t *cid = p + 92;
    if (flags & ALX_F_RETIRED) {
        /* a retired slot keeps its names and nothing else */
        if (flags != ALX_F_RETIRED || kind || inputs || langs || !zero(p + 8, 6) || !zero(cid, 36))
            return ALX_E_FORM;
        return ALX_OK;
    }
    if (kind != ALX_NUM && kind != ALX_TRUTH) return ALX_E_FORM;
    if (inputs > 15 || langs == 0) return ALX_E_FORM;
    if (((flags & ALX_F_WITNESSED) != 0) != (langs >= 2)) return ALX_E_FORM;
    if (cid[0] != 0x01 || cid[1] != 0x55 || cid[2] != 0x12 || cid[3] != 0x20) return ALX_E_FORM;
    return ALX_OK;
}

int alx_open(alx_t *t, const uint8_t *tbl, uint32_t len)
{
    if (!t) return ALX_E_FORM;
    t->tbl = 0;
    t->count = 0;
    if (!tbl || len < ALX_HEAD + 32u) return ALX_E_FORM;
    if (tbl[0] != 'A' || tbl[1] != 'L' || tbl[2] != 'X' || tbl[3] != 'T') return ALX_E_FORM;
    if (tbl[4] != 1 || !zero(tbl + 5, 3) || !zero(tbl + 20, 12)) return ALX_E_FORM;
    uint32_t count = be32(tbl + 8);
    if (be32(tbl + 12) != ALX_ENTRY || be32(tbl + 16) != ALX_BASE_END) return ALX_E_FORM;
    if (count > ALX_MAX || len != ALX_HEAD + count * ALX_ENTRY + 32u) return ALX_E_FORM;
    uint8_t h[SHA256_DIGEST_LEN];
    sha256(tbl, len - 32u, h);
    if (!same_bytes(h, tbl + len - 32u, 32)) return ALX_E_HASH;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = tbl + ALX_HEAD + i * ALX_ENTRY;
        int s = check_entry(p, i + 1u);
        if (s) return s;
        for (uint32_t j = 0; j < i; j++) { /* shorthands and targets are distinct */
            const uint8_t *q = tbl + ALX_HEAD + j * ALX_ENTRY;
            if (same_bytes(p + 16, q + 16, 32) || same_bytes(p + 48, q + 48, 44)) return ALX_E_FORM;
        }
    }
    t->tbl = tbl;
    t->count = count;
    return ALX_OK;
}

int alx_get(const alx_t *t, uint32_t slot, alx_entry_t *e)
{
    if (!t || !t->tbl || !e || slot == 0 || slot > t->count) return ALX_E_SLOT;
    const uint8_t *p = t->tbl + ALX_HEAD + (slot - 1u) * ALX_ENTRY;
    e->slot = slot;
    e->kind = p[4];
    e->inputs = p[5];
    e->flags = p[6];
    e->languages = p[7];
    e->major = be16(p + 8);
    e->minor = be16(p + 10);
    e->patch = be16(p + 12);
    for (uint32_t i = 0; i < 32; i++) e->shorthand[i] = (char) p[16 + i];
    e->shorthand[32] = 0;
    for (uint32_t i = 0; i < 44; i++) e->target[i] = (char) p[48 + i];
    e->target[44] = 0;
    for (uint32_t i = 0; i < 36; i++) e->cid[i] = p[92 + i];
    return ALX_OK;
}

static int hexd(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int32_t alx_find(const alx_t *t, const char *s, uint32_t n)
{
    if (!t || !t->tbl || (!s && n)) return ALX_E_NAME;
    if (n >= 3 && s[0] == '@' && s[1] == 'S' && s[2] == ':') {
        /* "@S:" and exactly four upper-case hex digits */
        if (n != 7) return ALX_E_NAME;
        uint32_t v = 0;
        for (uint32_t i = 3; i < 7; i++) {
            int d = hexd(s[i]);
            if (d < 0) return ALX_E_NAME;
            v = v * 16u + (uint32_t) d;
        }
        if (v == 0 || v > t->count) return ALX_E_SLOT;
        return (int32_t) v;
    }
    if (n && s[0] == '@') s++, n--;
    if (n == 0 || n > 31) return ALX_E_NAME;
    for (uint32_t i = 0; i < t->count; i++) {
        const uint8_t *p = t->tbl + ALX_HEAD + i * ALX_ENTRY + 16;
        if (p[n] == 0 && same_bytes(p, (const uint8_t *) s, n)) return (int32_t) (i + 1u);
    }
    return ALX_E_NAME;
}

int32_t alx_shorthand(const alx_t *t, uint32_t slot, char *out, uint32_t cap)
{
    alx_entry_t e;
    int s = alx_get(t, slot, &e);
    if (s) return s;
    uint32_t n = 0;
    while (e.shorthand[n]) n++;
    if (!out || cap < n + 1u) return ALX_E_LIMIT;
    out[0] = '@';
    for (uint32_t i = 0; i < n; i++) out[i + 1u] = e.shorthand[i];
    return (int32_t) (n + 1u);
}

int alx_dock(const alx_t *t, uint32_t a, uint32_t b)
{
    alx_entry_t x, y;
    int s = alx_get(t, a, &x);
    if (!s) s = alx_get(t, b, &y);
    if (s) return s;
    if ((x.flags | y.flags) & ALX_F_RETIRED) return ALX_E_RETIRED;
    return y.inputs == 1u ? ALX_OK : ALX_E_DOCK;
}

int alx_pipeline(const alx_t *t, const uint32_t *slots, uint32_t n, uint32_t *at)
{
    uint32_t dummy;
    if (!at) at = &dummy;
    *at = 0;
    if (!slots || n == 0) return ALX_E_SLOT;
    alx_entry_t e;
    int s = alx_get(t, slots[0], &e);
    if (!s && (e.flags & ALX_F_RETIRED)) s = ALX_E_RETIRED;
    if (s) return s;
    for (uint32_t i = 1; i < n; i++) {
        s = alx_dock(t, slots[i - 1u], slots[i]);
        if (s) {
            *at = i;
            return s;
        }
    }
    return ALX_OK;
}

const char *alx_status_name(int s)
{
    switch (s) {
    case ALX_OK:
        return "OK";
    case ALX_E_FORM:
        return "FORM";
    case ALX_E_HASH:
        return "HASH";
    case ALX_E_NAME:
        return "NAME";
    case ALX_E_SLOT:
        return "SLOT";
    case ALX_E_RETIRED:
        return "RETIRED";
    case ALX_E_DOCK:
        return "DOCK";
    case ALX_E_LIMIT:
        return "LIMIT";
    default:
        return "?";
    }
}
