/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_wasm.c — z-contract on Z-Wasm exports, through the z-wasm call-check
 * hook (zw_plugin_set_check). Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"
#include "zcontract_wasm.h"

#define WASM_MAX_ARGS 8u

struct zc_wasm_guard {
    zw_plugin *plugin;
    char *export_name;
    const zc_contract *contract;
    const zc_fn *fn;
    zc_evlog *log;
    uint8_t digest[32];
    zc_vals vals; /* scratch, reused per call */
    uint64_t calls, violations;
    int log_failed;
    int has_last;
    int32_t last_part, last_reason;
    uint32_t last_index;
    uint64_t last_seq;
    char last_msg[320];
};

/* A truth or tag code: anything outside int32 becomes -1 (INVALID). */
static int32_t narrow(int64_t v) {
    return v >= INT32_MIN && v <= INT32_MAX ? (int32_t) v : -1;
}

static int32_t args_needed(const zc_fn *f, uint32_t *out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->nparams; i++) {
        int32_t k = f->params[i].type.kind;
        if (k == ZC_T_BYTES) return ZC_E_MAPPING;
        n += k == ZC_T_RAT ? 2u : 1u;
    }
    *out = n;
    return ZXV_OK;
}

/* Record a violation; returns non-zero (the hook's "refuse"). */
static int32_t violation(zc_wasm_guard *g, int32_t part, uint32_t index, int32_t reason,
                         const char *msg, int output) {
    uint8_t subject[32];
    uint8_t tag = 0;
    int32_t st;
    uint64_t seq = 0;
    g->violations++;
    g->has_last = 1;
    g->last_part = part;
    g->last_index = index;
    g->last_reason = reason;
    snprintf(g->last_msg, sizeof g->last_msg, "%s", msg);
    memset(subject, 0, sizeof subject);
    (void) zc_vals_digest(&g->vals, part, subject);
    /* input: false/neutral/potential; output: false/forbidden/manifest */
    (void) zxv_hex_pack(ZXV_HEX_BASE_FALSE, output ? ZXV_HEX_POL_FORBIDDEN : ZXV_HEX_POL_NEUTRAL,
                        output ? ZXV_HEX_PHASE_MANIFEST : ZXV_HEX_PHASE_POTENTIAL, &tag);
    st = g->log ? zc_evlog_append(g->log, ZC_ORIGIN_WASM, part, index, reason, tag, g->digest,
                                  subject, msg, &seq)
                : ZC_E_LOG;
    if (st != ZXV_OK) {
        g->log_failed = 1;
        seq = 0;
    }
    g->last_seq = seq;
    return 1;
}

static int32_t log_refused(zc_wasm_guard *g) {
    g->violations++;
    g->has_last = 1;
    g->last_part = 0;
    g->last_index = 0;
    g->last_reason = ZC_V_OK;
    g->last_seq = 0;
    snprintf(g->last_msg, sizeof g->last_msg,
             "the evidence log refused a record earlier: this guard refuses every call");
    return 1;
}

static int32_t check_cb(void *user, zw_plugin *plugin, int32_t stage, const char *export_name,
                        const int64_t *args, uint32_t nargs, int64_t result, int32_t has_result) {
    zc_wasm_guard *g = (zc_wasm_guard *) user;
    const zc_fn *f = g->fn;
    int32_t reason = ZC_V_OK, st;
    uint32_t index = 0;
    char msg[320];
    (void) export_name;
    (void) zc_vals_clear(&g->vals);

    if (stage == ZW_CHECK_BEFORE) {
        uint32_t need = 0, k = 0;
        g->calls++;
        if (g->log_failed) return log_refused(g);
        (void) args_needed(f, &need);
        if (nargs != need) {
            snprintf(msg, sizeof msg, "%s: %u Wasm arguments, the contract maps to %u", f->name,
                     (unsigned) nargs, (unsigned) need);
            for (uint32_t i = 0; i < nargs && i < ZC_MAX_VALUES; i++)
                (void) zc_vals_push_int(&g->vals, args[i]);
            return violation(g, ZC_PART_PARAMS, 0, ZC_V_ARITY, msg, 0);
        }
        for (uint32_t i = 0; i < f->nparams; i++) {
            switch (f->params[i].type.kind) {
            case ZC_T_RAT:
                (void) zc_vals_push_rat(&g->vals, args[k], args[k + 1u]);
                k += 2;
                break;
            case ZC_T_TRUTH:
                (void) zc_vals_push_truth(&g->vals, narrow(args[k]));
                k++;
                break;
            case ZC_T_TAG:
                (void) zc_vals_push_tag(&g->vals, narrow(args[k]));
                k++;
                break;
            default: (void) zc_vals_push_int(&g->vals, args[k]); k++; break;
            }
        }
        st = zc_check(g->contract, f->name, ZC_PART_PARAMS, &g->vals, &reason, &index, msg,
                      sizeof msg, NULL);
        if (st == ZC_E_VIOLATION) return violation(g, ZC_PART_PARAMS, index, reason, msg, 0);
        if (st != ZXV_OK) {
            snprintf(msg, sizeof msg, "%s: the check failed (%s)", f->name, zc_status_name(st));
            return violation(g, ZC_PART_PARAMS, 0, ZC_V_INVALID, msg, 0);
        }
        return 0;
    }

    /* ZW_CHECK_AFTER: the result, then the staged proposals */
    if (g->log_failed) return log_refused(g);
    if (has_result) {
        int32_t rk = f->has_result ? f->result.kind : ZC_T_INT;
        if (rk == ZC_T_TRUTH)
            (void) zc_vals_push_truth(&g->vals, narrow(result));
        else if (rk == ZC_T_TAG)
            (void) zc_vals_push_tag(&g->vals, narrow(result));
        else
            (void) zc_vals_push_int(&g->vals, result);
    }
    st = zc_check(g->contract, f->name, ZC_PART_RESULT, &g->vals, &reason, &index, msg,
                  sizeof msg, NULL);
    if (st != ZXV_OK) {
        if (st != ZC_E_VIOLATION) {
            snprintf(msg, sizeof msg, "%s: the check failed (%s)", f->name, zc_status_name(st));
            reason = ZC_V_INVALID;
        }
        return violation(g, ZC_PART_RESULT, index, reason, msg, 1);
    }
    (void) zc_vals_clear(&g->vals);
    uint32_t n = 0;
    if (zw_plugin_staged_count(plugin, &n) != ZXV_OK) {
        snprintf(msg, sizeof msg, "%s: the staged proposals cannot be read", f->name);
        return violation(g, ZC_PART_PROPOSALS, 0, ZC_V_INVALID, msg, 1);
    }
    for (uint32_t i = 0; i < n; i++) {
        int64_t num = 0, den = 0;
        uint8_t label[80];
        size_t ll = 0;
        if (zw_plugin_staged_get(plugin, i, NULL, &num, &den, label, sizeof label, &ll) != ZXV_OK) {
            snprintf(msg, sizeof msg, "%s: staged proposal %u cannot be read", f->name, (unsigned) i);
            return violation(g, ZC_PART_PROPOSALS, i, ZC_V_INVALID, msg, 1);
        }
        if (i >= ZC_MAX_VALUES) break; /* COUNT is decided from n below */
        (void) zc_vals_push_rat(&g->vals, num, den);
    }
    if (n > ZC_MAX_VALUES) {
        snprintf(msg, sizeof msg, "%s: %u proposals, more than any contract allows", f->name,
                 (unsigned) n);
        return violation(g, ZC_PART_PROPOSALS, ZC_MAX_VALUES, ZC_V_COUNT, msg, 1);
    }
    st = zc_check(g->contract, f->name, ZC_PART_PROPOSALS, &g->vals, &reason, &index, msg,
                  sizeof msg, NULL);
    if (st != ZXV_OK) {
        if (st != ZC_E_VIOLATION) {
            snprintf(msg, sizeof msg, "%s: the check failed (%s)", f->name, zc_status_name(st));
            reason = ZC_V_INVALID;
        }
        return violation(g, ZC_PART_PROPOSALS, index, reason, msg, 1);
    }
    return 0;
}

int32_t zc_wasm_attach(zw_plugin *plugin, const char *export_name, const zc_contract *contract,
                       const char *fn, zc_evlog *log, zc_wasm_guard **out_guard) {
    const zc_fn *f;
    zc_wasm_guard *g;
    uint32_t need = 0;
    int32_t st;
    size_t n;
    if (!plugin || !export_name || !contract || !fn || !log || !out_guard) {
        zc_set_error("zc_wasm_attach: NULL argument");
        return ZXV_E_ARG;
    }
    f = zc_find_fn(contract, fn);
    if (!f) {
        zc_set_errorf("the contract declares no function '%.64s'", fn);
        return ZXV_E_NOT_FOUND;
    }
    if (args_needed(f, &need) != ZXV_OK) {
        zc_set_errorf("%s: a bytes parameter cannot be passed to a Wasm export", f->name);
        return ZC_E_MAPPING;
    }
    if (need > WASM_MAX_ARGS) {
        zc_set_errorf("%s: maps to %u Wasm arguments; Z-Wasm calls take at most %u", f->name,
                      (unsigned) need, WASM_MAX_ARGS);
        return ZC_E_MAPPING;
    }
    if (f->has_result && (f->result.kind == ZC_T_RAT || f->result.kind == ZC_T_BYTES)) {
        zc_set_errorf("%s: a Wasm export returns one i32/i64; return a %s through "
                      "'proposes' instead", f->name, f->result.kind == ZC_T_RAT ? "rational" : "byte string");
        return ZC_E_MAPPING;
    }
    g = (zc_wasm_guard *) calloc(1, sizeof *g);
    if (!g) return ZXV_E_NOMEM;
    n = strlen(export_name);
    g->export_name = (char *) malloc(n + 1u);
    if (!g->export_name) {
        free(g);
        return ZXV_E_NOMEM;
    }
    memcpy(g->export_name, export_name, n + 1u);
    g->plugin = plugin;
    g->contract = contract;
    g->fn = f;
    g->log = log;
    st = zc_contract_digest(contract, g->digest);
    if (st == ZXV_OK) st = zw_plugin_set_check(plugin, export_name, check_cb, g);
    if (st != ZXV_OK) {
        if (st == ZW_E_EXPORT) zc_set_errorf("the plugin exports no function '%.64s'", export_name);
        else if (st == ZXV_E_CAPACITY) zc_set_error("the plugin has no free check slot");
        free(g->export_name);
        free(g);
        return st;
    }
    *out_guard = g;
    return ZXV_OK;
}

void zc_wasm_detach(zc_wasm_guard *guard) {
    if (!guard) return;
    (void) zw_plugin_set_check(guard->plugin, guard->export_name, NULL, NULL);
    (void) zc_vals_clear(&guard->vals);
    free(guard->export_name);
    free(guard);
}

int32_t zc_wasm_args(const zc_vals *vals, int64_t *args, uint32_t cap, uint32_t *out_n) {
    uint32_t n = 0;
    if (!vals || !out_n) return ZXV_E_ARG;
    for (uint32_t i = 0; i < vals->n; i++) {
        if (vals->v[i].kind == ZC_T_BYTES) {
            zc_set_error("zc_wasm_args: a bytes value cannot be a Wasm argument");
            return ZC_E_MAPPING;
        }
        n += vals->v[i].kind == ZC_T_RAT ? 2u : 1u;
    }
    *out_n = n;
    if (!args || cap < n) return ZXV_E_BUFFER;
    n = 0;
    for (uint32_t i = 0; i < vals->n; i++) {
        args[n++] = vals->v[i].num;
        if (vals->v[i].kind == ZC_T_RAT) args[n++] = vals->v[i].den;
    }
    return ZXV_OK;
}

int32_t zc_wasm_last_violation(const zc_wasm_guard *guard, int32_t *out_part,
                               uint32_t *out_index, int32_t *out_reason, uint64_t *out_seq,
                               char *msg, size_t msg_cap, size_t *out_msg_len) {
    size_t n;
    if (!guard) return ZXV_E_ARG;
    if (!guard->has_last) return ZXV_E_NOT_FOUND;
    n = strlen(guard->last_msg);
    if (out_msg_len) *out_msg_len = n;
    if (msg && msg_cap <= n) return ZXV_E_BUFFER;
    if (msg) memcpy(msg, guard->last_msg, n + 1u);
    if (out_part) *out_part = guard->last_part;
    if (out_index) *out_index = guard->last_index;
    if (out_reason) *out_reason = guard->last_reason;
    if (out_seq) *out_seq = guard->last_seq;
    return ZXV_OK;
}

int32_t zc_wasm_stats(const zc_wasm_guard *guard, uint64_t *out_calls, uint64_t *out_violations,
                      int32_t *out_log_failed) {
    if (!guard) return ZXV_E_ARG;
    if (out_calls) *out_calls = guard->calls;
    if (out_violations) *out_violations = guard->violations;
    if (out_log_failed) *out_log_failed = guard->log_failed;
    return ZXV_OK;
}
