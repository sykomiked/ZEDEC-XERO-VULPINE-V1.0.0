/* test_fs.c — Tri-Space Storage and State (O2) Tests
 *
 * Tests for transactional file operations, tri-space replication,
 * snapshots, and recovery.
 *
 * Author: 36N9 Genetics, LLC
 * License: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [TEST] %s ... ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() return

/* ===== Registry Init Tests ===== */

TEST(registry_init_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    ASSERT(reg.node_count == 0, "no nodes");
    ASSERT(reg.tx_count == 0, "no transactions");
    ASSERT(reg.snapshot_count == 0, "no snapshots");
    ASSERT(reg.next_node_id == 1, "next node id is 1");
    ASSERT(reg.next_tx_id == 1, "next tx id is 1");
    ASSERT(reg.next_snapshot_id == 1, "next snapshot id is 1");
    ASSERT(reg.total_version == 0, "total version is 0");
    PASS();
}

/* ===== Node Management Tests ===== */

TEST(create_node_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    int32_t idx = fs_create_node(&reg, "/etc/config", FS_NODE_FILE, 0);
    ASSERT(idx >= 0, "node created");
    ASSERT(idx == 0, "first node is index 0");
    ASSERT(reg.node_count == 1, "count incremented");

    fs_node_t *n = fs_get_node(&reg, 0);
    ASSERT(n != NULL, "node retrieved");
    ASSERT(strcmp(n->path, "/etc/config") == 0, "path matches");
    ASSERT(strcmp(n->name, "config") == 0, "name extracted from path");
    ASSERT(n->type == FS_NODE_FILE, "type is file");
    ASSERT(n->parent_id == 0, "parent is 0 (root)");
    ASSERT(n->exists_in_s_plus, "exists in S+");
    ASSERT(!n->exists_in_s_minus, "not in S-");
    ASSERT(!n->exists_in_s_zero, "not in S0");
    ASSERT(n->version == 0, "version is 0");
    PASS();
}

TEST(create_dir_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    int32_t idx = fs_create_node(&reg, "/home", FS_NODE_DIR, 0);
    ASSERT(idx >= 0, "dir created");
    fs_node_t *n = fs_get_node(&reg, (uint32_t)idx);
    ASSERT(n->type == FS_NODE_DIR, "type is dir");
    ASSERT(strcmp(n->name, "home") == 0, "name is home");
    PASS();
}

TEST(find_node_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/a", FS_NODE_FILE, 0);
    fs_create_node(&reg, "/b", FS_NODE_FILE, 0);

    ASSERT(fs_find_node(&reg, "/a") == 0, "found /a at 0");
    ASSERT(fs_find_node(&reg, "/b") == 1, "found /b at 1");
    ASSERT(fs_find_node(&reg, "/c") == -1, "/c not found");
    PASS();
}

TEST(duplicate_path_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    int32_t idx1 = fs_create_node(&reg, "/dup", FS_NODE_FILE, 0);
    int32_t idx2 = fs_create_node(&reg, "/dup", FS_NODE_FILE, 0);
    ASSERT(idx1 == idx2, "duplicate returns same index");
    ASSERT(reg.node_count == 1, "count still 1");
    PASS();
}

TEST(write_data_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/data", FS_NODE_FILE, 0);

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT(fs_write_data(&reg, 0, data, 4), "write succeeds");

    fs_node_t *n = fs_get_node(&reg, 0);
    ASSERT(n->data_len == 4, "data length is 4");
    ASSERT(n->data[0] == 0xDE, "data[0] matches");
    ASSERT(n->crc != 0, "CRC is non-zero");
    ASSERT(n->version == 1, "version incremented");
    ASSERT(reg.total_version > 0, "total version incremented");
    PASS();
}

TEST(delete_node_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/tmp", FS_NODE_FILE, 0);
    uint8_t data[] = {0x01};
    fs_write_data(&reg, 0, data, 1);

    ASSERT(fs_delete_node(&reg, 0), "delete succeeds");
    fs_node_t *n = fs_get_node(&reg, 0);
    ASSERT(!n->exists_in_s_plus, "not in S+");
    ASSERT(n->data_len == 0, "data cleared");
    ASSERT(n->crc == 0, "CRC cleared");
    PASS();
}

/* ===== Tri-Space Replication Tests ===== */

TEST(replicate_s_minus_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/rep", FS_NODE_FILE, 0);

    ASSERT(fs_replicate_to_s_minus(&reg, 0), "replicated to S-");
    ASSERT(fs_get_node(&reg, 0)->exists_in_s_minus, "exists in S-");
    PASS();
}

TEST(replicate_s_zero_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/rep", FS_NODE_FILE, 0);

    ASSERT(fs_replicate_to_s_zero(&reg, 0), "replicated to S0");
    ASSERT(fs_get_node(&reg, 0)->exists_in_s_zero, "exists in S0");
    PASS();
}

TEST(recover_from_s_minus_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/rec", FS_NODE_FILE, 0);
    fs_replicate_to_s_minus(&reg, 0);

    /* Simulate S+ failure */
    fs_delete_node(&reg, 0);
    ASSERT(!fs_get_node(&reg, 0)->exists_in_s_plus, "S+ lost");

    /* Recover from S- */
    ASSERT(fs_recover_from_s_minus(&reg, 0), "recovered from S-");
    ASSERT(fs_get_node(&reg, 0)->exists_in_s_plus, "S+ restored");
    PASS();
}

TEST(recover_from_s_zero_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/rec", FS_NODE_FILE, 0);
    fs_replicate_to_s_zero(&reg, 0);

    fs_delete_node(&reg, 0);
    ASSERT(!fs_get_node(&reg, 0)->exists_in_s_plus, "S+ lost");

    ASSERT(fs_recover_from_s_zero(&reg, 0), "recovered from S0");
    ASSERT(fs_get_node(&reg, 0)->exists_in_s_plus, "S+ restored");
    PASS();
}

TEST(recover_no_backup_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/norec", FS_NODE_FILE, 0);
    fs_delete_node(&reg, 0);

    ASSERT(!fs_recover_from_s_minus(&reg, 0), "no S- backup fails");
    ASSERT(!fs_recover_from_s_zero(&reg, 0), "no S0 backup fails");
    PASS();
}

/* ===== Integrity Tests ===== */

TEST(verify_integrity_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/integ", FS_NODE_FILE, 0);

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    fs_write_data(&reg, 0, data, 5);

    ASSERT(fs_verify_integrity(&reg, 0), "integrity verified");

    /* Corrupt data */
    fs_get_node(&reg, 0)->data[0] ^= 0xFF;
    ASSERT(!fs_verify_integrity(&reg, 0), "corrupted data fails");
    PASS();
}

TEST(verify_integrity_empty_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/empty", FS_NODE_FILE, 0);
    ASSERT(fs_verify_integrity(&reg, 0), "empty data is valid");
    PASS();
}

TEST(verify_integrity_deleted_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/del", FS_NODE_FILE, 0);
    uint8_t data[] = {0x01};
    fs_write_data(&reg, 0, data, 1);
    fs_delete_node(&reg, 0);
    ASSERT(!fs_verify_integrity(&reg, 0), "deleted node fails integrity");
    PASS();
}

/* ===== Snapshot Tests ===== */

TEST(create_snapshot_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/a", FS_NODE_FILE, 0);
    fs_create_node(&reg, "/b", FS_NODE_FILE, 0);
    uint8_t data[] = {0xAA, 0xBB};
    fs_write_data(&reg, 0, data, 2);

    int32_t snap_idx = fs_create_snapshot(&reg, "snap1");
    ASSERT(snap_idx >= 0, "snapshot created");
    ASSERT(reg.snapshot_count == 1, "snapshot count incremented");

    fs_snapshot_t *snap = &reg.snapshots[0];
    ASSERT(strcmp(snap->label, "snap1") == 0, "label matches");
    ASSERT(snap->node_count == 2, "2 nodes in snapshot");
    ASSERT(snap->valid, "snapshot is valid");
    ASSERT(snap->nodes[0].data[0] == 0xAA, "snapshot has data");
    PASS();
}

TEST(restore_snapshot_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/orig", FS_NODE_FILE, 0);
    uint8_t data1[] = {0x01, 0x02};
    fs_write_data(&reg, 0, data1, 2);

    /* Snapshot */
    int32_t snap_idx = fs_create_snapshot(&reg, "before-change");

    /* Modify */
    uint8_t data2[] = {0xFF, 0xFF, 0xFF};
    fs_write_data(&reg, 0, data2, 3);
    ASSERT(fs_get_node(&reg, 0)->data_len == 3, "data changed to 3 bytes");

    /* Restore */
    ASSERT(fs_restore_snapshot(&reg, (uint32_t)snap_idx), "snapshot restored");
    ASSERT(fs_get_node(&reg, 0)->data_len == 2, "data restored to 2 bytes");
    ASSERT(fs_get_node(&reg, 0)->data[0] == 0x01, "data[0] restored");
    PASS();
}

TEST(restore_snapshot_delete_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/keep", FS_NODE_FILE, 0);
    fs_create_node(&reg, "/delete", FS_NODE_FILE, 0);

    int32_t snap_idx = fs_create_snapshot(&reg, "before-delete");

    /* Delete a node */
    fs_delete_node(&reg, 1);
    ASSERT(reg.node_count == 2, "still 2 nodes (marked deleted)");

    /* Restore */
    fs_restore_snapshot(&reg, (uint32_t)snap_idx);
    ASSERT(fs_get_node(&reg, 1)->exists_in_s_plus, "deleted node restored");
    PASS();
}

/* ===== Transaction Tests ===== */

TEST(begin_transaction_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    int32_t idx = fs_begin_transaction(&reg);
    ASSERT(idx >= 0, "transaction begun");
    ASSERT(reg.tx_count == 1, "tx count incremented");

    fs_transaction_t *tx = fs_get_transaction(&reg, 0);
    ASSERT(tx->status == FS_TX_ACTIVE, "status is active");
    ASSERT(tx->op_count == 0, "no ops yet");
    PASS();
}

TEST(tx_commit_create_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);

    int32_t tx_idx = fs_begin_transaction(&reg);
    ASSERT(fs_tx_add_create(&reg, (uint32_t)tx_idx, "/newfile",
                            FS_NODE_FILE, 0), "create op added");
    ASSERT(fs_tx_commit(&reg, (uint32_t)tx_idx), "transaction committed");

    fs_transaction_t *tx = fs_get_transaction(&reg, (uint32_t)tx_idx);
    ASSERT(tx->status == FS_TX_COMMITTED, "status is committed");

    int32_t node_idx = fs_find_node(&reg, "/newfile");
    ASSERT(node_idx >= 0, "file created by transaction");
    PASS();
}

TEST(tx_commit_write_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/data", FS_NODE_FILE, 0);

    int32_t tx_idx = fs_begin_transaction(&reg);
    uint8_t data[] = {0x42, 0x43, 0x44};
    ASSERT(fs_tx_add_write(&reg, (uint32_t)tx_idx, 0, data, 3), "write op added");
    ASSERT(fs_tx_commit(&reg, (uint32_t)tx_idx), "transaction committed");

    fs_node_t *n = fs_get_node(&reg, 0);
    ASSERT(n->data_len == 3, "data written");
    ASSERT(n->data[0] == 0x42, "data[0] matches");
    ASSERT(fs_verify_integrity(&reg, 0), "integrity verified");
    PASS();
}

TEST(tx_abort_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/orig", FS_NODE_FILE, 0);
    uint8_t orig[] = {0x01};
    fs_write_data(&reg, 0, orig, 1);

    int32_t tx_idx = fs_begin_transaction(&reg);
    uint8_t newdata[] = {0xFF, 0xFF};
    fs_tx_add_write(&reg, (uint32_t)tx_idx, 0, newdata, 2);
    ASSERT(fs_tx_abort(&reg, (uint32_t)tx_idx), "transaction aborted");

    fs_transaction_t *tx = fs_get_transaction(&reg, (uint32_t)tx_idx);
    ASSERT(tx->status == FS_TX_ABORTED, "status is aborted");

    /* Data should be rolled back */
    fs_node_t *n = fs_get_node(&reg, 0);
    ASSERT(n->data_len == 1, "original data restored");
    ASSERT(n->data[0] == 0x01, "original data[0] matches");
    PASS();
}

TEST(tx_multi_op_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/existing", FS_NODE_FILE, 0);

    int32_t tx_idx = fs_begin_transaction(&reg);
    fs_tx_add_create(&reg, (uint32_t)tx_idx, "/new1", FS_NODE_FILE, 0);
    uint8_t data[] = {0xAA};
    fs_tx_add_write(&reg, (uint32_t)tx_idx, 0, data, 1);
    fs_tx_add_delete(&reg, (uint32_t)tx_idx, 0);
    ASSERT(fs_tx_commit(&reg, (uint32_t)tx_idx), "multi-op transaction committed");

    ASSERT(fs_find_node(&reg, "/new1") >= 0, "new file created");
    /* Node 0 was written then deleted — should not exist in S+ */
    ASSERT(!fs_get_node(&reg, 0)->exists_in_s_plus, "node 0 deleted");
    PASS();
}

TEST(tx_not_active_rejects_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    int32_t tx_idx = fs_begin_transaction(&reg);
    fs_tx_commit(&reg, (uint32_t)tx_idx);

    /* Can't add ops to committed transaction */
    ASSERT(!fs_tx_add_create(&reg, (uint32_t)tx_idx, "/x", FS_NODE_FILE, 0),
           "can't add to committed tx");
    ASSERT(!fs_tx_commit(&reg, (uint32_t)tx_idx), "can't commit twice");
    PASS();
}

/* ===== Query Tests ===== */

TEST(count_by_type_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/f1", FS_NODE_FILE, 0);
    fs_create_node(&reg, "/f2", FS_NODE_FILE, 0);
    fs_create_node(&reg, "/d1", FS_NODE_DIR, 0);

    ASSERT(fs_count_by_type(&reg, FS_NODE_FILE) == 2, "2 files");
    ASSERT(fs_count_by_type(&reg, FS_NODE_DIR) == 1, "1 dir");
    PASS();
}

TEST(count_by_space_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    fs_create_node(&reg, "/a", FS_NODE_FILE, 0);
    fs_create_node(&reg, "/b", FS_NODE_FILE, 0);
    fs_replicate_to_s_minus(&reg, 0);
    fs_replicate_to_s_zero(&reg, 1);

    ASSERT(fs_count_by_space(&reg, FS_SPACE_S_PLUS) == 2, "2 in S+");
    ASSERT(fs_count_by_space(&reg, FS_SPACE_S_MINUS) == 1, "1 in S-");
    ASSERT(fs_count_by_space(&reg, FS_SPACE_S_ZERO) == 1, "1 in S0");
    PASS();
}

TEST(count_tx_by_status_test) {
    fs_registry_t reg;
    fs_registry_init(&reg);
    int32_t t1 = fs_begin_transaction(&reg);
    int32_t t2 = fs_begin_transaction(&reg);
    (void)fs_begin_transaction(&reg);

    fs_tx_commit(&reg, (uint32_t)t1);
    fs_tx_abort(&reg, (uint32_t)t2);

    ASSERT(fs_count_tx_by_status(&reg, FS_TX_COMMITTED) == 1, "1 committed");
    ASSERT(fs_count_tx_by_status(&reg, FS_TX_ABORTED) == 1, "1 aborted");
    ASSERT(fs_count_tx_by_status(&reg, FS_TX_ACTIVE) == 1, "1 active");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(fs_node_type_name(FS_NODE_FILE), "file") == 0, "file name");
    ASSERT(strcmp(fs_node_type_name(FS_NODE_DIR), "dir") == 0, "dir name");
    ASSERT(strcmp(fs_space_name(FS_SPACE_S_PLUS), "S+") == 0, "S+ name");
    ASSERT(strcmp(fs_space_name(FS_SPACE_S_MINUS), "S-") == 0, "S- name");
    ASSERT(strcmp(fs_space_name(FS_SPACE_S_ZERO), "S0") == 0, "S0 name");
    ASSERT(strcmp(fs_tx_status_name(FS_TX_ACTIVE), "active") == 0, "active name");
    ASSERT(strcmp(fs_tx_status_name(FS_TX_COMMITTED), "committed") == 0, "committed name");
    ASSERT(strcmp(fs_tx_status_name(FS_TX_ABORTED), "aborted") == 0, "aborted name");
    ASSERT(strcmp(fs_op_type_name(FS_OP_CREATE), "create") == 0, "create name");
    ASSERT(strcmp(fs_op_type_name(FS_OP_WRITE), "write") == 0, "write name");
    ASSERT(strcmp(fs_op_type_name(FS_OP_DELETE), "delete") == 0, "delete name");
    PASS();
}

/* ===== Replica data (audit HIGH: recover used to set flags only) ===== */

static fs_registry_t rreg; /* registry is ~3 MB; keep it off the stack */

static int node_has(fs_registry_t *reg, uint32_t idx, const char *text) {
    fs_node_t *n = fs_get_node(reg, idx);
    uint32_t len = (uint32_t)strlen(text);
    return n && n->exists_in_s_plus && n->data_len == len && memcmp(n->data, text, len) == 0;
}

TEST(recover_restores_data_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/doc", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"precious", 8);
    fs_write_data(&rreg, 0, (const uint8_t *)"precious", 8); /* version 2 */
    uint32_t crc = fs_get_node(&rreg, 0)->crc;
    ASSERT(fs_replicate_to_s_minus(&rreg, 0), "replicated");
    fs_delete_node(&rreg, 0);
    ASSERT(fs_get_node(&rreg, 0)->data_len == 0, "delete emptied S+");
    uint64_t tv = rreg.total_version;
    ASSERT(fs_recover_from_s_minus(&rreg, 0), "recovered");
    ASSERT(node_has(&rreg, 0, "precious"), "recovered node holds the replicated bytes");
    ASSERT(fs_get_node(&rreg, 0)->crc == crc, "CRC restored");
    ASSERT(fs_get_node(&rreg, 0)->version == 2, "version restored");
    ASSERT(fs_verify_integrity(&rreg, 0), "recovered node passes integrity");
    ASSERT(rreg.total_version == tv + 1, "recovery is a versioned change");
    PASS();
}

TEST(replica_is_a_copy_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/doc", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"old", 3);
    fs_replicate_to_s_zero(&rreg, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"new-longer", 10);
    const fs_replica_t *r = fs_get_replica(&rreg, 0, FS_SPACE_S_ZERO);
    ASSERT(r && r->data_len == 3 && memcmp(r->data, "old", 3) == 0, "a later write does not change the replica");
    /* recover over a live node rolls it back to the replica */
    ASSERT(fs_recover_from_s_zero(&rreg, 0), "recovered over live node");
    ASSERT(node_has(&rreg, 0, "old"), "rolled back to replica");
    ASSERT(fs_get_node(&rreg, 0)->version == 1, "version rolled back to the replica's");
    fs_node_t *n = fs_get_node(&rreg, 0);
    int clean = 1;
    for (uint32_t i = 3; i < FS_MAX_DATA_LEN; i++) if (n->data[i]) clean = 0;
    ASSERT(clean, "no bytes of the longer data survive past the recovered length");
    PASS();
}

TEST(spaces_are_independent_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/doc", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"v1", 2);
    fs_replicate_to_s_minus(&rreg, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"v2", 2);
    fs_replicate_to_s_zero(&rreg, 0);
    fs_delete_node(&rreg, 0);
    ASSERT(fs_recover_from_s_zero(&rreg, 0) && node_has(&rreg, 0, "v2"), "S0 holds v2");
    ASSERT(fs_recover_from_s_minus(&rreg, 0) && node_has(&rreg, 0, "v1"), "S- holds v1");
    ASSERT(fs_get_replica(&rreg, 0, FS_SPACE_S_PLUS) == NULL, "S+ has no replica slot");
    PASS();
}

TEST(corrupt_replica_refused_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/doc", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"abcd", 4);
    fs_replicate_to_s_minus(&rreg, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"live", 4);
    rreg.replica_s_minus[0].data[1] ^= 0x01; /* bit rot in the backup */
    ASSERT(!fs_recover_from_s_minus(&rreg, 0), "corrupted replica is not restored");
    ASSERT(node_has(&rreg, 0, "live"), "node untouched by the refused recovery");
    PASS();
}

TEST(stale_replica_not_recovered_into_new_node_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/a", FS_NODE_FILE, 0);
    int32_t snap = fs_create_snapshot(&rreg, "before-b");
    fs_create_node(&rreg, "/b", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 1, (const uint8_t *)"b-data", 6);
    fs_replicate_to_s_minus(&rreg, 1);
    fs_restore_snapshot(&rreg, (uint32_t)snap); /* /b is gone, slot 1 free */
    int32_t c = fs_create_node(&rreg, "/c", FS_NODE_FILE, 0);
    ASSERT(c == 1, "/c reuses index 1");
    ASSERT(fs_get_replica(&rreg, 1, FS_SPACE_S_MINUS) == NULL, "no replica for /c");
    ASSERT(!fs_recover_from_s_minus(&rreg, 1), "/b's replica is not recovered into /c");
    ASSERT(fs_get_node(&rreg, 1)->data_len == 0, "/c stays empty");
    PASS();
}

TEST(replica_survives_snapshot_restore_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/doc", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"keep", 4);
    int32_t snap = fs_create_snapshot(&rreg, "pre-replica");
    fs_replicate_to_s_minus(&rreg, 0);
    fs_restore_snapshot(&rreg, (uint32_t)snap); /* rolls the S- flag back */
    ASSERT(!fs_get_node(&rreg, 0)->exists_in_s_minus, "flag rolled back");
    fs_delete_node(&rreg, 0);
    ASSERT(fs_recover_from_s_minus(&rreg, 0), "replica outside the snapshot still recovers");
    ASSERT(node_has(&rreg, 0, "keep"), "data back");
    ASSERT(fs_get_node(&rreg, 0)->exists_in_s_minus, "flag re-set");
    PASS();
}

TEST(replicate_deleted_keeps_old_replica_test) {
    fs_registry_init(&rreg);
    fs_create_node(&rreg, "/doc", FS_NODE_FILE, 0);
    fs_write_data(&rreg, 0, (const uint8_t *)"good", 4);
    fs_replicate_to_s_minus(&rreg, 0);
    fs_delete_node(&rreg, 0);
    ASSERT(!fs_replicate_to_s_minus(&rreg, 0), "a deleted node cannot be replicated");
    ASSERT(!fs_replicate_to_s_minus(&rreg, 7), "bad index refused");
    ASSERT(fs_recover_from_s_minus(&rreg, 0) && node_has(&rreg, 0, "good"), "the earlier replica was not wiped");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV FS (O2) Tests ===\n\n");

    RUN(registry_init_test);
    RUN(create_node_test);
    RUN(create_dir_test);
    RUN(find_node_test);
    RUN(duplicate_path_test);
    RUN(write_data_test);
    RUN(delete_node_test);
    RUN(replicate_s_minus_test);
    RUN(replicate_s_zero_test);
    RUN(recover_from_s_minus_test);
    RUN(recover_from_s_zero_test);
    RUN(recover_no_backup_test);
    RUN(verify_integrity_test);
    RUN(verify_integrity_empty_test);
    RUN(verify_integrity_deleted_test);
    RUN(create_snapshot_test);
    RUN(restore_snapshot_test);
    RUN(restore_snapshot_delete_test);
    RUN(begin_transaction_test);
    RUN(tx_commit_create_test);
    RUN(tx_commit_write_test);
    RUN(tx_abort_test);
    RUN(tx_multi_op_test);
    RUN(tx_not_active_rejects_test);
    RUN(count_by_type_test);
    RUN(count_by_space_test);
    RUN(count_tx_by_status_test);
    RUN(name_functions_test);
    RUN(recover_restores_data_test);
    RUN(replica_is_a_copy_test);
    RUN(spaces_are_independent_test);
    RUN(corrupt_replica_refused_test);
    RUN(stale_replica_not_recovered_into_new_node_test);
    RUN(replica_survives_snapshot_restore_test);
    RUN(replicate_deleted_keeps_old_replica_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
