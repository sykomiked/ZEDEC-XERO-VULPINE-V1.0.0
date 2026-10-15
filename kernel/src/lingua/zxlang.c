/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxlang.c — command-line front door to Lingua (hosted builds).
 *
 *   zxlang run   <file> [input ...]   compile and run; prints the output
 *   zxlang twin  <file>               print the canonical twin
 *   zxlang check <file>               compile; check the twin round trip
 *   zxlang ir    <file>               list the IR
 *   zxlang pack  <file>               print the precompiled program (zlir)
 *
 * The language comes from the extension (zl_langs.def lists them all,
 * e.g. .f for FORTRAN, .bas for BASIC, .fs for Forth); a .zlir file is a
 * precompiled program and takes run, ir and pack. Add "--twin" before the file to accept
 * only the twin spelling. Exit status: 0 ok, 1 compile refusal, 2 run-time
 * trap, 3 usage or I/O error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zl.h"

#define ZLIR (-1) /* a precompiled program, not a source language */

static zl_work_t work;
static zl_prog_t prog, prog2;
static zl_run_t run;
static char src[1 << 20], out[1 << 20], twin[1 << 20];

/* The language whose extension list (zl_langs.def) holds path's extension. */
static int lang_of(const char *path)
{
    static const struct {
        int lang;
        const char *ext;
    } langs[] = {
#define ZL_LANG_X(id, n, name, twin, fe, ext) {ZL_LANG_##id, ext},
#include "zl_langs.def"
#undef ZL_LANG_X
    };
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    if (!strcmp(dot, ".zlir")) return ZLIR;
    size_t n = strlen(dot);
    for (size_t i = 0; i < sizeof langs / sizeof langs[0]; i++)
        for (const char *e = langs[i].ext; *e;) {
            size_t k = strcspn(e, " ");
            if (k == n && !strncmp(e, dot, n)) return langs[i].lang;
            e += k + (e[k] == ' ');
        }
    return 0;
}

static int usage(void)
{
    fprintf(stderr, "usage: zxlang run|twin|check|ir|pack [--twin] "
                    "<file.f|.bas|.fs|... (see zl_langs.def)|.zlir> [input ...]\n");
    return 3;
}

int main(int argc, char **argv)
{
    zl_diag_t d;
    int a = 2, twin_only = 0;
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    if (!strcmp(argv[a], "--twin")) {
        twin_only = 1;
        a++;
    }
    if (a >= argc) return usage();
    const char *path = argv[a++];
    int lang = lang_of(path);
    if (!lang) {
        fprintf(stderr, "zxlang: unknown extension: %s\n", path);
        return 3;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 3;
    }
    size_t n = fread(src, 1, sizeof src, f);
    int big = !feof(f);
    fclose(f);
    if (big) {
        fprintf(stderr, "zxlang: %s is larger than 1 MB\n", path);
        return 3;
    }

    int s = lang == ZLIR ? zl_unpack(src, n, &prog, &d)
                         : zl_compile(lang | (twin_only ? ZL_TWIN : 0), src, n, &prog, &work, &d);
    if (s != ZL_OK) {
        fprintf(stderr, "%s:%u:%u: %s: %s\n", path, d.line, d.col, zl_status_name(s), d.msg);
        return 1;
    }
    if (!strcmp(cmd, "pack")) {
        size_t tl;
        if (zl_pack(&prog, twin, sizeof twin, &tl) != ZL_OK) {
            fprintf(stderr, "%s: pack: program too large\n", path);
            return 1;
        }
        fwrite(twin, 1, tl, stdout);
        return 0;
    }
    if ((!strcmp(cmd, "twin") || !strcmp(cmd, "check")) && lang == ZLIR) {
        fprintf(stderr, "%s: a precompiled program has no source to twin\n", path);
        return 3;
    }
    if (!strcmp(cmd, "twin") || !strcmp(cmd, "check")) {
        size_t tl;
        s = zl_twin(lang, src, n, twin, sizeof twin, &tl, &work, &d);
        if (s != ZL_OK) {
            fprintf(stderr, "%s: twin: %s\n", path, d.msg);
            return 1;
        }
        if (!strcmp(cmd, "twin")) {
            fwrite(twin, 1, tl, stdout);
            return 0;
        }
        s = zl_compile(lang | ZL_TWIN, twin, tl, &prog2, &work, &d);
        if (s != ZL_OK || !zl_prog_equal(&prog, &prog2)) {
            fprintf(stderr, "%s: twin round trip failed: %s\n", path, s ? d.msg : "IR differs");
            return 1;
        }
        printf("%s: %s, %u IR instructions, %u slots, digest %016llx; twin %s ok\n", path,
               zl_lang_name(lang), prog.ncode, prog.nslots,
               (unsigned long long) zl_prog_digest(&prog), zl_twin_name(lang));
        return 0;
    }
    if (!strcmp(cmd, "ir")) {
        for (uint32_t i = 0; i < prog.ncode; i++)
            printf("%5u  %-6s %lld %d   ; line %u\n", i, zl_op_name(prog.code[i].op),
                   (long long) prog.code[i].a, prog.code[i].b, prog.code[i].line);
        return 0;
    }
    if (strcmp(cmd, "run")) return usage();
    static int64_t in[1024];
    size_t nin = 0;
    for (; a < argc && nin < 1024; a++) in[nin++] = strtoll(argv[a], 0, 10);
    run.in = in;
    run.nin = nin;
    run.preload = prog.lang == ZL_LANG_FORTH; /* Forth takes inputs on the stack */
    run.out = out;
    run.out_cap = sizeof out;
    run.fuel = 100000000ull;
    s = zl_run(&prog, &run);
    fwrite(out, 1, run.out_len, stdout);
    if (s != ZL_OK) {
        fflush(stdout);
        fprintf(stderr, "%s:%u: stopped: %s\n", path, run.trap_line, zl_status_name(s));
        return 2;
    }
    return 0;
}
