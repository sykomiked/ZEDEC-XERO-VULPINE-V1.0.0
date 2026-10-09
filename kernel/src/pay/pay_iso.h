/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_iso.h — ISO 20022 MX messages for kernel/src/pay, built to validate
 * against the published base XSDs in kernel/src/pay/xsd (see PROVENANCE.txt)
 * and, in the CBPR profile, to the SWIFT CBPR+ usage-guideline restrictions
 * that are publicly documented.
 *
 * MESSAGES BUILT HERE
 *   head.001.001.02  Business Application Header (AppHdr)
 *   pacs.008.001.08  FI to FI customer credit transfer
 *   pain.001.001.09  customer credit transfer initiation
 *   pain.002.001.11  customer payment status report (CBPR+ uses .10; .10's
 *                    XSD could not be obtained, see xsd/PROVENANCE.txt)
 *   camt.052.001.08  account report
 *   camt.053.001.08  statement
 *   camt.054.001.08  debit/credit notification
 *   pacs.002/004/009 and camt.056/029 are owned by kernel/src/cbank and are
 *   deliberately NOT built here; pay_ledger_reverse is the ledger hook a
 *   pacs.004 return or a camt.056/029 cancellation resolves to.
 * PARSERS (strict, bounded, fuzzed): inbound pacs.008.001.08 and
 *   camt.053.001.08. (Inbound pacs.002 parsing belongs with cbank.)
 * The existing kernel/src/iso20022 module is kept as is; its pacs.008 output
 * is not schema-valid (see the report in test_pay.c) and pay_iso replaces it
 * for anything that leaves the platform.
 *
 * CBPR PROFILE RESTRICTIONS (enforced in C, beyond the base XSD)
 *   - character sets: identifiers in the SWIFT FIN "X" set
 *     [A-Za-z0-9/-?:().,'+ ] with no leading or trailing '/' and no "//";
 *     names, addresses and free text in the CBPR+ extended set (X plus
 *     !#$%&*=^_`{|}~";<>@[\]); no characters outside printable ASCII.
 *   - UETR mandatory on every pacs.008 transaction (UUIDv4, lowercase).
 *   - one transaction per pacs.008 (NbOfTxs = 1); SttlmMtd INDA or INGA;
 *     InstgAgt, InstdAgt and IntrBkSttlmDt present; agents by BICFI.
 *   - CreDtTm with an explicit UTC offset (Z or +hh:mm).
 *   - structured postal addresses: at least TwnNm and Ctry.
 *   - amounts with no more decimals than the currency's minor units and at
 *     most 18 total digits.
 *   These follow the public CBPR+ material as understood when written; the
 *   authoritative usage guidelines are on SWIFT MyStandards.
 *
 * VFV, CRYPTO AND THE NCR JURISDICTION (standards-correct handling)
 *   - VFV is a platform-internal, non-ISO unit (default alpha "VFV", numeric
 *     846, 2 minor units). It is NEVER written into a Ccy attribute or a Ccy
 *     element: the schema pattern [A-Z]{3} would accept "VFV", but ISO 4217
 *     does not, so doing so would be schema-valid and standards-wrong. Crypto
 *     assets and share assets are refused the same way. Builders accept an
 *     amount only for an asset flagged iso4217.
 *   - VFV transfers therefore stay internal (pay_ledger). When an external
 *     message must mention a VFV leg or the platform jurisdiction, it goes in
 *     proprietary free text, per message:
 *       pacs.008 / pain.001   CdtTrfTxInf/RmtInf/Strd/AddtlRmtInf
 *       camt.052/053/054      Ntry/NtryDtls/TxDtls/RmtInf/Strd/AddtlRmtInf
 *       pain.002              TxInfAndSts/StsRsnInf/AddtlInf
 *     as the tokens "/ZXV/JURIS/<code>" and "/ZXV/VFV/<amount>" (both inside
 *     the FIN X character set). SplmtryData is not used because CBPR+ does
 *     not allow it.
 *   - Country fields are ISO 3166-1 alpha-2 only. The platform jurisdiction
 *     ("NCR", New California Republic) is never emitted as a country, and
 *     "NC" (New Caledonia) is refused when it equals the first two letters
 *     of a non-ISO platform jurisdiction unless the address explicitly
 *     confirms it is a real New Caledonia address.
 *
 * HONEST LIMITS. Schema validity is not certification: passing the XSD and
 * these checks does not make a message acceptable to SWIFT, CIPS, SEPA or
 * FedNow, which have their own certification and testing. There is no SWIFT
 * or CIPS connectivity: these functions produce bytes and hand them to a
 * gateway callback (pay_roles.h). Operating as a bank or money transmitter
 * needs licences. VFV's store-credit classification is a legal question for
 * counsel. Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_PAY_ISO_H
#define ZXV_PAY_ISO_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_util.h"
#include "pay_ledger.h"

#define PAY_ISO_ERR_ARG     (-1)
#define PAY_ISO_ERR_TRUNC   (-2)
#define PAY_ISO_ERR_FIELD   (-3) /* a field failed a length/charset/format rule */
#define PAY_ISO_ERR_CCY     (-4) /* non-ISO-4217 asset in a currency field      */
#define PAY_ISO_ERR_COUNTRY (-5) /* not ISO 3166 alpha-2, or the jurisdiction   */
#define PAY_ISO_ERR_PROFILE (-6) /* a CBPR rule failed                         */
#define PAY_ISO_ERR_XML     (-7) /* own structural check failed (a bug)         */

typedef enum { PAY_ISO_BASE = 0, PAY_ISO_CBPR = 1 } pay_iso_profile_t;

typedef struct {
    pay_iso_profile_t profile;
    const pay_platform_t *platform; /* NULL = pay_platform_default */
} pay_iso_ctx_t;

/* ===== Building blocks ===== */
typedef struct {
    char strt[71];
    char bldg[17];
    char pst_cd[17];
    char twn[36];
    char ctry_sub[36];
    char ctry[3];
    bool ctry_confirmed; /* explicitly a real address in `ctry` (see NC rule) */
} pay_addr_t;

typedef struct {
    char name[141];
    bool has_adr;
    pay_addr_t adr;
    char lei[21]; /* optional: Id/OrgId/LEI */
} pay_party_t;

typedef struct {
    char bic[12];
    char lei[21];
    char clr_mmb[36];
    char name[141];
} pay_agent_t;

typedef struct {
    char iban[35];
    char othr[35];
    char othr_prtry[36]; /* SchmeNm/Prtry for Othr */
} pay_acct_t;

typedef struct {
    uint64_t units;
    char ccy[4];
    uint8_t minor;
    bool iso4217; /* set ONLY by pay_iso_amt_from_asset; builders refuse amounts without it */
} pay_amt_t;

/* Optional platform note carried in proprietary free text (see above). */
typedef struct {
    bool jurisdiction;  /* emit "/ZXV/JURIS/<platform jurisdiction>" */
    bool has_vfv;       /* emit "/ZXV/VFV/<amount>"                  */
    uint64_t vfv_units; /* VFV minor units of the internal leg        */
} pay_note_t;

/* Fill an amount from a ledger asset. Refuses (PAY_ISO_ERR_CCY) any asset
 * not flagged iso4217: VFV, crypto, shares, units. */
int32_t pay_iso_amt_from_asset(const pay_asset_t *a, uint64_t units, pay_amt_t *out);

/* ===== head.001.001.02 ===== */
typedef struct {
    char fr_bic[12];
    char to_bic[12];
    char biz_msg_idr[36];
    char msg_def_idr[36]; /* e.g. "pacs.008.001.08" */
    char biz_svc[36];     /* e.g. "swift.cbprplus.02" (CBPR: mandatory) */
    char cre_dt[36];
    bool pssbl_dplct;
} pay_bah_t;
int32_t pay_iso_bah(const pay_iso_ctx_t *x, const pay_bah_t *m, char *out, uint32_t cap);

/* ===== pacs.008.001.08 ===== */
#define PAY_ISO_MAX_TX 4u
typedef struct {
    char instr_id[36];
    char e2e[36];
    char tx_id[36];
    char uetr[PAY_UETR_LEN + 1];
    pay_amt_t amt;
    char chrg_br[5]; /* DEBT CRED SHAR SLEV */
    pay_party_t dbtr;
    pay_acct_t dbtr_acct;
    pay_agent_t dbtr_agt;
    pay_agent_t cdtr_agt;
    pay_party_t cdtr;
    pay_acct_t cdtr_acct;
    char ustrd[141];
    pay_note_t note;
} pay_ct_tx_t;

typedef struct {
    char msg_id[36];
    char cre_dt_tm[36];
    char sttlm_mtd[5]; /* INDA INGA CLRG COVE */
    char intr_bk_sttlm_dt[11];
    pay_agent_t instg_agt;
    pay_agent_t instd_agt;
    pay_ct_tx_t tx[PAY_ISO_MAX_TX];
    uint32_t n_tx;
} pay_pacs008_t;
int32_t pay_iso_pacs008(const pay_iso_ctx_t *x, const pay_pacs008_t *m, char *out, uint32_t cap);

/* ===== pain.001.001.09 ===== */
typedef struct {
    char msg_id[36];
    char cre_dt_tm[36];
    pay_party_t initg_pty;
    char pmt_inf_id[36];
    char reqd_exctn_dt[11];
    pay_party_t dbtr;
    pay_acct_t dbtr_acct;
    pay_agent_t dbtr_agt;
    pay_ct_tx_t tx[PAY_ISO_MAX_TX]; /* uses instr_id, e2e, uetr, amt, cdtr*, ustrd, note */
    uint32_t n_tx;
} pay_pain001_t;
int32_t pay_iso_pain001(const pay_iso_ctx_t *x, const pay_pain001_t *m, char *out, uint32_t cap);

/* ===== pain.002.001.11 ===== */
typedef struct {
    char orgnl_instr_id[36];
    char orgnl_e2e[36];
    char orgnl_uetr[PAY_UETR_LEN + 1];
    char sts[5];    /* ACCP ACSC ACSP ACTC ACWC PDNG RJCT ... */
    char rsn_cd[5]; /* ExternalStatusReason1Code, e.g. AC04, or "" */
    pay_note_t note;
} pay_sts_tx_t;

typedef struct {
    char msg_id[36];
    char cre_dt_tm[36];
    char orgnl_msg_id[36];
    char orgnl_msg_nm_id[36]; /* "pain.001.001.09" */
    char grp_sts[5];          /* optional */
    char orgnl_pmt_inf_id[36];
    pay_sts_tx_t tx[PAY_ISO_MAX_TX];
    uint32_t n_tx;
} pay_pain002_t;
int32_t pay_iso_pain002(const pay_iso_ctx_t *x, const pay_pain002_t *m, char *out, uint32_t cap);

/* ===== camt.052 / camt.053 / camt.054 (.001.08) ===== */
#define PAY_ISO_MAX_BAL  4u
#define PAY_ISO_MAX_NTRY 16u
typedef enum { PAY_CAMT_052 = 52, PAY_CAMT_053 = 53, PAY_CAMT_054 = 54 } pay_camt_kind_t;

typedef struct {
    char code[5]; /* OPBD CLBD ITBD CLAV ... */
    pay_amt_t amt;
    bool debit; /* balance is a debit (CdtDbtInd DBIT) */
    char dt[11];
} pay_bal_t;

typedef struct {
    char ntry_ref[36];
    pay_amt_t amt;
    bool debit;
    char sts[5]; /* BOOK PDNG INFO */
    char bookg_dt[11];
    char val_dt[11];
    char acct_svcr_ref[36];
    char e2e[36];
    char uetr[PAY_UETR_LEN + 1];
    char ustrd[141];
    pay_note_t note;
} pay_ntry_t;

typedef struct {
    pay_camt_kind_t kind;
    char msg_id[36];
    char cre_dt_tm[36];
    char rpt_id[36];
    pay_acct_t acct;
    char acct_ccy[4];
    pay_bal_t bal[PAY_ISO_MAX_BAL];
    uint32_t n_bal;
    pay_ntry_t ntry[PAY_ISO_MAX_NTRY];
    uint32_t n_ntry;
} pay_camt_t;
int32_t pay_iso_camt(const pay_iso_ctx_t *x, const pay_camt_t *m, char *out, uint32_t cap);

/* ===== Strict inbound parsers ===== */
#define PAY_ISO_IN_MAX 262144u /* largest accepted document, bytes */

typedef struct {
    uint64_t units;
    uint8_t frac; /* decimals present in the text */
    char ccy[4];
} pay_in_amt_t;

typedef struct {
    char instr_id[36], e2e[36], tx_id[36], uetr[PAY_UETR_LEN + 1];
    pay_in_amt_t amt;
    char intr_bk_sttlm_dt[11];
    char chrg_br[5];
    char dbtr_name[141], cdtr_name[141];
    char dbtr_acct[35], cdtr_acct[35];
    char dbtr_agt_bic[12], cdtr_agt_bic[12];
    char ustrd[141];
} pay_pacs008_in_tx_t;

typedef struct {
    char msg_id[36];
    char cre_dt_tm[36];
    uint32_t nb_of_txs;
    char sttlm_mtd[5];
    pay_pacs008_in_tx_t tx[PAY_ISO_MAX_TX];
    uint32_t n_tx;
} pay_pacs008_in_t;

typedef struct {
    char code[5];
    pay_in_amt_t amt;
    bool debit;
    char dt[11];
} pay_in_bal_t;

typedef struct {
    pay_in_amt_t amt;
    bool debit;
    char sts[5];
    char e2e[36];
    char uetr[PAY_UETR_LEN + 1];
} pay_in_ntry_t;

typedef struct {
    char msg_id[36];
    char cre_dt_tm[36];
    char stmt_id[36];
    char acct_id[35];
    char acct_ccy[4];
    pay_in_bal_t bal[PAY_ISO_MAX_BAL];
    uint32_t n_bal;
    pay_in_ntry_t ntry[PAY_ISO_MAX_NTRY];
    uint32_t n_ntry;
} pay_camt053_in_t;

/* Parse; returns 0 or a negative PAY_ISO_ERR_*. Never reads past `len`. */
int32_t pay_iso_parse_pacs008(const char *xml, uint32_t len, pay_pacs008_in_t *out);
int32_t pay_iso_parse_camt053(const char *xml, uint32_t len, pay_camt053_in_t *out);

/* Own structural check: well-formed, ASCII, no DTD/PI/comments/CDATA,
 * balanced, a single root named `root` with xmlns == `ns`. */
int32_t pay_iso_check_xml(const char *xml, uint32_t len, const char *root, const char *ns);

/* Field rules, exported for tests. */
bool pay_iso_id_ok(const pay_iso_ctx_t *x, const char *s, uint32_t max);
bool pay_iso_text_ok(const pay_iso_ctx_t *x, const char *s, uint32_t max);
bool pay_iso_datetime_ok(const pay_iso_ctx_t *x, const char *s);
bool pay_iso_date_ok(const char *s);
int32_t pay_iso_country_ok(const pay_iso_ctx_t *x, const char *cc, bool confirmed);

#endif /* ZXV_PAY_ISO_H */
