/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fairground's model against zxvd's real dispatcher (LINK). mesh.books is
 * zxvd's own, read from a scratch books file in the test's folder (never
 * the owner's), with sales and purchases posted as zxvd posts them
 * (zxvd_rpc_mesh_post); swarm.state is the stub host's. The peer network
 * (net.offers, net.command) and the peer book (peer.status) are hosts this
 * in-process zxvd does not link, so it refuses them ("not available"), and
 * they are answered on a link of Fairground's own (FG_LINK) in their
 * shapes (api/zxvd.openrpc.json), which passes every other request to the
 * real dispatcher. Nothing goes on the network: Fairground never sends a
 * mode. */
#include "fairground_model.h"

static fairground_t FG, FG2;
static zxm_link_t FG_LINK;
static int fg_sent; /* requests FG_LINK answered itself */
static char fg_req[ZXM_REQ_MAX + 1];
static const char *FG_OFFERS, *FG_NET, *FG_PEER;

static int32_t fg_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"net.offers\"")) r = FG_OFFERS;
    if (strstr(req, "\"net.command\"")) r = FG_NET;
    if (strstr(req, "\"peer.status\"")) r = FG_PEER;
    if (!r) return inproc(NULL, req, len, resp, cap);
    fg_sent++;
    memcpy(fg_req, req, len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX);
    fg_req[len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX] = 0;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define FG_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define FG_B "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define FG_PARTS(r, v, i, g)                                                                       \
    "{\"reserve floor\":\"" r "\",\"V-Bill dividend pool\":\"" v                                   \
    "\",\"infrastructure/node bounties\":\"" i "\",\"regenerative capital\":\"" g "\"}"
#define FG_SETTLE(role, peer, price, fee, paid, parts)                                             \
    "{\"role\":\"" role "\",\"peer\":\"" peer "\",\"price_minor\":\"" price                        \
    "\",\"fee_minor\":\"" fee "\",\"paid_minor\":\"" paid "\",\"fee_parts\":" parts                \
    ",\"invoice\":\"" FG_A "\",\"settle\":\"" FG_A "\",\"session\":\"" FG_A "\"}"
#define FG_STATUS(history)                                                                         \
    "{\"serving\":true,\"port\":7420,\"price_minor\":\"100\",\"currency\":\"VFV\","                \
    "\"wallet_minor\":\"29983\",\"fees_paid_minor\":\"17\",\"buckets\":" FG_PARTS(                 \
        "9", "4", "2", "2") ",\"sold\":1,\"bought\":1,\"refused\":0,\"sessions\":2,\"book_ok\":"   \
                            "true,\"history\":[" history "]}"

APPS_TEST(fairground)
{
    int before;
    int64_t num = 0, den = 0;
    const char *was_state = STATE;

    /* FR1-FR5, the C the model runs */
    int64_t pr[3] = {300, 400, 0}, un[3] = {2, 4, 6}, parts[FAIRGROUND_PARTS];
    CHECK(fairground_assurance_fee(0) == 0 && fairground_assurance_fee(1124) == 0 &&
              fairground_assurance_fee(1125) == 1 && fairground_assurance_fee(20000) == 17 &&
              fairground_assurance_fee(10000000) == 8889 &&
              fairground_assurance_fee(-1) == FAIRGROUND_REFUSED &&
              fairground_assurance_fee(FAIRGROUND_AMOUNT_MAX + 1) == FAIRGROUND_REFUSED,
          "fairground: the assurance fee (FR1)");
    CHECK(fairground_offer_allowed(1, 1, 0, 0) && !fairground_offer_allowed(1, 1, -1, 0) &&
              !fairground_offer_allowed(1, 1, 0, -1) && !fairground_offer_allowed(0, 1, 0, 0) &&
              !fairground_offer_allowed(FAIRGROUND_NINE + 1, 1, 0, 0),
          "fairground: an offer inside the limits (FR2)");
    CHECK(fairground_unit_price(pr, un, 3, &num, &den) && num == 175 && den == 3 &&
              !fairground_unit_price(pr, un, 0, &num, &den),
          "fairground: 700 over 12 units is 175/3 (FR3)");
    CHECK(fairground_record_fits(0, 1, 1, 0, 0, 0) && !fairground_record_fits(0, 0, 1, 0, 0, 0),
          "fairground: an offer's record (FR4)");
    CHECK(fairground_fee_parts(17, parts) && parts[0] == 10 && parts[1] == 4 && parts[2] == 2 &&
              parts[3] == 1,
          "fairground: 17 split 10, 4, 2, 1 (FR5)");

    /* the real dispatcher: no peer network and no peer book here */
    fairground_init(&FG, &LINK);
    before = sent;
    CHECK(fairground_approve(&FG) == ZXM_E_STATE && fairground_cancel(&FG) == ZXM_E_STATE &&
              fairground_want(&FG, FAIRGROUND_COMPUTE, 1, 1) == ZXM_E_ARG &&
              strstr(FG.notice, "Set your limits") &&
              fairground_unit_price_of(&FG, FAIRGROUND_WANT, &num, &den) == ZXM_E_STATE &&
              sent == before,
          "fairground: nothing held, no limits: nothing asked: %s", FG.notice);
    CHECK(fairground_read_offers(&FG) == ZXM_E_REMOTE && !FG.offers_known &&
              strstr(LINK.err, "available"),
          "fairground: net.offers refused by this zxvd: %s", LINK.err);
    CHECK(fairground_read_peer(&FG) == ZXM_E_REMOTE && !FG.peer_known,
          "fairground: peer.status refused: %s", LINK.err);

    /* mesh.books, zxvd's own, on scratch books */
    char root[256], scratch[512];
    uint8_t inv[32];
    for (int i = 0; i < 32; i++) inv[i] = (uint8_t) (0x50 + i);
    CHECK(zxf_root(root, sizeof root) == 0, "fairground: the test's folder");
    snprintf(scratch, sizeof scratch, "%s/fairground-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
          "fairground: scratch books open");
    CHECK(fairground_read_books(&FG) == ZXM_OK && FG.books_known && FG.wallet == 0 &&
              FG.sold == 0 && FG.bought == 0,
          "fairground: the books before any sale: %s", LINK.err);
    CHECK(zxvd_rpc_mesh_post(true, 50000, 0, inv) == 0, "fairground: a sale of 50000 settles");
    inv[0] = 0x01;
    CHECK(zxvd_rpc_mesh_post(false, 20000, (uint64_t) fairground_assurance_fee(20000), inv) == 0,
          "fairground: a purchase of 20000 and its fee settle");
    CHECK(fairground_read_books(&FG) == ZXM_OK && FG.wallet == 29983 && FG.fees == 17 &&
              FG.sold == 1 && FG.bought == 1 && strstr(FG.notice, "Wallet 29983 VFV"),
          "fairground: the books: %s", FG.notice);

    /* swarm.state's market, from the stub host */
    STATE = "{\"money_supply\":1000000,\"pot\":2500}";
    CHECK(fairground_read_market(&FG) == ZXM_E_PROTOCOL && !FG.market_known,
          "fairground: a market without its levy and fees is refused");
    STATE = "{\"money_supply\":1000000,\"pot\":2500,\"levied\":40,\"fees\":12,\"cycle\":3}";
    CHECK(fairground_read_market(&FG) == ZXM_OK && FG.market_known && FG.money_supply == 1000000 &&
              FG.pot == 2500 && FG.levied == 40 && FG.market_fees == 12,
          "fairground: the swarm's market: %s", FG.notice);
    STATE = was_state;

    /* the peer network on its own link */
    zxm_link_init(&FG_LINK, fg_send, &FG_LINK);
    fairground_init(&FG, &FG_LINK);
    FG_OFFERS = "{\"share\":{\"compute\":\"10\",\"storage\":\"0\"},\"wants\":[{\"slot\":0,"
                "\"resource\":\"compute\",\"units_left\":\"4\",\"max_price\":\"300\"}],"
                "\"outstanding\":[{\"role\":\"buy\",\"peer\":\"" FG_B "\",\"units\":\"1\","
                "\"seq\":\"7\",\"tries\":2,\"want\":0},{\"role\":\"sell\",\"peer\":\"" FG_A
                "\",\"units\":\"3\",\"seq\":\"9\",\"tries\":0,\"want\":-1}]}";
    CHECK(fairground_read_offers(&FG) == ZXM_OK && FG.offers_known && FG.share_compute == 10 &&
              FG.nwant == 1 && FG.want[0].max_price == 300 && FG.nout == 2 && !FG.out[0].sell &&
              FG.out[1].sell && FG.out[1].want == -1 && FG.out[0].tries == 2,
          "fairground: the offers: %s", FG.notice);
    FG_OFFERS = "{\"share\":{\"compute\":\"10\",\"storage\":\"0\"},\"wants\":[],"
                "\"outstanding\":[{\"role\":\"sell\",\"peer\":\"" FG_A "\",\"units\":\"3\","
                "\"seq\":\"9\",\"tries\":0,\"want\":2}]}";
    CHECK(fairground_read_offers(&FG) == ZXM_E_PROTOCOL && FG.nout == 2 && FG.nwant == 1,
          "fairground: a sell that reserves a want is not zxvd's; nothing kept");
    FG_OFFERS = "{\"share\":{\"compute\":\"010\",\"storage\":\"0\"},\"wants\":[],"
                "\"outstanding\":[]}";
    CHECK(fairground_read_offers(&FG) == ZXM_E_PROTOCOL && FG.share_compute == 10,
          "fairground: a leading zero is not zxvd's");
    FG_OFFERS = "{\"share\":{\"compute\":\"1\",\"storage\":\"0\"},\"wants\":[{\"slot\":0,"
                "\"resource\":\"gold\",\"units_left\":\"4\",\"max_price\":\"300\"}],"
                "\"outstanding\":[]}";
    CHECK(fairground_read_offers(&FG) == ZXM_E_PROTOCOL && FG.share_compute == 10,
          "fairground: a resource zxvd does not name");

    /* the peer book: every settlement's fee is FR1's, its parts FR5's */
    FG_PEER = FG_STATUS(
        FG_SETTLE("seller", FG_A, "50000", "44", "0", FG_PARTS("23", "11", "6", "4")) "," FG_SETTLE(
            "buyer", FG_B, "20000", "17", "20017", FG_PARTS("10", "4", "2", "1")));
    CHECK(fairground_read_peer(&FG) == ZXM_OK && FG.peer_known && FG.serving && FG.book_ok &&
              FG.nsettle == 2 && !FG.settle[0].buyer && FG.settle[0].fee == 44 &&
              FG.settle[1].paid == 20017 && FG.settle[1].part[0] == 10 && FG.peer_price == 100,
          "fairground: the peer book: %s", FG.notice);
    FG_PEER =
        FG_STATUS(FG_SETTLE("buyer", FG_B, "20000", "18", "20018", FG_PARTS("9", "4", "2", "3")));
    CHECK(fairground_read_peer(&FG) == ZXM_E_PROTOCOL && FG.nsettle == 2,
          "fairground: a fee that is not 0.08889%% of the price is refused; nothing kept");
    FG_PEER =
        FG_STATUS(FG_SETTLE("buyer", FG_B, "20000", "17", "20000", FG_PARTS("10", "4", "2", "1")));
    CHECK(fairground_read_peer(&FG) == ZXM_E_PROTOCOL && FG.nsettle == 2,
          "fairground: a purchase that did not pay its fee is refused");
    FG_PEER =
        FG_STATUS(FG_SETTLE("buyer", FG_B, "20000", "17", "20017", FG_PARTS("8", "4", "2", "2")));
    CHECK(fairground_read_peer(&FG) == ZXM_E_PROTOCOL && FG.nsettle == 2,
          "fairground: parts that are not the four-bucket split are refused");
    FG_PEER =
        FG_STATUS(FG_SETTLE("seller", FG_B, "50000", "44", "44", FG_PARTS("23", "11", "6", "4")));
    CHECK(fairground_read_peer(&FG) == ZXM_E_PROTOCOL, "fairground: a seller pays nothing");

    /* FM2: the owner's limits */
    CHECK(fairground_set_limits(&FG, 0, 1, 1) == ZXM_E_ARG &&
              fairground_set_limits(&FG, 1, 1, FAIRGROUND_NINE + 1) == ZXM_E_ARG &&
              fairground_set_limits(&FG, -1, 1, 1) == ZXM_E_ARG && FG.most_units == 0,
          "fairground: limits outside 1..999999999 refused");
    CHECK(fairground_set_limits(&FG, 20000, 400, 50) == ZXM_OK && FG.ceiling == 20000 &&
              FG.floor_price == 400 && FG.most_units == 50,
          "fairground: limits set: %s", FG.notice);

    /* FM3: a want waits for the owner, then goes */
    before = fg_sent;
    CHECK(fairground_want(&FG, FAIRGROUND_COMPUTE, 5, 20001) == ZXM_E_ARG &&
              strstr(FG.notice, "ceiling") &&
              fairground_want(&FG, FAIRGROUND_COMPUTE, 51, 300) == ZXM_E_ARG &&
              strstr(FG.notice, "most") &&
              fairground_want(&FG, FAIRGROUND_COMPUTE, 0, 300) == ZXM_E_ARG &&
              fairground_want(&FG, FAIRGROUND_COMPUTE, 5, 0) == ZXM_E_ARG &&
              fairground_want(&FG, (fairground_res_t) 3, 5, 300) == ZXM_E_ARG && !FG.held.pending &&
              FG.nrec == 0 && fg_sent == before,
          "fairground: wants past the limits refused, nothing asked");
    CHECK(fairground_read_books(&FG) == ZXM_OK && FG.wallet == 29983,
          "fairground: the books through the market's link");
    before = fg_sent;
    CHECK(fairground_want(&FG, FAIRGROUND_COMPUTE, 5, 20000) == ZXM_OK && FG.held.pending &&
              FG.held.fee == 17 && FG.held.projected && FG.held.wallet_after == 9966 &&
              !strcmp(FG.held.cmd, "want compute 5 20000") && fg_sent == before &&
              strstr(FG.notice, "Waiting for your approval") &&
              strstr(FG.notice, "Wallet after one receipt: 9966 VFV") && FG.nrec == 1 &&
              FG.rec[0].status == FAIRGROUND_WAITING,
          "fairground: a want held, nothing sent: %s", FG.notice);
    CHECK(fairground_share(&FG, 1, 1) == ZXM_E_STATE &&
              fairground_sell(&FG, FAIRGROUND_STORAGE, 1, 500, FG_A) == ZXM_E_STATE &&
              fg_sent == before,
          "fairground: one offer waits at a time");
    FG_NET = "{\"result\":\"wanted\"}";
    CHECK(fairground_approve(&FG) == ZXM_OK && fg_sent == before + 1 &&
              strstr(fg_req, "\"command\":\"want compute 5 20000\"") && !FG.held.pending &&
              FG.rec[0].status == FAIRGROUND_SENT && !strcmp(FG.answer, "wanted"),
          "fairground: approved and sent: %s", FG.notice);
    CHECK(fairground_approve(&FG) == ZXM_E_STATE && fairground_cancel(&FG) == ZXM_E_STATE,
          "fairground: an approval is spent once");

    /* FM3: a sell, refused by zxvd; a cancelled one sends nothing */
    before = fg_sent;
    CHECK(fairground_sell(&FG, FAIRGROUND_STORAGE, 3, 399, FG_A) == ZXM_E_ARG &&
              strstr(FG.notice, "floor") &&
              fairground_sell(&FG, FAIRGROUND_STORAGE, 3, 500, "AA") == ZXM_E_ARG &&
              fairground_sell(&FG, FAIRGROUND_STORAGE, 3, 500, NULL) == ZXM_E_ARG &&
              fg_sent == before,
          "fairground: a sell below the floor or to no NodeID: nothing asked");
    CHECK(fairground_sell(&FG, FAIRGROUND_STORAGE, 3, 500, FG_B) == ZXM_OK && FG.held.projected &&
              FG.held.wallet_after == 30483 && FG.held.fee == 0 &&
              strstr(FG.notice, "Sell 3 storage units for 500 to 01234567") && fg_sent == before,
          "fairground: a sell held: %s", FG.notice);
    CHECK(fairground_cancel(&FG) == ZXM_OK && !FG.held.pending && fg_sent == before &&
              FG.rec[1].status == FAIRGROUND_CANCELLED && strstr(FG.notice, "nothing was sent"),
          "fairground: cancelled, nothing sent");
    FG_NET = "{\"result\":\"refused\"}";
    CHECK(fairground_sell(&FG, FAIRGROUND_COMPUTE, 2, 450, FG_B) == ZXM_OK &&
              fairground_approve(&FG) == ZXM_OK && fg_sent == before + 1 &&
              strstr(fg_req, "\"command\":\"sell compute 2 450 " FG_B "\"") &&
              FG.rec[2].status == FAIRGROUND_REFUSED_BY_ZXVD && strstr(FG.notice, "refused"),
          "fairground: zxvd refused the sell: %s", FG.notice);
    FG_NET = "{\"result\":\"networking is on\"}";
    CHECK(fairground_sell(&FG, FAIRGROUND_COMPUTE, 2, 450, FG_B) == ZXM_OK &&
              fairground_approve(&FG) == ZXM_E_PROTOCOL && FG.held.pending &&
              FG.rec[3].status == FAIRGROUND_WAITING,
          "fairground: an answer that is not the sell's is not zxvd's; it still waits");
    FG_NET = "{\"result\":\"offered\"}";
    CHECK(fairground_approve(&FG) == ZXM_OK && FG.rec[3].status == FAIRGROUND_SENT,
          "fairground: the sell offered");

    /* FM3: share, and stopping it */
    FG_NET = "{\"result\":\"sharing\"}";
    CHECK(fairground_share(&FG, -1, 0) == ZXM_E_ARG &&
              fairground_share(&FG, 0, FAIRGROUND_MINOR_MAX + 1) == ZXM_E_ARG &&
              fairground_share(&FG, 10, 4096) == ZXM_OK && strstr(FG.notice, "approval") &&
              fairground_approve(&FG) == ZXM_OK &&
              strstr(fg_req, "\"command\":\"share 10 4096\"") &&
              FG.rec[4].status == FAIRGROUND_SENT,
          "fairground: sharing approved: %s", FG.notice);
    FG_NET = "{\"result\":\"sharing nothing\"}";
    CHECK(fairground_share(&FG, 0, 0) == ZXM_OK && strstr(FG.notice, "Stop sharing") &&
              fairground_approve(&FG) == ZXM_OK && FG.rec[5].status == FAIRGROUND_SENT,
          "fairground: sharing stopped");

    /* FM5: the records and the price per unit across the sent offers */
    FG_NET = "{\"result\":\"wanted\"}";
    CHECK(fairground_want(&FG, FAIRGROUND_STORAGE, 7, 300) == ZXM_OK &&
              fairground_approve(&FG) == ZXM_OK,
          "fairground: a second want");
    CHECK(fairground_unit_price_of(&FG, FAIRGROUND_WANT, &num, &den) == ZXM_OK && num == 5075 &&
              den == 3 && fairground_unit_price_of(&FG, FAIRGROUND_SELL, &num, &den) == ZXM_OK &&
              num == 225 && den == 1 &&
              fairground_unit_price_of(&FG, FAIRGROUND_SHARE, &num, &den) == ZXM_E_ARG,
          "fairground: 20300 over 12 units wanted is 5075/3; 450 over 2 sold is 225");
    CHECK(fairground_records_fit(&FG) && FG.nrec == 7, "fairground: every record fits FR4");
    for (uint32_t i = 0; i < FAIRGROUND_RECORDS; i++) {
        (void) fairground_share(&FG, 1, 1);
        (void) fairground_cancel(&FG);
    }
    CHECK(FG.nrec == FAIRGROUND_RECORDS && FG.rec[0].offer == 7 && fairground_records_fit(&FG) &&
              fairground_unit_price_of(&FG, FAIRGROUND_WANT, &num, &den) == ZXM_E_STATE,
          "fairground: the list is bounded, the oldest dropped first");

    /* a second model over the same books */
    fairground_init(&FG2, &FG_LINK);
    CHECK(fairground_read_books(&FG2) == ZXM_OK && FG2.wallet == 29983,
          "fairground: a second model reads the same books");

    /* back to books in memory: the scratch file is removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0,
          "fairground: scratch books removed");
    FG_OFFERS = FG_NET = FG_PEER = NULL;
}
