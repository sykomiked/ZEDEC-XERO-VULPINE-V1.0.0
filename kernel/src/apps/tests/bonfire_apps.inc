/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Bonfire's model. The venues go through zxvd's real dispatcher (LINK):
 * place.* is zxv_place.c, linked here, so the owner's saved places and the
 * distance to them are made the way zxvd does it. pspace.* (zxvd_pspace.c)
 * and social.* (zxvd_people.c) are served in the daemon by groups this
 * test does not link (the dispatcher answers -32601), so their answers are
 * made up in their documented shapes (FAKE). */
#include "bonfire_model.h"

#define BF_C58(x)                                                                                  \
    x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x  \
        x x x x x x x x x x x
#define BF_ROOM   "b" BF_C58("f")
#define BF_OTHER  "b" BF_C58("g")
#define BF_H16(x) x x x x x x x x x x x x x x x x
#define BF_GROUP  BF_H16("a") BF_H16("b") BF_H16("c") BF_H16("d")
#define BF_OK     "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

static bonfire_t BF;

static int bf_raw(const char *method, const char *params)
{
    zxm_begin(&LINK, method);
    zxm_p_raw(&LINK, params, (uint32_t) strlen(params));
    return zxm_call(&LINK);
}

APPS_TEST(bonfire)
{
    char text[BONFIRE_ANNOUNCE + 1], name[8];
    uint32_t g = 99, h = 99, at = 99;
    bonfire_init(&BF, &LINK);

    /* BR1-BR3: the rules the model runs */
    CHECK(bonfire_guest_may_join(0, 5, 3, 2) && !bonfire_guest_may_join(0, 5, 4, 2) &&
              !bonfire_guest_may_join(-1, 5, 0, 1) && !bonfire_guest_may_join(0, 5, 0, 0) &&
              bonfire_seats_left(5, 5) == 0 && bonfire_seats_left(5, 6) == BONFIRE_REFUSED &&
              bonfire_seats_left(5, 9) == BONFIRE_REFUSED,
          "bonfire: a guest on the list joins while the party fits");

    /* BM1: the venues through the real dispatcher */
    CHECK(zpl_open(NULL) == 0, "bonfire: places in memory");
    CHECK(bonfire_places(&BF) == ZXM_OK && BF.nplace == 0, "bonfire: no places yet: %s", LINK.err);
    CHECK(bf_raw("place.add", "{\"name\":\"Riverside Green\",\"lat\":51507400,\"lon\":-127800}") ==
                  ZXM_OK &&
              bf_raw("place.add", "{\"name\":\"Hall\",\"lat\":51517400,\"lon\":-127800,"
                                  "\"note\":\"the side door\"}") == ZXM_OK,
          "bonfire: two places saved: %s", LINK.err);
    CHECK(bonfire_places(&BF) == ZXM_OK && BF.nplace == 2 && BF.place[0].distance_m == -1 &&
              BF.place[0].lat == 51507400 && BF.place[1].lon == -127800,
          "bonfire: two venues, no distance while here is unknown: %s", LINK.err);
    CHECK(bf_raw("place.set_here", "{\"lat\":51507400,\"lon\":-127800,\"accuracy_m\":3}") ==
                  ZXM_OK &&
              bonfire_places(&BF) == ZXM_OK && BF.place[0].distance_m == 0 &&
              BF.place[1].distance_m > 1000 && BF.place[1].distance_m < 1200,
          "bonfire: the hall about 1.1 km away: %lld", (long long) BF.place[1].distance_m);

    /* BM2: a gathering, its arguments checked */
    CHECK(bonfire_plan(&BF, "", 1767225600, 5, 0, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "Fire", -1, 5, 0, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "Fire", BONFIRE_START_MAX + 1, 5, 0, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "Fire", 1767225600, 0, 0, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "Fire", 1767225600, BONFIRE_SEATS_MAX + 1, 0, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "Fire", 1767225600, 5, 2, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "\xc3", 1767225600, 5, 0, &g) == ZXM_E_ARG &&
              bonfire_plan(&BF, "Fire", 1767225600, 5, 0, NULL) == ZXM_E_ARG && BF.ngathering == 0,
          "bonfire: no name, a start or seats out of range, no such venue: nothing kept");
    CHECK(bonfire_plan(&BF, "Midwinter fire", 1767225600, 5, 0, &g) == ZXM_OK && g == 0 &&
              BF.ngathering == 1 && BF.gathering[0].present == -1 && bonfire_left(&BF, 0) == 5 &&
              bonfire_full_x100(&BF, 0) == 0,
          "bonfire: a fire for five at Riverside Green");
    for (uint32_t i = 1; i < BONFIRE_GATHERINGS; i++)
        CHECK(bonfire_plan(&BF, "Another", 0, 1, -1, &h) == ZXM_OK && h == i,
              "bonfire: gathering %u", i);
    CHECK(bonfire_plan(&BF, "One too many", 0, 1, -1, &h) == ZXM_E_STATE &&
              BF.ngathering == BONFIRE_GATHERINGS,
          "bonfire: a ninth gathering refused");

    /* BM3: the invite list */
    CHECK(bonfire_invite(&BF, 0, "Ana", &at) == ZXM_OK && at == 0 &&
              bonfire_invite(&BF, 0, "Bo", &at) == ZXM_OK && at == 1 &&
              bonfire_invite(&BF, 0, "Cy", &at) == ZXM_OK && at == 2 &&
              bonfire_guest_of(&BF, 0, "Bo") == 1 && bonfire_guest_of(&BF, 0, "Dee") == -1,
          "bonfire: three guests on the list");
    CHECK(bonfire_invite(&BF, 0, "Ana", &at) == ZXM_E_STATE &&
              bonfire_invite(&BF, 0, "", &at) == ZXM_E_ARG &&
              bonfire_invite(&BF, 0, "a name longer than thirty-two bytes", &at) == ZXM_E_ARG &&
              bonfire_invite(&BF, 9, "Dee", &at) == ZXM_E_ARG && BF.gathering[0].nguest == 3,
          "bonfire: a guest twice, no name, too long a name, no such gathering: refused");
    for (uint32_t i = 0; i < BONFIRE_GUESTS; i++) {
        snprintf(name, sizeof name, "g%u", i);
        CHECK(bonfire_invite(&BF, 1, name, &at) == ZXM_OK && at == i, "bonfire: guest %u", i);
    }
    CHECK(bonfire_invite(&BF, 1, "late", &at) == ZXM_E_STATE &&
              BF.gathering[1].nguest == BONFIRE_GUESTS,
          "bonfire: the 101st guest refused");

    /* BM4: answers, a yes only while the party fits */
    CHECK(bonfire_answer(&BF, 0, 0, BONFIRE_YES, 3, 1700000000000) == ZXM_OK &&
              BF.gathering[0].going == 3 && bonfire_left(&BF, 0) == 2 &&
              bonfire_full_x100(&BF, 0) == 6000,
          "bonfire: Ana and two more coming, two seats left");
    CHECK(!bonfire_may_join(&BF, 0, 1, 3) && bonfire_may_join(&BF, 0, 1, 2) &&
              !bonfire_may_join(&BF, 0, -1, 1) && !bonfire_may_join(&BF, 0, 7, 1),
          "bonfire: Bo with two more does not fit, with one does; nobody off the list joins");
    bonfire_guest_t keep = BF.gathering[0].guest[1];
    CHECK(bonfire_answer(&BF, 0, 1, BONFIRE_YES, 3, 1700000000001) == ZXM_E_STATE &&
              BF.gathering[0].going == 3 && BF.gathering[0].guest[1].answer == keep.answer &&
              BF.gathering[0].guest[1].party == keep.party &&
              BF.gathering[0].guest[1].stamp == keep.stamp,
          "bonfire: a party with no room refused, nothing changed");
    CHECK(bonfire_answer(&BF, 0, 1, BONFIRE_YES, 2, 1700000000002) == ZXM_OK &&
              bonfire_left(&BF, 0) == 0 && bonfire_full_x100(&BF, 0) == 10000 &&
              bonfire_answer(&BF, 0, 2, BONFIRE_YES, 1, 1700000000003) == ZXM_E_STATE,
          "bonfire: Bo and one more fill it; Cy finds no seat");
    CHECK(bonfire_answer(&BF, 0, 0, BONFIRE_YES, 1, 1700000000004) == ZXM_OK &&
              BF.gathering[0].going == 3 && bonfire_left(&BF, 0) == 2,
          "bonfire: Ana's own earlier yes not counted against her: now alone, two seats free");
    CHECK(bonfire_answer(&BF, 0, 2, BONFIRE_MAYBE, 1, 1700000000005) == ZXM_OK &&
              bonfire_answered(&BF, 0, BONFIRE_YES) == 2 &&
              bonfire_answered(&BF, 0, BONFIRE_MAYBE) == 1 &&
              bonfire_answered(&BF, 0, BONFIRE_NO) == 0 &&
              bonfire_answered(&BF, 0, BONFIRE_ANSWERS) == BONFIRE_REFUSED,
          "bonfire: two yes, one maybe (witness.kind_count)");
    CHECK(bonfire_answer(&BF, 0, 3, BONFIRE_YES, 1, 1) == ZXM_E_ARG &&
              bonfire_answer(&BF, 0, 0, BONFIRE_ANSWERS, 1, 1) == ZXM_E_ARG &&
              bonfire_answer(&BF, 0, 0, BONFIRE_YES, 0, 1) == ZXM_E_ARG &&
              bonfire_answer(&BF, 0, 0, BONFIRE_YES, BONFIRE_PARTY_MAX + 1, 1) == ZXM_E_ARG &&
              bonfire_answer(&BF, 0, 0, BONFIRE_YES, 1, BONFIRE_STAMP_MAX + 1) == ZXM_E_ARG &&
              bonfire_answer(&BF, 0, 0, BONFIRE_YES, 1, -1) == ZXM_E_ARG &&
              BF.gathering[0].going == 3,
          "bonfire: no such guest, answer, party or stamp: refused (BR5)");
    CHECK(bonfire_answer(&BF, 0, 1, BONFIRE_NO, 2, 1700000000006) == ZXM_OK &&
              bonfire_left(&BF, 0) == 4 && bonfire_answered(&BF, 0, BONFIRE_NO) == 1,
          "bonfire: Bo says no after all: four seats left");
    CHECK(bonfire_notice(&BF, 0) == ZXM_OK &&
              !strcmp(BF.notice, "Midwinter fire: 4 of 5 seats left."),
          "bonfire: the notice: %s", BF.notice);

    /* BM6: opening a space, only on the owner's word */
    last_req[0] = 0;
    CHECK(bonfire_open_space(&BF, 0, "fire", false) == ZXM_E_STATE && !last_req[0] &&
              !BF.gathering[0].cid[0] &&
              bonfire_open_space(&BF, 0, "Bad Topic", true) == ZXM_E_ARG &&
              bonfire_open_space(&BF, 9, "fire", true) == ZXM_E_ARG && !last_req[0],
          "bonfire: no word from the owner, a bad topic or no such gathering: nothing sent");
    CHECK(bonfire_open_space(&BF, 0, "fire", true) == ZXM_E_REMOTE && LINK.err_code == -32601 &&
              !BF.gathering[0].cid[0],
          "bonfire: pspace.create not linked here: -32601 (%s)", LINK.err);

    /* ---- pspace.* and social.*, answers made up in their shapes (FAKE) ---- */
    BF.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32012,\"message\":\"feature "
             "pspace is off\"}}";
    CHECK(bonfire_open_space(&BF, 0, "fire", true) == ZXM_E_REMOTE && BF.off &&
              !BF.gathering[0].cid[0],
          "bonfire: the switch off (BM6)");
    CANNED = BF_OK "{\"cid\":\"bshort\"}}";
    CHECK(bonfire_open_space(&BF, 0, "fire", true) == ZXM_E_PROTOCOL && !BF.gathering[0].cid[0],
          "bonfire: a CID that is not 59 bytes refused (SR2)");
    CANNED = BF_OK "{\"cid\":\"" BF_ROOM "\"}}";
    CHECK(bonfire_open_space(&BF, 0, "fire", true) == ZXM_OK && !BF.off &&
              !strcmp(BF.gathering[0].cid, BF_ROOM) && strstr(last_req, "pspace.create") &&
              strstr(last_req, "\"kind\":\"room\"") && strstr(last_req, "\"gov\":\"owner\"") &&
              strstr(last_req, "\"name\":\"Midwinter fire\"") &&
              strstr(last_req, "\"topic\":\"fire\""),
          "bonfire: the fire's room opened (%s)", FAKE.err);
    last_req[0] = 0;
    CHECK(bonfire_open_space(&BF, 0, "fire", true) == ZXM_E_STATE && !last_req[0],
          "bonfire: opened once only");

    /* BM6: the invite-only group */
    last_req[0] = 0;
    CHECK(bonfire_open_group(&BF, 0, false) == ZXM_E_STATE && !last_req[0] &&
              !BF.gathering[0].group[0],
          "bonfire: no group without the owner's word, nothing sent");
    CANNED = BF_OK "{\"group\":\"" BF_GROUP "\",\"open\":true,\"my_role\":\"owner\"}}";
    CHECK(bonfire_open_group(&BF, 0, true) == ZXM_E_PROTOCOL && !BF.gathering[0].group[0],
          "bonfire: an open group is not the invite-only one asked for");
    CANNED = BF_OK "{\"group\":\"" BF_GROUP "\",\"open\":false,\"my_role\":\"owner\"}}";
    CHECK(bonfire_open_group(&BF, 0, true) == ZXM_OK && !strcmp(BF.gathering[0].group, BF_GROUP) &&
              strstr(last_req, "social.group_create") && strstr(last_req, "\"open\":false"),
          "bonfire: the fire's invite-only group (%s)", FAKE.err);

    /* BM7: announcing, warned and sent only on the owner's word */
    CHECK(bonfire_announcement(&BF, 0, text, sizeof text) == ZXM_OK &&
              !strcmp(text, "Gathering: Midwinter fire. Starts 1767225600 (seconds since 1970). 4 "
                            "of 5 seats left. At Riverside Green."),
          "bonfire: the announcement: %s", text);
    CHECK(bonfire_announcement(&BF, 0, text, 10) == ZXM_E_TOOBIG, "bonfire: too small a buffer");
    last_req[0] = 0;
    CHECK(bonfire_announce(&BF, 1, true) == ZXM_E_STATE && !last_req[0],
          "bonfire: a gathering with no space: nothing sent");
    CHECK(bonfire_announce(&BF, 0, false) == ZXM_E_STATE && !last_req[0] &&
              !strcmp(BF.notice, BONFIRE_NO_DELETE),
          "bonfire: not confirmed: the notice that posts stay, nothing sent");
    CANNED = BF_OK "{\"cid\":\"" BF_ROOM "\",\"posted\":true}}";
    CHECK(bonfire_announce(&BF, 0, true) == ZXM_OK && strstr(last_req, "pspace.post") &&
              strstr(last_req, BF_ROOM) && strstr(last_req, "Riverside Green") && !BF.notice[0],
          "bonfire: announced on the owner's word (%s)", FAKE.err);
    CANNED = BF_OK "{\"cid\":\"" BF_OTHER "\",\"posted\":true}}";
    CHECK(bonfire_announce(&BF, 0, true) == ZXM_E_PROTOCOL,
          "bonfire: an answer for another space refused");

    /* BM8: who is in the fire's room */
    CANNED = BF_OK "{\"members\":[{\"id\":\"" BF_GROUP "\",\"addr\":\"\",\"last_seen_ms\":1}],"
                   "\"count\":2}}";
    CHECK(bonfire_present(&BF, 0) == ZXM_OK && BF.gathering[0].present == 2 &&
              bonfire_present(&BF, 1) == ZXM_E_STATE,
          "bonfire: one member and this node (%s)", FAKE.err);
    CANNED = BF_OK "{\"members\":[],\"count\":5}}";
    CHECK(bonfire_present(&BF, 0) == ZXM_E_PROTOCOL && BF.gathering[0].present == 2,
          "bonfire: a count past the list and this node refused");
    CANNED = NULL;
    BF.link = &LINK;
    zpl_close();
}
