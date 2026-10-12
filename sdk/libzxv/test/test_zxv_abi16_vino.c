/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi16_vino.c — conformance test of the ABI 1.6 vino group,
 * linked against the shared library only (no kernel headers):
 *   - version, feature bit, capability mask, status names and classes;
 *   - the play lane: accounts, backing, the exact 1.8x coverage boundary on
 *     issue and on withdrawal, ladder faces, one active rail per voucher,
 *     transfers, rail switches, redemption retiring a voucher with nothing
 *     paid out, listings, both capacity limits, and a byte-for-byte
 *     unchanged sandbox after every refusal;
 *   - a seeded random walk checked call by call against a model kept here;
 *   - requests: the 160 documented bytes, a known SHA-256 id, the round
 *     trip, and every malformed field; no paid chance in the live lane;
 *   - receipts: the 88-byte message, ML-DSA-87 sign and verify, a flipped
 *     bit, another wallet, another time, the window and expiry;
 *   - the daily meter: the exact cap, refusals counting nothing, new days,
 *     the clock going back;
 *   - capability gating, context binding and poisoning. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)
#define IS(call, want)                                                                             \
    do {                                                                                           \
        int32_t rc_ = (call);                                                                      \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_),                     \
              zxv_status_name(want));                                                              \
    } while (0)
#define OK(call) IS(call, ZXV_OK)

static zxv_ctx *ctx;
static zxv_cap *root, *vino, *sign;
static uint32_t feat2;

/* ---- a snapshot of everything a sandbox shows ---- */
#define SN_ACCTS 16
#define SN_VOUCH 64
typedef struct {
    uint32_t owner[SN_ACCTS], count[SN_ACCTS];
    int32_t kind[SN_ACCTS];
    uint64_t backing[SN_ACCTS], faces[SN_ACCTS];
    uint64_t face[SN_VOUCH];
    uint32_t iss[SN_VOUCH], hold[SN_VOUCH];
    int32_t rail[SN_VOUCH], state[SN_VOUCH], vrc[SN_VOUCH], arc[SN_ACCTS];
} snap_t;

static void snap(zxv_vino_play *p, snap_t *s)
{
    memset(s, 0, sizeof *s);
    for (uint32_t a = 0; a < SN_ACCTS; a++)
        s->arc[a] = zxv_vino_account_info(vino, p, a, &s->owner[a], &s->kind[a], &s->backing[a],
                                          &s->faces[a], &s->count[a]);
    for (uint64_t v = 0; v < SN_VOUCH; v++)
        s->vrc[v] = zxv_vino_voucher_info(vino, p, v, &s->face[v], &s->iss[v], &s->hold[v],
                                          &s->rail[v], &s->state[v]);
}
#define UNCHANGED(p, before, call, want)                                                           \
    do {                                                                                           \
        snap_t a_;                                                                                 \
        IS(call, want);                                                                            \
        snap((p), &a_);                                                                            \
        CHECK(!memcmp(&a_, (before), sizeof a_), "%s changed the sandbox", #call);                 \
    } while (0)

static void test_basics(void)
{
    uint32_t maj = 0, min = 0, pat = 0;
    OK(zxv_version(&maj, &min, &pat));
    CHECK(maj == 1 && min == 6 && pat == 0, "abi %u.%u.%u", maj, min, pat);
    CHECK(ZXV_ABI_VERSION_MINOR == 6, "header minor");
    CHECK(feat2 & ZXV_FEAT2_VINO, "feature bit 0x%x", feat2);
    CHECK(ZXV_FEAT2_VINO == 0x100u && ZXV_CAP_VINO == (UINT64_C(1) << 15), "numbers");
    CHECK(ZXV_CAP_ALL_1_6 == (ZXV_CAP_ALL_1_5 | ZXV_CAP_VINO), "cap mask");
    zxv_cap *c = NULL;
    IS(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_6 | (UINT64_C(1) << 16), &c), ZXV_E_ARG);
    CHECK(c == NULL, "no cap on refusal");
    static const struct {
        int32_t code;
        const char *name;
        int32_t cls;
    } t[] = {
        {ZXV_E_RAIL, "ZXV_E_RAIL", ZXV_CLASS_REFUSED},
        {ZXV_E_COVERAGE, "ZXV_E_COVERAGE", ZXV_CLASS_REFUSED},
        {ZXV_E_LANE, "ZXV_E_LANE", ZXV_CLASS_USAGE},
        {ZXV_E_EXPIRED, "ZXV_E_EXPIRED", ZXV_CLASS_REFUSED},
        {ZXV_E_CHANCE, "ZXV_E_CHANCE", ZXV_CLASS_REFUSED},
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        CHECK(t[i].code == -46 - (int32_t) i, "%s is %d", t[i].name, t[i].code);
        CHECK(!strcmp(zxv_status_name(t[i].code), t[i].name), "name of %d", t[i].code);
        CHECK(zxv_status_class(t[i].code) == t[i].cls, "class of %s", t[i].name);
    }
    CHECK(!strcmp(zxv_status_name(-51), "ZXV_E_UNKNOWN"), "-51 is reserved");
}

/* An issuer with `backing`, two holders. */
static zxv_vino_play *sandbox(uint64_t backing, uint32_t *iss, uint32_t *h1, uint32_t *h2)
{
    zxv_vino_play *p = NULL;
    OK(zxv_vino_play_create(vino, &p));
    OK(zxv_vino_play_open(vino, p, 700, 1, iss));
    OK(zxv_vino_play_open(vino, p, 801, 0, h1));
    OK(zxv_vino_play_open(vino, p, 802, 0, h2));
    if (backing) OK(zxv_vino_play_back(vino, p, *iss, backing));
    return p;
}

static void test_accounts(void)
{
    zxv_vino_play *p = NULL;
    IS(zxv_vino_play_create(vino, NULL), ZXV_E_ARG);
    OK(zxv_vino_play_create(vino, &p));
    uint32_t a = 99, owner = 0, count = 9;
    int32_t kind = -1;
    uint64_t backing = 9, faces = 9;
    IS(zxv_vino_play_open(vino, p, 1, 1, NULL), ZXV_E_ARG);
    IS(zxv_vino_play_open(vino, p, 1, 2, &a), ZXV_E_ARG);
    IS(zxv_vino_play_open(vino, p, 1, -1, &a), ZXV_E_ARG);
    IS(zxv_vino_play_open(vino, NULL, 1, 1, &a), ZXV_E_ARG);
    CHECK(a == 99, "no account on refusal");
    IS(zxv_vino_account_info(vino, p, 0, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    for (uint32_t i = 0; i < ZXV_VINO_MAX_ACCOUNTS; i++) {
        OK(zxv_vino_play_open(vino, p, 1000 + i, (int32_t) (i & 1), &a));
        CHECK(a == i, "account %u got id %u", i, a);
    }
    IS(zxv_vino_play_open(vino, p, 1, 0, &a), ZXV_E_CAPACITY);
    OK(zxv_vino_account_info(vino, p, 7, &owner, &kind, &backing, &faces, &count));
    CHECK(owner == 1007 && kind == 1 && backing == 0 && faces == 0 && count == 0, "info 7");
    OK(zxv_vino_account_info(vino, p, 8, &owner, &kind, NULL, NULL, NULL));
    CHECK(owner == 1008 && kind == 0, "info 8");
    OK(zxv_vino_account_info(vino, p, 9, NULL, NULL, NULL, NULL, NULL));
    IS(zxv_vino_account_info(vino, p, ZXV_VINO_MAX_ACCOUNTS, NULL, NULL, NULL, NULL, NULL),
       ZXV_E_NOT_FOUND);
    IS(zxv_vino_account_info(vino, NULL, 0, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
    zxv_vino_play_destroy(NULL);
}

static void test_backing(void)
{
    uint32_t iss, h1, h2;
    zxv_vino_play *p = sandbox(0, &iss, &h1, &h2);
    snap_t s;
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_play_back(vino, p, h1, 5), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_play_back(vino, p, 3, 5), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_play_back(vino, p, iss, 0), ZXV_E_RANGE);
    UNCHANGED(p, &s, zxv_vino_play_back(vino, p, iss, ZXV_VINO_AMOUNT_MAX + 1), ZXV_E_RANGE);
    UNCHANGED(p, &s, zxv_vino_play_back(vino, NULL, iss, 5), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, iss, 1), ZXV_E_FUNDS);
    /* backing reaches exactly 2^62 and stops there */
    for (int i = 0; i < 8; i++) OK(zxv_vino_play_back(vino, p, iss, ZXV_VINO_AMOUNT_MAX));
    uint64_t b = 0;
    OK(zxv_vino_account_info(vino, p, iss, NULL, NULL, &b, NULL, NULL));
    CHECK(b == (UINT64_C(1) << 62), "backing %llu", (unsigned long long) b);
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_play_back(vino, p, iss, 1), ZXV_E_RANGE);
    /* with nothing outstanding all of it can come back out */
    for (int i = 0; i < 8; i++) OK(zxv_vino_play_unback(vino, p, iss, ZXV_VINO_AMOUNT_MAX));
    OK(zxv_vino_account_info(vino, p, iss, NULL, NULL, &b, NULL, NULL));
    CHECK(b == 0, "all withdrawn");
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, iss, 0), ZXV_E_RANGE);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, h2, 1), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, 77, 1), ZXV_E_NOT_FOUND);
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
}

static void test_coverage(void)
{
    uint32_t iss, h1, h2;
    /* backing 9 covers outstanding 5 exactly: 90 >= 90 */
    zxv_vino_play *p = sandbox(9, &iss, &h1, &h2);
    uint64_t id = 0, id2 = 0;
    snap_t s;
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 8, ZXV_VINO_RAIL_BEARER, &id),
              ZXV_E_COVERAGE);
    CHECK(id == 0, "no id on refusal");
    OK(zxv_vino_issue(vino, p, iss, h1, 5, ZXV_VINO_RAIL_BEARER, &id));
    CHECK(id == 1, "first id %llu", (unsigned long long) id);
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 1, ZXV_VINO_RAIL_GHOST, &id2),
              ZXV_E_COVERAGE);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, iss, 1), ZXV_E_COVERAGE);
    /* one more unit of backing lets a 1 through: 100 >= 108 is false, so 1
     * needs 11: 110 >= 108 */
    OK(zxv_vino_play_back(vino, p, iss, 1));
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h2, 1, ZXV_VINO_RAIL_GHOST, &id2),
              ZXV_E_COVERAGE);
    OK(zxv_vino_play_back(vino, p, iss, 1));
    OK(zxv_vino_issue(vino, p, iss, h2, 1, ZXV_VINO_RAIL_GHOST, &id2));
    CHECK(id2 == 2, "second id");
    /* 11 covers 6 (110 >= 108); withdrawing 1 would leave 100 < 108 */
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, iss, 1), ZXV_E_COVERAGE);
    uint64_t back = 0, faces = 0;
    uint32_t count = 0;
    OK(zxv_vino_account_info(vino, p, iss, NULL, NULL, &back, &faces, &count));
    CHECK(back == 11 && faces == 6 && count == 2, "issuer %llu %llu %u", (unsigned long long) back,
          (unsigned long long) faces, count);
    /* redeeming retires: outstanding falls, backing stays, nothing paid */
    OK(zxv_vino_redeem(vino, p, id, h1));
    OK(zxv_vino_account_info(vino, p, iss, NULL, NULL, &back, &faces, &count));
    CHECK(back == 11 && faces == 1 && count == 1, "after redeem");
    OK(zxv_vino_account_info(vino, p, h1, NULL, NULL, &back, &faces, &count));
    CHECK(back == 0 && faces == 0 && count == 0, "holder after redeem");
    /* now 2 of backing covers 1 (20 >= 18) and 9 can be withdrawn */
    OK(zxv_vino_play_unback(vino, p, iss, 9));
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_play_unback(vino, p, iss, 1), ZXV_E_COVERAGE);
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
}

static void test_issue_args(void)
{
    uint32_t iss, h1, h2;
    zxv_vino_play *p = sandbox(UINT64_C(1) << 40, &iss, &h1, &h2);
    uint64_t id = 0;
    snap_t s;
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 4, 0, &id), ZXV_E_DENOM);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 0, 0, &id), ZXV_E_DENOM);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, ZXV_COIN_TOP_FACE + 1, 0, &id), ZXV_E_DENOM);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 5, 2, &id), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 5, -1, &id), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, h1, h2, 5, 0, &id), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, iss, 5, 0, &id), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, 9, h1, 5, 0, &id), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, 9, 5, 0, &id), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 5, 0, NULL), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_issue(vino, NULL, iss, h1, 5, 0, &id), ZXV_E_ARG);
    CHECK(id == 0, "no id");
    /* every ladder face issues; the top face too */
    uint64_t total = 0;
    for (uint32_t r = 1; r <= 45; r++) {
        uint64_t f = 0;
        OK(zxv_coin_face(r, &f));
        OK(zxv_vino_issue(vino, p, iss, h1, f, (int32_t) (r & 1), &id));
        CHECK(id == r, "id %llu for rung %u", (unsigned long long) id, r);
        uint64_t face = 0;
        int32_t rail = -1, state = 0;
        OK(zxv_vino_voucher_info(vino, p, id, &face, NULL, NULL, &rail, &state));
        CHECK(face == f && rail == (int32_t) (r & 1) && state == ZXV_VINO_V_ACTIVE, "rung %u", r);
        total += f;
    }
    uint64_t faces = 0;
    OK(zxv_vino_account_info(vino, p, h1, NULL, NULL, NULL, &faces, NULL));
    CHECK(faces == total, "holder total");
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
}

static void test_rails(void)
{
    uint32_t iss, h1, h2;
    zxv_vino_play *p = sandbox(1000, &iss, &h1, &h2);
    uint64_t v = 0;
    OK(zxv_vino_issue(vino, p, iss, h1, 13, ZXV_VINO_RAIL_BEARER, &v));
    snap_t s;
    snap(p, &s);
    /* spending it on the ghost rail while it is a bearer note is refused */
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h1, h2, ZXV_VINO_RAIL_GHOST), ZXV_E_RAIL);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, v, h1, ZXV_VINO_RAIL_BEARER), ZXV_E_RAIL);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, v, h2, ZXV_VINO_RAIL_GHOST), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, v, h1, 7), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, 99, h1, 1), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, 0, h1, 1), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, v, 50, 1), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, NULL, v, h1, 1), ZXV_E_ARG);
    OK(zxv_vino_set_rail(vino, p, v, h1, ZXV_VINO_RAIL_GHOST));
    int32_t rail = -1;
    OK(zxv_vino_voucher_info(vino, p, v, NULL, NULL, NULL, &rail, NULL));
    CHECK(rail == ZXV_VINO_RAIL_GHOST, "switched");
    /* switching moved nothing */
    uint64_t f1 = 0, f2 = 0;
    OK(zxv_vino_account_info(vino, p, h1, NULL, NULL, NULL, &f1, NULL));
    CHECK(f1 == 13, "still held");
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h1, h2, ZXV_VINO_RAIL_BEARER), ZXV_E_RAIL);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h1, h1, ZXV_VINO_RAIL_GHOST), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h2, h1, ZXV_VINO_RAIL_GHOST), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h1, iss, ZXV_VINO_RAIL_GHOST), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h1, 40, ZXV_VINO_RAIL_GHOST), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, 40, h2, ZXV_VINO_RAIL_GHOST), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v + 1, h1, h2, ZXV_VINO_RAIL_GHOST),
              ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h1, h2, 2), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, NULL, v, h1, h2, 1), ZXV_E_ARG);
    OK(zxv_vino_transfer(vino, p, v, h1, h2, ZXV_VINO_RAIL_GHOST));
    uint32_t holder = 0, issuer = 9;
    OK(zxv_vino_voucher_info(vino, p, v, NULL, &issuer, &holder, &rail, NULL));
    CHECK(holder == h2 && issuer == iss && rail == ZXV_VINO_RAIL_GHOST, "moved, rail kept");
    OK(zxv_vino_account_info(vino, p, h1, NULL, NULL, NULL, &f1, NULL));
    OK(zxv_vino_account_info(vino, p, h2, NULL, NULL, NULL, &f2, NULL));
    CHECK(f1 == 0 && f2 == 13, "totals moved");
    /* redeem: only its holder; then it is spent for good */
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_redeem(vino, p, v, h1), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_redeem(vino, p, v, iss), ZXV_E_ARG);
    UNCHANGED(p, &s, zxv_vino_redeem(vino, p, 5, h2), ZXV_E_NOT_FOUND);
    UNCHANGED(p, &s, zxv_vino_redeem(vino, NULL, v, h2), ZXV_E_ARG);
    OK(zxv_vino_redeem(vino, p, v, h2));
    int32_t state = 0;
    OK(zxv_vino_voucher_info(vino, p, v, NULL, NULL, &holder, NULL, &state));
    CHECK(state == ZXV_VINO_V_REDEEMED && holder == h2, "redeemed");
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_redeem(vino, p, v, h2), ZXV_E_STATE);
    UNCHANGED(p, &s, zxv_vino_transfer(vino, p, v, h2, h1, ZXV_VINO_RAIL_GHOST), ZXV_E_STATE);
    UNCHANGED(p, &s, zxv_vino_set_rail(vino, p, v, h2, ZXV_VINO_RAIL_BEARER), ZXV_E_STATE);
    /* ids are never reused */
    uint64_t w = 0;
    OK(zxv_vino_issue(vino, p, iss, h1, 1, 0, &w));
    CHECK(w == v + 1, "next id %llu", (unsigned long long) w);
    IS(zxv_vino_voucher_info(vino, p, w + 1, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_vino_voucher_info(vino, NULL, w, NULL, NULL, NULL, NULL, NULL), ZXV_E_ARG);
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
}

static void test_listing(void)
{
    uint32_t iss, h1, h2, iss2;
    zxv_vino_play *p = sandbox(1000, &iss, &h1, &h2);
    OK(zxv_vino_play_open(vino, p, 703, 1, &iss2));
    OK(zxv_vino_play_back(vino, p, iss2, 1000));
    uint64_t id;
    /* ids 1..6, alternating issuers and holders */
    for (int i = 0; i < 6; i++)
        OK(zxv_vino_issue(vino, p, (i & 1) ? iss2 : iss, (i % 3) ? h1 : h2, 2, 0, &id));
    OK(zxv_vino_redeem(vino, p, 4, h2));
    uint64_t ids[8] = {0};
    size_t n = 99;
    OK(zxv_vino_vouchers(vino, p, h1, ids, 8, &n));
    CHECK(n == 4 && ids[0] == 2 && ids[1] == 3 && ids[2] == 5 && ids[3] == 6, "h1 list");
    OK(zxv_vino_vouchers(vino, p, h2, ids, 1, &n));
    CHECK(n == 1 && ids[0] == 1, "h2 list");
    OK(zxv_vino_vouchers(vino, p, iss, ids, 3, &n));
    CHECK(n == 3 && ids[0] == 1 && ids[1] == 3 && ids[2] == 5, "iss list");
    OK(zxv_vino_vouchers(vino, p, iss2, ids, 2, &n));
    CHECK(n == 2 && ids[0] == 2 && ids[1] == 6, "iss2 list (4 redeemed)");
    uint64_t keep[8] = {7, 7, 7, 7, 7, 7, 7, 7};
    IS(zxv_vino_vouchers(vino, p, h1, keep, 3, &n), ZXV_E_BUFFER);
    CHECK(n == 4 && keep[0] == 7 && keep[2] == 7, "untouched on BUFFER");
    IS(zxv_vino_vouchers(vino, p, h1, NULL, 8, &n), ZXV_E_BUFFER);
    IS(zxv_vino_vouchers(vino, p, h1, ids, 8, NULL), ZXV_E_ARG);
    IS(zxv_vino_vouchers(vino, p, 9, ids, 8, &n), ZXV_E_NOT_FOUND);
    IS(zxv_vino_vouchers(vino, NULL, h1, ids, 8, &n), ZXV_E_ARG);
    /* an empty account lists nothing, NULL ids allowed */
    uint32_t h3;
    OK(zxv_vino_play_open(vino, p, 9, 0, &h3));
    n = 9;
    OK(zxv_vino_vouchers(vino, p, h3, NULL, 0, &n));
    CHECK(n == 0, "empty");
    zxv_vino_play_destroy(p);
}

static void test_capacity(void)
{
    uint32_t iss, h1, h2;
    zxv_vino_play *p = sandbox(UINT64_C(1) << 20, &iss, &h1, &h2);
    uint64_t id = 0;
    for (uint32_t i = 0; i < ZXV_VINO_MAX_VOUCHERS; i++)
        if (zxv_vino_issue(vino, p, iss, (i & 1) ? h1 : h2, 1, 0, &id) != ZXV_OK) {
            CHECK(0, "issue %u failed", i);
            break;
        }
    snap_t s;
    snap(p, &s);
    UNCHANGED(p, &s, zxv_vino_issue(vino, p, iss, h1, 1, 0, &id), ZXV_E_CAPACITY);
    /* redeeming one frees a slot; the new id is still fresh */
    OK(zxv_vino_redeem(vino, p, 1, h2));
    OK(zxv_vino_issue(vino, p, iss, h1, 1, 0, &id));
    CHECK(id == ZXV_VINO_MAX_VOUCHERS + 1, "fresh id");
    uint32_t count = 0;
    OK(zxv_vino_account_info(vino, p, iss, NULL, NULL, NULL, NULL, &count));
    CHECK(count == ZXV_VINO_MAX_VOUCHERS, "count %u", count);
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
}

/* ---- a seeded random walk against a model ---- */
static uint64_t rng = 0x9E3779B97F4A7C15u;
static uint64_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

#define MA 6
#define MV 400
static void test_model(void)
{
    static const uint64_t ladder[] = {1, 2, 3, 5, 8, 13, 21, 34, 55, 89};
    zxv_vino_play *p = NULL;
    OK(zxv_vino_play_create(vino, &p));
    int issuer[MA];
    uint64_t back[MA] = {0}, faces[MA] = {0};
    uint32_t count[MA] = {0};
    uint64_t vf[MV + 1] = {0};
    uint32_t vi[MV + 1] = {0}, vh[MV + 1] = {0};
    int vr[MV + 1] = {0}, vs[MV + 1] = {0};
    uint64_t nv = 0;
    for (uint32_t a = 0; a < MA; a++) {
        uint32_t id;
        issuer[a] = a < 2;
        OK(zxv_vino_play_open(vino, p, a, issuer[a], &id));
    }
    for (int step = 0; step < 4000; step++) {
        uint32_t x = (uint32_t) (next() % MA), y = (uint32_t) (next() % MA);
        int32_t rail = (int32_t) (next() % 2);
        uint64_t v = nv ? 1 + next() % nv : 1;
        int32_t rc, want;
        switch (next() % 6) {
        case 0: {
            uint64_t amt = 1 + next() % 60;
            rc = zxv_vino_play_back(vino, p, x, amt);
            want = issuer[x] ? ZXV_OK : ZXV_E_ARG;
            if (!want) back[x] += amt;
            break;
        }
        case 1: {
            uint64_t amt = 1 + next() % 60;
            rc = zxv_vino_play_unback(vino, p, x, amt);
            want = !issuer[x]                             ? ZXV_E_ARG
                   : amt > back[x]                        ? ZXV_E_FUNDS
                   : 10 * (back[x] - amt) < 18 * faces[x] ? ZXV_E_COVERAGE
                                                          : ZXV_OK;
            if (!want) back[x] -= amt;
            break;
        }
        case 2: {
            uint64_t f = ladder[next() % 10];
            uint64_t id = 0;
            if (nv == MV) continue;
            rc = zxv_vino_issue(vino, p, x, y, f, rail, &id);
            want = !issuer[x] || issuer[y]              ? ZXV_E_ARG
                   : 10 * back[x] < 18 * (faces[x] + f) ? ZXV_E_COVERAGE
                                                        : ZXV_OK;
            if (!want) {
                nv++;
                CHECK(id == nv, "model id");
                vf[nv] = f, vi[nv] = x, vh[nv] = y, vr[nv] = rail, vs[nv] = 1;
                faces[x] += f, faces[y] += f, count[x]++, count[y]++;
            }
            break;
        }
        case 3:
            rc = zxv_vino_transfer(vino, p, v, x, y, rail);
            want = v > nv                ? ZXV_E_NOT_FOUND
                   : !vs[v]              ? ZXV_E_STATE
                   : vh[v] != x          ? ZXV_E_ARG
                   : issuer[y] || x == y ? ZXV_E_ARG
                   : vr[v] != rail       ? ZXV_E_RAIL
                                         : ZXV_OK;
            if (!want) {
                faces[x] -= vf[v], faces[y] += vf[v], count[x]--, count[y]++;
                vh[v] = y;
            }
            break;
        case 4:
            rc = zxv_vino_set_rail(vino, p, v, x, rail);
            want = v > nv          ? ZXV_E_NOT_FOUND
                   : !vs[v]        ? ZXV_E_STATE
                   : vh[v] != x    ? ZXV_E_ARG
                   : vr[v] == rail ? ZXV_E_RAIL
                                   : ZXV_OK;
            if (!want) vr[v] = rail;
            break;
        default:
            rc = zxv_vino_redeem(vino, p, v, x);
            want = v > nv       ? ZXV_E_NOT_FOUND
                   : !vs[v]     ? ZXV_E_STATE
                   : vh[v] != x ? ZXV_E_ARG
                                : ZXV_OK;
            if (!want) {
                faces[x] -= vf[v], faces[vi[v]] -= vf[v], count[x]--, count[vi[v]]--;
                vs[v] = 0;
            }
            break;
        }
        CHECK(rc == want, "step %d: %s, model says %s", step, zxv_status_name(rc),
              zxv_status_name(want));
        if (rc != want) break;
    }
    for (uint32_t a = 0; a < MA; a++) {
        uint64_t b = 0, f = 0;
        uint32_t c = 0;
        OK(zxv_vino_account_info(vino, p, a, NULL, NULL, &b, &f, &c));
        CHECK(b == back[a] && f == faces[a] && c == count[a], "account %u vs model", a);
        if (issuer[a]) CHECK(10 * b >= 18 * f, "coverage of %u", a);
    }
    for (uint64_t v = 1; v <= nv; v++) {
        uint64_t f = 0;
        uint32_t i = 0, h = 0;
        int32_t r = 0, s = 0;
        OK(zxv_vino_voucher_info(vino, p, v, &f, &i, &h, &r, &s));
        CHECK(f == vf[v] && i == vi[v] && h == vh[v] && r == vr[v] &&
                  s == (vs[v] ? ZXV_VINO_V_ACTIVE : ZXV_VINO_V_REDEEMED),
              "voucher %llu vs model", (unsigned long long) v);
    }
    CHECK(nv > 50, "the walk issued %llu", (unsigned long long) nv);
    OK(zxv_vino_play_check(vino, p));
    zxv_vino_play_destroy(p);
}

/* ---- requests ---- */
static zxv_vino_request base_req(void)
{
    zxv_vino_request r;
    memset(&r, 0, sizeof r);
    r.lane = ZXV_VINO_LANE_LIVE;
    for (int i = 0; i < 32; i++) r.merchant[i] = (uint8_t) (i + 1);
    for (int i = 0; i < 16; i++) r.nonce[i] = (uint8_t) (0xa0 + i);
    r.amount = 1234567;
    r.created_at = 1700000000;
    r.expires_at = 1700000600;
    r.memo = "Sword of Dawn";
    return r;
}

static void hex32(const char *h, uint8_t out[32])
{
    for (int i = 0; i < 32; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

static void test_requests(void)
{
    zxv_vino_request r = base_req();
    uint8_t b[160], id[32], want_id[32];
    OK(zxv_vino_request_encode(&r, b, id));
    uint8_t w[160] = {0};
    memcpy(w, "ZXVR", 4);
    w[4] = 1;
    w[5] = 2;
    for (int i = 0; i < 32; i++) w[8 + i] = (uint8_t) (i + 1);
    for (int i = 0; i < 16; i++) w[40 + i] = (uint8_t) (0xa0 + i);
    w[56] = 0x87, w[57] = 0xd6, w[58] = 0x12;               /* 1234567    */
    w[64] = 0x00, w[65] = 0xf1, w[66] = 0x53, w[67] = 0x65; /* 1700000000 */
    w[72] = 0x58, w[73] = 0xf3, w[74] = 0x53, w[75] = 0x65; /* 1700000600 */
    w[80] = 13;
    memcpy(w + 81, "Sword of Dawn", 13);
    CHECK(!memcmp(b, w, 160), "request bytes");
    hex32("1e7de163084ef123f7af1f9c547f8b950d6a3620b95c21e69aeceeaef4293f74", want_id);
    CHECK(!memcmp(id, want_id, 32), "request id");
    OK(zxv_vino_request_encode(&r, b, NULL));

    zxv_vino_request d;
    char memo[65];
    uint8_t id2[32];
    OK(zxv_vino_request_decode(b, &d, memo, sizeof memo, id2));
    CHECK(d.lane == 2 && d.flags == 0 && d.amount == 1234567 && d.created_at == 1700000000 &&
              d.expires_at == 1700000600 && !memcmp(d.merchant, r.merchant, 32) &&
              !memcmp(d.nonce, r.nonce, 16) && d.memo == memo && !strcmp(memo, "Sword of Dawn") &&
              !memcmp(id2, want_id, 32),
          "round trip");
    OK(zxv_vino_request_decode(b, &d, memo, sizeof memo, NULL));
    IS(zxv_vino_request_decode(b, &d, memo, 64, NULL), ZXV_E_ARG);
    IS(zxv_vino_request_decode(NULL, &d, memo, 65, NULL), ZXV_E_ARG);
    IS(zxv_vino_request_decode(b, NULL, memo, 65, NULL), ZXV_E_ARG);
    IS(zxv_vino_request_decode(b, &d, NULL, 65, NULL), ZXV_E_ARG);
    IS(zxv_vino_request_encode(NULL, b, NULL), ZXV_E_ARG);
    IS(zxv_vino_request_encode(&r, NULL, NULL), ZXV_E_ARG);

    /* encode refusals, each leaving out untouched */
#define BAD(body, code)                                                                            \
    do {                                                                                           \
        zxv_vino_request q = base_req();                                                           \
        body;                                                                                      \
        uint8_t o[160];                                                                            \
        memset(o, 0xEE, sizeof o);                                                                 \
        IS(zxv_vino_request_encode(&q, o, NULL), code);                                            \
        CHECK(o[0] == 0xEE && o[159] == 0xEE, "out untouched: %s", #body);                         \
    } while (0)
    BAD(q.lane = 0, ZXV_E_LANE);
    BAD(q.lane = 3, ZXV_E_LANE);
    BAD(q.flags = 0x2, ZXV_E_FORMAT);
    BAD(q.flags = 0x10000, ZXV_E_FORMAT);
    BAD(q.flags = ZXV_VINO_REQ_CHANCE, ZXV_E_CHANCE);
    BAD(q.amount = 0, ZXV_E_RANGE);
    BAD(q.amount = ZXV_VINO_AMOUNT_MAX + 1, ZXV_E_RANGE);
    BAD(q.expires_at = q.created_at, ZXV_E_RANGE);
    BAD(q.expires_at = q.created_at - 1, ZXV_E_RANGE);
    BAD(q.memo = "tab\there", ZXV_E_FORMAT);
    BAD(q.memo = "caf\xc3\xa9", ZXV_E_FORMAT);
    BAD(q.memo = "0123456789012345678901234567890123456789012345678901234567890123x",
        ZXV_E_TOO_LONG);

    /* the play lane may mark a chance outcome; edges that are allowed */
    zxv_vino_request q = base_req();
    q.lane = ZXV_VINO_LANE_PLAY;
    q.flags = ZXV_VINO_REQ_CHANCE;
    q.memo = NULL;
    q.amount = ZXV_VINO_AMOUNT_MAX;
    q.expires_at = q.created_at + 1;
    OK(zxv_vino_request_encode(&q, b, NULL));
    CHECK(b[5] == 1 && b[6] == 1 && b[80] == 0, "play chance bytes");
    OK(zxv_vino_request_decode(b, &d, memo, sizeof memo, NULL));
    CHECK(d.flags == 1 && d.lane == 1 && memo[0] == 0 && d.amount == ZXV_VINO_AMOUNT_MAX,
          "play chance decoded");
    q = base_req();
    q.memo = "0123456789012345678901234567890123456789012345678901234567890123";
    OK(zxv_vino_request_encode(&q, b, NULL));
    OK(zxv_vino_request_decode(b, &d, memo, sizeof memo, NULL));
    CHECK(strlen(memo) == 64 && b[80] == 64, "64-byte memo");
    q.memo = " ~";
    OK(zxv_vino_request_encode(&q, b, NULL));

    /* decode refusals on bytes that encode would never write */
    uint8_t good[160];
    r = base_req();
    OK(zxv_vino_request_encode(&r, good, NULL));
#define DBAD(off, val, code)                                                                       \
    do {                                                                                           \
        uint8_t t_[160];                                                                           \
        memcpy(t_, good, 160);                                                                     \
        t_[off] = (uint8_t) (val);                                                                 \
        IS(zxv_vino_request_decode(t_, &d, memo, sizeof memo, NULL), code);                        \
    } while (0)
    DBAD(0, 'Y', ZXV_E_FORMAT);
    DBAD(3, 'r', ZXV_E_FORMAT);
    DBAD(4, 2, ZXV_E_FORMAT);
    DBAD(4, 0, ZXV_E_FORMAT);
    DBAD(5, 0, ZXV_E_LANE);
    DBAD(5, 3, ZXV_E_LANE);
    DBAD(6, 1, ZXV_E_CHANCE);
    DBAD(6, 2, ZXV_E_FORMAT);
    DBAD(7, 1, ZXV_E_FORMAT);
    DBAD(63, 0x08, ZXV_E_RANGE); /* amount 2^59 + ... */
    DBAD(75, 0x00, ZXV_E_RANGE); /* expires before created */
    DBAD(80, 65, ZXV_E_TOO_LONG);
    DBAD(80, 14, ZXV_E_FORMAT); /* 14th memo byte is the zero pad */
    DBAD(81, 0x7f, ZXV_E_FORMAT);
    DBAD(81, 0x1f, ZXV_E_FORMAT);
    DBAD(94, 'x', ZXV_E_FORMAT); /* memo padding */
    DBAD(145, 1, ZXV_E_FORMAT);
    DBAD(159, 1, ZXV_E_FORMAT);
    {
        uint8_t t[160];
        memcpy(t, good, 160);
        memset(t + 56, 0, 8);
        IS(zxv_vino_request_decode(t, &d, memo, sizeof memo, NULL), ZXV_E_RANGE);
        memcpy(t, good, 160);
        t[80] = 12; /* a shorter memo leaves a non-zero byte in the pad */
        IS(zxv_vino_request_decode(t, &d, memo, sizeof memo, NULL), ZXV_E_FORMAT);
        t[93] = 0;
        OK(zxv_vino_request_decode(t, &d, memo, sizeof memo, NULL));
        CHECK(!strcmp(memo, "Sword of Daw"), "12-byte memo");
    }
}

/* ---- receipts ---- */
static void test_receipts(void)
{
    uint8_t id[32], msg[88], pkx[2592];
    hex32("1e7de163084ef123f7af1f9c547f8b950d6a3620b95c21e69aeceeaef4293f74", id);
    memset(pkx, 0x5a, sizeof pkx);
    OK(zxv_vino_receipt_msg(id, pkx, UINT64_C(0x0102030405060708), msg));
    uint8_t hpk[32];
    hex32("8de8d8e4e01a3bfbf6649c453b5595d0252d8bd5e3cbcc5af5217b24f5c600ac", hpk);
    CHECK(!memcmp(msg, "zxv/vino-rcpt/v1", 16) && !memcmp(msg + 16, id, 32) &&
              !memcmp(msg + 48, hpk, 32) && msg[80] == 8 && msg[87] == 1,
          "receipt message");
    IS(zxv_vino_receipt_msg(NULL, pkx, 0, msg), ZXV_E_ARG);
    IS(zxv_vino_receipt_msg(id, NULL, 0, msg), ZXV_E_ARG);
    IS(zxv_vino_receipt_msg(id, pkx, 0, NULL), ZXV_E_ARG);

    zxv_vino_request r = base_req();
    uint8_t req[160];
    OK(zxv_vino_request_encode(&r, req, NULL));
    static uint8_t pk[2][2592], sk[2][4896], sig[4627];
    if (!(feat2 & ZXV_FEAT2_VHUB)) {
        /* a ZXV_NO_PQC build */
        IS(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], 1700000100, sig), ZXV_E_NOTIMPL);
        IS(zxv_vino_receipt_verify(req, pk[0], 1700000100, sig, 1700000100), ZXV_E_NOTIMPL);
        return;
    }
    for (int k = 0; k < 2; k++) {
        uint8_t seed[32];
        memset(seed, 0x31 + k, sizeof seed);
        OK(zxv_mldsa87_keygen_seed(root, seed, pk[k], sk[k]));
    }
    const uint64_t at = 1700000100, now = 1700000200;
    OK(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], at, sig));
    OK(zxv_vino_receipt_verify(req, pk[0], at, sig, now));
    /* the signature is ML-DSA-87 over the documented message */
    uint8_t rid[32];
    zxv_vino_request d;
    char memo[65];
    OK(zxv_vino_request_decode(req, &d, memo, sizeof memo, rid));
    OK(zxv_vino_receipt_msg(rid, pk[0], at, msg));
    OK(zxv_mldsa87_verify(root, pk[0], msg, sizeof msg, NULL, 0, sig));
    /* window edges */
    OK(zxv_vino_receipt_verify(req, pk[0], at, sig, r.expires_at));
    IS(zxv_vino_receipt_verify(req, pk[0], at, sig, r.expires_at + 1), ZXV_E_EXPIRED);
    static uint8_t s2[4627];
    OK(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], r.created_at, s2));
    OK(zxv_vino_receipt_verify(req, pk[0], r.created_at, s2, r.created_at));
    OK(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], r.expires_at, s2));
    OK(zxv_vino_receipt_verify(req, pk[0], r.expires_at, s2, r.expires_at));
    IS(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], r.created_at - 1, s2), ZXV_E_EXPIRED);
    IS(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], r.expires_at + 1, s2), ZXV_E_EXPIRED);
    IS(zxv_vino_receipt_verify(req, pk[0], r.created_at - 1, sig, now), ZXV_E_EXPIRED);
    IS(zxv_vino_receipt_verify(req, pk[0], r.expires_at + 1, sig, now), ZXV_E_EXPIRED);
    /* forgeries */
    IS(zxv_vino_receipt_verify(req, pk[1], at, sig, now), ZXV_E_VERIFY);
    IS(zxv_vino_receipt_verify(req, pk[0], at + 1, sig, now), ZXV_E_VERIFY);
    sig[100] ^= 1;
    IS(zxv_vino_receipt_verify(req, pk[0], at, sig, now), ZXV_E_VERIFY);
    sig[100] ^= 1;
    uint8_t req2[160];
    memcpy(req2, req, 160);
    req2[56] ^= 1; /* another amount */
    IS(zxv_vino_receipt_verify(req2, pk[0], at, sig, now), ZXV_E_VERIFY);
    req2[56] ^= 1;
    req2[0] = 'Q';
    IS(zxv_vino_receipt_verify(req2, pk[0], at, sig, now), ZXV_E_FORMAT);
    IS(zxv_vino_receipt_sign(sign, req2, sk[0], pk[0], at, s2), ZXV_E_FORMAT);
    OK(zxv_vino_receipt_verify(req, pk[0], at, sig, now));
    /* arguments and the cap */
    IS(zxv_vino_receipt_verify(NULL, pk[0], at, sig, now), ZXV_E_ARG);
    IS(zxv_vino_receipt_verify(req, NULL, at, sig, now), ZXV_E_ARG);
    IS(zxv_vino_receipt_verify(req, pk[0], at, NULL, now), ZXV_E_ARG);
    IS(zxv_vino_receipt_sign(sign, NULL, sk[0], pk[0], at, s2), ZXV_E_ARG);
    IS(zxv_vino_receipt_sign(sign, req, NULL, pk[0], at, s2), ZXV_E_ARG);
    IS(zxv_vino_receipt_sign(sign, req, sk[0], NULL, at, s2), ZXV_E_ARG);
    IS(zxv_vino_receipt_sign(sign, req, sk[0], pk[0], at, NULL), ZXV_E_ARG);
    IS(zxv_vino_receipt_sign(vino, req, sk[0], pk[0], at, s2), ZXV_E_CAP);
    IS(zxv_vino_receipt_sign(NULL, req, sk[0], pk[0], at, s2), ZXV_E_ARG);
}

/* ---- the meter ---- */
static void test_meter(void)
{
    zxv_vino_meter *m = NULL;
    IS(zxv_vino_meter_create(vino, 0, &m), ZXV_E_RANGE);
    IS(zxv_vino_meter_create(vino, ZXV_VINO_AMOUNT_MAX + 1, &m), ZXV_E_RANGE);
    IS(zxv_vino_meter_create(vino, 100, NULL), ZXV_E_ARG);
    IS(zxv_vino_meter_create(sign, 100, &m), ZXV_E_CAP);
    CHECK(m == NULL, "no meter");
    OK(zxv_vino_meter_create(vino, 100, &m));
    uint64_t left = 0;
    OK(zxv_vino_meter_admit(vino, m, 5, 60, &left));
    CHECK(left == 40, "left %llu", (unsigned long long) left);
    IS(zxv_vino_meter_admit(vino, m, 5, 41, &left), ZXV_E_LIMIT);
    CHECK(left == 40, "nothing counted");
    OK(zxv_vino_meter_admit(vino, m, 5, 40, &left));
    CHECK(left == 0, "exactly the cap");
    IS(zxv_vino_meter_admit(vino, m, 5, 1, NULL), ZXV_E_LIMIT);
    IS(zxv_vino_meter_admit(vino, m, 4, 1, &left), ZXV_E_RANGE);
    IS(zxv_vino_meter_admit(vino, m, 5, 0, &left), ZXV_E_RANGE);
    IS(zxv_vino_meter_admit(vino, m, 5, ZXV_VINO_AMOUNT_MAX + 1, &left), ZXV_E_RANGE);
    /* a refused amount on a new day does not move the day */
    IS(zxv_vino_meter_admit(vino, m, 6, 101, &left), ZXV_E_LIMIT);
    CHECK(left == 100, "fresh day left");
    IS(zxv_vino_meter_admit(vino, m, 5, 1, &left), ZXV_E_LIMIT);
    OK(zxv_vino_meter_admit(vino, m, 7, 100, &left));
    CHECK(left == 0, "a new day starts fresh");
    IS(zxv_vino_meter_admit(vino, m, 6, 1, &left), ZXV_E_RANGE);
    OK(zxv_vino_meter_admit(vino, m, UINT64_MAX, 1, NULL));
    IS(zxv_vino_meter_admit(vino, NULL, 9, 1, &left), ZXV_E_ARG);
    zxv_vino_meter_destroy(m);
    zxv_vino_meter_destroy(NULL);
    OK(zxv_vino_meter_create(vino, ZXV_VINO_AMOUNT_MAX, &m));
    OK(zxv_vino_meter_admit(vino, m, 0, ZXV_VINO_AMOUNT_MAX, &left));
    CHECK(left == 0, "max cap");
    zxv_vino_meter_destroy(m);
}

/* ---- capabilities, binding, poisoning ---- */
static void test_context(void)
{
    uint32_t iss, h1, h2;
    zxv_vino_play *p = sandbox(100, &iss, &h1, &h2);
    zxv_vino_meter *m = NULL;
    OK(zxv_vino_meter_create(vino, 10, &m));
    uint64_t id = 0;
    zxv_vino_play *q = NULL;
    IS(zxv_vino_play_create(sign, &q), ZXV_E_CAP);
    IS(zxv_vino_play_create(NULL, &q), ZXV_E_ARG);
    IS(zxv_vino_issue(sign, p, iss, h1, 1, 0, &id), ZXV_E_CAP);
    IS(zxv_vino_play_check(sign, p), ZXV_E_CAP);
    IS(zxv_vino_meter_admit(sign, m, 1, 1, NULL), ZXV_E_CAP);
    /* a cap of another context */
    zxv_ctx *ctx2 = NULL;
    zxv_cap *other = NULL, *child = NULL;
    OK(zxv_ctx_create(&ctx2));
    OK(zxv_cap_create(ctx2, 1, NULL, ZXV_CAP_ALL_1_6, &other));
    IS(zxv_vino_issue(other, p, iss, h1, 1, 0, &id), ZXV_E_CAP);
    IS(zxv_vino_voucher_info(other, p, 1, NULL, NULL, NULL, NULL, NULL), ZXV_E_CAP);
    IS(zxv_vino_meter_admit(other, m, 1, 1, NULL), ZXV_E_CAP);
    /* attenuated, then attested false, then revoked */
    OK(zxv_cap_attenuate(vino, ZXV_CAP_VINO, &child));
    OK(zxv_vino_issue(child, p, iss, h1, 1, 0, &id));
    OK(zxv_cap_set_attestation(child, ZXV_LP_BOTH));
    IS(zxv_vino_issue(child, p, iss, h1, 1, 0, &id), ZXV_E_CAP);
    OK(zxv_cap_revoke(child));
    IS(zxv_vino_issue(child, p, iss, h1, 1, 0, &id), ZXV_E_CAP);
    zxv_cap_destroy(child);
    zxv_cap_destroy(other);
    zxv_ctx_destroy(ctx2);
    OK(zxv_vino_play_check(vino, p));

    /* a halted context refuses everything stateful, before validation */
    OK(zxv_ctx_halt(ctx, "test halt"));
    IS(zxv_vino_play_create(vino, &q), ZXV_E_POISONED);
    IS(zxv_vino_play_open(vino, p, 1, 0, NULL), ZXV_E_POISONED);
    IS(zxv_vino_play_back(vino, p, iss, 1), ZXV_E_POISONED);
    IS(zxv_vino_play_unback(vino, p, iss, 1), ZXV_E_POISONED);
    IS(zxv_vino_issue(vino, p, iss, h1, 4, 0, &id), ZXV_E_POISONED);
    IS(zxv_vino_transfer(vino, p, 1, h1, h2, 0), ZXV_E_POISONED);
    IS(zxv_vino_set_rail(vino, p, 1, h1, 1), ZXV_E_POISONED);
    IS(zxv_vino_redeem(vino, p, 1, h1), ZXV_E_POISONED);
    IS(zxv_vino_voucher_info(vino, p, 1, NULL, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_vino_account_info(vino, p, 0, NULL, NULL, NULL, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_vino_vouchers(vino, p, 0, NULL, 0, NULL), ZXV_E_POISONED);
    IS(zxv_vino_play_check(vino, p), ZXV_E_POISONED);
    zxv_vino_meter *m2 = NULL;
    IS(zxv_vino_meter_create(vino, 10, &m2), ZXV_E_POISONED);
    CHECK(m2 == NULL, "no meter");
    IS(zxv_vino_meter_admit(vino, m, 1, 1, NULL), ZXV_E_POISONED);
    /* pure calls do not depend on a context */
    zxv_vino_request r = base_req();
    uint8_t b[160];
    OK(zxv_vino_request_encode(&r, b, NULL));
    zxv_vino_play_destroy(p);
    zxv_vino_meter_destroy(m);
}

int main(void)
{
    if (zxv_features2(&feat2) != ZXV_OK) return 1;
    if (zxv_ctx_create(&ctx) != ZXV_OK) return 1;
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_6, &root));
    OK(zxv_cap_attenuate(root, ZXV_CAP_VINO, &vino));
    OK(zxv_cap_attenuate(root, ZXV_CAP_CRYPTO_SIGN, &sign));
    if (!root || !vino || !sign) {
        printf("test_zxv_abi16_vino: cannot create capabilities, %d checks, %d failures\n", checks,
               failures);
        return 1;
    }
    test_basics();
    test_accounts();
    test_backing();
    test_coverage();
    test_issue_args();
    test_rails();
    test_listing();
    test_capacity();
    test_model();
    test_requests();
    test_receipts();
    test_meter();
    test_context();
    zxv_cap_destroy(sign);
    zxv_cap_destroy(vino);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    printf("test_zxv_abi16_vino: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
