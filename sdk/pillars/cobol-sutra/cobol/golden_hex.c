/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* golden_hex.c — prints one record as "kind hex..." for GOLDEN.cbl. It
 * does not use libzsutra: the golden records come from GnuCOBOL alone. */
#include <stdio.h>

int golden_hex(const char *kind, const unsigned char *rec, const int *len);

int golden_hex(const char *kind, const unsigned char *rec, const int *len) {
    int k = 0;
    while (k < 12 && kind[k] != ' ') k++;
    printf("%.*s", k, kind);
    for (int i = 0; i < *len; i++) printf(" %02x", rec[i]);
    printf("\n");
    return 0;
}
