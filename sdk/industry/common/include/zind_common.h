/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zind_common.h — the small shared layer of the ZXV industry kits
 * (sdk/industry). Every industry kit links it; it calls only the public
 * libzxv ABI (zxv_api.h) and nothing under kernel/.
 *
 *   status codes   a kit-local range (-3000..-4999), one block per kit
 *   errors         a thread-local last-error text
 *   quantities     exact rationals over zxv_rat_*: parse, format, checked
 *                  arithmetic, and conversion to integer minor units with an
 *                  explicit rounding rule (the default refuses to round)
 *   subjects       domain-separated SHA3-256 digests
 *   seen-sets      replay / duplicate detection over 32-byte digests
 *   signed chains  append-only records, SHA3-256 hash chained, each signed
 *                  with ML-DSA-87; export, import and offline verification
 *   quarantine     a sink that writes Forbidden and Both events into the
 *                  libzxv quarantine log (zxv_hexlog)
 *
 * No floats anywhere (rule R11 in sdk/docs/ABI.md). Kit API 0.1 (preview):
 * until 1.0 this interface may change; the libzxv ABI under it does not. */
#ifndef ZIND_COMMON_H
#define ZIND_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZIND_VERSION_MAJOR 0
#define ZIND_VERSION_MINOR 1

/* ===== Status codes =====
 * Every function returns ZXV_OK, a libzxv ZXV_E_* code, or a code from the
 * industry range. Blocks: common -3000..-3099, logix-clear -3100..-3199,
 * tri-deliver -3200..-3299, v-shelf -3300..-3399, tri-trace -3400..-3499,
 * z-genome -3500..-3599, vinea-triage -3600..-3699, mesh-command -3700..-3799,
 * aero-orbital -3800..-3899, tri-modem -3900..-3999, v-band -4000..-4099;
 * later kits take the next free hundred (see sdk/industry/README.md). */
#define ZIND_E_HELD     (-3001) /* held as Both: frozen until a human resolves it */
#define ZIND_E_REPLAY   (-3002) /* already seen: a replay, a duplicate or double use */
#define ZIND_E_CHAIN    (-3003) /* a hash link or a signature in a chain is broken */
#define ZIND_E_INEXACT  (-3004) /* the exact value has no exact form in minor units */
#define ZIND_E_NEGATIVE (-3005) /* the operation would make a quantity negative */
#define ZIND_E_FROZEN   (-3006) /* the item is Forbidden (absorbing) and never moves */
#define ZIND_E_UNTRUSTED (-3007) /* a record was signed by a key not in the trust set */

/* Static name of a ZIND_E_* code; otherwise zxv_status_name(status). Never
 * NULL. Each kit has its own *_status_name that knows its block and falls
 * back to this one. */
const char *zind_status_name(int32_t status);

/* ===== Errors =====
 * The calling thread's last error text from any industry kit; "" when none.
 * ZXV_E_BUFFER (with *out_len = needed) if `cap` is too small. */
int32_t zind_last_error(char *buf, size_t cap, size_t *out_len);
/* For kit code: set the thread's last error (printf-style, truncated). */
void zind_set_error(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* ===== Exact quantities ===== */
typedef struct {
    int64_t num;
    int64_t den; /* always > 0; num/den always reduced */
} zind_qty;

#define ZIND_QTY_ZERO ((zind_qty){0, 1})

/* num/den reduced. den == 0 is ZXV_E_ARG; INT64_MIN is ZXV_E_RANGE. */
int32_t zind_qty_make(int64_t num, int64_t den, zind_qty *out);
/* "12.75", "-0.1", "3/4", "42" exactly (zxv_rat_parse). ZXV_E_FORMAT on
 * anything else, ZXV_E_RANGE when it does not fit an int64 rational. */
int32_t zind_qty_parse(const char *text, zind_qty *out);
/* A terminating decimal when one exists, else "n/d". Never rounds. */
int32_t zind_qty_format(zind_qty q, char *buf, size_t cap, size_t *out_len);
/* Checked arithmetic: overflow is ZXV_E_RANGE and *out is unchanged. */
int32_t zind_qty_add(zind_qty a, zind_qty b, zind_qty *out);
int32_t zind_qty_sub(zind_qty a, zind_qty b, zind_qty *out);
int32_t zind_qty_mul(zind_qty a, zind_qty b, zind_qty *out);
/* -1, 0 or +1 in *out_cmp. */
int32_t zind_qty_cmp(zind_qty a, zind_qty b, int32_t *out_cmp);
/* -1, 0 or +1 (a well-formed quantity only). */
int zind_qty_sign(zind_qty q);
/* 1 if well formed (den > 0, reduced, no INT64_MIN). */
int zind_qty_valid(zind_qty q);
/* 1 if the quantity is an integer (den == 1). */
int zind_qty_is_int(zind_qty q);

/* Rounding rules for zind_qty_to_minor. EXACT refuses to round. */
#define ZIND_ROUND_EXACT     0 /* ZIND_E_INEXACT unless exact          */
#define ZIND_ROUND_HALF_UP   1 /* half away from zero                  */
#define ZIND_ROUND_HALF_EVEN 2 /* banker's rounding                    */
#define ZIND_ROUND_CEIL      3 /* toward +infinity                     */
#define ZIND_ROUND_FLOOR     4 /* toward -infinity                     */
#define ZIND_ROUND_TRUNC     5 /* toward zero                          */

/* q in minor units with `digits` (0..18) decimal places: *out_minor =
 * round(q * 10^digits). *out_residual (may be NULL) = q - minor/10^digits,
 * exactly, so nothing is lost silently: a caller records it. Beyond
 * +-ZXV_AMOUNT_MAX minor units is ZXV_E_RANGE. */
int32_t zind_qty_to_minor(zind_qty q, uint8_t digits, int32_t round_mode, int64_t *out_minor,
                          zind_qty *out_residual);
/* minor / 10^digits. */
int32_t zind_qty_from_minor(int64_t minor, uint8_t digits, zind_qty *out);

/* ===== Subjects =====
 * SHA3-256( "zind/" || domain || 0x00 || data ). Domain: 1..32 printable
 * ASCII bytes. Used as quarantine-log subjects and seen-set keys. */
int32_t zind_subject(const char *domain, const uint8_t *data, size_t len, uint8_t out[32]);

/* ===== Seen-sets =====
 * A fixed-capacity set of 32-byte digests (open addressing, no deletion).
 * add: ZXV_OK when new, ZIND_E_REPLAY when already present, ZXV_E_CAPACITY
 * when full (nothing is added). */
typedef struct zind_seen zind_seen;
int32_t zind_seen_create(uint32_t capacity, zind_seen **out);
void zind_seen_destroy(zind_seen *s);
int32_t zind_seen_add(zind_seen *s, const uint8_t digest[32]);
int32_t zind_seen_has(const zind_seen *s, const uint8_t digest[32], int32_t *out_present);
int32_t zind_seen_count(const zind_seen *s, uint32_t *out_count);

/* ===== Signers (ML-DSA-87, FIPS 204) =====
 * A signer holds a key pair; the secret key is wiped on destroy. Key
 * generation and signing need ZXV_CAP_CRYPTO_SIGN. The key id is
 * SHA3-256("zind/key/v1" || pk). */
#define ZIND_PK_BYTES  ZXV_MLDSA87_PK_BYTES
#define ZIND_SIG_BYTES ZXV_MLDSA87_SIG_BYTES
#define ZIND_KEYID_BYTES 32u

typedef struct zind_signer zind_signer;
/* Deterministic from a 32-byte seed (tests, examples, key escrow). */
int32_t zind_signer_from_seed(const zxv_cap *cap, const uint8_t seed[32], zind_signer **out);
/* Fresh key from the OS random source. */
int32_t zind_signer_generate(const zxv_cap *cap, zind_signer **out);
void zind_signer_destroy(zind_signer *s);
/* Borrowed pointer to the public key (ZIND_PK_BYTES), valid while s lives. */
const uint8_t *zind_signer_pk(const zind_signer *s);
const uint8_t *zind_signer_key_id(const zind_signer *s);
int32_t zind_key_id(const uint8_t pk[ZIND_PK_BYTES], uint8_t out[32]);
/* Sign arbitrary bytes under a FIPS 204 context string (<= 255 bytes). */
int32_t zind_sign(const zxv_cap *cap, const zind_signer *s, const uint8_t *msg, size_t len,
                  const char *context, uint8_t sig[ZIND_SIG_BYTES]);
/* ZXV_OK or ZXV_E_VERIFY. Needs CRYPTO_SIGN like zxv_mldsa87_verify. */
int32_t zind_verify(const zxv_cap *cap, const uint8_t pk[ZIND_PK_BYTES], const uint8_t *msg,
                    size_t len, const char *context, const uint8_t sig[ZIND_SIG_BYTES]);

/* ===== Signed, hash-chained records =====
 * A chain belongs to one domain (1..32 printable ASCII bytes, e.g.
 * "logix-clear/release"). Record k:
 *   hash_k = SHA3-256( "zind/rec/v1" || u8 len(domain) || domain ||
 *                      u64le seq || u16le kind || u32le len || payload ||
 *                      prev[32] || key_id[32] )
 *   prev_k = hash_{k-1} (32 zeros for k = 0)
 *   sig_k  = ML-DSA-87( hash_k, context = domain ) by the key named key_id
 * The head is the last hash (32 zeros when empty). Payloads are opaque
 * bytes, at most ZIND_PAYLOAD_MAX. */
#define ZIND_DOMAIN_MAX  32u
#define ZIND_PAYLOAD_MAX 8192u /* room for a co-signature (ZIND_SIG_BYTES) */
#define ZIND_CHAIN_MAX   (1u << 20)

typedef struct zind_chain zind_chain;

typedef struct {
    uint64_t seq;
    uint16_t kind;
    const uint8_t *payload;
    size_t payload_len;
    const uint8_t *prev;   /* 32 bytes */
    const uint8_t *hash;   /* 32 bytes */
    const uint8_t *key_id; /* 32 bytes */
    const uint8_t *sig;    /* ZIND_SIG_BYTES */
} zind_record;

/* capacity: 1..ZIND_CHAIN_MAX records. */
int32_t zind_chain_create(const char *domain, uint32_t capacity, zind_chain **out);
void zind_chain_destroy(zind_chain *c);
/* Hash, sign and append. Needs CRYPTO_SIGN. A full chain is ZXV_E_CAPACITY
 * and appends nothing. out_seq and out_hash may be NULL. */
int32_t zind_chain_append(const zxv_cap *cap, zind_chain *c, const zind_signer *signer,
                          uint16_t kind, const uint8_t *payload, size_t len, uint64_t *out_seq,
                          uint8_t out_hash[32]);
int32_t zind_chain_count(const zind_chain *c, uint32_t *out_count);
int32_t zind_chain_head(const zind_chain *c, uint8_t out[32]);
const char *zind_chain_domain(const zind_chain *c);
/* A view of record i; pointers stay valid until the chain is destroyed. */
int32_t zind_chain_at(const zind_chain *c, uint32_t i, zind_record *out);

/* Verify every link and every signature. trusted: n public keys
 * (ZIND_PK_BYTES each); a record's key_id must name one of them.
 *   ZXV_OK            every record intact and signed by a trusted key
 *   ZIND_E_CHAIN      a hash link or signature is broken (*out_bad = index)
 *   ZIND_E_UNTRUSTED  a record names an unknown key (*out_bad = index)
 * out_bad may be NULL; it is set to UINT32_MAX on ZXV_OK. Needs CRYPTO_SIGN. */
int32_t zind_chain_verify(const zxv_cap *cap, const zind_chain *c,
                          const uint8_t *const *trusted, size_t n_trusted, uint32_t *out_bad);

/* Serialize: "ZIND" u8 version(1) u8 len(domain) domain u32le count, then per
 * record u64le seq u16le kind u32le len payload prev[32] hash[32] key_id[32]
 * sig[ZIND_SIG_BYTES]. buf may be NULL to ask the size (ZXV_E_BUFFER). */
int32_t zind_chain_export(const zind_chain *c, uint8_t *buf, size_t cap, size_t *out_len);
/* Parse bytes made by zind_chain_export. Structure only (sizes, sequence
 * numbers in order, domain equal to `domain`); call zind_chain_verify
 * before trusting any of it. ZXV_E_FORMAT on malformed input. */
int32_t zind_chain_import(const char *domain, const uint8_t *buf, size_t len, zind_chain **out);

/* ===== Tags =====
 * A 64-state tag from a ZXV_LP_* value, a ZXV_HEX_POL_* polarity and a
 * ZXV_HEX_PHASE_* phase (zxv_hex_from_lp + zxv_hex_pack). */
int32_t zind_tag(int32_t lp, uint8_t polarity, uint8_t phase, uint8_t *out_tag);
/* The ZXV_LP_* value of a tag's base. */
int32_t zind_tag_lp(uint8_t tag, int32_t *out_lp);

/* ===== Quarantine sink =====
 * Writes Forbidden and Both events into a zxv_hexlog. The log accepts only
 * forbidden tags, so an event is logged as zxv_hex_forbid(tag): a held Both
 * keeps base BOTH in the record (base/forbidden/phase) while the live item
 * stays Both (ESCALATE), not Forbidden. Any other event (base TRUE, FALSE or
 * NEITHER without forbidden polarity) is ZXV_E_ARG: it does not belong in
 * a quarantine log. If the log is full and a halt context was given, the
 * context is halted (zxv_ctx_halt) and ZXV_E_CAPACITY returned: forbidden
 * work that cannot be recorded must not continue. Needs ZXV_CAP_ATTEST. */
#define ZIND_REASON_HELD_BOTH  1 /* contradictory evidence held for a human */
#define ZIND_REASON_FORBIDDEN  2 /* refused for good                        */
#define ZIND_REASON_TAMPER     3 /* a signature or hash did not verify      */
#define ZIND_REASON_REPLAY     4 /* a replayed or double submission         */
#define ZIND_REASON_MISMATCH   5 /* a count or total did not reconcile      */
#define ZIND_REASON_DUPLICATE  6 /* one identity seen in two places         */
#define ZIND_REASON_KIT_BASE  32 /* kits use 32..255 for their own reasons  */

typedef struct zind_qsink zind_qsink;
int32_t zind_qsink_create(const zxv_cap *cap, zxv_hexlog *log, zxv_ctx *halt_ctx,
                          zind_qsink **out);
void zind_qsink_destroy(zind_qsink *s);
int32_t zind_qsink_event(zind_qsink *s, uint8_t tag, uint8_t reason, uint32_t submitter,
                         const uint8_t subject[32], uint64_t *out_seq);
/* Events written so far. */
int32_t zind_qsink_written(const zind_qsink *s, uint64_t *out_count);

/* ===== Little-endian byte writer for canonical payloads ===== */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int overflow; /* set once a write did not fit; later writes are ignored */
} zind_wr;
void zind_wr_init(zind_wr *w, uint8_t *buf, size_t cap);
void zind_wr_u8(zind_wr *w, uint8_t v);
void zind_wr_u16(zind_wr *w, uint16_t v);
void zind_wr_u32(zind_wr *w, uint32_t v);
void zind_wr_u64(zind_wr *w, uint64_t v);
void zind_wr_bytes(zind_wr *w, const void *p, size_t n);
/* u8 length then bytes; strings over 255 bytes set overflow. */
void zind_wr_str(zind_wr *w, const char *s);
void zind_wr_qty(zind_wr *w, zind_qty q);

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    int bad; /* set once a read ran past the end */
} zind_rd;
void zind_rd_init(zind_rd *r, const uint8_t *buf, size_t len);
uint8_t zind_rd_u8(zind_rd *r);
uint16_t zind_rd_u16(zind_rd *r);
uint32_t zind_rd_u32(zind_rd *r);
uint64_t zind_rd_u64(zind_rd *r);
/* Copies a u8-length string into out (cap >= 256 recommended); bad if it
 * does not fit or contains a NUL byte. */
void zind_rd_str(zind_rd *r, char *out, size_t cap);
void zind_rd_qty(zind_rd *r, zind_qty *out);

/* Printable ASCII identifier check: 1..max bytes of [A-Za-z0-9._:/-]. */
int zind_ident_ok(const char *s, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* ZIND_COMMON_H */
