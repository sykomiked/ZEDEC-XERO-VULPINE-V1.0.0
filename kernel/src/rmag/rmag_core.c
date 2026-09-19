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
    g_resource_table = malloc(sizeof(resource_table_t));
    g_resource_table->slot_count = slot_count;
    g_resource_table->slots = calloc(slot_count, sizeof(rational_t));
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

rational_t rmag_div_quotas(rational_t a, rational_t b) {
    rational_t quotient = {a.num * b.den, a.den * b.num};
    quotient = rational_normalize(quotient);
    return quotient;
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
