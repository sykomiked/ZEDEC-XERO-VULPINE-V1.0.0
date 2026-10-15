/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_notice.h — the notification center: what apps have to tell the
 * person, gathered in one panel, with a banner for what is new.
 *
 * Apps post notices here (the system bus, social/zx_notify.h, feeds the
 * same center through a small adapter). The center keeps them grouped by
 * app and by thread, knows what has been read, shows a banner for each new
 * one when the person may be interrupted, and draws the panel and the
 * banner with zui_canvas.
 *
 *   NC1 A NOTICE has an app id, a title and body, a priority (the same
 *       numbers as zx_notify's zxn_pri_t: low, normal, high, urgent), a
 *       time in ms given by the caller, up to ZUI_NOTICE_ACTIONS actions
 *       (a label and an action id the app understands), and a thread key
 *       ("" for none) that groups notices about one thing ("chat with
 *       Ana"). Strings are copied and cut on a UTF-8 boundary.
 *   NC2 POST, UPDATE, WITHDRAW. A post returns the notice's id (never 0).
 *       An update replaces a notice's text, actions, priority and time in
 *       place; it banners again only when asked to (`alert`), and then
 *       counts as unread again. A withdraw removes a notice (or a whole
 *       thread) at once, banner and all.
 *   NC3 GROUPS. The panel lists apps, the one with the newest notice
 *       first; within an app, its threads the same way; within a thread,
 *       its notices newest first. Newest is the later time, then the later
 *       post.
 *   NC4 READ. Each notice is read or unread; the center counts unread per
 *       app and thread, and marks a notice read when the person opens it,
 *       acts on it, or reads its group. A read notice never banners.
 *   NC5 BANNERS. A new notice is shown as a banner for ZUI_NOTICE_BANNER_MS
 *       (an urgent one stays until it is dismissed or acted on), one at a
 *       time, the highest priority first, then the oldest. An urgent
 *       notice takes the place of a banner that is not urgent, which waits
 *       its turn again. Up to ZUI_NOTICE_BANNERS wait; past that, the
 *       lowest-priority, oldest waiting one loses its banner (it stays in
 *       the panel) only when it is no more urgent than the newcomer, else
 *       the newcomer gets none.
 *   NC6 DO NOT DISTURB mirrors zx_notify N6: while it is on, a notice's
 *       banner is HELD unless its priority is at least the breakthrough
 *       priority or its app is on the allow list; a MUTED app's notices
 *       are kept and never banner. Turning it off releases the held
 *       banners (NC5 order). The decision is zui_notice_decide; a shell
 *       that keeps do-not-disturb on the system bus installs a gate
 *       callback that asks the bus instead (zui_notice_decide(bus->dnd,
 *       bus->dnd_breakthrough, ...), the same numbers), and calls
 *       zui_notice_regate when the bus's setting changes.
 *   NC7 AI IS HELD. A notice an AI agent wrote names the agent and is
 *       marked as such in the panel and the banner. Its priority is at
 *       most normal, so it never breaks through do-not-disturb. Its
 *       actions are held like any AI answer (zui_widget.h W4): the person
 *       accepts or rejects it first, and zui_notice_act refuses its
 *       actions until it is accepted.
 *   NC8 NOTHING ACTS BY ITSELF. zui_notice_act only hands back the action
 *       id the person chose, for the app to run; the center never runs
 *       one, and is called only from the person's input.
 *   NC9 HISTORY. The center keeps up to `cap` notices (ZUI_NOTICE_MAX at
 *       most); a post past that evicts the oldest posted first, read or
 *       not, and counts it.
 *   NC10 DRAWING. The panel and the banner sit on the #002911 ground as
 *       carved panels with light green text; gold is used once, for the
 *       one primary action (the first action, or Accept on a held AI
 *       notice) of the selected notice in the panel, or of the banner
 *       (the shell shows banners only while the panel is closed, so the
 *       two never share a screen).
 *       Drawing also records where each notice and action is, for
 *       zui_notice_hit. Integer only, no allocation, deterministic.
 *   NC11 PREVIEWS OFF. With hide_previews set (the person's privacy
 *       setting) no title, body or action is drawn: the panel shows one
 *       card per app saying how many it has ("3 notifications", from
 *       zui_notice_summary) and the banner says the same for its app.
 *       Opening the card still reaches the app (ZUI_NOTICE_HIT_CARD).
 */
#ifndef ZXV_ZUI_NOTICE_H
#define ZXV_ZUI_NOTICE_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_palette.h"

#ifndef ZUI_NOTICE_MAX
#    define ZUI_NOTICE_MAX 64u
#endif
#define ZUI_NOTICE_ACTIONS   3u
#define ZUI_NOTICE_BANNERS   8u
#define ZUI_NOTICE_LIST      8u /* apps on the allow list, and muted apps */
#define ZUI_NOTICE_BANNER_MS 5000u
#define ZUI_NOTICE_HITS      96u
#define ZUI_NOTICE_SHOWN     3u /* notices drawn per thread before "+n more" */

#define ZUI_NOTICE_APP_MAX    32u
#define ZUI_NOTICE_THREAD_MAX 32u
#define ZUI_NOTICE_TITLE_MAX  64u
#define ZUI_NOTICE_BODY_MAX   192u
#define ZUI_NOTICE_LABEL_MAX  24u
#define ZUI_NOTICE_ACTION_MAX 48u
#define ZUI_NOTICE_AGENT_MAX  32u

typedef enum {
    ZUI_NPRI_LOW = 0, /* = ZXN_PRI_LOW ... ZXN_PRI_URGENT */
    ZUI_NPRI_NORMAL,
    ZUI_NPRI_HIGH,
    ZUI_NPRI_URGENT
} zui_npri_t;

/* A notice's banner (NC5, NC6). */
typedef enum {
    ZUI_BANNER_NONE = 0, /* shown already, read, or never to be shown */
    ZUI_BANNER_WAITING,  /* in the queue                              */
    ZUI_BANNER_HELD,     /* do not disturb: waiting for it to end     */
    ZUI_BANNER_SHOWING   /* on screen now                             */
} zui_banner_state_t;

/* What a gate says about a notice's banner (NC6). */
typedef enum { ZUI_GATE_SHOW = 0, ZUI_GATE_HOLD, ZUI_GATE_SILENT } zui_gate_t;

/* NC7: the W4 numbering. */
enum { ZUI_HELD_NONE = 0, ZUI_HELD_PENDING = 1, ZUI_HELD_ACCEPTED = 2, ZUI_HELD_REJECTED = 3 };

enum {
    ZUI_NOTICE_OK = 0,
    ZUI_NOTICE_E_NONE = -1,     /* no such notice, or no such action    */
    ZUI_NOTICE_E_HELD = -2,     /* NC7: an AI notice not yet accepted   */
    ZUI_NOTICE_E_REJECTED = -3, /* NC7: the person rejected it          */
    ZUI_NOTICE_E_ARG = -4
};

typedef struct {
    char label[ZUI_NOTICE_LABEL_MAX];
    char action[ZUI_NOTICE_ACTION_MAX];
} zui_notice_action_t;

typedef struct {
    uint32_t id;  /* 0 = a free slot */
    uint32_t seq; /* post order (NC3 ties, NC9 eviction) */
    char app[ZUI_NOTICE_APP_MAX];
    char thread[ZUI_NOTICE_THREAD_MAX];
    char title[ZUI_NOTICE_TITLE_MAX];
    char body[ZUI_NOTICE_BODY_MAX];
    char agent[ZUI_NOTICE_AGENT_MAX]; /* NC7: "" unless an AI wrote it */
    uint8_t pri;                      /* zui_npri_t */
    uint64_t time_ms;
    zui_notice_action_t act[ZUI_NOTICE_ACTIONS];
    uint8_t nact;
    bool read;
    bool ai;
    uint8_t held;   /* ZUI_HELD_*: PENDING until the person decides (NC7) */
    uint8_t banner; /* zui_banner_state_t */
} zui_notice_t;

/* What a post or an update gives. NULL strings are empty. */
typedef struct {
    const char *app, *thread, *title, *body;
    zui_npri_t pri;
    uint64_t time_ms;
    const zui_notice_action_t *act;
    uint32_t nact;     /* more than ZUI_NOTICE_ACTIONS: the first three */
    const char *agent; /* NC7: the AI agent that wrote it; NULL or "" for none */
} zui_notice_spec_t;

typedef zui_gate_t (*zui_notice_gate_fn)(void *ctx, const zui_notice_t *n);

/* NC3: an app, or a thread within one. */
typedef struct {
    const char *app, *thread; /* thread NULL for an app group */
    uint32_t count, unread;
    uint32_t newest; /* the id of its newest notice */
    uint64_t newest_ms;
    uint32_t newest_seq;
} zui_notice_group_t;

/* Where drawing put things (NC10). action is an index into the notice's
 * actions, or one of these. */
enum { ZUI_NOTICE_HIT_CARD = -1, ZUI_NOTICE_HIT_ACCEPT = -2, ZUI_NOTICE_HIT_REJECT = -3 };
typedef struct {
    uint32_t id;
    int32_t action;
    bool banner;
    zui_rect_t r;
} zui_notice_hit_t;

typedef struct {
    zui_notice_t n[ZUI_NOTICE_MAX];
    uint32_t cap; /* NC9: at most ZUI_NOTICE_MAX */
    uint32_t next_id, next_seq;
    uint32_t evicted;        /* NC9 */
    uint32_t banner_dropped; /* NC5: notices that lost their banner */
    /* NC5: the banner on screen */
    uint32_t banner;
    uint64_t banner_since;
    uint32_t banner_ms;
    /* NC6 */
    bool dnd;
    uint8_t breakthrough; /* zui_npri_t */
    char allow[ZUI_NOTICE_LIST][ZUI_NOTICE_APP_MAX];
    char muted[ZUI_NOTICE_LIST][ZUI_NOTICE_APP_MAX];
    uint32_t nallow, nmuted;
    zui_notice_gate_fn gate; /* NULL: zui_notice_decide with the above */
    void *gate_ctx;
    /* NC10: the panel */
    uint32_t sel;   /* the selected notice (0: the newest) */
    int32_t scroll; /* pixels scrolled down */
    zui_notice_hit_t hit[ZUI_NOTICE_HITS];
    uint32_t nhit;
    uint64_t now; /* the latest time seen */
    const zui_colors_t *colors; /* zui_palette.h C6: the set painted with; NULL dark */
    bool hide_previews;         /* NC11 */
} zui_notice_center_t;

void zui_notice_init(zui_notice_center_t *c);
/* NC9: a smaller history; evicts at once, oldest first. */
void zui_notice_set_cap(zui_notice_center_t *c, uint32_t cap);

/* NC2. now_ms is the caller's clock (for the banner's time on screen). */
uint32_t zui_notice_post(zui_notice_center_t *c, const zui_notice_spec_t *spec, uint64_t now_ms);
bool zui_notice_update(zui_notice_center_t *c, uint32_t id, const zui_notice_spec_t *spec,
                       bool alert, uint64_t now_ms);
bool zui_notice_withdraw(zui_notice_center_t *c, uint32_t id, uint64_t now_ms);
/* Every notice of one thread of an app; returns how many went. */
uint32_t zui_notice_withdraw_thread(zui_notice_center_t *c, const char *app, const char *thread,
                                    uint64_t now_ms);
const zui_notice_t *zui_notice_get(const zui_notice_center_t *c, uint32_t id);
uint32_t zui_notice_count(const zui_notice_center_t *c);

/* NC3: app groups, newest first; returns how many (only cap written). */
uint32_t zui_notice_apps(const zui_notice_center_t *c, zui_notice_group_t *out, uint32_t cap);
/* NC3: one app's threads, newest first. */
uint32_t zui_notice_threads(const zui_notice_center_t *c, const char *app, zui_notice_group_t *out,
                            uint32_t cap);
/* NC3: notice ids newest first: all (app NULL), one app's (thread NULL),
 * or one thread's. */
uint32_t zui_notice_list(const zui_notice_center_t *c, const char *app, const char *thread,
                         uint32_t *ids, uint32_t cap);

/* NC4 */
bool zui_notice_mark_read(zui_notice_center_t *c, uint32_t id, bool read);
/* app NULL: everything; thread NULL: the whole app. Returns how many changed. */
uint32_t zui_notice_mark_group_read(zui_notice_center_t *c, const char *app, const char *thread);
uint32_t zui_notice_unread(const zui_notice_center_t *c, const char *app, const char *thread);

/* NC5: the banner's notice id now (0 none). */
uint32_t zui_notice_banner(const zui_notice_center_t *c);
/* NC5: time passes: a banner's time runs out, the next one shows. */
void zui_notice_tick(zui_notice_center_t *c, uint64_t now_ms);
/* The person dismissed the banner (it stays in the panel, unread). */
void zui_notice_banner_dismiss(zui_notice_center_t *c, uint64_t now_ms);
/* How many banners wait (not counting the one showing, nor held ones). */
uint32_t zui_notice_waiting(const zui_notice_center_t *c);

/* NC6 */
zui_gate_t zui_notice_decide(bool dnd, zui_npri_t breakthrough, bool allowed, bool muted,
                             zui_npri_t pri);
/* Returns the number of held banners released (when turning it off). */
uint32_t zui_notice_set_dnd(zui_notice_center_t *c, bool on, zui_npri_t breakthrough,
                            uint64_t now_ms);
bool zui_notice_allow(zui_notice_center_t *c, const char *app, bool allowed);
bool zui_notice_mute(zui_notice_center_t *c, const char *app, bool muted);
void zui_notice_set_gate(zui_notice_center_t *c, zui_notice_gate_fn gate, void *ctx);
/* Ask the gate again about every banner not yet shown (the outside
 * setting changed). Returns how many were released from held. */
uint32_t zui_notice_regate(zui_notice_center_t *c, uint64_t now_ms);

/* NC7 */
bool zui_notice_accept(zui_notice_center_t *c, uint32_t id);
bool zui_notice_reject(zui_notice_center_t *c, uint32_t id);
/* NC8: the person chose action i of notice id (i = -1: they opened the
 * notice itself; *action is then its app id). Marks it read and takes
 * down its banner. */
int zui_notice_act(zui_notice_center_t *c, uint32_t id, int32_t i, const char **action,
                   uint64_t now_ms);

/* NC10. text may be NULL (or have no font): nothing is written, the
 * shapes are the same. Returns the height the content needs, in pixels
 * (more than r.h: the panel scrolls). */
int32_t zui_notice_draw_center(zui_notice_center_t *c, zui_surface_t *s, zui_rect_t r,
                               const zui_text_t *text);
/* Draws the banner (if any) in r. Returns false when there is none. */
bool zui_notice_draw_banner(zui_notice_center_t *c, zui_surface_t *s, zui_rect_t r,
                            const zui_text_t *text);
/* NC11: "3 notifications" ("1 notification") for app (NULL: all of them).
 * Returns the length written (always NUL-terminated when cap > 0). */
uint32_t zui_notice_summary(const zui_notice_center_t *c, const char *app, char *out, uint32_t cap);
/* What the last drawing put at (x, y): its notice id (0 none) and the
 * action (an index, or ZUI_NOTICE_HIT_*); banner says it was the banner's. */
uint32_t zui_notice_hit(const zui_notice_center_t *c, int32_t x, int32_t y, int32_t *action,
                        bool *banner);

#endif /* ZXV_ZUI_NOTICE_H */
