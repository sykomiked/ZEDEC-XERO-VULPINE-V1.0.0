/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_case.h — one projected program and what zl_run did with it, as
 * test_zl_proj writes them for zxp_driver. */
#ifndef ZXP_CASE_H
#define ZXP_CASE_H

#include "zxp_rt.h"
#include "cases_fn.h"

typedef struct {
    zxp_fn_t c, x; /* the C and the x86-64 projection */
    uint64_t fuel;
    size_t cap;
    int status;
    uint64_t used;
    uint32_t trap_pc;
    size_t nin;
    int64_t in[4];
    size_t len;
    const char *out;
    const char *what;
} zxp_case_t;

extern const zxp_case_t zxp_cases[];
extern const unsigned zxp_ncases;

#endif
