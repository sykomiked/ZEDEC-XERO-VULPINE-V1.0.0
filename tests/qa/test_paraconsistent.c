/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include "lpres.h"
#include "m5_types.h"
#include "lpres_core.h"
static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
/* Independent Belnap oracle: bit 0 is positive evidence, bit 1 negative.
 * Conjunction intersects positive evidence and unions negative evidence;
 * disjunction does the dual. No production operation is used by the oracle. */
static int meet(int a, int b)
{
    return (a & b & 1) | ((a | b) & 2);
}
static int join(int a, int b)
{
    return ((a | b) & 1) | (a & b & 2);
}
static void algebra(void)
{
    for (int a = 0; a < 4; a++) {
        CHECK(lpres_negate(lpres_negate(a)) == (lpres_state_t) a);
        CHECK(lpres_implies_actuator_authority(a) == (a == 1));
        for (int b = 0; b < 4; b++) {
            CHECK(lpres_conjoin(a, b) == (lpres_state_t) meet(a, b));
            CHECK(lpres_disjoin(a, b) == (lpres_state_t) join(a, b));
            CHECK(lpres_conjoin(a, b) == lpres_conjoin(b, a));
            CHECK(lpres_disjoin(a, b) == lpres_disjoin(b, a));
            CHECK(lpres_negate(lpres_conjoin(a, b)) ==
                  lpres_disjoin(lpres_negate(a), lpres_negate(b)));
            CHECK(lpres_conjoin(a, lpres_disjoin(a, b)) == (lpres_state_t) a);
            for (int c = 0; c < 4; c++) {
                CHECK(lpres_conjoin(a, lpres_conjoin(b, c)) ==
                      lpres_conjoin(lpres_conjoin(a, b), c));
                CHECK(lpres_disjoin(a, lpres_disjoin(b, c)) ==
                      lpres_disjoin(lpres_disjoin(a, b), c));
                CHECK(lpres_conjoin(a, lpres_disjoin(b, c)) ==
                      lpres_disjoin(lpres_conjoin(a, b), lpres_conjoin(a, c)));
            }
        }
    }
    /* Contradiction does not authorize an unrelated action (no explosion). */
    CHECK(!lpres_implies_actuator_authority(LPRES_STATE_BOTH));
}
static void registry(void)
{
    lpres_registry_t r;
    lpres_registry_init(&r);
    CHECK(lpres_attest(NULL, "x", 0, 0, NULL, NULL, 0) == -1);
    CHECK(lpres_attest(&r, NULL, 0, 0, NULL, NULL, 0) == -1);
    int x = lpres_attest(&r, "sensor", LPRES_SUBJECT_SENSOR, LPRES_STATE_NEITHER, "qa", NULL, 0);
    CHECK(x == 0);
    CHECK(!lpres_safety_gate_clear(&r));
    CHECK(lpres_add_evidence(&r, x, true));
    CHECK(lpres_get(&r, x)->state == LPRES_STATE_TRUE);
    CHECK(lpres_add_evidence(&r, x, false));
    CHECK(lpres_get(&r, x)->state == LPRES_STATE_BOTH);
    CHECK(lpres_count_contradictions(&r) == 1);
    CHECK(!lpres_safety_gate_clear(&r));
    r.attestations[x].positive_evidence = UINT32_MAX;
    r.attestations[x].negative_evidence = UINT32_MAX;
    CHECK(lpres_add_evidence(&r, x, true));
    CHECK(r.attestations[x].positive_evidence == UINT32_MAX);
    CHECK(r.attestations[x].state == LPRES_STATE_BOTH);
    CHECK(lpres_add_evidence(&r, x, false));
    CHECK(r.attestations[x].negative_evidence == UINT32_MAX);
    CHECK(r.attestations[x].state == LPRES_STATE_BOTH);
    CHECK(lpres_revoke(&r, x));
    CHECK(!lpres_add_evidence(&r, x, true));
    CHECK(!lpres_update_state(&r, x, LPRES_STATE_TRUE));
    CHECK(lpres_get(&r, LPRES_MAX_ATTESTATIONS) == NULL);
    for (unsigned i = 1; i < LPRES_MAX_ATTESTATIONS; i++)
        CHECK(lpres_attest(&r, "entry", 0, 1, NULL, NULL, 0) == (int) i);
    CHECK(lpres_attest(&r, "overflow", 0, 1, NULL, NULL, 0) == -1);
    CHECK(r.count == LPRES_MAX_ATTESTATIONS);
}
static void trits(void)
{
    CHECK(trit_canon_is_sound());
    CHECK(trit_canon(TRIT_GLUT) == TRIT_GLUT_NEUTRAL);
    CHECK(!trit_is_canonical((trit_t) 99));
    for (int i = 0; i < 6; i++) CHECK(trit_canon(trit_canon(i)) == trit_canon(i));
    lpres_init();
    lpres_set_presence(7, TRIT_GLUT);
    CHECK(lpres_get_presence(7) == TRIT_GLUT_NEUTRAL);
    lpres_set_presence(8, TRIT_TRUE);
    lpres_set_presence(7, TRIT_GLUT_MINUS);
    CHECK(lpres_get_presence(8) == TRIT_TRUE);
    CHECK(lpres_get_presence(7) == TRIT_GLUT_MINUS);
    lpres_set_presence(7, (trit_t) 99);
    CHECK(lpres_get_presence(7) == TRIT_GLUT_MINUS);
}
/* Exhaust every eight-event evidence sequence from every initial state.
 * The model records existence of positive/negative evidence independently. */
static void evidence_sequences(void)
{
    for (unsigned initial = 0; initial < 4; initial++) {
        for (unsigned sequence = 0; sequence < 256; sequence++) {
            lpres_registry_t r;
            lpres_registry_init(&r);
            int idx = lpres_attest(&r, "subject", LPRES_SUBJECT_SENSOR, initial, "qa", NULL, 0);
            unsigned expected = initial;
            for (unsigned step = 0; step < 8; step++) {
                bool positive = (sequence >> step) & 1;
                expected |= positive ? 1u : 2u;
                CHECK(lpres_add_evidence(&r, idx, positive));
                CHECK(lpres_get(&r, idx)->state == (lpres_state_t) expected);
                CHECK(lpres_implies_actuator_authority(lpres_get(&r, idx)->state) ==
                      (expected == 1));
            }
        }
    }
}
int main(void)
{
    algebra();
    registry();
    trits();
    evidence_sequences();
    printf("paraconsistent: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
