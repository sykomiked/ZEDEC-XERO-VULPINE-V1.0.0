/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_swarm_economy.c — known-answer tests for the emotional economy, the
 * market, the witness, the triple ledger and the fractal reserve. Every
 * expected value is worked by hand from the rules in the headers. */
#include <stdio.h>
#include "swarm_budget.h"
#include "swarm_emotion.h"
#include "swarm_market.h"
#include "swarm_ledger.h"
#include "swarm_reserve.h"
#include "zcapital.h"

_Static_assert((int)SWARM_CAP_FINANCIAL == (int)ZCAP_FINANCIAL, "cap order");
_Static_assert((int)SWARM_CAP_MANUFACTURED == (int)ZCAP_MANUFACTURED, "cap order");
_Static_assert((int)SWARM_CAP_INTELLECTUAL == (int)ZCAP_INTELLECTUAL, "cap order");
_Static_assert((int)SWARM_CAP_HUMAN == (int)ZCAP_HUMAN, "cap order");
_Static_assert((int)SWARM_CAP_SOCIAL == (int)ZCAP_SOCIAL, "cap order");
_Static_assert((int)SWARM_CAP_NATURAL == (int)ZCAP_NATURAL, "cap order");
_Static_assert((int)SWARM_CAP_CULTURAL == (int)ZCAP_CULTURAL, "cap order");
_Static_assert((int)SWARM_CAP_SPIRITUAL == (int)ZCAP_SPIRITUAL, "cap order");
_Static_assert((int)SWARM_CAP_SYSTEM == (int)ZCAP_SYSTEM, "cap order");
_Static_assert((int)SWARM_CAP_COUNT == ZCAP_FORM_COUNT, "cap count");

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } } while (0)

static const swarm_slot_t *slot(const swarm_budget_t *b, uint32_t id) {
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].model_id == id) return &b->slots[i];
    return 0;
}

static uint64_t fin(const swarm_market_t *m, uint32_t id) {
    return swarm_market_trader(m, id)->cap[SWARM_CAP_FINANCIAL];
}

static swarm_feeling_t feel(swarm_emotion_t e, uint8_t k) {
    swarm_feeling_t f = { e, k };
    return f;
}

/* L = 2, T = 2100: model 1 conducts, models 2 and 3 work level 1. */
static void three_models(swarm_budget_t *b) {
    swarm_budget_init(b, 2, 2100);
    swarm_budget_register(b, 1, 0);
    swarm_budget_register(b, 2, 1);
    swarm_budget_register(b, 3, 1);
}

static void test_muldiv(void) {
    uint64_t r = 7;
    CHECK(swarm_muldiv(10, 10, 3, &r) == 33 && r == 1, "100 / 3 = 33 r 1");
    CHECK(swarm_muldiv((uint64_t)1 << 40, (uint64_t)1 << 40, (uint64_t)1 << 30, &r) ==
          (uint64_t)1 << 50 && r == 0, "2^80 / 2^30 through 128 bits");
    CHECK(swarm_muldiv(UINT64_MAX, UINT64_MAX, UINT64_MAX, &r) == UINT64_MAX && r == 0,
          "(2^64-1)^2 / (2^64-1)");
    CHECK(swarm_muldiv((uint64_t)1 << 62, 1000, 3, 0) == UINT64_MAX, "overflow reported");
}

static void test_emotion_palette(void) {
    const uint64_t charge[6] = {0, 1, 2, 3, 5, 8};
    for (uint8_t k = 0; k <= 5; k++) CHECK(swarm_emotion_charge(k) == charge[k], "Fibonacci charge");
    CHECK(swarm_emotion_charge(6) == 0, "intensity out of range");
    CHECK(swarm_feeling_charge(feel(SWARM_EMO_NEUTRAL, 5)) == 0, "neutral has no charge");
    swarm_emotion_t e;
    CHECK(swarm_emotion_from_codepoint(0x1F914u, &e) == SWARM_OK && e == SWARM_EMO_CURIOUS, "🤔 is curious");
    CHECK(swarm_emotion_from_codepoint(0x1F61Fu, &e) == SWARM_OK && e == SWARM_EMO_WORRY, "😟 is worry");
    CHECK(swarm_emotion_from_codepoint(0x1F600u, &e) == SWARM_ERR_ARG, "😀 is not in the palette");
    CHECK(swarm_emotion_profile(SWARM_EMO_WORRY)->valence == -1, "worry is GLUT minus");
    CHECK(swarm_emotion_profile(SWARM_EMO_WORRY)->verify_passes == 3, "worry checks thrice");
    CHECK(swarm_emotion_profile(SWARM_EMO_JOY)->valence == 1, "joy is GLUT plus");
    CHECK(swarm_emotion_profile(SWARM_EMO_COUNT) == 0, "profile out of range");
    CHECK(swarm_emotion_imag_pool(2100, feel(SWARM_EMO_JOY, 5)) == 800, "E1 8/21 of 2100");
    CHECK(swarm_emotion_imag_pool(2100, feel(SWARM_EMO_JOY, 1)) == 100, "E1 1/21 of 2100");
}

static void test_emotion_cycle(void) {
    /* Mood joy 5 -> imaginary 800, real 1300. Real: 1300 * 2/3 = 866 r 2 and
     * 1300 / 3 = 433 r 1 -> 867, 433; level 1: 217, 216.
     * Imaginary by charge 0 : 3 : 5 -> 0, 300, 500. */
    swarm_budget_t b;
    swarm_emotion_state_t s;
    three_models(&b);
    swarm_emotion_init(&s);
    swarm_emotion_set_mood(&s, feel(SWARM_EMO_JOY, 5));
    swarm_emotion_set_feeling(&s, 1, feel(SWARM_EMO_CALM, 0));
    swarm_emotion_set_feeling(&s, 2, feel(SWARM_EMO_CURIOUS, 3));
    swarm_emotion_set_feeling(&s, 3, feel(SWARM_EMO_WORRY, 4));
    CHECK(swarm_emotion_begin_cycle(&b, &s) == SWARM_OK, "emotion cycle");
    CHECK(slot(&b, 1)->allotted == 867 && slot(&b, 1)->allotted_im == 0, "model 1: 867 + 0i");
    CHECK(slot(&b, 2)->allotted == 517 && slot(&b, 2)->allotted_im == 300, "model 2: 217 + 300i");
    CHECK(slot(&b, 3)->allotted == 716 && slot(&b, 3)->allotted_im == 500, "model 3: 216 + 500i");
    CHECK(s.last_imag_pool == 800, "imaginary pool 800");

    /* E4: nobody charged -> everything stays real: 1400, 350, 350. */
    swarm_emotion_set_feeling(&s, 2, feel(SWARM_EMO_NEUTRAL, 3));
    swarm_emotion_set_feeling(&s, 3, feel(SWARM_EMO_WORRY, 0));
    swarm_emotion_begin_cycle(&b, &s);
    CHECK(slot(&b, 1)->allotted == 1400 && slot(&b, 2)->allotted == 350 &&
          slot(&b, 3)->allotted == 350, "E4 folds back to real");
    CHECK(swarm_emotion_set_mood(&s, feel(SWARM_EMO_JOY, 6)) == SWARM_ERR_ARG, "intensity 6 rejected");
}

static void test_capped_split(void) {
    uint64_t w[3] = {100, 100, 50}, out[3];
    CHECK(swarm_capped_split(1300, w, 3, 495, out) == 0, "all sold");
    CHECK(out[0] == 495 && out[1] == 495 && out[2] == 310, "two capped, third takes the rest");
    uint64_t one[3] = {100, 0, 0};
    CHECK(swarm_capped_split(1300, one, 3, 495, out) == 805, "single bidder leaves 805 unsold");
    CHECK(out[0] == 495, "single bidder capped");
}

static void test_market_cycle_and_settle(void) {
    /* Floor 2100 * 8/21 = 800, market 1300, cap 1300 * 8/21 = 495.
     * Bids 100, 100, 50 -> 495, 495, 310. Floor 800 -> 533, 267 -> 134, 133. */
    swarm_budget_t b;
    swarm_market_t m;
    three_models(&b);
    swarm_market_init(&m);
    for (uint32_t id = 1; id <= 3; id++) swarm_market_join(&m, id, 1000);
    CHECK(swarm_market_join(&m, 1, 5) == SWARM_ERR_DUPLICATE, "join once");
    CHECK(swarm_market_bid(&m, 1, 1001) == SWARM_ERR_ARG, "M5 no bidding money you lack");
    swarm_market_bid(&m, 1, 100);
    swarm_market_bid(&m, 2, 100);
    swarm_market_bid(&m, 3, 50);
    CHECK(swarm_market_begin_cycle(&b, &m, 0) == SWARM_OK, "market cycle");
    CHECK(m.last_market == 1300 && m.last_floor == 800 && m.last_unsold == 0, "floor 800, market 1300");
    CHECK(slot(&b, 1)->allotted == 1028 && slot(&b, 1)->allotted_mk == 495, "model 1: 533 + 495");
    CHECK(slot(&b, 2)->allotted == 629 && slot(&b, 2)->allotted_mk == 495, "model 2: 134 + 495");
    CHECK(slot(&b, 3)->allotted == 443 && slot(&b, 3)->allotted_mk == 310, "model 3: 133 + 310");
    CHECK(m.pot == 250 && fin(&m, 1) == 900 && fin(&m, 3) == 950, "M5 bids paid into the pot");
    CHECK(swarm_market_conserved(&m), "M8 conserved while the pot is open");

    /* M6: income 250 * 13/21 = 154, dividend 96. Value 30 : 10 ->
     * 115 r 20 and 38 r 20, tie to the lower index -> 116, 38. */
    CHECK(swarm_market_credit(&m, 1, SWARM_CAP_FINANCIAL, 5) == SWARM_ERR_ARG, "money is not credited");
    swarm_market_credit(&m, 1, SWARM_CAP_INTELLECTUAL, 30);
    swarm_market_credit(&m, 2, SWARM_CAP_HUMAN, 10);
    swarm_market_credit(&m, 3, SWARM_CAP_SOCIAL, 5);
    swarm_market_settle(&m);
    CHECK(fin(&m, 1) == 1016 && fin(&m, 2) == 938 && fin(&m, 3) == 1046, "M6 income and dividend");
    CHECK(m.pot == 0 && swarm_market_conserved(&m), "M8 conserved after settling");
    CHECK(swarm_market_trader(&m, 3)->cap[SWARM_CAP_SOCIAL] == 5, "Social kept, never paid out");
    CHECK(swarm_cap_is_crown(SWARM_CAP_SPIRITUAL) && !swarm_cap_is_crown(SWARM_CAP_SYSTEM), "M9 Crown forms");
}

static void test_single_bidder_cannot_monopolise(void) {
    /* Only model 1 bids: it may win 495; 805 falls back to the floor.
     * Floor 1605 -> 1070, 535 -> 268, 267. */
    swarm_budget_t b;
    swarm_market_t m;
    three_models(&b);
    swarm_market_init(&m);
    for (uint32_t id = 1; id <= 3; id++) swarm_market_join(&m, id, 1000);
    swarm_market_bid(&m, 1, 100);
    swarm_market_begin_cycle(&b, &m, 0);
    CHECK(slot(&b, 1)->allotted_mk == 495, "M4 capped at 8/21 of the market");
    CHECK(m.last_unsold == 805 && m.last_floor == 1605, "unsold falls back to the floor");
    CHECK(slot(&b, 1)->allotted == 1565 && slot(&b, 2)->allotted == 268 &&
          slot(&b, 3)->allotted == 267, "1565, 268, 267");
}

static void test_wealth_cap(void) {
    /* Model 1 bids everything, model 2 produces the only value: model 2 ends
     * with 2000. Cap max(3000 * 8/21 = 1142, 1000) = 1142: excess 858 ->
     * 429 each to models 1 and 3; model 3 tops out at 1142, its 287 goes
     * to model 1. Final 716, 1142, 1142. */
    swarm_budget_t b;
    swarm_market_t m;
    three_models(&b);
    swarm_market_init(&m);
    for (uint32_t id = 1; id <= 3; id++) swarm_market_join(&m, id, 1000);
    swarm_market_bid(&m, 1, 1000);
    swarm_market_begin_cycle(&b, &m, 0);
    swarm_market_credit(&m, 2, SWARM_CAP_HUMAN, 1);
    swarm_market_settle(&m);
    CHECK(fin(&m, 1) == 716 && fin(&m, 2) == 1142 && fin(&m, 3) == 1142, "M7 wealth cap");
    CHECK(swarm_market_conserved(&m), "M8 conserved through the cap");
}

static void test_market_with_emotion(void) {
    /* Imaginary 800, real 1300: floor 495, market 805, cap 306.
     * Bids 100, 100, 50 -> 306, 306, 193. Floor 495 -> 330, 165 -> 83, 82.
     * Imaginary 0, 300, 500. Totals 636, 689, 775. */
    swarm_budget_t b;
    swarm_market_t m;
    swarm_emotion_state_t s;
    three_models(&b);
    swarm_market_init(&m);
    swarm_emotion_init(&s);
    for (uint32_t id = 1; id <= 3; id++) swarm_market_join(&m, id, 1000);
    swarm_market_bid(&m, 1, 100);
    swarm_market_bid(&m, 2, 100);
    swarm_market_bid(&m, 3, 50);
    swarm_emotion_set_mood(&s, feel(SWARM_EMO_JOY, 5));
    swarm_emotion_set_feeling(&s, 2, feel(SWARM_EMO_CURIOUS, 3));
    swarm_emotion_set_feeling(&s, 3, feel(SWARM_EMO_WORRY, 4));

    swarm_budget_t pre_b = b;
    swarm_market_t pre_m = m;
    swarm_emotion_state_t pre_e = s;
    swarm_market_begin_cycle(&b, &m, &s);
    CHECK(slot(&b, 1)->allotted == 636 && slot(&b, 2)->allotted == 689 &&
          slot(&b, 3)->allotted == 775, "636, 689, 775");
    CHECK(slot(&b, 2)->allotted_mk == 306 && slot(&b, 3)->allotted_im == 500, "parts on each axis");

    /* W1: cycle 1, n = 3, offset 1 + 1 % 2 = 2: 1 <- 3, 2 <- 1, 3 <- 2. */
    CHECK(swarm_witness_for(&b, 0) == 3 && swarm_witness_for(&b, 1) == 1 &&
          swarm_witness_for(&b, 2) == 2, "W1 rotation, never self");
    swarm_witness_t w[3];
    CHECK(swarm_witness_cycle(&b, &pre_b, &pre_m, &pre_e, w) == 3, "three witness records");
    CHECK(w[0].verdict == SWARM_WIT_TRUE && w[1].verdict == SWARM_WIT_TRUE &&
          w[2].verdict == SWARM_WIT_TRUE, "W3 all agree");

    swarm_ledger_t l;
    swarm_ledger_init(&l);
    CHECK(swarm_ledger_post_cycle(&l, &b, &m, s.last_imag_pool, w, 3), "L1 an honest cycle settles");
    CHECK(l.posted == 9, "3 financial + 3 externality + 3 provenance entries");

    swarm_witness_reward(&m, w, 3);
    CHECK(swarm_market_trader(&m, 1)->social_cycle == 1 &&
          swarm_market_trader(&m, 3)->social_cycle == 1, "W4 each witness earns 1 Social");

    /* Tamper: move one token from model 3 to model 2. It still sums to T,
     * so models 2 and 3 are contested and the cycle is held. */
    swarm_budget_t bad = b;
    bad.slots[1].allotted += 1;
    bad.slots[2].allotted -= 1;
    swarm_witness_cycle(&bad, &pre_b, &pre_m, &pre_e, w);
    CHECK(w[0].verdict == SWARM_WIT_TRUE && w[1].verdict == SWARM_WIT_GLUT &&
          w[2].verdict == SWARM_WIT_GLUT, "W3 GLUT for a contested allotment");
    CHECK(!swarm_ledger_post_cycle(&l, &bad, &m, s.last_imag_pool, w, 3), "L1 a contested cycle is held");

    /* Tamper: mint one token. The cycle no longer adds up: all FALSE. */
    bad = b;
    bad.slots[0].allotted += 1;
    swarm_witness_cycle(&bad, &pre_b, &pre_m, &pre_e, w);
    CHECK(w[0].verdict == SWARM_WIT_FALSE && w[2].verdict == SWARM_WIT_FALSE, "W3 FALSE when T is broken");
    CHECK(l.settled_cycles == 1 && l.held_cycles == 1, "ledger counts");
}

static void test_witness_rotates(void) {
    swarm_budget_t b;
    three_models(&b);
    swarm_budget_begin_cycle(&b);                         /* cycle 1: offset 2 */
    swarm_budget_begin_cycle(&b);                         /* cycle 2: offset 1 */
    CHECK(swarm_witness_for(&b, 0) == 2 && swarm_witness_for(&b, 1) == 3 &&
          swarm_witness_for(&b, 2) == 1, "W1 pairing changes next cycle");
    swarm_budget_t solo;
    swarm_budget_init(&solo, 1, 10);
    swarm_budget_register(&solo, 9, 0);
    swarm_budget_begin_cycle(&solo);
    CHECK(swarm_witness_for(&solo, 0) == 0, "W1 the kernel witnesses a lone model");
}

static void test_fractal_reserve(void) {
    swarm_budget_t b;
    swarm_emotion_state_t s;
    swarm_reserve_t r;
    three_models(&b);
    swarm_emotion_init(&s);
    swarm_emotion_set_feeling(&s, 1, feel(SWARM_EMO_JOY, 2));      /* charge 2 */
    swarm_emotion_set_feeling(&s, 2, feel(SWARM_EMO_CURIOUS, 3));  /* charge 3 */
    swarm_emotion_set_feeling(&s, 3, feel(SWARM_EMO_WORRY, 4));    /* charge 5 */

    CHECK(swarm_reserve_build(&b, &s, 2, &r) == SWARM_OK, "build model 2's reserve");
    CHECK(r.num_peers == 2 && r.peer_id[0] == 2 && r.peer_id[1] == 3, "F1 own level one by one");
    CHECK(r.summary[0][SWARM_EMO_JOY] == 2, "F2 level 0 as a summary");
    CHECK(r.total == 10, "F3 full reserve: 2 + 3 + 5");
    CHECK(swarm_reserve_audit(&r, &b, &s), "F3 audit passes");
    r.summary[0][SWARM_EMO_JOY] = 3;
    CHECK(!swarm_reserve_audit(&r, &b, &s), "F3 audit catches emotion the swarm lacks");

    swarm_reserve_build(&b, &s, 1, &r);
    CHECK(r.num_peers == 1 && r.summary[1][SWARM_EMO_CURIOUS] == 3 &&
          r.summary[1][SWARM_EMO_WORRY] == 5 && r.total == 10, "the conductor's view");
    CHECK(swarm_reserve_build(&b, &s, 42, &r) == SWARM_ERR_NO_MODEL, "unknown owner");
}

int main(void) {
    printf("=== test_swarm_economy ===\n");
    test_muldiv();
    test_emotion_palette();
    test_emotion_cycle();
    test_capped_split();
    test_market_cycle_and_settle();
    test_single_bidder_cannot_monopolise();
    test_wealth_cap();
    test_market_with_emotion();
    test_witness_rotates();
    test_fractal_reserve();
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}
