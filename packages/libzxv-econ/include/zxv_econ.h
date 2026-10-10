/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_econ.h - public header of libzxv-econ.
 *
 * The engine facade as one static library, three calls:
 *   zxv_compress    multilateral netting of gross obligations (abacus):
 *                   every member's net position kept exactly, at most
 *                   members-1 legs left
 *   zxv_route       cheapest path over directed corridors with their own
 *                   fee and capacity (d(A->B) need not equal d(B->A)),
 *                   exact integers, deterministic tie-breaks
 *   zxv_decompress  the legs posted to the ledger of record (pay_ledger),
 *                   each with the 0.08889% assurance fee split into its four
 *                   buckets; all or nothing, with rollback
 * plus the engine's lifecycle (init, members, fund, withdraw, corridors,
 * check). Integer only; no allocation; no randomness source (the caller's
 * seed derives every posting id). See zxv/engine/zxv_engine.h for the
 * guarantees and HONEST LIMITS.
 *
 * zxv_engine_t is large (the ledger journal): keep it static or on the
 * heap, not on a small stack.
 */
#ifndef ZXV_ECON_H
#define ZXV_ECON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "zxv/engine/zxv_engine.h"

#ifdef __cplusplus
}
#endif

#endif /* ZXV_ECON_H */
