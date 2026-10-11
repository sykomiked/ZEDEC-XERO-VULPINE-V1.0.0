/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_tool.c — check a contract file from the command line.
 *
 *   zc-tool check FILE    parse; print the canonical text, or the error as
 *                         FILE:LINE:COL: message, the line and a caret
 *   zc-tool digest FILE   the SHA3-256 of the canonical text (hex)
 *
 * Exit status: 0 accepted, 1 refused, 2 usage or I/O error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcontract.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *b = NULL;
    size_t cap = 0, n = 0;
    if (!f) return NULL;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2u : 4096u;
            if (cap > (size_t) 64 * 1024 * 1024) break;
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

int main(int argc, char **argv) {
    size_t len = 0;
    char *text;
    zc_contract *c = NULL;
    uint32_t line = 0, col = 0;
    uint64_t off = 0;
    char msg[400];
    if (argc != 3 || (strcmp(argv[1], "check") != 0 && strcmp(argv[1], "digest") != 0)) {
        fprintf(stderr, "usage: zc-tool check|digest FILE\n");
        return 2;
    }
    text = slurp(argv[2], &len);
    if (!text) {
        fprintf(stderr, "zc-tool: cannot read %s\n", argv[2]);
        return 2;
    }
    int32_t st = zc_parse(text, len, &c, &line, &col, &off, msg, sizeof msg, NULL);
    if (st != ZXV_OK) {
        /* msg is "line L, column C: ..."; print it compiler-style */
        const char *m = strstr(msg, ": ");
        fprintf(stderr, "%s:%u:%u: %s\n", argv[2], (unsigned) line, (unsigned) col, m ? m + 2 : msg);
        if (st == ZC_E_SYNTAX && off <= len) {
            size_t a = (size_t) off, e = (size_t) off;
            while (a > 0 && text[a - 1] != '\n') a--;
            while (e < len && text[e] != '\n') e++;
            fprintf(stderr, "  %.*s\n  ", (int) (e - a), text + a);
            for (size_t k = a; k < (size_t) off; k++) fputc(text[k] == '\t' ? '\t' : ' ', stderr);
            fprintf(stderr, "^\n");
        }
        free(text);
        return st == ZC_E_SYNTAX ? 1 : 2;
    }
    if (strcmp(argv[1], "check") == 0) {
        size_t need = 0;
        (void) zc_contract_format(c, NULL, 0, &need);
        char *out = (char *) malloc(need + 1u);
        if (out && zc_contract_format(c, out, need + 1u, &need) == ZXV_OK) fputs(out, stdout);
        free(out);
    } else {
        uint8_t d[32];
        if (zc_contract_digest(c, d) != ZXV_OK) return 2;
        for (int i = 0; i < 32; i++) printf("%02x", d[i]);
        printf("  %s\n", argv[2]);
    }
    zc_contract_destroy(c);
    free(text);
    return 0;
}
