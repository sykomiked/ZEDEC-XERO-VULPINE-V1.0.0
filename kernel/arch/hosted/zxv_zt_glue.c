/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_zt_glue.c — the hosted app's forward-pass glue (zxv_model_host.h M3):
 * runs the kernel tensor engine (kernel/src/tensor/zt_model) on the GGUF the
 * model slot mapped and writes the reply.
 *
 *   G1 LOAD.   The model is loaded once per mapped file (zt_model_load into
 *      a malloc'd arena) and kept until zxv_zt_release (the slot calls it
 *      when it closes the file). Each request starts from an empty KV cache.
 *   G2 CHAT.   When the vocabulary has <|im_start|> and <|im_end|> (Qwen2,
 *      Qwen3 and other ChatML models), the prompt ids are wrapped as
 *      "<|im_start|>user\n" PROMPT "<|im_end|>\n<|im_start|>assistant\n",
 *      with the special tokens inserted as ids, so text the user types can
 *      never become a control token. Other vocabularies continue the text.
 *   G3 DECODE. Greedy (argmax), integer only, up to max_new tokens or the
 *      context limit. Generation stops at tok->eos, <|im_end|> or
 *      <|endoftext|>; the stop token is not written.
 *   G4 LIMITS. One sequence at a time (the host is single-threaded), a
 *      context of at most 2048 tokens, scalar C speed (see zt_model.h:
 *      ESTIMATED 2.5 to 6 tokens a second for a 0.5B model on one core,
 *      not measured). No sampling settings are exposed yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_model_host.h"
#include "zt_model.h"

#define GLUE_CTX     2048u
#define GLUE_BATCH   16u
#define GLUE_MAX_NEW 256u

static struct {
    const zt_gguf_t *g; /* the file the model was loaded from */
    const uint8_t *buf;
    zt_model_t m;
    void *arena;
    zt_model_state_t s;
    void *state_mem;
    uint32_t n_ctx;
} G;

void zxv_zt_release(void)
{
    free(G.arena);
    free(G.state_mem);
    memset(&G, 0, sizeof G);
}

static void set_err(char *err, size_t cap, const char *fmt, const char *a, int b)
{
    if (err && cap) snprintf(err, cap, fmt, a, b);
}

int zxv_zt_can_run(const zt_gguf_t *g)
{
    return g && zt_model_arena_bytes(g) > 0;
}

static int32_t ensure_loaded(const zt_gguf_t *g, char *err, size_t err_cap)
{
    if (G.arena && G.g == g && G.buf == g->buf) return 0;
    zxv_zt_release();
    zt_model_err_t me;
    memset(&me, 0, sizeof me);
    uint64_t ab = zt_model_arena_bytes(g);
    if (!ab) {
        zt_model_cfg_t cfg;
        int32_t r = zt_model_config(g, &cfg, &me);
        set_err(err, err_cap, "the engine cannot run this file (%s, error %d)",
                me.what ? me.what : (me.name[0] ? me.name : "unsupported"),
                (int) (r ? r : me.code));
        return r ? r : -1;
    }
    if (ab > ((uint64_t) 1 << 40) || !(G.arena = malloc((size_t) ab))) {
        set_err(err, err_cap, "%s (error %d)", "not enough memory for the weights", -1);
        return -1;
    }
    int32_t r = zt_model_load(&G.m, g, G.arena, ab, &me);
    if (r) {
        set_err(err, err_cap, "the weights did not load (%s, error %d)", me.what ? me.what : "load",
                (int) r);
        zxv_zt_release();
        return r;
    }
    uint32_t n_ctx = G.m.cfg.n_ctx_train;
    if (n_ctx == 0 || n_ctx > GLUE_CTX) n_ctx = GLUE_CTX;
    uint64_t sb = zt_model_state_bytes(&G.m, n_ctx, GLUE_BATCH, ZT_KV_Q16);
    if (!sb || !(G.state_mem = malloc((size_t) sb)) ||
        zt_model_state_init(&G.s, &G.m, n_ctx, GLUE_BATCH, ZT_KV_Q16, G.state_mem, sb) != 0) {
        set_err(err, err_cap, "%s (error %d)", "could not set up the KV cache", -1);
        zxv_zt_release();
        return -1;
    }
    G.g = g;
    G.buf = g->buf;
    G.n_ctx = n_ctx;
    return 0;
}

static int32_t find_tok(const zt_tok_t *t, const char *s)
{
    return zt_tok_find(t, (const uint8_t *) s, strlen(s));
}

/* Encode plain text (no special parsing) onto ids[*n]. */
static int32_t push_text(const zt_tok_t *t, const char *s, int32_t *ids, uint64_t cap, uint64_t *n)
{
    uint64_t len = strlen(s), wb = zt_tok_work_bytes(len), got = 0;
    void *work = malloc((size_t) (wb ? wb : 1));
    if (!work) return -1;
    int32_t r =
        zt_tok_encode(t, (const uint8_t *) s, len, false, ids + *n, cap - *n, &got, work, wb);
    free(work);
    *n += got;
    return r;
}

int32_t zxv_zt_generate_n(const zt_gguf_t *g, const zt_tok_t *tok, const int32_t *ids,
                          uint64_t n_ids, uint32_t max_new, char *out, size_t cap, char *err,
                          size_t err_cap)
{
    if (!out || cap == 0) return -1;
    out[0] = 0;
    if (!g || !tok || (!ids && n_ids)) {
        set_err(err, err_cap, "%s (error %d)", "bad argument", -1);
        return -1;
    }
    int32_t r = ensure_loaded(g, err, err_cap);
    if (r) return r < 0 ? r : -1;
    if (tok->n_vocab != G.m.cfg.n_vocab) {
        set_err(err, err_cap, "the tokenizer has %s vocabulary size (%d)", "a different",
                (int) tok->n_vocab);
        return -1;
    }

    /* G2: the prompt, wrapped in the chat template when the vocabulary has it */
    int32_t im_start = find_tok(tok, "<|im_start|>"), im_end = find_tok(tok, "<|im_end|>");
    int32_t eot = find_tok(tok, "<|endoftext|>");
    uint64_t cap_ids = n_ids + 64, n = 0;
    if (cap_ids > G.n_ctx) {
        set_err(err, err_cap, "the message is too long for the %s context (%d tokens)", "model",
                (int) G.n_ctx);
        return -1;
    }
    int32_t *p = malloc((size_t) cap_ids * sizeof *p);
    int32_t *gen = malloc((size_t) (max_new ? max_new : 1) * sizeof *gen);
    zt_fx *logits = malloc((size_t) G.m.cfg.n_vocab * sizeof *logits);
    if (!p || !gen || !logits) {
        free(p);
        free(gen);
        free(logits);
        set_err(err, err_cap, "%s (error %d)", "out of memory", -1);
        return -1;
    }
    uint64_t skip = (n_ids && tok->bos >= 0 && ids[0] == tok->bos) ? 1 : 0;
    r = 0;
    if (im_start >= 0 && im_end >= 0) {
        if (tok->add_bos && tok->bos >= 0) p[n++] = tok->bos;
        p[n++] = im_start;
        r |= push_text(tok, "user\n", p, cap_ids, &n);
        memcpy(p + n, ids + skip, (size_t) (n_ids - skip) * sizeof *p);
        n += n_ids - skip;
        p[n++] = im_end;
        r |= push_text(tok, "\n", p, cap_ids, &n);
        p[n++] = im_start;
        r |= push_text(tok, "assistant\n", p, cap_ids, &n);
    } else {
        memcpy(p, ids, (size_t) n_ids * sizeof *p);
        n = n_ids;
    }
    if (r || n == 0) {
        free(p);
        free(gen);
        free(logits);
        set_err(err, err_cap, "%s (error %d)", "could not build the prompt", (int) r);
        return -1;
    }

    /* G3: greedy decode */
    zt_model_state_seek(&G.s, 0);
    uint32_t n_gen = 0;
    r = zt_model_eval(&G.m, &G.s, p, (uint32_t) n, logits, 0);
    while (r == 0 && n_gen < max_new) {
        int32_t t = zt_argmax(logits, G.m.cfg.n_vocab);
        if (t < 0 || t == tok->eos || t == im_end || t == eot) break;
        gen[n_gen++] = t;
        if (n_gen == max_new || G.s.pos >= G.n_ctx) break;
        r = zt_model_decode(&G.m, &G.s, t, logits);
    }
    if (r) {
        free(p);
        free(gen);
        free(logits);
        set_err(err, err_cap, "the forward pass %s (error %d)", "failed", (int) r);
        return r < 0 ? r : -1;
    }
    uint64_t dn = 0;
    r = zt_tok_decode(tok, gen, n_gen, (uint8_t *) out, cap - 1, &dn);
    if (r == ZT_TOK_ESPACE) r = 0; /* the reply was cut to fit; dn bytes are valid */
    out[dn < cap ? dn : cap - 1] = 0;
    free(p);
    free(gen);
    free(logits);
    if (r) {
        set_err(err, err_cap, "the reply could not be %s (error %d)", "decoded", (int) r);
        return -1;
    }
    return (int32_t) n_gen;
}

int32_t zxv_zt_generate(const zt_gguf_t *g, const zt_tok_t *tok, const int32_t *ids, uint64_t n_ids,
                        char *out, size_t cap, char *err, size_t err_cap)
{
    return zxv_zt_generate_n(g, tok, ids, n_ids, GLUE_MAX_NEW, out, cap, err, err_cap);
}
