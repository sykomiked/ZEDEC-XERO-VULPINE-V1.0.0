/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_bootlegger.c — the Bootlegger P2P message parsers
 * (bootlegger/bootlegger.c): the signed handshake, the sealed message
 * opener, and the ML-KEM-768 session acceptor.
 *
 * Byte 0 (mod 3) picks:
 *   0  handshake: the wire handshake (magic, BE version/phase/peer_id,
 *      pubkey, signature) decoded into bootlegger_handshake_t, optionally
 *      with a pinned key, given to bootlegger_handshake_recv. Property: the
 *      connection is marked authenticated only when recv returns 0.
 *   1  message: a 32-byte session key, then a message; bootlegger_open_msg
 *      on the raw bytes, and a seal -> open round trip of the same bytes as
 *      a payload must return them unchanged.
 *   2  KEM: decapsulation key and ciphertext from the input given to
 *      bootlegger_kem_accept on an authenticated connection. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bootlegger.h"
#include "fuzz_in.h"

static bootlegger_conn_t g_conn;
static uint8_t g_pl[65536 + 64], g_sealed[65536 + 64];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 65536u) return 0;
    fz_in in;
    fz_init(&in, data, size);
    uint8_t mode = (uint8_t) (fz_u8(&in) % 3u);
    memset(&g_conn, 0, sizeof g_conn);

    if (mode == 0) {
        bootlegger_handshake_t hs;
        uint8_t pin = fz_u8(&in);
        if (pin & 1) {
            uint8_t pk[32];
            fz_bytes(&in, pk, 32);
            bootlegger_pin_peer(&g_conn, pk);
        }
        fz_bytes(&in, hs.magic, BOOTLEGGER_MAGIC_LEN);
        uint8_t be[8];
        fz_bytes(&in, be, sizeof be); /* big-endian version, phase, peer_id */
        hs.version = (uint16_t) ((be[0] << 8) | be[1]);
        hs.phase = (uint16_t) ((be[2] << 8) | be[3]);
        hs.peer_id =
            ((uint32_t) be[4] << 24) | ((uint32_t) be[5] << 16) | ((uint32_t) be[6] << 8) | be[7];
        fz_bytes(&in, hs.pubkey, 32);
        fz_bytes(&in, hs.signature, 64);
        uint8_t bytes[BOOTLEGGER_HS_BYTES];
        if (bootlegger_handshake_bytes(&hs, bytes) != BOOTLEGGER_HS_BYTES) abort();
        int rc = bootlegger_handshake_recv(&g_conn, &hs);
        if ((rc == 0) != (g_conn.authenticated == 1)) abort();
    } else if (mode == 1) {
        uint8_t key[32];
        fz_bytes(&in, key, 32);
        memcpy(g_conn.rx_key, key, 32);
        memcpy(g_conn.tx_key, key, 32);
        g_conn.connected = 1;
        g_conn.authenticated = 1;
        g_conn.has_session = 1;
        uint8_t *msg = fz_dup(in.p, in.n);
        uint32_t len = (uint32_t) in.n;
        bootlegger_msg_type_t type;
        int r = bootlegger_open_msg(&g_conn, msg, len, &type, g_pl, sizeof g_pl);
        if (r > 0 && (uint32_t) r + BOOTLEGGER_MSG_HDR + BOOTLEGGER_TAG_SIZE != len) abort();
        /* round trip: seal the input as a payload, then open it */
        if (len <= 0xFFFFu) {
            g_conn.tx_seq = 0;
            g_conn.rx_seq = 0;
            int w = bootlegger_seal_msg(&g_conn, BOOTLEGGER_MSG_CHAT, msg, (uint16_t) len, g_sealed,
                                        sizeof g_sealed);
            if (w != (int) (len + BOOTLEGGER_MSG_HDR + BOOTLEGGER_TAG_SIZE)) abort();
            int o = bootlegger_open_msg(&g_conn, g_sealed, (uint32_t) w, &type, g_pl, sizeof g_pl);
            if (o != (int) len || type != BOOTLEGGER_MSG_CHAT || memcmp(g_pl, msg, len) != 0)
                abort();
        }
        free(msg);
    } else {
        static uint8_t dk[MLKEM768_DK_BYTES], ct[MLKEM768_CT_BYTES];
        fz_bytes(&in, dk, sizeof dk);
        fz_bytes(&in, ct, sizeof ct);
        g_conn.connected = 1;
        g_conn.authenticated = 1;
        (void) bootlegger_kem_accept(&g_conn, dk, ct);
    }
    return 0;
}
