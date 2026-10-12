/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvtriage.h — vinea-triage: store-and-forward triage records for first
 * responders on intermittent links.
 *
 *   records   a triage record: the patient's tag ID, a START or SALT
 *             category, integer vitals each with a fixed unit and a
 *             plausible range, the observation time, a bounded note, and
 *             optionally the hash of the record it updates
 *   outbox    on the responder's device: records are numbered and packed
 *             into bundles; each bundle is signed with ML-DSA-87 and hash
 *             chained to the responder's previous bundle; unacknowledged
 *             bundles are kept for resending
 *   station   receives bundles in any order, any number of times:
 *             duplicates are dropped, a bundle ahead of a gap waits for the
 *             missing ones, two different bundles with the same number
 *             from one responder (a fork) hold that responder as BOTH
 *   patients  every patient's current picture is the set of records not
 *             superseded by a later one. Records from two responders that
 *             disagree on the category and do not build on each other are
 *             held as BOTH for a person at the station, NEVER merged: a
 *             registered reviewer signs which category stands
 *   acks      the station signs what it holds; the outbox drops only what
 *             a valid acknowledgement covers
 *
 * PRIVACY: libzxv exposes ML-KEM-1024 but no public symmetric cipher, and
 * this kit does not build one. Bundles are therefore NOT encrypted: they
 * are signed (integrity, origin) but readable by anyone who sees them.
 * Encryption must be provided by the transport (README "Limits"). A bundle
 * that claims an encrypted payload is refused (ZXV_E_NOTIMPL).
 *
 * Advisory: not a medical device, not HIPAA-certified; a category is what
 * a responder recorded, not a diagnosis. Integer code only (R11). Kit API
 * 0.1 (preview). */
#ifndef ZVTRIAGE_H
#define ZVTRIAGE_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZVT_VERSION_MAJOR 0
#define ZVT_VERSION_MINOR 1

/* Status codes (-3600..-3699). */
#define ZVT_E_RESPONDER (-3601) /* unknown responder, or not that responder's key       */
#define ZVT_E_GAP       (-3602) /* bundle ahead of a gap: kept until the gap is filled  */
#define ZVT_E_FORK      (-3603) /* two different bundles with one number: responder held */
#define ZVT_E_RECORD    (-3604) /* a record is invalid (category, vital, unit, note)    */
#define ZVT_E_PATIENT   (-3605) /* unknown patient                                     */
#define ZVT_E_REVIEWER  (-3606) /* not a registered reviewer, or not that reviewer's key */

const char *zvt_status_name(int32_t status);

/* Triage systems and categories (the category is a field, not a decision
 * the kit makes). EXPECTANT exists only in SALT. */
#define ZVT_SYS_START 1
#define ZVT_SYS_SALT  2

#define ZVT_CAT_MINOR     1 /* green  */
#define ZVT_CAT_DELAYED   2 /* yellow */
#define ZVT_CAT_IMMEDIATE 3 /* red    */
#define ZVT_CAT_EXPECTANT 4 /* gray (SALT only) */
#define ZVT_CAT_DEAD      5 /* black  */

/* Vitals: each kind has one unit and a plausible range; anything else is
 * ZVT_E_RECORD. Values are integers in that unit. */
#define ZVT_V_RESP      1 /* respiratory rate, ZVT_U_PER_MIN, 0..150      */
#define ZVT_V_PULSE     2 /* pulse, ZVT_U_PER_MIN, 0..350                  */
#define ZVT_V_SBP       3 /* systolic pressure, ZVT_U_MMHG, 0..400         */
#define ZVT_V_SPO2      4 /* oxygen saturation, ZVT_U_PERCENT, 0..100      */
#define ZVT_V_CAPREFILL 5 /* capillary refill, ZVT_U_MS, 0..20000          */
#define ZVT_V_GCS       6 /* Glasgow coma scale, ZVT_U_SCORE, 3..15        */
#define ZVT_V_TEMP      7 /* temperature, ZVT_U_DECI_C (371 = 37.1 C), 150..460 */

#define ZVT_U_PER_MIN 1
#define ZVT_U_MMHG    2
#define ZVT_U_PERCENT 3
#define ZVT_U_MS      4
#define ZVT_U_SCORE   5
#define ZVT_U_DECI_C  6

#define ZVT_ID_MAX         32u  /* patient, responder and reviewer IDs: [A-Za-z0-9._:/-] */
#define ZVT_VITALS_MAX     8u
#define ZVT_NOTE_MAX       200u /* UTF-8 bytes, no control characters */
#define ZVT_BUNDLE_RECORDS 32u  /* records per bundle                 */
#define ZVT_BUNDLE_MAX     16384u /* bundle bytes before the signature */
#define ZVT_MAX_RESPONDERS 256u
#define ZVT_MAX_PATIENTS   65536u
#define ZVT_MAX_RECORDS    65536u
#define ZVT_PENDING_MAX    32u  /* out-of-order bundles held per responder */
#define ZVT_OUTBOX_MAX     256u /* unacknowledged bundles on a device      */

#define ZVT_BUNDLE_CTX  "vinea-triage/bundle"
#define ZVT_ACK_CTX     "vinea-triage/ack"
#define ZVT_RESOLVE_CTX "vinea-triage/resolve"
#define ZVT_JOURNAL     "vinea-triage/station" /* the station's zind_chain domain */

typedef struct {
    uint8_t kind, unit;
    int32_t value;
} zvt_vital;

typedef struct {
    char patient[ZVT_ID_MAX + 1];
    uint8_t system;   /* ZVT_SYS_*  */
    uint8_t category; /* ZVT_CAT_*  */
    uint64_t observed; /* the device's clock, unix seconds */
    uint8_t based_on[32]; /* the record this one updates, or all zeros */
    uint8_t n_vitals;
    zvt_vital vitals[ZVT_VITALS_MAX]; /* distinct kinds */
    char note[ZVT_NOTE_MAX + 1];
} zvt_record;

/* ZXV_OK or ZVT_E_RECORD (with the reason in zind_last_error). */
int32_t zvt_record_check(const zvt_record *r);

/* ===== Outbox (the responder's device) ===== */
typedef struct zvt_outbox zvt_outbox;

/* The cap needs CRYPTO_SIGN; `key` is borrowed and must outlive the outbox. */
int32_t zvt_outbox_create(const zxv_cap *cap, const char *responder, const zind_signer *key,
                          zvt_outbox **out);
void zvt_outbox_destroy(zvt_outbox *o);
/* Validate, number and stage a record. out_hash (may be NULL): the record's
 * hash, which a later record may name in based_on. */
int32_t zvt_outbox_record(zvt_outbox *o, const zvt_record *r, uint8_t out_hash[32]);
/* Pack staged records (up to ZVT_BUNDLE_RECORDS and ZVT_BUNDLE_MAX bytes)
 * into the next bundle, chain and sign it. ZXV_E_STATE if nothing is
 * staged, ZXV_E_CAPACITY with ZVT_OUTBOX_MAX bundles unacknowledged. */
int32_t zvt_outbox_seal(zvt_outbox *o, uint64_t now, uint64_t *out_seq);
int32_t zvt_outbox_staged(const zvt_outbox *o, uint32_t *out_count);
/* Unacknowledged bundles, oldest first; bundle i as bytes to send. */
int32_t zvt_outbox_pending(const zvt_outbox *o, uint32_t *out_count);
int32_t zvt_outbox_bundle(const zvt_outbox *o, uint32_t i, uint8_t *buf, size_t cap,
                          size_t *out_len);
/* Apply a station acknowledgement signed by station_pk: drop every bundle
 * it covers. ZXV_E_VERIFY if the signature fails, ZIND_E_CHAIN if the
 * station holds a different history for this responder. */
int32_t zvt_outbox_ack(zvt_outbox *o, const uint8_t station_pk[ZIND_PK_BYTES],
                       const uint8_t *ack, size_t len);

/* ===== Station (the receiving end) ===== */
typedef struct zvt_station zvt_station;

/* The cap needs CRYPTO_SIGN (and ATTEST with a quarantine log, which may be
 * NULL). The station key signs acknowledgements and the journal. */
int32_t zvt_station_create(const zxv_cap *cap, const zind_signer *station_key, zxv_hexlog *qlog,
                           zvt_station **out);
void zvt_station_destroy(zvt_station *s);
int32_t zvt_responder_add(zvt_station *s, const char *responder, const uint8_t pk[ZIND_PK_BYTES]);
int32_t zvt_reviewer_add(zvt_station *s, const char *reviewer, const uint8_t pk[ZIND_PK_BYTES]);

typedef struct {
    char responder[ZVT_ID_MAX + 1];
    uint64_t seq;          /* the bundle's number                         */
    uint32_t bundles;      /* bundles applied by this call (with drained ones) */
    uint32_t records;      /* records applied                             */
    uint32_t duplicates;   /* records already held, dropped               */
    uint32_t new_conflicts; /* patients that became BOTH                   */
} zvt_receipt;

/* Receive one bundle. ZXV_OK (applied, with any buffered bundles it
 * unblocked), ZIND_E_REPLAY (already held: dropped), ZVT_E_GAP (kept until
 * the missing bundles arrive), ZVT_E_FORK (conflicts with an accepted
 * bundle: the responder is held), ZIND_E_HELD (the responder is held),
 * ZIND_E_CHAIN (bad signature: logged), ZVT_E_RESPONDER, ZXV_E_FORMAT,
 * ZXV_E_NOTIMPL (an encrypted bundle), ZXV_E_CAPACITY. out may be NULL. */
int32_t zvt_station_receive(zvt_station *s, const uint8_t *buf, size_t len, zvt_receipt *out);

/* A signed acknowledgement of everything held from `responder`. */
int32_t zvt_station_ack(const zvt_station *s, const char *responder, uint8_t *buf, size_t cap,
                        size_t *out_len);

typedef struct {
    uint64_t next_seq;  /* bundles held in order          */
    uint32_t pending;   /* bundles waiting behind a gap   */
    int32_t lp;         /* TRUE, or BOTH once forked      */
} zvt_responder_view;
int32_t zvt_responder_state(const zvt_station *s, const char *responder, zvt_responder_view *out);

typedef struct {
    int32_t lp;          /* NEITHER (no record), TRUE (one category), BOTH (held) */
    uint8_t tag;         /* 64-state tag: base = lp; phase Potential (one record),
                            Emerging (updated), Manifest (a reviewer's decision stands) */
    uint8_t category;    /* when TRUE; 0 when BOTH                                */
    uint8_t categories;  /* bit (1 << category) for every category held           */
    uint32_t heads;      /* current records (and decisions) not superseded         */
    uint32_t records;    /* every record ever received for the patient            */
    int resolved;        /* the current picture is a reviewer's decision           */
} zvt_patient_view;

int32_t zvt_patient_count(const zvt_station *s, uint32_t *out_count);
/* The ID of patient i (in order of first record); buf >= ZVT_ID_MAX + 1. */
int32_t zvt_patient_id(const zvt_station *s, uint32_t i, char *buf, size_t cap);
int32_t zvt_patient_state(const zvt_station *s, const char *patient, zvt_patient_view *out);
/* Head i of a patient: its hash, category, and either the record and its
 * responder (out_is_decision = 0) or a reviewer's decision (1, rec left
 * untouched, responder = the reviewer). Any out pointer may be NULL. */
int32_t zvt_patient_head(const zvt_station *s, const char *patient, uint32_t i, zvt_record *rec,
                         char *responder, size_t responder_cap, uint8_t out_hash[32],
                         uint8_t *out_category, int *out_is_decision);

/* The bytes a reviewer signs (context ZVT_RESOLVE_CTX) to settle a held
 * patient: the patient, the hashes of every current head in order, the
 * category that stands and the reviewer's ID. ZXV_E_STATE unless BOTH. */
int32_t zvt_resolve_message(const zvt_station *s, const char *patient, uint8_t category,
                            const char *reviewer, uint8_t *buf, size_t cap, size_t *out_len);
/* Apply it (the station's cap needs ATTEST). A forged signature is
 * ZXV_E_VERIFY and is logged. The decision becomes the patient's single
 * head; later records that build on it (based_on = its hash, from
 * zvt_patient_head) replace it, others stand beside it. */
int32_t zvt_resolve(zvt_station *s, const char *patient, uint8_t category, const char *reviewer,
                    const uint8_t sig[ZIND_SIG_BYTES]);

/* The station's journal (domain ZVT_JOURNAL, signed by the station key):
 * kind 1 an applied bundle (responder, seq, bundle hash, counts), kind 2
 * a reviewer's decision (message, reviewer key id, signature), kind 3 a
 * fork (responder, seq, both bundle hashes). */
#define ZVT_J_BUNDLE  1
#define ZVT_J_RESOLVE 2
#define ZVT_J_FORK    3
const zind_chain *zvt_station_journal(const zvt_station *s);

/* Statistics: bundles applied, replays dropped, duplicate records dropped,
 * signatures refused. Any pointer may be NULL. */
int32_t zvt_station_stats(const zvt_station *s, uint64_t *bundles, uint64_t *replays,
                          uint64_t *dup_records, uint64_t *bad_sigs);

#ifdef __cplusplus
}
#endif

#endif /* ZVTRIAGE_H */
