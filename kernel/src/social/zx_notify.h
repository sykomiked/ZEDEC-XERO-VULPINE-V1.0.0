/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_notify.h — one notification bus for the whole system.
 *
 * Every module that has something to tell the user posts here: the AI
 * companion finishing (or failing) a request, a message arriving, a call
 * ringing or missed, a trade or economy event, an update, a peer asking to
 * connect. The desktop, the hosted app's OS bridge and anything else
 * subscribe once and get all of it, filtered as they ask.
 *
 *   N1  A NOTE carries a kind, a priority, a source module name, a short
 *       title and body, an optional action id (what a click should open),
 *       a logical timestamp (the bus's own Lamport-style counter) and an
 *       optional wall-time hint.
 *   N2  COALESCE.  A note with a nonzero key collapses into an UNREAD note
 *       of the same kind, source and key: count goes up, the title, body and
 *       action become the latest, the priority becomes the higher of the
 *       two, last_ts moves. Ten "3 new messages from Ana" become one note
 *       with count 10. Once read, the next one starts a fresh note.
 *   N3  BOUNDED QUEUE.  The caller supplies `cap` slots. When full, the
 *       victim is the oldest READ note; failing that, the oldest unread note
 *       of the lowest priority, but only if that priority is <= the newcomer's.
 *       Otherwise the newcomer is dropped (counted in `dropped`): a flood of
 *       low-priority notes can never push out an urgent unread one.
 *   N4  READ STATE.  mark_read / mark_all_read / unread counts per kind mask.
 *   N5  SUBSCRIBERS.  Up to ZXN_MAX_SUBS callbacks, each with a kind mask and
 *       a minimum priority. A callback must not post to the bus it is
 *       called from (the bus is not re-entrant).
 *   N6  DO NOT DISTURB.  While on, a note is stored but HELD (not delivered)
 *       unless its priority is at least `breakthrough` or its kind is in the
 *       allow mask. Turning DND off delivers the held notes, oldest first.
 *       A MUTED kind is stored and never delivered.
 *   N7  AI TASK HOOK.  zxn_task_finished is the one call an AI task makes
 *       when it ends. Its key is the task id, so retries coalesce. Where a
 *       task ends today: chg_infer() returns (chiglet/chiglet.c, also called
 *       from voice.c, cards.c, games.c), swarm_overlap_settle() settles a
 *       shared job (swarm/swarm_overlap.c), swarm_quality_credit() passes an
 *       output through the quality gate, and the hosted app's answer() in
 *       kernel/arch/hosted/zxv_host.c replies to /api/ask. Those callers post;
 *       this module does not reach into them.
 *   N8  NO MESSAGE CONTENT.  zxn_message_received takes a sender label and
 *       no body on purpose: a DM is ciphertext (social_post.h P9) and its
 *       plaintext must not leak into an OS notification centre.
 *
 * RULE: freestanding C11. No libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Slots are the caller's. Strings are copied,
 * truncated at a UTF-8 boundary, and stored raw; escaping for a shell or an
 * OS API is the bridge's job (kernel/arch/hosted/zx_notify_host.c).
 *
 * HONEST LIMITS
 * -------------
 * Not thread-safe and not re-entrant: one owner thread posts and delivers.
 * Lookups are linear in cap (fine for the tens to hundreds of notes a person
 * can read). Delivery is synchronous; a slow subscriber stalls the poster.
 * The logical clock orders notes on this device only. Held notes count
 * against the same capacity, so a long DND with a flood can still drop
 * low-priority notes (N3). Coalescing compares the source by its truncated
 * text.
 */
#ifndef ZX_NOTIFY_H
#define ZX_NOTIFY_H

#include <stdint.h>
#include <stdbool.h>

#define ZXN_SOURCE_MAX 24u
#define ZXN_TITLE_MAX  64u
#define ZXN_BODY_MAX   192u
#define ZXN_ACTION_MAX 48u
#define ZXN_MAX_SUBS   8u

typedef enum {
    ZXN_AI_TASK_DONE = 0,
    ZXN_AI_TASK_FAILED,
    ZXN_MESSAGE,
    ZXN_CALL_INCOMING,
    ZXN_CALL_MISSED,
    ZXN_ECONOMY, /* trade or economy event */
    ZXN_UPDATE,  /* update available */
    ZXN_PEER_REQUEST,
    ZXN_KIND_COUNT
} zxn_kind_t;

#define ZXN_KIND_BIT(k) (1u << (uint32_t) (k))
#define ZXN_ALL_KINDS   ((1u << ZXN_KIND_COUNT) - 1u)

typedef enum { ZXN_PRI_LOW = 0, ZXN_PRI_NORMAL, ZXN_PRI_HIGH, ZXN_PRI_URGENT } zxn_pri_t;

typedef struct {
    uint32_t seq; /* unique per bus, never 0 */
    uint8_t kind, pri;
    bool used, read, held;
    uint32_t count;             /* N2: how many posts this note stands for */
    uint64_t key;               /* N2: 0 = never coalesce */
    uint64_t first_ts, last_ts; /* logical */
    uint64_t wall_ms;           /* optional hint, 0 = none */
    char source[ZXN_SOURCE_MAX];
    char title[ZXN_TITLE_MAX];
    char body[ZXN_BODY_MAX];
    char action[ZXN_ACTION_MAX];
} zxn_note_t;

typedef struct {
    zxn_kind_t kind;
    zxn_pri_t pri;
    const char *source, *title, *body, *action; /* NULL = empty */
    uint64_t key;
    uint64_t wall_ms;
} zxn_spec_t;

/* coalesced = this delivery updates an existing note (N2). */
typedef void (*zxn_sub_fn)(void *ctx, const zxn_note_t *n, bool coalesced);

typedef struct {
    zxn_sub_fn fn;
    void *ctx;
    uint32_t kind_mask;
    uint8_t min_pri;
} zxn_sub_t;

typedef struct {
    zxn_note_t *slot;
    uint32_t cap;
    zxn_sub_t sub[ZXN_MAX_SUBS];
    uint64_t clock;
    uint32_t next_seq;
    uint32_t muted_kinds;
    bool dnd;
    uint8_t dnd_breakthrough;
    uint32_t dnd_allow_kinds;
    uint32_t dropped, evicted, coalesced, delivered;
} zxn_bus_t;

bool zxn_init(zxn_bus_t *b, zxn_note_t *slots, uint32_t cap);

/* N5: returns a subscription id >= 0, or -1 if full. */
int32_t zxn_subscribe(zxn_bus_t *b, zxn_sub_fn fn, void *ctx, uint32_t kind_mask,
                      zxn_pri_t min_pri);
void zxn_unsubscribe(zxn_bus_t *b, int32_t id);

/* N1-N3, N6: returns the note's seq, or 0 if it was dropped. */
uint32_t zxn_post(zxn_bus_t *b, const zxn_spec_t *spec);

const zxn_note_t *zxn_get(const zxn_bus_t *b, uint32_t seq);

/* N4 */
bool zxn_mark_read(zxn_bus_t *b, uint32_t seq);
uint32_t zxn_mark_all_read(zxn_bus_t *b, uint32_t kind_mask);
uint32_t zxn_unread(const zxn_bus_t *b, uint32_t kind_mask);
/* Newest first (by last_ts). Returns the count written. */
uint32_t zxn_list(const zxn_bus_t *b, const zxn_note_t **out, uint32_t max, bool unread_only);

/* N6. Returns the number of held notes delivered when DND turns off. */
uint32_t zxn_set_dnd(zxn_bus_t *b, bool on, zxn_pri_t breakthrough, uint32_t allow_kinds);
void zxn_mute(zxn_bus_t *b, uint32_t kind_mask);

/* N7: the AI-task completion hook. ok picks DONE (NORMAL) or FAILED (HIGH).
 * action, e.g. "chat:42", tells the UI what to open. */
uint32_t zxn_task_finished(zxn_bus_t *b, const char *source, uint64_t task_id, bool ok,
                           const char *title, const char *summary, const char *action);

/* N8: a message arrived. No body, by design. */
uint32_t zxn_message_received(zxn_bus_t *b, const char *source, const char *from_label,
                              uint64_t conversation_key);

/* Calls: incoming rings URGENT; missed is HIGH. Keyed by call id. */
uint32_t zxn_call(zxn_bus_t *b, const char *source, bool missed, const char *caller_label,
                  uint64_t call_id);

const char *zxn_kind_name(zxn_kind_t k);

/* ---- Hosted bridge, kernel/arch/hosted/zx_notify_host.c (hosted app only,
 * uses libc; never linked into the kernel). Subscribe zxn_host_deliver to
 * show notes as OS notifications: macOS osascript "display notification",
 * Linux notify-send, chosen at run time (AUTO uses one only when it is
 * installed, and notify-send only with a desktop session); otherwise
 * nothing. Commands run via fork/execvp with an argv array, never through a
 * shell. ---- */
typedef enum {
    ZXN_HOST_AUTO = 0,      /* this OS's notifier if present, else nothing */
    ZXN_HOST_PRINT,         /* stdout (use for --server: no desktop there) */
    ZXN_HOST_DRY_RUN_MAC,   /* build the osascript argv, run nothing */
    ZXN_HOST_DRY_RUN_LINUX, /* build the notify-send argv, run nothing */
    ZXN_HOST_OFF            /* deliver nothing */
} zxn_host_mode_t;
void zxn_host_set_mode(zxn_host_mode_t mode);
/* What AUTO resolves to on this machine now: "osascript", "notify-send" or
 * "none"; other modes give "print", "dry-run" or "off". */
const char *zxn_host_backend(void);
void zxn_host_deliver(void *ctx, const zxn_note_t *n, bool coalesced);
/* Escape for the inside of an AppleScript "..." literal: \ -> \\, " -> \",
 * CR/LF/TAB and other control bytes -> space, invalid UTF-8 -> '?'. Always
 * NUL-terminates; returns the length written. */
uint32_t zxn_host_escape_applescript(const char *in, char *out, uint32_t cap);
/* Clean a notify-send argument: control bytes -> space, invalid UTF-8 ->
 * '?', and & < > as entities (notify-send bodies may be parsed as markup). */
uint32_t zxn_host_clean_text(const char *in, char *out, uint32_t cap);
/* DRY_RUN: the argv the last delivery would have run, NULL past the end. */
const char *zxn_host_last_argv(uint32_t i);

#endif /* ZX_NOTIFY_H */
