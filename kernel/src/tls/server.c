/* server.c — the TLS 1.3 server handshake. See server.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "server.h"
#include "ecdsa.h"
#include "../robin_debanks/sha256.h"

/* alert descriptions (RFC 8446 6) */
#define AL_CLOSE_NOTIFY       0u
#define AL_UNEXPECTED_MESSAGE 10u
#define AL_BAD_RECORD_MAC     20u
#define AL_RECORD_OVERFLOW    22u
#define AL_HANDSHAKE_FAILURE  40u
#define AL_ILLEGAL_PARAMETER  47u
#define AL_DECODE_ERROR       50u
#define AL_DECRYPT_ERROR      51u
#define AL_PROTOCOL_VERSION   70u
#define AL_INTERNAL_ERROR     80u
#define AL_MISSING_EXTENSION  109u

#define TLS_HS_KEY_UPDATE 24u

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void wipe(uint8_t *d, uint32_t n)
{
    volatile uint8_t *v = d;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}
static uint16_t g16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}
static uint32_t g24(const uint8_t *p)
{
    return ((uint32_t) p[0] << 16) | ((uint32_t) p[1] << 8) | p[2];
}

typedef struct {
    uint8_t *p;
    uint32_t cap, *len;
} out_t;

/* V4: send a fatal alert and fail */
static void fail(tls_server_t *s, out_t *o, tls_error_t e, uint8_t desc)
{
    if (s->state == TLS_SRV_FAILED) return;
    s->state = TLS_SRV_FAILED;
    if (s->error == TLS_ERR_NONE) s->error = e;
    s->alert_sent = desc;
    uint8_t a[2] = {2, desc};
    if (o && o->p && *o->len < o->cap)
        *o->len += tls_record_write(&s->tx, TLS_CT_ALERT, a, 2, o->p + *o->len, o->cap - *o->len);
}

static bool tr_add(tls_server_t *s, out_t *o, const uint8_t *d, uint32_t n)
{
    if (s->transcript_len + n > TLS_TRANSCRIPT_MAX) {
        fail(s, o, TLS_ERR_TOO_BIG, AL_INTERNAL_ERROR);
        return false;
    }
    cpy(s->transcript + s->transcript_len, d, n);
    s->transcript_len += n;
    return true;
}

static bool emit(tls_server_t *s, out_t *o, uint8_t type, const uint8_t *m, uint32_t n)
{
    uint32_t w = tls_record_write(&s->tx, type, m, n, o->p + *o->len, o->cap - *o->len);
    if (!w) {
        fail(s, o, TLS_ERR_TOO_BIG, AL_INTERNAL_ERROR);
        return false;
    }
    *o->len += w;
    return true;
}

/* one handshake message: header, transcript, record */
static bool hs_out(tls_server_t *s, out_t *o, uint8_t type, const uint8_t *body, uint32_t n)
{
    static uint8_t m[4 + TLS_SRV_CERT_MAX + 16];
    if (n > sizeof m - 4) {
        fail(s, o, TLS_ERR_TOO_BIG, AL_INTERNAL_ERROR);
        return false;
    }
    m[0] = type;
    m[1] = (uint8_t) (n >> 16);
    m[2] = (uint8_t) (n >> 8);
    m[3] = (uint8_t) n;
    cpy(m + 4, body, n);
    return tr_add(s, o, m, 4 + n) && emit(s, o, TLS_CT_HANDSHAKE, m, 4 + n);
}

bool tls_server_init(tls_server_t *s, const uint8_t *cert_der, uint32_t cert_len,
                     const uint8_t key32[32], const uint8_t random32[32],
                     const uint8_t x25519_priv32[X25519_LEN])
{
    if (!s) return false;
    wipe((uint8_t *) s, sizeof *s);
    tls_keys_clear(&s->rx);
    tls_keys_clear(&s->tx);
    uint8_t pub[65];
    if (!cert_der || !key32 || !random32 || !x25519_priv32 || cert_len == 0 ||
        cert_len > TLS_SRV_CERT_MAX || !ecdsa_p256_public(key32, pub)) {
        s->state = TLS_SRV_FAILED;
        s->error = TLS_ERR_UNSUPPORTED;
        return false;
    }
    cpy(s->cert, cert_der, cert_len);
    s->cert_len = cert_len;
    cpy(s->key, key32, 32);
    cpy(s->random, random32, 32);
    cpy(s->priv, x25519_priv32, X25519_LEN);
    x25519_public(s->pub, s->priv);
    s->state = TLS_SRV_WAIT_CH;
    return true;
}

/* the extensions block: find one; false also when a length lies */
static int ext_find(const uint8_t *e, uint32_t n, uint16_t want, const uint8_t **b, uint16_t *bl)
{
    uint32_t at = 0;
    int found = 0;
    while (at < n) {
        if (n - at < 4) return -1;
        uint16_t t = g16(e + at), l = g16(e + at + 2);
        if (l > n - at - 4) return -1;
        if (t == want) {
            if (found) return -1; /* a repeated extension: RFC 8446 4.2 */
            *b = e + at + 4;
            *bl = l;
            found = 1;
        }
        at += 4u + l;
    }
    return found;
}

static void client_hello(tls_server_t *s, out_t *o, const uint8_t *m, uint32_t n)
{
    const uint8_t *p = m + 4;
    uint32_t left = n - 4, at = 0;
    if (left < 2 + 32 + 1) goto decode;
    if (g16(p) < 0x0303) {
        fail(s, o, TLS_ERR_UNSUPPORTED, AL_PROTOCOL_VERSION);
        return;
    }
    at = 2 + 32;
    uint8_t sid_len = p[at++];
    if (sid_len > 32 || left - at < sid_len + 2u) goto decode;
    const uint8_t *sid = p + at;
    at += sid_len;
    uint16_t cs_len = g16(p + at);
    at += 2;
    if ((cs_len & 1) || cs_len == 0 || left - at < cs_len + 1u) goto decode;
    bool suite = false;
    for (uint32_t i = 0; i < cs_len; i += 2)
        if (g16(p + at + i) == TLS_CS_CHACHA20_POLY1305_SHA256) suite = true;
    at += cs_len;
    uint8_t cm_len = p[at++];
    if (cm_len != 1 || left - at < 1u + 2u || p[at] != 0) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_ILLEGAL_PARAMETER);
        return;
    }
    at += 1;
    uint16_t ext_len = g16(p + at);
    at += 2;
    if (ext_len != left - at) goto decode;
    const uint8_t *ext = p + at, *b;
    uint16_t bl;

    /* supported_versions must offer TLS 1.3 */
    int r = ext_find(ext, ext_len, 43, &b, &bl);
    if (r < 0) goto decode;
    bool v13 = false;
    if (r == 1) {
        if (bl < 1 || b[0] != bl - 1u || (b[0] & 1)) goto decode;
        for (uint32_t i = 1; i + 1 < bl; i += 2)
            if (g16(b + i) == TLS_VERSION_13) v13 = true;
    }
    if (!v13) {
        fail(s, o, TLS_ERR_UNSUPPORTED, AL_PROTOCOL_VERSION);
        return;
    }
    if (!suite) {
        fail(s, o, TLS_ERR_UNSUPPORTED, AL_HANDSHAKE_FAILURE);
        return;
    }
    /* signature_algorithms must offer ecdsa_secp256r1_sha256 */
    r = ext_find(ext, ext_len, 13, &b, &bl);
    if (r < 0) goto decode;
    if (r == 0) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_MISSING_EXTENSION);
        return;
    }
    if (bl < 2 || g16(b) != bl - 2u || (bl & 1)) goto decode;
    bool sig = false;
    for (uint32_t i = 2; i < bl; i += 2)
        if (g16(b + i) == 0x0403) sig = true;
    if (!sig) {
        fail(s, o, TLS_ERR_UNSUPPORTED, AL_HANDSHAKE_FAILURE);
        return;
    }
    /* key_share: an x25519 entry (V1: no HelloRetryRequest) */
    r = ext_find(ext, ext_len, 51, &b, &bl);
    if (r < 0) goto decode;
    if (r == 0) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_MISSING_EXTENSION);
        return;
    }
    if (bl < 2 || g16(b) != bl - 2u) goto decode;
    const uint8_t *share = 0;
    for (uint32_t i = 2; i < bl;) {
        if (bl - i < 4) goto decode;
        uint16_t grp = g16(b + i), kl = g16(b + i + 2);
        if (kl > bl - i - 4) goto decode;
        if (grp == TLS_GROUP_X25519) {
            if (kl != X25519_LEN) {
                fail(s, o, TLS_ERR_PROTOCOL, AL_ILLEGAL_PARAMETER);
                return;
            }
            if (!share) share = b + i + 4;
        }
        i += 4u + kl;
    }
    if (!share) {
        fail(s, o, TLS_ERR_HELLO_RETRY, AL_HANDSHAKE_FAILURE);
        return;
    }
    uint8_t shared[X25519_LEN];
    if (!x25519_shared(shared, s->priv, share)) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_ILLEGAL_PARAMETER); /* RFC 8446 7.4.2 */
        return;
    }
    if (!tr_add(s, o, m, n)) return;

    /* ServerHello */
    uint8_t sh[2 + 32 + 1 + 32 + 3 + 2 + 6 + 8 + X25519_LEN];
    uint32_t k = 0;
    sh[k++] = 3;
    sh[k++] = 3;
    cpy(sh + k, s->random, 32);
    k += 32;
    sh[k++] = sid_len;
    cpy(sh + k, sid, sid_len);
    k += sid_len;
    sh[k++] = (uint8_t) (TLS_CS_CHACHA20_POLY1305_SHA256 >> 8);
    sh[k++] = (uint8_t) TLS_CS_CHACHA20_POLY1305_SHA256;
    sh[k++] = 0;
    sh[k++] = 0;
    sh[k++] = 6 + 8 + X25519_LEN;
    static const uint8_t ext_sv[6] = {0, 43, 0, 2, 3, 4};
    cpy(sh + k, ext_sv, 6);
    k += 6;
    uint8_t ks[8] = {0, 51, 0, 4 + X25519_LEN, 0, 0x1D, 0, X25519_LEN};
    cpy(sh + k, ks, 8);
    k += 8;
    cpy(sh + k, s->pub, X25519_LEN);
    k += X25519_LEN;
    if (!hs_out(s, o, TLS_HS_SERVER_HELLO, sh, k)) goto out;
    if (sid_len && !emit(s, o, TLS_CT_CHANGE_CIPHER_SPEC, (const uint8_t *) "\x01", 1)) goto out;

    tls13_early_secret(&s->sched, 0, 0);
    if (!tls13_handshake_secret(&s->sched, shared, X25519_LEN, s->transcript, s->transcript_len)) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_INTERNAL_ERROR);
        goto out;
    }
    uint8_t key[CHACHA20_KEY_LEN], iv[CHACHA20_NONCE_LEN];
    tls13_traffic_keys(s->sched.s_hs_traffic, key, sizeof key, iv, sizeof iv);
    tls_keys_set(&s->tx, key, iv);
    tls13_traffic_keys(s->sched.c_hs_traffic, key, sizeof key, iv, sizeof iv);
    tls_keys_set(&s->rx, key, iv);

    /* EncryptedExtensions (empty), Certificate, CertificateVerify, Finished */
    if (!hs_out(s, o, TLS_HS_ENCRYPTED_EXTENSIONS, (const uint8_t *) "\0\0", 2)) goto out;
    {
        static uint8_t c[TLS_SRV_CERT_MAX + 16];
        uint32_t cl = s->cert_len, list = cl + 5;
        c[0] = 0; /* certificate_request_context */
        c[1] = (uint8_t) (list >> 16);
        c[2] = (uint8_t) (list >> 8);
        c[3] = (uint8_t) list;
        c[4] = (uint8_t) (cl >> 16);
        c[5] = (uint8_t) (cl >> 8);
        c[6] = (uint8_t) cl;
        cpy(c + 7, s->cert, cl);
        c[7 + cl] = 0; /* no per-certificate extensions */
        c[8 + cl] = 0;
        if (!hs_out(s, o, TLS_HS_CERTIFICATE, c, cl + 9)) goto out;
    }
    {
        static const char ctx[] = "TLS 1.3, server CertificateVerify";
        uint8_t content[64 + sizeof ctx + HASH_LEN], h[HASH_LEN], cv[4 + ECDSA_P256_SIG_MAX];
        uint32_t cl = 0;
        for (int i = 0; i < 64; i++) content[cl++] = 0x20;
        for (uint32_t i = 0; i < sizeof ctx; i++) content[cl++] = (uint8_t) ctx[i];
        sha256(s->transcript, s->transcript_len, content + cl);
        cl += HASH_LEN;
        sha256(content, cl, h);
        uint32_t sl = ecdsa_p256_sign_digest(s->key, h, HASH_LEN, cv + 4, ECDSA_P256_SIG_MAX);
        if (!sl) {
            fail(s, o, TLS_ERR_PROTOCOL, AL_INTERNAL_ERROR);
            goto out;
        }
        cv[0] = 0x04;
        cv[1] = 0x03;
        cv[2] = (uint8_t) (sl >> 8);
        cv[3] = (uint8_t) sl;
        if (!hs_out(s, o, TLS_HS_CERTIFICATE_VERIFY, cv, 4 + sl)) goto out;
    }
    {
        uint8_t th[HASH_LEN], vd[HASH_LEN];
        sha256(s->transcript, s->transcript_len, th);
        tls13_finished(s->sched.s_hs_traffic, th, vd);
        if (!hs_out(s, o, TLS_HS_FINISHED, vd, HASH_LEN)) goto out;
    }
    /* V3: application secrets over ClientHello..server Finished; our sending
     * side switches now, the client's after its Finished */
    sha256(s->transcript, s->transcript_len, s->th_srv_fin);
    if (!tls13_master_secret(&s->sched, s->transcript, s->transcript_len)) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_INTERNAL_ERROR);
        goto out;
    }
    cpy(s->s_ap, s->sched.s_ap_traffic, HASH_LEN);
    cpy(s->c_ap, s->sched.c_ap_traffic, HASH_LEN);
    tls13_traffic_keys(s->s_ap, key, sizeof key, iv, sizeof iv);
    tls_keys_set(&s->tx, key, iv);
    s->state = TLS_SRV_WAIT_FIN;
out:
    wipe(shared, sizeof shared);
    wipe(key, sizeof key);
    wipe(iv, sizeof iv);
    return;
decode:
    fail(s, o, TLS_ERR_PROTOCOL, AL_DECODE_ERROR);
}

/* V5: next = HKDF-Expand-Label(secret, "traffic upd", "", 32) */
static void key_step(uint8_t secret[HASH_LEN], tls_keys_t *k)
{
    uint8_t next[HASH_LEN], key[CHACHA20_KEY_LEN], iv[CHACHA20_NONCE_LEN];
    tls13_expand_label(secret, "traffic upd", 0, 0, next, HASH_LEN);
    cpy(secret, next, HASH_LEN);
    tls13_traffic_keys(secret, key, sizeof key, iv, sizeof iv);
    tls_keys_set(k, key, iv);
    wipe(next, sizeof next);
    wipe(key, sizeof key);
    wipe(iv, sizeof iv);
}

static void hs_message(tls_server_t *s, out_t *o, const uint8_t *m, uint32_t n)
{
    uint8_t type = m[0];
    if (s->state == TLS_SRV_WAIT_CH) {
        if (type != TLS_HS_CLIENT_HELLO) {
            fail(s, o, TLS_ERR_PROTOCOL, AL_UNEXPECTED_MESSAGE);
            return;
        }
        client_hello(s, o, m, n);
        return;
    }
    if (s->state == TLS_SRV_WAIT_FIN) {
        if (type != TLS_HS_FINISHED) {
            fail(s, o, TLS_ERR_PROTOCOL, AL_UNEXPECTED_MESSAGE);
            return;
        }
        if (n != 4 + HASH_LEN) {
            fail(s, o, TLS_ERR_PROTOCOL, AL_DECODE_ERROR);
            return;
        }
        uint8_t want[HASH_LEN];
        tls13_finished(s->sched.c_hs_traffic, s->th_srv_fin, want);
        if (!ct_equal(want, m + 4, HASH_LEN)) {
            fail(s, o, TLS_ERR_BAD_FINISHED, AL_DECRYPT_ERROR);
            return;
        }
        uint8_t key[CHACHA20_KEY_LEN], iv[CHACHA20_NONCE_LEN];
        tls13_traffic_keys(s->c_ap, key, sizeof key, iv, sizeof iv);
        tls_keys_set(&s->rx, key, iv);
        wipe(key, sizeof key);
        wipe(iv, sizeof iv);
        wipe((uint8_t *) &s->sched, sizeof s->sched);
        wipe(s->transcript, sizeof s->transcript);
        s->transcript_len = 0;
        s->state = TLS_SRV_ESTABLISHED;
        return;
    }
    /* established: only KeyUpdate (V5) */
    if (type != TLS_HS_KEY_UPDATE) {
        fail(s, o, TLS_ERR_PROTOCOL, AL_UNEXPECTED_MESSAGE);
        return;
    }
    if (n != 5 || m[4] > 1) {
        fail(s, o, TLS_ERR_PROTOCOL, n != 5 ? AL_DECODE_ERROR : AL_ILLEGAL_PARAMETER);
        return;
    }
    key_step(s->c_ap, &s->rx);
    if (m[4] == 1) { /* update_requested: answer, then step our own keys */
        static const uint8_t ku[5] = {TLS_HS_KEY_UPDATE, 0, 0, 1, 0};
        if (!emit(s, o, TLS_CT_HANDSHAKE, ku, 5)) return;
        key_step(s->s_ap, &s->tx);
    }
}

static void drain(tls_server_t *s, out_t *o, bool before_keys)
{
    uint32_t at = 0;
    while (s->state != TLS_SRV_FAILED && s->hs_len - at >= 4u) {
        uint32_t blen = g24(s->hs_buf + at + 1);
        if (blen > TLS_HS_MSG_MAX - 4u) {
            fail(s, o, TLS_ERR_TOO_BIG, AL_RECORD_OVERFLOW);
            return;
        }
        if (s->hs_len - at < 4u + blen) break;
        hs_message(s, o, s->hs_buf + at, 4u + blen);
        at += 4u + blen;
        /* RFC 8446 5.1: the ClientHello may not share a record with what
         * follows it, since the keys change after it */
        if (before_keys && s->state != TLS_SRV_FAILED && at != s->hs_len) {
            fail(s, o, TLS_ERR_PROTOCOL, AL_UNEXPECTED_MESSAGE);
            return;
        }
    }
    if (at) {
        for (uint32_t i = 0; i + at < s->hs_len; i++) s->hs_buf[i] = s->hs_buf[i + at];
        s->hs_len -= at;
    }
}

uint32_t tls_server_feed(tls_server_t *s, const uint8_t *in, uint32_t in_len, uint8_t *reply,
                         uint32_t reply_cap, uint32_t *reply_len)
{
    uint32_t dummy = 0;
    if (!reply_len) reply_len = &dummy;
    *reply_len = 0;
    out_t o = {reply, reply_cap, reply_len};
    if (!s || !in || !reply || s->state == TLS_SRV_FAILED) return 0;
    static uint8_t pt[TLS_MAX_CIPHERTEXT];
    uint32_t off = 0;
    while (off < in_len && s->state != TLS_SRV_FAILED) {
        uint32_t used = 0, plen = 0;
        uint8_t ptype = 0;
        bool before_keys = !s->rx.active;
        int rc =
            tls_record_read(&s->rx, in + off, in_len - off, &used, &ptype, pt, sizeof pt, &plen);
        if (rc == TLS_REC_NEED_MORE) break;
        if (rc == TLS_REC_BAD) {
            fail(s, &o, TLS_ERR_PROTOCOL, AL_DECODE_ERROR);
            break;
        }
        if (rc == TLS_REC_AUTH_FAIL) {
            fail(s, &o, TLS_ERR_BAD_RECORD, AL_BAD_RECORD_MAC);
            break;
        }
        off += used;
        if (rc == TLS_REC_SKIP) continue;
        if (ptype == TLS_CT_ALERT) {
            if (plen != 2) {
                fail(s, &o, TLS_ERR_PROTOCOL, AL_DECODE_ERROR);
                break;
            }
            if (pt[1] == AL_CLOSE_NOTIFY) {
                s->peer_closed = true;
                continue;
            }
            s->alert_got = pt[1];
            s->state = TLS_SRV_FAILED; /* never answer an alert with one */
            if (s->error == TLS_ERR_NONE) s->error = TLS_ERR_ALERT;
            break;
        }
        if (ptype == TLS_CT_HANDSHAKE) {
            if (plen == 0 || s->hs_len + plen > TLS_HS_MSG_MAX) {
                fail(s, &o, TLS_ERR_TOO_BIG, AL_DECODE_ERROR);
                break;
            }
            cpy(s->hs_buf + s->hs_len, pt, plen);
            s->hs_len += plen;
            drain(s, &o, before_keys);
            continue;
        }
        if (ptype == TLS_CT_APPLICATION_DATA && s->state == TLS_SRV_ESTABLISHED) {
            if (s->app_off) {
                for (uint32_t i = 0; i + s->app_off < s->app_len; i++)
                    s->app[i] = s->app[i + s->app_off];
                s->app_len -= s->app_off;
                s->app_off = 0;
            }
            if (s->app_len + plen > sizeof s->app) {
                /* the caller has not read: stop here, keep the record */
                off -= used;
                s->rx.seq--; /* it will be decrypted again */
                break;
            }
            cpy(s->app + s->app_len, pt, plen);
            s->app_len += plen;
            continue;
        }
        fail(s, &o, TLS_ERR_PROTOCOL, AL_UNEXPECTED_MESSAGE);
        break;
    }
    return off;
}

uint32_t tls_server_send(tls_server_t *s, const uint8_t *data, uint32_t len, uint8_t *out,
                         uint32_t cap)
{
    if (!s || s->state != TLS_SRV_ESTABLISHED || len > TLS_MAX_PLAINTEXT) return 0;
    return tls_record_write(&s->tx, TLS_CT_APPLICATION_DATA, data, len, out, cap);
}

uint32_t tls_server_read(tls_server_t *s, uint8_t *out, uint32_t cap)
{
    if (!s || !out || s->app_off >= s->app_len) return 0;
    uint32_t n = s->app_len - s->app_off;
    if (n > cap) n = cap;
    cpy(out, s->app + s->app_off, n);
    s->app_off += n;
    if (s->app_off >= s->app_len) s->app_off = s->app_len = 0;
    return n;
}

uint32_t tls_server_close(tls_server_t *s, uint8_t *out, uint32_t cap)
{
    static const uint8_t a[2] = {1, AL_CLOSE_NOTIFY};
    if (!s || s->state == TLS_SRV_FAILED || !s->tx.active) return 0;
    return tls_record_write(&s->tx, TLS_CT_ALERT, a, 2, out, cap);
}
