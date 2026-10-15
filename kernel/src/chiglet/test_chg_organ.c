/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_chg_organ.c — the chiglet vocabulary (chg_vocab.h V1-V3) and the
 * organ router (chg_organ.h O1-O6), with scripted cells. */
#include <stdio.h>
#include <string.h>
#include "chg_organ.h"
#include "chg_vocab.h"

static int pass, fail;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m);                                      \
        }                                                                                          \
    } while (0)

/* ---- V1-V3 ---- */
static void test_vocab(void)
{
    CHECK(chgv_organ_find("smith", 5) == CHGV_O_SMITH, "V1 smith");
    CHECK(chgv_organ_find("voice_of_tiamat", 15) == CHGV_O_VOICE_OF_TIAMAT, "V1 first organ");
    CHECK(chgv_organ_find("clerk", 5) == CHGV_O_CLERK, "V1 last organ");
    CHECK(chgv_organ_find("smit", 4) == -1, "V3 no prefix match");
    CHECK(chgv_organ_find("smiths", 6) == -1, "V3 no longer match");
    CHECK(chgv_organ_find("Smith", 5) == -1, "V3 lower case only");
    CHECK(chgv_organ_find("smith", 0) == -1 && chgv_organ_find(NULL, 5) == -1, "V3 empty");
    CHECK(chgv_organ_find("voice", 5) == CHGV_O_VOICE, "V3 voice is not voice_of_tiamat");
    CHECK(strcmp(chgv_organ_name(CHGV_O_ARBITER), "arbiter") == 0, "V1 name");
    CHECK(strcmp(chgv_organ_name(CHGV_ORGANS), "") == 0, "V1 name out of range");
    CHECK(chgv_organ_witness(CHGV_O_SMITH) == CHGV_O_WARDEN, "V1 smith <- warden");
    CHECK(chgv_organ_witness(CHGV_O_RECKONER) == CHGV_O_ARBITER, "V1 reckoner <- arbiter");
    CHECK(chgv_organ_witness(CHGV_O_NATURAL_PHILOSOPHER) == CHGV_O_RECKONER,
          "V1 natural_philosopher <- reckoner");
    CHECK(chgv_organ_witness(CHGV_O_BARD) == CHGV_O_ARBITER, "V1 default arbiter");
    CHECK(chgv_organ_witness(CHGV_ORGANS) == -1, "V1 witness out of range");
    for (uint32_t i = 0; i < CHGV_ORGANS; i++) {
        const char *n = chgv_organ_name(i);
        CHECK(chgv_organ_find(n, strlen(n)) == (int) i, "V1 every organ round-trips");
    }

    CHECK(chgv_act_find("trust_device", 12) == CHGV_A_TRUST_DEVICE, "V2 act");
    CHECK(chgv_act_find("enrol_key", 9) == 0, "V2 first act is index 0");
    CHECK(chgv_act_find("import_machine", 14) == CHGV_NACTS - 1, "V2 last act");
    CHECK(chgv_act_find("trust", 5) == -1, "V3 act prefix");
    CHECK(chgv_act_find("format_disk", 11) == -1, "V3 unknown act");
    CHECK(chgv_act_layer(CHGV_A_ENROL_KEY) == 0, "V2 L0");
    CHECK(chgv_act_layer(CHGV_A_TRUST_DEVICE) == 1, "V2 L1");
    CHECK(chgv_act_layer(CHGV_A_ADD_RULE) == 2, "V2 L2");
    CHECK(chgv_act_layer(CHGV_A_RELEASE_QUARANTINE) == 3, "V2 L3");
    CHECK(chgv_act_layer(CHGV_A_STAGE_UPDATE) == 4, "V2 L4");
    CHECK(chgv_act_layer(CHGV_A_LOAD_MODEL) == 5, "V2 L5");
    CHECK(chgv_act_layer(CHGV_A_SET_LANGUAGE) == 6, "V2 L6");
    CHECK(chgv_act_layer(CHGV_A_RUN_COMMAND) == 7, "V2 L7");
    CHECK(chgv_act_layer(CHGV_NACTS) == -1, "V2 layer out of range");
    CHECK(strcmp(chgv_act_name(CHGV_NACTS), "") == 0, "V2 name out of range");
    uint32_t per_layer[8] = {0};
    for (uint32_t i = 0; i < CHGV_NACTS; i++) {
        const char *n = chgv_act_name(i);
        CHECK(chgv_act_find(n, strlen(n)) == (int) i, "V2 every act round-trips");
        int l = chgv_act_layer(i);
        if (l >= 0 && l < 8) per_layer[l]++;
        for (size_t k = 0; n[k]; k++)
            CHECK((n[k] >= 'a' && n[k] <= 'z') || n[k] == '_', "V3 act names are [a-z_]");
    }
    for (int l = 0; l < 8; l++) CHECK(per_layer[l] > 0, "V2 every layer has an act");
}

/* ---- scripted cells ---- */
typedef struct {
    lpres_state_t lead_v[CHGO_CELLS], check_v[CHGO_CELLS];
    uint64_t lead_tok, check_tok;
    int fail_answer, fail_check;
    int answered[CHGO_CELLS], checked[CHGO_CELLS];
    char seen_answer[64];
} script_t;

static int s_answer(void *ctx, const chgo_cell_t *c, const char *p, size_t pl, char *out,
                    size_t cap, lpres_state_t *v, uint64_t *tok)
{
    script_t *s = ctx;
    (void) p;
    (void) pl;
    s->answered[c->agent]++;
    if (s->fail_answer) return -1;
    snprintf(out, cap, "answer from %u", (unsigned) c->agent);
    *v = s->lead_v[c->agent];
    *tok = s->lead_tok;
    return 0;
}

static int s_check(void *ctx, const chgo_cell_t *c, const char *p, size_t pl, const char *a,
                   size_t al, lpres_state_t *v, uint64_t *tok)
{
    script_t *s = ctx;
    (void) p;
    (void) pl;
    s->checked[c->agent]++;
    snprintf(s->seen_answer, sizeof s->seen_answer, "%.*s", (int) al, a);
    if (s->fail_check) return -1;
    *v = s->check_v[c->agent];
    *tok = s->check_tok;
    return 0;
}

static void script_reset(script_t *s)
{
    memset(s, 0, sizeof *s);
    s->lead_tok = 10;
    s->check_tok = 5;
}

enum { LIN_QWEN = 1, LIN_OTHER = 2 };

/* smith: agent 0 (qwen) lead, agent 1 (qwen); warden: agent 2 (qwen), agent 3 (other);
 * arbiter: agent 4 (other) */
static void table(chgo_table_t *t)
{
    chgo_init(t);
    CHECK(chgo_add(t, CHGV_O_SMITH, 0, LIN_QWEN) == CHGO_OK, "O1 add");
    CHECK(chgo_add(t, CHGV_O_SMITH, 1, LIN_QWEN) == CHGO_OK, "O1 add");
    CHECK(chgo_add(t, CHGV_O_WARDEN, 2, LIN_QWEN) == CHGO_OK, "O1 add");
    CHECK(chgo_add(t, CHGV_O_WARDEN, 3, LIN_OTHER) == CHGO_OK, "O1 add");
    CHECK(chgo_add(t, CHGV_O_ARBITER, 4, LIN_OTHER) == CHGO_OK, "O1 add");
}

static void test_table(void)
{
    chgo_table_t t;
    table(&t);
    CHECK(chgo_add(&t, CHGV_ORGANS, 9, 1) == CHGO_E_ARG, "O1 unknown organ refused");
    CHECK(chgo_add(NULL, 0, 0, 0) == CHGO_E_ARG, "O1 null table");
    CHECK(chgo_lead(&t, CHGV_O_SMITH) == 0, "O1 first listed is the lead");
    CHECK(chgo_lead(&t, CHGV_O_WARDEN) == 2, "O1 warden lead");
    CHECK(chgo_lead(&t, CHGV_O_BARD) == -1, "O1 no bard");
    CHECK(chgo_witness(&t, 0) == 3, "O3 smith's witness: warden of another lineage");
    CHECK(chgo_witness(&t, 2) == 4, "O3 warden's witness: arbiter of another lineage");
    CHECK(chgo_witness(&t, 4) == -1, "O3 the only arbiter has no other-lineage witness");
    CHECK(chgo_witness(&t, 99) == -1 && chgo_witness(&t, -1) == -1, "O3 bad lead");

    /* O3 fallback: the witness organ has only same-lineage cells, the lead's
     * own organ has another lineage */
    chgo_table_t u;
    chgo_init(&u);
    chgo_add(&u, CHGV_O_BARD, 0, LIN_QWEN);
    chgo_add(&u, CHGV_O_ARBITER, 1, LIN_QWEN);
    chgo_add(&u, CHGV_O_BARD, 2, LIN_OTHER);
    CHECK(chgo_witness(&u, 0) == 2, "O3 falls back to the lead's own organ");

    chgo_table_t f;
    chgo_init(&f);
    int rc = CHGO_OK;
    for (uint32_t i = 0; i < CHGO_CELLS && rc == CHGO_OK; i++) rc = chgo_add(&f, 0, i, 1);
    CHECK(rc == CHGO_OK && f.n == CHGO_CELLS, "O1 fills to CHGO_CELLS");
    CHECK(chgo_add(&f, 0, 0, 1) == CHGO_E_FULL, "O1 full");
}

static int ask(const chgo_table_t *t, uint8_t organ, uint64_t budget, script_t *s, char *out,
               size_t cap, chgo_result_t *r)
{
    return chgo_ask(t, organ, "fix the loop", 12, budget, s_answer, s_check, s, out, cap, r);
}

static void test_ask(void)
{
    chgo_table_t t;
    table(&t);
    script_t s;
    char out[64];
    chgo_result_t r;

    /* agree: TRUE + TRUE from another lineage */
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_TRUE;
    CHECK(ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r) == CHGO_OK, "O2 ok");
    CHECK(strcmp(out, "answer from 0") == 0, "O2 the lead answered");
    CHECK(s.answered[0] == 1 && s.answered[1] == 0, "O2 only the lead answers");
    CHECK(r.lead_cell == 0 && r.witness_cell == 3, "O3 cells");
    CHECK(s.checked[3] == 1 && s.checked[2] == 0, "O3 the same-lineage warden is never asked");
    CHECK(strcmp(s.seen_answer, "answer from 0") == 0, "O3 the witness reads the answer");
    CHECK(r.witnessed && r.verdict == LPRES_STATE_TRUE, "O4 agreement keeps TRUE");
    CHECK(r.tokens == 15, "O5 both charged");
    CHECK(lpx_base(r.tag) == LPX_B_TRUE && lpx_pol(r.tag) == LPX_P_NEGENTROPIC &&
              lpx_phase(r.tag) == LPX_PH_POTENTIAL,
          "O6 agreed TRUE is negentropic");
    CHECK(lpx_action(r.tag) == LPX_ACT_PROCEED, "O6 offer it");

    /* agree on FALSE */
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_FALSE;
    s.check_v[3] = LPRES_STATE_FALSE;
    ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r);
    CHECK(r.witnessed && r.verdict == LPRES_STATE_FALSE && lpx_pol(r.tag) == LPX_P_NEGENTROPIC,
          "O4 agreement keeps FALSE");

    /* contradiction: TRUE against FALSE is BOTH, shown, never averaged */
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_FALSE;
    ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r);
    CHECK(r.witnessed && r.verdict == LPRES_STATE_BOTH, "O4 TRUE vs FALSE is BOTH");
    CHECK(lpx_pol(r.tag) == LPX_P_NEUTRAL && lpx_action(r.tag) == LPX_ACT_ESCALATE,
          "O6 a dispute goes to the person");
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_FALSE;
    s.check_v[3] = LPRES_STATE_BOTH;
    ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r);
    CHECK(r.verdict == LPRES_STATE_BOTH, "O4 a BOTH witness makes BOTH");

    /* the witness says nothing: one voice is not enough */
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_NEITHER;
    ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r);
    CHECK(!r.witnessed && r.verdict == LPRES_STATE_NEITHER, "O4 silent witness: NEITHER");
    CHECK(r.witness_cell == 3 && r.tokens == 15, "O4 it was asked and charged");
    CHECK(lpx_action(r.tag) == LPX_ACT_WAIT, "O6 not sure");

    /* no witness of another lineage at all */
    script_reset(&s);
    s.lead_v[4] = LPRES_STATE_TRUE;
    ask(&t, CHGV_O_ARBITER, 100, &s, out, sizeof out, &r);
    CHECK(r.witness_cell == -1 && !r.witnessed && r.verdict == LPRES_STATE_NEITHER,
          "O4 unwitnessed TRUE is NEITHER");
    CHECK(r.lead == LPRES_STATE_TRUE, "O2 the lead's own verdict is kept");

    /* a BOTH lead stays BOTH and asks nobody */
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_BOTH;
    ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r);
    CHECK(r.verdict == LPRES_STATE_BOTH && r.witness_cell == -1 && s.checked[3] == 0,
          "O4 BOTH lead stays BOTH");
    CHECK(lpx_action(r.tag) == LPX_ACT_ESCALATE, "O6 BOTH escalates");

    /* a NEITHER lead stays NEITHER and asks nobody */
    script_reset(&s);
    ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r);
    CHECK(r.verdict == LPRES_STATE_NEITHER && r.witness_cell == -1 && s.checked[3] == 0 &&
              r.tokens == 10,
          "O4 NEITHER lead asks no witness");

    /* O5 budget */
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_TRUE;
    ask(&t, CHGV_O_SMITH, 10, &s, out, sizeof out, &r);
    CHECK(r.witness_cell == -1 && s.checked[3] == 0 && r.verdict == LPRES_STATE_NEITHER,
          "O5 lead used it up: no witness");
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_TRUE;
    ask(&t, CHGV_O_SMITH, 14, &s, out, sizeof out, &r);
    CHECK(s.checked[3] == 1 && !r.witnessed && r.verdict == LPRES_STATE_NEITHER && r.tokens == 14,
          "O5 an over-budget witness does not count");
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_TRUE;
    ask(&t, CHGV_O_SMITH, 15, &s, out, sizeof out, &r);
    CHECK(r.witnessed && r.tokens == 15, "O5 exactly the budget counts");
    script_reset(&s);
    CHECK(ask(&t, CHGV_O_SMITH, 9, &s, out, sizeof out, &r) == CHGO_E_BUDGET && out[0] == 0 &&
              r.tokens == 9,
          "O5 an overrunning lead is refused and its answer dropped");
    script_reset(&s);
    CHECK(ask(&t, CHGV_O_SMITH, 0, &s, out, sizeof out, &r) == CHGO_E_BUDGET && s.answered[0] == 0,
          "O5 no budget: nothing runs");
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = LPRES_STATE_TRUE;
    s.check_tok = UINT64_MAX;
    ask(&t, CHGV_O_SMITH, UINT64_MAX, &s, out, sizeof out, &r);
    CHECK(!r.witnessed && r.tokens == UINT64_MAX, "O5 overflow is checked");

    /* errors */
    script_reset(&s);
    CHECK(ask(&t, CHGV_O_BARD, 100, &s, out, sizeof out, &r) == CHGO_E_NOCELL && s.answered[0] == 0,
          "O2 no cell: nothing runs");
    script_reset(&s);
    s.fail_answer = 1;
    CHECK(ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r) == CHGO_E_CELL && out[0] == 0,
          "a failed lead");
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.fail_check = 1;
    CHECK(ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r) == CHGO_E_CELL, "a failed witness");
    script_reset(&s);
    s.lead_v[0] = (lpres_state_t) 7;
    CHECK(ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r) == CHGO_E_CELL,
          "an invalid verdict is refused");
    script_reset(&s);
    s.lead_v[0] = LPRES_STATE_TRUE;
    s.check_v[3] = (lpres_state_t) 9;
    CHECK(ask(&t, CHGV_O_SMITH, 100, &s, out, sizeof out, &r) == CHGO_E_CELL,
          "an invalid witness verdict is refused");
    CHECK(chgo_ask(&t, CHGV_O_SMITH, "x", 1, 10, s_answer, s_check, &s, out, sizeof out, NULL) ==
              CHGO_E_ARG,
          "null result");
    CHECK(chgo_ask(&t, CHGV_O_SMITH, "x", 1, 10, NULL, s_check, &s, out, sizeof out, &r) ==
              CHGO_E_ARG,
          "null answer fn");
    CHECK(chgo_ask(&t, CHGV_ORGANS, "x", 1, 10, s_answer, s_check, &s, out, sizeof out, &r) ==
              CHGO_E_ARG,
          "unknown organ");

    /* a small out buffer stays terminated */
    script_reset(&s);
    char tiny[6];
    ask(&t, CHGV_O_SMITH, 100, &s, tiny, sizeof tiny, &r);
    CHECK(strlen(tiny) < sizeof tiny, "out is terminated");
}

/* every act's layer, by the enum's layer bands */
static void test_layers(void)
{
    static const uint32_t first[9] = {
        CHGV_A_ENROL_KEY,          CHGV_A_TRUST_DEVICE, CHGV_A_ADD_RULE,
        CHGV_A_RELEASE_QUARANTINE, CHGV_A_STAGE_UPDATE, CHGV_A_CHOOSE_BACKEND,
        CHGV_A_SET_LANGUAGE,       CHGV_A_RUN_COMMAND,  CHGV_NACTS};
    int bad = 0;
    for (int l = 0; l < 8; l++)
        for (uint32_t a = first[l]; a < first[l + 1]; a++)
            if (chgv_act_layer(a) != l) bad++;
    CHECK(bad == 0, "V2 every act sits in its layer band");
    CHECK(chgv_act_find(NULL, 3) == -1 && chgv_act_find("enrol_key", 0) == -1, "V3 act empty");
    /* V3 exact length: a longer buffer whose first n bytes are a name */
    CHECK(chgv_organ_find("smithy", 5) == CHGV_O_SMITH, "V3 length bounds the match");
    CHECK(chgv_act_find("stop_apps", 8) == CHGV_A_STOP_APP, "V3 act length bounds the match");
}

/* a cell that writes a fixed text and leaves verdict and tokens alone
 * unless told */
typedef struct {
    const char *text;
    int fill; /* fill the whole buffer, no NUL */
    int fail; /* write junk then fail */
    int set;  /* set verdict and tokens */
    lpres_state_t v;
    uint64_t tok;
    size_t seen_len;
    lpres_state_t cv;
    int checked;
} raw_t;

static int r_answer(void *ctx, const chgo_cell_t *c, const char *p, size_t pl, char *out,
                    size_t cap, lpres_state_t *v, uint64_t *tok)
{
    raw_t *x = ctx;
    (void) c;
    (void) p;
    (void) pl;
    if (x->fill) {
        memset(out, 'a', cap);
    } else {
        snprintf(out, cap, "%s", x->text);
    }
    if (x->fail) return -1;
    if (x->set) {
        *v = x->v;
        *tok = x->tok;
    }
    return 0;
}

static int r_check(void *ctx, const chgo_cell_t *c, const char *p, size_t pl, const char *a,
                   size_t al, lpres_state_t *v, uint64_t *tok)
{
    raw_t *x = ctx;
    (void) c;
    (void) p;
    (void) pl;
    (void) a;
    x->checked++;
    x->seen_len = al;
    *v = x->cv;
    (void) tok; /* leaves the tokens alone */
    return 0;
}

static void test_edges(void)
{
    chgo_table_t t;
    chgo_result_t r;
    char out[16];
    raw_t x;

    /* O1: init empties the table; a stale cell past n is never read */
    chgo_init(&t);
    chgo_add(&t, CHGV_O_SMITH, 0, 1);
    chgo_add(&t, CHGV_O_WARDEN, 1, 2);
    chgo_init(&t);
    CHECK(t.n == 0 && chgo_lead(&t, CHGV_O_SMITH) == -1, "O1 init empties the table");
    chgo_init(NULL); /* no crash */
    CHECK(chgo_lead(NULL, CHGV_O_SMITH) == -1 && chgo_witness(NULL, 0) == -1, "O1 null table");
    chgo_add(&t, CHGV_O_SMITH, 0, 1);
    chgo_add(&t, CHGV_O_WARDEN, 1, 2);
    t.n = 1;
    CHECK(chgo_witness(&t, 0) == -1, "O3 a stale witness past n is never picked");
    t.n = 2;
    CHECK(chgo_lead(&t, CHGV_O_WARDEN) == 1, "O1 the last cell is a lead");
    chgo_init(&t);
    chgo_add(&t, CHGV_O_WARDEN, 0, 2);
    chgo_add(&t, CHGV_O_SMITH, 1, 1);
    t.n = 1;
    CHECK(chgo_witness(&t, 1) == -1, "O3 a lead index of n is out of range");

    /* O3: a witness at table index 0 */
    chgo_init(&t);
    chgo_add(&t, CHGV_O_WARDEN, 0, 2);
    chgo_add(&t, CHGV_O_SMITH, 1, 1);
    chgo_add(&t, CHGV_O_SMITH, 2, 3);
    CHECK(chgo_witness(&t, 1) == 0, "O3 the witness organ's cell at index 0 is used");
    memset(&x, 0, sizeof x);
    x.text = "fixed";
    x.set = 1;
    x.v = LPRES_STATE_TRUE;
    x.tok = 4;
    x.cv = LPRES_STATE_TRUE;
    CHECK(chgo_ask(&t, CHGV_O_SMITH, "p", 1, 100, r_answer, r_check, &x, out, sizeof out, &r) ==
                  CHGO_OK &&
              r.witness_cell == 0 && r.witnessed,
          "O3 a witness at index 0 is asked");
    CHECK(x.seen_len == 5, "O3 the witness gets the answer's length");
    CHECK(r.tokens == 4, "O5 a witness reporting no tokens costs none");

    /* O3: a corrupt organ number in a cell has no witness */
    chgo_init(&t);
    t.cell[0] = (chgo_cell_t){200, 0, 1};
    t.cell[1] = (chgo_cell_t){200, 1, 2};
    t.n = 2;
    CHECK(chgo_witness(&t, 0) == -1, "O3 an unknown organ has no witness");

    /* O2: an answer that reports no tokens costs none; cap 1 works */
    chgo_init(&t);
    chgo_add(&t, CHGV_O_BARD, 0, 1);
    memset(&x, 0, sizeof x);
    x.text = "song";
    CHECK(chgo_ask(&t, CHGV_O_BARD, "p", 1, 100, r_answer, r_check, &x, out, sizeof out, &r) ==
                  CHGO_OK &&
              r.tokens == 0 && r.verdict == LPRES_STATE_NEITHER && strcmp(out, "song") == 0,
          "O5 an answer reporting no tokens costs none");
    char one[1] = {'z'};
    CHECK(chgo_ask(&t, CHGV_O_BARD, "p", 1, 100, r_answer, r_check, &x, one, 1, &r) == CHGO_OK &&
              one[0] == 0,
          "O2 a one-byte buffer holds the empty answer");
    CHECK(chgo_ask(&t, CHGV_O_BARD, "p", 1, 100, r_answer, r_check, &x, out, 0, &r) == CHGO_E_ARG,
          "O2 a zero-byte buffer is refused");

    /* O2: a cell that fills the buffer without a NUL is cut to cap-1 */
    memset(&x, 0, sizeof x);
    x.fill = 1;
    chgo_ask(&t, CHGV_O_BARD, "p", 1, 100, r_answer, r_check, &x, out, sizeof out, &r);
    CHECK(strlen(out) == sizeof out - 1 && out[sizeof out - 2] == 'a', "O2 out is cut at cap-1");

    /* a failed cell's junk is dropped */
    memset(&x, 0, sizeof x);
    x.text = "junk";
    x.fail = 1;
    CHECK(chgo_ask(&t, CHGV_O_BARD, "p", 1, 100, r_answer, r_check, &x, out, sizeof out, &r) ==
                  CHGO_E_CELL &&
              out[0] == 0,
          "a failed lead's text is dropped");

    /* errors leave a clean result and an empty out */
    memset(&r, 0x5a, sizeof r);
    strcpy(out, "stale");
    CHECK(chgo_ask(&t, CHGV_O_SMITH, "p", 1, 100, r_answer, r_check, &x, out, sizeof out, &r) ==
                  CHGO_E_NOCELL &&
              r.tokens == 0 && r.lead_cell == -1 && r.witness_cell == -1 && !r.witnessed &&
              r.verdict == LPRES_STATE_NEITHER && out[0] == 0,
          "O2 no cell: clean result, empty out");

    /* O5: a lead that uses exactly the budget is kept, with no witness */
    chgo_init(&t);
    chgo_add(&t, CHGV_O_SMITH, 0, 1);
    chgo_add(&t, CHGV_O_WARDEN, 1, 2);
    memset(&x, 0, sizeof x);
    x.text = "edge";
    x.set = 1;
    x.v = LPRES_STATE_TRUE;
    x.tok = 7;
    x.cv = LPRES_STATE_TRUE;
    CHECK(chgo_ask(&t, CHGV_O_SMITH, "p", 1, 7, r_answer, r_check, &x, out, sizeof out, &r) ==
                  CHGO_OK &&
              strcmp(out, "edge") == 0 && r.tokens == 7 && x.checked == 0 &&
              r.verdict == LPRES_STATE_NEITHER,
          "O5 a lead at exactly the budget is kept");
}

int main(void)
{
    test_vocab();
    test_table();
    test_ask();
    test_layers();
    test_edges();
    printf("%d/%d checks passed\n", pass, pass + fail);
    return fail ? 1 : 0;
}
