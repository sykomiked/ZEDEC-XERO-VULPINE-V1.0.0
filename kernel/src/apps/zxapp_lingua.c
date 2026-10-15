/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_lingua.c — legacy-language app logic (zxapp.h A2) through Lingua.
 *
 * An entry is a whole program in any Lingua language (zl_langs.def: FORTRAN 77,
 * Minimal BASIC, Forth, Pascal, ALGOL 60, LISP, Scheme, Logo and the rest; a
 * language whose front-end is not built yet refuses at load), or the
 * same program precompiled to Lingua IR ("zlir", from `zxlang pack`). It is
 * compiled once, when the app loads, and every call runs the compiled IR,
 * so a precompiled entry never needs a front-end at all.
 *
 *   L1 ARGUMENTS IN. The call's arguments, every element of every vector in
 *      contract order, are the program's input: what FORTRAN's READ * and
 *      BASIC's INPUT read, and for Forth the data stack, first argument
 *      deepest. A zlir entry takes them the way its source language did.
 *   L2 ANSWER OUT. The answer is every number the program prints, in order,
 *      as integers (den = 1). Printed text that is not a number is the
 *      runtime's refusal, never skipped: a program that prints words is a
 *      program for people, not a block.
 *   L3 CHECKED. A trap (overflow, division by zero, an inexact division,
 *      bounds, fuel) is a refusal naming the source line, so a witnessed
 *      call (A5) joins it as BOTH, never as an answer.
 *
 * Hosted: it allocates each compiled program (about 200 KB) at load. */
#include <stdlib.h>
#include <string.h>

#include "../lingua/zl.h"
#include "zxapp_lingua.h"

typedef struct {
    zxapp_lang_t lang;
    int zl;
    uint64_t fuel;
} lctx_t;

static lctx_t CTX[ZXAPP_LANG_COUNT -
                  ZXAPP_LANG_ZLIR]; /* one set per process: the last registration's fuel applies */

static void say(char *err, uint32_t cap, const char *a, const char *b, uint32_t line)
{
    if (!err || !cap) return;
    uint32_t k = 0;
    if (line) {
        char t[16];
        int n = 0;
        do t[n++] = (char) ('0' + line % 10u);
        while ((line /= 10u) && n < 15);
        const char *pre = "line ";
        while (*pre && k + 1 < cap) err[k++] = *pre++;
        while (n && k + 1 < cap) err[k++] = t[--n];
        if (k + 1 < cap) err[k++] = ':';
        if (k + 1 < cap) err[k++] = ' ';
    }
    for (; a && *a && k + 1 < cap; a++) err[k++] = *a;
    for (; b && *b && k + 1 < cap; b++) err[k++] = *b;
    err[k] = 0;
}

static int l_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    const lctx_t *c = ctx;
    zl_prog_t *p = malloc(sizeof *p);
    if (!p) {
        say(err, cap, "out of memory", 0, 0);
        return ZXAPP_E_RUNTIME;
    }
    zl_diag_t d;
    int s;
    if (c->lang == ZXAPP_LANG_ZLIR) {
        s = zl_unpack(src, n, p, &d);
    } else {
        zl_work_t *w = malloc(sizeof *w);
        if (!w) {
            free(p);
            say(err, cap, "out of memory", 0, 0);
            return ZXAPP_E_RUNTIME;
        }
        s = zl_compile(c->zl, src, n, p, w, &d);
        free(w);
    }
    if (s != ZL_OK) {
        say(err, cap, d.msg ? d.msg : zl_status_name(s), 0, d.line);
        free(p);
        return ZXAPP_E_RUNTIME;
    }
    *compiled = p;
    return ZXAPP_OK;
}

/* L2: the printed numbers. Returns how many, or -1 at the first token that
 * is not an integer (or does not fit int64). */
static int64_t numbers(const char *s, size_t n, zxapp_num_t *out, uint32_t cap)
{
    int64_t count = 0;
    size_t i = 0;
    for (;;) {
        while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
        if (i >= n) return count;
        int neg = 0;
        if (s[i] == '-') {
            neg = 1;
            i++;
        }
        size_t st = i;
        uint64_t v = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            uint64_t dg = (uint64_t) (s[i] - '0');
            if (v > (0x8000000000000000ull - dg) / 10u) return -1;
            v = v * 10u + dg;
            i++;
        }
        if (i == st || (i < n && s[i] != ' ' && s[i] != '\n' && s[i] != '\t' && s[i] != '\r'))
            return -1;
        if (!neg && v > 0x7fffffffffffffffull) return -1;
        if ((uint64_t) count < cap)
            out[count] = (zxapp_num_t){
                neg ? (v == 0x8000000000000000ull ? INT64_MIN : -(int64_t) v) : (int64_t) v, 1};
        count++;
    }
}

static int l_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    const lctx_t *c = ctx;
    const zl_prog_t *p = compiled;
    size_t nin = 0;
    for (uint32_t i = 0; i < nargs; i++) nin += args[i].n;
    int rc = ZXAPP_E_RUNTIME;
    int64_t *in = malloc((nin ? nin : 1u) * sizeof *in);
    zl_run_t *r = calloc(1, sizeof *r);
    char *text = malloc(1u << 16);
    if (!in || !r || !text) {
        say(err, errcap, "out of memory", 0, 0);
        goto done;
    }
    size_t k = 0;
    for (uint32_t i = 0; i < nargs; i++)
        for (uint32_t j = 0; j < args[i].n; j++) in[k++] = args[i].v[j];
    r->in = in;
    r->nin = nin;
    r->preload = p->lang == ZL_LANG_FORTH; /* L1 */
    r->out = text;
    r->out_cap = 1u << 16;
    r->fuel = c->fuel;
    int s = zl_run(p, r);
    if (s != ZL_OK) {
        say(err, errcap, "stopped: ", zl_status_name(s), r->trap_line);
        goto done;
    }
    int64_t got = numbers(text, r->out_len, out, cap);
    if (got < 0) {
        say(err, errcap, "printed text that is not a number", 0, 0);
        goto done;
    }
    *n_out = (uint32_t) got;
    rc = (uint64_t) got > cap ? ZXAPP_E_BUFFER : ZXAPP_OK;
done:
    free(in);
    free(r);
    free(text);
    return rc;
}

static void l_unload(void *ctx, void *compiled)
{
    (void) ctx;
    free(compiled);
}

int zxapp_lingua_register(zxapp_host_t *h, uint64_t fuel)
{
    static const int ZL[] = {
        0, /* ZXAPP_LANG_ZLIR: precompiled, no front-end */
#define ZL_LANG_X(id, n, name, twin, fe, ext, rt) ZL_LANG_##id,
#include "../lingua/zl_langs.def"
#undef ZL_LANG_X
    };
    _Static_assert(sizeof ZL / sizeof ZL[0] == sizeof CTX / sizeof CTX[0], "one per runtime");
    for (uint32_t i = 0; i < sizeof ZL / sizeof ZL[0]; i++) {
        zxapp_lang_t lang = (zxapp_lang_t) (ZXAPP_LANG_ZLIR + (int) i);
        CTX[i] = (lctx_t){lang, ZL[i], fuel};
        zxapp_runtime_t rt = {lang, &CTX[i], l_load, l_call, l_unload};
        int s = zxapp_host_register(h, &rt);
        if (s != ZXAPP_OK) return s;
    }
    return ZXAPP_OK;
}
