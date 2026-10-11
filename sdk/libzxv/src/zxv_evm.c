/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_evm.c — ABI 1.1: the ledger's exact balances as an EVM state-channel
 * state, encoded exactly as Solidity's
 *   abi.encode(bytes32 channelId, uint256 nonce, address[] parties,
 *              int256[] balances)
 * so sdk/solidity/ZxvStateChannel.sol recomputes the same keccak256 digest.
 *
 * Layout (each slot 32 bytes, big-endian):
 *   0  channelId
 *   1  nonce
 *   2  offset of parties  = 4 * 32
 *   3  offset of balances = (4 + 1 + n) * 32
 *   4  n, then n addresses (left-padded to 32 bytes)
 *   .  n, then n int256 (two's complement, sign-extended)
 * Total (6 + 2n) * 32 bytes. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <stdlib.h>
#include <string.h>

static void put_u64(uint8_t slot[32], uint64_t v)
{
    memset(slot, 0, 32);
    for (int i = 0; i < 8; i++) slot[31 - i] = (uint8_t) (v >> (8 * i));
}

static void put_i64(uint8_t slot[32], int64_t v)
{
    memset(slot, v < 0 ? 0xFF : 0x00, 32);
    uint64_t u = (uint64_t) v;
    for (int i = 0; i < 8; i++) slot[31 - i] = (uint8_t) (u >> (8 * i));
}

static int32_t encode(const uint8_t channel_id[32], uint64_t nonce, const uint8_t *addresses,
                      const int64_t *balances, uint32_t count, uint8_t *buf, size_t cap_bytes,
                      size_t *out_len, uint8_t digest[32])
{
    if (!channel_id || !out_len || !digest || (count && (!addresses || !balances)))
        return zxvi_fail(ZXV_E_ARG, "NULL argument");
    if (count == 0 || count > 1024) return zxvi_fail(ZXV_E_ARG, "a channel has 1..1024 parties");
    size_t need = (size_t) (6 + 2 * (size_t) count) * 32;
    *out_len = need;
    if (!buf || cap_bytes < need)
        return zxvi_fail(ZXV_E_BUFFER, "state encoding needs %zu bytes", need);
    uint8_t *p = buf;
    memcpy(p, channel_id, 32);
    put_u64(p + 32, nonce);
    put_u64(p + 64, 4 * 32);
    put_u64(p + 96, (uint64_t) (5 + count) * 32);
    p += 128;
    put_u64(p, count);
    p += 32;
    for (uint32_t i = 0; i < count; i++, p += 32) {
        memset(p, 0, 12);
        memcpy(p + 12, addresses + 20 * (size_t) i, 20);
    }
    put_u64(p, count);
    p += 32;
    for (uint32_t i = 0; i < count; i++, p += 32) put_i64(p, balances[i]);
    return zxv_keccak256(buf, need, digest);
}

int32_t zxv_evm_encode_state(const zxv_cap *cap, const uint8_t channel_id[32], uint64_t nonce,
                             const uint8_t *addresses, const int64_t *balances, uint32_t count,
                             uint8_t *buf, size_t cap_bytes, size_t *out_len, uint8_t digest[32])
{
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_EVM, "evm_encode_state", NULL);
    if (rc) return rc;
    return encode(channel_id, nonce, addresses, balances, count, buf, cap_bytes, out_len, digest);
}

int32_t zxv_evm_ledger_state(const zxv_cap *cap, const uint8_t channel_id[32], uint64_t nonce,
                             const char *ccy, const uint32_t *accounts, const uint8_t *addresses,
                             uint32_t count, uint8_t *buf, size_t cap_bytes, size_t *out_len,
                             uint8_t digest[32])
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_EVM | ZXV_CAP_LEDGER_READ, "evm_ledger_state", &ctx);
    if (rc) return rc;
    if (!ccy || !accounts || count == 0 || count > 1024)
        return zxvi_fail(ZXV_E_ARG, "ccy/accounts missing or count not 1..1024");
    int64_t *bal = malloc(count * sizeof *bal);
    if (!bal) return zxvi_fail(ZXV_E_NOMEM, "balance allocation failed");
    if ((rc = zxvi_ctx_enter(ctx))) {
        free(bal);
        return rc;
    }
    for (uint32_t i = 0; i < count && rc == ZXV_OK; i++)
        rc = zxvi_ledger_equity(ctx, accounts[i], ccy, &bal[i]);
    zxvi_ctx_leave(ctx);
    if (rc == ZXV_OK) rc = encode(channel_id, nonce, addresses, bal, count, buf, cap_bytes, out_len, digest);
    free(bal);
    return rc;
}
