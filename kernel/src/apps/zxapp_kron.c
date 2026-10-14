/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_kron.c — the KRON runtime for app manifests (zxapp.h A2), over the
 * apl-kron pillar (sdk/pillars/apl-kron/include/zkron.h). Hosted only: the
 * pillar sits on libzxv.
 *
 * A KRON entry is one program in APL glyphs, K-style ASCII or canonical
 * KRON. Its free names are the call's arguments (integer vectors), and its
 * value is the result: exact rationals, or four-valued truth returned as
 * num = the ZXV_LP_* code with den = 0. Nothing is rounded: a result that
 * does not fit int64/int64 is the pillar's refusal, passed on. */
#include <stdlib.h>
#include <string.h>

#include "zkron.h"
#include "zxapp_kron.h"

static void say(char *err, uint32_t cap, const char *m)
{
    if (!err || !cap) return;
    size_t n = strlen(m);
    if (n >= cap) n = cap - 1u;
    memcpy(err, m, n);
    err[n] = 0;
}

static int k_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    kr_program *p = 0;
    kr_diag d;
    memset(&d, 0, sizeof d);
    if (kr_parse(src, n, KR_SPELL_AUTO, &p, &d) != KR_OK) {
        say(err, cap, d.msg[0] ? d.msg : "KRON did not parse");
        return ZXAPP_E_RUNTIME;
    }
    *compiled = p;
    return ZXAPP_OK;
}

static int k_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    int rc = ZXAPP_E_RUNTIME;
    kr_binding *b = calloc(nargs ? nargs : 1u, sizeof *b);
    kr_value **vals = calloc(nargs ? nargs : 1u, sizeof *vals);
    kr_value *res = 0;
    int64_t *num = 0, *den = 0;
    int32_t *tr = 0;
    if (!b || !vals) {
        say(err, errcap, "out of memory");
        goto done;
    }
    for (uint32_t i = 0; i < nargs; i++) {
        int64_t shape[2] = {(int64_t) args[i].n, 0};
        int64_t one = 1;
        int64_t *dens = calloc(args[i].n ? args[i].n : 1u, sizeof *dens);
        if (!dens) {
            say(err, errcap, "out of memory");
            goto done;
        }
        for (uint32_t k = 0; k < args[i].n; k++) dens[k] = one;
        int32_t s = kr_value_make(KR_KIND_NUM, 1u, shape, args[i].v, dens, 0, &vals[i]);
        free(dens);
        if (s != KR_OK) {
            say(err, errcap, "an argument KRON cannot hold");
            goto done;
        }
        b[i].name = args[i].name;
        b[i].value = vals[i];
    }
    kr_diag d;
    memset(&d, 0, sizeof d);
    if (kr_eval((const kr_program *) compiled, b, nargs, 0, &res, &d) != KR_OK) {
        say(err, errcap, d.msg[0] ? d.msg : "KRON refused");
        goto done;
    }
    int32_t kind = 0;
    uint64_t count = 0;
    kr_value_info(res, &kind, 0, 0, &count);
    *n_out = (uint32_t) count;
    if (count > cap) {
        rc = ZXAPP_E_BUFFER;
        goto done;
    }
    num = calloc(count ? count : 1u, sizeof *num);
    den = calloc(count ? count : 1u, sizeof *den);
    tr = calloc(count ? count : 1u, sizeof *tr);
    if (!num || !den || !tr) {
        say(err, errcap, "out of memory");
        goto done;
    }
    if (kind == KR_KIND_NUM) {
        if (kr_value_rats(res, num, den, count) != KR_OK) goto done;
        for (uint64_t k = 0; k < count; k++) out[k] = (zxapp_num_t){num[k], den[k]};
    } else {
        if (kr_value_truths(res, tr, count) != KR_OK) goto done;
        for (uint64_t k = 0; k < count; k++) out[k] = (zxapp_num_t){tr[k], 0};
    }
    rc = ZXAPP_OK;
done:
    for (uint32_t i = 0; vals && i < nargs; i++) kr_value_destroy(vals[i]);
    kr_value_destroy(res);
    free(vals);
    free(b);
    free(num);
    free(den);
    free(tr);
    return rc;
}

static void k_unload(void *ctx, void *compiled)
{
    (void) ctx;
    kr_program_destroy((kr_program *) compiled);
}

int zxapp_kron_register(zxapp_host_t *h)
{
    zxapp_runtime_t rt = {ZXAPP_LANG_KRON, 0, k_load, k_call, k_unload};
    return zxapp_host_register(h, &rt);
}
