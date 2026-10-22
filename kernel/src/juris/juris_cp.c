/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* juris_cp.c — juris_cp.h JC1-JC7: the gateway key and the compliance plug-ins. */
#include "juris_cp.h"
#include "../robin_debanks/sha256.h"

static lpx_t tag(uint8_t base)
{
    return lpx_make(base, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

static bool cc_ok(const char *c)
{
    return c[0] >= 'A' && c[0] <= 'Z' && c[1] >= 'A' && c[1] <= 'Z';
}

/* "prefix/CC", then "/region" when region, then "/d" when d (JURIS_CP_ID_MAX) */

static void put_id(juris_plugin_t *p, const char *prefix, const char *country, const char *region,
                   char d)
{
    unsigned o = 0;
    for (const char *s = prefix; *s; s++) p->id[o++] = *s;
    p->id[o++] = '/';
    p->id[o++] = country[0];
    p->id[o++] = country[1];
    if (region) {
        p->id[o++] = '/';
        for (unsigned i = 0; i < CP_REGION_LEN && region[i]; i++) p->id[o++] = region[i];
    }
    if (d) {
        p->id[o] = '/';
        p->id[o + 1u] = d;
    }
}

static void plugin_base(juris_plugin_t *p, const char *country, uint32_t hooks, void *ctx)
{
    cp_zero(p, sizeof *p);
    p->abi = JURIS_PLUGIN_ABI;
    p->size = (uint32_t) sizeof *p;
    p->country[0] = country[0];
    p->country[1] = country[1];
    p->hooks = hooks;
    p->ctx = ctx;
}

/* ---- JC1 ---- */
void juris_gw_init(juris_gw_t *g)
{
    if (g) cp_zero(g, sizeof *g);
}

bool juris_gw_set(juris_gw_t *g, const uint8_t *pk, size_t len)
{
    if (!g || !pk || len != JURIS_GW_PK_LEN) return false;
    cp_copy(g->pk, pk, JURIS_GW_PK_LEN);
    g->set = true;
    return true;
}

void juris_gw_key_id(const uint8_t pk[JURIS_GW_PK_LEN], uint8_t out[32])
{
    sha256(pk, JURIS_GW_PK_LEN, out);
}

bool juris_gw_verify(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                     uint32_t sig_len)
{
    const juris_gw_t *g = ctx;
    if (!g || !g->set || !msg || !sig || sig_len != JURIS_GW_SIG_LEN) return false;
    return pqm_mldsa87_verify(g->pk, msg, len, (const uint8_t *) JURIS_GW_CTX, JURIS_GW_CTX_LEN,
                              sig);
}

void juris_gw_sign(const uint8_t sk[JURIS_GW_SK_LEN], const uint8_t *msg, size_t len,
                   const uint8_t *rnd, uint8_t sig[JURIS_GW_SIG_LEN])
{
    pqm_mldsa87_sign(sk, msg, len, (const uint8_t *) JURIS_GW_CTX, JURIS_GW_CTX_LEN, rnd, sig);
}

/* ---- JC2 ---- */
bool juris_cp_manifest(const juris_plugin_t *p, uint64_t version, const uint8_t *image,
                       size_t image_len, const uint8_t *sig, uint32_t sig_len,
                       juris_manifest_t *out)
{
    if (!p || !out || (!image && image_len)) return false;
    cp_zero(out, sizeof *out);
    cp_copy(out->id, p->id, JURIS_ID_LEN);
    out->country[0] = p->country[0];
    out->country[1] = p->country[1];
    out->hooks = p->hooks;
    out->version = version;
    sha256(image, image_len, out->image_hash);
    out->sig = sig;
    out->sig_len = sig_len;
    return true;
}

/* ---- JC3 / JC4 ---- */
/* LPX_B_TRUE mapped, LPX_B_NEITHER unknown country, LPX_B_FALSE unmappable */
static uint8_t map_transfer(const juris_cp_capctl_t *a, const juris_transfer_t *t,
                            capctl_transfer_t *o)
{
    cp_zero(o, sizeof *o);
    if (!cc_ok(t->from_country) || !cc_ok(t->to_country) || t->from_country[2] || t->to_country[2])
        return LPX_B_NEITHER;
    if (!a->ccy || !a->ccy(t->ccy, &o->ccy)) return LPX_B_FALSE;
    o->from[0] = t->from_country[0];
    o->from[1] = t->from_country[1];
    o->to[0] = t->to_country[0];
    o->to[1] = t->to_country[1];
    o->amount = t->amount;
    o->sender = t->sender;
    o->receiver = t->receiver;
    return LPX_B_TRUE;
}

static uint64_t now_of(juris_clock_fn clock, void *cctx)
{
    return clock ? clock(cctx) : 0;
}

static lpx_t capctl_transfer(void *ctx, const juris_transfer_t *t)
{
    juris_cp_capctl_t *a = ctx;
    if (!a || !t) return tag(LPX_B_NEITHER);
    a->have = false;
    if (!a->cc->enabled) return tag(LPX_B_NEITHER);
    capctl_transfer_t ct;
    uint8_t m = map_transfer(a, t, &ct);
    if (m != LPX_B_TRUE) return tag(m);
    capctl_decision_t d = capctl_evaluate(a->cc, &ct, now_of(a->clock, a->cctx), &a->last);
    a->have = true;
    return tag(d == CAPCTL_DENY ? LPX_B_FALSE : LPX_B_TRUE);
}

static void capctl_commit_hook(void *ctx, const juris_transfer_t *t)
{
    juris_cp_capctl_t *a = ctx;
    if (!a || !t) return;
    capctl_transfer_t ct;
    if (map_transfer(a, t, &ct) == LPX_B_TRUE)
        (void) capctl_commit(a->cc, &ct, now_of(a->clock, a->cctx));
}

bool juris_cp_capctl_plugin(juris_plugin_t *p, juris_cp_capctl_t *a, capctl_t *cc,
                            const char country[2], juris_clock_fn clock, void *cctx,
                            juris_ccy_fn ccy)
{
    if (!p) return false;
    cp_zero(p, sizeof *p);
    if (!a || !cc || !country || !cc_ok(country)) return false;
    if (capctl_require(cc, country, 0) != CAPCTL_OK) return false;
    cp_zero(a, sizeof *a);
    a->cc = cc;
    a->clock = clock;
    a->cctx = cctx;
    a->ccy = ccy;
    plugin_base(p, country, JURIS_HOOK_TRANSFER | JURIS_HOOK_COMMIT, a);
    put_id(p, "capctl", country, 0, 0);
    p->transfer = capctl_transfer;
    p->commit = capctl_commit_hook;
    return true;
}

bool juris_cp_capctl_report(const juris_cp_capctl_t *a, capctl_result_t *out)
{
    if (!a || !out || !a->have || a->last.decision != CAPCTL_ALLOW_WITH_REPORT) return false;
    cp_copy(out, &a->last, sizeof *out);
    return true;
}

/* ---- JC5 ---- */
uint32_t juris_cp_ymd(uint64_t unix_s)
{
    uint64_t days = cp_udivmod64(unix_s, 86400u, 0);
    if (days > 2932896u) return 0; /* 9999-12-31 */
    /* days since 1970-01-01 to a civil date (proleptic Gregorian) */
    uint32_t z = (uint32_t) days + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
    uint32_t y = yoe + era * 400u + (m <= 2u ? 1u : 0u);
    return y * 10000u + m * 100u + d;
}

static lpx_t tax_rate(void *ctx, juris_tax_kind_t k, uint64_t *num, uint64_t *den)
{
    const juris_cp_tax_t *a = ctx;
    if (num) *num = 0;
    if (den) *den = 0;
    if (!a || !num || !den) return tag(LPX_B_NEITHER);
    if (k != JURIS_TAX_VAT || a->region[0] || (a->kind != TAXHOOK_VAT && a->kind != TAXHOOK_GST) ||
        !a->th->enabled)
        return tag(LPX_B_NEITHER);
    uint64_t now = now_of(a->clock, a->cctx);
    taxhook_line_t line = {TAXHOOK_STANDARD, 1, TAXHOOK_PPM};
    taxhook_invoice_t inv;
    cp_zero(&inv, sizeof inv);
    inv.date = juris_cp_ymd(now);
    inv.seller[0] = inv.buyer[0] = a->country[0];
    inv.seller[1] = inv.buyer[1] = a->country[1];
    inv.lines = &line;
    inv.n_lines = 1;
    taxhook_receipt_t r;
    if (taxhook_compute(a->th, a->country, a->region, a->kind, &inv, now, &r) != TAXHOOK_OK)
        return tag(LPX_B_FALSE);
    *num = r.line[0].rate_ppm;
    *den = TAXHOOK_PPM;
    return tag(LPX_B_TRUE);
}

bool juris_cp_tax_plugin(juris_plugin_t *p, juris_cp_tax_t *a, taxhook_t *th, const char country[2],
                         const char region[CP_REGION_LEN], uint8_t kind, juris_clock_fn clock,
                         void *cctx)
{
    if (!p) return false;
    cp_zero(p, sizeof *p);
    if (!a || !th || !country || !region || !cc_ok(country) || kind < TAXHOOK_VAT ||
        kind > TAXHOOK_SALES)
        return false;
    bool end = false; /* uppercase letters or digits, then zeros */
    for (unsigned i = 0; i < CP_REGION_LEN; i++) {
        char c = region[i];
        end |= c == 0;
        if (end ? c != 0 : !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    cp_zero(a, sizeof *a);
    a->th = th;
    a->country[0] = country[0];
    a->country[1] = country[1];
    cp_copy(a->region, region, CP_REGION_LEN);
    a->kind = kind;
    a->clock = clock;
    a->cctx = cctx;
    plugin_base(p, country, JURIS_HOOK_TAX, a);
    put_id(p, "taxhook", country, region, (char) ('0' + kind));
    p->tax_rate = tax_rate;
    return true;
}

/* ---- JC6 ---- */
static bool pred_eq(const kyc_pred_t *a, const kyc_pred_t *b)
{
    return a->kind == b->kind && a->age == b->age && a->country[0] == b->country[0] &&
           a->country[1] == b->country[1];
}

bool juris_kyc_init(juris_kyc_gate_t *g, kyc_verifier_t *v, const kyc_pred_t *need,
                    const char country[2], juris_clock_fn clock, void *cctx,
                    juris_kyc_entry_t *store, uint32_t cap)
{
    if (!g) return false;
    cp_zero(g, sizeof *g);
    if (!v || !need || !kyc_pred_ok(need) || !country || !cc_ok(country) || (!store && cap))
        return false;
    g->v = v;
    g->need = *need;
    g->country[0] = country[0];
    g->country[1] = country[1];
    g->clock = clock;
    g->cctx = cctx;
    g->e = store;
    g->cap = cap;
    return true;
}

void juris_kyc_enable(juris_kyc_gate_t *g, bool on, bool need_receiver)
{
    if (!g) return;
    g->enabled = on;
    g->need_receiver = need_receiver;
}

static juris_kyc_entry_t *kyc_find(const juris_kyc_gate_t *g, uint32_t account)
{
    for (uint32_t i = 0; i < g->n; i++)
        if (g->e[i].account == account) return &g->e[i];
    return 0;
}

kyc_status_t juris_kyc_admit(juris_kyc_gate_t *g, const kyc_request_t *req, const uint8_t *proof,
                             size_t len, uint64_t now, uint32_t account)
{
    if (!g || !g->v || !req || !proof) return KYC_E_ARG;
    if (!pred_eq(&req->pred, &g->need)) return KYC_E_PREDICATE;
    juris_kyc_entry_t *e = kyc_find(g, account);
    if (!e && g->n >= g->cap) return KYC_E_FULL;
    kyc_result_t res;
    kyc_status_t st = kyc_verify(g->v, req, proof, len, now, &res);
    if (st != KYC_OK) return st;
    if (!e) {
        e = &g->e[g->n++];
        e->account = account;
        e->expiry_day = res.expiry_day;
    } else if (res.expiry_day > e->expiry_day) {
        e->expiry_day = res.expiry_day;
    }
    return KYC_OK;
}

static bool kyc_ok(const juris_kyc_gate_t *g, uint32_t account, uint64_t day)
{
    const juris_kyc_entry_t *e = kyc_find(g, account);
    return e && day <= e->expiry_day;
}

static lpx_t kyc_transfer(void *ctx, const juris_transfer_t *t)
{
    const juris_kyc_gate_t *g = ctx;
    if (!g || !t || !g->enabled) return tag(LPX_B_NEITHER);
    uint64_t day = cp_udivmod64(now_of(g->clock, g->cctx), 86400u, 0);
    bool ok = kyc_ok(g, t->sender, day) && (!g->need_receiver || kyc_ok(g, t->receiver, day));
    return tag(ok ? LPX_B_TRUE : LPX_B_FALSE);
}

bool juris_kyc_plugin(juris_plugin_t *p, juris_kyc_gate_t *g)
{
    if (!p) return false;
    cp_zero(p, sizeof *p);
    if (!g || !g->v) return false;
    plugin_base(p, g->country, JURIS_HOOK_TRANSFER, g);
    put_id(p, "kycgate", g->country, 0, 0);
    p->transfer = kyc_transfer;
    return true;
}
