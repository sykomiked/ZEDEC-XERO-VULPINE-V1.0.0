/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_axion.c — the AXION front-end (twin Axiomdrake): a placeholder until it
 * is built. AXION is one of ZXV's own three languages for coding by purpose
 * and intent; it runs loop-free truth checks and is built and checked in
 * kernel/src/alexandria/alx_axion.c (docs/ALEXANDRIA.md). This entry gives it its
 * Lingua number and twin. */
#include "zl_int.h"

int zl_axion_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the AXION front-end is not built yet");
}
