/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_post.c — the posting bridge: one record -> one libzxv ledger posting
 * through zxv_ledger_post_cap. This file only reads the record exactly;
 * whether the posting is allowed (capability, funds, currency, duplicate
 * reference, A = L + E) is decided by the ledger, and its status is
 * returned unchanged. Integer code only (R11). */
#include <stdio.h>
#include <string.h>

#include "zs_internal.h"

/* A TEXT item without its trailing spaces, into dst[cap]. */
static int32_t read_text(const zs_layout *l, const uint8_t *rec, size_t rec_len, const char *ref,
                         char *dst, size_t cap, const char *what) {
    char b[64];
    size_t n = 0;
    uint32_t idx;
    int32_t st;
    if ((st = zs_find(l, ref, &idx, NULL)) != ZXV_OK) return st;
    if (l->items[idx].f.kind != ZS_K_TEXT) {
        zs_set_errorf("the %s field %s must be PIC X", what, l->items[idx].f.name);
        return ZXV_E_ARG;
    }
    if (l->items[idx].f.size >= sizeof b) {
        zs_set_errorf("the %s field %s is longer than %zu bytes", what, l->items[idx].f.name,
                      sizeof b - 1u);
        return ZS_E_FIT;
    }
    if ((st = zs_get_value(l, rec, rec_len, ref, b, sizeof b, &n)) != ZXV_OK) return st;
    while (n > 0 && b[n - 1u] == ' ') n--;
    b[n] = '\0';
    if (n >= cap) {
        zs_set_errorf("the %s '%s' is longer than %zu bytes", what, b, cap - 1u);
        return ZS_E_FIT;
    }
    memcpy(dst, b, n + 1u);
    return ZXV_OK;
}

static int32_t read_account(const zs_layout *l, const uint8_t *rec, size_t rec_len,
                            const char *ref, uint32_t *out) {
    int64_t v;
    uint32_t sc;
    int32_t st = zs_get_decimal(l, rec, rec_len, ref, &v, &sc);
    if (st != ZXV_OK) return st;
    if (v % zs_pow10[sc] != 0 || v < 0 || v / zs_pow10[sc] > (int64_t) UINT32_MAX) {
        zs_set_errorf("account %s is not a whole number in 0..4294967295", ref);
        return ZS_E_FIT;
    }
    *out = (uint32_t) (v / zs_pow10[sc]);
    return ZXV_OK;
}

int32_t zs_posting_read(const zs_layout *l, const zs_posting_map *m, const uint8_t *rec,
                        size_t rec_len, zs_posting *out) {
    int64_t v;
    uint32_t sc;
    int32_t st, mu;
    if (!out) return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    if (!l || !m || !rec || !m->amount || !m->from || !m->to || !m->reference ||
        (!m->currency && !m->fixed_currency) || m->minor_units < -1 || m->minor_units > 18)
        return ZXV_E_ARG;
    if (m->currency) {
        if ((st = read_text(l, rec, rec_len, m->currency, out->currency, sizeof out->currency,
                            "currency")) != ZXV_OK)
            return st;
    } else {
        size_t n = strlen(m->fixed_currency);
        if (n >= sizeof out->currency) return ZXV_E_ARG;
        memcpy(out->currency, m->fixed_currency, n + 1u);
    }
    if ((st = read_account(l, rec, rec_len, m->from, &out->from)) != ZXV_OK) return st;
    if ((st = read_account(l, rec, rec_len, m->to, &out->to)) != ZXV_OK) return st;
    if ((st = read_text(l, rec, rec_len, m->reference, out->reference, sizeof out->reference,
                        "reference")) != ZXV_OK)
        return st;
    mu = m->minor_units;
    if (mu < 0) {
        uint8_t minor = 0;
        st = zxv_iso4217_by_alpha(out->currency, NULL, &minor, NULL);
        if (st != ZXV_OK || minor == ZXV_MINOR_NA || minor > 18) {
            zs_set_errorf("currency '%s' has no ISO 4217 minor unit; set minor_units",
                          out->currency);
            return ZXV_E_CURRENCY;
        }
        mu = minor;
    }
    if ((st = zs_get_decimal(l, rec, rec_len, m->amount, &v, &sc)) != ZXV_OK) return st;
    if ((uint32_t) mu >= sc) {
        int64_t mul = zs_pow10[(uint32_t) mu - sc];
        if (v > INT64_MAX / mul || v < -(INT64_MAX / mul)) {
            zs_set_errorf("amount does not fit an int64 in minor units");
            return ZS_E_FIT;
        }
        out->amount_minor = v * mul;
    } else {
        int64_t div = zs_pow10[sc - (uint32_t) mu];
        if (v % div != 0) {
            char t[32];
            zs_decimal_text(v, sc, t);
            zs_set_errorf("amount %s has more decimals than %s's %d minor units (never rounded)",
                          t, out->currency, (int) mu);
            return ZS_E_FIT;
        }
        out->amount_minor = v / div;
    }
    return ZXV_OK;
}

int32_t zs_post(const zxv_cap *cap, const zs_layout *l, const zs_posting_map *m,
                const uint8_t *rec, size_t rec_len, zs_posting *out) {
    zs_posting p;
    uint64_t id = 0;
    int32_t st = zs_posting_read(l, m, rec, rec_len, &p);
    if (st == ZXV_OK) {
        st = zxv_ledger_post_cap(cap, p.from, p.to, p.amount_minor, p.currency, p.reference, &id);
        if (st == ZXV_OK) p.entry_id = id;
        else {
            char why[256];
            size_t n = 0;
            if (zxv_last_error(why, sizeof why, &n) != ZXV_OK) why[0] = '\0';
            zs_set_errorf("ledger refused %s: %s%s%s", p.reference, zxv_status_name(st),
                          why[0] ? ": " : "", why);
        }
    }
    if (out) *out = p;
    return st;
}
