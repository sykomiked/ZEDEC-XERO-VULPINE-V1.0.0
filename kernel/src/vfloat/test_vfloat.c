/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vfloat.c — Vino floating vouchers (vfloat.h), real ML-DSA-87. */
#include <stdio.h>
#include <string.h>
#include "vfloat.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
        }                                                                                          \
    } while (0)

/* ---- a stand-in bounty board: letters of marque with an allowance ---- */
typedef struct {
    uint8_t id[32];
    vna_id_t bearer;
    uint8_t resource;
    uint64_t left;
    bool live;
} letter_t;

typedef struct {
    letter_t l[8];
    uint32_t n, calls, takes;
    uint64_t last_units;
    bool last_take;
} board_t;

static bool board_fn(void *ctx, const uint8_t letter[32], const vna_id_t *bearer, uint8_t resource,
                     uint64_t units, bool take)
{
    board_t *b = (board_t *) ctx;
    b->calls++;
    b->last_units = units;
    b->last_take = take;
    for (uint32_t i = 0; i < b->n; i++) {
        letter_t *l = &b->l[i];
        if (memcmp(l->id, letter, 32) != 0) continue;
        if (!l->live || memcmp(l->bearer.b, bearer->b, 32) != 0 || l->resource != resource ||
            units > l->left)
            return false;
        if (take) l->left -= units, b->takes++;
        return true;
    }
    return false;
}

static uint8_t *add_letter(board_t *b, uint8_t tag, const vna_id_t *bearer, uint8_t res,
                           uint64_t allowance)
{
    letter_t *l = &b->l[b->n++];
    memset(l, 0, sizeof *l);
    l->id[0] = tag;
    l->id[31] = 0x4D;
    l->bearer = *bearer;
    l->resource = res;
    l->left = allowance;
    l->live = true;
    return l->id;
}

static vna_identity_t IA, IB, IC;
static board_t BA, BB; /* each node's view of the same board */
static uint8_t SCOPE[VNA_CID_RAW_LEN];
static uint8_t V[VF_VOUCHER_LEN], V2[VF_VOUCHER_LEN], V3[VF_VOUCHER_LEN];

static void ident(vna_identity_t *i, uint8_t s)
{
    uint8_t seed[32];
    memset(seed, s, 32);
    seed[31] = 0x77;
    CHECK(vna_identity_create(i, seed, 0, 1) == VNA_OK);
}

/* A and B know each other; both boards carry the same letters. */
static void pair(vf_t *a, vf_t *b, int32_t *ab, int32_t *ba)
{
    vf_init(a, &IA, 10);
    vf_init(b, &IB, 10);
    vf_set_letter_hook(a, board_fn, &BA);
    vf_set_letter_hook(b, board_fn, &BB);
    *ab = vf_add_peer(a, IB.pk);
    *ba = vf_add_peer(b, IA.pk);
}

static void boards(void)
{
    memset(&BA, 0, sizeof BA);
    memset(&BB, 0, sizeof BB);
    for (uint32_t r = 0; r < VF_RES_COUNT; r++) {
        add_letter(&BA, (uint8_t) (0x10 + r), &IB.id, (uint8_t) r, 1000000);
        add_letter(&BB, (uint8_t) (0x10 + r), &IB.id, (uint8_t) r, 1000000);
        add_letter(&BA, (uint8_t) (0x20 + r), &IA.id, (uint8_t) r, 1000000);
        add_letter(&BB, (uint8_t) (0x20 + r), &IA.id, (uint8_t) r, 1000000);
    }
}

static uint8_t LB[VF_RES_COUNT][32], LA[VF_RES_COUNT][32]; /* letters held by B / by A */

static void letters(void)
{
    for (uint32_t r = 0; r < VF_RES_COUNT; r++) {
        memset(LB[r], 0, 32), LB[r][0] = (uint8_t) (0x10 + r), LB[r][31] = 0x4D;
        memset(LA[r], 0, 32), LA[r][0] = (uint8_t) (0x20 + r), LA[r][31] = 0x4D;
    }
}

static void t_codec(void)
{
    vf_voucher_t v, w;
    memset(&v, 0, sizeof v);
    v.resource = VF_RES_STORAGE;
    for (uint32_t i = 0; i < 32; i++)
        v.issuer.b[i] = (uint8_t) i, v.bearer.b[i] = (uint8_t) (99 - i);
    for (uint32_t i = 0; i < VNA_CID_RAW_LEN; i++) v.scope[i] = (uint8_t) (i * 3);
    for (uint32_t i = 0; i < 32; i++)
        v.letter[i] = (uint8_t) (i ^ 0xA5), v.prev[i] = (uint8_t) (i + 7);
    v.units = 0x0102030405060708ull;
    v.pair_seq = 0x1112131415161718ull;
    v.issued_epoch = 0x21222324u;
    v.expiry_epoch = 0x31323334u;
    uint8_t buf[VF_VOUCHER_LEN];
    memset(buf, 0xEE, sizeof buf);
    vf_encode_body(&v, buf);
    CHECK(memcmp(buf, "VFLT", 4) == 0 && buf[4] == VF_VERSION && buf[5] == VF_RES_STORAGE);
    CHECK(buf[6] == 0 && buf[7] == 0);
    CHECK(buf[8] == 0 && buf[39] == 31 && buf[40] == 99 && buf[71] == 68);
    CHECK(buf[72] == 0 && buf[107] == 105);             /* scope */
    CHECK(buf[108] == 0xA5 && buf[139] == (31 ^ 0xA5)); /* letter */
    CHECK(buf[140] == 0x08 && buf[147] == 0x01);        /* units LE */
    CHECK(buf[148] == 0x18 && buf[155] == 0x11);        /* seq LE */
    CHECK(buf[156] == 7 && buf[187] == 38);             /* prev */
    CHECK(buf[188] == 0x24 && buf[191] == 0x21 && buf[192] == 0x34 && buf[195] == 0x31);
    CHECK(vf_decode(buf, VF_VOUCHER_LEN, &w) == VF_OK);
    CHECK(memcmp(&v.issuer, &w.issuer, 32) == 0 && memcmp(&v.bearer, &w.bearer, 32) == 0);
    CHECK(memcmp(v.scope, w.scope, VNA_CID_RAW_LEN) == 0 && memcmp(v.letter, w.letter, 32) == 0);
    CHECK(memcmp(v.prev, w.prev, 32) == 0 && w.resource == v.resource);
    CHECK(w.units == v.units && w.pair_seq == v.pair_seq);
    CHECK(w.issued_epoch == v.issued_epoch && w.expiry_epoch == v.expiry_epoch);

    CHECK(vf_decode(buf, VF_VOUCHER_LEN - 1, &w) == VF_E_DECODE);
    CHECK(vf_decode(buf, VF_VOUCHER_LEN + 1, &w) == VF_E_DECODE);
    CHECK(vf_decode(NULL, VF_VOUCHER_LEN, &w) == VF_E_DECODE);
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t c = buf[i];
        buf[i] ^= (i == 5) ? 0x04 : 0x01;
        CHECK(vf_decode(buf, VF_VOUCHER_LEN, &w) == VF_E_DECODE);
        buf[i] = c;
    }
    buf[5] = VF_RES_COUNT;
    CHECK(vf_decode(buf, VF_VOUCHER_LEN, &w) == VF_E_DECODE);
    buf[5] = VF_RES_COUNT - 1;
    CHECK(vf_decode(buf, VF_VOUCHER_LEN, &w) == VF_OK && w.resource == VF_RES_COUNT - 1);
    buf[6] = 0x80;
    CHECK(vf_decode(buf, VF_VOUCHER_LEN, &w) == VF_E_DECODE);
    buf[6] = 0, buf[7] = 0x80;
    CHECK(vf_decode(buf, VF_VOUCHER_LEN, &w) == VF_E_DECODE);
}

static void t_peers(void)
{
    vf_t f;
    vf_init(&f, &IA, 5);
    CHECK(f.epoch == 5 && !f.sealed && f.letter == NULL);
    int32_t b = vf_add_peer(&f, IB.pk);
    int32_t c = vf_add_peer(&f, IC.pk);
    CHECK(b == 0 && c == 1);
    CHECK(vf_add_peer(&f, IB.pk) == 0);
    CHECK(vf_find_peer(&f, &IC.id) == 1 && vf_find_peer(&f, &IA.id) == VF_E_PEER);
    CHECK(f.peer[0].cap[0] == VF_DEFAULT_CAP && f.peer[1].cap[VF_RES_COUNT - 1] == VF_DEFAULT_CAP);
    CHECK(vf_set_cap(&f, 1, VF_RES_AIRC, 77) == VF_OK && f.peer[1].cap[VF_RES_AIRC] == 77);
    CHECK(vf_set_cap(&f, 1, VF_RES_COUNT, 77) == VF_E_ARG);
    CHECK(vf_set_cap(&f, 1, 0, VF_CAP_LIMIT) == VF_OK);
    CHECK(vf_set_cap(&f, 1, 0, VF_CAP_LIMIT + 1) == VF_E_ARG);
    CHECK(vf_set_cap(&f, 2, 0, 1) == VF_E_PEER);
    CHECK(vf_set_cap(&f, VF_MAX_PEERS, 0, 1) == VF_E_PEER);
    /* fill the table with distinct keys */
    static uint8_t pk[VNA_PK_LEN];
    uint32_t added = 2;
    for (uint32_t i = 0; i < VF_MAX_PEERS; i++) {
        memcpy(pk, IC.pk, VNA_PK_LEN);
        pk[0] ^= (uint8_t) (i + 1);
        int32_t k = vf_add_peer(&f, pk);
        if (added < VF_MAX_PEERS) {
            CHECK(k == (int32_t) added);
            added++;
        } else {
            CHECK(k == VF_E_PEER);
        }
    }
    CHECK(vf_add_peer(&f, IB.pk) == 0); /* known key still found when full */
    CHECK(vf_weight(&f, VF_MAX_PEERS) == 0);
}

static void t_issue_accept(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    CHECK(ab == 0 && ba == 0);

    /* no hook: nothing is issued, nothing changes */
    vf_set_letter_hook(&a, NULL, NULL);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 100, 5, NULL, V) == VF_E_LETTER);
    CHECK(a.peer[0].out_seq == 0 && vf_open_units(&a, 0, VF_RES_COMPUTE, VF_DIR_OWED) == 0);
    CHECK(a.peer[0].score == 0);
    vf_set_letter_hook(&a, board_fn, &BA);

    /* wrong letter (held by A, not B), wrong resource letter, over allowance */
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LA[1], 100, 5, NULL, V) == VF_E_LETTER);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[0], 100, 5, NULL, V) == VF_E_LETTER);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 1000001, 5, NULL, V) == VF_E_LETTER);
    CHECK(BA.takes == 0 && a.peer[0].out_seq == 0);

    /* argument checks come before the board is asked */
    BA.calls = 0;
    CHECK(vf_issue(&a, 1, VF_RES_COMPUTE, SCOPE, LB[1], 100, 5, NULL, V) == VF_E_PEER);
    CHECK(vf_issue(&a, VF_MAX_PEERS, VF_RES_COMPUTE, SCOPE, LB[1], 100, 5, NULL, V) == VF_E_PEER);
    CHECK(vf_issue(&a, 0, VF_RES_COUNT, SCOPE, LB[1], 100, 5, NULL, V) == VF_E_ARG);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, NULL, LB[1], 100, 5, NULL, V) == VF_E_ARG);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, NULL, 100, 5, NULL, V) == VF_E_ARG);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 100, 5, NULL, NULL) == VF_E_ARG);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 0, 5, NULL, V) == VF_E_ARG);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 100, 0, NULL, V) == VF_E_ARG);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 100, UINT32_MAX - 9, NULL, V) == VF_E_ARG);
    CHECK(BA.calls == 0);

    /* the real thing */
    uint8_t rnd[32];
    memset(rnd, 0x3C, 32);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 100, UINT32_MAX - 10, rnd, V) == VF_OK);
    CHECK(BA.takes == 1 && BA.last_take && BA.last_units == 100 && BA.l[2].left == 999900);
    CHECK(a.peer[0].out_seq == 1 && a.peer[0].score == 100);
    CHECK(vf_open_units(&a, 0, VF_RES_COMPUTE, VF_DIR_OWED) == 100);
    CHECK(vf_net(&a, 0, VF_RES_COMPUTE) == -100);
    vf_voucher_t v;
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &v) == VF_OK);
    CHECK(v.pair_seq == 1 && v.issued_epoch == 10 && v.expiry_epoch == UINT32_MAX);
    CHECK(memcmp(v.issuer.b, IA.id.b, 32) == 0 && memcmp(v.bearer.b, IB.id.b, 32) == 0);
    uint8_t z[32] = {0};
    CHECK(memcmp(v.prev, z, 32) == 0 && memcmp(v.letter, LB[1], 32) == 0);
    CHECK(vna_verify(IA.pk, VF_CTX, V, VF_BODY_LEN, V + VF_BODY_LEN));
    CHECK(!vna_verify(IA.pk, "vinea/v3/trade-receipt", V, VF_BODY_LEN, V + VF_BODY_LEN));
    uint8_t h[32];
    vna_sha3(V, VF_BODY_LEN, h);
    CHECK(memcmp(a.peer[0].out_prev, h, 32) == 0);

    /* B: no hook refuses (and leaves the chain), then a hook that denies */
    vf_set_letter_hook(&b, NULL, NULL);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_E_LETTER);
    CHECK(b.peer[0].in_seq == 0);
    board_t empty;
    memset(&empty, 0, sizeof empty);
    vf_set_letter_hook(&b, board_fn, &empty);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_E_LETTER && empty.calls == 1);
    vf_set_letter_hook(&b, board_fn, &BB);

    /* tampering is caught */
    V[140] ^= 1;
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_E_SIG);
    V[140] ^= 1;
    V[VF_BODY_LEN + 9] ^= 0x40;
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_E_SIG);
    V[VF_BODY_LEN + 9] ^= 0x40;
    CHECK(b.peer[0].in_seq == 0);

    BB.calls = 0;
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(BB.calls == 1 && !BB.last_take && BB.last_units == 100 && BB.l[2].left == 1000000);
    CHECK(b.peer[0].in_seq == 1 && memcmp(b.peer[0].in_prev, h, 32) == 0);
    CHECK(vf_net(&b, 0, VF_RES_COMPUTE) == 100);
    CHECK(b.peer[0].score == 0); /* score is the issuer's view only */

    /* V2: the same voucher twice */
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_E_SEQ);
    CHECK(vf_net(&b, 0, VF_RES_COMPUTE) == 100);

    /* out of order: 3 before 2 */
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 7, 50, NULL, V2) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_STORAGE, SCOPE, LB[2], 9, 50, NULL, V3) == VF_OK);
    CHECK(vf_accept(&b, V3, VF_VOUCHER_LEN) == VF_E_SEQ);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_accept(&b, V3, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_net(&b, 0, VF_RES_COMPUTE) == 107 && vf_net(&b, 0, VF_RES_STORAGE) == 9);
    CHECK(vf_net(&a, 0, VF_RES_STORAGE) == -9 && a.peer[0].score == 116);

    /* the right seq but the wrong prev: a forked chain */
    vf_voucher_t f;
    CHECK(vf_issue(&a, 0, VF_RES_AIRC, SCOPE, LB[3], 5, 50, NULL, V) == VF_OK);
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &f) == VF_OK);
    f.prev[31] ^= 1;
    vf_encode_body(&f, V2);
    vna_sign(IA.sk, VF_CTX, V2, VF_BODY_LEN, NULL, V2 + VF_BODY_LEN);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_SEQ);
    f.prev[31] ^= 1;
    f.prev[0] ^= 1;
    vf_encode_body(&f, V2);
    vna_sign(IA.sk, VF_CTX, V2, VF_BODY_LEN, NULL, V2 + VF_BODY_LEN);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_SEQ);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);

    /* units zero or issued after expiry, signed: refused as malformed */
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &f) == VF_OK);
    f.pair_seq = 5;
    vna_sha3(V, VF_BODY_LEN, f.prev);
    f.units = 0;
    vf_encode_body(&f, V2);
    vna_sign(IA.sk, VF_CTX, V2, VF_BODY_LEN, NULL, V2 + VF_BODY_LEN);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_DECODE);
    f.units = 1;
    f.issued_epoch = f.expiry_epoch + 1;
    vf_encode_body(&f, V2);
    vna_sign(IA.sk, VF_CTX, V2, VF_BODY_LEN, NULL, V2 + VF_BODY_LEN);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_DECODE);
    f.issued_epoch = f.expiry_epoch;
    vf_encode_body(&f, V2);
    vna_sign(IA.sk, VF_CTX, V2, VF_BODY_LEN, NULL, V2 + VF_BODY_LEN);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_OK); /* issued == expiry is fine */

    /* signed by someone else under A's name */
    f.pair_seq = 6;
    vna_sha3(V2, VF_BODY_LEN, f.prev);
    vf_encode_body(&f, V3);
    vna_sign(IC.sk, VF_CTX, V3, VF_BODY_LEN, NULL, V3 + VF_BODY_LEN);
    CHECK(vf_accept(&b, V3, VF_VOUCHER_LEN) == VF_E_SIG);
    CHECK(b.peer[0].in_seq == 5);

    /* not addressed to B; from an unknown issuer */
    vf_t c;
    vf_init(&c, &IC, 10);
    vf_set_letter_hook(&c, board_fn, &BB);
    CHECK(vf_add_peer(&c, IA.pk) == 0);
    CHECK(vf_accept(&c, V, VF_VOUCHER_LEN) == VF_E_BEARER);
    vf_t b2;
    vf_init(&b2, &IB, 10);
    vf_set_letter_hook(&b2, board_fn, &BB);
    CHECK(vf_accept(&b2, V, VF_VOUCHER_LEN) == VF_E_PEER);
    CHECK(vf_accept(&b2, V, 3) == VF_E_DECODE);
}

static void t_caps_full(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    CHECK(vf_set_cap(&a, 0, VF_RES_TRANSFER, 10) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 11, 9, NULL, V) == VF_E_CAP);
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 6, 9, NULL, V) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 5, 9, NULL, V2) == VF_E_CAP);
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 4, 9, NULL, V2) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 1, 9, NULL, V3) == VF_E_CAP);
    CHECK(BA.takes == 2 && BA.l[0].left == 999990); /* refused issues take nothing */
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 11, 9, NULL, V3) == VF_OK);

    CHECK(vf_set_cap(&b, 0, VF_RES_TRANSFER, 8) == VF_OK);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_CAP);
    CHECK(b.peer[0].in_seq == 1);
    CHECK(vf_set_cap(&b, 0, VF_RES_TRANSFER, 9) == VF_OK);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_CAP);
    CHECK(vf_set_cap(&b, 0, VF_RES_TRANSFER, 10) == VF_OK);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_OK); /* same bytes, retried */
    CHECK(vf_set_cap(&b, 0, VF_RES_COMPUTE, 10) == VF_OK);
    CHECK(vf_accept(&b, V3, VF_VOUCHER_LEN) == VF_E_CAP); /* one voucher over the cap */

    /* the open table fills */
    vf_t x, y;
    boards();
    pair(&x, &y, &ab, &ba);
    vf_status_t st = VF_OK;
    uint32_t n = 0;
    while (st == VF_OK && n < VF_MAX_OPEN + 2) {
        st = vf_issue(&x, 0, (uint8_t) (n % VF_RES_COUNT), SCOPE, LB[n % VF_RES_COUNT], 1, 9, NULL,
                      V);
        if (st == VF_OK) n++;
    }
    CHECK(n == VF_MAX_OPEN && st == VF_E_FULL);
    CHECK(x.peer[0].out_seq == VF_MAX_OPEN);
    /* freeing one slot by offset lets one more through */
    y.open[0] = x.open[0];
    y.open[0].dir = VF_DIR_HELD;
    x.open[5].dir = VF_DIR_HELD; /* pretend one came the other way */
    CHECK(vf_offset(&x, 0, x.open[5].resource) == 1);
    CHECK(vf_issue(&x, 0, VF_RES_STORAGE, SCOPE, LB[2], 1, 9, NULL, V) == VF_OK);

    /* the bearer's table fills too */
    vf_t p, q;
    boards();
    pair(&p, &q, &ab, &ba);
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) {
        q.open[i].used = true;
        q.open[i].peer = 9;
    }
    CHECK(vf_issue(&p, 0, VF_RES_STORAGE, SCOPE, LB[2], 3, 9, NULL, V) == VF_OK);
    CHECK(vf_accept(&q, V, VF_VOUCHER_LEN) == VF_E_FULL && q.peer[0].in_seq == 0);
    q.open[VF_MAX_OPEN - 1].used = false;
    CHECK(vf_accept(&q, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(q.open[VF_MAX_OPEN - 1].used && q.open[VF_MAX_OPEN - 1].units == 3);
    CHECK(q.open[VF_MAX_OPEN - 1].peer == 0 && q.open[VF_MAX_OPEN - 1].dir == VF_DIR_HELD);
}

/* exact ceilings, offsets with other peers' and resources' vouchers about,
 * decay and seal across more than one peer */
static void t_more(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    CHECK(vf_set_cap(&a, 0, VF_RES_COMPUTE, 9) == VF_OK);
    CHECK(vf_set_cap(&b, 0, VF_RES_COMPUTE, 9) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 9, 9, NULL, V) == VF_OK);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);

    /* a second peer C, and other resources, in A's table */
    int32_t ac = vf_add_peer(&a, IC.pk);
    CHECK(ac == 1);
    a.open[10] = (vf_open_t){true, 1, VF_DIR_HELD, VF_RES_COMPUTE, 30, 1, 50};
    a.open[11] = (vf_open_t){true, 0, VF_DIR_HELD, VF_RES_STORAGE, 30, 1, 50};
    a.open[12] = (vf_open_t){true, 0, VF_DIR_HELD, VF_RES_COMPUTE, 30, 1, 4};
    CHECK(vf_offset(&a, 0, VF_RES_COMPUTE) == 4);
    CHECK(a.open[10].units == 50 && a.open[11].units == 50 && !a.open[12].used);
    CHECK(vf_open_units(&a, 0, VF_RES_COMPUTE, VF_DIR_OWED) == 5);
    CHECK(vf_open_units(&a, 1, VF_RES_COMPUTE, VF_DIR_HELD) == 50);

    /* same expiry: the later slot with the higher seq goes second */
    vf_t c;
    vf_init(&c, &IA, 1);
    CHECK(vf_add_peer(&c, IB.pk) == 0);
    c.open[0] = (vf_open_t){true, 0, VF_DIR_OWED, VF_RES_AIRC, 20, 1, 5};
    c.open[1] = (vf_open_t){true, 0, VF_DIR_OWED, VF_RES_AIRC, 20, 2, 5};
    c.open[2] = (vf_open_t){true, 0, VF_DIR_HELD, VF_RES_AIRC, 20, 1, 3};
    CHECK(vf_offset(&c, 0, VF_RES_AIRC) == 3);
    CHECK(c.open[0].units == 2 && c.open[1].units == 5);

    /* decay and seal reach every peer, not just the first */
    a.peer[0].score = 2100;
    a.peer[1].score = 2100;
    CHECK(vf_tick(&a, 11) == VF_OK);
    CHECK(a.peer[0].score == 1300 && a.peer[1].score == 1300);
    /* expired vouchers do not count twice at the seal */
    CHECK(vf_tick(&a, 30) == VF_OK);
    uint64_t ev = a.evaporated;
    CHECK(vf_seal(&a) == 0 && a.evaporated == ev);
    CHECK(a.peer[0].score == 0 && a.peer[1].score == 0);
}

static void t_chunk(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    static uint8_t data[9000];
    for (uint32_t i = 0; i < sizeof data; i++) data[i] = (uint8_t) (i * 31 + 7);
    uint8_t cid[VNA_CID_RAW_LEN];
    vna_cid_raw(data, 8193, cid);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 8192, 9, NULL, V) == VF_E_CHUNK);
    data[100] ^= 1;
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 8193, 9, NULL, V) == VF_E_CHUNK);
    data[100] ^= 1;
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], NULL, data, 8193, 9, NULL, V) == VF_E_ARG);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, NULL, 8193, 9, NULL, V) == VF_E_ARG);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 0, 9, NULL, V) == VF_E_ARG);
    CHECK(BA.calls == 0 && a.peer[0].out_seq == 0);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 8193, 9, NULL, V) == VF_OK);
    vf_voucher_t v;
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &v) == VF_OK);
    CHECK(v.resource == VF_RES_TRANSFER && v.units == 3);
    CHECK(memcmp(v.scope, SCOPE, VNA_CID_RAW_LEN) == 0);
    vna_cid_raw(data, 4096, cid);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 4096, 9, NULL, V) == VF_OK);
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &v) == VF_OK && v.units == 1);
    vna_cid_raw(data, 1, cid);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 1, 9, NULL, V) == VF_OK);
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &v) == VF_OK && v.units == 1);
    vna_cid_raw(data, 4097, cid);
    CHECK(vf_issue_chunk(&a, 0, SCOPE, LB[0], cid, data, 4097, 9, NULL, V) == VF_OK);
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &v) == VF_OK && v.units == 2);
    CHECK(vf_open_units(&a, 0, VF_RES_TRANSFER, VF_DIR_OWED) == 7);
}

static void t_net_offset(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    /* A owes B 10 + 6 transfer (expiring 30, 20); B owes A 4 + 9 (25, 25) */
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 10, 20, NULL, V) == VF_OK);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_TRANSFER, SCOPE, LB[0], 6, 10, NULL, V) == VF_OK);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_issue(&b, 0, VF_RES_TRANSFER, SCOPE, LA[0], 4, 15, NULL, V) == VF_OK);
    CHECK(vf_accept(&a, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_issue(&b, 0, VF_RES_TRANSFER, SCOPE, LA[0], 9, 15, NULL, V) == VF_OK);
    CHECK(vf_accept(&a, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_issue(&b, 0, VF_RES_STORAGE, SCOPE, LA[2], 50, 15, NULL, V) == VF_OK);
    CHECK(vf_accept(&a, V, VF_VOUCHER_LEN) == VF_OK);

    CHECK(vf_net(&a, 0, VF_RES_TRANSFER) == -3 && vf_net(&b, 0, VF_RES_TRANSFER) == 3);
    CHECK(vf_net(&a, 0, VF_RES_STORAGE) == 50 && vf_net(&b, 0, VF_RES_STORAGE) == -50);
    CHECK(vf_offset(&a, 0, VF_RES_TRANSFER) == 13);
    CHECK(vf_offset(&b, 0, VF_RES_TRANSFER) == 13);
    CHECK(vf_net(&a, 0, VF_RES_TRANSFER) == -3 && vf_net(&b, 0, VF_RES_TRANSFER) == 3);
    CHECK(vf_open_units(&a, 0, VF_RES_TRANSFER, VF_DIR_HELD) == 0);
    CHECK(vf_open_units(&b, 0, VF_RES_TRANSFER, VF_DIR_OWED) == 0);
    /* oldest first: the 6 expiring at 20 went whole, 7 came off the 10 */
    uint32_t left = 0;
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) {
        const vf_open_t *o = &a.open[i];
        if (o->used && o->resource == VF_RES_TRANSFER) {
            left++;
            CHECK(o->units == 3 && o->expiry == 30 && o->seq == 1 && o->dir == VF_DIR_OWED);
        }
        const vf_open_t *p = &b.open[i];
        if (p->used && p->resource == VF_RES_TRANSFER)
            CHECK(p->units == 3 && p->expiry == 30 && p->seq == 1 && p->dir == VF_DIR_HELD);
    }
    CHECK(left == 1);
    CHECK(vf_offset(&a, 0, VF_RES_STORAGE) == 0 && vf_net(&a, 0, VF_RES_STORAGE) == 50);
    CHECK(vf_offset(&a, 0, VF_RES_TRANSFER) == 0);
    CHECK(vf_offset(&a, 1, VF_RES_TRANSFER) == 0 && vf_offset(&a, 0, VF_RES_COUNT) == 0);
    CHECK(vf_offset(&a, VF_MAX_PEERS, 0) == 0);

    /* equal expiry: lower seq goes first */
    vf_t c, d;
    boards();
    pair(&c, &d, &ab, &ba);
    CHECK(vf_issue(&c, 0, VF_RES_AIRC, SCOPE, LB[3], 5, 10, NULL, V) == VF_OK);
    CHECK(vf_issue(&c, 0, VF_RES_AIRC, SCOPE, LB[3], 5, 10, NULL, V) == VF_OK);
    c.open[0].seq = 9; /* make slot 1 the lower seq */
    c.open[2] = c.open[0];
    c.open[2].dir = VF_DIR_HELD;
    c.open[2].units = 3;
    CHECK(vf_offset(&c, 0, VF_RES_AIRC) == 3);
    CHECK(c.open[0].units == 5 && c.open[1].units == 2 && !c.open[2].used);
    c.open[3] = c.open[0];
    c.open[3].dir = VF_DIR_HELD;
    c.open[3].units = 5;
    c.open[0].expiry = 9; /* now slot 0 expires first */
    CHECK(vf_offset(&c, 0, VF_RES_AIRC) == 5);
    CHECK(!c.open[0].used && c.open[1].units == 2);
}

static void t_tick(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 21000, 3, NULL, V) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 5, 4, NULL, V2) == VF_OK);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_tick(&a, 9) == VF_E_ARG && a.epoch == 10);
    CHECK(vf_tick(&a, 12) == VF_OK && a.epoch == 12);
    CHECK(a.peer[0].score == 21005 * 13 / 21 * 13 / 21);
    CHECK(vf_open_units(&a, 0, VF_RES_COMPUTE, VF_DIR_OWED) == 21005 && a.evaporated == 0);
    CHECK(vf_tick(&a, 13) == VF_OK);
    CHECK(vf_open_units(&a, 0, VF_RES_COMPUTE, VF_DIR_OWED) == 5 && a.evaporated == 21000);
    CHECK(vf_tick(&a, 13) == VF_OK && a.evaporated == 21000);
    CHECK(vf_tick(&a, 14) == VF_OK);
    CHECK(vf_open_units(&a, 0, VF_RES_COMPUTE, VF_DIR_OWED) == 0 && a.evaporated == 21005);

    /* B sees V2 only after it has expired: it evaporates but keeps the chain */
    CHECK(vf_tick(&b, 14) == VF_OK);
    CHECK(vf_open_units(&b, 0, VF_RES_COMPUTE, VF_DIR_HELD) == 0 && b.evaporated == 21000);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_EXPIRED);
    CHECK(b.peer[0].in_seq == 2 && b.evaporated == 21005);
    CHECK(vf_open_units(&b, 0, VF_RES_COMPUTE, VF_DIR_HELD) == 0);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_SEQ);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 8, 1, NULL, V3) == VF_OK);
    CHECK(vf_accept(&b, V3, VF_VOUCHER_LEN) == VF_OK);
    /* expiry exactly at the current epoch counts as gone */
    vf_t c, d;
    boards();
    pair(&c, &d, &ab, &ba);
    CHECK(vf_issue(&c, 0, VF_RES_COMPUTE, SCOPE, LB[1], 4, 2, NULL, V) == VF_OK);
    CHECK(vf_tick(&d, 11) == VF_OK);
    CHECK(vf_accept(&d, V, VF_VOUCHER_LEN) == VF_OK);
    vf_t e, g;
    boards();
    pair(&e, &g, &ab, &ba);
    CHECK(vf_issue(&e, 0, VF_RES_COMPUTE, SCOPE, LB[1], 4, 2, NULL, V) == VF_OK);
    CHECK(vf_tick(&g, 12) == VF_OK);
    CHECK(vf_accept(&g, V, VF_VOUCHER_LEN) == VF_E_EXPIRED && g.evaporated == 4);
    /* a forged expired voucher does not move the chain */
    vf_t h, k;
    boards();
    pair(&h, &k, &ab, &ba);
    CHECK(vf_issue(&h, 0, VF_RES_COMPUTE, SCOPE, LB[1], 4, 2, NULL, V) == VF_OK);
    V[VF_BODY_LEN] ^= 1;
    CHECK(vf_tick(&k, 20) == VF_OK);
    CHECK(vf_accept(&k, V, VF_VOUCHER_LEN) == VF_E_SIG && k.peer[0].in_seq == 0 &&
          k.evaporated == 0);
}

static void t_priority(void)
{
    vf_t a;
    vf_init(&a, &IA, 1);
    CHECK(vf_add_peer(&a, IB.pk) == 0 && vf_add_peer(&a, IC.pk) == 1);
    CHECK(vf_weight(&a, 0) == VF_PRIO_BASE && vf_weight(&a, 1) == VF_PRIO_BASE);
    CHECK(vf_weight(&a, 2) == 0);
    a.peer[0].score = VF_PRIO_HALF;
    CHECK(vf_weight(&a, 0) == 27); /* 21 + 13/2 */
    a.peer[0].score = VF_PRIO_HALF * 12;
    CHECK(vf_weight(&a, 0) == 33);
    a.peer[0].score = 1ull << 60;
    CHECK(vf_weight(&a, 0) == 33 && vf_weight(&a, 0) < VF_PRIO_BASE + VF_PRIO_BOOST);
    a.peer[0].score = 80660; /* 13*s/(s+2^20) just under 1 */
    CHECK(vf_weight(&a, 0) == 21);
    a.peer[0].score = 87382; /* just over */
    CHECK(vf_weight(&a, 0) == 22);

    /* the score saturates and decays */
    boards();
    vf_t x, y;
    int32_t ab, ba;
    pair(&x, &y, &ab, &ba);
    x.peer[0].score = (1ull << 60) - 3;
    CHECK(vf_issue(&x, 0, VF_RES_STORAGE, SCOPE, LB[2], 5, 9, NULL, V) == VF_OK);
    CHECK(x.peer[0].score == 1ull << 60);
    x.peer[0].score = (1ull << 60) - 5;
    CHECK(vf_issue(&x, 0, VF_RES_STORAGE, SCOPE, LB[2], 5, 9, NULL, V) == VF_OK);
    CHECK(x.peer[0].score == 1ull << 60);
    x.peer[0].score = (1ull << 60) - 6;
    CHECK(vf_issue(&x, 0, VF_RES_STORAGE, SCOPE, LB[2], 5, 9, NULL, V) == VF_OK);
    CHECK(x.peer[0].score == (1ull << 60) - 1);
    CHECK(vf_tick(&x, 10 + 85) == VF_OK && x.peer[0].score == 1);
    CHECK(vf_tick(&x, 10 + 86) == VF_OK && x.peer[0].score == 0);
    x.peer[0].score = 1ull << 60;
    CHECK(vf_tick(&x, UINT32_MAX) == VF_OK && x.peer[0].score == 0);

    /* split: exact, and nobody is shut out */
    vf_t s;
    vf_init(&s, &IA, 1);
    static uint8_t pk[VNA_PK_LEN];
    uint32_t idx[VF_MAX_PEERS], out[VF_MAX_PEERS];
    for (uint32_t i = 0; i < VF_MAX_PEERS; i++) {
        memcpy(pk, IC.pk, VNA_PK_LEN);
        pk[1] ^= (uint8_t) (i + 1);
        idx[i] = (uint32_t) vf_add_peer(&s, pk);
    }
    s.peer[0].score = 1ull << 60;
    s.peer[1].score = 1ull << 60;
    for (uint32_t n = 1; n <= VF_MAX_PEERS; n++) {
        uint32_t slots = 2 * n;
        CHECK(vf_split(&s, idx, n, slots, out) == VF_OK);
        uint32_t sum = 0, mn = UINT32_MAX;
        for (uint32_t i = 0; i < n; i++) sum += out[i], mn = out[i] < mn ? out[i] : mn;
        CHECK(sum == slots && mn >= 1);
    }
    CHECK(vf_split(&s, idx, 2, 0, out) == VF_OK && out[0] == 0 && out[1] == 0);
    CHECK(vf_split(&s, idx, 3, 1000, out) == VF_OK);
    CHECK(out[0] == 379 && out[1] == 379 && out[2] == 242); /* 33:33:21 */
    CHECK(vf_split(&s, idx + 2, 2, 5, out) == VF_OK && out[0] == 3 && out[1] == 2);
    CHECK(vf_split(&s, idx, 2, 7, out) == VF_OK && out[0] == 4 && out[1] == 3);
    uint32_t idx2[3] = {2, 0, 3};
    CHECK(vf_split(&s, idx2, 3, 4, out) == VF_OK && out[0] == 1 && out[1] == 2 && out[2] == 1);
    uint32_t bad[2] = {0, VF_MAX_PEERS};
    CHECK(vf_split(&s, bad, 2, 4, out) == VF_E_ARG);
    CHECK(vf_split(&s, idx, 0, 4, out) == VF_E_ARG);
    CHECK(vf_split(&s, idx, VF_MAX_PEERS + 1, 4, out) == VF_E_ARG);
    CHECK(vf_split(&s, NULL, 2, 4, out) == VF_E_ARG);
    CHECK(vf_split(&s, idx, 2, 4, NULL) == VF_E_ARG);
    CHECK(vf_split(&s, idx, 1, UINT32_MAX, out) == VF_OK && out[0] == UINT32_MAX);
}

static void t_seal(void)
{
    vf_t a, b;
    int32_t ab, ba;
    boards();
    pair(&a, &b, &ab, &ba);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 30, 9, NULL, V) == VF_OK);
    CHECK(vf_accept(&b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_issue(&b, 0, VF_RES_STORAGE, SCOPE, LA[2], 12, 9, NULL, V) == VF_OK);
    CHECK(vf_accept(&a, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 4, 9, NULL, V2) == VF_OK); /* in flight */
    CHECK(vf_seal(&b) == 42 && b.sealed && b.evaporated == 42 && b.seal_epoch == 10);
    CHECK(vf_net(&b, 0, VF_RES_COMPUTE) == 0 && vf_net(&b, 0, VF_RES_STORAGE) == 0);
    for (uint32_t i = 0; i < VF_MAX_OPEN; i++) CHECK(!b.open[i].used);
    CHECK(b.peer[0].score == 0 && b.peer[0].in_seq == 1 && b.peer[0].out_seq == 1);
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_SEALED);
    CHECK(vf_issue(&b, 0, VF_RES_STORAGE, SCOPE, LA[2], 1, 9, NULL, V) == VF_E_SEALED);
    CHECK(vf_seal(&b) == 0 && b.evaporated == 42);
    CHECK(vf_unseal(&b) == VF_E_SEALED && b.sealed);
    CHECK(vf_tick(&b, 11) == VF_OK && vf_unseal(&b) == VF_OK && !b.sealed);
    CHECK(vf_unseal(&b) == VF_OK);
    /* the voucher from before the seal evaporates on arrival, chain moves */
    CHECK(vf_accept(&b, V2, VF_VOUCHER_LEN) == VF_E_EXPIRED);
    CHECK(b.peer[0].in_seq == 2 && b.evaporated == 46);
    CHECK(vf_open_units(&b, 0, VF_RES_COMPUTE, VF_DIR_HELD) == 0);
    /* A has not sealed; once on the new epoch its next voucher counts */
    CHECK(vf_tick(&a, 11) == VF_OK);
    CHECK(vf_issue(&a, 0, VF_RES_COMPUTE, SCOPE, LB[1], 6, 9, NULL, V3) == VF_OK);
    CHECK(vf_accept(&b, V3, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_net(&b, 0, VF_RES_COMPUTE) == 6);
    /* issuing after an unseal works and the score restarts from zero */
    CHECK(vf_issue(&b, 0, VF_RES_STORAGE, SCOPE, LA[2], 2, 9, NULL, V) == VF_OK);
    CHECK(b.peer[0].score == 2);
    /* a voucher issued after the seal epoch but before unseal never exists:
     * issue refuses while sealed (checked above) */
}

int main(void)
{
    ident(&IA, 0xA1);
    ident(&IB, 0xB2);
    ident(&IC, 0xC3);
    letters();
    static const uint8_t book[] = "Alexandria booklet 001.42";
    vna_cid_raw(book, (uint32_t) sizeof book - 1, SCOPE);
    t_codec();
    t_peers();
    t_issue_accept();
    t_caps_full();
    t_chunk();
    t_more();
    t_net_offset();
    t_tick();
    t_priority();
    t_seal();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
