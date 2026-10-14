/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsc_internal.h — shared pieces of Sutra-Clear's sources: field checks, a
 * growable little-endian writer, the netting backends and the journal
 * builder. Not installed. Integer code only (R11). */
#ifndef ZSC_INTERNAL_H
#define ZSC_INTERNAL_H

#include "zsclear.h"

/* ---- field checks (zsc_parse.c) ---- */
int zsc_code_ok(const char *s);          /* 1..11 of [A-Z0-9]          */
int zsc_text_ok(const char *s, size_t max); /* 1..max of 0x21..0x7e    */
int zsc_name_ok(const char *s);          /* 1..70 bytes UTF-8, no controls */
int zsc_dt_ok(const char *s);            /* the ISO 8601 subset         */
int zsc_bic_ok(const char *s);           /* 8 or 11, AAAABBCC[DDD]      */
/* The ISO 4217 minor units of a payable currency, or -1. */
int zsc_ccy_minor(const char *ccy);
/* Canonical bytes: content only (digest) or the whole instruction. */
void zsc_wr_content(zind_wr *w, const zsc_instr *in);
void zsc_wr_instr(zind_wr *w, const zsc_instr *in);
#define ZSC_INSTR_WIRE_MAX 512u

/* ---- growable writer, same encoding as zind_wr ---- */
typedef struct {
    uint8_t *buf;
    size_t len, cap;
    int bad; /* allocation failed or a string was too long */
} zsc_dbuf;
void zsc_db_u8(zsc_dbuf *d, uint8_t v);
void zsc_db_u16(zsc_dbuf *d, uint16_t v);
void zsc_db_u32(zsc_dbuf *d, uint32_t v);
void zsc_db_u64(zsc_dbuf *d, uint64_t v);
void zsc_db_bytes(zsc_dbuf *d, const void *p, size_t n);
void zsc_db_str(zsc_dbuf *d, const char *s);
void zsc_db_free(zsc_dbuf *d);

/* ---- netting (zsc_net.c) ---- */
typedef struct {
    uint32_t from, to; /* participant indices */
    int64_t amount;    /* 1..ZXV_AMOUNT_MAX   */
} zsc_oblig;

/* Multilateral net positions of n_parts participants from n obligations in
 * one currency, through the treaty engine (zxv_treaty_run, OP_NET) in
 * batches of at most 16 parties and 64 obligations, summed with checked
 * int64 and cross-checked against the plain sum of each participant's
 * credits minus debits. pos[] is written only on ZXV_OK. ZXV_E_RANGE on an
 * overflow; the treaty engine's own refusals pass through. */
int32_t zsc_net_treaty(const char *ccy, uint32_t n_parts, const zsc_oblig *obs, size_t n,
                       int64_t *pos);
/* The same through zxv_netting_* on ctx (codes[] names the participants). */
int32_t zsc_net_zxv(zxv_ctx *ctx, const char *cycle_id, const char *ccy, uint32_t n_parts,
                    const char (*codes)[12], const zsc_oblig *obs, size_t n, int64_t *pos);

/* ---- journal (zsc_net.c) ---- */
/* The ledger's postings, entry order, as read through the cap. */
typedef struct {
    uint64_t id;
    uint32_t from, to;
    int64_t amount;
    char ref[40];
} zsc_jpost;
typedef struct {
    char name[64];
    char ccy[12];
    int32_t kind;
} zsc_jacct;
typedef struct {
    zsc_jacct *acct;
    uint32_t n_acct;
    zsc_jpost *post;
    size_t n_post;
} zsc_journal;
int32_t zsc_journal_read(const zxv_cap *cap, zsc_journal *out);
void zsc_journal_free(zsc_journal *j);
/* SHA3-256 (zind_subject "sutra-clear/journal") of the postings with
 * id <= upto: "ZSJP1" then per posting u64 id, str from name, str to name,
 * str ccy, i64 amount, str ref. */
int32_t zsc_journal_digest(const zsc_journal *j, uint64_t upto, uint8_t out[32]);

#endif /* ZSC_INTERNAL_H */
