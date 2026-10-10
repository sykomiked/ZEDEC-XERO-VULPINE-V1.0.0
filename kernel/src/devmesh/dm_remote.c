/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_remote.c — capabilities, routing, remote requests and money
 * confirmation. See dm_remote.h. */
#include "dm_remote.h"
#include "dm_internal.h"
#include "dm_sync.h"

#include "../mlkem/keccak.h"

#define CTX_MONEY "zxv-devmesh/v1/money"

/* ===== model catalogue =====
 * Sizes are the Q4_K_M GGUF files published by the Qwen team; ram_mb adds
 * an f16 KV cache at 4096 tokens and runtime buffers (estimates, see
 * docs/MOBILE_AND_DEVICES.md). Qwen2.5 3B and 72B are NOT Apache-2.0 (Qwen
 * licence) and are deliberately absent. */
static const dm_model_t CATALOG[] = {
    {"qwen2.5-0.5b-instruct-q4_k_m", 494u, 491u, 700u, 4u, DM_MODEL_SMALL, 1u},
    {"qwen2.5-1.5b-instruct-q4_k_m", 1544u, 1120u, 1500u, 4u, DM_MODEL_SMALL, 1u},
    {"qwen2.5-7b-instruct-q4_k_m", 7616u, 4680u, 5400u, 4u, DM_MODEL_LARGE, 1u},
    {"qwen2.5-14b-instruct-q4_k_m", 14770u, 8990u, 10300u, 4u, DM_MODEL_LARGE, 1u},
};
#define CATALOG_N (sizeof CATALOG / sizeof CATALOG[0])

const dm_model_t *dm_model_catalog(uint32_t i)
{
    return i < CATALOG_N ? &CATALOG[i] : 0;
}

void dm_caps_fit(dm_caps_t *caps, uint8_t role)
{
    if (!caps) return;
    uint32_t budget =
        role == DM_ROLE_HOME ? (uint32_t) (((uint64_t) caps->ram_mb * 3u) >> 2) : caps->ram_mb >> 1;
    caps->nmodels = 0;
    caps->features &= ~(uint32_t) (DM_FEAT_ASSIST_SMALL | DM_FEAT_ASSIST_LARGE);
    for (uint32_t i = 0; i < CATALOG_N && caps->nmodels < DM_MAX_MODELS; i++) {
        if (CATALOG[i].ram_mb > budget) continue;
        dm_mcpy(&caps->models[caps->nmodels++], &CATALOG[i], sizeof(dm_model_t));
        caps->features |=
            CATALOG[i].cls == DM_MODEL_LARGE ? DM_FEAT_ASSIST_LARGE : DM_FEAT_ASSIST_SMALL;
    }
}

bool dm_caps_can_run(const dm_caps_t *c, uint8_t cls)
{
    if (!c) return false;
    for (uint32_t i = 0; i < c->nmodels && i < DM_MAX_MODELS; i++)
        if (c->models[i].cls == cls) return true;
    return false;
}

int32_t dm_caps_encode(const dm_caps_t *c, uint8_t *out, uint32_t cap)
{
    if (!c || !out || c->nmodels > DM_MAX_MODELS) return -1;
    dm_w_t w;
    dm_w_init(&w, out, cap);
    dm_w_u32(&w, c->ram_mb);
    dm_w_u32(&w, c->free_ram_mb);
    dm_w_u32(&w, c->compute);
    dm_w_u8(&w, c->battery_pct);
    dm_w_u8(&w, c->charging);
    dm_w_u8(&w, c->net);
    dm_w_u8(&w, c->nmodels);
    dm_w_u32(&w, c->features);
    for (uint32_t i = 0; i < c->nmodels; i++) {
        const dm_model_t *md = &c->models[i];
        dm_w_bytes(&w, md->name, DM_MODEL_NAME);
        dm_w_u32(&w, md->params_m);
        dm_w_u32(&w, md->file_mb);
        dm_w_u32(&w, md->ram_mb);
        dm_w_u8(&w, md->quant_bits);
        dm_w_u8(&w, md->cls);
        dm_w_u8(&w, md->license_apache2);
    }
    return w.err ? -1 : (int32_t) w.n;
}

dm_status_t dm_caps_decode(dm_caps_t *c, const uint8_t *in, uint32_t len)
{
    if (!c || !in) return DM_ERR_ARG;
    dm_r_t r;
    dm_r_init(&r, in, len);
    dm_mset(c, 0, sizeof *c);
    c->ram_mb = dm_r_u32(&r);
    c->free_ram_mb = dm_r_u32(&r);
    c->compute = dm_r_u32(&r);
    c->battery_pct = dm_r_u8(&r);
    c->charging = dm_r_u8(&r);
    c->net = dm_r_u8(&r);
    c->nmodels = dm_r_u8(&r);
    c->features = dm_r_u32(&r);
    if (r.err || c->nmodels > DM_MAX_MODELS || c->net > DM_NET_METERED) return DM_ERR_FORMAT;
    if (c->battery_pct > 100u && c->battery_pct != 255u) return DM_ERR_FORMAT;
    for (uint32_t i = 0; i < c->nmodels; i++) {
        dm_model_t *md = &c->models[i];
        dm_r_bytes(&r, md->name, DM_MODEL_NAME);
        md->params_m = dm_r_u32(&r);
        md->file_mb = dm_r_u32(&r);
        md->ram_mb = dm_r_u32(&r);
        md->quant_bits = dm_r_u8(&r);
        md->cls = dm_r_u8(&r);
        md->license_apache2 = dm_r_u8(&r);
        if (r.err || md->name[DM_MODEL_NAME - 1] != 0) return DM_ERR_FORMAT;
        if (md->cls != DM_MODEL_SMALL && md->cls != DM_MODEL_LARGE) return DM_ERR_FORMAT;
    }
    return dm_r_done(&r) ? DM_OK : DM_ERR_FORMAT;
}

static void send_caps(dm_mesh_t *m, dm_peer_t *p, uint64_t now)
{
    uint8_t buf[DM_CAPS_WIRE];
    int32_t n = dm_caps_encode(&m->caps, buf, sizeof buf);
    if (n > 0) (void) dm_send_app(m, p->id, DM_APP_CAPS, buf, (uint32_t) n, now);
}

dm_status_t dm_caps_update(dm_mesh_t *m, const dm_caps_t *caps, uint64_t now_ms)
{
    if (!m || !caps || caps->nmodels > DM_MAX_MODELS) return DM_ERR_ARG;
    dm_mcpy(&m->caps, caps, sizeof m->caps);
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        dm_peer_t *p = &m->peers[i];
        if (p->used && p->s.state == DM_SESS_UP) send_caps(m, p, now_ms);
    }
    return DM_OK;
}

/* ===== routing ===== */
static bool live(const dm_mesh_t *m, const dm_peer_t *p, uint64_t now)
{
    if (!p->used || p->s.state != DM_SESS_UP || !p->have_caps) return false;
    if (now - p->s.last_rx_ms > DM_LIVE_MS) return false;
    const dm_dev_t *d = dm_roster_find(&m->roster, p->id);
    return d && d->status == DM_DEV_ACTIVE;
}

static const dm_peer_t *best_peer(const dm_mesh_t *m, uint32_t need_any, uint64_t now)
{
    const dm_peer_t *best = 0;
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        const dm_peer_t *p = &m->peers[i];
        if (!live(m, p, now) || !(p->caps.features & need_any)) continue;
        if (!best) {
            best = p;
            continue;
        }
        bool ph = p->paired_role == DM_ROLE_HOME, bh = best->paired_role == DM_ROLE_HOME;
        if (ph != bh) {
            if (ph) best = p;
            continue;
        }
        if (p->caps.compute > best->caps.compute) best = p;
    }
    return best;
}

static bool allow_metered(const dm_mesh_t *m)
{
    const dm_field_t *f = dm_settings_get(&m->settings, DM_KEY_ALLOW_METERED);
    return f && f->len == 1 && f->value[0] == '1';
}

dm_route_t dm_route(const dm_mesh_t *m, uint8_t req_kind, uint8_t model_cls, uint64_t now_ms,
                    uint8_t target[DM_ID_BYTES])
{
    if (!m || !target) return DM_ROUTE_UNAVAILABLE;
    dm_mcpy(target, m->self_id, DM_ID_BYTES);
    const dm_caps_t *lc = &m->caps;
    if (req_kind == DM_REQ_WALLET) {
        const dm_peer_t *w =
            (lc->features & DM_FEAT_WALLET) ? 0 : best_peer(m, DM_FEAT_WALLET, now_ms);
        if (w) dm_mcpy(target, w->id, DM_ID_BYTES);
        return DM_ROUTE_CONFIRM;
    }
    if (req_kind == DM_REQ_FILE || req_kind == DM_REQ_MEDIA) {
        uint32_t f = req_kind == DM_REQ_FILE ? DM_FEAT_FILES : DM_FEAT_STREAM;
        if (m->self_role == DM_ROLE_HOME && (lc->features & f)) return DM_ROUTE_LOCAL;
        const dm_peer_t *p = best_peer(m, f, now_ms);
        if (p) {
            dm_mcpy(target, p->id, DM_ID_BYTES);
            return DM_ROUTE_REMOTE;
        }
        if (lc->features & f) return DM_ROUTE_LOCAL;
        return DM_ROUTE_UNAVAILABLE;
    }
    /* assistant prompt */
    uint8_t cls = model_cls == DM_MODEL_LARGE ? DM_MODEL_LARGE : DM_MODEL_SMALL;
    bool local_can = dm_caps_can_run(lc, cls);
    if (local_can && (m->self_role == DM_ROLE_HOME || cls == DM_MODEL_LARGE)) return DM_ROUTE_LOCAL;
    uint32_t need = cls == DM_MODEL_LARGE ? DM_FEAT_ASSIST_LARGE
                                          : (DM_FEAT_ASSIST_SMALL | DM_FEAT_ASSIST_LARGE);
    const dm_peer_t *p = lc->net == DM_NET_OFFLINE ? 0 : best_peer(m, need, now_ms);
    if (p) {
        bool low_batt = lc->battery_pct != 255u && lc->battery_pct < 20u && !lc->charging;
        bool metered = lc->net == DM_NET_METERED && !allow_metered(m);
        if (cls == DM_MODEL_SMALL && local_can && metered && !low_batt) return DM_ROUTE_LOCAL;
        dm_mcpy(target, p->id, DM_ID_BYTES);
        return DM_ROUTE_REMOTE;
    }
    if (local_can) return DM_ROUTE_LOCAL;
    if (cls == DM_MODEL_LARGE && dm_caps_can_run(lc, DM_MODEL_SMALL))
        return DM_ROUTE_LOCAL_DEGRADED;
    return lc->net == DM_NET_OFFLINE ? DM_ROUTE_UNAVAILABLE : DM_ROUTE_NEED_CAPACITY;
}

/* ===== requests ===== */
static dm_req_t *req_find(dm_mesh_t *m, uint32_t id, const uint8_t peer[DM_ID_BYTES])
{
    for (uint32_t i = 0; i < DM_MAX_REQS; i++) {
        dm_req_t *q = &m->reqs[i];
        if (q->used && q->req_id == id && dm_meq(q->target, peer, DM_ID_BYTES)) return q;
    }
    return 0;
}

/* payload must already sit at m->app_buf + 5 or anywhere else. */
static dm_status_t req_send(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint8_t kind,
                            const uint8_t *payload, uint32_t len, uint64_t now, uint32_t *req_id)
{
    if (len > DM_REC_MAX - 6u) return DM_ERR_SIZE;
    if (!dm_peer_up(m, peer, now)) return DM_ERR_NOPEER;
    dm_req_t *q = 0;
    for (uint32_t i = 0; i < DM_MAX_REQS && !q; i++)
        if (!m->reqs[i].used) q = &m->reqs[i];
    if (!q) return DM_ERR_FULL;
    uint32_t id = ++m->next_req;
    if (len) dm_mcpy(m->app_buf + 5, payload, len);
    for (int i = 0; i < 4; i++) m->app_buf[i] = (uint8_t) (id >> (24 - 8 * i));
    m->app_buf[4] = kind;
    dm_status_t st = dm_send_app(m, peer, DM_APP_REQ, m->app_buf, len + 5u, now);
    if (st != DM_OK) return st;
    dm_mset(q, 0, sizeof *q);
    q->used = 1;
    q->kind = kind;
    q->req_id = id;
    dm_mcpy(q->target, peer, DM_ID_BYTES);
    q->sent_ms = now;
    q->last_ms = now;
    if (req_id) *req_id = id;
    return DM_OK;
}

dm_status_t dm_request(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint8_t kind,
                       const uint8_t *payload, uint32_t len, uint64_t now_ms, uint32_t *req_id)
{
    if (!m || !peer || (len && !payload)) return DM_ERR_ARG;
    if (kind < DM_REQ_PROMPT || kind > DM_REQ_MEDIA) return DM_ERR_ARG;
    if (kind == DM_REQ_WALLET) return DM_ERR_PERM; /* use dm_wallet_request */
    return req_send(m, peer, kind, payload, len, now_ms, req_id);
}

dm_status_t dm_reply(dm_mesh_t *m, const uint8_t to[DM_ID_BYTES], uint32_t req_id,
                     const uint8_t *data, uint32_t len, bool final, uint64_t now_ms)
{
    if (!m || !to || (len && !data)) return DM_ERR_ARG;
    if (len > DM_CHUNK_MAX) return DM_ERR_SIZE;
    uint8_t buf[5u + DM_CHUNK_MAX];
    for (int i = 0; i < 4; i++) buf[i] = (uint8_t) (req_id >> (24 - 8 * i));
    buf[4] = final ? 1u : 0u;
    if (len) dm_mcpy(buf + 5, data, len);
    return dm_send_app(m, to, DM_APP_REPLY, buf, len + 5u, now_ms);
}

void dm_remote_tick(dm_mesh_t *m, uint64_t now_ms)
{
    for (uint32_t i = 0; i < DM_MAX_REQS; i++) {
        dm_req_t *q = &m->reqs[i];
        if (!q->used) continue;
        if (!dm_peer_up(m, q->target, now_ms) || now_ms - q->last_ms > DM_REQ_TIMEOUT_MS) {
            uint32_t id = q->req_id;
            uint8_t tgt[DM_ID_BYTES];
            dm_mcpy(tgt, q->target, DM_ID_BYTES);
            dm_mset(q, 0, sizeof *q);
            dm_emit(m, DM_EV_REQ_FALLBACK, tgt, 0, id, DM_ERR_NOPEER);
        }
    }
}

void dm_remote_on_session_up(dm_mesh_t *m, dm_peer_t *p, uint64_t now_ms)
{
    send_caps(m, p, now_ms);
    (void) dm_sync_push(m, p->id, now_ms);
}

/* ===== money ===== */
static bool money_valid(const dm_money_t *a)
{
    if (a->kind < DM_MONEY_PAY || a->kind > DM_MONEY_SIGN) return false;
    if (a->rail != DM_RAIL_DEBIT && a->rail != DM_RAIL_CREDIT && a->rail != DM_RAIL_EQUITY)
        return false;
    if (a->currency[0] != 'V' || a->currency[1] != 'F' || a->currency[2] != 'V' ||
        a->currency[3] != 0)
        return false;
    if (a->memo[DM_MONEY_MEMO - 1] != 0) return false;
    return a->amount > 0;
}

int32_t dm_money_encode(const dm_money_t *a, uint8_t *out, uint32_t cap)
{
    if (!a || !out) return -1;
    dm_w_t w;
    dm_w_init(&w, out, cap);
    dm_w_bytes(&w, a->action_id, DM_ID_BYTES);
    dm_w_u8(&w, a->kind);
    dm_w_u16(&w, a->rail);
    dm_w_bytes(&w, a->currency, 4);
    dm_w_u64(&w, a->amount);
    dm_w_bytes(&w, a->payee, DM_HASH_BYTES);
    dm_w_bytes(&w, a->memo, DM_MONEY_MEMO);
    dm_w_bytes(&w, a->origin, DM_ID_BYTES);
    dm_w_u64(&w, a->expires_ms);
    return w.err ? -1 : (int32_t) w.n;
}

dm_status_t dm_money_decode(dm_money_t *a, const uint8_t *in, uint32_t len)
{
    if (!a || !in) return DM_ERR_ARG;
    dm_r_t r;
    dm_r_init(&r, in, len);
    dm_mset(a, 0, sizeof *a);
    dm_r_bytes(&r, a->action_id, DM_ID_BYTES);
    a->kind = dm_r_u8(&r);
    a->rail = dm_r_u16(&r);
    dm_r_bytes(&r, a->currency, 4);
    a->amount = dm_r_u64(&r);
    dm_r_bytes(&r, a->payee, DM_HASH_BYTES);
    dm_r_bytes(&r, a->memo, DM_MONEY_MEMO);
    dm_r_bytes(&r, a->origin, DM_ID_BYTES);
    a->expires_ms = dm_r_u64(&r);
    if (!dm_r_done(&r) || !money_valid(a)) return DM_ERR_FORMAT;
    return DM_OK;
}

int32_t dm_confirm_encode(const dm_confirm_t *c, uint8_t *out, uint32_t cap)
{
    if (!c || !out || c->sig_len > PQM_SIG_MAX_BYTES) return -1;
    dm_w_t w;
    dm_w_init(&w, out, cap);
    dm_w_bytes(&w, c->digest, DM_HASH_BYTES);
    dm_w_bytes(&w, c->confirmer, DM_ID_BYTES);
    dm_w_u8(&w, c->approve);
    dm_w_u32(&w, c->sig_len);
    dm_w_bytes(&w, c->sig, c->sig_len);
    return w.err ? -1 : (int32_t) w.n;
}

dm_status_t dm_confirm_decode(dm_confirm_t *c, const uint8_t *in, uint32_t len)
{
    if (!c || !in) return DM_ERR_ARG;
    dm_r_t r;
    dm_r_init(&r, in, len);
    dm_mset(c, 0, sizeof *c);
    dm_r_bytes(&r, c->digest, DM_HASH_BYTES);
    dm_r_bytes(&r, c->confirmer, DM_ID_BYTES);
    c->approve = dm_r_u8(&r);
    c->sig_len = dm_r_u32(&r);
    if (r.err || c->sig_len > PQM_SIG_MAX_BYTES || c->approve > 1u) return DM_ERR_FORMAT;
    dm_r_bytes(&r, c->sig, c->sig_len);
    return dm_r_done(&r) ? DM_OK : DM_ERR_FORMAT;
}

static bool money_digest(const dm_money_t *a, uint8_t out[32])
{
    uint8_t buf[DM_MONEY_WIRE];
    int32_t n = dm_money_encode(a, buf, sizeof buf);
    if (n < 0) return false;
    sha3_256(buf, (size_t) n, out);
    return true;
}

static void confirm_msg(const dm_confirm_t *c, uint8_t msg[DM_HASH_BYTES + DM_ID_BYTES + 1u])
{
    dm_mcpy(msg, c->digest, DM_HASH_BYTES);
    dm_mcpy(msg + DM_HASH_BYTES, c->confirmer, DM_ID_BYTES);
    msg[DM_HASH_BYTES + DM_ID_BYTES] = c->approve;
}

static const dm_dev_t *held_device(const dm_mesh_t *m, const uint8_t id[DM_ID_BYTES],
                                   dm_status_t *st)
{
    const dm_dev_t *d = dm_roster_find(&m->roster, id);
    if (!d) {
        *st = DM_ERR_UNKNOWN;
        return 0;
    }
    if (d->status != DM_DEV_ACTIVE) {
        *st = DM_ERR_REVOKED;
        return 0;
    }
    if (!(d->flags & DM_FLAG_HELD)) {
        *st = DM_ERR_PERM;
        return 0;
    }
    *st = DM_OK;
    return d;
}

dm_status_t dm_money_confirm_local(dm_mesh_t *m, const dm_money_t *a, bool approve, uint64_t now_ms,
                                   dm_confirm_t *out)
{
    if (!m || !a || !out) return DM_ERR_ARG;
    if (!m->joined) return DM_ERR_STATE;
    if (!money_valid(a)) return DM_ERR_FORMAT;
    if (now_ms > a->expires_ms) return DM_ERR_EXPIRED;
    dm_status_t st;
    if (!held_device(m, m->self_id, &st)) return st;
    dm_mset(out, 0, sizeof *out);
    if (!money_digest(a, out->digest)) return DM_ERR_FORMAT;
    dm_mcpy(out->confirmer, m->self_id, DM_ID_BYTES);
    out->approve = approve ? 1u : 0u;
    uint8_t msg[DM_HASH_BYTES + DM_ID_BYTES + 1u], rn[32];
    confirm_msg(out, msg);
    m->host.random(m->host.ctx, rn, 32);
    if (!pqm_sign(&m->sig_sk, msg, sizeof msg, (const uint8_t *) CTX_MONEY, sizeof CTX_MONEY - 1,
                  rn, &m->sig))
        return DM_ERR_CRYPTO;
    size_t sl = pqm_sig_encode(&m->sig, out->sig, sizeof out->sig);
    if (!sl) return DM_ERR_CRYPTO;
    out->sig_len = (uint32_t) sl;
    return DM_OK;
}

dm_status_t dm_money_verify(dm_mesh_t *m, const dm_money_t *a, const dm_confirm_t *c,
                            uint64_t now_ms)
{
    if (!m || !a || !c) return DM_ERR_ARG;
    if (!m->joined) return DM_ERR_STATE;
    if (!money_valid(a)) return DM_ERR_FORMAT;
    if (now_ms > a->expires_ms) return DM_ERR_EXPIRED;
    uint8_t dg[32];
    if (!money_digest(a, dg) || !dm_meq(dg, c->digest, 32)) return DM_ERR_AUTH;
    dm_status_t st;
    if (!held_device(m, c->confirmer, &st)) return st;
    const pqm_sig_pk_t *pk = 0;
    if (dm_meq(c->confirmer, m->self_id, DM_ID_BYTES)) {
        pk = &m->sig_pk;
    } else {
        dm_peer_t *p = dm_peer_find(m, c->confirmer);
        if (!p || !p->have_keys) return DM_ERR_UNKNOWN;
        pk = &p->sig_pk;
    }
    uint8_t msg[DM_HASH_BYTES + DM_ID_BYTES + 1u];
    confirm_msg(c, msg);
    if (!pqm_sig_decode(&m->sig, c->sig, c->sig_len) ||
        !pqm_verify(pk, msg, sizeof msg, (const uint8_t *) CTX_MONEY, sizeof CTX_MONEY - 1,
                    &m->sig))
        return DM_ERR_AUTH;
    if (!c->approve) return DM_ERR_PERM; /* the user said no */
    uint32_t k = m->money_seen_n < DM_MONEY_SEEN ? m->money_seen_n : DM_MONEY_SEEN;
    for (uint32_t i = 0; i < k; i++)
        if (dm_meq(m->money_seen[i], a->action_id, DM_ID_BYTES)) return DM_ERR_REPLAY;
    dm_mcpy(m->money_seen[m->money_seen_n % DM_MONEY_SEEN], a->action_id, DM_ID_BYTES);
    m->money_seen_n++;
    return DM_OK;
}

dm_status_t dm_money_ask(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], const dm_money_t *a,
                         uint64_t now_ms)
{
    if (!m || !peer || !a) return DM_ERR_ARG;
    if (!money_valid(a)) return DM_ERR_FORMAT;
    dm_status_t st;
    if (!held_device(m, peer, &st)) return st;
    uint8_t buf[DM_MONEY_WIRE];
    int32_t n = dm_money_encode(a, buf, sizeof buf);
    if (n < 0) return DM_ERR_FORMAT;
    st = dm_send_app(m, peer, DM_APP_MONEY_ASK, buf, (uint32_t) n, now_ms);
    if (st != DM_OK) return st;
    dm_mcpy(&m->money_asked, a, sizeof *a);
    m->money_asked_used = 1;
    return DM_OK;
}

dm_status_t dm_money_answer(dm_mesh_t *m, bool approve, uint64_t now_ms)
{
    if (!m) return DM_ERR_ARG;
    if (!m->money_pending_used) return DM_ERR_STATE;
    m->money_pending_used = 0;
    dm_status_t st = dm_money_confirm_local(m, &m->money_pending, approve, now_ms, &m->conf_tmp);
    if (st != DM_OK) return st;
    int32_t a = dm_money_encode(&m->money_pending, m->app_buf, sizeof m->app_buf);
    if (a < 0) return DM_ERR_FORMAT;
    int32_t b = dm_confirm_encode(&m->conf_tmp, m->app_buf + a,
                                  (uint32_t) (sizeof m->app_buf - (uint32_t) a));
    if (b < 0) return DM_ERR_SIZE;
    return dm_send_app(m, m->money_pending_from, DM_APP_MONEY_ANSWER, m->app_buf,
                       (uint32_t) (a + b), now_ms);
}

dm_status_t dm_wallet_request(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], const dm_money_t *a,
                              uint64_t now_ms, uint32_t *req_id)
{
    if (!m || !peer || !a) return DM_ERR_ARG;
    dm_status_t st = dm_money_confirm_local(m, a, true, now_ms, &m->conf_tmp);
    if (st != DM_OK) return st;
    uint8_t *p = m->app_buf + 5;
    uint32_t cap = (uint32_t) sizeof m->app_buf - 5u;
    int32_t ml = dm_money_encode(a, p + 4, cap - 4u);
    if (ml < 0) return DM_ERR_FORMAT;
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) ((uint32_t) ml >> (24 - 8 * i));
    int32_t cl = dm_confirm_encode(&m->conf_tmp, p + 4 + ml, cap - 4u - (uint32_t) ml);
    if (cl < 0) return DM_ERR_SIZE;
    return req_send(m, peer, DM_REQ_WALLET, p, 4u + (uint32_t) (ml + cl), now_ms, req_id);
}

dm_status_t dm_wallet_check(dm_mesh_t *m, const uint8_t *payload, uint32_t len, uint64_t now_ms,
                            dm_money_t *out)
{
    if (!m || !payload || !out) return DM_ERR_ARG;
    if (len < 4u) return DM_ERR_FORMAT;
    uint32_t ml = ((uint32_t) payload[0] << 24) | ((uint32_t) payload[1] << 16) |
                  ((uint32_t) payload[2] << 8) | payload[3];
    if (ml > len - 4u) return DM_ERR_FORMAT;
    dm_status_t st = dm_money_decode(out, payload + 4, ml);
    if (st != DM_OK) return st;
    st = dm_confirm_decode(&m->conf_tmp, payload + 4 + ml, len - 4u - ml);
    if (st != DM_OK) return st;
    return dm_money_verify(m, out, &m->conf_tmp, now_ms);
}

/* ===== app records ===== */
void dm_remote_on_app(dm_mesh_t *m, dm_peer_t *p, uint8_t app, const uint8_t *d, uint32_t n,
                      uint64_t now_ms)
{
    switch (app) {
    case DM_APP_CAPS:
        if (dm_caps_decode(&p->caps, d, n) == DM_OK) {
            p->have_caps = 1;
            dm_emit(m, DM_EV_CAPS, p->id, 0, 0, 0);
        } else {
            p->have_caps = 0;
        }
        break;
    case DM_APP_SYNC: {
        int32_t c = dm_sync_merge(&m->settings, d, n);
        if (c > 0) dm_emit(m, DM_EV_SYNC, p->id, 0, (uint64_t) c, 0);
        break;
    }
    case DM_APP_REQ:
        if (n < 5u) break;
        if (m->host.on_request) {
            uint32_t id =
                ((uint32_t) d[0] << 24) | ((uint32_t) d[1] << 16) | ((uint32_t) d[2] << 8) | d[3];
            m->host.on_request(m->host.ctx, p->id, id, d[4], d + 5, n - 5u);
        }
        break;
    case DM_APP_REPLY: {
        if (n < 5u) break;
        uint32_t id =
            ((uint32_t) d[0] << 24) | ((uint32_t) d[1] << 16) | ((uint32_t) d[2] << 8) | d[3];
        dm_req_t *q = req_find(m, id, p->id);
        if (!q) break; /* unknown, finished, or already fallen back: drop */
        q->last_ms = now_ms;
        bool final = d[4] != 0;
        if (final) dm_mset(q, 0, sizeof *q);
        if (m->host.on_reply) m->host.on_reply(m->host.ctx, p->id, id, d + 5, n - 5u, final);
        break;
    }
    case DM_APP_MONEY_ASK: {
        dm_status_t st;
        if (!held_device(m, m->self_id, &st)) break;
        if (dm_money_decode(&m->money_pending, d, n) != DM_OK) break;
        dm_mcpy(m->money_pending_from, p->id, DM_ID_BYTES);
        m->money_pending_used = 1;
        if (m->host.on_money) m->host.on_money(m->host.ctx, p->id, &m->money_pending);
        break;
    }
    case DM_APP_MONEY_ANSWER: {
        if (!m->money_asked_used || n < DM_MONEY_WIRE) break;
        dm_money_t a;
        if (dm_money_decode(&a, d, DM_MONEY_WIRE) != DM_OK) break;
        uint8_t x[DM_MONEY_WIRE], y[DM_MONEY_WIRE];
        if (dm_money_encode(&a, x, sizeof x) < 0 ||
            dm_money_encode(&m->money_asked, y, sizeof y) < 0 || !dm_meq(x, y, DM_MONEY_WIRE))
            break;
        if (dm_confirm_decode(&m->conf_tmp, d + DM_MONEY_WIRE, n - DM_MONEY_WIRE) != DM_OK) break;
        if (!dm_meq(m->conf_tmp.confirmer, p->id, DM_ID_BYTES)) break;
        dm_status_t st = dm_money_verify(m, &a, &m->conf_tmp, now_ms);
        m->money_asked_used = 0;
        dm_emit(m, st == DM_OK ? DM_EV_MONEY_CONFIRMED : DM_EV_MONEY_DECLINED, p->id, 0, a.amount,
                st);
        if (m->host.on_money_answer) m->host.on_money_answer(m->host.ctx, &a, &m->conf_tmp, st);
        break;
    }
    default:
        break;
    }
}
