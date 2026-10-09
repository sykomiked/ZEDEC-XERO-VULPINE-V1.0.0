/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_evolve.c — the evolution protocol. See swarm_evolve.h. */
#include "swarm_evolve.h"
#include "swarm_budget.h"   /* swarm_muldiv */

swarm_evo_verdict_t swarm_evo_judge(const swarm_model_state_t *champion,
                                    const swarm_model_state_t *candidate,
                                    uint32_t budget_tokens)
{
    if (candidate->cost_tokens > budget_tokens) return SWARM_EVO_REJECT_BUDGET;
    if (candidate->score_milli < champion->score_milli) return SWARM_EVO_REJECT_WORSE;
    if (candidate->score_milli == champion->score_milli &&
        candidate->cost_tokens >= champion->cost_tokens)
        return SWARM_EVO_REJECT_NO_GAIN;
    return SWARM_EVO_ACCEPT;
}

uint64_t swarm_evo_outlier_share(uint64_t total_tokens)
{
    uint64_t rem;
    return swarm_muldiv(total_tokens, 1, 21, &rem);
}

uint32_t swarm_evo_outlier(const swarm_dna_t *dna, uint32_t n)
{
    if (n < 2) return 0;
    uint32_t sum[SWARM_DNA_GENES] = { 0 };
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t g = 0; g < SWARM_DNA_GENES; g++) sum[g] += dna[i].gene[g];
    uint32_t best = 1, best_d = 0;
    for (uint32_t i = 1; i < n; i++) {
        /* distance from the mean, scaled by n to stay in whole numbers */
        uint32_t d = 0;
        for (uint32_t g = 0; g < SWARM_DNA_GENES; g++) {
            uint32_t x = dna[i].gene[g] * n, m = sum[g];
            d += x > m ? x - m : m - x;
        }
        if (d > best_d) { best_d = d; best = i; }
    }
    return best;
}

uint32_t swarm_evo_mobility(uint8_t *level, const uint64_t *fitness, uint32_t n,
                            uint32_t num_levels)
{
    uint32_t swaps = 0;
    for (uint32_t d = 1; d + 1 < num_levels; d++) {
        int32_t weak = -1, strong = -1;
        for (uint32_t i = 0; i < n; i++) {
            if (level[i] == d && (weak < 0 || fitness[i] < fitness[weak])) weak = (int32_t)i;
            if (level[i] == d + 1 && (strong < 0 || fitness[i] > fitness[strong])) strong = (int32_t)i;
        }
        if (weak >= 0 && strong >= 0 && fitness[strong] > fitness[weak]) {
            level[weak] = (uint8_t)(d + 1);
            level[strong] = (uint8_t)d;
            swaps++;
        }
    }
    return swaps;
}

uint32_t swarm_evo_crc32(const uint8_t *p, uint32_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

/* Layout: "ZXVM" ver(1) pad(3) id parent seed (8 each) genes(8)
 * parent_a/parent_b of the DNA are not kept: the state's parent is the
 * lineage. generation base adapter score cost (4 each) crc (4) = 64. */
void swarm_evo_save(const swarm_model_state_t *s, uint8_t out[SWARM_EVO_RECORD])
{
    for (uint32_t i = 0; i < SWARM_EVO_RECORD; i++) out[i] = 0;
    out[0] = 'Z'; out[1] = 'X'; out[2] = 'V'; out[3] = 'M'; out[4] = 1;
    put64(out + 8, s->id);
    put64(out + 16, s->parent);
    put64(out + 24, s->dna.seed);
    for (uint32_t g = 0; g < SWARM_DNA_GENES; g++) out[32 + g] = s->dna.gene[g];
    put32(out + 40, s->dna.generation);
    put32(out + 44, s->base_model);
    put32(out + 48, s->adapter);
    put32(out + 52, s->score_milli);
    put32(out + 56, s->cost_tokens);
    put32(out + 60, swarm_evo_crc32(out, 60));
}

bool swarm_evo_restore(const uint8_t in[SWARM_EVO_RECORD], swarm_model_state_t *s)
{
    if (in[0] != 'Z' || in[1] != 'X' || in[2] != 'V' || in[3] != 'M' || in[4] != 1) return false;
    if (get32(in + 60) != swarm_evo_crc32(in, 60)) return false;
    s->id = get64(in + 8);
    s->parent = get64(in + 16);
    s->dna.seed = get64(in + 24);
    for (uint32_t g = 0; g < SWARM_DNA_GENES; g++) s->dna.gene[g] = in[32 + g];
    s->dna.parent_a = s->dna.parent_b = 0;
    s->dna.generation = get32(in + 40);
    s->base_model = get32(in + 44);
    s->adapter = get32(in + 48);
    s->score_milli = get32(in + 52);
    s->cost_tokens = get32(in + 56);
    return true;
}
