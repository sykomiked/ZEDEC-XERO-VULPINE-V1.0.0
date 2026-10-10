/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cb_mx.c — ISO 20022 writers. Writes each message to <dir>/<name>.xml
 * (argv[1], default /tmp) so the verify step can run xmllint --schema on it
 * against the base XSDs in kernel/src/pay/xsd. */
#include "cb_mx.h"
#include "cb_util.h"
#include "cb_test.h"

#define NOW   1791367200u /* 2026-10-07T10:00:00Z */
#define UETR1 "7f1c0a52-3b6e-4d1a-9c2e-5a8b7c6d5e4f"

static char buf[16384];
static char tbuf[16400];
static const char *outdir = "/tmp";

static void save(const char *name, const char *xml)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.xml", outdir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(xml, f);
    fclose(f);
}

static cb_vss_receipt vss(void)
{
    cb_vss_receipt r;
    memset(&r, 0, sizeof r);
    snprintf(r.rcpt_id, sizeof r.rcpt_id, "R-2026-0001");
    snprintf(r.sys_ref, sizeof r.sys_ref, "VSS/1.0");
    r.rail_dr = 555;
    r.rail_cr = 888;
    snprintf(r.ccy, sizeof r.ccy, "XOF");
    r.amt = 1000000;
    r.eqty_coord = 1000000;
    snprintf(r.xwalk_ref, sizeof r.xwalk_ref, "IFRS-XW-v1");
    snprintf(r.anchr_ref, sizeof r.anchr_ref, "merkle:2026-10-07");
    snprintf(r.sgntr, sizeof r.sgntr, "ed25519:dGVzdC1zaWduYXR1cmU=");
    snprintf(r.tx_ref, sizeof r.tx_ref, "P1");
    return r;
}

/* Remove the VSSReceipt element so the rest of the message can be searched. */
static void strip_vss(const char *in, char *out, size_t cap)
{
    const char *a = strstr(in, "<VSSReceipt");
    const char *b = a ? strstr(a, "</VSSReceipt>") : 0;
    if (!a || !b) {
        snprintf(out, cap, "%s", in);
        return;
    }
    size_t n1 = (size_t) (a - in);
    snprintf(out, cap, "%.*s%s", (int) n1, in, b + strlen("</VSSReceipt>"));
}

static bool rails_only_in_splmtry(const char *xml)
{
    static char rest[16384];
    strip_vss(xml, rest, sizeof rest);
    return !strstr(rest, "555") && !strstr(rest, "777") && !strstr(rest, "888") &&
           !strstr(rest, "VFV") && !strstr(rest, "<Ctry>NC") && !strstr(rest, "NCR") &&
           !strstr(rest, "<Ctry>NR") && !strstr(rest, "NRE") && !strstr(rest, "<Ctry>PN") &&
           !strstr(rest, "PNS");
}

/* For every cap below the full length the writer must report truncation,
 * NUL-terminate inside cap and never touch a byte past it. */
typedef int32_t (*writer)(const void *m, char *b, uint32_t cap);
static bool trunc_safe(writer w, const void *m, int32_t full)
{
    for (int32_t cap = 1; cap <= full; cap++) {
        memset(tbuf, 'Z', sizeof tbuf);
        int32_t r = w(m, tbuf, (uint32_t) cap);
        if (r != CB_MX_E_TRUNC) return false;
        if (cb_strnlen(tbuf, (size_t) cap) >= (size_t) cap) return false;
        for (size_t i = (size_t) cap; i < (size_t) cap + 16; i++)
            if (tbuf[i] != 'Z') return false;
    }
    return w(m, tbuf, (uint32_t) full + 1) == full;
}

static int32_t w008(const void *m, char *b, uint32_t c)
{
    return cb_mx_pacs008_write((const cb_mx_pacs008 *) m, b, c);
}
static int32_t w009(const void *m, char *b, uint32_t c)
{
    return cb_mx_pacs009_write((const cb_mx_pacs009 *) m, b, c);
}
static int32_t w002(const void *m, char *b, uint32_t c)
{
    return cb_mx_pacs002_write((const cb_mx_pacs002 *) m, b, c);
}
static int32_t w004(const void *m, char *b, uint32_t c)
{
    return cb_mx_pacs004_write((const cb_mx_pacs004 *) m, b, c);
}
static int32_t w056(const void *m, char *b, uint32_t c)
{
    return cb_mx_camt056_write((const cb_mx_camt056 *) m, b, c);
}
static int32_t w029(const void *m, char *b, uint32_t c)
{
    return cb_mx_camt029_write((const cb_mx_camt029 *) m, b, c);
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    cb_vss_receipt rc = vss();

    cb_mx_pacs008 p8;
    memset(&p8, 0, sizeof p8);
    p8.h = (cb_mx_hdr){"MSG-P1", NOW, "TESTSNA1", "TESTEGCX", "CLRG", "ZXV-MPS-TEST"};
    p8.instr_id = "INSTR-P1";
    p8.e2e_id = "E2E-P1";
    p8.uetr = UETR1;
    p8.amt = (cb_mx_amt){"XOF", 1000000};
    p8.sttlm_dt = NOW;
    p8.chrg_br = "SLEV";
    p8.dbtr = (cb_mx_party){"Awa Diop & Fils <SARL>", "SN", "SN0123456789"};
    p8.dbtr_agt = "TESTSNA1";
    p8.cdtr_agt = "TESTKEC1";
    p8.cdtr = (cb_mx_party){"Wanjiru Traders", "KE", "KE998877"};
    p8.purpose = "GDDS";
    p8.vss = &rc;
    int32_t n8 = cb_mx_pacs008_write(&p8, buf, sizeof buf);
    CHECK(n8 > 0, "pacs.008 written");
    save("pacs008", buf);
    {
        const char *a = strstr(buf, "<VSSReceipt"), *b = a ? strstr(a, "</VSSReceipt>") : 0;
        static char frag[4096];
        if (a && b) snprintf(frag, sizeof frag, "%.*s</VSSReceipt>\n", (int) (b - a), a);
        save("vss_receipt", frag);
        CHECK(a && b, "VSSReceipt element extracted for schema validation");
    }
    CHECK(strstr(buf, "<IntrBkSttlmAmt Ccy=\"XOF\">1000000</IntrBkSttlmAmt>") != 0,
          "pacs.008: XOF has 0 minor units, alphabetic Ccy");
    CHECK(strstr(buf, "Awa Diop &amp; Fils &lt;SARL&gt;") != 0, "pacs.008: names XML-escaped");
    CHECK(strstr(buf, "<RailDr>555</RailDr><RailCr>888</RailCr>") != 0,
          "VSSReceipt carries rails 555/888");
    CHECK(strstr(buf, "<BckgDscl>ASPL</BckgDscl>") != 0, "VSSReceipt BckgDscl defaults to ASPL");
    CHECK(strstr(buf, "<Envlp><VSSReceipt xmlns=\"urn:zedec:vss:receipt:1\">") != 0,
          "VSSReceipt inside SplmtryData/Envlp in its own namespace");
    CHECK(rails_only_in_splmtry(buf), "pacs.008: rail numerics and VFV only inside VSSReceipt");
    CHECK(trunc_safe(w008, &p8, n8), "pacs.008: every truncation is caught without overrun");

    /* Validation refusals. */
    cb_mx_pacs008 bad = p8;
    bad.amt.ccy = "VFV";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_CCY, "Ccy VFV refused");
    bad.amt.ccy = "xof";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_CCY, "lowercase Ccy refused");
    bad = p8;
    bad.dbtr.ctry = "NCR";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_CTRY, "country NCR refused");
    bad.dbtr.ctry = "NRE";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_CTRY, "country NRE refused");
    bad.cdtr.ctry = bad.dbtr.ctry = "PNS";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_CTRY, "country PNS refused");
    bad = p8;
    bad.uetr = "7F1C0A52-3B6E-4D1A-9C2E-5A8B7C6D5E4F";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_UETR, "uppercase UETR refused");
    bad = p8;
    bad.dbtr_agt = "TEST";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_BIC, "malformed BIC refused");
    bad = p8;
    bad.e2e_id = "123456789012345678901234567890123456";
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_ID, "36-char EndToEndId refused");
    cb_vss_receipt rv = rc;
    rv.rail_dr = 999;
    bad = p8;
    bad.vss = &rv;
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_VSS, "receipt rail 999 refused");
    rv = rc;
    rv.bckg = CB_BCKG_EFCT;
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_VSS,
          "EFCT receipt without attestation refused");
    snprintf(rv.attest_ref, sizeof rv.attest_ref, "AUDIT-1");
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) > 0 &&
              strstr(buf, "<BckgDscl>EFCT</BckgDscl>"),
          "EFCT with attestation written");
    bad = p8;
    bad.amt = (cb_mx_amt){"TND", 1234567};
    bad.vss = 0;
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) > 0 && strstr(buf, "Ccy=\"TND\">1234.567<"),
          "TND written with 3 minor units");
    bad.amt = (cb_mx_amt){"XAU", 1};
    CHECK(cb_mx_pacs008_write(&bad, buf, sizeof buf) == CB_MX_E_CCY, "XAU (no minor unit) refused");

    /* pacs.009: net settlement from the XOF pool to the settlement agent. */
    cb_mx_pacs009 p9;
    memset(&p9, 0, sizeof p9);
    p9.h = (cb_mx_hdr){"NET-C1-XOF", NOW + 7200, "TESTSNDA", "TESTEGCX", "INDA", 0};
    p9.instr_id = "NET-C1-XOF";
    p9.e2e_id = "CYCLE-1-XOF";
    p9.amt = (cb_mx_amt){"XOF", 102000000};
    p9.sttlm_dt = NOW;
    p9.dbtr = "TESTSNDA";
    p9.cdtr = "TESTEGCX";
    cb_vss_receipt r9 = rc;
    snprintf(r9.rcpt_id, sizeof r9.rcpt_id, "R-NET-C1-XOF");
    r9.rail_dr = 777;
    r9.rail_cr = 555;
    r9.amt = 102000000;
    p9.vss = &r9;
    int32_t n9 = cb_mx_pacs009_write(&p9, buf, sizeof buf);
    CHECK(n9 > 0 && strstr(buf, "<Dbtr><FinInstnId><BICFI>TESTSNDA</BICFI>"), "pacs.009 written");
    save("pacs009", buf);
    CHECK(trunc_safe(w009, &p9, n9), "pacs.009: truncation safe");

    cb_mx_pacs002 p2;
    memset(&p2, 0, sizeof p2);
    p2.h = (cb_mx_hdr){"STS-P2", NOW, "TESTEGCX", "TESTSNA1", 0, 0};
    p2.orgnl_msg_id = "MSG-P2";
    p2.orgnl_msg_nm_id = "pacs.008.001.08";
    p2.orgnl_e2e_id = "E2E-P2";
    p2.orgnl_uetr = UETR1;
    p2.tx_sts = "RJCT";
    CHECK(cb_mx_pacs002_write(&p2, buf, sizeof buf) == CB_MX_E_CODE,
          "RJCT without a reason refused");
    p2.rsn_cd = "AM04";
    p2.addtl_inf = "Prefunded position insufficient";
    int32_t n2 = cb_mx_pacs002_write(&p2, buf, sizeof buf);
    CHECK(n2 > 0 && strstr(buf, "<TxSts>RJCT</TxSts><StsRsnInf><Rsn><Cd>AM04</Cd>"),
          "pacs.002 RJCT/AM04 written");
    save("pacs002_rjct", buf);
    CHECK(trunc_safe(w002, &p2, n2), "pacs.002: truncation safe");
    p2.tx_sts = "ACSC";
    p2.rsn_cd = 0;
    p2.addtl_inf = 0;
    p2.accptnc_dt = NOW;
    CHECK(cb_mx_pacs002_write(&p2, buf, sizeof buf) > 0 && strstr(buf, "<TxSts>ACSC</TxSts>"),
          "pacs.002 ACSC written");
    save("pacs002_acsc", buf);

    cb_mx_pacs004 p4;
    memset(&p4, 0, sizeof p4);
    p4.h = (cb_mx_hdr){"RTR-MSG-1", NOW, "TESTKEC1", "TESTEGCX", "CLRG", "ZXV-MPS-TEST"};
    p4.rtr_id = "RTR-1";
    p4.orgnl_msg_id = "MSG-P1";
    p4.orgnl_msg_nm_id = "pacs.008.001.08";
    p4.orgnl_e2e_id = "E2E-P1";
    p4.orgnl_uetr = UETR1;
    p4.orgnl_amt = (cb_mx_amt){"XOF", 1000000};
    p4.rtrd_amt = (cb_mx_amt){"KES", 21250000};
    p4.sttlm_dt = NOW;
    p4.chrg_br = "SLEV";
    p4.rsn_cd = "AC04";
    p4.addtl_inf = "Closed account";
    cb_vss_receipt r4 = rc;
    snprintf(r4.rcpt_id, sizeof r4.rcpt_id, "R-RTR-1");
    r4.rail_dr = 888;
    r4.rail_cr = 555;
    p4.vss = &r4;
    int32_t n4 = cb_mx_pacs004_write(&p4, buf, sizeof buf);
    CHECK(n4 > 0 && strstr(buf, "<RtrdIntrBkSttlmAmt Ccy=\"KES\">212500.00</RtrdIntrBkSttlmAmt>"),
          "pacs.004 written with returned KES amount");
    save("pacs004", buf);
    CHECK(rails_only_in_splmtry(buf), "pacs.004: rails only inside VSSReceipt");
    CHECK(trunc_safe(w004, &p4, n4), "pacs.004: truncation safe");

    cb_mx_camt056 c6;
    memset(&c6, 0, sizeof c6);
    c6.assgnmt_id = "ASSGN-1";
    c6.assgnr = "TESTSNA1";
    c6.assgne = "TESTEGCX";
    c6.cre_dt = NOW;
    c6.case_id = "CASE-1";
    c6.case_cretr = "TESTSNA1";
    c6.orgnl_msg_id = "MSG-P25";
    c6.orgnl_msg_nm_id = "pacs.008.001.08";
    c6.orgnl_e2e_id = "E2E-P25";
    c6.orgnl_uetr = UETR1;
    c6.orgnl_amt = (cb_mx_amt){"XOF", 2000000};
    c6.orgnl_sttlm_dt = NOW;
    c6.rsn_cd = "DUPL";
    int32_t n6 = cb_mx_camt056_write(&c6, buf, sizeof buf);
    CHECK(n6 > 0 && strstr(buf, "<CxlRsnInf><Rsn><Cd>DUPL</Cd>"), "camt.056 written");
    save("camt056", buf);
    CHECK(trunc_safe(w056, &c6, n6), "camt.056: truncation safe");

    cb_mx_camt029 c9;
    memset(&c9, 0, sizeof c9);
    c9.assgnmt_id = "ASSGN-2";
    c9.assgnr = "TESTEGCX";
    c9.assgne = "TESTSNA1";
    c9.cre_dt = NOW;
    c9.case_id = "CASE-1";
    c9.case_cretr = "TESTSNA1";
    c9.accepted = true;
    c9.orgnl_msg_id = "MSG-P25";
    c9.orgnl_msg_nm_id = "pacs.008.001.08";
    c9.orgnl_e2e_id = "E2E-P25";
    c9.orgnl_uetr = UETR1;
    int32_t n29 = cb_mx_camt029_write(&c9, buf, sizeof buf);
    CHECK(n29 > 0 && strstr(buf, "<Conf>CNCL</Conf>") && strstr(buf, "<TxCxlSts>ACCR</TxCxlSts>"),
          "camt.029 accepted cancellation written");
    save("camt029_cncl", buf);
    CHECK(trunc_safe(w029, &c9, n29), "camt.029: truncation safe");
    c9.accepted = false;
    CHECK(cb_mx_camt029_write(&c9, buf, sizeof buf) == CB_MX_E_CODE,
          "camt.029 rejection without a reason refused");
    c9.rsn_cd = "AGNT";
    c9.addtl_inf = "Settled in a closed cycle; request a return";
    CHECK(cb_mx_camt029_write(&c9, buf, sizeof buf) > 0 && strstr(buf, "<Conf>RJCR</Conf>"),
          "camt.029 rejected cancellation written");
    save("camt029_rjcr", buf);

    CHECK(cb_mx_vss_write(&rc, buf, sizeof buf) > 0 && strncmp(buf, "<SplmtryData>", 13) == 0,
          "VSSReceipt fragment alone");
    CHECK(cb_mx_pacs008_write(0, buf, sizeof buf) == CB_MX_E_NULL &&
              cb_mx_pacs008_write(&p8, 0, 10) == CB_MX_E_NULL,
          "NULL arguments refused");
    CB_TEST_DONE("test_cb_mx");
}
