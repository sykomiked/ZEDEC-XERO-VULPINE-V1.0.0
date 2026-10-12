/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* command_demo.c — mesh-command at a wildfire. Units report over a patchy
 * mesh; an engine says road R12 is open, a drone says it is closed. The
 * kit holds R12 as BOTH and recommends nothing that depends on it. The
 * incident commander signs a decision naming the evidence they saw and
 * why; a forged decision and a unit's equivocation are refused and
 * logged. The advice proposes the evacuation over R9; the commander
 * approves it with a signature. The journal is replayed offline from
 * public keys. Advisory only: the kit dispatches nothing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zmcommand.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zmc_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char *INC = "WILDFIRE-0817";
static const char *k_rec[] = {"-", "ASSIGN", "HELD (resolve first)", "UNKNOWN (no reports)",
                              "BLOCKED (fact says otherwise)", "NO RESOURCE"};

static zind_signer *signer(const zxv_cap *cap, uint8_t v) {
    uint8_t seed[32];
    zind_signer *s = NULL;
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (v + i);
    if (zind_signer_from_seed(cap, seed, &s) != ZXV_OK) return NULL;
    return s;
}

static int32_t report_in(const zxv_cap *cap, zmc_incident *inc, const zind_signer *k, const char *unit,
                    uint64_t no, const char *fact, int32_t claim, const char *note,
                    zmc_ingest_info *info) {
    uint8_t buf[5000];
    size_t len = 0;
    zmc_report r;
    int32_t st;
    memset(&r, 0, sizeof r);
    snprintf(r.fact, sizeof r.fact, "%s", fact);
    r.claim = claim;
    r.observed = 1723900000 + no * 60;
    r.lat_e6 = 38440000;
    r.lon_e6 = -122710000;
    snprintf(r.note, sizeof r.note, "%s", note);
    st = zmc_report_sign(cap, k, INC, unit, no, &r, buf, sizeof buf, &len);
    if (st != ZXV_OK) return st;
    return zmc_ingest(inc, buf, len, info);
}

static void show(const zmc_incident *inc, const char *fact) {
    zmc_fact_view v;
    if (zmc_fact_state(inc, fact, &v) != ZXV_OK) return;
    printf("  %-16s %-7s (%u say TRUE, %u say FALSE; epoch %u%s; tag %s)\n", fact,
           zxv_lp_name(v.lp), v.pos, v.neg, v.epoch,
           v.decision == ZXV_LP_NEITHER ? "" : ", commander decided", zxv_hex_name(v.tag));
}

static void advice(const zmc_incident *inc) {
    zmc_rec rec[8];
    size_t n = 0;
    if (zmc_recommend(inc, rec, 8, &n) != ZXV_OK) return;
    for (size_t i = 0; i < n; i++)
        printf("  %-12s %s%s%s\n", rec[i].task, k_rec[rec[i].status],
               rec[i].resource[0] ? " " : "", rec[i].resource);
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *op = NULL, *ic = NULL, *e7 = NULL, *d1 = NULL, *m3 = NULL, *rogue = NULL;
    zmc_incident *inc = NULL;
    zmc_ingest_info info;
    uint8_t msg[512], sig[ZIND_SIG_BYTES];
    size_t len = 0;
    int32_t st;
    const char *why = "drone thermal at 14:02 shows the culvert under R12 collapsed";

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_CRYPTO_SIGN | ZXV_CAP_ATTEST, &cap));
    TRY(zxv_hexlog_create(cap, 256, &qlog));
    op = signer(cap, 1);
    ic = signer(cap, 2);
    e7 = signer(cap, 3);
    d1 = signer(cap, 4);
    m3 = signer(cap, 5);
    rogue = signer(cap, 6);
    if (!op || !ic || !e7 || !d1 || !m3 || !rogue) return 1;
    TRY(zmc_incident_create(cap, INC, op, qlog, &inc));
    TRY(zmc_unit_add(inc, "ENGINE-7", zind_signer_pk(e7)));
    TRY(zmc_unit_add(inc, "DRONE-1", zind_signer_pk(d1)));
    TRY(zmc_unit_add(inc, "MEDIC-3", zind_signer_pk(m3)));
    TRY(zmc_commander_add(inc, "IC-RIVERA", zind_signer_pk(ic)));
    TRY(zmc_resource_add(inc, "BUS-2", "bus"));
    TRY(zmc_resource_add(inc, "AMB-4", "ambulance"));
    TRY(zmc_task_add(inc, "evac-via-R12", 9, "bus", "road:R12/open", ZXV_LP_TRUE));
    TRY(zmc_task_add(inc, "evac-via-R9", 8, "bus", "road:R9/open", ZXV_LP_TRUE));
    TRY(zmc_task_add(inc, "burn-unit", 7, "ambulance", NULL, 0));

    printf("mesh-command demo (advisory: the kit recommends; people decide and dispatch)\n\n");
    printf("field reports arrive over the mesh:\n");
    TRY(report_in(cap, inc, e7, "ENGINE-7", 1, "road:R12/open", ZXV_LP_TRUE, "passed mile 4 at 13:40",
             &info));
    TRY(report_in(cap, inc, m3, "MEDIC-3", 1, "road:R9/open", ZXV_LP_TRUE, "clear to the school",
             &info));
    TRY(report_in(cap, inc, d1, "DRONE-1", 1, "road:R12/open", ZXV_LP_FALSE, "culvert down at mile 6",
             &info));
    printf("  DRONE-1 contradicts ENGINE-7 on road:R12/open: %s -> %s%s\n",
           zxv_lp_name(info.lp_before), zxv_lp_name(info.lp_after),
           info.new_hold ? " (held for the commander, logged)" : "");
    st = report_in(cap, inc, e7, "ENGINE-7", 1, "road:R12/open", ZXV_LP_TRUE, "passed mile 4 at 13:40",
              NULL);
    printf("  ENGINE-7's report 1 again over another path: %s\n", zmc_status_name(st));
    st = report_in(cap, inc, e7, "ENGINE-7", 1, "road:R12/open", ZXV_LP_FALSE, "changed my mind", NULL);
    printf("  ENGINE-7 signs a different report 1: %s (refused, logged)\n", zmc_status_name(st));
    show(inc, "road:R12/open");
    show(inc, "road:R9/open");

    printf("\nadvice while R12 is held:\n");
    advice(inc);

    printf("\nthe incident commander decides R12 is closed:\n");
    TRY(zmc_decision_message(inc, "road:R12/open", ZXV_LP_FALSE, "IC-RIVERA", why, msg, sizeof msg,
                             &len));
    TRY(zind_sign(cap, rogue, msg, len, ZMC_DECISION_CTX, sig));
    printf("  a decision signed by another key: %s\n",
           zmc_status_name(zmc_resolve(inc, "road:R12/open", ZXV_LP_FALSE, "IC-RIVERA", why, sig)));
    TRY(zind_sign(cap, ic, msg, len, ZMC_DECISION_CTX, sig));
    printf("  IC-RIVERA's decision presented as OPEN instead: %s\n",
           zmc_status_name(zmc_resolve(inc, "road:R12/open", ZXV_LP_TRUE, "IC-RIVERA", why, sig)));
    TRY(zmc_resolve(inc, "road:R12/open", ZXV_LP_FALSE, "IC-RIVERA", why, sig));
    printf("  applied; logged with who (IC-RIVERA) and why (\"%s\")\n", why);
    show(inc, "road:R12/open");

    printf("\nadvice now:\n");
    advice(inc);
    TRY(zmc_approve_message(inc, "evac-via-R9", "BUS-2", "IC-RIVERA", msg, sizeof msg, &len));
    TRY(zind_sign(cap, ic, msg, len, ZMC_APPROVE_CTX, sig));
    TRY(zmc_approve(inc, "evac-via-R9", "BUS-2", "IC-RIVERA", sig));
    printf("  IC-RIVERA approves BUS-2 for evac-via-R9 (signed)\n");
    TRY(zmc_approve_message(inc, "evac-via-R12", "AMB-4", "IC-RIVERA", msg, sizeof msg, &len));
    TRY(zind_sign(cap, ic, msg, len, ZMC_APPROVE_CTX, sig));
    printf("  an approval of AMB-4 for evac-via-R12: %s\n",
           zmc_status_name(zmc_approve(inc, "evac-via-R12", "AMB-4", "IC-RIVERA", sig)));

    {
        const char *uids[3] = {"ENGINE-7", "DRONE-1", "MEDIC-3"};
        const uint8_t *upks[3] = {zind_signer_pk(e7), zind_signer_pk(d1), zind_signer_pk(m3)};
        const char *cids[1] = {"IC-RIVERA"};
        const uint8_t *cpks[1] = {zind_signer_pk(ic)};
        zmc_trust t = {uids, upks, 3, cids, cpks, 1, zind_signer_pk(op)};
        zmc_verdict v;
        uint8_t *j;
        uint32_t qn = 0;
        if (zind_chain_export(zmc_journal(inc), NULL, 0, &len) != ZXV_E_BUFFER) return 1;
        j = (uint8_t *) malloc(len);
        if (!j) return 1;
        TRY(zind_chain_export(zmc_journal(inc), j, len, &len));
        st = zmc_verify_journal(cap, j, len, &t, &v);
        printf("\njournal (%zu bytes) replayed offline from public keys: %s\n", len,
               zmc_status_name(st));
        printf("  %u records: %u reports, %u facts, %u held, %u decision(s), %u approval(s)\n",
               v.records, v.reports, v.facts, v.held, v.decisions, v.approvals);
        j[len / 2] ^= 1;
        printf("  the same journal with one flipped bit: %s\n",
               zmc_status_name(zmc_verify_journal(cap, j, len, &t, &v)));
        free(j);
        zxv_hexlog_count(qlog, &qn);
        printf("quarantine log: %u entries (the hold, the equivocation, two refused decisions, the resolution), "
               "verifies: %s\n",
               qn, zmc_status_name(zxv_hexlog_verify(qlog)));
    }

    zmc_incident_destroy(inc);
    zind_signer_destroy(op);
    zind_signer_destroy(ic);
    zind_signer_destroy(e7);
    zind_signer_destroy(d1);
    zind_signer_destroy(m3);
    zind_signer_destroy(rogue);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
