/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mercer_model.h — Mercer, a small shop, as a model (zxm.h): a catalogue
 * of products (cost, margin, price, stock) kept in a file of the Hoard,
 * sales rung up at the till and posted to the owner's books with a
 * numbered receipt saved and printed, sales on a customer's credit inside
 * a declared limit, a stocktake by product and place, and listings shown
 * beside the market's offers with the seller's card. Composed of its
 * parts' models: the till and the books are Tallystick's (and through it
 * Coffer's), the market Fairground's and the card Talisman's.
 *
 *   MM1 MONEY IS INTEGERS. Every amount is an int64 count of minor units
 *       of the books' currency; the rules are mercer_rules.h (MR1-MR6). A
 *       value that does not fit is refused, never rounded, cut or wrapped.
 *   MM2 THE CATALOGUE. A product has a category (1..9999), a code
 *       (1..999999, one product per code), a name, its cost, the owner's
 *       margin mnum / mden over cost and its stock; its price is MR2 and
 *       the factor the margin puts on cost, 1 + mnum / mden, is
 *       tally.ratio_add, reduced. It is kept only when MR5 says its node
 *       fits.
 *   MM3 THE TILL. mercer_set_till names, on the owner's word, the issuer
 *       account counter sales are paid from, the owner's holder account
 *       they are paid into and the tax rate (taxn / taxd, 0..100%).
 *   MM4 A SALE RUNG UP (mercer_ring) is checked first: the stock after it
 *       is MR1 (never below zero), its amount tallystick.taxed_line_total
 *       (qty * price and its tax), and a sale on credit MR3 against the
 *       customer's declared limit. It is drafted and issued through
 *       Tallystick, so its number is tallystick.next_doc_number's. Nothing
 *       is sent.
 *   MM5 TWO SOURCES (MR6). The buyer's receipt (mercer_receipt) is joined
 *       with the till's record: a receipt that disagrees makes the sale
 *       BOTH, held; it is never posted until the owner rules
 *       (mercer_rule): the till's record stands (TRUE), or the sale is
 *       voided (FALSE, nothing posted, its number not used again).
 *   MM6 POSTED ONLY ON THE OWNER'S WORD. mercer_post takes owner_confirmed
 *       and refuses without it (ZXM_E_STATE, nothing sent). A cash sale is
 *       posted (ledger.post, from the counter to the owner's account,
 *       through Tallystick and Coffer: coffer.transfer_ok), read back from
 *       the books (ledger.entries) and must be paid in full
 *       (tallystick.paid_in_full) before its receipt is numbered. A sale
 *       on credit moves no money: it goes on the customer's tab, and a
 *       payment against the tab (mercer_credit_payment) is a posting of
 *       its own, on the owner's word. The stock changes only after the
 *       posting succeeded.
 *   MM7 SAVED AND PRINTED on the owner's word: the receipt through
 *       Tallystick (a new file, never over an earlier one, then
 *       print.submit), the catalogue as a file of the Hoard (files.write,
 *       replaced; files.read, all or nothing).
 *   MM8 LISTINGS are shown, never sent: a product may be listed when
 *       fairground.offer_allowed says so under the owner's market limits
 *       (the price over the floor, the stock under the most units), with
 *       its assurance fee (fairground.assurance_fee), a second currency at
 *       the rate the owner types (tally.fx_amount), the market's offers
 *       (net.offers) and the seller's tier and whether the next one is
 *       earned (player.card, talisman.earned). Tiers give no priority,
 *       discount or credit, and the listing says so.
 *   MM9 THE DAY'S SALES. The price per unit across the posted sales is
 *       fairground.offer_unit_price, exact; a stocktake's cells are MR4.
 *
 * zxvd has no shop catalogue, no tab of a customer's credit and no
 * listing of its own, so these are the model's, the catalogue kept in a
 * file; none is invented. No drawing, no allocation, integers only. */
#ifndef MERCER_MODEL_H
#define MERCER_MODEL_H

#include "fairground_model.h"
#include "mercer_rules.h"
#include "talisman_model.h"
#include "tallystick_model.h"
#include "zxm.h"

#define MERCER_PRODUCTS     64u
#define MERCER_NAME_MAX     47u
#define MERCER_CUSTOMERS    16u
#define MERCER_DAY_SALES    64u
#define MERCER_FILE_MAX     127u
#define MERCER_CAT_MAX      8192u
#define MERCER_LISTING_MAX  511u
#define MERCER_NOTICE_MAX   255u
#define MERCER_COUNT_ROWS   1024u /* a stocktake's rows */
#define MERCER_CATEGORY_MAX INT64_C(9999)
#define MERCER_CODE_MAX     INT64_C(999999)

typedef struct {
    int64_t category, code;
    char name[MERCER_NAME_MAX + 1];
    int64_t cost, mnum, mden;
    int64_t price;              /* MR2 */
    int64_t factor_n, factor_d; /* 1 + mnum / mden, reduced (tally.ratio_add) */
    int64_t stock;
} mercer_product_t;

typedef struct {
    uint32_t account; /* the customer's issuer account */
    int64_t limit;    /* declared, on the owner's word */
    int64_t owed;     /* the tab */
} mercer_customer_t;

typedef struct {
    bool open;
    int64_t number; /* the till's (tallystick.next_doc_number) */
    uint32_t product;
    int64_t qty, amount;
    int32_t customer; /* a credit customer's place, -1 a cash sale */
    bool have_receipt;
    int64_t rqty, ramount;
    lpres_state_t state; /* MR6 */
    bool ruled;          /* the owner's ruling on a BOTH */
} mercer_sale_t;

typedef struct {
    zxm_link_t *link;
    tallystick_t till;   /* MM4, MM6, MM7: Tallystick, and Coffer through it */
    fairground_t market; /* MM8 */
    talisman_t card;     /* MM8 */
    bool till_set;
    uint32_t counter, payee;
    int64_t taxn, taxd;
    mercer_product_t p[MERCER_PRODUCTS];
    uint32_t np;
    mercer_customer_t c[MERCER_CUSTOMERS];
    uint32_t nc;
    mercer_sale_t sale;
    int64_t day_amount[MERCER_DAY_SALES], day_qty[MERCER_DAY_SALES];
    uint32_t nday;
    char listing[MERCER_LISTING_MAX + 1];
    char notice[MERCER_NOTICE_MAX + 1];
} mercer_t;

void mercer_init(mercer_t *m, zxm_link_t *link);
/* The books' accounts (ledger.accounts, ledger.balance through Coffer). */
int mercer_load(mercer_t *m);

/* MM3, on the owner's word. */
int mercer_set_till(mercer_t *m, uint32_t counter, uint32_t payee, int64_t taxn, int64_t taxd,
                    bool owner_confirmed);
/* MM2: a product, its price worked out; ZXM_E_ARG when it does not fit. */
int mercer_add_product(mercer_t *m, int64_t category, int64_t code, const char *name, int64_t cost,
                       int64_t mnum, int64_t mden, int64_t stock);
/* The product with code, or -1. */
int32_t mercer_find(const mercer_t *m, int64_t code);
/* MR3: a customer's declared limit (an issuer account), on the owner's
 * word; a lower limit than the tab already owed is refused. */
int mercer_set_credit(mercer_t *m, uint32_t account, int64_t limit, bool owner_confirmed);
/* The customer with account, or -1. */
int32_t mercer_customer(const mercer_t *m, uint32_t account);

/* MM4: ring up qty of the product with code; on_credit names the
 * customer's account, 0 for a cash sale. Nothing is sent. */
int mercer_ring(mercer_t *m, int64_t code, int64_t qty, uint32_t on_credit);
/* MM5: what the buyer's receipt says of the open sale. */
int mercer_receipt(mercer_t *m, int64_t rqty, int64_t ramount);
/* MM5: the owner's ruling on a sale held BOTH. */
int mercer_rule(mercer_t *m, bool till_stands, bool owner_confirmed);
/* MM6: post the open sale; *entry is its ledger entry (0 on credit). */
int mercer_post(mercer_t *m, bool owner_confirmed, uint64_t *entry);
/* MM6: a payment of amount (1..the tab) against a customer's tab. */
int mercer_credit_payment(mercer_t *m, uint32_t account, int64_t amount, bool owner_confirmed,
                          uint64_t *entry);

/* MM7 */
int mercer_save_receipt(mercer_t *m, const char *name, bool owner_confirmed);
int mercer_print_receipt(mercer_t *m, const char *uri, bool owner_confirmed);
int mercer_save_catalogue(mercer_t *m, const char *name, bool owner_confirmed);
int mercer_load_catalogue(mercer_t *m, const char *name);

/* MM8: the owner's market limits (fairground_set_limits), on the owner's
 * word; the market's offers; the seller's card on day (days since 1970). */
int mercer_set_market_limits(mercer_t *m, int64_t ceiling, int64_t floor_price, int64_t most_units,
                             bool owner_confirmed);
int mercer_read_offers(mercer_t *m);
int mercer_read_card(mercer_t *m, uint32_t day);
/* MM8: the listing of the product with code into m->listing; rnum / rden
 * a second currency's rate (0 / 0: none). ZXM_E_STATE when it may not be
 * offered. */
int mercer_list(mercer_t *m, int64_t code, int64_t rnum, int64_t rden);

/* MM9: the price per unit across the day's posted sales, reduced. */
int mercer_unit_price(const mercer_t *m, int64_t *num, int64_t *den);
/* MM9: a stocktake's n rows (product code, place, units counted): each
 * row's cell (MR4) into cell, and *differ the catalogue products whose
 * counted units, over every place, are not their stock. */
int mercer_stocktake(const mercer_t *m, const int64_t *product, const int64_t *place,
                     const int64_t *qty, uint32_t n, int64_t *cell, uint32_t *differ);

#endif
