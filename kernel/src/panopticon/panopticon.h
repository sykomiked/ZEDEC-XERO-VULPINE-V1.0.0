/*
 * panopticon.h — Real-time surveillance awareness & friendliness rating
 *
 * Monitors incoming connections, tracks who is watching the user,
 * rates entities on a friendliness scale, and provides real-time
 * transparency about what the Panopticon itself is doing.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 — Streisand Engine License
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#ifndef PANOPTICON_H
#define PANOPTICON_H

#include "m5_types.h"

#define PANOPTICON_MAX_WATCHERS   128
#define PANOPTICON_MAX_EVENTS     256
#define PANOPTICON_NAME_LEN       64
#define PANOPTICON_DESC_LEN       256
#define PANOPTICON_MAX_LOG        512

/* Friendliness scale: -100 (hostile) to +100 (trusted) */
#define PANOPTICON_FRIENDLY_MIN   (-100)
#define PANOPTICON_FRIENDLY_MAX   100
#define PANOPTICON_NEUTRAL        0

/* Watcher categories */
typedef enum {
    WATCHER_UNKNOWN     = 0,
    WATCHER_LOCAL_USER  = 1,
    WATCHER_P2P_PEER    = 2,
    WATCHER_NETWORK     = 3,
    WATCHER_SYSTEM      = 4,
    WATCHER_EXTERNAL    = 5,
    WATCHER_GOVERNMENT  = 6,
    WATCHER_CORPORATE   = 7,
    WATCHER_ADMIRALTY   = 8,
} panopticon_watcher_type_t;

/* Threat levels */
typedef enum {
    THREAT_NONE     = 0,
    THREAT_LOW      = 1,
    THREAT_MEDIUM   = 2,
    THREAT_HIGH     = 3,
    THREAT_CRITICAL = 4,
} panopticon_threat_t;

/* What the watcher is doing */
typedef enum {
    ACTION_OBSERVE       = 0,
    ACTION_PROBE         = 1,
    ACTION_SCAN          = 2,
    ACTION_CONNECT       = 3,
    ACTION_DATA_REQUEST  = 4,
    ACTION_FINGERPRINT   = 5,
    ACTION_TRACK         = 6,
    ACTION_INJECT        = 7,
    ACTION_MITM          = 8,
} panopticon_action_t;

/* A watcher entry */
typedef struct {
    uint32_t id;
    uint8_t  ip[4];
    uint16_t port;
    uint8_t  pubkey[32];
    panopticon_watcher_type_t type;
    int8_t   friendliness;       /* -100 to +100 */
    panopticon_threat_t threat;
    panopticon_action_t last_action;
    uint64_t first_seen;         /* phase tick */
    uint64_t last_seen;          /* phase tick */
    uint32_t event_count;
    uint8_t  name[PANOPTICON_NAME_LEN];
    uint8_t  description[PANOPTICON_DESC_LEN];
    uint8_t  active;
    uint8_t  watching_you;       /* 1 if currently watching */
} panopticon_watcher_t;

/* Event log entry */
typedef struct {
    uint64_t timestamp;          /* phase tick */
    uint32_t watcher_id;
    panopticon_action_t action;
    uint8_t  detail[PANOPTICON_DESC_LEN];
    int8_t   friendliness_delta;
} panopticon_event_t;

/* Panopticon state */
typedef struct {
    panopticon_watcher_t watchers[PANOPTICON_MAX_WATCHERS];
    panopticon_event_t   events[PANOPTICON_MAX_EVENTS];
    uint32_t watcher_count;
    uint32_t event_count;
    uint32_t event_head;         /* ring buffer */
    uint64_t phase_tick;
    /* Self-reporting: what the Panopticon itself is doing */
    uint8_t  self_active;
    uint8_t  self_scanning;
    uint8_t  self_logging;
    uint8_t  self_reporting;
    uint8_t  self_description[PANOPTICON_DESC_LEN];
} panopticon_state_t;

/* ============================================================
 * Core API
 * ============================================================ */

int panopticon_init(panopticon_state_t *state);
int panopticon_tick(panopticon_state_t *state);

/* Register a watcher */
int panopticon_register_watcher(panopticon_state_t *state,
                                uint32_t ip, uint16_t port,
                                panopticon_watcher_type_t type,
                                const char *name);
int panopticon_update_watcher(panopticon_state_t *state, uint32_t id,
                              panopticon_action_t action,
                              const char *detail);
int panopticon_remove_watcher(panopticon_state_t *state, uint32_t id);

/* Friendliness rating */
int panopticon_rate_friendliness(panopticon_state_t *state, uint32_t id,
                                 int8_t delta);
int8_t panopticon_get_friendliness(panopticon_state_t *state, uint32_t id);
panopticon_threat_t panopticon_get_threat(panopticon_state_t *state, uint32_t id);

/* Classify watcher type from behavior */
panopticon_watcher_type_t panopticon_classify(uint32_t ip, uint16_t port,
                                              panopticon_action_t action);
const char *panopticon_watcher_type_name(panopticon_watcher_type_t type);
const char *panopticon_threat_name(panopticon_threat_t threat);
const char *panopticon_action_name(panopticon_action_t action);

/* Friendliness label */
const char *panopticon_friendliness_label(int8_t score);

/* Event log */
int panopticon_log_event(panopticon_state_t *state, uint32_t watcher_id,
                         panopticon_action_t action, const char *detail,
                         int8_t friendliness_delta);
int panopticon_get_events(panopticon_state_t *state,
                          panopticon_event_t *events, int max,
                          uint64_t since_tick);

/* Real-time status report — what is the Panopticon doing right now? */
int panopticon_self_report(panopticon_state_t *state,
                           char *buf, uint16_t buf_len);
int panopticon_set_self_description(panopticon_state_t *state,
                                    const char *description);

/* Full watcher report — who's watching and why */
int panopticon_watcher_report(panopticon_state_t *state, uint32_t id,
                              char *buf, uint16_t buf_len);

/* List all active watchers */
int panopticon_list_watchers(panopticon_state_t *state,
                             panopticon_watcher_t *watchers, int max);

/* Summary: how many watchers, average friendliness, threat level */
int panopticon_summary(panopticon_state_t *state,
                       uint32_t *total_watchers,
                       uint32_t *active_watchers,
                       int8_t *avg_friendliness,
                       panopticon_threat_t *max_threat);

/* Alert: check for critical threats and generate alert text */
int panopticon_check_alerts(panopticon_state_t *state,
                            char *alert_buf, uint16_t buf_len);

/* Auto-rate: automatically adjust friendliness based on action patterns */
int panopticon_auto_rate(panopticon_state_t *state, uint32_t id);

#endif /* PANOPTICON_H */
