/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* street_demo.c — a simulated street. Home "no7" loses its WAN. Three
 * neighbours publish signed adverts (two Ed25519, one ML-DSA-87); a fourth
 * offers a relay over amateur radio, which is refused. The owner of no7 has
 * agreements with two neighbours and caps what no7 may draw. One neighbour
 * claims the internet while no7's own probe through it fails: that
 * contradiction is held as BOTH until the owner signs a decision. The
 * planner then shares the eligible bandwidth max-min fairly among no7's
 * four flows, exactly, and an old advert replayed later is refused. Nothing
 * here touches a real router or radio. */
#include <stdio.h>
#include <string.h>

#include "ztmodem.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, ztm_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

#define NOW 1760000000ULL

static ztm_advert advert(const char *id, uint64_t seq, int medium, int64_t kbps, uint32_t lat) {
    ztm_advert a;
    memset(&a, 0, sizeof a);
    snprintf(a.neighbour, sizeof a.neighbour, "%s", id);
    a.seq = seq;
    a.issued = NOW;
    a.expires = NOW + 900;
    a.wan_up = 1;
    a.medium = (uint8_t) medium;
    zind_qty_make(kbps, 1, &a.offer_kbps);
    a.latency_ms = lat;
    a.n_claims = 1;
    snprintf(a.claims[0].name, sizeof a.claims[0].name, "internet");
    a.claims[0].lp = ZXV_LP_TRUE;
    return a;
}

static void show(const ztm_planner *p, const char *id) {
    static const char *lp[] = {"NEITHER", "TRUE", "FALSE", "BOTH"};
    ztm_view v;
    char b[48];
    size_t n = 0;
    if (ztm_neighbour_view(p, id, "internet", NOW + 10, &v) != ZXV_OK) return;
    zind_qty_format(v.budget_kbps, b, sizeof b, &n);
    printf("  %-5s internet: %-7s %-6s budget %s kbit/s%s%s\n", id, lp[v.lp & 3],
           v.held ? "HELD" : "", b, v.eligible ? "" : ", not eligible: ",
           v.eligible ? "" : ztm_status_name(v.why));
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    ztm_planner *p = NULL;
    zind_signer *ml = NULL;
    uint8_t owner_sk[32], owner_pk[32], sk3[32], pk3[32], sk5[32], pk5[32], sk9[32], pk9[32];
    uint8_t buf[ZTM_ADVERT_MAX], old[ZTM_ADVERT_MAX], msg[200], sig[64];
    size_t len = 0, oldlen = 0, ml_ = 0;
    ztm_advert a;
    ztm_plan pl;
    ztm_demand d[4] = {{"tv", {8000, 1}}, {"laptop", {3000, 1}}, {"phone", {500, 1}},
                       {"doorbell", {64, 1}}};
    uint32_t nq = 0;
    int32_t rc;

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zxv_hexlog_create(cap, 64, &qlog));
    for (int i = 0; i < 32; i++) {
        owner_sk[i] = (uint8_t) (i + 1);
        sk3[i] = (uint8_t) (i + 3);
        sk5[i] = (uint8_t) (i + 5);
        sk9[i] = (uint8_t) (i + 9);
    }
    TRY(zxv_ed25519_public(cap, owner_sk, owner_pk));
    TRY(zxv_ed25519_public(cap, sk3, pk3));
    TRY(zxv_ed25519_public(cap, sk5, pk5));
    TRY(zxv_ed25519_public(cap, sk9, pk9));
    TRY(zind_signer_from_seed(cap, sk9, &ml));
    TRY(ztm_planner_create(cap, "no7", owner_pk, qlog, &p));
    TRY(ztm_neighbour_add(p, "no3", ZTM_ALG_ED25519, pk3, 32));
    TRY(ztm_neighbour_add(p, "no5", ZTM_ALG_ED25519, pk5, 32));
    TRY(ztm_neighbour_add(p, "no9", ZTM_ALG_MLDSA87, zind_signer_pk(ml), ZIND_PK_BYTES));
    TRY(ztm_neighbour_add(p, "ham", ZTM_ALG_ED25519, pk9, 32));
    TRY(ztm_agreement_set(p, "no3", (zind_qty){6000, 1}));
    TRY(ztm_agreement_set(p, "no9", (zind_qty){20000, 1}));
    TRY(ztm_agreement_set(p, "ham", (zind_qty){20000, 1}));

    printf("== adverts (no7's WAN is down)\n");
    a = advert("no3", 41, ZTM_MEDIUM_WIFI, 10000, 12);
    TRY(ztm_advert_seal_ed25519(cap, &a, sk3, old, sizeof old, &oldlen));
    a.seq = 42;
    TRY(ztm_advert_seal_ed25519(cap, &a, sk3, buf, sizeof buf, &len));
    TRY(ztm_advert_accept(p, old, oldlen, NOW));
    TRY(ztm_advert_accept(p, buf, len, NOW));
    a = advert("no5", 7, ZTM_MEDIUM_ETHERNET, 50000, 3);
    TRY(ztm_advert_seal_ed25519(cap, &a, sk5, buf, sizeof buf, &len));
    TRY(ztm_advert_accept(p, buf, len, NOW));
    a = advert("no9", 3, ZTM_MEDIUM_POWERLINE, 4000, 25);
    TRY(ztm_advert_seal_mldsa(cap, &a, ml, buf, sizeof buf, &len));
    printf("  no9 (ML-DSA-87, %zu bytes): %s\n", len, ztm_status_name(ztm_advert_accept(p, buf, len, NOW)));
    a = advert("ham", 1, ZTM_MEDIUM_AMATEUR, 9600, 400);
    TRY(ztm_advert_seal_ed25519(cap, &a, sk9, buf, sizeof buf, &len));
    printf("  ham (amateur radio): %s\n", ztm_status_name(ztm_advert_accept(p, buf, len, NOW)));
    printf("  no3 replays advert 41: %s\n", ztm_status_name(ztm_advert_accept(p, old, oldlen, NOW + 5)));

    printf("== no7 probes through no9 and fails, although no9 claims the internet\n");
    rc = ztm_probe_record(p, "no9", "internet", ZXV_LP_FALSE, NOW + 10);
    printf("  probe: %s\n", ztm_status_name(rc));
    show(p, "no3");
    show(p, "no5");
    show(p, "no9");
    show(p, "ham");

    TRY(ztm_plan_make(p, "internet", NOW + 10, 0, d, 4, &pl));
    printf("== plan while no9 is held (%u eligible)\n", pl.eligible);
    for (uint32_t i = 0; i < pl.n; i++) {
        char b[48], w[48];
        size_t n = 0;
        zind_qty_format(pl.a[i].alloc_kbps, b, sizeof b, &n);
        zind_qty_format(d[i].demand_kbps, w, sizeof w, &n);
        printf("  %-8s wants %5s kbit/s -> via %s, %s kbit/s\n", pl.a[i].flow, w, pl.a[i].relay, b);
    }

    printf("== the owner looks at no9 and signs USE (the probe was a local fault)\n");
    TRY(ztm_owner_message(p, "no9", "internet", ZTM_USE, msg, sizeof msg, &ml_));
    TRY(zxv_ed25519_sign(cap, owner_sk, msg, ml_, sig));
    TRY(ztm_hold_resolve(p, "no9", "internet", ZTM_USE, sig));
    TRY(ztm_plan_make(p, "internet", NOW + 20, 0, d, 4, &pl));
    printf("== plan with no9 (%u eligible, %u of %u flows satisfied, tag %s)\n", pl.eligible,
           pl.satisfied, pl.n, zxv_hex_name(pl.tag));
    for (uint32_t i = 0; i < pl.n; i++) {
        char b[48];
        size_t n = 0;
        zind_qty_format(pl.a[i].alloc_kbps, b, sizeof b, &n);
        printf("  %-8s -> via %s, %s kbit/s\n", pl.a[i].flow, pl.a[i].relay, b);
    }
    {
        const char *ids[2] = {"no3", "no9"};
        for (int i = 0; i < 2; i++) {
            zind_qty r;
            char b[48];
            size_t n = 0;
            TRY(ztm_budget_remaining(p, ids[i], &r));
            zind_qty_format(r, b, sizeof b, &n);
            printf("  rmag budget left on %s: %s kbit/s\n", ids[i], b);
        }
    }
    TRY(zxv_hexlog_count(qlog, &nq));
    printf("  quarantine log entries: %u (replay, hold)\n", nq);
    printf("  no5 has no agreement with no7's owner, so it is never used.\n");

    ztm_planner_destroy(p);
    zind_signer_destroy(ml);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
