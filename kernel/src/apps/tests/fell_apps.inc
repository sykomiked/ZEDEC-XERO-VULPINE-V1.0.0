/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fell's model against zxvd's real dispatcher (LINK): place.* is zxvd's
 * own (zxv_place.c), files.* its Hoard (zxv_files.c), and ehop.*,
 * harmonic.* and freight.* its radio framing, none of which transmits.
 * chat.*, pspace.* are served in the daemon by zxvd_chat.c and
 * zxvd_pspace.c, which this test does not link, so the team room's answers
 * are made up in their documented shapes (FAKE), as Commons's test does.
 * The incident, the grid, the sectors' priority, the BOTH test (a sighting
 * in sector 4 at 14:10 and one in sector 9 at 14:12: the lead is BOTH and
 * neither sector dropped), crews overdue, claims, casualties kept in the
 * Hoard, the team room, and the radio; every change refused without the
 * owner's word, with nothing sent and nothing changed. */
#include "fell_model.h"

#define FL_H16(x) x x x x x x x x x x x x x x x x
#define FL_H64(x) FL_H16(x) FL_H16(x) FL_H16(x) FL_H16(x)
#define FL_ME     FL_H64("a")
#define FL_BO     FL_H64("b")
#define FL_P1     FL_H64("1")
#define FL_P2     FL_H64("2")
#define FL_OK     "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"
#define FL_KEY    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"

#define FL_POST(id, author, mine, lamport, text)                                                   \
    "{\"id\":\"" id "\",\"author\":\"" author "\",\"mine\":" mine                                  \
    ",\"kind\":\"post\",\"lamport\":\"" lamport                                                    \
    "\",\"wall_ms\":\"1700000000000\",\"text\":\"" text                                            \
    "\",\"reply_to\":null,\"replies\":0,\"reactions\":[]}"

static fell_t FE, FE2;

APPS_TEST(fell)
{
    int before;
    int64_t sector = 0, s1 = -1, s2 = -1, s3 = -1;

    CHECK(zpl_open(NULL) == 0, "fell: places in memory");
    zxm_link_init(&FAKE, canned, &FAKE);
    fell_init(&FE, &LINK);
    FE.room.link = &FAKE; /* the team room: made-up answers */

    /* FM1: the incident opens only on the commander's word */
    before = sent;
    CHECK(fell_open_incident(&FE, 17, false) == ZXM_E_STATE && !FE.open && sent == before &&
              fell_open_incident(&FE, 0, true) == ZXM_E_ARG &&
              fell_open_incident(&FE, FELL_INCIDENT_MAX + 1, true) == ZXM_E_ARG,
          "fell: FM1, unconfirmed or out of range: nothing opened");
    CHECK(fell_add_sector(&FE, 1, 5000, true) == ZXM_E_STATE,
          "fell: no sector before the incident opens");
    CHECK(fell_open_incident(&FE, 17, true) == ZXM_OK && FE.open && FE.incident == 17 &&
              fell_open_incident(&FE, 18, true) == ZXM_E_STATE,
          "fell: incident 17 open, one at a time");

    /* Waymark's places: place.add on the owner's word, read back by place.list */
    before = sent;
    CHECK(fell_add_place(&FE, "Fell corner", 46000000, 7000000, "grid corner", false) ==
                  ZXM_E_STATE &&
              sent == before && FE.map.nplaces == 0,
          "fell: a place unconfirmed: nothing sent, nothing kept");
    CHECK(fell_add_place(&FE, "Fell corner", 46000000, 7000000, "grid corner", true) == ZXM_OK &&
              fell_add_place(&FE, "Fell ridge", 46002500, 7001500, "", true) == ZXM_OK &&
              fell_add_place(&FE, "Fell gully", 46000500, 7009000, "", true) == ZXM_OK &&
              FE.map.nplaces == 3 && fell_refresh_map(&FE) == ZXM_OK && FE.map.nplaces == 3,
          "fell: three places kept (place.add, place.here, place.list): %s", LINK.err);
    CHECK(fell_add_place(&FE, "Fell off", 90000001, 0, "", true) == ZXM_E_ARG &&
              FE.map.nplaces == 3,
          "fell: a place off the globe is refused by Waymark");

    /* FM2: the grid, from a kept place */
    CHECK(fell_set_grid(&FE, "Nowhere", 1000, 3, true) == ZXM_E_ARG &&
              fell_set_grid(&FE, "Fell corner", 99, 3, true) == ZXM_E_ARG &&
              fell_set_grid(&FE, "Fell corner", 1000, 65, true) == ZXM_E_ARG &&
              fell_set_grid(&FE, "Fell corner", 1000, 3, false) == ZXM_E_STATE && !FE.grid_set &&
              fell_sector_at(&FE, 46000000, 7000000, &sector) == ZXM_E_STATE,
          "fell: a grid needs a kept corner, its limits and the commander's word");
    CHECK(fell_set_grid(&FE, "Fell corner", 1000, 3, true) == ZXM_OK &&
              fell_sector_at(&FE, 46002500, 7001500, &sector) == ZXM_OK && sector == 8 &&
              fell_sector_at(&FE, 46000000, 7000000, &sector) == ZXM_OK && sector == 1 &&
              fell_sector_at(&FE, 46000500, 7009000, &sector) == ZXM_E_ARG &&
              fell_sector_at(&FE, 45999999, 7000000, &sector) == ZXM_E_ARG,
          "fell: FR3, the ridge is sector 8; a point past the grid or south of it refused");

    /* FM3: twelve sectors, their priority (FR1) */
    static const int64_t poa[12] = {500, 500,  1000, 2000, 500, 500,
                                    500, 1500, 1000, 1000, 500, 500};
    bool all = true;
    for (int64_t s = 1; s <= 12; s++)
        all = fell_add_sector(&FE, s, poa[s - 1], true) == ZXM_OK && all;
    CHECK(all && FE.nsector == 12 && fell_add_sector(&FE, 13, 100, false) == ZXM_E_STATE &&
              fell_add_sector(&FE, 1, 100, true) == ZXM_E_ARG &&
              fell_add_sector(&FE, 14, 10001, true) == ZXM_E_ARG && FE.nsector == 12,
          "fell: twelve sectors; unconfirmed, twice or past 10000 refused");
    CHECK(fell_priorities(&FE) == ZXM_OK && FE.sector[3].priority == 2000 &&
              FE.sector[7].priority == 1500 && strstr(FE.notice, "sector 4, 20.00%") &&
              strstr(FE.notice, "commander decides"),
          "fell: sector 4 suggested first: %s", FE.notice);
    CHECK(fell_set_coverage(&FE, 4, 7500, false) == ZXM_E_STATE && FE.sector[3].cov == 0 &&
              fell_set_coverage(&FE, 4, 7500, true) == ZXM_OK && fell_priorities(&FE) == ZXM_OK,
          "fell: sector 4 searched to 75%%, on the commander's word");
    {
        int64_t sum = 0;
        for (uint32_t i = 0; i < FE.nsector; i++) sum += FE.sector[i].priority;
        CHECK(sum == FELL_X100 && FE.sector[3].priority < 2000 && strstr(FE.notice, "sector 8") &&
                  fell_unsearched_count(&FE, 5000) == 11u &&
                  fell_unsearched_count(&FE, 7501) == 12u,
              "fell: after the search the priorities still sum to 10000, sector 8 next (%s)",
              FE.notice);
    }

    /* FM4, FR8: the BOTH test. Sector 4 at 14:10, sector 9 at 14:12 (seconds of the day) */
    CHECK(fell_sighting(&FE, 99, 51000) == ZXM_E_ARG && fell_sighting(&FE, 4, -1) == ZXM_E_ARG &&
              fell_leads(&FE, 51200) == ZXM_OK && FE.lead == LPRES_STATE_NEITHER && FE.nlead == 0,
          "fell: no sighting yet: no lead (NEITHER)");
    CHECK(fell_sighting(&FE, 4, 51000) == ZXM_OK && fell_leads(&FE, 51100) == ZXM_OK &&
              FE.lead == LPRES_STATE_TRUE && FE.nlead == 1 &&
              FE.sector[3].seen == LPRES_STATE_TRUE && FE.sector[8].seen == LPRES_STATE_FALSE,
          "fell: one sighting: sector 4 is the lead (TRUE)");
    CHECK(fell_sighting(&FE, 9, 51120) == ZXM_OK && fell_leads(&FE, 51200) == ZXM_OK &&
              (int) FE.lead == 3 && FE.nlead == 2 && FE.recent == 2 &&
              FE.sector[3].seen == LPRES_STATE_BOTH && FE.sector[8].seen == LPRES_STATE_BOTH &&
              fell_is_lead(FE.sector[3].seen) && fell_is_lead(FE.sector[8].seen) &&
              strstr(FE.notice, "sector 4") && strstr(FE.notice, "sector 9") &&
              strstr(FE.notice, "BOTH"),
          "fell: sector 4 at 14:10 and sector 9 at 14:12: BOTH (state 3), neither dropped: %s",
          FE.notice);
    CHECK(fell_leads(&FE, 51000 + FELL_SIGHT_WINDOW + 1) == ZXM_OK && FE.lead == LPRES_STATE_TRUE &&
              FE.nlead == 1 && FE.sector[8].seen == LPRES_STATE_TRUE,
          "fell: fifteen minutes on, only the later sighting counts");
    CHECK(fell_sighting_at(&FE, "Fell ridge", 51300) == ZXM_OK &&
              FE.sight[FE.nsight - 1u].sector == 8 &&
              fell_sighting_at(&FE, "Fell gully", 51300) == ZXM_E_ARG,
          "fell: a sighting at a kept place falls in its sector; off the grid refused");
    while (FE.nsight < FELL_SIGHTINGS) fell_sighting(&FE, 1, 40000);
    CHECK(fell_sighting(&FE, 1, 40000) == ZXM_E_STATE && FE.nsight == FELL_SIGHTINGS,
          "fell: sightings full: refused, never overwritten");

    /* FM5, FR2: crews */
    CHECK(fell_add_crew(&FE, 1, 0, false) == ZXM_E_STATE && FE.ncrew == 0 &&
              fell_add_crew(&FE, 1, 0, true) == ZXM_OK &&
              fell_add_crew(&FE, 2, 0, true) == ZXM_OK &&
              fell_add_crew(&FE, 2, 0, true) == ZXM_E_ARG && fell_check_in(&FE, 1, 50) == ZXM_OK &&
              fell_check_in(&FE, 1, 40) == ZXM_E_ARG && fell_check_in(&FE, 3, 50) == ZXM_E_ARG,
          "fell: two crews; a check-in never goes back");
    CHECK(fell_crew_watch(&FE, 60) == ZXM_OK && FE.overdue == 0 && !FE.notice[0],
          "fell: at minute 60 both crews are safe");
    CHECK(fell_crew_watch(&FE, 61) == ZXM_OK && FE.overdue == 1 && !FE.crew[0].overdue &&
              FE.crew[1].overdue && strstr(FE.notice, "Crew 2 overdue") &&
              strstr(FE.notice, "calls nobody"),
          "fell: crew 2 missed its check-in: overdue, for the commander: %s", FE.notice);
    CHECK(fell_check_in(&FE, 1, 720) == ZXM_OK && fell_check_in(&FE, 2, 720) == ZXM_OK &&
              fell_crew_watch(&FE, 720) == ZXM_OK && FE.overdue == 0 &&
              fell_crew_watch(&FE, 721) == ZXM_OK && FE.overdue == 2,
          "fell: twelve hours is the shift's end; a minute past, both are overdue");

    /* FM6, FR6: callout claims */
    CHECK(fell_add_claim(&FE, 1, 10, 2500, 40, 45, false) == ZXM_E_STATE && FE.nclaim == 0 &&
              fell_add_claim(&FE, 1, 10, 2500, 40, 45, true) == ZXM_OK &&
              FE.claim[0].claim == 26800 && FE.claims_total == 26800 &&
              fell_add_claim(&FE, 1, 999, 999999, 9999, 9999, true) == ZXM_E_ARG &&
              fell_add_claim(&FE, 9, 1, 1, 1, 1, true) == ZXM_E_ARG && FE.claims_total == 26800,
          "fell: a claim of 268.00 kept; one past 9(9) or for no crew refused");

    /* FM7, FR7: casualties, kept and written to the Hoard */
    CHECK(fell_add_casualty(&FE, 1, 4, 2, 1700000000000LL, 3, "penicillin", false) == ZXM_E_STATE &&
              FE.ncas == 0 &&
              fell_add_casualty(&FE, 1, 4, 2, 1700000000000LL, 3, "penicillin", true) == ZXM_OK &&
              fell_add_casualty(&FE, 2, 9, 0, 1700000060000LL, 0, "", true) == ZXM_OK &&
              fell_add_casualty(&FE, 3, 99, 1, 0, 0, "", true) == ZXM_E_ARG &&
              fell_add_casualty(&FE, 3, 4, 5, 0, 0, "", true) == ZXM_E_ARG &&
              fell_add_casualty(&FE, 3, 4, 1, 0, 7, "x", true) == ZXM_E_ARG &&
              fell_add_casualty(&FE, 3, 4, 1, 0, 3, "a\nb", true) == ZXM_E_ARG &&
              fell_add_casualty(&FE, 1, 4, 1, 0, 0, "", true) == ZXM_E_ARG && FE.ncas == 2,
          "fell: two casualties; an unkept sector, a triage past 4, a field past 6, a note "
          "of two lines or a second casualty 1 refused");
    before = sent;
    CHECK(fell_save(&FE, false) == ZXM_E_STATE && sent == before,
          "fell: the file is written only on the owner's word");
    CHECK(fell_save(&FE, true) == ZXM_OK &&
              !strcmp(FE.file, "fell 1\nincident 17\ncas 1 4 2 1700000000000 3 penicillin\n"
                               "cas 2 9 0 1700000060000 0\n"),
          "fell: written to the Hoard: %s", LINK.err);
    fell_init(&FE2, &LINK);
    CHECK(fell_load(&FE2) == ZXM_OK && FE2.open && FE2.incident == 17 && FE2.ncas == 2 &&
              FE2.cas[0].sector == 4 && FE2.cas[0].field == 3 &&
              !strcmp(FE2.cas[0].note, "penicillin") && FE2.cas[1].stamp == 1700000060000LL &&
              !FE2.cas[1].note[0],
          "fell: read back whole: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", FELL_FILE);
    zxm_p_str(&LINK, "text", "fell 1\nincident 17\ncas 3 4 1 0 0\ncas 3 4 1 0 0\n");
    CHECK(zxm_call(&LINK) == ZXM_OK && fell_load(&FE2) == ZXM_E_PROTOCOL && FE2.ncas == 2 &&
              FE2.cas[0].casualty == 1 && strstr(FE2.notice, "damaged"),
          "fell: a file with one casualty twice changes nothing");

    /* FM8: the team room, Commons's */
    CANNED =
        FL_OK "{\"posts\":[" FL_POST(FL_P2, FL_BO, "false", "8", "crew 3 at the gully") "," FL_POST(
            FL_P1, FL_ME, "true", "7", "briefing at 06:00") "]}}";
    CHECK(fell_room_feed(&FE) == ZXM_OK && FE.room.nfeed == 2 &&
              !strcmp(FE.room.feed[0].text, "crew 3 at the gully") && strstr(last_req, "chat.feed"),
          "fell: the team room's feed, as zxvd ranks it (%s)", FAKE.err);
    last_req[0] = 0;
    CHECK(fell_room_post(&FE, 0, "sector 8 next", false) == ZXM_E_STATE && !last_req[0] &&
              fell_room_post(&FE, 5, "sector 8 next", true) == ZXM_E_ARG && !last_req[0],
          "fell: a post unconfirmed or to no room: nothing sent");
    CANNED = FL_OK "{\"id\":\"" FL_P2 "\",\"author\":\"" FL_ME "\",\"kind\":\"post\"}}";
    CHECK(fell_room_post(&FE, 0, "sector 8 next", true) == ZXM_OK &&
              strstr(last_req, "chat.post") && strstr(last_req, "sector 8 next"),
          "fell: posted to the team (%s)", FAKE.err);
    last_req[0] = 0;
    before = sent;
    CHECK(fell_share_post(&FE, 1, false) == ZXM_E_STATE && sent == before && !last_req[0],
          "fell: sharing where the command post is waits for the owner's word");
    CHECK(fell_share_post(&FE, 1, true) == ZXM_E_STATE && !last_req[0] &&
              strstr(FE.notice, "not shared"),
          "fell: not opted in and no location: not shared (%s)", FE.notice);
    CHECK(fell_set_share(&FE, true, false) == ZXM_E_STATE && !FE.share_optin &&
              fell_set_share(&FE, true, true) == ZXM_OK && FE.share_optin,
          "fell: sharing turned on, confirmed");
    CHECK(fell_share_post(&FE, 1, true) == ZXM_E_STATE && !last_req[0],
          "fell: opted in, but zxvd holds no location: not shared");
    CHECK(waymark_set_here(&FE.map, -33868800, 151209300, 25) == ZXM_OK &&
              fell_share_post(&FE, 7, true) == ZXM_E_STATE && !last_req[0],
          "fell: a crew not on the roster is not told");
    CHECK(
        fell_share_post(&FE, 1, true) == ZXM_OK && strstr(last_req, "chat.post") &&
            strstr(last_req, "Crew 1: the command post is at -33.868800, 151.209300 (within 25 m)"),
        "fell: where the command post is, posted for crew 1: %s", last_req);

    /* FM9: radio, composed and never transmitted */
    static const aerie_chan_t ch = {AERIE_PRIVATE, FL_KEY, 3, 7, 1, 11},
                              rx = {AERIE_PRIVATE, FL_KEY, 3, 7, 1, 12},
                              ham = {AERIE_AMATEUR, FL_KEY, 3, 7, 1, 11};
    before = sent;
    CHECK(fell_radio_bands(&FE, (1u << 0) | (1u << 3), false) == ZXM_E_STATE && sent == before &&
              FE.radio.allowed == AERIE_BANDS_NONE,
          "fell: the radio bands are the owner's choice");
    CHECK(fell_radio_bands(&FE, 1u << AERIE_SETTLE, true) == ZXM_E_ARG &&
              fell_radio_bands(&FE, (1u << 0) | (1u << 3), true) == ZXM_OK && FE.radio.nbands == 5,
          "fell: never the settle band; control and chat allowed (ehop.bands): %s", LINK.err);
    before = sent;
    CHECK(fell_radio_seal(&FE, &ham, "crew 2 found the subject", 1700000000000LL) == ZXM_E_STATE &&
              sent == before,
          "fell: never sealed on amateur radio (AR5): nothing sent");
    CHECK(fell_radio_seal(&FE, &ch, "crew 2 found the subject", 1700000000000LL) == ZXM_OK &&
              FE.radio.frame_hex[0],
          "fell: sealed on the chat band (ehop.seal): %s", LINK.err);
    fell_init(&FE2, &LINK);
    CHECK(fell_radio_bands(&FE2, 1u << 3, true) == ZXM_OK &&
              fell_radio_open(&FE2, &rx, FE.radio.frame_hex, 1700000000000LL) == ZXM_OK &&
              FE2.radio.ndata == 24 && !memcmp(FE2.radio.data, "crew 2 found the subject", 24),
          "fell: opened at the other end (ehop.open): %s", LINK.err);
    CHECK(fell_line_send(&FE, 2, 0, 0) == ZXM_E_ARG && fell_line_send(&FE, 1, 2, 3) == ZXM_OK &&
              FE.radio.line.tag == 123 &&
              fell_line_read(&FE, FE.radio.line.frame_hex, &s1, &s2, &s3) == ZXM_OK && s1 == 1 &&
              s2 == 2 && s3 == 3,
          "fell: a line frame's tag 123, split back (harmonic.encode, harmonic.decode): %s",
          LINK.err);
    CHECK(fell_freight(&FE, "") == ZXM_E_ARG &&
              fell_freight(&FE, "sector 8: two hikers, one injured, ridge trail") == ZXM_OK &&
              FE.radio.freight.payload_len == 46,
          "fell: a long message as freight (freight.pack): %s", LINK.err);
}
