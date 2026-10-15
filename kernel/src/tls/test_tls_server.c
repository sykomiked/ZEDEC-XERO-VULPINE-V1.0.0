/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_tls_server.c — the TLS 1.3 server (server.h V1-V5) and the
 * self-signed certificate (selfsigned.h C1-C4), against the repo's own
 * client (handshake.h) with TLS_VERIFY_REQUIRED: the client validates the
 * certificate through trust.c and checks CertificateVerify and Finished, so
 * a pass means the server's flight, signature and key schedule are right.
 * Hand-built ClientHellos drive every refusal. Interoperability with OpenSSL
 * is checked separately (kernel/arch/hosted/test_zxvd_irc.sh). */
#include <stdio.h>
#include <string.h>
#include "server.h"
#include "selfsigned.h"
#include "x509.h"

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define NOW 1770000000 /* 2026-02-02 */
static uint8_t CERT[TLS_SELFSIGNED_MAX], KEY[32];
static uint32_t CERT_LEN;
static tls_trust_t TRUST;
static tls_server_t S;
static tls_client_t C;
static uint8_t A[16384], B[16384];

static void fill(uint8_t *p, uint32_t n, uint8_t seed)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (seed + 37 * i + (i >> 3));
}

/* a fresh server and a connected client; returns whether both established */
static int connect_pair(const char *host)
{
    uint8_t r[32], x[32], r2[32], x2[32];
    fill(r, 32, 1);
    fill(x, 32, 2);
    fill(r2, 32, 3);
    fill(x2, 32, 4);
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    tls_client_init(&C, host, r2, x2, TLS_VERIFY_REQUIRED);
    tls_client_set_trust(&C, &TRUST, NOW);
    uint32_t an = tls_client_hello(&C, A, sizeof A), bn = 0, rl = 0;
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    tls_client_feed(&C, B, bn, A, sizeof A, &an);
    if (an) tls_server_feed(&S, A, an, B, sizeof B, &rl);
    return tls_server_established(&S) && tls_client_established(&C);
}

/* ---- hand-built ClientHellos ---- */
typedef struct {
    int no_versions, old_version_only, no_chacha, no_sigalg_p256, no_sigalgs, no_keyshare,
        other_group_only, short_share, dup_ext, compression, low_point, legacy10;
} ch_opt_t;

static uint32_t put_ext(uint8_t *o, uint32_t at, uint16_t t, const uint8_t *b, uint16_t l)
{
    o[at++] = (uint8_t) (t >> 8);
    o[at++] = (uint8_t) t;
    o[at++] = (uint8_t) (l >> 8);
    o[at++] = (uint8_t) l;
    memcpy(o + at, b, l);
    return at + l;
}

/* one plaintext record holding a ClientHello */
static uint32_t make_ch(const ch_opt_t *o, uint8_t *out)
{
    uint8_t m[1024];
    uint32_t at = 4;
    m[at++] = 3;
    m[at++] = o->legacy10 ? 1 : 3;
    fill(m + at, 32, 9);
    at += 32;
    m[at++] = 32;
    fill(m + at, 32, 10);
    at += 32;
    m[at++] = 0;
    m[at++] = 4;
    m[at++] = 0x13;
    m[at++] = 0x01; /* AES-128-GCM */
    m[at++] = 0x13;
    m[at++] = o->no_chacha ? 0x02 : 0x03;
    m[at++] = o->compression ? 2 : 1;
    m[at++] = 0;
    if (o->compression) m[at++] = 1;
    uint32_t el = at;
    at += 2;
    uint32_t es = at;
    if (!o->no_versions) {
        uint8_t v[5] = {4, 3, 4, 3, 3};
        if (o->old_version_only) v[2] = 3, v[3] = 3;
        at = put_ext(m, at, 43, v, 5);
    }
    if (!o->no_sigalgs) {
        uint8_t s[6] = {0, 4, 0x08, 0x04, 0x04, 0x03};
        if (o->no_sigalg_p256) s[4] = 0x05;
        at = put_ext(m, at, 13, s, 6);
    }
    if (o->dup_ext) at = put_ext(m, at, 13, (const uint8_t *) "\0\2\4\3", 4);
    if (!o->no_keyshare) {
        uint8_t k[2 + 4 + 32];
        k[0] = 0;
        k[1] = o->short_share ? 4 + 31 : 4 + 32;
        k[2] = 0;
        k[3] = o->other_group_only ? 0x17 : 0x1D;
        k[4] = 0;
        k[5] = o->short_share ? 31 : 32;
        if (o->low_point)
            memset(k + 6, 0, 32);
        else
            fill(k + 6, 32, 11);
        at = put_ext(m, at, 51, k, (uint16_t) (o->short_share ? 37 : 38));
    }
    m[el] = (uint8_t) ((at - es) >> 8);
    m[el + 1] = (uint8_t) (at - es);
    uint32_t bl = at - 4;
    m[0] = 1;
    m[1] = 0;
    m[2] = (uint8_t) (bl >> 8);
    m[3] = (uint8_t) bl;
    return tls_record_write(0, TLS_CT_HANDSHAKE, m, at, out, 4096);
}

/* feed one hand-built hello to a fresh server: the alert it sent */
static uint8_t refuse(const ch_opt_t *o, tls_error_t *err)
{
    uint8_t r[32], x[32];
    fill(r, 32, 1);
    fill(x, 32, 2);
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    uint32_t n = make_ch(o, A), bn = 0;
    tls_server_feed(&S, A, n, B, sizeof B, &bn);
    *err = S.error;
    /* the reply is a plaintext fatal alert record */
    if (!tls_server_failed(&S) || bn != 7 || B[0] != TLS_CT_ALERT || B[5] != 2) return 0xFF;
    return B[6];
}

static void test_cert(void)
{
    uint8_t ser[16], pem[2048];
    fill(KEY, 32, 77);
    fill(ser, 16, 5);
    CERT_LEN = tls_selfsigned_p256(KEY, ser, "zxvd irc gateway", NOW - 86400, NOW + 3650LL * 86400,
                                   CERT, sizeof CERT);
    CHECK(CERT_LEN > 300 && CERT_LEN <= TLS_SELFSIGNED_MAX, "C1 certificate built");
    x509_cert_t c;
    CHECK(x509_parse(&c, CERT, CERT_LEN) == X509_OK, "C1 parses");
    CHECK(c.key.type == X509_KEY_EC && c.key.curve == ECDSA_P256, "C1 P-256 key");
    CHECK(x509_signed_by(&c, &c), "C1 self-signed signature verifies");
    CHECK(c.has_bc && !c.is_ca, "C2 not a CA");
    CHECK(c.has_ku && (c.ku & X509_KU_DIGITAL_SIGNATURE), "C2 digitalSignature");
    CHECK(c.has_eku && c.eku_server_auth, "C2 serverAuth");
    CHECK(x509_matches_host(&c, "localhost", 9), "C2 localhost");
    CHECK(x509_matches_host(&c, "127.0.0.1", 9), "C2 127.0.0.1");
    CHECK(!x509_matches_host(&c, "example.com", 11), "C2 no other host");
    CHECK(!x509_matches_host(&c, "192.168.1.2", 11), "C2 no network address");
    tls_trust_init(&TRUST);
    CHECK(tls_trust_add_der(&TRUST, CERT, CERT_LEN), "C4 pinned in a store");
    const uint8_t *cs[1] = {CERT};
    uint32_t ls[1] = {CERT_LEN};
    CHECK(tls_verify_chain(&TRUST, cs, ls, 1, "localhost", 9, NOW, 0) == TLS_CERT_OK,
          "C4 pinned certificate validates");
    CHECK(tls_verify_chain(&TRUST, cs, ls, 1, "localhost", 9, NOW + 3651LL * 86400, 0) ==
              TLS_CERT_EXPIRED,
          "C3 expires");
    CHECK(tls_verify_chain(&TRUST, cs, ls, 1, "localhost", 9, NOW - 2 * 86400, 0) ==
              TLS_CERT_NOT_YET_VALID,
          "C3 not before");
    /* C3: GeneralizedTime from 2050, and a leap day */
    static uint8_t c2[TLS_SELFSIGNED_MAX];
    uint32_t n2 = tls_selfsigned_p256(KEY, ser, "x", 951782400 /* 2000-02-29 */,
                                      2524608000LL /* 2050-01-01 */, c2, sizeof c2);
    x509_cert_t cc;
    CHECK(n2 && x509_parse(&cc, c2, n2) == X509_OK, "C3 2050 parses");
    CHECK(n2 && cc.not_before == 951782400 && cc.not_after == 2524608000LL,
          "C3 times round trip (leap day, GeneralizedTime)");
    CHECK(memmem(c2, n2, "000229000000Z", 13) != NULL, "C3 UTCTime for 2000");
    CHECK(memmem(c2, n2, "20500101000000Z", 15) != NULL, "C3 GeneralizedTime for 2050");
    /* refusals */
    uint8_t zero[32] = {0};
    char big[80];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    CHECK(!tls_selfsigned_p256(zero, ser, "x", 1, 2, c2, sizeof c2), "C1 d = 0 refused");
    CHECK(!tls_selfsigned_p256(KEY, ser, "", 1, 2, c2, sizeof c2), "C1 empty CN refused");
    CHECK(!tls_selfsigned_p256(KEY, ser, big, 1, 2, c2, sizeof c2), "C1 long CN refused");
    CHECK(!tls_selfsigned_p256(KEY, ser, "x", 2, 2, c2, sizeof c2), "C3 empty validity refused");
    CHECK(!tls_selfsigned_p256(KEY, ser, "x", -5, 2, c2, sizeof c2), "C3 before 1970 refused");
    CHECK(!tls_selfsigned_p256(KEY, ser, "x", 1, 2, c2, 200), "C1 short buffer refused");
    /* PEM */
    uint32_t pl = tls_cert_pem(CERT, CERT_LEN, (char *) pem, sizeof pem);
    CHECK(pl > 0 && !strncmp((char *) pem, "-----BEGIN CERTIFICATE-----\n", 28), "PEM head");
    CHECK(pl && !strcmp((char *) pem + pl - 26, "-----END CERTIFICATE-----\n"), "PEM tail");
    CHECK(tls_cert_pem(CERT, CERT_LEN, (char *) pem, 100) == 0, "PEM short buffer refused");
}

static void test_handshake(void)
{
    CHECK(connect_pair("localhost"), "V2 handshake with the pinned certificate");
    CHECK(tls_client_is_authenticated(&C), "V2 client authenticated the server");
    /* V3: application data both ways */
    uint32_t an = tls_client_send(&C, (const uint8_t *) "NICK fox\r\n", 10, A, sizeof A), bn = 0;
    CHECK(tls_server_feed(&S, A, an, B, sizeof B, &bn) == an && bn == 0, "client data accepted");
    char got[64] = {0};
    CHECK(tls_server_read(&S, (uint8_t *) got, sizeof got) == 10 && !strcmp(got, "NICK fox\r\n"),
          "server reads client data");
    bn = tls_server_send(&S, (const uint8_t *) "PING :z\r\n", 9, B, sizeof B);
    tls_client_feed(&C, B, bn, A, sizeof A, &an);
    memset(got, 0, sizeof got);
    CHECK(tls_client_read(&C, (uint8_t *) got, sizeof got) == 9 && !strcmp(got, "PING :z\r\n"),
          "client reads server data");
    /* a record split across feeds */
    an = tls_client_send(&C, (const uint8_t *) "abc", 3, A, sizeof A);
    CHECK(tls_server_feed(&S, A, 4, B, sizeof B, &bn) == 0, "partial record waits");
    CHECK(tls_server_feed(&S, A, an, B, sizeof B, &bn) == an, "whole record consumed");
    CHECK(tls_server_read(&S, (uint8_t *) got, 3) == 3 && !memcmp(got, "abc", 3), "split read");
    /* a tampered record: bad_record_mac */
    an = tls_client_send(&C, (const uint8_t *) "x", 1, A, sizeof A);
    A[an - 1] ^= 1;
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_sent == 20 && S.error == TLS_ERR_BAD_RECORD,
          "V4 tampered record refused with bad_record_mac");
    CHECK(tls_server_feed(&S, A, an, B, sizeof B, &bn) == 0, "V4 a failed server takes nothing");
    CHECK(tls_server_send(&S, (const uint8_t *) "x", 1, B, sizeof B) == 0, "V4 nor sends");

    /* the client's wrong host name: the client refuses, the server learns by alert */
    CHECK(!connect_pair("example.com"), "a client asking for another host refuses");
    CHECK(C.cert_status == TLS_CERT_NAME_MISMATCH, "  because of the name");
}

/* client Finished tampered: decrypt_error */
static void test_bad_finished(void)
{
    uint8_t r[32], x[32], r2[32], x2[32];
    fill(r, 32, 1);
    fill(x, 32, 2);
    fill(r2, 32, 3);
    fill(x2, 32, 4);
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    tls_client_init(&C, "localhost", r2, x2, TLS_VERIFY_REQUIRED);
    tls_client_set_trust(&C, &TRUST, NOW);
    uint32_t an = tls_client_hello(&C, A, sizeof A), bn = 0;
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(B[0] == TLS_CT_HANDSHAKE && B[5] == TLS_HS_SERVER_HELLO, "V3 ServerHello first");
    uint32_t shl = 5 + ((uint32_t) B[3] << 8 | B[4]);
    CHECK(B[shl] == TLS_CT_CHANGE_CIPHER_SPEC, "V3 compatibility change_cipher_spec");
    tls_client_feed(&C, B, bn, A, sizeof A, &an);
    /* application data before the client's Finished is refused */
    tls_server_t keep = S;
    uint8_t early[64];
    uint32_t en = tls_record_write(&C.tx, TLS_CT_APPLICATION_DATA, (const uint8_t *) "hi", 2, early,
                                   sizeof early); /* under the client's app keys */
    tls_server_feed(&S, early, en, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S), "V3 data before the client Finished refused");
    S = keep;
    A[an - 1] ^= 0x80;
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_sent == 20, "V4 tampered Finished record refused");
}

/* KeyUpdate (V5), driven by hand on the client's side */
static void key_step(uint8_t secret[HASH_LEN], tls_keys_t *k)
{
    uint8_t next[HASH_LEN], key[32], iv[12];
    tls13_expand_label(secret, "traffic upd", 0, 0, next, HASH_LEN);
    memcpy(secret, next, HASH_LEN);
    tls13_traffic_keys(secret, key, 32, iv, 12);
    tls_keys_set(k, key, iv);
}

static void test_key_update(void)
{
    CHECK(connect_pair("localhost"), "connected for KeyUpdate");
    uint8_t c_ap[HASH_LEN], s_ap[HASH_LEN];
    memcpy(c_ap, C.sched.c_ap_traffic, HASH_LEN);
    memcpy(s_ap, C.sched.s_ap_traffic, HASH_LEN);
    static const uint8_t ku[5] = {24, 0, 0, 1, 1}; /* update_requested */
    uint32_t an = tls_record_write(&C.tx, TLS_CT_HANDSHAKE, ku, 5, A, sizeof A), bn = 0;
    key_step(c_ap, &C.tx);
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(tls_server_established(&S) && bn > 0, "V5 KeyUpdate answered");
    uint8_t type = 0, pt[64];
    uint32_t used = 0, pl = 0;
    CHECK(tls_record_read(&C.rx, B, bn, &used, &type, pt, sizeof pt, &pl) == TLS_REC_OK &&
              type == TLS_CT_HANDSHAKE && pl == 5 && pt[0] == 24 && pt[4] == 0,
          "V5 the answer is a KeyUpdate, not requested");
    key_step(s_ap, &C.rx);
    an = tls_client_send(&C, (const uint8_t *) "after", 5, A, sizeof A);
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    char got[8] = {0};
    CHECK(tls_server_read(&S, (uint8_t *) got, 7) == 5 && !strcmp(got, "after"),
          "V5 the server reads under the new client keys");
    bn = tls_server_send(&S, (const uint8_t *) "back", 4, B, sizeof B);
    tls_client_feed(&C, B, bn, A, sizeof A, &an);
    memset(got, 0, sizeof got);
    CHECK(tls_client_read(&C, (uint8_t *) got, 7) == 4 && !strcmp(got, "back"),
          "V5 the client reads under the new server keys");
    static const uint8_t bad[5] = {24, 0, 0, 1, 7};
    an = tls_record_write(&C.tx, TLS_CT_HANDSHAKE, bad, 5, A, sizeof A);
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_sent == 47, "V5 a bad KeyUpdate value refused");

    CHECK(connect_pair("localhost"), "connected for close");
    uint8_t cn[2] = {1, 0};
    an = tls_record_write(&C.tx, TLS_CT_ALERT, cn, 2, A, sizeof A);
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(S.peer_closed && !tls_server_failed(&S), "V5 close_notify");
    CHECK(tls_server_close(&S, B, sizeof B) > 0, "close_notify sent back");
    CHECK(connect_pair("localhost"), "connected for a fatal alert");
    uint8_t fa[2] = {2, 40};
    an = tls_record_write(&C.tx, TLS_CT_ALERT, fa, 2, A, sizeof A);
    tls_server_feed(&S, A, an, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_got == 40 && bn == 0,
          "V4 a fatal alert is not answered");
}

static void test_refusals(void)
{
    tls_error_t e;
    ch_opt_t o;
    memset(&o, 0, sizeof o);
    CHECK(refuse(&o, &e) == 0xFF && S.state == TLS_SRV_WAIT_FIN,
          "the hand-built hello is accepted");
    memset(&o, 0, sizeof o);
    o.no_versions = 1;
    CHECK(refuse(&o, &e) == 70, "V1 no supported_versions: protocol_version");
    memset(&o, 0, sizeof o);
    o.old_version_only = 1;
    CHECK(refuse(&o, &e) == 70, "V1 TLS 1.2 only: protocol_version");
    memset(&o, 0, sizeof o);
    o.legacy10 = 1;
    CHECK(refuse(&o, &e) == 70, "V1 legacy_version below 1.2: protocol_version");
    memset(&o, 0, sizeof o);
    o.no_chacha = 1;
    CHECK(refuse(&o, &e) == 40, "V1 no ChaCha20-Poly1305: handshake_failure");
    memset(&o, 0, sizeof o);
    o.no_sigalg_p256 = 1;
    CHECK(refuse(&o, &e) == 40, "V2 no ecdsa_secp256r1_sha256: handshake_failure");
    memset(&o, 0, sizeof o);
    o.no_sigalgs = 1;
    CHECK(refuse(&o, &e) == 109, "V2 no signature_algorithms: missing_extension");
    memset(&o, 0, sizeof o);
    o.no_keyshare = 1;
    CHECK(refuse(&o, &e) == 109, "V1 no key_share: missing_extension");
    memset(&o, 0, sizeof o);
    o.other_group_only = 1;
    CHECK(refuse(&o, &e) == 40 && e == TLS_ERR_HELLO_RETRY, "V1 no x25519 share: no retry");
    memset(&o, 0, sizeof o);
    o.short_share = 1;
    CHECK(refuse(&o, &e) == 47, "V1 a 31-byte x25519 share: illegal_parameter");
    memset(&o, 0, sizeof o);
    o.low_point = 1;
    CHECK(refuse(&o, &e) == 47, "V1 an all-zero x25519 share: illegal_parameter");
    memset(&o, 0, sizeof o);
    o.dup_ext = 1;
    CHECK(refuse(&o, &e) == 50, "V1 a repeated extension: decode_error");
    memset(&o, 0, sizeof o);
    o.compression = 1;
    CHECK(refuse(&o, &e) == 47, "V1 compression offered: illegal_parameter");

    /* a hello with a lying length, and a non-hello first */
    uint8_t r[32], x[32];
    fill(r, 32, 1);
    fill(x, 32, 2);
    memset(&o, 0, sizeof o);
    uint32_t n = make_ch(&o, A), bn;
    A[5 + 4 + 2 + 32 + 1 + 32 + 2 + 4 + 2] ^= 0x40; /* the extensions length */
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    tls_server_feed(&S, A, n, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_sent == 50, "V4 a lying length: decode_error");
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    uint8_t fin[8] = {20, 0, 0, 4, 1, 2, 3, 4};
    n = tls_record_write(0, TLS_CT_HANDSHAKE, fin, 8, A, sizeof A);
    tls_server_feed(&S, A, n, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_sent == 10, "V4 Finished first: unexpected_message");
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    n = tls_record_write(0, TLS_CT_APPLICATION_DATA, fin, 8, A, sizeof A);
    tls_server_feed(&S, A, n, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S) && S.alert_sent == 10, "V4 data first: unexpected_message");
    /* the hello and another message in one record */
    uint8_t m[1024];
    memset(&o, 0, sizeof o);
    n = make_ch(&o, A);
    memcpy(m, A + 5, n - 5);
    memcpy(m + n - 5, fin, 8);
    n = tls_record_write(0, TLS_CT_HANDSHAKE, m, n - 5 + 8, A, sizeof A);
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    tls_server_feed(&S, A, n, B, sizeof B, &bn);
    CHECK(tls_server_failed(&S), "a message after the hello in its record refused (RFC 8446 5.1)");
    /* the hello split over two records is fine */
    memset(&o, 0, sizeof o);
    n = make_ch(&o, A);
    memcpy(m, A + 5, n - 5);
    uint32_t half = (n - 5) / 2;
    uint32_t w = tls_record_write(0, TLS_CT_HANDSHAKE, m, half, A, sizeof A);
    w += tls_record_write(0, TLS_CT_HANDSHAKE, m + half, n - 5 - half, A + w, sizeof A - w);
    tls_server_init(&S, CERT, CERT_LEN, KEY, r, x);
    tls_server_feed(&S, A, w, B, sizeof B, &bn);
    CHECK(S.state == TLS_SRV_WAIT_FIN, "a hello split over two records");

    /* init refusals */
    uint8_t zero[32] = {0};
    CHECK(!tls_server_init(&S, CERT, CERT_LEN, zero, r, x) && tls_server_failed(&S),
          "init: a key out of range");
    CHECK(!tls_server_init(&S, CERT, TLS_SRV_CERT_MAX + 1, KEY, r, x), "init: a long certificate");
    CHECK(!tls_server_init(&S, NULL, 0, KEY, r, x), "init: no certificate");
}

int main(void)
{
    test_cert();
    test_handshake();
    test_bad_finished();
    test_key_update();
    test_refusals();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_tls_server\n");
        return 1;
    }
    printf("[PASS] test_tls_server\n");
    return 0;
}
