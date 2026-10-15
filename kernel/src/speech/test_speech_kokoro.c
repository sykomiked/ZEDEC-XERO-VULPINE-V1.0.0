/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_speech_kokoro.c — the English pronouncer (speech_g2p_en, G1-G7) and
 * the Kokoro voice behind speech_tts_ops_t (speech_kokoro, KS1-KS6).
 * Offline it checks the dictionary parser fail-closed on a small file built
 * here, the context words, stems, compounds, spelling, numbers and
 * punctuation, and the refusals. With ZXV_KOKORO_MODEL (and optionally
 * ZXV_KOKORO_LEX, a .zxlx) it speaks ZXV_KOKORO_TEXT to ZXV_KOKORO_WAV
 * (16 kHz) and prints the phonemes and the time taken. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "speech_g2p_en.h"
#include "speech_kokoro.h"
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

/* a small .zxlx, the same layout as tools/kokoro/build_lexicon.py */
static uint32_t make_lex(uint8_t *b, const char *const *words, uint32_t n)
{
    uint32_t blob = 16 + 4 * n, o = blob;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = o - blob;
        b[16 + 4 * i] = (uint8_t) off;
        b[17 + 4 * i] = (uint8_t) (off >> 8);
        b[18 + 4 * i] = (uint8_t) (off >> 16);
        b[19 + 4 * i] = (uint8_t) (off >> 24);
        const char *w = words[2 * i], *p = words[2 * i + 1];
        memcpy(b + o, w, strlen(w) + 1);
        o += (uint32_t) strlen(w) + 1;
        memcpy(b + o, p, strlen(p) + 1);
        o += (uint32_t) strlen(p) + 1;
    }
    memcpy(b, "ZXLX", 4);
    b[4] = 1;
    b[5] = b[6] = b[7] = 0;
    uint32_t bl = o - blob;
    b[8] = (uint8_t) n, b[9] = (uint8_t) (n >> 8), b[10] = 0, b[11] = 0;
    b[12] = (uint8_t) bl, b[13] = (uint8_t) (bl >> 8), b[14] = (uint8_t) (bl >> 16), b[15] = 0;
    return o;
}

static const char *const LEXW[] = {
    "apple", "ˈæpəl",  "git",  "ɡˈɪt",  "hub",  "hˈʌb", "jump",  "ʤˈʌmp",
    "paris", "pˈɛɹəs", "stop", "stˈɑp", "wait", "wˈAt", "watch", "wˈɑʧ",
};

static speech_lex_t LEX;
static uint8_t LEXB[1024];
static uint32_t LEXN;

static const char *say(const speech_lex_t *lex, const char *t)
{
    static char out[2048];
    int32_t n = speech_g2p_en(lex, t, (uint32_t) strlen(t), out, sizeof out, 0);
    return n < 0 ? "<over>" : out;
}

static void test_lex(void)
{
    LEXN = make_lex(LEXB, LEXW, 8);
    CHECK(speech_lex_parse(&LEX, LEXB, LEXN) == SPEECH_LEX_OK && LEX.count == 8, "parse");
    CHECK(speech_lex_find(&LEX, "hub", 3) && !strcmp(speech_lex_find(&LEX, "hub", 3), "hˈʌb"),
          "find");
    CHECK(!speech_lex_find(&LEX, "hu", 2) && !speech_lex_find(&LEX, "zzz", 3), "not found");
    static uint8_t b[1024];
    speech_lex_t l;
    memcpy(b, LEXB, LEXN);
    b[0] = 'Y';
    CHECK(speech_lex_parse(&l, b, LEXN) == SPEECH_LEX_EMAGIC && !l.count, "magic");
    memcpy(b, LEXB, LEXN);
    b[4] = 2;
    CHECK(speech_lex_parse(&l, b, LEXN) == SPEECH_LEX_EVERSION, "version");
    CHECK(speech_lex_parse(&l, LEXB, LEXN - 1) == SPEECH_LEX_ESIZE, "truncated");
    CHECK(speech_lex_parse(&l, LEXB, 10) == SPEECH_LEX_ESIZE, "too short");
    memcpy(b, LEXB, LEXN);
    b[20] ^= 1;
    CHECK(speech_lex_parse(&l, b, LEXN) == SPEECH_LEX_EOFFSET, "offset");
    /* out of order */
    static const char *const bad[] = {"hub", "hˈʌb", "git", "ɡˈɪt"};
    uint32_t n = make_lex(b, bad, 2);
    CHECK(speech_lex_parse(&l, b, n) == SPEECH_LEX_EORDER, "order");
    static const char *const dup[] = {"git", "ɡˈɪt", "git", "ɡˈɪt"};
    n = make_lex(b, dup, 2);
    CHECK(speech_lex_parse(&l, b, n) == SPEECH_LEX_EORDER, "duplicate");
    static const char *const ph[] = {"git", "gIt"}; /* ASCII g is not Kokoro's ɡ */
    n = make_lex(b, ph, 1);
    CHECK(speech_lex_parse(&l, b, n) == SPEECH_LEX_EPHONE, "foreign phoneme");
    static const char *const sp[] = {"a b", "ɐ"};
    n = make_lex(b, sp, 1);
    CHECK(speech_lex_parse(&l, b, n) == SPEECH_LEX_ERECORD, "space in a word");
    CHECK(speech_lex_parse(0, LEXB, LEXN) == SPEECH_LEX_EARG, "null");
    /* every single-byte flip is refused or still parses to something checked */
    uint32_t refused = 0;
    for (uint32_t i = 0; i < LEXN; i++) {
        memcpy(b, LEXB, LEXN);
        b[i] ^= 0x41;
        if (speech_lex_parse(&l, b, LEXN) != SPEECH_LEX_OK) refused++;
    }
    CHECK(refused > LEXN / 2, "byte flips mostly refused (%u of %u)", refused, LEXN);
}

static void test_words(void)
{
    CHECK(!strcmp(say(0, "Hello, I am your Chiglet."), "həlˈO, ˌI æm jʊɹ ʧˈɪɡlət."),
          "the greeting: %s", say(0, "Hello, I am your Chiglet."));
    CHECK(!strcmp(say(0, "the apple"), "ði ˈæpəl") || strstr(say(0, "the apple"), "ði "),
          "the before a vowel: %s", say(0, "the apple"));
    CHECK(!strcmp(say(&LEX, "the apple"), "ði ˈæpəl"), "the + dictionary: %s",
          say(&LEX, "the apple"));
    CHECK(!strncmp(say(0, "the stop"), "ðə ", 5), "the before a consonant: %s", say(0, "the stop"));
    CHECK(!strcmp(say(&LEX, "to apple"), "tʊ ˈæpəl"), "to before a vowel");
    CHECK(!strcmp(say(&LEX, "to stop"), "tə stˈɑp"), "to before a consonant");
    CHECK(!strcmp(say(&LEX, "want to."), "wˈɑnt tu."), "to before a pause: %s",
          say(&LEX, "want to."));
    CHECK(!strcmp(say(&LEX, "a jump"), "ɐ ʤˈʌmp"), "a: %s", say(&LEX, "a jump"));
    CHECK(!strcmp(say(&LEX, "plan a."), "plˈæn ˈA."), "a alone: %s", say(&LEX, "plan a."));
    CHECK(!strcmp(say(&LEX, "an apple"), "ɐn ˈæpəl"), "an");
    CHECK(!strcmp(say(&LEX, "I am up"), "ˌI ɐm ˌʌp"), "am before a vowel: %s",
          say(&LEX, "I am up"));
    CHECK(!strcmp(say(&LEX, "Paris"), "pˈɛɹəs"), "case folded");
    /* stems */
    CHECK(!strcmp(say(&LEX, "jumps"), "ʤˈʌmps"), "-s after p: %s", say(&LEX, "jumps"));
    CHECK(!strcmp(say(&LEX, "watches"), "wˈɑʧᵻz"), "-es after ʧ: %s", say(&LEX, "watches"));
    CHECK(!strcmp(say(&LEX, "hubs"), "hˈʌbz"), "-s after b");
    CHECK(!strcmp(say(&LEX, "jumped"), "ʤˈʌmpt"), "-ed after p");
    CHECK(!strcmp(say(&LEX, "waited"), "wˈAɾᵻd"), "-ed after a vowel t: %s", say(&LEX, "waited"));
    CHECK(!strcmp(say(&LEX, "stopped"), "stˈɑpt"), "doubled consonant: %s", say(&LEX, "stopped"));
    CHECK(!strcmp(say(&LEX, "jumping"), "ʤˈʌmpɪŋ"), "-ing");
    CHECK(!strcmp(say(&LEX, "waiting"), "wˈAɾɪŋ"), "-ing flap");
    CHECK(!strcmp(say(&LEX, "stopping"), "stˈɑpɪŋ"), "-ing doubled");
    CHECK(!strcmp(say(&LEX, "Paris's"), "pˈɛɹəsᵻz"), "possessive: %s", say(&LEX, "Paris's"));
    /* compounds, ours, spelling */
    CHECK(!strcmp(say(&LEX, "githubs"), "ɡˈɪthˌʌbz") || strstr(say(&LEX, "githubs"), "ɡˈɪt"),
          "compound + s: %s", say(&LEX, "githubs"));
    CHECK(!strcmp(say(&LEX, "jumphub"), "ʤˈʌmphˌʌb"), "compound: %s", say(&LEX, "jumphub"));
    CHECK(!strcmp(say(0, "GitHub"), "ɡˈɪthʌb"), "ours first");
    CHECK(!strcmp(say(0, "ZXV"), "zˌiˌɛksvˈi"), "ZXV");
    CHECK(!strcmp(say(0, "CPU"), "sˌipˌijˈu"), "spelt: %s", say(0, "CPU"));
    CHECK(!strcmp(say(0, "x"), "ˈɛks"), "one letter: %s", say(0, "x"));
    CHECK(!strcmp(say(0, "github.com"), "ɡˈɪthʌb dˈɑt kˈɑm") ||
              strstr(say(0, "github.com"), "dˈɑt"),
          "a dot in a name: %s", say(0, "github.com"));
    /* rules: every word gets one primary stress and only Kokoro's symbols */
    static const char *const oov[] = {"florpinator", "blick",   "zorb",      "quixotically",
                                      "thwump",      "renewal", "xylophone", "unbelievable"};
    for (uint32_t i = 0; i < 8; i++) {
        char o[256];
        int32_t n = speech_g2p_word(0, oov[i], (uint32_t) strlen(oov[i]), o, sizeof o);
        uint32_t prim = 0;
        for (int32_t k = 0; k + 1 < n; k++)
            if ((uint8_t) o[k] == 0xCB && (uint8_t) o[k + 1] == 0x88) prim++;
        CHECK(n > 0 && prim == 1, "rules for %s: %s", oov[i], o);
    }
    CHECK(!strcmp(say(0, "blick"), "blˈɪk"), "blick: %s", say(0, "blick"));
    CHECK(!strcmp(say(0, "zorb"), "zˈɔɹb"), "zorb: %s", say(0, "zorb"));
    /* long words go through rules in pieces */
    char lw[200];
    memset(lw, 'a', 150);
    lw[150] = 0;
    CHECK(strcmp(say(0, lw), "<over>"), "a 150-letter word");
}

static void test_numbers(void)
{
    CHECK(!strcmp(say(0, "3 apps"), "θɹˈi ˈæps"), "3 apps: %s", say(0, "3 apps"));
    CHECK(!strcmp(say(0, "12%"), "twˈɛlv pəɹsˈɛnt"), "percent");
    CHECK(!strcmp(say(0, "1999"), "nˌIntˈin nˈIndi nˈIn"), "a year");
    CHECK(!strcmp(say(0, "2026"), "twˈɛnti twˈɛnti sˈɪks"), "this year");
    CHECK(!strcmp(say(0, "2005"), "tˈu θˈWzᵊnd fˈIv"), "two thousand five: %s", say(0, "2005"));
    CHECK(!strcmp(say(0, "1,234"), "wˈʌn θˈWzᵊnd tˈu hˈʌndɹəd θˈɜɹɾi fˈɔɹ"), "commas: %s",
          say(0, "1,234"));
    CHECK(!strcmp(say(0, "22nd"), "twˈɛnti sˈɛkənd"), "ordinal");
    CHECK(!strcmp(say(0, "1st"), "fˈɜɹst"), "first");
    CHECK(!strcmp(say(0, "-4"), "mˈInəs fˈɔɹ"), "minus");
    CHECK(!strcmp(say(0, "2.5"), "tˈu pˈYnt fˈIv"), "decimal");
    CHECK(!strcmp(say(0, "$1"), "wˈʌn dˈɑləɹ"), "one dollar");
    CHECK(!strcmp(say(0, "$3.50"), "θɹˈi dˈɑləɹz ænd fˈɪfti sˈɛnts"), "dollars and cents: %s",
          say(0, "$3.50"));
    CHECK(!strcmp(say(0, "4:05"), "fˈɔɹ ˈO fˈIv"), "a time");
    CHECK(!strcmp(say(0, "007"), "zˈɪɹO zˈɪɹO sˈɛvən"), "leading zeros: digits");
    CHECK(!strcmp(say(0, "1990s"), "nˌIntˈin nˈIndiz"), "decade: %s", say(0, "1990s"));
    CHECK(!strcmp(say(0, "1000000"), "wˈʌn mˈɪljᵊn"), "a million");
    CHECK(strcmp(say(0, "12345678901234567890"), "<over>"), "a long digit run");
}

static void test_punct(void)
{
    CHECK(!strcmp(say(0, "Stop -- now!"), "stˈɑp — nˈW!"), "dash: %s", say(0, "Stop -- now!"));
    CHECK(!strcmp(say(0, "wait..."), "wˈAt…"), "ellipsis: %s", say(0, "wait..."));
    CHECK(!strcmp(say(0, "(hello)"), "(həlˈO)"), "brackets: %s", say(0, "(hello)"));
    CHECK(!strcmp(say(0, "\xE2\x80\x9Chello\xE2\x80\x9D"), "“həlˈO”"), "curly quotes");
    CHECK(!strcmp(say(0, "me & you"), "mˌi ænd jˈu") || strstr(say(0, "me & you"), "ænd"),
          "ampersand: %s", say(0, "me & you"));
    CHECK(!strcmp(say(0, "\xF0\x9F\x98\x80"), ""), "an emoji says nothing");
    CHECK(!strcmp(say(0, "don\xE2\x80\x99t"), say(0, "don't")), "curly apostrophe");
    char out[8];
    CHECK(speech_g2p_en(0, "hello there", 11, out, sizeof out, 0) == -1, "G7: does not fit");
    CHECK(speech_g2p_en(0, 0, 0, out, sizeof out, 0) == -1, "null");
    speech_g2p_stats_t st;
    char o2[256];
    speech_g2p_en(&LEX, "the apple jumps over ZXV 3 florp", 32, o2, sizeof o2, &st);
    CHECK(st.words == 6 && st.numbers == 1 && st.stemmed == 1 && st.spelled == 0 && st.rules == 1,
          "stats: w%u n%u s%u sp%u r%u", st.words, st.numbers, st.stemmed, st.spelled, st.rules);
}

static void test_refusals(void)
{
    speech_kokoro_t e;
    memset(&e, 0, sizeof e);
    int16_t pcm[16];
    uint32_t n = 9;
    CHECK(speech_kokoro_say(&e, "hi", "en", pcm, 16, &n) == SPEECH_KK_EARG, "not ready");
    CHECK(speech_kokoro_has_lang("en") && speech_kokoro_has_lang("en-GB") &&
              speech_kokoro_has_lang("") && !speech_kokoro_has_lang("fr") &&
              !speech_kokoro_has_lang("eng"),
          "languages");
    CHECK(strstr(speech_kokoro_attribution, "Apache-2.0") &&
              strstr(speech_kokoro_attribution, "CC BY 3.0") &&
              strstr(speech_kokoro_attribution, "CC BY 4.0") &&
              strstr(speech_g2p_en_attribution, "misaki"),
          "credits");
}

static void test_real(void)
{
    const char *mp = getenv("ZXV_KOKORO_MODEL");
    if (!mp) {
        printf("[skip] real Kokoro voice (set ZXV_KOKORO_MODEL)\n");
        return;
    }
    FILE *f = fopen(mp, "rb");
    CHECK(f, "open the model");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc((size_t) len);
    CHECK(fread(file, 1, (size_t) len, f) == (size_t) len, "read");
    fclose(f);
    zt_gguf_t g;
    CHECK(zt_gguf_open(&g, file, (uint64_t) len) == ZT_GGUF_OK, "gguf");
    uint64_t ab = zt_kokoro_arena_bytes(&g);
    void *arena = malloc(ab);
    static zt_kokoro_t k;
    zt_model_err_t err;
    CHECK(zt_kokoro_load(&k, &g, arena, ab, &err) == ZT_GGUF_OK, "load");
    zt_pool_t *pool = zt_pool_new(zt_pool_ncpu());
    zt_kern_t kern;
    zt_kern_threaded(&kern, zt_simd_best(), pool);
    k.kern = &kern;
    speech_lex_t lex, *lp = 0;
    uint8_t *lb = 0;
    const char *lf = getenv("ZXV_KOKORO_LEX");
    if (lf && (f = fopen(lf, "rb"))) {
        fseek(f, 0, SEEK_END);
        long ln = ftell(f);
        fseek(f, 0, SEEK_SET);
        lb = malloc((size_t) ln);
        CHECK(fread(lb, 1, (size_t) ln, f) == (size_t) ln, "read lexicon");
        fclose(f);
        CHECK(speech_lex_parse(&lex, lb, (uint64_t) ln) == SPEECH_LEX_OK, "lexicon parses");
        lp = &lex;
    }
    static speech_kokoro_t e;
    e.k = &k;
    e.lex = lp;
    e.voice = getenv("ZXV_KOKORO_VOICE") ? getenv("ZXV_KOKORO_VOICE") : "af_heart";
    e.speed_pct = 100;
    e.max_frames = 40 * 20;
    speech_kokoro_need(&k, e.max_frames, &e.work_bytes, &e.pcm24_cap);
    e.work = malloc(e.work_bytes);
    e.pcm24 = malloc((size_t) e.pcm24_cap * 2);
    const char *text = getenv("ZXV_KOKORO_TEXT");
    if (!text) text = "Hello, I am your Chiglet.";
    uint32_t cap = 16000 * 120, n = 0;
    int16_t *pcm = malloc((size_t) cap * 2);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int32_t r = speech_kokoro_say(&e, text, "en", pcm, cap, &n);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double) (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    CHECK(r == SPEECH_KK_OK && n > 0, "speak: %d", r);
    printf("phonemes: %s\n", e.phon);
    printf("%u pieces, %.2f s for %.2f s of speech, clipped %u, words %u (lexicon %u, built-in %u, "
           "rules %u)\n",
           e.pieces, s, n / 16000.0, e.last_clip, e.last_g2p.words, e.last_g2p.lexicon,
           e.last_g2p.builtin, e.last_g2p.rules);
    const char *wp = getenv("ZXV_KOKORO_WAV");
    if (wp && (f = fopen(wp, "wb"))) {
        uint32_t rate = 16000, bytes = n * 2, v;
        uint16_t h16[4] = {1, 1, 2, 16};
        fwrite("RIFF", 1, 4, f);
        v = 36 + bytes;
        fwrite(&v, 4, 1, f);
        fwrite("WAVEfmt ", 1, 8, f);
        v = 16;
        fwrite(&v, 4, 1, f);
        fwrite(h16, 2, 2, f);
        fwrite(&rate, 4, 1, f);
        v = rate * 2;
        fwrite(&v, 4, 1, f);
        fwrite(h16 + 2, 2, 2, f);
        fwrite("data", 1, 4, f);
        fwrite(&bytes, 4, 1, f);
        fwrite(pcm, 2, n, f);
        fclose(f);
    }
    zt_pool_free(pool);
    free(pcm);
    free(e.work);
    free(e.pcm24);
    free(lb);
    free(arena);
    free(file);
}

int main(void)
{
    test_lex();
    test_words();
    test_numbers();
    test_punct();
    test_refusals();
    test_real();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
