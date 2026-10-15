/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_lexis.c — the LEXIS front-end (twin Lexwyrm): a placeholder until it
 * is built. LEXIS is one of ZXV's own three languages for coding by purpose
 * and intent; it resolves names to verified blocks and is built and checked in
 * kernel/src/alexandria/alx_ref.c (docs/ALEXANDRIA.md). This entry gives it its
 * Lingua number and twin. */
#include "zl_int.h"

int zl_lexis_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the LEXIS front-end is not built yet");
}
