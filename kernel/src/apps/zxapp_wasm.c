/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_wasm.c — the WebAssembly runtime for app manifests (zxapp.h A2), over
 * the Z-Wasm kit (sdk/kits/z-wasm/include/zwasm.h). Hosted only.
 *
 * WebAssembly is how an app's logic can be in any language: Rust, Go, C++,
 * Swift, Kotlin, Dart, Zig, AssemblyScript and the rest compile to it, and
 * Z-Wasm runs the result in a sandbox that imports nothing but exact
 * rational and four-valued logic helpers, and charges every instruction to
 * a fuel budget (so a stuck plugin stops, it never hangs the screen).
 *
 *   W1 An entry's source is a .wasm module that exports `run`, taking up to
 *      eight i32/i64 parameters and returning one (integers only: Z-Wasm
 *      refuses floating point, as the pillars do, R11).
 *   W2 The call's arguments are `run`'s parameters in order. They are
 *      integer vectors of one length n, or n = 1 for a value every row
 *      shares; row k calls run(args[0][k], args[1][k], ...) and its result
 *      is the row's value (den = 1).
 *   W3 A trap or running out of fuel is a refusal (ZXAPP_E_RUNTIME, in
 *      Z-Wasm's words). The module is then dead; the runtime reloads it on
 *      the next call so one bad input does not stop the app's logic. */
#include <stdlib.h>
#include <string.h>

#include "zwasm.h"
#include "zxapp_wasm.h"

typedef struct {
    zw_host *host;
    zw_plugin *p;
    uint8_t *bytes; /* kept for W3's reload */
    uint32_t n;
} wasm_entry_t;

static void say(char *err, uint32_t cap, const char *m)
{
    if (!err || !cap) return;
    size_t n = strlen(m);
    if (n >= cap) n = cap - 1u;
    memcpy(err, m, n);
    err[n] = 0;
}

static void say_last(char *err, uint32_t cap, const char *dflt)
{
    char m[160];
    if (zw_last_error(m, sizeof m, 0) == ZW_OK && m[0])
        say(err, cap, m);
    else
        say(err, cap, dflt);
}

static int w_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    zxapp_wasm_t *w = ctx;
    wasm_entry_t *e = calloc(1, sizeof *e);
    if (e) e->bytes = malloc(n ? n : 1u);
    if (!e || !e->bytes) {
        free(e);
        say(err, cap, "out of memory");
        return ZXAPP_E_RUNTIME;
    }
    memcpy(e->bytes, src, n);
    e->n = n;
    e->host = w->host;
    if (zw_plugin_load(e->host, e->bytes, n, "zxapp", &e->p) != ZW_OK) {
        say_last(err, cap, "Z-Wasm refused the module");
        free(e->bytes);
        free(e);
        return ZXAPP_E_RUNTIME;
    }
    *compiled = e;
    return ZXAPP_OK;
}

static int w_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    wasm_entry_t *e = compiled;
    if (nargs > 8u) {
        say(err, errcap, "run takes at most eight arguments");
        return ZXAPP_E_RUNTIME;
    }
    uint32_t rows = 1;
    for (uint32_t i = 0; i < nargs; i++) {
        if (!args[i].n) {
            say(err, errcap, "an empty argument");
            return ZXAPP_E_RUNTIME;
        }
        if (args[i].n == 1u) continue;
        if (rows != 1u && args[i].n != rows) {
            say(err, errcap, "Wasm arguments of two lengths");
            return ZXAPP_E_RUNTIME;
        }
        rows = args[i].n;
    }
    *n_out = rows;
    if (rows > cap) return ZXAPP_E_BUFFER;
    int32_t state = ZW_PLUGIN_READY;
    if (zw_plugin_state(e->p, &state) == ZW_OK && state == ZW_PLUGIN_DEAD) { /* W3 */
        zw_plugin_destroy(e->p);
        e->p = 0;
        if (zw_plugin_load(e->host, e->bytes, e->n, "zxapp", &e->p) != ZW_OK) {
            say_last(err, errcap, "Z-Wasm could not reload the module");
            return ZXAPP_E_RUNTIME;
        }
    }
    for (uint32_t k = 0; k < rows; k++) {
        int64_t a[8];
        for (uint32_t i = 0; i < nargs; i++) a[i] = args[i].v[args[i].n == 1u ? 0 : k];
        int64_t r = 0;
        int32_t has = 0;
        if (zw_plugin_call(e->p, "run", a, nargs, &r, &has, 0) != ZW_OK) {
            say_last(err, errcap, "the Wasm logic stopped");
            return ZXAPP_E_RUNTIME;
        }
        if (!has) {
            say(err, errcap, "run returned nothing");
            return ZXAPP_E_RUNTIME;
        }
        out[k] = (zxapp_num_t){r, 1};
    }
    return ZXAPP_OK;
}

static void w_unload(void *ctx, void *compiled)
{
    (void) ctx;
    wasm_entry_t *e = compiled;
    if (!e) return;
    zw_plugin_destroy(e->p);
    free(e->bytes);
    free(e);
}

int zxapp_wasm_register(zxapp_host_t *h, zxapp_wasm_t *w, uint64_t fuel)
{
    memset(w, 0, sizeof *w);
    zxv_ctx *ctx = 0;
    zxv_cap *root = 0, *bcap = 0;
    zxv_rmag *rm = 0;
    zw_host *host = 0;
    uint32_t bud = 0;
    if (!fuel) fuel = 100000000u;
    if (fuel > (uint64_t) INT64_MAX) fuel = (uint64_t) INT64_MAX;
    if (zxv_ctx_create(&ctx) != ZXV_OK) return ZXAPP_E_RUNTIME;
    w->ctx = ctx;
    if (zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) != ZXV_OK) goto fail;
    w->root = root;
    if (zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &bcap) != ZXV_OK) goto fail;
    w->budget_cap = bcap;
    if (zxv_rmag_create_cap(root, &rm) != ZXV_OK) goto fail;
    w->rmag = rm;
    if (zxv_rmag_budget(root, rm, "zxapp-wasm", ZXV_RES_COMPUTE_CYCLES, (int64_t) fuel, 1, 1,
                        &bud) != ZXV_OK)
        goto fail;
    w->budget = bud;
    if (zw_host_create(bcap, rm, bud, &host) != ZW_OK) goto fail;
    w->host = host;
    if (zw_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC) != ZW_OK) goto fail;
    zxapp_runtime_t rt = {ZXAPP_LANG_WASM, w, w_load, w_call, w_unload};
    if (zxapp_host_register(h, &rt) == ZXAPP_OK) return ZXAPP_OK;
fail:
    zxapp_wasm_close(w);
    return ZXAPP_E_RUNTIME;
}

void zxapp_wasm_close(zxapp_wasm_t *w)
{
    if (w->host) zw_host_destroy((zw_host *) w->host);
    if (w->rmag) zxv_rmag_destroy((zxv_rmag *) w->rmag);
    if (w->budget_cap) zxv_cap_destroy((zxv_cap *) w->budget_cap);
    if (w->root) zxv_cap_destroy((zxv_cap *) w->root);
    if (w->ctx) zxv_ctx_destroy((zxv_ctx *) w->ctx);
    memset(w, 0, sizeof *w);
}
