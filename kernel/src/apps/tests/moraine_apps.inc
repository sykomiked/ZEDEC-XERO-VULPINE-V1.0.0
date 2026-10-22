/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Moraine's model against a recovery engine and zxvd's real journal.
 * zxvd's recovery group (zxvd_estate.c) is not linked into this
 * in-process zxvd, so recovery.* is answered on a link of Moraine's own
 * (MR_LINK) by a small engine written here from recovery.h R1-R4 (five
 * guardians, a quorum of 3, after a device's veto 4 and 14 days, the time
 * lock), logging each release as zxvd does; every other request
 * (journal.query) goes to the real dispatcher, under a journal on disk.
 * Every signature is a real Ed25519 signature made with throwaway keys
 * this test generates (libzxv zxv_ed25519_keygen) and checked by the
 * engine; nothing comes from build_system/keys/. No key's bytes are ever
 * given to the model: keys are public ids and counts. */
#include "moraine_model.h"
#include "zxv_api.h"

#define MR_H16  "0123456789abcdef"
#define MR_H128 MR_H16 MR_H16 MR_H16 MR_H16 MR_H16 MR_H16 MR_H16 MR_H16

static moraine_t MR;
static zxm_link_t MR_LINK;
static zxv_ctx *MR_CTX;
static zxv_cap *MR_CAP;
static int mr_sent, mr_finalized; /* recovery.* requests answered; releases made */

typedef struct {
    uint8_t pk[32], sk[32];
} mr_key_t;

static mr_key_t MK_ROOT, MK_G[5], MK_GW, MK_DEV, MK_NEW;

static struct {
    bool set_up;
    uint8_t g[5][32], dev[32];
    int state; /* 0 idle, 1 pending, 2 thawing */
    int64_t claim, opened, timelock;
    uint32_t approved; /* a bit per guardian */
    bool vetoed;
} ME;

static void mr_hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

static bool mr_unhex(const char *s, uint8_t *p, size_t n)
{
    if (strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[2 * i + (size_t) k];
            unsigned d = c >= '0' && c <= '9'   ? (unsigned) (c - '0')
                         : c >= 'a' && c <= 'f' ? (unsigned) (c - 'a' + 10)
                                                : 16u;
            if (d > 15u) return false;
            v = v * 16u + d;
        }
        p[i] = (uint8_t) v;
    }
    return true;
}

static int64_t mr_num(const char *from, const char *key)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(from, k);
    if (!p) return -1;
    p += strlen(k);
    if (*p == '"') p++;
    if (*p < '0' || *p > '9') return -1;
    int64_t v = 0;
    for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
    return v;
}

static bool mr_str(const char *from, const char *key, char *out, size_t cap)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *p = strstr(from, k);
    if (!p) return false;
    p += strlen(k);
    const char *e = strchr(p, '"');
    if (!e || (size_t) (e - p) >= cap) return false;
    memcpy(out, p, (size_t) (e - p));
    out[e - p] = 0;
    return true;
}

static int32_t mr_ok(char *resp, uint32_t cap, uint32_t id, const char *result)
{
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", id, result);
}

static int32_t mr_err(char *resp, uint32_t cap, uint32_t id, const char *why)
{
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32060,"
                    "\"message\":\"%s\"}}",
                    id, why);
}

/* the bytes a signer signs: kind and the claim (rec_claim_msg's role) */
static size_t mr_msg(const char *kind, uint8_t *m, size_t cap)
{
    int n = snprintf((char *) m, cap, "moraine-test|%s|%lld", kind, (long long) ME.claim);
    return n > 0 && (size_t) n < cap ? (size_t) n : 0;
}

static bool mr_verify(const uint8_t pk[32], const char *kind, const char *params)
{
    char sh[MORAINE_SIG_HEX + 1];
    uint8_t sig[64], m[96];
    size_t n = mr_msg(kind, m, sizeof m);
    return n && mr_str(params, "sig", sh, sizeof sh) && mr_unhex(sh, sig, 64) &&
           zxv_ed25519_verify(MR_CAP, pk, m, n, sig) == ZXV_OK;
}

static uint32_t mr_count(void)
{
    uint32_t c = 0;
    for (int g = 0; g < 5; g++) c += (ME.approved >> g) & 1u;
    return c;
}

static int32_t mr_claim_reply(char *resp, uint32_t cap, uint32_t id)
{
    static const char *const S[] = {"idle", "pending", "thawing"};
    char s[256];
    snprintf(s, sizeof s, "{\"state\":\"%s\",\"claim\":\"%lld\",\"approvals\":%u,\"vetoed\":%s}",
             S[ME.state], (long long) ME.claim, (unsigned) mr_count(),
             ME.vetoed ? "true" : "false");
    return mr_ok(resp, cap, id, s);
}

static int32_t mr_engine(const char *m, const char *p, char *resp, uint32_t cap, uint32_t id)
{
    char s[512];
    if (!strcmp(m, "recovery.setup")) {
        if (ME.set_up) return mr_err(resp, cap, id, "SET_UP");
        if (!mr_str(p, "guardians", s, sizeof s) || strlen(s) != 5 * 64)
            return mr_err(resp, cap, id, "guardians must be exactly 5 keys");
        for (int g = 0; g < 5; g++) {
            char one[65];
            memcpy(one, s + 64 * g, 64);
            one[64] = 0;
            mr_unhex(one, ME.g[g], 32);
        }
        if (!mr_str(p, "first_device", s, sizeof s) || !mr_unhex(s, ME.dev, 32))
            return mr_err(resp, cap, id, "first_device");
        ME.timelock = mr_num(p, "timelock") > 0 ? mr_num(p, "timelock") : 259200;
        ME.set_up = true;
        snprintf(s, sizeof s,
                 "{\"timelock\":\"%lld\",\"rate_window\":\"2592000\",\"rate_max\":3,"
                 "\"state\":\"idle\"}",
                 (long long) ME.timelock);
        return mr_ok(resp, cap, id, s);
    }
    if (!strcmp(m, "recovery.status")) {
        static const char *const S[] = {"idle", "pending", "thawing"};
        if (!ME.set_up)
            return mr_ok(resp, cap, id, "{\"set_up\":false,\"notices_delivered\":false}");
        snprintf(s, sizeof s,
                 "{\"set_up\":true,\"state\":\"%s\",\"claim\":\"%lld\",\"approvals\":%u,"
                 "\"needed\":%u,\"vetoed\":%s,\"devices\":1,\"can_view\":true,\"can_spend\":%s,"
                 "\"gateway_verified\":false,\"opened\":\"%lld\",\"vbill_spendable_minor\":\"0\","
                 "\"liquid_allowance_minor\":\"0\",\"notices\":\"0\",\"notices_delivered\":false}",
                 S[ME.state], (long long) ME.claim, (unsigned) mr_count(), ME.vetoed ? 4u : 3u,
                 ME.vetoed ? "true" : "false", ME.state == 1 ? "false" : "true",
                 (long long) ME.opened);
        return mr_ok(resp, cap, id, s);
    }
    if (!ME.set_up) return mr_err(resp, cap, id, "recovery is not set up");
    if (!strcmp(m, "recovery.claim")) {
        if (ME.state == 1) return mr_err(resp, cap, id, "REC_ERR_BUSY");
        ME.state = 1;
        ME.claim++;
        ME.opened = mr_num(p, "now");
        ME.approved = 0;
        ME.vetoed = false;
        return mr_claim_reply(resp, cap, id);
    }
    if (!strcmp(m, "recovery.message")) {
        uint8_t b[96];
        char hx[200];
        if (!mr_str(p, "kind", s, sizeof s)) return mr_err(resp, cap, id, "kind");
        size_t n = mr_msg(s, b, sizeof b);
        mr_hex(b, n, hx);
        snprintf(s, sizeof s, "{\"message\":\"%s\",\"bytes\":%u,\"scheme\":\"ed25519\"}", hx,
                 (unsigned) n);
        return mr_ok(resp, cap, id, s);
    }
    if (ME.state != 1) return mr_err(resp, cap, id, "REC_ERR_STATE");
    if (!strcmp(m, "recovery.approve")) {
        int64_t g = mr_num(p, "guardian");
        if (g < 0 || g > 4) return mr_err(resp, cap, id, "REC_ERR_GUARDIAN");
        if (!mr_verify(ME.g[g], "approve", p)) return mr_err(resp, cap, id, "REC_ERR_SIG");
        ME.approved |= 1u << g;
        return mr_claim_reply(resp, cap, id);
    }
    if (!strcmp(m, "recovery.veto") || !strcmp(m, "recovery.cancel")) {
        bool veto = !strcmp(m, "recovery.veto");
        uint8_t dev[32];
        if (!mr_str(p, "device", s, sizeof s) || !mr_unhex(s, dev, 32) || memcmp(dev, ME.dev, 32))
            return mr_err(resp, cap, id, "REC_ERR_DEVICE");
        if (!mr_verify(ME.dev, veto ? "veto" : "cancel", p))
            return mr_err(resp, cap, id, "REC_ERR_SIG");
        if (veto)
            ME.vetoed = true;
        else
            ME.state = 0;
        return mr_claim_reply(resp, cap, id);
    }
    if (!strcmp(m, "recovery.finalize")) {
        int64_t now = mr_num(p, "now");
        uint32_t need = ME.vetoed ? 4u : 3u;
        int64_t wait = ME.vetoed ? 14 * 86400 : ME.timelock;
        if (mr_count() < need) return mr_err(resp, cap, id, "REC_ERR_QUORUM");
        if (now < ME.opened + wait) return mr_err(resp, cap, id, "REC_ERR_TIME");
        ME.state = 2;
        mr_finalized++;
        zxvd_log(ZXVD_LOG_INFO, "recovery", "finalize claim %lld", (long long) ME.claim);
        return mr_claim_reply(resp, cap, id);
    }
    return mr_err(resp, cap, id, "no such method here");
}

/* recovery.* here; everything else to the real dispatcher */
static int32_t mr_route(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    static char r[ZXM_REQ_MAX + 1];
    char m[48];
    if (len > ZXM_REQ_MAX) return -1;
    memcpy(r, req, len);
    r[len] = 0;
    if (!mr_str(r, "method", m, sizeof m) || strncmp(m, "recovery.", 9))
        return inproc(ctx, req, len, resp, cap);
    mr_sent++;
    const char *p = strstr(r, "\"params\":");
    return mr_engine(m, p ? p + 9 : "", resp, cap, (uint32_t) mr_num(r, "id"));
}

static void mr_keygen(mr_key_t *k)
{
    if (zxv_ed25519_keygen(MR_CAP, k->pk, k->sk) != ZXV_OK) memset(k, 0, sizeof *k);
}

static char *mr_pk(const mr_key_t *k)
{
    static char b[8][65];
    static int i;
    char *o = b[i++ & 7];
    mr_hex(k->pk, 32, o);
    return o;
}

/* key k's signature over what moraine_message answers for kind */
static void mr_signed(const mr_key_t *k, const char *kind, char *out)
{
    char msg[MORAINE_MSG_HEX + 1];
    uint8_t m[MORAINE_MSG_HEX / 2], sig[64];
    size_t n = 0;
    if (moraine_message(&MR, kind, msg, sizeof msg) == ZXM_OK) n = strlen(msg) / 2;
    if (!n || !mr_unhex(msg, m, n)) n = 0;
    if (zxv_ed25519_sign(MR_CAP, k->sk, m, n, sig) != ZXV_OK) memset(sig, 0, sizeof sig);
    mr_hex(sig, 64, out);
}

APPS_TEST(moraine)
{
    const int64_t T0 = INT64_C(1790000000), DAY = 86400, LOCK = 3 * 86400;
    static const int64_t TREE[5] = {0, 1, 1, 2, 0}, LOOP[5] = {2, 1, 0, 0, 0};
    char sig[MORAINE_SIG_HEX + 1], guardians[5 * 64 + 1], jdir[256];
    uint32_t k0 = 9, k1 = 9;
    moraine_t *m = &MR;

    /* the rules */
    CHECK(moraine_release_now(1, 1, 1) && !moraine_release_now(1, 0, 1) &&
              !moraine_release_now(2, 5, 1) && !moraine_release_now(1, 5, 2) &&
              moraine_custody_ok(1, 0, 0, 5, 5) == 1 && moraine_custody_ok(3, 1, 0, 5, 5) == 0 &&
              moraine_custody_ok(-1, 0, 0, 0, 0) == MORAINE_REFUSED &&
              moraine_key_strength(256, 3, 5) == 100 && moraine_key_strength(127, 3, 3) == 45 &&
              moraine_key_strength(1, 4, 3) == MORAINE_REFUSED,
          "moraine: the rules");

    /* throwaway keys, made here */
    CHECK(zxv_ctx_create(&MR_CTX) == ZXV_OK &&
              zxv_cap_create(MR_CTX, 1, NULL, ZXV_CAP_CRYPTO_SIGN, &MR_CAP) == ZXV_OK,
          "moraine: a signing capability for the test's own keys");
    mr_keygen(&MK_ROOT);
    for (int g = 0; g < 5; g++) mr_keygen(&MK_G[g]);
    mr_keygen(&MK_GW);
    mr_keygen(&MK_DEV);
    mr_keygen(&MK_NEW);
    snprintf(guardians, sizeof guardians, "%s%s%s%s%s", mr_pk(&MK_G[0]), mr_pk(&MK_G[1]),
             mr_pk(&MK_G[2]), mr_pk(&MK_G[3]), mr_pk(&MK_G[4]));

    memset(&ME, 0, sizeof ME);
    zxm_link_init(&MR_LINK, mr_route, NULL);
    moraine_init(m, &MR_LINK);

    /* nothing is asked before the guardians are set */
    int before = mr_sent;
    CHECK(moraine_claim(m, 0, 7, mr_pk(&MK_NEW), T0, true) == ZXM_E_ARG &&
              moraine_approve(m, 0, MR_H128, true) == ZXM_E_STATE &&
              moraine_release(m, T0, true) == ZXM_E_STATE && mr_sent == before,
          "moraine: nothing asked before the guardians are set");
    CHECK(moraine_refresh(m, T0) == ZXM_OK && !m->set_up, "moraine: no guardians yet");

    /* MM1: a hierarchy that loops, or no word from the owner: nothing sent */
    before = mr_sent;
    CHECK(moraine_setup(m, 210, mr_pk(&MK_ROOT), guardians, mr_pk(&MK_GW), mr_pk(&MK_DEV), LOCK,
                        LOOP, T0, true) == ZXM_E_ARG &&
              moraine_setup(m, 0, mr_pk(&MK_ROOT), guardians, mr_pk(&MK_GW), mr_pk(&MK_DEV), LOCK,
                            TREE, T0, true) == ZXM_E_ARG &&
              moraine_setup(m, 210, mr_pk(&MK_ROOT), "abc", mr_pk(&MK_GW), mr_pk(&MK_DEV), LOCK,
                            TREE, T0, true) == ZXM_E_ARG &&
              mr_sent == before,
          "moraine: a looping hierarchy or a short guardian list is refused before sending");
    CHECK(moraine_setup(m, 210, mr_pk(&MK_ROOT), guardians, mr_pk(&MK_GW), mr_pk(&MK_DEV), LOCK,
                        TREE, T0, false) == ZXM_E_STATE &&
              mr_sent == before && !m->set_up && !ME.set_up,
          "moraine: without the owner's word nothing is sent and nothing kept");
    CHECK(moraine_setup(m, 210, mr_pk(&MK_ROOT), guardians, mr_pk(&MK_GW), mr_pk(&MK_DEV), LOCK,
                        TREE, T0, true) == ZXM_OK &&
              m->set_up && m->timelock == LOCK && m->rank[3] == 3 && m->rank[4] == 1 &&
              !strcmp(m->state, "idle"),
          "moraine: five guardians set: %s", MR_LINK.err);

    /* MM2: keys by public id and counts only */
    CHECK(moraine_add_key(m, "abc", "Vault", 43, 64, 20, 4, 0, T0 + 365 * DAY, true, &k0) ==
                  ZXM_E_ARG &&
              moraine_add_key(m, mr_pk(&MK_NEW), "Vault", 43, 64, 11, 4, 0, T0, true, &k0) ==
                  ZXM_E_ARG &&
              moraine_add_key(m, mr_pk(&MK_NEW), "Vault", 43, 257, 20, 4, 0, T0, true, &k0) ==
                  ZXM_E_ARG &&
              moraine_add_key(m, mr_pk(&MK_NEW), "Vault", 43, 64, 20, 4, 0, T0, false, &k0) ==
                  ZXM_E_STATE &&
              m->nkey == 0,
          "moraine: a key outside the limits, or without the owner's word, is not kept");
    CHECK(moraine_add_key(m, mr_pk(&MK_NEW), "Vault", 43, 64, 20, 4, 0, T0 + 365 * DAY, true,
                          &k0) == ZXM_OK &&
              k0 == 0 && m->key[0].bits == 258 && moraine_key_score(m, 0) == 100 &&
              moraine_key_days(m, 0, T0) == 365,
          "moraine: a key of 258 bits held by 3 of 5: strength 100, 365 days left");
    CHECK(moraine_add_key(m, mr_pk(&MK_GW), "Door", 20, 10, 12, 1, 0, T0 + DAY, true, &k1) ==
                  ZXM_OK &&
              k1 == 1 && m->key[1].bits == 66 && moraine_key_score(m, 1) == 55 &&
              strstr(m->notice, "below 128") &&
              moraine_add_key(m, mr_pk(&MK_GW), "Again", 20, 10, 12, 1, 0, T0, true, &k1) ==
                  ZXM_E_ARG,
          "moraine: a short key is kept with advice; one id is kept once: %s", m->notice);

    /* MM3: grants, the access grid */
    CHECK(moraine_grant(m, 7, 0, 1, 1, 0, T0 + 7 * DAY, false) == ZXM_E_STATE && m->ngrant == 0 &&
              moraine_grant(m, 7, 0, 2, 1, 0, T0 + DAY, true) == ZXM_E_ARG &&
              moraine_grant(m, 0, 0, 1, 1, 0, T0 + DAY, true) == ZXM_E_ARG,
          "moraine: a grant needs the owner's word and flags that are flags");
    CHECK(moraine_grant(m, 7, 0, 1, 1, 0, T0 + 30 * DAY, true) == ZXM_OK &&
              moraine_grant(m, 8, 0, 1, 1, 1, T0 + 30 * DAY, true) == ZXM_OK &&
              moraine_grant(m, 7, 1, 1, 1, 0, T0 + 60 * DAY, true) == ZXM_OK && m->ngrant == 3 &&
              moraine_live_grants(m, 7, 0, T0) == 1 && moraine_live_grants(m, 8, 0, T0) == 0 &&
              moraine_live_grants(m, 9, 0, T0) == 0 &&
              moraine_live_grants(m, 7, 0, T0 + 30 * DAY) == 0,
          "moraine: the access grid: 7 live on key 0, 8 revoked, none past the window");

    /* MM4: a claim only by a requester with live access */
    before = mr_sent;
    CHECK(moraine_claim(m, 0, 8, mr_pk(&MK_NEW), T0, true) == ZXM_E_STATE &&
              moraine_claim(m, 0, 7, mr_pk(&MK_NEW), T0 + 31 * DAY, true) == ZXM_E_STATE &&
              moraine_claim(m, 0, 7, mr_pk(&MK_NEW), T0, false) == ZXM_E_STATE &&
              mr_sent == before && ME.state == 0,
          "moraine: no claim for a revoked grant, a closed window or without the owner's word");
    CHECK(moraine_claim(m, 0, 7, mr_pk(&MK_NEW), T0, true) == ZXM_OK &&
              !strcmp(m->state, "pending") && m->opened == T0 && ME.state == 1,
          "moraine: requester 7 asks for key 0: %s", MR_LINK.err);
    CHECK(moraine_claim(m, 1, 7, mr_pk(&MK_NEW), T0, true) == ZXM_E_STATE,
          "moraine: one claim at a time");

    /* the BOTH test: guardian 2 approves, guardian 4 objects within the wait */
    mr_signed(&MK_G[2], "approve", sig);
    CHECK(moraine_approve(m, 2, sig, false) == ZXM_E_STATE && ME.approved == 0 &&
              moraine_approve(m, 2, sig, true) == ZXM_OK && m->approvals == 1 &&
              moraine_claim_word(m) == LPRES_STATE_TRUE,
          "moraine: guardian 2 approves: %s", MR_LINK.err);
    before = mr_sent;
    CHECK(moraine_object(m, 4, T0 + LOCK, true) == ZXM_E_STATE &&
              moraine_object(m, 4, T0 + DAY, false) == ZXM_E_STATE &&
              moraine_claim_word(m) == LPRES_STATE_TRUE &&
              moraine_object(m, 4, T0 + DAY, true) == ZXM_OK && mr_sent == before &&
              moraine_claim_word(m) == LPRES_STATE_BOTH && strstr(m->notice, "Held"),
          "moraine: guardian 4 objects within the wait: BOTH, held: %s", m->notice);
    mr_signed(&MK_G[0], "approve", sig);
    CHECK(moraine_approve(m, 0, sig, true) == ZXM_OK, "moraine: guardian 0 approves");
    mr_signed(&MK_G[1], "approve", sig);
    CHECK(moraine_approve(m, 1, sig, true) == ZXM_OK && m->approvals == 3,
          "moraine: guardian 1 approves: a quorum, and still held");
    before = mr_sent;
    CHECK(!moraine_due(m, T0 + LOCK + 1) && !moraine_release_ready(m, T0 + LOCK + 1) &&
              moraine_release(m, T0 + LOCK + 1, true) == ZXM_E_STATE && mr_sent == before &&
              mr_finalized == 0 && !m->key[0].released && !strcmp(m->state, "pending") &&
              m->ncustody == 0 && strstr(m->notice, "neither released nor lost"),
          "moraine: BOTH: the key is neither released nor lost: %s", m->notice);

    /* MM8: the owner's device cancels; the key stays in escrow */
    mr_signed(&MK_DEV, "cancel", sig);
    CHECK(moraine_cancel(m, mr_pk(&MK_DEV), sig, false) == ZXM_E_STATE && ME.state == 1 &&
              moraine_cancel(m, mr_pk(&MK_DEV), sig, true) == ZXM_OK && !strcmp(m->state, "idle") &&
              !m->key[0].released && moraine_claim_word(m) == LPRES_STATE_NEITHER,
          "moraine: cancelled, the key still in escrow: %s", MR_LINK.err);

    /* a claim the guardians approve, released after the wait */
    const int64_t T1 = T0 + 10 * DAY;
    CHECK(moraine_claim(m, 0, 7, mr_pk(&MK_NEW), T1, true) == ZXM_OK, "moraine: asked again");
    for (uint32_t g = 0; g < 3u; g++) {
        mr_signed(&MK_G[g], "approve", sig);
        CHECK(moraine_approve(m, g, sig, true) == ZXM_OK, "moraine: guardian %u approves", g);
    }
    before = mr_sent;
    CHECK(moraine_claim_word(m) == LPRES_STATE_TRUE && !moraine_due(m, T1 + LOCK - 1) &&
              moraine_release(m, T1 + LOCK - 1, true) == ZXM_E_STATE &&
              moraine_release(m, T1 + LOCK, false) == ZXM_E_STATE && mr_sent == before &&
              mr_finalized == 0,
          "moraine: before the wait, or without the owner's word, nothing is released");
    CHECK(moraine_due(m, T1 + LOCK) && moraine_release_ready(m, T1 + LOCK) &&
              moraine_release(m, T1 + LOCK, true) == ZXM_OK && mr_finalized == 1 &&
              m->key[0].released && m->ncustody == 1 && m->custody[0].number == 1 &&
              m->custody[0].prev == 0 && m->custody[0].guardian == 2 &&
              m->custody[0].stamp == T1 + LOCK && m->custody[0].requester == 7 &&
              m->custody[0].seq == 0 && strstr(m->notice, "no journal") &&
              moraine_register_intact(m) == LPRES_STATE_TRUE,
          "moraine: released, register entry 1 by guardian 2: %s", m->notice);
    CHECK(moraine_refresh(m, T1 + LOCK) == ZXM_OK && !strcmp(m->state, "thawing") &&
              moraine_claim(m, 0, 7, mr_pk(&MK_NEW), T1 + LOCK, true) == ZXM_E_STATE,
          "moraine: a released key is not asked for again");

    /* a device's veto: 4 approvals and 14 days (zxvd's rule), stamped by the journal */
    snprintf(jdir, sizeof jdir, "%s/zxv-moraine-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "moraine: a journal");
    const int64_t T2 = T1 + 20 * DAY;
    ME.state = 0; /* the engine's thaw is over */
    CHECK(moraine_refresh(m, T2) == ZXM_OK &&
              moraine_claim(m, 1, 7, mr_pk(&MK_NEW), T2, true) == ZXM_OK,
          "moraine: requester 7 asks for key 1: %s", MR_LINK.err);
    mr_signed(&MK_DEV, "veto", sig);
    CHECK(moraine_veto(m, mr_pk(&MK_DEV), sig, true) == ZXM_OK && m->vetoed && m->needed == 4,
          "moraine: a device vetoes: %s", m->notice);
    for (uint32_t g = 0; g < 3u; g++) {
        mr_signed(&MK_G[g], "approve", sig);
        CHECK(moraine_approve(m, g, sig, true) == ZXM_OK, "moraine: guardian %u approves", g);
    }
    CHECK(!moraine_due(m, T2 + 14 * DAY) && moraine_release(m, T2 + 14 * DAY, true) == ZXM_E_STATE,
          "moraine: after a veto, 3 approvals are not enough");
    mr_signed(&MK_G[3], "approve", sig);
    CHECK(moraine_approve(m, 3, sig, true) == ZXM_OK && m->approvals == 4 &&
              !moraine_due(m, T2 + 14 * DAY - 1) && moraine_due(m, T2 + 14 * DAY),
          "moraine: 4 approvals, due after 14 days");
    CHECK(moraine_release(m, T2 + 14 * DAY, true) == ZXM_OK && mr_finalized == 2 &&
              m->ncustody == 2 && m->custody[1].number == 2 && m->custody[1].prev == 1 &&
              m->custody[1].guardian == 3 && m->custody[1].seq > 0 &&
              moraine_register_intact(m) == LPRES_STATE_TRUE,
          "moraine: released, register entry 2 stamped with journal seq %lld",
          (long long) m->custody[1].seq);
    zjr_close();
    CHECK(rm_tree(jdir) == 0, "moraine: journal cleanup");

    /* the register is checked whole: a reused number breaks it */
    m->custody[1].number = 1;
    CHECK(moraine_register_intact(m) == LPRES_STATE_FALSE, "moraine: a reused number breaks it");
    m->custody[1].number = 2;
    CHECK(moraine_register_intact(m) == LPRES_STATE_TRUE, "moraine: the register whole again");
    zxv_cap_destroy(MR_CAP);
    zxv_ctx_destroy(MR_CTX);
}
