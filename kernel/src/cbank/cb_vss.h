/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_vss.h — the VINO Sovereign Standard (VSS) receipt and conformance checks.
 *
 * Implements, from the VSS proposal (Volume II, Parts IX and X):
 *   VSSReceipt   RcptId, SysRef, RailDr, RailCr, Amt, EqtyCoord, BckgDscl
 *                (EFCT/ASPL, default ASPL), XwalkRef, AnchrRef, Sgntr.
 *   C1 Balance        sum of debit legs == sum of credit legs, exactly.
 *   C2 Receipt        exactly one complete receipt per transaction.
 *   C3 Reversibility  a published crosswalk maps statutory trial-balance
 *                     lines into VSS records and back out with tolerance 0
 *                     (US GAAP, IFRS, IPSAS with fund tags, AAOIFI with
 *                     profit sharing on the equity rail and no interest).
 *   C4 Disclosure     aspirational backing is never summed or presented as
 *                     effective; effective backing carries an attestation.
 *   C5 Integrity      identifiers pass their rail's check algorithm and fail
 *                     the other rails' — through caller hooks only (see
 *                     cb_vss_c5_hooks; kernel/src/cardnet/cn_check.h offers
 *                     cn_luhn_valid, cn_damm_valid, cn_verhoeff_valid).
 *   Levels  Bronze C1 C2 C5 / Silver C1-C3 C5 / Gold C1-C5 plus independent
 *           attestation of C4.
 *
 * Rails in this repository: DEBIT 555, CREDIT 777, EQUITY 888 (see
 * kernel/src/vino_stores/vino_stores.h). The proposal's Part V resolution
 * class (orphaned obligations) uses designator 811: it is a procedure record
 * (an authorised discharge with an audit receipt), never a rail of value and
 * never a way to void a living person's enforceable debt.
 *
 * HONEST LIMITS. VSS is a voluntary proposal by its author; it is not an
 * adopted accounting standard. Passing these checks is a self-test, not
 * certification, audit or regulatory recognition; Gold requires an
 * independent attestor this code cannot be. The crosswalk tables are a
 * small illustrative chart (a handful of line items per framework), not a
 * complete mapping of any framework's taxonomy, and were not reviewed by a
 * standard setter, a CPA or a Shari'ah board. Interest-bearing statutory
 * lines are refused rather than mapped, because this platform forbids usury;
 * an entity with such lines cannot pass C3 here. The VSSReceipt element is
 * not registered with the ISO 20022 Registration Authority.
 */
#ifndef ZXV_CB_VSS_H
#define ZXV_CB_VSS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../pay/pay_rails.h"                        /* the canonical rail numerics */
#define CB_RAIL_DEBIT           ZXV_RAIL_CODE_DEBIT  /* 555 */
#define CB_RAIL_CREDIT          ZXV_RAIL_CODE_CREDIT /* 777 */
#define CB_RAIL_EQUITY          ZXV_RAIL_CODE_EQUITY /* 888 */
#define CB_VSS_RESOLUTION_CLASS 811u                 /* procedure designator, not a rail */

#define CB_VSS_ID_LEN  36u  /* Max35Text */
#define CB_VSS_REF_LEN 141u /* Max140Text */
#define CB_VSS_SYS_LEN 65u  /* Max64Text */

typedef enum { CB_BCKG_ASPL = 0, CB_BCKG_EFCT = 1 } cb_bckg; /* zero value = ASPL */

typedef struct {
    char rcpt_id[CB_VSS_ID_LEN];
    char sys_ref[CB_VSS_SYS_LEN];
    uint16_t rail_dr, rail_cr;
    char ccy[4];                     /* ISO 4217 alpha; never VFV */
    uint64_t amt;                    /* minor units of ccy */
    uint64_t eqty_coord;             /* minor units of ccy */
    uint8_t bckg;                    /* cb_bckg, ASPL unless attested */
    char attest_ref[CB_VSS_REF_LEN]; /* required for EFCT; not an XML element */
    char xwalk_ref[CB_VSS_REF_LEN];  /* optional */
    char anchr_ref[CB_VSS_REF_LEN];  /* optional */
    char sgntr[CB_VSS_REF_LEN];      /* required, e.g. "ed25519:<base64>" */
    char tx_ref[CB_VSS_ID_LEN];      /* the transaction this receipt covers */
} cb_vss_receipt;

#define CB_VSS_OK         0
#define CB_VSS_E_NULL     (-1)
#define CB_VSS_E_RAIL     (-2)
#define CB_VSS_E_CCY      (-3)
#define CB_VSS_E_FIELD    (-4)
#define CB_VSS_E_BCKG     (-5) /* EFCT without attestation */
#define CB_VSS_E_BALANCE  (-6)
#define CB_VSS_E_RECEIPT  (-7)
#define CB_VSS_E_XWALK    (-8)
#define CB_VSS_E_USURY    (-9)
#define CB_VSS_E_FUND     (-10)
#define CB_VSS_E_CAP      (-11)
#define CB_VSS_E_MISMATCH (-12)
#define CB_VSS_E_NOHOOK   (-13)
#define CB_VSS_E_ID       (-14)
#define CB_VSS_E_LIVING   (-15) /* 811 refused for a living natural person */
#define CB_VSS_E_AUTH     (-16)

bool cb_vss_rail_valid(uint16_t rail);
/* Receipt completeness: required fields, lengths, rails 555/777/888 with
 * RailDr != RailCr, ISO 4217 Ccy, EFCT only with an attestation ref. */
int cb_vss_receipt_check(const cb_vss_receipt *r);

/* ===== C1 / C2 ===== */
#define CB_SIDE_DR 1u
#define CB_SIDE_CR 2u

typedef struct {
    uint16_t rail;
    uint8_t side;
    uint64_t amount;
} cb_vss_leg;

typedef struct {
    char tx_id[CB_VSS_ID_LEN];
    char ccy[4];
    uint16_t n_legs;
    const cb_vss_leg *legs;
} cb_vss_tx;

int cb_vss_c1(const cb_vss_tx *tx, uint32_t n, uint32_t *bad);
int cb_vss_c2(const cb_vss_tx *tx, uint32_t n, const cb_vss_receipt *rc, uint32_t nr,
              uint32_t *bad);

/* Append-only receipt log: there is deliberately no update or delete. */
#define CB_VSS_LOG_CAP 256u
typedef struct {
    uint32_t n;
    cb_vss_receipt r[CB_VSS_LOG_CAP];
} cb_vss_log;
int cb_vss_log_append(cb_vss_log *l, const cb_vss_receipt *r); /* checks + unique RcptId */

/* ===== C3 crosswalks ===== */
typedef enum { CB_FW_US_GAAP = 1, CB_FW_IFRS = 2, CB_FW_IPSAS = 3, CB_FW_AAOIFI = 4 } cb_framework;

#define CB_XW_FUND     0x01u /* IPSAS: fund/appropriation tag required */
#define CB_XW_PROFIT   0x02u /* AAOIFI profit sharing: equity rail */
#define CB_XW_INTEREST 0x04u /* interest construct: refused */

typedef struct {
    uint8_t fw;
    const char *code;
    const char *label;
    uint16_t rail;
    uint8_t flags;
} cb_xw_item;

extern const cb_xw_item cb_xw_table[];
extern const uint32_t cb_xw_count;
const char *cb_vss_xwalk_ref(cb_framework fw); /* e.g. "IFRS-XW-v1" */

#define CB_FUND_LEN 9u
typedef struct {
    char code[24];
    uint8_t side;
    uint64_t amount;
    char fund[CB_FUND_LEN];
} cb_tb_line;

typedef struct {
    uint16_t rail;
    uint8_t side;
    uint64_t amount;
    uint16_t xw; /* index into cb_xw_table */
    char fund[CB_FUND_LEN];
} cb_vss_rec;

int cb_vss_map_in(cb_framework fw, const cb_tb_line *tb, uint32_t n, cb_vss_rec *out, uint32_t cap,
                  uint32_t *bad);
int cb_vss_map_out(cb_framework fw, const cb_vss_rec *in, uint32_t n, cb_tb_line *out,
                   uint32_t cap);
/* Map in, map out, and require an exact line-by-line reconstruction of a
 * balanced trial balance. scratch_rec and scratch_tb must hold n entries. */
int cb_vss_c3(cb_framework fw, const cb_tb_line *tb, uint32_t n, cb_vss_rec *scratch_rec,
              cb_tb_line *scratch_tb, uint32_t *bad);

/* ===== C4 backing disclosure ===== */
typedef struct {
    char kind[24];
    char ccy[4];
    uint64_t amount;
    uint8_t label; /* cb_bckg */
    char attest_ref[CB_VSS_REF_LEN];
} cb_vss_backing;

typedef struct {
    uint64_t effective;
    uint64_t aspirational;
} cb_vss_backing_totals;

/* Totals for one currency, kept apart. Fails on a mislabelled claim. */
int cb_vss_backing_sum(const cb_vss_backing *b, uint32_t n, const char *ccy,
                       cb_vss_backing_totals *out);
/* Presented figures must equal the separate sums exactly. */
int cb_vss_c4(const cb_vss_backing *b, uint32_t n, const char *ccy,
              const cb_vss_backing_totals *presented, uint32_t *bad);

/* ===== C5 identifier integrity (hooks only) ===== */
typedef bool (*cb_vss_id_check_fn)(const char *digits);
typedef struct {
    cb_vss_id_check_fn debit;  /* 555: Luhn in the proposal (cn_luhn_valid) */
    cb_vss_id_check_fn credit; /* 777: Damm (cn_damm_valid) */
    cb_vss_id_check_fn equity; /* 888: Verhoeff (cn_verhoeff_valid) */
} cb_vss_c5_hooks;

typedef struct {
    char id[24];
    uint16_t rail;
} cb_vss_ident;

int cb_vss_c5(const cb_vss_c5_hooks *h, const cb_vss_ident *ids, uint32_t n, uint32_t *bad);

/* ===== Levels ===== */
typedef enum { CB_VSS_NOT_RUN = 0, CB_VSS_PASS = 1, CB_VSS_FAIL = 2 } cb_vss_outcome;
typedef enum {
    CB_VSS_NONE = 0,
    CB_VSS_BRONZE = 1,
    CB_VSS_SILVER = 2,
    CB_VSS_GOLD = 3
} cb_vss_level_t;

typedef struct {
    uint8_t c1, c2, c3, c4, c5;       /* cb_vss_outcome */
    char c4_attestor[CB_VSS_REF_LEN]; /* independent attestation of C4 (Gold) */
    uint64_t run_at;                  /* a level claim must state this date */
} cb_vss_results;

cb_vss_level_t cb_vss_level(const cb_vss_results *r);
const char *cb_vss_level_name(cb_vss_level_t l);

/* ===== 811 resolution procedure record ===== */
typedef enum {
    CB_RES_DISSOLVED_ENTITY = 1,
    CB_RES_STATUTE_BARRED = 2,
    CB_RES_IRRECONCILABLE_LEGACY = 3,
    CB_RES_DEFUNCT_STATE = 4
} cb_res_basis;

typedef struct {
    uint16_t designator; /* must be 811 */
    char claim_id[CB_VSS_ID_LEN];
    uint8_t basis;
    bool debtor_living_person;
    char authority_ref[CB_VSS_REF_LEN];    /* who lawfully holds the power */
    char notice_ref[CB_VSS_REF_LEN];       /* public / party notice */
    char adjudication_ref[CB_VSS_REF_LEN]; /* decision of that authority */
    cb_vss_receipt receipt;                /* the permanent audit receipt */
} cb_vss_resolution;

int cb_vss_resolution_check(const cb_vss_resolution *r);

#endif /* ZXV_CB_VSS_H */
