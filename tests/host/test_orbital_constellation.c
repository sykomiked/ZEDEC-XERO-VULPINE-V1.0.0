/* test_orbital_constellation.c — Host-side tests for Orbital Elevator,
 * Constellation Coordinator, and Event Transport.
 *
 * Compile: gcc -Wall -Werror -Wextra -I.../event_space -I.../orbital_elevator \
 *          -I.../constellation -I.../event_transport \
 *          -o test test_orbital_constellation.c \
 *          event_envelope.c event_sequencer.c self_audit.c self_healing.c \
 *          orbital_elevator.c constellation_coordinator.c event_transport.c -lm
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "event_space.h"
#include "orbital_elevator.h"
#include "constellation_coordinator.h"
#include "event_transport.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  [TEST] %s ... ", #name); \
    tests_run++; \
    name(); \
} while (0)

#define PASS() do { printf("PASS\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; return; } while (0)
#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); } \
    } while (0)

static void copy_str_h(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Orbital Elevator Tests ===== */

/* Adapter: storage.read v1 → v3 (adds a version field) */
static bool adapter_storage_v1_to_v3(const ev_envelope_t *in,
                                      ev_envelope_t *out) {
    if (!in || !out) return false;
    *out = *in;
    /* v3 adds a "flags" field in the first payload byte */
    if (out->payload_len < EV_PAYLOAD_MAX - 1) {
        out->payload[out->payload_len] = 0x00; /* flags = 0 */
        out->payload_len++;
    }
    return true;
}

/* Adapter: storage.read v3 → v4 (changes schema name) */
static bool adapter_storage_v3_to_v4(const ev_envelope_t *in,
                                      ev_envelope_t *out) {
    if (!in || !out) return false;
    *out = *in;
    copy_str_h(out->schema, "zxv.storage.read.v4", EV_SCHEMA_LEN);
    return true;
}

/* Adapter that always fails */
static bool adapter_always_fail(const ev_envelope_t *in,
                                 ev_envelope_t *out) {
    (void)in; (void)out;
    return false;
}

TEST(orbital_init) {
    static oe_elevator_t oe;
    oe_init(&oe);
    ASSERT(oe.num_schemas == 0, "no schemas after init");
    ASSERT(oe.num_adapters == 0, "no adapters after init");
    ASSERT(oe.num_edges == 0, "no edges after init");
    ASSERT(oe.next_adapter_id == 1, "next_adapter_id starts at 1");
    PASS();
}

TEST(orbital_register_schema) {
    static oe_elevator_t oe;
    oe_init(&oe);

    bool r = oe_register_schema(&oe, "zxv.storage.read");
    ASSERT(r, "schema registration should succeed");
    ASSERT(oe.num_schemas == 1, "one schema registered");

    /* Duplicate registration should be idempotent */
    r = oe_register_schema(&oe, "zxv.storage.read");
    ASSERT(r, "duplicate registration should succeed");
    ASSERT(oe.num_schemas == 1, "still one schema");

    /* Add versions */
    r = oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    ASSERT(r, "add version 1");
    r = oe_schema_add_version(&oe, "zxv.storage.read", 3, 1, 256);
    ASSERT(r, "add version 3");
    r = oe_schema_add_version(&oe, "zxv.storage.read", 4, 1, 256);
    ASSERT(r, "add version 4");

    /* Duplicate version should fail */
    r = oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    ASSERT(!r, "duplicate version should fail");

    int16_t latest = oe_schema_latest_version(&oe, "zxv.storage.read");
    ASSERT(latest == 4, "latest version should be 4");

    ASSERT(oe_schema_exists(&oe, "zxv.storage.read"), "schema exists");
    ASSERT(!oe_schema_exists(&oe, "nonexistent"), "nonexistent schema");
    PASS();
}

TEST(orbital_register_adapter) {
    static oe_elevator_t oe;
    oe_init(&oe);

    uint32_t id1 = oe_register_adapter(&oe, "storage.v1_to_v3",
                                        adapter_storage_v1_to_v3);
    ASSERT(id1 > 0, "adapter registration should return nonzero ID");

    uint32_t id2 = oe_register_adapter(&oe, "storage.v3_to_v4",
                                        adapter_storage_v3_to_v4);
    ASSERT(id2 > id1, "second adapter ID should be higher");

    oe_adapter_t *a = oe_get_adapter(&oe, id1);
    ASSERT(a != NULL, "should find adapter by ID");
    ASSERT(a->transform == adapter_storage_v1_to_v3, "transform matches");
    ASSERT(!a->signed_adapter, "not signed by default");

    ASSERT(oe_get_adapter(&oe, 999) == NULL, "nonexistent adapter");
    PASS();
}

TEST(orbital_compat_graph) {
    static oe_elevator_t oe;
    oe_init(&oe);

    oe_register_schema(&oe, "zxv.storage.read");
    oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 3, 1, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 4, 1, 256);

    uint32_t a1 = oe_register_adapter(&oe, "v1_to_v3", adapter_storage_v1_to_v3);
    uint32_t a2 = oe_register_adapter(&oe, "v3_to_v4", adapter_storage_v3_to_v4);

    bool r = oe_register_compat(&oe, "zxv.storage.read", 1,
                                 "zxv.storage.read", 3, a1);
    ASSERT(r, "register compat edge v1→v3");

    r = oe_register_compat(&oe, "zxv.storage.read", 3,
                            "zxv.storage.read", 4, a2);
    ASSERT(r, "register compat edge v3→v4");

    ASSERT(oe.num_edges == 2, "two edges registered");
    PASS();
}

TEST(orbital_translate_chain) {
    static oe_elevator_t oe;
    static ev_sequencer_t seq;
    oe_init(&oe);
    ev_seq_init(&seq, "test.node");

    oe_register_schema(&oe, "zxv.storage.read");
    oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 3, 1, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 4, 1, 256);

    uint32_t a1 = oe_register_adapter(&oe, "v1_to_v3", adapter_storage_v1_to_v3);
    uint32_t a2 = oe_register_adapter(&oe, "v3_to_v4", adapter_storage_v3_to_v4);

    oe_register_compat(&oe, "zxv.storage.read", 1,
                       "zxv.storage.read", 3, a1);
    oe_register_compat(&oe, "zxv.storage.read", 3,
                       "zxv.storage.read", 4, a2);

    /* Create a v1 envelope */
    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.storage.read", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"test", 4);
    ev_envelope_compute_crc(&env);

    /* Translate v1 → v4 (two hops) */
    oe_translate_result_t r = oe_translate(&oe, &env,
                                            "zxv.storage.read", 4);
    ASSERT(r == OE_TRANSLATE_OK, "translation v1→v4 should succeed");
    ASSERT(env.schema_version == 4, "envelope should be v4 after translation");

    /* Check adapter was invoked */
    oe_adapter_t *ad1 = oe_get_adapter(&oe, a1);
    ASSERT(ad1->invocation_count == 1, "adapter 1 invoked once");
    oe_adapter_t *ad2 = oe_get_adapter(&oe, a2);
    ASSERT(ad2->invocation_count == 1, "adapter 2 invoked once");

    ASSERT(oe.total_translations == 1, "one translation recorded");
    ASSERT(oe.total_hops == 2, "two hops recorded");
    PASS();
}

TEST(orbital_translate_no_path) {
    static oe_elevator_t oe;
    static ev_sequencer_t seq;
    oe_init(&oe);
    ev_seq_init(&seq, "test.node");

    oe_register_schema(&oe, "zxv.storage.read");
    oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 4, 1, 256);
    /* No adapter registered — no path from v1 to v4 */

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.storage.read", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"x", 1);

    oe_translate_result_t r = oe_translate(&oe, &env,
                                            "zxv.storage.read", 4);
    ASSERT(r == OE_TRANSLATE_NO_PATH, "should report no path");
    ASSERT(oe.total_no_path == 1, "no_path counter incremented");
    PASS();
}

TEST(orbital_translate_same_version) {
    static oe_elevator_t oe;
    static ev_sequencer_t seq;
    oe_init(&oe);
    ev_seq_init(&seq, "test.node");

    oe_register_schema(&oe, "zxv.storage.read");
    oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.storage.read", 1);

    oe_translate_result_t r = oe_translate(&oe, &env,
                                            "zxv.storage.read", 1);
    ASSERT(r == OE_TRANSLATE_OK, "same version should be OK (no-op)");
    ASSERT(oe.total_translations == 1, "translation counted");
    ASSERT(oe.total_hops == 0, "no hops for same version");
    PASS();
}

TEST(orbital_translate_schema_not_found) {
    static oe_elevator_t oe;
    static ev_sequencer_t seq;
    oe_init(&oe);
    ev_seq_init(&seq, "test.node");

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "unknown.schema", 1);

    oe_translate_result_t r = oe_translate(&oe, &env,
                                            "nonexistent.schema", 1);
    ASSERT(r == OE_TRANSLATE_SCHEMA_NOT_FOUND, "schema not found");
    PASS();
}

TEST(orbital_adapter_failure) {
    static oe_elevator_t oe;
    static ev_sequencer_t seq;
    oe_init(&oe);
    ev_seq_init(&seq, "test.node");

    oe_register_schema(&oe, "zxv.test");
    oe_schema_add_version(&oe, "zxv.test", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.test", 2, 0, 256);

    uint32_t bad = oe_register_adapter(&oe, "always_fail", adapter_always_fail);
    oe_register_compat(&oe, "zxv.test", 1, "zxv.test", 2, bad);

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.test", 1);

    oe_translate_result_t r = oe_translate(&oe, &env, "zxv.test", 2);
    ASSERT(r == OE_TRANSLATE_FAILED, "adapter failure should be reported");
    ASSERT(oe.total_failures == 1, "failure recorded");
    PASS();
}

TEST(orbital_orbit_manifest) {
    static oe_elevator_t oe;
    oe_init(&oe);

    bool r = oe_register_orbit(&oe, "org.zxv.notes", "neutral", 4);
    ASSERT(r, "register orbit");

    r = oe_orbit_add_dep(&oe, "org.zxv.notes", "zxv.storage", 2, true);
    ASSERT(r, "add required dependency");

    /* Register the dependency schema with version 2 */
    oe_register_schema(&oe, "zxv.storage");
    oe_schema_add_version(&oe, "zxv.storage", 2, 0, 256);

    ASSERT(oe_orbit_compatible(&oe, "org.zxv.notes"),
           "orbit should be compatible when deps satisfied");

    /* Add a missing required dependency */
    r = oe_orbit_add_dep(&oe, "org.zxv.notes", "zxv.network", 1, true);
    ASSERT(r, "add missing dependency");

    ASSERT(!oe_orbit_compatible(&oe, "org.zxv.notes"),
           "orbit should be incompatible when required dep missing");
    PASS();
}

/* ===== Constellation Coordinator Tests ===== */

TEST(constellation_init) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "arm64.cluster0");
    ASSERT(cc.num_nodes == 0, "no nodes after init");
    ASSERT(cc.num_executors == 0, "no executors after init");
    ASSERT(cc.num_routes == 0, "no routes after init");
    PASS();
}

TEST(constellation_register_nodes) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "arm64.cluster0");

    int32_t n1 = cc_register_node(&cc, "x86.host0", "x86_64", 1);
    ASSERT(n1 >= 0, "register x86 node");
    ASSERT(cc.num_nodes == 1, "one node registered");

    int32_t n2 = cc_register_node(&cc, "arm64.cluster0", "arm64", 1);
    ASSERT(n2 >= 0, "register arm64 node");
    ASSERT(cc.num_nodes == 2, "two nodes registered");

    /* Duplicate same incarnation should be idempotent */
    int32_t n3 = cc_register_node(&cc, "x86.host0", "x86_64", 1);
    ASSERT(n3 == n1, "duplicate returns same index");
    ASSERT(cc.num_nodes == 2, "still two nodes");

    /* New incarnation should mark old as failed */
    int32_t n4 = cc_register_node(&cc, "x86.host0", "x86_64", 2);
    ASSERT(n4 >= 0, "new incarnation registered");
    ASSERT(n4 != n1, "new incarnation gets new slot");

    cc_node_t *old = cc_get_node_by_idx(&cc, (uint32_t)n1);
    ASSERT(old->state == CC_NODE_FAILED, "old incarnation should be failed");
    PASS();
}

TEST(constellation_node_lifecycle) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "local");

    int32_t idx = cc_register_node(&cc, "riscv.sec0", "riscv64", 1);
    ASSERT(idx >= 0, "register riscv node");

    cc_node_t *node = cc_get_node_by_idx(&cc, (uint32_t)idx);
    ASSERT(node->state == CC_NODE_DISCOVERED, "starts as DISCOVERED");

    bool r = cc_node_authenticate(&cc, (uint32_t)idx);
    ASSERT(r, "authenticate node");
    ASSERT(node->state == CC_NODE_QUARANTINED, "authenticated → quarantined");

    r = cc_node_activate(&cc, (uint32_t)idx);
    ASSERT(r, "activate node");
    ASSERT(node->state == CC_NODE_ACTIVE, "now active");

    r = cc_node_fail(&cc, (uint32_t)idx);
    ASSERT(r, "fail node");
    ASSERT(node->state == CC_NODE_FAILED, "now failed");
    ASSERT(cc.total_node_failures == 1, "failure counted");

    r = cc_node_recover(&cc, (uint32_t)idx);
    ASSERT(r, "recover node");
    ASSERT(node->state == CC_NODE_QUARANTINED, "recovered → quarantined");
    ASSERT(cc.total_node_recoveries == 1, "recovery counted");

    r = cc_node_activate(&cc, (uint32_t)idx);
    ASSERT(r, "activate again");
    ASSERT(node->state == CC_NODE_ACTIVE, "active again");
    PASS();
}

TEST(constellation_node_schemas) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "local");

    int32_t idx = cc_register_node(&cc, "arm64.io0", "arm64", 1);
    ASSERT(idx >= 0, "register node");

    bool r = cc_node_add_schema(&cc, (uint32_t)idx, "zxv.storage");
    ASSERT(r, "add schema 1");
    r = cc_node_add_schema(&cc, (uint32_t)idx, "zxv.network");
    ASSERT(r, "add schema 2");

    /* Duplicate should be idempotent */
    r = cc_node_add_schema(&cc, (uint32_t)idx, "zxv.storage");
    ASSERT(r, "duplicate schema ok");

    cc_node_t *node = cc_get_node_by_idx(&cc, (uint32_t)idx);
    ASSERT(node->num_schemas == 2, "two schemas registered");
    PASS();
}

TEST(constellation_executor) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "local");

    int32_t nidx = cc_register_node(&cc, "arm64.npu0", "arm64", 1);
    ASSERT(nidx >= 0, "register NPU node");
    cc_node_authenticate(&cc, (uint32_t)nidx);
    cc_node_activate(&cc, (uint32_t)nidx);

    int32_t eidx = cc_register_executor(&cc, "arm64.npu0", "ai.infer",
                                         42, 71);
    ASSERT(eidx >= 0, "register executor");
    cc_executor_add_schema(&cc, (uint32_t)eidx, "ai.tensor.infer");
    cc_executor_add_capability(&cc, (uint32_t)eidx, "ai.infer.v2");

    cc_executor_t *ex = cc_get_executor(&cc, (uint32_t)eidx);
    ASSERT(ex->cost_p50 == 42, "cost_p50 set");
    ASSERT(ex->cost_p99 == 71, "cost_p99 set");
    ASSERT(ex->num_schemas == 1, "one schema");
    ASSERT(ex->num_capabilities == 1, "one capability");

    /* Find executor for a supported schema */
    int32_t found = cc_find_executor(&cc, "ai.tensor.infer", 1);
    ASSERT(found == eidx, "should find the executor");

    /* Find executor for unsupported schema */
    found = cc_find_executor(&cc, "zxv.storage", 1);
    ASSERT(found < 0, "no executor for unsupported schema");
    PASS();
}

TEST(constellation_routing) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "local");

    /* Register two nodes */
    int32_t n1 = cc_register_node(&cc, "x86.host0", "x86_64", 1);
    int32_t n2 = cc_register_node(&cc, "arm64.storage0", "arm64", 1);
    ASSERT(n1 >= 0 && n2 >= 0, "register nodes");

    cc_node_authenticate(&cc, (uint32_t)n1);
    cc_node_activate(&cc, (uint32_t)n1);
    cc_node_authenticate(&cc, (uint32_t)n2);
    cc_node_activate(&cc, (uint32_t)n2);

    /* Add explicit route */
    bool r = cc_add_route(&cc, "zxv.storage", 1, "arm64.storage0", 100);
    ASSERT(r, "add route");

    /* Route a storage event */
    cc_node_t *target = cc_route_event(&cc, "zxv.storage", 1);
    ASSERT(target != NULL, "should route to target");
    ASSERT(strcmp(target->id, "arm64.storage0") == 0, "routed to storage node");
    ASSERT(target->total_events_routed == 1, "event routed count");

    /* Route unknown schema — should fail */
    target = cc_route_event(&cc, "unknown.schema", 1);
    ASSERT(target == NULL, "no route for unknown schema");
    ASSERT(cc.total_events_dropped == 1, "drop counted");
    PASS();
}

TEST(constellation_health_check) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "local");

    int32_t n1 = cc_register_node(&cc, "x86.host0", "x86_64", 1);
    cc_node_authenticate(&cc, (uint32_t)n1);
    cc_node_activate(&cc, (uint32_t)n1);

    cc_node_t *node = cc_get_node_by_idx(&cc, (uint32_t)n1);
    node->last_heartbeat_sequence = 100;

    /* Health check with recent heartbeat — no failures */
    uint32_t failures = cc_check_health(&cc, 150, 100);
    ASSERT(failures == 0, "no failures with recent heartbeat");
    ASSERT(node->state == CC_NODE_ACTIVE, "still active");

    /* Health check with stale heartbeat — should degrade */
    failures = cc_check_health(&cc, 250, 100);
    ASSERT(failures == 0, "degraded is not a failure");
    ASSERT(node->state == CC_NODE_DEGRADED, "should be degraded");

    /* Another stale check — should fail */
    failures = cc_check_health(&cc, 350, 100);
    ASSERT(failures == 1, "one failure detected");
    ASSERT(node->state == CC_NODE_FAILED, "should be failed");
    PASS();
}

TEST(constellation_heartbeat_recovery) {
    static cc_coordinator_t cc;
    cc_coordinator_init(&cc, "local");

    int32_t n1 = cc_register_node(&cc, "x86.host0", "x86_64", 1);
    cc_node_authenticate(&cc, (uint32_t)n1);
    cc_node_activate(&cc, (uint32_t)n1);

    cc_node_t *node = cc_get_node_by_idx(&cc, (uint32_t)n1);

    /* Degrade the node */
    node->state = CC_NODE_DEGRADED;
    node->last_heartbeat_sequence = 100;

    /* Send heartbeat — should restore to active */
    bool r = cc_node_heartbeat(&cc, (uint32_t)n1, 200);
    ASSERT(r, "heartbeat succeeds");
    ASSERT(node->state == CC_NODE_ACTIVE, "restored to active");
    ASSERT(node->last_heartbeat_sequence == 200, "sequence updated");
    PASS();
}

/* ===== Event Transport Tests ===== */

TEST(transport_init) {
    static et_transport_t et;
    et_init(&et);
    ASSERT(et.num_channels == 0, "no channels after init");
    ASSERT(et.next_channel_id == 1, "next_channel_id starts at 1");
    PASS();
}

TEST(transport_create_channel) {
    static et_transport_t et;
    et_init(&et);

    int32_t idx = et_create_channel(&et, "arm64-to-x86",
                                     ET_TRANSPORT_SHMEM,
                                     "arm64.cluster0", "x86.host0");
    ASSERT(idx >= 0, "channel creation succeeds");
    ASSERT(et.num_channels == 1, "one channel registered");

    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch != NULL, "channel found by index");
    ASSERT(ch->type == ET_TRANSPORT_SHMEM, "type is shmem");
    ASSERT(ch->state == ET_CHANNEL_CONNECTING, "starts as connecting");

    /* Find by remote node */
    int32_t found = et_find_channel(&et, "x86.host0");
    ASSERT(found == idx, "find channel by remote node");
    PASS();
}

TEST(transport_activate) {
    static et_transport_t et;
    et_init(&et);

    int32_t idx = et_create_channel(&et, "local", ET_TRANSPORT_LOCAL,
                                     "a", "b");
    bool r = et_channel_activate(&et, (uint32_t)idx);
    ASSERT(r, "activate channel");

    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch->state == ET_CHANNEL_ACTIVE, "channel is active");
    ASSERT(et_channel_healthy(&et, (uint32_t)idx), "channel is healthy");
    PASS();
}

TEST(transport_send_local) {
    static et_transport_t et;
    static ev_sequencer_t seq;
    et_init(&et);
    ev_seq_init(&seq, "test.node");

    int32_t idx = et_create_channel(&et, "local", ET_TRANSPORT_LOCAL,
                                     "a", "b");
    et_channel_activate(&et, (uint32_t)idx);

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.test", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"hello", 5);
    ev_envelope_compute_crc(&env);

    et_delivery_result_t r = et_send(&et, (uint32_t)idx, &env);
    ASSERT(r == ET_DELIVERY_OK, "local send should be OK");

    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch->total_sent == 1, "sent count incremented");
    PASS();
}

TEST(transport_send_inactive) {
    static et_transport_t et;
    static ev_sequencer_t seq;
    et_init(&et);
    ev_seq_init(&seq, "test.node");

    int32_t idx = et_create_channel(&et, "net", ET_TRANSPORT_NETWORK,
                                     "a", "b");
    /* Don't activate — channel is still CONNECTING */

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.test", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"x", 1);
    ev_envelope_compute_crc(&env);

    et_delivery_result_t r = et_send(&et, (uint32_t)idx, &env);
    ASSERT(r == ET_DELIVERY_DROPPED, "send to inactive channel should drop");
    PASS();
}

TEST(transport_send_receive_shmem) {
    static et_transport_t et;
    static ev_sequencer_t seq;
    et_init(&et);
    ev_seq_init(&seq, "test.node");

    int32_t idx = et_create_channel(&et, "shmem", ET_TRANSPORT_SHMEM,
                                     "a", "b");
    et_channel_activate(&et, (uint32_t)idx);

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.test", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"data", 4);
    ev_envelope_compute_crc(&env);

    et_delivery_result_t r = et_send(&et, (uint32_t)idx, &env);
    ASSERT(r == ET_DELIVERY_PENDING, "shmem send should be pending");

    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch->pending_count == 1, "one pending delivery");

    /* Receive */
    ev_envelope_t out;
    r = et_receive(&et, (uint32_t)idx, &out);
    ASSERT(r == ET_DELIVERY_OK, "receive should succeed");
    ASSERT(out.payload_len == 4, "payload length matches");
    ASSERT(ch->pending_count == 0, "pending queue empty after receive");
    ASSERT(ch->total_received == 1, "received count incremented");
    PASS();
}

TEST(transport_queue_full) {
    static et_transport_t et;
    static ev_sequencer_t seq;
    et_init(&et);
    ev_seq_init(&seq, "test.node");

    int32_t idx = et_create_channel(&et, "q", ET_TRANSPORT_SHMEM,
                                     "a", "b");
    et_channel_activate(&et, (uint32_t)idx);

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.test", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"x", 1);
    ev_envelope_compute_crc(&env);

    /* Fill the queue */
    for (uint32_t i = 0; i < ET_MAX_PENDING; i++) {
        et_delivery_result_t r = et_send(&et, (uint32_t)idx, &env);
        ASSERT(r == ET_DELIVERY_PENDING, "send should be pending");
    }

    /* Next send should be dropped */
    et_delivery_result_t r = et_send(&et, (uint32_t)idx, &env);
    ASSERT(r == ET_DELIVERY_DROPPED, "queue full should drop");

    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch->total_dropped == 1, "one drop recorded");
    PASS();
}

TEST(transport_process_pending) {
    static et_transport_t et;
    static ev_sequencer_t seq;
    et_init(&et);
    ev_seq_init(&seq, "test.node");

    int32_t idx = et_create_channel(&et, "q", ET_TRANSPORT_SHMEM,
                                     "a", "b");
    et_channel_activate(&et, (uint32_t)idx);

    ev_envelope_t env;
    ev_envelope_init(&env, &seq.node, 1, "zxv.test", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"x", 1);
    ev_envelope_compute_crc(&env);

    /* Queue 3 events */
    for (uint32_t i = 0; i < 3; i++)
        et_send(&et, (uint32_t)idx, &env);

    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch->pending_count == 3, "3 pending");

    /* Process pending — should clear queue */
    uint32_t processed = et_process_pending(&et, (uint32_t)idx);
    ASSERT(processed == 3, "3 processed");
    ASSERT(ch->pending_count == 0, "queue cleared");
    PASS();
}

TEST(transport_find_no_channel) {
    static et_transport_t et;
    et_init(&et);

    int32_t found = et_find_channel(&et, "nonexistent");
    ASSERT(found < 0, "no channel found for nonexistent node");
    PASS();
}

TEST(transport_deactivate) {
    static et_transport_t et;
    et_init(&et);

    int32_t idx = et_create_channel(&et, "ch", ET_TRANSPORT_LOCAL,
                                     "a", "b");
    et_channel_activate(&et, (uint32_t)idx);
    ASSERT(et_channel_healthy(&et, (uint32_t)idx), "healthy when active");

    et_channel_deactivate(&et, (uint32_t)idx);
    et_channel_t *ch = et_get_channel(&et, (uint32_t)idx);
    ASSERT(ch->state == ET_CHANNEL_DISCONNECTED, "disconnected");
    ASSERT(!et_channel_healthy(&et, (uint32_t)idx), "not healthy when disconnected");
    PASS();
}

/* ===== Integrated Test: Full Pipeline ===== */

TEST(integrated_cross_isa_pipeline) {
    static oe_elevator_t oe;
    static cc_coordinator_t cc;
    static et_transport_t et;
    static ev_sequencer_t seq;

    oe_init(&oe);
    cc_coordinator_init(&cc, "arm64.cluster0");
    et_init(&et);
    ev_seq_init(&seq, "arm64.cluster0");

    /* 1. Register schemas and adapters in Orbital Elevator */
    oe_register_schema(&oe, "zxv.storage.read");
    oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 4, 1, 256);

    uint32_t adapter = oe_register_adapter(&oe, "v1_to_v4_combined",
        adapter_storage_v1_to_v3);
    /* Register a direct v1→v3 edge, then v3→v4 */
    oe_register_compat(&oe, "zxv.storage.read", 1,
                       "zxv.storage.read", 3, adapter);
    uint32_t adapter2 = oe_register_adapter(&oe, "v3_to_v4",
        adapter_storage_v3_to_v4);
    oe_register_compat(&oe, "zxv.storage.read", 3,
                       "zxv.storage.read", 4, adapter2);

    /* 2. Register nodes in Constellation Coordinator */
    int32_t x86 = cc_register_node(&cc, "x86.host0", "x86_64", 1);
    int32_t arm = cc_register_node(&cc, "arm64.cluster0", "arm64", 1);
    ASSERT(x86 >= 0 && arm >= 0, "nodes registered");

    cc_node_authenticate(&cc, (uint32_t)x86);
    cc_node_activate(&cc, (uint32_t)x86);
    cc_node_authenticate(&cc, (uint32_t)arm);
    cc_node_activate(&cc, (uint32_t)arm);

    cc_node_add_schema(&cc, (uint32_t)arm, "zxv.storage.read");
    cc_add_route(&cc, "zxv.storage.read", 1, "arm64.cluster0", 100);

    /* 3. Create transport channel */
    int32_t ch = et_create_channel(&et, "x86-to-arm64",
                                    ET_TRANSPORT_SHMEM,
                                    "x86.host0", "arm64.cluster0");
    et_channel_activate(&et, (uint32_t)ch);

    /* 4. Create a v1 event from x86 node */
    ev_node_id_t x86_node;
    copy_str_h(x86_node.id, "x86.host0", EV_NODE_ID_LEN);
    x86_node.incarnation = 1;

    ev_envelope_t env;
    ev_envelope_init(&env, &x86_node, 1, "zxv.storage.read", 1);
    ev_envelope_set_payload(&env, (const uint8_t *)"doc1", 4);
    ev_envelope_compute_crc(&env);

    /* 5. Translate v1 → v4 through Orbital Elevator */
    oe_translate_result_t tr = oe_translate(&oe, &env,
                                             "zxv.storage.read", 4);
    ASSERT(tr == OE_TRANSLATE_OK, "translation succeeds");
    ASSERT(env.schema_version == 4, "now v4");

    /* 6. Route through Constellation Coordinator */
    cc_node_t *target = cc_route_event(&cc, "zxv.storage.read", 4);
    ASSERT(target != NULL, "routed to target");
    ASSERT(strcmp(target->id, "arm64.cluster0") == 0, "routed to arm64");

    /* 7. Send through transport */
    et_delivery_result_t dr = et_send(&et, (uint32_t)ch, &env);
    ASSERT(dr == ET_DELIVERY_PENDING, "queued for delivery");

    /* 8. Receive on the other side */
    ev_envelope_t received;
    dr = et_receive(&et, (uint32_t)ch, &received);
    ASSERT(dr == ET_DELIVERY_OK, "received OK");
    ASSERT(received.schema_version == 4, "received v4 envelope");
    ASSERT(ev_envelope_validate(&received), "envelope valid after transport");

    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV Orbital Elevator + Constellation + Transport Tests ===\n\n");

    /* Orbital Elevator */
    RUN(orbital_init);
    RUN(orbital_register_schema);
    RUN(orbital_register_adapter);
    RUN(orbital_compat_graph);
    RUN(orbital_translate_chain);
    RUN(orbital_translate_no_path);
    RUN(orbital_translate_same_version);
    RUN(orbital_translate_schema_not_found);
    RUN(orbital_adapter_failure);
    RUN(orbital_orbit_manifest);

    /* Constellation Coordinator */
    RUN(constellation_init);
    RUN(constellation_register_nodes);
    RUN(constellation_node_lifecycle);
    RUN(constellation_node_schemas);
    RUN(constellation_executor);
    RUN(constellation_routing);
    RUN(constellation_health_check);
    RUN(constellation_heartbeat_recovery);

    /* Event Transport */
    RUN(transport_init);
    RUN(transport_create_channel);
    RUN(transport_activate);
    RUN(transport_send_local);
    RUN(transport_send_inactive);
    RUN(transport_send_receive_shmem);
    RUN(transport_queue_full);
    RUN(transport_process_pending);
    RUN(transport_find_no_channel);
    RUN(transport_deactivate);

    /* Integrated */
    RUN(integrated_cross_isa_pipeline);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
