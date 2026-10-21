/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kitling_model.c — Kitling, your Chiglet companion (rules in
 * kitling_model.h KM1-KM5 and kitling_rules.h KR1-KR5). Integers only. */
#include <string.h>

#include "kitling_model.h"

static const char *const GENE[KITLING_GENES] = {"emotion", "temperament", "curiosity", "caution",
                                                "warmth",  "pace",        "verbosity", "specialty"};
static const char *const EMOTION[] = {"neutral", "calm",  "joy",         "curious",
                                      "love",    "worry", "frustration", "sadness"};
enum { NEMOTION = sizeof EMOTION / sizeof EMOTION[0] };
/* the traits zxvd expresses in permille, by gene (swarm_dna.h D2) */
static const char *const PERMILLE[KITLING_GENES] = {0,
                                                    0,
                                                    "curiosity_permille",
                                                    "caution_permille",
                                                    "warmth_permille",
                                                    "pace_permille",
                                                    "verbosity_permille",
                                                    0};

/* ---- text, bounded ---- */

static void put(char *out, uint32_t cap, uint32_t *n, const char *s)
{
    while (*s && *n + 1u < cap) out[(*n)++] = *s++;
    out[*n] = 0;
}

static void put_i64(char *out, uint32_t cap, uint32_t *n, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do t[k++] = (char) ('0' + u % 10u);
    while ((u /= 10u) && k < 20u);
    if (v < 0) t[k++] = '-';
    char r[24];
    for (uint32_t i = 0; i < k; i++) r[i] = t[k - 1u - i];
    r[k] = 0;
    put(out, cap, n, r);
}

static void say(kitling_t *k, const char *a, int64_t v, const char *b)
{
    uint32_t n = 0;
    k->notice[0] = 0;
    put(k->notice, sizeof k->notice, &n, a);
    if (b) {
        put_i64(k->notice, sizeof k->notice, &n, v);
        put(k->notice, sizeof k->notice, &n, b);
    }
}

/* a decimal string of at most 20 digits that fits 64 bits */
static bool dec_u64(const char *s, uint64_t *v)
{
    uint64_t x = 0;
    uint32_t i = 0;
    if (!s[0]) return false;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 20u) return false;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (x > (UINT64_MAX - d) / 10u) return false;
        x = x * 10u + d;
    }
    *v = x;
    return true;
}

static bool get_dec(zxm_link_t *l, int32_t obj, const char *key, uint64_t *v)
{
    char s[24];
    return zxm_get_str(l, obj, key, s, sizeof s) == ZXM_OK && dec_u64(s, v);
}

static bool get_range(zxm_link_t *l, int32_t obj, const char *key, int64_t lo, int64_t hi,
                      int64_t *v)
{
    int64_t x;
    if (zxm_get_i64(l, obj, key, &x) != ZXM_OK || x < lo || x > hi) return false;
    *v = x;
    return true;
}

static int bad(kitling_t *k, const char *why)
{
    say(k, why, 0, 0);
    return ZXM_E_PROTOCOL;
}

void kitling_init(kitling_t *k, zxm_link_t *link, int64_t cap)
{
    memset(k, 0, sizeof *k);
    k->link = link;
    k->stage = KITLING_REFUSED;
    k->cap = cap >= 0 && cap <= 999999999 ? cap : 0;
    k->next_seq = 1;
}

int kitling_set_cap(kitling_t *k, int64_t cap)
{
    if (!k || cap < 0 || cap > 999999999) return ZXM_E_ARG;
    k->cap = cap;
    return ZXM_OK;
}

/* ---- KM1: reading the companion ---- */

int kitling_dna(kitling_t *k)
{
    if (!k || !k->link) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    zxm_begin(l, "companion.dna");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result, g = zxm_get(l, r, "genes"), t = zxm_get(l, r, "traits");
    uint64_t seed, pa, pb;
    int64_t gen, gene[KITLING_GENES], strength[KITLING_GENES];
    bool founder;
    if (!get_dec(l, r, "seed", &seed) || !get_dec(l, r, "parent_a", &pa) ||
        !get_dec(l, r, "parent_b", &pb) || zxm_get_bool(l, r, "founder", &founder) != ZXM_OK ||
        !get_range(l, r, "generation", 0, KITLING_GEN_MAX, &gen))
        return bad(k, "the companion's DNA is not readable");
    if (founder != (pa == 0 && pb == 0)) return bad(k, "the companion's lineage disagrees");
    for (uint32_t i = 0; i < KITLING_GENES; i++) {
        if (!get_range(l, g, GENE[i], 0, KITLING_GENE_MAX, &gene[i]))
            return bad(k, "a gene is missing or not a byte");
        strength[i] = kitling_strength(gene[i]);
        int64_t p;
        /* KR4 witnesses zxvd's own expression (swarm_dna.h D2) */
        if (PERMILLE[i] && (!get_range(l, t, PERMILLE[i], 0, 1000, &p) || p != strength[i]))
            return bad(k, "a trait does not match its gene");
    }
    k->seed = seed;
    k->parent_a = pa;
    k->parent_b = pb;
    k->founder = founder;
    memcpy(k->gene, gene, sizeof gene);
    memcpy(k->strength, strength, sizeof strength);
    k->generation = gen;
    k->stage = kitling_stage(gen);
    k->grown = kitling_grown(gen);
    k->have_dna = true;
    return ZXM_OK;
}

int kitling_generation(kitling_t *k)
{
    if (!k || !k->link) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    zxm_begin(l, "companion.generation");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int64_t gen;
    uint64_t steps, growths;
    if (!get_range(l, l->result, "generation", 0, KITLING_GEN_MAX, &gen) ||
        zxm_get_u64(l, l->result, "steps", &steps) != ZXM_OK ||
        zxm_get_u64(l, l->result, "growths", &growths) != ZXM_OK)
        return bad(k, "the companion's generation is not readable");
    k->generation = gen;
    k->stage = kitling_stage(gen);
    k->grown = kitling_grown(gen);
    k->steps = steps;
    k->growths = growths;
    return ZXM_OK;
}

/* one feeling of companion.mood (zxvd_companion.h H5) */
static bool feeling(zxm_link_t *l, int32_t obj, kitling_feeling_t *f)
{
    static const int64_t CHARGE[KITLING_INTENSITY + 1] = {0, 1, 2, 3, 5, 8};
    kitling_feeling_t x;
    memset(&x, 0, sizeof x);
    if (zxm_get_str(l, obj, "emotion", x.name, sizeof x.name) != ZXM_OK) return false;
    x.emotion = -1;
    for (int64_t i = 0; i < NEMOTION; i++)
        if (!strcmp(x.name, EMOTION[i])) x.emotion = i;
    if (x.emotion < 0 || !get_range(l, obj, "intensity", 0, KITLING_INTENSITY, &x.intensity) ||
        !get_range(l, obj, "charge", 0, 8, &x.charge) ||
        !get_range(l, obj, "valence", -1, 1, &x.valence))
        return false;
    /* the charge is the intensity's Fibonacci charge, the neutral emoji's 0 */
    if (x.charge != (x.emotion ? CHARGE[x.intensity] : 0)) return false;
    *f = x;
    return true;
}

int kitling_mood(kitling_t *k)
{
    if (!k || !k->link) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    zxm_begin(l, "companion.mood");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    kitling_feeling_t f, h, s;
    bool at;
    int32_t r = l->result;
    if (!feeling(l, zxm_get(l, r, "feeling"), &f) || !feeling(l, zxm_get(l, r, "home"), &h) ||
        !feeling(l, zxm_get(l, r, "swarm_mood"), &s) ||
        zxm_get_bool(l, r, "at_home", &at) != ZXM_OK || at != (f.emotion == h.emotion))
        return bad(k, "the companion's mood is not readable");
    k->feeling = f;
    k->home = h;
    k->swarm = s;
    k->at_home = at;
    k->have_mood = true;
    return ZXM_OK;
}

int kitling_budget(kitling_t *k)
{
    if (!k || !k->link) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    k->have_budget = false; /* KM3: a talk needs a fresh budget */
    zxm_begin(l, "swarm.state");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t c = zxm_get(l, l->result, "companion");
    uint64_t cycle;
    bool open, out;
    int64_t allotted, used, remaining, per;
    if (zxm_get_u64(l, l->result, "cycle", &cycle) != ZXM_OK ||
        zxm_get_bool(l, c, "cycle_open", &open) != ZXM_OK ||
        zxm_get_bool(l, c, "exhausted", &out) != ZXM_OK ||
        !get_range(l, c, "allotted", 0, 999999999, &allotted) ||
        !get_range(l, c, "used", 0, allotted, &used) ||
        !get_range(l, c, "remaining", 0, allotted, &remaining) ||
        !get_range(l, c, "max_per_answer", 0, 999999999, &per) || used + remaining != allotted)
        return bad(k, "the companion's budget is not readable");
    if (cycle != k->spent_cycle) { /* KM3: a new cycle, nothing spent in it yet */
        k->spent_cycle = cycle;
        k->spent = 0;
    }
    k->cycle = cycle;
    k->cycle_open = open;
    k->exhausted = out;
    k->allotted = allotted;
    k->used = used;
    k->remaining = remaining;
    k->per_answer = per;
    k->used_x100 = allotted ? used * 10000 / allotted : 0; /* used_share's used_x100 */
    k->zeck = kitling_zeck(remaining);
    k->have_budget = true;
    return ZXM_OK;
}

/* ---- KM3: the owner's budget ---- */

int kitling_may_talk(kitling_t *k)
{
    if (!k) return ZXM_E_ARG;
    if (!k->have_budget) {
        say(k, "Read the companion's budget first.", 0, 0);
        return ZXM_E_STATE;
    }
    if (!k->cycle_open || k->exhausted) {
        say(k, "The companion rests until the next cycle.", 0, 0);
        return ZXM_E_STATE;
    }
    if (k->per_answer < 1 || k->remaining < k->per_answer) {
        say(k, "Only ", k->remaining, " tokens are left this cycle: not enough for an answer.");
        return ZXM_E_STATE;
    }
    if (k->spent + k->per_answer > k->cap) {
        say(k, "Your cap for this cycle is reached: ", k->cap - k->spent, " tokens left of it.");
        return ZXM_E_STATE;
    }
    if (k->head - k->tail >= KITLING_MEMORY) {
        say(k, "Kitling's memory is full: forget some talks first.", 0, 0);
        return ZXM_E_STATE;
    }
    return ZXM_OK;
}

int kitling_talk(kitling_t *k, const char *prompt, int64_t stamp)
{
    static const zxm_ai_action_t TALK = {"Talk", "Companion", 0};
    if (!k || !k->link || !prompt || !prompt[0]) return ZXM_E_ARG;
    int rc = kitling_budget(k);
    if (rc != ZXM_OK) return rc;
    rc = kitling_may_talk(k);
    if (rc != ZXM_OK) return rc;
    kitling_memory_t m = {k->next_seq, stamp, k->have_mood ? k->feeling.emotion : 0,
                          k->have_mood ? k->feeling.intensity : 0, 0};
    if (!kitling_memory_ok(m.seq, m.stamp, m.emotion, m.intensity, m.tokens) || stamp < 0) {
        say(k, "That moment cannot be remembered.", 0, 0);
        return ZXM_E_ARG;
    }
    zxm_held_t held;
    rc = zxm_ai_ask(k->link, &TALK, 0, "You are the owner's Chiglet companion. Answer kindly.",
                    prompt, (uint32_t) strlen(prompt), &held);
    k->have_budget = false; /* the answer spent some of it */
    if (rc != ZXM_OK) return rc;
    /* KM3: what the answer cost, counted whole (zxvd's gate held it under max_per_answer) */
    int64_t cost = held.cost.tokens > 999999999u ? 999999999 : (int64_t) held.cost.tokens;
    m.tokens = cost;
    k->spent += cost;
    k->held = held;
    if (kitling_memory_ok(m.seq, m.stamp, m.emotion, m.intensity, m.tokens)) {
        k->memory[k->head % KITLING_MEMORY] = m;
        k->head++;
        k->next_seq = m.seq + 1 > 999999999 ? 1 : m.seq + 1;
    }
    say(k, "Kitling answered for ", cost, " tokens.");
    return ZXM_OK;
}

/* ---- KM4: memories ---- */

int kitling_forget(kitling_t *k, uint32_t n, bool confirmed)
{
    if (!k) return ZXM_E_ARG;
    if (!confirmed) {
        say(k, "Forgetting waits for your word.", 0, 0);
        return ZXM_E_STATE;
    }
    if (n > k->head - k->tail) return ZXM_E_ARG;
    k->tail += n;
    say(k, "Forgot ", (int64_t) n, " talks.");
    return ZXM_OK;
}

uint32_t kitling_memories(const kitling_t *k)
{
    return k ? (uint32_t) (k->head - k->tail) : 0;
}

const kitling_memory_t *kitling_memory_at(const kitling_t *k, uint32_t i)
{
    if (!k || i >= k->head - k->tail) return 0;
    return &k->memory[(k->tail + i) % KITLING_MEMORY];
}

/* ---- KM2: the Zeckendorf form for display ---- */

int32_t kitling_zeck_text(int64_t mask, char *out, uint32_t cap)
{
    if (!out || cap < 2u) return -1;
    out[0] = 0;
    if (mask < 0 || (mask & (mask >> 1)) || mask > kitling_zeck(KITLING_ZECK_MAX)) return -1;
    if (!mask) {
        out[0] = '0';
        out[1] = 0;
        return 1;
    }
    int64_t face[KITLING_ZECK_TIERS];
    face[0] = 1;
    face[1] = 2;
    for (uint32_t i = 2; i < KITLING_ZECK_TIERS; i++) face[i] = face[i - 1] + face[i - 2];
    uint32_t n = 0;
    for (uint32_t i = KITLING_ZECK_TIERS; i-- > 0;) {
        if (!(mask >> i & 1)) continue;
        if (n) put(out, cap, &n, " + ");
        put_i64(out, cap, &n, face[i]);
    }
    return n + 1u >= cap ? -1 : (int32_t) n;
}
