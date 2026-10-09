/* test_pungent.c — PungentClove: cloves and hop layers must really be encrypted.
 *
 * Regression for the old module, which derived every clove "session key" from
 * the PUBLIC destination hash (so anyone could compute it) and then set
 * encrypted = true without encrypting anything, and whose bulb_unseal returned
 * success for any hop without decrypting. Each check below fails on that code.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "pungent.h"
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

static void fill(uint8_t *p, uint32_t n, uint8_t seed)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (seed + i * 7u);
}

static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES], ct[MLKEM768_CT_BYTES];
static pc_bulb_t bulb;
static pungent_t pc;

int main(void)
{
    printf("=== pungent: real clove and layer encryption ===\n");
    const char *msg = "meet at the third lantern";
    uint32_t mlen = (uint32_t) strlen(msg);
    uint8_t dest[PC_HASH_SIZE], nonce[PC_NONCE_SIZE], out[PC_CLOVE_MAX_PAYLOAD];
    fill(dest, sizeof dest, 3);
    fill(nonce, sizeof nonce, 9);

    /* ML-KEM-768 clove key: both ends agree, and it is not a function of
     * dest_hash alone (different coins give a different key). */
    uint8_t d[32], z[32], coins[32], coins2[32];
    fill(d, 32, 1);
    fill(z, 32, 2);
    fill(coins, 32, 5);
    fill(coins2, 32, 6);
    mlkem768_keygen(d, z, ek, dk);
    uint8_t k_send[32], k_recv[32], k_other[32], k_wrong[32];
    pungent_clove_key_encaps(ek, coins, dest, ct, k_send);
    pungent_clove_key_decaps(dk, ct, dest, k_recv);
    CK(memcmp(k_send, k_recv, 32) == 0, "ML-KEM clove key: sender and destination agree");
    pungent_clove_key_encaps(ek, coins2, dest, ct, k_other);
    CK(memcmp(k_send, k_other, 32) != 0, "same dest_hash, fresh coins -> different key");
    /* The old derivation: key = H(dest_hash). Whatever H is, a key that only
     * depends on dest_hash must open the clove; ours must not. */
    sha3_256(dest, PC_HASH_SIZE, k_wrong);

    pungent_bulb_init(&bulb, 3, 0);
    int ci = pungent_bulb_add_clove(&bulb, (const uint8_t *) msg, mlen, dest, PC_TIER_GARLIC,
                                    k_send, nonce);
    CK(ci == 0, "clove added");
    CK(bulb.cloves[0].encrypted, "clove marked encrypted");
    CK(memcmp(bulb.cloves[0].payload, msg, mlen) != 0, "clove payload is not plaintext");
    int n = pungent_clove_open(&bulb.cloves[0], k_recv, out, sizeof out);
    CK(n == (int) mlen && memcmp(out, msg, mlen) == 0, "destination opens the clove");
    CK(pungent_clove_open(&bulb.cloves[0], k_wrong, out, sizeof out) < 0,
       "key derived from public dest_hash cannot open it");
    pc_clove_t t = bulb.cloves[0];
    t.payload[2] ^= 1;
    CK(pungent_clove_open(&t, k_recv, out, sizeof out) < 0, "tampered ciphertext rejected");
    t = bulb.cloves[0];
    t.dest_hash[0] ^= 1;
    CK(pungent_clove_open(&t, k_recv, out, sizeof out) < 0, "tampered dest_hash (AAD) rejected");

    /* Hop layers. */
    uint8_t hop_keys[3][PC_SESSION_KEY_SIZE], nb[PC_NONCE_SIZE], bad[32];
    fill(hop_keys[0], 32, 40);
    fill(hop_keys[1], 32, 50);
    fill(hop_keys[2], 32, 60);
    fill(nb, sizeof nb, 77);
    fill(bad, 32, 99);
    pc_bulb_t saved;
    CK(pungent_bulb_seal(&bulb, (const uint8_t(*)[32]) hop_keys, nb) == 0, "bulb sealed");
    CK(bulb.sealed && bulb.wire_len > mlen + 3 * PC_TAG_SIZE, "three tags on the wire");
    saved = bulb;
    CK(pungent_bulb_unseal(&bulb, 0, bad) < 0 && !bulb.sealed, "wrong hop key: refused, discarded");
    bulb = saved;
    CK(pungent_bulb_unseal(&bulb, 1, hop_keys[1]) < 0, "out-of-order unseal refused");
    bulb = saved;
    bulb.wire[10] ^= 0x80;
    CK(pungent_bulb_unseal(&bulb, 0, hop_keys[0]) < 0, "tampered wire refused");
    bulb = saved;
    int ok = pungent_bulb_unseal(&bulb, 0, hop_keys[0]) == 0 &&
             pungent_bulb_unseal(&bulb, 1, hop_keys[1]) == 0 &&
             pungent_bulb_unseal(&bulb, 2, hop_keys[2]) == 0;
    CK(ok && !bulb.sealed && bulb.num_cloves == 1, "all three hops peel in order");
    n = pungent_clove_open(&bulb.cloves[0], k_recv, out, sizeof out);
    CK(n == (int) mlen && memcmp(out, msg, mlen) == 0, "clove survives the round trip");

    pc_bulb_t empty;
    pungent_bulb_init(&empty, 0, 0);
    pungent_bulb_add_clove(&empty, (const uint8_t *) msg, mlen, dest, 1, k_send, nonce);
    CK(pungent_bulb_seal(&empty, (const uint8_t(*)[32]) hop_keys, nb) < 0,
       "zero-hop bulb is not 'sealed'");

    /* Tunnels and relays. */
    pungent_init(&pc);
    uint8_t pkh[PC_HASH_SIZE];
    fill(pkh, sizeof pkh, 11);
    CK(pungent_relay_add(&pc, "r0", PC_TIER_GARLIC, 0) < 0, "relay needs a real pubkey hash");
    for (int i = 0; i < 4; i++) {
        char name[4] = {'r', (char) ('0' + i), 0, 0};
        pkh[0] = (uint8_t) i;
        pungent_relay_add(&pc, name, PC_TIER_GARLIC, pkh);
        pungent_relay_set_online(&pc, (uint32_t) i, true);
    }
    uint32_t path[3];
    CK(pungent_select_path(&pc, PC_TIER_GARLIC, 3, path) == 3 && path[0] != path[1] &&
           path[1] != path[2] && path[0] != path[2],
       "path has three distinct relays");
    int32_t tu = pungent_tunnel_create(&pc, PC_TUNNEL_OUTBOUND, PC_TIER_GARLIC, 3);
    pungent_tunnel_add_hop(&pc, (uint32_t) tu, path[0], hop_keys[0]);
    pungent_tunnel_add_hop(&pc, (uint32_t) tu, path[1], hop_keys[1]);
    CK(pungent_tunnel_activate(&pc, (uint32_t) tu) < 0, "tunnel missing a hop key stays down");
    pungent_tunnel_add_hop(&pc, (uint32_t) tu, path[2], hop_keys[2]);
    CK(pungent_tunnel_activate(&pc, (uint32_t) tu) == 0, "tunnel with all hop keys activates");
    uint8_t buf[256];
    int w =
        pungent_tunnel_send(&pc, (uint32_t) tu, (const uint8_t *) msg, mlen, nb, buf, sizeof buf);
    CK(w == (int) (mlen + 3 * PC_TAG_SIZE), "tunnel output carries one tag per hop");
    int32_t L = w;
    for (uint32_t h = 0; h < 3 && L >= 0; h++)
        L = pungent_layer_peel(hop_keys[h], nb, h, buf, (uint32_t) L);
    CK(L == (int32_t) mlen && memcmp(buf, msg, mlen) == 0, "each relay peels its own layer");

    /* Hidden service address is a hash of a real public key. */
    int32_t s = pungent_hidden_service_create(&pc, "svc", 80, (uint32_t) tu, ek, sizeof ek);
    uint8_t h[32];
    sha3_256(ek, sizeof ek, h);
    CK(s == 0 && memcmp(pc.services[0].service_key, h, 32) == 0, "service key = SHA3-256(pk)");
    CK(pungent_hidden_service_find(&pc, h) != 0, "service found by its address");

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
