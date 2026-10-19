/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_post.c — the posting bridge: one global node -> one libzxv ledger
 * posting through zxv_ledger_post_cap. This file only reads the record
 * exactly; whether the posting is allowed (capability, funds, currency,
 * duplicate reference, A = L + E) is decided by the ledger, and its
 * status is returned unchanged. Integer code only (R11). */
#include <stdio.h>
#include <string.h>

#include "zm_internal.h"

static int32_t read_account(const zm_record *r, const char *name, uint32_t *out) {
    int64_t v;
    uint32_t sc;
    int32_t st = zm_get_decimal(r, name, &v, &sc);
    if (st != ZXV_OK) return st;
    if (v % zm_pow10[sc] != 0 || v < 0 || v / zm_pow10[sc] > (int64_t) UINT32_MAX) {
        zm_set_errorf("account %s is not a whole number in 0..4294967295", name);
        return ZM_E_FIT;
    }
    *out = (uint32_t) (v / zm_pow10[sc]);
    return ZXV_OK;
}

/* A field's M text (or the record's reference) into dst[cap]. */
static int32_t read_text(const zm_record *r, const char *name, char *dst, size_t cap,
                         const char *what) {
    char b[ZM_MAX_REF + 1];
    size_t n = 0;
    int32_t st = name ? zm_get_mtext(r, name, b, sizeof b, &n) : zm_record_ref(r, b, sizeof b, &n);
    if (st == ZXV_E_BUFFER) {
        zm_set_errorf("the %s is longer than %zu bytes", what, cap - 1u);
        return ZM_E_FIT;
    }
    if (st != ZXV_OK) return st;
    if (!name) {
        memmove(b, b + 1, n); /* drop the "^" */
        n--;
    }
    if (n == 0) {
        zm_set_errorf("the %s field %s is empty", what, name ? name : "?");
        return ZXV_E_FIELD;
    }
    if (n >= cap) {
        zm_set_errorf("the %s '%s' is longer than %zu bytes", what, b, cap - 1u);
        return ZM_E_FIT;
    }
    memcpy(dst, b, n + 1u);
    return ZXV_OK;
}

int32_t zm_posting_read(const zm_record *r, const zm_posting_map *m, zm_posting *out) {
    int64_t v;
    uint32_t sc;
    int32_t st, mu;
    if (!out) return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    if (!r || !m || !m->amount || !m->from || !m->to || (!m->currency && !m->fixed_currency) ||
        m->minor_units < -1 || m->minor_units > 18)
        return ZXV_E_ARG;
    if (m->currency) {
        if ((st = read_text(r, m->currency, out->currency, sizeof out->currency, "currency")) !=
            ZXV_OK)
            return st;
    } else {
        size_t n = strlen(m->fixed_currency);
        if (n == 0 || n >= sizeof out->currency) return ZXV_E_ARG;
        memcpy(out->currency, m->fixed_currency, n + 1u);
    }
    if ((st = read_account(r, m->from, &out->from)) != ZXV_OK) return st;
    if ((st = read_account(r, m->to, &out->to)) != ZXV_OK) return st;
    if ((st = read_text(r, m->reference, out->reference, sizeof out->reference, "reference")) !=
        ZXV_OK)
        return st;
    mu = m->minor_units;
    if (mu < 0) {
        uint8_t minor = 0;
        st = zxv_iso4217_by_alpha(out->currency, NULL, &minor, NULL);
        if (st != ZXV_OK || minor == ZXV_MINOR_NA || minor > 18) {
            zm_set_errorf("currency '%s' has no ISO 4217 minor unit; set minor_units",
                          out->currency);
            return ZXV_E_CURRENCY;
        }
        mu = minor;
    }
    if ((st = zm_get_decimal(r, m->amount, &v, &sc)) != ZXV_OK) return st;
    if ((uint32_t) mu >= sc) {
        int64_t mul = zm_pow10[(uint32_t) mu - sc];
        if (v > INT64_MAX / mul || v < -(INT64_MAX / mul)) {
            zm_set_errorf("amount does not fit an int64 in minor units");
            return ZM_E_FIT;
        }
        out->amount_minor = v * mul;
    } else {
        int64_t div = zm_pow10[sc - (uint32_t) mu];
        if (v % div != 0) {
            char t[32];
            zm_dec_text(v, sc, t);
            zm_set_errorf("amount %s has more decimals than %s's %d minor units (never rounded)",
                          t, out->currency, (int) mu);
            return ZM_E_FIT;
        }
        out->amount_minor = v / div;
    }
    return ZXV_OK;
}

int32_t zm_post(const zxv_cap *cap, const zm_record *r, const zm_posting_map *m,
                zm_posting *out) {
    zm_posting p;
    uint64_t id = 0;
    int32_t st = zm_posting_read(r, m, &p);
    if (st == ZXV_OK) {
        st = zxv_ledger_post_cap(cap, p.from, p.to, p.amount_minor, p.currency, p.reference, &id);
        if (st == ZXV_OK) {
            p.entry_id = id;
        } else {
            char why[256];
            size_t n = 0;
            if (zxv_last_error(why, sizeof why, &n) != ZXV_OK) why[0] = '\0';
            zm_set_errorf("ledger refused %s: %s%s%s", p.reference, zxv_status_name(st),
                          why[0] ? ": " : "", why);
        }
    }
    if (out) *out = p;
    return st;
}
