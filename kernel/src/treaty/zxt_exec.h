/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxt_exec.h — run a treaty on the engine, in userland (see zxt.h).
 *
 * zxt.h only reads and checks treaties. This half runs one: it opens a
 * fresh engine (src/engine/zxv_engine.h) in the caller's workspace, funds
 * each party's deposit, nets the obligations, and for SETL posts the net
 * legs with the 0.08889% assurance fee and pays each party's claim back
 * out. Every number it returns is a signed integer that fits PIC S9(18).
 *
 *   NET   net[i]      = what party i is owed minus what it owes (gross)
 *         leg[]       = the netted legs (at most n_party - 1 of them)
 *         balance[i]  = deposit (nothing is posted)
 *   SETL  as NET, then the legs are posted (all or nothing) and
 *         fees        = the assurance fee charged on them
 *         dividend[i] = party i's weight share of what the fee put in the
 *                       V-Bill dividend bucket: floor shares, then one unit
 *                       each to the largest remainders (ties: lowest index),
 *                       so the shares sum to the bucket's growth exactly.
 *                       REPORTED ONLY: it stays in the bucket.
 *         balance[i]  = after the legs, the fee and the claim withdrawal.
 *
 * E1  Fail closed: a treaty zxt_validate refuses, a size error in a sum
 *     (ZXT_E_SIZE), or any engine refusal (ZXT_E_EXEC + the zxv_status_t,
 *     e.g. a claim larger than what is left after settlement is
 *     ZXT_E_EXEC + ZXV_E_FUNDS) gives a zeroed result carrying only the
 *     status. The workspace engine is scratch; it is not reused.
 * E2  After a successful run the engine passes zxv_engine_check.
 * E3  Deterministic: the same treaty and seed give the same bytes.
 */
#ifndef ZXV_TREATY_ZXT_EXEC_H
#define ZXV_TREATY_ZXT_EXEC_H

#include "zxt.h"
#include "../engine/zxv_engine.h"

#define ZXT_E_SIZE (-15)  /* a result outside PIC S9(18)          */
#define ZXT_E_EXEC (-100) /* plus a zxv_status_t: -101 .. -106      */

/* RESULT record (copybooks/ZXVRSLT.cpy). */
#define ZXT_RES_MAGIC "ZXR1"
#define ZXT_RES_HEAD  38u /* MAGIC 4, STATUS 5, WHERE 5, NPARTY 2, NLEG 3, FEES 19 */
#define ZXT_RES_PARTY 57u /* NET 19, BALANCE 19, DIVIDEND 19                       */
#define ZXT_RES_LEG   23u /* FROM 2, TO 2, AMOUNT 19                               */
#define ZXT_RES_LEN   (ZXT_RES_HEAD + ZXT_MAX_PARTIES * ZXT_RES_PARTY + ZXT_MAX_OWES * ZXT_RES_LEG)

typedef struct {
    int32_t status; /* ZXT_OK or a negative code              */
    uint32_t where; /* parse fault offset (zxt_cobol.c), else 0 */
    uint8_t n_party;
    uint8_t n_leg;
    int64_t fees;
    int64_t net[ZXT_MAX_PARTIES];
    int64_t balance[ZXT_MAX_PARTIES];
    int64_t dividend[ZXT_MAX_PARTIES];
    zxt_owe_t leg[ZXT_MAX_OWES];
} zxt_result_t;

/* Run `t` in workspace `ws` (overwritten). Returns r->status. */
int32_t zxt_exec(const zxt_treaty_t *t, zxv_engine_t *ws, const uint8_t seed[32], zxt_result_t *r);

/* Write the result record; `cap` must be >= ZXT_RES_LEN. */
zxt_status_t zxt_emit_result(const zxt_result_t *r, uint8_t *rec, uint32_t cap);

/* The COBOL entry point: CALL 'ZXV_ENGINE' USING ZXV-TREATY ZXV-RESULT.
 * Parses the ZXT_REC_LEN-byte treaty record, runs it, and writes the
 * ZXT_RES_LEN-byte result record (a refused treaty gives its status and
 * fault offset). Returns the status, which COBOL sees as RETURN-CODE.
 * Uses one static engine: not reentrant, one call at a time. */
int ZXV_ENGINE(const char *treaty, char *result);

#endif /* ZXV_TREATY_ZXT_EXEC_H */
