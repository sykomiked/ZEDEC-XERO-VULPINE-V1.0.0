/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vintner_model.h — Vintner, the vineyard and wine ledger, as a model
 * (zxm.h): lots from harvest through must and barrel to bottle, their
 * volumes and losses, the cellar probe, barrel lot labels from the grant
 * engine, and sales in the owner's books. The books are zxvd's (ledger.*);
 * the labels are zxvd's (grants.status). The model keeps a copy for the
 * view and never writes a book of its own.
 *
 *   VN1 A SALE'S VALUE is bottles * price, the price per bottle in integer
 *       minor units of the books' currency: bottles and price 0 ..
 *       VINTNER_NINE each (Sutra's 9(9)), else VINTNER_REFUSED, never
 *       rounded or wrapped (vintner_sale_value; sale.sutra, sale.cpp).
 *   VN2 VOLUME IS CONSERVED. A lot moves harvest -> must -> barrel ->
 *       bottled, each step in millilitres: what came in minus what went on
 *       minus what was lost must be exactly 0 (vintner_flow_gap; flow.tb),
 *       or the step is refused and nothing changes. A lot's whole chain
 *       holds the same way: harvest - now - every loss = 0.
 *   VN3 THE CELLAR PROBE counts sixteenths of a degree, VINTNER_RAW_LO ..
 *       VINTNER_RAW_HI (-55 to 125 degrees); its reading in tenths of a
 *       degree is floor(raw * 10 / 16), and a count outside the range is
 *       refused (vintner_probe_tenths; probe.fs, probe.kron).
 *   VN4 A LOT'S RECORD, ^ZXVINTNER, holds a lot number of 9 digits, a
 *       vintage of 4, a stage of 1, bottles of 9 and a volume of 12 (ml),
 *       sign aside (vintner_lot_fits; lot.myra).
 *   VN5 THE OWNER CONFIRMS A SALE. vintner_propose_sale checks a sale and
 *       holds it (vintner_t.held) with a notice for the view; it sends
 *       nothing. Only vintner_confirm_sale posts it (ledger.post, from the
 *       buyers' issuer account to the cellar's sales account), and
 *       vintner_cancel_sale drops it. One sale waits at a time. A sale is
 *       a transfer Coffer allows (coffer_transfer_ok): its value 1 ..
 *       COFFER_AMOUNT_MAX, balanced; the buyers' account is an issuer, so
 *       its after and room are taken at the edge, 0.
 *   VN6 WHAT IS READ IS CHECKED. A balance whose equity is not debit -
 *       credit, sales entries that do not add up to the balance, a number
 *       that is not a 64-bit decimal, or a label that does not fit is
 *       refused (ZXM_E_PROTOCOL) and the model keeps what it had. Nothing
 *       is changed before it is checked.
 *
 * No drawing, no allocation, integers only. */
#ifndef VINTNER_MODEL_H
#define VINTNER_MODEL_H

#include "coffer_model.h"
#include "zxm.h"

#define VINTNER_LOTS       32u
#define VINTNER_ENTRIES    128u
#define VINTNER_LABEL_MAX  63u
#define VINTNER_STATUS_MAX 31u
#define VINTNER_REF_MAX    35u /* ledger.post reference */
#define VINTNER_CCY_MAX    15u
#define VINTNER_NOTICE_MAX 255u

#define VINTNER_NINE        INT64_C(999999999)    /* 9 digits: Sutra's 9(9), MYRA's i9 */
#define VINTNER_VOLUME_MAX  INT64_C(999999999999) /* 12 digits of ml, MYRA's i12 */
#define VINTNER_VINTAGE_MAX 9999
#define VINTNER_RAW_LO      (-880) /* -55 degrees in sixteenths */
#define VINTNER_RAW_HI      2000   /* 125 degrees */
#define VINTNER_REFUSED     INT64_MIN

#define VINTNER_SALES_NAME  "vintner sales"
#define VINTNER_BUYERS_NAME "vintner buyers"

typedef enum {
    VINTNER_HARVEST = 1,
    VINTNER_MUST = 2,
    VINTNER_BARREL = 3,
    VINTNER_BOTTLED = 4,
} vintner_stage_t;

typedef struct {
    uint32_t lot;
    int64_t vintage;
    vintner_stage_t stage;
    int64_t harvest; /* ml picked */
    int64_t volume;  /* ml now */
    int64_t lost;    /* ml lost over every step */
    int64_t bottles; /* filled */
    int64_t sold;    /* posted sales */
    bool labelled;   /* the grant engine knows the lot */
    bool backed;
    char label[VINTNER_LABEL_MAX + 1];
    char status[VINTNER_STATUS_MAX + 1];
} vintner_lot_t;

typedef struct {
    bool pending;
    uint32_t lot;
    int64_t bottles, price, value;
    char ref[VINTNER_REF_MAX + 1];
} vintner_sale_t;

typedef struct {
    zxm_link_t *link;
    bool books; /* the two accounts are known */
    uint32_t sales, buyers;
    char ccy[VINTNER_CCY_MAX + 1];
    int64_t revenue;                /* the sales account's balance, minor units */
    int64_t entry[VINTNER_ENTRIES]; /* the sales account's journal, oldest first */
    uint32_t ne;
    vintner_lot_t lot[VINTNER_LOTS];
    uint32_t nlots;
    vintner_sale_t held; /* VN5 */
    bool probe_ok;
    int64_t probe_tenths; /* the last reading */
    char notice[VINTNER_NOTICE_MAX + 1];
} vintner_t;

/* The rules, as the app's logic says them (vintner.zxapp). */

/* VN1: bottles * price; VINTNER_REFUSED outside 0 .. VINTNER_NINE each. */
int64_t vintner_sale_value(int64_t bottles, int64_t price);
/* VN2: before - after - loss, exact; VINTNER_REFUSED past 64 bits. */
int64_t vintner_flow_gap(int64_t before, int64_t after, int64_t loss);
/* VN3: floor(raw * 10 / 16); VINTNER_REFUSED outside VINTNER_RAW_LO .. HI. */
int64_t vintner_probe_tenths(int64_t raw);
/* VN4: every value fits its ^ZXVINTNER field, sign aside. */
bool vintner_lot_fits(int64_t lot, int64_t vintage, int64_t stage, int64_t bottles, int64_t volume);

void vintner_init(vintner_t *v, zxm_link_t *link);

/* The books: ledger.accounts, then ledger.open_account for whichever of
 * the sales (holder) and buyers (issuer) accounts in ccy is missing, then
 * the sales balance. The currency must be enabled already (Coffer). */
int vintner_open_books(vintner_t *v, const char *ccy);
/* ledger.balance of the sales account into v->revenue (VN6). */
int vintner_revenue(vintner_t *v);
/* ledger.entries of the sales account into v->entry (VN6). */
int vintner_sales(vintner_t *v);

/* Lots (VN2, VN4). */
int vintner_add_lot(vintner_t *v, uint32_t lot, int64_t vintage, int64_t harvest_ml);
/* harvest -> must, must -> barrel: after_ml goes on, loss_ml is lost. */
int vintner_advance(vintner_t *v, uint32_t lot, int64_t after_ml, int64_t loss_ml);
/* barrel -> bottled: bottles of bottle_ml each, loss_ml lost. */
int vintner_bottle(vintner_t *v, uint32_t lot, int64_t bottles, int64_t bottle_ml, int64_t loss_ml);
/* The whole chain of a lot conserved: harvest - volume - lost = 0. */
bool vintner_conserved(const vintner_lot_t *l);
/* The lot numbered lot, or NULL. */
const vintner_lot_t *vintner_lot(const vintner_t *v, uint32_t lot);

/* grants.status at now (unix seconds): each lot the grant engine knows
 * takes its label, status and backing; *found is how many matched. */
int vintner_labels(vintner_t *v, uint64_t now, uint32_t *found);

/* VN3: a probe count; ZXM_E_ARG (and probe_ok false) outside the range. */
int vintner_probe(vintner_t *v, int64_t raw);

/* VN5: check a sale and hold it for the owner; nothing is sent. */
int vintner_propose_sale(vintner_t *v, uint32_t lot, int64_t bottles, int64_t price,
                         const char *ref);
/* Post the held sale (ledger.post), then read the revenue; *entry is its id. */
int vintner_confirm_sale(vintner_t *v, uint64_t *entry);
int vintner_cancel_sale(vintner_t *v);

#endif
