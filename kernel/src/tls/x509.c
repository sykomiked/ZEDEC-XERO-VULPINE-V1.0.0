/* x509.c — strict X.509 v3 parsing and certificate signature checks. See
 * x509.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "x509.h"
#include "rsa.h"

/* ---- DER reader --------------------------------------------------------- */

typedef struct {
    const uint8_t *p;
    uint32_t len;
} der_t;

/* Read one TLV from the front of `cur`. Single-byte tags, definite minimal
 * lengths of at most four bytes. `val` gets the contents, `whole` (if not
 * null) the full encoding. */
static bool der_next(der_t *cur, uint8_t *tag, der_t *val, der_t *whole)
{
    if (cur->len < 2) return false;
    const uint8_t *start = cur->p;
    uint8_t t = cur->p[0];
    if ((t & 0x1f) == 0x1f) return false; /* multi-byte tag */
    uint32_t at = 1, l = cur->p[at++];
    if (l & 0x80) {
        uint32_t nb = l & 0x7f;
        if (nb == 0 || nb > 4 || nb > cur->len - at) return false;
        if (cur->p[at] == 0) return false; /* leading zero: not minimal */
        l = 0;
        for (uint32_t i = 0; i < nb; i++) l = (l << 8) | cur->p[at++];
        if (l < 0x80) return false; /* long form for a short length */
    }
    if (l > cur->len - at) return false;
    *tag = t;
    val->p = cur->p + at;
    val->len = l;
    if (whole) {
        whole->p = start;
        whole->len = at + l;
    }
    cur->p += at + l;
    cur->len -= at + l;
    return true;
}

static bool der_expect(der_t *cur, uint8_t want, der_t *val, der_t *whole)
{
    uint8_t t;
    der_t save = *cur;
    if (!der_next(cur, &t, val, whole) || t != want) {
        *cur = save;
        return false;
    }
    return true;
}

static bool der_peek(const der_t *cur, uint8_t want)
{
    return cur->len > 0 && cur->p[0] == want;
}

static bool eq(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen)
{
    if (alen != blen) return false;
    for (uint32_t i = 0; i < alen; i++)
        if (a[i] != b[i]) return false;
    return true;
}

#define OID_EQ(v, oid) eq((v).p, (v).len, oid, sizeof(oid))

/* ---- object identifiers (DER contents) ---------------------------------- */

static const uint8_t OID_RSA[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
static const uint8_t OID_RSA_SHA256[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
static const uint8_t OID_RSA_SHA384[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c};
static const uint8_t OID_RSA_SHA512[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d};
static const uint8_t OID_RSA_PSS[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a};
static const uint8_t OID_MGF1[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08};
static const uint8_t OID_EC_PUB[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const uint8_t OID_P256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const uint8_t OID_P384[] = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const uint8_t OID_ECDSA_SHA256[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
static const uint8_t OID_ECDSA_SHA384[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03};
static const uint8_t OID_ECDSA_SHA512[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x04};
static const uint8_t OID_SHA256[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
static const uint8_t OID_SHA384[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02};
static const uint8_t OID_SHA512[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03};
static const uint8_t OID_EKU_SERVER[] = {0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
static const uint8_t OID_EKU_ANY[] = {0x55, 0x1d, 0x25, 0x00};

/* extensions under id-ce (2.5.29) are 55 1d xx */
#define CE_SKI 0x0e
#define CE_KU  0x0f
#define CE_SAN 0x11
#define CE_BC  0x13
#define CE_NC  0x1e
#define CE_CRL 0x1f
#define CE_POL 0x20
#define CE_AKI 0x23
#define CE_EKU 0x25

/* ---- time --------------------------------------------------------------- */

/* days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's
 * days_from_civil), in 32-bit arithmetic: years 1950-9999 stay far inside
 * int32, and 32-bit targets get no 64-bit division helper call */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int32_t era = y / 400; /* y >= 1949, so no negative-year correction */
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (int32_t) (m > 2 ? m - 3 : m + 9) + 2) / 5 + (int32_t) d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t x509_unix_time(uint32_t y, uint32_t mo, uint32_t d, uint32_t h, uint32_t mi, uint32_t s)
{
    static const uint8_t mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1950 || y > 9999 || mo < 1 || mo > 12 || d < 1 || h > 23 || mi > 59 || s > 59)
        return -1;
    uint32_t leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (d > mdays[mo - 1] || (mo == 2 && d == 29 && !leap)) return -1;
    return (int64_t) days_from_civil((int32_t) y, mo, d) * 86400 +
           (int64_t) (h * 3600 + mi * 60 + s);
}

static bool digits(const uint8_t *p, uint32_t n, uint32_t *out)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return true;
}

/* UTCTime YYMMDDHHMMSSZ or GeneralizedTime YYYYMMDDHHMMSSZ, nothing else */
static bool parse_time(der_t *cur, int64_t *out)
{
    uint8_t tag;
    der_t v;
    if (!der_next(cur, &tag, &v, 0)) return false;
    uint32_t y, mo, d, h, mi, s, at;
    if (tag == 0x17 && v.len == 13) {
        if (!digits(v.p, 2, &y)) return false;
        y += y < 50 ? 2000 : 1900;
        at = 2;
    } else if (tag == 0x18 && v.len == 15) {
        if (!digits(v.p, 4, &y)) return false;
        at = 4;
    } else
        return false;
    if (v.p[v.len - 1] != 'Z') return false;
    if (!digits(v.p + at, 2, &mo) || !digits(v.p + at + 2, 2, &d) || !digits(v.p + at + 4, 2, &h) ||
        !digits(v.p + at + 6, 2, &mi) || !digits(v.p + at + 8, 2, &s))
        return false;
    *out = x509_unix_time(y, mo, d, h, mi, s);
    return *out >= 0;
}

/* ---- algorithms --------------------------------------------------------- */

static tls_hash_t hash_oid(der_t oid)
{
    if (OID_EQ(oid, OID_SHA256)) return TLS_HASH_SHA256;
    if (OID_EQ(oid, OID_SHA384)) return TLS_HASH_SHA384;
    if (OID_EQ(oid, OID_SHA512)) return TLS_HASH_SHA512;
    return TLS_HASH_NONE;
}

/* A hash AlgorithmIdentifier: SEQUENCE { OID, NULL or absent }. */
static tls_hash_t parse_hash_alg(der_t ai)
{
    der_t oid, nul;
    if (!der_expect(&ai, 0x06, &oid, 0)) return TLS_HASH_NONE;
    if (ai.len && (!der_expect(&ai, 0x05, &nul, 0) || nul.len)) return TLS_HASH_NONE;
    if (ai.len) return TLS_HASH_NONE;
    return hash_oid(oid);
}

/* RSASSA-PSS-params with every field explicit and the TLS-profile values:
 * MGF1 over the same hash, salt length = digest length, trailer 1. */
static tls_hash_t parse_pss_params(der_t params)
{
    der_t f, ai, mgf_oid, mgf_ai, salt, trailer;
    if (!der_expect(&params, 0xa0, &f, 0) || !der_expect(&f, 0x30, &ai, 0) || f.len)
        return TLS_HASH_NONE;
    tls_hash_t h = parse_hash_alg(ai);
    if (h == TLS_HASH_NONE) return TLS_HASH_NONE;
    if (!der_expect(&params, 0xa1, &f, 0) || !der_expect(&f, 0x30, &ai, 0) || f.len)
        return TLS_HASH_NONE;
    if (!der_expect(&ai, 0x06, &mgf_oid, 0) || !OID_EQ(mgf_oid, OID_MGF1)) return TLS_HASH_NONE;
    if (!der_expect(&ai, 0x30, &mgf_ai, 0) || ai.len) return TLS_HASH_NONE;
    if (parse_hash_alg(mgf_ai) != h) return TLS_HASH_NONE;
    if (!der_expect(&params, 0xa2, &f, 0) || !der_expect(&f, 0x02, &salt, 0) || f.len)
        return TLS_HASH_NONE;
    if (salt.len != 1 || salt.p[0] != tls_digest_len(h)) return TLS_HASH_NONE;
    if (params.len) {
        if (!der_expect(&params, 0xa3, &f, 0) || !der_expect(&f, 0x02, &trailer, 0) || f.len)
            return TLS_HASH_NONE;
        if (trailer.len != 1 || trailer.p[0] != 1) return TLS_HASH_NONE;
    }
    return params.len ? TLS_HASH_NONE : h;
}

/* signatureAlgorithm AlgorithmIdentifier contents */
static x509_err_t parse_sigalg(der_t ai, x509_sigalg_t *out)
{
    der_t oid, nul;
    out->kind = X509_SIG_NONE;
    out->hash = TLS_HASH_NONE;
    if (!der_expect(&ai, 0x06, &oid, 0)) return X509_ERR_PARSE;
    if (OID_EQ(oid, OID_RSA_SHA256) || OID_EQ(oid, OID_RSA_SHA384) || OID_EQ(oid, OID_RSA_SHA512)) {
        /* parameters NULL (RFC 4055) or absent */
        if (ai.len && (!der_expect(&ai, 0x05, &nul, 0) || nul.len)) return X509_ERR_PARSE;
        if (ai.len) return X509_ERR_PARSE;
        out->kind = X509_SIG_RSA_PKCS1;
        out->hash = OID_EQ(oid, OID_RSA_SHA256)   ? TLS_HASH_SHA256
                    : OID_EQ(oid, OID_RSA_SHA384) ? TLS_HASH_SHA384
                                                  : TLS_HASH_SHA512;
        return X509_OK;
    }
    if (OID_EQ(oid, OID_ECDSA_SHA256) || OID_EQ(oid, OID_ECDSA_SHA384) ||
        OID_EQ(oid, OID_ECDSA_SHA512)) {
        if (ai.len) return X509_ERR_PARSE; /* parameters MUST be absent */
        out->kind = X509_SIG_ECDSA;
        out->hash = OID_EQ(oid, OID_ECDSA_SHA256)   ? TLS_HASH_SHA256
                    : OID_EQ(oid, OID_ECDSA_SHA384) ? TLS_HASH_SHA384
                                                    : TLS_HASH_SHA512;
        return X509_OK;
    }
    if (OID_EQ(oid, OID_RSA_PSS)) {
        der_t params;
        if (!der_expect(&ai, 0x30, &params, 0) || ai.len) return X509_ERR_UNSUPPORTED;
        tls_hash_t h = parse_pss_params(params);
        if (h == TLS_HASH_NONE) return X509_ERR_UNSUPPORTED;
        out->kind = X509_SIG_RSA_PSS;
        out->hash = h;
        return X509_OK;
    }
    return X509_ERR_UNSUPPORTED; /* SHA-1, MD5, Ed25519, DSA, ... */
}

/* A positive INTEGER's magnitude, minimal DER. */
static bool parse_uint(der_t *cur, const uint8_t **p, uint32_t *len)
{
    der_t v;
    if (!der_expect(cur, 0x02, &v, 0) || v.len == 0) return false;
    if (v.p[0] & 0x80) return false;
    if (v.len > 1 && v.p[0] == 0 && !(v.p[1] & 0x80)) return false;
    if (v.p[0] == 0 && v.len > 1) {
        v.p++;
        v.len--;
    }
    *p = v.p;
    *len = v.len;
    return true;
}

/* BIT STRING with no unused bits */
static bool bit_string(der_t *cur, der_t *out)
{
    der_t v;
    if (!der_expect(cur, 0x03, &v, 0) || v.len < 1 || v.p[0] != 0) return false;
    out->p = v.p + 1;
    out->len = v.len - 1;
    return true;
}

static x509_err_t parse_spki(der_t spki, x509_pubkey_t *k)
{
    der_t ai, oid, key, param;
    k->type = X509_KEY_NONE;
    if (!der_expect(&spki, 0x30, &ai, 0)) return X509_ERR_PARSE;
    if (!bit_string(&spki, &key) || spki.len) return X509_ERR_PARSE;
    if (!der_expect(&ai, 0x06, &oid, 0)) return X509_ERR_PARSE;
    if (OID_EQ(oid, OID_RSA)) {
        if (!der_expect(&ai, 0x05, &param, 0) || param.len || ai.len) return X509_ERR_PARSE;
        der_t seq;
        if (!der_expect(&key, 0x30, &seq, 0) || key.len) return X509_ERR_PARSE;
        if (!parse_uint(&seq, &k->n, &k->n_len) || !parse_uint(&seq, &k->e, &k->e_len) || seq.len)
            return X509_ERR_PARSE;
        k->type = X509_KEY_RSA;
        return X509_OK;
    }
    if (OID_EQ(oid, OID_EC_PUB)) {
        if (!der_expect(&ai, 0x06, &param, 0) || ai.len) return X509_ERR_UNSUPPORTED;
        if (OID_EQ(param, OID_P256))
            k->curve = ECDSA_P256;
        else if (OID_EQ(param, OID_P384))
            k->curve = ECDSA_P384;
        else
            return X509_ERR_UNSUPPORTED;
        k->point = key.p;
        k->point_len = key.len;
        k->type = X509_KEY_EC;
        return X509_OK;
    }
    /* any other key type parses, but cannot verify anything */
    return X509_OK;
}

/* ---- extensions --------------------------------------------------------- */

static x509_err_t parse_ext(x509_cert_t *c, uint8_t id, der_t v)
{
    der_t inner, x;
    switch (id) {
    case CE_BC:
        if (!der_expect(&v, 0x30, &inner, 0) || v.len) return X509_ERR_PARSE;
        c->has_bc = true;
        if (der_peek(&inner, 0x01)) {
            if (!der_expect(&inner, 0x01, &x, 0) || x.len != 1) return X509_ERR_PARSE;
            if (x.p[0] != 0xff) return X509_ERR_PARSE; /* DER: TRUE is ff, FALSE omitted */
            c->is_ca = true;
        }
        if (inner.len) {
            const uint8_t *p;
            uint32_t l;
            if (!parse_uint(&inner, &p, &l) || inner.len || l > 1) return X509_ERR_PARSE;
            c->path_len = l ? p[0] : 0;
        }
        return X509_OK;
    case CE_KU: {
        if (!der_expect(&v, 0x03, &x, 0) || v.len || x.len < 2 || x.len > 3 || x.p[0] > 7)
            return X509_ERR_PARSE;
        /* bit 0 is the most significant bit of the first content byte */
        uint32_t bits = 0;
        for (uint32_t i = 1; i < x.len; i++)
            for (uint32_t b = 0; b < 8; b++)
                if (x.p[i] & (0x80u >> b)) bits |= 1u << (8 * (i - 1) + b);
        c->has_ku = true;
        c->ku = bits;
        return X509_OK;
    }
    case CE_EKU:
        if (!der_expect(&v, 0x30, &inner, 0) || v.len || inner.len == 0) return X509_ERR_PARSE;
        c->has_eku = true;
        while (inner.len) {
            if (!der_expect(&inner, 0x06, &x, 0)) return X509_ERR_PARSE;
            if (OID_EQ(x, OID_EKU_SERVER)) c->eku_server_auth = true;
            if (OID_EQ(x, OID_EKU_ANY)) c->eku_any = true;
        }
        return X509_OK;
    case CE_SAN: {
        if (!der_expect(&v, 0x30, &inner, 0) || v.len || inner.len == 0) return X509_ERR_PARSE;
        der_t walk = inner;
        uint8_t t;
        while (walk.len)
            if (!der_next(&walk, &t, &x, 0)) return X509_ERR_PARSE;
        c->san.p = inner.p;
        c->san.len = inner.len;
        return X509_OK;
    }
    case CE_NC: {
        if (!der_expect(&v, 0x30, &inner, 0) || v.len) return X509_ERR_PARSE;
        c->has_nc = true;
        if (der_peek(&inner, 0xa0)) {
            if (!der_expect(&inner, 0xa0, &x, 0)) return X509_ERR_PARSE;
            c->nc_permitted.p = x.p;
            c->nc_permitted.len = x.len;
        }
        if (der_peek(&inner, 0xa1)) {
            if (!der_expect(&inner, 0xa1, &x, 0)) return X509_ERR_PARSE;
            c->nc_excluded.p = x.p;
            c->nc_excluded.len = x.len;
        }
        return inner.len ? X509_ERR_PARSE : X509_OK;
    }
    default:
        return X509_OK;
    }
}

static x509_err_t parse_extensions(x509_cert_t *c, der_t exts)
{
    uint32_t seen = 0; /* bit per id-ce number < 32 */
    der_t list;
    if (!der_expect(&exts, 0x30, &list, 0) || exts.len || list.len == 0) return X509_ERR_PARSE;
    while (list.len) {
        der_t ext, oid, crit, val;
        bool critical = false;
        if (!der_expect(&list, 0x30, &ext, 0)) return X509_ERR_PARSE;
        if (!der_expect(&ext, 0x06, &oid, 0)) return X509_ERR_PARSE;
        if (der_peek(&ext, 0x01)) {
            if (!der_expect(&ext, 0x01, &crit, 0) || crit.len != 1 || crit.p[0] != 0xff)
                return X509_ERR_PARSE; /* DER: an explicit FALSE is not allowed */
            critical = true;
        }
        if (!der_expect(&ext, 0x04, &val, 0) || ext.len) return X509_ERR_PARSE;

        bool known = false;
        if (oid.len == 3 && oid.p[0] == 0x55 && oid.p[1] == 0x1d) {
            uint8_t id = oid.p[2];
            if (id < 32) {
                if (seen & (1u << id)) return X509_ERR_PARSE; /* repeated */
                seen |= 1u << id;
            }
            if (id == CE_BC || id == CE_KU || id == CE_EKU || id == CE_SAN || id == CE_NC) {
                x509_err_t e = parse_ext(c, id, val);
                if (e != X509_OK) return e;
                known = true;
            }
            /* understood well enough to ignore safely when non-critical, and
             * never critical in practice */
            if (id == CE_SKI || id == CE_AKI || id == CE_CRL) known = known || !critical;
        }
        if (critical && !known) return X509_ERR_CRITICAL;
    }
    return X509_OK;
}

/* ---- certificate -------------------------------------------------------- */

x509_err_t x509_parse(x509_cert_t *c, const uint8_t *der, uint32_t len)
{
    if (!c || !der) return X509_ERR_PARSE;
    for (uint32_t i = 0; i < sizeof *c; i++) ((uint8_t *) c)[i] = 0;
    c->path_len = -1;

    der_t all = {der, len}, cert, tbs_whole, tbs, ai_outer, ai_inner, sigv, whole;
    if (!der_expect(&all, 0x30, &cert, &whole) || all.len) return X509_ERR_PARSE;
    c->raw.p = whole.p;
    c->raw.len = whole.len;
    if (!der_expect(&cert, 0x30, &tbs, &tbs_whole)) return X509_ERR_PARSE;
    c->tbs.p = tbs_whole.p;
    c->tbs.len = tbs_whole.len;
    if (!der_expect(&cert, 0x30, &ai_outer, 0)) return X509_ERR_PARSE;
    if (!bit_string(&cert, &sigv) || cert.len) return X509_ERR_PARSE;
    c->sig.p = sigv.p;
    c->sig.len = sigv.len;

    /* version */
    c->version = 1;
    if (der_peek(&tbs, 0xa0)) {
        der_t ver, vi;
        if (!der_expect(&tbs, 0xa0, &ver, 0) || !der_expect(&ver, 0x02, &vi, 0) || ver.len)
            return X509_ERR_PARSE;
        if (vi.len != 1 || vi.p[0] > 2) return X509_ERR_PARSE;
        if (vi.p[0] == 0) return X509_ERR_PARSE; /* DER: the default is omitted */
        c->version = vi.p[0] + 1u;
        if (c->version == 2) return X509_ERR_UNSUPPORTED;
    }
    der_t serial;
    if (!der_expect(&tbs, 0x02, &serial, 0) || serial.len == 0 || serial.len > 21)
        return X509_ERR_PARSE;

    if (!der_expect(&tbs, 0x30, &ai_inner, 0)) return X509_ERR_PARSE;
    /* the two algorithm identifiers must be the same bytes */
    if (!eq(ai_inner.p, ai_inner.len, ai_outer.p, ai_outer.len)) return X509_ERR_PARSE;

    der_t issuer, subject, validity, spki, w;
    if (!der_expect(&tbs, 0x30, &issuer, &w)) return X509_ERR_PARSE;
    c->issuer.p = w.p;
    c->issuer.len = w.len;
    if (!der_expect(&tbs, 0x30, &validity, 0)) return X509_ERR_PARSE;
    if (!parse_time(&validity, &c->not_before) || !parse_time(&validity, &c->not_after) ||
        validity.len)
        return X509_ERR_PARSE;
    if (!der_expect(&tbs, 0x30, &subject, &w)) return X509_ERR_PARSE;
    c->subject.p = w.p;
    c->subject.len = w.len;
    if (!der_expect(&tbs, 0x30, &spki, 0)) return X509_ERR_PARSE;

    der_t uid;
    if (der_peek(&tbs, 0x81) && !der_expect(&tbs, 0x81, &uid, 0)) return X509_ERR_PARSE;
    if (der_peek(&tbs, 0x82) && !der_expect(&tbs, 0x82, &uid, 0)) return X509_ERR_PARSE;
    if (der_peek(&tbs, 0xa3)) {
        der_t exts;
        if (c->version != 3) return X509_ERR_PARSE;
        if (!der_expect(&tbs, 0xa3, &exts, 0)) return X509_ERR_PARSE;
        x509_err_t e = parse_extensions(c, exts);
        if (e != X509_OK) return e;
    }
    if (tbs.len) return X509_ERR_PARSE;

    x509_err_t e = parse_sigalg(ai_outer, &c->sigalg);
    if (e == X509_ERR_PARSE) return e;
    /* An unsupported signature algorithm is not a parse failure: a trust
     * anchor's self-signature is never checked, and a certificate whose
     * signature must be checked fails there instead. */
    e = parse_spki(spki, &c->key);
    if (e != X509_OK) return e;
    return X509_OK;
}

/* ---- signatures --------------------------------------------------------- */

bool x509_verify_sig(const x509_pubkey_t *key, x509_sigalg_t alg, const uint8_t *msg,
                     uint32_t msg_len, const uint8_t *sig, uint32_t sig_len)
{
    uint8_t dg[TLS_DIGEST_MAX];
    if (!key || !msg || !sig) return false;
    uint32_t dl = tls_digest(alg.hash, msg, msg_len, dg);
    if (!dl) return false;
    switch (alg.kind) {
    case X509_SIG_RSA_PKCS1:
        if (key->type != X509_KEY_RSA) return false;
        return rsa_verify_pkcs1(key->n, key->n_len, key->e, key->e_len, alg.hash, dg, sig, sig_len);
    case X509_SIG_RSA_PSS:
        if (key->type != X509_KEY_RSA) return false;
        return rsa_verify_pss(key->n, key->n_len, key->e, key->e_len, alg.hash, dg, sig, sig_len);
    case X509_SIG_ECDSA:
        if (key->type != X509_KEY_EC) return false;
        return ecdsa_verify_digest(key->curve, key->point, key->point_len, dg, dl, sig, sig_len);
    default:
        return false;
    }
}

bool x509_signed_by(const x509_cert_t *child, const x509_cert_t *issuer)
{
    if (!child || !issuer) return false;
    return x509_verify_sig(&issuer->key, child->sigalg, child->tbs.p, child->tbs.len, child->sig.p,
                           child->sig.len);
}

/* ---- names -------------------------------------------------------------- */

static uint8_t lower(uint8_t ch)
{
    return (ch >= 'A' && ch <= 'Z') ? (uint8_t) (ch + 32) : ch;
}

static bool ieq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

/* A hostname a certificate may name: LDH labels, no empty label. */
static bool valid_dns(const uint8_t *s, uint32_t n)
{
    if (n == 0 || n > 253) return false;
    uint32_t label = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t ch = lower(s[i]);
        if (ch == '.') {
            if (label == 0) return false;
            label = 0;
            continue;
        }
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
            return false;
        if (++label > 63) return false;
    }
    return label != 0;
}

static bool parse_ipv4(const uint8_t *s, uint32_t n, uint8_t out[4])
{
    uint32_t part = 0, val = 0, nd = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '.') {
            if (nd == 0 || val > 255 || part > 3) return false;
            out[part++] = (uint8_t) val;
            val = 0;
            nd = 0;
            continue;
        }
        if (s[i] < '0' || s[i] > '9' || nd == 3) return false;
        if (nd == 1 && val == 0) return false; /* no leading zeros */
        val = val * 10 + (s[i] - '0');
        nd++;
    }
    return part == 4;
}

static bool dns_matches(const uint8_t *pat, uint32_t pl, const uint8_t *host, uint32_t hl)
{
    if (pl >= 2 && pat[0] == '*' && pat[1] == '.') {
        /* "*.a.b": the rest must itself hold at least two labels */
        const uint8_t *rest = pat + 2;
        uint32_t rl = pl - 2, dots = 0;
        if (!valid_dns(rest, rl)) return false;
        for (uint32_t i = 0; i < rl; i++) dots += rest[i] == '.';
        if (dots < 1) return false;
        uint32_t first = 0;
        while (first < hl && host[first] != '.') first++;
        if (first == 0 || first == hl) return false;
        return hl - first - 1 == rl && ieq(host + first + 1, rest, rl);
    }
    if (!valid_dns(pat, pl)) return false;
    return pl == hl && ieq(pat, host, hl);
}

bool x509_matches_host(const x509_cert_t *c, const char *host, uint32_t host_len)
{
    if (!c || !host || host_len == 0 || c->san.len == 0) return false;
    const uint8_t *h = (const uint8_t *) host;
    if (h[host_len - 1] == '.') host_len--; /* absolute form */
    uint8_t ip[4];
    bool is_ip = parse_ipv4(h, host_len, ip);
    if (!is_ip && !valid_dns(h, host_len)) return false;

    der_t walk = {c->san.p, c->san.len}, v;
    uint8_t tag;
    while (walk.len && der_next(&walk, &tag, &v, 0)) {
        if (is_ip && tag == 0x87 && v.len == 4 && eq(v.p, 4, ip, 4)) return true;
        if (!is_ip && tag == 0x82 && dns_matches(v.p, v.len, h, host_len)) return true;
    }
    return false;
}

/* RFC 5280 4.2.1.10: "example.com" covers itself and every subdomain;
 * ".example.com" covers subdomains only. */
static bool dns_in_subtree(const uint8_t *name, uint32_t nl, const uint8_t *base, uint32_t bl)
{
    if (bl == 0) return true;
    if (base[0] == '.') return nl > bl && ieq(name + nl - bl, base, bl);
    if (nl == bl) return ieq(name, base, bl);
    return nl > bl && name[nl - bl - 1] == '.' && ieq(name + nl - bl, base, bl);
}

static bool ip_in_subtree(const uint8_t *ip, uint32_t il, const uint8_t *base, uint32_t bl)
{
    if (bl != 2 * il) return false;
    for (uint32_t i = 0; i < il; i++)
        if ((ip[i] & base[il + i]) != (base[i] & base[il + i])) return false;
    return true;
}

/* Walk GeneralSubtrees. Returns -1 on a form we do not handle (fail closed),
 * otherwise sets *has_type when a subtree of the name's type exists and
 * *hit when one contains it. */
static int subtrees(x509_span_t st, uint8_t tag, const uint8_t *name, uint32_t nl, bool *has_type,
                    bool *hit)
{
    der_t walk = {st.p, st.len}, sub, base;
    uint8_t t;
    *has_type = false;
    *hit = false;
    while (walk.len) {
        if (!der_expect(&walk, 0x30, &sub, 0)) return -1;
        if (!der_next(&sub, &t, &base, 0) || sub.len) return -1; /* minimum/maximum: unsupported */
        if (t != 0x82 && t != 0x87) return -1;
        if (t != tag) continue;
        *has_type = true;
        if (t == 0x82 && dns_in_subtree(name, nl, base.p, base.len)) *hit = true;
        if (t == 0x87 && ip_in_subtree(name, nl, base.p, base.len)) *hit = true;
    }
    return 0;
}

bool x509_name_constraints_ok(const x509_cert_t *ca, const x509_cert_t *leaf)
{
    if (!ca || !leaf) return false;
    if (!ca->has_nc) return true;
    bool has, hit;
    /* reject constraint forms we cannot evaluate even if the leaf has no
     * name of that type: a directoryName or email constraint that is
     * silently ignored would be a constraint not enforced */
    if (subtrees(ca->nc_permitted, 0, 0, 0, &has, &hit) < 0) return false;
    if (subtrees(ca->nc_excluded, 0, 0, 0, &has, &hit) < 0) return false;

    der_t walk = {leaf->san.p, leaf->san.len}, v;
    uint8_t tag;
    while (walk.len) {
        if (!der_next(&walk, &tag, &v, 0)) return false;
        if (tag != 0x82 && tag != 0x87) continue;
        if (subtrees(ca->nc_excluded, tag, v.p, v.len, &has, &hit) < 0 || hit) return false;
        if (subtrees(ca->nc_permitted, tag, v.p, v.len, &has, &hit) < 0) return false;
        if (has && !hit) return false;
    }
    return true;
}
