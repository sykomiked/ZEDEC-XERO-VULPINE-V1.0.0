/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_capmkt.h — glue between the device mesh and the capacity market
 * (kernel/src/capmkt). When dm_route() answers DM_ROUTE_NEED_CAPACITY, or
 * the user asks for more than the home node can do, the app buys capacity:
 *
 *   1. dm_capmkt_bid_action() describes the escrow lock (qty x limit VFV,
 *      rail DEBIT 555) as a DM_MONEY_ESCROW money action;
 *   2. the user confirms it on a held device (dm_money_confirm_local on the
 *      phone, or dm_money_ask from the home node);
 *   3. dm_capmkt_bid() checks that confirmation (dm_money_verify) and that
 *      it is for exactly this bid, then places it with cm_bid().
 * A home node offering spare capacity uses dm_capmkt_offer() (no money
 * leaves the user, so no confirmation is needed).
 *
 * All of the user's devices share one market account, derived from the
 * mesh id, so capacity bought from the phone can be used by the home node
 * and the other way round.
 */
#ifndef ZXV_DM_CAPMKT_H
#define ZXV_DM_CAPMKT_H

#include "devmesh.h"
#include "dm_remote.h"
#include "../capmkt/capmkt.h"

/* SHA3-256("zxv-devmesh/v1/market" || mesh id), first 16 bytes. */
dm_status_t dm_capmkt_account(const dm_mesh_t *m, uint8_t id[CM_ID_BYTES]);

dm_status_t dm_capmkt_bid_action(const dm_mesh_t *m, cm_key_t key, uint64_t qty, uint64_t limit,
                                 const uint8_t action_id[DM_ID_BYTES], uint64_t expires_ms,
                                 dm_money_t *out);

dm_status_t dm_capmkt_bid(dm_mesh_t *m, cm_market_t *mkt, const dm_money_t *a,
                          const dm_confirm_t *c, cm_key_t key, uint64_t qty, uint64_t limit,
                          uint64_t order_expires_ms, uint64_t now_ms, uint32_t *order_id);

dm_status_t dm_capmkt_offer(dm_mesh_t *m, cm_market_t *mkt, cm_key_t key, uint64_t qty,
                            uint64_t price, uint64_t expires_ms, uint32_t *order_id);

#endif /* ZXV_DM_CAPMKT_H */
