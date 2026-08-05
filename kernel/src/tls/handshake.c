/* handshake.c — TLS 1.3 client handshake. See handshake.h, and read the
 * authentication warning at the top of it before using this for anything. */
#include "handshake.h"
#include "../robin_debanks/sha256.h"

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void wipe(uint8_t *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = 0; }
static uint16_t g16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t g24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

const char *tls_error_str(tls_error_t e) {
    switch (e) {
    case TLS_ERR_NONE:         return "no error";
    case TLS_ERR_PROTOCOL:     return "malformed or unexpected message";
    case TLS_ERR_UNSUPPORTED:  return "server chose something we do not implement";
    case TLS_ERR_BAD_RECORD:   return "record failed authentication";
    case TLS_ERR_BAD_FINISHED: return "server Finished did not verify";
    case TLS_ERR_NO_VERIFIER:  return "no certificate verifier (see handshake.h)";
    case TLS_ERR_TOO_BIG:      return "message exceeded a fixed buffer";
    case TLS_ERR_ALERT:        return "server sent a fatal alert";
    case TLS_ERR_HELLO_RETRY:  return "server wants a key-share group we do not offer";
    }
    return "unknown";
}

static void fail(tls_client_t *c, tls_error_t e) {
    c->state = TLS_ST_FAILED;
    if (c->error == TLS_ERR_NONE) c->error = e;
}

/* Append to the transcript. Every handshake message goes here, in wire form,
 * INCLUDING its 4-byte header — the transcript is the concatenation of the
 * messages, not of the records that carried them. */
static bool tr_add(tls_client_t *c, const uint8_t *d, uint32_t n) {
    if (c->transcript_len + n > TLS_TRANSCRIPT_MAX) { fail(c, TLS_ERR_TOO_BIG); return false; }
    cpy(c->transcript + c->transcript_len, d, n);
    c->transcript_len += n;
    return true;
}
static void tr_hash(const tls_client_t *c, uint8_t out[HASH_LEN]) {
    sha256(c->transcript, c->transcript_len, out);
}

void tls_client_init(tls_client_t *c,
                     const char *hostname,
                     const uint8_t random32[32],
                     const uint8_t priv32[X25519_LEN],
                     tls_verify_policy_t policy) {
    if (!c) return;
    wipe((uint8_t *)c, sizeof(*c));
    c->state = TLS_ST_INIT;
    c->policy = policy;
    c->authenticated = false;     /* never set true: no verifier exists */

    if (hostname) {
        uint32_t i = 0;
        while (hostname[i] && i < TLS_MAX_HOSTNAME - 1u) {
            c->hostname[i] = (uint8_t)hostname[i];
            i++;
        }
        c->hostname_len = i;
    }
    if (random32) cpy(c->random, random32, 32);
    if (priv32)   cpy(c->priv, priv32, X25519_LEN);
    x25519_public(c->pub, c->priv);
    /* A non-empty legacy_session_id makes middleboxes treat the flow as a
     * resumed TLS 1.2 session and leave it alone (RFC 8446 D.4). */
    for (int i = 0; i < 32; i++) c->session_id[i] = (uint8_t)(c->random[i] ^ 0x5A);
    tls_keys_clear(&c->rx);
    tls_keys_clear(&c->tx);
}

/* ---------------- ClientHello ---------------- */

static uint32_t put_ext(uint8_t *o, uint32_t at, uint16_t type,
                        const uint8_t *body, uint16_t len) {
    o[at++] = (uint8_t)(type >> 8); o[at++] = (uint8_t)type;
    o[at++] = (uint8_t)(len >> 8);  o[at++] = (uint8_t)len;
    for (uint16_t i = 0; i < len; i++) o[at++] = body[i];
    return at;
}

uint32_t tls_client_hello(tls_client_t *c, uint8_t *out, uint32_t cap) {
    if (!c || !out || c->state != TLS_ST_INIT) return 0;

    uint8_t m[1024];
    uint32_t at = 0;
    m[at++] = TLS_HS_CLIENT_HELLO;
    at += 3;                                   /* length, filled below */
    uint32_t body = at;

    m[at++] = 0x03; m[at++] = 0x03;            /* legacy_version */
    cpy(m + at, c->random, 32); at += 32;
    m[at++] = 32; cpy(m + at, c->session_id, 32); at += 32;

    m[at++] = 0x00; m[at++] = 0x02;            /* cipher_suites length */
    m[at++] = (uint8_t)(TLS_CS_CHACHA20_POLY1305_SHA256 >> 8);
    m[at++] = (uint8_t)TLS_CS_CHACHA20_POLY1305_SHA256;

    m[at++] = 0x01; m[at++] = 0x00;            /* legacy_compression_methods */

    uint32_t extlen_at = at; at += 2;
    uint32_t ext_start = at;

    /* server_name (0) — SNI. Omitting it makes most virtual hosts refuse. */
    if (c->hostname_len) {
        uint8_t sni[TLS_MAX_HOSTNAME + 8];
        uint32_t s = 0;
        uint16_t hl = (uint16_t)c->hostname_len;
        sni[s++] = (uint8_t)((hl + 3) >> 8); sni[s++] = (uint8_t)(hl + 3);
        sni[s++] = 0;                          /* host_name type */
        sni[s++] = (uint8_t)(hl >> 8); sni[s++] = (uint8_t)hl;
        cpy(sni + s, c->hostname, hl); s += hl;
        at = put_ext(m, at, 0, sni, (uint16_t)s);
    }

    /* supported_groups (10): x25519 only */
    { uint8_t b[4] = { 0x00, 0x02, 0x00, 0x1D };
      at = put_ext(m, at, 10, b, 4); }

    /* signature_algorithms (13). Required by the spec even though we cannot
     * verify any of them — a server will not proceed without it. Offering
     * these is NOT a claim that we check them; see the header. */
    { uint8_t b[10] = { 0x00, 0x08,
                        0x04, 0x03,   /* ecdsa_secp256r1_sha256 */
                        0x08, 0x04,   /* rsa_pss_rsae_sha256    */
                        0x08, 0x07,   /* ed25519                */
                        0x04, 0x01 }; /* rsa_pkcs1_sha256       */
      at = put_ext(m, at, 13, b, 10); }

    /* supported_versions (43): TLS 1.3 only */
    { uint8_t b[3] = { 0x02, 0x03, 0x04 };
      at = put_ext(m, at, 43, b, 3); }

    /* key_share (51): our x25519 public key */
    { uint8_t b[6 + X25519_LEN];
      uint32_t s = 0;
      s += 2;                                   /* client_shares length */
      b[s++] = 0x00; b[s++] = 0x1D;             /* group = x25519 */
      b[s++] = 0x00; b[s++] = X25519_LEN;
      cpy(b + s, c->pub, X25519_LEN); s += X25519_LEN;
      b[0] = (uint8_t)((s - 2) >> 8); b[1] = (uint8_t)(s - 2);
      at = put_ext(m, at, 51, b, (uint16_t)s); }

    uint32_t ext_len = at - ext_start;
    m[extlen_at]     = (uint8_t)(ext_len >> 8);
    m[extlen_at + 1] = (uint8_t)ext_len;

    uint32_t blen = at - body;
    m[1] = (uint8_t)(blen >> 16); m[2] = (uint8_t)(blen >> 8); m[3] = (uint8_t)blen;

    if (!tr_add(c, m, at)) return 0;
    uint32_t n = tls_record_write(&c->tx, TLS_CT_HANDSHAKE, m, at, out, cap);
    if (!n) { fail(c, TLS_ERR_TOO_BIG); return 0; }
    c->state = TLS_ST_WAIT_SH;
    return n;
}

/* ---------------- ServerHello ---------------- */

/* The special random value that marks a ServerHello as a HelloRetryRequest
 * (RFC 8446 4.1.3): SHA-256 of "HelloRetryRequest". */
static const uint8_t HRR_RANDOM[32] = {
    0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,
    0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C
};

static bool find_ext(const uint8_t *e, uint32_t n, uint16_t want,
                     const uint8_t **body, uint16_t *blen) {
    uint32_t at = 0;
    while (at + 4u <= n) {
        uint16_t t = g16(e + at);
        uint16_t l = g16(e + at + 2);
        if (at + 4u + l > n) return false;      /* a length that lies */
        if (t == want) { *body = e + at + 4; *blen = l; return true; }
        at += 4u + l;
    }
    return false;
}

static void handle_server_hello(tls_client_t *c, const uint8_t *m, uint32_t n) {
    /* m includes the 4-byte handshake header */
    if (n < 4 + 2 + 32 + 1) { fail(c, TLS_ERR_PROTOCOL); return; }
    const uint8_t *p = m + 4;
    uint32_t left = n - 4;

    if (left < 2 + 32 + 1) { fail(c, TLS_ERR_PROTOCOL); return; }
    const uint8_t *srv_random = p + 2;

    /* A HelloRetryRequest means the server wants a group we did not offer.
     * We offer only x25519, so this is a clean refusal rather than a retry we
     * cannot satisfy. */
    bool hrr = true;
    for (int i = 0; i < 32; i++) if (srv_random[i] != HRR_RANDOM[i]) hrr = false;
    if (hrr) { fail(c, TLS_ERR_HELLO_RETRY); return; }

    uint32_t at = 2 + 32;
    uint8_t sid_len = p[at++];
    if (sid_len > 32 || at + sid_len + 3u > left) { fail(c, TLS_ERR_PROTOCOL); return; }
    at += sid_len;

    uint16_t suite = g16(p + at); at += 2;
    if (suite != TLS_CS_CHACHA20_POLY1305_SHA256) { fail(c, TLS_ERR_UNSUPPORTED); return; }
    at += 1;                                     /* legacy_compression_method */

    if (at + 2u > left) { fail(c, TLS_ERR_PROTOCOL); return; }
    uint16_t ext_len = g16(p + at); at += 2;
    if (at + ext_len > left) { fail(c, TLS_ERR_PROTOCOL); return; }
    const uint8_t *ext = p + at;

    const uint8_t *b; uint16_t bl;
    if (!find_ext(ext, ext_len, 43, &b, &bl) || bl != 2 || g16(b) != TLS_VERSION_13) {
        fail(c, TLS_ERR_UNSUPPORTED); return;    /* not actually TLS 1.3 */
    }
    if (!find_ext(ext, ext_len, 51, &b, &bl)) { fail(c, TLS_ERR_PROTOCOL); return; }
    if (bl != 4 + X25519_LEN || g16(b) != TLS_GROUP_X25519 ||
        g16(b + 2) != X25519_LEN) { fail(c, TLS_ERR_UNSUPPORTED); return; }

    uint8_t shared[X25519_LEN];
    if (!x25519_shared(shared, c->priv, b + 4)) {
        /* A low-order point: the peer is trying to force a predictable
         * secret. RFC 8446 7.4.2 requires aborting. */
        fail(c, TLS_ERR_PROTOCOL); return;
    }

    /* transcript is ClientHello..ServerHello at this point */
    tls13_early_secret(&c->sched, 0, 0);
    if (!tls13_handshake_secret(&c->sched, shared, X25519_LEN,
                                c->transcript, c->transcript_len)) {
        wipe(shared, sizeof shared); fail(c, TLS_ERR_PROTOCOL); return;
    }
    wipe(shared, sizeof shared);

    uint8_t k[CHACHA20_KEY_LEN], iv[CHACHA20_NONCE_LEN];
    tls13_traffic_keys(c->sched.s_hs_traffic, k, sizeof k, iv, sizeof iv);
    tls_keys_set(&c->rx, k, iv);
    tls13_traffic_keys(c->sched.c_hs_traffic, k, sizeof k, iv, sizeof iv);
    tls_keys_set(&c->tx, k, iv);
    wipe(k, sizeof k); wipe(iv, sizeof iv);

    c->state = TLS_ST_WAIT_EE;
}

/* ---------------- encrypted handshake messages ---------------- */

static void handle_hs_message(tls_client_t *c, const uint8_t *m, uint32_t n,
                              uint8_t *reply, uint32_t reply_cap, uint32_t *reply_len) {
    uint8_t type = m[0];

    /* A ticket may arrive after the handshake; it is not part of the
     * transcript and we do not resume, so drop it. */
    if (type == TLS_HS_NEW_SESSION_TICKET && c->state == TLS_ST_ESTABLISHED) return;

    if (type == TLS_HS_SERVER_HELLO) {
        if (c->state != TLS_ST_WAIT_SH) { fail(c, TLS_ERR_PROTOCOL); return; }
        if (!tr_add(c, m, n)) return;
        handle_server_hello(c, m, n);
        return;
    }

    /* Everything below is encrypted and must arrive in order. */
    switch (c->state) {

    case TLS_ST_WAIT_EE:
        if (type != TLS_HS_ENCRYPTED_EXTENSIONS) { fail(c, TLS_ERR_PROTOCOL); return; }
        if (!tr_add(c, m, n)) return;
        c->state = TLS_ST_WAIT_CERT;
        return;

    case TLS_ST_WAIT_CERT:
        if (type != TLS_HS_CERTIFICATE) { fail(c, TLS_ERR_PROTOCOL); return; }
        if (!tr_add(c, m, n)) return;
        /* The certificate chain is carried, parsed by nobody, and verified by
         * nobody. That is the documented limitation, not an oversight. */
        c->state = TLS_ST_WAIT_CV;
        return;

    case TLS_ST_WAIT_CV:
        if (type != TLS_HS_CERTIFICATE_VERIFY) { fail(c, TLS_ERR_PROTOCOL); return; }
        if (!tr_add(c, m, n)) return;
        /* THE AUTHENTICATION DECISION, in one place.
         * With TLS_VERIFY_REQUIRED we stop here, because there is nothing to
         * verify the signature with. Only an explicit acknowledgement of an
         * unauthenticated channel continues. */
        if (c->policy != TLS_VERIFY_INSECURE_ACKNOWLEDGED) {
            fail(c, TLS_ERR_NO_VERIFIER);
            return;
        }
        /* snapshot the transcript hash: the server's Finished is over
         * ClientHello..CertificateVerify */
        tr_hash(c, c->th_precert_fin);
        c->have_th_precert = true;
        c->state = TLS_ST_WAIT_FIN;
        return;

    case TLS_ST_WAIT_FIN: {
        if (type != TLS_HS_FINISHED) { fail(c, TLS_ERR_PROTOCOL); return; }
        if (n != 4 + HASH_LEN) { fail(c, TLS_ERR_PROTOCOL); return; }
        if (!c->have_th_precert) { fail(c, TLS_ERR_PROTOCOL); return; }

        uint8_t want[HASH_LEN];
        if (!tls13_finished(c->sched.s_hs_traffic, c->th_precert_fin, want)) {
            fail(c, TLS_ERR_PROTOCOL); return;
        }
        /* Constant-time: this is a MAC comparison. */
        if (!ct_equal(want, m + 4, HASH_LEN)) { fail(c, TLS_ERR_BAD_FINISHED); return; }

        if (!tr_add(c, m, n)) return;

        /* application secrets are over ClientHello..server Finished */
        if (!tls13_master_secret(&c->sched, c->transcript, c->transcript_len)) {
            fail(c, TLS_ERR_PROTOCOL); return;
        }

        /* our Finished is over the same transcript, keyed by OUR handshake
         * secret, and must be sent under the HANDSHAKE keys — before we
         * switch to application keys. */
        uint8_t th[HASH_LEN], vd[HASH_LEN];
        tr_hash(c, th);
        if (!tls13_finished(c->sched.c_hs_traffic, th, vd)) {
            fail(c, TLS_ERR_PROTOCOL); return;
        }
        uint8_t fin[4 + HASH_LEN];
        fin[0] = TLS_HS_FINISHED; fin[1] = 0; fin[2] = 0; fin[3] = HASH_LEN;
        cpy(fin + 4, vd, HASH_LEN);

        uint32_t w = tls_record_write(&c->tx, TLS_CT_HANDSHAKE, fin, sizeof fin,
                                      reply, reply_cap);
        if (!w) { fail(c, TLS_ERR_TOO_BIG); return; }
        if (reply_len) *reply_len = w;

        /* now switch both directions to application keys */
        uint8_t k[CHACHA20_KEY_LEN], iv[CHACHA20_NONCE_LEN];
        tls13_traffic_keys(c->sched.s_ap_traffic, k, sizeof k, iv, sizeof iv);
        tls_keys_set(&c->rx, k, iv);
        tls13_traffic_keys(c->sched.c_ap_traffic, k, sizeof k, iv, sizeof iv);
        tls_keys_set(&c->tx, k, iv);
        wipe(k, sizeof k); wipe(iv, sizeof iv);

        c->state = TLS_ST_ESTABLISHED;
        /* authenticated stays FALSE — the channel is encrypted, not verified */
        return;
    }

    default:
        fail(c, TLS_ERR_PROTOCOL);
        return;
    }
}

/* Pull whole handshake messages out of the reassembly buffer. */
static void drain_hs(tls_client_t *c, uint8_t *reply, uint32_t reply_cap,
                     uint32_t *reply_len) {
    uint32_t at = 0;
    while (c->hs_len - at >= 4u) {
        uint32_t blen = g24(c->hs_buf + at + 1);
        if (blen > TLS_HS_MSG_MAX - 4u) { fail(c, TLS_ERR_TOO_BIG); return; }
        if (c->hs_len - at < 4u + blen) break;          /* incomplete */
        handle_hs_message(c, c->hs_buf + at, 4u + blen, reply, reply_cap, reply_len);
        at += 4u + blen;
        if (c->state == TLS_ST_FAILED) return;
    }
    if (at) {
        for (uint32_t i = 0; i + at < c->hs_len; i++) c->hs_buf[i] = c->hs_buf[i + at];
        c->hs_len -= at;
    }
}

uint32_t tls_client_feed(tls_client_t *c, const uint8_t *in, uint32_t in_len,
                         uint8_t *reply, uint32_t reply_cap, uint32_t *reply_len) {
    if (reply_len) *reply_len = 0;
    if (!c || !in || c->state == TLS_ST_FAILED) return 0;

    uint32_t off = 0;
    static uint8_t pt[TLS_MAX_CIPHERTEXT];
    while (off < in_len) {
        uint32_t used = 0, plen = 0;
        uint8_t  ptype = 0;
        int rc = tls_record_read(&c->rx, in + off, in_len - off, &used,
                                 &ptype, pt, sizeof pt, &plen);
        if (rc == TLS_REC_NEED_MORE) break;
        if (rc == TLS_REC_BAD)       { fail(c, TLS_ERR_PROTOCOL);   break; }
        if (rc == TLS_REC_AUTH_FAIL) { fail(c, TLS_ERR_BAD_RECORD); break; }
        off += used;
        if (rc == TLS_REC_SKIP) continue;

        if (ptype == TLS_CT_ALERT) {
            /* level 1 is a warning; close_notify (0) ends the stream. */
            if (plen >= 2) {
                c->alert_desc = pt[1];
                if (pt[1] == 0) { c->peer_closed = true; continue; }
                if (pt[0] == 2) { c->error = TLS_ERR_ALERT; fail(c, TLS_ERR_ALERT); break; }
            }
            continue;
        }

        if (ptype == TLS_CT_HANDSHAKE) {
            if (c->hs_len + plen > TLS_HS_MSG_MAX) { fail(c, TLS_ERR_TOO_BIG); break; }
            cpy(c->hs_buf + c->hs_len, pt, plen);
            c->hs_len += plen;
            drain_hs(c, reply, reply_cap, reply_len);
            if (c->state == TLS_ST_FAILED) break;
            continue;
        }

        if (ptype == TLS_CT_APPLICATION_DATA) {
            if (c->state != TLS_ST_ESTABLISHED) { fail(c, TLS_ERR_PROTOCOL); break; }
            /* compact anything already consumed before appending */
            if (c->app_off) {
                for (uint32_t i = 0; i + c->app_off < c->app_len; i++)
                    c->app[i] = c->app[i + c->app_off];
                c->app_len -= c->app_off;
                c->app_off = 0;
            }
            if (c->app_len + plen > sizeof c->app) { fail(c, TLS_ERR_TOO_BIG); break; }
            cpy(c->app + c->app_len, pt, plen);
            c->app_len += plen;
            continue;
        }
        /* anything else at this point is a protocol violation */
        fail(c, TLS_ERR_PROTOCOL);
        break;
    }
    return off;
}

uint32_t tls_client_send(tls_client_t *c, const uint8_t *data, uint32_t len,
                         uint8_t *out, uint32_t cap) {
    if (!c || c->state != TLS_ST_ESTABLISHED) return 0;
    return tls_record_write(&c->tx, TLS_CT_APPLICATION_DATA, data, len, out, cap);
}

uint32_t tls_client_read(tls_client_t *c, uint8_t *out, uint32_t cap) {
    if (!c || !out || c->app_off >= c->app_len) return 0;
    uint32_t avail = c->app_len - c->app_off;
    uint32_t n = avail < cap ? avail : cap;
    cpy(out, c->app + c->app_off, n);
    c->app_off += n;
    if (c->app_off >= c->app_len) { c->app_off = 0; c->app_len = 0; }
    return n;
}
