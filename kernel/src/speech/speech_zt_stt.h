/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_zt_stt.h — the on-device ears: Whisper on the integer engine,
 * behind speech_session's text-level speech-in boundary.
 *
 * tensor/zt_whisper.h holds the model (log-mel, encoder, greedy decoder);
 * this module turns one captured utterance into text for speech_session, so
 * dictation, voice control (zui_voicectl.h), talking to the Chiglet
 * (zui_chgvoice.h) and live captions all hear through the same model.
 * Nothing leaves the machine, and nothing is heard unless the session was
 * listening: push-to-talk, or hands-free when the person armed it.
 *
 *   ZT1 AUDIO IN, TEXT OUT. speech_zt_stt_transcribe takes 16 kHz mono PCM,
 *       makes the log-mel, encodes it and decodes greedily, then turns the
 *       tokens into UTF-8 (zt_tok_decode) with the leading space trimmed.
 *   ZT2 LONGER THAN 30 s. Whisper hears 30 s at a time; longer audio is cut
 *       into 30 s windows, each transcribed and joined with a space. (A word
 *       on a window's edge can be split: there is no overlap yet.)
 *   ZT3 LANGUAGE. A hint ("fr", "pt-BR") picks Whisper's language by its
 *       base code; no hint, or one Whisper does not know, is detected from
 *       the first window. The language heard is written to lang_out.
 *   ZT4 SILENCE IS NOT WORDS. Whisper writes text even for silence ("Thank
 *       you."). Audio whose mean level is under quiet_level gives "" without
 *       running the model; and a window whose no-speech token outscores the
 *       first word it chose (plus no_speech_bias, 0 by default; raise it to
 *       treat more doubtful audio as silence) gives "" too. speech_session then reports
 *       SPEECH_ERR_EMPTY instead of acting on words nobody said.
 *   ZT5 WEIGHTS ARE NEVER IN GIT. openai/whisper-base (Apache-2.0 on Hugging
 *       Face, MIT upstream) is fetched by bench/tensor/fetch_pinned.py and
 *       converted by bench/tensor/convert_whisper.py.
 *
 * Freestanding: no libc, no allocation, no float. Single-threaded.
 */
#ifndef ZXV_SPEECH_ZT_STT_H
#define ZXV_SPEECH_ZT_STT_H

#include <stdbool.h>
#include <stdint.h>
#include "speech_session.h"
#include "zt_tok.h"
#include "zt_whisper.h"

typedef enum {
    SPEECH_ZT_STT_OK = 0,
    SPEECH_ZT_STT_EARG = -1,  /* NULL, or not ready                  */
    SPEECH_ZT_STT_EMODEL = -2 /* the model or the tokenizer failed   */
} speech_zt_stt_result_t;

#define SPEECH_ZT_STT_QUIET 120u /* ZT4: mean |sample| below this is silence */

typedef struct {
    /* loaded by the caller, with w->kern bound to the machine's kernel set
     * (zt_simd_best, threaded) */
    const zt_whisper_t *w;
    zt_whisper_state_t *st;
    const zt_tok_t *tok;
    const zt_gguf_t *g; /* the language codes */
    /* token buffer: n_text_ctx ids */
    int32_t *ids;
    uint32_t ids_cap;
    uint32_t max_tokens; /* per window; 0: n_text_ctx / 2 */
    uint32_t quiet_level;
    int32_t no_speech_bias; /* ZT4: Q16 added to the no-speech logit; 0 default */
    bool ready;
    /* counters */
    uint32_t utterances, windows, silent, no_speech, failed, cut;
    char lang[8]; /* the language last heard */
} speech_zt_stt_t;

/* quiet_level SPEECH_ZT_STT_QUIET, everything else empty. */
void speech_zt_stt_defaults(speech_zt_stt_t *e);
/* Check the parts. SPEECH_ZT_STT_OK or EARG. */
int32_t speech_zt_stt_ready(speech_zt_stt_t *e);

/* speech_stt_ops_t.transcribe (ctx: a speech_zt_stt_t). 0 ok ("" when
 * nothing was said, ZT4), < 0 refused. */
int speech_zt_stt_transcribe(const int16_t *pcm, uint32_t n, const char *lang_hint, char *text,
                             uint32_t cap, char *lang_out, uint32_t lang_cap, void *ctx);
void speech_zt_stt_ops(speech_zt_stt_t *e, speech_stt_ops_t *ops);

/* ZT4: mean |sample| of n samples. */
uint32_t speech_zt_stt_level(const int16_t *pcm, uint32_t n);

extern const char speech_zt_stt_attribution[];

#endif /* ZXV_SPEECH_ZT_STT_H */
