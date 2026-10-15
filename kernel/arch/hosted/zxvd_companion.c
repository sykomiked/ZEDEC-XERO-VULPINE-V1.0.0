/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_companion.c — companion.dna, companion.generation and companion.mood:
 * the Chiglet companion zxvd's swarm holds, and companion.name,
 * companion.name_preview and companion.name_confirm: its name, given once.
 * Behind the "companion" feature (not regulated, every method `always`). The
 * rules are in zxvd_companion.h (H1-H11); the DNA in kernel/src/swarm/
 * swarm_dna.h (D1-D5), its steps in swarm_evolve.h (V7), the feelings in
 * swarm_emotion.h and the name in kernel/src/chiglet/chg_name.h (N1-N7).
 * Integers only.
 */
#include <string.h>

#include "zxvd_groups.h"
#include "zxvd_companion.h"

static bool (*SOURCE)(zxvd_companion_t *out);
static const zxvd_names_t *NAMES;

void zxvd_companion_set_source(bool (*fn)(zxvd_companion_t *out))
{
    SOURCE = fn;
}

void zxvd_companion_set_names(const zxvd_names_t *names)
{
    NAMES = names;
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

/* H3, H9: the seed, the genes by name and the expressed traits */
static void put_genes(zxvd_buf_t *b, const swarm_dna_t *dna, const zxvd_companion_t *k)
{
    swarm_traits_t t = swarm_dna_express(dna, k->num_specialties);
    put_u64(b, "seed", dna->seed);
    zxvd_buf_printf(b, ",\"genes\":{");
    for (uint32_t g = 0; g < SWARM_DNA_GENES; g++)
        zxvd_buf_printf(b, "%s\"%s\":%u", g ? "," : "", GENE[g], (unsigned) dna->gene[g]);
    zxvd_buf_printf(b, "},\"traits\":{\"home\":");
    put_feeling(b, t.home);
    zxvd_buf_printf(b,
                    ",\"curiosity_permille\":%u,\"caution_permille\":%u,\"warmth_permille\":%u,"
                    "\"pace_permille\":%u,\"verbosity_permille\":%u,\"specialty\":%u,"
                    "\"specialty_name\":",
                    (unsigned) t.curiosity_permille, (unsigned) t.caution_permille,
                    (unsigned) t.warmth_permille, (unsigned) t.pace_permille,
                    (unsigned) t.verbosity_permille, (unsigned) t.specialty);
    if (k->specialty && k->num_specialties)
        zxvd_buf_json_str(b, k->specialty[t.specialty]);
    else
        zxvd_buf_printf(b, "null");
    zxvd_buf_printf(b, "}");
}

/* H3 */
static void m_dna(zxvd_call_t *c)
{
    zxvd_companion_t k;
    if (!snap(c, &k)) return;
    zxvd_buf_t *b = zxvd_res(c);
    zxvd_buf_printf(b, "{\"agent_id\":%u,", (unsigned) k.agent_id);
    put_genes(b, &k.dna, &k);
    zxvd_buf_printf(b, ",");
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

/* H11: chg_name.h's codes by name, indexed by -code */
static const char *const CODE[] = {"CHGN_OK",       "CHGN_E_ARG",      "CHGN_E_EMPTY",
                                   "CHGN_E_UTF8",   "CHGN_E_START",    "CHGN_E_CHAR",
                                   "CHGN_E_SEP",    "CHGN_E_RESERVED", "CHGN_E_LOCKED",
                                   "CHGN_E_DIGEST", "CHGN_E_RECORD"};

/* H7, H11: the name record, or the call fails */
static bool names(zxvd_call_t *c, chgn_t *n)
{
    if (!NAMES || !NAMES->get(n)) {
        zxvd_fail(c, ZXVD_E_COMPANION_NAME,
                  "naming is not available here: no name store, or its record is damaged");
        return false;
    }
    return true;
}

static void name_refused(zxvd_call_t *c, int rc)
{
    zxvd_fail(c, ZXVD_E_COMPANION_NAME, "%s: %s", CODE[-rc], chgn_reason(rc));
}

/* H11: name is a string of at most 63 bytes */
static bool name_param(zxvd_call_t *c, char name[64])
{
    return zxvd_param_str(c, "name", name, 64, true);
}

/* H8 */
static void put_name(zxvd_buf_t *b, const chgn_t *n)
{
    if (!n->locked) {
        zxvd_buf_printf(b, "{\"named\":false,\"name\":null}");
        return;
    }
    zxvd_buf_printf(b, "{\"named\":true,\"name\":");
    zxvd_buf_json_str(b, n->name);
    zxvd_buf_printf(b, "}");
}

/* H8 */
static void m_name(zxvd_call_t *c)
{
    chgn_t n;
    if (!names(c, &n)) return;
    put_name(zxvd_res(c), &n);
}

/* H9 */
static void m_name_preview(zxvd_call_t *c)
{
    zxvd_companion_t k;
    chgn_t n;
    char name[64];
    if (!snap(c, &k) || !names(c, &n) || !name_param(c, name)) return;
    chgn_preview_t p;
    int rc = chgn_preview(&n, name, strlen(name), &p);
    if (rc != CHGN_OK) {
        name_refused(c, rc);
        return;
    }
    zxvd_buf_t *b = zxvd_res(c);
    zxvd_buf_printf(b, "{\"name\":");
    zxvd_buf_json_str(b, name);
    zxvd_buf_printf(b, ",");
    put_genes(b, &p.dna, &k);
    zxvd_buf_printf(b, ",\"confirm\":\"");
    for (uint32_t i = 0; i < CHGN_DIGEST; i++) zxvd_buf_printf(b, "%02x", (unsigned) p.confirm[i]);
    zxvd_buf_printf(b, "\"}");
}

static int hexval(char h)
{
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
}

/* H10 */
static void m_name_confirm(zxvd_call_t *c)
{
    chgn_t n;
    char name[64], hex[2 * CHGN_DIGEST + 2];
    if (!names(c, &n) || !name_param(c, name) ||
        !zxvd_param_str(c, "confirm", hex, sizeof hex, true))
        return;
    uint8_t d[CHGN_DIGEST];
    bool ok = strlen(hex) == 2 * CHGN_DIGEST;
    for (uint32_t i = 0; ok && i < CHGN_DIGEST; i++) {
        int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
        ok = hi >= 0 && lo >= 0;
        d[i] = (uint8_t) (hi * 16 + lo);
    }
    if (!ok) {
        zxvd_fail(c, ZXVD_E_PARAMS, "confirm: 64 hex digits, as companion.name_preview gave");
        return;
    }
    int rc = chgn_confirm(&n, name, strlen(name), d);
    if (rc != CHGN_OK) {
        name_refused(c, rc);
        return;
    }
    if (!NAMES->commit(&n)) {
        zxvd_fail(c, ZXVD_E_COMPANION_NAME, "the name could not be kept; the chiglet is not named");
        return;
    }
    put_name(zxvd_res(c), &n);
}

static const zxvd_group_method_t METHODS[] = {
    {"companion.dna", m_dna, true},
    {"companion.generation", m_generation, true},
    {"companion.mood", m_mood, true},
    {"companion.name", m_name, true},
    {"companion.name_preview", m_name_preview, true},
    {"companion.name_confirm", m_name_confirm, true},
};

static const zxvd_group_t GROUP = {
    "companion",
    false,
    "the Chiglet companion: its DNA, generation and mood, read from the swarm, and its name, "
    "given once (moves no money)",
    METHODS,
    sizeof METHODS / sizeof METHODS[0],
    NULL};

const zxvd_group_t *zxvd_companion_group(void)
{
    return &GROUP;
}
