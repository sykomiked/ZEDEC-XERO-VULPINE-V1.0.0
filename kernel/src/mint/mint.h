/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mint.h — minting Vino or V-Bills against attested capacity, on pay_ledger.
 *
 * OWNER DECISIONS implemented here: "mint module: e-WHR -> Vino or V-Bills,
 * jurisdiction haircut, real sig verify"; "mint only against verified
 * capacity (compute, kWh, e-WHR)"; "toxic-asset quarantine to Q-Notes" (the
 * quarantine hand-off is M8; the Q-Note side is kernel/src/qnote). VFV / Vino
 * coins are Fibonacci-only (vino_stores.h: vino_mint refuses a face off the
 * ladder), so a Vino mint is reported as its Zeckendorf coins (M6). Holders'
 * funds are never seized: quarantine moves nothing on the ledger.
 *
 *   M1 JURISDICTION POLICY (fail closed). A table of up to MINT_MAX_JURIS
 *      entries: jurisdiction code (an operator-chosen uint32) -> enabled,
 *      haircut_bps (0..10000), cap (per-epoch mint cap, fiat minor units,
 *      < PAY_BAL_MAX). The table starts EMPTY: minting is off everywhere
 *      until the operator enables a jurisdiction. An unknown or disabled
 *      jurisdiction is refused (MINT_ERR_JURIS). An update keeps the epoch's
 *      usage and may not lower the cap below it (MINT_ERR_STATE).
 *   M2 ATTESTORS. A registry of up to MINT_MAX_ATTESTORS custodians /
 *      attestors (uint32 id -> enabled, haircut_bps 0..10000). Unknown or
 *      disabled: MINT_ERR_UNKNOWN. The id is what the verify hook is asked
 *      to check the signature against.
 *   M3 UNIT VALUES. Each capacity kind (E_WHR electronic warehouse receipt,
 *      COMPUTE attested compute units, KWH attested energy) has an operator-
 *      set value of num/den fiat minor units per unit (den > 0). num == 0
 *      means unset, and a kind without a value is refused (MINT_ERR_PRICE).
 *      The operator updates it from its own price source; a claim records
 *      the value in force when it minted.
 *   M4 CLAIM. A capacity claim is signed by its attestor over
 *        "zxv/mint-claim/v1" (17 bytes) || attestor (u32) || id[32] ||
 *        kind (u8) || jurisdiction (u32) || qty (u64) || expiry (u64)
 *      all little-endian (mint_claim_msg, MINT_CLAIM_MSG bytes), checked by
 *      the caller's verify hook (ML-DSA-65 in production and in the test).
 *      The target (Vino or V-Bill) and the holder are the operator's choice
 *      at mint time, not part of the attestation. Checks, in order: args
 *      (kind, qty > 0, target, holder of the target's asset and not an
 *      issuer) -> attestor (M2) -> jurisdiction (M1) -> unit value (M3) ->
 *      expiry > now (MINT_ERR_EXPIRED) -> signature (MINT_ERR_SIG) -> id not
 *      already held (MINT_ERR_DUP) -> value (M5) -> cap (M7) -> slot (M9) ->
 *      ledger. A refusal at any step records and posts nothing.
 *   M5 VALUE. gross = floor(qty * num / den); then the ATTESTOR haircut,
 *      then the JURISDICTION haircut, each h(x, b) = floor(x * (10000 - b) /
 *      10000):  net = h(h(gross, attestor_bps), juris_bps). (The order is
 *      fixed and documented; floors make it matter by at most one unit.)
 *      A gross that does not fit 64 bits is MINT_ERR_OVERFLOW. What the
 *      haircuts and floors drop is recorded per claim (gross - net) and is
 *      never minted.
 *   M6 TARGETS. VINO: Vino is minted at par, one Vino minor unit per fiat
 *      minor unit: units = face = net, issued in ONE posting from the Vino
 *      issuer to the holder; the claim records the Zeckendorf split of
 *      `units` into Fibonacci ladder coins (rung r has face F(r+1): 1, 2, 3,
 *      5, ... up to rung 45 = 1,836,311,903; amounts past the top face repeat
 *      the top rung; see mint_zeckendorf — the same split as vino_zeckendorf,
 *      cross-checked in the test). VBILL: units = floor(net / backing)
 *      V-Bills (backing = fiat minor units per V-Bill, operator-set, > 0),
 *      face = units * backing, issued in ONE posting from the V-Bill issuer
 *      (its CREDIT is V-Bills outstanding); remainder = net - face is
 *      recorded and not minted. units == 0 is MINT_ERR_ZERO (nothing
 *      recorded, so the claim may come back when the value is higher).
 *   M7 CAP. Each jurisdiction's per-epoch usage counts the FACE minted
 *      (fiat value actually issued). A mint whose face would take usage above
 *      the cap is refused whole (MINT_ERR_CAP); there is no partial mint.
 *      mint_epoch_advance starts a new epoch and resets every usage to 0.
 *   M8 LIFECYCLE. LIVE -> QUARANTINED (mint_quarantine: the claim is toxic —
 *      disputed or failed audit. It stops counting as backing (M10) and its
 *      face, the fiat value of the units still outstanding, is returned for
 *      a Q-Note lot. Nothing moves on the ledger: holders keep their units.)
 *      LIVE or QUARANTINED -> RETIRED (mint_retire: the capacity was consumed
 *      or redeemed and the units come back: ONE pay_ledger_redeem at par of
 *      the claim's units from the account that returns them to the issuer).
 *      Anything else is MINT_ERR_STATE; an unknown id is MINT_ERR_UNKNOWN.
 *   M9 REPLAY. A claim id is refused while any slot holds it, whatever its
 *      status. A slot is reused only when its claim is RETIRED, expired
 *      (expiry <= now: a replay of that message is refused as expired) and
 *      minted in an earlier epoch (so the epoch audit M11 stays exact).
 *      Ledger requests carry identifiers derived by SHA3-256 from
 *      "zxv/mint/req" || seed || counter, so no two postings of this module
 *      share a UETR or idempotency key, nor collide with another module's
 *      ids built from the same seed (two mints on one ledger need distinct
 *      seeds).
 *   M10 BACKING. mint_backing(now) = sum of the face of LIVE claims with
 *      expiry > now. Quarantined, retired and expired claims count 0.
 *   M11 AUDIT. mint_check: per target, the LIVE and QUARANTINED units summed
 *      over the claims equal the counters, and issued - retired == live +
 *      quarantined; the issuer's CREDIT is >= that (== when `exclusive`,
 *      i.e. the issuer issues only through this module); every
 *      jurisdiction's usage equals the face of its claims minted in the
 *      current epoch and is <= its cap; every claim is internally consistent
 *      (net <= gross, face + remainder == net, units / face per M6).
 *
 * NO USURY. Nothing here grows with time or balance: no interest, no fee.
 * Every multiply and add is overflow-checked; every division is a documented
 * floor whose remainder is recorded (M5, M6).
 *
 * HONEST LIMITS. A signature proves WHO attested, not that the capacity
 * exists, is unencumbered or is not attested twice to someone else; haircuts
 * are policy, not guarantees; unit values are only as good as the operator's
 * price source. V-Bills (and Vino minted against assets) are likely
 * securities in many jurisdictions; minting may be a regulated activity,
 * which is why every jurisdiction is off by default. It is designed to
 * support a backed-issuance policy and needs legal review before any real
 * use; no legal or regulatory status is claimed. Freestanding C11: no
 * allocation, no floating point, no libc.
 */
#ifndef ZXV_MINT_H
#define ZXV_MINT_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"

#define MINT_MAX_JURIS     16u
#define MINT_MAX_ATTESTORS 16u
#define MINT_MAX_CLAIMS    64u
#define MINT_BPS           10000u
#define MINT_LADDER_RUNGS  45u /* rung 45 = F(46) = 1,836,311,903 */
#define MINT_ZECK_MAX      24u /* distinct rungs below the top in one split */
#define MINT_TAG_LEN       17u /* "zxv/mint-claim/v1" */
#define MINT_CLAIM_MSG     (MINT_TAG_LEN + 4u + 32u + 1u + 4u + 8u + 8u)

typedef enum {
    MINT_OK = 0,
    MINT_ERR_ARG = -1,
    MINT_ERR_FULL = -2,
    MINT_ERR_SIG = -3,     /* M4 */
    MINT_ERR_UNKNOWN = -4, /* M2 attestor, M8 claim id */
    MINT_ERR_JURIS = -5,   /* M1 */
    MINT_ERR_DUP = -6,     /* M9 */
    MINT_ERR_EXPIRED = -7, /* M4 */
    MINT_ERR_PRICE = -8,   /* M3 */
    MINT_ERR_CAP = -9,     /* M7 */
    MINT_ERR_ZERO = -10,   /* M6 */
    MINT_ERR_OVERFLOW = -11,
    MINT_ERR_LEDGER = -12, /* see last_ledger */
    MINT_ERR_STATE = -13
} mint_status_t;

typedef enum {
    MINT_KIND_EWHR = 0,
    MINT_KIND_COMPUTE = 1,
    MINT_KIND_KWH = 2,
    MINT_KINDS
} mint_kind_t;
typedef enum { MINT_TARGET_VINO = 0, MINT_TARGET_VBILL = 1, MINT_TARGETS } mint_target_t;
typedef enum {
    MINT_FREE = 0,
    MINT_LIVE = 1,
    MINT_QUARANTINED = 2,
    MINT_RETIRED = 3
} mint_claim_state_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's */
typedef bool (*mint_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                               const uint8_t *sig, uint32_t sig_len);

typedef struct {
    bool used, enabled;
    uint32_t code;
    uint32_t haircut_bps;
    uint64_t cap;        /* per epoch, fiat minor units of face */
    uint64_t used_epoch; /* face minted this epoch */
} mint_juris_t;

typedef struct {
    bool used, enabled;
    uint32_t id;
    uint32_t haircut_bps;
} mint_attestor_t;

typedef struct {
    uint64_t num, den; /* fiat minor units per unit = num / den; num 0 = unset */
} mint_unit_t;

/* M6: top coins of rung 45, then n distinct rungs (largest first) */
typedef struct {
    uint64_t top;
    uint32_t n;
    uint8_t rung[MINT_ZECK_MAX];
} mint_coins_t;

/* M4: what the attestor signs */
typedef struct {
    uint32_t attestor;
    uint8_t id[32];
    uint8_t kind; /* mint_kind_t */
    uint32_t juris;
    uint64_t qty, expiry;
} mint_capacity_t;

typedef struct {
    uint8_t state; /* mint_claim_state_t */
    mint_capacity_t cap;
    uint64_t epoch;
    uint64_t unit_num, unit_den;     /* M3 at mint time */
    uint32_t att_bps, juris_bps;     /* M5 at mint time */
    uint64_t gross, net;             /* M5 */
    uint8_t target;                  /* mint_target_t */
    uint32_t holder;                 /* the account the units were issued to */
    uint64_t backing;                /* VBILL: fiat per V-Bill at mint time */
    uint64_t units, face, remainder; /* M6 */
    mint_coins_t coins;              /* VINO only */
} mint_claim_t;

typedef struct {
    pay_ledger_t *L;
    uint32_t issuer[MINT_TARGETS]; /* Vino issuer, V-Bill issuer */
    uint64_t vbill_backing;
    bool exclusive;
    mint_verify_fn verify;
    void *vctx;
    uint8_t seed[32];
    uint64_t seq, epoch;
    mint_juris_t juris[MINT_MAX_JURIS];
    mint_attestor_t att[MINT_MAX_ATTESTORS];
    mint_unit_t unit[MINT_KINDS];
    mint_claim_t claim[MINT_MAX_CLAIMS];
    uint64_t issued[MINT_TARGETS], retired[MINT_TARGETS];   /* lifetime units */
    uint64_t live[MINT_TARGETS], quarantined[MINT_TARGETS]; /* units now */
    pay_status_t last_ledger;
    bool ready;
} mint_t;

/* Bind to the ledger. vino_issuer and vbill_issuer: active ISSUER accounts of
 * two distinct assets; vbill_backing > 0; verify non-NULL. `exclusive`: the
 * issuers issue only through this module (M11 then checks CREDIT equality). */
mint_status_t mint_init(mint_t *m, pay_ledger_t *L, uint32_t vino_issuer, uint32_t vbill_issuer,
                        uint64_t vbill_backing, bool exclusive, mint_verify_fn verify, void *vctx,
                        const uint8_t seed[32]);
/* M6: future V-Bill mints only. */
mint_status_t mint_set_backing(mint_t *m, uint64_t vbill_backing);

/* M1 / M2 / M3 */
mint_status_t mint_juris_set(mint_t *m, uint32_t code, bool enabled, uint32_t haircut_bps,
                             uint64_t cap);
const mint_juris_t *mint_juris(const mint_t *m, uint32_t code);
mint_status_t mint_attestor_set(mint_t *m, uint32_t id, bool enabled, uint32_t haircut_bps);
mint_status_t mint_unit_set(mint_t *m, uint8_t kind, uint64_t num, uint64_t den);

/* M5 (pure): floor(x * (10000 - bps) / 10000); bps above 10000 gives 0. */
uint64_t mint_haircut(uint64_t x, uint32_t bps);
/* M5 (pure). False (outputs untouched) when den == 0 or gross does not fit. */
bool mint_value(uint64_t qty, uint64_t num, uint64_t den, uint32_t att_bps, uint32_t juris_bps,
                uint64_t *gross, uint64_t *net);
/* M6 (pure): face of a ladder rung (0 off the ladder), and the split. */
uint64_t mint_coin_face(uint32_t rung);
bool mint_zeckendorf(uint64_t amount, mint_coins_t *out);

/* M4 */
void mint_claim_msg(const mint_capacity_t *c, uint8_t out[MINT_CLAIM_MSG]);
/* M4-M7, M9. *index (optional) gets the claim slot. */
mint_status_t mint_issue(mint_t *m, const mint_capacity_t *c, const uint8_t *sig, uint32_t sig_len,
                         uint8_t target, uint32_t holder, uint64_t now, uint32_t *index);
/* M7 */
mint_status_t mint_epoch_advance(mint_t *m);
/* M8. *face (optional) gets the claim's face. */
mint_status_t mint_quarantine(mint_t *m, const uint8_t id[32], uint64_t *face);
mint_status_t mint_retire(mint_t *m, const uint8_t id[32], uint32_t from);
const mint_claim_t *mint_find(const mint_t *m, const uint8_t id[32]);
/* M10 */
uint64_t mint_backing(const mint_t *m, uint64_t now);
/* M11 */
bool mint_check(const mint_t *m);

#endif /* ZXV_MINT_H */
