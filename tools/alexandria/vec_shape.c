/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vec_shape DIM — the shape embedding alx_vec gives each file, for
 * pack_matrix.py --vectorize. Reads paths on standard input and writes
 * "PATH<TAB>SCALE<TAB>ROW" (the row's dim / 32 Q8_0 blocks in hex). The
 * first line written is "model<TAB>CID-HEX". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_vec.h"

static unsigned char buf[64u << 20];

int main(int argc, char **argv)
{
    unsigned dim = argc == 2 ? (unsigned) atoi(argv[1]) : 0;
    if (dim < 32 || dim > ALX_VEC_MAXDIM || (dim & (dim - 1))) {
        fprintf(stderr, "usage: vec_shape DIM (a power of two, 32..4096)\n");
        return 2;
    }
    static uint8_t row[ALX_VEC_MAXDIM / 32 * ALX_VEC_BLOCK];
    static int32_t work[ALX_VEC_MAXDIM + ALX_VEC_SEEN];
    uint8_t cid[36];
    alx_vec_shape_model(dim, cid);
    printf("model\t");
    for (int i = 0; i < 36; i++) printf("%02x", cid[i]);
    putchar('\n');
    char line[4200];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        FILE *f = fopen(line, "rb");
        if (!f) {
            fprintf(stderr, "vec_shape: cannot read %s\n", line);
            return 1;
        }
        size_t len = fread(buf, 1, sizeof buf, f);
        fclose(f);
        uint32_t sc = alx_vec_shape(buf, (uint32_t) len, dim, row, work);
        printf("%s\t%u\t", line, sc);
        for (unsigned i = 0; i < dim / 32 * ALX_VEC_BLOCK; i++) printf("%02x", row[i]);
        putchar('\n');
    }
    return 0;
}
