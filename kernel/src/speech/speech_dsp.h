/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_dsp.h — the integer audio front end for speech recognition.
 *
 * Everything a speech model needs before it sees audio, done in integers:
 *
 *   resample     any rate from 8 kHz to 96 kHz down (or up) to 16 kHz, with a
 *                Kaiser-windowed sinc (bandlimited interpolation, exact
 *                rational time base, streaming, chunk-size independent)
 *   VAD          energy + zero-crossing voice activity detection with an
 *                adaptive noise floor, onset debounce and hangover
 *   pre-emphasis y[n] = x[n] - a x[n-1] (optional; Whisper does NOT use it)
 *   framing      fixed frame/hop windows over a sample stream
 *   FFT          400-point mixed-radix DFT (16 x 25 Cooley-Tukey), Q30
 *                twiddles, 64-bit accumulators
 *   log-mel      the Whisper front end: periodic Hann, n_fft 400, hop 160,
 *                power spectrum, 80-band Slaney mel filterbank, log10 with a
 *                1e-10 floor, clamp to (max - 8), then (x + 4) / 4. The
 *                result matches a float64 numpy reference within the
 *                tolerance test_speech.c measures and asserts.
 *
 * Output units. Log-mel values are Q16 fixed point: 65536 == 1.0 in Whisper's
 * normalised units, so a Whisper encoder port reads them as value / 65536.
 *
 * Freestanding: no libc, no allocation, no floating point, no 64-bit
 * division. The only divisions are 32-bit ones by constants or at init.
 */
#ifndef ZXV_SPEECH_DSP_H
#define ZXV_SPEECH_DSP_H

#include <stdint.h>
#include <stdbool.h>

#define SPEECH_SAMPLE_RATE 16000u
#define SPEECH_N_FFT       400u
#define SPEECH_HOP         160u
#define SPEECH_N_MELS      80u
#define SPEECH_N_BINS      201u /* n_fft / 2 + 1 */
#define SPEECH_Q           16   /* log-mel outputs are Q16 */

/* ===== Resampler: any rate in [8000, 96000] -> 16000 ===== */
#define SPEECH_RS_MIN_RATE 8000u
#define SPEECH_RS_MAX_RATE 96000u
#define SPEECH_RS_BUF      1024u

typedef struct {
    uint32_t in_rate;
    uint32_t step_int;  /* in_rate / 16000                         */
    uint32_t step_rem;  /* in_rate % 16000                         */
    uint32_t scale_q16; /* min(1, 16000 / in_rate) in Q16          */
    uint32_t span;      /* taps on each side, in input samples     */
    uint32_t pos;       /* integer part of the next output time    */
    uint32_t frac;      /* fractional part, numerator over 16000   */
    uint32_t fill;      /* samples held in buf                     */
    bool passthrough;   /* in_rate == 16000: exact copy            */
    int16_t buf[SPEECH_RS_BUF];
} speech_resampler_t;

/* 0 on success, -1 for a rate outside [8000, 96000] or NULL. */
int speech_resampler_init(speech_resampler_t *r, uint32_t in_rate);

/* Push up to n input samples; writes at most cap output samples. *consumed
 * (optional) gets the number of input samples taken; call again with the rest
 * when it is less than n. Returns the number of output samples written. The
 * output is independent of how the input is chunked. */
uint32_t speech_resample(speech_resampler_t *r, const int16_t *in, uint32_t n, uint32_t *consumed,
                         int16_t *out, uint32_t cap);

/* Input samples of delay: push this many zeros at end of stream to flush. */
uint32_t speech_resampler_latency(const speech_resampler_t *r);

/* ===== Voice activity detection ===== */
typedef enum {
    SPEECH_VAD_SILENCE = 0, /* no speech                                */
    SPEECH_VAD_START = 1,   /* speech began (onset_frames ago)          */
    SPEECH_VAD_SPEECH = 2,  /* speech continues                         */
    SPEECH_VAD_END = 3      /* speech ended (after the hangover)        */
} speech_vad_event_t;

typedef struct {
    uint32_t frame_len;    /* samples per decision frame (160 = 10 ms) */
    uint32_t onset_frames; /* active frames needed to declare START    */
    uint32_t hangover;     /* inactive frames before END               */
    uint32_t ratio_q4;     /* energy must exceed noise * ratio / 16    */
    uint32_t min_rms;      /* absolute floor on frame RMS (samples)    */
    uint32_t zcr_noise;    /* zero crossings per frame above which a   */
                           /* frame needs twice the energy ratio       */
    uint64_t noise;        /* adaptive noise floor (sum of squares)    */
    uint32_t run;          /* consecutive active frames                */
    uint32_t quiet;        /* consecutive inactive frames in speech    */
    uint32_t frames;       /* frames processed                         */
    uint32_t start_frame;  /* frame index where the last START began   */
    bool in_speech;
    bool primed;          /* noise floor seeded                       */
    uint64_t last_energy; /* diagnostics: last frame sum of squares   */
    uint32_t last_zcr;
} speech_vad_t;

/* Defaults: 160-sample frames, onset 3, hangover 30 (300 ms), ratio 4x,
 * min RMS 60, zcr_noise 64. */
void speech_vad_init(speech_vad_t *v);
/* Classify one frame of v->frame_len samples (n must equal frame_len). */
speech_vad_event_t speech_vad_frame(speech_vad_t *v, const int16_t *frame, uint32_t n);

/* ===== Pre-emphasis ===== */
typedef struct {
    int32_t coef_q15; /* 0.97 -> 31785 */
    int16_t prev;
} speech_preemph_t;

void speech_preemph_init(speech_preemph_t *p, int32_t coef_q15);
/* y[i] = x[i] - coef * x[i-1], rounded and saturated. in == out is allowed. */
void speech_preemph(speech_preemph_t *p, const int16_t *in, int16_t *out, uint32_t n);

/* ===== Framing ===== */
typedef struct {
    uint32_t frame_len; /* <= SPEECH_N_FFT */
    uint32_t hop;       /* 1 .. frame_len  */
    uint32_t fill;
    int16_t buf[SPEECH_N_FFT];
} speech_framer_t;

/* 0 on success, -1 on bad sizes. */
int speech_framer_init(speech_framer_t *f, uint32_t frame_len, uint32_t hop);
/* Consume input until a full frame is buffered. Returns true when a frame is
 * ready (read it from f->buf, then call speech_framer_advance); *consumed gets
 * the samples taken. */
bool speech_framer_push(speech_framer_t *f, const int16_t *in, uint32_t n, uint32_t *consumed);
void speech_framer_advance(speech_framer_t *f);

/* ===== FFT and log-mel ===== */
typedef struct {
    int32_t x[SPEECH_N_FFT];  /* windowed frame, Q8 of int16 units */
    int32_t yr[SPEECH_N_FFT]; /* 25-point stage, row-major [n1][k2] */
    int32_t yi[SPEECH_N_FFT];
    uint64_t power[SPEECH_N_BINS];
} speech_fft_work_t;

/* 400-point DFT of a real input (sum of |x| < 2^31), bins 0..200. Outputs are in
 * the same units as the input (no 1/N scaling). */
void speech_dft400(speech_fft_work_t *w, const int32_t *x, int32_t *re, int32_t *im);

/* Windowed power spectrum of one 400-sample frame into w->power. */
void speech_power_spectrum(speech_fft_work_t *w, const int16_t *frame);

/* log10(mel energy) of one frame, Q16, floored at -10 (1e-10). This is the
 * value BEFORE Whisper's (max - 8) clamp and (x + 4) / 4 scaling. */
void speech_mel_frame(speech_fft_work_t *w, const int16_t *frame, int32_t *log10_q16);

/* Whisper's normalisation over a whole spectrogram (count values, in place):
 * v = max(v, max_all - 8); v = (v + 4) / 4. */
void speech_logmel_finalize(int32_t *v, uint32_t count);

/* The whole Whisper front end over n samples of 16 kHz audio: reflect-pad 200
 * samples each side, n / 160 frames (Whisper drops the last STFT frame).
 * out is frame-major: out[f * 80 + m]. Needs n > 200 and
 * max_frames >= n / 160. Returns the frame count, or -1. */
int32_t speech_logmel(speech_fft_work_t *w, const int16_t *pcm, uint32_t n, int32_t *out,
                      uint32_t max_frames);

/* log2 of a non-zero 64-bit value, Q16. Returns INT32_MIN for 0. */
int32_t speech_log2_q16(uint64_t v);

#endif /* ZXV_SPEECH_DSP_H */
