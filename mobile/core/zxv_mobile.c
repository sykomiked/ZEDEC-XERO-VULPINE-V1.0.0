/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_mobile.c — the mobile core's C API over kernel/src/devmesh. */
#include "zxv_mobile.h"

#include "../../kernel/src/devmesh/devmesh.h"
#include "../../kernel/src/devmesh/dm_remote.h"
#include "../../kernel/src/devmesh/dm_sync.h"
#include "../../kernel/src/capmkt/capmkt.h"

static dm_mesh_t g_mesh;
static zxv_host_t g_host;
static int g_started;

static void h_random(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    g_host.random(g_host.ctx, out, n);
}

static int h_send(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *frame, uint32_t len)
{
    (void) ctx;
    return g_host.send(g_host.ctx, to, frame, len);
}

static void h_event(void *ctx, const dm_event_t *ev)
{
    (void) ctx;
    if (g_host.event) g_host.event(g_host.ctx, ev->kind, ev->peer, ev->sas, ev->u64);
}

static void h_reply(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id,
                    const uint8_t *data, uint32_t len, bool final)
{
    (void) ctx;
    (void) from;
    if (g_host.reply) g_host.reply(g_host.ctx, req_id, data, len, final ? 1 : 0);
}

static void h_money(void *ctx, const uint8_t from[DM_ID_BYTES], const dm_money_t *m)
{
    (void) ctx;
    (void) from;
    if (g_host.money) g_host.money(g_host.ctx, m->amount, m->rail, m->memo);
}

int zxv_api_version(void)
{
    return ZXV_API_VERSION;
}

int zxv_start(const zxv_host_t *host, const uint8_t seed[64], const char *name, int role, int flags,
              int level)
{
    if (!host || !host->random || !host->send || !seed) return DM_ERR_ARG;
    dm_mcpy(&g_host, host, sizeof g_host);
    dm_host_t h;
    dm_mset(&h, 0, sizeof h);
    h.random = h_random;
    h.send = h_send;
    h.on_event = h_event;
    h.on_reply = h_reply;
    h.on_money = h_money;
    pqm_level_t lv = level == 3 ? PQM_LEVEL_MATRIX : PQM_LEVEL_HIGH;
    int st = dm_init(&g_mesh, &h, lv, seed, name, (dm_role_t) role, (uint8_t) flags);
    g_started = st == DM_OK;
    return st;
}

void zxv_stop(void)
{
    dm_wipe(&g_mesh);
    dm_mset(&g_host, 0, sizeof g_host);
    g_started = 0;
}

int zxv_self_id(uint8_t out[ZXV_ID_BYTES])
{
    if (!g_started || !out) return DM_ERR_STATE;
    dm_mcpy(out, dm_self_id(&g_mesh), DM_ID_BYTES);
    return 0;
}

int zxv_create_mesh(uint64_t now_ms)
{
    return g_started ? dm_create(&g_mesh, now_ms) : DM_ERR_STATE;
}

int zxv_invite(int role, int flags, uint64_t now_ms, char *qr_text, uint32_t qr_cap, char code[11])
{
    if (!g_started || !qr_text || !code) return DM_ERR_ARG;
    uint8_t qr[DM_QR_MAX];
    uint32_t qn = 0;
    int st = dm_invite(&g_mesh, (uint8_t) role, (uint8_t) flags, 0, 0, now_ms, qr, &qn, code);
    if (st != DM_OK) return st;
    return dm_b32_encode(qr, qn, qr_text, qr_cap) < 0 ? DM_ERR_SIZE : 0;
}

int zxv_join_qr_text(const char *qr_text, uint64_t now_ms)
{
    if (!g_started || !qr_text) return DM_ERR_ARG;
    uint8_t qr[DM_QR_MAX];
    int32_t n = dm_b32_decode(qr_text, qr, sizeof qr);
    if (n < 0) return DM_ERR_FORMAT;
    return dm_join_qr(&g_mesh, qr, (uint32_t) n, now_ms);
}

int zxv_join_code(const char *code, uint64_t now_ms)
{
    return g_started ? dm_join_code(&g_mesh, code, now_ms) : DM_ERR_STATE;
}

int zxv_confirm_sas(const uint8_t peer[ZXV_ID_BYTES], int match, uint64_t now_ms)
{
    return g_started ? dm_pair_confirm(&g_mesh, peer, match != 0, now_ms) : DM_ERR_STATE;
}

int zxv_receive(const uint8_t *frame, uint32_t len, uint64_t now_ms)
{
    return g_started ? dm_receive(&g_mesh, frame, len, now_ms) : DM_ERR_STATE;
}

void zxv_tick(uint64_t now_ms)
{
    if (g_started) dm_tick(&g_mesh, now_ms);
}

int zxv_connect(const uint8_t peer[ZXV_ID_BYTES], uint64_t now_ms)
{
    return g_started ? dm_connect(&g_mesh, peer, now_ms) : DM_ERR_STATE;
}

int zxv_device_count(void)
{
    return g_started ? dm_roster(&g_mesh)->n : 0;
}

int zxv_device(int i, uint8_t id[ZXV_ID_BYTES], char name[32])
{
    const dm_roster_t *r = dm_roster(&g_mesh);
    if (!g_started || i < 0 || i >= r->n || !id || !name) return DM_ERR_ARG;
    const dm_dev_t *d = &r->dev[i];
    dm_mcpy(id, d->id, DM_ID_BYTES);
    dm_mcpy(name, d->name, DM_NAME_MAX);
    return d->role | (d->flags << 8) | (d->status << 16);
}

int zxv_rename(const uint8_t id[ZXV_ID_BYTES], const char *name, uint64_t now_ms)
{
    return g_started ? dm_rename(&g_mesh, id, name, now_ms) : DM_ERR_STATE;
}

int zxv_revoke(const uint8_t id[ZXV_ID_BYTES], uint64_t now_ms)
{
    return g_started ? dm_revoke(&g_mesh, id, now_ms) : DM_ERR_STATE;
}

int zxv_set_caps(uint32_t ram_mb, uint32_t compute, int battery_pct, int charging, int net,
                 uint32_t features, uint64_t now_ms)
{
    if (!g_started) return DM_ERR_STATE;
    dm_caps_t c;
    dm_mset(&c, 0, sizeof c);
    c.ram_mb = ram_mb;
    c.free_ram_mb = ram_mb >> 1;
    c.compute = compute;
    c.battery_pct = (uint8_t) battery_pct;
    c.charging = (uint8_t) (charging != 0);
    c.net = (uint8_t) net;
    c.features = features;
    dm_caps_fit(&c, g_mesh.self_role);
    return dm_caps_update(&g_mesh, &c, now_ms);
}

const char *zxv_local_model(int i)
{
    if (!g_started || i < 0 || i >= g_mesh.caps.nmodels) return 0;
    return g_mesh.caps.models[i].name;
}

int zxv_route(int kind, int model_class, uint64_t now_ms, uint8_t target[ZXV_ID_BYTES])
{
    if (!g_started || !target) return DM_ERR_STATE;
    return (int) dm_route(&g_mesh, (uint8_t) kind, (uint8_t) model_class, now_ms, target);
}

int zxv_prompt(const uint8_t target[ZXV_ID_BYTES], const char *text, uint64_t now_ms,
               uint32_t *req_id)
{
    if (!g_started || !target || !text) return DM_ERR_ARG;
    uint32_t n = 0;
    while (n < DM_REC_MAX - 8u && text[n]) n++;
    return dm_request(&g_mesh, target, DM_REQ_PROMPT, (const uint8_t *) text, n, now_ms, req_id);
}

int zxv_set_setting(uint16_t key, const char *value, uint64_t now_ms)
{
    if (!g_started || !value) return DM_ERR_ARG;
    uint32_t n = 0;
    while (n <= DM_SET_VALUE && value[n]) n++;
    if (n > DM_SET_VALUE) return DM_ERR_SIZE;
    return dm_set(&g_mesh, key, (const uint8_t *) value, (uint8_t) n, now_ms);
}

int zxv_get_setting(uint16_t key, char *out, uint32_t cap)
{
    if (!g_started || !out || !cap) return DM_ERR_ARG;
    const dm_field_t *f = dm_settings_get(&g_mesh.settings, key);
    if (!f) return DM_ERR_UNKNOWN;
    if (f->len + 1u > cap) return DM_ERR_SIZE;
    dm_mcpy(out, f->value, f->len);
    out[f->len] = 0;
    return f->len;
}

int zxv_money_answer(int approve, uint64_t now_ms)
{
    return g_started ? dm_money_answer(&g_mesh, approve != 0, now_ms) : DM_ERR_STATE;
}

uint64_t zxv_assure_fee(uint64_t amount)
{
    return cm_fee_assure(amount);
}
