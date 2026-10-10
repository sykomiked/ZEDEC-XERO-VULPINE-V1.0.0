/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_crypto.h — chain-agnostic crypto payment records.
 *
 * A transfer intent records what is to be paid on an external chain: the
 * ledger asset (with its ISO 24165 DTI when the operator registered one),
 * chain and chain id, from/to addresses, amount and fee in the asset's
 * minor units, a slot for the transaction hash, confirmations, the UETR that
 * ties it to the pay_ledger posting, and the FATF R.16 travel-rule record.
 *
 * Address validation:
 *   Bitcoin  base58check (P2PKH, P2SH; double SHA-256 checksum) and
 *            segwit bech32 (BIP-173, witness v0) / bech32m (BIP-350, v1+,
 *            including P2TR), mainnet "bc" and testnet "tb".
 *   Ethereum 0x + 40 hex with the EIP-55 mixed-case checksum (Keccak-256,
 *            built on mlkem/keccak.h; see pay_keccak256). All-lower or
 *            all-upper addresses carry no checksum and are reported as such.
 *
 * THE WALLET BOUNDARY. Nothing here creates, signs or broadcasts an
 * external-chain transaction and nothing here holds a private key. An
 * intent is handed to the operator's PAY_GW_CHAIN gateway callback (a
 * wallet); the wallet signs and broadcasts, and the operator records the
 * resulting hash and confirmations back here. Signing is the wallet's job.
 *
 * HONEST LIMITS. A checksum-valid address can still belong to nobody, to the
 * wrong chain, or to a sanctioned party; screening is a hook (pay_roles).
 * Schema validity is not certification; there is no SWIFT or CIPS
 * connectivity; transmitting crypto for others needs licences (VASP /
 * money transmitter) in most jurisdictions; VFV's store-credit status is a
 * legal question for counsel. Freestanding.
 */
#ifndef ZXV_PAY_CRYPTO_H
#define ZXV_PAY_CRYPTO_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"
#include "pay_ledger.h"
#include "pay_roles.h"

typedef enum {
    PAY_ADDR_INVALID = 0,
    PAY_ADDR_BTC_P2PKH,
    PAY_ADDR_BTC_P2SH,
    PAY_ADDR_BTC_P2WPKH,
    PAY_ADDR_BTC_P2WSH,
    PAY_ADDR_BTC_P2TR,
    PAY_ADDR_BTC_SEGWIT, /* other witness version / length */
    PAY_ADDR_ETH_CHECKSUMMED,
    PAY_ADDR_ETH_NO_CHECKSUM
} pay_addr_kind_t;

/* Bitcoin. testnet selects version bytes 0x6f/0xc4 and HRP "tb". */
pay_addr_kind_t pay_btc_addr_check(const char *addr, bool testnet);
/* Base58 decode (no checksum). Returns decoded length or -1. */
int32_t pay_base58_decode(const char *s, uint8_t *out, uint32_t cap);
/* Bech32/bech32m segwit decode: witness version and program. */
bool pay_segwit_decode(const char *addr, const char *hrp, uint8_t *version, uint8_t *prog,
                       uint32_t *prog_len);

/* Ethereum. */
pay_addr_kind_t pay_eth_addr_check(const char *addr);
/* EIP-55 checksummed rendering of a 20-byte address ("0x" + 40 chars). */
void pay_eth_checksum(const uint8_t addr20[20], char out[43]);

typedef enum {
    PAY_CHAIN_BITCOIN = 0,
    PAY_CHAIN_BITCOIN_TESTNET = 1,
    PAY_CHAIN_EVM = 2 /* chain_id per EIP-155: 1 Ethereum mainnet, ... */
} pay_chain_t;

typedef enum {
    PAY_INTENT_DRAFT = 0,
    PAY_INTENT_VALIDATED,
    PAY_INTENT_HANDED_TO_WALLET,
    PAY_INTENT_BROADCAST, /* the wallet reported a tx hash */
    PAY_INTENT_CONFIRMED,
    PAY_INTENT_FAILED
} pay_intent_state_t;

typedef struct {
    uint16_t asset;
    char asset_code[PAY_CODE_MAX + 1];
    char dti[10];
    uint8_t minor;
    pay_chain_t chain;
    uint64_t chain_id;
    char from[91];
    char to[91];
    uint64_t amount;
    uint64_t fee;
    uint8_t tx_hash[32];
    bool has_hash;
    uint32_t confirmations;
    uint32_t required_confirmations;
    pay_intent_state_t state;
    char uetr[PAY_UETR_LEN + 1];
    pay_travel_rule_t travel;
} pay_crypto_intent_t;

/* Fill an intent from a ledger crypto asset. PAY_ERR_NO_ASSET when the asset
 * is not a crypto asset. */
pay_status_t pay_crypto_intent_init(pay_crypto_intent_t *it, const pay_ledger_t *L, uint16_t asset,
                                    pay_chain_t chain, uint64_t chain_id, const char *from,
                                    const char *to, uint64_t amount, uint64_t fee,
                                    uint32_t required_confirmations, const char *uetr);
/* Check both addresses for the chain, the UETR, amount > 0. -> VALIDATED. */
pay_status_t pay_crypto_intent_validate(pay_crypto_intent_t *it);
/* Run the role's compliance gate (travel rule, screening, limits), then hand
 * the intent, rendered as a canonical text record, to the role's
 * PAY_GW_CHAIN gateway. -> HANDED_TO_WALLET. */
pay_status_t pay_crypto_intent_handoff(pay_crypto_intent_t *it, pay_role_cfg_t *role,
                                       uint8_t kyc_tier, uint64_t tick);
/* Render the canonical text record handed to the wallet. */
int32_t pay_crypto_intent_render(const pay_crypto_intent_t *it, char *out, uint32_t cap);
/* The wallet reports back. */
pay_status_t pay_crypto_record_hash(pay_crypto_intent_t *it, const uint8_t hash[32]);
pay_status_t pay_crypto_record_confirmations(pay_crypto_intent_t *it, uint32_t conf);
pay_status_t pay_crypto_record_failure(pay_crypto_intent_t *it);

#endif /* ZXV_PAY_CRYPTO_H */
