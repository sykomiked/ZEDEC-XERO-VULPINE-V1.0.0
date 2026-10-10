/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_notify.c — the system notification bus. See zx_notify.h N1-N8.
 * Freestanding: no libc, no allocation, no floating point. */
#include "zx_notify.h"

/* Copy at most cap-1 bytes, never splitting a UTF-8 sequence. */
static void copy_str(char *dst, const char *src, uint32_t cap)
{
    uint32_t n = 0;
    if (src)
        while (n + 1u < cap && src[n]) n++;
    if (src && src[n] && n > 0) {
        /* Truncated: back up over continuation bytes to a lead byte, and drop
         * that lead byte too if its sequence would not fit. */
        uint32_t k = n;
        while (k > 0 && ((uint8_t) src[k] & 0xC0u) == 0x80u) k--;
        n = k;
    }
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
}

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

bool zxn_init(zxn_bus_t *b, zxn_note_t *slots, uint32_t cap)
{
    if (!b || !slots || cap == 0) return false;
    b->slot = slots;
    b->cap = cap;
    for (uint32_t i = 0; i < cap; i++) {
        slots[i].used = false;
        slots[i].seq = 0;
    }
    for (uint32_t i = 0; i < ZXN_MAX_SUBS; i++) b->sub[i].fn = 0;
    b->clock = 0;
    b->next_seq = 1;
    b->muted_kinds = 0;
    b->dnd = false;
    b->dnd_breakthrough = ZXN_PRI_URGENT;
    b->dnd_allow_kinds = 0;
    b->dropped = b->evicted = b->coalesced = b->delivered = 0;
    return true;
}

int32_t zxn_subscribe(zxn_bus_t *b, zxn_sub_fn fn, void *ctx, uint32_t kind_mask, zxn_pri_t min_pri)
{
    if (!b || !fn) return -1;
    for (uint32_t i = 0; i < ZXN_MAX_SUBS; i++)
        if (!b->sub[i].fn) {
            b->sub[i].fn = fn;
            b->sub[i].ctx = ctx;
            b->sub[i].kind_mask = kind_mask;
            b->sub[i].min_pri = (uint8_t) min_pri;
            return (int32_t) i;
        }
    return -1;
}

void zxn_unsubscribe(zxn_bus_t *b, int32_t id)
{
    if (b && id >= 0 && (uint32_t) id < ZXN_MAX_SUBS) b->sub[id].fn = 0;
}

static void deliver(zxn_bus_t *b, const zxn_note_t *n, bool coalesced)
{
    if (b->muted_kinds & ZXN_KIND_BIT(n->kind)) return;
    for (uint32_t i = 0; i < ZXN_MAX_SUBS; i++) {
        const zxn_sub_t *s = &b->sub[i];
        if (!s->fn || !(s->kind_mask & ZXN_KIND_BIT(n->kind)) || n->pri < s->min_pri) continue;
        s->fn(s->ctx, n, coalesced);
        b->delivered++;
    }
}

static bool held_by_dnd(const zxn_bus_t *b, uint8_t kind, uint8_t pri)
{
    if (!b->dnd) return false;
    if (pri >= b->dnd_breakthrough) return false;
    return !(b->dnd_allow_kinds & ZXN_KIND_BIT(kind));
}

/* N3: pick a slot for a newcomer of priority pri, or -1 to drop it. */
static int32_t victim(const zxn_bus_t *b, uint8_t pri)
{
    int32_t best = -1;
    for (uint32_t i = 0; i < b->cap; i++) {
        const zxn_note_t *n = &b->slot[i];
        if (!n->used) return (int32_t) i;
        if (n->read && (best < 0 || !b->slot[best].read || n->last_ts < b->slot[best].last_ts))
            best = (int32_t) i;
    }
    if (best >= 0 && b->slot[best].read) return best;
    best = -1;
    for (uint32_t i = 0; i < b->cap; i++) {
        const zxn_note_t *n = &b->slot[i];
        if (n->pri > pri) continue;
        if (best < 0 || n->pri < b->slot[best].pri ||
            (n->pri == b->slot[best].pri && n->last_ts < b->slot[best].last_ts))
            best = (int32_t) i;
    }
    return best;
}

uint32_t zxn_post(zxn_bus_t *b, const zxn_spec_t *sp)
{
    if (!b || !sp || (uint32_t) sp->kind >= ZXN_KIND_COUNT) return 0;
    uint8_t pri = (uint8_t) (sp->pri > ZXN_PRI_URGENT ? ZXN_PRI_URGENT : sp->pri);
    char src[ZXN_SOURCE_MAX];
    copy_str(src, sp->source, ZXN_SOURCE_MAX);
    uint64_t ts = ++b->clock;

    /* N2 */
    if (sp->key)
        for (uint32_t i = 0; i < b->cap; i++) {
            zxn_note_t *n = &b->slot[i];
            if (!n->used || n->read || n->kind != (uint8_t) sp->kind || n->key != sp->key ||
                !str_eq(n->source, src))
                continue;
            n->count++;
            n->last_ts = ts;
            if (sp->wall_ms) n->wall_ms = sp->wall_ms;
            if (pri > n->pri) n->pri = pri;
            copy_str(n->title, sp->title, ZXN_TITLE_MAX);
            copy_str(n->body, sp->body, ZXN_BODY_MAX);
            copy_str(n->action, sp->action, ZXN_ACTION_MAX);
            b->coalesced++;
            bool hold = held_by_dnd(b, n->kind, n->pri);
            if (hold)
                n->held = true;
            else {
                bool was_held = n->held;
                n->held = false;
                deliver(b, n, !was_held);
            }
            return n->seq;
        }

    int32_t v = victim(b, pri);
    if (v < 0) {
        b->dropped++;
        return 0;
    }
    zxn_note_t *n = &b->slot[v];
    if (n->used) b->evicted++;
    n->used = true;
    n->seq = b->next_seq++;
    if (b->next_seq == 0) b->next_seq = 1;
    n->kind = (uint8_t) sp->kind;
    n->pri = pri;
    n->read = false;
    n->count = 1;
    n->key = sp->key;
    n->first_ts = n->last_ts = ts;
    n->wall_ms = sp->wall_ms;
    copy_str(n->source, src, ZXN_SOURCE_MAX);
    copy_str(n->title, sp->title, ZXN_TITLE_MAX);
    copy_str(n->body, sp->body, ZXN_BODY_MAX);
    copy_str(n->action, sp->action, ZXN_ACTION_MAX);
    n->held = held_by_dnd(b, n->kind, n->pri);
    if (!n->held) deliver(b, n, false);
    return n->seq;
}

const zxn_note_t *zxn_get(const zxn_bus_t *b, uint32_t seq)
{
    if (!b || !seq) return 0;
    for (uint32_t i = 0; i < b->cap; i++)
        if (b->slot[i].used && b->slot[i].seq == seq) return &b->slot[i];
    return 0;
}

bool zxn_mark_read(zxn_bus_t *b, uint32_t seq)
{
    zxn_note_t *n = (zxn_note_t *) zxn_get(b, seq);
    if (!n) return false;
    n->read = true;
    n->held = false; /* read in the UI: nothing left to announce */
    return true;
}

uint32_t zxn_mark_all_read(zxn_bus_t *b, uint32_t kind_mask)
{
    uint32_t c = 0;
    if (!b) return 0;
    for (uint32_t i = 0; i < b->cap; i++) {
        zxn_note_t *n = &b->slot[i];
        if (n->used && !n->read && (kind_mask & ZXN_KIND_BIT(n->kind))) {
            n->read = true;
            n->held = false;
            c++;
        }
    }
    return c;
}

uint32_t zxn_unread(const zxn_bus_t *b, uint32_t kind_mask)
{
    uint32_t c = 0;
    if (!b) return 0;
    for (uint32_t i = 0; i < b->cap; i++) {
        const zxn_note_t *n = &b->slot[i];
        if (n->used && !n->read && (kind_mask & ZXN_KIND_BIT(n->kind))) c++;
    }
    return c;
}

uint32_t zxn_list(const zxn_bus_t *b, const zxn_note_t **out, uint32_t max, bool unread_only)
{
    uint32_t c = 0;
    if (!b || !out) return 0;
    for (uint32_t i = 0; i < b->cap; i++) {
        const zxn_note_t *n = &b->slot[i];
        if (!n->used || (unread_only && n->read)) continue;
        /* insertion into newest-first order, keeping the newest `max` */
        uint32_t j = c < max ? c++ : max;
        while (j > 0 && out[j - 1]->last_ts < n->last_ts) {
            if (j < max) out[j] = out[j - 1];
            j--;
        }
        if (j < max) out[j] = n;
    }
    return c;
}

uint32_t zxn_set_dnd(zxn_bus_t *b, bool on, zxn_pri_t breakthrough, uint32_t allow_kinds)
{
    if (!b) return 0;
    b->dnd = on;
    b->dnd_breakthrough = (uint8_t) breakthrough;
    b->dnd_allow_kinds = allow_kinds;
    if (on) return 0;
    uint32_t released = 0;
    for (;;) { /* oldest first */
        int32_t pick = -1;
        for (uint32_t i = 0; i < b->cap; i++) {
            const zxn_note_t *n = &b->slot[i];
            if (n->used && n->held && (pick < 0 || n->last_ts < b->slot[pick].last_ts))
                pick = (int32_t) i;
        }
        if (pick < 0) break;
        b->slot[pick].held = false;
        deliver(b, &b->slot[pick], false);
        released++;
    }
    return released;
}

void zxn_mute(zxn_bus_t *b, uint32_t kind_mask)
{
    if (b) b->muted_kinds = kind_mask;
}

uint32_t zxn_task_finished(zxn_bus_t *b, const char *source, uint64_t task_id, bool ok,
                           const char *title, const char *summary, const char *action)
{
    zxn_spec_t s;
    s.kind = ok ? ZXN_AI_TASK_DONE : ZXN_AI_TASK_FAILED;
    s.pri = ok ? ZXN_PRI_NORMAL : ZXN_PRI_HIGH;
    s.source = source ? source : "ai";
    s.title = title ? title : (ok ? "Task complete" : "Task failed");
    s.body = summary;
    s.action = action;
    s.key = task_id ? task_id : 0;
    s.wall_ms = 0;
    return zxn_post(b, &s);
}

uint32_t zxn_message_received(zxn_bus_t *b, const char *source, const char *from_label,
                              uint64_t conversation_key)
{
    zxn_spec_t s;
    s.kind = ZXN_MESSAGE;
    s.pri = ZXN_PRI_NORMAL;
    s.source = source ? source : "social";
    s.title = "New message";
    s.body = from_label; /* who, never what (N8) */
    s.action = 0;
    s.key = conversation_key;
    s.wall_ms = 0;
    return zxn_post(b, &s);
}

uint32_t zxn_call(zxn_bus_t *b, const char *source, bool missed, const char *caller_label,
                  uint64_t call_id)
{
    zxn_spec_t s;
    s.kind = missed ? ZXN_CALL_MISSED : ZXN_CALL_INCOMING;
    s.pri = missed ? ZXN_PRI_HIGH : ZXN_PRI_URGENT;
    s.source = source ? source : "call";
    s.title = missed ? "Missed call" : "Incoming call";
    s.body = caller_label;
    s.action = 0;
    s.key = call_id;
    s.wall_ms = 0;
    return zxn_post(b, &s);
}

const char *zxn_kind_name(zxn_kind_t k)
{
    static const char *const names[ZXN_KIND_COUNT] = {"ai-task-done",  "ai-task-failed", "message",
                                                      "call-incoming", "call-missed",    "economy",
                                                      "update",        "peer-request"};
    return (uint32_t) k < ZXN_KIND_COUNT ? names[k] : "unknown";
}
