/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Forage's model against zxvd's real dispatcher (LINK). swarm.state is the
 * stub host's. The peer network (net.offers, net.command), the peer book
 * (peer.status, peer.serve) and the power ledger (power.status) are groups
 * this in-process zxvd does not link, so it refuses them, and they are
 * answered on a link of Forage's own (FO_LINK) in their shapes
 * (api/zxvd.openrpc.json, zxv_host.c net_command), which passes every
 * other request to the real dispatcher. Nothing goes on the network:
 * Forage never sends a mode, and serving is turned on only on the owner's
 * word. */
#include "forage_model.h"

static forage_t FRG;
static zxm_link_t FO_LINK;
static int fo_sent; /* requests FO_LINK answered itself */
static char fo_req[ZXM_REQ_MAX + 1];
static const char *FO_OFFERS, *FO_NET, *FO_PEER, *FO_SERVE, *FO_POWER;

static int32_t fo_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"net.offers\"")) r = FO_OFFERS;
    if (strstr(req, "\"net.command\"")) r = FO_NET;
    if (strstr(req, "\"peer.status\"")) r = FO_PEER;
    if (strstr(req, "\"peer.serve\"")) r = FO_SERVE;
    if (strstr(req, "\"power.status\"")) r = FO_POWER;
    if (!r) return inproc(NULL, req, len, resp, cap);
    fo_sent++;
    memcpy(fo_req, req, len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX);
    fo_req[len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX] = 0;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define FO_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define FO_B "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define FO_PEER_ANSWER(serving, price)                                                             \
    "{\"serving\":" serving ",\"port\":7420,\"price_minor\":\"" price "\",\"currency\":\"VFV\","   \
    "\"wallet_minor\":\"29983\",\"fees_paid_minor\":\"17\",\"buckets\":{\"reserve floor\":"        \
    "\"9\",\"V-Bill dividend pool\":\"4\",\"infrastructure/node bounties\":\"2\","                 \
    "\"regenerative capital\":\"2\"},\"sold\":1,\"bought\":1,\"refused\":0,\"sessions\":2,"        \
    "\"book_ok\":true,\"history\":[]}"
#define FO_POWER_ANSWER(mw, posture, scale)                                                        \
    "{\"device\":\"laptop\",\"host\":\"linux\",\"total_uj\":5000,\"total_mw\":" mw                 \
    ",\"source\":\"metered\",\"posture\":\"" posture "\",\"token_scale_permille\":" scale          \
    ",\"battery\":null,\"meters\":[],\"consumers\":[]}"

APPS_TEST(forage)
{
    int before;
    int64_t tr = 0, mr = 0;
    uint32_t best = 99;
    const char *was_state = STATE;

    /* FR1-FR4, the C the model runs */
    int64_t am[3] = {600, 300, 0}, mi[3] = {10, 5, 1}, rk[3];
    CHECK(forage_net_after_fee(20000) == 19983 && forage_net_after_fee(1125) == 1124 &&
              forage_net_after_fee(0) == 0 && forage_net_after_fee(-1) == FORAGE_REFUSED,
          "forage: the net after the fee (FR1)");
    CHECK(forage_claim_fits(1, 0, 0, 0) && !forage_claim_fits(0, 0, 0, 0) &&
              !forage_claim_fits(1, 0, -1, 0) && !forage_claim_fits(1, 0, 0, -1) &&
              !forage_claim_fits(1, FORAGE_NINE + 1, 0, 0),
          "forage: a claim inside the budgets (FR2)");
    CHECK(forage_rate_rank(am, mi, 3, rk) && rk[0] == 1 && rk[1] == 1 && rk[2] == 3,
          "forage: 60, 60 and 0 a minute rank 1, 1, 3 (FR3)");
    CHECK(forage_record_fits(9999, 1, 1, 0, 0, 1, 0) &&
              !forage_record_fits(10000, 1, 1, 0, 0, 1, 0),
          "forage: a bounty's record (FR4)");

    /* the real dispatcher: no peer network, peer book or power ledger here */
    forage_init(&FRG, &LINK);
    before = sent;
    CHECK(forage_approve(&FRG, true) == ZXM_E_STATE && forage_cancel(&FRG) == ZXM_E_STATE &&
              forage_rank(&FRG) == ZXM_E_STATE && forage_claim(&FRG, 0) == ZXM_E_ARG &&
              forage_best(&FRG, &best) == ZXM_E_STATE && sent == before,
          "forage: an empty board: nothing asked");
    CHECK(forage_serve(&FRG, 100, false) == ZXM_E_STATE &&
              forage_serve_stop(&FRG, false) == ZXM_E_STATE &&
              forage_serve(&FRG, -1, true) == ZXM_E_ARG &&
              forage_serve(&FRG, FORAGE_NINE + 1, true) == ZXM_E_ARG && sent == before &&
              !FRG.serving && strstr(FRG.notice, "word"),
          "forage: serving is never touched without the owner: %s", FRG.notice);
    CHECK(forage_read_offers(&FRG) == ZXM_E_REMOTE && !FRG.offers_known &&
              strstr(LINK.err, "available"),
          "forage: net.offers refused by this zxvd: %s", LINK.err);
    CHECK(forage_read_peer(&FRG) == ZXM_E_REMOTE && !FRG.peer_known,
          "forage: peer.status refused: %s", LINK.err);
    CHECK(forage_serve(&FRG, 100, true) == ZXM_E_REMOTE && !FRG.serving,
          "forage: peer.serve refused here, serving still off: %s", FRG.notice);
    CHECK(forage_read_power(&FRG) == ZXM_E_REMOTE && !FRG.power_known,
          "forage: power.status is not linked here: %s", LINK.err);

    /* swarm.state, from the stub host */
    STATE = "{\"tokens_per_cycle\":1000,\"bounties_returned\":3}";
    CHECK(forage_read_swarm(&FRG) == ZXM_E_PROTOCOL && !FRG.swarm_known,
          "forage: a swarm without its quest is refused");
    STATE = "{\"tokens_per_cycle\":-5,\"bounties_returned\":3,\"quest\":{\"contributed\":12,"
            "\"refused\":0,\"skills\":[]}}";
    CHECK(forage_read_swarm(&FRG) == ZXM_E_PROTOCOL && !FRG.swarm_known,
          "forage: negative tokens a cycle are refused");
    STATE = "{\"tokens_per_cycle\":1000,\"bounties_returned\":3,\"quest\":{\"contributed\":12,"
            "\"refused\":1,\"skills\":[]},\"cycle\":4}";
    CHECK(forage_read_swarm(&FRG) == ZXM_OK && FRG.swarm_known && FRG.cycle_tokens == 1000 &&
              FRG.bounties_returned == 3 && FRG.quest_contributed == 12 && FRG.quest_refused == 1,
          "forage: the swarm: %s", FRG.notice);
    STATE = was_state;

    /* the peer network, the peer book and the power ledger on their own link */
    zxm_link_init(&FO_LINK, fo_send, &FO_LINK);
    {
        forage_t keep = FRG;
        forage_init(&FRG, &FO_LINK);
        FRG.cycle_tokens = keep.cycle_tokens;
        FRG.bounties_returned = keep.bounties_returned;
        FRG.quest_contributed = keep.quest_contributed;
        FRG.quest_refused = keep.quest_refused;
        FRG.swarm_known = keep.swarm_known;
    }
    FO_POWER = FO_POWER_ANSWER("4000", "balanced", "1000");
    CHECK(forage_read_power(&FRG) == ZXM_E_PROTOCOL && !FRG.power_known,
          "forage: a posture whose scale is not its own is refused");
    FO_POWER = FO_POWER_ANSWER("-1", "balanced", "618");
    CHECK(forage_read_power(&FRG) == ZXM_E_PROTOCOL && !FRG.power_known,
          "forage: a negative draw is refused");
    FO_POWER = FO_POWER_ANSWER("4000", "dozing", "618");
    CHECK(forage_read_power(&FRG) == ZXM_E_PROTOCOL && !FRG.power_known,
          "forage: a posture zxvd does not name is refused");
    FO_POWER = FO_POWER_ANSWER("4000", "balanced", "618");
    CHECK(forage_read_power(&FRG) == ZXM_OK && FRG.power_known && FRG.draw_mw == 4000 &&
              FRG.scale_permille == 618,
          "forage: the power ledger: %s", FRG.notice);
    FO_PEER = FO_PEER_ANSWER("false", "0");
    CHECK(forage_read_peer(&FRG) == ZXM_OK && FRG.peer_known && !FRG.serving &&
              FRG.wallet == 29983 && FRG.book_ok,
          "forage: the peer book: %s", FRG.notice);
    FO_OFFERS = "{\"share\":{\"compute\":\"10\",\"storage\":\"0\"},\"wants\":[{\"slot\":0,"
                "\"resource\":\"compute\",\"units_left\":\"4\",\"max_price\":\"300\"}],"
                "\"outstanding\":[{\"role\":\"buy\",\"peer\":\"" FO_B "\",\"units\":\"1\","
                "\"seq\":\"7\",\"tries\":2,\"want\":0},{\"role\":\"sell\",\"peer\":\"" FO_A
                "\",\"units\":\"3\",\"seq\":\"9\",\"tries\":0,\"want\":-1}]}";
    CHECK(forage_read_offers(&FRG) == ZXM_OK && FRG.offers_known && FRG.nwant == 1 &&
              FRG.nsell == 1 && FRG.nbuy == 1 && FRG.share_compute == 10,
          "forage: the offers: %s", FRG.notice);
    FO_OFFERS = "{\"share\":{\"compute\":\"10\",\"storage\":\"0\"},\"wants\":[],"
                "\"outstanding\":[{\"role\":\"sell\",\"peer\":\"" FO_A "\",\"units\":\"3\","
                "\"seq\":\"9\",\"tries\":0,\"want\":2}]}";
    CHECK(forage_read_offers(&FRG) == ZXM_E_PROTOCOL && FRG.nwant == 1 && FRG.nsell == 1,
          "forage: a sell that reserves a want is not zxvd's; nothing kept");
    FO_OFFERS = "{\"share\":{\"compute\":\"10\",\"storage\":\"0\"},\"wants\":[],"
                "\"outstanding\":[{\"role\":\"sell\",\"peer\":\"AA\",\"units\":\"3\","
                "\"seq\":\"9\",\"tries\":0,\"want\":-1}]}";
    CHECK(forage_read_offers(&FRG) == ZXM_E_PROTOCOL && FRG.nsell == 1,
          "forage: a NodeID that is not 64 hex digits is refused");

    /* FG3: the owner's budgets */
    CHECK(forage_set_budget(&FRG, 0, 6000, true) == ZXM_E_ARG &&
              forage_set_budget(&FRG, 500, FORAGE_NINE + 1, true) == ZXM_E_ARG &&
              forage_set_budget(&FRG, 500, 6000, false) == ZXM_E_STATE && FRG.budget_tokens == 0 &&
              FRG.budget_mw == 0,
          "forage: budgets out of range or without the owner's word: unchanged");

    /* FG2: the board */
    CHECK(forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 2, 20000, 10, 300, 1000) == ZXM_OK &&
              forage_add_bounty(&FRG, FO_B, FORAGE_STORAGE, 1, 9000, 3, 100, 500) == ZXM_OK &&
              forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 5, 50000, 100, 450, 2500) == ZXM_OK &&
              forage_add_bounty(&FRG, FO_B, FORAGE_COMPUTE, 1, 1000, 1, 600, 0) == ZXM_OK &&
              FRG.nboard == 4 && FRG.board[0].net == 19983 && FRG.board[1].net == 8992 &&
              FRG.board[2].net == 49956 && !FRG.ranked,
          "forage: four bounties on the board: %s", FRG.notice);
    CHECK(forage_add_bounty(&FRG, "AA", FORAGE_COMPUTE, 1, 1, 1, 1, 0) == ZXM_E_ARG &&
              forage_add_bounty(&FRG, FO_A, (forage_res_t) 3, 1, 1, 1, 1, 0) == ZXM_E_ARG &&
              forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 1, 0, 1, 1, 0) == ZXM_E_ARG &&
              forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 1, 1, 0, 1, 0) == ZXM_E_ARG &&
              forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 1, 1, 1, 0, 0) == ZXM_E_ARG &&
              forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 1, 1, 1, 1, -1) == ZXM_E_ARG &&
              FRG.nboard == 4,
          "forage: a bounty out of range is not put on the board");
    CHECK(forage_best(&FRG, &best) == ZXM_E_STATE, "forage: nothing is best before ranking");
    CHECK(forage_rank(&FRG) == ZXM_OK && FRG.ranked && FRG.board[1].rank == 1 &&
              FRG.board[0].rank == 2 && FRG.board[3].rank == 3 && FRG.board[2].rank == 4,
          "forage: 8992/3, 19983/10, 1000/1 and 49956/100 a minute rank 1, 2, 3, 4");
    before = fo_sent;
    CHECK(forage_claim(&FRG, 1) == ZXM_E_ARG && strstr(FRG.notice, "budgets") &&
              !FRG.held.pending && FRG.nrec == 0 && fo_sent == before,
          "forage: no claim before the budgets are set: %s", FRG.notice);
    CHECK(forage_set_budget(&FRG, 500, 6000, true) == ZXM_OK && FRG.budget_tokens == 500 &&
              FRG.budget_mw == 6000,
          "forage: the owner's budgets: %s", FRG.notice);
    /* a cycle of 1000 tokens at 618 per mille is 618; the owner's 500 is less */
    CHECK(forage_rooms(&FRG, 1, &tr, &mr) && tr == 400 && mr == 1500 &&
              forage_rooms(&FRG, 2, &tr, &mr) && tr == 50 && mr == -500 &&
              forage_rooms(&FRG, 3, &tr, &mr) && tr == -100 && !forage_rooms(&FRG, 4, &tr, &mr),
          "forage: each claim's margins (FG3)");
    CHECK(forage_best(&FRG, &best) == ZXM_OK && best == 1, "forage: the best that fits is 1");

    /* FG4: a claim waits for the owner, then goes */
    CHECK(forage_claim(&FRG, 3) == ZXM_E_ARG && strstr(FRG.notice, "swarm") &&
              forage_claim(&FRG, 2) == ZXM_E_ARG && strstr(FRG.notice, "power") &&
              forage_claim(&FRG, 9) == ZXM_E_ARG && !FRG.held.pending && fo_sent == before,
          "forage: claims past the budgets refused, nothing asked");
    CHECK(forage_claim(&FRG, 1) == ZXM_OK && FRG.held.pending && FRG.held.net == 8992 &&
              FRG.held.projected && FRG.held.wallet_after == 38975 &&
              !strcmp(FRG.held.cmd, "sell storage 1 8992 " FO_B) && fo_sent == before &&
              strstr(FRG.notice, "Waiting for your approval") && FRG.nrec == 1 &&
              FRG.rec[0].status == FORAGE_WAITING,
          "forage: a claim held, nothing sent: %s", FRG.notice);
    CHECK(forage_claim(&FRG, 0) == ZXM_E_STATE &&
              forage_post(&FRG, FORAGE_COMPUTE, 1, 100) == ZXM_E_STATE && fo_sent == before,
          "forage: one waits at a time");
    FO_NET = "{\"result\":\"offered\"}";
    CHECK(forage_approve(&FRG, false) == ZXM_E_STATE && fo_sent == before && FRG.held.pending &&
              FRG.rec[0].status == FORAGE_WAITING && !FRG.board[1].claimed &&
              strstr(FRG.notice, "your word"),
          "forage: without the owner's word nothing is sent: %s", FRG.notice);
    CHECK(forage_approve(&FRG, true) == ZXM_OK && fo_sent == before + 1 &&
              strstr(fo_req, "\"command\":\"sell storage 1 8992 " FO_B "\"") && !FRG.held.pending &&
              FRG.rec[0].status == FORAGE_SENT && FRG.board[1].claimed &&
              !strcmp(FRG.answer, "offered"),
          "forage: approved and sent: %s", FRG.notice);
    CHECK(forage_approve(&FRG, true) == ZXM_E_STATE && forage_cancel(&FRG) == ZXM_E_STATE &&
              forage_claim(&FRG, 1) == ZXM_E_STATE && strstr(FRG.notice, "already"),
          "forage: an approval is spent once, a bounty claimed once");
    /* the claim sent takes its 100 tokens from the budget */
    CHECK(forage_rooms(&FRG, 0, &tr, &mr) && tr == 100 && mr == 1000 &&
              forage_best(&FRG, &best) == ZXM_OK && best == 0,
          "forage: the next best is 0, with 100 tokens to spare");

    /* zxvd refuses a claim; an answer that is not the sell's leaves it waiting */
    before = fo_sent;
    FO_NET = "{\"result\":\"networking is on\"}";
    CHECK(forage_claim(&FRG, 0) == ZXM_OK && forage_approve(&FRG, true) == ZXM_E_PROTOCOL &&
              FRG.held.pending && FRG.rec[1].status == FORAGE_WAITING && !FRG.board[0].claimed,
          "forage: an answer that is not the sell's is not zxvd's; it still waits");
    FO_NET = "{\"result\":\"refused\"}";
    CHECK(forage_approve(&FRG, true) == ZXM_OK && fo_sent == before + 2 &&
              strstr(fo_req, "\"command\":\"sell compute 2 19983 " FO_A "\"") &&
              FRG.rec[1].status == FORAGE_REFUSED_BY_ZXVD && !FRG.board[0].claimed &&
              strstr(FRG.notice, "refused"),
          "forage: zxvd refused the claim: %s", FRG.notice);

    /* FG4: a bounty of the node's own, posted at the net of its value */
    before = fo_sent;
    CHECK(forage_post(&FRG, FORAGE_COMPUTE, 0, 20000) == ZXM_E_ARG &&
              forage_post(&FRG, FORAGE_COMPUTE, 4, 0) == ZXM_E_ARG &&
              forage_post(&FRG, (forage_res_t) 0, 4, 20000) == ZXM_E_ARG && fo_sent == before,
          "forage: a post out of range: nothing asked");
    CHECK(forage_post(&FRG, FORAGE_COMPUTE, 4, 20000) == ZXM_OK && FRG.held.net == 19983 &&
              FRG.held.fee == 17 && FRG.held.wallet_after == 9983 &&
              !strcmp(FRG.held.cmd, "want compute 4 19983") && fo_sent == before,
          "forage: a post held, at most its value paid: %s", FRG.notice);
    CHECK(forage_cancel(&FRG) == ZXM_OK && !FRG.held.pending &&
              FRG.rec[2].status == FORAGE_CANCELLED && fo_sent == before &&
              strstr(FRG.notice, "nothing was sent"),
          "forage: cancelled, nothing sent");
    FO_NET = "{\"result\":\"wanted\"}";
    CHECK(forage_post(&FRG, FORAGE_STORAGE, 2, 9000) == ZXM_OK &&
              forage_approve(&FRG, true) == ZXM_OK && fo_sent == before + 1 &&
              strstr(fo_req, "\"command\":\"want storage 2 8992\"") &&
              FRG.rec[3].status == FORAGE_SENT,
          "forage: a bounty posted: %s", FRG.notice);

    /* FG5: serving, on the owner's word only */
    before = fo_sent;
    FO_SERVE = FO_PEER_ANSWER("true", "100");
    CHECK(forage_serve(&FRG, 100, false) == ZXM_E_STATE && fo_sent == before && !FRG.serving,
          "forage: serving stays off without the owner");
    CHECK(forage_serve(&FRG, 100, true) == ZXM_OK && fo_sent == before + 1 &&
              strstr(fo_req, "\"price_minor\":100") && FRG.serving && FRG.serve_price == 100,
          "forage: serving turned on by the owner: %s", FRG.notice);
    CHECK(forage_serve_stop(&FRG, true) == ZXM_E_PROTOCOL && FRG.serving,
          "forage: zxvd says it still serves: not taken as stopped");
    FO_SERVE = FO_PEER_ANSWER("false", "100");
    CHECK(forage_serve_stop(&FRG, false) == ZXM_E_STATE && fo_sent == before + 2 &&
              forage_serve_stop(&FRG, true) == ZXM_OK && fo_sent == before + 3 &&
              !strstr(fo_req, "price_minor") && !FRG.serving,
          "forage: serving turned off: %s", FRG.notice);

    /* FG7: the records, bounded */
    CHECK(forage_records_fit(&FRG) && FRG.nrec == 4, "forage: every record fits FR4");
    for (uint32_t i = 0; i < FORAGE_RECORDS; i++) {
        (void) forage_post(&FRG, FORAGE_COMPUTE, 1, 100);
        (void) forage_cancel(&FRG);
    }
    CHECK(FRG.nrec == FORAGE_RECORDS && FRG.rec[0].no == 4 && forage_records_fit(&FRG),
          "forage: the list is bounded, the oldest dropped first");
    /* the board, full */
    while (FRG.nboard < FORAGE_BOUNTIES)
        (void) forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 1, 100, 1, 1, 0);
    CHECK(forage_add_bounty(&FRG, FO_A, FORAGE_COMPUTE, 1, 100, 1, 1, 0) == ZXM_E_STATE &&
              FRG.nboard == FORAGE_BOUNTIES && forage_rank(&FRG) == ZXM_OK,
          "forage: the board is bounded");
    FO_OFFERS = FO_NET = FO_PEER = FO_SERVE = FO_POWER = NULL;
}
