/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_zt_stt.c — the on-device ears. See speech_zt_stt.h. */
#include "speech_zt_stt.h"

const char speech_zt_stt_attribution[] =
    "Speech recognition: openai/whisper-base, Apache-2.0 (MIT upstream).";

void speech_zt_stt_defaults(speech_zt_stt_t *e)
{
    if (!e) return;
    uint8_t *p = (uint8_t *) e;
    for (uint32_t i = 0; i < sizeof *e; i++) p[i] = 0;
    e->quiet_level = SPEECH_ZT_STT_QUIET;
}

int32_t speech_zt_stt_ready(speech_zt_stt_t *e)
{
    if (!e) return SPEECH_ZT_STT_EARG;
    e->ready = e->w && e->st && e->tok && e->g && e->ids && e->ids_cap >= 4;
    return e->ready ? SPEECH_ZT_STT_OK : SPEECH_ZT_STT_EARG;
}

uint32_t speech_zt_stt_level(const int16_t *pcm, uint32_t n)
{
    if (!pcm || !n) return 0;
    uint64_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (uint32_t) (pcm[i] < 0 ? -(int32_t) pcm[i] : pcm[i]);
    return (uint32_t) (s / n);
}

/* ZT4: did the no-speech token outscore the first word chosen? */
typedef struct {
    int32_t no_speech, bias;
    bool none;
} step_t;

static bool on_step(void *ctx, uint32_t step, const zt_fx *logits, int32_t token)
{
    step_t *s = ctx;
    if (step == 0 && s->no_speech >= 0 && token >= 0 &&
        (int64_t) logits[s->no_speech] + s->bias > (int64_t) logits[token]) {
        s->none = true;
        return false;
    }
    return true;
}

/* ZT3: Whisper's index for a hint's base code, or -1 */
static int32_t hint_lang(const zt_gguf_t *g, const char *h)
{
    if (!h || !h[0]) return -1;
    char b[8];
    uint32_t k = 0;
    for (; h[k] && h[k] != '-' && h[k] != '_' && k < sizeof b - 1; k++) {
        char c = h[k];
        b[k] = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
    }
    if (h[k] && h[k] != '-' && h[k] != '_') return -1; /* too long to be a code */
    b[k] = 0;
    return zt_whisper_lang_find(g, b);
}

static void put_lang(speech_zt_stt_t *e, uint32_t li, char *out, uint32_t cap)
{
    zt_gguf_str_t s = zt_whisper_lang_code(e->g, li);
    uint32_t k = 0;
    for (; k < s.len && k < sizeof e->lang - 1; k++) e->lang[k] = (char) s.p[k];
    e->lang[k] = 0;
    if (!out || !cap) return;
    uint32_t j = 0;
    for (; e->lang[j] && j + 1 < cap; j++) out[j] = e->lang[j];
    out[j] = 0;
}

int speech_zt_stt_transcribe(const int16_t *pcm, uint32_t n, const char *lang_hint, char *text,
                             uint32_t cap, char *lang_out, uint32_t lang_cap, void *ctx)
{
    speech_zt_stt_t *e = ctx;
    if (text && cap) text[0] = 0;
    if (lang_out && lang_cap) lang_out[0] = 0;
    if (!e || !e->ready || !pcm || !text || cap < 2) return SPEECH_ZT_STT_EARG;
    e->utterances++;
    if (speech_zt_stt_level(pcm, n) < e->quiet_level) { /* ZT4 */
        e->silent++;
        return SPEECH_ZT_STT_OK;
    }
    const zt_whisper_cfg_t *c = &e->w->cfg;
    uint32_t win = c->n_samples ? c->n_samples : 480000u;
    uint32_t maxt = e->max_tokens ? e->max_tokens : c->n_text_ctx / 2;
    int32_t li = hint_lang(e->g, lang_hint);
    uint32_t out = 0;
    for (uint32_t off = 0; off < n; off += win) {
        uint32_t m = n - off < win ? n - off : win;
        /* a quiet tail window is not transcribed */
        if (off && speech_zt_stt_level(pcm + off, m) < e->quiet_level) break;
        e->windows++;
        if (zt_whisper_mel(e->w, e->st, pcm + off, m) != 0 || zt_whisper_encode(e->w, e->st) != 0)
            goto fail;
        if (li < 0) { /* ZT3: from the first window */
            uint32_t d = 0;
            if (zt_whisper_detect_lang(e->w, e->st, &d) != 0) goto fail;
            li = (int32_t) d;
        }
        step_t sc = {c->no_speech, e->no_speech_bias, false};
        uint32_t got = 0;
        if (zt_whisper_greedy(e->w, e->st, (uint32_t) li, false, maxt, e->ids, e->ids_cap, &got,
                              on_step, &sc) != 0)
            goto fail;
        if (sc.none) {
            e->no_speech++;
            continue;
        }
        if (!got) continue;
        /* the words, after a space between windows */
        if (out && out + 1 < cap) text[out++] = ' ';
        uint64_t nt = 0;
        int32_t r = zt_tok_decode(e->tok, e->ids, got, (uint8_t *) text + out, cap - 1 - out, &nt);
        if (r != 0) e->cut++; /* it did not fit: keep what did (nt), cut at a character */
        uint32_t s = out, k = (uint32_t) nt;
        while (k && text[s] == ' ') { /* ZT1: no leading space */
            for (uint32_t i = s; i + 1 < s + k; i++) text[i] = text[i + 1];
            k--;
        }
        out = s + k;
        while (out && ((uint8_t) text[out - 1] & 0xC0u) == 0x80u && r != 0) out--;
        text[out] = 0;
        if (r != 0) break;
    }
    while (out && text[out - 1] == ' ') out--;
    text[out] = 0;
    if (li >= 0) put_lang(e, (uint32_t) li, lang_out, lang_cap);
    return SPEECH_ZT_STT_OK;
fail:
    e->failed++;
    text[0] = 0;
    return SPEECH_ZT_STT_EMODEL;
}

void speech_zt_stt_ops(speech_zt_stt_t *e, speech_stt_ops_t *ops)
{
    if (!ops) return;
    ops->transcribe = speech_zt_stt_transcribe;
    ops->ctx = e;
}
