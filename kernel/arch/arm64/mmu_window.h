/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mmu_window.h — map a physical window (a firmware framebuffer) into the
 * arm64 identity tables, wherever the firmware put it.
 *
 * AAVMF puts the ramfb/GOP framebuffer near the top of RAM, which with 4 GB
 * of RAM is past 4 GB; a PCIe display's BAR sits in the first GB. Either way
 * the kernel must map exactly that range, as Normal Non-cacheable (the
 * scanout reads memory, not the cache) and never executable, without
 * disturbing anything else the tables map:
 *   - an L1 slot that is a 1 GB block (RAM) or empty is split into a spare L2
 *     table: the 2 MB blocks of the window take win_attr, the others keep
 *     what the slot had (the block's attributes, or nothing);
 *   - an L1 slot that is already a table has only the window's 2 MB entries
 *     rewritten; an entry that is itself a table (the kernel image's 4 KB
 *     pages) is refused, never overwritten.
 * Everything is checked before anything is written: on any refusal the
 * tables are as they were. The window is widened to whole 2 MB blocks.
 *
 * Pure table arithmetic on caller-owned arrays (no system registers), so the
 * host test (test_mmu_window.c) runs it; arm64_mmu.c supplies the tables and
 * does the barriers and TLB invalidation. Table pointers are identity: an
 * entry's address bits are the table's address. */
#ifndef ZXV_MMU_WINDOW_H
#define ZXV_MMU_WINDOW_H

#include <stdint.h>

#define MMU_W_ENTRIES 512u
#define MMU_W_GB      0x40000000ull
#define MMU_W_2MB     0x200000ull

enum {
    MMU_W_OK = 0,
    MMU_W_EMPTY = -1,   /* len 0, or base + len overflows */
    MMU_W_RANGE = -2,   /* ends past pa_top (the CPU's PA range or the 512 GB L1 reach) */
    MMU_W_NOSPARE = -3, /* more L1 slots to split than spare L2 tables */
    MMU_W_PAGES = -4,   /* overlaps a 2 MB entry already refined to 4 KB pages */
};

/* Map [base, base + len) into l1 (one L1 table: 512 slots of 1 GB). spare
 * holds nspare unused L2 tables; *used counts the ones taken (it is read,
 * then advanced). win_attr is the leaf's attribute bits (no address, no
 * valid/type bits). Returns MMU_W_OK or a refusal above. */
int mmu_window_map(uint64_t *l1, uint64_t (*spare)[MMU_W_ENTRIES], uint32_t nspare, uint32_t *used,
                   uint64_t base, uint64_t len, uint64_t pa_top, uint64_t win_attr);

#endif /* ZXV_MMU_WINDOW_H */
