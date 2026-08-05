/* test_neon.c — Neon Orchestrator Tests
 * Tests plugin pipeline: FILTER -> CONSENT -> TRANSMUTE -> ASCEND -> REFLECT -> LOG -> PERSIST
 * plus ethics gate (MEEMO).
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "neon_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== Neon Orchestrator Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 256;
    double complex entries[256];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    neon_orchestrator_t orch;
    neon_init(&orch, &matrix);

    neon_register_plugin(&orch, NEON_FILTER, "filter", neon_plugin_filter, NULL, 1.0);
    neon_register_plugin(&orch, NEON_CONSENT, "consent", neon_plugin_consent, NULL, 1.0);
    neon_register_plugin(&orch, NEON_TRANSMUTE, "transmute", neon_plugin_transmute, NULL, 1.618);
    neon_register_plugin(&orch, NEON_ASCEND, "ascend", neon_plugin_ascend, NULL, 1.618);
    neon_register_plugin(&orch, NEON_REFLECT, "reflect", neon_plugin_reflect, NULL, 2.618);
    neon_register_plugin(&orch, NEON_LOG, "log", neon_plugin_log, NULL, 1.0);
    neon_register_plugin(&orch, NEON_PERSIST, "persist", neon_plugin_persist, NULL, 1.0);
    neon_register_plugin(&orch, NEON_ETHICS, "ethics", neon_plugin_ethics, NULL, 1.618);

    assert(orch.num_plugins == 8);

    neon_data_t data;
    memset(&data, 0, sizeof(data));
    memcpy(data.payload, "Hello World", 11);
    data.payload_len = 11;
    data.consent_granted = true;
    data.emotion_index = 0.5;

    int rc = neon_execute(&orch, &data);
    assert(rc == 0);
    assert(data.persisted == true);
    assert(data.attestation == TRIT_TRUE);
    assert(orch.cycle_count == 1);
    printf("  [PASS] Full pipeline execution (FILTER->PERSIST+ETHICS)\n");

    neon_data_t data2;
    memset(&data2, 0, sizeof(data2));
    data2.payload_len = 0;
    data2.consent_granted = true;
    data2.emotion_index = 0.5;
    rc = neon_execute(&orch, &data2);
    assert(rc == -1);
    assert(data2.filtered == true);
    printf("  [PASS] Filter rejects empty payload\n");

    neon_data_t data3;
    memset(&data3, 0, sizeof(data3));
    memcpy(data3.payload, "test", 4);
    data3.payload_len = 4;
    data3.consent_granted = false;
    data3.emotion_index = 0.5;
    rc = neon_execute(&orch, &data3);
    assert(rc == -2);
    printf("  [PASS] Consent gate blocks unconsented data\n");

    neon_data_t data4;
    memset(&data4, 0, sizeof(data4));
    memcpy(data4.payload, "test", 4);
    data4.payload_len = 4;
    data4.consent_granted = true;
    data4.emotion_index = -0.8;
    rc = neon_execute(&orch, &data4);
    assert(rc == -3);
    printf("  [PASS] Ethics gate blocks negative emotion data\n");

    neon_data_t data5;
    memset(&data5, 0, sizeof(data5));
    memcpy(data5.payload, "reverse", 7);
    data5.payload_len = 7;
    data5.consent_granted = true;
    data5.emotion_index = 0.7;
    rc = neon_execute(&orch, &data5);
    assert(rc == 0);
    char reversed[8] = {0};
    memcpy(reversed, data5.payload, 7);
    assert(reversed[0] == 'e' && reversed[6] == 'r');
    printf("  [PASS] Transmute reverses payload\n");

    printf("=== All neon tests passed ===\n\n");
    return 0;
}
