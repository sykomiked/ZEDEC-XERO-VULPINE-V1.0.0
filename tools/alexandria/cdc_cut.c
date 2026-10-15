/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cdc_cut — the chunk lengths alx_cdc gives each file, for pack_matrix.py.
 * Reads "TIER PATH" lines on standard input and writes "PATH<TAB>len,len,..."
 * (an empty file gives no lengths). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_cdc.h"

static unsigned char buf[64u << 20];

int main(void)
{
    char line[4200];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (n < 3 || line[1] != ' ' || line[0] < '0' || line[0] > '2') {
            fprintf(stderr, "cdc_cut: bad line: %s\n", line);
            return 1;
        }
        FILE *f = fopen(line + 2, "rb");
        if (!f) {
            fprintf(stderr, "cdc_cut: cannot read %s\n", line + 2);
            return 1;
        }
        size_t len = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (len == sizeof buf) {
            fprintf(stderr, "cdc_cut: %s is over 64 MiB\n", line + 2);
            return 1;
        }
        printf("%s\t", line + 2);
        for (size_t at = 0; at < len;) {
            uint32_t c = alx_cdc_cut(buf + at, (uint32_t) (len - at), (uint32_t) (line[0] - '0'));
            printf(at ? ",%u" : "%u", c);
            at += c;
        }
        putchar('\n');
    }
    return 0;
}
