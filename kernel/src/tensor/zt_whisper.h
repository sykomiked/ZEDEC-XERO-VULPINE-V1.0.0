/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_whisper.h — speech to text, in integers: OpenAI Whisper's log-mel front
 * end, audio encoder and text decoder.
 *
 *   T25 SPEECH IN.  zt_whisper_load reads the "whisper" GGUF that
 *       bench/tensor/convert_whisper.py writes from the Hugging Face
 *       openai/whisper-* checkpoints (tested: whisper-base, 74M parameters,
 *       Apache-2.0 on Hugging Face, MIT upstream). Q8_0 matrices are used in
 *       place; conv1 (rows of 240 values) is rounded once to 8-bit blocks at
 *       load; norms, biases, position tables and the mel filterbank are F32.
 *       LOG-MEL, as whisper/audio.py: 16 kHz int16 PCM (x = s / 32768),
 *       padded with zeros or cut to n_frames * 160 samples (30 s for every
 *       released model); a 400-point STFT every 160 samples with a periodic
 *       Hann window, centred (200 samples of reflection padding at each end),
 *       the last frame dropped (3000 frames); power |X|^2; mel = filterbank
 *       (80 x 201, librosa's slaney mel) times power; log10(max(mel,
 *       1e-10)); max(x, max over all of x - 8); (x + 4) / 4. In integers:
 *       each frame's windowed samples s * (1 - cos) are formed exactly (64
 *       bits) and shifted down by the fewest bits that keep their absolute
 *       sum below 2^31 (a per-frame block exponent, so quiet frames keep up
 *       to 31 fraction bits and loud ones lose nothing that matters); a
 *       direct real DFT against a Q30 cosine table (gen_whisper_tables.py,
 *       60-digit arithmetic) in exact 64-bit sums (below 2^61), rounded once
 *       to the frame's scale; power below 2^63; the filterbank (Q35 or so,
 *       the most fraction bits that fit 31 bits) applied as an exact 96-bit
 *       sum held in two 64-bit words; log2 of that to 24 fraction bits by
 *       repeated squaring (zt_log2 takes Q16 input and cannot reach 1e-10),
 *       times log10(2) in Q32; the clamps and the (x + 4) / 4 in Q24, then
 *       rounded to Q16. Frames that are all zero cost nothing. The frames
 *       are independent, so a hosted caller's par_rows splits them.
 *       ENCODER: conv1 (n_mels -> d, kernel 3, padding 1) + GELU, conv2
 *       (d -> d, kernel 3, stride 2, padding 1) + GELU, both as im2col and a
 *       matrix product (zt_nn); plus the sinusoidal position table; n_layer
 *       pre-LayerNorm blocks (LayerNorm; Q and V with bias, K without; non-
 *       causal attention; output with bias; residual; LayerNorm; fc1 + GELU
 *       (erf form) + fc2; residual); the final LayerNorm. GELU is zt_nn's Q30
 *       table, attention zt_nn's exact 64-bit scores and sums.
 *       DECODER: token embedding (a Q8_0 row, dequantised) + learned
 *       position; per layer LayerNorm, causal self-attention over a Q16 KV
 *       cache, LayerNorm, cross-attention to the encoder output (its K and V
 *       per layer are computed once per utterance, in zt_whisper_encode),
 *       LayerNorm, MLP (GELU erf); the final LayerNorm; logits against the
 *       token embedding (tied).
 *       GREEDY DECODING without timestamps, as openai-whisper's
 *       DecodingTask at temperature 0 and whisper.cpp's -bs 1 -bo 1 -nt: the
 *       prompt <|startoftranscript|><|lang|><|transcribe|><|notimestamps|>;
 *       at every step the file's suppress_tokens (generation_config.json)
 *       and every special token after <|endoftext|> (start, language, task,
 *       no-speech, timestamps) are excluded; at the first step also
 *       begin_suppress_tokens (" " and <|endoftext|>); the largest logit
 *       wins, the lower id on ties; stop at <|endoftext|>, max_tokens, or
 *       the end of the text context. zt_whisper_detect_lang is the language
 *       detection of both references (the largest language-token logit
 *       after <|startoftranscript|>). Text: zt_tok_load reads the same file
 *       (byte-level BPE, pre-tokenizer "whisper"); zt_tok_decode the ids
 *       below <|endoftext|>.
 *       Every result is the same, bit for bit, on every machine and kernel
 *       set (zt_kern.h's C reference or zt_simd.h's SIMD and threaded sets).
 *       HONEST LIMITS.  One 30-second window: longer audio is cut (the
 *       caller must split it; no seek, no condition on previous text, no
 *       timestamps, no temperature fallback, no beam search, no no-speech
 *       or compression-ratio checks). 16 kHz mono int16 only: no resampling.
 *       Only n_fft 400 and hop 160 (every released Whisper). Not the
 *       reference bit for bit: Q8_0 weights (whisper.cpp's ggml-base.bin
 *       is F16; transformers runs float32), 16-bit activation blocks in the
 *       matrix products, Q16 activations, Q16 softmax weights (an attention
 *       weight below 2^-16 of the largest counts as zero) and GELU from a
 *       table; the measured gap to a float64 forward pass on the same
 *       weights, and to whisper.cpp's transcripts, is in
 *       bench/results/2026-10-12-whisper-base/REPORT.md. Values saturate at
 *       +-32768 (counted in n_sat). Only whisper-base has been run; the
 *       larger sizes use the same code but are unmeasured (large-v3's 128
 *       mel bins are accepted, its float16-trained weights are not checked).
 *       Scalar C is about as fast as a single core allows: see the report.
 * Freestanding: no libc, no malloc, no floating point; 64-bit divisions only
 * through zt_udiv64. */
#ifndef ZT_WHISPER_H
#define ZT_WHISPER_H

#include <stdint.h>
#include <stdbool.h>
#include "zt_nn.h"

#define ZT_WHISPER_MAX_LAYERS 64u

typedef struct {
    uint32_t sample_rate, n_fft, hop, n_mels, n_bins; /* n_bins = n_fft / 2 + 1 */
    uint32_t n_frames, n_samples;                     /* 2 * n_audio_ctx, n_frames * hop */
    uint32_t n_audio_ctx, n_audio_head, n_audio_layer, n_audio_ff;
    uint32_t n_text_ctx, n_text_head, n_text_layer, n_text_ff;
    uint32_t n_state, n_vocab, eps_f32;
    int32_t eot, sot, translate, transcribe, sot_lm, sot_prev, no_speech, no_timestamps;
    int32_t timestamp_begin, lang_first;
    uint32_t n_lang;
} zt_whisper_cfg_t;

typedef struct {
    zt_mat_t wq, wk, wv, wo;
    const zt_fx *bq, *bv, *bo; /* K has no bias */
} zt_whisper_att_t;

typedef struct {
    zt_nn_norm_t ln_att, ln_cross, ln_mlp; /* ln_cross: decoder only */
    zt_whisper_att_t att, cross;           /* cross: decoder only */
    zt_mat_t w1, w2;
    const zt_fx *b1, *b2;
} zt_whisper_layer_t;

typedef struct {
    zt_whisper_cfg_t cfg;
    const uint32_t *mel_fil;         /* [n_mels][n_bins], value = f / 2^fil_frac */
    const uint16_t *fil_lo, *fil_hi; /* each band's nonzero bins [lo, hi) */
    uint32_t fil_frac;
    zt_mat_t conv1, conv2;
    const zt_fx *conv1_b, *conv2_b, *enc_pos, *dec_pos;
    zt_whisper_layer_t *enc, *dec;
    zt_nn_norm_t enc_ln, dec_ln;
    zt_mat_t tok;             /* [n_vocab][n_state]: the logits matrix */
    zt_gguf_tensor_t tok_t;   /* the same, for dequantising one row */
    const uint8_t *suppress;  /* bitmap: excluded at every step */
    const uint8_t *suppress0; /* bitmap: excluded at the first step as well */
    /* null: the portable C reference; a hosted caller may set zt_simd.h's
     * kernels (and threads) after loading. The bits do not change. */
    const struct zt_kern *kern;
} zt_whisper_t;

int32_t zt_whisper_config(const zt_gguf_t *g, zt_whisper_cfg_t *c, zt_model_err_t *err);
/* Arena bytes zt_whisper_load needs, or 0 when the file cannot be loaded. */
uint64_t zt_whisper_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the model (Q8_0 weights are used in place). */
int32_t zt_whisper_load(zt_whisper_t *w, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                        zt_model_err_t *err);

/* Everything one utterance needs: mel, encoder output, cross K/V, KV cache,
 * activations. */
typedef struct {
    zt_nn_t nn;
    zt_fx *mel;     /* [n_frames][n_mels], Q16 (frames are rows) */
    zt_fx *enc;     /* [n_audio_ctx][n_state] */
    zt_fx *ck, *cv; /* [n_text_layer][n_audio_ctx][n_state] cross K, V */
    zt_fx *sk, *sv; /* [n_text_layer][n_text_ctx][n_state] self K, V */
    zt_fx *x, *xn, *q, *k, *v, *att, *h, *col, *logits;
    uint32_t n_past; /* decoder tokens in the KV cache */
    uint32_t n_sat;  /* values that hit the Q16 limit (mel, encode and decode) */
} zt_whisper_state_t;

uint64_t zt_whisper_state_bytes(const zt_whisper_t *w);
int32_t zt_whisper_state_init(zt_whisper_state_t *st, const zt_whisper_t *w, void *mem,
                              uint64_t bytes);

/* Log-mel spectrogram of n 16 kHz samples (padded or cut to n_samples) into
 * st->mel. */
int32_t zt_whisper_mel(const zt_whisper_t *w, zt_whisper_state_t *st, const int16_t *pcm,
                       uint64_t n);
/* st->mel -> st->enc, then every decoder layer's cross K and V. Resets the
 * decoder (n_past = 0). */
int32_t zt_whisper_encode(const zt_whisper_t *w, zt_whisper_state_t *st);
/* Feed n tokens after the n_past already in the cache. logits: n_vocab
 * values for the last token, or n * n_vocab (every token) with all; may be
 * null. */
int32_t zt_whisper_decode(const zt_whisper_t *w, zt_whisper_state_t *st, const int32_t *ids,
                          uint32_t n, zt_fx *logits, bool all);
/* The language index (0 .. n_lang - 1; token lang_first + index) whose token
 * has the largest logit after <|startoftranscript|>. Resets the decoder. */
int32_t zt_whisper_detect_lang(const zt_whisper_t *w, zt_whisper_state_t *st, uint32_t *lang);

/* Called with each step's logits before suppression (step 0: after the
 * prompt) and the token chosen; return false to stop. */
typedef bool (*zt_whisper_step_fn)(void *ctx, uint32_t step, const zt_fx *logits, int32_t token);
/* Greedy decoding after the prompt <|startoftranscript|><|lang|><|transcribe|>
 * <|notimestamps|> (task: translate instead when translate is set). Writes
 * up to cap text tokens (without <|endoftext|>) to out. */
int32_t zt_whisper_greedy(const zt_whisper_t *w, zt_whisper_state_t *st, uint32_t lang,
                          bool translate, uint32_t max_tokens, int32_t *out, uint32_t cap,
                          uint32_t *n_out, zt_whisper_step_fn fn, void *ctx);
/* mel + encode + (language detection when lang < 0) + greedy transcription. */
int32_t zt_whisper_transcribe(const zt_whisper_t *w, zt_whisper_state_t *st, const int16_t *pcm,
                              uint64_t n, int32_t lang, int32_t *out, uint32_t cap,
                              uint32_t *n_out);

/* The language code ("en") of index lang, as a GGUF string; len 0 if none. */
zt_gguf_str_t zt_whisper_lang_code(const zt_gguf_t *g, uint32_t lang);
/* The index of a language code, or -1. */
int32_t zt_whisper_lang_find(const zt_gguf_t *g, const char *code);

#endif /* ZT_WHISPER_H */
