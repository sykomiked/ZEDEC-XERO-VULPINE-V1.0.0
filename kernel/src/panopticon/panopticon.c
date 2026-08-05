/*
 * panopticon.c — Real-time surveillance awareness & friendliness rating
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "panopticon.h"
#include "../include/freestanding.h"

static uint32_t next_watcher_id = 1;

static int8_t clamp_friendliness(int8_t val) {
    if (val < PANOPTICON_FRIENDLY_MIN) return PANOPTICON_FRIENDLY_MIN;
    if (val > PANOPTICON_FRIENDLY_MAX) return PANOPTICON_FRIENDLY_MAX;
    return val;
}

static void safe_strncpy(uint8_t *dst, const char *src, int max) {
    int i = 0;
    if (src) {
        while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    }
    dst[i] = 0;
}

int panopticon_init(panopticon_state_t *state) {
    if (!state) return -1;
    fs_memset(state, 0, sizeof(*state));
    state->self_active = 1;
    state->self_scanning = 1;
    state->self_logging = 1;
    state->self_reporting = 1;
    safe_strncpy(state->self_description,
        "Panopticon: Active surveillance awareness system. Monitoring all "
        "incoming connections, classifying watchers, rating friendliness, "
        "and providing real-time transparency about observation activities.",
        PANOPTICON_DESC_LEN);
    return 0;
}

int panopticon_tick(panopticon_state_t *state) {
    if (!state) return -1;
    state->phase_tick++;
    /* Decay friendliness slightly for inactive watchers */
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        panopticon_watcher_t *w = &state->watchers[i];
        if (!w->active) continue;
        if (state->phase_tick - w->last_seen > 100) {
            w->watching_you = 0;
        }
    }
    return 0;
}

int panopticon_register_watcher(panopticon_state_t *state,
                                uint32_t ip, uint16_t port,
                                panopticon_watcher_type_t type,
                                const char *name) {
    if (!state) return -1;
    /* Check if already registered */
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active &&
            state->watchers[i].ip[0] == (ip >> 24) & 0xFF &&
            state->watchers[i].ip[1] == (ip >> 16) & 0xFF &&
            state->watchers[i].ip[2] == (ip >> 8) & 0xFF &&
            state->watchers[i].ip[3] == ip & 0xFF &&
            state->watchers[i].port == port) {
            state->watchers[i].last_seen = state->phase_tick;
            state->watchers[i].watching_you = 1;
            return state->watchers[i].id;
        }
    }
    /* Find free slot */
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (!state->watchers[i].active) {
            panopticon_watcher_t *w = &state->watchers[i];
            fs_memset(w, 0, sizeof(*w));
            w->id = next_watcher_id++;
            w->ip[0] = (ip >> 24) & 0xFF;
            w->ip[1] = (ip >> 16) & 0xFF;
            w->ip[2] = (ip >> 8) & 0xFF;
            w->ip[3] = ip & 0xFF;
            w->port = port;
            w->type = type;
            w->friendliness = PANOPTICON_NEUTRAL;
            w->threat = THREAT_NONE;
            w->last_action = ACTION_OBSERVE;
            w->first_seen = state->phase_tick;
            w->last_seen = state->phase_tick;
            w->event_count = 0;
            w->active = 1;
            w->watching_you = 1;
            safe_strncpy(w->name, name ? name : "Unknown", PANOPTICON_NAME_LEN);
            switch (type) {
                case WATCHER_LOCAL_USER:  w->friendliness = 50; break;
                case WATCHER_P2P_PEER:    w->friendliness = 20; break;
                case WATCHER_SYSTEM:      w->friendliness = 30; break;
                case WATCHER_NETWORK:     w->friendliness = -10; break;
                case WATCHER_EXTERNAL:    w->friendliness = -20; break;
                case WATCHER_GOVERNMENT:  w->friendliness = -50; break;
                case WATCHER_CORPORATE:   w->friendliness = -40; break;
                case WATCHER_ADMIRALTY:   w->friendliness = 60; break;
                default: break;
            }
            state->watcher_count++;
            return w->id;
        }
    }
    return -1;
}

int panopticon_update_watcher(panopticon_state_t *state, uint32_t id,
                              panopticon_action_t action,
                              const char *detail) {
    if (!state) return -1;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active && state->watchers[i].id == id) {
            panopticon_watcher_t *w = &state->watchers[i];
            w->last_action = action;
            w->last_seen = state->phase_tick;
            w->watching_you = 1;
            w->event_count++;
            if (detail) {
                safe_strncpy(w->description, detail, PANOPTICON_DESC_LEN);
            }
            /* Auto-adjust friendliness based on action */
            int8_t delta = 0;
            switch (action) {
                case ACTION_OBSERVE:      delta = -1; break;
                case ACTION_PROBE:        delta = -5; break;
                case ACTION_SCAN:         delta = -10; break;
                case ACTION_CONNECT:      delta = -2; break;
                case ACTION_DATA_REQUEST: delta = -3; break;
                case ACTION_FINGERPRINT:  delta = -15; break;
                case ACTION_TRACK:        delta = -20; break;
                case ACTION_INJECT:       delta = -40; break;
                case ACTION_MITM:         delta = -50; break;
            }
            w->friendliness = clamp_friendliness(w->friendliness + delta);
            /* Update threat level */
            if (w->friendliness <= -80) w->threat = THREAT_CRITICAL;
            else if (w->friendliness <= -50) w->threat = THREAT_HIGH;
            else if (w->friendliness <= -20) w->threat = THREAT_MEDIUM;
            else if (w->friendliness < 0) w->threat = THREAT_LOW;
            else w->threat = THREAT_NONE;
            panopticon_log_event(state, id, action, detail, delta);
            return 0;
        }
    }
    return -1;
}

int panopticon_remove_watcher(panopticon_state_t *state, uint32_t id) {
    if (!state) return -1;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active && state->watchers[i].id == id) {
            state->watchers[i].active = 0;
            state->watchers[i].watching_you = 0;
            if (state->watcher_count > 0) state->watcher_count--;
            return 0;
        }
    }
    return -1;
}

int panopticon_rate_friendliness(panopticon_state_t *state, uint32_t id,
                                 int8_t delta) {
    if (!state) return -1;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active && state->watchers[i].id == id) {
            state->watchers[i].friendliness =
                clamp_friendliness(state->watchers[i].friendliness + delta);
            return 0;
        }
    }
    return -1;
}

int8_t panopticon_get_friendliness(panopticon_state_t *state, uint32_t id) {
    if (!state) return 0;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active && state->watchers[i].id == id)
            return state->watchers[i].friendliness;
    }
    return 0;
}

panopticon_threat_t panopticon_get_threat(panopticon_state_t *state, uint32_t id) {
    if (!state) return THREAT_NONE;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active && state->watchers[i].id == id)
            return state->watchers[i].threat;
    }
    return THREAT_NONE;
}

panopticon_watcher_type_t panopticon_classify(uint32_t ip, uint16_t port,
                                              panopticon_action_t action) {
    /* Private ranges = local/system */
    uint8_t a = (ip >> 24) & 0xFF;
    if (a == 10 || a == 127) return WATCHER_LOCAL_USER;
    if (a == 192 && ((ip >> 16) & 0xFF) == 168) return WATCHER_NETWORK;
    if (a == 172 && ((ip >> 16) & 0xFF) >= 16 && ((ip >> 16) & 0xFF) <= 31)
        return WATCHER_NETWORK;
    /* Port-based heuristics */
    if (port == 22 || port == 23) return WATCHER_EXTERNAL;
    if (action == ACTION_SCAN || action == ACTION_FINGERPRINT)
        return WATCHER_EXTERNAL;
    if (action == ACTION_INJECT || action == ACTION_MITM)
        return WATCHER_GOVERNMENT;
    return WATCHER_UNKNOWN;
}

const char *panopticon_watcher_type_name(panopticon_watcher_type_t type) {
    switch (type) {
        case WATCHER_LOCAL_USER:  return "Local User";
        case WATCHER_P2P_PEER:    return "P2P Peer";
        case WATCHER_NETWORK:     return "Network Node";
        case WATCHER_SYSTEM:      return "System Service";
        case WATCHER_EXTERNAL:    return "External Entity";
        case WATCHER_GOVERNMENT:  return "Government Actor";
        case WATCHER_CORPORATE:   return "Corporate Actor";
        case WATCHER_ADMIRALTY:   return "Admiralty/Sovereign";
        default: return "Unknown";
    }
}

const char *panopticon_threat_name(panopticon_threat_t threat) {
    switch (threat) {
        case THREAT_NONE:     return "None";
        case THREAT_LOW:      return "Low";
        case THREAT_MEDIUM:   return "Medium";
        case THREAT_HIGH:     return "High";
        case THREAT_CRITICAL: return "CRITICAL";
        default: return "Unknown";
    }
}

const char *panopticon_action_name(panopticon_action_t action) {
    switch (action) {
        case ACTION_OBSERVE:      return "Observing";
        case ACTION_PROBE:        return "Probing";
        case ACTION_SCAN:         return "Scanning";
        case ACTION_CONNECT:      return "Connecting";
        case ACTION_DATA_REQUEST: return "Requesting Data";
        case ACTION_FINGERPRINT:  return "Fingerprinting";
        case ACTION_TRACK:        return "Tracking";
        case ACTION_INJECT:       return "Injecting";
        case ACTION_MITM:         return "MITM Attack";
        default: return "Unknown";
    }
}

const char *panopticon_friendliness_label(int8_t score) {
    if (score >= 75) return "Trusted Ally";
    if (score >= 50) return "Friendly";
    if (score >= 25) return "Cooperative";
    if (score >= 0)  return "Neutral";
    if (score >= -25) return "Suspicious";
    if (score >= -50) return "Hostile";
    if (score >= -75) return "Adversarial";
    return "Hostile Threat";
}

int panopticon_log_event(panopticon_state_t *state, uint32_t watcher_id,
                         panopticon_action_t action, const char *detail,
                         int8_t friendliness_delta) {
    if (!state) return -1;
    panopticon_event_t *e = &state->events[state->event_head];
    e->timestamp = state->phase_tick;
    e->watcher_id = watcher_id;
    e->action = action;
    e->friendliness_delta = friendliness_delta;
    safe_strncpy(e->detail, detail ? detail : "", PANOPTICON_DESC_LEN);
    state->event_head = (state->event_head + 1) % PANOPTICON_MAX_EVENTS;
    if (state->event_count < PANOPTICON_MAX_EVENTS) state->event_count++;
    return 0;
}

int panopticon_get_events(panopticon_state_t *state,
                          panopticon_event_t *events, int max,
                          uint64_t since_tick) {
    if (!state || !events) return 0;
    int count = 0;
    uint32_t start = state->event_count < PANOPTICON_MAX_EVENTS ?
        0 : state->event_head;
    for (uint32_t i = 0; i < state->event_count && count < max; i++) {
        uint32_t idx = (start + i) % PANOPTICON_MAX_EVENTS;
        if (state->events[idx].timestamp >= since_tick) {
            events[count] = state->events[idx];
            count++;
        }
    }
    return count;
}

int panopticon_self_report(panopticon_state_t *state,
                           char *buf, uint16_t buf_len) {
    if (!state || !buf) return -1;
    int pos = 0;
    pos += fs_snprintf(buf + pos, buf_len - pos,
        "=== PANOPTICON STATUS REPORT ===\n"
        "Phase Tick: %llu\n"
        "Active: %s | Scanning: %s | Logging: %s | Reporting: %s\n"
        "Total Watchers: %u | Active Watchers: %u\n"
        "Event Log: %u entries\n"
        "Description: %s\n",
        (unsigned long long)state->phase_tick,
        state->self_active ? "YES" : "NO",
        state->self_scanning ? "YES" : "NO",
        state->self_logging ? "YES" : "NO",
        state->self_reporting ? "YES" : "NO",
        state->watcher_count, state->watcher_count, state->event_count,
        state->self_description);
    return pos;
}

int panopticon_set_self_description(panopticon_state_t *state,
                                    const char *description) {
    if (!state || !description) return -1;
    safe_strncpy(state->self_description, description, PANOPTICON_DESC_LEN);
    return 0;
}

int panopticon_watcher_report(panopticon_state_t *state, uint32_t id,
                              char *buf, uint16_t buf_len) {
    if (!state || !buf) return -1;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        panopticon_watcher_t *w = &state->watchers[i];
        if (w->active && w->id == id) {
            int pos = 0;
            pos += fs_snprintf(buf + pos, buf_len - pos,
                "=== WATCHER REPORT #%u ===\n"
                "Name: %s\n"
                "IP: %u.%u.%u.%u:%u\n"
                "Type: %s\n"
                "Friendliness: %d (%s)\n"
                "Threat Level: %s\n"
                "Last Action: %s\n"
                "Events: %u\n"
                "First Seen: tick %llu\n"
                "Last Seen: tick %llu\n"
                "Currently Watching: %s\n"
                "Description: %s\n",
                w->id, w->name,
                w->ip[0], w->ip[1], w->ip[2], w->ip[3], w->port,
                panopticon_watcher_type_name(w->type),
                w->friendliness,
                panopticon_friendliness_label(w->friendliness),
                panopticon_threat_name(w->threat),
                panopticon_action_name(w->last_action),
                w->event_count,
                (unsigned long long)w->first_seen,
                (unsigned long long)w->last_seen,
                w->watching_you ? "YES" : "NO",
                w->description);
            return pos;
        }
    }
    return fs_snprintf(buf, buf_len, "Watcher #%u not found\n", id);
}

int panopticon_list_watchers(panopticon_state_t *state,
                             panopticon_watcher_t *watchers, int max) {
    if (!state || !watchers) return 0;
    int count = 0;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS && count < max; i++) {
        if (state->watchers[i].active) {
            watchers[count] = state->watchers[i];
            count++;
        }
    }
    return count;
}

int panopticon_summary(panopticon_state_t *state,
                       uint32_t *total_watchers,
                       uint32_t *active_watchers,
                       int8_t *avg_friendliness,
                       panopticon_threat_t *max_threat) {
    if (!state) return -1;
    uint32_t active = 0;
    int32_t sum = 0;
    panopticon_threat_t max_t = THREAT_NONE;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        if (state->watchers[i].active) {
            active++;
            sum += state->watchers[i].friendliness;
            if (state->watchers[i].threat > max_t)
                max_t = state->watchers[i].threat;
        }
    }
    if (total_watchers) *total_watchers = state->watcher_count;
    if (active_watchers) *active_watchers = active;
    if (avg_friendliness) *avg_friendliness = active > 0 ? (int8_t)(sum / active) : 0;
    if (max_threat) *max_threat = max_t;
    return 0;
}

int panopticon_check_alerts(panopticon_state_t *state,
                            char *alert_buf, uint16_t buf_len) {
    if (!state || !alert_buf) return 0;
    int pos = 0;
    int alert_count = 0;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        panopticon_watcher_t *w = &state->watchers[i];
        if (!w->active || !w->watching_you) continue;
        if (w->threat >= THREAT_HIGH) {
            pos += fs_snprintf(alert_buf + pos, buf_len - pos,
                "[ALERT] %s (ID:%u) — Threat: %s | Friendliness: %d (%s) | "
                "Action: %s | IP: %u.%u.%u.%u\n",
                w->name, w->id,
                panopticon_threat_name(w->threat),
                w->friendliness,
                panopticon_friendliness_label(w->friendliness),
                panopticon_action_name(w->last_action),
                w->ip[0], w->ip[1], w->ip[2], w->ip[3]);
            alert_count++;
        }
    }
    if (alert_count == 0) {
        pos = fs_snprintf(alert_buf, buf_len, "No critical threats detected.\n");
    }
    return alert_count;
}

int panopticon_auto_rate(panopticon_state_t *state, uint32_t id) {
    if (!state) return -1;
    for (uint32_t i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        panopticon_watcher_t *w = &state->watchers[i];
        if (w->active && w->id == id) {
            /* Recalculate based on event count and last action */
            int8_t base = 0;
            switch (w->type) {
                case WATCHER_LOCAL_USER:  base = 50; break;
                case WATCHER_P2P_PEER:    base = 20; break;
                case WATCHER_SYSTEM:      base = 30; break;
                case WATCHER_ADMIRALTY:   base = 60; break;
                case WATCHER_NETWORK:     base = -10; break;
                case WATCHER_EXTERNAL:    base = -20; break;
                case WATCHER_CORPORATE:   base = -40; break;
                case WATCHER_GOVERNMENT:  base = -50; break;
                default: base = 0; break;
            }
            /* Penalize for hostile actions */
            switch (w->last_action) {
                case ACTION_SCAN:        base -= 10; break;
                case ACTION_FINGERPRINT: base -= 15; break;
                case ACTION_TRACK:       base -= 20; break;
                case ACTION_INJECT:      base -= 40; break;
                case ACTION_MITM:        base -= 50; break;
                default: break;
            }
            /* Penalize for volume of events */
            if (w->event_count > 50) base -= 10;
            if (w->event_count > 100) base -= 10;
            w->friendliness = clamp_friendliness(base);
            if (w->friendliness <= -80) w->threat = THREAT_CRITICAL;
            else if (w->friendliness <= -50) w->threat = THREAT_HIGH;
            else if (w->friendliness <= -20) w->threat = THREAT_MEDIUM;
            else if (w->friendliness < 0) w->threat = THREAT_LOW;
            else w->threat = THREAT_NONE;
            return 0;
        }
    }
    return -1;
}
