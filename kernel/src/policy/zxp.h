/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* zxp.h — zxv_contract_dsl: the declarative contract a policy evaluates.
 *
 * A contract here is the MACHINE-READABLE RULE LAYER of an agreement, never
 * the agreement itself. The prose (the signed agreement, treaty or terms)
 * stays an external document; its SHA-256 is the DOC line, and every
 * transfer checked against the contract must carry the same digest. The rule
 * layer holds only bounded predicates:
 *
 *   SIGN     a signature threshold (k of the parties)
 *   WINDOW   the dates the contract allows transfers in; LOCK lockouts
 *   CAP      a per-transfer ceiling per asset (and, if any CAP is present,
 *            the list of assets allowed at all)
 *   LIMIT    a cumulative ceiling per paying party and asset
 *   ALLOW / DENY  jurisdiction tags (prefix match: "US" covers "US-NY")
 *   DUE      a payment obligation: payer, payee, asset, amount, date, an
 *            optional fixed PENALTY and an optional AFTER condition
 *   GATE     ALL / ANY over earlier DUE terms or gates
 *
 * Like the treaty pillar (../treaty/zxt.h) it has one abstract form
 * (zxp_contract_t) and two spellings that lower to it:
 *
 *   SUTRA   for people and agents, one line per rule:
 *
 *             CONTRACT SUPPLY-2027 20261015
 *             DOC 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
 *             LAW KE
 *             PARTY ACME US-DE
 *             PARTY BETA KE
 *             SIGN 2
 *             WINDOW 20261015 20271231
 *             CAP USD 2000000
 *             ALLOW KE
 *             ALLOW US
 *             DUE T1 ACME BETA USD 1200000 20261231 PENALTY 12000
 *             DUE T2 ACME BETA USD 300000 20270331 AFTER T1
 *             END
 *
 *   RECORD  the fixed 4419-byte DISPLAY record of copybooks/ZXVCNTR.cpy, so
 *           a COBOL program can fill it and CALL 'ZXV_POLICY'.
 *
 * GUARANTEES (tested in test_zxp.c, fuzzed in fuzz_zxp.c)
 *   C1  Freestanding: no libc, no allocation, no recursion, no floats.
 *   C2  Fail closed with the byte offset of the first fault; a refused
 *       parse leaves the output all zero.
 *   C3  One reading: single spaces, '\n' line ends, sections in the order
 *       above, list sections sorted, no leading zeros. parse(x) == OK
 *       implies emit(parse(x)) == x byte for byte, in both forms.
 *   C4  The Sutra and the record of one contract parse to equal ASTs.
 *   C5  References only point backwards (AFTER and GATE operands name an
 *       earlier clause), so a dependency cycle cannot be written.
 *
 * A contract may ALLOW and DENY the same tag. That is not refused: it is a
 * contradiction the evaluator reports as BOTH and escalates (policy.h).
 */
#ifndef ZXV_POLICY_ZXP_H
#define ZXV_POLICY_ZXP_H

#include <stdbool.h>
#include <stdint.h>

#define ZXP_MAX_LAWS    4u
#define ZXP_MAX_PARTIES 15u /* the 16th zxt slot is the escrow (policy.h) */
#define ZXP_MAX_LOCKS   8u
#define ZXP_MAX_CAPS    8u
#define ZXP_MAX_LIMITS  32u
#define ZXP_MAX_TAGS    16u /* per ALLOW list and per DENY list */
#define ZXP_MAX_CLAUSES 32u /* DUE terms and GATEs together    */
#define ZXP_MAX_REFS    8u
#define ZXP_NAME_MAX    30u
#define ZXP_ID_MAX      8u
#define ZXP_JUR_MAX     6u /* "KE", "US-NY", "GB-ENG" */
#define ZXP_PIC18_MAX   999999999999999999LL
#define ZXP_ESCROW_ID   "ESCROW" /* reserved: no party may use it */

/* RECORD layout (copybooks/ZXVCNTR.cpy). */
#define ZXP_REC_MAGIC  "ZXC1"
#define ZXP_REC_HEAD   137u /* MAGIC 4 NAME 30 DATE 8 DOC 64 SIGN 2 WFROM 8 WTO 8
                               NLAW 1 NPARTY 2 NLOCK 1 NCAP 1 NLIMIT 2
                               NALLOW 2 NDENY 2 NCLAUSE 2; the LAW table follows */
#define ZXP_REC_LAWS   (ZXP_MAX_LAWS * ZXP_JUR_MAX)
#define ZXP_REC_PARTY  14u /* ID 8, JUR 6                         */
#define ZXP_REC_LOCK   16u /* FROM 8, TO 8                        */
#define ZXP_REC_CAP    22u /* ASSET 3, AMOUNT 19                  */
#define ZXP_REC_LIMIT  24u /* PARTY 2, ASSET 3, AMOUNT 19         */
#define ZXP_REC_TAG    6u
#define ZXP_REC_CLAUSE 87u /* KIND 4 ID 8 PAYER 2 PAYEE 2 ASSET 3 AMOUNT 19
                              DATE 8 PENALTY 19 AFTER 2 GOP 3 NREF 1 REF 8x2 */
#define ZXP_REC_LEN                                                                                \
    (ZXP_REC_HEAD + ZXP_REC_LAWS + ZXP_MAX_PARTIES * ZXP_REC_PARTY + ZXP_MAX_LOCKS * ZXP_REC_LOCK + \
     ZXP_MAX_CAPS * ZXP_REC_CAP + ZXP_MAX_LIMITS * ZXP_REC_LIMIT +                                  \
     2u * ZXP_MAX_TAGS * ZXP_REC_TAG + ZXP_MAX_CLAUSES * ZXP_REC_CLAUSE)

typedef enum {
    ZXP_OK = 0,
    ZXP_E_ARG = -1,     /* NULL pointer or impossible length          */
    ZXP_E_MAGIC = -2,   /* record does not start with "ZXC1"          */
    ZXP_E_SYNTAX = -3,  /* Sutra text outside the grammar             */
    ZXP_E_NAME = -4,    /* contract name, party id or clause id       */
    ZXP_E_DATE = -5,    /* not a calendar date YYYYMMDD, or out of order */
    ZXP_E_DOC = -6,     /* DOC not 64 lowercase hex digits            */
    ZXP_E_JUR = -7,     /* jurisdiction tag malformed                 */
    ZXP_E_COUNT = -8,   /* a section has too few or too many lines    */
    ZXP_E_NUMBER = -9,  /* malformed number or outside PIC S9(18)     */
    ZXP_E_PARTY = -10,  /* unknown, duplicate or reserved party id    */
    ZXP_E_ORDER = -11,  /* list not sorted, duplicate, overlapping lock */
    ZXP_E_SIGN = -12,   /* threshold not 1..parties                   */
    ZXP_E_ASSET = -13,  /* asset not three capital letters            */
    ZXP_E_REF = -14,    /* AFTER/GATE names no earlier clause          */
    ZXP_E_CLAUSE = -15, /* payer == payee, amount <= 0, bad gate      */
    ZXP_E_PAD = -16,    /* unused slot not as INITIALIZE leaves it    */
    ZXP_E_CAP = -17     /* output buffer too small                    */
} zxp_status_t;

typedef enum { ZXP_CL_DUE = 1, ZXP_CL_GATE = 2 } zxp_clause_kind_t;
typedef enum { ZXP_GATE_ALL = 1, ZXP_GATE_ANY = 2 } zxp_gate_op_t;

typedef struct {
    char id[ZXP_ID_MAX + 1u];
    char jur[ZXP_JUR_MAX + 1u];
} zxp_party_t;

typedef struct {
    uint32_t from, to; /* YYYYMMDD, inclusive */
} zxp_span_t;

typedef struct {
    char asset[4];
    int64_t amount; /* > 0 */
} zxp_cap_t;

typedef struct {
    uint8_t party; /* 0-based */
    char asset[4];
    int64_t amount; /* > 0, cumulative */
} zxp_limit_t;

/* A DUE term or a GATE. Fields of the other kind stay zero. */
typedef struct {
    uint8_t kind; /* zxp_clause_kind_t */
    char id[ZXP_ID_MAX + 1u];
    /* DUE */
    uint8_t payer, payee; /* 0-based party indices */
    char asset[4];
    int64_t amount;  /* > 0 */
    uint32_t date;   /* due by, YYYYMMDD */
    int64_t penalty; /* fixed amount owed on breach; 0 = none */
    uint8_t after;   /* 1 + index of an earlier clause; 0 = none */
    /* GATE */
    uint8_t op; /* zxp_gate_op_t */
    uint8_t n_ref;
    uint8_t ref[ZXP_MAX_REFS]; /* indices of earlier clauses */
} zxp_clause_t;

typedef struct {
    char name[ZXP_NAME_MAX + 1u];
    uint32_t date;   /* the contract's own date */
    uint8_t doc[32]; /* SHA-256 of the prose document */
    uint8_t n_law, n_party, sign, n_lock, n_cap, n_limit, n_allow, n_deny, n_clause;
    char law[ZXP_MAX_LAWS][ZXP_JUR_MAX + 1u];
    zxp_party_t party[ZXP_MAX_PARTIES];
    zxp_span_t window;
    zxp_span_t lock[ZXP_MAX_LOCKS];
    zxp_cap_t cap[ZXP_MAX_CAPS];
    zxp_limit_t limit[ZXP_MAX_LIMITS];
    char allow[ZXP_MAX_TAGS][ZXP_JUR_MAX + 1u];
    char deny[ZXP_MAX_TAGS][ZXP_JUR_MAX + 1u];
    zxp_clause_t clause[ZXP_MAX_CLAUSES];
} zxp_contract_t;

/* Longest canonical Sutra text (bounds an emit buffer). */
#define ZXP_SUTRA_MAX                                                                              \
    (9u + ZXP_NAME_MAX + 10u + 69u + ZXP_MAX_LAWS * 11u +                                          \
     ZXP_MAX_PARTIES * (7u + ZXP_ID_MAX + ZXP_JUR_MAX + 1u) + 8u + 25u + ZXP_MAX_LOCKS * 23u +      \
     ZXP_MAX_CAPS * 28u + ZXP_MAX_LIMITS * (12u + ZXP_ID_MAX + 18u) +                              \
     2u * ZXP_MAX_TAGS * (7u + ZXP_JUR_MAX) +                                                       \
     ZXP_MAX_CLAUSES * (4u + 3u * (ZXP_ID_MAX + 1u) + 4u + 19u + 9u + 27u + 7u + ZXP_ID_MAX +      \
                        ZXP_MAX_REFS * (ZXP_ID_MAX + 1u)) +                                         \
     4u)

void zxp_clear(zxp_contract_t *c);
zxp_status_t zxp_validate(const zxp_contract_t *c);
bool zxp_equal(const zxp_contract_t *a, const zxp_contract_t *b);

zxp_status_t zxp_parse_sutra(const char *src, uint32_t len, zxp_contract_t *out, uint32_t *err_off);
zxp_status_t zxp_emit_sutra(const zxp_contract_t *c, char *out, uint32_t cap, uint32_t *n_out);
zxp_status_t zxp_parse_record(const uint8_t *rec, uint32_t len, zxp_contract_t *out,
                              uint32_t *err_off);
zxp_status_t zxp_emit_record(const zxp_contract_t *c, uint8_t *rec, uint32_t cap);

/* Helpers the evaluator and drafting share. */
bool zxp_date_ok(uint32_t yyyymmdd);
/* Days since 1970-01-01 for a valid date (proleptic Gregorian). */
int32_t zxp_days(uint32_t yyyymmdd);
/* The date `days` after `yyyymmdd`; 0 if the input or result is invalid. */
uint32_t zxp_date_add(uint32_t yyyymmdd, int32_t days);
/* Jurisdiction tag grammar: AA or AA-X..XXX (A-Z, 0-9 after the dash). */
bool zxp_jur_ok(const char *s);
/* `tag` lies inside `scope`: equal, or scope is "AA" and tag is "AA-...". */
bool zxp_jur_covers(const char *scope, const char *tag);
/* Index of a clause by id, or -1. */
int32_t zxp_find_clause(const zxp_contract_t *c, const char *id);
/* Byte offset of clause i's line in the canonical Sutra form (the "AST
 * offset" a mediation bundle cites), or -1 if c is invalid. */
int32_t zxp_clause_offset(const zxp_contract_t *c, uint32_t i);

#endif /* ZXV_POLICY_ZXP_H */
