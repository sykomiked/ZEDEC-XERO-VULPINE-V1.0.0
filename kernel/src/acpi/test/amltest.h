/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* amltest.h — check macros and helpers shared by the AML host tests. */
#ifndef ZXV_AMLTEST_H
#define ZXV_AMLTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../aml.h"
#include "../aml_int.h"
#include "amlfake.h"
#include "aml_fixtures.h"

static int g_checks, g_fails;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fails++;                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        unsigned long long a_ = (unsigned long long) (a), b_ = (unsigned long long) (b); \
        g_checks++;                                                                    \
        if (a_ != b_) {                                                                \
            g_fails++;                                                                 \
            printf("FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__, __LINE__, #a, #b, \
                   a_, b_);                                                            \
        }                                                                              \
    } while (0)

#define ARENA_SIZE (4u << 20)
static uint8_t g_arena[ARENA_SIZE] __attribute__((aligned(16)));

static inline int setup(amlfake_t *f, aml_ctx_t *c, const uint8_t *tbl, uint32_t len)
{
    fk_init(f);
    if (aml_init(c, &f->plat, g_arena, ARENA_SIZE)) return -100;
    return tbl ? aml_load_table(c, tbl, len) : 0;
}

/* Evaluate an integer-returning path; returns the status. */
static inline int ev_int(aml_ctx_t *c, const char *path, uint64_t *v)
{
    *v = 0xDEADDEADDEADDEADull;
    return aml_eval_int(c, 0, path, v);
}

static inline int ev_int1(aml_ctx_t *c, const char *path, uint64_t arg, uint64_t *v)
{
    aml_val_t a = aml_int(arg), r;
    int e = aml_eval(c, 0, path, &a, 1, &r);
    *v = 0xDEADDEADDEADDEADull;
    if (e) return e;
    e = aml_val_to_int(c, &r, v);
    aml_val_free(c, &r);
    return e;
}

/* 1 when v is a string equal to s. */
static inline int val_is_str(const aml_val_t *v, const char *s)
{
    size_t n = strlen(s);
    return v->type == AML_T_STR && v->len == n && memcmp(v->u.bytes, s, n) == 0 &&
           v->u.bytes[n] == 0;
}

static inline int val_is_buf(const aml_val_t *v, const uint8_t *b, uint32_t n)
{
    return v->type == AML_T_BUF && v->len == n && memcmp(v->u.bytes, b, n) == 0;
}

static inline int finish_tests(const char *name)
{
    printf("%s: %d checks, %d failed\n", name, g_checks, g_fails);
    return g_fails ? 1 : 0;
}

#endif
