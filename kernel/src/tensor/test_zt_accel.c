/* test_zt_accel.c — the tensor engine's GPU accelerator front door
 * (zt_accel.c), host tests. Proves the no-orphan contract: the module is
 * built, its registry is coherent, and the CPU-only fallback is explicit
 * and fail-closed — never a silent success on a machine with no GPU.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * The build under test has no GPU backend compiled in (no ZT_HAVE_METAL,
 * no ZA_DL), which is the honest default for a host test: zt_accel_open
 * must REFUSE with a clear reason, not pretend. The GPU backends
 * themselves are proven separately by the vtrace GPU suite (the same
 * kernels, bit-identical on the CPU emulator and PTX for sm_86). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "zt_accel.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            return 1;                                                                              \
        }                                                                                          \
        checks++;                                                                                  \
    } while (0)

int main(void)
{
    char err[256];
    zt_accel_t *a;

    /* ---- 1. no GPU backend compiled in: refuse, fail-closed -------------- */
    a = zt_accel_open(err, sizeof err);
    CHECK(a == NULL);
    CHECK(strstr(err, "no GPU backend") != NULL);
    printf("  no-backend refusal: %s\n", err);

    /* ---- 2. ZT_GPU=cpu: the explicit CPU-only request is honoured -------- */
    setenv("ZT_GPU", "cpu", 1);
    a = zt_accel_open(err, sizeof err);
    CHECK(a == NULL);
    CHECK(strstr(err, "ZT_GPU") != NULL);
    printf("  cpu-only request: %s\n", err);
    unsetenv("ZT_GPU");

    /* ---- 3. ZT_GPU naming a backend not compiled in: reported, not silent */
    setenv("ZT_GPU", "cuda", 1);
    a = zt_accel_open(err, sizeof err);
    CHECK(a == NULL);
    CHECK(strstr(err, "not built in") != NULL || strstr(err, "no GPU backend") != NULL);
    printf("  unbuilt-backend request: %s\n", err);
    unsetenv("ZT_GPU");

    /* ---- 4. the API contract: NULL-safe accessors, the CPU passthrough ---- */
    zt_accel_free(NULL);
    CHECK(zt_accel_api(NULL) != NULL && zt_accel_api(NULL)[0] == 0); /* ""  */
    CHECK(zt_accel_name(NULL)[0] == 0);
    CHECK(zt_accel_state_align() == 65536u); /* the GPU page alignment      */
    CHECK(zt_accel_map(NULL, err, 16u) == -1);
    CHECK(zt_accel_map_state(NULL, err, 16u) == -1);

    /* ---- 5. the kernel passthrough: with no accelerator the caller's
     * kernels are used verbatim — the exact CPU-forward fallback ------ */
    {
        zt_kern_t base, out;
        memset(&base, 0x5A, sizeof base);
        memset(&out, 0, sizeof out);
        zt_accel_kern(NULL, &out, &base, true);
        CHECK(memcmp(&base, &out, sizeof base) == 0);
    }

    printf("zt_accel: %d checks passed (host, no GPU backend — fail-closed)\n", checks);
    return 0;
}