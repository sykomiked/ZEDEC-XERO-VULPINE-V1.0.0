/* rails.c — Dragon/Phoenix/Thunderbird Payment Rails implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "rails.h"
#include <stddef.h>

static const char *rail_names[] = {"Dragon", "Phoenix", "Thunderbird"};
static const char *net_names[] = {
    "Visa", "MasterCard", "Amex", "UnionPay",
    "JCB", "Discover", "RuPay", "Interac", "EFTPOS"
};
static const char *tx_type_names[] = {
    "Auth", "Capture", "Sale", "Refund", "Void", "Voucher", "Transfer", "Settle"
};
static const char *tx_status_names[] = {
    "Pending", "Approved", "Declined", "Settled", "Reversed"
};

const char *rail_name(rail_type_t r) {
    if (r < RAIL_TYPE_MAX) return rail_names[r];
    return "Unknown";
}

const char *rail_network_name(conventional_network_t n) {
    if (n < NET_MAX) return net_names[n];
    return "Unknown";
}

const char *tx_type_name(tx_type_t t) {
    if (t < TX_MAX) return tx_type_names[t];
    return "Unknown";
}

const char *tx_status_name(tx_status_t s) {
    if (s < TX_MAX_STATUS) return tx_status_names[s];
    return "Unknown";
}

void rail_system_init(rail_system_t *rs) {
    uint32_t i;
    for (i = 0; i < RAIL_TYPE_MAX; i++) {
        rail_processor_init(&rs->rails[i], (rail_type_t)i);
    }
    rs->cross_rail_volume = SR_ZERO;
    rs->total_transactions = 0;
    rs->total_cards = 0;
    rs->system_health = SR_ONE;
}

void rail_processor_init(rail_processor_t *rp, rail_type_t rail) {
    rp->rail = rail;
    rp->reg_status = 0x01;
    rp->reg_volume = SR_ZERO;
    rp->reg_fees = SR_ZERO;
    rp->reg_tx_count = 0;
    rp->reg_decline_count = 0;
    rp->num_cards = 0;
    rp->num_transactions = 0;
    rp->batch_head = 0;
    rp->batch_tail = 0;
    rp->irq_batch_ready = false;
    rp->irq_fraud_spike = false;
    rp->irq_coverage_system_breach = false;
    
    uint32_t i;
    for (i = 0; i < NET_MAX; i++) rp->compat_enabled[i] = true;
    for (i = 0; i < 64; i++) rp->dma_batch[i] = 0;
    
    /* M⁵ system coordinates */
    rp->system_m5.omega = rail;
    rp->system_m5.r = SR_ONE;
    rp->system_m5.ell = SR_ONE;
    rp->system_m5.phi = SR_ZERO;
    rp->system_m5.chi = 0;
    rp->system_coverage = SR_ONE;
}

uint32_t rail_issue_card(rail_processor_t *rp,
                          uint32_t holder_uci_id,
                          uint16_t country_code,
                          uint64_t pan_hash,
                          surplus_real_t limit,
                          capital_type_t backing,
                          conventional_network_t compat,
                          const char *holder_name) {
    if (rp->num_cards >= 512) return 0xFFFFFFFF;
    payment_card_t *card = &rp->cards[rp->num_cards];
    
    card->card_id = rp->num_cards;
    card->rail = rp->rail;
    card->holder_uci_id = holder_uci_id;
    card->country_code = country_code;
    card->reg_pan = pan_hash;
    card->reg_expiry = 0;
    card->reg_cvv_hash = 0;
    card->reg_balance = limit;
    card->reg_limit = limit;
    
    /* M⁵ */
    card->m5.omega = rp->num_cards;
    card->m5.r = limit;
    card->m5.ell = SR_ONE;
    card->m5.phi = SR_ZERO;
    card->m5.chi = 0;
    
    surplus_real_t product = SR_MUL(limit, SR_ONE);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    card->coverage_ratio = SR_DIV(product, floor);
    
    /* DMA */
    card->dma_head = 0;
    card->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 16; i++) card->dma_tx_queue[i] = 0;
    
    /* IRQs */
    card->irq_fraud_alert = false;
    card->irq_limit_exceeded = false;
    card->irq_coverage_breach = (SR_CMP(card->coverage_ratio, SR_ONE) < 0);
    card->irq_settlement_pending = false;
    
    /* Conventional compat */
    card->compat_network = compat;
    card->compat_pan = pan_hash; /* Bridge PAN */
    
    card->is_voucher_card = (backing == CAP_FINANCIAL); /* Can be changed */
    card->backing_capital = backing;
    
    int j;
    for (j = 0; j < 63 && holder_name && holder_name[j]; j++)
        card->holder_name[j] = holder_name[j];
    card->holder_name[j] = 0;
    
    return rp->num_cards++;
}

int32_t rail_process_tx(rail_processor_t *rp,
                         tx_type_t type,
                         uint32_t card_id,
                         uint32_t merchant_id,
                         surplus_real_t amount,
                         const m5_coords_t *m5) {
    if (card_id >= rp->num_cards) return -1;
    if (rp->num_transactions >= 1024) return -1;
    
    payment_card_t *card = &rp->cards[card_id];
    transaction_t *tx = &rp->transactions[rp->num_transactions];
    
    /* Initialize transaction */
    tx->tx_id = rp->num_transactions + 1;
    tx->type = type;
    tx->status = TX_PENDING;
    tx->rail = rp->rail;
    tx->amount = amount;
    tx->merchant_id = merchant_id;
    tx->card_id = card_id;
    tx->timestamp = 0;
    tx->m5 = (m5 != NULL) ? *m5 : card->m5;
    tx->settled = false;
    tx->settlement_amount = SR_ZERO;
    tx->compat_net = card->compat_network;
    
    /* Coverage check */
    surplus_real_t product = SR_MUL(tx->m5.r, tx->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    tx->coverage_ratio = SR_DIV(product, floor);
    
    /* Balance check */
    if (type == TX_SALE || type == TX_AUTH || type == TX_VOUCHER) {
        if (SR_CMP(amount, card->reg_balance) > 0) {
            tx->status = TX_DECLINED;
            card->irq_limit_exceeded = true;
            rp->reg_decline_count++;
            rp->num_transactions++;
            return -1;
        }
        
        /* Coverage verification */
        if (SR_CMP(tx->coverage_ratio, SR_ONE) < 0) {
            card->irq_coverage_breach = true;
            tx->status = TX_DECLINED;
            rp->reg_decline_count++;
            rp->num_transactions++;
            return -1;
        }
        
        /* Approve */
        tx->status = TX_APPROVED;
        card->reg_balance = SR_SUB(card->reg_balance, amount);
        
        /* Push to card DMA */
        card->dma_tx_queue[card->dma_tail] = (uint32_t)tx->tx_id;
        card->dma_tail = (card->dma_tail + 1) % 16;
        
        /* Push to rail batch */
        rp->dma_batch[rp->batch_tail] = (uint32_t)tx->tx_id;
        rp->batch_tail = (rp->batch_tail + 1) % 64;
        
        /* Update volume */
        rp->reg_volume = SR_ADD(rp->reg_volume, amount);
        rp->reg_tx_count++;
        
        /* Fee (0.5% default) */
        surplus_real_t fee = SR_MUL(amount, SR_FROM_FLOAT(0.005));
        rp->reg_fees = SR_ADD(rp->reg_fees, fee);
    }
    
    if (type == TX_REFUND) {
        tx->status = TX_APPROVED;
        card->reg_balance = SR_ADD(card->reg_balance, amount);
        rp->reg_volume = SR_SUB(rp->reg_volume, amount);
    }
    
    /* Generate auth code */
    int j;
    for (j = 0; j < 15; j++) {
        tx->auth_code[j] = 'A' + (char)((tx->tx_id >> j) % 26);
    }
    tx->auth_code[j] = 0;
    
    rp->num_transactions++;
    return 0;
}

int32_t rail_process_voucher_tx(rail_processor_t *rp,
                                 uint32_t card_id,
                                 uint32_t merchant_id,
                                 surplus_real_t voucher_amount) {
    /* Floating voucher payment — no debt, pay-it-forward */
    m5_coords_t m5 = rp->cards[card_id].m5;
    m5.ell = SR_ONE; /* Vouchers have full logical presence */
    m5.phi = SR_ZERO;
    
    return rail_process_tx(rp, TX_VOUCHER, card_id, merchant_id,
                           voucher_amount, &m5);
}

int32_t rail_settle_batch(rail_processor_t *rp) {
    /* Settle all pending transactions in batch */
    while (rp->batch_head != rp->batch_tail) {
        uint32_t tx_idx = rp->dma_batch[rp->batch_head];
        if (tx_idx < rp->num_transactions) {
            transaction_t *tx = &rp->transactions[tx_idx];
            if (tx->status == TX_APPROVED && !tx->settled) {
                tx->status = TX_SETTLED;
                tx->settled = true;
                tx->settlement_amount = tx->amount;
            }
        }
        rp->batch_head = (rp->batch_head + 1) % 64;
    }
    rp->irq_batch_ready = false;
    return 0;
}

int32_t rail_bridge_to_conventional(rail_processor_t *rp,
                                     transaction_t *tx,
                                     conventional_network_t net) {
    if (!rp->compat_enabled[net]) return -1;
    
    /* Bridge: format transaction for conventional network */
    tx->compat_net = net;
    
    /* Conventional networks use different fee structures */
    surplus_real_t conv_fee = SR_ZERO;
    switch (net) {
        case NET_VISA:       conv_fee = SR_FROM_FLOAT(0.015); break;
        case NET_MASTERCARD: conv_fee = SR_FROM_FLOAT(0.015); break;
        case NET_AMEX:       conv_fee = SR_FROM_FLOAT(0.025); break;
        case NET_UNIONPAY:   conv_fee = SR_FROM_FLOAT(0.010); break;
        case NET_JCB:        conv_fee = SR_FROM_FLOAT(0.012); break;
        case NET_DISCOVER:   conv_fee = SR_FROM_FLOAT(0.013); break;
        case NET_RUPAY:      conv_fee = SR_FROM_FLOAT(0.008); break;
        default:             conv_fee = SR_FROM_FLOAT(0.015); break;
    }
    
    surplus_real_t fee = SR_MUL(tx->amount, conv_fee);
    rp->reg_fees = SR_ADD(rp->reg_fees, fee);
    
    return 0;
}

bool rail_verify_coverage(rail_processor_t *rp, uint32_t card_id) {
    if (card_id >= rp->num_cards) return false;
    payment_card_t *card = &rp->cards[card_id];
    surplus_real_t product = SR_MUL(card->m5.r, card->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    card->coverage_ratio = SR_DIV(product, floor);
    return SR_CMP(product, floor) >= 0;
}
