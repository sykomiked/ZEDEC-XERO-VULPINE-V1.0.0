/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_x86_extable.c — host test for the exception-fixup table lookup that
 * the x86_64 #GP handler uses (x86_extable.c). Run under ASan/UBSan by
 * `make -C kernel test-x86-extable` (part of verify-all). */
#include <stdio.h>
#include "x86_extable.h"

static int g_fail, g_pass;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, #c);                                   \
        }                                                                                          \
    } while (0)

int main(void)
{
    /* Shaped like the kernel table: rdmsr + wrmsr entries, plus extras. */
    const x86_extable_entry_t t[] = {
        {0x100010u, 0x100030u}, /* rdmsr -> fixup */
        {0x100050u, 0x100060u}, /* wrmsr -> fixup */
        {0x200000u, 0},         /* fixup 0 = no recovery: must be ignored */
        {0x100010u, 0x999999u}, /* duplicate insn: first entry wins */
        {0xFFFFFFFFFFFFFFF0u, 0xFFFFFFFFFFFFFFF8u},
    };
    const x86_extable_entry_t *b = t, *e = t + sizeof t / sizeof t[0];

    /* hits */
    CHECK(x86_extable_search(b, e, 0x100010u) == 0x100030u);
    CHECK(x86_extable_search(b, e, 0x100050u) == 0x100060u);
    CHECK(x86_extable_search(b, e, 0xFFFFFFFFFFFFFFF0u) == 0xFFFFFFFFFFFFFFF8u);
    /* misses: neighbours of a hit, fixup addresses, 0, an entry with no fixup */
    CHECK(x86_extable_search(b, e, 0x10000Fu) == 0);
    CHECK(x86_extable_search(b, e, 0x100011u) == 0);
    CHECK(x86_extable_search(b, e, 0x100030u) == 0);
    CHECK(x86_extable_search(b, e, 0) == 0);
    CHECK(x86_extable_search(b, e, 0x200000u) == 0);
    /* last entry is inside [b, e); one past it is not read (ASan would flag) */
    CHECK(x86_extable_search(b, e - 1, 0xFFFFFFFFFFFFFFF0u) == 0);
    /* empty, reversed and NULL bounds */
    CHECK(x86_extable_search(b, b, 0x100010u) == 0);
    CHECK(x86_extable_search(e, b, 0x100010u) == 0);
    CHECK(x86_extable_search(NULL, e, 0x100010u) == 0);
    CHECK(x86_extable_search(b, NULL, 0x100010u) == 0);
    CHECK(x86_extable_search(NULL, NULL, 0) == 0);

    /* Every address in a window resolves only at the registered insns. */
    unsigned hits = 0;
    for (uint64_t rip = 0x100000u; rip < 0x100100u; rip++)
        if (x86_extable_search(b, e, rip)) hits++;
    CHECK(hits == 2);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
