/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_lisp.c — the LISP (twin Coilwyrm) and Scheme (twin Spiraldrake) front-end: placeholder until
 * it is built. */
#include "zl_int.h"

int zl_lisp_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    (void) src;
    (void) len;
    (void) tw;
    (void) work;
    return zl_fail(c, ZL_E_UNSUPPORTED, "the LISP and Scheme front-end is not built yet");
}
