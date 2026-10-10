/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_ccy.c — lookups over the generated tables. See cb_ccy.h. */
#include "cb_ccy.h"
#include "cb_util.h"

static int cmpn(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char) a[i] < (unsigned char) b[i] ? -1 : 1;
        if (!a[i]) return 0;
    }
    return 0;
}

const cb_ccy_t *cb_ccy_by_alpha(const char *alpha)
{
    if (!cb_is_upper_n(alpha, 3)) return 0;
    uint32_t lo = 0, hi = CB_CCY_COUNT;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        int c = cmpn(cb_ccy_tbl[mid].alpha, alpha, 4);
        if (c == 0) return &cb_ccy_tbl[mid];
        if (c < 0)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return 0;
}

const cb_ccy_t *cb_ccy_by_num(uint16_t num)
{
    uint32_t lo = 0, hi = CB_CCY_COUNT;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        const cb_ccy_t *c = &cb_ccy_tbl[cb_ccy_by_num_idx[mid]];
        if (c->num == num) return c;
        if (c->num < num)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return 0;
}

bool cb_ccy_payable(const cb_ccy_t *c)
{
    if (!c || (c->flags & CB_CCYF_NA) || c->minor > 4u) return false;
    return !cb_streq(c->alpha, "XTS") && !cb_streq(c->alpha, "XXX");
}

const cb_country_t *cb_country_by_a2(const char *a2)
{
    if (!cb_is_upper_n(a2, 2)) return 0;
    uint32_t lo = 0, hi = CB_COUNTRY_COUNT;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        int c = cmpn(cb_country_tbl[mid].a2, a2, 3);
        if (c == 0) return &cb_country_tbl[mid];
        if (c < 0)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return 0;
}

const cb_country_t *cb_country_by_a3(const char *a3)
{
    if (!cb_is_upper_n(a3, 3)) return 0;
    for (uint32_t i = 0; i < CB_COUNTRY_COUNT; i++)
        if (cmpn(cb_country_tbl[i].a3, a3, 4) == 0) return &cb_country_tbl[i];
    return 0;
}

const cb_country_t *cb_country_by_num(uint16_t num)
{
    for (uint32_t i = 0; i < CB_COUNTRY_COUNT; i++)
        if (cb_country_tbl[i].num == num) return &cb_country_tbl[i];
    return 0;
}

size_t cb_ccy_for_country(const char *a2, const cb_ccy_t **out, size_t max)
{
    size_t n = 0;
    if (!cb_is_upper_n(a2, 2)) return 0;
    for (uint32_t i = 0; i < CB_CTRY_CCY_COUNT; i++) {
        if (cmpn(cb_ctry_ccy_tbl[i].a2, a2, 3) != 0) continue;
        if (out && n < max) out[n] = &cb_ccy_tbl[cb_ctry_ccy_tbl[i].ccy];
        n++;
    }
    return n;
}

bool cb_ccy_used_in(const char *a2, const char *alpha)
{
    const cb_ccy_t *c = cb_ccy_by_alpha(alpha);
    if (!c || !cb_is_upper_n(a2, 2)) return false;
    for (uint32_t i = 0; i < CB_CTRY_CCY_COUNT; i++)
        if (cmpn(cb_ctry_ccy_tbl[i].a2, a2, 3) == 0 && &cb_ccy_tbl[cb_ctry_ccy_tbl[i].ccy] == c)
            return true;
    return false;
}

const cb_au_member_t *cb_au_by_a2(const char *a2)
{
    if (!cb_is_upper_n(a2, 2)) return 0;
    for (uint32_t i = 0; i < CB_AU_COUNT; i++)
        if (cmpn(cb_au_tbl[i].a2, a2, 3) == 0) return &cb_au_tbl[i];
    return 0;
}

bool cb_is_au_member(const char *a2)
{
    return cb_au_by_a2(a2) != 0;
}
