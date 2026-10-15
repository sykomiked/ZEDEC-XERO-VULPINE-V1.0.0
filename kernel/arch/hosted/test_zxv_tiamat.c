/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_tiamat.c — zxv_tiamat.h H1-H5 with scripted cells (no model).
 *   sh kernel/arch/hosted/test_hosted.sh runs it; argv[1] is a scratch dir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_tiamat.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) failures++;                                                                      \
        printf((c) ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static const char *CONF = "# two lineages, three cells\n"
                          "slot 0 \"/models/a b/qwen.gguf\"\n"
                          "slot 1 /models/smol.gguf\r\n"
                          "cell Spinewyrm 0 qwen - what why\n"
                          "cell Codelindworm 0 qwen /ad/code.gguf Code PYTHON function\n"
                          "cell Vouchwyvern 1 smollm - check   # a comment\n"
                          "log /tmp/t.log\n";

static int parse_err(const char *text, const char *want)
{
    zxv_tiamat_conf_t c;
    char err[200];
    return zxv_tiamat_parse(text, &c, err, sizeof err) == -1 && strstr(err, want) != NULL;
}

/* scripted cells: the reply each cell gives, and its result code */
static const char *REPLY[3];
static int RC[3];
static char LAST_PROMPT[3][4096];
static int CALLS[3];

static int ask(void *ctx, uint32_t cell, const char *prompt, char *out, size_t cap)
{
    (void) ctx;
    CALLS[cell]++;
    snprintf(LAST_PROMPT[cell], sizeof LAST_PROMPT[cell], "%s", prompt);
    snprintf(out, cap, "%s", REPLY[cell]);
    return RC[cell];
}

static void script(const char *r0, int rc0, const char *r2, int rc2)
{
    REPLY[0] = r0, RC[0] = rc0, REPLY[1] = "", RC[1] = 0, REPLY[2] = r2, RC[2] = rc2;
    memset(CALLS, 0, sizeof CALLS);
}

int main(int argc, char **argv)
{
    zxv_tiamat_conf_t c;
    char err[200];
    printf("H1 config\n");
    CHECK(zxv_tiamat_parse(CONF, &c, err, sizeof err) == 0, "a good config parses (%s)", err);
    CHECK(c.n_cells == 3 && !strcmp(c.slot_path[0], "/models/a b/qwen.gguf") &&
              !strcmp(c.slot_path[1], "/models/smol.gguf"),
          "quoted paths keep their spaces; CRLF is accepted");
    CHECK(!c.cell[0].adapter[0] && !strcmp(c.cell[1].adapter, "/ad/code.gguf") &&
              c.cell[2].slot == 1 && !strcmp(c.cell[2].lineage, "smollm"),
          "cells keep slot, lineage and adapter ('-' is none)");
    CHECK(c.cell[1].n_kw == 3 && !strcmp(c.cell[1].kw[1], "python") && c.cell[2].n_kw == 1,
          "keywords are lower-cased; a comment ends the line");
    CHECK(!strcmp(c.log_path, "/tmp/t.log"), "log path");
    CHECK(parse_err("slot 4 /x\ncell A 0 q -\n", "line 1"), "slot 4 is refused");
    CHECK(parse_err("slot 0 /x\ncell A 1 q -\n", "slot 1, which has no model"),
          "a cell on an empty slot is refused");
    CHECK(parse_err("slot 0 /x\n", "no cells"), "no cells is refused");
    CHECK(parse_err("slot 0 /x\ncell A 0 q -\ncell A 0 r -\n", "twice"), "a name twice is refused");
    CHECK(parse_err("model /x\n", "unknown directive"), "an unknown directive is refused");
    CHECK(parse_err("slot 0 \"/x\n", "quote"), "an open quote is refused");
    CHECK(parse_err("slot 0 /x\ncell A 0 q - abcdefghijklmnopqrstuvwxyz\n", "23 bytes"),
          "a keyword longer than 23 bytes is refused");
    CHECK(parse_err("slot 0 /x extra\ncell A 0 q -\n", "extra words"), "extra words are refused");

    printf("H2 route\n");
    int w = 9;
    CHECK(zxv_tiamat_route(&c, "Write a python function", &w) == 1 && w == 2,
          "keywords pick the lead; the witness is of another lineage");
    CHECK(zxv_tiamat_route(&c, "hello", &w) == 0 && w == 2, "no keyword: the first cell leads");
    CHECK(zxv_tiamat_route(&c, "please check this", &w) == 2 && w == 0,
          "a smollm lead gets a qwen witness");
    zxv_tiamat_conf_t one;
    zxv_tiamat_parse("slot 0 /x\ncell A 0 q -\ncell B 0 q -\n", &one, err, sizeof err);
    CHECK(zxv_tiamat_route(&one, "anything", &w) == 0 && w == -1,
          "one lineage: no witness (same-trunk cells are one witness)");

    printf("H3 witness verdicts\n");
    CHECK(zxv_tiamat_verdict("true") == ZXV_TV_TRUE, "true");
    CHECK(zxv_tiamat_verdict("False. It is 391.") == ZXV_TV_FALSE, "False.");
    CHECK(zxv_tiamat_verdict("It is BOTH, and true in part") == ZXV_TV_BOTH, "earliest word wins");
    CHECK(zxv_tiamat_verdict("untrue") == ZXV_TV_NEITHER, "whole words only");
    CHECK(zxv_tiamat_verdict("") == ZXV_TV_NEITHER, "no word: NEITHER");

    printf("H3 reckoner\n");
    char e[96];
    CHECK(zxv_tiamat_reckon("What is 17 times 23?", "17 x 23 = 391.", e, sizeof e) == ZXV_TV_TRUE &&
              !strcmp(e, "17 x 23 = 391"),
          "17 times 23: 391 is TRUE (%s)", e);
    CHECK(zxv_tiamat_reckon("What is 17 times 23?", "It is 371.", NULL, 0) == ZXV_TV_FALSE,
          "371 is FALSE");
    CHECK(zxv_tiamat_reckon("What is 17 times 23?", "391 (17 times 23)", NULL, 0) == ZXV_TV_TRUE,
          "the sum restated after the result is skipped");
    CHECK(zxv_tiamat_reckon("17 x 23", "391.0", NULL, 0) == ZXV_TV_TRUE, "391.0 is whole");
    CHECK(zxv_tiamat_reckon("17*23", "390.5", NULL, 0) == ZXV_TV_FALSE, "390.5 is FALSE");
    CHECK(zxv_tiamat_reckon("What is 5 times 1?", "5 times 1 is 5", NULL, 0) == ZXV_TV_TRUE,
          "a result equal to an operand");
    CHECK(zxv_tiamat_reckon("1,234 plus 1", "1,235", NULL, 0) == ZXV_TV_TRUE, "thousands commas");
    CHECK(zxv_tiamat_reckon("What is -5 minus 3?", "-8", NULL, 0) == ZXV_TV_TRUE, "negatives");
    CHECK(zxv_tiamat_reckon("84 divided by 4", "21", NULL, 0) == ZXV_TV_TRUE, "exact division");
    CHECK(zxv_tiamat_reckon("7 divided by 2", "3.5", NULL, 0) == -1,
          "a division with a remainder is not checked");
    CHECK(zxv_tiamat_reckon("8 divided by 0", "none", NULL, 0) == -1, "division by zero");
    CHECK(zxv_tiamat_reckon("999999999999 times 999999999999", "1", NULL, 0) == -1,
          "overflow is refused");
    CHECK(zxv_tiamat_reckon("What is 17 times 23?", "I don't know.", NULL, 0) == -1,
          "an answer with no number is not checked");
    CHECK(zxv_tiamat_reckon("Who wrote Hamlet?", "Shakespeare, 1600", NULL, 0) == -1,
          "no sum in the question");
    CHECK(zxv_tiamat_reckon("box2 times", "3", NULL, 0) == -1, "digits inside a word");

    printf("H4 join\n");
    int ok = 1;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++)
            for (int d = 0; d < 4; d++) {
                int x = zxv_tiamat_join(zxv_tiamat_join(a, b), d);
                if (x != zxv_tiamat_join(a, zxv_tiamat_join(b, d)) ||
                    zxv_tiamat_join(a, b) != zxv_tiamat_join(b, a))
                    ok = 0;
                if ((a == ZXV_TV_BOTH || b == ZXV_TV_BOTH || d == ZXV_TV_BOTH) && x != ZXV_TV_BOTH)
                    ok = 0;
            }
    CHECK(ok, "order and grouping never change the verdict; one BOTH stays BOTH");
    CHECK(zxv_tiamat_join(ZXV_TV_TRUE, ZXV_TV_FALSE) == ZXV_TV_BOTH, "TRUE with FALSE is BOTH");
    CHECK(zxv_tiamat_join(ZXV_TV_NEITHER, ZXV_TV_TRUE) == ZXV_TV_TRUE, "NEITHER is the identity");

    printf("H4 answers\n");
    static zxv_tiamat_trace_t t;
    char out[4096];
    int shown;
    script("Canberra is the capital.", 1, "true", 1);
    int tv = zxv_tiamat_answer(&c, ask, NULL, "what is the capital of Australia", &t, out,
                               sizeof out, &shown);
    CHECK(tv == ZXV_TV_TRUE && shown && strstr(out, "Canberra is the capital.") &&
              strstr(out, "checked by Vouchwyvern (TRUE)"),
          "witnessed TRUE: shown as checked");
    CHECK(strstr(LAST_PROMPT[2], "\"The answer to the question 'what is the capital of "
                                 "Australia' is: Canberra is the capital.\"") != NULL,
          "the witness is shown question and answer as one statement");

    script("17 times 23 is 371.", 1, "true", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "What is 17 times 23?", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_FALSE && !shown && strstr(out, "Held") && !strstr(out, "371.") &&
              strstr(out, "17 x 23 = 391") && CALLS[2] == 0 && t.witness_skipped,
          "reckoner FALSE: held, the draft not shown, no model asked against the proof");

    script("17 times 23 is 391.", 1, "false", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "What is 17 times 23?", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_TRUE && shown && strstr(out, "exact arithmetic") &&
              !strstr(out, "Vouchwyvern") && CALLS[2] == 0,
          "reckoner TRUE: shown with the exact check");

    script("Canberra and Sydney.", 1, "Both, in part", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "capital?", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_BOTH && !shown && strstr(out, "both for and against it (BOTH)") &&
              strstr(out, "Vouchwyvern said BOTH"),
          "a BOTH witness: held with its reason");

    script("Sydney.", 1, "false", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "capital?", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_FALSE && !shown && strstr(out, "found it wrong (FALSE)"),
          "a FALSE witness: held");

    script("Probably rain.", 1, "maybe", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "will it rain", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_NEITHER && shown && strstr(out, "Not checked"),
          "nothing decides: shown, marked not checked");

    script("Probably rain.", 1, "its budget is spent", 0);
    tv = zxv_tiamat_answer(&c, ask, NULL, "will it rain", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_NEITHER && shown && strstr(out, "could not answer (its budget is spent)"),
          "a witness that cannot answer: not checked, with its reason");

    script("x", ZXV_TIAMAT_INTEGRITY, "true", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "hello", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_BOTH && !shown && strstr(out, "integrity check") && CALLS[2] == 0,
          "a lead failing the integrity check is held; no witness is asked");

    script("no model", 0, "true", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "hello", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_NEITHER && !shown && strstr(out, "could not answer: no model"),
          "a lead that cannot answer says why");

    script("Fine.", 1, "true", 1);
    tv = zxv_tiamat_answer(&one, ask, NULL, "hello", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_NEITHER && shown && strstr(out, "no cell of another lineage") &&
              CALLS[1] == 0,
          "one lineage: unwitnessed, never TRUE");

    printf("H5 log\n");
    char path[600];
    snprintf(path, sizeof path, "%s/tiamat_test.log", argc > 1 ? argv[1] : ".");
    remove(path);
    script("line one\nsays \"391\"", 1, "true", 1);
    zxv_tiamat_answer(&c, ask, NULL, "What is 17 times 23?", &t, out, sizeof out, &shown);
    CHECK(zxv_tiamat_log(path, "What is 17 times 23?", &c, &t) == 0, "the log is written");
    FILE *f = fopen(path, "rb");
    char line[8192] = "";
    if (f) {
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
    }
    CHECK(strstr(line, "\"lead\":\"Spinewyrm\"") && strstr(line, "line one\\nsays \\\"391\\\"") &&
              strstr(line, "\"verdict\":\"TRUE\"") && strstr(line, "\"expr\":\"17 x 23 = 391\"") &&
              line[strlen(line) - 1] == '\n',
          "one JSON line, strings escaped");
    remove(path);
    CHECK(zxv_tiamat_log("", "q", &c, &t) == 0, "no log path: nothing written");

    printf(failures ? "\nFAILED: %d\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
