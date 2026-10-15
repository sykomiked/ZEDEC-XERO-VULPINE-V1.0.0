/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pce.c — shared parts of the paraconsistent decision engines (pce.h). */
#include "pce.h"

bool pce_purpose_ok(uint32_t purpose)
{
    return purpose >= PCE_PURPOSE_GAME && purpose <= PCE_PURPOSE_FINANCE;
}

const char *pce_purpose_name(uint32_t purpose)
{
    static const char *const names[] = {"?",        "game",        "logistics", "scheduling",
                                        "business", "negotiation", "mapping",   "finance"};
    return pce_purpose_ok(purpose) ? names[purpose] : names[0];
}

pce_verdict_t pce_verdict(uint8_t base, uint8_t pol, uint8_t phase, uint32_t advisory)
{
    pce_verdict_t v;
    v.tag = lpx_make(base, pol, phase);
    v.act = lpx_action(v.tag);
    if ((advisory & PCE_ADV_ALL) != 0u && v.act == LPX_ACT_PROCEED) v.act = LPX_ACT_ESCALATE;
    v.kind = pdx_kind_of_tag(v.tag);
    return v;
}

uint8_t pce_trend(rat_t m0, rat_t m1, bool stale)
{
    if (stale || !m0.valid || !m1.valid) return LPX_P_ENTROPIC;
    int c = rat_cmp(m1, m0);
    if (c > 0) return LPX_P_NEGENTROPIC;
    if (c < 0) return LPX_P_ENTROPIC;
    return LPX_P_NEUTRAL;
}

void pce_hash_begin(pce_hash_t *h, uint8_t op)
{
    static const uint8_t dom[] = {'z', 'x', 'v', '/', 'p', 'c', 'e', '/', 'v', '1', '/'};
    sha256_init(&h->c);
    sha256_update(&h->c, dom, sizeof dom);
    sha256_update(&h->c, &op, 1);
}

void pce_hash_u64(pce_hash_t *h, uint64_t v)
{
    uint8_t b[8];
    for (uint32_t i = 0; i < 8u; i++) b[i] = (uint8_t) (v >> (8u * i));
    sha256_update(&h->c, b, sizeof b);
}

void pce_hash_i64(pce_hash_t *h, int64_t v)
{
    pce_hash_u64(h, (uint64_t) v);
}

void pce_hash_rat(pce_hash_t *h, rat_t r)
{
    pce_hash_i64(h, r.valid ? r.num : 0);
    pce_hash_i64(h, r.valid ? r.den : 0);
    uint8_t ok = r.valid ? 1u : 0u;
    sha256_update(&h->c, &ok, 1);
}

void pce_hash_end(pce_hash_t *h, uint8_t out[PDX_HASH])
{
    sha256_final(&h->c, out);
}

int pce_log(pdx_log_t *log, uint8_t op, const pce_verdict_t *v, uint32_t aux,
            const uint8_t in[PDX_HASH], const uint8_t out[PDX_HASH])
{
    if (log == NULL) return PCE_OK;
    if (v == NULL) return PCE_E_ARG;
    return pdx_log_append(log, op, v->kind, v->tag, (uint8_t) v->act, aux, in, out) ? PCE_OK
                                                                                    : PCE_E_LOG;
}

void pce_copy(void *dst, const void *src, size_t n)
{
    /* volatile keeps the loop a loop: no memcpy call is generated */
    volatile uint8_t *d = (volatile uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

bool pce_rat_nonneg(rat_t r)
{
    return r.valid && r.num >= 0;
}

/* An invalid input wins both, so an overflow is never hidden. */
rat_t pce_rat_min(rat_t a, rat_t b)
{
    if (!a.valid) return a;
    if (!b.valid) return b;
    return rat_cmp(a, b) <= 0 ? a : b;
}

rat_t pce_rat_max(rat_t a, rat_t b)
{
    if (!a.valid) return a;
    if (!b.valid) return b;
    return rat_cmp(a, b) >= 0 ? a : b;
}
