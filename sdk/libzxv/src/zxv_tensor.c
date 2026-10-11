/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_tensor.c — ABI 1.1: the integer inference engine (kernel/src/tensor:
 * zt_gguf, zt_tok, zt_model) behind opaque model and session handles.
 *
 * Files are read into memory once (the engine uses Q8_0/Q4_K/Q6_K weights
 * in place, so the bytes must outlive the model). Matrix products use the
 * best SIMD kernel set this CPU has (zt_simd_best: AVX2, AVX-512 or NEON);
 * every kernel set gives the C reference's bits, so results stay identical
 * across machines. One thread per generate call: sessions on different
 * threads may share one model. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#ifndef ZXV_NO_TENSOR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zt_gguf.h"
#include "zt_model.h"
#include "zt_simd.h"
#include "zt_tok.h"

int32_t zxvi_rmag_charge(zxv_rmag *rmag, uint32_t budget, uint64_t n);
zxv_ctx *zxvi_rmag_ctx(const zxv_rmag *rmag);

struct zxv_model {
    uint8_t *file, *tok_file; /* owned bytes (tok_file NULL = same file) */
    size_t file_len, tok_len;
    zt_gguf_t g, tg;
    zt_model_t m;
    zt_tok_t tok;
    void *arena, *tok_arena;
};

struct zxv_session {
    const zxv_model *model;
    zt_model_state_t s;
    void *mem;
    zxv_rmag *rmag;
    uint32_t budget;
};

struct zxv_lora {
    int unused;
};

static int32_t model_err(int32_t code, const zt_model_err_t *e, const char *what)
{
    const char *why = e && e->what ? e->what : "";
    const char *name = e ? e->name : "";
    int32_t st = code == ZT_MODEL_EARCH || code == ZT_MODEL_ECONFIG ? ZXV_E_NOTIMPL : ZXV_E_FORMAT;
    if (code == ZT_MODEL_ESPACE) st = ZXV_E_INTERNAL;
    return zxvi_fail(st, "%s: engine error %d %s%s%s", what, code, why, name[0] ? ": " : "", name);
}

static int32_t read_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return zxvi_fail(ZXV_E_IO, "cannot open \"%.200s\"", path);
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return zxvi_fail(ZXV_E_IO, "cannot seek \"%.200s\"", path);
    }
    long n = ftell(f);
    rewind(f);
    if (n <= 0) {
        fclose(f);
        return zxvi_fail(ZXV_E_FORMAT, "\"%.200s\" is empty", path);
    }
    uint8_t *b = malloc((size_t) n);
    if (!b) {
        fclose(f);
        return zxvi_fail(ZXV_E_NOMEM, "cannot allocate %ld bytes for \"%.200s\"", n, path);
    }
    size_t got = fread(b, 1, (size_t) n, f);
    fclose(f);
    if (got != (size_t) n) {
        free(b);
        return zxvi_fail(ZXV_E_IO, "short read on \"%.200s\"", path);
    }
    *out = b;
    *out_len = (size_t) n;
    return ZXV_OK;
}

void zxv_model_destroy(zxv_model *model)
{
    if (!model) return;
    free(model->arena);
    free(model->tok_arena);
    free(model->file);
    free(model->tok_file);
    free(model);
}

/* Takes ownership of file and tok_file (freed on failure too). */
static int32_t model_build(uint8_t *file, size_t len, uint8_t *tok_file, size_t tok_len,
                           zxv_model **out)
{
    zxv_model *M = calloc(1, sizeof *M);
    if (!M) {
        free(file);
        free(tok_file);
        return zxvi_fail(ZXV_E_NOMEM, "model allocation failed");
    }
    M->file = file;
    M->file_len = len;
    M->tok_file = tok_file;
    M->tok_len = tok_len;
    int32_t r = zt_gguf_open(&M->g, M->file, M->file_len);
    if (r != 0) {
        zxv_model_destroy(M);
        return zxvi_fail(ZXV_E_FORMAT, "not a readable GGUF file (engine error %d)", r);
    }
    zt_model_err_t err;
    memset(&err, 0, sizeof err);
    uint64_t ab = zt_model_arena_bytes(&M->g);
    if (ab == 0) {
        zt_model_cfg_t cfg;
        r = zt_model_config(&M->g, &cfg, &err);
        zxv_model_destroy(M);
        return model_err(r ? r : ZT_MODEL_ECONFIG, &err, "model");
    }
    M->arena = malloc(ab);
    if (!M->arena) {
        zxv_model_destroy(M);
        return zxvi_fail(ZXV_E_NOMEM, "cannot allocate the %llu-byte weight arena",
                         (unsigned long long) ab);
    }
    r = zt_model_load(&M->m, &M->g, M->arena, ab, &err);
    if (r != 0) {
        zxv_model_destroy(M);
        return model_err(r, &err, "model");
    }
    M->m.kern = zt_simd_best();
    const zt_gguf_t *tg = &M->g;
    if (M->tok_file) {
        r = zt_gguf_open(&M->tg, M->tok_file, M->tok_len);
        if (r != 0) {
            zxv_model_destroy(M);
            return zxvi_fail(ZXV_E_FORMAT, "tokenizer is not a readable GGUF file (%d)", r);
        }
        tg = &M->tg;
    }
    uint64_t tb = zt_tok_arena_bytes(tg);
    if (tb == 0) {
        zxv_model_destroy(M);
        return zxvi_fail(ZXV_E_NOTIMPL, "the file has no BPE tokenizer this engine reads");
    }
    M->tok_arena = malloc(tb);
    if (!M->tok_arena) {
        zxv_model_destroy(M);
        return zxvi_fail(ZXV_E_NOMEM, "tokenizer arena allocation failed");
    }
    r = zt_tok_load(&M->tok, tg, M->tok_arena, tb);
    if (r != 0) {
        zxv_model_destroy(M);
        return zxvi_fail(ZXV_E_FORMAT, "tokenizer load failed (engine error %d)", r);
    }
    if (M->tok.n_vocab != M->m.cfg.n_vocab) {
        uint32_t a = M->tok.n_vocab, b = M->m.cfg.n_vocab;
        zxv_model_destroy(M);
        return zxvi_fail(ZXV_E_FORMAT, "tokenizer has %u tokens but the model %u", a, b);
    }
    *out = M;
    return zxvi_ok();
}

int32_t zxv_model_open(const zxv_cap *cap, const char *model_path, const char *tokenizer_path,
                       zxv_model **out_model)
{
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_AI_INFERENCE, "model_open", NULL);
    if (rc) return rc;
    if (!model_path || !out_model) return zxvi_fail(ZXV_E_ARG, "model_path or out_model is NULL");
    uint8_t *f = NULL, *t = NULL;
    size_t fl = 0, tl = 0;
    if ((rc = read_file(model_path, &f, &fl))) return rc;
    if (tokenizer_path && (rc = read_file(tokenizer_path, &t, &tl))) {
        free(f);
        return rc;
    }
    return model_build(f, fl, t, tl, out_model);
}

int32_t zxv_model_open_memory(const zxv_cap *cap, const uint8_t *model, size_t model_len,
                              const uint8_t *tokenizer, size_t tokenizer_len,
                              zxv_model **out_model)
{
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_AI_INFERENCE, "model_open_memory", NULL);
    if (rc) return rc;
    if (!model || !model_len || !out_model || (tokenizer_len && !tokenizer))
        return zxvi_fail(ZXV_E_ARG, "model bytes or out_model missing");
    uint8_t *f = malloc(model_len), *t = NULL;
    if (!f) return zxvi_fail(ZXV_E_NOMEM, "model copy allocation failed");
    memcpy(f, model, model_len);
    if (tokenizer && tokenizer_len) {
        t = malloc(tokenizer_len);
        if (!t) {
            free(f);
            return zxvi_fail(ZXV_E_NOMEM, "tokenizer copy allocation failed");
        }
        memcpy(t, tokenizer, tokenizer_len);
    }
    return model_build(f, model_len, t, t ? tokenizer_len : 0, out_model);
}

int32_t zxv_model_info(const zxv_model *model, int32_t key, int64_t *out_value)
{
    if (!model || !out_value) return zxvi_fail(ZXV_E_ARG, "model or out_value is NULL");
    const zt_model_cfg_t *c = &model->m.cfg;
    switch (key) {
    case ZXV_MODEL_ARCH: *out_value = c->arch; break;
    case ZXV_MODEL_N_VOCAB: *out_value = c->n_vocab; break;
    case ZXV_MODEL_N_CTX_TRAIN: *out_value = c->n_ctx_train; break;
    case ZXV_MODEL_N_LAYER: *out_value = c->n_layer; break;
    case ZXV_MODEL_N_EMBD: *out_value = c->n_embd; break;
    case ZXV_MODEL_N_HEAD: *out_value = c->n_head; break;
    case ZXV_MODEL_N_HEAD_KV: *out_value = c->n_head_kv; break;
    case ZXV_MODEL_EOS: *out_value = model->tok.eos; break;
    default: return zxvi_fail(ZXV_E_ARG, "unknown model info key %d", key);
    }
    return zxvi_ok();
}

int32_t zxv_tokenize(const zxv_model *model, const uint8_t *text, size_t len,
                     int32_t parse_special, int32_t *ids, size_t cap, size_t *out_count)
{
    if (!model || (!text && len) || !out_count) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    uint64_t wb = zt_tok_work_bytes(len);
    size_t maxn = len + 2;
    void *work = malloc(wb ? wb : 1);
    int32_t *tmp = malloc(maxn * sizeof *tmp);
    if (!work || !tmp) {
        free(work);
        free(tmp);
        return zxvi_fail(ZXV_E_NOMEM, "tokenizer scratch allocation failed");
    }
    uint64_t n = 0;
    int32_t r = zt_tok_encode(&model->tok, text ? text : (const uint8_t *) "", len,
                              parse_special != 0, tmp, maxn, &n, work, wb);
    free(work);
    if (r != 0) {
        free(tmp);
        return zxvi_fail(ZXV_E_FORMAT, "tokenizer error %d", r);
    }
    *out_count = (size_t) n;
    if (!ids || cap < n) {
        free(tmp);
        return zxvi_fail(ZXV_E_BUFFER, "ids needs room for %llu tokens", (unsigned long long) n);
    }
    memcpy(ids, tmp, n * sizeof *tmp);
    free(tmp);
    return zxvi_ok();
}

int32_t zxv_detokenize(const zxv_model *model, const int32_t *ids, size_t count, char *buf,
                       size_t cap, size_t *out_len)
{
    if (!model || (!ids && count) || !out_len) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    for (size_t i = 0; i < count; i++)
        if (ids[i] < 0 || (uint32_t) ids[i] >= model->tok.n_vocab)
            return zxvi_fail(ZXV_E_ARG, "token id %d is out of range", ids[i]);
    /* Size first, then render. */
    uint64_t need = 0;
    size_t probe_cap = count * 64 + 16;
    uint8_t *tmp = NULL;
    int32_t r;
    for (;;) {
        tmp = malloc(probe_cap);
        if (!tmp) return zxvi_fail(ZXV_E_NOMEM, "detokenize scratch allocation failed");
        r = zt_tok_decode(&model->tok, ids, count, tmp, probe_cap, &need);
        if (r != ZT_TOK_ESPACE || probe_cap > ((size_t) 1 << 30)) break;
        free(tmp);
        probe_cap *= 4;
    }
    if (r != 0) {
        free(tmp);
        return zxvi_fail(ZXV_E_FORMAT, "detokenize error %d", r);
    }
    int32_t rc = zxvi_out_text_n((const char *) tmp, (size_t) need, buf, cap, out_len);
    free(tmp);
    return rc;
}

int32_t zxv_session_create(const zxv_model *model, uint32_t n_ctx, int32_t kv_type,
                           zxv_session **out_session)
{
    if (!model || !out_session) return zxvi_fail(ZXV_E_ARG, "model or out_session is NULL");
    if (kv_type != ZXV_KV_Q16 && kv_type != ZXV_KV_Q8)
        return zxvi_fail(ZXV_E_ARG, "kv_type must be ZXV_KV_Q16 or ZXV_KV_Q8");
    if (n_ctx == 0) {
        n_ctx = model->m.cfg.n_ctx_train ? model->m.cfg.n_ctx_train : 4096;
        if (n_ctx > 4096) n_ctx = 4096;
    }
    if (n_ctx > ZT_MODEL_MAX_CTX) return zxvi_fail(ZXV_E_RANGE, "n_ctx exceeds %u", ZT_MODEL_MAX_CTX);
    const uint32_t n_batch = 32;
    uint64_t bytes = zt_model_state_bytes(&model->m, n_ctx, n_batch, (uint32_t) kv_type);
    zxv_session *S = calloc(1, sizeof *S);
    void *mem = bytes ? malloc(bytes) : NULL;
    if (!S || !mem) {
        free(S);
        free(mem);
        return zxvi_fail(ZXV_E_NOMEM, "cannot allocate the %llu-byte KV cache",
                         (unsigned long long) bytes);
    }
    int32_t r = zt_model_state_init(&S->s, &model->m, n_ctx, n_batch, (uint32_t) kv_type, mem, bytes);
    if (r != 0) {
        free(S);
        free(mem);
        return model_err(r, NULL, "session");
    }
    S->model = model;
    S->mem = mem;
    *out_session = S;
    return zxvi_ok();
}

void zxv_session_destroy(zxv_session *session)
{
    if (!session) return;
    free(session->mem);
    free(session);
}

int32_t zxv_session_reset(zxv_session *session)
{
    if (!session) return zxvi_fail(ZXV_E_ARG, "session is NULL");
    zt_model_state_seek(&session->s, 0);
    return zxvi_ok();
}

int32_t zxv_session_position(zxv_session *session, uint32_t *out_pos)
{
    if (!session || !out_pos) return zxvi_fail(ZXV_E_ARG, "session or out_pos is NULL");
    *out_pos = session->s.pos;
    return zxvi_ok();
}

int32_t zxv_session_bind_budget(zxv_session *session, zxv_rmag *rmag, uint32_t budget)
{
    if (!session) return zxvi_fail(ZXV_E_ARG, "session is NULL");
    session->rmag = rmag;
    session->budget = budget;
    return zxvi_ok();
}

typedef struct {
    zxv_token_fn fn;
    void *user;
    zxv_session *s;
    bool budget_out;
} gen_bridge;

static bool on_tok(void *ctx, int32_t id, const uint8_t *piece, uint32_t len)
{
    gen_bridge *b = ctx;
    if (b->s->rmag && zxvi_rmag_charge(b->s->rmag, b->s->budget, 1) != ZXV_OK) {
        b->budget_out = true;
        return false;
    }
    return b->fn ? b->fn(b->user, id, piece, len) != 0 : true;
}

int32_t zxv_generate(const zxv_cap *cap, zxv_session *session, const uint8_t *prompt,
                     size_t prompt_len, uint32_t max_tokens, int32_t temperature_q16,
                     uint32_t top_k, int32_t top_p_q16, int32_t repeat_penalty_q16,
                     uint32_t repeat_last_n, uint64_t seed, zxv_token_fn on_token, void *user,
                     uint32_t *out_n_prompt, uint32_t *out_n_generated, int32_t *out_stop)
{
    zxv_ctx *cctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_AI_INFERENCE, "generate", &cctx);
    if (rc) return rc;
    if (!session || (!prompt && prompt_len)) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    /* A bound budget is charged on this cap's authority: it must belong to
     * the same context (ABI 1.2), and that context must not be halted. */
    if (session->rmag && zxvi_rmag_ctx(session->rmag) != cctx)
        return zxvi_fail(ZXV_E_CAP, "generate: the bound budget belongs to a different context than the capability");
    if (temperature_q16 < 0 || top_p_q16 < 0 || repeat_penalty_q16 < 0)
        return zxvi_fail(ZXV_E_ARG, "sampling parameters must be >= 0");
    const zxv_model *M = session->model;
    if (session->rmag) {
        /* Charge the prompt up front: count its tokens first. */
        size_t n = 0;
        rc = zxv_tokenize(M, prompt, prompt_len, 1, NULL, 0, &n);
        if (rc != ZXV_E_BUFFER && rc != ZXV_OK) return rc;
        if ((rc = zxvi_rmag_charge(session->rmag, session->budget, n))) return rc;
    }
    zt_sampler_t sp;
    zt_sampler_params_t p;
    memset(&p, 0, sizeof p);
    p.temperature = temperature_q16;
    p.top_k = top_k;
    p.top_p = top_p_q16;
    p.repeat_penalty = repeat_penalty_q16;
    p.repeat_last_n = repeat_last_n;
    p.seed = seed;
    zt_sampler_init(&sp, &p);
    gen_bridge br = {on_token, user, session, false};
    zt_gen_params_t gp;
    memset(&gp, 0, sizeof gp);
    gp.sampler = temperature_q16 == 0 ? NULL : &sp;
    gp.max_tokens = max_tokens;
    gp.parse_special = true;
    gp.on_token = on_tok;
    gp.ctx = &br;
    uint64_t wb = zt_model_gen_work_bytes(&M->m, &M->tok, prompt_len, max_tokens);
    void *work = malloc(wb ? wb : 1);
    if (!work) return zxvi_fail(ZXV_E_NOMEM, "generation scratch allocation failed");
    zt_gen_result_t res;
    memset(&res, 0, sizeof res);
    int32_t r = zt_model_generate(&M->m, &session->s, &M->tok, prompt ? prompt : (const uint8_t *) "",
                                  prompt_len, &gp, work, wb, &res);
    free(work);
    if (r != 0) {
        if (r == ZT_MODEL_ECTX) return zxvi_fail(ZXV_E_CAPACITY, "the prompt does not fit the context");
        return model_err(r, NULL, "generate");
    }
    int32_t stop = br.budget_out ? ZXV_STOP_BUDGET : (int32_t) res.stop;
    if (out_n_prompt) *out_n_prompt = res.n_prompt;
    if (out_n_generated) *out_n_generated = res.n_gen;
    if (out_stop) *out_stop = stop;
    return zxvi_ok();
}

#else /* ZXV_NO_TENSOR */

struct zxv_lora {
    int unused;
};
#define NI return zxvi_fail(ZXV_E_NOTIMPL, "built without the tensor engine (ZXV_NO_TENSOR)")
int32_t zxv_model_open(const zxv_cap *c, const char *a, const char *b, zxv_model **o) { (void) c; (void) a; (void) b; (void) o; NI; }
int32_t zxv_model_open_memory(const zxv_cap *c, const uint8_t *a, size_t al, const uint8_t *b, size_t bl, zxv_model **o) { (void) c; (void) a; (void) al; (void) b; (void) bl; (void) o; NI; }
void zxv_model_destroy(zxv_model *m) { (void) m; }
int32_t zxv_model_info(const zxv_model *m, int32_t k, int64_t *o) { (void) m; (void) k; (void) o; NI; }
int32_t zxv_tokenize(const zxv_model *m, const uint8_t *t, size_t l, int32_t p, int32_t *i, size_t c, size_t *o) { (void) m; (void) t; (void) l; (void) p; (void) i; (void) c; (void) o; NI; }
int32_t zxv_detokenize(const zxv_model *m, const int32_t *i, size_t n, char *b, size_t c, size_t *o) { (void) m; (void) i; (void) n; (void) b; (void) c; (void) o; NI; }
int32_t zxv_session_create(const zxv_model *m, uint32_t n, int32_t k, zxv_session **o) { (void) m; (void) n; (void) k; (void) o; NI; }
void zxv_session_destroy(zxv_session *s) { (void) s; }
int32_t zxv_session_reset(zxv_session *s) { (void) s; NI; }
int32_t zxv_session_position(zxv_session *s, uint32_t *o) { (void) s; (void) o; NI; }
int32_t zxv_session_bind_budget(zxv_session *s, zxv_rmag *r, uint32_t b) { (void) s; (void) r; (void) b; NI; }
int32_t zxv_generate(const zxv_cap *c, zxv_session *s, const uint8_t *p, size_t pl, uint32_t m, int32_t t, uint32_t k, int32_t tp, int32_t rp, uint32_t rl, uint64_t sd, zxv_token_fn f, void *u, uint32_t *a, uint32_t *b, int32_t *d) { (void) c; (void) s; (void) p; (void) pl; (void) m; (void) t; (void) k; (void) tp; (void) rp; (void) rl; (void) sd; (void) f; (void) u; (void) a; (void) b; (void) d; NI; }

#endif

/* LoRA is declared in the ABI but not implemented by the engine yet. */
int32_t zxv_lora_load(const zxv_cap *cap, zxv_model *model, const char *path, int32_t scale_q16,
                      zxv_lora **out_lora)
{
    (void) cap;
    (void) model;
    (void) path;
    (void) scale_q16;
    if (out_lora) *out_lora = NULL;
    return zxvi_fail(ZXV_E_NOTIMPL, "LoRA adapters are not implemented by the integer engine yet");
}

int32_t zxv_lora_apply(const zxv_cap *cap, zxv_session *session, zxv_lora *lora)
{
    (void) cap;
    (void) session;
    (void) lora;
    return zxvi_fail(ZXV_E_NOTIMPL, "LoRA adapters are not implemented by the integer engine yet");
}

void zxv_lora_destroy(zxv_lora *lora)
{
    (void) lora;
}
