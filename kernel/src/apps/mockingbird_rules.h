/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mockingbird_rules.h — Mockingbird's rules in C, the reference its Eve,
 * KRON, Trebago, Sutra and MYRA blocks are checked against
 * (blocks/mockingbird). Pure integer functions, no state: the model
 * (mockingbird_model.c) and the language test (tests/mockingbird_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs. Addresses and sizes are bytes.
 *
 *   MR1 BOOT PASSED. A boot runs its five stages in order (rom, loader,
 *       kernel, drivers, apps) and each gives a code: 0 passed, 1 ..
 *       MOCK_CODE_MAX a fault, MOCK_NOT_RUN a stage never reached because
 *       one before it failed. The boot passed when every stage ran and
 *       gave 0. mockingbird_first_failed names the first stage that did
 *       not, in order (C only: the blocks answer the truth).
 *   MR2 REGION OVERLAPS. A memory map is regions sorted by base, each
 *       base .. base + size - 1. The count of neighbours that overlap or
 *       are out of order: pairs i - 1, i with base[i - 1] + size[i - 1] >
 *       base[i]. 0 is a clean map. Every base and size 0 .. MOCK_ADDR_MAX
 *       and every region's end at most MOCK_ADDR_MAX (a 48-bit address
 *       space); anything else is refused.
 *   MR3 RUN CYCLES. The cycles one run takes: steps * cost + stall, steps
 *       0 .. MOCK_STEPS_MAX, cost (cycles a step) 0 .. MOCK_COST_MAX, stall
 *       0 .. MOCK_STALL_MAX (Sutra's pictures); anything else is refused,
 *       never wrapped.
 *   MR4 LIMITS. An emulated device holds 1 .. MOCK_RAM_MAX bytes of RAM
 *       (4 GiB), 0 .. MOCK_DEVICES_MAX devices and runs 0 ..
 *       MOCK_CYCLES_MAX cycles (10^9) before it stops, each held to its
 *       own range.
 *   MR5 DEVICE RECORD. A device's record fits ^ZXMOCK: device number at
 *       most 2 digits, kind 2, base 15, size 15, irq 3, sign aside. */
#ifndef MOCKINGBIRD_RULES_H
#define MOCKINGBIRD_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define MOCK_STAGES      5u
#define MOCK_CODE_MAX    INT64_C(255)
#define MOCK_NOT_RUN     INT64_C(-1)
#define MOCK_ADDR_MAX    INT64_C(281474976710656) /* 2^48 */
#define MOCK_STEPS_MAX   INT64_C(999999999)
#define MOCK_COST_MAX    INT64_C(9999)
#define MOCK_STALL_MAX   INT64_C(999999999)
#define MOCK_RAM_MAX     INT64_C(4294967296) /* 4 GiB */
#define MOCK_DEVICES_MAX INT64_C(64)
#define MOCK_CYCLES_MAX  INT64_C(1000000000)

static inline bool mockingbird_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* MR1 */
static inline bool mockingbird_boot_passed(const int64_t code[MOCK_STAGES])
{
    for (uint32_t i = 0; i < MOCK_STAGES; i++)
        if (code[i] != 0) return false;
    return true;
}

/* MR1, in order: the first stage that did not pass, or MOCK_STAGES */
static inline uint32_t mockingbird_first_failed(const int64_t code[MOCK_STAGES])
{
    for (uint32_t i = 0; i < MOCK_STAGES; i++)
        if (code[i] != 0) return i;
    return MOCK_STAGES;
}

/* MR2: false when refused; *out the count of overlapping neighbours */
static inline bool mockingbird_region_overlaps(const int64_t *base, const int64_t *size, uint32_t n,
                                               int64_t *out)
{
    int64_t c = 0;
    if (!base || !size || !out || n == 0) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!mockingbird_in(base[i], 0, MOCK_ADDR_MAX) ||
            !mockingbird_in(size[i], 0, MOCK_ADDR_MAX) || base[i] > MOCK_ADDR_MAX - size[i])
            return false;
    for (uint32_t i = 1; i < n; i++)
        if (base[i - 1] + size[i - 1] > base[i]) c++;
    *out = c;
    return true;
}

/* MR3: false when refused */
static inline bool mockingbird_run_cycles(int64_t steps, int64_t cost, int64_t stall, int64_t *out)
{
    if (!out || !mockingbird_in(steps, 0, MOCK_STEPS_MAX) ||
        !mockingbird_in(cost, 0, MOCK_COST_MAX) || !mockingbird_in(stall, 0, MOCK_STALL_MAX))
        return false;
    *out = steps * cost + stall;
    return true;
}

/* MR4 */
static inline bool mockingbird_limits_ok(int64_t ram, int64_t devices, int64_t cycles)
{
    return mockingbird_in(ram, 1, MOCK_RAM_MAX) && mockingbird_in(devices, 0, MOCK_DEVICES_MAX) &&
           mockingbird_in(cycles, 0, MOCK_CYCLES_MAX);
}

/* MR5: |v| fits the digits */
static inline bool mockingbird_digits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

static inline bool mockingbird_device_record_ok(int64_t device, int64_t kind, int64_t base,
                                                int64_t size, int64_t irq)
{
    return mockingbird_digits(device, 99) && mockingbird_digits(kind, 99) &&
           mockingbird_digits(base, INT64_C(999999999999999)) &&
           mockingbird_digits(size, INT64_C(999999999999999)) && mockingbird_digits(irq, 999);
}

#endif
