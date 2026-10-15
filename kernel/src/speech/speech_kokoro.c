/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_kokoro.c — Kokoro behind speech_tts_ops_t. See speech_kokoro.h. */
#include "speech_kokoro.h"

const char speech_kokoro_attribution[] =
    "Voice: Kokoro-82M by hexgrad (Apache-2.0). Its training data includes Koniwa "
    "(CC BY 3.0) and SIWIS (CC BY 4.0). English pronunciations from misaki by hexgrad "
    "(Apache-2.0).";

static const int16_t ZEROS[256];

void speech_kokoro_need(const zt_kokoro_t *k, uint32_t max_frames, uint64_t *work_bytes,
                        uint32_t *pcm24_cap)
{
    if (work_bytes) *work_bytes = zt_kokoro_work_bytes(k, ZT_KOKORO_MAX_TOK, max_frames);
    if (pcm24_cap) *pcm24_cap = max_frames * ZT_KOKORO_HOP_SAMPLES;
}

bool speech_kokoro_has_lang(const char *l)
{
    if (!l || !l[0]) return true;
    char a = l[0], b = l[1];
    if (a >= 'A' && a <= 'Z') a = (char) (a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char) (b - 'A' + 'a');
    return a == 'e' && b == 'n' && (l[2] == 0 || l[2] == '-' || l[2] == '_');
}

static bool is_pause(const char *p, uint32_t i, uint32_t n)
{
    char c = p[i];
    if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') return true;
    /* — (E2 80 94) and … (E2 80 A6): the last byte */
    return i >= 2 && (uint8_t) p[i - 2] == 0xE2 && (uint8_t) p[i - 1] == 0x80 &&
           ((uint8_t) c == 0x94 || (uint8_t) c == 0xA6) && i < n;
}

/* KS3: the end (exclusive, in bytes) of the next piece starting at s */
static uint32_t piece_end(const char *p, uint32_t n, uint32_t s)
{
    uint32_t cps = 0, last_pause = 0, last_space = 0, i = s;
    while (i < n) {
        if (((uint8_t) p[i] & 0xC0) != 0x80) {
            if (cps == SPEECH_KK_PIECE) break;
            cps++;
        }
        if (is_pause(p, i, n)) last_pause = i + 1;
        if (p[i] == ' ') last_space = i + 1;
        i++;
    }
    if (i >= n) return n;
    if (last_pause > s) return last_pause;
    if (last_space > s) return last_space;
    return i;
}

/* 24 kHz in, appended to pcm16 at *out; false when the output is full */
static bool push(speech_kokoro_t *e, const int16_t *in, uint32_t n, int16_t *pcm16, uint32_t cap,
                 uint32_t *out)
{
    uint32_t at = 0;
    while (at < n) {
        uint32_t used = 0;
        *out += speech_resample(&e->rs, in + at, n - at, &used, pcm16 + *out, cap - *out);
        at += used;
        if (*out >= cap && at < n) return false;
        if (!used) break;
    }
    return true;
}

int32_t speech_kokoro_say(speech_kokoro_t *e, const char *text, const char *lang, int16_t *pcm16,
                          uint32_t cap, uint32_t *out_n)
{
    if (!e || !e->k || !e->voice || !text || !pcm16 || !out_n || !e->work || !e->pcm24)
        return SPEECH_KK_EARG;
    *out_n = 0;
    if (!speech_kokoro_has_lang(lang)) {
        e->refused++;
        return SPEECH_KK_ENOLANG;
    }
    uint32_t tl = 0;
    while (text[tl]) tl++;
    int32_t pn = speech_g2p_en(e->lex, text, tl, e->phon, sizeof e->phon, &e->last_g2p);
    if (pn < 0) {
        e->refused++;
        return SPEECH_KK_ESPACE;
    }
    if (pn == 0) {
        e->refused++;
        return SPEECH_KK_ENOTEXT;
    }
    speech_resampler_init(&e->rs, ZT_KOKORO_RATE);
    uint32_t n = (uint32_t) pn, out = 0, spoken = 0;
    bool cut = false;
    e->last_clip = 0;
    for (uint32_t s = 0; s < n && !cut;) {
        uint32_t end = piece_end(e->phon, n, s);
        while (s < end && e->phon[s] == ' ') s++;
        uint32_t m = zt_kokoro_ids(e->k, e->phon + s, end - s, e->ids, ZT_KOKORO_MAX_TOK);
        uint32_t cps = 0;
        for (uint32_t i = s; i < end; i++) cps += ((uint8_t) e->phon[i] & 0xC0) != 0x80;
        s = end;
        if (m < 3) continue; /* nothing Kokoro knows in this piece */
        if (zt_kokoro_voice(e->k, e->voice, cps, e->style)) {
            e->failed++;
            return SPEECH_KK_EVOICE;
        }
        zt_kokoro_stats_t st;
        int32_t r =
            zt_kokoro_synth(e->k, e->ids, m, e->style, e->speed_pct ? e->speed_pct : 100,
                            e->max_frames, e->pcm24, e->pcm24_cap, e->work, e->work_bytes, &st);
        if (r) {
            e->failed++;
            return r == ZT_NN_ESPACE ? SPEECH_KK_ESPACE : SPEECH_KK_EMODEL;
        }
        e->pieces++;
        spoken++;
        e->last_clip += st.clipped;
        if (!push(e, e->pcm24, st.samples, pcm16, cap, &out)) cut = true;
    }
    if (!spoken) {
        e->refused++;
        return SPEECH_KK_ENOTEXT;
    }
    for (uint32_t flush = speech_resampler_latency(&e->rs); flush && !cut;) {
        uint32_t k = flush > 256u ? 256u : flush;
        if (!push(e, ZEROS, k, pcm16, cap, &out)) cut = true;
        flush -= k;
    }
    if (cut) e->cut++;
    e->last_samples = out;
    e->utterances++;
    *out_n = out;
    return SPEECH_KK_OK;
}

int speech_kokoro_synthesize(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                             uint32_t *out_len, void *ctx)
{
    return speech_kokoro_say((speech_kokoro_t *) ctx, text, lang, pcm, cap, out_len);
}

void speech_kokoro_ops(speech_kokoro_t *e, speech_tts_ops_t *ops)
{
    if (!ops) return;
    ops->synthesize = speech_kokoro_synthesize;
    ops->ctx = e;
}
