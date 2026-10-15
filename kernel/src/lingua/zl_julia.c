/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_julia.c — the Julia front-end (twin Tallydrake): a placeholder until it
 * is built (LINGUA_SPEC.md). */
#include "zl_int.h"

int zl_julia_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the Julia front-end is not built yet");
}
