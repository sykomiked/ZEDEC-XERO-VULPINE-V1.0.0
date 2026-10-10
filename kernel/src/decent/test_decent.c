/* test_decent.c — decent: hashes must be cryptographic and content-checked,
 * rooms must really encrypt, and unbuilt capabilities must fail closed.
 *
 * Regression for the old module: FNV-1a stood in for every hash, a peer or DID
 * "public key" was a hash of its own label, an "E2EE" room key was a hash of
 * the public room id, bitswap marked a CID resolved without seeing a byte, and
 * the radio path reported packets sent while sending nothing.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "decent.h"
#include "../robin_debanks/sha256.h"
#include "../mlkem/keccak.h"

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

static decent_t d;

/* SHA-256("abc"), FIPS 180-2 Appendix B.1: an external anchor. */
static const uint8_t SHA256_ABC[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

int main(void)
{
    printf("=== decent: real hashes, real room encryption, fail-closed stubs ===\n");
    decent_init(&d);

    const uint8_t pk1[] = "peer-one-public-key-bytes", pk2[] = "peer-two-public-key-bytes";
    CK(decent_ipfs_node_add(&d, "p1", "/ip4/1", 0, 0) < 0, "peer needs a public key");
    int32_t n1 = decent_ipfs_node_add(&d, "same-label", "/ip4/1", pk1, sizeof pk1);
    int32_t n2 = decent_ipfs_node_add(&d, "same-label", "/ip4/2", pk2, sizeof pk2);
    uint8_t h[32];
    sha3_256(pk1, sizeof pk1, h);
    CK(n1 == 0 && memcmp(d.ipfs_nodes[0].pubkey, h, 32) == 0, "peer key hash = SHA3-256(pk)");
    CK(memcmp(d.ipfs_nodes[0].pubkey, d.ipfs_nodes[1].pubkey, 32) != 0,
       "same label, different keys -> different key hashes");

    int32_t c = decent_ipfs_cid_register_content(&d, (const uint8_t *) "abc", 3, 0x55);
    CK(c == 0 && memcmp(d.cids[0].hash, SHA256_ABC, 32) == 0, "content CID is SHA-256 (FIPS KAT)");

    uint8_t want[32];
    sha256((const uint8_t *) "block-data", 10, want);
    int32_t c2 = decent_ipfs_cid_register(&d, want, 0x55, 10, DECENT_CID_V1);
    decent_ipfs_node_connect(&d, (uint32_t) n2);
    CK(decent_ipfs_bitswap(&d, (uint32_t) n2, (uint32_t) c2, (const uint8_t *) "block-dat!", 10) ==
               DECENT_EMISMATCH &&
           !d.cids[c2].resolved,
       "bitswap with wrong bytes: rejected, still unresolved");
    CK(decent_ipfs_bitswap(&d, (uint32_t) n2, (uint32_t) c2, (const uint8_t *) "block-data", 10) ==
               0 &&
           d.cids[c2].resolved,
       "bitswap with matching bytes resolves the CID");

    /* Rooms. */
    uint8_t key[32], wrong[32];
    for (int i = 0; i < 32; i++) {
        key[i] = (uint8_t) (i * 13 + 1);
        wrong[i] = (uint8_t) (i * 13 + 2);
    }
    CK(decent_matrix_room_create(&d, "!r:z", "R", true, 0) < 0, "encrypted room needs a key");
    int32_t r = decent_matrix_room_create(&d, "!r:z", "R", true, key);
    const char *body = "the plan stays in the room";
    CK(decent_matrix_room_send_event(&d, (uint32_t) r, DECENT_MATRIX_EVENT_MESSAGE, "alice",
                                     body) == 0,
       "event sent");
    decent_matrix_event_t *e = &d.matrix_rooms[r].events[0];
    CK(e->encrypted && memcmp(e->content, body, strlen(body)) != 0, "event body is ciphertext");
    char out[300];
    CK(decent_matrix_event_open(&d, (uint32_t) r, 0, key, out, sizeof out) == (int) strlen(body) &&
           strcmp(out, body) == 0,
       "room key opens the event");
    sha3_256((const uint8_t *) "!r:z", 4, h);
    CK(decent_matrix_event_open(&d, (uint32_t) r, 0, h, out, sizeof out) < 0,
       "hash of the public room id does not open it");
    CK(decent_matrix_event_open(&d, (uint32_t) r, 0, wrong, out, sizeof out) < 0,
       "wrong key rejected");
    e->content[0] ^= 1;
    CK(decent_matrix_event_open(&d, (uint32_t) r, 0, key, out, sizeof out) < 0,
       "tampered event rejected");

    /* DIDs. */
    CK(decent_did_create(&d, "x", 1, 0, 0) < 0, "DID needs a public key");
    int32_t a = decent_did_create(&d, "alice", 1, pk1, sizeof pk1);
    int32_t b = decent_did_create(&d, "alice", 1, pk2, sizeof pk2);
    CK(a >= 0 && b >= 0 && strcmp(d.dids[a].did, d.dids[b].did) != 0,
       "two keys never share a DID string");
    CK(decent_did_zk_verify(&d, (uint32_t) a) == DECENT_ENOTIMPL && !d.dids[a].zk_verified,
       "zk verify fails closed");

    /* Radio. */
    int32_t ch = decent_sdr_channel_create(&d, "433", 433920000, 125000, 1);
    decent_sdr_channel_activate(&d, (uint32_t) ch);
    CK(decent_sdr_send_bulb(&d, (uint32_t) ch, (const uint8_t *) "x", 1) == DECENT_ENOTIMPL &&
           d.sdr_channels[ch].packets_sent == 0 && !d.sdr_channels[ch].garlic_encrypted,
       "radio send fails closed and claims no encryption");

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
