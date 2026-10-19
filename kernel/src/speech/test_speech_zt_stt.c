/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_speech_zt_stt.c — host tests for the on-device ears on the tiny
 * Whisper fixture: ready, the silence gate, language hints and detection,
 * 30 s windows (1 s in the fixture), the no-speech gate, text that does not
 * fit, and binding into speech_session's push-to-talk. With ZXV_STT_MODEL
 * (a converted whisper GGUF) and ZXV_STT_WAV (16 kHz mono 16-bit) it also
 * transcribes real speech (ZXV_STT_LANG: a language hint), and checks
 * ZXV_STT_EXPECT is in it when set. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speech_zt_stt.h"
#include "zt_simd.h"
#include "test_whisper_fixture.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
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

static void *alloc64(uint64_t n)
{
    void *p = NULL;
    return posix_memalign(&p, 64, n ? n : 64) ? NULL : p;
}

typedef struct {
    zt_gguf_t g;
    zt_whisper_t w;
    zt_whisper_state_t st;
    zt_tok_t tok;
    int32_t ids[512];
    void *mem[3];
} ears_t;

static int load(ears_t *m, const uint8_t *file, uint64_t len)
{
    memset(m, 0, sizeof *m);
    zt_model_err_t err;
    if (zt_gguf_open(&m->g, file, len) != ZT_GGUF_OK) return -1;
    uint64_t ab = zt_whisper_arena_bytes(&m->g);
    m->mem[0] = alloc64(ab);
    if (zt_whisper_load(&m->w, &m->g, m->mem[0], ab, &err) != 0) return -2;
    uint64_t sb = zt_whisper_state_bytes(&m->w);
    m->mem[1] = alloc64(sb);
    if (zt_whisper_state_init(&m->st, &m->w, m->mem[1], sb) != 0) return -3;
    uint64_t tb = zt_tok_arena_bytes(&m->g);
    m->mem[2] = alloc64(tb);
    if (zt_tok_load(&m->tok, &m->g, m->mem[2], tb) != 0) return -4;
    return 0;
}

static void unload(ears_t *m)
{
    for (int i = 0; i < 3; i++) free(m->mem[i]);
}

static void wire(speech_zt_stt_t *e, ears_t *m)
{
    speech_zt_stt_defaults(e);
    e->w = &m->w;
    e->st = &m->st;
    e->tok = &m->tok;
    e->g = &m->g;
    e->ids = m->ids;
    e->ids_cap = sizeof m->ids / sizeof m->ids[0];
}

/* the transcript the fixture's greedy ids decode to */
static char WANT[64];

static void test_ready(ears_t *m)
{
    speech_zt_stt_t e;
    speech_zt_stt_defaults(&e);
    CHECK(e.quiet_level == SPEECH_ZT_STT_QUIET && !e.ready && !e.w, "defaults");
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG && !e.ready, "nothing loaded");
    CHECK(speech_zt_stt_ready(0) == SPEECH_ZT_STT_EARG, "NULL");
    wire(&e, m);
    e.ids_cap = 3;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG, "too few ids");
    e.ids_cap = 4;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_OK && e.ready, "ready");
    const zt_whisper_t *w = e.w;
    e.w = 0;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG && !e.ready, "no model");
    e.w = w;
    e.tok = 0;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG, "no tokenizer");
    wire(&e, m);
    e.g = 0;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG, "no language table");
    wire(&e, m);
    e.st = 0;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG, "no state");
    wire(&e, m);
    e.ids = 0;
    CHECK(speech_zt_stt_ready(&e) == SPEECH_ZT_STT_EARG, "no ids");
    CHECK(speech_zt_stt_attribution[0] && strstr(speech_zt_stt_attribution, "whisper"),
          "attribution");
}

static void test_words(ears_t *m)
{
    speech_zt_stt_t e;
    wire(&e, m);
    speech_zt_stt_ready(&e);
    static int16_t pcm[WF_N_PCM];
    make_pcm(pcm);
    char text[64], lang[8];
    /* the expected text: the fixture's greedy ids, decoded, leading space off */
    uint64_t nt = 0;
    uint32_t nref = sizeof WF_GREEDY / sizeof WF_GREEDY[0] - 1; /* without EOT */
    zt_tok_decode(&m->tok, WF_GREEDY, nref, (uint8_t *) WANT, sizeof WANT - 1, &nt);
    WANT[nt] = 0;
    uint32_t lead = 0;
    while (WANT[lead] == ' ') lead++;
    memmove(WANT, WANT + lead, strlen(WANT + lead) + 1);
    CHECK(WANT[0], "the fixture says something (%s)", WANT);

    e.max_tokens = nref;
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "", text, sizeof text, lang, sizeof lang, &e) ==
              SPEECH_ZT_STT_OK,
          "transcribes");
    /* WF_N_PCM is 12000 samples: one 1 s window in the fixture */
    CHECK(!strcmp(text, WANT), "the greedy words: '%s' want '%s'", text, WANT);
    CHECK(!strcmp(lang, "en") && !strcmp(e.lang, "en"), "detected en (%s)", lang);
    CHECK(e.utterances == 1 && e.windows == 1 && !e.silent && !e.no_speech && !e.cut,
          "counted: %u %u", e.utterances, e.windows);
    CHECK(text[0] != ' ', "no leading space");

    /* ZT3: a hint picks the language, by its base code */
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "fr-CA", text, sizeof text, lang, sizeof lang,
                                   &e) == 0 &&
              !strcmp(lang, "fr"),
          "fr-CA hint: %s", lang);
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "DE", text, sizeof text, lang, sizeof lang, &e) ==
                  0 &&
              !strcmp(lang, "de"),
          "DE hint: %s", lang);
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "xx", text, sizeof text, lang, sizeof lang, &e) ==
                  0 &&
              !strcmp(lang, "en"),
          "unknown hint: detected (%s)", lang);
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "english", text, sizeof text, lang, sizeof lang,
                                   &e) == 0 &&
              !strcmp(lang, "en"),
          "too long to be a code: detected (%s)", lang);
    /* no lang_out is fine */
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "", text, sizeof text, 0, 0, &e) == 0 &&
              !strcmp(text, WANT) && !strcmp(e.lang, "en"),
          "no lang_out");
    char l1[2];
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "", text, sizeof text, l1, sizeof l1, &e) == 0 &&
              l1[0] == 'e' && !l1[1],
          "lang_out cut to fit: %s", l1);

    /* ZT2: two windows of speech, joined by a space */
    static int16_t two[16000 + WF_N_PCM];
    memcpy(two, pcm, WF_N_PCM * 2);
    memset(two + WF_N_PCM, 0, (16000 - WF_N_PCM) * 2);
    memcpy(two + 16000, pcm, WF_N_PCM * 2);
    uint32_t w0 = e.windows;
    char t2[128];
    CHECK(speech_zt_stt_transcribe(two, 16000 + WF_N_PCM, "en", t2, sizeof t2, 0, 0, &e) == 0,
          "two windows");
    char want2[160];
    snprintf(want2, sizeof want2, "%s %s", WANT, WANT);
    CHECK(!strcmp(t2, want2) && e.windows == w0 + 2, "joined: '%s' (%u windows)", t2,
          e.windows - w0);
    /* a quiet tail window is not run */
    static int16_t tail[16000 + 8000];
    memcpy(tail, two, 16000 * 2);
    for (uint32_t i = 16000; i < 24000; i++) tail[i] = (int16_t) (i & 1 ? 20 : -20);
    w0 = e.windows;
    CHECK(speech_zt_stt_transcribe(tail, 24000, "en", t2, sizeof t2, 0, 0, &e) == 0 &&
              !strcmp(t2, WANT) && e.windows == w0 + 1,
          "quiet tail skipped: '%s' (%u windows)", t2, e.windows - w0);

    /* text that does not fit: cut, never overrun */
    char small[5];
    memset(small, 'Z', sizeof small);
    uint32_t c0 = e.cut;
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", small, sizeof small, 0, 0, &e) == 0,
          "small buffer");
    CHECK(strlen(small) <= 4 && !strncmp(small, WANT, strlen(small)) && e.cut == c0 + 1,
          "cut to fit: '%s' (cut %u)", small, e.cut - c0);
    char big2[160];
    memset(big2, 'Z', sizeof big2);
    c0 = e.cut;
    speech_zt_stt_transcribe(two, 16000 + WF_N_PCM, "en", big2, (uint32_t) strlen(WANT) + 3, 0, 0,
                             &e);
    CHECK(e.cut == c0 + 1 && strlen(big2) <= strlen(WANT) + 2 &&
              !strncmp(big2, want2, strlen(big2)),
          "second window cut: '%s'", big2);
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", small, 1, 0, 0, &e) == SPEECH_ZT_STT_EARG,
          "cap 1 refused");
}

static void test_silence(ears_t *m)
{
    speech_zt_stt_t e;
    wire(&e, m);
    speech_zt_stt_ready(&e);
    static int16_t q[16000];
    char text[32] = "junk", lang[8] = "xx";
    CHECK(speech_zt_stt_transcribe(q, 16000, "", text, sizeof text, lang, sizeof lang, &e) == 0 &&
              !text[0] && !lang[0],
          "silence: nothing, without the model");
    CHECK(e.silent == 1 && e.windows == 0, "counted silent, no window run");
    for (int i = 0; i < 16000; i++) q[i] = (int16_t) (i & 1 ? 119 : -119);
    CHECK(speech_zt_stt_transcribe(q, 16000, "", text, sizeof text, 0, 0, &e) == 0 && !text[0] &&
              e.silent == 2,
          "just under the level");
    e.quiet_level = 119;
    CHECK(speech_zt_stt_transcribe(q, 16000, "", text, sizeof text, 0, 0, &e) == 0 &&
              e.silent == 2 && e.windows == 1,
          "at the level: the model runs");
    CHECK(speech_zt_stt_level(q, 16000) == 119 && speech_zt_stt_level(0, 3) == 0 &&
              speech_zt_stt_level(q, 0) == 0,
          "level");
    int16_t ext[2] = {-32768, 32767};
    CHECK(speech_zt_stt_level(ext, 2) == 32767, "level at full scale: %u",
          speech_zt_stt_level(ext, 2));
    CHECK(speech_zt_stt_transcribe(q, 0, "", text, sizeof text, 0, 0, &e) == 0 && !text[0],
          "no samples");

    static int16_t pcm[WF_N_PCM];
    make_pcm(pcm);

    /* refusals */
    char t[8];
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "", t, sizeof t, 0, 0, 0) == SPEECH_ZT_STT_EARG,
          "NULL ctx");
    CHECK(speech_zt_stt_transcribe(0, 5, "", t, sizeof t, 0, 0, &e) == SPEECH_ZT_STT_EARG,
          "NULL pcm");
    CHECK(speech_zt_stt_transcribe(pcm, 5, "", 0, 9, 0, 0, &e) == SPEECH_ZT_STT_EARG, "NULL text");
    speech_zt_stt_t nr;
    speech_zt_stt_defaults(&nr);
    strcpy(t, "junk");
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "", t, sizeof t, 0, 0, &nr) ==
                  SPEECH_ZT_STT_EARG &&
              !t[0],
          "not ready: refused, text cleared");
}

/* ZT4 again: the step-0 logit margin between the word greedy chose and the
 * no-speech token, then no_speech_bias just past it and just short of it. */
static bool on_step_gap(void *ctx, uint32_t step, const zt_fx *logits, int32_t token)
{
    int64_t *r = ctx;
    if (step == 0) {
        r[0] = token;
        r[1] = (int64_t) logits[token] - logits[(int32_t) r[2]];
    }
    return false;
}

static void test_no_speech(ears_t *m)
{
    speech_zt_stt_t e;
    wire(&e, m);
    speech_zt_stt_ready(&e);
    static int16_t pcm[WF_N_PCM];
    make_pcm(pcm);
    int64_t r[3] = {-1, 0, m->w.cfg.no_speech};
    uint32_t got = 0;
    zt_whisper_mel(&m->w, &m->st, pcm, WF_N_PCM);
    zt_whisper_encode(&m->w, &m->st);
    zt_whisper_greedy(&m->w, &m->st, 0, false, 4, m->ids, 4, &got, on_step_gap, r);
    CHECK(r[0] == WF_GREEDY[0] && r[1] > 0 && r[1] < INT32_MAX,
          "step 0: chose %lld, %lld (Q16) above no-speech", (long long) r[0], (long long) r[1]);
    char text[64] = "junk", lang[8];
    e.no_speech_bias = (int32_t) r[1];
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", text, sizeof text, 0, 0, &e) == 0 &&
              !strcmp(text, WANT) && !e.no_speech,
          "a tie is speech ('%s')", text);
    e.no_speech_bias = (int32_t) r[1] + 1;
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", text, sizeof text, lang, sizeof lang, &e) ==
                  0 &&
              !text[0] && e.no_speech == 1,
          "no-speech outscores the word: nothing ('%s')", text);
    CHECK(!strcmp(lang, "en"), "the language is still told: %s", lang);
    /* two windows, both no-speech: nothing, and both counted */
    static int16_t two[32000];
    memcpy(two, pcm, WF_N_PCM * 2);
    memcpy(two + 16000, pcm, WF_N_PCM * 2);
    uint32_t w0 = e.windows;
    CHECK(speech_zt_stt_transcribe(two, 32000, "en", text, sizeof text, 0, 0, &e) == 0 &&
              !text[0] && e.no_speech == 3 && e.windows == w0 + 2,
          "every window no-speech: nothing (%u)", e.no_speech);
    /* no no-speech token in the model: never no-speech */
    zt_whisper_t w2 = m->w;
    w2.cfg.no_speech = -1;
    e.w = &w2;
    e.no_speech_bias = INT32_MAX;
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", text, sizeof text, 0, 0, &e) == 0 &&
              !strcmp(text, WANT) && e.no_speech == 3,
          "no no-speech token: words");
    /* the largest bias does not overflow the compare */
    e.w = &m->w;
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", text, sizeof text, 0, 0, &e) == 0 &&
              !text[0] && e.no_speech == 4,
          "INT32_MAX bias: nothing");
    e.no_speech_bias = INT32_MIN;
    CHECK(speech_zt_stt_transcribe(pcm, WF_N_PCM, "en", text, sizeof text, 0, 0, &e) == 0 &&
              !strcmp(text, WANT),
          "INT32_MIN bias: words");
}

/* speech_session: push-to-talk through the ears */
static int n_tr;
static char TR[64];
static void on_ev(const speech_event_t *ev, void *ctx)
{
    (void) ctx;
    if (ev->type == SPEECH_EV_TRANSCRIPT) {
        n_tr++;
        snprintf(TR, sizeof TR, "%s", ev->text);
    }
}

static void test_session(ears_t *m)
{
    speech_zt_stt_t e;
    wire(&e, m);
    speech_zt_stt_ready(&e);
    speech_stt_ops_t ops;
    memset(&ops, 0, sizeof ops);
    speech_zt_stt_ops(&e, &ops);
    CHECK(ops.transcribe == speech_zt_stt_transcribe && ops.ctx == &e, "ops");
    speech_zt_stt_ops(&e, 0);
    speech_config_t cfg;
    speech_config_defaults(&cfg);
    cfg.use = SPEECH_USE_DICTATION;
    cfg.on_event = on_ev;
    static int16_t cap[16000 * 2], tts[16];
    speech_session_t s;
    CHECK(speech_session_init(&s, &cfg, cap, 16000 * 2, tts, 16) == SPEECH_OK, "session");
    speech_session_bind_stt(&s, &ops);
    static int16_t pcm[WF_N_PCM];
    make_pcm(pcm);
    speech_session_ptt_down(&s);
    speech_session_feed(&s, pcm, WF_N_PCM);
    int r = speech_session_ptt_up(&s);
    CHECK(r == SPEECH_OK && n_tr == 1 && !strncmp(TR, WANT, strlen(TR)) && TR[0],
          "push-to-talk heard: %d '%s'", r, TR);
    /* silence: EMPTY, nothing acted on */
    static int16_t q[8000];
    speech_session_ptt_down(&s);
    speech_session_feed(&s, q, 8000);
    r = speech_session_ptt_up(&s);
    CHECK(r == SPEECH_ERR_EMPTY && n_tr == 1, "silence is not words: %d", r);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* 16 kHz mono 16-bit PCM: the data chunk's samples, or NULL */
static int16_t *read_wav(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t) len + 1);
    size_t got = fread(b, 1, (size_t) len, f);
    fclose(f);
    int16_t *p = 0;
    int fmt_ok = 0;
    if (got >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4))
        for (size_t o = 12; o + 8 <= got && !p;) {
            uint32_t cl = rd32(b + o + 4);
            const uint8_t *c = b + o + 8;
            if (!memcmp(b + o, "fmt ", 4) && cl >= 16 && o + 8 + cl <= got)
                fmt_ok = c[0] == 1 && c[2] == 1 && rd32(c + 4) == 16000 && c[14] == 16;
            else if (!memcmp(b + o, "data", 4) && fmt_ok) {
                size_t avail = got - (o + 8) < cl ? got - (o + 8) : cl;
                *n = (uint32_t) (avail / 2);
                p = malloc(avail + 2);
                memcpy(p, c, avail);
            }
            o += 8 + (size_t) cl + (cl & 1);
        }
    free(b);
    return p;
}

static void test_real(void)
{
    const char *mp = getenv("ZXV_STT_MODEL"), *wp = getenv("ZXV_STT_WAV");
    if (!mp || !wp) return;
    FILE *f = fopen(mp, "rb");
    CHECK(f, "open %s", mp);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = alloc64((uint64_t) len);
    size_t got = fread(file, 1, (size_t) len, f);
    fclose(f);
    static ears_t m;
    CHECK(got == (size_t) len && load(&m, file, (uint64_t) len) == 0, "real model loads");
    uint32_t n = 0;
    int16_t *pcm = read_wav(wp, &n);
    CHECK(pcm && n, "16 kHz mono WAV: %s", wp);
    /* the machine's best kernel set, on 4 threads, as a host would bind it */
    zt_pool_t *pool = zt_pool_new(4);
    zt_kern_t kern;
    zt_kern_threaded(&kern, zt_simd_best(), pool);
    m.w.kern = &kern;
    if (pcm && n) {
        speech_zt_stt_t e;
        wire(&e, &m);
        speech_zt_stt_ready(&e);
        char text[512], lang[8];
        const char *hint = getenv("ZXV_STT_LANG");
        CHECK(speech_zt_stt_transcribe(pcm, n, hint ? hint : "", text, sizeof text, lang,
                                       sizeof lang, &e) == 0,
              "real speech transcribes");
        printf("  heard (%s): %s\n", lang, text);
        const char *want = getenv("ZXV_STT_EXPECT");
        CHECK(!want || strstr(text, want), "contains '%s'", want ? want : "");
    }
    free(pcm);
    zt_pool_free(pool);
    unload(&m);
    free(file);
}

int main(void)
{
    static ears_t m;
    CHECK(load(&m, (const uint8_t *) WF_GGUF, WF_GGUF_LEN) == 0, "the fixture loads");
    test_ready(&m);
    test_words(&m);
    test_silence(&m);
    test_no_speech(&m);
    test_session(&m);
    unload(&m);
    test_real();
    printf("%s test_speech_zt_stt: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
