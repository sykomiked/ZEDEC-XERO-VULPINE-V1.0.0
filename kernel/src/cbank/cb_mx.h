/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_mx.h — bounded ISO 20022 writers for the cb_net payment flow:
 *   pacs.008.001.08 FIToFICstmrCdtTrf  customer credit transfer
 *   pacs.009.001.08 FICdtTrf           FI credit transfer (also net settlement)
 *   pacs.002.001.10 FIToFIPmtStsRpt    payment status (ACSP/ACSC/RJCT...)
 *   pacs.004.001.09 PmtRtr             payment return
 *   camt.056.001.08 FIToFIPmtCxlReq    cancellation request
 *   camt.029.001.09 RsltnOfInvstgtn    resolution of the cancellation
 * Each writer returns the number of bytes written, or a negative CB_MX_E_*
 * code; output never overruns `cap` and is always NUL-terminated.
 *
 * Rules enforced by every writer:
 *   - Ccy is always an active, payable ISO 4217 alphabetic code; amounts are
 *     exact minor units printed with the ISO minor-unit count.
 *   - Vino rail numerics (555/777/888) appear ONLY inside the VSSReceipt in
 *     SplmtryData/Envlp, never as a currency or anywhere else; VFV is never
 *     given a country, and no rail jurisdiction (NCR, NRE, PNS) or its
 *     look-alike ("NC") is ever one: country codes written are the ISO
 *     3166-1 codes of real parties and are validated.
 *   - BICs follow the BICFIDec2014 pattern; UETRs the UUIDv4 pattern.
 * The VSSReceipt is written in namespace urn:zedec:vss:receipt:1 so a
 * VSS-unaware parser skips it (the Envlp content model is xs:any lax).
 *
 * cbank writes its own messages rather than reusing kernel/src/pay, whose
 * writer was still being built when this module was written.
 *
 * HONEST LIMITS. Output is checked against the base ISO 20022 XSDs (the
 * copies in kernel/src/pay/xsd) by the cbank verify step; it is NOT checked
 * against SWIFT CBPR+, PAPSS or any market-infrastructure usage guideline,
 * which add restrictions this code does not know. These are serialisers:
 * nothing is sent anywhere, there is no SWIFT, CIPS, PAPSS or RTGS
 * connection, and schema validity is not certification. The VSSReceipt
 * block is unregistered with the ISO 20022 Registration Authority.
 */
#ifndef ZXV_CB_MX_H
#define ZXV_CB_MX_H

#include <stdint.h>
#include <stdbool.h>
#include "cb_vss.h"

#define CB_MX_E_NULL  (-1)
#define CB_MX_E_TRUNC (-2)
#define CB_MX_E_CCY   (-3)
#define CB_MX_E_ID    (-4)
#define CB_MX_E_BIC   (-5)
#define CB_MX_E_CTRY  (-6)
#define CB_MX_E_UETR  (-7)
#define CB_MX_E_VSS   (-8)
#define CB_MX_E_AMT   (-9)
#define CB_MX_E_CODE  (-10)
#define CB_MX_E_TEXT  (-11)

typedef struct {
    const char *msg_id;
    uint64_t cre_dt;       /* Unix seconds UTC */
    const char *instg_agt; /* BIC, optional */
    const char *instd_agt; /* BIC, optional */
    const char *sttlm_mtd; /* "CLRG", "INDA", "INGA", "COVE" */
    const char *clr_sys;   /* proprietary clearing system name, optional */
} cb_mx_hdr;

typedef struct {
    const char *name; /* Max140Text, optional */
    const char *ctry; /* ISO 3166-1 alpha-2, optional */
    const char *acct; /* account id (Othr/Id), optional */
} cb_mx_party;

typedef struct {
    const char *ccy;
    uint64_t units;
} cb_mx_amt;

typedef struct {
    cb_mx_hdr h;
    const char *instr_id; /* optional */
    const char *e2e_id;
    const char *uetr; /* optional */
    cb_mx_amt amt;
    uint64_t sttlm_dt;   /* 0 = omit */
    const char *chrg_br; /* "SLEV", "SHAR", "DEBT", "CRED" */
    cb_mx_party dbtr;
    const char *dbtr_agt;
    const char *cdtr_agt;
    cb_mx_party cdtr;
    const char *purpose;       /* ISO external purpose code, optional */
    const cb_vss_receipt *vss; /* optional */
} cb_mx_pacs008;

typedef struct {
    cb_mx_hdr h;
    const char *instr_id;
    const char *e2e_id;
    const char *uetr;
    cb_mx_amt amt;
    uint64_t sttlm_dt;
    const char *dbtr; /* debtor FI BIC */
    const char *cdtr; /* creditor FI BIC */
    const cb_vss_receipt *vss;
} cb_mx_pacs009;

typedef struct {
    cb_mx_hdr h;
    const char *orgnl_msg_id;
    const char *orgnl_msg_nm_id; /* e.g. "pacs.008.001.08" */
    const char *orgnl_instr_id;
    const char *orgnl_e2e_id;
    const char *orgnl_uetr;
    const char *tx_sts; /* ACSP, ACSC, RJCT, CANC, PDNG ... */
    const char *rsn_cd; /* status reason, optional (required for RJCT) */
    const char *addtl_inf;
    uint64_t accptnc_dt; /* 0 = omit */
} cb_mx_pacs002;

typedef struct {
    cb_mx_hdr h;
    const char *rtr_id;
    const char *orgnl_msg_id;
    const char *orgnl_msg_nm_id;
    const char *orgnl_e2e_id;
    const char *orgnl_uetr;
    cb_mx_amt orgnl_amt;
    cb_mx_amt rtrd_amt;
    uint64_t sttlm_dt;
    const char *chrg_br; /* optional */
    const char *rsn_cd;  /* ISO return reason, e.g. AC04, AM05, CUST, FOCR */
    const char *addtl_inf;
    const cb_vss_receipt *vss;
} cb_mx_pacs004;

typedef struct {
    const char *assgnmt_id;
    const char *assgnr; /* BIC */
    const char *assgne; /* BIC */
    uint64_t cre_dt;
    const char *case_id;
    const char *case_cretr; /* BIC */
    const char *orgnl_msg_id;
    const char *orgnl_msg_nm_id;
    const char *orgnl_e2e_id;
    const char *orgnl_uetr;
    cb_mx_amt orgnl_amt;
    uint64_t orgnl_sttlm_dt;
    const char *rsn_cd; /* DUPL, CUST, FRAD, TECH, UPAY, AGNT ... */
    const char *addtl_inf;
} cb_mx_camt056;

typedef struct {
    const char *assgnmt_id;
    const char *assgnr;
    const char *assgne;
    uint64_t cre_dt;
    const char *case_id;
    const char *case_cretr;
    bool accepted; /* Conf CNCL + TxCxlSts ACCR, else RJCR + RJCR */
    const char *orgnl_msg_id;
    const char *orgnl_msg_nm_id;
    const char *orgnl_e2e_id;
    const char *orgnl_uetr;
    const char *rsn_cd; /* rejection reason when !accepted: ARDT, AGNT, NOOR, AM04 ... */
    const char *addtl_inf;
} cb_mx_camt029;

int32_t cb_mx_pacs008_write(const cb_mx_pacs008 *m, char *buf, uint32_t cap);
int32_t cb_mx_pacs009_write(const cb_mx_pacs009 *m, char *buf, uint32_t cap);
int32_t cb_mx_pacs002_write(const cb_mx_pacs002 *m, char *buf, uint32_t cap);
int32_t cb_mx_pacs004_write(const cb_mx_pacs004 *m, char *buf, uint32_t cap);
int32_t cb_mx_camt056_write(const cb_mx_camt056 *m, char *buf, uint32_t cap);
int32_t cb_mx_camt029_write(const cb_mx_camt029 *m, char *buf, uint32_t cap);

/* The VSSReceipt fragment alone, wrapped in <SplmtryData><Envlp>. */
int32_t cb_mx_vss_write(const cb_vss_receipt *r, char *buf, uint32_t cap);

#endif /* ZXV_CB_MX_H */
