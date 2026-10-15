/* selfsigned.c — a self-signed P-256 certificate. See selfsigned.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "selfsigned.h"
#include "ecdsa.h"
#include "../robin_debanks/sha256.h"

typedef struct {
    uint8_t *p;
    uint32_t n, cap;
    bool bad;
} dw_t;

static void put(dw_t *w, const uint8_t *s, uint32_t n)
{
    if (w->bad || n > w->cap - w->n) {
        w->bad = true;
        return;
    }
    for (uint32_t i = 0; i < n; i++) w->p[w->n + i] = s[i];
    w->n += n;
}
static void put1(dw_t *w, uint8_t b)
{
    put(w, &b, 1);
}
/* tag and definite length (at most 65535) */
static void hdr(dw_t *w, uint8_t tag, uint32_t len)
{
    put1(w, tag);
    if (len < 0x80) {
        put1(w, (uint8_t) len);
    } else if (len < 0x100) {
        put1(w, 0x81);
        put1(w, (uint8_t) len);
    } else {
        put1(w, 0x82);
        put1(w, (uint8_t) (len >> 8));
        put1(w, (uint8_t) len);
    }
}
static void tlv(dw_t *w, uint8_t tag, const uint8_t *v, uint32_t len)
{
    hdr(w, tag, len);
    put(w, v, len);
}
/* wrap everything written since `from` in tag: shift it right by the header */
static void wrap(dw_t *w, uint32_t from, uint8_t tag)
{
    if (w->bad) return;
    uint32_t len = w->n - from, h = len < 0x80 ? 2 : len < 0x100 ? 3 : 4;
    if (h > w->cap - w->n) {
        w->bad = true;
        return;
    }
    for (uint32_t i = w->n; i-- > from;) w->p[i + h] = w->p[i];
    uint32_t keep = w->n + h;
    w->n = from;
    hdr(w, tag, len);
    w->n = keep;
}

static const uint8_t OID_ECDSA_SHA256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
static const uint8_t OID_EC_PUBKEY[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
static const uint8_t OID_P256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
static const uint8_t OID_CN[] = {0x55, 0x04, 0x03};
static const uint8_t OID_SAN[] = {0x55, 0x1D, 0x11};
static const uint8_t OID_BC[] = {0x55, 0x1D, 0x13};
static const uint8_t OID_KU[] = {0x55, 0x1D, 0x0F};
static const uint8_t OID_EKU[] = {0x55, 0x1D, 0x25};
static const uint8_t OID_SERVER_AUTH[] = {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};

static void sig_alg(dw_t *w)
{
    uint32_t at = w->n;
    tlv(w, 0x06, OID_ECDSA_SHA256, sizeof OID_ECDSA_SHA256);
    wrap(w, at, 0x30);
}

static void name(dw_t *w, const char *cn, uint32_t cl)
{
    uint32_t at = w->n;
    tlv(w, 0x06, OID_CN, sizeof OID_CN);
    tlv(w, 0x0C, (const uint8_t *) cn, cl); /* UTF8String */
    wrap(w, at, 0x30);
    wrap(w, at, 0x31);
    wrap(w, at, 0x30);
}

static bool leap(uint32_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* C3: seconds since 1970 as UTCTime or GeneralizedTime; years and months by
 * subtraction (no 64-bit division on 32-bit targets) */
static bool when(dw_t *w, int64_t t)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (t < 0) return false;
    uint64_t s = (uint64_t) t;
    uint32_t y = 1970, mo = 0, d = 0, h = 0, mi = 0;
    for (;;) {
        uint64_t ys = (leap(y) ? 366u : 365u) * 86400ull;
        if (s < ys) break;
        s -= ys;
        if (++y > 9999) return false;
    }
    for (;; mo++) {
        uint64_t ms = (uint64_t) (mdays[mo] + (mo == 1 && leap(y))) * 86400u;
        if (s < ms) break;
        s -= ms;
    }
    /* under a month now: 32 bits, and division by constants only */
    uint32_t r = (uint32_t) s;
    d = r / 86400u;
    r %= 86400u;
    h = r / 3600u;
    r %= 3600u;
    mi = r / 60u;
    uint32_t v[6] = {y, mo + 1, d + 1, h, mi, r % 60u};
    uint8_t txt[16];
    uint32_t n = 0;
    bool utc = y >= 1950 && y <= 2049;
    if (utc) {
        uint32_t yy = y - (y >= 2000 ? 2000 : 1900);
        txt[n++] = (uint8_t) ('0' + yy / 10);
        txt[n++] = (uint8_t) ('0' + yy % 10);
    } else {
        txt[n++] = (uint8_t) ('0' + y / 1000);
        txt[n++] = (uint8_t) ('0' + y / 100 % 10);
        txt[n++] = (uint8_t) ('0' + y / 10 % 10);
        txt[n++] = (uint8_t) ('0' + y % 10);
    }
    for (int i = 1; i < 6; i++) {
        txt[n++] = (uint8_t) ('0' + v[i] / 10);
        txt[n++] = (uint8_t) ('0' + v[i] % 10);
    }
    txt[n++] = 'Z';
    tlv(w, utc ? 0x17 : 0x18, txt, n);
    return true;
}

static void ext(dw_t *w, const uint8_t *oid, uint32_t ol, bool critical, uint32_t value_from)
{
    /* the value was written at value_from; wrap it in an OCTET STRING and
     * put extnID and critical in front */
    wrap(w, value_from, 0x04);
    uint32_t vlen = w->n - value_from;
    uint8_t pre[16];
    dw_t q = {pre, 0, sizeof pre, false};
    tlv(&q, 0x06, oid, ol);
    if (critical) tlv(&q, 0x01, (const uint8_t *) "\xFF", 1);
    if (w->bad || q.n > w->cap - w->n) {
        w->bad = true;
        return;
    }
    for (uint32_t i = vlen; i-- > 0;) w->p[value_from + q.n + i] = w->p[value_from + i];
    for (uint32_t i = 0; i < q.n; i++) w->p[value_from + i] = pre[i];
    w->n += q.n;
    wrap(w, value_from, 0x30);
}

uint32_t tls_selfsigned_p256(const uint8_t priv[32], const uint8_t serial[16], const char *cn,
                             int64_t not_before, int64_t not_after, uint8_t *out, uint32_t cap)
{
    uint8_t pub[65], h[32], sig[ECDSA_P256_SIG_MAX], ser[16];
    uint32_t cl = 0;
    if (!priv || !serial || !cn || !out || not_after <= not_before) return 0;
    while (cn[cl] && cl <= 64) cl++;
    if (cl == 0 || cl > 64) return 0;
    if (!ecdsa_p256_public(priv, pub)) return 0;
    for (int i = 0; i < 16; i++) ser[i] = serial[i];
    ser[0] = (uint8_t) ((ser[0] & 0x7F) | 0x40); /* positive, 16 bytes, no pad */

    dw_t w = {out, 0, cap, false};
    /* TBSCertificate */
    tlv(&w, 0xA0, (const uint8_t *) "\x02\x01\x02", 3); /* [0] version v3 */
    tlv(&w, 0x02, ser, 16);
    sig_alg(&w);
    name(&w, cn, cl);
    uint32_t at = w.n;
    if (!when(&w, not_before) || !when(&w, not_after)) return 0;
    wrap(&w, at, 0x30);
    name(&w, cn, cl);
    at = w.n; /* SubjectPublicKeyInfo */
    uint32_t alg = w.n;
    tlv(&w, 0x06, OID_EC_PUBKEY, sizeof OID_EC_PUBKEY);
    tlv(&w, 0x06, OID_P256, sizeof OID_P256);
    wrap(&w, alg, 0x30);
    hdr(&w, 0x03, 66);
    put1(&w, 0);
    put(&w, pub, 65);
    wrap(&w, at, 0x30);

    uint32_t exts = w.n; /* [3] Extensions */
    uint32_t v = w.n;    /* subjectAltName */
    tlv(&w, 0x82, (const uint8_t *) "localhost", 9);
    tlv(&w, 0x87, (const uint8_t *) "\x7F\x00\x00\x01", 4);
    tlv(&w, 0x87, (const uint8_t *) "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01", 16);
    wrap(&w, v, 0x30);
    ext(&w, OID_SAN, sizeof OID_SAN, false, v);
    v = w.n; /* basicConstraints: cA FALSE (the DEFAULT, so an empty SEQUENCE) */
    hdr(&w, 0x30, 0);
    ext(&w, OID_BC, sizeof OID_BC, true, v);
    v = w.n; /* keyUsage: digitalSignature (bit 0) */
    tlv(&w, 0x03, (const uint8_t *) "\x07\x80", 2);
    ext(&w, OID_KU, sizeof OID_KU, true, v);
    v = w.n; /* extendedKeyUsage: serverAuth */
    tlv(&w, 0x06, OID_SERVER_AUTH, sizeof OID_SERVER_AUTH);
    wrap(&w, v, 0x30);
    ext(&w, OID_EKU, sizeof OID_EKU, false, v);
    wrap(&w, exts, 0x30);
    wrap(&w, exts, 0xA3);
    wrap(&w, 0, 0x30); /* the TBSCertificate SEQUENCE */
    if (w.bad) return 0;

    sha256(out, w.n, h);
    uint32_t sl = ecdsa_p256_sign_digest(priv, h, 32, sig, sizeof sig);
    if (!sl) return 0;
    sig_alg(&w);
    hdr(&w, 0x03, sl + 1);
    put1(&w, 0);
    put(&w, sig, sl);
    wrap(&w, 0, 0x30);
    return w.bad ? 0 : w.n;
}

uint32_t tls_cert_pem(const uint8_t *der, uint32_t len, char *out, uint32_t cap)
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char HEAD[] = "-----BEGIN CERTIFICATE-----\n";
    static const char TAIL[] = "-----END CERTIFICATE-----\n";
    uint32_t n = 0, col = 0;
#define EMIT(ch)                                                                                   \
    do {                                                                                           \
        if (n + 1 >= cap) return 0;                                                                \
        out[n++] = (ch);                                                                           \
    } while (0)
    if (!der || !out || cap == 0) return 0;
    for (uint32_t i = 0; HEAD[i]; i++) EMIT(HEAD[i]);
    for (uint32_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t) der[i] << 16;
        if (i + 1 < len) b |= (uint32_t) der[i + 1] << 8;
        if (i + 2 < len) b |= der[i + 2];
        EMIT(B64[(b >> 18) & 63]);
        EMIT(B64[(b >> 12) & 63]);
        EMIT(i + 1 < len ? B64[(b >> 6) & 63] : '=');
        EMIT(i + 2 < len ? B64[b & 63] : '=');
        if ((col += 4) == 64 || i + 3 >= len) {
            EMIT('\n');
            col = 0;
        }
    }
    for (uint32_t i = 0; TAIL[i]; i++) EMIT(TAIL[i]);
#undef EMIT
    out[n] = 0;
    return n;
}
