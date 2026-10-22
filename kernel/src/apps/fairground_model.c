/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fairground_model.c — Fairground, the cooperative market (see
 * fairground_model.h). Every answer is checked whole before anything of it
 * is kept, and an offer is sent only on the owner's approval. */
#include <stdio.h>
#include <string.h>

#include "fairground_model.h"

/* the four parts of a fee, as zxvd names them (pay_assure.h B1) */
static const char *const PART_NAME[FAIRGROUND_PARTS] = {"reserve floor", "V-Bill dividend pool",
                                                        "infrastructure/node bounties",
                                                        "regenerative capital"};

/* ---- forms --------------------------------------------------------------------------- */

/* a count zxvd sends as a decimal string: 1..18 digits, no leading zero */
static bool dec_val(const char *s, int64_t *v)
{
    int64_t x = 0;
    uint32_t i = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 18) return false;
        if (i == 1 && s[0] == '0') return false;
        x = x * 10 + (s[i] - '0');
    }
    if (!i) return false;
    *v = x;
    return true;
}

/* the same with an optional minus sign ("-0" is not zxvd's) */
static bool sdec_val(const char *s, int64_t *v)
{
    if (s[0] != '-') return dec_val(s, v);
    if (!dec_val(s + 1, v) || *v == 0) return false;
    *v = -*v;
    return true;
}

static bool get_dec(const zxm_link_t *l, int32_t o, const char *k, bool sign, int64_t *v)
{
    char b[24];
    if (zxm_get_str(l, o, k, b, sizeof b) != ZXM_OK) return false;
    return sign ? sdec_val(b, v) : dec_val(b, v);
}

/* a whole JSON integer, 0..max */
static bool get_count(const zxm_link_t *l, int32_t o, const char *k, int64_t max, int64_t *v)
{
    return zxm_get_i64(l, o, k, v) == ZXM_OK && *v >= 0 && *v <= max;
}

/* a NodeID: 64 lowercase hex digits */
static bool node_ok(const char *s)
{
    uint32_t i = 0;
    if (!s) return false;
    for (; s[i]; i++)
        if (i >= FAIRGROUND_NODE || !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return i == FAIRGROUND_NODE;
}

static int64_t resource_of(const char *s)
{
    if (!strcmp(s, "compute")) return FAIRGROUND_COMPUTE;
    if (!strcmp(s, "storage")) return FAIRGROUND_STORAGE;
    if (!strcmp(s, "other")) return 3;
    return 0;
}

static const char *resource_text(fairground_res_t r)
{
    return r == FAIRGROUND_COMPUTE ? "compute" : "storage";
}

static bool in_nine(int64_t v)
{
    return v >= 1 && v <= FAIRGROUND_NINE;
}

/* ---- state ----------------------------------------------------------------------------- */

void fairground_init(fairground_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
}

int fairground_set_limits(fairground_t *f, int64_t ceiling, int64_t floor_price, int64_t most_units)
{
    if (!f || !in_nine(ceiling) || !in_nine(floor_price) || !in_nine(most_units)) return ZXM_E_ARG;
    f->ceiling = ceiling;
    f->floor_price = floor_price;
    f->most_units = most_units;
    snprintf(f->notice, sizeof f->notice,
             "Your limits: pay at most %lld a receipt, sell for at least %lld, at most %lld units "
             "an offer.",
             (long long) ceiling, (long long) floor_price, (long long) most_units);
    return ZXM_OK;
}

/* ---- FM1: net.offers ----------------------------------------------------------------- */

int fairground_read_offers(fairground_t *f)
{
    fairground_want_t w[FAIRGROUND_WANTS];
    fairground_pending_t p[FAIRGROUND_PENDING];
    int64_t compute, storage;
    char s[16];
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "net.offers");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result, sh, wa, ou;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    sh = zxm_get(l, r, "share");
    if (sh < 0 || l->tok[sh].type != W4_J_OBJ || !get_dec(l, sh, "compute", false, &compute) ||
        !get_dec(l, sh, "storage", false, &storage))
        return ZXM_E_PROTOCOL;
    wa = zxm_get(l, r, "wants");
    ou = zxm_get(l, r, "outstanding");
    if (wa < 0 || l->tok[wa].type != W4_J_ARR || ou < 0 || l->tok[ou].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t nw = zxm_len(l, wa), no = zxm_len(l, ou);
    if (nw > FAIRGROUND_WANTS || no > FAIRGROUND_PENDING) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < nw; i++) {
        int32_t e = zxm_at(l, wa, i);
        if (e < 0 || l->tok[e].type != W4_J_OBJ ||
            !get_count(l, e, "slot", FAIRGROUND_WANTS - 1, &w[i].slot) ||
            zxm_get_str(l, e, "resource", s, sizeof s) != ZXM_OK ||
            !(w[i].resource = resource_of(s)) ||
            !get_dec(l, e, "units_left", false, &w[i].units_left) ||
            !get_dec(l, e, "max_price", false, &w[i].max_price))
            return ZXM_E_PROTOCOL;
    }
    for (uint32_t i = 0; i < no; i++) {
        int32_t e = zxm_at(l, ou, i);
        if (e < 0 || l->tok[e].type != W4_J_OBJ || zxm_get_str(l, e, "role", s, sizeof s) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (!strcmp(s, "sell"))
            p[i].sell = true;
        else if (!strcmp(s, "buy"))
            p[i].sell = false;
        else
            return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, e, "peer", p[i].peer, sizeof p[i].peer) != ZXM_OK ||
            !node_ok(p[i].peer) || !get_dec(l, e, "units", false, &p[i].units) ||
            !get_dec(l, e, "seq", false, &p[i].seq) ||
            !get_count(l, e, "tries", INT64_C(4294967295), &p[i].tries) ||
            zxm_get_i64(l, e, "want", &p[i].want) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        /* a sell reserves no want; a buy reserves one of the want slots */
        if (p[i].sell ? p[i].want != -1
                      : (p[i].want < 0 || p[i].want >= (int64_t) FAIRGROUND_WANTS))
            return ZXM_E_PROTOCOL;
    }
    f->share_compute = compute;
    f->share_storage = storage;
    memcpy(f->want, w, nw * sizeof w[0]);
    f->nwant = nw;
    memcpy(f->out, p, no * sizeof p[0]);
    f->nout = no;
    f->offers_known = true;
    snprintf(f->notice, sizeof f->notice,
             "Sharing %lld compute a cycle and %lld storage bytes; %u wants open, %u receipts "
             "waiting on the other side.",
             (long long) compute, (long long) storage, nw, no);
    return ZXM_OK;
}

/* ---- FM1: mesh.books ------------------------------------------------------------------- */

/* an account id: null until the first settled request, else 0 or more */
static bool account_ok(const zxm_link_t *l, int32_t o, const char *k)
{
    int64_t v;
    int32_t t = zxm_get(l, o, k);
    if (t < 0) return false;
    if (l->tok[t].type == W4_J_NULL) return true;
    return get_count(l, o, k, INT64_C(4294967295), &v);
}

int fairground_read_books(fairground_t *f)
{
    int64_t wallet, fees, sold, bought;
    char ccy[8];
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "mesh.books");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ ||
        zxm_get_str(l, r, "currency", ccy, sizeof ccy) != ZXM_OK || strcmp(ccy, "VFV") ||
        !account_ok(l, r, "wallet_account") || !account_ok(l, r, "counterparty_account") ||
        !account_ok(l, r, "fee_account") || !get_dec(l, r, "wallet_minor", true, &wallet) ||
        !get_dec(l, r, "fees_minor", true, &fees) || !get_dec(l, r, "sold", true, &sold) ||
        !get_dec(l, r, "bought", true, &bought) || fees < 0 || sold < 0 || bought < 0)
        return ZXM_E_PROTOCOL;
    f->wallet = wallet;
    f->fees = fees;
    f->sold = sold;
    f->bought = bought;
    f->books_known = true;
    snprintf(f->notice, sizeof f->notice,
             "Wallet %lld VFV; %lld sales and %lld purchases settled; %lld paid in fees.",
             (long long) wallet, (long long) sold, (long long) bought, (long long) fees);
    return ZXM_OK;
}

/* ---- FM1: peer.status ------------------------------------------------------------------ */

/* one settlement, whole: FR1's fee, FR5's parts, paid = price + fee for a purchase */
static bool settlement_ok(const zxm_link_t *l, int32_t e, fairground_settlement_t *s)
{
    char role[8];
    int64_t want[FAIRGROUND_PARTS];
    if (e < 0 || l->tok[e].type != W4_J_OBJ ||
        zxm_get_str(l, e, "role", role, sizeof role) != ZXM_OK)
        return false;
    if (!strcmp(role, "buyer"))
        s->buyer = true;
    else if (!strcmp(role, "seller"))
        s->buyer = false;
    else
        return false;
    if (zxm_get_str(l, e, "peer", s->peer, sizeof s->peer) != ZXM_OK || !node_ok(s->peer) ||
        !get_dec(l, e, "price_minor", false, &s->price) ||
        !get_dec(l, e, "fee_minor", false, &s->fee) ||
        !get_dec(l, e, "paid_minor", false, &s->paid))
        return false;
    if (s->fee != fairground_assurance_fee(s->price)) return false; /* FR1 */
    if (s->paid != (s->buyer ? s->price + s->fee : 0)) return false;
    int32_t fp = zxm_get(l, e, "fee_parts");
    if (fp < 0 || l->tok[fp].type != W4_J_OBJ || !fairground_fee_parts(s->fee, want)) return false;
    for (uint32_t i = 0; i < FAIRGROUND_PARTS; i++)
        if (!get_dec(l, fp, PART_NAME[i], false, &s->part[i]) || s->part[i] != want[i])
            return false; /* FR5 */
    return true;
}

int fairground_read_peer(fairground_t *f)
{
    fairground_settlement_t h[FAIRGROUND_HISTORY];
    int64_t price, wallet, paid, part, sold, bought, refused, port;
    bool serving, book_ok;
    char ccy[8];
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "peer.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || zxm_get_bool(l, r, "serving", &serving) != ZXM_OK ||
        !get_count(l, r, "port", 65535, &port) || !get_dec(l, r, "price_minor", false, &price) ||
        zxm_get_str(l, r, "currency", ccy, sizeof ccy) != ZXM_OK || strcmp(ccy, "VFV") ||
        !get_dec(l, r, "wallet_minor", false, &wallet) ||
        !get_dec(l, r, "fees_paid_minor", false, &paid) ||
        !get_count(l, r, "sold", INT64_C(4294967295), &sold) ||
        !get_count(l, r, "bought", INT64_C(4294967295), &bought) ||
        !get_count(l, r, "refused", INT64_C(4294967295), &refused) ||
        zxm_get_bool(l, r, "book_ok", &book_ok) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t b = zxm_get(l, r, "buckets");
    if (b < 0 || l->tok[b].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < FAIRGROUND_PARTS; i++)
        if (!get_dec(l, b, PART_NAME[i], false, &part)) return ZXM_E_PROTOCOL;
    int32_t a = zxm_get(l, r, "history");
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, a);
    if (n > FAIRGROUND_HISTORY) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++)
        if (!settlement_ok(l, zxm_at(l, a, i), &h[i])) return ZXM_E_PROTOCOL;
    f->serving = serving;
    f->book_ok = book_ok;
    f->peer_price = price;
    f->peer_wallet = wallet;
    f->fees_paid = paid;
    f->peer_sold = sold;
    f->peer_bought = bought;
    f->peer_refused = refused;
    memcpy(f->settle, h, n * sizeof h[0]);
    f->nsettle = n;
    f->peer_known = true;
    snprintf(f->notice, sizeof f->notice,
             "%s at %lld VFV an answer; %lld sold, %lld bought, %lld refused; the peer book %s.",
             serving ? "Serving" : "Not serving", (long long) price, (long long) sold,
             (long long) bought, (long long) refused, book_ok ? "verifies" : "does NOT verify");
    return ZXM_OK;
}

/* ---- FM1: swarm.state's market ---------------------------------------------------------- */

static bool get_big(const zxm_link_t *l, int32_t o, const char *k, int64_t *v)
{
    uint64_t u;
    if (zxm_get_u64(l, o, k, &u) != ZXM_OK || u > (uint64_t) INT64_MAX) return false;
    *v = (int64_t) u;
    return true;
}

int fairground_read_market(fairground_t *f)
{
    int64_t supply, pot, levied, fees;
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "swarm.state");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || !get_big(l, r, "money_supply", &supply) ||
        !get_big(l, r, "pot", &pot) || !get_big(l, r, "levied", &levied) ||
        !get_big(l, r, "fees", &fees))
        return ZXM_E_PROTOCOL;
    f->money_supply = supply;
    f->pot = pot;
    f->levied = levied;
    f->market_fees = fees;
    f->market_known = true;
    snprintf(f->notice, sizeof f->notice,
             "The swarm's market: %lld in supply, %lld in the pot, %lld levied, %lld in fees.",
             (long long) supply, (long long) pot, (long long) levied, (long long) fees);
    return ZXM_OK;
}

/* ---- FM3, FM5: offers held for the owner -------------------------------------------------- */

/* a new record, waiting; the oldest dropped when the list is full */
static uint32_t record_add(fairground_t *f, fairground_kind_t kind, fairground_res_t res,
                           int64_t units, int64_t price)
{
    if (f->nrec == FAIRGROUND_RECORDS) {
        memmove(f->rec, f->rec + 1, (FAIRGROUND_RECORDS - 1) * sizeof f->rec[0]);
        f->nrec--;
    }
    fairground_record_t *r = &f->rec[f->nrec];
    r->offer = f->next_offer;
    f->next_offer = f->next_offer == FAIRGROUND_OFFER_MAX ? 0 : f->next_offer + 1;
    r->kind = kind;
    r->resource = res;
    r->units = units;
    r->price = price;
    r->status = FAIRGROUND_WAITING;
    return f->nrec++;
}

static int hold(fairground_t *f, fairground_kind_t kind, fairground_res_t res, int64_t units,
                int64_t price, const char *cmd)
{
    fairground_held_t *h = &f->held;
    size_t n = strlen(cmd);
    if (n > FAIRGROUND_CMD_MAX) return ZXM_E_ARG;
    h->kind = kind;
    h->resource = res;
    h->units = units;
    h->price = price;
    memcpy(h->cmd, cmd, n + 1);
    h->record = record_add(f, kind, res, units, price);
    h->pending = true;
    return ZXM_OK;
}

int fairground_share(fairground_t *f, int64_t compute, int64_t storage)
{
    char cmd[FAIRGROUND_CMD_MAX + 1];
    if (!f) return ZXM_E_ARG;
    if (f->held.pending) return ZXM_E_STATE;
    if (compute < 0 || compute > FAIRGROUND_MINOR_MAX || storage < 0 ||
        storage > FAIRGROUND_MINOR_MAX)
        return ZXM_E_ARG;
    snprintf(cmd, sizeof cmd, "share %lld %lld", (long long) compute, (long long) storage);
    memset(&f->held, 0, sizeof f->held);
    int rc = hold(f, FAIRGROUND_SHARE, FAIRGROUND_COMPUTE, compute, storage, cmd);
    if (rc != ZXM_OK) return rc;
    if (!compute && !storage)
        snprintf(f->notice, sizeof f->notice,
                 "Stop sharing compute and storage. Waiting for your approval.");
    else
        snprintf(f->notice, sizeof f->notice,
                 "Share %lld compute tokens a cycle and %lld storage bytes. Waiting for your "
                 "approval.",
                 (long long) compute, (long long) storage);
    return ZXM_OK;
}

/* FM2: price and units within the owner's limits, through FR2's margins */
static bool within_limits(fairground_t *f, fairground_kind_t kind, int64_t units, int64_t price)
{
    if (!f->most_units) {
        snprintf(f->notice, sizeof f->notice, "Set your limits before you want or sell anything.");
        return false;
    }
    if (!in_nine(units) || !in_nine(price)) {
        snprintf(f->notice, sizeof f->notice,
                 "Not offered: price and units are each 1 to 999999999.");
        return false;
    }
    int64_t priceroom = kind == FAIRGROUND_WANT ? f->ceiling - price : price - f->floor_price;
    int64_t unitsroom = f->most_units - units;
    if (!fairground_offer_allowed(price, units, priceroom, unitsroom)) {
        snprintf(f->notice, sizeof f->notice, "Not offered: %s.",
                 unitsroom < 0             ? "more units than your most for one offer"
                 : kind == FAIRGROUND_WANT ? "the price passes your ceiling"
                                           : "the price is below your floor");
        return false;
    }
    return true;
}

int fairground_want(fairground_t *f, fairground_res_t resource, int64_t units, int64_t max_price)
{
    char cmd[FAIRGROUND_CMD_MAX + 1], after[64] = "";
    if (!f || (resource != FAIRGROUND_COMPUTE && resource != FAIRGROUND_STORAGE)) return ZXM_E_ARG;
    if (f->held.pending) return ZXM_E_STATE;
    if (!within_limits(f, FAIRGROUND_WANT, units, max_price)) return ZXM_E_ARG;
    int64_t fee = fairground_assurance_fee(max_price);
    snprintf(cmd, sizeof cmd, "want %s %lld %lld", resource_text(resource), (long long) units,
             (long long) max_price);
    memset(&f->held, 0, sizeof f->held);
    int rc = hold(f, FAIRGROUND_WANT, resource, units, max_price, cmd);
    if (rc != ZXM_OK) return rc;
    f->held.fee = fee;
    if (f->books_known) { /* FM4: at the ceiling, the fee on top */
        int64_t w = coffer_balance(f->wallet, -(max_price + fee));
        if (w != COFFER_REFUSED) {
            f->held.projected = true;
            f->held.wallet_after = w;
            snprintf(after, sizeof after, " Wallet after one receipt: %lld VFV.", (long long) w);
        }
    }
    snprintf(f->notice, sizeof f->notice,
             "Want %lld %s units, paying at most %lld a receipt plus the 0.08889%% fee (%lld).%s "
             "Waiting for your approval.",
             (long long) units, resource_text(resource), (long long) max_price, (long long) fee,
             after);
    return ZXM_OK;
}

int fairground_sell(fairground_t *f, fairground_res_t resource, int64_t units, int64_t price,
                    const char *buyer)
{
    char cmd[FAIRGROUND_CMD_MAX + 1], after[64] = "";
    if (!f || (resource != FAIRGROUND_COMPUTE && resource != FAIRGROUND_STORAGE) || !node_ok(buyer))
        return ZXM_E_ARG;
    if (f->held.pending) return ZXM_E_STATE;
    if (!within_limits(f, FAIRGROUND_SELL, units, price)) return ZXM_E_ARG;
    snprintf(cmd, sizeof cmd, "sell %s %lld %lld %s", resource_text(resource), (long long) units,
             (long long) price, buyer);
    memset(&f->held, 0, sizeof f->held);
    int rc = hold(f, FAIRGROUND_SELL, resource, units, price, cmd);
    if (rc != ZXM_OK) return rc;
    memcpy(f->held.peer, buyer, FAIRGROUND_NODE + 1);
    f->held.fee = fairground_assurance_fee(price);
    if (f->books_known) { /* FM4: the buyer pays the fee */
        int64_t w = coffer_balance(f->wallet, price);
        if (w != COFFER_REFUSED) {
            f->held.projected = true;
            f->held.wallet_after = w;
            snprintf(after, sizeof after, " Wallet after: %lld VFV.", (long long) w);
        }
    }
    snprintf(f->notice, sizeof f->notice,
             "Sell %lld %s units for %lld to %.8s; the buyer pays the 0.08889%% fee (%lld).%s "
             "Waiting for your approval.",
             (long long) units, resource_text(resource), (long long) price, buyer,
             (long long) f->held.fee, after);
    return ZXM_OK;
}

int fairground_approve(fairground_t *f)
{
    char res[FAIRGROUND_ANSWER + 1];
    if (!f) return ZXM_E_ARG;
    fairground_held_t *h = &f->held;
    if (!h->pending) return ZXM_E_STATE;
    zxm_link_t *l = f->link;
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", h->cmd);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) {
        snprintf(f->notice, sizeof f->notice, "Not sent: %.200s. It still waits for you.", l->err);
        return rc;
    }
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ ||
        zxm_get_str(l, r, "result", res, sizeof res) != ZXM_OK) {
        snprintf(f->notice, sizeof f->notice,
                 "zxvd's answer was not understood; read the offers before approving again.");
        return ZXM_E_PROTOCOL;
    }
    const char *yes = h->kind == FAIRGROUND_WANT   ? "wanted"
                      : h->kind == FAIRGROUND_SELL ? "offered"
                      : h->units || h->price       ? "sharing"
                                                   : "sharing nothing";
    fairground_status_t st;
    if (!strcmp(res, yes))
        st = FAIRGROUND_SENT;
    else if (!strcmp(res, "refused"))
        st = FAIRGROUND_REFUSED_BY_ZXVD;
    else {
        snprintf(f->notice, sizeof f->notice,
                 "zxvd's answer was not understood; read the offers before approving again.");
        return ZXM_E_PROTOCOL;
    }
    memcpy(f->answer, res, strlen(res) + 1);
    f->rec[h->record].status = st;
    h->pending = false;
    snprintf(f->notice, sizeof f->notice,
             st == FAIRGROUND_SENT ? "Approved and sent: %.200s." : "zxvd refused it: %.200s.",
             h->cmd);
    return ZXM_OK;
}

int fairground_cancel(fairground_t *f)
{
    if (!f) return ZXM_E_ARG;
    if (!f->held.pending) return ZXM_E_STATE;
    f->rec[f->held.record].status = FAIRGROUND_CANCELLED;
    f->held.pending = false;
    snprintf(f->notice, sizeof f->notice, "Cancelled; nothing was sent.");
    return ZXM_OK;
}

/* ---- FM5 ---------------------------------------------------------------------------------- */

int fairground_unit_price_of(const fairground_t *f, fairground_kind_t kind, int64_t *num,
                             int64_t *den)
{
    int64_t price[FAIRGROUND_RECORDS], units[FAIRGROUND_RECORDS];
    uint32_t n = 0;
    if (!f || !num || !den || (kind != FAIRGROUND_WANT && kind != FAIRGROUND_SELL))
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < f->nrec; i++)
        if (f->rec[i].kind == kind && f->rec[i].status == FAIRGROUND_SENT) {
            price[n] = f->rec[i].price;
            units[n] = f->rec[i].units;
            n++;
        }
    if (!n) return ZXM_E_STATE;
    return fairground_unit_price(price, units, n, num, den) ? ZXM_OK : ZXM_E_ARG;
}

bool fairground_records_fit(const fairground_t *f)
{
    if (!f) return false;
    for (uint32_t i = 0; i < f->nrec; i++) {
        const fairground_record_t *r = &f->rec[i];
        if (!fairground_record_fits(r->offer, r->kind, r->resource, r->units, r->price, r->status))
            return false;
    }
    return true;
}
