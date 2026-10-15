/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_lattice.c — matrix words, the one resolver, and ALXI images
 * (alx_lattice.h). */
#include "alx_lattice.h"
#include "alx_matrix.h"
#include "alx_z.h"
#include "alx_int.h"
#include "../robin_debanks/sha256.h"

uint64_t alx_lat_layer(uint32_t layer)
{
    return layer < ALX_MX_LAYERS ? (uint64_t) ALX_G_MATRIX << 56 | (uint64_t) layer << 48 : 0;
}

uint64_t alx_lat_booklet(uint32_t layer, uint32_t booklet)
{
    if (layer >= ALX_MX_LAYERS || booklet > ALX_LAT_MAXIDX) return 0;
    return alx_lat_layer(layer) | (uint64_t) (booklet + 1u) << 24;
}

uint64_t alx_lat_atom(uint32_t layer, uint32_t booklet, uint32_t atom)
{
    if (atom > ALX_LAT_MAXIDX) return 0;
    uint64_t w = alx_lat_booklet(layer, booklet);
    return w ? w | (uint64_t) (atom + 1u) : 0;
}

int alx_lat_parse(uint64_t w, uint32_t *layer, uint32_t *booklet, uint32_t *atom)
{
    if ((uint32_t) (w >> 56) != ALX_G_MATRIX) return ALX_E_DOMAIN;
    uint32_t l = (uint32_t) (w >> 48) & 255u, b = (uint32_t) (w >> 24) & 0xFFFFFFu,
             a = (uint32_t) w & 0xFFFFFFu;
    /* the layer must exist; an atom needs a booklet; the all-ones index is
     * never written (it would wrap to the whole) */
    if (l >= ALX_MX_LAYERS || (a && !b) || b == 0xFFFFFFu || a == 0xFFFFFFu) return ALX_E_FORM;
    if (layer) *layer = l;
    if (booklet) *booklet = b ? b - 1u : 0;
    if (atom) *atom = a ? a - 1u : 0;
    return a ? ALX_LAT_ATOM : b ? ALX_LAT_BOOKLET : ALX_LAT_LAYER;
}

static bool cid_is(const uint8_t *cid, const uint8_t *b, uint32_t len)
{
    uint8_t h[32];
    sha256(b, len, h);
    return cid[0] == 0x01 && cid[1] == 0x55 && cid[2] == 0x12 && cid[3] == 0x20 &&
           alx_same(cid + 4, h, 32);
}

static void put_cid(alx_lat_hit_t *h, const uint8_t *cid)
{
    for (uint32_t i = 0; i < 36; i++) h->cid[i] = cid[i];
}

/* fetch a block and check it is the one its CID names */
static int get(alx_lat_fetch_fn fetch, void *ctx, const uint8_t *cid, const uint8_t **b,
               uint32_t *len, alx_lat_hit_t *h)
{
    put_cid(h, cid);
    *len = 0;
    *b = fetch(ctx, cid, len);
    if (!*b) return ALX_E_NAME;
    return cid_is(cid, *b, *len) ? ALX_OK : ALX_E_HASH;
}

int alx_lat_resolve(const uint8_t *root, uint32_t root_len, const uint8_t *root_cid, uint64_t w,
                    alx_lat_fetch_fn fetch, void *ctx, uint8_t *scratch, uint32_t scratch_len,
                    alx_lat_hit_t *h)
{
    if (!h || !fetch) return ALX_E_FORM;
    h->bytes = 0;
    h->len = 0;
    int kind = alx_lat_parse(w, &h->layer, &h->booklet, &h->atom);
    if (kind < 0) return kind;
    h->kind = (uint32_t) kind;
    if (root_cid && (!root || !cid_is(root_cid, root, root_len))) {
        put_cid(h, root_cid);
        return ALX_E_HASH;
    }
    int r = alx_mx_check(root, root_len);
    if (r != ALX_OK) return r;
    alx_mx_layer_t l;
    alx_mx_layer(root, h->layer, &l);
    if (!l.booklets) return ALX_E_NAME; /* an empty layer: no tree */
    const uint8_t *b, *cid = l.root;
    uint32_t len, lv, n;
    uint64_t under;
    uint32_t depth = alx_mx_depth(l.booklets);
    if (kind != ALX_LAT_LAYER && h->booklet >= l.booklets) return ALX_E_NAME;
    /* down the fan-out tree, one digit of the booklet number per level */
    for (uint32_t d = depth; d-- > 0;) {
        if ((r = get(fetch, ctx, cid, &b, &len, h)) != ALX_OK) return r;
        if (alx_node_check(b, len, &lv, &n, &under) != ALX_OK || lv != d) return ALX_E_FORM;
        if (kind == ALX_LAT_LAYER) { /* the layer is its top node */
            h->bytes = b;
            h->len = len;
            return under == l.booklets ? ALX_OK : ALX_E_FORM;
        }
        uint32_t dg = alx_mx_digit(h->booklet, d);
        if (dg >= n) return ALX_E_FORM;
        cid = alx_node_child(b, dg);
    }
    if ((r = get(fetch, ctx, cid, &b, &len, h)) != ALX_OK) return r;
    if (alx_z_is(b, len)) { /* stored compressed: open it against its plain CID */
        int32_t pl = alx_z_open(b, len, scratch, scratch_len);
        if (pl < 0) return pl;
        b = scratch;
        len = (uint32_t) pl;
    }
    uint32_t lay, atoms;
    if ((r = alx_bk2_check(b, len, &lay, &atoms)) != ALX_OK) return r;
    if (lay != h->layer) return ALX_E_FORM;
    if (kind == ALX_LAT_BOOKLET) {
        h->bytes = b;
        h->len = len;
        return ALX_OK;
    }
    if (h->atom >= atoms) return ALX_E_NAME;
    const uint8_t *acid;
    uint32_t off, alen;
    alx_bk2_atom(b, h->atom, &acid, &off, &alen);
    put_cid(h, acid);
    h->bytes = b + off;
    h->len = alen;
    return ALX_OK;
}

/* ---- ALXI images ---- */

static int cmp36(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < 36; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

int alx_img_open(alx_img_t *im, const uint8_t *b, uint64_t len)
{
    if (!im || !b || len < 64u || b[0] != 'A' || b[1] != 'L' || b[2] != 'X' || b[3] != 'I' ||
        b[4] != 1 || b[5] || b[6] || b[7] || alx_be32(b + 12) != ALX_IMG_SECTOR ||
        !alx_zero(b + 52, 12))
        return ALX_E_FORM;
    uint32_t n = alx_be32(b + 8);
    uint64_t table_end = 64u + (uint64_t) n * ALX_IMG_ENTRY, at = table_end;
    if (table_end > len) return ALX_E_FORM;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = b + 64u + (uint64_t) i * ALX_IMG_ENTRY;
        uint64_t start = (uint64_t) alx_be32(e + 36) * ALX_IMG_SECTOR, l = alx_be32(e + 40);
        if (alx_be32(e + 44) || (i && cmp36(e - ALX_IMG_ENTRY, e) >= 0) || start < at || l > len ||
            start > len - l)
            return ALX_E_FORM;
        at = start + l;
    }
    im->b = b;
    im->len = len;
    im->n = n;
    im->sector = ALX_IMG_SECTOR;
    return ALX_OK;
}

const uint8_t *alx_img_root(const alx_img_t *im)
{
    return im->b + 16;
}

const uint8_t *alx_img_fetch(void *ctx, const uint8_t cid[36], uint32_t *len)
{
    const alx_img_t *im = (const alx_img_t *) ctx;
    uint32_t lo = 0, hi = im->n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const uint8_t *e = im->b + 64u + (uint64_t) mid * ALX_IMG_ENTRY;
        int c = cmp36(e, cid);
        if (!c) {
            *len = alx_be32(e + 40);
            return im->b + (uint64_t) alx_be32(e + 36) * ALX_IMG_SECTOR;
        }
        if (c < 0)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return 0;
}
