/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zadapter.h — adapter-matrix: ingest external records and label each one
 * with a lifecycle / trust state (kit API 0.2).
 *
 * An adapter turns an inbound payload into a RECORD: a flat list of named
 * fields (exact rationals, text, booleans, nulls) plus a label:
 *
 *   accepted, unverified   ZXV_LP_NEITHER  tag neither/neutral/emerging
 *   refused                ZXV_LP_FALSE    tag false/neutral/emerging,
 *                                          with one or more reasons
 *   attested by the host   ZXV_LP_TRUE / FALSE / BOTH (za_record_attest,
 *                                          needs ZXV_CAP_ATTEST), tag
 *                                          <state>/neutral/manifest
 *
 * Adapters (this version):
 *   ZA_SRC_JSON      a JSON document, e.g. an HTTP response body. Parsing
 *                    only: the kit never opens a connection.
 *   ZA_SRC_MQTT      an MQTT PUBLISH payload with its topic name: a JSON
 *                    document, an exact decimal, or UTF-8 text.
 *   ZA_SRC_FIX       a FIX tag=value message, SOH (0x01) delimited.
 *   ZA_SRC_FIX_PIPE  the same with '|' as the delimiter (logs, tests).
 *
 * Numbers are parsed EXACTLY from their decimal text into int64 rationals
 * ("0.1" is 1/10, "1.5e-3" is 3/2000); never through a float. A number
 * that has no exact int64 rational form (1e400, 1e-30) refuses the record:
 * nothing is rounded.
 *
 * Validation (za_validate) checks a record against a z-contract function
 * whose parameter names are field names; a failure refuses the record with
 * the contract's reason and appends evidence to a zc_evlog.
 *
 * HONEST LIMIT. An adapter LABELS data. It cannot make a bad source
 * accurate: a well-formed record that passes its contract is still only
 * "accepted, unverified" (NEITHER). Truth comes from the host attesting it
 * with its own capability, on its own evidence. See README.md.
 *
 * The tag is libzxv's 64-state tag (ABI 1.3, zxv_hex_*): zxv_hex_name or
 * zc_tag_format gives its text, za_record_action what libzxv says happens
 * next (accepted-unverified: WAIT, refused: REFUSE, attested TRUE:
 * PROCEED). To quarantine every validation refusal in a libzxv quarantine
 * log as a Forbidden record, attach z-contract's zc_hexsink to the
 * evidence log passed to za_validate (see README.md).
 *
 * KIT API STABILITY. 0.x. Calls only libzxv ABI 1.3 and z-contract 0.2.
 * Opaque handles, int32_t status returns, no floats. 0.2 (over 0.1): real
 * zxv_hex_* tags, attestation moves the tag by the libzxv rules
 * (zxv_hex_set, then zxv_hex_step to manifest), za_record_action.
 */
#ifndef ZADAPTER_H
#define ZADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "zcontract.h"
#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZA_VERSION_MAJOR 0
#define ZA_VERSION_MINOR 2
#define ZA_VERSION_PATCH 0

/* adapter-matrix status codes start at -1301. A malformed payload is NOT
 * an error: it gives a refused record. Errors are API misuse (ZXV_E_ARG),
 * memory (ZXV_E_NOMEM) and these: */
#define ZA_E_REFUSED (-1301) /* za_validate: the record is (now) refused     */

const char *za_status_name(int32_t status);
int32_t za_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Sources ===== */
#define ZA_SRC_JSON     1
#define ZA_SRC_MQTT     2
#define ZA_SRC_FIX      3
#define ZA_SRC_FIX_PIPE 4

/* ===== Limits ===== */
#define ZA_MAX_PAYLOAD (1024u * 1024u) /* bytes in one payload                 */
#define ZA_MAX_FIELDS  1024u           /* fields in one record                 */
#define ZA_MAX_DEPTH   32u             /* JSON nesting                         */
#define ZA_MAX_NAME    255u            /* bytes in a field name (a JSON path)  */
#define ZA_MAX_REASONS 16u             /* reasons kept per record              */

/* ===== Refusal reasons ===== */
#define ZA_R_MALFORMED   1  /* not well-formed for its format                   */
#define ZA_R_LIMIT       2  /* a size, depth, field or name limit               */
#define ZA_R_NUMBER      3  /* a number with no exact int64 rational form       */
#define ZA_R_DUPLICATE   4  /* a JSON path given twice (ambiguous)              */
#define ZA_R_UTF8        5  /* text that is not valid UTF-8                     */
#define ZA_R_TOPIC       6  /* an MQTT topic name that is not valid             */
#define ZA_R_FIX_HEADER  7  /* FIX: 8, 9, 35 not first, or 10 not last          */
#define ZA_R_FIX_LENGTH  8  /* FIX: BodyLength (9) does not match               */
#define ZA_R_FIX_CHECKSUM 9 /* FIX: CheckSum (10) does not match                */
#define ZA_R_UNSUPPORTED 10 /* valid in the format, outside this adapter (FIX   */
                            /* data fields such as RawData)                     */
#define ZA_R_MISSING     11 /* za_validate: a contract parameter has no field   */
#define ZA_R_TYPE        12 /* za_validate: a field cannot be that type         */
#define ZA_R_CONTRACT    13 /* za_validate: the contract refused the value      */

const char *za_reason_name(int32_t reason);

/* ===== Records ===== */
typedef struct za_record za_record;

/* Field kinds. */
#define ZA_F_NUMBER 1 /* exact rational (za_field_rat)                       */
#define ZA_F_TEXT   2 /* UTF-8 bytes (za_field_text)                         */
#define ZA_F_BOOL   3 /* JSON true/false (za_field_rat gives 1/1 or 0/1)      */
#define ZA_F_NULL   4 /* JSON null                                           */

/* Ingest one payload. `source` is a label the caller chooses (a URL, a
 * broker name); for ZA_SRC_MQTT it is the TOPIC NAME and is validated.
 * Returns ZXV_OK with a record whether the payload was accepted or
 * refused; check za_record_state. A record refused HERE keeps no fields
 * (they were never well-formed); one refused later by za_validate keeps
 * them, so the host can inspect what was refused.
 * ZXV_E_ARG for NULL arguments or an unknown kind, ZXV_E_NOMEM. */
int32_t za_ingest(int32_t kind, const char *source, const uint8_t *payload, size_t len,
                  za_record **out_record);
void za_record_destroy(za_record *record);

/* *out_lp_state: ZXV_LP_NEITHER (accepted, unverified), ZXV_LP_FALSE
 * (refused), or what the host attested. *out_tag: the libzxv 64-state tag
 * code (zxv_hex_name / zc_tag_format give its text). Either pointer may be
 * NULL. */
int32_t za_record_state(const za_record *record, int32_t *out_lp_state, int32_t *out_tag);
/* What libzxv says happens next for the record's tag: a ZXV_HEX_ACT_*
 * value (zxv_hex_action). Kit API 0.2. */
int32_t za_record_action(const za_record *record, int32_t *out_action);
/* ZA_SRC_*, and the source label (static to the record). For FIX, when
 * the caller passed "" or NULL, the label is "SenderCompID->TargetCompID"
 * taken from the message (both must be present). That label is what the
 * message CLAIMS; the kit cannot authenticate it. */
int32_t za_record_source(const za_record *record, int32_t *out_kind, const char **out_source);

int32_t za_record_reason_count(const za_record *record, uint32_t *out_count);
/* Reason i: a ZA_R_* code and a message (static to the record). */
int32_t za_record_reason(const za_record *record, uint32_t index, int32_t *out_reason,
                         const char **out_message);

int32_t za_record_field_count(const za_record *record, uint32_t *out_count);
/* Field i: its name (a JSON path such as "order.items.0.price", a FIX
 * field name such as "Price", or "value") and kind. Static to the record. */
int32_t za_field(const za_record *record, uint32_t index, const char **out_name,
                 int32_t *out_kind);
/* Index of the field called `name`; ZXV_E_NOT_FOUND. */
int32_t za_field_find(const za_record *record, const char *name, uint32_t *out_index);
/* NUMBER (and BOOL as 0/1): the reduced rational. ZXV_E_STATE otherwise. */
int32_t za_field_rat(const za_record *record, uint32_t index, int64_t *out_num,
                     int64_t *out_den);
/* TEXT: the bytes (static to the record, not NUL-terminated in general;
 * a NUL follows them). NUMBER: the decimal text as it arrived. */
int32_t za_field_text(const za_record *record, uint32_t index, const uint8_t **out_bytes,
                      size_t *out_len);

/* SHA3-256 of a canonical encoding of the record (kind, source, fields):
 * the subject of its evidence records. */
int32_t za_record_digest(const za_record *record, uint8_t out[32]);

/* ===== Validation against a z-contract ===== */
/* Each parameter of `fn` is looked up by name among the fields:
 *   int / rat    a NUMBER, or TEXT that is an exact decimal ("12.50")
 *   truth        a BOOL, or TEXT "neither" / "true" / "false" / "both"
 *   tag          TEXT "base/polarity/phase"
 *   bytes        TEXT (its UTF-8 bytes)
 * A missing field (ZA_R_MISSING), a field that cannot be that type
 * (ZA_R_TYPE) or a contract violation (ZA_R_CONTRACT) refuses the record
 * and, if `log` is not NULL, appends one evidence record (origin
 * ZC_ORIGIN_ADAPTER, subject = za_record_digest). Fields the contract does
 * not name are allowed. Returns ZXV_OK (the record stays NEITHER: passing
 * a contract is not verification), ZA_E_REFUSED, ZXV_E_STATE (the record
 * was already refused or attested), ZXV_E_NOT_FOUND (no such fn), or
 * ZC_E_LOG / ZXV_E_CAPACITY if the log refused the evidence (the record is
 * refused anyway: fail closed). */
int32_t za_validate(za_record *record, const zc_contract *contract, const char *fn,
                    zc_evlog *log);

/* ===== Attestation by the host ===== */
/* The host states what it believes about an ACCEPTED record, on its own
 * evidence. `cap` must pass zxv_cap_check(cap, ZXV_CAP_ATTEST). lp_state
 * is ZXV_LP_TRUE, ZXV_LP_FALSE or ZXV_LP_BOTH; the tag becomes
 * <state>/neutral/manifest. A refused record cannot be attested TRUE or
 * BOTH (ZXV_E_STATE); attesting is once only (ZXV_E_STATE after). */
int32_t za_record_attest(za_record *record, const zxv_cap *cap, int32_t lp_state);

/* ===== Exact decimals ===== */
/* Parse a JSON-style number ("-12.5", "1.5e-3", "7E+2") or a plain decimal
 * exactly into a reduced int64 rational. ZXV_E_FORMAT for bad syntax,
 * ZXV_E_RANGE when it has no exact int64 form. Pure. */
int32_t za_decimal_parse(const char *text, size_t len, int64_t *out_num, int64_t *out_den);

#ifdef __cplusplus
}
#endif

#endif /* ZADAPTER_H */
