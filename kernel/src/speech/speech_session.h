/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_session.h — voice conversations, dictation, live captions and
 *                    read-aloud, driven through bound speech engines.
 *
 * One session turns a microphone stream into text and text into speech:
 *
 *   mic PCM (any rate 8-96 kHz)
 *     -> resample to 16 kHz -> [hands-free: VAD + 300 ms pre-roll]
 *     -> capture an utterance -> STT engine -> transcript
 *     -> use ASSISTANT:  reply hook (the chat model) -> TTS engine -> speaker
 *        use DICTATION:  transcript event (the app inserts the text)
 *        use CAPTIONS:   caption filter (e.g. xlate: translate) -> caption
 *
 * Two ways to start an utterance:
 *   PUSH_TO_TALK  speech_session_ptt_down() ... speech_session_ptt_up()
 *   HANDS_FREE    speech_session_arm(); the VAD finds start and end itself
 *
 * ENGINES. The session prefers TEXT-level engines (speech_stt_ops_t /
 * speech_tts_ops_t: Whisper, the macOS Speech framework, AVSpeechSynthesizer,
 * Piper, Kokoro ...). If none is bound it falls back to a bound voice_t and
 * voice.h's phoneme boundary (voice_listen / voice_speak); text that came
 * that way is flagged SPEECH_EVF_LOSSY because voice.h renders phonemes, not
 * words. With neither bound the session FAILS CLOSED: an ERROR event with
 * SPEECH_ERR_NO_ENGINE, no transcript, no audio. It never invents either.
 *
 * Single-threaded and synchronous: engines and hooks run inside the call
 * that triggered them. Time is counted in samples; there is no clock.
 * Freestanding: no libc, no allocation, no float. The caller owns the
 * capture and TTS buffers.
 */
#ifndef ZXV_SPEECH_SESSION_H
#define ZXV_SPEECH_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include "speech_dsp.h"
#include "voice.h"

#define SPEECH_TEXT_MAX 1024u
#define SPEECH_LANG_MAX 16u
#define SPEECH_PREROLL  4800u /* 300 ms at 16 kHz, kept ahead of a VAD start */

typedef enum {
    SPEECH_OK = 0,
    SPEECH_ERR_ARG = -1,
    SPEECH_ERR_NO_ENGINE = -2, /* no STT / TTS bound: fail closed            */
    SPEECH_ERR_ENGINE = -3,    /* the bound engine reported failure          */
    SPEECH_ERR_STATE = -4,     /* call not valid in the current state        */
    SPEECH_ERR_NO_REPLY = -5,  /* no reply hook, or it had nothing to say    */
    SPEECH_ERR_EMPTY = -6      /* nothing was captured / recognised          */
} speech_result_t;

typedef enum { SPEECH_MODE_PUSH_TO_TALK = 0, SPEECH_MODE_HANDS_FREE = 1 } speech_mode_t;

typedef enum {
    SPEECH_USE_ASSISTANT = 0, /* talk to the system; replies are spoken     */
    SPEECH_USE_DICTATION = 1, /* voice typing into the focused text field   */
    SPEECH_USE_CAPTIONS = 2   /* live captions (calls, video), translatable */
} speech_use_t;

typedef enum {
    SPEECH_ST_IDLE = 0,      /* not listening                               */
    SPEECH_ST_ARMED = 1,     /* hands-free: waiting for speech              */
    SPEECH_ST_LISTENING = 2, /* capturing an utterance                      */
    SPEECH_ST_THINKING = 3,  /* STT / reply running (inside a call)         */
    SPEECH_ST_SPEAKING = 4   /* reply audio handed to the speaker           */
} speech_state_t;

typedef enum {
    SPEECH_EV_STATE = 0,
    SPEECH_EV_UTTERANCE_START = 1,
    SPEECH_EV_UTTERANCE_END = 2, /* samples = captured length            */
    SPEECH_EV_TRANSCRIPT = 3,    /* text = what was heard, lang = its tag */
    SPEECH_EV_CAPTION = 4,       /* text = caption to show                */
    SPEECH_EV_REPLY = 5,         /* text = the assistant's reply          */
    SPEECH_EV_SPOKEN = 6,        /* samples = PCM handed to the speaker   */
    SPEECH_EV_BARGE_IN = 7,      /* user spoke over playback: stop audio  */
    SPEECH_EV_ERROR = 8          /* code = speech_result_t                */
} speech_event_type_t;

#define SPEECH_EVF_LOSSY              1u /* via voice.h phonemes, not words  */
#define SPEECH_EVF_MACHINE_TRANSLATED 2u /* caption was machine translated   */
#define SPEECH_EVF_TRUNCATED          4u /* hit the capture or text limit    */

typedef struct {
    speech_event_type_t type;
    speech_state_t state; /* state after the event                        */
    const char *text;     /* never NULL ("" when there is no text)        */
    const char *lang;     /* BCP 47 tag or ""                             */
    int32_t code;
    uint32_t flags;
    uint32_t samples;
} speech_event_t;

/* ===== Text-level engine boundaries (preferred over voice.h phonemes) =====
 * Both return 0 on success, negative on failure. pcm is 16 kHz mono S16LE.
 * transcribe: lang_hint may be "" (auto); writes a NUL-terminated transcript
 * and, if it knows, the spoken language tag into lang_out. */
typedef struct {
    int (*transcribe)(const int16_t *pcm, uint32_t n, const char *lang_hint, char *text,
                      uint32_t cap, char *lang_out, uint32_t lang_cap, void *ctx);
    void *ctx;
} speech_stt_ops_t;

typedef struct {
    int (*synthesize)(const char *text, const char *lang, int16_t *pcm, uint32_t cap,
                      uint32_t *out_len, void *ctx);
    void *ctx;
} speech_tts_ops_t;

/* ===== App hooks ===== */
/* The conversational model: user text in, reply out. 0 ok, negative refuse. */
typedef int (*speech_reply_fn)(const char *user_text, const char *lang, char *reply, uint32_t cap,
                               void *ctx);
/* Caption filter: returns <0 on error, 0 when out is the input unchanged, 1
 * when out is a machine translation. xlate_caption_filter() matches this. */
typedef int (*speech_text_filter_fn)(const char *in, const char *src_lang, char *out, uint32_t cap,
                                     void *ctx);
typedef void (*speech_event_fn)(const speech_event_t *ev, void *ctx);
/* The speaker: e.g. a wrapper around audio_write(). 0 ok. */
typedef int (*speech_audio_out_fn)(const int16_t *pcm, uint32_t n, void *ctx);

typedef struct {
    speech_mode_t mode;
    speech_use_t use;
    uint32_t input_rate;        /* microphone rate, 8000..96000        */
    char lang[SPEECH_LANG_MAX]; /* spoken-language hint, "" = auto     */
    uint32_t max_utterance;     /* samples at 16 kHz (<= capture cap)  */
    bool barge_in;              /* hands-free: speech stops playback   */
    bool speak_replies;         /* ASSISTANT: read replies aloud       */
    speech_reply_fn reply;
    void *reply_ctx;
    speech_text_filter_fn caption_filter;
    void *caption_ctx;
    speech_event_fn on_event;
    void *event_ctx;
    speech_audio_out_fn audio_out;
    void *audio_ctx;
} speech_config_t;

typedef struct {
    speech_config_t cfg;
    speech_state_t state;
    speech_stt_ops_t stt;
    speech_tts_ops_t tts;
    voice_t *voice; /* optional voice.h fallback */
    speech_resampler_t rs;
    speech_vad_t vad;
    int16_t *capture;
    uint32_t capture_cap;
    uint32_t captured;
    int16_t *tts_pcm;
    uint32_t tts_cap;
    int16_t frame[SPEECH_HOP];
    uint32_t frame_fill;
    int16_t preroll[SPEECH_PREROLL];
    uint32_t pre_head;
    uint32_t pre_count;
    uint32_t utterances;
    uint32_t errors;
    char transcript[SPEECH_TEXT_MAX];
    char reply[SPEECH_TEXT_MAX];
    char caption[SPEECH_TEXT_MAX];
    char heard_lang[SPEECH_LANG_MAX];
} speech_session_t;

/* PUSH_TO_TALK, ASSISTANT, 16 kHz, auto language, 30 s, barge-in on, speak
 * replies on, no hooks. */
void speech_config_defaults(speech_config_t *cfg);

/* capture: caller buffer for one utterance at 16 kHz; tts_pcm: caller buffer
 * for one synthesized reply at 16 kHz (may be NULL if nothing is ever
 * spoken). Returns SPEECH_OK or SPEECH_ERR_ARG. */
int speech_session_init(speech_session_t *s, const speech_config_t *cfg, int16_t *capture,
                        uint32_t capture_cap, int16_t *tts_pcm, uint32_t tts_cap);

/* Bind / unbind (NULL) engines. The ops are copied. */
void speech_session_bind_stt(speech_session_t *s, const speech_stt_ops_t *ops);
void speech_session_bind_tts(speech_session_t *s, const speech_tts_ops_t *ops);
void speech_session_bind_voice(speech_session_t *s, voice_t *v);

/* Push-to-talk. ptt_down while SPEAKING is a barge-in. ptt_up runs STT and
 * the use-specific step inside the call. */
int speech_session_ptt_down(speech_session_t *s);
int speech_session_ptt_up(speech_session_t *s);

/* Hands-free: start / stop listening for speech. */
int speech_session_arm(speech_session_t *s);
int speech_session_stop(speech_session_t *s);

/* Feed microphone samples at cfg.input_rate. Utterances that end inside this
 * call (VAD end, or the length limit) are processed before it returns. */
int speech_session_feed(speech_session_t *s, const int16_t *pcm, uint32_t n);

/* The speaker finished playing the last reply. */
void speech_session_playback_done(speech_session_t *s);

/* Read text aloud (read-aloud / accessibility). lang NULL = cfg.lang. */
int speech_session_say(speech_session_t *s, const char *text, const char *lang);

/* A typed prompt into the same conversation as voice (ASSISTANT): runs the
 * reply hook and, if speak_replies, speaks the reply. */
int speech_session_submit_text(speech_session_t *s, const char *text);

speech_state_t speech_session_state(const speech_session_t *s);
const char *speech_state_name(speech_state_t st);
const char *speech_result_name(int r);

#endif /* ZXV_SPEECH_SESSION_H */
