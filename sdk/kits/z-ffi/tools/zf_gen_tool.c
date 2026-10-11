/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zf_gen_tool.c — zf-gen: generate typed wrappers from a z-ffi spec.
 *
 *   zf-gen SPEC.zf OUT.h OUT.c
 *
 * OUT.c includes OUT.h by its file name. On a spec error: the position,
 * the message and the offending line with a caret, exit status 1. Usage or
 * I/O errors: exit status 2. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zffi.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    size_t cap = 0, n = 0;
    if (!f) return NULL;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2u : 4096u;
            if (cap > (size_t) 2 * ZF_MAX_SPEC) break;
            char *nb = (char *) realloc(b, cap);
            if (!nb) break;
            b = nb;
        }
        size_t r = fread(b + n, 1, cap - n, f);
        n += r;
        if (r == 0) break;
    }
    fclose(f);
    *len = n;
    return b ? b : (char *) calloc(1, 1);
}

static int write_file(const char *path, const char *text, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(text, 1, len, f);
    return (fclose(f) == 0 && w == len) ? 0 : -1;
}

int main(int argc, char **argv) {
    size_t len = 0, hl = 0, cl = 0;
    char *spec, *h = NULL, *c = NULL, msg[400];
    uint32_t line = 0, col = 0;
    uint64_t off = 0;
    const char *base;
    if (argc != 4) {
        fprintf(stderr, "usage: zf-gen SPEC.zf OUT.h OUT.c\n");
        return 2;
    }
    spec = slurp(argv[1], &len);
    if (!spec) {
        fprintf(stderr, "zf-gen: cannot read %s\n", argv[1]);
        return 2;
    }
    base = strrchr(argv[2], '/');
    base = base ? base + 1 : argv[2];
    int32_t st = zf_gen(spec, len, base, &h, &hl, &c, &cl, &line, &col, &off, msg, sizeof msg);
    if (st == ZF_E_SPEC) {
        size_t ls = (size_t) off, le = (size_t) off;
        while (ls > 0 && spec[ls - 1u] != '\n') ls--;
        while (le < len && spec[le] != '\n') le++;
        fprintf(stderr, "%s:%u:%u: %s\n  %.*s\n  %*s^\n", argv[1], (unsigned) line, (unsigned) col,
                strchr(msg, ':') ? strchr(msg, ':') + 2 : msg, (int) (le - ls), spec + ls,
                (int) (off - ls), "");
        free(spec);
        return 1;
    }
    if (st != ZXV_OK) {
        fprintf(stderr, "zf-gen: %s\n", zf_status_name(st));
        free(spec);
        return 2;
    }
    if (write_file(argv[2], h, hl) != 0 || write_file(argv[3], c, cl) != 0) {
        fprintf(stderr, "zf-gen: cannot write the output files\n");
        return 2;
    }
    zf_gen_free(h);
    zf_gen_free(c);
    free(spec);
    return 0;
}
