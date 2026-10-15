/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_x86_irq.c: host tests for the pure parts of x86_irq.h, the LAPIC
 * timer calibration arithmetic and the MADT walk. */
#include "x86_irq.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static void test_calibration(void)
{
    /* QEMU: 1 GHz bus / 16 = 62.5 MHz; 625000 decrements in 10 ms. */
    CHECK(x86_lapic_period_count(625000, 10000, 10000) == 625000);
    CHECK(x86_lapic_period_count(625000, 10000, 1000) == 62500); /* 1 kHz */
    CHECK(x86_rate_hz(625000, 10000) == 62500000ull);
    /* no calibration: 0, never a guess */
    CHECK(x86_lapic_period_count(0, 10000, 10000) == 0);
    CHECK(x86_lapic_period_count(625000, 0, 10000) == 0);
    CHECK(x86_lapic_period_count(625000, 10000, 0) == 0);
    CHECK(x86_rate_hz(1, 0) == 0);
    /* a very slow timer still ticks: at least 1 */
    CHECK(x86_lapic_period_count(1, 10000, 100) == 1);
    /* a full 32-bit count over a short window clamps instead of wrapping */
    CHECK(x86_lapic_period_count(0xFFFFFFFFull, 10000, 1000000) == 0xFFFFFFFFu);
    /* largest inputs this code sees do not overflow 64 bits */
    CHECK(x86_lapic_period_count(0x100000000ull, 1000000, 1000000) == 0xFFFFFFFFu);
    CHECK(x86_lapic_period_count(4000000000ull, 1000000, 10000) == 40000000u);
}

/* One MADT entry at t + *o: type, length, then two 32-bit fields at w1_at
 * and w1_at + 4 (0 = none). Returns the entry. */
static uint8_t *ent(uint8_t *t, uint32_t *o, uint8_t type, uint8_t len, uint32_t w1_at, uint32_t w1,
                    uint32_t w2)
{
    uint8_t *e = t + *o;
    e[0] = type;
    e[1] = len;
    if (w1_at) {
        put32(e + w1_at, w1);
        if (w1_at + 8u <= len) put32(e + w1_at + 4, w2);
    }
    *o += len;
    return e;
}

/* A MADT: header (36) + LAPIC address (4) + flags (4), then entries. */
static uint32_t madt_build(uint8_t *t, int with_override, int truncate)
{
    memset(t, 0, 256);
    memcpy(t, "APIC", 4);
    put32(t + 36, 0xFEE00000u);
    put32(t + 40, 1);
    uint32_t o = 44;
    ent(t, &o, 0, 8, 4, 1, 0);             /* processor LAPIC, enabled */
    ent(t, &o, 0, 8, 4, 0, 0)[3] = 1;      /* disabled, not online-capable */
    ent(t, &o, 9, 16, 4, 300, 2);          /* x2APIC 300, online-capable */
    ent(t, &o, 1, 12, 4, 0xFEC00000u, 0);  /* IOAPIC, GSI 0 */
    ent(t, &o, 1, 12, 4, 0xFEC10000u, 24); /* a second IOAPIC: the first is kept */
    if (with_override) ent(t, &o, 5, 12, 4, 0xFEE00000u, 0x1); /* 64-bit LAPIC base */
    if (truncate) {
        ent(t, &o, 0, 40, 0, 0, 0); /* claims 40 bytes ... */
        o -= 36;                    /* ... the table ends 4 bytes in */
    }
    put32(t + 4, o);
    return o;
}

static void test_madt(void)
{
    uint8_t t[256];
    x86_madt_t m;
    uint32_t len = madt_build(t, 0, 0);
    CHECK(x86_madt_scan(t, len, &m) == 0);
    CHECK(m.lapic_base == 0xFEE00000u);
    CHECK(m.ioapic_base == 0xFEC00000u);
    CHECK(m.ioapic_gsi == 0);
    CHECK(m.cpus == 2);
    CHECK(m.bad == 0);

    len = madt_build(t, 1, 0);
    CHECK(x86_madt_scan(t, len, &m) == 0);
    CHECK(m.lapic_base == 0x1FEE00000ull); /* the 64-bit override wins */

    len = madt_build(t, 0, 1);
    CHECK(x86_madt_scan(t, len, &m) == 0);
    CHECK(m.bad == 1);  /* damaged entry list is flagged */
    CHECK(m.cpus == 2); /* entries before it are kept */

    /* zero-length entry: flagged, no endless loop */
    len = madt_build(t, 0, 0);
    t[44 + 1] = 0;
    CHECK(x86_madt_scan(t, len, &m) == 0 && m.bad == 1);

    /* wrong signature, wrong length, too short */
    len = madt_build(t, 0, 0);
    t[0] = 'X';
    CHECK(x86_madt_scan(t, len, &m) == -1);
    len = madt_build(t, 0, 0);
    CHECK(x86_madt_scan(t, len - 1, &m) == -1);
    CHECK(x86_madt_scan(t, 40, &m) == -1);
}

int main(void)
{
    test_calibration();
    test_madt();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
