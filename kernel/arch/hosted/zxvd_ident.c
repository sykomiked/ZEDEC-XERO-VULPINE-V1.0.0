/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_ident.c — passkeys.*: zxvd as a WebAuthn-shaped relying party for
 * ML-DSA-65 passkeys (kernel/src/ident, id_passkey.c), behind the
 * "passkeys" feature switch (zxvd_ext.h X3; authentication only, so not
 * regulated; off by default like every feature).
 *
 * THE CEREMONIES
 *   A1  register_begin / auth_begin hand out a 32-byte challenge from the OS
 *       with the rp id, the origin and a deadline (300 s). A challenge is
 *       single-use: its finish call consumes it whether it succeeds or not.
 *       At most 16 are open; a new one replaces an expired one, else the
 *       oldest.
 *   A2  register_finish takes the authenticator's authenticatorData with
 *       attested credential data (COSE key {1:7, 3:-49}); attestation format
 *       "none", so the authenticator model is not proven, and the challenge
 *       binds the ceremony only through zxvd's own bookkeeping.
 *   A3  auth_finish rebuilds clientDataJSON from the challenge and the
 *       origin given at auth_begin, and verifies the ML-DSA-65 signature
 *       over authenticatorData || SHA-256(clientDataJSON). The signature
 *       counter must increase (a cloned key shows up as a replay).
 *   A4  zxvd stores public keys only. Passkey private keys live on the
 *       authenticator and never travel on this socket; there is no secret
 *       to return. A revoked credential stays listed and never logs in.
 *   A5  In memory only (32 credentials): a restart forgets them.
 *   A6  Errors: -32602 bad params, ZXVD_E_PASSKEYS (-32041) a refused
 *       ceremony (the message names the reason).
 * Not served here: ident's root identity, recovery (Argon2id, BCH, Shamir,
 * guardians) and KYC, which have no wire format for zxvd yet.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "zxvd_groups.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "ident.h"

#define ZXVD_E_PASSKEYS (-32041)
#define MAX_CREDS       32u
#define MAX_CHAL        16u
#define CHAL_LEN        32u
#define CHAL_SECS       300

typedef struct {
    bool used, revoked;
    id_rp_cred_t cred;
    uint8_t user[16];
    char rp_id[ID_RP_MAX + 1];
    int64_t registered, last_used;
} cred_t;

typedef struct {
    bool used, create, require_uv, has_user;
    uint8_t chal[CHAL_LEN];
    uint8_t user[16];
    char rp_id[ID_RP_MAX + 1];
    char origin[ID_RP_MAX + 1];
    int64_t expires;
    uint32_t seq;
} chal_t;

static struct {
    cred_t creds[MAX_CREDS];
    chal_t chal[MAX_CHAL];
    uint32_t seq;
    uint8_t auth[ID_AUTHDATA_MAX];
    uint8_t sig[ID_MLDSA65_SIG];
} P;

static int (*RANDOM)(uint8_t *p, size_t n) = zxv_os_random;

void zxvd_passkeys_set_random(int (*fn)(uint8_t *p, size_t n))
{
    RANDOM = fn ? fn : zxv_os_random;
}

static const char *why(id_status_t st)
{
    switch (st) {
    case ID_ERR_FORMAT:
        return "malformed authenticator data";
    case ID_ERR_AUTH:
        return "the authenticator data or signature does not verify";
    case ID_ERR_REPLAY:
        return "the signature counter did not increase (replay or cloned key)";
    default:
        return "bad argument";
    }
}

static bool p_now(zxvd_call_t *c, int64_t *now)
{
    if (!zxvd_p_has(c, "now")) {
        *now = (int64_t) time(NULL);
        return true;
    }
    if (!zxvd_p_i64(c, "now", now)) return false;
    if (*now >= 0) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"now\" must be unix seconds");
    return false;
}

static bool p_hex_exact(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t len, bool required)
{
    uint32_t n;
    if (!zxvd_p_hex(c, key, out, len, &n, required)) return false;
    if (n == len || (!required && n == 0 && !zxvd_p_has(c, key))) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes of hex", key, len);
    return false;
}

/* rp_id (default "zxv.id"): 1..64 bytes, printable, no quote or backslash */
static bool text_ok(const char *s)
{
    for (; *s; s++)
        if (*s < 0x20 || *s > 0x7e || *s == '"' || *s == '\\') return false;
    return true;
}

static bool p_rp(zxvd_call_t *c, char rp[ID_RP_MAX + 1])
{
    if (!zxvd_p_str(c, "rp_id", rp, ID_RP_MAX + 1, false)) return false;
    if (!rp[0]) memcpy(rp, ID_PLATFORM_RP, sizeof ID_PLATFORM_RP);
    if (text_ok(rp)) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"rp_id\" must be printable text");
    return false;
}

static void put_cred(zxvd_buf_t *b, const cred_t *k)
{
    zxvd_buf_raw(b, "{\"cred_id\":", 11);
    zxvd_buf_hex(b, k->cred.cred_id, ID_CRED_ID);
    zxvd_buf_raw(b, ",\"user\":", 8);
    zxvd_buf_hex(b, k->user, 16);
    zxvd_buf_raw(b, ",\"rp_id\":", 9);
    zxvd_buf_json_str(b, k->rp_id);
    zxvd_buf_printf(b, ",\"sign_count\":%u,\"revoked\":%s,\"registered\":%lld,\"last_used\":",
                    (unsigned) k->cred.sign_count, k->revoked ? "true" : "false",
                    (long long) k->registered);
    if (k->last_used >= 0)
        zxvd_buf_printf(b, "%lld}", (long long) k->last_used);
    else
        zxvd_buf_raw(b, "null}", 5);
}

static cred_t *find_cred(const uint8_t id[ID_CRED_ID])
{
    for (uint32_t i = 0; i < MAX_CREDS; i++)
        if (P.creds[i].used && !memcmp(P.creds[i].cred.cred_id, id, ID_CRED_ID)) return &P.creds[i];
    return NULL;
}

/* A1: a free slot, else an expired one, else the oldest */
static chal_t *chal_slot(int64_t now)
{
    chal_t *best = &P.chal[0];
    for (uint32_t i = 0; i < MAX_CHAL; i++) {
        chal_t *h = &P.chal[i];
        if (!h->used || h->expires <= now) return h;
        if (h->seq < best->seq) best = h;
    }
    return best;
}

static void m_begin(zxvd_call_t *c, bool create)
{
    char rp[ID_RP_MAX + 1], origin[ID_RP_MAX + 1];
    uint8_t user[16] = {0};
    bool uv;
    int64_t now;
    if (!p_rp(c, rp) || !p_hex_exact(c, "user", user, 16, create) ||
        !zxvd_p_bool(c, "require_uv", true, &uv) ||
        !zxvd_p_str(c, "origin", origin, sizeof origin, false) || !p_now(c, &now))
        return;
    if (!origin[0]) {
        if (strlen(rp) > ID_RP_MAX - 8u)
            return zxvd_fail(c, ZXVD_E_PARAMS, "give \"origin\" for an rp_id this long");
        memcpy(origin, "https://", 8);
        memcpy(origin + 8, rp, strlen(rp) + 1);
    } else if (!text_ok(origin)) {
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"origin\" must be printable text");
    }
    chal_t *h = chal_slot(now);
    memset(h, 0, sizeof *h);
    if (RANDOM(h->chal, CHAL_LEN) != 0) return zxvd_fail(c, ZXVD_E_INTERNAL, "no random bytes");
    h->used = true;
    h->create = create;
    h->require_uv = uv;
    h->has_user = zxvd_p_has(c, "user");
    memcpy(h->user, user, 16);
    memcpy(h->rp_id, rp, sizeof rp);
    memcpy(h->origin, origin, sizeof origin);
    h->expires = now + CHAL_SECS;
    h->seq = ++P.seq;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b,
                    "{\"type\":\"%s\",\"challenge\":", create ? "webauthn.create" : "webauthn.get");
    zxvd_buf_hex(b, h->chal, CHAL_LEN);
    zxvd_buf_raw(b, ",\"rp_id\":", 9);
    zxvd_buf_json_str(b, rp);
    zxvd_buf_raw(b, ",\"origin\":", 10);
    zxvd_buf_json_str(b, origin);
    zxvd_buf_printf(b, ",\"alg\":%d,\"require_uv\":%s,\"expires\":%lld", ID_COSE_MLDSA65,
                    uv ? "true" : "false", (long long) h->expires);
    if (create) {
        zxvd_buf_raw(b, ",\"user\":", 8);
        zxvd_buf_hex(b, user, 16);
        zxvd_buf_raw(b, "}", 1);
        return;
    }
    zxvd_buf_raw(b, ",\"allow\":[", 10); /* the user's live credentials for this rp */
    bool first = true;
    for (uint32_t i = 0; h->has_user && i < MAX_CREDS; i++) {
        const cred_t *k = &P.creds[i];
        if (!k->used || k->revoked || memcmp(k->user, user, 16) || strcmp(k->rp_id, rp)) continue;
        if (!first) zxvd_buf_raw(b, ",", 1);
        zxvd_buf_hex(b, k->cred.cred_id, ID_CRED_ID);
        first = false;
    }
    zxvd_buf_raw(b, "]}", 2);
}

static void m_register_begin(zxvd_call_t *c)
{
    m_begin(c, true);
}

static void m_auth_begin(zxvd_call_t *c)
{
    m_begin(c, false);
}

/* A1: find and consume the challenge; NULL after failing the call */
static chal_t *take(zxvd_call_t *c, bool create, int64_t now, chal_t *out)
{
    uint8_t ch[CHAL_LEN];
    if (!p_hex_exact(c, "challenge", ch, CHAL_LEN, true)) return NULL;
    for (uint32_t i = 0; i < MAX_CHAL; i++) {
        chal_t *h = &P.chal[i];
        if (!h->used || memcmp(h->chal, ch, CHAL_LEN)) continue;
        *out = *h;
        memset(h, 0, sizeof *h);
        if (out->create != create) break;
        if (out->expires <= now) {
            zxvd_fail(c, ZXVD_E_PASSKEYS, "the challenge has expired");
            return NULL;
        }
        return out;
    }
    zxvd_fail(c, ZXVD_E_PASSKEYS, "unknown or used challenge");
    return NULL;
}

static void m_register_finish(zxvd_call_t *c)
{
    uint32_t n;
    int64_t now;
    chal_t h;
    cred_t k;
    if (!p_now(c, &now) || !zxvd_p_hex(c, "auth", P.auth, sizeof P.auth, &n, true) ||
        !take(c, true, now, &h))
        return;
    id_status_t st = id_rp_register(&k.cred, h.rp_id, P.auth, n, h.require_uv);
    if (st != ID_OK) return zxvd_fail(c, ZXVD_E_PASSKEYS, "register: %s", why(st));
    if (find_cred(k.cred.cred_id))
        return zxvd_fail(c, ZXVD_E_PASSKEYS, "register: that credential is already registered");
    cred_t *slot = NULL;
    for (uint32_t i = 0; !slot && i < MAX_CREDS; i++)
        if (!P.creds[i].used) slot = &P.creds[i];
    if (!slot) return zxvd_fail(c, ZXVD_E_PASSKEYS, "register: the credential table is full");
    k.used = true;
    k.revoked = false;
    memcpy(k.user, h.user, 16);
    memcpy(k.rp_id, h.rp_id, sizeof k.rp_id);
    k.registered = now;
    k.last_used = -1;
    *slot = k;
    put_cred(zxvd_result(c), slot);
}

static void m_auth_finish(zxvd_call_t *c)
{
    uint8_t id[ID_CRED_ID], cdh[ID_HASH];
    uint32_t n;
    int64_t now;
    chal_t h;
    if (!p_now(c, &now) || !p_hex_exact(c, "cred_id", id, ID_CRED_ID, true) ||
        !zxvd_p_hex(c, "auth", P.auth, ID_AUTHDATA_MIN, &n, true) ||
        !p_hex_exact(c, "sig", P.sig, ID_MLDSA65_SIG, true) || !take(c, false, now, &h))
        return;
    cred_t *k = find_cred(id);
    if (!k || strcmp(k->rp_id, h.rp_id) || (h.has_user && memcmp(k->user, h.user, 16)))
        return zxvd_fail(c, ZXVD_E_PASSKEYS, "login: unknown credential for this challenge");
    if (k->revoked) return zxvd_fail(c, ZXVD_E_PASSKEYS, "login: the credential is revoked");
    id_status_t st = id_client_data_hash("webauthn.get", h.chal, CHAL_LEN, h.origin, cdh);
    if (st == ID_OK) st = id_rp_verify(&k->cred, h.rp_id, P.auth, n, cdh, P.sig, h.require_uv);
    if (st != ID_OK) return zxvd_fail(c, ZXVD_E_PASSKEYS, "login: %s", why(st));
    k->last_used = now;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"verified\":true,\"uv\":%s,\"credential\":",
                    P.auth[32] & ID_AUTH_UV ? "true" : "false");
    put_cred(b, k);
    zxvd_buf_raw(b, "}", 1);
}

static void m_list(zxvd_call_t *c)
{
    uint8_t user[16] = {0};
    if (!p_hex_exact(c, "user", user, 16, false)) return;
    bool any = !zxvd_p_has(c, "user");
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"credentials\":[", 16);
    bool first = true;
    for (uint32_t i = 0; i < MAX_CREDS; i++) {
        const cred_t *k = &P.creds[i];
        if (!k->used || (!any && memcmp(k->user, user, 16))) continue;
        if (!first) zxvd_buf_raw(b, ",", 1);
        put_cred(b, k);
        first = false;
    }
    zxvd_buf_raw(b, "]}", 2);
}

static void m_revoke(zxvd_call_t *c)
{
    uint8_t id[ID_CRED_ID];
    if (!p_hex_exact(c, "cred_id", id, ID_CRED_ID, true)) return;
    cred_t *k = find_cred(id);
    if (!k) return zxvd_fail(c, ZXVD_E_PASSKEYS, "revoke: unknown credential");
    if (k->revoked) return zxvd_fail(c, ZXVD_E_PASSKEYS, "revoke: already revoked");
    k->revoked = true;
    put_cred(zxvd_result(c), k);
}

static void m_status(zxvd_call_t *c)
{
    uint32_t creds = 0, active = 0, open = 0;
    int64_t now;
    if (!p_now(c, &now)) return;
    for (uint32_t i = 0; i < MAX_CREDS; i++) {
        creds += P.creds[i].used;
        active += P.creds[i].used && !P.creds[i].revoked;
    }
    for (uint32_t i = 0; i < MAX_CHAL; i++) open += P.chal[i].used && P.chal[i].expires > now;
    zxvd_buf_printf(zxvd_result(c),
                    "{\"rp_id\":\"%s\",\"alg\":%d,\"attestation\":\"none\",\"credentials\":%u,"
                    "\"active\":%u,\"open_challenges\":%u,\"max_credentials\":%u,"
                    "\"in_memory\":true}",
                    ID_PLATFORM_RP, ID_COSE_MLDSA65, (unsigned) creds, (unsigned) active,
                    (unsigned) open, MAX_CREDS);
}

static void passkeys_close(void)
{
    memset(&P, 0, sizeof P); /* A5: in memory only */
}

static const zxvd_group_method_t METHODS[] = {
    {"passkeys.status", m_status, true},
    {"passkeys.register_begin", m_register_begin, false},
    {"passkeys.register_finish", m_register_finish, false},
    {"passkeys.auth_begin", m_auth_begin, false},
    {"passkeys.auth_finish", m_auth_finish, false},
    {"passkeys.list", m_list, false},
    {"passkeys.revoke", m_revoke, false},
};

static const zxvd_group_t GROUP = {
    "passkeys",
    false,
    "ML-DSA-65 passkeys, WebAuthn-shaped (kernel/src/ident): zxvd is the relying party; "
    "public keys only, in memory",
    METHODS,
    sizeof METHODS / sizeof METHODS[0],
    passkeys_close};

const zxvd_group_t *zxvd_passkeys_group(void)
{
    return &GROUP;
}
