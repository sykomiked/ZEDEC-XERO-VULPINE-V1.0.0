/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ze_post.c — the posting bridge: one record -> one libzxv ledger posting
 * through zxv_ledger_post_cap. This file only reads the record exactly;
 * whether the posting is allowed (capability, funds, currency, duplicate
 * reference, balance) is decided by the ledger, and its status is returned
 * unchanged. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "ze_internal.h"

/* A String component without its trailing spaces, or an enumeration
 * literal in upper case, into dst[cap]. */
static int32_t read_text(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                         char *dst, size_t cap, const char *what, int allow_enum) {
    char b[80];
    size_t n = 0;
    uint32_t t;
    int32_t st;
    if ((st = ze_find(p, type, path, &t, NULL)) != ZXV_OK) return st;
    if (p->types[t].t.kind == ZE_K_ENUM && allow_enum) {
        if ((st = ze_get_value(p, type, val, len, path, b, sizeof b, &n)) != ZXV_OK) return st;
        for (size_t k = 0; k < n; k++)
            if (b[k] >= 'a' && b[k] <= 'z') b[k] = (char) (b[k] - 32);
    } else if (p->types[t].t.kind == ZE_K_TEXT) {
        if (p->types[t].t.size >= sizeof b) {
            ze_set_errorf("the %s component %s is longer than %zu characters", what, path, sizeof b - 1u);
            return ZE_E_FIT;
        }
        if ((st = ze_get_value(p, type, val, len, path, b, sizeof b, &n)) != ZXV_OK) return st;
        while (n > 0 && b[n - 1u] == ' ') n--;
        b[n] = '\0';
    } else {
        ze_set_errorf("the %s component %s must be a String%s", what, path, allow_enum ? " or an enumeration" : "");
        return ZXV_E_ARG;
    }
    if (n >= cap) {
        ze_set_errorf("the %s '%s' is longer than %zu bytes", what, b, cap - 1u);
        return ZE_E_FIT;
    }
    memcpy(dst, b, n + 1u);
    return ZXV_OK;
}

static int32_t read_account(const ze_pkg *p, uint32_t type, const uint8_t *val, size_t len, const char *path,
                            uint32_t *out) {
    int64_t v;
    uint32_t sc;
    int32_t st = ze_get_decimal(p, type, val, len, path, &v, &sc);
    if (st != ZXV_OK) return st;
    if (v % ze_pow10[sc] != 0 || v < 0 || v / ze_pow10[sc] > (int64_t) UINT32_MAX) {
        ze_set_errorf("account %s is not a whole number in 0 .. 4294967295", path);
        return ZE_E_FIT;
    }
    *out = (uint32_t) (v / ze_pow10[sc]);
    return ZXV_OK;
}

int32_t ze_posting_read(const ze_pkg *p, uint32_t type, const ze_posting_map *m, const uint8_t *val, size_t len,
                        ze_posting *out) {
    int64_t v;
    uint32_t sc;
    int32_t st, mu;
    if (!out) return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    if (!p || !m || !val || !m->amount || !m->from || !m->to || !m->reference ||
        (!m->currency && !m->fixed_currency) || m->minor_units < -1 || m->minor_units > 18)
        return ZXV_E_ARG;
    if (m->currency) {
        if ((st = read_text(p, type, val, len, m->currency, out->currency, sizeof out->currency, "currency", 1)) !=
            ZXV_OK)
            return st;
    } else {
        size_t n = strlen(m->fixed_currency);
        if (n >= sizeof out->currency) return ZXV_E_ARG;
        memcpy(out->currency, m->fixed_currency, n + 1u);
    }
    if ((st = read_account(p, type, val, len, m->from, &out->from)) != ZXV_OK) return st;
    if ((st = read_account(p, type, val, len, m->to, &out->to)) != ZXV_OK) return st;
    if ((st = read_text(p, type, val, len, m->reference, out->reference, sizeof out->reference, "reference", 0)) !=
        ZXV_OK)
        return st;
    mu = m->minor_units;
    if (mu < 0) {
        uint8_t minor = 0;
        st = zxv_iso4217_by_alpha(out->currency, NULL, &minor, NULL);
        if (st != ZXV_OK || minor == ZXV_MINOR_NA || minor > 18) {
            ze_set_errorf("currency '%s' has no ISO 4217 minor unit; set minor_units", out->currency);
            return ZXV_E_CURRENCY;
        }
        mu = minor;
    }
    if ((st = ze_get_decimal(p, type, val, len, m->amount, &v, &sc)) != ZXV_OK) return st;
    if ((uint32_t) mu >= sc) {
        int64_t mul = ze_pow10[(uint32_t) mu - sc];
        if (v > INT64_MAX / mul || v < -(INT64_MAX / mul)) {
            ze_set_errorf("amount does not fit an int64 in minor units");
            return ZE_E_FIT;
        }
        out->amount_minor = v * mul;
    } else {
        int64_t div = ze_pow10[sc - (uint32_t) mu];
        if (v % div != 0) {
            ze_sb s = {0};
            ze_sb_dec(&s, v, sc);
            ze_set_errorf("amount %s has more decimals than %s's %d minor units (never rounded)",
                          s.failed ? "?" : s.p, out->currency, (int) mu);
            free(s.p);
            return ZE_E_FIT;
        }
        out->amount_minor = v / div;
    }
    return ZXV_OK;
}

int32_t ze_post(const zxv_cap *cap, const ze_pkg *p, uint32_t type, const ze_posting_map *m, const uint8_t *val,
                size_t len, ze_posting *out) {
    ze_posting ps;
    uint64_t id = 0;
    int32_t st = ze_posting_read(p, type, m, val, len, &ps);
    if (st == ZXV_OK) {
        st = zxv_ledger_post_cap(cap, ps.from, ps.to, ps.amount_minor, ps.currency, ps.reference, &id);
        if (st == ZXV_OK) ps.entry_id = id;
        else {
            char why[256];
            size_t n = 0;
            if (zxv_last_error(why, sizeof why, &n) != ZXV_OK) why[0] = '\0';
            ze_set_errorf("ledger refused %s: %s%s%s", ps.reference, zxv_status_name(st), why[0] ? ": " : "", why);
        }
    }
    if (out) *out = ps;
    return st;
}
