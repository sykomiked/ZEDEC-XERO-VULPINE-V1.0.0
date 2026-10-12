/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trace_demo.c — a medicine pack on tri-trace: the maker commissions the
 * serial, ships it to a wholesaler, who ships it to a pharmacy, which
 * dispenses it. A second pack is cloned: the same serial turns up at
 * another pharmacy, the scan is recorded as evidence and the pack is
 * frozen as BOTH until a reviewer signs a decision. Both chains are then
 * verified offline with nothing but the public keys. */
#include <stdio.h>

#include "zttrace.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, ztt_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char *k_state[] = {"-", "held", "in transit", "dispensed", "BOTH (frozen for review)",
                                "counterfeit"};
static const char *k_lp[] = {"NEITHER", "TRUE", "FALSE", "BOTH"};

static int offline(const zxv_cap *cap, const ztt_registry *r, const ztt_trust *tr, const char *item) {
    static uint8_t buf[65536];
    size_t len = 0;
    ztt_verdict v;
    TRY(ztt_item_export(r, item, buf, sizeof buf, &len));
    TRY(ztt_verify_export(cap, item, buf, len, tr, &v));
    printf("  offline: %s, %u records (%zu bytes): %s, %s%s%s\n", item, v.events, len, k_lp[v.lp & 3],
           k_state[v.state], v.holder[0] ? " at " : "", v.holder);
    buf[len / 2] ^= 1;
    printf("  offline, one bit flipped: %s\n",
           ztt_status_name(ztt_verify_export(cap, item, buf, len, tr, &v)));
    return 0;
}

int main(void) {
    static const char *ids[4] = {"MAKER", "WHOLESALE", "PHARMACY", "PHARMACY2"};
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *op = NULL, *rev = NULL, *k[4] = {NULL};
    const uint8_t *pks[4], *rpk[1];
    ztt_registry *r = NULL;
    ztt_trust tr;
    uint8_t seed[32] = {0}, msg[192], sig[ZIND_SIG_BYTES];
    size_t len = 0;
    uint32_t nq = 0;
    int32_t st = 0;
    const char *pack = "01.09506000134352.21.SN1001", *clone = "01.09506000134352.21.SN1002";

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zxv_hexlog_create(cap, 64, &qlog));
    seed[0] = 1;
    TRY(zind_signer_from_seed(cap, seed, &op));
    seed[0] = 2;
    TRY(zind_signer_from_seed(cap, seed, &rev));
    TRY(ztt_registry_create(cap, op, qlog, &r));
    for (int i = 0; i < 4; i++) {
        seed[0] = (uint8_t) (10 + i);
        TRY(zind_signer_from_seed(cap, seed, &k[i]));
        pks[i] = zind_signer_pk(k[i]);
        TRY(ztt_party_add(r, ids[i], pks[i]));
    }
    TRY(ztt_reviewer_add(r, zind_signer_pk(rev)));
    rpk[0] = zind_signer_pk(rev);
    tr = (ztt_trust){ids, pks, 4, rpk, 1, zind_signer_pk(op)};

    printf("tri-trace demo\n  pack %s\n", pack);
    TRY(ztt_scan(r, pack, "MAKER", k[0], ZTT_EV_COMMISSION, NULL, 1000));
    TRY(ztt_scan(r, pack, "MAKER", k[0], ZTT_EV_SHIP, "WHOLESALE", 1100));
    TRY(ztt_scan(r, pack, "WHOLESALE", k[1], ZTT_EV_RECEIVE, NULL, 1200));
    printf("  the wholesaler's scanner re-sends its receipt: %s\n",
           ztt_status_name(ztt_scan(r, pack, "WHOLESALE", k[1], ZTT_EV_RECEIVE, NULL, 1200)));
    printf("  the maker's key signing as the pharmacy: %s\n",
           ztt_status_name(ztt_scan(r, pack, "PHARMACY", k[0], ZTT_EV_RECEIVE, NULL, 1250)));
    TRY(ztt_scan(r, pack, "WHOLESALE", k[1], ZTT_EV_SHIP, "PHARMACY", 1300));
    TRY(ztt_scan(r, pack, "PHARMACY", k[2], ZTT_EV_RECEIVE, NULL, 1400));
    TRY(ztt_scan(r, pack, "PHARMACY", k[2], ZTT_EV_DISPENSE, NULL, 1500));
    TRY(ztt_item_state(r, pack, &st, NULL, NULL, 0));
    printf("  commissioned, shipped twice, received, dispensed: %s\n", k_state[st]);
    if (offline(cap, r, &tr, pack)) return 1;

    printf("  pack %s\n", clone);
    TRY(ztt_scan(r, clone, "MAKER", k[0], ZTT_EV_COMMISSION, NULL, 2000));
    TRY(ztt_scan(r, clone, "MAKER", k[0], ZTT_EV_SHIP, "PHARMACY", 2100));
    TRY(ztt_scan(r, clone, "PHARMACY", k[2], ZTT_EV_RECEIVE, NULL, 2200));
    printf("  the same serial is scanned in at PHARMACY2: %s\n",
           ztt_status_name(ztt_scan(r, clone, "PHARMACY2", k[3], ZTT_EV_RECEIVE, NULL, 2300)));
    zxv_hexlog_count(qlog, &nq);
    printf("  quarantine log records: %u; dispensing now: %s\n", nq,
           ztt_status_name(ztt_scan(r, clone, "PHARMACY", k[2], ZTT_EV_DISPENSE, NULL, 2400)));
    if (offline(cap, r, &tr, clone)) return 1;
    TRY(ztt_review_message(r, clone, ZTT_COUNTERFEIT, "", msg, sizeof msg, &len));
    TRY(zind_sign(cap, rev, msg, len, ZTT_REVIEW_CTX, sig));
    TRY(ztt_review_resolve(r, clone, zind_signer_pk(rev), ZTT_COUNTERFEIT, "", sig));
    TRY(ztt_item_state(r, clone, &st, NULL, NULL, 0));
    printf("  the reviewer signed COUNTERFEIT: %s; any further scan: %s\n", k_state[st],
           ztt_status_name(ztt_scan(r, clone, "PHARMACY", k[2], ZTT_EV_DISPENSE, NULL, 2500)));
    if (offline(cap, r, &tr, clone)) return 1;
    printf("  (advisory: the registry flags; people decide what happens to the stock)\n");

    ztt_registry_destroy(r);
    for (int i = 0; i < 4; i++) zind_signer_destroy(k[i]);
    zind_signer_destroy(op);
    zind_signer_destroy(rev);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
