/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_racket.c — the Racket front-end (twin Dialectwyrm): a dialect of the
 * s-expression front-end in zl_lisp.c, which reads c->p->lang. */
#include "zl_int.h"

int zl_racket_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work)
{
    return zl_lisp_compile(src, len, c, tw, work);
}
