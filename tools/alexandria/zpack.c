/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zpack — store booklets as alx_z does, for pack_matrix.py. Reads records
 * on standard input (a 4-byte big-endian length, then that many bytes of a
 * booklet) and answers each with a record of the stored block: an ALXZ
 * block, or the booklet itself when compressing would not shrink it. */
#include <stdio.h>
#include <stdlib.h>
#include "alx_z.h"

static uint8_t in[ALX_Z_MAXPLAIN], out[ALX_Z_MAXPLAIN + ALX_Z_HEAD];
static int32_t work[ALX_Z_WORK];

int main(void)
{
    uint8_t h[4];
    while (fread(h, 1, 4, stdin) == 4) {
        uint32_t n = (uint32_t) h[0] << 24 | (uint32_t) h[1] << 16 | (uint32_t) h[2] << 8 | h[3];
        if (n > sizeof in || fread(in, 1, n, stdin) != n) {
            fprintf(stderr, "zpack: bad record\n");
            return 1;
        }
        uint32_t m = alx_z_store(in, n, out, sizeof out, work);
        if (!m && n) {
            fprintf(stderr, "zpack: cannot store\n");
            return 1;
        }
        h[0] = (uint8_t) (m >> 24), h[1] = (uint8_t) (m >> 16), h[2] = (uint8_t) (m >> 8), h[3] = (uint8_t) m;
        fwrite(h, 1, 4, stdout);
        fwrite(out, 1, m, stdout);
        fflush(stdout);
    }
    return 0;
}
