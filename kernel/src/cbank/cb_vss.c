/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_vss.c — see cb_vss.h. */
#include "cb_vss.h"
#include "cb_ccy.h"
#include "cb_util.h"

bool cb_vss_rail_valid(uint16_t rail)
{
    return rail == CB_RAIL_DEBIT || rail == CB_RAIL_CREDIT || rail == CB_RAIL_EQUITY;
}

static bool text_ok(const char *s, size_t cap, bool required)
{
    size_t n = cb_strnlen(s, cap);
    if (n >= cap) return false;
    return required ? n > 0 : true;
}

int cb_vss_receipt_check(const cb_vss_receipt *r)
{
    if (!r) return CB_VSS_E_NULL;
    if (!cb_vss_rail_valid(r->rail_dr) || !cb_vss_rail_valid(r->rail_cr) ||
        r->rail_dr == r->rail_cr)
        return CB_VSS_E_RAIL;
    if (!cb_ccy_payable(cb_ccy_by_alpha(r->ccy))) return CB_VSS_E_CCY;
    if (!text_ok(r->rcpt_id, sizeof r->rcpt_id, true) ||
        !text_ok(r->sys_ref, sizeof r->sys_ref, true) ||
        !text_ok(r->sgntr, sizeof r->sgntr, true) ||
        !text_ok(r->xwalk_ref, sizeof r->xwalk_ref, false) ||
        !text_ok(r->anchr_ref, sizeof r->anchr_ref, false) ||
        !text_ok(r->tx_ref, sizeof r->tx_ref, false) ||
        !text_ok(r->attest_ref, sizeof r->attest_ref, false))
        return CB_VSS_E_FIELD;
    if (r->amt == 0) return CB_VSS_E_FIELD;
    if (r->bckg != CB_BCKG_ASPL && r->bckg != CB_BCKG_EFCT) return CB_VSS_E_BCKG;
    if (r->bckg == CB_BCKG_EFCT && !r->attest_ref[0]) return CB_VSS_E_BCKG;
    return CB_VSS_OK;
}

static bool leg_sums(const cb_vss_tx *t, uint64_t *dr, uint64_t *cr)
{
    *dr = *cr = 0;
    if (!t->legs || t->n_legs < 2) return false;
    for (uint32_t j = 0; j < t->n_legs; j++) {
        const cb_vss_leg *l = &t->legs[j];
        if (!cb_vss_rail_valid(l->rail) || l->amount == 0) return false;
        uint64_t *acc = l->side == CB_SIDE_DR ? dr : l->side == CB_SIDE_CR ? cr : 0;
        if (!acc || !cb_add_ok(*acc, l->amount, acc)) return false;
    }
    return true;
}

int cb_vss_c1(const cb_vss_tx *tx, uint32_t n, uint32_t *bad)
{
    if (n && !tx) return CB_VSS_E_NULL;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t dr, cr;
        if (!leg_sums(&tx[i], &dr, &cr) || dr != cr) {
            if (bad) *bad = i;
            return CB_VSS_E_BALANCE;
        }
    }
    return CB_VSS_OK;
}

static bool has_leg(const cb_vss_tx *t, uint16_t rail, uint8_t side)
{
    for (uint32_t j = 0; j < t->n_legs; j++)
        if (t->legs[j].rail == rail && t->legs[j].side == side) return true;
    return false;
}

int cb_vss_c2(const cb_vss_tx *tx, uint32_t n, const cb_vss_receipt *rc, uint32_t nr, uint32_t *bad)
{
    if ((n && !tx) || (nr && !rc)) return CB_VSS_E_NULL;
    if (nr != n) {
        if (bad) *bad = n < nr ? n : nr;
        return CB_VSS_E_RECEIPT;
    }
    for (uint32_t i = 0; i < nr; i++) {
        if (cb_vss_receipt_check(&rc[i]) != CB_VSS_OK) {
            if (bad) *bad = i;
            return CB_VSS_E_RECEIPT;
        }
        for (uint32_t j = 0; j < i; j++)
            if (cb_streq(rc[i].rcpt_id, rc[j].rcpt_id)) {
                if (bad) *bad = i;
                return CB_VSS_E_RECEIPT;
            }
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t found = 0, k = 0;
        for (uint32_t j = 0; j < nr; j++)
            if (cb_streq(rc[j].tx_ref, tx[i].tx_id)) {
                found++;
                k = j;
            }
        uint64_t dr, cr;
        bool ok = found == 1 && leg_sums(&tx[i], &dr, &cr) && rc[k].amt == dr &&
                  cb_streq(rc[k].ccy, tx[i].ccy) && has_leg(&tx[i], rc[k].rail_dr, CB_SIDE_DR) &&
                  has_leg(&tx[i], rc[k].rail_cr, CB_SIDE_CR);
        if (!ok) {
            if (bad) *bad = i;
            return CB_VSS_E_RECEIPT;
        }
    }
    return CB_VSS_OK;
}

int cb_vss_log_append(cb_vss_log *l, const cb_vss_receipt *r)
{
    if (!l || !r) return CB_VSS_E_NULL;
    int c = cb_vss_receipt_check(r);
    if (c != CB_VSS_OK) return c;
    for (uint32_t i = 0; i < l->n; i++)
        if (cb_streq(l->r[i].rcpt_id, r->rcpt_id)) return CB_VSS_E_RECEIPT;
    if (l->n >= CB_VSS_LOG_CAP) return CB_VSS_E_CAP;
    cb_memcpy(&l->r[l->n++], r, sizeof *r);
    return CB_VSS_OK;
}

/* ===== C3 crosswalk tables =====
 * Debit-natured items -> 555, credit-natured -> 777, equity -> 888. The
 * statement side (Dr/Cr) of each line is carried unchanged, so a contra or
 * reversal entry round-trips exactly. */
const cb_xw_item cb_xw_table[] = {
    {CB_FW_US_GAAP, "USG.ASSET", "Assets (ASC 210)", CB_RAIL_DEBIT, 0},
    {CB_FW_US_GAAP, "USG.LIAB", "Liabilities incl. contract liabilities (ASC 606)", CB_RAIL_CREDIT,
     0},
    {CB_FW_US_GAAP, "USG.EQUITY", "Stockholders' equity (ASC 505)", CB_RAIL_EQUITY, 0},
    {CB_FW_US_GAAP, "USG.REV", "Revenue (ASC 606)", CB_RAIL_CREDIT, 0},
    {CB_FW_US_GAAP, "USG.EXP", "Expenses", CB_RAIL_DEBIT, 0},
    {CB_FW_US_GAAP, "USG.INT", "Interest (ASC 835) - refused", 0, CB_XW_INTEREST},
    {CB_FW_IFRS, "IFRS.ASSET", "Assets (IAS 1, IAS 16)", CB_RAIL_DEBIT, 0},
    {CB_FW_IFRS, "IFRS.LIAB", "Liabilities (IAS 1)", CB_RAIL_CREDIT, 0},
    {CB_FW_IFRS, "IFRS.ECL", "Expected credit loss allowance, contra (IFRS 9)", CB_RAIL_CREDIT, 0},
    {CB_FW_IFRS, "IFRS.EQUITY", "Equity (IAS 1)", CB_RAIL_EQUITY, 0},
    {CB_FW_IFRS, "IFRS.OCI", "Other comprehensive income, equity sub-coordinate", CB_RAIL_EQUITY,
     0},
    {CB_FW_IFRS, "IFRS.REV", "Revenue (IFRS 15)", CB_RAIL_CREDIT, 0},
    {CB_FW_IFRS, "IFRS.EXP", "Expenses", CB_RAIL_DEBIT, 0},
    {CB_FW_IFRS, "IFRS.EIR", "Effective-interest income/expense (IFRS 9) - refused", 0,
     CB_XW_INTEREST},
    {CB_FW_IPSAS, "IPSAS.ASSET", "Assets (IPSAS 1)", CB_RAIL_DEBIT, CB_XW_FUND},
    {CB_FW_IPSAS, "IPSAS.LIAB", "Liabilities (IPSAS 1)", CB_RAIL_CREDIT, CB_XW_FUND},
    {CB_FW_IPSAS, "IPSAS.NETASSET", "Net assets/equity (IPSAS 1)", CB_RAIL_EQUITY, CB_XW_FUND},
    {CB_FW_IPSAS, "IPSAS.REV.NX", "Revenue, non-exchange (IPSAS 23)", CB_RAIL_CREDIT, CB_XW_FUND},
    {CB_FW_IPSAS, "IPSAS.REV.X", "Revenue, exchange", CB_RAIL_CREDIT, CB_XW_FUND},
    {CB_FW_IPSAS, "IPSAS.EXP", "Expenses by appropriation", CB_RAIL_DEBIT, CB_XW_FUND},
    {CB_FW_IPSAS, "IPSAS.INT", "Interest - refused", 0, CB_XW_INTEREST | CB_XW_FUND},
    {CB_FW_AAOIFI, "AAOIFI.ASSET", "Assets", CB_RAIL_DEBIT, 0},
    {CB_FW_AAOIFI, "AAOIFI.MURABAHA", "Murabahah receivables (sale-based)", CB_RAIL_DEBIT, 0},
    {CB_FW_AAOIFI, "AAOIFI.IJARAH", "Ijarah assets (lease-based)", CB_RAIL_DEBIT, 0},
    {CB_FW_AAOIFI, "AAOIFI.MUSHARAKA", "Musharakah financing (partnership)", CB_RAIL_EQUITY,
     CB_XW_PROFIT},
    {CB_FW_AAOIFI, "AAOIFI.MUDARABA", "Mudarabah financing (profit sharing)", CB_RAIL_EQUITY,
     CB_XW_PROFIT},
    {CB_FW_AAOIFI, "AAOIFI.URIA", "Unrestricted investment accounts (profit sharing)",
     CB_RAIL_EQUITY, CB_XW_PROFIT},
    {CB_FW_AAOIFI, "AAOIFI.PROFIT", "Profit share distributable to investment account holders",
     CB_RAIL_EQUITY, CB_XW_PROFIT},
    {CB_FW_AAOIFI, "AAOIFI.LIAB", "Liabilities (non-interest)", CB_RAIL_CREDIT, 0},
    {CB_FW_AAOIFI, "AAOIFI.EQUITY", "Owners' equity", CB_RAIL_EQUITY, 0},
    {CB_FW_AAOIFI, "AAOIFI.REV", "Revenue (sales, ijarah rent, fees)", CB_RAIL_CREDIT, 0},
    {CB_FW_AAOIFI, "AAOIFI.EXP", "Expenses", CB_RAIL_DEBIT, 0},
    {CB_FW_AAOIFI, "AAOIFI.INT", "Interest - refused (purify, do not book)", 0, CB_XW_INTEREST},
};
const uint32_t cb_xw_count = sizeof cb_xw_table / sizeof cb_xw_table[0];

const char *cb_vss_xwalk_ref(cb_framework fw)
{
    switch (fw) {
    case CB_FW_US_GAAP:
        return "USGAAP-XW-v1";
    case CB_FW_IFRS:
        return "IFRS-XW-v1";
    case CB_FW_IPSAS:
        return "IPSAS-XW-v1";
    case CB_FW_AAOIFI:
        return "AAOIFI-XW-v1";
    default:
        return "";
    }
}

static int xw_find(cb_framework fw, const char *code)
{
    for (uint32_t i = 0; i < cb_xw_count; i++)
        if (cb_xw_table[i].fw == fw && cb_streq(cb_xw_table[i].code, code)) return (int) i;
    return -1;
}

int cb_vss_map_in(cb_framework fw, const cb_tb_line *tb, uint32_t n, cb_vss_rec *out, uint32_t cap,
                  uint32_t *bad)
{
    if ((n && (!tb || !out))) return CB_VSS_E_NULL;
    if (n > cap) return CB_VSS_E_CAP;
    for (uint32_t i = 0; i < n; i++) {
        int x = xw_find(fw, tb[i].code);
        int err = CB_VSS_OK;
        if (x < 0)
            err = CB_VSS_E_XWALK;
        else if (cb_xw_table[x].flags & CB_XW_INTEREST)
            err = CB_VSS_E_USURY;
        else if ((cb_xw_table[x].flags & CB_XW_FUND) &&
                 (!tb[i].fund[0] || cb_strnlen(tb[i].fund, CB_FUND_LEN) >= CB_FUND_LEN))
            err = CB_VSS_E_FUND;
        else if ((cb_xw_table[x].flags & CB_XW_PROFIT) && cb_xw_table[x].rail != CB_RAIL_EQUITY)
            err = CB_VSS_E_XWALK;
        else if ((tb[i].side != CB_SIDE_DR && tb[i].side != CB_SIDE_CR) || tb[i].amount == 0)
            err = CB_VSS_E_FIELD;
        if (err != CB_VSS_OK) {
            if (bad) *bad = i;
            return err;
        }
        cb_memset(&out[i], 0, sizeof out[i]);
        out[i].rail = cb_xw_table[x].rail;
        out[i].side = tb[i].side;
        out[i].amount = tb[i].amount;
        out[i].xw = (uint16_t) x;
        cb_strlcpy(out[i].fund, tb[i].fund, sizeof out[i].fund);
    }
    return CB_VSS_OK;
}

int cb_vss_map_out(cb_framework fw, const cb_vss_rec *in, uint32_t n, cb_tb_line *out, uint32_t cap)
{
    if (n && (!in || !out)) return CB_VSS_E_NULL;
    if (n > cap) return CB_VSS_E_CAP;
    for (uint32_t i = 0; i < n; i++) {
        if (in[i].xw >= cb_xw_count) return CB_VSS_E_XWALK;
        const cb_xw_item *x = &cb_xw_table[in[i].xw];
        if (x->fw != fw || x->rail != in[i].rail || (x->flags & CB_XW_INTEREST))
            return CB_VSS_E_XWALK;
        cb_memset(&out[i], 0, sizeof out[i]);
        if (!cb_strlcpy(out[i].code, x->code, sizeof out[i].code)) return CB_VSS_E_XWALK;
        out[i].side = in[i].side;
        out[i].amount = in[i].amount;
        cb_strlcpy(out[i].fund, in[i].fund, sizeof out[i].fund);
    }
    return CB_VSS_OK;
}

int cb_vss_c3(cb_framework fw, const cb_tb_line *tb, uint32_t n, cb_vss_rec *sr, cb_tb_line *st,
              uint32_t *bad)
{
    if (!tb || !sr || !st || n == 0) return CB_VSS_E_NULL;
    uint64_t dr = 0, cr = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t *a = tb[i].side == CB_SIDE_DR ? &dr : &cr;
        if (!cb_add_ok(*a, tb[i].amount, a)) return CB_VSS_E_BALANCE;
    }
    if (dr != cr) return CB_VSS_E_BALANCE;
    int r = cb_vss_map_in(fw, tb, n, sr, n, bad);
    if (r != CB_VSS_OK) return r;
    r = cb_vss_map_out(fw, sr, n, st, n);
    if (r != CB_VSS_OK) return r;
    for (uint32_t i = 0; i < n; i++) {
        if (!cb_streq(st[i].code, tb[i].code) || st[i].side != tb[i].side ||
            st[i].amount != tb[i].amount || !cb_streq(st[i].fund, tb[i].fund)) {
            if (bad) *bad = i;
            return CB_VSS_E_MISMATCH;
        }
    }
    return CB_VSS_OK;
}

/* ===== C4 ===== */

int cb_vss_backing_sum(const cb_vss_backing *b, uint32_t n, const char *ccy,
                       cb_vss_backing_totals *out)
{
    if ((n && !b) || !out || !ccy) return CB_VSS_E_NULL;
    out->effective = out->aspirational = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (b[i].label == CB_BCKG_EFCT && !b[i].attest_ref[0]) return CB_VSS_E_BCKG;
        if (b[i].label != CB_BCKG_EFCT && b[i].label != CB_BCKG_ASPL) return CB_VSS_E_BCKG;
        if (!cb_streq(b[i].ccy, ccy)) continue;
        uint64_t *a = b[i].label == CB_BCKG_EFCT ? &out->effective : &out->aspirational;
        if (!cb_add_ok(*a, b[i].amount, a)) return CB_VSS_E_CAP;
    }
    return CB_VSS_OK;
}

int cb_vss_c4(const cb_vss_backing *b, uint32_t n, const char *ccy,
              const cb_vss_backing_totals *presented, uint32_t *bad)
{
    if (!presented) return CB_VSS_E_NULL;
    for (uint32_t i = 0; i < n && b; i++)
        if (b[i].label == CB_BCKG_EFCT && !b[i].attest_ref[0]) {
            if (bad) *bad = i;
            return CB_VSS_E_BCKG;
        }
    cb_vss_backing_totals t;
    int r = cb_vss_backing_sum(b, n, ccy, &t);
    if (r != CB_VSS_OK) return r;
    if (presented->effective != t.effective || presented->aspirational != t.aspirational)
        return CB_VSS_E_MISMATCH;
    return CB_VSS_OK;
}

/* ===== C5 ===== */

int cb_vss_c5(const cb_vss_c5_hooks *h, const cb_vss_ident *ids, uint32_t n, uint32_t *bad)
{
    if (!h || !h->debit || !h->credit || !h->equity) return CB_VSS_E_NOHOOK;
    if (n && !ids) return CB_VSS_E_NULL;
    for (uint32_t i = 0; i < n; i++) {
        bool d = h->debit(ids[i].id), c = h->credit(ids[i].id), q = h->equity(ids[i].id);
        bool ok;
        switch (ids[i].rail) {
        case CB_RAIL_DEBIT:
            ok = d && !c && !q;
            break;
        case CB_RAIL_CREDIT:
            ok = c && !d && !q;
            break;
        case CB_RAIL_EQUITY:
            ok = q && !d && !c;
            break;
        default:
            ok = false;
            break;
        }
        if (!ok) {
            if (bad) *bad = i;
            return CB_VSS_E_ID;
        }
    }
    return CB_VSS_OK;
}

/* ===== levels ===== */

cb_vss_level_t cb_vss_level(const cb_vss_results *r)
{
    if (!r || r->run_at == 0) return CB_VSS_NONE;
    bool c1 = r->c1 == CB_VSS_PASS, c2 = r->c2 == CB_VSS_PASS, c3 = r->c3 == CB_VSS_PASS;
    bool c4 = r->c4 == CB_VSS_PASS, c5 = r->c5 == CB_VSS_PASS;
    /* A failed C4 is critical at every level (mislabelled backing misleads). */
    if (r->c4 == CB_VSS_FAIL) return CB_VSS_NONE;
    if (!(c1 && c2 && c5)) return CB_VSS_NONE;
    if (!c3) return CB_VSS_BRONZE;
    if (c4 && r->c4_attestor[0] && cb_strnlen(r->c4_attestor, CB_VSS_REF_LEN) < CB_VSS_REF_LEN)
        return CB_VSS_GOLD;
    return CB_VSS_SILVER;
}

const char *cb_vss_level_name(cb_vss_level_t l)
{
    switch (l) {
    case CB_VSS_BRONZE:
        return "Bronze";
    case CB_VSS_SILVER:
        return "Silver";
    case CB_VSS_GOLD:
        return "Gold";
    default:
        return "None";
    }
}

/* ===== 811 ===== */

int cb_vss_resolution_check(const cb_vss_resolution *r)
{
    if (!r) return CB_VSS_E_NULL;
    if (r->designator != CB_VSS_RESOLUTION_CLASS) return CB_VSS_E_RAIL;
    if (r->debtor_living_person) return CB_VSS_E_LIVING;
    if (r->basis < CB_RES_DISSOLVED_ENTITY || r->basis > CB_RES_DEFUNCT_STATE)
        return CB_VSS_E_FIELD;
    if (!text_ok(r->claim_id, sizeof r->claim_id, true)) return CB_VSS_E_FIELD;
    if (!text_ok(r->authority_ref, sizeof r->authority_ref, true) ||
        !text_ok(r->notice_ref, sizeof r->notice_ref, true) ||
        !text_ok(r->adjudication_ref, sizeof r->adjudication_ref, true))
        return CB_VSS_E_AUTH;
    int c = cb_vss_receipt_check(&r->receipt);
    if (c != CB_VSS_OK) return c;
    if (!cb_streq(r->receipt.tx_ref, r->claim_id)) return CB_VSS_E_RECEIPT;
    return CB_VSS_OK;
}
