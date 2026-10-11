/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_util.c — last error, findings, safe text, value-type names. Integer
 * code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zau_internal.h"

static _Thread_local char g_err[512];

void zau_set_error(const char *msg) {
    snprintf(g_err, sizeof g_err, "%s", msg ? msg : "");
}

int32_t zau_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1u);
    return ZXV_OK;
}

const char *zau_status_name(int32_t status) {
    if (status == ZAU_E_INPUT) return "ZAU_E_INPUT";
    return zxv_status_name(status);
}

int zau_clean(char *out, size_t cap, const uint8_t *s, size_t n) {
    size_t i = 0, k = 0;
    if (!cap) return n != 0;
    while (i < n && k + 1u < cap) {
        uint8_t c = s[i];
        size_t len = c < 0x80u ? 1u : (c & 0xE0u) == 0xC0u ? 2u : (c & 0xF0u) == 0xE0u ? 3u
                                     : (c & 0xF8u) == 0xF0u ? 4u : 0u;
        if (len == 1u) {
            out[k++] = (c < 0x20u || c == 0x7Fu) ? '?' : (char) c;
            i++;
        } else if (len && i + len <= n && zw_utf8_ok(s + i, len)) {
            if (k + len >= cap) break;
            memcpy(out + k, s + i, len);
            k += len;
            i += len;
        } else {
            out[k++] = '?';
            i++;
        }
    }
    out[k] = '\0';
    return i < n;
}

void zau_add(zau_report *r, uint32_t check, int32_t severity, const char *code, const char *exp,
             int64_t func, int64_t offset, const char *fmt, ...) {
    char raw[600];
    va_list ap;
    zau_check *c;
    zau_finding *f;
    size_t n;
    if (!r || check >= ZAU_CHECKS || severity < ZAU_INFO || severity > ZAU_ERROR) return;
    c = &r->checks[check];
    c->n[severity]++;
    if (severity == ZAU_ERROR) r->errors++;
    if (severity == ZAU_WARNING) r->warnings++;
    if (c->n[0] + c->n[1] + c->n[2] > ZAU_PER_CHECK_MAX || r->nf >= ZAU_MAX_FINDINGS) {
        c->suppressed++;
        return;
    }
    if (r->nf == r->fcap) {
        uint32_t nc = r->fcap ? r->fcap * 2u : 32u;
        zau_finding *nf = (zau_finding *) realloc(r->f, (size_t) nc * sizeof *nf);
        if (!nf) {
            r->oom = 1;
            c->suppressed++;
            return;
        }
        r->f = nf;
        r->fcap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(raw, sizeof raw, fmt, ap);
    va_end(ap);
    n = strlen(raw);
    f = &r->f[r->nf];
    memset(f, 0, sizeof *f);
    f->message = (char *) malloc(n + 1u);
    if (!f->message) {
        r->oom = 1;
        c->suppressed++;
        return;
    }
    (void) zau_clean(f->message, n + 1u, (const uint8_t *) raw, n);
    f->check = check;
    f->severity = severity;
    snprintf(f->code, sizeof f->code, "%s", code ? code : "");
    if (exp) (void) zau_clean(f->exp, sizeof f->exp, (const uint8_t *) exp, strlen(exp));
    f->func = func;
    f->offset = offset;
    r->nf++;
}

void zau_skip(zau_report *r, uint32_t check, const char *why) {
    if (!r || check >= ZAU_CHECKS) return;
    r->checks[check].skipped = 1;
    zau_summary(r, check, "skipped: %s", why);
}

void zau_summary(zau_report *r, uint32_t check, const char *fmt, ...) {
    char raw[200];
    va_list ap;
    if (!r || check >= ZAU_CHECKS) return;
    va_start(ap, fmt);
    vsnprintf(raw, sizeof raw, fmt, ap);
    va_end(ap);
    (void) zau_clean(r->checks[check].summary, sizeof r->checks[check].summary,
                     (const uint8_t *) raw, strlen(raw));
}

const char *zau_vt_name(uint8_t t) {
    switch (t) {
    case WT_I32: return "i32";
    case WT_I64: return "i64";
    case WT_F32: return "f32";
    case WT_F64: return "f64";
    case WT_V128: return "v128";
    case WT_FUNCREF: return "funcref";
    case WT_EXTERNREF: return "externref";
    default: return "?";
    }
}

int zau_vt_float(uint8_t t) { return t == WT_F32 || t == WT_F64 || t == WT_V128; }

void zau_sig(const zau_ftype *t, char *out, size_t cap) {
    size_t k = 0;
    if (!cap) return;
    if (t->np > ZAU_MAX_SIG || t->nr > 1u) {
        snprintf(out, cap, "?");
        return;
    }
#define PUT(ch)                                                                                \
    do {                                                                                       \
        if (k + 1u < cap) out[k++] = (ch);                                                     \
    } while (0)
    PUT(t->nr == 0 ? 'v' : t->r[0] == WT_I32 ? 'i' : t->r[0] == WT_I64 ? 'I' : '?');
    PUT('(');
    for (uint32_t i = 0; i < t->np; i++) PUT(t->p[i] == WT_I32 ? 'i' : t->p[i] == WT_I64 ? 'I' : '?');
    PUT(')');
#undef PUT
    out[k] = '\0';
}
