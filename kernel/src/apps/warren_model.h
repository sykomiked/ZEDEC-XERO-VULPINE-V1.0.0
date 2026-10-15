/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warren_model.h — Warren, services, as a model (zxm.h): zxvd's engines as
 * feature switches (features.get, features.set), the energy each consumer
 * drew and its share (power.top), the daemon itself (zxvd.version,
 * sys.info) and its recent events (log.tail). The rules are warren_rules.h
 * (WR1-WR5), the same ones the app's Sutra, KRON, Eve, BASIC and MYRA
 * blocks run. No drawing, no allocation, integers only.
 *
 *   WM1 OWNER. Whether this session is the owner's is the view's word
 *       (warren_set_owner): zxvd has no method that says whose session it
 *       is. Nothing changes outside the owner's session.
 *   WM2 A SWITCH changes only when change_ok (WR2) says so, and a regulated
 *       engine turned on only with the owner's approval too (approval_ok,
 *       WR3); otherwise ZXM_E_STATE and nothing is sent. The change is
 *       taken only when zxvd's reply names the same engine in the asked
 *       state (ZXM_E_PROTOCOL otherwise).
 *   WM3 A RESTART is off then on (zxvd has no restart). Both steps are
 *       checked before the first is sent. When the "on" fails, the engine
 *       waits for a retry: attempt k waits backoff (WR4) seconds, the view
 *       keeps the time and calls warren_retry; after the last attempt
 *       Warren gives up and says so. Defaults taken: 5 attempts, 2 seconds
 *       doubled, at most 60 (warren_set_backoff changes them).
 *   WM4 A REFRESH (zxvd.version, sys.info, features.get), an energy read
 *       (power.top) and an events read (log.tail) are each all or nothing:
 *       a reply out of bounds leaves what was shown as it was.
 *   WM5 ENERGY is per consumer, as zxvd ranks it (the system, apps and
 *       agents), each with its share of the whole the reply covers
 *       (banker.share): zxvd keeps no energy per engine.
 *   WM6 The notice is composed for the view; zxvd has no method that
 *       posts one. Warren never calls zxvd.shutdown. */
#ifndef WARREN_MODEL_H
#define WARREN_MODEL_H

#include "warren_rules.h"
#include "zxm.h"

#define WARREN_ENGINES    32u  /* zxvd_ext.h ZXVD_EXT_MAX_FEATURES */
#define WARREN_NAME_MAX   23u  /* a feature name, ^[a-z0-9_]{1,23}$ */
#define WARREN_CONSUMERS  64u  /* power.top n at most */
#define WARREN_WHO_MAX    31u  /* a consumer's name */
#define WARREN_EVENTS     64u  /* one log.tail page */
#define WARREN_TEXT_MAX   159u /* zxvd_log.h ZXVD_LOG_TEXT, its NUL aside */
#define WARREN_WORD_MAX   15u
#define WARREN_NOTICE_MAX 383u

#define WARREN_TRIES 5  /* WM3 defaults */
#define WARREN_BASE  2  /* seconds */
#define WARREN_MOST  60 /* seconds */

typedef struct {
    char name[WARREN_NAME_MAX + 1];
    bool enabled;
    bool regulated;
    uint32_t groups;    /* method groups behind the switch */
    uint32_t methods;   /* methods across them */
    uint32_t restarts;  /* restarts the owner made (WM3) */
    int64_t changed_ms; /* zxvd's clock at the owner's last change; 0 none */
} warren_engine_t;

typedef struct {
    uint32_t id;
    char kind[WARREN_WORD_MAX + 1]; /* system, app, agent */
    char name[WARREN_WHO_MAX + 1];
    uint64_t uj; /* microjoules in the last settled interval */
    uint64_t mw;
    int64_t share_x100; /* of the whole (WM5) */
} warren_consumer_t;

typedef struct {
    uint64_t seq;
    int64_t time_ms;
    char level[WARREN_WORD_MAX + 1];
    char source[WARREN_WORD_MAX + 1];
    char text[WARREN_TEXT_MAX + 1];
} warren_event_t;

typedef struct {
    bool waiting;                     /* an engine waits to come back on */
    bool gave_up;                     /* its last attempt failed too */
    char engine[WARREN_NAME_MAX + 1]; /* which (kept after giving up, for the notice) */
    uint32_t attempt;                 /* attempts made */
    int64_t wait_s;                   /* before the next, while waiting */
} warren_retry_t;

typedef struct {
    zxm_link_t *link;
    bool owner; /* WM1 */
    bool have;  /* a refresh succeeded */
    char app[WARREN_TEXT_MAX + 1];
    char protocol[WARREN_WORD_MAX + 1];
    char libzxv[WARREN_WORD_MAX + 1];
    uint64_t libzxv_features;
    int64_t time_ms; /* zxvd's clock at the last read */
    uint64_t uptime_s;
    uint64_t pid;
    uint64_t rss_bytes;
    warren_engine_t engine[WARREN_ENGINES];
    uint32_t nengines;
    bool saved; /* switch changes are kept in a books file */
    warren_consumer_t consumer[WARREN_CONSUMERS];
    uint32_t nconsumers;
    uint64_t whole_uj;
    warren_event_t event[WARREN_EVENTS];
    uint32_t nevents;
    uint64_t next;    /* log.tail since, for the next read */
    uint64_t dropped; /* events the ring no longer held */
    uint32_t tries;   /* WM3 */
    int64_t base_s, most_s;
    warren_retry_t retry;
    char notice[WARREN_NOTICE_MAX + 1];
} warren_t;

void warren_init(warren_t *w, zxm_link_t *link);
/* WM1 */
void warren_set_owner(warren_t *w, bool owner);
/* WM3: ZXM_E_ARG outside WR4's domain, or while a retry waits (ZXM_E_STATE). */
int warren_set_backoff(warren_t *w, int64_t tries, int64_t base, int64_t most);
/* WM4: zxvd.version, sys.info, features.get */
int warren_refresh(warren_t *w);
/* WM4/WM5: power.top for the n (1..64) highest consumers */
int warren_energy(warren_t *w, uint32_t n);
/* WM4: log.tail from the last read on, at most max (1..64) */
int warren_events(warren_t *w, uint32_t max);
/* WM2: turn one engine on or off; approved is the owner's confirmed word.
 * ZXM_E_ARG for an engine not listed by the last refresh. */
int warren_switch(warren_t *w, const char *name, bool enable, bool approved);
/* WM3: off then on, for an engine that is on. */
int warren_restart(warren_t *w, const char *name, bool approved);
/* WM3: the next attempt for the engine waiting; ZXM_E_STATE when none waits. */
int warren_retry(warren_t *w, bool approved);
/* WR4: the wait before attempt k of the present settings; WARREN_REFUSED
 * outside them. */
int64_t warren_wait(const warren_t *w, int64_t k);
/* WR5: whether engine i's service record fits ^ZXWARREN. */
bool warren_record(const warren_t *w, uint32_t i);
const warren_engine_t *warren_find(const warren_t *w, const char *name);

#endif
