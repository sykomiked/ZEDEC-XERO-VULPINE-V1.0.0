/* fs.c — Tri-Space Storage and State (O2)
 *
 * Implements transactional file operations, tri-space replication,
 * snapshots, and recovery.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "fs.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_equal(const char *a, const char *b, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return false;
}

static uint32_t fs_crc32(const uint8_t *data, uint32_t len) {
    if (!data || len == 0) return 0;
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ===== Registry Init ===== */

void fs_registry_init(fs_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
    reg->next_node_id = 1;
    reg->next_tx_id = 1;
    reg->next_snapshot_id = 1;
}

/* ===== Node Management ===== */

int32_t fs_create_node(fs_registry_t *reg, const char *path, fs_node_type_t type,
                       uint32_t parent_id) {
    if (!reg || !path) return -1;
    if (reg->node_count >= FS_MAX_NODES) return -1;

    /* Check for duplicate path */
    for (uint32_t i = 0; i < reg->node_count; i++) {
        if (str_equal(reg->nodes[i].path, path, FS_MAX_PATH_LEN))
            return (int32_t)i;
    }

    fs_node_t *n = &reg->nodes[reg->node_count];
    ev_memset(n, 0, sizeof(*n));
    n->id = reg->next_node_id++;
    copy_str(n->path, path, FS_MAX_PATH_LEN);

    /* Extract name from path (last component) */
    uint32_t path_len = 0;
    while (path_len < FS_MAX_PATH_LEN && path[path_len]) path_len++;
    uint32_t last_slash = 0;
    for (uint32_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_slash = i + 1;
    }
    copy_str(n->name, path + last_slash, FS_MAX_NAME_LEN);

    n->type = type;
    n->parent_id = parent_id;
    n->data_len = 0;
    n->crc = 0;
    n->version = 0;
    n->exists_in_s_plus = true;
    n->exists_in_s_minus = false;
    n->exists_in_s_zero = false;

    reg->total_version++;
    return (int32_t)reg->node_count++;
}

fs_node_t *fs_get_node(fs_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->node_count) return NULL;
    return &reg->nodes[idx];
}

int32_t fs_find_node(fs_registry_t *reg, const char *path) {
    if (!reg || !path) return -1;
    for (uint32_t i = 0; i < reg->node_count; i++) {
        if (str_equal(reg->nodes[i].path, path, FS_MAX_PATH_LEN))
            return (int32_t)i;
    }
    return -1;
}

bool fs_write_data(fs_registry_t *reg, uint32_t idx, const uint8_t *data,
                   uint32_t data_len) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n || !data) return false;
    if (data_len > FS_MAX_DATA_LEN) return false;

    ev_memcpy(n->data, data, data_len);
    n->data_len = data_len;
    n->crc = fs_crc32(data, data_len);
    n->version++;
    reg->total_version++;
    return true;
}

bool fs_delete_node(fs_registry_t *reg, uint32_t idx) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n) return false;
    n->exists_in_s_plus = false;
    n->data_len = 0;
    n->crc = 0;
    reg->total_version++;
    return true;
}

/* ===== Tri-Space Replication ===== */

bool fs_replicate_to_s_minus(fs_registry_t *reg, uint32_t idx) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n || !n->exists_in_s_plus) return false;
    n->exists_in_s_minus = true;
    return true;
}

bool fs_replicate_to_s_zero(fs_registry_t *reg, uint32_t idx) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n || !n->exists_in_s_plus) return false;
    n->exists_in_s_zero = true;
    return true;
}

bool fs_recover_from_s_minus(fs_registry_t *reg, uint32_t idx) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n || !n->exists_in_s_minus) return false;
    n->exists_in_s_plus = true;
    return true;
}

bool fs_recover_from_s_zero(fs_registry_t *reg, uint32_t idx) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n || !n->exists_in_s_zero) return false;
    n->exists_in_s_plus = true;
    return true;
}

bool fs_verify_integrity(fs_registry_t *reg, uint32_t idx) {
    fs_node_t *n = fs_get_node(reg, idx);
    if (!n) return false;
    if (!n->exists_in_s_plus) return false;
    if (n->data_len == 0) return true;  /* empty is valid */
    uint32_t expected = fs_crc32(n->data, n->data_len);
    return expected == n->crc;
}

/* ===== Snapshots ===== */

int32_t fs_create_snapshot(fs_registry_t *reg, const char *label) {
    if (!reg) return -1;
    if (reg->snapshot_count >= FS_MAX_SNAPSHOTS) return -1;

    fs_snapshot_t *snap = &reg->snapshots[reg->snapshot_count];
    ev_memset(snap, 0, sizeof(*snap));
    snap->id = reg->next_snapshot_id++;
    if (label)
        copy_str(snap->label, label, FS_MAX_NAME_LEN);

    /* Copy all nodes */
    for (uint32_t i = 0; i < reg->node_count; i++) {
        ev_memcpy(&snap->nodes[i], &reg->nodes[i], sizeof(fs_node_t));
    }
    snap->node_count = reg->node_count;
    snap->total_version = reg->total_version;
    snap->valid = true;

    return (int32_t)reg->snapshot_count++;
}

bool fs_restore_snapshot(fs_registry_t *reg, uint32_t snap_idx) {
    if (!reg || snap_idx >= reg->snapshot_count) return false;
    fs_snapshot_t *snap = &reg->snapshots[snap_idx];
    if (!snap->valid) return false;

    /* Restore all nodes */
    reg->node_count = snap->node_count;
    for (uint32_t i = 0; i < snap->node_count; i++) {
        ev_memcpy(&reg->nodes[i], &snap->nodes[i], sizeof(fs_node_t));
    }
    reg->total_version = snap->total_version;
    return true;
}

/* ===== Transactions ===== */

int32_t fs_begin_transaction(fs_registry_t *reg) {
    if (!reg) return -1;
    if (reg->tx_count >= FS_MAX_TRANS) return -1;

    fs_transaction_t *tx = &reg->transactions[reg->tx_count];
    ev_memset(tx, 0, sizeof(*tx));
    tx->id = reg->next_tx_id++;
    tx->status = FS_TX_ACTIVE;
    tx->op_count = 0;

    /* Create snapshot for rollback */
    int32_t snap_idx = fs_create_snapshot(reg, "tx-rollback");
    if (snap_idx >= 0) {
        tx->snapshot_id = (uint32_t)snap_idx;
    }

    return (int32_t)reg->tx_count++;
}

bool fs_tx_add_create(fs_registry_t *reg, uint32_t tx_idx, const char *path,
                      fs_node_type_t type, uint32_t parent_id) {
    if (!reg || !path) return false;
    fs_transaction_t *tx = fs_get_transaction(reg, tx_idx);
    if (!tx || tx->status != FS_TX_ACTIVE) return false;
    if (tx->op_count >= FS_MAX_TX_OPS) return false;

    fs_tx_op_t *op = &tx->ops[tx->op_count++];
    ev_memset(op, 0, sizeof(*op));
    op->type = FS_OP_CREATE;
    copy_str(op->name, path, FS_MAX_NAME_LEN);
    op->data_len = type;  /* reuse data_len for type */
    op->target_node_id = parent_id;
    return true;
}

bool fs_tx_add_write(fs_registry_t *reg, uint32_t tx_idx, uint32_t node_idx,
                     const uint8_t *data, uint32_t data_len) {
    if (!reg || !data) return false;
    fs_transaction_t *tx = fs_get_transaction(reg, tx_idx);
    if (!tx || tx->status != FS_TX_ACTIVE) return false;
    if (tx->op_count >= FS_MAX_TX_OPS) return false;
    if (data_len > FS_MAX_DATA_LEN) return false;

    fs_tx_op_t *op = &tx->ops[tx->op_count++];
    ev_memset(op, 0, sizeof(*op));
    op->type = FS_OP_WRITE;
    op->target_node_id = node_idx;
    ev_memcpy(op->data, data, data_len);
    op->data_len = data_len;
    return true;
}

bool fs_tx_add_delete(fs_registry_t *reg, uint32_t tx_idx, uint32_t node_idx) {
    if (!reg) return false;
    fs_transaction_t *tx = fs_get_transaction(reg, tx_idx);
    if (!tx || tx->status != FS_TX_ACTIVE) return false;
    if (tx->op_count >= FS_MAX_TX_OPS) return false;

    fs_tx_op_t *op = &tx->ops[tx->op_count++];
    ev_memset(op, 0, sizeof(*op));
    op->type = FS_OP_DELETE;
    op->target_node_id = node_idx;
    return true;
}

bool fs_tx_commit(fs_registry_t *reg, uint32_t tx_idx) {
    if (!reg) return false;
    fs_transaction_t *tx = fs_get_transaction(reg, tx_idx);
    if (!tx || tx->status != FS_TX_ACTIVE) return false;

    /* Execute all operations */
    for (uint32_t i = 0; i < tx->op_count; i++) {
        fs_tx_op_t *op = &tx->ops[i];
        switch (op->type) {
            case FS_OP_CREATE:
                if (fs_create_node(reg, op->name,
                                   (fs_node_type_t)op->data_len,
                                   op->target_node_id) < 0)
                    goto commit_fail;
                break;
            case FS_OP_WRITE:
                if (!fs_write_data(reg, op->target_node_id,
                                   op->data, op->data_len))
                    goto commit_fail;
                break;
            case FS_OP_DELETE:
                if (!fs_delete_node(reg, op->target_node_id))
                    goto commit_fail;
                break;
            default:
                break;
        }
    }

    tx->status = FS_TX_COMMITTED;
    return true;

commit_fail:
    /* Rollback to snapshot */
    if (tx->snapshot_id < reg->snapshot_count) {
        fs_restore_snapshot(reg, tx->snapshot_id);
    }
    tx->status = FS_TX_ABORTED;
    return false;
}

bool fs_tx_abort(fs_registry_t *reg, uint32_t tx_idx) {
    if (!reg) return false;
    fs_transaction_t *tx = fs_get_transaction(reg, tx_idx);
    if (!tx || tx->status != FS_TX_ACTIVE) return false;

    /* Restore snapshot */
    if (tx->snapshot_id < reg->snapshot_count) {
        fs_restore_snapshot(reg, tx->snapshot_id);
    }
    tx->status = FS_TX_ABORTED;
    return true;
}

fs_transaction_t *fs_get_transaction(fs_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->tx_count) return NULL;
    return &reg->transactions[idx];
}

/* ===== Queries ===== */

uint32_t fs_count_by_type(fs_registry_t *reg, fs_node_type_t type) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->node_count; i++) {
        if (reg->nodes[i].type == type && reg->nodes[i].exists_in_s_plus)
            count++;
    }
    return count;
}

uint32_t fs_count_by_space(fs_registry_t *reg, fs_space_t space) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->node_count; i++) {
        switch (space) {
            case FS_SPACE_S_PLUS:
                if (reg->nodes[i].exists_in_s_plus) count++;
                break;
            case FS_SPACE_S_MINUS:
                if (reg->nodes[i].exists_in_s_minus) count++;
                break;
            case FS_SPACE_S_ZERO:
                if (reg->nodes[i].exists_in_s_zero) count++;
                break;
        }
    }
    return count;
}

uint32_t fs_count_tx_by_status(fs_registry_t *reg, fs_tx_status_t status) {
    if (!reg) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < reg->tx_count; i++) {
        if (reg->transactions[i].status == status) count++;
    }
    return count;
}

/* ===== Name Functions ===== */

const char *fs_node_type_name(fs_node_type_t type) {
    switch (type) {
        case FS_NODE_FILE:    return "file";
        case FS_NODE_DIR:     return "dir";
        case FS_NODE_SYMLINK: return "symlink";
        case FS_NODE_DEVICE:  return "device";
        default:               return "unknown";
    }
}

const char *fs_space_name(fs_space_t space) {
    switch (space) {
        case FS_SPACE_S_PLUS:  return "S+";
        case FS_SPACE_S_MINUS: return "S-";
        case FS_SPACE_S_ZERO:  return "S0";
        default:                return "unknown";
    }
}

const char *fs_tx_status_name(fs_tx_status_t status) {
    switch (status) {
        case FS_TX_ACTIVE:    return "active";
        case FS_TX_COMMITTED: return "committed";
        case FS_TX_ABORTED:   return "aborted";
        case FS_TX_PENDING:   return "pending";
        default:               return "unknown";
    }
}

const char *fs_op_type_name(fs_op_type_t type) {
    switch (type) {
        case FS_OP_CREATE: return "create";
        case FS_OP_WRITE:  return "write";
        case FS_OP_DELETE: return "delete";
        case FS_OP_MKDIR:  return "mkdir";
        case FS_OP_RENAME: return "rename";
        default:            return "unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * PROVIDES fs_registry_ready. NOT vfs_ready and NOT zxvfs_ready -- both of
 * those are already provided by other modules at contract 1, and a third
 * provider that meant something different would be alternative provision of a
 * capability it cannot actually stand in for.
 *
 * REQUIRES_NONE is measured (fs.o's `nm -u` is empty) and is the interesting
 * part: this is a tri-space store with S-/S0 replication, but it includes no
 * capability header at all and touches no block device -- the registry is
 * entirely caller-owned memory. That is also why agent A placed it at L2 and
 * not at L3 beside zxvfs_tri.
 */
#include "zxv_decl.h"
static int zxvd_fs_bringup(void) {
    static fs_registry_t reg;
    int32_t idx;
    fs_registry_init(&reg);
    idx = fs_create_node(&reg, "/zxv", FS_NODE_DIR, 0u);
    if (idx < 0) return -1;
    if (fs_find_node(&reg, "/zxv") != idx) return -1;
    if (fs_find_node(&reg, "/nope") >= 0)  return -1;
    return 0;
}

ZXV_DECLARE(fs,
    ZXV_PROVIDES(fs_registry_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_fs_bringup));
