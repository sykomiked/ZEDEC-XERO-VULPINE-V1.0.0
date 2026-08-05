/* test_security.c — Security Fabric Tests
 * Tests 5-layer zero-trust: HSM keys, policy gates, sessions, threshold sigs, time gates.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "security_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== Security Fabric Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 256;
    double complex entries[256];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    security_fabric_t sec;
    security_init(&sec, &matrix);
    assert(sec.threshold_required == 3);

    uint32_t key0 = security_create_key(&sec);
    uint32_t key1 = security_create_key(&sec);
    assert(key0 == 0 && key1 == 1);
    assert(sec.num_keys == 2);
    assert(sec.keys[0].active);
    printf("  [PASS] HSM key creation\n");

    assert(security_rotate_key(&sec, key0) == 0);
    assert(sec.keys[key0].rotation_cycle == 1);
    printf("  [PASS] Key rotation\n");

    uint32_t pol0 = security_add_policy(&sec, "admin_access", TRIT_TRUE, (rational_t){1, 1}, SEC_LAYER_POLICY);
    assert(pol0 == 0);
    assert(security_check_policy(&sec, pol0, TRIT_TRUE, (rational_t){2, 1}) == 0);
    assert(security_check_policy(&sec, pol0, TRIT_FALSE, (rational_t){2, 1}) == -3);
    printf("  [PASS] Policy gate enforcement\n");

    uint32_t sess0 = security_create_session(&sec, 42, 100);
    assert(sess0 == 0);
    assert(security_session_valid(&sec, sess0));
    printf("  [PASS] Session creation and validation\n");

    security_add_threshold_sig(&sec, 0, 0);
    security_add_threshold_sig(&sec, 1, 0);
    security_add_threshold_sig(&sec, 2, 0);
    assert(security_threshold_reached(&sec, 0));
    printf("  [PASS] Threshold BLS (3/3 signatures)\n");

    security_add_threshold_sig(&sec, 0, 1);
    assert(!security_threshold_reached(&sec, 1));
    printf("  [PASS] Insufficient threshold rejected\n");

    int auth_rc = security_authenticate(&sec, sess0, pol0);
    assert(auth_rc == 0);
    printf("  [PASS] Full authentication flow\n");

    sec.sessions[sess0].emotion_index = -0.8;
    auth_rc = security_authenticate(&sec, sess0, pol0);
    assert(auth_rc == -5);
    assert(!sec.sessions[sess0].authenticated);
    printf("  [PASS] Negative emotion blocks authentication\n");

    printf("=== All security tests passed ===\n\n");
    return 0;
}
