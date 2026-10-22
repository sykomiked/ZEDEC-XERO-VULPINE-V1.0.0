/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fairground_model.h — Fairground, the cooperative market, as a model
 * (zxm.h): what the owner shares, wants and sells on the peer network, the
 * settled sales and purchases with their fees, and the swarm's market. The
 * offers, the books and the settlements are zxvd's (net.offers,
 * net.command, mesh.books, peer.status, swarm.state); the model keeps a
 * checked copy for the view and never writes a book of its own. The rules
 * are fairground_rules.h (FR1-FR5).
 *
 *   FM1 WHAT IS READ IS CHECKED. An answer is taken whole or not at all:
 *       a count that is not a decimal of at most 18 digits, a NodeID that
 *       is not 64 hex digits, a resource or role zxvd does not name, a
 *       settlement whose fee is not the assurance fee of its price (FR1),
 *       whose four parts are not FR5's split, or whose paid is not price +
 *       fee for a purchase (0 for a sale), is refused (ZXM_E_PROTOCOL) and
 *       the model keeps what it had.
 *   FM2 THE OWNER'S LIMITS. A want's price may not pass the owner's
 *       ceiling, a sell's price may not fall below the owner's floor, and
 *       no offer may hold more units than the owner's most; each limit is
 *       1..FAIRGROUND_NINE, and until the owner sets them nothing is
 *       allowed. The decision is FR2 on the margins the model works out.
 *   FM3 THE OWNER APPROVES. fairground_share, fairground_want and
 *       fairground_sell only check an offer and hold it (fairground_t
 *       held) with a notice; nothing is sent. Only fairground_approve
 *       sends it (net.command), and fairground_cancel drops it. One offer
 *       waits at a time. Fairground never sends "off", "lan", "online" or
 *       "peer": networking stays as the owner left it (off by default).
 *   FM4 THE WALLET AFTER. A want's notice shows the wallet if it settles
 *       at the ceiling: coffer.balance(wallet, -(price + fee)), the fee
 *       FR1's; a sell's shows coffer.balance(wallet, price) (the buyer
 *       pays the fee). Shown only once mesh.books has been read.
 *   FM5 THE RECORDS. Each offer the owner was asked about is kept in a
 *       bounded list of FAIRGROUND_RECORDS (the oldest dropped first),
 *       each fitting FR4: waiting, then sent, refused by zxvd or
 *       cancelled. The price per unit across the sent wants or sells is
 *       FR3 over them.
 *
 * No drawing, no allocation, integers only. */
#ifndef FAIRGROUND_MODEL_H
#define FAIRGROUND_MODEL_H

#include "coffer_model.h"
#include "fairground_rules.h"
#include "zxm.h"

#define FAIRGROUND_WANTS   16u  /* net.offers' wants */
#define FAIRGROUND_PENDING 32u  /* net.offers' outstanding */
#define FAIRGROUND_HISTORY 16u  /* peer.status' last settlements */
#define FAIRGROUND_RECORDS 32u  /* FM5 */
#define FAIRGROUND_NODE    64u  /* hex digits of a NodeID */
#define FAIRGROUND_CMD_MAX 255u /* net.command's maxLength */
#define FAIRGROUND_ANSWER  31u
#define FAIRGROUND_NOTICE  255u

typedef enum { FAIRGROUND_SHARE = 1, FAIRGROUND_WANT = 2, FAIRGROUND_SELL = 3 } fairground_kind_t;
typedef enum { FAIRGROUND_COMPUTE = 1, FAIRGROUND_STORAGE = 2 } fairground_res_t;
typedef enum {
    FAIRGROUND_WAITING = 0,
    FAIRGROUND_SENT = 1,
    FAIRGROUND_REFUSED_BY_ZXVD = 2,
    FAIRGROUND_CANCELLED = 3,
} fairground_status_t;

typedef struct {
    int64_t slot;
    int64_t resource; /* fairground_res_t, or 3 for zxvd's "other" */
    int64_t units_left, max_price;
} fairground_want_t;

typedef struct {
    bool sell; /* else a buy */
    char peer[FAIRGROUND_NODE + 1];
    int64_t units, seq, tries, want;
} fairground_pending_t;

typedef struct {
    bool buyer; /* else this node sold */
    char peer[FAIRGROUND_NODE + 1];
    int64_t price, fee, paid;
    int64_t part[FAIRGROUND_PARTS];
} fairground_settlement_t;

typedef struct {
    int64_t offer; /* its number, 0..FAIRGROUND_OFFER_MAX, wrapping */
    fairground_kind_t kind;
    fairground_res_t resource;
    int64_t units, price; /* a share: units compute, price storage bytes */
    fairground_status_t status;
} fairground_record_t;

typedef struct {
    bool pending;
    fairground_kind_t kind;
    fairground_res_t resource;
    int64_t units, price, fee;
    bool projected; /* wallet_after is known (FM4) */
    int64_t wallet_after;
    char peer[FAIRGROUND_NODE + 1];
    char cmd[FAIRGROUND_CMD_MAX + 1];
    uint32_t record; /* its place in rec */
} fairground_held_t;

typedef struct {
    zxm_link_t *link;
    /* FM2: the owner's limits, 0 until set */
    int64_t ceiling, floor_price, most_units;
    /* net.offers */
    bool offers_known;
    int64_t share_compute, share_storage;
    fairground_want_t want[FAIRGROUND_WANTS];
    uint32_t nwant;
    fairground_pending_t out[FAIRGROUND_PENDING];
    uint32_t nout;
    /* mesh.books */
    bool books_known;
    int64_t wallet, fees, sold, bought;
    /* peer.status */
    bool peer_known, serving, book_ok;
    int64_t peer_price, peer_wallet, fees_paid, peer_sold, peer_bought, peer_refused;
    fairground_settlement_t settle[FAIRGROUND_HISTORY];
    uint32_t nsettle;
    /* swarm.state: the swarm's market */
    bool market_known;
    int64_t money_supply, pot, levied, market_fees;
    /* FM3, FM5 */
    fairground_held_t held;
    fairground_record_t rec[FAIRGROUND_RECORDS];
    uint32_t nrec;
    int64_t next_offer;
    char answer[FAIRGROUND_ANSWER + 1]; /* net.command's last result */
    char notice[FAIRGROUND_NOTICE + 1];
} fairground_t;

void fairground_init(fairground_t *f, zxm_link_t *link);

/* FM2: the owner's ceiling for a want's price, floor for a sell's price and
 * most units for one offer, each 1..FAIRGROUND_NINE; ZXM_E_ARG otherwise
 * (nothing changed). */
int fairground_set_limits(fairground_t *f, int64_t ceiling, int64_t floor_price,
                          int64_t most_units);

/* FM1: net.offers, mesh.books, peer.status and swarm.state. */
int fairground_read_offers(fairground_t *f);
int fairground_read_books(fairground_t *f);
int fairground_read_peer(fairground_t *f);
int fairground_read_market(fairground_t *f);

/* FM3: check an offer and hold it for the owner; nothing is sent. */
int fairground_share(fairground_t *f, int64_t compute, int64_t storage);
int fairground_want(fairground_t *f, fairground_res_t resource, int64_t units, int64_t max_price);
int fairground_sell(fairground_t *f, fairground_res_t resource, int64_t units, int64_t price,
                    const char *buyer);
/* FM3: send the held offer (net.command); its record says what zxvd did. */
int fairground_approve(fairground_t *f);
int fairground_cancel(fairground_t *f);

/* FM5: FR3 over the sent records of one kind (want or sell); ZXM_E_STATE
 * when none was sent, ZXM_E_ARG for another kind. */
int fairground_unit_price_of(const fairground_t *f, fairground_kind_t kind, int64_t *num,
                             int64_t *den);
/* FM5: every record fits FR4. */
bool fairground_records_fit(const fairground_t *f);

#endif
