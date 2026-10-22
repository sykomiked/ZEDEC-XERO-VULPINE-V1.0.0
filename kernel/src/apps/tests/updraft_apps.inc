/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Updraft's model against zxvd's real dispatcher (LINK). ehop.* and
 * harmonic.* run as zxvd runs them, through Aerie's model. This in-process
 * zxvd links no device mesh or peer network, so it refuses devmesh.* and
 * net.*; those are answered on a link of Updraft's own (UPD_LINK), which
 * passes every other request to the real dispatcher. Nothing is
 * transmitted: zxvd has no radio method, and the operator keys. */
#include "updraft_model.h"

static updraft_t UPD, UPD2;
static zxm_link_t UPD_LINK;
static int upd_sent; /* requests UPD_LINK answered itself */
static char upd_req[ZXM_REQ_MAX + 1];
static const char *UPD_INVITE, *UPD_JOIN, *UPD_SAS, *UPD_PEERS;

static int32_t upd_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"devmesh.invite\"")) r = UPD_INVITE;
    if (strstr(req, "\"devmesh.join\"")) r = UPD_JOIN;
    if (strstr(req, "\"devmesh.confirm_sas\"")) r = UPD_SAS;
    if (strstr(req, "\"net.peers\"")) r = UPD_PEERS;
    if (!r) return inproc(NULL, req, len, resp, cap);
    upd_sent++;
    memcpy(upd_req, req, len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX);
    upd_req[len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX] = 0;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define UPD_KEY    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
#define UPD_DEVICE "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define UPD_NODE_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define UPD_NODE_B "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"

APPS_TEST(updraft)
{
    const int64_t NOW = 1700000000000LL;
    int before, ubefore;

    /* the real dispatcher: no device mesh, no peer network here */
    updraft_init(&UPD, &LINK);
    CHECK(updraft_invite(&UPD, NOW, true) == ZXM_E_REMOTE && !UPD.invited &&
              updraft_watch(&UPD, NOW) == ZXM_E_REMOTE && UPD.npeers == 0 && UPD.watch_ms == -1,
          "updraft: devmesh.invite and net.peers refused here: %s", LINK.err);

    zxm_link_init(&UPD_LINK, upd_send, &UPD_LINK);
    updraft_init(&UPD, &UPD_LINK);

    /* UM1: the licence, the operator's confirmed word, advice only */
    CHECK(!updraft_may_transmit(&UPD, UPDRAFT_RF_2M, 10) && strstr(UPD.notice, "No licence"),
          "updraft: no licence given: never transmit");
    CHECK(updraft_set_licence(&UPD, 3, 50, true) == ZXM_E_ARG &&
              updraft_set_licence(&UPD, UPDRAFT_RF_2M, UPDRAFT_POWER_MAX + 1, true) == ZXM_E_ARG &&
              updraft_set_licence(&UPD, UPDRAFT_RF_2M, -1, true) == ZXM_E_ARG &&
              updraft_set_licence(&UPD, UPDRAFT_RF_2M, 50, false) == ZXM_E_STATE &&
              !UPD.licence_set[UPDRAFT_RF_2M],
          "updraft: a band or power out of range, or no confirmed word: nothing kept");
    CHECK(updraft_set_licence(&UPD, UPDRAFT_RF_2M, 50, true) == ZXM_OK &&
              updraft_may_transmit(&UPD, UPDRAFT_RF_2M, 50) &&
              updraft_may_transmit(&UPD, UPDRAFT_RF_2M, 0) &&
              !updraft_may_transmit(&UPD, UPDRAFT_RF_2M, 51) && strstr(UPD.notice, "past") &&
              !updraft_may_transmit(&UPD, UPDRAFT_RF_70CM, 10) &&
              !updraft_may_transmit(&UPD, UPDRAFT_RF_2M, -1),
          "updraft: 50 dBm on 2 m is within a 50 dBm licence, 51 is past it: %s", UPD.notice);

    /* UM2: pairing a relay station */
    UPD_INVITE = "{\"code\":\"AB2CD-EF3GH\",\"expires_in_ms\":600000,\"qr\":\"ABC\"}";
    ubefore = upd_sent;
    CHECK(updraft_invite(&UPD, -1, true) == ZXM_E_ARG &&
              updraft_invite(&UPD, NOW, false) == ZXM_E_STATE && upd_sent == ubefore &&
              !UPD.invited,
          "updraft: a time before 1970, or no confirmed word: nothing asked");
    CHECK(updraft_invite(&UPD, NOW, true) == ZXM_OK && UPD.invited &&
              !strcmp(UPD.code, "AB2CD-EF3GH") && UPD.ttl_ms == 600000 &&
              strstr(upd_req, "\"role\":\"thin\"") && strstr(upd_req, "\"held\":false") &&
              strstr(upd_req, "\"admin\":false") && strstr(UPD.notice, "10 minutes"),
          "updraft: a relay is invited thin, not held, never an admin: %s", UPD.notice);
    UPD_INVITE = "{\"code\":\"ab2cd-ef3gh\",\"expires_in_ms\":600000}";
    CHECK(updraft_invite(&UPD, NOW, true) == ZXM_E_PROTOCOL && !strcmp(UPD.code, "AB2CD-EF3GH"),
          "updraft: a code of another form is no answer; the last one kept");
    UPD_JOIN = "{\"pairing\":true,\"frames_waiting\":1,\"next\":\"compare\"}";
    ubefore = upd_sent;
    CHECK(updraft_join(&UPD, "AB2CDEF3GH", true) == ZXM_E_ARG &&
              updraft_join(&UPD, "AB2CD-EF3GH", false) == ZXM_E_STATE && upd_sent == ubefore &&
              !UPD.pairing,
          "updraft: a code of another form, or no confirmed word: nothing asked");
    CHECK(updraft_join(&UPD, "AB2CD-EF3GH", true) == ZXM_OK && UPD.pairing &&
              strstr(upd_req, "AB2CD-EF3GH") && strstr(UPD.notice, "six digits"),
          "updraft: joining: %s", UPD.notice);
    UPD_SAS = "{\"confirmed\":true,\"in_roster\":true,\"devices\":3}";
    ubefore = upd_sent;
    CHECK(updraft_confirm_sas(&UPD, "xyz", true, true) == ZXM_E_ARG &&
              updraft_confirm_sas(&UPD, UPD_DEVICE, true, false) == ZXM_E_STATE &&
              upd_sent == ubefore && UPD.pairing,
          "updraft: a device id that is none, or no confirmed word: nothing asked");
    CHECK(updraft_confirm_sas(&UPD, UPD_DEVICE, true, true) == ZXM_OK && !UPD.pairing &&
              UPD.devices == 3 && strstr(upd_req, UPD_DEVICE) &&
              strstr(upd_req, "\"match\":true") && strstr(UPD.notice, "Relay paired"),
          "updraft: the digits match: %s", UPD.notice);
    UPD_SAS = "{\"confirmed\":true,\"in_roster\":true,\"devices\":99}";
    CHECK(updraft_confirm_sas(&UPD, UPD_DEVICE, true, true) == ZXM_E_PROTOCOL && UPD.devices == 3,
          "updraft: a mesh past its eight devices is no answer");

    /* UM3: links and the matrix */
    CHECK(updraft_set_link(&UPD, 0, 1, 30, 0, UPDRAFT_RF_2M, -120) == 0 &&
              UPD.links[0].margin == 74 &&
              updraft_set_link(&UPD, 1, 0, 20, 2, UPDRAFT_RF_2M, -110) == 1 &&
              UPD.links[1].margin == 40 &&
              updraft_set_link(&UPD, 0, 2, 0, 7, UPDRAFT_RF_70CM, -60) == 2 &&
              UPD.links[2].margin == -71 && strstr(UPD.notice, "below the far end's floor"),
          "updraft: three links: %s", UPD.notice);
    CHECK(updraft_set_link(&UPD, 3, 3, 30, 0, 1, -120) == ZXM_E_ARG &&
              updraft_set_link(&UPD, 0, 4, 62, 0, 1, -120) == ZXM_E_ARG &&
              updraft_set_link(&UPD, 0, 4, 30, 8, 1, -120) == ZXM_E_ARG &&
              updraft_set_link(&UPD, 1000, 4, 30, 0, 1, -120) == ZXM_E_ARG && UPD.nlinks == 3,
          "updraft: a station to itself, a power, a step or a station out of range: nothing kept");
    CHECK(updraft_link_state_at(&UPD, 0) == UPDRAFT_TWO_WAY &&
              updraft_link_state_at(&UPD, 1) == UPDRAFT_TWO_WAY &&
              updraft_link_state_at(&UPD, 2) == UPDRAFT_DOWN &&
              updraft_link_state_at(&UPD, 3) == UPDRAFT_REFUSED &&
              updraft_set_link(&UPD, 1, 0, 0, 7, UPDRAFT_RF_70CM, -60) == 1 && UPD.nlinks == 3 &&
              updraft_link_state_at(&UPD, 0) == UPDRAFT_ONE_WAY,
          "updraft: two-way, down, and one-way once 1 no longer reaches 0");

    /* UM4: relaying, through Aerie's model on the real dispatcher */
    aerie_chan_t tx = {AERIE_PRIVATE, UPD_KEY, 3, 7, 1, 11},
                 ham = {AERIE_AMATEUR, UPD_KEY, 0, 0, 0, 0};
    const uint8_t *X = (const uint8_t *) "net check at 1900",
                  *Y = (const uint8_t *) "net check at 1930";
    CHECK(updraft_bands(&UPD) == ZXM_OK && UPD.radio.nbands == 5, "updraft: ehop.bands: %s",
          LINK.err);
    CHECK(updraft_allow_bands(&UPD, 1u << 3, false) == ZXM_E_STATE &&
              UPD.radio.allowed == AERIE_BANDS_NONE &&
              updraft_allow_bands(&UPD, 1u << 3, true) == ZXM_OK,
          "updraft: the chat band, on the owner's word");
    before = sent;
    ubefore = upd_sent;
    CHECK(updraft_relay(&UPD, &tx, true, X, 17, NOW, false) == ZXM_E_STATE &&
              updraft_relay(&UPD, &ham, true, X, 17, NOW, true) == ZXM_E_STATE &&
              updraft_relay(&UPD, &tx, false, X, 17, NOW, true) == ZXM_E_ARG &&
              updraft_relay(&UPD, &tx, true, X, 17, -1, true) == ZXM_E_ARG && sent == before &&
              upd_sent == ubefore && UPD.relayed == 0 && UPD.radio.nlog == 0,
          "updraft: no confirmed word, sealed on amateur radio, clear on a private link: "
          "nothing asked");
    CHECK(updraft_relay(&UPD, &tx, true, X, 17, NOW, true) == ZXM_OK && UPD.relayed == 1 &&
              strlen(UPD.radio.frame_hex) > 0 && UPD.radio.nlog == 1,
          "updraft: sealed for a private link: %s", LINK.err);
    static char fx[AERIE_FRAME_HEX + 1], fy[AERIE_FRAME_HEX + 1];
    memcpy(fx, UPD.radio.frame_hex, strlen(UPD.radio.frame_hex) + 1u);
    CHECK(updraft_relay(&UPD, &tx, true, Y, 17, NOW + 1, true) == ZXM_OK && UPD.relayed == 2,
          "updraft: a second message sealed: %s", LINK.err);
    memcpy(fy, UPD.radio.frame_hex, strlen(UPD.radio.frame_hex) + 1u);
    CHECK(updraft_relay(&UPD, &ham, false, X, 17, NOW, true) == ZXM_E_STATE && UPD.relayed == 2,
          "updraft: amateur radio off until the operator's licence and call sign");
    CHECK(aerie_ham_enable(&UPD.radio, "W1AW", true) == ZXM_OK &&
              updraft_relay(&UPD, &ham, false, X, 17, NOW, true) == ZXM_OK && UPD.relayed == 3 &&
              strstr(UPD.radio.ham_text, "W1AW") && strstr(UPD.radio.ham_text, "net check") &&
              strstr(UPD.notice, "in the clear"),
          "updraft: on amateur radio in the clear, the call sign with it: %s", UPD.notice);

    /* UM5, UM6: two relays hear frame 31 two ways */
    updraft_init(&UPD2, &UPD_LINK);
    aerie_chan_t rx = {AERIE_PRIVATE, UPD_KEY, 3, 7, 1, 12};
    CHECK(updraft_bands(&UPD2) == ZXM_OK && updraft_allow_bands(&UPD2, 1u << 3, true) == ZXM_OK,
          "updraft: the far station's bands");
    int64_t dx = updraft_fletcher(X, 17), dy = updraft_fletcher(Y, 17);
    CHECK(dx != dy && dx >= 0 && dx <= UPDRAFT_DIGEST_MAX && updraft_fletcher(NULL, 0) == 0 &&
              updraft_fletcher(NULL, 1) == -1,
          "updraft: the two payloads' checksums differ");
    CHECK(updraft_hear(&UPD2, &rx, fx, NOW, 31, 1, 0, 1) == ZXM_OK && UPD2.heard == 1 &&
              updraft_frame(&UPD2, 31)->state == LPRES_STATE_TRUE &&
              updraft_frame(&UPD2, 31)->settled == dx && UPD2.radio.nlog == 1,
          "updraft: relay 1 hears frame 31 as X: %s", LINK.err);
    CHECK(updraft_hear(&UPD2, &rx, fx, NOW, 31, 3, 0, 1) == ZXM_E_STATE && UPD2.heard == 1,
          "updraft: the same sealed frame again is a replay, not a third relay");
    CHECK(updraft_hear(&UPD2, &rx, fy, NOW, 31, 2, 1, 2) == ZXM_OK &&
              updraft_frame(&UPD2, 31)->state == LPRES_STATE_BOTH &&
              updraft_frame(&UPD2, 31)->settled == -1 && strstr(UPD2.notice, "held"),
          "updraft: relay 2 hears frame 31 as Y: BOTH, held: %s", UPD2.notice);
    CHECK(updraft_set_room(&UPD2, 1, 1, 2) == ZXM_E_ARG &&
              updraft_set_room(&UPD2, 1, 1, 1) == ZXM_OK && !updraft_bulletin_ok(&UPD2, 31) &&
              !updraft_bulletin_ok(&UPD2, 99),
          "updraft: a BOTH frame is never posted to the room");
    CHECK(updraft_report(&UPD2, 31, 2, 0, 1, X, 17) == ZXM_E_STATE &&
              updraft_report(&UPD2, 31, 4, 4, 1, X, 17) == ZXM_E_ARG &&
              updraft_report(&UPD2, 31, 4, 0, 100, X, 17) == ZXM_E_ARG &&
              updraft_frame(&UPD2, 31)->nrep == 2,
          "updraft: one report a relay; a relay its own parent or 100 hops: nothing kept");
    CHECK(updraft_report(&UPD2, 31, 3, 1, 2, X, 17) == ZXM_OK &&
              updraft_frame(&UPD2, 31)->state == LPRES_STATE_TRUE &&
              updraft_frame(&UPD2, 31)->settled == dx && updraft_bulletin_ok(&UPD2, 31),
          "updraft: a third relay hears X: frame 31 settles to X and may be posted");
    CHECK(updraft_report(&UPD2, 32, 1, 0, 1, X, 17) == ZXM_OK &&
              updraft_report(&UPD2, 32, 2, 0, 1, Y, 17) == ZXM_OK &&
              updraft_frame(&UPD2, 32)->state == LPRES_STATE_BOTH &&
              updraft_frame_checksum(&UPD2, 32, dy) == ZXM_OK &&
              updraft_frame(&UPD2, 32)->state == LPRES_STATE_TRUE &&
              updraft_frame(&UPD2, 32)->settled == dy &&
              updraft_frame_checksum(&UPD2, 32, 7) == ZXM_OK &&
              updraft_frame(&UPD2, 32)->state == LPRES_STATE_FALSE &&
              !updraft_bulletin_ok(&UPD2, 32) &&
              updraft_frame_checksum(&UPD2, 33, dy) == ZXM_E_STATE &&
              updraft_frame_checksum(&UPD2, 32, -1) == ZXM_E_ARG,
          "updraft: frame 32's checksum names Y: settled to Y; one no relay heard: FALSE");
    for (int64_t k = 0; k < (int64_t) UPDRAFT_FRAMES; k++)
        if (updraft_report(&UPD2, 100 + k, 1, 0, 1, X, 17) != ZXM_OK) break;
    CHECK(UPD2.nframes == UPDRAFT_FRAMES && !updraft_frame(&UPD2, 31) &&
              updraft_frame(&UPD2, 100 + UPDRAFT_FRAMES - 1),
          "updraft: the oldest frames go when the table is full");

    /* UM7: the watch */
    UPD_PEERS = "{\"peers\":[{\"id\":\"" UPD_NODE_A "\",\"addr\":\"192.168.1.9:7000\","
                "\"bucket\":3,\"ubh\":true,\"last_seen_ms\":\"1700000000000\",\"in_book\":true,"
                "\"member\":true},{\"id\":\"" UPD_NODE_B "\",\"addr\":\"\",\"bucket\":9,"
                "\"ubh\":false,\"last_seen_ms\":\"1699999000000\",\"in_book\":false,"
                "\"member\":false}],\"count\":2}";
    CHECK(updraft_watch(&UPD2, NOW + 1000) == ZXM_OK && UPD2.npeers == 2 && UPD2.live == 1 &&
              UPD2.stale == 1 && UPD2.frame_rate == -1 && UPD2.peers[0].member &&
              strstr(UPD2.notice, "1 live, 1 stale"),
          "updraft: one peer live, one stale; no rate before two watches: %s", UPD2.notice);
    uint64_t h = UPD2.heard;
    CHECK(updraft_report(&UPD2, 500, 1, 0, 1, Y, 17) == ZXM_OK &&
              updraft_report(&UPD2, 500, 2, 0, 1, Y, 17) == ZXM_OK && UPD2.heard == h + 2 &&
              updraft_watch(&UPD2, NOW + 3000) == ZXM_OK && UPD2.frame_rate == 1,
          "updraft: two frames heard in two seconds: one a second (sounding.rate)");
    CHECK(updraft_watch(&UPD2, NOW) == ZXM_E_ARG && UPD2.watch_ms == NOW + 3000,
          "updraft: a watch before the last is refused");
    UPD_PEERS = "{\"peers\":[{\"id\":\"aaaa\",\"last_seen_ms\":\"0\",\"member\":false}]}";
    CHECK(updraft_watch(&UPD2, NOW + 4000) == ZXM_E_PROTOCOL && UPD2.npeers == 2,
          "updraft: a NodeID of 2 bytes is no NodeID; nothing kept");
    CHECK(!strstr(upd_req, "amount") && !strstr(upd_req, "ledger"),
          "updraft: messages only, never money");
}
