/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_tts.h — speech out, in integers: text to OuteTTS audio-code tokens (the
 * language model, zt_model.h) and the WavTokenizer decoder from codes to
 * 24 kHz 16-bit PCM.
 *
 *   T26 SPEECH OUT.  Three parts, each following llama.cpp's OuteTTS 0.3
 *       pipeline (tools/tts/tts.cpp before it became model-agnostic, and
 *       src/models/wavtokenizer-dec.cpp):
 *       TEXT.  zt_tts_normalize: numbers to English words (integer part up
 *       to 2147483647 as "one hundred twenty-three", then " point" and one
 *       word per decimal digit; a larger number becomes a space, as
 *       llama.cpp's std::stoi failure does), ASCII lower case, the
 *       characters - _ / , . \ to spaces, every other byte that is not a-z
 *       or ASCII white space dropped, runs of white space to one, trimmed,
 *       and each space written as <|space|>. zt_tts_prompt wraps it in
 *       "<|im_start|>\n", the default speaker's words (en_male_1, as
 *       llama.cpp ships it), "<|text_end|>\n" and the speaker's audio codes;
 *       zt_tok_encode with parse_special turns it into llama.cpp's tokens.
 *       CODES.  The model writes "\n", a word, <|t_d.dd|>, that word's audio
 *       codes <|0|>..<|4095|>, <|space|>, and so on, up to <|audio_end|> or
 *       <|im_end|>. zt_tts_codes keeps the code tokens (code = id - id of
 *       <|0|>) and drops everything else.
 *       DECODER.  zt_tts_load reads a llama.cpp "wavtokenizer-dec" GGUF
 *       (WavTokenizer-Large-75, F16) and zt_tts_spec runs it with zt_nn.h's
 *       blocks: the codebook row of each code (512 wide); conv1d k7 to 768;
 *       PosNet: two ResNet blocks (GroupNorm 32 groups, x sigmoid(x), conv
 *       k3, GroupNorm, x sigmoid(x), conv k3, residual), one single-head
 *       attention block over all frames (GroupNorm, k1 convs for Q, K, V,
 *       softmax(q.k / sqrt(768)) v, k1 output conv, residual), two more
 *       ResNet blocks, a final GroupNorm; LayerNorm; twelve ConvNeXt blocks
 *       (depthwise conv k7, LayerNorm, 768 -> 2304 linear, GELU in the tanh
 *       form llama.cpp uses, 2304 -> 768 linear, layer scale gamma,
 *       residual); LayerNorm; the output linear to 1282 values per frame:
 *       641 log-magnitudes and 641 phases.
 *       iSTFT.  zt_tts_istft, all integer: magnitude e^min(x, ln 100) in Q16
 *       (x <= 0: zt_exp; x > 0: 2^k e^(x - k ln 2) with ln 2 to 32 bits);
 *       cos and sin of the phase in Q30 by an exact angle reduction (the Q16
 *       angle times round(2^48 / 2 pi) is the angle in turns to 64 bits,
 *       wrapping), a 1024-step quarter-wave table, a 2^20-step fine table
 *       and one first-order step (error a few Q30 units, about 1e-9);
 *       re = mag cos, im = mag sin rounded to Q16; the inverse DFT of size
 *       1280 exactly as llama.cpp's irfft does it (every output sample
 *       y[k] = sum over the 641 bins of Re(X_m e^(2 pi i k m / 1280)), divided
 *       by 641, which weights the DC and Nyquist bins half as much as the
 *       textbook irfft does), as a direct sum of Q16 x Q30 products in exact
 *       64-bit integers (|sum| <= 641 * 100 * 2^46 < 2^62), using the
 *       symmetry of y[k] and y[1280 - k]; y rounded to Q24; times the
 *       periodic Hann window (Q30, from the same cos table), overlap-add at
 *       hop 320 in 64 bits, the 480 samples of padding cut from each end,
 *       and division by the overlap-added Hann^2 envelope; finally
 *       round(32767 x), clipped to int16 (llama.cpp truncates toward zero
 *       instead: at most one step apart). n codes give 320 n samples.
 *       zt_tts_decode zeroes the first `skip` samples (llama.cpp: 6000,
 *       0.25 s) and counts what clipped.
 *       Every step is integer and deterministic: the codes and the PCM are
 *       the same bits on every machine and kernel set (bench/tensor/zt_tts
 *       checks c, SIMD, threads and aarch64 under qemu).
 *       HONEST LIMITS.  Measured on one checkpoint pair, OuteTTS-0.3-500M
 *       Q8_0 and WavTokenizer-Large-75 F16, on five short English sentences
 *       (bench/results/2026-10-12-outetts/REPORT.md): accuracy against a
 *       float64 numpy decoder fed the same codes, and intelligibility by a
 *       whisper.cpp round trip next to llama.cpp's own output. The F16
 *       decoder weights are re-quantised to 8 bits (zt_nn), the largest
 *       error source. English only: no romanisation of other scripts (as
 *       llama.cpp), no speaker files (only the built-in en_male_1), no
 *       guide tokens. Number words follow llama.cpp, not English grammar
 *       ("1,000" is "one zero zero zero": the comma splits it first).
 *       Sampling uses zt_sample's integer draw: the same settings as
 *       llama.cpp (top-k 4, temperature 1) but different random numbers, so
 *       the same seed gives a different, equally likely, utterance. The
 *       inverse DFT is a direct O(n^2) sum (about 0.8 M multiply-adds per
 *       frame, 62 M per second of audio), not an FFT. The PosNet attention
 *       runs over every frame at once, so the work grows with the square of
 *       the utterance length. The decoder keeps a few 64 KB of work per frame
 *       (zt_tts_work_bytes). Values saturate at +-32768 in Q16 (counted in
 *       n_sat); none were seen on the test sentences.
 * Freestanding: no libc, no malloc, no floating point; 64-bit divisions only
 * through zt_udiv64. */
#ifndef ZT_TTS_H
#define ZT_TTS_H

#include <stdint.h>
#include <stdbool.h>
#include "zt_nn.h"
#include "zt_tok.h"

#define ZT_TTS_RATE  24000u /* samples per second */
#define ZT_TTS_HOP   320u   /* samples per code (frame) */
#define ZT_TTS_NFFT  1280u
#define ZT_TTS_NBIN  641u  /* NFFT / 2 + 1 */
#define ZT_TTS_SKIP  6000u /* llama.cpp zeroes the first 0.25 s */
#define ZT_TTS_CODES 4096u

/* ---- text ---- */

/* The normalised text ("hello<|space|>world"). Writes at most cap bytes and
 * returns the full length (call with cap 0 to size the buffer). */
uint64_t zt_tts_normalize(const uint8_t *text, uint64_t len, uint8_t *out, uint64_t cap);
/* The whole OuteTTS 0.3 prompt for text, with the default speaker. Same
 * convention as zt_tts_normalize. Tokenize it with parse_special. */
uint64_t zt_tts_prompt(const uint8_t *text, uint64_t len, uint8_t *out, uint64_t cap);

/* ---- codes ---- */

/* Token ids of the vocabulary's <|0|> (the first code; <|1|>..<|4095|> must
 * follow it), <|audio_end|>, <|im_end|> and <|endoftext|> (-1 when absent).
 * Returns ZT_NN_ECONFIG when the codes are missing or not consecutive. */
typedef struct {
    int32_t code0, audio_end, im_end, eot;
} zt_tts_vocab_t;
int32_t zt_tts_vocab(const zt_tok_t *t, zt_tts_vocab_t *v);
/* Keep the audio-code tokens of a generated sequence as code ids
 * 0..ZT_TTS_CODES-1 (in place is fine). Returns how many. */
uint32_t zt_tts_codes(const zt_tts_vocab_t *v, const int32_t *tokens, uint32_t n, int32_t *codes);

/* ---- decoder ---- */

typedef struct {
    uint32_t n_vocab, n_feat, n_embd, n_ff, n_out, n_posnet, n_convnext, n_groups;
    uint32_t eps_ln_f32, eps_gn_f32;
} zt_tts_cfg_t;

typedef struct {
    zt_nn_norm_t n1, n2;
    zt_mat_t c1, c2; /* conv k3: 3 * n_embd columns */
    const zt_fx *b1, *b2;
} zt_tts_res_t;

typedef struct {
    zt_gain_t dw; /* depthwise k7: channel c, tap j at dw.g[c * 7 + j] */
    const zt_fx *dw_b;
    zt_nn_norm_t norm;
    zt_mat_t pw1, pw2;
    const zt_fx *b1, *b2;
    zt_gain_t gamma;
} zt_tts_cnx_t;

typedef struct {
    zt_tts_cfg_t cfg;
    zt_gguf_tensor_t embd; /* the codebook, read a row at a time */
    zt_mat_t conv_in;      /* conv k7: 7 * n_feat columns */
    const zt_fx *conv_in_b;
    zt_tts_res_t res[4]; /* PosNet blocks 0, 1, 3, 4 */
    zt_nn_norm_t att_norm;
    zt_mat_t wq, wk, wv, wo;
    const zt_fx *bq, *bk, *bv, *bo;
    zt_nn_norm_t pos_norm; /* PosNet block 5 */
    zt_nn_norm_t tok_norm;
    zt_tts_cnx_t *cnx; /* n_convnext, in the arena */
    zt_nn_norm_t out_norm;
    zt_mat_t out;
    const zt_fx *out_b;
    const struct zt_kern *kern; /* null: C reference; a hosted caller may set one */
} zt_tts_dec_t;

int32_t zt_tts_config(const zt_gguf_t *g, zt_tts_cfg_t *c, zt_model_err_t *err);
uint64_t zt_tts_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the decoder (the codebook is read from it). */
int32_t zt_tts_load(zt_tts_dec_t *d, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                    zt_model_err_t *err);
/* The same with ZT_TTS_LOAD_* flags. ZT_TTS_LOAD_LEAN keeps one 8-bit block
 * per 32 weights (zt_nn_arena_t.float_residual off): half the arena and the
 * matrix work, at a measured cost in accuracy (REPORT.md). The default keeps
 * the residual block. */
#define ZT_TTS_LOAD_LEAN 1u
uint64_t zt_tts_arena_bytes_ex(const zt_gguf_t *g, uint32_t flags);
int32_t zt_tts_load_ex(zt_tts_dec_t *d, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                       uint32_t flags, zt_model_err_t *err);

typedef struct {
    uint32_t n_sat;     /* decoder values that hit the Q16 limit */
    uint32_t n_clip;    /* PCM samples clipped to the int16 range */
    uint32_t n_samples; /* 320 per code */
} zt_tts_stats_t;

uint64_t zt_tts_work_bytes(const zt_tts_dec_t *d, uint32_t max_codes);
/* The spectrogram: n rows of n_out Q16 values (log-magnitudes, then phases).
 * spec may be null when only zt_tts_decode's pcm is wanted. */
int32_t zt_tts_spec(const zt_tts_dec_t *d, const int32_t *codes, uint32_t n, zt_fx *spec,
                    void *work, uint64_t work_bytes, zt_tts_stats_t *st);
/* Codes to PCM: 320 n samples, the first `skip` set to zero. */
int32_t zt_tts_decode(const zt_tts_dec_t *d, const int32_t *codes, uint32_t n, int16_t *pcm,
                      uint32_t skip, void *work, uint64_t work_bytes, zt_tts_stats_t *st);

/* ---- iSTFT (exposed for tests) ---- */

/* e^x in Q16 for x <= 10 (larger x gives e^10). */
zt_fx zt_tts_exp(zt_fx x);
/* cos and sin of a Q16 angle (radians), Q30. */
void zt_tts_cossin(zt_fx angle, int32_t *c, int32_t *s);
uint64_t zt_tts_istft_bytes(uint32_t n);
/* spec: n frames of 1282 Q16 values at stride `stride`; pcm: 320 n samples.
 * kern (may be null) splits the frames across threads. Returns the clip
 * count. */
uint32_t zt_tts_istft(const zt_fx *spec, uint32_t stride, uint32_t n, int16_t *pcm, void *work,
                      uint64_t work_bytes, const struct zt_kern *kern);

#endif /* ZT_TTS_H */
