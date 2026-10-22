/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pannier's model against zxvd's real dispatcher (LINK): files.stat,
 * files.read, files.write, freight.pack and freight.unpack are zxvd's own.
 * The routing table (net.peers) and the peer book (peer.status) are hosts
 * this in-process zxvd does not link, so they are answered on a link of
 * Pannier's own (PN_LINK) in their shapes (api/zxvd.openrpc.json), which
 * passes every other request to the real dispatcher. Two carrier peers
 * (made up), a letter and a data file of the Hoard queued until the ring
 * pushes back, parcel 9 carried as freights rebuilt from their last k
 * packets only, then held BOTH between its signed receipt (sequence 5) and
 * a hop report of loss until a second receipt settles it; the queue saved
 * and read back only on the owner's word, closed oldest first. */
#include "pannier_model.h"

static pannier_t PN, PN2;
static zxm_link_t PN_LINK;
static int pn_sent; /* requests PN_LINK answered itself */
static const char *PN_PEERS, *PN_BOOK;
static char pn_text[6001];

static int32_t pn_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"net.peers\"")) r = PN_PEERS;
    if (strstr(req, "\"peer.status\"")) r = PN_BOOK;
    if (!r) return inproc(NULL, req, len, resp, cap);
    pn_sent++;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define PN_A "a1a2a3a4a5a6a7a8a9b0b1b2b3b4b5b6b7b8b9c0c1c2c3c4c5c6c7c8c9d0d1d2"
#define PN_B "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define PN_ROW(id, ms)                                                                             \
    "{\"id\":\"" id "\",\"addr\":\"192.168.1.7:7420\",\"bucket\":3,\"ubh\":true,"                  \
    "\"last_seen_ms\":\"" ms "\",\"in_book\":false,\"member\":false,\"held\":\"0\","               \
    "\"receivable\":\"0\",\"social\":\"0\",\"pending\":0}"
#define PN_T0 INT64_C(1792627200) /* seconds since 1970 */

static int pn_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK);
}

/* the file at path holds exactly want (n bytes) */
static bool pn_same(const char *path, const char *want, uint32_t n)
{
    static char got[6001];
    uint64_t size = 0;
    zxm_begin(&LINK, "files.read");
    zxm_p_str(&LINK, "path", path);
    zxm_p_u64(&LINK, "max", 6000);
    if (zxm_call(&LINK) || zxm_get_u64(&LINK, LINK.result, "size", &size) ||
        zxm_get_str(&LINK, LINK.result, "text", got, sizeof got))
        return false;
    return size == n && strlen(got) == n && !memcmp(got, want, n);
}

APPS_TEST(pannier)
{
    int64_t n = 0, peer = 0, num = 0, den = 0;
    int before, pbefore;

    /* the rules */
    {
        int64_t hop[3] = {90, 45, 15}, bad[2] = {90, -1};
        CHECK(pannier_route(hop, 3, &num, &den) == ZXM_OK && num == 5 && den == 2 &&
                  pannier_route(bad, 2, &num, &den) == ZXM_E_ARG &&
                  pannier_route(hop, 0, &num, &den) == ZXM_E_ARG && pannier_wait(1) == 0 &&
                  pannier_wait(2) == 30 && pannier_wait(16) == 3600 &&
                  pannier_send_now(TRELLIS_ACTIVE, 0, 16) &&
                  !pannier_send_now(TRELLIS_WAITING, 0, 1) &&
                  !pannier_send_now(TRELLIS_ACTIVE, 0, 17) &&
                  pannier_queue_room(16, 0, PANNIER_QUEUE) == 0,
              "pannier: the route time, the waits, send now and the ring's backpressure");
    }

    /* PM1-PM4: refused before anything is sent, nothing kept */
    zxm_link_init(&PN_LINK, pn_send, &PN_LINK);
    pannier_init(&PN, &PN_LINK);
    before = sent;
    pbefore = pn_sent;
    CHECK(pannier_add_peer(&PN, "A1", NULL) == ZXM_E_ARG &&
              pannier_add_peer(&PN, PN_A "0", NULL) == ZXM_E_ARG &&
              pannier_put_file(&PN, 1, "Pannier.test.data", 168, PN_T0, NULL) == ZXM_E_ARG &&
              pannier_send(&PN, 1, 1000000, 0, PN_T0, true) == ZXM_E_STATE &&
              pannier_receipt(&PN, 1, 5, PN_T0) == ZXM_E_STATE && PN.npeers == 0 && PN.head == 0 &&
              sent == before && pn_sent == pbefore,
          "pannier: refused before anything is sent");

    /* PM1: two carrier peers, A listed by net.peers, B not */
    CHECK(pannier_add_peer(&PN, PN_A, &peer) == ZXM_OK && peer == 1 &&
              pannier_add_peer(&PN, PN_B, &peer) == ZXM_OK && peer == 2 &&
              pannier_add_peer(&PN, PN_B, NULL) == ZXM_E_STATE && PN.npeers == 2 &&
              PN.peer[0].state == TRELLIS_WAITING && pannier_active(&PN) == 0,
          "pannier: two peers kept, both waiting");
    PN_PEERS = "{\"peers\":[" PN_ROW(PN_A, "1792627100000") "],\"count\":1}";
    CHECK(pannier_peers(&PN) == ZXM_OK && PN.peer[0].state == TRELLIS_ACTIVE &&
              PN.peer[0].seen_ms == INT64_C(1792627100000) && PN.peer[1].state == TRELLIS_WAITING &&
              pannier_active(&PN) == 1,
          "pannier: net.peers lists A: active, B waiting: %s", PN.notice);
    PN_PEERS = "{\"peers\":[{\"id\":\"zz\",\"last_seen_ms\":\"1\"}],\"count\":1}";
    CHECK(pannier_peers(&PN) == ZXM_E_PROTOCOL && PN.peer[0].state == TRELLIS_ACTIVE,
          "pannier: a table that is not zxvd's leaves the peers as they were");
    PN_PEERS = "{\"peers\":[" PN_ROW(PN_A, "1792627100000") "],\"count\":1}";

    /* PM2: the peer book */
    PN_BOOK = "{\"serving\":false,\"port\":0,\"price_minor\":\"0\",\"currency\":\"VFV\","
              "\"wallet_minor\":\"0\",\"fees_paid_minor\":\"0\",\"sold\":0,\"bought\":0,"
              "\"refused\":0,\"sessions\":0,\"book_ok\":true,\"history\":[]}";
    CHECK(pannier_book(&PN) == ZXM_OK && PN.book_known && PN.book_ok && !PN.serving,
          "pannier: the peer book verifies");

    /* PM3, PM4: a letter and a data file of the Hoard */
    for (uint32_t i = 0; i < 6000u; i++) pn_text[i] = (char) ('a' + (int) (i * 7u % 26u));
    pn_text[6000] = 0;
    CHECK(pn_write("Pannier.test.letter", "Subject: Hi\n\nHello there") == ZXM_OK &&
              pn_write("Pannier.test.data", pn_text) == ZXM_OK,
          "pannier: the test's letter and data: %s", LINK.err);
    before = sent;
    CHECK(pannier_put_message(&PN, 1, "Pannier.test.letter", 2, 11, 0, 0, PN_T0, NULL) ==
                  ZXM_E_STATE &&
              pannier_put_message(&PN, 1, "Pannier.test.letter", 2, 11, 1, 1, PN_T0, NULL) ==
                  ZXM_E_STATE &&
              pannier_put_message(&PN, 1, "Pannier.test.letter", 201, 0, 1, 0, PN_T0, NULL) ==
                  ZXM_E_ARG &&
              PN.head == 0 && sent == before,
          "pannier: a letter without a verified card or a consent, or past its size: not queued");
    CHECK(pannier_put_message(&PN, 1, "Pannier.test.letter", 3, 11, 1, 0, PN_T0, NULL) ==
                  ZXM_E_ARG &&
              PN.head == 0,
          "pannier: a letter whose file is not its payload: not queued");
    CHECK(pannier_put_message(&PN, 1, "Pannier.test.letter", 2, 11, 1, 0, PN_T0, &n) == ZXM_OK &&
              n == 1 && pannier_find(&PN, 1)->bytes == 24 &&
              pannier_find(&PN, 1)->kind == PANNIER_MESSAGE && pannier_find(&PN, 1)->freights == 1,
          "pannier: the letter queued as parcel 1: %s", PN.notice);
    bool all = true;
    for (int64_t i = 2; i <= 16; i++)
        all = all &&
              pannier_put_file(&PN, i == 4 ? 2 : 1, "Pannier.test.data", 168, PN_T0 + i, &n) ==
                  ZXM_OK &&
              n == i;
    CHECK(all && PN.head == 16 && pannier_room(&PN) == 0 && pannier_find(&PN, 9)->freights == 2 &&
              pannier_find(&PN, 9)->packets == 336 &&
              pannier_find(&PN, 9)->status == CARAVAN_DRAFTED,
          "pannier: sixteen parcels fill the ring; parcel 9 is two freights, 336 packets");
    before = sent;
    CHECK(pannier_put_file(&PN, 1, "Pannier.test.data", 168, PN_T0, &n) == ZXM_E_STATE &&
              PN.head == 16 && PN.tail == 0 && sent == before && strstr(PN.notice, "full"),
          "pannier: head - tail >= capacity is backpressure, nothing sent: %s", PN.notice);

    /* PM6: carried only when every rule holds, and on the owner's word */
    before = sent;
    CHECK(pannier_send(&PN, 9, 1000000, 0, PN_T0, false) == ZXM_E_STATE &&
              pannier_send(&PN, 4, 1000000, 0, PN_T0, true) == ZXM_E_STATE &&
              pannier_send(&PN, 9, 6000, 1, PN_T0, true) == ZXM_E_STATE &&
              pannier_find(&PN, 9)->status == CARAVAN_DRAFTED &&
              pannier_find(&PN, 9)->attempt == 0 && sent == before,
          "pannier: no word, a waiting peer, or no room on the receiver: nothing sent");
    CHECK(pannier_next(&PN, PN_T0) == 1, "pannier: the oldest that may go is parcel 1");
    CHECK(pannier_send(&PN, 9, 1000000, 0, PN_T0, true) == ZXM_OK &&
              pannier_find(&PN, 9)->status == CARAVAN_TRANSIT &&
              pannier_find(&PN, 9)->attempt == 1 && pannier_find(&PN, 9)->next_at == PN_T0 + 30 &&
              pn_same("Pannier.out.1.9", pn_text, 6000),
          "pannier: parcel 9 rebuilt from the last 168 packets of each freight: %s %s", PN.notice,
          LINK.err);
    CHECK(pannier_send(&PN, 9, 1000000, 0, PN_T0 + 60, true) == ZXM_E_STATE,
          "pannier: on its way: not sent twice before a receipt or a report");

    /* PM7, the BOTH test: a signed receipt (sequence 5) and a hop report of loss */
    CHECK(pannier_receipt(&PN, 9, 5, PN_T0 + 100) == ZXM_OK && PN.peer[0].last_seq == 5 &&
              pannier_state(&PN, 9) == LPRES_STATE_TRUE,
          "pannier: receipt 5 for parcel 9");
    CHECK(pannier_hop_lost(&PN, 9, PN_T0 + 110) == ZXM_OK &&
              pannier_state(&PN, 9) == LPRES_STATE_BOTH && (int) pannier_state(&PN, 9) == 3,
          "pannier: a hop says it was lost: parcel 9 is BOTH");
    before = sent;
    CHECK(pannier_send(&PN, 9, 1000000, 0, PN_T0 + 4000, true) == ZXM_E_STATE &&
              pannier_close(&PN, 9, PN_T0 + 4000, true) == ZXM_E_STATE &&
              pannier_give_up(&PN, 9, PN_T0 + 4000, true) == ZXM_E_STATE &&
              pannier_find(&PN, 9)->status == CARAVAN_TRANSIT &&
              pannier_find(&PN, 9)->attempt == 1 && sent == before,
          "pannier: a BOTH parcel is neither sent again nor closed: %s", PN.notice);
    CHECK(pannier_receipt(&PN, 9, 5, PN_T0 + 120) == ZXM_E_STATE &&
              pannier_receipt(&PN, 9, 4, PN_T0 + 120) == ZXM_E_STATE &&
              pannier_receipt(&PN, 9, INT64_C(1000000000000000000), PN_T0 + 120) == ZXM_E_STATE &&
              PN.peer[0].last_seq == 5 && pannier_find(&PN, 9)->receipts == 1 &&
              pannier_state(&PN, 9) == LPRES_STATE_BOTH,
          "pannier: a reused, falling or overflowing sequence is refused, nothing changed");

    /* PM9: saved only on the owner's word, read back whole */
    before = sent;
    CHECK(pannier_save(&PN, false) == ZXM_E_STATE && sent == before && PN.dirty,
          "pannier: not saved without the owner's word, nothing sent");
    CHECK(pannier_save(&PN, true) == ZXM_OK && !PN.dirty, "pannier: saved: %s", LINK.err);
    pannier_init(&PN2, &PN_LINK);
    CHECK(pannier_load(&PN2) == ZXM_OK && PN2.npeers == 2 && PN2.head == 16 && PN2.tail == 0 &&
              PN2.next_n == 17 && !memcmp(PN2.q, PN.q, sizeof PN.q) &&
              !memcmp(PN2.peer, PN.peer, sizeof PN.peer) &&
              pannier_state(&PN2, 9) == LPRES_STATE_BOTH,
          "pannier: read back, parcel 9 still BOTH: %s", LINK.err);
    CHECK(pn_write(PANNIER_FILE, "pannier 1\nq 1 0 2\nc 1 1 2 10 1 1 1 0 0 0 0 0 0 0 x\n") ==
                  ZXM_OK &&
              pannier_load(&PN2) == ZXM_E_STATE && PN2.head == 16 && PN2.npeers == 2,
          "pannier: a file that does not parse leaves what is open as it was");
    CHECK(pannier_save(&PN, true) == ZXM_OK, "pannier: saved again: %s", LINK.err);

    /* PM7, PM8: a second receipt is the witness; closed on the owner's word */
    CHECK(pannier_receipt(&PN, 9, 6, PN_T0 + 200) == ZXM_OK &&
              pannier_state(&PN, 9) == LPRES_STATE_TRUE,
          "pannier: a second receipt settles parcel 9: delivered");
    before = sent;
    CHECK(pannier_close(&PN, 9, PN_T0 + 210, false) == ZXM_E_STATE &&
              pannier_find(&PN, 9)->status == CARAVAN_TRANSIT && sent == before,
          "pannier: not closed without the owner's word");
    CHECK(pannier_close(&PN, 9, PN_T0 + 210, true) == ZXM_OK &&
              pannier_find(&PN, 9)->status == CARAVAN_DELIVERED && PN.tail == 0 &&
              pannier_room(&PN) == 0,
          "pannier: parcel 9 closed; the oldest, parcel 1, still holds the tail");

    /* the oldest first: parcel 1 carried, its receipt, closed: the tail moves */
    CHECK(pannier_send(&PN, 1, 1000000, 0, PN_T0 + 300, true) == ZXM_OK &&
              pn_same("Pannier.out.1.1", "Subject: Hi\n\nHello there", 24) &&
              pannier_receipt(&PN, 1, 7, PN_T0 + 310) == ZXM_OK &&
              pannier_close(&PN, 1, PN_T0 + 320, true) == ZXM_OK && PN.tail == 1 &&
              pannier_room(&PN) == 1,
          "pannier: parcel 1 delivered and closed; the ring has one slot: %s", LINK.err);
    CHECK(pannier_put_file(&PN, 1, "Pannier.test.data", 100, PN_T0 + 330, &n) == ZXM_OK &&
              n == 17 && pannier_room(&PN) == 0,
          "pannier: parcel 17 takes the freed slot");

    /* PR2: lost, backed off, sent again; then given up on the owner's word */
    CHECK(pannier_send(&PN, 2, 1000000, 0, PN_T0 + 400, true) == ZXM_OK &&
              pannier_hop_lost(&PN, 2, PN_T0 + 410) == ZXM_OK &&
              pannier_state(&PN, 2) == LPRES_STATE_FALSE &&
              pannier_send(&PN, 2, 1000000, 0, PN_T0 + 420, true) == ZXM_E_STATE &&
              pannier_send(&PN, 2, 1000000, 0, PN_T0 + 430, true) == ZXM_OK &&
              pannier_find(&PN, 2)->attempt == 2 && pannier_find(&PN, 2)->next_at == PN_T0 + 490,
          "pannier: lost, sent again after 30 s, then 60 s to wait: %s", PN.notice);
    const pannier_parcel_t *c2 = pannier_find(&PN, 2); /* its slot, until the ring reuses it */
    CHECK(c2 && pannier_hop_lost(&PN, 2, PN_T0 + 440) == ZXM_OK &&
              pannier_give_up(&PN, 2, PN_T0 + 450, false) == ZXM_E_STATE &&
              c2->status == CARAVAN_TRANSIT &&
              pannier_give_up(&PN, 2, PN_T0 + 450, true) == ZXM_OK &&
              c2->status == CARAVAN_RETURNED && PN.tail == 2 && pannier_room(&PN) == 1 &&
              pannier_find(&PN, 2) == NULL,
          "pannier: given up on the owner's word, the tail passes it");

    /* PM7: the recipient's word, on the owner's */
    CHECK(pannier_send(&PN, 3, 1000000, 0, PN_T0 + 500, true) == ZXM_OK &&
              pannier_receipt(&PN, 3, 8, PN_T0 + 510) == ZXM_OK &&
              pannier_hop_lost(&PN, 3, PN_T0 + 520) == ZXM_OK &&
              pannier_state(&PN, 3) == LPRES_STATE_BOTH &&
              pannier_witness(&PN, 3, PANNIER_SAID_LOST, PN_T0 + 530, false) == ZXM_E_STATE &&
              pannier_state(&PN, 3) == LPRES_STATE_BOTH &&
              pannier_witness(&PN, 3, PANNIER_SAID_LOST, PN_T0 + 530, true) == ZXM_OK &&
              pannier_state(&PN, 3) == LPRES_STATE_FALSE,
          "pannier: the recipient says it never came: lost, sent again later");

    /* PM1: a peer revoked only on the owner's word, and it stays revoked */
    before = sent;
    CHECK(pannier_revoke(&PN, 2, false) == ZXM_E_STATE && PN.peer[1].state == TRELLIS_WAITING &&
              sent == before && pannier_revoke(&PN, 2, true) == ZXM_OK &&
              PN.peer[1].state == TRELLIS_REVOKED,
          "pannier: B revoked on the owner's word");
    PN_PEERS = "{\"peers\":[" PN_ROW(PN_A, "1792627900000") "," PN_ROW(
        PN_B, "1792627900000") "],\"count\":2}";
    CHECK(pannier_peers(&PN) == ZXM_OK && PN.peer[1].state == TRELLIS_REVOKED &&
              pannier_active(&PN) == 1 &&
              pannier_send(&PN, 4, 1000000, 0, PN_T0 + 600, true) == ZXM_E_STATE,
          "pannier: a revoked peer stays revoked; its parcels wait");

    /* PM2: a book that does not verify stops every carry */
    PN_BOOK = "{\"serving\":false,\"port\":0,\"price_minor\":\"0\",\"currency\":\"VFV\","
              "\"wallet_minor\":\"0\",\"fees_paid_minor\":\"0\",\"sold\":0,\"bought\":0,"
              "\"refused\":0,\"sessions\":0,\"book_ok\":false,\"history\":[]}";
    before = sent;
    CHECK(pannier_book(&PN) == ZXM_OK && !PN.book_ok &&
              pannier_send(&PN, 5, 1000000, 0, PN_T0 + 700, true) == ZXM_E_STATE &&
              pannier_find(&PN, 5)->attempt == 0 && sent == before,
          "pannier: the peer book does not verify: nothing carried: %s", PN.notice);
}
