/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* halloo_model.h — Halloo, voice and video calls, as a model (zxm.h):
 * place, answer and end calls through zxvd's call group, decide whether
 * an incoming call may ring, and keep a history of the calls that ended.
 *
 * zxvd's call.* methods are signalling only: media (capture, codecs, RTP)
 * is the host's job and is not wired (H5). zxvd keeps no contacts, no
 * quiet hours and no call history, and call.status gives no time, so the
 * contacts, quiet hours and ring time are the owner's settings held here,
 * the history is kept here, and a call's length is measured from the
 * times the view passes to halloo_refresh. zxvd has no notification
 * method, so the model only composes each notice's text (halloo_t.notice)
 * for the view to show. No drawing, no allocation, integers only; the
 * rules are halloo_rules.h.
 *
 *   HM1 NO EMERGENCY CALLS. The view always shows HALLOO_NO_EMERGENCY:
 *       Halloo calls other ZXV nodes only.
 *   HM2 RINGING. An incoming call rings (a notice is composed) only when
 *       halloo_may_ring says so (HR1, HR2): a known contact, not blocked,
 *       outside the quiet hours. A blocked caller's ringing call is
 *       rejected (call.hangup) by halloo_screen; any other call that may
 *       not ring stays silent and is shown in the list, never answered or
 *       rejected for the owner.
 *   HM3 RING TIME. The owner's ring time is kept only inside HR3; an
 *       incoming call still ringing after it, by the view's clock, is
 *       left to ring silently (zxvd ends it at 30 s).
 *   HM4 SIGNALLING. call.signal's messages are not encrypted by zxvd. The
 *       model relays them only when the caller says the carrier is an
 *       encrypted session (encrypted = true); otherwise ZXM_E_STATE and
 *       nothing is sent.
 *   HM5 HISTORY. Each call that leaves a line ended is filed once (by its
 *       call id) as a record that fits HR5, the newest last; when
 *       HALLOO_HISTORY are kept the oldest goes. A record that does not fit
 *       HR5 is not filed.
 *   HM6 CALLING. A call is placed only to a member id 1..2^32 - 1 that is
 *       not a blocked contact, or to the loopback line (peer 0 here). */
#ifndef HALLOO_MODEL_H
#define HALLOO_MODEL_H

#include "halloo_rules.h"
#include "zxm.h"

#define HALLOO_NO_EMERGENCY                                                                        \
    "Halloo cannot reach emergency services. Call them from a phone network."
#define HALLOO_LINES       5u /* zxvd's four lines and the loopback line */
#define HALLOO_CONTACTS    64u
#define HALLOO_NAME_MAX    47u
#define HALLOO_HISTORY     64u
#define HALLOO_CALL_ID_LEN 16u
#define HALLOO_NOTICE_MAX  255u
#define HALLOO_SIGNAL_MAX  16u   /* messages call.signal takes at once */
#define HALLOO_MSG_MAX     1024u /* hex characters of one message here */

enum {
    HALLOO_IDLE = 0,
    HALLOO_OUTGOING,
    HALLOO_RINGING_OUT,
    HALLOO_INCOMING,
    HALLOO_CONNECTING,
    HALLOO_CONNECTED,
    HALLOO_HELD,
    HALLOO_ENDED,
};

typedef struct {
    int64_t peer; /* member id, 1..HALLOO_PEER_MAX */
    bool blocked;
    char name[HALLOO_NAME_MAX + 1];
} halloo_contact_t;

typedef struct {
    int32_t line; /* 0..4 */
    bool loopback;
    int32_t state;  /* HALLOO_IDLE..HALLOO_ENDED */
    int64_t peer;   /* 0 when none */
    bool outgoing;  /* dir 1 */
    bool video;     /* audio+video */
    int32_t reason; /* HALLOO_END_* */
    char call_id[HALLOO_CALL_ID_LEN + 1];
    int64_t since_ms;     /* when this model first saw the call on this line */
    int64_t connected_ms; /* when it first saw it connected, -1 never */
    bool rings;           /* HM2: the incoming call may ring */
} halloo_line_t;

typedef struct {
    int64_t seq, peer, dir, media, reason, ms, stamp; /* HR5 */
    char call_id[HALLOO_CALL_ID_LEN + 1];
} halloo_record_t;

typedef struct {
    zxm_link_t *link;
    int64_t self_id, loopback_id;
    halloo_contact_t contact[HALLOO_CONTACTS];
    uint32_t ncontacts;
    bool quiet_on;                     /* quiet hours set */
    int64_t quiet_opens, quiet_closes; /* minutes of the day */
    int64_t ring_ms;                   /* HR3 */
    halloo_line_t line[HALLOO_LINES];
    halloo_record_t history[HALLOO_HISTORY];
    uint32_t nhistory;
    int64_t next_seq;
    char notice[HALLOO_NOTICE_MAX + 1];
    char params[ZXM_REQ_MAX]; /* call.signal's params, built here */
} halloo_t;

void halloo_init(halloo_t *h, zxm_link_t *link);

/* The owner's settings. */
int halloo_add_contact(halloo_t *h, int64_t peer, const char *name);
int halloo_block(halloo_t *h, int64_t peer, bool blocked);
const halloo_contact_t *halloo_find_contact(const halloo_t *h, int64_t peer);
/* Quiet hours from minute opens up to minute closes (HR1); on false: none. */
int halloo_set_quiet(halloo_t *h, bool on, int64_t opens, int64_t closes);
int halloo_set_ring_ms(halloo_t *h, int64_t ring_ms); /* HR3 */

/* HM2: whether a call from peer may ring at minute `minute` of the day. */
bool halloo_may_ring(const halloo_t *h, int64_t peer, int64_t minute);

/* call.status: every line, measured against the view's clock now_ms (ms
 * since 1970) and minute of the day; files ended calls (HM5) and composes
 * the ringing notice (HM2). */
int halloo_refresh(halloo_t *h, int64_t now_ms, int64_t minute);
/* HM2: reject the ringing calls of blocked callers; how many in *n. */
int halloo_screen(halloo_t *h, uint32_t *n);

/* call.start (HM6); peer 0 rings the loopback line. The line in *line. */
int halloo_call(halloo_t *h, int64_t peer, bool video, int32_t *line);
int halloo_answer(halloo_t *h, int32_t line); /* call.accept */
int halloo_hangup(halloo_t *h, int32_t line); /* call.hangup */
/* call.signal (HM4): deliver nin messages (hex) and take this node's
 * outgoing ones, up to cap, each up to HALLOO_MSG_MAX hex characters. */
int halloo_signal(halloo_t *h, bool encrypted, const char *const *in, uint32_t nin,
                  char out[][HALLOO_MSG_MAX + 1], uint32_t cap, uint32_t *nout);

/* A call's length for the screen, "h:mm:ss" (HR4); ZXM_E_ARG past HR4. */
int halloo_length_text(int64_t ms, char *out, uint32_t cap);

#endif
