/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hedgerow_model.c — Hedgerow (see hedgerow_model.h). */
#include <string.h>

#include "hedgerow_model.h"

#define NOT_SERVED (-32601) /* zxvd: no such method here (HM4) */

static const char *const MODE_WORD[3] = {"off", "lan", "online"};
static const char *const PATH_WORD[HEDGEROW_PATHS + 1] = {"",
                                                          "the network on the local network",
                                                          "the network online",
                                                          "answering other nodes",
                                                          "the phone link",
                                                          "hosted spaces",
                                                          "the IRC gateway"};

void hedgerow_init(hedgerow_t *h, zxm_link_t *link)
{
    memset(h, 0, sizeof *h);
    h->link = link;
    h->mode = HEDGEROW_OFF;
}

static void put(char *out, uint32_t cap, uint32_t *k, const char *s)
{
    for (; *s && *k + 1u < cap; s++) out[(*k)++] = *s;
    out[*k] = 0;
}

/* HM2: the ask that waits for the owner */
static void ask_notice(hedgerow_t *h, int64_t path)
{
    uint32_t k = 0;
    h->notice[0] = 0;
    put(h->notice, HEDGEROW_NOTICE_MAX, &k, "Hedgerow: opening ");
    put(h->notice, HEDGEROW_NOTICE_MAX, &k, PATH_WORD[path]);
    put(h->notice, HEDGEROW_NOTICE_MAX, &k,
        " waits for your word. Nothing opens until you confirm; closing is at once.");
}

static uint32_t bit(int64_t path)
{
    return 1u << (uint32_t) path;
}

/* HM2: asked, then granted on the owner's word; GR3 decides */
static int may_open(hedgerow_t *h, int64_t path, bool confirmed)
{
    h->asked |= bit(path);
    if (!confirmed) {
        ask_notice(h, path);
        return ZXM_E_STATE;
    }
    h->granted |= bit(path);
    h->revoked &= ~bit(path);
    if (!gatehouse_use_ok(1, (h->granted >> path) & 1u, (h->revoked >> path) & 1u))
        return ZXM_E_STATE;
    return ZXM_OK;
}

/* HM2: a close revokes the grant */
static void closed(hedgerow_t *h, int64_t path)
{
    h->granted &= ~bit(path);
    h->revoked |= bit(path);
}

static int read_clock(zxm_link_t *l, int64_t *ms)
{
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", ms) || *ms < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* HM2/HR6: file one change, checked before it is kept; the oldest goes
 * when the journal is full */
static void file_rule(hedgerow_t *h, int64_t path, int64_t open, int64_t port)
{
    int64_t seq = h->seq >= 999999999 ? 1 : h->seq + 1;
    int64_t stamp = h->now_ms / 1000;
    if (!hedgerow_rule_ok(seq, path, open, port, stamp)) return;
    if (h->nrules == HEDGEROW_RULES) {
        memmove(&h->rule[0], &h->rule[1], (HEDGEROW_RULES - 1u) * sizeof h->rule[0]);
        h->nrules--;
    }
    hedgerow_rule_t *r = &h->rule[h->nrules++];
    r->seq = seq;
    r->path = path;
    r->open = open;
    r->port = port;
    r->stamp = stamp;
    h->seq = seq;
}

/* a decimal string of 1..18 digits ("^[0-9]+$" in zxvd's replies) */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, uint64_t *v)
{
    char s[24];
    if (zxm_get_str(l, obj, key, s, sizeof s)) return ZXM_E_PROTOCOL;
    uint32_t n = (uint32_t) strlen(s);
    if (n < 1u || n > 18u) return ZXM_E_PROTOCOL;
    uint64_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return ZXM_E_PROTOCOL;
        x = x * 10u + (uint64_t) (s[i] - '0');
    }
    *v = x;
    return ZXM_OK;
}

static bool port_ok(int64_t p)
{
    return p >= 0 && p <= 65535;
}

static bool hex_id(const char *s)
{
    uint32_t n = 0;
    for (; s[n]; n++)
        if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f'))) return false;
    return n == HEDGEROW_ID_HEX;
}

static bool printable(const char *s, uint32_t max)
{
    uint32_t n = 0;
    for (; s[n]; n++)
        if (n >= max || s[n] < ' ' || s[n] > '~') return false;
    return n >= 1u;
}

static bool feature_name(const char *s)
{
    uint32_t n = 0;
    for (; s[n]; n++)
        if (n >= HEDGEROW_FEAT_MAX ||
            !((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= '0' && s[n] <= '9') || s[n] == '_'))
            return false;
    return n >= 1u;
}

/* one part of a refresh: ZXM_OK, NOT_SERVED (left unknown), or a failure */
static int call_part(zxm_link_t *l, const char *method)
{
    zxm_begin(l, method);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == NOT_SERVED) return NOT_SERVED;
    return rc;
}

static int read_serve(hedgerow_t *h, zxm_link_t *l, int32_t o)
{
    bool serving;
    int64_t port;
    uint64_t price;
    if (zxm_get_bool(l, o, "serving", &serving) || zxm_get_i64(l, o, "port", &port) ||
        !port_ok(port) || get_dec(l, o, "price_minor", &price) || price > HEDGEROW_PRICE_MAX)
        return ZXM_E_PROTOCOL;
    h->serve_known = true;
    h->serving = serving;
    h->serve_port = port;
    h->serve_price = price;
    return ZXM_OK;
}

static int read_phones(hedgerow_t *h, zxm_link_t *l)
{
    int32_t o = l->result, arr = zxm_get(l, o, "phones");
    bool on;
    int64_t port;
    if (zxm_get_bool(l, o, "on", &on) || zxm_get_i64(l, o, "port", &port) || !port_ok(port) ||
        arr < 0 || l->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t p = zxm_at(l, arr, i);
        hedgerow_phone_t ph;
        memset(&ph, 0, sizeof ph);
        if (p < 0 || zxm_get_str(l, p, "phone_id", ph.id, sizeof ph.id) || !hex_id(ph.id) ||
            zxm_get_str(l, p, "name", ph.name, sizeof ph.name) ||
            !printable(ph.name, HEDGEROW_NAME_MAX) ||
            zxm_get_bool(l, p, "allow_post", &ph.allow_post) ||
            zxm_get_i64(l, p, "connected", &ph.connected) || ph.connected < 0)
            return ZXM_E_PROTOCOL;
        if (kept < HEDGEROW_PHONES) h->phone[kept++] = ph;
    }
    h->phone_known = true;
    h->phone_on = on;
    h->phone_port = port;
    h->nphones = kept;
    return ZXM_OK;
}

static int read_features(hedgerow_t *h, zxm_link_t *l)
{
    int32_t arr = zxm_get(l, l->result, "features");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0, on = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t f = zxm_at(l, arr, i), g = f < 0 ? -1 : zxm_get(l, f, "groups");
        hedgerow_feature_t ft;
        memset(&ft, 0, sizeof ft);
        if (f < 0 || zxm_get_str(l, f, "name", ft.name, sizeof ft.name) || !feature_name(ft.name) ||
            zxm_get_bool(l, f, "enabled", &ft.enabled) ||
            zxm_get_bool(l, f, "regulated", &ft.regulated) || g < 0 || l->tok[g].type != W4_J_ARR)
            return ZXM_E_PROTOCOL;
        for (uint32_t j = 0; j < zxm_len(l, g); j++) {
            int64_t m;
            int32_t gj = zxm_at(l, g, j);
            if (gj < 0 || zxm_get_i64(l, gj, "methods", &m) || m < 1 || m > 65535)
                return ZXM_E_PROTOCOL;
            ft.methods += m; /* at most ZXM_TOK_MAX groups of 65535: no overflow */
        }
        on += ft.enabled;
        if (kept < HEDGEROW_FEATURES) h->feature[kept++] = ft;
    }
    h->features_known = true;
    h->nfeatures = kept;
    h->exposed = on;
    return ZXM_OK;
}

static int read_spaces(hedgerow_t *h, zxm_link_t *l)
{
    int32_t arr = zxm_get(l, l->result, "spaces");
    bool online;
    if (zxm_get_bool(l, l->result, "online", &online) || arr < 0 || l->tok[arr].type != W4_J_ARR ||
        zxm_len(l, arr) > 8u)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), in = 0;
    for (uint32_t i = 0; i < n; i++) {
        char phase[12];
        int32_t s = zxm_at(l, arr, i);
        if (s < 0 || zxm_get_str(l, s, "phase", phase, sizeof phase)) return ZXM_E_PROTOCOL;
        if (!strcmp(phase, "in"))
            in++;
        else if (strcmp(phase, "searching") && strcmp(phase, "joining") &&
                 strcmp(phase, "not_found"))
            return ZXM_E_PROTOCOL;
    }
    h->spaces_known = true;
    h->spaces_online = online;
    h->nspaces = n;
    h->spaces_in = in;
    return ZXM_OK;
}

static int read_irc(hedgerow_t *h, zxm_link_t *l)
{
    int32_t o = l->result;
    bool on, listening, available = true;
    int64_t port, clients;
    if (zxm_get(l, o, "available") >= 0 && zxm_get_bool(l, o, "available", &available))
        return ZXM_E_PROTOCOL;
    if (!available) {
        h->irc_known = false;
        return ZXM_OK;
    }
    if (zxm_get_bool(l, o, "on", &on) || zxm_get_bool(l, o, "listening", &listening) ||
        zxm_get_i64(l, o, "port", &port) || !port_ok(port) ||
        zxm_get_i64(l, o, "clients", &clients) || clients < 0)
        return ZXM_E_PROTOCOL;
    h->irc_known = true;
    h->irc_on = on;
    h->irc_listening = listening;
    h->irc_port = port;
    h->irc_clients = clients;
    return ZXM_OK;
}

static int read_peers(hedgerow_t *h, zxm_link_t *l)
{
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t far[3] = {0, 0, 0}, unscoped = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        char addr[32];
        int32_t p = zxm_at(l, arr, i);
        if (p < 0 || zxm_get_str(l, p, "addr", addr, sizeof addr)) return ZXM_E_PROTOCOL;
        int64_t f = addr[0] ? hedgerow_far_of_scope(hedgerow_scope_of(addr)) : HEDGEROW_REFUSED;
        if (f < 0)
            unscoped++;
        else
            far[f]++;
    }
    h->peers_known = true;
    memcpy(h->peers_far, far, sizeof far);
    h->peers_unscoped = unscoped;
    return ZXM_OK;
}

int hedgerow_refresh(hedgerow_t *h)
{
    if (!h) return ZXM_E_ARG;
    zxm_link_t *l = h->link;
    static hedgerow_t keep;
    keep = *h;
    int64_t now = 0;
    int rc = read_clock(l, &now);
    if (rc != ZXM_OK) return rc;
    static const char *const PART[6] = {"peer.status", "phone.list", "features.get",
                                        "pspace.list", "irc.status", "net.peers"};
    bool *known[6] = {&h->serve_known,  &h->phone_known, &h->features_known,
                      &h->spaces_known, &h->irc_known,   &h->peers_known};
    for (uint32_t i = 0; i < 6u; i++) {
        rc = call_part(l, PART[i]);
        if (rc == NOT_SERVED) {
            *known[i] = false;
            continue;
        }
        if (rc != ZXM_OK) goto failed;
        switch (i) {
        case 0:
            rc = read_serve(h, l, l->result);
            break;
        case 1:
            rc = read_phones(h, l);
            break;
        case 2:
            rc = read_features(h, l);
            break;
        case 3:
            rc = read_spaces(h, l);
            break;
        case 4:
            rc = read_irc(h, l);
            break;
        default:
            rc = read_peers(h, l);
            break;
        }
        if (rc != ZXM_OK) goto failed;
    }
    h->now_ms = now;
    return ZXM_OK;
failed:
    *h = keep; /* what was shown stays */
    return rc;
}

int hedgerow_set_mode(hedgerow_t *h, int64_t mode, bool confirmed)
{
    if (!h || mode < HEDGEROW_OFF || mode > HEDGEROW_ONLINE) return ZXM_E_ARG;
    int64_t was = h->mode;
    for (int64_t p = was + 1; p <= mode; p++) { /* each wider step is an opening */
        int rc = may_open(h, p == HEDGEROW_LAN ? HEDGEROW_P_LAN : HEDGEROW_P_ONLINE, confirmed);
        if (rc != ZXM_OK) return rc;
    }
    zxm_link_t *l = h->link;
    int64_t now = 0;
    int rc = read_clock(l, &now);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", MODE_WORD[mode]);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    char said[64];
    if (zxm_get_str(l, l->result, "result", said, sizeof said)) return ZXM_E_PROTOCOL;
    const char *want = mode == HEDGEROW_OFF ? "networking is off" : "networking is on";
    if (strcmp(said, want)) { /* zxvd's own words, e.g. "could not start" */
        memcpy(l->err, said, strlen(said) + 1u);
        return ZXM_E_REMOTE;
    }
    h->now_ms = now;
    h->mode = mode;
    for (int64_t p = HEDGEROW_P_LAN; p <= HEDGEROW_P_ONLINE; p++) {
        bool before = was >= p, after = mode >= p;
        if (before == after) continue;
        if (!after) closed(h, p);
        file_rule(h, p, after, 0);
    }
    if (mode < was) h->notice[0] = 0;
    return ZXM_OK;
}

int hedgerow_serve(hedgerow_t *h, bool on, uint64_t price_minor, bool confirmed)
{
    if (!h || (on && price_minor > HEDGEROW_PRICE_MAX)) return ZXM_E_ARG;
    if (on) {
        if (h->mode == HEDGEROW_OFF) return ZXM_E_STATE; /* peer.serve needs networking */
        int rc = may_open(h, HEDGEROW_P_SERVE, confirmed);
        if (rc != ZXM_OK) return rc;
    }
    zxm_link_t *l = h->link;
    int64_t now = 0;
    int rc = read_clock(l, &now);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "peer.serve");
    if (on) zxm_p_u64(l, "price_minor", price_minor);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    hedgerow_t was = *h;
    if ((rc = read_serve(h, l, l->result)) != ZXM_OK || h->serving != on) {
        *h = was;
        return rc != ZXM_OK ? rc : ZXM_E_PROTOCOL;
    }
    h->now_ms = now;
    if (!on) closed(h, HEDGEROW_P_SERVE);
    file_rule(h, HEDGEROW_P_SERVE, on, h->serve_port);
    return ZXM_OK;
}

int hedgerow_phone(hedgerow_t *h, bool on, int32_t port, const char *bind_ip, bool confirmed)
{
    if (!h || (on && (port < -1 || port > 65535))) return ZXM_E_ARG;
    if (on && bind_ip &&
        (strchr(bind_ip, ':') || hedgerow_scope_of(bind_ip) != HEDGEROW_SCOPE_PRIVATE))
        return ZXM_E_ARG; /* zxvd listens for phones on a private IPv4 address only */
    if (on) {
        int rc = may_open(h, HEDGEROW_P_PHONE, confirmed);
        if (rc != ZXM_OK) return rc;
    }
    zxm_link_t *l = h->link;
    int64_t now = 0;
    int rc = read_clock(l, &now);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "phone.link");
    zxm_p_bool(l, "on", on);
    if (on && port >= 0) zxm_p_u64(l, "port", (uint64_t) port);
    if (on && bind_ip) zxm_p_str(l, "bind", bind_ip);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    bool back;
    int64_t got;
    if (zxm_get_bool(l, l->result, "on", &back) || back != on ||
        zxm_get_i64(l, l->result, "port", &got) || !port_ok(got))
        return ZXM_E_PROTOCOL;
    h->now_ms = now;
    h->phone_known = true;
    h->phone_on = on;
    h->phone_port = got;
    if (!on) closed(h, HEDGEROW_P_PHONE);
    file_rule(h, HEDGEROW_P_PHONE, on, got);
    return ZXM_OK;
}

/* HM1: a path's switch, the mode that governs it (2 for a path the mesh
 * mode does not govern) and its bind */
bool hedgerow_reachable(const hedgerow_t *h, int64_t path, int64_t far)
{
    if (!h || path < 1 || path > HEDGEROW_PATHS) return false;
    int64_t sw = 0, mode = h->mode, bind = 2;
    switch (path) {
    case HEDGEROW_P_LAN:
        sw = h->mode >= HEDGEROW_LAN;
        mode = HEDGEROW_LAN;
        break;
    case HEDGEROW_P_ONLINE:
        sw = h->mode == HEDGEROW_ONLINE;
        break;
    case HEDGEROW_P_SERVE:
        sw = h->serve_known && h->serving;
        break;
    case HEDGEROW_P_PHONE:
        sw = h->phone_known && h->phone_on;
        mode = HEDGEROW_ONLINE;
        bind = 1;
        break;
    case HEDGEROW_P_SPACES:
        sw = h->spaces_known && h->spaces_online && h->spaces_in > 0;
        break;
    default:
        sw = h->irc_known && h->irc_on && h->irc_listening;
        mode = HEDGEROW_ONLINE;
        bind = 0;
        break;
    }
    return hedgerow_reach(sw, mode, bind, far);
}

uint32_t hedgerow_open_count(const hedgerow_t *h, int64_t far)
{
    uint32_t n = 0;
    for (int64_t p = 1; p <= HEDGEROW_PATHS; p++) n += hedgerow_reachable(h, p, far);
    return n;
}

/* a decimal number of 1..digits digits at *s, at most max; -1 when none */
static int64_t number_at(const char **s, uint32_t digits, int64_t max)
{
    int64_t v = 0;
    uint32_t n = 0;
    while (**s >= '0' && **s <= '9') {
        if (++n > digits) return -1;
        v = v * 10 + (**s - '0');
        (*s)++;
    }
    return n == 0 || v > max ? -1 : v;
}

int64_t hedgerow_scope_of(const char *addr)
{
    if (!addr) return HEDGEROW_REFUSED;
    const char *s = addr;
    int64_t o[4];
    for (uint32_t i = 0; i < 4u; i++) {
        if (i && *s++ != '.') return HEDGEROW_REFUSED;
        if ((o[i] = number_at(&s, 3, 255)) < 0) return HEDGEROW_REFUSED;
    }
    if (*s == ':') {
        s++;
        if (number_at(&s, 5, 65535) < 0) return HEDGEROW_REFUSED;
    }
    if (*s) return HEDGEROW_REFUSED;
    return hedgerow_addr_scope(4, o[0], o[1], o[2], o[3]);
}

int64_t hedgerow_scope6(const uint8_t octets[16])
{
    if (!octets) return HEDGEROW_REFUSED;
    bool zero10 = true, mid = false;
    for (uint32_t i = 0; i < 10u; i++) zero10 = zero10 && octets[i] == 0;
    if (zero10 && octets[10] == 0xff && octets[11] == 0xff) /* ::ffff:a.b.c.d */
        return hedgerow_addr_scope(4, octets[12], octets[13], octets[14], octets[15]);
    for (uint32_t i = 2; i < 14u; i++) mid = mid || octets[i] != 0;
    int64_t c = octets[14], d = octets[15];
    if (octets[0] == 0 && octets[1] == 0 && mid) c = d = 255;
    return hedgerow_addr_scope(6, octets[0], octets[1], c, d);
}
