/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ai_selfcheck.c — host side of the boot AI self-check.
 *
 * The kernel prints "[AI_OK] swarm+tensor selfcheck 0x<hash>" only when its
 * own computation equals AI_BOOT_HASH. This test runs the SAME functions on
 * the host and requires the SAME constants, so a passing host test plus an
 * [AI_OK] line on bare metal means the two builds computed identical bits.
 * It also recomputes each constant independently of the code under test:
 *   - the swarm hash from the hand-worked allotments in swarm_boot.h;
 *   - the matvec from a plain integer dot product;
 * and prints the hashes so a changed constant is easy to read off. */
#include <stdio.h>
#include <string.h>
#include "swarm_boot.h"
#include "../tensor/zt_boot.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s\n", msg);                                                           \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("  [PASS] %s\n", msg);                                                          \
        }                                                                                          \
    } while (0)

static char g_line[256];
static void capture(const char *s)
{
    strncpy(g_line, s, sizeof g_line - 1);
}

int main(void)
{
    /* Swarm: the hash of the hand-worked numbers, without the allocator. */
    const uint32_t hand[] = {454, 137, 136, 61, 61, 60, 19, 18, 18, 18, 18, /* R4 allotments */
                             454, 273, 182, 91,                             /* R3 level budgets */
                             454, 10,                                       /* grants */
                             536};                                          /* R6 unused */
    uint32_t h = ZT_BOOT_FNV_INIT;
    for (unsigned i = 0; i < sizeof hand / sizeof hand[0]; i++) h = zt_boot_fnv(h, hand[i]);
    printf("  swarm hand hash 0x%08x\n", h);
    CHECK(h == SWARM_BOOT_HASH, "SWARM_BOOT_HASH equals the hash of the hand-worked cycle");

    /* FNV-1a known answers: four zero bytes, and "abcd" fed as one LE word. */
    CHECK(zt_boot_fnv(ZT_BOOT_FNV_INIT, 0) == 0x4b95f515u, "FNV-1a of four zero bytes");
    CHECK(zt_boot_fnv(ZT_BOOT_FNV_INIT, 0x64636261u) == 0xce3479bdu, "FNV-1a of \"abcd\"");

    uint32_t hs = 1, ht = 1;
    int fs = swarm_boot_selfcheck(&hs);
    printf("  swarm selfcheck step %d hash 0x%08x\n", fs, hs);
    CHECK(fs == 0 && hs == SWARM_BOOT_HASH, "swarm_boot_selfcheck passes on the host");

    int ft = zt_boot_selfcheck(&ht);
    printf("  tensor selfcheck step %d hash 0x%08x\n", ft, ht);
    CHECK(ft == 0 && ht == ZT_BOOT_HASH, "zt_boot_selfcheck passes on the host");

    uint32_t ha = zt_boot_fnv(zt_boot_fnv(ZT_BOOT_FNV_INIT, SWARM_BOOT_HASH), ZT_BOOT_HASH);
    printf("  combined hash 0x%08x\n", ha);
    CHECK(ha == AI_BOOT_HASH, "AI_BOOT_HASH = fnv(fnv(init, swarm), tensor)");

    /* The line the kernel prints, byte for byte. */
    char want[64];
    snprintf(want, sizeof want, "[AI_OK] swarm+tensor selfcheck 0x%08x\n", AI_BOOT_HASH);
    CHECK(ai_boot_selfcheck(capture) && strcmp(g_line, want) == 0,
          "ai_boot_selfcheck prints the [AI_OK] line");

    /* Second run is identical (no hidden state carried between runs). */
    uint32_t hs2 = 0, ht2 = 0;
    CHECK(swarm_boot_selfcheck(&hs2) == 0 && zt_boot_selfcheck(&ht2) == 0 && hs2 == hs && ht2 == ht,
          "self-checks are repeatable");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
