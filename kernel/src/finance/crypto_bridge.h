/* crypto_bridge.h — Web2-Web3 Bidirectional Cryptocurrency Bridge
 *
 * Hardware-as-code bridge connecting every major blockchain ecosystem
 * to the VOVINA SHAKINA kernel. Dual-directional:
 *   Web2 → Web3: conventional financial instruments tokenized on-chain
 *   Web3 → Web2: on-chain assets accessible via conventional rails
 *
 * Supports all major chains and smart contract languages:
 *   Chains: Bitcoin, Ethereum, BSC, Polygon, Solana, Avalanche, Cardano,
 *           Polkadot, Cosmos, Algorand, Near, Aptos, Sui, Stellar, Ripple,
 *           Litecoin, Dogecoin, Toncoin, Tron, Arbitrum, Optimism, Base,
 *           zkSync, Linea, Scroll, Mantle, Blast, and more
 *   Languages: Solidity, Vyper, Rust, Move, Cairo, Plutus, Michelson,
 *              Clarity, WebAssembly (Wasm), Go, Leo
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef CRYPTO_BRIDGE_H
#define CRYPTO_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"

/* ===== Blockchain Networks ===== */

typedef enum {
    CHAIN_BITCOIN      = 0,
    CHAIN_ETHEREUM     = 1,
    CHAIN_BSC          = 2,
    CHAIN_POLYGON      = 3,
    CHAIN_SOLANA       = 4,
    CHAIN_AVALANCHE    = 5,
    CHAIN_CARDANO      = 6,
    CHAIN_POLKADOT     = 7,
    CHAIN_COSMOS       = 8,
    CHAIN_ALGORAND     = 9,
    CHAIN_NEAR         = 10,
    CHAIN_APTOS        = 11,
    CHAIN_SUI          = 12,
    CHAIN_STELLAR      = 13,
    CHAIN_RIPPLE       = 14,
    CHAIN_LITECOIN     = 15,
    CHAIN_DOGECOIN     = 16,
    CHAIN_TONCOIN      = 17,
    CHAIN_TRON         = 18,
    CHAIN_ARBITRUM     = 19,
    CHAIN_OPTIMISM     = 20,
    CHAIN_BASE         = 21,
    CHAIN_ZKSYNC       = 22,
    CHAIN_LINEA        = 23,
    CHAIN_SCROLL       = 24,
    CHAIN_MANTLE       = 25,
    CHAIN_BLAST        = 26,
    CHAIN_FANTOM       = 27,
    CHAIN_HEDERA       = 28,
    CHAIN_FLOW         = 29,
    CHAIN_TEZOS        = 30,
    CHAIN_FILECOIN     = 31,
    CHAIN_CHAINLINK    = 32,
    CHAIN_UNISAT       = 33,
    CHAIN_STACKS       = 34,
    CHAIN_MAX          = 35
} chain_id_t;

/* ===== Smart Contract Languages ===== */

typedef enum {
    LANG_SOLIDITY    = 0,
    LANG_VYPER       = 1,
    LANG_RUST        = 2,
    LANG_MOVE        = 3,
    LANG_CAIRO       = 4,
    LANG_PLUTUS      = 5,
    LANG_MICHELSON   = 6,
    LANG_CLARITY     = 7,
    LANG_WASM        = 8,
    LANG_GO          = 9,
    LANG_LEO         = 10,
    LANG_MAX         = 11
} contract_lang_t;

/* ===== Bridge Direction ===== */

typedef enum {
    BRIDGE_WEB2_TO_WEB3 = 0,  /* Tokenize conventional asset on-chain */
    BRIDGE_WEB3_TO_WEB2 = 1,  /* Make on-chain asset available conventionally */
    BRIDGE_CROSS_CHAIN  = 2,  /* Move between chains */
} bridge_direction_t;

/* ===== Token Standards ===== */

typedef enum {
    TOKEN_FUNGIBLE     = 0,   /* ERC-20, SPL, etc. */
    TOKEN_NON_FUNGIBLE = 1,   /* ERC-721, ERC-1155 */
    TOKEN_SEMI_FUNGIBLE = 2,  /* ERC-1155 */
    TOKEN_SOVEREIGN    = 3,   /* M⁵-native sovereign token */
    TOKEN_VOUCHER      = 4,   /* Floating voucher tokenized */
    TOKEN_MAX          = 5
} token_standard_t;

/* ===== Bridge Device (hardware-as-code) ===== */

typedef struct {
    uint32_t bridge_id;
    chain_id_t source_chain;
    chain_id_t dest_chain;
    bridge_direction_t direction;
    
    /* Register map */
    surplus_real_t reg_amount;       /* Amount to bridge */
    uint64_t reg_source_addr;        /* Source address (hashed) */
    uint64_t reg_dest_addr;          /* Destination address (hashed) */
    uint32_t reg_confirmations;      /* Block confirmations */
    surplus_real_t reg_bridge_fee;   /* Bridge fee */
    surplus_real_t reg_gas_price;    /* Gas price estimate */
    
    /* M⁵ coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA buffer for bridge proofs */
    uint32_t dma_proofs[8];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQ flags */
    bool irq_bridge_requested;
    bool irq_source_confirmed;
    bool irq_dest_confirmed;
    bool irq_coverage_breach;
    bool irq_reorg_detected;
    
    /* Token info */
    token_standard_t token_type;
    contract_lang_t contract_lang;
    uint64_t token_id;               /* For NFTs */
    char token_symbol[16];
    char contract_addr[42];          /* Hex address */
    
    /* Status */
    bool completed;
    uint32_t source_tx_hash;         /* Simplified hash */
    uint32_t dest_tx_hash;
} bridge_device_t;

/* ===== Bridge Registry ===== */

typedef struct {
    bridge_device_t bridges[512];
    uint32_t num_bridges;
    
    /* Chain status */
    bool chain_active[CHAIN_MAX];
    surplus_real_t chain_coverage[CHAIN_MAX];
    uint32_t chain_tx_count[CHAIN_MAX];
    
    /* System metrics */
    surplus_real_t total_volume_bridged;
    uint32_t total_bridges_completed;
    surplus_real_t total_fees;
} bridge_registry_t;

/* ===== API ===== */

void bridge_registry_init(bridge_registry_t *reg);

/* Create bridge — like powering on a device */
uint32_t bridge_create(bridge_registry_t *reg,
                        chain_id_t source,
                        chain_id_t dest,
                        bridge_direction_t dir,
                        surplus_real_t amount,
                        uint64_t source_addr,
                        uint64_t dest_addr,
                        token_standard_t token_type,
                        contract_lang_t lang,
                        const char *token_symbol,
                        const char *contract_addr);

/* Execute bridge */
int32_t bridge_execute(bridge_registry_t *reg, uint32_t bridge_id);

/* Confirm source chain */
int32_t bridge_confirm_source(bridge_registry_t *reg, uint32_t bridge_id,
                               uint32_t tx_hash, uint32_t confirmations);

/* Confirm destination chain */
int32_t bridge_confirm_dest(bridge_registry_t *reg, uint32_t bridge_id,
                             uint32_t tx_hash);

/* Cross-chain swap */
int32_t bridge_cross_chain_swap(bridge_registry_t *reg,
                                 chain_id_t from,
                                 chain_id_t to,
                                 surplus_real_t amount,
                                 uint64_t from_addr,
                                 uint64_t to_addr);

/* Tokenize conventional asset (Web2 → Web3) */
uint32_t bridge_tokenize_asset(bridge_registry_t *reg,
                                chain_id_t target_chain,
                                contract_lang_t lang,
                                surplus_real_t asset_value,
                                uint64_t issuer_addr,
                                token_standard_t token_type,
                                const char *symbol);

/* Redeem tokenized asset (Web3 → Web2) */
int32_t bridge_redeem_asset(bridge_registry_t *reg,
                             uint32_t bridge_id,
                             uint64_t redeemer_addr);

/* Coverage verification */
bool bridge_verify_coverage(bridge_registry_t *reg, uint32_t bridge_id);

/* Chain info */
const char *chain_name(chain_id_t c);
const char *chain_symbol(chain_id_t c);
const char *contract_lang_name(contract_lang_t l);
const char *token_standard_name(token_standard_t t);
const char *bridge_direction_name(bridge_direction_t d);

/* Get chain native token decimals */
uint32_t chain_decimals(chain_id_t c);

#endif /* CRYPTO_BRIDGE_H */
