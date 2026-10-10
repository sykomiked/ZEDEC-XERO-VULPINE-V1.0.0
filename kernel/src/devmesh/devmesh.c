/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* devmesh.c — identities, roster, pairing and sessions. See devmesh.h. */
#include "devmesh.h"
#include "dm_internal.h"
#include "dm_sync.h"

#include "../mlkem/keccak.h"
#include "../tls/aead.h"

#define DM_VERSION 1u
#define DM_T_M1    1u
#define DM_T_M2    2u
#define DM_T_M3    3u
#define DM_T_REC   4u

#define DM_PAIR_QR   1u
#define DM_PAIR_CODE 2u

#define CTX_M1     "zxv-devmesh/v1/m1"
#define CTX_M2     "zxv-devmesh/v1/m2"
#define CTX_ROSTER "zxv-devmesh/v1/roster"

static const char CROCK[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/* ===== byte helpers ===== */
void dm_mcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    if (d == s || n == 0) return;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
}

void dm_mset(void *dst, uint8_t v, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

bool dm_meq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

int dm_mcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static bool is_zero(const uint8_t *p, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= p[i];
    return acc == 0;
}

static void name_copy(char dst[DM_NAME_MAX], const char *src)
{
    dm_mset(dst, 0, DM_NAME_MAX);
    if (!src) return;
    for (uint32_t i = 0; i + 1 < DM_NAME_MAX && src[i]; i++) dst[i] = src[i];
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static uint32_t cstrlen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

/* SHA3-256 over the concatenation of up to four parts (bounded scratch). */
static void hash4(const void *a, uint32_t an, const void *b, uint32_t bn, const void *c,
                  uint32_t cn, const void *d, uint32_t dn, uint8_t out[32])
{
    uint8_t buf[256];
    uint32_t n = 0;
    const void *ps[4] = {a, b, c, d};
    uint32_t ls[4] = {an, bn, cn, dn};
    for (int i = 0; i < 4; i++) {
        if (!ps[i] || !ls[i]) continue;
        if (ls[i] > sizeof buf - n) return; /* callers stay far below */
        dm_mcpy(buf + n, ps[i], ls[i]);
        n += ls[i];
    }
    sha3_256(buf, n, out);
    dm_mset(buf, 0, sizeof buf);
}

void dm_emit(dm_mesh_t *m, uint8_t kind, const uint8_t *peer, uint32_t sas, uint64_t u64,
             int32_t status)
{
    if (!m->host.on_event) return;
    dm_event_t ev;
    dm_mset(&ev, 0, sizeof ev);
    ev.kind = kind;
    if (peer) dm_mcpy(ev.peer, peer, DM_ID_BYTES);
    ev.sas = sas;
    ev.u64 = u64;
    ev.status = status;
    m->host.on_event(m->host.ctx, &ev);
}

static void rnd(dm_mesh_t *m, uint8_t *out, uint32_t n)
{
    m->host.random(m->host.ctx, out, n);
}

/* ===== keys ===== */
static dm_status_t keyhash(dm_mesh_t *m, const pqm_sig_pk_t *sp, const pqm_kem_pk_t *kp,
                           uint8_t out[32])
{
    size_t a = pqm_sig_pk_encode(sp, m->kh_buf, sizeof m->kh_buf);
    if (!a) return DM_ERR_FORMAT;
    size_t b = pqm_kem_pk_encode(kp, m->kh_buf + a, sizeof m->kh_buf - a);
    if (!b) return DM_ERR_FORMAT;
    sha3_256(m->kh_buf, a + b, out);
    return DM_OK;
}

dm_peer_t *dm_peer_find(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES])
{
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++)
        if (m->peers[i].used && dm_meq(m->peers[i].id, id, DM_ID_BYTES)) return &m->peers[i];
    return 0;
}

static void peer_drop(dm_peer_t *p)
{
    dm_mset(p, 0, sizeof *p);
}

static dm_peer_t *peer_alloc(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES])
{
    dm_peer_t *p = dm_peer_find(m, id);
    if (p) return p;
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        if (!m->peers[i].used) {
            p = &m->peers[i];
            break;
        }
    }
    if (!p) { /* reuse a slot of a device that is not active in the roster */
        for (uint32_t i = 0; i < DM_MAX_DEVICES && !p; i++) {
            const dm_dev_t *d = dm_roster_find(&m->roster, m->peers[i].id);
            if (!d || d->status != DM_DEV_ACTIVE) p = &m->peers[i];
        }
    }
    if (!p) return 0;
    peer_drop(p);
    p->used = 1;
    dm_mcpy(p->id, id, DM_ID_BYTES);
    return p;
}

static bool sig_pk_of(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], const pqm_sig_pk_t **out)
{
    if (dm_meq(id, m->self_id, DM_ID_BYTES)) {
        *out = &m->sig_pk;
        return true;
    }
    dm_peer_t *p = dm_peer_find(m, id);
    if (!p || !p->have_keys) return false;
    *out = &p->sig_pk;
    return true;
}

/* ===== lifecycle ===== */
dm_status_t dm_init(dm_mesh_t *m, const dm_host_t *host, pqm_level_t level, const uint8_t seed[64],
                    const char *name, dm_role_t role, uint8_t flags)
{
    if (!m || !host || !host->random || !host->send || !seed) return DM_ERR_ARG;
    if (level != PQM_LEVEL_STANDARD && level != PQM_LEVEL_HIGH && level != PQM_LEVEL_MATRIX)
        return DM_ERR_ARG;
    if (role != DM_ROLE_HOME && role != DM_ROLE_THIN && role != DM_ROLE_STANDALONE)
        return DM_ERR_ARG;
    dm_mset(m, 0, sizeof *m);
    dm_mcpy(&m->host, host, sizeof *host);
    m->level = level;
    if (!pqm_sig_keygen(level, seed, &m->sig_pk, &m->sig_sk)) return DM_ERR_CRYPTO;
    if (!pqm_kem_keygen(level, seed + 32, &m->kem_pk, &m->kem_sk)) return DM_ERR_CRYPTO;
    if (keyhash(m, &m->sig_pk, &m->kem_pk, m->self_kh) != DM_OK) return DM_ERR_CRYPTO;
    dm_mcpy(m->self_id, m->self_kh, DM_ID_BYTES);
    name_copy(m->self_name, name);
    m->self_role = (uint8_t) role;
    m->self_flags = flags & (DM_FLAG_ADMIN | DM_FLAG_HELD);
    dm_settings_init(&m->settings);
    return DM_OK;
}

void dm_wipe(dm_mesh_t *m)
{
    if (m) dm_mset(m, 0, sizeof *m);
}

const uint8_t *dm_self_id(const dm_mesh_t *m)
{
    return m->self_id;
}

const dm_roster_t *dm_roster(const dm_mesh_t *m)
{
    return &m->roster;
}

const dm_dev_t *dm_roster_find(const dm_roster_t *r, const uint8_t id[DM_ID_BYTES])
{
    for (uint32_t i = 0; i < r->n && i < DM_MAX_DEVICES; i++)
        if (dm_meq(r->dev[i].id, id, DM_ID_BYTES)) return &r->dev[i];
    return 0;
}

static dm_dev_t *roster_find_mut(dm_roster_t *r, const uint8_t id[DM_ID_BYTES])
{
    return (dm_dev_t *) dm_roster_find(r, id);
}

bool dm_is_admin(const dm_mesh_t *m)
{
    if (!m->joined) return false;
    const dm_dev_t *d = dm_roster_find(&m->roster, m->self_id);
    return d && d->status == DM_DEV_ACTIVE && (d->flags & DM_FLAG_ADMIN);
}

/* ===== roster encoding and signatures ===== */
static int32_t roster_body(const dm_roster_t *r, uint8_t *out, uint32_t cap)
{
    dm_w_t w;
    dm_w_init(&w, out, cap);
    dm_w_bytes(&w, "DMR1", 4);
    dm_w_u64(&w, r->version);
    dm_w_bytes(&w, r->mesh_id, DM_ID_BYTES);
    dm_w_u8(&w, r->n);
    for (uint32_t i = 0; i < r->n && i < DM_MAX_DEVICES; i++) {
        const dm_dev_t *d = &r->dev[i];
        dm_w_bytes(&w, d->id, DM_ID_BYTES);
        dm_w_bytes(&w, d->keyhash, DM_HASH_BYTES);
        dm_w_bytes(&w, d->name, DM_NAME_MAX);
        dm_w_u8(&w, d->role);
        dm_w_u8(&w, d->flags);
        dm_w_u8(&w, d->status);
        dm_w_u64(&w, d->since);
    }
    dm_w_bytes(&w, r->signer, DM_ID_BYTES);
    return w.err ? -1 : (int32_t) w.n;
}

#define DM_ROSTER_BODY_MAX (4u + 8u + DM_ID_BYTES + 1u + DM_MAX_DEVICES * 91u + DM_ID_BYTES)

int32_t dm_roster_encode(const dm_roster_t *r, uint8_t *out, uint32_t cap)
{
    if (!r || !out) return -1;
    int32_t b = roster_body(r, out, cap);
    if (b < 0) return -1;
    dm_w_t w;
    dm_w_init(&w, out + b, cap - (uint32_t) b);
    dm_w_u32(&w, r->sig_len);
    dm_w_bytes(&w, r->sig, r->sig_len);
    return w.err ? -1 : b + (int32_t) w.n;
}

static dm_status_t roster_decode(dm_roster_t *r, const uint8_t *in, uint32_t len)
{
    dm_r_t rd;
    dm_r_init(&rd, in, len);
    dm_mset(r, 0, sizeof *r);
    const uint8_t *mg = dm_r_take(&rd, 4);
    if (!mg || !dm_meq(mg, "DMR1", 4)) return DM_ERR_FORMAT;
    r->version = dm_r_u64(&rd);
    dm_r_bytes(&rd, r->mesh_id, DM_ID_BYTES);
    r->n = dm_r_u8(&rd);
    if (r->n == 0 || r->n > DM_MAX_DEVICES) return DM_ERR_FORMAT;
    for (uint32_t i = 0; i < r->n; i++) {
        dm_dev_t *d = &r->dev[i];
        dm_r_bytes(&rd, d->id, DM_ID_BYTES);
        dm_r_bytes(&rd, d->keyhash, DM_HASH_BYTES);
        dm_r_bytes(&rd, d->name, DM_NAME_MAX);
        d->role = dm_r_u8(&rd);
        d->flags = dm_r_u8(&rd);
        d->status = dm_r_u8(&rd);
        d->since = dm_r_u64(&rd);
        if (rd.err) return DM_ERR_FORMAT;
        if (d->name[DM_NAME_MAX - 1] != 0) return DM_ERR_FORMAT;
        if (d->role < DM_ROLE_HOME || d->role > DM_ROLE_STANDALONE) return DM_ERR_FORMAT;
        if (d->status != DM_DEV_ACTIVE && d->status != DM_DEV_REVOKED) return DM_ERR_FORMAT;
        if (d->flags & ~(DM_FLAG_ADMIN | DM_FLAG_HELD)) return DM_ERR_FORMAT;
        if (!dm_meq(d->id, d->keyhash, DM_ID_BYTES)) return DM_ERR_FORMAT;
        for (uint32_t j = 0; j < i; j++)
            if (dm_meq(r->dev[j].id, d->id, DM_ID_BYTES)) return DM_ERR_FORMAT;
    }
    dm_r_bytes(&rd, r->signer, DM_ID_BYTES);
    r->sig_len = dm_r_u32(&rd);
    if (rd.err || r->sig_len > PQM_SIG_MAX_BYTES) return DM_ERR_FORMAT;
    dm_r_bytes(&rd, r->sig, r->sig_len);
    return dm_r_done(&rd) ? DM_OK : DM_ERR_FORMAT;
}

static dm_status_t roster_sign(dm_mesh_t *m, dm_roster_t *r)
{
    uint8_t body[DM_ROSTER_BODY_MAX];
    uint8_t rn[32];
    dm_mcpy(r->signer, m->self_id, DM_ID_BYTES);
    int32_t b = roster_body(r, body, sizeof body);
    if (b < 0) return DM_ERR_SIZE;
    rnd(m, rn, 32);
    if (!pqm_sign(&m->sig_sk, body, (size_t) b, (const uint8_t *) CTX_ROSTER, sizeof CTX_ROSTER - 1,
                  rn, &m->sig))
        return DM_ERR_CRYPTO;
    size_t sl = pqm_sig_encode(&m->sig, r->sig, sizeof r->sig);
    if (!sl) return DM_ERR_CRYPTO;
    r->sig_len = (uint32_t) sl;
    return DM_OK;
}

static bool roster_verify(dm_mesh_t *m, const dm_roster_t *r, const pqm_sig_pk_t *pk)
{
    uint8_t body[DM_ROSTER_BODY_MAX];
    int32_t b = roster_body(r, body, sizeof body);
    if (b < 0) return false;
    if (!pqm_sig_decode(&m->sig, r->sig, r->sig_len)) return false;
    return pqm_verify(pk, body, (size_t) b, (const uint8_t *) CTX_ROSTER, sizeof CTX_ROSTER - 1,
                      &m->sig);
}

static uint32_t active_admins(const dm_roster_t *r)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < r->n; i++)
        if (r->dev[i].status == DM_DEV_ACTIVE && (r->dev[i].flags & DM_FLAG_ADMIN)) k++;
    return k;
}

/* ===== records ===== */
static void hdr(dm_mesh_t *m, dm_w_t *w, uint8_t type, const uint8_t *mesh_id)
{
    dm_w_u8(w, 'D');
    dm_w_u8(w, 'M');
    dm_w_u8(w, DM_VERSION);
    dm_w_u8(w, type);
    if (mesh_id)
        dm_w_bytes(w, mesh_id, DM_ID_BYTES);
    else {
        uint8_t z[DM_ID_BYTES] = {0};
        dm_w_bytes(w, z, DM_ID_BYTES);
    }
    dm_w_bytes(w, m->self_id, DM_ID_BYTES);
}

static void nonce_for(uint64_t seq, uint8_t n[12])
{
    for (int i = 0; i < 4; i++) n[i] = 0;
    for (int i = 0; i < 8; i++) n[4 + i] = (uint8_t) (seq >> (8 * i));
}

static dm_status_t seal_send(dm_mesh_t *m, dm_peer_t *p, uint8_t app, const uint8_t *data,
                             uint32_t len)
{
    if (len > DM_REC_MAX - 1u) return DM_ERR_SIZE;
    dm_w_t w;
    dm_w_init(&w, m->tx, sizeof m->tx);
    hdr(m, &w, DM_T_REC, m->joined ? m->mesh_id : 0);
    uint64_t seq = p->s.tx_seq++;
    dm_w_u64(&w, seq);
    uint32_t aad_len = w.n;
    uint8_t *ct = dm_w_reserve(&w, len + 1u);
    uint8_t *tag = dm_w_reserve(&w, 16);
    if (w.err) return DM_ERR_SIZE;
    ct[0] = app;
    if (len) dm_mcpy(ct + 1, data, len);
    uint8_t nonce[12];
    nonce_for(seq, nonce);
    aead_seal(p->s.k_tx, nonce, m->tx, aad_len, ct, ct, len + 1u, tag);
    int rc = m->host.send(m->host.ctx, p->id, m->tx, w.n);
    return rc == 0 ? DM_OK : DM_ERR_TRANSPORT;
}

dm_status_t dm_send_app(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint8_t app,
                        const uint8_t *data, uint32_t len, uint64_t now_ms)
{
    (void) now_ms;
    if (!m || !peer || (len && !data)) return DM_ERR_ARG;
    dm_peer_t *p = dm_peer_find(m, peer);
    if (!p) return DM_ERR_NOPEER;
    if (p->s.state == DM_SESS_PAIR_SAS) {
        if (app != DM_APP_SAS_OK && app != DM_APP_ROSTER && app != DM_APP_KEYS) return DM_ERR_STATE;
    } else if (p->s.state != DM_SESS_UP) {
        return DM_ERR_NOPEER;
    }
    return seal_send(m, p, app, data, len);
}

static void send_roster_to(dm_mesh_t *m, dm_peer_t *p, uint64_t now)
{
    int32_t n = dm_roster_encode(&m->roster, m->app_buf, sizeof m->app_buf);
    if (n > 0) (void) dm_send_app(m, p->id, DM_APP_ROSTER, m->app_buf, (uint32_t) n, now);
}

static void send_keys_to(dm_mesh_t *m, dm_peer_t *to, const uint8_t id[DM_ID_BYTES],
                         const pqm_sig_pk_t *sp, const pqm_kem_pk_t *kp, uint64_t now)
{
    dm_w_t w;
    dm_w_init(&w, m->app_buf, sizeof m->app_buf);
    dm_w_bytes(&w, id, DM_ID_BYTES);
    uint8_t *lenp = dm_w_reserve(&w, 4);
    if (w.err) return;
    size_t a = pqm_sig_pk_encode(sp, m->app_buf + w.n, w.cap - w.n);
    if (!a) return;
    w.n += (uint32_t) a;
    size_t b = pqm_kem_pk_encode(kp, m->app_buf + w.n, w.cap - w.n);
    if (!b) return;
    w.n += (uint32_t) b;
    for (int i = 0; i < 4; i++) lenp[i] = (uint8_t) (a >> (24 - 8 * i));
    (void) dm_send_app(m, to->id, DM_APP_KEYS, m->app_buf, w.n, now);
}

static void session_up(dm_mesh_t *m, dm_peer_t *p, uint64_t now)
{
    p->s.last_rx_ms = now;
    p->s.last_ping_ms = now;
    dm_emit(m, DM_EV_PEER_UP, p->id, 0, 0, 0);
    send_roster_to(m, p, now);
    dm_remote_on_session_up(m, p, now);
}

/* ===== roster install ===== */
static void forget_mesh(dm_mesh_t *m)
{
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) peer_drop(&m->peers[i]);
    dm_mset(&m->roster, 0, sizeof m->roster);
    dm_mset(m->mesh_id, 0, DM_ID_BYTES);
    m->joined = 0;
    m->inv_active = 0;
    m->hs_active = 0;
    dm_settings_init(&m->settings);
}

static void broadcast_roster(dm_mesh_t *m, const uint8_t *except, uint64_t now)
{
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        dm_peer_t *p = &m->peers[i];
        if (!p->used || p->s.state != DM_SESS_UP) continue;
        if (except && dm_meq(p->id, except, DM_ID_BYTES)) continue;
        send_roster_to(m, p, now);
    }
}

/* Replace m->roster with m->roster_tmp and apply the consequences. */
static void install_roster(dm_mesh_t *m, const uint8_t *from, uint64_t now)
{
    dm_mcpy(&m->roster, &m->roster_tmp, sizeof m->roster);
    for (uint32_t i = 0; i < m->roster.n; i++) {
        const dm_dev_t *d = &m->roster.dev[i];
        if (dm_meq(d->id, m->self_id, DM_ID_BYTES)) {
            if (d->status == DM_DEV_REVOKED) {
                forget_mesh(m);
                dm_emit(m, DM_EV_SELF_REVOKED, m->self_id, 0, 0, 0);
                return;
            }
            name_copy(m->self_name, d->name);
            m->self_role = d->role;
            m->self_flags = d->flags;
            continue;
        }
        dm_peer_t *p = dm_peer_find(m, d->id);
        if (d->status == DM_DEV_REVOKED) {
            if (p) {
                peer_drop(p);
                dm_emit(m, DM_EV_REVOKED, d->id, 0, 0, 0);
            }
        } else if (p) {
            name_copy(p->name, d->name);
            p->paired_role = d->role;
            p->paired_flags = d->flags;
        }
    }
    dm_emit(m, DM_EV_ROSTER, 0, 0, m->roster.version, 0);
    broadcast_roster(m, from, now);
}

/* Rules for a roster from a peer, relative to the one held (see header). */
static dm_status_t check_successor(dm_mesh_t *m, const dm_roster_t *cur, const dm_roster_t *nr)
{
    if (!dm_meq(nr->mesh_id, cur->mesh_id, DM_ID_BYTES)) return DM_ERR_FORMAT;
    if (nr->version <= cur->version) return DM_ERR_REPLAY;
    const dm_dev_t *s = dm_roster_find(cur, nr->signer);
    if (!s) return DM_ERR_UNKNOWN;
    if (s->status != DM_DEV_ACTIVE) return DM_ERR_REVOKED;
    if (!(s->flags & DM_FLAG_ADMIN)) return DM_ERR_PERM;
    for (uint32_t i = 0; i < cur->n; i++) {
        const dm_dev_t *o = &cur->dev[i];
        const dm_dev_t *n = dm_roster_find(nr, o->id);
        if (!n) return DM_ERR_PERM; /* no device is ever dropped */
        if (!dm_meq(n->keyhash, o->keyhash, DM_HASH_BYTES)) return DM_ERR_PERM;
        if (o->status == DM_DEV_REVOKED && n->status != DM_DEV_REVOKED) return DM_ERR_REVOKED;
    }
    if (active_admins(nr) == 0) return DM_ERR_PERM;
    const pqm_sig_pk_t *pk;
    if (!sig_pk_of(m, nr->signer, &pk)) return DM_ERR_UNKNOWN;
    if (!roster_verify(m, nr, pk)) return DM_ERR_AUTH;
    return DM_OK;
}

dm_status_t dm_roster_accept(dm_mesh_t *m, const uint8_t *enc, uint32_t len, uint64_t now_ms)
{
    if (!m || !enc) return DM_ERR_ARG;
    if (!m->joined) return DM_ERR_STATE;
    dm_status_t st = roster_decode(&m->roster_tmp, enc, len);
    if (st != DM_OK) return st;
    st = check_successor(m, &m->roster, &m->roster_tmp);
    if (st != DM_OK) return st;
    install_roster(m, m->roster_tmp.signer, now_ms);
    return DM_OK;
}

/* Admin edit: m->roster_tmp holds the edited copy. */
static dm_status_t admin_commit(dm_mesh_t *m, uint64_t now)
{
    m->roster_tmp.version = m->roster.version + 1u;
    if (active_admins(&m->roster_tmp) == 0) return DM_ERR_PERM;
    dm_status_t st = roster_sign(m, &m->roster_tmp);
    if (st != DM_OK) return st;
    install_roster(m, 0, now);
    return DM_OK;
}

static dm_status_t admin_begin(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], dm_dev_t **d)
{
    if (!dm_is_admin(m)) return DM_ERR_PERM;
    dm_mcpy(&m->roster_tmp, &m->roster, sizeof m->roster);
    *d = roster_find_mut(&m->roster_tmp, id);
    if (!*d) return DM_ERR_UNKNOWN;
    if ((*d)->status != DM_DEV_ACTIVE) return DM_ERR_REVOKED;
    return DM_OK;
}

dm_status_t dm_rename(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], const char *name,
                      uint64_t now_ms)
{
    if (!m || !id || !name) return DM_ERR_ARG;
    dm_dev_t *d;
    dm_status_t st = admin_begin(m, id, &d);
    if (st != DM_OK) return st;
    name_copy(d->name, name);
    d->since = m->roster.version + 1u;
    return admin_commit(m, now_ms);
}

dm_status_t dm_set_role(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], uint8_t role, uint8_t flags,
                        uint64_t now_ms)
{
    if (!m || !id) return DM_ERR_ARG;
    if (role < DM_ROLE_HOME || role > DM_ROLE_STANDALONE) return DM_ERR_ARG;
    if (flags & ~(DM_FLAG_ADMIN | DM_FLAG_HELD)) return DM_ERR_ARG;
    dm_dev_t *d;
    dm_status_t st = admin_begin(m, id, &d);
    if (st != DM_OK) return st;
    d->role = role;
    d->flags = flags;
    d->since = m->roster.version + 1u;
    return admin_commit(m, now_ms);
}

dm_status_t dm_revoke(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], uint64_t now_ms)
{
    if (!m || !id) return DM_ERR_ARG;
    dm_dev_t *d;
    dm_status_t st = admin_begin(m, id, &d);
    if (st != DM_OK) return st;
    d->status = DM_DEV_REVOKED;
    d->since = m->roster.version + 1u;
    return admin_commit(m, now_ms); /* refuses to revoke the last admin */
}

dm_status_t dm_create(dm_mesh_t *m, uint64_t now_ms)
{
    (void) now_ms;
    if (!m) return DM_ERR_ARG;
    if (m->joined) return DM_ERR_STATE;
    dm_roster_t *r = &m->roster;
    dm_mset(r, 0, sizeof *r);
    rnd(m, m->mesh_id, DM_ID_BYTES);
    dm_mcpy(r->mesh_id, m->mesh_id, DM_ID_BYTES);
    r->version = 1;
    r->n = 1;
    dm_dev_t *d = &r->dev[0];
    dm_mcpy(d->id, m->self_id, DM_ID_BYTES);
    dm_mcpy(d->keyhash, m->self_kh, DM_HASH_BYTES);
    name_copy(d->name, m->self_name);
    d->role = m->self_role;
    m->self_flags |= DM_FLAG_ADMIN;
    d->flags = m->self_flags;
    d->status = DM_DEV_ACTIVE;
    d->since = 1;
    dm_status_t st = roster_sign(m, r);
    if (st != DM_OK) return st;
    m->joined = 1;
    return DM_OK;
}

/* ===== text helpers ===== */
int32_t dm_b32_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap)
{
    if (!in || !out) return -1;
    uint32_t need = (len * 8u + 4u) / 5u;
    if (need + 1u > cap) return -1;
    uint32_t acc = 0, bits = 0, o = 0;
    for (uint32_t i = 0; i < len; i++) {
        acc = (acc << 8) | in[i];
        bits += 8;
        while (bits >= 5) {
            out[o++] = B32[(acc >> (bits - 5)) & 31u];
            bits -= 5;
        }
        acc &= (1u << bits) - 1u;
    }
    if (bits) out[o++] = B32[(acc << (5 - bits)) & 31u];
    out[o] = 0;
    return (int32_t) o;
}

int32_t dm_b32_decode(const char *in, uint8_t *out, uint32_t cap)
{
    if (!in || !out) return -1;
    uint32_t acc = 0, bits = 0, o = 0;
    for (uint32_t i = 0; in[i]; i++) {
        char c = in[i];
        uint32_t v;
        if (c >= 'a' && c <= 'z') c = (char) (c - 32);
        if (c >= 'A' && c <= 'Z')
            v = (uint32_t) (c - 'A');
        else if (c >= '2' && c <= '7')
            v = (uint32_t) (c - '2') + 26u;
        else
            return -1;
        acc = (acc << 5) | v;
        bits += 5;
        if (bits >= 8) {
            if (o >= cap) return -1;
            out[o++] = (uint8_t) (acc >> (bits - 8));
            bits -= 8;
        }
        acc &= (1u << bits) - 1u;
    }
    return (int32_t) o;
}

/* Normalise a short code: Crockford base32, case-insensitive, dashes and
 * spaces ignored, I/L -> 1, O -> 0. */
static bool code_norm(const char *code, char out[DM_CODE_CHARS])
{
    uint32_t n = 0;
    for (uint32_t i = 0; code[i] && i < 64; i++) {
        char c = code[i];
        if (c == '-' || c == ' ') continue;
        if (c >= 'a' && c <= 'z') c = (char) (c - 32);
        if (c == 'I' || c == 'L') c = '1';
        if (c == 'O') c = '0';
        bool ok = false;
        for (uint32_t k = 0; k < 32; k++)
            if (CROCK[k] == c) ok = true;
        if (!ok || n >= DM_CODE_CHARS) return false;
        out[n++] = c;
    }
    return n == DM_CODE_CHARS;
}

static void code_secret(const char norm[DM_CODE_CHARS], uint8_t out[32])
{
    static const char L[] = "zxv-devmesh/v1/code";
    hash4(L, sizeof L - 1, norm, DM_CODE_CHARS, 0, 0, 0, 0, out);
}

dm_status_t dm_code_tag(const char *code, uint8_t tag[8])
{
    static const char L[] = "zxv-devmesh/v1/code-tag";
    char norm[DM_CODE_CHARS];
    uint8_t h[32];
    if (!code || !tag || !code_norm(code, norm)) return DM_ERR_FORMAT;
    hash4(L, sizeof L - 1, norm, DM_CODE_CHARS, 0, 0, 0, 0, h);
    dm_mcpy(tag, h, 8);
    return DM_OK;
}

/* ===== pairing: invitations ===== */
dm_status_t dm_invite(dm_mesh_t *m, uint8_t new_role, uint8_t new_flags, const uint8_t *addr,
                      uint32_t addr_len, uint64_t now_ms, uint8_t qr[DM_QR_MAX], uint32_t *qr_len,
                      char code[DM_CODE_CHARS + 1u])
{
    if (!m || !qr || !qr_len || !code) return DM_ERR_ARG;
    if (addr_len > DM_ADDR_MAX || (addr_len && !addr)) return DM_ERR_ARG;
    if (new_role < DM_ROLE_HOME || new_role > DM_ROLE_STANDALONE) return DM_ERR_ARG;
    if (new_flags & ~(DM_FLAG_ADMIN | DM_FLAG_HELD)) return DM_ERR_ARG;
    if (!dm_is_admin(m)) return DM_ERR_PERM;
    if (m->roster.n >= DM_MAX_DEVICES) return DM_ERR_FULL;
    rnd(m, m->inv_qr, 32);
    uint8_t cr[DM_CODE_CHARS];
    rnd(m, cr, DM_CODE_CHARS);
    char norm[DM_CODE_CHARS];
    for (uint32_t i = 0; i < DM_CODE_CHARS; i++) {
        norm[i] = CROCK[cr[i] & 31u];
        code[i] = norm[i];
    }
    code[DM_CODE_CHARS] = 0;
    code_secret(norm, m->inv_code);
    m->inv_active = 1;
    m->inv_tries = DM_PAIR_TRIES;
    m->inv_role = new_role;
    m->inv_flags = new_flags;
    m->inv_expires = now_ms + DM_INVITE_TTL_MS;
    dm_w_t w;
    dm_w_init(&w, qr, DM_QR_MAX);
    dm_w_bytes(&w, "ZXVP1", 5);
    dm_w_bytes(&w, m->mesh_id, DM_ID_BYTES);
    dm_w_bytes(&w, m->self_id, DM_ID_BYTES);
    dm_w_bytes(&w, m->self_kh, DM_HASH_BYTES);
    dm_w_bytes(&w, m->inv_qr, 32);
    dm_w_u8(&w, (uint8_t) addr_len);
    if (addr_len) dm_w_bytes(&w, addr, addr_len);
    if (w.err) return DM_ERR_SIZE;
    *qr_len = w.n;
    dm_mset(cr, 0, sizeof cr);
    return DM_OK;
}

/* ===== handshake ===== */
static void mac(const uint8_t secret[32], const char *label, const uint8_t a[32],
                const uint8_t b[32], uint8_t out[32])
{
    hash4(label, cstrlen(label, 64), secret, 32, a, 32, b, b ? 32u : 0u, out);
}

static bool put_blob_sigpk(dm_w_t *w, const pqm_sig_pk_t *pk)
{
    uint8_t *lp = dm_w_reserve(w, 4);
    if (!lp) return false;
    size_t n = pqm_sig_pk_encode(pk, w->p + w->n, w->cap - w->n);
    if (!n) return (w->err = 1), false;
    w->n += (uint32_t) n;
    for (int i = 0; i < 4; i++) lp[i] = (uint8_t) (n >> (24 - 8 * i));
    return true;
}

static bool put_blob_kempk(dm_w_t *w, const pqm_kem_pk_t *pk)
{
    uint8_t *lp = dm_w_reserve(w, 4);
    if (!lp) return false;
    size_t n = pqm_kem_pk_encode(pk, w->p + w->n, w->cap - w->n);
    if (!n) return (w->err = 1), false;
    w->n += (uint32_t) n;
    for (int i = 0; i < 4; i++) lp[i] = (uint8_t) (n >> (24 - 8 * i));
    return true;
}

static bool put_blob_ct(dm_w_t *w, const pqm_kem_ct_t *ct)
{
    uint8_t *lp = dm_w_reserve(w, 4);
    if (!lp) return false;
    size_t n = pqm_kem_ct_encode(ct, w->p + w->n, w->cap - w->n);
    if (!n) return (w->err = 1), false;
    w->n += (uint32_t) n;
    for (int i = 0; i < 4; i++) lp[i] = (uint8_t) (n >> (24 - 8 * i));
    return true;
}

static bool put_blob_sig(dm_w_t *w, const pqm_sig_t *s)
{
    uint8_t *lp = dm_w_reserve(w, 4);
    if (!lp) return false;
    size_t n = pqm_sig_encode(s, w->p + w->n, w->cap - w->n);
    if (!n) return (w->err = 1), false;
    w->n += (uint32_t) n;
    for (int i = 0; i < 4; i++) lp[i] = (uint8_t) (n >> (24 - 8 * i));
    return true;
}

static const uint8_t *get_blob(dm_r_t *r, uint32_t *n)
{
    *n = dm_r_u32(r);
    return dm_r_take(r, *n);
}

static dm_status_t send_m1(dm_mesh_t *m, const uint8_t to[DM_ID_BYTES], uint8_t mode,
                           uint8_t pair_kind, const uint8_t *mesh_id, uint64_t now)
{
    uint8_t seed[32], nonce[32], h[32], rn[32];
    rnd(m, seed, 32);
    if (!pqm_kem_keygen(m->level, seed, &m->eph_pk, &m->eph_sk)) return DM_ERR_CRYPTO;
    dm_mset(seed, 0, 32);
    rnd(m, nonce, 32);
    dm_w_t w;
    dm_w_init(&w, m->tx, sizeof m->tx);
    hdr(m, &w, DM_T_M1, mesh_id);
    dm_w_u8(&w, mode);
    dm_w_u8(&w, pair_kind);
    dm_w_bytes(&w, nonce, 32);
    put_blob_kempk(&w, &m->eph_pk);
    if (mode == DM_MODE_PAIR) {
        put_blob_sigpk(&w, &m->sig_pk);
        put_blob_kempk(&w, &m->kem_pk);
        dm_w_bytes(&w, m->self_name, DM_NAME_MAX);
        dm_w_u8(&w, m->self_role);
        if (w.err) return DM_ERR_SIZE;
        sha3_256(m->tx, w.n, h);
        uint8_t mc[32];
        mac(m->join_secret, "zxv-devmesh/v1/mac1", h, 0, mc);
        dm_w_bytes(&w, mc, 32);
    }
    if (w.err) return DM_ERR_SIZE;
    sha3_256(m->tx, w.n, h);
    rnd(m, rn, 32);
    if (!pqm_sign(&m->sig_sk, h, 32, (const uint8_t *) CTX_M1, sizeof CTX_M1 - 1, rn, &m->sig))
        return DM_ERR_CRYPTO;
    put_blob_sig(&w, &m->sig);
    if (w.err) return DM_ERR_SIZE;
    sha3_256(m->tx, w.n, m->hs_th1);
    m->hs_active = 1;
    m->hs_mode = mode;
    m->hs_started = now;
    dm_mcpy(m->hs_peer, to, DM_ID_BYTES);
    return m->host.send(m->host.ctx, to, m->tx, w.n) == 0 ? DM_OK : DM_ERR_TRANSPORT;
}

dm_status_t dm_connect(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint64_t now_ms)
{
    if (!m || !peer) return DM_ERR_ARG;
    if (!m->joined) return DM_ERR_STATE;
    const dm_dev_t *d = dm_roster_find(&m->roster, peer);
    if (!d) return DM_ERR_UNKNOWN;
    if (d->status != DM_DEV_ACTIVE) return DM_ERR_REVOKED;
    dm_peer_t *p = dm_peer_find(m, peer);
    if (!p || !p->have_keys) return DM_ERR_UNKNOWN;
    if (p->s.state == DM_SESS_UP && now_ms - p->s.last_rx_ms <= DM_LIVE_MS) return DM_OK;
    if (m->hs_active && now_ms - m->hs_started < DM_LIVE_MS) return DM_ERR_STATE;
    return send_m1(m, peer, DM_MODE_SESSION, 0, m->mesh_id, now_ms);
}

dm_status_t dm_join_qr(dm_mesh_t *m, const uint8_t *qr, uint32_t qr_len, uint64_t now_ms)
{
    if (!m || !qr) return DM_ERR_ARG;
    if (m->joined) return DM_ERR_STATE;
    dm_r_t r;
    dm_r_init(&r, qr, qr_len);
    const uint8_t *mg = dm_r_take(&r, 5);
    if (!mg || !dm_meq(mg, "ZXVP1", 5)) return DM_ERR_FORMAT;
    dm_r_bytes(&r, m->mesh_id, DM_ID_BYTES);
    dm_r_bytes(&r, m->join_peer, DM_ID_BYTES);
    dm_r_bytes(&r, m->join_kh, DM_HASH_BYTES);
    dm_r_bytes(&r, m->join_secret, 32);
    uint8_t al = dm_r_u8(&r);
    if (al > DM_ADDR_MAX || !dm_r_take(&r, al) || !dm_r_done(&r)) return DM_ERR_FORMAT;
    if (!dm_meq(m->join_peer, m->join_kh, DM_ID_BYTES)) return DM_ERR_FORMAT;
    m->join_active = 1;
    m->join_have_kh = 1;
    return send_m1(m, m->join_peer, DM_MODE_PAIR, DM_PAIR_QR, m->mesh_id, now_ms);
}

dm_status_t dm_join_code(dm_mesh_t *m, const char *code, uint64_t now_ms)
{
    if (!m || !code) return DM_ERR_ARG;
    if (m->joined) return DM_ERR_STATE;
    char norm[DM_CODE_CHARS];
    if (!code_norm(code, norm)) return DM_ERR_FORMAT;
    code_secret(norm, m->join_secret);
    dm_mset(m->join_peer, 0, DM_ID_BYTES);
    dm_mset(m->mesh_id, 0, DM_ID_BYTES);
    m->join_active = 1;
    m->join_have_kh = 0;
    return send_m1(m, m->join_peer, DM_MODE_PAIR, DM_PAIR_CODE, 0, now_ms);
}

static void derive(dm_sess_t *s, uint8_t mode, const uint8_t ss_a[32], const uint8_t ss_b[32],
                   const uint8_t *psk, bool initiator)
{
    static const char L[] = "zxv-devmesh/v1/kdf";
    uint8_t in[sizeof L - 1 + 1 + 32 * 4], out[128];
    uint32_t n = 0;
    dm_mcpy(in, L, sizeof L - 1);
    n += sizeof L - 1;
    in[n++] = mode;
    dm_mcpy(in + n, ss_a, 32);
    n += 32;
    dm_mcpy(in + n, ss_b, 32);
    n += 32;
    if (psk)
        dm_mcpy(in + n, psk, 32);
    else
        dm_mset(in + n, 0, 32);
    n += 32;
    dm_mcpy(in + n, s->th, 32);
    n += 32;
    shake256(in, n, out, sizeof out);
    dm_mcpy(initiator ? s->k_tx : s->k_rx, out, 32);
    dm_mcpy(initiator ? s->k_rx : s->k_tx, out + 32, 32);
    s->sas = le32(out + 64) % DM_SAS_MOD;
    dm_mcpy(s->k_ext, out + 96, 32);
    dm_mset(in, 0, sizeof in);
    dm_mset(out, 0, sizeof out);
}

static dm_status_t on_m1(dm_mesh_t *m, const uint8_t *f, uint32_t len, uint64_t now)
{
    const uint8_t *mesh_id = f + 4, *sender = f + 20;
    dm_r_t r;
    dm_r_init(&r, f, len);
    dm_r_take(&r, DM_HDR_BYTES);
    uint8_t mode = dm_r_u8(&r), kind = dm_r_u8(&r);
    dm_r_take(&r, 32); /* nonce: makes every M1 distinct */
    uint32_t eph_len, sp_len = 0, kp_len = 0;
    const uint8_t *eph = get_blob(&r, &eph_len), *sp = 0, *kp = 0;
    char name[DM_NAME_MAX];
    uint8_t role = 0;
    const uint8_t *mc = 0;
    if (mode == DM_MODE_PAIR) {
        sp = get_blob(&r, &sp_len);
        kp = get_blob(&r, &kp_len);
        dm_r_bytes(&r, name, DM_NAME_MAX);
        role = dm_r_u8(&r);
        if (r.err) return DM_ERR_FORMAT;
        mc = dm_r_take(&r, 32);
    }
    uint32_t signed_len = r.off;
    uint32_t sig_len;
    const uint8_t *sg = get_blob(&r, &sig_len);
    if (!dm_r_done(&r) || !eph || !sg) return DM_ERR_FORMAT;
    if (dm_meq(sender, m->self_id, DM_ID_BYTES)) return DM_ERR_FORMAT;
    (void) role;

    dm_peer_t *p = 0;
    const pqm_sig_pk_t *spk = 0;
    const pqm_kem_pk_t *kpk = 0;
    const uint8_t *psk = 0;
    if (mode == DM_MODE_SESSION) {
        if (!m->joined || !dm_meq(mesh_id, m->mesh_id, DM_ID_BYTES)) return DM_ERR_UNKNOWN;
        const dm_dev_t *d = dm_roster_find(&m->roster, sender);
        if (!d) return DM_ERR_UNKNOWN;
        if (d->status != DM_DEV_ACTIVE) return DM_ERR_REVOKED;
        p = dm_peer_find(m, sender);
        if (!p || !p->have_keys) return DM_ERR_UNKNOWN;
        /* glare: both sides started; the smaller id stays initiator */
        if (m->hs_active && dm_meq(m->hs_peer, sender, DM_ID_BYTES)) {
            if (dm_mcmp(m->self_id, sender, DM_ID_BYTES) < 0) return DM_OK;
            m->hs_active = 0;
        }
        spk = &p->sig_pk;
        kpk = &p->kem_pk;
    } else if (mode == DM_MODE_PAIR) {
        if (!m->joined || !m->inv_active || !dm_is_admin(m)) return DM_ERR_STATE;
        if (now > m->inv_expires) {
            m->inv_active = 0;
            return DM_ERR_EXPIRED;
        }
        if (kind == DM_PAIR_QR) {
            if (!dm_meq(mesh_id, m->mesh_id, DM_ID_BYTES)) return DM_ERR_UNKNOWN;
            psk = m->inv_qr;
        } else if (kind == DM_PAIR_CODE) {
            psk = m->inv_code;
        } else {
            return DM_ERR_FORMAT;
        }
        uint8_t h[32], want[32];
        sha3_256(f, signed_len - 32u, h);
        mac(psk, "zxv-devmesh/v1/mac1", h, 0, want);
        if (!dm_meq(want, mc, 32)) {
            if (m->inv_tries) m->inv_tries--;
            if (!m->inv_tries) {
                m->inv_active = 0;
                dm_mset(m->inv_qr, 0, 32);
                dm_mset(m->inv_code, 0, 32);
            }
            return DM_ERR_AUTH;
        }
        if (!pqm_sig_pk_decode(&m->tmp_sig_pk, sp, sp_len)) return DM_ERR_FORMAT;
        if (!pqm_kem_pk_decode(&m->tmp_kem_pk, kp, kp_len)) return DM_ERR_FORMAT;
        if (m->tmp_sig_pk.level != m->level || m->tmp_kem_pk.level != m->level)
            return DM_ERR_FORMAT;
        uint8_t kh[32];
        if (keyhash(m, &m->tmp_sig_pk, &m->tmp_kem_pk, kh) != DM_OK) return DM_ERR_FORMAT;
        if (!dm_meq(kh, sender, DM_ID_BYTES)) return DM_ERR_FORMAT;
        const dm_dev_t *d = dm_roster_find(&m->roster, sender);
        if (d) return d->status == DM_DEV_REVOKED ? DM_ERR_REVOKED : DM_ERR_STATE;
        spk = &m->tmp_sig_pk;
        kpk = &m->tmp_kem_pk;
        if (!name[0] || name[DM_NAME_MAX - 1]) return DM_ERR_FORMAT;
        p = 0; /* allocated after the signature checks out */
        uint8_t h2[32];
        sha3_256(f, signed_len, h2);
        if (!pqm_sig_decode(&m->sig, sg, sig_len) ||
            !pqm_verify(spk, h2, 32, (const uint8_t *) CTX_M1, sizeof CTX_M1 - 1, &m->sig))
            return DM_ERR_AUTH;
        p = peer_alloc(m, sender);
        if (!p) return DM_ERR_FULL;
        dm_mcpy(&p->sig_pk, &m->tmp_sig_pk, sizeof p->sig_pk);
        dm_mcpy(&p->kem_pk, &m->tmp_kem_pk, sizeof p->kem_pk);
        dm_mcpy(p->keyhash, kh, 32);
        name_copy(p->name, name);
        p->have_keys = 1;
        p->paired_role = m->inv_role;
        p->paired_flags = m->inv_flags;
        spk = 0; /* already verified */
        kpk = &p->kem_pk;
    } else {
        return DM_ERR_FORMAT;
    }
    if (spk) {
        uint8_t h2[32];
        sha3_256(f, signed_len, h2);
        if (!pqm_sig_decode(&m->sig, sg, sig_len) ||
            !pqm_verify(spk, h2, 32, (const uint8_t *) CTX_M1, sizeof CTX_M1 - 1, &m->sig))
            return DM_ERR_AUTH;
    }
    if (!pqm_kem_pk_decode(&m->peer_eph, eph, eph_len) || m->peer_eph.level != m->level)
        return DM_ERR_FORMAT;

    dm_sess_t *s = &p->pend;
    dm_mset(s, 0, sizeof *s);
    sha3_256(f, len, s->th); /* th1 for now */
    uint8_t th1[32];
    dm_mcpy(th1, s->th, 32);

    uint8_t coins[32], ss_a[32], ss_b[32], nonce[32], rn[32], h[32];
    rnd(m, coins, 32);
    if (!pqm_encaps(&m->peer_eph, coins, &m->ct_a, ss_a)) return DM_ERR_CRYPTO;
    rnd(m, coins, 32);
    if (!pqm_encaps(kpk, coins, &m->ct_b, ss_b)) return DM_ERR_CRYPTO;
    dm_mset(coins, 0, 32);
    rnd(m, nonce, 32);

    dm_w_t w;
    dm_w_init(&w, m->tx, sizeof m->tx);
    hdr(m, &w, DM_T_M2, m->mesh_id);
    dm_w_bytes(&w, nonce, 32);
    put_blob_ct(&w, &m->ct_a);
    put_blob_ct(&w, &m->ct_b);
    if (mode == DM_MODE_PAIR) {
        put_blob_sigpk(&w, &m->sig_pk);
        put_blob_kempk(&w, &m->kem_pk);
        dm_w_bytes(&w, m->self_name, DM_NAME_MAX);
        dm_w_u8(&w, m->self_role);
        if (w.err) return DM_ERR_SIZE;
        sha3_256(m->tx, w.n, h);
        uint8_t mc2[32];
        mac(psk, "zxv-devmesh/v1/mac2", th1, h, mc2);
        dm_w_bytes(&w, mc2, 32);
    }
    if (w.err) return DM_ERR_SIZE;
    uint8_t sm[64];
    dm_mcpy(sm, th1, 32);
    sha3_256(m->tx, w.n, sm + 32);
    rnd(m, rn, 32);
    if (!pqm_sign(&m->sig_sk, sm, 64, (const uint8_t *) CTX_M2, sizeof CTX_M2 - 1, rn, &m->sig))
        return DM_ERR_CRYPTO;
    put_blob_sig(&w, &m->sig);
    if (w.err) return DM_ERR_SIZE;
    sha3_256(m->tx, w.n, h);
    hash4(th1, 32, h, 32, 0, 0, 0, 0, s->th);
    derive(s, mode, ss_a, ss_b, psk, false);
    dm_mset(ss_a, 0, 32);
    dm_mset(ss_b, 0, 32);
    s->mode = mode;
    s->initiator = 0;
    s->state = DM_SESS_M2_SENT;
    s->tx_seq = 1;
    s->rx_seq = 0;
    s->last_rx_ms = now;
    return m->host.send(m->host.ctx, sender, m->tx, w.n) == 0 ? DM_OK : DM_ERR_TRANSPORT;
}

static dm_status_t on_m2(dm_mesh_t *m, const uint8_t *f, uint32_t len, uint64_t now)
{
    const uint8_t *sender = f + 20;
    if (!m->hs_active) return DM_ERR_STATE;
    uint8_t mode = m->hs_mode;
    if (mode == DM_MODE_SESSION || m->join_have_kh) {
        if (!dm_meq(sender, m->hs_peer, DM_ID_BYTES)) return DM_ERR_STATE;
    }
    dm_r_t r;
    dm_r_init(&r, f, len);
    dm_r_take(&r, DM_HDR_BYTES);
    dm_r_take(&r, 32);
    uint32_t a_len, b_len, sp_len = 0, kp_len = 0;
    const uint8_t *ca = get_blob(&r, &a_len), *cb = get_blob(&r, &b_len), *sp = 0, *kp = 0;
    char name[DM_NAME_MAX];
    uint8_t role = 0;
    const uint8_t *mc = 0;
    uint32_t mac_off = 0;
    if (mode == DM_MODE_PAIR) {
        sp = get_blob(&r, &sp_len);
        kp = get_blob(&r, &kp_len);
        dm_r_bytes(&r, name, DM_NAME_MAX);
        role = dm_r_u8(&r);
        mac_off = r.off;
        mc = dm_r_take(&r, 32);
    }
    uint32_t signed_len = r.off;
    uint32_t sig_len;
    const uint8_t *sg = get_blob(&r, &sig_len);
    if (!dm_r_done(&r) || !ca || !cb || !sg) return DM_ERR_FORMAT;

    dm_peer_t *p;
    uint8_t kh[32];
    const pqm_sig_pk_t *spk;
    if (mode == DM_MODE_SESSION) {
        p = dm_peer_find(m, sender);
        if (!p || !p->have_keys) return DM_ERR_UNKNOWN;
        const dm_dev_t *d = dm_roster_find(&m->roster, sender);
        if (!d) return DM_ERR_UNKNOWN;
        if (d->status != DM_DEV_ACTIVE) return DM_ERR_REVOKED;
        spk = &p->sig_pk;
    } else {
        if (!m->join_active) return DM_ERR_STATE;
        if (!pqm_sig_pk_decode(&m->tmp_sig_pk, sp, sp_len)) return DM_ERR_FORMAT;
        if (!pqm_kem_pk_decode(&m->tmp_kem_pk, kp, kp_len)) return DM_ERR_FORMAT;
        if (m->tmp_sig_pk.level != m->level || m->tmp_kem_pk.level != m->level)
            return DM_ERR_FORMAT;
        if (keyhash(m, &m->tmp_sig_pk, &m->tmp_kem_pk, kh) != DM_OK) return DM_ERR_FORMAT;
        if (!dm_meq(kh, sender, DM_ID_BYTES)) return DM_ERR_FORMAT;
        if (m->join_have_kh && !dm_meq(kh, m->join_kh, 32)) return DM_ERR_AUTH;
        if (!name[0] || name[DM_NAME_MAX - 1]) return DM_ERR_FORMAT;
        uint8_t h[32], want[32];
        sha3_256(f, mac_off, h);
        mac(m->join_secret, "zxv-devmesh/v1/mac2", m->hs_th1, h, want);
        if (!dm_meq(want, mc, 32)) return DM_ERR_AUTH;
        spk = &m->tmp_sig_pk;
        p = 0;
    }
    uint8_t sm[64];
    dm_mcpy(sm, m->hs_th1, 32);
    sha3_256(f, signed_len, sm + 32);
    if (!pqm_sig_decode(&m->sig, sg, sig_len) ||
        !pqm_verify(spk, sm, 64, (const uint8_t *) CTX_M2, sizeof CTX_M2 - 1, &m->sig))
        return DM_ERR_AUTH;
    if (!pqm_kem_ct_decode(&m->ct_a, ca, a_len) || !pqm_kem_ct_decode(&m->ct_b, cb, b_len))
        return DM_ERR_FORMAT;
    if (mode == DM_MODE_PAIR) {
        p = peer_alloc(m, sender);
        if (!p) return DM_ERR_FULL;
        dm_mcpy(&p->sig_pk, &m->tmp_sig_pk, sizeof p->sig_pk);
        dm_mcpy(&p->kem_pk, &m->tmp_kem_pk, sizeof p->kem_pk);
        dm_mcpy(p->keyhash, kh, 32);
        name_copy(p->name, name);
        p->paired_role = role;
        p->have_keys = 1;
        dm_mcpy(m->join_peer, sender, DM_ID_BYTES);
        dm_mcpy(m->mesh_id, f + 4, DM_ID_BYTES);
    }
    uint8_t ss_a[32], ss_b[32], h[32];
    if (!pqm_decaps(&m->eph_sk, &m->ct_a, ss_a) || !pqm_decaps(&m->kem_sk, &m->ct_b, ss_b))
        return DM_ERR_CRYPTO;
    pqm_kem_sk_wipe(&m->eph_sk);
    m->hs_active = 0;
    dm_sess_t *s = &p->s;
    dm_mset(s, 0, sizeof *s);
    sha3_256(f, len, h);
    hash4(m->hs_th1, 32, h, 32, 0, 0, 0, 0, s->th);
    derive(s, mode, ss_a, ss_b, mode == DM_MODE_PAIR ? m->join_secret : 0, true);
    dm_mset(ss_a, 0, 32);
    dm_mset(ss_b, 0, 32);
    s->mode = mode;
    s->initiator = 1;
    s->tx_seq = 0;
    s->rx_seq = 0;

    /* M3: key confirmation */
    dm_w_t w;
    dm_w_init(&w, m->tx, sizeof m->tx);
    hdr(m, &w, DM_T_M3, m->mesh_id);
    uint32_t aad = w.n;
    uint8_t *ct = dm_w_reserve(&w, 32);
    uint8_t *tag = dm_w_reserve(&w, 16);
    if (w.err) return DM_ERR_SIZE;
    uint8_t nonce[12];
    nonce_for(0, nonce);
    aead_seal(s->k_tx, nonce, m->tx, aad, s->th, ct, 32, tag);
    s->tx_seq = 1;
    s->state = mode == DM_MODE_PAIR ? DM_SESS_PAIR_SAS : DM_SESS_UP;
    if (m->host.send(m->host.ctx, sender, m->tx, w.n) != 0) return DM_ERR_TRANSPORT;
    if (mode == DM_MODE_PAIR) {
        s->last_rx_ms = now;
        dm_emit(m, DM_EV_SAS, p->id, s->sas, 0, 0);
    } else {
        session_up(m, p, now);
    }
    return DM_OK;
}

static dm_status_t on_m3(dm_mesh_t *m, const uint8_t *f, uint32_t len, uint64_t now)
{
    if (len != DM_HDR_BYTES + 32u + 16u) return DM_ERR_FORMAT;
    dm_peer_t *p = dm_peer_find(m, f + 20);
    if (!p || p->pend.state != DM_SESS_M2_SENT) return DM_ERR_STATE;
    uint8_t pt[32], nonce[12];
    nonce_for(0, nonce);
    if (!aead_open(p->pend.k_rx, nonce, f, DM_HDR_BYTES, f + DM_HDR_BYTES, pt, 32,
                   f + DM_HDR_BYTES + 32))
        return DM_ERR_AUTH;
    if (!dm_meq(pt, p->pend.th, 32)) return DM_ERR_AUTH;
    dm_mcpy(&p->s, &p->pend, sizeof p->s);
    dm_mset(&p->pend, 0, sizeof p->pend);
    p->s.rx_seq = 0;
    p->s.last_rx_ms = now;
    if (p->s.mode == DM_MODE_PAIR) {
        p->s.state = DM_SESS_PAIR_SAS;
        dm_emit(m, DM_EV_SAS, p->id, p->s.sas, 0, 0);
    } else {
        p->s.state = DM_SESS_UP;
        session_up(m, p, now);
    }
    return DM_OK;
}

/* ===== pairing completion ===== */
static dm_status_t inviter_finish(dm_mesh_t *m, dm_peer_t *p, uint64_t now)
{
    dm_mcpy(&m->roster_tmp, &m->roster, sizeof m->roster);
    dm_roster_t *r = &m->roster_tmp;
    if (r->n >= DM_MAX_DEVICES) return DM_ERR_FULL;
    dm_dev_t *d = &r->dev[r->n++];
    dm_mset(d, 0, sizeof *d);
    dm_mcpy(d->id, p->id, DM_ID_BYTES);
    dm_mcpy(d->keyhash, p->keyhash, DM_HASH_BYTES);
    name_copy(d->name, p->name);
    d->role = p->paired_role;
    d->flags = p->paired_flags;
    d->status = DM_DEV_ACTIVE;
    d->since = m->roster.version + 1u;
    m->inv_active = 0;
    dm_mset(m->inv_qr, 0, 32);
    dm_mset(m->inv_code, 0, 32);
    p->s.state = DM_SESS_UP;
    dm_status_t st = admin_commit(m, now); /* broadcasts to every live peer */
    if (st != DM_OK) return st;
    /* the new device learns the keys of every other device, and the other
     * live devices learn its keys */
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        dm_peer_t *o = &m->peers[i];
        if (!o->used || o == p || !o->have_keys) continue;
        const dm_dev_t *od = dm_roster_find(&m->roster, o->id);
        if (!od || od->status != DM_DEV_ACTIVE) continue;
        send_keys_to(m, p, o->id, &o->sig_pk, &o->kem_pk, now);
        if (o->s.state == DM_SESS_UP) send_keys_to(m, o, p->id, &p->sig_pk, &p->kem_pk, now);
    }
    dm_emit(m, DM_EV_PAIRED, p->id, 0, m->roster.version, 0);
    p->s.last_rx_ms = now;
    p->s.last_ping_ms = now;
    dm_emit(m, DM_EV_PEER_UP, p->id, 0, 0, 0);
    dm_remote_on_session_up(m, p, now);
    return DM_OK;
}

static dm_status_t joiner_finish(dm_mesh_t *m, dm_peer_t *p, const uint8_t *enc, uint32_t len,
                                 uint64_t now)
{
    dm_roster_t *r = &m->roster_tmp;
    dm_status_t st = roster_decode(r, enc, len);
    if (st != DM_OK) return st;
    if (!dm_meq(r->signer, p->id, DM_ID_BYTES)) return DM_ERR_AUTH;
    if (!dm_meq(r->mesh_id, m->mesh_id, DM_ID_BYTES)) return DM_ERR_FORMAT;
    const dm_dev_t *sd = dm_roster_find(r, p->id);
    if (!sd || sd->status != DM_DEV_ACTIVE || !(sd->flags & DM_FLAG_ADMIN)) return DM_ERR_PERM;
    if (!dm_meq(sd->keyhash, p->keyhash, 32)) return DM_ERR_AUTH;
    const dm_dev_t *me = dm_roster_find(r, m->self_id);
    if (!me || me->status != DM_DEV_ACTIVE || !dm_meq(me->keyhash, m->self_kh, 32))
        return DM_ERR_STATE;
    if (!roster_verify(m, r, &p->sig_pk)) return DM_ERR_AUTH;
    m->joined = 1;
    m->join_active = 0;
    dm_mset(m->join_secret, 0, 32);
    p->s.state = DM_SESS_UP;
    install_roster(m, p->id, now);
    dm_emit(m, DM_EV_PAIRED, m->self_id, 0, m->roster.version, 0);
    p->s.last_rx_ms = now;
    p->s.last_ping_ms = now;
    dm_emit(m, DM_EV_PEER_UP, p->id, 0, 0, 0);
    dm_remote_on_session_up(m, p, now);
    return DM_OK;
}

dm_status_t dm_pair_confirm(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], bool match,
                            uint64_t now_ms)
{
    if (!m || !peer) return DM_ERR_ARG;
    dm_peer_t *p = dm_peer_find(m, peer);
    if (!p || p->s.state != DM_SESS_PAIR_SAS) return DM_ERR_STATE;
    if (!match) {
        peer_drop(p);
        m->inv_active = 0;
        m->join_active = 0;
        dm_emit(m, DM_EV_PAIR_FAILED, peer, 0, 0, DM_ERR_AUTH);
        return DM_OK;
    }
    p->s.sas_ok = 1;
    if (p->s.initiator) /* joiner: tell the inviter, wait for the roster */
        return dm_send_app(m, p->id, DM_APP_SAS_OK, 0, 0, now_ms);
    if (p->s.peer_sas_ok) return inviter_finish(m, p, now_ms);
    return DM_OK;
}

/* ===== records in ===== */
static void on_keys(dm_mesh_t *m, const uint8_t *d, uint32_t n)
{
    dm_r_t r;
    dm_r_init(&r, d, n);
    const uint8_t *id = dm_r_take(&r, DM_ID_BYTES);
    uint32_t a = dm_r_u32(&r);
    const uint8_t *sp = dm_r_take(&r, a);
    if (r.err || !id || !sp) return;
    uint32_t b = n - r.off;
    const uint8_t *kp = dm_r_take(&r, b);
    if (!kp || dm_meq(id, m->self_id, DM_ID_BYTES)) return;
    const dm_dev_t *dev = dm_roster_find(&m->roster, id);
    if (!dev || dev->status != DM_DEV_ACTIVE) return;
    if (!pqm_sig_pk_decode(&m->tmp_sig_pk, sp, a) || !pqm_kem_pk_decode(&m->tmp_kem_pk, kp, b))
        return;
    uint8_t kh[32];
    if (keyhash(m, &m->tmp_sig_pk, &m->tmp_kem_pk, kh) != DM_OK) return;
    if (!dm_meq(kh, dev->keyhash, 32)) return; /* self-certifying: must match the roster */
    dm_peer_t *p = dm_peer_find(m, id);
    if (p && p->have_keys) return;
    p = peer_alloc(m, id);
    if (!p) return;
    dm_mcpy(&p->sig_pk, &m->tmp_sig_pk, sizeof p->sig_pk);
    dm_mcpy(&p->kem_pk, &m->tmp_kem_pk, sizeof p->kem_pk);
    dm_mcpy(p->keyhash, kh, 32);
    name_copy(p->name, dev->name);
    p->paired_role = dev->role;
    p->paired_flags = dev->flags;
    p->have_keys = 1;
}

static dm_status_t on_rec(dm_mesh_t *m, const uint8_t *f, uint32_t len, uint64_t now)
{
    if (len < DM_HDR_BYTES + 8u + 1u + 16u) return DM_ERR_FORMAT;
    dm_peer_t *p = dm_peer_find(m, f + 20);
    if (!p) return DM_ERR_NOPEER;
    if (p->s.state != DM_SESS_UP && p->s.state != DM_SESS_PAIR_SAS) return DM_ERR_NOPEER;
    if (p->s.state == DM_SESS_UP && m->joined) {
        const dm_dev_t *d = dm_roster_find(&m->roster, p->id);
        if (!d || d->status != DM_DEV_ACTIVE) return DM_ERR_REVOKED;
    }
    dm_r_t r;
    dm_r_init(&r, f, len);
    dm_r_take(&r, DM_HDR_BYTES);
    uint64_t seq = dm_r_u64(&r);
    if (seq <= p->s.rx_seq) return DM_ERR_REPLAY;
    uint32_t clen = len - DM_HDR_BYTES - 8u - 16u;
    if (clen > sizeof m->rx) return DM_ERR_SIZE;
    uint8_t nonce[12];
    nonce_for(seq, nonce);
    if (!aead_open(p->s.k_rx, nonce, f, DM_HDR_BYTES + 8u, f + DM_HDR_BYTES + 8u, m->rx, clen,
                   f + len - 16u))
        return DM_ERR_AUTH;
    p->s.rx_seq = seq;
    p->s.last_rx_ms = now;
    uint8_t app = m->rx[0];
    const uint8_t *d = m->rx + 1;
    uint32_t n = clen - 1u;
    if (p->s.state == DM_SESS_PAIR_SAS) {
        if (app == DM_APP_SAS_OK && !p->s.initiator) {
            p->s.peer_sas_ok = 1;
            if (p->s.sas_ok) return inviter_finish(m, p, now);
            return DM_OK;
        }
        if (app == DM_APP_ROSTER && p->s.initiator && p->s.sas_ok)
            return joiner_finish(m, p, d, n, now);
        return DM_ERR_STATE;
    }
    switch (app) {
    case DM_APP_ROSTER: {
        dm_status_t st = dm_roster_accept(m, d, n, now);
        return st == DM_ERR_REPLAY ? DM_OK : st; /* older or same: ignore */
    }
    case DM_APP_KEYS:
        on_keys(m, d, n);
        return DM_OK;
    case DM_APP_PING:
        return dm_send_app(m, p->id, DM_APP_PONG, 0, 0, now);
    case DM_APP_PONG:
        return DM_OK;
    default:
        dm_remote_on_app(m, p, app, d, n, now);
        return DM_OK;
    }
}

dm_status_t dm_receive(dm_mesh_t *m, const uint8_t *frame, uint32_t len, uint64_t now_ms)
{
    if (!m || !frame) return DM_ERR_ARG;
    if (len < DM_HDR_BYTES || len > DM_FRAME_MAX) return DM_ERR_FORMAT;
    if (frame[0] != 'D' || frame[1] != 'M' || frame[2] != DM_VERSION) return DM_ERR_FORMAT;
    if (is_zero(frame + 20, DM_ID_BYTES)) return DM_ERR_FORMAT;
    switch (frame[3]) {
    case DM_T_M1:
        return on_m1(m, frame, len, now_ms);
    case DM_T_M2:
        return on_m2(m, frame, len, now_ms);
    case DM_T_M3:
        return on_m3(m, frame, len, now_ms);
    case DM_T_REC:
        return on_rec(m, frame, len, now_ms);
    default:
        return DM_ERR_FORMAT;
    }
}

void dm_tick(dm_mesh_t *m, uint64_t now_ms)
{
    if (!m) return;
    if (m->hs_active && now_ms - m->hs_started > 2u * DM_LIVE_MS) {
        pqm_kem_sk_wipe(&m->eph_sk);
        m->hs_active = 0;
    }
    if (m->inv_active && now_ms > m->inv_expires) m->inv_active = 0;
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        dm_peer_t *p = &m->peers[i];
        if (!p->used || p->s.state != DM_SESS_UP) continue;
        if (now_ms - p->s.last_rx_ms > 2u * DM_LIVE_MS) {
            dm_mset(&p->s, 0, sizeof p->s);
            dm_emit(m, DM_EV_PEER_DOWN, p->id, 0, 0, 0);
            continue;
        }
        if (now_ms - p->s.last_ping_ms >= DM_PING_MS) {
            p->s.last_ping_ms = now_ms;
            (void) dm_send_app(m, p->id, DM_APP_PING, 0, 0, now_ms);
        }
    }
    dm_remote_tick(m, now_ms);
}

bool dm_peer_up(const dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint64_t now_ms)
{
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        const dm_peer_t *p = &m->peers[i];
        if (p->used && dm_meq(p->id, peer, DM_ID_BYTES))
            return p->s.state == DM_SESS_UP && now_ms - p->s.last_rx_ms <= DM_LIVE_MS;
    }
    return false;
}

dm_status_t dm_session_export_key(const dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES],
                                  const char *label, uint8_t out[32])
{
    if (!m || !peer || !label || !out) return DM_ERR_ARG;
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        const dm_peer_t *p = &m->peers[i];
        if (!p->used || !dm_meq(p->id, peer, DM_ID_BYTES)) continue;
        if (p->s.state != DM_SESS_UP) return DM_ERR_NOPEER;
        static const char L[] = "zxv-devmesh/v1/export";
        uint32_t ll = cstrlen(label, 64);
        hash4(L, sizeof L - 1, p->s.k_ext, 32, label, ll, 0, 0, out);
        return DM_OK;
    }
    return DM_ERR_NOPEER;
}
