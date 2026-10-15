/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxlang.c — command-line front door to Lingua (hosted builds).
 *
 *   zxlang run   <file> [input ...]   compile and run; prints the output
 *   zxlang twin  <file>               print the canonical twin
 *   zxlang check <file>               compile; check the twin round trip
 *   zxlang ir    <file>               list the IR
 *
 * The language comes from the extension: .f .for .f77 (FORTRAN), .bas
 * (BASIC), .fs .fth .4th (Forth). Add "--twin" before the file to accept
 * only the twin spelling. Exit status: 0 ok, 1 compile refusal, 2 run-time
 * trap, 3 usage or I/O error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zl.h"

static zl_work_t work;
static zl_prog_t prog, prog2;
static zl_run_t run;
static char src[1 << 20], out[1 << 20], twin[1 << 20];

static int lang_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    if (!strcmp(dot, ".f") || !strcmp(dot, ".for") || !strcmp(dot, ".f77") || !strcmp(dot, ".F"))
        return ZL_LANG_FORTRAN;
    if (!strcmp(dot, ".bas") || !strcmp(dot, ".BAS")) return ZL_LANG_BASIC;
    if (!strcmp(dot, ".fs") || !strcmp(dot, ".fth") || !strcmp(dot, ".4th")) return ZL_LANG_FORTH;
    return 0;
}

static const char *op_name(int op)
{
    static const char *const n[] = {
        "HALT", "PUSH", "LOAD", "STORE", "LOADX", "STOREX", "ADD", "SUB",   "MUL",    "DIVT",
        "MODT", "DIVF", "MODF", "DIVX",  "POW",   "NEG",    "ABS", "SGN",   "MIN",    "MAX",
        "EQ",   "NE",   "LT",   "LE",    "GT",    "GE",     "AND", "OR",    "XOR",    "INVERT",
        "NOT",  "DUP",  "DROP", "SWAP",  "OVER",  "ROT",    "JMP", "JZ",    "JNZ",    "CALL",
        "RET",  "OUTI", "OUTS", "OUTC",  "NL",    "TAB",    "IN",  "LOADA", "STOREA", "TRAP"};
    return op >= 0 && op < (int) (sizeof n / sizeof n[0]) ? n[op] : "?";
}

static int usage(void)
{
    fprintf(stderr, "usage: zxlang run|twin|check|ir [--twin] <file.f|.bas|.fs> [input ...]\n");
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

    int s = zl_compile(lang | (twin_only ? ZL_TWIN : 0), src, n, &prog, &work, &d);
    if (s != ZL_OK) {
        fprintf(stderr, "%s:%u:%u: %s: %s\n", path, d.line, d.col, zl_status_name(s), d.msg);
        return 1;
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
            printf("%5u  %-6s %lld %d   ; line %u\n", i, op_name(prog.code[i].op),
                   (long long) prog.code[i].a, prog.code[i].b, prog.code[i].line);
        return 0;
    }
    if (strcmp(cmd, "run")) return usage();
    static int64_t in[1024];
    size_t nin = 0;
    for (; a < argc && nin < 1024; a++) in[nin++] = strtoll(argv[a], 0, 10);
    run.in = in;
    run.nin = nin;
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
