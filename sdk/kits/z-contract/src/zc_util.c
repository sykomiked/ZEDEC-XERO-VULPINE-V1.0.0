/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_util.c — status names, last error, UTF-8, text buffers, the
 * 64-state tag (on libzxv ABI 1.3 zxv_hex_*). Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"

static _Thread_local char g_err[512];

void zc_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void zc_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t zc_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1u);
    return ZXV_OK;
}

const char *zc_status_name(int32_t status) {
    switch (status) {
    case ZC_E_SYNTAX: return "ZC_E_SYNTAX";
    case ZC_E_VIOLATION: return "ZC_E_VIOLATION";
    case ZC_E_MAPPING: return "ZC_E_MAPPING";
    case ZC_E_LOG: return "ZC_E_LOG";
    default: return zxv_status_name(status);
    }
}

const char *zc_reason_name(int32_t reason) {
    static const char *const n[] = {"OK",          "ARITY", "KIND",        "INVALID",
                                    "NOT_INTEGER", "BELOW", "ABOVE",       "DENOMINATOR",
                                    "NOT_IN_SET",  "LENGTH", "COUNT"};
    if (reason < 0 || reason > ZC_V_COUNT) return "UNKNOWN";
    return n[reason];
}

void zc_out_msg(const char *src, char *msg, size_t cap, size_t *out_len) {
    size_t n = strlen(src);
    if (out_len) *out_len = n;
    if (msg && cap) {
        size_t k = n < cap - 1u ? n : cap - 1u;
        memcpy(msg, src, k);
        msg[k] = '\0';
    }
}

size_t zc_utf8_check(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint32_t cp;
        size_t k;
        if (c < 0x80u) {
            i++;
            continue;
        } else if ((c & 0xE0u) == 0xC0u) {
            k = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0u) == 0xE0u) {
            k = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            k = 3;
            cp = c & 0x07u;
        } else {
            return i;
        }
        if (n - i <= k) return i;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0u) != 0x80u) return i;
            cp = (cp << 6) | (s[i + j] & 0x3Fu);
        }
        if ((k == 1 && cp < 0x80u) || (k == 2 && cp < 0x800u) || (k == 3 && cp < 0x10000u) ||
            cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
            return i;
        i += k + 1u;
    }
    return n;
}

size_t zc_utf8_cut(const char *s, size_t n, size_t max) {
    if (n <= max) return n;
    n = max;
    while (n > 0 && (((const uint8_t *) s)[n] & 0xC0u) == 0x80u) n--;
    return n;
}

size_t zc_i64_text(int64_t v, char *b) {
    char t[24];
    size_t k = 0, n = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[k++] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) b[n++] = '-';
    while (k) b[n++] = t[--k];
    b[n] = '\0';
    return n;
}

/* ----- growable text buffer ----- */

void zc_buf_putn(zc_buf *b, const char *s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1u > b->cap) {
        size_t nc = b->cap ? b->cap : 256u;
        while (nc < b->n + n + 1u) nc *= 2u;
        char *nb = (char *) realloc(b->b, nc);
        if (!nb) {
            b->oom = 1;
            return;
        }
        b->b = nb;
        b->cap = nc;
    }
    memcpy(b->b + b->n, s, n);
    b->n += n;
    b->b[b->n] = '\0';
}

void zc_buf_puts(zc_buf *b, const char *s) { zc_buf_putn(b, s, strlen(s)); }

void zc_buf_i64(zc_buf *b, int64_t v) {
    char t[24];
    zc_buf_putn(b, t, zc_i64_text(v, t));
}

void zc_buf_u64(zc_buf *b, uint64_t v) {
    char t[24];
    size_t k = 0;
    char o[24];
    size_t n = 0;
    do {
        t[k++] = (char) ('0' + (int) (v % 10u));
        v /= 10u;
    } while (v);
    while (k) o[n++] = t[--k];
    zc_buf_putn(b, o, n);
}

void zc_buf_rat(zc_buf *b, int64_t n, int64_t d) {
    char t[96];
    size_t len = 0;
    if (zxv_rat_format(n, d, t, sizeof t, &len) == ZXV_OK) {
        zc_buf_putn(b, t, len);
    } else { /* cannot happen for a reduced rational; keep it exact */
        zc_buf_i64(b, n);
        zc_buf_puts(b, "/");
        zc_buf_i64(b, d);
    }
}

/* ----- the 64-state tag: libzxv ABI 1.3 (zxv_hex_*) ----- */

/* The name tables are the parser's vocabulary for tag{...} patterns; each
 * position's index is the ZXV_HEX_* code (test_check compares every tag's
 * text with zxv_hex_name). */
const char *const zc_base_names[4] = {"false", "neither", "both", "true"};
const char *const zc_pol_names[4] = {"entropic", "neutral", "forbidden", "negentropic"};
const char *const zc_phase_names[4] = {"potential", "emerging", "dissolving", "manifest"};

int32_t zc_tag_make(int32_t lp_state, int32_t polarity, int32_t phase, int32_t *out_tag) {
    uint8_t base = 0, tag = 0;
    if (!out_tag || lp_state < 0 || lp_state > 3 || polarity < 0 || polarity > 3 || phase < 0 ||
        phase > 3 || zxv_hex_from_lp(lp_state, &base) != ZXV_OK ||
        zxv_hex_pack(base, (uint8_t) polarity, (uint8_t) phase, &tag) != ZXV_OK) {
        zc_set_error("zc_tag_make: value out of range");
        return ZXV_E_ARG;
    }
    *out_tag = tag;
    return ZXV_OK;
}

int32_t zc_tag_split(int32_t tag, int32_t *out_lp_state, int32_t *out_polarity,
                     int32_t *out_phase) {
    uint8_t base = 0, pol = 0, ph = 0;
    int32_t lp = 0;
    if (tag < 0 || tag > 63 || zxv_hex_unpack((uint8_t) tag, &base, &pol, &ph) != ZXV_OK ||
        zxv_hex_to_lp(base, &lp) != ZXV_OK) {
        zc_set_error("zc_tag_split: a tag is 0..63");
        return ZXV_E_ARG;
    }
    if (out_lp_state) *out_lp_state = lp;
    if (out_polarity) *out_polarity = pol;
    if (out_phase) *out_phase = ph;
    return ZXV_OK;
}

int32_t zc_tag_action(int32_t tag, int32_t *out_action) {
    if (!out_action || tag < 0 || tag > 63) {
        zc_set_error("zc_tag_action: a tag is 0..63");
        return ZXV_E_ARG;
    }
    return zxv_hex_action((uint8_t) tag, out_action);
}

static int name_index(const char *const names[4], const char *s, size_t n) {
    for (int i = 0; i < 4; i++)
        if (strlen(names[i]) == n && memcmp(names[i], s, n) == 0) return i;
    return -1;
}

int32_t zc_tag_parse(const char *text, int32_t *out_tag) {
    const char *a, *b;
    int x, y, z;
    uint8_t tag = 0;
    if (!text || !out_tag) return ZXV_E_ARG;
    a = strchr(text, '/');
    b = a ? strchr(a + 1, '/') : NULL;
    if (!a || !b || strchr(b + 1, '/')) {
        zc_set_error("zc_tag_parse: expected base/polarity/phase");
        return ZXV_E_FORMAT;
    }
    x = name_index(zc_base_names, text, (size_t) (a - text));
    y = name_index(zc_pol_names, a + 1, (size_t) (b - a - 1));
    z = name_index(zc_phase_names, b + 1, strlen(b + 1));
    if (x < 0 || y < 0 || z < 0) {
        zc_set_errorf("zc_tag_parse: unknown %s name", x < 0 ? "base" : y < 0 ? "polarity" : "phase");
        return ZXV_E_FORMAT;
    }
    if (zxv_hex_pack((uint8_t) x, (uint8_t) y, (uint8_t) z, &tag) != ZXV_OK) return ZXV_E_INTERNAL;
    *out_tag = tag;
    return ZXV_OK;
}

int32_t zc_tag_format(int32_t tag, char *buf, size_t cap, size_t *out_len) {
    const char *t;
    size_t n;
    if (!out_len || tag < 0 || tag > 63) return ZXV_E_ARG;
    t = zxv_hex_name((uint8_t) tag);
    n = strlen(t);
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, t, n + 1u);
    return ZXV_OK;
}

void zc_val_text(const zc_val *v, char *out, size_t cap) {
    char t[96];
    size_t n = 0;
    switch (v->kind) {
    case ZC_T_INT: zc_i64_text(v->num, t); break;
    case ZC_T_RAT: {
        int64_t rn = 0, rd = 0;
        char a[24], b[24];
        if (zxv_rat_normalize(v->num, v->den, &rn, &rd) != ZXV_OK ||
            zxv_rat_format(rn, rd, t, sizeof t, &n) != ZXV_OK) {
            zc_i64_text(v->num, a);
            zc_i64_text(v->den, b);
            snprintf(t, sizeof t, "%s/%s", a, b);
        } else if (rd != 1 && !strchr(t, '/')) {
            /* a terminating decimal: also show the reduced fraction, whose
             * denominator is what den<= and den| judge */
            zc_i64_text(rn, a);
            zc_i64_text(rd, b);
            snprintf(t + n, sizeof t - n, " (%s/%s)", a, b);
        }
        break;
    }
    case ZC_T_TRUTH:
        snprintf(t, sizeof t, "%s",
                 v->num >= 0 && v->num <= 3 ? zxv_lp_name((int32_t) v->num) : "invalid truth");
        break;
    case ZC_T_TAG:
        if (zc_tag_format((int32_t) (v->num >= 0 && v->num <= 63 ? v->num : -1), t, sizeof t,
                          &n) != ZXV_OK) {
            char a[24];
            zc_i64_text(v->num, a);
            snprintf(t, sizeof t, "tag %s", a);
        }
        break;
    case ZC_T_BYTES: snprintf(t, sizeof t, "%zu bytes", v->len); break;
    default: snprintf(t, sizeof t, "?"); break;
    }
    snprintf(out, cap, "%s", t);
}
