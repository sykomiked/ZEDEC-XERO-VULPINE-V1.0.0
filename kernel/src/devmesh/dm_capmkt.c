/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_capmkt.c — see dm_capmkt.h. */
#include "dm_capmkt.h"

#include "../mlkem/keccak.h"
#include "../pay/pay_util.h"

dm_status_t dm_capmkt_account(const dm_mesh_t *m, uint8_t id[CM_ID_BYTES])
{
    static const char L[] = "zxv-devmesh/v1/market";
    if (!m || !id) return DM_ERR_ARG;
    if (!m->joined) return DM_ERR_STATE;
    uint8_t in[sizeof L - 1 + DM_ID_BYTES], h[32];
    dm_mcpy(in, L, sizeof L - 1);
    dm_mcpy(in + sizeof L - 1, m->mesh_id, DM_ID_BYTES);
    sha3_256(in, sizeof in, h);
    dm_mcpy(id, h, CM_ID_BYTES);
    return DM_OK;
}

static void key_payee(cm_key_t key, uint64_t qty, uint64_t limit, uint8_t out[DM_HASH_BYTES])
{
    static const char L[] = "zxv-capmkt/v1/bid";
    uint8_t in[sizeof L - 1 + 4 + 16];
    uint32_t n = sizeof L - 1;
    dm_mcpy(in, L, n);
    in[n++] = key.resource;
    in[n++] = key.tenor;
    in[n++] = (uint8_t) (key.region >> 8);
    in[n++] = (uint8_t) key.region;
    for (int i = 0; i < 8; i++) in[n++] = (uint8_t) (qty >> (56 - 8 * i));
    for (int i = 0; i < 8; i++) in[n++] = (uint8_t) (limit >> (56 - 8 * i));
    sha3_256(in, n, out);
}

dm_status_t dm_capmkt_bid_action(const dm_mesh_t *m, cm_key_t key, uint64_t qty, uint64_t limit,
                                 const uint8_t action_id[DM_ID_BYTES], uint64_t expires_ms,
                                 dm_money_t *out)
{
    if (!m || !action_id || !out || !qty || !limit) return DM_ERR_ARG;
    pay_u128 total = pay_mul64(qty, limit);
    if (total.hi || total.lo > CM_AMOUNT_MAX) return DM_ERR_SIZE;
    dm_mset(out, 0, sizeof *out);
    dm_mcpy(out->action_id, action_id, DM_ID_BYTES);
    out->kind = DM_MONEY_ESCROW;
    out->rail = DM_RAIL_DEBIT;
    out->currency[0] = 'V';
    out->currency[1] = 'F';
    out->currency[2] = 'V';
    out->amount = total.lo;
    key_payee(key, qty, limit, out->payee);
    static const char memo[] = "capacity bid: escrow at most qty x limit";
    dm_mcpy(out->memo, memo, sizeof memo);
    dm_mcpy(out->origin, m->self_id, DM_ID_BYTES);
    out->expires_ms = expires_ms;
    return DM_OK;
}

dm_status_t dm_capmkt_bid(dm_mesh_t *m, cm_market_t *mkt, const dm_money_t *a,
                          const dm_confirm_t *c, cm_key_t key, uint64_t qty, uint64_t limit,
                          uint64_t order_expires_ms, uint64_t now_ms, uint32_t *order_id)
{
    if (!m || !mkt || !a || !c) return DM_ERR_ARG;
    pay_u128 total = pay_mul64(qty, limit);
    uint8_t payee[DM_HASH_BYTES];
    key_payee(key, qty, limit, payee);
    if (a->kind != DM_MONEY_ESCROW || a->rail != DM_RAIL_DEBIT || total.hi ||
        a->amount != total.lo || !dm_meq(a->payee, payee, DM_HASH_BYTES))
        return DM_ERR_PERM; /* the confirmation is for a different bid */
    dm_status_t st = dm_money_verify(m, a, c, now_ms);
    if (st != DM_OK) return st;
    uint8_t acct[CM_ID_BYTES];
    st = dm_capmkt_account(m, acct);
    if (st != DM_OK) return st;
    cm_status_t cs = cm_bid(mkt, acct, key, qty, limit, order_expires_ms, order_id);
    return cs == CM_OK ? DM_OK : (cs == CM_ERR_FULL ? DM_ERR_FULL : DM_ERR_PERM);
}

dm_status_t dm_capmkt_offer(dm_mesh_t *m, cm_market_t *mkt, cm_key_t key, uint64_t qty,
                            uint64_t price, uint64_t expires_ms, uint32_t *order_id)
{
    if (!m || !mkt) return DM_ERR_ARG;
    uint8_t acct[CM_ID_BYTES];
    dm_status_t st = dm_capmkt_account(m, acct);
    if (st != DM_OK) return st;
    cm_status_t cs = cm_ask(mkt, acct, key, qty, price, expires_ms, order_id);
    return cs == CM_OK ? DM_OK : (cs == CM_ERR_FULL ? DM_ERR_FULL : DM_ERR_PERM);
}
