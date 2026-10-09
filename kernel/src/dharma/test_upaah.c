#include <stdio.h>
#include "upaah.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond)                                                                                  \
            printf("PASS: %s\n", msg);                                                             \
        else {                                                                                     \
            printf("FAIL: %s\n", msg);                                                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    printf("=== UPAAH (+) ===\n");
    CHECK(upaah_applies(TRIT_GLUT_PLUS), "UPAAH applies to TRIT_GLUT_PLUS (charge +1)");
    CHECK(!upaah_applies(TRIT_GLUT_MINUS) && !upaah_applies(TRIT_TRUE) &&
              !upaah_applies(TRIT_FALSE),
          "UPAAH does not apply to negative-charge or zero-charge trits");
    CHECK(upaah_bridge(TRIT_GLUT_PLUS) == VEIL_AIN_SOPH_AUR,
          "UPAAH bridges GLUT_PLUS to the source of emanation");

    printf("\n=== VPAAH (-) ===\n");
    CHECK(vpaah_applies(TRIT_GLUT_MINUS), "VPAAH applies to TRIT_GLUT_MINUS (charge -1)");
    CHECK(vpaah_bridge(TRIT_GLUT_MINUS) == VEIL_AIN_SOPH,
          "VPAAH bridges GLUT_MINUS to unbounded/unresolved potential");

    printf("\n=== PIR (+&-) ===\n");
    CHECK(pir_applies(TRIT_GLUT) && pir_applies(TRIT_GLUT_NEUTRAL) && pir_applies(TRIT_TRUE) &&
              pir_applies(TRIT_FALSE),
          "PIR applies to every zero-charge trit (balanced GLUT and both definite trits)");
    CHECK(pir_bridge(TRIT_GLUT_NEUTRAL) == SEPH_YESOD,
          "PIR bridges balanced GLUT to staged foundation");
    CHECK(pir_bridge(TRIT_TRUE) == SEPH_MALKUTH,
          "PIR bridges TRIT_TRUE (definite, non-glut) to Malkuth");
    CHECK(pir_bridge(TRIT_FALSE) == VEIL_AIN, "PIR bridges TRIT_FALSE (definite, non-glut) to Ain");

    printf("\n=== phase7_bridge: complete dispatcher ===\n");
    CHECK(phase7_bridge(TRIT_GLUT_PLUS) == VEIL_AIN_SOPH_AUR,
          "dispatches GLUT_PLUS to UPAAH's result");
    CHECK(phase7_bridge(TRIT_GLUT_MINUS) == VEIL_AIN_SOPH,
          "dispatches GLUT_MINUS to VPAAH's result");
    CHECK(phase7_bridge(TRIT_TRUE) == SEPH_MALKUTH, "dispatches TRIT_TRUE to PIR's result");
    CHECK(phase7_bridge(TRIT_FALSE) == VEIL_AIN, "dispatches TRIT_FALSE to PIR's result");

    printf("\n=== Round trip ===\n");
    CHECK(phase7_unbridge(phase7_bridge(TRIT_TRUE)) == TRIT_TRUE,
          "TRIT_TRUE round-trips through bridge/unbridge");
    CHECK(phase7_unbridge(phase7_bridge(TRIT_GLUT_PLUS)) == TRIT_GLUT_PLUS,
          "TRIT_GLUT_PLUS round-trips");
    CHECK(phase7_unbridge((l13_phase_t) 0) == TRIT_FALSE,
          "an unmapped/zero phase un-bridges to a definite TRIT_FALSE, not a crash");

    if (failures == 0)
        printf("\n=== ALL UPAAH/VPAAH/PIR TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
