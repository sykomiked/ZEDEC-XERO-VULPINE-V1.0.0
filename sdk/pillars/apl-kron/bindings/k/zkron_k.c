/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zkron_k.c — the ngn/k binding: one C function, loaded with 2:,
 *
 *     kx:`"libzkron_k.so"2:(`zkron;2)
 *     kx[0;"+/1r3 1r6"]          / "1r2"   the value text
 *     kx[4;"2 2#!4"]             / "(2 2;0 1 2 3;1 1 1 1)"  shape;num;den
 *
 * The first argument selects the text (KR_TEXT_*: 0 value, 1 display,
 * 2 canonical program, 3 digest, 4 shape;numerators;denominators in K
 * syntax); the second is the program, a char vector, in any spelling
 * (KR_SPELL_AUTO). The result is a char vector; a refusal is a K error
 * named by its status ('KR_E_DIVZERO). ngn/k keeps 32 bytes of an error
 * message, so the full diagnostic ("line 1, column 2: 1 ÷ 0: ...") is
 * kept per thread and returned by a second function, zkronwhy (arity 1,
 * argument ignored). kron.k wraps both.
 *
 * k.h is ngn/k's (AGPLv3) and is not copied here; it is included from the
 * ngn/k source tree at build time. The extension resolves Kc/KC/KE/CK/...
 * from the k binary, which must be linked with -Wl,-E. ngn/k passes
 * arguments to an extension with their references, so this function
 * releases them (unref). No float type is used (R11); k.h declares one,
 * which this file never touches. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k.h"
#include "zkron.h"

K zkron(K what, K text);
K zkronwhy(K ignored);

static _Thread_local char g_why[1024];

static void set_why(const char *msg, size_t n) {
    if (n >= sizeof g_why) n = sizeof g_why - 1u;
    memcpy(g_why, msg, n);
    g_why[n] = '\0';
}

/* A K error named by the status; the diagnostic goes to zkronwhy. */
static K fail_st(int32_t st, const char *msg) {
    set_why(msg, strlen(msg));
    return KE(kr_status_name(st)); /* KE copies at most 32 bytes */
}

static K fail(const char *msg) { return fail_st(ZXV_E_ARG, msg); }

K zkronwhy(K ignored) {
    unref(ignored);
    return KC(g_why, strlen(g_why));
}

K zkron(K what, K text) {
    char *src = NULL, *out = NULL;
    size_t n, cap = 4096u, len = 0;
    int32_t st;
    I w;
    if (TK(what) != 'i') {
        unref(what);
        unref(text);
        return fail("zkron: the first argument is an int (0 value, 1 display, 2 program, 3 digest, 4 nd)");
    }
    w = iK(what);
    unref(what);
    if (TK(text) != 'C') {
        unref(text);
        return fail("zkron: the program is a char vector");
    }
    n = NK(text);
    src = (char *) malloc(n ? n : 1u);
    if (!src) {
        unref(text);
        return fail_st(ZXV_E_NOMEM, "zkron: out of memory");
    }
    CK(src, text); /* copies the chars and releases text */
    for (;;) {
        char *nb = (char *) realloc(out, cap);
        if (!nb) {
            free(src);
            free(out);
            return fail_st(ZXV_E_NOMEM, "zkron: out of memory");
        }
        out = nb;
        st = kr_run_text(src, n, KR_SPELL_AUTO, w, out, cap, &len);
        if (st != ZXV_E_BUFFER || len + 1u <= cap) break;
        cap = len + 1u;
    }
    free(src);
    if (st != ZXV_OK) {
        K e;
        if (st == ZXV_E_ARG && len == 0) {
            free(out);
            return fail("zkron: the first argument is 0..4");
        }
        set_why(out, len < 1024u ? len : 1023u);
        e = KE(kr_status_name(st));
        free(out);
        return e;
    }
    g_why[0] = '\0';
    {
        K r = KC(out, len);
        free(out);
        return r;
    }
}
