/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_algol.c — the ALGOL 60 (twin Rootwyrm) front-end: placeholder until it is built. */
#include "zl_int.h"

int zl_algol_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the ALGOL 60 front-end is not built yet");
}
