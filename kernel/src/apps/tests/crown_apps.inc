/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Crown's model against a grant engine. The grant engine (zxvd_estate.c)
 * is not linked into this in-process zxvd, so grants.* is answered on a
 * link of Crown's own (CR_LINK) by a small sandbox engine written here
 * from grants.h G2 and G3 (its own split, not crown_allot), and every other
 * request goes to the real dispatcher. Only grants.setup and grants.fund
 * sandbox units are used: never money. */
#include "crown_model.h"

#define CR_ACCTS 16u

static crown_t CR;
static zxm_link_t CR_LINK;
static int cr_sent;    /* grants.* requests answered */
static bool cr_tamper; /* the engine answers a share the proposal did not say */
static bool cr_skew;   /* pool_credit answers a pool that is not old + credit */
static struct {
    bool set_up;
    int64_t epoch, pool, inflow, granted, total;
    uint32_t naccts;
    int64_t bal[CR_ACCTS];
    uint32_t lot;
} CRE;

/* the integer after "key": in the request, quoted or not; -1 when absent */
static int64_t cr_num(const char *from, const char *key)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(from, k);
    if (!p) return -1;
    p += strlen(k);
    if (*p == '"') p++;
    int64_t v = 0;
    for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
    return v;
}

/* grants.h G2 with the setup's defaults (9500 bps, volume 10000, stake 100000) */
static uint32_t cr_mask(const char *r)
{
    uint32_t m = 0;
    int64_t up = cr_num(r, "uptime_bps");
    if (up > 10000) m |= 0x80u;
    if (up < 9500) m |= 0x01u;
    if (cr_num(r, "dropped_reports") > 0) m |= 0x02u;
    if (cr_num(r, "routing_reports") > 0) m |= 0x04u;
    if (cr_num(r, "failed_challenges") > 0) m |= 0x08u;
    if (cr_num(r, "fraud_findings") > 0) m |= 0x10u;
    if (cr_num(r, "volume") < 10000) m |= 0x20u;
    if (cr_num(r, "stake") < 100000) m |= 0x40u;
    return m;
}

static int32_t cr_ok(char *resp, uint32_t cap, uint32_t id, const char *result)
{
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", id, result);
}

static int32_t cr_err(char *resp, uint32_t cap, uint32_t id, const char *why)
{
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32000,"
                    "\"message\":\"%s\"}}",
                    id, why);
}

static int32_t cr_distribute(const char *req, char *resp, uint32_t cap, uint32_t id)
{
    uint32_t acct[CROWN_SEATS], n = 0;
    int64_t w[CROWN_SEATS], W = 0, share[CROWN_SEATS], rem[CROWN_SEATS];
    const char *p = strstr(req, "\"recipients\":");
    int64_t price = cr_num(req, "price");
    if (!p || price < 1 || (uint32_t) cr_num(req, "lot") != CRE.lot)
        return cr_err(resp, cap, id, "lot");
    while ((p = strstr(p, "{\"account\":")) && n < CROWN_SEATS) {
        const char *end = strchr(p, '}');
        char one[512];
        size_t len = (size_t) (end - p) + 1u;
        if (!end || len >= sizeof one) return cr_err(resp, cap, id, "recipient");
        memcpy(one, p, len);
        one[len] = 0;
        if (cr_mask(one)) return cr_err(resp, cap, id, "GRANT_ERR_INELIGIBLE");
        acct[n] = (uint32_t) cr_num(one, "account");
        w[n] = cr_num(one, "weight");
        W += w[n++];
        p = end;
    }
    int64_t B = CRE.pool / price;
    if (n == 0 || W == 0) return cr_err(resp, cap, id, "GRANT_ERR_ARG");
    if (B == 0) return cr_err(resp, cap, id, "GRANT_ERR_EMPTY");
    /* G3: floor(B * w / W), then one each to the largest remainders, ties low */
    int64_t given = 0;
    bool bumped[CROWN_SEATS] = {false};
    for (uint32_t i = 0; i < n; i++) {
        share[i] = B * w[i] / W;
        rem[i] = B * w[i] % W;
        given += share[i];
    }
    for (int64_t left = B - given; left > 0; left--) {
        uint32_t best = n;
        for (uint32_t i = 0; i < n; i++)
            if (!bumped[i] && (best == n || rem[i] > rem[best])) best = i;
        bumped[best] = true;
        share[best]++;
    }
    if (cr_tamper && n > 1 && share[0] > 0) share[0]--, share[1]++;
    CRE.pool -= B * price;
    CRE.granted += B;
    CRE.total += B;
    char out[4096];
    int at = snprintf(out, sizeof out,
                      "{\"barrels\":\"%lld\",\"cost_minor\":\"%lld\",\"postings\":%u,\"shares\":[",
                      (long long) B, (long long) (B * price), n);
    for (uint32_t i = 0; i < n; i++) {
        if (acct[i] < CR_ACCTS) CRE.bal[acct[i]] += share[i];
        at +=
            snprintf(out + at, sizeof out - (size_t) at, "%s{\"account\":%u,\"barrels\":\"%lld\"}",
                     i ? "," : "", acct[i], (long long) share[i]);
    }
    snprintf(out + at, sizeof out - (size_t) at,
             "],\"pool_minor\":\"%lld\",\"label\":\"unbacked grant lot\"}", (long long) CRE.pool);
    return cr_ok(resp, cap, id, out);
}

static int32_t cr_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    char r[ZXM_REQ_MAX + 1], out[512];
    if (!strstr(req, "\"grants.")) return inproc(NULL, req, len, resp, cap);
    memcpy(r, req, len);
    r[len] = 0;
    cr_sent++;
    uint32_t id = l->seq;
    if (strstr(r, "\"grants.setup\"")) {
        if (CRE.set_up) return cr_err(resp, cap, id, "SET_UP");
        CRE.set_up = true;
        CRE.naccts = 2; /* the pool 0 and the reserve 1 */
        return cr_ok(resp, cap, id, "{\"pool\":0,\"reserve\":1,\"minted\":false}");
    }
    if (!CRE.set_up) {
        if (strstr(r, "\"grants.status\"")) return cr_ok(resp, cap, id, "{\"set_up\":false}");
        return cr_err(resp, cap, id, "not set up");
    }
    if (strstr(r, "\"grants.account_open\"")) {
        if (CRE.naccts >= CR_ACCTS) return cr_err(resp, cap, id, "full");
        snprintf(out, sizeof out, "{\"account\":%u,\"owner\":%lld,\"asset\":\"vino\"}", CRE.naccts,
                 (long long) cr_num(r, "owner"));
        CRE.bal[CRE.naccts++] = 0;
    } else if (strstr(r, "\"grants.lot_open\"")) {
        CRE.lot = (uint32_t) cr_num(r, "lot");
        snprintf(out, sizeof out, "{\"lot\":%u,\"label\":\"unbacked\",\"status\":\"open\"}",
                 CRE.lot);
    } else if (strstr(r, "\"grants.fund\"")) {
        int64_t a = cr_num(r, "account"), amt = cr_num(r, "amount_minor");
        if (a < 2 || a >= (int64_t) CRE.naccts || amt <= 0) return cr_err(resp, cap, id, "fund");
        CRE.bal[a] += amt;
        snprintf(out, sizeof out, "{\"account\":%lld,\"balance_minor\":\"%lld\",\"sandbox\":true}",
                 (long long) a, (long long) CRE.bal[a]);
    } else if (strstr(r, "\"grants.account\"")) {
        int64_t a = cr_num(r, "account");
        if (a < 0 || a >= (int64_t) CRE.naccts) return cr_err(resp, cap, id, "no account");
        snprintf(out, sizeof out,
                 "{\"account\":%lld,\"owner\":0,\"asset\":\"barrel\",\"balance_minor\":\"%lld\","
                 "\"credit_minor\":\"0\",\"sandbox\":true}",
                 (long long) a, (long long) CRE.bal[a]);
    } else if (strstr(r, "\"grants.status\"")) {
        snprintf(out, sizeof out,
                 "{\"set_up\":true,\"epoch\":\"%lld\",\"inflow_minor\":\"%lld\",\"pool_minor\":"
                 "\"%lld\",\"granted_barrels\":\"%lld\",\"total_barrels_granted\":\"%lld\","
                 "\"minted\":false,\"sandbox\":true}",
                 (long long) CRE.epoch, (long long) CRE.inflow, (long long) CRE.pool,
                 (long long) CRE.granted, (long long) CRE.total);
    } else if (strstr(r, "\"grants.epoch\"")) {
        int64_t e = cr_num(r, "epoch");
        if (e <= CRE.epoch) return cr_err(resp, cap, id, "epoch");
        CRE.epoch = e, CRE.inflow = 0, CRE.granted = 0;
        snprintf(out, sizeof out, "{\"epoch\":\"%lld\"}", (long long) e);
    } else if (strstr(r, "\"grants.pool_credit\"")) {
        int64_t a = cr_num(r, "from"), amt = cr_num(r, "amount_minor");
        if (a < 2 || a >= (int64_t) CRE.naccts || amt <= 0 || CRE.bal[a] < amt)
            return cr_err(resp, cap, id, "PAY_ERR_FUNDS");
        CRE.bal[a] -= amt, CRE.pool += amt, CRE.inflow += amt;
        snprintf(out, sizeof out, "{\"pool_minor\":\"%lld\",\"inflow_minor\":\"%lld\"}",
                 (long long) (CRE.pool + (cr_skew ? 1 : 0)), (long long) CRE.inflow);
    } else if (strstr(r, "\"grants.eval\"")) {
        uint32_t m = cr_mask(r);
        snprintf(out, sizeof out,
                 "{\"subject\":%lld,\"eligible\":%s,\"mask\":%u,\"reasons\":[],\"epoch\":\"%lld\"}",
                 (long long) cr_num(r, "subject"), m ? "false" : "true", m, (long long) CRE.epoch);
    } else if (strstr(r, "\"grants.distribute\"")) {
        return cr_distribute(r, resp, cap, id);
    } else {
        return cr_err(resp, cap, id, "method");
    }
    return cr_ok(resp, cap, id, out);
}

APPS_TEST(crown)
{
    /* the rules the model runs (crown_rules.h) */
    int64_t flag[CROWN_REASONS], s[3], e[3], t = 0;
    crown_reason_flags(0x10u, flag);
    CHECK(flag[4] == 1 && !crown_standing(flag), "crown: a fraud bit is not good standing");
    crown_reason_flags(0, flag);
    CHECK(crown_standing(flag), "crown: no bit is good standing");
    static const int64_t w[3] = {5, 3, 2};
    CHECK(crown_allot(7, w, 3, s, e, &t) == 0 && s[0] == 4 && s[1] == 2 && s[2] == 1,
          "crown: 7 by 5:3:2, largest remainder");
    CHECK(crown_draw_at(42, 5, 0) == 2 && crown_draw_at(42, 5, 1) == 0 &&
              crown_draw_at(42, 5, 5) == CROWN_REFUSED,
          "crown: the draw order from seed 42");

    zxm_link_init(&CR_LINK, cr_send, &CR_LINK);
    crown_init(&CR, &CR_LINK);

    /* nothing before the engine is set up */
    CHECK(crown_refresh(&CR, 1700000000) == ZXM_OK && !CR.set_up, "crown: not set up yet");
    CHECK(crown_propose(&CR, 7, 2500, 42, 3) == ZXM_E_STATE, "crown: no allotment before setup");

    /* the sandbox: setup, a funder's Vino account and its sandbox units */
    uint32_t funder = 0, bar[5];
    int64_t bal = 0;
    CHECK(crown_setup(&CR, 9) == ZXM_OK, "crown: setup: %s", CR_LINK.err);
    CHECK(crown_open_account(&CR, 100, false, &funder) == ZXM_OK, "crown: funder account");
    CHECK(crown_fund(&CR, funder, 1000000) == ZXM_OK, "crown: sandbox units: %s", CR_LINK.err);
    CHECK(crown_balance(&CR, funder, &bal) == ZXM_OK && bal == 1000000, "crown: funded");
    CHECK(crown_fund(&CR, funder, 0) == ZXM_E_ARG, "crown: a fund of 0 is refused here");
    CHECK(crown_open_lot(&CR, 7) == ZXM_OK, "crown: lot 7 open");

    /* an epoch and its credit (CM1) */
    CHECK(crown_epoch(&CR, 1) == ZXM_OK && CR.epoch == 1, "crown: epoch 1");
    CHECK(crown_epoch(&CR, 1) == ZXM_E_ARG, "crown: an epoch not above the current is refused");
    CHECK(crown_credit(&CR, funder, 250000, 1700000000) == ZXM_OK && CR.pool == 250000 &&
              CR.inflow == 250000,
          "crown: 250000 credited to the pool: %s", CR_LINK.err);
    cr_skew = true;
    CHECK(crown_credit(&CR, funder, 1000, 1700000000) == ZXM_E_PROTOCOL && CR.pool == 250000,
          "crown: a pool that is not old + credit is refused, nothing changes");
    cr_skew = false;
    CHECK(crown_refresh(&CR, 1700000000) == ZXM_OK && CR.pool == 251000 && CR.inflow == 251000 &&
              CR.epoch == 1,
          "crown: the engine's pool read back");

    /* members and their standing (CM2) */
    crown_member_t m;
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(crown_open_account(&CR, 200 + i, true, &bar[i]) == ZXM_OK, "crown: barrel %u", i);
        memset(&m, 0, sizeof m);
        m.subject = 200 + i, m.account = bar[i], m.weight = (int64_t) (i + 1);
        m.uptime_bps = i == 1 ? 9000 : 9900; /* member 1: low uptime */
        m.fraud = i == 3 ? 1 : 0;            /* member 3: a fraud finding */
        m.volume = 20000, m.stake = 200000;
        CHECK(crown_add_member(&CR, &m) == ZXM_OK, "crown: member %u", i);
    }
    CHECK(crown_add_member(&CR, &m) == ZXM_E_ARG, "crown: a member twice is refused");
    CHECK(crown_propose(&CR, 7, 2500, 42, 3) == ZXM_E_STATE, "crown: standing before the draw");
    for (uint32_t i = 0; i < 5; i++) CHECK(crown_eval(&CR, i) == ZXM_OK, "crown: eval %u", i);
    CHECK(CR.m[1].mask == 0x01u && !CR.m[1].standing && CR.m[3].mask == 0x10u &&
              !CR.m[3].standing && CR.m[0].standing && crown_eligible(&CR) == 3,
          "crown: three of five in good standing");

    /* the draw and the split, held for the owner (CM3-CM5) */
    int before = cr_sent;
    CHECK(crown_propose(&CR, 7, 2500, 20261021, 32) == ZXM_OK, "crown: proposed");
    CHECK(cr_sent == before && CR.held.pending, "crown: a proposal sends nothing");
    static const uint32_t elig[3] = {0, 2, 4};
    int64_t sum = 0;
    bool order = CR.held.seats == 3 && CR.held.barrels == 100 && CR.held.cost == 250000;
    for (uint32_t k = 0; k < CR.held.seats; k++) {
        order = order && CR.held.order[k] == elig[crown_draw_at(20261021, 3, k)];
        sum += CR.held.share[k];
    }
    CHECK(order && sum == 100, "crown: 100 grants to 3 members in the seed's order");
    CHECK(strstr(CR.notice, "Allotment") && strstr(CR.notice, "approval") &&
              !strstr(CR.notice, "raffle") && !strstr(CR.notice, "lottery") &&
              !strstr(CR.notice, "payout"),
          "crown: the notice speaks of an allotment awaiting approval: %s", CR.notice);
    CHECK(crown_propose(&CR, 7, 2500, 1, 3) == ZXM_E_STATE, "crown: one allotment waits at a time");
    CHECK(crown_approve(&CR, 1700000000) == ZXM_E_STATE && cr_sent == before && CR.held.pending,
          "crown: no approval without leave to post");
    crown_set_permission(&CR, true, true);
    CHECK(crown_approve(&CR, 1700000000) == ZXM_E_STATE && cr_sent == before,
          "crown: a revoked leave is no leave");
    crown_set_permission(&CR, true, false);
    int64_t want[3];
    uint32_t who[3];
    for (uint32_t k = 0; k < 3; k++) want[k] = CR.held.share[k], who[k] = CR.held.order[k];
    CHECK(crown_approve(&CR, 1700000000) == ZXM_OK && !CR.mismatch && !CR.held.pending &&
              CR.pool == 1000 && CR.granted == 100,
          "crown: approved, the pool less the cost: %s", CR_LINK.err);
    for (uint32_t k = 0; k < 3; k++)
        CHECK(crown_balance(&CR, CR.m[who[k]].account, &bal) == ZXM_OK && bal == want[k],
              "crown: member %u holds its %lld grants", who[k], (long long) want[k]);
    CHECK(crown_propose(&CR, 7, 2500, 42, 3) == ZXM_E_STATE && strstr(CR.notice, "one whole grant"),
          "crown: a pool short of one grant allots nothing");

    /* an engine that answers another split is flagged, and cancel drops a proposal */
    CHECK(crown_epoch(&CR, 2) == ZXM_OK && crown_credit(&CR, funder, 500000, 1700000100) == ZXM_OK,
          "crown: epoch 2 credited");
    for (uint32_t i = 0; i < 5; i++) CHECK(crown_eval(&CR, i) == ZXM_OK, "crown: eval again %u", i);
    CHECK(crown_propose(&CR, 7, 2500, 7, 3) == ZXM_OK && crown_cancel(&CR) == ZXM_OK &&
              !CR.held.pending && crown_cancel(&CR) == ZXM_E_STATE,
          "crown: a cancelled allotment is gone");
    cr_tamper = true;
    CHECK(crown_propose(&CR, 7, 2500, 7, 3) == ZXM_OK &&
              crown_approve(&CR, 1700000100) == ZXM_E_PROTOCOL && CR.mismatch,
          "crown: an engine split unlike the proposal is flagged");
    cr_tamper = false;
    CHECK(crown_set_cap(&CR, 2, 1) == ZXM_E_ARG && crown_set_cap(&CR, 1, 2) == ZXM_OK,
          "crown: a cap above 1 is refused");
}
