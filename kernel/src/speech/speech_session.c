/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_session.c — the speech session state machine. See speech_session.h. */
#include "speech_session.h"

/* ---- bounded string helpers (no libc) ---- */
static uint32_t s_len(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

/* Copy with truncation; returns true if src fitted. */
static bool s_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return false;
    if (!src) src = "";
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] == '\0';
}

static void emit(speech_session_t *s, speech_event_type_t type, const char *text, const char *lang,
                 int32_t code, uint32_t flags, uint32_t samples)
{
    if (!s->cfg.on_event) return;
    speech_event_t ev;
    ev.type = type;
    ev.state = s->state;
    ev.text = text ? text : "";
    ev.lang = lang ? lang : "";
    ev.code = code;
    ev.flags = flags;
    ev.samples = samples;
    s->cfg.on_event(&ev, s->cfg.event_ctx);
}

static void set_state(speech_session_t *s, speech_state_t st)
{
    if (s->state == st) return;
    s->state = st;
    emit(s, SPEECH_EV_STATE, "", "", 0, 0, 0);
}

static int fail(speech_session_t *s, int code)
{
    s->errors++;
    emit(s, SPEECH_EV_ERROR, "", "", code, 0, 0);
    return code;
}

/* Where the session rests between utterances. */
static speech_state_t rest_state(const speech_session_t *s)
{
    return s->cfg.mode == SPEECH_MODE_HANDS_FREE ? SPEECH_ST_ARMED : SPEECH_ST_IDLE;
}

void speech_config_defaults(speech_config_t *cfg)
{
    if (!cfg) return;
    cfg->mode = SPEECH_MODE_PUSH_TO_TALK;
    cfg->use = SPEECH_USE_ASSISTANT;
    cfg->input_rate = SPEECH_SAMPLE_RATE;
    cfg->lang[0] = '\0';
    cfg->max_utterance = 30u * SPEECH_SAMPLE_RATE;
    cfg->barge_in = true;
    cfg->speak_replies = true;
    cfg->reply = 0;
    cfg->reply_ctx = 0;
    cfg->caption_filter = 0;
    cfg->caption_ctx = 0;
    cfg->on_event = 0;
    cfg->event_ctx = 0;
    cfg->audio_out = 0;
    cfg->audio_ctx = 0;
}

int speech_session_init(speech_session_t *s, const speech_config_t *cfg, int16_t *capture,
                        uint32_t capture_cap, int16_t *tts_pcm, uint32_t tts_cap)
{
    if (!s || !cfg || !capture || capture_cap < SPEECH_PREROLL) return SPEECH_ERR_ARG;
    if (cfg->mode != SPEECH_MODE_PUSH_TO_TALK && cfg->mode != SPEECH_MODE_HANDS_FREE)
        return SPEECH_ERR_ARG;
    if (cfg->use != SPEECH_USE_ASSISTANT && cfg->use != SPEECH_USE_DICTATION &&
        cfg->use != SPEECH_USE_CAPTIONS)
        return SPEECH_ERR_ARG;
    s->cfg = *cfg;
    s->cfg.lang[SPEECH_LANG_MAX - 1] = '\0';
    if (speech_resampler_init(&s->rs, cfg->input_rate) != 0) return SPEECH_ERR_ARG;
    if (s->cfg.max_utterance == 0 || s->cfg.max_utterance > capture_cap)
        s->cfg.max_utterance = capture_cap;
    speech_vad_init(&s->vad);
    s->state = SPEECH_ST_IDLE;
    s->stt.transcribe = 0;
    s->stt.ctx = 0;
    s->tts.synthesize = 0;
    s->tts.ctx = 0;
    s->voice = 0;
    s->capture = capture;
    s->capture_cap = capture_cap;
    s->captured = 0;
    s->tts_pcm = tts_pcm;
    s->tts_cap = tts_pcm ? tts_cap : 0;
    s->frame_fill = 0;
    s->pre_head = 0;
    s->pre_count = 0;
    s->utterances = 0;
    s->errors = 0;
    s->transcript[0] = s->reply[0] = s->caption[0] = s->heard_lang[0] = '\0';
    return SPEECH_OK;
}

void speech_session_bind_stt(speech_session_t *s, const speech_stt_ops_t *ops)
{
    if (!s) return;
    s->stt.transcribe = ops ? ops->transcribe : 0;
    s->stt.ctx = ops ? ops->ctx : 0;
}

void speech_session_bind_tts(speech_session_t *s, const speech_tts_ops_t *ops)
{
    if (!s) return;
    s->tts.synthesize = ops ? ops->synthesize : 0;
    s->tts.ctx = ops ? ops->ctx : 0;
}

void speech_session_bind_voice(speech_session_t *s, voice_t *v)
{
    if (s) s->voice = v;
}

/* ---- speaking ---- */
static int speak(speech_session_t *s, const char *text, const char *lang)
{
    uint32_t n = 0, flags = 0;
    if (!s->tts_pcm || s->tts_cap == 0) return fail(s, SPEECH_ERR_NO_ENGINE);
    if (s->tts.synthesize) {
        int rc = s->tts.synthesize(text, lang, s->tts_pcm, s->tts_cap, &n, s->tts.ctx);
        if (rc < 0) return fail(s, SPEECH_ERR_ENGINE);
        if (n > s->tts_cap) n = s->tts_cap; /* untrusted engine */
    } else if (s->voice && voice_has_tts(s->voice)) {
        voice_result_t vr = voice_speak(s->voice, text, s->tts_pcm, s->tts_cap, &n);
        if (vr != VOICE_OK) return fail(s, SPEECH_ERR_ENGINE);
        flags |= SPEECH_EVF_LOSSY;
    } else {
        return fail(s, SPEECH_ERR_NO_ENGINE);
    }
    if (n == 0) return fail(s, SPEECH_ERR_EMPTY);
    if (s->cfg.audio_out && s->cfg.audio_out(s->tts_pcm, n, s->cfg.audio_ctx) != 0)
        return fail(s, SPEECH_ERR_ENGINE);
    set_state(s, SPEECH_ST_SPEAKING);
    emit(s, SPEECH_EV_SPOKEN, text, lang, 0, flags, n);
    return SPEECH_OK;
}

/* ---- the assistant turn: reply hook, then (optionally) speak ---- */
static int respond(speech_session_t *s, const char *text, const char *lang)
{
    if (!s->cfg.reply) return fail(s, SPEECH_ERR_NO_REPLY);
    s->reply[0] = '\0';
    int rc = s->cfg.reply(text, lang, s->reply, SPEECH_TEXT_MAX, s->cfg.reply_ctx);
    s->reply[SPEECH_TEXT_MAX - 1] = '\0';
    if (rc < 0 || s->reply[0] == '\0') return fail(s, SPEECH_ERR_NO_REPLY);
    emit(s, SPEECH_EV_REPLY, s->reply, lang, 0, 0, 0);
    if (!s->cfg.speak_replies) return SPEECH_OK;
    return speak(s, s->reply, lang);
}

/* ---- the end of an utterance: STT, then the use-specific step ---- */
static int finish_utterance(speech_session_t *s, uint32_t flags)
{
    uint32_t n = s->captured;
    s->captured = 0;
    s->utterances++;
    set_state(s, SPEECH_ST_THINKING);
    emit(s, SPEECH_EV_UTTERANCE_END, "", "", 0, flags, n);
    int rc = SPEECH_OK;
    const char *lang = s->cfg.lang;
    s->transcript[0] = '\0';
    s->heard_lang[0] = '\0';
    if (n == 0) {
        rc = fail(s, SPEECH_ERR_EMPTY);
    } else if (s->stt.transcribe) {
        int e = s->stt.transcribe(s->capture, n, s->cfg.lang, s->transcript, SPEECH_TEXT_MAX,
                                  s->heard_lang, SPEECH_LANG_MAX, s->stt.ctx);
        s->transcript[SPEECH_TEXT_MAX - 1] = '\0';
        s->heard_lang[SPEECH_LANG_MAX - 1] = '\0';
        if (e < 0) rc = fail(s, SPEECH_ERR_ENGINE);
    } else if (s->voice && voice_has_stt(s->voice)) {
        voice_result_t vr = voice_listen(s->voice, s->capture, n, s->transcript, SPEECH_TEXT_MAX);
        if (vr == VOICE_ERR_TRUNC)
            flags |= SPEECH_EVF_TRUNCATED;
        else if (vr != VOICE_OK)
            rc = fail(s, SPEECH_ERR_ENGINE);
        flags |= SPEECH_EVF_LOSSY;
    } else {
        rc = fail(s, SPEECH_ERR_NO_ENGINE);
    }
    if (rc == SPEECH_OK && s->transcript[0] == '\0') rc = fail(s, SPEECH_ERR_EMPTY);
    if (rc == SPEECH_OK) {
        if (s->heard_lang[0]) lang = s->heard_lang;
        emit(s, SPEECH_EV_TRANSCRIPT, s->transcript, lang, 0, flags, n);
        if (s->cfg.use == SPEECH_USE_CAPTIONS) {
            uint32_t cf = flags;
            const char *show = s->transcript;
            if (s->cfg.caption_filter) {
                s->caption[0] = '\0';
                int f = s->cfg.caption_filter(s->transcript, lang, s->caption, SPEECH_TEXT_MAX,
                                              s->cfg.caption_ctx);
                s->caption[SPEECH_TEXT_MAX - 1] = '\0';
                if (f > 0 && s->caption[0]) {
                    show = s->caption;
                    cf |= SPEECH_EVF_MACHINE_TRANSLATED;
                } else if (f == 0 && s->caption[0]) {
                    show = s->caption;
                }
                /* f < 0: the filter failed; caption the original, flagged by
                 * the absence of SPEECH_EVF_MACHINE_TRANSLATED. */
            }
            emit(s, SPEECH_EV_CAPTION, show, lang, 0, cf, n);
        } else if (s->cfg.use == SPEECH_USE_ASSISTANT) {
            rc = respond(s, s->transcript, lang);
        }
    }
    if (s->state != SPEECH_ST_SPEAKING) set_state(s, rest_state(s));
    return rc;
}

int speech_session_ptt_down(speech_session_t *s)
{
    if (!s) return SPEECH_ERR_ARG;
    if (s->cfg.mode != SPEECH_MODE_PUSH_TO_TALK) return SPEECH_ERR_STATE;
    if (s->state == SPEECH_ST_LISTENING) return SPEECH_OK;
    if (s->state == SPEECH_ST_SPEAKING) emit(s, SPEECH_EV_BARGE_IN, "", "", 0, 0, 0);
    s->captured = 0;
    set_state(s, SPEECH_ST_LISTENING);
    emit(s, SPEECH_EV_UTTERANCE_START, "", "", 0, 0, 0);
    return SPEECH_OK;
}

int speech_session_ptt_up(speech_session_t *s)
{
    if (!s) return SPEECH_ERR_ARG;
    if (s->cfg.mode != SPEECH_MODE_PUSH_TO_TALK || s->state != SPEECH_ST_LISTENING)
        return SPEECH_ERR_STATE;
    return finish_utterance(s, 0);
}

int speech_session_arm(speech_session_t *s)
{
    if (!s) return SPEECH_ERR_ARG;
    if (s->cfg.mode != SPEECH_MODE_HANDS_FREE) return SPEECH_ERR_STATE;
    if (s->state != SPEECH_ST_IDLE) return SPEECH_OK;
    speech_vad_init(&s->vad);
    s->frame_fill = 0;
    s->pre_count = 0;
    s->pre_head = 0;
    set_state(s, SPEECH_ST_ARMED);
    return SPEECH_OK;
}

int speech_session_stop(speech_session_t *s)
{
    if (!s) return SPEECH_ERR_ARG;
    s->captured = 0;
    set_state(s, SPEECH_ST_IDLE);
    return SPEECH_OK;
}

void speech_session_playback_done(speech_session_t *s)
{
    if (s && s->state == SPEECH_ST_SPEAKING) set_state(s, rest_state(s));
}

static void preroll_push(speech_session_t *s, const int16_t *x, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        s->preroll[s->pre_head] = x[i];
        s->pre_head = (s->pre_head + 1) % SPEECH_PREROLL;
        if (s->pre_count < SPEECH_PREROLL) s->pre_count++;
    }
}

/* Append to the capture; returns true when the length limit is reached. */
static bool capture_push(speech_session_t *s, const int16_t *x, uint32_t n)
{
    for (uint32_t i = 0; i < n && s->captured < s->cfg.max_utterance; i++)
        s->capture[s->captured++] = x[i];
    return s->captured >= s->cfg.max_utterance;
}

static void start_from_preroll(speech_session_t *s)
{
    uint32_t start = (s->pre_head + SPEECH_PREROLL - s->pre_count) % SPEECH_PREROLL;
    s->captured = 0;
    for (uint32_t i = 0; i < s->pre_count && s->captured < s->cfg.max_utterance; i++)
        s->capture[s->captured++] = s->preroll[(start + i) % SPEECH_PREROLL];
}

/* One 10 ms frame at 16 kHz, hands-free. */
static int hands_free_frame(speech_session_t *s, const int16_t *fr)
{
    preroll_push(s, fr, SPEECH_HOP);
    speech_vad_event_t e = speech_vad_frame(&s->vad, fr, SPEECH_HOP);
    if (s->state == SPEECH_ST_ARMED || (s->state == SPEECH_ST_SPEAKING && s->cfg.barge_in)) {
        if (e == SPEECH_VAD_START) {
            if (s->state == SPEECH_ST_SPEAKING) emit(s, SPEECH_EV_BARGE_IN, "", "", 0, 0, 0);
            start_from_preroll(s);
            set_state(s, SPEECH_ST_LISTENING);
            emit(s, SPEECH_EV_UTTERANCE_START, "", "", 0, 0, 0);
        }
        return SPEECH_OK;
    }
    if (s->state != SPEECH_ST_LISTENING) return SPEECH_OK;
    bool full = capture_push(s, fr, SPEECH_HOP);
    if (e == SPEECH_VAD_END) return finish_utterance(s, 0);
    if (full) {
        /* Restart the VAD so the rest of a long monologue starts afresh. */
        speech_vad_init(&s->vad);
        return finish_utterance(s, SPEECH_EVF_TRUNCATED);
    }
    return SPEECH_OK;
}

static int process16k(speech_session_t *s, const int16_t *x, uint32_t n)
{
    int rc = SPEECH_OK;
    if (s->cfg.mode == SPEECH_MODE_PUSH_TO_TALK) {
        if (s->state != SPEECH_ST_LISTENING) return SPEECH_OK;
        if (capture_push(s, x, n)) rc = finish_utterance(s, SPEECH_EVF_TRUNCATED);
        return rc;
    }
    if (s->state == SPEECH_ST_IDLE) return SPEECH_OK;
    for (uint32_t i = 0; i < n; i++) {
        s->frame[s->frame_fill++] = x[i];
        if (s->frame_fill == SPEECH_HOP) {
            s->frame_fill = 0;
            int r = hands_free_frame(s, s->frame);
            if (r != SPEECH_OK) rc = r;
        }
    }
    return rc;
}

int speech_session_feed(speech_session_t *s, const int16_t *pcm, uint32_t n)
{
    if (!s || (!pcm && n)) return SPEECH_ERR_ARG;
    int16_t block[256];
    uint32_t off = 0;
    int rc = SPEECH_OK;
    while (off < n) {
        uint32_t used = 0;
        uint32_t got = speech_resample(&s->rs, pcm + off, n - off, &used, block, 256);
        off += used;
        if (got) {
            int r = process16k(s, block, got);
            if (r != SPEECH_OK) rc = r;
        }
        if (used == 0 && got == 0) break; /* cannot happen; never spin */
    }
    return rc;
}

int speech_session_say(speech_session_t *s, const char *text, const char *lang)
{
    if (!s || !text) return SPEECH_ERR_ARG;
    if (text[0] == '\0') return SPEECH_ERR_EMPTY;
    if (s->state == SPEECH_ST_LISTENING || s->state == SPEECH_ST_THINKING) return SPEECH_ERR_STATE;
    return speak(s, text, lang ? lang : s->cfg.lang);
}

int speech_session_submit_text(speech_session_t *s, const char *text)
{
    if (!s || !text) return SPEECH_ERR_ARG;
    if (s_len(text, SPEECH_TEXT_MAX) == 0) return SPEECH_ERR_EMPTY;
    if (s->state == SPEECH_ST_LISTENING || s->state == SPEECH_ST_THINKING) return SPEECH_ERR_STATE;
    uint32_t flags = s_copy(s->transcript, SPEECH_TEXT_MAX, text) ? 0 : SPEECH_EVF_TRUNCATED;
    emit(s, SPEECH_EV_TRANSCRIPT, s->transcript, s->cfg.lang, 0, flags, 0);
    int rc = respond(s, s->transcript, s->cfg.lang);
    if (s->state != SPEECH_ST_SPEAKING) set_state(s, rest_state(s));
    return rc;
}

speech_state_t speech_session_state(const speech_session_t *s)
{
    return s ? s->state : SPEECH_ST_IDLE;
}

const char *speech_state_name(speech_state_t st)
{
    switch (st) {
    case SPEECH_ST_IDLE:
        return "idle";
    case SPEECH_ST_ARMED:
        return "armed";
    case SPEECH_ST_LISTENING:
        return "listening";
    case SPEECH_ST_THINKING:
        return "thinking";
    case SPEECH_ST_SPEAKING:
        return "speaking";
    }
    return "?";
}

const char *speech_result_name(int r)
{
    switch (r) {
    case SPEECH_OK:
        return "SPEECH_OK";
    case SPEECH_ERR_ARG:
        return "SPEECH_ERR_ARG";
    case SPEECH_ERR_NO_ENGINE:
        return "SPEECH_ERR_NO_ENGINE";
    case SPEECH_ERR_ENGINE:
        return "SPEECH_ERR_ENGINE";
    case SPEECH_ERR_STATE:
        return "SPEECH_ERR_STATE";
    case SPEECH_ERR_NO_REPLY:
        return "SPEECH_ERR_NO_REPLY";
    case SPEECH_ERR_EMPTY:
        return "SPEECH_ERR_EMPTY";
    }
    return "SPEECH_ERR_?";
}
