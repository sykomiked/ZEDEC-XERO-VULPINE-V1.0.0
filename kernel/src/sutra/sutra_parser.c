/* sutra_parser.c — SUTRA recursive-descent parser. See sutra.h.
 *
 * Statement boundaries within a STEP are keyword-driven, not
 * punctuation-driven (COBOL tradition): each statement starts with a
 * recognized keyword (CHECK-COVERAGE, IF, SET, DEBIT, CREDIT, ...)
 * and consumes exactly its own tokens; only IF/END-IF and the
 * trailing STEP dot use explicit terminators.
 */
#include "sutra.h"
#include <stddef.h>

typedef struct parser {
    sutra_token_t toks[SUTRA_MAX_TOKENS];
    uint32_t count;
    uint32_t pos;
    uint32_t err_line;
    const char *err_msg;
    bool failed;
} parser_t;

static bool streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

static const sutra_token_t *cur(parser_t *p) { return &p->toks[p->pos]; }
static void advance(parser_t *p) { if (p->pos < p->count - 1) p->pos++; }

static void fail(parser_t *p, const char *msg) {
    if (!p->failed) { p->failed = true; p->err_line = cur(p)->line; p->err_msg = msg; }
}

static bool at_ident(parser_t *p, const char *kw) {
    return cur(p)->type == TOK_IDENT && streq(cur(p)->text, kw);
}

/* Consumes an IDENT matching kw exactly, else records a parse failure. */
static void expect_ident(parser_t *p, const char *kw) {
    if (!at_ident(p, kw)) { fail(p, kw); return; }
    advance(p);
}
static void expect_dot(parser_t *p) {
    if (cur(p)->type != TOK_DOT) { fail(p, "expected '.'"); return; }
    advance(p);
}

static trit_t keyword_to_trit(const char *kw) {
    if (streq(kw, "TRUE")) return TRIT_TRUE;
    if (streq(kw, "FALSE")) return TRIT_FALSE;
    if (streq(kw, "GLUT")) return TRIT_GLUT;
    if (streq(kw, "GLUT-PLUS")) return TRIT_GLUT_PLUS;
    if (streq(kw, "GLUT-MINUS")) return TRIT_GLUT_MINUS;
    if (streq(kw, "GLUT-NEUTRAL")) return TRIT_GLUT_NEUTRAL;
    return TRIT_FALSE;
}

/* SUTRA's capital keywords map onto vino's capital_type_t, which is the
 * canonical zcap_form_t order (zcap_forms.h). The canonical names are
 * accepted; SUTRA_LANGUAGE_DIRECTIVE.md's older keywords are kept as
 * aliases (MATERIAL = MANUFACTURED, TEMPORAL = SYSTEM, RELATIONAL = HUMAN).
 * An unknown keyword maps to CAP_MAX (out of range), which the self-audit
 * flags and sutra_check_coverage refuses; it used to become FINANCIAL
 * silently. */
static capital_type_t keyword_to_capital(const char *kw) {
    if (streq(kw, "FINANCIAL"))    return CAP_FINANCIAL;
    if (streq(kw, "MANUFACTURED")) return CAP_MANUFACTURED;
    if (streq(kw, "MATERIAL")) return CAP_MANUFACTURED;
    if (streq(kw, "INTELLECTUAL")) return CAP_INTELLECTUAL;
    if (streq(kw, "HUMAN")) return CAP_HUMAN;
    if (streq(kw, "RELATIONAL")) return CAP_HUMAN;
    if (streq(kw, "SOCIAL"))       return CAP_SOCIAL;
    if (streq(kw, "NATURAL")) return CAP_NATURAL;
    if (streq(kw, "CULTURAL"))     return CAP_CULTURAL;
    if (streq(kw, "SPIRITUAL"))    return CAP_SPIRITUAL;
    if (streq(kw, "SYSTEM")) return CAP_SYSTEM;
    if (streq(kw, "TEMPORAL")) return CAP_SYSTEM;
    return CAP_MAX;
}

static payment_rail_t keyword_to_rail(const char *kw) {
    if (streq(kw, "UPI"))      return RAIL_UPI;
    if (streq(kw, "SWIFT"))    return RAIL_SWIFT;
    if (streq(kw, "ISO20022")) return RAIL_ISO20022;
    if (streq(kw, "FEDWIRE"))  return RAIL_FEDWIRE;
    if (streq(kw, "RTGS"))     return RAIL_RTGS;
    if (streq(kw, "SEPA"))     return RAIL_SEPA;
    if (streq(kw, "ACH"))      return RAIL_ACH;
    if (streq(kw, "PIX"))      return RAIL_PIX;
    return RAIL_VINO_NATIVE;
}

static void copy_ident_text(char *dst, const char *src) {
    uint32_t i = 0;
    while (src[i] && i < SUTRA_IDENT_LEN - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ---- DATA SECTION: "01 NAME <type> VALUE <value>." ---- */

static void parse_data_item(parser_t *p, sutra_program_t *prog) {
    if (cur(p)->type != TOK_NUMBER) { fail(p, "expected level-number (e.g. 01)"); return; }
    advance(p); /* level number, not stored -- SUTRA v1 is flat (no nested 05/10 groups) */

    if (cur(p)->type != TOK_IDENT) { fail(p, "expected data item name"); return; }
    if (prog->num_vars >= SUTRA_MAX_VARS) { fail(p, "too many DATA items"); return; }
    sutra_var_t *v = &prog->vars[prog->num_vars];
    copy_ident_text(v->name, cur(p)->text);
    advance(p);

    if (at_ident(p, "PIC")) {
        advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected PIC picture class (e.g. X)"); return; }
        advance(p); /* picture class letter, e.g. X -- size below is the only part we act on */
        if (cur(p)->type == TOK_LPAREN) {
            advance(p);
            if (cur(p)->type != TOK_NUMBER) { fail(p, "expected PIC size"); return; }
            advance(p);
            if (cur(p)->type != TOK_RPAREN) { fail(p, "expected ')'"); return; }
            advance(p);
        }
        v->kind = VAR_STRING;
        v->str_val[0] = '\0';
    } else if (at_ident(p, "SUTRA-AMOUNT")) {
        advance(p);
        v->kind = VAR_AMOUNT;
        v->amount_val = (rational_t){0, 1};
    } else if (at_ident(p, "SUTRA-CAPITAL")) {
        advance(p);
        v->kind = VAR_CAPITAL;
        v->capital_val = CAP_FINANCIAL;
    } else if (at_ident(p, "SUTRA-STATUS")) {
        advance(p);
        v->kind = VAR_STATUS;
        v->status_val = TRIT_FALSE;
    } else {
        fail(p, "expected a data type (PIC X(n), SUTRA-AMOUNT, SUTRA-CAPITAL, or SUTRA-STATUS)");
        return;
    }

    if (at_ident(p, "VALUE")) {
        advance(p);
        switch (v->kind) {
            case VAR_STRING:
                if (cur(p)->type != TOK_STRING) { fail(p, "expected string VALUE"); return; }
                copy_ident_text(v->str_val, cur(p)->str);
                advance(p);
                break;
            case VAR_AMOUNT:
                if (cur(p)->type != TOK_RATIONAL && cur(p)->type != TOK_NUMBER) { fail(p, "expected numeric VALUE"); return; }
                v->amount_val = (rational_t){cur(p)->num, cur(p)->den};
                advance(p);
                break;
            case VAR_CAPITAL:
                if (cur(p)->type != TOK_IDENT) { fail(p, "expected capital-name VALUE"); return; }
                v->capital_val = keyword_to_capital(cur(p)->text);
                advance(p);
                break;
            case VAR_STATUS:
                if (cur(p)->type != TOK_IDENT) { fail(p, "expected status-name VALUE"); return; }
                v->status_val = keyword_to_trit(cur(p)->text);
                advance(p);
                break;
        }
    }
    expect_dot(p);
    prog->num_vars++;
}

/* ---- Conditions: "COVERAGE-PASS" | "AUDIT-PASS" | "<var> = <status>" ---- */

static void parse_condition(parser_t *p, sutra_cond_t *cond) {
    cond->negate = false;
    if (at_ident(p, "COVERAGE-PASS")) { advance(p); cond->kind = COND_COVERAGE_PASS; return; }
    if (at_ident(p, "AUDIT-PASS"))    { advance(p); cond->kind = COND_AUDIT_PASS; return; }
    if (cur(p)->type != TOK_IDENT) { fail(p, "expected a condition"); return; }
    cond->kind = COND_STATUS_EQ;
    copy_ident_text(cond->var, cur(p)->text);
    advance(p);
    if (cur(p)->type != TOK_EQUALS) { fail(p, "expected '=' in condition"); return; }
    advance(p);
    if (cur(p)->type != TOK_IDENT) { fail(p, "expected a status value after '='"); return; }
    cond->value = keyword_to_trit(cur(p)->text);
    advance(p);
}

/* ---- Statements ---- */

static bool at_step_or_end(parser_t *p) {
    return (cur(p)->type == TOK_IDENT &&
            (streq(cur(p)->text, "END-PROGRAM") ||
             (cur(p)->text[0] == 'S' && cur(p)->text[1] == 'T' && cur(p)->text[2] == 'E' && cur(p)->text[3] == 'P')));
}

static void parse_stmt_list(parser_t *p, sutra_step_t *step, uint32_t *start, uint32_t *count);

static void parse_one_stmt(parser_t *p, sutra_step_t *step) {
    if (step->num_stmts >= SUTRA_MAX_STMTS_PER_STEP) { fail(p, "too many statements in one STEP"); return; }
    sutra_stmt_t *s = &step->stmts[step->num_stmts];

    if (at_ident(p, "CHECK-COVERAGE")) {
        advance(p);
        s->kind = STMT_CHECK_COVERAGE;
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected account var after CHECK-COVERAGE"); return; }
        copy_ident_text(s->a, cur(p)->text); advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected capital var after CHECK-COVERAGE"); return; }
        copy_ident_text(s->b, cur(p)->text); advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected amount var after CHECK-COVERAGE"); return; }
        copy_ident_text(s->c, cur(p)->text); advance(p);
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "SET")) {
        advance(p);
        s->kind = STMT_SET;
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected variable after SET"); return; }
        copy_ident_text(s->a, cur(p)->text); advance(p);
        expect_ident(p, "TO");
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected value after SET ... TO"); return; }
        copy_ident_text(s->b, cur(p)->text); advance(p);
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "DEBIT")) {
        advance(p);
        s->kind = STMT_TRANSFER;
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected from-account after DEBIT"); return; }
        copy_ident_text(s->a, cur(p)->text); advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected amount var after DEBIT account"); return; }
        copy_ident_text(s->c, cur(p)->text); advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected capital var after DEBIT amount"); return; }
        copy_ident_text(s->d, cur(p)->text); advance(p);
        expect_ident(p, "CREDIT");
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected to-account after CREDIT"); return; }
        copy_ident_text(s->b, cur(p)->text); advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected amount var after CREDIT account (must match DEBIT amount)"); return; }
        advance(p); /* CREDIT's amount/capital operands are validated to match DEBIT's at runtime, not re-parsed into new slots */
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected capital var after CREDIT amount"); return; }
        advance(p);
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "WRITE-TRIPLE-LEDGER")) {
        advance(p);
        s->kind = STMT_WRITE_LEDGER;
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "EMIT-ISO20022") || at_ident(p, "EMIT-SWIFT")) {
        s->kind = STMT_EMIT;
        copy_ident_text(s->a, cur(p)->text); /* which EMIT-* form */
        advance(p);
        if (cur(p)->type != TOK_IDENT) { fail(p, "expected a message subtype after EMIT-*"); return; }
        char subtype[SUTRA_IDENT_LEN];
        copy_ident_text(subtype, cur(p)->text);
        advance(p);
        /* Special case: "PACS.008" lexes as IDENT(PACS) DOT NUMBER(8) -- a
         * genuine embedded dot inside one conceptual subtype name, not a
         * statement terminator. Reconstruct it if present. */
        if (cur(p)->type == TOK_DOT && p->pos + 1 < p->count && p->toks[p->pos + 1].type == TOK_NUMBER) {
            uint32_t n = 0;
            while (subtype[n]) n++;
            if (n < SUTRA_IDENT_LEN - 1) subtype[n++] = '.';
            char numbuf[16]; int ni = 0;
            int64_t val = p->toks[p->pos + 1].num;
            if (val == 0) numbuf[ni++] = '0';
            char tmp[16]; int tn = 0;
            while (val > 0) { tmp[tn++] = (char)('0' + (val % 10)); val /= 10; }
            /* zero-pad to 3 digits, matching PACS.008-style subtype codes */
            while (tn < 3) tmp[tn++] = '0';
            while (tn > 0) numbuf[ni++] = tmp[--tn];
            numbuf[ni] = '\0';
            for (int i = 0; numbuf[i] && n < SUTRA_IDENT_LEN - 1; i++) subtype[n++] = numbuf[i];
            subtype[n] = '\0';
            advance(p); /* consume the DOT */
            advance(p); /* consume the NUMBER */
        }
        copy_ident_text(s->b, subtype);
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "NOTIFY-CHIGLET")) {
        advance(p);
        s->kind = STMT_NOTIFY;
        if (cur(p)->type != TOK_STRING) { fail(p, "expected a string after NOTIFY-CHIGLET"); return; }
        copy_ident_text(s->a, cur(p)->str);
        advance(p);
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "COMMIT")) { advance(p); s->kind = STMT_COMMIT; step->num_stmts++; return; }
    if (at_ident(p, "ROLLBACK")) { advance(p); s->kind = STMT_ROLLBACK; step->num_stmts++; return; }
    if (at_ident(p, "AUDIT-CHECK")) { advance(p); s->kind = STMT_AUDIT_CHECK; step->num_stmts++; return; }
    if (at_ident(p, "INVOKE")) {
        advance(p);
        expect_ident(p, "FS-PRA");
        expect_ident(p, "RESOLVE");
        s->kind = STMT_INVOKE_FSPRA;
        step->num_stmts++;
        return;
    }
    if (at_ident(p, "IF")) {
        advance(p);
        s->kind = STMT_IF;
        parse_condition(p, &s->cond);
        expect_ident(p, "THEN");
        step->num_stmts++; /* reserve this slot before recursing so nested indices are stable */
        uint32_t if_index = step->num_stmts - 1;
        uint32_t then_start, then_count;
        parse_stmt_list(p, step, &then_start, &then_count);
        uint32_t else_start = 0, else_count = 0;
        if (at_ident(p, "ELSE")) {
            advance(p);
            parse_stmt_list(p, step, &else_start, &else_count);
        }
        expect_ident(p, "END-IF");
        step->stmts[if_index].then_start = then_start;
        step->stmts[if_index].then_count = then_count;
        step->stmts[if_index].else_start = else_start;
        step->stmts[if_index].else_count = else_count;
        return;
    }

    fail(p, "unrecognized statement");
}

/* Parses statements until a block terminator (ELSE, END-IF, the next
 * STEP-N, END-PROGRAM, or a bare '.') without consuming the
 * terminator itself. The '.' case matters when a NESTED IF has
 * already consumed its own END-IF: control returns here with the
 * enclosing STEP's closing dot as the current token, which can never
 * legally start a new statement (every statement starts with a
 * keyword IDENT) -- so it must stop the loop, not be fed to
 * parse_one_stmt. */
static void parse_stmt_list(parser_t *p, sutra_step_t *step, uint32_t *start, uint32_t *count) {
    *start = step->num_stmts;
    /* direct_count counts LOOP ITERATIONS (one per direct child), not the
     * delta in step->num_stmts -- a nested IF reserves exactly one slot
     * for itself before recursing, so its own then/else subtree always
     * lands at HIGHER indices than any of its siblings, but that whole
     * subtree's slot count would otherwise inflate this list's count if
     * measured as a raw num_stmts delta (a real bug: the runtime would
     * then iterate past the nested IF and re-execute its branch bodies
     * unconditionally as if they were siblings). */
    uint32_t direct_count = 0;
    while (!p->failed && !at_ident(p, "ELSE") && !at_ident(p, "END-IF") && !at_step_or_end(p) &&
           cur(p)->type != TOK_EOF && cur(p)->type != TOK_DOT) {
        parse_one_stmt(p, step);
        direct_count++;
    }
    *count = direct_count;
}

/* ---- EXECUTION SECTION: "STEP-N @ORDINAL(N). <stmts>." ---- */

static void parse_step(parser_t *p, sutra_program_t *prog) {
    if (cur(p)->type != TOK_IDENT) { fail(p, "expected STEP-N label"); return; }
    advance(p); /* STEP-N label text itself is not semantically used -- @ORDINAL(N) is authoritative */

    if (prog->num_steps >= SUTRA_MAX_STEPS) { fail(p, "too many STEPs"); return; }
    sutra_step_t *step = &prog->steps[prog->num_steps];
    step->num_stmts = 0;

    if (cur(p)->type != TOK_AT) { fail(p, "expected '@ORDINAL(n)'"); return; }
    advance(p);
    expect_ident(p, "ORDINAL");
    if (cur(p)->type != TOK_LPAREN) { fail(p, "expected '(' after ORDINAL"); return; }
    advance(p);
    if (cur(p)->type != TOK_NUMBER) { fail(p, "expected ordinal number"); return; }
    step->ordinal = (uint32_t)cur(p)->num;
    advance(p);
    if (cur(p)->type != TOK_RPAREN) { fail(p, "expected ')'"); return; }
    advance(p);
    expect_dot(p);

    uint32_t s, c;
    parse_stmt_list(p, step, &s, &c);
    step->top_level_count = c; /* s is always 0: the top-level list starts this STEP's array fresh */
    expect_dot(p); /* the dot that closes the whole STEP (after the last stmt or END-IF) */

    prog->num_steps++;
}

/* ---- Top level ---- */

bool sutra_parse(const char *source, sutra_program_t *prog, uint32_t *err_line, const char **err_msg) {
    for (uint8_t *b = (uint8_t *)prog; b < (uint8_t *)prog + sizeof(*prog); b++) *b = 0;

    parser_t p;
    p.pos = 0; p.failed = false; p.err_line = 0; p.err_msg = NULL;
    p.count = sutra_lex_all(source, p.toks, SUTRA_MAX_TOKENS);
    if (p.count == (uint32_t)-1) {
        if (err_line) *err_line = 0;
        if (err_msg) *err_msg = "lex error (unterminated string, bad character, or too many tokens)";
        return false;
    }

    expect_ident(&p, "SUTRA-PROGRAM");
    expect_dot(&p);
    if (!p.failed && cur(&p)->type == TOK_IDENT) { copy_ident_text(prog->name, cur(&p)->text); advance(&p); }
    else fail(&p, "expected program name");
    expect_dot(&p);

    expect_ident(&p, "ENVIRONMENT");
    expect_ident(&p, "SECTION");
    expect_dot(&p);
    expect_ident(&p, "CONFIGURATION");
    expect_dot(&p);
    expect_ident(&p, "RAIL");
    expect_dot(&p);
    if (!p.failed && cur(&p)->type == TOK_IDENT) { prog->rail = keyword_to_rail(cur(&p)->text); advance(&p); }
    else fail(&p, "expected rail name");
    expect_dot(&p);
    expect_ident(&p, "MODE");
    expect_dot(&p);
    if (!p.failed && cur(&p)->type == TOK_IDENT) advance(&p); /* PARACONSISTENT (or future modes) -- accepted, not yet branched on */
    else fail(&p, "expected mode value");
    expect_dot(&p);
    expect_ident(&p, "AUDIT");
    expect_dot(&p);
    if (!p.failed && cur(&p)->type == TOK_IDENT) advance(&p); /* TRIPLE-LEDGER */
    else fail(&p, "expected audit mode value");
    expect_dot(&p);

    expect_ident(&p, "DATA");
    expect_ident(&p, "SECTION");
    expect_dot(&p);
    while (!p.failed && cur(&p)->type == TOK_NUMBER) {
        parse_data_item(&p, prog);
    }

    expect_ident(&p, "EXECUTION");
    expect_ident(&p, "SECTION");
    expect_dot(&p);
    while (!p.failed && cur(&p)->type == TOK_IDENT && !streq(cur(&p)->text, "END-PROGRAM")) {
        parse_step(&p, prog);
    }

    expect_ident(&p, "END-PROGRAM");
    expect_dot(&p);

    if (p.failed) {
        if (err_line) *err_line = p.err_line;
        if (err_msg) *err_msg = p.err_msg;
        return false;
    }
    return true;
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES(sutra_lex_ready) is measured: sutra_parser.o's `nm -u` is exactly
 * {sutra_lex_all}. No bring-up -- the parser is reached from the runtime, not
 * from boot.
 */
#include "zxv_decl.h"
ZXV_DECLARE(sutra_parser,
    ZXV_PROVIDES(sutra_parse_ready),
    ZXV_REQUIRES(sutra_lex_ready),
    ZXV_NO_BRINGUP);
