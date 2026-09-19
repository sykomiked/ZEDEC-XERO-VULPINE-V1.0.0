/* sutra_selfaudit.c — R4 self-audit pattern for SUTRA programs.
 * Checks structural invariants a correctly-parsed program must
 * satisfy; does not re-verify business logic (that's sutra_run's job).
 */
#include "sutra.h"

static bool streq_sa(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

sutra_audit_result_t sutra_self_audit(const sutra_program_t *prog) {
    if (prog->num_vars > SUTRA_MAX_VARS || prog->num_steps > SUTRA_MAX_STEPS) {
        return SUTRA_AUDIT_FAIL; /* corrupted counts -- should be structurally impossible */
    }

    /* All rational_t VAR_AMOUNT values must be normalized-safe: den != 0. */
    for (uint32_t i = 0; i < prog->num_vars; i++) {
        if (prog->vars[i].kind == VAR_AMOUNT && prog->vars[i].amount_val.den == 0) {
            return SUTRA_AUDIT_FAIL;
        }
        if (prog->vars[i].kind == VAR_CAPITAL && prog->vars[i].capital_val >= CAP_MAX) {
            return SUTRA_AUDIT_WARN;
        }
    }

    /* Ordinals should be distinct -- duplicate ordinals make dispatch
     * order ambiguous (the sort is stable but the language's own
     * design assumes each STEP has a unique position). */
    for (uint32_t i = 0; i < prog->num_steps; i++) {
        for (uint32_t j = i + 1; j < prog->num_steps; j++) {
            if (prog->steps[i].ordinal == prog->steps[j].ordinal) return SUTRA_AUDIT_WARN;
        }
    }

    /* Every IF's then/else ranges must stay within this step's own
     * flat stmts[] array bounds -- a violation here would mean the
     * parser produced a dangling range (should be structurally
     * impossible, but self-audit exists precisely to catch "should be
     * impossible" cases). */
    for (uint32_t s = 0; s < prog->num_steps; s++) {
        const sutra_step_t *step = &prog->steps[s];
        if (step->num_stmts > SUTRA_MAX_STMTS_PER_STEP) return SUTRA_AUDIT_FAIL;
        for (uint32_t i = 0; i < step->num_stmts; i++) {
            const sutra_stmt_t *st = &step->stmts[i];
            if (st->kind == STMT_IF) {
                if (st->then_start + st->then_count > step->num_stmts) return SUTRA_AUDIT_FAIL;
                if (st->else_start + st->else_count > step->num_stmts) return SUTRA_AUDIT_FAIL;
            }
            /* TRANSFER/CHECK-COVERAGE must reference variable names
             * that actually exist -- a dangling reference would be a
             * silent no-op in sutra_run (find_var returns NULL and the
             * statement is skipped), which is safe but worth flagging. */
            if (st->kind == STMT_TRANSFER || st->kind == STMT_CHECK_COVERAGE) {
                bool found_a = false, found_b = false;
                for (uint32_t v = 0; v < prog->num_vars; v++) {
                    if (streq_sa(prog->vars[v].name, st->a)) found_a = true;
                    if (streq_sa(prog->vars[v].name, st->b)) found_b = true;
                }
                if (!found_a || !found_b) return SUTRA_AUDIT_WARN;
            }
        }
    }

    return SUTRA_AUDIT_PASS;
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES(sutra_parse_ready): the audit reads a parsed program.
 * sutra_selfaudit.o's own `nm -u` is empty because sutra_program_t arrives
 * from the caller -- the edge is in the type, and it is declared here rather
 * than left implicit.
 */
#include "zxv_decl.h"
ZXV_DECLARE(sutra_selfaudit,
    ZXV_PROVIDES(sutra_audit_ready),
    ZXV_REQUIRES(sutra_parse_ready),
    ZXV_NO_BRINGUP);
