/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_fixture_run.h — the fixture models (test_model_fixture.h) rebuilt
 * as GGUF bytes, and one run of each: all logits of the first sequence
 * (batch 4), then 8 greedy decode steps. The same as test_zt_simd.c's, for
 * the accelerator tests (test_zt_metal_math.c, test_zt_metal.c). Test-only;
 * include once, after test_model_fixture.h and zt_model.h. */
#ifndef TEST_ZT_FIXTURE_RUN_H
#define TEST_ZT_FIXTURE_RUN_H

static uint64_t fx_mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint8_t *fx_rebuild(const mf_variant_t *v)
{
    uint8_t *b = calloc(1, v->file_len);
    memcpy(b, v->hdr, v->hdr_len);
    for (uint32_t t = 0; t < v->n_gen; t++) {
        const mf_gen_t *g = &v->gen[t];
        uint8_t *p = b + v->hdr_len + g->off;
        if (g->kind == 1) {
            for (uint64_t k = 0; k < g->n / 32; k++, p += 34) {
                uint16_t d = (uint16_t) ((g->param << 10) | (fx_mix(g->seed, k * 33) & 0x3FF));
                p[0] = (uint8_t) d;
                p[1] = (uint8_t) (d >> 8);
                for (int i = 0; i < 32; i++)
                    p[2 + i] =
                        (uint8_t) (int8_t) ((int) (fx_mix(g->seed, k * 33 + 1 + i) % 255) - 127);
            }
        } else if (g->kind == 2) {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = fx_mix(g->seed, i);
                uint16_t h = (uint16_t) ((((z >> 20) & 1) << 15) |
                                         ((g->param - ((z >> 16) & 1)) << 10) | (z & 0x3FF));
                p[2 * i] = (uint8_t) h;
                p[2 * i + 1] = (uint8_t) (h >> 8);
            }
        } else {
            for (uint64_t i = 0; i < g->n; i++) {
                uint64_t z = fx_mix(g->seed, i);
                float f = g->kind == 3   ? 1.0f + (float) ((int) (z % 1025) - 512) / 2048.0f
                          : g->kind == 4 ? (float) ((int) (z % 2049) - 1024) / 4096.0f
                                         : 1.0f + (float) i / 8.0f;
                memcpy(p + 4 * i, &f, 4);
            }
        }
    }
    return b;
}

/* all logits of the first sequence (batch 4), then 8 greedy decode steps */
static zt_fx *fx_run(const zt_model_t *m, const mf_variant_t *v, uint32_t kv, uint64_t *n_out)
{
    uint32_t V = v->n_vocab, n0 = v->seq_len[0], steps = 8;
    uint64_t need = zt_model_state_bytes(m, 64, 4, kv);
    void *mem = malloc(need);
    zt_model_state_t s;
    zt_fx *out = malloc((size_t) V * (n0 + steps) * 4);
    zt_model_state_init(&s, m, 64, 4, kv, mem, need);
    zt_model_eval(m, &s, v->seq, n0, out, ZT_EVAL_ALL_LOGITS);
    zt_fx *lg = out + (uint64_t) (n0 - 1) * V;
    for (uint32_t i = 0; i < steps; i++) {
        int32_t id = zt_argmax(lg, V);
        lg += V;
        zt_model_decode(m, &s, id, lg);
    }
    free(mem);
    *n_out = (uint64_t) V * (n0 + steps);
    return out;
}

#endif /* TEST_ZT_FIXTURE_RUN_H */
