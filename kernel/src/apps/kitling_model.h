/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kitling_model.h — Kitling, your Chiglet companion, as a model (zxm.h):
 * the companion zxvd's swarm holds, read with companion.dna,
 * companion.generation and companion.mood, its budget from swarm.state,
 * and talk through swarm.ask. No drawing, no allocation, integers only;
 * the rules are kitling_rules.h.
 *
 *   KM1 READING. kitling_dna, kitling_generation, kitling_mood and
 *       kitling_budget each make one call and keep what it says only when
 *       every field is there and inside its range (genes 0..255, a
 *       generation in 32 bits, an emotion in the palette, an intensity
 *       0..5, a charge that is the intensity's Fibonacci charge); anything
 *       else is ZXM_E_PROTOCOL and the model is left as it was.
 *   KM2 WHAT IT SHOWS. The stage and whether it is grown (KR1, KR2), each
 *       gene's strength in permille (KR4), the budget used this cycle in
 *       hundredths of a percent (used_share's used_x100: floor(10000 *
 *       used / allotted)) and the tokens left in Zeckendorf form (KR3),
 *       written as its faces, largest first ("233 + 21 + 3").
 *   KM3 THE OWNER'S BUDGET. The owner sets a cap of tokens for each swarm
 *       cycle (0: Kitling never talks). A talk is asked only when the
 *       budget is fresh from swarm.state, the cycle is open, the companion
 *       is not exhausted, a whole answer's worth (max_per_answer) is left,
 *       and what Kitling spent this cycle plus a whole answer stays inside
 *       the cap; otherwise ZXM_E_STATE and the notice says which. What
 *       Kitling spent starts again at 0 when the cycle number changes.
 *       Kitling moves no money: it calls no ledger, pay or market method.
 *   KM4 MEMORY. Every talk is kept as a memory (KR5) in a bounded ring of
 *       KITLING_MEMORY: when it is full (head - tail >= KITLING_MEMORY) a
 *       new talk is refused before it is asked (ZXM_E_STATE), and only the
 *       owner's confirmed kitling_forget makes room. A memory that does
 *       not fit its record is never kept.
 *   KM5 NOTICES. zxvd has no notification method: the model composes each
 *       notice's text (kitling_t.notice) for the view to show. */
#ifndef KITLING_MODEL_H
#define KITLING_MODEL_H

#include "kitling_rules.h"
#include "zxm.h"

#define KITLING_GENES      8u
#define KITLING_MEMORY     64u
#define KITLING_NAME_MAX   15u
#define KITLING_NOTICE_MAX 255u
#define KITLING_ZECK_TEXT  1024u /* 63 faces of 14 digits with " + " */
#define KITLING_INTENSITY  5

typedef struct {
    char name[KITLING_NAME_MAX + 1]; /* "neutral" .. "sadness" */
    int64_t emotion;                 /* palette index (swarm_emotion.h) */
    int64_t intensity;               /* 0..5 */
    int64_t charge;                  /* F(intensity + 1), 0 for 0 */
    int64_t valence;                 /* -1, 0 or 1 */
} kitling_feeling_t;

typedef struct {
    int64_t seq, stamp, emotion, intensity, tokens; /* KR5 */
} kitling_memory_t;

typedef struct {
    zxm_link_t *link;
    /* companion.dna and companion.generation (KM1) */
    bool have_dna;
    uint64_t seed, parent_a, parent_b;
    bool founder;
    int64_t gene[KITLING_GENES];
    int64_t strength[KITLING_GENES]; /* KR4, permille */
    int64_t generation;
    int64_t stage; /* KR1 */
    bool grown;    /* KR2 */
    uint64_t steps, growths;
    /* companion.mood */
    bool have_mood;
    kitling_feeling_t feeling, home, swarm;
    bool at_home;
    /* the companion's budget, swarm.state (KM3) */
    bool have_budget;
    uint64_t cycle;
    bool cycle_open, exhausted;
    int64_t allotted, used, remaining, per_answer;
    int64_t used_x100; /* KM2 */
    int64_t zeck;      /* KR3 of remaining */
    /* the owner's cap and what Kitling spent this cycle (KM3) */
    int64_t cap, spent;
    uint64_t spent_cycle;
    /* memories (KM4) */
    kitling_memory_t memory[KITLING_MEMORY];
    uint64_t head, tail;
    int64_t next_seq;
    zxm_held_t held; /* the last answer, for the owner to read */
    char notice[KITLING_NOTICE_MAX + 1];
} kitling_t;

/* cap: the owner's tokens per cycle, 0..999999999. */
void kitling_init(kitling_t *k, zxm_link_t *link, int64_t cap);
int kitling_set_cap(kitling_t *k, int64_t cap);

int kitling_dna(kitling_t *k);
int kitling_generation(kitling_t *k);
int kitling_mood(kitling_t *k);
int kitling_budget(kitling_t *k);

/* KM3: ZXM_OK when a talk may be asked now (after kitling_budget). */
int kitling_may_talk(kitling_t *k);
/* KM3, KM4: ask the companion; the answer is k->held, the memory kept. */
int kitling_talk(kitling_t *k, const char *prompt, int64_t stamp);
/* KM4: drop the n oldest memories, only with the owner's confirmation. */
int kitling_forget(kitling_t *k, uint32_t n, bool confirmed);
uint32_t kitling_memories(const kitling_t *k);
const kitling_memory_t *kitling_memory_at(const kitling_t *k, uint32_t i); /* 0 = oldest */

/* KM2: a Zeckendorf mask as its faces, largest first; "0" for none. The
 * length, or -1 when the mask is no Zeckendorf form or it does not fit. */
int32_t kitling_zeck_text(int64_t mask, char *out, uint32_t cap);

#endif
