/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_pce.c — the decision engines as the app model's `c` runtime
 * (zxapp_pce.h P1-P4). */
#include "zxapp_pce.h"
#include "../pce/pce_entry.h"
#include "../pce/pce_prob.h"

static void say(char *err, uint32_t cap, const char *a, const char *b)
{
    if (err == NULL || cap == 0u) return;
    uint32_t n = 0;
    for (const char *s = a; s != NULL && *s != '\0' && n + 1u < cap; s++) err[n++] = *s;
    for (const char *s = b; s != NULL && *s != '\0' && n + 1u < cap; s++) err[n++] = *s;
    err[n] = '\0';
}

static bool blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* P1 */
static int p_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    char name[ZXAPP_ENTRY_MAX];
    if (src == NULL) n = 0;
    uint32_t a = 0, b = n;
    while (a < b && blank(src[a])) a++;
    while (b > a && blank(src[b - 1u])) b--;
    if (b == a || b - a >= sizeof name) {
        say(err, cap, "a c block names one engine entry", 0);
        return ZXAPP_E_RUNTIME;
    }
    for (uint32_t i = a; i < b; i++) {
        if (blank(src[i])) {
            say(err, cap, "a c block names one engine entry", 0);
            return ZXAPP_E_RUNTIME;
        }
        name[i - a] = src[i];
    }
    name[b - a] = '\0';
    const pce_contract_t *c = pce_entry_find(name);
    if (c == NULL) {
        say(err, cap, "no engine entry named ", name);
        return ZXAPP_E_RUNTIME;
    }
    *compiled = (void *) c;
    return ZXAPP_OK;
}

/* P2, P3 */
static int p_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    const pce_contract_t *c = (const pce_contract_t *) compiled;
    pce_arg_t in[PCE_ENTRY_INPUTS];
    pce_num_t res[PCE_N_MAX];
    if (c == NULL || nargs > PCE_ENTRY_INPUTS || (args == NULL && nargs > 0u)) return ZXAPP_E_ARG;
    for (uint32_t i = 0; i < nargs; i++) {
        in[i].name = args[i].name;
        in[i].v = args[i].v;
        in[i].n = args[i].n;
    }
    uint32_t n = 0;
    int rc = pce_entry_call(c->entry, in, nargs, res, PCE_N_MAX, &n);
    switch (rc) {
    case PCE_OK:
        break;
    case PCE_E_ARG:
        say(err, errcap, c->entry, ": arguments outside its contract");
        return ZXAPP_E_ARG;
    case PCE_E_OVERFLOW:
        say(err, errcap, c->entry, ": too large to keep exactly");
        return ZXAPP_E_RUNTIME;
    case PCE_E_CONFLICT:
        say(err, errcap, c->entry, ": the evidence rules every case out");
        return ZXAPP_E_RUNTIME;
    default:
        say(err, errcap, c->entry, ": refused by the engine");
        return ZXAPP_E_RUNTIME;
    }
    *n_out = n;
    if (n > cap || (out == NULL && n > 0u)) return ZXAPP_E_BUFFER;
    for (uint32_t i = 0; i < n; i++) {
        out[i].num = res[i].num;
        out[i].den = res[i].den;
    }
    return ZXAPP_OK;
}

static void p_unload(void *ctx, void *compiled)
{
    (void) ctx;
    (void) compiled;
}

int zxapp_pce_register(zxapp_host_t *h)
{
    zxapp_runtime_t rt = {ZXAPP_LANG_C, 0, p_load, p_call, p_unload};
    return zxapp_host_register(h, &rt);
}
