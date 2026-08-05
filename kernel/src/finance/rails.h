/* rails.h — Dragon/Phoenix/Thunderbird Payment Rails
 *
 * Three payment card rails for the VOVINA SHAKINA economic system,
 * analogous to Visa/MasterCard/Amex but built on M⁵ axiomatic principles.
 *
 * Dragon Rail:   Eastern markets, high-value, sovereign-grade
 * Phoenix Rail:  Global South/emerging markets, rebirth/recovery focused
 * Thunderbird:   Western/global, speed-optimized, lightning settlement
 *
 * All three rails support:
 *   - Floating voucher payments (no-debt, pay-it-forward)
 *   - Triple-ledger settlement
 *   - Nine-capital backing
 *   - M⁵ coverage verification
 *   - Conventional card network compatibility (Visa/MC/Amex/UnionPay/JCB/Discover)
 *   - Hardware-as-code: each rail is a virtual payment processor device
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef RAILS_H
#define RAILS_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"
#include "triple_ledger.h"
#include "identity.h"

/* ===== Rail Types ===== */

typedef enum {
    RAIL_DRAGON      = 0,  /* Eastern, sovereign, high-value */
    RAIL_PHOENIX     = 1,  /* Global South, recovery, emerging */
    RAIL_THUNDERBIRD = 2,  /* Western, speed, global */
    RAIL_TYPE_MAX    = 3
} rail_type_t;

/* ===== Conventional Network Compatibility ===== */

typedef enum {
    NET_VISA       = 0,
    NET_MASTERCARD = 1,
    NET_AMEX       = 2,
    NET_UNIONPAY   = 3,
    NET_JCB        = 4,
    NET_DISCOVER   = 5,
    NET_RUPAY      = 6,
    NET_INTERAC    = 7,
    NET_EFTPOS     = 8,
    NET_MAX        = 9
} conventional_network_t;

/* ===== Transaction Types ===== */

typedef enum {
    TX_AUTH        = 0,  /* Authorization only */
    TX_CAPTURE     = 1,  /* Capture authorized funds */
    TX_SALE        = 2,  /* Auth + capture combined */
    TX_REFUND      = 3,
    TX_VOID        = 4,
    TX_VOUCHER     = 5,  /* Floating voucher payment */
    TX_TRANSFER    = 6,  /* P2P transfer */
    TX_SETTLE      = 7,  /* Settlement */
    TX_MAX         = 8
} tx_type_t;

/* ===== Transaction Status ===== */

typedef enum {
    TX_PENDING     = 0,
    TX_APPROVED    = 1,
    TX_DECLINED    = 2,
    TX_SETTLED     = 3,
    TX_REVERSED    = 4,
    TX_MAX_STATUS  = 5
} tx_status_t;

/* ===== Payment Card (hardware-as-code device) ===== */

typedef struct {
    uint32_t card_id;
    rail_type_t rail;
    uint32_t holder_uci_id;      /* Link to identity registry */
    uint16_t country_code;
    
    /* Register map */
    uint64_t reg_pan;             /* Primary Account Number (hashed) */
    uint32_t reg_expiry;
    uint32_t reg_cvv_hash;
    surplus_real_t reg_balance;   /* Available balance */
    surplus_real_t reg_limit;     /* Spending limit */
    
    /* M⁵ coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* DMA buffer for transaction queue */
    uint32_t dma_tx_queue[16];
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQ flags */
    bool irq_fraud_alert;
    bool irq_limit_exceeded;
    bool irq_coverage_breach;
    bool irq_settlement_pending;
    
    /* Conventional network mapping */
    conventional_network_t compat_network;
    uint64_t compat_pan;          /* Conventional PAN for bridge */
    
    /* Card type */
    bool is_voucher_card;         /* Floating voucher backed */
    capital_type_t backing_capital;  /* defined in vino.h */
    
    char holder_name[64];
} payment_card_t;

/* ===== Transaction ===== */

typedef struct {
    uint64_t tx_id;
    tx_type_t type;
    tx_status_t status;
    rail_type_t rail;
    
    surplus_real_t amount;
    surplus_real_t coverage_ratio;
    
    uint32_t merchant_id;
    uint32_t card_id;
    uint32_t timestamp;
    
    /* M⁵ coordinates */
    m5_coords_t m5;
    
    /* Settlement */
    bool settled;
    surplus_real_t settlement_amount;
    uint32_t settlement_tick;
    
    /* Conventional compatibility */
    conventional_network_t compat_net;
    char auth_code[16];
} transaction_t;

/* ===== Rail Processor (hardware-as-code) ===== */

typedef struct {
    rail_type_t rail;
    
    /* Register map */
    uint32_t reg_status;          /* Processor status */
    surplus_real_t reg_volume;    /* Total volume processed */
    surplus_real_t reg_fees;      /* Total fees collected */
    uint32_t reg_tx_count;        /* Transaction count */
    uint32_t reg_decline_count;   /* Decline count */
    
    /* Cards registered on this rail */
    payment_card_t cards[512];
    uint32_t num_cards;
    
    /* Transaction log */
    transaction_t transactions[1024];
    uint32_t num_transactions;
    
    /* DMA settlement batch */
    uint32_t dma_batch[64];
    uint32_t batch_head;
    uint32_t batch_tail;
    
    /* IRQ flags */
    bool irq_batch_ready;
    bool irq_fraud_spike;
    bool irq_coverage_system_breach;
    
    /* Conventional network bridges */
    bool compat_enabled[NET_MAX];
    
    /* M⁵ system coordinates */
    m5_coords_t system_m5;
    surplus_real_t system_coverage;
} rail_processor_t;

/* ===== Rail System (all three rails) ===== */

typedef struct {
    rail_processor_t rails[RAIL_TYPE_MAX];
    
    /* Cross-rail settlement */
    surplus_real_t cross_rail_volume;
    uint32_t total_transactions;
    uint32_t total_cards;
    
    /* System health */
    surplus_real_t system_health;
} rail_system_t;

/* ===== API ===== */

void rail_system_init(rail_system_t *rs);
void rail_processor_init(rail_processor_t *rp, rail_type_t rail);

/* Card management */
uint32_t rail_issue_card(rail_processor_t *rp,
                          uint32_t holder_uci_id,
                          uint16_t country_code,
                          uint64_t pan_hash,
                          surplus_real_t limit,
                          capital_type_t backing,
                          conventional_network_t compat,
                          const char *holder_name);

/* Transaction processing */
int32_t rail_process_tx(rail_processor_t *rp,
                         tx_type_t type,
                         uint32_t card_id,
                         uint32_t merchant_id,
                         surplus_real_t amount,
                         const m5_coords_t *m5);

/* Floating voucher payment */
int32_t rail_process_voucher_tx(rail_processor_t *rp,
                                 uint32_t card_id,
                                 uint32_t merchant_id,
                                 surplus_real_t voucher_amount);

/* Settlement */
int32_t rail_settle_batch(rail_processor_t *rp);

/* Conventional network bridge */
int32_t rail_bridge_to_conventional(rail_processor_t *rp,
                                     transaction_t *tx,
                                     conventional_network_t net);

/* Coverage verification */
bool rail_verify_coverage(rail_processor_t *rp, uint32_t card_id);

/* Rail names */
const char *rail_name(rail_type_t r);
const char *rail_network_name(conventional_network_t n);
const char *tx_type_name(tx_type_t t);
const char *tx_status_name(tx_status_t s);

#endif /* RAILS_H */
