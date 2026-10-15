/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_speech_zt_tts.c — host tests for the on-device voice: readiness, the
 * languages it speaks, the prompt it builds, codes to 16 kHz PCM through the
 * tiny WavTokenizer fixture (bit for bit against the parts run by hand), the
 * zeroed lead dropped, the same sentence giving the same audio, every
 * refusal, cutting at the output buffer, and speaking through
 * speech_session_say. The language model step is replaced by a codes
 * function: there is no 500M model in the test. Set ZXV_TTS_LM and
 * ZXV_TTS_DEC to the pinned files (bench/tensor/fetch_pinned.py outetts) to
 * also run one real sentence through the whole voice. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speech_zt_tts.h"
#include "test_tts_fixture.h"
#include "zt_simd.h"

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

#define MAXG 256u
#define NREP 30u /* the fixture's 7 codes, 30 times: 210 codes, 2.8 s */

static zt_tts_dec_t DEC;
static void *DWORK;
static uint64_t DWB;
static int32_t CODES[MAXG];
static uint32_t NCODES;

/* the fake language model */
static int calls, fail_codes, zero_codes, bad_code;
static char LAST_PROMPT[8192];
static uint64_t LAST_PLEN;

static int32_t codes_fn(void *ctx, const uint8_t *prompt, uint64_t plen, int32_t *codes,
                        uint32_t cap, uint32_t *n)
{
    (void) ctx;
    calls++;
    LAST_PLEN = plen;
    uint64_t k = plen < sizeof LAST_PROMPT - 1 ? plen : sizeof LAST_PROMPT - 1;
    memcpy(LAST_PROMPT, prompt, k);
    LAST_PROMPT[k] = 0;
    if (fail_codes) return -7;
    uint32_t m = zero_codes ? 0 : NCODES < cap ? NCODES : cap;
    for (uint32_t i = 0; i < m; i++) codes[i] = CODES[i];
    if (bad_code && m) codes[0] = 9999;
    *n = m;
    return 0;
}

static uint8_t PROMPT[16384];
static int32_t IDS[MAXG];
static int16_t PCM24[MAXG * ZT_TTS_HOP];

static void setup(speech_zt_tts_t *e)
{
    speech_zt_tts_defaults(e);
    e->dec = &DEC;
    e->prompt = PROMPT;
    e->prompt_cap = sizeof PROMPT;
    e->ids = IDS;
    e->max_gen = MAXG;
    e->dec_work = DWORK;
    e->dec_work_bytes = DWB;
    e->pcm24 = PCM24;
    e->pcm24_cap = MAXG * ZT_TTS_HOP;
    e->codes_fn = codes_fn;
    calls = fail_codes = zero_codes = bad_code = 0;
}

static int load_decoder(void)
{
    static zt_gguf_t g;
    zt_model_err_t err;
    if (zt_gguf_open(&g, TF_GGUF, sizeof TF_GGUF)) return -1;
    uint64_t ab = zt_tts_arena_bytes(&g);
    void *arena = malloc(ab);
    if (!arena || zt_tts_load(&DEC, &g, arena, ab, &err)) return -1;
    DWB = zt_tts_work_bytes(&DEC, MAXG);
    DWORK = malloc(DWB);
    for (uint32_t r = 0; r < NREP; r++)
        for (uint32_t i = 0; i < TF_NCODES; i++) CODES[NCODES++] = TF_CODES[i];
    return DWORK ? 0 : -1;
}

/* the same steps by hand */
static uint32_t by_hand(int16_t *out, uint32_t cap)
{
    static int16_t p24[MAXG * ZT_TTS_HOP];
    if (zt_tts_decode(&DEC, CODES, NCODES, p24, ZT_TTS_SKIP, DWORK, DWB, 0)) return 0;
    for (uint32_t i = 0; i < ZT_TTS_SKIP; i++)
        if (p24[i]) return 0; /* the lead really is zero */
    static speech_resampler_t rs;
    speech_resampler_init(&rs, 24000);
    uint32_t n = 0, used = 0, total = NCODES * ZT_TTS_HOP;
    n += speech_resample(&rs, p24 + ZT_TTS_SKIP, total - ZT_TTS_SKIP, &used, out, cap);
    static int16_t z[4096];
    uint32_t lat = speech_resampler_latency(&rs);
    n += speech_resample(&rs, z, lat, &used, out + n, cap - n);
    return n;
}

static void test_ready_and_langs(void)
{
    speech_zt_tts_t e;
    speech_zt_tts_defaults(&e);
    CHECK(e.seed == 42 && e.top_k == 4 && e.temperature == 65536, "defaults");
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_EARG && !e.ready, "nothing bound");
    CHECK(speech_zt_tts_ready(0) == SPEECH_ZT_EARG, "NULL");
    setup(&e);
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_OK && e.ready, "codes_fn + decoder: ready");
    e.pcm24_cap = ZT_TTS_HOP - 1;
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_EARG, "pcm24 too small");
    setup(&e);
    e.codes_fn = 0;
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_EARG, "no language model, no codes_fn");
    setup(&e);
    e.dec = 0;
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_EARG, "no decoder");
    setup(&e);
    e.max_gen = 0;
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_EARG, "no tokens");
    setup(&e);
    e.prompt = 0;
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_EARG, "no prompt buffer");

    const char *yes[] = {"", "en", "EN", "en-GB", "en_US", "En-au", 0};
    const char *no[] = {"e", "eng", "fr", "ja", "enx", "ne", "de-en", 0};
    for (int i = 0; yes[i]; i++) CHECK(speech_zt_tts_has_lang(yes[i]), "speaks %s", yes[i]);
    for (int i = 0; no[i]; i++) CHECK(!speech_zt_tts_has_lang(no[i]), "not %s", no[i]);
    CHECK(speech_zt_tts_has_lang(0), "NULL is the default (English)");

    speech_zt_need_t need;
    speech_zt_tts_need(0, 0, &DEC, 240, MAXG, &need);
    CHECK(need.gen_work_bytes == 0 && need.dec_work_bytes == DWB, "need: decoder work");
    CHECK(need.pcm24_cap == MAXG * ZT_TTS_HOP, "need: pcm");
    uint64_t base = zt_tts_prompt((const uint8_t *) "", 0, 0, 0);
    CHECK(need.prompt_cap > base + 240 * 9, "need: prompt room for 240 bytes");
    speech_zt_tts_need(0, 0, &DEC, 240, MAXG, 0); /* NULL need: no crash */
    CHECK(strstr(speech_zt_tts_attribution, "OuteTTS-0.3-500M by OuteAI, CC-BY-SA-4.0") != 0,
          "attribution");
    CHECK(strstr(speech_zt_tts_attribution, "1B") == 0, "never the 1B model");
}

static int16_t OUT[60000], REF[60000];

static void test_say(void)
{
    speech_zt_tts_t e;
    setup(&e);
    speech_zt_tts_ready(&e);
    uint32_t n = 0;
    CHECK(speech_zt_tts_say(&e, "Hello there, 2 foxes.", "en", OUT, 60000, &n) == SPEECH_ZT_OK,
          "says");
    CHECK(calls == 1, "codes asked once");
    static uint8_t want[16384];
    const char *t = "Hello there, 2 foxes.";
    uint64_t wl = zt_tts_prompt((const uint8_t *) t, strlen(t), want, sizeof want);
    CHECK(LAST_PLEN == wl && !memcmp(LAST_PROMPT, want, wl < 8191 ? wl : 8191),
          "the OuteTTS prompt");
    CHECK(strstr(LAST_PROMPT, "hello<|space|>there<|space|>two<|space|>foxes<|text_end|>") != 0,
          "normalised in the prompt");
    uint32_t r = by_hand(REF, 60000);
    CHECK(r > 0 && n == r, "length as by hand: %u vs %u", n, r);
    CHECK(!memcmp(OUT, REF, (size_t) n * 2), "bit for bit as by hand");
    uint32_t total = NCODES * ZT_TTS_HOP;
    uint32_t expect = (total - ZT_TTS_SKIP) * 2 / 3;
    CHECK(n >= expect - 2 && n <= expect + 64, "about 2/3 of the kept 24 kHz: %u ~ %u", n, expect);
    CHECK(e.last_codes == NCODES && e.last_samples == n && e.utterances == 1, "counters");
    int nonzero = 0;
    for (uint32_t i = 0; i < 100 && i < n; i++) nonzero += OUT[i] != 0;
    CHECK(nonzero > 50, "starts with sound, not the zeroed lead: %d", nonzero);

    /* ZS4: the same sentence, the same audio */
    static int16_t again[60000];
    uint32_t n2 = 0;
    speech_zt_tts_say(&e, "Hello there, 2 foxes.", "en", again, 60000, &n2);
    CHECK(n2 == n && !memcmp(again, OUT, (size_t) n * 2), "repeatable");

    /* ZS3: cut at the buffer */
    uint32_t nc = 0;
    CHECK(speech_zt_tts_say(&e, "Hi.", "", OUT, 1000, &nc) == SPEECH_ZT_OK && nc == 1000,
          "cut to 1000: %u", nc);
    CHECK(e.cut == 1 && !memcmp(OUT, REF, 2000), "counted, and the start is right");
    /* fewer codes than the lead: silence only, no crash */
    uint32_t keep = NCODES;
    NCODES = 7;
    uint32_t ns = 0;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 60000, &ns) == SPEECH_ZT_OK, "short");
    CHECK(ns < 64, "only the resampler's tail: %u", ns);
    NCODES = keep;
    /* codes past max_gen and pcm24 are clipped to fit */
    e.pcm24_cap = 100 * ZT_TTS_HOP;
    uint32_t nk = 0;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 60000, &nk) == SPEECH_ZT_OK, "pcm24 bound");
    CHECK(e.last_codes == 100, "codes cut to pcm24: %u", e.last_codes);
}

static void test_refusals(void)
{
    speech_zt_tts_t e;
    setup(&e);
    uint32_t n = 77;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 100, &n) == SPEECH_ZT_EARG && n == 0,
          "not ready");
    speech_zt_tts_ready(&e);
    CHECK(speech_zt_tts_say(&e, "Bonjour.", "fr", OUT, 100, &n) == SPEECH_ZT_ENOLANG, "French");
    CHECK(calls == 0 && e.refused == 1, "refused before any work");
    CHECK(speech_zt_tts_say(&e, "!!! ... ???", "en", OUT, 100, &n) == SPEECH_ZT_ENOTEXT,
          "no letters");
    CHECK(speech_zt_tts_say(&e, "", "en", OUT, 100, &n) == SPEECH_ZT_ENOTEXT, "empty");
    CHECK(speech_zt_tts_say(&e, "\xE3\x81\x82", "en", OUT, 100, &n) == SPEECH_ZT_ENOTEXT,
          "kana under en: nothing it can say");
    e.prompt_cap = 50;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 100, &n) == SPEECH_ZT_ESPACE, "prompt too long");
    e.prompt_cap = sizeof PROMPT;
    fail_codes = 1;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 100, &n) == SPEECH_ZT_EMODEL, "codes failed");
    fail_codes = 0;
    zero_codes = 1;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 100, &n) == SPEECH_ZT_ENOCODES, "no codes");
    zero_codes = 0;
    bad_code = 1;
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 100, &n) == SPEECH_ZT_EMODEL, "bad code");
    bad_code = 0;
    CHECK(e.failed == 3 && e.refused == 5, "counted: %u failed, %u refused", e.failed, e.refused);
    CHECK(speech_zt_tts_say(&e, 0, "en", OUT, 100, &n) == SPEECH_ZT_EARG, "NULL text");
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", 0, 100, &n) == SPEECH_ZT_EARG, "NULL pcm");
    CHECK(speech_zt_tts_say(&e, "Hi.", "en", OUT, 100, 0) == SPEECH_ZT_EARG, "NULL count");
    CHECK(speech_zt_tts_say(0, "Hi.", "en", OUT, 100, &n) == SPEECH_ZT_EARG, "NULL engine");
}

/* ---- ZS6: our own words ------------------------------------------------ */

static void test_respell(void)
{
    char out[256];
    CHECK(speech_zt_respell(0, "Hello, I am your Chiglet.", out, sizeof out) > 0 &&
              !strcmp(out, "Hello, I am your chig lett."),
          "Chiglet: %s", out);
    speech_zt_respell(0, "ZXV runs AI on the GPU; V-Bills too. Chiglet's day.", out, sizeof out);
    CHECK(
        !strcmp(out, "zee ex vee runs ay eye on the gee pee you; vee bills too. chig lett's day."),
        "built-ins: %s", out);
    speech_zt_respell(0, "chiglety unchiglet aid said", out, sizeof out);
    CHECK(!strcmp(out, "chiglety unchiglet aid said"), "whole words only: %s", out);
    const speech_zt_word_t mine[] = {{"chiglet", "chiggy"}, {"fox", "vulpine"}, {0, 0}};
    speech_zt_respell(mine, "The Chiglet and the FOX.", out, sizeof out);
    CHECK(!strcmp(out, "The chiggy and the vulpine."), "caller's words first: %s", out);
    CHECK(speech_zt_respell(0, "\xE3\x81\x82 AI", out, sizeof out) > 0 &&
              !strcmp(out, "\xE3\x81\x82 ay eye"),
          "other bytes pass through");
    CHECK(speech_zt_respell(0, "AI", out, 6) == -1, "does not fit");
    CHECK(speech_zt_respell(0, "AI", out, 7) == 6, "just fits");
    CHECK(speech_zt_respell(0, "", out, 1) == 0 && out[0] == 0, "empty");
    CHECK(speech_zt_respell(0, 0, out, 8) == -1 && speech_zt_respell(0, "a", 0, 8) == -1 &&
              speech_zt_respell(0, "a", out, 0) == -1,
          "bad args");
    int ok = 1;
    for (const speech_zt_word_t *w = speech_zt_lexicon; w->word; w++)
        for (const char *c = w->word; *c; c++)
            if (*c >= 'A' && *c <= 'Z') ok = 0;
    CHECK(ok, "lexicon words are lower case");

    /* it reaches the prompt */
    speech_zt_tts_t e;
    setup(&e);
    speech_zt_tts_ready(&e);
    uint32_t n;
    speech_zt_tts_say(&e, "I am your Chiglet.", "en", OUT, 60000, &n);
    CHECK(strstr(LAST_PROMPT, "your<|space|>chig<|space|>lett<|text_end|>") != 0,
          "respelt in the prompt");
    e.lexicon = mine;
    speech_zt_tts_say(&e, "I am your Chiglet.", "en", OUT, 60000, &n);
    CHECK(strstr(LAST_PROMPT, "your<|space|>chiggy<|text_end|>") != 0, "caller lexicon used");
    /* too long to respell: said as it is */
    static char big[SPEECH_ZT_SPELL + 64];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    big[0] = 'A';
    big[1] = 'I';
    big[2] = ' ';
    speech_zt_tts_say(&e, big, "en", OUT, 60000, &n);
    CHECK(calls > 0 && strstr(LAST_PROMPT, "ay<|space|>eye") == 0, "long text not respelt");
}

/* ---- through speech_session_say ------------------------------------------- */

static uint32_t played;
static int16_t PLAYED[60000];
static int audio_out(const int16_t *pcm, uint32_t n, void *ctx)
{
    (void) ctx;
    for (uint32_t i = 0; i < n && played < 60000; i++) PLAYED[played++] = pcm[i];
    return 0;
}

static void test_session(void)
{
    speech_zt_tts_t e;
    setup(&e);
    speech_zt_tts_ready(&e);
    speech_tts_ops_t ops;
    speech_zt_tts_ops(&e, &ops);
    CHECK(ops.synthesize == speech_zt_tts_synthesize && ops.ctx == &e, "ops");
    speech_zt_tts_ops(&e, 0);
    static int16_t cap[16000], tts[60000];
    speech_config_t cfg;
    speech_config_defaults(&cfg);
    cfg.audio_out = audio_out;
    speech_session_t s;
    CHECK(speech_session_init(&s, &cfg, cap, 16000, tts, 60000) == SPEECH_OK, "session");
    speech_session_bind_tts(&s, &ops);
    played = 0;
    CHECK(speech_session_say(&s, "Hello there, 2 foxes.", "en") == SPEECH_OK, "session says");
    uint32_t r = by_hand(REF, 60000);
    CHECK(played == r && !memcmp(PLAYED, REF, (size_t) r * 2), "the speaker got the voice: %u",
          played);
    speech_session_playback_done(&s);
    played = 0;
    CHECK(speech_session_say(&s, "Bonjour.", "fr") != SPEECH_OK && played == 0,
          "French refused, nothing played");
}

/* ---- one real sentence, when the pinned files are given ----------------- */

static uint8_t *slurp(const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t) n);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = 0;
    }
    fclose(f);
    *size = (uint64_t) n;
    return b;
}

static void test_real(void)
{
    const char *lmp = getenv("ZXV_TTS_LM"), *decp = getenv("ZXV_TTS_DEC");
    if (!lmp || !decp) {
        printf("  (real voice skipped: set ZXV_TTS_LM and ZXV_TTS_DEC)\n");
        return;
    }
    uint64_t ls = 0, ds = 0;
    uint8_t *lf = slurp(lmp, &ls), *df = slurp(decp, &ds);
    CHECK(lf && df, "pinned files read");
    if (!lf || !df) return;
    static zt_gguf_t lg, dg;
    zt_model_err_t err;
    CHECK(zt_gguf_open(&lg, lf, ls) == 0 && zt_gguf_open(&dg, df, ds) == 0, "gguf");
    static zt_model_t m;
    uint64_t mb = zt_model_arena_bytes(&lg);
    CHECK(zt_model_load(&m, &lg, malloc(mb), mb, &err) == 0, "LM loads");
    static zt_kern_t kern;
    zt_kern_threaded(&kern, zt_simd_best(), 0);
    m.kern = &kern;
    static zt_tok_t tok;
    uint64_t tb = zt_tok_arena_bytes(&lg);
    CHECK(zt_tok_load(&tok, &lg, malloc(tb), tb) == 0, "tokenizer");
    static zt_tts_dec_t dec;
    uint64_t db = zt_tts_arena_bytes(&dg);
    CHECK(zt_tts_load(&dec, &dg, malloc(db), db, &err) == 0, "decoder loads");
    dec.kern = &kern;
    const uint32_t max_gen = 1024;
    speech_zt_need_t need;
    speech_zt_tts_need(&m, &tok, &dec, 240, max_gen, &need);
    uint32_t n_ctx = need.prompt_cap + max_gen;
    if (n_ctx > ZT_MODEL_MAX_CTX) n_ctx = ZT_MODEL_MAX_CTX;
    uint64_t sb = zt_model_state_bytes(&m, n_ctx, 64, ZT_KV_Q16);
    static zt_model_state_t st;
    CHECK(zt_model_state_init(&st, &m, n_ctx, 64, ZT_KV_Q16, malloc(sb), sb) == 0, "state");
    speech_zt_tts_t e;
    speech_zt_tts_defaults(&e);
    e.lm = &m;
    e.st = &st;
    e.tok = &tok;
    e.dec = &dec;
    e.prompt = malloc(need.prompt_cap);
    e.prompt_cap = need.prompt_cap;
    e.ids = malloc(max_gen * 4);
    e.max_gen = max_gen;
    e.gen_work = malloc(need.gen_work_bytes);
    e.gen_work_bytes = need.gen_work_bytes;
    e.dec_work = malloc(need.dec_work_bytes);
    e.dec_work_bytes = need.dec_work_bytes;
    e.pcm24 = malloc((size_t) need.pcm24_cap * 2);
    e.pcm24_cap = need.pcm24_cap;
    CHECK(speech_zt_tts_ready(&e) == SPEECH_ZT_OK, "real voice ready");
    static int16_t pcm[16000 * 20];
    uint32_t n = 0;
    const char *say = getenv("ZXV_TTS_TEXT") ? getenv("ZXV_TTS_TEXT") : "Hello, I am your Chiglet.";
    int32_t r = speech_zt_tts_say(&e, say, "en", pcm, 16000 * 20, &n);
    printf("  real voice: %u codes, %u samples\n", e.last_codes, n);
    CHECK(r == SPEECH_ZT_OK && n > 8000, "a real sentence: %d, %u samples, %u codes", r, n,
          e.last_codes);
    const char *wav = getenv("ZXV_TTS_WAV");
    if (wav && r == 0) {
        FILE *f = fopen(wav, "wb");
        if (f) {
            uint8_t h[44] = {'R', 'I', 'F',  'F',  0,   0,   0,   0,    'W', 'A', 'V',
                             'E', 'f', 'm',  't',  ' ', 16,  0,   0,    0,   1,   0,
                             1,   0,   0x80, 0x3E, 0,   0,   0,   0x7D, 0,   0,   2,
                             0,   16,  0,    'd',  'a', 't', 'a', 0,    0,   0,   0};
            uint32_t data = n * 2, riff = 36 + data;
            memcpy(h + 4, &riff, 4);
            memcpy(h + 40, &data, 4);
            fwrite(h, 1, 44, f);
            fwrite(pcm, 2, n, f);
            fclose(f);
            printf("  wrote %s (%u samples, %u codes)\n", wav, n, e.last_codes);
        }
    }
    uint32_t n2 = 0;
    static int16_t pcm2[16000 * 20];
    speech_zt_tts_say(&e, say, "en", pcm2, 16000 * 20, &n2);
    CHECK(n2 == n && !memcmp(pcm, pcm2, (size_t) n * 2), "the real voice repeats exactly");
}

int main(void)
{
    if (load_decoder()) {
        printf("[FAIL] test_speech_zt_tts: the decoder fixture did not load\n");
        return 1;
    }
    test_ready_and_langs();
    test_say();
    test_refusals();
    test_respell();
    test_session();
    test_real();
    printf("%s test_speech_zt_tts: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
