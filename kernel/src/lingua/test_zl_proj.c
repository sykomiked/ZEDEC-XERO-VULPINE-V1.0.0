/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zl_proj.c — the IR projections (zl_proj.c) against zl_run.
 *
 * Usage: test_zl_proj <golden-dir> <out-dir> <random programs> <seed>
 *
 * The corpus is every golden program, compiled by every front-end that
 * accepts it, and random IR programs (half of them in the Forth subset).
 * Each program runs under zl_run. Then:
 *   Forth: the projection compiles with Lingua's Forth front-end and runs;
 *     its output must equal the original's, and it must end exactly when
 *     the original ends. Projections of programs that end are also written
 *     to <out-dir>/forth/NNNN.fs with NNNN.out, for gforth (proj_gforth.sh).
 *   C and x86-64: every projection goes into <out-dir>/cases_c.c and
 *     cases_x86.s, with the expected results in cases_data.c; the driver
 *     (zxp_driver.c) links them with zxp_rt.c and checks output, fuel,
 *     status and trapping instruction exactly.
 * It also checks what the projector refuses, and the alphabet table
 * (alphabet.tsv) against the IR: one row per instruction, in order, each
 * row's stack effect confirmed by running the instruction. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zl.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zl_work_t work;
static zl_prog_t P, Q;
static zl_run_t R1, R2;
static char out1[4096], out2[4096], text[1 << 21];
static FILE *fc, *fx, *fd;
static const char *outdir;
static unsigned ncase, nforth, nforth_skip, ngforth;
static uint64_t rng;

static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) (rng % n);
}

static void cstr(FILE *f, const char *s, size_t n)
{
    fputc('"', f);
    for (size_t i = 0; i < n; i++) fprintf(f, "\\%03o", (unsigned char) s[i]);
    fputc('"', f);
}

static int run(const zl_prog_t *p, zl_run_t *r, char *out, size_t cap, const int64_t *in,
               size_t nin, uint64_t fuel)
{
    memset(r, 0, offsetof(zl_run_t, mem));
    r->in = in;
    r->nin = nin;
    r->out = out;
    r->out_cap = cap;
    r->fuel = fuel;
    return zl_run(p, r);
}

/* one program through every target that takes it */
static void project_all(const zl_prog_t *p, const int64_t *in, size_t nin, uint64_t fuel,
                        size_t cap, const char *what)
{
    size_t n;
    zl_diag_t d;
    int s1 = run(p, &R1, out1, cap, in, nin, fuel);
    int machine = 1, forth = 1;
    for (uint32_t k = 0; k < p->ncode; k++) {
        machine &= zl_project_op(ZL_PROJ_C, p->code[k].op);
        forth &= zl_project_op(ZL_PROJ_FORTH, p->code[k].op);
    }
    if (p->word_bits == 62) machine = forth = 0;

    if (machine) {
        char name[32];
        snprintf(name, sizeof name, "zc_%u", ncase);
        int s = zl_project(ZL_PROJ_C, p, name, text, sizeof text, &n, &d);
        CHECK(s == ZL_OK && n == strlen(text), "%s: C projection %d %s", what, s, d.msg);
        fwrite(text, 1, n, fc);
        snprintf(name, sizeof name, "zx_%u", ncase);
        s = zl_project(ZL_PROJ_X86_64, p, name, text, sizeof text, &n, &d);
        CHECK(s == ZL_OK, "%s: x86-64 projection %d %s", what, s, d.msg);
        fwrite(text, 1, n, fx);
        fprintf(fd, "{zc_%u, zx_%u, %llu, %zu, %d, %llu, %u, %zu, ", ncase, ncase,
                (unsigned long long) fuel, cap, s1, (unsigned long long) R1.used, R1.trap_pc, nin);
        fprintf(fd, "{");
        for (size_t i = 0; i < nin; i++)
            fprintf(fd, in[i] == INT64_MIN ? "INT64_MIN," : "%lldLL,", (long long) in[i]);
        fprintf(fd, "0}, %zu, ", R1.out_len);
        cstr(fd, out1, R1.out_len);
        fprintf(fd, ", \"%s\"},\n", what);
        ncase++;
    } else {
        CHECK(zl_project(ZL_PROJ_C, p, "f", text, sizeof text, &n, &d) == ZL_E_UNSUPPORTED &&
                  n == 0 && text[0] == 0,
              "%s: C refuses it", what);
    }

    if (!forth) return;
    int s = zl_project(ZL_PROJ_FORTH, p, 0, text, sizeof text, &n, &d);
    CHECK(s == ZL_OK, "%s: Forth projection %d", what, s);
    if (s1 == ZL_E_FUEL || nin) { /* Forth spends its own fuel; inputs are not in the subset */
        nforth_skip++;
        return;
    }
    s = zl_compile(ZL_LANG_FORTH, text, n, &Q, &work, &d);
    if (s == ZL_E_LIMIT) { /* the projection is bigger than one Forth program may be */
        nforth_skip++;
        return;
    }
    CHECK(s == ZL_OK, "%s: projected Forth compiles: %d %s line %u\n%s", what, s, d.msg, d.line,
          text);
    if (s) return;
    int s2 = run(&Q, &R2, out2, cap, 0, 0, 200000000ull);
    CHECK((s1 == ZL_OK) == (s2 == ZL_OK) && R1.out_len == R2.out_len &&
              !memcmp(out1, out2, R1.out_len),
          "%s: Forth ends %d (want %d), printed [%.*s] want [%.*s]", what, s2, s1, (int) R2.out_len,
          out2, (int) R1.out_len, out1);
    nforth++;
    if (s1 == ZL_OK && ngforth < 400) {
        char path[1100];
        snprintf(path, sizeof path, "%s/forth/%04u.fs", outdir, ngforth);
        FILE *f = fopen(path, "wb");
        fwrite(text, 1, n, f);
        fclose(f);
        snprintf(path, sizeof path, "%s/forth/%04u.out", outdir, ngforth);
        f = fopen(path, "wb");
        fwrite(out1, 1, R1.out_len, f);
        fclose(f);
        ngforth++;
    }
}

/* ---- the corpus ---- */

static void goldens(const char *dir)
{
    DIR *dp = opendir(dir);
    CHECK(dp != 0, "golden directory %s", dir);
    if (!dp) return;
    static char src[1 << 16];
    unsigned used = 0;
    for (struct dirent *e; (e = readdir(dp)) != 0;) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || (l > 4 && !strcmp(e->d_name + l - 4, ".out"))) continue;
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(src, 1, sizeof src - 1, f);
        fclose(f);
        for (int lang = 1; lang < ZL_LANG__COUNT; lang++) {
            zl_diag_t d;
            if (zl_compile(lang, src, n, &P, &work, &d) != ZL_OK) continue;
            char what[300];
            snprintf(what, sizeof what, "%s in language %d", e->d_name, lang);
            project_all(&P, 0, 0, 0, sizeof out1, what);
            used++;
        }
    }
    closedir(dp);
    CHECK(used >= 10, "goldens compiled (%u)", used);
}

static const int CORE[] = {
    ZL_HALT,   ZL_PUSH, ZL_PUSH, ZL_PUSH, ZL_LOAD, ZL_STORE,  ZL_LOADX, ZL_STOREX, ZL_LOADA,
    ZL_STOREA, ZL_ADD,  ZL_SUB,  ZL_MUL,  ZL_DIVT, ZL_MODT,   ZL_DIVF,  ZL_MODF,   ZL_DIVX,
    ZL_NEG,    ZL_ABS,  ZL_SGN,  ZL_MIN,  ZL_MAX,  ZL_EQ,     ZL_NE,    ZL_LT,     ZL_LE,
    ZL_GT,     ZL_GE,   ZL_AND,  ZL_OR,   ZL_XOR,  ZL_INVERT, ZL_NOT,   ZL_DUP,    ZL_DROP,
    ZL_SWAP,   ZL_OVER, ZL_ROT,  ZL_JMP,  ZL_JZ,   ZL_JNZ,    ZL_CALL,  ZL_RET,    ZL_OUTI,
    ZL_OUTI,   ZL_OUTS, ZL_OUTC, ZL_NL,   ZL_TRAP};
static const int MORE[] = {ZL_POW, ZL_TAB, ZL_IN,   ZL_INEOF, ZL_RANGE,
                           ZL_SHL, ZL_SHR, ZL_WRAP, ZL_OUTD,  ZL_IN};

static int64_t value(uint32_t bits)
{
    static const int64_t edge[] = {0,  1, -1, 2,  10, -7, 255, 256, 2147483647, -2147483648LL,
                                   65, 9, 3,  -3, 100};
    switch (rnd(8)) {
    case 0:
        return edge[rnd(sizeof edge / sizeof edge[0])];
    case 1:
        if (bits == 64) {
            static const int64_t big[] = {INT64_MAX,     INT64_MIN,         INT64_MAX - 1,
                                          INT64_MIN + 1, (int64_t) 1 << 40, -((int64_t) 1 << 33)};
            return big[rnd(6)];
        }
        return (int64_t) rnd(4000) - 2000;
    default:
        return (int64_t) rnd(21) - 10;
    }
}

/* the cells an instruction pops, as far as the generator keeps count */
static int needs(int op)
{
    switch (op) {
    case ZL_ROT:
        return 3;
    case ZL_STOREX:
    case ZL_STOREA:
    case ZL_SWAP:
    case ZL_OVER:
    case ZL_POW:
    case ZL_SHL:
    case ZL_SHR:
        return 2;
    case ZL_HALT:
    case ZL_PUSH:
    case ZL_LOAD:
    case ZL_JMP:
    case ZL_CALL:
    case ZL_RET:
    case ZL_OUTS:
    case ZL_NL:
    case ZL_TAB:
    case ZL_IN:
    case ZL_INEOF:
    case ZL_TRAP:
        return 0;
    default:
        return op >= ZL_ADD && op <= ZL_XOR && op != ZL_NEG && op != ZL_ABS && op != ZL_SGN ? 2 : 1;
    }
}

static void random_program(int core_only)
{
    memset(&P, 0, sizeof P);
    P.lang = ZL_LANG_FORTH;
    P.word_bits = rnd(2) ? 64 : 32;
    P.nslots = 1 + rnd(8);
    static const char *const strs[] = {"HI", " a b ", "Q\"\\?\n", "\xff\x01z", "?\?=", "x"};
    P.nstr = 6;
    for (uint32_t i = 0; i < P.nstr; i++) {
        P.str_off[i] = P.strbytes;
        P.str_len[i] = (uint32_t) strlen(strs[i]);
        memcpy(P.str + P.strbytes, strs[i], P.str_len[i]);
        P.strbytes += P.str_len[i];
    }
    P.ncode = 2 + rnd(40);
    int depth = 0;
    for (uint32_t k = 0; k < P.ncode; k++) {
        zl_ins_t *in = &P.code[k];
        int op;
        if (!core_only && rnd(5) == 0)
            op = MORE[rnd(sizeof MORE / sizeof MORE[0])];
        else
            op = CORE[rnd(sizeof CORE / sizeof CORE[0])];
        if (needs(op) > depth && rnd(8)) op = ZL_PUSH; /* keep the stack fed */
        if (k + 1 == P.ncode && op != ZL_JMP && op != ZL_RET && op != ZL_TRAP) op = ZL_HALT;
        in->op = (uint8_t) op;
        in->line = k + 1;
        switch (op) {
        case ZL_PUSH:
            in->a = value(P.word_bits);
            if (in->a < 6 && in->a > -6 && rnd(2)) in->a = rnd(P.nslots + 2); /* addresses */
            depth++;
            break;
        case ZL_LOAD:
        case ZL_STORE:
            in->a = rnd(P.nslots);
            depth += op == ZL_LOAD ? 1 : -1;
            break;
        case ZL_LOADX:
        case ZL_STOREX:
            in->b = 1 + (int32_t) rnd(P.nslots);
            in->a = rnd(P.nslots - (uint32_t) in->b + 1);
            depth -= op == ZL_STOREX ? 2 : 0;
            break;
        case ZL_JMP:
        case ZL_JZ:
        case ZL_JNZ:
        case ZL_CALL:
            in->a = rnd(P.ncode);
            break;
        case ZL_OUTI:
            in->a = rnd(4);
            in->b = (int32_t) rnd(14);
            depth--;
            break;
        case ZL_OUTS:
            in->a = rnd(P.nstr);
            break;
        case ZL_TAB:
            in->a = 1 + rnd(9);
            break;
        case ZL_TRAP:
            in->a = rnd(2) ? ZL_E_DOMAIN : -2650;
            break;
        case ZL_POW:
            in->a = rnd(2);
            depth--;
            break;
        case ZL_RANGE:
            in->a = 1 + rnd(200);
            in->b = (int32_t) rnd(2);
            break;
        case ZL_WRAP:
            in->a = 1 + rnd(63);
            in->b = (int32_t) rnd(2);
            break;
        case ZL_OUTD:
            in->a = (int64_t) (rnd(20) << 8 | rnd(19));
            in->b = (int32_t) rnd(4);
            depth--;
            break;
        case ZL_HALT:
        case ZL_NL:
        case ZL_RET:
            break;
        case ZL_DUP:
        case ZL_OVER:
        case ZL_IN:
        case ZL_INEOF:
            depth++;
            break;
        case ZL_NEG:
        case ZL_ABS:
        case ZL_SGN:
        case ZL_INVERT:
        case ZL_NOT:
        case ZL_SWAP:
        case ZL_ROT:
        case ZL_LOADA:
            break;
        default: /* pops one more than it pushes */
            depth--;
            break;
        }
        if (depth < 0) depth = 0;
    }
    zl_diag_t d;
    CHECK(zl_verify(&P, &d) == ZL_OK, "random program verifies: %s", d.msg);
}

static void randoms(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        int core_only = (int) (i & 1);
        random_program(core_only);
        int64_t in[3];
        size_t nin = core_only ? 0 : rnd(4);
        for (size_t j = 0; j < nin; j++) in[j] = value(P.word_bits);
        char what[64];
        snprintf(what, sizeof what, "random %u", i);
        project_all(&P, in, nin, 300 + rnd(400), 8 + rnd(250), what);
    }
}

/* ---- what the projector refuses ---- */

static void refusals(void)
{
    size_t n = 7;
    zl_diag_t d;
    memset(&P, 0, sizeof P);
    P.lang = ZL_LANG_FORTH;
    P.word_bits = 64;
    P.ncode = 2;
    P.code[0].op = ZL_PUSH;
    P.code[0].a = 5;
    P.code[1].op = ZL_HALT;
    CHECK(zl_project(ZL_PROJ_C, &P, "ok_1", text, sizeof text, &n, &d) == ZL_OK && n > 0,
          "a two-instruction program");
    char small[64];
    CHECK(zl_project(ZL_PROJ_C, &P, "ok_1", small, sizeof small, &n, &d) == ZL_E_LIMIT && n == 0 &&
              small[0] == 0,
          "too small an output");
    CHECK(zl_project(ZL_PROJ_C, &P, "Ok", text, sizeof text, &n, &d) == ZL_E_ARG, "upper case");
    CHECK(zl_project(ZL_PROJ_C, &P, "9a", text, sizeof text, &n, &d) == ZL_E_ARG, "digit first");
    CHECK(zl_project(ZL_PROJ_C, &P, "", text, sizeof text, &n, &d) == ZL_E_ARG, "empty name");
    CHECK(zl_project(ZL_PROJ_X86_64, &P, 0, text, sizeof text, &n, &d) == ZL_E_ARG, "no name");
    CHECK(zl_project(ZL_PROJ_C, &P, "a234567890123456789012345678901234567890", text, sizeof text,
                     &n, &d) == ZL_OK,
          "40 bytes");
    CHECK(zl_project(ZL_PROJ_C, &P, "a2345678901234567890123456789012345678901", text, sizeof text,
                     &n, &d) == ZL_E_ARG,
          "41 bytes");
    CHECK(zl_project(ZL_PROJ_C, &P, "a-b", text, sizeof text, &n, &d) == ZL_E_ARG, "a dash");
    CHECK(zl_project(7, &P, "f", text, sizeof text, &n, &d) == ZL_E_ARG, "target 7");
    CHECK(zl_project(ZL_PROJ_C, 0, "f", text, sizeof text, &n, &d) == ZL_E_ARG, "no program");
    CHECK(zl_project(ZL_PROJ_C, &P, "f", 0, 9, &n, &d) == ZL_E_ARG, "no buffer");
    CHECK(zl_project(ZL_PROJ_C, &P, "f", text, 0, &n, &d) == ZL_E_ARG, "no room");
    CHECK(zl_project(ZL_PROJ_C, &P, "f", text, sizeof text, 0, &d) == ZL_E_ARG, "no length");
    CHECK(zl_project(ZL_PROJ_FORTH, &P, 0, text, sizeof text, &n, 0) == ZL_OK, "no diagnosis");
    P.word_bits = 62;
    CHECK(zl_project(ZL_PROJ_C, &P, "f", text, sizeof text, &n, &d) == ZL_E_UNSUPPORTED,
          "a list program");
    P.word_bits = 64;
    P.code[1].op = ZL_CONS;
    CHECK(zl_project(ZL_PROJ_C, &P, "f", text, sizeof text, &n, &d) == ZL_E_VERIFY,
          "CONS outside a list program does not verify");
    P.code[0].op = ZL_TAB;
    P.code[0].a = 8;
    P.code[1].op = ZL_HALT;
    CHECK(zl_project(ZL_PROJ_FORTH, &P, 0, text, sizeof text, &n, &d) == ZL_E_UNSUPPORTED &&
              d.line == 0,
          "TAB is not in the Forth subset");
    CHECK(zl_project(ZL_PROJ_X86_64, &P, "f", text, sizeof text, &n, &d) == ZL_OK,
          "TAB is in the machine subset");
    P.code[0].op = ZL_PUSH;
    P.code[0].a = 1;
    P.code[1].op = ZL_CALLI;
    P.word_bits = 62;
    CHECK(zl_project_op(ZL_PROJ_C, ZL_CALLI) == 0 && zl_project_op(ZL_PROJ_FORTH, ZL_POW) == 0 &&
              zl_project_op(ZL_PROJ_X86_64, ZL_POW) == 1 && zl_project_op(9, ZL_ADD) == 0,
          "the subsets");
    /* the same program always gives the same bytes */
    static char again[1 << 21];
    size_t n2;
    random_program(0);
    for (int t = 0; t < 3; t++) {
        int tg = t == 0 ? ZL_PROJ_C : t == 1 ? ZL_PROJ_X86_64 : ZL_PROJ_FORTH;
        int a = zl_project(tg, &P, "same", text, sizeof text, &n, &d);
        int b = zl_project(tg, &P, "same", again, sizeof again, &n2, &d);
        CHECK(a == b && n == n2 && !memcmp(text, again, n), "target %d is deterministic", tg);
    }
}

/* ---- the alphabet table ---- */

static void alphabet(const char *tsv)
{
    FILE *f = fopen(tsv, "r");
    CHECK(f != 0, "%s", tsv);
    if (!f) return;
    char line[512];
    int op = 0;
    static const char *const fam[] = {"permute", "compose", "resource", "recur",  "branch", "arith",
                                      "compare", "state",   "data",     "effect", "stop"};
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[32], family[32], pops[16], pushes[16], targets[32];
        int k = sscanf(line, "%31s %31s %15s %15s %31s", name, family, pops, pushes, targets);
        CHECK(k == 5, "alphabet line: %s", line);
        if (k != 5) break;
        CHECK(strcmp(name, zl_op_name(op)) == 0, "row %d is %s, not %s", op, name, zl_op_name(op));
        int ok = 0;
        for (unsigned i = 0; i < sizeof fam / sizeof fam[0]; i++) ok |= !strcmp(family, fam[i]);
        CHECK(ok, "%s: family %s", name, family);
        int c = strchr(targets, 'c') != 0, x = strchr(targets, 'x') != 0,
            fo = strchr(targets, 'f') != 0;
        CHECK(c == zl_project_op(ZL_PROJ_C, op) && x == zl_project_op(ZL_PROJ_X86_64, op) &&
                  fo == zl_project_op(ZL_PROJ_FORTH, op),
              "%s: targets %s", name, targets);
        /* a fixed stack effect is confirmed by running the instruction */
        if (pops[0] != '*' && pushes[0] != '*' && c) {
            int np = atoi(pops), nq = atoi(pushes);
            for (int extra = 0; extra < 2; extra++) {
                memset(&P, 0, sizeof P);
                P.lang = ZL_LANG_FORTH;
                P.word_bits = 64;
                P.nslots = 4;
                P.nstr = 1;
                P.str_len[0] = 1;
                P.str[0] = 'x';
                P.strbytes = 1;
                uint32_t pc = 0;
                for (int i = 0; i < np; i++) {
                    P.code[pc].op = ZL_PUSH;
                    P.code[pc++].a = 1;
                }
                P.code[pc].op = (uint8_t) op;
                P.code[pc].a = op == ZL_TAB     ? 8
                               : op == ZL_RANGE ? 5
                               : op == ZL_WRAP  ? 8
                               : op == ZL_TRAP  ? ZL_E_DOMAIN
                                                : 0;
                P.code[pc].b = op == ZL_LOADX || op == ZL_STOREX ? 2 : 0;
                if (op == ZL_JMP || op == ZL_JZ || op == ZL_JNZ || op == ZL_CALL)
                    P.code[pc].a = pc + 1;
                pc++;
                for (int i = 0; i < nq + extra; i++) P.code[pc++].op = ZL_DROP;
                P.code[pc++].op = ZL_HALT;
                if (op == ZL_RET) { /* RET needs a CALL to return to */
                    memmove(P.code + 1, P.code, sizeof(zl_ins_t) * pc);
                    P.code[0].op = ZL_CALL;
                    P.code[0].a = 2;
                    P.code[1].op = ZL_HALT;
                    pc++;
                }
                P.ncode = pc;
                int64_t in1 = 1;
                int s = run(&P, &R1, out1, sizeof out1, &in1, 1, 0);
                if (op == ZL_HALT || op == ZL_TRAP) break;
                CHECK(extra ? s == ZL_E_STACK : s == ZL_OK, "%s pops %d and pushes %d: %d", name,
                      np, nq, s);
            }
        }
        op++;
    }
    fclose(f);
    CHECK(op == ZL_OP__COUNT, "one alphabet row per instruction (%d of %d)", op, ZL_OP__COUNT);
}

int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    outdir = argv[2];
    rng = strtoull(argv[4], 0, 10) | 1;
    char path[1100];
    snprintf(path, sizeof path, "%s/cases_c.c", outdir);
    fc = fopen(path, "w");
    snprintf(path, sizeof path, "%s/cases_x86.s", outdir);
    fx = fopen(path, "w");
    snprintf(path, sizeof path, "%s/cases_data.c", outdir);
    fd = fopen(path, "w");
    if (!fc || !fx || !fd) return 2;
    fprintf(fc, "#include \"zxp_rt.h\"\n");
    fprintf(fd, "#include \"zxp_case.h\"\nconst zxp_case_t zxp_cases[] = {\n");
    refusals();
    alphabet(argv[5]);
    goldens(argv[1]);
    randoms((unsigned) strtoul(argv[3], 0, 10));
    fprintf(fx, "\t.section .note.GNU-stack,\"\",@progbits\n");
    fprintf(fd, "};\nconst unsigned zxp_ncases = %u;\n", ncase);
    /* the driver's declarations of every projected function */
    snprintf(path, sizeof path, "%s/cases_fn.h", outdir);
    FILE *fh = fopen(path, "w");
    for (unsigned i = 0; i < ncase; i++)
        fprintf(fh, "int zc_%u(zxp_io_t *);\nint zx_%u(zxp_io_t *);\n", i, i);
    fclose(fh);
    fclose(fc);
    fclose(fx);
    fclose(fd);
    CHECK(ncase > 100 && nforth > 100, "corpus: %u C and x86-64, %u Forth", ncase, nforth);
    printf("%u checks, %u failures; %u C and x86-64 cases, %u Forth runs (%u skipped), %u for "
           "gforth\n",
           checks, fails, ncase, nforth, nforth_skip, ngforth);
    return fails ? 1 : 0;
}
