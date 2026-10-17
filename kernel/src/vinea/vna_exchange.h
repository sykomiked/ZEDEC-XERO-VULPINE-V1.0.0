/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_exchange.h — the stateless 168-byte UBH-168 Epistemic Exchange order
 * book, 3-leaf streaming spot audit, and Social/Intellectual octane standing
 * gate across the 8-node Vinea clearinghouse.
 *
 * SEPARATION OF CLEARINGHOUSE AND WAREHOUSE
 *   The 8 regional armada nodes store only the 168-byte UBH-168 order book
 *   listings, 64-state hexagram tags (lpx_t), and bilateral settlement
 *   receipts — never the multi-hundred-megabyte .car payloads peers trade.
 *   Even 1,000,000 listings occupy 168 MB, while bulk data streams directly
 *   peer-to-peer from the provider's endpoint.
 *
 * UBH-168 EXCHANGE ENVELOPE (8 frames x 21 octets = 168 bytes exact)
 *   frame 0 (0..20)     UBH_FRAME_CONTENT_OBJECT header:
 *                       source_format_id = UBH_FMT_UBH_168,
 *                       target_type = VNA_EXCH_T_LISTING (1) | RECEIPT (2),
 *                       flags = 0x05 (has_payload | canonical),
 *                       payload_length = 126 (6 frames, zero padding),
 *                       schema_id = 0x5A585658 ("ZXVX"),
 *                       integrity_ref = root_digest[0..2]
 *   frames 1..6 (21..146, 126 bytes):
 *     +0   root_digest[32]      CIDv1 sha2-256 multihash of the .car root
 *     +32  provider[32]         seller / provider vna_id_t (ML-DSA-87 NodeID)
 *     +64  witness[32]          LISTING: SHA-256 of the 3 spot-audited leaves;
 *                               RECEIPT: buyer vna_id_t co-signing delivery
 *     +96  file_size (u64 BE)   verified subtree byte length (ipfsn_cat inv 3)
 *     +104 n_chunks  (u32 BE)   total 256 KiB UnixFS leaf blocks
 *     +108 spot_idx[3] (u16 BE) indices of the 3 spot-audited leaves
 *     +114 addr[6]              provider IPv4 (4B) + UDP port (2B BE)
 *     +120 form      (u8)       swarm_cap_t (INTELLECTUAL / MANUFACTURED / SYSTEM)
 *     +121 octane    (u8)       PLAIN 0, REGULAR 4, MIDGRADE 9, PREMIUM 14, RACING 23
 *     +122 lpx_tag   (u8)       64-state hexagram (base | polarity | phase)
 *     +123 armada    (u8)       8-bit bitmask of attesting regional seed nodes
 *     +124 domain    (u16 BE)   epistemic shard / bounty domain (0..7)
 *   frame 7 (147..167)  UBH_FRAME_INTEGRITY trailer:
 *                       payload_length = 126, schema_id = 6,
 *                       integrity_ref = SHA-256(bytes 0..146)[0..2]
 */
#ifndef VNA_EXCHANGE_H
#define VNA_EXCHANGE_H

#include <stdbool.h>
#include <stdint.h>
#include "vna_econ.h"
#include "../lpres/lpres.h"
#include "../lpres/lpres_hex.h"
#include "../swarm/swarm_denom.h"
#include "../ubh/ubh.h"

#define VNA_EXCH_FRAME_BYTES   21u
#define VNA_EXCH_PAYLOAD_BYTES 126u /* 6 * 21 octets: zero padding */
#define VNA_EXCH_ENV_BYTES     168u /* 8 * 21 octets = 168 bytes exact */
#define VNA_EXCH_SCHEMA        0x5A585658u
#define VNA_EXCH_LEAF_MAX      262144u /* 256 KiB UnixFS leaf ceiling */
#define VNA_EXCH_BOOK_CAP      64u
#define VNA_EXCH_QLOG_CAP      16u

typedef enum {
    VNA_EXCH_T_LISTING = 1,
    VNA_EXCH_T_RECEIPT = 2
} vna_exch_type_t;

typedef struct {
    uint16_t kind;           /* vna_exch_type_t */
    uint8_t root_digest[32]; /* CIDv1 sha2-256 */
    vna_id_t provider;
    uint8_t witness[32];     /* spot leaf digest (listing) or buyer id (receipt) */
    uint64_t file_size;
    uint32_t n_chunks;
    uint16_t spot_idx[3];
    uint8_t addr[6];         /* provider IPv4 + UDP port */
    uint8_t form;            /* swarm_cap_t */
    uint8_t octane;          /* SWARM_OCT_* */
    lpx_t lpx_tag;           /* 64-state hexagram */
    uint8_t armada;          /* 8-bit regional attestation mask */
    uint16_t domain;         /* shard 0..7 */
} vna_exch_rec_t;

typedef struct {
    bool used;
    uint8_t env[VNA_EXCH_ENV_BYTES]; /* canonical 168-byte UBH-168 record */
    vna_exch_rec_t rec;
    uint32_t pos_ev, neg_ev;
} vna_exch_slot_t;

typedef struct {
    bool used;
    vna_id_t peer;
    uint8_t octane;          /* SWARM_OCTANE_PLAIN .. SWARM_OCTANE_RACING */
    uint8_t curriculum_pin;  /* 1 once verified pinning zxv-curriculum.car */
    uint32_t listings_ok;
    uint32_t settlements_ok;
} vna_exch_standing_t;

typedef struct {
    vna_book_t *book;
    uint8_t curriculum_digest[32]; /* expected sha2-256 of zxv-curriculum.car root */
    uint8_t local_shard;           /* 0..7 */
    vna_exch_slot_t slots[VNA_EXCH_BOOK_CAP];
    vna_exch_standing_t peers[VNA_EXCH_BOOK_CAP];
    lpx_qlog_t qlog;
    lpx_qrec_t qrec[VNA_EXCH_QLOG_CAP];
    uint32_t listed, audited_ok, quarantined, settled, upgraded;
} vna_exchange_t;

/* Pack/unpack a 168-byte UBH-168 Exchange Envelope. */
vna_status_t vna_exch_pack(const vna_exch_rec_t *r, uint8_t out[VNA_EXCH_ENV_BYTES]);
vna_status_t vna_exch_unpack(const uint8_t in[VNA_EXCH_ENV_BYTES], vna_exch_rec_t *out);

/* Deterministically pick the 3 spot-audit leaf indices in [0, n_chunks) from
 * the root digest and the node's shard id. */
void vna_exch_pick_leaves(const uint8_t root_digest[32], uint8_t shard, uint32_t n_chunks,
                          uint16_t out_idx[3]);

/* Initialize the exchange clearinghouse bound to `book`. */
void vna_exch_init(vna_exchange_t *x, vna_book_t *book, const uint8_t curriculum_digest[32],
                   uint8_t local_shard);

/* Query a peer's current octane grade (SWARM_OCTANE_PLAIN = 0 if unknown). */
uint8_t vna_exch_peer_octane(const vna_exchange_t *x, const vna_id_t *peer);

/* Ring 1 Reciprocity Gate: verify that `peer` announced a valid public
 * provider record for `curriculum_digest`. Awards Social Crown standing and
 * upgrades the peer from PLAIN (0) to MIDGRADE (9). */
vna_status_t vna_exch_verify_curriculum_pin(vna_exchange_t *x, const vna_id_t *peer,
                                            const uint8_t announced_digest[32],
                                            const vna_provider_t *pv);

/* Ring 2 Stateless Spot Audit: verify 3 challenged leaf chunks in bounded
 * memory without storing the file. Advances the listing's lpx_t tag through
 * POTENTIAL -> EMERGING -> MANIFEST on pass (or FORBIDDEN + quarantine log on
 * hash/bounds mismatch), stores the 168-byte UBH-168 listing, and upgrades the
 * provider to PREMIUM (14) or RACING (23). */
vna_status_t vna_exch_spot_audit(vna_exchange_t *x, vna_exch_rec_t *offer,
                                 const uint8_t *const leaf_data[3], const uint32_t leaf_len[3],
                                 const uint8_t want_leaf_sha[3][32]);

/* Multi-node attestation join (`lpres_kjoin`) and P2P delivery receipt
 * settlement. Both observations are joined in lpres: TRUE|TRUE credits the
 * provider's tradeable capital and Social Crown standing; a contradiction or
 * corruption (FALSE/BOTH) updates the listing's lpx_t tag and quarantines on
 * invariant failure. */
vna_status_t vna_exch_settle_receipt(vna_exchange_t *x, const uint8_t root_digest[32],
                                     const vna_id_t *buyer, lpres_state_t seller_obs,
                                     lpres_state_t buyer_obs, uint64_t units);

/* Lookup a verified listing by its root digest; writes the 168-byte UBH-168
 * routing ticket if caller_octane >= required_octane. */
vna_status_t vna_exch_match(const vna_exchange_t *x, const vna_id_t *caller,
                            const uint8_t root_digest[32], uint8_t out_env[VNA_EXCH_ENV_BYTES]);

#endif /* VNA_EXCHANGE_H */
