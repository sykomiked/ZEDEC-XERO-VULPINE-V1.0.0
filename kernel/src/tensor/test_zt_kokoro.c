/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_kokoro.c — Kokoro speech out (T27). Offline: phoneme ids, the
 * harmonic source's determinism, the source STFT's magnitudes and phases and
 * the inverse STFT against double-precision sums. With ZXV_KOKORO_MODEL set
 * to tools/kokoro/kokoro2gguf.py's output it also speaks ZXV_KOKORO_PH
 * (misaki phonemes) in ZXV_KOKORO_VOICE (af_heart) to ZXV_KOKORO_WAV. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zt_kokoro.c"
#include "zt_simd.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void test_ids(void)
{
    static zt_kokoro_t k;
    memset(&k, 0, sizeof k);
    k.cfg.n_token = 178;
    k.vocab_cp[4] = ',';
    k.vocab_cp[16] = ' ';
    k.vocab_cp[50] = 'h';
    k.vocab_cp[83] = 0x259;   /* ə */
    k.vocab_cp[156] = 0x2C8;  /* ˈ */
    k.vocab_cp[177] = 0x1D4A; /* ᵊ, three bytes */
    int32_t ids[16];
    const char *s = "h\xC9\x99\xCB\x88 ,\xE1\xB5\x8A";
    uint32_t n = zt_kokoro_ids(&k, s, (uint32_t) strlen(s), ids, 16);
    static const int32_t want[] = {0, 50, 83, 156, 16, 4, 177, 0};
    CHECK(n == 8 && !memcmp(ids, want, sizeof want), "ids with boundaries (n %u)", n);
    const char *u = "h\xF0\x9F\x98\x80h"; /* an emoji: unknown, skipped */
    n = zt_kokoro_ids(&k, u, (uint32_t) strlen(u), ids, 16);
    CHECK(n == 4 && ids[1] == 50 && ids[2] == 50, "unknown code point skipped (n %u)", n);
    CHECK(zt_kokoro_ids(&k, "\xF0\x9F", 2, ids, 16) == 0, "a cut sequence alone: nothing");
    CHECK(zt_kokoro_ids(&k, "hhhh", 4, ids, 5) == 0, "would not fit: 0");
    CHECK(zt_kokoro_ids(&k, "hhh", 3, ids, 5) == 5, "just fits");
    CHECK(zt_kokoro_ids(&k, "xyz", 3, ids, 16) == 0, "nothing known: 0");
}

static void test_atan2(void)
{
    double worst = 0;
    for (int i = 0; i < 720; i++) {
        double a = (i - 359.5) * M_PI / 360;
        for (int m = 0; m < 3; m++) {
            double r = m == 0 ? 3e3 : m == 1 ? 1e9 : 4e18;
            int64_t x = (int64_t) (r * cos(a)), y = (int64_t) (r * sin(a));
            double e = fabs(atan2_q16(y, x) / 65536.0 - atan2((double) y, (double) x));
            if (e > worst) worst = e;
        }
    }
    CHECK(worst < 4.0 / 65536, "atan2 within 4 Q16 steps (worst %.2g)", worst);
    CHECK(atan2_q16(0, 0) == 0, "atan2(0, 0) = 0");
    CHECK(abs(atan2_q16(0, -5) - 205887) <= 1, "atan2(0, -1) = pi");
}

static void test_stft(void)
{
    enum { L = 3000 };
    static zt_fx src[L];
    static zt_fx har[(L / HOP + 1) * 22];
    for (int i = 0; i < L; i++) src[i] = (zt_fx) (65536 * 0.6 * sin(i * 0.37) * cos(i * 0.011));
    uint32_t fr = src_stft(src, L, har);
    CHECK(fr == L / HOP + 1, "frames");
    double wm = 0, wp = 0;
    for (uint32_t f = 0; f < fr; f++) {
        double xw[NFFT];
        for (int j = 0; j < (int) NFFT; j++) {
            int s = (int) f * HOP + j - NFFT / 2;
            if (s < 0) s = -s;
            if (s >= L) s = 2 * (L - 1) - s;
            xw[j] = src[s] / 65536.0 * (0.5 - 0.5 * cos(2 * M_PI * j / NFFT));
        }
        for (int m = 0; m < (int) NBIN; m++) {
            double re = 0, im = 0;
            for (int j = 0; j < (int) NFFT; j++) {
                re += xw[j] * cos(2 * M_PI * m * j / NFFT);
                im -= xw[j] * sin(2 * M_PI * m * j / NFFT);
            }
            double mag = hypot(re, im);
            double em = fabs(har[f * 22 + m] / 65536.0 - mag);
            if (em > wm) wm = em;
            if (mag > 1e-3) {
                double ep = fabs(har[f * 22 + NBIN + m] / 65536.0 - atan2(im, re));
                if (ep > M_PI) ep = 2 * M_PI - ep;
                if (ep > wp) wp = ep;
            }
        }
    }
    CHECK(wm < 1e-4 && wp < 2e-2, "source STFT magnitudes (%.2g) and phases (%.2g)", wm, wp);
}

static void test_istft(void)
{
    enum { FR = 200 };
    static zt_fx spec[FR * 22];
    static int16_t pcm[FR * HOP + 64];
    static int64_t acc[FR * HOP + 64], env[FR * HOP + 64];
    kwork_t w;
    memset(&w, 0, sizeof w);
    w.acc = acc;
    w.env = env;
    for (int f = 0; f < FR; f++)
        for (int m = 0; m < (int) NBIN; m++) {
            spec[f * 22 + m] = (zt_fx) (65536 * (-2.0 + 0.3 * sin(f * 0.1 + m)));
            spec[f * 22 + NBIN + m] = (zt_fx) (65536 * (1.3 * cos(f * 0.07 * (m + 1))));
        }
    uint32_t clip = 9;
    uint32_t n = istft(spec, FR, pcm, &clip, &w);
    CHECK(n == (FR - 1) * HOP, "samples (%u)", n);
    /* double reference */
    static double y[FR * HOP + 64], e[FR * HOP + 64];
    memset(y, 0, sizeof y);
    memset(e, 0, sizeof e);
    for (int f = 0; f < FR; f++) {
        double re[NBIN], im[NBIN];
        for (int m = 0; m < (int) NBIN; m++) {
            double mag = exp(spec[f * 22 + m] / 65536.0),
                   ph = sin(spec[f * 22 + NBIN + m] / 65536.0);
            re[m] = mag * cos(ph);
            im[m] = mag * sin(ph);
        }
        for (int j = 0; j < (int) NFFT; j++) {
            double v = re[0] + ((j & 1) ? -re[10] : re[10]);
            for (int m = 1; m < 10; m++)
                v += 2 * (re[m] * cos(2 * M_PI * m * j / 20) - im[m] * sin(2 * M_PI * m * j / 20));
            double wj = 0.5 - 0.5 * cos(2 * M_PI * j / 20);
            y[f * HOP + j] += v / 20 * wj;
            e[f * HOP + j] += wj * wj;
        }
    }
    int worst = 0;
    for (uint32_t i = 0; i < n; i++) {
        double v = y[i + 10] / (e[i + 10] > 1e-11 ? e[i + 10] : 1);
        int q = (int) lrint(v * 32767);
        if (abs(q - pcm[i]) > worst) worst = abs(q - pcm[i]);
    }
    CHECK(worst <= 2 && clip == 0, "iSTFT within 2 LSB of doubles (worst %d)", worst);
}

static void test_source(void)
{
    enum { N = 40 };
    static zt_fx f0[N];
    static uint64_t cum[N * 9];
    static zt_fx src[N * UP], src2[N * UP];
    static int32_t lw[9] = {1 << 14, 1 << 13, 1 << 12, 0, 0, 0, 0, 0, 1 << 10};
    static zt_fx lb[1] = {1000};
    static zt_kokoro_t k;
    k.l_lin.g = lw;
    k.l_lin.frac = 16;
    k.l_lin_b = lb;
    for (int i = 0; i < N; i++) f0[i] = (zt_fx) (65536 * (i < 10 ? 0 : 180 + 3 * i));
    kwork_t w;
    memset(&w, 0, sizeof w);
    w.cum = cum;
    w.src = src;
    source(&k, f0, N, &w);
    w.src = src2;
    source(&k, f0, N, &w);
    CHECK(!memcmp(src, src2, sizeof src), "the source is the same every time");
    /* voiced steps carry the fundamental: compare with the float phase */
    double worst = 0;
    for (int s = 15 * (int) UP; s < 35 * (int) UP; s++) {
        double sc = (s + 0.5) / UP - 0.5;
        int i0 = (int) floor(sc), i1 = i0 + 1 < N ? i0 + 1 : i0;
        double fr = sc - i0, cum0 = 0;
        for (int i = 0; i <= i0; i++) cum0 += f0[i] / 65536.0 / 24000;
        double ph = UP * cum0 + UP * fr * (f0[i1] / 65536.0 / 24000);
        double c0 = 0.25 * 0.1 * sin(2 * M_PI * ph), got = 0;
        (void) got;
        /* the l_linear sum minus the noise is about c0 + harmonics; check
         * just that the sign of the fundamental lines up most of the time */
        double v = atanh(src[s] / 65536.0 > 0.999 ? 0.999 : src[s] / 65536.0) - 1000 / 65536.0;
        if (fabs(c0) > 0.02 && (v > 0) != (c0 > 0)) worst += 1;
    }
    CHECK(worst < 200, "the voiced source follows the fundamental's phase (%g sign flips)", worst);
}

static int write_wav(const char *path, const int16_t *pcm, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t rate = ZT_KOKORO_RATE, bytes = n * 2, v;
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    uint16_t pf = 1, ch = 1, bl = 2, bits = 16;
    fwrite(&pf, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    v = rate * 2;
    fwrite(&v, 4, 1, f);
    fwrite(&bl, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(pcm, 2, n, f);
    fclose(f);
    return 0;
}

static void test_real(void)
{
    const char *mp = getenv("ZXV_KOKORO_MODEL");
    if (!mp) {
        printf("[skip] real Kokoro (set ZXV_KOKORO_MODEL)\n");
        return;
    }
    const char *ph = getenv("ZXV_KOKORO_PH");
    if (!ph)
        ph = "h\xC9\x99l\xCB\x88O, \xCB\x88I \xC3\xA6m j\xCA\x8A\xC9\xB9 "
             "\xCA\xA7\xCB\x88\xC9\xAA\xC9\xA1l\xC9\x99t.";
    const char *vn = getenv("ZXV_KOKORO_VOICE");
    if (!vn) vn = "af_heart";
    FILE *f = fopen(mp, "rb");
    CHECK(f, "open %s", mp);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t) len);
    size_t got = fread(file, 1, (size_t) len, f);
    fclose(f);
    CHECK(got == (size_t) len, "read the model");
    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, file, (uint64_t) len) == ZT_GGUF_OK, "parse the GGUF");
    uint64_t ab = zt_kokoro_arena_bytes(&g);
    CHECK(ab > 0, "arena size");
    void *arena = malloc(ab);
    static zt_kokoro_t k;
    zt_model_err_t err;
    int32_t r = zt_kokoro_load(&k, &g, arena, ab, &err);
    CHECK(r == ZT_GGUF_OK, "load: %d %s %s", r, err.what, err.name);
    if (r) return;
    zt_pool_t *pool = 0;
    zt_kern_t kern;
    if (!getenv("ZXV_KOKORO_C")) {
        pool = zt_pool_new(zt_pool_ncpu() > 1 ? zt_pool_ncpu() : 1);
        zt_kern_threaded(&kern, zt_simd_best(), pool);
        k.kern = &kern;
    }
    int32_t ids[ZT_KOKORO_MAX_TOK];
    uint32_t n = zt_kokoro_ids(&k, ph, (uint32_t) strlen(ph), ids, ZT_KOKORO_MAX_TOK);
    uint32_t ncp = 0;
    for (const char *p = ph; *p; p++) ncp += ((uint8_t) *p & 0xC0) != 0x80;
    zt_fx style[ZT_KOKORO_STYLE];
    CHECK(zt_kokoro_voice(&k, vn, ncp, style) == ZT_GGUF_OK, "voice %s", vn);
    uint32_t maxf = 40 * 30;
    uint64_t wb = zt_kokoro_work_bytes(&k, n, maxf);
    void *work = malloc(wb);
    uint32_t cap = maxf * ZT_KOKORO_HOP_SAMPLES;
    int16_t *pcm = malloc((size_t) cap * 2);
    zt_kokoro_stats_t st;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    r = zt_kokoro_synth(&k, ids, n, style, 100, maxf, pcm, cap, work, wb, &st);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    CHECK(r == ZT_GGUF_OK, "synth: %d", r);
    if (r == ZT_GGUF_OK) {
        printf("tokens %u frames %u samples %u clipped %u sat %u, %.2f s for %.2f s of speech, "
               "work %.1f MB\n",
               st.tokens, st.frames, st.samples, st.clipped, st.n_sat, secs,
               st.samples / (double) ZT_KOKORO_RATE, wb / 1e6);
        printf("dur");
        for (uint32_t i = 0; i < st.tokens; i++) printf(" %d", st.dur[i]);
        printf("\nf0");
        for (uint32_t i = 0; i < 2 * st.frames; i += 4) printf(" %.0f", st.f0[i] / 65536.0);
        printf("\n");
        const char *wp = getenv("ZXV_KOKORO_WAV");
        if (wp) CHECK(write_wav(wp, pcm, st.samples) == 0, "write %s", wp);
    }
    zt_pool_free(pool);
    free(pcm);
    free(work);
    free(arena);
    free(file);
}

int main(void)
{
    test_ids();
    test_atan2();
    test_stft();
    test_istft();
    test_source();
    test_real();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
