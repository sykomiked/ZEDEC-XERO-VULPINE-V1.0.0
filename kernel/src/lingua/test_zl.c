/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zl.c — Lingua tests: every IR operation and trap, the verifier, the
 * three front-ends (accepted programs, refusals by name, run-time traps),
 * the twin round trip, golden outputs written by gfortran, gforth and fpc,
 * and one algorithm in every language giving one trace.
 *
 * Usage: test_zl [golden-dir]   (default src/lingua/golden, run from kernel/) */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zl.h"

static int fails, checks;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zl_work_t work;
static zl_prog_t P, P2;
static zl_run_t R;
static char out[1 << 16], tw[1 << 16], tw2[1 << 16], pk[1 << 20];
static zl_prog_t P3;

/* ------------------------------------------------------------ IR builder */

static void prog_reset(int bits)
{
    memset(&P, 0, sizeof P);
    P.lang = ZL_LANG_FORTH;
    P.word_bits = (uint32_t) bits;
    P.nslots = 8;
}

static void I(int op, int64_t a, int32_t b)
{
    P.code[P.ncode].op = (uint8_t) op;
    P.code[P.ncode].a = a;
    P.code[P.ncode].b = b;
    P.code[P.ncode].line = P.ncode + 1;
    P.ncode++;
}

static int run_p(const zl_prog_t *p, const int64_t *in, size_t nin, uint64_t fuel)
{
    memset(&R, 0, sizeof R);
    R.in = in;
    R.nin = nin;
    R.out = out;
    R.out_cap = sizeof out;
    R.fuel = fuel;
    return zl_run(p, &R);
}

/* Run "push x, push y, op, print" and return status; *res gets the value. */
static int binop(int bits, int op, int64_t a_imm, int64_t x, int64_t y, int64_t *res)
{
    int64_t tr[4];
    zl_trace_t t = {tr, 4, 0};
    prog_reset(bits);
    I(ZL_PUSH, x, 0);
    I(ZL_PUSH, y, 0);
    I(op, a_imm, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.trace = &t;
    int s = zl_run(&P, &R);
    *res = t.n ? tr[0] : 0x7777;
    return s;
}

static void test_arith(void)
{
    const int64_t MIN = INT64_MIN, MAX = INT64_MAX;
    struct {
        int bits, op;
        int64_t imm, x, y, want;
        int st;
    } t[] = {
        {64, ZL_ADD, 0, 2, 3, 5, 0},
        {64, ZL_ADD, 0, MAX, 1, 0, ZL_E_OVERFLOW},
        {64, ZL_ADD, 0, MIN, -1, 0, ZL_E_OVERFLOW},
        {64, ZL_ADD, 0, MAX, -1, MAX - 1, 0},
        {32, ZL_ADD, 0, 2147483647, 1, 0, ZL_E_OVERFLOW},
        {32, ZL_ADD, 0, -2147483647, -1, -2147483647 - 1, 0},
        {32, ZL_ADD, 0, -2147483647 - 1, -1, 0, ZL_E_OVERFLOW},
        {64, ZL_SUB, 0, 2, 3, -1, 0},
        {64, ZL_SUB, 0, MIN, 1, 0, ZL_E_OVERFLOW},
        {64, ZL_SUB, 0, 0, MIN, 0, ZL_E_OVERFLOW},
        {32, ZL_SUB, 0, -2147483647, 2, 0, ZL_E_OVERFLOW},
        {64, ZL_MUL, 0, -4, 5, -20, 0},
        {64, ZL_MUL, 0, MAX, 2, 0, ZL_E_OVERFLOW},
        {64, ZL_MUL, 0, MIN, -1, 0, ZL_E_OVERFLOW},
        {32, ZL_MUL, 0, 65536, 32768, 0, ZL_E_OVERFLOW},
        {32, ZL_MUL, 0, 65536, 32767, 2147418112, 0},
        {64, ZL_DIVT, 0, 7, 2, 3, 0},
        {64, ZL_DIVT, 0, -7, 2, -3, 0},
        {64, ZL_DIVT, 0, 7, -2, -3, 0},
        {64, ZL_DIVT, 0, -7, -2, 3, 0},
        {64, ZL_DIVT, 0, 1, 0, 0, ZL_E_DIVZERO},
        {64, ZL_DIVT, 0, MIN, -1, 0, ZL_E_OVERFLOW},
        {64, ZL_DIVT, 0, 5, -1, -5, 0},
        {32, ZL_DIVT, 0, -2147483647 - 1, -1, 0, ZL_E_OVERFLOW},
        {64, ZL_MODT, 0, 7, 2, 1, 0},
        {64, ZL_MODT, 0, -7, 2, -1, 0},
        {64, ZL_MODT, 0, 7, -2, 1, 0},
        {64, ZL_MODT, 0, MIN, -1, 0, 0},
        {64, ZL_MODT, 0, 3, 0, 0, ZL_E_DIVZERO},
        {64, ZL_DIVF, 0, 7, 2, 3, 0},
        {64, ZL_DIVF, 0, -7, 2, -4, 0},
        {64, ZL_DIVF, 0, 7, -2, -4, 0},
        {64, ZL_DIVF, 0, -7, -2, 3, 0},
        {64, ZL_DIVF, 0, -8, 2, -4, 0},
        {64, ZL_DIVF, 0, 0, 5, 0, 0},
        {64, ZL_DIVF, 0, MIN, -1, 0, ZL_E_OVERFLOW},
        {64, ZL_DIVF, 0, 6, -1, -6, 0},
        {64, ZL_MODF, 0, 7, 2, 1, 0},
        {64, ZL_MODF, 0, -7, 2, 1, 0},
        {64, ZL_MODF, 0, 7, -2, -1, 0},
        {64, ZL_MODF, 0, -7, -2, -1, 0},
        {64, ZL_MODF, 0, -8, 2, 0, 0},
        {64, ZL_MODF, 0, MIN, -1, 0, 0},
        {64, ZL_MODF, 0, 1, 0, 0, ZL_E_DIVZERO},
        {64, ZL_DIVX, 0, 8, 2, 4, 0},
        {64, ZL_DIVX, 0, -9, 3, -3, 0},
        {64, ZL_DIVX, 0, 7, 2, 0, ZL_E_INEXACT},
        {64, ZL_DIVX, 0, -7, 2, 0, ZL_E_INEXACT},
        {64, ZL_DIVX, 0, 7, 0, 0, ZL_E_DIVZERO},
        {64, ZL_DIVX, 0, 7, -1, -7, 0},
        {64, ZL_POW, 0, 2, 10, 1024, 0},
        {64, ZL_POW, 0, 2, 62, (int64_t) 1 << 62, 0},
        {64, ZL_POW, 0, 2, 63, 0, ZL_E_OVERFLOW},
        {64, ZL_POW, 0, -2, 63, MIN, 0},
        {64, ZL_POW, 0, -2, 3, -8, 0},
        {64, ZL_POW, 0, 0, 0, 1, 0},
        {64, ZL_POW, 0, 7, 0, 1, 0},
        {64, ZL_POW, 0, 0, 5, 0, 0},
        {64, ZL_POW, 0, 2, -1, 0, 0},
        {64, ZL_POW, 0, 1, -5, 1, 0},
        {64, ZL_POW, 0, -1, -5, -1, 0},
        {64, ZL_POW, 0, -1, -4, 1, 0},
        {64, ZL_POW, 0, 0, -1, 0, ZL_E_DIVZERO},
        {64, ZL_POW, 1, 2, -1, 0, ZL_E_INEXACT},
        {64, ZL_POW, 1, -1, -3, -1, 0},
        {64, ZL_POW, 0, 3, 40, 0, ZL_E_OVERFLOW},
        {64, ZL_POW, 0, 10, 18, 1000000000000000000ll, 0},
        {64, ZL_POW, 0, 1, MAX, 1, 0},
        {64, ZL_POW, 0, -1, MAX, -1, 0},
        {64, ZL_POW, 0, 2, 64, 0, ZL_E_OVERFLOW},
        {32, ZL_POW, 0, 2, 31, 0, ZL_E_OVERFLOW},
        {32, ZL_POW, 0, -2, 31, -2147483647 - 1, 0},
        {32, ZL_POW, 0, 3, 19, 1162261467, 0},
        {32, ZL_POW, 0, 46341, 2, 0, ZL_E_OVERFLOW},
        {64, ZL_MIN, 0, 3, -4, -4, 0},
        {64, ZL_MIN, 0, -4, 3, -4, 0},
        {64, ZL_MAX, 0, 3, -4, 3, 0},
        {64, ZL_MAX, 0, -4, 3, 3, 0},
        {64, ZL_EQ, 0, 3, 3, 1, 0},
        {64, ZL_EQ, 0, 3, 4, 0, 0},
        {64, ZL_NE, 0, 3, 4, 1, 0},
        {64, ZL_NE, 0, 3, 3, 0, 0},
        {64, ZL_LT, 0, 3, 4, 1, 0},
        {64, ZL_LT, 0, 4, 4, 0, 0},
        {64, ZL_LE, 0, 4, 4, 1, 0},
        {64, ZL_LE, 0, 5, 4, 0, 0},
        {64, ZL_GT, 0, 5, 4, 1, 0},
        {64, ZL_GT, 0, 4, 4, 0, 0},
        {64, ZL_GE, 0, 4, 4, 1, 0},
        {64, ZL_GE, 0, 3, 4, 0, 0},
        {64, ZL_AND, 0, 12, 10, 8, 0},
        {64, ZL_OR, 0, 12, 10, 14, 0},
        {64, ZL_XOR, 0, 12, 10, 6, 0},
        {64, ZL_XOR, 0, -1, 0, -1, 0},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        int64_t r;
        int s = binop(t[i].bits, t[i].op, t[i].imm, t[i].x, t[i].y, &r);
        CHECK(s == t[i].st, "arith case %zu: op %d %lld,%lld status %d want %d", i, t[i].op,
              (long long) t[i].x, (long long) t[i].y, s, t[i].st);
        if (t[i].st == 0)
            CHECK(r == t[i].want, "arith case %zu: got %lld want %lld", i, (long long) r,
                  (long long) t[i].want);
    }
}

static int unop(int bits, int op, int64_t x, int64_t *res)
{
    int64_t tr[2];
    zl_trace_t t = {tr, 2, 0};
    prog_reset(bits);
    I(ZL_PUSH, x, 0);
    I(op, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.trace = &t;
    int s = zl_run(&P, &R);
    *res = t.n ? tr[0] : 0x7777;
    return s;
}

static void test_unary(void)
{
    struct {
        int bits, op;
        int64_t x, want;
        int st;
    } t[] = {
        {64, ZL_NEG, 5, -5, 0},
        {64, ZL_NEG, -5, 5, 0},
        {64, ZL_NEG, 0, 0, 0},
        {64, ZL_NEG, INT64_MIN, 0, ZL_E_OVERFLOW},
        {64, ZL_NEG, INT64_MAX, -INT64_MAX, 0},
        {32, ZL_NEG, -2147483647 - 1, 0, ZL_E_OVERFLOW},
        {32, ZL_NEG, 2147483647, -2147483647, 0},
        {64, ZL_ABS, -5, 5, 0},
        {64, ZL_ABS, 5, 5, 0},
        {64, ZL_ABS, 0, 0, 0},
        {64, ZL_ABS, INT64_MIN, 0, ZL_E_OVERFLOW},
        {32, ZL_ABS, -2147483647 - 1, 0, ZL_E_OVERFLOW},
        {32, ZL_ABS, -2147483647, 2147483647, 0},
        {64, ZL_SGN, -9, -1, 0},
        {64, ZL_SGN, 0, 0, 0},
        {64, ZL_SGN, 9, 1, 0},
        {64, ZL_SGN, INT64_MIN, -1, 0},
        {64, ZL_INVERT, 0, -1, 0},
        {64, ZL_INVERT, 5, -6, 0},
        {64, ZL_INVERT, -1, 0, 0},
        {64, ZL_NOT, 0, 1, 0},
        {64, ZL_NOT, 1, 0, 0},
        {64, ZL_NOT, -3, 0, 0},
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        int64_t r;
        int s = unop(t[i].bits, t[i].op, t[i].x, &r);
        CHECK(s == t[i].st, "unary case %zu: status %d want %d", i, s, t[i].st);
        if (t[i].st == 0)
            CHECK(r == t[i].want, "unary case %zu: got %lld want %lld", i, (long long) r,
                  (long long) t[i].want);
    }
}

static void test_stack_and_flow(void)
{
    int64_t tr[16];
    zl_trace_t t = {tr, 16, 0};
    int s;
    /* 1 2 3 ROT -> 2 3 1 ; OVER -> 2 3 1 3 ; SWAP -> 2 3 3 1 ; DROP -> 2 3 3 ; DUP -> 2 3 3 3 */
    prog_reset(64);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 2, 0);
    I(ZL_PUSH, 3, 0);
    I(ZL_ROT, 0, 0);
    I(ZL_OVER, 0, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_DUP, 0, 0);
    for (int k = 0; k < 4; k++) I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.trace = &t;
    s = zl_run(&P, &R);
    CHECK(s == 0 && t.n == 4 && tr[0] == 3 && tr[1] == 3 && tr[2] == 3 && tr[3] == 2,
          "stack shuffles: %d n=%zu %lld %lld %lld %lld", s, t.n, (long long) tr[0],
          (long long) tr[1], (long long) tr[2], (long long) tr[3]);
    CHECK(strcmp(out, "3332") == 0, "plain OUTI output '%s'", out);

    int under[] = {ZL_DROP,  ZL_DUP,    ZL_SWAP, ZL_OVER,   ZL_ROT,  ZL_ADD,   ZL_NEG,
                   ZL_STORE, ZL_JZ,     ZL_JNZ,  ZL_OUTI,   ZL_OUTC, ZL_LOADX, ZL_STOREX,
                   ZL_LOADA, ZL_STOREA, ZL_NOT,  ZL_INVERT, ZL_ABS,  ZL_SGN};
    for (size_t k = 0; k < sizeof under / sizeof under[0]; k++) {
        prog_reset(64);
        if (under[k] == ZL_SWAP || under[k] == ZL_OVER || under[k] == ZL_ADD ||
            under[k] == ZL_STOREX || under[k] == ZL_STOREA)
            I(ZL_PUSH, 0, 0); /* one short of two */
        if (under[k] == ZL_ROT) {
            I(ZL_PUSH, 0, 0);
            I(ZL_PUSH, 0, 0);
        }
        I(under[k], under[k] == ZL_LOADX || under[k] == ZL_STOREX ? 0 : 0,
          under[k] == ZL_LOADX || under[k] == ZL_STOREX ? 1 : 0);
        if (under[k] == ZL_JZ || under[k] == ZL_JNZ) P.code[P.ncode - 1].a = 0;
        I(ZL_HALT, 0, 0);
        s = run_p(&P, 0, 0, 0);
        CHECK(s == ZL_E_STACK, "underflow op %d: %d", under[k], s);
        CHECK(R.trap_pc == P.ncode - 2, "underflow trap pc op %d: %u", under[k], R.trap_pc);
    }
    /* overflow: 257 pushes */
    prog_reset(64);
    for (int k = 0; k < ZL_MAX_STACK; k++) I(ZL_PUSH, k, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_OK, "full stack is fine");
    P.ncode--;
    I(ZL_DUP, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_STACK, "DUP past the stack");
    P.code[P.ncode - 2].op = ZL_OVER;
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_STACK, "OVER past the stack");
    P.code[P.ncode - 2].op = ZL_LOAD;
    P.code[P.ncode - 2].a = 0;
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_STACK, "LOAD past the stack");
    P.code[P.ncode - 2].op = ZL_IN;
    {
        int64_t one = 1;
        CHECK(run_p(&P, &one, 1, 0) == ZL_E_STACK, "IN past the stack");
    }

    /* jumps */
    prog_reset(64);
    I(ZL_PUSH, 0, 0);
    I(ZL_JZ, 4, 0);
    I(ZL_PUSH, 111, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_PUSH, 5, 0);
    I(ZL_JNZ, 8, 0);
    I(ZL_PUSH, 222, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_JNZ, 12, 0);
    I(ZL_PUSH, 7, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_JZ, 16, 0);
    I(ZL_PUSH, 8, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == 0 && strcmp(out, "78") == 0, "JZ/JNZ taken and not taken: '%s'",
          out);

    /* CALL / RET */
    prog_reset(64);
    I(ZL_CALL, 3, 0);
    I(ZL_CALL, 3, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_PUSH, 4, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_RET, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == 0 && strcmp(out, "44") == 0, "CALL/RET: '%s'", out);
    prog_reset(64);
    I(ZL_RET, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_RSTACK, "RET with nothing to return to");
    prog_reset(64);
    I(ZL_CALL, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_RSTACK && R.used == ZL_MAX_CALLS + 1,
          "unbounded recursion stops at the call depth");
    /* exactly ZL_MAX_CALLS nested calls are fine */
    prog_reset(64);
    P.nslots = 1;
    I(ZL_PUSH, ZL_MAX_CALLS, 0);
    I(ZL_STORE, 0, 0);
    I(ZL_CALL, 4, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_LOAD, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_SUB, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_STORE, 0, 0);
    I(ZL_JZ, 11, 0);
    I(ZL_CALL, 4, 0);
    I(ZL_RET, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_OK, "64 nested calls fit");
    P.code[0].a = ZL_MAX_CALLS + 1;
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_RSTACK, "65 nested calls do not");

    /* fuel: 3 instructions */
    prog_reset(64);
    I(ZL_PUSH, 1, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 3) == ZL_OK && R.used == 3, "fuel exactly enough (used %llu)",
          (unsigned long long) R.used);
    CHECK(run_p(&P, 0, 0, 2) == ZL_E_FUEL && R.trap_pc == 2, "fuel one short");
    prog_reset(64);
    I(ZL_JMP, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_FUEL && R.used == 10000000ull,
          "default fuel is 10^7 (used %llu)", (unsigned long long) R.used);

    /* TRAP */
    prog_reset(64);
    I(ZL_TRAP, ZL_E_DOMAIN, 0);
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_DOMAIN && R.trap_line == 1, "TRAP stops with its status");
}

static void test_memory_io(void)
{
    int s;
    /* LOADX / STOREX on a 3-cell array at slot 2 */
    for (int64_t ix = -1; ix <= 3; ix++) {
        prog_reset(64);
        I(ZL_PUSH, ix, 0);
        I(ZL_PUSH, 42, 0);
        I(ZL_STOREX, 2, 3);
        I(ZL_PUSH, ix, 0);
        I(ZL_LOADX, 2, 3);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        s = run_p(&P, 0, 0, 0);
        if (ix < 0 || ix > 2)
            CHECK(s == ZL_E_BOUNDS && R.trap_pc == 2, "STOREX index %lld out of range",
                  (long long) ix);
        else
            CHECK(s == 0 && strcmp(out, "42") == 0, "STOREX/LOADX index %lld", (long long) ix);
        prog_reset(64);
        I(ZL_PUSH, ix, 0);
        I(ZL_LOADX, 2, 3);
        I(ZL_HALT, 0, 0);
        s = run_p(&P, 0, 0, 0);
        CHECK(s == ((ix < 0 || ix > 2) ? ZL_E_BOUNDS : ZL_OK), "LOADX index %lld: %d",
              (long long) ix, s);
    }
    /* the array does not alias its neighbours */
    prog_reset(64);
    I(ZL_PUSH, 9, 0);
    I(ZL_STORE, 1, 0);
    I(ZL_PUSH, 9, 0);
    I(ZL_STORE, 5, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_STOREX, 2, 3);
    I(ZL_PUSH, 2, 0);
    I(ZL_PUSH, 3, 0);
    I(ZL_STOREX, 2, 3);
    I(ZL_LOAD, 1, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_LOAD, 2, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_LOAD, 4, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == 0 && strcmp(out, "9139") == 0, "array cells in place: '%s'", out);
    /* LOADA / STOREA: 8 slots */
    for (int64_t a = -1; a <= 8; a++) {
        prog_reset(64);
        I(ZL_PUSH, 77, 0);
        I(ZL_PUSH, a, 0);
        I(ZL_STOREA, 0, 0);
        I(ZL_PUSH, a, 0);
        I(ZL_LOADA, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        s = run_p(&P, 0, 0, 0);
        if (a < 0 || a > 7)
            CHECK(s == ZL_E_BOUNDS, "STOREA address %lld", (long long) a);
        else
            CHECK(s == 0 && strcmp(out, "77") == 0, "STOREA/LOADA address %lld", (long long) a);
        prog_reset(64);
        I(ZL_PUSH, a, 0);
        I(ZL_LOADA, 0, 0);
        I(ZL_HALT, 0, 0);
        CHECK(run_p(&P, 0, 0, 0) == ((a < 0 || a > 7) ? ZL_E_BOUNDS : ZL_OK), "LOADA address %lld",
              (long long) a);
    }
    /* memory starts zeroed on every run */
    prog_reset(64);
    I(ZL_LOAD, 3, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_PUSH, 5, 0);
    I(ZL_STORE, 3, 0);
    I(ZL_HALT, 0, 0);
    run_p(&P, 0, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == 0 && strcmp(out, "0") == 0, "slots zeroed per run");

    /* number formats */
    struct {
        int64_t v;
        int fl, w;
        const char *want;
    } f[] = {
        {5, 0, 0, "5"},
        {-5, 0, 0, "-5"},
        {5, ZL_FMT_SIGNSPACE, 0, " 5"},
        {-5, ZL_FMT_SIGNSPACE, 0, "-5"},
        {0, ZL_FMT_SIGNSPACE, 0, " 0"},
        {5, ZL_FMT_TRAIL, 0, "5 "},
        {5, 3, 0, " 5 "},
        {-5, 3, 0, "-5 "},
        {42, 0, 12, "          42"},
        {-42, 0, 12, "         -42"},
        {123456, 0, 3, "123456"},
        {INT64_MIN, 0, 0, "-9223372036854775808"},
        {INT64_MAX, 0, 0, "9223372036854775807"},
        {7, 0, 1, "7"},
        {7, 0, 2, " 7"},
    };
    for (size_t k = 0; k < sizeof f / sizeof f[0]; k++) {
        prog_reset(64);
        I(ZL_PUSH, f[k].v, 0);
        I(ZL_OUTI, f[k].fl, f[k].w);
        I(ZL_HALT, 0, 0);
        s = run_p(&P, 0, 0, 0);
        CHECK(s == 0 && strcmp(out, f[k].want) == 0, "format %zu: '%s' want '%s'", k, out,
              f[k].want);
    }
    /* OUTC, NL, TAB, OUTS */
    prog_reset(64);
    strcpy(P.str, "AB");
    P.nstr = 1;
    P.str_off[0] = 0;
    P.str_len[0] = 2;
    P.strbytes = 2;
    I(ZL_OUTS, 0, 0);
    I(ZL_TAB, 5, 0);
    I(ZL_PUSH, 'x', 0);
    I(ZL_OUTC, 0, 0);
    I(ZL_TAB, 5, 0);
    I(ZL_TAB, 5, 0);
    I(ZL_NL, 0, 0);
    I(ZL_TAB, 3, 0);
    I(ZL_OUTS, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == 0 && strcmp(out, "AB   x         \n   AB") == 0, "TAB zones: '%s'",
          out);
    for (int64_t ch = -1; ch <= 256; ch += 257) {
        prog_reset(64);
        I(ZL_PUSH, ch, 0);
        I(ZL_OUTC, 0, 0);
        I(ZL_HALT, 0, 0);
        CHECK(run_p(&P, 0, 0, 0) == ZL_E_DOMAIN, "OUTC %lld is outside a byte", (long long) ch);
    }
    prog_reset(64);
    I(ZL_PUSH, 255, 0);
    I(ZL_OUTC, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_OUTC, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(run_p(&P, 0, 0, 0) == 0 && R.out_len == 2 && (unsigned char) out[0] == 255 && out[1] == 0,
          "OUTC 255 and 0");
    /* output buffer full: 4 bytes of room */
    prog_reset(64);
    I(ZL_PUSH, 12345, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = 5;
    CHECK(zl_run(&P, &R) == ZL_OK && R.out_len == 5, "exactly fits");
    R.out_cap = 4;
    CHECK(zl_run(&P, &R) == ZL_E_OUTPUT && R.out_len == 4, "one byte short");
    R.out_cap = 0;
    R.out = 0;
    CHECK(zl_run(&P, &R) == ZL_E_OUTPUT, "no buffer at all");
    /* input */
    {
        int64_t in[3] = {4, -9, (int64_t) 1 << 40};
        prog_reset(64);
        I(ZL_IN, 0, 0);
        I(ZL_IN, 0, 0);
        I(ZL_ADD, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        CHECK(run_p(&P, in, 2, 0) == 0 && strcmp(out, "-5") == 0, "IN reads in order");
        CHECK(run_p(&P, in, 1, 0) == ZL_E_INPUT, "IN past the end");
        prog_reset(32);
        I(ZL_IN, 0, 0);
        I(ZL_HALT, 0, 0);
        CHECK(run_p(&P, in + 2, 1, 0) == ZL_E_OVERFLOW, "IN checks the word width");
    }
    /* trace capacity */
    {
        int64_t tr[2];
        zl_trace_t t = {tr, 2, 99};
        prog_reset(64);
        for (int k = 0; k < 3; k++) {
            I(ZL_PUSH, k + 1, 0);
            I(ZL_OUTI, 0, 0);
        }
        I(ZL_HALT, 0, 0);
        memset(&R, 0, sizeof R);
        R.out = out;
        R.out_cap = sizeof out;
        R.trace = &t;
        CHECK(zl_run(&P, &R) == 0 && t.n == 2 && tr[0] == 1 && tr[1] == 2,
              "trace stops at its capacity");
    }
}

static void test_verify(void)
{
    zl_diag_t d;
    prog_reset(64);
    I(ZL_HALT, 0, 0);
    CHECK(zl_verify(&P, &d) == ZL_OK && d.status == ZL_OK, "minimal program");
    CHECK(zl_verify(0, &d) == ZL_E_VERIFY, "NULL program");
    struct {
        const char *what;
        void (*f)(void);
    } dummy;
    (void) dummy;
#define BAD(setup, what)                                                                           \
    do {                                                                                           \
        prog_reset(64);                                                                            \
        setup;                                                                                     \
        CHECK(zl_verify(&P, &d) == ZL_E_VERIFY && d.status == ZL_E_VERIFY, "verify rejects %s",    \
              what);                                                                               \
        CHECK(run_p(&P, 0, 0, 0) == ZL_E_VERIFY, "run refuses %s", what);                          \
    } while (0)
    BAD((I(ZL_HALT, 0, 0), P.lang = 0), "language 0");
    BAD((I(ZL_HALT, 0, 0), P.lang = ZL_LANG__COUNT), "language past the end");
    BAD((I(ZL_HALT, 0, 0), P.word_bits = 16), "16-bit words");
    BAD((void) 0, "empty code");
    BAD((I(ZL_HALT, 0, 0), P.ncode = ZL_MAX_CODE + 1), "oversized code");
    BAD((I(ZL_HALT, 0, 0), P.nslots = ZL_MAX_SLOTS + 1), "too many slots");
    BAD((I(ZL_HALT, 0, 0), P.nstr = ZL_MAX_STR + 1), "too many strings");
    BAD((I(ZL_HALT, 0, 0), P.strbytes = ZL_MAX_STRBYTES + 1), "too many string bytes");
    BAD((I(ZL_HALT, 0, 0), P.nstr = 1, P.str_off[0] = 2, P.str_len[0] = 1, P.strbytes = 2),
        "string past the table");
    BAD((I(ZL_HALT, 0, 0), P.nstr = 1, P.str_off[0] = 3, P.str_len[0] = 0, P.strbytes = 2),
        "string offset past the table");
    BAD((I(ZL_LOAD, 8, 0), I(ZL_HALT, 0, 0)), "LOAD past the slots");
    BAD((I(ZL_STORE, -1, 0), I(ZL_HALT, 0, 0)), "STORE before the slots");
    BAD((I(ZL_LOADX, 6, 3), I(ZL_HALT, 0, 0)), "LOADX array past the slots");
    BAD((I(ZL_STOREX, 0, 0), I(ZL_HALT, 0, 0)), "STOREX zero length");
    BAD((I(ZL_LOADX, -1, 2), I(ZL_HALT, 0, 0)), "LOADX negative base");
    BAD((I(ZL_JMP, 2, 0), I(ZL_HALT, 0, 0)), "jump past the end");
    BAD((I(ZL_JZ, -1, 0), I(ZL_HALT, 0, 0)), "jump before the start");
    BAD((I(ZL_JNZ, 5, 0), I(ZL_HALT, 0, 0)), "JNZ target");
    BAD((I(ZL_CALL, 9, 0), I(ZL_HALT, 0, 0)), "CALL target");
    BAD((I(ZL_OUTS, 0, 0), I(ZL_HALT, 0, 0)), "OUTS with no strings");
    BAD((I(ZL_OUTI, 4, 0), I(ZL_HALT, 0, 0)), "OUTI flags");
    BAD((I(ZL_OUTI, 0, 65), I(ZL_HALT, 0, 0)), "OUTI width");
    BAD((I(ZL_OUTI, 0, -1), I(ZL_HALT, 0, 0)), "OUTI negative width");
    BAD((I(ZL_POW, 2, 0), I(ZL_HALT, 0, 0)), "POW mode");
    BAD((I(ZL_TAB, 0, 0), I(ZL_HALT, 0, 0)), "TAB zone 0");
    BAD((I(ZL_TAB, 256, 0), I(ZL_HALT, 0, 0)), "TAB zone 256");
    BAD((I(ZL_TRAP, 0, 0), I(ZL_HALT, 0, 0)), "TRAP with status 0");
    BAD((I(ZL_TRAP, -2700, 0), I(ZL_HALT, 0, 0)), "TRAP with a foreign status");
    BAD((P.word_bits = 32, I(ZL_PUSH, 2147483648ll, 0), I(ZL_HALT, 0, 0)),
        "wide constant in 32 bits");
    BAD((P.word_bits = 32, I(ZL_PUSH, -2147483649ll, 0), I(ZL_HALT, 0, 0)),
        "wide negative constant in 32 bits");
    BAD((I(ZL_OP__COUNT, 0, 0), I(ZL_HALT, 0, 0)), "unknown opcode");
    BAD((I(ZL_PUSH, 0, 0)), "falling off the end");
    BAD((I(ZL_HALT, 0, 0), I(ZL_NL, 0, 0)), "last op not terminating");
#undef BAD
    /* accepted edges */
    prog_reset(32);
    I(ZL_PUSH, 2147483647, 0);
    I(ZL_PUSH, -2147483647 - 1, 0);
    I(ZL_TAB, 255, 0);
    I(ZL_TAB, 1, 0);
    I(ZL_OUTI, 3, 64);
    I(ZL_TRAP, ZL_E_ARG, 0);
    I(ZL_TRAP, -2699, 0);
    I(ZL_LOADX, 7, 1);
    I(ZL_JMP, 0, 0);
    CHECK(zl_verify(&P, &d) == ZL_OK, "edges accepted: %s", d.msg);
    prog_reset(64);
    I(ZL_RET, 0, 0);
    CHECK(zl_verify(&P, 0) == ZL_OK, "RET may end the code; NULL diag is fine");
    prog_reset(64);
    I(ZL_TRAP, ZL_E_DOMAIN, 0);
    CHECK(zl_verify(&P, 0) == ZL_OK, "TRAP may end the code");
}

static void test_equal_digest(void)
{
    prog_reset(64);
    I(ZL_PUSH, 1, 0);
    I(ZL_OUTI, 1, 2);
    I(ZL_HALT, 0, 0);
    strcpy(P.str, "hi");
    P.nstr = 1;
    P.str_len[0] = 2;
    P.strbytes = 2;
    memcpy(&P2, &P, sizeof P);
    CHECK(zl_prog_equal(&P, &P2) && zl_prog_digest(&P) == zl_prog_digest(&P2), "a copy is equal");
    P2.code[0].line = 99;
    CHECK(zl_prog_equal(&P, &P2) && zl_prog_digest(&P) == zl_prog_digest(&P2),
          "source lines are not compared");
#define DIFF(mut, what)                                                                            \
    do {                                                                                           \
        memcpy(&P2, &P, sizeof P);                                                                 \
        mut;                                                                                       \
        CHECK(!zl_prog_equal(&P, &P2) && !zl_prog_equal(&P2, &P), "%s differs", what);             \
        CHECK(zl_prog_digest(&P) != zl_prog_digest(&P2), "%s changes the digest", what);           \
    } while (0)
    DIFF(P2.code[0].a = 2, "operand a");
    DIFF(P2.code[1].b = 3, "operand b");
    DIFF(P2.code[1].op = ZL_OUTC, "opcode");
    DIFF(P2.lang = ZL_LANG_BASIC, "language");
    DIFF(P2.word_bits = 32, "word width");
    DIFF(P2.nslots = 9, "slot count");
    DIFF(P2.str[1] = 'o', "string bytes");
    DIFF((P2.str_len[0] = 1), "string length");
    DIFF((P2.nstr = 0), "string count");
    DIFF((P2.code[3].op = ZL_HALT, P2.ncode = 4), "code length");
#undef DIFF
}

/* ------------------------------------------------------------ front-ends */

static int compile(int lang, const char *src, zl_diag_t *d)
{
    return zl_compile(lang, src, strlen(src), &P, &work, d);
}

static int run_src(int lang, const char *src, const int64_t *in, size_t nin, uint64_t fuel)
{
    zl_diag_t d;
    int s = compile(lang, src, &d);
    if (s != ZL_OK) {
        printf("  (compile: %s line %u: %s)\n", zl_status_name(s), d.line, d.msg);
        return 1000 + s;
    }
    return run_p(&P, in, nin, fuel);
}

/* The twin round trip on any accepted program. */
static void twin_props(int lang, const char *src, size_t len, const char *label)
{
    zl_diag_t d;
    size_t n1 = 0, n2 = 0;
    int s = zl_compile(lang, src, len, &P, &work, &d);
    CHECK(s == ZL_OK, "%s compiles: %s line %u: %s", label, zl_status_name(s), d.line, d.msg);
    if (s) return;
    /* the precompiled form reads back as the same program, line numbers too */
    size_t pn = 0;
    s = zl_pack(&P, pk, sizeof pk, &pn);
    CHECK(s == ZL_OK && strlen(pk) == pn, "%s packs", label);
    s = zl_unpack(pk, pn, &P3, &d);
    CHECK(s == ZL_OK && zl_prog_equal(&P, &P3), "%s unpacks to the same IR: %s", label, d.msg);
    for (uint32_t i = 0; s == ZL_OK && i < P.ncode; i++)
        if (P3.code[i].line != P.code[i].line) {
            CHECK(0, "%s unpack keeps line %u", label, i);
            break;
        }
    s = zl_twin(lang, src, len, tw, sizeof tw, &n1, &work, &d);
    CHECK(s == ZL_OK, "%s twin: %s", label, d.msg);
    if (s) return;
    CHECK(strlen(tw) == n1, "%s twin is NUL-terminated at its length", label);
    s = zl_compile(lang | ZL_TWIN, tw, n1, &P2, &work, &d);
    CHECK(s == ZL_OK, "%s twin is accepted as canonical: %s line %u: %s\n%s", label,
          zl_status_name(s), d.line, d.msg, tw);
    CHECK(s == ZL_OK && zl_prog_equal(&P, &P2), "%s twin compiles to the same IR", label);
    CHECK(zl_prog_digest(&P) == zl_prog_digest(&P2), "%s twin digest", label);
    s = zl_twin(lang, tw, n1, tw2, sizeof tw2, &n2, &work, &d);
    CHECK(s == ZL_OK && n1 == n2 && memcmp(tw, tw2, n1) == 0, "%s twin of the twin is the twin",
          label);
    s = zl_compile(lang | ZL_TWIN, src, len, &P2, &work, &d);
    if (n1 == len && memcmp(tw, src, len) == 0)
        CHECK(s == ZL_OK, "%s is already canonical", label);
    else
        CHECK(s == ZL_E_NONCANON && d.status == ZL_E_NONCANON && d.line >= 1,
              "%s is refused as a twin (%d)", label, s);
    /* recompiling gives the same IR */
    s = zl_compile(lang, src, len, &P2, &work, &d);
    CHECK(s == ZL_OK && zl_prog_equal(&P, &P2), "%s compiles deterministically", label);
    /* a twin that does not fit is refused, not cut */
    if (n1 > 4) {
        s = zl_twin(lang, src, len, tw2, n1, &n2, &work, &d);
        CHECK(s == ZL_E_LIMIT && n2 == 0 && tw2[0] == 0, "%s twin one byte short is refused (%d)",
              label, s);
    }
}

static void expect_twin(int lang, const char *src, const char *want)
{
    zl_diag_t d;
    size_t n = 0;
    int s = zl_twin(lang, src, strlen(src), tw, sizeof tw, &n, &work, &d);
    CHECK(s == ZL_OK && strcmp(tw, want) == 0, "twin of %s:\n got  [%s]\n want [%s] (%s)",
          zl_lang_name(lang), tw, want, d.msg);
    twin_props(lang, src, strlen(src), "expect_twin");
}

static void expect_out(int lang, const char *src, const char *want, const char *what)
{
    int s = run_src(lang, src, 0, 0, 0);
    CHECK(s == ZL_OK && strcmp(out, want) == 0, "%s: status %d out [%s] want [%s]", what, s, out,
          want);
    twin_props(lang, src, strlen(src), what);
}

static void expect_refused(int lang, const char *src, int status, uint32_t line, const char *what)
{
    zl_diag_t d;
    memset(&P, 0x5a, sizeof P);
    int s = compile(lang, src, &d);
    CHECK(s == status && d.status == status, "%s: status %d (%s) want %d", what, s, d.msg, status);
    if (line) CHECK(d.line == line, "%s: line %u want %u", what, d.line, line);
    CHECK(d.msg && d.msg[0], "%s: a reason is given", what);
    CHECK(P.ncode == 0 && P.nslots == 0 && P.lang == 0, "%s: refused program is zeroed", what);
    size_t n = 7;
    s = zl_twin(lang, src, strlen(src), tw, sizeof tw, &n, &work, &d);
    CHECK(s == status && n == 0 && tw[0] == 0, "%s: no twin either (%d)", what, s);
}

/* A program that reads input: given in[0..nin), it prints want. */
static void expect_in(int lang, const char *src, const int64_t *in, size_t nin, const char *want,
                      const char *what)
{
    int s = run_src(lang, src, in, nin, 0);
    CHECK(s == ZL_OK && strcmp(out, want) == 0, "%s: status %d out [%s] want [%s]", what, s, out,
          want);
    twin_props(lang, src, strlen(src), what);
}

static void expect_trap(int lang, const char *src, int status, uint32_t line, const char *what)
{
    int s = run_src(lang, src, 0, 0, 0);
    CHECK(s == status, "%s: status %d want %d (out [%s])", what, s, status, out);
    if (line) CHECK(R.trap_line == line, "%s: trap line %u want %u", what, R.trap_line, line);
}

static void test_basic(void)
{
    const int B = ZL_LANG_BASIC;
    expect_out(B, "10 PRINT 1+2*3\n20 END\n", " 7 \n", "precedence");
    expect_out(B, "10 PRINT (1+2)*3; -2^2; 2^3^2; 2-3-4; 24/4/2\n", " 9 -4  64 -5  3 \n",
               "associativity");
    expect_out(B, "10 LET A=5\n20 PRINT A;\n30 PRINT \"X\"\n", " 5 X\n",
               "trailing semicolon joins lines");
    expect_out(B, "10 PRINT 1,2\n20 PRINT \"ABCDEFGHIJKLMNO\",3\n",
               " 1              2 \nABCDEFGHIJKLMNO                3 \n", "comma zones of 15");
    expect_out(B, "10 PRINT\n20 PRINT ,\"A\"\n", "\n               A\n",
               "empty PRINT and leading comma");
    expect_out(B, "10 FOR I=1 TO 3\n20 PRINT I;\n30 NEXT I\n40 PRINT I\n", " 1  2  3  4 \n",
               "FOR leaves the variable past the limit");
    expect_out(B, "10 FOR I=3 TO 1\n20 PRINT I\n30 NEXT I\n40 PRINT \"SKIP\"\n", "SKIP\n",
               "FOR that runs zero times");
    expect_out(B, "10 FOR I=10 TO 1 STEP -4\n20 PRINT I;\n30 NEXT I\n", " 10  6  2 ",
               "negative STEP");
    expect_out(B, "10 FOR I=1 TO 2\n20 FOR J=1 TO 2\n30 PRINT I*10+J;\n40 NEXT J\n50 NEXT I\n",
               " 11  12  21  22 ", "nested FOR");
    expect_out(B, "10 GOSUB 40\n20 GOSUB 40\n30 STOP\n40 PRINT \"S\";\n50 RETURN\n", "SS",
               "GOSUB twice");
    expect_out(B, "10 ON 3 GOTO 20,30,40\n20 PRINT 1\n30 PRINT 2\n40 PRINT 3\n", " 3 \n",
               "ON GOTO");
    expect_out(B,
               "10 READ A,B,C\n20 PRINT A+B+C\n30 RESTORE\n40 READ D\n50 PRINT D\n60 DATA 1,2\n70 "
               "DATA -3\n",
               " 0 \n 1 \n", "READ, DATA, RESTORE");
    expect_out(B, "10 DIM A(3)\n20 FOR I=0 TO 3\n30 LET A(I)=I*I\n40 NEXT I\n50 PRINT A(3);A(0)\n",
               " 9  0 \n", "DIM base 0");
    expect_out(B,
               "10 OPTION BASE 1\n20 DIM A(2)\n30 LET A(1)=4\n40 LET A(2)=5\n50 PRINT A(1)*A(2)\n",
               " 20 \n", "OPTION BASE 1");
    expect_out(B, "10 LET B(10)=7\n20 PRINT B(10)\n", " 7 \n", "undeclared arrays run 0 to 10");
    expect_out(B, "10 DEF FNS(X)=X*X\n20 DEF FNT(Y)=FNS(Y)+Y\n30 PRINT FNT(3);FNS(FNS(2))\n",
               " 12  16 \n", "DEF FN");
    expect_out(B, "10 PRINT ABS(-3);SGN(-3);SGN(0);INT(7);INT(-7/2);INT(7/2)\n",
               " 3 -1  0  7 -4  3 \n", "ABS SGN INT and exact INT(a/b)");
    expect_out(B,
               "10 IF 1<2 THEN 30\n20 PRINT \"NO\"\n30 IF 2<=2 THEN 50\n40 PRINT \"NO\"\n50 IF "
               "3>=4 THEN 70\n60 PRINT \"Y\"\n70 IF 1<>1 THEN 90\n80 IF 5>4 THEN 100\n90 PRINT "
               "\"NO\"\n100 IF 1=1 THEN 120\n110 PRINT \"NO\"\n120 END\n",
               "Y\n", "relations");
    expect_out(B, "10 let x = 2\n20 print x ^ 10\n30 go to 50\n40 print 0\n50 end\n", " 1024 \n",
               "lower case and GO TO");
    expect_out(B, "10 LET A1=3\n20 LET A=4\n30 PRINT A1*A\n", " 12 \n", "letter-digit names");
    expect_out(B, "10 REM\n20 REM   spaced   \n30 PRINT 1\n\n40 END\n", " 1 \n",
               "REM and blank lines");
    {
        int64_t in[2] = {6, 7};
        int s = run_src(B, "10 INPUT A, B\n20 PRINT A*B\n", in, 2, 0);
        CHECK(s == 0 && strcmp(out, " 42 \n") == 0, "INPUT reads the input list: %d [%s]", s, out);
    }
    expect_twin(B,
                "10 x=((1+2))*3\n20 print \"A\";x,\n30 for i=1 to 9 step 1\n40 next i\n50 GO SUB "
                "70\n60 stop\n70 return\n80 end\n",
                "10 LET X = (1 + 2) * 3\n20 PRINT \"A\"; X,\n30 FOR I = 1 TO 9\n40 NEXT I\n50 "
                "GOSUB 70\n60 STOP\n70 RETURN\n80 END\n");
    expect_twin(B, "0010 PRINT -(-1); 1-(2-3); (-2)^2; -2^2; 2*(-3); +5\n",
                "10 PRINT -(-1); 1 - (2 - 3); (-2) ^ 2; -2 ^ 2; 2 * (-3); 5\n");
    expect_twin(B, "10 PRINT ;;,\n20 ON X GO TO 10 , 20\n30 DATA +1,-2\n",
                "10 PRINT ; ; ,\n20 ON X GOTO 10, 20\n30 DATA 1, -2\n");
    expect_twin(B, "10 OPTION BASE 0\n20 DIM A(5),B(2)\n", "10 OPTION BASE 0\n20 DIM A(5), B(2)\n");

    expect_refused(B, "10 PRINT 1.5\n", ZL_E_UNSUPPORTED, 1, "real literal");
    expect_refused(B, "10 PRINT 1E3\n", ZL_E_UNSUPPORTED, 1, "exponent literal");
    expect_refused(B, "10 PRINT .5\n", ZL_E_UNSUPPORTED, 1, "leading-point literal");
    expect_refused(B, "10 LET A$=\"X\"\n", ZL_E_UNSUPPORTED, 1, "string variable");
    expect_refused(B, "10 PRINT A$\n", ZL_E_UNSUPPORTED, 1, "string variable in an expression");
    expect_refused(B, "10 PRINT SQR(4)\n", ZL_E_UNSUPPORTED, 1, "SQR");
    expect_refused(B, "10 PRINT RND(1)\n", ZL_E_UNSUPPORTED, 1, "RND");
    expect_refused(B, "10 RANDOMIZE\n", ZL_E_UNSUPPORTED, 1, "RANDOMIZE");
    expect_refused(B, "10 DIM A(2,2)\n", ZL_E_UNSUPPORTED, 1, "2-D DIM");
    expect_refused(B, "10 LET A(1,1)=2\n", ZL_E_UNSUPPORTED, 1, "2-D element");
    expect_refused(B, "10 DATA \"X\"\n", ZL_E_UNSUPPORTED, 1, "string DATA");
    expect_refused(B, "10 DEF FNA=1\n", ZL_E_UNSUPPORTED, 1, "DEF without parameter");
    expect_refused(B, "10 PRINT 99999999999999999999\n", ZL_E_UNSUPPORTED, 1,
                   "literal past 64 bits");
    expect_refused(B, "20 PRINT 1\n10 PRINT 2\n", ZL_E_SYNTAX, 2, "line numbers going down");
    expect_refused(B, "10 PRINT 1\n10 PRINT 2\n", ZL_E_SYNTAX, 2, "repeated line number");
    expect_refused(B, "PRINT 1\n", ZL_E_SYNTAX, 1, "no line number");
    expect_refused(B, "0 PRINT 1\n", ZL_E_SYNTAX, 1, "line 0");
    expect_refused(B, "10000 PRINT 1\n", ZL_E_SYNTAX, 1, "line 10000");
    expect_refused(B, "10 GOTO 30\n20 END\n", ZL_E_UNDEFINED, 1, "GOTO a missing line");
    expect_refused(B, "10 IF 1 THEN 10\n", ZL_E_SYNTAX, 1, "IF without a relation");
    expect_refused(B, "10 IF 1<2 GOTO 10\n", ZL_E_SYNTAX, 1, "IF without THEN");
    expect_refused(B, "10 FOR I=1 TO 2\n", ZL_E_SYNTAX, 0, "FOR without NEXT");
    expect_refused(B, "10 NEXT I\n", ZL_E_SYNTAX, 1, "NEXT without FOR");
    expect_refused(B, "10 FOR I=1 TO 2\n20 FOR J=1 TO 2\n30 NEXT I\n", ZL_E_SYNTAX, 3,
                   "crossed NEXT");
    expect_refused(B, "10 END\n20 PRINT 1\n", ZL_E_SYNTAX, 2, "statement after END");
    expect_refused(B, "10 PRINT 1 2\n", ZL_E_SYNTAX, 1, "PRINT items need separators");
    expect_refused(B, "10 PRINT \"ABC\n", ZL_E_SYNTAX, 1, "unterminated string");
    expect_refused(B, "10 LET X=(1+2\n", ZL_E_SYNTAX, 1, "missing ')'");
    expect_refused(B, "10 LET X=\n", ZL_E_SYNTAX, 1, "missing expression");
    expect_refused(B, "10 LET X 1\n", ZL_E_SYNTAX, 1, "missing '='");
    expect_refused(B, "10 FROB X\n", ZL_E_SYNTAX, 1, "unknown statement");
    expect_refused(B, "10 PRINT FNA(1)\n", ZL_E_UNDEFINED, 1, "FN before DEF");
    expect_refused(B, "10 DEF FNA(X)=FNA(X)\n", ZL_E_UNDEFINED, 1, "recursive DEF");
    expect_refused(B, "10 DEF FNA(X)=X\n20 DEF FNA(Y)=Y\n", ZL_E_SYNTAX, 2, "DEF twice");
    expect_refused(B, "10 LET A(1)=1\n20 DIM A(5)\n", ZL_E_SYNTAX, 2, "DIM after use");
    expect_refused(B, "10 DIM A(5)\n20 DIM A(6)\n", ZL_E_SYNTAX, 2, "DIM twice");
    expect_refused(B, "10 LET A=1\n20 LET A(1)=2\n", ZL_E_SYNTAX, 2,
                   "letter as variable then array");
    expect_refused(B, "10 LET A(1)=1\n20 LET A=2\n", ZL_E_SYNTAX, 2,
                   "letter as array then variable");
    expect_refused(B, "10 DIM A(1)\n20 OPTION BASE 1\n", ZL_E_SYNTAX, 2,
                   "OPTION BASE after an array");
    expect_refused(B, "10 OPTION BASE 2\n", ZL_E_SYNTAX, 1, "OPTION BASE 2");
    expect_refused(B, "10 OPTION BASE 0\n20 OPTION BASE 1\n", ZL_E_SYNTAX, 2, "OPTION BASE twice");
    expect_refused(B, "10 LET A1(2)=1\n", ZL_E_SYNTAX, 1, "array name with a digit");
    expect_refused(B, "10 FOR A(1)=1 TO 2\n", ZL_E_SYNTAX, 1, "array loop variable");
    expect_refused(B, "10 PRINT 1 REM\n", ZL_E_SYNTAX, 1, "text after the statement");
    expect_refused(B, "10 DIM A(-1)\n", ZL_E_SYNTAX, 1, "negative DIM");
    expect_refused(B, "10 DIM A(5000)\n", ZL_E_LIMIT, 1, "DIM past the slots");
    {
        static char big[ZL_MAX_LINE + 64];
        memset(big, ' ', sizeof big);
        memcpy(big, "10 PRINT", 8);
        big[sizeof big - 2] = '\n';
        big[sizeof big - 1] = 0;
        expect_refused(B, big, ZL_E_LIMIT, 1, "line too long");
    }

    expect_trap(B, "10 PRINT 1/0\n", ZL_E_DIVZERO, 0, "division by zero");
    expect_trap(B, "10 PRINT 7/2\n", ZL_E_INEXACT, 0, "inexact division");
    expect_trap(B, "10 PRINT 2^(-1)\n", ZL_E_INEXACT, 0, "inexact power");
    expect_trap(B, "10 DIM A(3)\n20 PRINT A(4)\n", ZL_E_BOUNDS, 0, "array bound");
    expect_trap(B, "10 OPTION BASE 1\n20 PRINT A(0)\n", ZL_E_BOUNDS, 0, "OPTION BASE 1 index 0");
    expect_trap(B, "10 RETURN\n", ZL_E_RSTACK, 0, "RETURN without GOSUB");
    expect_trap(B, "10 READ A\n20 READ B\n30 DATA 1\n", ZL_E_INPUT, 0, "out of DATA");
    expect_trap(B, "10 READ A\n", ZL_E_INPUT, 0, "READ with no DATA");
    expect_trap(B, "10 ON 4 GOTO 10,10,10\n", ZL_E_DOMAIN, 0, "ON out of range");
    expect_trap(B, "10 ON 0 GOTO 10\n", ZL_E_DOMAIN, 0, "ON zero");
    expect_trap(B, "10 GOTO 10\n", ZL_E_FUEL, 0, "endless loop");
    expect_trap(B, "10 PRINT 9223372036854775807+1\n", ZL_E_OVERFLOW, 0, "64-bit overflow");
    expect_trap(B, "10 GOSUB 10\n", ZL_E_RSTACK, 0, "GOSUB depth");
    expect_trap(B, "10 INPUT A\n", ZL_E_INPUT, 0, "INPUT with no input");
    expect_trap(B, "10 PRINT 1\n20 STOP\n30 PRINT 2\n", ZL_OK, 0, "STOP");
    CHECK(strcmp(out, " 1 \n") == 0, "STOP stops: [%s]", out);
}

static void test_fortran(void)
{
    const int F = ZL_LANG_FORTRAN;
#define FP(body) "      PROGRAM T\n" body "      END\n"
    expect_out(F, FP("      PRINT *, 1 + 2 * 3, 2 ** 3 ** 2, -2 ** 2, 7 / 2 * 2\n"),
               "           7         512          -4           6\n", "precedence, ** right-assoc");
    expect_out(F, FP("      PRINT *, -7 / 2, MOD(-7, 2), MOD(7, -2), 2 ** (-2), (-1) ** (-3)\n"),
               "          -3          -1           1           0          -1\n",
               "truncating division");
    expect_out(F,
               FP("      PRINT *, ABS(-4), IABS(4), MAX(1, 9, 3), MAX0(2, 1),\n     & MIN(4, -4, "
                  "0), MIN0(3, 2), ISIGN(3, -2), ISIGN(-3, 0)\n"),
               "           4           4           9           2          -4           2          "
               "-3           3\n",
               "intrinsics");
    expect_out(F,
               FP("      PRINT *, 'A', 'B', 1, 'C'\n      PRINT *\n      WRITE (6, *) 'W'\n      "
                  "WRITE(*,*)\n"),
               " AB           1 C\n\n W\n\n", "list-directed layout");
    expect_out(F, "      DO 10 I = 1, 3\n   10 PRINT *, I\n      PRINT *, I\n      END\n",
               "           1\n           2\n           3\n           4\n",
               "DO terminal statement and final value");
    expect_out(F, "      DO 10 I = 3, 1\n   10 PRINT *, I\n      PRINT *, I\n      END\n",
               "           3\n", "zero-trip DO");
    expect_out(F, "      DO 10 I = 10, 1, -3\n   10 PRINT *, I\n      END\n",
               "          10\n           7\n           4\n           1\n", "negative DO step");
    expect_out(F,
               "      DO 10 I = 1, 2\n      DO 10 J = 1, 2\n   10 PRINT *, I * 10 + J\n      END\n",
               "          11\n          12\n          21\n          22\n", "shared DO terminal");
    expect_out(F,
               "      N = 3\n      DO 10 I = 1, N\n      N = 1\n   10 CONTINUE\n      PRINT *, I\n "
               "     END\n",
               "           4\n", "trip count fixed at the start");
    expect_out(F,
               "      K = 0\n      DO 10 I = 1, 5\n      IF (I .EQ. 3) GOTO 10\n      K = K + I\n  "
               " 10 CONTINUE\n      PRINT *, K\n      END\n",
               "          12\n", "GOTO the terminal acts as cycle");
    expect_out(F,
               "      I = 2\n      IF (I .GT. 1 .AND. I .LT. 3 .OR. .NOT. I .EQ. 2) PRINT *, 'Y'\n"
               "      IF (I == 2 .AND. I /= 3 .AND. I <= 2 .AND. I >= 2\n     & .AND. I < 3 .AND. "
               "I > 1) PRINT *, 'Z'\n      END\n",
               " Y\n Z\n", "logical operators");
    expect_out(F,
               "      DO 20 I = -1, 1\n      IF (I) 10, 15, 18\n   10 PRINT *, 'N'\n      GOTO "
               "20\n   15 PRINT *, 'Z'\n"
               "      GOTO 20\n   18 PRINT *, 'P'\n   20 CONTINUE\n      END\n",
               " N\n Z\n P\n", "arithmetic IF");
    expect_out(F,
               "      DO 20 I = 0, 3\n      GO TO (5, 10), I\n      PRINT *, 'F'\n      GOTO 20\n  "
               "  5 PRINT *, 'A'\n      GOTO 20\n"
               "   10 PRINT *, 'B'\n   20 CONTINUE\n      END\n",
               " F\n A\n B\n F\n", "computed GOTO falls through out of range");
    expect_out(F,
               "      DO 10 I = 1, 4\n      IF (I .EQ. 1) THEN\n      PRINT *, 'ONE'\n      ELSE "
               "IF (I .EQ. 2) THEN\n"
               "      PRINT *, 'TWO'\n      ELSE IF (I .EQ. 3) THEN\n      PRINT *, 'THREE'\n      "
               "ELSE\n      PRINT *, 'MANY'\n      END IF\n"
               "   10 CONTINUE\n      END\n",
               " ONE\n TWO\n THREE\n MANY\n", "block IF chain");
    expect_out(F, "      IF (1 .EQ. 2) THEN\n      PRINT *, 'X'\n      ENDIF\n      END\n", "",
               "block IF not taken, ENDIF spelling");
    expect_out(F,
               "      J = 1\n      DO WHILE (J .LT. 50)\n      J = J * 2\n      END DO\n      "
               "PRINT *, J\n      END\n",
               "          64\n", "DO WHILE");
    expect_out(F,
               "      IMPLICIT INTEGER (A-H, O-Z)\n      ALPHA = 3\n      ZED = 4\n      PRINT *, "
               "ALPHA * ZED\n      END\n",
               "          12\n", "IMPLICIT INTEGER");
    expect_out(F,
               "      IMPLICIT NONE\n      INTEGER K\n      PARAMETER (K = 2 * 3 + 1)\n      "
               "INTEGER A(K), B(-2:2)\n"
               "      A(K) = K\n      B(-2) = -2\n      PRINT *, A(7), B(-2), K\n      END\n",
               "           7          -2           7\n", "IMPLICIT NONE, PARAMETER and bounds");
    expect_out(F,
               "      INTEGER A\n      DIMENSION A(3)\n      DIMENSION M(0:1)\n      A(3) = 1\n    "
               "  M(1) = 2\n      PRINT *, A(3) + M(1)\n      END\n",
               "           3\n", "DIMENSION");
    expect_out(F, "C comment\n* star\n! bang\n\n      PRINT *, 'IT''S' ! trailing\n      END\n",
               " IT'S\n", "comment forms");
    expect_out(F, "      PRINT *, 'ABC\n     1DEF'\n      END\n",
               " ABC                                                     DEF\n",
               "continued constant pads to column 72 (as gfortran)");
    expect_out(F, "      I = 1 +\n     &2 +\n     $3\n      PRINT *, I\n      END\n",
               "           6\n", "continuation lines");
    expect_out(F,
               "      I = 1                                                             99999999\n "
               "     PRINT *, I\n      END\n",
               "           1\n", "columns past 72 ignored");
    expect_out(F, "\tI = 5\n\tPRINT *, I\n\tEND\n", "           5\n", "tab form");
    expect_out(F, "      D O 1 0 I = 1 , 2\n   10 P R I N T *, I\n      E N D\n",
               "           1\n           2\n", "blanks are not significant");
    expect_out(F, "      IF = 3\n      PRINT *, IF\n      END\n", "           3\n",
               "no reserved words: a variable named IF");
    expect_out(F, "      I = 1 2\n      PRINT *, I\n      END\n", "          12\n",
               "blanks inside a constant do not count");
    {
        int64_t in[3] = {5, 6, 7};
        int s = run_src(F,
                        "      INTEGER A(2)\n      READ *, I, A(1)\n      READ (*, *) A(2)\n      "
                        "PRINT *, I + A(1) + A(2)\n      END\n",
                        in, 3, 0);
        CHECK(s == 0 && strcmp(out, "          18\n") == 0, "READ: %d [%s]", s, out);
    }
    expect_twin(F,
                "c lower comment\n      program p\n      integer a(2)\n      do 10 i=1,2\n   10 "
                "a(i)=i**2\n      print*,'x',a(2)\n      if(i.eq.3)then\n"
                "      goto (10,20),i\n      else\n      endif\n   20 continue\n      end\n",
                "C lower comment\n      PROGRAM P\n      INTEGER A(2)\n      DO 10 I = 1, 2\n10    "
                "  A(I) = I ** 2\n      PRINT *, 'x', A(2)\n"
                "      IF (I .EQ. 3) THEN\n        GOTO (10, 20), I\n      ELSE\n      END IF\n20  "
                "  CONTINUE\n      END\n");
    expect_twin(F, "      I = ((1 + 2)) * (3 - (4 - 5)) + (-6) - (-7) ** 2\n      END\n",
                "      I = (1 + 2) * (3 - (4 - 5)) + (-6) - (-7) ** 2\n      END\n");
    expect_twin(F, "      I = 1 ! one\n      J = 2\n     &+3\n* two\n      END\n",
                "      I = 1\nC one\n      J = 2 + 3\nC two\n      END\n");
    /* column 73 of the first line is cut; the twin re-splits at 66 columns */
    expect_twin(F,
                "      PRINT *, 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n     "
                "&AAAAAAAAAAAA'\n      END\n",
                "      PRINT *, '"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "\n     &AAAAAAAAAAAA'\n      END\n");

    expect_refused(F, FP("      REAL X\n"), ZL_E_UNSUPPORTED, 2, "REAL declaration");
    expect_refused(F, FP("      X = 1\n"), ZL_E_UNSUPPORTED, 2, "implicitly REAL name");
    expect_refused(F, FP("      I = 1.5\n"), ZL_E_UNSUPPORTED, 2, "real constant");
    expect_refused(F, FP("      I = 1E5\n"), ZL_E_UNSUPPORTED, 2, "exponent constant");
    expect_refused(F, FP("      DO10K=1.10\n"), ZL_E_UNSUPPORTED, 2,
                   "the DO10I=1.10 assignment to a REAL");
    expect_refused(F, FP("      I = SQRT(4)\n"), ZL_E_UNSUPPORTED, 2, "real intrinsic");
    expect_refused(F, FP("      I = FOO(4)\n"), ZL_E_UNSUPPORTED, 2, "function reference");
    expect_refused(F, FP("      CALL FOO\n"), ZL_E_UNSUPPORTED, 2, "CALL");
    expect_refused(F, FP("      CHARACTER*8 S\n"), ZL_E_UNSUPPORTED, 2, "CHARACTER");
    expect_refused(F, FP("      COMMON /B/ I\n"), ZL_E_UNSUPPORTED, 2, "COMMON");
    expect_refused(F, FP("      DATA I /1/\n"), ZL_E_UNSUPPORTED, 2, "DATA");
    expect_refused(F, FP("      INTEGER*8 I\n"), ZL_E_UNSUPPORTED, 2, "INTEGER*8");
    expect_refused(F, FP("      INTEGER A(2,2)\n"), ZL_E_UNSUPPORTED, 2, "2-D array");
    expect_refused(F, FP("      INTEGER A(2)\n      A(1,1) = 1\n"), ZL_E_UNSUPPORTED, 3,
                   "2-D reference");
    expect_refused(F, FP("      PRINT 10, I\n"), ZL_E_UNSUPPORTED, 2, "PRINT with FORMAT");
    expect_refused(F, FP("      INTEGER A(2)\n      PRINT *, (A(I), I = 1, 2)\n"), ZL_E_UNSUPPORTED,
                   3, "implied DO");
    expect_refused(F, FP("      WRITE (7, *) I\n"), ZL_E_UNSUPPORTED, 2, "WRITE to unit 7");
    expect_refused(F, FP("      STOP 1\n"), ZL_E_UNSUPPORTED, 2, "STOP code");
    expect_refused(F, FP("      IMPLICIT REAL (A-Z)\n"), ZL_E_UNSUPPORTED, 2, "IMPLICIT REAL");
    expect_refused(F, FP("      I = .TRUE.\n"), ZL_E_UNSUPPORTED, 2, "LOGICAL constant");
    expect_refused(F, FP("      I = 3000000000\n"), ZL_E_UNSUPPORTED, 2, "constant past 32 bits");
    expect_refused(F, FP("      I = 2147483648\n"), ZL_E_UNSUPPORTED, 2, "2^31");
    expect_refused(F, FP("      PRINT *, 'S' + 1\n"), ZL_E_SYNTAX, 2, "string in arithmetic");
    expect_refused(F, "      PRINT *, 1\n", ZL_E_SYNTAX, 0, "missing END");
    expect_refused(F, FP("      GOTO 99\n"), ZL_E_UNDEFINED, 2, "GOTO a missing label");
    expect_refused(F, FP("   10 I = 1\n   10 J = 2\n"), ZL_E_SYNTAX, 3, "label twice");
    expect_refused(F, FP("      DO 10 I = 1, 2\n"), ZL_E_SYNTAX, 0, "DO without its terminal");
    expect_refused(
        F, FP("      DO 10 I = 1, 2\n      DO 20 J = 1, 2\n   10 CONTINUE\n   20 CONTINUE\n"),
        ZL_E_SYNTAX, 4, "crossed DO");
    expect_refused(F, FP("      DO 10 I = 1, 2\n   10 GOTO 10\n"), ZL_E_SYNTAX, 3,
                   "GOTO as a DO terminal");
    expect_refused(F, FP("      DO 10 I = 1, 2\n   10 IF (I) 10, 10, 10\n"), ZL_E_SYNTAX, 3,
                   "arithmetic IF as a DO terminal");
    expect_refused(F, FP("      I = 1\n      INTEGER J\n"), ZL_E_SYNTAX, 3,
                   "declaration after code");
    expect_refused(F, FP("   10 INTEGER J\n"), ZL_E_SYNTAX, 2, "labelled declaration");
    expect_refused(F, FP("      IMPLICIT NONE\n      K = 1\n"), ZL_E_UNDEFINED, 3,
                   "undeclared under IMPLICIT NONE");
    expect_refused(F, FP("      INTEGER I\n      INTEGER I\n"), ZL_E_SYNTAX, 3, "INTEGER twice");
    expect_refused(F, FP("      PARAMETER (N = 1)\n      N = 2\n"), ZL_E_SYNTAX, 3,
                   "assignment to a PARAMETER");
    expect_refused(F, FP("      PARAMETER (N = M)\n"), ZL_E_SYNTAX, 2, "PARAMETER from a variable");
    expect_refused(F, FP("      PARAMETER (N = 1 / 0)\n"), ZL_E_DIVZERO, 2,
                   "PARAMETER division by zero");
    expect_refused(F, FP("      PARAMETER (N = 65536 * 65536)\n"), ZL_E_OVERFLOW, 2,
                   "PARAMETER overflow");
    expect_refused(F, FP("      ELSE\n"), ZL_E_SYNTAX, 2, "ELSE without IF");
    expect_refused(F, FP("      END IF\n"), ZL_E_SYNTAX, 2, "END IF without IF");
    expect_refused(F, FP("      END DO\n"), ZL_E_SYNTAX, 2, "END DO without DO");
    expect_refused(F, FP("      IF (1 .EQ. 1) THEN\n"), ZL_E_SYNTAX, 3, "END inside an IF block");
    expect_refused(F, FP("      IF (1 .EQ. 1) THEN\n      ELSE\n      ELSE\n      END IF\n"),
                   ZL_E_SYNTAX, 4, "two ELSEs");
    expect_refused(F, FP("      IF (1) PRINT *, 1\n"), ZL_E_SYNTAX, 2, "logical IF on an INTEGER");
    expect_refused(F, FP("      IF (1 .EQ. 1) IF (1 .EQ. 1) I = 1\n"), ZL_E_SYNTAX, 2,
                   "nested logical IF");
    expect_refused(F, FP("      I = 1 .EQ. 1\n"), ZL_E_SYNTAX, 2, "LOGICAL into INTEGER");
    expect_refused(F, FP("      IF (1 .AND. 2) I = 1\n"), ZL_E_SYNTAX, 2, ".AND. on integers");
    expect_refused(F, FP("      I = J + (1 .EQ. 1)\n"), ZL_E_SYNTAX, 2, "arithmetic on LOGICAL");
    expect_refused(F, "     1I = 1\n      END\n", ZL_E_SYNTAX, 1,
                   "continuation with nothing before");
    expect_refused(F, FP("   1XI = 1\n"), ZL_E_SYNTAX, 2, "letters in the label field");
    expect_refused(F, FP("   12\n"), ZL_E_SYNTAX, 2, "label with no statement");
    expect_refused(F, FP("      PRINT *, 'ABC\n"), ZL_E_SYNTAX, 2, "unterminated constant");
    expect_refused(F, FP("      I = 1 + + 2\n"), ZL_E_SYNTAX, 2, "two operators");
    expect_refused(F, FP("      FROB\n"), ZL_E_SYNTAX, 2, "unknown statement");
    expect_refused(F, "      PRINT *, 1\n      PROGRAM T\n      END\n", ZL_E_SYNTAX, 2,
                   "PROGRAM not first");
    expect_refused(F, FP("      END\n"), ZL_E_SYNTAX, 3, "statement after END");
    expect_refused(F, "      END PROGRAM\n", ZL_E_UNSUPPORTED, 1, "END PROGRAM");
    expect_refused(F, FP("      INTEGER A(5:1)\n"), ZL_E_LIMIT, 2, "empty bounds");
    expect_refused(F, FP("      ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFG = 1\n"), ZL_E_SYNTAX, 2,
                   "name past 31 characters");

    expect_trap(F, FP("      I = 2147483647\n      I = I + 1\n"), ZL_E_OVERFLOW, 3,
                "32-bit overflow");
    expect_trap(F, FP("      I = 65536 * 32768\n"), ZL_E_OVERFLOW, 2, "32-bit product");
    expect_trap(F, FP("      J = 0\n      I = 1 / J\n"), ZL_E_DIVZERO, 3, "division by zero");
    expect_trap(F, FP("      J = 0\n      I = MOD(1, J)\n"), ZL_E_DIVZERO, 3, "MOD by zero");
    expect_trap(F, FP("      INTEGER A(3)\n      I = 4\n      A(I) = 1\n"), ZL_E_BOUNDS, 4,
                "array store bound");
    expect_trap(F, FP("      INTEGER A(0:3)\n      I = -1\n      J = A(I)\n"), ZL_E_BOUNDS, 4,
                "array load below its lower bound");
    expect_trap(F, FP("      K = 0\n      DO 10 I = 1, 2, K\n   10 CONTINUE\n"), ZL_E_DOMAIN, 3,
                "zero DO step");
    expect_trap(F, FP("   10 GOTO 10\n"), ZL_E_FUEL, 0, "endless loop");
    expect_trap(F, FP("      READ *, I\n"), ZL_E_INPUT, 2, "READ with no input");
    expect_trap(F, FP("      PRINT *, 1\n      STOP\n      PRINT *, 2\n"), ZL_OK, 0, "STOP");
    CHECK(strcmp(out, "           1\n") == 0, "STOP stops: [%s]", out);
#undef FP
}

static void test_forth(void)
{
    const int Q = ZL_LANG_FORTH;
    expect_out(Q, "1 2 + . 7 3 - . 6 7 * . -7 2 / . -7 2 MOD . 7 -2 /MOD . . cr",
               "3 4 42 -4 1 -4 -1 \n", "arithmetic");
    expect_out(Q, "5 NEGATE . -5 ABS . 3 9 MIN . 3 9 MAX . 4 1+ . 4 1- . 4 2* . -7 2/ .",
               "-5 5 3 9 5 3 8 -4 ", "unary words");
    expect_out(Q,
               "1 1 = . 1 2 = . 1 2 <> . 1 2 < . 2 1 > . 0 0= . 5 0= . -1 0< . 1 0> . 3 0<> . TRUE "
               ". FALSE .",
               "-1 0 -1 -1 -1 -1 0 -1 -1 -1 -1 0 ", "comparisons give -1 and 0");
    expect_out(Q, "12 10 AND . 12 10 OR . 12 10 XOR . 0 INVERT .", "8 14 6 -1 ", "bitwise");
    expect_out(Q,
               "1 2 3 ROT . . . 1 2 OVER . . . 1 2 SWAP . . 1 2 NIP . 1 2 TUCK . . . 1 2 2DUP . . "
               ". . 1 2 3 2DROP . 4 DUP . . 5 6 DROP .",
               "1 3 2 1 2 1 1 2 2 2 1 2 2 1 2 1 1 4 4 5 ", "stack words");
    expect_out(
        Q,
        "65 EMIT SPACE 66 EMIT 3 SPACES 67 EMIT 0 SPACES -2 SPACES CR .\" hi there\" .( and this)",
        "A B   C\nhi thereand this", "character output");
    expect_out(Q, ": SQ DUP * ; : CUBE DUP SQ * ; 3 CUBE .", "27 ", "words calling words");
    expect_out(Q, ": A 1 ; : B A 10 + ; : A 2 ; B . A .", "11 2 ",
               "redefinition keeps old bindings");
    expect_out(Q, ": T IF 1 ELSE 2 THEN . ; 0 T 5 T : U IF 3 . THEN ; 0 U 1 U", "2 1 3 ",
               "IF ELSE THEN");
    expect_out(Q,
               ": C 0 BEGIN 1+ DUP 5 = UNTIL . ; C : W 3 BEGIN DUP WHILE DUP . 1- REPEAT DROP ; W",
               "5 3 2 1 ", "BEGIN loops");
    expect_out(Q, ": L 5 0 DO I . LOOP ; L : M 3 0 DO 2 0 DO J . I . LOOP LOOP ; M",
               "0 1 2 3 4 0 0 0 1 1 0 1 1 2 0 2 1 ", "DO LOOP, I and J");
    expect_out(
        Q, ": P 10 0 DO I . 4 +LOOP ; P : N 0 10 DO I . -4 +LOOP ; N : Z 0 9 DO I . -3 +LOOP ; Z",
        "0 4 8 10 6 2 9 6 3 0 ", "+LOOP both ways");
    expect_out(Q,
               ": E 10 0 DO I DUP . 3 = IF LEAVE THEN LOOP ; E : X 10 0 DO I 2 = IF UNLOOP EXIT "
               "THEN I . LOOP ; X",
               "0 1 2 3 0 1 ", "LEAVE and EXIT");
    expect_out(Q, ": F DUP 1 > IF DUP 1- RECURSE * THEN ; 20 F .", "2432902008176640000 ",
               "RECURSE");
    expect_out(Q, "VARIABLE V 7 V ! V @ . 3 V +! V ? 10 CONSTANT K K K * .", "7 10 100 ",
               "variables and constants");
    expect_out(Q, "CREATE T 3 CELLS ALLOT 5 T 2 CELLS + ! T 2 CELLS + @ . T CELL+ @ .", "5 0 ",
               "CREATE ALLOT");
    expect_out(Q, "CREATE T 3 ALLOT VARIABLE V 1 V ! T 3 + @ .", "1 ",
               "ALLOT without CELLS sizes cells");
    expect_out(Q, "5 ?DUP . . 0 ?DUP .", "5 5 0 ", "?DUP");
    expect_out(Q, "( a comment ) 1 . \\ to the end\n 2 . BYE 3 .", "1 2 ", "comments and BYE");
    expect_out(Q, ": sq dup * ; 4 SQ . 5 Sq .", "16 25 ", "names ignore case");
    expect_twin(Q, "\\  head   comment  \n: sq ( n -- n*n )   dup * ;  3 sq\n.  cr 1 2\n+ .",
                "\\ head   comment\n: SQ ( n -- n*n ) DUP * ;\n3 SQ . CR\n1 2 + .\n");
    expect_twin(Q, ".\"  two  spaces\" 10 constant ten variable v create t 2 cells allot",
                ".\"  two  spaces\" 10 CONSTANT TEN VARIABLE V CREATE T 2 CELLS ALLOT\n");

    expect_refused(Q, "IF", ZL_E_SYNTAX, 1, "IF outside a definition");
    expect_refused(Q, "5 0 DO I . LOOP", ZL_E_SYNTAX, 1, "DO outside a definition");
    expect_refused(Q, ";", ZL_E_SYNTAX, 1, "; outside a definition");
    expect_refused(Q, ": A : B ;", ZL_E_SYNTAX, 1, "nested :");
    expect_refused(Q, ": A 1", ZL_E_SYNTAX, 0, "unended definition");
    expect_refused(Q, ": A IF ;", ZL_E_SYNTAX, 1, "unfinished IF");
    expect_refused(Q, ": A THEN ;", ZL_E_SYNTAX, 1, "THEN without IF");
    expect_refused(Q, ": A ELSE ;", ZL_E_SYNTAX, 1, "ELSE without IF");
    expect_refused(Q, ": A UNTIL ;", ZL_E_SYNTAX, 1, "UNTIL without BEGIN");
    expect_refused(Q, ": A REPEAT ;", ZL_E_SYNTAX, 1, "REPEAT without WHILE");
    expect_refused(Q, ": A WHILE ;", ZL_E_SYNTAX, 1, "WHILE without BEGIN");
    expect_refused(Q, ": A LOOP ;", ZL_E_SYNTAX, 1, "LOOP without DO");
    expect_refused(Q, ": A I ;", ZL_E_SYNTAX, 1, "I outside DO");
    expect_refused(Q, ": A 1 0 DO J LOOP ;", ZL_E_SYNTAX, 1, "J with one loop");
    expect_refused(Q, ": A LEAVE ;", ZL_E_SYNTAX, 1, "LEAVE outside DO");
    expect_refused(Q, ": A UNLOOP ;", ZL_E_SYNTAX, 1, "UNLOOP outside DO");
    expect_refused(Q, ": A 1 0 DO RECURSE LOOP ;", ZL_E_UNSUPPORTED, 1, "RECURSE with DO");
    expect_refused(Q, "FROB", ZL_E_UNDEFINED, 1, "undefined word");
    expect_refused(Q, "1\n2\nFROB", ZL_E_UNDEFINED, 3, "line of an undefined word");
    expect_refused(Q, "1.5 .", ZL_E_UNSUPPORTED, 1, "float literal");
    expect_refused(Q, "HEX", ZL_E_UNSUPPORTED, 1, "HEX");
    expect_refused(Q, ">R", ZL_E_UNSUPPORTED, 1, "return stack words");
    expect_refused(Q, "99999999999999999999 .", ZL_E_UNSUPPORTED, 1, "number past 64 bits");
    expect_refused(Q, "-99999999999999999999 .", ZL_E_UNSUPPORTED, 1,
                   "negative number past 64 bits");
    expect_refused(Q, "VARIABLE X CONSTANT Y", ZL_E_UNSUPPORTED, 1, "CONSTANT without a literal");
    expect_refused(Q, ": A 5 CONSTANT B ;", ZL_E_UNSUPPORTED, 1, "CONSTANT inside a definition");
    expect_refused(Q, "VARIABLE V 4 ALLOT", ZL_E_UNSUPPORTED, 1, "ALLOT after VARIABLE");
    expect_refused(Q, "CREATE T DUP ALLOT", ZL_E_UNSUPPORTED, 1, "ALLOT without a literal");
    expect_refused(Q, "CREATE T 0 ALLOT", ZL_E_LIMIT, 1, "ALLOT 0");
    expect_refused(Q, "CREATE T 5000 ALLOT", ZL_E_LIMIT, 1, "ALLOT too big");
    expect_refused(Q, ".\" open", ZL_E_SYNTAX, 1, "unterminated .\"");
    expect_refused(Q, ": A .( x) ;", ZL_E_UNSUPPORTED, 1, ".( inside a definition");
    expect_refused(Q, "( open", ZL_E_SYNTAX, 0, "unterminated ( comment");
    expect_refused(Q, ":", ZL_E_SYNTAX, 1, ": without a name");
    expect_refused(Q, "VARIABLE", ZL_E_SYNTAX, 1, "VARIABLE without a name");
    expect_refused(Q, ": ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFG ;", ZL_E_SYNTAX, 1,
                   "name past 31 characters");

    expect_trap(Q, ".", ZL_E_STACK, 1, "stack underflow");
    expect_trap(Q, "1 0 /", ZL_E_DIVZERO, 1, "division by zero");
    expect_trap(Q, "300 EMIT", ZL_E_DOMAIN, 1, "EMIT past a byte");
    expect_trap(Q, ": F DUP 1 > IF DUP 1- RECURSE * THEN ; 25 F", ZL_E_OVERFLOW, 1,
                "factorial overflow");
    expect_trap(Q, ": F DUP IF 1- RECURSE THEN ; 2000 F", ZL_E_RSTACK, 1,
                "recursion past the call depth");
    expect_trap(Q, ": D RECURSE ; D", ZL_E_RSTACK, 1, "unbounded recursion");
    expect_trap(Q, ": G BEGIN AGAIN ; G", ZL_E_FUEL, 1, "endless loop");
    expect_trap(Q, "-1 @", ZL_E_BOUNDS, 1, "fetch from a bad address");
    expect_trap(Q, "9223372036854775807 1 +", ZL_E_OVERFLOW, 1, "64-bit overflow");
}

static void test_api(void)
{
    zl_diag_t d;
    size_t n;
    CHECK(zl_compile(ZL_LANG_BASIC, "", 0, &P, &work, 0) == ZL_E_ARG, "NULL diag");
    CHECK(zl_compile(ZL_LANG_BASIC, "", 0, 0, &work, &d) == ZL_E_ARG && d.status == ZL_E_ARG,
          "NULL program");
    CHECK(zl_compile(ZL_LANG_BASIC, "", 0, &P, 0, &d) == ZL_E_ARG, "NULL work");
    CHECK(zl_compile(ZL_LANG_BASIC, 0, 5, &P, &work, &d) == ZL_E_ARG, "NULL source with a length");
    CHECK(zl_compile(0, "", 0, &P, &work, &d) == ZL_E_ARG, "language 0");
    CHECK(zl_compile(ZL_LANG__COUNT, "", 0, &P, &work, &d) == ZL_E_ARG, "language past the end");
    CHECK(zl_compile(ZL_LANG_BASIC | 0x200, "", 0, &P, &work, &d) == ZL_E_ARG, "unknown flag bits");
    CHECK(zl_compile(ZL_LANG_BASIC, 0, 0, &P, &work, &d) == ZL_OK, "empty BASIC program is fine");
    CHECK(run_p(&P, 0, 0, 0) == ZL_OK && R.out_len == 0, "and runs");
    CHECK(zl_compile(ZL_LANG_FORTH, "", 0, &P, &work, &d) == ZL_OK, "empty Forth program");
    CHECK(zl_compile(ZL_LANG_FORTRAN, "", 0, &P, &work, &d) == ZL_E_SYNTAX,
          "empty FORTRAN program lacks END");
    CHECK(zl_twin(ZL_LANG_BASIC, "", 0, 0, 10, &n, &work, &d) == ZL_E_ARG, "twin NULL out");
    CHECK(zl_twin(ZL_LANG_BASIC, "", 0, tw, 0, &n, &work, &d) == ZL_E_ARG, "twin zero cap");
    CHECK(zl_twin(ZL_LANG_BASIC, "", 0, tw, 10, 0, &work, &d) == ZL_E_ARG, "twin NULL length");
    CHECK(zl_twin(ZL_LANG_BASIC, "", 0, tw, 10, &n, 0, &d) == ZL_E_ARG, "twin NULL work");
    CHECK(zl_twin(ZL_LANG_BASIC, "", 0, tw, 10, &n, &work, 0) == ZL_E_ARG, "twin NULL diag");
    CHECK(zl_twin(ZL_LANG__COUNT, "", 0, tw, 10, &n, &work, &d) == ZL_E_ARG, "twin bad language");
    CHECK(zl_twin(ZL_LANG_BASIC | ZL_TWIN, "10 END\n", 7, tw, 10, &n, &work, &d) == ZL_OK && n == 7,
          "twin accepts the twin flag");
    CHECK(run_p(0, 0, 0, 0) == ZL_E_ARG, "run NULL program");
    CHECK(zl_run(&P, 0) == ZL_E_ARG, "run NULL state");
    memset(&R, 0, sizeof R);
    R.out_cap = 4;
    CHECK(zl_run(&P, &R) == ZL_E_ARG, "run with a capacity but no buffer");
    memset(&R, 0, sizeof R);
    R.nin = 2;
    CHECK(zl_run(&P, &R) == ZL_E_ARG, "run with inputs but no array");
    CHECK(strcmp(zl_lang_name(ZL_LANG_FORTRAN), "FORTRAN") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_BASIC), "BASIC") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_FORTH | ZL_TWIN), "Forth") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_PASCAL), "Pascal") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_ALGOL60), "ALGOL 60") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_LISP), "LISP") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_SCHEME), "Scheme") == 0 &&
              strcmp(zl_lang_name(ZL_LANG_LOGO), "Logo") == 0 &&
              strcmp(zl_lang_name(0), "unknown") == 0,
          "language names");
    CHECK(strcmp(zl_twin_name(ZL_LANG_FORTRAN), "Reckonwyrm") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_BASIC), "Hatchling") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_FORTH), "Cairndrake") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_PASCAL), "Tutordrake") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_ALGOL60), "Rootwyrm") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_LISP), "Coilwyrm") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_SCHEME), "Spiraldrake") == 0 &&
              strcmp(zl_twin_name(ZL_LANG_LOGO | ZL_TWIN), "Kiteling") == 0 &&
              strcmp(zl_twin_name(ZL_LANG__COUNT), "unknown") == 0,
          "twin names");
    int st[] = {ZL_OK,        ZL_E_ARG,       ZL_E_SYNTAX, ZL_E_UNSUPPORTED, ZL_E_NONCANON,
                ZL_E_LIMIT,   ZL_E_UNDEFINED, ZL_E_VERIFY, ZL_E_OVERFLOW,    ZL_E_DIVZERO,
                ZL_E_INEXACT, ZL_E_BOUNDS,    ZL_E_STACK,  ZL_E_RSTACK,      ZL_E_FUEL,
                ZL_E_OUTPUT,  ZL_E_INPUT,     ZL_E_DOMAIN};
    for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
        CHECK(strcmp(zl_status_name(st[i]), "unknown status") != 0, "status %d has a name", st[i]);
        for (size_t j = 0; j < i; j++)
            CHECK(strcmp(zl_status_name(st[i]), zl_status_name(st[j])) != 0, "names %d %d differ",
                  st[i], st[j]);
    }
    CHECK(strcmp(zl_status_name(-1), "unknown status") == 0, "unknown status");
    /* limits: too much code, too many strings */
    {
        static char big[1 << 20];
        size_t k = 0;
        for (int i = 0; i < 3000; i++) k += (size_t) sprintf(big + k, "1 2 + DROP ");
        CHECK(zl_compile(ZL_LANG_FORTH, big, k, &P, &work, &d) == ZL_E_LIMIT, "code table limit");
        k = 0;
        for (int i = 0; i < ZL_MAX_STR + 1; i++) k += (size_t) sprintf(big + k, ".\" s%d\" ", i);
        CHECK(zl_compile(ZL_LANG_FORTH, big, k, &P, &work, &d) == ZL_E_LIMIT, "string table limit");
        k = 0;
        for (int i = 0; i < ZL_MAX_SLOTS + 1; i++)
            k += (size_t) sprintf(big + k, "VARIABLE V%d ", i);
        CHECK(zl_compile(ZL_LANG_FORTH, big, k, &P, &work, &d) == ZL_E_LIMIT, "slot limit");
        k = (size_t) sprintf(big, "10 PRINT ");
        for (int i = 0; i < ZL_MAX_DEPTH + 2; i++) big[k++] = '(';
        big[k++] = '1';
        for (int i = 0; i < ZL_MAX_DEPTH + 2; i++) big[k++] = ')';
        big[k++] = '\n';
        CHECK(zl_compile(ZL_LANG_BASIC, big, k, &P, &work, &d) == ZL_E_LIMIT,
              "BASIC nesting limit");
        k = (size_t) sprintf(big, "      I = ");
        for (int i = 0; i < ZL_MAX_DEPTH + 2; i++) big[k++] = '(';
        big[k++] = '1';
        for (int i = 0; i < ZL_MAX_DEPTH + 2; i++) big[k++] = ')';
        k += (size_t) sprintf(big + k, "\n      END\n");
        CHECK(zl_compile(ZL_LANG_FORTRAN, big, k, &P, &work, &d) != ZL_OK, "FORTRAN nesting limit");
        k = (size_t) sprintf(big, ": A ");
        for (int i = 0; i < ZL_MAX_DEPTH + 1; i++) k += (size_t) sprintf(big + k, "BEGIN ");
        CHECK(zl_compile(ZL_LANG_FORTH, big, k, &P, &work, &d) == ZL_E_LIMIT,
              "Forth nesting limit");
    }
}

#include "test_zl_langs.inc"

/* ------------------------------------------------------------ golden files */

static size_t slurp(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return (size_t) -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

/* The language a golden file's extension names (zl_langs.def), or 0. */
static int golden_lang(const char *name)
{
    static const struct {
        int lang;
        const char *ext;
    } langs[] = {
#define ZL_LANG_X(id, n, lname, twin, fe, ext, rt) {ZL_LANG_##id, ext},
#include "zl_langs.def"
#undef ZL_LANG_X
    };
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    size_t n = strlen(dot);
    for (size_t i = 0; i < sizeof langs / sizeof langs[0]; i++)
        for (const char *e = langs[i].ext; *e;) {
            size_t k = strcspn(e, " ");
            if (k == n && !strncmp(e, dot, n)) return langs[i].lang;
            e += k + (e[k] == ' ');
        }
    return 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/* A golden's input, <name>.in next to it: integers, one per line (what the
 * reference tool reads on stdin). No file: no input. */
static size_t golden_in(const char *path, int64_t *in, size_t cap)
{
    static char text[1 << 12];
    size_t n = slurp(path, text, sizeof text), k = 0;
    if (n == (size_t) -1) return 0;
    for (char *p = text, *e; *p && k < cap; p = e + 1) {
        in[k++] = strtoll(p, &e, 10);
        CHECK(e != p && *e == '\n', "%s: one integer per line", path);
        if (e == p || !*e) break;
    }
    return k;
}

/* Every program in the golden folder (any extension zl_langs.def knows)
 * compiles, runs to its .out byte for byte (given its .in, if any) and
 * round-trips through its twin; every collatz program prints the same 40
 * numbers. */
static void test_golden(const char *dir)
{
    enum { NG = 512 };
    static char names[NG][256];
    static const char *g[NG];
    static char src[1 << 16], want[1 << 16];
    static int64_t traces[NG][64], in[256];
    size_t tn[NG] = {0}, ng = 0;
    char path[512];
    int ti = 0, ncollatz = 0, langs_seen[ZL_LANG__COUNT] = {0};
    DIR *dp = opendir(dir);
    CHECK(dp != 0, "open %s", dir);
    if (!dp) return;
    for (struct dirent *e; (e = readdir(dp)) != 0;)
        if (golden_lang(e->d_name) && ng < NG) {
            snprintf(names[ng], sizeof names[ng], "%s", e->d_name);
            g[ng] = names[ng];
            ng++;
        }
    closedir(dp);
    qsort(g, ng, sizeof g[0], name_cmp);
    for (size_t i = 0; i < ng; i++) {
        int lang = golden_lang(g[i]);
        langs_seen[lang] = 1;
        snprintf(path, sizeof path, "%s/%s", dir, g[i]);
        size_t n = slurp(path, src, sizeof src);
        CHECK(n != (size_t) -1, "read %s", path);
        if (n == (size_t) -1) continue;
        snprintf(path, sizeof path, "%s/%s.out", dir, g[i]);
        size_t wn = slurp(path, want, sizeof want);
        CHECK(wn != (size_t) -1, "read %s", path);
        zl_diag_t d;
        int s = zl_compile(lang, src, n, &P, &work, &d);
        CHECK(s == ZL_OK, "%s compiles: %s line %u: %s", g[i], zl_status_name(s), d.line, d.msg);
        if (s) continue;
        int is_collatz = strncmp(g[i], "collatz.", 8) == 0;
        ncollatz += is_collatz;
        zl_trace_t t = {traces[ti], 64, 0};
        memset(&R, 0, sizeof R);
        R.out = out;
        R.out_cap = sizeof out;
        R.trace = is_collatz ? &t : 0;
        snprintf(path, sizeof path, "%s/%s.in", dir, g[i]);
        R.in = in;
        R.nin = golden_in(path, in, sizeof in / sizeof in[0]);
        if (lang == ZL_LANG_FORTH) R.preload = 1;
        s = zl_run(&P, &R);
        CHECK(s == ZL_OK, "%s runs: %s at line %u", g[i], zl_status_name(s), R.trap_line);
        CHECK(R.out_len == wn && memcmp(out, want, wn) == 0,
              "%s output matches %s.out byte for byte:\n%s", g[i], g[i], out);
        twin_props(lang, src, n, g[i]);
        if (is_collatz) tn[ti++] = t.n;
    }
    for (int lang = 1; lang < ZL_LANG__COUNT; lang++) {
        zl_diag_t d;
        if (compile(lang, "", &d) == ZL_E_UNSUPPORTED && d.msg && strstr(d.msg, "not built yet"))
            continue; /* a placeholder front-end has no goldens yet */
        CHECK(langs_seen[lang], "%s has golden programs", zl_lang_name(lang));
    }
    /* one algorithm, every language, one sequence of numbers */
    CHECK(ti >= 4 && ti == ncollatz, "every collatz golden ran (%d of %d)", ti, ncollatz);
    for (int j = 0; j < ti; j++) {
        CHECK(tn[j] == 40, "collatz trace %d has 40 numbers (%zu)", j, tn[j]);
        for (size_t k = 0; j > 0 && k < tn[0] && k < tn[j]; k++)
            CHECK(traces[0][k] == traces[j][k], "collatz trace %d, number %zu: %lld, first %lld", j,
                  k, (long long) traces[j][k], (long long) traces[0][k]);
    }
}

/* ------------------------------------------------------------ precompiled */

static int unpack_s(const char *t)
{
    zl_diag_t d;
    memset(&P3, 0x5a, sizeof P3);
    int s = zl_unpack(t, strlen(t), &P3, &d);
    if (s != ZL_OK)
        CHECK(d.status == s && d.msg && d.msg[0] && P3.ncode == 0 && P3.lang == 0,
              "refusal %d is reported and zeroes the program", s);
    return s;
}

static void test_pack(void)
{
    zl_diag_t d;
    size_t n = 0;
    prog_reset(64);
    I(ZL_PUSH, -0x7fffffffffffffffll - 1, 0);
    I(ZL_PUSH, 0x7fffffffffffffffll, -0x7fffffff - 1);
    I(ZL_DROP, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_OUTS, 1, 0);
    I(ZL_HALT, 0, 0);
    P.nstr = 2;
    P.str_off[0] = 0;
    P.str_len[0] = 0;
    P.str_off[1] = 0;
    P.str_len[1] = 3;
    memcpy(P.str, "a\n\xff", 3);
    P.strbytes = 3;
    P.code[5].line = 4000000000u;
    CHECK(zl_verify(&P, &d) == ZL_OK, "pack fixture verifies: %s", d.msg);
    CHECK(zl_pack(&P, pk, sizeof pk, &n) == ZL_OK, "extremes pack");
    CHECK(strstr(pk, "PUSH -9223372036854775808 0 1\n") &&
              strstr(pk, "PUSH 9223372036854775807 -2147483648 2\n") && strstr(pk, "str 0 \n") &&
              strstr(pk, "str 3 610aff\n") && strstr(pk, "HALT 0 0 4000000000\n"),
          "pack text:\n%s", pk);
    CHECK(strncmp(pk, "zlir 1\nlang 3\nbits 64\nslots 8\ncode 6\n", 37) == 0, "header:\n%s", pk);
    CHECK(unpack_s(pk) == ZL_OK && zl_prog_equal(&P, &P3) && P3.code[5].line == 4000000000u &&
              P3.str_len[1] == 3 && memcmp(P3.str + P3.str_off[1], "a\n\xff", 3) == 0,
          "extremes read back");
    CHECK(run_p(&P3, 0, 0, 0) == ZL_OK && strcmp(out, "a\n\xff") == 0, "unpacked program runs");
    size_t full = n;
    for (size_t cap = 1; cap <= full; cap += full / 7 + 1) {
        size_t m = 99;
        CHECK(zl_pack(&P, pk, cap, &m) == ZL_E_LIMIT && m == 0 && pk[0] == 0,
              "cap %zu is too small", cap);
    }
    CHECK(zl_pack(&P, pk, full, &n) == ZL_E_LIMIT, "no room for the NUL");
    CHECK(zl_pack(&P, 0, 0, &n) == ZL_E_LIMIT && n == 0, "a size query is refused, not written");
    CHECK(zl_pack(&P, pk, full + 1, &n) == ZL_OK && n == full, "length + 1 is enough");
    CHECK(zl_pack(0, pk, sizeof pk, &n) == ZL_E_ARG && zl_pack(&P, pk, sizeof pk, 0) == ZL_E_ARG,
          "pack arguments");
    P.code[0].op = 200;
    CHECK(zl_pack(&P, pk, sizeof pk, &n) == ZL_E_VERIFY && n == 0,
          "an unverified program is not packed");
    P.code[0].op = ZL_PUSH;
    CHECK(zl_unpack(0, 3, &P3, &d) == ZL_E_ARG && zl_unpack("x", 1, 0, &d) == ZL_E_ARG,
          "unpack arguments");
    CHECK(zl_unpack("", 0, &P3, 0) == ZL_E_SYNTAX, "empty text, no diagnostic wanted");

    /* every single-byte change to a packed program is refused or reads back as a
     * verified program with the same digest it claims */
    prog_reset(32);
    I(ZL_PUSH, 12, 0);
    I(ZL_JZ, 3, 0);
    I(ZL_OUTS, 0, 0);
    I(ZL_HALT, 0, 0);
    P.nstr = 1;
    P.str_len[0] = 2;
    memcpy(P.str, "ok", 2);
    P.strbytes = 2;
    CHECK(zl_pack(&P, pk, sizeof pk, &n) == ZL_OK, "small program packs");
    static char good[4096], bad[4096];
    memcpy(good, pk, n + 1);
    int accepted = 0;
    for (size_t i = 0; i < n; i++) {
        static const char subst[] = "0 1-9az\nAZ";
        for (const char *c = subst; *c; c++) {
            if (good[i] == *c) continue;
            memcpy(bad, good, n + 1);
            bad[i] = *c;
            if (unpack_s(bad) == ZL_OK) {
                accepted++;
                CHECK(zl_verify(&P3, &d) == ZL_OK, "accepted mutant verifies");
            }
        }
        memcpy(bad, good, n + 1);
        memmove(bad + i, bad + i + 1, n - i); /* delete one byte */
        CHECK(unpack_s(bad) != ZL_OK, "deleting byte %zu is refused", i);
    }
    /* only a change in a source line number keeps the digest (lines are not in it) */
    CHECK(accepted > 0 && accepted < 40, "%d single-byte mutants accepted, all in line numbers",
          accepted);

    static const struct {
        const char *t, *why;
    } refuse[] = {
        {"zlir 2\n", "version"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nHALT 0 0 1\ndigest 0000000000000000\nend\n",
         "wrong digest"},
        {"zlir 1\nlang 9\nbits 32\nslots 0\ncode 1\nHALT 0 0 1\n", "language"},
        {"zlir 1\nlang 1\nbits 33\nslots 0\ncode 1\nHALT 0 0 1\n", "width"},
        {"zlir 1\nlang 1\nbits 032\n", "leading zero"},
        {"zlir 1\nlang 1\nbits 32\nslots -0\n", "minus zero"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 0\n", "no code"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nHALTX 0 0 1\n", "unknown op"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nHALT 0 0 1 \n", "trailing space"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nPUSH 9223372036854775808 0 1\n", "a too big"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nPUSH 0 2147483648 1\n", "b too big"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nJMP 5 0 1\n", "jump outside"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nHALT 0 0 1\nstr 1 4G\n", "bad hex"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nHALT 0 0 1\nstr 1 4A\n", "upper-case hex"},
        {"zlir 1\nlang 1\nbits 32\nslots 0\ncode 1\nHALT 0 0 1\nstr 9000 \n", "string too long"},
        {"zlir 1\r\n", "CRLF"},
    };
    for (size_t i = 0; i < sizeof refuse / sizeof refuse[0]; i++)
        CHECK(unpack_s(refuse[i].t) != ZL_OK, "refused: %s", refuse[i].why);
    memcpy(bad, good, n + 1);
    strcpy(bad + n, "x");
    CHECK(unpack_s(bad) == ZL_E_SYNTAX, "text after end is refused");
    CHECK(unpack_s(good) == ZL_OK, "the original still reads");

    /* Forth-style arguments arrive on the data stack */
    zl_diag_t dd;
    int64_t args[3] = {7, -2, 40};
    CHECK(compile(ZL_LANG_FORTH, ". . .", &dd) == ZL_OK, "stack printer compiles");
    memset(&R, 0, sizeof R);
    R.in = args;
    R.nin = 3;
    R.preload = 1;
    R.out = out;
    R.out_cap = sizeof out;
    CHECK(zl_run(&P, &R) == ZL_OK && strcmp(out, "40 -2 7 ") == 0, "preload: [%s]", out);
    CHECK(run_p(&P, args, 3, 0) == ZL_E_STACK, "without preload the stack is empty");
    static int64_t many[ZL_MAX_STACK + 1];
    memset(&R, 0, sizeof R);
    R.in = many;
    R.nin = ZL_MAX_STACK + 1;
    R.preload = 1;
    R.out = out;
    R.out_cap = sizeof out;
    CHECK(zl_run(&P, &R) == ZL_E_STACK, "more arguments than the stack holds");
    R.nin = ZL_MAX_STACK;
    CHECK(zl_run(&P, &R) == ZL_OK && strcmp(out, "0 0 0 ") == 0, "a full stack is fine");
    CHECK(strcmp(zl_op_name(ZL_PUSH), "PUSH") == 0 && strcmp(zl_op_name(ZL_TRAP), "TRAP") == 0 &&
              strcmp(zl_op_name(ZL_OP__COUNT), "?") == 0 && strcmp(zl_op_name(-1), "?") == 0,
          "op names");
}

/* ------------------------------------------------- frames and lists */

static uint32_t here(void)
{
    return P.ncode;
}

static void patch(uint32_t at, uint32_t target)
{
    P.code[at].a = target;
}

static void sym(const char *name)
{
    size_t n = strlen(name);
    P.str_off[P.nstr] = P.strbytes;
    P.str_len[P.nstr] = (uint32_t) n;
    memcpy(P.str + P.strbytes, name, n);
    P.strbytes += (uint32_t) n;
    P.nstr++;
}

/* Verify and run P, then compare the status and (when given) the output. */
static void expect_run(int status, const char *want, const char *what)
{
    zl_diag_t d;
    CHECK(zl_verify(&P, &d) == ZL_OK, "%s verifies: %s", what, d.msg);
    int s = run_p(&P, 0, 0, 0);
    CHECK(s == status, "%s: status %d want %d (out [%s])", what, s, status, out);
    if (want) CHECK(strcmp(out, want) == 0, "%s: out [%s] want [%s]", what, out, want);
}

static void test_frames(void)
{
    uint32_t j, sub, inner, call;

    /* arguments arrive in order; the result is left on the stack */
    prog_reset(64);
    j = here();
    I(ZL_JMP, 0, 0);
    sub = here();
    I(ZL_ENTER, 3, 2); /* sub(a, b), local c */
    I(ZL_LOADF, 0, 0);
    I(ZL_LOADF, 1, 0);
    I(ZL_SUB, 0, 0);
    I(ZL_STOREF, 2, 0);
    I(ZL_LOADF, 2, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    patch(j, here());
    I(ZL_LINK, 0, 0);
    I(ZL_PUSH, 7, 0);
    I(ZL_PUSH, 5, 0);
    I(ZL_CALL, sub, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "2", "two parameters");
    CHECK(R.used == 15, "every instruction costs one unit of fuel (%llu)",
          (unsigned long long) R.used);

    /* a nested procedure reaches its parent's locals through the static link */
    prog_reset(64);
    j = here();
    I(ZL_JMP, 0, 0);
    inner = here();
    I(ZL_ENTER, 1, 0); /* inner, local z */
    I(ZL_LOADF, 0, 1);
    I(ZL_PUSH, 1, 0);
    I(ZL_ADD, 0, 0);
    I(ZL_STOREF, 0, 1); /* x := x + 1 in the parent */
    I(ZL_PUSH, 4, 0);
    I(ZL_STOREF, 0, 0); /* z is its own */
    I(ZL_LINK, 1, 0);   /* the parent's handle, then the main frame's */
    I(ZL_OUTI, 2, 0);
    I(ZL_LINK, 2, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_LINK, 9, 0); /* walking past the main frame stays there */
    I(ZL_OUTI, 2, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    sub = here();
    I(ZL_ENTER, 2, 0); /* outer, locals x y */
    I(ZL_PUSH, 10, 0);
    I(ZL_STOREF, 0, 0);
    I(ZL_PUSH, 99, 0);
    I(ZL_STOREF, 1, 0);
    I(ZL_LINK, 0, 0);
    I(ZL_CALL, inner, 0);
    I(ZL_LOADF, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_LOADF, 1, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    patch(j, here());
    I(ZL_LINK, 0, 0);
    I(ZL_CALL, sub, 0);
    I(ZL_LINK, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "1 0 0 11 99 0", "static link");

    /* recursion: each call has its own frame */
    prog_reset(64);
    j = here();
    I(ZL_JMP, 0, 0);
    sub = here();
    I(ZL_ENTER, 1, 1); /* fact(n) */
    I(ZL_LOADF, 0, 0);
    I(ZL_PUSH, 2, 0);
    I(ZL_LT, 0, 0);
    call = here();
    I(ZL_JNZ, 0, 0);
    I(ZL_LINK, 1, 0); /* a sibling call: the same parent */
    I(ZL_LOADF, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_SUB, 0, 0);
    I(ZL_CALL, sub, 0);
    I(ZL_LOADF, 0, 0);
    I(ZL_MUL, 0, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    patch(call, here());
    I(ZL_PUSH, 1, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    patch(j, here());
    call = here();
    I(ZL_LINK, 0, 0);
    I(ZL_PUSH, 20, 0);
    I(ZL_CALL, sub, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "2432902008176640000", "recursive factorial");
    P.code[call + 1].a = 21;
    expect_run(ZL_E_OVERFLOW, 0, "factorial past 64 bits");
    P.code[call + 1].a = 2000;
    expect_run(ZL_E_RSTACK, 0, "recursion past the call depth");

    /* frame cells run out before the call depth */
    prog_reset(64);
    sub = here();
    I(ZL_LINK, 0, 0);
    I(ZL_ENTER, ZL_MAX_FRAMES / 4, 0);
    I(ZL_JMP, sub, 0);
    expect_run(ZL_E_RSTACK, 0, "frame cells exhausted");
    CHECK(R.trap_pc == 1 && R.used == 14, "the fifth frame is refused (pc %u, used %llu)",
          R.trap_pc, (unsigned long long) R.used);
    prog_reset(64);
    I(ZL_LINK, 0, 0);
    I(ZL_ENTER, ZL_MAX_FRAMES, 0);
    I(ZL_LOADF, ZL_MAX_FRAMES - 1, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_LINK, 0, 0);
    I(ZL_ENTER, ZL_MAX_FRAMES, 0); /* LEAVE gave the cells back */
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "0", "one frame may take every cell, and locals start at zero");

    /* var parameters: a frame cell's address works with LOADA / STOREA */
    prog_reset(64);
    I(ZL_LINK, 0, 0);
    I(ZL_PUSH, 3, 0);
    I(ZL_ENTER, 2, 1);
    I(ZL_PUSH, 41, 0);
    I(ZL_ADDRF, 1, 0);
    I(ZL_STOREA, 0, 0);
    I(ZL_ADDRF, 1, 0);
    I(ZL_LOADA, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_ADDRF, 0, 0);
    I(ZL_LOADA, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_ADDRF, 1, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "41 3 4097", "frame addresses");
    P.code[4].a = 2;
    expect_run(ZL_E_BOUNDS, 0, "ADDRF past the frame");
    prog_reset(64);
    I(ZL_LINK, 0, 0);
    I(ZL_ENTER, 1, 0);
    I(ZL_ADDRF, 0, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_LOADA, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_BOUNDS, 0, "an address in a dropped frame");
    prog_reset(64);
    I(ZL_PUSH, 7, 0);
    I(ZL_PUSH, ZL_MAX_SLOTS, 0);
    I(ZL_STOREA, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_BOUNDS, 0, "no frame cells in the main program");
    prog_reset(64);
    I(ZL_PUSH, 7, 0);
    I(ZL_PUSH, 5, 0);
    I(ZL_STOREA, 0, 0);
    I(ZL_PUSH, 5, 0);
    I(ZL_LOADA, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_PUSH, 8, 0);
    I(ZL_LOADA, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_BOUNDS, "7", "slot addresses still work, and stop at nslots");
    prog_reset(64);
    I(ZL_PUSH, 5, 0);
    I(ZL_STOREA, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "STOREA needs a value under the address");

    /* traps */
    prog_reset(64);
    I(ZL_LEAVE, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_RSTACK, 0, "LEAVE in the main program");
    prog_reset(64);
    I(ZL_PUSH, 1, 0);
    I(ZL_ENTER, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_DOMAIN, 0, "a static link to a frame that does not exist");
    P.code[0].a = -1;
    expect_run(ZL_E_DOMAIN, 0, "a negative static link");
    P.code[0].a = 0;
    expect_run(ZL_OK, "", "the main frame as static link");
    prog_reset(64);
    I(ZL_PUSH, 0, 0);
    I(ZL_ENTER, 1, 1);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "ENTER without its static link");
    prog_reset(64);
    I(ZL_LOADF, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_BOUNDS, 0, "LOADF in the empty main frame");
    prog_reset(64);
    I(ZL_LINK, 0, 0);
    I(ZL_ENTER, 1, 0);
    I(ZL_STOREF, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "STOREF with nothing to store");
    P.code[2].a = 1;
    expect_run(ZL_E_BOUNDS, 0, "STOREF past the frame");

    /* BOUND checks and keeps the index */
    for (int64_t v = -1; v <= 3; v++) {
        prog_reset(64);
        I(ZL_PUSH, v, 0);
        I(ZL_BOUND, 3, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        char want[24];
        snprintf(want, sizeof want, "%lld", (long long) v);
        expect_run(v >= 0 && v < 3 ? ZL_OK : ZL_E_BOUNDS, v >= 0 && v < 3 ? want : "", "BOUND");
    }
    prog_reset(64);
    I(ZL_BOUND, 3, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "BOUND on an empty stack");
}

/* Emit code that pushes the list of n numbers 1..n (or NIL). */
static void push_list(int n)
{
    I(ZL_PUSH, ZL_V_NIL, 0);
    for (int i = n; i >= 1; i--) {
        I(ZL_PUSH, i, 0);
        I(ZL_SWAP, 0, 0);
        I(ZL_CONS, 0, 0);
    }
}

static void print_in(int dialect, void (*build)(void), const char *want)
{
    prog_reset(62);
    P.lang = ZL_LANG_FORTH;
    sym("FOO");
    build();
    I(ZL_OUTV, dialect, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, want, "print");
}

static void b_list(void)
{
    push_list(3);
}
static void b_nil(void)
{
    I(ZL_PUSH, ZL_V_NIL, 0);
}
static void b_true(void)
{
    I(ZL_PUSH, ZL_V_TRUE, 0);
}
static void b_false(void)
{
    I(ZL_PUSH, ZL_V_FALSE, 0);
}
static void b_sym(void)
{
    I(ZL_PUSH, ZL_V_SYM, 0);
}
static void b_neg(void)
{
    I(ZL_PUSH, -42, 0);
}
static void b_nested(void) /* ((1) () FOO -5) */
{
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_PUSH, -5, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_PUSH, ZL_V_SYM, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_CONS, 0, 0);
    push_list(1);
    I(ZL_SWAP, 0, 0);
    I(ZL_CONS, 0, 0);
}
static void b_dotted(void) /* (1 2 . 3) */
{
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 2, 0);
    I(ZL_PUSH, 3, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_CONS, 0, 0);
}
static void b_pair_last(void) /* ((1 2)) */
{
    push_list(2);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_CONS, 0, 0);
}

static void test_lists(void)
{
    static const struct {
        void (*build)(void);
        const char *want[ZL_DIALECT__COUNT];
    } cases[] = {
        {b_list, {"(1 2 3)", "(1 2 3)", "1 2 3", "[1 2 3]"}},
        {b_nil, {"NIL", "()", "", "[]"}},
        {b_true, {"T", "#t", "true", "true"}},
        {b_false, {"NIL", "#f", "false", "false"}},
        {b_sym, {"FOO", "FOO", "FOO", "FOO"}},
        {b_neg, {"-42", "-42", "-42", "-42"}},
        {b_nested, {"((1) NIL FOO -5)", "((1) () FOO -5)", "[1] [] FOO -5", "[[1] [] FOO -5]"}},
        {b_pair_last, {"((1 2))", "((1 2))", "[1 2]", "[[1 2]]"}},
        {b_dotted, {"(1 2 . 3)", "(1 2 . 3)", 0, 0}},
    };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++)
        for (int d = 0; d < ZL_DIALECT__COUNT; d++) {
            if (cases[c].want[d])
                print_in(d, cases[c].build, cases[c].want[d]);
            else {
                prog_reset(62);
                cases[c].build();
                I(ZL_OUTV, d, 0);
                I(ZL_HALT, 0, 0);
                expect_run(ZL_E_DOMAIN, 0, "Logo has no dotted pairs");
            }
        }

    /* CAR, CDR, and the empty list */
    prog_reset(62);
    push_list(3);
    I(ZL_DUP, 0, 0);
    I(ZL_CAR, 0, 0);
    I(ZL_OUTV, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_OUTV, 0, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_CAR, 1, 0);
    I(ZL_OUTV, 0, 0);
    I(ZL_CDR, 1, 0);
    I(ZL_OUTV, 1, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "1(2 3)NIL()", "CAR and CDR");
    for (int op = ZL_CAR; op <= ZL_CDR; op++) {
        prog_reset(62);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(op, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "strict CAR / CDR of the empty list");
        P.code[0].a = 5;
        P.code[1].a = 1;
        expect_run(ZL_E_DOMAIN, 0, "CAR / CDR of a number");
        P.code[0].a = ZL_V_TRUE;
        expect_run(ZL_E_DOMAIN, 0, "CAR / CDR of true");
        P.code[0].op = ZL_NL;
        expect_run(ZL_E_STACK, 0, "CAR / CDR of nothing");
    }

    /* SETCAR, SETCDR, and a cycle that runs into the output cap */
    prog_reset(62);
    push_list(2);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 9, 0);
    I(ZL_SETCAR, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_PUSH, 8, 0);
    I(ZL_SETCDR, 0, 0);
    I(ZL_OUTV, 1, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "(9 2 . 8)", "SETCAR and SETCDR");
    prog_reset(62);
    push_list(1);
    I(ZL_DUP, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_SETCDR, 0, 0);
    I(ZL_OUTV, 1, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_OUTPUT, 0, "a cycle stops at the output cap");
    CHECK(R.out_len == sizeof out && strncmp(out, "(1 1 1", 6) == 0, "cycle output [%.8s]", out);
    for (int op = ZL_SETCAR; op <= ZL_SETCDR; op++) {
        prog_reset(62);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_PUSH, 1, 0);
        I(op, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "SETCAR / SETCDR of the empty list");
        P.code[0].op = ZL_NL;
        expect_run(ZL_E_STACK, 0, "SETCAR / SETCDR with one value");
    }

    /* ISA */
    {
        static const int64_t vals[] = {0,          -7,       ZL_V_NIL,    ZL_V_TRUE,
                                       ZL_V_FALSE, ZL_V_SYM, ZL_V_SYM + 1};
        static const char *const want[] = {"1000000", "1000000", "0010000", "0000000",
                                           "0000000", "0001000", "0001000"};
        for (size_t v = 0; v < sizeof vals / sizeof vals[0]; v++) {
            prog_reset(62);
            sym("A");
            sym("B");
            for (int t = 0; t < ZL_ISA__COUNT; t++) {
                I(ZL_PUSH, vals[v], 0);
                I(ZL_ISA, t, 0);
                I(ZL_OUTI, 0, 0);
            }
            I(ZL_HALT, 0, 0);
            expect_run(ZL_OK, want[v], "ISA");
        }
        prog_reset(62);
        push_list(1);
        I(ZL_ISA, ZL_ISA_PAIR, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_ISA, ZL_ISA_NUMBER, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_STACK, "1", "ISA of a pair, then of nothing");
    }

    /* references are not numbers: only EQ and NE take them */
    {
        static const int ops[] = {ZL_ADD, ZL_SUB, ZL_MUL, ZL_DIVT, ZL_MODF, ZL_POW, ZL_MIN, ZL_MAX,
                                  ZL_LT,  ZL_LE,  ZL_GT,  ZL_GE,   ZL_AND,  ZL_OR,  ZL_XOR};
        for (size_t k = 0; k < sizeof ops / sizeof ops[0]; k++)
            for (int side = 0; side < 2; side++) {
                prog_reset(62);
                I(ZL_PUSH, side ? 1 : ZL_V_NIL, 0);
                I(ZL_PUSH, side ? ZL_V_TRUE : 1, 0);
                I(ops[k], 0, 0);
                I(ZL_HALT, 0, 0);
                expect_run(ZL_E_DOMAIN, 0, "arithmetic on a reference");
            }
        static const int un[] = {ZL_NEG, ZL_ABS, ZL_SGN, ZL_INVERT};
        for (size_t k = 0; k < sizeof un / sizeof un[0]; k++) {
            prog_reset(62);
            I(ZL_PUSH, ZL_V_NIL, 0);
            I(un[k], 0, 0);
            I(ZL_HALT, 0, 0);
            expect_run(ZL_E_DOMAIN, 0, "unary arithmetic on a reference");
            P.code[0].a = -5;
            I(ZL_OUTI, 0, 0);
            P.code[2] = P.code[3];
            P.code[3].op = ZL_HALT;
            expect_run(ZL_OK, 0, "unary arithmetic on a number");
        }
        prog_reset(62);
        push_list(1);
        I(ZL_DUP, 0, 0);
        I(ZL_EQ, 0, 0);
        I(ZL_OUTI, 0, 0);
        push_list(1);
        push_list(1);
        I(ZL_EQ, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_NE, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_PUSH, ZL_V_TRUE, 0);
        I(ZL_PUSH, ZL_V_FALSE, 0);
        I(ZL_NE, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "1001", "EQ and NE compare identity");
        prog_reset(62);
        I(ZL_PUSH, ZL_V_REF - 1, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_ADD, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_OVERFLOW, 0, "a number stays inside 62 bits");
        P.code[0].a = -ZL_V_REF + 1;
        P.code[2].op = ZL_SUB;
        expect_run(ZL_E_OVERFLOW, 0, "and below -2^62");
        P.code[2].op = ZL_ADD;
        I(ZL_OUTI, 0, 0);
        P.code[3] = P.code[4];
        P.code[4].op = ZL_HALT;
        expect_run(ZL_OK, "-4611686018427387902", "62-bit edge arithmetic");
        prog_reset(62);
        I(ZL_IN, 0, 0);
        I(ZL_HALT, 0, 0);
        int64_t big = ZL_V_REF;
        memset(&R, 0, sizeof R);
        R.in = &big;
        R.nin = 1;
        R.out = out;
        R.out_cap = sizeof out;
        CHECK(zl_run(&P, &R) == ZL_E_OVERFLOW, "input cannot forge a reference");
    }

    /* the heap runs out; deep nesting refuses to print */
    prog_reset(62);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_JMP, 1, 0);
    expect_run(ZL_E_HEAP, 0, "cons cells exhausted");
    CHECK(R.trap_pc == 2 && R.used == 1 + 3 * (ZL_MAX_CELLS - 1) + 2,
          "every cell but the reserved 0 is used (used %llu)", (unsigned long long) R.used);
    prog_reset(62);
    I(ZL_PUSH, 1, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "CONS needs two values");
    for (int depth = ZL_MAX_STACK; depth <= ZL_MAX_STACK + 1; depth++) {
        prog_reset(62);
        I(ZL_PUSH, 7, 0);
        for (int i = 0; i < depth; i++) {
            I(ZL_PUSH, ZL_V_NIL, 0);
            I(ZL_CONS, 0, 0);
        }
        I(ZL_OUTV, 2, 0);
        I(ZL_HALT, 0, 0);
        expect_run(depth <= ZL_MAX_STACK ? ZL_OK : ZL_E_DOMAIN, 0, "print nesting limit");
        if (depth <= ZL_MAX_STACK)
            CHECK(out[0] == '[' && out[depth - 2] == '[' && out[depth - 1] == '7' &&
                      out[depth] == ']' && strlen(out) == 2 * (size_t) depth - 1,
                  "deep Logo list printed (%zu)", strlen(out));
    }
    prog_reset(62);
    I(ZL_OUTV, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "OUTV of nothing");
    prog_reset(62);
    push_list(3);
    I(ZL_OUTV, 3, 0);
    I(ZL_HALT, 0, 0);
    zl_diag_t d;
    CHECK(zl_verify(&P, &d) == ZL_OK, "fixture");
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = 4;
    CHECK(zl_run(&P, &R) == ZL_E_OUTPUT && R.out_len == 4 && memcmp(out, "[1 2", 4) == 0,
          "OUTV fills the cap and stops");

    /* verifier rules for the new instructions */
#define BAD(bits, setup, what)                                                                     \
    do {                                                                                           \
        prog_reset(bits);                                                                          \
        setup;                                                                                     \
        I(ZL_HALT, 0, 0);                                                                          \
        CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "verify rejects %s", what);                        \
    } while (0)
#define OK(bits, setup, what)                                                                      \
    do {                                                                                           \
        prog_reset(bits);                                                                          \
        setup;                                                                                     \
        I(ZL_HALT, 0, 0);                                                                          \
        CHECK(zl_verify(&P, &d) == ZL_OK, "verify accepts %s: %s", what, d.msg);                   \
    } while (0)
    for (int op = ZL_CONS; op <= ZL_OUTV; op++) {
        BAD(64, I(op, 0, 0), "a list op in a 64-bit program");
        BAD(32, I(op, 0, 0), "a list op in a 32-bit program");
        OK(62, I(op, 0, 0), "a list op in a 62-bit program");
    }
    BAD(62, I(ZL_CAR, 2, 0), "CAR mode 2");
    BAD(62, I(ZL_CDR, -1, 0), "CDR mode -1");
    OK(62, (I(ZL_CAR, 1, 0), I(ZL_CDR, 1, 0)), "nil-tolerant CAR / CDR");
    BAD(62, I(ZL_ISA, ZL_ISA__COUNT, 0), "an unknown type test");
    BAD(62, I(ZL_ISA, -1, 0), "a negative type test");
    OK(62, I(ZL_ISA, ZL_ISA__COUNT - 1, 0), "the last type test");
    BAD(62, I(ZL_OUTV, ZL_DIALECT__COUNT, 0), "an unknown dialect");
    BAD(62, I(ZL_OUTV, -1, 0), "a negative dialect");
    OK(62, I(ZL_OUTV, ZL_DIALECT__COUNT - 1, 0), "the last dialect");
    BAD(62, I(ZL_PUSH, ZL_V_REF + 5, 0), "a forged pair constant");
    BAD(62, I(ZL_PUSH, -ZL_V_REF, 0), "-2^62");
    BAD(62, I(ZL_PUSH, ZL_V_SYM, 0), "a symbol with no strings");
    BAD(62, (sym("A"), I(ZL_PUSH, ZL_V_SYM + 1, 0)), "a symbol past the strings");
    BAD(62, I(ZL_PUSH, ZL_V_FALSE + 1, 0), "a constant past false");
    BAD(62, I(ZL_PUSH, ZL_V_TRUE - 1, 0), "a constant before true");
    BAD(62, I(ZL_PUSH, INT64_MAX, 0), "INT64_MAX in 62 bits");
    OK(62,
       (sym("A"), I(ZL_PUSH, ZL_V_SYM, 0), I(ZL_PUSH, ZL_V_NIL, 0), I(ZL_PUSH, ZL_V_TRUE, 0),
        I(ZL_PUSH, ZL_V_FALSE, 0), I(ZL_PUSH, ZL_V_REF - 1, 0), I(ZL_PUSH, -ZL_V_REF + 1, 0)),
       "the 62-bit constants");
    OK(64, I(ZL_PUSH, ZL_V_REF + 5, 0), "any constant in 64 bits");
    BAD(62, P.word_bits = 63, "63-bit words");
    BAD(64, I(ZL_ENTER, ZL_MAX_FRAMES + 1, 0), "an oversized frame");
    BAD(64, I(ZL_ENTER, -1, 0), "a negative frame");
    BAD(64, I(ZL_ENTER, 2, 3), "more parameters than cells");
    BAD(64, I(ZL_ENTER, 2, -1), "negative parameters");
    BAD(64, I(ZL_ENTER, ZL_MAX_STACK, ZL_MAX_STACK), "more parameters than the stack holds");
    OK(64, (I(ZL_ENTER, ZL_MAX_FRAMES, ZL_MAX_STACK - 1), I(ZL_ENTER, 0, 0)), "frame edges");
    BAD(64, I(ZL_LINK, -1, 0), "a negative link");
    BAD(64, I(ZL_LINK, ZL_MAX_CALLS, 0), "a link deeper than any call");
    OK(64, I(ZL_LINK, ZL_MAX_CALLS - 1, 0), "the deepest link");
    for (int op = ZL_LOADF; op <= ZL_ADDRF; op++) {
        BAD(64, I(op, -1, 0), "a negative frame offset");
        BAD(64, I(op, ZL_MAX_FRAMES, 0), "a frame offset past the cells");
        BAD(64, I(op, 0, -1), "negative hops");
        BAD(64, I(op, 0, ZL_MAX_CALLS), "too many hops");
        OK(64, I(op, ZL_MAX_FRAMES - 1, ZL_MAX_CALLS - 1), "frame operand edges");
    }
    BAD(64, I(ZL_BOUND, 0, 0), "an empty bound");
    BAD(64, I(ZL_BOUND, -3, 0), "a negative bound");
    OK(64, (I(ZL_BOUND, 1, 0), I(ZL_LEAVE, 0, 0)), "bound 1 and LEAVE");
#undef BAD
#undef OK

    /* the new instructions pack and unpack */
    {
        size_t n = 0;
        prog_reset(62);
        sym("FOO");
        for (int op = ZL_ENTER; op < ZL_OP__COUNT; op++)
            I(op, op == ZL_BOUND || op == ZL_RANGE ? 1 : op == ZL_WRAP ? 8 : 0, 0);
        I(ZL_PUSH, ZL_V_SYM, 0);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_HALT, 0, 0);
        CHECK(zl_verify(&P, &d) == ZL_OK, "pack fixture: %s", d.msg);
        CHECK(zl_pack(&P, pk, sizeof pk, &n) == ZL_OK && strstr(pk, "\nOUTV 0 0 ") &&
                  strstr(pk, "\nbits 62\n"),
              "62-bit programs pack");
        CHECK(unpack_s(pk) == ZL_OK && zl_prog_equal(&P, &P3), "and unpack unchanged");
        for (int op = ZL_ENTER; op < ZL_OP__COUNT; op++)
            CHECK(strcmp(zl_op_name(op), "?") != 0, "op %d has a name", op);
    }
    CHECK(strcmp(zl_status_name(ZL_E_HEAP), "list cells exhausted") == 0, "heap status name");
}

/* ------------------------------------- strings, vectors, procedures, catch */

/* Emit code that pushes a string made at run time: literal i, copied. */
static void push_made(int i)
{
    I(ZL_PUSH, ZL_V_STRC + i, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_PUSH, (int64_t) P.str_len[i], 0);
    I(ZL_SSUB, 0, 0);
}

static void test_ext(void)
{
    zl_diag_t d;
    /* literals and made strings print raw or quoted */
    for (int made = 0; made < 2; made++)
        for (int q = 0; q < 2; q++) {
            prog_reset(62);
            sym("say \"hi\\\" ok");
            if (made)
                push_made(0);
            else
                I(ZL_PUSH, ZL_V_STRC, 0);
            I(ZL_OUTV, ZL_DIALECT_SCHEME, q);
            I(ZL_HALT, 0, 0);
            expect_run(ZL_OK, q ? "\"say \\\"hi\\\\\\\" ok\"" : "say \"hi\\\" ok", "string print");
        }
    /* SCAT, SSUB, VLEN, VREF on literal and made strings */
    prog_reset(62);
    sym("abc");
    sym("defgh");
    I(ZL_PUSH, ZL_V_STRC, 0);
    I(ZL_PUSH, ZL_V_STRC + 1, 0);
    I(ZL_SCAT, 0, 0); /* abcdefgh */
    I(ZL_DUP, 0, 0);
    I(ZL_VLEN, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 7, 0);
    I(ZL_VREF, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 2, 0);
    I(ZL_PUSH, 4, 0);
    I(ZL_SSUB, 0, 0); /* cdef */
    I(ZL_DUP, 0, 0);
    I(ZL_OUTV, 0, 0);
    I(ZL_SCAT, 0, 0); /* abcdefghcdef */
    I(ZL_DUP, 0, 0);
    I(ZL_OUTV, 2, 0);
    I(ZL_VLEN, 0, 0);
    I(ZL_OUTI, 1, 0);
    I(ZL_PUSH, ZL_V_STRC, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_VREF, 0, 0);
    I(ZL_OUTI, 1, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "8 104 cdefabcdefghcdef 12 98", "string building");
    /* the empty string, and a string of exactly 8 and 9 bytes */
    for (int n = 0; n <= 9; n++) {
        prog_reset(62);
        sym("0123456789");
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_PUSH, n, 0);
        I(ZL_SSUB, 0, 0);
        I(ZL_DUP, 0, 0);
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_SCMP, 0, 0);
        I(ZL_OUTI, 2, 0);
        I(ZL_DUP, 0, 0);
        I(ZL_VLEN, 0, 0);
        I(ZL_OUTI, 2, 0);
        I(ZL_OUTV, 1, 1);
        I(ZL_HALT, 0, 0);
        char want[32];
        snprintf(want, sizeof want, "-1 %d \"%.*s\"", n, n, "0123456789");
        expect_run(ZL_OK, want, "substring lengths");
    }
    /* SSUB bounds */
    {
        static const int64_t bad[][2] = {{-1, 1}, {0, -1}, {4, 0}, {3, 2}, {0, 4}, {1, 2}};
        for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
            prog_reset(62);
            sym("abc");
            I(ZL_PUSH, ZL_V_STRC, 0);
            I(ZL_PUSH, bad[k][0], 0);
            I(ZL_PUSH, bad[k][1], 0);
            I(ZL_SSUB, 0, 0);
            I(ZL_OUTV, 0, 0);
            I(ZL_HALT, 0, 0);
            expect_run(k < 3 || k == 3 || k == 4 ? ZL_E_BOUNDS : ZL_OK, k == 5 ? "bc" : 0,
                       "substring bounds");
        }
        prog_reset(62);
        sym("abc");
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_PUSH, 3, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_SSUB, 0, 0);
        I(ZL_VLEN, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "0", "an empty substring at the end");
    }
    /* SCMP order */
    {
        static const char *const pairs[][2] = {{"a", "b"},    {"b", "a"},    {"ab", "ab"},
                                               {"ab", "abc"}, {"abc", "ab"}, {"", ""},
                                               {"", "a"},     {"\xff", "a"}};
        static const int want[] = {-1, 1, 0, -1, 1, 0, -1, 1};
        for (size_t k = 0; k < sizeof want / sizeof want[0]; k++) {
            prog_reset(62);
            sym(pairs[k][0]);
            sym(pairs[k][1]);
            I(ZL_PUSH, ZL_V_STRC, 0);
            I(ZL_PUSH, ZL_V_STRC + 1, 0);
            I(ZL_SCMP, 0, 0);
            I(ZL_OUTI, 0, 0);
            I(ZL_HALT, 0, 0);
            char w[8];
            snprintf(w, sizeof w, "%d", want[k]);
            expect_run(ZL_OK, w, "string compare");
        }
    }
    /* string operations refuse what is not a string */
    {
        static const int ops[] = {ZL_SCAT, ZL_SCMP};
        static const int64_t notstr[] = {5, ZL_V_NIL, ZL_V_TRUE, ZL_V_SYM};
        for (size_t k = 0; k < 2; k++)
            for (size_t v = 0; v < 4; v++)
                for (int side = 0; side < 2; side++) {
                    prog_reset(62);
                    sym("x");
                    I(ZL_PUSH, side ? ZL_V_STRC : notstr[v], 0);
                    I(ZL_PUSH, side ? notstr[v] : ZL_V_STRC, 0);
                    I(ops[k], 0, 0);
                    I(ZL_HALT, 0, 0);
                    expect_run(ZL_E_DOMAIN, 0, "string op on a non-string");
                }
        prog_reset(62);
        I(ZL_PUSH, 5, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_SSUB, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "substring of a number");
        prog_reset(62);
        I(ZL_PUSH, 0, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_SSUB, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_STACK, 0, "SSUB needs three values");
        prog_reset(62);
        sym("x");
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_SCAT, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_STACK, 0, "SCAT needs two values");
    }
    /* SCONV */
    {
        static const struct {
            int64_t v;
            const char *want;
        } fn[] = {{0, "0"},
                  {-7, "-7"},
                  {ZL_V_REF - 1, "4611686018427387903"},
                  {-ZL_V_REF + 1, "-4611686018427387903"}};
        for (size_t k = 0; k < sizeof fn / sizeof fn[0]; k++) {
            prog_reset(62);
            I(ZL_PUSH, fn[k].v, 0);
            I(ZL_SCONV, ZL_SCONV_FROM_NUMBER, 0);
            I(ZL_DUP, 0, 0);
            I(ZL_OUTV, 0, 1);
            I(ZL_SCONV, ZL_SCONV_TO_NUMBER, 0);
            I(ZL_OUTI, 0, 0);
            I(ZL_HALT, 0, 0);
            char w[64];
            snprintf(w, sizeof w, "\"%s\"%s", fn[k].want, fn[k].want);
            expect_run(ZL_OK, w, "number to string and back");
        }
        static const struct {
            const char *s;
            int status;
            const char *want;
        } tn[] = {{"42", ZL_OK, "42"},
                  {"+42", ZL_OK, "42"},
                  {"-0", ZL_OK, "0"},
                  {"007", ZL_OK, "7"},
                  {"", ZL_E_DOMAIN, 0},
                  {"-", ZL_E_DOMAIN, 0},
                  {"+", ZL_E_DOMAIN, 0},
                  {"4a", ZL_E_DOMAIN, 0},
                  {" 4", ZL_E_DOMAIN, 0},
                  {"4 ", ZL_E_DOMAIN, 0},
                  {"--4", ZL_E_DOMAIN, 0},
                  {"4611686018427387903", ZL_OK, "4611686018427387903"},
                  {"4611686018427387904", ZL_E_OVERFLOW, 0},
                  {"-4611686018427387904", ZL_E_OVERFLOW, 0},
                  {"99999999999999999999", ZL_E_OVERFLOW, 0},
                  {"-9223372036854775808", ZL_E_OVERFLOW, 0}};
        for (size_t k = 0; k < sizeof tn / sizeof tn[0]; k++) {
            prog_reset(62);
            sym(tn[k].s);
            I(ZL_PUSH, ZL_V_STRC, 0);
            I(ZL_SCONV, ZL_SCONV_TO_NUMBER, 0);
            I(ZL_OUTI, 0, 0);
            I(ZL_HALT, 0, 0);
            expect_run(tn[k].status, tn[k].want, tn[k].s);
        }
        for (int64_t b = -1; b <= 256; b += 1) {
            if (b > 1 && b < 254 && b != 65) continue;
            prog_reset(62);
            I(ZL_PUSH, b, 0);
            I(ZL_SCONV, ZL_SCONV_FROM_BYTE, 0);
            I(ZL_DUP, 0, 0);
            I(ZL_VLEN, 0, 0);
            I(ZL_OUTI, 0, 0);
            I(ZL_PUSH, 0, 0);
            I(ZL_VREF, 0, 0);
            I(ZL_OUTI, 1, 0);
            I(ZL_HALT, 0, 0);
            char w[16];
            snprintf(w, sizeof w, "1 %d", (int) b);
            expect_run(b >= 0 && b <= 255 ? ZL_OK : ZL_E_DOMAIN, b >= 0 && b <= 255 ? w : 0,
                       "byte to string");
        }
        prog_reset(62);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_SCONV, ZL_SCONV_FROM_NUMBER, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "a reference is not a number");
        P.code[1].a = ZL_SCONV_TO_NUMBER;
        expect_run(ZL_E_DOMAIN, 0, "nor a string");
        P.code[0].op = ZL_NL;
        expect_run(ZL_E_STACK, 0, "SCONV of nothing");
    }
    /* vectors */
    prog_reset(62);
    I(ZL_PUSH, 3, 0);
    I(ZL_VNEW, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 42, 0);
    I(ZL_VSET, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 2, 0);
    push_list(2);
    I(ZL_VSET, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_OUTV, ZL_DIALECT_SCHEME, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_OUTV, ZL_DIALECT_LOGO, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_VREF, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_VLEN, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_VNEW, 0, 0);
    I(ZL_OUTV, ZL_DIALECT_LISP, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "#(0 42 (1 2)){0 42 [1 2]}42 3#()", "vectors");
    /* a vector inside a list inside a vector, and strings in both */
    prog_reset(62);
    sym("s");
    I(ZL_PUSH, 1, 0);
    I(ZL_VNEW, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_PUSH, ZL_V_STRC, 0);
    I(ZL_VSET, 0, 0); /* #("s") */
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_CONS, 0, 0); /* (#("s")) */
    I(ZL_PUSH, 2, 0);
    I(ZL_VNEW, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_ROT, 0, 0);
    I(ZL_ROT, 0, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_VSET, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_OUTV, ZL_DIALECT_LISP, 1);
    I(ZL_OUTV, ZL_DIALECT_LOGO_SHOW, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "#((#(\"s\")) 0){[{s}] 0}", "nesting vectors and lists");
    /* vector traps */
    {
        static const int64_t idx[] = {-1, 3, ZL_V_NIL};
        for (int op = 0; op < 2; op++)
            for (size_t k = 0; k < 3; k++) {
                prog_reset(62);
                I(ZL_PUSH, 3, 0);
                I(ZL_VNEW, 0, 0);
                I(ZL_PUSH, idx[k], 0);
                if (op) I(ZL_PUSH, 1, 0);
                I(op ? ZL_VSET : ZL_VREF, 0, 0);
                I(ZL_HALT, 0, 0);
                expect_run(ZL_E_BOUNDS, 0, "vector index out of range");
            }
        prog_reset(62);
        sym("abc");
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_PUSH, 65, 0);
        I(ZL_VSET, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "strings are immutable");
        static const int64_t notvec[] = {7, ZL_V_NIL, ZL_V_TRUE};
        for (size_t k = 0; k < 3; k++) {
            prog_reset(62);
            I(ZL_PUSH, notvec[k], 0);
            I(ZL_VLEN, 0, 0);
            I(ZL_HALT, 0, 0);
            expect_run(ZL_E_DOMAIN, 0, "length of a non-vector");
            prog_reset(62);
            I(ZL_PUSH, notvec[k], 0);
            I(ZL_PUSH, 0, 0);
            I(ZL_VREF, 0, 0);
            I(ZL_HALT, 0, 0);
            expect_run(ZL_E_DOMAIN, 0, "index into a non-vector");
        }
        prog_reset(62);
        push_list(1);
        I(ZL_VLEN, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "a pair is not a vector");
        static const int64_t badn[] = {-1, ZL_V_NIL};
        for (size_t k = 0; k < 2; k++) {
            prog_reset(62);
            I(ZL_PUSH, badn[k], 0);
            I(ZL_VNEW, 0, 0);
            I(ZL_HALT, 0, 0);
            expect_run(ZL_E_DOMAIN, 0, "a vector of a bad size");
        }
        prog_reset(62);
        I(ZL_PUSH, ZL_MAX_BLK - 2, 0);
        I(ZL_VNEW, 0, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_VNEW, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_HEAP, 0, "the block heap fills");
        CHECK(R.trap_pc == 3, "the second vector does not fit (pc %u)", R.trap_pc);
        P.code[0].a = ZL_MAX_BLK - 3;
        expect_run(ZL_OK, 0, "the largest vector that fits, then an empty one");
        prog_reset(62);
        sym("abcdefgh");
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_DUP, 0, 0);
        I(ZL_SCAT, 0, 0);
        I(ZL_DROP, 0, 0);
        I(ZL_JMP, 0, 0);
        expect_run(ZL_E_FUEL, 0, "dropped strings are reclaimed: the loop runs out of fuel");
        CHECK(R.collections > 0, "collected %u times", R.collections);
        prog_reset(62);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_PUSH, 5, 0); /* 1 */
        I(ZL_SCONV, 0, 0);
        I(ZL_CONS, 0, 0); /* kept: a list of them */
        I(ZL_JMP, 1, 0);
        expect_run(ZL_E_HEAP, 0, "a kept list of number strings fills the pair heap first");
        CHECK(R.used == 1 + 4 * (uint64_t) (ZL_MAX_CELLS - 1) + 3 && R.collections == 1 &&
                  R.trap_pc == 3,
              "one pair per string, one collection that frees nothing, HEAP at the CONS (%llu, %u)",
              (unsigned long long) R.used, R.collections);
    }
    /* ISA on the new kinds */
    {
        prog_reset(62);
        sym("x");
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_PUSH, ZL_V_SYM, 0);
        I(ZL_PUSH, ZL_V_CODE, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_VNEW, 0, 0);
        push_made(0);
        I(ZL_PUSH, 9, 0);
        for (int v = 0; v < 6; v++) {
            for (int t = 0; t < ZL_ISA__COUNT; t++) {
                I(ZL_DUP, 0, 0);
                I(ZL_ISA, t, 0);
                I(ZL_OUTI, 0, 0);
            }
            I(ZL_DROP, 0, 0);
            I(ZL_NL, 0, 0);
        }
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "1000000\n0000100\n0000010\n0000001\n0001000\n0000100\n",
                   "ISA string, vector, procedure");
    }
    /* CALLI */
    {
        uint32_t j, f;
        prog_reset(62);
        j = here();
        I(ZL_JMP, 0, 0);
        f = here();
        I(ZL_PUSH, 7, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_RET, 0, 0);
        patch(j, here());
        I(ZL_PUSH, ZL_V_CODE + f, 0);
        I(ZL_DUP, 0, 0);
        I(ZL_CALLI, 0, 0);
        I(ZL_CALLI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "77", "calling a procedure value");
        prog_reset(62);
        I(ZL_PUSH, 5, 0);
        I(ZL_CALLI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "calling a number");
        P.code[0].a = ZL_V_NIL;
        expect_run(ZL_E_DOMAIN, 0, "calling the empty list");
        P.code[0].op = ZL_NL;
        expect_run(ZL_E_STACK, 0, "calling nothing");
        prog_reset(62);
        I(ZL_PUSH, ZL_V_CODE, 0);
        I(ZL_CALLI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_RSTACK, 0, "endless indirect recursion");
        prog_reset(62);
        I(ZL_PUSH, ZL_V_CODE, 0);
        I(ZL_OUTV, 1, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_DOMAIN, 0, "a procedure has no printed form");
    }
    /* CATCH, UNCATCH, THROW */
    {
        uint32_t c, h, j, f;
        /* a throw from two calls and a frame deep, back to the handler */
        prog_reset(64);
        j = here();
        I(ZL_JMP, 0, 0);
        f = here();
        I(ZL_ENTER, 1, 1);
        I(ZL_LOADF, 0, 0);
        I(ZL_PUSH, 3, 0);
        I(ZL_GT, 0, 0);
        c = here();
        I(ZL_JZ, 0, 0);
        I(ZL_LOADF, 0, 0);
        I(ZL_THROW, 0, 0);
        patch(c, here());
        I(ZL_LINK, 1, 0);
        I(ZL_LOADF, 0, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_ADD, 0, 0);
        I(ZL_CALL, f, 0);
        I(ZL_LEAVE, 0, 0);
        I(ZL_RET, 0, 0);
        patch(j, here());
        I(ZL_PUSH, 111, 0); /* stays on the stack under the handler */
        h = here();
        I(ZL_CATCH, 0, 0);
        I(ZL_LINK, 0, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_CALL, f, 0);
        I(ZL_UNCATCH, 0, 0);
        I(ZL_PUSH, -1, 0);
        I(ZL_OUTI, 2, 0);
        patch(h, here());
        I(ZL_OUTI, 2, 0);
        I(ZL_OUTI, 2, 0);
        I(ZL_LINK, 0, 0); /* the frames unwound to the main program's */
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "4 111 0", "throw unwinds calls and frames");
        /* nested handlers: the inner one catches first; UNCATCH restores the outer */
        prog_reset(32);
        I(ZL_CATCH, 9, 0); /* 0: outer */
        I(ZL_CATCH, 6, 0); /* 1: inner */
        I(ZL_UNCATCH, 0, 0);
        I(ZL_PUSH, 2, 0);  /* 3 */
        I(ZL_THROW, 0, 0); /* goes to the outer handler */
        I(ZL_HALT, 0, 0);
        I(ZL_OUTI, 2, 0); /* 6: inner handler */
        I(ZL_HALT, 0, 0);
        I(ZL_HALT, 0, 0);
        I(ZL_PUSH, 100, 0); /* 9: outer handler */
        I(ZL_ADD, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "102", "UNCATCH hands over to the outer handler");
        P.code[2].op = ZL_NL;
        expect_run(ZL_OK, "\n2 ", "the inner handler catches first");
        /* traps */
        prog_reset(64);
        I(ZL_PUSH, 1, 0);
        I(ZL_THROW, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_THROW, 0, "an uncaught throw");
        CHECK(R.trap_pc == 1, "the throw is where it stops (%u)", R.trap_pc);
        CHECK(strcmp(zl_status_name(ZL_E_THROW), "exception not caught") == 0, "status name");
        prog_reset(64);
        I(ZL_CATCH, 0, 0);
        I(ZL_THROW, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_STACK, 0, "THROW of nothing");
        prog_reset(64);
        I(ZL_UNCATCH, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_RSTACK, 0, "UNCATCH with no CATCH");
        prog_reset(64);
        I(ZL_CATCH, 0, 0);
        I(ZL_JMP, 0, 0);
        expect_run(ZL_E_RSTACK, 0, "too many CATCHes");
        CHECK(R.used == 2 * ZL_MAX_CALLS + 1, "every handler slot is used (%llu)",
              (unsigned long long) R.used);
        /* a handler left behind by a returned procedure is refused */
        prog_reset(64);
        j = here();
        I(ZL_JMP, 0, 0);
        f = here();
        I(ZL_CATCH, 0, 0);
        I(ZL_RET, 0, 0);
        patch(j, here());
        P.code[f].a = here();
        I(ZL_CALL, f, 0);
        I(ZL_PUSH, 5, 0);
        I(ZL_THROW, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_RSTACK, 0, "a stale handler from a returned call");
        prog_reset(64);
        I(ZL_PUSH, 1, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_CATCH, 6, 0);
        I(ZL_DROP, 0, 0);
        I(ZL_DROP, 0, 0);
        I(ZL_THROW, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_STACK, 0, "throw with the stack below the handler's");
        prog_reset(64);
        I(ZL_PUSH, 1, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_CATCH, 7, 0);
        I(ZL_DROP, 0, 0);
        I(ZL_DROP, 0, 0);
        I(ZL_THROW, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_E_RSTACK, 0, "a handler above the stack is refused");
        prog_reset(64);
        I(ZL_LINK, 0, 0);
        I(ZL_ENTER, 0, 0);
        I(ZL_CATCH, 0, 0);
        I(ZL_LEAVE, 0, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_THROW, 0, 0);
        I(ZL_HALT, 0, 0);
        P.code[2].a = 6;
        expect_run(ZL_E_RSTACK, 0, "a handler whose frame was left");
    }

    /* the verifier */
#define BAD(bits, setup, what)                                                                     \
    do {                                                                                           \
        prog_reset(bits);                                                                          \
        setup;                                                                                     \
        I(ZL_HALT, 0, 0);                                                                          \
        CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "verify rejects %s", what);                        \
    } while (0)
#define OK(bits, setup, what)                                                                      \
    do {                                                                                           \
        prog_reset(bits);                                                                          \
        setup;                                                                                     \
        I(ZL_HALT, 0, 0);                                                                          \
        CHECK(zl_verify(&P, &d) == ZL_OK, "verify accepts %s: %s", what, d.msg);                   \
    } while (0)
    for (int op = ZL_VNEW; op <= ZL_CALLI; op++) {
        BAD(64, I(op, 0, 0), "a block op in a 64-bit program");
        BAD(32, I(op, 0, 0), "a block op in a 32-bit program");
        OK(62, I(op, 0, 0), "a block op in a 62-bit program");
    }
    for (int bits = 32; bits <= 64; bits += 30) {
        OK(bits, (I(ZL_CATCH, 1, 0), I(ZL_UNCATCH, 0, 0), I(ZL_THROW, 0, 0)), "exceptions");
        OK(bits == 62 ? 64 : bits, (I(ZL_CATCH, 1, 0), I(ZL_THROW, 0, 0)), "exceptions any width");
    }
    OK(62, (I(ZL_CATCH, 1, 0), I(ZL_THROW, 0, 0)), "exceptions in 62 bits");
    BAD(64, I(ZL_CATCH, 2, 0), "a handler past the end");
    BAD(64, I(ZL_CATCH, -1, 0), "a handler before the start");
    BAD(62, I(ZL_SCONV, ZL_SCONV__COUNT, 0), "an unknown conversion");
    BAD(62, I(ZL_SCONV, -1, 0), "a negative conversion");
    OK(62, I(ZL_SCONV, ZL_SCONV__COUNT - 1, 0), "the last conversion");
    BAD(62, I(ZL_OUTV, 0, 2), "OUTV quoting 2");
    BAD(62, I(ZL_OUTV, 0, -1), "OUTV quoting -1");
    OK(62, I(ZL_OUTV, 0, 1), "OUTV quoting 1");
    BAD(62, I(ZL_PUSH, ZL_V_STRC, 0), "a string literal with no strings");
    BAD(62, (sym("a"), I(ZL_PUSH, ZL_V_STRC + 1, 0)), "a string literal past the strings");
    OK(62, (sym("a"), I(ZL_PUSH, ZL_V_STRC, 0)), "a string literal");
    BAD(62, I(ZL_PUSH, ZL_V_BLK + 1, 0), "a forged block");
    BAD(62, I(ZL_PUSH, ZL_V_CODE + 2, 0), "a procedure past the code");
    OK(62, (I(ZL_PUSH, ZL_V_CODE + 1, 0), I(ZL_NL, 0, 0)), "a procedure inside the code");
    BAD(62, I(ZL_PUSH, ZL_V_CODE - 1, 0), "a constant just below the procedures");
#undef BAD
#undef OK
    /* the new op names pack and unpack */
    {
        size_t n = 0;
        prog_reset(62);
        sym("FOO");
        for (int op = ZL_VNEW; op < ZL_OP__COUNT; op++)
            I(op, op == ZL_CATCH || op == ZL_RANGE ? 1 : op == ZL_WRAP ? 8 : 0, 0);
        I(ZL_PUSH, ZL_V_STRC, 0);
        I(ZL_PUSH, ZL_V_CODE + 2, 0);
        I(ZL_HALT, 0, 0);
        CHECK(zl_verify(&P, &d) == ZL_OK, "pack fixture: %s", d.msg);
        CHECK(zl_pack(&P, pk, sizeof pk, &n) == ZL_OK && strstr(pk, "\nTHROW 0 0 ") &&
                  strstr(pk, "\nSCONV 0 0 "),
              "the new ops pack");
        CHECK(unpack_s(pk) == ZL_OK && zl_prog_equal(&P, &P3), "and unpack unchanged");
        for (int op = ZL_VNEW; op < ZL_OP__COUNT; op++)
            CHECK(strcmp(zl_op_name(op), "?") != 0, "op %d has a name", op);
    }
}

/* ------------------------------------------------- E6: ranges, input's end,
 * shifts and wrapping, fixed-point output, and CATCH that takes traps */

/* push x, run op a b, print: the output and status */
static int e6_un(int bits, int op, int64_t a, int32_t b, int64_t x)
{
    prog_reset(bits);
    I(ZL_PUSH, x, 0);
    I(op, a, b);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    return run_p(&P, 0, 0, 0);
}

static int e6_bin(int bits, int op, int64_t x, int64_t y)
{
    prog_reset(bits);
    I(ZL_PUSH, x, 0);
    I(ZL_PUSH, y, 0);
    I(op, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    return run_p(&P, 0, 0, 0);
}

static void e6_outd(int64_t x, int64_t a, int32_t b, const char *want)
{
    int64_t tr[2];
    zl_trace_t t = {tr, 2, 0};
    prog_reset(64);
    I(ZL_PUSH, x, 0);
    I(ZL_OUTD, a, b);
    I(ZL_HALT, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.trace = &t;
    int s = zl_run(&P, &R);
    CHECK(s == ZL_OK && strcmp(out, want) == 0 && t.n == 1 && tr[0] == x,
          "OUTD %lld a=%lld b=%d: [%s] want [%s]", (long long) x, (long long) a, b, out, want);
}

static void test_e6(void)
{
    zl_diag_t d;
    /* RANGE: symmetric and two's complement, any word size, the value stays */
    static const struct {
        int64_t x, a;
        int32_t b;
        int ok;
    } rg[] = {
        {999999999, 999999999, 0, 1},
        {-999999999, 999999999, 0, 1},
        {1000000000, 999999999, 0, 0},
        {-1000000000, 999999999, 0, 0},
        {2147483647, 2147483647, 1, 1},
        {-2147483648LL, 2147483647, 1, 1},
        {2147483648LL, 2147483647, 1, 0},
        {-2147483649LL, 2147483647, 1, 0},
        {-2147483648LL, 2147483647, 0, 0},
        {0, 1, 0, 1},
        {-2, 1, 1, 1},
        {-3, 1, 1, 0},
        {-2, 1, 0, 0},
        {-2, 2, 0, 1},
        {INT64_MAX, INT64_MAX, 1, 1},
        {INT64_MIN, INT64_MAX, 1, 1},
        {INT64_MIN + 1, INT64_MAX, 0, 1},
        {INT64_MIN, INT64_MAX, 0, 0},
    };
    for (unsigned i = 0; i < sizeof rg / sizeof rg[0]; i++) {
        int s = e6_un(64, ZL_RANGE, rg[i].a, rg[i].b, rg[i].x);
        char want[32];
        snprintf(want, sizeof want, "%lld", (long long) rg[i].x);
        CHECK(rg[i].ok ? s == ZL_OK && strcmp(out, want) == 0 : s == ZL_E_OVERFLOW,
              "RANGE %lld in %lld/%d: %d [%s]", (long long) rg[i].x, (long long) rg[i].a, rg[i].b,
              s, out);
    }
    CHECK(e6_un(32, ZL_RANGE, 5, 0, 5) == ZL_OK && e6_un(62, ZL_RANGE, 5, 0, 6) == ZL_E_OVERFLOW,
          "RANGE in 32 and 62 bits");
    CHECK(e6_un(62, ZL_RANGE, 5, 0, ZL_V_NIL) == ZL_E_DOMAIN, "RANGE of a reference");
    prog_reset(64);
    I(ZL_RANGE, 5, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "RANGE on an empty stack");

    /* INEOF */
    prog_reset(32);
    I(ZL_INEOF, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_IN, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_INEOF, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_IN, 0, 0);
    I(ZL_HALT, 0, 0);
    int64_t one[1] = {42};
    CHECK(run_p(&P, one, 1, 0) == ZL_E_INPUT && strcmp(out, "0 42 1 ") == 0,
          "INEOF before and after the last value: [%s]", out);
    CHECK(run_p(&P, 0, 0, 0) == ZL_E_INPUT && strcmp(out, "1 ") == 0, "INEOF with no input");
    prog_reset(32);
    for (int i = 0; i < ZL_MAX_STACK; i++) I(ZL_PUSH, 0, 0);
    I(ZL_INEOF, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "INEOF on a full stack");

    /* SHL and SHR */
    static const struct {
        int bits, op;
        int64_t x, n;
        int s;
        const char *want;
    } sh[] = {
        {64, ZL_SHL, 1, 0, ZL_OK, "1"},
        {64, ZL_SHL, 3, 4, ZL_OK, "48"},
        {64, ZL_SHL, -3, 4, ZL_OK, "-48"},
        {64, ZL_SHL, 1, 62, ZL_OK, "4611686018427387904"},
        {64, ZL_SHL, 1, 63, ZL_E_OVERFLOW, 0},
        {64, ZL_SHL, -1, 63, ZL_E_OVERFLOW, 0},
        {64, ZL_SHL, -1, 62, ZL_OK, "-4611686018427387904"},
        {64, ZL_SHL, 0, 63, ZL_OK, "0"},
        {64, ZL_SHL, 3, 62, ZL_E_OVERFLOW, 0},
        {64, ZL_SHL, -2, 62, ZL_OK, "-9223372036854775808"},
        {64, ZL_SHL, -3, 62, ZL_E_OVERFLOW, 0},
        {64, ZL_SHL, 2, 62, ZL_E_OVERFLOW, 0},
        {64, ZL_SHL, 1, 64, ZL_E_DOMAIN, 0},
        {64, ZL_SHL, 1, -1, ZL_E_DOMAIN, 0},
        {32, ZL_SHL, 1, 30, ZL_OK, "1073741824"},
        {32, ZL_SHL, 1, 31, ZL_E_OVERFLOW, 0},
        {32, ZL_SHL, -1, 31, ZL_OK, "-2147483648"},
        {62, ZL_SHL, 1, 61, ZL_OK, "2305843009213693952"},
        {62, ZL_SHL, 1, 62, ZL_E_OVERFLOW, 0},
        {64, ZL_SHR, 48, 4, ZL_OK, "3"},
        {64, ZL_SHR, -48, 4, ZL_OK, "-3"},
        {64, ZL_SHR, -49, 4, ZL_OK, "-4"},
        {64, ZL_SHR, 7, 1, ZL_OK, "3"},
        {64, ZL_SHR, -7, 1, ZL_OK, "-4"},
        {64, ZL_SHR, -1, 63, ZL_OK, "-1"},
        {64, ZL_SHR, INT64_MAX, 63, ZL_OK, "0"},
        {64, ZL_SHR, INT64_MIN, 63, ZL_OK, "-1"},
        {64, ZL_SHR, 5, 0, ZL_OK, "5"},
        {64, ZL_SHR, 5, 64, ZL_E_DOMAIN, 0},
        {64, ZL_SHR, 5, -1, ZL_E_DOMAIN, 0},
    };
    for (unsigned i = 0; i < sizeof sh / sizeof sh[0]; i++) {
        int s = e6_bin(sh[i].bits, sh[i].op, sh[i].x, sh[i].n);
        CHECK(s == sh[i].s && (!sh[i].want || strcmp(out, sh[i].want) == 0),
              "%s %lld %lld in %d bits: %d [%s]", zl_op_name(sh[i].op), (long long) sh[i].x,
              (long long) sh[i].n, sh[i].bits, s, out);
    }
    CHECK(e6_bin(62, ZL_SHL, ZL_V_NIL, 1) == ZL_E_DOMAIN &&
              e6_bin(62, ZL_SHR, 1, ZL_V_TRUE) == ZL_E_DOMAIN,
          "shifts of references");
    prog_reset(64);
    I(ZL_PUSH, 1, 0);
    I(ZL_SHL, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "SHL needs two");

    /* WRAP */
    static const struct {
        int bits;
        int64_t x, a;
        int32_t b;
        int s;
        const char *want;
    } wr[] = {
        {64, 255, 8, 0, ZL_OK, "-1"},
        {64, 128, 8, 0, ZL_OK, "-128"},
        {64, 127, 8, 0, ZL_OK, "127"},
        {64, -129, 8, 0, ZL_OK, "127"},
        {64, -1, 8, 1, ZL_OK, "255"},
        {64, 256, 8, 1, ZL_OK, "0"},
        {64, 2147483648LL, 32, 0, ZL_OK, "-2147483648"},
        {64, -1, 32, 1, ZL_OK, "4294967295"},
        {32, -1, 32, 1, ZL_E_OVERFLOW, 0},
        {32, 2147483647, 32, 0, ZL_OK, "2147483647"},
        {64, INT64_MIN, 63, 0, ZL_OK, "0"},
        {64, -1, 63, 1, ZL_OK, "9223372036854775807"},
        {64, INT64_MAX, 63, 0, ZL_OK, "-1"},
        {64, 3, 1, 0, ZL_OK, "-1"},
        {64, 2, 1, 0, ZL_OK, "0"},
        {64, 3, 1, 1, ZL_OK, "1"},
        {62, -1, 62, 1, ZL_OK, "4611686018427387903"},
        {62, -1, 63, 1, ZL_E_OVERFLOW, 0},
        {62, -1, 61, 1, ZL_OK, "2305843009213693951"},
    };
    for (unsigned i = 0; i < sizeof wr / sizeof wr[0]; i++) {
        int s = e6_un(wr[i].bits, ZL_WRAP, wr[i].a, wr[i].b, wr[i].x);
        CHECK(s == wr[i].s && (!wr[i].want || strcmp(out, wr[i].want) == 0),
              "WRAP %lld to %lld/%d in %d bits: %d [%s]", (long long) wr[i].x, (long long) wr[i].a,
              wr[i].b, wr[i].bits, s, out);
    }
    CHECK(e6_un(62, ZL_WRAP, 8, 0, ZL_V_FALSE) == ZL_E_DOMAIN, "WRAP of a reference");

    /* OUTD */
    e6_outd(12345, 2, 0, "123.45");
    e6_outd(-5, 2, 0, "-0.05");
    e6_outd(5, 2, ZL_FMT_SIGNSPACE, " 0.05");
    e6_outd(0, 3, 0, "0.000");
    e6_outd(7, 0, 0, "7");
    e6_outd(-7, 0, ZL_FMT_TRAIL, "-7 ");
    e6_outd(42, 1 | 8 << 8, 0, "     4.2");
    e6_outd(42, 1 | 3 << 8, 0, "4.2");
    e6_outd(42, 1 | 2 << 8, 0, "4.2");
    e6_outd(INT64_MIN, 18, 0, "-9.223372036854775808");
    e6_outd(INT64_MAX, 18, 0, "9.223372036854775807");
    e6_outd(1, 18, 0, "0.000000000000000001");
    e6_outd(100, 2, ZL_FMT_SIGNSPACE | ZL_FMT_TRAIL, " 1.00 ");
    prog_reset(62);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_OUTD, 2, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_DOMAIN, "", "OUTD of a reference");
    prog_reset(64);
    I(ZL_OUTD, 2, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, "", "OUTD on an empty stack");
    prog_reset(64);
    I(ZL_PUSH, 123456, 0);
    I(ZL_OUTD, 2 | 64 << 8, 0);
    I(ZL_HALT, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = 10;
    CHECK(zl_run(&P, &R) == ZL_E_OUTPUT, "OUTD past the output");
    R.out_cap = sizeof out;
    P.code[1].a = 2 | 10 << 8;
    CHECK(zl_run(&P, &R) == ZL_OK && strcmp(out, "   1234.56") == 0, "width 10: [%s]", out);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = 8;
    CHECK(zl_run(&P, &R) == ZL_E_OUTPUT, "the padding fills the output first");

    /* CATCH b = 1 takes recoverable traps, with the status as the value */
    static const struct {
        int op;
        int64_t x, y;
        int st;
    } tr[] = {
        {ZL_DIVT, 1, 0, ZL_E_DIVZERO},
        {ZL_DIVX, 7, 2, ZL_E_INEXACT},
        {ZL_ADD, INT64_MAX, 1, ZL_E_OVERFLOW},
        {ZL_SHL, 1, 99, ZL_E_DOMAIN},
    };
    for (unsigned i = 0; i < sizeof tr / sizeof tr[0]; i++) {
        uint32_t h;
        prog_reset(64);
        I(ZL_PUSH, 7, 0); /* under the handler: kept */
        h = here();
        I(ZL_CATCH, 0, 1);
        I(ZL_PUSH, 99, 0); /* above it: dropped */
        I(ZL_PUSH, tr[i].x, 0);
        I(ZL_PUSH, tr[i].y, 0);
        I(tr[i].op, 1, 0);
        I(ZL_UNCATCH, 0, 0);
        I(ZL_HALT, 0, 0);
        patch(h, here());
        I(ZL_OUTI, 2, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        char want[32];
        snprintf(want, sizeof want, "%d 7", tr[i].st);
        expect_run(ZL_OK, want, zl_op_name(tr[i].op));
        P.code[1].b = 0;
        expect_run(tr[i].st, 0, "a plain CATCH lets the trap stop the program");
    }
    /* BOUNDS and INPUT */
    prog_reset(64);
    I(ZL_CATCH, 4, 1);
    I(ZL_PUSH, 9, 0);
    I(ZL_BOUND, 3, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_IN, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_INPUT, "-2623", "BOUNDS is caught; the handler is used up, INPUT then stops");
    /* the nearest trap-catching handler, past a plain inner one */
    prog_reset(64);
    I(ZL_CATCH, 6, 1); /* 0: outer, takes traps */
    I(ZL_CATCH, 9, 0); /* 1: inner, THROW only */
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_DIVT, 0, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_OUTI, 0, 0);  /* 6 */
    I(ZL_PUSH, 5, 0);  /* 7 */
    I(ZL_THROW, 0, 0); /* 8: no handler left: the inner one was dropped */
    I(ZL_HALT, 0, 0);  /* 9 */
    expect_run(ZL_E_THROW, "-2621", "the outer handler takes the trap and the inner is gone");
    /* traps that are machine limits are never caught */
    prog_reset(64);
    I(ZL_CATCH, 3, 1);
    I(ZL_JMP, 1, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_FUEL, "", "FUEL is not caught");
    prog_reset(64);
    I(ZL_CATCH, 3, 1);
    I(ZL_TRAP, ZL_E_DIVZERO, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "-2621", "a TRAP op is caught like the real thing");
    P.code[1].a = ZL_E_OUTPUT;
    expect_run(ZL_E_OUTPUT, "", "OUTPUT is a machine limit");
    P.code[1].a = ZL_E_STACK;
    expect_run(ZL_E_STACK, "", "STACK too");
    P.code[1].a = ZL_E_HEAP;
    expect_run(ZL_E_HEAP, "", "and HEAP");
    P.code[1].a = ZL_E_RSTACK;
    expect_run(ZL_E_RSTACK, "", "and RSTACK");
    P.code[1].a = ZL_E_INPUT;
    expect_run(ZL_OK, "-2628", "INPUT is caught");
    P.code[1].a = ZL_E_DOMAIN;
    expect_run(ZL_OK, "-2629", "DOMAIN is caught");
    P.code[1].a = ZL_E_BOUNDS;
    expect_run(ZL_OK, "-2623", "BOUNDS is caught");
    P.code[1].a = ZL_E_OVERFLOW;
    expect_run(ZL_OK, "-2620", "OVERFLOW is caught");
    P.code[1].a = ZL_E_INEXACT;
    expect_run(ZL_OK, "-2622", "INEXACT is caught");
    P.code[1].a = ZL_E_THROW;
    expect_run(ZL_E_THROW, "", "THROW's own trap is not");
    P.code[1].a = ZL_E_FUEL;
    expect_run(ZL_E_FUEL, "", "nor FUEL");
    /* a trap in a called procedure unwinds its frames */
    {
        uint32_t j, f, h;
        prog_reset(64);
        j = here();
        I(ZL_JMP, 0, 0);
        f = here();
        I(ZL_ENTER, 2, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_PUSH, 0, 0);
        I(ZL_MODT, 0, 0);
        I(ZL_LEAVE, 0, 0);
        I(ZL_RET, 0, 0);
        patch(j, here());
        h = here();
        I(ZL_CATCH, 0, 1);
        I(ZL_LINK, 0, 0);
        I(ZL_CALL, f, 0);
        I(ZL_HALT, 0, 0);
        patch(h, here());
        I(ZL_OUTI, 2, 0);
        I(ZL_LINK, 0, 0);
        I(ZL_OUTI, 0, 0);
        I(ZL_HALT, 0, 0);
        expect_run(ZL_OK, "-2621 0", "a trap inside a procedure: frames unwound");
    }
    /* a handler whose sp is the stack's top cannot take a value: STACK stops */
    prog_reset(64);
    for (int i = 0; i < ZL_MAX_STACK; i++) I(ZL_PUSH, 0, 0);
    I(ZL_CATCH, (int64_t) ZL_MAX_STACK + 3, 1);
    I(ZL_TRAP, ZL_E_DOMAIN, 0);
    I(ZL_HALT, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_DOMAIN, "", "a full stack at the handler: the trap stands");
    /* the trap is reported where it happened, when not caught */
    prog_reset(64);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_DIVT, 0, 0);
    P.code[2].line = 7;
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_DIVZERO, "", "uncaught");
    CHECK(R.trap_line == 7 && R.trap_pc == 2, "trap line and pc");

    /* verify */
    prog_reset(64);
    I(ZL_RANGE, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "RANGE 0");
    P.code[0].a = 1;
    P.code[0].b = 2;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "RANGE mode 2");
    P.code[0].b = -1;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "RANGE mode -1");
    P.code[0].b = 1;
    CHECK(zl_verify(&P, &d) == ZL_OK, "RANGE 1, 1");
    P.code[0].a = -5;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "RANGE -5");
    P.code[0] = (zl_ins_t){.op = ZL_WRAP, .a = 0};
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "WRAP 0");
    P.code[0].a = 64;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "WRAP 64");
    P.code[0].a = 63;
    CHECK(zl_verify(&P, &d) == ZL_OK, "WRAP 63");
    P.code[0].a = 1;
    P.code[0].b = 1;
    CHECK(zl_verify(&P, &d) == ZL_OK, "WRAP 1 unsigned");
    P.code[0].b = 2;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "WRAP mode 2");
    P.code[0].b = -1;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "WRAP mode -1");
    P.code[0] = (zl_ins_t){.op = ZL_OUTD, .a = 18 | 64 << 8, .b = 3};
    CHECK(zl_verify(&P, &d) == ZL_OK, "OUTD 18 digits, width 64, both flags");
    P.code[0].a = 19;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "OUTD 19 digits");
    P.code[0].a = 65 << 8;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "OUTD width 65");
    P.code[0].a = -1;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "OUTD negative");
    P.code[0].a = 0;
    P.code[0].b = 4;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "OUTD flag 4");
    P.code[0].b = -1;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "OUTD flag -1");
    P.code[0] = (zl_ins_t){.op = ZL_CATCH, .a = 1, .b = 2};
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "CATCH mode 2");
    P.code[0].b = -1;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "CATCH mode -1");
    P.code[0].b = 1;
    CHECK(zl_verify(&P, &d) == ZL_OK, "CATCH mode 1");
    for (int op = ZL_RANGE; op <= ZL_OUTD; op++)
        for (int bits = 32; bits <= 64; bits += 30) {
            prog_reset(bits == 92 ? 62 : bits);
            I(op, op == ZL_RANGE ? 3 : op == ZL_WRAP ? 8 : 0, 0);
            I(ZL_HALT, 0, 0);
            CHECK(zl_verify(&P, &d) == ZL_OK, "%s in %d bits", zl_op_name(op), bits);
        }
    prog_reset(62);
    I(ZL_INEOF, 0, 0);
    I(ZL_SHL, 0, 0);
    I(ZL_SHR, 0, 0);
    I(ZL_HALT, 0, 0);
    CHECK(zl_verify(&P, &d) == ZL_OK, "in 62 bits too");
    CHECK(strcmp(zl_op_name(ZL_RANGE), "RANGE") == 0 && strcmp(zl_op_name(ZL_OUTD), "OUTD") == 0 &&
              strcmp(zl_op_name(ZL_INEOF), "INEOF") == 0,
          "E6 op names");
}

/* ------------------------------------------------- E7: reclamation */

static void test_gc(void)
{
    uint32_t L, W, D;
    const int64_t N = 4000; /* live pairs; with three dropped per step, 16000 allocations */
    /* a list kept in a slot survives collections; garbage beside it goes */
    prog_reset(62);
    P.nslots = 8;
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_STORE, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_STORE, 1, 0);
    L = here();
    I(ZL_LOAD, 1, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_ADD, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_STORE, 1, 0);
    I(ZL_LOAD, 0, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_STORE, 0, 0);
    for (int g = 0; g < 3; g++) {
        I(ZL_PUSH, 7, 0);
        I(ZL_PUSH, ZL_V_NIL, 0);
        I(ZL_CONS, 0, 0);
        I(ZL_DROP, 0, 0);
    }
    I(ZL_LOAD, 1, 0);
    I(ZL_PUSH, N, 0);
    I(ZL_LT, 0, 0);
    I(ZL_JNZ, L, 0);
    I(ZL_LOAD, 0, 0); /* an alias: identity must survive the moves */
    I(ZL_STORE, 2, 0);
    for (int pass = 0; pass < 2; pass++) { /* garbage again, to move the list twice */
        I(ZL_PUSH, 0, 0);
        I(ZL_STORE, 1, 0);
        uint32_t G = here();
        I(ZL_PUSH, 1, 0);
        I(ZL_PUSH, 2, 0);
        I(ZL_CONS, 0, 0);
        I(ZL_DROP, 0, 0);
        I(ZL_LOAD, 1, 0);
        I(ZL_PUSH, 1, 0);
        I(ZL_ADD, 0, 0);
        I(ZL_DUP, 0, 0);
        I(ZL_STORE, 1, 0);
        I(ZL_PUSH, 20000, 0);
        I(ZL_LT, 0, 0);
        I(ZL_JNZ, G, 0);
    }
    I(ZL_LOAD, 0, 0);
    I(ZL_LOAD, 2, 0);
    I(ZL_EQ, 0, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_PUSH, 0, 0); /* sum and length */
    I(ZL_STORE, 3, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_STORE, 5, 0);
    I(ZL_LOAD, 0, 0);
    I(ZL_STORE, 4, 0);
    W = here();
    I(ZL_LOAD, 4, 0);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_EQ, 0, 0);
    D = here();
    I(ZL_JNZ, 0, 0);
    I(ZL_LOAD, 3, 0);
    I(ZL_LOAD, 4, 0);
    I(ZL_CAR, 0, 0);
    I(ZL_ADD, 0, 0);
    I(ZL_STORE, 3, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_ADD, 0, 0);
    I(ZL_STORE, 5, 0);
    I(ZL_LOAD, 4, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_STORE, 4, 0);
    I(ZL_JMP, W, 0);
    patch(D, here());
    I(ZL_LOAD, 3, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_HALT, 0, 0);
    char want[64];
    snprintf(want, sizeof want, "1 %lld %lld", (long long) (N * (N + 1) / 2), (long long) N);
    expect_run(ZL_OK, want, "a 4000-pair list through collections");
    CHECK(R.collections >= 3, "it was moved by %u collections", R.collections);

    /* vectors, strings, a cycle and a frame cell */
    prog_reset(62);
    P.nslots = 8;
    sym("abc");
    sym("def");
    uint32_t j = here();
    I(ZL_JMP, 0, 0);
    uint32_t f = here(); /* f: a frame cell holds a pair through more garbage */
    I(ZL_ENTER, 1, 0);
    I(ZL_PUSH, 41, 0);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_STOREF, 0, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_STORE, 1, 0);
    uint32_t G = here();
    I(ZL_PUSH, 12345, 0); /* a two-cell string, dropped */
    I(ZL_SCONV, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_PUSH, 3, 0); /* a four-cell vector, dropped */
    I(ZL_VNEW, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_LOAD, 1, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_ADD, 0, 0);
    I(ZL_DUP, 0, 0);
    I(ZL_STORE, 1, 0);
    I(ZL_PUSH, 30000, 0);
    I(ZL_LT, 0, 0);
    I(ZL_JNZ, G, 0);
    I(ZL_LOADF, 0, 0);
    I(ZL_CAR, 0, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    patch(j, here());
    I(ZL_PUSH, 3, 0);
    I(ZL_VNEW, 0, 0);
    I(ZL_STORE, 5, 0);
    I(ZL_LOAD, 5, 0); /* v[0] = "abc" ++ "def", a made string */
    I(ZL_PUSH, 0, 0);
    I(ZL_PUSH, ZL_V_STRC, 0);
    I(ZL_PUSH, ZL_V_STRC + 1, 0);
    I(ZL_SCAT, 0, 0);
    I(ZL_VSET, 0, 0);
    I(ZL_LOAD, 5, 0); /* v[1] = (1 . 2) */
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 2, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_VSET, 0, 0);
    I(ZL_LOAD, 5, 0); /* v[2] = v: a cycle */
    I(ZL_PUSH, 2, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_VSET, 0, 0);
    I(ZL_LINK, 0, 0);
    I(ZL_CALL, f, 0);
    I(ZL_OUTI, 2, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_PUSH, 0, 0);
    I(ZL_VREF, 0, 0);
    I(ZL_OUTV, 0, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_VREF, 0, 0);
    I(ZL_CDR, 0, 0);
    I(ZL_OUTI, 1, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_PUSH, 2, 0);
    I(ZL_VREF, 0, 0);
    I(ZL_LOAD, 5, 0);
    I(ZL_EQ, 0, 0);
    I(ZL_OUTI, 1, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "41 abcdef 2 1", "vectors, strings, a cycle and a frame through collections");
    CHECK(R.collections >= 2, "blocks collected %u times", R.collections);

    /* a full heap of live pairs: one collection, then HEAP, at the same CONS */
    prog_reset(62);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_JMP, 1, 0);
    expect_run(ZL_E_HEAP, 0, "live pairs fill the heap");
    CHECK(R.collections == 1 && R.trap_pc == 2, "one collection (%u)", R.collections);
    /* a collection frees what the stack no longer holds, then the retry fits */
    prog_reset(62);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, ZL_V_NIL, 0);
    I(ZL_CONS, 0, 0); /* (1) */
    L = here();
    I(ZL_PUSH, 1, 0);
    I(ZL_SWAP, 0, 0);
    I(ZL_CONS, 0, 0); /* (1 1): a new head on the kept tail */
    I(ZL_CDR, 0, 0);  /* drop the head again: it becomes garbage */
    I(ZL_JMP, L, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.fuel = 6 * 40000;
    CHECK(zl_run(&P, &R) == ZL_E_FUEL && R.collections >= 1, "a growing list whose tail is kept");
    /* garbage of one kind does not stop the other */
    prog_reset(62);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_PUSH, 7, 0);
    I(ZL_VNEW, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_JMP, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.fuel = 8 * 50000;
    CHECK(zl_run(&P, &R) == ZL_E_FUEL && R.collections >= 2, "pairs and vectors both collected");
    /* fuel is the same with or without collections: the retry is free */
    prog_reset(62);
    I(ZL_PUSH, 1, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_CONS, 0, 0);
    I(ZL_DROP, 0, 0);
    I(ZL_JMP, 0, 0);
    memset(&R, 0, sizeof R);
    R.out = out;
    R.out_cap = sizeof out;
    R.fuel = 5 * 20000;
    CHECK(zl_run(&P, &R) == ZL_E_FUEL && R.used == 5 * 20000 && R.collections == 1,
          "20000 pairs: one collection, fuel exact (%llu, %u)", (unsigned long long) R.used,
          R.collections);
    /* a 32-bit program never holds references: nothing to collect, ever */
    CHECK(strcmp(zl_op_name(ZL_JMPI), "JMPI") == 0, "JMPI's name");
    /* JMPI: a tail call through a value runs in constant call depth */
    prog_reset(62);
    P.nslots = 4;
    j = here();
    I(ZL_JMP, 0, 0);
    f = here(); /* f(n): n == 0 ? print : f(n - 1), as a tail call */
    I(ZL_ENTER, 1, 1);
    I(ZL_LOADF, 0, 0);
    uint32_t z = here();
    I(ZL_JZ, 0, 0);
    I(ZL_LOADF, 0, 0);
    I(ZL_PUSH, 1, 0);
    I(ZL_SUB, 0, 0);
    I(ZL_LINK, 1, 0); /* the static link, taken before LEAVE */
    I(ZL_SWAP, 0, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_PUSH, ZL_V_CODE + f, 0);
    I(ZL_JMPI, 0, 0);
    patch(z, here());
    I(ZL_LOADF, 0, 0);
    I(ZL_OUTI, 0, 0);
    I(ZL_LEAVE, 0, 0);
    I(ZL_RET, 0, 0);
    patch(j, here());
    I(ZL_LINK, 0, 0);
    I(ZL_PUSH, 100000, 0);
    I(ZL_CALL, f, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_OK, "0", "100000 tail calls through a value, far past the call depth");
    prog_reset(62);
    I(ZL_PUSH, 5, 0);
    I(ZL_JMPI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_DOMAIN, 0, "JMPI to a number");
    prog_reset(62);
    I(ZL_JMPI, 0, 0);
    I(ZL_HALT, 0, 0);
    expect_run(ZL_E_STACK, 0, "JMPI on an empty stack");
    prog_reset(64);
    I(ZL_JMPI, 0, 0);
    I(ZL_HALT, 0, 0);
    zl_diag_t d;
    CHECK(zl_verify(&P, &d) == ZL_E_VERIFY, "JMPI only in 62-bit programs");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "src/lingua/golden";
    test_arith();
    test_unary();
    test_stack_and_flow();
    test_memory_io();
    test_verify();
    test_equal_digest();
    test_basic();
    test_fortran();
    test_forth();
    test_langs();
    test_api();
    test_pack();
    test_frames();
    test_lists();
    test_ext();
    test_e6();
    test_gc();
    test_golden(dir);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
