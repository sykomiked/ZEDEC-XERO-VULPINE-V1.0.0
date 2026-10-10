/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_tls_record.c — the TLS 1.3 record reader (tls/record.c).
 *
 * Input: byte 0 selects cleartext (even) or protected (odd) records; for
 * protected records the next 44 bytes are the key and IV. The rest is a
 * byte stream read record by record. Properties: consumed never exceeds
 * what is left, the payload length never exceeds the output capacity, and a
 * protected record that reads OK survives a write + read round trip under
 * the same key and sequence number. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "record.h"
#include "fuzz_in.h"

static uint8_t g_out[TLS_MAX_PLAINTEXT + 256];
static uint8_t g_rt[TLS_MAX_PLAINTEXT + 512];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) return 0;
    fz_in in;
    fz_init(&in, data, size);
    uint8_t mode = fz_u8(&in);
    tls_keys_t k, k2;
    memset(&k, 0, sizeof k);
    uint8_t key[CHACHA20_KEY_LEN], iv[CHACHA20_NONCE_LEN];
    if (mode & 1) {
        fz_bytes(&in, key, sizeof key);
        fz_bytes(&in, iv, sizeof iv);
        tls_keys_set(&k, key, iv);
    }
    uint32_t cap = (mode & 2) ? 64u : (uint32_t) sizeof g_out;
    uint8_t *buf = fz_dup(in.p, in.n);
    uint32_t len = (uint32_t) in.n, off = 0;
    for (int i = 0; i < 64 && off < len; i++) {
        uint32_t used = 0, out_len = 0;
        uint8_t type = 0;
        uint64_t seq = k.seq;
        int rc = tls_record_read(&k, buf + off, len - off, &used, &type, g_out, cap, &out_len);
        if (used > len - off) abort();
        if (rc == TLS_REC_OK || rc == TLS_REC_SKIP) {
            if (out_len > cap) abort();
            if (rc == TLS_REC_OK && (mode & 1) && k.active && out_len <= TLS_MAX_PLAINTEXT) {
                /* write the payload back under the same key and sequence
                 * number and read it again: it must come back unchanged */
                tls_keys_set(&k2, key, iv);
                k2.seq = seq;
                uint32_t w = tls_record_write(&k2, type, g_out, out_len, g_rt, sizeof g_rt);
                if (w == 0) abort();
                tls_keys_set(&k2, key, iv);
                k2.seq = seq;
                static uint8_t back[TLS_MAX_PLAINTEXT + 256];
                uint32_t u2 = 0, l2 = 0;
                uint8_t t2 = 0;
                if (tls_record_read(&k2, g_rt, w, &u2, &t2, back, sizeof back, &l2) != TLS_REC_OK ||
                    u2 != w || t2 != type || l2 != out_len || memcmp(back, g_out, l2) != 0)
                    abort();
            }
        }
        if (rc != TLS_REC_OK && rc != TLS_REC_SKIP) break;
        if (used == 0) break;
        off += used;
    }
    free(buf);
    return 0;
}
