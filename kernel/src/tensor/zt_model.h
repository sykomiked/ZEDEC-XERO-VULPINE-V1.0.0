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
 *       the norms and biases. Q4_K and Q6_K matrices are used in place too
 *       (T22). Matrices of other types (F32, F16, BF16, Q4_0, Q5_0, Q5_1,
 *       and Q4_K/Q6_K under ZT_LOAD_CONVERT) are converted once into zt_q8_t
 *       blocks in the caller's arena, with a 25-bit scale per block (value =
 *       q * scale >> shift).
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
 *   T22 K-QUANTS IN PLACE.  A Q4_K or Q6_K matrix whose rows are a whole
 *       number of 256-weight blocks is read straight from the file, with no
 *       arena and no second rounding: each 32-weight sub-block meets one
 *       activation block in exact 32-bit sums, then its 6-bit (Q4_K) or
 *       int8 (Q6_K) scale and the block's f16 d (and dmin) are applied in 64
 *       bits as for Q8_0. Q4_K's min term uses the activation block's exact
 *       sum, which act_quant forms once per input. zt_kern.h holds the C
 *       reference; the AVX2, AVX-512 and NEON kernels match it bit for bit
 *       (test_zt_simd), and the C reference matches exact math (ggml's
 *       dequantization in 128-bit integers) to within the Q16 rounding.
 *       Measured on Qwen2.5-1.5B Q4_K_M (bench/results/2026-10-10-qwen2.5-
 *       1.5b/REPORT.md): logits about 60 times closer (by KL) to a float64 forward
 *       pass than the converted path, load in well under a second instead of
 *       about 36, and faster decoding. ZT_LOAD_CONVERT keeps the old path.
 *   T23 FILTERS AND PROBES.  zt_gen_params_t.filter constrains every choice:
 *       before each token a mask callback marks the tokens that may come
 *       next (zt_gram.h's grammar hook is one), and zt_sample_masked /
 *       zt_argmax_masked choose among those only, draw for draw the same as
 *       the unmasked sampler when every token is allowed. on_hidden gets
 *       each generated token's probe input: the final hidden state
 *       (zt_model_hidden) or a layer's residual stream (zt_model_set_tap),
 *       for zt_evh.h's evidence head. Neither changes a logit.
 *       HONEST LIMITS.  Tested only on tiny random models (2 layers, 64
 *       wide) against a float64 numpy forward pass of llama.cpp's semantics;
 *       since then one real checkpoint, Qwen2.5-1.5B Q4_K_M, has been run
 *       and measured (T22's report); other models are unmeasured. On the
 *       test models (logits of standard deviation 3.4 to 5.1), with the Q16
 *       KV cache:
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
 *   T23 TENSOR PARALLEL (HPC slice 1).  When zt_model_t.dist is set, the
 *       forward pass runs as one of `size` ranks that call zt_model_eval with
 *       the same arguments in lockstep (SPMD). Every matrix product is split
 *       by OUTPUT ROWS (zt_dist_part): rank r computes only its rows, with
 *       the same kernel and the same full input as a single process would,
 *       then dist->allgather gives every rank every row. Attention is split
 *       the same way by query head. Nothing is ever summed across ranks
 *       except the saturation count (an integer), so each value is computed
 *       whole by one rank and the logits are bit for bit those of one
 *       process, for every rank count, kernel set and thread count
 *       (test_zt_dist). The rest of the step (norms, RoPE, the KV cache,
 *       SiLU, residuals) is repeated on every rank, so ranks stay identical
 *       without a broadcast. A rank reads only its rows of each weight
 *       matrix, so with the file mapped (not read) only about 1/size of the
 *       weights becomes resident per rank. s->dist_macs counts the
 *       multiply-adds this rank did in matrix products: an exact integer
 *       meter for settling shared compute between nodes. HONEST LIMITS: the
 *       KV cache and the per-token steps are replicated (no memory saving
 *       there); communication is two all-gathers of each activation per
 *       sublayer, more bytes than a reduce-scatter design, chosen because a
 *       reduction would change rounding unless it carried exact 64-bit sums;
 *       the transport is the caller's (tensor/dist/zt_dist.h: sockets, MPI).
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
    ZT_MODEL_EARG = -36,     /* bad argument (token id, count) */
    ZT_MODEL_EDIST = -37     /* the tensor-parallel transport failed (T23) */
};

#define ZT_MODEL_MAX_LAYERS 512u
#define ZT_MODEL_MAX_DIM    32768u /* longest matrix row (n_embd, n_ff, n_head * head_dim) */
#define ZT_MODEL_MAX_HEAD   512u
#define ZT_MODEL_MAX_CTX    65535u
#define ZT_MMB_MIN          4u /* fewest tokens for the batched matrix kernels */
#define ZT_ATT_TILES        4u /* attention work items per head (token tiles) */

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

/* A matrix of `rows` rows of `cols` values: GGUF Q8_0, Q4_K or Q6_K blocks in
 * place (raw, qtype), or zt_q8_t blocks in the arena (q8). Exactly one is set. */
typedef struct {
    const uint8_t *raw;
    const zt_q8_t *q8;
    uint32_t rows, cols;
    uint32_t qtype; /* raw's ZT_GGML_* type: Q8_0, Q4_K or Q6_K (0 when q8 is set) */
    /* zt_nn.h (T23) only: the 8-bit blocks of what q8 leaves out of a float
     * matrix (weight = q8 + q8lo), or null. zt_model never sets it. */
    const zt_q8_t *q8lo;
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

/* T23: one rank of a tensor-parallel group. allgather is called with y
 * holding, for each of B vectors (y + b * ystride), this rank's part of n
 * values: units [u0, u1) of zt_dist_part(n / unit, size, rank), that is
 * values [u0 * unit, u1 * unit). On return every rank's y holds all n values
 * of all B vectors, and *count is the sum of every rank's *count. It returns
 * 0, or nonzero when the transport failed (then the forward pass returns
 * ZT_MODEL_EDIST). Every rank must make the same calls in the same order. */
typedef struct zt_dist {
    uint32_t rank, size;
    int (*allgather)(void *ctx, zt_fx *y, uint32_t ystride, uint32_t B, uint32_t n, uint32_t unit,
                     uint32_t *count);
    void *ctx;
} zt_dist_t;

/* Rank r of size takes units [n * r / size, n * (r + 1) / size). */
static inline void zt_dist_part(uint32_t n, uint32_t size, uint32_t rank, uint32_t *u0,
                                uint32_t *u1)
{
    *u0 = (uint32_t) zt_udiv64((uint64_t) n * rank, size, 0);
    *u1 = (uint32_t) zt_udiv64((uint64_t) n * (rank + 1), size, 0);
}

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
    /* T23: null (zt_model_load's default) runs alone; set, this process is
     * one rank of a tensor-parallel group. Hosted builds set it. */
    const zt_dist_t *dist;
} zt_model_t;

/* Read and check the configuration only (no tensor data is touched). */
int32_t zt_model_config(const zt_gguf_t *g, zt_model_cfg_t *cfg, zt_model_err_t *err);
/* Arena bytes zt_model_load needs for this file, or 0 if it cannot be loaded
 * (zt_model_config says why). Includes 64 bytes of alignment slack. */
uint64_t zt_model_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the model (Q8_0, Q4_K and Q6_K weights are
 * used in place). */
int32_t zt_model_load(zt_model_t *m, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                      zt_model_err_t *err);
/* The same with ZT_LOAD_* flags. ZT_LOAD_CONVERT converts Q4_K and Q6_K
 * matrices into zt_q8_t blocks in the arena as before T22 (for experiments
 * that rewrite weights, and to compare); the default uses them in place. */
#define ZT_LOAD_CONVERT 1u
uint64_t zt_model_arena_bytes_ex(const zt_gguf_t *g, uint32_t flags);
int32_t zt_model_load_ex(zt_model_t *m, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                         uint32_t flags, zt_model_err_t *err);

/* Per-sequence state: the KV cache and the activation scratch for batches of
 * up to n_batch tokens. */
#define ZT_LORA_MAX_RANK 256u

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
    int32_t *asum;                             /* [n_batch][a_stride / 32] block sums of a16 */
    uint32_t a_stride;
    /* Batched matrix products (zt_kern.h zt_mmb_t), used when the kernel set
     * has mm_batch and a batch holds at least ZT_MMB_MIN tokens: a16, ash
     * and asum repacked token-major for the mmb_cols-wide input (0: none). */
    int16_t *aT;
    int64_t *ashT, *asumT;
    uint8_t *ashx;
    uint32_t mmb_cols;
    /* Attention scratch for each of n_head * att_tiles work items (the items
     * may run in threads; each writes only its own). att_tiles is
     * ZT_ATT_TILES, or n_batch when that is smaller. */
    uint32_t att_tiles;
    zt_fx *scores; /* [items][n_ctx] */
    int64_t *acc;  /* [items][head_dim] */
    int32_t *qs;   /* [items][head_dim] */
    /* Values that hit the Q16 range limit (+-32768) since zt_model_state_init:
     * matrix-product outputs and residual-stream additions. Same count on
     * every kernel and thread count. 0 means nothing clipped. */
    uint32_t n_sat;
    /* The last token's hidden state after the final RMS norm (the input of
     * the output matrix), n_embd values, from the latest eval that asked for
     * logits; valid until the next eval (zt_model_hidden). */
    const zt_fx *hid;
    /* Optional probe tap (zt_model_set_tap): after layer tap_layer, the
     * residual stream of each eval's last token is copied to tap. */
    zt_fx *tap;
    int32_t tap_layer;
    /* T23: the LoRA adapter this sequence runs with (null: the base model
     * alone), set by zt_model_state_set_lora, and its scratch: the rank-sized
     * intermediate of each of n_batch tokens, quantised as act_quant does. */
    const struct zt_lora *lora;
    zt_fx *lt;      /* [n_batch][ZT_LORA_MAX_RANK] */
    int16_t *la16;  /* [n_batch][ZT_LORA_MAX_RANK] */
    uint8_t *lash;  /* [n_batch][ZT_LORA_MAX_RANK / 32] */
    int32_t *lasum; /* [n_batch][ZT_LORA_MAX_RANK / 32] */
    /* T23: multiply-adds this rank computed in matrix products since
     * zt_model_state_init (the whole product when running alone). */
    uint64_t dist_macs;
} zt_model_state_t;

uint64_t zt_model_state_bytes(const zt_model_t *m, uint32_t n_ctx, uint32_t n_batch,
                              uint32_t kv_type);
int32_t zt_model_state_init(zt_model_state_t *s, const zt_model_t *m, uint32_t n_ctx,
                            uint32_t n_batch, uint32_t kv_type, void *mem, uint64_t bytes);
/* Forget everything after the first pos tokens (0 empties the cache). */
void zt_model_state_seek(zt_model_state_t *s, uint32_t pos);
/* The last token's final hidden state (n_embd Q16 values, after the output
 * norm), or null before any eval with logits. */
const zt_fx *zt_model_hidden(const zt_model_state_t *s);
/* Copy the residual stream after layer `layer` (0-based) of each eval's
 * last token into buf (n_embd values); layer < 0 or buf null turns it off.
 * EARG for a layer the model does not have. The output bits do not change. */
int32_t zt_model_set_tap(zt_model_state_t *s, const zt_model_t *m, int32_t layer, zt_fx *buf);

#define ZT_EVAL_ALL_LOGITS 1u
/* Run n tokens at positions s->pos.. and append them to the cache. logits
 * (Q16) receives n_vocab values for the last token, or n * n_vocab for every
 * token with ZT_EVAL_ALL_LOGITS; it may be null. Tokens go through in
 * batches of n_batch. */
int32_t zt_model_eval(const zt_model_t *m, zt_model_state_t *s, const int32_t *tokens, uint32_t n,
                      zt_fx *logits, uint32_t flags);
int32_t zt_model_decode(const zt_model_t *m, zt_model_state_t *s, int32_t token, zt_fx *logits);
/* T23: the same with n_embd Q16 values per position in place of token ids,
 * for an image or audio encoder's output (zt_vision.h); embd holds n rows.
 * The rest of the pass is unchanged, so mixing token and embedding runs in
 * one sequence gives what one combined run would. */
int32_t zt_model_eval_embd(const zt_model_t *m, zt_model_state_t *s, const zt_fx *embd, uint32_t n,
                           zt_fx *logits, uint32_t flags);
/* A token's input embedding (n_embd Q16 values), as eval uses it. */
int32_t zt_model_embed(const zt_model_t *m, int32_t token, zt_fx *out);

/* ---- T23 LoRA adapters: one shared base, a different adapter per sequence ----
 *
 * A LoRA adapter adds scale * B (A x) to the output of a base matrix W
 * (y = W x + scale * B A x), A of rank rows by W's columns, B of W's rows by
 * rank. zt_lora_load reads llama.cpp's adapter GGUF (convert_lora_to_gguf.py:
 * general.type "adapter", adapter.type "lora", adapter.lora.alpha, tensors
 * "blk.<l>.<matrix>.weight.lora_a" and ".lora_b" of ggml shapes
 * [cols, rank] and [rank, rows]) for any of the seven per-layer matrices
 * (attn_q, attn_k, attn_v, attn_output, ffn_gate, ffn_up, ffn_down). The
 * scale is alpha / rank times the caller's scale (llama.cpp's
 * adapter_scale), or the caller's scale alone when alpha is 0 or absent.
 *   WEIGHTS.  F32, F16, BF16 and Q8_0 adapters are read exactly into 48
 *       fraction bits (the scale is folded into B there), then stored as
 *       zt_q8_t blocks with the same 25-bit scales as converted base
 *       weights; small adapter values keep their relative precision because
 *       each block carries its own shift. B's rank is padded with zeros to
 *       a multiple of 32. The adapter's memory is the caller's arena; the
 *       adapter file is not used after zt_lora_load.
 *   APPLY.  After each base product, for each token: t = A x with the same
 *       16-bit activation blocks the base product read (Q16 result), t is
 *       quantised to 16-bit blocks again, then B t is added to y in Q16 and
 *       saturated (counted in n_sat). Every step is zt_kern.h's integer dot
 *       (C reference, or the model's kernel set, which gives the same bits),
 *       so the logits are the same on every machine, thread count and
 *       batching, as for the base model.
 *   SHARING.  The base zt_model_t is read-only; any number of sequences
 *       (zt_model_state_t, one per agent) can use it at once, each with its
 *       own adapter or none. An adapter is read-only too and can be shared.
 *   LIMITS.  Rank 1..ZT_LORA_MAX_RANK. No adapters on token_embd or output
 *       (refused, not ignored), no DoRA or other variants, no merging into
 *       the base weights. Changing a sequence's adapter does not recompute
 *       its KV cache: set it on an empty state (or seek to 0 after). Adapter
 *       weights are rounded to 8 bits per block of 32, like converted base
 *       weights; on the tiny test models that alone moves the logits by up
 *       to 0.11 against a float64 forward pass of the merged weights (numpy
 *       simulation), and the engine measures 0.04 to 0.15 (test_zt_lora).
 *       No real adapter on a real model has been run yet. */
typedef struct {
    const zt_q8_t *a; /* rank rows of cols / 32 blocks */
    const zt_q8_t *b; /* rows rows of rpad / 32 blocks, the scale folded in */
    uint32_t rank, rpad, rows, cols;
} zt_lora_mat_t;

#define ZT_LORA_MATS 7u /* per layer: wq wk wv wo wg wu wd */

typedef struct zt_lora {
    uint32_t n_layer;    /* the base model's */
    uint32_t n_mats;     /* matrices with an adapter */
    uint32_t max_rank;   /* largest rank */
    zt_lora_mat_t *mats; /* [n_layer][ZT_LORA_MATS] in the arena; rank 0 = none */
    uint32_t alpha_f32;  /* f32 bits of adapter.lora.alpha (0 when absent) */
    uint32_t scale_q16;  /* the caller's scale */
} zt_lora_t;

/* Arena bytes zt_lora_load needs for this adapter on this base, or 0 when it
 * cannot be applied (zt_lora_load says why). */
uint64_t zt_lora_arena_bytes(const zt_model_t *m, const zt_gguf_t *ad);
/* scale_q16: the caller's scale (65536 = 1.0); 0 is refused. */
int32_t zt_lora_load(zt_lora_t *lo, const zt_model_t *m, const zt_gguf_t *ad, uint32_t scale_q16,
                     void *arena, uint64_t arena_bytes, zt_model_err_t *err);
/* Run this sequence with lo (null: the base alone). lo must have been loaded
 * for the same base model. Returns ZT_MODEL_EARG on a mismatch. */
int32_t zt_model_state_set_lora(zt_model_state_t *s, const zt_model_t *m, const zt_lora_t *lo);

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
/* The same choices among the tokens whose bit is set in allow ((n + 7) / 8
 * bytes, bit i = token i; null = every token): the others are removed
 * before top-k, top-p and the draw, so the allowed tokens keep their
 * probability ratios. With every bit set the result is zt_sample's (and
 * zt_argmax's), draw for draw. EARG when no token is allowed. The
 * repetition penalty still reads the whole history. */
int32_t zt_sample_masked(zt_sampler_t *sp, const zt_fx *logits, uint32_t n_vocab,
                         const int32_t *history, uint32_t n_history, const uint8_t *allow,
                         void *work, uint64_t work_bytes);
int32_t zt_argmax_masked(const zt_fx *logits, uint32_t n, const uint8_t *allow);

/* ---- generate ---- */
enum {
    ZT_GEN_EOS = 1,      /* tok->eos or a caller stop id (not passed to the callback) */
    ZT_GEN_MAX = 2,      /* max_tokens reached */
    ZT_GEN_STOP_STR = 3, /* the output ends with a stop string */
    ZT_GEN_CALLBACK = 4, /* on_token returned false */
    ZT_GEN_CTX = 5,      /* the KV cache is full */
    ZT_GEN_FILTER = 6    /* the filter allowed no token (a finished grammar with no end token) */
};

/* A token filter, such as a grammar (zt_gram.h: zt_gram_hook_mask and
 * zt_gram_hook_accept with a zt_gram_hook_t). Before each choice, mask
 * gets a cleared bitmap of (n_vocab + 7) / 8 bytes, sets the bits of the
 * tokens that may come next and returns how many (< 0: an error, which
 * generate returns). After a token other than an end token is chosen,
 * accept is told (< 0: an error). */
typedef struct {
    int32_t (*mask)(void *ctx, uint8_t *allow, uint32_t n_vocab);
    int32_t (*accept)(void *ctx, int32_t id);
    void *ctx;
} zt_gen_filter_t;

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
    /* Optional: constrain every choice (mask null = off). */
    zt_gen_filter_t filter;
    /* Optional: called for each generated token (not an end token), before
     * on_token, with the probe input that chose it: the tap if one is set
     * (zt_model_set_tap), else the final hidden state; n = n_embd; n_sat =
     * the state's saturation count so far. For an evidence head (zt_evh.h:
     * zt_evh_hook with a zt_evh_hook_t). */
    void (*on_hidden)(void *hctx, int32_t id, const zt_fx *h, uint32_t n, uint32_t n_sat);
    void *hctx;
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
