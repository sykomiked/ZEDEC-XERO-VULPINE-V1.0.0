/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_handoff.c — host tests for boot handoff v2 (zxv_handoff.h). */
#include "zxv_bootmem.h"
#include "zxv_handoff.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    static zxh_t h;
    memset(&h, 0, sizeof h);

    /* absent / sealed / tampered */
    CHECK(zxh_accept(NULL) == 0);
    CHECK(zxh_accept(&h) == 0);
    h.rsdp = 0x1F77E014u;
    h.window = 1;
    zxh_seal(&h);
    CHECK(zxh_accept(&h) == 1);
    h.rsdp ^= 1;
    CHECK(zxh_accept(&h) == 2);
    h.rsdp ^= 1;
    CHECK(zxh_accept(&h) == 1);
    h.version = 9;
    CHECK(zxh_accept(&h) == 2);
    h.version = ZXH_VERSION;
    h.window = 4;
    h.sum = zxh_digest(&h);
    CHECK(zxh_accept(&h) == 2); /* verdict out of range even with a good sum */
    h.window = 1;
    h.n_mem = ZXH_MAX_MEM + 1;
    h.sum = zxh_digest(&h);
    CHECK(zxh_accept(&h) == 2);

    /* merge adjacent same-kind regions, split on gaps and kind changes */
    memset(&h, 0, sizeof h);
    CHECK(zxh_add_mem(&h, 0x0, 0, ZXH_MEM_USABLE) == 0 && h.n_mem == 0);
    CHECK(zxh_add_mem(&h, 0x0, 16, ZXH_MEM_USABLE) == 0);
    CHECK(zxh_add_mem(&h, 0x10000, 16, ZXH_MEM_USABLE) == 0 && h.n_mem == 1);
    CHECK(h.mem[0].pages == 32);
    CHECK(zxh_add_mem(&h, 0x20000, 1, ZXH_MEM_ACPI_NVS) == 0 && h.n_mem == 2);
    CHECK(zxh_add_mem(&h, 0x30000, 1, ZXH_MEM_ACPI_NVS) == 0 && h.n_mem == 3); /* gap */
    CHECK(zxh_pages_of(&h, ZXH_MEM_USABLE) == 32 && zxh_pages_of(&h, ZXH_MEM_ACPI_NVS) == 2);

    /* table full -> -1, nothing written past the end */
    memset(&h, 0, sizeof h);
    int rc = 0;
    for (uint32_t i = 0; i < ZXH_MAX_MEM + 5; i++)
        rc |= zxh_add_mem(&h, (uint64_t) i * 0x2000u, 1, 1u + (i & 1u));
    CHECK(rc == -1 && h.n_mem == ZXH_MAX_MEM);

    /* EFI types */
    CHECK(zxh_kind_of_efi(7) == ZXH_MEM_USABLE && zxh_kind_of_efi(3) == ZXH_MEM_USABLE);
    CHECK(zxh_kind_of_efi(2) == ZXH_MEM_LOADER && zxh_kind_of_efi(9) == ZXH_MEM_ACPI_RECLAIM);
    CHECK(zxh_kind_of_efi(10) == ZXH_MEM_ACPI_NVS && zxh_kind_of_efi(5) == ZXH_MEM_RUNTIME);
    CHECK(zxh_kind_of_efi(11) == ZXH_MEM_RESERVED && zxh_kind_of_efi(99) == ZXH_MEM_RESERVED);

    /* clock */
    CHECK(zxh_efi_time_to_unix(1970, 1, 1, 0, 0, 0, 2047) == 0);
    CHECK(zxh_efi_time_to_unix(2000, 3, 1, 0, 0, 0, 2047) == 951868800);
    CHECK(zxh_efi_time_to_unix(2024, 2, 29, 12, 0, 0, 2047) == 1709208000);
    CHECK(zxh_efi_time_to_unix(2024, 2, 29, 12, 0, 0, -60) == 1709208000 - 3600);
    CHECK(zxh_efi_time_to_unix(1969, 12, 31, 0, 0, 0, 0) == 0);
    CHECK(zxh_efi_time_to_unix(2024, 13, 1, 0, 0, 0, 0) == 0);
    CHECK(zxh_efi_time_to_unix(2024, 1, 1, 24, 0, 0, 0) == 0);

    /* bootmem: usable only, clipped to [floor, ceil), pages only */
    {
        static zxh_t m;
        static zxb_t b;
        memset(&m, 0, sizeof m);
        memset(&b, 0, sizeof b);
        zxh_add_mem(&m, 0x0, 0x9F, ZXH_MEM_USABLE);          /* below floor */
        zxh_add_mem(&m, 0x100000, 0x700, ZXH_MEM_USABLE);    /* 1..8 MB, floor at 6 MB */
        zxh_add_mem(&m, 0x800000, 0x8, ZXH_MEM_ACPI_NVS);    /* never */
        zxh_add_mem(&m, 0x900000, 0x3F700, ZXH_MEM_USABLE);  /* 9 MB .. 1 GB+ */
        zxh_add_mem(&m, 0x40000000, 0x100, ZXH_MEM_RUNTIME); /* never */
        CHECK(zxb_from_handoff(&b, &m, 0x600000, 0x40000000) == 2);
        CHECK(b.r[0].lo == 0x600000 && b.r[0].hi == 0x800000);
        CHECK(b.r[1].lo == 0x900000 && b.r[1].hi == 0x40000000);
        uint64_t a = zxb_take(&b, 3 * 1024 * 1024); /* too big for 6-8 MB */
        CHECK(a == 0x900000);
        CHECK(zxb_take(&b, 1) == 0x600000); /* rounds to one page, first fit */
        CHECK(zxb_take(&b, 0) == 0);
        CHECK(zxb_take(&b, 0x80000000ULL) == 0); /* nothing that large */
        zxb_exclude(&b, 0x700000, 0x701000);     /* split range 0 */
        CHECK(b.n == 3 && b.r[0].hi == 0x700000 && b.r[2].lo == 0x701000);
        uint64_t rem = zxb_remaining(&b);
        CHECK(rem == (0x700000 - 0x601000) + (0x800000 - 0x701000) + (0x40000000 - 0xC00000));
        static zxb_t e; /* no evidence: nothing handed out */
        memset(&e, 0, sizeof e);
        CHECK(zxb_take(&e, 4096) == 0);
    }
    /* bootmem from a Multiboot1 map: type 1 only, malformed size stops */
    {
        static uint8_t mm[3 * 24];
        static zxb_t b;
        memset(mm, 0, sizeof mm);
        memset(&b, 0, sizeof b);
        uint64_t ents[3][3] = {
            {0x0, 0x9FC00, 1}, {0x100000, 0x1FF00000, 1}, {0xFFFC0000, 0x40000, 2}};
        for (int i = 0; i < 3; i++) {
            uint8_t *e = mm + i * 24;
            e[0] = 20;
            for (int k = 0; k < 8; k++) {
                e[4 + k] = (uint8_t) (ents[i][0] >> (8 * k));
                e[12 + k] = (uint8_t) (ents[i][1] >> (8 * k));
            }
            e[20] = (uint8_t) ents[i][2];
        }
        CHECK(zxb_from_multiboot1(&b, mm, sizeof mm, 0x600000, 0x40000000) == 1);
        CHECK(b.r[0].lo == 0x600000 && b.r[0].hi == 0x20000000);
        mm[24] = 3; /* malformed size on entry 2: stop there */
        memset(&b, 0, sizeof b);
        CHECK(zxb_from_multiboot1(&b, mm, sizeof mm, 0x600000, 0x40000000) == 1);
    }

    /* window verdict from an EFI map (40-byte descriptors) */
    {
        static uint8_t map[4 * 40];
        memset(map, 0, sizeof map);
        uint64_t d[4][3] = {
            {7, 0x100000, 0x700}, {10, 0x800000, 8}, {2, 0x808000, 8}, {5, 0x900000, 4}};
        for (int i = 0; i < 4; i++) {
            memcpy(map + i * 40, &d[i][0], 4);
            memcpy(map + i * 40 + 8, &d[i][1], 8);
            memcpy(map + i * 40 + 24, &d[i][2], 8);
        }
        uint8_t fl = 0;
        uint32_t wt = 0;
        uint64_t wb = 0;
        CHECK(zxh_window_of_map(map, sizeof map, 40, 0x100000, 0x600000, 0, &fl, &wt, &wb) == 1);
        CHECK(fl == 0 && wt == 0);
        CHECK(zxh_window_of_map(map, sizeof map, 40, 0x100000, 0x804000, 0, &fl, &wt, &wb) == 3);
        CHECK(fl == ZXH_F_ACPI_NVS_LOST && wt == 10 && wb == 0x800000);
        fl = 0;
        CHECK(zxh_window_of_map(map, sizeof map, 40, 0x808000, 0x810000, 0, &fl, &wt, &wb) == 2);
        CHECK(zxh_window_of_map(map, sizeof map, 40, 0x808000, 0x810000, 1, &fl, &wt, &wb) == 1);
        CHECK(zxh_window_of_map(map, sizeof map, 40, 0x100000, 0x901000, 1, &fl, &wt, &wb) == 2);
        CHECK(wt == 5 && wb == 0x900000); /* runtime beats the earlier ACPI hit */
        CHECK(zxh_window_of_map(map, sizeof map, 16, 0, 1, 1, &fl, &wt, &wb) == 2); /* bad dsz */
    }

    printf("%s zxv_handoff\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
