/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi_zwasm.c — AIPI executor on Z-Wasm + Z-Contract. See aipi_zwasm.h. */
#include "aipi_zwasm.h"

#include <stdlib.h>
#include <string.h>

#include "zcontract.h"
#include "zcontract_wasm.h"
#include "zwasm.h"

struct aipi_zw {
    zw_host *host;
    zc_evlog *log;
    aipi_zw_stats_t st;
};

int aipi_zw_create(const zxv_cap *budget_cap, zxv_rmag *rmag, uint32_t budget, aipi_zw **out)
{
    aipi_zw *z;
    if (!budget_cap || !rmag || !out) return AIPI_ERR_ARG;
    *out = NULL;
    if (!(z = calloc(1, sizeof *z))) return AIPI_ERR_EXEC;
    if (zw_host_create(budget_cap, rmag, budget, &z->host) != ZXV_OK ||
        zc_evlog_create(1024, &z->log) != ZXV_OK) {
        aipi_zw_destroy(z);
        return AIPI_ERR_EXEC;
    }
    *out = z;
    return AIPI_OK;
}

void aipi_zw_destroy(aipi_zw *z)
{
    if (!z) return;
    zc_evlog_destroy(z->log);
    zw_host_destroy(z->host);
    free(z);
}

void aipi_zw_stats(const aipi_zw *z, aipi_zw_stats_t *out)
{
    if (z && out) *out = z->st;
}

int aipi_zw_pack(const int64_t *args, uint32_t n, uint8_t *out, uint32_t cap, uint32_t *len)
{
    if ((!args && n) || !out || !len) return AIPI_ERR_ARG;
    if (n > AIPI_ZW_MAX_ARGS) return AIPI_ERR_SPACE;
    if (cap < 8u * n) return AIPI_ERR_SPACE;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t b = 0; b < 8u; b++) out[8u * i + b] = (uint8_t) ((uint64_t) args[i] >> (8u * b));
    *len = 8u * n;
    return AIPI_OK;
}

int aipi_zw_unpack(const uint8_t *in, uint32_t len, int64_t *args, uint32_t *n)
{
    if ((!in && len) || !args || !n) return AIPI_ERR_ARG;
    if (len % 8u || len / 8u > AIPI_ZW_MAX_ARGS) return AIPI_ERR_MALFORMED;
    *n = len / 8u;
    for (uint32_t i = 0; i < *n; i++) {
        uint64_t v = 0;
        for (uint32_t b = 0; b < 8u; b++) v |= (uint64_t) in[8u * i + b] << (8u * b);
        args[i] = (int64_t) v;
    }
    return AIPI_OK;
}

/* A loaded function: the plugin, and the contract guard on its entry. */
typedef struct {
    zw_plugin *pl;
    zc_contract *ct;
    zc_wasm_guard *guard;
} loaded_t;

static void unload(loaded_t *l)
{
    zc_wasm_detach(l->guard); /* children first */
    zw_plugin_destroy(l->pl);
    zc_contract_destroy(l->ct);
    memset(l, 0, sizeof *l);
}

static int load(aipi_zw *z, const aipi_code_t *code, bool guard_entry, loaded_t *l)
{
    memset(l, 0, sizeof *l);
    if (zw_host_set_call_fuel(z->host, code->fn->fuel_max) != ZXV_OK) return AIPI_ERR_EXEC;
    if (zw_plugin_load(z->host, code->module, code->module_len, "aipi", &l->pl) != ZXV_OK) {
        z->st.refused_module++;
        return AIPI_ERR_EXEC;
    }
    if (code->contract) {
        if (zc_parse((const char *) code->contract, code->contract_len, &l->ct, NULL, NULL, NULL,
                     NULL, 0, NULL) != ZXV_OK) {
            z->st.refused_contract++;
            unload(l);
            return AIPI_ERR_EXEC;
        }
        if (guard_entry &&
            zc_wasm_attach(l->pl, code->fn->entry, l->ct, code->fn->entry, z->log, &l->guard) !=
                ZXV_OK) {
            z->st.refused_contract++;
            unload(l);
            return AIPI_ERR_EXEC;
        }
    }
    return AIPI_OK;
}

/* One export call returning one integer. */
static int call1(aipi_zw *z, loaded_t *l, const char *name, const int64_t *args, uint32_t n,
                 int64_t *res)
{
    int32_t has = 0, st;
    uint64_t fuel = 0;
    st = zw_plugin_call(l->pl, name, args, n, res, &has, &fuel);
    z->st.fuel_used += fuel;
    if (st == ZW_E_CHECK) {
        z->st.refused_contract++;
        return AIPI_ERR_EXEC;
    }
    if (st == ZW_E_TRAP || st == ZW_E_FUEL) {
        z->st.trapped++;
        return AIPI_ERR_EXEC;
    }
    if (st != ZXV_OK || !has) return AIPI_ERR_EXEC;
    return AIPI_OK;
}

static int zw_run(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                  uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    aipi_zw *z = (aipi_zw *) ctx;
    int64_t args[AIPI_ZW_MAX_ARGS], res = 0;
    uint32_t n;
    loaded_t l;
    int r;
    z->st.runs++;
    if ((r = aipi_zw_unpack(in, in_len, args, &n)) != AIPI_OK) return r;
    if ((r = load(z, code, true, &l)) != AIPI_OK) return r;
    r = call1(z, &l, code->fn->entry, args, n, &res);
    unload(&l);
    if (r != AIPI_OK) return r;
    return aipi_zw_pack(&res, 1, out, cap, out_len);
}

/* A claimed result outside the contract's result type is wrong whatever the
 * checker would say. 1 = inside (or no contract), 0 = outside. */
static int within_contract(aipi_zw *z, const loaded_t *l, const char *fn, int64_t result)
{
    zc_vals *v = NULL;
    int32_t reason = 0, st;
    uint32_t idx = 0;
    if (!l->ct) return 1;
    if (zc_vals_create(&v) != ZXV_OK) return AIPI_ERR_EXEC;
    if (zc_vals_push_int(v, result) != ZXV_OK) {
        zc_vals_destroy(v);
        return AIPI_ERR_EXEC;
    }
    st = zc_check(l->ct, fn, ZC_PART_RESULT, v, &reason, &idx, NULL, 0, NULL);
    zc_vals_destroy(v);
    if (st == ZXV_OK) return 1;
    if (st == ZC_E_VIOLATION) {
        z->st.refused_contract++;
        return 0;
    }
    return AIPI_ERR_EXEC;
}

static int zw_check(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                    const uint8_t *out, uint32_t out_len, const uint8_t *cert, uint32_t cert_len)
{
    aipi_zw *z = (aipi_zw *) ctx;
    int64_t args[AIPI_ZW_MAX_ARGS + 2], verdict = 0;
    uint32_t n, k, ri;
    bool want_cert = code->fn->verify == AIPI_VERIFY_CERT;
    loaded_t l;
    int r;
    z->st.checks++;
    if ((r = aipi_zw_unpack(in, in_len, args, &n)) != AIPI_OK) return 0;
    ri = n; /* args = inputs, result, [certificate] */
    if (aipi_zw_unpack(out, out_len, args + n, &k) != AIPI_OK || k != 1) return 0;
    n += 1;
    if (want_cert) {
        if (aipi_zw_unpack(cert, cert_len, args + n, &k) != AIPI_OK || k != 1) return 0;
        n += 1;
    }
    if (n > AIPI_ZW_MAX_ARGS) return AIPI_ERR_EXEC; /* the function cannot be checked */
    if ((r = load(z, code, false, &l)) != AIPI_OK) return r;
    r = within_contract(z, &l, code->fn->entry, args[ri]);
    if (r == 1) {
        /* a checker that traps or runs out of fuel has not accepted */
        r = call1(z, &l, code->fn->checker, args, n, &verdict) == AIPI_OK && verdict == 1;
    }
    unload(&l);
    return r;
}

static int zw_certify(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                      const uint8_t *out, uint32_t out_len, uint8_t *cert, uint32_t cap,
                      uint32_t *cert_len)
{
    aipi_zw *z = (aipi_zw *) ctx;
    int64_t args[AIPI_ZW_MAX_ARGS + 1], c = 0;
    uint32_t n, k;
    loaded_t l;
    int r;
    z->st.certs++;
    if ((r = aipi_zw_unpack(in, in_len, args, &n)) != AIPI_OK) return r;
    if (aipi_zw_unpack(out, out_len, args + n, &k) != AIPI_OK || k != 1) return AIPI_ERR_EXEC;
    n += 1;
    if (n > AIPI_ZW_MAX_ARGS) return AIPI_ERR_EXEC;
    if ((r = load(z, code, false, &l)) != AIPI_OK) return r;
    r = call1(z, &l, code->fn->certifier, args, n, &c);
    unload(&l);
    if (r != AIPI_OK) return r;
    return aipi_zw_pack(&c, 1, cert, cap, cert_len);
}

void aipi_zw_ops(aipi_zw *z, aipi_exec_ops_t *ops)
{
    if (!ops) return;
    ops->ctx = z;
    ops->run = zw_run;
    ops->check = zw_check;
    ops->certify = zw_certify;
}
