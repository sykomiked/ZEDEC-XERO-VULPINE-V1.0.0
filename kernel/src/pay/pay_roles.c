/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_roles.c — roles, identifiers, gateways and compliance hooks. */
#include "pay_roles.h"
#include "pay_tables.h"

/* ===== ISO 7064 MOD 97-10 ===== */

static uint32_t mod97_step(uint32_t r, uint32_t d)
{
    return (r * 10u + d) % 97u; /* 32-bit: freestanding-safe */
}

int32_t pay_mod97(const char *s, size_t n)
{
    uint32_t r = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (pay_is_digit(c)) {
            r = mod97_step(r, (uint32_t) (c - '0'));
        } else if (pay_is_upper(c)) {
            uint32_t v = (uint32_t) (c - 'A') + 10u;
            r = mod97_step(r, v / 10u);
            r = mod97_step(r, v % 10u);
        } else {
            return -1;
        }
    }
    return (int32_t) r;
}

bool pay_lei_valid(const char *lei)
{
    if (!lei || pay_strnlen(lei, 21) != 20) return false;
    for (int i = 0; i < 18; i++)
        if (!pay_is_alnum_upper(lei[i])) return false;
    if (!pay_is_digit(lei[18]) || !pay_is_digit(lei[19])) return false;
    return pay_mod97(lei, 20) == 1;
}

bool pay_bic_valid(const char *bic)
{
    size_t n = pay_strnlen(bic, 12);
    char cc[3];
    if (!bic || (n != 8 && n != 11)) return false;
    for (size_t i = 0; i < n; i++)
        if (!pay_is_alnum_upper(bic[i])) return false;
    if (!pay_is_upper(bic[4]) || !pay_is_upper(bic[5])) return false;
    cc[0] = bic[4];
    cc[1] = bic[5];
    cc[2] = '\0';
    return pay_iso3166_valid(cc);
}

/* IBAN lengths from the SWIFT IBAN Registry (ISO 13616 registration
 * authority). Countries not listed here are refused rather than guessed. */
static const struct {
    char cc[3];
    uint8_t len;
} iban_len[] = {
    {"AD", 24}, {"AE", 23}, {"AL", 28}, {"AT", 20}, {"AZ", 28}, {"BA", 20}, {"BE", 16}, {"BG", 22},
    {"BH", 22}, {"BR", 29}, {"BY", 28}, {"CH", 21}, {"CR", 22}, {"CY", 28}, {"CZ", 24}, {"DE", 22},
    {"DK", 18}, {"DO", 28}, {"EE", 20}, {"EG", 29}, {"ES", 24}, {"FI", 18}, {"FO", 18}, {"FR", 27},
    {"GB", 22}, {"GE", 22}, {"GI", 23}, {"GL", 18}, {"GR", 27}, {"GT", 28}, {"HR", 21}, {"HU", 28},
    {"IE", 22}, {"IL", 23}, {"IQ", 23}, {"IS", 26}, {"IT", 27}, {"JO", 30}, {"KW", 30}, {"KZ", 20},
    {"LB", 28}, {"LC", 32}, {"LI", 21}, {"LT", 20}, {"LU", 20}, {"LV", 21}, {"MC", 27}, {"MD", 24},
    {"ME", 22}, {"MK", 19}, {"MR", 27}, {"MT", 31}, {"MU", 30}, {"NL", 18}, {"NO", 15}, {"PK", 24},
    {"PL", 28}, {"PS", 29}, {"PT", 25}, {"QA", 29}, {"RO", 24}, {"RS", 22}, {"SA", 24}, {"SC", 31},
    {"SE", 24}, {"SI", 19}, {"SK", 24}, {"SM", 27}, {"ST", 25}, {"SV", 28}, {"TL", 23}, {"TN", 24},
    {"TR", 26}, {"UA", 29}, {"VA", 22}, {"VG", 24}, {"XK", 20},
};

uint32_t pay_iban_length(const char *country)
{
    if (!country) return 0;
    for (size_t i = 0; i < sizeof iban_len / sizeof iban_len[0]; i++)
        if (iban_len[i].cc[0] == country[0] && iban_len[i].cc[1] == country[1])
            return iban_len[i].len;
    return 0;
}

bool pay_iban_valid(const char *iban)
{
    char re[34];
    size_t n = pay_strnlen(iban, 35);
    if (!iban || n < 15 || n > 34) return false;
    if (!pay_is_upper(iban[0]) || !pay_is_upper(iban[1]) || !pay_is_digit(iban[2]) ||
        !pay_is_digit(iban[3]))
        return false;
    if (pay_iban_length(iban) != n) return false;
    for (size_t i = 4; i < n; i++)
        if (!pay_is_alnum_upper(iban[i])) return false;
    for (size_t i = 0; i < n; i++) re[i] = iban[i + 4 < n ? i + 4 : i + 4 - n];
    return pay_mod97(re, n) == 1;
}

/* ===== Fees ===== */

pay_status_t pay_fee_compute(const pay_fee_rule_t *f, uint64_t amount, uint64_t *fee)
{
    if (!f || !fee) return PAY_ERR_ARG;
    *fee = 0;
    switch (f->kind) {
    case PAY_FEE_NONE:
        return PAY_OK;
    case PAY_FEE_FLAT:
        *fee = f->flat;
        return PAY_OK;
    case PAY_FEE_PROPORTIONAL:
        if (f->prop.den == 0) return PAY_ERR_ARG;
        return pay_muldiv(amount, f->prop.num, f->prop.den, fee, 0) ? PAY_OK : PAY_ERR_OVERFLOW;
    case PAY_FEE_INTEREST:
    case PAY_FEE_LATE:
        return PAY_ERR_USURY;
    default:
        return PAY_ERR_ARG;
    }
}

/* ===== Roles ===== */

pay_status_t pay_role_init(pay_role_cfg_t *c, pay_role_t role, uint32_t id, const char *name)
{
    pay_platform_t p;
    if (!c || (unsigned) role >= PAY_ROLE_COUNT) return PAY_ERR_ARG;
    pay_memset(c, 0, sizeof *c);
    c->role = role;
    c->id = id;
    pay_strlcpy(c->name, name, sizeof c->name);
    pay_platform_default(&p);
    pay_strlcpy(c->jurisdiction, p.jurisdiction, sizeof c->jurisdiction);
    pay_tithe_policy_default(&c->tithe);
    c->fee.kind = PAY_FEE_NONE;
    c->equity_enabled = false;
    return PAY_OK;
}

pay_status_t pay_role_validate(const pay_role_cfg_t *c)
{
    uint64_t fee;
    if (!c || (unsigned) c->role >= PAY_ROLE_COUNT || c->name[0] == '\0') return PAY_ERR_ARG;
    if (pay_fee_compute(&c->fee, 1000000u, &fee) == PAY_ERR_USURY) return PAY_ERR_USURY;
    if (c->country[0] && !pay_iso3166_valid(c->country)) return PAY_ERR_ARG;
    switch (c->role) {
    case PAY_ROLE_INDIVIDUAL:
        return PAY_OK;
    case PAY_ROLE_SELF_BANK:
        return c->ledger ? PAY_OK : PAY_ERR_STATE;
    case PAY_ROLE_INSTITUTION:
        if (!pay_lei_valid(c->lei) || !pay_bic_valid(c->bic)) return PAY_ERR_ARG;
        if (c->iban[0] && !pay_iban_valid(c->iban)) return PAY_ERR_ARG;
        return PAY_OK;
    default:
        return PAY_ERR_ARG;
    }
}

pay_status_t pay_role_set_gateway(pay_role_cfg_t *c, pay_gateway_kind_t k, pay_gateway_submit_fn fn,
                                  void *ctx, const char *endpoint)
{
    if (!c || (unsigned) k >= PAY_GW_COUNT || !fn) return PAY_ERR_ARG;
    if (c->role == PAY_ROLE_INDIVIDUAL && k != PAY_GW_CHAIN) return PAY_ERR_POLICY;
    c->gw[k].enabled = true;
    c->gw[k].submit = fn;
    c->gw[k].ctx = ctx;
    pay_strlcpy(c->gw[k].endpoint, endpoint, sizeof c->gw[k].endpoint);
    return PAY_OK;
}

static bool party_min(const pay_travel_party_t *p)
{
    return p->name[0] && p->account[0] && (p->address[0] || p->national_id[0] || p->birth[0]);
}

bool pay_travel_rule_complete(const pay_travel_rule_t *t)
{
    return t && t->present && party_min(&t->originator) && t->beneficiary.name[0] &&
           t->beneficiary.account[0];
}

pay_status_t pay_role_authorize(pay_role_cfg_t *c, const pay_screen_req_t *req, uint8_t kyc_tier,
                                uint64_t tick, pay_screen_t *screen)
{
    pay_screen_t v = PAY_SCREEN_PASS;
    uint64_t used;
    if (screen) *screen = PAY_SCREEN_PASS;
    if (!c || !req) return PAY_ERR_ARG;
    if (kyc_tier < c->compliance.kyc_tier_required) return PAY_ERR_POLICY;
    if (c->limits.per_tx_max && req->amount > c->limits.per_tx_max) return PAY_ERR_LIMIT;
    if (c->limits.period_ticks &&
        (tick < c->period_start || tick - c->period_start >= c->limits.period_ticks)) {
        c->period_start = tick;
        c->period_used = 0;
    }
    if (!pay_add_ok(c->period_used, req->amount, &used)) return PAY_ERR_LIMIT;
    if (c->limits.per_period_max && used > c->limits.per_period_max) return PAY_ERR_LIMIT;
    if (req->crypto && c->compliance.travel_rule_required &&
        req->amount >= c->compliance.travel_rule_threshold &&
        !pay_travel_rule_complete(req->travel))
        return PAY_ERR_POLICY;
    if (c->compliance.screening_required) {
        if (!c->screen) return PAY_ERR_STATE;
        v = c->screen(c->screen_ctx, req);
        if (screen) *screen = v;
        if (v != PAY_SCREEN_PASS) return PAY_ERR_POLICY;
    }
    c->period_used = used;
    return PAY_OK;
}

pay_status_t pay_role_dispatch(const pay_role_cfg_t *c, const pay_gw_msg_t *m)
{
    if (!c || !m || (unsigned) m->kind >= PAY_GW_COUNT || !m->doc) return PAY_ERR_ARG;
    const pay_gateway_t *g = &c->gw[m->kind];
    if (!g->enabled || !g->submit) return PAY_ERR_STATE;
    return g->submit(g->ctx, m) == 0 ? PAY_OK : PAY_ERR_POLICY;
}
