/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tributary's model against zxvd's real dispatcher (LINK): model.info,
 * swarm.ask and swarm.state as zxvd answers them (the stub host's model,
 * answer and state), and peer.ask refused, since this in-process zxvd has
 * no peer network. power.status, play.run and peer.ask are answered on a
 * link of Tributary's own (TB_LINK) in their shapes
 * (api/zxvd.openrpc.json), which passes every other request to the real
 * dispatcher and counts every request it is given. The clock is the
 * test's: each read moves it TB_STEP ms. */
#include "tributary_model.h"

static tributary_t TB;
static zxm_link_t TB_LINK;
static const char *TB_POWER, *TB_PLAY, *TB_PEER; /* the results, when set */
static int64_t TB_NOW, TB_STEP;
static int tb_sent;                  /* every request TB_LINK was given */
static char tb_req[ZXM_REQ_MAX + 1]; /* the last peer.ask sent */

static int32_t tb_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    tb_sent++;
    if (strstr(req, "\"power.status\"")) r = TB_POWER;
    if (strstr(req, "\"play.run\"")) r = TB_PLAY;
    if (strstr(req, "\"peer.ask\"")) {
        r = TB_PEER;
        memcpy(tb_req, req, len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX);
        tb_req[len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX] = 0;
    }
    if (r) return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    return inproc(NULL, req, len, resp, cap);
}

static int64_t tb_clock(void *ctx)
{
    (void) ctx;
    int64_t now = TB_NOW;
    TB_NOW += TB_STEP;
    return now;
}

#define TB_SETTLE(answer, paid)                                                                    \
    "{\"answer\":\"" answer "\",\"settlement\":{\"role\":\"buyer\",\"price_minor\":\"100\","       \
    "\"fee_minor\":\"5\",\"paid_minor\":\"" paid "\"},\"security\":\"ML-KEM-1024\"}"

APPS_TEST(tributary)
{
    const char *was_state = STATE;
    char shown[512];
    int before;

    /* TB1-TB8 through the model's own C */
    {
        int64_t code[3] = {1, 1, 2}, agree[3], n = 0, d = 0, tk[2] = {3, 1};
        CHECK(tributary_agree_counts(code, 3, agree) && agree[0] == 2 && agree[2] == 1 &&
                  tributary_verdict(code, 3, 3) == LPRES_STATE_BOTH &&
                  tributary_ask_within_caps(0, 0, 0) && !tributary_ask_within_caps(-1, 0, 0) &&
                  tributary_confidence_x100(2, 3, &n, &d) && n == 200 && d == 3 &&
                  tributary_ledger_after(0, 4, 3) == 24 &&
                  tributary_ledger_after(TRIB_LEDGER_MAX, 0, 1) == TRIB_REFUSED &&
                  tributary_source_fits(1, 0, 1, 1700000000000) &&
                  !tributary_source_fits(1, 100, 1, 0) && tributary_token_permille(tk, 2, 0) == 750,
              "tributary: the rules (TB1-TB8)");
    }

    /* TM1: the slot as the real dispatcher answers it */
    tributary_init(&TB, &LINK, tb_clock, NULL);
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tinyllama\",\"arch\":\"llama\"}";
    CHECK(tributary_read_slot(&TB) == ZXM_OK && TB.slot_known && TB.can_generate &&
              !strcmp(TB.slot_name, "tinyllama") && strstr(TB.notice, "tinyllama"),
          "tributary: model.info: %s (%s)", TB.notice, LINK.err);
    MODEL = "{\"name\":\"x\"}";
    tributary_init(&TB, &LINK, tb_clock, NULL);
    CHECK(tributary_read_slot(&TB) == ZXM_E_PROTOCOL && !TB.slot_known,
          "tributary: a slot without loaded is not zxvd's answer");

    /* peer.ask on the real dispatcher: no peer network here, refused */
    TB_NOW = INT64_C(1700000000000), TB_STEP = 250;
    MODEL = "{\"loaded\":true,\"can_generate\":true,\"name\":\"tinyllama\"}";
    CHECK(tributary_read_slot(&TB) == ZXM_OK &&
              tributary_set_caps(&TB, 1000, 100000, 500, 3, 4, true) == ZXM_OK &&
              tributary_question(&TB, "What is six times seven?") == ZXM_OK,
          "tributary: caps and a question on the real dispatcher");
    CHECK(tributary_ask_peer(&TB, "10.0.0.7", 100, true) == ZXM_E_REMOTE && TB.n == 0 &&
              TB.spent_price == 0,
          "tributary: peer.ask refused by this zxvd, nothing kept: %s", LINK.err);

    /* TM2: caps, owner-confirmed, before anything is asked */
    zxm_link_init(&TB_LINK, tb_send, &TB_LINK);
    tributary_init(&TB, &TB_LINK, tb_clock, NULL);
    CHECK(tributary_read_slot(&TB) == ZXM_OK, "tributary: the slot through its own link");
    before = tb_sent;
    CHECK(tributary_question(&TB, "What is six times seven?") == ZXM_OK &&
              tributary_ask_slot(&TB) == ZXM_E_STATE &&
              tributary_ask_peer(&TB, "10.0.0.7", 100, true) == ZXM_E_STATE &&
              tributary_ask_play(&TB, "sums") == ZXM_E_STATE && tb_sent == before && TB.n == 0,
          "tributary: no caps, nothing asked");
    CHECK(tributary_set_caps(&TB, 0, 1, 0, 3, 4, true) == ZXM_E_ARG &&
              tributary_set_caps(&TB, 1000, 0, 0, 3, 4, true) == ZXM_E_ARG &&
              tributary_set_caps(&TB, 1000, 1, -1, 3, 4, true) == ZXM_E_ARG &&
              tributary_set_caps(&TB, 1000, 1, 0, 0, 4, true) == ZXM_E_ARG &&
              tributary_set_caps(&TB, 1000, 1, 0, 9, 4, true) == ZXM_E_ARG &&
              tributary_set_caps(&TB, 1000, 1, 0, 3, 24, true) == ZXM_E_ARG && !TB.caps_set,
          "tributary: caps outside TM2 refused");
    CHECK(tributary_set_caps(&TB, 1000, 100000, 500, 3, 4, false) == ZXM_E_STATE && !TB.caps_set &&
              TB.cap_tokens == 0 && tb_sent == before && strstr(TB.notice, "confirm"),
          "tributary: caps without the owner's word: unchanged, nothing sent");
    CHECK(tributary_set_caps(&TB, 1000, 100000, 500, 3, 4, true) == ZXM_OK && TB.caps_set &&
              TB.quorum == 3 && TB.octane == 4,
          "tributary: caps set: %s", TB.notice);

    /* TM3: a question */
    CHECK(tributary_question(&TB, "") == ZXM_E_ARG && tributary_question(&TB, NULL) == ZXM_E_ARG,
          "tributary: an empty question is none");
    CHECK(tributary_question(&TB, "What is six times seven?") == ZXM_OK && TB.question == 2 &&
              TB.prompt_len == 24 && TB.n == 0,
          "tributary: question %lld", (long long) TB.question);

    /* TM4: the slot, its tokens from the companion's budget gate */
    TB_POWER = "{\"device\":\"laptop\",\"total_uj\":9000,\"total_mw\":36}";
    STATE = "{\"companion\":{\"cycle_open\":true,\"remaining\":970,\"last_answer\":{"
            "\"charged\":30}}}";
    ANSWER = "42";
    CHECK(tributary_ask_slot(&TB) == ZXM_OK && TB.n == 1 && TB.a[0].tokens == 30 &&
              TB.a[0].uj == 9000 && TB.a[0].ms == 250 && TB.a[0].code == 1 &&
              !strcmp(last_prompt, "What is six times seven?") && TB.ledger == 30 &&
              TB.spent_tokens == 30 && TB.held == ZXM_HELD_NONE,
          "tributary: the slot answered: %s (%s)", TB.notice, TB_LINK.err);

    /* a peer: priced, the owner's act, after the arguments and the caps */
    before = tb_sent;
    CHECK(tributary_ask_peer(&TB, "", 100, true) == ZXM_E_ARG &&
              tributary_ask_peer(&TB, "10.0.0.7", -1, true) == ZXM_E_ARG &&
              tributary_ask_peer(&TB, "10.0.0.7", 501, true) == ZXM_E_STATE &&
              tributary_ask_peer(&TB, "10.0.0.7", 200, false) == ZXM_E_STATE &&
              strstr(TB.notice, "confirm") && tb_sent == before && TB.n == 1 && TB.spent_price == 0,
          "tributary: a peer without the owner's word, or past the price cap: nothing sent");
    TB_PEER = TB_SETTLE("42.", "105");
    CHECK(tributary_ask_peer(&TB, "10.0.0.7", 200, true) == ZXM_OK && TB.n == 2 &&
              TB.a[1].code == 1 && TB.a[1].paid == 105 && TB.spent_price == 105 &&
              strstr(tb_req, "\"max_price_minor\":200") && strstr(tb_req, "\"peer\":\"10.0.0.7\""),
          "tributary: a peer said 42., normalised to the slot's 42: %s", TB.notice);
    TB_PEER = TB_SETTLE(" 41 ", "90");
    CHECK(tributary_ask_peer(&TB, "10.0.0.9", 200, true) == ZXM_OK && TB.n == 3 &&
              TB.a[2].code == 2 && TB.spent_price == 195,
          "tributary: another peer said 41");
    before = tb_sent;
    CHECK(tributary_ask_peer(&TB, "10.0.0.11", 400, true) == ZXM_E_STATE && tb_sent == before &&
              strstr(TB.notice, "Over a cap"),
          "tributary: 195 spent, 400 more would pass the 500 cap: %s", TB.notice);
    TB_PEER = "{\"answer\":\"x\",\"settlement\":{\"paid_minor\":\"-5\"}}";
    CHECK(tributary_ask_peer(&TB, "10.0.0.11", 10, true) == ZXM_E_PROTOCOL && TB.n == 3,
          "tributary: a settlement that is none: not kept");

    /* TM5, TM6: the join, BOTH, both answers shown with their models */
    CHECK(tributary_join(&TB) == ZXM_OK && TB.verdict == LPRES_STATE_BOTH &&
              (int) TB.verdict == 3 && TB.agree[0] == 2 && TB.agree[2] == 1 && TB.models[0] == 2 &&
              TB.models[2] == 1 && TB.conf_num[0] == 200 && TB.conf_den[0] == 3 &&
              TB.conf_num[2] == 100 && TB.conf_den[2] == 3 && TB.lead == 1 &&
              TB.lead_share_x100 == 6666 && TB.lead_meets_bar && TB.points[0] == 100 &&
              TB.points[1] == 100 && TB.points[2] == 0 && TB.uj_num == 9000 && TB.uj_den == 1 &&
              TB.fastest == 250 && TB.slowest == 250 && TB.held == ZXM_HELD_PENDING &&
              strstr(TB.notice, "BOTH"),
          "tributary: 42, 42, 41 under a quorum of 3: %s", TB.notice);
    CHECK(tributary_show(&TB, shown, sizeof shown) > 0 &&
              !strcmp(shown, "\"42\" (2 of 3): the slot tinyllama peer 10.0.0.7; \"41\" (1 of 3): "
                             "peer 10.0.0.9"),
          "tributary: shown: %s", shown);
    CHECK(tributary_token_share(&TB, 0) == 1000 && tributary_token_share(&TB, 1) == 0,
          "tributary: the slot spent every token of the question");

    /* TM7: the owner's word, once */
    CHECK(tributary_accept(&TB, 9, true) == ZXM_E_ARG &&
              tributary_accept(&TB, 2, false) == ZXM_E_STATE && TB.held == ZXM_HELD_PENDING,
          "tributary: an answer nobody gave, or no word: nothing accepted");
    CHECK(tributary_accept(&TB, 2, true) == ZXM_OK && TB.held == ZXM_HELD_ACCEPTED &&
              TB.accepted == 2 && strstr(TB.notice, "41") &&
              tributary_accept(&TB, 1, true) == ZXM_E_STATE && tributary_reject(&TB) == ZXM_E_STATE,
          "tributary: the owner accepted the minority 41 (BOTH follows from BOTH)");
    before = tb_sent;
    CHECK(tributary_ask_slot(&TB) == ZXM_E_STATE && tb_sent == before,
          "tributary: an accepted question asks no more");

    /* a new question: too few, then all agree; a template; a reject */
    CHECK(tributary_question(&TB, "Is the sum checked?") == ZXM_OK && TB.n == 0 && TB.ledger == 0 &&
              tributary_join(&TB) == ZXM_E_STATE &&
              tributary_show(&TB, shown, sizeof shown) == ZXM_E_STATE,
          "tributary: a new question starts empty");
    TB_PLAY = "{\"template\":\"sums\",\"verdict\":\"TRUE\",\"headline\":\"ok\"}";
    CHECK(tributary_ask_play(&TB, "") == ZXM_E_ARG && tributary_ask_play(&TB, "sums") == ZXM_OK &&
              TB.a[0].tokens == 0 && !strcmp(TB.a[0].text, "TRUE") &&
              tributary_join(&TB) == ZXM_OK && TB.verdict == LPRES_STATE_NEITHER &&
              strstr(TB.notice, "NEITHER") && !TB.lead_meets_bar && TB.points[0] == 100,
          "tributary: one answer of three: NEITHER: %s", TB.notice);
    ANSWER = "true.";
    CHECK(tributary_ask_slot(&TB) == ZXM_OK && tributary_ask_play(&TB, "sums") == ZXM_OK &&
              tributary_join(&TB) == ZXM_OK && TB.verdict == LPRES_STATE_TRUE &&
              TB.models[0] == 2 && TB.agree[0] == 3 && TB.lead == 1 && TB.lead_share_x100 == 6666 &&
              TB.lead_meets_bar && TB.points[0] == 200 && TB.points[1] == 100 &&
              strstr(TB.notice, "agree"),
          "tributary: TRUE, true. and TRUE agree; the template asked twice is one model: %s",
          TB.notice);
    CHECK(tributary_reject(&TB) == ZXM_OK && TB.held == ZXM_HELD_REJECTED &&
              tributary_accept(&TB, 1, true) == ZXM_E_STATE,
          "tributary: rejected, then nothing to accept");
    TB_PLAY = "{\"template\":\"sums\",\"verdict\":\"MAYBE\"}";
    CHECK(tributary_ask_play(&TB, "sums") == ZXM_E_PROTOCOL && TB.n == 3,
          "tributary: a verdict that is none: not kept");

    /* the token cap: 30 spent and 2 more estimated would pass 31 */
    CHECK(tributary_set_caps(&TB, 31, 100000, 0, 2, 0, true) == ZXM_OK &&
              tributary_question(&TB, "Again?") == ZXM_OK && tributary_ask_slot(&TB) == ZXM_OK &&
              TB.spent_tokens == 30,
          "tributary: 30 of 31 tokens");
    before = tb_sent;
    CHECK(tributary_ask_slot(&TB) == ZXM_E_STATE && tb_sent == before && TB.n == 1,
          "tributary: 1 token of room, 2 estimated: nothing asked");

    /* the energy reading: none keeps nothing */
    CHECK(tributary_set_caps(&TB, 1000, 100000, 0, 2, 0, true) == ZXM_OK, "tributary: room again");
    TB_POWER = "{\"total_uj\":-1}";
    CHECK(tributary_ask_slot(&TB) == ZXM_E_PROTOCOL && TB.n == 1 && TB.spent_tokens == 30,
          "tributary: negative energy is no reading: not kept");
    TB_POWER = NULL;
    CHECK(tributary_ask_slot(&TB) == ZXM_E_REMOTE && TB.n == 1,
          "tributary: no power ledger in this harness: not kept (%s)", TB_LINK.err);

    /* no estimate needed: without last_answer a token per four bytes (M4) */
    TB_POWER = "{\"total_uj\":7}";
    STATE = "{\"companion\":{\"cycle_open\":true}}";
    ANSWER = "Forty-two";
    CHECK(tributary_ask_slot(&TB) == ZXM_OK && TB.a[1].tokens == (6 + 9 + 3) / 4,
          "tributary: estimated %lld tokens", (long long) TB.a[1].tokens);

    /* bounded: TRIB_SOURCES answers, then nothing asked */
    while (TB.n < TRIB_SOURCES && tributary_ask_slot(&TB) == ZXM_OK) {
    }
    before = tb_sent;
    CHECK(TB.n == TRIB_SOURCES && tributary_ask_slot(&TB) == ZXM_E_STATE && tb_sent == before,
          "tributary: %u answers kept, then nothing asked", TB.n);

    TB_POWER = TB_PLAY = TB_PEER = NULL;
    STATE = was_state;
    MODEL = "{}";
    ANSWER = "ok";
}
