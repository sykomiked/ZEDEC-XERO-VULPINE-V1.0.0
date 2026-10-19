/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_zt_tts.h — the on-device voice: OuteTTS on the integer engine,
 * behind speech_session's text-level speech-out boundary.
 *
 * tensor/zt_tts.h holds the parts (the prompt, the audio-code tokens, the
 * WavTokenizer decoder); this module joins them into one call that takes a
 * sentence and gives 16 kHz PCM, so the screen reader (zui_reader.h R5), the
 * Chiglet's voice (zui_chgvoice.h) and speech_session_say can all speak
 * through the same voice. Nothing leaves the machine.
 *
 *   ZS1 TEXT IN, 16 kHz OUT. speech_zt_tts_say builds the OuteTTS 0.3 prompt
 *       (zt_tts_prompt), empties the language model's cache and generates
 *       audio-code tokens (zt_model_generate, stopping at <|audio_end|> or
 *       <|endoftext|>), keeps the codes (zt_tts_codes), decodes them at
 *       24 kHz (zt_tts_decode), drops the lead llama.cpp zeroes
 *       (ZT_TTS_SKIP samples: silence, so the next sentence follows without a
 *       gap) and resamples to 16 kHz (speech_dsp.h, flushed to the end).
 *   ZS2 ENGLISH ONLY, AND IT SAYS SO. zt_tts_normalize keeps the letters a-z
 *       and nothing else, so a language other than "", "en" or "en-..." is
 *       refused (SPEECH_ZT_ENOLANG) rather than read out as silence or
 *       nonsense; the caller shows the words instead (zui_chgvoice.h CV8).
 *       Text with no letters left is refused too (SPEECH_ZT_ENOTEXT).
 *   ZS3 BOUNDED. The caller owns every buffer (speech_zt_tts_need says how
 *       big). A prompt too long for its buffer is refused; generation stops
 *       at max_gen tokens; PCM past the output buffer is cut and counted.
 *   ZS4 THE SAME SENTENCE SOUNDS THE SAME. Each utterance samples from the
 *       same seed (zt_sample, top-k 4, temperature 1 as llama.cpp), so a
 *       repeated sentence is the same audio, bit for bit, on every machine.
 *   ZS6 OUR OWN WORDS. Before the prompt is built, whole words are respelt
 *       the way they are said (case folded, a word being letters, digits, '-'
 *       and an apostrophe): the caller's lexicon first, then the built-in one
 *       (Chiglet "chig lett", ZXV "zee ex vee", V-Bill "vee bill", AI "ay eye"
 *       ...), because the model reads letters it has never seen as a word
 *       ("Chiglet" came back from Whisper as "Chibbutt", "chig lett" as
 *       "chiglet"). Text whose
 *       respelling would not fit SPEECH_ZT_SPELL is used as it is.
 *   ZS5 WEIGHTS ARE NEVER IN GIT. OuteTTS-0.3-500M (CC-BY-SA-4.0) and
 *       WavTokenizer-Large-75 (MIT) are fetched and checked by SHA-256 with
 *       bench/tensor/fetch_pinned.py. Ship speech_zt_tts_attribution with the
 *       voice. Never OuteTTS-0.3-1B: its licence forbids commercial use.
 *
 * The codes step can be replaced (codes_fn) by a host that has codes from
 * elsewhere, and by the tests, which have no 500M model to run.
 *
 * Freestanding: no libc, no allocation, no float. Single-threaded.
 */
#ifndef ZXV_SPEECH_ZT_TTS_H
#define ZXV_SPEECH_ZT_TTS_H

#include <stdbool.h>
#include <stdint.h>
#include "speech_dsp.h"
#include "speech_session.h"
#include "zt_model.h"
#include "zt_tts.h"

typedef enum {
    SPEECH_ZT_OK = 0,
    SPEECH_ZT_EARG = -1,    /* NULL, or not ready                               */
    SPEECH_ZT_ENOLANG = -2, /* ZS2: no voice for that language                  */
    SPEECH_ZT_ENOTEXT = -3, /* ZS2: nothing left to say                         */
    SPEECH_ZT_ESPACE = -4,  /* ZS3: the prompt does not fit its buffer          */
    SPEECH_ZT_EMODEL = -5,  /* generation or decoding failed                    */
    SPEECH_ZT_ENOCODES = -6 /* the model made no audio                          */
} speech_zt_result_t;

#define SPEECH_ZT_SPELL 2048u /* ZS6: bytes of respelt text, with the NUL */

/* ZS6: one respelling; word is lower case. */
typedef struct {
    const char *word, *say;
} speech_zt_word_t;

extern const speech_zt_word_t speech_zt_lexicon[]; /* ends with {0, 0} */

/* Returns 0 and the code count in *n, or < 0. */
typedef int32_t (*speech_zt_codes_fn)(void *ctx, const uint8_t *prompt, uint64_t prompt_len,
                                      int32_t *codes, uint32_t cap, uint32_t *n);

typedef struct {
    /* the models, loaded by the caller (codes_fn set: lm, st and tok may be NULL) */
    const zt_model_t *lm;
    zt_model_state_t *st;
    const zt_tok_t *tok;
    const zt_tts_dec_t *dec;
    /* buffers (speech_zt_tts_need) */
    uint8_t *prompt;
    uint32_t prompt_cap;
    int32_t *ids; /* max_gen tokens; the codes are kept in place */
    uint32_t max_gen;
    void *gen_work;
    uint64_t gen_work_bytes;
    void *dec_work;
    uint64_t dec_work_bytes; /* zt_tts_work_bytes(dec, max_gen) */
    int16_t *pcm24;
    uint32_t pcm24_cap; /* ZT_TTS_HOP * max_gen */
    /* sampling: ZS4 */
    uint64_t seed;
    uint32_t top_k;
    zt_fx temperature; /* Q16 */
    /* replaces the language model (tests, or codes from elsewhere) */
    speech_zt_codes_fn codes_fn;
    void *codes_ctx;
    /* ZS6: the caller's words, tried before the built-in ones ({0, 0} ends it) */
    const speech_zt_word_t *lexicon;
    char spelled[SPEECH_ZT_SPELL];
    /* found by speech_zt_tts_ready */
    zt_tts_vocab_t voc;
    bool ready;
    /* counters */
    uint32_t utterances, refused, failed, cut;
    uint32_t last_codes, last_clip, last_samples;
    speech_resampler_t rs;
} speech_zt_tts_t;

/* Seed 42, top-k 4, temperature 1, as bench/tensor/zt_tts.c. */
void speech_zt_tts_defaults(speech_zt_tts_t *e);
/* Check the parts and buffers and find the code tokens. SPEECH_ZT_OK, or
 * EARG for a missing part, EMODEL when the vocabulary has no OuteTTS codes. */
int32_t speech_zt_tts_ready(speech_zt_tts_t *e);

/* The buffer sizes for a text of up to max_text bytes and max_gen tokens
 * (lm and tok may be NULL when a codes_fn will be used: gen_work is 0). */
typedef struct {
    uint32_t prompt_cap;
    uint64_t gen_work_bytes, dec_work_bytes;
    uint32_t pcm24_cap;
} speech_zt_need_t;
void speech_zt_tts_need(const zt_model_t *lm, const zt_tok_t *tok, const zt_tts_dec_t *dec,
                        uint32_t max_text, uint32_t max_gen, speech_zt_need_t *need);

/* ZS2 */
bool speech_zt_tts_has_lang(const char *bcp47);

/* ZS6: respell text into out (cap bytes); returns the length, or -1 when it
 * does not fit (out is then not to be used). */
int32_t speech_zt_respell(const speech_zt_word_t *extra, const char *text, char *out, uint32_t cap);

/* ZS1: text to 16 kHz PCM; writes at most cap samples, the count in *out_n. */
int32_t speech_zt_tts_say(speech_zt_tts_t *e, const char *text, const char *lang, int16_t *pcm16,
                          uint32_t cap, uint32_t *out_n);

/* speech_tts_ops_t.synthesize (ctx: a speech_zt_tts_t). */
int speech_zt_tts_synthesize(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                             uint32_t *out_len, void *ctx);
void speech_zt_tts_ops(speech_zt_tts_t *e, speech_tts_ops_t *ops);

/* ZS5 */
extern const char speech_zt_tts_attribution[];

#endif /* ZXV_SPEECH_ZT_TTS_H */
