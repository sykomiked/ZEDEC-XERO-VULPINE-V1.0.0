/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* voice.h — ZXV native voice: the deterministic text<->phoneme bridge for
 *           Chiglet speech, with fail-closed boundaries to the real models.
 *
 * FLAVOR
 * ------
 *   The Chiglet finds its voice; we spell the sounds, the engine sings them.
 *
 * WHAT THIS IS (and honestly is NOT)
 * ----------------------------------
 * This module owns exactly one REAL, deterministic, on-device thing: a
 * grapheme->phoneme decomposition. Give it "cat" and it always, on every run
 * and every target, produces the phoneme token sequence {PH_K, PH_AE, PH_T}.
 * Lowercasing, a handful of English digraph rules (sh/ch/th/ng/ph/ck), and a
 * per-letter default table. Integer only, bounded arrays, length-checked. No
 * clock, no allocation, no libc, no float. It is asserted against known answers
 * in test_voice.c.
 *
 * What this module does NOT do, and never pretends to: it does not synthesize
 * audio and it does not recognize speech. Those are NEURAL MODELS. They live
 * behind ops boundaries — function pointers a caller binds (voice_set_tts /
 * voice_set_stt), exactly like the hardware boundary in audio.h (audio_ops_t)
 * and the model boundary in chiglet.h. PCM here is the same S16LE the audio
 * mixer speaks (int16_t samples), so a bound TTS can hand its output straight
 * to audio_write().
 *
 * THE BOUNDARY IS FAIL-CLOSED. With no engine bound, voice_speak() returns
 * VOICE_ERR_NO_ENGINE and writes NOT ONE PCM sample; voice_listen() returns
 * VOICE_ERR_NO_ENGINE and writes an empty string. It never fabricates audio and
 * never fabricates text. Silence and a typed error are the honest answers when
 * the model is absent.
 */
#ifndef ZXV_VOICE_H
#define ZXV_VOICE_H

#include <stdint.h>
#include <stdbool.h>
#include "chiglet.h"   /* chiglet_t + chg_infer, for voice_chiglet_say */

/* ===== Return codes. 0 = ok; negative = a typed refusal, never a fake. ===== */
typedef enum {
    VOICE_OK            =  0,
    VOICE_ERR_ARG       = -1,   /* NULL pointer / zero cap / bad argument     */
    VOICE_ERR_NO_ENGINE = -2,   /* no TTS/STT model bound — fail closed       */
    VOICE_ERR_ENGINE    = -3,   /* the bound engine itself reported failure   */
    VOICE_ERR_TRUNC     = -4,   /* output did not fit the caller's buffer     */
    VOICE_ERR_NO_REPLY  = -5    /* the Chiglet had nothing DECIDED to say      */
} voice_result_t;

/* ===== Phoneme token alphabet =====
 * A compact ARPABET-ish subset. Values are FIXED and load-bearing: the
 * known-answer tests pin exact numbers, so never renumber these. Digraphs get
 * their own single token (SH, CH, TH, NG). PH_SIL is a word boundary. */
typedef enum {
    PH_SIL = 0,   /* silence / word boundary */
    PH_AE,        /* 1  'a'  (cat)           */
    PH_B,         /* 2  'b'                  */
    PH_K,         /* 3  'c','k','ck'         */
    PH_D,         /* 4  'd'                  */
    PH_EH,        /* 5  'e'                  */
    PH_F,         /* 6  'f','ph'             */
    PH_G,         /* 7  'g'                  */
    PH_HH,        /* 8  'h'                  */
    PH_IH,        /* 9  'i'                  */
    PH_JH,        /* 10 'j'                  */
    PH_L,         /* 11 'l'                  */
    PH_M,         /* 12 'm'                  */
    PH_N,         /* 13 'n'                  */
    PH_OW,        /* 14 'o'                  */
    PH_P,         /* 15 'p'                  */
    PH_KW,        /* 16 'q'  -> /kw/         */
    PH_R,         /* 17 'r'                  */
    PH_S,         /* 18 's'                  */
    PH_T,         /* 19 't'                  */
    PH_AH,        /* 20 'u'                  */
    PH_V,         /* 21 'v'                  */
    PH_W,         /* 22 'w'                  */
    PH_KS,        /* 23 'x'  -> /ks/         */
    PH_Y,         /* 24 'y'                  */
    PH_Z,         /* 25 'z'                  */
    PH_SH,        /* 26 'sh'                 */
    PH_CH,        /* 27 'ch'                 */
    PH_TH,        /* 28 'th'                 */
    PH_NG,        /* 29 'ng'                 */
    PH__COUNT     /* 30                      */
} voice_phoneme_t;

/* Largest phoneme run any single call will materialize on the stack. */
#define VOICE_MAX_PHONEMES 256u

/* ===== OPS BOUNDARY: the real TTS model =====
 * A caller binds a synth that turns a phoneme run into S16LE PCM. Returns 0 on
 * success (and sets *out_len to samples written, <= cap), or a negative code on
 * failure. NULL synth => voice is mute-by-design (VOICE_ERR_NO_ENGINE). */
typedef struct {
    int (*synth)(const uint8_t *phonemes, uint32_t n,
                 int16_t *pcm, uint32_t cap, uint32_t *out_len, void *ctx);
    void *ctx;
} voice_tts_ops_t;

/* ===== OPS BOUNDARY: the real STT model =====
 * A caller binds a recognizer that turns PCM into a phoneme run. Returns 0 on
 * success (and sets *out_len to phonemes written, <= cap), or a negative code.
 * NULL recognize => VOICE_ERR_NO_ENGINE. */
typedef struct {
    int (*recognize)(const int16_t *pcm, uint32_t n,
                     uint8_t *phonemes, uint32_t cap, uint32_t *out_len, void *ctx);
    void *ctx;
} voice_stt_ops_t;

/* ===== The voice endpoint ===== */
typedef struct {
    voice_tts_ops_t tts;   /* .synth == NULL until voice_set_tts()      */
    voice_stt_ops_t stt;   /* .recognize == NULL until voice_set_stt()  */
    uint64_t spoken;       /* successful voice_speak() calls            */
    uint64_t heard;        /* successful voice_listen() calls           */
} voice_t;

/* Zeroes *v. NULL-safe (no-op on NULL). No engines bound afterward. */
void voice_init(voice_t *v);

/* Bind / rebind the engines. Passing NULL ops UNBINDS (returns to fail-closed).
 * The ops struct is copied by value; the caller need not keep it alive, but the
 * ctx pointer and callbacks it names must outlive the voice_t. */
void voice_set_tts(voice_t *v, const voice_tts_ops_t *ops);
void voice_set_stt(voice_t *v, const voice_stt_ops_t *ops);
bool voice_has_tts(const voice_t *v);
bool voice_has_stt(const voice_t *v);

/* ===== THE REAL ON-DEVICE CORE =====
 * Normalise `text` (lowercase, digraph rules, per-letter defaults, whitespace
 * collapsed to one PH_SIL) into at most `cap` phoneme tokens in `out`. Returns
 * the number of tokens written (<= cap). DETERMINISTIC: same input, same
 * output, every run, every target. Length-checked: it never writes past `cap`
 * and never reads past the terminating NUL. Returns 0 on NULL args or cap 0. */
uint32_t voice_text_to_phonemes(const char *text, uint8_t *out, uint32_t cap);

/* Canonical string a phoneme decodes back to (lossy — g2p is not injective, so
 * PH_K prints "k" even though it may have come from 'c'). Returns "" for out-of
 * -range tokens. Used by voice_listen to render an STT phoneme run as text. */
const char *voice_phoneme_str(uint8_t phoneme);

/* ===== Speak: on-device text->phonemes, then the BOUND TTS synth =====
 * Decomposes `text` here, then asks the bound engine to sing it into `pcm`
 * (capacity `cap` samples; *out_len set to samples produced). With NO engine
 * bound: returns VOICE_ERR_NO_ENGINE and writes NO PCM (out_len set to 0). */
voice_result_t voice_speak(voice_t *v, const char *text,
                           int16_t *pcm, uint32_t cap, uint32_t *out_len);

/* ===== Listen: the BOUND STT recognize, then phonemes->text on-device =====
 * Asks the bound engine to recognize `pcm` (n samples) into phonemes, then
 * renders them to `out_text` (capacity `cap` bytes incl. NUL). No engine bound:
 * VOICE_ERR_NO_ENGINE, out_text = "". VOICE_ERR_TRUNC if the text overflows. */
voice_result_t voice_listen(voice_t *v, const int16_t *pcm, uint32_t n,
                            char *out_text, uint32_t cap);

/* ===== Chiglet loop helper: speak whatever the Chiglet DECIDED =====
 * Runs inference over `ev` (k evidence vectors), and IF the Chiglet reaches a
 * DECIDED verdict, speaks that label's name through voice_speak(). If the
 * Chiglet is unavailable, denied, or UNCERTAIN, there is nothing honest to say:
 * returns VOICE_ERR_NO_REPLY and writes no PCM. (A missing TTS still surfaces as
 * VOICE_ERR_NO_ENGINE from the underlying voice_speak.) */
voice_result_t voice_chiglet_say(voice_t *v, chiglet_t *c,
                                 const surplus_real_t ev[][CHG_DIM], uint32_t k,
                                 int16_t *pcm, uint32_t cap, uint32_t *out_len);

/* Human-readable name of a result code (for logs). Never NULL. */
const char *voice_result_name(voice_result_t r);

#endif /* ZXV_VOICE_H */
