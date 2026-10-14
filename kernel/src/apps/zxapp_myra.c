/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_myra.c — the MYRA runtime for app manifests (zxapp.h A2), over the
 * mumps-myra pillar (sdk/pillars/mumps-myra/include/zmyra.h). Hosted only:
 * the pillar sits on libzxv.
 *
 * MYRA declares what an M global's records hold; the pillar holds values
 * to that declaration exactly, by M's own number rules. It is a schema
 * language with no statements, so a MYRA entry asks the question it can
 * answer: do these values make a record of this node?
 *
 *     ^ZXFEE(batch:i9, seq:i9) = amount:n15.2! ^ fee:n9.2! ^ from:i10! ^ to:i10!
 *
 *   MY1 The source is a MYRA schema or a global map (zm_schema_parse). The
 *       entry checks records of its first node.
 *   MY2 Each argument names a key or piece of that node (case-insensitive)
 *       of type INTEGER, NUMBER or DATE, in the field's own units: an
 *       argument for n15.2 is in hundredths, one for a DATE is a $H day.
 *       Arguments are integer vectors of one length n, or n = 1 for a
 *       value every row shares.
 *   MY3 Row k stores each argument's k-th value in a fresh record with
 *       zm_put_decimal and answers a truth (num = ZXV_LP_*, den = 0, as
 *       Eve's): TRUE when every value fits its field, FALSE when one does
 *       not (ZM_E_FIT: too many digits, a date outside $H 0..2980013).
 *   MY4 A name the node does not have, a TEXT field, vectors of two
 *       lengths: a refusal (ZXAPP_E_RUNTIME), in the pillar's words where
 *       it has them. */
#include <stdlib.h>
#include <string.h>

#include "zmyra.h"
#include "zxapp_myra.h"

static void say(char *err, uint32_t cap, const char *m)
{
    if (!err || !cap) return;
    size_t n = strlen(m);
    if (n >= cap) n = cap - 1u;
    memcpy(err, m, n);
    err[n] = 0;
}

static void say2(char *err, uint32_t cap, const char *m1, const char *m2)
{
    if (!err || !cap) return;
    uint32_t k = 0;
    for (const char *p = m1; p && *p && k + 1u < cap; p++) err[k++] = *p;
    for (const char *p = m2; p && *p && k + 1u < cap; p++) err[k++] = *p;
    err[k] = 0;
}

static int m_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    zm_schema *s = 0;
    zm_diag d;
    memset(&d, 0, sizeof d);
    if (zm_schema_parse(src, n, &s, &d) != ZXV_OK) {
        say(err, cap, d.msg[0] ? d.msg : "MYRA did not parse");
        return ZXAPP_E_RUNTIME;
    }
    uint32_t nodes = 0;
    if (zm_node_count(s, &nodes) != ZXV_OK || !nodes) {
        zm_schema_destroy(s);
        say(err, cap, "a MYRA entry needs a node to check");
        return ZXAPP_E_RUNTIME;
    }
    *compiled = s;
    return ZXAPP_OK;
}

static int m_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    const zm_schema *s = compiled;
    uint32_t rows = 1, scale[ZXAPP_INPUTS * 4u];
    if (nargs > sizeof scale / sizeof scale[0]) {
        say(err, errcap, "too many arguments");
        return ZXAPP_E_RUNTIME;
    }
    for (uint32_t i = 0; i < nargs; i++) { /* MY2, MY4 */
        uint32_t idx = 0;
        zm_field f;
        if (!args[i].n || !args[i].name || zm_field_find(s, 0, args[i].name, &idx) != ZXV_OK ||
            zm_field_at(s, 0, idx, &f) != ZXV_OK) {
            say2(err, errcap,
                 "an argument the MYRA node does not have: ", args[i].name ? args[i].name : "?");
            return ZXAPP_E_RUNTIME;
        }
        if (f.type == ZM_T_TEXT) {
            say2(err, errcap, "a TEXT field takes no number: ", args[i].name);
            return ZXAPP_E_RUNTIME;
        }
        scale[i] = f.scale;
        if (args[i].n == 1u) continue;
        if (rows != 1u && args[i].n != rows) {
            say(err, errcap, "MYRA arguments of two lengths");
            return ZXAPP_E_RUNTIME;
        }
        rows = args[i].n;
    }
    *n_out = rows;
    if (rows > cap) return ZXAPP_E_BUFFER;
    zm_record *r = 0;
    if (zm_record_create(s, 0, &r) != ZXV_OK) {
        say(err, errcap, "out of memory");
        return ZXAPP_E_RUNTIME;
    }
    int rc = ZXAPP_OK;
    for (uint32_t k = 0; k < rows && rc == ZXAPP_OK; k++) { /* MY3 */
        zm_record_clear(r);
        int32_t lp = ZXV_LP_TRUE;
        for (uint32_t i = 0; i < nargs && lp == ZXV_LP_TRUE; i++) {
            int64_t x = args[i].v[args[i].n == 1u ? 0 : k];
            int32_t st = zm_put_decimal(r, args[i].name, x, scale[i]);
            if (st == ZM_E_FIT)
                lp = ZXV_LP_FALSE;
            else if (st != ZXV_OK) {
                char m[160];
                size_t len = 0;
                say(err, errcap,
                    zm_last_error(m, sizeof m, &len) == ZXV_OK && m[0] ? m : "MYRA refused");
                rc = ZXAPP_E_RUNTIME;
            }
        }
        out[k] = (zxapp_num_t){lp, 0};
    }
    zm_record_destroy(r);
    return rc;
}

static void m_unload(void *ctx, void *compiled)
{
    (void) ctx;
    zm_schema_destroy(compiled);
}

int zxapp_myra_register(zxapp_host_t *h)
{
    zxapp_runtime_t rt = {ZXAPP_LANG_MYRA, 0, m_load, m_call, m_unload};
    return zxapp_host_register(h, &rt);
}
