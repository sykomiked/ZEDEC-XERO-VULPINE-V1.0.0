/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cb_net.c — multilateral netting engine: conversion at signed rates,
 * limits, CFM, cut-offs, screening, idempotency, returns, cancellations,
 * conservation and net settlement per central bank. */
#include "cb_net.h"
#include "cb_ccy.h"
#include "cb_util.h"
#include "ed25519_verify.h"
#include "test_cb_rate_vectors.h"
#include "cb_test.h"

static bool verify(const uint8_t *m, size_t n, const uint8_t s[64], const uint8_t pk[32])
{
    return ed25519_verify(m, n, s, pk);
}

static int screen_calls;
static int screen(void *ctx, const cb_screen_req *r)
{
    (void) ctx;
    screen_calls++;
    if (r->cdtr_name && strcmp(r->cdtr_name, "BLOCKED PERSON") == 0) return CB_SCREEN_HIT;
    if (r->cdtr_name && strcmp(r->cdtr_name, "SCREEN DOWN") == 0) return CB_SCREEN_ERROR;
    return CB_SCREEN_CLEAR;
}

static cb_config cfg;
static cb_net eng;
static cb_settle_instr ins[CB_MAX_CCY];
static int A, B, C, D, E; /* participants */

static void build_config(void)
{
    cb_config_init(&cfg, CB_ROLE_CENTRAL_BANK, "Test multilateral settlement operator", "TESTEGCX",
                   "EG");
    cb_config_add_ccy(&cfg, "USD", "USD settlement account (test profile)", "TESTUS33", "US",
                      false);
    cb_config_add_ccy(&cfg, "XOF", "BCEAO (test profile)", "TESTSNDA", "SN", true);
    cb_config_add_ccy(&cfg, "NGN", "Central Bank of Nigeria (test profile)", "TESTNGLA", "NG",
                      true);
    cb_config_add_ccy(&cfg, "KES", "Central Bank of Kenya (test profile)", "TESTKENA", "KE", true);
    cb_config_add_ccy(&cfg, "GHS", "Bank of Ghana (test profile)", "TESTGHAC", "GH", true);
    cb_strlcpy(cfg.settle_unit, "USD", sizeof cfg.settle_unit);
    cb_config_set_window(&cfg, "NGN", 8 * 60, 16 * 60, 17 * 60, 60, CB_WEEKEND_SAT_SUN);
    cb_config_set_window(&cfg, "KES", 8 * 60, 16 * 60, 17 * 60, 180, CB_WEEKEND_SAT_SUN);
    const char *lc[4] = {"XOF", "NGN", "KES", "GHS"};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (i != j) cb_config_add_corridor(&cfg, lc[i], lc[j], 0, 0);
    cb_corridor *k = (cb_corridor *) cb_config_corridor(&cfg, "XOF", "NGN");
    k->per_payment_max = 100000000u;
    k->per_cycle_max = 150000000u;
    cb_config_add_source(&cfg, 1, "Test rate committee", tv_pk_src1, 3600);
    cb_config_add_source(&cfg, 2, "Second test source", tv_pk_src2, 3600);
    cfg.verify = verify;
    cfg.screen = screen;
}

static cb_pay_req mk(const char *id, int s, int r, uint64_t amt)
{
    cb_pay_req q;
    memset(&q, 0, sizeof q);
    snprintf(q.msg_id, sizeof q.msg_id, "%s", id);
    snprintf(q.e2e_id, sizeof q.e2e_id, "E2E-%s", id);
    q.dbtr_agt = (uint16_t) s;
    q.cdtr_agt = (uint16_t) r;
    q.amount = amt;
    snprintf(q.dbtr_name, sizeof q.dbtr_name, "Payer %s", id);
    snprintf(q.cdtr_name, sizeof q.cdtr_name, "Payee %s", id);
    return q;
}

static cb_pay_result pay(const char *id, int s, int r, uint64_t amt, uint64_t now)
{
    cb_pay_req q = mk(id, s, r, amt);
    cb_pay_result res;
    memset(&res, 0, sizeof res);
    cb_net_submit(&eng, &q, now, &res);
    return res;
}

static bool rej(cb_pay_result r, const char *code)
{
    return r.status == CB_PS_REJECTED && strcmp(r.reason, code) == 0;
}

static uint32_t rng = 12345u;
static uint32_t next(void)
{
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static void test_config(void)
{
    cb_config c;
    CHECK(cb_config_init(&c, CB_ROLE_CENTRAL_BANK, "CB", "TESTEGCX", "EG") == CB_OK &&
              !c.rp.vfv_enabled && c.n_ccy == 0,
          "central bank profile: no VFV, no default currency");
    CHECK(cb_config_validate(&c) == CB_E_NO_CCY, "central bank without a currency fails");
    CHECK(cb_config_add_ccy(&c, "VFV", "x", "TESTEGCX", "EG", false) == CB_E_VFV_AS_CCY,
          "VFV refused as a currency profile");
    CHECK(cb_config_add_ccy(&c, "XAU", "x", "TESTEGCX", "EG", false) == CB_E_BAD_CCY,
          "XAU (minor N.A.) refused");
    CHECK(cb_config_add_ccy(&c, "XOF", "x", "TESTNGLA", "NG", true) == CB_E_ISSUER_COUNTRY,
          "legal tender XOF with a Nigerian issuer refused");
    CHECK(cb_config_add_ccy(&c, "EGP", "Central bank (test)", "TESTEGCX", "EG", true) == CB_OK,
          "EGP legal tender in EG accepted");
    CHECK(c.ccy[0].minor == 2 && c.ccy[0].rounding == CB_ROUND_EXACT,
          "minor units from ISO 4217, rounding exact by default");
    CHECK(cb_config_validate(&c) == CB_E_UNIT, "missing settlement unit fails");
    cb_strlcpy(c.settle_unit, "EGP", sizeof c.settle_unit);
    CHECK(cb_config_validate(&c) == CB_E_NO_SCREEN, "screening required without a hook fails");
    c.screen = screen;
    CHECK(cb_config_validate(&c) == CB_OK, "minimal central bank config validates");
    c.rp.vfv_enabled = true;
    CHECK(cb_config_validate(&c) == CB_E_ROLE, "central bank cannot switch VFV on without perm");
    c.rp.vfv_enabled = false;
    CHECK(cb_config_set_window(&c, "EGP", 900, 800, 1000, 0, 0) == CB_E_CALENDAR,
          "cut-off before opening refused");
    CHECK(cb_config_set_window(&c, "EGP", 480, 960, 1020, 120, CB_WEEKEND_FRI_SAT) == CB_OK,
          "Friday/Saturday weekend with UTC+2 accepted");
    CHECK(cb_config_add_holiday(&c, "EGP", 2026, 2, 30) == CB_E_DATE, "invalid date refused");
    CHECK(cb_config_add_holiday(&c, "EGP", 2026, 10, 6) == CB_OK, "holiday slot added");
    /* 2026-10-06 is a Tuesday holiday; 2026-10-09 a Friday (weekend here). */
    uint64_t tue = (uint64_t) cb_days_from_civil(2026, 10, 6) * 86400u + 9u * 3600u;
    uint64_t fri = (uint64_t) cb_days_from_civil(2026, 10, 9) * 86400u + 9u * 3600u;
    uint64_t wed = (uint64_t) cb_days_from_civil(2026, 10, 7) * 86400u;
    CHECK(cb_business_status(&c.ccy[0], tue) == CB_BIZ_HOLIDAY, "holiday closes the window");
    CHECK(cb_business_status(&c.ccy[0], fri) == CB_BIZ_CLOSED_DAY, "Friday weekend closed");
    CHECK(cb_business_status(&c.ccy[0], wed + 5u * 3600u) == CB_BIZ_BEFORE_OPEN &&
              cb_business_status(&c.ccy[0], wed + 7u * 3600u) == CB_BIZ_OPEN &&
              cb_business_status(&c.ccy[0], wed + 14u * 3600u) == CB_BIZ_AFTER_CUTOFF,
          "UTC+2 local window: before open / open / after cut-off");
    c.n_fees = 1;
    c.fees[0].kind = CB_FEE_TIME_ACCRUAL;
    CHECK(cb_config_validate(&c) == CB_E_USURY, "time-accruing fee schedule refused");
    c.fees[0].kind = CB_FEE_FLAT;
    c.fees[0].amount = 100;
    CHECK(cb_config_validate(&c) == CB_OK, "flat per-payment fee accepted");
    c.ccy[0].reserve_bps = 10001;
    CHECK(cb_config_validate(&c) == CB_E_RESERVE, "reserve over 100% refused");

    cb_role_profile rp;
    cb_role_default(CB_ROLE_SELF_BANKING, &rp);
    CHECK(rp.vfv_enabled && (rp.perms & CB_PERM_VFV), "self-banking operator: VFV on by default");
    cb_role_default(CB_ROLE_COMMERCIAL_BANK, &rp);
    CHECK(!rp.vfv_enabled && (rp.perms & CB_PERM_HOLD_PREFUND), "commercial bank: no VFV");
    cb_role_default(CB_ROLE_INDIVIDUAL, &rp);
    CHECK(rp.needs_sponsor && !(rp.perms & CB_PERM_HOLD_PREFUND), "individual needs a sponsor");
    CHECK(cb_role_default((cb_role) 9, &rp) == CB_E_ROLE, "unknown role refused");
}

static void test_rates(void)
{
    build_config();
    CHECK(cb_config_validate(&cfg) == CB_OK, "operator config validates");
    CHECK(cb_net_init(&eng, &cfg) == CB_OK, "engine init");
    int ok = 0;
    for (int i = 0; i < TV_N_RATES; i++)
        if (cb_net_rate_submit(&eng, &tv_rates[i], TV_NOW) == CB_OK) ok++;
    /* Only XOF, NGN, KES, GHS are configured (USD is the unit itself). */
    CHECK(ok == 4, "signed rates accepted for configured currencies only");
    CHECK(cb_net_rate_submit(&eng, &tv_xof_wrongkey, TV_NOW) == CB_E_SIG,
          "rate signed with the wrong key refused");
    CHECK(cb_net_rate_submit(&eng, &tv_xof_stale, TV_NOW) == CB_E_STALE,
          "rate older than the source's staleness limit refused");
    CHECK(cb_net_rate_submit(&eng, &tv_rates[3], TV_NOW) == CB_E_REPLAY, "replayed rate refused");
    cb_rate_rec t = tv_rates[3];
    t.mant = 78;
    CHECK(cb_net_rate_submit(&eng, &t, TV_NOW) == CB_E_SIG, "tampered mantissa breaks signature");
    CHECK(cb_net_rate_submit(&eng, &tv_kes_seq2, TV_NOW) == CB_OK, "newer KES rate accepted");
}

static void test_flow(void)
{
    uint64_t now = TV_NOW;
    A = cb_net_add_participant(&eng, "TESTSNA1", "SN", "XOF", CB_ROLE_COMMERCIAL_BANK, 500000000);
    B = cb_net_add_participant(&eng, "TESTNGB1", "NG", "NGN", CB_ROLE_COMMERCIAL_BANK,
                               5000000000ll);
    C = cb_net_add_participant(&eng, "TESTKEC1", "KE", "KES", CB_ROLE_COMMERCIAL_BANK,
                               1000000000ll);
    D = cb_net_add_participant(&eng, "TESTGHD1", "GH", "GHS", CB_ROLE_SELF_BANKING, 100000000ll);
    E = cb_net_add_participant(&eng, "TESTCIE1", "CI", "XOF", CB_ROLE_COMMERCIAL_BANK, 300000000);
    CHECK(A == 0 && B == 1 && C == 2 && D == 3 && E == 4, "five participants admitted");
    CHECK(cb_net_add_participant(&eng, "TESTSNI1", "SN", "XOF", CB_ROLE_INDIVIDUAL, 0) == CB_E_PERM,
          "an individual cannot be a direct participant");
    CHECK(cb_net_add_participant(&eng, "TESTSNA1", "SN", "XOF", CB_ROLE_COMMERCIAL_BANK, 0) ==
              CB_E_DUP,
          "duplicate BIC refused");

    CHECK(rej(pay("P0", A, B, 1000, now), "TM01"), "no open cycle: rejected TM01");
    CHECK(cb_net_open_cycle(&eng, now) == CB_OK && eng.cycle == 1, "cycle 1 opened");

    /* XOF -> KES exact: 1,000,000 XOF = 1,700.00 USD = 212,500.00 KES at 0.0080. */
    cb_pay_result r = pay("P1", A, C, 1000000, now);
    CHECK(r.status == CB_PS_ACCEPTED && r.unit == 170000 && r.recv == 21250000,
          "XOF->KES converted exactly through USD");
    CHECK(eng.pos[A] == -1000000 && eng.pos[C] == 21250000, "positions booked");
    CHECK(cb_net_check(&eng) == CB_OK, "conservation after one payment");

    /* XOF -> NGN leaves a remainder: exact rounding rejects it. */
    r = pay("P2", A, B, 1000000, now);
    CHECK(rej(r, "AM12"), "inexact conversion rejected under CB_ROUND_EXACT");
    cb_config_ccy(&cfg, "XOF")->rounding = CB_ROUND_FLOOR_REPORTED;
    cb_config_ccy(&cfg, "NGN")->rounding = CB_ROUND_FLOOR_REPORTED;
    uint64_t qr, qu;
    bool exact;
    CHECK(cb_net_quote(&eng, (uint16_t) A, (uint16_t) B, 1000000, now, &qr, &qu, &exact) == CB_OK &&
              qr == 261538461 && !exact,
          "quote XOF->NGN: 2,615,384.61 NGN, inexact");
    r = pay("P3", A, B, 1000000, now);
    const cb_pay_rec *pr = cb_net_find(&eng, "P3");
    CHECK(r.status == CB_PS_ACCEPTED && r.recv == 261538461 && pr && pr->rem2 == 3500 &&
              pr->den2 == 6500 && pr->rem1 == 0,
          "floor-reported: remainder 3500/6500 of a kobo recorded, not charged");

    /* Idempotency. */
    int64_t before = eng.pos[A];
    r = pay("P3", A, B, 1000000, now);
    CHECK(r.status == CB_PS_ACCEPTED && r.duplicate && eng.pos[A] == before,
          "same message id replayed: same outcome, nothing booked");
    r = pay("P3", A, B, 2000000, now);
    CHECK(rej(r, "DUPL") && eng.pos[A] == before && cb_net_find(&eng, "P3")->send == 1000000,
          "same id, different content: DUPL, original untouched");
    r = pay("P2", A, B, 1000000, now);
    CHECK(rej(r, "AM12") && r.duplicate, "a rejected id replays its rejection");

    /* Corridor limits XOF->NGN: per payment 100,000,000, per cycle 150,000,000. */
    CHECK(rej(pay("P4", A, B, 100000001, now), "AM02"), "per-payment corridor limit AM02");
    CHECK(pay("P5", A, B, 100000000, now).status == CB_PS_ACCEPTED, "at the per-payment limit");
    CHECK(rej(pay("P6", A, B, 49000001, now), "AM14"), "per-cycle corridor limit AM14");

    /* Prefund: A has 500,000,000 - 102,000,000 left. */
    CHECK(rej(pay("P7", A, C, 400000000, now), "AM04"), "insufficient prefund AM04");
    cb_config_ccy(&cfg, "XOF")->net_debit_cap = 10000000;
    CHECK(pay("P8", A, C, 400000000, now).status == CB_PS_ACCEPTED,
          "interest-free net debit cap covers the gap");
    cb_config_ccy(&cfg, "XOF")->net_debit_cap = 0;

    /* Domestic XOF (Senegal -> Cote d'Ivoire): no FX, no unit value. */
    r = pay("P9", E, A, 5000000, now);
    CHECK(r.status == CB_PS_ACCEPTED && r.recv == 5000000 && r.unit == 0,
          "same-currency payment needs no rate");

    /* CFM switches. */
    cb_ccy_profile *ng = cb_config_ccy(&cfg, "NGN");
    ng->cfm &= ~CB_CFM_INBOUND;
    CHECK(rej(pay("P10", C, B, 1000, now), "RR04"), "inbound closed: RR04");
    ng->cfm |= CB_CFM_INBOUND | CB_CFM_PURPOSE_REQ;
    CHECK(rej(pay("P11", C, B, 1000, now), "RR04"), "purpose code required: RR04");
    cb_config_ccy(&cfg, "KES")->rounding = CB_ROUND_FLOOR_REPORTED;
    cb_config_ccy(&cfg, "GHS")->rounding = CB_ROUND_FLOOR_REPORTED;
    cb_pay_req q = mk("P12", C, B, 100000);
    snprintf(q.purpose, sizeof q.purpose, "GDDS");
    cb_net_submit(&eng, &q, now, &r);
    CHECK(r.status == CB_PS_ACCEPTED, "with purpose code GDDS accepted");
    ng->cfm &= ~CB_CFM_PURPOSE_REQ;
    ng->cfm |= CB_CFM_HALT;
    CHECK(rej(pay("P13", B, C, 1000, now), "RR04"), "emergency halt: RR04");
    ng->cfm &= ~CB_CFM_HALT;
    ng->cfm_max_outbound = 500;
    CHECK(rej(pay("P14", B, C, 1000, now), "AM02"), "CFM outbound cap per payment: AM02");
    ng->cfm_max_outbound = 0;

    /* Cut-offs: 13:30 UTC is 16:30 in Nairobi (after cut-off) but open in Dakar. */
    CHECK(rej(pay("P15", A, C, 1000, now + 3u * 3600u + 1800u), "TM01"),
          "after the KES cut-off: TM01");
    CHECK(pay("P16", A, E, 1000, now + 3u * 3600u + 1800u).status == CB_PS_ACCEPTED,
          "XOF domestic still open at that time");

    /* Screening. */
    q = mk("P17", A, C, 1000);
    snprintf(q.cdtr_name, sizeof q.cdtr_name, "BLOCKED PERSON");
    cb_net_submit(&eng, &q, now, &r);
    CHECK(rej(r, "RR04"), "screening hit: RR04");
    q = mk("P18", A, C, 1000);
    snprintf(q.cdtr_name, sizeof q.cdtr_name, "SCREEN DOWN");
    cb_net_submit(&eng, &q, now, &r);
    CHECK(rej(r, "MS03"), "screening error fails closed: MS03");

    /* Stale rates. */
    CHECK(rej(pay("P19", A, C, 1000, now + 3601u), "MS03"), "stale rate: MS03");
    CHECK(rej(pay("P20", A, A, 1000, now), "RC01"), "payment to self refused");
    cb_net_set_active(&eng, (uint16_t) D, false);
    CHECK(rej(pay("P21", A, D, 1000, now), "AC06"), "inactive participant AC06");
    cb_net_set_active(&eng, (uint16_t) D, true);
    CHECK(rej(pay("P22", A, C, 0, now), "AM01"), "zero amount AM01");
    q = mk("P23", A, C, 1000);
    q.fi_transfer = true;
    cb_net_submit(&eng, &q, now, &r);
    CHECK(r.status == CB_PS_ACCEPTED, "commercial bank may send pacs.009");
    q = mk("P24", D, C, 1000);
    q.fi_transfer = true;
    cb_net_submit(&eng, &q, now, &r);
    CHECK(rej(r, "AG01"), "self-banking node may not send pacs.009");
    CHECK(cb_net_check(&eng) == CB_OK, "conservation after mixed traffic");

    /* Cancellation in the open cycle. */
    int64_t pa = eng.pos[A], pc = eng.pos[C];
    CHECK(pay("P25", A, C, 2000000, now).status == CB_PS_ACCEPTED, "payment to cancel");
    bool acc;
    char why[5];
    cb_net_cancel(&eng, "P25", "CASE-1", now, &acc, why);
    CHECK(acc && eng.pos[A] == pa && eng.pos[C] == pc &&
              cb_net_find(&eng, "P25")->status == CB_PS_CANCELLED,
          "camt.056 accepted: legs reversed exactly");
    cb_net_cancel(&eng, "P25", "CASE-1", now, &acc, why);
    CHECK(acc, "same case id replayed: same answer");
    cb_net_cancel(&eng, "P25", "CASE-2", now, &acc, why);
    CHECK(!acc && strcmp(why, "ARDT") == 0, "second case on a cancelled payment: ARDT");
    cb_net_cancel(&eng, "NOPE", "CASE-3", now, &acc, why);
    CHECK(!acc && strcmp(why, "NOOR") == 0, "unknown original: NOOR");
    cb_net_cancel(&eng, "P2", "CASE-4", now, &acc, why);
    CHECK(!acc && strcmp(why, "ARJR") == 0, "rejected original: ARJR");

    /* Return in the same cycle. */
    cb_pay_result rr;
    pa = eng.pos[A];
    pc = eng.pos[C];
    int64_t ua = eng.upos[A];
    CHECK(cb_net_return(&eng, "P1", "RTR-1", "AC04", now, &rr) == CB_OK &&
              eng.pos[A] == pa + 1000000 && eng.pos[C] == pc - 21250000 &&
              eng.upos[A] == ua + 170000 && cb_net_find(&eng, "P1")->status == CB_PS_RETURNED,
          "pacs.004 return books the exact reverse at the original amounts");
    CHECK(cb_net_return(&eng, "P1", "RTR-1", "AC04", now, &rr) == CB_OK && rr.duplicate,
          "return id replayed: idempotent");
    CHECK(cb_net_return(&eng, "P5", "RTR-1", "AC04", now, &rr) == CB_E_DUP,
          "return id reused for another payment refused");
    CHECK(cb_net_return(&eng, "P1", "RTR-2", "AC04", now, &rr) == CB_E_CYCLE &&
              strcmp(rr.reason, "ARDT") == 0,
          "a payment is returned at most once");
    CHECK(cb_net_check(&eng) == CB_OK, "conservation after cancel and return");

    /* Close cycle 1. */
    int64_t pre[5];
    int64_t posv[5];
    for (int i = 0; i < 5; i++) {
        pre[i] = eng.part[i].prefund;
        posv[i] = eng.pos[i];
    }
    uint32_t n = 0;
    CHECK(cb_net_close_cycle(&eng, now + 7200u, ins, CB_MAX_CCY, &n) == CB_OK && n == 5,
          "cycle closed: one instruction per configured currency");
    bool lines_ok = true;
    int64_t unit_sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t s = 0;
        for (uint32_t j = 0; j < ins[i].n_lines; j++) s += ins[i].lines[j].net;
        if (s != ins[i].participants_net || ins[i].agent_net != -s) lines_ok = false;
        unit_sum += ins[i].unit_net;
    }
    CHECK(lines_ok, "each instruction: lines sum to the CB net; agent net is its negative");
    CHECK(unit_sum == 0, "settlement-unit values of all currency pools sum to zero");
    CHECK(strcmp(ins[1].ccy, "XOF") == 0 && strcmp(ins[1].issuer_bic, "TESTSNDA") == 0 &&
              ins[1].n_lines == 2,
          "XOF instruction goes to the XOF issuer and covers both XOF banks");
    bool pf = true;
    for (int i = 0; i < 5; i++)
        if (eng.part[i].prefund != pre[i] + posv[i] || eng.pos[i] != 0) pf = false;
    CHECK(pf, "nets applied to prefund; positions reset");
    CHECK(cb_net_find(&eng, "P3")->status == CB_PS_SETTLED &&
              strcmp(cb_net_txsts(CB_PS_SETTLED), "ACSC") == 0,
          "accepted payments settled (ACSC)");
    cb_net_cancel(&eng, "P3", "CASE-5", now, &acc, why);
    CHECK(!acc && strcmp(why, "AGNT") == 0, "cancel after settlement refused: AGNT");
    CHECK(cb_net_close_cycle(&eng, now, ins, CB_MAX_CCY, &n) == CB_E_CYCLE,
          "closing a closed cycle refused");

    /* Cycle 2: return a settled payment. */
    cb_net_open_cycle(&eng, now + 7300u);
    CHECK(cb_net_return(&eng, "P3", "RTR-3", "FOCR", now + 7300u, &rr) == CB_OK &&
              eng.pos[B] == -261538461 && eng.pos[A] == 1000000,
          "return of a settled payment books in the next cycle");
    CHECK(cb_net_check(&eng) == CB_OK, "conservation in cycle 2");
}

static void test_stress(void)
{
    uint64_t now = TV_NOW;
    bool ok = true;
    uint32_t accepted = 0;
    char id[32];
    for (int i = 0; i < 300; i++) {
        int s = (int) (next() % 5), r = (int) (next() % 5);
        uint64_t amt = 1 + next() % 5000000u;
        snprintf(id, sizeof id, "S%d", i);
        cb_pay_result res = pay(id, s, r, amt, now);
        if (res.status == CB_PS_ACCEPTED) accepted++;
        if ((next() % 7) == 0 && res.status == CB_PS_ACCEPTED) {
            bool acc;
            char why[5];
            char cs[32];
            snprintf(cs, sizeof cs, "C%d", i);
            cb_net_cancel(&eng, id, cs, now, &acc, why);
        }
        if (cb_net_check(&eng) != CB_OK) ok = false;
    }
    CHECK(ok && accepted > 150, "300 random payments + cancels: conservation after every step");
    uint32_t n;
    CHECK(cb_net_close_cycle(&eng, now + 7200u, ins, CB_MAX_CCY, &n) == CB_OK,
          "stress cycle closes");
    int64_t unit_sum = 0, neg = 0;
    for (uint32_t i = 0; i < n; i++) unit_sum += ins[i].unit_net;
    for (int i = 0; i < 5; i++)
        if (eng.part[i].prefund < 0) neg++;
    CHECK(unit_sum == 0, "stress: unit nets sum to zero");
    CHECK(neg == 0, "stress: no prefund overdrawn (debit cap 0)");
}

static cb_net cap_eng;
static void test_capacity(void)
{
    CHECK(cb_net_init(&cap_eng, &cfg) == CB_OK, "second engine");
    cb_net_open_cycle(&cap_eng, TV_NOW);
    cb_net_add_participant(&cap_eng, "TESTSNA1", "SN", "XOF", CB_ROLE_COMMERCIAL_BANK, 1000000);
    cb_net_add_participant(&cap_eng, "TESTCIE1", "CI", "XOF", CB_ROLE_COMMERCIAL_BANK, 1000000);
    cb_pay_result r;
    char id[32];
    int full = 0;
    for (uint32_t i = 0; i < CB_NET_MAX_PAY + 1; i++) {
        cb_pay_req q = mk("x", 0, 1, 1);
        snprintf(q.msg_id, sizeof q.msg_id, "CAP%u", i);
        snprintf(id, sizeof id, "CAP%u", i);
        if (cb_net_submit(&cap_eng, &q, TV_NOW, &r) == CB_E_FULL) full++;
    }
    CHECK(full == 1 && cap_eng.n_pay == CB_NET_MAX_PAY,
          "record store full: refused, not overwritten");
    cb_pay_req q = mk("x", 0, 1, 1);
    q.msg_id[0] = 0;
    CHECK(cb_net_submit(&cap_eng, &q, TV_NOW, &r) == CB_E_ID, "empty message id refused");
}

int main(void)
{
    test_config();
    test_rates();
    test_flow();
    test_stress();
    test_capacity();
    CHECK(screen_calls > 0, "screening hook was consulted");
    CB_TEST_DONE("test_cb_net");
}
