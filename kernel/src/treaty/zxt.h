/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxt.h — ZXV treaties: the COBOL copybook record and its compact Sutra form.
 *
 * A treaty is one batch of work for the engine (src/engine/zxv_engine.h):
 * who takes part, what each one deposits and may claim back, how a dividend
 * is shared, and who owes whom. It has ONE abstract form (zxt_treaty_t) and
 * two concrete ones that lower to it:
 *
 *   RECORD  the fixed 2542-byte record laid out by copybooks/ZXVTRTY.cpy.
 *           A COBOL program fills it and runs CALL 'ZXV_ENGINE' USING
 *           ZXV-TREATY ZXV-RESULT (zxt_cobol.c). Every numeric field is
 *           DISPLAY text, so the record is plain ASCII on every machine.
 *   SUTRA   a compact line form for agents and people, e.g.
 *
 *             TREATY CORRIDOR-7 USD SETL
 *             PARTY ACME 5000 4000 1/2
 *             PARTY BETA 3000 3000 1/2
 *             OWE ACME BETA 1200
 *             END
 *
 * This file is the validate-and-lower half only (the "libzxv" side): it
 * reads, checks and writes treaties, and never runs one. Running one is
 * zxt_exec.h, which lives in userland with the engine.
 *
 * GUARANTEES (each one tested in test_zxt.c and fuzzed in fuzz_zxt.c)
 *   T1  Zero allocation, no recursion, no floating point, no libc: the
 *       caller owns every byte; stack use is a few hundred bytes.
 *   T2  Fail closed: any input the grammar or the rules do not admit is
 *       refused with a negative zxt_status_t and the byte offset of the
 *       first fault; a refused parse leaves the output treaty all zero.
 *   T3  One reading: each concrete form has exactly one spelling per
 *       treaty (single spaces, '\n' line ends, no leading zeros, no "+",
 *       no "-0", fractions in lowest terms, record pads exactly as COBOL's
 *       INITIALIZE leaves them). So parse(x) == OK implies
 *       emit(parse(x)) == x byte for byte, in both forms.
 *   T4  Same AST: the Sutra and record forms of one treaty parse to equal
 *       zxt_treaty_t values (zxt_equal), so either form lowers to the other.
 *   T5  The rules, checked on every parse and every emit:
 *         R1  weights: every party's weight num/den has 1 <= den,
 *             0 <= num <= den, gcd(num, den) == 1, and the weights sum to
 *             exactly 1. A sum that is not 1, or whose exact computation
 *             would leave 64 bits, is refused (ZXT_E_WEIGHT).
 *         R2  claim <= deposit for every party (ZXT_E_CLAIM).
 *         R3  every amount fits PIC S9(18): |v| <= 999 999 999 999 999 999;
 *             deposits and claims are >= 0, obligations are > 0.
 *         R4  1..16 parties with distinct ids; 0..64 obligations, each
 *             between two different known parties.
 *
 * HONEST LIMITS. A treaty is a batch description, not a contract in law:
 * signatures, consent and authority live elsewhere (pqsec, lpres). One
 * asset per treaty. The weights only share the dividend the batch's fees
 * put in the V-Bill bucket, and that share is reported, not paid.
 */
#ifndef ZXV_TREATY_ZXT_H
#define ZXV_TREATY_ZXT_H

#include <stdbool.h>
#include <stdint.h>

#define ZXT_MAX_PARTIES 16u
#define ZXT_MAX_OWES 64u
#define ZXT_NAME_MAX 30u
#define ZXT_ID_MAX 8u
#define ZXT_PIC18_MAX 999999999999999999LL /* PIC S9(18) */
#define ZXT_WEIGHT_MAX 999999999u          /* PIC 9(9)   */

/* RECORD layout (copybooks/ZXVTRTY.cpy), byte offsets from 0. */
#define ZXT_REC_MAGIC "ZXT1"
#define ZXT_REC_HEAD 46u   /* MAGIC 4, NAME 30, ASSET 3, OP 4, NPARTY 2, NOWE 3 */
#define ZXT_REC_PARTY 64u  /* ID 8, DEPOSIT 19, CLAIM 19, WNUM 9, WDEN 9      */
#define ZXT_REC_OWE 23u    /* FROM 2, TO 2, AMOUNT 19                         */
#define ZXT_REC_LEN (ZXT_REC_HEAD + ZXT_MAX_PARTIES * ZXT_REC_PARTY + ZXT_MAX_OWES * ZXT_REC_OWE)

/* The longest canonical Sutra text: header, 16 full PARTY lines, 64 full
 * OWE lines and END. */
#define ZXT_SUTRA_MAX (7u + ZXT_NAME_MAX + 5u + 5u + \
                       ZXT_MAX_PARTIES * (6u + ZXT_ID_MAX + 19u + 19u + 21u) + \
                       ZXT_MAX_OWES * (4u + 2u * (ZXT_ID_MAX + 1u) + 19u) + 4u)

typedef enum {
    ZXT_OK = 0,
    ZXT_E_ARG = -1,     /* NULL pointer or a length the form cannot have  */
    ZXT_E_MAGIC = -2,   /* record does not start with "ZXT1"              */
    ZXT_E_SYNTAX = -3,  /* Sutra text outside the grammar                 */
    ZXT_E_NAME = -4,    /* treaty name not a COBOL-style name of 1..30    */
    ZXT_E_ASSET = -5,   /* asset not three capital letters                */
    ZXT_E_OP = -6,      /* operation not NET or SETL                      */
    ZXT_E_COUNT = -7,   /* party or obligation count out of range         */
    ZXT_E_NUMBER = -8,  /* malformed number or outside PIC S9(18)          */
    ZXT_E_PARTY = -9,   /* bad, duplicate or unknown party id             */
    ZXT_E_WEIGHT = -10, /* R1                                              */
    ZXT_E_CLAIM = -11,  /* R2                                              */
    ZXT_E_OWE = -12,    /* obligation to self, of zero/negative amount    */
    ZXT_E_PAD = -13,    /* unused record slot not as INITIALIZE leaves it */
    ZXT_E_CAP = -14     /* output buffer too small                         */
} zxt_status_t;

typedef enum { ZXT_OP_NET = 1, ZXT_OP_SETL = 2 } zxt_op_t;

typedef struct {
    char id[ZXT_ID_MAX + 1u]; /* NUL-terminated, zero-filled after it */
    int64_t deposit;          /* >= 0, PIC S9(18)                      */
    int64_t claim;            /* 0 <= claim <= deposit                 */
    uint32_t w_num;           /* weight num/den, lowest terms          */
    uint32_t w_den;
} zxt_party_t;

typedef struct {
    uint8_t from; /* party index, 0-based */
    uint8_t to;
    int64_t amount; /* > 0, PIC S9(18) */
} zxt_owe_t;

typedef struct {
    char name[ZXT_NAME_MAX + 1u];
    char asset[4];
    uint8_t op; /* zxt_op_t */
    uint8_t n_party;
    uint8_t n_owe;
    zxt_party_t party[ZXT_MAX_PARTIES];
    zxt_owe_t owe[ZXT_MAX_OWES];
} zxt_treaty_t;

/* Set every byte of *t to zero. */
void zxt_clear(zxt_treaty_t *t);
/* R1-R4 plus the name/id/asset/op spelling rules on an AST built by hand.
 * Unused slots and the bytes after each NUL must be zero. */
zxt_status_t zxt_validate(const zxt_treaty_t *t);
/* Field-by-field equality of two treaties (all slots, all bytes). */
bool zxt_equal(const zxt_treaty_t *a, const zxt_treaty_t *b);

/* RECORD form. `len` must be exactly ZXT_REC_LEN. *err_off (may be NULL)
 * gets the byte offset of the first fault, 0 on success. */
zxt_status_t zxt_parse_record(const uint8_t *rec, uint32_t len, zxt_treaty_t *out,
                              uint32_t *err_off);
/* Write the canonical record; `cap` must be >= ZXT_REC_LEN. */
zxt_status_t zxt_emit_record(const zxt_treaty_t *t, uint8_t *rec, uint32_t cap);

/* SUTRA form. The text is exactly `len` bytes (no NUL needed). */
zxt_status_t zxt_parse_sutra(const char *src, uint32_t len, zxt_treaty_t *out, uint32_t *err_off);
/* Write the canonical text (no NUL); *n_out gets its length. On failure
 * nothing past what the caller already had is relied on and *n_out is 0. */
zxt_status_t zxt_emit_sutra(const zxt_treaty_t *t, char *out, uint32_t cap, uint32_t *n_out);

/* ---- PIC S9(18) arithmetic, as COBOL does it with ON SIZE ERROR ----
 * Each returns false (a size error) and leaves *r untouched when an
 * operand or the exact result is outside +-999 999 999 999 999 999, or on
 * division by zero. Division truncates toward zero; the remainder has the
 * dividend's sign (DIVIDE ... GIVING q REMAINDER r). */
bool zxt_pic_ok(int64_t v);
bool zxt_pic_add(int64_t a, int64_t b, int64_t *r);
bool zxt_pic_sub(int64_t a, int64_t b, int64_t *r);
bool zxt_pic_mul(int64_t a, int64_t b, int64_t *r);
bool zxt_pic_div(int64_t a, int64_t b, int64_t *q, int64_t *rem);

#endif /* ZXV_TREATY_ZXT_H */
