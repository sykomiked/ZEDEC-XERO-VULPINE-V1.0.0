/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* jroute.h — the Automated Settlement and Jurisdictional Routing Matrix:
 * the router that turns a VINO burn into the asset a holder's zone permits.
 *
 * Design note: docs/JURISDICTION_ROUTER.md. The apps named below (Caravan
 * #090, Tallystick #078, Neap #234, Assize #178, Portolan #344, Seachest
 * #403) are catalogue slots that are mostly not built yet; this module is
 * the kernel half and reaches them only through jr_hooks_t.
 *
 *   R1 PROFILE GATE. A router starts in the COMMUNITY profile with
 *      allotment off. jr_accept_allotment refuses (JR_E_PROFILE) until the deployment
 *      selects the INSTITUTION profile AND calls jr_enable. Allotment into
 *      assets is the point where VINO stops being closed-loop store credit
 *      (docs/JURISDICTION_ROUTER.md, "Why it is off by default").
 *   R2 CAPABILITY MASK. A zone carries a 32-bit mask: JR_CAP_COMMODITY 0x01,
 *      JR_CAP_COMPUTE 0x02, JR_CAP_MUTUAL 0x04, JR_CAP_METALS 0x08. Every
 *      other bit is reserved. A zone whose mask sets a reserved bit is an
 *      unhandled flag and reads BOTH (R6), so it goes to escrow, never to a
 *      silent default.
 *   R3 HIERARCHY. Candidates are tried in a fixed order: the asset the holder
 *      asked for (if any), then every active asset in level order
 *        level 1 physical: COMMODITY, then METALS   (warehouse receipts)
 *        level 2 compute:  COMPUTE                   (TPC / AIRC quota)
 *        level 3 mutual:   MUTUAL                    (local clearing credit)
 *      and, inside a level, by registration order. An asset is a candidate
 *      only when its bit is in the zone mask, it is active, a COMPUTE asset
 *      that needs an operator node has one, a region-bound asset (region !=
 *      0) matches the zone's region (otherwise it is cross-border), a
 *      physical asset has enough free lots (and needs at most
 *      JR_MAX_RECEIPT_LOTS), and the amount makes at least one lot. The
 *      asked-for asset alone may make zero lots when the request sets
 *      `accumulate`: the burn then only grows the holder's voucher (R5).
 *      No candidate while some permitted asset exists: JR_E_DUST, nothing
 *      burned. No permitted asset at all: level 4, escrow (R6).
 *   R4 EXACT RATIO. An asset's parity is num/den base units per VINO unit
 *      and its lot is `lot` base units. With r the holder's voucher on that
 *      asset (in 1/num VINO units),
 *          total = vino * num + r          (128-bit, never wraps)
 *          lots  = floor(total / (den * lot))
 *          r'    = total mod (den * lot)
 *      so vino*num + r == lots*den*lot + r' holds exactly, and r' < den*lot.
 *      den*lot must fit 64 bits (checked at registration). A lot count that
 *      does not fit 64 bits makes the asset no candidate (it never wraps).
 *      No floating point anywhere.
 *   R5 VOUCHER. r' stays on the holder's register as the exact fraction
 *      r'/num VINO (jr_voucher reports it reduced to p/q). The next burn on
 *      the same asset adds to it. An asset's parity is fixed for its life:
 *      a new price is a new asset, and retiring one leaves its vouchers
 *      readable but not spendable elsewhere (an open decision).
 *   R6 FOUR-VALUED ROUTE. The zone verdict (R8) and each class verdict
 *      combine in the information order, where TRUE joined with FALSE is
 *      BOTH (with lpres_state_t's encoding the join is a bitwise OR):
 *        zone FALSE   (sanctioned) refused, JR_E_SANCTIONED, nothing burned.
 *        zone BOTH    the VINO is burned into escrow, reason ZONE_CONFLICT.
 *        zone NEITHER (unknown territory, or no proof backend) the VINO
 *                     becomes non-transferable local compute credit from the
 *                     configured N asset; with none configured, escrow.
 *        zone TRUE    the hierarchy runs. A class whose zone bit is clear is
 *                     skipped without asking anyone. For a class whose bit is
 *                     set, the zone's TRUE is joined with every registered
 *                     registry's answer for that class: NEITHER adds nothing,
 *                     FALSE makes BOTH. A BOTH candidate does not fall
 *                     through to a cheaper asset: its planned delivery
 *                     (lots reserved, voucher not yet moved) is frozen in
 *                     escrow with every claim pinned side by side, reason
 *                     CLASS_CONFLICT.
 *   R7 ESCROW AND RULINGS. An escrow holds the burned VINO, the frozen plan
 *      and up to JR_MAX_CLAIMS pinned claims (source id, verdict, evidence
 *      hash). jr_escrow_settle takes an arbiter's signed ruling over
 *        "zxv/jr-ruling/v1" || escrow id u64 || kind u8 || caps u32 ||
 *        region u32 || escrow digest[32]                (little-endian)
 *      checked by hooks.arbiter_verify (no hook: refused). DELIVER commits a
 *      frozen plan; REROUTE runs R3 with the ruling's caps and region and no
 *      registries (the ruling settled the contradiction); REFUND returns the
 *      VINO through hooks.refund. A settled escrow cannot be settled again.
 *   R8 PROOF OF ZONE. The client locates itself against the Portolan table
 *      (jr_zone_locate: integer point-in-polygon, closed polygons, so a
 *      point on a shared border lies in both zones) and builds a claim that
 *      carries the table hash, zone id, mask, region, verdict, epoch and a
 *      nullifier sha256(domain || device secret || epoch). Coordinates, IP
 *      and identity never leave the device. The verifier (jr_zone_verify)
 *      needs the same table hash and a fresh epoch, refuses a reused
 *      nullifier and a TRUE claim whose mask or region differs from its own
 *      table, and asks hooks.proof_verify about the proof bytes. With no
 *      proof hook every claim reads NEITHER. See HONEST LIMITS.
 *   R9 ATOMICITY. A allotment is planned in full first. Only then is
 *      hooks.burn called; if it fails nothing changes. After a burn the
 *      router commits (lots, voucher, escrow, receipt chain) and notifies
 *      hooks.deliver or hooks.escrow, which cannot fail.
 *   R10 RECEIPT CHAIN. Each receipt's digest is sha256 over
 *      "zxv/jr-receipt/v1" || previous digest || its fields, so the whole
 *      allotment history replays and any edit shows.
 *   R11 SOFTWARE FIRST. The physical layer starts OFF: until it is turned on
 *      the COMMODITY and METALS bits are cleared from every zone mask, so a
 *      cold node allots only compute (TPC ticks, AIRC bandwidth, storage
 *      quotas) and mutual credit, and nothing waits on a warehouse. Only the
 *      detachable producer ingress driver (src/jringress) turns it on, and it
 *      goes off again when the last free physical lot is withdrawn.
 *
 * HONEST LIMITS. R8 is not a zero-knowledge proof. The shipped verifier
 * checks a claim's consistency with the table and its freshness, but the
 * location itself is whatever the device says: a proof hook that checks a
 * device signature makes it a signed claim from trusted hardware, and a GPS
 * reading can be spoofed. A real ZK proof of polygon membership needs a
 * vetted proving system (a circuit over the table commitment); none is in
 * the tree, so that hook is a stub slot. Nothing here is legal advice: the
 * masks are whatever the deployment's table says. Freestanding C11: no
 * allocation, no float, no 64-bit division (i386 builds need no libgcc).
 */
#ifndef ZXV_JROUTE_H
#define ZXV_JROUTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../lpres/lpres.h"

#define JR_CAP_COMMODITY 0x01u /* physical agricultural commodity lots */
#define JR_CAP_COMPUTE   0x02u /* P2P compute / octane vouchers */
#define JR_CAP_MUTUAL    0x04u /* zero-interest mutual credit */
#define JR_CAP_METALS    0x08u /* precious metals / bonded storage */
#define JR_CAP_KNOWN     0x0Fu
#define JR_CAP_PHYSICAL  (JR_CAP_COMMODITY | JR_CAP_METALS)

/* Compute parity unit (R11): 1 VINO = 2^16 TPC clock ticks on the reference
 * node. A COMPUTE asset in ticks is num = JR_TPC_TICKS_PER_VINO, den = 1. */
#define JR_TPC_TICKS_PER_VINO 65536u

#define JR_MAX_ASSETS       16u
#define JR_MAX_LOTS         256u
#define JR_MAX_VOUCHERS     128u
#define JR_MAX_ESCROWS      32u
#define JR_MAX_CLAIMS       4u
#define JR_MAX_REGISTRIES   3u /* + the zone itself = JR_MAX_CLAIMS */
#define JR_MAX_NULLIFIERS   128u
#define JR_MAX_RECEIPT_LOTS 32u
#define JR_MAX_ZONE_PTS     64u
#define JR_LOT_HDR_LEN      128u
#define JR_CLAIM_MSG_LEN    (16u + 32u + 4u + 4u + 4u + 1u + 8u + 32u)
#define JR_RULING_MSG_LEN   (16u + 8u + 1u + 4u + 4u + 32u)

typedef enum {
    JR_OK = 0,
    JR_E_ARG = -1,
    JR_E_PROFILE = -2, /* R1 */
    JR_E_FULL = -3,
    JR_E_DUST = -4,       /* R3: nothing burned */
    JR_E_SANCTIONED = -5, /* R6: nothing burned */
    JR_E_BURN = -6,       /* R9: the burn hook refused, nothing changed */
    JR_E_PROOF = -7,      /* R8 */
    JR_E_REPLAY = -8,     /* R8 */
    JR_E_CLAIM = -9,      /* R8: claim disagrees with the table */
    JR_E_NOTFOUND = -10,
    JR_E_SETTLED = -11, /* R7 */
    JR_E_SIG = -12,     /* R7 */
    JR_E_RULING = -13,  /* R7: ruling kind not valid for this escrow */
    JR_E_REFUND = -14,  /* R7: the refund hook refused */
    JR_E_RANGE = -15,   /* R4: den*lot past 64 bits */
    JR_E_FORMAT = -16   /* lot header */
} jr_status_t;

typedef enum { JR_PROFILE_COMMUNITY = 0, JR_PROFILE_INSTITUTION = 1 } jr_profile_t;

typedef enum {
    JR_OUT_ALLOTTED = 1,     /* lots delivered (voucher may also move) */
    JR_OUT_VOUCHER_ONLY = 2, /* R3 accumulate: zero lots, voucher grew */
    JR_OUT_LOCAL_CREDIT = 3, /* R6 zone NEITHER: non-transferable compute */
    JR_OUT_ESCROW = 4        /* R6/R3 level 4 */
} jr_outcome_t;

typedef enum {
    JR_ESC_ZONE_CONFLICT = 1,
    JR_ESC_CLASS_CONFLICT = 2,
    JR_ESC_NO_ROUTE = 3
} jr_escrow_reason_t;

typedef enum { JR_RULE_DELIVER = 1, JR_RULE_REROUTE = 2, JR_RULE_REFUND = 3 } jr_rule_kind_t;

/* Ledger event on each receipt (in its R10 digest). The names follow the
 * project's mutual-aid wording; a name does not change what the transfer
 * is in law (docs/JURISDICTION_ROUTER.md, "Wording"). */
typedef enum {
    JR_EVENT_DISCRETIONARY_ALLOTMENT = 1,       /* accepted directly */
    JR_EVENT_CUSTODIAL_HOLD = 2,                /* placed in Seachest escrow */
    JR_EVENT_CUSTODIAL_IN_KIND_DISBURSEMENT = 3 /* released from escrow by a ruling */
} jr_event_t;

/* The action label and notice a front end shows before jr_accept_allotment. */
#define JR_UI_ACTION "Accept Discretionary System Allotment"
#define JR_UI_NOTICE                                                                               \
    "In accordance with the mutual aid and public-benefit covenants of the distribution, the "     \
    "system allocates available surplus reserves to the participant. Allotments are provided "     \
    "gratuitously in good faith, determined by jurisdictional feasibility, without contractual "   \
    "consideration or guarantee of monetary yield."

/* ---- lot header (Caravan #090 warehouse receipt), fixed width ---- */
typedef struct {
    uint16_t grade_kind; /* 1 olive oil (acidity), 2 metal (fineness), other: deployment */
    uint32_t warehouse_id;
    uint32_t volume; /* millilitres or milligrams; > 0 */
    uint64_t drum_serial;
    uint64_t lot_number;
    uint32_t grade;       /* acidity in 0.001 %, or fineness in parts per 10000 */
    uint16_t haircut_bps; /* custodian haircut on the backing receipt, <= 10000 */
    uint8_t vault_pubkey[32];
    uint8_t custodian_receipt[32]; /* hash of the custodian-signed receipt */
    int32_t depot_lat;             /* bonded depot pickup, microdegrees */
    int32_t depot_lon;
} jr_lot_hdr_t;

/* "ZXLT" magic, version 1, then the fields above at fixed offsets, reserved
 * bytes zero, and the first four bytes of sha256(bytes 0..123) at 124. */
void jr_lot_hdr_encode(const jr_lot_hdr_t *h, uint8_t out[JR_LOT_HDR_LEN]);
jr_status_t jr_lot_hdr_decode(const uint8_t in[JR_LOT_HDR_LEN], jr_lot_hdr_t *h);

/* ---- Portolan (#344) zone table: a ROM table, integer polygons ---- */
typedef struct {
    int32_t lat; /* microdegrees */
    int32_t lon;
} jr_pt_t;

typedef enum { JR_ZONE = 1, JR_SANCTION = 2 } jr_zone_kind_t;

typedef struct {
    uint32_t id;
    uint8_t kind;    /* jr_zone_kind_t */
    uint32_t caps;   /* R2, ignored for JR_SANCTION */
    uint32_t region; /* mutual-credit region; 0 = none */
    uint16_t npts;   /* 3..JR_MAX_ZONE_PTS */
    const jr_pt_t *pts;
} jr_zone_t;

/* sha256 over the canonical encoding of the table (order matters). */
jr_status_t jr_table_hash(const jr_zone_t *zones, uint32_t n, uint8_t out[32]);

/* Client side. Writes the zone that decides (TRUE: the lowest id among
 * agreeing zones) and returns the verdict: FALSE in any sanction polygon,
 * NEITHER in no zone, BOTH when containing zones disagree on mask or region
 * or a mask sets a reserved bit, TRUE otherwise. A malformed table entry
 * reads NEITHER. */
lpres_state_t jr_zone_locate(const jr_zone_t *zones, uint32_t n, jr_pt_t p, uint32_t *zone_id,
                             uint32_t *caps, uint32_t *region);

typedef struct {
    uint8_t table_hash[32];
    uint32_t zone_id;
    uint32_t caps;
    uint32_t region;
    uint8_t verdict; /* lpres_state_t */
    uint64_t epoch;
    uint8_t nullifier[32];
} jr_zone_claim_t;

jr_status_t jr_zone_attest(const jr_zone_t *zones, uint32_t n, jr_pt_t p,
                           const uint8_t device_secret[32], uint64_t epoch, jr_zone_claim_t *out);
/* The bytes a proof (or a device signature) covers. */
void jr_zone_claim_msg(const jr_zone_claim_t *c, uint8_t out[JR_CLAIM_MSG_LEN]);

/* ---- registries and hooks ---- */
typedef struct {
    uint32_t source; /* 0 = the zone claim; else a registry id */
    uint8_t verdict; /* lpres_state_t */
    uint8_t evidence[32];
} jr_claim_t;

/* A registry's determination for one capability class in one zone. */
typedef lpres_state_t (*jr_registry_fn)(void *ctx, uint32_t cap, uint32_t zone_id,
                                        uint8_t evidence[32]);
typedef bool (*jr_verify_fn)(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                             uint32_t siglen);

typedef struct {
    uint64_t receipt_id;
    uint8_t user[32];
    uint64_t vino_burned;
    uint8_t outcome;   /* jr_outcome_t */
    uint8_t event;     /* jr_event_t */
    uint32_t asset_id; /* 0 for an escrow with no plan */
    uint32_t cap;      /* the asset's class bit */
    bool transferable; /* false for R6 NEITHER credit */
    uint64_t lots;     /* lots (physical) or lot units (compute, mutual) */
    uint32_t nlot_idx; /* physical: lots listed below */
    uint16_t lot_idx[JR_MAX_RECEIPT_LOTS];
    uint64_t voucher_num; /* holder's voucher on the asset after this: num/den VINO, reduced */
    uint64_t voucher_den;
    uint8_t route;      /* lpres_state_t */
    uint64_t escrow_id; /* 0 unless outcome == JR_OUT_ESCROW */
    uint8_t digest[32]; /* R10 */
} jr_receipt_t;

typedef struct {
    uint64_t id;
    uint8_t user[32];
    uint64_t vino;
    uint8_t reason; /* jr_escrow_reason_t */
    bool settled;
    uint8_t ruling; /* jr_rule_kind_t once settled */
    uint32_t zone_id;
    uint32_t caps;
    uint32_t region;
    bool operator_node;
    /* the frozen plan (CLASS_CONFLICT only; asset_id 0 otherwise) */
    uint32_t asset_id;
    uint64_t lots;
    uint64_t voucher_after; /* raw r' in 1/num VINO units */
    uint32_t nlot_idx;
    uint16_t lot_idx[JR_MAX_RECEIPT_LOTS];
    uint32_t nclaims;
    jr_claim_t claims[JR_MAX_CLAIMS]; /* pinned side by side (Assize #178) */
    uint8_t digest[32];
} jr_escrow_t;

typedef struct {
    void *ctx;
    /* VINO ledger: 0 = burned / refunded. */
    int (*burn)(void *ctx, const uint8_t user[32], uint64_t vino);
    int (*refund)(void *ctx, const uint8_t user[32], uint64_t vino);
    /* Caravan (physical), the TPC/AIRC issuer (compute), Tallystick/Neap (mutual). */
    void (*deliver)(void *ctx, const jr_receipt_t *r);
    /* Seachest (#403) holds it, Assize (#178) shows the pinned claims. */
    void (*escrow)(void *ctx, const jr_escrow_t *e);
    jr_verify_fn proof_verify; /* R8; NULL = stub, every claim NEITHER */
    void *proof_ctx;
    jr_verify_fn arbiter_verify; /* R7; NULL = no rulings accepted */
    void *arbiter_ctx;
} jr_hooks_t;

/* ---- assets and the router ---- */
typedef struct {
    uint32_t id;
    uint32_t cap;      /* exactly one known bit */
    uint64_t num, den; /* base units per VINO = num/den; both > 0 */
    uint64_t lot;      /* base units per lot; > 0; den*lot fits 64 bits */
    uint32_t region;   /* 0 = any region */
    bool needs_operator;
    bool active;
} jr_asset_t;

typedef struct {
    jr_lot_hdr_t hdr;
    uint32_t asset_id;
    uint8_t state;   /* 0 free, 1 escrowed, 2 released, 3 withdrawn by its producer */
    uint32_t source; /* producer slot + 1 from the ingress driver; 0 = added directly */
} jr_lot_t;

typedef struct {
    uint8_t user[32];
    uint32_t asset_id;
    uint64_t r; /* < den*lot */
} jr_voucher_t;

typedef struct {
    uint8_t nf[32];
    uint64_t epoch;
} jr_nullifier_t;

typedef struct {
    uint8_t profile;
    bool enabled;
    bool physical_online; /* R11: false until a producer pledges lots */
    jr_hooks_t hooks;
    const jr_zone_t *zones;
    uint32_t nzones;
    uint8_t table_hash[32];
    uint64_t max_age; /* epochs a claim stays fresh */
    uint32_t n_asset; /* R6 NEITHER credit asset, 0 = none */
    uint32_t nassets;
    jr_asset_t assets[JR_MAX_ASSETS];
    uint32_t nlots;
    jr_lot_t lots[JR_MAX_LOTS];
    uint32_t nvouchers;
    jr_voucher_t vouchers[JR_MAX_VOUCHERS];
    uint32_t nescrows;
    jr_escrow_t escrows[JR_MAX_ESCROWS];
    uint32_t nnull;
    jr_nullifier_t nulls[JR_MAX_NULLIFIERS];
    uint32_t nregs;
    jr_registry_fn regs[JR_MAX_REGISTRIES];
    void *reg_ctx[JR_MAX_REGISTRIES];
    uint32_t reg_id[JR_MAX_REGISTRIES];
    uint64_t next_receipt;
    uint64_t burned_total;   /* every VINO the router has burned */
    uint64_t refunded_total; /* every VINO returned by R7 REFUND */
    uint8_t head[32];        /* R10 */
} jr_router_t;

typedef struct {
    uint8_t user[32];
    uint64_t vino;
    uint32_t want_asset; /* 0 = no preference */
    bool accumulate;     /* R3: allow a zero-lot burn on want_asset */
    bool operator_node;
} jr_request_t;

/* The verified zone a allotment runs under (from jr_zone_verify). */
typedef struct {
    lpres_state_t verdict;
    uint32_t zone_id;
    uint32_t caps;
    uint32_t region;
} jr_zone_ok_t;

jr_status_t jr_init(jr_router_t *r, const jr_hooks_t *hooks, const jr_zone_t *zones,
                    uint32_t nzones, uint64_t max_age);
jr_status_t jr_set_profile(jr_router_t *r, jr_profile_t p); /* COMMUNITY also disables */
jr_status_t jr_enable(jr_router_t *r, bool on);             /* R1: INSTITUTION only */
jr_status_t jr_add_asset(jr_router_t *r, const jr_asset_t *a);
jr_status_t jr_retire_asset(jr_router_t *r, uint32_t id);
jr_status_t jr_set_n_asset(jr_router_t *r, uint32_t id); /* must be a COMPUTE asset */
jr_status_t jr_add_registry(jr_router_t *r, uint32_t id, jr_registry_fn fn, void *ctx);
jr_status_t jr_add_lot(jr_router_t *r, uint32_t asset_id, const uint8_t hdr[JR_LOT_HDR_LEN]);
uint32_t jr_free_lots(const jr_router_t *r, uint32_t asset_id);
/* R11. The producer ingress driver (src/jringress) adds lots with a source,
 * withdraws a source's free lots, and turns the physical layer on or off.
 * jr_set_physical(r, true) needs at least one free physical lot. */
jr_status_t jr_add_lot_src(jr_router_t *r, uint32_t asset_id, const uint8_t hdr[JR_LOT_HDR_LEN],
                           uint32_t source);
uint32_t jr_withdraw_source(jr_router_t *r, uint32_t source);
uint32_t jr_free_physical(const jr_router_t *r);
jr_status_t jr_set_physical(jr_router_t *r, bool on);

jr_status_t jr_zone_verify(jr_router_t *r, const jr_zone_claim_t *c, const uint8_t *proof,
                           uint32_t proof_len, uint64_t now_epoch, jr_zone_ok_t *out);

/* R3-R6, R9-R10. On JR_OK the receipt is filled (outcome ESCROW carries
 * the escrow id). On any error nothing was burned and nothing changed. */
jr_status_t jr_accept_allotment(jr_router_t *r, const jr_request_t *q, const jr_zone_ok_t *z,
                                jr_receipt_t *out);

void jr_ruling_msg(const jr_escrow_t *e, uint8_t kind, uint32_t caps, uint32_t region,
                   uint8_t out[JR_RULING_MSG_LEN]);
/* R7. REROUTE fills `out` with the new receipt (which may itself be an
 * escrow if the ruling's caps permit nothing); DELIVER fills it with the
 * delivery; REFUND leaves outcome 0. */
jr_status_t jr_escrow_settle(jr_router_t *r, uint64_t escrow_id, uint8_t kind, uint32_t caps,
                             uint32_t region, const uint8_t *sig, uint32_t siglen,
                             jr_receipt_t *out);
const jr_escrow_t *jr_escrow_get(const jr_router_t *r, uint64_t id);

/* R5: the holder's voucher on an asset as reduced p/q VINO (0/1 when none). */
jr_status_t jr_voucher(const jr_router_t *r, const uint8_t user[32], uint32_t asset_id, uint64_t *p,
                       uint64_t *q);

/* Exact 128-bit helpers (exposed for tests). */
typedef struct {
    uint64_t hi, lo;
} jr_u128_t;
jr_u128_t jr_mul64(uint64_t a, uint64_t b);
/* q = n / d, rem = n mod d; d > 0. Returns false when q needs more than 64 bits. */
bool jr_divmod(jr_u128_t n, uint64_t d, uint64_t *q, uint64_t *rem);

#endif /* ZXV_JROUTE_H */
