/* vino.h — Vino Decentralized Bank Node: Triple Ledger System
 * ZEDEC pqOS native financial infrastructure.
 *
 * Triple Ledger (in-memory tables in this module):
 *   1. Primary ledger  — transaction history
 *   2. Balance ledger  — current account states
 *   3. Audit ledger    — a hash chain over transactions. vino_hash() is
 *      FNV-1a stretched to 32 bytes: NOT a cryptographic hash, so the chain
 *      detects accidents, not tampering. It is not a regulatory-grade proof.
 *
 * Nine Forms of Capital (this module's own order; see finance/capital_forms.h
 * and zcapital.h for the canonical order):
 *   1. Financial (currency, deposits)
 *   2. Material (physical assets, commodities)
 *   3. Knowledge (IP, patents, data)
 *   4. Social (trust, reputation, network)
 *   5. Cultural (heritage, language, tradition)
 *   6. Spiritual (ethical, moral, purpose)
 *   7. Living (ecosystems, biodiversity)
 *   8. Built (infrastructure, technology)
 *   9. Human (skills, health, education)
 *
 * Interoperability — what actually exists:
 *   - vino_msg_to_iso20022 / _camt053 / _pacs008 / _mt103 write a fixed
 *     opening fragment of the named message (no amounts, parties or closing
 *     tags). They are placeholders for a message builder, not conformant
 *     ISO 20022 or SWIFT messages, and nothing has been certified.
 *   - CIPS, SPFS, Visa, Mastercard, Bitcoin and Ethereum adapters and every
 *     vino_msg_from_* parser return VINO_ENOTIMPL and write nothing.
 *   - There is no networking: vino_vote_block and vino_sync_peers return
 *     VINO_ENOTIMPL; there is no consensus.
 *   The payment_rail_t / msg_standard_t enums are labels only.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef VINO_H
#define VINO_H

#include <stdint.h>
#include <stdbool.h>

#define VINO_MAX_ACCOUNTS    1024
#define VINO_MAX_TXNS        65536
#define VINO_ADDR_LEN        64
#define VINO_NAME_LEN        64
#define VINO_HASH_LEN        32
#define VINO_MAX_ASSETS      512
#define VINO_MAX_PEERS       128
#define VINO_MAX_MESSAGES    256

/* Returned by adapters and consensus calls that do not exist yet. */
#define VINO_ENOTIMPL (-38)

/* Nine forms of capital */
typedef enum {
    CAP_FINANCIAL = 0,
    CAP_MATERIAL   = 1,
    CAP_KNOWLEDGE  = 2,
    CAP_SOCIAL     = 3,
    CAP_CULTURAL   = 4,
    CAP_SPIRITUAL  = 5,
    CAP_LIVING     = 6,
    CAP_BUILT      = 7,
    CAP_HUMAN      = 8,
    CAP_MAX        = 9
} capital_type_t;

/* Backward-compatible aliases for triple_ledger / kernel_main */
#define CAP_PHYSICAL     CAP_MATERIAL
#define CAP_LAND         CAP_LIVING
#define CAP_INTELLECTUAL CAP_KNOWLEDGE
#define CAP_ECOLOGICAL   CAP_BUILT

/* Asset classes */
typedef enum {
    ASSET_CURRENCY    = 0,   /* Fiat: USD, EUR, CNY, etc. */
    ASSET_CRYPTO      = 1,   /* BTC, ETH, etc. */
    ASSET_EQUITY      = 2,   /* Stocks */
    ASSET_BOND        = 3,   /* Government/corporate bonds */
    ASSET_COMMODITY   = 4,   /* Gold, oil, wheat, etc. */
    ASSET_OPTION      = 5,   /* Options */
    ASSET_FUTURE      = 6,   /* Futures contracts */
    ASSET_FOREX       = 7,   /* Forex pairs */
    ASSET_TOKEN       = 8,   /* Utility tokens */
    ASSET_NFT         = 9,   /* Non-fungible */
    ASSET_CBDC        = 10,  /* Central Bank Digital Currency */
    ASSET_STABLECOIN  = 11,  /* USDT, USDC, etc. */
    ASSET_MAX         = 12
} asset_class_t;

/* Transaction types */
typedef enum {
    TXN_TRANSFER    = 0,
    TXN_DEPOSIT     = 1,
    TXN_WITHDRAW    = 2,
    TXN_TRADE       = 3,
    TXN_ISSUE       = 4,
    TXN_REDEEM      = 5,
    TXN_STAKE       = 6,
    TXN_UNSTAKE     = 7,
    TXN_SWAP        = 8,
    TXN_BRIDGE      = 9,   /* Cross-chain/cross-rail */
    TXN_SETTLE      = 10,  /* Settlement */
    TXN_CLEAR       = 11   /* Clearing */
} txn_type_t;

/* Payment rail types */
typedef enum {
    RAIL_VINO_NATIVE  = 0,
    RAIL_SWIFT        = 1,
    RAIL_CIPS         = 2,
    RAIL_SPFS         = 3,
    RAIL_ISO20022     = 4,
    RAIL_VISA         = 5,
    RAIL_MASTERCARD   = 6,
    RAIL_HORMUNG      = 7,
    RAIL_EVC          = 8,
    RAIL_BLOCKCHAIN   = 9,
    RAIL_SEPA         = 10,
    RAIL_FEDWIRE      = 11,
    RAIL_CHIPS        = 12,
    RAIL_RTGS         = 13,
    RAIL_ACH          = 14,
    RAIL_UPI          = 15,  /* India */
    RAIL_PIX          = 16,  /* Brazil */
    RAIL_FAST         = 17,  /* Singapore */
    RAIL_NPP          = 18,  /* Australia */
    RAIL_INTERAC      = 19,  /* Canada */
    RAIL_MAX          = 20
} payment_rail_t;

/* Messaging standards */
typedef enum {
    MSG_VINO_NATIVE  = 0,
    MSG_ISO20022     = 1,
    MSG_CAMT053      = 2,
    MSG_CAMT054      = 3,
    MSG_PACS008      = 4,  /* Customer credit transfer */
    MSG_PACS009      = 5,  /* FI to FI credit transfer */
    MSG_PAIN001      = 6,  /* Customer payment initiation */
    MSG_MT103        = 7,  /* SWIFT MT103 */
    MSG_MT202        = 8,  /* SWIFT MT202 */
    MSG_MX_HEAD      = 9,  /* SWIFT MX header */
    MSG_CIPS_001     = 10, /* CIPS credit transfer */
    MSG_SPFS         = 11, /* SPFS message */
    MSG_MAX          = 12
} msg_standard_t;

typedef struct vino_account {
    char address[VINO_ADDR_LEN];
    char name[VINO_NAME_LEN];
    uint64_t balance[CAP_MAX];  /* Balance per capital type */
    uint32_t asset_balances[ASSET_MAX];  /* Count per asset class */
    uint32_t nonce;
    bool active;
    uint32_t created_tick;
} vino_account_t;

typedef struct vino_transaction {
    uint32_t id;
    txn_type_t type;
    capital_type_t capital;
    asset_class_t asset;
    char from_addr[VINO_ADDR_LEN];
    char to_addr[VINO_ADDR_LEN];
    uint64_t amount;
    uint8_t  hash[VINO_HASH_LEN];
    uint8_t  prev_hash[VINO_HASH_LEN];
    uint32_t timestamp;
    payment_rail_t rail;
    msg_standard_t msg_type;
    char memo[64];
    bool confirmed;
    uint32_t block_height;
} vino_transaction_t;

typedef struct vino_asset {
    char symbol[16];
    char name[VINO_NAME_LEN];
    asset_class_t class;
    uint32_t precision;
    uint64_t total_supply;
    bool active;
} vino_asset_t;

typedef struct vino_peer {
    char address[VINO_ADDR_LEN];
    char endpoint[64];
    uint32_t trust_score;
    bool connected;
    uint32_t last_seen;
} vino_peer_t;

typedef struct vino_message {
    msg_standard_t standard;
    payment_rail_t rail;
    char raw[512];
    uint32_t raw_len;
    uint32_t txn_id;
    bool processed;
} vino_message_t;

typedef struct vino_ledger {
    /* Triple ledger */
    vino_transaction_t primary[VINO_MAX_TXNS];   /* Transaction history */
    vino_account_t     balances[VINO_MAX_ACCOUNTS]; /* Current balances */
    vino_transaction_t audit[VINO_MAX_TXNS];      /* Audit trail */

    uint32_t num_txns;
    uint32_t num_accounts;
    uint32_t num_audit;
    uint32_t block_height;
    uint8_t  chain_head_hash[VINO_HASH_LEN];

    /* Assets */
    vino_asset_t assets[VINO_MAX_ASSETS];
    uint32_t num_assets;

    /* P2P network */
    vino_peer_t peers[VINO_MAX_PEERS];
    uint32_t num_peers;

    /* Message queue */
    vino_message_t messages[VINO_MAX_MESSAGES];
    uint32_t num_messages;

    /* Consensus */
    uint32_t consensus_threshold;
    uint32_t node_id;
    bool is_validator;
    uint32_t validator_stake;

    /* Stats */
    uint64_t total_volume[CAP_MAX];
    uint32_t txn_count;
} vino_ledger_t;

/* Core ledger operations */
void vino_init(vino_ledger_t *v, uint32_t node_id);
int32_t vino_create_account(vino_ledger_t *v, const char *address, const char *name);
int32_t vino_transfer(vino_ledger_t *v, const char *from, const char *to,
                       uint64_t amount, capital_type_t capital,
                       payment_rail_t rail, const char *memo);
int32_t vino_trade(vino_ledger_t *v, const char *from, const char *to,
                    asset_class_t asset, uint64_t amount, uint64_t price,
                    const char *memo);
int32_t vino_issue(vino_ledger_t *v, const char *to, asset_class_t asset,
                    uint64_t amount, const char *memo);
int32_t vino_bridge(vino_ledger_t *v, const char *from, const char *to,
                     uint64_t amount, payment_rail_t src_rail,
                     payment_rail_t dst_rail, const char *memo);
vino_account_t *vino_get_account(vino_ledger_t *v, const char *address);
int32_t vino_get_balance(vino_ledger_t *v, const char *address,
                          capital_type_t capital, uint64_t *out);

/* Asset management */
int32_t vino_register_asset(vino_ledger_t *v, const char *symbol, const char *name,
                             asset_class_t class, uint32_t precision, uint64_t supply);
vino_asset_t *vino_get_asset(vino_ledger_t *v, const char *symbol);

/* P2P / consensus: peer table only; vote/sync return VINO_ENOTIMPL */
int32_t vino_add_peer(vino_ledger_t *v, const char *address, const char *endpoint);
int32_t vino_remove_peer(vino_ledger_t *v, const char *address);
void vino_set_validator(vino_ledger_t *v, bool is_validator, uint32_t stake);
int32_t vino_propose_block(vino_ledger_t *v);
int32_t vino_vote_block(vino_ledger_t *v, uint32_t block_height, bool approve);
int32_t vino_sync_peers(vino_ledger_t *v);

/* Messaging adapters: see the header comment for what is real */
int32_t vino_msg_to_iso20022(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_from_iso20022(const char *xml, vino_transaction_t *txn);
int32_t vino_msg_to_camt053(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_from_camt053(const char *xml, vino_transaction_t *txn);
int32_t vino_msg_to_mt103(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_from_mt103(const char *msg, vino_transaction_t *txn);
int32_t vino_msg_to_pacs008(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_to_cips(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_to_spfs(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_to_visa(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_to_mastercard(const vino_transaction_t *txn, char *out, uint32_t max_out);

/* Blockchain adapters: not implemented (VINO_ENOTIMPL) */
int32_t vino_msg_to_btc(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_to_eth(const vino_transaction_t *txn, char *out, uint32_t max_out);
int32_t vino_msg_from_btc(const char *raw, vino_transaction_t *txn);
int32_t vino_msg_from_eth(const char *raw, vino_transaction_t *txn);

/* Capital type names */
const char *vino_capital_name(capital_type_t c);
const char *vino_asset_class_name(asset_class_t a);
const char *vino_rail_name(payment_rail_t r);
const char *vino_msg_standard_name(msg_standard_t m);

/* Non-cryptographic hash (FNV-1a based); see header comment */
void vino_hash(const void *data, uint32_t len, uint8_t out[VINO_HASH_LEN]);

#endif
