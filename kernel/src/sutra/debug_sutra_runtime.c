#include <stdio.h>
#include <string.h>
#include "sutra.h"

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
"END-PROGRAM.\n";

static vino_ledger_t ledger;

int main(void) {
    vino_init(&ledger, 1);
    vino_create_account(&ledger, "alice@vino", "Alice");
    vino_account_t *alice = vino_get_account(&ledger, "alice@vino");
    alice->balance[CAP_FINANCIAL] = 1000;

    sutra_program_t prog;
    uint32_t err_line; const char *err_msg;
    bool ok = sutra_parse(TRANSFER_FUNDS_SRC, &prog, &err_line, &err_msg);
    printf("parsed=%d num_vars=%u num_steps=%u\n", ok, prog.num_vars, prog.num_steps);
    for (uint32_t i = 0; i < prog.num_vars; i++) {
        printf("  var[%u] name='%s' kind=%d str='%s' amount=%lld/%lld capital=%d status=%d\n",
               i, prog.vars[i].name, prog.vars[i].kind, prog.vars[i].str_val,
               (long long)prog.vars[i].amount_val.num, (long long)prog.vars[i].amount_val.den,
               prog.vars[i].capital_val, prog.vars[i].status_val);
    }

    sutra_step_t *st = &prog.steps[0];
    printf("step[0] ordinal=%u top_level_count=%u num_stmts=%u\n", st->ordinal, st->top_level_count, st->num_stmts);
    printf("stmt[0].kind=%d a='%s' b='%s' c='%s'\n", st->stmts[0].kind, st->stmts[0].a, st->stmts[0].b, st->stmts[0].c);
    printf("stmt[1].kind=%d cond.kind=%d then_start=%u then_count=%u else_start=%u else_count=%u\n",
           st->stmts[1].kind, st->stmts[1].cond.kind, st->stmts[1].then_start, st->stmts[1].then_count,
           st->stmts[1].else_start, st->stmts[1].else_count);

    /* Directly call sutra_check_coverage the same way the runtime does */
    bool cov = sutra_check_coverage(&ledger, "alice@vino", CAP_FINANCIAL, prog.vars[2].amount_val);
    printf("direct sutra_check_coverage(alice, FINANCIAL, TRANSFER-AMOUNT) = %d\n", cov);

    sutra_runtime_t rt;
    sutra_runtime_init(&rt, &ledger);
    sutra_run(&rt, &prog);

    for (uint32_t i = 0; i < prog.num_vars; i++) {
        if (strcmp(prog.vars[i].name, "TXN-STATUS") == 0)
            printf("AFTER RUN: TXN-STATUS status_val=%d\n", prog.vars[i].status_val);
    }
    return 0;
}
