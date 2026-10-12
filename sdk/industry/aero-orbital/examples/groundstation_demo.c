/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* groundstation_demo.c — one pass of a SmallSat over a ground station.
 * The station's surveyed position goes to ECEF; an orbit-determination fix
 * of the spacecraft in ECEF goes back to its sub-satellite point and
 * height. The spacecraft sends every housekeeping packet three times; the
 * station forwards each copy over the ground network in UBH-168 frames.
 * One packet arrives with a single-event upset in one copy: the receiver
 * holds it as BOTH and logs it, and an analyst signs which copy is right.
 * The decision record then verifies offline. All integer arithmetic. */
#include <stdio.h>
#include <string.h>

#include "zaero.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zao_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char *k_state[] = {"-", "collecting", "released", "corrected (logged)", "HELD (BOTH)",
                                "accepted by analyst", "rejected"};

static void print_nd(const char *what, int64_t nd) {
    int64_t a = nd < 0 ? -nd : nd;
    printf("%s%s%lld.%09lld deg", what, nd < 0 ? "-" : "", (long long) (a / 1000000000),
           (long long) (a % 1000000000));
}

static int send(zao_rx *rx, const zao_tm *tm, int upset, zao_key *k, int32_t *out) {
    uint8_t b[ZAO_TM_MAX_BYTES], fr[32 * ZXV_WIRE_FRAME_BYTES];
    size_t len = 0, nf = 0;
    if (zao_tm_encode(tm, b, sizeof b, &len) != ZXV_OK) return -1;
    if (upset) b[30] ^= 0x08; /* a bit flipped in RAM after the CRC was computed */
    if (zao_wire_encode(4, b, len, fr, sizeof fr, &nf) != ZXV_OK) return -1;
    *out = zao_rx_submit(rx, fr, nf, k);
    return 0;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *op = NULL, *an = NULL;
    zao_rx *rx = NULL;
    zao_rx_cfg cfg = {3, ZAO_POLICY_HOLD, 1024};
    zao_geo station = {78229772000, 15407786000, 500000}, sub; /* a high-latitude station, 500 m */
    zao_ecef st_ecef, sat = {1215000000, 334000000, 6994000000};
    zao_tm tm;
    zao_key k;
    zao_pkt_info info;
    uint8_t seed[32] = {0}, msg[64], sig[ZIND_SIG_BYTES];
    static uint8_t chain[65536];
    size_t ml = 0, cl = 0;
    uint32_t nq = 0, nr = 0, nc = 0, bad = 0;
    int32_t rc = 0;
    const uint8_t *an_pk[1];

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zxv_hexlog_create(cap, 64, &qlog));
    seed[0] = 1;
    TRY(zind_signer_from_seed(cap, seed, &op));
    seed[0] = 2;
    TRY(zind_signer_from_seed(cap, seed, &an));
    TRY(zao_rx_create(cap, op, qlog, &cfg, &rx));
    TRY(zao_analyst_add(rx, zind_signer_pk(an)));
    an_pk[0] = zind_signer_pk(an);

    printf("== geodesy (WGS-84, integer fixed point)\n");
    TRY(zao_geo_to_ecef(&station, &st_ecef));
    print_nd("  station ", station.lat_nd);
    print_nd(", ", station.lon_nd);
    printf(", %lld mm -> ECEF (%lld, %lld, %lld) mm\n", (long long) station.h_mm,
           (long long) st_ecef.x_mm, (long long) st_ecef.y_mm, (long long) st_ecef.z_mm);
    TRY(zao_ecef_to_geo(&sat, &sub));
    printf("  orbit fix ECEF (%lld, %lld, %lld) mm -> ", (long long) sat.x_mm,
           (long long) sat.y_mm, (long long) sat.z_mm);
    print_nd("", sub.lat_nd);
    print_nd(", ", sub.lon_nd);
    printf(", height %lld.%03lld km\n", (long long) (sub.h_mm / 1000000),
           (long long) (sub.h_mm % 1000000 / 1000));

    printf("== telemetry: three copies of each packet over UBH-168\n");
    memset(&tm, 0, sizeof tm);
    tm.scid = 0x2C1;
    tm.vcid = 1;
    tm.time_ms = 1760000000000ULL;
    tm.n = 3;
    tm.v[0].channel = 1; /* battery, mV */
    tm.v[1].channel = 2; /* panel +X, mK */
    tm.v[2].channel = 3; /* reaction wheel, rpm */
    for (uint32_t seq = 100; seq < 103; seq++) {
        tm.seq = seq;
        tm.v[0].value = 8212 - (int64_t) seq;
        tm.v[1].value = 291450 + (int64_t) seq * 3;
        tm.v[2].value = -1200;
        for (int c = 0; c < 3; c++)
            if (send(rx, &tm, seq == 101 && c == 1, &k, &rc) != 0) return 1;
        TRY(zao_rx_info(rx, k, &info));
        printf("  packet %u: %s (%s), %u copies, %u CRC failure(s), worst copy-to-copy %u "
               "bit(s)\n",
               seq, k_state[info.state], zao_status_name(rc), info.copies, info.crc_bad,
               info.max_bit_diff);
    }
    k.seq = 101;
    TRY(zxv_hexlog_count(qlog, &nq));
    printf("  quarantine log entries: %u\n", nq);
    TRY(zao_rx_copy(rx, k, 1, NULL, 0, &cl, NULL) == ZXV_E_BUFFER ? ZXV_OK : ZXV_E_INTERNAL);

    printf("== the analyst decides packet 101 (copy 0 agrees with copy 2)\n");
    TRY(zao_review_message(rx, k, ZAO_ACCEPT, 0, msg, sizeof msg, &ml));
    TRY(zind_sign(cap, an, msg, ml, ZAO_REVIEW_CTX, sig));
    TRY(zao_review_resolve(rx, k, zind_signer_pk(an), ZAO_ACCEPT, 0, sig));
    TRY(zao_rx_info(rx, k, &info));
    TRY(zao_rx_value(rx, k, &tm));
    printf("  packet 101: %s, battery %lld mV, tag %s\n", k_state[info.state],
           (long long) tm.v[0].value, zxv_hex_name(info.tag));

    TRY(zao_decisions_export(rx, chain, sizeof chain, &cl));
    TRY(zao_decisions_verify(cap, chain, cl, an_pk, 1, zind_signer_pk(op), &nr, &nc, &bad));
    printf("  decision chain: %zu bytes, %u review(s), %u correction(s), verified offline\n", cl,
           nr, nc);
    chain[cl / 2] ^= 1;
    printf("  the same chain with one bit flipped: %s\n",
           zao_status_name(zao_decisions_verify(cap, chain, cl, an_pk, 1, zind_signer_pk(op), &nr,
                                                &nc, &bad)));

    zao_rx_destroy(rx);
    zind_signer_destroy(op);
    zind_signer_destroy(an);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
