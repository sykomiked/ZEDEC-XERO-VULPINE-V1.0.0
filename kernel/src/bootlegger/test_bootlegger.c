/* test_bootlegger.c — Bootlegger: authentication must be a real signature
 * check and sessions must be real AEAD.
 *
 * Regression for the old module, which accepted any handshake carrying the
 * magic string, treated a set `authenticated` byte as proof, and "encrypted"
 * with a phase-frequency XOR (freq_crypto). Each check below fails on it.
 * The Ed25519 vector was produced with python `cryptography` (seed 00..1f).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "bootlegger.h"

static int fails = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            printf("  ok   %s\n", m);                                                              \
        else {                                                                                     \
            printf("  FAIL %s\n", m);                                                              \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static const uint8_t PK[32] = {0x03, 0xa1, 0x07, 0xbf, 0xf3, 0xce, 0x10, 0xbe, 0x1d, 0x70, 0xdd,
                               0x18, 0xe7, 0x4b, 0xc0, 0x99, 0x67, 0xe4, 0xd6, 0x30, 0x9b, 0xa5,
                               0x0d, 0x5f, 0x1d, 0xdc, 0x86, 0x64, 0x12, 0x55, 0x31, 0xb8};
static const uint8_t SIG[64] = {
    0x39, 0xbf, 0x46, 0x28, 0x1b, 0x72, 0x11, 0xa4, 0xb3, 0xaa, 0x00, 0xdd, 0x97, 0x4a, 0xff, 0x67,
    0x4b, 0x6f, 0xce, 0x06, 0x9a, 0x78, 0x54, 0xc2, 0x46, 0x23, 0x48, 0x23, 0x4e, 0xfb, 0xb7, 0xf8,
    0xd0, 0x5d, 0xc1, 0x67, 0x8b, 0x51, 0x72, 0xe8, 0xf9, 0x6c, 0x4a, 0x37, 0x64, 0x9e, 0x11, 0x0f,
    0xd3, 0x1d, 0xda, 0x58, 0x98, 0x2e, 0xea, 0xfc, 0x2e, 0x84, 0x7d, 0xa5, 0xb4, 0x84, 0x86, 0x07};

static bootlegger_conn_t A, B, C;
static bootlegger_handshake_t hs, bad;
static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES], ct[MLKEM768_CT_BYTES];
static uint8_t wire[600], pt[600];

static void good_hs(bootlegger_handshake_t *h)
{
    memset(h, 0, sizeof *h);
    memcpy(h->magic, BOOTLEGGER_MAGIC, BOOTLEGGER_MAGIC_LEN);
    h->version = BOOTLEGGER_VERSION;
    h->phase = 7;
    h->peer_id = 0x25dc6af5u;
    memcpy(h->pubkey, PK, 32);
    memcpy(h->signature, SIG, 64);
}

int main(void)
{
    printf("=== bootlegger: verified handshakes and AEAD sessions ===\n");
    bootlegger_role_t role;
    CK(bootlegger_peer_id_of(PK) == 0x25dc6af5u, "peer_id is SHA3-256(pubkey)[0..4] BE");

    good_hs(&hs);
    bad = hs;
    bad.signature[10] ^= 1;
    CK(bootlegger_handshake_recv(&A, &bad) == BOOTLEGGER_EAUTH && !A.authenticated,
       "bad signature rejected");
    bad = hs;
    bad.phase = 8;
    CK(bootlegger_handshake_recv(&A, &bad) == BOOTLEGGER_EAUTH, "edited signed field rejected");
    bad = hs;
    bad.peer_id ^= 1;
    CK(bootlegger_handshake_recv(&A, &bad) == BOOTLEGGER_EAUTH, "wrong peer_id rejected");
    bad = hs;
    bad.magic[0] = 'X';
    CK(bootlegger_handshake_recv(&A, &bad) == BOOTLEGGER_EAUTH, "wrong magic rejected");

    uint8_t other[32];
    memset(other, 9, 32);
    bootlegger_pin_peer(&C, other);
    CK(bootlegger_handshake_recv(&C, &hs) == BOOTLEGGER_EAUTH, "pinned key mismatch rejected");

    CK(bootlegger_handshake_recv(&A, &hs) == 0 && A.authenticated && A.peer_id == hs.peer_id,
       "genuine handshake accepted");
    CK(bootlegger_authenticate(&A, &role) == 0, "authenticate re-verifies stored handshake");

    /* Setting the byte by hand is not authentication. */
    memset(&C, 0, sizeof C);
    C.authenticated = 1;
    CK(bootlegger_authenticate(&C, &role) == BOOTLEGGER_EAUTH && !C.authenticated,
       "forged authenticated flag rejected");
    CK(bootlegger_handshake_send(&A) == BOOTLEGGER_ENOTIMPL, "handshake_send: no signer, ENOTIMPL");

    /* Session: both sides authenticated (B also verified the same peer). */
    memset(&C, 0, sizeof C);
    uint8_t coins[32];
    memset(coins, 0x42, 32);
    CK(bootlegger_kem_initiate(&C, ek, coins, ct) == BOOTLEGGER_EAUTH,
       "no session for an unauthenticated peer");
    CK(bootlegger_handshake_recv(&B, &hs) == 0, "responder side verified");
    uint8_t d[32], z[32];
    memset(d, 1, 32);
    memset(z, 2, 32);
    mlkem768_keygen(d, z, ek, dk);
    CK(bootlegger_kem_initiate(&A, ek, coins, ct) == 0 && bootlegger_kem_accept(&B, dk, ct) == 0,
       "ML-KEM-768 session established");
    CK(memcmp(A.tx_key, B.rx_key, 32) == 0 && memcmp(A.rx_key, B.tx_key, 32) == 0 &&
           memcmp(A.tx_key, A.rx_key, 32) != 0,
       "per-direction keys agree");

    const char *msg = "meet at the usual place";
    uint16_t ml = (uint16_t) strlen(msg);
    int n = bootlegger_seal_msg(&A, BOOTLEGGER_MSG_CHAT, msg, ml, wire, sizeof wire);
    CK(n == BOOTLEGGER_MSG_HDR + ml + BOOTLEGGER_TAG_SIZE &&
           memcmp(wire + BOOTLEGGER_MSG_HDR, msg, ml) != 0,
       "sealed message is not plaintext");
    bootlegger_msg_type_t ty;
    uint8_t w2[600];
    memcpy(w2, wire, (size_t) n);
    w2[5] ^= 1;
    CK(bootlegger_open_msg(&B, w2, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "tampered ciphertext rejected");
    memcpy(w2, wire, (size_t) n);
    w2[0] = BOOTLEGGER_MSG_PRIVATE;
    CK(bootlegger_open_msg(&B, w2, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "tampered header (AAD) rejected");
    int r = bootlegger_open_msg(&B, wire, (uint32_t) n, &ty, pt, sizeof pt);
    CK(r == ml && ty == BOOTLEGGER_MSG_CHAT && memcmp(pt, msg, ml) == 0, "genuine message opens");
    CK(bootlegger_open_msg(&B, wire, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "replay rejected (sequence nonce)");
    CK(bootlegger_open_msg(&A, wire, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "reflection to sender rejected (direction keys)");

    /* Fail-closed stubs. */
    memset(&C, 0, sizeof C);
    C.connected = 1;
    CK(bootlegger_send_msg(&C, BOOTLEGGER_MSG_CHAT, msg, ml) == BOOTLEGGER_ENOKEY,
       "send without session: ENOKEY");
    CK(bootlegger_send_msg(&A, BOOTLEGGER_MSG_CHAT, msg, ml) == BOOTLEGGER_ENOTIMPL,
       "send with session: no transport, ENOTIMPL");
    uint16_t gl = 4;
    CK(bootlegger_garlic_wrap(&A, wire, &gl) == BOOTLEGGER_ENOTIMPL, "garlic_wrap: ENOTIMPL");

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
