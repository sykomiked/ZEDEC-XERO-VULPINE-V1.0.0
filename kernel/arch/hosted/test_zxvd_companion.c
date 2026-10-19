/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_companion.c — zxvd_companion.c (companion.*) behind the real RPC
 * layer on the real swarm DNA and emotion code, and the companion's step
 * (swarm_evolve.h V7). Replies are compared exactly with literal JSON. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_groups.h"
#include "zxvd_companion.h"
#include "swarm_evolve.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[1000], WANT[4000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.900s\n    got  %.900s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *req(const char *method)
{
    snprintf(REQB, sizeof REQB, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":{}}",
             method);
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *NONE_ERR = "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32070,"
                              "\"message\":\"no companion yet: the swarm has no agents\"}}";

/* ----- the stand-in host ----- */

static zxvd_companion_t K; /* what the source hands out */
static bool HAVE;

static bool source(zxvd_companion_t *out)
{
    if (!HAVE) return false;
    *out = K;
    return true;
}

static const char *const SPEC[] = {"A", "B", "C\"q"};

static void fixture(void)
{
    memset(&K, 0, sizeof K);
    K.agent_id = 7;
    K.dna.seed = UINT64_MAX;
    static const uint8_t g[SWARM_DNA_GENES] = {10, 7, 0, 255, 128, 1, 254, 20};
    memcpy(K.dna.gene, g, sizeof g);
    K.dna.parent_a = 5;
    K.dna.parent_b = 0;
    K.dna.generation = 3;
    K.feeling = (swarm_feeling_t){SWARM_EMO_WORRY, 4};
    K.mood = (swarm_feeling_t){SWARM_EMO_LOVE, 5};
    K.specialty = SPEC;
    K.num_specialties = 3;
    K.steps = 2;
    K.growths = 9;
    K.last_step = 8;
    HAVE = true;
}

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_companion_group()) == 0, "add the companion group");
    zxvd_companion_set_source(source);
    fixture();
}

/* ----- tests ----- */

static void test_group(void)
{
    fresh();
    const zxvd_group_t *g = zxvd_companion_group();
    CHECK(!g->regulated && !strcmp(g->feature, "companion") && g->n_methods == 3 && !g->close,
          "group shape");
    for (uint32_t i = 0; i < g->n_methods; i++) CHECK(g->methods[i].always, "method %u always", i);
    const char *d = rpc(req("rpc.discover"));
    CHECK(strstr(d, "\"web4.card_verify\",\"companion.dna\",\"companion.generation\","
                    "\"companion.mood\"]") != NULL,
          "discover lists the companion methods: %.300s", d);
    EXPECT(req("features.get"),
           ok("{\"features\":[{\"name\":\"companion\",\"enabled\":false,\"regulated\":false,"
              "\"groups\":[{\"note\":\"the Chiglet companion: its DNA, generation and mood, read "
              "from the swarm (moves no money)\",\"methods\":3}]}],\"saved\":false}"));
    /* a second add of the same group is a no-op, a clash with it is refused */
    CHECK(zxvd_rpc_add_group(g) == 0, "re-adding is a no-op");
    static const zxvd_group_method_t CLASH[] = {{"companion.mood", NULL, true}};
    static const zxvd_group_t OTHER = {"other", false, "x", CLASH, 1, NULL};
    CHECK(zxvd_rpc_add_group(&OTHER) == -1, "a clashing name is refused");
}

static void test_no_companion(void)
{
    fresh();
    zxvd_companion_set_source(NULL);
    EXPECT(req("companion.dna"), NONE_ERR);
    EXPECT(req("companion.generation"), NONE_ERR);
    EXPECT(req("companion.mood"), NONE_ERR);
    zxvd_companion_set_source(source);
    HAVE = false;
    EXPECT(req("companion.dna"), NONE_ERR);
    EXPECT(req("companion.generation"), NONE_ERR);
    EXPECT(req("companion.mood"), NONE_ERR);
}

static void test_dna(void)
{
    fresh(); /* the feature is off: `always` methods answer */
    EXPECT(req("companion.dna"),
           ok("{\"agent_id\":7,\"seed\":\"18446744073709551615\",\"genes\":{\"emotion\":10,"
              "\"temperament\":7,\"curiosity\":0,\"caution\":255,\"warmth\":128,\"pace\":1,"
              "\"verbosity\":254,\"specialty\":20},\"traits\":{\"home\":{\"emotion\":\"joy\","
              "\"emoji\":\"\xF0\x9F\x98\x84\",\"codepoint\":128516,\"intensity\":1,\"charge\":1,"
              "\"valence\":1},\"curiosity_permille\":0,\"caution_permille\":1000,"
              "\"warmth_permille\":502,\"pace_permille\":4,\"verbosity_permille\":996,"
              "\"specialty\":2,\"specialty_name\":\"C\\\"q\"},\"parent_a\":\"5\","
              "\"parent_b\":\"0\",\"founder\":false,\"generation\":3}"));
    /* no specialty table: the index from D2's rule with none, and null */
    K.num_specialties = 0;
    const char *r = rpc(req("companion.dna"));
    CHECK(strstr(r, "\"specialty\":0,\"specialty_name\":null}") != NULL, "no table: %s", r);
    K.num_specialties = 3;
    K.specialty = NULL;
    r = rpc(req("companion.dna"));
    CHECK(strstr(r, "\"specialty\":2,\"specialty_name\":null}") != NULL, "NULL table: %s", r);
    /* founder only with both parents 0; parent_b alone also makes a child */
    K.dna.parent_a = 0;
    r = rpc(req("companion.dna"));
    CHECK(strstr(r, "\"parent_a\":\"0\",\"parent_b\":\"0\",\"founder\":true,\"generation\":3}"),
          "founder: %s", r);
    K.dna.parent_b = 12345678901234567890ull;
    r = rpc(req("companion.dna"));
    CHECK(strstr(r, "\"parent_a\":\"0\",\"parent_b\":\"12345678901234567890\",\"founder\":false"),
          "parent_b: %s", r);
    /* a founder grown from a seed (D1), as the host's agent 0 starts */
    K.dna = swarm_dna_from_seed(1);
    K.agent_id = 1;
    K.specialty = SPEC;
    r = rpc(req("companion.dna"));
    char want[200];
    snprintf(want, sizeof want, "{\"agent_id\":1,\"seed\":\"1\",\"genes\":{\"emotion\":%u,",
             K.dna.gene[0]);
    CHECK(strstr(r, want) && strstr(r, "\"founder\":true,\"generation\":0}"), "from seed: %s", r);
}

static void test_generation(void)
{
    fresh();
    EXPECT(req("companion.generation"),
           ok("{\"agent_id\":7,\"generation\":3,\"seed\":\"18446744073709551615\",\"steps\":2,"
              "\"growths\":9,\"last_step\":8}"));
    K.dna.generation = 4000000000u;
    K.dna.seed = 0;
    K.steps = 10000000000ull;
    K.growths = 0;
    K.last_step = 0;
    EXPECT(req("companion.generation"),
           ok("{\"agent_id\":7,\"generation\":4000000000,\"seed\":\"0\",\"steps\":10000000000,"
              "\"growths\":0,\"last_step\":0}"));
}

/* every emotion of the palette as JSON, at a given intensity */
static const char *const EMO_JSON[SWARM_EMO_COUNT] = {
    "{\"emotion\":\"neutral\",\"emoji\":\"\xF0\x9F\x98\x90\",\"codepoint\":128528",
    "{\"emotion\":\"calm\",\"emoji\":\"\xF0\x9F\x98\x8C\",\"codepoint\":128524",
    "{\"emotion\":\"joy\",\"emoji\":\"\xF0\x9F\x98\x84\",\"codepoint\":128516",
    "{\"emotion\":\"curious\",\"emoji\":\"\xF0\x9F\xA4\x94\",\"codepoint\":129300",
    "{\"emotion\":\"love\",\"emoji\":\"\xF0\x9F\xA5\xB0\",\"codepoint\":129392",
    "{\"emotion\":\"worry\",\"emoji\":\"\xF0\x9F\x98\x9F\",\"codepoint\":128543",
    "{\"emotion\":\"frustration\",\"emoji\":\"\xF0\x9F\x98\xA0\",\"codepoint\":128544",
    "{\"emotion\":\"sadness\",\"emoji\":\"\xF0\x9F\x98\xA2\",\"codepoint\":128546",
};
static const int VALENCE[SWARM_EMO_COUNT] = {0, 1, 1, 1, 1, -1, -1, -1};

static void test_mood(void)
{
    fresh();
    /* home from DNA: genes 10 and 7 -> joy, intensity 1 */
    EXPECT(req("companion.mood"),
           ok("{\"agent_id\":7,\"feeling\":%s,\"intensity\":4,\"charge\":5,\"valence\":-1},"
              "\"home\":%s,\"intensity\":1,\"charge\":1,\"valence\":1},\"at_home\":false,"
              "\"swarm_mood\":%s,\"intensity\":5,\"charge\":8,\"valence\":1}}",
              EMO_JSON[5], EMO_JSON[2], EMO_JSON[4]));
    /* at home: the same emotion as home, whatever the intensity */
    K.feeling = (swarm_feeling_t){SWARM_EMO_JOY, 3};
    const char *r = rpc(req("companion.mood"));
    CHECK(strstr(r, "\"at_home\":true") != NULL, "at home: %s", r);
    /* each emotion, each intensity: emoji, Fibonacci charge (neutral 0), valence */
    static const unsigned CHARGE[] = {0, 1, 2, 3, 5, 8, 0};
    for (uint32_t e = 0; e < SWARM_EMO_COUNT; e++)
        for (uint8_t k = 0; k <= 6; k++) {
            K.feeling = (swarm_feeling_t){(swarm_emotion_t) e, k};
            K.mood = (swarm_feeling_t){(swarm_emotion_t) (SWARM_EMO_COUNT - 1u - e), k};
            char want[600];
            snprintf(want, sizeof want,
                     "{\"agent_id\":7,\"feeling\":%s,\"intensity\":%u,\"charge\":%u,"
                     "\"valence\":%d},\"home\":%s,\"intensity\":1,\"charge\":1,\"valence\":1},"
                     "\"at_home\":%s,\"swarm_mood\":%s,\"intensity\":%u,\"charge\":%u,"
                     "\"valence\":%d}}",
                     EMO_JSON[e], (unsigned) k, e ? CHARGE[k] : 0u, VALENCE[e], EMO_JSON[2],
                     e == 2 ? "true" : "false", EMO_JSON[SWARM_EMO_COUNT - 1u - e], (unsigned) k,
                     (SWARM_EMO_COUNT - 1u - e) ? CHARGE[k] : 0u,
                     VALENCE[SWARM_EMO_COUNT - 1u - e]);
            EXPECT(req("companion.mood"), ok("%s", want));
        }
    /* an emotion outside the palette reads as neutral, in feeling and mood */
    K.feeling = (swarm_feeling_t){SWARM_EMO_COUNT, 2};
    K.mood = (swarm_feeling_t){(swarm_emotion_t) 200, 3};
    EXPECT(req("companion.mood"),
           ok("{\"agent_id\":7,\"feeling\":%s,\"intensity\":2,\"charge\":0,\"valence\":0},"
              "\"home\":%s,\"intensity\":1,\"charge\":1,\"valence\":1},\"at_home\":false,"
              "\"swarm_mood\":%s,\"intensity\":3,\"charge\":0,\"valence\":0}}",
              EMO_JSON[0], EMO_JSON[2], EMO_JSON[0]));
    K.feeling = (swarm_feeling_t){(swarm_emotion_t) 200, 1};
    K.mood = (swarm_feeling_t){SWARM_EMO_COUNT, 4};
    EXPECT(req("companion.mood"),
           ok("{\"agent_id\":7,\"feeling\":%s,\"intensity\":1,\"charge\":0,\"valence\":0},"
              "\"home\":%s,\"intensity\":1,\"charge\":1,\"valence\":1},\"at_home\":false,"
              "\"swarm_mood\":%s,\"intensity\":4,\"charge\":0,\"valence\":0}}",
              EMO_JSON[0], EMO_JSON[2], EMO_JSON[0]));
    /* a home of neutral: an out-of-palette feeling is at home */
    K.dna.gene[SWARM_GENE_EMOTION] = 8;
    r = rpc(req("companion.mood"));
    CHECK(strstr(r, "\"at_home\":true") != NULL, "neutral home: %s", r);
}

static bool same_dna(const swarm_dna_t *a, const swarm_dna_t *b)
{
    return a->seed == b->seed && !memcmp(a->gene, b->gene, SWARM_DNA_GENES) &&
           a->parent_a == b->parent_a && a->parent_b == b->parent_b &&
           a->generation == b->generation;
}

/* swarm_evolve.h V7, and its effect on companion.generation */
static void test_step(void)
{
    swarm_dna_t c = swarm_dna_from_seed(11), d = swarm_dna_from_seed(22), before = c;
    CHECK(!swarm_evo_companion(NULL, 0, &d, 9, 1), "no companion");
    CHECK(!swarm_evo_companion(&c, 0, NULL, 9, 1), "no donor");
    CHECK(!swarm_evo_companion(&c, 9, &d, 9, 1) && same_dna(&c, &before), "a tie is no step");
    CHECK(!swarm_evo_companion(&c, 9, &d, 8, 1) && same_dna(&c, &before), "a less fit donor");
    CHECK(!swarm_evo_companion(&c, 1ull << 40, &d, 1ull << 41, 1) && same_dna(&c, &before),
          "both above UINT32_MAX count as UINT32_MAX: a tie");
    CHECK(!swarm_evo_companion(&c, UINT32_MAX, &d, 1ull << 41, 1) && same_dna(&c, &before),
          "UINT32_MAX against more: a tie");
    swarm_dna_t want = swarm_dna_combine(&before, &d, 1);
    CHECK(swarm_evo_companion(&c, UINT32_MAX - 1u, &d, 1ull << 40, 1) && same_dna(&c, &want),
          "a fitter donor: the child");
    CHECK(c.parent_a == 11 && c.parent_b == 22 && c.generation == 1 && c.seed != 11,
          "D5: parents and generation 1");
    /* the generation is the larger parent's + 1 */
    d.generation = 5;
    swarm_dna_t c2 = c;
    want = swarm_dna_combine(&c2, &d, 77);
    CHECK(swarm_evo_companion(&c2, 0, &d, 1, 77) && same_dna(&c2, &want) && c2.generation == 6,
          "max(1, 5) + 1 = 6");
    swarm_dna_t c3 = swarm_dna_from_seed(33);
    c3.generation = 9;
    CHECK(swarm_evo_companion(&c3, 0, &d, 1, 2) && c3.generation == 10, "max(9, 5) + 1 = 10");
    /* the nonce makes repeated pairings differ */
    swarm_dna_t x = before, y = before;
    CHECK(swarm_evo_companion(&x, 0, &d, 1, 1) && swarm_evo_companion(&y, 0, &d, 1, 2) &&
              x.seed != y.seed,
          "nonce");

    /* a host that steps its companion: companion.generation follows */
    fresh();
    K.dna = swarm_dna_from_seed(11);
    K.steps = K.growths = K.last_step = 0;
    EXPECT(req("companion.generation"),
           ok("{\"agent_id\":7,\"generation\":0,\"seed\":\"11\",\"steps\":0,\"growths\":0,"
              "\"last_step\":0}"));
    K.growths = 1;
    if (swarm_evo_companion(&K.dna, 0, &d, 3, K.growths)) {
        K.steps++;
        K.last_step = K.growths;
    }
    char w[300];
    snprintf(w, sizeof w,
             "{\"agent_id\":7,\"generation\":6,\"seed\":\"%llu\",\"steps\":1,\"growths\":1,"
             "\"last_step\":1}",
             (unsigned long long) K.dna.seed);
    EXPECT(req("companion.generation"), ok("%s", w));
    const char *r = rpc(req("companion.dna"));
    CHECK(strstr(r, "\"parent_a\":\"11\",\"parent_b\":\"22\",\"founder\":false,\"generation\":6}"),
          "companion.dna shows the lineage: %s", r);
}

int main(void)
{
    test_group();
    test_no_companion();
    test_dna();
    test_generation();
    test_mood();
    test_step();
    zxvd_rpc_groups_reset();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_companion: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_companion: %d checks\n", checks);
    return 0;
}
