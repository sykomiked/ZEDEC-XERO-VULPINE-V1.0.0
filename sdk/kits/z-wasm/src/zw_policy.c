/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw_policy.c — validator policy, status and reason names, last error.
 * Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zw_wasm.h"

/* ----- Last error (thread-local, like libzxv R7) ----- */

static _Thread_local char g_err[256];

void zw_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

void zw_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t zw_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1u);
    return ZXV_OK;
}

/* ----- Names ----- */

const char *zw_status_name(int32_t status) {
    switch (status) {
    case ZW_E_REJECTED: return "ZW_E_REJECTED";
    case ZW_E_LINK: return "ZW_E_LINK";
    case ZW_E_TRAP: return "ZW_E_TRAP";
    case ZW_E_FUEL: return "ZW_E_FUEL";
    case ZW_E_EXPORT: return "ZW_E_EXPORT";
    case ZW_E_DEAD: return "ZW_E_DEAD";
    case ZW_E_CHECK: return "ZW_E_CHECK";
    default: return zxv_status_name(status);
    }
}

const char *zw_reason_name(int32_t reason) {
    switch (reason) {
    case ZW_R_OK: return "OK";
    case ZW_R_MALFORMED: return "MALFORMED";
    case ZW_R_TOO_LARGE: return "TOO_LARGE";
    case ZW_R_FLOAT_TYPE: return "FLOAT_TYPE";
    case ZW_R_FLOAT_OP: return "FLOAT_OP";
    case ZW_R_SIMD: return "SIMD";
    case ZW_R_MEMORY_NO_MAX: return "MEMORY_NO_MAX";
    case ZW_R_MEMORY_LIMIT: return "MEMORY_LIMIT";
    case ZW_R_TABLE_LIMIT: return "TABLE_LIMIT";
    case ZW_R_IMPORT_DENIED: return "IMPORT_DENIED";
    case ZW_R_IMPORT_KIND: return "IMPORT_KIND";
    case ZW_R_IMPORT_SIG: return "IMPORT_SIG";
    case ZW_R_START: return "START";
    case ZW_R_LIMIT: return "LIMIT";
    case ZW_R_RESERVED_NAME: return "RESERVED_NAME";
    case ZW_R_UNSUPPORTED: return "UNSUPPORTED";
    case ZW_R_INDEX: return "INDEX";
    default: return "UNKNOWN";
    }
}

/* ----- Policy ----- */

/* Defaults and the largest value each limit accepts. */
static const uint64_t k_default[ZW_LIM_COUNT + 1] = {
    0,
    UINT64_C(1) << 20, /* MODULE_BYTES  1 MiB   */
    16,                /* MEMORY_PAGES  1 MiB   */
    1024,              /* TABLE_ELEMS           */
    4096,              /* FUNCTIONS             */
    UINT64_C(128) << 10, /* BODY_BYTES          */
    1024,              /* LOCALS                */
    256,               /* GLOBALS               */
    1024,              /* TYPES                 */
    64,                /* IMPORTS               */
    256,               /* EXPORTS               */
    UINT64_C(1) << 20, /* DATA_BYTES            */
    256,               /* NESTING               */
    16,                /* PARAMS                */
    0,                 /* BULK_MEMORY (off)     */
    4096,              /* ELEM_SEGMENTS         */
};

static const uint64_t k_max[ZW_LIM_COUNT + 1] = {
    0,
    UINT64_C(64) << 20, /* MODULE_BYTES 64 MiB                               */
    ZW_PAGE_LIMIT_MAX,  /* MEMORY_PAGES (4 GiB)                              */
    UINT64_C(1) << 20,  /* TABLE_ELEMS                                       */
    100000,             /* FUNCTIONS                                         */
    UINT64_C(8) << 20,  /* BODY_BYTES                                        */
    50000,              /* LOCALS                                            */
    4096,               /* GLOBALS (the validator keeps a 4096-bit set)      */
    100000,             /* TYPES                                             */
    1024,               /* IMPORTS                                           */
    4096,               /* EXPORTS (uniqueness check is quadratic)           */
    UINT64_C(64) << 20, /* DATA_BYTES                                        */
    990,                /* NESTING (wasm3 d_m3ValCtrlDepth is 1000; the meter */
                        /* adds one level)                                   */
    16,                 /* PARAMS (the signature strings hold 16)            */
    1,                  /* BULK_MEMORY                                       */
    UINT64_C(1) << 20,  /* ELEM_SEGMENTS                                     */
};

void zw_policy_defaults(zw_policy *p) {
    memset(p, 0, sizeof *p);
    memcpy(p->lim, k_default, sizeof p->lim);
}

void zw_policy_copy(zw_policy *dst, const zw_policy *src) {
    memcpy(dst, src, sizeof *dst);
}

int32_t zw_policy_create(zw_policy **out_policy) {
    zw_policy *p;
    if (!out_policy) return ZXV_E_ARG;
    p = (zw_policy *) malloc(sizeof *p);
    if (!p) return ZXV_E_NOMEM;
    zw_policy_defaults(p);
    *out_policy = p;
    return ZXV_OK;
}

void zw_policy_destroy(zw_policy *policy) {
    free(policy);
}

int32_t zw_policy_set_limit(zw_policy *policy, int32_t which, uint64_t value) {
    if (!policy || which < 1 || which > ZW_LIM_COUNT) {
        zw_set_error("zw_policy_set_limit: unknown limit");
        return ZXV_E_ARG;
    }
    if (value > k_max[which]) {
        zw_set_errorf("zw_policy_set_limit: limit %d is at most %llu", (int) which,
                      (unsigned long long) k_max[which]);
        return ZXV_E_ARG;
    }
    policy->lim[which] = value;
    return ZXV_OK;
}

int32_t zw_policy_get_limit(const zw_policy *policy, int32_t which, uint64_t *out_value) {
    if (!policy || !out_value || which < 1 || which > ZW_LIM_COUNT) return ZXV_E_ARG;
    *out_value = policy->lim[which];
    return ZXV_OK;
}

static int sig_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 3u || n > ZW_SIG_MAX) return 0;
    if (s[0] != 'i' && s[0] != 'I' && s[0] != 'v') return 0;
    if (s[1] != '(' || s[n - 1u] != ')') return 0;
    for (size_t i = 2; i + 1u < n; i++)
        if (s[i] != 'i' && s[i] != 'I') return 0;
    return 1;
}

static int name_ok(const char *s) {
    size_t n;
    if (!s) return 0;
    n = strlen(s);
    return n >= 1u && n <= ZW_NAME_MAX && zw_utf8_ok((const uint8_t *) s, n);
}

int32_t zw_policy_allow_import(zw_policy *policy, const char *module, const char *name,
                               const char *signature) {
    if (!policy || !name_ok(module) || !name_ok(name) || !signature || !sig_ok(signature)) {
        zw_set_error("zw_policy_allow_import: bad module, name or signature");
        return ZXV_E_ARG;
    }
    for (uint32_t i = 0; i < policy->n_allow; i++) {
        if (strcmp(policy->allow[i].module, module) == 0 &&
            strcmp(policy->allow[i].name, name) == 0) {
            zw_set_errorf("zw_policy_allow_import: %s.%s already allowed", module, name);
            return ZXV_E_DUPLICATE;
        }
    }
    if (policy->n_allow >= ZW_ALLOW_MAX) {
        zw_set_error("zw_policy_allow_import: allow-list full");
        return ZXV_E_CAPACITY;
    }
    zw_allow *a = &policy->allow[policy->n_allow++];
    snprintf(a->module, sizeof a->module, "%s", module);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->sig, sizeof a->sig, "%s", signature);
    return ZXV_OK;
}
