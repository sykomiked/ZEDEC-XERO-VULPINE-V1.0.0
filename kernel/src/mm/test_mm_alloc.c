/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_mm_alloc.c -- size-wrap checks on every allocator a kernel image reaches.
 *
 *   kmalloc / kcalloc / krealloc / kfree   (src/mm/mm.c, linked-list heap)
 *   fs_malloc / fs_calloc                   (include/freestanding.h, bump arena)
 *
 * Each must refuse a request whose size (or count * size) wraps, instead of
 * handing back a block smaller than the caller believes it got. The heap here
 * is a static buffer standing in for KERNEL_HEAP_BASE, because mm_init() writes
 * to that fixed physical address, which a host process does not own.
 */
#include <stdint.h>
#include <stdio.h>
#include "freestanding.h"
#include "mm.h"
#undef printf /* freestanding.h stubs printf out; this host test reports */

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

#define HEAP_BYTES (64u * 1024u)
static uint8_t g_heap[HEAP_BYTES] __attribute__((aligned(16)));
static mm_state_t g_mm;

static void heap_reset(void)
{
    for (uint32_t i = 0; i < HEAP_BYTES; i++) g_heap[i] = 0xA5;
    g_mm.heap_used = 0;
    g_mm.heap_head = (heap_block_t *) (void *) g_heap;
    g_mm.heap_head->size = HEAP_BYTES - (uint32_t) sizeof(heap_block_t);
    g_mm.heap_head->free = true;
    g_mm.heap_head->next = 0;
    g_mm.heap_head->prev = 0;
}

static int in_heap(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *) p;
    return b >= g_heap && b + n <= g_heap + HEAP_BYTES;
}

static void test_kmalloc(void)
{
    printf("=== kmalloc / kcalloc / krealloc ===\n");
    heap_reset();
    CHECK(kmalloc(&g_mm, 0) == 0, "kmalloc(0) refused");
    CHECK(kmalloc(&g_mm, 0xFFFFFFFFu) == 0, "kmalloc(UINT32_MAX) refused (size + 7 would wrap)");
    CHECK(kmalloc(&g_mm, 0xFFFFFFFDu) == 0, "kmalloc(UINT32_MAX - 2) refused");
    CHECK(kmalloc(&g_mm, HEAP_MAX + 1u) == 0, "kmalloc(HEAP_MAX + 1) refused");
    CHECK(g_mm.heap_used == 0, "refusals did not touch the accounting");

    void *p = kmalloc(&g_mm, 100);
    CHECK(p != 0 && in_heap(p, 100), "kmalloc(100) inside the heap");
    CHECK(((uintptr_t) p & 7u) == 0, "kmalloc result 8-byte aligned");

    /* count * size wraps 32 bits: 0x10000 * 0x10000 == 2^32 -> 0 */
    CHECK(kcalloc(&g_mm, 0x10000u, 0x10000u) == 0,
          "kcalloc(2^16, 2^16) refused (product wraps to 0)");
    CHECK(kcalloc(&g_mm, 0x10001u, 0xFFFFu) == 0,
          "kcalloc(0x10001, 0xFFFF) refused (product wraps)");
    CHECK(kcalloc(&g_mm, 0xFFFFFFFFu, 2u) == 0, "kcalloc(UINT32_MAX, 2) refused");
    CHECK(kcalloc(&g_mm, 3u, 0xAAAAAAABu) == 0, "kcalloc(3, 0xAAAAAAAB) refused (wraps to 1)");
    CHECK(kcalloc(&g_mm, 0, 16) == 0, "kcalloc with a zero product refused like kmalloc(0)");

    uint8_t *z = (uint8_t *) kcalloc(&g_mm, 25u, 4u);
    int zero = z != 0;
    for (uint32_t i = 0; z && i < 100; i++) zero = zero && z[i] == 0;
    CHECK(zero, "kcalloc(25, 4) returns 100 zeroed bytes");

    uint32_t used = g_mm.heap_used;
    CHECK(krealloc(&g_mm, z, 0xFFFFFFFFu) == 0, "krealloc to UINT32_MAX refused");
    CHECK(g_mm.heap_used == used, "failed krealloc kept the old block");
    uint8_t *g = (uint8_t *) krealloc(&g_mm, z, 400u);
    CHECK(g != 0 && in_heap(g, 400), "krealloc growth inside the heap");
    int kept = g != 0;
    for (uint32_t i = 0; g && i < 100; i++) kept = kept && g[i] == 0;
    CHECK(kept, "krealloc kept the old contents");

    kfree(&g_mm, g);
    kfree(&g_mm, p);
    CHECK(g_mm.heap_used == 0, "every block returned: heap_used back to 0");
    kfree(&g_mm, p); /* double free must not underflow the accounting */
    CHECK(g_mm.heap_used == 0, "double kfree ignored");
}

static void test_fs_alloc(void)
{
    printf("=== fs_malloc / fs_calloc ===\n");
    CHECK(fs_malloc((size_t) -1) == 0, "fs_malloc(SIZE_MAX) refused");
    CHECK(fs_malloc((size_t) 1 << 21) == 0, "fs_malloc(2 MB) refused (arena is 1 MB)");
    CHECK(fs_calloc((size_t) 1 << (sizeof(size_t) * 4), (size_t) 1 << (sizeof(size_t) * 4)) == 0,
          "fs_calloc(2^(w/2), 2^(w/2)) refused (product wraps to 0)");
    CHECK(fs_calloc((size_t) -1, 2) == 0, "fs_calloc(SIZE_MAX, 2) refused");
    CHECK(fs_calloc(3, ((size_t) -1) / 3 + 1) == 0, "fs_calloc(3, SIZE_MAX/3 + 1) refused (wraps)");

    uint8_t *a = (uint8_t *) fs_malloc(3);
    uint8_t *b = (uint8_t *) fs_malloc(5);
    CHECK(a != 0 && b != 0, "small fs_malloc calls succeed");
    CHECK(((uintptr_t) a & 15u) == 0 && ((uintptr_t) b & 15u) == 0,
          "fs_malloc blocks are 16-byte aligned");
    CHECK(b >= a + 3, "blocks do not overlap");

    uint8_t *c = (uint8_t *) fs_calloc(10, 10);
    int zero = c != 0;
    for (int i = 0; c && i < 100; i++) zero = zero && c[i] == 0;
    CHECK(zero, "fs_calloc(10, 10) returns 100 zeroed bytes");

    /* Exhaust the arena: the last request that fits succeeds, the next fails. */
    size_t got = 0;
    while (fs_malloc(4096)) got += 4096;
    CHECK(got > 0 && got <= 1024 * 1024, "arena exhausts within its 1 MB");
    CHECK(fs_malloc(4096) == 0, "exhausted arena keeps refusing");
}

int main(void)
{
    test_kmalloc();
    test_fs_alloc();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
