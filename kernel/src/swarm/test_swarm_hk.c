/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_swarm_hk.c — known-answer tests for the Hackronomicon shorthand that
 * agents use with each other. Expected readings are worked by hand from the
 * precedence in swarm_hk.h (K1). */
#include <stdio.h>
#include <string.h>
#include "swarm_hk.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s\n", msg);                                                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void reads_as(const char *in, const char *want)
{
    swarm_hk_ast_t a;
    char buf[512];
    if (swarm_hk_parse(in, &a) != SWARM_HK_OK) {
        printf("  FAIL: parse \"%s\"\n", in);
        failures++;
        return;
    }
    if (swarm_hk_canonical(&a, buf, sizeof buf) < 0 || strcmp(buf, want) != 0) {
        printf("  FAIL: \"%s\"\n    got  %s\n    want %s\n", in, buf, want);
        failures++;
    }
}

static void test_precedence(void)
{
    /* The book's worked parse (Ch 2). */
    reads_as("create((schematics for kit) > ((items + components) / costs))^(27/33)",
             "(create((schematics for kit > ((items + components) / costs))))^(27/33)");
    /* The seven-step example: the dial binds tighter than "-", "->" loosest. */
    reads_as("write(email, tone: warm && direct) -> ask(1) − jargon ^(27/33)",
             "(write(email, tone: (warm && direct)) -> (ask(1) - (jargon)^(27/33)))");
    reads_as("a + b / c", "(a + (b / c))");
    reads_as("a -> b -> c", "((a -> b) -> c)");
    reads_as("a > b + c", "(a > (b + c))");
    reads_as("a && b > c", "((a && b) > c)");
    reads_as("a → b", "(a -> b)");
    reads_as("formality^(-1)", "(formality)^(-1)");
    reads_as("jargon^(0)", "(jargon)^(0)");
    reads_as("fan_out(question, models: 3) -> compare(answers) -> verify(queue)^(3/2)",
             "((fan_out(question, models: 3) -> compare(answers)) -> (verify(queue))^(3/2))");
    reads_as("gate(web page - ads)", "gate((web page - ads))");
    reads_as("plan(decision-by friday)", "plan(decision-by friday)");
    reads_as("route()", "route()");
}

static void test_errors(void)
{
    swarm_hk_ast_t a;
    CHECK(swarm_hk_parse("verb(a, b", &a) == SWARM_HK_ERR_SYNTAX, "unclosed call");
    CHECK(swarm_hk_parse("a +", &a) == SWARM_HK_ERR_SYNTAX, "dangling operator");
    CHECK(swarm_hk_parse("a^(1/0)", &a) == SWARM_HK_ERR_SYNTAX, "zero denominator");
    CHECK(swarm_hk_parse("(a + b): c", &a) == SWARM_HK_ERR_SYNTAX, "key outside a call");
    CHECK(swarm_hk_parse("x(a + b: c)", &a) == SWARM_HK_ERR_SYNTAX, "key must be plain words");
    char big[300];
    for (int i = 0; i < 299; i++) big[i] = 'a';
    big[299] = 0;
    CHECK(swarm_hk_parse(big, &a) == SWARM_HK_ERR_TOO_LONG, "too long");
}

static void test_requests(void)
{
    swarm_hk_msg_t m;
    CHECK(swarm_hk_request(&m, 2, 3, 4, SWARM_HK_TRUE,
                           "architect(task) -> implement(spec) -> audit(output)^(3/2)") ==
              SWARM_HK_OK,
          "a three-step request");
    CHECK(m.from == 2 && m.to == 3 && m.ordinal == 4 && m.ast.num_ops == 3,
          "envelope and op count");
    CHECK(swarm_hk_request(&m, 2, 2, 1, SWARM_HK_TRUE, "ask(1)") == SWARM_HK_ERR_SELF,
          "K4 no self-requests");
    CHECK(swarm_hk_request(&m, 2, 3, 1, SWARM_HK_TRUE, "a + b + c + d + e + f + g") ==
              SWARM_HK_ERR_STACKED,
          "K3 six operators is stacking");
    CHECK(swarm_hk_request(&m, 2, 3, 1, SWARM_HK_TRUE, "a + b + c + d + e + f") == SWARM_HK_OK,
          "K3 five operators is allowed");
    CHECK(swarm_hk_words("one two  three\nfour") == 4, "word count");
    char strand[400];
    int k = 0;
    for (int w = 0; w < 90; w++) {
        strand[k++] = 'w';
        strand[k++] = ' ';
    }
    strand[k] = 0;
    CHECK(!swarm_hk_strand_ok(strand), "K4 90 words is too long");
    strand[2 * 89] = 0;
    CHECK(swarm_hk_strand_ok(strand), "K4 89 words fits");
}

static void test_truth(void)
{
    CHECK(swarm_hk_and(SWARM_HK_TRUE, SWARM_HK_FALSE) == SWARM_HK_GLUT, "⊤∧⊥ = ⊥̸");
    CHECK(swarm_hk_and(SWARM_HK_TRUE, SWARM_HK_UNKNOWN) == SWARM_HK_UNKNOWN, "⊤∧U = U");
    CHECK(swarm_hk_or(SWARM_HK_TRUE, SWARM_HK_UNKNOWN) == SWARM_HK_TRUE, "⊤∨U = ⊤");
    CHECK(swarm_hk_and(SWARM_HK_GLUT, SWARM_HK_TRUE) == SWARM_HK_GLUT, "⊥̸∧x = ⊥̸");
    CHECK(swarm_hk_and(SWARM_HK_PARADOX, SWARM_HK_GLUT) == SWARM_HK_PARADOX, "P dominates ⊥̸");
    CHECK(swarm_hk_or(SWARM_HK_NEUTRAL, SWARM_HK_FALSE) == SWARM_HK_FALSE, "N∨x = x");
    CHECK(swarm_hk_and(SWARM_HK_TRUE, SWARM_HK_TRUE) == SWARM_HK_TRUE, "⊤∧⊤ = ⊤");
    CHECK(swarm_hk_or(SWARM_HK_FALSE, SWARM_HK_FALSE) == SWARM_HK_FALSE, "⊥∨⊥ = ⊥");
}

int main(void)
{
    printf("=== test_swarm_hk ===\n");
    test_precedence();
    test_errors();
    test_requests();
    test_truth();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
