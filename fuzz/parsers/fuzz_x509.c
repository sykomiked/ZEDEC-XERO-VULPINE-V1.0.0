/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_x509.c — the X.509 parser and chain validator (tls/x509.c,
 * tls/trust.c).
 *
 * Input: byte 0 = number of certificates (1-4); then each certificate as a
 * 2-byte big-endian length and its bytes. The first certificate is also put
 * in a trust store, so both the anchor path and the intermediate path run.
 * Properties: no crash or sanitizer report; a parsed certificate's spans lie
 * inside the input; a certificate never validates for a host it does not
 * name. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "trust.h"
#include "fuzz_in.h"

static int inside(const uint8_t *b, uint32_t n, const uint8_t *p, uint32_t l)
{
    return l == 0 || (p >= b && p + l <= b + n);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3 || size > 65536) return 0;
    uint8_t *buf = fz_dup(data, size);
    uint32_t count = (buf[0] & 3) + 1, at = 1;
    const uint8_t *certs[4];
    uint32_t lens[4], n = 0;
    while (n < count && at + 2 <= size) {
        uint32_t l = ((uint32_t) buf[at] << 8) | buf[at + 1];
        at += 2;
        if (l > size - at) l = (uint32_t) (size - at);
        certs[n] = buf + at;
        lens[n++] = l;
        at += l;
    }
    if (n == 0) {
        free(buf);
        return 0;
    }

    x509_cert_t c;
    if (x509_parse(&c, certs[0], lens[0]) == X509_OK) {
        if (!inside(certs[0], lens[0], c.tbs.p, c.tbs.len) ||
            !inside(certs[0], lens[0], c.issuer.p, c.issuer.len) ||
            !inside(certs[0], lens[0], c.subject.p, c.subject.len) ||
            !inside(certs[0], lens[0], c.sig.p, c.sig.len) ||
            !inside(certs[0], lens[0], c.san.p, c.san.len))
            abort();
        (void) x509_signed_by(&c, &c);
        (void) x509_name_constraints_ok(&c, &c);
        (void) x509_matches_host(&c, "test.zxv.example", 16);
    }

    static tls_trust_t t;
    tls_trust_init(&t);
    tls_trust_add_der(&t, certs[0], lens[0]);
    const int64_t now = 1791806400; /* 2026-10-12 */
    (void) tls_verify_chain(&t, certs, lens, n, "test.zxv.example", 16, now, 0);
    if (n > 1) {
        x509_cert_t leaf;
        tls_cert_status_t s =
            tls_verify_chain(&t, certs + 1, lens + 1, n - 1, "zz.invalid", 10, now, &leaf);
        /* a chain that validates for a host always names that host */
        if (s == TLS_CERT_OK && !x509_matches_host(&leaf, "zz.invalid", 10)) abort();
    }
    free(buf);
    return 0;
}
