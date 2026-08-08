/* abupdate.h — A/B signed-package update with probation + auto-rollback
 *
 * The ZXV-native form of "upgrade discipline": an application is held in
 * two slots (A/B). A new signed package is verified (Ed25519 root key),
 * staged into the INACTIVE slot, and put ON PROBATION — it does not
 * become active until it proves itself (ab_confirm after a healthy run).
 * If probation expires without confirmation, or the new version fails to
 * verify/load, the system AUTO-ROLLS-BACK to the last known-good slot.
 *
 * This mirrors the cellular incarnation model: a higher incarnation
 * replaces a running one only after it authenticates and runs cleanly;
 * otherwise the previous incarnation keeps serving. Because all state
 * transitions go through ZXVFS's journaled writes, an update interrupted
 * by power loss recovers to a consistent A-or-B state — never a
 * half-applied one.
 *
 * State + slots live as files in ZXVFS:
 *   "app.state"  — ab_state_t (below)
 *   "app.slotA"  — the .zsp bytes of slot A
 *   "app.slotB"  — the .zsp bytes of slot B
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV update-discipline slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ABUPDATE_H
#define ZXV_ABUPDATE_H

#include <stdint.h>
#include <stdbool.h>
#include "../zxvfs/zxvfs.h"

#define AB_STATE_MAGIC   0x41425545u   /* 'ABUE' — state format v2 (adds rollback_floor) */
#define AB_SLOT_NONE     0xFF
#define AB_DEFAULT_PROBATION 1         /* runs to survive before auto-confirm option */

typedef struct {
    uint32_t magic;
    uint8_t  active_slot;     /* 0=A, 1=B — the known-good slot in use */
    uint8_t  probation_slot;  /* 0/1 on probation, or AB_SLOT_NONE */
    uint8_t  _pad0[2];
    uint32_t version[2];      /* authenticated version per slot (from the ZSP header) */
    uint32_t probation_remaining; /* ab_boot_tick decrements; 0 => rollback */
    uint32_t rollbacks;       /* stats: auto-rollbacks performed */
    uint32_t promotions;      /* stats: confirmed promotions */
    uint32_t rollback_floor;  /* monotonic anti-rollback floor (P0-6): no v2 package
                               * below this version may ever be staged again */
} ab_state_t;

typedef enum {
    AB_OK = 0,
    AB_ERR_IO       = -1,
    AB_ERR_VERIFY   = -2,   /* new package failed signature/hash — rejected */
    AB_ERR_NONE     = -3,   /* nothing on probation to confirm/rollback */
    AB_ERR_STATE    = -4,
    AB_ERR_ROLLBACK = -5,   /* v2 package version below the anti-rollback floor */
} ab_result_t;

/* Load A/B state from ZXVFS, or initialize it (slot A active) if absent. */
ab_result_t ab_init(zxvfs_t *fs, ab_state_t *st);

/* Stage a new signed package as an update: verify it against root_pubkey,
 * write it to the INACTIVE slot, and put that slot on probation. On a
 * verification failure NOTHING changes (the active slot keeps serving).
 * `zsp`/`len` are the raw signed-package bytes. */
ab_result_t ab_stage_update(zxvfs_t *fs, ab_state_t *st,
                            const uint8_t *zsp, uint32_t len,
                            const uint8_t root_pubkey[32]);

/* Return the .zsp bytes of the slot that should RUN next: the probation
 * slot if one is pending (so it can prove itself), else the active slot.
 * Writes the slot index to *slot_out. */
ab_result_t ab_slot_to_run(zxvfs_t *fs, ab_state_t *st,
                           uint8_t *buf, uint32_t max, uint32_t *out_len,
                           uint8_t *slot_out);

/* Confirm the probation slot as the new known-good active slot. */
ab_result_t ab_confirm(zxvfs_t *fs, ab_state_t *st);

/* Roll back: discard the probation slot, keep the active slot. */
ab_result_t ab_rollback(zxvfs_t *fs, ab_state_t *st);

/* One probation "tick" (call once per boot/run of the probation slot).
 * Decrements probation_remaining; when it reaches 0 without a confirm,
 * performs an automatic rollback. Returns AB_OK; sets *did_rollback. */
ab_result_t ab_boot_tick(zxvfs_t *fs, ab_state_t *st, bool *did_rollback);

#endif /* ZXV_ABUPDATE_H */
