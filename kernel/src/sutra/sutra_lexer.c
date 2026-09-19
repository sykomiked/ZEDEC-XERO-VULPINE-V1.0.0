/* sutra_lexer.c — SUTRA tokenizer. See sutra.h.
 *
 * COBOL-style hyphenated identifiers (END-IF, TXN-STATUS, GLUT-NEUTRAL,
 * WRITE-TRIPLE-LEDGER) mean '-' is an IDENTIFIER character here, not a
 * minus operator -- the flagship grammar has no subtraction operator
 * at all, so this is unambiguous, not a compromise.
 */
#include "sutra.h"

static bool is_ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool is_ident_cont(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

void sutra_lexer_init(sutra_lexer_t *lx, const char *source) {
    lx->src = source;
    lx->pos = 0;
    lx->len = 0;
    while (source[lx->len]) lx->len++;
    lx->line = 1;
}

static char peek(sutra_lexer_t *lx, uint32_t ahead) {
    uint32_t p = lx->pos + ahead;
    return p < lx->len ? lx->src[p] : '\0';
}

static void skip_ws_and_comments(sutra_lexer_t *lx) {
    for (;;) {
        char c = peek(lx, 0);
        if (c == '\n') { lx->line++; lx->pos++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { lx->pos++; continue; }
        if (c == '*' && peek(lx, 1) == '>') { /* COBOL-style *> comment to end of line */
            while (peek(lx, 0) != '\0' && peek(lx, 0) != '\n') lx->pos++;
            continue;
        }
        break;
    }
}

bool sutra_lex_next(sutra_lexer_t *lx, sutra_token_t *out) {
    skip_ws_and_comments(lx);
    out->line = lx->line;
    out->text[0] = '\0';
    out->str[0] = '\0';
    out->num = 0;
    out->den = 1;

    char c = peek(lx, 0);
    if (c == '\0') { out->type = TOK_EOF; return true; }

    if (c == '.') { lx->pos++; out->type = TOK_DOT; return true; }
    if (c == '@') { lx->pos++; out->type = TOK_AT; return true; }
    if (c == '(') { lx->pos++; out->type = TOK_LPAREN; return true; }
    if (c == ')') { lx->pos++; out->type = TOK_RPAREN; return true; }
    if (c == '=') { lx->pos++; out->type = TOK_EQUALS; return true; }

    if (c == '"' || c == '\'') {
        char quote = c;
        lx->pos++;
        uint32_t n = 0;
        while (peek(lx, 0) != quote) {
            char ch = peek(lx, 0);
            if (ch == '\0' || ch == '\n') return false; /* unterminated string: genuine lex error */
            if (n < SUTRA_STRING_LEN - 1) out->str[n++] = ch;
            lx->pos++;
        }
        lx->pos++; /* closing quote */
        out->str[n] = '\0';
        out->type = TOK_STRING;
        return true;
    }

    if (is_digit(c)) {
        int64_t whole = 0;
        while (is_digit(peek(lx, 0))) { whole = whole * 10 + (peek(lx, 0) - '0'); lx->pos++; }
        if (peek(lx, 0) == '/' && is_digit(peek(lx, 1))) {
            lx->pos++; /* consume '/' */
            int64_t denom = 0;
            while (is_digit(peek(lx, 0))) { denom = denom * 10 + (peek(lx, 0) - '0'); lx->pos++; }
            out->type = TOK_RATIONAL;
            out->num = whole;
            out->den = denom;
        } else {
            out->type = TOK_NUMBER;
            out->num = whole;
            out->den = 1;
        }
        return true;
    }

    if (is_ident_start(c)) {
        uint32_t n = 0;
        while (is_ident_cont(peek(lx, 0))) {
            if (n < SUTRA_IDENT_LEN - 1) out->text[n++] = peek(lx, 0);
            lx->pos++;
        }
        out->text[n] = '\0';
        out->type = TOK_IDENT;
        return true;
    }

    return false; /* unrecognized character: genuine lex error */
}

uint32_t sutra_lex_all(const char *source, sutra_token_t *tokens, uint32_t max_tokens) {
    sutra_lexer_t lx;
    sutra_lexer_init(&lx, source);
    uint32_t count = 0;
    for (;;) {
        if (count >= max_tokens) return (uint32_t)-1;
        sutra_token_t tok;
        if (!sutra_lex_next(&lx, &tok)) return (uint32_t)-1;
        tokens[count++] = tok;
        if (tok.type == TOK_EOF) break;
    }
    return count;
}

/* ---- DECLARATION -----------------------------------------------------------

 * The root of the sutra chain, and the only sutra file with a bring-up.
 *
 * sutra_lexer.o's `nm -u` is empty, so this is the floor. sutra_parser.o
 * requires it (U sutra_lex_all), and the runtime requires the parser. Rooting
 * here and nowhere else is the transitive rule applied in the direction the
 * calls actually run.
 */
#include "zxv_decl.h"
static int zxvd_sutra_lexer_bringup(void) {
    static sutra_token_t toks[16];
    uint32_t n = sutra_lex_all("give 1 to bob", toks, 16u);
    return (n > 0u) ? 0 : -1;
}

ZXV_DECLARE(sutra_lexer,
    ZXV_PROVIDES(sutra_lex_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_sutra_lexer_bringup));
