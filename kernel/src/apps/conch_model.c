/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* conch_model.c — Conch (see conch_model.h). */
#include <string.h>

#include "conch_model.h"

/* CM4: what each sound kind shows to begin with */
static const conch_pattern_t START_MAP[CONCH_SOUNDS + 1] = {
    {0, 0}, /* no kind 0 */
    {2, 2}, /* call */
    {1, 1}, /* message */
    {3, 3}, /* alarm */
    {1, 0}, /* task */
    {0, 0}, /* other */
};

void conch_init(conch_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->cap = CONCH_CAP_START;
    c->loudest = CONCH_LOUDEST_START;
    memcpy(c->map, START_MAP, sizeof c->map);
}

static bool good_name(const char *x)
{
    size_t n = x ? strlen(x) : 0;
    if (n < 1 || n > CONCH_NAME) return false;
    for (size_t i = 0; i < n; i++)
        if (x[i] < 0x20 || x[i] > 0x7e) return false;
    return true;
}

static int32_t device_at(const conch_t *c, const char *name)
{
    for (uint32_t i = 0; name && i < c->ndev; i++)
        if (!strcmp(c->dev[i].name, name)) return (int32_t) i;
    return -1;
}

const conch_device_t *conch_device(const conch_t *c, const char *name)
{
    int32_t i = device_at(c, name);
    return i < 0 ? NULL : &c->dev[i];
}

int conch_pair(conch_t *c, const char *name, uint32_t side, bool owner_confirmed)
{
    if (!c || !good_name(name) || side >= (uint32_t) CONCH_SIDES) return ZXM_E_ARG;
    if (device_at(c, name) >= 0) return ZXM_E_ARG; /* one name once */
    if (c->ndev >= CONCH_DEVICES) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    conch_device_t *d = &c->dev[c->ndev];
    memset(d, 0, sizeof *d);
    memcpy(d->name, name, strlen(name) + 1);
    d->side = (uint8_t) side;
    c->ndev++;
    return ZXM_OK;
}

int conch_unpair(conch_t *c, const char *name, bool owner_confirmed)
{
    int32_t i = c && good_name(name) ? device_at(c, name) : -1;
    if (i < 0) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    for (uint32_t k = (uint32_t) i; k + 1 < c->ndev; k++) c->dev[k] = c->dev[k + 1];
    c->ndev--;
    memset(&c->dev[c->ndev], 0, sizeof c->dev[c->ndev]);
    return ZXM_OK;
}

/* CM2: every band under the cap, with one band's gain as given */
static bool levels_fit(const conch_t *c, uint32_t band, int64_t gain, int64_t cap, int64_t loudest)
{
    if (loudest < 0 || loudest > CONCH_SPL_MAX) return false;
    for (uint32_t b = 0; b < CONCH_BANDS; b++) {
        int64_t g = b == band ? gain : c->gain[b];
        if (g < CONCH_GAIN_MIN || g > CONCH_GAIN_MAX) return false;
        if (!conch_level_ok(g, loudest + g, cap)) return false;
    }
    return true;
}

int conch_set_gain(conch_t *c, uint32_t band, int64_t gain, bool owner_confirmed)
{
    if (!c || band >= CONCH_BANDS || !levels_fit(c, band, gain, c->cap, c->loudest))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    c->gain[band] = gain;
    return ZXM_OK;
}

int conch_set_cap(conch_t *c, int64_t cap, bool owner_confirmed)
{
    if (!c || !levels_fit(c, CONCH_BANDS, 0, cap, c->loudest)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    c->cap = cap;
    return ZXM_OK;
}

int conch_set_loudest(conch_t *c, int64_t loudest, bool owner_confirmed)
{
    if (!c || !levels_fit(c, CONCH_BANDS, 0, c->cap, loudest)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    c->loudest = loudest;
    return ZXM_OK;
}

int conch_band_level(const conch_t *c, uint32_t band, int64_t input, int64_t *out, bool *limited)
{
    if (!c || !out || !limited || band >= CONCH_BANDS || input < 0 || input > CONCH_SPL_MAX)
        return ZXM_E_ARG;
    int64_t g = c->gain[band], total = input + g;
    if (total < 0) total = 0; /* a cut below silence is silence */
    *limited = !conch_level_ok(g, total, c->cap);
    *out = *limited ? c->cap : total;
    return ZXM_OK;
}

int conch_set_balance(conch_t *c, int64_t balance, bool mono, bool owner_confirmed)
{
    if (!c || !conch_balance_ok(balance, mono ? 1 : 0)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    c->balance = balance;
    c->mono = mono;
    return ZXM_OK;
}

int64_t conch_ear(const conch_t *c, uint32_t side)
{
    return c ? conch_ear_pct(c->balance, (int64_t) side) : -1;
}

int conch_set_alert(conch_t *c, int64_t sound, int64_t flash, int64_t vibrate, bool owner_confirmed)
{
    if (!c || !conch_alert_digits_ok(sound, flash, vibrate) ||
        !conch_alert_map_ok(sound, flash, vibrate))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6 */
    c->map[sound].flash = (uint8_t) flash;
    c->map[sound].vibrate = (uint8_t) vibrate;
    return ZXM_OK;
}

uint8_t conch_sound_of(const char *kind, uint32_t pri)
{
    if (pri >= CONCH_PRI_URGENT) return CONCH_SOUND_ALARM;
    if (!kind) return CONCH_SOUND_OTHER;
    if (!strcmp(kind, "call-incoming") || !strcmp(kind, "call-missed")) return CONCH_SOUND_CALL;
    if (!strcmp(kind, "message") || !strcmp(kind, "peer-request")) return CONCH_SOUND_MESSAGE;
    if (!strcmp(kind, "ai-task-done") || !strcmp(kind, "ai-task-failed")) return CONCH_SOUND_TASK;
    return CONCH_SOUND_OTHER;
}

static const conch_alert_t *alert_in(const conch_alert_t *a, uint32_t n, uint32_t seq)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i].seq == seq) return &a[i];
    return NULL;
}

const conch_alert_t *conch_alert(const conch_t *c, uint32_t seq)
{
    return c ? alert_in(c->alert, c->nalert, seq) : NULL;
}

static int u32(zxm_link_t *l, int32_t o, const char *key, uint32_t max, uint32_t *v)
{
    uint64_t x;
    if (zxm_get_u64(l, o, key, &x) || x > max) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

static int read_alert(zxm_link_t *l, int32_t o, conch_alert_t *a)
{
    uint32_t pri;
    memset(a, 0, sizeof *a);
    if (o < 0 || u32(l, o, "seq", UINT32_MAX, &a->seq) || !a->seq ||
        u32(l, o, "pri", CONCH_PRI_URGENT, &pri) || u32(l, o, "count", UINT32_MAX, &a->count) ||
        !a->count || zxm_get_bool(l, o, "read", &a->read) ||
        zxm_get_str(l, o, "kind", a->kind, sizeof a->kind) ||
        zxm_get_str(l, o, "source", a->source, sizeof a->source) ||
        zxm_get_str(l, o, "title", a->title, sizeof a->title))
        return ZXM_E_PROTOCOL;
    a->pri = (uint8_t) pri;
    a->sound = conch_sound_of(a->kind, pri);
    return ZXM_OK;
}

int conch_refresh(conch_t *c)
{
    if (!c || !c->link) return ZXM_E_ARG;
    zxm_link_t *l = c->link;
    int64_t ms;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", &ms) || ms < 0 || ms > CONCH_MS_MAX)
        return ZXM_E_PROTOCOL;
    if (c->seen && ms < c->now_ms) return ZXM_E_PROTOCOL; /* CM5: the clock went back */
    zxm_begin(l, "notes.list");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    uint32_t unread;
    int32_t arr = zxm_get(l, l->result, "notes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > CONCH_NOTES ||
        u32(l, l->result, "unread", UINT32_MAX, &unread))
        return ZXM_E_PROTOCOL;
    static conch_alert_t got[CONCH_NOTES]; /* the new view, kept apart until it is whole (CM5) */
    uint32_t n = zxm_len(l, arr), fresh = 0;
    uint8_t flash = 0, vibrate = 0;
    for (uint32_t i = 0; i < n; i++) {
        conch_alert_t *a = &got[i];
        if (read_alert(l, zxm_at(l, arr, i), a)) return ZXM_E_PROTOCOL;
        if (alert_in(got, i, a->seq)) return ZXM_E_PROTOCOL; /* one seq once */
        const conch_alert_t *was = alert_in(c->alert, c->nalert, a->seq);
        a->fresh = !a->read && (!was || a->count > was->count);
        a->seen_ms = a->fresh || !was ? ms : was->seen_ms;
        a->flash = c->map[a->sound].flash;
        a->vibrate = c->map[a->sound].vibrate;
        if (!a->fresh) continue;
        fresh++;
        if (a->flash > flash) flash = a->flash;
        if (a->vibrate > vibrate) vibrate = a->vibrate;
    }
    memcpy(c->alert, got, n * sizeof got[0]);
    memset(&c->alert[n], 0, (CONCH_NOTES - n) * sizeof got[0]);
    c->nalert = n;
    c->unread = unread;
    c->fresh = fresh;
    c->flash_now = flash;
    c->vibrate_now = vibrate;
    c->now_ms = ms;
    c->seen = true;
    return ZXM_OK;
}

int conch_dismiss(conch_t *c, uint32_t *marked, bool owner_confirmed)
{
    uint32_t n;
    if (!c || !c->link) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6: zxvd's notices are read on the owner's word */
    zxm_link_t *l = c->link;
    zxm_begin(l, "notes.mark_read");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (u32(l, l->result, "marked", UINT32_MAX, &n)) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < c->nalert; i++) {
        c->alert[i].read = true;
        c->alert[i].fresh = false;
    }
    c->unread = 0;
    c->fresh = 0;
    c->flash_now = 0;
    c->vibrate_now = 0;
    if (marked) *marked = n;
    return ZXM_OK;
}
