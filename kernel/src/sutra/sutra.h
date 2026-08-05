/* sutra.h — SUTRA: Nonlinear COBOL for the M5 Triple-Ledger Bank
 *
 * Implements the concrete grammar shown in the flagship TRANSFER-FUNDS
 * example of zxv_docs/SUTRA_LANGUAGE_DIRECTIVE.md: SUTRA-PROGRAM /
 * ENVIRONMENT SECTION / DATA SECTION / EXECUTION SECTION, with
 * ordinal-tagged STEP-N @ORDINAL(N) statements dispatched by ordinal
 * order rather than line order.
 *
 * Design decision (deliberate, not an oversight): SUTRA does NOT
 * reimplement the ledger, the 9 capitals, or the payment-rail message
 * formats. kernel/src/vino/vino.h ALREADY provides a real, tested
 * triple-ledger (primary=history+audit via hash chain, balances=
 * current state), capital_type_t (9 forms), payment_rail_t (20
 * rails), and message generators (vino_msg_to_iso20022, _to_mt103,
 * _to_pacs008, _to_cips, _to_spfs, ...). SUTRA is a language FRONT
 * END whose runtime interprets the AST directly against a real
 * vino_ledger_t. SUTRA-CAPITAL IS capital_type_t; SUTRA-RAIL IS
 * payment_rail_t; SUTRA-AMOUNT IS rational_t; SUTRA-STATUS IS trit_t
 * (all from m5_types.h / vino.h, not reinvented).
 *
 * DEBIT ... / CREDIT ... immediately paired on the same account pair
 * and amount compile to a single vino_transfer() call (atomic, with
 * LPRES presence attestation) -- there is no safe way to split a
 * transfer into two independent non-atomic halves without either
 * duplicating vino's atomicity logic or weakening it, so the
 * DEBIT+CREDIT pair is treated as sugar for one real transfer.
 *
 * Scope (v1, honest limits): supports the single-program, flat
 * DATA + ordinal-STEP shape of the flagship example (declarations,
 * STEP-N @ORDINAL(N) blocks, IF/THEN/ELSE/END-IF, SET, CHECK-COVERAGE,
 * DEBIT/CREDIT, WRITE-TRIPLE-LEDGER, EMIT-ISO20022/EMIT-SWIFT,
 * NOTIFY-CHIGLET, COMMIT/ROLLBACK, INVOKE FS-PRA RESOLVE). Nested
 * PROCEDURE definitions, RECORD/WITH{} construction, and the full
 * Chiglet NLP pipeline are intentionally out of scope for v1 and are
 * NOT silently faked.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef SUTRA_H
#define SUTRA_H

#include "m5_types.h"
#include "vino.h"

#define SUTRA_MAX_TOKENS     4096
#define SUTRA_MAX_STEPS      64
#define SUTRA_MAX_STMTS_PER_STEP 32
#define SUTRA_MAX_VARS       64
#define SUTRA_IDENT_LEN      48
#define SUTRA_STRING_LEN     128
#define SUTRA_SOURCE_MAX     16384

/* SUTRA type aliases onto the real M5 / Vino types (see file header). */
typedef rational_t     sutra_amount_t;   /* SUTRA-AMOUNT */
typedef trit_t         sutra_status_t;   /* SUTRA-STATUS */
typedef capital_type_t sutra_capital_t;  /* SUTRA-CAPITAL */
typedef payment_rail_t sutra_rail_t;     /* SUTRA-RAIL */
typedef ordinal_t      sutra_ordinal_t;  /* SUTRA-ORDINAL */

/* ---- Lexer ---- */

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,          /* identifiers AND keywords (keyword-ness is
                         * decided by the parser via string compare,
                         * matching COBOL's traditionally huge and
                         * context-sensitive reserved-word set) */
    TOK_NUMBER,         /* integer, e.g. 4 */
    TOK_RATIONAL,       /* n/d, e.g. 500/1 */
    TOK_STRING,         /* "..." */
    TOK_DOT,            /* . (statement/clause terminator) */
    TOK_AT,             /* @ */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_EQUALS,
} sutra_token_type_t;

typedef struct sutra_token {
    sutra_token_type_t type;
    char text[SUTRA_IDENT_LEN];   /* raw text for IDENT/keyword */
    int64_t num, den;             /* for NUMBER (den=1) / RATIONAL */
    char str[SUTRA_STRING_LEN];   /* for STRING (unescaped contents) */
    uint32_t line;
} sutra_token_t;

typedef struct sutra_lexer {
    const char *src;
    uint32_t pos, len, line;
} sutra_lexer_t;

void sutra_lexer_init(sutra_lexer_t *lx, const char *source);
/* Fills *out, returns false only on a genuine lex error (advances
 * past comments/whitespace transparently; TOK_EOF is a normal,
 * non-error terminal token, not a failure). */
bool sutra_lex_next(sutra_lexer_t *lx, sutra_token_t *out);
/* Tokenizes the whole source into tokens[], returns token count, or
 * (uint32_t)-1 if it would overflow SUTRA_MAX_TOKENS or hits a lex error. */
uint32_t sutra_lex_all(const char *source, sutra_token_t *tokens, uint32_t max_tokens);

/* ---- AST ---- */

typedef enum {
    STMT_SET,             /* SET <var> TO <expr>  (also covers "SET TXN-STATUS TO TRUE") */
    STMT_IF,              /* IF <cond> THEN <stmts> [ELSE <stmts>] END-IF */
    STMT_CHECK_COVERAGE,  /* CHECK-COVERAGE <acct> <capital> <amount-var> */
    STMT_TRANSFER,        /* fused DEBIT+CREDIT pair -> one vino_transfer */
    STMT_WRITE_LEDGER,    /* WRITE-TRIPLE-LEDGER (no-op marker: vino_transfer already wrote it) */
    STMT_EMIT,            /* EMIT-ISO20022 / EMIT-SWIFT <msg-subtype ident> */
    STMT_NOTIFY,          /* NOTIFY-CHIGLET "<string>" */
    STMT_COMMIT,
    STMT_ROLLBACK,
    STMT_INVOKE_FSPRA,    /* INVOKE FS-PRA RESOLVE */
    STMT_AUDIT_CHECK,     /* AUDIT-CHECK (sets a synthetic AUDIT-PASS condition var) */
} sutra_stmt_kind_t;

typedef enum { COND_STATUS_EQ, COND_COVERAGE_PASS, COND_AUDIT_PASS } sutra_cond_kind_t;

typedef struct sutra_cond {
    sutra_cond_kind_t kind;
    char var[SUTRA_IDENT_LEN];   /* for COND_STATUS_EQ */
    trit_t value;                /* for COND_STATUS_EQ */
    bool negate;                 /* IF NOT ... (reserved for future use) */
} sutra_cond_t;

typedef struct sutra_stmt {
    sutra_stmt_kind_t kind;
    char a[SUTRA_IDENT_LEN];   /* generic operand slots -- meaning depends on kind: */
    char b[SUTRA_IDENT_LEN];   /* SET: a=var, b=value-ident/keyword */
    char c[SUTRA_IDENT_LEN];   /* TRANSFER: a=from-var, b=to-var, c=amount-var, d=capital-var */
    char d[SUTRA_IDENT_LEN];
    sutra_cond_t cond;         /* IF */
    uint32_t then_start, then_count; /* index range into the step's stmt array */
    uint32_t else_start, else_count;
} sutra_stmt_t;

typedef struct sutra_step {
    uint32_t ordinal;                          /* @ORDINAL(N) */
    sutra_stmt_t stmts[SUTRA_MAX_STMTS_PER_STEP];
    uint32_t num_stmts;       /* total flat array slots used (includes nested subtrees) */
    uint32_t top_level_count; /* direct statement count at this STEP's own top level (always starts at stmts[0]) */
} sutra_step_t;

typedef enum { VAR_STRING, VAR_AMOUNT, VAR_STATUS, VAR_CAPITAL } sutra_var_kind_t;

typedef struct sutra_var {
    char name[SUTRA_IDENT_LEN];
    sutra_var_kind_t kind;
    char str_val[SUTRA_STRING_LEN];  /* VAR_STRING (e.g. account addresses) */
    rational_t amount_val;           /* VAR_AMOUNT */
    trit_t status_val;               /* VAR_STATUS */
    capital_type_t capital_val;      /* VAR_CAPITAL */
} sutra_var_t;

typedef struct sutra_program {
    char name[SUTRA_IDENT_LEN];
    sutra_rail_t rail;
    sutra_var_t vars[SUTRA_MAX_VARS];
    uint32_t num_vars;
    sutra_step_t steps[SUTRA_MAX_STEPS];
    uint32_t num_steps;
} sutra_program_t;

/* Parses a full SUTRA-PROGRAM from source into *prog. Returns true on
 * success. On failure, err_line/err_msg (if non-NULL) describe where
 * and why -- there is no silent partial-parse. */
bool sutra_parse(const char *source, sutra_program_t *prog,
                  uint32_t *err_line, const char **err_msg);

/* ---- Runtime ---- */

typedef struct sutra_runtime {
    vino_ledger_t *ledger;   /* the REAL vino ledger this program executes against */
    char last_emit[600];     /* most recent EMIT-* generated message, for inspection/tests */
    uint32_t last_emit_len;
    uint32_t transfers_executed;
    uint32_t steps_executed;
    /* EMIT-ISO20022/EMIT-SWIFT statements carry no operands of their own
     * in the flagship grammar -- they refer to "the transaction that was
     * just settled", so the runtime caches the most recent TRANSFER's
     * details here for EMIT to build a message from. */
    char last_from[VINO_ADDR_LEN];
    char last_to[VINO_ADDR_LEN];
    uint64_t last_amount;
    capital_type_t last_capital;
    bool has_last_transfer;
    char notify_log[4][SUTRA_STRING_LEN]; /* NOTIFY-CHIGLET messages, most recent 4 */
    uint32_t notify_count;
} sutra_runtime_t;

void sutra_runtime_init(sutra_runtime_t *rt, vino_ledger_t *ledger);
/* Executes every step of prog in ascending ordinal order (the
 * "nonlinear dispatch": step ARRAY order need not match ordinal
 * order -- this sorts/dispatches by ordinal, per the language's own
 * "steps ordinal-tagged, not line-sequenced" design principle).
 * Returns true iff every step executed without a fatal runtime error
 * (a GLUT/rejected status is NOT a fatal error -- FS-PRA paraconsistent
 * handling means the program continues; see design principle #2). */
bool sutra_run(sutra_runtime_t *rt, sutra_program_t *prog);

/* ---- Capital (sutra_capital.c) ---- */

const char *sutra_capital_name(sutra_capital_t c);
bool sutra_check_coverage(vino_ledger_t *ledger, const char *account_addr,
                           sutra_capital_t capital, rational_t required_amount);

/* ---- Rails (sutra_rails.c) ---- */

int32_t sutra_emit_message(const vino_transaction_t *txn, const char *emit_form,
                            const char *subtype, char *out, uint32_t max_out);

/* ---- Chiglet (sutra_chiglet.c) ---- */

typedef struct chiglet_result {
    bool matched;
    rational_t amount;
    char from_account[64];
    char to_account[64];
    payment_rail_t rail;
    capital_type_t capital;
} chiglet_result_t;

chiglet_result_t sutra_chiglet_translate(const char *sentence);

/* ---- Self-audit (R4 pattern) ---- */

typedef enum { SUTRA_AUDIT_PASS = 0, SUTRA_AUDIT_WARN = 1, SUTRA_AUDIT_FAIL = 2 } sutra_audit_result_t;
sutra_audit_result_t sutra_self_audit(const sutra_program_t *prog);

#endif /* SUTRA_H */
