/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Kitling's model against zxvd's real dispatcher (LINK): the companion
 * through zxvd's own companion group (zxvd_companion.c, H1-H6) over a
 * swarm_dna.c genome, the budget through swarm.state and talk through
 * swarm.ask (test_apps.c's stub host). test_apps.sh links neither the
 * companion group nor swarm_dna.c and swarm_denom.c, so they are compiled
 * here, once for every test that needs them; swarm_denom.c is the witness
 * of the Zeckendorf form (kitling_rules.h KR3, swarm_denom.h D3). */
#include "kitling_model.h"

#ifndef ZXV_APPS_COMPANION_SOURCES
#    define ZXV_APPS_COMPANION_SOURCES
#    include "zxvd_companion.h"
#    include "swarm_denom.h"
#    include "swarm_dna.c"
#    include "swarm_denom.c"
#    include "zxvd_companion.c"
#endif

static zxvd_companion_t KT_K;
static bool KT_HAVE;
static kitling_t KT, KT2;

static bool kt_source(zxvd_companion_t *out)
{
    if (!KT_HAVE) return false;
    *out = KT_K;
    return true;
}

/* swarm_denom.h D3 at the densest octane: the tiers that hold a token */
static int64_t kt_denom_mask(uint64_t v)
{
    swarm_wallet_t w;
    if (swarm_denom_init(&w, 1, SWARM_OCTANE_MAX) != SWARM_OK || swarm_denom_set(&w, v) != SWARM_OK)
        return -2;
    int64_t m = 0;
    for (uint32_t t = 0; t < SWARM_DENOM_TIERS; t++) {
        if (w.count[t] > 1u || (w.count[t] && t >= 63u)) return -3;
        if (w.count[t]) m |= INT64_C(1) << t;
    }
    return m;
}

#define KT_STATE(cycle, open, allotted, used, remaining, out, per)                                 \
    "{\"levels\":1,\"tokens_per_cycle\":1000,\"tick\":1,\"cycle\":" cycle                          \
    ",\"companion\":{\"cycle_open\":" open ",\"allotted\":" allotted ",\"used\":" used             \
    ",\"remaining\":" remaining ",\"exhausted\":" out ",\"max_per_answer\":" per                   \
    ",\"asks\":0,\"exhausted_asks\":0,\"quarantined\":0,\"quarantine_ok\":true}}"

APPS_TEST(kitling)
{
    /* KR3 witnessed against swarm_denom.c itself, and its display (KM2) */
    bool same = true;
    for (uint64_t v = 0; v < 2000u; v++) same &= kt_denom_mask(v) == kitling_zeck((int64_t) v);
    static const int64_t BIG[] = {KITLING_ZECK32_MAX, KITLING_ZECK32_MAX + 1, 999999999,
                                  INT64_C(10610209857723), KITLING_ZECK_MAX};
    for (uint32_t i = 0; i < sizeof BIG / sizeof BIG[0]; i++)
        same &= kt_denom_mask((uint64_t) BIG[i]) == kitling_zeck(BIG[i]);
    CHECK(same,
          "kitling: Zeckendorf form = swarm_denom.c's canonical form (D3), 0..1999 and edges");
    char text[KITLING_ZECK_TEXT];
    CHECK(kitling_zeck_text(kitling_zeck(450), text, sizeof text) == 17 &&
              !strcmp(text, "377 + 55 + 13 + 5") && kitling_zeck_text(0, text, sizeof text) == 1 &&
              !strcmp(text, "0") && kitling_zeck_text(3, text, sizeof text) == -1 &&
              kitling_zeck_text(-1, text, sizeof text) == -1 &&
              kitling_zeck_text(kitling_zeck(KITLING_ZECK_MAX), text, sizeof text) > 0 &&
              kitling_zeck_text(kitling_zeck(12), text, 4) == -1,
          "kitling: Zeckendorf text, refusals and a buffer too small: %s", text);

    /* the companion group, its source not set yet: no companion (H6) */
    CHECK(zxvd_rpc_add_group(zxvd_companion_group()) == 0, "kitling: the companion group");
    zxvd_companion_set_source(kt_source);
    KT_HAVE = false;
    kitling_init(&KT, &LINK, 600);
    CHECK(kitling_dna(&KT) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_COMPANION && !KT.have_dna &&
              KT.stage == KITLING_REFUSED,
          "kitling: no companion yet: zxvd's refusal, nothing kept");

    /* a companion of generation 5, from a real genome (swarm_dna.h D1) */
    memset(&KT_K, 0, sizeof KT_K);
    KT_K.agent_id = 1;
    KT_K.dna = swarm_dna_from_seed(UINT64_C(0x5EED5EED5EED5EED));
    KT_K.dna.parent_a = 11;
    KT_K.dna.parent_b = UINT64_MAX;
    KT_K.dna.generation = 5;
    KT_K.feeling = (swarm_feeling_t){SWARM_EMO_JOY, 4};
    KT_K.mood = (swarm_feeling_t){SWARM_EMO_CALM, 2};
    KT_K.steps = 3;
    KT_K.growths = 8;
    KT_HAVE = true;
    swarm_traits_t tr = swarm_dna_express(&KT_K.dna, 0);
    CHECK(kitling_dna(&KT) == ZXM_OK && KT.have_dna && KT.seed == UINT64_C(0x5EED5EED5EED5EED) &&
              KT.parent_a == 11 && KT.parent_b == UINT64_MAX && !KT.founder && KT.generation == 5 &&
              KT.stage == 3 && KT.grown,
          "kitling: DNA read, stage 3, grown: %s", LINK.err);
    same = true;
    for (uint32_t g = 0; g < KITLING_GENES; g++)
        same &= KT.gene[g] == KT_K.dna.gene[g] && KT.strength[g] == kitling_strength(KT.gene[g]);
    CHECK(same && KT.strength[SWARM_GENE_CURIOSITY] == tr.curiosity_permille &&
              KT.strength[SWARM_GENE_CAUTION] == tr.caution_permille &&
              KT.strength[SWARM_GENE_WARMTH] == tr.warmth_permille &&
              KT.strength[SWARM_GENE_PACE] == tr.pace_permille &&
              KT.strength[SWARM_GENE_VERBOSITY] == tr.verbosity_permille,
          "kitling: the 8 genes, their strengths = swarm_dna.c's expression (D2)");

    KT_K.dna.generation = 34;
    CHECK(kitling_generation(&KT) == ZXM_OK && KT.generation == 34 && KT.stage == 5 && KT.grown &&
              KT.steps == 3 && KT.growths == 8,
          "kitling: generation 34, the sage stage: %s", LINK.err);
    KT_K.dna.generation = 0;
    KT_K.dna.parent_a = KT_K.dna.parent_b = 0;
    CHECK(kitling_dna(&KT) == ZXM_OK && KT.founder && KT.stage == 0 && !KT.grown,
          "kitling: a founder, newborn");

    CHECK(kitling_mood(&KT) == ZXM_OK && KT.have_mood && !strcmp(KT.feeling.name, "joy") &&
              KT.feeling.emotion == SWARM_EMO_JOY && KT.feeling.intensity == 4 &&
              KT.feeling.charge == 5 && KT.feeling.valence == 1 &&
              KT.swarm.emotion == SWARM_EMO_CALM && KT.swarm.charge == 2 &&
              KT.home.emotion == (int64_t) tr.home.emotion &&
              KT.at_home == (tr.home.emotion == SWARM_EMO_JOY),
          "kitling: mood, joy at 4 (charge 5), the swarm calm: %s", LINK.err);

    /* KM3: the budget, the owner's cap, the cycle */
    int before = sent;
    CHECK(kitling_may_talk(&KT) == ZXM_E_STATE && sent == before && strstr(KT.notice, "budget"),
          "kitling: no talk before the budget is read, nothing sent");
    STATE = KT_STATE("7", "true", "450", "0", "450", "false", "256");
    ANSWER = "Hello, I am here.";
    CHECK(kitling_budget(&KT) == ZXM_OK && KT.have_budget && KT.cycle == 7 && KT.allotted == 450 &&
              KT.remaining == 450 && KT.per_answer == 256 && KT.used_x100 == 0 &&
              KT.zeck == kitling_zeck(450) && kitling_may_talk(&KT) == ZXM_OK,
          "kitling: the companion's budget: %s", LINK.err);
    asked = 0;
    CHECK(kitling_talk(&KT, "How are you today?", 1700000000000) == ZXM_OK && asked == 1 &&
              !strcmp(KT.held.text, "Hello, I am here.") && KT.held.state == ZXM_HELD_PENDING &&
              KT.spent > 0 && KT.spent == (int64_t) KT.held.cost.tokens &&
              kitling_memories(&KT) == 1 && kitling_memory_at(&KT, 0)->seq == 1 &&
              kitling_memory_at(&KT, 0)->emotion == SWARM_EMO_JOY &&
              kitling_memory_at(&KT, 0)->tokens == KT.spent && strstr(last_prompt, "How are you") &&
              !KT.have_budget,
          "kitling: a talk, its cost and its memory: %s", LINK.err);
    int64_t spent = KT.spent;
    CHECK(kitling_set_cap(&KT, spent + 255) == ZXM_OK && kitling_budget(&KT) == ZXM_OK &&
              KT.spent == spent && kitling_may_talk(&KT) == ZXM_E_STATE && strstr(KT.notice, "cap"),
          "kitling: one token short of the owner's cap: refused (%s)", KT.notice);
    asked = 0;
    CHECK(kitling_talk(&KT, "Again?", 1700000001000) == ZXM_E_STATE && asked == 0 &&
              kitling_memories(&KT) == 1,
          "kitling: a refused talk asks nothing");
    CHECK(kitling_set_cap(&KT, spent + 256) == ZXM_OK && kitling_budget(&KT) == ZXM_OK &&
              kitling_may_talk(&KT) == ZXM_OK && kitling_set_cap(&KT, 1000000000) == ZXM_E_ARG &&
              kitling_set_cap(&KT, -1) == ZXM_E_ARG,
          "kitling: exactly a whole answer inside the cap; caps outside 9 digits refused");
    kitling_set_cap(&KT, 0);
    STATE = KT_STATE("8", "true", "450", "100", "350", "false", "256");
    CHECK(kitling_budget(&KT) == ZXM_OK && KT.spent == 0 && KT.used_x100 == 2222 &&
              kitling_may_talk(&KT) == ZXM_E_STATE,
          "kitling: a new cycle starts the spending again; a cap of 0 never talks");
    kitling_set_cap(&KT, 999999999);
    STATE = KT_STATE("8", "true", "450", "195", "255", "false", "256");
    CHECK(kitling_budget(&KT) == ZXM_OK && kitling_may_talk(&KT) == ZXM_E_STATE &&
              strstr(KT.notice, "255 tokens"),
          "kitling: less than a whole answer left: refused (%s)", KT.notice);
    STATE = KT_STATE("8", "true", "450", "450", "0", "true", "256");
    CHECK(kitling_budget(&KT) == ZXM_OK && KT.used_x100 == 10000 && KT.zeck == 0 &&
              kitling_may_talk(&KT) == ZXM_E_STATE && strstr(KT.notice, "rests"),
          "kitling: exhausted: the companion rests");
    STATE = KT_STATE("8", "false", "450", "0", "450", "false", "256");
    CHECK(kitling_budget(&KT) == ZXM_OK && kitling_may_talk(&KT) == ZXM_E_STATE,
          "kitling: the cycle closed: no talk");

    /* KM1: a budget that does not add up is not kept, and the next talk needs a fresh one */
    STATE = KT_STATE("8", "true", "450", "1", "450", "false", "256");
    CHECK(kitling_budget(&KT) == ZXM_E_PROTOCOL && !KT.have_budget &&
              kitling_may_talk(&KT) == ZXM_E_STATE,
          "kitling: used + remaining past allotted: refused");
    STATE = "{\"cycle\":8}";
    CHECK(kitling_budget(&KT) == ZXM_E_PROTOCOL && !KT.have_budget, "kitling: no companion budget");

    /* KM4: the memory fills, refuses, and makes room only on the owner's word */
    STATE = KT_STATE("9", "true", "999999", "0", "999999", "false", "256");
    bool all = true;
    while (kitling_memories(&KT) < KITLING_MEMORY && all)
        all = kitling_talk(&KT, "Tell me a story.", 1700000002000) == ZXM_OK;
    CHECK(all && kitling_memories(&KT) == KITLING_MEMORY, "kitling: 64 memories kept");
    asked = 0;
    CHECK(kitling_talk(&KT, "One more?", 1700000003000) == ZXM_E_STATE && asked == 0 &&
              strstr(KT.notice, "memory is full"),
          "kitling: a full memory refuses a talk before asking");
    CHECK(kitling_forget(&KT, 1, false) == ZXM_E_STATE && kitling_memories(&KT) == KITLING_MEMORY &&
              kitling_forget(&KT, KITLING_MEMORY + 1, true) == ZXM_E_ARG &&
              kitling_forget(&KT, 1, true) == ZXM_OK &&
              kitling_memories(&KT) == KITLING_MEMORY - 1 && kitling_memory_at(&KT, 0)->seq == 2,
          "kitling: forgetting waits for the owner's word");
    CHECK(kitling_talk(&KT, "One more?", 1700000003000) == ZXM_OK &&
              kitling_memory_at(&KT, KITLING_MEMORY - 1)->seq == 65 &&
              kitling_memory_at(&KT, KITLING_MEMORY - 1)->stamp == 1700000003000 &&
              !kitling_memory_at(&KT, KITLING_MEMORY),
          "kitling: room made, the talk kept as memory 65");
    all = true;
    for (uint32_t i = 0; i < kitling_memories(&KT); i++) {
        const kitling_memory_t *m = kitling_memory_at(&KT, i);
        all &= kitling_memory_ok(m->seq, m->stamp, m->emotion, m->intensity, m->tokens);
    }
    CHECK(all, "kitling: every memory fits its record (KR5)");
    CHECK(kitling_talk(&KT, "", 1) == ZXM_E_ARG && kitling_talk(&KT, "x", -1) == ZXM_E_STATE,
          "kitling: an empty prompt refused; a full memory first");
    kitling_forget(&KT, 1, true);
    CHECK(kitling_talk(&KT, "x", -1) == ZXM_E_ARG && kitling_talk(&KT, "x", INT64_MAX) == ZXM_E_ARG,
          "kitling: a moment that cannot be remembered is refused");

    /* KM1: replies that are not the companion's leave the model as it was */
    kitling_init(&KT2, &FAKE, 10);
    memcpy(&KT2.gene, KT.gene, sizeof KT.gene);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"seed\":\"1\",\"genes\":{\"emotion\":1,"
             "\"temperament\":2,\"curiosity\":256,\"caution\":4,\"warmth\":5,\"pace\":6,"
             "\"verbosity\":7,\"specialty\":8},\"traits\":{},\"parent_a\":\"0\",\"parent_b\":\"0\","
             "\"founder\":true,\"generation\":1}}";
    CHECK(kitling_dna(&KT2) == ZXM_E_PROTOCOL && !KT2.have_dna && KT2.stage == KITLING_REFUSED,
          "kitling: a gene that is no byte: refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"seed\":\"1\",\"genes\":{\"emotion\":1,"
             "\"temperament\":2,\"curiosity\":3,\"caution\":4,\"warmth\":5,\"pace\":6,"
             "\"verbosity\":7,\"specialty\":8},\"traits\":{\"curiosity_permille\":13,"
             "\"caution_permille\":16,\"warmth_permille\":20,\"pace_permille\":24,"
             "\"verbosity_permille\":27},\"parent_a\":\"0\",\"parent_b\":\"0\",\"founder\":true,"
             "\"generation\":1}}";
    CHECK(kitling_dna(&KT2) == ZXM_E_PROTOCOL && !KT2.have_dna,
          "kitling: a trait that does not match its gene (12 permille, not 13): refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"seed\":\"18446744073709551616\"}}";
    CHECK(kitling_dna(&KT2) == ZXM_E_PROTOCOL, "kitling: a seed past 64 bits: refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"generation\":4294967296,\"steps\":0,"
             "\"growths\":0}}";
    CHECK(kitling_generation(&KT2) == ZXM_E_PROTOCOL && KT2.stage == KITLING_REFUSED,
          "kitling: a generation past 32 bits: refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"feeling\":{\"emotion\":\"joy\","
             "\"intensity\":4,\"charge\":8,\"valence\":1},\"home\":{\"emotion\":\"joy\","
             "\"intensity\":4,\"charge\":5,\"valence\":1},\"at_home\":true,\"swarm_mood\":{"
             "\"emotion\":\"neutral\",\"intensity\":0,\"charge\":0,\"valence\":0}}}";
    CHECK(kitling_mood(&KT2) == ZXM_E_PROTOCOL && !KT2.have_mood,
          "kitling: a charge that is not its intensity's Fibonacci charge: refused");
    CANNED = NULL;
    CHECK(kitling_budget(&KT2) == ZXM_E_TRANSPORT && !KT2.have_budget &&
              kitling_talk(&KT2, "hi", 1) == ZXM_E_TRANSPORT,
          "kitling: no connection: nothing read, nothing asked");
    CHECK(kitling_dna(NULL) == ZXM_E_ARG && kitling_may_talk(NULL) == ZXM_E_ARG &&
              kitling_memories(NULL) == 0 && !kitling_memory_at(NULL, 0),
          "kitling: no model");

    zxvd_companion_set_source(NULL);
    zxvd_rpc_remove_group(zxvd_companion_group());
    STATE = "{}";
    ANSWER = "ok";
}
