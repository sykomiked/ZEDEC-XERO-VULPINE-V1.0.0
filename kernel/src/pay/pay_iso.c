/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_iso.c — ISO 20022 MX builders and field rules. See pay_iso.h.
 * The strict parsers live in pay_iso_parse.c. Element order in every
 * builder follows the sequences of the XSDs in kernel/src/pay/xsd. */
#include "pay_iso.h"
#include "pay_roles.h"
#include "pay_tables.h"

#define NS_PREFIX "urn:iso:std:iso:20022:tech:xsd:"

/* Same values as pay_platform_default (checked by test_pay.c). */
static const pay_platform_t k_default_platform = {"VFV", PAY_RAIL_DEBIT_CODE, 2, "NCR", false};

static const pay_platform_t *plat(const pay_iso_ctx_t *x)
{
    return (x && x->platform) ? x->platform : &k_default_platform;
}

static bool cbpr(const pay_iso_ctx_t *x)
{
    return x && x->profile == PAY_ISO_CBPR;
}

/* ===== Character sets ===== */

static bool fin_x(char c)
{
    if (pay_is_digit(c) || pay_is_upper(c) || pay_is_lower(c)) return true;
    switch (c) {
    case '/':
    case '-':
    case '?':
    case ':':
    case '(':
    case ')':
    case '.':
    case ',':
    case '\'':
    case '+':
    case ' ':
        return true;
    default:
        return false;
    }
}

static bool cbpr_ext(char c)
{
    if (fin_x(c)) return true;
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '*':
    case '=':
    case '^':
    case '_':
    case '`':
    case '{':
    case '|':
    case '}':
    case '~':
    case '"':
    case ';':
    case '<':
    case '>':
    case '@':
    case '[':
    case '\\':
    case ']':
        return true;
    default:
        return false;
    }
}

static bool printable(char c)
{
    return c >= 0x20 && c <= 0x7e;
}

bool pay_iso_id_ok(const pay_iso_ctx_t *x, const char *s, uint32_t max)
{
    size_t n = pay_strnlen(s, max + 1u);
    if (!s || n == 0 || n > max) return false;
    if (s[0] == ' ' || s[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; i++) {
        if (cbpr(x) ? !fin_x(s[i]) : !printable(s[i])) return false;
        if (cbpr(x) && i + 1 < n && s[i] == '/' && s[i + 1] == '/') return false;
    }
    if (cbpr(x) && (s[0] == '/' || s[n - 1] == '/')) return false;
    return true;
}

bool pay_iso_text_ok(const pay_iso_ctx_t *x, const char *s, uint32_t max)
{
    size_t n = pay_strnlen(s, max + 1u);
    if (!s || n == 0 || n > max) return false;
    if (s[0] == ' ' || s[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; i++)
        if (cbpr(x) ? !cbpr_ext(s[i]) : !printable(s[i])) return false;
    return true;
}

static bool digits(const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (!pay_is_digit(s[i])) return false;
    return true;
}

static int num2(const char *s)
{
    return (s[0] - '0') * 10 + (s[1] - '0');
}

bool pay_iso_date_ok(const char *s)
{
    if (!s || pay_strnlen(s, 11) != 10) return false;
    if (!digits(s, 4) || s[4] != '-' || !digits(s + 5, 2) || s[7] != '-' || !digits(s + 8, 2))
        return false;
    int m = num2(s + 5), d = num2(s + 8);
    return m >= 1 && m <= 12 && d >= 1 && d <= 31 && s[0] != '0';
}

bool pay_iso_datetime_ok(const pay_iso_ctx_t *x, const char *s)
{
    size_t n = pay_strnlen(s, 36), i = 19;
    char d[11];
    if (!s || n < 19 || n > 35) return false;
    for (int k = 0; k < 10; k++) d[k] = s[k];
    d[10] = '\0';
    if (!pay_iso_date_ok(d) || s[10] != 'T') return false;
    if (!digits(s + 11, 2) || s[13] != ':' || !digits(s + 14, 2) || s[16] != ':' ||
        !digits(s + 17, 2))
        return false;
    if (num2(s + 11) > 23 || num2(s + 14) > 59 || num2(s + 17) > 59) return false;
    if (i < n && s[i] == '.') {
        size_t f = ++i;
        while (i < n && pay_is_digit(s[i])) i++;
        if (i == f || i - f > 9) return false;
    }
    if (i == n) return !cbpr(x); /* CBPR: offset mandatory */
    if (s[i] == 'Z') return i + 1 == n;
    if ((s[i] == '+' || s[i] == '-') && i + 6 == n && digits(s + i + 1, 2) && s[i + 3] == ':' &&
        digits(s + i + 4, 2))
        return num2(s + i + 1) <= 14 && num2(s + i + 4) <= 59;
    return false;
}

int32_t pay_iso_country_ok(const pay_iso_ctx_t *x, const char *cc, bool confirmed)
{
    const pay_platform_t *p = plat(x);
    if (!cc || !pay_iso3166_valid(cc)) return PAY_ISO_ERR_COUNTRY;
    if (pay_streq(cc, p->jurisdiction)) return PAY_ISO_ERR_COUNTRY;
    if (!p->jurisdiction_iso && p->jurisdiction[0] == cc[0] && p->jurisdiction[1] == cc[1] &&
        !confirmed)
        return PAY_ISO_ERR_COUNTRY; /* "NC" must never stand in for "NCR" */
    return 0;
}

/* ===== Amounts ===== */

int32_t pay_iso_amt_from_asset(const pay_asset_t *a, uint64_t units, pay_amt_t *out)
{
    if (!a || !out) return PAY_ISO_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    if (!a->iso4217 || a->kind != PAY_ASSET_FIAT || a->store_credit) return PAY_ISO_ERR_CCY;
    if (pay_strnlen(a->code, 4) != 3 || a->minor > 4) return PAY_ISO_ERR_CCY;
    pay_strlcpy(out->ccy, a->code, sizeof out->ccy);
    out->units = units;
    out->minor = a->minor;
    out->iso4217 = true;
    return 0;
}

static int32_t amt_check(const pay_iso_ctx_t *x, const pay_amt_t *a)
{
    const pay_platform_t *p = plat(x);
    if (!a->iso4217 || pay_strnlen(a->ccy, 4) != 3) return PAY_ISO_ERR_CCY;
    for (int i = 0; i < 3; i++)
        if (!pay_is_upper(a->ccy[i])) return PAY_ISO_ERR_CCY;
    if (pay_streq(a->ccy, p->vfv_alpha)) return PAY_ISO_ERR_CCY;
    if (a->minor > 5) return PAY_ISO_ERR_FIELD; /* schema fractionDigits 5 */
    if (pay_dec_digits(a->units) > 18) return PAY_ISO_ERR_FIELD;
    return 0;
}

/* ===== Block validation ===== */

static int32_t addr_check(const pay_iso_ctx_t *x, const pay_addr_t *a)
{
    if (a->strt[0] && !pay_iso_text_ok(x, a->strt, 70)) return PAY_ISO_ERR_FIELD;
    if (a->bldg[0] && !pay_iso_text_ok(x, a->bldg, 16)) return PAY_ISO_ERR_FIELD;
    if (a->pst_cd[0] && !pay_iso_text_ok(x, a->pst_cd, 16)) return PAY_ISO_ERR_FIELD;
    if (a->twn[0] && !pay_iso_text_ok(x, a->twn, 35)) return PAY_ISO_ERR_FIELD;
    if (a->ctry_sub[0] && !pay_iso_text_ok(x, a->ctry_sub, 35)) return PAY_ISO_ERR_FIELD;
    if (cbpr(x) && (!a->twn[0] || !a->ctry[0])) return PAY_ISO_ERR_PROFILE;
    if (a->ctry[0]) return pay_iso_country_ok(x, a->ctry, a->ctry_confirmed);
    return 0;
}

static int32_t party_check(const pay_iso_ctx_t *x, const pay_party_t *p, bool need_adr)
{
    if (!pay_iso_text_ok(x, p->name, 140)) return PAY_ISO_ERR_FIELD;
    if (p->lei[0] && !pay_lei_valid(p->lei)) return PAY_ISO_ERR_FIELD;
    if (p->has_adr) return addr_check(x, &p->adr);
    if (need_adr && cbpr(x)) return PAY_ISO_ERR_PROFILE;
    return 0;
}

static int32_t agent_check(const pay_iso_ctx_t *x, const pay_agent_t *a)
{
    if (a->bic[0] && !pay_bic_valid(a->bic)) return PAY_ISO_ERR_FIELD;
    if (a->lei[0] && !pay_lei_valid(a->lei)) return PAY_ISO_ERR_FIELD;
    if (a->clr_mmb[0] && !pay_iso_id_ok(x, a->clr_mmb, 35)) return PAY_ISO_ERR_FIELD;
    if (a->name[0] && !pay_iso_text_ok(x, a->name, 140)) return PAY_ISO_ERR_FIELD;
    if (!a->bic[0] && !a->lei[0] && !a->clr_mmb[0] && !a->name[0]) return PAY_ISO_ERR_FIELD;
    if (cbpr(x) && !a->bic[0]) return PAY_ISO_ERR_PROFILE;
    return 0;
}

static int32_t acct_check(const pay_iso_ctx_t *x, const pay_acct_t *a)
{
    if (a->iban[0]) return pay_iban_valid(a->iban) ? 0 : PAY_ISO_ERR_FIELD;
    if (!pay_iso_id_ok(x, a->othr, 34)) return PAY_ISO_ERR_FIELD;
    if (a->othr_prtry[0] && !pay_iso_id_ok(x, a->othr_prtry, 35)) return PAY_ISO_ERR_FIELD;
    return 0;
}

static bool acct_present(const pay_acct_t *a)
{
    return a->iban[0] || a->othr[0];
}

static bool in_set(const char *s, const char *const *set)
{
    for (; *set; set++)
        if (pay_streq(s, *set)) return true;
    return false;
}

static bool code4(const char *s)
{
    size_t n = pay_strnlen(s, 5);
    if (n == 0 || n > 4) return false;
    for (size_t i = 0; i < n; i++)
        if (!pay_is_alnum_upper(s[i])) return false;
    return true;
}

/* ===== Writers ===== */

static void open_(pay_w *w, const char *t)
{
    pay_w_c(w, '<');
    pay_w_s(w, t);
    pay_w_c(w, '>');
}

static void close_(pay_w *w, const char *t)
{
    pay_w_s(w, "</");
    pay_w_s(w, t);
    pay_w_c(w, '>');
}

static void el(pay_w *w, const char *t, const char *v)
{
    open_(w, t);
    pay_w_esc(w, v);
    close_(w, t);
}

static void el_opt(pay_w *w, const char *t, const char *v)
{
    if (v && v[0]) el(w, t, v);
}

static void el_u64(pay_w *w, const char *t, uint64_t v)
{
    open_(w, t);
    pay_w_u64(w, v);
    close_(w, t);
}

static void el_amt(pay_w *w, const char *t, const pay_amt_t *a)
{
    pay_w_c(w, '<');
    pay_w_s(w, t);
    pay_w_s(w, " Ccy=\"");
    pay_w_s(w, a->ccy);
    pay_w_s(w, "\">");
    pay_w_amount(w, a->units, a->minor);
    close_(w, t);
}

static void doc_open(pay_w *w, const char *msg, const char *body)
{
    pay_w_s(w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Document xmlns=\"" NS_PREFIX);
    pay_w_s(w, msg);
    pay_w_s(w, "\">\n");
    open_(w, body);
    pay_w_c(w, '\n');
}

static void doc_close(pay_w *w, const char *body)
{
    close_(w, body);
    pay_w_s(w, "\n</Document>\n");
}

static void w_addr(pay_w *w, const pay_addr_t *a)
{
    open_(w, "PstlAdr");
    el_opt(w, "StrtNm", a->strt);
    el_opt(w, "BldgNb", a->bldg);
    el_opt(w, "PstCd", a->pst_cd);
    el_opt(w, "TwnNm", a->twn);
    el_opt(w, "CtrySubDvsn", a->ctry_sub);
    el_opt(w, "Ctry", a->ctry);
    close_(w, "PstlAdr");
}

static void w_party(pay_w *w, const char *tag, const pay_party_t *p)
{
    open_(w, tag);
    el(w, "Nm", p->name);
    if (p->has_adr) w_addr(w, &p->adr);
    if (p->lei[0]) {
        pay_w_s(w, "<Id><OrgId>");
        el(w, "LEI", p->lei);
        pay_w_s(w, "</OrgId></Id>");
    }
    close_(w, tag);
    pay_w_c(w, '\n');
}

static void w_fi(pay_w *w, const pay_agent_t *a)
{
    open_(w, "FinInstnId");
    el_opt(w, "BICFI", a->bic);
    if (a->clr_mmb[0]) {
        open_(w, "ClrSysMmbId");
        el(w, "MmbId", a->clr_mmb);
        close_(w, "ClrSysMmbId");
    }
    el_opt(w, "LEI", a->lei);
    el_opt(w, "Nm", a->name);
    close_(w, "FinInstnId");
}

static void w_agent(pay_w *w, const char *tag, const pay_agent_t *a)
{
    open_(w, tag);
    w_fi(w, a);
    close_(w, tag);
    pay_w_c(w, '\n');
}

static void w_acct(pay_w *w, const char *tag, const pay_acct_t *a)
{
    open_(w, tag);
    open_(w, "Id");
    if (a->iban[0]) {
        el(w, "IBAN", a->iban);
    } else {
        open_(w, "Othr");
        el(w, "Id", a->othr);
        if (a->othr_prtry[0]) {
            open_(w, "SchmeNm");
            el(w, "Prtry", a->othr_prtry);
            close_(w, "SchmeNm");
        }
        close_(w, "Othr");
    }
    close_(w, "Id");
    close_(w, tag);
    pay_w_c(w, '\n');
}

static bool note_any(const pay_note_t *n)
{
    return n && (n->jurisdiction || n->has_vfv);
}

/* The proprietary tokens. Both are inside the FIN X character set. */
static void w_note_lines(pay_w *w, const pay_iso_ctx_t *x, const pay_note_t *n, const char *tag)
{
    const pay_platform_t *p = plat(x);
    if (n->jurisdiction) {
        open_(w, tag);
        pay_w_s(w, "/ZXV/JURIS/");
        pay_w_esc(w, p->jurisdiction);
        close_(w, tag);
    }
    if (n->has_vfv) {
        open_(w, tag);
        pay_w_s(w, "/ZXV/VFV/");
        pay_w_amount(w, n->vfv_units, p->vfv_minor);
        close_(w, tag);
    }
}

static int32_t note_check(const pay_iso_ctx_t *x, const pay_note_t *n)
{
    const pay_platform_t *p = plat(x);
    size_t jl = pay_strnlen(p->jurisdiction, 8);
    if (!n) return 0;
    if (n->jurisdiction) {
        if (jl == 0) return PAY_ISO_ERR_FIELD;
        for (size_t i = 0; i < jl; i++)
            if (!pay_is_alnum_upper(p->jurisdiction[i])) return PAY_ISO_ERR_FIELD;
    }
    return 0;
}

static void w_rmt(pay_w *w, const pay_iso_ctx_t *x, const char *ustrd, const pay_note_t *n)
{
    bool has_u = ustrd && ustrd[0], has_n = note_any(n);
    if (!has_u && !has_n) return;
    open_(w, "RmtInf");
    if (has_u && !(has_n && cbpr(x))) el(w, "Ustrd", ustrd);
    if (has_n) {
        open_(w, "Strd");
        if (has_u && cbpr(x)) el(w, "AddtlRmtInf", ustrd); /* CBPR: Ustrd or Strd, not both */
        w_note_lines(w, x, n, "AddtlRmtInf");
        close_(w, "Strd");
    }
    close_(w, "RmtInf");
    pay_w_c(w, '\n');
}

static int32_t finish(pay_w *w, const char *root, const char *msg)
{
    char ns[64];
    pay_w nsw;
    int32_t n = pay_w_finish(w);
    if (n < 0) return PAY_ISO_ERR_TRUNC;
    pay_w_init(&nsw, ns, sizeof ns);
    pay_w_s(&nsw, NS_PREFIX);
    pay_w_s(&nsw, msg);
    if (pay_w_finish(&nsw) < 0) return PAY_ISO_ERR_ARG;
    if (pay_iso_check_xml(w->buf, (uint32_t) n, root, ns) != 0) return PAY_ISO_ERR_XML;
    return n;
}

/* ===== head.001.001.02 ===== */

int32_t pay_iso_bah(const pay_iso_ctx_t *x, const pay_bah_t *m, char *out, uint32_t cap)
{
    pay_w w;
    if (!m || !out) return PAY_ISO_ERR_ARG;
    if (!pay_bic_valid(m->fr_bic) || !pay_bic_valid(m->to_bic)) return PAY_ISO_ERR_FIELD;
    if (!pay_iso_id_ok(x, m->biz_msg_idr, 35) || !pay_iso_id_ok(x, m->msg_def_idr, 35))
        return PAY_ISO_ERR_FIELD;
    if (m->biz_svc[0] && !pay_iso_id_ok(x, m->biz_svc, 35)) return PAY_ISO_ERR_FIELD;
    if (cbpr(x) && !m->biz_svc[0]) return PAY_ISO_ERR_PROFILE;
    if (!pay_iso_datetime_ok(x, m->cre_dt)) return PAY_ISO_ERR_FIELD;
    pay_w_init(&w, out, cap);
    pay_w_s(&w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<AppHdr xmlns=\"" NS_PREFIX
                "head.001.001.02\">\n");
    pay_w_s(&w, "<Fr><FIId><FinInstnId>");
    el(&w, "BICFI", m->fr_bic);
    pay_w_s(&w, "</FinInstnId></FIId></Fr>\n<To><FIId><FinInstnId>");
    el(&w, "BICFI", m->to_bic);
    pay_w_s(&w, "</FinInstnId></FIId></To>\n");
    el(&w, "BizMsgIdr", m->biz_msg_idr);
    el(&w, "MsgDefIdr", m->msg_def_idr);
    el_opt(&w, "BizSvc", m->biz_svc);
    el(&w, "CreDt", m->cre_dt);
    if (m->pssbl_dplct) el(&w, "PssblDplct", "true");
    pay_w_s(&w, "\n</AppHdr>\n");
    return finish(&w, "AppHdr", "head.001.001.02");
}

/* ===== pacs.008.001.08 ===== */

static const char *const k_chrgbr[] = {"DEBT", "CRED", "SHAR", "SLEV", 0};
static const char *const k_sttlm[] = {"INDA", "INGA", "COVE", "CLRG", 0};
static const char *const k_sttlm_cbpr[] = {"INDA", "INGA", 0};

static int32_t ct_tx_check(const pay_iso_ctx_t *x, const pay_ct_tx_t *t, bool pacs)
{
    int32_t r;
    if (t->instr_id[0] && !pay_iso_id_ok(x, t->instr_id, 35)) return PAY_ISO_ERR_FIELD;
    if (!pay_iso_id_ok(x, t->e2e, 35)) return PAY_ISO_ERR_FIELD;
    if (pacs && t->tx_id[0] && !pay_iso_id_ok(x, t->tx_id, 35)) return PAY_ISO_ERR_FIELD;
    if (t->uetr[0] && !pay_uetr_valid(t->uetr)) return PAY_ISO_ERR_FIELD;
    if (pacs && cbpr(x) && !t->uetr[0]) return PAY_ISO_ERR_PROFILE;
    if ((r = amt_check(x, &t->amt)) != 0) return r;
    if (pacs && !in_set(t->chrg_br, k_chrgbr)) return PAY_ISO_ERR_FIELD;
    if (pacs) {
        if ((r = party_check(x, &t->dbtr, true)) != 0) return r;
        if ((r = agent_check(x, &t->dbtr_agt)) != 0) return r;
    }
    if ((r = agent_check(x, &t->cdtr_agt)) != 0) return r;
    if ((r = party_check(x, &t->cdtr, true)) != 0) return r;
    if (pacs && acct_present(&t->dbtr_acct) && (r = acct_check(x, &t->dbtr_acct)) != 0) return r;
    if (acct_present(&t->cdtr_acct) && (r = acct_check(x, &t->cdtr_acct)) != 0) return r;
    if (t->ustrd[0] && !pay_iso_text_ok(x, t->ustrd, 140)) return PAY_ISO_ERR_FIELD;
    return note_check(x, &t->note);
}

int32_t pay_iso_pacs008(const pay_iso_ctx_t *x, const pay_pacs008_t *m, char *out, uint32_t cap)
{
    pay_w w;
    int32_t r;
    if (!m || !out || m->n_tx == 0 || m->n_tx > PAY_ISO_MAX_TX) return PAY_ISO_ERR_ARG;
    if (!pay_iso_id_ok(x, m->msg_id, 35) || !pay_iso_datetime_ok(x, m->cre_dt_tm))
        return PAY_ISO_ERR_FIELD;
    if (!in_set(m->sttlm_mtd, k_sttlm)) return PAY_ISO_ERR_FIELD;
    if (m->intr_bk_sttlm_dt[0] && !pay_iso_date_ok(m->intr_bk_sttlm_dt)) return PAY_ISO_ERR_FIELD;
    bool instg = m->instg_agt.bic[0] || m->instg_agt.lei[0] || m->instg_agt.clr_mmb[0];
    bool instd = m->instd_agt.bic[0] || m->instd_agt.lei[0] || m->instd_agt.clr_mmb[0];
    if (cbpr(x)) {
        if (m->n_tx != 1 || !in_set(m->sttlm_mtd, k_sttlm_cbpr) || !m->intr_bk_sttlm_dt[0] ||
            !instg || !instd)
            return PAY_ISO_ERR_PROFILE;
    }
    if (instg && (r = agent_check(x, &m->instg_agt)) != 0) return r;
    if (instd && (r = agent_check(x, &m->instd_agt)) != 0) return r;
    for (uint32_t i = 0; i < m->n_tx; i++)
        if ((r = ct_tx_check(x, &m->tx[i], true)) != 0) return r;

    pay_w_init(&w, out, cap);
    doc_open(&w, "pacs.008.001.08", "FIToFICstmrCdtTrf");
    open_(&w, "GrpHdr");
    el(&w, "MsgId", m->msg_id);
    el(&w, "CreDtTm", m->cre_dt_tm);
    el_u64(&w, "NbOfTxs", m->n_tx);
    pay_w_s(&w, "<SttlmInf>");
    el(&w, "SttlmMtd", m->sttlm_mtd);
    pay_w_s(&w, "</SttlmInf>");
    close_(&w, "GrpHdr");
    pay_w_c(&w, '\n');
    for (uint32_t i = 0; i < m->n_tx; i++) {
        const pay_ct_tx_t *t = &m->tx[i];
        open_(&w, "CdtTrfTxInf");
        pay_w_c(&w, '\n');
        open_(&w, "PmtId");
        el_opt(&w, "InstrId", t->instr_id);
        el(&w, "EndToEndId", t->e2e);
        el_opt(&w, "TxId", t->tx_id);
        el_opt(&w, "UETR", t->uetr);
        close_(&w, "PmtId");
        pay_w_c(&w, '\n');
        el_amt(&w, "IntrBkSttlmAmt", &t->amt);
        el_opt(&w, "IntrBkSttlmDt", m->intr_bk_sttlm_dt);
        el_amt(&w, "InstdAmt", &t->amt);
        el(&w, "ChrgBr", t->chrg_br);
        pay_w_c(&w, '\n');
        if (instg) w_agent(&w, "InstgAgt", &m->instg_agt);
        if (instd) w_agent(&w, "InstdAgt", &m->instd_agt);
        w_party(&w, "Dbtr", &t->dbtr);
        if (acct_present(&t->dbtr_acct)) w_acct(&w, "DbtrAcct", &t->dbtr_acct);
        w_agent(&w, "DbtrAgt", &t->dbtr_agt);
        w_agent(&w, "CdtrAgt", &t->cdtr_agt);
        w_party(&w, "Cdtr", &t->cdtr);
        if (acct_present(&t->cdtr_acct)) w_acct(&w, "CdtrAcct", &t->cdtr_acct);
        w_rmt(&w, x, t->ustrd, &t->note);
        close_(&w, "CdtTrfTxInf");
        pay_w_c(&w, '\n');
    }
    doc_close(&w, "FIToFICstmrCdtTrf");
    return finish(&w, "Document", "pacs.008.001.08");
}

/* ===== pain.001.001.09 ===== */

int32_t pay_iso_pain001(const pay_iso_ctx_t *x, const pay_pain001_t *m, char *out, uint32_t cap)
{
    pay_w w;
    int32_t r;
    uint64_t sum = 0;
    if (!m || !out || m->n_tx == 0 || m->n_tx > PAY_ISO_MAX_TX) return PAY_ISO_ERR_ARG;
    if (!pay_iso_id_ok(x, m->msg_id, 35) || !pay_iso_datetime_ok(x, m->cre_dt_tm) ||
        !pay_iso_id_ok(x, m->pmt_inf_id, 35) || !pay_iso_date_ok(m->reqd_exctn_dt))
        return PAY_ISO_ERR_FIELD;
    if ((r = party_check(x, &m->initg_pty, false)) != 0) return r;
    if ((r = party_check(x, &m->dbtr, true)) != 0) return r;
    if ((r = acct_check(x, &m->dbtr_acct)) != 0) return r;
    if ((r = agent_check(x, &m->dbtr_agt)) != 0) return r;
    for (uint32_t i = 0; i < m->n_tx; i++) {
        if ((r = ct_tx_check(x, &m->tx[i], false)) != 0) return r;
        /* CtrlSum is only emitted when every amount shares the first's scale */
        if (m->tx[i].amt.minor != m->tx[0].amt.minor || !pay_add_ok(sum, m->tx[i].amt.units, &sum))
            sum = UINT64_MAX;
    }

    pay_w_init(&w, out, cap);
    doc_open(&w, "pain.001.001.09", "CstmrCdtTrfInitn");
    open_(&w, "GrpHdr");
    el(&w, "MsgId", m->msg_id);
    el(&w, "CreDtTm", m->cre_dt_tm);
    el_u64(&w, "NbOfTxs", m->n_tx);
    if (sum != UINT64_MAX && pay_dec_digits(sum) <= 18) {
        open_(&w, "CtrlSum");
        pay_w_amount(&w, sum, m->tx[0].amt.minor);
        close_(&w, "CtrlSum");
    }
    w_party(&w, "InitgPty", &m->initg_pty);
    close_(&w, "GrpHdr");
    pay_w_c(&w, '\n');
    open_(&w, "PmtInf");
    pay_w_c(&w, '\n');
    el(&w, "PmtInfId", m->pmt_inf_id);
    el(&w, "PmtMtd", "TRF");
    el_u64(&w, "NbOfTxs", m->n_tx);
    pay_w_s(&w, "<ReqdExctnDt>");
    el(&w, "Dt", m->reqd_exctn_dt);
    pay_w_s(&w, "</ReqdExctnDt>\n");
    w_party(&w, "Dbtr", &m->dbtr);
    w_acct(&w, "DbtrAcct", &m->dbtr_acct);
    w_agent(&w, "DbtrAgt", &m->dbtr_agt);
    for (uint32_t i = 0; i < m->n_tx; i++) {
        const pay_ct_tx_t *t = &m->tx[i];
        open_(&w, "CdtTrfTxInf");
        open_(&w, "PmtId");
        el_opt(&w, "InstrId", t->instr_id);
        el(&w, "EndToEndId", t->e2e);
        el_opt(&w, "UETR", t->uetr);
        close_(&w, "PmtId");
        pay_w_s(&w, "<Amt>");
        el_amt(&w, "InstdAmt", &t->amt);
        pay_w_s(&w, "</Amt>\n");
        w_agent(&w, "CdtrAgt", &t->cdtr_agt);
        w_party(&w, "Cdtr", &t->cdtr);
        if (acct_present(&t->cdtr_acct)) w_acct(&w, "CdtrAcct", &t->cdtr_acct);
        w_rmt(&w, x, t->ustrd, &t->note);
        close_(&w, "CdtTrfTxInf");
        pay_w_c(&w, '\n');
    }
    close_(&w, "PmtInf");
    pay_w_c(&w, '\n');
    doc_close(&w, "CstmrCdtTrfInitn");
    return finish(&w, "Document", "pain.001.001.09");
}

/* ===== pain.002.001.11 ===== */

int32_t pay_iso_pain002(const pay_iso_ctx_t *x, const pay_pain002_t *m, char *out, uint32_t cap)
{
    pay_w w;
    if (!m || !out || m->n_tx > PAY_ISO_MAX_TX) return PAY_ISO_ERR_ARG;
    if (!pay_iso_id_ok(x, m->msg_id, 35) || !pay_iso_datetime_ok(x, m->cre_dt_tm) ||
        !pay_iso_id_ok(x, m->orgnl_msg_id, 35) || !pay_iso_id_ok(x, m->orgnl_msg_nm_id, 35))
        return PAY_ISO_ERR_FIELD;
    if (m->grp_sts[0] && !code4(m->grp_sts)) return PAY_ISO_ERR_FIELD;
    if (m->n_tx && !pay_iso_id_ok(x, m->orgnl_pmt_inf_id, 35)) return PAY_ISO_ERR_FIELD;
    for (uint32_t i = 0; i < m->n_tx; i++) {
        const pay_sts_tx_t *t = &m->tx[i];
        if (t->orgnl_instr_id[0] && !pay_iso_id_ok(x, t->orgnl_instr_id, 35))
            return PAY_ISO_ERR_FIELD;
        if (!pay_iso_id_ok(x, t->orgnl_e2e, 35) || !code4(t->sts)) return PAY_ISO_ERR_FIELD;
        if (t->orgnl_uetr[0] && !pay_uetr_valid(t->orgnl_uetr)) return PAY_ISO_ERR_FIELD;
        if (t->rsn_cd[0] && !code4(t->rsn_cd)) return PAY_ISO_ERR_FIELD;
        if (note_check(x, &t->note) != 0) return PAY_ISO_ERR_FIELD;
    }
    pay_w_init(&w, out, cap);
    doc_open(&w, "pain.002.001.11", "CstmrPmtStsRpt");
    open_(&w, "GrpHdr");
    el(&w, "MsgId", m->msg_id);
    el(&w, "CreDtTm", m->cre_dt_tm);
    close_(&w, "GrpHdr");
    pay_w_c(&w, '\n');
    open_(&w, "OrgnlGrpInfAndSts");
    el(&w, "OrgnlMsgId", m->orgnl_msg_id);
    el(&w, "OrgnlMsgNmId", m->orgnl_msg_nm_id);
    el_opt(&w, "GrpSts", m->grp_sts);
    close_(&w, "OrgnlGrpInfAndSts");
    pay_w_c(&w, '\n');
    if (m->n_tx) {
        open_(&w, "OrgnlPmtInfAndSts");
        el(&w, "OrgnlPmtInfId", m->orgnl_pmt_inf_id);
        for (uint32_t i = 0; i < m->n_tx; i++) {
            const pay_sts_tx_t *t = &m->tx[i];
            open_(&w, "TxInfAndSts");
            el_opt(&w, "OrgnlInstrId", t->orgnl_instr_id);
            el(&w, "OrgnlEndToEndId", t->orgnl_e2e);
            el_opt(&w, "OrgnlUETR", t->orgnl_uetr);
            el(&w, "TxSts", t->sts);
            if (t->rsn_cd[0] || note_any(&t->note)) {
                open_(&w, "StsRsnInf");
                if (t->rsn_cd[0]) {
                    pay_w_s(&w, "<Rsn>");
                    el(&w, "Cd", t->rsn_cd);
                    pay_w_s(&w, "</Rsn>");
                }
                if (note_any(&t->note)) w_note_lines(&w, x, &t->note, "AddtlInf");
                close_(&w, "StsRsnInf");
            }
            close_(&w, "TxInfAndSts");
            pay_w_c(&w, '\n');
        }
        close_(&w, "OrgnlPmtInfAndSts");
        pay_w_c(&w, '\n');
    }
    doc_close(&w, "CstmrPmtStsRpt");
    return finish(&w, "Document", "pain.002.001.11");
}

/* ===== camt.052 / 053 / 054 ===== */

static const char *const k_ntry_sts[] = {"BOOK", "PDNG", "INFO", 0};

int32_t pay_iso_camt(const pay_iso_ctx_t *x, const pay_camt_t *m, char *out, uint32_t cap)
{
    pay_w w;
    int32_t r;
    const char *msg, *body, *rpt;
    if (!m || !out || m->n_bal > PAY_ISO_MAX_BAL || m->n_ntry > PAY_ISO_MAX_NTRY)
        return PAY_ISO_ERR_ARG;
    switch (m->kind) {
    case PAY_CAMT_052:
        msg = "camt.052.001.08";
        body = "BkToCstmrAcctRpt";
        rpt = "Rpt";
        break;
    case PAY_CAMT_053:
        msg = "camt.053.001.08";
        body = "BkToCstmrStmt";
        rpt = "Stmt";
        break;
    case PAY_CAMT_054:
        msg = "camt.054.001.08";
        body = "BkToCstmrDbtCdtNtfctn";
        rpt = "Ntfctn";
        break;
    default:
        return PAY_ISO_ERR_ARG;
    }
    if (m->kind == PAY_CAMT_053 && m->n_bal == 0) return PAY_ISO_ERR_FIELD;
    if (m->kind == PAY_CAMT_054 && m->n_bal != 0) return PAY_ISO_ERR_FIELD; /* no Bal in 054 */
    if (!pay_iso_id_ok(x, m->msg_id, 35) || !pay_iso_datetime_ok(x, m->cre_dt_tm) ||
        !pay_iso_id_ok(x, m->rpt_id, 35))
        return PAY_ISO_ERR_FIELD;
    if ((r = acct_check(x, &m->acct)) != 0) return r;
    if (m->acct_ccy[0]) {
        pay_amt_t probe = {0, {0}, 0, true};
        pay_strlcpy(probe.ccy, m->acct_ccy, sizeof probe.ccy);
        if ((r = amt_check(x, &probe)) != 0) return r;
    }
    for (uint32_t i = 0; i < m->n_bal; i++) {
        const pay_bal_t *b = &m->bal[i];
        if (!code4(b->code) || !pay_iso_date_ok(b->dt)) return PAY_ISO_ERR_FIELD;
        if ((r = amt_check(x, &b->amt)) != 0) return r;
    }
    for (uint32_t i = 0; i < m->n_ntry; i++) {
        const pay_ntry_t *e = &m->ntry[i];
        if ((r = amt_check(x, &e->amt)) != 0) return r;
        if (!in_set(e->sts, k_ntry_sts)) return PAY_ISO_ERR_FIELD;
        if (e->ntry_ref[0] && !pay_iso_id_ok(x, e->ntry_ref, 35)) return PAY_ISO_ERR_FIELD;
        if (e->acct_svcr_ref[0] && !pay_iso_id_ok(x, e->acct_svcr_ref, 35))
            return PAY_ISO_ERR_FIELD;
        if (e->bookg_dt[0] && !pay_iso_date_ok(e->bookg_dt)) return PAY_ISO_ERR_FIELD;
        if (e->val_dt[0] && !pay_iso_date_ok(e->val_dt)) return PAY_ISO_ERR_FIELD;
        if (e->e2e[0] && !pay_iso_id_ok(x, e->e2e, 35)) return PAY_ISO_ERR_FIELD;
        if (e->uetr[0] && !pay_uetr_valid(e->uetr)) return PAY_ISO_ERR_FIELD;
        if (e->ustrd[0] && !pay_iso_text_ok(x, e->ustrd, 140)) return PAY_ISO_ERR_FIELD;
        if (note_check(x, &e->note) != 0) return PAY_ISO_ERR_FIELD;
    }

    pay_w_init(&w, out, cap);
    doc_open(&w, msg, body);
    open_(&w, "GrpHdr");
    el(&w, "MsgId", m->msg_id);
    el(&w, "CreDtTm", m->cre_dt_tm);
    close_(&w, "GrpHdr");
    pay_w_c(&w, '\n');
    open_(&w, rpt);
    pay_w_c(&w, '\n');
    el(&w, "Id", m->rpt_id);
    el(&w, "CreDtTm", m->cre_dt_tm);
    open_(&w, "Acct");
    open_(&w, "Id");
    if (m->acct.iban[0]) {
        el(&w, "IBAN", m->acct.iban);
    } else {
        open_(&w, "Othr");
        el(&w, "Id", m->acct.othr);
        if (m->acct.othr_prtry[0]) {
            open_(&w, "SchmeNm");
            el(&w, "Prtry", m->acct.othr_prtry);
            close_(&w, "SchmeNm");
        }
        close_(&w, "Othr");
    }
    close_(&w, "Id");
    el_opt(&w, "Ccy", m->acct_ccy);
    close_(&w, "Acct");
    pay_w_c(&w, '\n');
    /* No Intrst element is ever emitted: no usury (pay_ledger.h). */
    for (uint32_t i = 0; i < m->n_bal; i++) {
        const pay_bal_t *b = &m->bal[i];
        pay_w_s(&w, "<Bal><Tp><CdOrPrtry>");
        el(&w, "Cd", b->code);
        pay_w_s(&w, "</CdOrPrtry></Tp>");
        el_amt(&w, "Amt", &b->amt);
        el(&w, "CdtDbtInd", b->debit ? "DBIT" : "CRDT");
        pay_w_s(&w, "<Dt>");
        el(&w, "Dt", b->dt);
        pay_w_s(&w, "</Dt></Bal>\n");
    }
    for (uint32_t i = 0; i < m->n_ntry; i++) {
        const pay_ntry_t *e = &m->ntry[i];
        open_(&w, "Ntry");
        el_opt(&w, "NtryRef", e->ntry_ref);
        el_amt(&w, "Amt", &e->amt);
        el(&w, "CdtDbtInd", e->debit ? "DBIT" : "CRDT");
        pay_w_s(&w, "<Sts>");
        el(&w, "Cd", e->sts);
        pay_w_s(&w, "</Sts>");
        if (e->bookg_dt[0]) {
            pay_w_s(&w, "<BookgDt>");
            el(&w, "Dt", e->bookg_dt);
            pay_w_s(&w, "</BookgDt>");
        }
        if (e->val_dt[0]) {
            pay_w_s(&w, "<ValDt>");
            el(&w, "Dt", e->val_dt);
            pay_w_s(&w, "</ValDt>");
        }
        el_opt(&w, "AcctSvcrRef", e->acct_svcr_ref);
        pay_w_s(&w, "<BkTxCd><Domn><Cd>PMNT</Cd><Fmly><Cd>");
        pay_w_s(&w, e->debit ? "ICDT" : "RCDT");
        pay_w_s(&w, "</Cd><SubFmlyCd>OTHR</SubFmlyCd></Fmly></Domn></BkTxCd>");
        if (e->e2e[0] || e->uetr[0] || e->ustrd[0] || note_any(&e->note)) {
            pay_w_s(&w, "<NtryDtls><TxDtls>");
            if (e->e2e[0] || e->uetr[0]) {
                open_(&w, "Refs");
                el_opt(&w, "EndToEndId", e->e2e);
                el_opt(&w, "UETR", e->uetr);
                close_(&w, "Refs");
            }
            w_rmt(&w, x, e->ustrd, &e->note);
            pay_w_s(&w, "</TxDtls></NtryDtls>");
        }
        close_(&w, "Ntry");
        pay_w_c(&w, '\n');
    }
    close_(&w, rpt);
    pay_w_c(&w, '\n');
    doc_close(&w, body);
    return finish(&w, "Document", msg);
}
