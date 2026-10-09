/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_voice.c — the Chiglet finds its voice; we prove the spelling is real
 *                and the singing is never faked.
 *
 * Host build:
 *   cc -std=c11 -Wall -Werror -Wextra -O1 -fsanitize=address,undefined \
 *      -DTEST_HOST -Isrc/voice -Isrc/chiglet -Isrc/surplus \
 *      src/voice/test_voice.c src/voice/voice.c src/chiglet/chiglet.c \
 *      src/surplus/surplus.c -o /tmp/test_voice -lm && /tmp/test_voice
 */
#include <stdio.h>
#include <string.h>
#include "voice.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", (m));                                                            \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", (m));                                                            \
    } while (0)

/* ================= mock TTS engine =================
 * Deterministic stub: emits ONE sample per phoneme, value = phoneme*100. This
 * is not audio; it is a known pattern that PROVES the on-device text->phoneme
 * run reached the engine intact and in order. */
static int mock_synth(const uint8_t *ph, uint32_t n, int16_t *pcm, uint32_t cap, uint32_t *out_len,
                      void *ctx)
{
    (void) ctx;
    uint32_t w = 0;
    for (uint32_t i = 0; i < n && w < cap; i++) pcm[w++] = (int16_t) (ph[i] * 100);
    *out_len = w;
    return 0;
}
/* A synth that always refuses, to exercise VOICE_ERR_ENGINE. */
static int failing_synth(const uint8_t *ph, uint32_t n, int16_t *pcm, uint32_t cap,
                         uint32_t *out_len, void *ctx)
{
    (void) ph;
    (void) n;
    (void) pcm;
    (void) cap;
    (void) out_len;
    (void) ctx;
    return -1;
}
/* A HOSTILE synth that lies: reports got = cap + 1000 without writing that far.
 * voice_speak must clamp its reported length to cap (never trust the boundary). */
static int lying_synth(const uint8_t *ph, uint32_t n, int16_t *pcm, uint32_t cap, uint32_t *out_len,
                       void *ctx)
{
    (void) ph;
    (void) n;
    (void) pcm;
    (void) ctx;
    *out_len = cap + 1000u; /* a lie: claims more than the buffer holds */
    return 0;
}

/* ================= mock STT engine =================
 * Returns a fixed phoneme run {PH_K, PH_AE, PH_T}, ignoring the PCM entirely.
 * That is exactly what a stub should do: it is not recognizing anything, it is
 * standing in for the model so the phoneme->text rendering can be tested. */
static int mock_recognize(const int16_t *pcm, uint32_t n, uint8_t *ph, uint32_t cap,
                          uint32_t *out_len, void *ctx)
{
    (void) pcm;
    (void) n;
    (void) ctx;
    static const uint8_t fixed[3] = {PH_K, PH_AE, PH_T};
    uint32_t w = 0;
    for (uint32_t i = 0; i < 3 && w < cap; i++) ph[w++] = fixed[i];
    *out_len = w;
    return 0;
}

/* Build a Chiglet whose two orthogonal evidence vectors decide label 0 ("cat"),
 * with thresholds low enough that the verdict is DECIDED. */
static void build_chiglet(chiglet_t *c)
{
    chg_model_t m;
    memset(&m, 0, sizeof(m));
    m.K = 2;
    m.D = 4;
    m.L = 2;
    m.epoch = 1;
    /* proto[0] = cat direction, proto[1] = dog direction */
    m.proto[0][0] = SR_FROM_FLOAT(1.0);
    m.proto[1][1] = SR_FROM_FLOAT(1.0);
    strcpy(m.label_name[0], "cat");
    strcpy(m.label_name[1], "dog");
    m.R_min = SR_FROM_FLOAT(1.5);
    m.margin_min = SR_FROM_FLOAT(0.1);
    m.loaded = true;

    chg_init(c, CHG_CAP_INFER);
    chg_status_t st = chg_load_model(c, &m);
    CHECK(st == CHG_OK, "chiglet model loads for voice_chiglet_say");
}

int main(void)
{
    printf("=== ZXV voice — spell the sounds, the engine sings ===\n");

    /* ---------- ANCHOR 1: deterministic, known-answer g2p ---------- */
    {
        uint8_t a[16], b[16];
        uint32_t na = voice_text_to_phonemes("cat", a, sizeof a);
        uint32_t nb = voice_text_to_phonemes("cat", b, sizeof b);
        CHECK(na == 3, "'cat' decomposes to exactly 3 phonemes");
        CHECK(a[0] == PH_K && a[1] == PH_AE && a[2] == PH_T,
              "'cat' == {PH_K, PH_AE, PH_T} (known answer)");
        CHECK(na == nb && memcmp(a, b, na) == 0,
              "voice_text_to_phonemes is deterministic (same in, same out)");

        /* case-insensitivity + a real digraph rule */
        uint8_t s[16];
        uint32_t ns = voice_text_to_phonemes("SHIP", s, sizeof s);
        CHECK(ns == 3 && s[0] == PH_SH && s[1] == PH_IH && s[2] == PH_P,
              "'SHIP' lowercases and applies the 'sh' digraph -> {SH,IH,P}");

        /* whitespace collapses to a single boundary, never leading/doubled */
        uint8_t w[16];
        uint32_t nw = voice_text_to_phonemes("  a  b ", w, sizeof w);
        CHECK(nw == 3 && w[0] == PH_AE && w[1] == PH_SIL && w[2] == PH_B,
              "'  a  b ' -> {AE, SIL, B}: leading/dup spaces collapsed");
    }

    /* ---------- ANCHOR 2: speak with NO engine => NO_ENGINE, NO pcm ---------- */
    {
        voice_t v;
        voice_init(&v);
        int16_t pcm[8];
        for (int i = 0; i < 8; i++) pcm[i] = 0x5A5A; /* sentinel */
        uint32_t out_len = 12345;
        voice_result_t r = voice_speak(&v, "cat", pcm, 8, &out_len);
        CHECK(r == VOICE_ERR_NO_ENGINE, "voice_speak unbound => VOICE_ERR_NO_ENGINE");
        CHECK(out_len == 0, "unbound voice_speak sets out_len = 0");
        int untouched = 1;
        for (int i = 0; i < 8; i++)
            if (pcm[i] != 0x5A5A) untouched = 0;
        CHECK(untouched, "unbound voice_speak writes NO pcm (never fabricated audio)");
        CHECK(!voice_has_tts(&v), "voice_has_tts false before binding");
    }

    /* ---------- ANCHOR 3: mock TTS bound => engine gets the phoneme run ---------- */
    {
        voice_t v;
        voice_init(&v);
        voice_tts_ops_t ops = {mock_synth, 0};
        voice_set_tts(&v, &ops);
        CHECK(voice_has_tts(&v), "voice_has_tts true after binding");

        int16_t pcm[8] = {0};
        uint32_t out_len = 0;
        voice_result_t r = voice_speak(&v, "cat", pcm, 8, &out_len);
        CHECK(r == VOICE_OK, "voice_speak with mock TTS => VOICE_OK");
        CHECK(out_len == 3, "mock TTS emitted one sample per phoneme (3)");
        CHECK(pcm[0] == PH_K * 100 && pcm[1] == PH_AE * 100 && pcm[2] == PH_T * 100,
              "PCM pattern proves {K,AE,T} reached the engine in order");
        CHECK(v.spoken == 1, "spoken counter records real work only");

        /* a refusing engine surfaces as a typed error, not fake success */
        voice_tts_ops_t bad = {failing_synth, 0};
        voice_set_tts(&v, &bad);
        r = voice_speak(&v, "cat", pcm, 8, &out_len);
        CHECK(r == VOICE_ERR_ENGINE && out_len == 0,
              "a failing TTS => VOICE_ERR_ENGINE, out_len 0");

        /* a HOSTILE engine that reports got > cap must be CLAMPED to cap — the
         * caller must never be told there are more samples than the buffer holds. */
        voice_tts_ops_t liar = {lying_synth, 0};
        voice_set_tts(&v, &liar);
        out_len = 0;
        r = voice_speak(&v, "cat", pcm, 8, &out_len);
        CHECK(r == VOICE_OK && out_len == 8,
              "a synth lying got>cap is clamped to cap (no out-of-bounds length)");

        /* unbinding returns to fail-closed */
        voice_set_tts(&v, 0);
        CHECK(!voice_has_tts(&v), "voice_set_tts(NULL) unbinds");
        CHECK(voice_speak(&v, "cat", pcm, 8, &out_len) == VOICE_ERR_NO_ENGINE,
              "unbound again => VOICE_ERR_NO_ENGINE");
    }

    /* ---------- ANCHOR 4: listen — unbound closed, mock maps deterministically ---------- */
    {
        voice_t v;
        voice_init(&v);
        int16_t pcm[4] = {1, 2, 3, 4};
        char text[16];

        voice_result_t r = voice_listen(&v, pcm, 4, text, sizeof text);
        CHECK(r == VOICE_ERR_NO_ENGINE, "voice_listen unbound => VOICE_ERR_NO_ENGINE");
        CHECK(text[0] == '\0', "unbound voice_listen writes empty string (no fake text)");

        voice_stt_ops_t ops = {mock_recognize, 0};
        voice_set_stt(&v, &ops);
        r = voice_listen(&v, pcm, 4, text, sizeof text);
        CHECK(r == VOICE_OK, "voice_listen with mock STT => VOICE_OK");
        /* {PH_K,PH_AE,PH_T} render to canonical (lossy) chars "kat" */
        CHECK(strcmp(text, "kat") == 0, "phonemes {K,AE,T} map back to deterministic text 'kat'");
        CHECK(v.heard == 1, "heard counter records real work only");
    }

    /* ---------- ANCHOR 5: length checks / truncation, no overrun ---------- */
    {
        /* tiny cap on g2p: writes exactly cap tokens, no more */
        uint8_t one[1];
        uint32_t n = voice_text_to_phonemes("cat", one, 1);
        CHECK(n == 1 && one[0] == PH_K, "g2p honours cap=1: writes 1 token, no overrun");

        uint8_t two[2];
        n = voice_text_to_phonemes("cat", two, 2);
        CHECK(n == 2 && two[0] == PH_K && two[1] == PH_AE,
              "g2p honours cap=2: writes 2 tokens exactly");

        uint32_t zero_n = voice_text_to_phonemes("cat", one, 0);
        CHECK(zero_n == 0, "g2p with cap=0 writes nothing");

        /* truncation on listen: 'kat' needs 4 bytes incl NUL; give it 3 */
        voice_t v;
        voice_init(&v);
        voice_stt_ops_t ops = {mock_recognize, 0};
        voice_set_stt(&v, &ops);
        int16_t pcm[2] = {0, 0};
        char small[3];
        voice_result_t r = voice_listen(&v, pcm, 2, small, sizeof small);
        CHECK(r == VOICE_ERR_TRUNC, "voice_listen into a too-small buffer => VOICE_ERR_TRUNC");
        CHECK(strlen(small) < sizeof small && small[sizeof small - 1] == '\0',
              "truncated text stays NUL-terminated, no overrun");
    }

    /* ---------- ANCHOR 6: the Chiglet loop helper ---------- */
    {
        chiglet_t c;
        build_chiglet(&c);
        voice_t v;
        voice_init(&v);
        voice_tts_ops_t ops = {mock_synth, 0};
        voice_set_tts(&v, &ops);

        /* two orthogonal evidence vectors -> DECIDED label 0 ("cat") */
        surplus_real_t ev[2][CHG_DIM];
        memset(ev, 0, sizeof(ev));
        ev[0][0] = SR_FROM_FLOAT(1.0); /* along the cat prototype   */
        ev[1][2] = SR_FROM_FLOAT(1.0); /* orthogonal, distinct dir  */

        int16_t pcm[8] = {0};
        uint32_t out_len = 0;
        voice_result_t r = voice_chiglet_say(&v, &c, ev, 2, pcm, 8, &out_len);
        CHECK(r == VOICE_OK, "voice_chiglet_say speaks a DECIDED reply");
        CHECK(out_len == 3 && pcm[0] == PH_K * 100 && pcm[1] == PH_AE * 100 && pcm[2] == PH_T * 100,
              "Chiglet's decided label 'cat' was spelled and sung as {K,AE,T}");

        /* collinear evidence -> R collapses -> UNCERTAIN -> honest silence */
        surplus_real_t dup[2][CHG_DIM];
        memset(dup, 0, sizeof(dup));
        dup[0][0] = SR_FROM_FLOAT(1.0);
        dup[1][0] = SR_FROM_FLOAT(1.0); /* identical direction */
        int16_t pcm2[8];
        for (int i = 0; i < 8; i++) pcm2[i] = 0x7777;
        out_len = 999;
        r = voice_chiglet_say(&v, &c, dup, 2, pcm2, 8, &out_len);
        CHECK(r == VOICE_ERR_NO_REPLY,
              "UNCERTAIN Chiglet => VOICE_ERR_NO_REPLY (never a fabricated sentence)");
        int quiet = 1;
        for (int i = 0; i < 8; i++)
            if (pcm2[i] != 0x7777) quiet = 0;
        CHECK(quiet && out_len == 0, "no reply => no PCM written");

        /* an unloaded Chiglet also has nothing to say */
        chiglet_t empty;
        chg_init(&empty, CHG_CAP_INFER);
        r = voice_chiglet_say(&v, &empty, ev, 2, pcm, 8, &out_len);
        CHECK(r == VOICE_ERR_NO_REPLY, "model-less Chiglet => VOICE_ERR_NO_REPLY");
    }

    /* ---------- argument guards ---------- */
    {
        voice_t v;
        voice_init(&v);
        int16_t pcm[4];
        char t[4];
        CHECK(voice_speak(0, "x", pcm, 4, 0) == VOICE_ERR_ARG, "voice_speak NULL v => ARG");
        CHECK(voice_speak(&v, 0, pcm, 4, 0) == VOICE_ERR_ARG, "voice_speak NULL text => ARG");
        CHECK(voice_listen(&v, 0, 0, t, 4) == VOICE_ERR_ARG, "voice_listen NULL pcm => ARG");
        CHECK(voice_text_to_phonemes(0, (uint8_t *) pcm, 4) == 0, "g2p NULL text => 0");
    }

    printf("\n=== %d checks, %d failure(s) ===\n", checks, failures);
    return failures ? 1 : 0;
}
