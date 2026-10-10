/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cb_vss.c — VSS receipt, conformance invariants C1-C5, levels, 811. */
#include "cb_vss.h"
#include "cb_util.h"
#include "cb_test.h"

/* Test-only check-digit algorithms for the C5 hook (the production ones
 * live in kernel/src/cardnet/cn_check.h; cbank must not depend on them). */
static bool t_luhn(const char *s)
{
    size_t n = strlen(s);
    unsigned t = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned d = (unsigned) (s[n - 1 - i] - '0');
        if (i & 1) d = d * 2 > 9 ? d * 2 - 9 : d * 2;
        t += d;
    }
    return n > 1 && t % 10 == 0;
}

static const uint8_t DM[10][10] = {{0, 3, 1, 7, 5, 9, 8, 6, 4, 2}, {7, 0, 9, 2, 1, 5, 4, 8, 6, 3},
                                   {4, 2, 0, 6, 8, 7, 1, 3, 5, 9}, {1, 7, 5, 0, 9, 8, 3, 4, 2, 6},
                                   {6, 1, 2, 3, 0, 4, 5, 9, 7, 8}, {3, 6, 7, 4, 2, 0, 9, 5, 8, 1},
                                   {5, 8, 6, 9, 7, 2, 0, 1, 3, 4}, {8, 9, 4, 5, 3, 6, 2, 0, 1, 7},
                                   {9, 4, 3, 8, 6, 1, 7, 2, 0, 5}, {2, 5, 8, 1, 4, 3, 6, 7, 9, 0}};
static bool t_damm(const char *s)
{
    unsigned i = 0;
    for (; *s; s++) i = DM[i][*s - '0'];
    return i == 0;
}

static const uint8_t VD[10][10] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {1, 2, 3, 4, 0, 6, 7, 8, 9, 5},
                                   {2, 3, 4, 0, 1, 7, 8, 9, 5, 6}, {3, 4, 0, 1, 2, 8, 9, 5, 6, 7},
                                   {4, 0, 1, 2, 3, 9, 5, 6, 7, 8}, {5, 9, 8, 7, 6, 0, 4, 3, 2, 1},
                                   {6, 5, 9, 8, 7, 1, 0, 4, 3, 2}, {7, 6, 5, 9, 8, 2, 1, 0, 4, 3},
                                   {8, 7, 6, 5, 9, 3, 2, 1, 0, 4}, {9, 8, 7, 6, 5, 4, 3, 2, 1, 0}};
static const uint8_t VP[8][10] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {1, 5, 7, 6, 2, 8, 3, 0, 9, 4},
                                  {5, 8, 0, 3, 7, 9, 6, 1, 4, 2}, {8, 9, 1, 6, 0, 4, 3, 5, 2, 7},
                                  {9, 4, 5, 3, 1, 2, 6, 8, 7, 0}, {4, 2, 8, 6, 5, 7, 3, 9, 0, 1},
                                  {2, 7, 9, 3, 8, 0, 6, 4, 1, 5}, {7, 0, 4, 6, 9, 1, 3, 2, 5, 8}};
static bool t_verhoeff(const char *s)
{
    size_t n = strlen(s);
    unsigned c = 0;
    for (size_t i = 0; i < n; i++) c = VD[c][VP[i % 8][s[n - 1 - i] - '0']];
    return c == 0;
}

static cb_vss_receipt receipt(const char *id, const char *tx, uint64_t amt)
{
    cb_vss_receipt r;
    memset(&r, 0, sizeof r);
    snprintf(r.rcpt_id, sizeof r.rcpt_id, "%s", id);
    snprintf(r.sys_ref, sizeof r.sys_ref, "VSS/1.0");
    r.rail_dr = CB_RAIL_DEBIT;
    r.rail_cr = CB_RAIL_CREDIT;
    snprintf(r.ccy, sizeof r.ccy, "USD");
    r.amt = amt;
    r.eqty_coord = amt;
    snprintf(r.xwalk_ref, sizeof r.xwalk_ref, "USGAAP-XW-v1");
    snprintf(r.sgntr, sizeof r.sgntr, "ed25519:dGVzdA==");
    snprintf(r.tx_ref, sizeof r.tx_ref, "%s", tx);
    return r;
}

static cb_vss_log logbuf;

static void test_receipt(void)
{
    cb_vss_receipt r = receipt("R-2026-0001", "T1", 100000);
    CHECK(r.bckg == CB_BCKG_ASPL, "zeroed receipt defaults to ASPL");
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_OK, "proposal example receipt is complete");
    r.bckg = CB_BCKG_EFCT;
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_BCKG, "EFCT without an attestation refused");
    snprintf(r.attest_ref, sizeof r.attest_ref, "ATTEST-2026-01");
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_OK, "EFCT with an attestation accepted");
    r = receipt("R2", "T1", 1);
    r.rail_cr = 999;
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_RAIL, "rail 999 refused (rails are 555/777/888)");
    r.rail_cr = 811;
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_RAIL, "811 is a procedure designator, not a rail");
    r.rail_cr = CB_RAIL_DEBIT;
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_RAIL, "RailDr == RailCr refused");
    r = receipt("R3", "T1", 1);
    snprintf(r.ccy, sizeof r.ccy, "VFV");
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_CCY, "Amt Ccy VFV refused (ISO 4217 only)");
    r = receipt("R4", "T1", 1);
    r.sgntr[0] = 0;
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_FIELD, "unsigned receipt refused");
    r = receipt("", "T1", 1);
    CHECK(cb_vss_receipt_check(&r) == CB_VSS_E_FIELD, "missing RcptId refused");

    r = receipt("L1", "T1", 5);
    CHECK(cb_vss_log_append(&logbuf, &r) == CB_VSS_OK, "receipt appended to the log");
    CHECK(cb_vss_log_append(&logbuf, &r) == CB_VSS_E_RECEIPT,
          "duplicate RcptId refused (append-only)");
}

static void test_c1_c2(void)
{
    /* Appendix D: T1 balanced, T2 off by one. */
    cb_vss_leg t1l[2] = {{CB_RAIL_DEBIT, CB_SIDE_DR, 100000}, {CB_RAIL_CREDIT, CB_SIDE_CR, 100000}};
    cb_vss_leg t2l[2] = {{CB_RAIL_DEBIT, CB_SIDE_DR, 100000}, {CB_RAIL_CREDIT, CB_SIDE_CR, 99900}};
    cb_vss_leg t3l[3] = {{CB_RAIL_DEBIT, CB_SIDE_DR, 70000},
                         {CB_RAIL_DEBIT, CB_SIDE_DR, 30000},
                         {CB_RAIL_EQUITY, CB_SIDE_CR, 100000}};
    cb_vss_tx tx[3] = {{"T1", "USD", 2, t1l}, {"T2", "USD", 2, t2l}, {"T3", "USD", 3, t3l}};
    uint32_t bad = 99;
    CHECK(cb_vss_c1(&tx[0], 1, &bad) == CB_VSS_OK, "C1: Appendix D T1 balanced passes");
    CHECK(cb_vss_c1(tx, 3, &bad) == CB_VSS_E_BALANCE && bad == 1,
          "C1: Appendix D T2 imbalance of 1 fails at T2");
    cb_vss_tx ok2[2] = {tx[0], tx[2]};
    CHECK(cb_vss_c1(ok2, 2, &bad) == CB_VSS_OK, "C1: split debit legs balance to equity credit");

    cb_vss_receipt rc[2] = {receipt("R1", "T1", 100000), receipt("R3", "T3", 100000)};
    rc[1].rail_cr = CB_RAIL_EQUITY;
    CHECK(cb_vss_c2(ok2, 2, rc, 2, &bad) == CB_VSS_OK, "C2: one complete receipt per transaction");
    CHECK(cb_vss_c2(ok2, 2, rc, 1, &bad) == CB_VSS_E_RECEIPT, "C2: missing receipt fails");
    cb_vss_receipt dup[2] = {rc[0], rc[0]};
    snprintf(dup[1].rcpt_id, sizeof dup[1].rcpt_id, "R1b");
    CHECK(cb_vss_c2(ok2, 2, dup, 2, &bad) == CB_VSS_E_RECEIPT,
          "C2: two receipts for one transaction, none for another, fails");
    rc[1].amt = 99999;
    CHECK(cb_vss_c2(ok2, 2, rc, 2, &bad) == CB_VSS_E_RECEIPT && bad == 1,
          "C2: receipt amount not matching the transaction fails");
    rc[1].amt = 100000;
    rc[1].rail_cr = CB_RAIL_CREDIT;
    CHECK(cb_vss_c2(ok2, 2, rc, 2, &bad) == CB_VSS_E_RECEIPT,
          "C2: receipt naming a rail the transaction does not use fails");
}

static cb_vss_rec srec[16];
static cb_tb_line stb[16];

static cb_tb_line L(const char *code, uint8_t side, uint64_t amt, const char *fund)
{
    cb_tb_line l;
    memset(&l, 0, sizeof l);
    snprintf(l.code, sizeof l.code, "%s", code);
    l.side = side;
    l.amount = amt;
    if (fund) snprintf(l.fund, sizeof l.fund, "%s", fund);
    return l;
}

static void test_c3(void)
{
    uint32_t bad = 99;
    /* US GAAP: $1,000 cash sale (Part III worked example) plus expenses. */
    cb_tb_line us[4] = {L("USG.ASSET", CB_SIDE_DR, 100000, 0), L("USG.REV", CB_SIDE_CR, 100000, 0),
                        L("USG.EXP", CB_SIDE_DR, 40000, 0), L("USG.LIAB", CB_SIDE_CR, 40000, 0)};
    CHECK(cb_vss_c3(CB_FW_US_GAAP, us, 4, srec, stb, &bad) == CB_VSS_OK,
          "C3 US GAAP: trial balance reconstructs exactly");
    CHECK(srec[0].rail == CB_RAIL_DEBIT && srec[1].rail == CB_RAIL_CREDIT,
          "US GAAP: assets to 555, revenue to 777");
    cb_tb_line usi[2] = {L("USG.INT", CB_SIDE_DR, 500, 0), L("USG.LIAB", CB_SIDE_CR, 500, 0)};
    CHECK(cb_vss_c3(CB_FW_US_GAAP, usi, 2, srec, stb, &bad) == CB_VSS_E_USURY && bad == 0,
          "US GAAP: interest line refused (no usury)");

    cb_tb_line ifrs[5] = {
        L("IFRS.ASSET", CB_SIDE_DR, 500000, 0), L("IFRS.ECL", CB_SIDE_CR, 20000, 0),
        L("IFRS.EQUITY", CB_SIDE_CR, 400000, 0), L("IFRS.OCI", CB_SIDE_CR, 30000, 0),
        L("IFRS.LIAB", CB_SIDE_CR, 50000, 0)};
    CHECK(cb_vss_c3(CB_FW_IFRS, ifrs, 5, srec, stb, &bad) == CB_VSS_OK,
          "C3 IFRS: ECL contra and OCI sub-coordinate round-trip");
    CHECK(srec[3].rail == CB_RAIL_EQUITY, "IFRS OCI on the equity rail");
    cb_tb_line ifrsx[2] = {L("IFRS.EIR", CB_SIDE_CR, 1, 0), L("IFRS.ASSET", CB_SIDE_DR, 1, 0)};
    CHECK(cb_vss_c3(CB_FW_IFRS, ifrsx, 2, srec, stb, &bad) == CB_VSS_E_USURY,
          "IFRS: effective-interest line refused");

    cb_tb_line ip[3] = {L("IPSAS.ASSET", CB_SIDE_DR, 900000, "GENFUND"),
                        L("IPSAS.REV.NX", CB_SIDE_CR, 600000, "GENFUND"),
                        L("IPSAS.NETASSET", CB_SIDE_CR, 300000, "CAPFUND")};
    CHECK(cb_vss_c3(CB_FW_IPSAS, ip, 3, srec, stb, &bad) == CB_VSS_OK &&
              strcmp(stb[2].fund, "CAPFUND") == 0,
          "C3 IPSAS: fund tags carried in and back out");
    ip[1].fund[0] = 0;
    CHECK(cb_vss_c3(CB_FW_IPSAS, ip, 3, srec, stb, &bad) == CB_VSS_E_FUND && bad == 1,
          "IPSAS: line without a fund tag refused");

    cb_tb_line aa[5] = {
        L("AAOIFI.MURABAHA", CB_SIDE_DR, 300000, 0), L("AAOIFI.MUSHARAKA", CB_SIDE_DR, 200000, 0),
        L("AAOIFI.URIA", CB_SIDE_CR, 350000, 0), L("AAOIFI.PROFIT", CB_SIDE_CR, 50000, 0),
        L("AAOIFI.EQUITY", CB_SIDE_CR, 100000, 0)};
    CHECK(cb_vss_c3(CB_FW_AAOIFI, aa, 5, srec, stb, &bad) == CB_VSS_OK,
          "C3 AAOIFI: murabahah, musharakah, URIA, profit share round-trip");
    bool eq = srec[1].rail == CB_RAIL_EQUITY && srec[2].rail == CB_RAIL_EQUITY &&
              srec[3].rail == CB_RAIL_EQUITY;
    CHECK(eq, "AAOIFI: profit-sharing items on the equity rail 888");
    cb_tb_line aai[2] = {L("AAOIFI.INT", CB_SIDE_CR, 10, 0), L("AAOIFI.ASSET", CB_SIDE_DR, 10, 0)};
    CHECK(cb_vss_c3(CB_FW_AAOIFI, aai, 2, srec, stb, &bad) == CB_VSS_E_USURY,
          "AAOIFI: interest construct refused");
    bool none_on_credit = true;
    for (uint32_t i = 0; i < cb_xw_count; i++)
        if (cb_xw_table[i].fw == CB_FW_AAOIFI && (cb_xw_table[i].flags & CB_XW_PROFIT) &&
            cb_xw_table[i].rail != CB_RAIL_EQUITY)
            none_on_credit = false;
    CHECK(none_on_credit, "AAOIFI table: no profit-share item off the equity rail");

    cb_tb_line wrongfw[2] = {L("IFRS.ASSET", CB_SIDE_DR, 1, 0), L("IFRS.LIAB", CB_SIDE_CR, 1, 0)};
    CHECK(cb_vss_c3(CB_FW_US_GAAP, wrongfw, 2, srec, stb, &bad) == CB_VSS_E_XWALK,
          "code from another framework refused");
    cb_tb_line unbal[2] = {L("USG.ASSET", CB_SIDE_DR, 2, 0), L("USG.LIAB", CB_SIDE_CR, 1, 0)};
    CHECK(cb_vss_c3(CB_FW_US_GAAP, unbal, 2, srec, stb, &bad) == CB_VSS_E_BALANCE,
          "unbalanced statutory trial balance fails C3");
    /* Tampering with a VSS record breaks the reverse mapping. */
    cb_vss_map_in(CB_FW_US_GAAP, us, 4, srec, 16, &bad);
    srec[1].rail = CB_RAIL_EQUITY;
    CHECK(cb_vss_map_out(CB_FW_US_GAAP, srec, 4, stb, 16) == CB_VSS_E_XWALK,
          "record with a rail its crosswalk line does not allow is refused on the way out");
    CHECK(strcmp(cb_vss_xwalk_ref(CB_FW_AAOIFI), "AAOIFI-XW-v1") == 0, "XwalkRef names");
}

static void test_c4(void)
{
    cb_vss_backing b[3];
    memset(b, 0, sizeof b);
    snprintf(b[0].kind, sizeof b[0].kind, "gold-vault");
    snprintf(b[0].ccy, 4, "USD");
    b[0].amount = 500000;
    b[0].label = CB_BCKG_EFCT;
    snprintf(b[0].attest_ref, sizeof b[0].attest_ref, "AUDITOR-2026-Q3");
    snprintf(b[1].kind, sizeof b[1].kind, "natural-capital");
    snprintf(b[1].ccy, 4, "USD");
    b[1].amount = 9000000;
    b[1].label = CB_BCKG_ASPL;
    snprintf(b[2].kind, sizeof b[2].kind, "social-capital");
    snprintf(b[2].ccy, 4, "USD");
    b[2].amount = 100;
    b[2].label = CB_BCKG_ASPL;
    cb_vss_backing_totals t;
    CHECK(cb_vss_backing_sum(b, 3, "USD", &t) == CB_VSS_OK && t.effective == 500000 &&
              t.aspirational == 9000100,
          "C4: effective and aspirational summed apart");
    cb_vss_backing_totals pres = {500000, 9000100};
    uint32_t bad;
    CHECK(cb_vss_c4(b, 3, "USD", &pres, &bad) == CB_VSS_OK, "C4: honest presentation passes");
    cb_vss_backing_totals inflated = {9500100, 0};
    CHECK(cb_vss_c4(b, 3, "USD", &inflated, &bad) == CB_VSS_E_MISMATCH,
          "C4: aspirational presented as effective fails");
    b[1].label = CB_BCKG_EFCT;
    CHECK(cb_vss_c4(b, 3, "USD", &pres, &bad) == CB_VSS_E_BCKG && bad == 1,
          "C4: claim labelled effective without attestation is a critical failure");
}

static void test_c5_levels(void)
{
    cb_vss_c5_hooks h = {t_luhn, t_damm, t_verhoeff};
    cb_vss_ident good[3] = {{"8800000000000005", CB_RAIL_DEBIT},
                            {"8820000000000003", CB_RAIL_CREDIT},
                            {"8840000000000011", CB_RAIL_EQUITY}};
    uint32_t bad = 99;
    CHECK(cb_vss_c5(&h, good, 3, &bad) == CB_VSS_OK,
          "C5: each identifier passes its rail and fails the others");
    /* The proposal's Appendix D Verhoeff vector also passes Luhn, so it
     * violates the cross-rail property the proposal states for it. */
    cb_vss_ident appd = {"8840000000000007", CB_RAIL_EQUITY};
    CHECK(t_verhoeff(appd.id) && t_luhn(appd.id), "Appendix D Verhoeff vector also passes Luhn");
    CHECK(cb_vss_c5(&h, &appd, 1, &bad) == CB_VSS_E_ID, "C5 catches the cross-rail collision");
    cb_vss_ident misrail = {"8800000000000005", CB_RAIL_CREDIT};
    CHECK(cb_vss_c5(&h, &misrail, 1, &bad) == CB_VSS_E_ID, "C5: Luhn id on the credit rail fails");
    cb_vss_c5_hooks none = {0, 0, 0};
    CHECK(cb_vss_c5(&none, good, 3, &bad) == CB_VSS_E_NOHOOK, "C5 without hooks cannot run");

    cb_vss_results r;
    memset(&r, 0, sizeof r);
    r.run_at = 1791367200u;
    r.c1 = r.c2 = r.c5 = CB_VSS_PASS;
    CHECK(cb_vss_level(&r) == CB_VSS_BRONZE, "C1 C2 C5: Bronze");
    r.c3 = CB_VSS_PASS;
    CHECK(cb_vss_level(&r) == CB_VSS_SILVER, "C1-C3 C5: Silver");
    r.c4 = CB_VSS_PASS;
    CHECK(cb_vss_level(&r) == CB_VSS_SILVER, "C4 self-tested only: still Silver");
    snprintf(r.c4_attestor, sizeof r.c4_attestor, "Independent attestor ref 42");
    CHECK(cb_vss_level(&r) == CB_VSS_GOLD, "C1-C5 with independent C4 attestation: Gold");
    r.c4 = CB_VSS_FAIL;
    CHECK(cb_vss_level(&r) == CB_VSS_NONE, "a failed C4 voids every level");
    r.c4 = CB_VSS_PASS;
    r.c5 = CB_VSS_NOT_RUN;
    CHECK(cb_vss_level(&r) == CB_VSS_NONE, "C5 not run: no level");
    r.c5 = CB_VSS_PASS;
    r.run_at = 0;
    CHECK(cb_vss_level(&r) == CB_VSS_NONE, "a level claim needs the date of the run");
    CHECK(strcmp(cb_vss_level_name(CB_VSS_GOLD), "Gold") == 0, "level names");
}

static void test_811(void)
{
    cb_vss_resolution r;
    memset(&r, 0, sizeof r);
    r.designator = CB_VSS_RESOLUTION_CLASS;
    snprintf(r.claim_id, sizeof r.claim_id, "CLAIM-77");
    r.basis = CB_RES_DISSOLVED_ENTITY;
    snprintf(r.authority_ref, sizeof r.authority_ref, "Registrar of Companies order 2026/18");
    snprintf(r.notice_ref, sizeof r.notice_ref, "Gazette notice 2026-07-01");
    snprintf(r.adjudication_ref, sizeof r.adjudication_ref, "Court ref 2026/HC/991");
    r.receipt = receipt("R-811-1", "CLAIM-77", 123456);
    r.receipt.rail_dr = CB_RAIL_CREDIT;
    r.receipt.rail_cr = CB_RAIL_DEBIT;
    CHECK(cb_vss_resolution_check(&r) == CB_VSS_OK,
          "811: authorised discharge of a dissolved entity's claim with receipt");
    r.debtor_living_person = true;
    CHECK(cb_vss_resolution_check(&r) == CB_VSS_E_LIVING,
          "811 never discharges a living person's debt");
    r.debtor_living_person = false;
    r.adjudication_ref[0] = 0;
    CHECK(cb_vss_resolution_check(&r) == CB_VSS_E_AUTH, "811 without an adjudication refused");
    snprintf(r.adjudication_ref, sizeof r.adjudication_ref, "Court ref 2026/HC/991");
    r.designator = 777;
    CHECK(cb_vss_resolution_check(&r) == CB_VSS_E_RAIL, "the resolution class is 811, not 777");
    r.designator = CB_VSS_RESOLUTION_CLASS;
    snprintf(r.receipt.tx_ref, sizeof r.receipt.tx_ref, "OTHER");
    CHECK(cb_vss_resolution_check(&r) == CB_VSS_E_RECEIPT, "receipt must reference the claim");
}

int main(void)
{
    test_receipt();
    test_c1_c2();
    test_c3();
    test_c4();
    test_c5_levels();
    test_811();
    CB_TEST_DONE("test_cb_vss");
}
