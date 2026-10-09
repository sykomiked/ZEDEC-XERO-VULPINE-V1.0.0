/* test_yantra.c — Host-side tests for Yantra Fabric
 *
 * Tests device registry, capability states, state machines,
 * digital twin assertions, and contradiction detection.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "event_space.h"
#include "yantra_fabric.h"

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

/* ===== Tests ===== */

TEST(yf_init_test) {
    static yf_fabric_t yf;
    yf_init(&yf);
    ASSERT(yf.num_devices == 0, "no devices after init");
    ASSERT(yf.next_device_id == 1, "next_device_id starts at 1");
    PASS();
}

TEST(yf_register_device_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "dlp-projector", "display",
                                      YF_MODEL_ONLY);
    ASSERT(idx >= 0, "device registration succeeds");
    ASSERT(yf.num_devices == 1, "one device registered");

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev != NULL, "device found by index");
    ASSERT(strcmp(dev->name, "dlp-projector") == 0, "name matches");
    ASSERT(dev->capability == YF_MODEL_ONLY, "capability is MODEL_ONLY");

    /* Find by name */
    yf_device_t *found = yf_get_device_by_name(&yf, "dlp-projector");
    ASSERT(found == dev, "found by name");

    /* Duplicate name should fail */
    int32_t dup = yf_register_device(&yf, "dlp-projector", "display",
                                      YF_MODEL_ONLY);
    ASSERT(dup < 0, "duplicate name rejected");
    PASS();
}

TEST(yf_capability_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "sensor0", "sensor",
                                      YF_EMULATED);

    bool r = yf_set_capability(&yf, (uint32_t)idx, YF_RTL_SIMULATED);
    ASSERT(r, "capability upgrade succeeds");

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev->capability == YF_RTL_SIMULATED, "capability is RTL_SIMULATED");
    ASSERT(yf.total_capability_upgrades == 1, "upgrade counted");

    /* Downgrade */
    r = yf_set_capability(&yf, (uint32_t)idx, YF_EMULATED);
    ASSERT(r, "capability downgrade succeeds");
    ASSERT(yf.total_capability_downgrades == 1, "downgrade counted");

    /* Capability name */
    const char *name = yf_capability_name(YF_CERTIFIED);
    ASSERT(strcmp(name, "CERTIFIED") == 0, "CERTIFIED name correct");

    name = yf_capability_name(YF_MODEL_ONLY);
    ASSERT(strcmp(name, "MODEL_ONLY") == 0, "MODEL_ONLY name correct");
    PASS();
}

TEST(yf_registers_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "uart0", "serial", YF_EMULATED);

    bool r = yf_device_add_register(&yf, (uint32_t)idx, "DR", 0x00,
                                      32, true, true, 0);
    ASSERT(r, "add register DR");
    r = yf_device_add_register(&yf, (uint32_t)idx, "FR", 0x18,
                                32, true, false, 0x90);
    ASSERT(r, "add register FR");

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev->num_registers == 2, "two registers");
    ASSERT(strcmp(dev->registers[0].name, "DR") == 0, "register 0 name");
    ASSERT(dev->registers[0].offset == 0x00, "register 0 offset");
    ASSERT(dev->registers[0].readable, "register 0 readable");
    ASSERT(dev->registers[0].writable, "register 0 writable");
    ASSERT(!dev->registers[1].writable, "register 1 not writable");
    PASS();
}

TEST(yf_state_machine_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "projector", "display",
                                      YF_FPGA_VERIFIED);

    yf_device_add_state(&yf, (uint32_t)idx, "off", false);
    yf_device_add_state(&yf, (uint32_t)idx, "on", false);
    yf_device_add_state(&yf, (uint32_t)idx, "safe", true);
    yf_device_add_state(&yf, (uint32_t)idx, "error", false);

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev->num_states == 4, "four states");
    ASSERT(dev->current_state == 0, "starts in state 0 (off)");

    /* Transition to on */
    bool r = yf_device_set_state(&yf, (uint32_t)idx, 1);
    ASSERT(r, "set state to on");
    ASSERT(dev->current_state == 1, "now in state 1 (on)");

    /* Transition to safe state */
    r = yf_device_set_state(&yf, (uint32_t)idx, 2);
    ASSERT(r, "set state to safe");
    ASSERT(dev->current_state == 2, "now in state 2 (safe)");
    ASSERT(dev->total_safe_state_transitions == 1, "safe transition counted");
    ASSERT(yf.total_safe_state_events == 1, "global safe events counted");

    /* Verify safe state flag */
    ASSERT(dev->states[2].is_safe_state, "state 2 is safe");
    ASSERT(!dev->states[1].is_safe_state, "state 1 is not safe");
    PASS();
}

TEST(yf_device_events_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "btn0", "input", YF_EMULATED);

    yf_device_add_state(&yf, (uint32_t)idx, "released", false);
    yf_device_add_state(&yf, (uint32_t)idx, "pressed", false);

    yf_device_add_event(&yf, (uint32_t)idx, "press", "zxv.input.press",
                         0, 1);
    yf_device_add_event(&yf, (uint32_t)idx, "release", "zxv.input.release",
                         1, 0);

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev->num_events == 2, "two events");

    /* Emit press event */
    char schema[EV_SCHEMA_LEN];
    bool r = yf_device_emit_event(&yf, (uint32_t)idx, 0, schema, sizeof(schema));
    ASSERT(r, "emit press event");
    ASSERT(strcmp(schema, "zxv.input.press") == 0, "schema matches");
    ASSERT(dev->current_state == 1, "now in pressed state");
    ASSERT(dev->total_events_emitted == 1, "one event emitted");

    /* Emit release event */
    r = yf_device_emit_event(&yf, (uint32_t)idx, 1, schema, sizeof(schema));
    ASSERT(r, "emit release event");
    ASSERT(dev->current_state == 0, "now in released state");
    ASSERT(dev->total_events_emitted == 2, "two events emitted");

    /* Try to emit press from wrong state (should fail) */
    /* We're in state 0 (released), press expects state 0, so it works */
    r = yf_device_emit_event(&yf, (uint32_t)idx, 0, schema, sizeof(schema));
    ASSERT(r, "emit press from released state");

    /* Now try press from state 1 (should fail, press expects state 0) */
    r = yf_device_emit_event(&yf, (uint32_t)idx, 0, schema, sizeof(schema));
    ASSERT(!r, "press from wrong state should fail");
    PASS();
}

TEST(yf_digital_twin_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "temp0", "sensor",
                                      YF_DEVICE_MEASURED);
    yf_device_add_state(&yf, (uint32_t)idx, "normal", false);
    yf_device_add_state(&yf, (uint32_t)idx, "safe", true);

    /* Consistent assertion */
    bool r = yf_twin_assert(&yf, (uint32_t)idx,
                             "temp.normal", "temp.normal", 100);
    ASSERT(r, "assert consistent");

    /* Contradictory assertion */
    r = yf_twin_assert(&yf, (uint32_t)idx,
                       "temp.normal", "temp.high", 200);
    ASSERT(r, "assert contradictory");

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev->num_twin_assertions == 2, "two assertions");
    ASSERT(dev->twin_assertions[0].status == YF_TWIN_CONSISTENT,
           "first is consistent");
    ASSERT(dev->twin_assertions[1].status == YF_TWIN_CONTRADICTORY,
           "second is contradictory");
    ASSERT(dev->total_contradictions == 1, "one contradiction");
    ASSERT(yf.total_contradictions == 1, "global contradiction count");

    /* Check contradictions */
    uint32_t c = yf_twin_check_contradictions(&yf, (uint32_t)idx);
    ASSERT(c == 1, "one contradiction found");

    /* Enter safe state due to contradiction */
    r = yf_twin_enter_safe_state(&yf, (uint32_t)idx);
    ASSERT(r, "enter safe state");
    ASSERT(dev->current_state == 1, "now in safe state");
    PASS();
}

TEST(yf_ready_for_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "gpu0", "accelerator",
                                      YF_FPGA_VERIFIED);

    bool ready = yf_device_ready_for(&yf, (uint32_t)idx, YF_EMULATED);
    ASSERT(ready, "FPGA_VERIFIED >= EMULATED");

    ready = yf_device_ready_for(&yf, (uint32_t)idx, YF_FPGA_VERIFIED);
    ASSERT(ready, "FPGA_VERIFIED >= FPGA_VERIFIED");

    ready = yf_device_ready_for(&yf, (uint32_t)idx, YF_DEVICE_CONNECTED);
    ASSERT(!ready, "FPGA_VERIFIED < DEVICE_CONNECTED");
    PASS();
}

TEST(yf_devices_at_capability_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    yf_register_device(&yf, "dev0", "sensor", YF_EMULATED);
    yf_register_device(&yf, "dev1", "display", YF_EMULATED);
    yf_register_device(&yf, "dev2", "input", YF_MODEL_ONLY);
    yf_register_device(&yf, "dev3", "sensor", YF_CERTIFIED);

    uint32_t indices[YF_MAX_DEVICES];
    uint32_t count = yf_get_devices_at_capability(&yf, YF_EMULATED,
                                                    indices, YF_MAX_DEVICES);
    ASSERT(count == 2, "two EMULATED devices");

    count = yf_get_devices_at_capability(&yf, YF_MODEL_ONLY,
                                          indices, YF_MAX_DEVICES);
    ASSERT(count == 1, "one MODEL_ONLY device");

    count = yf_get_devices_at_capability(&yf, YF_CERTIFIED,
                                          indices, YF_MAX_DEVICES);
    ASSERT(count == 1, "one CERTIFIED device");
    PASS();
}

TEST(yf_capability_progression_test) {
    static yf_fabric_t yf;
    yf_init(&yf);

    int32_t idx = yf_register_device(&yf, "chip0", "accelerator",
                                      YF_MODEL_ONLY);

    /* Progress through all maturity layers */
    yf_set_capability(&yf, (uint32_t)idx, YF_EMULATED);
    yf_set_capability(&yf, (uint32_t)idx, YF_RTL_SIMULATED);
    yf_set_capability(&yf, (uint32_t)idx, YF_SYNTHESIZED);
    yf_set_capability(&yf, (uint32_t)idx, YF_FPGA_VERIFIED);
    yf_set_capability(&yf, (uint32_t)idx, YF_DEVICE_CONNECTED);
    yf_set_capability(&yf, (uint32_t)idx, YF_DEVICE_MEASURED);
    yf_set_capability(&yf, (uint32_t)idx, YF_QUALIFIED);
    yf_set_capability(&yf, (uint32_t)idx, YF_CERTIFIED);

    yf_device_t *dev = yf_get_device(&yf, (uint32_t)idx);
    ASSERT(dev->capability == YF_CERTIFIED, "reached CERTIFIED");
    ASSERT(yf.total_capability_upgrades == 8, "8 upgrades");
    ASSERT(yf.total_capability_downgrades == 0, "no downgrades");

    /* Verify all capability names */
    ASSERT(strcmp(yf_capability_name(YF_MODEL_ONLY), "MODEL_ONLY") == 0, "name 0");
    ASSERT(strcmp(yf_capability_name(YF_CERTIFIED), "CERTIFIED") == 0, "name 8");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV Yantra Fabric Tests ===\n\n");

    RUN(yf_init_test);
    RUN(yf_register_device_test);
    RUN(yf_capability_test);
    RUN(yf_registers_test);
    RUN(yf_state_machine_test);
    RUN(yf_device_events_test);
    RUN(yf_digital_twin_test);
    RUN(yf_ready_for_test);
    RUN(yf_devices_at_capability_test);
    RUN(yf_capability_progression_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
