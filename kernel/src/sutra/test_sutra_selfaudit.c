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

static const char *GOOD_SRC = "SUTRA-PROGRAM. TEST.\n"
                              "ENVIRONMENT SECTION.\n"
                              "    CONFIGURATION.\n"
                              "        RAIL. UPI.\n"
                              "        MODE. PARACONSISTENT.\n"
                              "        AUDIT. TRIPLE-LEDGER.\n"
                              "DATA SECTION.\n"
                              "    01 SENDER-ACCOUNT    PIC X(32) VALUE \"alice@vino\".\n"
                              "    01 RECEIVER-ACCOUNT  PIC X(32) VALUE \"bob@vino\".\n"
                              "    01 TRANSFER-AMOUNT   SUTRA-AMOUNT VALUE 500/1.\n"
                              "    01 CAPITAL-TYPE      SUTRA-CAPITAL VALUE FINANCIAL.\n"
                              "EXECUTION SECTION.\n"
                              "    STEP-1 @ORDINAL(1).\n"
                              "        DEBIT SENDER-ACCOUNT TRANSFER-AMOUNT CAPITAL-TYPE\n"
                              "        CREDIT RECEIVER-ACCOUNT TRANSFER-AMOUNT CAPITAL-TYPE\n"
                              "    .\n"
                              "END-PROGRAM.\n";

static const char *DUP_ORDINAL_SRC = "SUTRA-PROGRAM. TEST.\n"
                                     "ENVIRONMENT SECTION.\n"
                                     "    CONFIGURATION.\n"
                                     "        RAIL. UPI.\n"
                                     "        MODE. PARACONSISTENT.\n"
                                     "        AUDIT. TRIPLE-LEDGER.\n"
                                     "DATA SECTION.\n"
                                     "EXECUTION SECTION.\n"
                                     "    STEP-1 @ORDINAL(1).\n"
                                     "        COMMIT\n"
                                     "    .\n"
                                     "    STEP-2 @ORDINAL(1).\n"
                                     "        COMMIT\n"
                                     "    .\n"
                                     "END-PROGRAM.\n";

int main(void)
{
    printf("=== A well-formed program passes self-audit ===\n");
    {
        sutra_program_t prog;
        bool ok = sutra_parse(GOOD_SRC, &prog, NULL, NULL);
        CHECK(ok, "well-formed program parses");
        CHECK(sutra_self_audit(&prog) == SUTRA_AUDIT_PASS, "well-formed program passes self-audit");
    }

    printf("\n=== Duplicate ordinals are flagged as a warning ===\n");
    {
        sutra_program_t prog;
        bool ok = sutra_parse(DUP_ORDINAL_SRC, &prog, NULL, NULL);
        CHECK(ok, "duplicate-ordinal program still parses (syntactically valid)");
        CHECK(sutra_self_audit(&prog) == SUTRA_AUDIT_WARN,
              "duplicate ordinals across STEPs are flagged WARN");
    }

    if (failures == 0)
        printf("\n=== ALL SUTRA SELF-AUDIT TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
