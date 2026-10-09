/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_config.c — see cb_config.h. */
#include "cb_config.h"
#include "cb_ccy.h"
#include "cb_util.h"

int cb_role_default(cb_role role, cb_role_profile *o)
{
    if (!o) return CB_E_NULL;
    cb_memset(o, 0, sizeof *o);
    o->role = (uint8_t) role;
    o->screening_required = true;
    switch (role) {
    case CB_ROLE_CENTRAL_BANK:
        o->perms = CB_PERM_OPERATE_CYCLE | CB_PERM_ADMIT | CB_PERM_SEND_FI | CB_PERM_HOLD_PREFUND |
                   CB_PERM_RETURN | CB_PERM_CANCEL;
        break;
    case CB_ROLE_COMMERCIAL_BANK:
        o->perms = CB_PERM_SEND_CUSTOMER | CB_PERM_SEND_FI | CB_PERM_HOLD_PREFUND | CB_PERM_RETURN |
                   CB_PERM_CANCEL;
        break;
    case CB_ROLE_SELF_BANKING:
        o->perms = CB_PERM_SEND_CUSTOMER | CB_PERM_HOLD_PREFUND | CB_PERM_RETURN | CB_PERM_CANCEL |
                   CB_PERM_VFV;
        o->vfv_enabled = true;
        break;
    case CB_ROLE_INSTITUTION:
        o->perms = CB_PERM_SEND_CUSTOMER | CB_PERM_CANCEL;
        o->needs_sponsor = true;
        break;
    case CB_ROLE_INDIVIDUAL:
        o->perms = CB_PERM_SEND_CUSTOMER | CB_PERM_CANCEL;
        o->needs_sponsor = true;
        break;
    default:
        return CB_E_ROLE;
    }
    return CB_OK;
}

int cb_config_init(cb_config *c, cb_role role, const char *operator_name, const char *operator_bic,
                   const char *home_country)
{
    if (!c) return CB_E_NULL;
    cb_memset(c, 0, sizeof *c);
    int r = cb_role_default(role, &c->rp);
    if (r != CB_OK) return r;
    if (!operator_name || !operator_name[0] ||
        !cb_strlcpy(c->operator_name, operator_name, sizeof c->operator_name))
        return CB_E_NAME;
    if (!cb_bic_valid(operator_bic)) return CB_E_BIC;
    cb_strlcpy(c->operator_bic, operator_bic, sizeof c->operator_bic);
    if (!cb_country_by_a2(home_country)) return CB_E_COUNTRY;
    cb_strlcpy(c->home_country, home_country, sizeof c->home_country);
    c->corridor_default_deny = true;
    return CB_OK;
}

static bool is_vfv_code(const char *alpha)
{
    return cb_streq(alpha, "VFV");
}

int cb_config_add_ccy(cb_config *c, const char *alpha, const char *issuer_name,
                      const char *issuer_bic, const char *issuer_country, bool legal_tender)
{
    if (!c || !alpha) return CB_E_NULL;
    if (is_vfv_code(alpha)) return CB_E_VFV_AS_CCY;
    const cb_ccy_t *k = cb_ccy_by_alpha(alpha);
    if (!cb_ccy_payable(k)) return CB_E_BAD_CCY;
    if (k->num == 846u || k->num == 810u || k->num == 888u) return CB_E_VFV_AS_CCY;
    if (cb_config_ccy(c, alpha)) return CB_E_DUP;
    if (c->n_ccy >= CB_MAX_CCY) return CB_E_FULL;
    if (!issuer_name || !issuer_name[0]) return CB_E_NAME;
    if (!cb_bic_valid(issuer_bic)) return CB_E_BIC;
    if (!cb_country_by_a2(issuer_country)) return CB_E_COUNTRY;
    if (legal_tender && !cb_ccy_used_in(issuer_country, alpha)) return CB_E_ISSUER_COUNTRY;
    cb_ccy_profile *p = &c->ccy[c->n_ccy];
    cb_memset(p, 0, sizeof *p);
    cb_strlcpy(p->alpha, k->alpha, sizeof p->alpha);
    p->num = k->num;
    p->minor = k->minor;
    if (!cb_strlcpy(p->issuer_name, issuer_name, sizeof p->issuer_name)) return CB_E_NAME;
    cb_strlcpy(p->issuer_bic, issuer_bic, sizeof p->issuer_bic);
    cb_strlcpy(p->issuer_country, issuer_country, sizeof p->issuer_country);
    p->legal_tender = legal_tender;
    p->rounding = CB_ROUND_EXACT;
    p->cal.open_min = 8u * 60u;
    p->cal.cutoff_min = 16u * 60u;
    p->cal.close_min = 17u * 60u;
    p->cal.weekend_mask = CB_WEEKEND_SAT_SUN;
    p->prefund_required = true;
    p->cfm = CB_CFM_OUTBOUND | CB_CFM_INBOUND | CB_CFM_NONRESIDENT;
    c->n_ccy++;
    return CB_OK;
}

cb_ccy_profile *cb_config_ccy(cb_config *c, const char *alpha)
{
    if (!c || !alpha) return 0;
    for (uint32_t i = 0; i < c->n_ccy; i++)
        if (cb_streq(c->ccy[i].alpha, alpha)) return &c->ccy[i];
    return 0;
}

const cb_ccy_profile *cb_config_ccy_c(const cb_config *c, const char *alpha)
{
    return cb_config_ccy((cb_config *) c, alpha);
}

static bool window_vals_ok(uint16_t open_min, uint16_t cutoff_min, uint16_t close_min,
                           int16_t utc_offset_min, uint8_t weekend_mask)
{
    return open_min < cutoff_min && cutoff_min <= close_min && close_min <= 1440u &&
           utc_offset_min >= -14 * 60 && utc_offset_min <= 14 * 60 && (weekend_mask & 0x80u) == 0 &&
           weekend_mask != 0x7fu;
}

static bool window_ok(const cb_calendar *k)
{
    return window_vals_ok(k->open_min, k->cutoff_min, k->close_min, k->utc_offset_min,
                          k->weekend_mask) &&
           k->n_holidays <= CB_MAX_HOLIDAYS;
}

int cb_config_set_window(cb_config *c, const char *alpha, uint16_t open_min, uint16_t cutoff_min,
                         uint16_t close_min, int16_t utc_offset_min, uint8_t weekend_mask)
{
    cb_ccy_profile *p = cb_config_ccy(c, alpha);
    if (!p) return CB_E_UNKNOWN_CCY;
    if (!window_vals_ok(open_min, cutoff_min, close_min, utc_offset_min, weekend_mask))
        return CB_E_CALENDAR;
    p->cal.open_min = open_min;
    p->cal.cutoff_min = cutoff_min;
    p->cal.close_min = close_min;
    p->cal.utc_offset_min = utc_offset_min;
    p->cal.weekend_mask = weekend_mask;
    return CB_OK;
}

uint32_t cb_days_from_civil(uint32_t y, uint32_t m, uint32_t d)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970u || y > 9999u || m < 1u || m > 12u || d < 1u) return 0xffffffffu;
    bool leap = (y % 4u == 0 && y % 100u != 0) || y % 400u == 0;
    uint32_t md = mdays[m - 1u] + ((m == 2u && leap) ? 1u : 0u);
    if (d > md) return 0xffffffffu;
    uint32_t yy = m <= 2u ? y - 1u : y;
    uint32_t era = yy / 400u;
    uint32_t yoe = yy - era * 400u;
    uint32_t mp = m > 2u ? m - 3u : m + 9u;
    uint32_t doy = (153u * mp + 2u) / 5u + d - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097u + doe - 719468u;
}

int cb_config_add_holiday(cb_config *c, const char *alpha, uint32_t y, uint32_t m, uint32_t d)
{
    cb_ccy_profile *p = cb_config_ccy(c, alpha);
    if (!p) return CB_E_UNKNOWN_CCY;
    uint32_t day = cb_days_from_civil(y, m, d);
    if (day == 0xffffffffu) return CB_E_DATE;
    for (uint32_t i = 0; i < p->cal.n_holidays; i++)
        if (p->cal.holidays[i] == day) return CB_E_DUP;
    if (p->cal.n_holidays >= CB_MAX_HOLIDAYS) return CB_E_FULL;
    p->cal.holidays[p->cal.n_holidays++] = day;
    return CB_OK;
}

int cb_config_add_corridor(cb_config *c, const char *from, const char *to, uint64_t per_payment_max,
                           uint64_t per_cycle_max)
{
    if (!c) return CB_E_NULL;
    if (!cb_config_ccy(c, from) || !cb_config_ccy(c, to)) return CB_E_UNKNOWN_CCY;
    if (cb_config_corridor(c, from, to)) return CB_E_DUP;
    if (c->n_corr >= CB_MAX_CORRIDORS) return CB_E_FULL;
    cb_corridor *k = &c->corr[c->n_corr++];
    cb_memset(k, 0, sizeof *k);
    cb_strlcpy(k->from, from, sizeof k->from);
    cb_strlcpy(k->to, to, sizeof k->to);
    k->enabled = true;
    k->per_payment_max = per_payment_max;
    k->per_cycle_max = per_cycle_max;
    return CB_OK;
}

const cb_corridor *cb_config_corridor(const cb_config *c, const char *from, const char *to)
{
    if (!c) return 0;
    for (uint32_t i = 0; i < c->n_corr; i++)
        if (cb_streq(c->corr[i].from, from) && cb_streq(c->corr[i].to, to)) return &c->corr[i];
    return 0;
}

int cb_config_add_source(cb_config *c, uint8_t id, const char *name, const uint8_t pubkey[32],
                         uint32_t max_age_s)
{
    if (!c || !pubkey) return CB_E_NULL;
    if (max_age_s == 0) return CB_E_SOURCE;
    for (uint32_t i = 0; i < c->n_src; i++)
        if (c->src[i].id == id) return CB_E_DUP;
    if (c->n_src >= CB_MAX_FX_SOURCES) return CB_E_FULL;
    cb_fx_source *s = &c->src[c->n_src];
    cb_memset(s, 0, sizeof *s);
    s->id = id;
    if (!name || !name[0] || !cb_strlcpy(s->name, name, sizeof s->name)) return CB_E_NAME;
    cb_memcpy(s->pubkey, pubkey, 32);
    s->max_age_s = max_age_s;
    s->enabled = true;
    c->n_src++;
    return CB_OK;
}

int cb_config_validate(const cb_config *c)
{
    if (!c) return CB_E_NULL;
    if (c->rp.role < CB_ROLE_CENTRAL_BANK || c->rp.role > CB_ROLE_INDIVIDUAL) return CB_E_ROLE;
    if (!cb_bic_valid(c->operator_bic)) return CB_E_BIC;
    if (!cb_country_by_a2(c->home_country)) return CB_E_COUNTRY;
    if (c->rp.vfv_enabled && !(c->rp.perms & CB_PERM_VFV)) return CB_E_ROLE;
    if (c->n_ccy == 0) return CB_E_NO_CCY;
    for (uint32_t i = 0; i < c->n_ccy; i++) {
        const cb_ccy_profile *p = &c->ccy[i];
        if (is_vfv_code(p->alpha)) return CB_E_VFV_AS_CCY;
        const cb_ccy_t *k = cb_ccy_by_alpha(p->alpha);
        if (!cb_ccy_payable(k) || k->num != p->num || k->minor != p->minor) return CB_E_BAD_CCY;
        for (uint32_t j = 0; j < i; j++)
            if (cb_streq(c->ccy[j].alpha, p->alpha)) return CB_E_DUP;
        if (p->rounding != CB_ROUND_EXACT && p->rounding != CB_ROUND_FLOOR_REPORTED)
            return CB_E_ROUNDING;
        if (!window_ok(&p->cal)) return CB_E_CALENDAR;
        if (!cb_bic_valid(p->issuer_bic)) return CB_E_BIC;
        if (p->legal_tender && !cb_ccy_used_in(p->issuer_country, p->alpha))
            return CB_E_ISSUER_COUNTRY;
        if (p->reserve_bps > 10000u) return CB_E_RESERVE;
    }
    const cb_ccy_profile *u = cb_config_ccy_c(c, c->settle_unit);
    if (!u) return CB_E_UNIT;
    for (uint32_t i = 0; i < c->n_corr; i++)
        if (!cb_config_ccy_c(c, c->corr[i].from) || !cb_config_ccy_c(c, c->corr[i].to))
            return CB_E_UNKNOWN_CCY;
    if (c->n_src && !c->verify) return CB_E_NO_VERIFY;
    if (c->rp.screening_required && !c->screen) return CB_E_NO_SCREEN;
    if (c->n_fees > CB_MAX_FEES || cb_usury_check_schedule(c->fees, c->n_fees, 0) != CB_USURY_OK)
        return CB_E_USURY;
    return CB_OK;
}

cb_biz cb_business_status(const cb_ccy_profile *p, uint64_t now)
{
    int64_t local = (int64_t) now + (int64_t) p->cal.utc_offset_min * 60;
    if (local < 0) return CB_BIZ_CLOSED_DAY;
    uint64_t sod;
    uint32_t day = (uint32_t) cb_udiv64((uint64_t) local, 86400u, &sod);
    uint32_t minute = (uint32_t) sod / 60u;
    if (p->cal.weekend_mask & (1u << cb_weekday(day))) return CB_BIZ_CLOSED_DAY;
    for (uint32_t i = 0; i < p->cal.n_holidays && i < CB_MAX_HOLIDAYS; i++)
        if (p->cal.holidays[i] == day) return CB_BIZ_HOLIDAY;
    if (minute < p->cal.open_min) return CB_BIZ_BEFORE_OPEN;
    if (minute >= p->cal.cutoff_min) return CB_BIZ_AFTER_CUTOFF;
    return CB_BIZ_OPEN;
}

static uint32_t put_be(uint8_t *o, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) o[i] = (uint8_t) (v >> (8u * (n - 1u - i)));
    return n;
}

void cb_rate_canon(const cb_rate_rec *r, uint8_t out[CB_RATE_CANON_LEN])
{
    static const char tag[12] = {'Z', 'X', 'V', '-', 'C', 'B', 'R', 'A', 'T', 'E', '-', '1'};
    uint32_t n = 0;
    cb_memcpy(out, tag, 12);
    n = 12;
    cb_memcpy(out + n, r->ccy, 3);
    n += 3;
    cb_memcpy(out + n, r->unit, 3);
    n += 3;
    n += put_be(out + n, r->mant, 8);
    out[n++] = r->scale;
    n += put_be(out + n, r->published_at, 8);
    n += put_be(out + n, r->valid_until, 8);
    n += put_be(out + n, r->seq, 4);
    out[n++] = r->source_id;
}

static const cb_fx_source *find_src(const cb_config *c, uint8_t id)
{
    for (uint32_t i = 0; i < c->n_src; i++)
        if (c->src[i].id == id) return &c->src[i];
    return 0;
}

int cb_rate_check(const cb_config *c, const cb_rate_rec *r, uint64_t now)
{
    if (!c || !r) return CB_E_NULL;
    const cb_fx_source *s = find_src(c, r->source_id);
    if (!s || !s->enabled) return CB_E_SOURCE;
    if (!cb_streq(r->unit, c->settle_unit)) return CB_E_UNIT;
    if (!cb_config_ccy_c(c, r->ccy) || cb_streq(r->ccy, r->unit)) return CB_E_UNKNOWN_CCY;
    if (r->mant == 0 || r->mant > CB_RATE_MANT_MAX || r->scale > CB_RATE_SCALE_MAX)
        return CB_E_RATE;
    if (!c->verify) return CB_E_NO_VERIFY;
    uint8_t canon[CB_RATE_CANON_LEN];
    cb_rate_canon(r, canon);
    if (!c->verify(canon, sizeof canon, r->sig, s->pubkey)) return CB_E_SIG;
    if (r->published_at > now || r->valid_until <= now) return CB_E_STALE;
    if (now - r->published_at > s->max_age_s) return CB_E_STALE;
    return CB_OK;
}

bool cb_rate_fresh(const cb_config *c, const cb_rate_rec *r, uint64_t now)
{
    const cb_fx_source *s = find_src(c, r->source_id);
    if (!s || !s->enabled) return false;
    if (r->published_at > now || r->valid_until <= now) return false;
    return now - r->published_at <= s->max_age_s;
}
