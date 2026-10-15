/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Threshold's model against zxvd's real dispatcher. zxvd's own seats.* and
 * passkeys.* groups need Argon2id and ML-DSA, which this harness does not
 * link (test_zxvd_seats.sh tests them), so the two groups here stand in for
 * them through zxvd_rpc_add_group: the same method names, parameters,
 * answers and refusals as api/zxvd.openrpc.json, a roster of TH_CAP seats
 * (zxvd's limit, 4096, made small so SEAT_ERR_FULL is reached), and
 * passkeys behind their own switch, which starts off. */
#include "threshold_model.h"

#define TH_CAP  4u
#define TH_SESS 4u
#define TH_E    (-32080)

typedef struct {
    char id[33], name[48], pass[257];
    int role, lock; /* 0 owner, 1 person, 2 guest; 0 none, 1 pass, 2 passkey */
    uint32_t tries;
    uint64_t wait;
} th_seat_t;

static th_seat_t TS[TH_CAP];
static uint32_t TN, TSEQ;
static bool TAUTO;
static char TSESS[TH_SESS][65];
static int TSEAT[TH_SESS];
static uint32_t TNSESS;
static char TLAST_PASS[300]; /* what the stand-in was last given as a passphrase */

static const char *const TH_ROLE[] = {"owner", "person", "guest"};
static const char *const TH_LOCK[] = {"none", "pass", "passkey"};

/* a bounded copy, cut to fit */
static void th_cpy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1u;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int th_find(const char *id)
{
    for (uint32_t i = 0; i < TN; i++)
        if (!strcmp(TS[i].id, id)) return (int) i;
    return -1;
}

static int th_word(const char *s, const char *const *w)
{
    for (int i = 0; i < 3; i++)
        if (!strcmp(s, w[i])) return i;
    return -1;
}

static void th_hex(char *out, uint32_t chars, uint32_t n)
{
    for (uint32_t i = 0; i < chars; i++) out[i] = '0';
    for (uint32_t i = 0; i < 8; i++) out[chars - 1 - i] = "0123456789abcdef"[(n >> (4 * i)) & 15u];
    out[chars] = 0;
}

static void th_seat_json(zxvd_buf_t *o, const th_seat_t *s)
{
    zxvd_buf_printf(o, "{\"id\":\"%s\",\"name\":", s->id);
    zxvd_buf_json_str(o, s->name);
    zxvd_buf_printf(o, ",\"role\":\"%s\",\"lock\":\"%s\",\"color\":0,\"wait_ms\":%llu}",
                    TH_ROLE[s->role], TH_LOCK[s->lock], (unsigned long long) s->wait);
}

/* the acting seat of the call's session, or -1 after failing it */
static int th_actor(zxvd_call_t *c)
{
    char tok[80];
    if (!zxvd_p_str(c, "session", tok, sizeof tok, true)) return -1;
    for (uint32_t i = 0; i < TNSESS; i++)
        if (!strcmp(TSESS[i], tok)) return TSEAT[i];
    zxvd_fail(c, TH_E, "SEAT_ERR_DENIED: unknown or ended session");
    return -1;
}

static void th_list(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_printf(o, "{\"seats\":[");
    for (uint32_t i = 0; i < TN; i++) {
        if (i) zxvd_buf_printf(o, ",");
        th_seat_json(o, &TS[i]);
    }
    zxvd_buf_printf(o, "],\"auto_enter\":%s}", TAUTO ? "true" : "false");
}

static void th_add(zxvd_call_t *c)
{
    char name[64], role[16] = "person", lock[16] = "none", pass[300] = "";
    int actor = TN ? th_actor(c) : 0;
    if (actor < 0 || !zxvd_p_str(c, "name", name, sizeof name, true) ||
        !zxvd_p_str(c, "role", role, sizeof role, false) ||
        !zxvd_p_str(c, "lock", lock, sizeof lock, false) ||
        !zxvd_p_str(c, "pass", pass, sizeof pass, false))
        return;
    th_cpy(TLAST_PASS, sizeof TLAST_PASS, pass);
    if (!role[0]) snprintf(role, sizeof role, "person");
    if (!lock[0]) snprintf(lock, sizeof lock, "none");
    int r = th_word(role, TH_ROLE), k = th_word(lock, TH_LOCK);
    if (TN && TS[actor].role != 0) {
        zxvd_fail(c, TH_E, "SEAT_ERR_DENIED: owners add seats");
        return;
    }
    if (!name[0] || strlen(name) > 47)
        {
        zxvd_fail(c, TH_E, "SEAT_ERR_NAME: empty, too long, or already used");
        return;
    }
    if (r < 0 || k < 0 || (r == 2 && k) || (k == 1 && (strlen(pass) < 4 || strlen(pass) > 256)))
        {
        zxvd_fail(c, TH_E, "SEAT_ERR_ARG: bad argument");
        return;
    }
    for (uint32_t i = 0; i < TN; i++)
        if (!strcmp(TS[i].name, name))
            {
        zxvd_fail(c, TH_E, "SEAT_ERR_NAME: empty, too long, or already used");
        return;
    }
    if (TN == TH_CAP) {
        zxvd_fail(c, TH_E, "SEAT_ERR_FULL: roster array is full");
        return;
    }
    th_seat_t *s = &TS[TN++];
    memset(s, 0, sizeof *s);
    th_hex(s->id, 32, ++TSEQ);
    th_cpy(s->name, sizeof s->name, name);
    th_cpy(s->pass, sizeof s->pass, pass);
    s->role = TN == 1 ? 0 : r;
    s->lock = k;
    th_seat_json(zxvd_res(c), s);
}

static void th_drop_sessions(int seat)
{
    for (uint32_t i = 0; i < TNSESS;)
        if (TSEAT[i] == seat) {
            TNSESS--;
            memcpy(TSESS[i], TSESS[TNSESS], sizeof TSESS[i]);
            TSEAT[i] = TSEAT[TNSESS];
        } else
            i++;
}

static void th_remove(zxvd_call_t *c)
{
    char id[64];
    int actor = th_actor(c);
    if (actor < 0 || !zxvd_p_str(c, "id", id, sizeof id, true)) return;
    int at = th_find(id);
    if (at < 0) {
        zxvd_fail(c, TH_E, "SEAT_ERR_NOTFOUND: no such seat");
        return;
    }
    if (TS[actor].role != 0) {
        zxvd_fail(c, TH_E, "SEAT_ERR_DENIED: owners remove seats");
        return;
    }
    if (TS[at].role == 0) {
        uint32_t owners = 0;
        for (uint32_t i = 0; i < TN; i++) owners += TS[i].role == 0;
        if (owners == 1) {
        zxvd_fail(c, TH_E, "SEAT_ERR_LAST_OWNER: no owner would be left");
        return;
    }
    }
    th_drop_sessions(at);
    for (uint32_t i = 0; i < TNSESS; i++)
        if (TSEAT[i] > at) TSEAT[i]--;
    for (uint32_t i = (uint32_t) at; i + 1 < TN; i++) TS[i] = TS[i + 1];
    TN--;
    zxvd_buf_printf(zxvd_res(c), "{\"removed\":\"%s\"}", id);
}

static void th_unlock(zxvd_call_t *c)
{
    char id[64], pass[300] = "";
    bool pk;
    if (!zxvd_p_str(c, "id", id, sizeof id, true) ||
        !zxvd_p_str(c, "pass", pass, sizeof pass, false) ||
        !zxvd_p_bool(c, "passkey_ok", false, &pk))
        return;
    th_cpy(TLAST_PASS, sizeof TLAST_PASS, pass);
    int at = th_find(id);
    if (at < 0) {
        zxvd_fail(c, TH_E, "SEAT_ERR_NOTFOUND: no such seat");
        return;
    }
    th_seat_t *s = &TS[at];
    if (s->wait)
        {
        zxvd_buf_printf(zxvd_res(c),
                               "{\"ok\":false,\"status\":\"SEAT_ERR_WAIT\",\"wait_ms\":%llu}",
                               (unsigned long long) s->wait);
        return;
    }
    bool ok = s->lock == 0 || (s->lock == 1 && !strcmp(pass, s->pass)) || (s->lock == 2 && pk);
    if (!ok) {
        s->tries++;
        s->wait = s->tries >= 2 ? 30000u : 0u; /* zxvd: five free tries, then 30 s */
        {
        zxvd_buf_printf(zxvd_res(c),
                               "{\"ok\":false,\"status\":\"SEAT_ERR_WRONG\",\"wait_ms\":%llu}",
                               (unsigned long long) s->wait);
        return;
    }
    }
    s->tries = 0;
    if (TNSESS == TH_SESS) th_drop_sessions(TSEAT[0]);
    th_hex(TSESS[TNSESS], 64, 0x5e550000u + ++TSEQ);
    TSEAT[TNSESS] = at;
    zxvd_buf_printf(zxvd_res(c), "{\"ok\":true,\"wait_ms\":0,\"session\":\"%s\",\"id\":\"%s\"}",
                    TSESS[TNSESS], s->id);
    TNSESS++;
}

static void th_lock(zxvd_call_t *c)
{
    char tok[80];
    if (!zxvd_p_str(c, "session", tok, sizeof tok, true)) return;
    bool ended = false;
    for (uint32_t i = 0; i < TNSESS; i++)
        if (!strcmp(TSESS[i], tok)) {
            TNSESS--;
            memcpy(TSESS[i], TSESS[TNSESS], sizeof TSESS[i]);
            TSEAT[i] = TSEAT[TNSESS];
            ended = true;
            break;
        }
    zxvd_buf_printf(zxvd_res(c), "{\"ended\":%s}", ended ? "true" : "false");
}

static void th_set_lock(zxvd_call_t *c)
{
    char id[64], lock[16], old[300] = "", pass[300] = "";
    int actor = th_actor(c);
    if (actor < 0 || !zxvd_p_str(c, "id", id, sizeof id, true) ||
        !zxvd_p_str(c, "lock", lock, sizeof lock, true) ||
        !zxvd_p_str(c, "old_pass", old, sizeof old, false) ||
        !zxvd_p_str(c, "pass", pass, sizeof pass, false))
        return;
    th_cpy(TLAST_PASS, sizeof TLAST_PASS, pass);
    int at = th_find(id), k = th_word(lock, TH_LOCK);
    if (at < 0) {
        zxvd_fail(c, TH_E, "SEAT_ERR_NOTFOUND: no such seat");
        return;
    }
    th_seat_t *s = &TS[at];
    if (TS[actor].role != 0 && at != actor)
        {
        zxvd_fail(c, TH_E, "SEAT_ERR_DENIED: a person changes only their own lock");
        return;
    }
    if (k < 0 || (s->role == 2 && k) || (k == 1 && (strlen(pass) < 4 || strlen(pass) > 256)))
        {
        zxvd_fail(c, TH_E, "SEAT_ERR_ARG: bad argument");
        return;
    }
    if (TS[actor].role != 0 && s->lock == 1 && strcmp(old, s->pass))
        {
        zxvd_fail(c, TH_E, "SEAT_ERR_WRONG: wrong passphrase");
        return;
    }
    s->lock = k;
    th_cpy(s->pass, sizeof s->pass, k == 1 ? pass : "");
    th_seat_json(zxvd_res(c), s);
}

static void th_auto(zxvd_call_t *c)
{
    if (TN == 1 && TS[0].lock == 0 && TAUTO)
        zxvd_buf_printf(zxvd_res(c), "{\"seat\":\"%s\"}", TS[0].id);
    else
        zxvd_buf_printf(zxvd_res(c), "{\"seat\":null}");
}

static void th_set_auto(zxvd_call_t *c)
{
    bool on;
    int actor = th_actor(c);
    if (actor < 0 || !zxvd_p_bool(c, "auto_enter", false, &on)) return;
    if (TS[actor].role != 0) {
        zxvd_fail(c, TH_E, "SEAT_ERR_DENIED: owners set auto-enter");
        return;
    }
    TAUTO = on;
    zxvd_buf_printf(zxvd_res(c), "{\"auto_enter\":%s}", on ? "true" : "false");
}

static const zxvd_group_method_t TH_SEATS_M[] = {
    {"seats.list", th_list, true},       {"seats.add", th_add, true},
    {"seats.remove", th_remove, true},   {"seats.unlock", th_unlock, true},
    {"seats.lock", th_lock, true},       {"seats.set_lock", th_set_lock, true},
    {"seats.auto", th_auto, true},       {"seats.set_auto", th_set_auto, true},
};
static const zxvd_group_t TH_SEATS = {"seats", false, "the people who use this device",
                                      TH_SEATS_M, 8, NULL};

/* ---- passkeys: one credential store, challenges single use ---- */

typedef struct {
    char cred[33], user[33];
    uint64_t count;
    bool revoked, used;
} th_key_t;

static th_key_t TK[4];
static uint32_t TKN;
static char TCH[65], TCH_USER[33];
static int TCH_KIND; /* 0 none, 1 create, 2 get */

static void th_key_json(zxvd_buf_t *o, const th_key_t *k)
{
    zxvd_buf_printf(o,
                    "{\"cred_id\":\"%s\",\"user\":\"%s\",\"rp_id\":\"zxv.id\",\"sign_count\":%llu,"
                    "\"revoked\":%s,\"registered\":1700000000,\"last_used\":",
                    k->cred, k->user, (unsigned long long) k->count, k->revoked ? "true" : "false");
    if (k->used)
        zxvd_buf_printf(o, "1700000100}");
    else
        zxvd_buf_printf(o, "null}");
}

static void th_challenge(zxvd_call_t *c, int kind)
{
    char user[64] = "";
    if (!zxvd_p_str(c, "user", user, sizeof user, kind == 1)) return;
    th_hex(TCH, 64, 0xc4a1000u + ++TSEQ);
    th_cpy(TCH_USER, sizeof TCH_USER, user);
    TCH_KIND = kind;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_printf(o,
                    "{\"type\":\"%s\",\"challenge\":\"%s\",\"rp_id\":\"zxv.id\",\"origin\":"
                    "\"https://zxv.id\",\"alg\":-49,\"require_uv\":true,\"expires\":1700000300",
                    kind == 1 ? "webauthn.create" : "webauthn.get", TCH);
    if (kind == 1) zxvd_buf_printf(o, ",\"user\":\"%s\"", user);
    zxvd_buf_printf(o, "}");
}

static void th_reg_begin(zxvd_call_t *c)
{
    th_challenge(c, 1);
}

static void th_auth_begin(zxvd_call_t *c)
{
    th_challenge(c, 2);
}

static bool th_take(zxvd_call_t *c, int kind)
{
    char ch[80];
    if (!zxvd_p_str(c, "challenge", ch, sizeof ch, true)) return false;
    bool ok = TCH_KIND == kind && !strcmp(ch, TCH);
    TCH_KIND = 0; /* single use */
    if (!ok) zxvd_fail(c, -32041, "IDENT_ERR_CHALLENGE: unknown or used challenge");
    return ok;
}

static void th_reg_finish(zxvd_call_t *c)
{
    char auth[4100];
    if (!zxvd_p_str(c, "auth", auth, sizeof auth, true) || !th_take(c, 1)) return;
    if (TKN == 4) {
        zxvd_fail(c, -32041, "IDENT_ERR_FULL: credential store full");
        return;
    }
    th_key_t *k = &TK[TKN++];
    memset(k, 0, sizeof *k);
    th_hex(k->cred, 32, 0xc3ed000u + ++TSEQ);
    th_cpy(k->user, sizeof k->user, TCH_USER);
    th_key_json(zxvd_res(c), k);
}

static void th_auth_finish(zxvd_call_t *c)
{
    char cred[64], auth[100], sig[6700];
    if (!zxvd_p_str(c, "cred_id", cred, sizeof cred, true) ||
        !zxvd_p_str(c, "auth", auth, sizeof auth, true) ||
        !zxvd_p_str(c, "sig", sig, sizeof sig, true) || !th_take(c, 2))
        return;
    for (uint32_t i = 0; i < TKN; i++)
        if (!strcmp(TK[i].cred, cred) && !TK[i].revoked && sig[0] == '1') {
            TK[i].count++;
            TK[i].used = true;
            zxvd_buf_t *o = zxvd_res(c);
            zxvd_buf_printf(o, "{\"verified\":true,\"uv\":true,\"credential\":");
            th_key_json(o, &TK[i]);
            {
        zxvd_buf_printf(o, "}");
        return;
    }
        }
    zxvd_fail(c, -32041, "IDENT_ERR_SIG: signature refused");
}

static void th_keys(zxvd_call_t *c)
{
    char user[64] = "";
    if (!zxvd_p_str(c, "user", user, sizeof user, false)) return;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_printf(o, "{\"credentials\":[");
    for (uint32_t i = 0, n = 0; i < TKN; i++)
        if (!user[0] || !strcmp(TK[i].user, user)) {
            if (n++) zxvd_buf_printf(o, ",");
            th_key_json(o, &TK[i]);
        }
    zxvd_buf_printf(o, "]}");
}

static void th_revoke(zxvd_call_t *c)
{
    char cred[64];
    if (!zxvd_p_str(c, "cred_id", cred, sizeof cred, true)) return;
    for (uint32_t i = 0; i < TKN; i++)
        if (!strcmp(TK[i].cred, cred)) {
            TK[i].revoked = true;
            {
        th_key_json(zxvd_res(c), &TK[i]);
        return;
    }
        }
    zxvd_fail(c, -32041, "IDENT_ERR_NOTFOUND: no such credential");
}

static const zxvd_group_method_t TH_KEYS_M[] = {
    {"passkeys.register_begin", th_reg_begin, false},
    {"passkeys.register_finish", th_reg_finish, false},
    {"passkeys.auth_begin", th_auth_begin, false},
    {"passkeys.auth_finish", th_auth_finish, false},
    {"passkeys.list", th_keys, false},
    {"passkeys.revoke", th_revoke, false},
};
static const zxvd_group_t TH_KEYS = {"passkeys", true, "passkeys", TH_KEYS_M, 6, NULL};

/* whether n bytes at p hold the text s anywhere */
static bool th_holds(const void *p, size_t n, const char *s)
{
    size_t k = strlen(s);
    const char *b = p;
    for (size_t i = 0; i + k <= n; i++)
        if (!memcmp(b + i, s, k)) return true;
    return false;
}

static bool th_wiped(const zxm_link_t *l)
{
    for (uint32_t i = 0; i < sizeof l->req; i++)
        if (l->req[i]) return false;
    return l->req_len == 0;
}

APPS_TEST(threshold)
{
    static threshold_t T;
    CHECK(zxvd_rpc_add_group(&TH_SEATS) == 0 && zxvd_rpc_add_group(&TH_KEYS) == 0,
          "threshold: seats and passkeys groups");
    threshold_init(&T, &LINK);

    /* TM1: an empty device; the first seat is the owner's and needs no session */
    CHECK(threshold_refresh(&T) == ZXM_OK && T.total == 0 && !T.auto_enter && !T.auto_seat[0] &&
              !threshold_skips_login(&T),
          "threshold: an empty device: %s", LINK.err);
    CHECK(threshold_add(&T, "Ada", THRESHOLD_OWNER, THRESHOLD_LOCK_NONE, NULL, 0) == ZXM_OK &&
              T.total == 1 && T.seat[0].role == THRESHOLD_OWNER && !T.auto_enter,
          "threshold: the first seat: %s", LINK.err);
    char ada[THRESHOLD_ID + 1];
    memcpy(ada, T.seat[0].id, sizeof ada);

    /* TH2 against zxvd's own answer: auto-enter off, then on */
    CHECK(!threshold_skips_login(&T) && !T.auto_seat[0], "threshold: auto off, the login shows");
    CHECK(threshold_set_auto(&T, true) == ZXM_E_STATE && !strcmp(T.notice, "Unlock a seat first."),
          "threshold: no session, nothing asked");
    CHECK(threshold_unlock(&T, ada, NULL, false) == ZXM_OK && T.login == THRESHOLD_LOGIN_OK &&
              !strcmp(T.me, ada) && T.my_role == THRESHOLD_OWNER && strlen(T.session) == 64,
          "threshold: a seat with no lock opens with nothing: %s", LINK.err);
    CHECK(threshold_set_auto(&T, true) == ZXM_OK && T.auto_enter && threshold_skips_login(&T) &&
              !strcmp(T.auto_seat, ada),
          "threshold: one seat, no lock, auto on: skipped, as zxvd says: %s", LINK.err);

    /* TM4, TH3: a passphrase held to 12..256 before anything is asked */
    int sent0 = sent;
    CHECK(threshold_add(&T, "Bo", THRESHOLD_PERSON, THRESHOLD_LOCK_PASS, "short", 0) ==
                  ZXM_E_ARG &&
              sent == sent0 && T.total == 1,
          "threshold: 5 bytes is too short, nothing sent");
    CHECK(threshold_add(&T, "Gus", THRESHOLD_GUEST, THRESHOLD_LOCK_PASS, "a long enough one", 0) ==
                  ZXM_E_STATE &&
              sent == sent0 && !strcmp(T.notice, "A guest never has a lock."),
          "threshold: a guest never has a lock (TH1), nothing sent");
    CHECK(threshold_add(&T, "Bo", THRESHOLD_PERSON, THRESHOLD_LOCK_PASS, "correct horse 12", 7) ==
                  ZXM_OK &&
              T.total == 2 && T.seat[1].role == THRESHOLD_PERSON &&
              T.seat[1].lock == THRESHOLD_LOCK_PASS && !threshold_skips_login(&T) &&
              !T.auto_seat[0],
          "threshold: a person with a passphrase; two seats, the login shows: %s", LINK.err);
    CHECK(!strcmp(TLAST_PASS, "correct horse 12") && !strstr(LINK.req, "horse") &&
              !strstr(LINK.resp, "horse") && !th_holds(&T, sizeof T, "horse"),
          "threshold: the passphrase reached zxvd and is kept nowhere here (TM2)");
    char bo[THRESHOLD_ID + 1];
    memcpy(bo, T.seat[1].id, sizeof bo);
    CHECK(threshold_add(&T, "Gus", THRESHOLD_GUEST, THRESHOLD_LOCK_NONE, NULL, 0) == ZXM_OK &&
              T.total == 3 && T.seat[2].role == THRESHOLD_GUEST,
          "threshold: a guest, unlocked: %s", LINK.err);
    char gus[THRESHOLD_ID + 1];
    memcpy(gus, T.seat[2].id, sizeof gus);

    /* TM3: a wrong entry shows zxvd's back-off; never tried again here */
    sent0 = sent;
    CHECK(threshold_unlock(&T, bo, "not the phrase", false) == ZXM_OK &&
              T.login == THRESHOLD_LOGIN_WRONG && T.wait_ms == 0 && sent == sent0 + 1 &&
              !strcmp(T.me, ada) && !strcmp(T.notice, "That was not right."),
          "threshold: one wrong entry, tries still free, the owner still acting");
    CHECK(threshold_unlock(&T, bo, "not it either", false) == ZXM_OK &&
              T.login == THRESHOLD_LOGIN_WRONG && T.wait_ms == 30000 && sent == sent0 + 2 &&
              !strcmp(T.notice, "That was not right. Try again in 30 s.") &&
              threshold_seat(&T, bo)->wait_ms == 30000 && th_wiped(&LINK),
          "threshold: the back-off zxvd started, shown: %s", T.notice);
    CHECK(threshold_unlock(&T, bo, "correct horse 12", false) == ZXM_OK &&
              T.login == THRESHOLD_LOGIN_WAIT && T.wait_ms == 30000 && !strcmp(T.me, ada) &&
              sent == sent0 + 3,
          "threshold: backing off: SEAT_ERR_WAIT, shown, one request");
    TS[1].wait = 0; /* the back-off ran out */
    CHECK(threshold_unlock(&T, bo, "correct horse 12", false) == ZXM_OK &&
              T.login == THRESHOLD_LOGIN_OK && !strcmp(T.me, bo) &&
              T.my_role == THRESHOLD_PERSON && T.wait_ms == 0,
          "threshold: the right passphrase: %s", LINK.err);

    /* TH1 in the model: a person their own lock only */
    sent0 = sent;
    CHECK(threshold_set_lock(&T, ada, THRESHOLD_LOCK_PASS, NULL, "taking it over!") ==
                  ZXM_E_STATE &&
              sent == sent0 && !strcmp(T.notice, "Only an owner may change another person's lock."),
          "threshold: a person may not lock the owner, nothing sent");
    CHECK(threshold_set_lock(&T, gus, THRESHOLD_LOCK_PASS, NULL, "a guest lock 1") ==
                  ZXM_E_STATE &&
              sent == sent0,
          "threshold: no one gives a guest a lock");
    CHECK(threshold_set_lock(&T, bo, THRESHOLD_LOCK_PASS, "wrong old phrase", "a new phrase 12") ==
                  ZXM_E_REMOTE &&
              LINK.err_code == TH_E && !strncmp(LINK.err, "SEAT_ERR_WRONG", 14) && th_wiped(&LINK),
          "threshold: a wrong old passphrase, zxvd's refusal shown: %s", LINK.err);
    CHECK(threshold_set_lock(&T, bo, THRESHOLD_LOCK_PASS, "correct horse 12", "a new phrase 12") ==
                  ZXM_OK &&
              !strcmp(TS[1].pass, "a new phrase 12"),
          "threshold: a person changes their own lock: %s", LINK.err);
    CHECK(threshold_set_auto(&T, false) == ZXM_E_REMOTE && !strncmp(LINK.err, "SEAT_ERR_DENIED", 15),
          "threshold: only an owner sets auto-enter, as zxvd says: %s", LINK.err);
    CHECK(threshold_add(&T, "Cy", THRESHOLD_PERSON, THRESHOLD_LOCK_PASS, "a long phrase!", 0) ==
              ZXM_E_STATE,
          "threshold: a person gives no new seat a lock (TH1)");
    CHECK(threshold_add(&T, "Cy", THRESHOLD_PERSON, THRESHOLD_LOCK_NONE, NULL, 0) ==
                  ZXM_E_REMOTE &&
              !strncmp(LINK.err, "SEAT_ERR_DENIED", 15) && T.total == 3,
          "threshold: zxvd decides who adds, and the model shows it: %s", LINK.err);

    /* TM2: seats.lock ends the session */
    CHECK(threshold_lock(&T) == ZXM_OK && !T.session[0] && !T.me[0] && T.my_role == 0,
          "threshold: locked: %s", LINK.err);
    CHECK(threshold_lock(&T) == ZXM_E_STATE, "threshold: nothing to lock");

    /* the owner: any lock but a guest's; SEAT_ERR_FULL is shown, never a limit of the model's */
    CHECK(threshold_unlock(&T, ada, NULL, false) == ZXM_OK && T.my_role == THRESHOLD_OWNER,
          "threshold: the owner again: %s", LINK.err);
    CHECK(threshold_set_lock(&T, bo, THRESHOLD_LOCK_NONE, NULL, NULL) == ZXM_OK &&
              threshold_seat(&T, bo)->lock == THRESHOLD_LOCK_NONE,
          "threshold: an owner resets anyone's lock: %s", LINK.err);
    CHECK(threshold_add(&T, "Dee", THRESHOLD_PERSON, THRESHOLD_LOCK_NONE, NULL, 0) == ZXM_OK &&
              T.total == 4 && !T.full,
          "threshold: a fourth seat: %s", LINK.err);
    CHECK(threshold_add(&T, "Eli", THRESHOLD_PERSON, THRESHOLD_LOCK_NONE, NULL, 0) ==
                  ZXM_E_REMOTE &&
              T.full && T.total == 4 && LINK.err_code == TH_E &&
              !strcmp(T.notice, "This device holds as many people as zxvd allows."),
          "threshold: zxvd's limit, shown: %s", LINK.err);
    CHECK(threshold_remove(&T, gus) == ZXM_OK && T.total == 3 && !threshold_seat(&T, gus) &&
              !T.full,
          "threshold: a seat removed: %s", LINK.err);
    CHECK(threshold_remove(&T, ada) == ZXM_E_REMOTE && !strncmp(LINK.err, "SEAT_ERR_LAST_OWNER", 19),
          "threshold: the last owner stays: %s", LINK.err);
    CHECK(threshold_remove(&T, "zz") == ZXM_E_ARG && threshold_unlock(&T, "12", NULL, false) ==
                                                          ZXM_E_ARG,
          "threshold: an id is 32 hex digits");

    /* TM1: a reply out of bounds changes nothing */
    TS[1].role = 2, TS[1].lock = 1; /* a guest with a lock: zxvd never says so */
    CHECK(threshold_refresh(&T) == ZXM_E_PROTOCOL && T.total == 3 &&
              threshold_seat(&T, bo)->role == THRESHOLD_PERSON,
          "threshold: a reply that breaks TH1 leaves the roster as shown");
    TS[1].role = 1, TS[1].lock = 0;
    CHECK(threshold_refresh(&T) == ZXM_OK, "threshold: whole again: %s", LINK.err);

    /* TM5: passkeys sit behind their switch, which starts off */
    char user[THRESHOLD_ID + 1];
    memcpy(user, bo, sizeof user);
    CHECK(threshold_passkey_begin(&T, user) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_OFF &&
              T.ch_kind == THRESHOLD_CH_NONE,
          "threshold: passkeys off: zxvd's -32012 shown: %s", LINK.err);
    CHECK(zxvd_rpc_enable("passkeys") == 0, "threshold: passkeys on");
    CHECK(threshold_passkey_finish(&T, "abcd") == ZXM_E_STATE, "threshold: no challenge yet");
    CHECK(threshold_passkey_begin(&T, user) == ZXM_OK && T.ch_kind == THRESHOLD_CH_REGISTER &&
              strlen(T.challenge) == 64 && T.ch_expires == 1700000300,
          "threshold: a registration challenge: %s", LINK.err);
    CHECK(threshold_passkey_finish(&T, "abc") == ZXM_E_ARG, "threshold: auth is whole bytes");
    CHECK(threshold_passkey_finish(&T, "a0b1c2d3") == ZXM_OK && T.nkeys == 1 &&
              !strcmp(T.key[0].user, user) && !T.key[0].revoked && T.key[0].last_used == -1 &&
              T.ch_kind == THRESHOLD_CH_NONE && !T.challenge[0],
          "threshold: a passkey registered, its challenge used: %s", LINK.err);
    char cred[THRESHOLD_ID + 1];
    memcpy(cred, T.key[0].cred_id, sizeof cred);

    static char auth[75], sig[6619];
    memset(auth, 'a', 74);
    memset(sig, '1', 6618);
    CHECK(threshold_signin_finish(&T, cred, auth, sig) == ZXM_E_STATE,
          "threshold: a sign-in needs its challenge");
    CHECK(threshold_signin_begin(&T, user) == ZXM_OK && T.ch_kind == THRESHOLD_CH_AUTH,
          "threshold: a sign-in challenge: %s", LINK.err);
    CHECK(threshold_signin_finish(&T, cred, auth, sig) == ZXM_OK && T.passkey_ok &&
              T.ch_kind == THRESHOLD_CH_NONE,
          "threshold: verified: %s", LINK.err);
    CHECK(threshold_set_lock(&T, bo, THRESHOLD_LOCK_PASSKEY, NULL, NULL) == ZXM_OK &&
              threshold_seat(&T, bo)->lock == THRESHOLD_LOCK_PASSKEY,
          "threshold: the owner gives Bo a passkey lock: %s", LINK.err);
    CHECK(threshold_unlock(&T, bo, NULL, false) == ZXM_OK && T.login == THRESHOLD_LOGIN_OK &&
              !strcmp(T.me, bo) && !T.passkey_ok,
          "threshold: the passkey's verdict opens the seat once: %s", LINK.err);
    CHECK(threshold_signin_begin(&T, NULL) == ZXM_OK, "threshold: a sign-in, any user");
    sig[0] = '0'; /* a signature the stand-in refuses */
    CHECK(threshold_signin_finish(&T, cred, auth, sig) == ZXM_E_REMOTE && !T.passkey_ok &&
              T.ch_kind == THRESHOLD_CH_NONE,
          "threshold: a refused signature, its challenge spent: %s", LINK.err);
    CHECK(threshold_unlock(&T, bo, NULL, false) == ZXM_OK && T.login == THRESHOLD_LOGIN_WRONG,
          "threshold: no verdict, no entry");
    CHECK(threshold_passkey_revoke(&T, cred) == ZXM_OK && T.key[0].revoked,
          "threshold: revoked: %s", LINK.err);
    CHECK(threshold_passkeys(&T, NULL) == ZXM_OK && T.nkeys == 1 && T.key[0].revoked &&
              T.key[0].last_used == 1700000100 && T.key[0].sign_count == 1,
          "threshold: every passkey listed: %s", LINK.err);

    zxvd_rpc_remove_group(&TH_KEYS);
    zxvd_rpc_remove_group(&TH_SEATS);
}
