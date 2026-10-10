/*
 * rmag_core.c — Rational Magnitude Engine (RMAG)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

#include "m5_types.h"
#include "rmag_core.h"
#include <stdlib.h>
#include <assert.h>

static resource_table_t *g_resource_table;

void rmag_init(ordinal_t slot_count) {
    if (g_resource_table) return; /* Already initialized */
    resource_table_t *t = malloc(sizeof(resource_table_t));
    if (!t) return;
    t->slots = calloc(slot_count, sizeof(rational_t));
    if (!t->slots) return; /* no free() in the kernel heap: t is abandoned */
    for (ordinal_t i = 0; i < slot_count; i++) t->slots[i].den = 1; /* 0/1, not 0/0 */
    t->slot_count = slot_count;
    g_resource_table = t;
}

rational_t rmag_get_quota(ordinal_t slot) {
    if (!g_resource_table || slot >= g_resource_table->slot_count) {
        rational_t zero = {0, 1};
        return zero;
    }
    return g_resource_table->slots[slot];
}

void rmag_set_quota(ordinal_t slot, rational_t quota) {
    if (!g_resource_table || slot >= g_resource_table->slot_count) return;
    g_resource_table->slots[slot] = quota;
}

rational_t rmag_add_quotas(rational_t a, rational_t b) {
    rational_t sum = {a.num * b.den + b.num * a.den, a.den * b.den};
    sum = rational_normalize(sum);
    return sum;
}

rational_t rmag_sub_quotas(rational_t a, rational_t b) {
    rational_t diff = {a.num * b.den - b.num * a.den, a.den * b.den};
    diff = rational_normalize(diff);
    return diff;
}

rational_t rmag_mul_quotas(rational_t a, rational_t b) {
    rational_t product = {a.num * b.num, a.den * b.den};
    product = rational_normalize(product);
    return product;
}

/* Division by zero (b.num == 0) and an operand with den == 0 have no value.
 * Before this check, {a.num * b.den, a.den * 0} reached rational_normalize,
 * which turns x/0 into sign(x)/1: 5 / 0 returned 1, a fabricated quota.
 * Such a division now returns 0/1 (the convention of rmag_rational_divide
 * in rmag.c); rmag_div_quotas_checked reports it instead. Model and proof:
 * proofs/rational_bounds/RationalBounds.lean (divQ, div_by_zero_detected). */
rational_t rmag_div_quotas(rational_t a, rational_t b) {
    if (b.num == 0 || a.den == 0 || b.den == 0) {
        rational_t zero = {0, 1};
        return zero;
    }
    rational_t quotient = {a.num * b.den, a.den * b.num};
    quotient = rational_normalize(quotient);
    return quotient;
}

/* INT64_MIN has no int64 negation, which rational_normalize and m5_gcd take. */
static bool rmag_i64_ok(int64_t v)
{
    return v != INT64_MIN;
}

bool rmag_div_quotas_checked(rational_t a, rational_t b, rational_t *out)
{
    int64_t n, d;
    if (!out) return false;
    if (b.num == 0 || a.den == 0 || b.den == 0) return false;
    if (!rmag_i64_ok(a.num) || !rmag_i64_ok(a.den) || !rmag_i64_ok(b.num) || !rmag_i64_ok(b.den))
        return false;
    if (__builtin_mul_overflow(a.num, b.den, &n) || __builtin_mul_overflow(a.den, b.num, &d))
        return false;
    if (!rmag_i64_ok(n) || !rmag_i64_ok(d)) return false;
    rational_t q = {n, d};
    *out = rational_normalize(q);
    return true;
}

/* ---- DECLARATION -----------------------------------------------------------

 * rmag is the quota arithmetic every dharma/dharana/sephirot file reaches for:
 * `nm -u` on dharana.o, sephirot.o, chakra.o, karma.o, mantra.o, uvn.o,
 * naga_raja.o and dharma.o names rmag_add_quotas / rmag_mul_quotas /
 * rmag_sub_quotas / rmag_get_quota / rmag_set_quota and nothing else across
 * that boundary. rmag_core.o's own `nm -u` is EMPTY -- it calls out to nothing,
 * which is why it requires nothing. It is the floor of that subtree.
 *
 * NO BRING-UP ON PURPOSE. rmag is a leaf: rooting it here would put its symbols
 * in the ELF whether or not anything ever computes a quota. It is reachable
 * exactly when one of its callers is, and that is the honest signal.
 */
#include "zxv_decl.h"
ZXV_DECLARE(rmag,
    ZXV_PROVIDES(rmag_ready),
    ZXV_REQUIRES_NONE,
    ZXV_NO_BRINGUP);
