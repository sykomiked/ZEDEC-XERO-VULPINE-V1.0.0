/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_companion.c — companion.dna, companion.generation and companion.mood:
 * the Chiglet companion zxvd's swarm holds, behind the "companion" feature
 * (not regulated, every method `always`). The rules are in zxvd_companion.h
 * (H1-H6); the DNA in kernel/src/swarm/swarm_dna.h (D1-D5), its steps in
 * swarm_evolve.h (V7) and the feelings in swarm_emotion.h. Integers only.
 */
#include <string.h>

#include "zxvd_groups.h"
#include "zxvd_companion.h"

static bool (*SOURCE)(zxvd_companion_t *out);

void zxvd_companion_set_source(bool (*fn)(zxvd_companion_t *out))
{
    SOURCE = fn;
}

static const char *const GENE[SWARM_DNA_GENES] = {
    "emotion", "temperament", "curiosity", "caution", "warmth", "pace", "verbosity", "specialty"};
static const char *const EMOTION[SWARM_EMO_COUNT] = {"neutral", "calm",  "joy",         "curious",
                                                     "love",    "worry", "frustration", "sadness"};

/* H2, H6: a fresh snapshot, or the call fails */
static bool snap(zxvd_call_t *c, zxvd_companion_t *k)
{
    memset(k, 0, sizeof *k);
    if (!SOURCE || !SOURCE(k)) {
        zxvd_fail(c, ZXVD_E_COMPANION, "no companion yet: the swarm has no agents");
        return false;
    }
    /* an emotion outside the palette reads as neutral */
    if ((uint32_t) k->feeling.emotion >= SWARM_EMO_COUNT) k->feeling.emotion = SWARM_EMO_NEUTRAL;
    if ((uint32_t) k->mood.emotion >= SWARM_EMO_COUNT) k->mood.emotion = SWARM_EMO_NEUTRAL;
    return true;
}

static void put_u64(zxvd_buf_t *b, const char *key, uint64_t v)
{
    zxvd_buf_printf(b, "\"%s\":\"%llu\"", key, (unsigned long long) v);
}

/* H5: one feeling (in the palette: snap checked it) */
static void put_feeling(zxvd_buf_t *b, swarm_feeling_t f)
{
    const swarm_emotion_profile_t *p = swarm_emotion_profile(f.emotion);
    uint32_t cp = p->codepoint; /* the palette is 4-byte UTF-8 (U+1F600..U+1F9FF) */
    char e[5] = {(char) (0xF0u | (cp >> 18)), (char) (0x80u | ((cp >> 12) & 0x3Fu)),
                 (char) (0x80u | ((cp >> 6) & 0x3Fu)), (char) (0x80u | (cp & 0x3Fu)), 0};
    zxvd_buf_printf(b,
                    "{\"emotion\":\"%s\",\"emoji\":\"%s\",\"codepoint\":%u,\"intensity\":%u,"
                    "\"charge\":%u,\"valence\":%d}",
                    EMOTION[f.emotion], e, (unsigned) cp, (unsigned) f.intensity,
                    (unsigned) swarm_feeling_charge(f), (int) p->valence);
}

/* H3 */
static void m_dna(zxvd_call_t *c)
{
    zxvd_companion_t k;
    if (!snap(c, &k)) return;
    zxvd_buf_t *b = zxvd_res(c);
    swarm_traits_t t = swarm_dna_express(&k.dna, k.num_specialties);
    zxvd_buf_printf(b, "{\"agent_id\":%u,", (unsigned) k.agent_id);
    put_u64(b, "seed", k.dna.seed);
    zxvd_buf_printf(b, ",\"genes\":{");
    for (uint32_t g = 0; g < SWARM_DNA_GENES; g++)
        zxvd_buf_printf(b, "%s\"%s\":%u", g ? "," : "", GENE[g], (unsigned) k.dna.gene[g]);
    zxvd_buf_printf(b, "},\"traits\":{\"home\":");
    put_feeling(b, t.home);
    zxvd_buf_printf(b,
                    ",\"curiosity_permille\":%u,\"caution_permille\":%u,\"warmth_permille\":%u,"
                    "\"pace_permille\":%u,\"verbosity_permille\":%u,\"specialty\":%u,"
                    "\"specialty_name\":",
                    (unsigned) t.curiosity_permille, (unsigned) t.caution_permille,
                    (unsigned) t.warmth_permille, (unsigned) t.pace_permille,
                    (unsigned) t.verbosity_permille, (unsigned) t.specialty);
    if (k.specialty && k.num_specialties)
        zxvd_buf_json_str(b, k.specialty[t.specialty]);
    else
        zxvd_buf_printf(b, "null");
    zxvd_buf_printf(b, "},");
    put_u64(b, "parent_a", k.dna.parent_a);
    zxvd_buf_printf(b, ",");
    put_u64(b, "parent_b", k.dna.parent_b);
    zxvd_buf_printf(b, ",\"founder\":%s,\"generation\":%u}",
                    k.dna.parent_a == 0 && k.dna.parent_b == 0 ? "true" : "false",
                    (unsigned) k.dna.generation);
}

/* H4 */
static void m_generation(zxvd_call_t *c)
{
    zxvd_companion_t k;
    if (!snap(c, &k)) return;
    zxvd_buf_t *b = zxvd_res(c);
    zxvd_buf_printf(b, "{\"agent_id\":%u,\"generation\":%u,", (unsigned) k.agent_id,
                    (unsigned) k.dna.generation);
    put_u64(b, "seed", k.dna.seed);
    zxvd_buf_printf(b, ",\"steps\":%llu,\"growths\":%llu,\"last_step\":%llu}",
                    (unsigned long long) k.steps, (unsigned long long) k.growths,
                    (unsigned long long) k.last_step);
}

/* H5 */
static void m_mood(zxvd_call_t *c)
{
    zxvd_companion_t k;
    if (!snap(c, &k)) return;
    zxvd_buf_t *b = zxvd_res(c);
    swarm_feeling_t home = swarm_dna_express(&k.dna, k.num_specialties).home;
    zxvd_buf_printf(b, "{\"agent_id\":%u,\"feeling\":", (unsigned) k.agent_id);
    put_feeling(b, k.feeling);
    zxvd_buf_printf(b, ",\"home\":");
    put_feeling(b, home);
    zxvd_buf_printf(
        b, ",\"at_home\":%s,\"swarm_mood\":", k.feeling.emotion == home.emotion ? "true" : "false");
    put_feeling(b, k.mood);
    zxvd_buf_printf(b, "}");
}

static const zxvd_group_method_t METHODS[] = {
    {"companion.dna", m_dna, true},
    {"companion.generation", m_generation, true},
    {"companion.mood", m_mood, true},
};

static const zxvd_group_t GROUP = {
    "companion",
    false,
    "the Chiglet companion: its DNA, generation and mood, read from the swarm (moves no money)",
    METHODS,
    sizeof METHODS / sizeof METHODS[0],
    NULL};

const zxvd_group_t *zxvd_companion_group(void)
{
    return &GROUP;
}
