/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_zt_tts.c — the on-device voice. See speech_zt_tts.h. */
#include "speech_zt_tts.h"

const char speech_zt_tts_attribution[] = "Speech model: OuteTTS-0.3-500M by OuteAI, CC-BY-SA-4.0. "
                                         "Audio decoder: WavTokenizer-Large-75, MIT.";

const speech_zt_word_t speech_zt_lexicon[] = {
    {"chiglet", "chig lett"},
    {"chiglets", "chig letts"},
    {"chiglet's", "chig lett's"},
    {"zxv", "zee ex vee"},
    {"zedec", "zed deck"},
    {"zedei", "zed day"},
    {"v-bill", "vee bill"},
    {"v-bills", "vee bills"},
    {"ai", "ay eye"},
    {"gpu", "gee pee you"},
    {"cpu", "see pee you"},
    {"ui", "you eye"},
    {"pq", "pee queue"},
    {"tts", "tee tee ess"},
    {"ascw", "ay ess see double you"},
    {0, 0},
};

static bool word_ch(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '\'';
}

static const char *look_up(const speech_zt_word_t *t, const char *w, uint32_t n)
{
    for (; t && t->word; t++) {
        uint32_t i = 0;
        for (; i < n && t->word[i]; i++) {
            char c = w[i];
            if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
            if (c != t->word[i]) break;
        }
        if (i == n && t->word[i] == 0) return t->say;
    }
    return 0;
}

int32_t speech_zt_respell(const speech_zt_word_t *extra, const char *text, char *out, uint32_t cap)
{
    if (!text || !out || !cap) return -1;
    uint32_t o = 0;
    for (uint32_t i = 0; text[i];) {
        const char *src = text + i;
        uint32_t n = 1;
        const char *say = 0;
        if (word_ch(text[i])) {
            n = 0;
            while (word_ch(text[i + n])) n++;
            say = look_up(extra, src, n);
            if (!say) say = look_up(speech_zt_lexicon, src, n);
        }
        const char *put = say ? say : src;
        uint32_t k = n;
        if (say) {
            k = 0;
            while (say[k]) k++;
        }
        if (o + k + 1 > cap) return -1;
        for (uint32_t j = 0; j < k; j++) out[o + j] = put[j];
        o += k;
        i += n;
    }
    out[o] = 0;
    return (int32_t) o;
}

void speech_zt_tts_defaults(speech_zt_tts_t *e)
{
    if (!e) return;
    uint8_t *p = (uint8_t *) e;
    for (uint32_t i = 0; i < sizeof *e; i++) p[i] = 0;
    e->seed = 42;
    e->top_k = 4;
    e->temperature = 65536;
}

int32_t speech_zt_tts_ready(speech_zt_tts_t *e)
{
    if (!e) return SPEECH_ZT_EARG;
    e->ready = false;
    if (!e->dec || !e->prompt || !e->prompt_cap || !e->ids || !e->max_gen || !e->dec_work ||
        !e->pcm24 || e->pcm24_cap < ZT_TTS_HOP)
        return SPEECH_ZT_EARG;
    if (!e->codes_fn) {
        if (!e->lm || !e->st || !e->tok || !e->gen_work) return SPEECH_ZT_EARG;
        if (zt_tts_vocab(e->tok, &e->voc) != 0) return SPEECH_ZT_EMODEL;
    }
    e->ready = true;
    return SPEECH_ZT_OK;
}

void speech_zt_tts_need(const zt_model_t *lm, const zt_tok_t *tok, const zt_tts_dec_t *dec,
                        uint32_t max_text, uint32_t max_gen, speech_zt_need_t *need)
{
    if (!need) return;
    /* the prompt: the speaker's words and codes, then up to max_text bytes,
     * each of which can grow to "<|space|>" (9) or a number's words */
    uint64_t base = zt_tts_prompt((const uint8_t *) "", 0, 0, 0);
    uint64_t p = base + (uint64_t) max_text * 16u + 64u;
    need->prompt_cap = p > 0xFFFFFFF0u ? 0xFFFFFFF0u : (uint32_t) p;
    need->gen_work_bytes = lm && tok ? zt_model_gen_work_bytes(lm, tok, p, max_gen) : 0;
    need->dec_work_bytes = dec ? zt_tts_work_bytes(dec, max_gen) : 0;
    need->pcm24_cap = ZT_TTS_HOP * max_gen;
}

bool speech_zt_tts_has_lang(const char *l)
{
    if (!l || !l[0]) return true;
    char a = l[0], b = l[1];
    if (a >= 'A' && a <= 'Z') a = (char) (a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char) (b - 'A' + 'a');
    return a == 'e' && b == 'n' && (l[2] == 0 || l[2] == '-' || l[2] == '_');
}

typedef struct {
    int32_t *ids;
    uint32_t n, cap;
} gen_t;

static bool on_token(void *ctx, int32_t id, const uint8_t *piece, uint32_t len)
{
    (void) piece;
    (void) len;
    gen_t *g = ctx;
    if (g->n >= g->cap) return false;
    g->ids[g->n++] = id;
    return true;
}

/* the language model: prompt -> code ids in e->ids */
static int32_t lm_codes(speech_zt_tts_t *e, uint64_t plen, uint32_t *n)
{
    zt_model_state_seek(e->st, 0);
    zt_sampler_params_t sp;
    uint8_t *q = (uint8_t *) &sp;
    for (uint32_t i = 0; i < sizeof sp; i++) q[i] = 0;
    sp.temperature = e->temperature;
    sp.top_k = e->top_k;
    sp.seed = e->seed; /* ZS4 */
    zt_sampler_t smp;
    zt_sampler_init(&smp, &sp);
    int32_t stops[2] = {e->voc.audio_end, e->voc.eot};
    gen_t g = {e->ids, 0, e->max_gen};
    zt_gen_params_t gp;
    q = (uint8_t *) &gp;
    for (uint32_t i = 0; i < sizeof gp; i++) q[i] = 0;
    gp.sampler = e->temperature > 0 && e->top_k != 1 ? &smp : 0;
    gp.max_tokens = e->max_gen;
    gp.parse_special = true;
    gp.stop_ids = stops;
    gp.n_stop_ids = e->voc.eot >= 0 ? 2u : 1u;
    gp.on_token = on_token;
    gp.ctx = &g;
    zt_gen_result_t res;
    if (zt_model_generate(e->lm, e->st, e->tok, e->prompt, plen, &gp, e->gen_work,
                          e->gen_work_bytes, &res) != 0)
        return SPEECH_ZT_EMODEL;
    *n = zt_tts_codes(&e->voc, e->ids, g.n, e->ids);
    return SPEECH_ZT_OK;
}

static const int16_t ZEROS[256];

int32_t speech_zt_tts_say(speech_zt_tts_t *e, const char *text, const char *lang, int16_t *pcm16,
                          uint32_t cap, uint32_t *out_n)
{
    if (out_n) *out_n = 0;
    if (!e || !e->ready || !text || !pcm16 || !out_n) return SPEECH_ZT_EARG;
    if (!speech_zt_tts_has_lang(lang)) {
        e->refused++;
        return SPEECH_ZT_ENOLANG;
    }
    if (speech_zt_respell(e->lexicon, text, e->spelled, sizeof e->spelled) >= 0) text = e->spelled;
    uint64_t tlen = 0;
    while (text[tlen]) tlen++;
    if (zt_tts_normalize((const uint8_t *) text, tlen, 0, 0) == 0) {
        e->refused++;
        return SPEECH_ZT_ENOTEXT;
    }
    uint64_t plen = zt_tts_prompt((const uint8_t *) text, tlen, 0, 0);
    if (plen + 1 > e->prompt_cap) {
        e->refused++;
        return SPEECH_ZT_ESPACE;
    }
    zt_tts_prompt((const uint8_t *) text, tlen, e->prompt, plen);
    e->prompt[plen] = 0;

    uint32_t n = 0;
    int32_t r = e->codes_fn ? e->codes_fn(e->codes_ctx, e->prompt, plen, e->ids, e->max_gen, &n)
                            : lm_codes(e, plen, &n);
    if (r != 0) {
        e->failed++;
        return SPEECH_ZT_EMODEL;
    }
    if (n > e->max_gen) n = e->max_gen;
    if (n * ZT_TTS_HOP > e->pcm24_cap) n = e->pcm24_cap / ZT_TTS_HOP;
    e->last_codes = n;
    if (n == 0) {
        e->failed++;
        return SPEECH_ZT_ENOCODES;
    }
    zt_tts_stats_t st;
    if (zt_tts_decode(e->dec, e->ids, n, e->pcm24, ZT_TTS_SKIP, e->dec_work, e->dec_work_bytes,
                      &st) != 0) {
        e->failed++;
        return SPEECH_ZT_EMODEL;
    }
    e->last_clip = st.n_clip;
    uint32_t total = n * ZT_TTS_HOP;
    uint32_t lead = total > ZT_TTS_SKIP ? ZT_TTS_SKIP : total; /* ZS1: drop the zeroed lead */

    /* 24 kHz -> 16 kHz, then the resampler's tail */
    speech_resampler_init(&e->rs, ZT_TTS_RATE);
    uint32_t out = 0, at = lead, flush = speech_resampler_latency(&e->rs);
    bool cut = false;
    while (at < total && !cut) {
        uint32_t used = 0;
        out += speech_resample(&e->rs, e->pcm24 + at, total - at, &used, pcm16 + out, cap - out);
        at += used;
        if (out >= cap && at < total) cut = true;
        if (!used && !cut) break;
    }
    while (flush && !cut) {
        uint32_t k = flush > 256u ? 256u : flush, used = 0;
        out += speech_resample(&e->rs, ZEROS, k, &used, pcm16 + out, cap - out);
        flush -= used;
        if (out >= cap && flush) cut = true;
        if (!used && !cut) break;
    }
    if (cut) e->cut++;
    e->last_samples = out;
    e->utterances++;
    *out_n = out;
    return SPEECH_ZT_OK;
}

int speech_zt_tts_synthesize(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                             uint32_t *out_len, void *ctx)
{
    return speech_zt_tts_say((speech_zt_tts_t *) ctx, text, lang, pcm, cap, out_len);
}

void speech_zt_tts_ops(speech_zt_tts_t *e, speech_tts_ops_t *ops)
{
    if (!ops) return;
    ops->synthesize = speech_zt_tts_synthesize;
    ops->ctx = e;
}
