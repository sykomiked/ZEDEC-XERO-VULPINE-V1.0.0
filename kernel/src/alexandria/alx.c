/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx.c — the Library of Alexandria's index and booklets (alx.h). */
#include "alx_int.h"
#include "../robin_debanks/sha256.h"

uint32_t alx_be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

uint16_t alx_be16(const uint8_t *p)
{
    return (uint16_t) (p[0] << 8 | p[1]);
}

bool alx_zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return false;
    return true;
}

bool alx_same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bool word(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

/* A NUL-padded "a.b" field of cap bytes: its length, or 0 when malformed.
 * more: more than one dot allowed (block.entry may nest). */
static uint32_t name_field(const uint8_t *p, uint32_t cap, bool more)
{
    uint32_t n = 0;
    while (n < cap && p[n]) n++;
    if (n == 0 || n == cap || !alx_zero(p + n, cap - n)) return 0;
    if (!(p[0] >= 'a' && p[0] <= 'z')) return 0;
    uint32_t dots = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] == '.') {
            if (i + 1 == n || p[i - 1] == '.') return 0;
            dots++;
        } else if (!word(p[i])) {
            return 0;
        }
    }
    return dots == 1 || (more && dots > 1) ? n : 0;
}

/* The class a shorthand's namespace belongs to (1..9), or 0. */
static uint32_t class_of(const uint8_t *s)
{
    static const struct {
        const char *ns;
        uint8_t cls;
    } NS[] = {{"mem", 1},  {"crypto", 2}, {"drv", 3},   {"logic", 4}, {"num", 5},
              {"fin", 6},  {"prob", 6},   {"strat", 6}, {"wire", 7},  {"text", 8},
              {"time", 9}, {"swarm", 9},  {"cell", 9}};
    for (uint32_t i = 0; i < sizeof NS / sizeof NS[0]; i++) {
        uint32_t k = 0;
        while (NS[i].ns[k] && s[k] == (uint8_t) NS[i].ns[k]) k++;
        if (!NS[i].ns[k] && s[k] == '.') return NS[i].cls;
    }
    return 0;
}

bool alx_language_ok(uint32_t l)
{
    return (l >= 1 && l <= 99) || (l >= 101 && l <= 199);
}

static const uint8_t *book_rec(const alx_t *t, uint32_t i)
{
    return t->tbl + ALX_HEAD + i * ALX_BOOK;
}

const uint8_t *alx_slot_rec(const alx_t *t, uint32_t slot)
{
    return t->tbl + ALX_HEAD + t->books * ALX_BOOK + (slot - 1u) * ALX_SLOTREC;
}

static const uint8_t *target_rec(const alx_t *t, uint32_t i)
{
    return t->tbl + ALX_HEAD + t->books * ALX_BOOK + t->slots * ALX_SLOTREC + i * ALX_TARGET;
}

static bool in_booklet(uint32_t off, uint32_t len, uint32_t booklen)
{
    return off >= ALX_ALIGN && off % ALX_ALIGN == 0 && len > 0 && off < booklen &&
           len <= booklen - off;
}

/* One slot record; *next is the first target the next live slot must claim. */
static int check_slot(const alx_t *t, uint32_t slot, uint32_t *next)
{
    const uint8_t *p = alx_slot_rec(t, slot);
    if (alx_be32(p) != slot) return ALX_E_FORM;
    uint8_t kind = p[4], inputs = p[5], flags = p[6], langs = p[7];
    if (flags & ~(ALX_F_RETIRED | ALX_F_WITNESSED)) return ALX_E_FORM;
    if (!alx_zero(p + 14, 2) || !alx_zero(p + 98, 2)) return ALX_E_FORM;
    if (!name_field(p + 16, 32, false) || !name_field(p + 48, 44, true)) return ALX_E_FORM;
    uint32_t place = alx_be16(p + 92), item = alx_be16(p + 94);
    uint32_t cls = class_of(p + 16);
    if (place < 100 || place > 999 || item < 1 || item > 99 || place / 100 != cls)
        return ALX_E_FORM;
    uint32_t bk = alx_be16(p + 96), first = alx_be32(p + 100);
    uint32_t moff = alx_be32(p + 104), mlen = alx_be32(p + 108);
    if (flags & ALX_F_RETIRED) {
        /* a retired slot keeps its number, names and place, and nothing else */
        if (flags != ALX_F_RETIRED || kind || inputs || langs || !alx_zero(p + 8, 6) ||
            bk != 0xFFFFu || first || moff || mlen || !alx_zero(p + 112, 32))
            return ALX_E_FORM;
        return ALX_OK;
    }
    if (kind != ALX_NUM && kind != ALX_TRUTH) return ALX_E_FORM;
    if (inputs > 15 || langs == 0) return ALX_E_FORM;
    if (((flags & ALX_F_WITNESSED) != 0) != (langs >= 2)) return ALX_E_FORM;
    if (bk >= t->books || alx_be16(book_rec(t, bk)) != place / 10u * 10u) return ALX_E_FORM;
    uint32_t booklen = alx_be32(book_rec(t, bk) + 4);
    if (!in_booklet(moff, mlen, booklen)) return ALX_E_FORM;
    if (first != *next || langs > t->targets - first) return ALX_E_FORM;
    uint32_t prev = 0;
    for (uint32_t i = first; i < first + langs; i++) {
        const uint8_t *q = target_rec(t, i);
        uint32_t lang = alx_be16(q + 4);
        if (alx_be32(q) != slot || !alx_language_ok(lang) || lang <= prev || !alx_zero(q + 6, 2) ||
            !in_booklet(alx_be32(q + 8), alx_be32(q + 12), booklen))
            return ALX_E_FORM;
        prev = lang;
    }
    *next = first + langs;
    return ALX_OK;
}

int alx_open(alx_t *t, const uint8_t *tbl, uint32_t len)
{
    if (!t) return ALX_E_FORM;
    t->tbl = 0;
    t->slots = t->books = t->targets = t->nroots = 0;
    t->roots = 0;
    if (!tbl || len < ALX_HEAD + 32u) return ALX_E_FORM;
    if (tbl[0] != 'A' || tbl[1] != 'L' || tbl[2] != 'X' || tbl[3] != 'T') return ALX_E_FORM;
    if (tbl[4] != 1 || !alx_zero(tbl + 5, 3) || !alx_zero(tbl + 24, 8)) return ALX_E_FORM;
    uint32_t slots = alx_be32(tbl + 8), books = alx_be32(tbl + 12), targets = alx_be32(tbl + 16);
    if (alx_be32(tbl + 20) != ALX_BASE_END) return ALX_E_FORM;
    if (slots > ALX_MAX || books > ALX_MAX_BOOKS || targets > ALX_MAX * 15u) return ALX_E_FORM;
    if (len != ALX_HEAD + books * ALX_BOOK + slots * ALX_SLOTREC + targets * ALX_TARGET + 32u)
        return ALX_E_FORM;
    uint8_t h[SHA256_DIGEST_LEN];
    sha256(tbl, len - 32u, h);
    if (!alx_same(h, tbl + len - 32u, 32)) return ALX_E_HASH;
    alx_t c = {tbl, slots, books, targets, 0, 0};
    for (uint32_t i = 0; i < books; i++) {
        const uint8_t *b = book_rec(&c, i);
        uint32_t div = alx_be16(b), blen = alx_be32(b + 4);
        if (div < 100 || div > 990 || div % 10 || (i && div <= alx_be16(book_rec(&c, i - 1u))))
            return ALX_E_FORM;
        if (blen < ALX_ALIGN || blen > ALX_BOOK_MAX || blen % ALX_ALIGN) return ALX_E_FORM;
        if (!alx_zero(b + 2, 2) || !alx_zero(b + 8, 4)) return ALX_E_FORM;
        if (b[12] != 0x01 || b[13] != 0x55 || b[14] != 0x12 || b[15] != 0x20) return ALX_E_FORM;
    }
    uint32_t next = 0;
    for (uint32_t s = 1; s <= slots; s++) {
        int r = check_slot(&c, s, &next);
        if (r) return r;
        const uint8_t *p = alx_slot_rec(&c, s);
        for (uint32_t j = 1; j < s; j++) { /* names and places are distinct */
            const uint8_t *q = alx_slot_rec(&c, j);
            if (alx_same(p + 16, q + 16, 32) || alx_same(p + 48, q + 48, 44) ||
                alx_same(p + 92, q + 92, 4))
                return ALX_E_FORM;
        }
    }
    if (next != targets) return ALX_E_FORM;
    *t = c;
    return ALX_OK;
}

int alx_get(const alx_t *t, uint32_t slot, alx_entry_t *e)
{
    if (!t || !t->tbl || !e || slot == 0 || slot > t->slots) return ALX_E_SLOT;
    const uint8_t *p = alx_slot_rec(t, slot);
    e->slot = slot;
    e->kind = p[4];
    e->inputs = p[5];
    e->flags = p[6];
    e->languages = p[7];
    e->major = alx_be16(p + 8);
    e->minor = alx_be16(p + 10);
    e->patch = alx_be16(p + 12);
    e->place = alx_be16(p + 92);
    e->item = alx_be16(p + 94);
    e->booklet = alx_be16(p + 96);
    e->first_target = alx_be32(p + 100);
    e->manifest_off = alx_be32(p + 104);
    e->manifest_len = alx_be32(p + 108);
    for (uint32_t i = 0; i < 32; i++) e->content[i] = p[112 + i];
    for (uint32_t i = 0; i < 32; i++) e->shorthand[i] = (char) p[16 + i];
    e->shorthand[32] = 0;
    for (uint32_t i = 0; i < 44; i++) e->target[i] = (char) p[48 + i];
    e->target[44] = 0;
    return ALX_OK;
}

int alx_booklet(const alx_t *t, uint32_t index, alx_booklet_t *b)
{
    if (!t || !t->tbl || !b || index >= t->books) return ALX_E_SLOT;
    const uint8_t *p = book_rec(t, index);
    b->division = alx_be16(p);
    b->length = alx_be32(p + 4);
    for (uint32_t i = 0; i < 36; i++) b->cid[i] = p[12 + i];
    return ALX_OK;
}

int alx_target(const alx_t *t, uint32_t slot, uint32_t language, uint32_t *off, uint32_t *len)
{
    alx_entry_t e;
    int s = alx_get(t, slot, &e);
    if (s) return s;
    if (e.flags & ALX_F_RETIRED) return ALX_E_RETIRED;
    for (uint32_t i = e.first_target; i < e.first_target + e.languages; i++) {
        const uint8_t *q = target_rec(t, i);
        if (alx_be16(q + 4) == language) {
            if (off) *off = alx_be32(q + 8);
            if (len) *len = alx_be32(q + 12);
            return ALX_OK;
        }
    }
    return ALX_E_LANG;
}

int alx_booklet_check(const alx_t *t, uint32_t index, const uint8_t *b, uint32_t len)
{
    alx_booklet_t bk;
    int s = alx_booklet(t, index, &bk);
    if (s) return s;
    if (!b || len != bk.length) return ALX_E_FORM;
    uint8_t h[SHA256_DIGEST_LEN];
    sha256(b, len, h);
    if (!alx_same(h, bk.cid + 4, 32)) return ALX_E_HASH;
    /* the header the index relies on: "ALXB", version 1, the division, the
     * length, zero padding */
    if (b[0] != 'A' || b[1] != 'L' || b[2] != 'X' || b[3] != 'B' || b[4] != 1 || b[5] != 0 ||
        alx_be16(b + 6) != bk.division || alx_be32(b + 12) != len || !alx_zero(b + 16, 48))
        return ALX_E_FORM;
    return ALX_OK;
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
    static const char *const N[] = {"OK",      "FORM",    "HASH",  "NAME", "SLOT",
                                    "RETIRED", "DOCK",    "LIMIT", "LANG", "SYNTAX",
                                    "LINEAGE", "OVERLAY", "DOMAIN", "BOTH", "TRUST", "SIGN"};
    return s <= 0 && s >= -15 ? N[-s] : "?";
}
