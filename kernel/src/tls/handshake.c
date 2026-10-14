/* handshake.c — TLS 1.3 client handshake. See handshake.h, and read the
 * authentication policy at the top of it before using this for anything. */
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
    case TLS_ERR_BAD_CERT:
        return "server certificate chain did not validate";
    case TLS_ERR_BAD_SIGNATURE:
        return "server CertificateVerify signature did not verify";
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
    c->authenticated = false; /* set only after chain, CertificateVerify and Finished */
    c->cert_status = TLS_CERT_NO_CERT;

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

void tls_client_set_trust(tls_client_t *c, const tls_trust_t *trust, int64_t now_unix)
{
    if (!c) return;
    c->trust = trust;
    c->now = now_unix;
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

    /* signature_algorithms (13): exactly what trust.c and the
     * CertificateVerify check below can verify. The rsa_pkcs1 entries apply
     * to signatures inside certificates only (RFC 8446 4.2.3); a
     * CertificateVerify that uses them is refused. */
    {
        uint8_t b[18] = {0x00, 0x10, 0x04, 0x03, /* ecdsa_secp256r1_sha256 */
                         0x05, 0x03,             /* ecdsa_secp384r1_sha384 */
                         0x08, 0x04,             /* rsa_pss_rsae_sha256 */
                         0x08, 0x05,             /* rsa_pss_rsae_sha384 */
                         0x08, 0x06,             /* rsa_pss_rsae_sha512 */
                         0x04, 0x01,             /* rsa_pkcs1_sha256 (certificates) */
                         0x05, 0x01,             /* rsa_pkcs1_sha384 (certificates) */
                         0x06, 0x01};            /* rsa_pkcs1_sha512 (certificates) */
        at = put_ext(m, at, 13, b, 18);
    }

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
    /* RFC 8446 4.1.3: legacy_session_id_echo must be exactly what we sent */
    if (sid_len != 32 || !ct_equal(p + at, c->session_id, 32)) {
        fail(c, TLS_ERR_PROTOCOL);
        return;
    }
    at += sid_len;

    uint16_t suite = g16(p + at); at += 2;
    if (suite != TLS_CS_CHACHA20_POLY1305_SHA256) { fail(c, TLS_ERR_UNSUPPORTED); return; }
    if (p[at] != 0) {
        fail(c, TLS_ERR_PROTOCOL);
        return;
    } /* legacy_compression_method */
    at += 1;

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

/* ---------------- server authentication ---------------- */

/* Certificate (RFC 8446 4.4.2): parse the list, validate the chain, keep the
 * leaf key. `m` includes the 4-byte header. */
static bool check_certificate(tls_client_t *c, const uint8_t *m, uint32_t n)
{
    if (!c->trust || c->now <= 0) {
        fail(c, TLS_ERR_NO_VERIFIER);
        return false;
    }
    const uint8_t *certs[TLS_CHAIN_MAX * 2];
    uint32_t lens[TLS_CHAIN_MAX * 2], count = 0;
    uint32_t at = 4;
    if (n < at + 1 || m[at] != 0) { /* certificate_request_context: empty */
        fail(c, TLS_ERR_PROTOCOL);
        return false;
    }
    at += 1;
    if (n < at + 3 || g24(m + at) != n - at - 3) {
        fail(c, TLS_ERR_PROTOCOL);
        return false;
    }
    at += 3;
    while (at < n) {
        if (n - at < 3) {
            fail(c, TLS_ERR_PROTOCOL);
            return false;
        }
        uint32_t cl = g24(m + at);
        at += 3;
        if (cl == 0 || cl > n - at) {
            fail(c, TLS_ERR_PROTOCOL);
            return false;
        }
        if (count == TLS_CHAIN_MAX * 2) {
            fail(c, TLS_ERR_TOO_BIG);
            return false;
        }
        certs[count] = m + at;
        lens[count++] = cl;
        at += cl;
        if (n - at < 2) {
            fail(c, TLS_ERR_PROTOCOL);
            return false;
        }
        uint32_t el = g16(m + at); /* per-certificate extensions: ignored */
        at += 2;
        if (el > n - at) {
            fail(c, TLS_ERR_PROTOCOL);
            return false;
        }
        at += el;
    }

    x509_cert_t leaf;
    c->cert_status = tls_verify_chain(c->trust, certs, lens, count, (const char *) c->hostname,
                                      c->hostname_len, c->now, &leaf);
    if (c->cert_status != TLS_CERT_OK) {
        fail(c, TLS_ERR_BAD_CERT);
        return false;
    }

    tls_peer_key_t *k = &c->peer_key;
    wipe((uint8_t *) k, sizeof *k);
    k->type = leaf.key.type;
    if (leaf.key.type == X509_KEY_RSA && leaf.key.n_len <= sizeof k->n &&
        leaf.key.e_len <= sizeof k->e) {
        cpy(k->n, leaf.key.n, leaf.key.n_len);
        k->n_len = leaf.key.n_len;
        cpy(k->e, leaf.key.e, leaf.key.e_len);
        k->e_len = leaf.key.e_len;
    } else if (leaf.key.type == X509_KEY_EC && leaf.key.point_len <= sizeof k->point) {
        k->curve = leaf.key.curve;
        cpy(k->point, leaf.key.point, leaf.key.point_len);
        k->point_len = leaf.key.point_len;
    } else {
        fail(c, TLS_ERR_BAD_CERT);
        return false;
    }
    c->cert_ok = true;
    return true;
}

/* CertificateVerify (RFC 8446 4.4.3): the leaf key's signature over
 * 64 spaces || "TLS 1.3, server CertificateVerify" || 0 || transcript hash. */
static bool check_certificate_verify(tls_client_t *c, const uint8_t *m, uint32_t n)
{
    if (n < 8 || g16(m + 6) != n - 8) {
        fail(c, TLS_ERR_PROTOCOL);
        return false;
    }
    uint16_t scheme = g16(m + 4);
    const tls_peer_key_t *pk = &c->peer_key;
    x509_sigalg_t alg = {X509_SIG_NONE, TLS_HASH_NONE};
    bool key_fits = false;
    switch (scheme) {
    case 0x0403:
        alg = (x509_sigalg_t){X509_SIG_ECDSA, TLS_HASH_SHA256};
        key_fits = pk->type == X509_KEY_EC && pk->curve == ECDSA_P256;
        break;
    case 0x0503:
        alg = (x509_sigalg_t){X509_SIG_ECDSA, TLS_HASH_SHA384};
        key_fits = pk->type == X509_KEY_EC && pk->curve == ECDSA_P384;
        break;
    case 0x0804:
    case 0x0805:
    case 0x0806:
        alg = (x509_sigalg_t){X509_SIG_RSA_PSS, scheme == 0x0804   ? TLS_HASH_SHA256
                                                : scheme == 0x0805 ? TLS_HASH_SHA384
                                                                   : TLS_HASH_SHA512};
        key_fits = pk->type == X509_KEY_RSA;
        break;
    default:
        break; /* rsa_pkcs1_*, ed25519, sha1 ...: not allowed here */
    }
    if (!key_fits) {
        fail(c, TLS_ERR_BAD_SIGNATURE);
        return false;
    }

    static const char ctx[] = "TLS 1.3, server CertificateVerify";
    uint8_t content[64 + sizeof ctx + HASH_LEN];
    uint32_t cl = 0;
    for (int i = 0; i < 64; i++) content[cl++] = 0x20;
    for (uint32_t i = 0; i < sizeof ctx; i++) content[cl++] = (uint8_t) ctx[i]; /* incl. NUL */
    tr_hash(c, content + cl);
    cl += HASH_LEN;

    x509_pubkey_t key = {0};
    key.type = pk->type;
    key.n = pk->n;
    key.n_len = pk->n_len;
    key.e = pk->e;
    key.e_len = pk->e_len;
    key.curve = pk->curve;
    key.point = pk->point;
    key.point_len = pk->point_len;
    if (!x509_verify_sig(&key, alg, content, cl, m + 8, n - 8)) {
        fail(c, TLS_ERR_BAD_SIGNATURE);
        return false;
    }
    c->cv_ok = true;
    return true;
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
        /* THE CHAIN DECISION. Only an explicit acknowledgement of an
         * unauthenticated channel skips it. */
        if (c->policy != TLS_VERIFY_INSECURE_ACKNOWLEDGED && !check_certificate(c, m, n)) return;
        c->state = TLS_ST_WAIT_CV;
        return;

    case TLS_ST_WAIT_CV:
        if (type != TLS_HS_CERTIFICATE_VERIFY) { fail(c, TLS_ERR_PROTOCOL); return; }
        /* THE KEY-POSSESSION DECISION: the signature is over the transcript
         * through Certificate, so it is checked before CertificateVerify
         * itself joins the transcript. */
        if (c->policy != TLS_VERIFY_INSECURE_ACKNOWLEDGED) {
            if (!c->cert_ok) {
                fail(c, TLS_ERR_NO_VERIFIER);
                return;
            }
            if (!check_certificate_verify(c, m, n)) return;
        }
        if (!tr_add(c, m, n)) return;
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
        /* authenticated only when the chain and the CertificateVerify both
         * checked out under TLS_VERIFY_REQUIRED; otherwise the channel is
         * encrypted, not verified */
        c->authenticated = c->policy == TLS_VERIFY_REQUIRED && c->cert_ok && c->cv_ok;
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
        uint8_t type = c->hs_buf[at];
        handle_hs_message(c, c->hs_buf + at, 4u + blen, reply, reply_cap, reply_len);
        at += 4u + blen;
        if (c->state == TLS_ST_FAILED) return;
        /* RFC 8446 5.1: handshake messages must not span a key change. The
         * ServerHello arrives in the clear and installs the handshake keys,
         * so anything after it in the same plaintext record (e.g. a forged
         * unencrypted EncryptedExtensions) is refused, not processed. */
        if (type == TLS_HS_SERVER_HELLO && at != c->hs_len) {
            fail(c, TLS_ERR_PROTOCOL);
            return;
        }
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
