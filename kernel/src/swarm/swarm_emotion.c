/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_emotion.c — imaginary-axis emotional economy. See swarm_emotion.h. */
#include "swarm_emotion.h"

/* Default behaviour profiles. Positive feelings explore, negative ones check
 * their work; frustration means "the user is waiting", so it runs first. */
static const swarm_emotion_profile_t PROFILES[SWARM_EMO_COUNT] = {
    /* codepoint  val  temp  explore verify prio */
    {0x1F610u, 0, 700, 100, 1, 2},  /* 😐 neutral */
    {0x1F60Cu, 1, 500, 50, 1, 1},   /* 😌 calm: steady, low key */
    {0x1F604u, 1, 900, 300, 1, 2},  /* 😄 joy: playful, creative */
    {0x1F914u, 1, 1000, 500, 1, 2}, /* 🤔 curious: explores most */
    {0x1F970u, 1, 800, 200, 1, 2},  /* 🥰 love: warm, personal */
    {0x1F61Fu, -1, 300, 50, 3, 3},  /* 😟 worry: careful, checks thrice */
    {0x1F620u, -1, 400, 50, 2, 4},  /* 😠 frustration: fast, first */
    {0x1F622u, -1, 600, 100, 2, 2}, /* 😢 sadness: gentle, checks twice */
};

uint64_t swarm_emotion_charge(uint8_t intensity)
{
    if (intensity == 0 || intensity > SWARM_EMO_MAX_INTENSITY) return 0;
    return swarm_fib((uint32_t) intensity + 1u);
}

uint64_t swarm_feeling_charge(swarm_feeling_t f)
{
    if (f.emotion == SWARM_EMO_NEUTRAL || f.emotion >= SWARM_EMO_COUNT) return 0;
    return swarm_emotion_charge(f.intensity);
}

const swarm_emotion_profile_t *swarm_emotion_profile(swarm_emotion_t e)
{
    if ((uint32_t) e >= SWARM_EMO_COUNT) return 0;
    return &PROFILES[e];
}

swarm_status_t swarm_emotion_from_codepoint(uint32_t codepoint, swarm_emotion_t *out)
{
    if (!out) return SWARM_ERR_ARG;
    for (uint32_t e = 0; e < SWARM_EMO_COUNT; e++) {
        if (PROFILES[e].codepoint == codepoint) {
            *out = (swarm_emotion_t) e;
            return SWARM_OK;
        }
    }
    return SWARM_ERR_ARG;
}

static bool feeling_valid(swarm_feeling_t f)
{
    return (uint32_t) f.emotion < SWARM_EMO_COUNT && f.intensity <= SWARM_EMO_MAX_INTENSITY;
}

void swarm_emotion_init(swarm_emotion_state_t *s)
{
    if (!s) return;
    s->mood.emotion = SWARM_EMO_NEUTRAL;
    s->mood.intensity = 0;
    s->num = 0;
    s->last_imag_pool = 0;
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        s->model_id[i] = 0;
        s->feeling[i].emotion = SWARM_EMO_NEUTRAL;
        s->feeling[i].intensity = 0;
    }
}

swarm_status_t swarm_emotion_set_mood(swarm_emotion_state_t *s, swarm_feeling_t mood)
{
    if (!s || !feeling_valid(mood)) return SWARM_ERR_ARG;
    s->mood = mood;
    return SWARM_OK;
}

swarm_status_t swarm_emotion_set_feeling(swarm_emotion_state_t *s, uint32_t model_id,
                                         swarm_feeling_t f)
{
    if (!s || !feeling_valid(f)) return SWARM_ERR_ARG;
    for (uint32_t i = 0; i < s->num; i++) {
        if (s->model_id[i] == model_id) {
            s->feeling[i] = f;
            return SWARM_OK;
        }
    }
    if (s->num >= SWARM_MAX_MODELS) return SWARM_ERR_FULL;
    s->model_id[s->num] = model_id;
    s->feeling[s->num] = f;
    s->num++;
    return SWARM_OK;
}

uint64_t swarm_emotion_imag_pool(uint64_t total, swarm_feeling_t mood)
{
    return swarm_muldiv(total, swarm_feeling_charge(mood), SWARM_EMO_CHARGE_DEN, 0);
}

static uint64_t model_charge(const swarm_emotion_state_t *s, uint32_t model_id)
{
    for (uint32_t i = 0; i < s->num; i++)
        if (s->model_id[i] == model_id) return swarm_feeling_charge(s->feeling[i]);
    return 0;
}

uint64_t swarm_emotion_pool(const swarm_budget_t *b, const swarm_emotion_state_t *s)
{
    if (!b || !s) return 0;
    uint64_t any = 0;
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].active) any += model_charge(s, b->slots[i].model_id);
    return any ? swarm_emotion_imag_pool(b->tokens_per_cycle, s->mood) : 0; /* E1, E4 */
}

void swarm_emotion_apply(swarm_budget_t *b, swarm_emotion_state_t *s, uint64_t imag)
{
    if (!b || !s) return;
    uint64_t charge[SWARM_MAX_MODELS];
    uint64_t share[SWARM_MAX_MODELS];
    for (uint32_t i = 0; i < b->num_slots; i++)
        charge[i] = b->slots[i].active ? model_charge(s, b->slots[i].model_id) : 0;
    swarm_split_lr(imag, charge, b->num_slots, share); /* E3 */
    for (uint32_t i = 0; i < b->num_slots; i++) {
        b->slots[i].allotted_im = share[i];
        b->slots[i].allotted += share[i];
    }
    s->last_imag_pool = imag;
}

swarm_status_t swarm_emotion_begin_cycle(swarm_budget_t *b, swarm_emotion_state_t *s)
{
    if (!b || !s) return SWARM_ERR_ARG;
    uint64_t imag = swarm_emotion_pool(b, s);
    swarm_status_t st = swarm_budget_begin_cycle_real(b, b->tokens_per_cycle - imag); /* E2 */
    if (st != SWARM_OK) return st;
    swarm_emotion_apply(b, s, imag);
    return SWARM_OK;
}
