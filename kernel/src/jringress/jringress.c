/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* jringress.c — the producer ingress driver (jringress.h I1-I3). */
#include "jringress.h"
#include "../robin_debanks/sha256.h"

/* domain tags are exact-length byte labels hashed by size — never read as
 * C strings, so no terminator: mark them where the compiler knows how */
#if defined(__has_attribute)
#if __has_attribute(nonstring)
#define DOM_TAG __attribute__((nonstring))
#endif
#endif
#ifndef DOM_TAG
#define DOM_TAG
#endif
static const char PLEDGE_DOM[17] DOM_TAG = "zxv/jr-ingress/v1";
static const char WITHDRAW_DOM[18] DOM_TAG = "zxv/jr-withdraw/v1";

static void put(uint8_t *o, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) o[i] = (uint8_t) (v >> (8 * i));
}
static void copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static bool same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

jr_status_t jri_init(jri_t *g, jr_router_t *r, jr_verify_fn verify, void *vctx)
{
    if (!g || !r) return JR_E_ARG;
    uint8_t *b = (uint8_t *) g;
    for (uint32_t i = 0; i < sizeof *g; i++) b[i] = 0;
    g->router = r;
    g->verify = verify;
    g->vctx = vctx;
    return JR_OK;
}

void jri_pledge_msg(const uint8_t key[32], uint32_t asset_id, uint64_t version, const uint8_t *hdrs,
                    uint32_t n, uint8_t out[JRI_PLEDGE_MSG_LEN])
{
    copy(out, (const uint8_t *) PLEDGE_DOM, 17);
    copy(out + 17, key, 32);
    put(out + 49, asset_id, 4);
    put(out + 53, version, 8);
    put(out + 61, n, 4);
    sha256(hdrs, (size_t) n * JR_LOT_HDR_LEN, out + 65);
}

void jri_withdraw_msg(const uint8_t key[32], uint64_t version, uint8_t out[JRI_WITHDRAW_MSG_LEN])
{
    copy(out, (const uint8_t *) WITHDRAW_DOM, 18);
    copy(out + 18, key, 32);
    put(out + 50, version, 8);
}

static jri_producer_t *find(jri_t *g, const uint8_t key[32])
{
    for (uint32_t i = 0; i < JRI_MAX_PRODUCERS; i++)
        if (g->prod[i].used && same(g->prod[i].key, key, 32)) return &g->prod[i];
    return 0;
}

jr_status_t jri_pledge(jri_t *g, const uint8_t key[32], uint32_t asset_id, uint64_t version,
                       const uint8_t *hdrs, uint32_t n, const uint8_t *sig, uint32_t siglen)
{
    if (!g || !key || !hdrs || n == 0 || n > JRI_MAX_MANIFEST_LOTS) return JR_E_ARG;
    jr_router_t *r = g->router;
    jri_producer_t *p = find(g, key);
    if (p && version <= p->version) return JR_E_REPLAY;
    if (!p) {
        for (uint32_t i = 0; i < JRI_MAX_PRODUCERS && !p; i++)
            if (!g->prod[i].used) p = &g->prod[i];
        if (!p) return JR_E_FULL;
    }
    const jr_asset_t *a = 0;
    for (uint32_t i = 0; i < r->nassets; i++)
        if (r->assets[i].id == asset_id) a = &r->assets[i];
    if (!a) return JR_E_NOTFOUND;
    if (!(a->cap & JR_CAP_PHYSICAL)) return JR_E_ARG;
    if (!g->verify) return JR_E_SIG;
    uint8_t msg[JRI_PLEDGE_MSG_LEN];
    jri_pledge_msg(key, asset_id, version, hdrs, n, msg);
    if (!g->verify(g->vctx, msg, JRI_PLEDGE_MSG_LEN, sig, siglen)) return JR_E_SIG;
    /* all or none: check every header before adding any */
    if (r->nlots + n > JR_MAX_LOTS) return JR_E_FULL;
    jr_lot_hdr_t h[JRI_MAX_MANIFEST_LOTS];
    for (uint32_t i = 0; i < n; i++) {
        jr_status_t s = jr_lot_hdr_decode(hdrs + (size_t) i * JR_LOT_HDR_LEN, &h[i]);
        if (s != JR_OK) return s;
        for (uint32_t k = 0; k < i; k++)
            if (h[k].warehouse_id == h[i].warehouse_id && h[k].drum_serial == h[i].drum_serial)
                return JR_E_ARG;
        for (uint32_t k = 0; k < r->nlots; k++)
            if (r->lots[k].hdr.warehouse_id == h[i].warehouse_id &&
                r->lots[k].hdr.drum_serial == h[i].drum_serial)
                return JR_E_ARG;
    }
    uint32_t slot = (uint32_t) (p - g->prod) + 1u;
    for (uint32_t i = 0; i < n; i++)
        (void) jr_add_lot_src(r, asset_id, hdrs + (size_t) i * JR_LOT_HDR_LEN, slot);
    if (!p->used) {
        copy(p->key, key, 32);
        p->used = true;
    }
    p->version = version;
    p->pledged += n;
    return jr_set_physical(r, true);
}

jr_status_t jri_withdraw(jri_t *g, const uint8_t key[32], uint64_t version, const uint8_t *sig,
                         uint32_t siglen, uint32_t *n)
{
    if (!g || !key || !n) return JR_E_ARG;
    *n = 0;
    jri_producer_t *p = find(g, key);
    if (!p) return JR_E_NOTFOUND;
    if (version <= p->version) return JR_E_REPLAY;
    if (!g->verify) return JR_E_SIG;
    uint8_t msg[JRI_WITHDRAW_MSG_LEN];
    jri_withdraw_msg(key, version, msg);
    if (!g->verify(g->vctx, msg, JRI_WITHDRAW_MSG_LEN, sig, siglen)) return JR_E_SIG;
    p->version = version;
    *n = jr_withdraw_source(g->router, (uint32_t) (p - g->prod) + 1u);
    return JR_OK;
}
