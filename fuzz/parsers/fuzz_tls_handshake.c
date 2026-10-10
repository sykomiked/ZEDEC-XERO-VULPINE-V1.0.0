/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_tls_handshake.c — the TLS 1.3 client handshake parser (tls/handshake.c).
 *
 * The client sends its ClientHello (fixed randomness) and the fuzz input is
 * what the server says back. Byte 0 picks the mode:
 *
 *   even  RAW: the rest of the input is the server's byte stream, fed to
 *         tls_client_feed in chunks whose sizes come from the input (so
 *         partial records and records spanning feeds are exercised). This
 *         reaches the plaintext ServerHello parser and the record layer.
 *   odd   FLIGHT: the harness answers with a well-formed ServerHello (a real
 *         x25519 share), derives the server handshake keys exactly as a
 *         server would, and ENCRYPTS the rest of the input under them as the
 *         handshake flight. This puts fuzz bytes behind the AEAD, into the
 *         EncryptedExtensions / Certificate / CertificateVerify / Finished
 *         parsers, which a raw fuzzer could never reach.
 *
 * Properties: feed never reports consuming more than it was given, the
 * reply length stays within its buffer, and a client that is not ESTABLISHED
 * never hands out application data. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "handshake.h"
#include "../robin_debanks/sha256.h"
#include "fuzz_in.h"

static tls_client_t g_c;
static uint8_t g_ch[2048], g_rep[1024], g_rec[TLS_MAX_PLAINTEXT + 512], g_app[256];

static void hs_hdr(uint8_t *m, uint8_t type, uint32_t n)
{
    m[0] = type;
    m[1] = (uint8_t) (n >> 16);
    m[2] = (uint8_t) (n >> 8);
    m[3] = (uint8_t) n;
}

static void feed(const uint8_t *p, uint32_t n)
{
    uint32_t rl = 0;
    uint32_t used = tls_client_feed(&g_c, p, n, g_rep, sizeof g_rep, &rl);
    if (used > n || rl > sizeof g_rep) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 65536u) return 0;
    fz_in in;
    fz_init(&in, data, size);
    uint8_t mode = fz_u8(&in);

    uint8_t rnd[32], priv[32];
    for (int i = 0; i < 32; i++) {
        rnd[i] = (uint8_t) (i * 7 + 1);
        priv[i] = (uint8_t) (0x55 ^ (i * 13));
    }
    tls_client_init(&g_c, "example.org", rnd, priv,
                    (mode & 2) ? TLS_VERIFY_REQUIRED : TLS_VERIFY_INSECURE_ACKNOWLEDGED);
    uint32_t chn = tls_client_hello(&g_c, g_ch, sizeof g_ch);
    if (chn <= TLS_REC_HDR_LEN) abort();

    if ((mode & 1) == 0) {
        uint8_t *buf = fz_dup(in.p, in.n);
        uint32_t len = (uint32_t) in.n, off = 0;
        uint32_t step = (mode >> 2) ? (uint32_t) (mode >> 2) * 7u : len;
        while (off < len && !tls_client_failed(&g_c)) {
            uint32_t n = len - off < step ? len - off : step;
            uint8_t *chunk = fz_dup(buf + off, n);
            feed(chunk, n);
            free(chunk);
            off += n;
        }
        free(buf);
    } else {
        /* server: transcript = ClientHello || ServerHello || flight */
        static uint8_t tr[TLS_TRANSCRIPT_MAX];
        uint32_t trn = chn - TLS_REC_HDR_LEN;
        memcpy(tr, g_ch + TLS_REC_HDR_LEN, trn);
        uint8_t spriv[32], spub[32];
        for (int i = 0; i < 32; i++) spriv[i] = (uint8_t) (0x77 + i * 3);
        x25519_public(spub, spriv);

        uint8_t sh[4 + 128];
        uint32_t k = 4;
        sh[k++] = 3;
        sh[k++] = 3;
        for (int i = 0; i < 32; i++) sh[k++] = (uint8_t) (0xA0 + i);
        sh[k++] = 32;
        memcpy(sh + k, g_c.session_id, 32);
        k += 32;
        sh[k++] = 0x13;
        sh[k++] = 0x03;
        sh[k++] = 0;
        static const uint8_t ext[] = {0, 43, 0, 2, 3, 4, 0, 51, 0, 36, 0, 0x1D, 0, 32};
        sh[k++] = 0;
        sh[k++] = (uint8_t) (sizeof ext + 32);
        memcpy(sh + k, ext, sizeof ext);
        k += sizeof ext;
        memcpy(sh + k, spub, 32);
        k += 32;
        hs_hdr(sh, TLS_HS_SERVER_HELLO, k - 4);
        memcpy(tr + trn, sh, k);
        trn += k;
        uint32_t w = tls_record_write(0, TLS_CT_HANDSHAKE, sh, k, g_rec, sizeof g_rec);
        feed(g_rec, w);

        uint8_t shared[32], key[32], iv[12];
        x25519_shared(shared, spriv, g_c.pub);
        tls13_schedule_t s;
        tls13_early_secret(&s, 0, 0);
        tls13_handshake_secret(&s, shared, 32, tr, trn);
        tls13_traffic_keys(s.s_hs_traffic, key, 32, iv, 12);
        tls_keys_t tx;
        tls_keys_set(&tx, key, iv);

        /* the flight, cut into records of the sizes the input asks for */
        uint32_t left = (uint32_t) in.n;
        const uint8_t *p = in.p;
        uint32_t rsz = (mode >> 2) ? (uint32_t) (mode >> 2) * 64u : TLS_MAX_PLAINTEXT;
        for (int r = 0; r < 64 && left && !tls_client_failed(&g_c); r++) {
            uint32_t n = left < rsz ? left : rsz;
            w = tls_record_write(&tx, TLS_CT_HANDSHAKE, p, n, g_rec, sizeof g_rec);
            if (w == 0) break;
            feed(g_rec, w);
            p += n;
            left -= n;
        }
    }

    uint32_t got = tls_client_read(&g_c, g_app, sizeof g_app);
    if (got > sizeof g_app) abort();
    if (!tls_client_established(&g_c) && got != 0) abort();
    return 0;
}
