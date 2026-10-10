/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_test.h — tiny host test harness shared by the cbank tests (host only). */
#ifndef ZXV_CB_TEST_H
#define ZXV_CB_TEST_H

#include <stdio.h>
#include <string.h>

static int cb_t_pass, cb_t_fail;

#define CHECK(cond, name)                                                                          \
    do {                                                                                           \
        if (cond) {                                                                                \
            cb_t_pass++;                                                                           \
            printf("[PASS] %s\n", name);                                                           \
        } else {                                                                                   \
            cb_t_fail++;                                                                           \
            printf("[FAIL] %s (%s:%d)\n", name, __FILE__, __LINE__);                               \
        }                                                                                          \
    } while (0)

#define CB_TEST_DONE(mod)                                                                          \
    do {                                                                                           \
        printf("%s: %d passed, %d failed\n", mod, cb_t_pass, cb_t_fail);                           \
        return cb_t_fail ? 1 : 0;                                                                  \
    } while (0)

#endif /* ZXV_CB_TEST_H */
