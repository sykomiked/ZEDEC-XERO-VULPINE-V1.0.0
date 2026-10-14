/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_web4.c — web4.*: the owner's Web4 envelope feed. Web4 is Chiglet
 * companions talking to each other (kernel/src/web4/web4_agent.h); this
 * group gives the owner's companion an ML-DSA-65 key and a feed of the
 * envelopes it signs, behind the "web4" feature switch (zxvd_ext.h X3; not
 * regulated: money-carrying envelopes only wait for consent here, nothing
 * is paid; off by default like every feature).
 *
 * THE FEED
 *   W1  web4.open makes the owner's companion: an ML-DSA-65 key pair from 32
 *       bytes of the OS (or "seed", for tests), self-bound (web4_agent.h A1,
 *       A2). Opening an open feed returns it unchanged ("new": false).
 *   W2  web4.post adds a REQUEST, RESPONSE or STREAM envelope addressed to
 *       another companion's agent_id. An envelope that carries personal data
 *       ("pii": true) or moves money ("amount_minor" > 0) is HELD: it is
 *       stored unsigned and nothing can read its bytes until the owner
 *       answers it. Any other envelope is sealed at once (w4_env_seal:
 *       sequence number, timestamp, ML-DSA-65 signature) and is "signed".
 *   W3  web4.consent answers a held envelope with a CONSENT entry, itself
 *       signed by the owner's key. grant: a consent token (w4_consent_issue,
 *       web4_agent.h K1) bound to the envelope's action digest, scope and
 *       amount is checked and recorded single-use (w4_consent_check), then
 *       attached and the envelope sealed. deny: the payload is wiped and the
 *       envelope is never signed. An answered envelope cannot be answered
 *       again. At most HELD_MAX envelopes wait at once.
 *   W4  web4.feed lists the feed newest first, paged by "before" (an id) and
 *       "limit", optionally one status. web4.envelope returns a signed
 *       entry's bytes as hex (what a carrier would deliver); a held or denied
 *       one is refused. web4.verify checks an entry, or envelope bytes from
 *       another companion against its public key. These three only read and
 *       answer while the feature is off.
 *   W5  In memory: FEED_MAX entries; when full the oldest entry that is not
 *       held makes room. A restart forgets the feed and makes a new key
 *       unless "seed" is given again.
 *   W6  Errors: -32602 bad params, ZXVD_E_WEB4 (-32090) a refused feed
 *       operation (the message says why).
 * Not here: delivery. No envelope is sent to or received from another node;
 * web4.envelope hands the bytes to whatever carries them.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "zxvd_groups.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "web4_agent.h"

#define ZXVD_E_WEB4  (-32090)
#define FEED_MAX     64u
#define HELD_MAX     32u
#define PAGE_DEF     20u
#define PAGE_MAX     50u
#define TTL_DEF      600u
#define TTL_MAX      86400u
#define NOW_MAX      253402300799ll /* 9999-12-31T23:59:59Z */
#define K_CONSENT    6u             /* a feed kind, not a w4 wire kind */
#define CTX_ANSWER   "web4/v1/consent-answer"
#define ANSWER_BODY  (4u + 1u + 1u + W4_ID_LEN + 8u + 8u + 4u + W4_HASH_LEN)
#define ANSWER_BYTES (ANSWER_BODY + W4_SIG_LEN)
#define LIT(b, s)    zxvd_buf_raw(b, s, sizeof s - 1)

enum { ST_SIGNED = 0, ST_HELD = 1, ST_DENIED = 2 };

typedef struct {
    uint32_t id;
    uint8_t kind, status;
    bool pii, grant;
    int64_t amount;
    uint32_t link; /* envelope: its CONSENT entry; CONSENT: the envelope answered */
    w4_env_t env;  /* CONSENT: from, to, tool, req_id of the envelope; own seq, ts */
    uint8_t action[W4_HASH_LEN];
    uint32_t nbytes;
    uint8_t bytes[W4_ENV_MAX];
} entry_t;

static struct {
    bool open;
    w4_agent_t me;
    w4_consent_used_t used[FEED_MAX];
    w4_consent_log_t log;
    entry_t feed[FEED_MAX];
    uint32_t n, next_id;
    w4_env_t draft;
    uint8_t buf[W4_ENV_MAX];
    uint8_t pk[W4_PK_LEN];
    char text[W4_MSG_MAX_PAYLOAD + 1];
} W;

static int (*RANDOM)(uint8_t *p, size_t n) = zxv_os_random;

void zxvd_web4_set_random(int (*fn)(uint8_t *p, size_t n))
{
    RANDOM = fn ? fn : zxv_os_random;
}

static const char *const KINDS[] = {"?",      "REQUEST", "RESPONSE", "STREAM",
                                    "CANCEL", "ERROR",   "CONSENT"};
static const char *const STATES[] = {"signed", "held", "denied"};

static bool p_now(zxvd_call_t *c, uint64_t *ms)
{
    int64_t now = (int64_t) time(NULL);
    if (zxvd_p_has(c, "now")) {
        if (!zxvd_p_i64(c, "now", &now)) return false;
        if (now < 0 || now > NOW_MAX) {
            zxvd_fail(c, ZXVD_E_PARAMS, "\"now\" must be unix seconds");
            return false;
        }
    }
    *ms = (uint64_t) now * 1000u;
    return true;
}

/* an optional u32 with a default and an inclusive range */
static bool p_u32_in(zxvd_call_t *c, const char *key, uint32_t def, uint32_t lo, uint32_t hi,
                     uint32_t *v)
{
    *v = def;
    if (!zxvd_p_has(c, key)) return true;
    if (!zxvd_p_u32(c, key, v)) return false;
    if (*v >= lo && *v <= hi) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u..%u", key, lo, hi);
    return false;
}

static bool p_hex_exact(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t len, bool required)
{
    uint32_t n;
    if (!zxvd_p_hex(c, key, out, len, &n, required)) return false;
    if (n == len || !zxvd_p_has(c, key)) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes of hex", key, len);
    return false;
}

static bool ready(zxvd_call_t *c)
{
    if (W.open) return true;
    zxvd_fail(c, ZXVD_E_WEB4, "the feed is not open: call web4.open first");
    return false;
}

static bool draw(zxvd_call_t *c, uint8_t *p, size_t n)
{
    if (RANDOM(p, n) == 0) return true;
    zxvd_fail(c, ZXVD_E_INTERNAL, "no random bytes");
    return false;
}

static uint32_t held_count(void)
{
    uint32_t h = 0;
    for (uint32_t i = 0; i < W.n; i++) h += W.feed[i].status == ST_HELD;
    return h;
}

static entry_t *find(uint32_t id)
{
    for (uint32_t i = 0; i < W.n; i++)
        if (W.feed[i].id == id) return &W.feed[i];
    return NULL;
}

/* W5: a fresh entry; when full, the oldest entry that is not held goes (at
 * most HELD_MAX of FEED_MAX are held, so there is always one) */
static entry_t *slot_new(void)
{
    if (W.n == FEED_MAX) {
        uint32_t i = 0;
        while (W.feed[i].status == ST_HELD) i++;
        memmove(&W.feed[i], &W.feed[i + 1], (W.n - 1 - i) * sizeof W.feed[0]);
        W.n--;
    }
    entry_t *e = &W.feed[W.n++];
    memset(e, 0, sizeof *e);
    e->id = W.next_id++;
    return e;
}

static void put_u64(zxvd_buf_t *b, const char *key, uint64_t v)
{
    zxvd_buf_printf(b, ",\"%s\":\"%llu\"", key, (unsigned long long) v);
}

static void put_entry(zxvd_buf_t *b, const entry_t *e)
{
    const w4_env_t *v = &e->env;
    zxvd_buf_printf(b, "{\"id\":%u,\"kind\":\"%s\",\"status\":\"%s\",\"from\":", (unsigned) e->id,
                    KINDS[e->kind], STATES[e->status]);
    zxvd_buf_hex(b, v->from, W4_ID_LEN);
    LIT(b, ",\"to\":");
    zxvd_buf_hex(b, v->to, W4_ID_LEN);
    LIT(b, ",\"tool\":");
    zxvd_buf_json_str(b, v->tool);
    put_u64(b, "req_id", v->req_id);
    memcpy(W.text, v->payload, v->payload_len);
    W.text[v->payload_len] = 0;
    zxvd_buf_printf(b, ",\"chunk\":%u,\"final\":%s,\"payload\":", (unsigned) v->chunk,
                    v->flags & W4_MF_FINAL ? "true" : "false");
    zxvd_buf_json_str(b, W.text);
    zxvd_buf_printf(b, ",\"pii\":%s,\"amount_minor\":\"%lld\"", e->pii ? "true" : "false",
                    (long long) e->amount);
    if (e->status == ST_SIGNED) {
        put_u64(b, "seq", v->seq);
        zxvd_buf_printf(b, ",\"ts\":%llu", (unsigned long long) (v->ts_ms / 1000u));
    } else {
        LIT(b, ",\"seq\":null,\"ts\":null");
    }
    if (e->kind == K_CONSENT)
        zxvd_buf_printf(b, ",\"consent\":null,\"answers\":%u,\"grant\":%s", (unsigned) e->link,
                        e->grant ? "true" : "false");
    else if (e->link)
        zxvd_buf_printf(b, ",\"consent\":%u,\"answers\":null,\"grant\":null", (unsigned) e->link);
    else
        LIT(b, ",\"consent\":null,\"answers\":null,\"grant\":null");
    zxvd_buf_printf(b, ",\"bytes\":%u}", (unsigned) e->nbytes);
}

static void put_state(zxvd_call_t *c, bool fresh)
{
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"open\":true,\"new\":%s,\"agent_id\":", fresh ? "true" : "false");
    zxvd_buf_hex(b, W.me.card.agent_id, W4_ID_LEN);
    LIT(b, ",\"name\":");
    zxvd_buf_json_str(b, W.me.card.name);
    LIT(b, ",\"pk\":");
    zxvd_buf_hex(b, W.me.card.pk, W4_PK_LEN);
    zxvd_buf_printf(b,
                    ",\"envelopes\":%u,\"held\":%u,\"max_envelopes\":%u,\"max_held\":%u,"
                    "\"in_memory\":true}",
                    (unsigned) W.n, (unsigned) held_count(), FEED_MAX, HELD_MAX);
}

/* W1 */
static void m_open(zxvd_call_t *c)
{
    char name[W4_NAME_MAX];
    uint8_t seed[32], rnd[32];
    uint64_t ms;
    if (!zxvd_p_str(c, "name", name, sizeof name, false) ||
        !p_hex_exact(c, "seed", seed, sizeof seed, false) || !p_now(c, &ms))
        return;
    if (W.open) return put_state(c, false);
    if (!name[0]) memcpy(name, "chiglet", 8);
    if ((!zxvd_p_has(c, "seed") && !draw(c, seed, sizeof seed)) || !draw(c, rnd, sizeof rnd))
        return;
    int r = w4_agent_create(&W.me, seed, name, NULL, NULL, ms, rnd);
    memset(seed, 0, sizeof seed);
    if (r != W4_OK)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"name\" must be 1..31 bytes of a-z 0-9 - _ .");
    w4_consent_log_init(&W.log, W.used, FEED_MAX);
    W.next_id = 1;
    W.open = true;
    put_state(c, true);
}

/* W2 */
static void m_post(zxvd_call_t *c)
{
    char kind[16], tool[W4_TOOL_NAME_MAX];
    uint8_t to[W4_ID_LEN], rnd[32];
    int64_t req = 0, amount = 0;
    uint32_t chunk;
    bool final, pii;
    uint64_t ms;
    if (!ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true) ||
        !p_hex_exact(c, "to", to, W4_ID_LEN, true) ||
        !zxvd_p_str(c, "tool", tool, sizeof tool, false) ||
        !zxvd_p_str(c, "payload", W.text, sizeof W.text, false) ||
        !p_u32_in(c, "chunk", 0, 0, UINT32_MAX, &chunk) ||
        !zxvd_p_bool(c, "final", false, &final) || !zxvd_p_bool(c, "pii", false, &pii) ||
        !p_now(c, &ms))
        return;
    uint8_t k = 0;
    for (uint8_t i = W4_MSG_REQUEST; i <= W4_MSG_STREAM; i++)
        if (!strcmp(kind, KINDS[i])) k = i;
    if (!strcmp(kind, "CONSENT"))
        return zxvd_fail(c, ZXVD_E_PARAMS,
                         "a CONSENT envelope answers a held one: use web4.consent");
    if (!k) return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be REQUEST, RESPONSE or STREAM");
    if (zxvd_p_has(c, "req_id")) {
        if (!zxvd_p_amount(c, "req_id", &req)) return;
        if (req <= 0) return zxvd_fail(c, ZXVD_E_PARAMS, "\"req_id\" must be a positive integer");
    } else if (k != W4_MSG_REQUEST) {
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"req_id\" names the request this %s answers", kind);
    } else {
        req = (int64_t) W.next_id;
    }
    if (zxvd_p_has(c, "amount_minor")) {
        if (!zxvd_p_amount(c, "amount_minor", &amount)) return;
        if (amount < 0) return zxvd_fail(c, ZXVD_E_PARAMS, "\"amount_minor\" must not be negative");
    }
    if (!tool[0]) memcpy(tool, "chat", 5);
    uint32_t len = (uint32_t) strlen(W.text);
    if (w4_env_make(&W.draft, k, W.me.card.agent_id, to, (uint64_t) req, chunk, tool,
                    (const uint8_t *) W.text, len) != W4_OK)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"tool\" must be 1..31 bytes of a-z 0-9 - _ .");
    if (final) W.draft.flags |= W4_MF_FINAL;
    bool hold = pii || amount > 0;
    if (hold && held_count() >= HELD_MAX)
        return zxvd_fail(c, ZXVD_E_WEB4,
                         "%u envelopes already wait for consent: answer one with web4.consent",
                         HELD_MAX);
    if (!hold && !draw(c, rnd, sizeof rnd)) return;
    entry_t *e = slot_new();
    e->kind = k;
    e->pii = pii;
    e->amount = amount;
    e->env = W.draft;
    w4_action_digest(tool, e->env.from, to, (uint64_t) req, e->env.payload, len, e->action);
    if (hold) {
        e->status = ST_HELD;
    } else {
        e->status = ST_SIGNED;
        e->nbytes = (uint32_t) w4_env_seal(&W.me, &e->env, ms, rnd, e->bytes, sizeof e->bytes);
    }
    put_entry(zxvd_result(c), e);
}

static uint32_t answer_body(const entry_t *a, uint8_t out[ANSWER_BODY])
{
    w4_w w;
    w4_w_init(&w, out, ANSWER_BODY);
    w4_w_strn(&w, "W4CA", 4);
    w4_w_byte(&w, 1);
    w4_w_byte(&w, a->grant);
    w4_w_bytes(&w, a->env.from, W4_ID_LEN);
    uint8_t t[8];
    w4_le64_put(t, a->env.seq);
    w4_w_bytes(&w, t, 8);
    w4_le64_put(t, a->env.ts_ms);
    w4_w_bytes(&w, t, 8);
    w4_le32_put(t, a->link);
    w4_w_bytes(&w, t, 4);
    w4_w_bytes(&w, a->action, W4_HASH_LEN);
    return w.len;
}

/* W3 */
static void m_consent(zxvd_call_t *c)
{
    uint32_t id, ttl;
    bool grant;
    uint64_t ms;
    uint8_t nonce[16], rc[32], re[32], ra[32];
    if (!ready(c) || !zxvd_p_u32(c, "id", &id)) return;
    if (!zxvd_p_has(c, "grant"))
        return zxvd_fail(c, ZXVD_E_PARAMS, "missing boolean parameter \"grant\"");
    if (!zxvd_p_bool(c, "grant", false, &grant) ||
        !p_u32_in(c, "expires_in", TTL_DEF, 1, TTL_MAX, &ttl) || !p_now(c, &ms))
        return;
    entry_t *e = find(id);
    if (!e) return zxvd_fail(c, ZXVD_E_WEB4, "no envelope %u in the feed", (unsigned) id);
    if (e->status != ST_HELD)
        return zxvd_fail(c, ZXVD_E_WEB4, "envelope %u is not waiting for consent", (unsigned) id);
    if (!draw(c, ra, sizeof ra) || (grant && (!draw(c, nonce, sizeof nonce) ||
                                              !draw(c, rc, sizeof rc) || !draw(c, re, sizeof re))))
        return;
    uint32_t scope = (e->pii ? W4_CONSENT_PII : 0u) | (e->amount > 0 ? W4_CONSENT_MONEY : 0u);
    w4_consent_t tok;
    if (grant) {
        w4_consent_issue(W.me.card.pk, W.me.sk, W.me.card.agent_id, e->action, scope,
                         (uint64_t) e->amount, ms, ms + (uint64_t) ttl * 1000u, nonce, rc, &tok);
        int r = w4_consent_check(&tok, W.me.card.pk, W.me.card.agent_id, e->action, scope,
                                 (uint64_t) e->amount, ms, &W.log);
        if (r == W4_ERR_SPACE)
            return zxvd_fail(c, ZXVD_E_WEB4,
                             "%u consents are still unexpired: try again when one expires",
                             FEED_MAX);
        if (r != W4_OK) return zxvd_fail(c, ZXVD_E_WEB4, "the consent token did not check (%d)", r);
    }
    entry_t *a = slot_new(); /* may move e (W5) */
    e = find(id);
    a->kind = K_CONSENT;
    a->status = ST_SIGNED;
    a->grant = grant;
    a->pii = e->pii;
    a->amount = e->amount;
    a->link = id;
    memcpy(a->action, e->action, W4_HASH_LEN);
    memcpy(a->env.from, W.me.card.agent_id, W4_ID_LEN);
    memcpy(a->env.to, e->env.to, W4_ID_LEN);
    memcpy(a->env.tool, e->env.tool, sizeof a->env.tool);
    a->env.req_id = e->env.req_id;
    a->env.seq = W.me.next_seq++;
    a->env.ts_ms = ms;
    a->nbytes = answer_body(a, a->bytes);
    w4_sign(W.me.sk, CTX_ANSWER, a->bytes, a->nbytes, ra, a->bytes + a->nbytes);
    a->nbytes += W4_SIG_LEN;
    e->link = a->id;
    if (grant) {
        e->env.consent = tok;
        e->env.flags |= W4_MF_CONSENT;
        e->status = ST_SIGNED;
        e->nbytes = (uint32_t) w4_env_seal(&W.me, &e->env, ms, re, e->bytes, sizeof e->bytes);
    } else {
        memset(e->env.payload, 0, sizeof e->env.payload);
        e->env.payload_len = 0;
        e->status = ST_DENIED;
    }
    zxvd_buf_t *b = zxvd_result(c);
    LIT(b, "{\"envelope\":");
    put_entry(b, e);
    LIT(b, ",\"consent\":");
    put_entry(b, a);
    LIT(b, "}");
}

/* W4 */
static void m_feed(zxvd_call_t *c)
{
    uint32_t before, limit;
    char st[8];
    if (!p_u32_in(c, "before", UINT32_MAX, 1, UINT32_MAX, &before) ||
        !p_u32_in(c, "limit", PAGE_DEF, 1, PAGE_MAX, &limit) ||
        !zxvd_p_str(c, "status", st, sizeof st, false))
        return;
    int want = -1;
    for (int i = 0; i < 3; i++)
        if (!strcmp(st, STATES[i])) want = i;
    if (st[0] && want < 0)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"status\" must be signed, held or denied");
    zxvd_buf_t *b = zxvd_result(c);
    if (!W.open) {
        LIT(b, "{\"open\":false,\"agent_id\":null,\"envelopes\":[],\"next\":null,\"total\":0,"
               "\"held\":0}");
        return;
    }
    LIT(b, "{\"open\":true,\"agent_id\":");
    zxvd_buf_hex(b, W.me.card.agent_id, W4_ID_LEN);
    LIT(b, ",\"envelopes\":[");
    uint32_t shown = 0, last = 0;
    bool more = false;
    for (uint32_t i = W.n; i-- > 0;) {
        const entry_t *e = &W.feed[i];
        if (e->id >= before || (want >= 0 && e->status != want)) continue;
        if (shown == limit) {
            more = true;
            break;
        }
        if (shown) LIT(b, ",");
        put_entry(b, e);
        last = e->id;
        shown++;
    }
    LIT(b, "],\"next\":");
    if (more)
        zxvd_buf_printf(b, "%u", (unsigned) last);
    else
        LIT(b, "null");
    zxvd_buf_printf(b, ",\"total\":%u,\"held\":%u}", (unsigned) W.n, (unsigned) held_count());
}

/* a signed entry by "id", or NULL after failing the call */
static const entry_t *signed_entry(zxvd_call_t *c)
{
    uint32_t id;
    if (!zxvd_p_u32(c, "id", &id)) return NULL;
    const entry_t *e = find(id);
    if (!e)
        zxvd_fail(c, ZXVD_E_WEB4, "no envelope %u in the feed", (unsigned) id);
    else if (e->status == ST_HELD)
        zxvd_fail(c, ZXVD_E_WEB4,
                  "envelope %u is held for consent: nothing leaves until the owner answers "
                  "(web4.consent)",
                  (unsigned) id);
    else if (e->status == ST_DENIED)
        zxvd_fail(c, ZXVD_E_WEB4, "envelope %u was denied: it was dropped and never signed",
                  (unsigned) id);
    else
        return e;
    return NULL;
}

static void m_envelope(zxvd_call_t *c)
{
    const entry_t *e = signed_entry(c);
    if (!e) return;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"id\":%u,\"kind\":\"%s\",\"bytes\":%u,\"envelope\":", (unsigned) e->id,
                    KINDS[e->kind], (unsigned) e->nbytes);
    zxvd_buf_hex(b, e->bytes, e->nbytes);
    LIT(b, "}");
}

static void invalid(zxvd_call_t *c, const char *why)
{
    zxvd_buf_t *b = zxvd_result(c);
    LIT(b, "{\"valid\":false,\"reason\":");
    zxvd_buf_json_str(b, why);
    LIT(b, "}");
}

static void verify_answer(zxvd_call_t *c, const uint8_t *p, uint32_t n, const uint8_t id[W4_ID_LEN])
{
    if (n != ANSWER_BYTES || p[4] != 1 || p[5] > 1) return invalid(c, "malformed");
    if (memcmp(p + 6, id, W4_ID_LEN)) return invalid(c, "the key is not the sender's");
    if (!w4_verify(W.pk, CTX_ANSWER, p, ANSWER_BODY, p + ANSWER_BODY))
        return invalid(c, "signature");
    const uint8_t *q = p + 6 + W4_ID_LEN;
    zxvd_buf_t *b = zxvd_result(c);
    LIT(b, "{\"valid\":true,\"kind\":\"CONSENT\",\"from\":");
    zxvd_buf_hex(b, id, W4_ID_LEN);
    put_u64(b, "seq", w4_le64_get(q));
    zxvd_buf_printf(b, ",\"ts\":%llu,\"answers\":%u,\"grant\":%s,\"action\":",
                    (unsigned long long) (w4_le64_get(q + 8) / 1000u),
                    (unsigned) w4_le32_get(q + 16), p[5] ? "true" : "false");
    zxvd_buf_hex(b, q + 20, W4_HASH_LEN);
    zxvd_buf_printf(b, ",\"bytes\":%u}", (unsigned) n);
}

/* W4: an envelope (or a CONSENT answer) against W.pk */
static void verify_bytes(zxvd_call_t *c, const uint8_t *p, uint32_t n)
{
    uint8_t id[W4_ID_LEN], act[W4_HASH_LEN];
    w4_key_id(W.pk, id);
    if (n >= 4 && !memcmp(p, "W4CA", 4)) return verify_answer(c, p, n, id);
    uint32_t sl;
    w4_env_t *v = &W.draft;
    if (w4_env_decode(p, n, v, &sl) != W4_OK) return invalid(c, "malformed");
    if (memcmp(v->from, id, W4_ID_LEN)) return invalid(c, "the key is not the sender's");
    if (!w4_verify(W.pk, W4_CTX_ENV, p, sl, v->sig)) return invalid(c, "signature");
    bool consent = v->flags & W4_MF_CONSENT;
    if (consent) {
        /* the token: signed by the same owner key, bound to this action; its
         * time window and single use are the receiver's to enforce */
        w4_consent_used_t one;
        w4_consent_log_t scratch;
        w4_consent_log_init(&scratch, &one, 1);
        w4_action_digest(v->tool, v->from, v->to, v->req_id, v->payload, v->payload_len, act);
        if (w4_consent_check(&v->consent, W.pk, v->from, act, v->consent.scope, v->consent.max_vfv,
                             v->consent.issued_ms, &scratch) != W4_OK)
            return invalid(c, "consent");
    }
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"valid\":true,\"kind\":\"%s\",\"from\":", KINDS[v->kind]);
    zxvd_buf_hex(b, v->from, W4_ID_LEN);
    LIT(b, ",\"to\":");
    zxvd_buf_hex(b, v->to, W4_ID_LEN);
    put_u64(b, "seq", v->seq);
    zxvd_buf_printf(b, ",\"ts\":%llu", (unsigned long long) (v->ts_ms / 1000u));
    put_u64(b, "req_id", v->req_id);
    zxvd_buf_printf(b, ",\"chunk\":%u,\"tool\":", (unsigned) v->chunk);
    zxvd_buf_json_str(b, v->tool);
    zxvd_buf_printf(b, ",\"final\":%s,\"payload_bytes\":%u,\"consent\":%s",
                    v->flags & W4_MF_FINAL ? "true" : "false", (unsigned) v->payload_len,
                    consent ? "true" : "false");
    if (consent)
        zxvd_buf_printf(b, ",\"consent_scope\":{\"pii\":%s,\"money\":%s},\"max_minor\":\"%llu\"",
                        v->consent.scope & W4_CONSENT_PII ? "true" : "false",
                        v->consent.scope & W4_CONSENT_MONEY ? "true" : "false",
                        (unsigned long long) v->consent.max_vfv);
    zxvd_buf_printf(b, ",\"bytes\":%u}", (unsigned) n);
}

static void m_verify(zxvd_call_t *c)
{
    bool by_id = zxvd_p_has(c, "id"), by_bytes = zxvd_p_has(c, "envelope");
    if (by_id == by_bytes)
        return zxvd_fail(c, ZXVD_E_PARAMS, "give \"id\" or \"envelope\", not both");
    if (by_id) {
        const entry_t *e = signed_entry(c);
        if (!e) return;
        memcpy(W.pk, W.me.card.pk, W4_PK_LEN);
        return verify_bytes(c, e->bytes, e->nbytes);
    }
    uint32_t n;
    if (!zxvd_p_hex(c, "envelope", W.buf, sizeof W.buf, &n, true) ||
        !p_hex_exact(c, "pk", W.pk, W4_PK_LEN, false))
        return;
    if (!zxvd_p_has(c, "pk")) {
        if (!W.open) return zxvd_fail(c, ZXVD_E_WEB4, "give the sender's \"pk\", or open the feed");
        memcpy(W.pk, W.me.card.pk, W4_PK_LEN);
    }
    verify_bytes(c, W.buf, n);
}

static void web4_close(void)
{
    memset(&W, 0, sizeof W); /* W5: in memory only; wipes the key */
}

static const zxvd_group_method_t METHODS[] = {
    {"web4.feed", m_feed, true},     {"web4.envelope", m_envelope, true},
    {"web4.verify", m_verify, true}, {"web4.open", m_open, false},
    {"web4.post", m_post, false},    {"web4.consent", m_consent, false},
};

static const zxvd_group_t GROUP = {
    "web4",
    false,
    "Web4 envelope feed (kernel/src/web4): the owner's companion signs envelopes with ML-DSA-65; "
    "personal data and money wait for consent; in memory, no network delivery",
    METHODS,
    sizeof METHODS / sizeof METHODS[0],
    web4_close};

const zxvd_group_t *zxvd_web4_group(void)
{
    return &GROUP;
}
