/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxva_cuda.c — the CUDA backend of the analyser's heavy stage
 * (zxva_cuda.c) against the C reference, word for word, on an NVIDIA GPU.
 * Nothing CUDA is needed to build it:
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST -Iinclude -Isrc/modbind \
 *       -Isrc/vaudio src/vaudio/test_zxva_cuda.c src/vaudio/zxva_cuda.c \
 *       src/vaudio/zxva_analyse.c src/vaudio/zxva.c src/robin_debanks/sha256.c \
 *       -ldl -o /tmp/test_zxva_cuda && /tmp/test_zxva_cuda
 *
 * Checks (test_zxva_backend.h): random plans word for word, then whole
 * analyses with identical bytes. With no driver, NVRTC or GPU it prints
 * [SKIP] and passes, unless ZXVA_CUDA_REQUIRE=1.
 *
 * "test_zxva_cuda --source" prints the kernel source instead, for the
 * offline checks (clang to PTX for sm_75/86/90 with test_zxva_cuda_ptx.h,
 * and test_zxva_cuda_emu.cpp on the CPU): tools/zxva/test_zxva_gpu.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_zxva_backend.h"
#include "zxva_cuda.h"

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--source") == 0) {
        char *s = zxva_cuda_source();
        if (!s) return 1;
        fputs(s, stdout);
        free(s);
        return 0;
    }
    char err[512];
    zxva_cuda_t *g = zxva_cuda_new(0, err, sizeof err);
    if (!g) {
        const char *req = getenv("ZXVA_CUDA_REQUIRE");
        printf("[SKIP] no CUDA GPU: %s\n", err);
        if (req && strcmp(req, "1") == 0) {
            printf("[FAIL] ZXVA_CUDA_REQUIRE=1\n");
            return 1;
        }
        printf("test_zxva_cuda: skipped (built, no GPU)\n");
        return 0;
    }
    printf("[INFO] GPU: %s\n", zxva_cuda_name(g));
    zxva_backend_t be;
    zxva_cuda_backend(g, &be);
    int fails = 0, words = 0, files = 0;
    int b1 = tb_plans(&be, 200, &words);
    printf("[%s] 200 random plans word for word (%d words)\n", b1 ? "FAIL" : "PASS", words);
    int b2 = tb_analyses(&be, &files);
    printf("[%s] %d whole analyses: same ZXVA bytes as the CPU\n", b2 ? "FAIL" : "PASS", files);
    fails = (b1 != 0) + (b2 != 0);
    printf("[INFO] kernel time %.2f ms\n", zxva_cuda_kernel_ms(g));
    zxva_cuda_free(g);
    printf("test_zxva_cuda: %d passed, %d failed (on %s)\n", 2 - fails, fails, "GPU");
    return fails != 0;
}
