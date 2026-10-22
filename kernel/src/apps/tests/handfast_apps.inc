/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Handfast's model against zxvd's real dispatcher: an agreement opened and
 * kept in the test's scratch Hoard (files.read, files.mkdir, files.write)
 * under a journal on disk (journal.query), its parties, clauses and
 * revisions, the party matrix with clause 4 agreed by one party and
 * disputed by the other (BOTH, held in escrow, while the agreed clauses go
 * ahead), the obligations and the payment schedule, read back by a second
 * model, and every revision proven against the journal (journal.verify).
 * pspace.* and web4.verify are served in the daemon by zxvd_pspace.c and
 * zxvd_web4.c, which this test does not link (the dispatcher answers
 * -32601), so they are answered on a link of their own (HF_LINK) in
 * zxvd's documented shapes (api/zxvd.openrpc.json); every other request
 * goes to the real dispatcher. */
#include "handfast_model.h"

#define HF_H16(x) x x x x x x x x x x x x x x x x
#define HF_KA     HF_H16("a1b2")
#define HF_KB     HF_H16("c3d4")
#define HF_ROOM   "bafkreihandfastroom"
#define HF_ENV_A  "5745455601" HF_KA /* an envelope the fake says party A signed */
#define HF_ENV_B  "5745455602" HF_KB

static handfast_t HF, HF2;
static zxm_link_t HF_LINK;
static const char *HF_PHASE = "in"; /* the room's phase pspace.list gives */
static int HF_POSTS, HF_VERIFIES, HF_LISTS;

static int32_t hf_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"pspace.list\"")) {
        HF_LISTS++;
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"online\":true,\"spaces\":["
                        "{\"cid\":\"bafkreiotherroom\",\"phase\":\"in\",\"members\":3},"
                        "{\"cid\":\"" HF_ROOM "\",\"phase\":\"%s\",\"members\":2}]}}",
                        l->seq, HF_PHASE);
    }
    if (strstr(req, "\"pspace.post\"")) {
        HF_POSTS++;
        return snprintf(
            resp, cap,
            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"posted\":true,\"cid\":\"" HF_ROOM "\"}}",
            l->seq);
    }
    if (strstr(req, "\"web4.verify\"")) {
        HF_VERIFIES++;
        const char *from = strstr(req, HF_ENV_A) ? HF_KA : strstr(req, HF_ENV_B) ? HF_KB : 0;
        if (!from)
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":false,"
                            "\"reason\":\"signature\"}}",
                            l->seq);
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":true,\"kind\":"
                        "\"REQUEST\",\"from\":\"%s\",\"to\":\"%s\",\"seq\":\"1\",\"ts\":1790000000,"
                        "\"req_id\":\"1\",\"chunk\":0,\"tool\":\"handfast\",\"final\":false,"
                        "\"payload_bytes\":120,\"consent\":false}}",
                        l->seq, from, from);
    }
    return inproc(NULL, req, len, resp, cap);
}

APPS_TEST(handfast)
{
    const int64_t DAY = 20000; /* 2024-10-04 */
    const int64_t DEAL = 164;
    static const char *C1 = "1. The parties share the barn and its upkeep.";
    static const char *C1B = "1. The parties share the barn, its roof and its upkeep.";
    static const char *C2 = "2. Each pays half of the roof's repair by the first of the month.";
    static const char *C4 = "4. Party B keeps the north door key.\n\tOnly B lends it.";
    static char text[HANDFAST_TEXT_MAX + 2];
    char jdir[256];
    uint32_t ia = 0, ib = 0;
    int64_t v = 0, esc[4];
    handfast_t *h = &HF;

    /* the rules */
    CHECK(handfast_limits_ok(2, 1) && !handfast_limits_ok(1, 1) &&
              !handfast_limits_ok(2, HANDFAST_TEXT_MAX + 1) && handfast_share(1000, 3, 1) == 334 &&
              handfast_share(10, 4, 3) == 2 && handfast_ready_rule(2, 1, 0) &&
              !handfast_ready_rule(2, 1, 1) && trk_stream_due(1200000, 1, 12, 0) == 100000,
          "handfast: the rules");

    snprintf(jdir, sizeof jdir, "%s/zxv-handfastj-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "handfast: a journal");
    zxm_link_init(&HF_LINK, hf_send, &HF_LINK);

    /* nothing before an agreement is open */
    handfast_init(h, &HF_LINK);
    int before = sent;
    CHECK(handfast_add_party(h, "Ada", 7, HF_KA, true, &ia) == ZXM_E_STATE &&
              handfast_put_clause(h, 1, 0, C1, (uint32_t) strlen(C1), true) == ZXM_E_STATE &&
              handfast_mark(h, 0, 1, 1, true) == ZXM_E_STATE &&
              handfast_add_obligation(h, 1, DAY, 1, 0, 1, true) == ZXM_E_STATE &&
              handfast_set_schedule(h, 100, 2, true) == ZXM_E_STATE &&
              handfast_post(h, "hello", true) == ZXM_E_STATE &&
              handfast_check_signed(h, 0, HF_ENV_A) == ZXM_E_STATE && !handfast_ready(h) &&
              sent == before && HF_POSTS == 0 && HF_VERIFIES == 0 &&
              strstr(h->notice, "never signs, sends or moves money"),
          "handfast: nothing asked before an agreement is open: %s", h->notice);
    CHECK(handfast_load(h, DEAL) == ZXM_E_STATE && !h->loaded && handfast_load(h, 0) == ZXM_E_ARG,
          "handfast: no agreement yet");

    /* HM1: opening, refused before anything is sent */
    before = sent;
    CHECK(handfast_open(h, 0, "Barn", true) == ZXM_E_ARG &&
              handfast_open(h, HANDFAST_DEAL_MAX + 1, "Barn", true) == ZXM_E_ARG &&
              handfast_open(h, DEAL, "", true) == ZXM_E_ARG &&
              handfast_open(h, DEAL, "Barn\tshare", true) == ZXM_E_ARG && sent == before,
          "handfast: a bad opening refused before anything is sent");
    CHECK(handfast_open(h, DEAL, "Barn share", false) == ZXM_E_STATE && sent == before &&
              !h->loaded,
          "handfast: without the owner's word nothing is sent and nothing kept");
    CHECK(handfast_open(h, DEAL, "Barn share", true) == ZXM_OK && h->loaded && h->deal == DEAL,
          "handfast: agreement 164 opened: %s", HF_LINK.err);
    handfast_init(&HF2, &HF_LINK);
    CHECK(handfast_open(&HF2, DEAL, "Again", true) == ZXM_E_STATE && !HF2.loaded,
          "handfast: an agreement number is never reused");

    /* HM2: parties */
    before = sent;
    CHECK(handfast_add_party(h, "", 7, HF_KA, true, &ia) == ZXM_E_ARG &&
              handfast_add_party(h, "Ada", 0, HF_KA, true, &ia) == ZXM_E_ARG &&
              handfast_add_party(h, "Ada", KINDRED_CONTACT_MAX + 1, HF_KA, true, &ia) ==
                  ZXM_E_ARG &&
              handfast_add_party(h, "Ada", 7, "abc", true, &ia) == ZXM_E_ARG &&
              handfast_add_party(h, "Ada", 7, HF_KA, false, &ia) == ZXM_E_STATE && sent == before &&
              h->nparty == 0,
          "handfast: a party outside its limits, or without the owner's word, is refused");
    CHECK(handfast_add_party(h, "Ada", 7, HF_KA, true, &ia) == ZXM_OK && ia == 0 &&
              handfast_add_party(h, "Bo", 8, "", true, &ib) == ZXM_OK && ib == 1 &&
              handfast_add_party(h, "Bo again", 8, HF_KB, true, &ib) == ZXM_E_ARG && h->nparty == 2,
          "handfast: two parties, a contact once: %s", HF_LINK.err);

    /* HM3: clauses, each revision on the journal */
    before = sent;
    CHECK(handfast_put_clause(h, 1, 0, C1, 0, true) == ZXM_E_ARG &&
              handfast_put_clause(h, 1, 0, text, HANDFAST_TEXT_MAX + 1, true) == ZXM_E_ARG &&
              handfast_put_clause(h, 1, 1, C1, (uint32_t) strlen(C1), true) == ZXM_E_ARG &&
              handfast_put_clause(h, 1, 9, C1, (uint32_t) strlen(C1), true) == ZXM_E_ARG &&
              handfast_put_clause(h, 1000, 0, C1, (uint32_t) strlen(C1), true) == ZXM_E_ARG &&
              handfast_put_clause(h, 1, 0, "a\001b", 3, true) == ZXM_E_ARG &&
              handfast_put_clause(h, 1, 0, C1, (uint32_t) strlen(C1), false) == ZXM_E_STATE &&
              sent == before && h->nrev == 0,
          "handfast: a clause outside its limits, or without the owner's word, is refused");
    CHECK(handfast_put_clause(h, 1, 0, C1, (uint32_t) strlen(C1), true) == ZXM_OK &&
              handfast_put_clause(h, 2, 0, C2, (uint32_t) strlen(C2), true) == ZXM_OK &&
              handfast_put_clause(h, 4, 1, C4, (uint32_t) strlen(C4), true) == ZXM_OK &&
              h->nclause == 3 && h->nrev == 3 && h->rev[0].seq > 0 &&
              h->rev[1].seq > h->rev[0].seq && h->rev[2].parent == 1 && h->rev[0].prev == 0,
          "handfast: three clauses filed, each stamped with the journal: %s", HF_LINK.err);
    CHECK(handfast_add_party(h, "Cy", 9, "", true, &ib) == ZXM_E_STATE && h->nparty == 2,
          "handfast: parties come before the clauses");
    CHECK(handfast_put_clause(h, 1, 4, C1B, (uint32_t) strlen(C1B), true) == ZXM_E_ARG,
          "handfast: a clause never sits under one of its own");
    CHECK(handfast_mark(h, ia, 1, HANDFAST_AGREED, true) == ZXM_OK && h->mark[0][0] == 1 &&
              handfast_put_clause(h, 1, 0, C1B, (uint32_t) strlen(C1B), true) == ZXM_OK &&
              h->nrev == 4 && h->rev[3].rev == 2 && h->rev[3].prev == h->rev[0].seq &&
              h->rev[3].seq > h->rev[2].seq && h->mark[0][0] == HANDFAST_SILENT,
          "handfast: clause 1 revised, linked to its first revision, its marks cleared");
    CHECK(handfast_read_clause(h, 1, text, sizeof text) == (int) strlen(C1B) &&
              !memcmp(text, C1B, strlen(C1B)) &&
              handfast_read_clause(h, 4, text, sizeof text) == (int) strlen(C4) &&
              !memcmp(text, C4, strlen(C4)) &&
              handfast_read_clause(h, 3, text, sizeof text) == ZXM_E_ARG,
          "handfast: each clause's text read back");

    /* HM4: the party matrix and the BOTH test */
    before = sent;
    CHECK(handfast_mark(h, 2, 1, 1, true) == ZXM_E_ARG &&
              handfast_mark(h, 0, 3, 1, true) == ZXM_E_ARG &&
              handfast_mark(h, 0, 1, 3, true) == ZXM_E_ARG &&
              handfast_mark(h, 0, 1, -1, true) == ZXM_E_ARG &&
              handfast_mark(h, 0, 1, 1, false) == ZXM_E_STATE && sent == before &&
              h->mark[0][0] == HANDFAST_SILENT,
          "handfast: a mark refused, nothing sent, nothing changed");
    CHECK(handfast_mark(h, ia, 1, HANDFAST_AGREED, true) == ZXM_OK &&
              handfast_mark(h, ia, 2, HANDFAST_AGREED, true) == ZXM_OK &&
              handfast_mark(h, ia, 4, HANDFAST_AGREED, true) == ZXM_OK &&
              handfast_mark(h, 1, 1, HANDFAST_AGREED, true) == ZXM_OK &&
              handfast_clause_state(h, 2) == LPRES_STATE_NEITHER &&
              handfast_mark(h, 1, 2, HANDFAST_AGREED, true) == ZXM_OK &&
              handfast_mark(h, 1, 4, HANDFAST_DISPUTED, true) == ZXM_OK,
          "handfast: A agrees 1, 2 and 4; B agrees 1 and 2 and disputes 4: %s", HF_LINK.err);
    CHECK(handfast_clause_state(h, 4) == LPRES_STATE_BOTH && handfast_escrow(h, esc, 4) == 1 &&
              esc[0] == 4 && !handfast_goes_ahead(h, 4) && handfast_goes_ahead(h, 1) &&
              handfast_goes_ahead(h, 2) && handfast_unagreed(h) == 1 && !handfast_ready(h),
          "handfast: clause 4 is BOTH and held in escrow, 1 and 2 go ahead, not ready to sign");

    /* HM6: obligations and the schedule */
    before = sent;
    CHECK(handfast_add_obligation(h, 3, DAY + 30, 1000, 0, 3, true) == ZXM_E_ARG &&
              handfast_add_obligation(h, 2, DAY + 30, -1, 0, 3, true) == ZXM_E_ARG &&
              handfast_add_obligation(h, 2, DAY + 30, 1000, 100, 3, true) == ZXM_E_ARG &&
              handfast_add_obligation(h, 2, DAY + 30, SEALWAX_UNITS_MAX + 1, 0, 3, true) ==
                  ZXM_E_ARG &&
              handfast_add_obligation(h, 2, DAY + 30, 1000, 0, 0, true) == ZXM_E_ARG &&
              handfast_add_obligation(h, 2, DAY + 30, 1000, 0, 4, true) == ZXM_E_ARG &&
              handfast_add_obligation(h, 2, -1, 1000, 0, 3, true) == ZXM_E_ARG &&
              handfast_add_obligation(h, 2, DAY + 30, 1000, 0, 3, false) == ZXM_E_STATE &&
              sent == before && h->nobl == 0,
          "handfast: an obligation outside its fields, or without the owner's word, is refused");
    CHECK(handfast_add_obligation(h, 2, DAY + 30, 1000, 0, 3, true) == ZXM_OK &&
              handfast_add_obligation(h, 1, DAY + 30, 0, 1, 3, true) == ZXM_OK &&
              handfast_add_obligation(h, 4, DAY + 60, 50, 25, 2, true) == ZXM_OK && h->nobl == 3,
          "handfast: three obligations: %s", HF_LINK.err);
    CHECK(handfast_owed(h, ia, DAY + 30, &v) == ZXM_OK && v == 50001 &&
              handfast_owed(h, 1, DAY + 30, &v) == ZXM_OK && v == 50000 &&
              handfast_owed(h, ia, DAY + 60, &v) == ZXM_OK && v == 0 &&
              handfast_owed(h, 1, DAY + 60, &v) == ZXM_OK && v == 5025 &&
              handfast_owed(h, 2, DAY, &v) == ZXM_E_ARG,
          "handfast: each party's obligations per due date, the odd unit to the first");
    CHECK(handfast_instalment(h, 1, &v) == ZXM_E_STATE &&
              handfast_set_schedule(h, TRK_TOTAL_MAX + 1, 12, true) == ZXM_E_ARG &&
              handfast_set_schedule(h, 1200000, 0, true) == ZXM_E_ARG &&
              handfast_set_schedule(h, 1200000, 12, false) == ZXM_E_STATE && h->instalments == 0,
          "handfast: a schedule outside Trickle's domain, or unconfirmed, is refused");
    CHECK(handfast_set_schedule(h, 1200001, 12, true) == ZXM_OK &&
              handfast_instalment(h, 1, &v) == ZXM_OK && v == 100000 &&
              handfast_instalment(h, 12, &v) == ZXM_OK && v == 1200001 &&
              handfast_instalment(h, 0, &v) == ZXM_E_ARG &&
              handfast_instalment(h, 13, &v) == ZXM_E_ARG,
          "handfast: instalment n of 12 owes floor(total * n / 12) - paid");
    CHECK(handfast_record_paid(h, 1200002, true) == ZXM_E_ARG &&
              handfast_record_paid(h, 250000, false) == ZXM_E_STATE && h->paid == 0 &&
              handfast_record_paid(h, 250000, true) == ZXM_OK &&
              handfast_instalment(h, 2, &v) == ZXM_OK && v == 0 &&
              handfast_instalment(h, 3, &v) == ZXM_OK && v == 50000,
          "handfast: what the parties report paid: nothing due until it is passed");

    /* HM1: a second model reads the same file */
    handfast_init(&HF2, &HF_LINK);
    CHECK(handfast_load(&HF2, DEAL) == ZXM_OK && HF2.nparty == 2 && HF2.nrev == 4 &&
              HF2.nclause == 3 && HF2.nobl == 3 && HF2.paid == 250000 && HF2.instalments == 12 &&
              HF2.total == 1200001 && !strcmp(HF2.title, "Barn share") &&
              !strcmp(HF2.party[0].key, HF_KA) && !HF2.party[1].key[0] &&
              HF2.rev[3].seq == h->rev[3].seq &&
              handfast_clause_state(&HF2, 4) == LPRES_STATE_BOTH && handfast_unagreed(&HF2) == 1,
          "handfast: the agreement read back: %s", HF2.notice);

    /* HM5: ready to sign once B agrees 4 and B's key is known */
    CHECK(handfast_mark(h, 1, 4, HANDFAST_AGREED, true) == ZXM_OK &&
              handfast_clause_state(h, 4) == LPRES_STATE_TRUE && handfast_escrow(h, esc, 4) == 0 &&
              handfast_unagreed(h) == 0 && !handfast_ready(h),
          "handfast: every clause agreed, but B's signature line has no key yet");
    memcpy(h->party[1].key, HF_KB, HANDFAST_KEY_HEX + 1u);
    CHECK(handfast_ready(h), "handfast: ready to sign");

    /* HM7: the negotiation room */
    before = sent;
    CHECK(handfast_post(h, "Shall we sign Friday?", true) == ZXM_E_STATE &&
              handfast_set_room(h, "bad cid!", true) == ZXM_E_ARG &&
              handfast_set_room(h, HF_ROOM, false) == ZXM_E_STATE && !h->room[0] &&
              handfast_set_room(h, HF_ROOM, true) == ZXM_OK,
          "handfast: a room, on the owner's word");
    int lists = HF_LISTS;
    CHECK(handfast_post(h, "", true) == ZXM_E_ARG &&
              handfast_post(h, "Shall we sign Friday?", false) == ZXM_E_STATE && HF_POSTS == 0 &&
              HF_LISTS == lists,
          "handfast: an empty note, or one unconfirmed, sends nothing");
    HF_PHASE = "joining";
    CHECK(handfast_post(h, "Shall we sign Friday?", true) == ZXM_E_STATE && HF_POSTS == 0,
          "handfast: not in the room yet: nothing posted (%s)", h->notice);
    HF_PHASE = "in";
    CHECK(handfast_post(h, "Shall we sign Friday?", true) == ZXM_OK && HF_POSTS == 1,
          "handfast: a note posted to the room: %s", HF_LINK.err);

    /* HM8: signatures, each party's own act, checked against its key */
    CHECK(handfast_check_signed(h, 0, "zz") == ZXM_E_ARG &&
              handfast_check_signed(h, 0, "abc") == ZXM_E_ARG &&
              handfast_check_signed(h, 9, HF_ENV_A) == ZXM_E_ARG && HF_VERIFIES == 0,
          "handfast: a copy that is not whole hex bytes is never sent to web4.verify");
    CHECK(handfast_check_signed(h, 0, HF_ENV_A) == ZXM_OK && h->party[0].signed_ok &&
              handfast_check_signed(h, 1, HF_ENV_A) == ZXM_OK && !h->party[1].signed_ok &&
              strstr(h->notice, "not from this party's key") &&
              handfast_check_signed(h, 1, "00ff") == ZXM_OK && !h->party[1].signed_ok &&
              strstr(h->notice, "does not hold") &&
              handfast_check_signed(h, 1, HF_ENV_B) == ZXM_OK && h->party[1].signed_ok &&
              HF_VERIFIES == 4,
          "handfast: each signed copy counts only from its own party's key: %s", h->notice);

    /* HM8: proof against the journal */
    CHECK(handfast_prove(h, 1) == LPRES_STATE_TRUE && handfast_prove(h, 4) == LPRES_STATE_TRUE &&
              handfast_prove(h, 3) == LPRES_STATE_NEITHER,
          "handfast: every revision proven against the journal: %s", h->notice);
    int64_t keep = h->rev[3].prev;
    h->rev[3].prev = keep + 1; /* a broken link in memory only */
    CHECK(handfast_prove(h, 1) == LPRES_STATE_BOTH && strstr(h->notice, "Held"),
          "handfast: a broken revision chain and an intact journal: BOTH, held");
    h->rev[3].prev = keep;

    /* with no journal, nothing to prove against: its revisions alone hold */
    zjr_close();
    CHECK(handfast_prove(h, 1) == LPRES_STATE_TRUE && strstr(h->notice, "no journal"),
          "handfast: no journal: the revisions hold, nothing proven against it: %s", h->notice);
}
