/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_hear.h — one place where what the person says goes: to their
 * Chiglet, to voice control, or into the text field as dictation.
 *
 * The speech session (speech/speech_session.h) records while the person
 * holds the talk key, or while they armed hands-free listening, and its
 * recognizer (speech/speech_zt_stt.h: Whisper on the device) gives text. This
 * module decides who gets it.
 *
 *   HE1 WHO HEARS IT. Dictation ON (zui_voicectl.h VC6): voice control, so
 *       everything but "stop dictation" is typed. Otherwise a phrase that
 *       starts with the Chiglet's name ("Chiglet, read this", "hey chiglet
 *       how are you") goes to the Chiglet alone, name removed; then, unless a
 *       text field has focus (so "how are you" can be dictated into a
 *       message), the Chiglet's own phrases (zui_cv_hear); then voice control (commands,
 *       and in AUTO dictation, text for the focused field). Nothing else
 *       acts on speech.
 *   HE2 TALKING OVER IT STOPS IT. When the person starts to speak while the
 *       Chiglet is talking, the Chiglet stops (barge-in) so it never talks
 *       over them, and while the person speaks the Chiglet waits
 *       (zui_cv_set_busy).
 *   HE3 THE MIC IS SHOWN. listening is true from the start of an utterance
 *       to its transcript (or error), for the status bar's mic mark; heard
 *       keeps the last words, for a caption of what was understood.
 *   HE4 NOTHING SAID, NOTHING DONE. An empty transcript, or one the
 *       recognizer flags lossy, is not acted on.
 *
 * Integer only, no allocation. Single-threaded: call it on the UI thread.
 */
#ifndef ZXV_ZUI_HEAR_H
#define ZXV_ZUI_HEAR_H

#include <stdbool.h>
#include <stdint.h>
#include "speech_session.h"
#include "zui_chgvoice.h"
#include "zui_voicectl.h"

#define ZUI_HEAR_TEXT 256u

typedef enum {
    ZUI_HEAR_CHIGLET = 0, /* the Chiglet acted on it            */
    ZUI_HEAR_CONTROL,     /* voice control acted (or typed it)  */
    ZUI_HEAR_DICTATED,    /* typed into the focused field       */
    ZUI_HEAR_UNKNOWN,     /* nobody had a use for it            */
    ZUI_HEAR_IGNORED      /* HE4: empty or lossy                */
} zui_hear_result_t;

typedef struct {
    zui_cv_t *cv;       /* may be NULL: no Chiglet  */
    zui_voicectl_t *vc; /* may be NULL: no control  */
    /* the facts for a status the person asks for (may be NULL) */
    void (*facts)(zui_cv_facts_t *f, void *ctx);
    void *facts_ctx;
    bool listening; /* HE3 */
    char heard[ZUI_HEAR_TEXT];
    uint32_t now; /* ms, from zui_hear_tick */
    uint32_t to_chiglet, to_control, dictated, unknown, ignored, barge_ins;
    zui_hear_result_t last;
} zui_hear_t;

void zui_hear_init(zui_hear_t *h, zui_cv_t *cv, zui_voicectl_t *vc);
void zui_hear_set_facts(zui_hear_t *h, void (*fn)(zui_cv_facts_t *f, void *ctx), void *ctx);
void zui_hear_tick(zui_hear_t *h, uint32_t now_ms);

/* HE1: route words the person said. */
zui_hear_result_t zui_hear_text(zui_hear_t *h, const char *utf8, uint32_t now_ms);
/* HE2, HE3: the person started speaking / stopped (no transcript). */
void zui_hear_start(zui_hear_t *h, uint32_t now_ms);
void zui_hear_end(zui_hear_t *h, uint32_t now_ms);

/* A speech_event_fn: pass as speech_config_t.on_event with a zui_hear_t. */
void zui_hear_on_event(const speech_event_t *ev, void *hear);

#endif /* ZXV_ZUI_HEAR_H */
