/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztw_internal.h — Tri-Witness's parsed forms of the Sutra-Clear cycle
 * record and journal export. Not installed. Integer code only (R11). */
#ifndef ZTW_INTERNAL_H
#define ZTW_INTERNAL_H

#include "ztwit.h"

#define ZTW_MAX_CCY       16u
#define ZTW_HOUSE_DOMAIN  "sutra-clear"
#define ZTW_ZSC_REC_CYCLE 1u

typedef struct {
    ztw_field *f;
    size_t n, cap;
    int oom;
} ztw_fieldlog;

typedef struct {
    uint8_t source;
    char msg_id[36];
    char cre_dt_tm[36];
    char e2e[36];
    char ccy[4];
    int64_t amount;
    char dbtr_agt[12], cdtr_agt[12];
    char dbtr_nm[71], dbtr_acct[35];
    char cdtr_nm[71], cdtr_acct[35];
} ztw_instr;

typedef struct {
    char e2e[36];
    uint32_t n;
    ztw_instr *v;
} ztw_held;

typedef struct {
    char e2e[36];
    uint8_t choice;
    uint8_t group[32], chosen[32], op_id[32];
    const uint8_t *sig; /* ZIND_SIG_BYTES inside the blob */
} ztw_res;

typedef struct {
    uint8_t c;
    uint16_t p;
    int64_t net;
} ztw_pos;

typedef struct {
    uint8_t c;
    uint16_t p;
    uint64_t gross, rem_in, fee, rem_out, split[4];
} ztw_fee;

typedef struct {
    uint8_t kind, c, b;
    uint16_t p;
    int64_t amount;
    uint64_t entry;
    char from[64], to[64], ref[36];
} ztw_post;

typedef struct {
    char name[64];
    uint8_t c;
    int64_t open, close;
} ztw_bal;

typedef struct {
    uint8_t kind;
    char msg_id[36];
    uint8_t dig[32];
} ztw_iso;

typedef struct {
    const uint8_t *blob;
    size_t blob_len, bad_at;
    char house[9];
    uint8_t key_id[32];
    uint32_t no;
    char cycle_id[17];
    char settle_dt[36];
    uint8_t backend;
    uint32_t n_ccy;
    char ccy[ZTW_MAX_CCY][4];
    uint8_t minor[ZTW_MAX_CCY];
    uint32_t n_part;
    char (*code)[12];
    uint32_t n_instr;
    ztw_instr *instr;
    uint32_t n_wd;
    ztw_instr *wd;
    uint32_t n_held;
    ztw_held *held;
    uint32_t n_res;
    ztw_res *res;
    uint32_t n_pos;
    ztw_pos *pos;
    uint32_t n_fee;
    ztw_fee *fee;
    uint32_t n_post;
    ztw_post *post;
    uint32_t n_bal;
    ztw_bal *bal;
    uint64_t first, last, head;
    uint8_t jdig[32];
    uint32_t n_iso;
    ztw_iso *iso;
} ztw_cycle;

typedef struct {
    char name[64];
    char ccy[12];
    uint8_t kind;
} ztw_jacct;

typedef struct {
    uint64_t id;
    uint32_t from, to;
    int64_t amount;
    char ref[40];
} ztw_jpost;

typedef struct {
    ztw_jacct *acct;
    uint32_t n_acct;
    ztw_jpost *post;
    size_t n_post;
} ztw_journal;

/* ztw_parse.c */
int32_t ztw_parse_cycle(const uint8_t *b, size_t n, ztw_cycle *c, ztw_fieldlog *log);
void ztw_cycle_free(ztw_cycle *c);
void ztw_fieldlog_free(ztw_fieldlog *l);

#define ZTW_FRAG_CYCLE      1 /* a whole cycle record: rec = its first record */
#define ZTW_FRAG_BADKIND    2 /* record rec is not a cycle fragment           */
#define ZTW_FRAG_BROKEN     3 /* record rec breaks the fragment sequence      */
#define ZTW_FRAG_INCOMPLETE 4 /* the chain ends inside a cycle record         */
typedef int32_t (*ztw_frag_cb)(void *arg, int what, uint32_t rec, uint32_t cyc,
                               const uint8_t *blob, size_t len, uint32_t nrec);
int32_t ztw_fragments(const zind_chain *ch, ztw_frag_cb cb, void *arg);

int32_t ztw_parse_journal(const uint8_t *b, size_t n, ztw_journal *j);
void ztw_journal_free(ztw_journal *j);

#endif /* ZTW_INTERNAL_H */
