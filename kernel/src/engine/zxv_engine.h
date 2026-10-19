/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_engine.h — the libzxv facade: compress, route, decompress.
 *
 * Three calls over modules that already exist and are already tested:
 *
 *   1  COMPRESS    a batch of gross obligations ("A owes B 100") is netted
 *                  multilaterally by the abacus (abacus.h): every member's net
 *                  position is kept exactly, offsetting cycles cancel, and at
 *                  most members-1 legs remain.
 *   2  ROUTE       the cheapest corridor path from A to B for an amount, over
 *                  DIRECTED corridors with their own fee and capacity, so the
 *                  cost from A to B need not equal the cost from B to A.
 *                  Exact integers: cost is the sum of each hop's fee in parts
 *                  per million; ties go to the fewest hops, then the lowest
 *                  member ids (deterministic on every machine).
 *   3  DECOMPRESS  the net legs are posted to the ledger of record
 *                  (pay_ledger.h), each as one atomic payment carrying the
 *                  0.08889% assurance fee split into its four buckets.
 *
 * GUARANTEES (each one tested)
 *   G1  COMPRESS changes no member's net position and conserves value; a
 *       failure leaves the caller's output untouched.
 *   G2  DECOMPRESS is all or nothing: it first checks every payer can cover
 *       its legs plus an upper bound on their fees (a leg whose amount plus
 *       that bound exceeds the ledger's per-line limit, 2^59, is refused as
 *       ZXV_E_ARG). If a posting is still
 *       refused part-way, the postings already made are reversed (pacs.004
 *       style, newest first), so the batch leaves no partial state. Only the
 *       payers' sub-unit fee carries (always < 1 minor unit) can differ.
 *   G3  After every call the ledger satisfies L1-L5 (pay_ledger.h): DEBIT ==
 *       CREDIT per asset, EQUITY sums to zero, no holder below zero.
 *   G4  Money enters only through zxv_engine_fund (a deposit mirrored from
 *       outside, recorded as a claim on the engine's issuer account) and
 *       leaves only through zxv_engine_withdraw. Fees stay inside, in the four
 *       bucket accounts.
 *
 * HONEST LIMITS. Netting is not consensus and a route is not liquidity: the
 * corridor capacities are what the caller says they are. Operating this for
 * other people's money needs licences. Freestanding: no libc, no allocation,
 * no floating point.
 */
#ifndef ZXV_ENGINE_H
#define ZXV_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "../pay/pay_ledger.h"
#include "../abacus/abacus.h"

#define ZXV_ENGINE_MAX_MEMBERS   AB_MAX_MEMBERS
#define ZXV_ENGINE_MAX_CORRIDORS 256u
#define ZXV_ENGINE_MAX_LEGS      PAY_MAX_ACCOUNTS
#define ZXV_FEE_PPM_MAX          1000000u /* a corridor fee is at most 100% */

typedef enum {
    ZXV_E_OK = 0,
    ZXV_E_ARG = -1,
    ZXV_E_FULL = -2,
    ZXV_E_NO_ROUTE = -3,
    ZXV_E_FUNDS = -4,
    ZXV_E_LEDGER = -5,
    ZXV_E_OVERFLOW = -6
} zxv_status_t;

typedef struct {
    uint32_t from;
    uint32_t to;
    uint64_t amount; /* minor units of the engine's asset */
} zxv_obligation_t;

typedef struct {
    uint32_t from;
    uint32_t to;
    uint32_t fee_ppm;  /* this direction's fee, parts per million */
    uint64_t capacity; /* the most this direction can carry */
} zxv_corridor_t;

typedef struct {
    pay_ledger_t L;
    abacus_t ab;
    uint8_t seed[32];
    uint64_t seq;
    uint16_t asset;
    uint32_t issuer;
    uint32_t bucket[PAY_ASSURE_BUCKETS];
    uint32_t acct[ZXV_ENGINE_MAX_MEMBERS];
    uint32_t n_members;
    zxv_corridor_t cor[ZXV_ENGINE_MAX_CORRIDORS];
    uint32_t n_cor;
    uint64_t fees; /* assurance fees collected */
} zxv_engine_t;

/* Open an engine for one asset. numeric > 0 registers an ISO 4217 currency
 * (alpha, numeric and minor units from the caller's table); numeric == 0 a
 * plain unit. `seed` (32 bytes) makes posting identifiers unique. */
zxv_status_t zxv_engine_init(zxv_engine_t *e, const char *alpha, uint16_t numeric, uint8_t minor,
                             const uint8_t seed[32]);
zxv_status_t zxv_engine_add_member(zxv_engine_t *e, uint32_t *member);
zxv_status_t zxv_engine_fund(zxv_engine_t *e, uint32_t member, uint64_t amount, uint64_t tick);
zxv_status_t zxv_engine_withdraw(zxv_engine_t *e, uint32_t member, uint64_t amount, uint64_t tick);
uint64_t zxv_engine_balance(const zxv_engine_t *e, uint32_t member);
/* The balance of fee bucket b (PAY_ASSURE_*). */
uint64_t zxv_engine_bucket(const zxv_engine_t *e, uint32_t b);
zxv_status_t zxv_engine_add_corridor(zxv_engine_t *e, const zxv_corridor_t *c);
/* G3, plus: the issuer's claim equals everything held by members and buckets. */
bool zxv_engine_check(const zxv_engine_t *e);

/* 1: net `in` into `out` (cap entries). */
zxv_status_t zxv_compress(zxv_engine_t *e, const zxv_obligation_t *in, uint32_t n,
                          zxv_obligation_t *out, uint32_t cap, uint32_t *n_out);

/* 2: cheapest path src -> dst for `amount`; path[0] = src, path[n-1] = dst. */
zxv_status_t zxv_route(const zxv_engine_t *e, uint32_t src, uint32_t dst, uint64_t amount,
                       uint32_t *path, uint32_t cap, uint32_t *n_path, uint64_t *cost_ppm);

/* 3: post `legs` with the assurance fee, all or nothing. *fees_out gets the
 * total fee charged (may be NULL). */
zxv_status_t zxv_decompress(zxv_engine_t *e, const zxv_obligation_t *legs, uint32_t n,
                            uint64_t tick, uint64_t *fees_out);

#endif /* ZXV_ENGINE_H */
