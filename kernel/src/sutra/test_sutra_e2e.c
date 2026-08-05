#include <stdio.h>
#include <string.h>
#include "sutra.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static const char *TRANSFER_FUNDS_SRC =
"SUTRA-PROGRAM. TRANSFER-FUNDS.\n"
"ENVIRONMENT SECTION.\n"
"    CONFIGURATION.\n"
"        RAIL. UPI.\n"
"        MODE. PARACONSISTENT.\n"
"        AUDIT. TRIPLE-LEDGER.\n"
"\n"
"DATA SECTION.\n"
"    01 SENDER-ACCOUNT    PIC X(32) VALUE \"alice@vino\".\n"
"    01 RECEIVER-ACCOUNT  PIC X(32) VALUE \"bob@vino\".\n"
"    01 TRANSFER-AMOUNT   SUTRA-AMOUNT VALUE 500/1.\n"
"    01 CAPITAL-TYPE      SUTRA-CAPITAL VALUE FINANCIAL.\n"
"    01 TXN-STATUS        SUTRA-STATUS VALUE GLUT-NEUTRAL.\n"
"\n"
"EXECUTION SECTION.\n"
"    STEP-1 @ORDINAL(1).\n"
"        CHECK-COVERAGE SENDER-ACCOUNT CAPITAL-TYPE TRANSFER-AMOUNT\n"
"        IF COVERAGE-PASS THEN\n"
"            SET TXN-STATUS TO TRUE\n"
"        ELSE\n"
"            SET TXN-STATUS TO GLUT-MINUS\n"
"            INVOKE FS-PRA RESOLVE\n"
"        END-IF.\n"
"\n"
"    STEP-2 @ORDINAL(2).\n"
"        IF TXN-STATUS = TRUE THEN\n"
"            DEBIT SENDER-ACCOUNT TRANSFER-AMOUNT CAPITAL-TYPE\n"
"            CREDIT RECEIVER-ACCOUNT TRANSFER-AMOUNT CAPITAL-TYPE\n"
"            WRITE-TRIPLE-LEDGER\n"
"            EMIT-ISO20022 PACS.008\n"
"        END-IF.\n"
"\n"
"    STEP-3 @ORDINAL(3).\n"
"        IF TXN-STATUS = TRUE THEN\n"
"            AUDIT-CHECK\n"
"            IF AUDIT-PASS THEN\n"
"                COMMIT\n"
"            ELSE\n"
"                ROLLBACK\n"
"                SET TXN-STATUS TO GLUT-PLUS\n"
"            END-IF\n"
"        END-IF.\n"
"\n"
"    STEP-4 @ORDINAL(4).\n"
"        IF TXN-STATUS = TRUE THEN\n"
"            EMIT-SWIFT MT103\n"
"            NOTIFY-CHIGLET \"Transfer complete\"\n"
"        END-IF.\n"
"END-PROGRAM.\n";

static vino_ledger_t ledger; /* too large for the stack */

int main(void) {
    printf("=== Happy path: sufficient funds ===\n");
    {
        vino_init(&ledger, 1);
        vino_create_account(&ledger, "alice@vino", "Alice");
        vino_create_account(&ledger, "bob@vino", "Bob");
        vino_account_t *alice = vino_get_account(&ledger, "alice@vino");
        alice->balance[CAP_FINANCIAL] = 1000;

        sutra_program_t prog;
        uint32_t err_line = 0; const char *err_msg = NULL;
        bool parsed = sutra_parse(TRANSFER_FUNDS_SRC, &prog, &err_line, &err_msg);
        CHECK(parsed, "flagship program parses");
        if (!parsed) { printf("parse error line %u: %s\n", err_line, err_msg); return 1; }

        sutra_runtime_t rt;
        sutra_runtime_init(&rt, &ledger);
        bool ran = sutra_run(&rt, &prog);
        CHECK(ran, "sutra_run completes");
        CHECK(rt.steps_executed == 4, "all 4 steps executed");
        CHECK(rt.transfers_executed == 1, "exactly 1 real vino_transfer happened");

        vino_account_t *alice2 = vino_get_account(&ledger, "alice@vino");
        vino_account_t *bob2 = vino_get_account(&ledger, "bob@vino");
        CHECK(alice2->balance[CAP_FINANCIAL] == 500, "alice's REAL balance decreased by 500 (1000 -> 500)");
        CHECK(bob2->balance[CAP_FINANCIAL] == 500, "bob's REAL balance increased by 500 (0 -> 500)");

        sutra_var_t *status = NULL;
        for (uint32_t i = 0; i < prog.num_vars; i++) if (strcmp(prog.vars[i].name, "TXN-STATUS") == 0) status = &prog.vars[i];
        CHECK(status && status->status_val == TRIT_TRUE, "TXN-STATUS ended as TRUE (coverage + audit both passed)");

        CHECK(rt.last_emit_len > 0, "EMIT-SWIFT MT103 (the last EMIT executed) produced a real non-empty message");
        CHECK(rt.notify_count == 1, "NOTIFY-CHIGLET fired exactly once");
        CHECK(strcmp(rt.notify_log[3], "Transfer complete") == 0, "NOTIFY-CHIGLET message content is correct");
    }

    printf("\n=== Rejection path: insufficient funds (paraconsistent, not a crash) ===\n");
    {
        vino_init(&ledger, 2);
        vino_create_account(&ledger, "alice@vino", "Alice");
        vino_create_account(&ledger, "bob@vino", "Bob");
        vino_account_t *alice = vino_get_account(&ledger, "alice@vino");
        alice->balance[CAP_FINANCIAL] = 100; /* insufficient for a 500 transfer */

        sutra_program_t prog;
        sutra_parse(TRANSFER_FUNDS_SRC, &prog, NULL, NULL);

        sutra_runtime_t rt;
        sutra_runtime_init(&rt, &ledger);
        bool ran = sutra_run(&rt, &prog);
        CHECK(ran, "sutra_run completes even on the rejection path (no crash)");
        CHECK(rt.transfers_executed == 0, "no real transfer happened when coverage fails");

        vino_account_t *alice2 = vino_get_account(&ledger, "alice@vino");
        CHECK(alice2->balance[CAP_FINANCIAL] == 100, "alice's balance is UNCHANGED when coverage fails");

        sutra_var_t *status = NULL;
        for (uint32_t i = 0; i < prog.num_vars; i++) if (strcmp(prog.vars[i].name, "TXN-STATUS") == 0) status = &prog.vars[i];
        CHECK(status && status->status_val == TRIT_GLUT_MINUS,
              "TXN-STATUS ends as GLUT-MINUS (paraconsistent rejection state, not a boolean crash/exception)");
        CHECK(rt.notify_count == 0, "NOTIFY-CHIGLET never fires on the rejection path (STEP-4's IF correctly gates on TXN-STATUS=TRUE)");
    }

    if (failures == 0) printf("\n=== ALL SUTRA END-TO-END TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
