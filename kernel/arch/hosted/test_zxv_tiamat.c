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

/* H6: cells that answer by what they are asked. The split prompt gets
 * SPLIT_REPLY (from SPLIT_RC); a witness prompt gets "false" when it holds
 * WITNESS_NO, "both" when it holds WITNESS_BOTH, else "true"; any other
 * prompt is a part, answered with ANSWER (or "42" for "6 times 7"). */
static const char *SPLIT_REPLY, *WITNESS_NO, *WITNESS_BOTH;
static int SPLIT_RC;
static int CALLS6[3], SPLITS6[3];

static int ask6(void *ctx, uint32_t cell, const char *prompt, char *out, size_t cap)
{
    (void) ctx;
    CALLS6[cell]++;
    if (!strncmp(prompt, "Split this question", 19)) {
        SPLITS6[cell]++;
        snprintf(out, cap, "%s", SPLIT_REPLY);
        return SPLIT_RC;
    }
    if (!strncmp(prompt, "Is the following statement", 26)) {
        snprintf(out, cap, "%s",
                 WITNESS_NO && strstr(prompt, WITNESS_NO)       ? "false"
                 : WITNESS_BOTH && strstr(prompt, WITNESS_BOTH) ? "both"
                                                                : "true");
        return 1;
    }
    snprintf(out, cap, "%s", strstr(prompt, "6 times 7") ? "42" : "answer to: ");
    if (!strstr(prompt, "6 times 7")) snprintf(out, cap, "answer to: %s", prompt);
    return 1;
}

static void script6(const char *split, int rc, const char *no, const char *both)
{
    SPLIT_REPLY = split, SPLIT_RC = rc, WITNESS_NO = no, WITNESS_BOTH = both;
    memset(CALLS6, 0, sizeof CALLS6);
    memset(SPLITS6, 0, sizeof SPLITS6);
}

static void test_h6(const zxv_tiamat_conf_t *c, const char *dir)
{
    static zxv_tiamat_deleg_t d;
    static zxv_tiamat_trace_t t;
    char out[8192], ref[8192];
    int shown, rshown;
    printf("H6 delegate\n");

    /* no split: the same answer as H2-H4 alone */
    script6("NONE", 1, NULL, NULL);
    int tv = zxv_tiamat_delegate(c, ask6, NULL, "what is a python function", &d, out, sizeof out,
                                 &shown);
    script6("NONE", 1, NULL, NULL);
    int rtv =
        zxv_tiamat_answer(c, ask6, NULL, "what is a python function", &t, ref, sizeof ref, &rshown);
    CHECK(tv == rtv && tv == ZXV_TV_TRUE && shown == rshown && !strcmp(out, ref),
          "NONE: answered exactly as without H6");
    CHECK(d.holder == 1 && d.take.moved && d.take.tv == LPRES_STATE_TRUE && d.take.score_to == 2 &&
              d.n_parts == 0 && d.part[0].lead == 1,
          "the position goes to the best match (Codelindworm, 2), which leads");
    CHECK(d.tree.phase == SWD_SEALED && d.tree.n == 1 && d.tree.root_tv == LPRES_STATE_TRUE,
          "one sealed node carries the verdict");

    script6("x", 0, NULL, NULL);
    tv = zxv_tiamat_delegate(c, ask6, NULL, "what is a python function", &d, out, sizeof out,
                             &shown);
    CHECK(tv == ZXV_TV_TRUE && d.n_parts == 0 && d.split_rc == 0,
          "a holder that cannot split: one part");
    script6("just one line", 1, NULL, NULL);
    tv = zxv_tiamat_delegate(c, ask6, NULL, "what is a python function", &d, out, sizeof out,
                             &shown);
    CHECK(d.n_parts == 0 && !strcmp(d.part[0].lead_reply, "answer to: what is a python function"),
          "one line is not a split: the whole question runs");

    /* three parts: the reckoner, a held part, a witnessed part */
    const char *q3 = "Tell me what is 6 times 7, some python code, and why the sky is blue";
    script6("1. what is 6 times 7\n2) write python code\n - why is the sky blue  \n\n", 1, "python",
            NULL);
    tv = zxv_tiamat_delegate(c, ask6, NULL, q3, &d, out, sizeof out, &shown);
    CHECK(d.take.to == 0 && d.take.tv == LPRES_STATE_TRUE && d.holder == 0,
          "a tie inside one lineage: the first placed holds it, TRUE");
    CHECK(d.n_parts == 3 && !strcmp(d.part_q[0], "what is 6 times 7") &&
              !strcmp(d.part_q[1], "write python code") &&
              !strcmp(d.part_q[2], "why is the sky blue"),
          "list marks and spaces are trimmed, empty lines skipped");
    CHECK(!d.hand[0].moved && d.part[0].lead == 0 && d.part[0].reckon_tv == ZXV_TV_TRUE,
          "part 1 stays with the holder; the reckoner proves 42");
    CHECK(d.hand[1].moved && d.hand[1].from == 0 && d.hand[1].to == 1 &&
              d.hand[1].tv == LPRES_STATE_TRUE && d.part[1].tv == ZXV_TV_FALSE,
          "part 2 passes to Codelindworm; its witness says FALSE");
    CHECK(d.tree.node[d.node[1]].holder == 1 && d.tree.node[d.node[0]].holder == 0,
          "each part's node is held by the cell it went to (its record)");
    CHECK(!d.hand[2].moved && d.part[2].tv == LPRES_STATE_TRUE, "part 3 witnessed TRUE");
    CHECK(tv == ZXV_TV_FALSE && !shown && d.tree.n == 4 && d.n_conflicts == 0,
          "the whole is the conjoin: one FALSE part makes it FALSE");
    CHECK(strstr(out, "1. what is 6 times 7\n42") && strstr(out, "2. write python code\n") &&
              strstr(out, "Held: Codelindworm's answer") &&
              !strstr(out, "answer to: write python code") &&
              strstr(out, "split the question into 3 parts") && strstr(out, ": FALSE."),
          "every part is shown with its own verdict; the held draft is not");
    CHECK(SPLITS6[0] == 1 && SPLITS6[1] == 0 && CALLS6[2] == 2,
          "one split ask, one witness ask per unproved part");

    /* a part whose best matches tie across lineages; a BOTH witness */
    script6("check this code\nwhy is the sky blue", 1, NULL, "sky");
    tv = zxv_tiamat_delegate(c, ask6, NULL, "what and why", &d, out, sizeof out, &shown);
    CHECK(d.hand[0].moved && d.hand[0].to == 1 && d.hand[0].tv == LPRES_STATE_BOTH,
          "a tie across lineages: the first placed takes it, the claim BOTH");
    CHECK(d.part[1].tv == LPRES_STATE_BOTH && tv == ZXV_TV_BOTH && !shown && d.n_conflicts == 2,
          "a BOTH part makes the whole BOTH; both stay in the tree");

    /* five lines: four parts run, the fifth is lost and the whole is NEITHER */
    script6("why a\nwhy b\nwhy c\nwhy d\nwhy e", 1, NULL, NULL);
    tv = zxv_tiamat_delegate(c, ask6, NULL, "why", &d, out, sizeof out, &shown);
    CHECK(d.n_parts == 4 && d.n_lost == 1 && d.part[3].tv == LPRES_STATE_TRUE &&
              tv == ZXV_TV_NEITHER && shown && strstr(out, "1 further part was not run"),
          "four TRUE parts and a lost one: NEITHER, never TRUE");

    /* a decimal is not a list mark */
    script6("why a\nnone\nwhy b", 1, NULL, NULL);
    zxv_tiamat_delegate(c, ask6, NULL, "why", &d, out, sizeof out, &shown);
    CHECK(d.n_parts == 2 && !strcmp(d.part_q[1], "why b"), "a NONE line among parts is skipped");
    script6("3.5 times 2\n2. why", 1, NULL, NULL);
    zxv_tiamat_delegate(c, ask6, NULL, "why", &d, out, sizeof out, &shown);
    CHECK(d.n_parts == 2 && !strcmp(d.part_q[0], "3.5 times 2") && !strcmp(d.part_q[1], "why"),
          "\"3.5\" keeps its digits; \"2.\" is a mark");

    /* H5 for H6 */
    char path[600];
    snprintf(path, sizeof path, "%s/tiamat_h6.log", dir);
    remove(path);
    script6("1. what is 6 times 7\n2. write python code", 1, "python", NULL);
    zxv_tiamat_delegate(c, ask6, NULL, q3, &d, out, sizeof out, &shown);
    CHECK(zxv_tiamat_log_deleg(path, q3, c, &d) == 0, "the H6 log is written");
    FILE *f = fopen(path, "rb");
    static char line[65536];
    line[0] = 0;
    if (f) {
        if (!fgets(line, sizeof line, f)) line[0] = 0;
        fclose(f);
    }
    CHECK(strstr(line, "\"holder\":\"Spinewyrm\"") &&
              strstr(line, "\"hand\":{\"from\":\"Spinewyrm\",\"to\":\"Codelindworm\"") &&
              strstr(line, "\"parts\":[{\"q\":\"what is 6 times 7\"") &&
              strstr(line, "\"conflicts\":0,\"verdict\":\"FALSE\"") &&
              line[strlen(line) - 1] == '\n',
          "one JSON line with the position, the hand-offs and every part");
    remove(path);
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
    CHECK(parse_err("slot 12 /x\ncell A 0 q -\n", "line 1"), "slot 12 is refused");
    {
        zxv_tiamat_conf_t p;
        char e[200];
        const char *h = "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef";
        char txt[300];
        snprintf(txt, sizeof txt, "slot 0 /x %s\nslot 1 /y\ncell A 0 q -\n", h);
        CHECK(zxv_tiamat_parse(txt, &p, e, sizeof e) == 0 && !strcmp(p.slot_sha[0], h) &&
                  p.slot_sha[1][0] == 0,
              "a slot may carry its pinned SHA-256");
    }
    CHECK(parse_err("slot 0 /x abc\ncell A 0 q -\n", "64 hex"), "a short SHA-256 is refused");
    CHECK(parse_err(
              "slot 0 /x "
              "g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\ncell A 0 q -\n",
              "64 hex"),
          "a SHA-256 with a non-hex digit is refused");
    CHECK(parse_err("slot 0 /x\ncell A 1 q -\n", "slot 1, which has no model"),
          "a cell on an empty slot is refused");
    CHECK(parse_err("slot 0 /x\n", "no cells"), "no cells is refused");
    CHECK(parse_err("slot 0 /x\ncell A 0 q -\ncell A 0 r -\n", "twice"), "a name twice is refused");
    CHECK(parse_err("model /x\n", "unknown directive"), "an unknown directive is refused");
    CHECK(parse_err("slot 0 \"/x\n", "quote"), "an open quote is refused");
    CHECK(parse_err("slot 0 /x\ncell A 0 q - abcdefghijklmnopqrstuvwxyz\n", "23 bytes"),
          "a keyword longer than 23 bytes is refused");
    CHECK(parse_err("slot 0 /x "
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef extra\n"
                    "cell A 0 q -\n",
                    "extra words"),
          "extra words are refused");
    CHECK(parse_err("cell A 0 q - x\nlog /l extra\nslot 0 /x\n", "extra words"),
          "extra words after log are refused");

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
    CHECK(strncmp(out, "17 x 23 = 391 (exact arithmetic)\n", 33) == 0 && t.exact_shown == 1,
          "reckoner FALSE: the exact result is the answer shown first");

    script("17 times 23 is 391.", 1, "false", 1);
    tv = zxv_tiamat_answer(&c, ask, NULL, "What is 17 times 23?", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_TRUE && shown && strstr(out, "exact arithmetic") &&
              !strstr(out, "Vouchwyvern") && CALLS[2] == 0 && t.exact_shown == 0,
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

    printf("H3-H4 planted answers\n");
    script("never asked", 1, "false", 1);
    tv = zxv_tiamat_check(&c, ask, NULL, "what is the capital of Australia", "Sydney.", &t, out,
                          sizeof out, &shown);
    CHECK(tv == ZXV_TV_FALSE && !shown && CALLS[0] == 0 && CALLS[2] == 1 &&
              !strcmp(t.lead_reply, "Sydney.") && strstr(LAST_PROMPT[2], "is: Sydney."),
          "a planted answer goes to the witness; the lead is not asked");
    script("never asked", 1, "true", 1);
    tv = zxv_tiamat_check(&c, ask, NULL, "What is 6 times 7?", "42", &t, out, sizeof out, &shown);
    CHECK(tv == ZXV_TV_TRUE && shown && CALLS[0] == 0 && CALLS[2] == 0,
          "a planted sum is decided by the reckoner alone");

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

    CHECK(!c.delegate, "H6 is off unless the config says delegate");
    {
        zxv_tiamat_conf_t d6;
        char e[200];
        char txt[600];
        snprintf(txt, sizeof txt, "%sdelegate\n", CONF);
        CHECK(zxv_tiamat_parse(txt, &d6, e, sizeof e) == 0 && d6.delegate == 1,
              "the delegate directive turns H6 on");
        CHECK(parse_err("delegate now\ncell A 0 q -\n", "extra words"), "delegate takes no words");
    }
    test_h6(&c, argc > 1 ? argv[1] : ".");

    printf(failures ? "\nFAILED: %d\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
