/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lodestone_rules.h — Lodestone's rules in C, the reference its Trebago,
 * KRON and MYRA blocks are checked against (blocks/lodestone). Pure integer
 * functions, no state: the model (lodestone_model.c) and the language test
 * (tests/lodestone_zxapp.inc) both include this, so the languages are
 * tested against the very C the model runs.
 *
 *   LR1 METHODS PER GROUP: a method's group is its name up to the first
 *       '.' (the whole name when it has none); groups are numbered from 1
 *       in the order they first appear in rpc.discover's list. How many
 *       methods a group holds is witness.kind_count (count_by_kind, WR2,
 *       @num.witness_kind_count), used as it is: the group numbers are the
 *       kinds, the group asked for is want.
 *   LR2 MEAN LATENCY: the exact mean of the latencies measured, sum / n as
 *       a reduced fraction of milliseconds, never rounded; every latency
 *       0..LODESTONE_LATENCY_MAX (Retort's RT2 range, the longest call it
 *       allows) and at least one, else refused (Trebago divides by zero;
 *       KRON answers: held).
 *   LR3 FASTEST AND SLOWEST: retort.latency_fastest_ms and
 *       retort.latency_slowest_ms (fastest_latency, slowest_latency, RT2,
 *       @num.retort_latency_fastest_ms, @num.retort_latency_slowest_ms),
 *       used as they are, folded from LODESTONE_LATENCY_MAX and 0.
 *   LR4 A CONTACT RECORD of ^ZXLODESTONE holds integers of at most the
 *       digits its pieces declare, sign aside (blocks/lodestone/contact.myra):
 *       contact 3 (net.peers lists at most 256), bucket 3, ubh 1, seen 15
 *       (ms since 1970 until the year 33658), book 1, member 1, pending 9. */
#ifndef LODESTONE_RULES_H
#define LODESTONE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "retort_rules.h"
#include "witness_rules.h"

#define LODESTONE_LATENCY_MAX RETORT_LATENCY_MAX /* LR2: Retort's range, 3600000 ms */
#define LODESTONE_REFUSED     INT64_MIN

/* LR1: how many of the n group numbers are want (witness.kind_count). */
static inline int64_t lodestone_group_count(const int64_t *group, uint32_t n, int64_t want)
{
    return witness_kind_count(group, n, want);
}

/* LR2: *num / *den, reduced; false (nothing written) when refused. */
static inline bool lodestone_latency_mean(const int64_t *ms, uint32_t n, int64_t *num, int64_t *den)
{
    int64_t s = 0;
    if (!ms || !num || !den || n == 0) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!retort_latency_ok(ms[i])) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (s > INT64_MAX - ms[i]) return false; /* past 2^63 / 3600000 samples */
        s += ms[i];
    }
    int64_t d = (int64_t) n;
    int64_t g = retort_gcd(s, d); /* d >= 1, so g >= 1 */
    *num = s / g;
    *den = d / g;
    return true;
}

/* LR3: the fastest and the slowest of n latencies; false when one is no
 * latency or there is none. */
static inline bool lodestone_latency_span(const int64_t *ms, uint32_t n, int64_t *fastest,
                                          int64_t *slowest)
{
    int64_t lo = LODESTONE_LATENCY_MAX, hi = 0;
    if (!ms || !fastest || !slowest || n == 0) return false;
    for (uint32_t i = 0; i < n; i++) {
        lo = retort_latency_fastest(lo, ms[i]);
        hi = retort_latency_slowest(hi, ms[i]);
        if (lo == RETORT_REFUSED || hi == RETORT_REFUSED) return false;
    }
    *fastest = lo;
    *slowest = hi;
    return true;
}

/* LR4 */
static inline bool lodestone_contact_record_fits(int64_t contact, int64_t bucket, int64_t ubh,
                                                 int64_t seen, int64_t book, int64_t member,
                                                 int64_t pending)
{
    return witness_digits_fit(contact, 3) && witness_digits_fit(bucket, 3) &&
           witness_digits_fit(ubh, 1) && witness_digits_fit(seen, 15) &&
           witness_digits_fit(book, 1) && witness_digits_fit(member, 1) &&
           witness_digits_fit(pending, 9);
}

#endif
