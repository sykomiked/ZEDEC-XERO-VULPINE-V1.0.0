/* rmag_core.h — Rational Magnitude Engine (RMAG)
 * Uses rational_t { int64_t num, den } from m5_types.h for all quota arithmetic.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

#ifndef RMAG_CORE_H
#define RMAG_CORE_H

#include "m5_types.h"

typedef struct resource_table {
    ordinal_t slot_count;
    rational_t *slots;
} resource_table_t;

void rmag_init(ordinal_t slot_count);
rational_t rmag_get_quota(ordinal_t slot);
void rmag_set_quota(ordinal_t slot, rational_t quota);
rational_t rmag_add_quotas(rational_t a, rational_t b);
rational_t rmag_sub_quotas(rational_t a, rational_t b);
rational_t rmag_mul_quotas(rational_t a, rational_t b);
/* a / b. A zero divisor (b.num == 0) or an operand with den == 0 yields 0/1,
 * never a fabricated value. add/sub/mul/div do NOT check int64 overflow of
 * the cross products (see docs/FORMAL_INVARIANTS.md). */
rational_t rmag_div_quotas(rational_t a, rational_t b);
/* Checked a / b: false (out untouched) on a zero divisor, a den == 0 operand,
 * an INT64_MIN operand or product, or int64 overflow of a.num * b.den or
 * a.den * b.num; otherwise *out is the normalized quotient, out->den > 0. */
bool rmag_div_quotas_checked(rational_t a, rational_t b, rational_t *out);

#endif