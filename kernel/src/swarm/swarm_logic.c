/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_logic.c — spaces and paradox operators. See swarm_logic.h. */
#include "swarm_logic.h"

swarm_hk_truth_t swarm_logic_truth(const swarm_claim_t *c)
{
    if (c->space == SWARM_SPACE_NEU) return SWARM_HK_NEUTRAL;
    if (!c->verified) return SWARM_HK_UNKNOWN;
    return c->space == SWARM_SPACE_POS ? SWARM_HK_TRUE : SWARM_HK_FALSE;
}

static swarm_lg_result_t result(swarm_lg_kind_t k, swarm_claim_t out, uint32_t freed)
{
    swarm_lg_result_t r;
    r.kind = k;
    r.out = out;
    r.freed = freed;
    r.escalate = false;
    r.truth = swarm_logic_truth(&out);
    return r;
}

swarm_lg_result_t swarm_logic_meet(const swarm_claim_t *a, const swarm_claim_t *b)
{
    /* L1 */
    if (a->topic != b->topic) return result(SWARM_LG_UNRELATED, *a, 0);

    /* L3 */
    if (a->space == SWARM_SPACE_NEU && b->space == SWARM_SPACE_NEU) {
        swarm_claim_t o = *a;
        o.tokens = a->tokens + b->tokens;
        return result(SWARM_LG_HOLD, o, 0);
    }
    if (a->space == SWARM_SPACE_NEU) return result(SWARM_LG_HOLD, *b, 0);
    if (b->space == SWARM_SPACE_NEU) return result(SWARM_LG_HOLD, *a, 0);

    /* L2 */
    if (a->space == b->space) {
        swarm_claim_t o = *a;
        o.level = (uint8_t) ((a->level > b->level ? a->level : b->level) + 1u);
        o.verified = a->verified || b->verified;
        o.tokens = a->tokens > b->tokens ? a->tokens : b->tokens;
        return result(SWARM_LG_REINFORCE, o, a->tokens < b->tokens ? a->tokens : b->tokens);
    }

    /* S+ against S- */
    if (a->verified && b->verified) {
        if (a->level == b->level) { /* L7 */
            swarm_claim_t o = *a;
            o.space = SWARM_SPACE_NEU;
            o.verified = false;
            o.tokens = 0;
            swarm_lg_result_t r = result(SWARM_LG_PARADOX, o, a->tokens + b->tokens);
            r.escalate = true;
            r.truth = SWARM_HK_PARADOX;
            return r;
        }
        const swarm_claim_t *hi = a->level > b->level ? a : b; /* L6 */
        const swarm_claim_t *lo = hi == a ? b : a;
        swarm_lg_result_t r = result(SWARM_LG_GLUT, *hi, lo->tokens);
        r.truth = SWARM_HK_GLUT;
        return r;
    }
    if (a->verified != b->verified) { /* L5, evidence */
        const swarm_claim_t *w = a->verified ? a : b;
        return result(SWARM_LG_DOMINATE, *w, (w == a ? b : a)->tokens);
    }
    if (a->level == b->level) { /* L4 */
        swarm_claim_t o = *a;
        o.space = SWARM_SPACE_NEU;
        o.tokens = 0;
        return result(SWARM_LG_ANNIHILATE, o, a->tokens + b->tokens);
    }
    const swarm_claim_t *w = a->level > b->level ? a : b; /* L5, level */
    return result(SWARM_LG_DOMINATE, *w, (w == a ? b : a)->tokens);
}
