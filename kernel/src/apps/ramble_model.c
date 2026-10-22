/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ramble_model.c — Ramble, the Web4 viewer (ramble_model.h RM1-RM6). */
#include "ramble_model.h"

#include <stdio.h>
#include <string.h>

#include "reckoner_rules.h"

#define RAMBLE_BIN_LEN 36u /* a CIDv1 raw sha2-256: 0x01 0x55 0x12 0x20 + 32 bytes */

static const char *const KIND[] = {"REQUEST", "RESPONSE", "STREAM", "CONSENT"};
static const char *const STATUS[] = {"signed", "held", "denied"};

/* ---- reading answers ------------------------------------------------------ */

static int32_t code_of(const char *s, const char *const *names, int32_t n)
{
    for (int32_t i = 0; i < n; i++)
        if (!strcmp(s, names[i])) return i;
    return -1;
}

static bool is_null(const zxm_link_t *l, int32_t obj, const char *key)
{
    int32_t t = zxm_get(l, obj, key);
    return t >= 0 && l->tok[t].type == W4_J_NULL;
}

/* a signed decimal of at most 19 digits that fits int64 */
static bool dec_i64(const char *s, int64_t *v)
{
    bool neg = *s == '-';
    uint64_t u = 0;
    uint32_t n = 0;
    if (neg) s++;
    for (; *s; s++, n++) {
        if (*s < '0' || *s > '9' || n >= 19u) return false;
        u = u * 10u + (uint64_t) (*s - '0');
    }
    if (!n || u > (uint64_t) INT64_MAX) return false;
    *v = neg ? -(int64_t) u : (int64_t) u;
    return true;
}

static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t u = 0;
    if (zxm_get_u64(l, obj, key, &u) != ZXM_OK || u > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) u;
    return ZXM_OK;
}

/* a u32 that may be null (0) */
static int get_u32_or_null(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    *v = 0;
    if (is_null(l, obj, key)) return ZXM_OK;
    return get_u32(l, obj, key, v);
}

static bool hex_of(const char *s, uint32_t len)
{
    if (!s || strlen(s) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
              (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    return true;
}

static int32_t nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex[0..2n) into out[0..n); false on a digit that is no hex */
static bool unhex(const char *hex, uint32_t n, uint8_t *out)
{
    for (uint32_t i = 0; i < n; i++) {
        int32_t hi = nibble(hex[2u * i]), lo = nibble(hex[2u * i + 1u]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t) (hi << 4 | lo);
    }
    return true;
}

/* 1..31 of a-z 0-9 . _ - (web4's names) */
static bool name_ok(const char *s)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (n < 1u || n > RAMBLE_NAME_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
              s[i] == '_' || s[i] == '-'))
            return false;
    return true;
}

static bool now_ok(int64_t now)
{
    return now >= 0 && now <= RAMBLE_NOW_MAX;
}

/* ---- the feed -------------------------------------------------------------- */

void ramble_init(ramble_t *r, zxm_link_t *link)
{
    memset(r, 0, sizeof *r);
    r->link = link;
    r->verdict.scope = r->verdict.max_minor = -1;
    r->card.scope = r->card.max_minor = -1;
}

int ramble_open(ramble_t *r, const char *name, int64_t now)
{
    char id[RAMBLE_HEX_ID + 1], nm[RAMBLE_NAME_MAX + 1];
    bool open = false;
    if ((name && !name_ok(name)) || (now != -1 && !now_ok(now))) return ZXM_E_ARG;
    zxm_begin(r->link, "web4.open");
    if (name) zxm_p_str(r->link, "name", name);
    if (now >= 0) zxm_p_u64(r->link, "now", (uint64_t) now);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    if (zxm_get_bool(r->link, o, "open", &open) != ZXM_OK || !open ||
        zxm_get_str(r->link, o, "agent_id", id, sizeof id) != ZXM_OK ||
        !hex_of(id, RAMBLE_HEX_ID) || zxm_get_str(r->link, o, "name", nm, sizeof nm) != ZXM_OK ||
        !name_ok(nm))
        return ZXM_E_PROTOCOL;
    r->open = true;
    memcpy(r->agent_id, id, sizeof id);
    memcpy(r->name, nm, sizeof nm);
    return ZXM_OK;
}

/* one feed entry at token o into e; RR2 before it is taken */
static int read_entry(const zxm_link_t *l, int32_t o, ramble_env_t *e)
{
    char kind[16], status[16];
    bool grant = false;
    int64_t ts = 0;
    memset(e, 0, sizeof *e);
    if (o < 0 || get_u32(l, o, "id", &e->id) != ZXM_OK || e->id == 0 ||
        zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
        zxm_get_str(l, o, "status", status, sizeof status) != ZXM_OK ||
        zxm_get_str(l, o, "from", e->from, sizeof e->from) != ZXM_OK ||
        zxm_get_str(l, o, "to", e->to, sizeof e->to) != ZXM_OK ||
        zxm_get_str(l, o, "tool", e->tool, sizeof e->tool) != ZXM_OK ||
        zxm_get_bool(l, o, "final", &e->final) != ZXM_OK ||
        zxm_get_str(l, o, "payload", e->payload, sizeof e->payload) != ZXM_OK ||
        zxm_get_bool(l, o, "pii", &e->pii) != ZXM_OK ||
        get_dec(l, o, "amount_minor", &e->amount) != ZXM_OK ||
        get_u32_or_null(l, o, "consent", &e->consent) != ZXM_OK ||
        get_u32_or_null(l, o, "answers", &e->answers) != ZXM_OK ||
        get_u32(l, o, "bytes", &e->bytes) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t k = code_of(kind, KIND, 4), s = code_of(status, STATUS, 3);
    if (k < 0 || s < 0 || !hex_of(e->from, RAMBLE_HEX_ID) || !hex_of(e->to, RAMBLE_HEX_ID))
        return ZXM_E_PROTOCOL;
    e->kind = (uint8_t) k;
    e->status = (uint8_t) s;
    e->ts = -1;
    if (!is_null(l, o, "ts")) {
        if (zxm_get_i64(l, o, "ts", &ts) != ZXM_OK || !now_ok(ts)) return ZXM_E_PROTOCOL;
        e->ts = ts;
    }
    e->grant = -1;
    if (!is_null(l, o, "grant")) {
        if (zxm_get_bool(l, o, "grant", &grant) != ZXM_OK) return ZXM_E_PROTOCOL;
        e->grant = grant ? 1 : 0;
    }
    /* a held or denied entry is unsigned; a signed one has its time */
    if ((s == RAMBLE_SIGNED) != (e->ts >= 0)) return ZXM_E_PROTOCOL;
    if (!ramble_envelope_record_ok(e->id, k, s, e->amount, e->ts < 0 ? 0 : e->ts))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int ramble_feed(ramble_t *r, uint32_t before, uint32_t limit, int32_t status)
{
    static ramble_env_t page[RAMBLE_FEED_MAX];
    uint32_t next = 0, total = 0, held = 0;
    bool open = false;
    if (limit < 1u || limit > RAMBLE_FEED_MAX || status < -1 || status > RAMBLE_DENIED)
        return ZXM_E_ARG;
    zxm_begin(r->link, "web4.feed");
    if (before) zxm_p_u64(r->link, "before", before);
    zxm_p_u64(r->link, "limit", limit);
    if (status >= 0) zxm_p_str(r->link, "status", STATUS[status]);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = r->link;
    int32_t o = l->result;
    if (zxm_get_bool(l, o, "open", &open) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, o, "envelopes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > limit) return ZXM_E_TOOBIG;
    if (!open && n) return ZXM_E_PROTOCOL;
    if (get_u32_or_null(l, o, "next", &next) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (open &&
        (get_u32(l, o, "total", &total) != ZXM_OK || get_u32(l, o, "held", &held) != ZXM_OK))
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        if (read_entry(l, zxm_at(l, arr, i), &page[i]) != ZXM_OK) return ZXM_E_PROTOCOL;
        /* newest first, and only those asked for */
        if ((i && page[i].id >= page[i - 1u].id) || (before && page[i].id >= before) ||
            (status >= 0 && page[i].status != (uint8_t) status))
            return ZXM_E_PROTOCOL;
    }
    /* RM1: the whole page checked, now kept */
    memcpy(r->env, page, n * sizeof page[0]);
    r->n = n;
    r->next = next;
    r->total = total;
    r->held = held;
    r->open = open;
    return ZXM_OK;
}

int32_t ramble_find(const ramble_t *r, uint32_t id)
{
    for (uint32_t i = 0; i < r->n; i++)
        if (r->env[i].id == id) return (int32_t) i;
    return -1;
}

/* ---- RM2: an envelope's bytes and their check ------------------------------- */

int ramble_envelope(ramble_t *r, uint32_t id)
{
    static char hex[2u * RAMBLE_ENV_MAX + 1u];
    static uint8_t bin[RAMBLE_ENV_MAX];
    char kind[16];
    uint32_t got = 0, bytes = 0;
    int32_t at = ramble_find(r, id);
    if (at < 0) return ZXM_E_ARG;
    if (r->env[at].status != RAMBLE_SIGNED) return ZXM_E_STATE; /* nothing held leaves */
    zxm_begin(r->link, "web4.envelope");
    zxm_p_u64(r->link, "id", id);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    rc = zxm_get_str(r->link, o, "envelope", hex, sizeof hex);
    if (rc == ZXM_E_TOOBIG) return ZXM_E_TOOBIG;
    if (rc != ZXM_OK || get_u32(r->link, o, "id", &got) != ZXM_OK || got != id ||
        get_u32(r->link, o, "bytes", &bytes) != ZXM_OK ||
        zxm_get_str(r->link, o, "kind", kind, sizeof kind) != ZXM_OK ||
        code_of(kind, KIND, 4) != r->env[at].kind || bytes == 0 || bytes > RAMBLE_ENV_MAX ||
        strlen(hex) != 2u * bytes || !unhex(hex, bytes, bin))
        return ZXM_E_PROTOCOL;
    memcpy(r->raw, bin, bytes);
    r->raw_len = bytes;
    r->raw_id = id;
    return ZXM_OK;
}

int ramble_verify(ramble_t *r, uint32_t id)
{
    ramble_verdict_t v;
    bool pii = false, money = false;
    int32_t at = ramble_find(r, id);
    if (at < 0) return ZXM_E_ARG;
    if (r->env[at].status != RAMBLE_SIGNED) return ZXM_E_STATE;
    zxm_begin(r->link, "web4.verify");
    zxm_p_u64(r->link, "id", id);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = r->link;
    int32_t o = l->result;
    memset(&v, 0, sizeof v);
    v.scope = v.max_minor = -1;
    if (zxm_get_bool(l, o, "valid", &v.valid) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (!v.valid) {
        if (zxm_get_str(l, o, "reason", v.reason, sizeof v.reason) != ZXM_OK || !v.reason[0])
            return ZXM_E_PROTOCOL;
    } else {
        if (zxm_get_bool(l, o, "consent", &v.consent) != ZXM_OK) return ZXM_E_PROTOCOL;
        if (v.consent) {
            int32_t sc = zxm_get(l, o, "consent_scope");
            if (sc < 0 || zxm_get_bool(l, sc, "pii", &pii) != ZXM_OK ||
                zxm_get_bool(l, sc, "money", &money) != ZXM_OK ||
                get_dec(l, o, "max_minor", &v.max_minor) != ZXM_OK || v.max_minor < 0)
                return ZXM_E_PROTOCOL;
            v.scope = (pii ? RAMBLE_SCOPE_PII : 0) | (money ? RAMBLE_SCOPE_MONEY : 0);
        }
    }
    v.done = true;
    r->verdict = v;
    return ZXM_OK;
}

/* ---- RM3: consent, the owner's act ------------------------------------------ */

/* the grants still running at now */
static uint32_t live_grants(const ramble_t *r, int64_t now)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->ngrants; i++)
        if (r->grant[i].expires > now) n++;
    return n;
}

int ramble_consent(ramble_t *r, uint32_t id, bool grant, int64_t expires_in, int64_t now,
                   bool owner_confirmed)
{
    char status[16];
    uint32_t got = 0, cid = 0;
    int32_t at = ramble_find(r, id);
    if (at < 0 || !now_ok(now) || expires_in < 1 || expires_in > RAMBLE_GRANT_MAX) return ZXM_E_ARG;
    const ramble_env_t *e = &r->env[at];
    if (e->status != RAMBLE_HELD || e->kind == RAMBLE_CONSENT) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE; /* never by itself */
    int64_t need = ramble_need(e->pii, e->amount);
    if (grant) {
        /* the grant is the envelope's own scope and amount: it covers it (RR1) */
        if (!ramble_consent_covers(ramble_scope_gap(need, need), 0, expires_in)) return ZXM_E_ARG;
        if (live_grants(r, now) >= RAMBLE_GRANTS) return ZXM_E_TOOBIG;
    }
    zxm_begin(r->link, "web4.consent");
    zxm_p_u64(r->link, "id", id);
    zxm_p_bool(r->link, "grant", grant);
    zxm_p_i64(r->link, "expires_in", expires_in);
    zxm_p_u64(r->link, "now", (uint64_t) now);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = r->link;
    int32_t env = zxm_get(l, l->result, "envelope"), con = zxm_get(l, l->result, "consent");
    if (env < 0 || con < 0 || get_u32(l, env, "id", &got) != ZXM_OK || got != id ||
        zxm_get_str(l, env, "status", status, sizeof status) != ZXM_OK ||
        code_of(status, STATUS, 3) != (grant ? RAMBLE_SIGNED : RAMBLE_DENIED) ||
        get_u32(l, con, "id", &cid) != ZXM_OK || cid == 0 ||
        get_u32(l, con, "answers", &got) != ZXM_OK || got != id)
        return ZXM_E_PROTOCOL;
    if (grant) {
        /* the expired ones make room first */
        uint32_t k = 0;
        for (uint32_t i = 0; i < r->ngrants; i++)
            if (r->grant[i].expires > now) r->grant[k++] = r->grant[i];
        r->ngrants = k;
        r->grant[k] = (ramble_grant_t){id, need, e->amount, now + expires_in};
        r->ngrants++;
    }
    ramble_env_t *w = &r->env[at];
    w->status = grant ? RAMBLE_SIGNED : RAMBLE_DENIED;
    w->consent = cid;
    if (!grant) w->payload[0] = 0; /* zxvd wipes it */
    if (r->held) r->held--;
    return ZXM_OK;
}

bool ramble_covers(const ramble_t *r, uint32_t id, bool pii, int64_t amount, int64_t now)
{
    for (uint32_t i = r->ngrants; i-- > 0;) {
        const ramble_grant_t *g = &r->grant[i];
        if (g->id != id) continue;
        int64_t gap = ramble_scope_gap(ramble_need(pii, amount), g->scope);
        return ramble_consent_covers(gap, ramble_margin(g->ceiling, amount),
                                     ramble_margin(g->expires, now));
    }
    return false;
}

/* ---- RM4: content by CID ---------------------------------------------------- */

int ramble_cid_to_block(const char *cid, char out[RAMBLE_BLOCK_MAX + 1])
{
    static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t bin[RAMBLE_BIN_LEN], digit[64];
    uint32_t acc = 0, bits = 0, nb = 0, nd = 0, zeros = 0;
    out[0] = 0;
    if (!cid || strlen(cid) != SEEDBANK_CID_LEN || !seedbank_cid_ok(cid, SEEDBANK_CID_LEN))
        return ZXM_E_ARG;
    for (uint32_t i = 1; i < SEEDBANK_CID_LEN; i++) { /* base32, RFC 4648 lower case */
        char c = cid[i];
        acc = acc << 5 | (uint32_t) (c >= 'a' ? c - 'a' : c - '2' + 26);
        bits += 5u;
        if (bits >= 8u) {
            bits -= 8u;
            if (nb >= RAMBLE_BIN_LEN) return ZXM_E_ARG;
            bin[nb++] = (uint8_t) (acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    if (nb != RAMBLE_BIN_LEN || acc != 0 || bin[0] != 0x01 || bin[1] != 0x55 || bin[2] != 0x12 ||
        bin[3] != 0x20)
        return ZXM_E_ARG;
    while (zeros < nb && !bin[zeros]) zeros++;
    for (uint32_t i = zeros; i < nb; i++) { /* digits = digits * 256 + byte */
        uint32_t carry = bin[i];
        for (uint32_t j = 0; j < nd; j++) {
            carry += (uint32_t) digit[j] << 8;
            digit[j] = (uint8_t) (carry % 58u);
            carry /= 58u;
        }
        while (carry) {
            digit[nd++] = (uint8_t) (carry % 58u);
            carry /= 58u;
        }
    }
    if (1u + zeros + nd > RAMBLE_BLOCK_MAX) return ZXM_E_ARG;
    out[0] = 'z';
    for (uint32_t i = 0; i < zeros; i++) out[1 + i] = '1';
    for (uint32_t i = 0; i < nd; i++) out[1 + zeros + i] = B58[digit[nd - 1u - i]];
    out[1 + zeros + nd] = 0;
    return ZXM_OK;
}

int ramble_cid_of(ramble_t *r, const uint8_t *data, uint32_t len)
{
    static const char HEX[] = "0123456789abcdef";
    static char hex[2u * RAMBLE_DATA_MAX + 1u];
    char cid[SEEDBANK_CID_LEN + 1], uri[RAMBLE_URI_MAX + 1];
    if ((!data && len) || len > RAMBLE_DATA_MAX) return ZXM_E_ARG;
    for (uint32_t i = 0; i < len; i++) {
        hex[2u * i] = HEX[data[i] >> 4];
        hex[2u * i + 1u] = HEX[data[i] & 15u];
    }
    hex[2u * len] = 0;
    zxm_begin(r->link, "web4.cid");
    zxm_p_strn(r->link, "data_hex", hex, 2u * len);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    if (zxm_get_str(r->link, o, "cid", cid, sizeof cid) != ZXM_OK ||
        !seedbank_cid_ok(cid, (uint32_t) strlen(cid)) ||
        zxm_get_str(r->link, o, "uri", uri, sizeof uri) != ZXM_OK || strncmp(uri, "ipfs://", 7) ||
        strcmp(uri + 7, cid))
        return ZXM_E_PROTOCOL;
    memcpy(r->cid, cid, sizeof cid);
    memcpy(r->uri, uri, sizeof uri);
    return ZXM_OK;
}

int ramble_fetch(ramble_t *r, const char *cid)
{
    static char hex[2u * RAMBLE_GET_MAX + 1u];
    static uint8_t bin[RAMBLE_GET_MAX];
    char block[RAMBLE_BLOCK_MAX + 1], back[RAMBLE_BLOCK_MAX + 1];
    uint64_t bytes = 0;
    if (ramble_cid_to_block(cid, block) != ZXM_OK) return ZXM_E_ARG;
    zxm_begin(r->link, "aipi.get");
    zxm_p_str(r->link, "cid", block);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    if (zxm_get_str(r->link, o, "cid", back, sizeof back) != ZXM_OK || strcmp(back, block) ||
        zxm_get_u64(r->link, o, "bytes", &bytes) != ZXM_OK || bytes > RAMBLE_GET_MAX ||
        !seedbank_get_ok((int64_t) bytes) ||
        zxm_get_str(r->link, o, "hex", hex, sizeof hex) != ZXM_OK || strlen(hex) != 2u * bytes ||
        !unhex(hex, (uint32_t) bytes, bin))
        return ZXM_E_PROTOCOL;
    memcpy(r->got, bin, (size_t) bytes);
    r->got_len = (uint32_t) bytes;
    memcpy(r->got_cid, cid, SEEDBANK_CID_LEN + 1u);
    return ZXM_OK;
}

/* ---- agent cards ------------------------------------------------------------ */

int ramble_card_verify(ramble_t *r, const char *name, uint64_t bound_ms, const char *pk_hex,
                       const char *sig_hex, const char *agent_id)
{
    ramble_verdict_t v;
    if (!name_ok(name) || !hex_of(pk_hex, RAMBLE_PK_HEX) || !hex_of(sig_hex, RAMBLE_SIG_HEX) ||
        (agent_id && !hex_of(agent_id, RAMBLE_HEX_ID)))
        return ZXM_E_ARG;
    zxm_begin(r->link, "web4.card_verify");
    zxm_p_str(r->link, "name", name);
    zxm_p_u64(r->link, "bound_ms", bound_ms);
    zxm_p_str(r->link, "pk_hex", pk_hex);
    zxm_p_str(r->link, "bind_sig_hex", sig_hex);
    if (agent_id) zxm_p_str(r->link, "agent_id", agent_id);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = r->link;
    memset(&v, 0, sizeof v);
    v.scope = v.max_minor = -1;
    if (zxm_get_bool(l, l->result, "valid", &v.valid) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (v.valid) {
        if (zxm_get_str(l, l->result, "agent_id", v.agent_id, sizeof v.agent_id) != ZXM_OK ||
            !hex_of(v.agent_id, RAMBLE_HEX_ID))
            return ZXM_E_PROTOCOL;
    } else if (zxm_get_str(l, l->result, "reason", v.reason, sizeof v.reason) != ZXM_OK ||
               !v.reason[0]) {
        return ZXM_E_PROTOCOL;
    }
    v.done = true;
    r->card = v;
    return ZXM_OK;
}

/* ---- RM5: money advice ------------------------------------------------------ */

int ramble_money_after(ramble_t *r, uint32_t account, uint32_t id)
{
    int64_t d = 0, c = 0, q = 0;
    int32_t at = ramble_find(r, id);
    if (at < 0 || r->env[at].amount <= 0) return ZXM_E_ARG;
    zxm_begin(r->link, "ledger.balance");
    zxm_p_u64(r->link, "account", account);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    if (get_dec(r->link, o, "debit_minor", &d) != ZXM_OK ||
        get_dec(r->link, o, "credit_minor", &c) != ZXM_OK ||
        get_dec(r->link, o, "equity_minor", &q) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* equity is debit - credit, as the books keep it */
    if (q == INT64_MIN || ramble_margin(d, c) != q) return ZXM_E_PROTOCOL;
    int64_t after = reckoner_statement_close(q, 0, r->env[at].amount);
    if (after == RECKONER_REFUSED) return ZXM_E_TOOBIG;
    r->balance = q;
    r->after = after;
    return ZXM_OK;
}

/* ---- RM6: notices ----------------------------------------------------------- */

int ramble_notice(ramble_t *r, uint32_t id)
{
    int32_t at = ramble_find(r, id);
    if (at < 0) return ZXM_E_ARG;
    const ramble_env_t *e = &r->env[at];
    if (e->status != RAMBLE_HELD) return ZXM_E_STATE;
    int n;
    if (e->amount > 0)
        n = snprintf(r->notice, sizeof r->notice,
                     "Envelope %u (%s) waits for your consent: it moves %lld minor units%s. "
                     "Nothing leaves until you answer.",
                     (unsigned) e->id, e->tool, (long long) e->amount,
                     e->pii ? " and carries personal data" : "");
    else
        n = snprintf(r->notice, sizeof r->notice,
                     "Envelope %u (%s) waits for your consent: it carries personal data. "
                     "Nothing leaves until you answer.",
                     (unsigned) e->id, e->tool);
    return n > 0 && (uint32_t) n < sizeof r->notice ? ZXM_OK : ZXM_E_TOOBIG;
}
