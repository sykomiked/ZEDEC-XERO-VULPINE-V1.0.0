/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwa.h — the hardware audit log: an append-only, hash-chained (SHA-256)
 * record of every step a hardware update takes (staged, tested, activated,
 * rolled back, rejected, quarantined, contract replaced).
 *
 * Each record carries a hexagram tag (lpres_hex.h). Quarantines carry a
 * FORBIDDEN tag, so the forbidden records of this log are the hardware
 * quarantine list; the other records are the normal history around them.
 * lpx_qlog stays forbidden-only by its own rule (X6), which is why updates
 * keep this sibling log instead of writing ordinary history into it.
 *
 *   hash(n) = SHA-256("zxv/hw-audit/v1" || seq (u64 LE) || tag || event ||
 *                     reason || region (u32 LE) || version (u32 LE) ||
 *                     subject[32] || prev[32])
 *
 * prev of the first record is 32 zero bytes. A full log refuses the append
 * and the caller must halt (fail closed): nothing changes hardware unless
 * the change can be recorded. Signing the head is the caller's job.
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef HWA_H
#define HWA_H

#include <stdbool.h>
#include <stdint.h>
#include "lpres_hex.h"

#define HWA_HASH 32u

/* Events (record.event, and the event callback of hwr.h). */
typedef enum {
    HWA_EV_STAGED = 1,      /* verified and loaded into the inactive slot */
    HWA_EV_TESTED = 2,      /* switched to and proven: probation begins */
    HWA_EV_ACTIVATED = 3,   /* committed: permanent, floor raised */
    HWA_EV_ROLLED_BACK = 4, /* back to the previous image (or to S0) */
    HWA_EV_REJECTED = 5,    /* refused before loading (signature, replay) */
    HWA_EV_QUARANTINED = 6, /* failed in hardware; payload blocked */
    HWA_EV_CONTRACT = 7     /* a function's conformance vectors replaced */
} hwa_event_t;

typedef struct {
    uint64_t seq;
    lpx_t tag;
    uint8_t event;   /* hwa_event_t */
    uint8_t reason;  /* the region's reason code (hwr_qreason_t), 0 if none */
    uint32_t region; /* region id, or function id for HWA_EV_CONTRACT */
    uint32_t version;
    uint8_t subject[32]; /* SHA-256 of the image header */
    uint8_t prev[HWA_HASH];
    uint8_t hash[HWA_HASH];
} hwa_rec_t;

typedef struct {
    hwa_rec_t *rec;
    uint32_t cap, n;
    uint8_t head[HWA_HASH]; /* hash of the last record (zeros: empty) */
} hwa_log_t;

void hwa_init(hwa_log_t *log, hwa_rec_t *storage, uint32_t cap);
/* false when full: the caller halts. */
bool hwa_append(hwa_log_t *log, lpx_t tag, uint8_t event, uint8_t reason, uint32_t region,
                uint32_t version, const uint8_t subject[32]);
/* Recompute every link; false if any record was altered. */
bool hwa_verify(const hwa_log_t *log);
const hwa_rec_t *hwa_get(const hwa_log_t *log, uint32_t index);
/* Records whose tag is forbidden (the quarantines). */
uint32_t hwa_forbidden(const hwa_log_t *log);

#endif /* HWA_H */
