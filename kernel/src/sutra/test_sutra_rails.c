#include <stdio.h>
#include "sutra.h"

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
    vino_transaction_t txn;
    for (uint8_t *b = (uint8_t *) &txn; b < (uint8_t *) &txn + sizeof(txn); b++) *b = 0;
    txn.amount = 500;
    txn.capital = CAP_FINANCIAL;

    char out[512];
    int32_t n;

    printf("=== EMIT-ISO20022 dispatch ===\n");
    n = sutra_emit_message(&txn, "EMIT-ISO20022", "PACS.008", out, sizeof(out));
    CHECK(n > 0, "EMIT-ISO20022 PACS.008 dispatches successfully (n > 0)");

    n = sutra_emit_message(&txn, "EMIT-ISO20022", "CAMT.053", out, sizeof(out));
    CHECK(n > 0, "EMIT-ISO20022 CAMT.053 dispatches successfully (n > 0)");

    n = sutra_emit_message(&txn, "EMIT-ISO20022", "PAIN001", out, sizeof(out));
    CHECK(n > 0, "EMIT-ISO20022 with an unrecognized subtype falls back to generic "
                 "vino_msg_to_iso20022 (n > 0)");

    printf("\n=== EMIT-SWIFT dispatch ===\n");
    n = sutra_emit_message(&txn, "EMIT-SWIFT", "MT103", out, sizeof(out));
    CHECK(n > 0, "EMIT-SWIFT MT103 dispatches successfully (n > 0)");

    n = sutra_emit_message(&txn, "EMIT-SWIFT", "MT202", out, sizeof(out));
    CHECK(n > 0, "EMIT-SWIFT with an unrecognized subtype falls back to MT103 (n > 0)");

    printf("\n=== Unrecognized emit_form ===\n");
    n = sutra_emit_message(&txn, "EMIT-BOGUS", "MT103", out, sizeof(out));
    CHECK(n == -1, "unrecognized emit_form returns -1, does not crash");

    if (failures == 0)
        printf("\n=== ALL SUTRA RAILS TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
