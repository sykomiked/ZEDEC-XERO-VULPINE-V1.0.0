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
rational_t rmag_div_quotas(rational_t a, rational_t b);

#endif