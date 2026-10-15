/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxva_vk.c — the Vulkan backend of the analyser's heavy stage
 * (zxva_vk.c) against the C reference, word for word, on any Vulkan device
 * with shaderInt64 (a GPU, or lavapipe on the CPU):
 *
 *   sh src/vaudio/gen_zxva_vk_spv.sh /tmp/zxva_vk_spv.h
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -DTEST_HOST -Iinclude -Isrc/modbind \
 *       -Isrc/vaudio -DZXVA_VK_SPV_H='"/tmp/zxva_vk_spv.h"' src/vaudio/test_zxva_vk.c \
 *       src/vaudio/zxva_vk.c src/vaudio/zxva_analyse.c src/vaudio/zxva.c \
 *       src/robin_debanks/sha256.c -ldl -o /tmp/test_zxva_vk && /tmp/test_zxva_vk
 *
 * Built without ZXVA_VK_SPV_H (or without the Vulkan headers) it checks that
 * the stubs say so and that the analyser falls back to the CPU. With no
 * device it prints [SKIP] and passes, unless ZXVA_VK_REQUIRE=1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_zxva_backend.h"
#include "zxva_vk.h"

int main(void)
{
    char err[512];
    zxva_vk_t *g = zxva_vk_new(-1, err, sizeof err);
    const char *req = getenv("ZXVA_VK_REQUIRE");
    if (!g) {
        printf("[SKIP] no Vulkan backend: %s\n", err);
        /* the fallback still analyses, on the CPU */
        zxva_backend_t be;
        zxva_vk_backend(NULL, &be);
        int files = 0, bad = tb_analyses(&be, &files);
        printf("[%s] fallback backend \"%s\": %d analyses identical\n", bad ? "FAIL" : "PASS",
               be.name, files);
        if (bad || (req && strcmp(req, "1") == 0)) return 1;
        printf("test_zxva_vk: 1 passed, 0 failed (no device)\n");
        return 0;
    }
    printf("[INFO] device: %s\n", zxva_vk_name(g));
    zxva_backend_t be;
    zxva_vk_backend(g, &be);
    int words = 0, files = 0;
    int b1 = tb_plans(&be, 200, &words);
    printf("[%s] 200 random plans word for word (%d words)\n", b1 ? "FAIL" : "PASS", words);
    int b2 = tb_analyses(&be, &files);
    printf("[%s] %d whole analyses: same ZXVA bytes as the CPU\n", b2 ? "FAIL" : "PASS", files);
    printf("[INFO] kernel time %.2f ms\n", zxva_vk_kernel_ms(g));
    zxva_vk_free(g);
    int fails = (b1 != 0) + (b2 != 0);
    printf("test_zxva_vk: %d passed, %d failed (on %s)\n", 2 - fails, fails, "a Vulkan device");
    return fails != 0;
}
