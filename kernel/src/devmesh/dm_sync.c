/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_sync.c — per-field vector clocks with last-writer-wins. See dm_sync.h. */
#include "dm_sync.h"
#include "dm_internal.h"

void dm_settings_init(dm_settings_t *s)
{
    if (s) dm_mset(s, 0, sizeof *s);
}

static dm_field_t *find(dm_settings_t *s, uint16_t key)
{
    for (uint32_t i = 0; i < DM_SET_MAX; i++)
        if (s->f[i].key == key) return &s->f[i];
    return 0;
}

const dm_field_t *dm_settings_get(const dm_settings_t *s, uint16_t key)
{
    if (!s || !key) return 0;
    return find((dm_settings_t *) s, key);
}

static uint32_t vc_get(const dm_field_t *f, const uint8_t id[DM_ID_BYTES])
{
    for (uint32_t i = 0; i < f->nvc && i < DM_MAX_DEVICES; i++)
        if (dm_meq(f->vc[i].id, id, DM_ID_BYTES)) return f->vc[i].ctr;
    return 0;
}

/* Set id's counter in f (adding the entry). Fails when the clock is full. */
static bool vc_set(dm_field_t *f, const uint8_t id[DM_ID_BYTES], uint32_t ctr)
{
    for (uint32_t i = 0; i < f->nvc; i++) {
        if (dm_meq(f->vc[i].id, id, DM_ID_BYTES)) {
            f->vc[i].ctr = ctr;
            return true;
        }
    }
    if (f->nvc >= DM_MAX_DEVICES) return false;
    dm_mcpy(f->vc[f->nvc].id, id, DM_ID_BYTES);
    f->vc[f->nvc].ctr = ctr;
    f->nvc++;
    return true;
}

int dm_vc_compare(const dm_field_t *a, const dm_field_t *b)
{
    bool a_gt = false, b_gt = false;
    for (uint32_t i = 0; i < a->nvc; i++) {
        uint32_t x = a->vc[i].ctr, y = vc_get(b, a->vc[i].id);
        if (x > y) a_gt = true;
        if (y > x) b_gt = true;
    }
    for (uint32_t i = 0; i < b->nvc; i++) {
        uint32_t y = b->vc[i].ctr, x = vc_get(a, b->vc[i].id);
        if (x > y) a_gt = true;
        if (y > x) b_gt = true;
    }
    if (a_gt && b_gt) return 2;
    if (a_gt) return 1;
    if (b_gt) return -1;
    return 0;
}

dm_status_t dm_settings_set(dm_settings_t *s, const uint8_t self[DM_ID_BYTES], uint16_t key,
                            const uint8_t *value, uint8_t len, uint64_t ts)
{
    if (!s || !self || !key || len > DM_SET_VALUE || (len && !value)) return DM_ERR_ARG;
    dm_field_t *f = find(s, key);
    if (!f) {
        f = find(s, 0);
        if (!f) return DM_ERR_FULL;
        dm_mset(f, 0, sizeof *f);
        f->key = key;
    }
    uint32_t c = vc_get(f, self);
    if (c == 0xFFFFFFFFu) return DM_ERR_FULL;
    if (!vc_set(f, self, c + 1u)) return DM_ERR_FULL;
    dm_mset(f->value, 0, DM_SET_VALUE);
    if (len) dm_mcpy(f->value, value, len);
    f->len = len;
    f->ts = ts;
    dm_mcpy(f->writer, self, DM_ID_BYTES);
    return DM_OK;
}

/* Total order used for concurrent writes: (ts, writer, len, value). */
static int lww_cmp(const dm_field_t *a, const dm_field_t *b)
{
    if (a->ts != b->ts) return a->ts > b->ts ? 1 : -1;
    int c = dm_mcmp(a->writer, b->writer, DM_ID_BYTES);
    if (c) return c;
    if (a->len != b->len) return a->len > b->len ? 1 : -1;
    return dm_mcmp(a->value, b->value, DM_SET_VALUE);
}

int dm_settings_merge_field(dm_settings_t *s, const dm_field_t *remote)
{
    if (!s || !remote || !remote->key || remote->len > DM_SET_VALUE || remote->nvc > DM_MAX_DEVICES)
        return DM_ERR_ARG;
    dm_field_t *f = find(s, remote->key);
    if (!f) {
        f = find(s, 0);
        if (!f) return DM_ERR_FULL;
        dm_mcpy(f, remote, sizeof *f);
        return 1;
    }
    int cmp = dm_vc_compare(f, remote);
    bool take = false;
    if (cmp == -1) {
        take = true;
    } else if (cmp == 2 || cmp == 0) {
        int w = lww_cmp(remote, f);
        if (w > 0) take = true;
        if (cmp == 2 && w != 0) s->conflicts++;
    }
    /* merged clock = pointwise max (computed before the value moves) */
    dm_field_t merged;
    dm_mcpy(&merged, take ? remote : f, sizeof merged);
    const dm_field_t *other = take ? f : remote;
    for (uint32_t i = 0; i < other->nvc; i++) {
        uint32_t mine = vc_get(&merged, other->vc[i].id);
        if (other->vc[i].ctr > mine && !vc_set(&merged, other->vc[i].id, other->vc[i].ctr))
            return DM_ERR_FULL;
    }
    bool changed =
        take && (f->len != remote->len || !dm_meq(f->value, remote->value, DM_SET_VALUE));
    dm_mcpy(f, &merged, sizeof *f);
    return changed ? 1 : 0;
}

int32_t dm_sync_encode(const dm_settings_t *s, uint8_t *out, uint32_t cap)
{
    if (!s || !out) return -1;
    dm_w_t w;
    dm_w_init(&w, out, cap);
    uint8_t n = 0;
    for (uint32_t i = 0; i < DM_SET_MAX; i++)
        if (s->f[i].key) n++;
    dm_w_u8(&w, n);
    for (uint32_t i = 0; i < DM_SET_MAX; i++) {
        const dm_field_t *f = &s->f[i];
        if (!f->key) continue;
        dm_w_u16(&w, f->key);
        dm_w_u8(&w, f->len);
        dm_w_bytes(&w, f->value, f->len);
        dm_w_u64(&w, f->ts);
        dm_w_bytes(&w, f->writer, DM_ID_BYTES);
        dm_w_u8(&w, f->nvc);
        for (uint32_t k = 0; k < f->nvc; k++) {
            dm_w_bytes(&w, f->vc[k].id, DM_ID_BYTES);
            dm_w_u32(&w, f->vc[k].ctr);
        }
    }
    return w.err ? -1 : (int32_t) w.n;
}

int32_t dm_sync_merge(dm_settings_t *s, const uint8_t *in, uint32_t len)
{
    if (!s || !in) return DM_ERR_ARG;
    dm_r_t r;
    dm_r_init(&r, in, len);
    uint8_t n = dm_r_u8(&r);
    if (n > DM_SET_MAX) return DM_ERR_FORMAT;
    int32_t changed = 0;
    dm_field_t f;
    for (uint32_t i = 0; i < n; i++) {
        dm_mset(&f, 0, sizeof f);
        f.key = dm_r_u16(&r);
        f.len = dm_r_u8(&r);
        if (r.err || !f.key || f.len > DM_SET_VALUE) return DM_ERR_FORMAT;
        dm_r_bytes(&r, f.value, f.len);
        f.ts = dm_r_u64(&r);
        dm_r_bytes(&r, f.writer, DM_ID_BYTES);
        f.nvc = dm_r_u8(&r);
        if (r.err || f.nvc == 0 || f.nvc > DM_MAX_DEVICES) return DM_ERR_FORMAT;
        for (uint32_t k = 0; k < f.nvc; k++) {
            dm_r_bytes(&r, f.vc[k].id, DM_ID_BYTES);
            f.vc[k].ctr = dm_r_u32(&r);
            for (uint32_t j = 0; j < k; j++)
                if (dm_meq(f.vc[j].id, f.vc[k].id, DM_ID_BYTES)) return DM_ERR_FORMAT;
        }
        if (r.err) return DM_ERR_FORMAT;
        int rc = dm_settings_merge_field(s, &f);
        if (rc < 0) return rc;
        changed += rc;
    }
    return dm_r_done(&r) ? changed : DM_ERR_FORMAT;
}

dm_status_t dm_sync_push(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint64_t now_ms)
{
    if (!m || !peer) return DM_ERR_ARG;
    int32_t n = dm_sync_encode(&m->settings, m->app_buf, sizeof m->app_buf);
    if (n < 0) return DM_ERR_SIZE;
    return dm_send_app(m, peer, DM_APP_SYNC, m->app_buf, (uint32_t) n, now_ms);
}

dm_status_t dm_set(dm_mesh_t *m, uint16_t key, const uint8_t *value, uint8_t len, uint64_t now_ms)
{
    if (!m) return DM_ERR_ARG;
    dm_status_t st = dm_settings_set(&m->settings, m->self_id, key, value, len, now_ms);
    if (st != DM_OK) return st;
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        dm_peer_t *p = &m->peers[i];
        if (p->used && p->s.state == DM_SESS_UP) (void) dm_sync_push(m, p->id, now_ms);
    }
    return DM_OK;
}
