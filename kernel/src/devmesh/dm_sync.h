/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_sync.h — settings sync across the user's devices.
 *
 * Every setting is a field (16-bit key, up to DM_SET_VALUE bytes) with its
 * own vector clock: one counter per device that ever wrote it. A local write
 * bumps this device's counter. Merging a remote copy of a field:
 *
 *   remote clock dominates local   take the remote value
 *   local dominates or equal       keep the local value
 *   concurrent (neither dominates) last writer wins: the larger
 *                                  (timestamp, writer id, value bytes) wins,
 *                                  and the conflict is counted
 *   in every case                  the merged clock is the pointwise maximum
 *
 * The tie-break is a total order on (ts, writer, value), so the merge is
 * commutative, associative and idempotent: devices that have seen the same
 * writes hold the same settings, whatever order the syncs arrived in.
 * Timestamps only break ties between concurrent writes; a device with a
 * wrong clock cannot overwrite a write it has already seen, because the
 * vector clock decides causally ordered writes.
 *
 * Wire form (dm_sync_encode): u8 count, then per field
 *   u16 key | u8 len | value | u64 ts | writer id | u8 nvc | nvc x (id | u32)
 * Settings travel only inside authenticated devmesh sessions, between
 * devices that are ACTIVE in the roster.
 */
#ifndef ZXV_DM_SYNC_H
#define ZXV_DM_SYNC_H

#include "devmesh.h"

/* Well-known keys (hosts may use any other non-zero key). */
#define DM_KEY_THEME         1u
#define DM_KEY_LANGUAGE      2u
#define DM_KEY_ALLOW_METERED 3u /* "1": send big requests over metered links */
#define DM_KEY_LOCAL_MODEL   4u /* preferred on-device model name */
#define DM_KEY_HOME_MODEL    5u
#define DM_KEY_MARKET_LIMIT  6u /* decimal VFV minor units per day */

#define DM_SYNC_MAX                                                                                \
    (1u + DM_SET_MAX * (2u + 1u + DM_SET_VALUE + 8u + DM_ID_BYTES + 1u +                           \
                        DM_MAX_DEVICES * (DM_ID_BYTES + 4u)))

void dm_settings_init(dm_settings_t *s);

/* Local write by device `self` at time ts. */
dm_status_t dm_settings_set(dm_settings_t *s, const uint8_t self[DM_ID_BYTES], uint16_t key,
                            const uint8_t *value, uint8_t len, uint64_t ts);
const dm_field_t *dm_settings_get(const dm_settings_t *s, uint16_t key);

/* Merge one remote field; returns 1 if the local value changed, 0 if not,
 * or a negative dm_status_t. */
int dm_settings_merge_field(dm_settings_t *s, const dm_field_t *remote);

/* -1 a < b, 1 a > b, 0 equal, 2 concurrent. */
int dm_vc_compare(const dm_field_t *a, const dm_field_t *b);

int32_t dm_sync_encode(const dm_settings_t *s, uint8_t *out, uint32_t cap);
/* Merge every field of an encoded snapshot; returns fields changed or < 0. */
int32_t dm_sync_merge(dm_settings_t *s, const uint8_t *in, uint32_t len);

/* Mesh-level: write locally and push the snapshot to every live peer. */
dm_status_t dm_set(dm_mesh_t *m, uint16_t key, const uint8_t *value, uint8_t len, uint64_t now_ms);
dm_status_t dm_sync_push(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint64_t now_ms);

#endif /* ZXV_DM_SYNC_H */
