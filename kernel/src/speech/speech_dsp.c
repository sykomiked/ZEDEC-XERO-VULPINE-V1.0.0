/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_dsp.c — integer audio front end. See speech_dsp.h. */
#include "speech_dsp.h"
#include "speech_tables.h"

static int16_t sat16(int64_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t) v;
}

/* Round-to-nearest arithmetic shift right of a signed 64-bit value. */
static int64_t rshr(int64_t v, unsigned s)
{
    return (v + ((int64_t) 1 << (s - 1))) >> s;
}

/* ===== Resampler ===== */

int speech_resampler_init(speech_resampler_t *r, uint32_t in_rate)
{
    if (!r || in_rate < SPEECH_RS_MIN_RATE || in_rate > SPEECH_RS_MAX_RATE) return -1;
    r->in_rate = in_rate;
    r->passthrough = (in_rate == SPEECH_SAMPLE_RATE);
    r->step_int = in_rate / SPEECH_SAMPLE_RATE;
    r->step_rem = in_rate % SPEECH_SAMPLE_RATE;
    if (in_rate <= SPEECH_SAMPLE_RATE) {
        r->scale_q16 = 65536u;
        r->span = SPEECH_RS_NZ;
    } else {
        r->scale_q16 = (SPEECH_SAMPLE_RATE << 16) / in_rate;
        r->span = (SPEECH_RS_NZ * in_rate + SPEECH_SAMPLE_RATE - 1) / SPEECH_SAMPLE_RATE;
    }
    for (uint32_t i = 0; i < SPEECH_RS_BUF; i++) r->buf[i] = 0;
    /* Pre-fill span-1 zeros so output 0 sits exactly on input sample 0. */
    r->fill = r->span - 1;
    r->pos = r->span - 1;
    r->frac = 0;
    return 0;
}

uint32_t speech_resampler_latency(const speech_resampler_t *r)
{
    if (!r || r->passthrough) return 0;
    return r->span;
}

static int16_t rs_interp(const speech_resampler_t *r)
{
    /* Output time = pos + frac / 16000 input samples. frac < 16000 < 2^14,
     * so frac << 16 fits in 32 bits and the division is by a constant. */
    int32_t frac_q16 = (int32_t) ((r->frac << 16) / SPEECH_SAMPLE_RATE);
    int64_t acc = 0;
    const uint32_t taps = 2 * r->span;
    const uint32_t tmax = SPEECH_RS_L * SPEECH_RS_NZ;
    for (uint32_t t = 0; t < taps; t++) {
        uint32_t i = r->pos - r->span + 1 + t;
        int32_t d = ((int32_t) r->span - 1 - (int32_t) t) * 65536 + frac_q16;
        uint32_t ad = (uint32_t) (d < 0 ? -d : d);
        /* u = |d| * scale, in Q16 zero crossings; table step is 1/L. */
        uint64_t u = ((uint64_t) ad * r->scale_q16) >> 16;
        uint64_t tp = u * SPEECH_RS_L; /* Q16 table index */
        uint32_t idx = (uint32_t) (tp >> 16);
        if (idx >= tmax) continue;
        int64_t fr = (int64_t) (tp & 0xFFFFu);
        int64_t h0 = SPEECH_RS_PROTO_Q24[idx];
        int64_t h1 = SPEECH_RS_PROTO_Q24[idx + 1];
        int64_t h = h0 + (((h1 - h0) * fr) >> 16);
        int64_t wgt = (h * (int64_t) r->scale_q16) >> 16; /* Q24 */
        acc += (int64_t) r->buf[i] * wgt;
    }
    return sat16(rshr(acc, 24));
}

uint32_t speech_resample(speech_resampler_t *r, const int16_t *in, uint32_t n, uint32_t *consumed,
                         int16_t *out, uint32_t cap)
{
    uint32_t produced = 0, used = 0;
    if (consumed) *consumed = 0;
    if (!r || (!in && n) || (!out && cap)) return 0;
    if (r->passthrough) {
        uint32_t k = n < cap ? n : cap;
        for (uint32_t i = 0; i < k; i++) out[i] = in[i];
        if (consumed) *consumed = k;
        return k;
    }
    for (;;) {
        while (produced < cap && r->pos + r->span < r->fill) {
            out[produced++] = rs_interp(r);
            r->pos += r->step_int;
            r->frac += r->step_rem;
            if (r->frac >= SPEECH_SAMPLE_RATE) {
                r->frac -= SPEECH_SAMPLE_RATE;
                r->pos++;
            }
        }
        if (produced == cap || used == n) break;
        /* Drop history no future output can reach: keep pos - span + 1 on. */
        if (r->pos + 1 > r->span) {
            uint32_t drop = r->pos + 1 - r->span;
            if (drop > r->fill) drop = r->fill;
            for (uint32_t i = drop; i < r->fill; i++) r->buf[i - drop] = r->buf[i];
            r->fill -= drop;
            r->pos -= drop;
        }
        uint32_t room = SPEECH_RS_BUF - r->fill;
        uint32_t k = n - used < room ? n - used : room;
        if (k == 0) break;
        for (uint32_t i = 0; i < k; i++) r->buf[r->fill + i] = in[used + i];
        r->fill += k;
        used += k;
    }
    if (consumed) *consumed = used;
    return produced;
}

/* ===== VAD ===== */

void speech_vad_init(speech_vad_t *v)
{
    if (!v) return;
    v->frame_len = SPEECH_HOP;
    v->onset_frames = 3;
    v->hangover = 30;
    v->ratio_q4 = 64; /* 4.0x */
    v->min_rms = 60;
    v->zcr_noise = 64;
    v->noise = 0;
    v->run = 0;
    v->quiet = 0;
    v->frames = 0;
    v->start_frame = 0;
    v->in_speech = false;
    v->primed = false;
    v->last_energy = 0;
    v->last_zcr = 0;
}

speech_vad_event_t speech_vad_frame(speech_vad_t *v, const int16_t *frame, uint32_t n)
{
    if (!v || !frame || n == 0 || n != v->frame_len) return SPEECH_VAD_SILENCE;
    uint64_t e = 0;
    uint32_t zcr = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = frame[i];
        e += (uint64_t) ((int64_t) s * s);
        if (i > 0 && ((frame[i - 1] < 0) != (s < 0))) zcr++;
    }
    v->last_energy = e;
    v->last_zcr = zcr;
    uint32_t idx = v->frames++;
    if (!v->primed) {
        v->noise = e;
        v->primed = true;
    }
    /* Active: energy above noise * ratio (doubled for hiss-like frames with a
     * high zero-crossing count) and above the absolute floor. */
    uint64_t ratio = v->ratio_q4;
    if (zcr > v->zcr_noise) ratio *= 2;
    uint64_t floor_e = (uint64_t) v->min_rms * v->min_rms * n;
    bool active = (e * 16 > v->noise * ratio) && (e > floor_e);

    /* Noise floor: fast down, slow up when inactive, very slow when active so
     * a step in background level is eventually absorbed. */
    if (e < v->noise) {
        v->noise -= (v->noise - e) >> 2;
    } else if (!active) {
        v->noise += (e - v->noise) >> 5;
    } else {
        v->noise += (e - v->noise) >> 11;
    }

    if (!v->in_speech) {
        if (active) {
            v->run++;
            if (v->run >= v->onset_frames) {
                v->in_speech = true;
                v->quiet = 0;
                v->start_frame = idx + 1 - v->onset_frames;
                return SPEECH_VAD_START;
            }
        } else {
            v->run = 0;
        }
        return SPEECH_VAD_SILENCE;
    }
    if (active) {
        v->quiet = 0;
        return SPEECH_VAD_SPEECH;
    }
    v->quiet++;
    if (v->quiet >= v->hangover) {
        v->in_speech = false;
        v->run = 0;
        v->quiet = 0;
        return SPEECH_VAD_END;
    }
    return SPEECH_VAD_SPEECH;
}

/* ===== Pre-emphasis ===== */

void speech_preemph_init(speech_preemph_t *p, int32_t coef_q15)
{
    if (!p) return;
    if (coef_q15 < 0) coef_q15 = 0;
    if (coef_q15 > 32767) coef_q15 = 32767;
    p->coef_q15 = coef_q15;
    p->prev = 0;
}

void speech_preemph(speech_preemph_t *p, const int16_t *in, int16_t *out, uint32_t n)
{
    if (!p || !in || !out) return;
    for (uint32_t i = 0; i < n; i++) {
        int16_t x = in[i];
        int64_t y = (int64_t) x - rshr((int64_t) p->coef_q15 * p->prev, 15);
        p->prev = x;
        out[i] = sat16(y);
    }
}

/* ===== Framing ===== */

int speech_framer_init(speech_framer_t *f, uint32_t frame_len, uint32_t hop)
{
    if (!f || frame_len == 0 || frame_len > SPEECH_N_FFT || hop == 0 || hop > frame_len) return -1;
    f->frame_len = frame_len;
    f->hop = hop;
    f->fill = 0;
    return 0;
}

bool speech_framer_push(speech_framer_t *f, const int16_t *in, uint32_t n, uint32_t *consumed)
{
    uint32_t used = 0;
    if (consumed) *consumed = 0;
    if (!f || (!in && n)) return false;
    while (f->fill < f->frame_len && used < n) f->buf[f->fill++] = in[used++];
    if (consumed) *consumed = used;
    return f->fill == f->frame_len;
}

void speech_framer_advance(speech_framer_t *f)
{
    if (!f || f->fill < f->hop) return;
    for (uint32_t i = f->hop; i < f->fill; i++) f->buf[i - f->hop] = f->buf[i];
    f->fill -= f->hop;
}

/* ===== 400-point DFT: n = n1 + 16 n2, k = k2 + 25 k1 =====
 * X[k2 + 25 k1] = sum_n1 W16^(n1 k1) * W400^(n1 k2) * sum_n2 x[n1 + 16 n2] W25^(n2 k2)
 * W_N^j = exp(-2 pi i j / N); W25 = W400^16, W16 = W400^25. */

#define N1 16u
#define N2 25u

void speech_dft400(speech_fft_work_t *w, const int32_t *x, int32_t *re, int32_t *im)
{
    /* Stage 1: sixteen 25-point DFTs of the real input, then the twiddle. */
    for (uint32_t n1 = 0; n1 < N1; n1++) {
        for (uint32_t k2 = 0; k2 < N2; k2++) {
            int64_t ar = 0, ai = 0;
            uint32_t j = 0, step = (16u * k2) % SPEECH_N_FFT;
            for (uint32_t n2 = 0; n2 < N2; n2++) {
                int64_t v = x[n1 + N1 * n2];
                ar += v * SPEECH_COS_Q30[j];
                ai -= v * SPEECH_SIN_Q30[j];
                j += step;
                if (j >= SPEECH_N_FFT) j -= SPEECH_N_FFT;
            }
            int64_t yr = rshr(ar, 30), yi = rshr(ai, 30);
            uint32_t t = (n1 * k2) % SPEECH_N_FFT;
            int64_t c = SPEECH_COS_Q30[t], s = SPEECH_SIN_Q30[t];
            /* (yr + i yi)(c - i s) */
            w->yr[n1 * N2 + k2] = (int32_t) rshr(yr * c + yi * s, 30);
            w->yi[n1 * N2 + k2] = (int32_t) rshr(yi * c - yr * s, 30);
        }
    }
    /* Stage 2: 16-point DFTs across n1, only for k <= 200 (k1 <= 8). */
    for (uint32_t k1 = 0; k1 <= 8; k1++) {
        for (uint32_t k2 = 0; k2 < N2; k2++) {
            uint32_t k = k2 + N2 * k1;
            if (k >= SPEECH_N_BINS) break;
            int64_t ar = 0, ai = 0;
            uint32_t j = 0, step = (25u * k1) % SPEECH_N_FFT;
            for (uint32_t n1 = 0; n1 < N1; n1++) {
                int64_t zr = w->yr[n1 * N2 + k2], zi = w->yi[n1 * N2 + k2];
                int64_t c = SPEECH_COS_Q30[j], s = SPEECH_SIN_Q30[j];
                ar += zr * c + zi * s;
                ai += zi * c - zr * s;
                j += step;
                if (j >= SPEECH_N_FFT) j -= SPEECH_N_FFT;
            }
            re[k] = (int32_t) rshr(ar, 30);
            im[k] = (int32_t) rshr(ai, 30);
        }
    }
}

#define FB 8u /* fractional bits kept after the window */

void speech_power_spectrum(speech_fft_work_t *w, const int16_t *frame)
{
    int32_t re[SPEECH_N_BINS], im[SPEECH_N_BINS];
    for (uint32_t i = 0; i < SPEECH_N_FFT; i++)
        w->x[i] = (int32_t) rshr((int64_t) frame[i] * SPEECH_HANN_Q30[i], 30 - FB);
    speech_dft400(w, w->x, re, im);
    for (uint32_t k = 0; k < SPEECH_N_BINS; k++) {
        int64_t a = re[k], b = im[k];
        w->power[k] = (uint64_t) (a * a) + (uint64_t) (b * b);
    }
}

static uint32_t msb64(uint64_t v)
{
    uint32_t b = 0;
    while (v >>= 1) b++;
    return b;
}

int32_t speech_log2_q16(uint64_t v)
{
    if (v == 0) return INT32_MIN;
    uint32_t b = msb64(v);
    /* 26 fraction bits below the leading one: top 10 index the table, the
     * low 16 interpolate. */
    uint64_t m = b >= 26 ? (v >> (b - 26)) : (v << (26 - b));
    uint32_t idx = (uint32_t) ((m >> 16) & 0x3FFu);
    uint32_t fr = (uint32_t) (m & 0xFFFFu);
    uint64_t l0 = SPEECH_LOG2_Q30[idx], l1 = SPEECH_LOG2_Q30[idx + 1];
    uint64_t l = l0 + (((l1 - l0) * fr) >> 16); /* Q30 */
    return (int32_t) (b * 65536u + (uint32_t) ((l + (1u << 13)) >> 14));
}

#define LOG10_FLOOR_Q16 (-10 * 65536)

void speech_mel_frame(speech_fft_work_t *w, const int16_t *frame, int32_t *log10_q16)
{
    speech_power_spectrum(w, frame);
    for (uint32_t m = 0; m < SPEECH_N_MELS; m++) {
        /* power (< 2^62) times a Q32 weight (< 2^27) needs ~90 bits: split
         * the power into 32-bit halves and keep a 96-bit sum (hi:lo). */
        uint64_t hi = 0, lo = 0;
        uint32_t k0 = SPEECH_MEL_FIRST[m], cnt = SPEECH_MEL_COUNT[m], off = SPEECH_MEL_OFFSET[m];
        for (uint32_t i = 0; i < cnt; i++) {
            uint64_t p = w->power[k0 + i], wq = SPEECH_MEL_W_Q32[off + i];
            hi += (p >> 32) * wq;
            lo += (p & 0xFFFFFFFFull) * wq;
        }
        hi += lo >> 32;
        lo &= 0xFFFFFFFFull;
        int32_t l2;
        if (hi == 0 && lo == 0) {
            log10_q16[m] = LOG10_FLOOR_Q16;
            continue;
        }
        if (hi >= (1ull << 31))
            l2 = speech_log2_q16(hi) + 32 * 65536;
        else
            l2 = speech_log2_q16((hi << 32) | lo);
        /* mel = sum / 2^32 (Q32 weights) / 2^(2 (15 + FB)) (int16 scale plus
         * the window's fraction bits, squared). */
        int64_t l2t = (int64_t) l2 - (32 + 2 * (15 + (int64_t) FB)) * 65536;
        int64_t l10 = rshr(l2t * SPEECH_LOG10_2_Q30, 30);
        if (l10 < LOG10_FLOOR_Q16) l10 = LOG10_FLOOR_Q16;
        log10_q16[m] = (int32_t) l10;
    }
}

void speech_logmel_finalize(int32_t *v, uint32_t count)
{
    if (!v || count == 0) return;
    int32_t mx = v[0];
    for (uint32_t i = 1; i < count; i++)
        if (v[i] > mx) mx = v[i];
    int32_t lo = mx - 8 * 65536;
    for (uint32_t i = 0; i < count; i++) {
        int32_t x = v[i] < lo ? lo : v[i];
        v[i] = (x + 4 * 65536) / 4;
    }
}

int32_t speech_logmel(speech_fft_work_t *w, const int16_t *pcm, uint32_t n, int32_t *out,
                      uint32_t max_frames)
{
    const uint32_t pad = SPEECH_N_FFT / 2;
    if (!w || !pcm || !out || n <= pad) return -1;
    uint32_t frames = n / SPEECH_HOP;
    if (frames > max_frames) return -1;
    int16_t frame[SPEECH_N_FFT];
    for (uint32_t f = 0; f < frames; f++) {
        for (uint32_t i = 0; i < SPEECH_N_FFT; i++) {
            /* Index into the reflect-padded signal. */
            int64_t p = (int64_t) f * SPEECH_HOP + i - pad;
            if (p < 0) p = -p;
            if (p >= (int64_t) n) p = 2 * ((int64_t) n - 1) - p;
            frame[i] = pcm[p];
        }
        speech_mel_frame(w, frame, out + f * SPEECH_N_MELS);
    }
    speech_logmel_finalize(out, frames * SPEECH_N_MELS);
    return (int32_t) frames;
}
