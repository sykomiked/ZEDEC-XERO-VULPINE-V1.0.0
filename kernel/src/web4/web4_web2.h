/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_web2.h — the Web 2 side of Web 4: data models and bounded codecs.
 *
 * What Web 2 is good at, and what this file gives the bridge:
 *   identity           OAuth 2.0 / OpenID Connect (authorization code + PKCE
 *                      S256, RFC 7636; device authorization grant, RFC 8628)
 *   familiar APIs      HTTP/1.1 request/response parse and build (RFC 9112)
 *   tokens             JWS compact parse + verification hook (RFC 7515/7519);
 *                      HS256 is implemented in-module (HMAC-SHA256)
 *   speed / push       webhooks with HMAC-SHA256 signed payloads
 *   feeds              ActivityPub (ActivityStreams 2.0 Create/Note) and
 *                      AT Protocol (app.bsky.feed.post) record shapes, enough
 *                      to mirror one text post in and out of the platform feed
 *   data               a bounded JSON tokenizer and writer (RFC 8259)
 *
 * Every parser works on caller memory with fixed limits, never allocates and
 * fails closed: input it does not fully understand is rejected, not guessed.
 *
 * HONEST LIMITS.
 *  - No sockets and no TLS here. These are codecs and state machines; the host
 *    (or kernel/src/tls) moves the bytes. Talking to a real IdP or a real
 *    Mastodon / Bluesky server has NOT been tested from this module.
 *  - JWS signatures other than HS256 (RS256, ES256, EdDSA, ML-DSA) are verified
 *    through a caller callback; this module has no RSA or ECDSA-P256 code.
 *  - HTTP: no HTTP/2 or HTTP/3, no trailers (rejected), no obs-fold (rejected),
 *    no transfer codings other than a single "chunked" (others rejected).
 *  - JSON: numbers are kept as text; w4_json_i64/u64 accept integers only.
 *    Duplicate object keys are reported as an error by w4_json_get.
 *  - ActivityPub: one Note with plain-text content; HTML in incoming content is
 *    reduced to text by dropping tags and decoding five entities. No JSON-LD
 *    expansion, no HTTP Signatures, no inbox delivery.
 *  - AT Protocol: the post record JSON only (no DAG-CBOR, no repo commits, no
 *    facets/embeds). The 300-grapheme limit is enforced on code points, which
 *    is stricter than or equal to graphemes for most text but not identical.
 */
#ifndef ZXV_WEB4_WEB2_H
#define ZXV_WEB4_WEB2_H

#include "web4_util.h"

/* ======================================================================
 * JSON (RFC 8259) — tokenizer and writer
 * ====================================================================== */
enum { W4_J_OBJ = 1, W4_J_ARR, W4_J_STR, W4_J_NUM, W4_J_TRUE, W4_J_FALSE, W4_J_NULL };

typedef struct {
    uint8_t type;
    uint32_t start, end; /* byte span; strings exclude the quotes */
    uint32_t size;       /* object: number of members; array: number of elements */
    int32_t parent;      /* token index, -1 for the root; object values' parent is the key */
} w4_jtok_t;

#define W4_JSON_MAX_DEPTH 32u

/* Tokenize exactly one JSON value (whitespace around it allowed). Returns the
 * token count, or W4_ERR_PARSE (malformed, depth > max_depth, bad UTF-8 or
 * escape) / W4_ERR_SPACE (more than `max` tokens). */
int32_t w4_json_parse(const char *js, uint32_t len, w4_jtok_t *t, uint32_t max, uint32_t max_depth);
/* Index of the first token after the subtree rooted at i. */
int32_t w4_json_skip(const w4_jtok_t *t, int32_t n, int32_t i);
/* Value token of member `key` in object `obj`: index, W4_ERR_NOTFOUND, or
 * W4_ERR_PARSE when the key occurs more than once. */
int32_t w4_json_get(const char *js, const w4_jtok_t *t, int32_t n, int32_t obj, const char *key);
/* Element `idx` of array `arr`, or W4_ERR_NOTFOUND. */
int32_t w4_json_at(const w4_jtok_t *t, int32_t n, int32_t arr, uint32_t idx);
/* Decode a string token (escapes, \u surrogate pairs -> UTF-8). Writes a NUL.
 * Returns the byte length, W4_ERR_ARG (not a string) or W4_ERR_SPACE. */
int32_t w4_json_str(const char *js, const w4_jtok_t *tok, char *out, uint32_t cap);
/* True iff the string token decodes to exactly `s`. */
bool w4_json_str_eq(const char *js, const w4_jtok_t *tok, const char *s);
/* Integers only (no fraction/exponent), range checked. */
int w4_json_i64(const char *js, const w4_jtok_t *tok, int64_t *v);
int w4_json_u64(const char *js, const w4_jtok_t *tok, uint64_t *v);
int w4_json_bool(const w4_jtok_t *tok, bool *v);
/* Convenience: member lookups that also check the type. */
int w4_json_get_str(const char *js, const w4_jtok_t *t, int32_t n, int32_t obj, const char *key,
                    char *out, uint32_t cap);
int w4_json_get_u64(const char *js, const w4_jtok_t *t, int32_t n, int32_t obj, const char *key,
                    uint64_t *v);

/* Writer. Commas and colons are inserted for you; misuse sets w.err. */
typedef struct {
    w4_w w;
    uint8_t depth;
    uint8_t kind[W4_JSON_MAX_DEPTH]; /* W4_J_OBJ / W4_J_ARR */
    uint8_t count[W4_JSON_MAX_DEPTH];
    bool after_key;
} w4_jw;

void w4_jw_init(w4_jw *j, char *buf, uint32_t cap);
void w4_jw_obj(w4_jw *j);
void w4_jw_obj_end(w4_jw *j);
void w4_jw_arr(w4_jw *j);
void w4_jw_arr_end(w4_jw *j);
void w4_jw_key(w4_jw *j, const char *k);
void w4_jw_str(w4_jw *j, const char *s);
void w4_jw_strn(w4_jw *j, const char *s, uint32_t n);
void w4_jw_u64(w4_jw *j, uint64_t v);
void w4_jw_i64(w4_jw *j, int64_t v);
void w4_jw_bool(w4_jw *j, bool v);
void w4_jw_null(w4_jw *j);
/* "0x"+hex of bytes, as a JSON string (Ethereum DATA). */
void w4_jw_hexstr(w4_jw *j, const uint8_t *p, uint32_t n);
/* A JSON string holding an Ethereum QUANTITY. */
void w4_jw_qty(w4_jw *j, const w4_u256 *v);
void w4_jw_qty64(w4_jw *j, uint64_t v);
/* Finish: all containers closed, NUL written. Returns length or negative. */
int32_t w4_jw_finish(w4_jw *j);

/* ======================================================================
 * HTTP/1.1 (RFC 9110 / 9112)
 * ====================================================================== */
#define W4_HTTP_MAX_HDRS 32u
#define W4_HTTP_MAX_BODY (1u << 20)

typedef struct {
    const char *name;
    uint32_t name_len;
    const char *val; /* OWS trimmed */
    uint32_t val_len;
} w4_hdr_t;

typedef struct {
    /* request */
    const char *method;
    uint32_t method_len;
    const char *target;
    uint32_t target_len;
    /* response */
    uint16_t status;
    const char *reason;
    uint32_t reason_len;
    /* both */
    uint8_t minor; /* HTTP/1.minor, 0 or 1 */
    w4_hdr_t h[W4_HTTP_MAX_HDRS];
    uint32_t nh;
    const uint8_t *body; /* into the input, or into the caller's chunk buffer */
    uint32_t body_len;
    uint32_t consumed; /* bytes of input this message used */
    bool chunked;
    bool until_close; /* response with no length: body is the rest of input */
} w4_http_msg_t;

/* Parse a complete request in buf[0..len). W4_OK, W4_ERR_PENDING (incomplete;
 * read more), or W4_ERR_PARSE. A chunked body is de-chunked into
 * chunk_buf[0..chunk_cap) (may be NULL when chunked bodies are not accepted).
 * Refused (request-smuggling defences): both Content-Length and
 * Transfer-Encoding, differing repeated Content-Length, any transfer coding
 * other than exactly "chunked", obs-fold, bare LF, whitespace before ':'. */
int w4_http_parse_request(const uint8_t *buf, uint32_t len, w4_http_msg_t *m, uint8_t *chunk_buf,
                          uint32_t chunk_cap);
/* Same for a response. `head_request`: the request was HEAD (no body). */
int w4_http_parse_response(const uint8_t *buf, uint32_t len, bool head_request, w4_http_msg_t *m,
                           uint8_t *chunk_buf, uint32_t chunk_cap);
/* First header with this name (case-insensitive), or NULL. */
const w4_hdr_t *w4_http_hdr(const w4_http_msg_t *m, const char *name);

/* Builders. Header values containing CR, LF or NUL are refused (header
 * injection). Content-Length is added when body_len > 0 or the method
 * normally has a body (POST/PUT/PATCH). */
int32_t w4_http_build_request(w4_w *w, const char *method, const char *host, const char *target,
                              const w4_hdr_t *hdrs, uint32_t nh, const uint8_t *body,
                              uint32_t body_len);
int32_t w4_http_build_response(w4_w *w, uint16_t status, const char *reason, const w4_hdr_t *hdrs,
                               uint32_t nh, const uint8_t *body, uint32_t body_len);
/* Header literal helper for builder arrays. */
w4_hdr_t w4_hdr(const char *name, const char *val);

/* URLs: scheme://host[:port]/path?query#fragment (http/https/ws/wss/ipfs). */
typedef struct {
    const char *scheme;
    uint32_t scheme_len;
    const char *host;
    uint32_t host_len;
    uint16_t port; /* explicit or scheme default; 0 for ipfs */
    const char *path;
    uint32_t path_len; /* "/" if absent */
    const char *query;
    uint32_t query_len; /* without '?' */
} w4_url_t;
int w4_url_parse(const char *s, uint32_t len, w4_url_t *u);

/* application/x-www-form-urlencoded and URI query strings. */
void w4_w_pct(w4_w *w, const char *s);                              /* RFC 3986 unreserved kept */
void w4_w_form(w4_w *w, bool *first, const char *k, const char *v); /* k=v with & */
/* Find key in a query/form string and percent-decode its value ('+' = space).
 * Returns the decoded length, W4_ERR_NOTFOUND, W4_ERR_PARSE (bad escape or
 * duplicated key) or W4_ERR_SPACE. */
int32_t w4_form_get(const char *q, uint32_t qlen, const char *key, char *out, uint32_t cap);

/* ======================================================================
 * JWS / JWT (RFC 7515, RFC 7519)
 * ====================================================================== */
#define W4_JWS_MAX_PAYLOAD 4096u
#define W4_JWS_MAX_SIG     3328u /* fits ML-DSA-65 (3309) as well as RS/ES/HS */

typedef struct {
    char alg[24];
    char typ[24];
    char kid[96];
    const char *signing_input; /* header '.' payload, inside the caller's string */
    uint32_t signing_input_len;
    uint8_t payload[W4_JWS_MAX_PAYLOAD];
    uint32_t payload_len;
    uint8_t sig[W4_JWS_MAX_SIG];
    uint32_t sig_len;
} w4_jws_t;

/* Verifier hook: return W4_OK only for a valid signature by a key the host
 * trusts for this alg/kid. */
typedef int (*w4_jws_verify_fn)(void *ctx, const char *alg, const char *kid, const uint8_t *msg,
                                uint32_t msg_len, const uint8_t *sig, uint32_t sig_len);

/* Parse compact serialization. Refuses alg "none", a "crit" header, an
 * unsigned token, non-canonical base64url and non-JSON-object segments. */
int w4_jws_parse(const char *compact, uint32_t len, w4_jws_t *j);
/* Verify: the header alg must equal `expected_alg` (no alg switching), then
 * the hook decides. */
int w4_jws_verify(const w4_jws_t *j, const char *expected_alg, w4_jws_verify_fn fn, void *ctx);
/* In-module HS256 verifier: ctx is a w4_hs256_key_t. */
typedef struct {
    const uint8_t *key;
    uint32_t key_len;
} w4_hs256_key_t;
int w4_jws_hs256_verify(void *ctx, const char *alg, const char *kid, const uint8_t *msg,
                        uint32_t msg_len, const uint8_t *sig, uint32_t sig_len);
/* Build an HS256 compact JWS from exact header and payload bytes. */
int32_t w4_jws_sign_hs256(const char *header_json, uint32_t hlen, const uint8_t *payload,
                          uint32_t plen, const uint8_t *key, uint32_t key_len, char *out,
                          uint32_t cap);

/* Registered claims an OIDC relying party checks. */
typedef struct {
    char iss[192];
    char sub[192];
    char aud[192]; /* the audience that matched (or the single one) */
    char nonce[96];
    char azp[192];
    uint64_t exp, iat, nbf;
    uint32_t aud_count; /* entries in aud (1 for a string) */
    bool has_exp, has_iat, has_nbf, has_nonce;
} w4_jwt_claims_t;

/* Parse claims from a JWS payload. `want_aud` selects the matching entry when
 * aud is an array (else the first is kept). */
int w4_jwt_claims(const w4_jws_t *j, const char *want_aud, w4_jwt_claims_t *c);
/* OIDC Core 3.1.3.7 checks: iss and aud match (several audiences require
 * azp == aud), exp required and in the future, nbf/iat not in the future
 * (with leeway), nonce matches when given. NumericDate values must be
 * integers here (fractional seconds are refused, not rounded). */
int w4_jwt_validate(const w4_jwt_claims_t *c, const char *iss, const char *aud, const char *nonce,
                    uint64_t now_s, uint64_t leeway_s);

/* ======================================================================
 * OAuth 2.0 / OpenID Connect flows
 * ====================================================================== */
#define W4_PKCE_VERIFIER_MAX 128u
#define W4_PKCE_CHALLENGE    43u

/* RFC 7636 4.1: 43..128 chars of [A-Za-z0-9-._~]. */
bool w4_pkce_verifier_ok(const char *v, uint32_t len);
/* S256 challenge: BASE64URL(SHA256(ASCII(verifier))), 43 chars + NUL. */
int w4_pkce_s256(const char *verifier, uint32_t len, char out[W4_PKCE_CHALLENGE + 1]);
/* Verifier from 32 caller-supplied random bytes (43 chars + NUL). */
int w4_pkce_verifier_from_random(const uint8_t rnd[32], char out[W4_PKCE_CHALLENGE + 1]);

enum {
    W4_OA_IDLE = 0,
    W4_OA_AUTHORIZING,     /* user sent to the authorization endpoint     */
    W4_OA_HAVE_CODE,       /* redirect came back with a code              */
    W4_OA_TOKEN_REQUESTED, /* token request built and sent                */
    W4_OA_DEVICE_PENDING,  /* device flow: user code shown, polling       */
    W4_OA_DONE,
    W4_OA_FAILED
};

typedef struct {
    char client_id[128];
    char redirect_uri[256];
    char scope[256];
    char authz_endpoint[256]; /* full URL */
    char token_host[128];     /* Host header for the token request */
    char token_path[192];     /* path of the token endpoint */
    char device_path[192];    /* path of the device authorization endpoint */
} w4_oauth_cfg_t;

typedef struct {
    uint8_t st;
    w4_oauth_cfg_t cfg;
    char state[48];
    char nonce[48];
    char verifier[W4_PKCE_VERIFIER_MAX + 1];
    char code[512];
    char access_token[2048];
    char refresh_token[512];
    char id_token[6144];
    char token_type[16];
    char granted_scope[256];
    uint64_t expires_at; /* seconds */
    char error[64];
    /* device flow */
    char device_code[256];
    char user_code[32];
    char verification_uri[256];
    char verification_uri_complete[320];
    uint64_t device_expires_at;
    uint64_t interval_s;
    uint64_t next_poll_at;
} w4_oauth_t;

/* Authorization code + PKCE. `rnd` is 64 bytes of caller randomness:
 * 16 for state, 16 for nonce, 32 for the PKCE verifier. */
int w4_oauth_code_begin(w4_oauth_t *f, const w4_oauth_cfg_t *cfg, const uint8_t rnd[64]);
/* The URL to send the user to (response_type=code, S256 challenge, nonce). */
int32_t w4_oauth_code_auth_url(const w4_oauth_t *f, char *out, uint32_t cap);
/* Handle the redirect's query string. A state mismatch fails the flow
 * (CSRF), an "error" parameter fails it with that error. */
int w4_oauth_code_callback(w4_oauth_t *f, const char *query, uint32_t qlen);
/* Build the POST to the token endpoint (public client: client_id in body). */
int32_t w4_oauth_code_token_request(w4_oauth_t *f, uint8_t *out, uint32_t cap);
/* Handle the token endpoint's HTTP status and JSON body. */
int w4_oauth_token_response(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                            uint64_t now_s);

/* Device authorization grant (RFC 8628). */
int w4_oauth_device_begin(w4_oauth_t *f, const w4_oauth_cfg_t *cfg);
int32_t w4_oauth_device_request(const w4_oauth_t *f, uint8_t *out, uint32_t cap);
int w4_oauth_device_response(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                             uint64_t now_s);
/* True when the next poll may be sent (interval honoured). */
bool w4_oauth_device_poll_due(const w4_oauth_t *f, uint64_t now_s);
int32_t w4_oauth_device_poll_request(w4_oauth_t *f, uint8_t *out, uint32_t cap, uint64_t now_s);
/* authorization_pending: W4_ERR_PENDING (keep polling); slow_down: interval
 * += 5 s and W4_ERR_PENDING; access_denied: W4_ERR_DENIED; expired_token or
 * local expiry: W4_ERR_EXPIRED; success: W4_OK (DONE). */
int w4_oauth_device_poll_response(w4_oauth_t *f, uint16_t status, const char *json, uint32_t len,
                                  uint64_t now_s);

/* ======================================================================
 * Webhooks (HMAC-SHA256 signed payloads)
 * ====================================================================== */
/* GitHub style header value "sha256=<hex HMAC(secret, body)>". */
int32_t w4_webhook_sign_gh(const uint8_t *secret, uint32_t slen, const uint8_t *body, uint32_t blen,
                           char *out, uint32_t cap);
int w4_webhook_verify_gh(const uint8_t *secret, uint32_t slen, const uint8_t *body, uint32_t blen,
                         const char *hdr, uint32_t hlen);
/* Timestamped style "t=<unix>,v1=<hex HMAC(secret, "<t>." || body)>": the
 * timestamp is signed, so an old delivery cannot be replayed past `tol_s`. */
int32_t w4_webhook_sign_ts(const uint8_t *secret, uint32_t slen, uint64_t t, const uint8_t *body,
                           uint32_t blen, char *out, uint32_t cap);
int w4_webhook_verify_ts(const uint8_t *secret, uint32_t slen, const uint8_t *body, uint32_t blen,
                         const char *hdr, uint32_t hlen, uint64_t now_s, uint64_t tol_s);

/* ======================================================================
 * Social posts: platform feed <-> ActivityPub / AT Protocol
 * ====================================================================== */
enum { W4_POST_WEB4 = 0, W4_POST_ACTIVITYPUB = 1, W4_POST_ATPROTO = 2 };
#define W4_POST_TEXT_MAX 3000u

typedef struct {
    uint8_t origin;   /* W4_POST_* */
    char id[256];     /* AP object id / at:// URI / web4:<hex> */
    char author[256]; /* AP actor URI / DID / web4 agent hex */
    char text[W4_POST_TEXT_MAX + 1];
    uint64_t created_s; /* unix seconds */
    char lang[16];      /* BCP 47, optional */
    char reply_to[256]; /* parent id/URI, optional */
} w4_post_t;

/* RFC 3339 "YYYY-MM-DDTHH:MM:SSZ" (fractional seconds accepted on parse). */
int32_t w4_rfc3339_format(uint64_t unix_s, char *out, uint32_t cap);
int w4_rfc3339_parse(const char *s, uint32_t len, uint64_t *unix_s);

/* ActivityStreams 2.0 Create{Note}. */
int32_t w4_ap_note_build(const w4_post_t *p, char *out, uint32_t cap);
/* Accepts a Create whose object is a Note, or a bare Note. */
int w4_ap_note_parse(const char *json, uint32_t len, w4_post_t *p);
/* app.bsky.feed.post record. reply_to, when set, must be an at:// URI and
 * reply_cid its CID (both root and parent are set to it: a direct reply). */
int32_t w4_atp_post_build(const w4_post_t *p, const char *reply_cid, char *out, uint32_t cap);
int w4_atp_post_parse(const char *json, uint32_t len, w4_post_t *p);

#endif /* ZXV_WEB4_WEB2_H */
