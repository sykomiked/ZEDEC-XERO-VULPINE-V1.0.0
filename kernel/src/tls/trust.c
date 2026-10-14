/* trust.c — trust store and chain validation. See trust.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "trust.h"
#include "rsa.h"
#include "trust_roots.h"

const char *tls_cert_status_str(tls_cert_status_t s)
{
    switch (s) {
    case TLS_CERT_OK:
        return "certificate chain verified";
    case TLS_CERT_NO_CERT:
        return "server sent no certificate";
    case TLS_CERT_NO_TRUST:
        return "no trusted roots loaded";
    case TLS_CERT_NO_TIME:
        return "current time unknown, cannot check expiry";
    case TLS_CERT_PARSE:
        return "malformed certificate";
    case TLS_CERT_UNSUPPORTED:
        return "unsupported algorithm or critical extension";
    case TLS_CERT_EXPIRED:
        return "certificate expired";
    case TLS_CERT_NOT_YET_VALID:
        return "certificate not yet valid";
    case TLS_CERT_NAME_MISMATCH:
        return "certificate does not match the host name";
    case TLS_CERT_UNKNOWN_ISSUER:
        return "no path to a trusted root";
    case TLS_CERT_BAD_SIGNATURE:
        return "certificate signature does not verify";
    case TLS_CERT_NOT_CA:
        return "issuer is not a certificate authority";
    case TLS_CERT_PATH_LEN:
        return "path length constraint exceeded";
    case TLS_CERT_KEY_USAGE:
        return "key usage forbids this use";
    case TLS_CERT_EKU:
        return "certificate not valid for server authentication";
    case TLS_CERT_NAME_CONSTRAINT:
        return "name constraints exclude the host";
    case TLS_CERT_WEAK_KEY:
        return "key too weak or unsupported";
    case TLS_CERT_CHAIN_TOO_LONG:
        return "certificate chain too long";
    }
    return "unknown";
}

static bool bytes_eq(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl)
{
    if (al != bl) return false;
    for (uint32_t i = 0; i < al; i++)
        if (a[i] != b[i]) return false;
    return true;
}

/* a byte copy: a struct assignment becomes a libcall (__aeabi_memcpy8 on
 * 32-bit ARM) that not every kernel image links */
static void copy_cert(x509_cert_t *dst, const x509_cert_t *src)
{
    if (!dst) return;
    const uint8_t *s = (const uint8_t *) src;
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < sizeof *dst; i++) d[i] = s[i];
}

static tls_cert_status_t parse_status(x509_err_t e)
{
    return e == X509_ERR_PARSE ? TLS_CERT_PARSE : TLS_CERT_UNSUPPORTED;
}

static bool key_ok(const x509_pubkey_t *k)
{
    if (k->type == X509_KEY_RSA) return rsa_key_acceptable(k->n, k->n_len, k->e, k->e_len);
    if (k->type == X509_KEY_EC) return ecdsa_pubkey_valid(k->curve, k->point, k->point_len);
    return false;
}

void tls_trust_init(tls_trust_t *t)
{
    if (t) t->n = 0;
}

bool tls_trust_add_der(tls_trust_t *t, const uint8_t *der, uint32_t len)
{
    x509_cert_t c;
    if (!t || !der || t->n >= TLS_TRUST_MAX) return false;
    if (x509_parse(&c, der, len) != X509_OK) return false;
    if (!key_ok(&c.key)) return false;
    for (uint32_t i = 0; i < t->n; i++)
        if (bytes_eq(t->a[i].der, t->a[i].len, der, c.raw.len)) return true;
    t->a[t->n].der = der;
    t->a[t->n].len = c.raw.len;
    t->a[t->n].subject = c.subject.p;
    t->a[t->n].subject_len = c.subject.len;
    t->n++;
    return true;
}

uint32_t tls_trust_add_builtin(tls_trust_t *t)
{
    uint32_t added = 0;
    for (uint32_t i = 0; i < TLS_BUILTIN_ROOT_COUNT; i++)
        if (tls_trust_add_der(t, tls_builtin_roots[i].der, tls_builtin_roots[i].len)) added++;
    return added;
}

uint32_t tls_trust_builtin_count(void)
{
    return TLS_BUILTIN_ROOT_COUNT;
}

static tls_cert_status_t check_time(const x509_cert_t *c, int64_t now)
{
    if (now < c->not_before) return TLS_CERT_NOT_YET_VALID;
    if (now > c->not_after) return TLS_CERT_EXPIRED;
    return TLS_CERT_OK;
}

/* Is `ca` allowed to have issued a certificate with `below` intermediates
 * (non-leaf CAs) beneath it? */
static tls_cert_status_t check_issuer(const x509_cert_t *ca, uint32_t below, int64_t now)
{
    tls_cert_status_t s = check_time(ca, now);
    if (s != TLS_CERT_OK) return s;
    if (!ca->has_bc || !ca->is_ca) return TLS_CERT_NOT_CA;
    if (ca->path_len >= 0 && below > (uint32_t) ca->path_len) return TLS_CERT_PATH_LEN;
    if (ca->has_ku && !(ca->ku & X509_KU_KEY_CERT_SIGN)) return TLS_CERT_KEY_USAGE;
    if (ca->has_eku && !ca->eku_server_auth && !ca->eku_any) return TLS_CERT_EKU;
    if (!key_ok(&ca->key)) return TLS_CERT_WEAK_KEY;
    return TLS_CERT_OK;
}

/* the most informative of two failures: a specific reason beats "no path" */
static tls_cert_status_t worse(tls_cert_status_t have, tls_cert_status_t got)
{
    return have == TLS_CERT_UNKNOWN_ISSUER ? got : have;
}

tls_cert_status_t tls_verify_chain(const tls_trust_t *t, const uint8_t *const certs[],
                                   const uint32_t lens[], uint32_t count, const char *host,
                                   uint32_t host_len, int64_t now, x509_cert_t *leaf_out)
{
    x509_cert_t path[TLS_CHAIN_MAX]; /* path[0] = leaf */
    bool used[TLS_CHAIN_MAX * 2];
    if (!certs || !lens || count == 0) return TLS_CERT_NO_CERT;
    if (!t || t->n == 0) return TLS_CERT_NO_TRUST;
    if (now <= 0) return TLS_CERT_NO_TIME;
    if (count > TLS_CHAIN_MAX * 2) return TLS_CERT_CHAIN_TOO_LONG;
    for (uint32_t i = 0; i < count; i++) used[i] = false;

    /* the leaf */
    x509_err_t pe = x509_parse(&path[0], certs[0], lens[0]);
    if (pe != X509_OK) return parse_status(pe);
    if (path[0].raw.len != lens[0]) return TLS_CERT_PARSE;
    const x509_cert_t *leaf = &path[0];
    tls_cert_status_t s = check_time(leaf, now);
    if (s != TLS_CERT_OK) return s;
    if (!x509_matches_host(leaf, host, host_len)) return TLS_CERT_NAME_MISMATCH;
    if (leaf->has_ku && !(leaf->ku & X509_KU_DIGITAL_SIGNATURE)) return TLS_CERT_KEY_USAGE;
    if (leaf->has_eku && !leaf->eku_server_auth) return TLS_CERT_EKU;
    if (!key_ok(&leaf->key)) return TLS_CERT_WEAK_KEY;
    used[0] = true;

    /* a leaf the owner placed in the store directly is trusted as is */
    for (uint32_t a = 0; a < t->n; a++)
        if (bytes_eq(t->a[a].der, t->a[a].len, leaf->raw.p, leaf->raw.len)) {
            copy_cert(leaf_out, leaf);
            return TLS_CERT_OK;
        }

    uint32_t depth = 1; /* certificates in path[] */
    tls_cert_status_t why = TLS_CERT_UNKNOWN_ISSUER;
    for (;;) {
        const x509_cert_t *cur = &path[depth - 1];
        uint32_t below = depth - 1; /* intermediates already under the next issuer */
        if (depth >= TLS_CHAIN_MAX) return TLS_CERT_CHAIN_TOO_LONG;
        x509_cert_t *cand = &path[depth];
        /* a signature algorithm we cannot check (SHA-1, Ed25519, ...) */
        if (cur->sigalg.kind == X509_SIG_NONE) return TLS_CERT_UNSUPPORTED;

        /* 1. a trust anchor issued it: the path is complete */
        for (uint32_t a = 0; a < t->n; a++) {
            if (!bytes_eq(t->a[a].subject, t->a[a].subject_len, cur->issuer.p, cur->issuer.len))
                continue;
            if (x509_parse(cand, t->a[a].der, t->a[a].len) != X509_OK) continue;
            tls_cert_status_t cs = check_issuer(cand, below, now);
            if (cs != TLS_CERT_OK) {
                why = worse(why, cs);
                continue;
            }
            if (!x509_signed_by(cur, cand)) {
                why = worse(why, TLS_CERT_BAD_SIGNATURE);
                continue;
            }
            for (uint32_t i = 1; i <= depth; i++)
                if (!x509_name_constraints_ok(&path[i], leaf)) return TLS_CERT_NAME_CONSTRAINT;
            copy_cert(leaf_out, leaf);
            return TLS_CERT_OK;
        }

        /* 2. one of the certificates the server sent issued it */
        bool advanced = false;
        for (uint32_t j = 1; j < count && !advanced; j++) {
            if (used[j]) continue;
            x509_err_t e = x509_parse(cand, certs[j], lens[j]);
            if (e != X509_OK) {
                why = worse(why, parse_status(e));
                continue;
            }
            if (cand->raw.len != lens[j]) {
                why = worse(why, TLS_CERT_PARSE);
                continue;
            }
            if (!bytes_eq(cand->subject.p, cand->subject.len, cur->issuer.p, cur->issuer.len))
                continue;
            tls_cert_status_t cs = check_issuer(cand, below, now);
            if (cs != TLS_CERT_OK) {
                why = worse(why, cs);
                continue;
            }
            if (!x509_signed_by(cur, cand)) {
                why = worse(why, TLS_CERT_BAD_SIGNATURE);
                continue;
            }
            used[j] = true;
            depth++;
            advanced = true;
        }
        if (!advanced) return why;
    }
}
