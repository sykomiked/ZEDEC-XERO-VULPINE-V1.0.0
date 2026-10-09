#include <stdio.h>
#include <string.h>
#include "sutra.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void) {
    printf("=== Basic tokens ===\n");
    {
        sutra_token_t toks[64];
        uint32_t n = sutra_lex_all("SUTRA-PROGRAM. TRANSFER-FUNDS.", toks, 64);
        CHECK(n == 5, "5 tokens: IDENT, DOT, IDENT, DOT, EOF");
        CHECK(toks[0].type == TOK_IDENT && strcmp(toks[0].text, "SUTRA-PROGRAM") == 0,
              "hyphenated identifier 'SUTRA-PROGRAM' lexed as one token");
        CHECK(toks[1].type == TOK_DOT, "period after SUTRA-PROGRAM is TOK_DOT");
        CHECK(toks[2].type == TOK_IDENT && strcmp(toks[2].text, "TRANSFER-FUNDS") == 0,
              "hyphenated identifier 'TRANSFER-FUNDS' lexed as one token");
        CHECK(toks[4].type == TOK_EOF, "stream ends with TOK_EOF");
    }

    printf("\n=== Rational and integer literals ===\n");
    {
        sutra_token_t toks[16];
        uint32_t n = sutra_lex_all("500/1 42 7/3", toks, 16);
        CHECK(n == 4, "3 literals + EOF");
        CHECK(toks[0].type == TOK_RATIONAL && toks[0].num == 500 && toks[0].den == 1, "500/1 lexed as rational 500/1");
        CHECK(toks[1].type == TOK_NUMBER && toks[1].num == 42 && toks[1].den == 1, "42 lexed as integer (den=1)");
        CHECK(toks[2].type == TOK_RATIONAL && toks[2].num == 7 && toks[2].den == 3, "7/3 lexed as rational 7/3");
    }

    printf("\n=== Strings ===\n");
    {
        sutra_token_t toks[16];
        uint32_t n = sutra_lex_all("\"alice@vino\" 'single quoted'", toks, 16);
        CHECK(n == 3, "2 strings + EOF");
        CHECK(toks[0].type == TOK_STRING && strcmp(toks[0].str, "alice@vino") == 0, "double-quoted string content correct");
        CHECK(toks[1].type == TOK_STRING && strcmp(toks[1].str, "single quoted") == 0, "single-quoted string content correct");
    }

    printf("\n=== Comments (COBOL-style *>) ===\n");
    {
        sutra_token_t toks[16];
        uint32_t n = sutra_lex_all("STEP-1 *> this is a comment\n@ORDINAL(1).", toks, 16);
        /* STEP-1, @, ORDINAL, (, 1, ), ., EOF = 8 tokens */
        CHECK(n == 8, "comment is fully skipped, not tokenized");
        CHECK(toks[0].type == TOK_IDENT && strcmp(toks[0].text, "STEP-1") == 0, "STEP-1 before comment lexed correctly");
        CHECK(toks[1].type == TOK_AT, "@ after comment/newline lexed correctly");
        CHECK(toks[2].type == TOK_IDENT && strcmp(toks[2].text, "ORDINAL") == 0, "ORDINAL after @ lexed correctly");
        CHECK(toks[3].type == TOK_LPAREN, "( lexed correctly");
        CHECK(toks[4].type == TOK_NUMBER && toks[4].num == 1, "1 inside parens lexed correctly");
        CHECK(toks[5].type == TOK_RPAREN, ") lexed correctly");
        CHECK(toks[6].type == TOK_DOT, ". after ORDINAL(1) lexed correctly");
    }

    printf("\n=== Full DATA SECTION line ===\n");
    {
        const char *src = "01 TRANSFER-AMOUNT   SUTRA-AMOUNT VALUE 500/1.";
        sutra_token_t toks[32];
        uint32_t n = sutra_lex_all(src, toks, 32);
        CHECK(n == 7, "NUMBER(01) IDENT(TRANSFER-AMOUNT) IDENT(SUTRA-AMOUNT) IDENT(VALUE) RATIONAL(500/1) DOT EOF = 7 tokens");
        CHECK(toks[0].type == TOK_NUMBER && toks[0].num == 1, "leading '01' level number lexed as integer 1");
        CHECK(toks[1].type == TOK_IDENT && strcmp(toks[1].text, "TRANSFER-AMOUNT") == 0, "TRANSFER-AMOUNT lexed correctly");
        CHECK(toks[4].type == TOK_RATIONAL && toks[4].num == 500 && toks[4].den == 1, "trailing 500/1 lexed as rational");
    }

    printf("\n=== Error cases ===\n");
    {
        sutra_token_t toks[16];
        uint32_t n1 = sutra_lex_all("\"unterminated string", toks, 16);
        CHECK(n1 == (uint32_t)-1, "unterminated string is a genuine lex error");

        uint32_t n2 = sutra_lex_all("VALID # INVALID", toks, 16);
        CHECK(n2 == (uint32_t)-1, "unrecognized character '#' is a genuine lex error");
    }

    if (failures == 0) printf("\n=== ALL SUTRA LEXER TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
