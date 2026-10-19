/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_reader.h — the screen reader: speaks the zui widget tree for people
 * who cannot see the screen, or would rather hear it.
 *
 * zui_widget.h gives every widget a role and a name (W6). The reader watches
 * the tree once a frame and says what changed, and it has its own cursor so a
 * person can explore the screen without moving keyboard focus.
 *
 *   R1 FOCUS IS SPOKEN. When keyboard focus moves, the reader says the new
 *      control: its name (or label, or placeholder), its role, its value, its
 *      state (disabled, checked, selected) and where it is ("3 of 12" in a
 *      list or a menu). Moving into a new landmark or named group says that
 *      group first. A list's selection moving under focus is spoken too.
 *   R2 A READER CURSOR. Next / previous item, heading, landmark and held
 *      answer move the reader's own cursor through the screen in reading
 *      order (tree order, a list's rows in turn) and speak what is there.
 *      Keyboard focus stays put until the person asks to activate (R6).
 *   R3 HELD AI AND ERRORS ARE ANNOUNCED. A held answer (zui_widget.h W4)
 *      appearing is announced with who made it, what it cost and its text;
 *      accepting or rejecting it is announced. A label drawn in the alert
 *      colour is an error line: it is announced, interrupting, when it
 *      appears or changes. Marked live regions are announced on change.
 *   R4 VERBOSITY. LOW says name, role, value and the state that matters;
 *      MEDIUM adds positions, counts, cost and the group being entered; HIGH
 *      adds shortcuts and hints.
 *   R5 SPEECH IS A CALLBACK. Every utterance goes to one zui_speak_fn with a
 *      priority: INTERRUPT (stop what is being said, say this) or QUEUE (say
 *      it after). An empty text with INTERRUPT means stop. Within one call the
 *      first utterance may interrupt and the rest queue, so nothing the
 *      reader says cuts off its own sentence. zui_reader_voice_t adapts that
 *      to a speech engine (speech_session_say fits behind a one-line shim);
 *      the reader itself does not depend on speech/.
 *   R6 KEYS. Ctrl+Alt with:
 *        Right / Left        next / previous item
 *        Down / Up           next / previous heading
 *        Page Down / Page Up next / previous landmark
 *        Home / End          first / last item
 *        N (Shift+N)         next (previous) held AI answer
 *        A (Shift+A)         read the whole screen (from the reader cursor)
 *        F                   say where focus is again
 *        R                   repeat the last thing said
 *        V                   next verbosity level
 *        S                   stop speaking
 *        Space or Enter      move keyboard focus to the reader cursor
 *        Z                   turn the reader off and on
 *      zui_reader_event returns whether it used the event (both halves of a
 *      bound key press are used, so the app never sees half of one).
 *   R7 MARKS. The widget tree has no heading, landmark, check box or live
 *      region roles; the app marks those widgets on the reader. A title-font
 *      label is a heading without a mark, and a named panel is a region.
 *
 * Integer only, no allocation, deterministic. The reader keeps a pointer to
 * the zui_ui_t and reads it; only R6's activate changes it.
 */
#ifndef ZXV_ZUI_READER_H
#define ZXV_ZUI_READER_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_input.h"
#include "zui_widget.h"

#ifndef ZUI_READER_MARKS
#    define ZUI_READER_MARKS 64u /* widgets with a heading/landmark/check/live/hint mark */
#endif
#define ZUI_READER_UTTER 768u /* bytes of one utterance, with the NUL */
#define ZUI_READER_HINT  64u

/* R5 */
typedef enum { ZUI_SAY_QUEUE = 0, ZUI_SAY_INTERRUPT } zui_say_t;
typedef void (*zui_speak_fn)(const char *text, zui_say_t how, void *ctx);

/* R4 */
typedef enum { ZUI_VERB_LOW = 0, ZUI_VERB_MEDIUM, ZUI_VERB_HIGH, ZUI_VERB_COUNT } zui_verbosity_t;

/* R7 */
typedef enum {
    ZUI_LANDMARK_NONE = 0,
    ZUI_LANDMARK_BANNER,        /* the top bar: title, account     */
    ZUI_LANDMARK_NAVIGATION,    /* the rail, a sidebar of places   */
    ZUI_LANDMARK_MAIN,          /* the main content                */
    ZUI_LANDMARK_SEARCH,        /* a search box and its options    */
    ZUI_LANDMARK_REGION,        /* any other named part            */
    ZUI_LANDMARK_COMPLEMENTARY, /* an inspector, a side panel   */
    ZUI_LANDMARK_CONTENTINFO,   /* the status bar               */
    ZUI_LANDMARK_COUNT
} zui_landmark_t;

typedef enum { ZUI_CHECK_NONE = 0, ZUI_CHECK_OFF, ZUI_CHECK_ON, ZUI_CHECK_MIXED } zui_check_t;
typedef enum { ZUI_LIVE_OFF = 0, ZUI_LIVE_POLITE, ZUI_LIVE_ASSERTIVE } zui_live_t;

typedef struct {
    zui_id_t id;
    uint8_t kind;               /* the widget's kind when marked: a rebuilt tree drops it */
    uint8_t heading;            /* 1..6, 0 = not a heading      */
    uint8_t landmark;           /* zui_landmark_t                */
    uint8_t check;              /* zui_check_t: a check box      */
    uint8_t live;               /* zui_live_t                    */
    char hint[ZUI_READER_HINT]; /* spoken at HIGH      */
} zui_reader_mark_t;

/* R2: where the reader cursor can go. */
typedef enum {
    ZUI_READ_NEXT = 0,
    ZUI_READ_PREV,
    ZUI_READ_FIRST,
    ZUI_READ_LAST,
    ZUI_READ_NEXT_HEADING,
    ZUI_READ_PREV_HEADING,
    ZUI_READ_NEXT_LANDMARK,
    ZUI_READ_PREV_LANDMARK,
    ZUI_READ_NEXT_HELD,
    ZUI_READ_PREV_HELD
} zui_read_move_t;

typedef struct {
    zui_ui_t *ui;
    zui_speak_fn speak;
    void *speak_ctx;
    bool on;
    zui_verbosity_t verbosity;
    /* R2: the reader cursor: a widget, and a row when it is a list (-1 = the
     * list itself) */
    zui_id_t cursor;
    int32_t cursor_row;
    /* what was last spoken about, to find changes */
    zui_id_t focus;
    int32_t focus_sel;
    zui_id_t focus_group;
    bool menu_open, palette_open;
    int32_t menu_hot, palette_hot;
    uint32_t palette_n;
    uint32_t n_seen;
    bool primed;               /* the first update has set the baselines */
    uint32_t sig[ZUI_WIDGETS]; /* R3: held, error and live signatures (0 = not shown) */
    zui_reader_mark_t mark[ZUI_READER_MARKS];
    uint32_t n_mark;
    bool said; /* R5: something was said in this call */
    uint32_t utterances;
    char buf[ZUI_READER_UTTER];
    char last[ZUI_READER_UTTER];
} zui_reader_t;

/* On, MEDIUM. Nothing is said until the first zui_reader_update. */
void zui_reader_init(zui_reader_t *r, zui_ui_t *ui, zui_speak_fn speak, void *ctx);
void zui_reader_set_on(zui_reader_t *r, bool on); /* says "screen reader on/off" */
void zui_reader_set_verbosity(zui_reader_t *r, zui_verbosity_t v);

/* R1, R3: call once a frame after the events are handled. */
void zui_reader_update(zui_reader_t *r);
/* R6: returns true when the reader used the event. */
bool zui_reader_event(zui_reader_t *r, const zui_event_t *e);

/* R2: move the reader cursor and say the item. Returns false (and says
 * "no next heading", "top", ...) when there is nothing that way. */
bool zui_reader_move(zui_reader_t *r, zui_read_move_t how);
/* Read every item in order (from the reader cursor when from_cursor). */
void zui_reader_read_all(zui_reader_t *r, bool from_cursor);
void zui_reader_where(zui_reader_t *r);  /* say the focused control again */
void zui_reader_repeat(zui_reader_t *r); /* say the last utterance again  */
void zui_reader_stop(zui_reader_t *r);   /* stop speaking                 */
/* Move keyboard focus to the reader cursor (and select its row). */
bool zui_reader_activate(zui_reader_t *r);

/* R3: say an app's own message; an error interrupts and starts "Error: ". */
void zui_reader_announce(zui_reader_t *r, const char *text, zui_say_t how);
void zui_reader_error(zui_reader_t *r, const char *text);

/* R7: marks. Each returns false when the mark table is full or id is bad. */
bool zui_reader_set_heading(zui_reader_t *r, zui_id_t id, uint8_t level);
bool zui_reader_set_landmark(zui_reader_t *r, zui_id_t id, zui_landmark_t lm);
bool zui_reader_set_check(zui_reader_t *r, zui_id_t id, zui_check_t c);
bool zui_reader_set_live(zui_reader_t *r, zui_id_t id, zui_live_t live);
bool zui_reader_set_hint(zui_reader_t *r, zui_id_t id, const char *hint);
/* Call when the app rebuilds its tree. (A mark whose widget id is gone, or
 * now holds a different kind of widget, is dropped on the next update.) */
void zui_reader_clear_marks(zui_reader_t *r);

/* What the reader says for a widget (row >= 0: a list's row) at the current
 * verbosity, without context. Returns the length (cut to fit, always
 * NUL-terminated). */
uint32_t zui_reader_describe(const zui_reader_t *r, zui_id_t id, int32_t row, char *out,
                             uint32_t cap);
const char *zui_landmark_name(zui_landmark_t lm); /* "navigation", "main", ... */
const char *zui_verbosity_name(zui_verbosity_t v);

/* ---- R5: a speech engine behind the reader ----------------------------------
 * Pass zui_reader_voice_speak as the speak function and a zui_reader_voice_t
 * as its ctx. say() speaks text (lang may be NULL for the engine's own);
 * stop() cuts off what is playing (NULL: interrupts just queue); caption()
 * shows the same words on screen (NULL: none). For speech_session_t:
 *   static int say(void *e, const char *t, const char *l)
 *   { return speech_session_say((speech_session_t *) e, t, l); }
 * tidy, when set, rewrites what is said (not the caption) the way a person
 * would say it, with tidy_words as its words: pass speech_tidy_fn
 * (speech/speech_tidy.h), so a hash or a link on screen is named rather than
 * spelled character by character. NULL says the text as it is.
 */
typedef struct {
    int (*say)(void *engine, const char *text, const char *lang); /* 0 = ok */
    void (*stop)(void *engine);
    void (*caption)(void *engine, const char *text);
    void *engine;
    const char *lang;
    uint32_t said, stopped, failed;
    uint32_t (*tidy)(const char *in, uint32_t n, char *out, uint32_t cap, const void *words);
    const void *tidy_words;
} zui_reader_voice_t;

void zui_reader_voice_speak(const char *text, zui_say_t how, void *voice);

#endif /* ZXV_ZUI_READER_H */
