/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_model_file.h — the tiny qwen2 test model as a file a model slot opens
 * (shared by the hosted tests and write_test_model.c): the model rebuilt from
 * kernel/src/tensor/test_model_fixture.h with the qwen2 tokenizer fixture's
 * metadata added, as a real model file carries its tokenizer. Include after
 * zt_model.h (or zt_gguf.h), test_tok_fixture.h, <stdlib.h> and <string.h>. */
#ifndef TEST_MODEL_FILE_H
#define TEST_MODEL_FILE_H

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

/* The end of a GGUF's tensor infos. */
static uint64_t tinfo_end(const zt_gguf_t *g)
{
    uint64_t p = g->tinfo_off;
    for (uint64_t i = 0; i < g->n_tensors; i++) {
        p += 8 + le64(g->buf + p);                                 /* name */
        uint32_t nd = (uint32_t) (le64(g->buf + p) & 0xFFFFFFFFu); /* n_dims, u32 */
        p += 4 + 8 * (uint64_t) nd + 4 + 8;                        /* dims, type, offset */
    }
    return p;
}

/* The test model with the tokenizer fixture's metadata added: one GGUF that
 * a slot can open, as a real model file is. */
static uint8_t *model_with_tokenizer(const uint8_t *model, uint64_t mlen, uint64_t *out_len)
{
    zt_gguf_t m, t;
    if (zt_gguf_open(&m, model, mlen) || zt_gguf_open(&t, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN))
        return NULL;
    uint64_t mkv = m.tinfo_off - m.kv_off, tkv = t.tinfo_off - t.kv_off;
    uint64_t ti = tinfo_end(&m) - m.tinfo_off, data = mlen - m.data_off;
    uint64_t head = 24 + mkv + tkv + ti, doff = (head + 31) & ~31ull;
    uint8_t *b = calloc(1, doff + data);
    if (!b) return NULL;
    memcpy(b, model, 8); /* magic, version */
    uint64_t nt = m.n_tensors, nkv = m.n_kv + t.n_kv;
    memcpy(b + 8, &nt, 8);
    memcpy(b + 16, &nkv, 8);
    memcpy(b + 24, model + m.kv_off, mkv); /* the model's keys come first and win */
    memcpy(b + 24 + mkv, TOK_GGUF + t.kv_off, tkv);
    memcpy(b + 24 + mkv + tkv, model + m.tinfo_off, ti);
    memcpy(b + doff, model + m.data_off, data);
    *out_len = doff + data;
    return b;
}

#endif /* TEST_MODEL_FILE_H */
