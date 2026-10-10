/* test_bootlegger.c — Bootlegger v2: a handshake that binds identities,
 * ephemeral keys and fresh nonces, and sessions that are real AEAD.
 *
 * Regression for the audit's HIGH item: the v1 handshake signed only static
 * fields (replayable) and the ML-KEM key was not bound to the identity (man in
 * the middle). Every attack below succeeds against a protocol that omits a
 * nonce, an ephemeral key or the version from the signed transcript.
 * Identities are ML-DSA-65 keys derived at run time from fixed public test
 * seeds: no private key is stored in the tree.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "bootlegger.h"

static int fails = 0, checks = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (c)                                                                                     \
            printf("  ok   %s\n", m);                                                              \
        else {                                                                                     \
            printf("  FAIL %s\n", m);                                                              \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static bootlegger_identity_t IA, IB, IM; /* Alice, Bob, Mallory */
static bootlegger_replay_cache_t RA, RB, RM;
static bootlegger_hs_t HA, HB, HM;
static bootlegger_conn_t CA, CB, CM;
static bootlegger_hello_t hello, hello2;
static bootlegger_reply_t reply, reply2;
static bootlegger_finish_t fin, fin2;
static uint8_t wire[600], pt[600];

static uint32_t g_ctr = 1;
static const uint8_t *fresh(void)
{
    static uint8_t r[32];
    memset(r, 0, sizeof r);
    r[0] = (uint8_t) g_ctr;
    r[1] = (uint8_t) (g_ctr >> 8);
    r[2] = 0xEE;
    g_ctr++;
    return r;
}

static void reset_all(void)
{
    memset(&CA, 0, sizeof CA);
    memset(&CB, 0, sizeof CB);
    memset(&CM, 0, sizeof CM);
    memset(&HA, 0, sizeof HA);
    memset(&HB, 0, sizeof HB);
    memset(&HM, 0, sizeof HM);
}

/* A full honest run; returns 0 if every step succeeded. */
static int run_honest(void)
{
    reset_all();
    if (bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello)) return 1;
    if (bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply)) return 2;
    if (bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin)) return 3;
    if (bootlegger_hs_accept(&HB, &CB, &fin)) return 4;
    return 0;
}

int main(void)
{
    printf("=== bootlegger v2: signed transcript handshake and AEAD sessions ===\n");
    uint8_t seed[32];
    memset(seed, 0xA1, 32);
    bootlegger_identity_init(&IA, seed);
    memset(seed, 0xB2, 32);
    bootlegger_identity_init(&IB, seed);
    memset(seed, 0x3E, 32);
    bootlegger_identity_init(&IM, seed);
    bootlegger_replay_init(&RA);
    bootlegger_replay_init(&RB);
    bootlegger_replay_init(&RM);
    bootlegger_role_t role;

    /* ---- 1. a successful handshake ---- */
    CK(run_honest() == 0, "honest handshake: HELLO, REPLY, FINISH, accept all succeed");
    CK(CA.authenticated && CB.authenticated && CA.has_session && CB.has_session,
       "both sides authenticated with a session");
    CK(CA.peer_id == bootlegger_peer_id_of(IB.pk) && CB.peer_id == bootlegger_peer_id_of(IA.pk),
       "each side knows the other's identity");
    CK(memcmp(CA.tx_key, CB.rx_key, 32) == 0 && memcmp(CA.rx_key, CB.tx_key, 32) == 0 &&
           memcmp(CA.tx_key, CA.rx_key, 32) != 0,
       "per-direction keys agree");
    CK(memcmp(CA.transcript_hash, CB.transcript_hash, 32) == 0, "both hold the same H(T)");
    {
        uint8_t t[32];
        bootlegger_transcript_hash(BOOTLEGGER_VERSION, IA.pk, hello.ek, reply.hello.nonce, IB.pk,
                                   reply.hello.ek, hello.nonce, t);
        CK(memcmp(t, CA.transcript_hash, 32) == 0,
           "H(T) = Identity_A||EphKey_A||Nonce_B||Identity_B||EphKey_B||Nonce_A");
    }
    CK(bootlegger_authenticate(&CA, &role) == 0 && bootlegger_authenticate(&CB, &role) == 0,
       "authenticate re-derives the binding tag");
    {
        uint8_t ka[32];
        memcpy(ka, CA.tx_key, 32);
        CK(run_honest() == 0 && memcmp(ka, CA.tx_key, 32) != 0,
           "a second handshake (fresh nonces and ephemeral keys) gives new keys");
    }

    /* AEAD over the session */
    const char *msg = "meet at the usual place";
    uint16_t ml = (uint16_t) strlen(msg);
    int n = bootlegger_seal_msg(&CA, BOOTLEGGER_MSG_CHAT, msg, ml, wire, sizeof wire);
    CK(n == BOOTLEGGER_MSG_HDR + ml + BOOTLEGGER_TAG_SIZE &&
           memcmp(wire + BOOTLEGGER_MSG_HDR, msg, ml) != 0,
       "sealed message is not plaintext");
    bootlegger_msg_type_t ty;
    uint8_t w2[600];
    memcpy(w2, wire, (size_t) n);
    w2[5] ^= 1;
    CK(bootlegger_open_msg(&CB, w2, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "tampered ciphertext rejected");
    memcpy(w2, wire, (size_t) n);
    w2[0] = BOOTLEGGER_MSG_PRIVATE;
    CK(bootlegger_open_msg(&CB, w2, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "tampered header (AAD) rejected");
    int r = bootlegger_open_msg(&CB, wire, (uint32_t) n, &ty, pt, sizeof pt);
    CK(r == ml && ty == BOOTLEGGER_MSG_CHAT && memcmp(pt, msg, ml) == 0, "genuine message opens");
    CK(bootlegger_open_msg(&CB, wire, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "message replay rejected (sequence nonce)");
    CK(bootlegger_open_msg(&CA, wire, (uint32_t) n, &ty, pt, sizeof pt) == BOOTLEGGER_EAUTH,
       "reflection to sender rejected (direction keys)");

    /* ---- 2. replay ---- */
    reset_all();
    CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0 &&
           bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply) == 0,
       "replay setup: HELLO and REPLY");
    CK(bootlegger_hs_respond(&HM, &CM, &IB, &RB, &hello, fresh(), &reply2) == BOOTLEGGER_EREPLAY &&
           !CM.authenticated,
       "a replayed HELLO (nonce already seen) is REJECTED");
    CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin) == 0 &&
           bootlegger_hs_accept(&HB, &CB, &fin) == 0,
       "the original session still completes");
    {
        /* replay the old REPLY into a NEW initiator session */
        bootlegger_hs_t h;
        bootlegger_conn_t c;
        memset(&c, 0, sizeof c);
        CK(bootlegger_hs_initiate(&h, &IA, &RA, fresh(), &hello2) == 0 &&
               bootlegger_hs_finish(&h, &c, &IA, &RA, &reply, fresh(), &fin2) ==
                   BOOTLEGGER_EREPLAY &&
               !c.authenticated,
           "a replayed REPLY is REJECTED (its nonce is in the cache)");
        bootlegger_replay_cache_t empty;
        bootlegger_replay_init(&empty);
        CK(bootlegger_hs_initiate(&h, &IA, &empty, fresh(), &hello2) == 0 &&
               bootlegger_hs_finish(&h, &c, &IA, &empty, &reply, fresh(), &fin2) ==
                   BOOTLEGGER_EAUTH &&
               !c.authenticated,
           "...and even after cache eviction: sig_B covers the old Nonce_A and EphKey_A");
        /* replay the old FINISH into a NEW responder session */
        bootlegger_hs_t hb;
        bootlegger_conn_t cb;
        memset(&cb, 0, sizeof cb);
        bootlegger_replay_cache_t e2;
        bootlegger_replay_init(&e2);
        CK(bootlegger_hs_respond(&hb, &cb, &IB, &e2, &hello, fresh(), &reply2) == 0 &&
               bootlegger_hs_accept(&hb, &cb, &fin) == BOOTLEGGER_EAUTH && !cb.authenticated,
           "a replayed FINISH is REJECTED (sig_A covers the old Nonce_B)");
    }

    /* ---- 3. man in the middle ---- */
    {
        /* Mallory swaps Alice's ephemeral key in HELLO for her own */
        bootlegger_hs_t hm;
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0, "MITM setup: HELLO");
        CK(bootlegger_hs_initiate(&hm, &IM, &RM, fresh(), &hello2) == 0, "Mallory's own key");
        bootlegger_hello_t forged = hello;
        memcpy(forged.ek, hello2.ek, MLKEM768_EK_BYTES);
        CK(bootlegger_hs_respond(&HB, &CB, &IB, &RB, &forged, fresh(), &reply) == 0,
           "Bob answers the forged HELLO (it is unsigned at this point)");
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin) == BOOTLEGGER_EAUTH &&
               !CA.authenticated && !CA.has_session,
           "Alice REJECTS: Bob signed Mallory's ephemeral key, not hers");

        /* Mallory swaps Bob's ephemeral key (and ciphertext) in REPLY */
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0 &&
               bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply) == 0,
           "MITM setup 2");
        reply2 = reply;
        memcpy(reply2.hello.ek, hello2.ek, MLKEM768_EK_BYTES);
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply2, fresh(), &fin) == BOOTLEGGER_EAUTH &&
               !CA.authenticated,
           "a REPLY with a substituted ephemeral key is REJECTED");

        /* Mallory answers as herself, signing correctly with her OWN identity,
         * to an Alice who pinned Bob */
        reset_all();
        bootlegger_pin_peer(&CA, IB.pk);
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0 &&
               bootlegger_hs_respond(&HM, &CM, &IM, &RM, &hello, fresh(), &reply) == 0,
           "Mallory produces a correctly self-signed REPLY");
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin) == BOOTLEGGER_EAUTH &&
               !CA.authenticated,
           "pinned to Bob, Alice REJECTS Mallory's identity");

        /* Mallory claims Bob's identity but signs with her key */
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0 &&
               bootlegger_hs_respond(&HM, &CM, &IM, &RM, &hello, fresh(), &reply) == 0,
           "MITM setup 3");
        memcpy(reply.hello.identity, IB.pk, BOOTLEGGER_ID_PK_BYTES);
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin) == BOOTLEGGER_EAUTH,
           "a REPLY claiming Bob's identity, signed by another key, is REJECTED");

        /* Mallory swaps the KEM ciphertext in FINISH */
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0 &&
               bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply) == 0 &&
               bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin) == 0,
           "MITM setup 4");
        fin2 = fin;
        fin2.ct[7] ^= 0x40;
        CK(bootlegger_hs_accept(&HB, &CB, &fin2) == BOOTLEGGER_EAUTH && !CB.authenticated,
           "a FINISH with a swapped ciphertext fails key confirmation");

        /* reflection: Bob receives his own identity */
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IB, &RA, fresh(), &hello) == 0 &&
               bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply) ==
                   BOOTLEGGER_EAUTH,
           "a HELLO carrying our own identity (reflection) is REJECTED");
    }

    /* ---- 4. downgrade ---- */
    {
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0, "downgrade setup");
        hello2 = hello;
        hello2.version = 1;
        CK(bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello2, fresh(), &reply) ==
               BOOTLEGGER_EVERSION,
           "a HELLO offering version 1 is REJECTED (no downgrade)");
        hello2.version = 3;
        CK(bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello2, fresh(), &reply) ==
               BOOTLEGGER_EVERSION,
           "an unknown version is REJECTED");
        CK(bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply) == 0,
           "the genuine v2 HELLO is answered");
        reply2 = reply;
        reply2.hello.version = 1;
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply2, fresh(), &fin) == BOOTLEGGER_EVERSION,
           "a REPLY downgraded to version 1 is REJECTED");
        {
            /* the version is inside the signed transcript */
            uint8_t t1[32], t2[32];
            bootlegger_transcript_hash(1, IA.pk, hello.ek, reply.hello.nonce, IB.pk, reply.hello.ek,
                                       hello.nonce, t1);
            bootlegger_transcript_hash(2, IA.pk, hello.ek, reply.hello.nonce, IB.pk, reply.hello.ek,
                                       hello.nonce, t2);
            CK(memcmp(t1, t2, 32) != 0, "the protocol version is part of H(T)");
        }
    }

    /* ---- 5. tampered transcript / missing pieces ---- */
    {
        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0 &&
               bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello, fresh(), &reply) == 0,
           "tamper setup");
        bootlegger_hs_t ha_saved = HA;
        reply2 = reply;
        reply2.hello.nonce[3] ^= 1;
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply2, fresh(), &fin) == BOOTLEGGER_EAUTH,
           "a REPLY with an edited Nonce_B is REJECTED");
        HA = ha_saved;
        reply2 = reply;
        reply2.sig[100] ^= 1;
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply2, fresh(), &fin) == BOOTLEGGER_EAUTH,
           "a REPLY with an edited signature is REJECTED");
        HA = ha_saved;
        reply2 = reply;
        memset(reply2.sig, 0, sizeof reply2.sig);
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply2, fresh(), &fin) == BOOTLEGGER_EAUTH,
           "an UNSIGNED REPLY is REJECTED");
        HA = ha_saved;
        reply2 = reply;
        memset(reply2.hello.nonce, 0, BOOTLEGGER_NONCE_BYTES);
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply2, fresh(), &fin) == BOOTLEGGER_ENONCE,
           "a REPLY with a zero nonce is REJECTED");
        HA = ha_saved;
        CK(bootlegger_hs_finish(&HA, &CA, &IA, &RA, &reply, fresh(), &fin) == 0,
           "the untouched REPLY is accepted");
        bootlegger_hs_t hb_saved = HB;
        fin2 = fin;
        fin2.sig[50] ^= 1;
        CK(bootlegger_hs_accept(&HB, &CB, &fin2) == BOOTLEGGER_EAUTH,
           "a FINISH with an edited signature is REJECTED");
        HB = hb_saved;
        fin2 = fin;
        fin2.confirm[0] ^= 1;
        CK(bootlegger_hs_accept(&HB, &CB, &fin2) == BOOTLEGGER_EAUTH,
           "a FINISH with an edited confirmation is REJECTED");
        HB = hb_saved;
        CK(bootlegger_hs_accept(&HB, &CB, &fin) == 0, "the untouched FINISH is accepted");

        reset_all();
        CK(bootlegger_hs_initiate(&HA, &IA, &RA, fresh(), &hello) == 0, "zero-nonce setup");
        hello2 = hello;
        memset(hello2.nonce, 0, BOOTLEGGER_NONCE_BYTES);
        CK(bootlegger_hs_respond(&HB, &CB, &IB, &RB, &hello2, fresh(), &reply) == BOOTLEGGER_ENONCE,
           "a HELLO with a missing (zero) nonce is REJECTED");
        CK(bootlegger_hs_accept(&HM, &CM, &fin) == BOOTLEGGER_ESTATE,
           "a FINISH with no handshake in progress is REJECTED");
        {
            /* the caller reused its entropy: the nonce repeats and is refused */
            bootlegger_hs_t h1, h2;
            uint8_t rr[32];
            memset(rr, 0x77, 32);
            CK(bootlegger_hs_initiate(&h1, &IA, &RA, rr, &hello) == 0 &&
                   bootlegger_hs_initiate(&h2, &IA, &RA, rr, &hello2) == BOOTLEGGER_EREPLAY,
               "reused entropy (a repeated own nonce) is refused");
        }
    }

    /* ---- 6. the replay cache is bounded ---- */
    {
        bootlegger_replay_cache_t rc;
        bootlegger_replay_init(&rc);
        bootlegger_hs_t h;
        for (int i = 0; i < BOOTLEGGER_REPLAY_SLOTS + 10; i++)
            (void) bootlegger_hs_initiate(&h, &IA, &rc, fresh(), &hello);
        CK(rc.count == BOOTLEGGER_REPLAY_SLOTS && rc.next < BOOTLEGGER_REPLAY_SLOTS,
           "the replay cache holds at most BOOTLEGGER_REPLAY_SLOTS nonces");
    }

    /* ---- 7. forged state and fail-closed stubs ---- */
    bootlegger_conn_t F;
    memset(&F, 0, sizeof F);
    F.authenticated = 1;
    CK(bootlegger_authenticate(&F, &role) == BOOTLEGGER_EAUTH && !F.authenticated,
       "forged authenticated flag rejected");
    CK(run_honest() == 0, "fresh honest session");
    F = CA;
    F.peer_id_hash[0] ^= 1;
    CK(bootlegger_authenticate(&F, &role) == BOOTLEGGER_EAUTH,
       "a connection whose peer identity was edited is rejected");
    memset(&F, 0, sizeof F);
    F.connected = 1;
    CK(bootlegger_send_msg(&F, BOOTLEGGER_MSG_CHAT, msg, ml) == BOOTLEGGER_ENOKEY,
       "send without session: ENOKEY");
    CK(run_honest() == 0 &&
           bootlegger_send_msg(&CA, BOOTLEGGER_MSG_CHAT, msg, ml) == BOOTLEGGER_ENOTIMPL,
       "send with session: no transport, ENOTIMPL");
    uint16_t gl = 4;
    CK(bootlegger_garlic_wrap(&CA, wire, &gl) == BOOTLEGGER_ENOTIMPL, "garlic_wrap: ENOTIMPL");

    printf("=== %s (%d of %d check(s) failed) ===\n", fails ? "FAILED" : "ALL PASSED", fails,
           checks);
    return fails ? 1 : 0;
}
