/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mmu_window.c — see mmu_window.h. */
#include "mmu_window.h"

#define TYPE_MASK  3ull
#define TYPE_BLOCK 1ull /* L1/L2 block */
#define TYPE_TABLE 3ull /* L1/L2 table */
#define ADDR_TABLE 0x0000FFFFFFFFF000ull
#define ADDR_L1    0x0000FFFFC0000000ull

static uint64_t *table_at(uint64_t e)
{
    return (uint64_t *) (uintptr_t) (e & ADDR_TABLE);
}

int mmu_window_map(uint64_t *l1, uint64_t (*spare)[MMU_W_ENTRIES], uint32_t nspare, uint32_t *used,
                   uint64_t base, uint64_t len, uint64_t pa_top, uint64_t win_attr)
{
    if (len == 0 || base + len < base) return MMU_W_EMPTY;
    uint64_t lo = base & ~(MMU_W_2MB - 1u);
    uint64_t hi = base + len;
    if (hi > UINT64_MAX - MMU_W_2MB) return MMU_W_RANGE;
    hi = (hi + MMU_W_2MB - 1u) & ~(MMU_W_2MB - 1u);
    if (pa_top > MMU_W_ENTRIES * MMU_W_GB) pa_top = MMU_W_ENTRIES * MMU_W_GB;
    if (hi > pa_top) return MMU_W_RANGE;

    /* check: how many slots split, and no 4 KB-page entry in the way */
    uint32_t need = 0;
    for (uint64_t g = lo / MMU_W_GB; g * MMU_W_GB < hi; g++) {
        uint64_t e = l1[g];
        if ((e & TYPE_MASK) != TYPE_TABLE) {
            need++;
            continue;
        }
        const uint64_t *t = table_at(e);
        for (uint64_t a = lo > g * MMU_W_GB ? lo : g * MMU_W_GB; a < hi && a < (g + 1) * MMU_W_GB;
             a += MMU_W_2MB)
            if ((t[(a / MMU_W_2MB) % MMU_W_ENTRIES] & TYPE_MASK) == TYPE_TABLE) return MMU_W_PAGES;
    }
    if (*used > nspare || need > nspare - *used) return MMU_W_NOSPARE;

    /* mutate: split the slots that need it, then write the window */
    for (uint64_t g = lo / MMU_W_GB; g * MMU_W_GB < hi; g++) {
        uint64_t e = l1[g];
        uint64_t *t;
        if ((e & TYPE_MASK) == TYPE_TABLE) {
            t = table_at(e);
        } else {
            t = spare[(*used)++];
            for (uint32_t j = 0; j < MMU_W_ENTRIES; j++)
                t[j] = (e & TYPE_MASK) == TYPE_BLOCK
                           ? (e & ~ADDR_L1) | (g * MMU_W_GB + j * MMU_W_2MB)
                           : 0;
        }
        for (uint64_t a = lo > g * MMU_W_GB ? lo : g * MMU_W_GB; a < hi && a < (g + 1) * MMU_W_GB;
             a += MMU_W_2MB)
            t[(a / MMU_W_2MB) % MMU_W_ENTRIES] = a | win_attr | TYPE_BLOCK;
        if ((e & TYPE_MASK) != TYPE_TABLE) l1[g] = (uint64_t) (uintptr_t) t | TYPE_TABLE;
    }
    return MMU_W_OK;
}
