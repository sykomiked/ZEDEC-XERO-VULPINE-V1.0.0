/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_mx.c — see cb_mx.h. Element order follows the base ISO 20022 XSDs. */
#include "cb_mx.h"
#include "cb_ccy.h"
#include "cb_util.h"

#define NS_PFX "urn:iso:std:iso:20022:tech:xsd:"
#define VSS_NS "urn:zedec:vss:receipt:1"

/* ===== validation helpers ===== */

static bool txt(const char *s, size_t max, bool required)
{
    if (!s) return !required;
    size_t n = cb_strnlen(s, max + 1);
    if (n > max) return false;
    return required ? n > 0 : true;
}

static bool opt_bic(const char *s)
{
    return !s || cb_bic_valid(s);
}

static bool uetr_ok(const char *s)
{
    if (!s) return true;
    static const char pat[] = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    if (cb_strnlen(s, 37) != 36) return false;
    for (uint32_t i = 0; i < 36; i++) {
        char c = s[i], p = pat[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (p == '-' || p == '4') {
            if (c != p) return false;
        } else if (p == 'y') {
            if (c != '8' && c != '9' && c != 'a' && c != 'b') return false;
        } else if (!hex) {
            return false;
        }
    }
    return true;
}

static bool code4(const char *s, bool required)
{
    if (!s) return !required;
    size_t n = cb_strnlen(s, 5);
    if (n == 0) return !required;
    if (n > 4) return false;
    for (size_t i = 0; i < n; i++)
        if (!cb_is_upper(s[i]) && !cb_is_digit(s[i])) return false;
    return true;
}

static int amt_ok(const cb_mx_amt *a, uint8_t *minor)
{
    const cb_ccy_t *c = cb_ccy_by_alpha(a->ccy);
    if (!cb_ccy_payable(c)) return CB_MX_E_CCY;
    /* totalDigits 18 */
    if (a->units >= 1000000000000000000ull) return CB_MX_E_AMT;
    *minor = c->minor;
    return 0;
}

static bool ctry_ok(const char *c)
{
    return !c || !c[0] || cb_country_by_a2(c) != 0;
}

static bool sttlm_ok(const char *m)
{
    return m && (cb_streq(m, "CLRG") || cb_streq(m, "INDA") || cb_streq(m, "INGA") ||
                 cb_streq(m, "COVE"));
}

static bool chrg_ok(const char *c, bool required)
{
    if (!c) return !required;
    return cb_streq(c, "SLEV") || cb_streq(c, "SHAR") || cb_streq(c, "DEBT") || cb_streq(c, "CRED");
}

/* ===== writing helpers ===== */

static void open_tag(cb_w *w, const char *t)
{
    cb_w_c(w, '<');
    cb_w_s(w, t);
    cb_w_c(w, '>');
}

static void close_tag(cb_w *w, const char *t)
{
    cb_w_s(w, "</");
    cb_w_s(w, t);
    cb_w_c(w, '>');
}

static void el(cb_w *w, const char *t, const char *v)
{
    if (!v || !v[0]) return;
    open_tag(w, t);
    cb_w_esc(w, v);
    close_tag(w, t);
}

static void el_dt(cb_w *w, const char *t, uint64_t v)
{
    open_tag(w, t);
    cb_w_datetime(w, v);
    close_tag(w, t);
}

static void el_date(cb_w *w, const char *t, uint64_t v)
{
    if (!v) return;
    open_tag(w, t);
    cb_w_date(w, v);
    close_tag(w, t);
}

static void el_amt(cb_w *w, const char *t, const cb_mx_amt *a, uint8_t minor)
{
    cb_w_c(w, '<');
    cb_w_s(w, t);
    cb_w_s(w, " Ccy=\"");
    cb_w_s(w, a->ccy);
    cb_w_s(w, "\">");
    cb_w_amount(w, a->units, minor);
    close_tag(w, t);
}

static void el_fi(cb_w *w, const char *t, const char *bic)
{
    if (!bic) return;
    open_tag(w, t);
    cb_w_s(w, "<FinInstnId><BICFI>");
    cb_w_s(w, bic);
    cb_w_s(w, "</BICFI></FinInstnId>");
    close_tag(w, t);
}

static void el_party(cb_w *w, const char *t, const cb_mx_party *p)
{
    open_tag(w, t);
    el(w, "Nm", p->name);
    if (p->ctry && p->ctry[0]) {
        cb_w_s(w, "<PstlAdr><Ctry>");
        cb_w_s(w, p->ctry);
        cb_w_s(w, "</Ctry></PstlAdr>");
    }
    close_tag(w, t);
}

static void el_acct(cb_w *w, const char *t, const char *acct)
{
    if (!acct || !acct[0]) return;
    open_tag(w, t);
    cb_w_s(w, "<Id><Othr><Id>");
    cb_w_esc(w, acct);
    cb_w_s(w, "</Id></Othr></Id>");
    close_tag(w, t);
}

static void doc_open(cb_w *w, const char *ns, const char *root)
{
    cb_w_s(w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Document xmlns=\"" NS_PFX);
    cb_w_s(w, ns);
    cb_w_s(w, "\">");
    open_tag(w, root);
}

static int32_t doc_close(cb_w *w, const char *root)
{
    close_tag(w, root);
    cb_w_s(w, "</Document>\n");
    return cb_w_finish(w) < 0 ? CB_MX_E_TRUNC : (int32_t) w->len;
}

static int hdr_ok(const cb_mx_hdr *h, bool need_sttlm)
{
    if (!txt(h->msg_id, 35, true)) return CB_MX_E_ID;
    if (!opt_bic(h->instg_agt) || !opt_bic(h->instd_agt)) return CB_MX_E_BIC;
    if (need_sttlm && !sttlm_ok(h->sttlm_mtd)) return CB_MX_E_CODE;
    if (!txt(h->clr_sys, 35, false)) return CB_MX_E_TEXT;
    return 0;
}

static void sttlm_inf(cb_w *w, const cb_mx_hdr *h)
{
    cb_w_s(w, "<SttlmInf><SttlmMtd>");
    cb_w_s(w, h->sttlm_mtd);
    cb_w_s(w, "</SttlmMtd>");
    if (h->clr_sys && h->clr_sys[0]) {
        cb_w_s(w, "<ClrSys><Prtry>");
        cb_w_esc(w, h->clr_sys);
        cb_w_s(w, "</Prtry></ClrSys>");
    }
    cb_w_s(w, "</SttlmInf>");
}

static void hdr_agents(cb_w *w, const cb_mx_hdr *h)
{
    el_fi(w, "InstgAgt", h->instg_agt);
    el_fi(w, "InstdAgt", h->instd_agt);
}

/* ===== VSSReceipt ===== */

static void vss_body(cb_w *w, const cb_vss_receipt *r)
{
    uint8_t minor = cb_ccy_by_alpha(r->ccy)->minor;
    cb_w_s(w, "<SplmtryData><PlcAndNm>VSSReceipt (VINO Sovereign Standard, unregistered "
              "private-deployment extension)</PlcAndNm><Envlp><VSSReceipt xmlns=\"" VSS_NS "\">");
    el(w, "RcptId", r->rcpt_id);
    el(w, "SysRef", r->sys_ref);
    open_tag(w, "RailDr");
    cb_w_u64(w, r->rail_dr);
    close_tag(w, "RailDr");
    open_tag(w, "RailCr");
    cb_w_u64(w, r->rail_cr);
    close_tag(w, "RailCr");
    cb_mx_amt a = {r->ccy, r->amt};
    el_amt(w, "Amt", &a, minor);
    open_tag(w, "EqtyCoord");
    cb_w_amount(w, r->eqty_coord, minor);
    close_tag(w, "EqtyCoord");
    el(w, "BckgDscl", r->bckg == CB_BCKG_EFCT ? "EFCT" : "ASPL");
    el(w, "XwalkRef", r->xwalk_ref);
    el(w, "AnchrRef", r->anchr_ref);
    el(w, "Sgntr", r->sgntr);
    cb_w_s(w, "</VSSReceipt></Envlp></SplmtryData>");
}

static int vss_ok(const cb_vss_receipt *r)
{
    if (!r) return 0;
    return cb_vss_receipt_check(r) == CB_VSS_OK ? 0 : CB_MX_E_VSS;
}

int32_t cb_mx_vss_write(const cb_vss_receipt *r, char *buf, uint32_t cap)
{
    if (!r || !buf || !cap) return CB_MX_E_NULL;
    if (vss_ok(r)) return CB_MX_E_VSS;
    cb_w w;
    cb_w_init(&w, buf, cap);
    vss_body(&w, r);
    return cb_w_finish(&w) < 0 ? CB_MX_E_TRUNC : (int32_t) w.len;
}

/* ===== pacs.008 ===== */

int32_t cb_mx_pacs008_write(const cb_mx_pacs008 *m, char *buf, uint32_t cap)
{
    if (!m || !buf || !cap) return CB_MX_E_NULL;
    uint8_t minor;
    int e = hdr_ok(&m->h, true);
    if (e) return e;
    if (!txt(m->instr_id, 35, false) || !txt(m->e2e_id, 35, true)) return CB_MX_E_ID;
    if (!uetr_ok(m->uetr)) return CB_MX_E_UETR;
    if ((e = amt_ok(&m->amt, &minor))) return e;
    if (!chrg_ok(m->chrg_br, true)) return CB_MX_E_CODE;
    if (!cb_bic_valid(m->dbtr_agt) || !cb_bic_valid(m->cdtr_agt)) return CB_MX_E_BIC;
    if (!ctry_ok(m->dbtr.ctry) || !ctry_ok(m->cdtr.ctry)) return CB_MX_E_CTRY;
    if (!txt(m->dbtr.name, 140, false) || !txt(m->cdtr.name, 140, false) ||
        !txt(m->dbtr.acct, 34, false) || !txt(m->cdtr.acct, 34, false))
        return CB_MX_E_TEXT;
    if (!code4(m->purpose, false)) return CB_MX_E_CODE;
    if (vss_ok(m->vss)) return CB_MX_E_VSS;

    cb_w w;
    cb_w_init(&w, buf, cap);
    doc_open(&w, "pacs.008.001.08", "FIToFICstmrCdtTrf");
    cb_w_s(&w, "<GrpHdr>");
    el(&w, "MsgId", m->h.msg_id);
    el_dt(&w, "CreDtTm", m->h.cre_dt);
    cb_w_s(&w, "<NbOfTxs>1</NbOfTxs>");
    sttlm_inf(&w, &m->h);
    hdr_agents(&w, &m->h);
    cb_w_s(&w, "</GrpHdr><CdtTrfTxInf><PmtId>");
    el(&w, "InstrId", m->instr_id);
    el(&w, "EndToEndId", m->e2e_id);
    el(&w, "UETR", m->uetr);
    cb_w_s(&w, "</PmtId>");
    el_amt(&w, "IntrBkSttlmAmt", &m->amt, minor);
    el_date(&w, "IntrBkSttlmDt", m->sttlm_dt);
    el(&w, "ChrgBr", m->chrg_br);
    el_party(&w, "Dbtr", &m->dbtr);
    el_acct(&w, "DbtrAcct", m->dbtr.acct);
    el_fi(&w, "DbtrAgt", m->dbtr_agt);
    el_fi(&w, "CdtrAgt", m->cdtr_agt);
    el_party(&w, "Cdtr", &m->cdtr);
    el_acct(&w, "CdtrAcct", m->cdtr.acct);
    if (m->purpose && m->purpose[0]) {
        cb_w_s(&w, "<Purp><Cd>");
        cb_w_s(&w, m->purpose);
        cb_w_s(&w, "</Cd></Purp>");
    }
    if (m->vss) vss_body(&w, m->vss);
    cb_w_s(&w, "</CdtTrfTxInf>");
    return doc_close(&w, "FIToFICstmrCdtTrf");
}

/* ===== pacs.009 ===== */

int32_t cb_mx_pacs009_write(const cb_mx_pacs009 *m, char *buf, uint32_t cap)
{
    if (!m || !buf || !cap) return CB_MX_E_NULL;
    uint8_t minor;
    int e = hdr_ok(&m->h, true);
    if (e) return e;
    if (!txt(m->instr_id, 35, false) || !txt(m->e2e_id, 35, true)) return CB_MX_E_ID;
    if (!uetr_ok(m->uetr)) return CB_MX_E_UETR;
    if ((e = amt_ok(&m->amt, &minor))) return e;
    if (!cb_bic_valid(m->dbtr) || !cb_bic_valid(m->cdtr)) return CB_MX_E_BIC;
    if (vss_ok(m->vss)) return CB_MX_E_VSS;

    cb_w w;
    cb_w_init(&w, buf, cap);
    doc_open(&w, "pacs.009.001.08", "FICdtTrf");
    cb_w_s(&w, "<GrpHdr>");
    el(&w, "MsgId", m->h.msg_id);
    el_dt(&w, "CreDtTm", m->h.cre_dt);
    cb_w_s(&w, "<NbOfTxs>1</NbOfTxs>");
    sttlm_inf(&w, &m->h);
    hdr_agents(&w, &m->h);
    cb_w_s(&w, "</GrpHdr><CdtTrfTxInf><PmtId>");
    el(&w, "InstrId", m->instr_id);
    el(&w, "EndToEndId", m->e2e_id);
    el(&w, "UETR", m->uetr);
    cb_w_s(&w, "</PmtId>");
    el_amt(&w, "IntrBkSttlmAmt", &m->amt, minor);
    el_date(&w, "IntrBkSttlmDt", m->sttlm_dt);
    el_fi(&w, "Dbtr", m->dbtr);
    el_fi(&w, "Cdtr", m->cdtr);
    if (m->vss) vss_body(&w, m->vss);
    cb_w_s(&w, "</CdtTrfTxInf>");
    return doc_close(&w, "FICdtTrf");
}

/* ===== pacs.002 ===== */

int32_t cb_mx_pacs002_write(const cb_mx_pacs002 *m, char *buf, uint32_t cap)
{
    if (!m || !buf || !cap) return CB_MX_E_NULL;
    int e = hdr_ok(&m->h, false);
    if (e) return e;
    if (!txt(m->orgnl_msg_id, 35, true) || !txt(m->orgnl_msg_nm_id, 35, true) ||
        !txt(m->orgnl_instr_id, 35, false) || !txt(m->orgnl_e2e_id, 35, false))
        return CB_MX_E_ID;
    if (!uetr_ok(m->orgnl_uetr)) return CB_MX_E_UETR;
    if (!code4(m->tx_sts, true) || !code4(m->rsn_cd, false)) return CB_MX_E_CODE;
    if (cb_streq(m->tx_sts, "RJCT") && (!m->rsn_cd || !m->rsn_cd[0])) return CB_MX_E_CODE;
    if (!txt(m->addtl_inf, 105, false)) return CB_MX_E_TEXT;

    cb_w w;
    cb_w_init(&w, buf, cap);
    doc_open(&w, "pacs.002.001.10", "FIToFIPmtStsRpt");
    cb_w_s(&w, "<GrpHdr>");
    el(&w, "MsgId", m->h.msg_id);
    el_dt(&w, "CreDtTm", m->h.cre_dt);
    hdr_agents(&w, &m->h);
    cb_w_s(&w, "</GrpHdr><OrgnlGrpInfAndSts>");
    el(&w, "OrgnlMsgId", m->orgnl_msg_id);
    el(&w, "OrgnlMsgNmId", m->orgnl_msg_nm_id);
    cb_w_s(&w, "</OrgnlGrpInfAndSts><TxInfAndSts>");
    el(&w, "OrgnlInstrId", m->orgnl_instr_id);
    el(&w, "OrgnlEndToEndId", m->orgnl_e2e_id);
    el(&w, "OrgnlUETR", m->orgnl_uetr);
    el(&w, "TxSts", m->tx_sts);
    if ((m->rsn_cd && m->rsn_cd[0]) || (m->addtl_inf && m->addtl_inf[0])) {
        cb_w_s(&w, "<StsRsnInf>");
        if (m->rsn_cd && m->rsn_cd[0]) {
            cb_w_s(&w, "<Rsn><Cd>");
            cb_w_s(&w, m->rsn_cd);
            cb_w_s(&w, "</Cd></Rsn>");
        }
        el(&w, "AddtlInf", m->addtl_inf);
        cb_w_s(&w, "</StsRsnInf>");
    }
    if (m->accptnc_dt) el_dt(&w, "AccptncDtTm", m->accptnc_dt);
    cb_w_s(&w, "</TxInfAndSts>");
    return doc_close(&w, "FIToFIPmtStsRpt");
}

/* ===== pacs.004 ===== */

int32_t cb_mx_pacs004_write(const cb_mx_pacs004 *m, char *buf, uint32_t cap)
{
    if (!m || !buf || !cap) return CB_MX_E_NULL;
    uint8_t mo, mr;
    int e = hdr_ok(&m->h, true);
    if (e) return e;
    if (!txt(m->rtr_id, 35, true) || !txt(m->orgnl_msg_id, 35, true) ||
        !txt(m->orgnl_msg_nm_id, 35, true) || !txt(m->orgnl_e2e_id, 35, false))
        return CB_MX_E_ID;
    if (!uetr_ok(m->orgnl_uetr)) return CB_MX_E_UETR;
    if ((e = amt_ok(&m->orgnl_amt, &mo)) || (e = amt_ok(&m->rtrd_amt, &mr))) return e;
    if (!chrg_ok(m->chrg_br, false)) return CB_MX_E_CODE;
    if (!code4(m->rsn_cd, true)) return CB_MX_E_CODE;
    if (!txt(m->addtl_inf, 105, false)) return CB_MX_E_TEXT;
    if (vss_ok(m->vss)) return CB_MX_E_VSS;

    cb_w w;
    cb_w_init(&w, buf, cap);
    doc_open(&w, "pacs.004.001.09", "PmtRtr");
    cb_w_s(&w, "<GrpHdr>");
    el(&w, "MsgId", m->h.msg_id);
    el_dt(&w, "CreDtTm", m->h.cre_dt);
    cb_w_s(&w, "<NbOfTxs>1</NbOfTxs>");
    sttlm_inf(&w, &m->h);
    hdr_agents(&w, &m->h);
    cb_w_s(&w, "</GrpHdr><TxInf>");
    el(&w, "RtrId", m->rtr_id);
    cb_w_s(&w, "<OrgnlGrpInf>");
    el(&w, "OrgnlMsgId", m->orgnl_msg_id);
    el(&w, "OrgnlMsgNmId", m->orgnl_msg_nm_id);
    cb_w_s(&w, "</OrgnlGrpInf>");
    el(&w, "OrgnlEndToEndId", m->orgnl_e2e_id);
    el(&w, "OrgnlUETR", m->orgnl_uetr);
    el_amt(&w, "OrgnlIntrBkSttlmAmt", &m->orgnl_amt, mo);
    el_amt(&w, "RtrdIntrBkSttlmAmt", &m->rtrd_amt, mr);
    el_date(&w, "IntrBkSttlmDt", m->sttlm_dt);
    el(&w, "ChrgBr", m->chrg_br);
    cb_w_s(&w, "<RtrRsnInf><Rsn><Cd>");
    cb_w_s(&w, m->rsn_cd);
    cb_w_s(&w, "</Cd></Rsn>");
    el(&w, "AddtlInf", m->addtl_inf);
    cb_w_s(&w, "</RtrRsnInf>");
    if (m->vss) vss_body(&w, m->vss);
    cb_w_s(&w, "</TxInf>");
    return doc_close(&w, "PmtRtr");
}

/* ===== camt.056 / camt.029 ===== */

static void party40_agt(cb_w *w, const char *t, const char *bic)
{
    open_tag(w, t);
    el_fi(w, "Agt", bic);
    close_tag(w, t);
}

static void assgnmt(cb_w *w, const char *id, const char *assgnr, const char *assgne, uint64_t dt)
{
    cb_w_s(w, "<Assgnmt>");
    el(w, "Id", id);
    party40_agt(w, "Assgnr", assgnr);
    party40_agt(w, "Assgne", assgne);
    el_dt(w, "CreDtTm", dt);
    cb_w_s(w, "</Assgnmt>");
}

static void case5(cb_w *w, const char *t, const char *id, const char *cretr)
{
    open_tag(w, t);
    el(w, "Id", id);
    party40_agt(w, "Cretr", cretr);
    close_tag(w, t);
}

int32_t cb_mx_camt056_write(const cb_mx_camt056 *m, char *buf, uint32_t cap)
{
    if (!m || !buf || !cap) return CB_MX_E_NULL;
    uint8_t mo;
    int e;
    if (!txt(m->assgnmt_id, 35, true) || !txt(m->case_id, 35, true) ||
        !txt(m->orgnl_msg_id, 35, true) || !txt(m->orgnl_msg_nm_id, 35, true) ||
        !txt(m->orgnl_e2e_id, 35, false))
        return CB_MX_E_ID;
    if (!cb_bic_valid(m->assgnr) || !cb_bic_valid(m->assgne) || !cb_bic_valid(m->case_cretr))
        return CB_MX_E_BIC;
    if (!uetr_ok(m->orgnl_uetr)) return CB_MX_E_UETR;
    if ((e = amt_ok(&m->orgnl_amt, &mo))) return e;
    if (!code4(m->rsn_cd, true)) return CB_MX_E_CODE;
    if (!txt(m->addtl_inf, 105, false)) return CB_MX_E_TEXT;

    cb_w w;
    cb_w_init(&w, buf, cap);
    doc_open(&w, "camt.056.001.08", "FIToFIPmtCxlReq");
    assgnmt(&w, m->assgnmt_id, m->assgnr, m->assgne, m->cre_dt);
    cb_w_s(&w, "<Undrlyg><TxInf>");
    case5(&w, "Case", m->case_id, m->case_cretr);
    cb_w_s(&w, "<OrgnlGrpInf>");
    el(&w, "OrgnlMsgId", m->orgnl_msg_id);
    el(&w, "OrgnlMsgNmId", m->orgnl_msg_nm_id);
    cb_w_s(&w, "</OrgnlGrpInf>");
    el(&w, "OrgnlEndToEndId", m->orgnl_e2e_id);
    el(&w, "OrgnlUETR", m->orgnl_uetr);
    el_amt(&w, "OrgnlIntrBkSttlmAmt", &m->orgnl_amt, mo);
    el_date(&w, "OrgnlIntrBkSttlmDt", m->orgnl_sttlm_dt);
    cb_w_s(&w, "<CxlRsnInf><Rsn><Cd>");
    cb_w_s(&w, m->rsn_cd);
    cb_w_s(&w, "</Cd></Rsn>");
    el(&w, "AddtlInf", m->addtl_inf);
    cb_w_s(&w, "</CxlRsnInf></TxInf></Undrlyg>");
    return doc_close(&w, "FIToFIPmtCxlReq");
}

int32_t cb_mx_camt029_write(const cb_mx_camt029 *m, char *buf, uint32_t cap)
{
    if (!m || !buf || !cap) return CB_MX_E_NULL;
    if (!txt(m->assgnmt_id, 35, true) || !txt(m->case_id, 35, true) ||
        !txt(m->orgnl_msg_id, 35, true) || !txt(m->orgnl_msg_nm_id, 35, true) ||
        !txt(m->orgnl_e2e_id, 35, false))
        return CB_MX_E_ID;
    if (!cb_bic_valid(m->assgnr) || !cb_bic_valid(m->assgne) || !cb_bic_valid(m->case_cretr))
        return CB_MX_E_BIC;
    if (!uetr_ok(m->orgnl_uetr)) return CB_MX_E_UETR;
    if (!code4(m->rsn_cd, !m->accepted)) return CB_MX_E_CODE;
    if (!txt(m->addtl_inf, 105, false)) return CB_MX_E_TEXT;

    cb_w w;
    cb_w_init(&w, buf, cap);
    doc_open(&w, "camt.029.001.09", "RsltnOfInvstgtn");
    assgnmt(&w, m->assgnmt_id, m->assgnr, m->assgne, m->cre_dt);
    case5(&w, "RslvdCase", m->case_id, m->case_cretr);
    cb_w_s(&w, "<Sts><Conf>");
    cb_w_s(&w, m->accepted ? "CNCL" : "RJCR");
    cb_w_s(&w, "</Conf></Sts><CxlDtls><TxInfAndSts><OrgnlGrpInf>");
    el(&w, "OrgnlMsgId", m->orgnl_msg_id);
    el(&w, "OrgnlMsgNmId", m->orgnl_msg_nm_id);
    cb_w_s(&w, "</OrgnlGrpInf>");
    el(&w, "OrgnlEndToEndId", m->orgnl_e2e_id);
    el(&w, "OrgnlUETR", m->orgnl_uetr);
    el(&w, "TxCxlSts", m->accepted ? "ACCR" : "RJCR");
    if ((m->rsn_cd && m->rsn_cd[0]) || (m->addtl_inf && m->addtl_inf[0])) {
        cb_w_s(&w, "<CxlStsRsnInf>");
        if (m->rsn_cd && m->rsn_cd[0]) {
            cb_w_s(&w, "<Rsn><Cd>");
            cb_w_s(&w, m->rsn_cd);
            cb_w_s(&w, "</Cd></Rsn>");
        }
        el(&w, "AddtlInf", m->addtl_inf);
        cb_w_s(&w, "</CxlStsRsnInf>");
    }
    cb_w_s(&w, "</TxInfAndSts></CxlDtls>");
    return doc_close(&w, "RsltnOfInvstgtn");
}
