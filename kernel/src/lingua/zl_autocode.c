/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_autocode.c — the Autocode front-end (twin Emberwyrm): a placeholder until it
 * is built (LINGUA_SPEC.md). */
#include "zl_int.h"

int zl_autocode_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the Autocode front-end is not built yet");
}
