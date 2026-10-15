/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_model_host.h — the hosted app's model slot: maps one GGUF file and
 * joins it to the kernel tensor engine (kernel/src/tensor).
 *
 *   M1 MAP.    The file is memory-mapped read-only (mmap, or a Windows file
 *      mapping); nothing is copied. zt_gguf_open checks every extent before
 *      anything else reads it, so a truncated or hostile file is refused.
 *   M2 TOKENS. The model's own byte-level BPE tokenizer (zt_tok) is loaded
 *      from the file. SentencePiece and WordPiece files load as "metadata
 *      only" and say so.
 *   M3 DECODE. Text generation needs the forward pass (kernel/src/tensor/
 *      zt_model*). The build compiles it in when it exists. The call into
 *      it lives in one glue file, zxv_zt_glue.c, compiled (with
 *      -DZXV_HAVE_ZT_GLUE) only when both zt_model.c and that file exist;
 *      its contract is zxv_zt_generate below. Without it the slot still
 *      maps, checks and tokenizes, and /api/ask says plainly that this
 *      build cannot generate.
 *   M4 PICK.   With no explicit --model, the first *.gguf (by name) in the
 *      models directory is used. No model at all is a normal state: the
 *      swarm runs without one.
 *   M6 SWARM.  Several models at once, beside the single slot above:
 *      ZXV_SLOTS base models, each mapped and loaded once, and ZXV_AGENTS
 *      agents, each running on one slot with its own KV cache and its own
 *      LoRA adapter (llama.cpp's adapter GGUF; kernel/src/tensor/zt_model.h
 *      T23) or none. Agents on one slot share its weights: an agent costs
 *      its KV cache (about 117 MB for Qwen2.5-1.5B at the 2048-token
 *      context) plus its adapter. Adapters are read once into the agent's
 *      own memory, so the adapter file can change or go away afterwards.
 *      Replies are bit-identical on every machine, as for the single slot.
 *      One request at a time across all agents (the host serialises).
 *      Adapters, like models, must carry a licence that allows the use
 *      they are put to; nothing here checks that.
 */
#ifndef ZXV_MODEL_HOST_H
#define ZXV_MODEL_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zt_gguf.h"
#include "zt_tok.h"

typedef struct {
    bool loaded;       /* mapped and its header parsed */
    bool tok_ok;       /* its tokenizer is supported and loaded */
    bool can_generate; /* the forward pass is compiled in */
    char path[1024];
    char name[96];      /* general.name, or the file name */
    char arch[32];      /* general.architecture */
    char tokenizer[24]; /* tokenizer.ggml.model */
    uint64_t bytes, n_tensors;
    int64_t n_layers, n_ctx, n_embd;
    uint32_t n_vocab;
    char status[320]; /* one honest line for the window */
} zxv_model_info_t;

/* Map and check one GGUF file (replacing any open one). 0 or negative. */
int zxv_model_open(const char *path);
/* The first *.gguf in dir, by byte order of the name. 0, or -1 if none. */
int zxv_model_find_in_dir(const char *dir, char *out, size_t cap);
const zxv_model_info_t *zxv_model_info(void);
/* Answer a prompt with the model: 1 when out holds a model answer, 0 when
 * the caller should answer without one (out then holds a note about the
 * model, possibly empty). */
int zxv_model_answer(const char *prompt, char *out, size_t cap);
void zxv_model_close(void);

/* M5 BUDGET. The same, with the generation handed to `run` (the hosted app
 * passes the swarm budget gate, zxv_budget_gate.h): run(rctx, gen, gctx)
 * must call gen(gctx, max_new) at most once and return what it returned; if
 * it returns without calling gen (the budget is spent), the slot reports
 * ZXV_MODEL_BUDGET_EXHAUSTED. Returns 1 (answered), 0
 * (no model answer; out holds a note) or ZXV_MODEL_BUDGET_EXHAUSTED (the
 * model did not run; out is empty). run NULL behaves as zxv_model_answer. */
#ifndef ZXV_GEN_FN_DEFINED
#    define ZXV_GEN_FN_DEFINED
typedef int32_t (*zxv_gen_fn)(void *ctx, uint32_t max_new);
#endif
typedef int32_t (*zxv_gen_runner_fn)(void *rctx, zxv_gen_fn gen, void *gctx);
#define ZXV_MODEL_BUDGET_EXHAUSTED 2
#define ZXV_MODEL_MAX_NEW          256u
int zxv_model_answer_ex(const char *prompt, zxv_gen_runner_fn run, void *rctx, char *out,
                        size_t cap);
/* The prompt's length in tokens (BOS included) for the answer in progress
 * (the single slot's, or an agent's, M6):
 * set before run is called, so the budget gate can charge it
 * (zxv_budget_gate.h B5); 0 before any prompt was tokenized. */
uint64_t zxv_model_prompt_tokens(void);

/* M6: slot and agent numbers are 0..ZXV_SLOTS-1 and 0..ZXV_AGENTS-1. */
#define ZXV_SLOTS  12u
#define ZXV_AGENTS 16u
/* Map and check a model into a slot (replacing it and closing its agents).
 * 0 or -1; zxv_slot_info(slot)->status says why. */
int zxv_slot_open(uint32_t slot, const char *path);
void zxv_slot_close(uint32_t slot);                   /* closes its agents first */
const zxv_model_info_t *zxv_slot_info(uint32_t slot); /* null when out of range */
/* Start an agent on a slot, with a LoRA adapter file (null or "": none) at
 * scale_q16 (65536 = 1.0; 0 means 1.0). Replaces any agent of that number.
 * 0, or -1 with the reason in err. */
int zxv_agent_open(uint32_t agent, uint32_t slot, const char *lora_path, uint32_t scale_q16,
                   char *err, size_t err_cap);
void zxv_agent_close(uint32_t agent);
int zxv_agent_slot(uint32_t agent); /* its slot, or -1 when not open */
/* As zxv_model_answer(_ex), for one agent. */
int zxv_agent_answer(uint32_t agent, const char *prompt, char *out, size_t cap);
int zxv_agent_answer_ex(uint32_t agent, const char *prompt, zxv_gen_runner_fn run, void *rctx,
                        char *out, size_t cap);

/* Forward-pass glue (zxv_zt_glue.c, M3). Generate a reply to the prompt's
 * token ids into out (UTF-8, NUL-terminated). Returns the number of tokens
 * generated, or negative with a reason in err. */
int32_t zxv_zt_generate(const zt_gguf_t *g, const zt_tok_t *tok, const int32_t *ids, uint64_t n_ids,
                        char *out, size_t cap, char *err, size_t err_cap);
/* The same with an explicit limit on new tokens (zxv_zt_generate uses 256). */
int32_t zxv_zt_generate_n(const zt_gguf_t *g, const zt_tok_t *tok, const int32_t *ids,
                          uint64_t n_ids, uint32_t max_new, char *out, size_t cap, char *err,
                          size_t err_cap);
/* What the last zxv_zt_generate(_n) call saw (zxv_zt_glue.c G5): margins
 * are top-1 minus runner-up logit (Q16) summed over the first n_first and
 * the last n_second generated tokens; n_sat counts logits on the Q16 rails;
 * forward_error is set when eval or decode failed. All zero before the first
 * call and after a call refused before the forward pass. */
typedef struct {
    uint32_t n_gen, n_first, n_second;
    uint64_t margin_first, margin_second;
    uint32_t n_sat;
    bool forward_error;
} zxv_zt_verdict_t;
void zxv_zt_last_verdict(zxv_zt_verdict_t *out);
/* 1 when the engine can load this file's weights (architecture, shapes and
 * weight types supported), else 0. Reads metadata only. */
int zxv_zt_can_run(const zt_gguf_t *g);
/* Free the loaded weights and KV cache (the slot calls it on close). */
void zxv_zt_release(void);
/* What runs the loaded model (zxv_zt_glue.c G7, zt_choose.h), measured:
 * "metal: Apple M3 Max (prompts and steps)", "vulkan: ... (prompts);
 * avx512vnni/8 (steps)", "neon/8", ...; "" before a model is loaded. */
const char *zxv_zt_gpu(void);
/* The measured record behind that choice (one line; ZT_CHOICE replays it). */
const char *zxv_zt_choice(void);

/* G6 (M6): a base loaded once, agents with their own KV cache and adapter. */
typedef struct zxv_zt_base zxv_zt_base_t;
typedef struct zxv_zt_agent zxv_zt_agent_t;
/* Load the weights of g (which must outlive the base). 0 or negative. */
int32_t zxv_zt_base_load(zxv_zt_base_t **out, const zt_gguf_t *g, char *err, size_t err_cap);
/* Free a base; -1 (and nothing freed) while agents still run on it. */
int32_t zxv_zt_base_free(zxv_zt_base_t *b);
/* As zxv_zt_gpu and zxv_zt_choice, for a base. */
const char *zxv_zt_base_gpu(const zxv_zt_base_t *b);
const char *zxv_zt_base_choice(const zxv_zt_base_t *b);
/* Whole forward passes and matrix products b's GPU has run so far (0 and 0
 * on the CPU). */
void zxv_zt_base_gpu_work(const zxv_zt_base_t *b, uint64_t *passes, uint64_t *products);
/* An agent on b with the LoRA adapter in lora (null: none) at scale_q16. The
 * adapter is copied: lora need not outlive the call. 0 or negative. */
int32_t zxv_zt_agent_new(zxv_zt_agent_t **out, zxv_zt_base_t *b, const zt_gguf_t *lora,
                         uint32_t scale_q16, char *err, size_t err_cap);
void zxv_zt_agent_free(zxv_zt_agent_t *a);
bool zxv_zt_agent_has_lora(const zxv_zt_agent_t *a);
/* As zxv_zt_generate_n, on the agent's base, cache and adapter; each request
 * starts from an empty cache. Sets the verdict (G5). */
int32_t zxv_zt_agent_generate(zxv_zt_agent_t *a, const zt_tok_t *tok, const int32_t *ids,
                              uint64_t n_ids, uint32_t max_new, char *out, size_t cap, char *err,
                              size_t err_cap);

#endif /* ZXV_MODEL_HOST_H */
