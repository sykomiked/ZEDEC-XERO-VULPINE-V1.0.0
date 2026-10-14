/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_tts.c — speech out (T26), offline: the OuteTTS text normalisation and
 * prompt, the code mapping, e^x and cos/sin against libm, the integer iSTFT
 * against a float64 iSTFT of the same Q16 spectrogram, and a tiny random
 * WavTokenizer-shaped decoder (test_tts_fixture.h, from gen_tts_fixture.py)
 * against a float64 numpy forward pass of llama.cpp's graph. Built with
 * -DZT_TEST_SIMD and zt_simd*.c it also checks that every kernel set this CPU
 * runs, with and without threads, gives the C reference's bits. Hosted test
 * (libc, libm for the reference values). */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE /* M_PI */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_tts.h"
#include "test_tts_fixture.h"
#ifdef ZT_TEST_SIMD
#    include "zt_simd.h"
#endif

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("[FAIL] ");                                                                     \
        } else                                                                                     \
            printf("[PASS] ");                                                                     \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static bool norm_is(const char *in, const char *want)
{
    uint8_t buf[512];
    uint64_t n = zt_tts_normalize((const uint8_t *) in, strlen(in), buf, sizeof buf - 1);
    uint64_t need = zt_tts_normalize((const uint8_t *) in, strlen(in), 0, 0);
    buf[n < sizeof buf - 1 ? n : sizeof buf - 1] = 0;
    bool ok = n == strlen(want) && need == n && !memcmp(buf, want, n);
    if (!ok) printf("       got '%s'\n", buf);
    return ok;
}

static void test_text(void)
{
#define SP "<|space|>"
    CHECK(
        norm_is("Hello world, it is 3 pm.", "hello" SP "world" SP "it" SP "is" SP "three" SP "pm"),
        "normalise: llama.cpp's own example");
    CHECK(norm_is("  I paid $1,234.56!  ", "i" SP "paid" SP "one" SP "two" SP "hundred" SP
                                           "thirty" SP "four" SP "point" SP "five" SP "six"),
          "normalise: comma splits the number, decimals digit by digit, $ and ! dropped");
    CHECK(norm_is("0.05 and 1000000 and 2147483647",
                  "zero" SP "point" SP "zero" SP "five" SP "and" SP "one" SP "million" SP "and" SP
                  "two" SP "billion" SP "one" SP "hundred" SP "forty" SP "seven" SP "million" SP
                  "four" SP "hundred" SP "eighty" SP "three" SP "thousand" SP "six" SP "hundred" SP
                  "forty" SP "seven"),
          "normalise: zero, millions, INT_MAX");
    CHECK(norm_is("x3000000000y 1.2.3", "x" SP "y" SP "one" SP "point" SP "two" SP "three"),
          "normalise: past INT_MAX becomes a space (std::stoi), 1.2.3 is 1.2 then 3");
    CHECK(norm_is("Don't\tSTOP\n\n_now_ / ok\\caf\xc3\xa9",
                  "dont" SP "stop" SP "now" SP "ok" SP "caf"),
          "normalise: case, - _ / \\ to spaces, other bytes dropped, white space collapsed");
    CHECK(norm_is("", "") && norm_is("?!", ""), "normalise: empty");
    CHECK(norm_is("105 and 20 and 99",
                  "one" SP "hundred" SP "five" SP "and" SP "twenty" SP "and" SP "ninety" SP "nine"),
          "normalise: hundreds and tens");
#undef SP
    const char *t = "Hi 2.";
    uint64_t n = zt_tts_prompt((const uint8_t *) t, strlen(t), 0, 0);
    char *p = malloc(n + 1);
    zt_tts_prompt((const uint8_t *) t, strlen(t), (uint8_t *) p, n);
    p[n] = 0;
    const char *head = "<|im_start|>\n<|text_start|>the<|space|>overall<|space|>";
    const char *mid =
        "lovely<|space|>hi<|space|>two<|text_end|>\n<|audio_start|>\nthe<|t_0.08|><|257|>";
    const char *tail = "<|1677|><|space|>";
    CHECK(
        !strncmp(p, head, strlen(head)) && strstr(p, mid) && !strcmp(p + n - strlen(tail), tail) &&
            !strstr(p, "code_start") && !strstr(p, "text_sep"),
        "prompt: <|im_start|>, speaker words, text, <|text_end|>, speaker codes (v0.3 form), %llu "
        "bytes",
        (unsigned long long) n);
    free(p);
    zt_tts_vocab_t v = {1000, 900, 901, 902};
    int32_t tok[8] = {999, 1000, 5095, 5096, 900, 1500, 198, 1001}, codes[8];
    uint32_t k = zt_tts_codes(&v, tok, 8, codes);
    CHECK(k == 4 && codes[0] == 0 && codes[1] == 4095 && codes[2] == 500 && codes[3] == 1,
          "codes: <|0|>..<|4095|> kept as 0..4095, everything else dropped");
}

static void test_math(void)
{
    double emax = 0, eabs = 0;
    for (int64_t x = -16 * 65536; x <= 10 * 65536; x += 677) {
        double e = exp((double) x / 65536) * 65536, g = zt_tts_exp((zt_fx) x);
        if (x > 0) {
            double r = fabs(g - e) / e;
            if (r > emax) emax = r;
        } else if (fabs(g - e) > eabs) {
            eabs = fabs(g - e);
        }
    }
    CHECK(emax < 4e-5, "e^x, 0 < x <= 10: relative error %.2e (< 4e-5)", emax);
    CHECK(eabs <= 2.0, "e^x, x <= 0: absolute error %.2f Q16 units (<= 2)", eabs);
    CHECK(zt_tts_exp(11 * 65536) == zt_tts_exp(10 * 65536), "e^x saturates at e^10");
    double cmax = 0;
    uint64_t r = 0x1234567;
    for (int i = 0; i < 200000; i++) {
        r = r * 6364136223846793005ull + 1442695040888963407ull;
        zt_fx a = i < 1000 ? (zt_fx) ((i - 500) * 411775 / 64) : (zt_fx) (uint32_t) (r >> 32);
        int32_t c, s;
        zt_tts_cossin(a, &c, &s);
        long double ang = (long double) a / 65536.0L;
        double ec = fabs(c - (double) (cosl(ang) * 1073741824.0L));
        double es = fabs(s - (double) (sinl(ang) * 1073741824.0L));
        if (ec > cmax) cmax = ec;
        if (es > cmax) cmax = es;
    }
    CHECK(cmax <= 4.0, "cos/sin over the whole Q16 range: max error %.2f Q30 units (<= 4)", cmax);
}

static void test_istft(void)
{
    uint32_t n = TF_NI, ns = n * ZT_TTS_HOP;
    uint64_t wb = zt_tts_istft_bytes(n);
    void *w = malloc(wb);
    int16_t *pcm = malloc(ns * 2);
    uint32_t clip = zt_tts_istft(TF_ISPEC, TF_NOUT, n, pcm, w, wb, 0);
    double emax = 0, ss = 0, se = 0;
    for (uint32_t i = 0; i < ns; i++) {
        double e = TF_IPCM[i] / 256.0, d = pcm[i] - e;
        if (fabs(d) > emax) emax = fabs(d);
        ss += e * e;
        se += d * d;
    }
    CHECK(clip == 0 && emax <= 0.6,
          "iSTFT (exp, cos/sin, 1280-point inverse DFT, Hann, overlap-add, envelope) vs float64: "
          "max %.3f LSB of int16 (<= 0.6: rounding only), SNR %.1f dB",
          emax, 10 * log10(ss / se));
    /* one frame of DC only: y = e^3 / 641 everywhere, so the output is
     * y hann / hann^2 = y / hann(i + 480) */
    zt_fx one[TF_NOUT];
    for (uint32_t i = 0; i < TF_NOUT; i++) one[i] = i < ZT_TTS_NBIN ? -30 * 65536 : 0;
    one[0] = 3 * 65536;
    int16_t p1[ZT_TTS_HOP];
    zt_tts_istft(one, TF_NOUT, 1, p1, w, wb, 0);
    bool ok = true;
    for (uint32_t i = 0; i < ZT_TTS_HOP; i++) {
        double hn = 0.5 * (1 - cos(2 * M_PI * (i + 480) / 1280.0));
        double dd = fabs(p1[i] - exp(3.0) / 641 * 32767 / hn);
        ok &= dd <= 0.55; /* rounding, plus e^x's 3.5e-5 relative error */
    }
    CHECK(ok,
          "iSTFT: a DC-only frame gives e^3 / 641 x 32767 / hann within 0.55 LSB (%d at the first "
          "sample)",
          (int) p1[0]);
    CHECK(zt_tts_istft(TF_ISPEC, TF_NOUT, n, pcm, w, wb - 64 - 1, 0) == 0 && wb > 0,
          "iSTFT: too small a work area is refused");
    free(w);
    free(pcm);
}

typedef struct {
    zt_tts_dec_t d;
    void *arena, *work;
    uint64_t wbytes;
} dec_t;

static int load(dec_t *D, uint32_t flags)
{
    zt_gguf_t g;
    zt_model_err_t err;
    static zt_gguf_t gs;
    if (zt_gguf_open(&g, TF_GGUF, sizeof TF_GGUF)) return -1;
    gs = g;
    uint64_t ab = zt_tts_arena_bytes_ex(&gs, flags);
    D->arena = malloc(ab);
    if (!ab || zt_tts_load_ex(&D->d, &gs, D->arena, ab, flags, &err)) {
        printf("load: %s %s\n", err.what, err.name);
        return -1;
    }
    D->wbytes = zt_tts_work_bytes(&D->d, TF_NCODES);
    D->work = malloc(D->wbytes);
    return 0;
}

static void spec_err(const zt_fx *s, double *mx_mag, double *mx_ph, double *rms)
{
    double ss = 0;
    *mx_mag = *mx_ph = 0;
    for (uint32_t i = 0; i < TF_NCODES * TF_NOUT; i++) {
        double d = fabs((double) s[i] - TF_SPEC[i]) / 65536;
        if (i % TF_NOUT < ZT_TTS_NBIN) {
            if (d > *mx_mag) *mx_mag = d;
        } else if (d > *mx_ph) {
            *mx_ph = d;
        }
        ss += d * d;
    }
    *rms = sqrt(ss / (TF_NCODES * TF_NOUT));
}

static double pcm_snr(const int16_t *p, double *mx)
{
    double ss = 0, se = 0;
    *mx = 0;
    for (uint32_t i = 0; i < TF_NCODES * ZT_TTS_HOP; i++) {
        double e = TF_PCM[i] / 256.0, d = p[i] - e;
        if (fabs(d) > *mx) *mx = fabs(d);
        ss += e * e;
        se += d * d;
    }
    return 10 * log10(ss / se);
}

static uint64_t fnv(uint64_t h, const void *p, uint64_t n)
{
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

/* FNV-1a 64 of the fixture's spectrogram (int32 little-endian) then PCM (int16
 * little-endian), as the C reference computed it on x86-64: every machine must
 * give these bits. A change to zt_nn's arithmetic changes it (re-derive it from
 * this test's output, and say why in the commit). */
#define TF_GOLDEN 0xd33803d39bf0c0eeull

static void test_decoder(void)
{
    dec_t D;
    CHECK(load(&D, 0) == 0, "decoder fixture loads (%u codes, %u channels, %u ConvNeXt blocks)",
          D.d.cfg.n_vocab, D.d.cfg.n_embd, D.d.cfg.n_convnext);
    static zt_fx spec[TF_NCODES * TF_NOUT], spec2[TF_NCODES * TF_NOUT];
    static int16_t pcm[TF_NCODES * ZT_TTS_HOP], pcm2[TF_NCODES * ZT_TTS_HOP];
    zt_tts_stats_t st;
    int32_t r = zt_tts_spec(&D.d, TF_CODES, TF_NCODES, spec, D.work, D.wbytes, &st);
    double mm, mp, rms, mx;
    spec_err(spec, &mm, &mp, &rms);
    CHECK(!r && st.n_sat == 0 && mm < 0.002 && mp < 0.004 && rms < 4e-4,
          "decoder spectrogram vs float64: log-magnitude max %.5f, phase max %.5f, RMS %.6f "
          "(< 0.002, 0.004, 4e-4)",
          mm, mp, rms);
    r = zt_tts_decode(&D.d, TF_CODES, TF_NCODES, pcm, 0, D.work, D.wbytes, &st);
    double snr = pcm_snr(pcm, &mx);
    CHECK(!r && st.n_clip == 0 && st.n_samples == TF_NCODES * ZT_TTS_HOP && snr > 50,
          "decoder PCM vs float64: SNR %.1f dB (> 50), max %.1f LSB", snr, mx);
    static uint8_t le[4 * TF_NCODES * TF_NOUT];
    for (uint32_t i = 0; i < TF_NCODES * TF_NOUT; i++)
        for (uint32_t b = 0; b < 4; b++) le[4 * i + b] = (uint8_t) ((uint32_t) spec[i] >> (8 * b));
    uint64_t h = fnv(0xcbf29ce484222325ull, le, sizeof le);
    for (uint32_t i = 0; i < TF_NCODES * ZT_TTS_HOP; i++) {
        uint8_t s2[2] = {(uint8_t) pcm[i], (uint8_t) ((uint16_t) pcm[i] >> 8)};
        h = fnv(h, s2, 2);
    }
    CHECK(h == TF_GOLDEN, "decoder bits: FNV %016llx, the x86-64 C reference's",
          (unsigned long long) h);
    zt_tts_decode(&D.d, TF_CODES, TF_NCODES, pcm2, 1000, D.work, D.wbytes, &st);
    bool sk = true;
    for (uint32_t i = 0; i < TF_NCODES * ZT_TTS_HOP; i++) sk &= pcm2[i] == (i < 1000 ? 0 : pcm[i]);
    CHECK(sk, "decode: the first `skip` samples are zero, the rest unchanged");
#ifdef ZT_TEST_SIMD
    const zt_kern_t *ks[8];
    uint32_t nk = zt_simd_list(ks, 8);
    zt_pool_t *pool = zt_pool_new(3);
    for (uint32_t i = 0; i < nk; i++)
        for (int th = 0; th < 2; th++) {
            zt_kern_t kt;
            zt_kern_threaded(&kt, ks[i], th ? pool : 0);
            D.d.kern = &kt;
            memset(pcm2, 0, sizeof pcm2);
            zt_tts_spec(&D.d, TF_CODES, TF_NCODES, spec2, D.work, D.wbytes, 0);
            zt_tts_decode(&D.d, TF_CODES, TF_NCODES, pcm2, 0, D.work, D.wbytes, 0);
            CHECK(!memcmp(spec, spec2, sizeof spec) && !memcmp(pcm, pcm2, sizeof pcm),
                  "kernel set %s%s: the C reference's bits", ks[i]->name, th ? ", 3 threads" : "");
        }
    D.d.kern = 0;
    zt_pool_free(pool);
#endif
    int32_t bad[2] = {1, 64};
    CHECK(zt_tts_decode(&D.d, bad, 2, pcm2, 0, D.work, D.wbytes, 0) == ZT_NN_EARG,
          "a code outside the codebook is refused");
    CHECK(zt_tts_decode(&D.d, TF_CODES, TF_NCODES, pcm2, 0, D.work, D.wbytes - 65, 0) ==
              ZT_NN_ESPACE,
          "too small a work area is refused");
    /* lean: one 8-bit block per 32 weights */
    dec_t L;
    if (load(&L, ZT_TTS_LOAD_LEAN) == 0) {
        zt_tts_spec(&L.d, TF_CODES, TF_NCODES, spec2, L.work, L.wbytes, 0);
        spec_err(spec2, &mm, &mp, &rms);
        zt_tts_decode(&L.d, TF_CODES, TF_NCODES, pcm2, 0, L.work, L.wbytes, 0);
        snr = pcm_snr(pcm2, &mx);
        CHECK(mm < 0.3 && mp < 0.3 && snr > 25,
              "lean decoder vs float64: log-magnitude max %.4f, phase max %.4f, PCM SNR %.1f dB "
              "(< 0.3, 0.3; > 25: the residual block matters)",
              mm, mp, snr);
        free(L.arena);
        free(L.work);
    }
    /* a wrong architecture is refused */
    uint8_t *b = malloc(sizeof TF_GGUF);
    memcpy(b, TF_GGUF, sizeof TF_GGUF);
    for (uint64_t i = 0; i + 16 < sizeof TF_GGUF; i++)
        if (!memcmp(b + i, "wavtokenizer-dec", 16)) {
            b[i] = 'W';
            break;
        }
    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, b, sizeof TF_GGUF) == 0 && zt_tts_arena_bytes(&g) == 0,
          "a file of another architecture is refused");
    free(b);
    free(D.arena);
    free(D.work);
}

int main(void)
{
    test_text();
    test_math();
    test_istft();
    test_decoder();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
