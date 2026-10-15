/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cdc_cut — the chunk lengths alx_cdc gives each file, for pack_matrix.py.
 * Reads "TIER PATH" lines on standard input and writes "PATH<TAB>len,len,..."
 * (an empty file gives no lengths). TIER is 0, 1 or 2 (hot, warm, cold), or
 * MIN:AVG:MAX for any other sizes (alx_cdc_make), for example the Fibonacci
 * tier 987:4181:17711. */
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
        char *path = strchr(line, ' ');
        alx_cdc_tier_t t;
        unsigned long mn, av, mx;
        int ok = path != 0;
        if (ok) {
            *path++ = 0;
            if (line[0] >= '0' && line[0] <= '2' && !line[1])
                t = *alx_cdc_tier((uint32_t) (line[0] - '0'));
            else
                ok = sscanf(line, "%lu:%lu:%lu", &mn, &av, &mx) == 3 && mx <= ALX_CDC_MAXLEN &&
                     alx_cdc_make(&t, (uint32_t) mn, (uint32_t) av, (uint32_t) mx) == 0;
        }
        if (!ok || !*path) {
            fprintf(stderr, "cdc_cut: bad line: %s\n", line);
            return 1;
        }
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "cdc_cut: cannot read %s\n", path);
            return 1;
        }
        size_t len = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (len == sizeof buf) {
            fprintf(stderr, "cdc_cut: %s is over 64 MiB\n", path);
            return 1;
        }
        printf("%s\t", path);
        for (size_t at = 0; at < len;) {
            uint32_t c = alx_cdc_cut_with(buf + at, (uint32_t) (len - at), &t);
            printf(at ? ",%u" : "%u", c);
            at += c;
        }
        putchar('\n');
    }
    return 0;
}
