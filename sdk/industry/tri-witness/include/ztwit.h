/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztwit.h — Tri-Witness: an offline, read-only replay of settlement
 * decisions, for regulators and auditors.
 *
 * Given a clearing house's exported record chain (Sutra-Clear, domain
 * "sutra-clear") and its ledger journal export ("ZSJ1"), a witness
 *
 *   - checks every record's hash, link, key and ML-DSA-87 signature on its
 *     own (not only zind_chain_verify's first failure) against the house
 *     keys it was told to trust;
 *   - reassembles each clearing-cycle record and parses it strictly;
 *   - re-runs the netting (the ABI 1.5 treaty engine, zxv_treaty_run with
 *     OP_NET, cross-checked against the plain sum) and the assurance fee
 *     (zxv_fee_assure_carry, zxv_fee_split) deterministically from the
 *     recorded instructions, with each participant's fee carry followed
 *     from cycle to cycle;
 *   - checks conservation (net positions sum to zero per currency), the
 *     postings the positions and fees require, every posting against the
 *     journal, every opening and closing balance, the journal digest, each
 *     operator decision's signature, replays of end-to-end ids, and the
 *     digests of the ISO 20022 output (re-rendered with zxv_msg_*);
 *   - writes a signed report (a zind_chain, domain "tri-witness/report"):
 *     one Forbidden record per discrepancy naming the cycle, the chain
 *     record, the field and the exact bytes, then a summary.
 *
 * READ-ONLY. ztw_create refuses a capability that can post to a ledger
 * (ZXV_CAP_LEDGER_POST): a witness never posts, and cannot. It needs
 * LEDGER_READ (to render ISO 20022 in its own scratch context) and
 * CRYPTO_SIGN (to check signatures and sign its report).
 *
 * A witness can show that the records are consistent with each other, with
 * the journal and with the arithmetic. It cannot show that the
 * instructions were authorised, that the journal is the whole ledger, or
 * that the house's key was not stolen. Integer code only (R11).
 * Kit API 0.1 (preview). */
#ifndef ZTWIT_H
#define ZTWIT_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZTW_VERSION_MAJOR 0
#define ZTW_VERSION_MINOR 1

/* Status codes (-4200..-4299). */
#define ZTW_E_DISCREPANCY (-4201) /* the replay found at least one discrepancy  */
#define ZTW_E_WRITABLE    (-4202) /* the cap can post: a witness is read-only    */
#define ZTW_E_NOTRUST     (-4203) /* no house key to trust was given             */

const char *ztw_status_name(int32_t status);

/* Finding codes. */
#define ZTW_F_IMPORT       1  /* the chain export cannot be read             */
#define ZTW_F_LINK         2  /* a record hash or its link is wrong           */
#define ZTW_F_SIGNATURE    3  /* a record signature does not verify           */
#define ZTW_F_UNTRUSTED    4  /* a record is signed by a key not trusted      */
#define ZTW_F_KIND         5  /* a record of a kind the house never writes    */
#define ZTW_F_FRAGMENT     6  /* cycle fragments out of order or incomplete   */
#define ZTW_F_FORMAT       7  /* a cycle record does not parse                */
#define ZTW_F_SEQUENCE     8  /* cycle numbers, house id or keys inconsistent */
#define ZTW_F_INSTRUCTION  9  /* an instruction is invalid or out of order    */
#define ZTW_F_REPLAY       10 /* an end-to-end id settles or returns twice    */
#define ZTW_F_HELD         11 /* a held group is malformed or also settled    */
#define ZTW_F_DECISION     12 /* an operator decision does not hold up        */
#define ZTW_F_NETTING      13 /* a net position differs from the replay       */
#define ZTW_F_CONSERVATION 14 /* positions do not sum to zero                 */
#define ZTW_F_FEE          15 /* gross, fee or carry out differs              */
#define ZTW_F_SPLIT        16 /* a fee split differs from zxv_fee_split       */
#define ZTW_F_CARRY        17 /* a carry in is not the last carry out         */
#define ZTW_F_POSTING      18 /* postings differ from what the replay needs   */
#define ZTW_F_JOURNAL      19 /* a posting is missing, different or foreign   */
#define ZTW_F_BALANCE      20 /* a balance differs from the journal           */
#define ZTW_F_JDIGEST      21 /* the journal digest differs                   */
#define ZTW_F_ISO          22 /* ISO 20022 output differs from a re-render    */
#define ZTW_F_JFORMAT      23 /* the journal export cannot be read            */

const char *ztw_finding_name(uint16_t code);

typedef struct ztw_witness ztw_witness;

/* A witness. The cap must hold LEDGER_READ and CRYPTO_SIGN and must NOT
 * hold LEDGER_POST (ZTW_E_WRITABLE). signer: the witness's own key, which
 * signs the report. Both are borrowed. */
int32_t ztw_create(const zxv_cap *cap, const zind_signer *signer, ztw_witness **out);
void ztw_destroy(ztw_witness *w);
/* Keys to trust: the house's record-signing keys and its operators' keys
 * (at most 8 each). */
int32_t ztw_trust_house(ztw_witness *w, const uint8_t pk[ZIND_PK_BYTES]);
int32_t ztw_trust_operator(ztw_witness *w, const uint8_t pk[ZIND_PK_BYTES]);

typedef struct {
    uint32_t records;      /* chain records read                       */
    uint32_t cycles;       /* clearing cycles replayed                 */
    uint32_t instructions; /* instructions re-netted                   */
    uint32_t postings;     /* cycle postings checked against the journal */
    uint32_t findings;     /* discrepancies (each Forbidden)           */
    uint64_t journal_postings;  /* postings in the journal export      */
    uint64_t covered_postings;  /* of those, covered by a cycle's digest */
    uint8_t chain_digest[32];   /* SHA3-256 of the chain export        */
    uint8_t journal_digest[32]; /* SHA3-256 of the journal export      */
} ztw_summary;

/* Replay everything. journal may be NULL (len 0): the journal checks are
 * then one ZTW_F_JFORMAT finding. Each call replaces the previous report.
 *   ZXV_OK             no discrepancy; the report ends in a TRUE summary
 *   ZTW_E_DISCREPANCY  at least one Forbidden finding; see ztw_finding_at
 *   ZTW_E_NOTRUST      no house key was trusted
 *   ZXV_E_NOMEM, ZXV_E_CAP (the witness's own cap stopped working) */
int32_t ztw_verify(ztw_witness *w, const uint8_t *chain, size_t chain_len,
                   const uint8_t *journal, size_t journal_len, ztw_summary *out);

typedef struct {
    uint16_t code;        /* ZTW_F_*                                      */
    uint32_t cycle_no;    /* 0 when not tied to a cycle                   */
    uint32_t record;      /* chain record index, UINT32_MAX for the journal */
    uint32_t item;        /* index within the section, UINT32_MAX if none */
    char field[48];       /* e.g. "fees[2].fee"                           */
    int64_t expected, found; /* the numbers, when the field is a number   */
    char detail[160];
    uint8_t raw[64];      /* the field's exact bytes in the cycle record  */
    uint8_t raw_len;
    uint8_t tag;          /* a Forbidden tag                              */
    uint8_t subject[32];  /* the hash of that chain record (or the journal digest) */
} ztw_finding;

/* The first ZTW_MAX_LISTED findings are listed (and signed into the
 * report); ztw_summary.findings counts them all. */
#define ZTW_MAX_LISTED 1024u
int32_t ztw_finding_count(const ztw_witness *w, uint32_t *out_count);
int32_t ztw_finding_at(const ztw_witness *w, uint32_t i, ztw_finding *out);

/* The signed report (borrowed; replaced by the next ztw_verify). Kinds: */
#define ZTW_REC_FINDING 1
#define ZTW_REC_SUMMARY 2
const zind_chain *ztw_report(const ztw_witness *w);

/* ===== Tools for auditors (pure: no witness, no capability) ===== */

/* The number of complete cycle records in a chain export (structure only;
 * nothing is verified). */
int32_t ztw_cycle_count(const uint8_t *chain, size_t len, uint32_t *out_count);
/* Reassemble cycle record k (0-based) from a chain export. R4-style buffer:
 * buf NULL asks the size (ZXV_E_BUFFER). */
int32_t ztw_cycle_blob(const uint8_t *chain, size_t len, uint32_t k, uint8_t *buf, size_t cap,
                       size_t *out_len);

/* Every field of a cycle record, as parsed: its name, where it is, how it
 * is encoded and whether the replay derives it (derived = 1: a change is
 * always a finding) or takes it as declared input (derived = 0: a message
 * id, a name, a label; a change is caught only by the signature).
 * ZXV_E_FORMAT if the record does not parse; ZXV_E_BUFFER (with *out_n) if
 * cap is too small. */
#define ZTW_FT_U8    1
#define ZTW_FT_U16   2
#define ZTW_FT_U32   3
#define ZTW_FT_U64   4
#define ZTW_FT_STR   5 /* u8 length, then the bytes; offset/len are the bytes */
#define ZTW_FT_BYTES 6
typedef struct {
    char name[48];
    uint32_t offset, len;
    uint8_t type, derived;
} ztw_field;
int32_t ztw_blob_fields(const uint8_t *blob, size_t len, ztw_field *out, size_t cap,
                        size_t *out_n);

#ifdef __cplusplus
}
#endif

#endif /* ZTWIT_H */
