/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_internal.h — z-audit internals (not part of the kit API). Integer
 * code only (R11). */
#ifndef ZAU_INTERNAL_H
#define ZAU_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zaudit.h"
#include "zcontract.h"
#include "zw_wasm.h"
#include "zwasm.h"

/* ----- the module as z-audit sees it ----- */
#define ZAU_NAME_MAX  63u   /* longer names are cut (and cannot be allowed)   */
#define ZAU_MAX_ITEMS 65536u /* types, imports, functions, exports, globals   */
#define ZAU_MAX_SIG   16u   /* params / results kept per type                 */

typedef struct {
    uint32_t np, nr;           /* the real counts                             */
    uint8_t p[ZAU_MAX_SIG];    /* the first ZAU_MAX_SIG value types           */
    uint8_t r[ZAU_MAX_SIG];
    int has_float;             /* any f32 / f64 / v128 among them             */
} zau_ftype;

typedef struct {
    char mod[ZAU_NAME_MAX + 1], name[ZAU_NAME_MAX + 1];
    int cut;                   /* a name was longer than ZAU_NAME_MAX         */
    uint8_t kind;              /* 0 func, 1 table, 2 memory, 3 global         */
    uint32_t type;             /* kind 0: type index; kind 3: value type      */
    size_t off;
} zau_import;

typedef struct {
    char name[ZAU_NAME_MAX + 1];
    int cut;
    uint8_t kind;
    uint32_t idx;
    size_t off;
} zau_export;

typedef struct {
    size_t start, code, end;   /* body start, first instruction, one past end */
    uint32_t nlocals;          /* declared locals (not params), capped        */
    int locals_ok;             /* nlocals <= 256 and no float local           */
    uint8_t local_type[256];
} zau_body;

typedef struct {
    int parsed;                /* 1 if the walk reached the end of the module */
    char err[200];
    size_t err_off;
    zau_ftype *types;
    uint32_t ntypes;
    zau_import *imports;
    uint32_t nimports, nfimports;
    uint32_t *funcs;           /* type index of each defined function         */
    uint32_t nfuncs;
    zau_export *exports;
    uint32_t nexports;
    zau_body *bodies;
    uint32_t nbodies;
} zau_module;

/* ----- the report ----- */
#define ZAU_CHECKS        7
#define ZAU_MAX_FINDINGS  4096u
#define ZAU_PER_CHECK_MAX 256u

#define ZAU_C_VALIDATE   0
#define ZAU_C_FLOAT      1
#define ZAU_C_IMPORTS    2
#define ZAU_C_PARSE      3
#define ZAU_C_DECLARED   4
#define ZAU_C_BOUNDS     5
#define ZAU_C_ANALYSIS   6

typedef struct {
    uint32_t check;
    int32_t severity;
    char code[32];
    char *message;
    char exp[ZAU_NAME_MAX + 1];
    int64_t func, offset;
} zau_finding;

typedef struct {
    int32_t status;            /* ZAU_PASS.. ; computed at the end            */
    int skipped;
    char summary[200];
    uint32_t n[3];             /* findings per severity                       */
    uint32_t suppressed;       /* past ZAU_PER_CHECK_MAX                      */
} zau_check;

struct zau_report {
    zau_check checks[ZAU_CHECKS];
    zau_finding *f;
    uint32_t nf, fcap;
    uint32_t errors, warnings;
    int strict;
    int oom;
    uint8_t module_hash[32];
    size_t module_len;
    int has_contract;
    uint8_t contract_digest[32];
};

struct zau_options {
    zw_policy *policy;
    uint32_t flags;
};

/* zau_util.c */
void zau_set_error(const char *msg);
/* Add a finding (message printf-style; invalid UTF-8 becomes '?'). */
void zau_add(zau_report *r, uint32_t check, int32_t severity, const char *code, const char *exp,
             int64_t func, int64_t offset, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 8, 9)))
#endif
    ;
void zau_skip(zau_report *r, uint32_t check, const char *why);
void zau_summary(zau_report *r, uint32_t check, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
/* Copy at most cap-1 bytes of s[0..n) into out, making it valid UTF-8 with
 * no control characters ('?' for each bad byte); returns 1 if cut. */
int zau_clean(char *out, size_t cap, const uint8_t *s, size_t n);
const char *zau_vt_name(uint8_t t);
int zau_vt_float(uint8_t t);
/* wasm3 signature text of a type ("i(Ii)"); "?" past ZAU_MAX_SIG. */
void zau_sig(const zau_ftype *t, char *out, size_t cap);

/* zau_module.c: walk the module; findings for module.float-simd go to r. */
void zau_module_read(zau_module *m, const uint8_t *w, size_t len, zau_report *r);
void zau_module_free(zau_module *m);
/* Type of function index fidx (imports first); NULL if out of range. */
const zau_ftype *zau_func_type(const zau_module *m, uint32_t fidx);

/* zau_bounds.c: contract.bounds feasibility helpers. */
/* 1 feasible, 0 proven empty, -1 not determined within the bound. */
int zau_rat_feasible(const zc_bounds *b);

/* zau_absint.c: exports.analysis for one export. */
void zau_analyze(zau_report *r, const zau_module *m, const uint8_t *w, const char *exp,
                 uint32_t fidx, const zc_contract *c, const char *fn);

#endif /* ZAU_INTERNAL_H */
