/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_carto.c — core index nodes and cartographers' overlays (alx_carto.h,
 * docs/spec/CARTOGRAPHY.md). */
#include "alx_carto.h"
#include "alx_int.h"
#include "../robin_debanks/sha256.h"

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t) alx_be32(p) << 32 | alx_be32(p + 4);
}

static const uint8_t *entry(const alx_idx_t *x, uint32_t i)
{
    return x->b + ALX_IDX_HEAD + i * ALX_IDX_ENTRY;
}

void alx_carto_cid(const uint8_t *b, uint32_t len, uint8_t cid[36])
{
    cid[0] = 0x01, cid[1] = 0x55, cid[2] = 0x12, cid[3] = 0x20;
    sha256(b, len, cid + 4);
}

static bool cid_ok(const uint8_t *c, bool dag_pb)
{
    return c[0] == 0x01 && (c[1] == 0x55 || (dag_pb && c[1] == 0x70)) && c[2] == 0x12 &&
           c[3] == 0x20;
}

/* The shape both kinds share; *body is the length the trailer follows. */
static int form(const uint8_t *b, uint32_t len, uint32_t kind, uint32_t *n, uint32_t *body)
{
    uint32_t tail = kind == ALX_IDX_CORE ? 32u : ALX_IDX_PK + ALX_IDX_SIG;
    if (!b || len < ALX_IDX_HEAD + tail || len > ALX_BOOK_MAX) return ALX_E_FORM;
    if (b[0] != 'A' || b[1] != 'L' || b[2] != 'X' || b[3] != 'I' || b[4] != 1 || b[5] != kind)
        return ALX_E_FORM;
    uint32_t div = alx_be16(b + 6);
    *n = alx_be32(b + 8);
    if (*n == 0 || *n > ALX_IDX_MAX || len != ALX_IDX_HEAD + *n * ALX_IDX_ENTRY + tail)
        return ALX_E_FORM;
    if (!alx_zero(b + 12, 4) || !alx_zero(b + 52, 12) || !cid_ok(b + 16, kind == ALX_IDX_OVERLAY))
        return ALX_E_FORM;
    if (kind == ALX_IDX_CORE ? div != 0 : (div < 100 || div > 990 || div % 10)) return ALX_E_FORM;
    for (uint32_t i = 0; i < *n; i++) {
        const uint8_t *e = b + ALX_IDX_HEAD + i * ALX_IDX_ENTRY;
        uint64_t w = be64(e);
        uint32_t domain = (uint32_t) (w >> 56), place = (uint32_t) (w >> 40) & 0xFFFFu,
                 item = (uint32_t) (w >> 32) & 0xFFu;
        if (i && w <= be64(e - ALX_IDX_ENTRY)) return ALX_E_FORM; /* strictly rising */
        if (place < 100 || place > 999 || item < 1 || item > 99) return ALX_E_FORM;
        if (!cid_ok(e + 8, kind == ALX_IDX_OVERLAY) || alx_be32(e + 48) == 0 ||
            !alx_zero(e + 92, 4))
            return ALX_E_FORM;
        if (!alx_zero(e + 56, 36) && !cid_ok(e + 56, kind == ALX_IDX_OVERLAY)) return ALX_E_FORM;
        if (kind == ALX_IDX_CORE) {
            if (domain != ALX_G_CODE || ((w >> 16) & 0xFFFFu) != 0) return ALX_E_FORM;
        } else if ((domain != ALX_G_DATA && domain != ALX_G_SENSOR && domain != ALX_G_EMBED &&
                    domain != ALX_G_ATTEST) ||
                   place / 10u * 10u != div)
            return ALX_E_FORM; /* never code; every entry in the overlay's division */
    }
    *body = ALX_IDX_HEAD + *n * ALX_IDX_ENTRY;
    return ALX_OK;
}

int alx_carto_core(alx_carto_t *c, const alx_grid_t *g, const uint8_t *b, uint32_t len)
{
    uint32_t n, body;
    if (!c || !g || !g->t) return ALX_E_FORM;
    c->core.b = 0;
    c->core.n = 0;
    c->nover = 0;
    int s = form(b, len, ALX_IDX_CORE, &n, &body);
    if (s) return s;
    uint8_t h[32];
    sha256(b, body, h);
    if (!alx_same(h, b + body, 32)) return ALX_E_HASH;
    const alx_t *t = g->t;
    /* the parent is the library's index; every word one of its entries */
    uint32_t tlen = ALX_HEAD + t->books * ALX_BOOK + t->slots * ALX_SLOTREC +
                    t->targets * ALX_TARGET + 32u,
             want = t->targets;
    uint8_t cid[36];
    alx_carto_cid(t->tbl, tlen, cid);
    if (!alx_same(cid, b + 16, 36)) return ALX_E_LINEAGE;
    for (uint32_t k = 1; k <= t->slots; k++) {
        alx_entry_t e;
        alx_get(t, k, &e);
        want += !(e.flags & ALX_F_RETIRED);
    }
    if (n != want) return ALX_E_FORM;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = b + ALX_IDX_HEAD + i * ALX_IDX_ENTRY;
        alx_grid_hit_t hit;
        alx_booklet_t bk;
        s = alx_grid_resolve(g, be64(e), &hit);
        if (s) return s;
        alx_booklet(t, hit.booklet, &bk);
        if (alx_be32(e + 48) != hit.len || alx_be32(e + 52) != hit.off ||
            !alx_same(e + 56, bk.cid, 36) || e[9] != 0x55)
            return ALX_E_FORM;
        if (hit.format == ALX_G_MANIFEST) {
            alx_entry_t se;
            alx_get(t, hit.slot, &se);
            if (!alx_same(e + 12, se.content, 32)) return ALX_E_HASH;
        }
    }
    c->core.b = b;
    c->core.n = n;
    c->core.key = 0;
    alx_carto_cid(b, len, c->core.cid);
    return ALX_OK;
}

int alx_carto_overlay_check(const uint8_t *b, uint32_t len, alx_verify_fn verify, void *vctx)
{
    uint32_t n, body;
    if (!verify) return ALX_E_FORM;
    int s = form(b, len, ALX_IDX_OVERLAY, &n, &body);
    if (s) return s;
    uint8_t h[32];
    sha256(b, body, h);
    return verify(vctx, b + body, h, 32, b + body + ALX_IDX_PK) ? ALX_OK : ALX_E_SIGN;
}

int alx_carto_mount(alx_carto_t *c, const uint8_t *b, uint32_t len,
                    const uint8_t (*keys)[ALX_IDX_PK], uint32_t nkeys, alx_verify_fn verify,
                    void *vctx)
{
    uint32_t n, body, k = 0;
    if (!c || !c->core.b || !verify) return ALX_E_FORM;
    int s = form(b, len, ALX_IDX_OVERLAY, &n, &body);
    if (s) return s;
    const uint8_t *pk = b + body, *sig = pk + ALX_IDX_PK;
    while (k < nkeys && !alx_same(keys[k], pk, ALX_IDX_PK)) k++;
    if (!keys || k == nkeys) return ALX_E_TRUST;
    uint8_t h[32], cid[36];
    sha256(b, body, h);
    if (!verify(vctx, pk, h, 32, sig)) return ALX_E_SIGN;
    alx_carto_cid(b, len, cid);
    bool parent = alx_same(b + 16, c->core.cid, 36);
    for (uint32_t i = 0; i < c->nover; i++) {
        if (alx_same(c->over[i].cid, cid, 36)) return ALX_OK; /* already mounted */
        parent |= alx_same(b + 16, c->over[i].cid, 36);
    }
    if (!parent) return ALX_E_LINEAGE;
    if (c->nover == ALX_CARTO_MOUNT) return ALX_E_LIMIT;
    alx_idx_t *x = &c->over[c->nover++];
    x->b = b;
    x->n = n;
    x->key = k;
    for (uint32_t i = 0; i < 36; i++) x->cid[i] = cid[i];
    return ALX_OK;
}

void alx_carto_demoted(alx_carto_t *c, uint8_t (*store)[36], uint32_t cap)
{
    c->demoted = store;
    c->ndemoted = 0;
    c->cap_demoted = store ? cap : 0;
}

/* the entry giving word w, or 0 */
static const uint8_t *find(const alx_idx_t *x, uint64_t w)
{
    uint32_t lo = 0, hi = x->b ? x->n : 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint64_t m = be64(entry(x, mid));
        if (m == w) return entry(x, mid);
        if (m < w)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return 0;
}

int alx_carto_demote(alx_carto_t *c, const uint8_t cid[36])
{
    if (!c || !cid) return ALX_E_FORM;
    for (uint32_t i = 0; c->core.b && i < c->core.n; i++)
        if (alx_same(entry(&c->core, i) + 8, cid, 36)) return ALX_E_FORM; /* sealed */
    for (uint32_t i = 0; i < c->ndemoted; i++)
        if (alx_same(c->demoted[i], cid, 36)) return ALX_OK;
    if (c->ndemoted >= c->cap_demoted) return ALX_E_LIMIT;
    for (uint32_t i = 0; i < 36; i++) c->demoted[c->ndemoted][i] = cid[i];
    c->ndemoted++;
    return ALX_OK;
}

static void fill(alx_carto_hit_t *h, uint64_t w, const uint8_t *e, uint32_t trust, uint32_t from)
{
    h->word = w;
    for (uint32_t i = 0; i < 36; i++) h->target[i] = e[8 + i], h->container[i] = e[56 + i];
    h->lba = alx_be32(e + 44);
    h->len = alx_be32(e + 48);
    h->off = alx_be32(e + 52);
    h->trust = trust;
    h->from = from;
}

int alx_carto_resolve(const alx_carto_t *c, uint64_t word, alx_carto_hit_t *h)
{
    if (!c || !h || !c->core.b) return ALX_E_FORM;
    const uint8_t *e = find(&c->core, word), *got = 0;
    if (e) {
        fill(h, word, e, ALX_T_SEALED, 0);
        return ALX_OK;
    }
    uint32_t from = 0;
    for (uint32_t k = 0; k < c->nover; k++) {
        e = find(&c->over[k], word);
        if (!e) continue;
        if (!got) {
            got = e;
            from = k + 1u;
        } else if (!alx_same(e + 8, got + 8, 36))
            return ALX_E_BOTH; /* the claims disagree: hold, use neither */
    }
    if (!got) return ALX_E_NAME;
    uint32_t trust = ALX_T_CLAIM;
    for (uint32_t i = 0; i < c->ndemoted; i++)
        if (alx_same(c->demoted[i], got + 8, 36)) trust = ALX_T_DEMOTED;
    fill(h, word, got, trust, from);
    return ALX_OK;
}
