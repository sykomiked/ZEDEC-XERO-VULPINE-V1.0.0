/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw_host.c — the Z-Wasm host runtime: wasm3 instances, fuel charged to a
 * libzxv rmag budget, the granted host functions, and the S0 proposal
 * queue (Tri-Isolate starter).
 *
 * This file links wasm3, whose public header declares f32/f64 members in
 * a union; no float value is created or used here. wasm3 itself is built
 * with d_m3HasFloat=0, and the validator refuses every float instruction
 * before a module reaches it. */
#include <stdlib.h>
#include <string.h>

#include "wasm3.h"
#include "zw_wasm.h"

#define ZW_STAGE_MAX   64u   /* proposals one call may stage          */
#define ZW_PENDING_MAX 1024u /* pending (S0) proposals in the queue   */
#define ZW_KEEP_MAX    4096u /* proposals kept, decided ones evicted  */
#define ZW_LABEL_MAX   64u
#define ZW_MAX_ARGS    8u

typedef struct {
    uint64_t id;
    int32_t state; /* ZXV_LP_NEITHER (S0) / TRUE / FALSE */
    uint32_t kind;
    int64_t num, den;
    char plugin[32];
    uint8_t label[ZW_LABEL_MAX];
    uint32_t label_len;
} zw_prop;

struct zw_host {
    const zxv_cap *cap;
    zxv_rmag *rmag;
    uint32_t budget;
    zw_policy policy;
    uint32_t grants;
    uint64_t call_fuel;
    uint64_t accept_caps;
    uint32_t stack_bytes;
    IM3Environment env;
    zw_prop *q;
    size_t qn, qcap;
    uint32_t pending;
    uint64_t next_id;
};

struct zw_plugin {
    zw_host *host;
    IM3Runtime rt;
    IM3Module mod;
    IM3Global fuel;
    uint8_t *code; /* the metered module; wasm3 keeps pointers into it */
    size_t code_len;
    int32_t state;
    char name[32];
    zw_prop staged[ZW_STAGE_MAX];
    uint32_t nstaged;
    /* call checks (kit API 0.2): one per export */
    struct {
        char *export_name;
        zw_check_fn fn;
        void *user;
    } checks[ZW_CHECKS_MAX];
    int in_check; /* 0, or ZW_CHECK_BEFORE + 1 / ZW_CHECK_AFTER + 1 */
};

/* Returned by a host function that finds the fuel exhausted. */
static const char zw_fuel_trap[] = "[trap] Z-Wasm fuel exhausted";

/* ===== Host functions ===== */

typedef struct {
    const char *name;
    const char *sig;
    uint32_t grant;
    M3RawCall fn;
} zw_hostfn;

/* Charge ZW_HOST_CALL_FUEL; 0 if the plugin may continue. */
static int charge_host_call(zw_plugin *pl) {
    M3TaggedValue v;
    memset(&v, 0, sizeof v);
    if (m3_GetGlobal(pl->fuel, &v) != m3Err_none) return -1;
    int64_t left = (int64_t) v.value.i64;
    left -= ZW_HOST_CALL_FUEL;
    v.type = c_m3Type_i64;
    v.value.i64 = (uint64_t) left;
    if (m3_SetGlobal(pl->fuel, &v) != m3Err_none) return -1;
    return left < 0 ? -1 : 0;
}

/* Bounds-checked little-endian stores into linear memory. */
static int mem_ok(IM3Runtime runtime, uint32_t off, uint32_t len) {
    uint32_t size = 0;
    uint8_t *m = m3_GetMemory(runtime, &size, 0);
    return m && (uint64_t) off + len <= size;
}

static void put_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put_le32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

#define HOST_PROLOGUE                                                                          \
    zw_plugin *pl = (zw_plugin *) _ctx->userdata;                                              \
    (void) runtime;                                                                            \
    (void) _mem;                                                                               \
    if (charge_host_call(pl) != 0) m3ApiTrap(zw_fuel_trap)

typedef int32_t (*rat2_fn)(int64_t, int64_t, int64_t, int64_t, int64_t *, int64_t *);

static const void *rat2(IM3Runtime runtime, uint64_t *_sp, void *_mem, rat2_fn f) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, an);
    m3ApiGetArg(int64_t, ad);
    m3ApiGetArg(int64_t, bn);
    m3ApiGetArg(int64_t, bd);
    m3ApiGetArg(uint32_t, out);
    int64_t n, d;
    (void) _mem;
    if (!mem_ok(runtime, out, 16)) m3ApiReturn(ZXV_E_ARG);
    int32_t st = f(an, ad, bn, bd, &n, &d);
    if (st == ZXV_OK) {
        uint8_t *m = m3_GetMemory(runtime, NULL, 0);
        put_le64(m + out, (uint64_t) n);
        put_le64(m + out + 8, (uint64_t) d);
    }
    m3ApiReturn(st);
}

static m3ApiRawFunction(h_rat_add) {
    HOST_PROLOGUE;
    return rat2(runtime, _sp, _mem, zxv_rat_add);
}
static m3ApiRawFunction(h_rat_sub) {
    HOST_PROLOGUE;
    return rat2(runtime, _sp, _mem, zxv_rat_sub);
}
static m3ApiRawFunction(h_rat_mul) {
    HOST_PROLOGUE;
    return rat2(runtime, _sp, _mem, zxv_rat_mul);
}
static m3ApiRawFunction(h_rat_div) {
    HOST_PROLOGUE;
    return rat2(runtime, _sp, _mem, zxv_rat_div);
}

static m3ApiRawFunction(h_rat_cmp) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, an);
    m3ApiGetArg(int64_t, ad);
    m3ApiGetArg(int64_t, bn);
    m3ApiGetArg(int64_t, bd);
    m3ApiGetArg(uint32_t, out);
    int32_t c;
    if (!mem_ok(runtime, out, 4)) m3ApiReturn(ZXV_E_ARG);
    int32_t st = zxv_rat_cmp(an, ad, bn, bd, &c);
    if (st == ZXV_OK) put_le32(m3_GetMemory(runtime, NULL, 0) + out, (uint32_t) c);
    m3ApiReturn(st);
}

static m3ApiRawFunction(h_rat_normalize) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, num);
    m3ApiGetArg(int64_t, den);
    m3ApiGetArg(uint32_t, out);
    int64_t n, d;
    if (!mem_ok(runtime, out, 16)) m3ApiReturn(ZXV_E_ARG);
    int32_t st = zxv_rat_normalize(num, den, &n, &d);
    if (st == ZXV_OK) {
        uint8_t *m = m3_GetMemory(runtime, NULL, 0);
        put_le64(m + out, (uint64_t) n);
        put_le64(m + out + 8, (uint64_t) d);
    }
    m3ApiReturn(st);
}

static m3ApiRawFunction(h_lp_not) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    int32_t r;
    int32_t st = zxv_lp_not(a, &r);
    m3ApiReturn(st == ZXV_OK ? r : st);
}

static m3ApiRawFunction(h_lp_and) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    int32_t r;
    int32_t st = zxv_lp_and(a, b, &r);
    m3ApiReturn(st == ZXV_OK ? r : st);
}

static m3ApiRawFunction(h_lp_or) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    int32_t r;
    int32_t st = zxv_lp_or(a, b, &r);
    m3ApiReturn(st == ZXV_OK ? r : st);
}

static m3ApiRawFunction(h_lp_from_evidence) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, pos);
    m3ApiGetArg(uint32_t, neg);
    int32_t r;
    int32_t st = zxv_lp_from_evidence(pos, neg, &r);
    m3ApiReturn(st == ZXV_OK ? r : st);
}

/* zxv.propose(kind i32, num i64, den i64, label_ptr i32, label_len i32) -> i32
 * Stages an exact rational for the host's S0 queue. 0 on success, or a
 * negative ZXV status (the plugin decides what to do with it). */
static m3ApiRawFunction(h_propose) {
    HOST_PROLOGUE;
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, kind);
    m3ApiGetArg(int64_t, num);
    m3ApiGetArg(int64_t, den);
    m3ApiGetArg(uint32_t, lptr);
    m3ApiGetArg(uint32_t, llen);
    int64_t n, d;
    zw_host *h = pl->host;
    (void) _mem;
    if (llen > ZW_LABEL_MAX) m3ApiReturn(ZXV_E_TOO_LONG);
    if (llen && !mem_ok(runtime, lptr, llen)) m3ApiReturn(ZXV_E_ARG);
    int32_t st = zxv_rat_normalize(num, den, &n, &d);
    if (st != ZXV_OK) m3ApiReturn(st);
    if (pl->nstaged >= ZW_STAGE_MAX || h->pending + pl->nstaged >= ZW_PENDING_MAX)
        m3ApiReturn(ZXV_E_CAPACITY);
    zw_prop *p = &pl->staged[pl->nstaged++];
    memset(p, 0, sizeof *p);
    p->state = ZXV_LP_NEITHER;
    p->kind = kind;
    p->num = n;
    p->den = d;
    memcpy(p->plugin, pl->name, sizeof p->plugin);
    if (llen) memcpy(p->label, m3_GetMemory(runtime, NULL, 0) + lptr, llen);
    p->label_len = llen;
    m3ApiReturn(ZXV_OK);
}

static const zw_hostfn k_hostfns[] = {
    {"rat_add", "i(IIIIi)", ZW_GRANT_RATIONAL, h_rat_add},
    {"rat_sub", "i(IIIIi)", ZW_GRANT_RATIONAL, h_rat_sub},
    {"rat_mul", "i(IIIIi)", ZW_GRANT_RATIONAL, h_rat_mul},
    {"rat_div", "i(IIIIi)", ZW_GRANT_RATIONAL, h_rat_div},
    {"rat_cmp", "i(IIIIi)", ZW_GRANT_RATIONAL, h_rat_cmp},
    {"rat_normalize", "i(IIi)", ZW_GRANT_RATIONAL, h_rat_normalize},
    {"lp_not", "i(i)", ZW_GRANT_LOGIC, h_lp_not},
    {"lp_and", "i(ii)", ZW_GRANT_LOGIC, h_lp_and},
    {"lp_or", "i(ii)", ZW_GRANT_LOGIC, h_lp_or},
    {"lp_from_evidence", "i(ii)", ZW_GRANT_LOGIC, h_lp_from_evidence},
    {"propose", "i(iIIii)", ZW_GRANT_PROPOSE, h_propose},
};
#define N_HOSTFNS (sizeof k_hostfns / sizeof k_hostfns[0])

/* ===== Host ===== */

int32_t zw_host_create(const zxv_cap *budget_cap, zxv_rmag *rmag, uint32_t budget,
                       zw_host **out_host) {
    zw_host *h;
    int32_t st;
    if (!budget_cap || !rmag || !out_host) {
        zw_set_error("zw_host_create: NULL argument");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(budget_cap, ZXV_CAP_BUDGET);
    if (st != ZXV_OK) {
        zw_set_error("zw_host_create: the budget capability lacks ZXV_CAP_BUDGET");
        return st;
    }
    st = zxv_rmag_status(rmag, budget, NULL, NULL, NULL, NULL, NULL);
    if (st != ZXV_OK) {
        zw_set_error("zw_host_create: unknown rmag budget");
        return st;
    }
    h = (zw_host *) calloc(1, sizeof *h);
    if (!h) return ZXV_E_NOMEM;
    h->env = m3_NewEnvironment();
    if (!h->env) {
        free(h);
        return ZXV_E_NOMEM;
    }
    h->cap = budget_cap;
    h->rmag = rmag;
    h->budget = budget;
    zw_policy_defaults(&h->policy);
    h->call_fuel = UINT64_C(10000000);
    h->accept_caps = ZXV_CAP_LEDGER_POST;
    h->stack_bytes = 64u * 1024u;
    h->next_id = 1;
    *out_host = h;
    return ZXV_OK;
}

void zw_host_destroy(zw_host *host) {
    if (!host) return;
    m3_FreeEnvironment(host->env);
    free(host->q);
    free(host);
}

int32_t zw_host_grant(zw_host *host, uint32_t grants) {
    if (!host || (grants & ~(ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE))) {
        zw_set_error("zw_host_grant: unknown grant bits");
        return ZXV_E_ARG;
    }
    host->grants |= grants;
    return ZXV_OK;
}

int32_t zw_host_policy(zw_host *host, zw_policy **out_policy) {
    if (!host || !out_policy) return ZXV_E_ARG;
    *out_policy = &host->policy;
    return ZXV_OK;
}

int32_t zw_host_set_call_fuel(zw_host *host, uint64_t max_fuel) {
    if (!host || max_fuel == 0 || max_fuel > (uint64_t) INT64_MAX) return ZXV_E_ARG;
    host->call_fuel = max_fuel;
    return ZXV_OK;
}

int32_t zw_host_set_accept_caps(zw_host *host, uint64_t needed) {
    if (!host || needed == 0 || (needed & ~ZXV_CAP_ALL)) return ZXV_E_ARG;
    host->accept_caps = needed;
    return ZXV_OK;
}

int32_t zw_host_set_stack_bytes(zw_host *host, uint32_t bytes) {
    if (!host || bytes < 4096u || bytes > 1024u * 1024u) return ZXV_E_ARG;
    host->stack_bytes = bytes;
    return ZXV_OK;
}

/* ===== Plugins ===== */

/* Is `name` an exported FUNCTION of the (metered) module? */
static int exported_func(const uint8_t *w, size_t len, const char *name) {
    size_t pos = 8, nlen = strlen(name);
    while (pos < len) {
        uint8_t id = w[pos++];
        uint32_t size, count;
        if (zw_rd_u32(w, len, &pos, &size) != 0 || size > len - pos) return 0;
        if (id == SEC_EXPORT) {
            size_t q = pos, end = pos + size;
            if (zw_rd_u32(w, end, &q, &count) != 0) return 0;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t l, idx;
                uint8_t kind;
                if (zw_rd_u32(w, end, &q, &l) != 0 || l > end - q) return 0;
                size_t at = q;
                q += l;
                if (zw_rd_u8(w, end, &q, &kind) != 0 || zw_rd_u32(w, end, &q, &idx) != 0) return 0;
                if (kind == 0 && l == nlen && memcmp(w + at, name, nlen) == 0) return 1;
            }
            return 0;
        }
        pos += size;
    }
    return 0;
}

int32_t zw_plugin_load(zw_host *host, const uint8_t *wasm, size_t len, const char *name,
                       zw_plugin **out_plugin) {
    zw_policy *eff = NULL, *relaxed = NULL;
    zw_plugin *pl = NULL;
    uint8_t *metered = NULL;
    size_t mlen = 0;
    IM3Module mod = NULL;
    M3Result r;
    int32_t st;

    if (!host || !wasm || !out_plugin || (name && strlen(name) > 31u)) {
        zw_set_error("zw_plugin_load: NULL argument or name longer than 31 bytes");
        return ZXV_E_ARG;
    }
    eff = (zw_policy *) malloc(sizeof *eff);
    relaxed = (zw_policy *) malloc(sizeof *relaxed);
    pl = (zw_plugin *) calloc(1, sizeof *pl);
    if (!eff || !relaxed || !pl) {
        st = ZXV_E_NOMEM;
        goto fail;
    }
    /* The effective allow-list is exactly the granted host functions. */
    zw_policy_copy(eff, &host->policy);
    eff->n_allow = 0;
    for (size_t i = 0; i < N_HOSTFNS; i++)
        if (host->grants & k_hostfns[i].grant)
            (void) zw_policy_allow_import(eff, "zxv", k_hostfns[i].name, k_hostfns[i].sig);

    st = zw_validate(eff, wasm, len, NULL, NULL, NULL, NULL, NULL, 0, NULL);
    if (st != ZW_OK) goto fail; /* zw_last_error holds the validator message */

    st = zw_meter(wasm, len, &metered, &mlen);
    if (st != ZW_OK) {
        zw_set_error("zw_plugin_load: metering failed");
        goto fail;
    }
    /* Defence in depth: the metered module must pass the same profile
     * (sizes relaxed for the inserted code, the fuel export allowed). */
    zw_policy_copy(relaxed, eff);
    for (int k = 1; k <= ZW_LIM_COUNT; k++)
        if (k != ZW_LIM_MEMORY_PAGES && k != ZW_LIM_TABLE_ELEMS && k != ZW_LIM_BULK_MEMORY)
            relaxed->lim[k] = UINT64_MAX;
    relaxed->lim[ZW_LIM_NESTING] = 1000u;
    st = zw_validate_ex(relaxed, metered, mlen, 1, NULL, NULL, NULL, NULL, NULL, 0, NULL);
    if (st != ZW_OK) {
        zw_set_error("zw_plugin_load: internal error: metered module failed validation");
        st = ZXV_E_INTERNAL;
        goto fail;
    }

    pl->host = host;
    pl->code = metered;
    pl->code_len = mlen;
    metered = NULL;
    pl->state = ZW_PLUGIN_READY;
    if (name) memcpy(pl->name, name, strlen(name));

    r = m3_ParseModule(host->env, &mod, pl->code, (uint32_t) pl->code_len);
    if (r) {
        zw_set_errorf("zw_plugin_load: wasm3 parse: %s", r);
        st = ZW_E_LINK;
        goto fail;
    }
    pl->rt = m3_NewRuntime(host->env, host->stack_bytes, pl);
    if (!pl->rt) {
        m3_FreeModule(mod);
        st = ZXV_E_NOMEM;
        goto fail;
    }
    r = m3_LoadModule(pl->rt, mod);
    if (r) {
        m3_FreeModule(mod);
        zw_set_errorf("zw_plugin_load: wasm3 load: %s", r);
        st = ZW_E_LINK;
        goto fail;
    }
    pl->mod = mod;
    for (size_t i = 0; i < N_HOSTFNS; i++) {
        if (!(host->grants & k_hostfns[i].grant)) continue;
        r = m3_LinkRawFunctionEx(mod, "zxv", k_hostfns[i].name, k_hostfns[i].sig,
                                 k_hostfns[i].fn, pl);
        if (r && r != m3Err_functionLookupFailed) {
            zw_set_errorf("zw_plugin_load: wasm3 link zxv.%s: %s", k_hostfns[i].name, r);
            st = ZW_E_LINK;
            goto fail;
        }
    }
    r = m3_CompileModule(mod);
    if (r) {
        zw_set_errorf("zw_plugin_load: wasm3 compile: %s", r);
        st = ZW_E_LINK;
        goto fail;
    }
    pl->fuel = m3_FindGlobal(mod, ZW_FUEL_EXPORT);
    if (!pl->fuel || m3_GetGlobalType(pl->fuel) != c_m3Type_i64) {
        zw_set_error("zw_plugin_load: internal error: fuel global missing");
        st = ZXV_E_INTERNAL;
        goto fail;
    }
    free(eff);
    free(relaxed);
    *out_plugin = pl;
    return ZXV_OK;
fail:
    if (pl) {
        if (pl->rt) m3_FreeRuntime(pl->rt);
        free(pl->code);
        free(pl);
    }
    free(metered);
    free(eff);
    free(relaxed);
    return st;
}

void zw_plugin_destroy(zw_plugin *plugin) {
    if (!plugin) return;
    for (int i = 0; i < ZW_CHECKS_MAX; i++) free(plugin->checks[i].export_name);
    if (plugin->rt) m3_FreeRuntime(plugin->rt);
    free(plugin->code);
    free(plugin);
}

int32_t zw_plugin_state(const zw_plugin *plugin, int32_t *out_state) {
    if (!plugin || !out_state) return ZXV_E_ARG;
    *out_state = plugin->state;
    return ZXV_OK;
}

static int32_t queue_push(zw_host *h, const zw_prop *p) {
    if (h->qn == h->qcap) {
        /* evict the oldest decided proposals once the kept set is full */
        if (h->qn >= ZW_KEEP_MAX) {
            size_t w = 0;
            size_t drop = h->qn - ZW_KEEP_MAX / 2u;
            for (size_t i = 0; i < h->qn; i++) {
                if (drop && h->q[i].state != ZXV_LP_NEITHER) {
                    drop--;
                    continue;
                }
                h->q[w++] = h->q[i];
            }
            h->qn = w;
        }
        if (h->qn == h->qcap) {
            size_t nc = h->qcap ? h->qcap * 2u : 64u;
            zw_prop *nq = (zw_prop *) realloc(h->q, nc * sizeof *nq);
            if (!nq) return ZXV_E_NOMEM;
            h->q = nq;
            h->qcap = nc;
        }
    }
    h->q[h->qn] = *p;
    h->q[h->qn].id = h->next_id++;
    h->qn++;
    h->pending++;
    return ZXV_OK;
}

/* ===== Call checks (kit API 0.2) ===== */

static int find_check(const zw_plugin *pl, const char *export_name) {
    for (int i = 0; i < ZW_CHECKS_MAX; i++)
        if (pl->checks[i].export_name && strcmp(pl->checks[i].export_name, export_name) == 0)
            return i;
    return -1;
}

int32_t zw_plugin_set_check(zw_plugin *plugin, const char *export_name, zw_check_fn fn,
                            void *user) {
    int i;
    if (!plugin || !export_name) {
        zw_set_error("zw_plugin_set_check: NULL argument");
        return ZXV_E_ARG;
    }
    if (plugin->in_check) {
        zw_set_error("zw_plugin_set_check: called from inside a check");
        return ZXV_E_STATE;
    }
    if (!exported_func(plugin->code, plugin->code_len, export_name)) {
        zw_set_errorf("plugin '%s' exports no function '%s'", plugin->name, export_name);
        return ZW_E_EXPORT;
    }
    i = find_check(plugin, export_name);
    if (!fn) {
        if (i >= 0) {
            free(plugin->checks[i].export_name);
            memset(&plugin->checks[i], 0, sizeof plugin->checks[i]);
        }
        return ZXV_OK;
    }
    if (i < 0) {
        size_t n = strlen(export_name);
        for (i = 0; i < ZW_CHECKS_MAX && plugin->checks[i].export_name; i++) {
        }
        if (i == ZW_CHECKS_MAX) {
            zw_set_error("zw_plugin_set_check: every check slot is in use");
            return ZXV_E_CAPACITY;
        }
        plugin->checks[i].export_name = (char *) malloc(n + 1);
        if (!plugin->checks[i].export_name) return ZXV_E_NOMEM;
        memcpy(plugin->checks[i].export_name, export_name, n + 1);
    }
    plugin->checks[i].fn = fn;
    plugin->checks[i].user = user;
    return ZXV_OK;
}

int32_t zw_plugin_staged_count(const zw_plugin *plugin, uint32_t *out_count) {
    if (!plugin || !out_count) return ZXV_E_ARG;
    if (plugin->in_check != ZW_CHECK_AFTER + 1) {
        zw_set_error("zw_plugin_staged_count: only inside a ZW_CHECK_AFTER check");
        return ZXV_E_STATE;
    }
    *out_count = plugin->nstaged;
    return ZXV_OK;
}

int32_t zw_plugin_staged_get(const zw_plugin *plugin, uint32_t index, uint32_t *out_kind,
                             int64_t *out_num, int64_t *out_den, uint8_t *label,
                             size_t label_cap, size_t *out_label_len) {
    const zw_prop *p;
    if (!plugin) return ZXV_E_ARG;
    if (plugin->in_check != ZW_CHECK_AFTER + 1) {
        zw_set_error("zw_plugin_staged_get: only inside a ZW_CHECK_AFTER check");
        return ZXV_E_STATE;
    }
    if (index >= plugin->nstaged) return ZXV_E_NOT_FOUND;
    p = &plugin->staged[index];
    if (out_label_len) *out_label_len = p->label_len;
    if (p->label_len && (!label || label_cap <= p->label_len)) return ZXV_E_BUFFER;
    if (out_kind) *out_kind = p->kind;
    if (out_num) *out_num = p->num;
    if (out_den) *out_den = p->den;
    if (label && label_cap) {
        if (p->label_len) memcpy(label, p->label, p->label_len);
        label[p->label_len] = 0;
    }
    return ZXV_OK;
}

static int32_t run_check(zw_plugin *pl, int ci, int32_t stage, const char *export_name,
                         const int64_t *args, uint32_t nargs, int64_t result, int32_t has_result) {
    int32_t rc;
    pl->in_check = stage + 1;
    rc = pl->checks[ci].fn(pl->checks[ci].user, pl, stage, export_name, args, nargs, result,
                           has_result);
    pl->in_check = 0;
    return rc;
}

int32_t zw_plugin_call(zw_plugin *plugin, const char *export_name, const int64_t *args,
                       uint32_t nargs, int64_t *out_result, int32_t *out_has_result,
                       uint64_t *out_fuel_used) {
    zw_host *h;
    IM3Function f = NULL;
    M3Result r;
    int64_t rn = 0, rd = 1;
    uint64_t fuel, used;
    uint32_t a32[ZW_MAX_ARGS];
    uint64_t a64[ZW_MAX_ARGS];
    const void *ptrs[ZW_MAX_ARGS];
    int32_t st;

    if (!plugin || !export_name || nargs > ZW_MAX_ARGS || (nargs && !args)) {
        zw_set_error("zw_plugin_call: bad argument");
        return ZXV_E_ARG;
    }
    h = plugin->host;
    if (plugin->in_check) {
        zw_set_error("zw_plugin_call: called from inside a check");
        return ZXV_E_STATE;
    }
    if (plugin->state == ZW_PLUGIN_DEAD) {
        zw_set_errorf("plugin '%s' is dead (it trapped or ran out of fuel); destroy it",
                      plugin->name);
        return ZW_E_DEAD;
    }
    if (!exported_func(plugin->code, plugin->code_len, export_name)) {
        zw_set_errorf("plugin '%s' exports no function '%s'", plugin->name, export_name);
        return ZW_E_EXPORT;
    }
    r = m3_FindFunction(&f, plugin->rt, export_name);
    if (r || !f) {
        zw_set_errorf("plugin '%s': %s", plugin->name, r ? r : "function not found");
        return ZW_E_EXPORT;
    }
    if (m3_GetArgCount(f) != nargs || m3_GetRetCount(f) > 1u) {
        zw_set_errorf("plugin '%s': '%s' takes %u arguments and returns %u values; called with %u",
                      plugin->name, export_name, (unsigned) m3_GetArgCount(f),
                      (unsigned) m3_GetRetCount(f), (unsigned) nargs);
        return ZW_E_EXPORT;
    }
    for (uint32_t i = 0; i < nargs; i++) {
        M3ValueType t = m3_GetArgType(f, i);
        if (t == c_m3Type_i32) {
            if (args[i] < INT32_MIN || args[i] > (int64_t) UINT32_MAX) {
                zw_set_errorf("argument %u does not fit an i32", (unsigned) i);
                return ZXV_E_RANGE;
            }
            a32[i] = (uint32_t) (uint64_t) args[i];
            ptrs[i] = &a32[i];
        } else if (t == c_m3Type_i64) {
            a64[i] = (uint64_t) args[i];
            ptrs[i] = &a64[i];
        } else {
            zw_set_errorf("'%s' parameter %u is not i32/i64", export_name, (unsigned) i);
            return ZW_E_EXPORT;
        }
    }
    M3ValueType rt = m3_GetRetCount(f) ? m3_GetRetType(f, 0) : c_m3Type_none;
    if (rt != c_m3Type_none && rt != c_m3Type_i32 && rt != c_m3Type_i64) {
        zw_set_errorf("'%s' result is not i32/i64", export_name);
        return ZW_E_EXPORT;
    }
    int ci = find_check(plugin, export_name);
    if (ci >= 0 && run_check(plugin, ci, ZW_CHECK_BEFORE, export_name, args, nargs, 0, 0) != 0) {
        zw_set_errorf("plugin '%s': the check on '%s' refused the arguments (nothing ran)",
                      plugin->name, export_name);
        return ZW_E_CHECK;
    }

    /* Fuel = min(call fuel, floor(remaining budget)). */
    st = zxv_cap_check(h->cap, ZXV_CAP_BUDGET);
    if (st != ZXV_OK) {
        zw_set_error("zw_plugin_call: the host's budget capability is no longer valid");
        return st;
    }
    st = zxv_rmag_status(h->rmag, h->budget, &rn, &rd, NULL, NULL, NULL);
    if (st != ZXV_OK) return st;
    fuel = (rn > 0 && rd > 0) ? (uint64_t) (rn / rd) : 0u;
    if (fuel > h->call_fuel) fuel = h->call_fuel;
    if (fuel == 0) {
        zw_set_errorf("plugin '%s': the rmag budget has no fuel left", plugin->name);
        return ZXV_E_BUDGET;
    }

    M3TaggedValue g;
    memset(&g, 0, sizeof g);
    g.type = c_m3Type_i64;
    g.value.i64 = fuel;
    if (m3_SetGlobal(plugin->fuel, &g) != m3Err_none) return ZXV_E_INTERNAL;
    plugin->nstaged = 0;

    r = m3_Call(f, nargs, nargs ? ptrs : NULL);

    memset(&g, 0, sizeof g);
    (void) m3_GetGlobal(plugin->fuel, &g);
    int64_t left = (int64_t) g.value.i64;
    used = left >= 0 ? fuel - (uint64_t) left : fuel;
    if (used > fuel) used = fuel;
    if (out_fuel_used) *out_fuel_used = used;

    if (used > 0) {
        int32_t cs = zxv_rmag_consume(h->cap, h->rmag, h->budget, (int64_t) used, 1);
        if (cs != ZXV_OK) {
            /* Cannot account for the work: stop the plugin. */
            plugin->state = ZW_PLUGIN_DEAD;
            plugin->nstaged = 0;
            zw_set_errorf("plugin '%s': charging %llu fuel to the budget failed (%s)",
                          plugin->name, (unsigned long long) used, zxv_status_name(cs));
            return cs;
        }
    }

    if (r) {
        int fuel_out = left < 0 || r == zw_fuel_trap;
        plugin->state = ZW_PLUGIN_DEAD;
        plugin->nstaged = 0; /* proposals of a failed run never reach the queue */
        if (fuel_out) {
            zw_set_errorf("plugin '%s' ran out of fuel after %llu units in '%s'", plugin->name,
                          (unsigned long long) used, export_name);
            return ZW_E_FUEL;
        }
        zw_set_errorf("plugin '%s' trapped in '%s': %s", plugin->name, export_name, r);
        return ZW_E_TRAP;
    }

    int64_t res = 0;
    if (rt == c_m3Type_i32) {
        uint32_t v = 0;
        const void *rp[1] = {&v};
        (void) m3_GetResults(f, 1, rp);
        res = (int64_t) (int32_t) v;
    } else if (rt == c_m3Type_i64) {
        uint64_t v = 0;
        const void *rp[1] = {&v};
        (void) m3_GetResults(f, 1, rp);
        res = (int64_t) v;
    }
    if (ci >= 0 && run_check(plugin, ci, ZW_CHECK_AFTER, export_name, args, nargs, res,
                             rt != c_m3Type_none) != 0) {
        plugin->state = ZW_PLUGIN_DEAD;
        plugin->nstaged = 0; /* like a trap: nothing reaches the queue */
        zw_set_errorf("plugin '%s': the check on '%s' refused the result; the plugin is dead",
                      plugin->name, export_name);
        return ZW_E_CHECK;
    }
    if (out_has_result) *out_has_result = rt != c_m3Type_none;
    if (out_result && rt != c_m3Type_none) *out_result = res;
    for (uint32_t i = 0; i < plugin->nstaged; i++) {
        st = queue_push(h, &plugin->staged[i]);
        if (st != ZXV_OK) {
            plugin->nstaged = 0;
            return st;
        }
    }
    plugin->nstaged = 0;
    return ZXV_OK;
}

/* ===== Queue ===== */

static zw_prop *find_prop(zw_host *h, uint64_t id) {
    for (size_t i = 0; i < h->qn; i++)
        if (h->q[i].id == id) return &h->q[i];
    return NULL;
}

int32_t zw_queue_pending(zw_host *host, uint32_t *out_count) {
    if (!host || !out_count) return ZXV_E_ARG;
    *out_count = host->pending;
    return ZXV_OK;
}

int32_t zw_queue_next(zw_host *host, uint64_t *out_id) {
    if (!host || !out_id) return ZXV_E_ARG;
    for (size_t i = 0; i < host->qn; i++) {
        if (host->q[i].state == ZXV_LP_NEITHER) {
            *out_id = host->q[i].id;
            return ZXV_OK;
        }
    }
    return ZXV_E_NOT_FOUND;
}

int32_t zw_proposal_get(zw_host *host, uint64_t id, int32_t *out_state, uint32_t *out_kind,
                        int64_t *out_num, int64_t *out_den, char plugin_name[32],
                        uint8_t *label, size_t label_cap, size_t *out_label_len) {
    zw_prop *p;
    if (!host) return ZXV_E_ARG;
    p = find_prop(host, id);
    if (!p) {
        zw_set_error("zw_proposal_get: no such proposal");
        return ZXV_E_NOT_FOUND;
    }
    if (out_label_len) *out_label_len = p->label_len;
    if (p->label_len && (!label || label_cap <= p->label_len)) return ZXV_E_BUFFER;
    if (out_state) *out_state = p->state;
    if (out_kind) *out_kind = p->kind;
    if (out_num) *out_num = p->num;
    if (out_den) *out_den = p->den;
    if (plugin_name) memcpy(plugin_name, p->plugin, 32);
    if (label && label_cap) {
        if (p->label_len) memcpy(label, p->label, p->label_len);
        if (label_cap > p->label_len) label[p->label_len] = 0;
    }
    return ZXV_OK;
}

static int32_t decide_check(zw_host *host, const zxv_cap *cap, uint64_t id, zw_prop **out) {
    zw_prop *p;
    int32_t st;
    if (!host || !cap) return ZXV_E_ARG;
    st = zxv_cap_check(cap, host->accept_caps);
    if (st != ZXV_OK) {
        zw_set_error("the deciding capability lacks the host's accept capabilities");
        return st;
    }
    p = find_prop(host, id);
    if (!p) {
        zw_set_error("no such proposal");
        return ZXV_E_NOT_FOUND;
    }
    if (p->state != ZXV_LP_NEITHER) {
        zw_set_error("the proposal was already decided");
        return ZXV_E_STATE;
    }
    *out = p;
    return ZXV_OK;
}

int32_t zw_proposal_accept(zw_host *host, const zxv_cap *cap, uint64_t id) {
    zw_prop *p;
    int32_t st = decide_check(host, cap, id, &p);
    if (st != ZXV_OK) return st;
    p->state = ZXV_LP_TRUE;
    host->pending--;
    return ZXV_OK;
}

int32_t zw_proposal_reject(zw_host *host, const zxv_cap *cap, uint64_t id) {
    zw_prop *p;
    int32_t st = decide_check(host, cap, id, &p);
    if (st != ZXV_OK) return st;
    p->state = ZXV_LP_FALSE;
    host->pending--;
    return ZXV_OK;
}

int32_t zw_proposal_accept_post(zw_host *host, const zxv_cap *cap, uint64_t id,
                                uint32_t from_account, uint32_t to_account, const char *ccy,
                                const char *reference, uint64_t *out_entry_id) {
    zw_prop *p;
    int32_t st = decide_check(host, cap, id, &p);
    if (st != ZXV_OK) return st;
    if (p->den != 1 || p->num <= 0) {
        zw_set_error("only a positive integer proposal can be posted as minor units");
        return ZXV_E_RANGE;
    }
    st = zxv_ledger_post_cap(cap, from_account, to_account, p->num, ccy, reference, out_entry_id);
    if (st != ZXV_OK) {
        char d[200];
        size_t n = 0;
        if (zxv_last_error(d, sizeof d, &n) != ZXV_OK) d[0] = '\0';
        zw_set_errorf("ledger post refused (%s): %s; the proposal stays S0", zxv_status_name(st), d);
        return st;
    }
    p->state = ZXV_LP_TRUE;
    host->pending--;
    return ZXV_OK;
}
