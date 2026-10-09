/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_web4_agent.c — Web 4 agents and the Web 2 / Web 3 bridge.
 *
 *   identity      self-bound and node-bound cards, tampering, directory rollback
 *   manifest      sign / verify / encode / decode / JSON face, tampering
 *   envelopes     the M2 pipeline: replay (in order, out of order, window),
 *                 stale timestamps, forged signatures that must not poison the
 *                 replay window, addressing, unknown peers and tools, input
 *                 limits, the rate limit and its fail-closed eviction, the
 *                 replay eviction floor
 *   consent       money and personal-data tools: missing, wrong action, wrong
 *                 scope, ceiling, expiry, unknown human, single use, full log
 *   carriage      freight frames with up to 168 - k rows lost per freight,
 *                 too few rows, a corrupted row; the offline outbox
 *   link          all three signatures verified; each leg's failure bit
 *   payments      VFV rail (ledger hook), EVM rail (ERC-20 and native), consent,
 *                 routing when offline, exact amount conversion
 *   content map   URL -> CID records
 *   policy        every routing row at every reach
 * plus mutation fuzzing of the envelope, manifest and freight decoders.
 *
 * ML-DSA-65 itself is tested against NIST vectors in kernel/src/pqsec; the
 * Ethereum and JWS primitives against published vectors in test_web4_web3.c
 * and test_web4_web2.c. This file tests the protocol built on them. */
#include <stdio.h>
#include <string.h>
#include "../pqsec/pq_security.h" /* prototype drift vs web4_agent.h = compile error */
#include "web4_bridge.h"

static int failures = 0, passes = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)

#define T0     1760000000000ull /* ms */
#define WINDOW 30000ull

static uint64_t rng_s = 0x9e3779b97f4a7c15ull;
static uint32_t rng(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 7;
    rng_s ^= rng_s << 17;
    return (uint32_t) (rng_s >> 16);
}

static void seed_of(uint8_t s[32], uint8_t tag)
{
    for (int i = 0; i < 32; i++) s[i] = (uint8_t) (tag * 31u + (uint32_t) i);
}

/* ---- actors ---- */
static w4_agent_t alice, bob, carol;
static uint8_t node_pk[W4_PK_LEN], node_sk[W4_SK_LEN];
static uint8_t human_pk[W4_PK_LEN], human_sk[W4_SK_LEN], human_id[W4_ID_LEN];
static uint8_t human2_pk[W4_PK_LEN], human2_sk[W4_SK_LEN];

static const uint8_t *human_lookup(void *ctx, const uint8_t id[W4_ID_LEN])
{
    (void) ctx;
    return memcmp(id, human_id, W4_ID_LEN) == 0 ? human_pk : NULL;
}

/* ---- bob's receiving runtime ---- */
static w4_card_t bob_dir_store[4], alice_dir_store[4];
static w4_dir_t bob_dir, alice_dir;
static w4_manifest_t bob_mf, mf2;
static w4_replay_ent_t replay[8];
static w4_rate_ent_t rate[16];
static w4_consent_used_t used[16];
static w4_consent_log_t cons_log;
static w4_rt_t rt;

static uint8_t buf[W4_ENV_MAX], buf2[W4_ENV_MAX];
static w4_env_t env, got;

static int32_t seal_req(w4_agent_t *from, const w4_agent_t *to, const char *tool, uint64_t req_id,
                        const char *payload, uint64_t now, uint8_t *out)
{
    if (w4_env_make(&env, W4_MSG_REQUEST, from->card.agent_id, to->card.agent_id, req_id, 0, tool,
                    (const uint8_t *) payload, (uint32_t) strlen(payload)))
        return -100;
    return w4_env_seal(from, &env, now, NULL, out, W4_ENV_MAX);
}

/* Seal a request that carries a consent token. */
static int32_t seal_req_consent(w4_agent_t *from, const char *tool, uint64_t req_id,
                                const char *payload, const w4_consent_t *c, uint64_t now,
                                uint8_t *out)
{
    if (w4_env_make(&env, W4_MSG_REQUEST, from->card.agent_id, bob.card.agent_id, req_id, 0, tool,
                    (const uint8_t *) payload, (uint32_t) strlen(payload)))
        return -100;
    env.flags = W4_MF_CONSENT;
    env.consent = *c;
    return w4_env_seal(from, &env, now, NULL, out, W4_ENV_MAX);
}

static int consent_for(const char *tool, uint64_t req_id, const char *payload, uint32_t scope,
                       uint64_t max_vfv, uint64_t iss, uint64_t exp, uint8_t nonce_tag,
                       w4_consent_t *out)
{
    uint8_t act[W4_HASH_LEN], nonce[16];
    w4_action_digest(tool, alice.card.agent_id, bob.card.agent_id, req_id,
                     (const uint8_t *) payload, (uint32_t) strlen(payload), act);
    memset(nonce, nonce_tag, sizeof nonce);
    return w4_consent_issue(human_pk, human_sk, alice.card.agent_id, act, scope, max_vfv, iss, exp,
                            nonce, NULL, out);
}

/* ===================================================================== */
static void test_identity(void)
{
    uint8_t s[32];
    seed_of(s, 1);
    CHECK(w4_agent_create(&alice, s, "alice", NULL, NULL, T0, NULL) == W4_OK, "alice self-bound");
    CHECK(alice.card.self_bound && memcmp(alice.card.peer_id, alice.card.agent_id, W4_ID_LEN) == 0,
          "self-bound: peer_id == agent_id");
    uint8_t id[W4_ID_LEN];
    w4_key_id(alice.card.pk, id);
    CHECK(memcmp(id, alice.card.agent_id, W4_ID_LEN) == 0, "agent_id = SHA3-256(pk) (A1)");

    seed_of(s, 2);
    pq_mldsa65_keygen(s, node_pk, node_sk);
    seed_of(s, 3);
    CHECK(w4_agent_create(&bob, s, "bob.agent", node_pk, node_sk, T0, NULL) == W4_OK,
          "bob bound to a node key");
    w4_key_id(node_pk, id);
    CHECK(!bob.card.self_bound && memcmp(bob.card.peer_id, id, W4_ID_LEN) == 0,
          "node-bound: peer_id = SHA3-256(node pk) (A2)");
    seed_of(s, 4);
    CHECK(w4_agent_create(&carol, s, "carol", NULL, NULL, T0, NULL) == W4_OK, "carol");

    CHECK(w4_agent_create(&alice, s, "Bad Name", NULL, NULL, T0, NULL) == W4_ERR_ARG,
          "names are lowercase [a-z0-9._-]");
    CHECK(w4_agent_create(&alice, s, "x", node_pk, NULL, T0, NULL) == W4_ERR_ARG,
          "node pk without node sk refused");
    seed_of(s, 1);
    w4_agent_create(&alice, s, "alice", NULL, NULL, T0, NULL);

    static w4_card_t c;
    c = bob.card;
    c.name[0] = 'c';
    CHECK(w4_card_verify(&c) == W4_ERR_SIG, "renamed card: binding signature fails");
    c = bob.card;
    c.pk[100] ^= 1;
    CHECK(w4_card_verify(&c) == W4_ERR_BINDING, "swapped key: agent_id no longer binds");
    c = bob.card;
    c.node_pk[7] ^= 1;
    CHECK(w4_card_verify(&c) == W4_ERR_BINDING, "swapped node key: peer_id no longer binds");
    c = bob.card;
    c.self_bound = true;
    CHECK(w4_card_verify(&c) != W4_OK, "node-bound card cannot claim self-binding");
    c = alice.card;
    c.bound_ms++;
    CHECK(w4_card_verify(&c) == W4_ERR_SIG, "binding time is signed");

    w4_dir_init(&bob_dir, bob_dir_store, 4);
    w4_dir_init(&alice_dir, alice_dir_store, 4);
    CHECK(w4_dir_add(&bob_dir, &alice.card) == W4_OK && w4_dir_add(&bob_dir, &carol.card) == W4_OK,
          "bob's directory: alice, carol");
    CHECK(w4_dir_add(&alice_dir, &bob.card) == W4_OK, "alice's directory: bob");
    c = bob.card;
    c.pk[0] ^= 1;
    CHECK(w4_dir_add(&alice_dir, &c) != W4_OK, "directory refuses an unverifiable card");
    /* rollback: an older binding for the same agent */
    static w4_agent_t old;
    seed_of(s, 1);
    w4_agent_create(&old, s, "alice", NULL, NULL, T0 - 1000, NULL);
    CHECK(w4_dir_add(&bob_dir, &old.card) == W4_ERR_STALE, "directory refuses an older binding");
    CHECK(w4_dir_find(&bob_dir, alice.card.agent_id) != NULL &&
              w4_dir_find(&bob_dir, bob.card.agent_id) == NULL,
          "directory find");

    seed_of(s, 9);
    pq_mldsa65_keygen(s, human_pk, human_sk);
    w4_key_id(human_pk, human_id);
    seed_of(s, 10);
    pq_mldsa65_keygen(s, human2_pk, human2_sk);
}

static void test_manifest(void)
{
    memset(&bob_mf, 0, sizeof bob_mf);
    bob_mf.version = 3;
    bob_mf.issued_ms = T0;
    bob_mf.expires_ms = T0 + 86400000ull;
    CHECK(w4_manifest_add_tool(&bob_mf, "echo", 0, 6000, 100, 0, 256) == W4_OK &&
              w4_manifest_add_tool(&bob_mf, "pay", 150, 600, 20, W4_TOOL_MONEY, 512) == W4_OK &&
              w4_manifest_add_tool(&bob_mf, "profile", 0, 600, 20, W4_TOOL_PII, 64) == W4_OK &&
              w4_manifest_add_tool(&bob_mf, "quote", 25, 600, 20, 0, 64) == W4_OK &&
              w4_manifest_add_tool(&bob_mf, "live", 0, 600, 20, W4_TOOL_STREAM, 64) == W4_OK &&
              w4_manifest_add_tool(&bob_mf, "slow", 0, 60, 2, 0, 64) == W4_OK,
          "six tools");
    CHECK(w4_manifest_add_tool(&bob_mf, "echo", 0, 1, 1, 0, 1) == W4_ERR_ARG, "duplicate tool");
    CHECK(w4_manifest_add_tool(&bob_mf, "x", 0, 0, 1, 0, 1) == W4_ERR_ARG, "rate 0 refused");
    CHECK(w4_manifest_add_tool(&bob_mf, "x", 0, 1, 1, 0x80, 1) == W4_ERR_ARG, "unknown flag");
    CHECK(w4_manifest_add_tool(&bob_mf, "x", 0, 1, 1, 0, W4_MSG_MAX_PAYLOAD + 1) == W4_ERR_ARG,
          "max_input above the envelope limit");
    CHECK(w4_manifest_sign(&bob, &bob_mf, NULL) == W4_OK, "sign manifest");
    CHECK(w4_manifest_verify(&bob_mf, bob.card.pk, T0 + 1) == W4_OK, "verify manifest");
    CHECK(w4_manifest_verify(&bob_mf, alice.card.pk, T0 + 1) == W4_ERR_BINDING,
          "manifest is bob's, not alice's");
    CHECK(w4_manifest_verify(&bob_mf, bob.card.pk, T0 + 86400000ull) == W4_ERR_EXPIRED,
          "manifest expires");

    static uint8_t enc[W4_MANIFEST_MAX];
    int32_t n = w4_manifest_encode(&bob_mf, enc, sizeof enc);
    CHECK(n > (int32_t) W4_SIG_LEN, "encode manifest");
    CHECK(w4_manifest_decode(enc, (uint32_t) n, &mf2) == W4_OK &&
              w4_manifest_verify(&mf2, bob.card.pk, T0 + 1) == W4_OK && mf2.ntools == 6 &&
              mf2.tools[1].price_vfv == 150 && mf2.version == 3,
          "decode round trip verifies");
    enc[60] ^= 1;
    CHECK(w4_manifest_decode(enc, (uint32_t) n, &mf2) != W4_OK ||
              w4_manifest_verify(&mf2, bob.card.pk, T0 + 1) != W4_OK,
          "tampered manifest does not verify");
    enc[60] ^= 1;
    CHECK(w4_manifest_decode(enc, (uint32_t) n - 1, &mf2) == W4_ERR_PARSE, "truncated manifest");
    uint32_t bad = 0;
    for (int i = 0; i < 3000; i++) {
        static uint8_t m[W4_MANIFEST_MAX];
        memcpy(m, enc, (size_t) n);
        uint32_t len = (uint32_t) n;
        int k = 1 + (int) (rng() % 4);
        for (int j = 0; j < k; j++) m[rng() % 300] ^= (uint8_t) (1u << (rng() % 8));
        if (rng() % 3 == 0) len = rng() % len;
        if (w4_manifest_decode(m, len, &mf2) == W4_OK &&
            w4_manifest_verify(&mf2, bob.card.pk, T0 + 1) == W4_OK)
            bad++;
    }
    CHECK(bad == 0, "manifest fuzz: 3000 mutations never verify");

    static char js[4096];
    int32_t jl = w4_manifest_json(&bob_mf, js, sizeof js);
    static w4_jtok_t tk[256];
    int32_t nt = jl > 0 ? w4_json_parse(js, (uint32_t) jl, tk, 256, 8) : -1;
    CHECK(nt > 0, "manifest JSON face parses");
    int32_t tools = nt > 0 ? w4_json_get(js, tk, nt, 0, "tools") : -1;
    CHECK(tools > 0 && tk[tools].type == W4_J_ARR && tk[tools].size == 6, "JSON lists 6 tools");
    int32_t t1 = tools > 0 ? w4_json_at(tk, nt, tools, 3) : -1;
    int32_t mm = t1 > 0 ? w4_json_get(js, tk, nt, t1, "moves_money") : -1;
    CHECK(mm > 0 && tk[mm].type == W4_J_TRUE, "a priced tool is shown as moving money");
    char alg[16];
    CHECK(w4_json_get_str(js, tk, nt, 0, "signature_alg", alg, sizeof alg) == W4_OK &&
              strcmp(alg, "ML-DSA-65") == 0,
          "JSON names the signature algorithm");

    w4_consent_log_init(&cons_log, used, 16);
    w4_rt_init(&rt, &bob, &bob_mf, &bob_dir, replay, 8, rate, 16, &cons_log, human_lookup, NULL,
               WINDOW);
}

static void test_envelopes(void)
{
    uint64_t now = T0 + 1000;
    int32_t n = seal_req(&alice, &bob, "echo", 1, "hello bob", now, buf);
    CHECK(n > 0, "seal request");
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now + 5, &got) == W4_OK && got.payload_len == 9 &&
              memcmp(got.payload, "hello bob", 9) == 0 && got.seq == 1,
          "bob receives alice's request");
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now + 6, &got) == W4_ERR_REPLAY,
          "same envelope again: replay");

    /* out of order within the window */
    int32_t n2 = seal_req(&alice, &bob, "echo", 2, "two", now, buf2);  /* seq 2 */
    int32_t n3 = seal_req(&alice, &bob, "echo", 3, "three", now, buf); /* seq 3 */
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n3, now + 10, &got) == W4_OK, "seq 3 first");
    CHECK(w4_rt_receive(&rt, buf2, (uint32_t) n2, now + 10, &got) == W4_OK,
          "then seq 2 (out of order, inside the window)");
    CHECK(w4_rt_receive(&rt, buf2, (uint32_t) n2, now + 10, &got) == W4_ERR_REPLAY,
          "seq 2 again: replay");

    /* forged signature does not poison the window */
    n = seal_req(&alice, &bob, "echo", 4, "four", now, buf); /* seq 4 */
    memcpy(buf2, buf, (size_t) n);
    buf2[n - 5] ^= 0x40;
    CHECK(w4_rt_receive(&rt, buf2, (uint32_t) n, now + 20, &got) == W4_ERR_SIG, "forged: SIG");
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now + 20, &got) == W4_OK,
          "the genuine seq 4 is still accepted after the forgery");

    /* signed field tampering */
    n = seal_req(&alice, &bob, "echo", 5, "five", now, buf);
    memcpy(buf2, buf, (size_t) n);
    buf2[n - (int32_t) W4_SIG_LEN - 1] ^= 1; /* last payload byte */
    CHECK(w4_rt_receive(&rt, buf2, (uint32_t) n, now + 20, &got) == W4_ERR_SIG,
          "tampered payload: SIG");

    /* time window */
    n = seal_req(&alice, &bob, "echo", 6, "old", now - WINDOW - 1, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_STALE, "too old: STALE");
    n = seal_req(&alice, &bob, "echo", 7, "future", now + WINDOW + 1, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_STALE,
          "from the future: STALE");

    /* far behind the window */
    {
        static w4_agent_t a2;
        a2 = alice;
        a2.next_seq = 1;
        n = seal_req(&a2, &bob, "echo", 8, "seq1", now, buf);
        CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_REPLAY,
              "an old sequence number re-signed: REPLAY");
    }

    /* addressing */
    n = seal_req(&alice, &carol, "echo", 9, "x", now, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_NOTFOUND,
          "addressed to someone else");
    {
        static w4_agent_t dave;
        uint8_t s[32];
        seed_of(s, 5);
        w4_agent_create(&dave, s, "dave", NULL, NULL, T0, NULL);
        n = seal_req(&dave, &bob, "echo", 9, "x", now, buf);
        CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_NOTFOUND,
              "sender not in the directory");
    }
    n = seal_req(&alice, &bob, "nosuchtool", 10, "x", now, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_NOTFOUND, "unknown tool");
    {
        static char big[300];
        memset(big, 'a', 299);
        big[299] = 0;
        n = seal_req(&alice, &bob, "echo", 11, big, now, buf);
        CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, now, &got) == W4_ERR_RANGE,
              "payload above the tool's max_input");
    }

    /* rate limit: slow is 60/min with a burst of 2 */
    uint64_t t = now + 100000;
    for (int i = 0; i < 2; i++) {
        n = seal_req(&alice, &bob, "slow", 20 + (uint64_t) i, "r", t, buf);
        CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_OK, "within the burst");
    }
    n = seal_req(&alice, &bob, "slow", 22, "r", t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_RATE, "burst used up: RATE");
    n = seal_req(&alice, &bob, "slow", 23, "r", t + 500, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t + 500, &got) == W4_ERR_RATE,
          "half a token later: still RATE");
    n = seal_req(&alice, &bob, "slow", 24, "r", t + 1000, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t + 1000, &got) == W4_OK,
          "one second later (60/min): one more call");

    /* response back to alice */
    static w4_env_t req, rsp;
    static w4_card_t adir[2];
    static w4_dir_t ad;
    static w4_replay_ent_t arp[2];
    static w4_rate_ent_t art[2];
    static w4_rt_t art_rt;
    w4_dir_init(&ad, adir, 2);
    w4_dir_add(&ad, &bob.card);
    w4_rt_init(&art_rt, &alice, NULL, &ad, arp, 2, art, 2, NULL, NULL, NULL, WINDOW);
    req = got;
    CHECK(w4_env_reply(&rsp, &req, W4_MSG_RESPONSE, 0, (const uint8_t *) "ok", 2) == W4_OK,
          "reply skeleton");
    rsp.flags = W4_MF_FINAL;
    n = w4_env_seal(&bob, &rsp, t + 1001, NULL, buf, sizeof buf);
    CHECK(w4_rt_receive(&art_rt, buf, (uint32_t) n, t + 1002, &got) == W4_OK &&
              got.kind == W4_MSG_RESPONSE && got.req_id == 24 && (got.flags & W4_MF_FINAL),
          "alice receives bob's response (node-bound sender)");
    CHECK(w4_env_reply(&rsp, &got, W4_MSG_RESPONSE, 0, NULL, 0) == W4_ERR_ARG,
          "only a request can be replied to");
    CHECK(w4_env_seal(&alice, &rsp, t, NULL, buf, sizeof buf) == W4_ERR_ARG,
          "an agent cannot seal an envelope from someone else");

    /* fuzz the decoder + pipeline */
    n = seal_req(&alice, &bob, "echo", 30, "fuzz target", t + 5000, buf);
    uint32_t accepted = 0;
    for (int i = 0; i < 1500; i++) {
        memcpy(buf2, buf, (size_t) n);
        uint32_t len = (uint32_t) n;
        int k = 1 + (int) (rng() % 3);
        for (int j = 0; j < k; j++) buf2[rng() % len] ^= (uint8_t) (1u << (rng() % 8));
        if (rng() % 4 == 0) len = rng() % len;
        if (w4_rt_receive(&rt, buf2, len, t + 5000, &got) == W4_OK) accepted++;
    }
    CHECK(accepted == 0, "envelope fuzz: 1500 mutations, none accepted");
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t + 5000, &got) == W4_OK,
          "the original still verifies after the fuzz");
    for (int i = 0; i < 3000; i++) {
        uint32_t len = rng() % 400;
        for (uint32_t j = 0; j < len; j++) buf2[j] = (uint8_t) rng();
        if (len > 5 && rng() % 2) memcpy(buf2, "W4EV\x01", 5);
        (void) w4_env_decode(buf2, len, &got, NULL);
    }
    CHECK(1, "random input to the envelope decoder: no crash");
}

static void test_tables_fail_closed(void)
{
    /* rate table of one bucket: a second caller cannot evict a recently used one */
    static w4_replay_ent_t rp[1];
    static w4_rate_ent_t rtab[1];
    static w4_rt_t r1;
    w4_rt_init(&r1, &bob, &bob_mf, &bob_dir, rp, 1, rtab, 1, &cons_log, human_lookup, NULL, WINDOW);
    uint64_t t = T0 + 500000;
    int32_t n = seal_req(&alice, &bob, "echo", 1, "a", t, buf);
    CHECK(w4_rt_receive(&r1, buf, (uint32_t) n, t, &got) == W4_OK, "alice takes the only bucket");
    n = seal_req(&carol, &bob, "echo", 1, "c", t + 10, buf);
    CHECK(w4_rt_receive(&r1, buf, (uint32_t) n, t + 10, &got) == W4_ERR_RATE,
          "carol refused: no free bucket, alice's is recent (fail closed)");
    /* carol's envelope evicted alice from the one-slot replay table */
    n = seal_req(&alice, &bob, "echo", 2, "a", t + 5, buf);
    CHECK(w4_rt_receive(&r1, buf, (uint32_t) n, t + 20, &got) == W4_ERR_STALE,
          "evicted peer's envelope from before the eviction: STALE (floor)");
    n = seal_req(&carol, &bob, "echo", 2, "c", t + 70000, buf);
    CHECK(w4_rt_receive(&r1, buf, (uint32_t) n, t + 70000, &got) == W4_OK,
          "a minute later the idle bucket may be reused");
}

static void test_consent(void)
{
    uint64_t t = T0 + 1000000;
    w4_consent_t c;
    /* money tool without a token */
    int32_t n = seal_req(&alice, &bob, "pay", 100, "pay 150 to shop", t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "money tool without a token: CONSENT");
    n = seal_req(&alice, &bob, "quote", 100, "q", t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "a priced tool (no MONEY flag) still needs consent");

    consent_for("pay", 101, "pay 150 to shop", W4_CONSENT_MONEY, 200, t, t + 60000, 1, &c);
    n = seal_req_consent(&alice, "pay", 101, "pay 150 to shop", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_OK, "money tool with its token");
    n = seal_req_consent(&alice, "pay", 101, "pay 150 to shop", &c, t + 1, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t + 1, &got) == W4_ERR_REPLAY,
          "the same token in a new envelope: single use");

    consent_for("pay", 102, "pay 150 to shop", W4_CONSENT_MONEY, 200, t, t + 60000, 2, &c);
    n = seal_req_consent(&alice, "pay", 102, "pay 999 to shop", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "token for a different payload: CONSENT");
    n = seal_req_consent(&alice, "pay", 103, "pay 150 to shop", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "token for a different request id: CONSENT");

    consent_for("pay", 104, "x", W4_CONSENT_MONEY, 149, t, t + 60000, 3, &c);
    n = seal_req_consent(&alice, "pay", 104, "x", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "ceiling below the tool's price: CONSENT");
    consent_for("pay", 105, "x", W4_CONSENT_PII, 1000, t, t + 60000, 4, &c);
    n = seal_req_consent(&alice, "pay", 105, "x", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "PII scope does not allow money");
    consent_for("pay", 106, "x", W4_CONSENT_MONEY, 1000, t - 120000, t - 60000, 5, &c);
    n = seal_req_consent(&alice, "pay", 106, "x", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_EXPIRED, "expired token");
    consent_for("pay", 107, "x", W4_CONSENT_MONEY, 1000, t, t + 60000, 6, &c);
    c.sig[10] ^= 1;
    n = seal_req_consent(&alice, "pay", 107, "x", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_SIG, "forged token: SIG");
    {
        uint8_t act[W4_HASH_LEN], nonce[16] = {7};
        w4_action_digest("pay", alice.card.agent_id, bob.card.agent_id, 108, (const uint8_t *) "x",
                         1, act);
        w4_consent_issue(human2_pk, human2_sk, alice.card.agent_id, act, W4_CONSENT_MONEY, 1000, t,
                         t + 60000, nonce, NULL, &c);
        n = seal_req_consent(&alice, "pay", 108, "x", &c, t, buf);
        CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
              "token from a human the host does not know");
    }
    {
        /* carol presents a token issued for alice */
        uint8_t act[W4_HASH_LEN], nonce[16] = {8};
        w4_action_digest("pay", carol.card.agent_id, bob.card.agent_id, 109, (const uint8_t *) "x",
                         1, act);
        w4_consent_issue(human_pk, human_sk, alice.card.agent_id, act, W4_CONSENT_MONEY, 1000, t,
                         t + 60000, nonce, NULL, &c);
        n = seal_req_consent(&carol, "pay", 109, "x", &c, t, buf);
        CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
              "token for another agent: CONSENT");
    }

    n = seal_req(&alice, &bob, "profile", 110, "email", t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "personal-data tool without a token");
    consent_for("profile", 111, "email", W4_CONSENT_MONEY, 1000, t, t + 60000, 9, &c);
    n = seal_req_consent(&alice, "profile", 111, "email", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_ERR_CONSENT,
          "MONEY scope does not allow personal data");
    consent_for("profile", 112, "email", W4_CONSENT_PII, 0, t, t + 60000, 10, &c);
    n = seal_req_consent(&alice, "profile", 112, "email", &c, t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_OK, "personal data with PII token");
    n = seal_req(&alice, &bob, "live", 113, "go", t, buf);
    CHECK(w4_rt_receive(&rt, buf, (uint32_t) n, t, &got) == W4_OK,
          "free tool without money or PII: no token needed");

    /* the single-use log fails closed when full */
    static w4_consent_used_t one[1];
    static w4_consent_log_t l1;
    w4_consent_log_init(&l1, one, 1);
    uint8_t act[W4_HASH_LEN] = {1}, nonce[16] = {1};
    w4_consent_issue(human_pk, human_sk, alice.card.agent_id, act, W4_CONSENT_MONEY, 10, t,
                     t + 1000, nonce, NULL, &c);
    CHECK(w4_consent_check(&c, human_pk, alice.card.agent_id, act, W4_CONSENT_MONEY, 10, t, &l1) ==
              W4_OK,
          "local consent check");
    nonce[0] = 2;
    w4_consent_issue(human_pk, human_sk, alice.card.agent_id, act, W4_CONSENT_MONEY, 10, t,
                     t + 5000, nonce, NULL, &c);
    CHECK(w4_consent_check(&c, human_pk, alice.card.agent_id, act, W4_CONSENT_MONEY, 10, t, &l1) ==
              W4_ERR_SPACE,
          "log full: refused (fail closed)");
    CHECK(w4_consent_check(&c, human_pk, alice.card.agent_id, act, W4_CONSENT_MONEY, 10, t + 1000,
                           &l1) == W4_OK,
          "after the first token expired its entry is pruned");
    CHECK(w4_consent_check(&c, human_pk, alice.card.agent_id, act, W4_CONSENT_MONEY, 11, t + 1000,
                           &l1) == W4_ERR_CONSENT,
          "amount above the ceiling");
    CHECK(w4_consent_issue(human_pk, human_sk, alice.card.agent_id, act, 0x10, 10, t, t + 1, nonce,
                           NULL, &c) == W4_ERR_ARG,
          "unknown consent scope refused at issue");
}

/* ---- carriage ---- */
#define MAXF 8
static uint8_t frames[MAXF][W4_FRAME_MAX];
static uint32_t frame_len[MAXF], nframes;
static int send_fail;
static int cap_send(void *ctx, const uint8_t peer[W4_ID_LEN], const uint8_t *f, uint32_t len)
{
    (void) ctx;
    (void) peer;
    if (send_fail) return W4_ERR_DENIED;
    if (nframes >= MAXF || len > W4_FRAME_MAX) return W4_ERR_SPACE;
    memcpy(frames[nframes], f, len);
    frame_len[nframes++] = len;
    return W4_OK;
}

/* Keep `keep` of the 168 rows of a frame (a random subset), in place. */
static uint32_t lose_rows(uint8_t *f, uint32_t keep)
{
    uint8_t *p = f + FREIGHT_HEADER_BYTES;
    uint8_t idx[FREIGHT_ROWS];
    for (uint32_t i = 0; i < FREIGHT_ROWS; i++) idx[i] = (uint8_t) i;
    for (uint32_t i = FREIGHT_ROWS - 1; i > 0; i--) {
        uint32_t j = rng() % (i + 1);
        uint8_t x = idx[i];
        idx[i] = idx[j];
        idx[j] = x;
    }
    static uint8_t tmp[FREIGHT_BYTES];
    for (uint32_t i = 0; i < keep; i++)
        memcpy(tmp + i * FREIGHT_PACKET_BYTES, p + idx[i] * FREIGHT_PACKET_BYTES,
               FREIGHT_PACKET_BYTES);
    memcpy(p, tmp, keep * FREIGHT_PACKET_BYTES);
    return FREIGHT_HEADER_BYTES + keep * FREIGHT_PACKET_BYTES;
}

static void test_carriage(void)
{
    static w4_freight_rx_t rx;
    w4_transport_t tr = {NULL, cap_send, W4_NET_LAN, true, 120};
    uint64_t t = T0 + 2000000;
    w4_env_make(&env, W4_MSG_REQUEST, alice.card.agent_id, bob.card.agent_id, 200, 0, "live",
                (const uint8_t *) "freight", 7);
    int32_t n = w4_env_seal(&alice, &env, t, NULL, buf, sizeof buf);
    nframes = 0;
    CHECK(w4_transport_send(&tr, bob.card.agent_id, buf, (uint32_t) n, 77) == W4_OK &&
              nframes == freight_set_count((uint64_t) n, 120) && nframes == 2,
          "envelope sent as a 2-freight set (k = 120)");
    w4_freight_rx_init(&rx);
    uint32_t got_len = 0;
    int r1 = W4_ERR_ARG, r2 = W4_ERR_ARG;
    if (nframes == 2) {
        frame_len[0] = lose_rows(frames[0], 120);
        frame_len[1] = lose_rows(frames[1], 120);
        r1 = w4_freight_rx_add(&rx, frames[0], frame_len[0], &got_len);
        r2 = w4_freight_rx_add(&rx, frames[1], frame_len[1], &got_len);
    }
    CHECK(r1 == W4_ERR_PENDING && r2 == W4_OK && got_len == (uint32_t) n &&
              memcmp(rx.buf, buf, (size_t) n) == 0,
          "48 of 168 rows lost in each freight: envelope rebuilt exactly");
    CHECK(w4_rt_receive(&rt, rx.buf, got_len, t, &got) == W4_OK, "and it verifies");

    nframes = 0;
    w4_transport_send(&tr, bob.card.agent_id, buf, (uint32_t) n, 78);
    w4_freight_rx_init(&rx);
    frame_len[0] = lose_rows(frames[0], 119);
    CHECK(w4_freight_rx_add(&rx, frames[0], frame_len[0], &got_len) != W4_OK,
          "119 rows of a k = 120 freight: cannot rebuild");
    nframes = 0;
    w4_transport_send(&tr, bob.card.agent_id, buf, (uint32_t) n, 79);
    w4_freight_rx_init(&rx);
    frames[0][FREIGHT_HEADER_BYTES + 5] ^= 0x10; /* corrupt data row 0 */
    int rc = w4_freight_rx_add(&rx, frames[0], FREIGHT_HEADER_BYTES + 120 * FREIGHT_PACKET_BYTES,
                               &got_len);
    CHECK(rc == W4_ERR_HASH, "a corrupted row is caught by the freight hash, not accepted");
    CHECK(w4_freight_rx_add(&rx, frames[1], 63, &got_len) == W4_ERR_PARSE &&
              w4_freight_rx_add(&rx, frames[1], FREIGHT_HEADER_BYTES + 20, &got_len) ==
                  W4_ERR_PARSE,
          "short and ragged frames refused");
    for (int i = 0; i < 300; i++) {
        static uint8_t f[W4_FRAME_MAX];
        for (uint32_t j = 0; j < sizeof f; j++) f[j] = (uint8_t) rng();
        if (i & 1) memcpy(f, frames[0], FREIGHT_HEADER_BYTES);
        w4_freight_rx_init(&rx);
        (void) w4_freight_rx_add(&rx, f, sizeof f, &got_len);
    }
    CHECK(1, "random freight frames: no crash");

    /* raw transport and offline outbox */
    static w4_outbox_slot_t slots[2];
    w4_outbox_t ob;
    w4_outbox_init(&ob, slots, 2);
    w4_transport_t raw = {NULL, cap_send, W4_NET_OFFLINE, false, 0};
    nframes = 0;
    CHECK(w4_post(&raw, &ob, bob.card.agent_id, buf, (uint32_t) n, 1) == W4_ERR_PENDING &&
              w4_outbox_count(&ob) == 1 && nframes == 0,
          "offline: envelope queued");
    CHECK(w4_post(&raw, &ob, bob.card.agent_id, buf, 10, 2) == W4_ERR_PENDING &&
              w4_post(&raw, &ob, bob.card.agent_id, buf, 10, 3) == W4_ERR_SPACE,
          "outbox full: SPACE");
    CHECK(w4_outbox_flush(&raw, &ob, 10) == 0, "flush while offline sends nothing");
    raw.reach = W4_NET_LAN;
    send_fail = 1;
    CHECK(w4_outbox_flush(&raw, &ob, 10) == 0 && w4_outbox_count(&ob) == 2,
          "link drops during flush: everything stays queued");
    send_fail = 0;
    CHECK(w4_outbox_flush(&raw, &ob, 10) == 2 && w4_outbox_count(&ob) == 0 && nframes == 2 &&
              frame_len[0] == (uint32_t) n && memcmp(frames[0], buf, (size_t) n) == 0,
          "link appears: queue flushed, bytes unchanged");
    send_fail = 1;
    CHECK(w4_post(&raw, &ob, bob.card.agent_id, buf, 10, 4) == W4_ERR_PENDING &&
              w4_outbox_count(&ob) == 1,
          "send failure while online: queued, not lost");
    send_fail = 0;
}

/* ---- link ---- */
static const uint8_t ETH_SK[32] = {0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46,
                                   0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46,
                                   0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46, 0x46};
static const uint8_t HS_KEY[32] = "0123456789abcdef0123456789abcdef";

static int32_t make_id_token(const char *nonce, const char *sub, uint64_t now_s, const uint8_t *key,
                             char *out, uint32_t cap)
{
    char p[600];
    int pl = snprintf(p, sizeof p,
                      "{\"iss\":\"https://id.example\",\"sub\":\"%s\",\"aud\":\"web4-client\","
                      "\"iat\":%llu,\"exp\":%llu,\"nonce\":\"%s\"}",
                      sub, (unsigned long long) now_s, (unsigned long long) (now_s + 600), nonce);
    const char *h = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    return w4_jws_sign_hs256(h, (uint32_t) strlen(h), (const uint8_t *) p, (uint32_t) pl, key, 32,
                             out, cap);
}

static void test_link(void)
{
    static w4_link_t l, l2;
    static w4_jws_t scratch;
    static char tok[W4_IDTOKEN_MAX];
    uint64_t now = T0 + 3000000;
    memset(&l, 0, sizeof l);
    strcpy(l.oidc_iss, "https://id.example");
    strcpy(l.oidc_sub, "user-4242");
    strcpy(l.oidc_aud, "web4-client");
    CHECK(w4_secp_pubkey(ETH_SK, l.eth_pub) == W4_OK, "wallet key");
    w4_eth_address(l.eth_pub, l.eth_addr);
    l.created_ms = now - 1000;
    l.expires_ms = now + 86400000ull;
    CHECK(w4_link_sign_agent(&l, &alice, NULL) == W4_OK, "Web 4 leg: agent signs (ML-DSA-65)");
    CHECK(w4_link_sign_eth(&l, w4_secp_signer, (void *) ETH_SK) == W4_OK,
          "Web 3 leg: wallet personal_signs");
    uint8_t d[W4_HASH_LEN], h[32], rec[64];
    char msg[96], nonce[44];
    w4_link_digest(&l, d);
    int32_t ml = w4_link_eth_message(d, msg, sizeof msg);
    CHECK(ml == 15 + 64 && strncmp(msg, "web4 link v1 0x", 15) == 0, "the text the wallet shows");
    w4_eth_personal_hash((const uint8_t *) msg, (uint32_t) ml, h);
    CHECK(w4_secp_recover(h, &l.eth_sig, rec) == W4_OK && memcmp(rec, l.eth_pub, 64) == 0,
          "ecrecover of the personal_sign gives the wallet key");
    CHECK(w4_link_nonce(d, nonce) == W4_OK && strlen(nonce) == 43, "OIDC nonce = b64url(digest)");
    uint64_t now_s = now / 1000u;
    int32_t tl = make_id_token(nonce, "user-4242", now_s, HS_KEY, tok, sizeof tok);
    CHECK(tl > 0 && w4_link_set_id_token(&l, tok, (uint32_t) tl) == W4_OK,
          "Web 2 leg: ID token carrying the nonce");

    w4_hs256_key_t hk = {HS_KEY, 32};
    uint32_t failed = 99;
    CHECK(w4_link_verify(&l, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_OK &&
              failed == 0,
          "link: all three worlds verify");
    CHECK(w4_link_verify(&l, bob.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_ERR_SIG &&
              failed == W4_LINK_WEB4,
          "wrong agent key: only the Web 4 bit");
    l2 = l;
    l2.eth_sig.s[31] ^= 1;
    CHECK(w4_link_verify(&l2, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_ERR_SIG &&
              failed == W4_LINK_WEB3,
          "bad wallet signature: only the Web 3 bit");
    l2 = l;
    l2.eth_addr[0] ^= 1;
    CHECK(w4_link_verify(&l2, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_ERR_SIG &&
              (failed & W4_LINK_WEB3),
          "address not the key's: Web 3 bit");
    {
        w4_hs256_key_t wrong = {(const uint8_t *) "ffffffffffffffffffffffffffffffff", 32};
        CHECK(w4_link_verify(&l, alice.card.pk, "HS256", w4_jws_hs256_verify, &wrong, &scratch, now,
                             &failed) == W4_ERR_SIG &&
                  failed == W4_LINK_WEB2,
              "ID token not from the trusted key: only the Web 2 bit");
        CHECK(w4_link_verify(&l, alice.card.pk, "RS256", w4_jws_hs256_verify, &hk, &scratch, now,
                             &failed) == W4_ERR_SIG &&
                  failed == W4_LINK_WEB2,
              "algorithm switching refused: Web 2 bit");
    }
    l2 = l;
    tl = make_id_token("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "user-4242", now_s, HS_KEY,
                       tok, sizeof tok);
    w4_link_set_id_token(&l2, tok, (uint32_t) tl);
    CHECK(w4_link_verify(&l2, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_ERR_SIG &&
              failed == W4_LINK_WEB2,
          "ID token for another nonce: Web 2 bit");
    l2 = l;
    tl = make_id_token(nonce, "user-9999", now_s, HS_KEY, tok, sizeof tok);
    w4_link_set_id_token(&l2, tok, (uint32_t) tl);
    CHECK(w4_link_verify(&l2, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_ERR_SIG &&
              failed == W4_LINK_WEB2,
          "ID token about another subject: Web 2 bit");
    CHECK(w4_link_verify(&l, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch,
                         now + 700000, &failed) == W4_ERR_SIG &&
              failed == W4_LINK_WEB2,
          "expired ID token: Web 2 bit");
    l2 = l;
    strcpy(l2.oidc_sub, "user-4243");
    CHECK(w4_link_verify(&l2, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch, now,
                         &failed) == W4_ERR_SIG &&
              failed == W4_LINK_ALL,
          "record edited after signing: all three bits");
    CHECK(w4_link_verify(&l, alice.card.pk, "HS256", w4_jws_hs256_verify, &hk, &scratch,
                         l.expires_ms, &failed) == W4_ERR_EXPIRED,
          "link record expires");
    l2 = l;
    l2.expires_ms = l2.created_ms;
    CHECK(w4_link_sign_agent(&l2, &alice, NULL) == W4_ERR_ARG, "empty validity refused");
}

/* ---- payments ---- */
static w4_vfv_settle_t last_post;
static int ledger_calls;
static int ledger(void *ctx, const w4_vfv_settle_t *s)
{
    (void) ctx;
    last_post = *s;
    ledger_calls++;
    return W4_OK;
}

static void intent_base(w4_pay_intent_t *p, uint64_t now)
{
    memset(p, 0, sizeof *p);
    memset(p->intent_id, 0xA5, 16);
    memcpy(p->payer_agent, alice.card.agent_id, W4_ID_LEN);
    memcpy(p->payee_agent, bob.card.agent_id, W4_ID_LEN);
    p->created_ms = now - 10;
    p->expires_ms = now + 600000;
    strcpy(p->memo, "order 17");
}

static int pay_consent(const w4_pay_intent_t *p, uint64_t max, uint8_t tag, uint64_t now,
                       w4_consent_t *c)
{
    uint8_t d[W4_HASH_LEN], nonce[16];
    w4_pay_digest(p, d);
    memset(nonce, tag, 16);
    return w4_consent_issue(human_pk, human_sk, alice.card.agent_id, d, W4_CONSENT_MONEY, max, now,
                            now + 60000, nonce, NULL, c);
}

static void test_payments(void)
{
    uint64_t now = T0 + 4000000;
    static w4_pay_intent_t p, q;
    static w4_settlement_t s;
    w4_consent_t c;
    static w4_consent_used_t pu[8];
    w4_consent_log_t pl;
    w4_consent_log_init(&pl, pu, 8);
    w4_rails_t rails;
    memset(&rails, 0, sizeof rails);
    rails.ledger = ledger;
    rails.reach = W4_NET_LAN;

    /* exact conversion */
    w4_u256 v;
    uint64_t back = 0;
    char dec[90];
    w4_w w;
    CHECK(w4_vfv_to_token(1234, 18, &v) == W4_OK, "12.34 VFV -> 18-decimal token");
    w4_w_init(&w, dec, sizeof dec);
    w4_w_u256_dec(&w, &v);
    w4_w_cstr(&w);
    CHECK(strcmp(dec, "12340000000000000000") == 0, "exactly 12340000000000000000 base units");
    CHECK(w4_token_to_vfv(&v, 18, &back) == W4_OK && back == 1234, "and back, exactly");
    w4_u256_from_u64(&v, 12340000000000000001ull);
    CHECK(w4_token_to_vfv(&v, 18, &back) == W4_ERR_RANGE,
          "sub-cent remainder refused, not rounded");
    CHECK(w4_vfv_to_token(1234, 0, &v) == W4_ERR_RANGE, "token with 0 decimals: 12.34 refused");
    CHECK(w4_vfv_to_token(1200, 0, &v) == W4_OK && w4_u256_to_u64(&v, &back) && back == 12,
          "token with 0 decimals: 12.00 -> 12");

    /* VFV */
    intent_base(&p, now);
    p.asset = W4_ASSET_VFV;
    p.decimals = W4_VFV_MINOR;
    w4_u256_from_u64(&p.amount, 1234);
    CHECK(w4_pay_sign(&p, &alice, NULL) == W4_OK, "payer agent signs the VFV intent");
    CHECK(w4_pay_sign(&p, &bob, NULL) == W4_ERR_BINDING, "only the payer can sign");
    pay_consent(&p, 1234, 1, now, &c);
    CHECK(w4_pay_settle(&p, alice.card.pk, NULL, human_pk, &pl, &rails, now, &s) == W4_ERR_CONSENT,
          "no human token: nothing moves");
    rails.reach = W4_NET_OFFLINE;
    ledger_calls = 0;
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_PENDING &&
              ledger_calls == 0 && pl.n == 0,
          "offline: VFV pending, consent not consumed");
    rails.reach = W4_NET_LAN;
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_OK &&
              ledger_calls == 1 && s.rail == W4_RAIL_VFV && last_post.amount_minor == 1234 &&
              memcmp(last_post.payee, bob.card.agent_id, W4_ID_LEN) == 0,
          "LAN: posted to the VFV ledger, exact amount");
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_REPLAY &&
              ledger_calls == 1,
          "the same consent cannot pay twice");
    pay_consent(&p, 1233, 2, now, &c);
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_CONSENT,
          "ceiling below the amount");
    q = p;
    w4_u256_from_u64(&q.amount, 99999);
    pay_consent(&q, 100000, 3, now, &c);
    CHECK(w4_pay_settle(&q, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_SIG,
          "amount changed after the payer signed: SIG");
    pay_consent(&p, 5000, 4, now, &c);
    CHECK(w4_pay_settle(&p, bob.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_BINDING,
          "payer key does not match the payer agent");
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human2_pk, &pl, &rails, now, &s) == W4_ERR_CONSENT,
          "token checked against another human's key");
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, p.expires_ms, &s) ==
              W4_ERR_EXPIRED,
          "intent expires");
    q = p;
    q.decimals = 18;
    CHECK(w4_pay_sign(&q, &alice, NULL) == W4_ERR_RANGE, "VFV intent must use VFV decimals");

    /* EVM: ERC-20 */
    uint8_t wallet_pub[64], wallet[20];
    w4_secp_pubkey(ETH_SK, wallet_pub);
    w4_eth_address(wallet_pub, wallet);
    intent_base(&p, now);
    p.asset = W4_ASSET_ERC20;
    p.decimals = 6;
    p.chain_id = 1;
    memset(p.token, 0x11, 20);
    memset(p.payee_eth, 0x22, 20);
    w4_vfv_to_token(1234, 6, &p.amount);
    CHECK(w4_pay_sign(&p, &alice, NULL) == W4_OK, "ERC-20 intent signed");
    rails.eth_sign = w4_secp_signer;
    rails.eth_ctx = (void *) ETH_SK;
    rails.evm_nonce = 7;
    w4_u256_from_u64(&rails.max_priority, 1000000000ull);
    w4_u256_from_u64(&rails.max_fee, 30000000000ull);
    rails.gas_limit = 65000;
    rails.reach = W4_NET_ONLINE;
    pay_consent(&p, 0, 5, now, &c);
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_OK &&
              s.rail == W4_RAIL_EVM && s.broadcast_now && s.raw_len > 0,
          "online: EIP-1559 transaction built, ready to broadcast");
    {
        w4_eth_tx_t tx;
        w4_ecdsa_sig_t sg;
        uint8_t un[256], hh[32], pub[64], th[32];
        int ok = w4_eth_1559_decode(s.raw_tx, s.raw_len, &tx, &sg) == W4_OK;
        CHECK(ok && tx.chain_id == 1 && tx.nonce == 7 && tx.gas_limit == 65000 &&
                  memcmp(tx.to, p.token, 20) == 0 && w4_u256_is_zero(&tx.value) &&
                  tx.data_len == 68,
              "decoded: chain 1, nonce 7, to = token contract, no ETH value");
        uint8_t sel[4];
        w4_abi_selector("transfer(address,uint256)", sel);
        w4_u256 amt;
        if (ok) w4_abi_word_u256(tx.data + 36, &amt);
        CHECK(ok && memcmp(tx.data, sel, 4) == 0 && memcmp(tx.data + 16, p.payee_eth, 20) == 0 &&
                  w4_u256_cmp(&amt, &p.amount) == 0,
              "calldata: transfer(payee, 12340000)");
        int32_t un_n = ok ? w4_eth_1559_unsigned(&tx, un, sizeof un) : -1;
        if (un_n > 0) w4_eth_signing_hash(un, (uint32_t) un_n, hh);
        CHECK(un_n > 0 && w4_secp_recover(hh, &sg, pub) == W4_OK &&
                  memcmp(pub, wallet_pub, 64) == 0,
              "signer recovered from the transaction is the wallet");
        w4_keccak256(s.raw_tx, s.raw_len, th);
        CHECK(memcmp(th, s.tx_hash, 32) == 0, "tx hash = Keccak-256(raw)");
        CHECK(strstr(s.rpc, "\"eth_sendRawTransaction\"") != NULL &&
                  strstr(s.rpc, "\"0x02") != NULL,
              "eth_sendRawTransaction body");
    }
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_REPLAY,
          "token intent: consent single use too");
    rails.reach = W4_NET_OFFLINE;
    pay_consent(&p, 0, 6, now, &c);
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_OK &&
              s.rail == W4_RAIL_EVM && !s.broadcast_now && s.raw_len > 0,
          "offline: signed now, kept for broadcast later");

    /* EVM: native ETH */
    intent_base(&p, now);
    p.asset = W4_ASSET_ETH;
    p.decimals = 18;
    p.chain_id = 11155111;
    memset(p.payee_eth, 0x33, 20);
    w4_u256_from_u64(&p.amount, 1000000000000000ull);
    w4_pay_sign(&p, &alice, NULL);
    rails.reach = W4_NET_ONLINE;
    pay_consent(&p, 0, 7, now, &c);
    {
        w4_eth_tx_t tx;
        w4_ecdsa_sig_t sg;
        CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_OK &&
                  w4_eth_1559_decode(s.raw_tx, s.raw_len, &tx, &sg) == W4_OK &&
                  tx.chain_id == 11155111 && memcmp(tx.to, p.payee_eth, 20) == 0 &&
                  w4_u256_cmp(&tx.value, &p.amount) == 0 && tx.data_len == 0,
              "native ETH: value to the payee, no calldata");
    }
    rails.eth_sign = NULL;
    pay_consent(&p, 0, 8, now, &c);
    CHECK(w4_pay_settle(&p, alice.card.pk, &c, human_pk, &pl, &rails, now, &s) == W4_ERR_UNSUPP,
          "no signer configured: refused before consent is used");
    q = p;
    memset(q.payee_eth, 0, 20);
    CHECK(w4_pay_sign(&q, &alice, NULL) == W4_ERR_ARG, "EVM intent without a recipient refused");
}

static void test_content(void)
{
    static w4_content_map_t m, m2;
    const char *body = "hello web4";
    uint64_t now = T0 + 5000000;
    CHECK(w4_cmap_make(&m, "https://example.org/a.txt", (const uint8_t *) body, 10, "text/plain",
                       &alice, now, NULL) == W4_OK,
          "content map made");
    CHECK(w4_cmap_verify(&m, alice.card.pk, (const uint8_t *) body, 10) == W4_OK,
          "verifies with the bytes");
    CHECK(w4_cmap_verify(&m, alice.card.pk, (const uint8_t *) "hello web5", 10) == W4_ERR_HASH,
          "other bytes: HASH");
    CHECK(w4_cmap_verify(&m, bob.card.pk, NULL, 0) == W4_ERR_BINDING, "other agent: BINDING");
    m2 = m;
    m2.url[8] = 'X';
    CHECK(w4_cmap_verify(&m2, alice.card.pk, NULL, 0) == W4_ERR_SIG, "edited URL: SIG");
    ipfsn_cid_t cid;
    w4_cid_of((const uint8_t *) body, 10, &cid);
    char uri[128];
    int32_t ul = w4_cid_uri(&m.cid, uri, sizeof uri);
    CHECK(ipfsn_cid_equal(&cid, &m.cid) && ul > 8 && strncmp(uri, "ipfs://b", 8) == 0,
          "the CID is CIDv1 of the bytes (ipfs:// form)");
    CHECK(w4_cmap_make(&m2, "ftp://example.org/a", (const uint8_t *) body, 10, NULL, &alice, now,
                       NULL) == W4_ERR_UNSUPP,
          "only http(s) URLs are mapped");
    CHECK(w4_cmap_make(&m2, "https://user:pw@example.org/", (const uint8_t *) body, 10, NULL,
                       &alice, now, NULL) == W4_ERR_ARG,
          "URL with credentials refused");
}

static void test_policy(void)
{
    struct {
        uint8_t op, reach, world, action;
    } want[] = {
        {W4_OP_LOGIN, W4_NET_ONLINE, W4_WORLD_WEB2, W4_DO_NOW},
        {W4_OP_LOGIN, W4_NET_LAN, W4_WORLD_WEB4, W4_DO_NOW},
        {W4_OP_LOGIN, W4_NET_OFFLINE, W4_WORLD_WEB4, W4_DO_LOCAL},
        {W4_OP_DISCOVER, W4_NET_LAN, W4_WORLD_WEB4, W4_DO_NOW},
        {W4_OP_DISCOVER, W4_NET_OFFLINE, W4_WORLD_WEB4, W4_DO_LOCAL},
        {W4_OP_AGENT_MSG, W4_NET_ONLINE, W4_WORLD_WEB4, W4_DO_NOW},
        {W4_OP_AGENT_MSG, W4_NET_OFFLINE, W4_WORLD_WEB4, W4_DO_QUEUE},
        {W4_OP_STREAM, W4_NET_OFFLINE, W4_WORLD_WEB4, W4_DO_REFUSE},
        {W4_OP_FEED_PUBLISH, W4_NET_ONLINE, W4_WORLD_WEB2, W4_DO_NOW},
        {W4_OP_FEED_PUBLISH, W4_NET_LAN, W4_WORLD_WEB4, W4_DO_NOW},
        {W4_OP_FEED_PUBLISH, W4_NET_OFFLINE, W4_WORLD_WEB2, W4_DO_QUEUE},
        {W4_OP_FEED_READ, W4_NET_OFFLINE, W4_WORLD_WEB2, W4_DO_LOCAL},
        {W4_OP_NOTIFY, W4_NET_LAN, W4_WORLD_WEB4, W4_DO_NOW},
        {W4_OP_API, W4_NET_LAN, W4_WORLD_WEB2, W4_DO_REFUSE},
        {W4_OP_PAY_VFV, W4_NET_LAN, W4_WORLD_WEB4, W4_DO_NOW},
        {W4_OP_PAY_VFV, W4_NET_OFFLINE, W4_WORLD_WEB4, W4_DO_QUEUE},
        {W4_OP_PAY_TOKEN, W4_NET_ONLINE, W4_WORLD_WEB3, W4_DO_NOW},
        {W4_OP_PAY_TOKEN, W4_NET_LAN, W4_WORLD_WEB3, W4_DO_QUEUE},
        {W4_OP_OWNERSHIP, W4_NET_ONLINE, W4_WORLD_WEB3, W4_DO_NOW},
        {W4_OP_OWNERSHIP, W4_NET_OFFLINE, W4_WORLD_WEB4, W4_DO_LOCAL},
        {W4_OP_SETTLE_FINAL, W4_NET_LAN, W4_WORLD_WEB3, W4_DO_QUEUE},
        {W4_OP_CONTENT_PUBLISH, W4_NET_OFFLINE, W4_WORLD_WEB3, W4_DO_LOCAL},
        {W4_OP_CONTENT_PUBLISH, W4_NET_ONLINE, W4_WORLD_WEB3, W4_DO_NOW},
        {W4_OP_CONTENT_FETCH, W4_NET_LAN, W4_WORLD_WEB3, W4_DO_NOW},
        {W4_OP_CONTENT_FETCH, W4_NET_OFFLINE, W4_WORLD_WEB3, W4_DO_LOCAL},
    };
    int ok = 1;
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
        uint8_t wd = 0, ac = 0;
        if (w4_route(want[i].op, want[i].reach, &wd, &ac) != W4_OK || wd != want[i].world ||
            ac != want[i].action) {
            printf("  route op %u reach %u -> world %u action %u\n", want[i].op, want[i].reach, wd,
                   ac);
            ok = 0;
        }
    }
    CHECK(ok, "routing: 25 (operation, reach) cases");
    ok = 1;
    for (uint8_t op = 0; op < W4_OP_COUNT; op++) {
        const w4_policy_t *p = w4_policy(op);
        if (!p || p->op != op || !p->why || strlen(p->why) < 20) ok = 0;
        for (uint8_t r = 0; r <= W4_NET_ONLINE; r++) {
            uint8_t wd, ac;
            if (w4_route(op, r, &wd, &ac) != W4_OK || ac < W4_DO_NOW || ac > W4_DO_REFUSE) ok = 0;
        }
    }
    CHECK(ok, "every row is indexed by its op, explained, and routes at every reach");
    uint8_t wd, ac;
    CHECK(w4_route(W4_OP_COUNT, 0, &wd, &ac) == W4_ERR_ARG &&
              w4_route(0, 3, &wd, &ac) == W4_ERR_ARG,
          "unknown op / reach refused");
}

int main(void)
{
    test_identity();
    test_manifest();
    test_envelopes();
    test_tables_fail_closed();
    test_consent();
    test_carriage();
    test_link();
    test_payments();
    test_content();
    test_policy();
    printf("web4_agent: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
