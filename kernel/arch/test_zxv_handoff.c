/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_handoff.c — host tests for boot handoff v2 (zxv_handoff.h). */
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

    printf("%s zxv_handoff\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
