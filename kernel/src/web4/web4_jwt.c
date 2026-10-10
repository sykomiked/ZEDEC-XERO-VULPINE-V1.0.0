/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_jwt.c — JWS compact parse/verify (RFC 7515), JWT claims (RFC 7519,
 * OIDC Core 3.1.3.7), and HMAC-SHA256 webhooks. */
#include "web4_web2.h"

#define HDR_MAX  1024u
#define HDR_TOKS 48u
#define PAY_TOKS 160u

int w4_jws_parse(const char *c, uint32_t len, w4_jws_t *j)
{
    if (!c || !j) return W4_ERR_ARG;
    w4_memset(j, 0, sizeof *j);
    uint32_t d1 = 0, d2 = 0, dots = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (c[i] == '.') {
            dots++;
            if (dots == 1)
                d1 = i;
            else if (dots == 2)
                d2 = i;
        }
    }
    if (dots != 2 || d1 == 0 || d2 == d1 + 1 || d2 + 1 >= len) return W4_ERR_PARSE;

    uint8_t hdr[HDR_MAX];
    int32_t hl = w4_b64url_decode(c, d1, hdr, sizeof hdr);
    if (hl <= 0) return W4_ERR_PARSE;
    w4_jtok_t t[HDR_TOKS];
    int32_t n = w4_json_parse((const char *) hdr, (uint32_t) hl, t, HDR_TOKS, 4);
    if (n <= 0 || t[0].type != W4_J_OBJ) return W4_ERR_PARSE;
    const char *hs = (const char *) hdr;
    if (w4_json_get_str(hs, t, n, 0, "alg", j->alg, sizeof j->alg)) return W4_ERR_PARSE;
    if (w4_casecmp_eq(j->alg, w4_strnlen(j->alg, sizeof j->alg), "none", 4)) return W4_ERR_PARSE;
    if (w4_json_get(hs, t, n, 0, "crit") != W4_ERR_NOTFOUND) return W4_ERR_UNSUPP;
    int r = w4_json_get_str(hs, t, n, 0, "typ", j->typ, sizeof j->typ);
    if (r != W4_OK && r != W4_ERR_NOTFOUND) return W4_ERR_PARSE;
    r = w4_json_get_str(hs, t, n, 0, "kid", j->kid, sizeof j->kid);
    if (r != W4_OK && r != W4_ERR_NOTFOUND) return W4_ERR_PARSE;

    int32_t pl = w4_b64url_decode(c + d1 + 1, d2 - d1 - 1, j->payload, sizeof j->payload);
    if (pl < 0) return pl == W4_ERR_SPACE ? W4_ERR_SPACE : W4_ERR_PARSE;
    j->payload_len = (uint32_t) pl;
    int32_t sl = w4_b64url_decode(c + d2 + 1, len - d2 - 1, j->sig, sizeof j->sig);
    if (sl <= 0) return sl == W4_ERR_SPACE ? W4_ERR_SPACE : W4_ERR_PARSE;
    j->sig_len = (uint32_t) sl;
    j->signing_input = c;
    j->signing_input_len = d2;
    return W4_OK;
}

int w4_jws_verify(const w4_jws_t *j, const char *expected_alg, w4_jws_verify_fn fn, void *ctx)
{
    if (!j || !expected_alg || !fn) return W4_ERR_ARG;
    if (!w4_streq(j->alg, expected_alg)) return W4_ERR_SIG; /* no algorithm switching */
    int r = fn(ctx, j->alg, j->kid, (const uint8_t *) j->signing_input, j->signing_input_len,
               j->sig, j->sig_len);
    return r == W4_OK ? W4_OK : W4_ERR_SIG;
}

int w4_jws_hs256_verify(void *ctx, const char *alg, const char *kid, const uint8_t *msg,
                        uint32_t msg_len, const uint8_t *sig, uint32_t sig_len)
{
    const w4_hs256_key_t *k = (const w4_hs256_key_t *) ctx;
    (void) kid;
    if (!k || !w4_streq(alg, "HS256") || sig_len != W4_HASH_LEN) return W4_ERR_SIG;
    if (k->key_len < 32) return W4_ERR_SIG; /* RFC 7518 3.2: key >= hash size */
    uint8_t mac[W4_HASH_LEN];
    w4_hmac_sha256(k->key, k->key_len, msg, msg_len, mac);
    bool ok = w4_ct_eq(mac, sig, W4_HASH_LEN);
    w4_memset(mac, 0, sizeof mac);
    return ok ? W4_OK : W4_ERR_SIG;
}

int32_t w4_jws_sign_hs256(const char *header_json, uint32_t hlen, const uint8_t *payload,
                          uint32_t plen, const uint8_t *key, uint32_t key_len, char *out,
                          uint32_t cap)
{
    int32_t a = w4_b64url_encode((const uint8_t *) header_json, hlen, out, cap);
    if (a < 0 || (uint32_t) a + 1 >= cap) return W4_ERR_SPACE;
    out[a] = '.';
    int32_t b = w4_b64url_encode(payload, plen, out + a + 1, cap - (uint32_t) a - 1);
    if (b < 0) return W4_ERR_SPACE;
    uint32_t si = (uint32_t) a + 1 + (uint32_t) b;
    if (si + 1 >= cap) return W4_ERR_SPACE;
    uint8_t mac[W4_HASH_LEN];
    w4_hmac_sha256(key, key_len, (const uint8_t *) out, si, mac);
    out[si] = '.';
    int32_t c = w4_b64url_encode(mac, W4_HASH_LEN, out + si + 1, cap - si - 1);
    if (c < 0) return W4_ERR_SPACE;
    return (int32_t) (si + 1 + (uint32_t) c);
}

static int opt_str(const char *js, const w4_jtok_t *t, int32_t n, const char *k, char *out,
                   uint32_t cap)
{
    int r = w4_json_get_str(js, t, n, 0, k, out, cap);
    return (r == W4_OK || r == W4_ERR_NOTFOUND) ? W4_OK : W4_ERR_PARSE;
}

static int opt_time(const char *js, const w4_jtok_t *t, int32_t n, const char *k, uint64_t *v,
                    bool *has)
{
    int32_t i = w4_json_get(js, t, n, 0, k);
    if (i == W4_ERR_NOTFOUND) return W4_OK;
    if (i < 0) return W4_ERR_PARSE;
    if (w4_json_u64(js, &t[i], v)) return W4_ERR_PARSE;
    *has = true;
    return W4_OK;
}

int w4_jwt_claims(const w4_jws_t *j, const char *want_aud, w4_jwt_claims_t *c)
{
    if (!j || !c) return W4_ERR_ARG;
    w4_memset(c, 0, sizeof *c);
    w4_jtok_t t[PAY_TOKS];
    const char *js = (const char *) j->payload;
    int32_t n = w4_json_parse(js, j->payload_len, t, PAY_TOKS, 8);
    if (n <= 0 || t[0].type != W4_J_OBJ) return W4_ERR_PARSE;
    if (opt_str(js, t, n, "iss", c->iss, sizeof c->iss)) return W4_ERR_PARSE;
    if (opt_str(js, t, n, "sub", c->sub, sizeof c->sub)) return W4_ERR_PARSE;
    if (opt_str(js, t, n, "azp", c->azp, sizeof c->azp)) return W4_ERR_PARSE;
    int r = w4_json_get_str(js, t, n, 0, "nonce", c->nonce, sizeof c->nonce);
    if (r == W4_OK)
        c->has_nonce = true;
    else if (r != W4_ERR_NOTFOUND)
        return W4_ERR_PARSE;
    if (opt_time(js, t, n, "exp", &c->exp, &c->has_exp)) return W4_ERR_PARSE;
    if (opt_time(js, t, n, "iat", &c->iat, &c->has_iat)) return W4_ERR_PARSE;
    if (opt_time(js, t, n, "nbf", &c->nbf, &c->has_nbf)) return W4_ERR_PARSE;
    int32_t a = w4_json_get(js, t, n, 0, "aud");
    if (a >= 0) {
        if (t[a].type == W4_J_STR) {
            if (w4_json_str(js, &t[a], c->aud, sizeof c->aud) < 0) return W4_ERR_PARSE;
            c->aud_count = 1;
        } else if (t[a].type == W4_J_ARR) {
            c->aud_count = t[a].size;
            for (uint32_t k = 0; k < t[a].size; k++) {
                int32_t e = w4_json_at(t, n, a, k);
                if (e < 0 || t[e].type != W4_J_STR) return W4_ERR_PARSE;
                if (k == 0 || (want_aud && w4_json_str_eq(js, &t[e], want_aud)))
                    if (w4_json_str(js, &t[e], c->aud, sizeof c->aud) < 0) return W4_ERR_PARSE;
            }
        } else {
            return W4_ERR_PARSE;
        }
    } else if (a != W4_ERR_NOTFOUND) {
        return W4_ERR_PARSE;
    }
    return W4_OK;
}

int w4_jwt_validate(const w4_jwt_claims_t *c, const char *iss, const char *aud, const char *nonce,
                    uint64_t now_s, uint64_t leeway_s)
{
    if (!c) return W4_ERR_ARG;
    if (iss && !w4_streq(c->iss, iss)) return W4_ERR_DENIED;
    if (aud) {
        if (c->aud_count == 0 || !w4_streq(c->aud, aud)) return W4_ERR_DENIED;
        if (c->aud_count > 1 && !w4_streq(c->azp, aud)) return W4_ERR_DENIED;
    }
    if (!c->has_exp || now_s >= c->exp + leeway_s) return W4_ERR_EXPIRED;
    if (c->has_nbf && c->nbf > now_s + leeway_s) return W4_ERR_EXPIRED;
    if (c->has_iat && c->iat > now_s + leeway_s) return W4_ERR_EXPIRED;
    if (nonce) {
        size_t nl = w4_strnlen(nonce, sizeof c->nonce);
        if (!c->has_nonce || w4_strnlen(c->nonce, sizeof c->nonce) != nl ||
            !w4_ct_eq(c->nonce, nonce, nl))
            return W4_ERR_DENIED;
    }
    return W4_OK;
}

/* ===== Webhooks ===== */
int32_t w4_webhook_sign_gh(const uint8_t *secret, uint32_t slen, const uint8_t *body, uint32_t blen,
                           char *out, uint32_t cap)
{
    uint8_t mac[W4_HASH_LEN];
    w4_hmac_sha256(secret, slen, body, blen, mac);
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_str(&w, "sha256=");
    w4_w_hex(&w, mac, W4_HASH_LEN);
    return w4_w_cstr(&w);
}

int w4_webhook_verify_gh(const uint8_t *secret, uint32_t slen, const uint8_t *body, uint32_t blen,
                         const char *hdr, uint32_t hlen)
{
    char want[80];
    if (w4_webhook_sign_gh(secret, slen, body, blen, want, sizeof want) < 0) return W4_ERR_ARG;
    if (hlen != 71) return W4_ERR_SIG;
    char low[72];
    for (uint32_t i = 0; i < hlen; i++) low[i] = w4_lower(hdr[i]);
    return w4_ct_eq(low, want, 71) ? W4_OK : W4_ERR_SIG;
}

static void ts_mac(const uint8_t *secret, uint32_t slen, uint64_t t, const uint8_t *body,
                   uint32_t blen, uint8_t mac[W4_HASH_LEN])
{
    /* HMAC over "<t>." || body, streamed (no staging buffer, no length cap). */
    char pre[24];
    w4_w w;
    w4_w_init(&w, pre, sizeof pre);
    w4_w_u64(&w, t);
    w4_w_byte(&w, '.');
    const uint8_t *parts[2] = {(const uint8_t *) pre, body};
    uint32_t lens[2] = {w.len, blen};
    w4_hmac_sha256_parts(secret, slen, parts, lens, 2, mac);
}

int32_t w4_webhook_sign_ts(const uint8_t *secret, uint32_t slen, uint64_t t, const uint8_t *body,
                           uint32_t blen, char *out, uint32_t cap)
{
    if (blen > W4_HTTP_MAX_BODY) return W4_ERR_ARG;
    uint8_t mac[W4_HASH_LEN];
    ts_mac(secret, slen, t, body, blen, mac);
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_str(&w, "t=");
    w4_w_u64(&w, t);
    w4_w_str(&w, ",v1=");
    w4_w_hex(&w, mac, W4_HASH_LEN);
    return w4_w_cstr(&w);
}

int w4_webhook_verify_ts(const uint8_t *secret, uint32_t slen, const uint8_t *body, uint32_t blen,
                         const char *hdr, uint32_t hlen, uint64_t now_s, uint64_t tol_s)
{
    if (blen > W4_HTTP_MAX_BODY || hlen < 2 || hdr[0] != 't' || hdr[1] != '=') return W4_ERR_PARSE;
    uint32_t i = 2;
    uint64_t t = 0;
    uint32_t ds = i;
    while (i < hlen && w4_is_digit(hdr[i])) {
        if (i - ds >= 19) return W4_ERR_PARSE;
        t = t * 10u + (uint64_t) (hdr[i] - '0');
        i++;
    }
    if (i == ds) return W4_ERR_PARSE;
    /* any number of ",v1=<hex>" entries (key rotation); one must match */
    uint8_t mac[W4_HASH_LEN];
    char want[65];
    ts_mac(secret, slen, t, body, blen, mac);
    w4_hex_encode(mac, W4_HASH_LEN, want, sizeof want);
    bool any = false, ok = false;
    while (i < hlen) {
        if (hdr[i] != ',') return W4_ERR_PARSE;
        i++;
        uint32_t ks = i;
        while (i < hlen && hdr[i] != '=' && hdr[i] != ',') i++;
        if (i >= hlen || hdr[i] != '=') return W4_ERR_PARSE;
        uint32_t ke = i++;
        uint32_t vs = i;
        while (i < hlen && hdr[i] != ',') i++;
        if (ke - ks == 2 && hdr[ks] == 'v' && hdr[ks + 1] == '1') {
            any = true;
            if (i - vs == 64) {
                char low[64];
                for (uint32_t k = 0; k < 64; k++) low[k] = w4_lower(hdr[vs + k]);
                if (w4_ct_eq(low, want, 64)) ok = true;
            }
        }
    }
    if (!any) return W4_ERR_PARSE;
    if (!ok) return W4_ERR_SIG;
    /* timestamp checked after the MAC so the answer does not leak which failed first */
    if (t + tol_s < now_s || t > now_s + tol_s) return W4_ERR_STALE;
    return W4_OK;
}
