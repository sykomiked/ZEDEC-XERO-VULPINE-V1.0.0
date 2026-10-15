/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxva_analyse.h — recordings to ZXVA vector audio, in integers, in the
 * kernel.
 *
 * zxva.h renders a ZXVA file at any rate and depth; this turns PCM into one.
 * It is the integer, freestanding counterpart of tools/zxva/wav2zxva.py (the
 * float build tool) and follows the rules that tool ended up with, so the
 * two agree closely on the same input (ZXVA.md section 7 has the measured
 * difference). What "vector" buys: the analysed sound re-renders at any
 * sample rate, depth or length with smooth partial trajectories and no
 * resampling filter: it scales cleanly. It does NOT add detail the source
 * never had: a 44.1 kHz recording has nothing above 22 kHz at any output
 * rate, and a partial is only as accurate as the analysis that found it.
 *
 * Pipeline, per channel (wav2zxva.py's steps; same constants):
 *   1. sinusoids: STFT with a ~85 ms Blackman-Harris window, hop 1/8, 4x
 *      zero padding, zero-phase framing (the heavy stage: zxva_stft_math.h,
 *      run by a backend, below). Peaks: local maxima above max(-110 dBFS,
 *      top - 90 dB), parabolic interpolation on the log magnitude, kept when
 *      their neighbourhood looks like the window's main lobe (similarity
 *      >= 0.95) or they stand 30 dB over the local median. Amplitude and
 *      phase come from the peak bin, corrected by the window's analytic
 *      main lobe.
 *   2. tracking: a peak continues a track within max(15 Hz, 3 %) when its
 *      phase arrives within 1/8 turn of where the frequency integral says
 *      (greedy, closest frequency first); tracks are born and die with a
 *      zero-amplitude breakpoint one hop outside. Kept when >= 9 frames,
 *      phase-coherent across a whole window (RMS surprise over 8 hops
 *      < 0.16 turn) and salient (median 12 dB above the local median):
 *      the 12 dB-above-median and phase-coherence rules that stopped noise
 *      from turning into spurious partials. The loudest max_tracks are kept,
 *      then simplified (breakpoints dropped while the renderer's own
 *      interpolation stays within -60 dB of the loudest partial).
 *   3. transients: onsets from a short (1 ms) / long (10 ms) energy ratio
 *      over 10, each fitted with a tone grain (least squares over a grid of
 *      frequency, duration and half-life), kept when it removes > 1 dB.
 *   4. noise: what partials and grains leave, as power on 24 ERB-spaced
 *      bands every 10 ms (gated at -90 dBFS).
 *   5. residual (optional): source minus the kernel renderer's own output at
 *      the native rate and depth, coded losslessly, so the file reproduces
 *      the original exactly there (checked before the file is returned).
 *   6. META: the source's CID (CIDv1 raw sha2-256 of the original file's
 *      bytes, or of the PCM when the caller has no file), its rate and
 *      depth, and the measured fidelity: waveform SNR of the parametric
 *      render in 1/100 dB and the ERB band-envelope error.
 *
 * Rules:
 *   N1 Integer only, freestanding, no allocation: the caller gives one work
 *      buffer (zxva_analyse_work_size) and one output buffer
 *      (zxva_analyse_out_bound).
 *   N2 Deterministic: same PCM, same config -> same bytes on every platform,
 *      compiler and backend. A GPU backend computes the stage of
 *      zxva_stft_math.h bit for bit (its test compares every word).
 *   N3 The output passes zxva_check, renders, and (with the residual)
 *      reproduces the input sample for sample at its native rate and depth.
 *
 * WHAT IS AND IS NOT IMPLEMENTED (differences from wav2zxva.py)
 *   - Amplitude and phase per peak come from the FFT bin (window-corrected),
 *     not from wav2zxva.py's joint least-squares refit of all peaks; close
 *     partials (within the ~4-bin main lobe) bias each other more here.
 *   - Not ported: the track extension pass (regrowing track ends by refit),
 *     the chirp refit (vibrato within one window) and the onset sharpening
 *     (a shared gain curve at attacks). Each makes wav2zxva.py a little
 *     more accurate on vibrato and attacks; the measured gap is in ZXVA.md.
 *   - Grain spectra use a power-of-two FFT (wav2zxva.py: 4x the segment);
 *     ERB band edges are computed with integer log2/exp2 and can differ by
 *     1 Hz from the float ones.
 *   - Inputs: 8..32-bit integer PCM (float sources are converted to 32-bit
 *     integers first, zui_audio.c), 1..8 channels, 8 kHz..768 kHz. 8- and
 *     12/20-bit sources are carried at the next residual depth (16/24) by an
 *     exact left shift.
 *   - Long inputs: memory grows with length (peaks of every frame are kept
 *     for tracking); split very long recordings and analyse the parts.
 */
#ifndef ZXV_ZXVA_ANALYSE_H
#define ZXV_ZXVA_ANALYSE_H

#include <stdbool.h>
#include <stdint.h>

#include "zxva.h"

/* ---- the heavy stage's backend interface (GPU-first) -------------------
 * One call computes frames k0 .. k0 + nf - 1 of one channel. CPU reference:
 * zxva_backend_cpu(). GPU backends (zxva_cuda.c, zxva_vk.c, zxva_metal.m:
 * hosted code, never in a kernel image) fill the same struct. */
typedef struct {
    uint32_t n, N, L;        /* window length, FFT size 2^L >= n            */
    uint32_t num, den;       /* frame k is centred at c0 + k * num / den,   */
    int64_t c0;              /* rounded half to even (zxva_stft_math.h)     */
    const int32_t *win;      /* n Q30 window values                         */
    const int32_t *tw;       /* N/2 cos then N/2 sin, Q30                   */
    const int32_t *x;        /* the channel, Q23                            */
    uint64_t len;            /* its length in samples                       */
    uint32_t floor_mag, rel; /* peak mask threshold: max(floor, top / rel)  */
} zxva_stft_plan_t;

/* per frame f of the batch: re, im, mag hold bins 0..N/2 at f * (N/2 + 1);
 * mask holds bit b of bin b at f * zxva_mask_words(N); top[f] the frame's
 * largest magnitude */
typedef struct zxva_backend {
    const char *name;
    void *ctx;
    int (*stft)(struct zxva_backend *b, const zxva_stft_plan_t *p, uint64_t k0, uint32_t nf,
                int32_t *re, int32_t *im, uint32_t *mag, uint32_t *mask, uint32_t *top);
    int32_t *scratch; /* CPU reference: 2N words (zxva_analyse sets it) */
    uint32_t scratch_words;
    uint32_t batch; /* frames per call the backend wants (0: 8; at most
                       1024); the work size grows with it            */
} zxva_backend_t;

static inline uint32_t zxva_mask_words(uint32_t N)
{
    return (N / 2 + 1 + 31) / 32;
}

/* the C reference of the stage (zxva_stft_math.h, run sequentially) */
void zxva_backend_cpu(zxva_backend_t *b);
int zxva_stft_cpu(zxva_backend_t *b, const zxva_stft_plan_t *p, uint64_t k0, uint32_t nf,
                  int32_t *re, int32_t *im, uint32_t *mag, uint32_t *mask, uint32_t *top);
/* window and twiddle tables (integer cos from zxva_sin_q30) */
void zxva_tables_bh(int32_t *win, uint32_t n);   /* Blackman-Harris, periodic */
void zxva_tables_hann(int32_t *win, uint32_t n); /* Hann, periodic */
void zxva_tables_tw(int32_t *tw, uint32_t N);    /* N/2 cos, N/2 sin */

/* ---- the analyser ------------------------------------------------------ */
typedef struct {
    const int32_t *pcm; /* interleaved, right-justified at bits           */
    uint64_t frames;
    uint32_t nch, rate, bits;
} zxva_pcm_t;

typedef struct {
    uint32_t max_tracks; /* partial tracks over all channels (0 = 1000)    */
    uint32_t max_peaks;  /* peaks kept per frame (0 = 160)                 */
    bool no_noise, no_grains;
    bool residual;           /* carry the lossless residual                    */
    const uint8_t *cid;      /* 36-byte CIDv1 of the original file, or NULL:
                                the CID of the PCM bytes (int32 LE) is used     */
    uint64_t src_bytes;      /* the original file's size (0: the PCM's)        */
    uint32_t src_kind;       /* ZXVA_SRC_*                                     */
    uint32_t src_bits;       /* the original depth if not pcm.bits (0: same)   */
    zxva_backend_t *backend; /* NULL: the CPU reference                     */
} zxva_an_cfg_t;

typedef struct {
    uint32_t ntracks, nbp, ngrains, nnoise;
    int32_t snr_cdb;       /* parametric render vs source, all layers      */
    int32_t snr_tonal_cdb; /* partials and grains only                     */
    uint32_t band_err_cdb; /* ERB band-envelope RMS error                  */
    bool exact;            /* residual present and verified sample-exact   */
    uint32_t param_bytes;  /* parametric part (header .. layers)           */
    uint32_t total_bytes;
    uint32_t res_bits;    /* the residual's depth (16/24/32)              */
    uint64_t stft_frames; /* frames through the backend                   */
} zxva_an_report_t;

#define ZXVA_AN_EARG    (-10) /* bad PCM or config                              */
#define ZXVA_AN_EWORK   (-11) /* work buffer too small                          */
#define ZXVA_AN_EOUT    (-12) /* output buffer too small                        */
#define ZXVA_AN_ECAP    (-13) /* over a cap (peaks pool, grains, duration)      */
#define ZXVA_AN_EBACK   (-14) /* the backend failed                             */
#define ZXVA_AN_EVERIFY (-15) /* internal: the result failed its own check    */

uint64_t zxva_analyse_work_size(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg);
uint64_t zxva_analyse_out_bound(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg);

/* Analyse pcm into out. Returns the ZXVA size in bytes, or < 0. out doubles
 * as scratch for trial renders while the file is built (layers are written,
 * rendered and rewritten), so give it zxva_analyse_out_bound bytes: a buffer
 * of exactly the final size can fail with ZXVA_AN_EOUT. work_size counts 16
 * bytes for aligning the work pointer. */
int64_t zxva_analyse(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg, void *work,
                     uint64_t work_size, uint8_t *out, uint64_t out_cap, zxva_an_report_t *rep);

/* Measure a ZXVA file (complete, valid, same channel count) against the
 * source PCM: fills rep->snr_cdb, snr_tonal_cdb and band_err_cdb with the
 * same definitions zxva_analyse records in META. work as for zxva_analyse
 * with a default config. 0 or < 0. */
int zxva_measure(const zxva_pcm_t *pcm, const uint8_t *zxva, uint32_t n, void *work,
                 uint64_t work_size, zxva_an_report_t *rep);

/* the CIDv1 raw sha2-256 of bytes (01 55 12 20 || SHA-256) */
void zxva_cid(const uint8_t *d, uint64_t n, uint8_t cid[ZXVA_CID_LEN]);

#endif
