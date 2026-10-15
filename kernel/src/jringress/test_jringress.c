/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_jringress.c — the producer ingress driver (jringress.h I1-I3) on a real
 * router: signed pledges turn the physical layer on, all-or-none manifests,
 * replay refusal, withdrawal turns it off, and allotted lots stay put. The
 * signature hook is a stand-in (SHA-256 of key byte || message). */
#include <stdio.h>
#include <string.h>
#include "jringress.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static uint8_t KEYB = 5;
static bool sig_ok(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig, uint32_t sl)
{
    uint8_t k = *(const uint8_t *) ctx, d[32];
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, &k, 1);
    sha256_update(&c, msg, len);
    sha256_final(&c, d);
    return sl == 32 && memcmp(d, sig, 32) == 0;
}
static void sign(const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, &KEYB, 1);
    sha256_update(&c, msg, len);
    sha256_final(&c, out);
}
static int burn(void *ctx, const uint8_t u[32], uint64_t v)
{
    (void) ctx;
    (void) u;
    (void) v;
    return 0;
}

static jr_router_t R;
static jri_t G;
static const uint8_t P1[32] = {0xA1, [31] = 0x11}, P2[32] = {0xA2, [31] = 0x22};
static uint8_t H[40][JR_LOT_HDR_LEN];

static void hdrs_at(uint32_t wh, uint32_t n, uint64_t first)
{
    for (uint32_t i = 0; i < n; i++) {
        jr_lot_hdr_t h;
        memset(&h, 0, sizeof h);
        h.grade_kind = 1;
        h.warehouse_id = wh;
        h.volume = 208000;
        h.drum_serial = first + i;
        jr_lot_hdr_encode(&h, H[i]);
    }
    /* a valid, unused header just past the manifest: reading it is a bug */
    jr_lot_hdr_t s;
    memset(&s, 0, sizeof s);
    s.warehouse_id = 0xFEED;
    s.volume = 1;
    s.drum_serial = 0xFEED0000u + wh;
    if (n < 40) jr_lot_hdr_encode(&s, H[n]);
}
static void hdrs(uint32_t wh, uint32_t n)
{
    hdrs_at(wh, n, 1);
}
static jr_status_t pledge(const uint8_t *key, uint32_t asset, uint64_t ver, uint32_t n)
{
    uint8_t m[JRI_PLEDGE_MSG_LEN], s[32];
    jri_pledge_msg(key, asset, ver, &H[0][0], n, m);
    sign(m, JRI_PLEDGE_MSG_LEN, s);
    return jri_pledge(&G, key, asset, ver, &H[0][0], n, s, 32);
}
static jr_status_t withdraw(const uint8_t *key, uint64_t ver, uint32_t *n)
{
    uint8_t m[JRI_WITHDRAW_MSG_LEN], s[32];
    jri_withdraw_msg(key, ver, m);
    sign(m, JRI_WITHDRAW_MSG_LEN, s);
    return jri_withdraw(&G, key, ver, s, 32, n);
}

int main(void)
{
    jr_hooks_t hk;
    memset(&hk, 0, sizeof hk);
    hk.burn = burn;
    CHECK(jr_init(&R, &hk, NULL, 0, 10) == JR_OK, "router");
    jr_asset_t oil = {1, JR_CAP_COMMODITY, 1, 1, 1, 0, false, false};
    jr_asset_t tpc = {2, JR_CAP_COMPUTE, JR_TPC_TICKS_PER_VINO, 1, 1, 0, false, false};
    CHECK(jr_add_asset(&R, &oil) == JR_OK && jr_add_asset(&R, &tpc) == JR_OK, "assets");
    CHECK(jri_init(NULL, &R, sig_ok, &KEYB) == JR_E_ARG &&
              jri_init(&G, NULL, sig_ok, &KEYB) == JR_E_ARG,
          "init null");
    CHECK(jri_init(&G, &R, NULL, NULL) == JR_OK, "init without a verifier");
    hdrs(10, 3);
    CHECK(pledge(P1, 1, 1, 3) == JR_E_SIG && R.nlots == 0, "no verifier: refused");
    CHECK(jri_init(&G, &R, sig_ok, &KEYB) == JR_OK && G.router == &R, "init");

    uint8_t m[JRI_PLEDGE_MSG_LEN], d[32];
    jri_pledge_msg(P1, 0x01020304, 0x1122334455667788ull, &H[0][0], 3, m);
    sha256(&H[0][0], 3 * JR_LOT_HDR_LEN, d);
    CHECK(memcmp(m, "zxv/jr-ingress/v1", 17) == 0 && m[17] == 0xA1 && m[49] == 4 && m[52] == 1 &&
              m[53] == 0x88 && m[60] == 0x11 && m[61] == 3 && memcmp(m + 65, d, 32) == 0,
          "pledge message layout");
    uint8_t w[JRI_WITHDRAW_MSG_LEN];
    jri_withdraw_msg(P1, 0x1122334455660102ull, w);
    CHECK(memcmp(w, "zxv/jr-withdraw/v1", 18) == 0 && w[18] == 0xA1 && w[49] == 0x11 &&
              w[50] == 2 && w[51] == 1 && w[57] == 0x11,
          "withdraw message layout");
    CHECK(m[48] == 0x11, "pledge key byte 31");

    uint8_t bad[32] = {0};
    CHECK(jri_pledge(&G, P1, 1, 1, &H[0][0], 3, bad, 32) == JR_E_SIG && R.nlots == 0, "bad sig");
    CHECK(pledge(P1, 1, 1, 0) == JR_E_ARG, "zero lots");
    CHECK(pledge(P1, 1, 1, JRI_MAX_MANIFEST_LOTS + 1) == JR_E_ARG, "too many lots");
    CHECK(pledge(P1, 9, 1, 3) == JR_E_NOTFOUND, "unknown asset");
    CHECK(pledge(P1, 0, 1, 3) == JR_E_NOTFOUND, "asset 0");
    CHECK(pledge(P1, 2, 1, 3) == JR_E_ARG, "compute takes no lots");
    CHECK(jri_pledge(NULL, P1, 1, 1, &H[0][0], 3, bad, 32) == JR_E_ARG &&
              jri_pledge(&G, NULL, 1, 1, &H[0][0], 3, bad, 32) == JR_E_ARG &&
              jri_pledge(&G, P1, 1, 1, NULL, 3, bad, 32) == JR_E_ARG,
          "pledge null");
    CHECK(!R.physical_online, "still software only");
    CHECK(pledge(P1, 1, 1, 3) == JR_OK && R.nlots == 3 && R.physical_online &&
              R.lots[0].source == 1 && G.prod[0].pledged == 3 && G.prod[0].version == 1,
          "pledge: lots in, physical layer on");
    CHECK(pledge(P1, 1, 1, 3) == JR_E_REPLAY, "same version replay");
    hdrs(11, 2);
    CHECK(pledge(P1, 1, 1, 2) == JR_E_REPLAY, "old version refused");
    memcpy(H[1], H[0], JR_LOT_HDR_LEN);
    CHECK(pledge(P1, 1, 2, 2) == JR_E_ARG && R.nlots == 3, "duplicate inside a manifest");
    hdrs(11, 3);
    memcpy(H[2], H[1], JR_LOT_HDR_LEN);
    CHECK(pledge(P1, 1, 2, 3) == JR_E_ARG && R.nlots == 3, "duplicate of a later manifest entry");
    hdrs_at(10, 1, 1);
    CHECK(pledge(P1, 1, 2, 1) == JR_E_ARG && R.nlots == 3, "duplicate of router lot 0");
    hdrs_at(10, 1, 3);
    CHECK(pledge(P1, 1, 2, 1) == JR_E_ARG && R.nlots == 3, "duplicate of router lot 2");
    hdrs(12, 3);
    H[2][0] = 'Q';
    CHECK(pledge(P1, 1, 2, 3) == JR_E_FORMAT && R.nlots == 3 && G.prod[0].version == 1,
          "bad header: none added, version unchanged");
    hdrs(12, 3);
    CHECK(pledge(P2, 1, 1, 3) == JR_OK && R.nlots == 6 && R.lots[3].source == 2, "second producer");
    hdrs_at(10, 1, 50);
    CHECK(pledge(P1, 1, 2, 1) == JR_OK && R.nlots == 7 && R.lots[6].source == 1 &&
              G.prod[0].pledged == 4 && G.prod[0].version == 2,
          "same warehouse, new serial: P1 keeps its slot");
    {
        jr_lot_hdr_t z0;
        memset(&z0, 0, sizeof z0);
        z0.volume = 1;
        jr_lot_hdr_encode(&z0, H[0]);
        CHECK(pledge(P2, 1, 2, 1) == JR_OK && R.nlots == 8, "warehouse 0, serial 0 is a real lot");
    }

    /* allot one of P1's lots, then P1 withdraws */
    CHECK(jr_set_profile(&R, JR_PROFILE_INSTITUTION) == JR_OK && jr_enable(&R, true) == JR_OK,
          "enable");
    jr_request_t q;
    memset(&q, 0, sizeof q);
    q.user[0] = 1;
    q.vino = 1;
    q.want_asset = 1;
    jr_zone_ok_t z = {LPRES_STATE_TRUE, 1, JR_CAP_COMMODITY | JR_CAP_COMPUTE, 0};
    jr_receipt_t o;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1 && o.lot_idx[0] == 0,
          "P1's first lot allotted");
    uint32_t n = 99;
    CHECK(withdraw(P1, 2, &n) == JR_E_REPLAY && n == 0, "withdraw replay, n reset");
    CHECK(withdraw(P1, 3, &n) == JR_OK && n == 3 && R.lots[0].state == 2 && R.lots[1].state == 3 &&
              R.lots[6].state == 3,
          "withdraw: free lots only");
    CHECK(R.physical_online, "P2's lots keep the layer on");
    CHECK(pledge(P1, 1, 3, 3) == JR_E_REPLAY, "a withdrawn producer's old pledge cannot replay");
    {
        uint8_t m2[JRI_WITHDRAW_MSG_LEN], s2[32] = {0};
        jri_withdraw_msg(P2, 5, m2);
        CHECK(jri_withdraw(&G, P2, 5, s2, 32, &n) == JR_E_SIG, "withdraw bad sig");
    }
    const uint8_t P3[32] = {0xA3};
    CHECK(withdraw(P3, 1, &n) == JR_E_NOTFOUND, "unknown producer");
    CHECK(jri_withdraw(NULL, P2, 1, NULL, 0, &n) == JR_E_ARG &&
              jri_withdraw(&G, P2, 1, NULL, 0, NULL) == JR_E_ARG,
          "withdraw null");
    G.verify = NULL;
    CHECK(withdraw(P2, 9, &n) == JR_E_SIG, "withdraw without a verifier");
    G.verify = sig_ok;
    CHECK(withdraw(P2, 3, &n) == JR_OK && n == 4 && !R.physical_online,
          "last free lot gone: layer off");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 2,
          "barrels gone: compute, nothing hangs");

    /* capacity */
    jri_init(&G, &R, sig_ok, &KEYB);
    for (uint32_t i = 0; i < JRI_MAX_PRODUCERS; i++) {
        uint8_t k[32] = {0xB0, (uint8_t) i};
        hdrs(100 + i, 1);
        CHECK(pledge(k, 1, 1, 1) == JR_OK, "fill producers");
    }
    {
        uint8_t k[32] = {0xC0};
        hdrs(999, 1);
        CHECK(pledge(k, 1, 1, 1) == JR_E_FULL, "producer table full");
    }
    {
        uint8_t k[32] = {0xB0, 0};
        uint32_t room = JR_MAX_LOTS - R.nlots;
        int ok = 0;
        uint64_t ver = 2;
        uint32_t wh = 2000;
        while (room >= JRI_MAX_MANIFEST_LOTS) {
            hdrs(wh++, JRI_MAX_MANIFEST_LOTS);
            ok += pledge(k, 1, ver++, JRI_MAX_MANIFEST_LOTS) == JR_OK;
            room -= JRI_MAX_MANIFEST_LOTS;
        }
        hdrs(wh++, room + 1);
        CHECK(ok > 0 && pledge(k, 1, ver, room + 1) == JR_E_FULL, "router lot table full");
        hdrs(wh, room);
        CHECK(room == 0 || pledge(k, 1, ver, room) == JR_OK, "exactly fills");
        CHECK(R.nlots == JR_MAX_LOTS, "lots full");
    }
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
