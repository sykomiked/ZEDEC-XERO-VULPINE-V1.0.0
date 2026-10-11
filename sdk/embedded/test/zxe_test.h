/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxe_test.h — check macros and helpers for the sdk/embedded host tests. */
#ifndef ZXE_TEST_H
#define ZXE_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_checks, g_fail;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_fail++;                                                 \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            fprintf(stderr, __VA_ARGS__);                             \
            fprintf(stderr, "\n");                                    \
        }                                                             \
    } while (0)

#define IS(expr, want)                                                                  \
    do {                                                                                \
        long long got_ = (long long) (expr);                                            \
        CHECK(got_ == (long long) (want), "%s = %lld, want %lld", #expr, got_,          \
              (long long) (want));                                                      \
    } while (0)

/* Run fn in a child process; 1 if the child died by a signal (abort) or
 * exited with status 70 (a fatal handler), i.e. the port failed closed. */
static inline int dies(void (*fn)(void))
{
    fflush(NULL);
    pid_t p = fork();
    if (p == 0) {
        if (!freopen("/dev/null", "w", stderr)) _exit(2);
        fn();
        _exit(0);
    }
    int st = 0;
    if (p < 0 || waitpid(p, &st, 0) != p) return 0;
    return WIFSIGNALED(st) || (WIFEXITED(st) && WEXITSTATUS(st) == 70);
}

static inline int finish(const char *name)
{
    printf("%s: %d checks, %d failures\n", name, g_checks, g_fail);
    return g_fail ? 1 : 0;
}

#endif /* ZXE_TEST_H */
