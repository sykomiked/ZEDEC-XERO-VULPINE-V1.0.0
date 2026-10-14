/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zt_whisper.c — T25 speech in: zt_whisper on a tiny random Whisper
 * (test_whisper_fixture.h, from gen_whisper_fixture.py) against float64
 * reference values of whisper's own semantics: the log-mel of a test signal,
 * the encoder output, teacher-forced decoder logits, language detection and
 * the greedy transcript; plus the log-mel front end on edge cases (silence,
 * full-scale input, longer than the window) and the tokenizer reading the
 * same file. Built with -DZT_TEST_SIMD and zt_simd*.c it also checks that
 * every kernel set this CPU runs, with and without threads, gives the C
 * reference's bits. Offline, hosted (libc, libm for the checks). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_whisper.h"
#include "zt_tok.h"
#include "test_whisper_fixture.h"
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

static uint64_t mix(uint64_t seed, uint64_t i)
{
    uint64_t z = seed + (i + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* gen_whisper_fixture.py's pcm_recipe */
static void make_pcm(int16_t *pcm)
{
    for (int64_t n = 0; n < (int64_t) WF_N_PCM; n++) {
        if (n >= 8000 && n < 9200) {
            pcm[n] = 0;
            continue;
        }
        int64_t env = n < 4000 ? n : n < 8000 ? 4000 : 12000 - n;
        int64_t a = env * 6000 / 4000;
        int64_t noise = (int64_t) (mix(WF_PCM_SEED, (uint64_t) n) % 2001) - 1000;
        pcm[n] = (int16_t) (((n / 20) & 1 ? a : -a) + noise);
    }
}

typedef struct {
    double max_abs, rel_rms;
} gap_t;

static gap_t gap(const zt_fx *z, const int32_t *ref, uint64_t n)
{
    double e2 = 0, r2 = 0, mx = 0;
    for (uint64_t i = 0; i < n; i++) {
        double e = (z[i] - (double) ref[i]) / 65536.0, r = ref[i] / 65536.0;
        e2 += e * e;
        r2 += r * r;
        if (fabs(e) > mx) mx = fabs(e);
    }
    gap_t g = {mx, sqrt(e2 / (r2 > 0 ? r2 : 1))};
    return g;
}

static void *alloc64(uint64_t n)
{
    void *p = NULL;
    return posix_memalign(&p, 64, n ? n : 64) ? NULL : p;
}

int main(void)
{
    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, (const uint8_t *) WF_GGUF, WF_GGUF_LEN) == ZT_GGUF_OK,
          "the fixture GGUF opens (%u bytes)", WF_GGUF_LEN);
    zt_model_err_t err;
    zt_whisper_cfg_t cfg;
    CHECK(zt_whisper_config(&g, &cfg, &err) == 0 && cfg.n_mels == WF_NM && cfg.n_state == WF_D &&
              cfg.n_audio_ctx == WF_ACTX && cfg.n_vocab == WF_NV && cfg.n_frames == 100 &&
              cfg.n_samples == 16000 && cfg.eot == WF_EOT && cfg.n_lang == 3,
          "config: 80 mels, width 32, audio context 50 (100 frames, 16000 samples), 276 tokens");
    uint64_t ab = zt_whisper_arena_bytes(&g);
    uint8_t *arena = alloc64(ab);
    zt_whisper_t w;
    CHECK(ab && zt_whisper_load(&w, &g, arena, ab - 65, &err) == ZT_NN_ESPACE,
          "an arena that is too small is refused");
    CHECK(zt_whisper_load(&w, &g, arena, ab, &err) == 0, "loads (%llu arena bytes): %s",
          (unsigned long long) ab, err.what);
    uint64_t sb = zt_whisper_state_bytes(&w);
    uint8_t *smem = alloc64(sb);
    zt_whisper_state_t st;
    CHECK(zt_whisper_state_init(&st, &w, smem, sb) == 0, "state (%llu bytes)",
          (unsigned long long) sb);

    int16_t pcm[WF_N_PCM];
    make_pcm(pcm);
    uint64_t nmel = (uint64_t) cfg.n_frames * cfg.n_mels, nenc = (uint64_t) WF_ACTX * WF_D;
    CHECK(zt_whisper_mel(&w, &st, pcm, WF_N_PCM) == 0, "log-mel runs");
    gap_t gm = gap(st.mel, WF_MEL, nmel);
    /* Q16 rounding (7.6e-6) + log2 to 24 bits + the DFT's rounding */
    CHECK(gm.max_abs < 3e-5, "log-mel vs float64: max abs err %.2e (< 3e-5), rel RMS %.2e",
          gm.max_abs, gm.rel_rms);
    zt_fx *mel0 = malloc(nmel * 4);
    memcpy(mel0, st.mel, nmel * 4);

    CHECK(zt_whisper_encode(&w, &st) == 0, "encoder runs");
    gap_t ge = gap(st.enc, WF_ENC, nenc);
    CHECK(ge.max_abs < 4e-3 && ge.rel_rms < 1e-3,
          "encoder output vs float64: max abs err %.2e (< 4e-3), rel RMS %.2e (< 1e-3)", ge.max_abs,
          ge.rel_rms);
    zt_fx *enc0 = malloc(nenc * 4);
    memcpy(enc0, st.enc, nenc * 4);

    uint32_t nseq = sizeof WF_SEQ / sizeof WF_SEQ[0], nlog = nseq - 3;
    zt_fx *lg = malloc((uint64_t) nseq * WF_NV * 4), *lg1 = malloc((uint64_t) WF_NV * 4);
    st.n_past = 0;
    CHECK(zt_whisper_decode(&w, &st, WF_SEQ, nseq, lg, true) == 0, "decoder: %u tokens at once",
          nseq);
    gap_t gl = gap(lg + 3ull * WF_NV, WF_LOGITS, (uint64_t) nlog * WF_NV);
    CHECK(gl.max_abs < 6e-3 && gl.rel_rms < 6e-4,
          "logits (%u positions) vs float64: max abs err %.2e (< 6e-3), rel RMS %.2e (< 6e-4)",
          nlog, gl.max_abs, gl.rel_rms);
    /* one token at a time through the KV cache gives the same bits */
    st.n_past = 0;
    int ok = 1;
    for (uint32_t i = 0; i < nseq; i++) {
        zt_whisper_decode(&w, &st, &WF_SEQ[i], 1, lg1, false);
        ok &= !memcmp(lg1, lg + (uint64_t) i * WF_NV, WF_NV * 4);
    }
    CHECK(ok, "feeding the tokens one at a time gives the batch's logits bit for bit");
    CHECK(zt_whisper_decode(&w, &st, WF_SEQ, 16, lg, false) == ZT_NN_ESPACE,
          "decoding past the text context is refused");

    uint32_t lang = 99;
    CHECK(zt_whisper_detect_lang(&w, &st, &lang) == 0 && (int) lang == WF_LANG,
          "language detection: index %u (float64: %d)", lang, WF_LANG);
    int32_t ids[16];
    uint32_t n_ids = 0, n_ref = sizeof WF_GREEDY / sizeof WF_GREEDY[0];
    CHECK(zt_whisper_greedy(&w, &st, lang, false, 8, ids, 16, &n_ids, 0, 0) == 0,
          "greedy decoding runs");
    ok = 1;
    for (uint32_t i = 0; i < n_ids && i < n_ref; i++) ok &= ids[i] == WF_GREEDY[i];
    uint32_t want = WF_GREEDY[n_ref - 1] == WF_EOT ? n_ref - 1 : n_ref;
    CHECK(ok && n_ids == (want < 8 ? want : 8), "greedy transcript: %u tokens, as float64's",
          n_ids);
    int32_t ids2[16];
    uint32_t n2 = 0;
    CHECK(zt_whisper_transcribe(&w, &st, pcm, WF_N_PCM, -1, ids2, 16, &n2) == 0 && n2 == n_ids &&
              !memcmp(ids, ids2, n2 * 4) && !memcmp(st.mel, mel0, nmel * 4),
          "zt_whisper_transcribe (mel + encode + detect + greedy) gives the same");

    /* tokenizer from the same file */
    uint64_t tb = zt_tok_arena_bytes(&g);
    void *ta = alloc64(tb);
    zt_tok_t tok;
    CHECK(zt_tok_load(&tok, &g, ta, tb) == 0 && tok.pre == ZT_TOK_PRE_GPT2 &&
              tok.n_vocab == WF_NV && tok.eos == WF_EOT,
          "zt_tok_load reads the whisper tokenizer (pre-tokenizer whisper = GPT-2)");
    int32_t tid[3] = {256, 258, 72};
    uint8_t txt[32];
    uint64_t nt = 0;
    CHECK(zt_tok_decode(&tok, tid, 3, txt, sizeof txt, &nt) == 0 && nt == 7 &&
              !memcmp(txt, " t thei", 7),
          "zt_tok_decode: merged tokens and a byte token to text (\" t thei\")");

    /* front-end edge cases */
    zt_fx *mel = st.mel;
    zt_whisper_mel(&w, &st, pcm, 0);
    ok = 1;
    for (uint64_t i = 0; i < nmel; i++) ok &= mel[i] == -98304; /* (-10 + 4) / 4 */
    CHECK(ok, "silence: every value is (log10(1e-10) + 4) / 4 = -1.5");
    static int16_t big[20000];
    for (int i = 0; i < 20000; i++) big[i] = (i / 7) & 1 ? 32767 : -32768;
    CHECK(zt_whisper_mel(&w, &st, big, 20000) == 0 && st.n_sat == 0,
          "full-scale square wave, longer than the window: runs, no saturation");
    zt_fx mx = INT32_MIN, mn = INT32_MAX;
    for (uint64_t i = 0; i < nmel; i++) {
        if (mel[i] > mx) mx = mel[i];
        if (mel[i] < mn) mn = mel[i];
    }
    CHECK(mx - mn <= 2 * 65536 + 1 && mx > 65536,
          "full scale: the 8-decade floor holds (range %.4f .. %.4f)", mx / 65536.0, mn / 65536.0);

#ifdef ZT_TEST_SIMD
    const zt_kern_t *ks[8];
    uint32_t nk = zt_simd_list(ks, 8);
    zt_pool_t *pool = zt_pool_new(3);
    for (uint32_t i = 0; i < nk; i++)
        for (int th = 0; th < 2; th++) {
            zt_kern_t kt;
            zt_kern_threaded(&kt, ks[i], th ? pool : 0);
            w.kern = &kt;
            zt_whisper_mel(&w, &st, pcm, WF_N_PCM);
            int same = !memcmp(st.mel, mel0, nmel * 4);
            zt_whisper_encode(&w, &st);
            same &= !memcmp(st.enc, enc0, nenc * 4);
            uint32_t n3 = 0;
            zt_whisper_greedy(&w, &st, lang, false, 8, ids2, 16, &n3, 0, 0);
            same &= n3 == n_ids && !memcmp(ids, ids2, n3 * 4);
            CHECK(same, "kernel set %s%s: the C reference's mel, encoder and transcript bits",
                  ks[i]->name, th ? ", 3 threads" : "");
        }
    w.kern = 0;
    zt_pool_free(pool);
#endif
    free(mel0);
    free(enc0);
    free(lg);
    free(lg1);
    free(ta);
    free(smem);
    free(arena);
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
