/* fs.h — Tri-Space Storage and State (O2)
 *
 * O2 FS provides tri-space storage transactions, snapshots, and recovery
 * for the ZXV kernel. It manages file system objects across S+ (primary),
 * S- (negative/backup), and S0 (neutral/mediating) spaces.
 *
 * Key responsibilities:
 *   - Transactional file operations with commit/rollback
 *   - Tri-space replication: S+ primary, S- backup, S0 mediator
 *   - Snapshot creation and restoration
 *   - Recovery from S- or S0 on S+ failure
 *   - Integrity verification via CRC-32
 *   - Bounded in-memory file system for kernel state
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== FS Constants ===== */

#define FS_MAX_NODES          256
#define FS_MAX_TRANS         128
#define FS_MAX_SNAPSHOTS      32
#define FS_MAX_NAME_LEN       32
#define FS_MAX_PATH_LEN       64
#define FS_MAX_DATA_LEN      256
#define FS_MAX_TX_OPS         16

/* ===== FS Node Type ===== */

typedef enum {
    FS_NODE_FILE      = 0,
    FS_NODE_DIR       = 1,
    FS_NODE_SYMLINK   = 2,
    FS_NODE_DEVICE    = 3,
} fs_node_type_t;

/* ===== FS Space ===== */

typedef enum {
    FS_SPACE_S_PLUS   = 0,   /* primary */
    FS_SPACE_S_MINUS  = 1,   /* backup/negative */
    FS_SPACE_S_ZERO   = 2,   /* mediator/neutral */
} fs_space_t;

/* ===== Transaction Status ===== */

typedef enum {
    FS_TX_ACTIVE    = 0,
    FS_TX_COMMITTED = 1,
    FS_TX_ABORTED   = 2,
    FS_TX_PENDING   = 3,
} fs_tx_status_t;

/* ===== Operation Type ===== */

typedef enum {
    FS_OP_CREATE    = 0,
    FS_OP_WRITE     = 1,
    FS_OP_DELETE    = 2,
    FS_OP_MKDIR     = 3,
    FS_OP_RENAME    = 4,
} fs_op_type_t;

/* ===== FS Node ===== */

typedef struct fs_node {
    uint32_t id;
    char name[FS_MAX_NAME_LEN];
    char path[FS_MAX_PATH_LEN];
    fs_node_type_t type;
    uint32_t parent_id;          /* 0 = root */
    uint8_t data[FS_MAX_DATA_LEN];
    uint32_t data_len;
    uint32_t crc;                /* CRC-32 of data */
    bool exists_in_s_plus;
    bool exists_in_s_minus;
    bool exists_in_s_zero;
    uint64_t version;            /* increments on each write */
} fs_node_t;

/* ===== Transaction Operation ===== */

typedef struct fs_tx_op {
    fs_op_type_t type;
    uint32_t target_node_id;
    char name[FS_MAX_NAME_LEN];
    uint8_t data[FS_MAX_DATA_LEN];
    uint32_t data_len;
} fs_tx_op_t;

/* ===== Transaction ===== */

typedef struct fs_transaction {
    uint32_t id;
    fs_tx_status_t status;
    fs_tx_op_t ops[FS_MAX_TX_OPS];
    uint32_t op_count;
    uint32_t snapshot_id;        /* snapshot for rollback */
} fs_transaction_t;

/* ===== Snapshot ===== */

typedef struct fs_snapshot {
    uint32_t id;
    char label[FS_MAX_NAME_LEN];
    fs_node_t nodes[FS_MAX_NODES];
    uint32_t node_count;
    uint64_t total_version;
    bool valid;
} fs_snapshot_t;

/* ===== FS Registry ===== */

typedef struct fs_registry {
    fs_node_t nodes[FS_MAX_NODES];
    uint32_t node_count;
    uint32_t next_node_id;

    fs_transaction_t transactions[FS_MAX_TRANS];
    uint32_t tx_count;
    uint32_t next_tx_id;

    fs_snapshot_t snapshots[FS_MAX_SNAPSHOTS];
    uint32_t snapshot_count;
    uint32_t next_snapshot_id;

    uint64_t total_version;      /* global version counter */
} fs_registry_t;

/* ===== API ===== */

void fs_registry_init(fs_registry_t *reg);

/* Node Management */
int32_t fs_create_node(fs_registry_t *reg, const char *path, fs_node_type_t type,
                       uint32_t parent_id);
fs_node_t *fs_get_node(fs_registry_t *reg, uint32_t idx);
int32_t fs_find_node(fs_registry_t *reg, const char *path);
bool fs_write_data(fs_registry_t *reg, uint32_t idx, const uint8_t *data,
                   uint32_t data_len);
bool fs_delete_node(fs_registry_t *reg, uint32_t idx);

/* Tri-Space Replication */
bool fs_replicate_to_s_minus(fs_registry_t *reg, uint32_t idx);
bool fs_replicate_to_s_zero(fs_registry_t *reg, uint32_t idx);
bool fs_recover_from_s_minus(fs_registry_t *reg, uint32_t idx);
bool fs_recover_from_s_zero(fs_registry_t *reg, uint32_t idx);
bool fs_verify_integrity(fs_registry_t *reg, uint32_t idx);

/* Snapshots */
int32_t fs_create_snapshot(fs_registry_t *reg, const char *label);
bool fs_restore_snapshot(fs_registry_t *reg, uint32_t snap_idx);

/* Transactions */
int32_t fs_begin_transaction(fs_registry_t *reg);
bool fs_tx_add_create(fs_registry_t *reg, uint32_t tx_idx, const char *path,
                      fs_node_type_t type, uint32_t parent_id);
bool fs_tx_add_write(fs_registry_t *reg, uint32_t tx_idx, uint32_t node_idx,
                     const uint8_t *data, uint32_t data_len);
bool fs_tx_add_delete(fs_registry_t *reg, uint32_t tx_idx, uint32_t node_idx);
bool fs_tx_commit(fs_registry_t *reg, uint32_t tx_idx);
bool fs_tx_abort(fs_registry_t *reg, uint32_t tx_idx);
fs_transaction_t *fs_get_transaction(fs_registry_t *reg, uint32_t idx);

/* Queries */
uint32_t fs_count_by_type(fs_registry_t *reg, fs_node_type_t type);
uint32_t fs_count_by_space(fs_registry_t *reg, fs_space_t space);
uint32_t fs_count_tx_by_status(fs_registry_t *reg, fs_tx_status_t status);

/* Name Functions */
const char *fs_node_type_name(fs_node_type_t type);
const char *fs_space_name(fs_space_t space);
const char *fs_tx_status_name(fs_tx_status_t status);
const char *fs_op_type_name(fs_op_type_t type);

#endif /* FS_H */
