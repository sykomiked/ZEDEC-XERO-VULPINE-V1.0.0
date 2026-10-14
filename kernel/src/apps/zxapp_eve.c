/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_eve.c — the Eve runtime for app manifests (zxapp.h A2), over the
 * ada-eve pillar (sdk/pillars/ada-eve/include/zeve.h). Hosted only: the
 * pillar sits on libzxv.
 *
 * Eve says what values may be, so an Eve entry is a set of limits: its
 * source is an Eve package, and the entry checks values against the first
 * record type the package declares.
 *
 *   EV1 Each argument names a component of that record ("share", or a
 *       path such as "limits.cap"). Arguments are integer vectors of one
 *       length n, or n = 1 for a value every row shares.
 *   EV2 Row k builds a record from each argument's k-th value and returns
 *       a truth (num = ZXV_LP_*, den = 0, as KRON's truths): TRUE when
 *       every value fits its declared range, FALSE when one does not.
 *   EV3 Components no argument names keep Eve's canonical initial value.
 *       A name the record does not have, or vectors of two lengths, is a
 *       refusal (ZXAPP_E_RUNTIME, with Eve's own words). */
#include <stdlib.h>
#include <string.h>

#include "zeve.h"
#include "zxapp_eve.h"

typedef struct {
    ze_pkg *pkg;
    uint32_t type; /* the record checked */
    uint64_t size;
} eve_entry_t;

static void say(char *err, uint32_t cap, const char *m)
{
    if (!err || !cap) return;
    size_t n = strlen(m);
    if (n >= cap) n = cap - 1u;
    memcpy(err, m, n);
    err[n] = 0;
}

static int e_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    ze_pkg *pkg = 0;
    ze_diag d;
    memset(&d, 0, sizeof d);
    if (ze_eve_parse(src, n, &pkg, &d) != ZXV_OK) {
        say(err, cap, d.msg[0] ? d.msg : "Eve did not parse");
        return ZXAPP_E_RUNTIME;
    }
    uint32_t count = 0;
    ze_type_count(pkg, &count);
    for (uint32_t i = 0; i < count; i++) {
        ze_type t;
        if (ze_type_at(pkg, i, &t) == ZXV_OK && t.kind == ZE_K_RECORD && t.name[0]) {
            eve_entry_t *e = malloc(sizeof *e);
            if (!e) break;
            e->pkg = pkg;
            e->type = i;
            e->size = t.size;
            *compiled = e;
            return ZXAPP_OK;
        }
    }
    ze_pkg_destroy(pkg);
    say(err, cap, "an Eve entry needs a record type to check");
    return ZXAPP_E_RUNTIME;
}

static int e_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    const eve_entry_t *e = compiled;
    uint32_t rows = 1;
    for (uint32_t i = 0; i < nargs; i++) {
        if (args[i].n == 1u) continue;
        if (rows != 1u && args[i].n != rows) {
            say(err, errcap, "Eve arguments of two lengths");
            return ZXAPP_E_RUNTIME;
        }
        rows = args[i].n;
    }
    for (uint32_t i = 0; i < nargs; i++)
        if (!args[i].n || ze_find(e->pkg, e->type, args[i].name, 0, 0) != ZXV_OK) {
            say(err, errcap, "an argument the Eve record does not have");
            return ZXAPP_E_RUNTIME;
        }
    *n_out = rows;
    if (rows > cap) return ZXAPP_E_BUFFER;
    uint8_t *val = malloc(e->size ? e->size : 1u);
    if (!val) {
        say(err, errcap, "out of memory");
        return ZXAPP_E_RUNTIME;
    }
    int rc = ZXAPP_OK;
    for (uint32_t k = 0; k < rows && rc == ZXAPP_OK; k++) {
        ze_value_init(e->pkg, e->type, val, e->size);
        int32_t lp = ZXV_LP_TRUE;
        for (uint32_t i = 0; i < nargs && lp == ZXV_LP_TRUE; i++) {
            int64_t x = args[i].v[args[i].n == 1u ? 0 : k];
            int32_t s = ze_put_decimal(e->pkg, e->type, val, e->size, args[i].name, x, 0);
            if (s == ZE_E_FIT)
                lp = ZXV_LP_FALSE; /* EV2: outside its limits */
            else if (s != ZXV_OK) {
                char m[160];
                say(err, errcap, ze_last_error(m, sizeof m, 0) == ZXV_OK ? m : "Eve refused");
                rc = ZXAPP_E_RUNTIME;
            }
        }
        out[k] = (zxapp_num_t){lp, 0};
    }
    free(val);
    return rc;
}

static void e_unload(void *ctx, void *compiled)
{
    (void) ctx;
    eve_entry_t *e = compiled;
    if (!e) return;
    ze_pkg_destroy(e->pkg);
    free(e);
}

int zxapp_eve_register(zxapp_host_t *h)
{
    zxapp_runtime_t rt = {ZXAPP_LANG_EVE, 0, e_load, e_call, e_unload};
    return zxapp_host_register(h, &rt);
}
