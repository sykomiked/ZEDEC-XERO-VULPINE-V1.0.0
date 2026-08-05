/* rmag.c — Rational Magnitude Engine (K2)
 *
 * Implements rational arithmetic and resource budget management.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "rmag.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Greatest Common Divisor (Euclidean) ===== */

static uint64_t gcd(uint64_t a, uint64_t b) {
    while (b != 0) {
        uint64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/* ===== Rational Arithmetic ===== */

rmag_rational_t rmag_rational_from_uint(uint64_t value) {
    rmag_rational_t r;
    r.numerator = value;
    r.denominator = 1;
    r.negative = false;
    return r;
}

rmag_rational_t rmag_rational_from_frac(uint64_t num, uint64_t den, bool neg) {
    rmag_rational_t r;
    if (den == 0) den = 1;
    r.numerator = num;
    r.denominator = den;
    r.negative = neg && num != 0;
    return r;
}

bool rmag_rational_is_zero(rmag_rational_t r) {
    return r.numerator == 0;
}

bool rmag_rational_equal(rmag_rational_t a, rmag_rational_t b) {
    /* Compare cross-multiplied values with sign */
    if (a.numerator == 0 && b.numerator == 0) return true;
    if (a.negative != b.negative) return false;
    /* a.num/a.den == b.num/b.den  =>  a.num*b.den == b.num*a.den */
    return a.numerator * b.denominator == b.numerator * a.denominator;
}

bool rmag_rational_less_than(rmag_rational_t a, rmag_rational_t b) {
    /* Handle signs */
    if (a.negative && !b.negative) return true;
    if (!a.negative && b.negative) return false;
    if (a.negative && b.negative) {
        /* -a < -b  =>  a > b  =>  a.num*b.den > b.num*a.den */
        return a.numerator * b.denominator > b.numerator * a.denominator;
    }
    /* Both positive: a < b  =>  a.num*b.den < b.num*a.den */
    return a.numerator * b.denominator < b.numerator * a.denominator;
}

rmag_rational_t rmag_rational_add(rmag_rational_t a, rmag_rational_t b) {
    rmag_rational_t result;
    /* Common denominator: a.num*b.den + b.num*a.den over a.den*b.den */
    uint64_t common_den = a.denominator * b.denominator;
    uint64_t a_term = a.numerator * b.denominator;
    uint64_t b_term = b.numerator * a.denominator;

    if (a.negative == b.negative) {
        /* Same sign: add magnitudes */
        result.numerator = a_term + b_term;
        result.negative = a.negative;
    } else {
        /* Different signs: subtract smaller from larger */
        if (a_term >= b_term) {
            result.numerator = a_term - b_term;
            result.negative = a.negative;
        } else {
            result.numerator = b_term - a_term;
            result.negative = b.negative;
        }
    }
    result.denominator = common_den;
    if (result.numerator == 0) result.negative = false;
    return rmag_rational_reduce(result);
}

rmag_rational_t rmag_rational_subtract(rmag_rational_t a, rmag_rational_t b) {
    b.negative = !b.negative;
    return rmag_rational_add(a, b);
}

rmag_rational_t rmag_rational_multiply(rmag_rational_t a, rmag_rational_t b) {
    rmag_rational_t result;
    result.numerator = a.numerator * b.numerator;
    result.denominator = a.denominator * b.denominator;
    result.negative = a.negative != b.negative;
    if (result.numerator == 0) result.negative = false;
    return rmag_rational_reduce(result);
}

rmag_rational_t rmag_rational_divide(rmag_rational_t a, rmag_rational_t b) {
    if (b.numerator == 0) return rmag_rational_from_uint(0);
    rmag_rational_t result;
    result.numerator = a.numerator * b.denominator;
    result.denominator = a.denominator * b.numerator;
    result.negative = a.negative != b.negative;
    if (result.numerator == 0) result.negative = false;
    return rmag_rational_reduce(result);
}

rmag_rational_t rmag_rational_reduce(rmag_rational_t r) {
    if (r.denominator == 0) {
        r.denominator = 1;
        return r;
    }
    if (r.numerator == 0) {
        r.denominator = 1;
        r.negative = false;
        return r;
    }
    uint64_t g = gcd(r.numerator, r.denominator);
    if (g > 1) {
        r.numerator /= g;
        r.denominator /= g;
    }
    return r;
}

/* ===== Registry Operations ===== */

void rmag_registry_init(rmag_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
}

int32_t rmag_register_source(rmag_registry_t *reg,
                             const char *name, uint32_t phase_id) {
    if (!reg || !name) return -1;
    if (reg->source_count >= RMAG_MAX_QUOTA_SOURCES) return -1;
    rmag_quota_source_t *s = &reg->sources[reg->source_count];
    ev_memset(s, 0, sizeof(*s));
    copy_str(s->name, name, RMAG_MAX_NAME_LEN);
    s->phase_id = phase_id;
    s->active = true;
    /* Initialize all budgets to zero */
    for (uint32_t i = 0; i < RMAG_RES_COUNT; i++) {
        s->total_budget[i] = rmag_rational_from_uint(0);
        s->allocated[i] = rmag_rational_from_uint(0);
    }
    return (int32_t)reg->source_count++;
}

int32_t rmag_set_source_budget(rmag_registry_t *reg, uint32_t source_idx,
                               rmag_resource_t res, rmag_rational_t budget) {
    if (!reg || source_idx >= reg->source_count) return -1;
    if (res >= RMAG_RES_COUNT) return -1;
    reg->sources[source_idx].total_budget[res] = rmag_rational_reduce(budget);
    return 0;
}

int32_t rmag_create_budget(rmag_registry_t *reg,
                           const char *name, rmag_resource_t res,
                           rmag_rational_t limit, bool enforce) {
    if (!reg || !name) return -1;
    if (reg->budget_count >= RMAG_MAX_BUDGET_ENTRIES) return -1;
    if (res >= RMAG_RES_COUNT) return -1;
    rmag_budget_entry_t *b = &reg->budgets[reg->budget_count];
    ev_memset(b, 0, sizeof(*b));
    copy_str(b->name, name, RMAG_MAX_NAME_LEN);
    b->resource = res;
    b->limit = rmag_rational_reduce(limit);
    b->consumed = rmag_rational_from_uint(0);
    b->enforce = enforce;
    b->active = true;
    return (int32_t)reg->budget_count++;
}

/* ===== Budget Operations ===== */

rmag_result_t rmag_consume(rmag_registry_t *reg, uint32_t budget_idx,
                           rmag_rational_t amount) {
    if (!reg || budget_idx >= reg->budget_count) return RMAG_RESULT_NOT_FOUND;
    rmag_budget_entry_t *b = &reg->budgets[budget_idx];
    if (!b->active) return RMAG_RESULT_INVALID;

    rmag_rational_t new_consumed = rmag_rational_add(b->consumed, amount);

    if (b->enforce && rmag_rational_less_than(b->limit, new_consumed)) {
        return RMAG_RESULT_DENIED;
    }

    b->consumed = new_consumed;
    return RMAG_RESULT_OK;
}

rmag_result_t rmag_release(rmag_registry_t *reg, uint32_t budget_idx,
                           rmag_rational_t amount) {
    if (!reg || budget_idx >= reg->budget_count) return RMAG_RESULT_NOT_FOUND;
    rmag_budget_entry_t *b = &reg->budgets[budget_idx];
    if (!b->active) return RMAG_RESULT_INVALID;

    /* Don't release more than consumed */
    if (rmag_rational_less_than(b->consumed, amount)) {
        b->consumed = rmag_rational_from_uint(0);
    } else {
        b->consumed = rmag_rational_subtract(b->consumed, amount);
    }
    return RMAG_RESULT_OK;
}

bool rmag_budget_exceeded(rmag_registry_t *reg, uint32_t budget_idx) {
    if (!reg || budget_idx >= reg->budget_count) return false;
    rmag_budget_entry_t *b = &reg->budgets[budget_idx];
    return rmag_rational_less_than(b->limit, b->consumed);
}

rmag_rational_t rmag_budget_remaining(rmag_registry_t *reg, uint32_t budget_idx) {
    if (!reg || budget_idx >= reg->budget_count) return rmag_rational_from_uint(0);
    rmag_budget_entry_t *b = &reg->budgets[budget_idx];
    if (rmag_rational_less_than(b->limit, b->consumed)) {
        return rmag_rational_from_uint(0);
    }
    return rmag_rational_subtract(b->limit, b->consumed);
}

/* ===== Source Allocation ===== */

rmag_result_t rmag_allocate_from_source(rmag_registry_t *reg,
                                        uint32_t source_idx,
                                        rmag_resource_t res,
                                        rmag_rational_t amount) {
    if (!reg || source_idx >= reg->source_count) return RMAG_RESULT_NOT_FOUND;
    if (res >= RMAG_RES_COUNT) return RMAG_RESULT_INVALID;
    rmag_quota_source_t *s = &reg->sources[source_idx];
    if (!s->active) return RMAG_RESULT_INVALID;

    rmag_rational_t new_allocated = rmag_rational_add(s->allocated[res], amount);

    if (rmag_rational_less_than(s->total_budget[res], new_allocated)) {
        return RMAG_RESULT_DENIED;
    }

    s->allocated[res] = new_allocated;
    return RMAG_RESULT_OK;
}

rmag_rational_t rmag_source_remaining(rmag_registry_t *reg,
                                      uint32_t source_idx,
                                      rmag_resource_t res) {
    if (!reg || source_idx >= reg->source_count) return rmag_rational_from_uint(0);
    if (res >= RMAG_RES_COUNT) return rmag_rational_from_uint(0);
    rmag_quota_source_t *s = &reg->sources[source_idx];
    if (rmag_rational_less_than(s->total_budget[res], s->allocated[res])) {
        return rmag_rational_from_uint(0);
    }
    return rmag_rational_subtract(s->total_budget[res], s->allocated[res]);
}

/* ===== Name Functions ===== */

const char *rmag_resource_name(rmag_resource_t res) {
    switch (res) {
        case RMAG_RES_COMPUTE_CYCLES:   return "compute_cycles";
        case RMAG_RES_MEMORY:           return "memory";
        case RMAG_RES_BANDWIDTH:        return "bandwidth";
        case RMAG_RES_ENERGY:           return "energy";
        case RMAG_RES_STORAGE:          return "storage";
        case RMAG_RES_STACK_DEPTH:      return "stack_depth";
        case RMAG_RES_HEAP_ALLOC:       return "heap_alloc";
        case RMAG_RES_IO_OPERATIONS:    return "io_operations";
        case RMAG_RES_NESTING_DEPTH:    return "nesting_depth";
        case RMAG_RES_DECOMPRESS_RATIO: return "decompress_ratio";
        case RMAG_RES_SOLVER_TIME:      return "solver_time";
        case RMAG_RES_FUZZ_ITERATIONS:  return "fuzz_iterations";
        case RMAG_RES_EVENT_COUNT:      return "event_count";
        default:                         return "unknown";
    }
}

const char *rmag_result_name(rmag_result_t result) {
    switch (result) {
        case RMAG_RESULT_OK:        return "ok";
        case RMAG_RESULT_DENIED:    return "denied";
        case RMAG_RESULT_NOT_FOUND: return "not_found";
        case RMAG_RESULT_INVALID:   return "invalid";
        default:                     return "unknown";
    }
}
