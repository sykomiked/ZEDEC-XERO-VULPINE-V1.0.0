/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_kokoro.h — the Chiglet's clear voice: Kokoro-82M on the integer
 * engine, behind speech_session's text-level speech-out boundary.
 *
 * It replaces OuteTTS (speech_zt_tts.h) as the voice the Chiglet, the
 * screen reader and speech_session_say use: OuteTTS slurred long sentences;
 * Kokoro reads them cleanly and is not autoregressive, so it is far faster.
 *
 *   KS1 TEXT IN, 16 kHz OUT. speech_kokoro_say turns English text into
 *       Kokoro's phonemes (speech_g2p_en.h), speaks them with the chosen
 *       voice (tensor/zt_kokoro.h, 24 kHz) and resamples to 16 kHz
 *       (speech_dsp.h, flushed to the end). Nothing leaves the machine.
 *   KS2 ENGLISH, AND IT SAYS SO. "", "en" and "en-..." are spoken (the voices
 *       are American English); any other language is refused
 *       (SPEECH_KK_ENOLANG) so the caller shows the words instead
 *       (zui_chgvoice.h CV8). Text with nothing to say is refused too.
 *   KS3 LONG TEXT IN PIECES. Kokoro takes 510 phonemes at a time; longer
 *       text is cut at a pause (. , ! ? ; : — …) or, failing one, a space,
 *       spoken piece by piece and joined.
 *   KS4 THE SAME TEXT SOUNDS THE SAME, bit for bit, on every machine
 *       (zt_kokoro.h: fixed source noise, integer arithmetic).
 *   KS5 BOUNDED. The caller owns every buffer (speech_kokoro_need); a piece
 *       longer than max_frames is refused, PCM past the output is cut and
 *       counted.
 *   KS6 WEIGHTS AND DICTIONARY ARE NEVER IN GIT: tools/kokoro/kokoro2gguf.py
 *       and build_lexicon.py make them from the released files. Ship
 *       speech_kokoro_attribution with the voice.
 *
 * Freestanding: no libc, no allocation, no float. Single-threaded.
 */
#ifndef ZXV_SPEECH_KOKORO_H
#define ZXV_SPEECH_KOKORO_H

#include <stdbool.h>
#include <stdint.h>
#include "speech_dsp.h"
#include "speech_g2p_en.h"
#include "speech_session.h"
#include "zt_kokoro.h"

typedef enum {
    SPEECH_KK_OK = 0,
    SPEECH_KK_EARG = -1,    /* NULL, or not ready                         */
    SPEECH_KK_ENOLANG = -2, /* KS2: no voice for that language            */
    SPEECH_KK_ENOTEXT = -3, /* KS2: nothing to say                        */
    SPEECH_KK_ESPACE = -4,  /* KS5: a buffer is too small                 */
    SPEECH_KK_EVOICE = -5,  /* the model has no such voice                */
    SPEECH_KK_EMODEL = -6   /* the forward pass failed                    */
} speech_kokoro_result_t;

#define SPEECH_KK_PHON  8192u /* bytes of phonemes for one call          */
#define SPEECH_KK_PIECE 400u  /* code points per piece (Kokoro: 510)      */

typedef struct {
    const zt_kokoro_t *k;    /* loaded by the caller                       */
    const speech_lex_t *lex; /* may be NULL: the built-in words (G3)       */
    const char *voice;       /* e.g. "af_heart"                            */
    uint32_t speed_pct;      /* 100: the voice's own pace                  */
    /* buffers (speech_kokoro_need) */
    void *work;
    uint64_t work_bytes;
    uint32_t max_frames; /* per piece; 40 a second                          */
    int16_t *pcm24;
    uint32_t pcm24_cap;
    /* scratch */
    char phon[SPEECH_KK_PHON];
    int32_t ids[ZT_KOKORO_MAX_TOK];
    zt_fx style[ZT_KOKORO_STYLE];
    speech_resampler_t rs;
    /* counters */
    uint32_t utterances, pieces, refused, failed, cut;
    uint32_t last_samples, last_clip;
    speech_g2p_stats_t last_g2p;
} speech_kokoro_t;

/* Work bytes and 24 kHz samples for pieces of up to max_frames frames. */
void speech_kokoro_need(const zt_kokoro_t *k, uint32_t max_frames, uint64_t *work_bytes,
                        uint32_t *pcm24_cap);

bool speech_kokoro_has_lang(const char *bcp47);

/* KS1: text to 16 kHz PCM; writes at most cap samples, the count in *out_n. */
int32_t speech_kokoro_say(speech_kokoro_t *e, const char *text, const char *lang, int16_t *pcm16,
                          uint32_t cap, uint32_t *out_n);

/* speech_tts_ops_t.synthesize (ctx: a speech_kokoro_t). */
int speech_kokoro_synthesize(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                             uint32_t *out_len, void *ctx);
void speech_kokoro_ops(speech_kokoro_t *e, speech_tts_ops_t *ops);

/* KS6 */
extern const char speech_kokoro_attribution[];

#endif /* ZXV_SPEECH_KOKORO_H */
