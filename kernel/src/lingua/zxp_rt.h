/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_rt.h — the small checked runtime a projected program runs on
 * (zl_project's C and x86-64 targets, docs/spec/ALPHABET.md).
 *
 * A projected program is one function, int f(zxp_io_t *io). It keeps the
 * IR's data stack, return stack and slots in io, counts one unit of fuel
 * per IR instruction as zl_run does, and returns ZL_OK or the same trap
 * status zl_run would, with io->trap_pc set to the same instruction. What
 * it prints, its fuel and its traps match zl_run exactly; the runtime holds
 * the parts that are easier to share than to generate (printing, input,
 * division, powers, shifts and wrapping). Freestanding C11, no libc. */
#ifndef ZXP_RT_H
#define ZXP_RT_H

#include <stddef.h>
#include <stdint.h>
#include "zl.h"

typedef struct {
    uint64_t fuel, used; /* fuel: instructions allowed (0: 10^7) */
    uint32_t trap_pc, pad;
    const int64_t *in;
    uint64_t nin, inpos;
    char *out;
    uint64_t cap, len, col;
    int64_t stack[ZL_MAX_STACK];
    uint32_t rstack[ZL_MAX_CALLS];
    int64_t mem[ZL_MAX_SLOTS];
} zxp_io_t;

typedef int (*zxp_fn_t)(zxp_io_t *io);

/* Reset the counters and zero the first nslots slots. */
void zxp_begin(zxp_io_t *io, uint32_t nslots);
int zxp_fits(uint32_t bits, int64_t v);
/* ZL_OUTI, ZL_OUTS, ZL_OUTC, ZL_NL, ZL_TAB, ZL_OUTD */
int zxp_outi(zxp_io_t *io, int64_t x, int64_t flags, int64_t width);
int zxp_outs(zxp_io_t *io, const char *s, uint64_t n);
int zxp_outc(zxp_io_t *io, int64_t x);
int zxp_nl(zxp_io_t *io);
int zxp_tab(zxp_io_t *io, int64_t zone);
int zxp_outd(zxp_io_t *io, int64_t x, int64_t a, int64_t b);
/* ZL_IN: the next input, checked against the word width */
int zxp_in(zxp_io_t *io, uint32_t bits, int64_t *x);
/* ZL_DIVT .. ZL_DIVX, ZL_POW (mode), ZL_SHL and ZL_SHR, ZL_WRAP, ZL_RANGE */
int zxp_div(int op, int64_t x, int64_t y, uint32_t bits, int64_t *z);
int zxp_pow(int64_t x, int64_t y, int64_t mode, uint32_t bits, int64_t *z);
int zxp_shift(int op, int64_t x, int64_t y, uint32_t bits, int64_t *z);
int zxp_wrap(int64_t x, int64_t a, int64_t b, uint32_t bits, int64_t *z);
int zxp_range(int64_t x, int64_t a, int64_t b);

#endif
