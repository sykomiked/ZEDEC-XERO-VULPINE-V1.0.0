/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* policy_cobol.c — the transfer and result records, and
 * CALL 'ZXV_POLICY' USING ZXV-CONTRACT ZXV-TRANSFER ZXV-POLICY-RESULT.
 * Records: copybooks/ZXVCNTR.cpy, ZXVPTXN.cpy, ZXVPRES.cpy. Freestanding. */
#include "policy_cobol.h"

static bool digits(const uint8_t *s, uint32_t n, uint64_t *v)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        x = x * 10u + (uint64_t) (s[i] - '0');
    }
    *v = x;
    return true;
}

static bool text(const uint8_t *s, uint32_t size, char *dst, uint32_t dsize)
{
    uint32_t n = 0;
    while (n < size && s[n] != ' ') {
        if (s[n] == 0 || n + 1u >= dsize) return false;
        dst[n] = (char) s[n];
        n++;
    }
    for (uint32_t i = n; i < size; i++)
        if (s[i] != ' ') return false;
    return true;
}

static int hexv(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int32_t policy_parse_txn_record(const uint8_t *rec, uint32_t len, policy_txn_t *t)
{
    uint64_t u;
    if (!t) return POLICY_E_TXN;
    for (uint32_t i = 0; i < (uint32_t) sizeof(*t); i++) ((volatile uint8_t *) t)[i] = 0;
    if (!rec || len != POLICY_TXN_REC_LEN) return POLICY_E_TXN;
    const char *m = POLICY_TXN_MAGIC;
    for (uint32_t i = 0; i < 4u; i++)
        if (rec[i] != (uint8_t) m[i]) return POLICY_E_TXN;
    if (!digits(rec + 4, 2u, &u) || u < 1 || u > ZXP_MAX_PARTIES) return POLICY_E_TXN;
    t->from = (uint8_t) (u - 1u);
    if (!digits(rec + 6, 2u, &u) || u < 1 || u > ZXP_MAX_PARTIES) return POLICY_E_TXN;
    t->to = (uint8_t) (u - 1u);
    if (!text(rec + 8, 3u, t->asset, sizeof t->asset)) return POLICY_E_TXN;
    if ((rec[11] != '+' && rec[11] != '-') || !digits(rec + 12, 18u, &u) || rec[11] == '-')
        return POLICY_E_TXN; /* a transfer amount is never negative */
    t->amount = (int64_t) u;
    if (!digits(rec + 30, 8u, &u)) return POLICY_E_TXN;
    t->date = (uint32_t) u;
    if (!text(rec + 38, 6u, t->from_jur, sizeof t->from_jur) ||
        !text(rec + 44, 6u, t->to_jur, sizeof t->to_jur))
        return POLICY_E_TXN;
    for (uint32_t i = 0; i < ZXP_MAX_PARTIES; i++) {
        if (rec[50 + i] == 'Y') t->signers |= (uint16_t) (1u << i);
        else if (rec[50 + i] != 'N') return POLICY_E_TXN;
    }
    for (uint32_t i = 0; i < 32u; i++) {
        int h = hexv(rec[65 + 2 * i]), l = hexv(rec[66 + 2 * i]);
        if (h < 0 || l < 0) return POLICY_E_TXN;
        t->doc[i] = (uint8_t) (h * 16 + l);
    }
    return 0;
}

void policy_emit_result_record(const policy_result_t *r, uint32_t where, uint8_t rec[POLICY_RES_REC_LEN])
{
    static const char *const dec[] = {"ALLOW", "DENY ", "FAULT"};
    static const char val[] = {'N', 'T', 'F', 'B'};
    const char *m = POLICY_RES_MAGIC, *d = dec[r->decision <= POLICY_FAULT ? r->decision : 2];
    int32_t f = r->fault < -9999 ? -9999 : r->fault > 9999 ? 9999 : r->fault;
    uint32_t af = (uint32_t) (f < 0 ? -f : f);
    for (uint32_t i = 0; i < 4u; i++) rec[i] = (uint8_t) m[i];
    for (uint32_t i = 0; i < 5u; i++) rec[4 + i] = (uint8_t) d[i];
    rec[9] = r->escalate ? 'Y' : 'N';
    rec[10] = (uint8_t) val[(unsigned) r->value & 3u];
    for (uint32_t i = 0; i < POLICY_RES_RULES; i++) {
        rec[11 + i] = (r->deny_mask >> i) & 1u ? 'Y' : 'N';
        rec[21 + i] = (r->both_mask >> i) & 1u ? 'Y' : 'N';
    }
    rec[31] = f < 0 ? '-' : '+';
    for (uint32_t i = 4; i > 0; i--) {
        rec[31 + i] = (uint8_t) ('0' + af % 10u);
        af /= 10u;
    }
    if (where > 99999u) where = 99999u;
    for (uint32_t i = 5; i > 0; i--) {
        rec[35 + i] = (uint8_t) ('0' + where % 10u);
        where /= 10u;
    }
}

int ZXV_POLICY(const char *contract, const char *transfer, char *result)
{
    static zxp_contract_t c;
    policy_txn_t t;
    policy_result_t r;
    uint32_t where = 0;
    if (!contract || !transfer || !result) return ZXP_E_ARG;
    zxp_status_t st = zxp_parse_record((const uint8_t *) contract, ZXP_REC_LEN, &c, &where);
    for (uint32_t i = 0; i < (uint32_t) sizeof r; i++) ((volatile uint8_t *) &r)[i] = 0;
    if (st != ZXP_OK) {
        r.decision = POLICY_FAULT;
        r.value = LPRES_STATE_FALSE;
        r.fault = st;
    } else {
        int32_t ts = policy_parse_txn_record((const uint8_t *) transfer, POLICY_TXN_REC_LEN, &t);
        if (ts != 0) {
            r.decision = POLICY_FAULT;
            r.value = LPRES_STATE_FALSE;
            r.fault = ts;
        } else {
            r = policy_check(&c, (const policy_host_t *) 0, (const policy_state_t *) 0, &t);
        }
    }
    policy_emit_result_record(&r, where, (uint8_t *) result);
    return r.decision == POLICY_FAULT ? r.fault : (int) r.decision;
}
