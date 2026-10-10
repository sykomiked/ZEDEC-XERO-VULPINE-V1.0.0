/* crypto_bridge.c — Web2-Web3 Bidirectional Cryptocurrency Bridge
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "crypto_bridge.h"

static const char *chain_names[] = {
    "Bitcoin", "Ethereum", "BSC", "Polygon", "Solana", "Avalanche",
    "Cardano", "Polkadot", "Cosmos", "Algorand", "Near", "Aptos",
    "Sui", "Stellar", "Ripple", "Litecoin", "Dogecoin", "Toncoin",
    "Tron", "Arbitrum", "Optimism", "Base", "zkSync", "Linea",
    "Scroll", "Mantle", "Blast", "Fantom", "Hedera", "Flow",
    "Tezos", "Filecoin", "Chainlink", "UniSat", "Stacks"
};

static const char *chain_syms[] = {
    "BTC", "ETH", "BNB", "MATIC", "SOL", "AVAX",
    "ADA", "DOT", "ATOM", "ALGO", "NEAR", "APT",
    "SUI", "XLM", "XRP", "LTC", "DOGE", "TON",
    "TRX", "ARB", "OP", "BASE", "ZKS", "LNA",
    "SCR", "MNT", "BLST", "FTM", "HBAR", "FLOW",
    "XTZ", "FIL", "LINK", "UNISAT", "STX"
};

static const char *lang_names[] = {
    "Solidity", "Vyper", "Rust", "Move", "Cairo",
    "Plutus", "Michelson", "Clarity", "Wasm", "Go", "Leo"
};

static const char *token_std_names[] = {
    "Fungible", "Non-Fungible", "Semi-Fungible", "Sovereign", "Voucher"
};

static const char *bridge_dir_names[] = {
    "Web2→Web3", "Web3→Web2", "Cross-Chain"
};

static const uint32_t chain_decs[] = {
    8, 18, 18, 18, 9, 18,
    6, 10, 6, 6, 24, 8,
    9, 7, 6, 8, 8, 9,
    6, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 8, 8,
    6, 18, 18, 8, 6
};

const char *chain_name(chain_id_t c) {
    if (c < CHAIN_MAX) return chain_names[c];
    return "Unknown";
}

const char *chain_symbol(chain_id_t c) {
    if (c < CHAIN_MAX) return chain_syms[c];
    return "???";
}

const char *contract_lang_name(contract_lang_t l) {
    if (l < LANG_MAX) return lang_names[l];
    return "Unknown";
}

const char *token_standard_name(token_standard_t t) {
    if (t < TOKEN_MAX) return token_std_names[t];
    return "Unknown";
}

const char *bridge_direction_name(bridge_direction_t d) {
    if (d <= BRIDGE_CROSS_CHAIN) return bridge_dir_names[d];
    return "Unknown";
}

uint32_t chain_decimals(chain_id_t c) {
    if (c < CHAIN_MAX) return chain_decs[c];
    return 18;
}

void bridge_registry_init(bridge_registry_t *reg) {
    reg->num_bridges = 0;
    reg->total_volume_bridged = SR_ZERO;
    reg->total_bridges_completed = 0;
    reg->total_fees = SR_ZERO;
    uint32_t i;
    for (i = 0; i < CHAIN_MAX; i++) {
        reg->chain_active[i] = true;
        reg->chain_coverage[i] = SR_ONE;
        reg->chain_tx_count[i] = 0;
    }
}

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
                        const char *contract_addr) {
    if (reg->num_bridges >= 512) return 0xFFFFFFFF;
    /* The enums index fixed tables (chain_active[], chain_tx_count[], the
     * name tables): reject out-of-range values from callers. */
    if ((uint32_t) source >= CHAIN_MAX || (uint32_t) dest >= CHAIN_MAX) return 0xFFFFFFFF;
    if ((uint32_t) dir > BRIDGE_CROSS_CHAIN || (uint32_t) token_type >= TOKEN_MAX ||
        (uint32_t) lang >= LANG_MAX)
        return 0xFFFFFFFF;
    if (SR_CMP(amount, SR_ZERO) <= 0) return 0xFFFFFFFF;
    bridge_device_t *b = &reg->bridges[reg->num_bridges];
    
    b->bridge_id = reg->num_bridges;
    b->source_chain = source;
    b->dest_chain = dest;
    b->direction = dir;
    
    /* Registers */
    b->reg_amount = amount;
    b->reg_source_addr = source_addr;
    b->reg_dest_addr = dest_addr;
    b->reg_confirmations = 0;
    b->reg_bridge_fee = SR_MUL(amount, SR_FROM_FLOAT(0.001)); /* 0.1% */
    b->reg_gas_price = SR_FROM_FLOAT(0.0001);
    
    /* M⁵ */
    b->m5.omega = reg->num_bridges;
    b->m5.r = amount;
    b->m5.ell = SR_ONE;
    b->m5.phi = SR_ZERO;
    b->m5.chi = 0;
    
    surplus_real_t product = SR_MUL(amount, SR_ONE);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    b->coverage_ratio = SR_DIV(product, floor);
    
    /* DMA */
    b->dma_head = 0;
    b->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 8; i++) b->dma_proofs[i] = 0;
    
    /* IRQs */
    b->irq_bridge_requested = true;
    b->irq_source_confirmed = false;
    b->irq_dest_confirmed = false;
    b->irq_coverage_breach = (SR_CMP(b->coverage_ratio, SR_ONE) < 0);
    b->irq_reorg_detected = false;
    
    /* Token info */
    b->token_type = token_type;
    b->contract_lang = lang;
    b->token_id = 0;
    int j;
    for (j = 0; j < 15 && token_symbol && token_symbol[j]; j++)
        b->token_symbol[j] = token_symbol[j];
    b->token_symbol[j] = 0;
    for (j = 0; j < 41 && contract_addr && contract_addr[j]; j++)
        b->contract_addr[j] = contract_addr[j];
    b->contract_addr[j] = 0;
    
    b->completed = false;
    b->source_tx_hash = 0;
    b->dest_tx_hash = 0;
    
    return reg->num_bridges++;
}

int32_t bridge_execute(bridge_registry_t *reg, uint32_t bridge_id) {
    if (bridge_id >= reg->num_bridges) return -1;
    bridge_device_t *b = &reg->bridges[bridge_id];

    /* Execute once: a second call would count the volume and fees twice. */
    if (!b->irq_bridge_requested) return -1;

    /* Coverage check */
    if (!bridge_verify_coverage(reg, bridge_id)) {
        b->irq_coverage_breach = true;
        return -1;
    }
    
    /* Check chain active */
    if (!reg->chain_active[b->source_chain] || !reg->chain_active[b->dest_chain]) {
        return -1;
    }
    
    /* Generate source tx hash (simplified) */
    b->source_tx_hash = (uint32_t)(b->bridge_id * 7919 + b->reg_source_addr);
    /* Source confirmation is bridge_confirm_source's job (it enforces the
     * per-chain confirmation depth); setting it here let bridge_confirm_dest
     * complete a bridge that had zero confirmations. */
    b->irq_bridge_requested = false;

    /* Update chain tx counts */
    reg->chain_tx_count[b->source_chain]++;
    reg->chain_tx_count[b->dest_chain]++;
    
    /* Update volume */
    reg->total_volume_bridged = SR_ADD(reg->total_volume_bridged, b->reg_amount);
    reg->total_fees = SR_ADD(reg->total_fees, b->reg_bridge_fee);
    
    return 0;
}

int32_t bridge_confirm_source(bridge_registry_t *reg, uint32_t bridge_id,
                               uint32_t tx_hash, uint32_t confirmations) {
    if (bridge_id >= reg->num_bridges) return -1;
    bridge_device_t *b = &reg->bridges[bridge_id];
    
    if (b->source_tx_hash != tx_hash) return -1;
    
    b->reg_confirmations = confirmations;
    
    /* Require minimum confirmations */
    uint32_t min_conf = 3;
    if (b->source_chain == CHAIN_BITCOIN) min_conf = 6;
    if (b->source_chain == CHAIN_ETHEREUM) min_conf = 12;
    
    if (confirmations >= min_conf) {
        b->irq_source_confirmed = true;
        /* Push proof to DMA */
        b->dma_proofs[b->dma_tail] = tx_hash;
        b->dma_tail = (b->dma_tail + 1) % 8;
    }
    
    return 0;
}

int32_t bridge_confirm_dest(bridge_registry_t *reg, uint32_t bridge_id,
                             uint32_t tx_hash) {
    if (bridge_id >= reg->num_bridges) return -1;
    bridge_device_t *b = &reg->bridges[bridge_id];
    
    if (!b->irq_source_confirmed) return -1;
    
    b->dest_tx_hash = tx_hash;
    b->irq_dest_confirmed = true;
    b->completed = true;
    
    reg->total_bridges_completed++;
    
    return 0;
}

int32_t bridge_cross_chain_swap(bridge_registry_t *reg,
                                 chain_id_t from,
                                 chain_id_t to,
                                 surplus_real_t amount,
                                 uint64_t from_addr,
                                 uint64_t to_addr) {
    uint32_t id = bridge_create(reg, from, to, BRIDGE_CROSS_CHAIN,
                                 amount, from_addr, to_addr,
                                 TOKEN_FUNGIBLE, LANG_SOLIDITY,
                                 chain_symbol(from), "");
    if (id == 0xFFFFFFFF) return -1;
    return bridge_execute(reg, id);
}

uint32_t bridge_tokenize_asset(bridge_registry_t *reg,
                                chain_id_t target_chain,
                                contract_lang_t lang,
                                surplus_real_t asset_value,
                                uint64_t issuer_addr,
                                token_standard_t token_type,
                                const char *symbol) {
    return bridge_create(reg, CHAIN_ETHEREUM, target_chain,
                          BRIDGE_WEB2_TO_WEB3, asset_value,
                          issuer_addr, issuer_addr,
                          token_type, lang, symbol, "");
}

int32_t bridge_redeem_asset(bridge_registry_t *reg,
                             uint32_t bridge_id,
                             uint64_t redeemer_addr) {
    if (bridge_id >= reg->num_bridges) return -1;
    bridge_device_t *b = &reg->bridges[bridge_id];
    
    if (!b->completed) return -1;
    
    /* Create reverse bridge */
    uint32_t reverse_id = bridge_create(reg, b->dest_chain, b->source_chain,
                                         BRIDGE_WEB3_TO_WEB2,
                                         b->reg_amount,
                                         b->reg_dest_addr,
                                         redeemer_addr,
                                         b->token_type, b->contract_lang,
                                         b->token_symbol, b->contract_addr);
    if (reverse_id == 0xFFFFFFFF) return -1;
    
    return bridge_execute(reg, reverse_id);
}

bool bridge_verify_coverage(bridge_registry_t *reg, uint32_t bridge_id) {
    if (bridge_id >= reg->num_bridges) return false;
    bridge_device_t *b = &reg->bridges[bridge_id];
    surplus_real_t product = SR_MUL(b->m5.r, b->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    b->coverage_ratio = SR_DIV(product, floor);
    return SR_CMP(product, floor) >= 0;
}
