/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* threshold_model.c — Threshold (see threshold_model.h). */
#include <string.h>

#include "threshold_model.h"

static const char *const ROLE[] = {"", "owner", "person", "guest"};
static const char *const LOCK[] = {"none", "pass", "passkey"};

void threshold_init(threshold_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

/* ---- small helpers: bounded text, no printf ---- */

static bool hex_of(const char *s, uint32_t chars)
{
    if (!s || strlen(s) != chars) return false;
    for (uint32_t i = 0; i < chars; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static bool hex_upto(const char *s, uint32_t max)
{
    size_t n = s ? strlen(s) : 0;
    return n >= 2 && n <= max && n % 2 == 0 && hex_of(s, (uint32_t) n);
}

static void put(char *out, uint32_t cap, uint32_t *o, const char *s)
{
    while (s && *s && *o + 1u < cap) out[(*o)++] = *s++;
    out[*o] = 0;
}

static void put_u32(char *out, uint32_t cap, uint32_t *o, uint32_t v)
{
    char d[11];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && n < sizeof d);
    while (n && *o + 1u < cap) out[(*o)++] = d[--n];
    out[*o] = 0;
}

static void say(threshold_t *t, const char *a, uint32_t num, const char *b)
{
    uint32_t o = 0;
    put(t->notice, sizeof t->notice, &o, a);
    if (b) {
        put_u32(t->notice, sizeof t->notice, &o, num);
        put(t->notice, sizeof t->notice, &o, b);
    }
}

static uint8_t named(const char *s, const char *const *names, uint8_t first, uint8_t n)
{
    for (uint8_t i = first; i < n; i++)
        if (!strcmp(s, names[i])) return i;
    return 0xffu;
}

/* TM2: a passphrase lives in one request only */
static int call_secret(zxm_link_t *l)
{
    int rc = zxm_call(l);
    memset(l->req, 0, sizeof l->req);
    l->req_len = 0;
    return rc;
}

static bool has_session(threshold_t *t)
{
    if (t->session[0]) return true;
    say(t, "Unlock a seat first.", 0, NULL);
    return false;
}

static void forget_session(threshold_t *t)
{
    memset(t->session, 0, sizeof t->session);
    memset(t->me, 0, sizeof t->me);
    t->my_role = 0;
}

/* ---- TM1: the roster ---- */

static int read_seat(zxm_link_t *l, int32_t o, uint32_t idx, threshold_seat_t *s)
{
    char role[8], lock[8];
    uint64_t color, wait;
    memset(s, 0, sizeof *s);
    if (o < 0 || zxm_get_str(l, o, "id", s->id, sizeof s->id) || !hex_of(s->id, THRESHOLD_ID) ||
        zxm_get_str(l, o, "name", s->name, sizeof s->name) || !s->name[0] ||
        zxm_get_str(l, o, "role", role, sizeof role) ||
        zxm_get_str(l, o, "lock", lock, sizeof lock) || zxm_get_u64(l, o, "color", &color) ||
        zxm_get_u64(l, o, "wait_ms", &wait) || color > UINT32_MAX || wait > THRESHOLD_WAIT_MAX)
        return ZXM_E_PROTOCOL;
    uint8_t r = named(role, ROLE, 1, 4), k = named(lock, LOCK, 0, 3);
    if (r == 0xffu || k == 0xffu ||
        !threshold_seat_fits(idx, r, k, (int64_t) color, (int64_t) wait))
        return ZXM_E_PROTOCOL;
    if (r == THRESHOLD_GUEST && k != THRESHOLD_LOCK_NONE) return ZXM_E_PROTOCOL; /* TH1 */
    s->role = r;
    s->lock = k;
    s->color = (uint32_t) color;
    s->wait_ms = (uint32_t) wait;
    return ZXM_OK;
}

int threshold_refresh(threshold_t *t)
{
    zxm_link_t *l = t->link;
    threshold_seat_t got[THRESHOLD_SHOWN];
    bool au;
    zxm_begin(l, "seats.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "seats");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_bool(l, l->result, "auto_enter", &au))
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), shown = n < THRESHOLD_SHOWN ? n : THRESHOLD_SHOWN;
    if (n > THRESHOLD_SEATS_MAX) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < shown; i++)
        if (read_seat(l, zxm_at(l, arr, i), i, &got[i])) return ZXM_E_PROTOCOL;
    char auto_seat[THRESHOLD_ID + 1] = "";
    zxm_begin(l, "seats.auto");
    rc = zxm_call(l);
    if (rc) return rc;
    int32_t a = zxm_get(l, l->result, "seat");
    if (a < 0) return ZXM_E_PROTOCOL;
    if (l->tok[a].type != W4_J_NULL &&
        (zxm_get_str(l, l->result, "seat", auto_seat, sizeof auto_seat) ||
         !hex_of(auto_seat, THRESHOLD_ID)))
        return ZXM_E_PROTOCOL;
    /* everything checked: commit */
    memcpy(t->seat, got, shown * sizeof got[0]);
    memset(t->seat + shown, 0, (THRESHOLD_SHOWN - shown) * sizeof got[0]);
    t->nseats = shown;
    t->total = n;
    t->full = false; /* SEAT_ERR_FULL belongs to the add it answered */
    t->auto_enter = au;
    memcpy(t->auto_seat, auto_seat, sizeof auto_seat);
    if (t->me[0]) {
        const threshold_seat_t *me = threshold_seat(t, t->me);
        if (me)
            t->my_role = me->role;
        else if (n <= THRESHOLD_SHOWN)
            forget_session(t); /* the acting seat is gone */
    }
    return ZXM_OK;
}

const threshold_seat_t *threshold_seat(const threshold_t *t, const char *id)
{
    for (uint32_t i = 0; id && i < t->nseats; i++)
        if (!strcmp(t->seat[i].id, id)) return &t->seat[i];
    return NULL;
}

bool threshold_skips_login(const threshold_t *t)
{
    int64_t lock = t->nseats ? t->seat[0].lock : -1;
    return threshold_skip(t->total, lock, t->auto_enter ? 1 : 0);
}

/* ---- seats ---- */

int threshold_add(threshold_t *t, const char *name, uint8_t role, uint8_t lock, const char *pass,
                  uint32_t color)
{
    zxm_link_t *l = t->link;
    size_t nl = name ? strlen(name) : 0;
    if (nl < 1 || nl > THRESHOLD_NAME || role < THRESHOLD_OWNER || role > THRESHOLD_GUEST ||
        lock > THRESHOLD_LOCK_PASSKEY || (pass && lock != THRESHOLD_LOCK_PASS))
        return ZXM_E_ARG;
    if (lock != THRESHOLD_LOCK_NONE) {
        /* TM4, TH1: the first seat is the owner's own; after it, the acting seat */
        uint8_t actor = t->session[0] ? t->my_role : THRESHOLD_OWNER;
        if (!threshold_rights(actor, 0, role)) {
            say(t,
                role == THRESHOLD_GUEST ? "A guest never has a lock."
                                        : "Only an owner may give another seat a lock.",
                0, NULL);
            return ZXM_E_STATE;
        }
    }
    if (lock == THRESHOLD_LOCK_PASS && (!pass || !threshold_phrase_ok((int64_t) strlen(pass)))) {
        say(t, "A passphrase is 12 to 256 bytes long.", 0, NULL);
        return ZXM_E_ARG;
    }
    zxm_begin(l, "seats.add");
    zxm_p_str(l, "name", name);
    zxm_p_str(l, "role", ROLE[role]);
    zxm_p_str(l, "lock", LOCK[lock]);
    if (pass) zxm_p_str(l, "pass", pass);
    zxm_p_u64(l, "color", color);
    if (t->session[0]) zxm_p_str(l, "session", t->session);
    int rc = call_secret(l);
    if (rc) {
        t->full = rc == ZXM_E_REMOTE && !strncmp(l->err, "SEAT_ERR_FULL", 13);
        if (t->full) say(t, "This device holds as many people as zxvd allows.", 0, NULL);
        return rc;
    }
    t->notice[0] = 0;
    return threshold_refresh(t);
}

int threshold_remove(threshold_t *t, const char *id)
{
    zxm_link_t *l = t->link;
    if (!hex_of(id, THRESHOLD_ID)) return ZXM_E_ARG;
    if (!has_session(t)) return ZXM_E_STATE;
    zxm_begin(l, "seats.remove");
    zxm_p_str(l, "id", id);
    zxm_p_str(l, "session", t->session);
    int rc = zxm_call(l);
    if (rc) return rc;
    char gone[THRESHOLD_ID + 1];
    if (zxm_get_str(l, l->result, "removed", gone, sizeof gone) || strcmp(gone, id))
        return ZXM_E_PROTOCOL;
    if (!strcmp(gone, t->me)) forget_session(t); /* zxvd ended it with the seat */
    return threshold_refresh(t);
}

int threshold_unlock(threshold_t *t, const char *id, const char *pass, bool passkey_ok)
{
    zxm_link_t *l = t->link;
    if (!hex_of(id, THRESHOLD_ID) || (pass && strlen(pass) > THRESHOLD_PASS_MAX)) return ZXM_E_ARG;
    bool pk = passkey_ok || t->passkey_ok;
    zxm_begin(l, "seats.unlock");
    zxm_p_str(l, "id", id);
    if (pass) zxm_p_str(l, "pass", pass);
    if (pk) zxm_p_bool(l, "passkey_ok", true);
    int rc = call_secret(l);
    t->passkey_ok = false; /* one verdict, one try */
    if (rc) return rc;
    bool ok;
    uint64_t wait = 0;
    if (zxm_get_bool(l, l->result, "ok", &ok) || zxm_get_u64(l, l->result, "wait_ms", &wait) ||
        wait > THRESHOLD_WAIT_MAX)
        return ZXM_E_PROTOCOL;
    if (ok) {
        char sess[THRESHOLD_TOKEN + 1], who[THRESHOLD_ID + 1];
        if (zxm_get_str(l, l->result, "session", sess, sizeof sess) ||
            !hex_of(sess, THRESHOLD_TOKEN) || zxm_get_str(l, l->result, "id", who, sizeof who) ||
            strcmp(who, id))
            return ZXM_E_PROTOCOL;
        memcpy(t->session, sess, sizeof sess);
        memcpy(t->me, who, sizeof who);
        t->login = THRESHOLD_LOGIN_OK;
        t->wait_ms = 0;
        t->notice[0] = 0;
        return threshold_refresh(t);
    }
    char st[16];
    if (zxm_get_str(l, l->result, "status", st, sizeof st)) return ZXM_E_PROTOCOL;
    if (!strcmp(st, "SEAT_ERR_WRONG"))
        t->login = THRESHOLD_LOGIN_WRONG;
    else if (!strcmp(st, "SEAT_ERR_WAIT"))
        t->login = THRESHOLD_LOGIN_WAIT;
    else
        return ZXM_E_PROTOCOL;
    t->wait_ms = (uint32_t) wait;
    /* TM3: shown, never tried again here */
    bool wrong = t->login == THRESHOLD_LOGIN_WRONG;
    if (wait)
        say(t, wrong ? "That was not right. Try again in " : "Please wait. Try again in ",
            (uint32_t) ((wait + 999u) / 1000u), " s.");
    else
        say(t, wrong ? "That was not right." : "Please wait.", 0, NULL);
    for (uint32_t i = 0; i < t->nseats; i++)
        if (!strcmp(t->seat[i].id, id)) t->seat[i].wait_ms = (uint32_t) wait;
    return ZXM_OK;
}

int threshold_lock(threshold_t *t)
{
    zxm_link_t *l = t->link;
    if (!has_session(t)) return ZXM_E_STATE;
    zxm_begin(l, "seats.lock");
    zxm_p_str(l, "session", t->session);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool ended;
    if (zxm_get_bool(l, l->result, "ended", &ended)) return ZXM_E_PROTOCOL;
    forget_session(t); /* ended now, or already gone */
    t->login = THRESHOLD_LOGIN_NONE;
    return ZXM_OK;
}

int threshold_set_lock(threshold_t *t, const char *id, uint8_t lock, const char *old_pass,
                       const char *pass)
{
    zxm_link_t *l = t->link;
    if (!hex_of(id, THRESHOLD_ID) || lock > THRESHOLD_LOCK_PASSKEY ||
        (pass && lock != THRESHOLD_LOCK_PASS) ||
        (old_pass && strlen(old_pass) > THRESHOLD_PASS_MAX))
        return ZXM_E_ARG;
    if (!has_session(t)) return ZXM_E_STATE;
    const threshold_seat_t *s = threshold_seat(t, id);
    if (!s) return ZXM_E_ARG;
    if (!threshold_rights(t->my_role, !strcmp(id, t->me) ? 1 : 0, s->role)) { /* TM4, TH1 */
        say(t,
            s->role == THRESHOLD_GUEST ? "A guest never has a lock."
                                       : "Only an owner may change another person's lock.",
            0, NULL);
        return ZXM_E_STATE;
    }
    if (lock == THRESHOLD_LOCK_PASS && (!pass || !threshold_phrase_ok((int64_t) strlen(pass)))) {
        say(t, "A passphrase is 12 to 256 bytes long.", 0, NULL);
        return ZXM_E_ARG;
    }
    zxm_begin(l, "seats.set_lock");
    zxm_p_str(l, "id", id);
    zxm_p_str(l, "lock", LOCK[lock]);
    if (old_pass) zxm_p_str(l, "old_pass", old_pass);
    if (pass) zxm_p_str(l, "pass", pass);
    zxm_p_str(l, "session", t->session);
    int rc = call_secret(l);
    if (rc) return rc;
    t->notice[0] = 0;
    return threshold_refresh(t);
}

int threshold_set_auto(threshold_t *t, bool on)
{
    zxm_link_t *l = t->link;
    if (!has_session(t)) return ZXM_E_STATE;
    zxm_begin(l, "seats.set_auto");
    zxm_p_bool(l, "auto_enter", on);
    zxm_p_str(l, "session", t->session);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool now;
    if (zxm_get_bool(l, l->result, "auto_enter", &now) || now != on) return ZXM_E_PROTOCOL;
    return threshold_refresh(t);
}

/* ---- TM5: passkeys ---- */

static int read_challenge(threshold_t *t, uint8_t kind, const char *user)
{
    zxm_link_t *l = t->link;
    char ch[THRESHOLD_TOKEN + 1];
    int64_t exp;
    if (zxm_get_str(l, l->result, "challenge", ch, sizeof ch) || !hex_of(ch, THRESHOLD_TOKEN) ||
        zxm_get_i64(l, l->result, "expires", &exp) || exp < 0)
        return ZXM_E_PROTOCOL;
    memcpy(t->challenge, ch, sizeof ch);
    t->ch_expires = exp;
    t->ch_kind = kind;
    memset(t->ch_user, 0, sizeof t->ch_user);
    if (user) memcpy(t->ch_user, user, THRESHOLD_ID);
    return ZXM_OK;
}

static void drop_challenge(threshold_t *t)
{
    memset(t->challenge, 0, sizeof t->challenge);
    t->ch_kind = THRESHOLD_CH_NONE;
    t->ch_expires = 0;
}

int threshold_passkey_begin(threshold_t *t, const char *user)
{
    zxm_link_t *l = t->link;
    if (!hex_of(user, THRESHOLD_ID)) return ZXM_E_ARG;
    zxm_begin(l, "passkeys.register_begin");
    zxm_p_str(l, "user", user);
    int rc = zxm_call(l);
    return rc ? rc : read_challenge(t, THRESHOLD_CH_REGISTER, user);
}

int threshold_passkey_finish(threshold_t *t, const char *auth_hex)
{
    zxm_link_t *l = t->link;
    if (!hex_upto(auth_hex, 4066u)) return ZXM_E_ARG;
    if (t->ch_kind != THRESHOLD_CH_REGISTER) return ZXM_E_STATE;
    zxm_begin(l, "passkeys.register_finish");
    zxm_p_str(l, "challenge", t->challenge);
    zxm_p_str(l, "auth", auth_hex);
    int rc = zxm_call(l);
    char user[THRESHOLD_ID + 1];
    memcpy(user, t->ch_user, sizeof user);
    if (rc != ZXM_E_TRANSPORT && rc != ZXM_E_TOOBIG) drop_challenge(t); /* single use */
    if (rc) return rc;
    char cred[THRESHOLD_ID + 1];
    if (zxm_get_str(l, l->result, "cred_id", cred, sizeof cred) || !hex_of(cred, THRESHOLD_ID))
        return ZXM_E_PROTOCOL;
    return threshold_passkeys(t, user);
}

int threshold_signin_begin(threshold_t *t, const char *user)
{
    zxm_link_t *l = t->link;
    if (user && !hex_of(user, THRESHOLD_ID)) return ZXM_E_ARG;
    zxm_begin(l, "passkeys.auth_begin");
    if (user) zxm_p_str(l, "user", user);
    int rc = zxm_call(l);
    t->passkey_ok = false;
    return rc ? rc : read_challenge(t, THRESHOLD_CH_AUTH, user);
}

int threshold_signin_finish(threshold_t *t, const char *cred_id, const char *auth_hex,
                            const char *sig_hex)
{
    zxm_link_t *l = t->link;
    if (!hex_of(cred_id, THRESHOLD_ID) || !hex_of(auth_hex, 74u) || !hex_of(sig_hex, 6618u))
        return ZXM_E_ARG;
    if (t->ch_kind != THRESHOLD_CH_AUTH) return ZXM_E_STATE;
    zxm_begin(l, "passkeys.auth_finish");
    zxm_p_str(l, "challenge", t->challenge);
    zxm_p_str(l, "cred_id", cred_id);
    zxm_p_str(l, "auth", auth_hex);
    zxm_p_str(l, "sig", sig_hex);
    int rc = zxm_call(l);
    if (rc != ZXM_E_TRANSPORT && rc != ZXM_E_TOOBIG) drop_challenge(t);
    t->passkey_ok = false;
    if (rc) return rc;
    bool verified;
    char cred[THRESHOLD_ID + 1];
    int32_t c = zxm_get(l, l->result, "credential");
    if (zxm_get_bool(l, l->result, "verified", &verified) || !verified || c < 0 ||
        zxm_get_str(l, c, "cred_id", cred, sizeof cred) || strcmp(cred, cred_id))
        return ZXM_E_PROTOCOL;
    t->passkey_ok = true;
    return ZXM_OK;
}

static int read_key(zxm_link_t *l, int32_t o, threshold_key_t *k)
{
    memset(k, 0, sizeof *k);
    int32_t lu = o < 0 ? -1 : zxm_get(l, o, "last_used");
    if (o < 0 || lu < 0 || zxm_get_str(l, o, "cred_id", k->cred_id, sizeof k->cred_id) ||
        !hex_of(k->cred_id, THRESHOLD_ID) || zxm_get_str(l, o, "user", k->user, sizeof k->user) ||
        !hex_of(k->user, THRESHOLD_ID) || zxm_get_u64(l, o, "sign_count", &k->sign_count) ||
        zxm_get_i64(l, o, "registered", &k->registered) || k->registered < 0 ||
        zxm_get_bool(l, o, "revoked", &k->revoked))
        return ZXM_E_PROTOCOL;
    k->last_used = -1;
    if (l->tok[lu].type != W4_J_NULL &&
        (zxm_get_i64(l, o, "last_used", &k->last_used) || k->last_used < 0))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int threshold_passkeys(threshold_t *t, const char *user)
{
    zxm_link_t *l = t->link;
    threshold_key_t got[THRESHOLD_KEYS];
    if (user && !hex_of(user, THRESHOLD_ID)) return ZXM_E_ARG;
    zxm_begin(l, "passkeys.list");
    if (user) zxm_p_str(l, "user", user);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "credentials");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > THRESHOLD_KEYS) n = THRESHOLD_KEYS;
    for (uint32_t i = 0; i < n; i++)
        if (read_key(l, zxm_at(l, arr, i), &got[i])) return ZXM_E_PROTOCOL;
    memcpy(t->key, got, n * sizeof got[0]);
    memset(t->key + n, 0, (THRESHOLD_KEYS - n) * sizeof got[0]);
    t->nkeys = n;
    return ZXM_OK;
}

int threshold_passkey_revoke(threshold_t *t, const char *cred_id)
{
    zxm_link_t *l = t->link;
    if (!hex_of(cred_id, THRESHOLD_ID)) return ZXM_E_ARG;
    zxm_begin(l, "passkeys.revoke");
    zxm_p_str(l, "cred_id", cred_id);
    int rc = zxm_call(l);
    if (rc) return rc;
    threshold_key_t k;
    if (read_key(l, l->result, &k) || strcmp(k.cred_id, cred_id) || !k.revoked)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < t->nkeys; i++)
        if (!strcmp(t->key[i].cred_id, cred_id)) t->key[i] = k;
    return ZXM_OK;
}
