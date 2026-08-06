/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_logistics.c — known-answer proofs for the bare-roots logistics module.
 * Every number is checked against an answer computed by hand. */

#include <stdio.h>
#include "logistics.h"

static int g_assertions = 0;
#define CHECK(cond, msg) do {                                   \
    g_assertions++;                                             \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); return 1; }     \
    else         { printf("ok  : %s\n", (msg)); }               \
} while (0)

/* SR equality for the host (double) — integer-valued answers compare exactly. */
static int sr_eq(surplus_real_t a, surplus_real_t b) { return SR_CMP(a, b) == 0; }

int main(void) {
    log_state_t st;
    log_init(&st);

    /* A supplied photo-attestation CID — the ops boundary. We do NOT compute it
     * from a real photo; it is handed to us and stored verbatim. */
    uint8_t proof_cid[LOG_CID_LEN];
    for (uint32_t i = 0; i < LOG_CID_LEN; i++) proof_cid[i] = (uint8_t)(0xA0 + i);

    /* Parties: a syndicate of four buyers banding together. */
    uint32_t parties[4] = { 7, 8, 9, 10 };

    /* ===================================================================== */
    /* ANCHOR 1 — a syndicate splits a fixed contract in proportion to shares, */
    /*            summing EXACTLY to the total (conserved).                    */
    /* ===================================================================== */
    int32_t cid = log_contract_open(&st, parties, 4, SR_FROM_INT(100),
                                    proof_cid, NULL);
    CHECK(cid > 0, "contract opens, returns a positive id");

    surplus_real_t shares[4] = { SR_FROM_INT(1), SR_FROM_INT(1),
                                 SR_FROM_INT(2), SR_FROM_INT(1) }; /* 1:1:2:1 of 100 */
    int32_t rc = log_syndicate_form(&st, (uint64_t)cid, parties, shares, 4);
    CHECK(rc == LOG_OK, "syndicate forms over the contract");

    const log_split_t *sp = log_split_find(&st, (uint64_t)cid);
    CHECK(sp != NULL, "syndicate split is recorded");
    /* Known answer: 100 * {1,1,2,1}/5 = {20,20,40,20}. */
    CHECK(sr_eq(sp->alloc[0], SR_FROM_INT(20)), "member 0 gets exactly 20 units");
    CHECK(sr_eq(sp->alloc[1], SR_FROM_INT(20)), "member 1 gets exactly 20 units");
    CHECK(sr_eq(sp->alloc[2], SR_FROM_INT(40)), "member 2 gets exactly 40 units");
    CHECK(sr_eq(sp->alloc[3], SR_FROM_INT(20)), "member 3 gets exactly 20 units");
    surplus_real_t sum = SR_ADD(SR_ADD(sp->alloc[0], sp->alloc[1]),
                                SR_ADD(sp->alloc[2], sp->alloc[3]));
    CHECK(sr_eq(sum, SR_FROM_INT(100)), "allocations sum EXACTLY to the total (conserved)");

    /* ===================================================================== */
    /* ANCHOR 5a — commodity backing records the EXACT supplied proof_cid.     */
    /* ===================================================================== */
    const log_contract_t *cc = log_contract_find(&st, (uint64_t)cid);
    CHECK(cc != NULL, "contract is findable");
    CHECK(cc->has_proof_cid, "contract carries a bound photo-attestation");
    int cid_ok = 1;
    for (uint32_t i = 0; i < LOG_CID_LEN; i++)
        if (cc->proof_cid[i] != (uint8_t)(0xA0 + i)) cid_ok = 0;
    CHECK(cid_ok, "proof_cid stored byte-for-byte as supplied (ops boundary)");

    /* An UNBOUND attestation stays unbound — never invented. */
    int32_t cid2 = log_contract_open(&st, parties, 2, SR_FROM_INT(50), NULL, NULL);
    CHECK(cid2 > 0, "second contract opens with NO proof supplied");
    const log_contract_t *cc2 = log_contract_find(&st, (uint64_t)cid2);
    CHECK(cc2 && !cc2->has_proof_cid, "absent proof_cid => UNBOUND, not fabricated");

    /* ===================================================================== */
    /* ANCHOR 2 — a secondary split gives the negotiator a bounded margin       */
    /*            (<= 11%) and the sub-contractors the majority; over 11% is     */
    /*            REFUSED.                                                       */
    /* ===================================================================== */
    int32_t pid = log_contract_open(&st, parties, 1, SR_FROM_INT(100), proof_cid, NULL);
    CHECK(pid > 0, "parent supply contract opens");

    uint32_t subs[3] = { 21, 22, 23 };
    surplus_real_t sub_shares[3] = { SR_FROM_INT(1), SR_FROM_INT(1), SR_FROM_INT(1) };
    surplus_real_t margin_11 = SR_DIV(SR_FROM_INT(11), SR_FROM_INT(100)); /* the customary */
    rc = log_subcontract_split(&st, (uint64_t)pid, subs, sub_shares, 3,
                               /*negotiator*/ 99, margin_11, NULL);
    CHECK(rc == LOG_OK, "secondary split at the customary 11% is accepted");

    const log_split_t *ss = log_split_find(&st, (uint64_t)pid);
    CHECK(ss != NULL && ss->is_subcontract, "sub-contract split recorded");
    /* Conservation: negotiator cut + all sub allocations == the parent total. */
    surplus_real_t sub_sum = SR_ADD(SR_ADD(ss->alloc[0], ss->alloc[1]), ss->alloc[2]);
    surplus_real_t whole = SR_ADD(ss->negotiator_cut, sub_sum);
    CHECK(sr_eq(whole, SR_FROM_INT(100)),
          "negotiator cut + sub allocations == parent total (conserved)");
    /* Bounded: the cut is at most 11 of 100. */
    CHECK(SR_CMP(ss->negotiator_cut, SR_FROM_INT(11)) <= 0,
          "negotiator margin is bounded at <= 11% of the units");
    /* Majority: the sub-contractors together hold strictly more than the cut. */
    CHECK(SR_CMP(sub_sum, ss->negotiator_cut) > 0,
          "sub-contractors take the MAJORITY, the negotiator a minority slice");

    /* Over 11% must be REFUSED — nothing written. */
    int32_t pid2 = log_contract_open(&st, parties, 1, SR_FROM_INT(100), proof_cid, NULL);
    CHECK(pid2 > 0, "second parent opens for the greedy-margin test");
    surplus_real_t margin_12 = SR_DIV(SR_FROM_INT(12), SR_FROM_INT(100));
    rc = log_subcontract_split(&st, (uint64_t)pid2, subs, sub_shares, 3, 99, margin_12, NULL);
    CHECK(rc == LOG_ERR_MARGIN, "a 12% margin is REFUSED (over the customary 11%)");
    CHECK(log_split_find(&st, (uint64_t)pid2) == NULL,
          "the refused split wrote NOTHING (no partial state)");

    /* ===================================================================== */
    /* ANCHOR 4 — a contract term that inflicts asymmetric harm is VOID         */
    /*            (compose onepolicy).                                          */
    /* ===================================================================== */
    op_term_t good = (op_term_t){0};
    good.give_a = SR_FROM_INT(10); good.give_b = SR_FROM_INT(10);
    good.reciprocal = true;
    int32_t cok = log_contract_open(&st, parties, 2, SR_FROM_INT(30), proof_cid, &good);
    CHECK(cok > 0, "a symbiotic, reciprocal term opens a contract");

    op_term_t harmful = (op_term_t){0};
    harmful.give_a = SR_FROM_INT(5);   /* A conveys value ...        */
    harmful.give_b = SR_FROM_INT(0);   /* ... and receives nothing   */
    harmful.harm_a = SR_FROM_INT(10);  /* ... AND is harmed          */
    harmful.reciprocal = false;
    int32_t cbad = log_contract_open(&st, parties, 2, SR_FROM_INT(30), proof_cid, &harmful);
    CHECK(cbad == LOG_ERR_VOID, "an asymmetric-harm term is VOID — the contract is refused");
    /* Also refuse a greedy sub-split whose term is harmful, even under 11%. */
    int32_t pid3 = log_contract_open(&st, parties, 1, SR_FROM_INT(100), proof_cid, NULL);
    rc = log_subcontract_split(&st, (uint64_t)pid3, subs, sub_shares, 3, 99, margin_11, &harmful);
    CHECK(rc == LOG_ERR_VOID, "a sub-split with an asymmetric-harm term is VOID");

    /* ===================================================================== */
    /* ANCHOR 3 — escrow HOLDS on deposit and RELEASES only with a delivery     */
    /*            proof; no proof => LOG_HELD, funds unchanged.                 */
    /* ===================================================================== */
    rc = log_escrow_deposit(&st, (uint64_t)cid, SR_FROM_INT(500)); /* material capital */
    CHECK(rc == LOG_OK, "material capital deposited into escrow");
    cc = log_contract_find(&st, (uint64_t)cid);
    CHECK(sr_eq(cc->escrow_held, SR_FROM_INT(500)), "escrow now holds 500 units of capital");

    /* No proof (UNBOUND) => HELD, untouched. */
    log_result_t held = log_escrow_release(&st, (uint64_t)cid, NULL);
    CHECK(held.status == LOG_HELD, "no delivery proof => LOG_HELD");
    CHECK(sr_eq(held.released, SR_ZERO), "nothing released without a proof");
    cc = log_contract_find(&st, (uint64_t)cid);
    CHECK(sr_eq(cc->escrow_held, SR_FROM_INT(500)), "escrow unchanged — never auto-released");
    CHECK(!cc->delivered, "contract is not marked delivered without a proof");

    /* A proof that does NOT confirm also holds. */
    log_delivery_t unconfirmed = (log_delivery_t){0};
    unconfirmed.confirming_party = 7;
    unconfirmed.confirmed = false;
    log_result_t still = log_escrow_release(&st, (uint64_t)cid, &unconfirmed);
    CHECK(still.status == LOG_HELD, "an unconfirmed attestation still => LOG_HELD");
    cc = log_contract_find(&st, (uint64_t)cid);
    CHECK(sr_eq(cc->escrow_held, SR_FROM_INT(500)), "escrow STILL untouched");

    /* ===================================================================== */
    /* ANCHOR 5b — a delivery proof releases the escrow AND raises credibility. */
    /* ===================================================================== */
    surplus_real_t cred_before = log_credibility(&st, 7);
    CHECK(sr_eq(cred_before, SR_ZERO), "party 7 starts with zero credibility");

    log_delivery_t landed = (log_delivery_t){0};
    landed.confirming_party = 7;
    landed.confirmed = true;
    for (uint32_t i = 0; i < LOG_CID_LEN; i++) landed.delivery_cid[i] = (uint8_t)i;
    log_result_t rel = log_escrow_release(&st, (uint64_t)cid, &landed);
    CHECK(rel.status == LOG_RELEASED, "a confirmed delivery RELEASES the escrow");
    CHECK(sr_eq(rel.released, SR_FROM_INT(500)), "the full 500 is paid out");
    CHECK(sr_eq(rel.escrow_remaining, SR_ZERO), "nothing remains held after release");
    cc = log_contract_find(&st, (uint64_t)cid);
    CHECK(cc->delivered, "contract is marked delivered");
    CHECK(sr_eq(cc->escrow_held, SR_ZERO), "escrow_held drained to zero");

    surplus_real_t cred_after = log_credibility(&st, 7);
    CHECK(SR_CMP(cred_after, cred_before) > 0,
          "party 7's credibility RISES with the completed delivery");
    /* Every party on the contract earns follow-through, not just the confirmer. */
    CHECK(SR_CMP(log_credibility(&st, 9), SR_ZERO) > 0,
          "another syndicate member's credibility also rises");

    /* IDEMPOTENCY: re-releasing an already-delivered contract pays NOTHING and does
     * NOT re-credit — one delivery is one rung, so credibility cannot be farmed. */
    surplus_real_t cred7_once = log_credibility(&st, 7);
    log_result_t again = log_escrow_release(&st, (uint64_t)cid, &landed);
    CHECK(again.status == LOG_HELD, "a second release on a delivered contract => LOG_HELD");
    CHECK(sr_eq(again.released, SR_ZERO), "a second release pays out nothing");
    CHECK(SR_CMP(log_credibility(&st, 7), cred7_once) == 0,
          "credibility does NOT rise again — no follow-through farming");

    printf("\nALL %d ASSERTIONS PASSED\n", g_assertions);
    return 0;
}
