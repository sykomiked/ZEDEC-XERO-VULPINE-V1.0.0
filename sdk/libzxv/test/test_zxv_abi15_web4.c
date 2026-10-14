/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi15_web4.c — conformance test of the ABI 1.5 web4 group, linked
 * against the shared library only (no kernel headers). Checked here:
 *   - identity against NIST ACVP ML-DSA-65 keyGen (tcId 26 and 27): the
 *     agent_id of a key made from the ACVP seed equals SHA3-256 of the ACVP
 *     public key, as the kernel's rule agent_id = SHA3-256(pk) demands;
 *   - the card, consent-token, manifest and envelope byte layouts as
 *     documented, and the action digest recomputed with zxv_sha3_256;
 *   - every signature binds: any flipped byte, wrong key or wrong context
 *     is refused; consent is single-use, revocable, expiring and scoped;
 *   - capabilities (CRYPTO_SIGN, ATTEST), context binding and fail-closed
 *     behaviour once the context halts; every error path. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                   \
    do {                                                \
        checks++;                                       \
        if (!(c)) {                                     \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)
#define OK(call)                                                       \
    do {                                                               \
        int32_t rc_ = (call);                                          \
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_)); \
    } while (0)
#define IS(call, want)                                                                 \
    do {                                                                               \
        int32_t rc_ = (call);                                                          \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want); \
    } while (0)

/* NIST ACVP ML-DSA-65 keyGen, tcId 26 and 27: seed and SHA3-256(pk), as in
 * kernel/src/pqsec/pq_kat_vectors.h. */
static const uint8_t ACVP_SEED26[32] = {
    0xa9, 0x91, 0xfd, 0x42, 0xb0, 0x71, 0xd4, 0x9c, 0x48, 0xae, 0x3e, 0x75, 0xc6, 0x47, 0x45, 0x9e,
    0x0d, 0xaa, 0xd1, 0xe1, 0xba, 0x35, 0x6a, 0x04, 0x80, 0x19, 0x12, 0xd3, 0x29, 0x4b, 0xcf, 0xf8};
static const uint8_t ACVP_HPK26[32] = {
    0x00, 0x83, 0xe4, 0xd4, 0xf5, 0x62, 0x78, 0x0d, 0xd5, 0xef, 0x68, 0x5c, 0x88, 0xdb, 0x5a, 0xfc,
    0xda, 0xc4, 0x4d, 0x1b, 0x4b, 0xe5, 0xe4, 0x25, 0x3d, 0x0d, 0xe7, 0x71, 0x10, 0x03, 0x16, 0xf3};
static const uint8_t ACVP_SEED27[32] = {
    0x49, 0x4f, 0x29, 0xad, 0x1c, 0x93, 0xab, 0xb2, 0xb9, 0x54, 0x5b, 0xd1, 0x4c, 0xc5, 0x75, 0xa9,
    0x8e, 0xcd, 0x30, 0x62, 0x13, 0x7b, 0x43, 0x9b, 0x49, 0xeb, 0x1a, 0x8c, 0xc6, 0x65, 0x2f, 0xc6};
static const uint8_t ACVP_HPK27[32] = {
    0xf0, 0xe3, 0x8a, 0xea, 0x58, 0x1d, 0x23, 0xd1, 0x40, 0x42, 0x4b, 0xa2, 0x7e, 0xd5, 0xda, 0x0b,
    0x16, 0xa2, 0x1e, 0x5e, 0x02, 0xc9, 0xa6, 0xae, 0x88, 0x27, 0x92, 0xc2, 0xe7, 0x88, 0x12, 0xba};

#define PK  ZXV_W4_PK_BYTES
#define SIG ZXV_W4_SIG_BYTES
#define TOK ZXV_W4_CONSENT_BYTES

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* The action digest as documented: SHA3-256 of length-prefixed pieces. */
static void ref_digest(const char *tool, const uint8_t from[32], const uint8_t to[32], uint64_t req,
                       const uint8_t *p, uint32_t n, uint8_t out[32])
{
    static uint8_t b[4096];
    size_t o = 0;
    put32(b + o, 14);
    memcpy(b + o + 4, "web4/v1/action", 14);
    o += 18;
    uint32_t tl = (uint32_t) strlen(tool);
    put32(b + o, tl);
    memcpy(b + o + 4, tool, tl);
    o += 4 + tl;
    memcpy(b + o, from, 32);
    memcpy(b + o + 32, to, 32);
    o += 64;
    put64(b + o, req);
    o += 8;
    put32(b + o, n);
    o += 4;
    if (n) memcpy(b + o, p, n);
    o += n;
    zxv_sha3_256(b, o, out);
}

typedef struct {
    zxv_w4_agent *h;
    uint8_t id[32], peer[32], pk[PK];
} agent_t;

static void mk(const zxv_cap *cap, const uint8_t *seed, const char *name, const zxv_w4_agent *node,
               uint64_t now, agent_t *a)
{
    a->h = NULL;
    OK(zxv_w4_agent_create(cap, seed, name, node, now, &a->h));
    if (a->h) OK(zxv_w4_agent_info(a->h, a->id, a->peer, a->pk));
}

static zxv_ctx *ctx, *ctx2;
static zxv_cap *cap, *cap2, *cap_attest_only, *cap_sign_only;
static agent_t alice, bob, carol, human, human2;

static void test_identity(void)
{
    uint8_t h[32];
    mk(cap, ACVP_SEED26, "alice", NULL, 1000, &alice);
    CHECK(memcmp(alice.id, ACVP_HPK26, 32) == 0,
          "agent_id of the ACVP tcId 26 key = SHA3-256 of the ACVP public key");
    zxv_sha3_256(alice.pk, PK, h);
    CHECK(memcmp(h, alice.id, 32) == 0 && memcmp(alice.peer, alice.id, 32) == 0,
          "agent_id = SHA3-256(pk); self-bound: peer_id = agent_id");
    mk(cap, ACVP_SEED27, "bob", NULL, 1000, &bob);
    CHECK(memcmp(bob.id, ACVP_HPK27, 32) == 0, "ACVP tcId 27");
    mk(cap, NULL, "carol.v2_x-1", NULL, 1000, &carol);
    agent_t c2;
    mk(cap, NULL, "carol", NULL, 1000, &c2);
    CHECK(memcmp(carol.id, c2.id, 32) != 0, "fresh keys differ");
    zxv_w4_agent_destroy(c2.h);
    OK(zxv_w4_agent_info(alice.h, NULL, NULL, NULL));

    /* the card, byte by byte */
    static uint8_t card[ZXV_W4_CARD_MAX + 4];
    size_t cl = 0;
    IS(zxv_w4_agent_card(alice.h, card, 10, &cl), ZXV_E_BUFFER);
    CHECK(cl == 7 + 5 + 8 + PK + SIG, "self-bound card length %zu", cl);
    cl = 0;
    IS(zxv_w4_agent_card(alice.h, NULL, 0, &cl), ZXV_E_BUFFER);
    OK(zxv_w4_agent_card(alice.h, card, sizeof card, &cl));
    CHECK(memcmp(card, "W4CD", 4) == 0 && card[4] == 1 && card[5] == 1 && card[6] == 5 &&
              memcmp(card + 7, "alice", 5) == 0 && le64(card + 12) == 1000 &&
              memcmp(card + 20, alice.pk, PK) == 0,
          "card: magic, version, self-bound, name, bound_ms, public key");
    uint8_t id[32], peer[32];
    char name[32];
    uint64_t bound = 0;
    OK(zxv_w4_card_verify(card, cl, id, peer, name, &bound));
    CHECK(memcmp(id, alice.id, 32) == 0 && memcmp(peer, alice.id, 32) == 0 &&
              strcmp(name, "alice") == 0 && bound == 1000,
          "card_verify returns the ids, name and time");
    OK(zxv_w4_card_verify(card, cl, NULL, NULL, NULL, NULL));
    static uint8_t t[ZXV_W4_CARD_MAX + 4];
    const size_t flips[] = {12, 20, 20 + PK + 7, cl - 1};
    for (unsigned i = 0; i < 4; i++) {
        memcpy(t, card, cl);
        t[flips[i]] ^= 0x01;
        IS(zxv_w4_card_verify(t, cl, NULL, NULL, NULL, NULL), ZXV_E_VERIFY);
    }
    memcpy(t, card, cl);
    t[7] = 'b'; /* "blice": another name, same signature */
    IS(zxv_w4_card_verify(t, cl, NULL, NULL, NULL, NULL), ZXV_E_VERIFY);
    const struct {
        size_t off;
        uint8_t v;
    } fmt[] = {{0, 'X'}, {4, 2}, {5, 2}, {6, 0}, {6, 32}, {7, 'A'}, {9, ' '}};
    for (unsigned i = 0; i < sizeof fmt / sizeof fmt[0]; i++) {
        memcpy(t, card, cl);
        t[fmt[i].off] = fmt[i].v;
        IS(zxv_w4_card_verify(t, cl, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    }
    memcpy(t, card, cl);
    t[5] = 0; /* claims a node key that is not there */
    IS(zxv_w4_card_verify(t, cl, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_card_verify(card, cl - 1, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_card_verify(card, cl + 1, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_card_verify(card, 10, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_card_verify(NULL, cl, NULL, NULL, NULL, NULL), ZXV_E_ARG);

    /* a node-bound agent: the node signs, peer_id = the node's id */
    agent_t node, hosted;
    mk(cap, NULL, "node-1", NULL, 500, &node);
    mk(cap, NULL, "svc", node.h, 600, &hosted);
    CHECK(memcmp(hosted.peer, node.id, 32) == 0 && memcmp(hosted.id, node.id, 32) != 0,
          "hosted agent: peer_id = node id");
    OK(zxv_w4_agent_card(hosted.h, card, sizeof card, &cl));
    CHECK(cl == 7 + 3 + 8 + 2 * PK + SIG && card[5] == 0 &&
              memcmp(card + 18 + PK, node.pk, PK) == 0,
          "node-bound card carries the node public key");
    zxv_w4_agent_destroy(node.h); /* the card keeps only the node's public key */
    OK(zxv_w4_card_verify(card, cl, id, peer, name, &bound));
    CHECK(memcmp(id, hosted.id, 32) == 0 && memcmp(peer, hosted.peer, 32) == 0 && bound == 600,
          "node-bound card verifies after the node handle is gone");
    memcpy(t, card, cl);
    memcpy(t + 18 + PK, alice.pk, PK); /* claim another node */
    IS(zxv_w4_card_verify(t, cl, NULL, NULL, NULL, NULL), ZXV_E_VERIFY);
    zxv_w4_agent_destroy(hosted.h);

    /* argument and capability errors */
    zxv_w4_agent *x = NULL;
    IS(zxv_w4_agent_create(cap, NULL, NULL, NULL, 0, &x), ZXV_E_ARG);
    IS(zxv_w4_agent_create(cap, NULL, "", NULL, 0, &x), ZXV_E_ARG);
    IS(zxv_w4_agent_create(cap, NULL, "Alice", NULL, 0, &x), ZXV_E_ARG);
    IS(zxv_w4_agent_create(cap, NULL, "a b", NULL, 0, &x), ZXV_E_ARG);
    IS(zxv_w4_agent_create(cap, NULL, "abcdefghijklmnopqrstuvwxyz012345", NULL, 0, &x),
       ZXV_E_TOO_LONG);
    OK(zxv_w4_agent_create(cap, NULL, "abcdefghijklmnopqrstuvwxyz01234", NULL, 0, &x));
    zxv_w4_agent_destroy(x);
    IS(zxv_w4_agent_create(cap, NULL, "a\xff", NULL, 0, &x), ZXV_E_UTF8);
    IS(zxv_w4_agent_create(cap, NULL, "ok", NULL, 0, NULL), ZXV_E_ARG);
    IS(zxv_w4_agent_create(cap_attest_only, NULL, "ok", NULL, 0, &x), ZXV_E_CAP);
    IS(zxv_w4_agent_create(NULL, NULL, "ok", NULL, 0, &x), ZXV_E_ARG);
    IS(zxv_w4_agent_create(cap2, NULL, "ok", alice.h, 0, &x), ZXV_E_CAP); /* node elsewhere */
    IS(zxv_w4_agent_info(NULL, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_w4_agent_card(alice.h, card, sizeof card, NULL), ZXV_E_ARG);
    zxv_w4_agent_destroy(NULL);
}

static void test_directory(void)
{
    static uint8_t ca[ZXV_W4_CARD_MAX], cb[ZXV_W4_CARD_MAX], cc[ZXV_W4_CARD_MAX],
        out[ZXV_W4_CARD_MAX];
    size_t la, lb, lc, lo;
    OK(zxv_w4_agent_card(alice.h, ca, sizeof ca, &la));
    OK(zxv_w4_agent_card(bob.h, cb, sizeof cb, &lb));
    OK(zxv_w4_agent_card(carol.h, cc, sizeof cc, &lc));
    zxv_w4_dir *d = NULL;
    IS(zxv_w4_dir_create(0, &d), ZXV_E_RANGE);
    IS(zxv_w4_dir_create(ZXV_W4_DIR_MAX + 1, &d), ZXV_E_RANGE);
    IS(zxv_w4_dir_create(2, NULL), ZXV_E_ARG);
    OK(zxv_w4_dir_create(2, &d));
    uint32_t n = 9;
    OK(zxv_w4_dir_count(d, &n));
    CHECK(n == 0, "empty directory");
    IS(zxv_w4_dir_find(d, alice.id, out, sizeof out, &lo), ZXV_E_NOT_FOUND);
    OK(zxv_w4_dir_add(d, ca, la));
    OK(zxv_w4_dir_add(d, ca, la)); /* same binding again: replaced */
    OK(zxv_w4_dir_count(d, &n));
    CHECK(n == 1, "re-adding the same agent replaces, count stays 1");
    lo = 0;
    IS(zxv_w4_dir_find(d, alice.id, out, la - 1, &lo), ZXV_E_BUFFER);
    CHECK(lo == la, "find BUFFER reports the card length");
    OK(zxv_w4_dir_find(d, alice.id, out, sizeof out, &lo));
    CHECK(lo == la && memcmp(out, ca, la) == 0, "find returns the card that was added");
    /* an older binding of the same key is refused; a newer one replaces */
    agent_t old, newer;
    mk(cap, ACVP_SEED26, "alice", NULL, 999, &old);
    mk(cap, ACVP_SEED26, "alice-v2", NULL, 2000, &newer);
    static uint8_t co[ZXV_W4_CARD_MAX], cn[ZXV_W4_CARD_MAX];
    size_t lco, lcn;
    OK(zxv_w4_agent_card(old.h, co, sizeof co, &lco));
    OK(zxv_w4_agent_card(newer.h, cn, sizeof cn, &lcn));
    IS(zxv_w4_dir_add(d, co, lco), ZXV_E_STATE);
    OK(zxv_w4_dir_add(d, cn, lcn));
    OK(zxv_w4_dir_find(d, alice.id, out, sizeof out, &lo));
    CHECK(lo == lcn && memcmp(out, cn, lcn) == 0, "a newer binding replaces the entry");
    zxv_w4_agent_destroy(old.h);
    zxv_w4_agent_destroy(newer.h);
    OK(zxv_w4_dir_add(d, cb, lb));
    IS(zxv_w4_dir_add(d, cc, lc), ZXV_E_CAPACITY);
    cc[lc - 5] ^= 1;
    IS(zxv_w4_dir_add(d, cc, lc), ZXV_E_VERIFY);
    IS(zxv_w4_dir_add(d, cc, 3), ZXV_E_FORMAT);
    OK(zxv_w4_dir_count(d, &n));
    CHECK(n == 2, "refused cards are not added");
    IS(zxv_w4_dir_find(d, carol.id, out, sizeof out, &lo), ZXV_E_NOT_FOUND);
    IS(zxv_w4_dir_find(d, NULL, out, sizeof out, &lo), ZXV_E_ARG);
    IS(zxv_w4_dir_find(d, bob.id, out, sizeof out, NULL), ZXV_E_ARG);
    IS(zxv_w4_dir_add(NULL, ca, la), ZXV_E_ARG);
    IS(zxv_w4_dir_count(NULL, &n), ZXV_E_ARG);
    IS(zxv_w4_dir_count(d, NULL), ZXV_E_ARG);
    zxv_w4_dir_destroy(d);
    zxv_w4_dir_destroy(NULL);
}

static void test_manifest(void)
{
    zxv_w4_manifest *m = NULL;
    IS(zxv_w4_manifest_create(1, 2000, 2000, &m), ZXV_E_ARG);
    IS(zxv_w4_manifest_create(1, 1000, 2000, NULL), ZXV_E_ARG);
    OK(zxv_w4_manifest_create(3, 1000, 2000, &m));
    OK(zxv_w4_manifest_add_tool(m, "echo", 0, 60, 5, 0, 100));
    OK(zxv_w4_manifest_add_tool(m, "pay", 250, 10, 1, ZXV_W4_TOOL_MONEY, 64));
    OK(zxv_w4_manifest_add_tool(m, "profile", 0, 1, 1, ZXV_W4_TOOL_PII | ZXV_W4_TOOL_STREAM,
                                2048));
    IS(zxv_w4_manifest_add_tool(m, "pay", 1, 1, 1, 0, 1), ZXV_E_DUPLICATE);
    IS(zxv_w4_manifest_add_tool(m, "t", 1, 0, 1, 0, 1), ZXV_E_ARG);
    IS(zxv_w4_manifest_add_tool(m, "t", 1, 1, 0, 0, 1), ZXV_E_ARG);
    IS(zxv_w4_manifest_add_tool(m, "t", 1, 1, 1, 8, 1), ZXV_E_ARG);
    IS(zxv_w4_manifest_add_tool(m, "t", 1, 1, 1, 0, 2049), ZXV_E_ARG);
    IS(zxv_w4_manifest_add_tool(m, "T", 1, 1, 1, 0, 1), ZXV_E_ARG);
    IS(zxv_w4_manifest_add_tool(m, NULL, 1, 1, 1, 0, 1), ZXV_E_ARG);
    IS(zxv_w4_manifest_add_tool(m, "abcdefghijklmnopqrstuvwxyz012345", 1, 1, 1, 0, 1),
       ZXV_E_TOO_LONG);
    IS(zxv_w4_manifest_add_tool(NULL, "t", 1, 1, 1, 0, 1), ZXV_E_ARG);

    uint8_t aid[32], pid[32];
    uint32_t ver, nt;
    uint64_t iss, exp;
    int32_t sg = 9;
    OK(zxv_w4_manifest_info(m, aid, pid, &ver, &iss, &exp, &nt, &sg));
    uint8_t zero[32] = {0};
    CHECK(ver == 3 && iss == 1000 && exp == 2000 && nt == 3 && sg == 0 &&
              memcmp(aid, zero, 32) == 0,
          "unsigned manifest info");
    static uint8_t enc[ZXV_W4_MANIFEST_MAX], enc2[ZXV_W4_MANIFEST_MAX];
    size_t el = 0;
    IS(zxv_w4_manifest_encode(m, enc, sizeof enc, &el), ZXV_E_STATE);
    IS(zxv_w4_manifest_verify(m, alice.pk, 1500), ZXV_E_VERIFY);

    IS(zxv_w4_manifest_sign(cap_attest_only, alice.h, m), ZXV_E_CAP);
    IS(zxv_w4_manifest_sign(cap2, alice.h, m), ZXV_E_CAP); /* cap of another context */
    IS(zxv_w4_manifest_sign(cap, NULL, m), ZXV_E_ARG);
    IS(zxv_w4_manifest_sign(cap, alice.h, NULL), ZXV_E_ARG);
    OK(zxv_w4_manifest_sign(cap_sign_only, alice.h, m));
    OK(zxv_w4_manifest_info(m, aid, pid, NULL, NULL, NULL, NULL, &sg));
    CHECK(sg == 1 && memcmp(aid, alice.id, 32) == 0 && memcmp(pid, alice.peer, 32) == 0,
          "signing fixes the agent and peer ids");
    OK(zxv_w4_manifest_verify(m, alice.pk, 1000));
    OK(zxv_w4_manifest_verify(m, alice.pk, 1999));
    IS(zxv_w4_manifest_verify(m, alice.pk, 999), ZXV_E_STATE);
    IS(zxv_w4_manifest_verify(m, alice.pk, 2000), ZXV_E_STATE);
    IS(zxv_w4_manifest_verify(m, bob.pk, 1500), ZXV_E_VERIFY);
    IS(zxv_w4_manifest_verify(m, NULL, 1500), ZXV_E_ARG);

    el = 0;
    IS(zxv_w4_manifest_encode(m, enc, 100, &el), ZXV_E_BUFFER);
    size_t body = 4 + 1 + 32 + 32 + 4 + 8 + 8 + 1 + (1 + 4 + 24) + (1 + 3 + 24) + (1 + 7 + 24);
    CHECK(el == body + SIG, "manifest length %zu", el);
    OK(zxv_w4_manifest_encode(m, enc, sizeof enc, &el));
    CHECK(memcmp(enc, "W4MF", 4) == 0 && enc[4] == 1 && memcmp(enc + 5, alice.id, 32) == 0 &&
              le32(enc + 69) == 3 && le64(enc + 73) == 1000 && le64(enc + 81) == 2000 &&
              enc[89] == 3 && enc[90] == 4 && memcmp(enc + 91, "echo", 4) == 0 &&
              le64(enc + 95) == 0 && le32(enc + 103) == 60 && le32(enc + 107) == 5 &&
              le32(enc + 111) == 0 && le32(enc + 115) == 100 && enc[119] == 3 &&
              le64(enc + 123) == 250 && le32(enc + 139) == ZXV_W4_TOOL_MONEY,
          "manifest wire layout");
    zxv_w4_manifest *m2 = NULL;
    OK(zxv_w4_manifest_decode(enc, el, &m2));
    OK(zxv_w4_manifest_verify(m2, alice.pk, 1500));
    size_t el2 = 0;
    OK(zxv_w4_manifest_encode(m2, enc2, sizeof enc2, &el2));
    CHECK(el2 == el && memcmp(enc, enc2, el) == 0, "decode / encode round trip");
    zxv_w4_manifest_destroy(m2);
    enc[123] ^= 1; /* the price of "pay" */
    OK(zxv_w4_manifest_decode(enc, el, &m2));
    IS(zxv_w4_manifest_verify(m2, alice.pk, 1500), ZXV_E_VERIFY);
    zxv_w4_manifest_destroy(m2);
    enc[123] ^= 1;
    IS(zxv_w4_manifest_decode(enc, el - 1, &m2), ZXV_E_FORMAT);
    IS(zxv_w4_manifest_decode(enc, el + 1, &m2), ZXV_E_FORMAT);
    IS(zxv_w4_manifest_decode(enc, ZXV_W4_MANIFEST_MAX + 1, &m2), ZXV_E_FORMAT);
    enc[0] = 'X';
    IS(zxv_w4_manifest_decode(enc, el, &m2), ZXV_E_FORMAT);
    enc[0] = 'W';
    enc[91] = 'E'; /* "Echo": not a valid tool name */
    IS(zxv_w4_manifest_decode(enc, el, &m2), ZXV_E_FORMAT);
    enc[91] = 'e';
    IS(zxv_w4_manifest_decode(NULL, el, &m2), ZXV_E_ARG);
    IS(zxv_w4_manifest_decode(enc, el, NULL), ZXV_E_ARG);

    /* JSON face */
    char js[4096];
    size_t jl = 0;
    IS(zxv_w4_manifest_to_json(m, js, 10, &jl), ZXV_E_BUFFER);
    size_t need = jl;
    OK(zxv_w4_manifest_to_json(m, js, sizeof js, &jl));
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", alice.id[i]);
    char frag[100];
    snprintf(frag, sizeof frag, "\"agent_id\":\"%s\"", hex);
    CHECK(jl == need && strlen(js) == jl && strstr(js, frag) &&
              strstr(js, "\"signature_alg\":\"ML-DSA-65\"") &&
              strstr(js, "\"name\":\"pay\",\"price_vfv_minor\":250") &&
              strstr(js, "\"moves_money\":true") && strstr(js, "\"personal_data\":true") &&
              strstr(js, "\"version\":3"),
          "JSON discovery face: %s", js);
    IS(zxv_w4_manifest_to_json(m, js, sizeof js, NULL), ZXV_E_ARG);

    /* tools by index and by name */
    char tn[32];
    uint64_t price;
    uint32_t rate, burst, flags, mi, idx = 9;
    OK(zxv_w4_manifest_tool(m, 2, tn, &price, &rate, &burst, &flags, &mi));
    CHECK(strcmp(tn, "profile") == 0 && price == 0 && rate == 1 && burst == 1 &&
              flags == (ZXV_W4_TOOL_PII | ZXV_W4_TOOL_STREAM) && mi == 2048,
          "tool 2 fields");
    OK(zxv_w4_manifest_tool(m, 0, NULL, NULL, NULL, NULL, NULL, NULL));
    IS(zxv_w4_manifest_tool(m, 3, tn, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    OK(zxv_w4_manifest_find(m, "pay", &idx));
    CHECK(idx == 1, "find pay = 1");
    IS(zxv_w4_manifest_find(m, "nope", &idx), ZXV_E_NOT_FOUND);
    IS(zxv_w4_manifest_find(m, "pay", NULL), ZXV_E_ARG);
    IS(zxv_w4_manifest_find(m, NULL, &idx), ZXV_E_ARG);

    /* changing a signed manifest drops the signature */
    OK(zxv_w4_manifest_add_tool(m, "more", 1, 1, 1, 0, 1));
    OK(zxv_w4_manifest_info(m, NULL, NULL, NULL, NULL, NULL, &nt, &sg));
    CHECK(sg == 0 && nt == 4, "add_tool after signing: unsigned again");
    IS(zxv_w4_manifest_verify(m, alice.pk, 1500), ZXV_E_VERIFY);
    IS(zxv_w4_manifest_encode(m, enc, sizeof enc, &el), ZXV_E_STATE);
    char nm[16];
    for (int i = 4; i < 16; i++) {
        snprintf(nm, sizeof nm, "t%u", (unsigned) i & 0xffu);
        OK(zxv_w4_manifest_add_tool(m, nm, 1, 1, 1, 0, 1));
    }
    IS(zxv_w4_manifest_add_tool(m, "t16", 1, 1, 1, 0, 1), ZXV_E_CAPACITY);
    OK(zxv_w4_manifest_sign(cap, alice.h, m));
    OK(zxv_w4_manifest_encode(m, enc, sizeof enc, &el));
    CHECK(el <= ZXV_W4_MANIFEST_MAX, "a full manifest fits ZXV_W4_MANIFEST_MAX");
    OK(zxv_w4_manifest_to_json(m, js, sizeof js, &jl));
    zxv_w4_manifest_destroy(m);
    zxv_w4_manifest_destroy(NULL);
    IS(zxv_w4_manifest_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
}

static void test_consent(void)
{
    const uint8_t *pay = (const uint8_t *) "50 VFV to bob";
    uint8_t act[32], ref[32];
    OK(zxv_w4_action_digest("pay", alice.id, bob.id, 42, pay, 13, act));
    ref_digest("pay", alice.id, bob.id, 42, pay, 13, ref);
    CHECK(memcmp(act, ref, 32) == 0, "action digest = documented SHA3-256 construction");
    uint8_t other[32];
    OK(zxv_w4_action_digest("pay", alice.id, bob.id, 43, pay, 13, other));
    CHECK(memcmp(other, act, 32) != 0, "the digest binds the request id");
    OK(zxv_w4_action_digest("pay", alice.id, bob.id, 42, NULL, 0, other));
    ref_digest("pay", alice.id, bob.id, 42, NULL, 0, ref);
    CHECK(memcmp(other, ref, 32) == 0, "empty payload digest");
    static uint8_t big[2049];
    IS(zxv_w4_action_digest("pay", alice.id, bob.id, 42, big, 2049, other), ZXV_E_TOO_LONG);
    OK(zxv_w4_action_digest("pay", alice.id, bob.id, 42, big, 2048, other));
    IS(zxv_w4_action_digest("Pay", alice.id, bob.id, 42, pay, 13, other), ZXV_E_ARG);
    IS(zxv_w4_action_digest("pay", NULL, bob.id, 42, pay, 13, other), ZXV_E_ARG);
    IS(zxv_w4_action_digest("pay", alice.id, bob.id, 42, NULL, 13, other), ZXV_E_ARG);
    IS(zxv_w4_action_digest("pay", alice.id, bob.id, 42, pay, 13, NULL), ZXV_E_ARG);

    /* issue */
    static uint8_t tok[TOK], tok2[TOK], tok3[TOK], t[TOK];
    const uint8_t nonce[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    IS(zxv_w4_consent_issue(cap_sign_only, human.h, alice.id, act, 1, 1000, 100, 200, nonce, tok),
       ZXV_E_CAP);
    IS(zxv_w4_consent_issue(cap2, human.h, alice.id, act, 1, 1000, 100, 200, nonce, tok),
       ZXV_E_CAP);
    IS(zxv_w4_consent_issue(cap, human.h, alice.id, act, 0, 1000, 100, 200, nonce, tok), ZXV_E_ARG);
    IS(zxv_w4_consent_issue(cap, human.h, alice.id, act, 4, 1000, 100, 200, nonce, tok), ZXV_E_ARG);
    IS(zxv_w4_consent_issue(cap, human.h, alice.id, act, 1, 1000, 200, 200, nonce, tok), ZXV_E_ARG);
    IS(zxv_w4_consent_issue(cap, NULL, alice.id, act, 1, 1000, 100, 200, nonce, tok), ZXV_E_ARG);
    IS(zxv_w4_consent_issue(cap, human.h, NULL, act, 1, 1000, 100, 200, nonce, tok), ZXV_E_ARG);
    IS(zxv_w4_consent_issue(cap, human.h, alice.id, act, 1, 1000, 100, 200, nonce, NULL),
       ZXV_E_ARG);
    OK(zxv_w4_consent_issue(cap_attest_only, human.h, alice.id, act, ZXV_W4_CONSENT_MONEY, 1000,
                            100, 200, nonce, tok));
    CHECK(memcmp(tok, "W4CS", 4) == 0 && memcmp(tok + 4, human.id, 32) == 0 &&
              memcmp(tok + 36, alice.id, 32) == 0 && memcmp(tok + 68, act, 32) == 0 &&
              le32(tok + 100) == 1 && le64(tok + 104) == 1000 && le64(tok + 112) == 100 &&
              le64(tok + 120) == 200 && memcmp(tok + 128, nonce, 16) == 0,
          "consent token layout");
    uint8_t hid[32], aid[32], ac[32], nn[16];
    uint32_t sc;
    uint64_t mx, is, ex;
    OK(zxv_w4_consent_info(tok, hid, aid, ac, &sc, &mx, &is, &ex, nn));
    CHECK(memcmp(hid, human.id, 32) == 0 && memcmp(aid, alice.id, 32) == 0 &&
              memcmp(ac, act, 32) == 0 && sc == 1 && mx == 1000 && is == 100 && ex == 200 &&
              memcmp(nn, nonce, 16) == 0,
          "consent_info fields");
    memcpy(t, tok, TOK);
    t[1] = 'X';
    IS(zxv_w4_consent_info(t, hid, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_consent_info(NULL, hid, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    OK(zxv_w4_consent_issue(cap, human.h, alice.id, act, ZXV_W4_CONSENT_MONEY, 1000, 100, 200,
                            NULL, tok2));
    OK(zxv_w4_consent_issue(cap, human.h, alice.id, act, ZXV_W4_CONSENT_MONEY, 1000, 100, 200,
                            NULL, tok3));
    CHECK(memcmp(tok2 + 128, tok3 + 128, 16) != 0, "a NULL nonce is fresh each time");

    /* check */
    zxv_w4_consent_log *log = NULL;
    IS(zxv_w4_consent_log_create(0, &log), ZXV_E_RANGE);
    IS(zxv_w4_consent_log_create(ZXV_W4_CONSENT_LOG_MAX + 1, &log), ZXV_E_RANGE);
    IS(zxv_w4_consent_log_create(4, NULL), ZXV_E_ARG);
    OK(zxv_w4_consent_log_create(4, &log));
    const uint32_t M = ZXV_W4_CONSENT_MONEY;
    IS(zxv_w4_consent_check(log, NULL, human.pk, alice.id, act, M, 1, 150), ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, M, 1001, 150), ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, ZXV_W4_CONSENT_PII, 0, 150),
       ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, M | ZXV_W4_CONSENT_PII, 1, 150),
       ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, bob.id, act, M, 1, 150), ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, other, M, 1, 150), ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human2.pk, alice.id, act, M, 1, 150), ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, M, 1, 99), ZXV_E_CONSENT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, M, 1, 200), ZXV_E_CONSENT);
    /* a forged token: the human_id says "human", fields changed after signing */
    memcpy(t, tok, TOK);
    put64(t + 104, 5000);
    IS(zxv_w4_consent_check(log, t, human.pk, alice.id, act, M, 4000, 150), ZXV_E_CONSENT);
    memcpy(t, tok, TOK);
    t[TOK - 1] ^= 1;
    IS(zxv_w4_consent_check(log, t, human.pk, alice.id, act, M, 1, 150), ZXV_E_CONSENT);
    t[0] = 'X';
    IS(zxv_w4_consent_check(log, t, human.pk, alice.id, act, M, 1, 150), ZXV_E_FORMAT);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, 0, 1, 150), ZXV_E_ARG);
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, 4, 1, 150), ZXV_E_ARG);
    IS(zxv_w4_consent_check(log, tok, NULL, alice.id, act, M, 1, 150), ZXV_E_ARG);
    IS(zxv_w4_consent_check(NULL, tok, human.pk, alice.id, act, M, 1, 150), ZXV_E_ARG);
    uint32_t rem = 9;
    OK(zxv_w4_consent_log_prune(log, 0, &rem));
    CHECK(rem == 0, "refused checks record nothing");
    /* the boundaries that pass: amount == max, first and last valid ms */
    OK(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, M, 1000, 199));
    IS(zxv_w4_consent_check(log, tok, human.pk, alice.id, act, M, 1000, 150), ZXV_E_CONSENT);
    OK(zxv_w4_consent_check(log, tok2, human.pk, alice.id, act, M, 0, 100));
    /* revoke before use */
    OK(zxv_w4_consent_revoke(log, tok3, 150));
    OK(zxv_w4_consent_revoke(log, tok3, 150)); /* twice */
    OK(zxv_w4_consent_log_prune(log, 0, &rem));
    CHECK(rem == 3, "two used + one revoked");
    IS(zxv_w4_consent_check(log, tok3, human.pk, alice.id, act, M, 1, 150), ZXV_E_CONSENT);
    IS(zxv_w4_consent_revoke(log, t, 150), ZXV_E_FORMAT);
    IS(zxv_w4_consent_revoke(log, NULL, 150), ZXV_E_ARG);
    IS(zxv_w4_consent_revoke(NULL, tok3, 150), ZXV_E_ARG);
    /* PII scope, an expired revoke is a no-op */
    static uint8_t tp[TOK], tq[TOK], tr[TOK];
    OK(zxv_w4_consent_issue(cap, human.h, alice.id, act, ZXV_W4_CONSENT_PII, 0, 100, 300, NULL, tp));
    OK(zxv_w4_consent_revoke(log, tp, 300));
    OK(zxv_w4_consent_log_prune(log, 0, &rem));
    CHECK(rem == 3, "revoking an expired token holds nothing");
    OK(zxv_w4_consent_check(log, tp, human.pk, alice.id, act, ZXV_W4_CONSENT_PII, 999999, 250));
    OK(zxv_w4_consent_log_prune(log, 0, &rem));
    CHECK(rem == 4, "log holds 4");
    /* full: fail closed until expired entries can be pruned */
    OK(zxv_w4_consent_issue(cap, human.h, alice.id, act, M, 10, 100, 400, NULL, tq));
    IS(zxv_w4_consent_check(log, tq, human.pk, alice.id, act, M, 1, 150), ZXV_E_CAPACITY);
    IS(zxv_w4_consent_revoke(log, tq, 150), ZXV_E_CAPACITY);
    OK(zxv_w4_consent_check(log, tq, human.pk, alice.id, act, M, 1, 250)); /* 200s pruned */
    OK(zxv_w4_consent_log_prune(log, 250, &rem));
    CHECK(rem == 2, "prune keeps the PII (300) and the new (400) entries");
    OK(zxv_w4_consent_log_prune(log, 400, &rem));
    CHECK(rem == 0, "everything expired by 400");
    OK(zxv_w4_consent_log_prune(log, 400, NULL));
    IS(zxv_w4_consent_log_prune(NULL, 400, &rem), ZXV_E_ARG);
    /* human2 cannot consent for human */
    OK(zxv_w4_consent_issue(cap, human2.h, alice.id, act, M, 10, 100, 400, NULL, tr));
    IS(zxv_w4_consent_check(log, tr, human.pk, alice.id, act, M, 1, 150), ZXV_E_CONSENT);
    OK(zxv_w4_consent_check(log, tr, human2.pk, alice.id, act, M, 1, 150));
    zxv_w4_consent_log_destroy(log);
    zxv_w4_consent_log_destroy(NULL);
}

static void test_envelopes(void)
{
    static uint8_t tok[TOK], env[ZXV_W4_ENV_MAX], env2[ZXV_W4_ENV_MAX], rep[ZXV_W4_ENV_MAX],
        t[ZXV_W4_ENV_MAX], tk[TOK];
    const uint8_t *pay = (const uint8_t *) "50 VFV";
    uint8_t act[32];
    OK(zxv_w4_action_digest("pay", alice.id, bob.id, 42, pay, 6, act));
    OK(zxv_w4_consent_issue(cap, human.h, alice.id, act, ZXV_W4_CONSENT_MONEY, 5000, 100, 900,
                            NULL, tok));
    size_t want = 104 + 3 + 6 + TOK + SIG, el = 0;
    IS(zxv_w4_env_seal(cap, alice.h, ZXV_W4_MSG_REQUEST, bob.id, 42, 0, "pay", 0, pay, 6, tok,
                       150, env, want - 1, &el),
       ZXV_E_BUFFER);
    CHECK(el == want, "seal BUFFER reports %zu", want);
    OK(zxv_w4_env_seal(cap, alice.h, ZXV_W4_MSG_REQUEST, bob.id, 42, 0, "pay", 0, pay, 6, tok,
                       150, env, sizeof env, &el));
    CHECK(el == want, "sealed envelope length");
    CHECK(memcmp(env, "W4EV", 4) == 0 && env[4] == 1 && env[5] == 1 && env[6] == 2 &&
              memcmp(env + 7, alice.id, 32) == 0 && memcmp(env + 39, bob.id, 32) == 0 &&
              le64(env + 71) == 1 && le64(env + 79) == 150 && le64(env + 87) == 42 &&
              le32(env + 95) == 0 && env[99] == 3 && memcmp(env + 100, "pay", 3) == 0 &&
              le32(env + 103) == 6 && memcmp(env + 107, pay, 6) == 0 &&
              memcmp(env + 113, tok, TOK) == 0,
          "envelope layout; the BUFFER call used no sequence number (seq 1)");
    int32_t kind;
    uint32_t flags, chunk, plen;
    uint8_t from[32], to[32];
    uint64_t seq, ts, req;
    char tool[32];
    OK(zxv_w4_env_decode(env, el, &kind, &flags, from, to, &seq, &ts, &req, &chunk, tool, &plen));
    CHECK(kind == ZXV_W4_MSG_REQUEST && flags == ZXV_W4_MF_CONSENT &&
              memcmp(from, alice.id, 32) == 0 && memcmp(to, bob.id, 32) == 0 && seq == 1 &&
              ts == 150 && req == 42 && chunk == 0 && strcmp(tool, "pay") == 0 && plen == 6,
          "env_decode fields");
    OK(zxv_w4_env_decode(env, el, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    OK(zxv_w4_env_verify(env, el, alice.pk));
    IS(zxv_w4_env_verify(env, el, bob.pk), ZXV_E_VERIFY);
    memcpy(t, env, el);
    t[109] ^= 1; /* the payload */
    IS(zxv_w4_env_verify(t, el, alice.pk), ZXV_E_VERIFY);
    memcpy(t, env, el);
    t[el - 1] ^= 1; /* the signature */
    IS(zxv_w4_env_verify(t, el, alice.pk), ZXV_E_VERIFY);
    IS(zxv_w4_env_verify(env, el - 1, alice.pk), ZXV_E_FORMAT);
    IS(zxv_w4_env_verify(env, el, NULL), ZXV_E_ARG);
    uint8_t pb[16];
    size_t pl = 0;
    IS(zxv_w4_env_payload(env, el, pb, 5, &pl), ZXV_E_BUFFER);
    CHECK(pl == 6, "payload BUFFER reports 6");
    OK(zxv_w4_env_payload(env, el, pb, sizeof pb, &pl));
    CHECK(pl == 6 && memcmp(pb, pay, 6) == 0, "payload out");
    IS(zxv_w4_env_payload(env, el, pb, sizeof pb, NULL), ZXV_E_ARG);
    OK(zxv_w4_env_consent(env, el, tk));
    CHECK(memcmp(tk, tok, TOK) == 0, "attached consent token out");

    /* the receiver: recompute the digest from the envelope and check consent */
    uint8_t act2[32];
    OK(zxv_w4_action_digest(tool, from, to, req, pb, pl, act2));
    zxv_w4_consent_log *log;
    OK(zxv_w4_consent_log_create(8, &log));
    OK(zxv_w4_consent_check(log, tk, human.pk, from, act2, ZXV_W4_CONSENT_MONEY, 50, 160));
    IS(zxv_w4_consent_check(log, tk, human.pk, from, act2, ZXV_W4_CONSENT_MONEY, 50, 161),
       ZXV_E_CONSENT);
    zxv_w4_consent_log_destroy(log);

    /* next seal: seq 2; no consent; FINAL; empty payload */
    size_t el2 = 0;
    OK(zxv_w4_env_seal(cap_sign_only, alice.h, ZXV_W4_MSG_CANCEL, bob.id, 42, 3, "pay",
                       ZXV_W4_MF_FINAL, NULL, 0, NULL, 151, env2, sizeof env2, &el2));
    OK(zxv_w4_env_decode(env2, el2, &kind, &flags, NULL, NULL, &seq, NULL, NULL, &chunk, NULL,
                         &plen));
    CHECK(el2 == 104 + 3 + SIG && kind == ZXV_W4_MSG_CANCEL && flags == ZXV_W4_MF_FINAL &&
              seq == 2 && chunk == 3 && plen == 0,
          "second envelope: seq 2, no consent, FINAL");
    OK(zxv_w4_env_verify(env2, el2, alice.pk));
    IS(zxv_w4_env_consent(env2, el2, tk), ZXV_E_NOT_FOUND);
    pl = 9;
    OK(zxv_w4_env_payload(env2, el2, NULL, 0, &pl));
    CHECK(pl == 0, "empty payload");

    /* reply */
    size_t rl = 0;
    OK(zxv_w4_env_reply(cap, bob.h, env, el, ZXV_W4_MSG_RESPONSE, 0, ZXV_W4_MF_FINAL,
                        (const uint8_t *) "ok", 2, 170, rep, sizeof rep, &rl));
    OK(zxv_w4_env_decode(rep, rl, &kind, &flags, from, to, &seq, &ts, &req, &chunk, tool, &plen));
    CHECK(kind == ZXV_W4_MSG_RESPONSE && flags == ZXV_W4_MF_FINAL &&
              memcmp(from, bob.id, 32) == 0 && memcmp(to, alice.id, 32) == 0 && seq == 1 &&
              ts == 170 && req == 42 && strcmp(tool, "pay") == 0 && plen == 2 &&
              rl == 104 + 3 + 2 + SIG,
          "reply: parties swapped, same request id and tool, bob's seq 1");
    OK(zxv_w4_env_verify(rep, rl, bob.pk));
    IS(zxv_w4_env_verify(rep, rl, alice.pk), ZXV_E_VERIFY);
    IS(zxv_w4_env_reply(cap, carol.h, env, el, ZXV_W4_MSG_RESPONSE, 0, 0, NULL, 0, 170, rep,
                        sizeof rep, &rl),
       ZXV_E_ARG); /* not addressed to carol */
    IS(zxv_w4_env_reply(cap, alice.h, rep, rl, ZXV_W4_MSG_RESPONSE, 0, 0, NULL, 0, 170, t,
                        sizeof t, &rl),
       ZXV_E_ARG); /* a reply to a RESPONSE */
    IS(zxv_w4_env_reply(cap, bob.h, env, el, ZXV_W4_MSG_REQUEST, 0, 0, NULL, 0, 170, rep,
                        sizeof rep, &rl),
       ZXV_E_ARG);
    IS(zxv_w4_env_reply(cap, bob.h, env, el, ZXV_W4_MSG_CANCEL, 0, 0, NULL, 0, 170, rep,
                        sizeof rep, &rl),
       ZXV_E_ARG);
    IS(zxv_w4_env_reply(cap, bob.h, env, el - 2, ZXV_W4_MSG_ERROR, 0, 0, NULL, 0, 170, rep,
                        sizeof rep, &rl),
       ZXV_E_FORMAT);
    IS(zxv_w4_env_reply(cap_attest_only, bob.h, env, el, ZXV_W4_MSG_ERROR, 0, 0, NULL, 0, 170,
                        rep, sizeof rep, &rl),
       ZXV_E_CAP);
    IS(zxv_w4_env_reply(cap, bob.h, env, el, ZXV_W4_MSG_STREAM, 0, 2, NULL, 0, 170, rep,
                        sizeof rep, &rl),
       ZXV_E_ARG);
    OK(zxv_w4_env_reply(cap, bob.h, env, el, ZXV_W4_MSG_STREAM, 1, 0, (const uint8_t *) "c", 1,
                        171, rep, sizeof rep, &rl));
    OK(zxv_w4_env_decode(rep, rl, NULL, NULL, NULL, NULL, &seq, NULL, NULL, &chunk, NULL, NULL));
    CHECK(seq == 2 && chunk == 1, "refused replies used no sequence number");

    /* seal errors */
    static uint8_t big[2049];
    IS(zxv_w4_env_seal(cap, alice.h, 0, bob.id, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 6, bob.id, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pay", ZXV_W4_MF_CONSENT, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pa y", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, NULL, 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, NULL, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pay", 0, NULL, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, NULL), ZXV_E_ARG);
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pay", 0, big, 2049, NULL, 1, env, sizeof env, &el), ZXV_E_TOO_LONG);
    memcpy(tk, tok, TOK);
    tk[2] = 0;
    IS(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pay", 0, pay, 6, tk, 1, env, sizeof env, &el), ZXV_E_FORMAT);
    IS(zxv_w4_env_seal(cap_attest_only, alice.h, 1, bob.id, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_CAP);
    IS(zxv_w4_env_seal(cap2, alice.h, 1, bob.id, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_CAP);
    IS(zxv_w4_env_seal(cap, NULL, 1, bob.id, 1, 0, "pay", 0, pay, 6, NULL, 1, env, sizeof env, &el), ZXV_E_ARG);
    OK(zxv_w4_env_seal(cap, alice.h, 1, bob.id, 1, 0, "pay", 0, big, 2048, tok, 1, env, sizeof env, &el));
    CHECK(el == 104 + 3 + 2048 + TOK + SIG && el <= ZXV_W4_ENV_MAX, "largest envelope fits");
    OK(zxv_w4_env_decode(env, el, NULL, NULL, NULL, NULL, &seq, NULL, NULL, NULL, NULL, NULL));
    CHECK(seq == 3, "refused seals used no sequence number (alice at 3)");

    /* decode errors */
    IS(zxv_w4_env_decode(env, el + 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_env_decode(env, ZXV_W4_ENV_MAX + 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    memcpy(t, env, el);
    t[5] = 9; /* kind */
    IS(zxv_w4_env_decode(t, el, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_FORMAT);
    IS(zxv_w4_env_decode(NULL, el, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    IS(zxv_w4_env_consent(env, el, NULL), ZXV_E_ARG);
    IS(zxv_w4_env_consent(t, el, tk), ZXV_E_FORMAT);
}

/* Fail closed: once a context halts, its key handles and caps refuse. */
static void test_poison(void)
{
    agent_t z;
    mk(cap2, NULL, "zed", NULL, 1, &z);
    zxv_w4_manifest *m;
    OK(zxv_w4_manifest_create(1, 1, 2, &m));
    OK(zxv_w4_manifest_sign(cap2, z.h, m));
    OK(zxv_ctx_halt(ctx2, "test"));
    zxv_w4_agent *x = NULL;
    uint8_t id[32], tk[TOK], buf[64];
    size_t n;
    IS(zxv_w4_agent_create(cap2, NULL, "ok", NULL, 0, &x), ZXV_E_POISONED);
    IS(zxv_w4_agent_info(z.h, id, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_w4_agent_card(z.h, NULL, 0, &n), ZXV_E_POISONED);
    IS(zxv_w4_manifest_sign(cap2, z.h, m), ZXV_E_POISONED);
    IS(zxv_w4_consent_issue(cap2, z.h, id, id, 1, 1, 1, 2, NULL, tk), ZXV_E_POISONED);
    IS(zxv_w4_env_seal(cap2, z.h, 1, id, 1, 0, "pay", 0, NULL, 0, NULL, 1, buf, sizeof buf, &n),
       ZXV_E_POISONED);
    IS(zxv_w4_env_seal(cap2, NULL, 1, id, 1, 0, "pay", 0, NULL, 0, NULL, 1, buf, sizeof buf, &n),
       ZXV_E_POISONED); /* poison first, before argument checks */
    OK(zxv_w4_manifest_verify(m, z.pk, 1)); /* pure: no context involved */
    zxv_w4_manifest_destroy(m);
    zxv_w4_agent_destroy(z.h); /* destroy still works */
}

int main(void)
{
    uint32_t m = 0;
    OK(zxv_features2(&m));
    if (!(m & ZXV_FEAT2_WEB4)) {
        zxv_ctx *c = NULL;
        zxv_cap *k = NULL;
        zxv_w4_agent *a = NULL;
        zxv_ctx_create(&c);
        zxv_cap_create(c, 1, NULL, ZXV_CAP_ALL_1_4, &k);
        IS(zxv_w4_agent_create(k, NULL, "a", NULL, 0, &a), ZXV_E_NOTIMPL);
        zxv_cap_destroy(k);
        zxv_ctx_destroy(c);
        printf("test_zxv_abi15_web4: %d checks, %d failures (web4 not built)\n", checks, failures);
        return failures ? 1 : 0;
    }
    OK(zxv_ctx_create(&ctx));
    OK(zxv_ctx_create(&ctx2));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_4, &cap));
    OK(zxv_cap_create(ctx2, 2, NULL, ZXV_CAP_ALL_1_4, &cap2));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_ATTEST, &cap_attest_only));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_CRYPTO_SIGN, &cap_sign_only));
    test_identity();
    mk(cap, NULL, "human-ana", NULL, 1, &human);
    mk(cap, NULL, "human-ben", NULL, 1, &human2);
    test_directory();
    test_manifest();
    test_consent();
    test_envelopes();
    test_poison();
    zxv_w4_agent *keys[] = {alice.h, bob.h, carol.h, human.h, human2.h};
    for (unsigned i = 0; i < 5; i++) zxv_w4_agent_destroy(keys[i]);
    zxv_cap_destroy(cap_attest_only);
    zxv_cap_destroy(cap_sign_only);
    zxv_cap_destroy(cap);
    zxv_cap_destroy(cap2);
    zxv_ctx_destroy(ctx);
    zxv_ctx_destroy(ctx2);
    printf("test_zxv_abi15_web4: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
