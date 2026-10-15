/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_tecton.c — the TECTON front-end (twin Tectondrake): a placeholder until it
 * is built. TECTON is one of ZXV's own three languages for coding by purpose
 * and intent; it composes verified blocks and is built and checked in
 * kernel/src/alexandria/alx_tecton.c (docs/ALEXANDRIA.md). This entry gives it its
 * Lingua number and twin. */
#include "zl_int.h"

int zl_tecton_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the TECTON front-end is not built yet");
}
