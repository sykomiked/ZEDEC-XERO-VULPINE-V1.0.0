/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* peg_demo.c — one operator day on a sovereign-peg desk: two test
 * currencies pegged by their issuers' signatures, a quote, issuance up to
 * the backing and one unit past it, an allowed flow, an outflow above the
 * cap refused and quarantined, a flow with conflicting authorisations held
 * as BOTH and released by an officer's signature, an epoch rollover, and
 * the signed operator log verified at the end. Test currencies only. */
#include <stdio.h>
#include <string.h>

#include "zspeg.h"

static int fail(const char *what, int32_t st) {
    char err[300];
    size_t n = 0;
    if (zind_last_error(err, sizeof err, &n) != ZXV_OK) err[0] = 0;
    fprintf(stderr, "peg_demo: %s: %s (%s)\n", what, zsp_status_name(st), err);
    return 1;
}

#define TRY(what, expr)                                                                        \
    do {                                                                                       \
        int32_t st_ = (expr);                                                                  \
        if (st_ != ZXV_OK) return fail(what, st_);                                             \
    } while (0)

static int32_t peg(const zxv_cap *cap, zsp_desk *d, const uint8_t sk[4896], uint8_t slot,
                   const char *rate, uint8_t minor, uint64_t epoch) {
    uint64_t num = 0, den = 0;
    uint8_t m[ZXV_VHUB_PEG_MSG_BYTES], sig[ZIND_SIG_BYTES];
    int32_t st = zsp_peg_parse(rate, minor, &num, &den);
    if (st == ZXV_OK) st = zsp_peg_message(slot, num, den, epoch, m);
    /* on the issuer's own machine: */
    if (st == ZXV_OK) st = zxv_mldsa87_sign(cap, sk, m, sizeof m, NULL, 0, sig);
    if (st == ZXV_OK) st = zsp_peg_set(d, cap, slot, num, den, epoch, sig);
    if (st == ZXV_OK)
        printf("peg   slot %u: %s units per V-Bill = %llu/%llu minor (epoch %llu)\n", slot, rate,
               (unsigned long long) num, (unsigned long long) den, (unsigned long long) epoch);
    return st;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *op = NULL, *rd = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *desk_key = NULL, *officer = NULL;
    zsp_desk *d = NULL;
    uint8_t s1[32], s2[32], s3[32], s4[32], pk_a[ZIND_PK_BYTES], sk_a[4896], pk_b[ZIND_PK_BYTES],
        sk_b[4896];
    uint32_t a1 = 0, b1 = 0, id = 0;
    uint64_t amt = 0, vb = 0, rcv = 0, circ = 0, flt = 0;
    int32_t st, covered = 0;
    zsp_flow fl;

    for (int i = 0; i < 32; i++) {
        s1[i] = (uint8_t) (1 + i);
        s2[i] = (uint8_t) (2 + i);
        s3[i] = (uint8_t) (3 + i);
        s4[i] = (uint8_t) (4 + i);
    }
    TRY("context", zxv_ctx_create(&ctx));
    TRY("operator cap", zxv_cap_create(ctx, 1, NULL, ZSP_CAP_OPERATOR, &op));
    TRY("read cap", zxv_cap_attenuate(op, ZXV_CAP_LEDGER_READ, &rd));
    TRY("quarantine log", zxv_hexlog_create(op, 256, &qlog));
    TRY("desk key", zind_signer_from_seed(op, s1, &desk_key));
    TRY("officer key", zind_signer_from_seed(op, s2, &officer));
    TRY("issuer A key", zxv_mldsa87_keygen_seed(op, s3, pk_a, sk_a));
    TRY("issuer B key", zxv_mldsa87_keygen_seed(op, s4, pk_b, sk_b));
    TRY("desk", zsp_desk_create(op, s1, desk_key, qlog, &d));
    TRY("officer", zsp_officer_add(d, op, zind_signer_pk(officer)));

    /* two test currencies; each float holds 1000 V-Bills of backing */
    TRY("ccy A", zsp_ccy_add(d, op, 0, "TESTA", 2, pk_a, 50000000, 1000));
    TRY("ccy B", zsp_ccy_add(d, op, 1, "TESTB", 2, pk_b, 50000000, 1000));
    TRY("peg A", peg(op, d, sk_a, 0, "1.50", 2, 1));
    TRY("peg B", peg(op, d, sk_b, 1, "3", 2, 1));
    TRY("limit A", zsp_limit_add(d, op, 0, 100000));
    TRY("limit B", zsp_limit_add(d, op, 1, 100000));
    TRY("account", zsp_account_open(d, op, 0, 1, &a1));
    TRY("account", zsp_account_open(d, op, 1, 2, &b1));

    TRY("quote", zsp_quote(d, rd, 0, 1, 1000, &amt, &vb, &rcv));
    printf("quote 10.00 TESTA -> largest exact %llu minor, %llu V-Bills, %llu TESTB minor\n",
           (unsigned long long) amt, (unsigned long long) vb, (unsigned long long) rcv);

    /* 1000 V-Bills at 150 minor each back exactly 150000 minor of TESTA */
    TRY("issue", zsp_issue(d, op, a1, 150000));
    st = zsp_issue(d, op, a1, 1);
    printf("issue 1500.00 TESTA: ok; 0.01 more: %s\n", zsp_status_name(st));
    TRY("coverage", zsp_coverage(d, rd, 0, &circ, &flt, &covered));
    printf("cover TESTA circulation %llu minor, float %llu V-Bills, covered %d\n",
           (unsigned long long) circ, (unsigned long long) flt, covered);

    memset(&fl, 0, sizeof fl);
    fl.from_slot = 0;
    fl.to_slot = 1;
    fl.sender = a1;
    fl.receiver = b1;
    fl.amount = 3000;
    fl.tick = 100;
    fl.approvals = 1;
    st = zsp_flow_submit(d, op, &fl, &id);
    printf("flow  #%u 30.00 TESTA, one approval: %s\n", id, zsp_status_name(st));

    TRY("outflow cap", zsp_outflow_cap_set(d, op, 0, 20000));
    fl.amount = 30000;
    fl.approvals = 3;
    st = zsp_flow_submit(d, op, &fl, &id);
    printf("flow  #%u 300.00 TESTA over the 200.00 cap: %s\n", id, zsp_status_name(st));

    fl.amount = 15000;
    fl.approvals = 1;
    fl.objections = 1;
    st = zsp_flow_submit(d, op, &fl, &id);
    printf("flow  #%u 150.00 TESTA, one approval and one objection: %s\n", id,
           zsp_status_name(st));
    {
        uint8_t m[64], sig[ZIND_SIG_BYTES];
        size_t len = 0;
        TRY("decision", zsp_decision_message(d, id, ZSP_RELEASE, m, sizeof m, &len));
        /* the officer, a person, signs on their own device: */
        TRY("sign", zind_sign(op, officer, m, len, ZSP_DECISION_CTX, sig));
        st = zsp_decide(d, op, id, zind_signer_pk(officer), ZSP_RELEASE, 200, sig);
        printf("decide #%u released by the officer: %s\n", id, zsp_status_name(st));
        st = zsp_decide(d, op, id, zind_signer_pk(officer), ZSP_RELEASE, 200, sig);
        printf("decide #%u the same decision again: %s\n", id, zsp_status_name(st));
    }
    {
        uint64_t ep = 0;
        int64_t ba = 0, bb = 0;
        TRY("epoch", zsp_epoch_next(d, op, &ep));
        zsp_balance(d, rd, a1, &ba);
        zsp_balance(d, rd, b1, &bb);
        printf("epoch %llu; holder A %lld minor, holder B %lld minor\n", (unsigned long long) ep,
               (long long) ba, (long long) bb);
    }
    {
        const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
        uint32_t n = 0, q = 0;
        uint64_t postings = 0;
        TRY("check", zsp_check(d, rd, &postings));
        TRY("log", zind_chain_verify(op, zsp_log(d), trust, 1, NULL));
        zind_chain_count(zsp_log(d), &n);
        zxv_hexlog_count(qlog, &q);
        printf("check hub ok (%llu postings), operator log %u signed records verify, %u "
               "quarantine records\n",
               (unsigned long long) postings, n, q);
    }
    zsp_desk_destroy(d);
    zind_signer_destroy(officer);
    zind_signer_destroy(desk_key);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(rd);
    zxv_cap_destroy(op);
    zxv_ctx_destroy(ctx);
    return 0;
}
