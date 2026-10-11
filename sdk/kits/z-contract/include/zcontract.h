/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zcontract.h — z-contract, typed contracts with exact bounds for ZXV
 * plugin inputs and outputs (kit API 0.2).
 *
 * A contract is a short text that declares functions and the exact values
 * their parameters, result and proposals may take:
 *
 *     # fees.zc
 *     fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0]
 *         proposes rat[0..10^12] den|100 max 1;
 *     fn price(p: rat[0..*] den<=10000, side: truth{true, false})
 *         -> tag{neither/neutral/emerging, true/neutral/manifest};
 *
 * A `*` in a tag pattern matches any name in that position. The full
 * grammar is in README.md, "Syntax".
 *
 * Three parts:
 *   1. PARSE (zc_parse). A hand-written recursive-descent parser. A refusal
 *      names the line, the column, the byte offset and what was expected.
 *      Pure: no global state except the thread-local last error.
 *   2. CHECK (zc_check). Values (zc_vals) are compared with the declared
 *      types exactly: rationals with zxv_rat_cmp, never through a float.
 *      A violation names the part, the index, a ZC_V_* reason and a
 *      message.
 *   3. EVIDENCE (zc_evlog). An append-only log, each record linked to the
 *      previous one by SHA-3-256 (zxv_sha3_256), so a removed or altered
 *      record is detected by zc_evlog_verify. A sink callback sends every
 *      record somewhere else too; the ready-made zc_hexsink writes each
 *      violation as a Forbidden record into a libzxv ABI 1.3 quarantine
 *      log (zxv_hexlog), see "The quarantine sink" below.
 *
 * The Z-Wasm binding (attach a contract to a plugin export; a violating
 * result kills the plugin like a trap) is in zcontract_wasm.h.
 *
 * HONEST LIMITS. A contract checks shapes and bounds of values. It cannot
 * tell whether a value in bounds is the right value. See README.md,
 * "Threat model".
 *
 * KIT API STABILITY. 0.x: it may change before 1.0. It calls only the
 * libzxv ABI 1.3 (zxv_api.h). Opaque handles, int32_t status returns,
 * fixed-width integers, no floats. 0.2 (over 0.1): the tag is libzxv's
 * real 64-state tag (zxv_hex_*), zc_tag_action, the zc_hexsink quarantine
 * sink, and zc_slot_bounds.
 */
#ifndef ZCONTRACT_H
#define ZCONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZC_VERSION_MAJOR 0
#define ZC_VERSION_MINOR 2
#define ZC_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's (passed through unchanged). Z-Wasm owns
 * -1001.., z-contract -1101.., z-ffi -1201.., adapter-matrix -1301... */
#define ZC_OK          0
#define ZC_E_SYNTAX    (-1101) /* the contract text does not parse (zc_parse)      */
#define ZC_E_VIOLATION (-1102) /* values break the contract (zc_check)            */
#define ZC_E_MAPPING   (-1103) /* the function cannot be bound to that target     */
                               /* (e.g. a bytes parameter on a Wasm export)       */
#define ZC_E_LOG       (-1104) /* the evidence log refused a record (full, or its */
                               /* sink refused): fail closed                       */

/* Static, never-NULL name ("ZC_E_SYNTAX"); other codes give libzxv's name. */
const char *zc_status_name(int32_t status);
/* The calling thread's last error detail (R4 two-call buffer). */
int32_t zc_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Limits ===== */
#define ZC_MAX_TEXT      (1024u * 1024u) /* contract text bytes                     */
#define ZC_MAX_FNS       256u            /* functions per contract                  */
#define ZC_MAX_PARAMS    16u             /* parameters per function                 */
#define ZC_MAX_NAME      63u             /* bytes in a name (dotted names included) */
#define ZC_MAX_SET       64u             /* entries in a truth{...} or tag{...} set  */
#define ZC_MAX_VALUES    64u             /* values in one zc_vals                   */
#define ZC_MAX_BYTES     (1024u * 1024u) /* bytes in one bytes value                */
#define ZC_MAX_PROPOSALS 64u             /* the largest `max N` (Z-Wasm stages 64)  */

/* ===== Types ===== */
#define ZC_T_INT   1 /* int64                                                     */
#define ZC_T_RAT   2 /* exact rational, two int64 (reduced, den > 0)              */
#define ZC_T_TRUTH 3 /* ZXV_LP_NEITHER / TRUE / FALSE / BOTH                      */
#define ZC_T_TAG   4 /* the 64-state tag, 0..63 (libzxv zxv_hex_* codes)          */
#define ZC_T_BYTES 5 /* a byte string (its length is what a contract bounds)      */

/* ===== The 64-state tag (libzxv ABI 1.3) =====
 * A tag is libzxv's 64-state hexagram tag (zxv_hex_* in zxv_api.h, from
 * kernel/src/lpres/lpres_hex.h):
 *
 *   code = base << 4 | polarity << 2 | phase          (0..63)
 *   base      false 0, neither 1, both 2, true 3      (NOT the ZXV_LP_* numbering)
 *   polarity  entropic 0, neutral 1, forbidden 2, negentropic 3
 *   phase     potential 0, emerging 1, dissolving 2, manifest 3
 *
 * Kit API 0.1 carried a placeholder with the same layout; since 0.2 these
 * functions call zxv_hex_*, so the codes ARE libzxv's codes. The text form
 * "base/polarity/phase" (e.g. "neither/neutral/emerging", the same text as
 * zxv_hex_name) is still the friendliest thing to store. */
#define ZC_TAG_PROVISIONAL 0 /* was 1 in kit API 0.1 (placeholder codes) */

/* Aliases kept from 0.1; they equal the ZXV_HEX_* constants. */
#define ZC_BASE_FALSE   ZXV_HEX_BASE_FALSE
#define ZC_BASE_NEITHER ZXV_HEX_BASE_NEITHER
#define ZC_BASE_BOTH    ZXV_HEX_BASE_BOTH
#define ZC_BASE_TRUE    ZXV_HEX_BASE_TRUE
#define ZC_POL_ENTROPIC    ZXV_HEX_POL_ENTROPIC
#define ZC_POL_NEUTRAL     ZXV_HEX_POL_NEUTRAL
#define ZC_POL_FORBIDDEN   ZXV_HEX_POL_FORBIDDEN
#define ZC_POL_NEGENTROPIC ZXV_HEX_POL_NEGENTROPIC
#define ZC_PH_POTENTIAL  ZXV_HEX_PHASE_POTENTIAL
#define ZC_PH_EMERGING   ZXV_HEX_PHASE_EMERGING
#define ZC_PH_DISSOLVING ZXV_HEX_PHASE_DISSOLVING
#define ZC_PH_MANIFEST   ZXV_HEX_PHASE_MANIFEST

/* Build a tag from a four-valued ZXV_LP_* state (zxv_hex_from_lp), a
 * ZXV_HEX_POL_* polarity and a ZXV_HEX_PHASE_* phase (zxv_hex_pack).
 * ZXV_E_ARG for values out of range. */
int32_t zc_tag_make(int32_t lp_state, int32_t polarity, int32_t phase, int32_t *out_tag);
/* Split a tag (zxv_hex_unpack); *out_lp_state is the ZXV_LP_* value of its
 * base (zxv_hex_to_lp). Any out pointer may be NULL. */
int32_t zc_tag_split(int32_t tag, int32_t *out_lp_state, int32_t *out_polarity,
                     int32_t *out_phase);
/* What libzxv says happens next for a tag: a ZXV_HEX_ACT_* value
 * (zxv_hex_action). A forbidden tag gives ZXV_HEX_ACT_ROLLBACK. (0.2) */
int32_t zc_tag_action(int32_t tag, int32_t *out_action);
/* "true/negentropic/manifest" <-> code. ZXV_E_FORMAT for unknown names. */
int32_t zc_tag_parse(const char *text, int32_t *out_tag);
int32_t zc_tag_format(int32_t tag, char *buf, size_t cap, size_t *out_len);

/* ===== Contracts ===== */
typedef struct zc_contract zc_contract;

/* Parse a contract. `text` need not be NUL-terminated; `len` bytes are
 * read (at most ZC_MAX_TEXT) and must be valid UTF-8. Returns ZXV_OK with
 * *out_contract, or ZC_E_SYNTAX. On ZC_E_SYNTAX (unlike libzxv R3) the
 * position outputs are written: *out_line and *out_col (1-based; the column
 * counts bytes) and *out_offset (0-based byte offset), and `msg` receives
 * a one-line message TRUNCATED to fit (always NUL-terminated when
 * msg_cap > 0; *out_msg_len gets its full length). Any out pointer except
 * out_contract may be NULL. ZXV_E_NOMEM, ZXV_E_ARG as usual. */
int32_t zc_parse(const char *text, size_t len, zc_contract **out_contract, uint32_t *out_line,
                 uint32_t *out_col, uint64_t *out_offset, char *msg, size_t msg_cap,
                 size_t *out_msg_len);
void zc_contract_destroy(zc_contract *contract);

/* The canonical text of the contract (R4): one function per line, numbers
 * written out (10^12 -> 1000000000000, rationals reduced), no comments.
 * zc_parse of the canonical text gives the same canonical text. */
int32_t zc_contract_format(const zc_contract *contract, char *buf, size_t cap, size_t *out_len);
/* SHA3-256 of the canonical text: what evidence records cite. */
int32_t zc_contract_digest(const zc_contract *contract, uint8_t out[32]);

/* Introspection. Function names are static to the contract (valid until
 * it is destroyed). ZXV_E_NOT_FOUND for an unknown function or index. */
int32_t zc_fn_count(const zc_contract *contract, uint32_t *out_count);
int32_t zc_fn_name(const zc_contract *contract, uint32_t index, const char **out_name);

#define ZC_PART_PARAMS    1 /* the function's parameters                         */
#define ZC_PART_RESULT    2 /* its result (`-> type`; absent: returns nothing)    */
#define ZC_PART_PROPOSALS 3 /* the values it proposes (`proposes type max N`;     */
                            /* absent: it must propose nothing)                   */

/* *out_nparams; *out_has_result / *out_has_proposes 0 or 1;
 * *out_max_proposals the declared `max N` (1 if omitted; 0 if no proposes). */
int32_t zc_fn_info(const zc_contract *contract, const char *fn, uint32_t *out_nparams,
                   int32_t *out_has_result, int32_t *out_has_proposes,
                   uint32_t *out_max_proposals);
/* The ZC_T_* type of a parameter (part PARAMS, index i), of the result
 * (part RESULT, index 0) or of the proposals (part PROPOSALS, index 0),
 * and for a parameter its name (static to the contract; "" otherwise). */
int32_t zc_slot_type(const zc_contract *contract, const char *fn, int32_t part, uint32_t index,
                     int32_t *out_type, const char **out_name);

/* The declared bounds of one slot (kit API 0.2), for tools such as
 * sdk/kits/z-audit. A plain output structure, filled by zc_slot_bounds.
 *   INT, RAT   has_lo / has_hi are 0 when that side is open ("*", or no
 *              range at all); lo and hi are reduced rationals (INT: den 1).
 *              den_mode / den_bound are the den<=N or den|N rule.
 *   BYTES      the length range, in lo_num / hi_num (den 1).
 *   TRUTH, TAG set_mask: bit k set when code k is allowed (all of them
 *              when no {...} set was written). */
#define ZC_DEN_NONE    0
#define ZC_DEN_LE      1 /* den<=N */
#define ZC_DEN_DIVIDES 2 /* den|N  */

typedef struct {
    int32_t type; /* ZC_T_* */
    int32_t has_lo, has_hi;
    int64_t lo_num, lo_den, hi_num, hi_den;
    int32_t den_mode; /* ZC_DEN_* */
    int64_t den_bound;
    uint64_t set_mask;
} zc_bounds;

/* Fill *out for the slot named as in zc_slot_type. ZXV_E_NOT_FOUND for an
 * unknown function or slot. */
int32_t zc_slot_bounds(const zc_contract *contract, const char *fn, int32_t part, uint32_t index,
                       zc_bounds *out);

/* ===== Values ===== */
typedef struct zc_vals zc_vals;

int32_t zc_vals_create(zc_vals **out_vals);
void zc_vals_destroy(zc_vals *vals);
int32_t zc_vals_clear(zc_vals *vals);
int32_t zc_vals_count(const zc_vals *vals, uint32_t *out_count);
/* Append one value (ZXV_E_CAPACITY past ZC_MAX_VALUES). A rational is
 * stored as given and judged by zc_check (den 0 or INT64_MIN is the
 * INVALID reason there, not an error here). Bytes are copied (at most
 * ZC_MAX_BYTES, ZXV_E_TOO_LONG otherwise). */
int32_t zc_vals_push_int(zc_vals *vals, int64_t value);
int32_t zc_vals_push_rat(zc_vals *vals, int64_t num, int64_t den);
int32_t zc_vals_push_truth(zc_vals *vals, int32_t lp_state);
int32_t zc_vals_push_tag(zc_vals *vals, int32_t tag);
int32_t zc_vals_push_bytes(zc_vals *vals, const uint8_t *data, size_t len);
/* Read value i back: its ZC_T_* kind, and num/den (INT: den 1; TRUTH and
 * TAG: the code, den 1; BYTES: num = length, den 1). */
int32_t zc_vals_get(const zc_vals *vals, uint32_t index, int32_t *out_kind, int64_t *out_num,
                    int64_t *out_den);

/* ===== Checking ===== */
#define ZC_V_OK          0
#define ZC_V_ARITY       1  /* wrong number of values (params, or a result that   */
                            /* is missing or not declared)                        */
#define ZC_V_KIND        2  /* a value of another kind (bytes for an int, ...)    */
#define ZC_V_INVALID     3  /* not a value at all: den 0, INT64_MIN, tag > 63,    */
                            /* truth > 3                                          */
#define ZC_V_NOT_INTEGER 4  /* a non-integral rational for an int                 */
#define ZC_V_BELOW       5  /* below the range's lower bound                       */
#define ZC_V_ABOVE       6  /* above the range's upper bound                       */
#define ZC_V_DENOMINATOR 7  /* the reduced denominator breaks den<=N or den|N      */
#define ZC_V_NOT_IN_SET  8  /* a truth or tag value not in the declared set        */
#define ZC_V_LENGTH      9  /* a byte length outside bytes[lo..hi]                 */
#define ZC_V_COUNT       10 /* more proposals than `max N` (or any, if undeclared) */

/* Static, never-NULL ("ABOVE"). */
const char *zc_reason_name(int32_t reason);

/* Check `vals` against part `part` of function `fn`:
 *   ZC_PART_PARAMS     one value per parameter, in order;
 *   ZC_PART_RESULT     zero values (no result) or one;
 *   ZC_PART_PROPOSALS  zero or more values, each against the proposes type.
 * Returns ZXV_OK, ZC_E_VIOLATION, or ZXV_E_NOT_FOUND (unknown fn) /
 * ZXV_E_ARG. On ZXV_OK and ZC_E_VIOLATION the outputs are written:
 * *out_reason (ZC_V_*), *out_index (the offending value; 0 for ARITY),
 * and `msg` (truncated to fit, NUL-terminated when msg_cap > 0;
 * *out_msg_len its full length). Any out pointer may be NULL. Pure. */
int32_t zc_check(const zc_contract *contract, const char *fn, int32_t part, const zc_vals *vals,
                 int32_t *out_reason, uint32_t *out_index, char *msg, size_t msg_cap,
                 size_t *out_msg_len);

/* SHA3-256 of a canonical encoding of (part, values): the "subject" an
 * evidence record cites without copying the data. */
int32_t zc_vals_digest(const zc_vals *vals, int32_t part, uint8_t out[32]);

/* ===== Evidence log =====
 * An append-only, hash-chained record of violations. Record k holds
 *   seq (1, 2, ...), origin, part, index, reason, tag, the contract digest,
 *   the subject digest, a text (function name and message, <= 255 bytes),
 *   prev = hash of record k-1 (32 zero bytes for the first), and
 *   hash = SHA3-256("zxv/zc-evlog/v1" || seq u64le || prev || origin u32le
 *          || part i32le || index u32le || reason i32le || tag i32le
 *          || contract[32] || subject[32] || text_len u32le || text).
 * The tag of a contract violation is false/forbidden/manifest: the kernel's
 * action for a forbidden tag is ROLLBACK and quarantine, which is what the
 * Z-Wasm binding does (the plugin is stopped and its proposals dropped).
 *
 * A full log REFUSES the append (ZXV_E_CAPACITY). Records are never
 * evicted: a caller that cannot record evidence must stop (fail closed),
 * which the Z-Wasm binding does by refusing the call. Signing the head is
 * the caller's job (zc_evlog_head, then ML-DSA-87 or Ed25519 via libzxv).
 *
 * SINKS. zc_evlog_set_sink registers a function called for every record
 * after it is hashed and before it is committed. A non-zero return
 * refuses the record (ZC_E_LOG), so a record is in this log only if the
 * sink took it. zc_hexsink (below) is a ready-made sink for libzxv's
 * ABI 1.3 quarantine log. */
typedef struct zc_evlog zc_evlog;

#define ZC_ORIGIN_HOST    0 /* the host appended it directly        */
#define ZC_ORIGIN_WASM    1 /* the Z-Wasm binding                    */
#define ZC_ORIGIN_ADAPTER 2 /* sdk/kits/adapter-matrix                */

#define ZC_EVLOG_TEXT_MAX 255u

typedef int32_t (*zc_evlog_sink)(void *user, uint64_t seq, uint32_t origin, int32_t tag,
                                 int32_t reason, const uint8_t subject[32],
                                 const uint8_t hash[32], const char *text);

/* capacity: 1 .. 2^20 records (allocated as records arrive). */
int32_t zc_evlog_create(uint32_t capacity, zc_evlog **out_log);
void zc_evlog_destroy(zc_evlog *log);
/* NULL fn removes the sink. */
int32_t zc_evlog_set_sink(zc_evlog *log, zc_evlog_sink fn, void *user);
/* Append a record. `contract` may be NULL (32 zero bytes are recorded);
 * `text` is truncated to ZC_EVLOG_TEXT_MAX bytes (at a UTF-8 boundary).
 * ZXV_E_CAPACITY when full, ZC_E_LOG when the sink refuses. */
int32_t zc_evlog_append(zc_evlog *log, uint32_t origin, int32_t part, uint32_t index,
                        int32_t reason, int32_t tag, const uint8_t contract[32],
                        const uint8_t subject[32], const char *text, uint64_t *out_seq);
int32_t zc_evlog_count(const zc_evlog *log, uint64_t *out_count);
/* Hash of the last record (32 zero bytes when empty). */
int32_t zc_evlog_head(const zc_evlog *log, uint8_t out[32]);
/* Read record `seq` (1-based). The text follows R4 (NULL text buffer or a
 * small one: ZXV_E_BUFFER with *out_text_len set, nothing else written).
 * Any other out pointer may be NULL. */
int32_t zc_evlog_get(const zc_evlog *log, uint64_t seq, uint32_t *out_origin, int32_t *out_part,
                     uint32_t *out_index, int32_t *out_reason, int32_t *out_tag,
                     uint8_t out_contract[32], uint8_t out_subject[32], uint8_t out_hash[32],
                     char *text, size_t text_cap, size_t *out_text_len);
/* Recompute every link. ZXV_OK, or ZXV_E_VERIFY (zc_last_error names the
 * first bad record). */
int32_t zc_evlog_verify(const zc_evlog *log);

/* ===== The quarantine sink (libzxv ABI 1.3, kit API 0.2) =====
 * zc_hexsink writes contract violations as Forbidden records into a
 * libzxv quarantine log (zxv_hexlog, see zxv_api.h). Attach it to an
 * evidence log and every record appended there (by the host, the Z-Wasm
 * binding or adapter-matrix) is also quarantined:
 *
 *   zxv_hexlog_create(attest_cap, 4096, &q);
 *   zc_hexsink_create(attest_cap, q, 0, &hs);
 *   zc_evlog_attach_hexsink(evlog, hs);
 *
 * Each evidence record becomes one zxv_hexlog_append with
 *   tag        the record's tag made forbidden (zxv_hex_forbid: the base
 *              and phase are kept, so an input refusal false/neutral/
 *              potential becomes false/forbidden/potential and an output
 *              violation stays false/forbidden/manifest);
 *   reason     the record's reason (a ZC_V_* code); a reason outside
 *              0..254 is stored as ZC_HEXSINK_REASON_OTHER (255);
 *   submitter  the record's origin (ZC_ORIGIN_*);
 *   subject    the evidence record's own hash, so each quarantine record
 *              points at exactly one evidence record (which in turn holds
 *              the contract digest and the subject digest of the values).
 *
 * The cap must pass zxv_cap_check(cap, ZXV_CAP_ATTEST) and belong to the
 * hexlog's context; libzxv checks both again on every append. If the
 * append fails (a full log: ZXV_E_CAPACITY, a revoked cap: ZXV_E_CAP, a
 * halted context: ZXV_E_POISONED) the sink refuses, so the evidence log
 * refuses the record too (ZC_E_LOG) and a Z-Wasm guard fails closed.
 * libzxv says a host whose quarantine log is full must halt its context:
 * zc_hexsink_halt_on_full does that for you.
 *
 * The sink keeps the cap and hexlog pointers (no copies): both must
 * outlive it, and it must outlive the evidence log it is attached to (or
 * be detached with zc_evlog_set_sink(log, NULL, NULL)). Not thread-safe,
 * like the evidence log (the zxv_hexlog itself is serialized). */
typedef struct zc_hexsink zc_hexsink;

#define ZC_HEXSINK_FORBIDDEN_ONLY 0x1u /* flag: only quarantine records whose  */
                                       /* tag is already forbidden (the output */
                                       /* violations); skip the others         */
#define ZC_HEXSINK_REASON_OTHER 255

int32_t zc_hexsink_create(const zxv_cap *cap, zxv_hexlog *hexlog, uint32_t flags,
                          zc_hexsink **out_sink);
void zc_hexsink_destroy(zc_hexsink *sink);
/* Halt `ctx` (zxv_ctx_halt) when the quarantine log is full, before the
 * record is refused. NULL turns it off. `ctx` must be the hexlog's. */
int32_t zc_hexsink_halt_on_full(zc_hexsink *sink, zxv_ctx *ctx);
/* zc_evlog_set_sink(log, zc_hexsink_record, sink). */
int32_t zc_evlog_attach_hexsink(zc_evlog *log, zc_hexsink *sink);
/* The sink function itself (a zc_evlog_sink; `user` is the zc_hexsink),
 * for a host that chains it behind its own sink. Returns 0 when the record
 * was quarantined or skipped, else the libzxv status of the failed append
 * (or ZXV_E_ARG for a tag outside 0..63). */
int32_t zc_hexsink_record(void *user, uint64_t seq, uint32_t origin, int32_t tag, int32_t reason,
                          const uint8_t subject[32], const uint8_t hash[32], const char *text);
/* Records quarantined, records skipped (FORBIDDEN_ONLY), and the status of
 * the last append (ZXV_OK before the first). Any out pointer may be NULL. */
int32_t zc_hexsink_stats(const zc_hexsink *sink, uint64_t *out_written, uint64_t *out_skipped,
                         int32_t *out_last_status);

#ifdef __cplusplus
}
#endif

#endif /* ZCONTRACT_H */
