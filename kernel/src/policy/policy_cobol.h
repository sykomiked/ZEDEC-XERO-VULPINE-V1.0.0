/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* policy_cobol.h — COBOL records for transfers and results, and the
 * ZXV_POLICY entry point (copybooks ZXVPTXN.cpy, ZXVPRES.cpy). */
#ifndef ZXV_POLICY_COBOL_H
#define ZXV_POLICY_COBOL_H

#include "policy.h"

#define POLICY_TXN_MAGIC "ZXQ1"
#define POLICY_TXN_REC_LEN                                                                         \
    129u /* MAGIC 4 FROM 2 TO 2 ASSET 3 AMOUNT 19 DATE 8                                           \
            FJUR 6 TJUR 6 SIGNED 15 DOC 64 */
#define POLICY_RES_MAGIC "ZXP1"
#define POLICY_RES_RULES 10u
#define POLICY_RES_REC_LEN                                                                         \
    41u /* MAGIC 4 DECISION 5 ESCALATE 1 VALUE 1                                                   \
           DENY 10 BOTH 10 FAULT 5 WHERE 5 */

/* 0, or POLICY_E_TXN for any field outside the copybook's rules. */
int32_t policy_parse_txn_record(const uint8_t *rec, uint32_t len, policy_txn_t *t);
void policy_emit_result_record(const policy_result_t *r, uint32_t where,
                               uint8_t rec[POLICY_RES_REC_LEN]);

/* CALL 'ZXV_POLICY' USING ZXV-CONTRACT ZXV-TRANSFER ZXV-POLICY-RESULT.
 * Stateless: no operator packs, LIMIT totals judged from zero (a host that
 * keeps totals calls policy_commit from C). RETURN-CODE is 0 ALLOW, 1 DENY,
 * or the negative fault code. Not reentrant (one static contract). */
int ZXV_POLICY(const char *contract, const char *transfer, char *result);

#endif /* ZXV_POLICY_COBOL_H */
