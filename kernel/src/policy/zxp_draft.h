/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* zxp_draft.h — drafting plug-ins: templates, not advice.
 *
 * A template turns a handful of parameters into two things:
 *   1. a valid contract rule layer (zxp_contract_t) with DOC left all zero;
 *   2. a Markdown prose SKELETON for people to complete, with every
 *      section a lawyer must write marked "[TO BE COMPLETED BY COUNSEL]".
 * The finished prose is hashed (SHA-256) by the host and the digest goes in
 * DOC; from then on every transfer under the contract must carry it.
 *
 * Templates: services, sale, loan, memorandum of understanding, bilateral
 * accord between states. Amounts are fixed numbers the parties choose; a
 * template never computes interest, fees or anything a jurisdiction rules
 * on. A family label for the governing jurisdiction is printed as
 * information only, from a short built-in table after the University of
 * Ottawa JuriGlobe classification; check it there before relying on it.
 */
#ifndef ZXV_POLICY_ZXP_DRAFT_H
#define ZXV_POLICY_ZXP_DRAFT_H

#include "zxp.h"

typedef enum {
    ZXP_TPL_SERVICES = 1, /* A (client) pays B (provider) in installments  */
    ZXP_TPL_SALE = 2,     /* A (buyer) pays B (seller) in installments     */
    ZXP_TPL_LOAN = 3,     /* A (lender) advances, B repays in installments */
    ZXP_TPL_MOU = 4,      /* no payment terms; non-binding by its text     */
    ZXP_TPL_BILATERAL = 5 /* two states; optional contributions A -> B     */
} zxp_template_t;

typedef struct {
    const char *name; /* contract name, COBOL-style, 1..30 */
    uint32_t date;    /* YYYYMMDD */
    const char *law;  /* governing jurisdiction tag */
    const char *a_id, *a_jur, *b_id, *b_jur;
    const char *asset;      /* three capital letters; ignored for MOU     */
    int64_t amount;         /* total to be paid (LOAN: amount advanced)  */
    int64_t repay;          /* LOAN only: total to be repaid, >= amount  */
    uint8_t installments;   /* 1..12; 0 with amount 0 for BILATERAL/MOU  */
    uint32_t first_due;     /* YYYYMMDD of the first installment          */
    uint16_t interval_days; /* between installments, 1..366               */
    int64_t penalty;        /* fixed sum per installment on breach; 0     */
    uint32_t window_to;     /* last date transfers are allowed            */
} zxp_draft_params_t;

/* Build the rule layer. DOC is all zero until the host hashes the prose. */
zxp_status_t zxp_draft(zxp_template_t tpl, const zxp_draft_params_t *p, zxp_contract_t *out);

/* The prose skeleton for a contract built by zxp_draft (NUL-terminated
 * Markdown). Returns its length, or 0 on bad input or a short buffer. */
uint32_t zxp_draft_prose(zxp_template_t tpl, const zxp_contract_t *c, char *out, uint32_t cap);

/* Informational legal-family bits for a jurisdiction tag (subdivision first,
 * then country). 0 when the table has no entry. */
#define ZXP_FAM_CIVIL     0x01u
#define ZXP_FAM_COMMON    0x02u
#define ZXP_FAM_MUSLIM    0x04u
#define ZXP_FAM_CUSTOMARY 0x08u
#define ZXP_FAM_OTHER     0x10u /* e.g. Jewish or Hindu personal law */
uint8_t zxp_family(const char *jur);
/* "civil law, customary law" etc. into out; returns the length. */
uint32_t zxp_family_text(uint8_t fam, char *out, uint32_t cap);

#endif /* ZXV_POLICY_ZXP_DRAFT_H */
