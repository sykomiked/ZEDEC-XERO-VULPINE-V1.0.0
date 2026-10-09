/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* iso20022.c — bounded, deterministic ISO 20022 MX serializer.
 *
 * No XML library, no libc, no float. Every write goes through iso_w, which
 * reserves one byte for the trailing NUL and refuses to write past `cap`.
 * The whole banking dialect, spoken through a length-checked megaphone. */

#include "iso20022.h"

/* ===== Rail / currency mapping ===== */

uint16_t iso20022_rail_ccy(iso_rail_t rail)
{
    switch (rail) {
    case ISO_RAIL_DEBIT:
        return ISO_CCY_DEBIT; /* 846 */
    case ISO_RAIL_CREDIT:
        return ISO_CCY_CREDIT; /* 810 */
    case ISO_RAIL_EQUITY:
        return ISO_CCY_EQUITY; /* 888 */
    default:
        return 0;
    }
}

const char *iso20022_ccy_alpha(uint16_t code)
{
    /* Real ISO 4217 numeric -> alpha-3, for the currencies a ZXV wire message
     * is actually likely to settle in, plus the 999/XXX sentinel. The Vino
     * rail numerics 846, 810 and 888 are NOT here on purpose — they are not
     * active currencies (810 is the withdrawn RUR code). */
    switch (code) {
    case 840u:
        return "USD";
    case 978u:
        return "EUR";
    case 826u:
        return "GBP";
    case 392u:
        return "JPY";
    case 356u:
        return "INR";
    case 156u:
        return "CNY";
    case 36u:
        return "AUD";
    case 124u:
        return "CAD";
    case 756u:
        return "CHF";
    case 710u:
        return "ZAR";
    case 986u:
        return "BRL";
    case 784u:
        return "AED";
    case 999u:
        return "XXX"; /* real code — but "no currency": caveat */
    default:
        return ""; /* unknown to a conventional parser      */
    }
}

const char *iso20022_ccy_caveat(uint16_t code)
{
    switch (code) {
    case 999u:
        return "ISO 4217 999=XXX 'no currency': it carries no value on "
               "the wire. Settle wire amounts in a real currency.";
    case 846u:
        return "846 is the Vino DEBIT/backing rail numeric, not an active "
               "ISO 4217 currency; conventional parsers reject it. Use a "
               "real settlement currency (USD/EUR/INR/...) on the wire.";
    case 810u:
        return "810 is the Vino CREDIT/claim rail numeric. It was the "
               "ISO 4217 code of the old Russian ruble (RUR), withdrawn in "
               "1998; a legacy parser may read it as RUR. Use a real "
               "settlement currency on the wire.";
    case 888u:
        return "888 is the Vino EQUITY rail numeric, not an active "
               "ISO 4217 currency; conventional parsers reject it. Use a "
               "real settlement currency on the wire.";
    default:
        return "";
    }
}

/* ===== Bounded writer ===== */

typedef struct {
    char *buf;
    uint32_t cap;
    uint32_t len;
    bool trunc;
} iso_w;

/* Append one char, reserving a byte for the final NUL. */
static void wc(iso_w *w, char c)
{
    if (w->trunc) return;
    if (w->cap == 0 || w->len + 1u >= w->cap) {
        w->trunc = true;
        return;
    }
    w->buf[w->len++] = c;
}

static void ws(iso_w *w, const char *s)
{
    while (*s) {
        wc(w, *s++);
        if (w->trunc) return;
    }
}

/* XML-escape a caller-supplied string (names, ids may contain & < > " '). */
static void ws_esc(iso_w *w, const char *s)
{
    while (*s) {
        char c = *s++;
        switch (c) {
        case '&':
            ws(w, "&amp;");
            break;
        case '<':
            ws(w, "&lt;");
            break;
        case '>':
            ws(w, "&gt;");
            break;
        case '"':
            ws(w, "&quot;");
            break;
        case '\'':
            ws(w, "&apos;");
            break;
        default:
            wc(w, c);
            break;
        }
        if (w->trunc) return;
    }
}

/* Unsigned decimal, no leading zeros (except the value 0 itself). */
static void wu(iso_w *w, uint64_t v)
{
    char tmp[20]; /* 2^64-1 is 20 digits */
    int n = 0;
    if (v == 0) {
        wc(w, '0');
        return;
    }
    while (v > 0 && n < 20) {
        tmp[n++] = (char) ('0' + (int) (v % 10u));
        v /= 10u;
    }
    while (n > 0) wc(w, tmp[--n]);
}

/* abs(units) as uint64, without signed-overflow UB on INT64_MIN. */
static uint64_t abs_u64(int64_t v)
{
    return (v < 0) ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}

/* Fixed-point decimal: magnitude of `units` with `frac_digits` places. */
static void w_amount(iso_w *w, int64_t units, uint8_t frac_digits)
{
    uint64_t mag = abs_u64(units);

    if (frac_digits == 0) {
        wu(w, mag);
        return;
    }

    /* pow10 = 10^frac_digits (bounded; frac_digits realistically <= 18). */
    uint64_t pow10 = 1;
    for (uint8_t i = 0; i < frac_digits && i < 19; i++) pow10 *= 10u;

    uint64_t whole = mag / pow10;
    uint64_t frac = mag % pow10;

    wu(w, whole);
    wc(w, '.');
    /* zero-pad the fraction to exactly frac_digits places */
    uint64_t scale = pow10 / 10u;
    for (uint8_t i = 0; i < frac_digits && i < 19; i++) {
        uint64_t d = (scale == 0) ? 0 : (frac / scale) % 10u;
        wc(w, (char) ('0' + (int) d));
        if (scale == 0) { /* keep padding zeros */
        } else
            scale /= 10u;
    }
}

/* ===== Public amount formatter ===== */

int32_t iso20022_format_amount(char *out, uint32_t cap, int64_t units, uint8_t frac_digits)
{
    if (!out) return ISO_ERR_NULL;
    iso_w w = {out, cap, 0, false};
    w_amount(&w, units, frac_digits);
    if (cap > 0) out[w.len] = '\0';
    return w.trunc ? ISO_ERR_TRUNC : (int32_t) w.len;
}

/* ===== PACS.008 ===== */

int32_t iso20022_pacs008_build(const pacs008_t *m, char *out, uint32_t cap)
{
    if (!m || !out) return ISO_ERR_NULL;
    iso_w w = {out, cap, 0, false};

    ws(&w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    ws(&w, "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.09\">\n");
    ws(&w, "  <FIToFICstmrCdtTrf>\n");

    /* --- Group header --- */
    ws(&w, "    <GrpHdr>\n");
    ws(&w, "      <MsgId>");
    ws_esc(&w, m->msg_id);
    ws(&w, "</MsgId>\n");
    ws(&w, "      <CreDtTm>");
    ws_esc(&w, m->cre_dt_tm);
    ws(&w, "</CreDtTm>\n");
    ws(&w, "      <NbOfTxs>");
    wu(&w, (uint64_t) m->nb_of_txs);
    ws(&w, "</NbOfTxs>\n");
    ws(&w, "      <SttlmInf><SttlmMtd>CLRG</SttlmMtd></SttlmInf>\n");
    ws(&w, "    </GrpHdr>\n");

    /* --- Credit transfer transaction --- */
    ws(&w, "    <CdtTrfTxInf>\n");
    ws(&w, "      <PmtId><EndToEndId>");
    ws_esc(&w, m->end_to_end_id);
    ws(&w, "</EndToEndId></PmtId>\n");

    ws(&w, "      <IntrBkSttlmAmt Ccy=\"");
    ws_esc(&w, m->ccy);
    ws(&w, "\">");
    w_amount(&w, m->amount_units, m->amount_frac);
    ws(&w, "</IntrBkSttlmAmt>\n");

    ws(&w, "      <ChrgBr>SLEV</ChrgBr>\n");

    ws(&w, "      <Dbtr><Nm>");
    ws_esc(&w, m->debtor_name);
    ws(&w, "</Nm></Dbtr>\n");
    ws(&w, "      <DbtrAcct><Id><Othr><Id>");
    ws_esc(&w, m->debtor_acct);
    ws(&w, "</Id></Othr></Id></DbtrAcct>\n");

    ws(&w, "      <Cdtr><Nm>");
    ws_esc(&w, m->creditor_name);
    ws(&w, "</Nm></Cdtr>\n");
    ws(&w, "      <CdtrAcct><Id><Othr><Id>");
    ws_esc(&w, m->creditor_acct);
    ws(&w, "</Id></Othr></Id></CdtrAcct>\n");

    ws(&w, "      <InstdAmt Ccy=\"");
    ws_esc(&w, m->ccy);
    ws(&w, "\">");
    w_amount(&w, m->amount_units, m->amount_frac);
    ws(&w, "</InstdAmt>\n");

    ws(&w, "    </CdtTrfTxInf>\n");
    ws(&w, "  </FIToFICstmrCdtTrf>\n");
    ws(&w, "</Document>\n");

    if (cap > 0) out[w.len] = '\0';
    return w.trunc ? ISO_ERR_TRUNC : (int32_t) w.len;
}

/* ===== CAMT.053 ===== */

static const char *sts_code(iso_entry_sts_t s)
{
    switch (s) {
    case ISO_STS_PDNG:
        return "PDNG";
    case ISO_STS_INFO:
        return "INFO";
    case ISO_STS_BOOK: /* fall through */
    default:
        return "BOOK";
    }
}

/* Emit one <Bal> with a code, magnitude amount, and sign-derived CdtDbtInd. */
static void w_balance(iso_w *w, const char *tp_code, const char *ccy, int64_t units, uint8_t frac)
{
    ws(w, "      <Bal><Tp><CdOrPrtry><Cd>");
    ws(w, tp_code);
    ws(w, "</Cd></CdOrPrtry></Tp><Amt Ccy=\"");
    ws_esc(w, ccy);
    ws(w, "\">");
    w_amount(w, units, frac);
    ws(w, "</Amt><CdtDbtInd>");
    ws(w, (units < 0) ? "DBIT" : "CRDT");
    ws(w, "</CdtDbtInd></Bal>\n");
}

int32_t iso20022_camt053_build(const camt053_t *m, char *out, uint32_t cap)
{
    if (!m || !out) return ISO_ERR_NULL;
    iso_w w = {out, cap, 0, false};

    uint32_t n = m->nb_entries;
    if (n > ISO_CAMT_MAX_ENTRIES) n = ISO_CAMT_MAX_ENTRIES;

    ws(&w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    ws(&w, "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:camt.053.001.08\">\n");
    ws(&w, "  <BkToCstmrStmt>\n");

    ws(&w, "    <GrpHdr><MsgId>");
    ws_esc(&w, m->msg_id);
    ws(&w, "</MsgId><CreDtTm>");
    ws_esc(&w, m->cre_dt_tm);
    ws(&w, "</CreDtTm></GrpHdr>\n");

    ws(&w, "    <Stmt>\n");
    ws(&w, "      <Id>");
    ws_esc(&w, m->msg_id);
    ws(&w, "</Id>\n");

    ws(&w, "      <Acct><Id><Othr><Id>");
    ws_esc(&w, m->acct_id);
    ws(&w, "</Id></Othr></Id><Ccy>");
    ws_esc(&w, m->ccy);
    ws(&w, "</Ccy></Acct>\n");

    /* Opening (OPBD) and closing (CLBD) balances. */
    w_balance(&w, "OPBD", m->ccy, m->opening_units, m->opening_frac);
    w_balance(&w, "CLBD", m->ccy, m->closing_units, m->closing_frac);

    /* Entries. */
    for (uint32_t i = 0; i < n; i++) {
        const camt_entry_t *e = &m->entries[i];
        ws(&w, "      <Ntry>\n");
        ws(&w, "        <Amt Ccy=\"");
        ws_esc(&w, m->ccy);
        ws(&w, "\">");
        w_amount(&w, e->units, e->frac_digits);
        ws(&w, "</Amt>\n");
        ws(&w, "        <CdtDbtInd>");
        ws(&w, (e->cdt_dbt == ISO_DBIT) ? "DBIT" : "CRDT");
        ws(&w, "</CdtDbtInd>\n");
        ws(&w, "        <Sts><Cd>");
        ws(&w, sts_code(e->status));
        ws(&w, "</Cd></Sts>\n");
        ws(&w, "      </Ntry>\n");
        if (w.trunc) break;
    }

    ws(&w, "    </Stmt>\n");
    ws(&w, "  </BkToCstmrStmt>\n");
    ws(&w, "</Document>\n");

    if (cap > 0) out[w.len] = '\0';
    return w.trunc ? ISO_ERR_TRUNC : (int32_t) w.len;
}
