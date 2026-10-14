/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_trust.c — X.509 parsing and server chain validation.
 *
 * Real chains (test_trust_real.h): Let's Encrypt RSA (www.mozilla.org via
 * YR1 and Root YR to ISRG Root X1) and ECDSA (YE2 and Root YE to ISRG Root
 * X2), Google (WE2 to GlobalSign ECC R4) and GitHub (Sectigo E36 to Sectigo
 * E46), all against the compiled-in root store, plus a real precertificate
 * that must be refused for its critical CT poison extension.
 *
 * Known-bad chains (test_trust_chains.h, made by OpenSSL): every refusal
 * reason has at least one certificate that triggers it. */
#include <stdio.h>
#include <string.h>
#include "trust.h"
#include "test_trust_real.h"
#include "test_trust_chains.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static tls_trust_t g_web, g_test;

/* 2026-10-12T12:00:00Z, inside every real certificate's validity */
#define NOW_REAL 1791806400LL

static tls_cert_status_t chain(const tls_trust_t *t, const char *host, int64_t now, int n, ...)
{
    const uint8_t *c[8];
    uint32_t l[8];
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    for (int i = 0; i < n; i++) {
        c[i] = __builtin_va_arg(ap, const uint8_t *);
        l[i] = __builtin_va_arg(ap, uint32_t);
    }
    __builtin_va_end(ap);
    return tls_verify_chain(t, c, l, (uint32_t) n, host, (uint32_t) strlen(host), now, 0);
}
#define C(x) (x), (uint32_t) sizeof(x)

#define EXPECT(st, want, what)                                                                     \
    do {                                                                                           \
        tls_cert_status_t got_ = (st);                                                             \
        CHECK(got_ == (want), "%s: %s (want %s)", what, tls_cert_status_str(got_),                 \
              tls_cert_status_str(want));                                                          \
    } while (0)

static void test_time(void)
{
    CHECK(x509_unix_time(1970, 1, 1, 0, 0, 0) == 0, "epoch");
    CHECK(x509_unix_time(2000, 3, 1, 0, 0, 0) == 951868800, "2000-03-01 (leap century)");
    CHECK(x509_unix_time(2026, 10, 12, 12, 0, 0) == NOW_REAL, "2026-10-12T12:00Z");
    CHECK(x509_unix_time(2038, 1, 19, 3, 14, 8) == 2147483648LL, "past 2^31 seconds");
    CHECK(x509_unix_time(2025, 2, 29, 0, 0, 0) < 0, "2025-02-29 rejected");
    CHECK(x509_unix_time(2024, 2, 29, 0, 0, 0) == 1709164800, "2024-02-29 accepted");
}

static void test_real(void)
{
    uint32_t n = tls_trust_add_builtin(&g_web);
    CHECK(n == tls_trust_builtin_count(), "all %u built-in roots load (%u)",
          tls_trust_builtin_count(), n);

    EXPECT(chain(&g_web, "www.mozilla.org", NOW_REAL, 3, C(R_MOZ_LEAF), C(R_YR1), C(R_ROOT_YR)),
           TLS_CERT_OK, "www.mozilla.org: RSA leaf, YR1, Root YR, ISRG Root X1");
    EXPECT(chain(&g_web, "www.mozilla.org", NOW_REAL, 3, C(R_MOZ_LEAF), C(R_ROOT_YR), C(R_YR1)),
           TLS_CERT_OK, "same chain sent out of order");
    EXPECT(chain(&g_web, "valid-isrgrootx2.letsencrypt.org", NOW_REAL, 3, C(R_X2_LEAF), C(R_YE2),
                 C(R_ROOT_YE)),
           TLS_CERT_OK, "Let's Encrypt ECDSA: P-256 leaf, YE2, Root YE, ISRG Root X2");
    EXPECT(chain(&g_web, "www.google.com", NOW_REAL, 2, C(R_GOOGLE_LEAF), C(R_WE2)), TLS_CERT_OK,
           "www.google.com: WE2 to GlobalSign ECC R4");
    EXPECT(chain(&g_web, "github.com", NOW_REAL, 2, C(R_GITHUB_LEAF), C(R_E36)), TLS_CERT_OK,
           "github.com: Sectigo E36 to Sectigo E46 (P-384 root)");

    /* the same real chains, made wrong */
    EXPECT(chain(&g_web, "www.mozilla.org.evil.example", NOW_REAL, 3, C(R_MOZ_LEAF), C(R_YR1),
                 C(R_ROOT_YR)),
           TLS_CERT_NAME_MISMATCH, "real chain, wrong host");
    EXPECT(chain(&g_web, "github.com", NOW_REAL, 1, C(R_GITHUB_LEAF)), TLS_CERT_UNKNOWN_ISSUER,
           "real leaf without its intermediate");
    EXPECT(chain(&g_web, "github.com", x509_unix_time(2027, 1, 1, 0, 0, 0), 2, C(R_GITHUB_LEAF),
                 C(R_E36)),
           TLS_CERT_EXPIRED, "real chain after the leaf expires");
    EXPECT(chain(&g_web, "github.com", x509_unix_time(2026, 8, 1, 0, 0, 0), 2, C(R_GITHUB_LEAF),
                 C(R_E36)),
           TLS_CERT_NOT_YET_VALID, "real chain before the leaf is valid");
    EXPECT(chain(&g_web, "github.com", 0, 2, C(R_GITHUB_LEAF), C(R_E36)), TLS_CERT_NO_TIME,
           "unknown time fails closed");
    EXPECT(chain(&g_web, "www.google.com", NOW_REAL, 2, C(R_GOOGLE_PRECERT), C(R_WR2)),
           TLS_CERT_UNSUPPORTED, "real precertificate (critical CT poison) refused");
    EXPECT(chain(&g_web, "github.com", NOW_REAL, 2, C(R_GITHUB_LEAF), C(R_WE2)),
           TLS_CERT_UNKNOWN_ISSUER, "real leaf with the wrong intermediate");

    static uint8_t bad[sizeof R_GITHUB_LEAF];
    memcpy(bad, R_GITHUB_LEAF, sizeof bad);
    bad[sizeof bad - 5] ^= 0x01; /* inside the signature */
    EXPECT(chain(&g_web, "github.com", NOW_REAL, 2, C(bad), C(R_E36)), TLS_CERT_BAD_SIGNATURE,
           "real leaf with one signature bit flipped");
    memcpy(bad, R_GITHUB_LEAF, sizeof bad);
    bad[20] ^= 0x01; /* the serial number, inside the signed bytes */
    EXPECT(chain(&g_web, "github.com", NOW_REAL, 2, C(bad), C(R_E36)), TLS_CERT_BAD_SIGNATURE,
           "real leaf with its serial number altered");

    tls_trust_t empty;
    tls_trust_init(&empty);
    EXPECT(chain(&empty, "github.com", NOW_REAL, 2, C(R_GITHUB_LEAF), C(R_E36)), TLS_CERT_NO_TRUST,
           "empty trust store trusts nothing");

    /* intermediates against roots, directly: RSA-4096/SHA-384 (GTS R1) and
     * the 2024 Let's Encrypt intermediates */
    x509_cert_t a, b;
    tls_trust_t one;
    tls_trust_init(&one);
    tls_trust_add_builtin(&one);
    int found = 0;
    for (uint32_t i = 0; i < one.n; i++) {
        x509_parse(&b, one.a[i].der, one.a[i].len);
        x509_parse(&a, R_WR2, sizeof R_WR2);
        if (b.subject.len == a.issuer.len && !memcmp(b.subject.p, a.issuer.p, a.issuer.len))
            found += x509_signed_by(&a, &b);
        x509_parse(&a, R_R11, sizeof R_R11);
        if (b.subject.len == a.issuer.len && !memcmp(b.subject.p, a.issuer.p, a.issuer.len))
            found += x509_signed_by(&a, &b);
        x509_parse(&a, R_E6, sizeof R_E6);
        if (b.subject.len == a.issuer.len && !memcmp(b.subject.p, a.issuer.p, a.issuer.len))
            found += x509_signed_by(&a, &b);
    }
    CHECK(found == 3, "WR2 by GTS R1, R11 by ISRG X1, E6 by ISRG X2 (%d of 3)", found);
}

static void test_synthetic(void)
{
    const int64_t now = 0; /* replaced below by the leaf's own validity */
    (void) now;
    x509_cert_t leaf;
    CHECK(x509_parse(&leaf, T_LEAF, sizeof T_LEAF) == X509_OK, "parse the OpenSSL leaf");
    CHECK(leaf.sigalg.kind == X509_SIG_RSA_PSS && leaf.sigalg.hash == TLS_HASH_SHA256,
          "leaf is signed RSA-PSS SHA-256");
    CHECK(leaf.key.type == X509_KEY_EC && leaf.key.curve == ECDSA_P384, "leaf key is P-384");
    const int64_t t0 = leaf.not_before + 86400;

    tls_trust_init(&g_test);
    CHECK(tls_trust_add_der(&g_test, T_ROOT, sizeof T_ROOT), "add the test root");
    CHECK(tls_trust_add_der(&g_test, T_ROOT, sizeof T_ROOT) && g_test.n == 1,
          "adding it twice keeps one copy");
    uint8_t junk[64];
    memset(junk, 0x30, sizeof junk);
    CHECK(!tls_trust_add_der(&g_test, junk, sizeof junk), "garbage is not a root");

    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_OK,
           "P-384 leaf, RSA-PSS by RSA-2048 intermediate, ECDSA by P-256 root");
    EXPECT(chain(&g_test, "TEST.Zxv.Example.", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_OK,
           "host match ignores case and a trailing dot");
    EXPECT(chain(&g_test, "a.wild.zxv.example", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_OK,
           "wildcard covers one label");
    EXPECT(chain(&g_test, "192.0.2.7", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_OK,
           "IPv4 literal against an iPAddress entry");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 3, C(T_LEAF), C(T_OTHERROOT), C(T_INT)),
           TLS_CERT_OK, "an unrelated extra certificate is ignored");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_RSA), C(T_INT)), TLS_CERT_OK,
           "RSA leaf, PKCS #1 v1.5 SHA-384");
    EXPECT(chain(&g_test, "wild.zxv.example", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_NAME_MISMATCH,
           "wildcard does not cover its own base");
    EXPECT(chain(&g_test, "a.b.wild.zxv.example", t0, 2, C(T_LEAF), C(T_INT)),
           TLS_CERT_NAME_MISMATCH, "wildcard does not cover two labels");
    EXPECT(chain(&g_test, "192.0.2.8", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_NAME_MISMATCH,
           "wrong IPv4 address");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_NOSAN), C(T_INT)),
           TLS_CERT_NAME_MISMATCH, "common name alone is not trusted");
    EXPECT(chain(&g_test, "a.example", t0, 2, C(T_LEAF_BADWILD), C(T_INT)), TLS_CERT_NAME_MISMATCH,
           "*.example refused");
    EXPECT(chain(&g_test, "ab.zxv.example", t0, 2, C(T_LEAF_BADWILD), C(T_INT)),
           TLS_CERT_NAME_MISMATCH, "partial-label wildcard a*.zxv.example refused");
    EXPECT(chain(&g_test, "test.zxv.example", leaf.not_before - 1, 2, C(T_LEAF), C(T_INT)),
           TLS_CERT_NOT_YET_VALID, "one second before notBefore");
    EXPECT(chain(&g_test, "test.zxv.example", leaf.not_after + 1, 2, C(T_LEAF), C(T_INT)),
           TLS_CERT_EXPIRED, "one second after notAfter");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 1, C(T_LEAF)), TLS_CERT_UNKNOWN_ISSUER,
           "intermediate missing");
    EXPECT(chain(&g_web, "test.zxv.example", t0, 2, C(T_LEAF), C(T_INT)), TLS_CERT_UNKNOWN_ISSUER,
           "test chain is not trusted by the web roots");

    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_UNDER_NOTCA), C(T_INT_NOTCA)),
           TLS_CERT_NOT_CA, "issuer with cA FALSE");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_UNDER_NOBC), C(T_INT_NOBC)),
           TLS_CERT_NOT_CA, "issuer without basicConstraints");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 3, C(T_LEAF_UNDER_INT2), C(T_INT2), C(T_INT)),
           TLS_CERT_PATH_LEN, "pathlen 0 intermediate above another CA");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_UNDER_NOKS), C(T_INT_NOKS)),
           TLS_CERT_KEY_USAGE, "issuer keyUsage without keyCertSign");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_UNDER_CLIENT), C(T_INT_CLIENT)),
           TLS_CERT_EKU, "issuer restricted to clientAuth");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_EKU_CLIENT), C(T_INT)), TLS_CERT_EKU,
           "leaf for clientAuth only");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_KU_BAD), C(T_INT)),
           TLS_CERT_KEY_USAGE, "leaf keyUsage without digitalSignature");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_CRIT), C(T_INT)),
           TLS_CERT_UNSUPPORTED, "leaf with an unknown critical extension");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_WEAK), C(T_INT)), TLS_CERT_WEAK_KEY,
           "RSA-1024 leaf");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_SHA1), C(T_INT)),
           TLS_CERT_UNSUPPORTED, "SHA-1 signature");

    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_NCON_OK), C(T_INT_NCON)), TLS_CERT_OK,
           "name-constrained CA, leaf inside zxv.example");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_NCON_EVIL), C(T_INT_NCON)),
           TLS_CERT_NAME_CONSTRAINT, "leaf also names evil.example outside the permitted tree");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_NCON_EXCLUDED), C(T_INT_NCON)),
           TLS_CERT_NAME_CONSTRAINT, "leaf names an excluded subtree");
    EXPECT(chain(&g_test, "test.zxv.example", t0, 2, C(T_LEAF_NCDIR), C(T_INT_NCDIR)),
           TLS_CERT_NAME_CONSTRAINT, "directoryName constraint fails closed");

    x509_cert_t ss;
    x509_parse(&ss, T_SELFSIGNED, sizeof T_SELFSIGNED);
    int64_t ts = ss.not_before + 60;
    EXPECT(chain(&g_test, "device.local", ts, 1, C(T_SELFSIGNED)), TLS_CERT_UNKNOWN_ISSUER,
           "self-signed device certificate, not pinned");
    tls_trust_t pin;
    tls_trust_init(&pin);
    tls_trust_add_der(&pin, T_SELFSIGNED, sizeof T_SELFSIGNED);
    EXPECT(chain(&pin, "device.local", ts, 1, C(T_SELFSIGNED)), TLS_CERT_OK,
           "self-signed device certificate, pinned by the owner");
    EXPECT(chain(&pin, "other.local", ts, 1, C(T_SELFSIGNED)), TLS_CERT_NAME_MISMATCH,
           "pinned certificate still checks the name");

    /* every truncation of a certificate fails to parse, none crashes */
    int bad = 0;
    for (uint32_t cut = 0; cut < sizeof T_LEAF; cut++)
        bad += x509_parse(&leaf, T_LEAF, cut) != X509_OK;
    CHECK(bad == (int) sizeof T_LEAF, "every truncation of the leaf is refused (%d)", bad);
}

int main(void)
{
    tls_trust_init(&g_web);
    test_time();
    test_real();
    test_synthetic();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_trust\n");
        return 1;
    }
    printf("[PASS] test_trust\n");
    return 0;
}
