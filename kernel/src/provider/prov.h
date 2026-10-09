/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov.h — the provider plug-in layer: how anyone with compute, storage,
 * memory, bandwidth, hosted models, fine-tuning or datasets (a hyperscaler, a
 * university cluster, a decentralised GPU or storage network, one person's
 * workstation) lists services on the ZXV network, gets matched to demand,
 * meters what was delivered, and gets paid.
 *
 * WHY A LARGE PROVIDER WOULD PLUG IN (design goals, each enforced below)
 *   G1  Keep your own stack. The provider keeps its endpoint, API, keys,
 *       terms of service and prices. The adapter (prov_adapter.h) speaks the
 *       provider's declared request shape; nothing is rewritten on its side.
 *   G2  No resale without you. Every listing (ask) is signed by the provider
 *       key that signed the descriptor. The network routes and settles; it
 *       never lists or resells a provider's service on its own.
 *   G3  You set the price. Asks carry the provider's own unit price; matching
 *       is price-time priority and the trade executes at the ASK price.
 *   G4  A small, flat, published fee. Default: the platform phi-percent rate
 *       (pay_tithe_phi, floor(a * phi / 100) = 1.618...%) on settled gross,
 *       charged once. An operator may publish a flat basis-point rate instead
 *       but never above PROV_FEE_MAX_BPS (5%). No listing, exit or data fees.
 *       The fee schedule's hash is in every receipt, so it cannot change
 *       silently.
 *   G5  No lock-in. A provider may leave at any time (prov_provider_leave):
 *       its asks are withdrawn, users' unstarted reservations are released in
 *       full, and its co-signed receipts are portable signed objects that any
 *       node can re-verify to rebuild its reputation (prov_rep_rebuild).
 *   G6  Verifiable metering. Both sides co-sign every usage receipt with
 *       ML-DSA-65 (FIPS 204) through the verify hook (prov_pq.c wires it).
 *       Disputes resolve against receipts only; a half-signed receipt moves
 *       no money.
 *   G7  Cooperative anti-monopoly cap, not a ban. Per market and cycle a
 *       provider may fill at most
 *           cap = max(floor(D * cap_q32 / 2^32), ceil(D / P))
 *       units, where D is the cycle's demand in that market (units already
 *       matched this cycle plus open bids that some eligible ask within
 *       their ceiling could serve) and P the number of providers with live
 *       asks or fills there this cycle. Default cap_q32 =
 *       PROV_CAP_INV_PHI2_Q32 (1/phi^2 = 38.2%). This is the same
 *       max(cap share, equal share) rule the swarm commons uses
 *       (docs/SWARM_ECONOMY.md section 6): with a single provider, it may
 *       fill everything; with competitors, excess demand flows to the next
 *       ask in price-time order.
 *   G8  Reputation from receipts. Laplace-smoothed SLA success over
 *       co-signed receipts (neutral prior, so new providers are not buried),
 *       with configurable Sybil resistance: counterparties must hold a stake
 *       and/or an attested identity, self-dealing is refused, and each user
 *       counts at most rep_max_per_user receipts per provider per cycle.
 *
 * WHY IT IS GOOD FOR THE USER
 *   U1  Per-unit price ceiling and a total budget cap per job; a fill never
 *       exceeds either.
 *   U2  Filters: data-residency regions, jurisdictions, licence (commercial
 *       use, SPDX allowlist), attested hardware only, minimum SLA, minimum
 *       reputation.
 *   U3  Privacy flags (no-training-on-my-data, no-retention) travel in the
 *       request envelope, are routed only to providers whose descriptor
 *       declares it honours them, and are recorded in the signed receipt.
 *   U4  No usury. There is no interest, no time-based charge on balances and
 *       no late fee anywhere; the only charge kinds are usage and the network
 *       fee. prov_charge_check refuses anything else (and defers to
 *       pay_usury_check for repayment schedules).
 *   U5  SLA breach returns the provider's own declared service credit to the
 *       user out of the gross, before the fee.
 *
 * WEB 3. A decentralised compute / storage / pinning / inference network
 * lists through the same descriptor (network = PROV_NET_WEB3, with its chain
 * family and payout address). Settlement assets may be chain assets mapped to
 * VFV or ISO 4217 at a posted rate (prov_chain.h), and receipts can be
 * anchored on any chain as a 32-byte commitment.
 *
 * HONEST LIMITS. This module encodes, matches, meters and settles. It opens
 * no network connection: request delivery and chain submission are host
 * hooks. Attestation evidence (TEE / confidential-compute reports) is carried
 * as an opaque blob and is VERIFIED only by the operator's attest hook;
 * without one it stays PROV_ATT_UNVERIFIED and never satisfies
 * require_attested. Reputation measures receipts, not quality. Nothing here
 * implies any partnership, endorsement or connectivity with any company; the
 * descriptor describes how any provider could participate. Consumer, data
 * protection, export-control, tax and financial-services law are for the
 * operator and counsel. Freestanding C11: no libc, no allocation, no float,
 * no 64-bit division (zt_udiv64 / pay_muldiv), no __int128.
 */
#ifndef ZXV_PROV_H
#define ZXV_PROV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ===== Capacities ===== */
#define PROV_MAX_PROVIDERS  32u
#define PROV_MAX_USERS      64u
#define PROV_MAX_OFFERS     8u /* per provider */
#define PROV_MAX_ASKS       128u
#define PROV_MAX_BIDS       128u
#define PROV_MAX_FILLS      256u
#define PROV_MAX_ASSETS     16u
#define PROV_MAX_REGIONS    8u
#define PROV_MAX_SLA        4u
#define PROV_MAX_ATTEST     4u
#define PROV_MAX_SPDX_ALLOW 4u
#define PROV_REP_LOG        64u /* receipt digests kept per provider for export */
#define PROV_NAME_MAX       48u
#define PROV_MODEL_MAX      64u
#define PROV_SPDX_MAX       40u
#define PROV_KEYPATH_MAX    48u
#define PROV_CODE_MAX       12u
#define PROV_CHAIN_ID_MAX   32u
#define PROV_ADDR_MAX       96u
#define PROV_HASH_LEN       32u
#define PROV_ID_LEN         32u

/* ML-DSA-65 sizes (FIPS 204); equal to PQ_MLDSA65_* in pqsec/pq_security.h. */
#define PROV_PK_BYTES  1952u
#define PROV_SIG_BYTES 3309u

/* 1/phi^2 = (3 - sqrt 5) / 2 in Q32: floor(2^31 * (3 - sqrt 5)). The test
 * recomputes it with an exact integer square root. */
#define PROV_CAP_INV_PHI2_Q32 1640531526u
#define PROV_Q32_ONE          ((uint64_t) 1 << 32)
#define PROV_Q16_ONE          65536u
#define PROV_FEE_MAX_BPS      500u /* hard ceiling: 5% */
#define PROV_NONE             0u   /* invalid handle; handles are index + 1 */

typedef enum {
    PROV_OK = 0,
    PROV_ERR_ARG = -1,
    PROV_ERR_FULL = -2,
    PROV_ERR_NOT_FOUND = -3,
    PROV_ERR_STATE = -4,
    PROV_ERR_AUTH = -5, /* bad or missing signature / wrong actor     */
    PROV_ERR_HOOK = -6, /* a required hook is missing                 */
    PROV_ERR_OVERFLOW = -7,
    PROV_ERR_POLICY = -8, /* refused by a user filter or network policy */
    PROV_ERR_USURY = -9,  /* interest, late fee or time-based charge    */
    PROV_ERR_DUPLICATE = -10,
    PROV_ERR_SETTLE = -11, /* the settle hook refused; nothing changed   */
    PROV_ERR_SPACE = -12,  /* caller buffer too small                    */
    PROV_ERR_PARSE = -13,
    PROV_ERR_TAMPER = -14, /* receipt body does not match its fill       */
    PROV_ERR_UNSUPPORTED = -15,
    PROV_ERR_BUDGET = -16 /* price ceiling or budget cap               */
} prov_status_t;

/* ===== Resource classes and units ===== */
typedef enum {
    PROV_RC_ACCEL = 0, /* GPU / TPU / NPU / other accelerator compute */
    PROV_RC_CPU = 1,
    PROV_RC_RAM = 2,
    PROV_RC_STORAGE = 3, /* object / block / archival storage           */
    PROV_RC_BANDWIDTH = 4,
    PROV_RC_INFERENCE = 5, /* hosted model inference                      */
    PROV_RC_FINETUNE = 6,
    PROV_RC_DATASET = 7,
    PROV_RC_PINNING = 8, /* content-addressed pinning (IPFS-style)      */
    PROV_RC_COUNT = 9
} prov_rclass_t;

typedef enum {
    PROV_UNIT_TOKEN = 0, /* model tokens (input + output, or as declared) */
    PROV_UNIT_ACCEL_SECOND = 1,
    PROV_UNIT_CORE_SECOND = 2,
    PROV_UNIT_GIB_SECOND = 3, /* RAM residency                         */
    PROV_UNIT_GIB_HOUR = 4,   /* storage / pinning                     */
    PROV_UNIT_GIB = 5,        /* bandwidth transferred                 */
    PROV_UNIT_REQUEST = 6,
    PROV_UNIT_RECORD = 7, /* dataset rows / items                  */
    PROV_UNIT_COUNT = 8
} prov_unit_t;

/* Which side of the world the provider lives on. Both use one descriptor. */
typedef enum { PROV_NET_WEB2 = 0, PROV_NET_WEB3 = 1 } prov_network_t;

/* Chain families (prov_chain.h has the adapters). */
typedef enum {
    PROV_CHAIN_NONE = 0,
    PROV_CHAIN_EVM = 1,       /* secp256k1, keccak-256, RLP, EIP-155/712   */
    PROV_CHAIN_UTXO = 2,      /* Bitcoin family: secp256k1, bech32/bech32m */
    PROV_CHAIN_ED25519 = 3,   /* Solana-style: ed25519, base58            */
    PROV_CHAIN_COSMOS = 4,    /* Tendermint: secp256k1, bech32, protobuf  */
    PROV_CHAIN_SUBSTRATE = 5, /* sr25519: external signer only            */
    PROV_CHAIN_OPAQUE = 6,    /* anything else: external hooks            */
    PROV_CHAIN_COUNT = 7
} prov_chain_family_t;

/* Request shapes the adapter can speak (prov_adapter.h). Names describe
 * common API styles, not any vendor. */
typedef enum {
    PROV_SHAPE_NONE = 0,
    PROV_SHAPE_MESSAGES = 1,   /* {"model","max_tokens","system","messages":[...]} */
    PROV_SHAPE_CHAT = 2,       /* {"model","messages":[{role,content}],"max_tokens"} */
    PROV_SHAPE_COMPLETION = 3, /* {"model","prompt","max_tokens"}                    */
    PROV_SHAPE_EMBEDDINGS = 4, /* {"model","input":[...]}                            */
    PROV_SHAPE_RAW = 5,        /* opaque bytes; the provider defines the format     */
    PROV_SHAPE_COUNT = 6
} prov_shape_t;

/* Attestation kinds. Evidence is opaque; only the attest hook judges it. */
typedef enum {
    PROV_ATTK_NONE = 0,
    PROV_ATTK_TEE_ENCLAVE = 1,  /* process enclave quote               */
    PROV_ATTK_TEE_VM = 2,       /* confidential VM report              */
    PROV_ATTK_ACCEL_CC = 3,     /* accelerator confidential-compute    */
    PROV_ATTK_TPM = 4,          /* measured boot quote                 */
    PROV_ATTK_ORG_IDENTITY = 5, /* signed organisational identity      */
    PROV_ATTK_OTHER = 6
} prov_attest_kind_t;

typedef enum {
    PROV_ATT_EMPTY = 0,
    PROV_ATT_UNVERIFIED = 1, /* evidence present, no hook or not yet checked */
    PROV_ATT_VERIFIED = 2,   /* the hook accepted it                        */
    PROV_ATT_FAILED = 3      /* the hook refused it                         */
} prov_attest_state_t;

typedef struct {
    uint8_t kind;                         /* prov_attest_kind_t  */
    uint8_t state;                        /* prov_attest_state_t */
    uint32_t len;                         /* evidence length     */
    uint8_t evidence_hash[PROV_HASH_LEN]; /* SHA3-256 of the opaque blob */
} prov_attest_t;

/* An SLA tier a provider offers. Service credit is a refund of part of the
 * job's own price on a breach, declared by the provider; never interest. */
typedef struct {
    uint32_t availability_ppm; /* e.g. 999000 = 99.9%                  */
    uint32_t max_latency_ms;   /* first byte / job start, 0 = none      */
    uint16_t credit_bps;       /* refund on breach, <= 10000            */
} prov_sla_t;

/* ISO 3166-1 alpha-2 region ("DE", "US", ...) or a sub-national tag. */
typedef struct {
    char code[8];
} prov_region_t;

/* Model / dataset licence metadata. */
typedef struct {
    char spdx[PROV_SPDX_MAX];          /* SPDX id or "LicenseRef-..."          */
    bool commercial_use;               /* the licence permits commercial use   */
    uint8_t terms_hash[PROV_HASH_LEN]; /* SHA3-256 of the provider's ToS text */
} prov_licence_t;

/* How the adapter talks to the provider's existing endpoint. */
typedef struct {
    uint8_t shape;                         /* prov_shape_t                             */
    char model[PROV_MODEL_MAX];            /* provider's own model name                */
    char usage_in[PROV_KEYPATH_MAX];       /* response path, e.g. "usage.input_tokens" */
    char usage_out[PROV_KEYPATH_MAX];      /* e.g. "usage.output_tokens"               */
    char text_path[PROV_KEYPATH_MAX];      /* e.g. "content.0.text"                    */
    char privacy_header[PROV_KEYPATH_MAX]; /* header the provider reads for no-train, "" = none */
} prov_api_t;

/* One service a provider offers. */
typedef struct {
    uint8_t rclass;                 /* prov_rclass_t */
    uint8_t unit;                   /* prov_unit_t   */
    uint8_t region;                 /* index into prov_desc_t.regions where it runs */
    uint8_t sla;                    /* index into prov_desc_t.sla                   */
    char accel_type[PROV_NAME_MAX]; /* "gpu:<model>", "tpu", "npu", ""   */
    uint32_t accel_count;
    uint64_t accel_mem_bytes;    /* per accelerator               */
    uint64_t capacity_per_cycle; /* units it can serve per cycle */
    prov_licence_t licence;
    prov_api_t api;
} prov_offer_t;

/* The provider descriptor: signed by the provider's ML-DSA-65 key. */
typedef struct {
    char name[PROV_NAME_MAX];
    uint8_t network;                  /* prov_network_t */
    uint8_t chain_family;             /* WEB3: native chain (prov_chain_family_t) */
    char chain_id[PROV_CHAIN_ID_MAX]; /* "1", "cosmoshub-4", genesis tag   */
    char payout_addr[PROV_ADDR_MAX];  /* WEB3: payout address text        */
    char jurisdiction[8];             /* ISO 3166-1 alpha-2 of the legal entity  */
    prov_region_t regions[PROV_MAX_REGIONS];
    uint8_t n_regions;
    prov_sla_t sla[PROV_MAX_SLA];
    uint8_t n_sla;
    prov_offer_t offers[PROV_MAX_OFFERS];
    uint8_t n_offers;
    bool honours_no_train; /* will not train on requests marked no_train */
    bool zero_retention;   /* will not retain request data               */
    uint64_t version;      /* strictly increasing on re-registration     */
} prov_desc_t;

/* ===== Assets a market may quote in ===== */
typedef enum {
    PROV_ASSET_VFV = 0,     /* platform asset, numeric 555 (not ISO 4217) */
    PROV_ASSET_ISO4217 = 1, /* fiat, settled over the existing pay rails  */
    PROV_ASSET_CHAIN = 2    /* a chain asset (prov_chain.h maps it)       */
} prov_asset_kind_t;

typedef struct {
    bool used;
    uint8_t kind;             /* prov_asset_kind_t */
    char code[PROV_CODE_MAX]; /* "VFV", "EUR", "ETH", ... */
    uint16_t numeric;         /* ISO 4217 numeric, 555 for VFV, 0 for chain assets */
    uint8_t minor;            /* decimals of the minor unit                      */
    uint8_t chain_family;
    char chain_id[PROV_CHAIN_ID_MAX];
} prov_asset_t;

/* ===== Hooks ===== */
/* Signature verify: ML-DSA-65 over msg with domain context "zxv-prov". */
typedef bool (*prov_verify_fn)(void *ctx, const uint8_t pk[PROV_PK_BYTES], const uint8_t *msg,
                               uint32_t len, const uint8_t sig[PROV_SIG_BYTES]);
/* Attestation: judge an opaque evidence blob for the provider whose
 * descriptor digest is `subject`. Return true only on real verification. */
typedef bool (*prov_attest_fn)(void *ctx, uint8_t kind, const uint8_t *evidence, uint32_t len,
                               const uint8_t subject[PROV_HASH_LEN]);

/* What the settle hook is asked to do. Amounts in the market's quote asset
 * minor units. Conservation: hold == net + fee + refund for SETTLE. */
typedef enum {
    PROV_SETTLE_HOLD = 0,   /* reserve `hold` from the user into escrow    */
    PROV_SETTLE_FINAL = 1,  /* pay net to provider, fee to commons, refund */
    PROV_SETTLE_RELEASE = 2 /* return the whole hold to the user           */
} prov_settle_kind_t;

typedef struct {
    uint8_t kind; /* prov_settle_kind_t */
    uint16_t asset;
    uint32_t user;     /* user handle     */
    uint32_t provider; /* provider handle */
    uint32_t fill;     /* fill handle     */
    uint64_t hold;
    uint64_t gross;             /* units * unit price                         */
    uint64_t sla_credit;        /* part of gross returned on an SLA breach  */
    uint64_t fee;               /* network fee on (gross - sla_credit)         */
    uint64_t net;               /* to provider = gross - sla_credit - fee      */
    uint64_t refund;            /* to user = hold - gross + sla_credit         */
    uint8_t ref[PROV_HASH_LEN]; /* receipt digest (or fill digest)    */
} prov_settlement_t;

typedef int (*prov_settle_fn)(void *ctx, const prov_settlement_t *s);

/* ===== Configuration ===== */
typedef enum { PROV_FEE_PHI = 0, PROV_FEE_BPS = 1 } prov_fee_mode_t;

typedef enum {
    PROV_SYBIL_NONE = 0,
    PROV_SYBIL_STAKE = 1,    /* counterparty stake >= min_stake        */
    PROV_SYBIL_ATTESTED = 2, /* counterparty identity attested         */
    PROV_SYBIL_EITHER = 3
} prov_sybil_t;

typedef struct {
    uint8_t fee_mode;         /* prov_fee_mode_t                         */
    uint16_t fee_bps;         /* BPS mode only, <= PROV_FEE_MAX_BPS      */
    uint32_t cap_q32;         /* per-provider share cap, Q32 of demand   */
    uint8_t sybil;            /* prov_sybil_t                            */
    uint64_t min_stake;       /* STAKE mode                              */
    uint8_t rep_max_per_user; /* receipts per (provider, user) per cycle */
    prov_verify_fn verify;
    void *verify_ctx;
    prov_attest_fn attest;
    void *attest_ctx;
    prov_settle_fn settle;
    void *settle_ctx;
} prov_config_t;

/* ===== Participants ===== */
typedef struct {
    uint32_t receipts; /* co-signed receipts counted     */
    uint32_t sla_met;
    uint32_t sla_breached;
    uint32_t disputes_lost;
    uint64_t units_served;
} prov_rep_t;

typedef struct {
    bool used;
    bool active;             /* false after prov_provider_leave */
    uint8_t id[PROV_ID_LEN]; /* SHA3-256 of the public key    */
    uint8_t pk[PROV_PK_BYTES];
    uint8_t desc_hash[PROV_HASH_LEN];
    prov_desc_t desc;
    prov_attest_t attest[PROV_MAX_ATTEST];
    prov_rep_t rep;
    uint8_t rep_log[PROV_REP_LOG][PROV_HASH_LEN];
    uint32_t rep_log_n; /* total ever logged; ring of PROV_REP_LOG */
    uint64_t last_ask_nonce;
} prov_provider_t;

typedef struct {
    bool used;
    uint8_t id[PROV_ID_LEN];
    uint8_t pk[PROV_PK_BYTES];
    uint64_t stake; /* as reported by the operator's staking source */
    bool attested;  /* identity attested by the operator            */
} prov_user_t;

/* ===== Orders ===== */
typedef struct {
    bool used;
    bool live;
    uint32_t provider;   /* handle */
    uint8_t offer;       /* index in the provider's descriptor */
    uint16_t asset;      /* quote asset */
    uint64_t unit_price; /* minor units of the asset per unit */
    uint64_t qty;        /* units available this cycle          */
    uint64_t seq;        /* time priority                       */
    uint64_t nonce;
} prov_ask_t;

/* A user's job request. Everything here protects the user. */
typedef struct {
    uint8_t rclass;
    uint8_t unit;
    uint16_t asset;
    uint64_t qty;            /* units wanted                          */
    uint64_t max_unit_price; /* U1 ceiling                             */
    uint64_t max_total;      /* U1 budget cap, 0 = qty * ceiling       */
    bool allow_split;        /* may be filled by several providers     */
    /* U2 filters (empty list = any) */
    prov_region_t regions[PROV_MAX_REGIONS];
    uint8_t n_regions;
    prov_region_t jurisdictions[PROV_MAX_REGIONS];
    uint8_t n_jurisdictions;
    bool require_commercial;
    char spdx_allow[PROV_MAX_SPDX_ALLOW][PROV_SPDX_MAX];
    uint8_t n_spdx;
    bool require_attested;
    uint32_t min_availability_ppm;
    uint32_t min_rep_q16; /* 0 = any                                */
    uint8_t net_mask;     /* bit 0 web2, bit 1 web3; 0 = both       */
    /* U3 privacy */
    bool no_train;
    bool no_retain;
    uint8_t request_hash[PROV_HASH_LEN]; /* SHA3-256 of the envelope   */
} prov_job_t;

typedef struct {
    bool used;
    bool open;
    bool cancelled;
    uint32_t user; /* handle */
    prov_job_t job;
    uint64_t seq;
    uint64_t filled;
    uint64_t spent; /* reserved so far, quote minor units */
} prov_bid_t;

typedef enum {
    PROV_FILL_RESERVED = 0, /* matched, hold placed               */
    PROV_FILL_SETTLED = 1,
    PROV_FILL_RELEASED = 2, /* hold returned (provider left, cancel) */
    PROV_FILL_DISPUTED = 3
} prov_fill_state_t;

typedef struct {
    bool used;
    uint8_t state;
    uint32_t bid, ask, provider, user;
    uint8_t offer;
    uint16_t asset;
    uint64_t qty;        /* reserved units          */
    uint64_t unit_price; /* ask price               */
    uint64_t hold;       /* qty * unit_price        */
    uint64_t cycle;
    bool no_train, no_retain;
    uint8_t request_hash[PROV_HASH_LEN];
    uint8_t desc_hash[PROV_HASH_LEN]; /* provider descriptor at match time */
    uint8_t job_id[PROV_HASH_LEN];    /* digest of the fill terms          */
} prov_fill_t;

/* ===== Usage receipts ===== */
typedef enum { PROV_SLA_MET = 0, PROV_SLA_LATENCY = 1, PROV_SLA_UNAVAILABLE = 2 } prov_sla_out_t;

#define PROV_RF_NO_TRAIN  0x01u
#define PROV_RF_NO_RETAIN 0x02u
#define PROV_RF_ATTESTED  0x04u

typedef struct {
    uint32_t version; /* 1 */
    uint8_t job_id[PROV_HASH_LEN];
    uint8_t provider_id[PROV_ID_LEN];
    uint8_t user_id[PROV_ID_LEN];
    uint8_t desc_hash[PROV_HASH_LEN];
    uint8_t fee_schedule[PROV_HASH_LEN];
    uint8_t request_hash[PROV_HASH_LEN];
    uint8_t response_hash[PROV_HASH_LEN];
    uint8_t rclass, unit, sla_outcome, flags;
    uint16_t asset;
    char asset_code[PROV_CODE_MAX];
    uint64_t units; /* delivered, <= reserved */
    uint64_t unit_price;
    uint64_t gross, sla_credit, fee, net, refund, hold;
    uint32_t latency_ms;
    uint64_t start_tick, end_tick;
    uint8_t sig_provider[PROV_SIG_BYTES];
    uint8_t sig_user[PROV_SIG_BYTES];
    bool has_sig_provider, has_sig_user;
} prov_receipt_t;

/* Canonical receipt body size and the domain separator. */
#define PROV_RECEIPT_BODY 330u

/* ===== The network state (one per node; static storage) ===== */
typedef struct {
    prov_config_t cfg;
    uint8_t fee_schedule[PROV_HASH_LEN];
    prov_asset_t assets[PROV_MAX_ASSETS];
    prov_provider_t prov[PROV_MAX_PROVIDERS];
    prov_user_t users[PROV_MAX_USERS];
    prov_ask_t asks[PROV_MAX_ASKS];
    prov_bid_t bids[PROV_MAX_BIDS];
    prov_fill_t fills[PROV_MAX_FILLS];
    uint8_t rep_pair[PROV_MAX_PROVIDERS][PROV_MAX_USERS]; /* per-cycle counts */
    uint64_t seq;
    uint64_t cycle;
    uint64_t fees_total;
} prov_net_t;

/* ===== Configuration and lifecycle ===== */
void prov_config_default(prov_config_t *c);
/* PROV_ERR_ARG if the config breaks a published rule (fee above the cap,
 * cap_q32 of 0). */
int prov_net_init(prov_net_t *n, const prov_config_t *cfg);
/* Advance the cycle: clears per-cycle Sybil counters and the per-cycle ask
 * quantities stay as posted (providers re-post to change them). */
void prov_next_cycle(prov_net_t *n);

/* ===== Fee (G4) ===== */
/* Network fee on an amount under the current schedule. */
uint64_t prov_fee(const prov_net_t *n, uint64_t amount);
/* SHA3-256 of the published schedule ("zxv-prov-fee-v1", mode, bps). */
void prov_fee_schedule_hash(const prov_config_t *c, uint8_t out[PROV_HASH_LEN]);

/* ===== No usury (U4) ===== */
typedef enum {
    PROV_CHARGE_USAGE = 0,
    PROV_CHARGE_NETWORK_FEE = 1,
    PROV_CHARGE_SLA_CREDIT = 2, /* a refund to the user */
    PROV_CHARGE_INTEREST = 3,   /* always refused        */
    PROV_CHARGE_LATE_FEE = 4,   /* always refused        */
    PROV_CHARGE_HOLDING = 5,    /* time-based balance charge: refused */
    PROV_CHARGE_COUNT = 6
} prov_charge_t;
/* PROV_OK for usage / fee / credit with repaid <= principal and no time
 * basis; PROV_ERR_USURY otherwise. */
int prov_charge_check(uint8_t kind, uint64_t principal, uint64_t repaid, bool time_based);

/* ===== Assets ===== */
int prov_asset_add(prov_net_t *n, uint8_t kind, const char *code, uint16_t numeric, uint8_t minor,
                   uint8_t chain_family, const char *chain_id, uint16_t *out);
const prov_asset_t *prov_asset(const prov_net_t *n, uint16_t asset);

/* ===== Providers ===== */
/* Canonical descriptor digest: SHA3-256("zxv-prov-desc-v1" || encoding). */
void prov_desc_digest(const prov_desc_t *d, const uint8_t pk[PROV_PK_BYTES],
                      uint8_t out[PROV_HASH_LEN]);
/* Register or update (higher version) a provider. `sig` signs the digest. */
int prov_register(prov_net_t *n, const prov_desc_t *d, const uint8_t pk[PROV_PK_BYTES],
                  const uint8_t sig[PROV_SIG_BYTES], uint32_t *handle);
/* Present attestation evidence for slot `slot`; the attest hook decides. */
int prov_attest_present(prov_net_t *n, uint32_t provider, uint8_t slot, uint8_t kind,
                        const uint8_t *evidence, uint32_t len);
bool prov_is_attested(const prov_net_t *n, uint32_t provider);
/* G5: leave at any time. Withdraws asks, releases unsettled holds in full,
 * keeps the record so receipts still verify. No exit fee exists. */
int prov_provider_leave(prov_net_t *n, uint32_t provider);
const prov_provider_t *prov_provider(const prov_net_t *n, uint32_t provider);

/* ===== Users ===== */
int prov_user_add(prov_net_t *n, const uint8_t pk[PROV_PK_BYTES], uint64_t stake, bool attested,
                  uint32_t *handle);
int prov_user_set_stake(prov_net_t *n, uint32_t user, uint64_t stake, bool attested);

/* ===== Asks (G2, G3) ===== */
/* Digest a provider signs to list: SHA3-256("zxv-prov-ask-v1" || provider id
 * || offer || asset code || price || qty || nonce). */
void prov_ask_digest(const prov_net_t *n, uint32_t provider, uint8_t offer, uint16_t asset,
                     uint64_t unit_price, uint64_t qty, uint64_t nonce, uint8_t out[PROV_HASH_LEN]);
int prov_ask_post(prov_net_t *n, uint32_t provider, uint8_t offer, uint16_t asset,
                  uint64_t unit_price, uint64_t qty, uint64_t nonce,
                  const uint8_t sig[PROV_SIG_BYTES], uint32_t *ask);
int prov_ask_cancel(prov_net_t *n, uint32_t provider, uint32_t ask);

/* ===== Bids and matching (G7, U1-U3) ===== */
void prov_job_default(prov_job_t *j);
int prov_bid_post(prov_net_t *n, uint32_t user, const prov_job_t *job, uint32_t *bid);
int prov_bid_cancel(prov_net_t *n, uint32_t user, uint32_t bid);
/* True iff ask `a` may serve job `j` (filters only; ignores cap and price). */
bool prov_eligible(const prov_net_t *n, const prov_job_t *j, uint32_t ask);
/* Match every open bid of market (rclass, unit, asset) for the current
 * cycle. Bids in price-time priority (higher ceiling, then earlier); asks in
 * price-time priority (lower price, then earlier); execution at the ask
 * price; per-provider cap per G7. Each fill places a HOLD through the settle
 * hook. Writes the number of fills made to *n_fills (may be NULL). */
int prov_match(prov_net_t *n, uint8_t rclass, uint8_t unit, uint16_t asset, uint32_t *n_fills);
/* The cap in units for a market cycle with demand D and P live providers. */
uint64_t prov_cap_units(uint64_t demand, uint32_t providers, uint32_t cap_q32);
const prov_fill_t *prov_fill(const prov_net_t *n, uint32_t fill);
/* Units provider `p` filled in market (rclass, unit, asset) in this cycle. */
uint64_t prov_filled_units(const prov_net_t *n, uint32_t provider, uint8_t rclass, uint8_t unit,
                           uint16_t asset);

/* ===== Receipts (G6) ===== */
typedef struct {
    uint64_t units;
    uint32_t latency_ms;
    uint8_t sla_outcome; /* prov_sla_out_t */
    uint8_t response_hash[PROV_HASH_LEN];
    uint64_t start_tick, end_tick;
} prov_usage_t;

/* Fill in the receipt for `fill` from metered usage; computes gross,
 * SLA credit, fee, net, refund. units must not exceed the reservation. */
int prov_receipt_build(const prov_net_t *n, uint32_t fill, const prov_usage_t *u,
                       prov_receipt_t *r);
/* Canonical body (PROV_RECEIPT_BODY bytes) and digest
 * SHA3-256("zxv-prov-receipt-v1" || body). */
int32_t prov_receipt_encode(const prov_receipt_t *r, uint8_t *out, uint32_t cap);
int prov_receipt_decode(const uint8_t *in, uint32_t len, prov_receipt_t *r);
void prov_receipt_digest(const prov_receipt_t *r, uint8_t out[PROV_HASH_LEN]);
/* Check both signatures against the registered keys, and the arithmetic
 * (gross == units * price, fee == prov_fee, conservation). */
int prov_receipt_verify(const prov_net_t *n, const prov_receipt_t *r);
/* Settle a fully co-signed receipt for `fill`: the body must match the fill
 * (job id, parties, price, asset, flags, units <= reserved). Calls the settle
 * hook with FINAL, updates reputation (G8). Idempotent: a second call is
 * PROV_ERR_DUPLICATE. A receipt missing either signature opens a dispute
 * (fill becomes DISPUTED) and moves no money. */
int prov_settle(prov_net_t *n, uint32_t fill, const prov_receipt_t *r);
/* Resolve a dispute only with a receipt both parties signed (for example an
 * amended one). Same checks as prov_settle. `provider_at_fault` records a
 * lost dispute against the provider's reputation. */
int prov_dispute_resolve(prov_net_t *n, uint32_t fill, const prov_receipt_t *r,
                         bool provider_at_fault);

/* ===== Reputation (G8) ===== */
/* Laplace-smoothed: (met + 1) * 2^16 / (met + breached + 2 * lost + 2). */
uint32_t prov_rep_score_q16(const prov_rep_t *r);
/* G5 export: copy up to `max` most recent receipt digests. Returns count. */
uint32_t prov_rep_export(const prov_net_t *n, uint32_t provider, uint8_t (*out)[PROV_HASH_LEN],
                         uint32_t max);
/* Rebuild a reputation on any node from portable receipts: only receipts
 * whose provider_id is `provider_id` and whose two signatures verify against
 * the given keys count. Returns how many counted. */
uint32_t prov_rep_rebuild(prov_verify_fn verify, void *ctx, const uint8_t provider_id[PROV_ID_LEN],
                          const uint8_t provider_pk[PROV_PK_BYTES], const prov_receipt_t *rs,
                          const uint8_t (*user_pks)[PROV_PK_BYTES], uint32_t count,
                          prov_rep_t *out);

/* ===== Small shared helpers (prov_util.c) ===== */
void prov_memset(void *p, uint8_t v, size_t n);
void prov_memcpy(void *d, const void *s, size_t n);
bool prov_memeq(const void *a, const void *b, size_t n);
bool prov_ct_eq(const void *a, const void *b, size_t n);
size_t prov_strnlen(const char *s, size_t max);
bool prov_streq(const char *a, const char *b);
bool prov_strlcpy(char *dst, const char *src, size_t cap);
void prov_le32_put(uint8_t *p, uint32_t v);
void prov_le64_put(uint8_t *p, uint64_t v);
uint32_t prov_le32_get(const uint8_t *p);
uint64_t prov_le64_get(const uint8_t *p);
/* Checked a * b. */
bool prov_mul_ok(uint64_t a, uint64_t b, uint64_t *out);
/* floor(a * b / c) exactly (128-bit intermediate); false on c == 0 or overflow. */
bool prov_muldiv(uint64_t a, uint64_t b, uint64_t c, uint64_t *out);
void prov_sha3(const uint8_t *p, size_t n, uint8_t out[PROV_HASH_LEN]);

/* Hash builder: bounded buffer, then one SHA3-256. */
#define PROV_HB_CAP 8192u
typedef struct {
    uint8_t buf[PROV_HB_CAP];
    uint32_t len;
    bool err;
} prov_hb_t;
void prov_hb_init(prov_hb_t *h, const char *domain);
void prov_hb_put(prov_hb_t *h, const void *p, uint32_t n);
void prov_hb_u8(prov_hb_t *h, uint8_t v);
void prov_hb_u32(prov_hb_t *h, uint32_t v);
void prov_hb_u64(prov_hb_t *h, uint64_t v);
void prov_hb_str(prov_hb_t *h, const char *s, uint32_t max); /* le32 length || bytes */
bool prov_hb_final(prov_hb_t *h, uint8_t out[PROV_HASH_LEN]);

#endif /* ZXV_PROV_H */
