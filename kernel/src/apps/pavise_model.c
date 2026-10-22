/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pavise_model.c — Pavise's model (pavise_model.h). */
#include <string.h>

#include "pavise_model.h"

#define PAVISE_DAY      86400
#define PAVISE_ADDR_MAX 64u /* bytes of an address zxvd gives */
#define PAVISE_ALL_CAPS ((1u << GATEHOUSE_CAPS) - 1u)

/* ---- small text helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static void say(pavise_t *p, const char *a, int64_t n, const char *b)
{
    uint32_t at = 0;
    p->notice[0] = 0;
    if (put(p->notice, sizeof p->notice, &at, a) && n >= 0 &&
        put_i64(p->notice, sizeof p->notice, &at, n) && b)
        put(p->notice, sizeof p->notice, &at, b);
}

/* exactly n lowercase or uppercase hex digits */
static bool hex_of(const char *s, uint32_t n)
{
    if (!s || len_of(s, n + 1u) != n) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static bool same_hex(const char *a, const char *b)
{
    for (uint32_t i = 0; i < PAVISE_ID_HEX; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'F') x = (char) (x + 32);
        if (y >= 'A' && y <= 'F') y = (char) (y + 32);
        if (x != y) return false;
        if (!x) return true;
    }
    return a[PAVISE_ID_HEX] == b[PAVISE_ID_HEX];
}

/* ---- addresses (HR4, HR5) ---- */

/* a decimal octet of s[*at..), up to `end` (or the end of s when end is 0) */
static bool octet(const char *s, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        if (++digits > 3u) return false;
        x = x * 10 + (s[i++] - '0');
    }
    if (!digits || (end ? s[i] != end : s[i] != 0 && s[i] != ':')) return false;
    *v = x;
    *at = i + (end ? 1u : 0u);
    return true;
}

/* hedgerow.addr_scope of an address as zxvd writes it: dotted IPv4 (a port
 * after ':' allowed), or IPv6 text, scoped by its first group (HR4) */
static int64_t scope_of_text(const char *addr)
{
    uint32_t n = len_of(addr, PAVISE_ADDR_MAX + 1u);
    if (!addr || !n || n > PAVISE_ADDR_MAX) return HEDGEROW_REFUSED;
    bool colon = false, dot = false;
    for (uint32_t i = 0; i < n; i++) colon |= addr[i] == ':', dot |= addr[i] == '.';
    if (dot && (!colon || addr[0] != ':')) {
        int64_t a, b, c, d;
        uint32_t at = 0;
        if (!octet(addr, &at, '.', &a) || !octet(addr, &at, '.', &b) ||
            !octet(addr, &at, '.', &c) || !octet(addr, &at, 0, &d))
            return HEDGEROW_REFUSED;
        return hedgerow_addr_scope(4, a, b, c, d);
    }
    if (!colon) return HEDGEROW_REFUSED;
    if (addr[0] == ':' && addr[1] == ':') /* ::/16: only ::1 is loopback */
        return hedgerow_addr_scope(6, 0, 0, strcmp(addr, "::1") ? 255 : 0,
                                   strcmp(addr, "::1") ? 255 : 1);
    int64_t g = 0;
    uint32_t i = 0;
    for (; i < 4u && addr[i] != ':'; i++) {
        char c = addr[i];
        int dv = c >= '0' && c <= '9'   ? c - '0'
                 : c >= 'a' && c <= 'f' ? c - 'a' + 10
                 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                        : -1;
        if (dv < 0) return HEDGEROW_REFUSED;
        g = g * 16 + dv;
    }
    if (!i || addr[i] != ':') return HEDGEROW_REFUSED;
    return hedgerow_addr_scope(6, g >> 8, g & 255, 1, 1);
}

int64_t pavise_far_of(const char *addr)
{
    int64_t s = scope_of_text(addr);
    return s < 0 ? PAVISE_REFUSED : hedgerow_far_of_scope(s);
}

/* ---- PM1: a refresh ---- */

/* A part zxvd refuses (unserved, switched off, not linked) is unknown. */
static bool refused(int rc)
{
    return rc == ZXM_E_REMOTE;
}

static int read_features(zxm_link_t *l, pavise_t *n)
{
    zxm_begin(l, "features.get");
    int rc = zxm_call(l);
    if (refused(rc)) return ZXM_OK;
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "features");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > PAVISE_FEATURES)
        return ZXM_E_PROTOCOL;
    n->nfeatures = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        pavise_feature_t *f = &n->feature[n->nfeatures];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "name", f->name, sizeof f->name) || !f->name[0] ||
            zxm_get_bool(l, o, "enabled", &f->enabled))
            return ZXM_E_PROTOCOL;
        n->nfeatures++;
    }
    n->features_known = true;
    return ZXM_OK;
}

static int read_net(zxm_link_t *l, pavise_t *n)
{
    bool supported = false;
    zxm_begin(l, "sys.net");
    int rc = zxm_call(l);
    if (refused(rc)) return ZXM_OK;
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "supported", &supported)) return ZXM_E_PROTOCOL;
    if (!supported) return ZXM_OK; /* unknown on this platform */
    int32_t arr = zxm_get(l, l->result, "interfaces");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    n->public_ifaces = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        bool up = false, loop = false, pub = false;
        int32_t o = zxm_at(l, arr, i);
        int32_t ad = o < 0 ? -1 : zxm_get(l, o, "addresses");
        if (o < 0 || zxm_get_bool(l, o, "up", &up) || zxm_get_bool(l, o, "loopback", &loop) ||
            ad < 0 || l->tok[ad].type != W4_J_ARR)
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < zxm_len(l, ad) && up && !loop; k++) {
            char text[PAVISE_ADDR_MAX + 1];
            int32_t t = zxm_at(l, ad, k);
            if (t < 0 || w4_json_str(l->resp, &l->tok[t], text, sizeof text) < 0) continue;
            pub |= scope_of_text(text) == HEDGEROW_SCOPE_PUBLIC;
        }
        n->public_ifaces += pub;
    }
    n->net_known = true;
    return ZXM_OK;
}

static int read_peers(zxm_link_t *l, pavise_t *n)
{
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (refused(rc)) return ZXM_OK;
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    n->peers_far[0] = n->peers_far[1] = n->peers_far[2] = n->peers_unscoped = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        char addr[PAVISE_ADDR_MAX + 1];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "addr", addr, sizeof addr)) return ZXM_E_PROTOCOL;
        int64_t far = pavise_far_of(addr);
        if (far < 0)
            n->peers_unscoped++;
        else
            n->peers_far[far]++;
    }
    n->peers_known = true;
    return ZXM_OK;
}

static int read_devices(zxm_link_t *l, pavise_t *n)
{
    zxm_begin(l, "devmesh.devices");
    int rc = zxm_call(l);
    if (refused(rc)) return ZXM_OK;
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "devices");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > PAVISE_DEVICES)
        return ZXM_E_PROTOCOL;
    n->ndevices = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        pavise_device_t *d = &n->device[n->ndevices];
        char status[12], since[24];
        uint32_t k = 0;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "id", d->id, sizeof d->id) ||
            !hex_of(d->id, PAVISE_ID_HEX) || zxm_get_str(l, o, "status", status, sizeof status) ||
            zxm_get_bool(l, o, "self", &d->self) || zxm_get_str(l, o, "since", since, sizeof since))
            return ZXM_E_PROTOCOL;
        d->since = 0;
        for (; since[k] >= '0' && since[k] <= '9' && k < 11u; k++)
            d->since = d->since * 10 + (since[k] - '0');
        if (!k || since[k] || (strcmp(status, "active") && strcmp(status, "revoked")))
            return ZXM_E_PROTOCOL;
        d->active = !strcmp(status, "active");
        n->ndevices++;
    }
    n->devices_known = true;
    return ZXM_OK;
}

static int read_keys(zxm_link_t *l, pavise_t *n, const pavise_t *was)
{
    zxm_begin(l, "passkeys.list");
    int rc = zxm_call(l);
    if (refused(rc)) {
        n->keys_known = false;
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "credentials");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > PAVISE_KEYS)
        return ZXM_E_PROTOCOL;
    n->nkeys = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        pavise_key_t *k = &n->key[n->nkeys];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "cred_id", k->id, sizeof k->id) ||
            !hex_of(k->id, PAVISE_ID_HEX) || zxm_get_bool(l, o, "revoked", &k->revoked) ||
            zxm_get_i64(l, o, "registered", &k->registered) || k->registered < 0 ||
            k->registered > LOCKBOX_STAMP_MAX)
            return ZXM_E_PROTOCOL;
        k->life_days = PAVISE_LIFE_DAYS;
        for (uint32_t j = 0; j < was->nkeys; j++) /* the owner's lifetime stays */
            if (same_hex(was->key[j].id, k->id)) k->life_days = was->key[j].life_days;
        n->nkeys++;
    }
    /* an app's key follows its id across the refresh */
    for (uint32_t a = 0; a < n->napps; a++) {
        int32_t old = was->app[a].key;
        n->app[a].key = -1;
        for (uint32_t j = 0; old >= 0 && j < n->nkeys; j++)
            if (same_hex(was->key[old].id, n->key[j].id)) n->app[a].key = (int32_t) j;
    }
    n->keys_known = true;
    return ZXM_OK;
}

void pavise_init(pavise_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
}

int pavise_refresh(pavise_t *p)
{
    static pavise_t next;
    next = *p;
    int rc = read_features(p->link, &next);
    if (!rc) rc = read_net(p->link, &next);
    if (!rc) rc = read_peers(p->link, &next);
    if (!rc) rc = read_devices(p->link, &next);
    if (!rc) rc = read_keys(p->link, &next, p);
    if (rc) {
        say(p, "nothing changed: zxvd's answer could not be read", -1, 0);
        return rc;
    }
    *p = next;
    pavise_trust_tree(p);
    say(p, "", (int64_t) p->nkeys, p->keys_known ? " keys read" : " keys: passkeys are off");
    return ZXM_OK;
}

/* ---- PM2: apps, grants and keys ---- */

int32_t pavise_find_app(const pavise_t *p, int64_t number)
{
    for (uint32_t i = 0; i < p->napps; i++)
        if (p->app[i].number == number) return (int32_t) i;
    return -1;
}

static int32_t find_key(const pavise_t *p, const char *id)
{
    if (!hex_of(id, PAVISE_ID_HEX)) return -1;
    for (uint32_t i = 0; i < p->nkeys; i++)
        if (same_hex(p->key[i].id, id)) return (int32_t) i;
    return -1;
}

static bool cap_ok(int64_t cap)
{
    return cap >= 0 && cap <= PAVISE_CAP_MAX;
}

int pavise_add_app(pavise_t *p, int64_t number, uint32_t needs, int64_t signd, int64_t bind,
                   const char *feature)
{
    uint32_t fn = len_of(feature, PAVISE_FEAT_MAX + 1u);
    if (number < 1 || number > PAVISE_APP_MAX || (needs & ~PAVISE_ALL_CAPS) ||
        !gatehouse_flag(signd) || bind < 0 || bind > 2 || !feature || fn > PAVISE_FEAT_MAX ||
        pavise_find_app(p, number) >= 0)
        return ZXM_E_ARG;
    if (p->napps >= PAVISE_APPS) return ZXM_E_TOOBIG;
    pavise_app_t *a = &p->app[p->napps];
    memset(a, 0, sizeof *a);
    a->number = number;
    a->needs = needs;
    a->signd = signd;
    a->bind = bind;
    a->key = -1;
    memcpy(a->feature, feature, fn + 1u);
    p->napps++;
    return ZXM_OK;
}

int pavise_grant(pavise_t *p, int64_t app, int64_t cap, bool on, bool confirmed)
{
    int32_t i = pavise_find_app(p, app);
    if (i < 0 || !cap_ok(cap)) return ZXM_E_ARG;
    pavise_app_t *a = &p->app[i];
    uint32_t bit = 1u << cap;
    if (!on) { /* a revoke is at once */
        a->revoked |= bit;
        say(p, "revoked for app ", app, 0);
        return ZXM_OK;
    }
    if (!confirmed) { /* a grant waits for the owner's word */
        say(p, "grant asked for app ", app, ": confirm to grant");
        return ZXM_E_STATE;
    }
    a->granted |= bit;
    a->revoked &= ~bit;
    say(p, "granted for app ", app, 0);
    return ZXM_OK;
}

int pavise_bind_key(pavise_t *p, int64_t app, const char *key_id, bool confirmed)
{
    int32_t i = pavise_find_app(p, app), k = find_key(p, key_id);
    if (!p->keys_known) return ZXM_E_STATE;
    if (i < 0 || k < 0) return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE;
    p->app[i].key = k;
    return ZXM_OK;
}

int pavise_key_life(pavise_t *p, const char *key_id, int64_t days, bool confirmed)
{
    int32_t k = find_key(p, key_id);
    if (k < 0 || days < 1 || days > PAVISE_LIFE_MAX) return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE;
    p->key[k].life_days = days;
    return ZXM_OK;
}

int pavise_set_mode(pavise_t *p, int64_t mode)
{
    if (mode < 0 || mode > 2) return ZXM_E_ARG;
    p->mode = mode;
    return ZXM_OK;
}

/* ---- PM4: policies ---- */

int pavise_policy(pavise_t *p, int64_t source, int64_t app, int64_t cap, int64_t effect,
                  bool confirmed)
{
    if (!pavise_policy_ok(source, app, cap, effect)) return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE;
    for (uint32_t i = 0; i < p->npolicies; i++) {
        pavise_policy_t *r = &p->policy[i];
        if (r->source == source && r->app == app && r->cap == cap) {
            r->effect = effect;
            return ZXM_OK;
        }
    }
    if (p->npolicies >= PAVISE_POLICIES) return ZXM_E_TOOBIG;
    p->policy[p->npolicies++] = (pavise_policy_t){source, app, cap, effect};
    return ZXM_OK;
}

int pavise_rule(pavise_t *p, int64_t app, int64_t cap, int64_t ruling, bool confirmed)
{
    if (app < 1 || app > PAVISE_APP_MAX || !cap_ok(cap) || ruling < 0 || ruling > 2)
        return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE; /* the owner's word */
    uint32_t i = 0;
    while (i < p->nrulings && !(p->ruling[i].app == app && p->ruling[i].cap == cap)) i++;
    if (!ruling) { /* withdrawn */
        if (i < p->nrulings) p->ruling[i] = p->ruling[--p->nrulings];
        return ZXM_OK;
    }
    if (i == p->nrulings) {
        if (p->nrulings >= PAVISE_RULINGS) return ZXM_E_TOOBIG;
        p->nrulings++;
    }
    p->ruling[i] = (pavise_ruling_t){app, cap, ruling};
    return ZXM_OK;
}

lpres_state_t pavise_clause_now(const pavise_t *p, int64_t app, int64_t cap)
{
    lpres_state_t joined = LPRES_STATE_NEITHER;
    int64_t ruling = 0;
    for (uint32_t i = 0; i < p->npolicies; i++) {
        const pavise_policy_t *r = &p->policy[i];
        if (r->app == app && r->cap == cap)
            joined = lpres_kjoin(joined, pavise_said(r->effect == 1 ? 1 : 2));
    }
    for (uint32_t i = 0; i < p->nrulings; i++)
        if (p->ruling[i].app == app && p->ruling[i].cap == cap) ruling = p->ruling[i].ruling;
    return pavise_clause_of(joined, ruling);
}

/* ---- PM5: a decision ---- */

static int64_t gate_of(const pavise_app_t *a, int64_t cap)
{
    uint32_t bit = 1u << cap;
    return gatehouse_use_ok((a->needs & bit) != 0, (a->granted & bit) != 0, (a->revoked & bit) != 0)
               ? 1
               : 0;
}

/* the switch an app's network path rides on: 1 with none named, else
 * whether features.get says it is on; -1 while the switches are unknown */
static int64_t switch_of(const pavise_t *p, const pavise_app_t *a)
{
    if (!a->feature[0]) return 1;
    if (!p->features_known) return -1;
    for (uint32_t i = 0; i < p->nfeatures; i++)
        if (!strcmp(p->feature[i].name, a->feature)) return p->feature[i].enabled ? 1 : 0;
    return 0;
}

/* hedgerow.path_open for an asker `far` away; 1 for a capability that opens
 * no network path; -1 when its switch is unknown */
static int64_t reach_of(const pavise_t *p, const pavise_app_t *a, int64_t cap, int64_t far)
{
    if (cap != PAVISE_CAP_NET && cap != PAVISE_CAP_PEERS) return 1;
    int64_t sw = switch_of(p, a);
    if (sw < 0) return -1;
    return hedgerow_reach(sw, p->mode, a->bind, far) ? 1 : 0;
}

/* the key's flag and lockbox.days_left of its expiry */
static void key_of(const pavise_t *p, const pavise_app_t *a, int64_t now, int64_t *ok,
                   int64_t *days)
{
    *ok = 0;
    *days = 0;
    if (a->key < 0 || (uint32_t) a->key >= p->nkeys) return;
    const pavise_key_t *k = &p->key[a->key];
    int64_t left = lockbox_days_left(k->registered + k->life_days * PAVISE_DAY, now);
    *ok = k->revoked ? 0 : 1;
    *days = left < 0 ? 0 : left;
}

static int file_entry(pavise_t *p, int64_t app, int64_t cap, lpres_state_t verdict, int64_t now)
{
    int64_t next = tstk_next_number(p->last);
    if (next == TSTK_REFUSED) return ZXM_E_TOOBIG; /* never wrapped */
    pavise_entry_t e = {next, p->last, app, cap, (int64_t) verdict, now};
    if (!pavise_ledger_ok(e.decision, e.app, e.cap, e.verdict) ||
        !witness_linked(e.decision, e.prev, p->last) ||
        !witness_record_fits(e.decision, e.prev, 1, now))
        return ZXM_E_ARG;
    p->entry[p->head] = e;
    p->head = (p->head + 1u) % PAVISE_LEDGER;
    if (p->count < PAVISE_LEDGER) p->count++;
    p->last = next;
    return ZXM_OK;
}

int pavise_decide(pavise_t *p, int64_t app, int64_t cap, int64_t far, int64_t now,
                  lpres_state_t *verdict)
{
    int32_t i = pavise_find_app(p, app);
    if (i < 0 || !cap_ok(cap) || far < 0 || far > 2 || now < 0 || now > LOCKBOX_STAMP_MAX ||
        !verdict)
        return ZXM_E_ARG;
    const pavise_app_t *a = &p->app[i];
    int64_t gate = gate_of(a, cap), reach = reach_of(p, a, cap, far), keyok, keydays;
    key_of(p, a, now, &keyok, &keydays);
    lpres_state_t clause = pavise_clause_now(p, app, cap);
    /* a part not known is not known false: the others decide, then it
     * joins as NEITHER, so the verdict is never TRUE without it */
    lpres_state_t v = pavise_decision(gate, reach < 0 ? 1 : reach, p->keys_known ? keyok : 1,
                                      p->keys_known ? keydays : 1, clause);
    if (!p->keys_known || reach < 0) v = lpres_conjoin(v, LPRES_STATE_NEITHER);
    int rc = file_entry(p, app, cap, v, now);
    if (rc) return rc;
    *verdict = v;
    if (v == LPRES_STATE_TRUE)
        say(p, "allowed, decision ", p->last, 0);
    else if (v == LPRES_STATE_BOTH)
        say(p, "held: the policies disagree; denied until you rule. Decision ", p->last, 0);
    else if (v == LPRES_STATE_NEITHER)
        say(p, "denied: the keys or the switches are not known yet. Decision ", p->last, 0);
    else if (!gate)
        say(p, "denied: not granted. Decision ", p->last, 0);
    else if (!reach)
        say(p, "denied: the path is closed. Decision ", p->last, 0);
    else if (!keyok || keydays < 1)
        say(p, "denied: no valid key, or it expired. Decision ", p->last, 0);
    else
        say(p, "denied by a policy. Decision ", p->last, 0);
    return ZXM_OK;
}

/* ---- PM6: the ledger ---- */

const pavise_entry_t *pavise_entry(const pavise_t *p, uint32_t i)
{
    if (i >= p->count) return 0;
    return &p->entry[(p->head + PAVISE_LEDGER - 1u - i) % PAVISE_LEDGER];
}

bool pavise_ledger_intact(const pavise_t *p)
{
    for (uint32_t i = p->count; i-- > 0;) {
        const pavise_entry_t *e = pavise_entry(p, i);
        const pavise_entry_t *older = pavise_entry(p, i + 1u);
        int64_t last = older ? older->decision : e->prev; /* the oldest kept links to its prev */
        if (!witness_linked(e->decision, e->prev, last) ||
            !pavise_ledger_ok(e->decision, e->app, e->cap, e->verdict))
            return false;
    }
    return true;
}

/* ---- PM7: measures ---- */

int pavise_exposure_now(const pavise_t *p, int64_t now, int64_t *score)
{
    static int64_t paths[PAVISE_APPS * 2u], days[PAVISE_KEYS], signd[PAVISE_APPS];
    uint32_t np = 0, nk = 0;
    if (!score || now < 0 || now > LOCKBOX_STAMP_MAX) return ZXM_E_ARG;
    if (!p->keys_known) return ZXM_E_STATE;
    bool beyond = !p->net_known || p->public_ifaces > 0;
    for (uint32_t i = 0; i < p->napps; i++) {
        const pavise_app_t *a = &p->app[i];
        for (int64_t c = PAVISE_CAP_NET; c <= PAVISE_CAP_PEERS; c++)
            if (gate_of(a, c)) paths[np++] = beyond && reach_of(p, a, c, 2) == 1;
        signd[i] = a->signd;
    }
    for (uint32_t i = 0; i < p->nkeys; i++) {
        const pavise_key_t *k = &p->key[i];
        if (k->revoked) continue;
        int64_t left = lockbox_days_left(k->registered + k->life_days * PAVISE_DAY, now);
        days[nk++] = left < 0 ? 0 : left;
    }
    int64_t s = pavise_exposure(paths, np, days, nk, signd, p->napps);
    if (s < 0) return ZXM_E_ARG;
    *score = s;
    return ZXM_OK;
}

int64_t pavise_app_caps(const pavise_t *p, int64_t app)
{
    static int64_t num[PAVISE_APPS * GATEHOUSE_CAPS], allow[PAVISE_APPS * GATEHOUSE_CAPS];
    uint32_t n = 0;
    int32_t at = -1;
    for (uint32_t i = 0; i < p->napps; i++)
        for (int64_t c = 0; c <= PAVISE_CAP_MAX; c++) {
            if (p->app[i].number == app && at < 0) at = (int32_t) n;
            num[n] = p->app[i].number;
            allow[n++] = gate_of(&p->app[i], c);
        }
    return at < 0 ? PAVISE_REFUSED : pavise_app_total(num, allow, n, (uint32_t) at);
}

int64_t pavise_cap_holders(const pavise_t *p, int64_t cap)
{
    static int64_t held[PAVISE_APPS];
    if (!cap_ok(cap)) return PAVISE_REFUSED;
    for (uint32_t i = 0; i < p->napps; i++) held[i] = gate_of(&p->app[i], cap);
    return gatehouse_holders(held, p->napps);
}

uint32_t pavise_trust_tree(pavise_t *p)
{
    uint32_t trusted = 0;
    p->nnodes = 0;
    for (uint32_t d = 0; d < p->ndevices; d++)
        for (uint32_t i = 0; i < p->napps && p->nnodes < PAVISE_NODES; i++) {
            const pavise_app_t *a = &p->app[i];
            bool key = p->keys_known && a->key >= 0 && !p->key[a->key].revoked;
            pavise_node_t n = {(int64_t) d + 1, a->number, (int64_t) a->key + 1,
                               p->device[d].active && key ? 1 : 0, p->device[d].since};
            if (!pavise_node_ok(n.device, n.app, n.key, n.trust, n.since)) continue;
            p->node[p->nnodes++] = n;
            trusted += (uint32_t) n.trust;
        }
    return trusted;
}

/* ---- PM8: history ---- */

lpres_state_t pavise_history(pavise_t *p)
{
    zxm_link_t *l = p->link;
    bool on = false, ok = false;
    p->history = 0;
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "max", PAVISE_HISTORY);
    if (zxm_call(l) || zxm_get_bool(l, l->result, "on", &on)) return LPRES_STATE_NEITHER;
    if (!on) {
        say(p, "no journal: nothing to prove the history against", -1, 0);
        return LPRES_STATE_NEITHER;
    }
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return LPRES_STATE_NEITHER;
    bool fits = true;
    int64_t newer = 0;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        int64_t seq = 0, ms = 0;
        char level[8];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_i64(l, o, "seq", &seq) || zxm_get_i64(l, o, "time_ms", &ms) ||
            zxm_get_str(l, o, "level", level, sizeof level))
            return LPRES_STATE_NEITHER;
        int64_t kind = !strcmp(level, "info") ? 1 : !strcmp(level, "warn") ? 2 : 3;
        /* newest first: each entry older than the one before it */
        fits = fits && witness_record_fits(seq, 0, kind, ms) && (!newer || seq < newer);
        newer = seq;
    }
    p->history = (int64_t) n;
    zxm_begin(l, "journal.verify");
    if (zxm_call(l) || zxm_get_bool(l, l->result, "ok", &ok)) return LPRES_STATE_NEITHER;
    lpres_state_t s = lpres_conjoin(fits ? LPRES_STATE_TRUE : LPRES_STATE_FALSE,
                                    ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    say(p, "", (int64_t) n,
        s == LPRES_STATE_TRUE ? " journal entries, the chain intact"
                              : " journal entries: the chain or an entry is broken");
    return s;
}
