/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_oauth.c — OAuth 2.0 / OIDC client state machines: authorization code
 * with PKCE S256 (RFC 6749 4.1, RFC 7636) and the device authorization grant
 * (RFC 8628). Pure state + codecs; the host sends the HTTP bytes. */
#include "web4_web2.h"

#define TOK_TOKS 64u

bool w4_pkce_verifier_ok(const char *v, uint32_t len)
{
    if (!v || len < 43 || len > W4_PKCE_VERIFIER_MAX) return false;
    for (uint32_t i = 0; i < len; i++) {
        char c = v[i];
        if (!(w4_is_alpha(c) || w4_is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~'))
            return false;
    }
    return true;
}

int w4_pkce_s256(const char *verifier, uint32_t len, char out[W4_PKCE_CHALLENGE + 1])
{
    if (!w4_pkce_verifier_ok(verifier, len)) return W4_ERR_ARG;
    uint8_t h[W4_HASH_LEN];
    w4_sha256((const uint8_t *) verifier, len, h);
    return w4_b64url_encode(h, W4_HASH_LEN, out, W4_PKCE_CHALLENGE + 1) == 43 ? W4_OK
                                                                              : W4_ERR_SPACE;
}

int w4_pkce_verifier_from_random(const uint8_t rnd[32], char out[W4_PKCE_CHALLENGE + 1])
{
    return w4_b64url_encode(rnd, 32, out, W4_PKCE_CHALLENGE + 1) == 43 ? W4_OK : W4_ERR_SPACE;
}

static void fail(w4_oauth_t *f, const char *err)
{
    f->st = W4_OA_FAILED;
    w4_strlcpy(f->error, err, sizeof f->error);
}

int w4_oauth_code_begin(w4_oauth_t *f, const w4_oauth_cfg_t *cfg, const uint8_t rnd[64])
{
    if (!f || !cfg || !rnd || !cfg->client_id[0] || !cfg->redirect_uri[0] ||
        !cfg->authz_endpoint[0])
        return W4_ERR_ARG;
    w4_memset(f, 0, sizeof *f);
    f->cfg = *cfg;
    if (w4_b64url_encode(rnd, 16, f->state, sizeof f->state) < 0) return W4_ERR_SPACE;
    if (w4_b64url_encode(rnd + 16, 16, f->nonce, sizeof f->nonce) < 0) return W4_ERR_SPACE;
    if (w4_pkce_verifier_from_random(rnd + 32, f->verifier)) return W4_ERR_SPACE;
    f->st = W4_OA_AUTHORIZING;
    return W4_OK;
}

int32_t w4_oauth_code_auth_url(const w4_oauth_t *f, char *out, uint32_t cap)
{
    if (!f || f->st != W4_OA_AUTHORIZING) return W4_ERR_STATE;
    char ch[W4_PKCE_CHALLENGE + 1];
    if (w4_pkce_s256(f->verifier, (uint32_t) w4_strnlen(f->verifier, sizeof f->verifier), ch))
        return W4_ERR_STATE;
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_str(&w, f->cfg.authz_endpoint);
    bool has_q = false;
    for (const char *p = f->cfg.authz_endpoint; *p; p++)
        if (*p == '?') has_q = true;
    w4_w_byte(&w, has_q ? '&' : '?');
    bool first = true;
    w4_w_form(&w, &first, "response_type", "code");
    w4_w_form(&w, &first, "client_id", f->cfg.client_id);
    w4_w_form(&w, &first, "redirect_uri", f->cfg.redirect_uri);
    if (f->cfg.scope[0]) w4_w_form(&w, &first, "scope", f->cfg.scope);
    w4_w_form(&w, &first, "state", f->state);
    w4_w_form(&w, &first, "nonce", f->nonce);
    w4_w_form(&w, &first, "code_challenge", ch);
    w4_w_form(&w, &first, "code_challenge_method", "S256");
    return w4_w_cstr(&w);
}

int w4_oauth_code_callback(w4_oauth_t *f, const char *query, uint32_t qlen)
{
    if (!f || !query) return W4_ERR_ARG;
    if (f->st != W4_OA_AUTHORIZING) return W4_ERR_STATE;
    char st[64];
    int32_t sl = w4_form_get(query, qlen, "state", st, sizeof st);
    size_t want = w4_strnlen(f->state, sizeof f->state);
    if (sl < 0 || (size_t) sl != want || !w4_ct_eq(st, f->state, want)) {
        fail(f, "state_mismatch"); /* CSRF / mix-up: never continue */
        return W4_ERR_SIG;
    }
    char err[64];
    if (w4_form_get(query, qlen, "error", err, sizeof err) >= 0) {
        fail(f, err);
        return W4_ERR_DENIED;
    }
    if (w4_form_get(query, qlen, "code", f->code, sizeof f->code) <= 0) {
        fail(f, "missing_code");
        return W4_ERR_PARSE;
    }
    f->st = W4_OA_HAVE_CODE;
    return W4_OK;
}

static int32_t form_post(const w4_oauth_t *f, const char *path, const char *body, uint32_t blen,
                         uint8_t *out, uint32_t cap)
{
    w4_hdr_t h[2] = {w4_hdr("Content-Type", "application/x-www-form-urlencoded"),
                     w4_hdr("Accept", "application/json")};
    w4_w w;
    w4_w_init(&w, out, cap);
    return w4_http_build_request(&w, "POST", f->cfg.token_host, path, h, 2, (const uint8_t *) body,
                                 blen);
}

int32_t w4_oauth_code_token_request(w4_oauth_t *f, uint8_t *out, uint32_t cap)
{
    if (!f || f->st != W4_OA_HAVE_CODE) return W4_ERR_STATE;
    char body[2048];
    w4_w w;
    w4_w_init(&w, body, sizeof body);
    bool first = true;
    w4_w_form(&w, &first, "grant_type", "authorization_code");
    w4_w_form(&w, &first, "code", f->code);
    w4_w_form(&w, &first, "redirect_uri", f->cfg.redirect_uri);
    w4_w_form(&w, &first, "client_id", f->cfg.client_id);
    w4_w_form(&w, &first, "code_verifier", f->verifier);
    int32_t bl = w4_w_cstr(&w);
    if (bl < 0) return bl;
    int32_t r = form_post(f, f->cfg.token_path, body, (uint32_t) bl, out, cap);
    if (r >= 0) f->st = W4_OA_TOKEN_REQUESTED;
    return r;
}

/* Parse a token endpoint body. Returns W4_OK on success; on an OAuth error
 * response copies "error" into f->error and returns W4_ERR_DENIED. */
static int parse_token_body(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                            uint64_t now_s)
{
    w4_jtok_t t[TOK_TOKS];
    int32_t n = w4_json_parse(json, len, t, TOK_TOKS, 4);
    if (n <= 0 || t[0].type != W4_J_OBJ) return W4_ERR_PARSE;
    if (status != 200) {
        if (w4_json_get_str(json, t, n, 0, "error", f->error, sizeof f->error))
            w4_strlcpy(f->error, "http_error", sizeof f->error);
        return W4_ERR_DENIED;
    }
    if (w4_json_get_str(json, t, n, 0, "access_token", f->access_token, sizeof f->access_token))
        return W4_ERR_PARSE;
    if (w4_json_get_str(json, t, n, 0, "token_type", f->token_type, sizeof f->token_type))
        return W4_ERR_PARSE;
    if (!w4_casecmp_eq(f->token_type, w4_strnlen(f->token_type, sizeof f->token_type), "bearer", 6))
        return W4_ERR_UNSUPP;
    uint64_t ein = 0;
    int r = w4_json_get_u64(json, t, n, 0, "expires_in", &ein);
    if (r == W4_OK)
        f->expires_at = now_s + ein;
    else if (r != W4_ERR_NOTFOUND)
        return W4_ERR_PARSE;
    r = w4_json_get_str(json, t, n, 0, "refresh_token", f->refresh_token, sizeof f->refresh_token);
    if (r != W4_OK && r != W4_ERR_NOTFOUND) return W4_ERR_PARSE;
    r = w4_json_get_str(json, t, n, 0, "id_token", f->id_token, sizeof f->id_token);
    if (r != W4_OK && r != W4_ERR_NOTFOUND) return W4_ERR_PARSE;
    r = w4_json_get_str(json, t, n, 0, "scope", f->granted_scope, sizeof f->granted_scope);
    if (r != W4_OK && r != W4_ERR_NOTFOUND) return W4_ERR_PARSE;
    return W4_OK;
}

int w4_oauth_token_response(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                            uint64_t now_s)
{
    if (!f || !json) return W4_ERR_ARG;
    if (f->st != W4_OA_TOKEN_REQUESTED) return W4_ERR_STATE;
    int r = parse_token_body(f, status, json, len, now_s);
    if (r == W4_OK) {
        f->st = W4_OA_DONE;
        w4_memset(f->verifier, 0, sizeof f->verifier); /* single use */
        w4_memset(f->code, 0, sizeof f->code);
    } else {
        if (r != W4_ERR_DENIED) w4_strlcpy(f->error, "bad_token_response", sizeof f->error);
        f->st = W4_OA_FAILED;
    }
    return r;
}

/* ===== Device authorization grant ===== */
int w4_oauth_device_begin(w4_oauth_t *f, const w4_oauth_cfg_t *cfg)
{
    if (!f || !cfg || !cfg->client_id[0] || !cfg->token_host[0] || !cfg->device_path[0])
        return W4_ERR_ARG;
    w4_memset(f, 0, sizeof *f);
    f->cfg = *cfg;
    f->st = W4_OA_IDLE;
    return W4_OK;
}

int32_t w4_oauth_device_request(const w4_oauth_t *f, uint8_t *out, uint32_t cap)
{
    if (!f || f->st != W4_OA_IDLE) return W4_ERR_STATE;
    char body[512];
    w4_w w;
    w4_w_init(&w, body, sizeof body);
    bool first = true;
    w4_w_form(&w, &first, "client_id", f->cfg.client_id);
    if (f->cfg.scope[0]) w4_w_form(&w, &first, "scope", f->cfg.scope);
    int32_t bl = w4_w_cstr(&w);
    if (bl < 0) return bl;
    return form_post(f, f->cfg.device_path, body, (uint32_t) bl, out, cap);
}

int w4_oauth_device_response(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                             uint64_t now_s)
{
    if (!f || !json) return W4_ERR_ARG;
    if (f->st != W4_OA_IDLE) return W4_ERR_STATE;
    w4_jtok_t t[TOK_TOKS];
    int32_t n = w4_json_parse(json, len, t, TOK_TOKS, 4);
    if (n <= 0 || t[0].type != W4_J_OBJ) {
        fail(f, "bad_device_response");
        return W4_ERR_PARSE;
    }
    if (status != 200) {
        char e[64];
        if (w4_json_get_str(json, t, n, 0, "error", e, sizeof e)) w4_strlcpy(e, "http_error", 64);
        fail(f, e);
        return W4_ERR_DENIED;
    }
    uint64_t ein = 0, iv = 5;
    if (w4_json_get_str(json, t, n, 0, "device_code", f->device_code, sizeof f->device_code) ||
        w4_json_get_str(json, t, n, 0, "user_code", f->user_code, sizeof f->user_code) ||
        w4_json_get_str(json, t, n, 0, "verification_uri", f->verification_uri,
                        sizeof f->verification_uri) ||
        w4_json_get_u64(json, t, n, 0, "expires_in", &ein) || ein == 0) {
        fail(f, "bad_device_response");
        return W4_ERR_PARSE;
    }
    int r = w4_json_get_str(json, t, n, 0, "verification_uri_complete",
                            f->verification_uri_complete, sizeof f->verification_uri_complete);
    if (r != W4_OK && r != W4_ERR_NOTFOUND) {
        fail(f, "bad_device_response");
        return W4_ERR_PARSE;
    }
    r = w4_json_get_u64(json, t, n, 0, "interval", &iv);
    if (r == W4_ERR_NOTFOUND)
        iv = 5; /* RFC 8628 3.2 default */
    else if (r != W4_OK || iv == 0 || iv > 3600) {
        fail(f, "bad_device_response");
        return W4_ERR_PARSE;
    }
    f->interval_s = iv;
    f->device_expires_at = now_s + ein;
    f->next_poll_at = now_s + iv;
    f->st = W4_OA_DEVICE_PENDING;
    return W4_OK;
}

bool w4_oauth_device_poll_due(const w4_oauth_t *f, uint64_t now_s)
{
    return f && f->st == W4_OA_DEVICE_PENDING && now_s >= f->next_poll_at;
}

int32_t w4_oauth_device_poll_request(w4_oauth_t *f, uint8_t *out, uint32_t cap, uint64_t now_s)
{
    if (!f || f->st != W4_OA_DEVICE_PENDING) return W4_ERR_STATE;
    if (now_s >= f->device_expires_at) {
        fail(f, "expired_token");
        return W4_ERR_EXPIRED;
    }
    if (!w4_oauth_device_poll_due(f, now_s)) return W4_ERR_PENDING;
    char body[768];
    w4_w w;
    w4_w_init(&w, body, sizeof body);
    bool first = true;
    w4_w_form(&w, &first, "grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    w4_w_form(&w, &first, "device_code", f->device_code);
    w4_w_form(&w, &first, "client_id", f->cfg.client_id);
    int32_t bl = w4_w_cstr(&w);
    if (bl < 0) return bl;
    f->next_poll_at = now_s + f->interval_s;
    return form_post(f, f->cfg.token_path, body, (uint32_t) bl, out, cap);
}

int w4_oauth_device_poll_response(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                                  uint64_t now_s)
{
    if (!f || !json) return W4_ERR_ARG;
    if (f->st != W4_OA_DEVICE_PENDING) return W4_ERR_STATE;
    int r = parse_token_body(f, status, json, len, now_s);
    if (r == W4_OK) {
        f->st = W4_OA_DONE;
        w4_memset(f->device_code, 0, sizeof f->device_code);
        return W4_OK;
    }
    if (r != W4_ERR_DENIED) {
        fail(f, "bad_token_response");
        return r;
    }
    if (w4_streq(f->error, "authorization_pending")) {
        f->error[0] = 0;
        if (now_s >= f->device_expires_at) {
            fail(f, "expired_token");
            return W4_ERR_EXPIRED;
        }
        return W4_ERR_PENDING;
    }
    if (w4_streq(f->error, "slow_down")) {
        f->error[0] = 0;
        f->interval_s += 5; /* RFC 8628 3.5 */
        f->next_poll_at = now_s + f->interval_s;
        return W4_ERR_PENDING;
    }
    if (w4_streq(f->error, "expired_token")) {
        f->st = W4_OA_FAILED;
        return W4_ERR_EXPIRED;
    }
    f->st = W4_OA_FAILED; /* access_denied and anything unknown */
    return W4_ERR_DENIED;
}
