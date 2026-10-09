#include <stdio.h>
#include <string.h>
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

int main(void)
{
    sutra_program_t prog;
    uint32_t err_line = 0;
    const char *err_msg = NULL;

    bool ok = sutra_parse(TRANSFER_FUNDS_SRC, &prog, &err_line, &err_msg);
    if (!ok) printf("PARSE ERROR at line %u: %s\n", err_line, err_msg ? err_msg : "(null)");
    CHECK(ok, "flagship TRANSFER-FUNDS program parses without error");
    if (!ok) {
        printf("\n=== ABORTING: cannot check structure after parse failure ===\n");
        return 1;
    }

    printf("\n=== Program header ===\n");
    CHECK(strcmp(prog.name, "TRANSFER-FUNDS") == 0, "program name is TRANSFER-FUNDS");
    CHECK(prog.rail == RAIL_UPI, "rail is RAIL_UPI");

    printf("\n=== DATA SECTION ===\n");
    CHECK(prog.num_vars == 5, "exactly 5 DATA items parsed");
    CHECK(strcmp(prog.vars[0].name, "SENDER-ACCOUNT") == 0 && prog.vars[0].kind == VAR_STRING &&
              strcmp(prog.vars[0].str_val, "alice@vino") == 0,
          "SENDER-ACCOUNT is a string var with value alice@vino");
    CHECK(strcmp(prog.vars[1].name, "RECEIVER-ACCOUNT") == 0 &&
              strcmp(prog.vars[1].str_val, "bob@vino") == 0,
          "RECEIVER-ACCOUNT is a string var with value bob@vino");
    CHECK(strcmp(prog.vars[2].name, "TRANSFER-AMOUNT") == 0 && prog.vars[2].kind == VAR_AMOUNT &&
              prog.vars[2].amount_val.num == 500 && prog.vars[2].amount_val.den == 1,
          "TRANSFER-AMOUNT is an amount var with value 500/1");
    CHECK(strcmp(prog.vars[3].name, "CAPITAL-TYPE") == 0 && prog.vars[3].kind == VAR_CAPITAL &&
              prog.vars[3].capital_val == CAP_FINANCIAL,
          "CAPITAL-TYPE is a capital var with value CAP_FINANCIAL");
    CHECK(strcmp(prog.vars[4].name, "TXN-STATUS") == 0 && prog.vars[4].kind == VAR_STATUS &&
              prog.vars[4].status_val == TRIT_GLUT_NEUTRAL,
          "TXN-STATUS is a status var with value TRIT_GLUT_NEUTRAL");

    printf("\n=== EXECUTION SECTION: step count and ordinals ===\n");
    CHECK(prog.num_steps == 4, "exactly 4 STEPs parsed");
    CHECK(prog.steps[0].ordinal == 1 && prog.steps[1].ordinal == 2 && prog.steps[2].ordinal == 3 &&
              prog.steps[3].ordinal == 4,
          "step ordinals are 1,2,3,4 in array order (source happens to be already ordinal-sorted)");

    printf("\n=== STEP-1 structure: CHECK-COVERAGE + IF/THEN/ELSE ===\n");
    {
        sutra_step_t *st = &prog.steps[0];
        /* NOTE: num_stmts is a flat, monotonically-growing array-allocation
         * count shared across ALL nesting levels (top-level + THEN + ELSE
         * all live in one shared array), not "count at this nesting level" --
         * so it will be 5 here (1 CHECK-COVERAGE + 1 IF + 1 THEN-SET + 2
         * ELSE-stmts), not 2. The per-level counts are then_count/else_count. */
        CHECK(
            st->num_stmts >= 2,
            "STEP-1's flat statement array has grown to include at least the 2 top-level entries");
        CHECK(st->stmts[0].kind == STMT_CHECK_COVERAGE, "first stmt is CHECK-COVERAGE");
        CHECK(strcmp(st->stmts[0].a, "SENDER-ACCOUNT") == 0 &&
                  strcmp(st->stmts[0].b, "CAPITAL-TYPE") == 0 &&
                  strcmp(st->stmts[0].c, "TRANSFER-AMOUNT") == 0,
              "CHECK-COVERAGE operands captured correctly");
        CHECK(st->stmts[1].kind == STMT_IF, "second stmt is IF");
        CHECK(st->stmts[1].cond.kind == COND_COVERAGE_PASS, "IF condition is COVERAGE-PASS");
        CHECK(st->stmts[1].then_count == 1 && st->stmts[1].else_count == 2,
              "THEN branch has 1 stmt (SET), ELSE branch has 2 stmts (SET, INVOKE FS-PRA)");
        sutra_stmt_t *then0 = &st->stmts[st->stmts[1].then_start];
        CHECK(then0->kind == STMT_SET && strcmp(then0->a, "TXN-STATUS") == 0 &&
                  strcmp(then0->b, "TRUE") == 0,
              "THEN branch: SET TXN-STATUS TO TRUE");
        sutra_stmt_t *else0 = &st->stmts[st->stmts[1].else_start];
        sutra_stmt_t *else1 = &st->stmts[st->stmts[1].else_start + 1];
        CHECK(else0->kind == STMT_SET && strcmp(else0->b, "GLUT-MINUS") == 0,
              "ELSE branch stmt 0: SET TXN-STATUS TO GLUT-MINUS");
        CHECK(else1->kind == STMT_INVOKE_FSPRA, "ELSE branch stmt 1: INVOKE FS-PRA RESOLVE");
    }

    printf("\n=== STEP-2 structure: fused DEBIT+CREDIT -> STMT_TRANSFER ===\n");
    {
        sutra_step_t *st = &prog.steps[1];
        CHECK(st->num_stmts >= 1,
              "STEP-2's flat statement array has grown to include at least the outer IF");
        sutra_stmt_t *ifstmt = &st->stmts[0];
        CHECK(ifstmt->kind == STMT_IF && ifstmt->cond.kind == COND_STATUS_EQ &&
                  strcmp(ifstmt->cond.var, "TXN-STATUS") == 0 && ifstmt->cond.value == TRIT_TRUE,
              "IF TXN-STATUS = TRUE parsed correctly");
        CHECK(ifstmt->then_count == 3,
              "THEN branch has 3 statements (fused TRANSFER, WRITE-LEDGER, EMIT)");
        sutra_stmt_t *xfer = &st->stmts[ifstmt->then_start];
        CHECK(xfer->kind == STMT_TRANSFER, "first THEN stmt is a fused STMT_TRANSFER");
        CHECK(strcmp(xfer->a, "SENDER-ACCOUNT") == 0, "TRANSFER from-account is SENDER-ACCOUNT");
        CHECK(strcmp(xfer->b, "RECEIVER-ACCOUNT") == 0, "TRANSFER to-account is RECEIVER-ACCOUNT");
        CHECK(strcmp(xfer->c, "TRANSFER-AMOUNT") == 0, "TRANSFER amount-var is TRANSFER-AMOUNT");
        CHECK(strcmp(xfer->d, "CAPITAL-TYPE") == 0,
              "TRANSFER capital-var is CAPITAL-TYPE (the bug I caught and fixed)");
        sutra_stmt_t *wl = &st->stmts[ifstmt->then_start + 1];
        CHECK(wl->kind == STMT_WRITE_LEDGER, "second THEN stmt is WRITE-TRIPLE-LEDGER");
        sutra_stmt_t *emit = &st->stmts[ifstmt->then_start + 2];
        CHECK(emit->kind == STMT_EMIT && strcmp(emit->a, "EMIT-ISO20022") == 0,
              "third THEN stmt is EMIT-ISO20022");
        CHECK(strcmp(emit->b, "PACS.008") == 0,
              "embedded-dot subtype 'PACS.008' correctly reconstructed from IDENT+DOT+NUMBER");
    }

    printf("\n=== STEP-3 structure: nested IF (AUDIT-CHECK / AUDIT-PASS) ===\n");
    {
        sutra_step_t *st = &prog.steps[2];
        sutra_stmt_t *outer_if = &st->stmts[0];
        CHECK(outer_if->then_count == 2, "outer THEN has 2 top-level stmts (AUDIT-CHECK, nested "
                                         "IF) via then_count, the correct per-level counter");
        sutra_stmt_t *audit_check = &st->stmts[outer_if->then_start];
        CHECK(audit_check->kind == STMT_AUDIT_CHECK, "first nested stmt is AUDIT-CHECK");
        sutra_stmt_t *inner_if = &st->stmts[outer_if->then_start + 1];
        CHECK(inner_if->kind == STMT_IF && inner_if->cond.kind == COND_AUDIT_PASS,
              "nested IF condition is AUDIT-PASS");
        CHECK(inner_if->then_count == 1 && inner_if->else_count == 2,
              "nested IF: THEN=1 (COMMIT), ELSE=2 (ROLLBACK, SET)");
        CHECK(st->stmts[inner_if->then_start].kind == STMT_COMMIT, "nested THEN is COMMIT");
        CHECK(st->stmts[inner_if->else_start].kind == STMT_ROLLBACK,
              "nested ELSE stmt 0 is ROLLBACK");
    }

    printf("\n=== STEP-4 structure: EMIT-SWIFT + NOTIFY-CHIGLET ===\n");
    {
        sutra_step_t *st = &prog.steps[3];
        sutra_stmt_t *iff = &st->stmts[0];
        CHECK(iff->then_count == 2, "THEN has 2 stmts (EMIT-SWIFT, NOTIFY-CHIGLET)");
        sutra_stmt_t *emit = &st->stmts[iff->then_start];
        CHECK(emit->kind == STMT_EMIT && strcmp(emit->a, "EMIT-SWIFT") == 0 &&
                  strcmp(emit->b, "MT103") == 0,
              "EMIT-SWIFT MT103 parsed correctly (no embedded dot in this subtype)");
        sutra_stmt_t *notify = &st->stmts[iff->then_start + 1];
        CHECK(notify->kind == STMT_NOTIFY && strcmp(notify->a, "Transfer complete") == 0,
              "NOTIFY-CHIGLET string content captured correctly");
    }

    printf("\n=== Error handling: malformed program ===\n");
    {
        sutra_program_t bad_prog;
        uint32_t bl = 0;
        const char *bm = NULL;
        bool bad_ok = sutra_parse("SUTRA-PROGRAM. X\nNOT-A-DOT-HERE", &bad_prog, &bl, &bm);
        CHECK(bad_ok == false,
              "malformed program (missing dot) is rejected, not silently accepted");
        CHECK(bm != NULL, "a diagnostic error message is provided on failure");
    }

    if (failures == 0)
        printf("\n=== ALL SUTRA PARSER TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
