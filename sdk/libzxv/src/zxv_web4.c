/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_web4.c — ABI 1.5: the Web 4 agent layer over kernel/src/web4.
 * Every signature, digest, manifest and consent rule is the kernel's
 * (web4_agent.c, ML-DSA-65 from kernel/src/pqsec/pq_mldsa65.c, the only
 * ML-DSA-65 in this library). This file keeps private keys inside opaque
 * handles that wipe on destroy, gates signing on capabilities, defines the
 * card and consent-token byte forms (see the header), and maps every kernel
 * status to a ZXV_E_* code with a reason. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#ifndef ZXV_NO_PQC

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "web4_agent.h"

_Static_assert(ZXV_W4_PK_BYTES == W4_PK_LEN, "pk size");
_Static_assert(ZXV_W4_SIG_BYTES == W4_SIG_LEN, "sig size");
_Static_assert(ZXV_W4_ID_BYTES == W4_ID_LEN, "id size");
_Static_assert(ZXV_W4_NAME_MAX + 1 == W4_NAME_MAX && ZXV_W4_NAME_MAX + 1 == W4_TOOL_NAME_MAX,
               "name sizes");
_Static_assert(ZXV_W4_MAX_TOOLS == W4_MAX_TOOLS, "tools");
_Static_assert(ZXV_W4_MSG_MAX_PAYLOAD == W4_MSG_MAX_PAYLOAD, "payload");
_Static_assert(ZXV_W4_CONSENT_BYTES == W4_CONSENT_BYTES, "consent bytes");
_Static_assert(ZXV_W4_MANIFEST_MAX == W4_MANIFEST_MAX, "manifest max");
_Static_assert(ZXV_W4_ENV_MAX == W4_ENV_MAX, "envelope max");
_Static_assert(ZXV_W4_TOOL_MONEY == W4_TOOL_MONEY && ZXV_W4_TOOL_PII == W4_TOOL_PII &&
                   ZXV_W4_TOOL_STREAM == W4_TOOL_STREAM,
               "tool flags");
_Static_assert(ZXV_W4_CONSENT_MONEY == W4_CONSENT_MONEY && ZXV_W4_CONSENT_PII == W4_CONSENT_PII,
               "consent scopes");
_Static_assert(ZXV_W4_MSG_REQUEST == W4_MSG_REQUEST && ZXV_W4_MSG_RESPONSE == W4_MSG_RESPONSE &&
                   ZXV_W4_MSG_STREAM == W4_MSG_STREAM && ZXV_W4_MSG_CANCEL == W4_MSG_CANCEL &&
                   ZXV_W4_MSG_ERROR == W4_MSG_ERROR,
               "message kinds");
_Static_assert(ZXV_W4_MF_FINAL == W4_MF_FINAL && ZXV_W4_MF_CONSENT == W4_MF_CONSENT, "flags");

/* The card: "W4CD" ver flags name_len name bound_ms pk [node_pk] sig. */
#define CARD_FIXED (4u + 1u + 1u + 1u + 8u + W4_PK_LEN + W4_SIG_LEN)
_Static_assert(ZXV_W4_CARD_MAX == CARD_FIXED + ZXV_W4_NAME_MAX + W4_PK_LEN, "card max");
/* The consent body the kernel signs and carries in envelopes. */
#define CONSENT_BODY (4u + 3u * 32u + 4u + 3u * 8u + 16u)
_Static_assert(CONSENT_BODY + W4_SIG_LEN == W4_CONSENT_BYTES, "consent layout");
/* Envelope bytes before the tool name and after it (web4_agent.c env_body). */
#define ENV_FIXED (4u + 1u + 1u + 1u + 32u + 32u + 8u + 8u + 8u + 4u + 1u + 4u)

/* pq_mldsa65_keygen hands its seed to the vendored code through a static,
 * so key generation is serialized. Signing and verifying use no globals. */
static pthread_mutex_t g_keygen = PTHREAD_MUTEX_INITIALIZER;

#define AGENT_MAGIC    0x5A585741u /* "ZXWA" */
#define DIR_MAGIC      0x5A585744u /* "ZXWD" */
#define MANIFEST_MAGIC 0x5A58574Du /* "ZXWM" */
#define CLOG_MAGIC     0x5A585743u /* "ZXWC" */

struct zxv_w4_agent {
    uint32_t magic;
    zxv_ctx *ctx;
    w4_agent_t a; /* holds the private key; wiped on destroy */
};

struct zxv_w4_dir {
    uint32_t magic;
    w4_dir_t d;
    w4_card_t *store;
};

struct zxv_w4_manifest {
    uint32_t magic;
    bool is_signed;
    w4_manifest_t m;
};

struct zxv_w4_consent_log {
    uint32_t magic;
    w4_consent_log_t l;
    w4_consent_used_t *store;
};

/* ===================================================================== */
/* Helpers                                                               */
/* ===================================================================== */

static int32_t bytes_room(size_t needed, const uint8_t *buf, size_t cap, size_t *out_len,
                          const char *what)
{
    *out_len = needed;
    if (needed && (!buf || cap < needed))
        return zxvi_fail(ZXV_E_BUFFER, "%s: needs %zu bytes, buffer holds %zu", what, needed,
                         buf ? cap : (size_t) 0);
    return ZXV_OK;
}

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

/* Agent and tool names: 1..31 of [a-z0-9._-] (the kernel's name_ok). */
static int32_t check_name(const char *s, const char *what)
{
    int32_t r = zxvi_check_str(s, ZXV_W4_NAME_MAX, what, false);
    if (r) return r;
    for (const char *p = s; *p; p++)
        if (!name_char(*p))
            return zxvi_fail(ZXV_E_ARG, "%s: only a-z, 0-9, '.', '_' and '-' are allowed", what);
    return ZXV_OK;
}

static int32_t agent_ok(const zxv_w4_agent *ag, const char *what)
{
    if (!ag || ag->magic != AGENT_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: agent is NULL or not an agent handle", what);
    return ZXV_OK;
}

/* Gate a signing call: the cap holds `bit` and belongs to the handle's
 * context (poison-first through the gate). */
static int32_t gate_agent(const zxv_cap *cap, uint64_t bit, const zxv_w4_agent *ag,
                          const char *what)
{
    if (!ag || ag->magic != AGENT_MAGIC) {
        int32_t r = zxvi_cap_gate(cap, bit, what, NULL);
        if (r) return r;
        return zxvi_fail(ZXV_E_ARG, "%s: agent is NULL or not an agent handle", what);
    }
    return zxvi_cap_gate_ctx(cap, bit, what, ag->ctx);
}

/* 32 fresh bytes for hedged ML-DSA signing. */
static int32_t fresh(uint8_t rnd[32])
{
    return zxv_random(rnd, 32);
}

/* ----- the card ----- */

static size_t card_len(const w4_card_t *c)
{
    size_t nl = strnlen(c->name, W4_NAME_MAX);
    return CARD_FIXED + nl + (c->self_bound ? 0u : W4_PK_LEN);
}

static void put_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint64_t get_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static void card_put(const w4_card_t *c, uint8_t *out)
{
    size_t nl = strnlen(c->name, W4_NAME_MAX), o = 0;
    memcpy(out, "W4CD", 4);
    o = 4;
    out[o++] = 1;
    out[o++] = c->self_bound ? 1u : 0u;
    out[o++] = (uint8_t) nl;
    memcpy(out + o, c->name, nl);
    o += nl;
    put_le64(out + o, c->bound_ms);
    o += 8;
    memcpy(out + o, c->pk, W4_PK_LEN);
    o += W4_PK_LEN;
    if (!c->self_bound) {
        memcpy(out + o, c->node_pk, W4_PK_LEN);
        o += W4_PK_LEN;
    }
    memcpy(out + o, c->bind_sig, W4_SIG_LEN);
}

/* Parse and verify a card. ZXV_E_FORMAT / ZXV_E_VERIFY. */
static int32_t card_get(const uint8_t *in, size_t len, w4_card_t *c, const char *what)
{
    if (!in) return zxvi_fail(ZXV_E_ARG, "%s: card is NULL", what);
    if (len < CARD_FIXED + 1u || memcmp(in, "W4CD", 4) != 0 || in[4] != 1 || in[5] > 1)
        return zxvi_fail(ZXV_E_FORMAT, "%s: not a version 1 agent card", what);
    memset(c, 0, sizeof *c);
    c->self_bound = in[5] == 1;
    size_t nl = in[6];
    if (nl == 0 || nl > ZXV_W4_NAME_MAX)
        return zxvi_fail(ZXV_E_FORMAT, "%s: name length %zu is not 1..31", what, nl);
    if (len != CARD_FIXED + nl + (c->self_bound ? 0u : W4_PK_LEN))
        return zxvi_fail(ZXV_E_FORMAT, "%s: %zu bytes is the wrong length for this card", what,
                         len);
    size_t o = 7;
    for (size_t i = 0; i < nl; i++)
        if (!name_char((char) in[o + i]))
            return zxvi_fail(ZXV_E_FORMAT, "%s: invalid name character", what);
    memcpy(c->name, in + o, nl);
    o += nl;
    c->bound_ms = get_le64(in + o);
    o += 8;
    memcpy(c->pk, in + o, W4_PK_LEN);
    o += W4_PK_LEN;
    w4_key_id(c->pk, c->agent_id);
    if (c->self_bound) {
        memcpy(c->peer_id, c->agent_id, W4_ID_LEN);
    } else {
        memcpy(c->node_pk, in + o, W4_PK_LEN);
        o += W4_PK_LEN;
        w4_key_id(c->node_pk, c->peer_id);
    }
    memcpy(c->bind_sig, in + o, W4_SIG_LEN);
    int rc = w4_card_verify(c);
    if (rc == W4_OK) return ZXV_OK;
    if (rc == W4_ERR_SIG) return zxvi_fail(ZXV_E_VERIFY, "%s: binding signature does not verify", what);
    return zxvi_fail(ZXV_E_FORMAT, "%s: card refused by the kernel (%d)", what, rc);
}

/* ----- the consent token ----- */

static void consent_put(const w4_consent_t *t, uint8_t out[W4_CONSENT_BYTES])
{
    size_t o = 0;
    memcpy(out, "W4CS", 4);
    o = 4;
    memcpy(out + o, t->human_id, 32);
    o += 32;
    memcpy(out + o, t->agent_id, 32);
    o += 32;
    memcpy(out + o, t->action, 32);
    o += 32;
    put_le32(out + o, t->scope);
    o += 4;
    put_le64(out + o, t->max_vfv);
    o += 8;
    put_le64(out + o, t->issued_ms);
    o += 8;
    put_le64(out + o, t->expires_ms);
    o += 8;
    memcpy(out + o, t->nonce, 16);
    o += 16;
    memcpy(out + o, t->sig, W4_SIG_LEN);
}

static int32_t consent_get(const uint8_t *in, w4_consent_t *t, const char *what)
{
    if (memcmp(in, "W4CS", 4) != 0)
        return zxvi_fail(ZXV_E_FORMAT, "%s: not a consent token", what);
    memset(t, 0, sizeof *t);
    size_t o = 4;
    memcpy(t->human_id, in + o, 32);
    o += 32;
    memcpy(t->agent_id, in + o, 32);
    o += 32;
    memcpy(t->action, in + o, 32);
    o += 32;
    t->scope = get_le32(in + o);
    o += 4;
    t->max_vfv = get_le64(in + o);
    o += 8;
    t->issued_ms = get_le64(in + o);
    o += 8;
    t->expires_ms = get_le64(in + o);
    o += 8;
    memcpy(t->nonce, in + o, 16);
    o += 16;
    memcpy(t->sig, in + o, W4_SIG_LEN);
    return ZXV_OK;
}

/* ===================================================================== */
/* Key handles                                                           */
/* ===================================================================== */

int32_t zxv_w4_agent_create(const zxv_cap *cap, const uint8_t *seed, const char *name,
                            const zxv_w4_agent *node, uint64_t now_ms, zxv_w4_agent **out_agent)
{
    const char *what = "w4_agent_create";
    zxv_ctx *ctx = NULL;
    int32_t r = zxvi_cap_gate(cap, ZXV_CAP_CRYPTO_SIGN, what, &ctx);
    if (r) return r;
    if (!out_agent) return zxvi_fail(ZXV_E_ARG, "%s: out_agent is NULL", what);
    if ((r = check_name(name, "w4_agent_create: name"))) return r;
    if (node) {
        if ((r = agent_ok(node, what))) return r;
        if (node->ctx != ctx)
            return zxvi_fail(ZXV_E_CAP, "%s: the node belongs to another context", what);
    }
    uint8_t s[32], rnd[32];
    if (seed)
        memcpy(s, seed, 32);
    else if ((r = zxv_random(s, 32)))
        return r;
    if ((r = fresh(rnd))) {
        zxvi_wipe(s, sizeof s);
        return r;
    }
    zxv_w4_agent *ag = calloc(1, sizeof *ag);
    if (!ag) {
        zxvi_wipe(s, sizeof s);
        return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    }
    pthread_mutex_lock(&g_keygen);
    int rc = w4_agent_create(&ag->a, s, name, node ? node->a.card.pk : NULL,
                             node ? node->a.sk : NULL, now_ms, rnd);
    pthread_mutex_unlock(&g_keygen);
    zxvi_wipe(s, sizeof s);
    zxvi_wipe(rnd, sizeof rnd);
    if (rc != W4_OK) {
        zxvi_wipe(ag, sizeof *ag);
        free(ag);
        return zxvi_fail(ZXV_E_INTERNAL, "%s: the new binding does not verify (%d)", what, rc);
    }
    ag->magic = AGENT_MAGIC;
    ag->ctx = ctx;
    *out_agent = ag;
    return zxvi_ok();
}

void zxv_w4_agent_destroy(zxv_w4_agent *agent)
{
    if (!agent) return;
    zxvi_wipe(agent, sizeof *agent);
    free(agent);
}

int32_t zxv_w4_agent_info(const zxv_w4_agent *agent, uint8_t out_agent_id[32],
                          uint8_t out_peer_id[32], uint8_t out_public_key[1952])
{
    int32_t r = agent_ok(agent, "w4_agent_info");
    if (r) return r;
    if ((r = zxvi_ctx_check(agent->ctx, "w4_agent_info"))) return r;
    if (out_agent_id) memcpy(out_agent_id, agent->a.card.agent_id, 32);
    if (out_peer_id) memcpy(out_peer_id, agent->a.card.peer_id, 32);
    if (out_public_key) memcpy(out_public_key, agent->a.card.pk, W4_PK_LEN);
    return zxvi_ok();
}

int32_t zxv_w4_agent_card(const zxv_w4_agent *agent, uint8_t *buf, size_t cap, size_t *out_len)
{
    int32_t r = agent_ok(agent, "w4_agent_card");
    if (r) return r;
    if ((r = zxvi_ctx_check(agent->ctx, "w4_agent_card"))) return r;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "w4_agent_card: out_len is NULL");
    size_t need;
    r = bytes_room(card_len(&agent->a.card), buf, cap, &need, "w4_agent_card");
    *out_len = need;
    if (r) return r;
    card_put(&agent->a.card, buf);
    return zxvi_ok();
}

int32_t zxv_w4_card_verify(const uint8_t *card, size_t len, uint8_t out_agent_id[32],
                           uint8_t out_peer_id[32], char out_name[32], uint64_t *out_bound_ms)
{
    w4_card_t *c = malloc(sizeof *c);
    if (!c) return zxvi_fail(ZXV_E_NOMEM, "w4_card_verify: out of memory");
    int32_t r = card_get(card, len, c, "w4_card_verify");
    if (r == ZXV_OK) {
        if (out_agent_id) memcpy(out_agent_id, c->agent_id, 32);
        if (out_peer_id) memcpy(out_peer_id, c->peer_id, 32);
        if (out_name) memcpy(out_name, c->name, 32);
        if (out_bound_ms) *out_bound_ms = c->bound_ms;
        r = zxvi_ok();
    }
    free(c);
    return r;
}

/* ===================================================================== */
/* Directory                                                             */
/* ===================================================================== */

static int32_t dir_ok(const zxv_w4_dir *d, const char *what)
{
    if (!d || d->magic != DIR_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: dir is NULL or not a directory handle", what);
    return ZXV_OK;
}

int32_t zxv_w4_dir_create(uint32_t capacity, zxv_w4_dir **out_dir)
{
    if (!out_dir) return zxvi_fail(ZXV_E_ARG, "w4_dir_create: out_dir is NULL");
    if (capacity < 1 || capacity > ZXV_W4_DIR_MAX)
        return zxvi_fail(ZXV_E_RANGE, "w4_dir_create: capacity %u is not 1..%u",
                         (unsigned) capacity, (unsigned) ZXV_W4_DIR_MAX);
    zxv_w4_dir *d = calloc(1, sizeof *d);
    if (d) d->store = calloc(capacity, sizeof *d->store);
    if (!d || !d->store) {
        free(d);
        return zxvi_fail(ZXV_E_NOMEM, "w4_dir_create: out of memory");
    }
    d->magic = DIR_MAGIC;
    w4_dir_init(&d->d, d->store, capacity);
    *out_dir = d;
    return zxvi_ok();
}

void zxv_w4_dir_destroy(zxv_w4_dir *dir)
{
    if (!dir) return;
    free(dir->store);
    dir->magic = 0;
    free(dir);
}

int32_t zxv_w4_dir_add(zxv_w4_dir *dir, const uint8_t *card, size_t len)
{
    int32_t r = dir_ok(dir, "w4_dir_add");
    if (r) return r;
    w4_card_t *c = malloc(sizeof *c);
    if (!c) return zxvi_fail(ZXV_E_NOMEM, "w4_dir_add: out of memory");
    r = card_get(card, len, c, "w4_dir_add");
    if (r == ZXV_OK) {
        int rc = w4_dir_add(&dir->d, c);
        if (rc == W4_OK)
            r = zxvi_ok();
        else if (rc == W4_ERR_STALE)
            r = zxvi_fail(ZXV_E_STATE, "w4_dir_add: the card is older than the one held");
        else if (rc == W4_ERR_SPACE)
            r = zxvi_fail(ZXV_E_CAPACITY, "w4_dir_add: directory full (%u)", (unsigned) dir->d.cap);
        else
            r = zxvi_fail(ZXV_E_INTERNAL, "w4_dir_add: unexpected kernel status %d", rc);
    }
    free(c);
    return r;
}

int32_t zxv_w4_dir_find(const zxv_w4_dir *dir, const uint8_t agent_id[32], uint8_t *buf,
                        size_t cap, size_t *out_len)
{
    int32_t r = dir_ok(dir, "w4_dir_find");
    if (r) return r;
    if (!agent_id || !out_len) return zxvi_fail(ZXV_E_ARG, "w4_dir_find: NULL argument");
    const w4_card_t *c = w4_dir_find(&dir->d, agent_id);
    if (!c) return zxvi_fail(ZXV_E_NOT_FOUND, "w4_dir_find: agent not in the directory");
    size_t need;
    r = bytes_room(card_len(c), buf, cap, &need, "w4_dir_find");
    *out_len = need;
    if (r) return r;
    card_put(c, buf);
    return zxvi_ok();
}

int32_t zxv_w4_dir_count(const zxv_w4_dir *dir, uint32_t *out_count)
{
    int32_t r = dir_ok(dir, "w4_dir_count");
    if (r) return r;
    if (!out_count) return zxvi_fail(ZXV_E_ARG, "w4_dir_count: out_count is NULL");
    *out_count = dir->d.n;
    return zxvi_ok();
}

/* ===================================================================== */
/* Manifests                                                             */
/* ===================================================================== */

static int32_t man_ok(const zxv_w4_manifest *m, const char *what)
{
    if (!m || m->magic != MANIFEST_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: manifest is NULL or not a manifest handle", what);
    return ZXV_OK;
}

int32_t zxv_w4_manifest_create(uint32_t version, uint64_t issued_ms, uint64_t expires_ms,
                               zxv_w4_manifest **out_manifest)
{
    if (!out_manifest) return zxvi_fail(ZXV_E_ARG, "w4_manifest_create: out_manifest is NULL");
    if (expires_ms <= issued_ms)
        return zxvi_fail(ZXV_E_ARG, "w4_manifest_create: expires_ms must be after issued_ms");
    zxv_w4_manifest *m = calloc(1, sizeof *m);
    if (!m) return zxvi_fail(ZXV_E_NOMEM, "w4_manifest_create: out of memory");
    m->magic = MANIFEST_MAGIC;
    m->m.version = version;
    m->m.issued_ms = issued_ms;
    m->m.expires_ms = expires_ms;
    *out_manifest = m;
    return zxvi_ok();
}

void zxv_w4_manifest_destroy(zxv_w4_manifest *m)
{
    if (!m) return;
    m->magic = 0;
    free(m);
}

int32_t zxv_w4_manifest_add_tool(zxv_w4_manifest *m, const char *name, uint64_t price_vfv,
                                 uint32_t rate_per_min, uint32_t burst, uint32_t flags,
                                 uint32_t max_input)
{
    const char *what = "w4_manifest_add_tool";
    int32_t r = man_ok(m, what);
    if (r) return r;
    if ((r = check_name(name, "w4_manifest_add_tool: name"))) return r;
    if (rate_per_min == 0 || burst == 0)
        return zxvi_fail(ZXV_E_ARG, "%s: rate_per_min and burst must be >= 1", what);
    if (flags & ~(uint32_t) (W4_TOOL_MONEY | W4_TOOL_PII | W4_TOOL_STREAM))
        return zxvi_fail(ZXV_E_ARG, "%s: unknown tool flags 0x%x", what, (unsigned) flags);
    if (max_input > W4_MSG_MAX_PAYLOAD)
        return zxvi_fail(ZXV_E_ARG, "%s: max_input %u is over %u", what, (unsigned) max_input,
                         (unsigned) W4_MSG_MAX_PAYLOAD);
    if (w4_manifest_find(&m->m, name) >= 0)
        return zxvi_fail(ZXV_E_DUPLICATE, "%s: tool '%s' is already listed", what, name);
    if (m->m.ntools >= W4_MAX_TOOLS)
        return zxvi_fail(ZXV_E_CAPACITY, "%s: a manifest lists at most 16 tools", what);
    int rc = w4_manifest_add_tool(&m->m, name, price_vfv, rate_per_min, burst, flags, max_input);
    if (rc != W4_OK) return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, rc);
    m->is_signed = false;
    memset(m->m.sig, 0, sizeof m->m.sig);
    return zxvi_ok();
}

int32_t zxv_w4_manifest_sign(const zxv_cap *cap, const zxv_w4_agent *agent, zxv_w4_manifest *m)
{
    const char *what = "w4_manifest_sign";
    int32_t r = gate_agent(cap, ZXV_CAP_CRYPTO_SIGN, agent, what);
    if (r) return r;
    if ((r = man_ok(m, what))) return r;
    uint8_t rnd[32];
    if ((r = fresh(rnd))) return r;
    int rc = w4_manifest_sign(&agent->a, &m->m, rnd);
    zxvi_wipe(rnd, sizeof rnd);
    if (rc != W4_OK) return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, rc);
    m->is_signed = true;
    return zxvi_ok();
}

int32_t zxv_w4_manifest_verify(const zxv_w4_manifest *m, const uint8_t public_key[1952],
                               uint64_t now_ms)
{
    const char *what = "w4_manifest_verify";
    int32_t r = man_ok(m, what);
    if (r) return r;
    if (!public_key) return zxvi_fail(ZXV_E_ARG, "%s: public_key is NULL", what);
    if (!m->is_signed) return zxvi_fail(ZXV_E_VERIFY, "%s: the manifest is not signed", what);
    int rc = w4_manifest_verify(&m->m, public_key, now_ms);
    switch (rc) {
    case W4_OK: return zxvi_ok();
    case W4_ERR_BINDING:
        return zxvi_fail(ZXV_E_VERIFY, "%s: the key is not the manifest's agent", what);
    case W4_ERR_SIG: return zxvi_fail(ZXV_E_VERIFY, "%s: signature does not verify", what);
    case W4_ERR_EXPIRED:
        return zxvi_fail(ZXV_E_STATE, "%s: not valid at %llu (valid %llu..%llu)", what,
                         (unsigned long long) now_ms, (unsigned long long) m->m.issued_ms,
                         (unsigned long long) m->m.expires_ms);
    default: return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, rc);
    }
}

int32_t zxv_w4_manifest_encode(const zxv_w4_manifest *m, uint8_t *buf, size_t cap,
                               size_t *out_len)
{
    const char *what = "w4_manifest_encode";
    int32_t r = man_ok(m, what);
    if (r) return r;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "%s: out_len is NULL", what);
    if (!m->is_signed) return zxvi_fail(ZXV_E_STATE, "%s: sign the manifest first", what);
    uint8_t tmp[W4_MANIFEST_MAX];
    int32_t n = w4_manifest_encode(&m->m, tmp, sizeof tmp);
    if (n < 0) return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, (int) n);
    size_t need;
    r = bytes_room((size_t) n, buf, cap, &need, what);
    *out_len = need;
    if (r) return r;
    memcpy(buf, tmp, (size_t) n);
    return zxvi_ok();
}

int32_t zxv_w4_manifest_decode(const uint8_t *bytes, size_t len, zxv_w4_manifest **out_manifest)
{
    const char *what = "w4_manifest_decode";
    if (!bytes || !out_manifest) return zxvi_fail(ZXV_E_ARG, "%s: NULL argument", what);
    if (len > W4_MANIFEST_MAX)
        return zxvi_fail(ZXV_E_FORMAT, "%s: %zu bytes is longer than any manifest", what, len);
    zxv_w4_manifest *m = calloc(1, sizeof *m);
    if (!m) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    int rc = w4_manifest_decode(bytes, (uint32_t) len, &m->m);
    if (rc != W4_OK) {
        free(m);
        return zxvi_fail(ZXV_E_FORMAT, "%s: malformed manifest", what);
    }
    m->magic = MANIFEST_MAGIC;
    m->is_signed = true;
    *out_manifest = m;
    return zxvi_ok();
}

int32_t zxv_w4_manifest_to_json(const zxv_w4_manifest *m, char *buf, size_t cap, size_t *out_len)
{
    const char *what = "w4_manifest_to_json";
    int32_t r = man_ok(m, what);
    if (r) return r;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "%s: out_len is NULL", what);
    enum { JSON_MAX = 16384 }; /* 16 tools at their longest stay far below */
    char *tmp = malloc(JSON_MAX);
    if (!tmp) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    int32_t n = w4_manifest_json(&m->m, tmp, JSON_MAX);
    if (n < 0)
        r = zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, (int) n);
    else
        r = zxvi_out_text_n(tmp, (size_t) n, buf, cap, out_len);
    free(tmp);
    return r;
}

int32_t zxv_w4_manifest_info(const zxv_w4_manifest *m, uint8_t out_agent_id[32],
                             uint8_t out_peer_id[32], uint32_t *out_version,
                             uint64_t *out_issued_ms, uint64_t *out_expires_ms,
                             uint32_t *out_tool_count, int32_t *out_signed)
{
    int32_t r = man_ok(m, "w4_manifest_info");
    if (r) return r;
    if (out_agent_id) memcpy(out_agent_id, m->m.agent_id, 32);
    if (out_peer_id) memcpy(out_peer_id, m->m.peer_id, 32);
    if (out_version) *out_version = m->m.version;
    if (out_issued_ms) *out_issued_ms = m->m.issued_ms;
    if (out_expires_ms) *out_expires_ms = m->m.expires_ms;
    if (out_tool_count) *out_tool_count = m->m.ntools;
    if (out_signed) *out_signed = m->is_signed ? 1 : 0;
    return zxvi_ok();
}

int32_t zxv_w4_manifest_tool(const zxv_w4_manifest *m, uint32_t index, char out_name[32],
                             uint64_t *out_price_vfv, uint32_t *out_rate_per_min,
                             uint32_t *out_burst, uint32_t *out_flags, uint32_t *out_max_input)
{
    int32_t r = man_ok(m, "w4_manifest_tool");
    if (r) return r;
    if (index >= m->m.ntools)
        return zxvi_fail(ZXV_E_NOT_FOUND, "w4_manifest_tool: index %u; the manifest has %u tools",
                         (unsigned) index, (unsigned) m->m.ntools);
    const w4_tool_t *t = &m->m.tools[index];
    if (out_name) memcpy(out_name, t->name, 32);
    if (out_price_vfv) *out_price_vfv = t->price_vfv;
    if (out_rate_per_min) *out_rate_per_min = t->rate_per_min;
    if (out_burst) *out_burst = t->burst;
    if (out_flags) *out_flags = t->flags;
    if (out_max_input) *out_max_input = t->max_input;
    return zxvi_ok();
}

int32_t zxv_w4_manifest_find(const zxv_w4_manifest *m, const char *name, uint32_t *out_index)
{
    int32_t r = man_ok(m, "w4_manifest_find");
    if (r) return r;
    if (!out_index) return zxvi_fail(ZXV_E_ARG, "w4_manifest_find: out_index is NULL");
    if ((r = zxvi_check_str(name, ZXV_W4_NAME_MAX, "w4_manifest_find: name", false))) return r;
    int32_t i = w4_manifest_find(&m->m, name);
    if (i < 0) return zxvi_fail(ZXV_E_NOT_FOUND, "w4_manifest_find: no tool '%s'", name);
    *out_index = (uint32_t) i;
    return zxvi_ok();
}

/* ===================================================================== */
/* Consent                                                               */
/* ===================================================================== */

int32_t zxv_w4_action_digest(const char *tool, const uint8_t from[32], const uint8_t to[32],
                             uint64_t req_id, const uint8_t *payload, size_t len,
                             uint8_t out_digest[32])
{
    const char *what = "w4_action_digest";
    if (!from || !to || !out_digest || (len && !payload))
        return zxvi_fail(ZXV_E_ARG, "%s: NULL argument", what);
    int32_t r = check_name(tool, "w4_action_digest: tool");
    if (r) return r;
    if (len > W4_MSG_MAX_PAYLOAD)
        return zxvi_fail(ZXV_E_TOO_LONG, "%s: payload is %zu bytes; maximum is 2048", what, len);
    w4_action_digest(tool, from, to, req_id, len ? payload : (const uint8_t *) "", (uint32_t) len,
                     out_digest);
    return zxvi_ok();
}

int32_t zxv_w4_consent_issue(const zxv_cap *cap, const zxv_w4_agent *human,
                             const uint8_t agent_id[32], const uint8_t action[32], uint32_t scope,
                             uint64_t max_vfv, uint64_t issued_ms, uint64_t expires_ms,
                             const uint8_t *nonce, uint8_t out_token[3453])
{
    const char *what = "w4_consent_issue";
    int32_t r = gate_agent(cap, ZXV_CAP_ATTEST, human, what);
    if (r) return r;
    if (!agent_id || !action || !out_token) return zxvi_fail(ZXV_E_ARG, "%s: NULL argument", what);
    if (scope == 0 || (scope & ~(uint32_t) (W4_CONSENT_MONEY | W4_CONSENT_PII)))
        return zxvi_fail(ZXV_E_ARG, "%s: scope 0x%x is not a nonzero ZXV_W4_CONSENT_* set", what,
                         (unsigned) scope);
    if (expires_ms <= issued_ms)
        return zxvi_fail(ZXV_E_ARG, "%s: expires_ms must be after issued_ms", what);
    uint8_t n[16], rnd[32];
    if (nonce)
        memcpy(n, nonce, 16);
    else if ((r = zxv_random(n, 16)))
        return r;
    if ((r = fresh(rnd))) return r;
    w4_consent_t *t = malloc(sizeof *t);
    if (!t) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    int rc = w4_consent_issue(human->a.card.pk, human->a.sk, agent_id, action, scope, max_vfv,
                              issued_ms, expires_ms, n, rnd, t);
    zxvi_wipe(rnd, sizeof rnd);
    if (rc == W4_OK) {
        consent_put(t, out_token);
        r = zxvi_ok();
    } else {
        r = zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, rc);
    }
    free(t);
    return r;
}

int32_t zxv_w4_consent_info(const uint8_t token[3453], uint8_t out_human_id[32],
                            uint8_t out_agent_id[32], uint8_t out_action[32], uint32_t *out_scope,
                            uint64_t *out_max_vfv, uint64_t *out_issued_ms,
                            uint64_t *out_expires_ms, uint8_t out_nonce[16])
{
    if (!token) return zxvi_fail(ZXV_E_ARG, "w4_consent_info: token is NULL");
    w4_consent_t t;
    int32_t r = consent_get(token, &t, "w4_consent_info");
    if (r) return r;
    if (out_human_id) memcpy(out_human_id, t.human_id, 32);
    if (out_agent_id) memcpy(out_agent_id, t.agent_id, 32);
    if (out_action) memcpy(out_action, t.action, 32);
    if (out_scope) *out_scope = t.scope;
    if (out_max_vfv) *out_max_vfv = t.max_vfv;
    if (out_issued_ms) *out_issued_ms = t.issued_ms;
    if (out_expires_ms) *out_expires_ms = t.expires_ms;
    if (out_nonce) memcpy(out_nonce, t.nonce, 16);
    return zxvi_ok();
}

static int32_t clog_ok(const zxv_w4_consent_log *l, const char *what)
{
    if (!l || l->magic != CLOG_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: log is NULL or not a consent log handle", what);
    return ZXV_OK;
}

int32_t zxv_w4_consent_log_create(uint32_t capacity, zxv_w4_consent_log **out_log)
{
    if (!out_log) return zxvi_fail(ZXV_E_ARG, "w4_consent_log_create: out_log is NULL");
    if (capacity < 1 || capacity > ZXV_W4_CONSENT_LOG_MAX)
        return zxvi_fail(ZXV_E_RANGE, "w4_consent_log_create: capacity %u is not 1..%u",
                         (unsigned) capacity, (unsigned) ZXV_W4_CONSENT_LOG_MAX);
    zxv_w4_consent_log *l = calloc(1, sizeof *l);
    if (l) l->store = calloc(capacity, sizeof *l->store);
    if (!l || !l->store) {
        free(l);
        return zxvi_fail(ZXV_E_NOMEM, "w4_consent_log_create: out of memory");
    }
    l->magic = CLOG_MAGIC;
    w4_consent_log_init(&l->l, l->store, capacity);
    *out_log = l;
    return zxvi_ok();
}

void zxv_w4_consent_log_destroy(zxv_w4_consent_log *log)
{
    if (!log) return;
    free(log->store);
    log->magic = 0;
    free(log);
}

int32_t zxv_w4_consent_check(zxv_w4_consent_log *log, const uint8_t *token,
                             const uint8_t human_public_key[1952], const uint8_t agent_id[32],
                             const uint8_t action[32], uint32_t need, uint64_t amount,
                             uint64_t now_ms)
{
    const char *what = "w4_consent_check";
    int32_t r = clog_ok(log, what);
    if (r) return r;
    if (!human_public_key || !agent_id || !action)
        return zxvi_fail(ZXV_E_ARG, "%s: NULL argument", what);
    if (need == 0 || (need & ~(uint32_t) (W4_CONSENT_MONEY | W4_CONSENT_PII)))
        return zxvi_fail(ZXV_E_ARG, "%s: need 0x%x is not a nonzero ZXV_W4_CONSENT_* set", what,
                         (unsigned) need);
    if (!token) return zxvi_fail(ZXV_E_CONSENT, "%s: no consent token was presented", what);
    w4_consent_t *t = malloc(sizeof *t);
    if (!t) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    r = consent_get(token, t, what);
    if (r == ZXV_OK) {
        int rc = w4_consent_check(t, human_public_key, agent_id, action, need, amount, now_ms,
                                  &log->l);
        switch (rc) {
        case W4_OK: r = zxvi_ok(); break;
        case W4_ERR_CONSENT:
            r = zxvi_fail(ZXV_E_CONSENT,
                          "%s: the token does not cover this human, agent, action, scope or "
                          "amount",
                          what);
            break;
        case W4_ERR_EXPIRED:
            r = zxvi_fail(ZXV_E_CONSENT, "%s: the token is expired or not yet valid", what);
            break;
        case W4_ERR_SIG:
            r = zxvi_fail(ZXV_E_CONSENT, "%s: the token's signature does not verify", what);
            break;
        case W4_ERR_REPLAY:
            r = zxvi_fail(ZXV_E_CONSENT, "%s: the token was already used or revoked", what);
            break;
        case W4_ERR_SPACE:
            r = zxvi_fail(ZXV_E_CAPACITY, "%s: the consent log is full (fail closed)", what);
            break;
        default: r = zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, rc);
        }
    }
    free(t);
    return r;
}

int32_t zxv_w4_consent_revoke(zxv_w4_consent_log *log, const uint8_t token[3453], uint64_t now_ms)
{
    const char *what = "w4_consent_revoke";
    int32_t r = clog_ok(log, what);
    if (r) return r;
    if (!token) return zxvi_fail(ZXV_E_ARG, "%s: token is NULL", what);
    w4_consent_t *t = malloc(sizeof *t);
    if (!t) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    r = consent_get(token, t, what);
    if (r) {
        free(t);
        return r;
    }
    /* The single-use key, exactly as w4_consent_check derives it:
     * SHA3-256(human_id || nonce). An entry under it makes the kernel
     * refuse the token as a replay. */
    uint8_t kb[W4_ID_LEN + 16], key[W4_HASH_LEN];
    memcpy(kb, t->human_id, W4_ID_LEN);
    memcpy(kb + W4_ID_LEN, t->nonce, 16);
    w4_sha3_256(kb, sizeof kb, key);
    uint64_t expires = t->expires_ms;
    free(t);
    if (expires <= now_ms) return zxvi_ok(); /* already unusable; nothing to hold */
    w4_consent_log_t *l = &log->l;
    for (uint32_t i = 0; i < l->n; i++)
        if (memcmp(l->e[i].key, key, W4_HASH_LEN) == 0) return zxvi_ok();
    if (l->n >= l->cap) {
        w4_consent_log_prune(l, now_ms);
        if (l->n >= l->cap)
            return zxvi_fail(ZXV_E_CAPACITY, "%s: the consent log is full", what);
    }
    memcpy(l->e[l->n].key, key, W4_HASH_LEN);
    l->e[l->n].expires_ms = expires;
    l->n++;
    return zxvi_ok();
}

int32_t zxv_w4_consent_log_prune(zxv_w4_consent_log *log, uint64_t now_ms, uint32_t *out_remaining)
{
    int32_t r = clog_ok(log, "w4_consent_log_prune");
    if (r) return r;
    w4_consent_log_prune(&log->l, now_ms);
    if (out_remaining) *out_remaining = log->l.n;
    return zxvi_ok();
}

/* ===================================================================== */
/* Envelopes                                                             */
/* ===================================================================== */

/* Decode into heap memory (an envelope is ~9 KiB). */
static int32_t env_parse(const uint8_t *env, size_t len, w4_env_t **out, uint32_t *signed_len,
                         const char *what)
{
    if (!env) return zxvi_fail(ZXV_E_ARG, "%s: envelope is NULL", what);
    if (len > W4_ENV_MAX)
        return zxvi_fail(ZXV_E_FORMAT, "%s: %zu bytes is longer than any envelope", what, len);
    w4_env_t *e = malloc(sizeof *e);
    if (!e) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    if (w4_env_decode(env, (uint32_t) len, e, signed_len) != W4_OK) {
        free(e);
        return zxvi_fail(ZXV_E_FORMAT, "%s: malformed envelope", what);
    }
    *out = e;
    return ZXV_OK;
}

/* Build and seal with an already-gated agent. */
static int32_t seal(zxv_w4_agent *ag, int32_t kind, const uint8_t to[32], uint64_t req_id,
                    uint32_t chunk, const char *tool, uint32_t flags, const uint8_t *payload,
                    size_t len, const uint8_t *consent_token, uint64_t now_ms, uint8_t *buf,
                    size_t buf_cap, size_t *out_len, const char *what)
{
    if (!to || !out_len || (len && !payload)) return zxvi_fail(ZXV_E_ARG, "%s: NULL argument", what);
    if (kind < W4_MSG_REQUEST || kind > W4_MSG_ERROR)
        return zxvi_fail(ZXV_E_ARG, "%s: kind %d is not a ZXV_W4_MSG_* value", what, (int) kind);
    if (flags & ~(uint32_t) W4_MF_FINAL)
        return zxvi_fail(ZXV_E_ARG, "%s: flags may only be ZXV_W4_MF_FINAL", what);
    int32_t r = check_name(tool, what);
    if (r) return r;
    if (len > W4_MSG_MAX_PAYLOAD)
        return zxvi_fail(ZXV_E_TOO_LONG, "%s: payload is %zu bytes; maximum is 2048", what, len);
    w4_env_t *e = calloc(1, sizeof *e);
    if (!e) return zxvi_fail(ZXV_E_NOMEM, "%s: out of memory", what);
    if (consent_token && (r = consent_get(consent_token, &e->consent, what))) {
        free(e);
        return r;
    }
    size_t need = ENV_FIXED + strlen(tool) + len + (consent_token ? W4_CONSENT_BYTES : 0u) +
                  W4_SIG_LEN;
    size_t have;
    r = bytes_room(need, buf, buf_cap, &have, what);
    *out_len = have;
    if (r) {
        free(e);
        return r;
    }
    uint8_t rnd[32];
    if ((r = fresh(rnd))) {
        free(e);
        return r;
    }
    w4_consent_t keep = e->consent;
    int rc = w4_env_make(e, (uint8_t) kind, ag->a.card.agent_id, to, req_id, chunk, tool,
                         len ? payload : NULL, (uint32_t) len);
    int32_t n = rc;
    if (rc == W4_OK) {
        e->flags = (uint8_t) flags;
        if (consent_token) {
            e->consent = keep;
            e->flags |= W4_MF_CONSENT;
        }
        n = w4_env_seal(&ag->a, e, now_ms, rnd, buf, (uint32_t) need);
    }
    zxvi_wipe(rnd, sizeof rnd);
    free(e);
    if (n < 0) return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, (int) n);
    if ((size_t) n != need)
        return zxvi_fail(ZXV_E_INTERNAL, "%s: sealed %d bytes, expected %zu", what, (int) n, need);
    return zxvi_ok();
}

int32_t zxv_w4_env_seal(const zxv_cap *cap, zxv_w4_agent *agent, int32_t kind,
                        const uint8_t to[32], uint64_t req_id, uint32_t chunk, const char *tool,
                        uint32_t flags, const uint8_t *payload, size_t len,
                        const uint8_t *consent_token, uint64_t now_ms, uint8_t *buf,
                        size_t buf_cap, size_t *out_len)
{
    const char *what = "w4_env_seal";
    int32_t r = gate_agent(cap, ZXV_CAP_CRYPTO_SIGN, agent, what);
    if (r) return r;
    return seal(agent, kind, to, req_id, chunk, tool, flags, payload, len, consent_token, now_ms,
                buf, buf_cap, out_len, what);
}

int32_t zxv_w4_env_reply(const zxv_cap *cap, zxv_w4_agent *agent, const uint8_t *request,
                         size_t request_len, int32_t kind, uint32_t chunk, uint32_t flags,
                         const uint8_t *payload, size_t len, uint64_t now_ms, uint8_t *buf,
                         size_t buf_cap, size_t *out_len)
{
    const char *what = "w4_env_reply";
    int32_t r = gate_agent(cap, ZXV_CAP_CRYPTO_SIGN, agent, what);
    if (r) return r;
    if (kind != W4_MSG_RESPONSE && kind != W4_MSG_STREAM && kind != W4_MSG_ERROR)
        return zxvi_fail(ZXV_E_ARG, "%s: a reply is RESPONSE, STREAM or ERROR (got %d)", what,
                         (int) kind);
    w4_env_t *req;
    if ((r = env_parse(request, request_len, &req, NULL, what))) return r;
    if (req->kind != W4_MSG_REQUEST) {
        free(req);
        return zxvi_fail(ZXV_E_ARG, "%s: the envelope is not a REQUEST", what);
    }
    if (memcmp(req->to, agent->a.card.agent_id, W4_ID_LEN) != 0) {
        free(req);
        return zxvi_fail(ZXV_E_ARG, "%s: the request is not addressed to this agent", what);
    }
    uint8_t to[W4_ID_LEN];
    char tool[W4_TOOL_NAME_MAX];
    memcpy(to, req->from, W4_ID_LEN);
    memcpy(tool, req->tool, sizeof tool);
    uint64_t req_id = req->req_id;
    free(req);
    return seal(agent, kind, to, req_id, chunk, tool, flags, payload, len, NULL, now_ms, buf,
                buf_cap, out_len, what);
}

int32_t zxv_w4_env_decode(const uint8_t *env, size_t len, int32_t *out_kind, uint32_t *out_flags,
                          uint8_t out_from[32], uint8_t out_to[32], uint64_t *out_seq,
                          uint64_t *out_ts_ms, uint64_t *out_req_id, uint32_t *out_chunk,
                          char out_tool[32], uint32_t *out_payload_len)
{
    w4_env_t *e;
    int32_t r = env_parse(env, len, &e, NULL, "w4_env_decode");
    if (r) return r;
    if (out_kind) *out_kind = e->kind;
    if (out_flags) *out_flags = e->flags;
    if (out_from) memcpy(out_from, e->from, 32);
    if (out_to) memcpy(out_to, e->to, 32);
    if (out_seq) *out_seq = e->seq;
    if (out_ts_ms) *out_ts_ms = e->ts_ms;
    if (out_req_id) *out_req_id = e->req_id;
    if (out_chunk) *out_chunk = e->chunk;
    if (out_tool) memcpy(out_tool, e->tool, 32);
    if (out_payload_len) *out_payload_len = e->payload_len;
    free(e);
    return zxvi_ok();
}

int32_t zxv_w4_env_payload(const uint8_t *env, size_t len, uint8_t *buf, size_t cap,
                           size_t *out_len)
{
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "w4_env_payload: out_len is NULL");
    w4_env_t *e;
    int32_t r = env_parse(env, len, &e, NULL, "w4_env_payload");
    if (r) return r;
    size_t need;
    r = bytes_room(e->payload_len, buf, cap, &need, "w4_env_payload");
    *out_len = need;
    if (r == ZXV_OK) {
        if (e->payload_len) memcpy(buf, e->payload, e->payload_len);
        r = zxvi_ok();
    }
    free(e);
    return r;
}

int32_t zxv_w4_env_consent(const uint8_t *env, size_t len, uint8_t out_token[3453])
{
    if (!out_token) return zxvi_fail(ZXV_E_ARG, "w4_env_consent: out_token is NULL");
    w4_env_t *e;
    int32_t r = env_parse(env, len, &e, NULL, "w4_env_consent");
    if (r) return r;
    if (e->flags & W4_MF_CONSENT) {
        consent_put(&e->consent, out_token);
        r = zxvi_ok();
    } else {
        r = zxvi_fail(ZXV_E_NOT_FOUND, "w4_env_consent: no consent token attached");
    }
    free(e);
    return r;
}

int32_t zxv_w4_env_verify(const uint8_t *env, size_t len, const uint8_t sender_public_key[1952])
{
    const char *what = "w4_env_verify";
    if (!sender_public_key) return zxvi_fail(ZXV_E_ARG, "%s: sender_public_key is NULL", what);
    w4_env_t *e;
    uint32_t sl = 0;
    int32_t r = env_parse(env, len, &e, &sl, what);
    if (r) return r;
    uint8_t id[W4_ID_LEN];
    w4_key_id(sender_public_key, id);
    if (memcmp(id, e->from, W4_ID_LEN) != 0)
        r = zxvi_fail(ZXV_E_VERIFY, "%s: the key is not the envelope's sender", what);
    else if (!w4_verify(sender_public_key, W4_CTX_ENV, env, sl, e->sig))
        r = zxvi_fail(ZXV_E_VERIFY, "%s: signature does not verify", what);
    else
        r = zxvi_ok();
    free(e);
    return r;
}

/* ZXV_FEAT2_WEB4 once this group is live (rule R9). */
uint32_t zxvi_feat2_web4(void)
{
    return ZXV_FEAT2_WEB4;
}

#else /* ZXV_NO_PQC: the group is absent; every call is ZXV_E_NOTIMPL (R9). */

#define NI return zxvi_fail(ZXV_E_NOTIMPL, "web4 needs ML-DSA-65; built with ZXV_NO_PQC")
#define U(x) (void) (x)

int32_t zxv_w4_agent_create(const zxv_cap *c, const uint8_t *s, const char *n,
                            const zxv_w4_agent *nd, uint64_t t, zxv_w4_agent **o)
{ U(c); U(s); U(n); U(nd); U(t); U(o); NI; }
void zxv_w4_agent_destroy(zxv_w4_agent *a) { U(a); }
int32_t zxv_w4_agent_info(const zxv_w4_agent *a, uint8_t i[32], uint8_t p[32], uint8_t k[1952])
{ U(a); U(i); U(p); U(k); NI; }
int32_t zxv_w4_agent_card(const zxv_w4_agent *a, uint8_t *b, size_t c, size_t *o)
{ U(a); U(b); U(c); U(o); NI; }
int32_t zxv_w4_card_verify(const uint8_t *c, size_t l, uint8_t a[32], uint8_t p[32], char n[32],
                           uint64_t *t)
{ U(c); U(l); U(a); U(p); U(n); U(t); NI; }
int32_t zxv_w4_dir_create(uint32_t c, zxv_w4_dir **o) { U(c); U(o); NI; }
void zxv_w4_dir_destroy(zxv_w4_dir *d) { U(d); }
int32_t zxv_w4_dir_add(zxv_w4_dir *d, const uint8_t *c, size_t l) { U(d); U(c); U(l); NI; }
int32_t zxv_w4_dir_find(const zxv_w4_dir *d, const uint8_t a[32], uint8_t *b, size_t c, size_t *o)
{ U(d); U(a); U(b); U(c); U(o); NI; }
int32_t zxv_w4_dir_count(const zxv_w4_dir *d, uint32_t *o) { U(d); U(o); NI; }
int32_t zxv_w4_manifest_create(uint32_t v, uint64_t i, uint64_t e, zxv_w4_manifest **o)
{ U(v); U(i); U(e); U(o); NI; }
void zxv_w4_manifest_destroy(zxv_w4_manifest *m) { U(m); }
int32_t zxv_w4_manifest_add_tool(zxv_w4_manifest *m, const char *n, uint64_t p, uint32_t r,
                                 uint32_t b, uint32_t f, uint32_t x)
{ U(m); U(n); U(p); U(r); U(b); U(f); U(x); NI; }
int32_t zxv_w4_manifest_sign(const zxv_cap *c, const zxv_w4_agent *a, zxv_w4_manifest *m)
{ U(c); U(a); U(m); NI; }
int32_t zxv_w4_manifest_verify(const zxv_w4_manifest *m, const uint8_t k[1952], uint64_t t)
{ U(m); U(k); U(t); NI; }
int32_t zxv_w4_manifest_encode(const zxv_w4_manifest *m, uint8_t *b, size_t c, size_t *o)
{ U(m); U(b); U(c); U(o); NI; }
int32_t zxv_w4_manifest_decode(const uint8_t *b, size_t l, zxv_w4_manifest **o)
{ U(b); U(l); U(o); NI; }
int32_t zxv_w4_manifest_to_json(const zxv_w4_manifest *m, char *b, size_t c, size_t *o)
{ U(m); U(b); U(c); U(o); NI; }
int32_t zxv_w4_manifest_info(const zxv_w4_manifest *m, uint8_t a[32], uint8_t p[32], uint32_t *v,
                             uint64_t *i, uint64_t *e, uint32_t *n, int32_t *s)
{ U(m); U(a); U(p); U(v); U(i); U(e); U(n); U(s); NI; }
int32_t zxv_w4_manifest_tool(const zxv_w4_manifest *m, uint32_t i, char n[32], uint64_t *p,
                             uint32_t *r, uint32_t *b, uint32_t *f, uint32_t *x)
{ U(m); U(i); U(n); U(p); U(r); U(b); U(f); U(x); NI; }
int32_t zxv_w4_manifest_find(const zxv_w4_manifest *m, const char *n, uint32_t *o)
{ U(m); U(n); U(o); NI; }
int32_t zxv_w4_action_digest(const char *t, const uint8_t f[32], const uint8_t o[32], uint64_t r,
                             const uint8_t *p, size_t l, uint8_t d[32])
{ U(t); U(f); U(o); U(r); U(p); U(l); U(d); NI; }
int32_t zxv_w4_consent_issue(const zxv_cap *c, const zxv_w4_agent *h, const uint8_t a[32],
                             const uint8_t x[32], uint32_t s, uint64_t m, uint64_t i, uint64_t e,
                             const uint8_t *n, uint8_t o[3453])
{ U(c); U(h); U(a); U(x); U(s); U(m); U(i); U(e); U(n); U(o); NI; }
int32_t zxv_w4_consent_info(const uint8_t t[3453], uint8_t h[32], uint8_t a[32], uint8_t x[32],
                            uint32_t *s, uint64_t *m, uint64_t *i, uint64_t *e, uint8_t n[16])
{ U(t); U(h); U(a); U(x); U(s); U(m); U(i); U(e); U(n); NI; }
int32_t zxv_w4_consent_log_create(uint32_t c, zxv_w4_consent_log **o) { U(c); U(o); NI; }
void zxv_w4_consent_log_destroy(zxv_w4_consent_log *l) { U(l); }
int32_t zxv_w4_consent_check(zxv_w4_consent_log *l, const uint8_t *t, const uint8_t k[1952],
                             const uint8_t a[32], const uint8_t x[32], uint32_t n, uint64_t m,
                             uint64_t w)
{ U(l); U(t); U(k); U(a); U(x); U(n); U(m); U(w); NI; }
int32_t zxv_w4_consent_revoke(zxv_w4_consent_log *l, const uint8_t t[3453], uint64_t w)
{ U(l); U(t); U(w); NI; }
int32_t zxv_w4_consent_log_prune(zxv_w4_consent_log *l, uint64_t w, uint32_t *o)
{ U(l); U(w); U(o); NI; }
int32_t zxv_w4_env_seal(const zxv_cap *c, zxv_w4_agent *a, int32_t k, const uint8_t t[32],
                        uint64_t r, uint32_t h, const char *tl, uint32_t f, const uint8_t *p,
                        size_t l, const uint8_t *ct, uint64_t w, uint8_t *b, size_t bc, size_t *o)
{ U(c); U(a); U(k); U(t); U(r); U(h); U(tl); U(f); U(p); U(l); U(ct); U(w); U(b); U(bc); U(o); NI; }
int32_t zxv_w4_env_reply(const zxv_cap *c, zxv_w4_agent *a, const uint8_t *q, size_t ql,
                         int32_t k, uint32_t h, uint32_t f, const uint8_t *p, size_t l,
                         uint64_t w, uint8_t *b, size_t bc, size_t *o)
{ U(c); U(a); U(q); U(ql); U(k); U(h); U(f); U(p); U(l); U(w); U(b); U(bc); U(o); NI; }
int32_t zxv_w4_env_decode(const uint8_t *e, size_t l, int32_t *k, uint32_t *f, uint8_t fr[32],
                          uint8_t to[32], uint64_t *s, uint64_t *t, uint64_t *r, uint32_t *c,
                          char tl[32], uint32_t *pl)
{ U(e); U(l); U(k); U(f); U(fr); U(to); U(s); U(t); U(r); U(c); U(tl); U(pl); NI; }
int32_t zxv_w4_env_payload(const uint8_t *e, size_t l, uint8_t *b, size_t c, size_t *o)
{ U(e); U(l); U(b); U(c); U(o); NI; }
int32_t zxv_w4_env_consent(const uint8_t *e, size_t l, uint8_t o[3453]) { U(e); U(l); U(o); NI; }
int32_t zxv_w4_env_verify(const uint8_t *e, size_t l, const uint8_t k[1952])
{ U(e); U(l); U(k); NI; }

uint32_t zxvi_feat2_web4(void)
{
    return 0;
}

#endif /* ZXV_NO_PQC */
