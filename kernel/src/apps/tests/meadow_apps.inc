/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Meadow's model. The rights record goes through zxvd's real dispatcher
 * (LINK): spaces.* is zxv_spaces.c, linked here, so a park this node owns
 * and a cloud top it was invited into as a guest are made and published
 * the way zxvd does it. pspace.* is served in the daemon by
 * zxvd_pspace.c, which this test does not link (the dispatcher answers
 * -32601), so its answers are made up in their documented shapes (FAKE). */
#include "meadow_model.h"

#define MD_C58(x)                                                                                  \
    x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x x  \
        x x x x x x x x x x x
#define MD_PARK   "b" MD_C58("p")
#define MD_TOP    "b" MD_C58("t")
#define MD_WILD   "b" MD_C58("w")
#define MD_H16(x) x x x x x x x x x x x x x x x x
#define MD_BO     MD_H16("b") MD_H16("b") MD_H16("b") MD_H16("b")
#define MD_CY     MD_H16("c") MD_H16("c") MD_H16("c") MD_H16("c")
#define MD_OK     "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

static meadow_t MD;

static int md_raw(const char *method, const char *params)
{
    zxm_begin(&LINK, method);
    zxm_p_raw(&LINK, params, (uint32_t) strlen(params));
    return zxm_call(&LINK);
}

APPS_TEST(meadow)
{
    static char big[MEADOW_POST_MAX + 2];
    char park[64], q[256];
    int32_t p = -1, t = -1;
    meadow_init(&MD, &LINK);

    /* MR1: the rights record's own table (zxv_spaces.h S1) */
    CHECK(meadow_role_room(MEADOW_PARK, MEADOW_PUBLIC) == 0 &&
              meadow_role_room(MEADOW_PARK, MEADOW_OWNER) == 3 &&
              meadow_role_room(MEADOW_CLOUDTOP, MEADOW_GUEST) == -2 &&
              meadow_role_room(MEADOW_OFFICE, MEADOW_MEMBER) == 0 &&
              meadow_role_room(MEADOW_HOME, MEADOW_MEMBER) == -1 &&
              meadow_role_room(MEADOW_KINDS, MEADOW_OWNER) == MEADOW_NO_ROOM &&
              meadow_role_room(MEADOW_PARK, -1) == MEADOW_NO_ROOM,
          "meadow: who may write in each kind");
    CHECK(meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, 1) &&
              meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, MEADOW_POST_MAX) &&
              !meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, 0) &&
              !meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, MEADOW_POST_MAX + 1) &&
              !meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, INT64_MIN) &&
              !meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, INT64_MAX) &&
              !meadow_seat_may_post(MEADOW_CLOUDTOP, MEADOW_GUEST, 10),
          "meadow: 1..1024 bytes, by a role the kind lets write");

    /* MM1: the rights record through the real dispatcher */
    CHECK(md_raw("spaces.create", "{\"kind\":\"park\",\"name\":\"Meadow Green\"}") == ZXM_OK &&
              zxm_get_str(&LINK, LINK.result, "id", park, sizeof park) == ZXM_OK,
          "meadow: a park this node owns: %s", LINK.err);
    snprintf(q, sizeof q, "{\"space\":\"%s\",\"cid\":\"" MD_PARK "\"}", park);
    CHECK(md_raw("spaces.set_cid", q) == ZXM_OK, "meadow: the park published: %s", LINK.err);
    CHECK(md_raw("spaces.join",
                 "{\"id\":\"meadow-hill\",\"kind\":\"cloudtop\",\"name\":\"Hill\","
                 "\"owner\":\"bo\",\"role\":\"guest\",\"cid\":\"" MD_TOP "\"}") == ZXM_OK,
          "meadow: a cloud top this node is a guest on: %s", LINK.err);
    CHECK(meadow_places(&MD) == ZXM_OK && MD.nplace >= 3u && !strcmp(MD.place[0].id, "home"),
          "meadow: spaces.list, home first: %s", LINK.err);
    for (uint32_t i = 0; i < MD.nplace; i++) {
        if (!strcmp(MD.place[i].id, park)) p = (int32_t) i;
        if (!strcmp(MD.place[i].id, "meadow-hill")) t = (int32_t) i;
    }
    CHECK(p >= 0 && t >= 0 && MD.place[p].kind == MEADOW_PARK && MD.place[p].role == MEADOW_OWNER &&
              MD.place[p].write && !strcmp(MD.place[p].cid, MD_PARK) &&
              MD.place[t].kind == MEADOW_CLOUDTOP && MD.place[t].role == MEADOW_GUEST &&
              !MD.place[t].write && !strcmp(MD.place[t].cid, MD_TOP),
          "meadow: the park's owner writes, the cloud top's guest does not");
    /* zxvd's own write flag and MR1 say the same of every seat */
    for (uint32_t i = 0; i < MD.nplace; i++)
        CHECK(MD.place[i].write == (meadow_role_room(MD.place[i].kind, MD.place[i].role) >= 0),
              "meadow: %s: zxvd's write is MR1's", MD.place[i].id);

    /* pspace.* is not linked here: the dispatcher says so, nothing kept */
    CHECK(meadow_spaces(&MD) == ZXM_E_REMOTE && LINK.err_code == -32601 && MD.nspace == 0 &&
              !MD.off,
          "meadow: pspace.list not linked here: -32601 (%s)", LINK.err);

    /* ---- pspace.*, answers made up in their shapes (FAKE) ---- */
    MD.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32012,\"message\":\"feature "
             "pspace is off\"}}";
    CHECK(meadow_find(&MD, "picnic") == ZXM_E_REMOTE && MD.off && MD.nfound == 0,
          "meadow: the switch off (MM7)");
    last_req[0] = 0;
    CHECK(meadow_find(&MD, "Bad Topic") == ZXM_E_ARG && meadow_find(&MD, "") == ZXM_E_ARG &&
              meadow_find(&MD, "abcdefghijklmnopqrstuvwxy") == ZXM_E_ARG && !last_req[0],
          "meadow: a topic outside [a-z0-9-]{1,24} is not sent");
    CANNED = MD_OK "{\"topic\":\"picnic\",\"done\":false,\"found\":[{\"cid\":\"" MD_WILD
                   "\",\"name\":\"Lawn\",\"topic\":\"picnic\",\"kind\":\"room\",\"gov\":\"owner\","
                   "\"quorum_pct\":51,\"created_ms\":1,\"creator\":\"" MD_BO
                   "\",\"members\":4,\"host\":\"" MD_BO "\"}]}}";
    CHECK(meadow_find(&MD, "picnic") == ZXM_OK && !MD.off && MD.nfound == 1 && !MD.find_done &&
              MD.found[0].members == 4 && MD.found[0].kind == MEADOW_ROOM &&
              !strcmp(MD.found[0].name, "Lawn") && strstr(last_req, "pspace.find") != NULL,
          "meadow: a lawn found, the search goes on (%s)", FAKE.err);
    CANNED = MD_OK "{\"topic\":\"picnic\",\"done\":true,\"found\":[{\"cid\":\"bshort\","
                   "\"members\":4,\"host\":\"" MD_BO "\"}]}}";
    CHECK(meadow_find(&MD, "picnic") == ZXM_E_PROTOCOL && MD.nfound == 1 && !MD.find_done,
          "meadow: a CID that is not 59 bytes refused (SR2), the list before kept");
    CANNED = MD_OK "{\"topic\":\"other\",\"done\":true,\"found\":[]}}";
    CHECK(meadow_find(&MD, "picnic") == ZXM_E_PROTOCOL, "meadow: another topic's answer refused");

    /* MM3: the joined spaces, each seat from the rights record */
    CANNED = MD_OK "{\"online\":true,\"spaces\":["
                   "{\"cid\":\"" MD_PARK "\",\"phase\":\"in\",\"name\":\"Green\",\"topic\":\"\","
                   "\"kind\":\"room\",\"gov\":\"owner\",\"quorum_pct\":51,\"created_ms\":1,"
                   "\"creator\":\"" MD_BO "\",\"members\":3},"
                   "{\"cid\":\"" MD_TOP "\",\"phase\":\"in\",\"members\":2},"
                   "{\"cid\":\"" MD_WILD "\",\"phase\":\"joining\",\"members\":0}]}}";
    CHECK(meadow_spaces(&MD) == ZXM_OK && MD.nspace == 3 && MD.online && MD.people == 5 &&
              MD.space[0].seat_kind == MEADOW_PARK && MD.space[0].seat_role == MEADOW_OWNER &&
              MD.space[1].seat_kind == MEADOW_CLOUDTOP && MD.space[1].seat_role == MEADOW_GUEST &&
              MD.space[2].seat_kind == MEADOW_PARK && MD.space[2].seat_role == MEADOW_PUBLIC &&
              MD.space[2].phase == MEADOW_JOINING && meadow_space_of(&MD, MD_WILD) == 2,
          "meadow: three spaces, five people; owner, guest and public seats (%s)", FAKE.err);
    CHECK(meadow_may_post(&MD, 0, 1) && !meadow_may_post(&MD, 1, 1) &&
              !meadow_may_post(&MD, 2, 1) && !meadow_may_post(&MD, 0, 0) &&
              !meadow_may_post(&MD, 0, MEADOW_POST_MAX + 1) && !meadow_may_post(&MD, 9, 1),
          "meadow: the park's owner may post; the guest, the one joining and an empty post not");
    CANNED = MD_OK "{\"online\":true,\"spaces\":[{\"cid\":\"" MD_PARK
                   "\",\"phase\":\"in\",\"members\":34}]}}";
    CHECK(meadow_spaces(&MD) == ZXM_E_PROTOCOL && MD.nspace == 3 && MD.people == 5,
          "meadow: 34 people in one space refused (MR4), the list before kept");

    /* MM4: events from the last index read */
    CANNED = MD_OK "{\"events\":["
                   "{\"index\":1,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1700000000000,"
                   "\"author\":\"" MD_BO "\",\"text\":\"hello\"},"
                   "{\"index\":2,\"kind\":\"hall\",\"lamport\":2,\"created_ms\":1700000000001,"
                   "\"author\":\"" MD_CY "\",\"state\":\"play\",\"pos_ms\":0,\"rate_q16\":65536},"
                   "{\"index\":3,\"kind\":\"post\",\"lamport\":3,\"created_ms\":1700000000002,"
                   "\"author\":\"" MD_CY "\",\"text\":\"hi\"}],\"next\":3}}";
    CHECK(meadow_events(&MD, 0) == ZXM_OK && MD.nevent == 3 && MD.fresh == 3 && MD.missed == 0 &&
              MD.event[0].post && !MD.event[1].post && !strcmp(MD.event[2].text, "hi") &&
              MD.space[0].last == 3 && MD.space[0].stamp == 1700000000002 &&
              strstr(last_req, "\"since\":0") != NULL,
          "meadow: three events, the last read is 3 (%s)", FAKE.err);
    CHECK(meadow_notice(&MD, 0) == ZXM_OK && !strcmp(MD.notice, "3 new in Green."),
          "meadow: the notice: %s", MD.notice);
    CANNED = MD_OK "{\"events\":[{\"index\":40,\"kind\":\"post\",\"lamport\":9,"
                   "\"created_ms\":1700000000009,\"author\":\"" MD_BO
                   "\",\"text\":\"late\"}],\"next\":40}}";
    CHECK(meadow_events(&MD, 0) == ZXM_OK && MD.fresh == 37 && MD.missed == 36 &&
              MD.space[0].last == 40 && strstr(last_req, "\"since\":3") != NULL &&
              meadow_notice(&MD, 0) == ZXM_OK && !strcmp(MD.notice, "37 new in Green (36 missed)."),
          "meadow: past the 32 kept, the rest missed: %s", MD.notice);
    CANNED = MD_OK "{\"events\":[],\"next\":40}}";
    CHECK(meadow_events(&MD, 0) == ZXM_OK && MD.fresh == 0 && meadow_notice(&MD, 0) == ZXM_OK &&
              !MD.notice[0],
          "meadow: nothing new, no notice");
    CANNED = MD_OK "{\"events\":[{\"index\":12,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1,"
                   "\"author\":\"" MD_BO "\",\"text\":\"old\"}],\"next\":12}}";
    CHECK(meadow_events(&MD, 0) == ZXM_E_PROTOCOL && MD.space[0].last == 40,
          "meadow: a next before the last read is refused (MR3)");
    CANNED = MD_OK "{\"events\":[],\"next\":1000000000000000000}}";
    CHECK(meadow_events(&MD, 0) == ZXM_E_PROTOCOL && MD.space[0].last == 40,
          "meadow: an index past 18 digits refused (MR3, MR5)");
    CHECK(meadow_events(&MD, 7) == ZXM_E_ARG, "meadow: no such space");

    /* members */
    CANNED = MD_OK "{\"members\":[{\"id\":\"" MD_BO "\",\"addr\":\"10.0.0.2:9000\","
                   "\"last_seen_ms\":1},{\"id\":\"" MD_CY "\",\"addr\":\"\",\"last_seen_ms\":2}],"
                   "\"count\":3}}";
    CHECK(meadow_members(&MD, 0) == ZXM_OK && MD.nmember == 2 && MD.space[0].members == 3 &&
              MD.people == 5 && !strcmp(MD.member[0].addr, "10.0.0.2:9000"),
          "meadow: the Green's members (%s)", FAKE.err);
    CANNED = MD_OK "{\"members\":[],\"count\":5}}";
    CHECK(meadow_members(&MD, 0) == ZXM_E_PROTOCOL && MD.space[0].members == 3,
          "meadow: a count past the list and this node refused");

    /* MM5, MM6: posts cannot be deleted, said before each send */
    memset(big, 'a', MEADOW_POST_MAX + 1u);
    big[MEADOW_POST_MAX + 1u] = 0;
    CANNED = MD_OK "{\"cid\":\"" MD_PARK "\",\"posted\":true}}";
    last_req[0] = 0;
    CHECK(meadow_post(&MD, 0, "", true) == ZXM_E_STATE &&
              meadow_post(&MD, 0, big, true) == ZXM_E_ARG &&
              meadow_post(&MD, 0, "\xc3", true) == ZXM_E_ARG &&
              meadow_post(&MD, 1, "hi", true) == ZXM_E_STATE && !last_req[0],
          "meadow: empty, 1025 bytes, half a character, or a guest's seat: not sent");
    CHECK(meadow_post(&MD, 0, "hello green", true) == ZXM_E_STATE && !last_req[0] &&
              !strcmp(MD.notice, MEADOW_NO_DELETE),
          "meadow: first the notice that posts stay, nothing sent");
    CHECK(meadow_post(&MD, 0, "hello green", false) == ZXM_E_STATE && !last_req[0],
          "meadow: not confirmed: nothing sent");
    big[MEADOW_POST_MAX] = 0;
    CHECK(meadow_post(&MD, 0, big, true) == ZXM_OK && strstr(last_req, "pspace.post") != NULL &&
              strstr(last_req, MD_PARK) != NULL && !MD.notice[0],
          "meadow: 1024 bytes posted on the owner's word (%s)", FAKE.err);
    last_req[0] = 0;
    CHECK(meadow_post(&MD, 0, "again", true) == ZXM_E_STATE && !last_req[0] &&
              !strcmp(MD.notice, MEADOW_NO_DELETE) && meadow_post(&MD, 0, "again", true) == ZXM_OK,
          "meadow: the next post is warned again, then sent");
    CANNED = MD_OK "{\"cid\":\"" MD_TOP "\",\"posted\":true}}";
    CHECK(meadow_post(&MD, 0, "x", true) == ZXM_E_STATE &&
              meadow_post(&MD, 0, "x", true) == ZXM_E_PROTOCOL,
          "meadow: an answer for another space refused");

    /* join and leave */
    CANNED = MD_OK "{\"cid\":\"" MD_WILD "\",\"phase\":\"in\"}}";
    CHECK(meadow_join(&MD, "bnot") == ZXM_E_ARG && meadow_join(&MD, MD_WILD) == ZXM_OK &&
              MD.nspace == 3 && MD.space[2].phase == MEADOW_IN && meadow_may_post(&MD, 2, 5),
          "meadow: the lawn joined, its public seat may post in a park (%s)", FAKE.err);
    CANNED = MD_OK "{\"cid\":\"" MD_TOP "\",\"left\":true}}";
    CHECK(meadow_leave(&MD, 1) == ZXM_OK && MD.nspace == 2 && MD.people == 3 &&
              meadow_space_of(&MD, MD_TOP) < 0 && meadow_space_of(&MD, MD_WILD) == 1 &&
              meadow_leave(&MD, 5) == ZXM_E_ARG,
          "meadow: left the hill (%s)", FAKE.err);
    CANNED = MD_OK "{\"online\":false,\"spaces\":[{\"cid\":\"" MD_PARK
                   "\",\"phase\":\"in\",\"members\":3}]}}";
    CHECK(meadow_spaces(&MD) == ZXM_OK && MD.nspace == 1 && !MD.online && MD.space[0].last == 40 &&
              !meadow_may_post(&MD, 0, 1),
          "meadow: offline, no posting; the Green keeps its last index (%s)", FAKE.err);
    CANNED = NULL;
    MD.link = &LINK;
}
