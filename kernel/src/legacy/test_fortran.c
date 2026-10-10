/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_fortran.c — Fortran record markers and IBM HFP <-> IEEE 754. The HFP
 * hex patterns are the textbook System/360 encodings. Host test (stdio). */
#include <stdio.h>
#include <string.h>
#include "fortran.h"

static int pass = 0, fail = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)
#define OK(c) CHECK(c, #c)

static void test_records(void)
{
    /* two records, 4-byte little-endian markers */
    uint8_t buf[64];
    uint32_t p = 0;
    p += fortran_rec_write((const uint8_t *) "ABC", 3, 4, false, buf + p, sizeof buf - p);
    p += fortran_rec_write((const uint8_t *) "WXYZ", 4, 4, false, buf + p, sizeof buf - p);
    OK(p == (4 + 3 + 4) + (4 + 4 + 4));

    fortran_rec_iter it;
    fortran_rec_init(&it, buf, p, 4, false);
    uint32_t n;
    const uint8_t *r = fortran_rec_next(&it, &n);
    OK(r && n == 3 && memcmp(r, "ABC", 3) == 0);
    r = fortran_rec_next(&it, &n);
    OK(r && n == 4 && memcmp(r, "WXYZ", 4) == 0);
    OK(fortran_rec_next(&it, &n) == 0);

    /* 8-byte big-endian markers */
    uint32_t q = fortran_rec_write((const uint8_t *) "HELLO", 5, 8, true, buf, sizeof buf);
    OK(q == 8 + 5 + 8);
    fortran_rec_init(&it, buf, q, 8, true);
    r = fortran_rec_next(&it, &n);
    OK(r && n == 5 && memcmp(r, "HELLO", 5) == 0);

    /* corrupted trailing marker -> rejected */
    buf[8 + 5 + 7] ^= 0xFF;
    fortran_rec_init(&it, buf, q, 8, true);
    OK(fortran_rec_next(&it, &n) == 0);
}

static void test_hfp64(void)
{
    /* IBM HFP double vectors -> IEEE double */
    struct {
        uint64_t hfp, ieee;
    } v[] = {
        {0x4110000000000000ULL, 0x3FF0000000000000ULL}, /* 1.0 */
        {0xC110000000000000ULL, 0xBFF0000000000000ULL}, /* -1.0 */
        {0x4080000000000000ULL, 0x3FE0000000000000ULL}, /* 0.5 */
        {0x4120000000000000ULL, 0x4000000000000000ULL}, /* 2.0 */
        {0x4138000000000000ULL, 0x400C000000000000ULL}, /* 3.5 */
        {0x0000000000000000ULL, 0x0000000000000000ULL}, /* +0 */
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint64_t got = hfp64_to_ieee64(v[i].hfp);
        CHECK(got == v[i].ieee, "hfp64->ieee64");
        if (got != v[i].ieee)
            printf("    hfp %016llx -> %016llx want %016llx\n", (unsigned long long) v[i].hfp,
                   (unsigned long long) got, (unsigned long long) v[i].ieee);
        uint64_t back = ieee64_to_hfp64(v[i].ieee);
        CHECK(back == v[i].hfp, "ieee64->hfp64");
        if (back != v[i].hfp)
            printf("    ieee %016llx -> %016llx want %016llx\n", (unsigned long long) v[i].ieee,
                   (unsigned long long) back, (unsigned long long) v[i].hfp);
    }
}

static void test_hfp32(void)
{
    struct {
        uint32_t hfp, ieee;
    } v[] = {
        {0x41100000u, 0x3F800000u}, /* 1.0 */
        {0xC1100000u, 0xBF800000u}, /* -1.0 */
        {0x40800000u, 0x3F000000u}, /* 0.5 */
        {0x41200000u, 0x40000000u}, /* 2.0 */
        {0x41380000u, 0x40600000u}, /* 3.5 */
        {0x00000000u, 0x00000000u}, /* +0 */
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint32_t got = hfp32_to_ieee32(v[i].hfp);
        CHECK(got == v[i].ieee, "hfp32->ieee32");
        if (got != v[i].ieee) printf("    hfp %08x -> %08x want %08x\n", v[i].hfp, got, v[i].ieee);
        uint32_t back = ieee32_to_hfp32(v[i].ieee);
        CHECK(back == v[i].hfp, "ieee32->hfp32");
        if (back != v[i].hfp)
            printf("    ieee %08x -> %08x want %08x\n", v[i].ieee, back, v[i].hfp);
    }
}

static void test_roundtrip(void)
{
    /* round-trip a range of IEEE doubles that are exactly representable */
    uint64_t seeds[] = {
        0x3FF0000000000000ULL, 0x4059000000000000ULL, /* 100.0 */
        0x3F50624DD2F1A9FCULL,                        /* ~0.001 (approx) */
        0x40C3880000000000ULL,                        /* 10000.0 */
        0xC059000000000000ULL,                        /* -100.0 */
    };
    for (unsigned i = 0; i < sizeof seeds / sizeof seeds[0]; i++) {
        uint64_t hfp = ieee64_to_hfp64(seeds[i]);
        uint64_t back = hfp64_to_ieee64(hfp);
        /* allow the value to match exactly for powers/exact; others may lose a
         * couple of low bits to base-16 wobble, so compare the high 48 bits. */
        CHECK((back >> 16) == (seeds[i] >> 16), "ieee64 round-trip (hi 48 bits)");
    }
}

int main(void)
{
    test_records();
    test_hfp64();
    test_hfp32();
    test_roundtrip();
    printf("Fortran/HFP: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
