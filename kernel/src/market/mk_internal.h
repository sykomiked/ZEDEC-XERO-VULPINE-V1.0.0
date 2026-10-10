/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_internal.h — helpers shared by the kernel/src/market translation units.
 * Not a public interface.
 *
 * HONEST LIMITS. Internal plumbing only; see market.h for the module's
 * limits. Freestanding: no libc, no allocation, no float.
 */
#ifndef ZXV_MK_INTERNAL_H
#define ZXV_MK_INTERNAL_H

#include "market.h"

#define MK_DAY 86400u

bool mk__id_eq(const mk_id_t *a, const mk_id_t *b);
bool mk__id_zero(const mk_id_t *a);
void mk__id_copy(mk_id_t *dst, const mk_id_t *src);

mk_store_t *mk__store(mk_market_t *m, uint16_t h);
const mk_store_t *mk__store_c(const mk_market_t *m, uint16_t h);
mk_listing_t *mk__listing(mk_market_t *m, uint16_t h);
const mk_listing_t *mk__listing_c(const mk_market_t *m, uint16_t h);
mk_order_t *mk__order(mk_market_t *m, uint16_t h);
const mk_ccy_t *mk__ccy(const mk_market_t *m, uint8_t h);

/* floor/half-up/ceil of a*b/c with a 128-bit intermediate. */
bool mk__muldiv_round(uint64_t a, uint64_t b, uint64_t c, mk_round_t mode, uint64_t *out);

/* Tax on one line (M5). Writes the line total and its contained tax. */
mk_status_t mk__line_tax(const mk_market_t *m, const char *region, uint16_t tax_class,
                         uint64_t goods, uint64_t ship, uint64_t *line_total, uint64_t *tax,
                         bool *unconfigured);

/* Call the settle hook when any leg is non-zero. */
bool mk__settle(mk_market_t *m, const mk_settle_t *s);
void mk__notify(mk_market_t *m, mk_note_t k, uint32_t ref);
bool mk__verify(const mk_market_t *m, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                const uint8_t *sig, uint32_t sig_len);

/* Build one order from lines of a single store and currency. */
typedef struct {
    const mk_id_t *buyer;
    const char *region;
    bool community;
    const mk_id_t *agent;
    uint16_t mandate;
} mk__build_t;
int32_t mk__order_build(mk_market_t *m, const mk__build_t *b, const mk_cart_line_t *lines,
                        uint32_t n);
mk_status_t mk__mandate_ok(const mk_market_t *m, const mk_order_t *o, uint64_t pending);
void mk__order_digest(const mk_market_t *m, mk_order_t *o, uint16_t h);

/* Timers, per file. */
void mk__order_tick(mk_market_t *m);
void mk__sub_tick(mk_market_t *m);
void mk__inv_tick(mk_market_t *m);

/* Subscriptions: register a paid subscription order (mk_extra.c). */
int32_t mk__sub_on_paid(mk_market_t *m, uint16_t order);

#endif /* ZXV_MK_INTERNAL_H */
