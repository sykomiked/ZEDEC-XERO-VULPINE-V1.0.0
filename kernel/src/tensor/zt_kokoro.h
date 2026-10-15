/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_kokoro.h — Kokoro-82M speech out, in integers: phonemes and a voice in,
 * 24 kHz 16-bit PCM out.
 *
 *   T27 KOKORO.  Kokoro-82M (hexgrad, Apache-2.0) is StyleTTS2 with an
 *       ISTFTNet vocoder. It is not autoregressive: one pass per sentence,
 *       which is why it is clear on long sentences where OuteTTS slurs, and
 *       far faster. zt_kokoro_synth follows hexgrad/kokoro's
 *       forward_with_tokens with zt_nn.h's blocks (Q16 activations, matrix
 *       products on zt_kern.h's kernels):
 *       PL-BERT (ALBERT: one shared layer run 12 times, 12 heads, GELU tanh,
 *       LayerNorm eps 1e-12) and a linear to 512; the duration encoder
 *       (three bidirectional LSTMs, each followed by an AdaLayerNorm from
 *       the voice's prosody style, the style appended to every step); a
 *       BiLSTM and a linear to 50 duration logits, sigmoid, summed, divided
 *       by the speed and rounded (at least one frame a phoneme); the frames
 *       repeat each phoneme's features that many times; a shared BiLSTM and
 *       two AdaIN residual stacks for pitch (F0) and energy (N) at twice the
 *       frame rate; the text encoder (embedding, three k5 convolutions with
 *       LayerNorm and leaky ReLU 0.2, a BiLSTM), repeated the same way;
 *       the decoder (an AdaIN residual block to 1024, three more with the
 *       text, F0 and N appended, one that doubles the rate to 512 wide);
 *       the generator: a harmonic source (below), two transposed
 *       convolutions (x10, x6, each split into its stride phases by the
 *       converter), each followed by three AdaIN residual blocks with the
 *       Snake activation x + sin^2(a x) / a, a k7 convolution to 22
 *       channels: e^x for 11 magnitudes, sin x for 11 phases, and an
 *       inverse STFT (n_fft 20, hop 5, periodic Hann) to 24 kHz.
 *       HARMONIC SOURCE.  F0 (Hz, Q16) is held for 300 samples a step; nine
 *       harmonics h F0 / 24000 become phase steps in turns x 2^32. The
 *       phase is summed in wrapping 64-bit integers, so it is exact for any
 *       length, where the float source loses precision after seconds; it
 *       is interpolated between steps as torch.nn.functional.interpolate
 *       does (linear, align_corners false) and its sine taken from
 *       zt_tts_cossin_turns. Voiced steps (F0 > 10 Hz) get 0.1 sin plus
 *       noise of 0.003, unvoiced ones noise of 0.1 / 3. The source's random
 *       numbers (initial phases, noise) are fixed: the initial phases are
 *       zero and the noise is a sum of 12 uniforms from xorshift64*, seeded
 *       the same every time, so the same phonemes and voice give the same
 *       PCM on every machine (tools/kokoro/kokoro_ref.py draws the same
 *       numbers). The source's STFT (reflect padding, magnitude by integer
 *       square root, phase by a 30-step CORDIC) feeds two noise
 *       convolutions.
 *       VOICES.  A voice pack is 510 style vectors of 256 (one per phoneme
 *       count); the first 128 steer the decoder, the last 128 the
 *       prosody. zt_kokoro_voice reads row n - 1 of voice.<name>.
 *       PHONEMES.  zt_kokoro_ids maps each code point of a phoneme string
 *       through kokoro.vocab (misaki's symbols) and adds the boundary
 *       token 0 at both ends; code points it does not know are skipped,
 *       as Kokoro does.
 *       The GGUF is written by tools/kokoro/kokoro2gguf.py from the
 *       released kokoro-v1_0.pth (weight norm folded in; F32). Weights are
 *       not in git.
 *       HONEST LIMITS.  Instance and layer norms use integer statistics;
 *       the weights are held as two 8-bit blocks (zt_nn's float residual);
 *       results are checked against the float64 numpy reference
 *       (tools/kokoro/kokoro_ref.py) and by a speech-to-text round trip,
 *       not against torch. Memory grows with the sentence: about 400 KB per
 *       frame (40 frames a second) of work; speak a sentence at a time.
 * Freestanding: no libc, no malloc, no floating point. */
#ifndef ZT_KOKORO_H
#define ZT_KOKORO_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"
#include "zt_gguf.h"
#include "zt_model.h"
#include "zt_nn.h"

#define ZT_KOKORO_STYLE       256u /* a voice's style vector                  */
#define ZT_KOKORO_MAX_TOK     512u /* PL-BERT's positions, boundaries included */
#define ZT_KOKORO_RATE        24000u
#define ZT_KOKORO_HOP_SAMPLES 600u /* PCM samples per frame */

typedef struct {
    uint32_t n_token, style, hidden, max_dur;
} zt_kokoro_cfg_t;

typedef struct {
    zt_mat_t w;
    const zt_fx *b; /* or null */
} zt_kk_lin_t;

typedef struct {
    zt_kk_lin_t wi; /* input matrix, with b_ih + b_hh */
    zt_mat_t wh;
} zt_kk_dir_t;

typedef struct {
    zt_kk_dir_t fw, bw;
    uint32_t in, hid;
} zt_kk_lstm_t;

/* AdainResBlk1d */
typedef struct {
    zt_kk_lin_t n1, n2, c1, c2;
    zt_mat_t sc; /* conv1x1, when cin != cout */
    bool has_sc, up;
    zt_gain_t pool_w; /* depthwise transposed conv k3 s2, [C][3] */
    const zt_fx *pool_b;
    uint32_t cin, cout;
} zt_kk_blk_t;

/* AdaINResBlock1 with Snake */
typedef struct {
    zt_kk_lin_t a1[3], a2[3], c1[3], c2[3];
    zt_gain_t al1[3], al2[3];
    const zt_fx *inv1[3], *inv2[3]; /* 1 / alpha, Q16 */
    uint32_t C, K;
} zt_kk_res_t;

typedef struct zt_kokoro {
    zt_kokoro_cfg_t cfg;
    const zt_gguf_t *g;
    /* PL-BERT */
    const zt_fx *word, *pos, *type;
    zt_nn_norm_t emb_norm, attn_norm, ffn_norm;
    zt_kk_lin_t map, q, k, v, o, up, down, bert_enc;
    /* predictor */
    zt_kk_lstm_t dur_lstm[3], lstm, shared;
    zt_kk_lin_t dur_norm[3], dur_proj, f0_proj, n_proj;
    zt_kk_blk_t f0[3], n[3];
    /* text encoder */
    const zt_fx *text_embd;
    zt_kk_lin_t cnn[3];
    zt_nn_norm_t cnn_norm[3];
    zt_kk_lstm_t text_lstm;
    /* decoder */
    zt_gain_t f0c_w, nc_w; /* k3 stride 2, one channel */
    const zt_fx *f0c_b, *nc_b;
    zt_kk_lin_t asr_res;
    zt_kk_blk_t encode, decode[4];
    /* generator */
    zt_gain_t l_lin; /* nine harmonics to one source */
    const zt_fx *l_lin_b;
    zt_kk_lin_t noise_conv[2], ups_p0[10], ups_p1[6], post;
    const zt_fx *ups_b[2];
    zt_kk_res_t noise_res[2], res[6];
    /* phonemes */
    uint32_t vocab_cp[256]; /* code point of each id (0: none) */
    /* hosted kernels (null: the C reference); set after zt_kokoro_load */
    const struct zt_kern *kern;
} zt_kokoro_t;

typedef struct {
    uint32_t tokens, frames, samples, clipped, n_sat;
    /* valid until the work memory is reused: */
    const int32_t *dur; /* frames per token */
    const zt_fx *f0;    /* 2 frames per frame, Hz */
} zt_kokoro_stats_t;

int32_t zt_kokoro_config(const zt_gguf_t *g, zt_kokoro_cfg_t *c, zt_model_err_t *err);
uint64_t zt_kokoro_arena_bytes(const zt_gguf_t *g);
/* The gguf buffer must outlive the model (voices are read from it). */
int32_t zt_kokoro_load(zt_kokoro_t *k, const zt_gguf_t *g, void *arena, uint64_t arena_bytes,
                       zt_model_err_t *err);

/* Phoneme string (UTF-8) to ids with boundaries: returns the count (0 when
 * nothing is known, or it would not fit cap or ZT_KOKORO_MAX_TOK). */
uint32_t zt_kokoro_ids(const zt_kokoro_t *k, const char *ph, uint32_t len, int32_t *ids,
                       uint32_t cap);
/* The style of voice `name` for n_phonemes phonemes (ZT_KOKORO_STYLE Q16
 * values). ZT_NN_EMISSING when the file has no such voice. */
int32_t zt_kokoro_voice(const zt_kokoro_t *k, const char *name, uint32_t n_phonemes, zt_fx *style);

/* Work bytes for up to max_tok ids and max_frames frames (PCM: 600 a frame). */
uint64_t zt_kokoro_work_bytes(const zt_kokoro_t *k, uint32_t max_tok, uint32_t max_frames);
/* Speak: ids (with their boundaries) and a style to PCM. speed_pct 100 is
 * the voice's own pace. Returns ZT_GGUF_OK, ZT_NN_ESPACE when the frames
 * would pass max_frames or pcm_cap, or ZT_NN_EARG. */
int32_t zt_kokoro_synth(const zt_kokoro_t *k, const int32_t *ids, uint32_t n, const zt_fx *style,
                        uint32_t speed_pct, uint32_t max_frames, int16_t *pcm, uint32_t pcm_cap,
                        void *work, uint64_t work_bytes, zt_kokoro_stats_t *st);

#endif /* ZT_KOKORO_H */
