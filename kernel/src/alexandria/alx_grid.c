/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_grid.c — 64-bit grid words resolved through a radix table over the
 * sealed index, and layer lineage checked upward from the root
 * (docs/spec/GRID_CHAIN.md). A word is a key: resolving it gives where
 * verified bytes lie, never a pointer to run. */
#include "alx_int.h"
#include "../robin_debanks/sha256.h"

int alx_grid_build(alx_grid_t *g, const alx_t *t)
{
    if (g) g->t = 0; /* a failed build leaves nothing to resolve against */
    if (!g || !t || !t->tbl) return ALX_E_FORM;
    for (uint32_t i = 0; i < 900; i++) g->count[i] = 0;
    /* count per place, then the start of each place's run */
    for (uint32_t s = 1; s <= t->slots; s++) {
        uint32_t p = alx_be16(alx_slot_rec(t, s) + 92) - 100u;
        g->count[p]++;
    }
    uint32_t at = 0;
    for (uint32_t i = 0; i < 900; i++) g->first[i] = (uint16_t) at, at += g->count[i];
    /* place each slot, keeping items rising within a place */
    uint8_t fill[900];
    for (uint32_t i = 0; i < 900; i++) fill[i] = 0;
    for (uint32_t s = 1; s <= t->slots; s++) {
        const uint8_t *r = alx_slot_rec(t, s);
        uint32_t p = alx_be16(r + 92) - 100u, item = alx_be16(r + 94);
        uint32_t k = g->first[p] + fill[p]++;
        while (k > g->first[p] && alx_be16(alx_slot_rec(t, g->order[k - 1]) + 94) > item) {
            g->order[k] = g->order[k - 1];
            k--;
        }
        g->order[k] = (uint16_t) s;
    }
    g->t = t;
    return ALX_OK;
}

int alx_grid_word(const alx_t *t, uint32_t slot, uint32_t format, uint32_t language, uint64_t *word)
{
    alx_entry_t e;
    int s = alx_get(t, slot, &e);
    if (s) return s;
    if (!word) return ALX_E_LIMIT;
    if (e.flags & ALX_F_RETIRED) return ALX_E_RETIRED;
    if (format == ALX_G_MANIFEST) {
        if (language) return ALX_E_LANG;
    } else if (format == ALX_G_LOGIC) {
        s = alx_target(t, slot, language, 0, 0);
        if (s) return s;
    } else
        return ALX_E_FORM;
    *word = (uint64_t) ALX_G_CODE << 56 | (uint64_t) e.place << 40 | (uint64_t) e.item << 32 |
            (uint64_t) format << 8 | language;
    return ALX_OK;
}

int alx_grid_resolve(const alx_grid_t *g, uint64_t word, alx_grid_hit_t *h)
{
    if (!g || !g->t || !g->t->tbl || !h) return ALX_E_FORM;
    uint32_t domain = (uint32_t) (word >> 56), place = (uint32_t) (word >> 40) & 0xFFFFu,
             item = (uint32_t) (word >> 32) & 0xFFu, ext = (uint32_t) (word >> 16) & 0xFFFFu,
             format = (uint32_t) (word >> 8) & 0xFFu, language = (uint32_t) word & 0xFFu;
    if (place < 100 || place > 999 || item < 1 || item > 99 || format > ALX_G_MANIFEST ||
        (format == ALX_G_MANIFEST ? language != 0 : !alx_language_ok(language)))
        return ALX_E_FORM;
    if (domain != ALX_G_CODE)
        return domain == ALX_G_DATA || domain == ALX_G_SENSOR || domain == ALX_G_EMBED ||
                       domain == ALX_G_ATTEST
                   ? ALX_E_DOMAIN
                   : ALX_E_FORM;
    if (ext) return ALX_E_OVERLAY;
    uint32_t p = place - 100u, lo = g->first[p], hi = lo + g->count[p];
    while (lo < hi) { /* items rise within a place */
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t it = alx_be16(alx_slot_rec(g->t, g->order[mid]) + 94);
        if (it == item) {
            alx_entry_t e;
            uint32_t slot = g->order[mid];
            alx_get(g->t, slot, &e);
            if (e.flags & ALX_F_RETIRED) return ALX_E_RETIRED;
            h->slot = slot;
            h->language = language;
            h->format = format;
            h->booklet = e.booklet;
            if (format == ALX_G_MANIFEST) {
                h->off = e.manifest_off;
                h->len = e.manifest_len;
                return ALX_OK;
            }
            return alx_target(g->t, slot, language, &h->off, &h->len);
        }
        if (it < item)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return ALX_E_NAME;
}

#define ALX_LINEAGE_MAX 64u

int alx_lineage(const alx_layer_t *l, uint32_t n, uint8_t leaf[36], uint32_t *at)
{
    uint8_t cid[36];
    if (at) *at = 0;
    if (!l || n == 0 || n > ALX_LINEAGE_MAX) return ALX_E_FORM;
    for (uint32_t i = 0; i < n; i++) {
        if (at) *at = i;
        const alx_layer_t *x = &l[i];
        if (!x->manifest || x->len == 0 || x->len > ALX_BOOK_MAX) return ALX_E_FORM;
        if (i == 0) {
            if (x->level != 0) return ALX_E_FORM;
            if (!alx_zero(x->parent, 36)) return ALX_E_LINEAGE;
        } else {
            uint8_t up = l[i - 1].level;
            if (x->level > 4 || (x->level != up + 1u && !(x->level == 4 && up == 4)))
                return ALX_E_FORM;
            if (!alx_same(x->parent, cid, 36)) return ALX_E_LINEAGE;
        }
        cid[0] = 0x01, cid[1] = 0x55, cid[2] = 0x12, cid[3] = 0x20;
        sha256(x->manifest, x->len, cid + 4);
    }
    if (leaf)
        for (uint32_t i = 0; i < 36; i++) leaf[i] = cid[i];
    return ALX_OK;
}
