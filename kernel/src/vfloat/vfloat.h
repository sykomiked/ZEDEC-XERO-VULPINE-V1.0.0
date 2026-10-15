/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vfloat.h — Vino floating vouchers: short-lived credits between two peers,
 * counted in capacity actually delivered (sectors moved, checked compute,
 * storage epochs, AIRC bandwidth), netted between the two of them, with no
 * global ledger and no fee. Design: docs/VINO_FLOATING_VOUCHERS.md.
 *
 * A voucher is the receiving node saying, under its own ML-DSA-87 key: "I
 * received N units of this resource from you, for this dataset, under this
 * letter of marque, and you hold credit for them with me until epoch E".
 *
 * ISSUANCE GOES THROUGH THE BOUNTY BOARD. Delivering capacity does not by
 * itself create credit. Every voucher names a letter of marque (the 32-byte
 * id of a letter issued by the Sicilian Royal Navy bounty board, pay_bounty,
 * the main VFV issuance path), and the issuer may sign one only after the
 * deployment's letter hook (vf_letter_fn) confirms that letter is live, is
 * held by the bearer, covers this resource, and has room for these units;
 * the hook takes the units off the letter. No hook: nothing is issued. The
 * bearer's side calls the same hook in check-only mode before accepting.
 * The prize court (vf_prize.h) is that hook: it lets a draw through only
 * for a claim whose prize passed the audit.
 *
 * SOFTWARE ONLY. Every resource is software capacity. A voucher promises no
 * goods, no commodity and no physical delivery, and the jurisdiction router
 * (jroute.h) never routes one: vouchers are not VINO and cannot be burned
 * for anything.
 *
 * It is built from parts that already exist:
 *   - identities, ML-DSA-87 signing and CIDs are Vinea's (vna_id.h,
 *     vna_pq.h, vna_cid.h); NodeID = SHA3-256(public key);
 *   - the per-pair sequence and previous-hash chain are the rule of Vinea's
 *     trade receipts (vna_econ.h, DOUBLE SPENDING);
 *   - the per-pair ceiling plays the part of devmesh's confirmed money
 *     amount: past it, nothing more is issued or accepted until the pair
 *     nets down or someone raises the ceiling on purpose;
 *   - the units are the ones jroute.h delivers into: compute in TPC ticks
 *     (JR_TPC_TICKS_PER_VINO per VINO), storage and transfer in 4096-byte
 *     sectors, AIRC in bytes.
 *
 *   V1 ISSUE. The issuer (the node that received the capacity) signs
 *        "VFLT" || version || resource || 0 0 || issuer id[32] ||
 *        bearer id[32] || scope CID[36] || letter[32] || units u64 ||
 *        pair_seq u64 || prev[32] || issued_epoch u32 || expiry_epoch u32
 *      (LE)
 *      with ML-DSA-87 under the context "vinea/v3/float-voucher". pair_seq
 *      is one more than the last voucher this issuer gave this bearer, and
 *      prev is the SHA3-256 of that voucher's body (zero for the first).
 *      The letter hook must approve first (see ISSUANCE above).
 *      vf_issue_chunk issues for one hydration chunk only after the chunk's
 *      bytes hash to the CID it was asked for (units = whole sectors).
 *   V2 ACCEPT. The bearer accepts only a voucher naming it as bearer, from
 *      a known peer, at exactly the next pair_seq with the matching prev,
 *      not expired, with a valid signature, within the ceiling, and under a
 *      letter the hook recognises for that bearer. Anything
 *      else is refused and changes nothing, with one exception: a voucher
 *      that is signed and next in sequence but whose window has gone
 *      (expired, or issued at or before this node's last seal) takes its
 *      place in the pair chain and evaporates (VF_E_EXPIRED), so the pair
 *      is never stuck behind it. A voucher already accepted is refused
 *      (VF_E_SEQ): it cannot be presented twice.
 *   V3 NET. Both sides hold the same open vouchers, so both compute the same
 *      net: held from the peer minus owed to the peer. vf_offset cancels the
 *      overlap locally, oldest first, the same way on both sides.
 *   V4 EXPIRE. vf_tick at a new epoch drops every voucher whose expiry has
 *      come. Nothing outlives its window.
 *   V5 PRIORITY, BOUNDED. A peer that delivered earns a score (units
 *      accepted, decayed by 13/21 each epoch). Its routing weight is
 *      VF_PRIO_BASE + a boost that saturates below VF_PRIO_BOOST, so the
 *      best peer weighs at most 34/21 of a newcomer and nobody weighs zero.
 *      vf_split shares slots by those weights; with at least two slots per
 *      peer every peer gets at least one.
 *   V6 SEAL. vf_seal (the air-gap seal) evaporates every open voucher and
 *      every score and refuses issue and accept until vf_unseal, which needs
 *      a later epoch. Pair sequences survive, and a voucher issued at or
 *      before the seal epoch evaporates on arrival (V2), so nothing from
 *      before the seal counts after it. The seal is local: a peer that did
 *      not seal still holds its side until expiry.
 *
 * WHAT A VOUCHER IS NOT. It never mints anything: no token supply changes
 * and nothing is created except a record of capacity already delivered. It
 * cannot be traded: the bearer is fixed in the signed bytes and there is no
 * transfer call. It earns nothing: no interest, no return, it only expires.
 * Netting reduces what two peers owe each other; it moves no value to a
 * third party.
 *
 * HONEST LIMITS. A voucher proves the issuer signed it, not that the work
 * was good: compute is only as checked as the AXION sandbox the caller ran,
 * and the caller passes the units in. Delivery and resending are the
 * caller's: a lost voucher blocks the next one in that pair until the same
 * bytes are resent. Two colluding identities can still write vouchers to
 * each other; the ceiling and the bounded priority limit what that buys.
 * No allocation; single-threaded.
 */
#ifndef ZXV_VFLOAT_H
#define ZXV_VFLOAT_H

#include <stdbool.h>
#include <stdint.h>
#include "../vinea/vna_id.h"
#include "../vinea/vna_cid.h"

#define VF_VERSION      1u
#define VF_CTX          "vinea/v3/float-voucher"
#define VF_BODY_LEN     196u
#define VF_VOUCHER_LEN  (VF_BODY_LEN + VNA_SIG_LEN)
#define VF_SECTOR_BYTES 4096u

#define VF_MAX_PEERS   32u
#define VF_MAX_OPEN    256u
#define VF_DEFAULT_CAP (1ull << 32) /* per pair, per resource, per direction */
#define VF_CAP_LIMIT   (1ull << 62) /* no ceiling may be set above this */

#define VF_PRIO_BASE  21u          /* every peer's floor weight */
#define VF_PRIO_BOOST 13u          /* the boost stays below this: max weight < 34 */
#define VF_PRIO_HALF  (1ull << 20) /* score at which half the boost is earned */
#define VF_DECAY_NUM  13u
#define VF_DECAY_DEN  21u

typedef enum {
    VF_RES_TRANSFER = 0, /* 4096-byte sectors moved (hydration) */
    VF_RES_COMPUTE = 1,  /* checked compute, in TPC ticks */
    VF_RES_STORAGE = 2,  /* 4096-byte sectors held for one epoch */
    VF_RES_AIRC = 3,     /* AIRC bandwidth, in bytes */
    VF_RES_COUNT = 4
} vf_res_t;

typedef enum {
    VF_OK = 0,
    VF_E_ARG = -1,
    VF_E_SEALED = -2,
    VF_E_PEER = -3,   /* unknown peer, or the table is full */
    VF_E_DECODE = -4, /* bad magic, version, reserved bytes or resource */
    VF_E_BEARER = -5, /* not addressed to this node */
    VF_E_SEQ = -6,    /* not the next voucher in this pair (or a replay) */
    VF_E_EXPIRED = -7,
    VF_E_SIG = -8,
    VF_E_CAP = -9,    /* over the pair's ceiling */
    VF_E_FULL = -10,  /* no room for another open voucher */
    VF_E_CHUNK = -11, /* the chunk does not hash to its CID */
    VF_E_LETTER = -12 /* no letter hook, or the letter does not cover it */
} vf_status_t;

typedef enum { VF_DIR_HELD = 0, VF_DIR_OWED = 1 } vf_dir_t;

typedef struct {
    uint8_t resource;
    vna_id_t issuer, bearer;
    uint8_t scope[VNA_CID_RAW_LEN];
    uint8_t letter[32];
    uint64_t units, pair_seq;
    uint8_t prev[32];
    uint32_t issued_epoch, expiry_epoch;
} vf_voucher_t;

/* The bounty board's word on a letter of marque. `take` true: the issuer
 * is about to sign, so confirm AND take `units` off the letter's allowance;
 * false: the bearer is checking, confirm only. True means approved. */
typedef bool (*vf_letter_fn)(void *ctx, const uint8_t letter[32], const vna_id_t *bearer,
                             uint8_t resource, uint64_t units, bool take);

typedef struct {
    bool used;
    vna_id_t id;
    uint8_t pk[VNA_PK_LEN];
    uint64_t out_seq, in_seq; /* last voucher issued to / accepted from */
    uint8_t out_prev[32], in_prev[32];
    uint64_t cap[VF_RES_COUNT]; /* ceiling, each direction */
    uint64_t score;             /* V5 */
} vf_peer_t;

typedef struct {
    bool used;
    uint8_t peer, dir, resource;
    uint32_t expiry;
    uint64_t seq, units;
} vf_open_t;

typedef struct {
    const vna_identity_t *me;
    vf_letter_fn letter;
    void *letter_ctx;
    bool sealed, ever_sealed;
    uint32_t epoch, seal_epoch;
    vf_peer_t peer[VF_MAX_PEERS];
    vf_open_t open[VF_MAX_OPEN];
    uint64_t evaporated; /* units dropped by expiry and seals, all time */
} vf_t;

void vf_init(vf_t *f, const vna_identity_t *me, uint32_t epoch);
/* Bind the bounty board's letter check (NULL: issue and accept refuse). */
void vf_set_letter_hook(vf_t *f, vf_letter_fn fn, void *ctx);
/* Add a peer by its ML-DSA-87 public key (the id is derived). Returns the
 * peer index, or VF_E_PEER when the table is full; adding a known key
 * returns its existing index. */
int32_t vf_add_peer(vf_t *f, const uint8_t pk[VNA_PK_LEN]);
int32_t vf_find_peer(const vf_t *f, const vna_id_t *id);
vf_status_t vf_set_cap(vf_t *f, uint32_t peer, uint8_t resource, uint64_t cap);

void vf_encode_body(const vf_voucher_t *v, uint8_t out[VF_BODY_LEN]);
vf_status_t vf_decode(const uint8_t *in, uint32_t len, vf_voucher_t *v);

/* V1. Writes VF_VOUCHER_LEN bytes to out and records the voucher as OWED.
 * rnd: 32 bytes for hedged signing, or NULL (deterministic). */
vf_status_t vf_issue(vf_t *f, uint32_t peer, uint8_t resource, const uint8_t scope[VNA_CID_RAW_LEN],
                     const uint8_t letter[32], uint64_t units, uint32_t lifetime,
                     const uint8_t *rnd, uint8_t out[VF_VOUCHER_LEN]);
/* V1 for one hydration chunk: checks vna_cid_raw(data) == chunk_cid, then
 * issues a TRANSFER voucher for ceil(len / 4096) sectors scoped to `scope`. */
vf_status_t vf_issue_chunk(vf_t *f, uint32_t peer, const uint8_t scope[VNA_CID_RAW_LEN],
                           const uint8_t letter[32], const uint8_t chunk_cid[VNA_CID_RAW_LEN],
                           const uint8_t *data, uint32_t len, uint32_t lifetime, const uint8_t *rnd,
                           uint8_t out[VF_VOUCHER_LEN]);
/* V2. The letter hook is asked with take == false. */
vf_status_t vf_accept(vf_t *f, const uint8_t *in, uint32_t len);

/* V3. Units of `resource` open in one direction with `peer`. */
uint64_t vf_open_units(const vf_t *f, uint32_t peer, uint8_t resource, uint8_t dir);
/* held - owed (positive: the peer owes this node). */
int64_t vf_net(const vf_t *f, uint32_t peer, uint8_t resource);
/* Cancel min(held, owed), oldest expiry first then lowest seq. Returns the
 * units cancelled from each side. */
uint64_t vf_offset(vf_t *f, uint32_t peer, uint8_t resource);

/* V4 and V5 decay. Moving to an earlier epoch is refused (VF_E_ARG). */
vf_status_t vf_tick(vf_t *f, uint32_t epoch);

/* V5. */
uint32_t vf_weight(const vf_t *f, uint32_t peer);
/* Share `slots` over peers idx[0..n) by weight, largest remainder; out[i]
 * sums to exactly `slots`. Returns VF_E_ARG on a bad index or n == 0. */
vf_status_t vf_split(const vf_t *f, const uint32_t *idx, uint32_t n, uint32_t slots, uint32_t *out);

/* V6. vf_seal returns the units evaporated by this seal. */
uint64_t vf_seal(vf_t *f);
vf_status_t vf_unseal(vf_t *f); /* VF_E_SEALED until the epoch has moved on */

#endif /* ZXV_VFLOAT_H */
