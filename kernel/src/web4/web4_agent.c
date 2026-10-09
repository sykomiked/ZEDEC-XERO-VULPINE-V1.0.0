/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_agent.c — agent identity, manifests, consent, envelopes, the
 * receiving runtime and carriage (see web4_agent.h for the rules A1..K2). */
#include "web4_agent.h"
#include "web4_web2.h"

/* ===== ML-DSA-65 wrappers ===== */
void w4_sign(const uint8_t sk[W4_SK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
             const uint8_t rnd[32], uint8_t sig[W4_SIG_LEN])
{
    pq_mldsa65_sign(sk, msg, len, (const uint8_t *) ctx, (uint32_t) w4_strnlen(ctx, 255), rnd, sig);
}

bool w4_verify(const uint8_t pk[W4_PK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
               const uint8_t sig[W4_SIG_LEN])
{
    return pq_mldsa65_verify(pk, msg, len, (const uint8_t *) ctx, (uint32_t) w4_strnlen(ctx, 255),
                             sig);
}

void w4_key_id(const uint8_t pk[W4_PK_LEN], uint8_t id[W4_ID_LEN])
{
    w4_sha3_256(pk, W4_PK_LEN, id);
}

/* ===== Identity ===== */
static bool name_ok(const char *s, uint32_t max)
{
    uint32_t n = (uint32_t) w4_strnlen(s, max);
    if (n == 0 || n >= max) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || w4_is_digit(c) || c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

static uint32_t bind_msg(const w4_card_t *c, uint8_t *out, uint32_t cap)
{
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_strn(&w, "W4BD", 4);
    w4_w_bytes(&w, c->agent_id, W4_ID_LEN);
    w4_w_bytes(&w, c->peer_id, W4_ID_LEN);
    uint8_t nl = (uint8_t) w4_strnlen(c->name, W4_NAME_MAX);
    w4_w_byte(&w, nl);
    w4_w_bytes(&w, c->name, nl);
    uint8_t t[8];
    w4_le64_put(t, c->bound_ms);
    w4_w_bytes(&w, t, 8);
    return w.err ? 0 : w.len;
}

int w4_agent_create(w4_agent_t *a, const uint8_t seed[32], const char *name, const uint8_t *node_pk,
                    const uint8_t *node_sk, uint64_t now_ms, const uint8_t *rnd)
{
    if (!a || !seed || !name_ok(name, W4_NAME_MAX) || (!node_pk) != (!node_sk)) return W4_ERR_ARG;
    w4_memset(a, 0, sizeof *a);
    w4_card_t *c = &a->card;
    pq_mldsa65_keygen(seed, c->pk, a->sk);
    w4_key_id(c->pk, c->agent_id);
    w4_strlcpy(c->name, name, sizeof c->name);
    c->bound_ms = now_ms;
    if (node_pk) {
        w4_memcpy(c->node_pk, node_pk, W4_PK_LEN);
        w4_key_id(node_pk, c->peer_id);
        c->self_bound = false;
    } else {
        w4_memcpy(c->peer_id, c->agent_id, W4_ID_LEN);
        c->self_bound = true;
    }
    uint8_t m[128];
    uint32_t ml = bind_msg(c, m, sizeof m);
    w4_sign(node_sk ? node_sk : a->sk, W4_CTX_BIND, m, ml, rnd, c->bind_sig);
    a->next_seq = 1;
    return w4_card_verify(c);
}

int w4_card_verify(const w4_card_t *c)
{
    if (!c || !name_ok(c->name, W4_NAME_MAX)) return W4_ERR_ARG;
    uint8_t id[W4_ID_LEN];
    w4_key_id(c->pk, id);
    if (!w4_memeq(id, c->agent_id, W4_ID_LEN)) return W4_ERR_BINDING;
    const uint8_t *signer = c->pk;
    if (c->self_bound) {
        if (!w4_memeq(c->peer_id, c->agent_id, W4_ID_LEN)) return W4_ERR_BINDING;
    } else {
        w4_key_id(c->node_pk, id);
        if (!w4_memeq(id, c->peer_id, W4_ID_LEN)) return W4_ERR_BINDING;
        signer = c->node_pk;
    }
    uint8_t m[128];
    uint32_t ml = bind_msg(c, m, sizeof m);
    return w4_verify(signer, W4_CTX_BIND, m, ml, c->bind_sig) ? W4_OK : W4_ERR_SIG;
}

void w4_dir_init(w4_dir_t *d, w4_card_t *storage, uint32_t cap)
{
    d->e = storage;
    d->cap = cap;
    d->n = 0;
}

const w4_card_t *w4_dir_find(const w4_dir_t *d, const uint8_t agent_id[W4_ID_LEN])
{
    for (uint32_t i = 0; i < d->n; i++)
        if (w4_memeq(d->e[i].agent_id, agent_id, W4_ID_LEN)) return &d->e[i];
    return NULL;
}

int w4_dir_add(w4_dir_t *d, const w4_card_t *c)
{
    int r = w4_card_verify(c);
    if (r) return r;
    for (uint32_t i = 0; i < d->n; i++)
        if (w4_memeq(d->e[i].agent_id, c->agent_id, W4_ID_LEN)) {
            if (c->bound_ms < d->e[i].bound_ms) return W4_ERR_STALE; /* no rollback */
            d->e[i] = *c;
            return W4_OK;
        }
    if (d->n >= d->cap) return W4_ERR_SPACE;
    d->e[d->n++] = *c;
    return W4_OK;
}

/* ===== Manifest ===== */
int w4_manifest_add_tool(w4_manifest_t *m, const char *name, uint64_t price_vfv,
                         uint32_t rate_per_min, uint32_t burst, uint32_t flags, uint32_t max_input)
{
    if (!m || !name_ok(name, W4_TOOL_NAME_MAX) || rate_per_min == 0 || burst == 0 ||
        max_input > W4_MSG_MAX_PAYLOAD || (flags & ~(W4_TOOL_MONEY | W4_TOOL_PII | W4_TOOL_STREAM)))
        return W4_ERR_ARG;
    if (m->ntools >= W4_MAX_TOOLS) return W4_ERR_SPACE;
    if (w4_manifest_find(m, name) >= 0) return W4_ERR_ARG;
    w4_tool_t *t = &m->tools[m->ntools++];
    w4_memset(t, 0, sizeof *t);
    w4_strlcpy(t->name, name, sizeof t->name);
    t->price_vfv = price_vfv;
    t->rate_per_min = rate_per_min;
    t->burst = burst;
    t->flags = flags;
    t->max_input = max_input;
    return W4_OK;
}

int32_t w4_manifest_find(const w4_manifest_t *m, const char *tool)
{
    for (uint32_t i = 0; i < m->ntools; i++)
        if (w4_streq(m->tools[i].name, tool)) return (int32_t) i;
    return W4_ERR_NOTFOUND;
}

static void put64(w4_w *w, uint64_t v)
{
    uint8_t b[8];
    w4_le64_put(b, v);
    w4_w_bytes(w, b, 8);
}

static void put32(w4_w *w, uint32_t v)
{
    uint8_t b[4];
    w4_le32_put(b, v);
    w4_w_bytes(w, b, 4);
}

static int32_t manifest_body(const w4_manifest_t *m, uint8_t *out, uint32_t cap)
{
    if (m->ntools > W4_MAX_TOOLS) return W4_ERR_ARG;
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_strn(&w, "W4MF", 4);
    w4_w_byte(&w, 1);
    w4_w_bytes(&w, m->agent_id, W4_ID_LEN);
    w4_w_bytes(&w, m->peer_id, W4_ID_LEN);
    put32(&w, m->version);
    put64(&w, m->issued_ms);
    put64(&w, m->expires_ms);
    w4_w_byte(&w, (uint8_t) m->ntools);
    for (uint32_t i = 0; i < m->ntools; i++) {
        const w4_tool_t *t = &m->tools[i];
        uint8_t nl = (uint8_t) w4_strnlen(t->name, W4_TOOL_NAME_MAX);
        w4_w_byte(&w, nl);
        w4_w_bytes(&w, t->name, nl);
        put64(&w, t->price_vfv);
        put32(&w, t->rate_per_min);
        put32(&w, t->burst);
        put32(&w, t->flags);
        put32(&w, t->max_input);
    }
    return w4_w_done(&w);
}

int w4_manifest_sign(const w4_agent_t *a, w4_manifest_t *m, const uint8_t *rnd)
{
    if (!a || !m || m->expires_ms <= m->issued_ms) return W4_ERR_ARG;
    w4_memcpy(m->agent_id, a->card.agent_id, W4_ID_LEN);
    w4_memcpy(m->peer_id, a->card.peer_id, W4_ID_LEN);
    uint8_t body[W4_MANIFEST_MAX];
    int32_t n = manifest_body(m, body, sizeof body);
    if (n < 0) return n;
    w4_sign(a->sk, W4_CTX_MANIFEST, body, (uint32_t) n, rnd, m->sig);
    return W4_OK;
}

int w4_manifest_verify(const w4_manifest_t *m, const uint8_t pk[W4_PK_LEN], uint64_t now_ms)
{
    uint8_t id[W4_ID_LEN];
    w4_key_id(pk, id);
    if (!w4_memeq(id, m->agent_id, W4_ID_LEN)) return W4_ERR_BINDING;
    uint8_t body[W4_MANIFEST_MAX];
    int32_t n = manifest_body(m, body, sizeof body);
    if (n < 0) return n;
    if (!w4_verify(pk, W4_CTX_MANIFEST, body, (uint32_t) n, m->sig)) return W4_ERR_SIG;
    if (now_ms < m->issued_ms || now_ms >= m->expires_ms) return W4_ERR_EXPIRED;
    return W4_OK;
}

int32_t w4_manifest_encode(const w4_manifest_t *m, uint8_t *out, uint32_t cap)
{
    int32_t n = manifest_body(m, out, cap);
    if (n < 0) return n;
    if (cap - (uint32_t) n < W4_SIG_LEN) return W4_ERR_SPACE;
    w4_memcpy(out + n, m->sig, W4_SIG_LEN);
    return n + (int32_t) W4_SIG_LEN;
}

/* Bounded reader over canonical little-endian records. */
typedef struct {
    const uint8_t *p;
    uint32_t len, off;
    bool err;
} rd_t;

static const uint8_t *rd_take(rd_t *r, uint32_t n)
{
    if (r->err || n > r->len - r->off) {
        r->err = true;
        return NULL;
    }
    const uint8_t *p = r->p + r->off;
    r->off += n;
    return p;
}

static void rd_bytes(rd_t *r, void *dst, uint32_t n)
{
    const uint8_t *p = rd_take(r, n);
    if (p) w4_memcpy(dst, p, n);
}

static uint8_t rd_u8(rd_t *r)
{
    const uint8_t *p = rd_take(r, 1);
    return p ? p[0] : 0;
}

static uint32_t rd_u32(rd_t *r)
{
    const uint8_t *p = rd_take(r, 4);
    return p ? w4_le32_get(p) : 0;
}

static uint64_t rd_u64(rd_t *r)
{
    const uint8_t *p = rd_take(r, 8);
    return p ? w4_le64_get(p) : 0;
}

static bool rd_name(rd_t *r, char *out, uint32_t cap)
{
    uint8_t n = rd_u8(r);
    if (r->err || n == 0 || n >= cap) return false;
    rd_bytes(r, out, n);
    out[n] = 0;
    return !r->err && name_ok(out, cap);
}

int w4_manifest_decode(const uint8_t *buf, uint32_t len, w4_manifest_t *m)
{
    if (!buf || !m) return W4_ERR_ARG;
    w4_memset(m, 0, sizeof *m);
    rd_t r = {buf, len, 0, false};
    const uint8_t *mg = rd_take(&r, 4);
    if (!mg || !w4_memeq(mg, "W4MF", 4) || rd_u8(&r) != 1) return W4_ERR_PARSE;
    rd_bytes(&r, m->agent_id, W4_ID_LEN);
    rd_bytes(&r, m->peer_id, W4_ID_LEN);
    m->version = rd_u32(&r);
    m->issued_ms = rd_u64(&r);
    m->expires_ms = rd_u64(&r);
    uint32_t nt = rd_u8(&r);
    if (r.err || nt > W4_MAX_TOOLS) return W4_ERR_PARSE;
    for (uint32_t i = 0; i < nt; i++) {
        char name[W4_TOOL_NAME_MAX];
        if (!rd_name(&r, name, sizeof name)) return W4_ERR_PARSE;
        uint64_t price = rd_u64(&r);
        uint32_t rate = rd_u32(&r), burst = rd_u32(&r), flags = rd_u32(&r), mi = rd_u32(&r);
        if (r.err || w4_manifest_add_tool(m, name, price, rate, burst, flags, mi))
            return W4_ERR_PARSE;
    }
    rd_bytes(&r, m->sig, W4_SIG_LEN);
    if (r.err || r.off != len) return W4_ERR_PARSE;
    return W4_OK;
}

int32_t w4_manifest_json(const w4_manifest_t *m, char *out, uint32_t cap)
{
    char hex[2 * W4_ID_LEN + 1];
    w4_jw j;
    w4_jw_init(&j, out, cap);
    w4_jw_obj(&j);
    w4_jw_key(&j, "type");
    w4_jw_str(&j, "web4.agent.manifest");
    w4_jw_key(&j, "agent_id");
    w4_hex_encode(m->agent_id, W4_ID_LEN, hex, sizeof hex);
    w4_jw_str(&j, hex);
    w4_jw_key(&j, "peer_id");
    w4_hex_encode(m->peer_id, W4_ID_LEN, hex, sizeof hex);
    w4_jw_str(&j, hex);
    w4_jw_key(&j, "version");
    w4_jw_u64(&j, m->version);
    w4_jw_key(&j, "issued_ms");
    w4_jw_u64(&j, m->issued_ms);
    w4_jw_key(&j, "expires_ms");
    w4_jw_u64(&j, m->expires_ms);
    w4_jw_key(&j, "signature_alg");
    w4_jw_str(&j, "ML-DSA-65");
    w4_jw_key(&j, "tools");
    w4_jw_arr(&j);
    for (uint32_t i = 0; i < m->ntools; i++) {
        const w4_tool_t *t = &m->tools[i];
        w4_jw_obj(&j);
        w4_jw_key(&j, "name");
        w4_jw_str(&j, t->name);
        w4_jw_key(&j, "price_vfv_minor");
        w4_jw_u64(&j, t->price_vfv);
        w4_jw_key(&j, "vfv_minor_digits");
        w4_jw_u64(&j, W4_VFV_MINOR);
        w4_jw_key(&j, "rate_per_min");
        w4_jw_u64(&j, t->rate_per_min);
        w4_jw_key(&j, "burst");
        w4_jw_u64(&j, t->burst);
        w4_jw_key(&j, "max_input");
        w4_jw_u64(&j, t->max_input);
        w4_jw_key(&j, "moves_money");
        w4_jw_bool(&j, (t->flags & W4_TOOL_MONEY) != 0 || t->price_vfv > 0);
        w4_jw_key(&j, "personal_data");
        w4_jw_bool(&j, (t->flags & W4_TOOL_PII) != 0);
        w4_jw_key(&j, "stream");
        w4_jw_bool(&j, (t->flags & W4_TOOL_STREAM) != 0);
        w4_jw_obj_end(&j);
    }
    w4_jw_arr_end(&j);
    w4_jw_obj_end(&j);
    return w4_jw_finish(&j);
}

/* ===== Consent ===== */
static uint32_t consent_body(const w4_consent_t *t, uint8_t out[160])
{
    w4_w w;
    w4_w_init(&w, out, 160);
    w4_w_strn(&w, "W4CS", 4);
    w4_w_bytes(&w, t->human_id, W4_ID_LEN);
    w4_w_bytes(&w, t->agent_id, W4_ID_LEN);
    w4_w_bytes(&w, t->action, W4_HASH_LEN);
    put32(&w, t->scope);
    put64(&w, t->max_vfv);
    put64(&w, t->issued_ms);
    put64(&w, t->expires_ms);
    w4_w_bytes(&w, t->nonce, 16);
    return w.len;
}

int w4_consent_issue(const uint8_t human_pk[W4_PK_LEN], const uint8_t human_sk[W4_SK_LEN],
                     const uint8_t agent_id[W4_ID_LEN], const uint8_t action[W4_HASH_LEN],
                     uint32_t scope, uint64_t max_vfv, uint64_t issued_ms, uint64_t expires_ms,
                     const uint8_t nonce[16], const uint8_t *rnd, w4_consent_t *out)
{
    if (!human_pk || !human_sk || !agent_id || !action || !nonce || !out ||
        expires_ms <= issued_ms || scope == 0 || (scope & ~(W4_CONSENT_MONEY | W4_CONSENT_PII)))
        return W4_ERR_ARG;
    w4_memset(out, 0, sizeof *out);
    w4_key_id(human_pk, out->human_id);
    w4_memcpy(out->agent_id, agent_id, W4_ID_LEN);
    w4_memcpy(out->action, action, W4_HASH_LEN);
    out->scope = scope;
    out->max_vfv = max_vfv;
    out->issued_ms = issued_ms;
    out->expires_ms = expires_ms;
    w4_memcpy(out->nonce, nonce, 16);
    uint8_t b[160];
    uint32_t n = consent_body(out, b);
    w4_sign(human_sk, W4_CTX_CONSENT, b, n, rnd, out->sig);
    return W4_OK;
}

void w4_consent_log_init(w4_consent_log_t *l, w4_consent_used_t *storage, uint32_t cap)
{
    l->e = storage;
    l->cap = cap;
    l->n = 0;
}

void w4_consent_log_prune(w4_consent_log_t *l, uint64_t now_ms)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->e[i].expires_ms > now_ms) l->e[o++] = l->e[i];
    l->n = o;
}

int w4_consent_check(const w4_consent_t *t, const uint8_t human_pk[W4_PK_LEN],
                     const uint8_t agent_id[W4_ID_LEN], const uint8_t action[W4_HASH_LEN],
                     uint32_t need, uint64_t amount, uint64_t now_ms, w4_consent_log_t *log)
{
    if (!t || !human_pk || !agent_id || !action || !log || need == 0) return W4_ERR_CONSENT;
    uint8_t id[W4_ID_LEN];
    w4_key_id(human_pk, id);
    if (!w4_memeq(id, t->human_id, W4_ID_LEN)) return W4_ERR_CONSENT;
    if (!w4_memeq(t->agent_id, agent_id, W4_ID_LEN)) return W4_ERR_CONSENT;
    if (!w4_ct_eq(t->action, action, W4_HASH_LEN)) return W4_ERR_CONSENT;
    if ((t->scope & need) != need) return W4_ERR_CONSENT;
    if ((need & W4_CONSENT_MONEY) && amount > t->max_vfv) return W4_ERR_CONSENT;
    if (now_ms < t->issued_ms || now_ms >= t->expires_ms) return W4_ERR_EXPIRED;
    uint8_t b[160];
    uint32_t n = consent_body(t, b);
    if (!w4_verify(human_pk, W4_CTX_CONSENT, b, n, t->sig)) return W4_ERR_SIG;
    uint8_t key[W4_HASH_LEN];
    uint8_t kb[W4_ID_LEN + 16];
    w4_memcpy(kb, t->human_id, W4_ID_LEN);
    w4_memcpy(kb + W4_ID_LEN, t->nonce, 16);
    w4_sha3_256(kb, sizeof kb, key);
    for (uint32_t i = 0; i < log->n; i++)
        if (w4_memeq(log->e[i].key, key, W4_HASH_LEN)) return W4_ERR_REPLAY;
    if (log->n >= log->cap) {
        w4_consent_log_prune(log, now_ms);
        if (log->n >= log->cap) return W4_ERR_SPACE; /* fail closed */
    }
    w4_memcpy(log->e[log->n].key, key, W4_HASH_LEN);
    log->e[log->n].expires_ms = t->expires_ms;
    log->n++;
    return W4_OK;
}

/* ===== Envelopes ===== */
void w4_action_digest(const char *tool, const uint8_t from[W4_ID_LEN], const uint8_t to[W4_ID_LEN],
                      uint64_t req_id, const uint8_t *payload, uint32_t len,
                      uint8_t out[W4_HASH_LEN])
{
    w4_hb h;
    w4_hb_init(&h);
    w4_hb_str(&h, "web4/v1/action", 32);
    w4_hb_str(&h, tool, W4_TOOL_NAME_MAX);
    w4_hb_put(&h, from, W4_ID_LEN);
    w4_hb_put(&h, to, W4_ID_LEN);
    w4_hb_u64(&h, req_id);
    w4_hb_u32(&h, len);
    w4_hb_put(&h, payload, len);
    w4_hb_sha3(&h, out);
}

int w4_env_make(w4_env_t *e, uint8_t kind, const uint8_t from[W4_ID_LEN],
                const uint8_t to[W4_ID_LEN], uint64_t req_id, uint32_t chunk, const char *tool,
                const uint8_t *payload, uint32_t len)
{
    if (!e || kind < W4_MSG_REQUEST || kind > W4_MSG_ERROR || !from || !to ||
        len > W4_MSG_MAX_PAYLOAD || (len && !payload) || !tool || !name_ok(tool, W4_TOOL_NAME_MAX))
        return W4_ERR_ARG;
    w4_memset(e, 0, sizeof *e);
    e->kind = kind;
    w4_memcpy(e->from, from, W4_ID_LEN);
    w4_memcpy(e->to, to, W4_ID_LEN);
    e->req_id = req_id;
    e->chunk = chunk;
    w4_strlcpy(e->tool, tool, sizeof e->tool);
    e->payload_len = len;
    if (len) w4_memcpy(e->payload, payload, len);
    return W4_OK;
}

int w4_env_reply(w4_env_t *r, const w4_env_t *req, uint8_t kind, uint32_t chunk,
                 const uint8_t *payload, uint32_t len)
{
    if (!req || req->kind != W4_MSG_REQUEST) return W4_ERR_ARG;
    return w4_env_make(r, kind, req->to, req->from, req->req_id, chunk, req->tool, payload, len);
}

static int32_t env_body(const w4_env_t *e, uint8_t *out, uint32_t cap)
{
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_strn(&w, "W4EV", 4);
    w4_w_byte(&w, 1);
    w4_w_byte(&w, e->kind);
    w4_w_byte(&w, e->flags);
    w4_w_bytes(&w, e->from, W4_ID_LEN);
    w4_w_bytes(&w, e->to, W4_ID_LEN);
    put64(&w, e->seq);
    put64(&w, e->ts_ms);
    put64(&w, e->req_id);
    put32(&w, e->chunk);
    uint8_t tl = (uint8_t) w4_strnlen(e->tool, W4_TOOL_NAME_MAX);
    w4_w_byte(&w, tl);
    w4_w_bytes(&w, e->tool, tl);
    put32(&w, e->payload_len);
    w4_w_bytes(&w, e->payload, e->payload_len);
    if (e->flags & W4_MF_CONSENT) {
        uint8_t cb[160];
        uint32_t cn = consent_body(&e->consent, cb);
        w4_w_bytes(&w, cb, cn);
        w4_w_bytes(&w, e->consent.sig, W4_SIG_LEN);
    }
    return w4_w_done(&w);
}

int32_t w4_env_seal(w4_agent_t *a, w4_env_t *e, uint64_t now_ms, const uint8_t *rnd, uint8_t *out,
                    uint32_t cap)
{
    if (!a || !e || !out) return W4_ERR_ARG;
    if (!w4_memeq(e->from, a->card.agent_id, W4_ID_LEN)) return W4_ERR_ARG;
    if (e->flags & ~(W4_MF_FINAL | W4_MF_CONSENT)) return W4_ERR_ARG;
    e->seq = a->next_seq++;
    e->ts_ms = now_ms;
    int32_t n = env_body(e, out, cap);
    if (n < 0) return n;
    if (cap - (uint32_t) n < W4_SIG_LEN) return W4_ERR_SPACE;
    w4_sign(a->sk, W4_CTX_ENV, out, (uint32_t) n, rnd, e->sig);
    w4_memcpy(out + n, e->sig, W4_SIG_LEN);
    return n + (int32_t) W4_SIG_LEN;
}

int w4_env_decode(const uint8_t *buf, uint32_t len, w4_env_t *e, uint32_t *signed_len)
{
    if (!buf || !e) return W4_ERR_ARG;
    w4_memset(e, 0, sizeof *e);
    if (len > W4_ENV_MAX) return W4_ERR_PARSE;
    rd_t r = {buf, len, 0, false};
    const uint8_t *mg = rd_take(&r, 4);
    if (!mg || !w4_memeq(mg, "W4EV", 4) || rd_u8(&r) != 1) return W4_ERR_PARSE;
    e->kind = rd_u8(&r);
    e->flags = rd_u8(&r);
    if (e->kind < W4_MSG_REQUEST || e->kind > W4_MSG_ERROR) return W4_ERR_PARSE;
    if (e->flags & ~(W4_MF_FINAL | W4_MF_CONSENT)) return W4_ERR_PARSE;
    rd_bytes(&r, e->from, W4_ID_LEN);
    rd_bytes(&r, e->to, W4_ID_LEN);
    e->seq = rd_u64(&r);
    e->ts_ms = rd_u64(&r);
    e->req_id = rd_u64(&r);
    e->chunk = rd_u32(&r);
    if (!rd_name(&r, e->tool, sizeof e->tool)) return W4_ERR_PARSE;
    e->payload_len = rd_u32(&r);
    if (r.err || e->payload_len > W4_MSG_MAX_PAYLOAD) return W4_ERR_PARSE;
    rd_bytes(&r, e->payload, e->payload_len);
    if (e->flags & W4_MF_CONSENT) {
        const uint8_t *cm = rd_take(&r, 4);
        if (!cm || !w4_memeq(cm, "W4CS", 4)) return W4_ERR_PARSE;
        rd_bytes(&r, e->consent.human_id, W4_ID_LEN);
        rd_bytes(&r, e->consent.agent_id, W4_ID_LEN);
        rd_bytes(&r, e->consent.action, W4_HASH_LEN);
        e->consent.scope = rd_u32(&r);
        e->consent.max_vfv = rd_u64(&r);
        e->consent.issued_ms = rd_u64(&r);
        e->consent.expires_ms = rd_u64(&r);
        rd_bytes(&r, e->consent.nonce, 16);
        rd_bytes(&r, e->consent.sig, W4_SIG_LEN);
    }
    uint32_t sl = r.off;
    rd_bytes(&r, e->sig, W4_SIG_LEN);
    if (r.err || r.off != len) return W4_ERR_PARSE;
    if (signed_len) *signed_len = sl;
    return W4_OK;
}

/* ===== Receiving runtime ===== */
void w4_rt_init(w4_rt_t *rt, const w4_agent_t *self, const w4_manifest_t *manifest,
                const w4_dir_t *dir, w4_replay_ent_t *replay, uint32_t replay_cap,
                w4_rate_ent_t *rate, uint32_t rate_cap, w4_consent_log_t *log,
                w4_human_pk_fn human_pk, void *human_ctx, uint64_t window_ms)
{
    w4_memset(rt, 0, sizeof *rt);
    rt->self = self;
    rt->manifest = manifest;
    rt->dir = dir;
    rt->replay = replay;
    rt->replay_cap = replay_cap;
    rt->rate = rate;
    rt->rate_cap = rate_cap;
    rt->consent_log = log;
    rt->human_pk = human_pk;
    rt->human_ctx = human_ctx;
    rt->window_ms = window_ms;
    for (uint32_t i = 0; i < replay_cap; i++) replay[i].used = false;
    for (uint32_t i = 0; i < rate_cap; i++) rate[i].used = false;
}

static w4_replay_ent_t *replay_find(const w4_rt_t *rt, const uint8_t peer[W4_ID_LEN])
{
    for (uint32_t i = 0; i < rt->replay_cap; i++)
        if (rt->replay[i].used && w4_memeq(rt->replay[i].peer, peer, W4_ID_LEN))
            return &rt->replay[i];
    return NULL;
}

static int replay_check(const w4_rt_t *rt, const uint8_t peer[W4_ID_LEN], uint64_t seq, uint64_t ts,
                        uint64_t now)
{
    if (ts + rt->window_ms < now || ts > now + rt->window_ms) return W4_ERR_STALE;
    if (seq == 0) return W4_ERR_REPLAY;
    const w4_replay_ent_t *e = replay_find(rt, peer);
    if (!e) return ts > rt->evict_floor_ms ? W4_OK : W4_ERR_STALE;
    if (seq > e->hi) return W4_OK;
    uint64_t d = e->hi - seq;
    if (d == 0 || d > 64) return W4_ERR_REPLAY;
    if ((e->bitmap >> (d - 1)) & 1u) return W4_ERR_REPLAY;
    return W4_OK;
}

static void replay_commit(w4_rt_t *rt, const uint8_t peer[W4_ID_LEN], uint64_t seq, uint64_t now)
{
    w4_replay_ent_t *e = replay_find(rt, peer);
    rt->tick++;
    if (!e) {
        for (uint32_t i = 0; i < rt->replay_cap && !e; i++)
            if (!rt->replay[i].used) e = &rt->replay[i];
        if (!e) { /* evict the least recently used; record the floor (R3 rule) */
            e = &rt->replay[0];
            for (uint32_t i = 1; i < rt->replay_cap; i++)
                if (rt->replay[i].last_used < e->last_used) e = &rt->replay[i];
            rt->evict_floor_ms = now;
        }
        w4_memcpy(e->peer, peer, W4_ID_LEN);
        e->used = true;
        e->hi = seq;
        e->bitmap = 0;
        e->last_used = rt->tick;
        return;
    }
    e->last_used = rt->tick;
    if (seq > e->hi) {
        uint64_t sh = seq - e->hi;
        if (sh > 64)
            e->bitmap = 0;
        else if (sh == 64)
            e->bitmap = (uint64_t) 1 << 63;
        else
            e->bitmap = (e->bitmap << sh) | ((uint64_t) 1 << (sh - 1));
        e->hi = seq;
    } else {
        e->bitmap |= (uint64_t) 1 << (e->hi - seq - 1);
    }
}

static int rate_take(w4_rt_t *rt, const uint8_t peer[W4_ID_LEN], uint32_t tool, const w4_tool_t *t,
                     uint64_t now)
{
    w4_rate_ent_t *e = NULL, *free_e = NULL, *old = NULL;
    for (uint32_t i = 0; i < rt->rate_cap; i++) {
        w4_rate_ent_t *x = &rt->rate[i];
        if (!x->used) {
            if (!free_e) free_e = x;
            continue;
        }
        if (x->tool == tool && w4_memeq(x->peer, peer, W4_ID_LEN)) {
            e = x;
            break;
        }
        if (!old || x->last_ms < old->last_ms) old = x;
    }
    uint64_t cap = (uint64_t) t->burst * 1000u;
    if (!e) {
        e = free_e;
        /* Evicting a recently used bucket would hand its owner a full bucket:
         * only buckets idle long enough to have refilled anyway may go. */
        if (!e && old && now >= old->last_ms + 60000u) e = old;
        if (!e) return W4_ERR_RATE; /* fail closed */
        w4_memcpy(e->peer, peer, W4_ID_LEN);
        e->tool = tool;
        e->used = true;
        e->milli_tokens = cap;
        e->last_ms = now;
    }
    if (now > e->last_ms) {
        uint64_t el = now - e->last_ms;
        if (el > 86400000u) el = 86400000u;
        uint64_t add = w4_udiv64(el * t->rate_per_min, 60u, NULL); /* milli-tokens */
        e->milli_tokens = (e->milli_tokens + add > cap) ? cap : e->milli_tokens + add;
        e->last_ms = now;
    }
    if (e->milli_tokens < 1000u) return W4_ERR_RATE;
    e->milli_tokens -= 1000u;
    return W4_OK;
}

int w4_rt_receive(w4_rt_t *rt, const uint8_t *buf, uint32_t len, uint64_t now_ms, w4_env_t *out)
{
    if (!rt || !buf || !out) return W4_ERR_ARG;
    uint32_t sl = 0;
    int r = w4_env_decode(buf, len, out, &sl);
    if (r) return r;
    if (!w4_memeq(out->to, rt->self->card.agent_id, W4_ID_LEN)) return W4_ERR_NOTFOUND;
    const w4_card_t *peer = w4_dir_find(rt->dir, out->from);
    if (!peer) return W4_ERR_NOTFOUND;
    r = replay_check(rt, out->from, out->seq, out->ts_ms, now_ms);
    if (r) return r;
    if (!w4_verify(peer->pk, W4_CTX_ENV, buf, sl, out->sig)) return W4_ERR_SIG;
    replay_commit(rt, out->from, out->seq, now_ms);
    if (out->kind != W4_MSG_REQUEST) return W4_OK;

    if (!rt->manifest) return W4_ERR_NOTFOUND;
    int32_t ti = w4_manifest_find(rt->manifest, out->tool);
    if (ti < 0) return W4_ERR_NOTFOUND;
    const w4_tool_t *t = &rt->manifest->tools[ti];
    if (out->payload_len > t->max_input) return W4_ERR_RANGE;
    r = rate_take(rt, out->from, (uint32_t) ti, t, now_ms);
    if (r) return r;
    uint32_t need = 0;
    if ((t->flags & W4_TOOL_MONEY) || t->price_vfv > 0) need |= W4_CONSENT_MONEY;
    if (t->flags & W4_TOOL_PII) need |= W4_CONSENT_PII;
    if (need) {
        if (!(out->flags & W4_MF_CONSENT) || !rt->human_pk || !rt->consent_log)
            return W4_ERR_CONSENT;
        const uint8_t *hpk = rt->human_pk(rt->human_ctx, out->consent.human_id);
        if (!hpk) return W4_ERR_CONSENT;
        uint8_t act[W4_HASH_LEN];
        w4_action_digest(out->tool, out->from, out->to, out->req_id, out->payload, out->payload_len,
                         act);
        r = w4_consent_check(&out->consent, hpk, out->from, act, need, t->price_vfv, now_ms,
                             rt->consent_log);
        if (r) return r == W4_ERR_SPACE ? W4_ERR_CONSENT : r;
    }
    return W4_OK;
}

/* ===== Carriage ===== */
int w4_transport_send(const w4_transport_t *t, const uint8_t peer_id[W4_ID_LEN],
                      const uint8_t *bytes, uint32_t len, uint64_t freight_id)
{
    if (!t || !t->send || !peer_id || !bytes || len == 0) return W4_ERR_ARG;
    if (t->reach == W4_NET_OFFLINE) return W4_ERR_PENDING;
    if (!t->freight) return t->send(t->ctx, peer_id, bytes, len);
    uint8_t k = t->freight_k ? t->freight_k : FREIGHT_ROWS;
    uint32_t count = freight_set_count(len, k);
    if (count == 0) return W4_ERR_ARG;
    uint8_t frame[W4_FRAME_MAX];
    for (uint32_t s = 0; s < count; s++) {
        freight_header_t h;
        if (freight_split(freight_id, k, 0, 0, bytes, len, s, &h, frame + FREIGHT_HEADER_BYTES))
            return W4_ERR_ARG;
        if (freight_header_serialize(&h, frame)) return W4_ERR_ARG;
        int r = t->send(t->ctx, peer_id, frame, W4_FRAME_MAX);
        if (r) return r;
    }
    return W4_OK;
}

void w4_freight_rx_init(w4_freight_rx_t *rx)
{
    freight_join_init(&rx->j, rx->buf, sizeof rx->buf);
}

int w4_freight_rx_add(w4_freight_rx_t *rx, const uint8_t *frame, uint32_t flen, uint32_t *len)
{
    if (!rx || !frame || flen < FREIGHT_HEADER_BYTES || flen > W4_FRAME_MAX ||
        (flen - FREIGHT_HEADER_BYTES) % FREIGHT_PACKET_BYTES)
        return W4_ERR_PARSE;
    freight_header_t h;
    if (freight_header_parse(frame, &h) != FREIGHT_OK) return W4_ERR_PARSE;
    uint32_t np = (flen - FREIGHT_HEADER_BYTES) / FREIGHT_PACKET_BYTES;
    int r =
        freight_join_add(&rx->j, &h, frame + FREIGHT_HEADER_BYTES, np, rx->work, sizeof rx->work);
    if (r == FREIGHT_ERR_TOO_FEW) return W4_ERR_PENDING;
    if (r != FREIGHT_OK) return r == FREIGHT_ERR_HASH ? W4_ERR_HASH : W4_ERR_PARSE;
    uint64_t total = 0;
    if (!freight_join_complete(&rx->j, &total)) return W4_ERR_PENDING;
    if (len) *len = (uint32_t) total;
    return W4_OK;
}

void w4_outbox_init(w4_outbox_t *ob, w4_outbox_slot_t *storage, uint32_t cap)
{
    ob->s = storage;
    ob->cap = cap;
    for (uint32_t i = 0; i < cap; i++) storage[i].used = false;
}

uint32_t w4_outbox_count(const w4_outbox_t *ob)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < ob->cap; i++) n += ob->s[i].used;
    return n;
}

int w4_post(const w4_transport_t *t, w4_outbox_t *ob, const uint8_t peer_id[W4_ID_LEN],
            const uint8_t *bytes, uint32_t len, uint64_t freight_id)
{
    if (!peer_id || !bytes || len == 0 || len > W4_ENV_MAX) return W4_ERR_ARG;
    if (t && t->reach != W4_NET_OFFLINE && t->send &&
        w4_transport_send(t, peer_id, bytes, len, freight_id) == W4_OK)
        return W4_OK;
    if (!ob) return W4_ERR_PENDING;
    for (uint32_t i = 0; i < ob->cap; i++)
        if (!ob->s[i].used) {
            ob->s[i].used = true;
            w4_memcpy(ob->s[i].peer, peer_id, W4_ID_LEN);
            ob->s[i].len = len;
            w4_memcpy(ob->s[i].data, bytes, len);
            return W4_ERR_PENDING;
        }
    return W4_ERR_SPACE;
}

uint32_t w4_outbox_flush(const w4_transport_t *t, w4_outbox_t *ob, uint64_t freight_id_base)
{
    uint32_t sent = 0;
    if (!t || !ob || t->reach == W4_NET_OFFLINE) return 0;
    for (uint32_t i = 0; i < ob->cap; i++) {
        if (!ob->s[i].used) continue;
        if (w4_transport_send(t, ob->s[i].peer, ob->s[i].data, ob->s[i].len, freight_id_base + i) !=
            W4_OK)
            break; /* link dropped again: the rest stay queued */
        ob->s[i].used = false;
        sent++;
    }
    return sent;
}
