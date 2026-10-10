/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_handshake.c — the TLS 1.3 client state machine against an in-test
 * server built from the same RFC-vector-tested primitives (hkdf, aead, x25519,
 * record). There is no published ChaCha20 TLS 1.3 trace to replay (RFC 8448
 * uses AES-128-GCM), so this proves the parts that are the client's own: the
 * message order, the transcript it hashes, both Finished MACs, the key
 * switches, and every refusal path.
 *
 * What it does NOT prove: interoperability with a real server, and anything
 * about authentication, which the client deliberately does not provide. */
#include <stdio.h>
#include <string.h>
#include "handshake.h"
#include "../robin_debanks/sha256.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* ---- the in-test server ---- */
typedef struct {
    uint8_t priv[32], pub[32];
    uint8_t tr[TLS_TRANSCRIPT_MAX];
    uint32_t trn;
    tls13_schedule_t s;
    tls_keys_t tx, rx;
} srv_t;

static void tr_add(srv_t *s, const uint8_t *m, uint32_t n)
{
    memcpy(s->tr + s->trn, m, n);
    s->trn += n;
}

static void hs_msg(uint8_t *m, uint8_t type, const uint8_t *body, uint32_t n)
{
    m[0] = type;
    m[1] = (uint8_t) (n >> 16);
    m[2] = (uint8_t) (n >> 8);
    m[3] = (uint8_t) n;
    if (n) memcpy(m + 4, body, n);
}

enum { MUT_NONE, MUT_HRR, MUT_SID, MUT_FIN, MUT_TAG, MUT_PLAIN_EE };

/* Build the server flight answering `ch` (one ClientHello record). Returns
 * bytes in `out`. */
static uint32_t server_flight(srv_t *s, const tls_client_t *c, const uint8_t *ch, uint32_t chn,
                              int mut, uint8_t *out)
{
    static const uint8_t hrr[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11,
                                    0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
                                    0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,
                                    0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};
    uint32_t at = 0, n;
    s->trn = 0;
    tr_add(s, ch + TLS_REC_HDR_LEN, chn - TLS_REC_HDR_LEN);

    uint8_t b[256], sh[260];
    uint32_t k = 0;
    b[k++] = 3;
    b[k++] = 3;
    for (int i = 0; i < 32; i++) b[k++] = mut == MUT_HRR ? hrr[i] : (uint8_t) (0xA0 + i);
    b[k++] = 32;
    memcpy(b + k, c->session_id, 32);
    if (mut == MUT_SID) b[k] ^= 1;
    k += 32;
    b[k++] = 0x13;
    b[k++] = 0x03;
    b[k++] = 0;
    uint8_t ext[] = {0, 43, 0, 2, 3, 4, 0, 51, 0, 36, 0, 0x1D, 0, 32};
    b[k++] = 0;
    b[k++] = (uint8_t) (sizeof ext + 32);
    memcpy(b + k, ext, sizeof ext);
    k += sizeof ext;
    memcpy(b + k, s->pub, 32);
    k += 32;
    hs_msg(sh, TLS_HS_SERVER_HELLO, b, k);
    tr_add(s, sh, 4 + k);

    uint8_t ee[6];
    hs_msg(ee, TLS_HS_ENCRYPTED_EXTENSIONS, (const uint8_t *) "\0\0", 2);

    if (mut == MUT_PLAIN_EE) {
        /* ServerHello and a forged plaintext EncryptedExtensions in ONE record */
        uint8_t both[300];
        memcpy(both, sh, 4 + k);
        memcpy(both + 4 + k, ee, 6);
        return tls_record_write(0, TLS_CT_HANDSHAKE, both, 4 + k + 6, out, 4096);
    }
    at += tls_record_write(0, TLS_CT_HANDSHAKE, sh, 4 + k, out + at, 4096);

    uint8_t shared[32];
    x25519_shared(shared, s->priv, c->pub);
    tls13_early_secret(&s->s, 0, 0);
    tls13_handshake_secret(&s->s, shared, 32, s->tr, s->trn);
    uint8_t key[32], iv[12];
    tls13_traffic_keys(s->s.s_hs_traffic, key, 32, iv, 12);
    tls_keys_set(&s->tx, key, iv);
    tls13_traffic_keys(s->s.c_hs_traffic, key, 32, iv, 12);
    tls_keys_set(&s->rx, key, iv);

    /* EE, Certificate (empty list), CertificateVerify (no signature), Finished */
    uint8_t cert[8], cv[8], fin[4 + HASH_LEN], th[HASH_LEN], vd[HASH_LEN];
    hs_msg(cert, TLS_HS_CERTIFICATE, (const uint8_t *) "\0\0\0\0", 4);
    hs_msg(cv, TLS_HS_CERTIFICATE_VERIFY, (const uint8_t *) "\x08\x07\0\0", 4);
    tr_add(s, ee, 6);
    tr_add(s, cert, 8);
    tr_add(s, cv, 8);
    sha256(s->tr, s->trn, th);
    tls13_finished(s->s.s_hs_traffic, th, vd);
    if (mut == MUT_FIN) vd[0] ^= 1;
    hs_msg(fin, TLS_HS_FINISHED, vd, HASH_LEN);
    tr_add(s, fin, sizeof fin);

    uint8_t flight[6 + 8 + 8 + 4 + HASH_LEN];
    memcpy(flight, ee, 6);
    memcpy(flight + 6, cert, 8);
    memcpy(flight + 14, cv, 8);
    memcpy(flight + 22, fin, sizeof fin);
    n = tls_record_write(&s->tx, TLS_CT_HANDSHAKE, flight, sizeof flight, out + at, 4096);
    if (mut == MUT_TAG) out[at + n - 1] ^= 1;
    at += n;
    tls13_master_secret(&s->s, s->tr, s->trn);
    return at;
}

static void client_new(tls_client_t *c, tls_verify_policy_t pol)
{
    uint8_t rnd[32], priv[32];
    for (int i = 0; i < 32; i++) {
        rnd[i] = (uint8_t) (i * 7 + 1);
        priv[i] = (uint8_t) (0x55 ^ (i * 13));
    }
    tls_client_init(c, "example.org", rnd, priv, pol);
}

static void srv_new(srv_t *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 32; i++) s->priv[i] = (uint8_t) (0x77 + i * 3);
    x25519_public(s->pub, s->priv);
}

/* Run one handshake with `mut`; returns the client's final state. */
static tls_client_t g_c;
static srv_t g_s;
static uint8_t g_ch[1024], g_fl[8192], g_rep[512];
static uint32_t g_repn;
static tls_hs_state_t run(int mut, tls_verify_policy_t pol)
{
    client_new(&g_c, pol);
    srv_new(&g_s);
    uint32_t chn = tls_client_hello(&g_c, g_ch, sizeof g_ch);
    if (!chn) return TLS_ST_FAILED;
    uint32_t fl = server_flight(&g_s, &g_c, g_ch, chn, mut, g_fl);
    g_repn = 0;
    tls_client_feed(&g_c, g_fl, fl, g_rep, sizeof g_rep, &g_repn);
    return g_c.state;
}

int main(void)
{
    printf("=== TLS 1.3 client handshake (in-test server) ===\n");

    /* ---- the happy path, with the insecure policy acknowledged ---- */
    CHECK(run(MUT_NONE, TLS_VERIFY_INSECURE_ACKNOWLEDGED) == TLS_ST_ESTABLISHED,
          "handshake completes: transcript, both key switches and server Finished agree");
    CHECK(!tls_client_is_authenticated(&g_c),
          "...and the connection reports itself UNauthenticated (no cert verifier exists)");
    CHECK(g_repn > 0, "the client produced its Finished");
    {
        uint8_t pt[64], typ;
        uint32_t used, pl;
        int rc = tls_record_read(&g_s.rx, g_rep, g_repn, &used, &typ, pt, sizeof pt, &pl);
        uint8_t th[HASH_LEN], want[HASH_LEN];
        sha256(g_s.tr, g_s.trn, th);
        tls13_finished(g_s.s.c_hs_traffic, th, want);
        CHECK(rc == TLS_REC_OK && typ == TLS_CT_HANDSHAKE && pl == 4 + HASH_LEN &&
                  pt[0] == TLS_HS_FINISHED && memcmp(pt + 4, want, HASH_LEN) == 0,
              "the client Finished verifies at the server, under the HANDSHAKE keys");
    }
    {
        /* application data both ways under the application keys */
        uint8_t key[32], iv[12], rec[256], pt[64], typ;
        uint32_t used, pl;
        tls_keys_t stx, srx;
        tls13_traffic_keys(g_s.s.s_ap_traffic, key, 32, iv, 12);
        tls_keys_set(&stx, key, iv);
        tls13_traffic_keys(g_s.s.c_ap_traffic, key, 32, iv, 12);
        tls_keys_set(&srx, key, iv);
        uint32_t n = tls_client_send(&g_c, (const uint8_t *) "GET /", 5, rec, sizeof rec);
        int rc = tls_record_read(&srx, rec, n, &used, &typ, pt, sizeof pt, &pl);
        CHECK(rc == TLS_REC_OK && typ == TLS_CT_APPLICATION_DATA && pl == 5 &&
                  memcmp(pt, "GET /", 5) == 0,
              "client -> server application data decrypts under the server's c_ap key");
        n = tls_record_write(&stx, TLS_CT_APPLICATION_DATA, (const uint8_t *) "200 OK", 6, rec,
                             sizeof rec);
        uint32_t dummy;
        tls_client_feed(&g_c, rec, n, g_rep, sizeof g_rep, &dummy);
        uint32_t got = tls_client_read(&g_c, pt, sizeof pt);
        CHECK(got == 6 && memcmp(pt, "200 OK", 6) == 0,
              "server -> client application data is delivered to the reader");
    }

    /* ---- refusals ---- */
    CHECK(run(MUT_NONE, TLS_VERIFY_REQUIRED) == TLS_ST_FAILED && g_c.error == TLS_ERR_NO_VERIFIER &&
              g_repn == 0,
          "TLS_VERIFY_REQUIRED stops at CertificateVerify: no verifier, no Finished sent");
    CHECK(run(MUT_FIN, TLS_VERIFY_INSECURE_ACKNOWLEDGED) == TLS_ST_FAILED &&
              g_c.error == TLS_ERR_BAD_FINISHED && g_repn == 0,
          "a wrong server Finished is refused and nothing is sent back");
    CHECK(run(MUT_TAG, TLS_VERIFY_INSECURE_ACKNOWLEDGED) == TLS_ST_FAILED &&
              g_c.error == TLS_ERR_BAD_RECORD,
          "a flipped AEAD tag fails the record, not the handshake logic");
    CHECK(run(MUT_HRR, TLS_VERIFY_INSECURE_ACKNOWLEDGED) == TLS_ST_FAILED &&
              g_c.error == TLS_ERR_HELLO_RETRY,
          "a HelloRetryRequest is a clean refusal (only x25519 is offered)");
    CHECK(run(MUT_SID, TLS_VERIFY_INSECURE_ACKNOWLEDGED) == TLS_ST_FAILED &&
              g_c.error == TLS_ERR_PROTOCOL,
          "a ServerHello whose session-id echo differs from ours is refused");
    CHECK(run(MUT_PLAIN_EE, TLS_VERIFY_INSECURE_ACKNOWLEDGED) == TLS_ST_FAILED &&
              g_c.error == TLS_ERR_PROTOCOL,
          "a plaintext message after ServerHello in the same record is refused (no key-change "
          "spanning)");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
