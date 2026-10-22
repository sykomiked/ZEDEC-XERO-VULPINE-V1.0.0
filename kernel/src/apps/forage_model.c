/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* forage_model.c — Forage, node bounties (see forage_model.h). Every answer
 * is checked whole before anything of it is kept, and a claim, a posted
 * bounty or a change to serving goes out only on the owner's word. */
#include <stdio.h>
#include <string.h>

#include "forage_model.h"

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

static bool get_dec(const zxm_link_t *l, int32_t o, const char *k, int64_t *v)
{
    char b[24];
    if (zxm_get_str(l, o, k, b, sizeof b) != ZXM_OK) return false;
    return dec_val(b, v);
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
        if (i >= FORAGE_NODE || !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return false;
    return i == FORAGE_NODE;
}

static bool in_nine(int64_t v, int64_t least)
{
    return v >= least && v <= FORAGE_NINE;
}

static const char *resource_text(forage_res_t r)
{
    return r == FORAGE_COMPUTE ? "compute" : "storage";
}

static bool resource_ok(forage_res_t r)
{
    return r == FORAGE_COMPUTE || r == FORAGE_STORAGE;
}

/* ---- state --------------------------------------------------------------------------- */

void forage_init(forage_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
}

int forage_set_budget(forage_t *f, int64_t tokens, int64_t mw, bool owner_confirmed)
{
    if (!f || !in_nine(tokens, 1) || !in_nine(mw, 1)) return ZXM_E_ARG;
    if (!owner_confirmed) {
        snprintf(f->notice, sizeof f->notice, "Your budgets change only on your word.");
        return ZXM_E_STATE;
    }
    f->budget_tokens = tokens;
    f->budget_mw = mw;
    snprintf(f->notice, sizeof f->notice,
             "Your budgets: bounties may take %lld swarm tokens a cycle and %lld mW.",
             (long long) tokens, (long long) mw);
    return ZXM_OK;
}

/* ---- FG1: swarm.state ------------------------------------------------------------------ */

static bool get_big(const zxm_link_t *l, int32_t o, const char *k, int64_t *v)
{
    uint64_t u;
    if (zxm_get_u64(l, o, k, &u) != ZXM_OK || u > (uint64_t) INT64_MAX) return false;
    *v = (int64_t) u;
    return true;
}

int forage_read_swarm(forage_t *f)
{
    int64_t tokens, returned, contributed, refused;
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "swarm.state");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result, q;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || !get_big(l, r, "tokens_per_cycle", &tokens) ||
        tokens > INT64_MAX / 1000 || !get_big(l, r, "bounties_returned", &returned))
        return ZXM_E_PROTOCOL;
    q = zxm_get(l, r, "quest");
    if (q < 0 || l->tok[q].type != W4_J_OBJ || !get_big(l, q, "contributed", &contributed) ||
        !get_count(l, q, "refused", INT64_C(4294967295), &refused))
        return ZXM_E_PROTOCOL;
    f->cycle_tokens = tokens;
    f->bounties_returned = returned;
    f->quest_contributed = contributed;
    f->quest_refused = refused;
    f->swarm_known = true;
    snprintf(f->notice, sizeof f->notice,
             "The swarm: %lld tokens a cycle; %lld returned to bounties; your quest has "
             "contributed %lld units.",
             (long long) tokens, (long long) returned, (long long) contributed);
    return ZXM_OK;
}

/* ---- FG1: power.status ----------------------------------------------------------------- */

int forage_read_power(forage_t *f)
{
    static const char *const POSTURE[4] = {"perform", "balanced", "conserve", "critical"};
    static const int64_t SCALE[4] = {1000, 618, 382, 0};
    int64_t mw, scale;
    char posture[16];
    uint32_t p = 0;
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "power.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || !get_count(l, r, "total_mw", INT64_MAX, &mw) ||
        zxm_get_str(l, r, "posture", posture, sizeof posture) != ZXM_OK ||
        !get_count(l, r, "token_scale_permille", 1000, &scale))
        return ZXM_E_PROTOCOL;
    while (p < 4u && strcmp(posture, POSTURE[p])) p++;
    /* the posture scales the swarm's tokens by its own ratio (docs/POWER.md) */
    if (p == 4u || scale != SCALE[p]) return ZXM_E_PROTOCOL;
    f->draw_mw = mw;
    f->scale_permille = scale;
    f->power_known = true;
    snprintf(f->notice, sizeof f->notice,
             "The node draws %lld mW, posture %s: %lld per mille of the swarm's tokens.",
             (long long) mw, posture, (long long) scale);
    return ZXM_OK;
}

/* ---- FG1: peer.status ------------------------------------------------------------------ */

/* serving, price and wallet of a peer.status or peer.serve answer */
static int read_peer_answer(forage_t *f, const zxm_link_t *l)
{
    int64_t price, wallet;
    bool serving, book_ok;
    char ccy[8];
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || zxm_get_bool(l, r, "serving", &serving) != ZXM_OK ||
        !get_dec(l, r, "price_minor", &price) ||
        zxm_get_str(l, r, "currency", ccy, sizeof ccy) != ZXM_OK || strcmp(ccy, "VFV") ||
        !get_dec(l, r, "wallet_minor", &wallet) ||
        zxm_get_bool(l, r, "book_ok", &book_ok) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    f->serving = serving;
    f->serve_price = price;
    f->wallet = wallet;
    f->book_ok = book_ok;
    f->peer_known = true;
    return ZXM_OK;
}

int forage_read_peer(forage_t *f)
{
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "peer.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = read_peer_answer(f, l);
    if (rc != ZXM_OK) return rc;
    snprintf(f->notice, sizeof f->notice, "%s; wallet %lld VFV; the peer book %s.",
             f->serving ? "Serving other nodes" : "Not serving other nodes", (long long) f->wallet,
             f->book_ok ? "verifies" : "does NOT verify");
    return ZXM_OK;
}

/* ---- FG1: net.offers ------------------------------------------------------------------- */

int forage_read_offers(forage_t *f)
{
    int64_t compute, storage, v;
    uint32_t nsell = 0, nbuy = 0;
    char s[16], peer[FORAGE_NODE + 1];
    if (!f) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "net.offers");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result, sh, wa, ou;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    sh = zxm_get(l, r, "share");
    if (sh < 0 || l->tok[sh].type != W4_J_OBJ || !get_dec(l, sh, "compute", &compute) ||
        !get_dec(l, sh, "storage", &storage))
        return ZXM_E_PROTOCOL;
    wa = zxm_get(l, r, "wants");
    ou = zxm_get(l, r, "outstanding");
    if (wa < 0 || l->tok[wa].type != W4_J_ARR || ou < 0 || l->tok[ou].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t nw = zxm_len(l, wa), no = zxm_len(l, ou);
    if (nw > FORAGE_WANTS || no > FORAGE_PENDING) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < nw; i++) {
        int32_t e = zxm_at(l, wa, i);
        if (e < 0 || l->tok[e].type != W4_J_OBJ || !get_count(l, e, "slot", FORAGE_WANTS - 1, &v) ||
            zxm_get_str(l, e, "resource", s, sizeof s) != ZXM_OK ||
            (strcmp(s, "compute") && strcmp(s, "storage") && strcmp(s, "other")) ||
            !get_dec(l, e, "units_left", &v) || !get_dec(l, e, "max_price", &v))
            return ZXM_E_PROTOCOL;
    }
    for (uint32_t i = 0; i < no; i++) {
        int32_t e = zxm_at(l, ou, i);
        bool sell;
        if (e < 0 || l->tok[e].type != W4_J_OBJ || zxm_get_str(l, e, "role", s, sizeof s) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (!strcmp(s, "sell"))
            sell = true;
        else if (!strcmp(s, "buy"))
            sell = false;
        else
            return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, e, "peer", peer, sizeof peer) != ZXM_OK || !node_ok(peer) ||
            !get_dec(l, e, "units", &v) || !get_dec(l, e, "seq", &v) ||
            !get_count(l, e, "tries", INT64_C(4294967295), &v) ||
            zxm_get_i64(l, e, "want", &v) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        /* a sell reserves no want; a buy reserves one of the want slots */
        if (sell ? v != -1 : (v < 0 || v >= (int64_t) FORAGE_WANTS)) return ZXM_E_PROTOCOL;
        if (sell)
            nsell++;
        else
            nbuy++;
    }
    f->share_compute = compute;
    f->share_storage = storage;
    f->nwant = nw;
    f->nsell = nsell;
    f->nbuy = nbuy;
    f->offers_known = true;
    snprintf(f->notice, sizeof f->notice,
             "%u bounties of yours open; %u claims and %u purchases waiting on the other side.", nw,
             nsell, nbuy);
    return ZXM_OK;
}

/* ---- FG2: the board -------------------------------------------------------------------- */

int forage_add_bounty(forage_t *f, const char *peer, forage_res_t resource, int64_t units,
                      int64_t amount, int64_t minutes, int64_t tokens, int64_t mw)
{
    if (!f || !node_ok(peer) || !resource_ok(resource) || !in_nine(units, 1) ||
        !in_nine(amount, 1) || !in_nine(minutes, 1) || !in_nine(tokens, 1) || !in_nine(mw, 0))
        return ZXM_E_ARG;
    if (f->nboard == FORAGE_BOUNTIES) return ZXM_E_STATE;
    int64_t net = forage_net_after_fee(amount);
    if (net == FORAGE_REFUSED || !forage_record_fits((int64_t) f->nboard, FORAGE_CLAIM, tokens, mw,
                                                     amount, minutes, FORAGE_WAITING))
        return ZXM_E_ARG;
    forage_bounty_t *b = &f->board[f->nboard++];
    memset(b, 0, sizeof *b);
    memcpy(b->peer, peer, FORAGE_NODE + 1);
    b->resource = resource;
    b->units = units;
    b->amount = amount;
    b->minutes = minutes;
    b->tokens = tokens;
    b->mw = mw;
    b->net = net;
    f->ranked = false;
    snprintf(f->notice, sizeof f->notice,
             "Bounty %u: %lld %s units for %lld VFV (%lld net of the fee) over %lld minutes.",
             f->nboard - 1u, (long long) units, resource_text(resource), (long long) amount,
             (long long) net, (long long) minutes);
    return ZXM_OK;
}

int forage_rank(forage_t *f)
{
    int64_t net[FORAGE_BOUNTIES], minutes[FORAGE_BOUNTIES], rank[FORAGE_BOUNTIES];
    if (!f) return ZXM_E_ARG;
    if (!f->nboard) return ZXM_E_STATE;
    for (uint32_t i = 0; i < f->nboard; i++) {
        net[i] = f->board[i].net;
        minutes[i] = f->board[i].minutes;
    }
    if (!forage_rate_rank(net, minutes, f->nboard, rank)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < f->nboard; i++) f->board[i].rank = rank[i];
    f->ranked = true;
    return ZXM_OK;
}

/* the tokens of the claims already sent */
static int64_t committed_tokens(const forage_t *f)
{
    int64_t t = 0;
    for (uint32_t i = 0; i < f->nrec; i++)
        if (f->rec[i].kind == FORAGE_CLAIM && f->rec[i].status == FORAGE_SENT)
            t += f->rec[i].tokens;
    return t;
}

bool forage_rooms(const forage_t *f, uint32_t bounty, int64_t *tokroom, int64_t *mwroom)
{
    if (!f || !tokroom || !mwroom || bounty >= f->nboard || !f->swarm_known || !f->power_known)
        return false;
    const forage_bounty_t *b = &f->board[bounty];
    int64_t cycle = f->cycle_tokens * f->scale_permille / 1000;
    int64_t allowed = f->budget_tokens < cycle ? f->budget_tokens : cycle;
    *tokroom = allowed - committed_tokens(f) - b->tokens;
    /* a draw past the cap's range is no room at all */
    *mwroom = f->draw_mw > FORAGE_NINE ? -1 : f->budget_mw - f->draw_mw - b->mw;
    return true;
}

int forage_best(const forage_t *f, uint32_t *bounty)
{
    int64_t tr, mr, best = 0;
    if (!f || !bounty) return ZXM_E_ARG;
    if (!f->ranked) return ZXM_E_STATE;
    for (uint32_t i = 0; i < f->nboard; i++) {
        const forage_bounty_t *b = &f->board[i];
        if (b->claimed || !forage_rooms(f, i, &tr, &mr) ||
            !forage_claim_fits(b->tokens, b->mw, tr, mr))
            continue;
        if (!best || b->rank < best) {
            best = b->rank;
            *bounty = i;
        }
    }
    return best ? ZXM_OK : ZXM_E_STATE;
}

/* ---- FG4, FG7: held for the owner -------------------------------------------------------- */

/* a new record, waiting; the oldest dropped when the list is full */
static uint32_t record_add(forage_t *f, forage_kind_t kind, int64_t tokens, int64_t mw,
                           int64_t amount, int64_t minutes)
{
    if (f->nrec == FORAGE_RECORDS) {
        memmove(f->rec, f->rec + 1, (FORAGE_RECORDS - 1) * sizeof f->rec[0]);
        f->nrec--;
    }
    forage_record_t *r = &f->rec[f->nrec];
    r->no = f->next_no;
    f->next_no = f->next_no == FORAGE_RECORD_MAX ? 0 : f->next_no + 1;
    r->kind = kind;
    r->tokens = tokens;
    r->mw = mw;
    r->amount = amount;
    r->minutes = minutes;
    r->status = FORAGE_WAITING;
    return f->nrec++;
}

int forage_claim(forage_t *f, uint32_t bounty)
{
    int64_t tr, mr;
    char after[64] = "";
    if (!f || bounty >= f->nboard) return ZXM_E_ARG;
    if (f->held.pending) return ZXM_E_STATE;
    forage_bounty_t *b = &f->board[bounty];
    if (b->claimed) {
        snprintf(f->notice, sizeof f->notice, "Bounty %u is already claimed.", bounty);
        return ZXM_E_STATE;
    }
    if (!forage_rooms(f, bounty, &tr, &mr)) {
        snprintf(f->notice, sizeof f->notice,
                 "Read the swarm and the power ledger before claiming anything.");
        return ZXM_E_STATE;
    }
    if (!f->budget_tokens) {
        snprintf(f->notice, sizeof f->notice, "Set your budgets before claiming anything.");
        return ZXM_E_ARG;
    }
    if (!forage_claim_fits(b->tokens, b->mw, tr, mr)) {
        snprintf(f->notice, sizeof f->notice, "Not claimed: %s.",
                 tr < 0 ? "past the swarm tokens your budget leaves this cycle"
                        : "past the power your budget leaves");
        return ZXM_E_ARG;
    }
    forage_held_t *h = &f->held;
    memset(h, 0, sizeof *h);
    int n = snprintf(h->cmd, sizeof h->cmd, "sell %s %lld %lld %s", resource_text(b->resource),
                     (long long) b->units, (long long) b->net, b->peer);
    if (n < 0 || (size_t) n >= sizeof h->cmd) return ZXM_E_ARG;
    h->kind = FORAGE_CLAIM;
    h->bounty = bounty;
    h->amount = b->amount;
    h->net = b->net;
    h->fee = fairground_assurance_fee(b->net);
    h->tokens = b->tokens;
    if (f->peer_known) { /* FG6: the claim paid at its net */
        int64_t w = coffer_balance(f->wallet, b->net);
        if (w != COFFER_REFUSED) {
            h->projected = true;
            h->wallet_after = w;
            snprintf(after, sizeof after, " Wallet after: %lld VFV.", (long long) w);
        }
    }
    h->record = record_add(f, FORAGE_CLAIM, b->tokens, b->mw, b->amount, b->minutes);
    h->pending = true;
    snprintf(f->notice, sizeof f->notice,
             "Claim bounty %u at %lld VFV, its %lld net of the 0.08889%% fee, from %.8s.%s "
             "Waiting for your approval.",
             bounty, (long long) b->amount, (long long) b->net, b->peer, after);
    return ZXM_OK;
}

int forage_post(forage_t *f, forage_res_t resource, int64_t units, int64_t amount)
{
    char after[64] = "";
    if (!f || !resource_ok(resource) || !in_nine(units, 1) || !in_nine(amount, 1)) return ZXM_E_ARG;
    if (f->held.pending) return ZXM_E_STATE;
    int64_t net = forage_net_after_fee(amount);
    if (net == FORAGE_REFUSED) return ZXM_E_ARG;
    int64_t fee = fairground_assurance_fee(net);
    forage_held_t *h = &f->held;
    memset(h, 0, sizeof *h);
    snprintf(h->cmd, sizeof h->cmd, "want %s %lld %lld", resource_text(resource), (long long) units,
             (long long) net);
    h->kind = FORAGE_POST;
    h->amount = amount;
    h->net = net;
    h->fee = fee;
    if (f->peer_known) { /* FG6: at most net + its fee paid out */
        int64_t w = coffer_balance(f->wallet, -(net + fee));
        if (w != COFFER_REFUSED) {
            h->projected = true;
            h->wallet_after = w;
            snprintf(after, sizeof after, " Wallet after: %lld VFV.", (long long) w);
        }
    }
    h->record = record_add(f, FORAGE_POST, 0, 0, amount, 0);
    h->pending = true;
    snprintf(f->notice, sizeof f->notice,
             "Post a bounty of %lld %s units: at most %lld VFV a receipt plus its fee (%lld), "
             "within your %lld.%s Waiting for your approval.",
             (long long) units, resource_text(resource), (long long) net, (long long) fee,
             (long long) amount, after);
    return ZXM_OK;
}

int forage_approve(forage_t *f, bool owner_confirmed)
{
    char res[FORAGE_ANSWER + 1];
    if (!f) return ZXM_E_ARG;
    forage_held_t *h = &f->held;
    if (!h->pending) return ZXM_E_STATE;
    if (!owner_confirmed) {
        snprintf(f->notice, sizeof f->notice, "Not sent: it waits for your word.");
        return ZXM_E_STATE;
    }
    zxm_link_t *l = f->link;
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", h->cmd);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) {
        snprintf(f->notice, sizeof f->notice, "Not sent: %.200s. It still waits for you.", l->err);
        return rc;
    }
    int32_t r = l->result;
    forage_status_t st;
    const char *yes = h->kind == FORAGE_CLAIM ? "offered" : "wanted";
    if (r < 0 || l->tok[r].type != W4_J_OBJ ||
        zxm_get_str(l, r, "result", res, sizeof res) != ZXM_OK)
        st = FORAGE_WAITING;
    else if (!strcmp(res, yes))
        st = FORAGE_SENT;
    else if (!strcmp(res, "refused"))
        st = FORAGE_REFUSED_BY_ZXVD;
    else
        st = FORAGE_WAITING;
    if (st == FORAGE_WAITING) {
        snprintf(f->notice, sizeof f->notice,
                 "zxvd's answer was not understood; read the offers before approving again.");
        return ZXM_E_PROTOCOL;
    }
    memcpy(f->answer, res, strlen(res) + 1);
    f->rec[h->record].status = st;
    if (st == FORAGE_SENT && h->kind == FORAGE_CLAIM) f->board[h->bounty].claimed = true;
    h->pending = false;
    snprintf(f->notice, sizeof f->notice,
             st == FORAGE_SENT ? "Approved and sent: %.200s." : "zxvd refused it: %.200s.", h->cmd);
    return ZXM_OK;
}

int forage_cancel(forage_t *f)
{
    if (!f) return ZXM_E_ARG;
    if (!f->held.pending) return ZXM_E_STATE;
    f->rec[f->held.record].status = FORAGE_CANCELLED;
    f->held.pending = false;
    snprintf(f->notice, sizeof f->notice, "Cancelled; nothing was sent.");
    return ZXM_OK;
}

/* ---- FG5: serving ------------------------------------------------------------------------ */

static int serve_call(forage_t *f, bool on, int64_t price)
{
    zxm_link_t *l = f->link;
    zxm_begin(l, "peer.serve");
    if (on) zxm_p_i64(l, "price_minor", price);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) {
        snprintf(f->notice, sizeof f->notice, "Serving unchanged: %.200s.", l->err);
        return rc;
    }
    rc = read_peer_answer(f, l);
    if (rc != ZXM_OK) return rc;
    if (f->serving != on) {
        snprintf(f->notice, sizeof f->notice, "zxvd says serving is %s.",
                 f->serving ? "still on" : "off");
        return ZXM_E_PROTOCOL;
    }
    if (on)
        snprintf(f->notice, sizeof f->notice, "Serving other nodes at %lld VFV an answer.",
                 (long long) f->serve_price);
    else
        snprintf(f->notice, sizeof f->notice, "Not serving other nodes.");
    return ZXM_OK;
}

int forage_serve(forage_t *f, int64_t price, bool owner_confirmed)
{
    if (!f || !in_nine(price, 0)) return ZXM_E_ARG;
    if (!owner_confirmed) {
        snprintf(f->notice, sizeof f->notice, "Serving stays off until you turn it on.");
        return ZXM_E_STATE;
    }
    return serve_call(f, true, price);
}

int forage_serve_stop(forage_t *f, bool owner_confirmed)
{
    if (!f) return ZXM_E_ARG;
    if (!owner_confirmed) {
        snprintf(f->notice, sizeof f->notice, "Serving changes only on your word.");
        return ZXM_E_STATE;
    }
    return serve_call(f, false, 0);
}

/* ---- FG7 ---------------------------------------------------------------------------------- */

bool forage_records_fit(const forage_t *f)
{
    if (!f) return false;
    for (uint32_t i = 0; i < f->nrec; i++) {
        const forage_record_t *r = &f->rec[i];
        if (!forage_record_fits(r->no, r->kind, r->tokens, r->mw, r->amount, r->minutes, r->status))
            return false;
    }
    return true;
}
