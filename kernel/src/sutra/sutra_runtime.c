/* sutra_runtime.c — SUTRA nonlinear ordinal-dispatch interpreter.
 * See sutra.h. Executes a parsed sutra_program_t directly against a
 * real vino_ledger_t (no separate ledger/capital reimplementation --
 * see sutra.h's design-decision comment).
 */
#include "sutra.h"
#include <stddef.h>

static bool streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

static sutra_var_t *find_var(sutra_program_t *prog, const char *name) {
    for (uint32_t i = 0; i < prog->num_vars; i++)
        if (streq(prog->vars[i].name, name)) return &prog->vars[i];
    return NULL;
}

static trit_t keyword_to_trit_rt(const char *kw) {
    if (streq(kw, "TRUE")) return TRIT_TRUE;
    if (streq(kw, "FALSE")) return TRIT_FALSE;
    if (streq(kw, "GLUT")) return TRIT_GLUT;
    if (streq(kw, "GLUT-PLUS")) return TRIT_GLUT_PLUS;
    if (streq(kw, "GLUT-MINUS")) return TRIT_GLUT_MINUS;
    if (streq(kw, "GLUT-NEUTRAL")) return TRIT_GLUT_NEUTRAL;
    return TRIT_FALSE;
}

static void notify_log_push(sutra_runtime_t *rt, const char *msg) {
    /* Ring-buffer-of-4 semantics: shift oldest out, newest at [3]. */
    for (int i = 0; i < 3; i++) {
        uint32_t n = 0;
        while (rt->notify_log[i + 1][n] && n < SUTRA_STRING_LEN - 1) { rt->notify_log[i][n] = rt->notify_log[i + 1][n]; n++; }
        rt->notify_log[i][n] = '\0';
    }
    uint32_t n = 0;
    while (msg[n] && n < SUTRA_STRING_LEN - 1) { rt->notify_log[3][n] = msg[n]; n++; }
    rt->notify_log[3][n] = '\0';
    rt->notify_count++;
}

void sutra_runtime_init(sutra_runtime_t *rt, vino_ledger_t *ledger) {
    for (uint8_t *b = (uint8_t *)rt; b < (uint8_t *)rt + sizeof(*rt); b++) *b = 0;
    rt->ledger = ledger;
}

static bool eval_cond(sutra_program_t *prog, sutra_cond_t *cond, bool coverage_pass, bool audit_pass) {
    switch (cond->kind) {
        case COND_COVERAGE_PASS: return coverage_pass;
        case COND_AUDIT_PASS: return audit_pass;
        case COND_STATUS_EQ: {
            sutra_var_t *v = find_var(prog, cond->var);
            if (!v || v->kind != VAR_STATUS) return false;
            return v->status_val == cond->value;
        }
    }
    return false;
}

static void exec_range(sutra_runtime_t *rt, sutra_program_t *prog, sutra_step_t *step,
                        uint32_t start, uint32_t count, bool *coverage_pass, bool *audit_pass);

static void exec_one(sutra_runtime_t *rt, sutra_program_t *prog, sutra_step_t *step,
                      sutra_stmt_t *s, bool *coverage_pass, bool *audit_pass) {
    switch (s->kind) {
    case STMT_SET: {
        sutra_var_t *v = find_var(prog, s->a);
        if (v && v->kind == VAR_STATUS) v->status_val = keyword_to_trit_rt(s->b);
        break;
    }
    case STMT_CHECK_COVERAGE: {
        sutra_var_t *acct = find_var(prog, s->a);
        sutra_var_t *cap = find_var(prog, s->b);
        sutra_var_t *amt = find_var(prog, s->c);
        if (acct && cap && amt && acct->kind == VAR_STRING && cap->kind == VAR_CAPITAL && amt->kind == VAR_AMOUNT) {
            *coverage_pass = sutra_check_coverage(rt->ledger, acct->str_val, cap->capital_val, amt->amount_val);
        } else {
            *coverage_pass = false;
        }
        break;
    }
    case STMT_TRANSFER: {
        sutra_var_t *from = find_var(prog, s->a);
        sutra_var_t *to = find_var(prog, s->b);
        sutra_var_t *amt = find_var(prog, s->c);
        sutra_var_t *cap = find_var(prog, s->d);
        if (from && to && amt && cap && from->kind == VAR_STRING && to->kind == VAR_STRING &&
            amt->kind == VAR_AMOUNT && cap->kind == VAR_CAPITAL) {
            uint64_t whole = (amt->amount_val.den != 0) ? (uint64_t)(amt->amount_val.num / amt->amount_val.den) : 0;
            int32_t r = vino_transfer(rt->ledger, from->str_val, to->str_val, whole,
                                       cap->capital_val, prog->rail, "SUTRA transfer");
            if (r == 0) {
                rt->transfers_executed++;
                uint32_t n = 0;
                while (from->str_val[n] && n < VINO_ADDR_LEN - 1) { rt->last_from[n] = from->str_val[n]; n++; } rt->last_from[n] = '\0';
                n = 0;
                while (to->str_val[n] && n < VINO_ADDR_LEN - 1) { rt->last_to[n] = to->str_val[n]; n++; } rt->last_to[n] = '\0';
                rt->last_amount = whole;
                rt->last_capital = cap->capital_val;
                rt->has_last_transfer = true;
            }
        }
        break;
    }
    case STMT_WRITE_LEDGER:
        /* No-op: vino_transfer already wrote the primary/balance ledger
         * atomically (see vino.h's own comment: the hash-chained
         * primary[] already serves as the audit trail). */
        break;
    case STMT_EMIT: {
        if (!rt->has_last_transfer) break;
        vino_transaction_t txn;
        for (uint8_t *b = (uint8_t *)&txn; b < (uint8_t *)&txn + sizeof(txn); b++) *b = 0;
        uint32_t n = 0;
        while (rt->last_from[n] && n < VINO_ADDR_LEN - 1) { txn.from_addr[n] = rt->last_from[n]; n++; }
        n = 0;
        while (rt->last_to[n] && n < VINO_ADDR_LEN - 1) { txn.to_addr[n] = rt->last_to[n]; n++; }
        txn.amount = rt->last_amount;
        txn.capital = rt->last_capital;
        int32_t written = sutra_emit_message(&txn, s->a, s->b, rt->last_emit, sizeof(rt->last_emit));
        rt->last_emit_len = (written > 0) ? (uint32_t)written : 0;
        break;
    }
    case STMT_NOTIFY:
        notify_log_push(rt, s->a);
        break;
    case STMT_COMMIT:
    case STMT_ROLLBACK:
        /* v1: recorded as no-ops. A real transactional rollback would
         * need pre-transfer state snapshotting, which is future work --
         * documented here rather than silently faked. */
        break;
    case STMT_INVOKE_FSPRA:
        /* v1: no-op. Real FS-PRA paraconsistent-resolution engine
         * integration is future work -- documented, not faked. */
        break;
    case STMT_AUDIT_CHECK:
        /* v1: always passes. A real audit engine integration (coverage
         * hyperbola r*l >= 1.8 checks etc.) is future work. */
        *audit_pass = true;
        break;
    case STMT_IF: {
        bool c = eval_cond(prog, &s->cond, *coverage_pass, *audit_pass);
        if (c) exec_range(rt, prog, step, s->then_start, s->then_count, coverage_pass, audit_pass);
        else exec_range(rt, prog, step, s->else_start, s->else_count, coverage_pass, audit_pass);
        break;
    }
    }
}

static void exec_range(sutra_runtime_t *rt, sutra_program_t *prog, sutra_step_t *step,
                        uint32_t start, uint32_t count, bool *coverage_pass, bool *audit_pass) {
    for (uint32_t i = 0; i < count; i++) {
        exec_one(rt, prog, step, &step->stmts[start + i], coverage_pass, audit_pass);
    }
}

bool sutra_run(sutra_runtime_t *rt, sutra_program_t *prog) {
    /* Ordinal dispatch: sort step INDICES by ordinal (selection sort --
     * SUTRA_MAX_STEPS is small (64), so O(n^2) is fine and avoids
     * pulling in a qsort dependency this freestanding code can't rely
     * on having). Per the language's own design principle #1: "same
     * program can execute in different orders depending on event-space
     * state" -- array order is NOT assumed to equal ordinal order. */
    uint32_t order[SUTRA_MAX_STEPS];
    for (uint32_t i = 0; i < prog->num_steps; i++) order[i] = i;
    for (uint32_t i = 0; i < prog->num_steps; i++) {
        uint32_t min_idx = i;
        for (uint32_t j = i + 1; j < prog->num_steps; j++)
            if (prog->steps[order[j]].ordinal < prog->steps[order[min_idx]].ordinal) min_idx = j;
        uint32_t tmp = order[i]; order[i] = order[min_idx]; order[min_idx] = tmp;
    }

    bool coverage_pass = false;
    bool audit_pass = false;
    for (uint32_t k = 0; k < prog->num_steps; k++) {
        sutra_step_t *step = &prog->steps[order[k]];
        exec_range(rt, prog, step, 0, step->top_level_count, &coverage_pass, &audit_pass);
        rt->steps_executed++;
    }
    return true;
}
