/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_trebago.c — the Trebago runtime for app manifests (zxapp.h A2), over
 * the fortran-trebago pillar (sdk/pillars/fortran-trebago/include/
 * ztrebago.h). Hosted only: the pillar sits on libzxv.
 *
 * A Trebago entry is one Trebago program, parsed and run by the pillar
 * (tb_parse, tb_exec) exactly as the Fortran module would run it:
 *
 *     trebago total
 *       in  amount(n), fee(n)
 *       out total
 *       total = sum(i, amount(i) + fee(i))
 *     end
 *
 *   TB1 Each argument binds the in or inout parameter of its name (a
 *       scalar takes a one-element vector). Every in and inout parameter
 *       must be bound; an argument the program does not take is refused.
 *       Arguments are integers (den 1); the program computes over exact
 *       rationals.
 *   TB2 The answer is the program's first out parameter (its first inout
 *       one if it has no out), every element, as reduced rationals. Other
 *       outputs are computed and dropped. An out vector's extent is the
 *       extent of the input that shares its dimension name (or a fixed
 *       extent), checked at load.
 *   TB3 Parameters are scalars and vectors: a matrix parameter is refused
 *       at load, since app arguments are vectors.
 *   TB4 Every refusal is the pillar's, with its words and status name: a
 *       result that does not fit int64/int64 (ZXV_E_RANGE, never rounded),
 *       a division by zero, a bound or shape error, the work limit. */
#include <stdlib.h>
#include <string.h>

#include "ztrebago.h"
#include "zxapp_trebago.h"

#define TB_APP_WORK (UINT64_C(1) << 28) /* rational operations in one call */

typedef struct {
    tb_program *p;
    tb_param *prm;
    uint32_t np;
    uint32_t ans; /* TB2 */
} tb_entry_t;

static void say(char *err, uint32_t cap, const char *m)
{
    if (!err || !cap) return;
    size_t n = strlen(m);
    if (n >= cap) n = cap - 1u;
    memcpy(err, m, n);
    err[n] = 0;
}

static void say3(char *err, uint32_t cap, const char *m1, const char *m2, const char *m3)
{
    if (!err || !cap) return;
    uint32_t k = 0;
    const char *m[3] = {m1, m2, m3};
    for (uint32_t j = 0; j < 3u; j++)
        for (const char *p = m[j]; p && *p && k + 1u < cap; p++) err[k++] = *p;
    err[k] = 0;
}

static int same_name(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (unsigned char) *a, y = (unsigned char) *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static int is_input(const tb_param *p)
{
    return p->role == TB_ROLE_IN || p->role == TB_ROLE_INOUT;
}

/* The input parameter whose dimension `dim` names; -1 when none does. */
static int32_t dim_source(const tb_entry_t *e, const char *dim)
{
    for (uint32_t i = 0; i < e->np; i++)
        if (is_input(&e->prm[i]) && e->prm[i].rank == 1u && e->prm[i].dim[0][0] &&
            same_name(e->prm[i].dim[0], dim))
            return (int32_t) i;
    return -1;
}

static void entry_free(tb_entry_t *e)
{
    if (!e) return;
    tb_program_destroy(e->p);
    free(e->prm);
    free(e);
}

static int t_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    tb_diag d;
    memset(&d, 0, sizeof d);
    tb_entry_t *e = calloc(1, sizeof *e);
    if (!e) {
        say(err, cap, "out of memory");
        return ZXAPP_E_RUNTIME;
    }
    if (tb_parse(src, n, &e->p, &d) != ZXV_OK) {
        say(err, cap, d.msg[0] ? d.msg : "Trebago did not parse");
        entry_free(e);
        return ZXAPP_E_RUNTIME;
    }
    tb_param_count(e->p, &e->np);
    e->prm = calloc(e->np ? e->np : 1u, sizeof *e->prm);
    if (!e->prm) {
        say(err, cap, "out of memory");
        entry_free(e);
        return ZXAPP_E_RUNTIME;
    }
    uint32_t out = UINT32_MAX, inout = UINT32_MAX;
    for (uint32_t i = 0; i < e->np; i++) {
        tb_param_at(e->p, i, &e->prm[i]);
        const tb_param *p = &e->prm[i];
        if (p->rank > 1u) { /* TB3 */
            say3(err, cap, "a Trebago entry takes scalars and vectors, not the matrix ", p->name,
                 "");
            entry_free(e);
            return ZXAPP_E_RUNTIME;
        }
        if (p->role == TB_ROLE_OUT && out == UINT32_MAX) out = i;
        if (p->role == TB_ROLE_INOUT && inout == UINT32_MAX) inout = i;
    }
    for (uint32_t i = 0; i < e->np; i++) {
        const tb_param *p = &e->prm[i];
        if (p->role == TB_ROLE_OUT && p->rank == 1u && p->dim[0][0] &&
            dim_source(e, p->dim[0]) < 0) {
            say3(err, cap, p->name, ": its extent comes from no input", "");
            entry_free(e);
            return ZXAPP_E_RUNTIME;
        }
    }
    e->ans = out != UINT32_MAX ? out : inout;
    if (e->ans == UINT32_MAX) {
        say(err, cap, "a Trebago entry needs an out parameter for its answer");
        entry_free(e);
        return ZXAPP_E_RUNTIME;
    }
    *compiled = e;
    return ZXAPP_OK;
}

static int t_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    const tb_entry_t *e = compiled;
    int rc = ZXAPP_E_RUNTIME;
    int32_t *bound = calloc(e->np ? e->np : 1u, sizeof *bound); /* arg index + 1 */
    tb_array *arr = calloc(e->np ? e->np : 1u, sizeof *arr);
    if (!bound || !arr) {
        say(err, errcap, "out of memory");
        goto done;
    }
    for (uint32_t i = 0; i < nargs; i++) { /* TB1 */
        uint32_t k = 0;
        while (k < e->np &&
               !(is_input(&e->prm[k]) && args[i].name && same_name(e->prm[k].name, args[i].name)))
            k++;
        if (k == e->np) {
            say3(err, errcap, "an argument the Trebago program does not take: ",
                 args[i].name ? args[i].name : "?", "");
            goto done;
        }
        if (bound[k]) {
            say3(err, errcap, "an argument given twice: ", args[i].name, "");
            goto done;
        }
        if (e->prm[k].rank == 0u && args[i].n != 1u) {
            say3(err, errcap, args[i].name, ": a scalar takes one value", "");
            goto done;
        }
        bound[k] = (int32_t) i + 1;
    }
    for (uint32_t k = 0; k < e->np; k++) {
        const tb_param *p = &e->prm[k];
        int64_t ext = 1;
        if (is_input(p)) {
            if (!bound[k]) {
                say3(err, errcap, "a missing argument: ", p->name, "");
                goto done;
            }
            ext = (int64_t) args[bound[k] - 1].n;
        } else if (p->rank == 1u) {
            if (p->dim[0][0]) {
                int32_t s = dim_source(e, p->dim[0]);
                ext = s >= 0 && bound[s] ? (int64_t) args[bound[s] - 1].n : 0;
            } else {
                ext = p->fixed[0];
            }
        }
        if (ext < 0 || ext > (int64_t) TB_MAX_ELEMENTS) {
            say3(err, errcap, p->name, ": too many elements", "");
            goto done;
        }
        size_t cnt = ext ? (size_t) ext : 1u;
        arr[k].name = p->name;
        arr[k].rank = p->rank;
        arr[k].extent[0] = p->rank ? ext : 0;
        arr[k].num = calloc(cnt, sizeof *arr[k].num);
        arr[k].den = calloc(cnt, sizeof *arr[k].den);
        if (!arr[k].num || !arr[k].den) {
            say(err, errcap, "out of memory");
            goto done;
        }
        for (size_t j = 0; j < cnt; j++) arr[k].den[j] = 1;
        if (is_input(p) && ext)
            memcpy(arr[k].num, args[bound[k] - 1].v, (size_t) ext * sizeof *arr[k].num);
    }
    tb_diag d;
    memset(&d, 0, sizeof d);
    int32_t s = tb_exec(e->p, arr, e->np, TB_APP_WORK, &d);
    if (s != ZXV_OK) { /* TB4 */
        say3(err, errcap, d.msg[0] ? d.msg : "Trebago refused", " [", tb_status_name(s));
        size_t k = strlen(err);
        if (k + 1u < errcap) {
            err[k] = ']';
            err[k + 1u] = 0;
        }
        goto done;
    }
    const tb_array *a = &arr[e->ans];
    uint32_t count = a->rank ? (uint32_t) a->extent[0] : 1u;
    *n_out = count;
    if (count > cap) {
        rc = ZXAPP_E_BUFFER;
        goto done;
    }
    for (uint32_t j = 0; j < count; j++) {
        int64_t rn = 0, rd = 1;
        if (zxv_rat_normalize(a->num[j], a->den[j], &rn, &rd) != ZXV_OK) {
            say(err, errcap, "Trebago answered a value that is not a rational");
            goto done;
        }
        out[j] = (zxapp_num_t){rn, rd};
    }
    rc = ZXAPP_OK;
done:
    for (uint32_t k = 0; arr && k < e->np; k++) {
        free(arr[k].num);
        free(arr[k].den);
    }
    free(arr);
    free(bound);
    return rc;
}

static void t_unload(void *ctx, void *compiled)
{
    (void) ctx;
    entry_free(compiled);
}

int zxapp_trebago_register(zxapp_host_t *h)
{
    zxapp_runtime_t rt = {ZXAPP_LANG_TREBAGO, 0, t_load, t_call, t_unload};
    return zxapp_host_register(h, &rt);
}
