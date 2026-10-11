/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* recovery.h — ledger-side account recovery control: who may move an
 * account to a new device, and how fast its value thaws afterwards.
 *
 * This sits ON TOP of kernel/src/ident (seed recovery: vault, Shamir shares,
 * biometric). ident gets the person their root seed back; this module is the
 * control workflow the ledger runs before it honours a new device. No key
 * shares live here: each guardian signs with their own key (ML-DSA in
 * production) through the caller's verify hook, exactly like vhub_verify_fn.
 *
 * Owner spec: "disposable device keys under the account's root anchor; a
 * recovery sandbox (view-only) while a claim is pending; time lock + old-
 * device veto; staged thaw (V-Bills locked 7 days; liquid funds 5% per day
 * for the first 72 hours unless gateway-verified); 3-of-5 guardians, each
 * signing with their own ML-DSA key (no key shares here). 4-of-5 approvals
 * plus a 14-day time lock override a stolen-device veto. Dormancy changes
 * nothing. No bonds, no slashing; rate-limit claims and notify ALL guardians
 * on every claim, approval, veto and finalisation (callback)."
 *
 *   R1 CLAIM. rec_claim_open(acct, new_device_key_hash, tick) is refused while
 *      a claim is open and when rate_max claims were opened in the last
 *      rate_window_ticks (default 3 per 30 days; cancelled claims count).
 *      Every guardian is notified. The claim is PENDING and the account is a
 *      SANDBOX: rec_can_view is true, rec_can_spend is false.
 *   R2 APPROVE. Guardian i (0..4) signs rec_claim_msg(REC_MSG_APPROVE), which
 *      binds the root anchor, the claim id and the new key hash. A duplicate
 *      returns REC_DUP and is not counted twice; an unknown index is refused.
 *   R3 VETO / CANCEL. An active device key (an old device; the claimed new
 *      key is not active yet) signs REC_MSG_VETO: the claim is VETOED and
 *      can then finalise only with >= 4 approvals and tick >= opened + 14
 *      days. Without a veto: >= 3 approvals and tick >= opened + timelock
 *      (default 72 hours). REC_MSG_CANCEL from such a device closes the
 *      claim outright.
 *   R4 FINALISE. rec_finalize(tick) checks R3, revokes every old device key
 *      (they are disposable), installs the new one and enters THAWING.
 *      Time is the caller's tick; ticks_per_day is in the config.
 *   R5 THAW. rec_vbill_spendable is 0 until finalised + 7 days, then all.
 *      rec_liquid_allowance(balance_at_finalise, tick) is the CUMULATIVE
 *      allowance floor(balance * 5 * (d + 1) / 100), d = whole days since
 *      finalise, while d < 3 (the first 72 hours), then the full balance. A
 *      gateway attestation (rec_gateway_verify, signed by the configured
 *      gateway over REC_MSG_GATEWAY) lifts the liquid limit at once; it never
 *      lifts the V-Bill lock.
 *   R6 SILENCE. No function changes state because time passed or nobody
 *      answered: a claim with no approvals stays PENDING forever, and an
 *      account that is never used stays as it is (dormancy changes nothing).
 *      There are no bonds and no slashing.
 *
 * HONEST LIMITS. The verify hook decides what a valid signature is; this
 * module only counts the answers. Guardians who are fooled or collude can
 * move an account, and a thief holding an old device can only delay (14
 * days), not stop, a 4-of-5 recovery: that is the owner's chosen trade-off.
 * Notification is a callback; delivery is the caller's job. The thaw
 * limits are what this module reports; the ledger must enforce them. This
 * is designed to support the recovery flow; it is not a custody service.
 * Freestanding C11: no allocation, no float.
 */
#ifndef ZXV_REC_RECOVERY_H
#define ZXV_REC_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#define REC_KEY           32u /* key hashes / identifiers */
#define REC_GUARDIANS     5u
#define REC_QUORUM        3u
#define REC_VETO_QUORUM   4u
#define REC_VETO_DAYS     14u
#define REC_VBILL_DAYS    7u
#define REC_LIQ_DAYS      3u /* 72 hours */
#define REC_LIQ_PCT       5u /* per day, cumulative */
#define REC_MAX_DEVICES   4u
#define REC_RATE_CAP      8u /* largest configurable rate_max */
#define REC_DEFAULT_RATE  3u
#define REC_DEFAULT_WDAYS 30u
#define REC_DEFAULT_TDAYS 3u /* default time lock: 72 hours */

typedef enum {
    REC_OK = 0,
    REC_DUP = 1, /* R2: duplicate approval, not counted */
    REC_ERR_ARG = -1,
    REC_ERR_SIG = -2,      /* bad signature */
    REC_ERR_BUSY = -3,     /* R1: a claim is already open */
    REC_ERR_RATE = -4,     /* R1: too many claims in the window */
    REC_ERR_STATE = -5,    /* no pending claim / not thawing */
    REC_ERR_GUARDIAN = -6, /* R2: unknown guardian */
    REC_ERR_QUORUM = -7,   /* R3: not enough approvals */
    REC_ERR_TIME = -8,     /* R3: time lock not over */
    REC_ERR_FULL = -9,     /* device table full */
    REC_ERR_DEVICE = -10   /* not an active device key */
} rec_status_t;

typedef enum { REC_IDLE = 0, REC_PENDING = 1, REC_THAWING = 2 } rec_state_t;

typedef enum {
    REC_EV_CLAIM = 1,
    REC_EV_APPROVE = 2,
    REC_EV_VETO = 3,
    REC_EV_CANCEL = 4,
    REC_EV_FINAL = 5
} rec_event_t;

/* What a signer signs: see rec_claim_msg / rec_device_msg. */
enum {
    REC_MSG_APPROVE = 1,
    REC_MSG_VETO = 2,
    REC_MSG_CANCEL = 3,
    REC_MSG_GATEWAY = 4,
    REC_MSG_DEV_ADD = 5,
    REC_MSG_DEV_DEL = 6
};

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's.
 * signer is the 32-byte hash of the signer's public key. */
typedef bool (*rec_verify_fn)(void *ctx, const uint8_t signer[REC_KEY], const uint8_t *msg,
                              uint32_t len, const uint8_t *sig, uint32_t sig_len);
/* notify(ctx, guardian, event, claim_id): called once per guardian. */
typedef void (*rec_notify_fn)(void *ctx, uint32_t guardian, rec_event_t ev, uint64_t claim_id);

typedef struct {
    uint64_t ticks_per_day;     /* > 0 */
    uint64_t timelock_ticks;    /* R3 without veto */
    uint64_t rate_window_ticks; /* R1 */
    uint32_t rate_max;          /* R1: 1..REC_RATE_CAP */
    uint8_t root[REC_KEY];      /* the account's root anchor (signs devices) */
    uint8_t guardian[REC_GUARDIANS][REC_KEY];
    uint8_t gateway[REC_KEY]; /* R5: gateway verification key */
} rec_cfg_t;

typedef struct {
    rec_cfg_t cfg;
    rec_verify_fn verify;
    void *vctx;
    rec_notify_fn notify;
    void *nctx;
    uint8_t device[REC_MAX_DEVICES][REC_KEY];
    bool dev_used[REC_MAX_DEVICES];
    uint64_t dev_seq; /* signed into device messages (no replay) */
    rec_state_t state;
    bool finalised; /* a claim has finalised at least once (THAW record) */
    /* the open claim */
    uint64_t claim_id; /* 0 before the first claim */
    uint8_t new_key[REC_KEY];
    uint64_t opened;
    uint8_t approved; /* bit i: guardian i approved */
    bool vetoed;
    /* R1 rate limit: open ticks, ring */
    uint64_t opens[REC_RATE_CAP];
    uint32_t n_opens;
    /* R5 */
    uint64_t fin_tick;
    bool gateway_ok;
} rec_account_t;

/* Defaults: time lock 3 days, 3 claims per 30 days. Keys are zeroed; the
 * caller fills root, guardian[] and gateway. */
void rec_cfg_default(rec_cfg_t *cfg, uint64_t ticks_per_day);
/* first_device: the first disposable device key under the root anchor. */
rec_status_t rec_init(rec_account_t *r, const rec_cfg_t *cfg, const uint8_t first_device[REC_KEY],
                      rec_verify_fn verify, void *vctx, rec_notify_fn notify, void *nctx);

/* Device keys: added and revoked only by the root anchor's signature over
 * rec_device_msg(REC_MSG_DEV_ADD / REC_MSG_DEV_DEL). */
#define REC_DEV_MSG (14u + 1u + REC_KEY + 8u + REC_KEY)
void rec_device_msg(const rec_account_t *r, uint8_t kind, const uint8_t key[REC_KEY],
                    uint8_t out[REC_DEV_MSG]);
rec_status_t rec_device_add(rec_account_t *r, const uint8_t key[REC_KEY], const uint8_t *sig,
                            uint32_t sig_len);
rec_status_t rec_device_revoke(rec_account_t *r, const uint8_t key[REC_KEY], const uint8_t *sig,
                               uint32_t sig_len);
bool rec_device_active(const rec_account_t *r, const uint8_t key[REC_KEY]);

/* "zxv/rec-msg/v1" || kind || root || claim_id (LE) || new key hash */
#define REC_CLAIM_MSG (14u + 1u + REC_KEY + 8u + REC_KEY)
void rec_claim_msg(const rec_account_t *r, uint8_t kind, uint8_t out[REC_CLAIM_MSG]);

rec_status_t rec_claim_open(rec_account_t *r, const uint8_t new_device_key_hash[REC_KEY],
                            uint64_t tick);
rec_status_t rec_approve(rec_account_t *r, uint32_t guardian, const uint8_t *sig, uint32_t sig_len);
/* device: an active device key (the old device) that signed. */
rec_status_t rec_veto(rec_account_t *r, const uint8_t device[REC_KEY], const uint8_t *sig,
                      uint32_t sig_len);
rec_status_t rec_cancel(rec_account_t *r, const uint8_t device[REC_KEY], const uint8_t *sig,
                        uint32_t sig_len);
rec_status_t rec_finalize(rec_account_t *r, uint64_t tick);
uint32_t rec_approvals(const rec_account_t *r);

/* R5: signed by cfg.gateway over rec_claim_msg(REC_MSG_GATEWAY) of the
 * finalised claim. Only while THAWING. */
rec_status_t rec_gateway_verify(rec_account_t *r, const uint8_t *sig, uint32_t sig_len);

bool rec_can_view(const rec_account_t *r);
bool rec_can_spend(const rec_account_t *r);
/* R5. PENDING: 0. Never finalised: everything. */
uint64_t rec_vbill_spendable(const rec_account_t *r, uint64_t vbill_balance, uint64_t tick);
uint64_t rec_liquid_allowance(const rec_account_t *r, uint64_t balance_at_finalise, uint64_t tick);

#endif /* ZXV_REC_RECOVERY_H */
