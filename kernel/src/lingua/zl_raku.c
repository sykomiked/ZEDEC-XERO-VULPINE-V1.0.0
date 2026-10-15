/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_raku.c — the Raku front-end (twin Kilnwyrm): a placeholder until it
 * is built (LINGUA_SPEC.md). */
#include "zl_int.h"

int zl_raku_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the Raku front-end is not built yet");
}
