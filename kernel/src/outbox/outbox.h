/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* outbox.h — pay while offline, settle when back, never spend twice.
 *
 * A device that may lose its connection (a till, a phone in a basement, a
 * market stall) is given an OFFLINE GRANT while it is online: money moved
 * out of the holder's account into escrow before anything is spent. While
 * offline the device signs entries against the grant and queues them in its
 * outbox; the payee can check each one on the spot with no network. When
 * either side reconnects, the entries are submitted to the ledger, which
 * pays them out of the escrow. Money is never spent twice because the
 * escrow can only be paid out once, whoever submits and in whatever order.
 *
 * THE GRANT (ledger side, online)
 * -------------------------------
 *   G1  obx_ledger_grant moves `limit` from the holder's pay_ledger account
 *       to the escrow account in one posting. The holder can no longer spend
 *       that money online; pay_ledger refuses the grant if it is not there.
 *   G2  The grant names the device by the hash of its ML-DSA-87 public key
 *       (hwkey.h: normally a key sealed in the TPM or Secure Enclave), caps
 *       the number of entries, carries an expiry tick, and is signed by the
 *       ledger's own key, so a payee can verify it offline.
 *   G3  A device flagged for fraud (D2-D5) gets no new grant.
 *
 * THE ENTRY (device side, offline)
 * --------------------------------
 *   E1  Entry n of a grant carries: grant id, n (1, 2, 3, ...), amount,
 *       running total of amounts 1..n, payee, a device tick, and the hash of
 *       entry n-1 (the grant id for n = 1), signed by the device key.
 *   E2  obx_device_spend refuses a spend that would take the running total
 *       over the limit, or n over the entry cap.
 *   E3  WRITE AHEAD: the device's new state is handed to the host's persist
 *       callback BEFORE the entry is returned to be handed to the payee. If
 *       the write fails, the spend is undone and nothing leaves the device;
 *       after a crash, no number has been handed out that the outbox forgot.
 *   E4  obx_verify_offline is what a payee runs with no network: grant
 *       signed by the ledger, device key matches the grant, entry signed by
 *       the device, amount and running total within the limit.
 *
 * SETTLEMENT (ledger side, when anyone reconnects)
 * ------------------------------------------------
 *   S1  Device or payee may submit an entry; order does not matter.
 *   S2  ACCEPTED: escrow -> payee, exactly the entry's amount, once.
 *   S3  DUPLICATE: the same entry again (same number, same hash). Nothing
 *       moves; the submitter treats it as settled. This makes retries safe.
 *   S4  Entries that are valid but cannot be paid yet (payee has no account
 *       for the asset) are refused with NO_PAYEE and are NOT recorded, so
 *       they can be submitted again later.
 *
 * DOUBLE-SPEND DEFENCES (each is fail-closed: nothing moves)
 * ----------------------------------------------------------
 *   D1  The escrow holds the whole limit before the first spend (G1), and
 *       the ledger never pays more than the limit (or the closing total,
 *       C1) out of one grant: the sum of accepted amounts is checked on
 *       every submission.
 *   D2  CONFLICT: two different entries with the same number. Only one can
 *       ever be accepted. The second is proof the device signed twice (for
 *       example after its state was restored from a backup); the device is
 *       flagged.
 *   D3  CONFLICT: an entry whose running total or previous-hash link
 *       disagrees with a neighbour already accepted. Flagged.
 *   D4  OVER_LIMIT / BAD_ENTRY: a signed entry claiming more than the grant
 *       allows, or a running total smaller than its own amount. Flagged.
 *   D5  An entry numbered after the device's own signed close (C1). Flagged.
 *
 * CLOSING AND EXPIRY
 * ------------------
 *   C1  obx_device_close signs (last number, running total, last hash). The
 *       ledger refunds limit - running total to the holder at once and keeps
 *       the rest in escrow for entries still to arrive.
 *   C2  At expiry + grace, whatever is still escrowed for the grant goes
 *       back to the holder and the grant is finished. Payees must submit
 *       before then; that window is the operator's policy.
 *   C3  obx_ledger_check: the escrow account's balance equals the sum over
 *       live grants of (covered - settled), exactly.
 *
 * ROLLBACK GUARD (device side, when the chip has a monotonic counter)
 * ------------------------------------------------------------------
 *   R1  obx_device_guard binds the outbox to a counter the security chip
 *       keeps for this grant (a TPM NV counter; obx_counter_t). The value
 *       read is stored in the outbox state and persisted. Guard right after
 *       obx_device_init, before anything else is persisted, so no copy of
 *       the state exists unguarded.
 *   R2  obx_device_spend_guarded and obx_device_close_guarded read the
 *       counter first. A value different from the stored one means this
 *       state is not the latest (restored from a backup, or a write that
 *       never finished): OBX_ERR_ROLLBACK, nothing is signed.
 *   R3  After signing, the counter is incremented, then the new value is
 *       written ahead with the rest of the state (E3). The chip only counts
 *       up, so every older copy of the state is refused from then on.
 *   R4  The unguarded obx_device_spend / obx_device_close refuse a guarded
 *       outbox (OBX_ERR_STATE).
 *   A crash or failed write between R3's increment and the write leaves the
 *   saved state one behind the chip: the outbox refuses to sign again
 *   (fail-closed). Nothing was handed to a payee (E3), so no money is at
 *   risk; the holder gets the unspent limit back at expiry (C2).
 *
 * HONEST LIMITS
 * -------------
 *   With the guard (R1-R4), a restored backup cannot sign. Without it (no
 *   counter on this machine: the Secure Enclave and the Windows Platform
 *   Crypto Provider expose none to applications), a device restored from an
 *   old backup can sign two different entries with the same number. The
 *   ledger still never pays more than the escrow (D1) and only one of them
 *   is paid (D2), but the payee holding the other is not paid by the
 *   ledger: their recourse is the conflict evidence and the flagged device.
 *   The guard defends against a restored file; it cannot stop someone who
 *   controls the chip itself (a TPM cleared, or a simulator in its place).
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_OUTBOX_H
#define ZXV_OUTBOX_H

#include <stdint.h>
#include <stdbool.h>
#include "../pay/pay_ledger.h"
#include "../pqsec/pq_matrix_algs.h"

#define OBX_MAX_ENTRIES 64u /* entries per grant */
#define OBX_MAX_GRANTS  16u /* live grants per ledger */
#define OBX_MAX_FLAGGED 32u
#define OBX_PK_BYTES    PQM_MLDSA87_PK_BYTES
#define OBX_SIG_BYTES   PQM_MLDSA87_SIG_BYTES
#define OBX_ID_BYTES    32u

typedef enum {
    OBX_OK = 0,
    OBX_ERR_ARG = -1,
    OBX_ERR_SIG = -2,       /* a signature does not verify          */
    OBX_ERR_LIMIT = -3,     /* E2: over the limit or the entry cap   */
    OBX_ERR_STATE = -4,     /* closed, finished, or wrong grant      */
    OBX_ERR_PERSIST = -5,   /* E3: the write-ahead record failed     */
    OBX_ERR_SIGNER = -6,    /* the signing callback failed           */
    OBX_ERR_LEDGER = -7,    /* pay_ledger refused a posting          */
    OBX_ERR_FULL = -8,      /* no free grant slot                    */
    OBX_ERR_FLAGGED = -9,   /* G3: device flagged for fraud          */
    OBX_ERR_ROLLBACK = -10, /* R2: this state is older than the chip  */
    OBX_ERR_COUNTER = -11   /* the chip's counter could not be used   */
} obx_status_t;

/* What the ledger says about a submitted entry. */
typedef enum {
    OBX_R_ACCEPTED = 0,
    OBX_R_DUPLICATE = 1, /* S3: already settled, nothing moved */
    OBX_R_UNKNOWN_GRANT = 2,
    OBX_R_BAD_SIG = 3,
    OBX_R_BAD_SEQ = 4,
    OBX_R_EXPIRED = 5,
    OBX_R_CONFLICT = 6,   /* D2, D3, D5 */
    OBX_R_OVER_LIMIT = 7, /* D1, D4 */
    OBX_R_BAD_ENTRY = 8,  /* D4 */
    OBX_R_NO_PAYEE = 9,   /* S4: not recorded, retry later */
    OBX_R_LEDGER = 10
} obx_result_t;

/* Signs msg with the caller's key (normally hwk_sign over a sealed key)
 * under the context string ctx. Returns 0 on success. */
typedef int (*obx_sign_fn)(void *sign_ctx, const uint8_t *msg, uint32_t msg_len, const uint8_t *ctx,
                           uint32_t ctx_len, uint8_t sig[OBX_SIG_BYTES]);

typedef struct {
    uint8_t id[OBX_ID_BYTES];
    uint8_t ledger_id[OBX_ID_BYTES]; /* SHAKE256 of the ledger's public key */
    uint8_t device_id[OBX_ID_BYTES]; /* SHAKE256 of the device's public key */
    uint32_t holder;                 /* pay_ledger owner id                 */
    uint16_t asset;                  /* pay_ledger asset index              */
    uint64_t limit;
    uint32_t max_entries;
    uint64_t issued_tick;
    uint64_t expires_tick;
    uint8_t sig[OBX_SIG_BYTES];
} obx_grant_t;

typedef struct {
    uint8_t grant_id[OBX_ID_BYTES];
    uint32_t seq; /* 1..max_entries */
    uint64_t amount;
    uint64_t cumulative;
    uint32_t payee; /* pay_ledger owner id */
    uint64_t tick;  /* device time; informational */
    uint8_t prev[OBX_ID_BYTES];
    uint8_t sig[OBX_SIG_BYTES];
} obx_entry_t;

typedef struct {
    uint8_t grant_id[OBX_ID_BYTES];
    uint32_t last_seq; /* 0 = nothing spent */
    uint64_t cumulative;
    uint8_t last_hash[OBX_ID_BYTES];
    uint8_t sig[OBX_SIG_BYTES];
} obx_close_t;

/* Hash of an entry's signed fields (E1); also its identity at the ledger. */
void obx_entry_hash(const obx_entry_t *e, uint8_t h[OBX_ID_BYTES]);
/* Hash of a public key: the id a grant names. */
void obx_key_id(const uint8_t pk[OBX_PK_BYTES], uint8_t id[OBX_ID_BYTES]);

/* E4: everything a payee can check with no network. */
obx_status_t obx_verify_offline(const uint8_t ledger_pk[OBX_PK_BYTES], const obx_grant_t *g,
                                const uint8_t device_pk[OBX_PK_BYTES], const obx_entry_t *e);

/* ---- device ----------------------------------------------------------- */
typedef enum { OBX_E_PENDING = 0, OBX_E_SETTLED = 1, OBX_E_REJECTED = 2 } obx_entry_state_t;

typedef struct {
    obx_grant_t grant;
    uint32_t next_seq;
    uint64_t cumulative;
    uint8_t prev[OBX_ID_BYTES];
    uint32_t n;                     /* entries queued */
    obx_entry_t e[OBX_MAX_ENTRIES]; /* e[i].seq == i + 1 */
    uint8_t state[OBX_MAX_ENTRIES]; /* obx_entry_state_t */
    uint8_t result[OBX_MAX_ENTRIES];
    bool closed;
    obx_close_t close;
    bool guarded; /* R1 */
    uint64_t ctr; /* the chip counter's value as of this state */
} obx_device_t;

/* R1: a monotonic counter held by the security chip, one per grant (`id`
 * is the grant id). Both return 0 on success. bump adds one and reports
 * the new value. */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, const uint8_t id[OBX_ID_BYTES], uint64_t *value);
    int (*bump)(void *ctx, const uint8_t id[OBX_ID_BYTES], uint64_t *value_after);
} obx_counter_t;

/* E3: write `dev` durably. Return 0 only once it is on stable storage. */
typedef int (*obx_persist_fn)(void *persist_ctx, const obx_device_t *dev);

/* Start an outbox on a grant: checks the ledger's signature and that the
 * grant names this device's key. */
obx_status_t obx_device_init(obx_device_t *dev, const obx_grant_t *g,
                             const uint8_t ledger_pk[OBX_PK_BYTES],
                             const uint8_t device_pk[OBX_PK_BYTES]);

/* E1-E3: sign the next entry, persist, then copy it to *out for the payee.
 * Refuses a guarded outbox (R4). */
obx_status_t obx_device_spend(obx_device_t *dev, uint32_t payee, uint64_t amount, uint64_t tick,
                              obx_sign_fn sign, void *sign_ctx, obx_persist_fn persist,
                              void *persist_ctx, obx_entry_t *out);

/* R1: bind a fresh outbox (nothing spent, not closed) to the chip counter
 * for its grant and persist. */
obx_status_t obx_device_guard(obx_device_t *dev, const obx_counter_t *ctr, obx_persist_fn persist,
                              void *persist_ctx);

/* R2-R3: obx_device_spend and obx_device_close for a guarded outbox. */
obx_status_t obx_device_spend_guarded(obx_device_t *dev, const obx_counter_t *ctr, uint32_t payee,
                                      uint64_t amount, uint64_t tick, obx_sign_fn sign,
                                      void *sign_ctx, obx_persist_fn persist, void *persist_ctx,
                                      obx_entry_t *out);
obx_status_t obx_device_close_guarded(obx_device_t *dev, const obx_counter_t *ctr, obx_sign_fn sign,
                                      void *sign_ctx, obx_persist_fn persist, void *persist_ctx,
                                      obx_close_t *out);

/* Index of the first pending entry at or after `from`, or -1. */
int32_t obx_device_next_pending(const obx_device_t *dev, uint32_t from);

/* Record the ledger's answer for entry `seq`. ACCEPTED and DUPLICATE settle
 * it; NO_PAYEE and LEDGER leave it pending; anything else rejects it. */
obx_status_t obx_device_ack(obx_device_t *dev, uint32_t seq, obx_result_t r, obx_persist_fn persist,
                            void *persist_ctx);

/* C1: stop spending on this grant and sign the closing statement. Refuses
 * a guarded outbox that is not yet closed (R4). */
obx_status_t obx_device_close(obx_device_t *dev, obx_sign_fn sign, void *sign_ctx,
                              obx_persist_fn persist, void *persist_ctx, obx_close_t *out);

/* ---- ledger ----------------------------------------------------------- */
typedef struct {
    bool used;
    bool closed;
    bool finished; /* C1 fully settled, or C2: escrow returned */
    obx_grant_t grant;
    uint8_t device_pk[OBX_PK_BYTES];
    uint32_t holder_acct;
    uint64_t covered; /* limit, or the closing total after C1 */
    uint64_t settled; /* sum of accepted amounts */
    uint32_t close_last;
    uint8_t close_hash[OBX_ID_BYTES];
    uint8_t seen[OBX_MAX_ENTRIES];
    uint64_t amt[OBX_MAX_ENTRIES];
    uint64_t cum[OBX_MAX_ENTRIES];
    uint8_t hash[OBX_MAX_ENTRIES][OBX_ID_BYTES];
    uint8_t prev[OBX_MAX_ENTRIES][OBX_ID_BYTES];
} obx_slot_t;

typedef struct {
    pay_ledger_t *L;
    uint16_t asset;
    uint32_t escrow_owner;
    uint32_t escrow_acct;
    uint8_t seed[32]; /* derives grant ids and posting identifiers */
    uint8_t pk[OBX_PK_BYTES];
    uint8_t id[OBX_ID_BYTES];
    obx_sign_fn sign;
    void *sign_ctx;
    uint64_t grace;
    uint64_t n_grants;
    uint64_t n_posts;
    uint64_t conflicts;
    obx_slot_t slot[OBX_MAX_GRANTS];
    uint8_t flagged[OBX_MAX_FLAGGED][OBX_ID_BYTES];
    uint32_t n_flagged;
} obx_ledger_t;

/* Opens the escrow account (owner escrow_owner) for `asset` on L. */
obx_status_t obx_ledger_init(obx_ledger_t *lg, pay_ledger_t *L, uint16_t asset,
                             uint32_t escrow_owner, const uint8_t seed[32],
                             const uint8_t ledger_pk[OBX_PK_BYTES], obx_sign_fn sign,
                             void *sign_ctx, uint64_t grace_ticks);

/* G1-G3. */
obx_status_t obx_ledger_grant(obx_ledger_t *lg, uint32_t holder,
                              const uint8_t device_pk[OBX_PK_BYTES], uint64_t limit,
                              uint32_t max_entries, uint64_t now, uint64_t expires,
                              obx_grant_t *out);

/* S1-S4, D1-D5. Returns OBX_OK with *res set, or an error for bad args. */
obx_status_t obx_ledger_submit(obx_ledger_t *lg, const obx_entry_t *e, uint64_t now,
                               obx_result_t *res);

/* C1. */
obx_status_t obx_ledger_close(obx_ledger_t *lg, const obx_close_t *c, uint64_t now);

/* C2: finish every grant past expiry + grace. Returns how many finished. */
uint32_t obx_ledger_expire(obx_ledger_t *lg, uint64_t now);

/* C3. */
bool obx_ledger_check(const obx_ledger_t *lg);

bool obx_ledger_flagged(const obx_ledger_t *lg, const uint8_t device_id[OBX_ID_BYTES]);

#endif /* ZXV_OUTBOX_H */
