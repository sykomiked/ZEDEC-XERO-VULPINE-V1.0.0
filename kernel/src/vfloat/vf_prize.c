/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vf_prize.c — the prize court for floating vouchers. See vf_prize.h. */
#include "vf_prize.h"

static bool eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}

/* Byte copy: a struct assignment may become a memcpy call, which a
 * freestanding build does not have. */
static void cpy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) a[i] = b[i];
}

static bool open_bearer(const vna_id_t *b)
{
    uint8_t x = 0xFF;
    for (uint32_t i = 0; i < VNA_ID_LEN; i++) x &= b->b[i];
    return x == 0xFF;
}

void vfp_init(vfp_court_t *c, vfp_verify_fn verify, void *vctx, vfp_audit_fn audit, void *actx)
{
    uint8_t *p = (uint8_t *) c;
    for (uint32_t i = 0; i < (uint32_t) sizeof *c; i++) p[i] = 0;
    c->verify = verify;
    c->vctx = vctx;
    c->audit = audit;
    c->actx = actx;
}

void vfp_bind(vfp_court_t *c, vf_t *f)
{
    c->f = f;
    vf_set_letter_hook(f, vfp_letter, c);
}

static int32_t find(const vfp_court_t *c, const uint8_t id[32])
{
    for (uint32_t i = 0; i < VFP_MAX_WARRANTS; i++)
        if (c->e[i].used && eq(c->e[i].w.id, id, 32)) return (int32_t) i;
    return -1;
}

const vfp_entry_t *vfp_get(const vfp_court_t *c, const uint8_t id[32])
{
    int32_t k = find(c, id);
    return k < 0 ? 0 : &c->e[k];
}

int32_t vfp_commission(vfp_court_t *c, const vfp_warrant_t *w, const uint8_t *seal, uint32_t len)
{
    if (!w) return VF_E_ARG;
    if (w->kind < VFP_RETRIEVAL || w->kind > VFP_CARTOGRAPHY) return VF_E_ARG;
    if (w->resource >= VF_RES_COUNT) return VF_E_ARG;
    if (w->cap == 0 || w->cap > VF_CAP_LIMIT) return VF_E_ARG;
    if (!vna_cid_is_raw(w->scope)) return VF_E_ARG;
    if (find(c, w->id) >= 0) return VFP_E_DUP;
    if (!c->verify || !c->verify(c->vctx, w, seal, len)) return VF_E_SIG;
    for (uint32_t i = 0; i < VFP_MAX_WARRANTS; i++) {
        vfp_entry_t *e = &c->e[i];
        if (e->used) continue;
        e->used = true;
        e->state = VFP_ACTIVE;
        cpy(&e->w, w, (uint32_t) sizeof *w);
        e->drawn = 0;
        e->cleared = e->condemned = e->held = 0;
        return VF_OK;
    }
    return VF_E_FULL;
}

int32_t vfp_revoke(vfp_court_t *c, const uint8_t id[32])
{
    int32_t k = find(c, id);
    if (k < 0) return VFP_E_PIRACY;
    c->e[k].state = VFP_REVOKED;
    return VF_OK;
}

static bool bearer_ok(const vfp_warrant_t *w, const vna_id_t *b)
{
    return open_bearer(&w->bearer) || eq(w->bearer.b, b->b, VNA_ID_LEN);
}

bool vfp_letter(void *ctx, const uint8_t letter[32], const vna_id_t *bearer, uint8_t resource,
                uint64_t units, bool take)
{
    vfp_court_t *c = (vfp_court_t *) ctx;
    int32_t k = find(c, letter);
    if (k < 0) {
        c->piracy++;
        return false;
    }
    vfp_entry_t *e = &c->e[k];
    if (e->state == VFP_REVOKED || resource != e->w.resource || !bearer_ok(&e->w, bearer) ||
        units > e->w.cap)
        return false;
    if (!take) return true; /* the bearer's check (P2) */
    /* P5: an issuing draw only for the claim under adjudication */
    if (!c->pending || c->pend_slot != (uint32_t) k || c->pend_units != units ||
        !eq(c->pend_bearer.b, bearer->b, VNA_ID_LEN))
        return false;
    if (e->state != VFP_ACTIVE || e->drawn > e->w.cap - units) return false;
    e->drawn += units;
    if (e->drawn == e->w.cap) e->state = VFP_FULFILLED;
    return true;
}

int32_t vfp_claim(vfp_court_t *c, uint32_t peer, const uint8_t id[32], const uint8_t *payload,
                  uint32_t len, uint32_t lifetime, const uint8_t *rnd, uint8_t out[VF_VOUCHER_LEN],
                  lpres_state_t *verdict)
{
    if (verdict) *verdict = LPRES_STATE_NEITHER;
    if (!c->f || !id) return VF_E_ARG;
    if (peer >= VF_MAX_PEERS || !c->f->peer[peer].used) return VF_E_PEER;
    int32_t k = find(c, id);
    if (k < 0) {
        c->piracy++;
        return VFP_E_PIRACY;
    }
    vfp_entry_t *e = &c->e[k];
    if (e->state != VFP_ACTIVE || e->w.expiry_epoch <= c->f->epoch) return VFP_E_CLOSED;
    const vna_id_t *who = &c->f->peer[peer].id;
    if (!bearer_ok(&e->w, who)) return VF_E_BEARER;
    if (!c->audit) return VFP_E_HELD;

    uint64_t units = 0;
    lpres_state_t v = c->audit(c->actx, &e->w, who, payload, len, &units);
    if (verdict) *verdict = v;
    if (v == LPRES_STATE_FALSE || (v == LPRES_STATE_TRUE && units == 0)) {
        e->condemned++;
        c->condemned++;
        return VFP_E_CONDEMNED;
    }
    if (v != LPRES_STATE_TRUE) {
        e->held++;
        c->held++;
        return VFP_E_HELD;
    }
    if (units > e->w.cap - e->drawn) return VF_E_CAP;

    c->pending = true;
    c->pend_slot = (uint32_t) k;
    cpy(&c->pend_bearer, who, (uint32_t) sizeof *who);
    c->pend_units = units;
    vf_status_t st =
        vf_issue(c->f, peer, e->w.resource, e->w.scope, e->w.id, units, lifetime, rnd, out);
    c->pending = false;
    if (st != VF_OK) return st;
    e->cleared++;
    c->cleared++;
    return VF_OK;
}
