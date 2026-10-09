/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_speech.c — host tests for the integer audio front end and the speech
 * session. Build (from kernel/):
 *   gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/speech -Isrc/voice \
 *       -Isrc/chiglet -Isrc/surplus src/speech/test_speech.c \
 *       src/speech/speech_dsp.c src/speech/speech_session.c src/voice/voice.c \
 *       src/chiglet/chiglet.c src/surplus/surplus.c -lm -o /tmp/test_speech
 */
#define _DEFAULT_SOURCE /* M_PI */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speech_dsp.h"
#include "speech_session.h"
#include "test_speech_fixture.h"

static int g_pass, g_fail;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

/* ---- the fixture's integer signal generators, mirrored from the .py ---- */
static uint32_t lcg(uint32_t s)
{
    return s * 1664525u + 1013904223u;
}
static int32_t tri(uint32_t ph)
{
    int32_t t = (int32_t) (ph >> 16);
    return t < 32768 ? 2 * t - 32768 : 2 * (65535 - t) - 32767;
}
static int16_t sat(int32_t v)
{
    return (int16_t) (v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}
static void signal_a(int16_t *out)
{
    uint32_t s = 12345, ph = 0;
    for (int32_t i = 0; i < 16000; i++) {
        s = lcg(s);
        int32_t noise = (int32_t) ((s >> 16) & 0x7FF) - 1024, x;
        if (i < 4000) {
            x = 0;
        } else if (i < 12000) {
            uint32_t f = 200u + (5800u * (uint32_t) (i - 4000)) / 8000u;
            ph += f * 268435u;
            x = ((tri(ph) * 12000) >> 15) + noise;
        } else {
            ph += 440u * 268435u;
            x = ((tri(ph) * 300) >> 15) + (noise >> 4);
        }
        out[i] = sat(x);
    }
}
static void signal_b(int16_t *out)
{
    uint32_t s = 999, ph = 0;
    for (int32_t i = 0; i < 8000; i++) {
        s = lcg(s);
        ph += 1000u * 268435u;
        out[i] = sat((int32_t) ((s >> 16) & 0x3F) - 32 + ((tri(ph) * 40) >> 15));
    }
}
static uint32_t fnv16(const int16_t *x, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) h = (h ^ (uint16_t) x[i]) * 16777619u;
    return h;
}

static speech_fft_work_t g_work;
static double g_logmel_tol;

static void check_logmel(const char *name, const int16_t *sig, uint32_t n, const int32_t *ref,
                         uint32_t frames)
{
    static int32_t out[100 * SPEECH_N_MELS];
    int32_t got = speech_logmel(&g_work, sig, n, out, 100);
    CHECK(got == (int32_t) frames, "log-mel frame count matches Whisper (n / 160)");
    double mx = 0, sum = 0;
    uint32_t cnt = frames * SPEECH_N_MELS;
    for (uint32_t i = 0; i < cnt; i++) {
        double e = fabs((double) (out[i] - ref[i])) / 65536.0;
        sum += e;
        if (e > mx) mx = e;
    }
    printf("  %s: %u frames x 80 mels, max |err| %.6f, mean |err| %.7f (Whisper units)\n", name,
           frames, mx, sum / cnt);
    if (mx > g_logmel_tol) g_logmel_tol = mx;
    CHECK(mx <= 0.0005, "log-mel within 0.0005 of the float64 Whisper reference");
    CHECK(sum / cnt <= 0.00002, "log-mel mean error within 0.00002");
}

/* A double-precision Whisper front end built from the same tables, for
 * signals not in the fixture (full scale: overflow check). */
#include "speech_tables.h"
static void ref_logmel(const int16_t *pcm, uint32_t n, double *out)
{
    uint32_t frames = n / 160;
    double mx = -1e300;
    for (uint32_t f = 0; f < frames; f++) {
        double xr[400], pw[201];
        for (int i = 0; i < 400; i++) {
            long p = (long) f * 160 + i - 200;
            if (p < 0) p = -p;
            if (p >= (long) n) p = 2 * ((long) n - 1) - p;
            xr[i] = pcm[p] / 32768.0 * (0.5 - 0.5 * cos(2 * M_PI * i / 400.0));
        }
        for (int k = 0; k < 201; k++) {
            double r = 0, m = 0;
            for (int i = 0; i < 400; i++) {
                double a = -2 * M_PI * ((k * i) % 400) / 400.0;
                r += xr[i] * cos(a);
                m += xr[i] * sin(a);
            }
            pw[k] = r * r + m * m;
        }
        for (int m = 0; m < 80; m++) {
            double acc = 0;
            for (int i = 0; i < SPEECH_MEL_COUNT[m]; i++)
                acc += pw[SPEECH_MEL_FIRST[m] + i] *
                       (SPEECH_MEL_W_Q32[SPEECH_MEL_OFFSET[m] + i] / 4294967296.0);
            double v = log10(acc < 1e-10 ? 1e-10 : acc);
            out[f * 80 + m] = v;
            if (v > mx) mx = v;
        }
    }
    for (uint32_t i = 0; i < frames * 80; i++) {
        double v = out[i] < mx - 8 ? mx - 8 : out[i];
        out[i] = (v + 4) / 4;
    }
}

static void test_logmel_fullscale(void)
{
    static int16_t x[4800];
    static int32_t out[30 * 80];
    static double ref[30 * 80];
    for (int i = 0; i < 4800; i++) x[i] = (i < 1600) ? 32767 : ((i / 9) & 1 ? 32767 : -32768);
    int32_t fr = speech_logmel(&g_work, x, 4800, out, 30);
    ref_logmel(x, 4800, ref);
    double mx = 0;
    for (int i = 0; i < fr * 80; i++) {
        double e = fabs(out[i] / 65536.0 - ref[i]);
        if (e > mx) mx = e;
    }
    printf("  full-scale DC + square wave: max |err| %.6f vs double reference\n", mx);
    if (mx > g_logmel_tol) g_logmel_tol = mx;
    CHECK(fr == 30 && mx <= 0.0005, "full-scale input: no overflow, within 0.0005");
}

static void test_logmel(void)
{
    static int16_t a[16000], b[8000];
    signal_a(a);
    signal_b(b);
    CHECK(fnv16(a, 16000) == FIX_A_FNV, "signal A generator matches the Python fixture");
    CHECK(fnv16(b, 8000) == FIX_B_FNV, "signal B generator matches the Python fixture");
    check_logmel("signal A (silence + chirp + quiet tone)", a, FIX_A_N, FIX_A_REF_Q16,
                 FIX_A_FRAMES);
    check_logmel("signal B (very quiet tone + noise)", b, FIX_B_N, FIX_B_REF_Q16, FIX_B_FRAMES);
    int32_t tiny[SPEECH_N_MELS];
    CHECK(speech_logmel(&g_work, a, 200, tiny, 1) == -1, "log-mel refuses n <= 200 (reflect pad)");
    CHECK(speech_logmel(&g_work, a, 16000, tiny, 1) == -1, "log-mel refuses a short out buffer");
    test_logmel_fullscale();
}

static void test_fft(void)
{
    static int32_t x[SPEECH_N_FFT], re[SPEECH_N_BINS], im[SPEECH_N_BINS];
    uint32_t s = 7;
    double worst = 0;
    for (int trial = 0; trial < 4; trial++) {
        for (uint32_t i = 0; i < SPEECH_N_FFT; i++) {
            s = lcg(s);
            x[i] = (int32_t) (s >> 13) - (1 << 18);
            if (trial == 1) x[i] = (i == 3) ? 100000 : 0;
            if (trial == 2) x[i] = 300000;
            if (trial == 3) x[i] >>= 10; /* small */
        }
        speech_dft400(&g_work, x, re, im);
        double peak = 1;
        for (uint32_t k = 0; k < SPEECH_N_BINS; k++) {
            double r = 0, m = 0;
            for (uint32_t n = 0; n < SPEECH_N_FFT; n++) {
                double ang = -2.0 * M_PI * (double) ((k * n) % SPEECH_N_FFT) / SPEECH_N_FFT;
                r += x[n] * cos(ang);
                m += x[n] * sin(ang);
            }
            double e = fabs(r - re[k]) + fabs(m - im[k]);
            if (e > worst) worst = e;
            if (fabs(r) > peak) peak = fabs(r);
        }
    }
    printf("  integer DFT-400 vs double DFT: max abs error %.2f LSB\n", worst);
    CHECK(worst <= 16.0, "DFT-400 within 16 LSB of a double-precision DFT (|X| up to 2^27)");
    CHECK(speech_log2_q16(1) == 0, "log2(1) == 0");
    CHECK(speech_log2_q16(1024) == 10 * 65536, "log2(1024) == 10");
    CHECK(abs(speech_log2_q16(3) - (int32_t) lround(log2(3.0) * 65536)) <= 1, "log2(3) Q16");
    CHECK(abs(speech_log2_q16(0xFFFFFFFFFFFFull) - (int32_t) lround(48.0 * 65536)) <= 1,
          "log2(2^48-1) Q16");
    CHECK(speech_log2_q16(0) == INT32_MIN, "log2(0) is flagged");
}

static void test_preemph_framer(void)
{
    speech_preemph_t p;
    speech_preemph_init(&p, 31785);
    int16_t in[4] = {1000, 1000, -32768, 32767}, out[4];
    speech_preemph(&p, in, out, 4);
    CHECK(out[0] == 1000, "pre-emphasis: first sample has zero history");
    CHECK(out[1] == 1000 - 970, "pre-emphasis: x - 0.97 x[-1]");
    CHECK(out[2] == -32768, "pre-emphasis saturates low");
    CHECK(out[3] == 32767, "pre-emphasis saturates high");
    speech_preemph_init(&p, 31785);
    int16_t two[2] = {500, 0};
    speech_preemph(&p, two, two, 1);
    speech_preemph(&p, two + 1, two + 1, 1);
    CHECK(two[0] == 500 && two[1] == -485, "pre-emphasis keeps state across calls, in place");

    speech_framer_t f;
    CHECK(speech_framer_init(&f, 401, 160) == -1, "framer refuses frame > 400");
    CHECK(speech_framer_init(&f, 400, 0) == -1, "framer refuses hop 0");
    CHECK(speech_framer_init(&f, 400, 160) == 0, "framer init");
    static int16_t ramp[1000];
    for (int i = 0; i < 1000; i++) ramp[i] = (int16_t) i;
    uint32_t off = 0, frames = 0;
    bool ok = true;
    while (off < 1000) {
        uint32_t used;
        bool ready = speech_framer_push(&f, ramp + off, 1000 - off > 7 ? 7 : 1000 - off, &used);
        off += used;
        if (ready) {
            if (f.buf[0] != (int16_t) (frames * 160) ||
                f.buf[399] != (int16_t) (frames * 160 + 399))
                ok = false;
            frames++;
            speech_framer_advance(&f);
        }
    }
    CHECK(frames == 4, "framer: 1000 samples give 1 + (1000-400)/160 = 4 frames");
    CHECK(ok, "framer: frame k starts at sample 160 k");
}

/* ---- resampler ---- */
static double tone_snr(uint32_t rate, uint32_t freq, uint32_t chunk, double *gain_out)
{
    static int16_t in[96000], out[20000];
    speech_resampler_t r;
    if (speech_resampler_init(&r, rate) != 0) return -1;
    uint32_t n = rate; /* 1 second */
    for (uint32_t i = 0; i < n; i++)
        in[i] = (int16_t) lround(12000.0 * sin(2 * M_PI * freq * (double) i / rate));
    uint32_t got = 0, off = 0;
    while (off < n) {
        uint32_t c = n - off < chunk ? n - off : chunk, used;
        got += speech_resample(&r, in + off, c, &used, out + got, 20000 - got);
        off += used;
    }
    /* Compare the middle half to the ideal 16 kHz tone. */
    double sig = 0, err = 0, en = 0;
    for (uint32_t i = 4000; i < 12000 && i < got; i++) {
        double ideal = 12000.0 * sin(2 * M_PI * freq * (double) i / 16000.0);
        sig += ideal * ideal;
        err += (out[i] - ideal) * (out[i] - ideal);
        en += (double) out[i] * out[i];
    }
    if (gain_out) *gain_out = sqrt(en / sig);
    return 10 * log10(sig / (err + 1e-9));
}

static void test_resampler(void)
{
    speech_resampler_t r;
    CHECK(speech_resampler_init(&r, 7999) == -1, "resampler refuses < 8 kHz");
    CHECK(speech_resampler_init(&r, 96001) == -1, "resampler refuses > 96 kHz");
    CHECK(speech_resampler_init(&r, 16000) == 0 && speech_resampler_latency(&r) == 0,
          "16 kHz is a passthrough");
    int16_t pin[5] = {1, -2, 3, -4, 5}, pout[5];
    uint32_t used;
    CHECK(speech_resample(&r, pin, 5, &used, pout, 5) == 5 && used == 5 &&
              memcmp(pin, pout, sizeof pin) == 0,
          "passthrough is exact");
    static const uint32_t rates[] = {8000, 11025, 22050, 24000, 32000, 44100, 48000, 96000};
    for (unsigned i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        double g;
        double snr = tone_snr(rates[i], 1000, 333, &g);
        printf("  resample %5u -> 16000, 1 kHz tone: SNR %.1f dB, gain %.4f\n", rates[i], snr, g);
        CHECK(snr >= 50.0, "resampled 1 kHz tone SNR >= 50 dB");
        CHECK(fabs(g - 1.0) < 0.01, "resampler passband gain within 1%");
    }
    double g3;
    tone_snr(44100, 3000, 1000, &g3);
    CHECK(fabs(g3 - 1.0) < 0.01, "44.1k -> 16k passes 3 kHz at unity gain");
    /* Stopband: an 11 kHz tone at 48 kHz must vanish (16 kHz Nyquist is 8). */
    double gs;
    tone_snr(48000, 11000, 480, &gs);
    printf("  48 kHz, 11 kHz tone (above the new Nyquist): residual gain %.5f (%.1f dB)\n", gs,
           20 * log10(gs + 1e-12));
    CHECK(gs < 0.001, "aliasing tone attenuated by more than 60 dB");
    /* Chunk independence. */
    static int16_t in[48000], o1[16100], o2[16100];
    uint32_t s = 5;
    for (int i = 0; i < 48000; i++) {
        s = lcg(s);
        in[i] = (int16_t) (s >> 16);
    }
    uint32_t n1 = 0, n2 = 0, off = 0;
    speech_resampler_init(&r, 48000);
    n1 = speech_resample(&r, in, 48000, &used, o1, 16100);
    CHECK(used == 48000, "one-shot push consumes everything when the output fits");
    speech_resampler_init(&r, 48000);
    uint32_t c = 1;
    while (off < 48000) {
        uint32_t k = 48000 - off < c ? 48000 - off : c;
        n2 += speech_resample(&r, in + off, k, &used, o2 + n2, 16100 - n2);
        off += used;
        c = c * 3 % 997 + 1;
    }
    CHECK(n1 == n2 && memcmp(o1, o2, n1 * sizeof(int16_t)) == 0,
          "resampler output independent of chunking");
    CHECK(n1 + 1 >= 16000 - speech_resampler_latency(&r) / 3,
          "48k: about 16000 outputs per second");
    /* Back-pressure: a tiny output buffer consumes only part of the input. */
    speech_resampler_init(&r, 48000);
    uint32_t got = speech_resample(&r, in, 48000, &used, o1, 10);
    CHECK(got == 10 && used < 48000, "output cap limits consumption");
}

/* ---- VAD ---- */
static void test_vad(void)
{
    speech_vad_t v;
    speech_vad_init(&v);
    int16_t fr[160];
    uint32_t s = 77;
    int starts = 0, ends = 0;
    uint32_t start_at = 0, end_at = 0;
    for (uint32_t f = 0; f < 300; f++) {
        for (int i = 0; i < 160; i++) {
            s = lcg(s);
            int32_t noise = (int32_t) ((s >> 16) & 0xFF) - 128; /* background */
            int32_t tone = 0;
            if (f >= 100 && f < 150)
                tone = (int32_t) lround(6000 * sin(2 * M_PI * 220 * (f * 160 + i) / 16000.0));
            fr[i] = sat(noise + tone);
        }
        speech_vad_event_t e = speech_vad_frame(&v, fr, 160);
        if (e == SPEECH_VAD_START) {
            starts++;
            start_at = v.start_frame;
        }
        if (e == SPEECH_VAD_END) {
            ends++;
            end_at = f;
        }
    }
    CHECK(starts == 1 && ends == 1, "VAD: one START and one END for one tone burst");
    CHECK(start_at == 100, "VAD: START reports the onset frame");
    CHECK(end_at == 149 + 30, "VAD: END exactly after the 30-frame hangover");
    /* Noise only, at a loud steady level: no speech. */
    speech_vad_init(&v);
    starts = 0;
    for (uint32_t f = 0; f < 500; f++) {
        for (int i = 0; i < 160; i++) {
            s = lcg(s);
            fr[i] = (int16_t) ((int32_t) ((s >> 16) & 0xFFF) - 2048);
        }
        if (speech_vad_frame(&v, fr, 160) == SPEECH_VAD_START) starts++;
    }
    CHECK(starts == 0, "VAD: steady loud noise is not speech");
    /* A click shorter than the onset debounce is ignored. */
    speech_vad_init(&v);
    starts = 0;
    for (uint32_t f = 0; f < 60; f++) {
        for (int i = 0; i < 160; i++)
            fr[i] = (f == 30 || f == 31) ? (int16_t) (i & 1 ? 9000 : -9000) : 0;
        if (speech_vad_frame(&v, fr, 160) == SPEECH_VAD_START) starts++;
    }
    CHECK(starts == 0, "VAD: a 2-frame click does not trigger START");
    CHECK(speech_vad_frame(&v, fr, 159) == SPEECH_VAD_SILENCE, "VAD: wrong frame size refused");
}

void test_session(void); /* below */

int main(void)
{
    printf("=== speech: integer front end ===\n");
    test_fft();
    test_logmel();
    test_preemph_framer();
    test_resampler();
    test_vad();
    printf("=== speech: session ===\n");
    test_session();
    printf("log-mel tolerance achieved: max |err| %.6f\n", g_logmel_tol);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) printf("[PASS] test_speech (%d checks)\n", g_pass);
    return g_fail ? 1 : 0;
}

/* ===== session tests with fake engines ===== */
typedef struct {
    int events[16];
    char last_transcript[256], last_reply[256], last_caption[256], last_lang[16];
    uint32_t transcript_flags, caption_flags, spoken_samples, spoken_flags, utter_samples;
    int last_error;
    uint32_t stt_calls, stt_n, tts_calls, out_samples;
    int16_t stt_first;
    bool stt_fail, filter_fail;
} rec_t;
static rec_t R;

static void on_event(const speech_event_t *ev, void *ctx)
{
    (void) ctx;
    R.events[ev->type]++;
    switch (ev->type) {
    case SPEECH_EV_TRANSCRIPT:
        snprintf(R.last_transcript, sizeof R.last_transcript, "%s", ev->text);
        snprintf(R.last_lang, sizeof R.last_lang, "%s", ev->lang);
        R.transcript_flags = ev->flags;
        break;
    case SPEECH_EV_REPLY:
        snprintf(R.last_reply, sizeof R.last_reply, "%s", ev->text);
        break;
    case SPEECH_EV_CAPTION:
        snprintf(R.last_caption, sizeof R.last_caption, "%s", ev->text);
        R.caption_flags = ev->flags;
        break;
    case SPEECH_EV_SPOKEN:
        R.spoken_samples = ev->samples;
        R.spoken_flags = ev->flags;
        break;
    case SPEECH_EV_UTTERANCE_END:
        R.utter_samples = ev->samples;
        break;
    case SPEECH_EV_ERROR:
        R.last_error = ev->code;
        break;
    default:
        break;
    }
}
static int fake_stt(const int16_t *pcm, uint32_t n, const char *hint, char *text, uint32_t cap,
                    char *lang, uint32_t lcap, void *ctx)
{
    (void) hint;
    (void) ctx;
    R.stt_calls++;
    R.stt_n = n;
    R.stt_first = pcm[0];
    if (R.stt_fail) return -1;
    snprintf(text, cap, "hello world");
    snprintf(lang, lcap, "en");
    return 0;
}
static int fake_tts(const char *text, const char *lang, int16_t *pcm, uint32_t cap, uint32_t *n,
                    void *ctx)
{
    (void) lang;
    (void) ctx;
    R.tts_calls++;
    uint32_t want = (uint32_t) strlen(text) * 10;
    for (uint32_t i = 0; i < want && i < cap; i++) pcm[i] = 1000;
    *n = want > cap ? cap + 999 : want; /* a lying engine on overflow */
    return 0;
}
static int fake_reply(const char *in, const char *lang, char *out, uint32_t cap, void *ctx)
{
    (void) lang;
    (void) ctx;
    snprintf(out, cap, "you said: %s", in);
    return 0;
}
static int fake_filter(const char *in, const char *src, char *out, uint32_t cap, void *ctx)
{
    (void) ctx;
    if (R.filter_fail) return -1;
    snprintf(out, cap, "[%s->es] %s", src, in);
    return 1;
}
static int fake_out(const int16_t *pcm, uint32_t n, void *ctx)
{
    (void) pcm;
    (void) ctx;
    R.out_samples += n;
    return 0;
}
/* voice.h phoneme engines: "helo" */
static int ph_stt(const int16_t *pcm, uint32_t n, uint8_t *ph, uint32_t cap, uint32_t *out,
                  void *ctx)
{
    (void) pcm;
    (void) n;
    (void) ctx;
    static const uint8_t w[4] = {PH_HH, PH_EH, PH_L, PH_OW};
    uint32_t k = cap < 4 ? cap : 4;
    for (uint32_t i = 0; i < k; i++) ph[i] = w[i];
    *out = k;
    return 0;
}
static int ph_tts(const uint8_t *ph, uint32_t n, int16_t *pcm, uint32_t cap, uint32_t *out,
                  void *ctx)
{
    (void) ph;
    (void) ctx;
    uint32_t k = n * 100 < cap ? n * 100 : cap;
    for (uint32_t i = 0; i < k; i++) pcm[i] = 7;
    *out = k;
    return 0;
}

static int16_t g_cap[16000 * 30], g_tts[64000];
static speech_session_t g_s;

static void reset_rec(void)
{
    memset(&R, 0, sizeof R);
}
static void feed_tone(speech_session_t *s, uint32_t rate, uint32_t ms, int amp)
{
    static int16_t buf[4800];
    uint32_t n = rate / 100; /* 10 ms chunks */
    for (uint32_t c = 0; c < ms / 10; c++) {
        for (uint32_t i = 0; i < n; i++)
            buf[i] =
                (int16_t) (amp ? lround(amp * sin(2 * M_PI * 300 * (double) (c * n + i) / rate))
                               : 0);
        speech_session_feed(s, buf, n);
    }
}
static void feed_silence(speech_session_t *s, uint32_t rate, uint32_t ms)
{
    feed_tone(s, rate, ms, 0);
}

static void session_init(speech_mode_t mode, speech_use_t use, uint32_t rate)
{
    speech_config_t cfg;
    speech_config_defaults(&cfg);
    cfg.mode = mode;
    cfg.use = use;
    cfg.input_rate = rate;
    cfg.on_event = on_event;
    cfg.reply = fake_reply;
    cfg.caption_filter = fake_filter;
    cfg.audio_out = fake_out;
    speech_session_init(&g_s, &cfg, g_cap, 16000 * 30, g_tts, 64000);
}

void test_session(void)
{
    speech_stt_ops_t stt = {fake_stt, 0};
    speech_tts_ops_t tts = {fake_tts, 0};

    /* Fail closed: nothing bound. */
    reset_rec();
    session_init(SPEECH_MODE_PUSH_TO_TALK, SPEECH_USE_ASSISTANT, 48000);
    CHECK(speech_session_ptt_up(&g_s) == SPEECH_ERR_STATE, "ptt_up while idle is a state error");
    CHECK(speech_session_arm(&g_s) == SPEECH_ERR_STATE, "arm in push-to-talk mode is refused");
    speech_session_ptt_down(&g_s);
    feed_tone(&g_s, 48000, 500, 8000);
    CHECK(speech_session_ptt_up(&g_s) == SPEECH_ERR_NO_ENGINE, "no STT bound: NO_ENGINE");
    CHECK(R.events[SPEECH_EV_ERROR] == 1 && R.last_error == SPEECH_ERR_NO_ENGINE,
          "no STT: one ERROR event");
    CHECK(R.events[SPEECH_EV_TRANSCRIPT] == 0 && R.events[SPEECH_EV_SPOKEN] == 0 &&
              R.out_samples == 0,
          "no STT: no transcript, no audio (nothing fabricated)");
    CHECK(speech_session_state(&g_s) == SPEECH_ST_IDLE, "back to idle after the error");
    CHECK(speech_session_say(&g_s, "hi", 0) == SPEECH_ERR_NO_ENGINE && R.out_samples == 0,
          "read-aloud with no TTS fails closed");

    /* Push-to-talk voice chat at 48 kHz. */
    reset_rec();
    session_init(SPEECH_MODE_PUSH_TO_TALK, SPEECH_USE_ASSISTANT, 48000);
    speech_session_bind_stt(&g_s, &stt);
    speech_session_bind_tts(&g_s, &tts);
    feed_tone(&g_s, 48000, 200, 8000); /* before ptt: ignored */
    speech_session_ptt_down(&g_s);
    CHECK(speech_session_state(&g_s) == SPEECH_ST_LISTENING, "ptt_down -> listening");
    feed_tone(&g_s, 48000, 1000, 8000);
    CHECK(speech_session_ptt_up(&g_s) == SPEECH_OK, "ptt_up runs the turn");
    CHECK(R.stt_calls == 1, "STT called once per utterance");
    CHECK(R.stt_n >= 15900 && R.stt_n <= 16000, "1 s at 48 kHz reaches STT as ~16000 samples");
    CHECK(strcmp(R.last_transcript, "hello world") == 0 && strcmp(R.last_lang, "en") == 0,
          "transcript and detected language are reported");
    CHECK(strcmp(R.last_reply, "you said: hello world") == 0, "reply hook answers the transcript");
    CHECK(R.tts_calls == 1 && R.spoken_samples == strlen("you said: hello world") * 10 &&
              R.out_samples == R.spoken_samples,
          "reply is synthesized and handed to the speaker");
    CHECK(speech_session_state(&g_s) == SPEECH_ST_SPEAKING, "speaking until playback is done");
    speech_session_ptt_down(&g_s);
    CHECK(R.events[SPEECH_EV_BARGE_IN] == 1, "ptt_down while speaking is a barge-in");
    speech_session_stop(&g_s);
    speech_session_say(&g_s, "x", 0);
    speech_session_playback_done(&g_s);
    CHECK(speech_session_state(&g_s) == SPEECH_ST_IDLE, "playback_done returns to idle");

    /* A lying TTS engine (out_len > cap) is clamped. */
    reset_rec();
    static char longtext[1000];
    memset(longtext, 'a', 999);
    speech_session_init(&g_s, &g_s.cfg, g_cap, 16000 * 30, g_tts, 5000);
    speech_session_bind_tts(&g_s, &tts);
    speech_session_say(&g_s, longtext, "en");
    CHECK(R.spoken_samples == 5000 && R.out_samples == 5000, "TTS out_len clamped to the buffer");

    /* Typed prompt into the same conversation; replies not spoken. */
    reset_rec();
    session_init(SPEECH_MODE_PUSH_TO_TALK, SPEECH_USE_ASSISTANT, 16000);
    g_s.cfg.speak_replies = false;
    speech_session_bind_tts(&g_s, &tts);
    CHECK(speech_session_submit_text(&g_s, "plan my day") == SPEECH_OK, "typed prompt accepted");
    CHECK(strcmp(R.last_reply, "you said: plan my day") == 0 && R.tts_calls == 0,
          "typed prompt gets a text reply, not spoken when speak_replies is off");
    g_s.cfg.reply = 0;
    CHECK(speech_session_submit_text(&g_s, "x") == SPEECH_ERR_NO_REPLY, "no reply hook: NO_REPLY");

    /* Hands-free dictation at 16 kHz with the VAD. */
    reset_rec();
    session_init(SPEECH_MODE_HANDS_FREE, SPEECH_USE_DICTATION, 16000);
    speech_session_bind_stt(&g_s, &stt);
    CHECK(speech_session_arm(&g_s) == SPEECH_OK && speech_session_state(&g_s) == SPEECH_ST_ARMED,
          "hands-free armed");
    feed_silence(&g_s, 16000, 500);
    feed_tone(&g_s, 16000, 1000, 6000);
    feed_silence(&g_s, 16000, 1000);
    CHECK(R.events[SPEECH_EV_UTTERANCE_START] == 1 && R.stt_calls == 1,
          "one utterance detected and transcribed");
    CHECK(R.events[SPEECH_EV_REPLY] == 0 && R.events[SPEECH_EV_SPOKEN] == 0,
          "dictation does not reply or speak");
    CHECK(R.utter_samples >= 16000 + 4000 && R.utter_samples <= 16000 + 4800 + 4800 + 160,
          "utterance = pre-roll + speech + hangover");
    CHECK(R.stt_first == 0, "pre-roll starts the capture before the first loud sample");
    CHECK(speech_session_state(&g_s) == SPEECH_ST_ARMED, "re-armed for the next utterance");

    /* Live captions through a translation filter. */
    reset_rec();
    session_init(SPEECH_MODE_HANDS_FREE, SPEECH_USE_CAPTIONS, 44100);
    speech_session_bind_stt(&g_s, &stt);
    speech_session_arm(&g_s);
    feed_silence(&g_s, 44100, 300);
    feed_tone(&g_s, 44100, 600, 6000);
    feed_silence(&g_s, 44100, 600);
    CHECK(strcmp(R.last_caption, "[en->es] hello world") == 0 &&
              (R.caption_flags & SPEECH_EVF_MACHINE_TRANSLATED),
          "caption is translated and marked machine-translated");
    R.filter_fail = true;
    feed_tone(&g_s, 44100, 600, 6000);
    feed_silence(&g_s, 44100, 600);
    CHECK(strcmp(R.last_caption, "hello world") == 0 &&
              !(R.caption_flags & SPEECH_EVF_MACHINE_TRANSLATED),
          "filter failure: original caption, not marked translated");

    /* Hands-free assistant: barge-in over playback. */
    reset_rec();
    session_init(SPEECH_MODE_HANDS_FREE, SPEECH_USE_ASSISTANT, 16000);
    speech_session_bind_stt(&g_s, &stt);
    speech_session_bind_tts(&g_s, &tts);
    speech_session_arm(&g_s);
    feed_silence(&g_s, 16000, 300);
    feed_tone(&g_s, 16000, 500, 6000);
    feed_silence(&g_s, 16000, 400);
    CHECK(speech_session_state(&g_s) == SPEECH_ST_SPEAKING, "hands-free reply is playing");
    feed_tone(&g_s, 16000, 300, 6000);
    CHECK(R.events[SPEECH_EV_BARGE_IN] == 1 && speech_session_state(&g_s) == SPEECH_ST_LISTENING,
          "speaking over the reply barges in and listens");
    feed_silence(&g_s, 16000, 400);
    CHECK(R.stt_calls == 2, "the barge-in utterance is transcribed");
    /* Without barge-in, speech during playback is ignored. */
    speech_session_stop(&g_s);
    g_s.cfg.barge_in = false;
    speech_session_arm(&g_s);
    feed_silence(&g_s, 16000, 300);
    feed_tone(&g_s, 16000, 500, 6000);
    feed_silence(&g_s, 16000, 400);
    uint32_t calls = R.stt_calls;
    feed_tone(&g_s, 16000, 500, 6000);
    feed_silence(&g_s, 16000, 400);
    CHECK(R.stt_calls == calls && speech_session_state(&g_s) == SPEECH_ST_SPEAKING,
          "barge-in off: the assistant does not hear over itself");

    /* Length limit. */
    reset_rec();
    session_init(SPEECH_MODE_PUSH_TO_TALK, SPEECH_USE_DICTATION, 16000);
    g_s.cfg.max_utterance = 8000;
    speech_session_bind_stt(&g_s, &stt);
    speech_session_ptt_down(&g_s);
    feed_tone(&g_s, 16000, 1000, 5000);
    CHECK(R.stt_calls == 1 && R.stt_n == 8000, "max utterance ends the capture");
    CHECK(R.transcript_flags & SPEECH_EVF_TRUNCATED, "length-limited utterance is flagged");

    /* STT engine failure. */
    reset_rec();
    session_init(SPEECH_MODE_PUSH_TO_TALK, SPEECH_USE_DICTATION, 16000);
    speech_session_bind_stt(&g_s, &stt);
    R.stt_fail = true;
    speech_session_ptt_down(&g_s);
    feed_tone(&g_s, 16000, 100, 5000);
    CHECK(speech_session_ptt_up(&g_s) == SPEECH_ERR_ENGINE && R.events[SPEECH_EV_TRANSCRIPT] == 0,
          "STT failure: ENGINE error, no transcript");

    /* voice.h fallback: phonemes, flagged lossy. */
    reset_rec();
    voice_t v;
    voice_init(&v);
    voice_stt_ops_t vs = {ph_stt, 0};
    voice_tts_ops_t vt = {ph_tts, 0};
    voice_set_stt(&v, &vs);
    voice_set_tts(&v, &vt);
    session_init(SPEECH_MODE_PUSH_TO_TALK, SPEECH_USE_ASSISTANT, 16000);
    speech_session_bind_voice(&g_s, &v);
    speech_session_ptt_down(&g_s);
    feed_tone(&g_s, 16000, 100, 5000);
    speech_session_ptt_up(&g_s);
    CHECK(strcmp(R.last_transcript, "helo") == 0 && (R.transcript_flags & SPEECH_EVF_LOSSY),
          "voice.h STT fallback renders phonemes, flagged lossy");
    CHECK(R.spoken_samples > 0 && (R.spoken_flags & SPEECH_EVF_LOSSY),
          "voice.h TTS fallback speaks, flagged lossy");
    speech_session_bind_stt(&g_s, &stt);
    speech_session_stop(&g_s);
    speech_session_ptt_down(&g_s);
    feed_tone(&g_s, 16000, 100, 5000);
    speech_session_ptt_up(&g_s);
    CHECK(strcmp(R.last_transcript, "hello world") == 0 && !(R.transcript_flags & SPEECH_EVF_LOSSY),
          "a text-level engine takes precedence over voice.h");

    speech_config_t bad;
    speech_config_defaults(&bad);
    bad.input_rate = 4000;
    CHECK(speech_session_init(&g_s, &bad, g_cap, 16000, g_tts, 100) == SPEECH_ERR_ARG,
          "init refuses an unsupported microphone rate");
    speech_config_defaults(&bad);
    CHECK(speech_session_init(&g_s, &bad, g_cap, 100, g_tts, 100) == SPEECH_ERR_ARG,
          "init refuses a capture buffer smaller than the pre-roll");
    CHECK(strcmp(speech_state_name(SPEECH_ST_ARMED), "armed") == 0, "state names");
}
