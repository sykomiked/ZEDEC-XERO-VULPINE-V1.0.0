/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_chgvoice.h — the Chiglet speaks, when it is asked to.
 *
 * A tap on the person's Chiglet makes it rise from its corner, show how it
 * feels and say what is going on; asking it to read a section reads that
 * section aloud. Every word it says is shown in its speech bubble while it
 * is said, so the same thing reaches people who cannot hear it. The voice is
 * whatever speaker the host binds (speech/speech_zt_tts.h: OuteTTS on the
 * integer engine, on the device); this module decides what is said, when,
 * and how the Chiglet looks while it says it.
 *
 *   CV1 ONLY WHEN ASKED. Speech starts from zui_cv_tap, zui_cv_hold,
 *       zui_cv_status, zui_cv_read and zui_cv_hear, and nothing else: there
 *       is no timer, event or notification path to the speaker, no wake word
 *       and no listening. Each entry is the person's own act: a tap, a key, or
 *       words the speech session heard on push-to-talk.
 *   CV2 A TAP. A tap while it is quiet gives the short status (CV4); a tap
 *       while it speaks stops it at once (a read remembers where, CV5). A
 *       hold (long press) gives the full status.
 *   CV3 SAY IT ONCE. A status with the same words as one said less than
 *       ZUI_CV_SAME_MS ago is not read again: the Chiglet says "Nothing has
 *       changed." zui_cv_repeat always says the last thing again.
 *   CV4 STATUS FROM FACTS. zui_cv_status_text builds the status from what
 *       the caller gives (zui_cv_facts_t) and the Chiglet's feeling and
 *       health, in this order: what needs the person, health when it is not
 *       OK, the feeling (and why, when a reason is given), failures, work
 *       running, work finished, the token budget. A fact that is not given is
 *       not said. The short form is the first two sentences. The words are a
 *       table (zui_cv_words_t), English built in, so the spoken-language
 *       tables can supply their own.
 *   CV5 ONE SENTENCE AT A TIME. Text is cut at sentence ends (. ! ? followed
 *       by a space or the end, the CJK 。！？, a new line) into chunks of at
 *       most ZUI_CV_CHUNK bytes (a longer sentence breaks at its last space,
 *       else between characters, never inside one). One chunk goes to the
 *       speaker at a time and the host calls zui_cv_spoken when it has played,
 *       which sends the next: the first words come after one sentence is
 *       made, not the whole section. A stopped read keeps the chunk it was in;
 *       zui_cv_resume starts that chunk again.
 *   CV6 VOICE OR WORDS. VOICE speaks and shows; WORDS (quiet) only shows,
 *       each chunk staying up for a time set by its length and the rate.
 *       Volume 0..100 and rate 50..200 % go to the speaker with each chunk.
 *   CV7 NEVER OVER ANOTHER VOICE. While the host says another voice is
 *       speaking (the screen reader, a call: zui_cv_set_busy), the next chunk
 *       waits. Nothing the Chiglet says interrupts anything else.
 *   CV8 NO BORROWED VOICE. When the speaker refuses a chunk (no engine, or
 *       no voice for the language: speak returns < 0), that chunk and the
 *       rest of the utterance are shown as words, and the bubble says so once.
 *       It never speaks one language in a voice made for another.
 *   CV9 THE POP-UP. HIDDEN, RISING, SHOWN, SETTLING. It rises over
 *       ZUI_CV_RISE_MS (the zui transition ceiling) with an ease, holds
 *       ZUI_CV_HOLD_MS after its last words, then settles back. Reduce motion
 *       (zui_motion.h) makes it appear in place with no travel, and the
 *       bubble fades instead of sliding. While it speaks
 *       the mouth follows the speech level (zui_cv_level), rising fast and
 *       falling over a few frames; with reduce motion the mouth stays half
 *       open instead of moving. Its feeling is zui_emote's morphing face
 *       beside it and the feeling's word heads the bubble. Nothing flashes:
 *       no part of it blinks or strobes.
 *   CV10 SAID LIKE A PERSON. What it reads (a section, a title, the facts in
 *       a status) goes through speech_tidy (speech/speech_tidy.h) first: a
 *       hash, an id or a code is named ("a hash", "3 IDs"), never spelled; a
 *       link is "a link to github.com"; a code block is "There is some code
 *       here."; markup is silent. The bubble shows the same words. "Spell
 *       that" (zui_cv_spell) says the last read again exactly, with hashes
 *       and codes spelled out in groups of four; zui_cv_set_verbatim turns
 *       the tidying off for a person who wants every character.
 *   CV11 FACE TO FACE. "Let's talk" (or zui_cv_call) opens a call window: the
 *       Chiglet large in its own panel with its name, its feeling, a mic
 *       mark, what the person said ("You: ...") and its own words as a
 *       caption, and an End button. Only the person opens it; it never opens
 *       itself. While it is open the host may listen hands-free (the call
 *       hook says when to arm and disarm speech_session), and the mic mark
 *       shows when it is listening. "Bye", "hang up", End, or two minutes
 *       with nothing said either way close it; closing on idle is silent.
 *       Another person's Chiglet visiting (zui_cv_visit) sits beside it in
 *       the window with its name and feeling; a visit is shown only in a
 *       call the person opened.
 *
 * Integer only, no allocation, no libc, deterministic: the same calls at the
 * same times give the same words and the same pixels. Time is the caller's
 * millisecond clock.
 */
#ifndef ZXV_ZUI_CHGVOICE_H
#define ZXV_ZUI_CHGVOICE_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_chiglet.h"
#include "zui_emote.h"
#include "speech_tidy.h"

#define ZUI_CV_TEXT    4096u /* bytes of one utterance (a status, a section), with the NUL */
#define ZUI_CV_CHUNK   240u  /* CV5: bytes of one spoken chunk                             */
#define ZUI_CV_SMALL   96u   /* bytes of a name, a reason, a title                         */
#define ZUI_CV_LANG    16u
#define ZUI_CV_SAME_MS 60000u      /* CV3 */
#define ZUI_CV_RISE_MS 180u        /* CV9: = ZUI_MOTION_MS                                       */
#define ZUI_CV_HOLD_MS 2500u       /* CV9 */
#define ZUI_CV_UNKNOWN 0xFFFFFFFFu /* a count or share that was not given                  */
#define ZUI_CV_CALL_IDLE_MS 120000u     /* CV11: a quiet call closes                          */

/* CV4: the facts a status is made from. Start from zui_cv_facts_init (every
 * count ZUI_CV_UNKNOWN, every string NULL) and fill in what is known; an
 * unknown fact is not said, and neither is a failed or finished count of 0. */
typedef struct {
    uint32_t need_you;     /* things waiting for the person                    */
    const char *need_what; /* the first of them, a few words ("the export")    */
    uint32_t running;      /* tasks working                                    */
    uint32_t finished;     /* tasks finished since the person last asked       */
    uint32_t failed;       /* tasks that failed                                */
    uint32_t budget_pct;   /* token budget left, 0..100, or ZUI_CV_UNKNOWN     */
    const char *why;       /* why it feels as it does ("the build is slow")    */
} zui_cv_facts_t;

/* CV4: the words. Each entry is a sentence pattern; %s and %u are filled in
 * order. one/many pairs are chosen by the count. */
typedef struct {
    const char *tag; /* "en" */
    const char *need_one, *need_one_what, *need_many, *need_many_what, *need_none;
    const char *slow, *failing, *stopped;
    const char *feel, *feel_very, *feel_why, *feel_very_why;
    const char *failed_one, *failed_many;
    const char *running_one, *running_many, *running_none;
    const char *finished_one, *finished_many;
    const char *budget;
    const char *same;      /* CV3 */
    const char *no_voice;  /* CV8: %s is the language */
    const char *not_found; /* zui_cv_hear: %s is the section asked for */
    const char *reading;   /* zui_cv_read with a title: %s */
    const char *nothing;   /* a read of empty text */
    const speech_tidy_words_t *say; /* CV10: NULL is English */
    const char *call_hello, *call_bye; /* CV11: "I am here.", "Bye for now." */
    const char *call_end, *call_you;   /* CV11: "End", "You: %s"          */
    const char *visiting;              /* CV11: "%s is visiting."          */
    const char *name;                  /* CV11: the name when none is set  */
} zui_cv_words_t;

extern const zui_cv_words_t zui_cv_words_en;

typedef enum { ZUI_CV_VOICE = 0, ZUI_CV_WORDS } zui_cv_mode_t;
typedef enum { ZUI_CV_HIDDEN = 0, ZUI_CV_RISING, ZUI_CV_SHOWN, ZUI_CV_SETTLING } zui_cv_phase_t;
typedef enum { ZUI_CV_K_NONE = 0, ZUI_CV_K_STATUS, ZUI_CV_K_READ, ZUI_CV_K_NOTE } zui_cv_kind_t;

typedef enum {
    ZUI_CV_SPEAKING = 0, /* it started saying something                      */
    ZUI_CV_STOPPED,      /* it was speaking and stopped                      */
    ZUI_CV_SAME,         /* CV3: "nothing has changed"                       */
    ZUI_CV_SET,          /* a setting changed (louder, slower ...)           */
    ZUI_CV_NOT_MINE,     /* zui_cv_hear: not a Chiglet phrase                */
    ZUI_CV_NOTHING,      /* nothing to resume, repeat or read                */
    ZUI_CV_BAD           /* a NULL or bad argument                           */
} zui_cv_result_t;

/* The speaker: start saying text (one chunk); 0 = it is playing (call
 * zui_cv_spoken when done), < 0 = refused (CV8). volume 0..100, rate 50..200. */
typedef int (*zui_cv_speak_fn)(const char *text, const char *lang, uint32_t volume,
                               uint32_t rate_pct, void *ctx);
typedef void (*zui_cv_stop_fn)(void *ctx);
/* zui_cv_hear: the text of a section. name "" is "this section" (what the
 * person has selected or is looking at). Writes the text and, optionally, a
 * title; returns the text's length, or < 0 when there is no such section. */
/* CV11: the call window opened (true) or closed: arm or disarm listening. */
typedef void (*zui_cv_call_fn)(bool open, void *ctx);
typedef enum {
    ZUI_CV_HIT_NONE = 0,
    ZUI_CV_HIT_BODY,
    ZUI_CV_HIT_WINDOW,
    ZUI_CV_HIT_END
} zui_cv_hit_t;
typedef int32_t (*zui_cv_section_fn)(const char *name, char *text, uint32_t cap, char *title,
                                     uint32_t title_cap, void *ctx);

typedef struct {
    zui_chiglet_t who;
    swarm_feeling_t feeling;
    zui_health_t health;
    zui_emote_t face;
    const zui_cv_words_t *words;
    char lang[ZUI_CV_LANG];
    zui_cv_mode_t mode;
    uint32_t volume, rate_pct;
    bool busy; /* CV7 */
    zui_cv_speak_fn speak;
    zui_cv_stop_fn stop;
    void *ctx;
    zui_cv_section_fn section;
    void *section_ctx;
    zui_rect_t anchor; /* where it rests; the pop-up rises from here */
    /* the utterance */
    zui_cv_kind_t kind;
    char text[ZUI_CV_TEXT];
    uint32_t len, pos;         /* pos: where the next chunk starts          */
    char exact[ZUI_CV_TEXT];   /* CV10: the last read as it was written     */
    uint32_t exact_len;
    bool verbatim;             /* CV10: read every character, untidied */
    uint32_t cur_start;        /* CV5: where the chunk being said started   */
    uint32_t resume_at;        /* a stopped read; ZUI_CV_UNKNOWN = none     */
    char chunk[ZUI_CV_CHUNK];  /* the words in the bubble now               */
    bool playing, waiting;     /* a chunk is out / one is ready but busy    */
    bool words_only, noted;    /* CV8                                       */
    bool in_speak, done_early; /* zui_cv_spoken from inside speak           */
    uint32_t words_until;      /* CV6: WORDS: when this chunk is done       */
    char note[ZUI_CV_SMALL];   /* CV8's line, shown above the words         */
    /* CV3 */
    char last_status[ZUI_CV_TEXT];
    uint32_t last_status_ms;
    bool have_status;
    /* CV9 */
    zui_cv_phase_t phase;
    uint32_t phase_ms, hold_until, now;
    uint32_t level; /* smoothed mouth level 0..256 */
    /* CV11 */
    bool call, listening, visiting;
    uint32_t call_ms, call_last;
    char name[ZUI_CV_SMALL];
    char heard[ZUI_CV_SMALL * 2];
    zui_chiglet_t guest;
    char guest_name[ZUI_CV_SMALL];
    swarm_feeling_t guest_feeling;
    zui_cv_call_fn on_call;
    void *call_ctx;
    zui_rect_t call_rect, end_rect; /* where they were last drawn */
    /* counters */
    uint32_t said, refused, stops, calls;
} zui_cv_t;

void zui_cv_facts_init(zui_cv_facts_t *f);

/* Quiet, VOICE, volume 70, rate 100, English words, lang "en". */
void zui_cv_init(zui_cv_t *cv, const zui_chiglet_t *who, zui_cv_speak_fn speak, zui_cv_stop_fn stop,
                 void *ctx);
void zui_cv_set_words(zui_cv_t *cv, const zui_cv_words_t *w); /* NULL: English */
void zui_cv_set_lang(zui_cv_t *cv, const char *bcp47);
void zui_cv_set_mode(zui_cv_t *cv, zui_cv_mode_t m);
void zui_cv_set_volume(zui_cv_t *cv, uint32_t v); /* clamped 0..100  */
void zui_cv_set_rate(zui_cv_t *cv, uint32_t pct); /* clamped 50..200 */
void zui_cv_set_busy(zui_cv_t *cv, bool busy, uint32_t now_ms);
void zui_cv_set_anchor(zui_cv_t *cv, zui_rect_t r);
void zui_cv_set_section(zui_cv_t *cv, zui_cv_section_fn fn, void *ctx);
/* Its feeling and health (the face morphs, CV9). */
void zui_cv_set_feeling(zui_cv_t *cv, swarm_feeling_t f, zui_health_t h, uint32_t now_ms);
void zui_cv_set_genes(zui_cv_t *cv, const zui_chiglet_t *who);

/* CV2 */
zui_cv_result_t zui_cv_tap(zui_cv_t *cv, const zui_cv_facts_t *f, uint32_t now_ms);
zui_cv_result_t zui_cv_hold(zui_cv_t *cv, const zui_cv_facts_t *f, uint32_t now_ms);
/* CV4 */
zui_cv_result_t zui_cv_status(zui_cv_t *cv, const zui_cv_facts_t *f, bool full, uint32_t now_ms);
/* CV5: read text aloud (title may be NULL). */
zui_cv_result_t zui_cv_read(zui_cv_t *cv, const char *text, const char *title, uint32_t now_ms);
zui_cv_result_t zui_cv_stop(zui_cv_t *cv, uint32_t now_ms);
zui_cv_result_t zui_cv_resume(zui_cv_t *cv, uint32_t now_ms);
zui_cv_result_t zui_cv_repeat(zui_cv_t *cv, uint32_t now_ms);
/* CV10: the last read again, exactly, with hashes and codes spelled out. */
zui_cv_result_t zui_cv_spell(zui_cv_t *cv, uint32_t now_ms);
void zui_cv_set_verbatim(zui_cv_t *cv, bool on);

/* CV11: face to face. */
void zui_cv_set_name(zui_cv_t *cv, const char *name);
void zui_cv_set_call_hook(zui_cv_t *cv, zui_cv_call_fn fn, void *ctx);
/* Open (says hello: SPEAKING) or close (says bye: STOPPED); NOTHING when it
 * already is so. */
zui_cv_result_t zui_cv_call(zui_cv_t *cv, bool open, uint32_t now_ms);
bool zui_cv_in_call(const zui_cv_t *cv);
/* From zui_hear: the mic is listening, and what was understood. */
void zui_cv_listening(zui_cv_t *cv, bool on, uint32_t now_ms);
void zui_cv_heard(zui_cv_t *cv, const char *utf8, uint32_t now_ms);
/* Another Chiglet visits (only in a call: else NOTHING), or leaves. */
zui_cv_result_t zui_cv_visit(zui_cv_t *cv, const zui_chiglet_t *who, const char *name,
                             swarm_feeling_t feeling, uint32_t now_ms);
void zui_cv_visit_end(zui_cv_t *cv, uint32_t now_ms);
/* The call window for a sw x sh screen, and what (x, y) is on it. */
zui_rect_t zui_cv_call_rect(const zui_cv_t *cv, int32_t sw, int32_t sh);
zui_cv_hit_t zui_cv_call_hit(const zui_cv_t *cv, int32_t x, int32_t y);

/* What the person said to it (push-to-talk). English phrases:
 *   stop | quiet | be quiet | shush                       -> stop
 *   continue | go on | keep reading | resume              -> resume
 *   repeat | again | say that again                       -> repeat
 *   spell that | spell it (out) | read that exactly | say it exactly
 *                                                          -> spell (CV10)
 *   let's talk | talk to me | come here | face to face     -> open the call
 *   bye | goodbye | hang up | end call | that's all        -> close it (in a call)
 *   how are you | how are you feeling | how do you feel   -> short status
 *   status | status report | report | what is going on    -> full status
 *   read this | read this section | read me this section | read it | read this to me
 *                                                          -> read the section ""
 *   read <name> | read me <name> | read the <name> section | read the <name>
 *                                                          -> read that section
 *   louder | quieter | softer | faster | slower           -> volume / rate by 10
 * Case, spaces and closing punctuation are ignored. Anything else is
 * ZUI_CV_NOT_MINE, for zui_voicectl. */
zui_cv_result_t zui_cv_hear(zui_cv_t *cv, const char *utf8, const zui_cv_facts_t *f,
                            uint32_t now_ms);

/* The host: the chunk handed to speak has finished playing. */
void zui_cv_spoken(zui_cv_t *cv, uint32_t now_ms);
/* The loudness of what is playing, 0..256 (zui_cv_level_pcm measures it). */
void zui_cv_level(zui_cv_t *cv, uint32_t level256);
uint32_t zui_cv_level_pcm(const int16_t *pcm, uint32_t n);
/* Once a frame: WORDS pacing, waiting chunks, the pop-up's phases. */
void zui_cv_tick(zui_cv_t *cv, uint32_t now_ms);

/* CV9: draw at now_ms (call zui_cv_tick first). text may be NULL (no
 * bubble words). */
void zui_cv_draw(zui_cv_t *cv, zui_surface_t *s, const zui_text_t *text, uint32_t now_ms);
/* Is (x, y) on the Chiglet (resting or popped up)? */
bool zui_cv_hit(const zui_cv_t *cv, int32_t x, int32_t y);
/* The words in the bubble now ("" when none). */
const char *zui_cv_bubble(const zui_cv_t *cv);
bool zui_cv_talking(const zui_cv_t *cv); /* a chunk is out, waiting, or more to come */

/* CV4: the status text alone. Returns its length (cut to fit, always
 * NUL-terminated). */
uint32_t zui_cv_status_text(const zui_cv_words_t *w, const zui_cv_facts_t *f, swarm_feeling_t feel,
                            zui_health_t h, bool full, char *out, uint32_t cap);
/* CV5: where the chunk that starts at *start (leading spaces skipped, so
 * *start may move) ends, at most max bytes on. Returns the end (exclusive);
 * end == *start at the end of the text. */
uint32_t zui_cv_chunk_end(const char *t, uint32_t len, uint32_t *start, uint32_t max);

#endif /* ZXV_ZUI_CHGVOICE_H */
