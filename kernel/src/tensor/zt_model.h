/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_model.h — a whole language model, run in integers: the GGUF file, the
 * tokenizer and rotary positions joined into one forward pass, plus sampling
 * and a generate loop.
 *
 *   T21 THE FORWARD PASS.  zt_model_load reads a qwen2, qwen3 or llama GGUF
 *       file (Qwen2.5, Qwen3, Llama 3.x and their relatives) and checks every
 *       tensor's name, type and shape before anything runs. Q8_0 matrices are
 *       used in place from the mapped file: each 34-byte block's f16 scale is
 *       decoded as it is used, so a Q8_0 model costs no extra memory beyond
 *       the norms and biases. Matrices of other types (F32, F16, BF16, Q4_0,
 *       Q5_0, Q5_1, Q4_K, Q6_K) are converted once into zt_q8_t blocks in
 *       the caller's arena, with a 25-bit scale per block (value = q *
 *       scale >> shift).
 *       All memory is the caller's: zt_model_arena_bytes for the weights,
 *       zt_model_state_bytes for the KV cache and the activations.
 *       Per layer, in llama.cpp's order: RMS norm; Q, K, V projections (plus
 *       biases when the file has them, as Qwen2 does); Qwen3's per-head RMS
 *       norm of Q and K; RoPE (NEOX pairs for Qwen, interleaved for Llama,
 *       and the rope_freqs divisors when present); causal attention over the
 *       KV cache, query head h reading KV head h / (n_head / n_head_kv) (GQA);
 *       the output projection and the residual; RMS norm; the SwiGLU MLP
 *       down(silu(gate x) * up x) and the residual. Then the final norm and
 *       the logits (the output matrix, or token_embd when tied).
 *       Arithmetic: activations are Q16.16 (T1). A matrix product turns its
 *       input into 16-bit blocks of 32 with a power-of-two exponent each
 *       (error at most 2^-16 of the block's largest value), multiplies them
 *       by the 8-bit weights in exact 32-bit sums per block, and WIDENS TO
 *       64 BITS when it applies the block's two scales; the row sum is kept in
 *       Q32 and rounded to Q16 once. Attention: scores are q.k in 64 bits
 *       (Q32) times 1/sqrt(head_dim) in Q30; softmax weights are e^(s - max)
 *       in Q16 (zt_exp), summed in 64 bits; the weighted sum of V is exact in
 *       64 bits and divided by the weight sum once per element, rounded to
 *       nearest. RMS norm sums squares in 64 bits (pre-shifted so n * max^2
 *       fits), adds eps from its f32 bits, and takes 1/sqrt to 32 fraction
 *       bits; gains keep up to 24 fraction bits. No floating point, no 128-bit
 *       arithmetic; 64-bit divisions go through zt_udiv64 (a handful per norm
 *       and per attention output), conversions use 32-bit division only.
 *       Every result is deterministic and the same on every machine: a batch
 *       prefill gives bit-for-bit the logits of feeding the tokens one at a
 *       time.
 *       KV CACHE.  ZT_KV_Q16 keeps K and V as Q16 (4 bytes per value, exact).
 *       ZT_KV_Q8 keeps each 32 values (or one head, if shorter) as int8 with
 *       a 25-bit scale: at most half a step, 1/254 of the block's largest
 *       value, per element (about 0.23% of it RMS on uniform data), at about
 *       1.16 bytes per value.
 *       SAMPLING.  Integer only, in llama.cpp's order: repetition penalty on
 *       the last n tokens (a positive logit divided by the penalty, a
 *       negative one multiplied), top-k, top-p (on probabilities at
 *       temperature 1), temperature, then a draw from a splitmix64 stream
 *       seeded by the caller. Probabilities are e^(l - max) in Q32. Ties go
 *       to the lower token id. Temperature 0 is greedy.
 *       GENERATE.  zt_model_generate tokenizes the prompt (zt_tok), prefills
 *       it in batches, then samples and feeds tokens until EOS (or a caller
 *       stop id), a stop string, max_tokens, the end of the context, or the
 *       callback says stop. It appends to whatever the state already holds,
 *       so a chat can continue turn after turn.
 *       HONEST LIMITS.  Tested only on tiny random models (2 layers, 64
 *       wide) against a float64 numpy forward pass of llama.cpp's semantics;
 *       no real Qwen or Llama checkpoint has been run here (downloads were
 *       blocked), so real-model accuracy is unmeasured. On the test models
 *       (logits of standard deviation 3.4 to 5.1), with the Q16 KV cache:
 *       max error 0.0013 for Q8_0 weights; 0.13 for F16 weights, which are
 *       rounded a second time to 8 bits (a numpy simulation of that rounding
 *       alone gives the same size); the Q8 KV cache adds about 0.1 (the
 *       same simulation of int8 K and V gives 0.08 to 0.15). Remaining
 *       error comes from Q16 activations (2^-17 absolute, so very small
 *       embedding values lose relative precision) and Q16 softmax weights
 *       (an attention weight below 2^-16 of the largest, or a sampling
 *       weight below about e^-22, counts as zero). It is not llama.cpp bit
 *       for bit. Values saturate at +-32768: a model whose residual stream
 *       grows past that would clip, and none was checked. Weight types are
 *       zt_gguf's (F32, F16, BF16, Q8_0, Q4_0, Q5_0, Q5_1, Q4_K, Q6_K), so
 *       the Q4_K_M files of models 896 wide (which also hold Q5_0/Q5_1
 *       tensors) load; their Q5 decoding is checked bit for bit against
 *       golden blocks (test_zt_q5), but no real Q4_K_M model has been run.
 *       Converted weights take 1.25 bytes each of arena, and the 8-bit
 *       re-quantisation adds its own rounding on top of the file's.
 *       One thread, scalar C: on one 2.8 GHz Xeon core a 0.5B-size layer
 *       runs at 1.2 (-O2) to 2.8 (-O3) billion multiply-adds a second, which
 *       makes Qwen2.5-0.5B an ESTIMATED 2.5 to 6 tokens a second; not
 *       measured on the real model. Context is at most 65535 tokens (the
 *       exact 64-bit V sum needs it), and attention is O(context) per token
 *       with no flash or paged layout. Not covered: YaRN or other rope
 *       scaling beyond one linear factor; sliding-window, MoE or
 *       multimodal models; a bias on the output projection; K and V head
 *       sizes that differ; SentencePiece tokenizers (zt_tok's limit). Stop
 *       strings are detected after the token that completes them has gone
 *       to the callback; the result says how many trailing bytes to trim.
 * Freestanding: no libc, no malloc, no floating point, no 64-bit division.
 */
#ifndef ZT_MODEL_H
#define ZT_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"
#include "zt_gguf.h"
#include "zt_tok.h"
#include "zt_rope.h"

enum { ZT_ARCH_QWEN2 = 1, ZT_ARCH_QWEN3 = 2, ZT_ARCH_LLAMA = 3 };
enum { ZT_KV_Q16 = 0, ZT_KV_Q8 = 1 };

/* Errors beyond the ZT_GGUF_* codes (which are passed through). */
enum {
    ZT_MODEL_EARCH = -30,    /* architecture not supported */
    ZT_MODEL_EMISSING = -31, /* a required key or tensor is absent */
    ZT_MODEL_ESHAPE = -32,   /* a tensor has the wrong shape */
    ZT_MODEL_ECONFIG = -33,  /* metadata out of range or inconsistent */
    ZT_MODEL_ESPACE = -34,   /* a caller buffer is too small */
    ZT_MODEL_ECTX = -35,     /* the KV cache is full */
    ZT_MODEL_EARG = -36      /* bad argument (token id, count) */
};

#define ZT_MODEL_MAX_LAYERS 512u
#define ZT_MODEL_MAX_DIM    32768u /* longest matrix row (n_embd, n_ff, n_head * head_dim) */
#define ZT_MODEL_MAX_HEAD   512u
#define ZT_MODEL_MAX_CTX    65535u

typedef struct {
    int32_t code;
    const char *what; /* static text: what went wrong */
    char name[64];    /* the key or tensor involved, NUL-terminated ("" if none) */
} zt_model_err_t;

typedef struct {
    uint32_t arch;
    uint32_t n_layer, n_embd, n_ff, n_head, n_head_kv, head_dim, n_rot;
    uint32_t n_vocab, n_ctx_train;
    uint32_t rope_base_f32;   /* f32 bits of rope.freq_base */
    uint32_t rope_linear_f32; /* f32 bits of a linear rope scaling factor, 0 when none */
    uint32_t rms_eps_f32;     /* f32 bits of layer_norm_rms_epsilon */
    bool rope_neox, has_qk_norm, has_qkv_bias, has_rope_freqs, tied_output;
} zt_model_cfg_t;

/* A matrix of `rows` rows of `cols` values: GGUF Q8_0 blocks in place (raw),
 * or zt_q8_t blocks in the arena (q8). Exactly one is set. */
typedef struct {
    const uint8_t *raw;
    const zt_q8_t *q8;
    uint32_t rows, cols;
} zt_mat_t;

typedef struct {
    const int32_t *g; /* gain, value = g / 2^frac */
    uint32_t frac;
} zt_gain_t;

typedef struct {
    zt_mat_t wq, wk, wv, wo, wg, wu, wd;
    const zt_fx *bq, *bk, *bv; /* Q16 biases, or null */
    zt_gain_t attn_norm, ffn_norm, q_norm, k_norm;
} zt_layer_t;

typedef struct {
    zt_model_cfg_t cfg;
    uint32_t q_dim, kv_dim;
    zt_gguf_tensor_t embd; /* token_embd, read a row at a time */
    zt_mat_t out;
    zt_gain_t out_norm;
    zt_layer_t *layers; /* in the arena */
    zt_rope_t rope;
    uint64_t eps_q48;      /* rms epsilon * 2^48 */
    uint32_t inv_sqrt_q30; /* 2^30 / sqrt(head_dim) */
    /* Kernel set for the matrix products (zt_kern.h); zt_model_load sets 0,
     * the portable C reference. A hosted caller may point it at zt_simd.h's
     * SIMD or threaded kernels afterwards: the output bits do not change. */
    const struct zt_kern *kern;
} zt_model_t;

/* Read and check the configuration only (no tensor data is touched). */
int32_t zt_model_config(const zt_gguf_t *g, zt_model_cfg_t *cfg, zt_model_err_t *err);
/* Arena bytes zt_model_load needs for this file, or 0 if it cannot be loaded
 * (zt_model_config says why). Includes 64 bytes of alignment slack. */
uint64_t zt_model_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the model (Q8_0 weights are used in place). */
int32_t zt_model_load(zt_model_t *m, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                      zt_model_err_t *err);

/* Per-sequence state: the KV cache and the activation scratch for batches of
 * up to n_batch tokens. */
typedef struct {
    uint32_t kv_type, n_ctx, n_batch, pos; /* pos = tokens in the cache */
    uint32_t nbh;                          /* KV blocks per head (ZT_KV_Q8) */
    int32_t *k16, *v16;                    /* ZT_KV_Q16: [layer][ctx][kv_dim] */
    int8_t *k8, *v8;                       /* ZT_KV_Q8: [layer][ctx][kv_dim] */
    int32_t *ks, *vs;                      /* ZT_KV_Q8 scales: [layer][ctx][n_head_kv * nbh] */
    uint8_t *ksh, *vsh;
    zt_fx *x, *xn, *q, *k, *v, *att, *gt, *up; /* [n_batch][...] */
    int16_t *a16;                              /* [n_batch][a_stride] */
    uint8_t *ash;                              /* [n_batch][a_stride / 32] */
    uint32_t a_stride;
    zt_fx *scores; /* [n_ctx] */
    int64_t *acc;  /* [head_dim] */
    int32_t *qs;   /* [head_dim] */
} zt_model_state_t;

uint64_t zt_model_state_bytes(const zt_model_t *m, uint32_t n_ctx, uint32_t n_batch,
                              uint32_t kv_type);
int32_t zt_model_state_init(zt_model_state_t *s, const zt_model_t *m, uint32_t n_ctx,
                            uint32_t n_batch, uint32_t kv_type, void *mem, uint64_t bytes);
/* Forget everything after the first pos tokens (0 empties the cache). */
void zt_model_state_seek(zt_model_state_t *s, uint32_t pos);

#define ZT_EVAL_ALL_LOGITS 1u
/* Run n tokens at positions s->pos.. and append them to the cache. logits
 * (Q16) receives n_vocab values for the last token, or n * n_vocab for every
 * token with ZT_EVAL_ALL_LOGITS; it may be null. Tokens go through in
 * batches of n_batch. */
int32_t zt_model_eval(const zt_model_t *m, zt_model_state_t *s, const int32_t *tokens, uint32_t n,
                      zt_fx *logits, uint32_t flags);
int32_t zt_model_decode(const zt_model_t *m, zt_model_state_t *s, int32_t token, zt_fx *logits);

/* ---- sampling ---- */
typedef struct {
    zt_fx temperature;      /* Q16; 0 = greedy */
    uint32_t top_k;         /* 0 = off */
    zt_fx top_p;            /* Q16; 0 or >= 1.0 = off */
    zt_fx repeat_penalty;   /* Q16; 0 or 1.0 = off */
    uint32_t repeat_last_n; /* how many recent tokens the penalty sees */
    uint64_t seed;
} zt_sampler_params_t;

typedef struct {
    zt_sampler_params_t p;
    uint64_t rng;
} zt_sampler_t;

void zt_sampler_init(zt_sampler_t *sp, const zt_sampler_params_t *p);
uint64_t zt_sample_work_bytes(uint32_t n_vocab);
/* Choose a token. history holds the tokens so far (the penalty reads its last
 * repeat_last_n). The logits are not modified. Returns the id, or < 0. */
int32_t zt_sample(zt_sampler_t *sp, const zt_fx *logits, uint32_t n_vocab, const int32_t *history,
                  uint32_t n_history, void *work, uint64_t work_bytes);
/* Highest logit, lowest id on ties. */
int32_t zt_argmax(const zt_fx *logits, uint32_t n);

/* ---- generate ---- */
enum {
    ZT_GEN_EOS = 1,      /* tok->eos or a caller stop id (not passed to the callback) */
    ZT_GEN_MAX = 2,      /* max_tokens reached */
    ZT_GEN_STOP_STR = 3, /* the output ends with a stop string */
    ZT_GEN_CALLBACK = 4, /* on_token returned false */
    ZT_GEN_CTX = 5       /* the KV cache is full */
};

typedef struct {
    const uint8_t *s;
    uint32_t len; /* 1..64 */
} zt_stop_str_t;

typedef struct {
    zt_sampler_t *sampler; /* null = greedy */
    uint32_t max_tokens;
    bool parse_special;      /* special-token text in the prompt becomes those tokens */
    const int32_t *stop_ids; /* extra end tokens, besides tok->eos */
    uint32_t n_stop_ids;
    const zt_stop_str_t *stop_strs;
    uint32_t n_stop_strs;
    /* Called with each generated token and its bytes; return false to stop. */
    bool (*on_token)(void *ctx, int32_t id, const uint8_t *piece, uint32_t len);
    void *ctx;
} zt_gen_params_t;

typedef struct {
    uint32_t n_prompt, n_gen, stop;
    uint32_t stop_trim; /* ZT_GEN_STOP_STR: trailing output bytes that are the stop string */
} zt_gen_result_t;

uint64_t zt_model_gen_work_bytes(const zt_model_t *m, const zt_tok_t *t, uint64_t prompt_len,
                                 uint32_t max_tokens);
int32_t zt_model_generate(const zt_model_t *m, zt_model_state_t *s, const zt_tok_t *t,
                          const uint8_t *prompt, uint64_t prompt_len, const zt_gen_params_t *gp,
                          void *work, uint64_t work_bytes, zt_gen_result_t *res);

#endif /* ZT_MODEL_H */
