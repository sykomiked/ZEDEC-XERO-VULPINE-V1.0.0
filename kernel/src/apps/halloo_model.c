/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* halloo_model.c — Halloo's model (halloo_model.h HM1-HM6) over zxvd's
 * call.start, call.accept, call.hangup, call.status and call.signal. */
#include "halloo_model.h"

#include <stdio.h>
#include <string.h>

static const char *const STATES[] = {"idle",       "outgoing",  "ringing_out", "incoming",
                                     "connecting", "connected", "held",        "ended"};
static const char *const REASONS[] = {"none",      "local_hangup", "remote_hangup", "rejected",
                                      "no_answer", "no_response",  "ice_failed",    "timeout",
                                      "busy",      "incompatible", "cancelled"};

static int32_t word_index(const char *w, const char *const *list, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(w, list[i])) return (int32_t) i;
    return -1;
}

static void say(halloo_t *h, const char *text)
{
    snprintf(h->notice, sizeof h->notice, "%s", text);
}

void halloo_init(halloo_t *h, zxm_link_t *link)
{
    memset(h, 0, sizeof *h);
    h->link = link;
    h->ring_ms = HALLOO_RING_MAX;
    h->next_seq = 1;
    for (uint32_t i = 0; i < HALLOO_LINES; i++) {
        h->line[i].line = (int32_t) i;
        h->line[i].connected_ms = -1;
    }
}

/* ----- the owner's settings ----- */

const halloo_contact_t *halloo_find_contact(const halloo_t *h, int64_t peer)
{
    for (uint32_t i = 0; i < h->ncontacts; i++)
        if (h->contact[i].peer == peer) return &h->contact[i];
    return NULL;
}

int halloo_add_contact(halloo_t *h, int64_t peer, const char *name)
{
    if (!h || !name || peer < 1 || peer > HALLOO_PEER_MAX) return ZXM_E_ARG;
    size_t n = strlen(name);
    if (n == 0 || n > HALLOO_NAME_MAX) return ZXM_E_ARG;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) name[i] < 0x20 || name[i] == 0x7f) return ZXM_E_ARG;
    if (halloo_find_contact(h, peer) || h->ncontacts >= HALLOO_CONTACTS) return ZXM_E_STATE;
    halloo_contact_t *c = &h->contact[h->ncontacts++];
    c->peer = peer;
    c->blocked = false;
    memcpy(c->name, name, n);
    c->name[n] = 0;
    return ZXM_OK;
}

int halloo_block(halloo_t *h, int64_t peer, bool blocked)
{
    if (!h) return ZXM_E_ARG;
    halloo_contact_t *c = (halloo_contact_t *) halloo_find_contact(h, peer);
    if (!c) return ZXM_E_STATE;
    c->blocked = blocked;
    return ZXM_OK;
}

int halloo_set_quiet(halloo_t *h, bool on, int64_t opens, int64_t closes)
{
    if (!h) return ZXM_E_ARG;
    if (on && (!halloo_minute(opens) || !halloo_minute(closes))) return ZXM_E_ARG;
    h->quiet_on = on;
    h->quiet_opens = on ? opens : 0;
    h->quiet_closes = on ? closes : 0;
    return ZXM_OK;
}

int halloo_set_ring_ms(halloo_t *h, int64_t ring_ms)
{
    if (!h || !halloo_ring_wait_ok(ring_ms)) return ZXM_E_ARG;
    h->ring_ms = ring_ms;
    return ZXM_OK;
}

bool halloo_may_ring(const halloo_t *h, int64_t peer, int64_t minute)
{
    if (!h || !halloo_minute(minute)) return false;
    const halloo_contact_t *c = halloo_find_contact(h, peer);
    int64_t quiet = h->quiet_on ? halloo_quiet_left(h->quiet_opens, h->quiet_closes, minute) : 0;
    if (quiet < 0) return false;
    return halloo_ring_ok(c ? 1 : 0, c && c->blocked ? 1 : 0, quiet);
}

/* ----- zxvd ----- */

/* HM5: file a call that ended, once by its call id. */
static void file_call(halloo_t *h, const halloo_line_t *ln, int64_t now_ms)
{
    if (!ln->call_id[0] || ln->peer < 1) return;
    for (uint32_t i = 0; i < h->nhistory; i++)
        if (!strcmp(h->history[i].call_id, ln->call_id)) return;
    halloo_record_t r;
    memset(&r, 0, sizeof r);
    r.seq = h->next_seq;
    r.peer = ln->peer;
    r.dir = ln->outgoing ? 1 : 0;
    r.media = ln->video ? 1 : 0;
    r.reason = ln->reason;
    r.ms = ln->connected_ms >= 0 && now_ms >= ln->connected_ms ? now_ms - ln->connected_ms : 0;
    r.stamp = now_ms;
    memcpy(r.call_id, ln->call_id, sizeof r.call_id);
    if (!halloo_call_ok(r.seq, r.peer, r.dir, r.media, r.reason, r.ms, r.stamp)) return;
    if (h->nhistory == HALLOO_HISTORY) {
        memmove(&h->history[0], &h->history[1], sizeof h->history[0] * (HALLOO_HISTORY - 1u));
        h->nhistory--;
    }
    h->history[h->nhistory++] = r;
    h->next_seq = h->next_seq < HALLOO_SEQ_MAX ? h->next_seq + 1 : 1;
}

/* One line object of call.status into ln; false when it is not one. */
static bool read_line(const zxm_link_t *l, int32_t o, halloo_line_t *ln)
{
    char st[16], media[16], why[16], id[HALLOO_CALL_ID_LEN + 2];
    uint64_t line = 0, peer = 0;
    bool loop = false, out = false;
    if (o < 0 || zxm_get_u64(l, o, "line", &line) != ZXM_OK || line >= HALLOO_LINES ||
        zxm_get_str(l, o, "state", st, sizeof st) != ZXM_OK)
        return false;
    int32_t s = word_index(st, STATES, 8u);
    if (s < 0) return false;
    memset(ln, 0, sizeof *ln);
    ln->line = (int32_t) line;
    ln->state = s;
    if (zxm_get_u64(l, o, "peer", &peer) == ZXM_OK && peer <= (uint64_t) HALLOO_PEER_MAX)
        ln->peer = (int64_t) peer;
    if (zxm_get_bool(l, o, "loopback", &loop) == ZXM_OK) ln->loopback = loop;
    if (zxm_get_bool(l, o, "outgoing", &out) == ZXM_OK) ln->outgoing = out;
    if (zxm_get_str(l, o, "media", media, sizeof media) == ZXM_OK)
        ln->video = !strcmp(media, "audio+video");
    if (zxm_get_str(l, o, "end_reason", why, sizeof why) == ZXM_OK) {
        int32_t r = word_index(why, REASONS, 11u);
        ln->reason = r < 0 ? HALLOO_END_NONE : r;
    }
    if (zxm_get_str(l, o, "call_id", id, sizeof id) == ZXM_OK && strlen(id) == HALLOO_CALL_ID_LEN)
        memcpy(ln->call_id, id, sizeof ln->call_id);
    return true;
}

int halloo_refresh(halloo_t *h, int64_t now_ms, int64_t minute)
{
    if (!h || !h->link || now_ms < 0 || now_ms > HALLOO_STAMP_MAX || !halloo_minute(minute))
        return ZXM_E_ARG;
    zxm_link_t *l = h->link;
    zxm_begin(l, "call.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "lines");
    uint32_t n = arr < 0 ? 0 : zxm_len(l, arr);
    if (arr < 0 || n > HALLOO_LINES) return ZXM_E_PROTOCOL;
    halloo_line_t fresh[HALLOO_LINES];
    bool seen[HALLOO_LINES] = {false};
    for (uint32_t i = 0; i < n; i++) {
        halloo_line_t ln;
        if (!read_line(l, zxm_at(l, arr, i), &ln) || seen[ln.line]) return ZXM_E_PROTOCOL;
        seen[ln.line] = true;
        fresh[ln.line] = ln;
    }
    int64_t self = 0, loop = 0;
    (void) zxm_get_i64(l, l->result, "self_id", &self);
    (void) zxm_get_i64(l, l->result, "loopback_id", &loop);
    /* checked whole: only now is the model changed */
    h->self_id = self;
    h->loopback_id = loop;
    h->notice[0] = 0;
    for (uint32_t i = 0; i < HALLOO_LINES; i++) {
        halloo_line_t *old = &h->line[i];
        halloo_line_t ln;
        if (seen[i]) {
            ln = fresh[i];
        } else {
            memset(&ln, 0, sizeof ln);
            ln.line = (int32_t) i;
        }
        bool same = ln.call_id[0] && !strcmp(ln.call_id, old->call_id);
        ln.since_ms = same ? old->since_ms : now_ms;
        ln.connected_ms = same ? old->connected_ms : -1;
        if ((ln.state == HALLOO_CONNECTED || ln.state == HALLOO_HELD) && ln.connected_ms < 0)
            ln.connected_ms = now_ms;
        ln.rings = ln.state == HALLOO_INCOMING && halloo_may_ring(h, ln.peer, minute) &&
                   now_ms - ln.since_ms < h->ring_ms;
        if (ln.state == HALLOO_ENDED) file_call(h, &ln, now_ms);
        if (ln.rings && !h->notice[0]) {
            const halloo_contact_t *c = halloo_find_contact(h, ln.peer);
            snprintf(h->notice, sizeof h->notice, "%s is calling (%s).", c ? c->name : "Someone",
                     ln.video ? "video" : "voice");
        }
        *old = ln;
    }
    return ZXM_OK;
}

int halloo_screen(halloo_t *h, uint32_t *n)
{
    if (!h || !h->link || !n) return ZXM_E_ARG;
    *n = 0;
    for (uint32_t i = 0; i < HALLOO_LINES; i++) {
        halloo_line_t *ln = &h->line[i];
        const halloo_contact_t *c = halloo_find_contact(h, ln->peer);
        if (ln->state != HALLOO_INCOMING || !c || !c->blocked) continue;
        int rc = halloo_hangup(h, ln->line);
        if (rc != ZXM_OK) return rc;
        (*n)++;
    }
    if (*n) say(h, "A blocked caller was turned away.");
    return ZXM_OK;
}

/* call.start, call.accept and call.hangup answer one line */
static int line_result(halloo_t *h, int32_t *line)
{
    halloo_line_t ln;
    zxm_link_t *l = h->link;
    if (!read_line(l, l->result, &ln)) return ZXM_E_PROTOCOL;
    halloo_line_t *old = &h->line[ln.line];
    bool same = ln.call_id[0] && !strcmp(ln.call_id, old->call_id);
    ln.since_ms = same ? old->since_ms : 0;
    ln.connected_ms = same ? old->connected_ms : -1;
    ln.rings = false;
    *old = ln;
    if (line) *line = ln.line;
    return ZXM_OK;
}

int halloo_call(halloo_t *h, int64_t peer, bool video, int32_t *line)
{
    if (!h || !h->link || peer < 0 || peer > HALLOO_PEER_MAX) return ZXM_E_ARG;
    const halloo_contact_t *c = halloo_find_contact(h, peer);
    if (c && c->blocked) return ZXM_E_STATE;
    zxm_link_t *l = h->link;
    zxm_begin(l, "call.start");
    if (peer == 0)
        zxm_p_str(l, "peer", "loopback");
    else
        zxm_p_u64(l, "peer", (uint64_t) peer);
    zxm_p_str(l, "media", video ? "video" : "audio");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    return line_result(h, line);
}

static int on_line(halloo_t *h, const char *method, int32_t line)
{
    if (!h || !h->link || line < 0 || line >= (int32_t) HALLOO_LINES) return ZXM_E_ARG;
    zxm_begin(h->link, method);
    zxm_p_u64(h->link, "line", (uint64_t) line);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    return line_result(h, NULL);
}

int halloo_answer(halloo_t *h, int32_t line)
{
    return on_line(h, "call.accept", line);
}

int halloo_hangup(halloo_t *h, int32_t line)
{
    return on_line(h, "call.hangup", line);
}

static bool is_hex_msg(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n % 2u || n > HALLOO_MSG_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

int halloo_signal(halloo_t *h, bool encrypted, const char *const *in, uint32_t nin,
                  char out[][HALLOO_MSG_MAX + 1], uint32_t cap, uint32_t *nout)
{
    if (!h || !h->link || !nout || (nin && !in) || (cap && !out) || nin > HALLOO_SIGNAL_MAX)
        return ZXM_E_ARG;
    *nout = 0;
    if (!encrypted) {
        say(h, "Call signals travel only inside an encrypted session.");
        return ZXM_E_STATE;
    }
    for (uint32_t i = 0; i < nin; i++)
        if (!in[i] || !is_hex_msg(in[i])) return ZXM_E_ARG;
    size_t used = 0, room = sizeof h->params;
    int w = snprintf(h->params, room, "{\"in\":[");
    used = (size_t) w;
    for (uint32_t i = 0; i < nin; i++) {
        w = snprintf(h->params + used, room - used, "%s\"%s\"", i ? "," : "", in[i]);
        if (w < 0 || (size_t) w >= room - used) return ZXM_E_TOOBIG;
        used += (size_t) w;
    }
    w = snprintf(h->params + used, room - used, "]}");
    if (w < 0 || (size_t) w >= room - used) return ZXM_E_TOOBIG;
    used += (size_t) w;
    zxm_link_t *l = h->link;
    zxm_begin(l, "call.signal");
    zxm_p_raw(l, h->params, (uint32_t) used);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "out");
    uint32_t n = arr < 0 ? 0 : zxm_len(l, arr);
    if (arr < 0 || n > cap) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t t = zxm_at(l, arr, i);
        if (t < 0 || l->tok[t].type != W4_J_STR) return ZXM_E_PROTOCOL;
        uint32_t len = l->tok[t].end - l->tok[t].start;
        if (len > HALLOO_MSG_MAX) return ZXM_E_PROTOCOL;
        memcpy(out[i], l->resp + l->tok[t].start, len);
        out[i][len] = 0;
        if (!is_hex_msg(out[i])) return ZXM_E_PROTOCOL;
    }
    *nout = n;
    return ZXM_OK;
}

int halloo_length_text(int64_t ms, char *out, uint32_t cap)
{
    int64_t hms = halloo_hms(ms);
    if (hms < 0 || !out || cap == 0) return ZXM_E_ARG;
    int w = snprintf(out, cap, "%lld:%02lld:%02lld", (long long) (hms / 10000),
                     (long long) (hms / 100 % 100), (long long) (hms % 100));
    if (w < 0 || (uint32_t) w >= cap) return ZXM_E_TOOBIG;
    return ZXM_OK;
}
