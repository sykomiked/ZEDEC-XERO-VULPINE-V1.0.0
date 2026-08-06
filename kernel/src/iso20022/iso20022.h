/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* iso20022.h — conventional-banking INTEROP for ZXV settlement.
 *
 * "Speaks fluent bank without forgetting whose ledger it is."
 *
 * ZXV settles on its own triple-ledger rails; a conventional bank does not.
 * This module EMITS ISO 20022 MX messages so the two can talk:
 *   - PACS.008 (FIToFICstmrCdtTrf) — FI-to-FI Customer Credit Transfer
 *   - CAMT.053 (BkToCstmrStmt)      — Bank-to-Customer Statement
 * built as deterministic, structurally well-formed XML/text with NO XML
 * library: every byte is written through a bounded, length-checked writer,
 * so a too-small caller buffer returns a truncation error and NEVER overruns.
 *
 * It also maps the triple-ledger three rails to ISO 4217 numeric currency
 * codes (matching vino_stores.h's VINO_ISO_* rail codes) — DEBIT=846,
 * CREDIT=888, EQUITY=999. 999 is the real ISO 4217 code XXX "no currency":
 * a conventional parser reads the equity rail as NO CURRENCY, not as live
 * equity. That is a deliberate, DOCUMENTED interop caveat, not an accident
 * (see iso20022_ccy_alpha / iso20022_ccy_caveat).
 *
 * OPS BOUNDARIES:
 *   - This SERIALIZES messages; it does NOT send them. The wire transport
 *     (SWIFT / RTGS / ISO 20022 network) is an ops boundary elsewhere.
 *   - Structural well-formedness only; full XSD schema validation is out of
 *     scope. A real deployment validates against the published schema.
 *   - Every message is built from SUPPLIED data — no amount, party, or id is
 *     ever invented.
 *
 * Freestanding: integer-only, fixed-size arrays, no libc, no allocation, no
 * floating point on target. Amounts are exact integer minor-units + a
 * decimal-place count, formatted deterministically (no float, no rounding).
 */
#ifndef ZXV_ISO20022_H
#define ZXV_ISO20022_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Result / error codes =====
 * The *_build functions return the number of bytes written (>= 0) on success,
 * or one of these negative codes. On truncation the output is still NUL-
 * terminated within `cap` and NOTHING is written past the buffer. */
#define ISO_ERR_NULL   (-1)   /* NULL message or output buffer               */
#define ISO_ERR_TRUNC  (-2)   /* output would not fit in cap — truncated     */

/* ===== The three triple-ledger rails, as ISO 4217 numeric codes =====
 * These MUST stay in lockstep with vino_stores.h (VINO_ISO_DEBIT/CREDIT/
 * EQUITY). They are duplicated here as plain macros rather than by including
 * vino_stores.h so this interop layer stays free of the whole ledger/vino/
 * edp_risk dependency chain. If those ever diverge, that is a bug. */
#define ISO_CCY_DEBIT   846u   /* asset/backing rail                          */
#define ISO_CCY_CREDIT  888u   /* claim/liability rail (Vino-internal numeric)*/
#define ISO_CCY_EQUITY  999u   /* live equity rail — ISO 4217 XXX "no currency"*/

typedef enum {
    ISO_RAIL_DEBIT  = 0,
    ISO_RAIL_CREDIT = 1,
    ISO_RAIL_EQUITY = 2
} iso_rail_t;

/* Rail -> ISO 4217 numeric code (846 / 888 / 999). Unknown rail -> 0. */
uint16_t iso20022_rail_ccy(iso_rail_t rail);

/* ISO 4217 numeric -> alpha-3 string.
 *   999 -> "XXX"  (real conventional code — but means "no currency": caveat)
 *   840 -> "USD", 978 -> "EUR", 356 -> "INR", ... (a table of real codes)
 *   846 / 888 -> "" (EMPTY): the Vino DEBIT/CREDIT rail numerics are NOT
 *     assigned conventional currencies; a bank parser will not recognise them.
 * Never returns NULL. Use iso20022_ccy_caveat() for the human-readable
 * interop warning that goes with a code. */
const char *iso20022_ccy_alpha(uint16_t code);

/* Human-readable interop caveat for a numeric code, or "" if the code is a
 * plain conventional currency with no caveat. Surfaces the 999->XXX trap and
 * the non-standard status of the 846/888 rail numerics. Never NULL. */
const char *iso20022_ccy_caveat(uint16_t code);

/* ===== Deterministic amount formatter =====
 * Renders a NON-NEGATIVE fixed-point decimal from exact integer minor units
 * and a decimal-place count. Sign is NOT emitted (ISO amounts are non-
 * negative; direction is carried by CdtDbtInd). abs(units) is used.
 *   units=123456, frac_digits=2 -> "1234.56"
 *   units=5,      frac_digits=2 -> "0.05"
 *   units=100,    frac_digits=0 -> "100"
 * Returns bytes written (excluding NUL), or ISO_ERR_TRUNC / ISO_ERR_NULL. */
int32_t iso20022_format_amount(char *out, uint32_t cap,
                               int64_t units, uint8_t frac_digits);

/* ===== Field sizes (bounded — no allocation) ===== */
#define ISO_MSGID_MAX     35
#define ISO_DTTM_MAX      32   /* e.g. "2026-08-05T12:00:00" (+optional zone) */
#define ISO_NAME_MAX      70
#define ISO_ACCT_MAX      35
#define ISO_CCY_MAX        4   /* 3 letters + NUL                             */
#define ISO_E2E_MAX       35
#define ISO_ACODE_MAX      5   /* 4-letter ISO code (CRDT/DBIT/BOOK/...) + NUL*/
#define ISO_CAMT_MAX_ENTRIES 32

/* ===== PACS.008 — FI-to-FI Customer Credit Transfer ===== */
typedef struct {
    char     msg_id[ISO_MSGID_MAX + 1];       /* GrpHdr/MsgId                 */
    char     cre_dt_tm[ISO_DTTM_MAX + 1];      /* GrpHdr/CreDtTm (SUPPLIED)   */
    uint32_t nb_of_txs;                        /* GrpHdr/NbOfTxs              */

    char     debtor_name[ISO_NAME_MAX + 1];
    char     debtor_acct[ISO_ACCT_MAX + 1];
    char     creditor_name[ISO_NAME_MAX + 1];
    char     creditor_acct[ISO_ACCT_MAX + 1];

    int64_t  amount_units;                     /* InstdAmt / IntrBkSttlmAmt   */
    uint8_t  amount_frac;                       /* decimal places             */
    char     ccy[ISO_CCY_MAX];                  /* alpha-3, e.g. "USD"         */

    char     end_to_end_id[ISO_E2E_MAX + 1];   /* PmtId/EndToEndId            */
} pacs008_t;

/* Serialize a PACS.008 MX document into `out` (cap bytes). Returns bytes
 * written (>=0) or a negative ISO_ERR_*. MsgDefIdr namespace carries
 * 'pacs.008.001.09'. */
int32_t iso20022_pacs008_build(const pacs008_t *msg, char *out, uint32_t cap);

/* ===== CAMT.053 — Bank-to-Customer Statement ===== */
typedef enum {
    ISO_CRDT = 0,   /* credit  -> "CRDT" */
    ISO_DBIT = 1    /* debit   -> "DBIT" */
} iso_cdtdbt_t;

typedef enum {
    ISO_STS_BOOK = 0,   /* "BOOK" — booked   */
    ISO_STS_PDNG = 1,   /* "PDNG" — pending  */
    ISO_STS_INFO = 2    /* "INFO" — informational */
} iso_entry_sts_t;

typedef struct {
    int64_t         units;
    uint8_t         frac_digits;
    iso_cdtdbt_t    cdt_dbt;
    iso_entry_sts_t status;
} camt_entry_t;

typedef struct {
    char     msg_id[ISO_MSGID_MAX + 1];
    char     cre_dt_tm[ISO_DTTM_MAX + 1];      /* SUPPLIED                    */
    char     acct_id[ISO_ACCT_MAX + 1];        /* Acct/Id/Othr/Id            */
    char     ccy[ISO_CCY_MAX];                  /* account currency, alpha-3  */

    int64_t  opening_units;                     /* OPBD balance               */
    uint8_t  opening_frac;
    int64_t  closing_units;                     /* CLBD balance               */
    uint8_t  closing_frac;

    camt_entry_t entries[ISO_CAMT_MAX_ENTRIES];
    uint32_t     nb_entries;                    /* clamped to MAX_ENTRIES     */
} camt053_t;

/* Serialize a CAMT.053 MX document. Returns bytes written (>=0) or a negative
 * ISO_ERR_*. MsgDefIdr namespace carries 'camt.053.001.08'. Balances render
 * their magnitude with a CdtDbtInd derived from sign (>=0 CRDT, else DBIT). */
int32_t iso20022_camt053_build(const camt053_t *msg, char *out, uint32_t cap);

#endif /* ZXV_ISO20022_H */
