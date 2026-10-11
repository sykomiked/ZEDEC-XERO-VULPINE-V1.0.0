/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* autoinit.c — linked into libzxv's own conformance tests when they run
 * against the embedded build: opens the port with the host CSPRNG before
 * main(), as a firmware's start-up code would with its board TRNG. */
#include <stdio.h>
#include <stdlib.h>

#include "zxp.h"
#include "zxp_posix.h"

__attribute__((constructor)) static void zxe_autoinit(void)
{
    int rc = zxp_set_entropy(zxp_posix_entropy, NULL);
    if (rc == ZXP_OK) rc = zxp_init();
    if (rc != ZXP_OK) {
        fprintf(stderr, "autoinit: zxp_init failed: %s\n", zxp_status_name(rc));
        exit(70);
    }
}
