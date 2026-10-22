/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_mmu_window.c — mmu_window_map on host-side tables shaped like
 * arm64_mmu.c's: L1[0] a table (device and RAM 2 MB blocks), L1[1] a table
 * whose block 0 is refined to 4 KB pages (the kernel image), L1[2..4] 1 GB RAM
 * blocks. The case the server found: AAVMF's framebuffer at 0x138430000 with
 * 4 GB of RAM, inside the 1 GB block at L1[4]. */
#include <stdio.h>
#include <string.h>

#include "mmu_window.h"

static int checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

#define RAM     0x701ull              /* AF | SH inner | Normal | block */
#define DEV     0x605ull              /* AF | SH outer | Device | block */
#define WIN     0x0060000000000708ull /* UXN | PXN | AF | SH inner | Normal-NC */
#define GB(n)   ((uint64_t) (n) * MMU_W_GB)
#define E(a, f) (((uint64_t) (a)) | (f)) /* an entry: address | bits */

static uint64_t l1[512] __attribute__((aligned(4096)));
static uint64_t l2_low[512] __attribute__((aligned(4096)));
static uint64_t l2_ram[512] __attribute__((aligned(4096)));
static uint64_t l3_img[512] __attribute__((aligned(4096)));
static uint64_t spare[2][512] __attribute__((aligned(4096)));
static uint32_t used;

static void tables(void)
{
    memset(l1, 0, sizeof l1);
    memset(spare, 0xA5, sizeof spare);
    used = 0;
    for (uint64_t j = 0; j < 512; j++) {
        l2_low[j] = E(j * MMU_W_2MB, j >= 64 && j < 80 ? DEV : RAM);
        l2_ram[j] = E(GB(1) + j * MMU_W_2MB, RAM);
        l3_img[j] = E(GB(1) + j * 4096, 0x703);
    }
    l2_ram[0] = (uint64_t) (uintptr_t) l3_img | 3;
    l1[0] = (uint64_t) (uintptr_t) l2_low | 3;
    l1[1] = (uint64_t) (uintptr_t) l2_ram | 3;
    for (int g = 2; g <= 4; g++) l1[g] = E(GB(g), RAM);
}

static uint64_t *t_of(uint64_t e)
{
    return (uint64_t *) (uintptr_t) (e & 0x0000FFFFFFFFF000ull);
}

int main(void)
{
    uint64_t top = 1ull << 36; /* a 36-bit PA range, as QEMU virt -cpu max reports at least */

    /* the server's case: 800x600x4 at 0x138430000, inside the RAM block L1[4] */
    tables();
    uint64_t fb = 0x138430000ull, len = 800ull * 600 * 4;
    uint64_t before[512];
    memcpy(before, l1, sizeof before);
    CHECK(mmu_window_map(l1, spare, 2, &used, fb, len, top, WIN) == MMU_W_OK);
    CHECK(used == 1 && l1[4] == ((uint64_t) (uintptr_t) spare[0] | 3));
    uint64_t *t = t_of(l1[4]);
    uint64_t j0 = (fb - GB(4)) / MMU_W_2MB, j1 = (fb + len - 1 - GB(4)) / MMU_W_2MB;
    CHECK(j0 == 450 && j1 == 451); /* 1.83 MiB starting 0.13 MiB into block 450 */
    CHECK(t[450] == E(GB(4) + 450 * MMU_W_2MB, WIN | 1) &&
          t[451] == E(GB(4) + 451 * MMU_W_2MB, WIN | 1));
    /* the rest of that GB keeps the RAM block's attributes, at its own address */
    CHECK(t[0] == E(GB(4), RAM) && t[449] == E(GB(4) + 449 * MMU_W_2MB, RAM) &&
          t[452] == E(GB(4) + 452 * MMU_W_2MB, RAM) && t[511] == E(GB(4) + 511 * MMU_W_2MB, RAM));
    CHECK(memcmp(l1, before, 4 * sizeof l1[0]) == 0 && l1[5] == 0);
    /* mapping it again changes nothing and takes no table */
    CHECK(mmu_window_map(l1, spare, 2, &used, fb, len, top, WIN) == MMU_W_OK && used == 1);

    /* a window across a GB boundary into an empty slot: two tables, the
     * empty one gets only the window */
    tables();
    CHECK(mmu_window_map(l1, spare, 2, &used, GB(5) - MMU_W_2MB, 3 * MMU_W_2MB, top, WIN) ==
          MMU_W_OK);
    CHECK(used == 2);
    t = t_of(l1[4]);
    CHECK(t[511] == E(GB(5) - MMU_W_2MB, WIN | 1) && t[510] == E(GB(5) - 2 * MMU_W_2MB, RAM));
    t = t_of(l1[5]);
    CHECK(t[0] == E(GB(5), WIN | 1) && t[1] == E(GB(5) + MMU_W_2MB, WIN | 1) && t[2] == 0 &&
          t[511] == 0);

    /* a PCIe BAR in the first GB: the existing table is edited in place */
    tables();
    CHECK(mmu_window_map(l1, spare, 2, &used, 0x10000000ull, 0x400000ull, top, WIN) == MMU_W_OK);
    CHECK(used == 0 && l1[0] == ((uint64_t) (uintptr_t) l2_low | 3));
    CHECK(l2_low[128] == E(0x10000000ull, WIN | 1) && l2_low[129] == E(0x10200000ull, WIN | 1) &&
          l2_low[127] == E(127 * MMU_W_2MB, RAM) && l2_low[130] == E(130 * MMU_W_2MB, RAM));

    /* refused, and nothing written */
    tables();
    memcpy(before, l1, sizeof before);
    uint64_t ram0 = l2_ram[1];
    CHECK(mmu_window_map(l1, spare, 2, &used, GB(1) + 4096, 4 * MMU_W_2MB, top, WIN) ==
          MMU_W_PAGES);
    CHECK(l2_ram[1] == ram0 && memcmp(l1, before, sizeof before) == 0 && used == 0);
    CHECK(mmu_window_map(l1, spare, 2, &used, fb, 0, top, WIN) == MMU_W_EMPTY);
    CHECK(mmu_window_map(l1, spare, 2, &used, UINT64_MAX - 10, 100, top, WIN) == MMU_W_EMPTY);
    CHECK(mmu_window_map(l1, spare, 2, &used, top - MMU_W_2MB, MMU_W_2MB + 1, top, WIN) ==
          MMU_W_RANGE);
    CHECK(mmu_window_map(l1, spare, 2, &used, top - MMU_W_2MB, MMU_W_2MB, top, WIN) == MMU_W_OK);
    used = 0;
    CHECK(mmu_window_map(l1, spare, 2, &used, GB(600), MMU_W_2MB, UINT64_MAX, WIN) == MMU_W_RANGE);
    CHECK(mmu_window_map(l1, spare, 2, &used, GB(7) - MMU_W_2MB, GB(1) + 2 * MMU_W_2MB, top, WIN) ==
          MMU_W_NOSPARE);
    CHECK(used == 0 && l1[6] == 0 && l1[7] == 0 && l1[8] == 0);
    used = 2;
    CHECK(mmu_window_map(l1, spare, 2, &used, GB(9), MMU_W_2MB, top, WIN) == MMU_W_NOSPARE);
    used = 3;
    CHECK(mmu_window_map(l1, spare, 2, &used, GB(9), MMU_W_2MB, top, WIN) == MMU_W_NOSPARE);

    printf("test_mmu_window: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
