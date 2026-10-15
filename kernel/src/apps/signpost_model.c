/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* signpost_model.c — Signpost, names and addresses (see signpost_model.h).
 * A card that verifies is advice for the owner, never trust by itself. */
#include <string.h>

#include "signpost_model.h"

/* ---- names (SN1) ------------------------------------------------------------ */

static bool name_byte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

int32_t signpost_name_check(const char *name, uint32_t n, signpost_scan_t *scan)
{
    signpost_scan_t sc = {SIGNPOST_BAD_LABEL, 0, 0, 0};
    if (name && n && name[n - 1u] == '.') n--; /* the root */
    sc.total = n;
    if (name && n) {
        uint32_t at = 0;
        sc.fault = SIGNPOST_FITS;
        for (;;) {
            uint32_t end = at, bad = 0;
            while (end < n && name[end] != '.') end++;
            for (uint32_t i = at; i < end; i++)
                bad += !name_byte(name[i]) || (name[i] == '-' && (i == at || i + 1u == end));
            int64_t f = signpost_name_fault(end - at, n, bad);
            sc.labels++;
            if (f != SIGNPOST_FITS) {
                sc.fault = (int32_t) f;
                sc.label = sc.labels - 1u;
                break;
            }
            if (end >= n) break;
            at = end + 1u;
            if (at == n) { /* a dot at the end of a name that had one already */
                sc.fault = SIGNPOST_BAD_LABEL;
                sc.label = sc.labels++;
                break;
            }
        }
    }
    if (scan) *scan = sc;
    return sc.fault;
}

/* ---- addresses (SN2) --------------------------------------------------------- */

/* A plain decimal of at most `most`, no leading zero, from s[*at]. */
static bool get_dec(const char *s, uint32_t n, uint32_t *at, uint32_t most, uint32_t *v)
{
    uint32_t i = *at, x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        x = x * 10u + (uint32_t) (s[i] - '0');
        if (x > most) return false;
        i++;
    }
    if (i == *at || (i - *at > 1u && s[*at] == '0')) return false;
    *v = x;
    *at = i;
    return true;
}

bool signpost_parse_ipv4(const char *s, uint32_t n, uint8_t octet[4], uint16_t *port)
{
    uint8_t o[4];
    uint32_t at = 0, v = 0, p = 0;
    if (!s || !octet) return false;
    for (uint32_t k = 0; k < 4u; k++) {
        if (k && (at >= n || s[at++] != '.')) return false;
        if (!get_dec(s, n, &at, 255u, &v)) return false;
        o[k] = (uint8_t) v;
    }
    if (at < n) {
        if (s[at++] != ':' || !get_dec(s, n, &at, 65535u, &p) || !p || at != n) return false;
    }
    memcpy(octet, o, 4);
    if (port) *port = (uint16_t) p;
    return true;
}

int64_t signpost_addr_number(const uint8_t octet[4])
{
    return ((int64_t) octet[0] << 24) | ((int64_t) octet[1] << 16) | ((int64_t) octet[2] << 8) |
           (int64_t) octet[3];
}

static int32_t class_of(const uint8_t o[4])
{
    return (int32_t) signpost_addr_class(o[0], o[1], o[2], o[3]);
}

void signpost_init(signpost_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

/* ---- content addresses: web4.cid ------------------------------------------------ */

static int read_cid(signpost_t *s)
{
    static signpost_cid_t c;
    zxm_link_t *l = s->link;
    memset(&c, 0, sizeof c);
    if (zxm_get_str(l, l->result, "cid", c.cid, sizeof c.cid) ||
        zxm_get_str(l, l->result, "uri", c.uri, sizeof c.uri) ||
        zxm_get_i64(l, l->result, "version", &c.version) ||
        zxm_get_i64(l, l->result, "codec", &c.codec))
        return ZXM_E_PROTOCOL;
    int32_t d = zxm_get(l, l->result, "bytes32_hex");
    if (d < 0) return ZXM_E_PROTOCOL;
    if (l->tok[d].type == W4_J_STR) {
        if (zxm_get_str(l, l->result, "bytes32_hex", c.digest, sizeof c.digest) ||
            strlen(c.digest) != SIGNPOST_ID_HEX)
            return ZXM_E_PROTOCOL;
        c.has_digest = true;
    } else if (l->tok[d].type != W4_J_NULL) {
        return ZXM_E_PROTOCOL;
    }
    s->cid = c;
    return ZXM_OK;
}

int signpost_cid(signpost_t *s, const char *cid)
{
    uint32_t n = cid ? (uint32_t) strlen(cid) : 0u;
    if (!n || n > SIGNPOST_CID_MAX) return ZXM_E_ARG;
    zxm_begin(s->link, "web4.cid");
    zxm_p_str(s->link, "cid", cid);
    int rc = zxm_call(s->link);
    return rc ? rc : read_cid(s);
}

int signpost_cid_of(signpost_t *s, const uint8_t *data, uint32_t n)
{
    static const char HEX[] = "0123456789abcdef";
    static char hex[2u * SIGNPOST_DATA_MAX + 1u];
    if ((!data && n) || n > SIGNPOST_DATA_MAX) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        hex[2u * i] = HEX[data[i] >> 4];
        hex[2u * i + 1u] = HEX[data[i] & 15u];
    }
    hex[2u * n] = 0;
    zxm_begin(s->link, "web4.cid");
    zxm_p_strn(s->link, "data_hex", hex, 2u * n);
    int rc = zxm_call(s->link);
    return rc ? rc : read_cid(s);
}

/* ---- agent cards: web4.card_verify ----------------------------------------------- */

static bool hex_of(const char *s, uint32_t len)
{
    if (!s || strlen(s) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
              (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    return true;
}

static bool card_name(const char *s)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (!n || n > SIGNPOST_CARD_NAME) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
              s[i] == '_' || s[i] == '-'))
            return false;
    return true;
}

int signpost_card_verify(signpost_t *s, const signpost_card_t *card)
{
    static signpost_verdict_t v;
    zxm_link_t *l = s->link;
    if (!card || !card_name(card->name) || !hex_of(card->pk_hex, SIGNPOST_PK_HEX) ||
        !hex_of(card->sig_hex, SIGNPOST_SIG_HEX) ||
        (card->agent_id && !hex_of(card->agent_id, SIGNPOST_ID_HEX)))
        return ZXM_E_ARG;
    zxm_begin(l, "web4.card_verify");
    zxm_p_str(l, "name", card->name);
    zxm_p_u64(l, "bound_ms", card->bound_ms);
    zxm_p_str(l, "pk_hex", card->pk_hex);
    zxm_p_str(l, "bind_sig_hex", card->sig_hex);
    if (card->agent_id) zxm_p_str(l, "agent_id", card->agent_id);
    int rc = zxm_call(l);
    if (rc) return rc;
    memset(&v, 0, sizeof v);
    if (zxm_get_bool(l, l->result, "valid", &v.valid)) return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "agent_id") >= 0 &&
        (zxm_get_str(l, l->result, "agent_id", v.agent_id, sizeof v.agent_id) ||
         strlen(v.agent_id) != SIGNPOST_ID_HEX))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "reason") >= 0 &&
        zxm_get_str(l, l->result, "reason", v.reason, sizeof v.reason))
        return ZXM_E_PROTOCOL;
    if (v.valid == !!v.reason[0]) return ZXM_E_PROTOCOL; /* a refusal says why, a pass does not */
    s->card = v;
    return ZXM_OK;
}

/* ---- peers: net.peers (SN5) --------------------------------------------------------- */

static bool node_id(const char *s)
{
    if (!s || strlen(s) != SIGNPOST_ID_HEX) return false;
    for (uint32_t i = 0; i < SIGNPOST_ID_HEX; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool digits_u64(const char *s, uint64_t *v)
{
    uint64_t x = 0;
    if (!s[0]) return false;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9' || x > (UINT64_MAX - (uint64_t) (*p - '0')) / 10u) return false;
        x = x * 10u + (uint64_t) (*p - '0');
    }
    *v = x;
    return true;
}

static int read_peer(const zxm_link_t *l, int32_t obj, signpost_peer_t *p)
{
    char addr[48], seen[24];
    memset(p, 0, sizeof *p);
    if (zxm_get_str(l, obj, "id", p->id, sizeof p->id) || !node_id(p->id) ||
        zxm_get_str(l, obj, "addr", addr, sizeof addr) ||
        zxm_get_i64(l, obj, "bucket", &p->bucket) ||
        zxm_get_str(l, obj, "last_seen_ms", seen, sizeof seen) ||
        !digits_u64(seen, &p->last_seen_ms))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, obj, "member") >= 0 && zxm_get_bool(l, obj, "member", &p->member))
        return ZXM_E_PROTOCOL;
    p->cls = SIGNPOST_REFUSED;
    if (addr[0]) {
        if (!signpost_parse_ipv4(addr, (uint32_t) strlen(addr), p->octet, &p->port) || !p->port)
            return ZXM_E_PROTOCOL;
        p->has_addr = true;
        p->cls = class_of(p->octet);
    }
    return ZXM_OK;
}

int signpost_peers(signpost_t *s)
{
    static signpost_peer_t next[SIGNPOST_PEERS];
    zxm_link_t *l = s->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr), n = total < SIGNPOST_PEERS ? total : SIGNPOST_PEERS;
    for (uint32_t i = 0; i < n; i++) {
        rc = read_peer(l, zxm_at(l, arr, i), &next[i]);
        if (rc) return rc;
    }
    memcpy(s->peer, next, (size_t) n * sizeof next[0]);
    s->npeers = n;
    s->peers_left_out = total - n;
    return ZXM_OK;
}

const signpost_peer_t *signpost_peer(const signpost_t *s, const char *id)
{
    if (!node_id(id)) return NULL;
    for (uint32_t i = 0; i < s->npeers; i++)
        if (!strcmp(s->peer[i].id, id)) return &s->peer[i];
    return NULL;
}

/* ---- the cache (SN3, SN4) ---------------------------------------------------------- */

/* name[0..n) as the cache keeps it: letters lower-cased, the root's dot
 * dropped; false when it is not a name (SN1). */
static bool cache_name(const char *name, uint32_t n, char out[SIGNPOST_NAME_MAX + 1])
{
    signpost_scan_t sc;
    if (!name || signpost_name_check(name, n, &sc) != SIGNPOST_FITS) return false;
    for (uint32_t i = 0; i < sc.total; i++) out[i] = lower(name[i]);
    out[sc.total] = 0;
    return true;
}

static int32_t find(const signpost_t *s, const char *key)
{
    for (uint32_t i = 0; i < s->ncache; i++)
        if (!strcmp(s->cache[i].name, key)) return (int32_t) i;
    return -1;
}

static void drop(signpost_t *s, uint32_t i)
{
    memmove(&s->cache[i], &s->cache[i + 1u], (size_t) (s->ncache - i - 1u) * sizeof s->cache[0]);
    s->ncache--;
}

/* SN3: where a new answer goes when the cache is full. */
static uint32_t victim(const signpost_t *s, int64_t now)
{
    uint32_t best = 0;
    int64_t least = INT64_MAX;
    for (uint32_t i = 0; i < s->ncache; i++) {
        int64_t left = signpost_ttl_left(s->cache[i].stored, s->cache[i].ttl, now);
        if (left < least) {
            least = left;
            best = i;
        }
    }
    return best;
}

int signpost_remember(signpost_t *s, const char *name, uint32_t n, const char *addr, int64_t ttl,
                      int64_t now)
{
    static signpost_answer_t a;
    uint16_t port = 1;
    memset(&a, 0, sizeof a);
    if (!cache_name(name, n, a.name) || !addr ||
        !signpost_parse_ipv4(addr, (uint32_t) strlen(addr), a.octet, &port) || port || ttl < 0 ||
        ttl > SIGNPOST_TTL_MAX || now < 0)
        return ZXM_E_ARG;
    a.cls = class_of(a.octet);
    a.stored = now;
    a.ttl = ttl;
    int32_t at = find(s, a.name);
    uint32_t slot = at >= 0                      ? (uint32_t) at
                    : s->ncache < SIGNPOST_CACHE ? s->ncache
                                                 : victim(s, now);
    if (!signpost_cache_fits((int64_t) slot + 1, a.cls, signpost_addr_number(a.octet), a.stored,
                             a.ttl))
        return ZXM_E_ARG;
    s->cache[slot] = a;
    if (slot == s->ncache) s->ncache++;
    return ZXM_OK;
}

int signpost_lookup(signpost_t *s, const char *name, uint32_t n, int64_t now,
                    signpost_answer_t *out, int64_t *left)
{
    char key[SIGNPOST_NAME_MAX + 1];
    if (!cache_name(name, n, key) || now < 0) return ZXM_E_ARG;
    int32_t at = find(s, key);
    if (at < 0) return ZXM_E_STATE;
    int64_t l = signpost_ttl_left(s->cache[at].stored, s->cache[at].ttl, now);
    if (l <= 0) {
        drop(s, (uint32_t) at);
        return ZXM_E_STATE;
    }
    if (out) *out = s->cache[at];
    if (left) *left = l;
    return ZXM_OK;
}

uint32_t signpost_sweep(signpost_t *s, int64_t now)
{
    uint32_t gone = 0;
    for (uint32_t i = s->ncache; i-- > 0;)
        if (signpost_ttl_left(s->cache[i].stored, s->cache[i].ttl, now) <= 0) {
            drop(s, i);
            gone++;
        }
    return gone;
}
