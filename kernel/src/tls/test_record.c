/* test_record.c — the TLS 1.3 record layer.
 *
 * Two record layers are driven against each other, so a record only decodes
 * if the sender and receiver agree on the header, the AAD, the nonce
 * derivation and the sequence number. The four silent mistakes named in
 * record.h each get a test that fails if they come back.
 */
#include <stdio.h>
#include <string.h>
#include "record.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

int main(void)
{
    printf("=== TLS 1.3 record layer (RFC 8446 section 5) ===\n");

    uint8_t key[32], iv[12];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t) (i + 1);
    for (int i = 0; i < 12; i++) iv[i] = (uint8_t) (0xA0 + i);

    static uint8_t rec[TLS_REC_BUF];
    static uint8_t got[TLS_REC_BUF];
    uint32_t n, used, glen;
    uint8_t gtype;
    int rc;

    /* ---------- unprotected records ---------- */
    {
        tls_keys_t w, r;
        tls_keys_clear(&w);
        tls_keys_clear(&r);
        const char *hs = "\x01\x00\x00\x04zxvv";
        n = tls_record_write(&w, TLS_CT_HANDSHAKE, (const uint8_t *) hs, 8, rec, sizeof rec);
        CHECK(n == 5 + 8, "an unprotected record is header + payload");
        CHECK(rec[0] == TLS_CT_HANDSHAKE, "the outer type is the REAL type when in clear");
        CHECK(rec[1] == 0x03 && rec[2] == 0x03, "legacy_record_version is 0x0303");
        CHECK(rec[3] == 0 && rec[4] == 8, "the length is big-endian");

        rc = tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK && used == n, "it reads back");
        CHECK(gtype == TLS_CT_HANDSHAKE && glen == 8 && memcmp(got, hs, 8) == 0,
              "type and payload survive");
    }

    /* ---------- partial input asks for more, never guesses ---------- */
    {
        tls_keys_t r;
        tls_keys_clear(&r);
        tls_keys_t w;
        tls_keys_clear(&w);
        n = tls_record_write(&w, TLS_CT_HANDSHAKE, (const uint8_t *) "abcdefgh", 8, rec,
                             sizeof rec);
        for (uint32_t cut = 0; cut < n; cut++) {
            rc = tls_record_read(&r, rec, cut, &used, &gtype, got, sizeof got, &glen);
            if (rc != TLS_REC_NEED_MORE) {
                printf("[FAIL] a %u-byte prefix of a %u-byte record did not "
                       "return NEED_MORE (got %d)\n",
                       cut, n, rc);
                failures++;
                break;
            }
            if (used != 0) {
                printf("[FAIL] NEED_MORE consumed bytes\n");
                failures++;
                break;
            }
        }
        CHECK(1, "every prefix of a record returns NEED_MORE and consumes nothing");
    }

    /* ---------- protected records ---------- */
    {
        tls_keys_t w, r;
        tls_keys_set(&w, key, iv);
        tls_keys_set(&r, key, iv);
        CHECK(w.seq == 0 && r.seq == 0, "installing keys RESETS the sequence number");

        const char *msg = "GET / HTTP/1.1\r\nHost: zxv\r\n\r\n";
        uint32_t mlen = (uint32_t) strlen(msg);

        n = tls_record_write(&w, TLS_CT_APPLICATION_DATA, (const uint8_t *) msg, mlen, rec,
                             sizeof rec);
        CHECK(n == 5 + mlen + 1 + 16,
              "a protected record is header + content + type byte + 16-byte tag");
        CHECK(rec[0] == TLS_CT_APPLICATION_DATA, "the outer type is application_data");
        CHECK(((uint32_t) rec[3] << 8 | rec[4]) == mlen + 1 + 16,
              "the header length counts the ENCRYPTED record INCLUDING the tag "
              "— this value is also the AAD, so getting it wrong is silent");
        CHECK(memcmp(rec + 5, msg, mlen) != 0, "the payload is actually encrypted");

        rc = tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK, "it decrypts");
        CHECK(gtype == TLS_CT_APPLICATION_DATA && glen == mlen && memcmp(got, msg, mlen) == 0,
              "and yields the original bytes");
        CHECK(w.seq == 1 && r.seq == 1, "both sides advanced their sequence by one");

        /* the REAL type comes from inside the encryption */
        n = tls_record_write(&w, TLS_CT_HANDSHAKE, (const uint8_t *) "\x14\x00\x00\x20", 4, rec,
                             sizeof rec);
        CHECK(rec[0] == TLS_CT_APPLICATION_DATA,
              "a protected HANDSHAKE record still shows application_data outside");
        rc = tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK && gtype == TLS_CT_HANDSHAKE && glen == 4,
              "the REAL content type is recovered from inside the encryption "
              "— trusting the outer type would see every handshake message as "
              "application data");
    }

    /* ---------- the sequence number really is used ---------- */
    {
        tls_keys_t w, r;
        tls_keys_set(&w, key, iv);
        tls_keys_set(&r, key, iv);
        uint8_t first[TLS_REC_BUF];
        uint32_t f1 = tls_record_write(&w, TLS_CT_APPLICATION_DATA, (const uint8_t *) "AAAA", 4,
                                       first, sizeof first);
        uint32_t f2 = tls_record_write(&w, TLS_CT_APPLICATION_DATA, (const uint8_t *) "AAAA", 4,
                                       rec, sizeof rec);
        CHECK(f1 == f2 && memcmp(first, rec, f1) != 0,
              "the SAME plaintext encrypts differently at sequence 0 and 1 "
              "(otherwise the nonce is not advancing and the keystream repeats)");

        /* records must be read IN ORDER */
        rc = tls_record_read(&r, rec, f2, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_AUTH_FAIL, "record 1 does not decrypt while the reader is still at 0 — "
                                       "records cannot be reordered");
        CHECK(r.seq == 0, "and a failed open does NOT advance the sequence number");
        rc = tls_record_read(&r, first, f1, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK, "record 0 then decrypts");
        rc = tls_record_read(&r, rec, f2, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK && glen == 4, "and record 1 follows");
    }

    /* ---------- tampering ---------- */
    {
        tls_keys_t w, r;
        tls_keys_set(&w, key, iv);
        tls_keys_set(&r, key, iv);
        n = tls_record_write(&w, TLS_CT_APPLICATION_DATA, (const uint8_t *) "secret payload", 14,
                             rec, sizeof rec);

        uint8_t bad[TLS_REC_BUF];

        memcpy(bad, rec, n);
        bad[7] ^= 0x01;
        tls_keys_set(&r, key, iv);
        CHECK(tls_record_read(&r, bad, n, &used, &gtype, got, sizeof got, &glen) ==
                  TLS_REC_AUTH_FAIL,
              "a flipped ciphertext bit fails to open");

        memcpy(bad, rec, n);
        bad[n - 1] ^= 0x01;
        tls_keys_set(&r, key, iv);
        CHECK(tls_record_read(&r, bad, n, &used, &gtype, got, sizeof got, &glen) ==
                  TLS_REC_AUTH_FAIL,
              "a flipped tag bit fails to open");

        /* the HEADER is authenticated because it is the AAD */
        memcpy(bad, rec, n);
        bad[1] = 0x04;
        tls_keys_set(&r, key, iv);
        CHECK(tls_record_read(&r, bad, n, &used, &gtype, got, sizeof got, &glen) ==
                  TLS_REC_AUTH_FAIL,
              "changing legacy_record_version fails — the HEADER is the AAD, "
              "so it is authenticated even though it is not encrypted");

        memcpy(bad, rec, n);
        bad[0] = TLS_CT_HANDSHAKE;
        tls_keys_set(&r, key, iv);
        CHECK(tls_record_read(&r, bad, n, &used, &gtype, got, sizeof got, &glen) ==
                  TLS_REC_AUTH_FAIL,
              "changing the outer content type fails");
    }

    /* ---------- change_cipher_spec is dropped without counting ---------- */
    {
        tls_keys_t w, r;
        tls_keys_set(&w, key, iv);
        tls_keys_set(&r, key, iv);

        uint8_t ccs[6] = {TLS_CT_CHANGE_CIPHER_SPEC, 0x03, 0x03, 0x00, 0x01, 0x01};
        rc = tls_record_read(&r, ccs, sizeof ccs, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_SKIP && used == 6, "a legacy change_cipher_spec record is skipped");
        CHECK(r.seq == 0, "and does NOT advance the sequence number — counting it would "
                          "desynchronise every nonce that follows");

        /* a real record still decodes right after it */
        n = tls_record_write(&w, TLS_CT_HANDSHAKE, (const uint8_t *) "\x02\x00", 2, rec,
                             sizeof rec);
        rc = tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK && gtype == TLS_CT_HANDSHAKE, "the next real record still decodes");
    }

    /* ---------- malformed and hostile ---------- */
    {
        tls_keys_t r;
        tls_keys_set(&r, key, iv);

        /* a length beyond the protocol maximum */
        uint8_t huge[5] = {23, 3, 3, 0xFF, 0xFF};
        CHECK(tls_record_read(&r, huge, 5, &used, &gtype, got, sizeof got, &glen) == TLS_REC_BAD,
              "a length above 2^14+256 is refused immediately, without waiting "
              "for bytes that may never legally arrive");

        /* a protected record too short to hold a tag and a type byte */
        uint8_t tiny[5 + 16] = {23, 3, 3, 0, 16};
        CHECK(tls_record_read(&r, tiny, sizeof tiny, &used, &gtype, got, sizeof got, &glen) ==
                  TLS_REC_BAD,
              "a record with no room for a content type is refused");

        /* an all-padding inner plaintext has no content type */
        tls_keys_t w;
        tls_keys_set(&w, key, iv);
        uint8_t nonce[12];
        tls13_record_nonce(iv, 0, nonce);
        uint8_t zeros[8];
        memset(zeros, 0, sizeof zeros);
        uint32_t ilen = sizeof zeros;
        uint32_t clen = ilen + 16;
        rec[0] = 23;
        rec[1] = 3;
        rec[2] = 3;
        rec[3] = (uint8_t) (clen >> 8);
        rec[4] = (uint8_t) clen;
        aead_seal(key, nonce, rec, 5, zeros, rec + 5, ilen, rec + 5 + ilen);
        tls_keys_set(&r, key, iv);
        CHECK(tls_record_read(&r, rec, 5 + clen, &used, &gtype, got, sizeof got, &glen) ==
                  TLS_REC_BAD,
              "an all-padding record is an ERROR, not an empty message");

        /* random junk must never crash */
        static uint8_t junk[600];
        for (uint32_t s = 0; s < 3000; s++) {
            uint32_t x = s * 2654435761u;
            for (uint32_t i = 0; i < sizeof junk; i++) {
                x = x * 1103515245u + 12345u;
                junk[i] = (uint8_t) (x >> 16);
            }
            tls_keys_t rr;
            tls_keys_set(&rr, key, iv);
            (void) tls_record_read(&rr, junk, sizeof junk, &used, &gtype, got, sizeof got, &glen);
            tls_keys_t cc;
            tls_keys_clear(&cc);
            (void) tls_record_read(&cc, junk, sizeof junk, &used, &gtype, got, sizeof got, &glen);
        }
        CHECK(1, "3000 randomised buffers parsed without crashing");
    }

    /* ---------- a key change resets the sequence ---------- */
    {
        tls_keys_t w, r;
        tls_keys_set(&w, key, iv);
        tls_keys_set(&r, key, iv);
        for (int i = 0; i < 3; i++) {
            n = tls_record_write(&w, TLS_CT_APPLICATION_DATA, (const uint8_t *) "xy", 2, rec,
                                 sizeof rec);
            tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        }
        CHECK(w.seq == 3 && r.seq == 3, "three records advanced both counters");

        uint8_t key2[32], iv2[12];
        for (int i = 0; i < 32; i++) key2[i] = (uint8_t) (0x70 + i);
        for (int i = 0; i < 12; i++) iv2[i] = (uint8_t) (0x30 + i);
        tls_keys_set(&w, key2, iv2);
        tls_keys_set(&r, key2, iv2);
        CHECK(w.seq == 0 && r.seq == 0, "a key change resets both sequence numbers to zero");
        n = tls_record_write(&w, TLS_CT_APPLICATION_DATA, (const uint8_t *) "after rekey", 11, rec,
                             sizeof rec);
        rc = tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK && glen == 11 && memcmp(got, "after rekey", 11) == 0,
              "and records under the new keys decode");
    }

    /* ---------- maximum-size payload ---------- */
    {
        tls_keys_t w, r;
        tls_keys_set(&w, key, iv);
        tls_keys_set(&r, key, iv);
        static uint8_t big[TLS_MAX_PLAINTEXT];
        for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t) (i * 31 + 7);
        n = tls_record_write(&w, TLS_CT_APPLICATION_DATA, big, sizeof big, rec, sizeof rec);
        CHECK(n == 5 + TLS_MAX_PLAINTEXT + 1 + 16, "a full 2^14 payload writes");
        rc = tls_record_read(&r, rec, n, &used, &gtype, got, sizeof got, &glen);
        CHECK(rc == TLS_REC_OK && glen == TLS_MAX_PLAINTEXT &&
                  memcmp(got, big, TLS_MAX_PLAINTEXT) == 0,
              "and round-trips intact at the maximum size");
        CHECK(tls_record_write(&w, TLS_CT_APPLICATION_DATA, big, TLS_MAX_PLAINTEXT + 1, rec,
                               sizeof rec) == 0,
              "one byte over the maximum is refused");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
