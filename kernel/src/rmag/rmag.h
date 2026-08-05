/* rmag.h — Rational Magnitude Engine (K2)
 *
 * K2 RMAG allocates exact compute, memory, bandwidth, energy, solver, and
 * hardware budgets. It provides rational arithmetic for precise resource
 * accounting and quota enforcement across the 13-phase pipeline.
 *
 * Referenced by:
 *   - Tri-Space Programming Spec: K2_RMAG allocates resource budgets
 *   - RCE Spec: K2_RMAG allocates compute/memory/bandwidth/energy/solver budgets
 *   - Root Computing Spec: K2_RMAG allocates compiler/runtime/array/stack/heap/IO budgets
 *   - UBH Spec: K2_RMAG enforces byte/nesting/decompression/memory/CPU/runtime budgets
 *   - Master Roadmap: kernel resource accounting subsystem
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef RMAG_H
#define RMAG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== RMAG Constants ===== */

#define RMAG_MAX_QUOTA_SOURCES   32
#define RMAG_MAX_BUDGET_ENTRIES  128
#define RMAG_MAX_NAME_LEN        32

/* ===== Resource Types ===== */

typedef enum {
    RMAG_RES_COMPUTE_CYCLES  = 0,  /* CPU cycles */
    RMAG_RES_MEMORY          = 1,  /* bytes of memory */
    RMAG_RES_BANDWIDTH       = 2,  /* bytes of network I/O */
    RMAG_RES_ENERGY          = 3,  /* millijoules */
    RMAG_RES_STORAGE         = 4,  /* bytes of persistent storage */
    RMAG_RES_STACK_DEPTH     = 5,  /* stack frames */
    RMAG_RES_HEAP_ALLOC      = 6,  /* heap allocation count */
    RMAG_RES_IO_OPERATIONS   = 7,  /* I/O operation count */
    RMAG_RES_NESTING_DEPTH   = 8,  /* container/format nesting depth */
    RMAG_RES_DECOMPRESS_RATIO = 9,  /* decompression ratio (x100) */
    RMAG_RES_SOLVER_TIME     = 10,  /* solver wall-time milliseconds */
    RMAG_RES_FUZZ_ITERATIONS = 11,  /* fuzzing iteration count */
    RMAG_RES_EVENT_COUNT     = 12,  /* event count */
    RMAG_RES_COUNT           = 13
} rmag_resource_t;

/* ===== Rational Number ===== */
/* Stored as numerator/denominator with sign.
 * For kernel use: 64-bit numerator/denominator, always reduced. */

typedef struct rmag_rational {
    uint64_t numerator;
    uint64_t denominator;  /* never zero */
    bool negative;
} rmag_rational_t;

/* ===== Budget Entry ===== */

typedef struct rmag_budget_entry {
    char name[RMAG_MAX_NAME_LEN];
    rmag_resource_t resource;
    rmag_rational_t limit;       /* maximum allowed */
    rmag_rational_t consumed;    /* current consumption */
    bool enforce;                /* if true, deny when consumed > limit */
    bool active;
} rmag_budget_entry_t;

/* ===== Quota Source ===== */
/* A quota source is a subsystem or phase that provides resource budgets. */

typedef struct rmag_quota_source {
    char name[RMAG_MAX_NAME_LEN];
    uint32_t phase_id;           /* 13-phase ID (K1=1..O7=13) */
    rmag_rational_t total_budget[RMAG_RES_COUNT];
    rmag_rational_t allocated[RMAG_RES_COUNT];
    bool active;
} rmag_quota_source_t;

/* ===== RMAG Registry ===== */

typedef struct rmag_registry {
    rmag_quota_source_t sources[RMAG_MAX_QUOTA_SOURCES];
    uint32_t source_count;

    rmag_budget_entry_t budgets[RMAG_MAX_BUDGET_ENTRIES];
    uint32_t budget_count;
} rmag_registry_t;

/* ===== Rational Arithmetic ===== */

rmag_rational_t rmag_rational_from_uint(uint64_t value);
rmag_rational_t rmag_rational_from_frac(uint64_t num, uint64_t den, bool neg);
bool rmag_rational_is_zero(rmag_rational_t r);
bool rmag_rational_equal(rmag_rational_t a, rmag_rational_t b);
bool rmag_rational_less_than(rmag_rational_t a, rmag_rational_t b);
rmag_rational_t rmag_rational_add(rmag_rational_t a, rmag_rational_t b);
rmag_rational_t rmag_rational_subtract(rmag_rational_t a, rmag_rational_t b);
rmag_rational_t rmag_rational_multiply(rmag_rational_t a, rmag_rational_t b);
rmag_rational_t rmag_rational_divide(rmag_rational_t a, rmag_rational_t b);

/* Reduce a rational to lowest terms */
rmag_rational_t rmag_rational_reduce(rmag_rational_t r);

/* ===== Registry Operations ===== */

void rmag_registry_init(rmag_registry_t *reg);

int32_t rmag_register_source(rmag_registry_t *reg,
                             const char *name, uint32_t phase_id);

int32_t rmag_set_source_budget(rmag_registry_t *reg, uint32_t source_idx,
                               rmag_resource_t res, rmag_rational_t budget);

int32_t rmag_create_budget(rmag_registry_t *reg,
                           const char *name, rmag_resource_t res,
                           rmag_rational_t limit, bool enforce);

/* ===== Budget Operations ===== */

typedef enum {
    RMAG_RESULT_OK        = 0,
    RMAG_RESULT_DENIED    = 1,  /* would exceed enforce limit */
    RMAG_RESULT_NOT_FOUND = 2,
    RMAG_RESULT_INVALID   = 3,
} rmag_result_t;

rmag_result_t rmag_consume(rmag_registry_t *reg, uint32_t budget_idx,
                           rmag_rational_t amount);

rmag_result_t rmag_release(rmag_registry_t *reg, uint32_t budget_idx,
                           rmag_rational_t amount);

bool rmag_budget_exceeded(rmag_registry_t *reg, uint32_t budget_idx);

rmag_rational_t rmag_budget_remaining(rmag_registry_t *reg, uint32_t budget_idx);

/* ===== Source Allocation ===== */

rmag_result_t rmag_allocate_from_source(rmag_registry_t *reg,
                                        uint32_t source_idx,
                                        rmag_resource_t res,
                                        rmag_rational_t amount);

rmag_rational_t rmag_source_remaining(rmag_registry_t *reg,
                                      uint32_t source_idx,
                                      rmag_resource_t res);

/* ===== Name Functions ===== */

const char *rmag_resource_name(rmag_resource_t res);
const char *rmag_result_name(rmag_result_t result);

#endif /* RMAG_H */
