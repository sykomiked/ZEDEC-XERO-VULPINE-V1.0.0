/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Watchfire's model against zxvd's real dispatcher (LINK): place.* and
 * files.* are zxvd's own (zxv_place.c, zxv_files.c). zxvd_people.c, which
 * serves call.*, and zxvd_phone.c, which serves phone.*, are not linked
 * into this test, so call.start and phone.list are served here by small
 * hosts that answer in their own shapes (api/zxvd.openrpc.json), set as
 * zxvd's people and phone ops; the requests still go through
 * zxvd_rpc_handle_line. */
#include "watchfire_model.h"

#define WF_ADA_PHONE "0123456789abcdef0123456789abcdef"

static uint32_t WF_STARTS, WF_LINE;
static long long WF_PEERS[8];

/* the number after "key": in the params, or -1 */
static long long wf_num(const char *ps, const char *key)
{
    char k[32];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(ps, k);
    if (!p) return -1;
    p += strlen(k);
    if (*p < '0' || *p > '9') return -1;
    long long v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
}

/* call.start on the next free line; member 13 is always busy */
static int wf_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    char ps[512];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (strcmp(method, "call.start")) {
        snprintf(msg, msg_cap, "not here");
        return -32601;
    }
    long long peer = wf_num(ps, "peer");
    if (peer < 1 || peer > 4294967295LL) {
        snprintf(msg, msg_cap, "peer: a member id or \"loopback\"");
        return -32602;
    }
    if (peer == 13 || WF_LINE > 3u) {
        snprintf(msg, msg_cap, "busy");
        return ZXVD_E_PEOPLE;
    }
    if (WF_STARTS < 8u) WF_PEERS[WF_STARTS] = peer;
    WF_STARTS++;
    zxvd_buf_printf(out,
                    "{\"line\":%u,\"loopback\":false,\"state\":\"ringing_out\",\"peer\":%lld,"
                    "\"outgoing\":true,\"call_id\":\"00000000000000%02u\",\"media\":\"%s\","
                    "\"end_reason\":\"none\",\"missed\":0}",
                    WF_LINE, peer, WF_STARTS % 100u,
                    strstr(ps, "\"media\":\"video\"") ? "audio+video" : "audio");
    WF_LINE++;
    return 0;
}

static const char *wf_link(bool on, int32_t port, const char *bind_ip, zxvd_buf_t *out)
{
    (void) on, (void) port, (void) bind_ip, (void) out;
    return "not here";
}

static const char *wf_pair(const char *name, bool allow_post, zxvd_buf_t *out)
{
    (void) name, (void) allow_post, (void) out;
    return "not here";
}

static const char *wf_revoke(const char *phone_id, zxvd_buf_t *out)
{
    (void) phone_id, (void) out;
    return "not here";
}

/* two paired phones: Ada's, connected once, and one no contact names */
static void wf_list(zxvd_buf_t *out)
{
    zxvd_buf_printf(out, "{\"on\":true,\"port\":47410,\"phones\":["
                         "{\"phone_id\":\"" WF_ADA_PHONE "\",\"name\":\"Ada's phone\","
                         "\"allow_post\":false,\"connected\":1},"
                         "{\"phone_id\":\"ffffffffffffffffffffffffffffffff\",\"name\":\"Spare\","
                         "\"allow_post\":false,\"connected\":0}]}");
}

static int wf_rpc(const char *method, const char *params)
{
    zxm_begin(&LINK, method);
    zxm_p_raw(&LINK, params, (uint32_t) strlen(params));
    return zxm_call(&LINK);
}

static watchfire_t WF, WF2;

APPS_TEST(watchfire)
{
    static const zxvd_people_ops_t people = {wf_people};
    static const zxvd_phone_ops_t phones = {wf_link, wf_pair, wf_list, wf_revoke};
    static char card[WATCHFIRE_LOCK_MAX + 1];
    static char big[WATCHFIRE_TEXT_MAX + 2];
    const int64_t t0 = 1700000000000LL;

    /* the rules as the model puts them together (WF1-WF3, WM2) */
    CHECK(watchfire_cancel_left(30, 0) == 30 && watchfire_cancel_left(30, 31) == 0 &&
              watchfire_cancel_left(30, -1) == WATCHFIRE_REFUSED && watchfire_ring_ok(10, 0, 0) &&
              !watchfire_ring_ok(9, 0, 0) && !watchfire_ring_ok(30, 1, 0) &&
              !watchfire_ring_ok(30, 0, 1) && watchfire_place_share_ok(1, 1, 1, 1) &&
              !watchfire_place_share_ok(1, 0, 1, 1),
          "WF1-WF3: the rules");
    watchfire_init(&WF, &LINK);
    CHECK(WF.window == WATCHFIRE_WINDOW_DEFAULT && !WF.optin && WF.state == WATCHFIRE_IDLE &&
              WF.ncontacts == 0 && WF.contact[0].line == -1,
          "init: a 30 s window, no sharing, idle");
    CHECK(strstr(WATCHFIRE_NO_SERVICES, "does not call emergency services") &&
              strstr(WATCHFIRE_NO_SERVICES, "not a medical device"),
          "the card says Watchfire calls no emergency services and is no medical device");

    /* WM1: the medical ID as typed */
    memset(big, 'x', WATCHFIRE_TEXT_MAX + 1u);
    big[WATCHFIRE_TEXT_MAX + 1u] = 0;
    CHECK(watchfire_set_medical(&WF, WATCHFIRE_MED_NAME, "Mira Okafor", t0) == ZXM_OK &&
              watchfire_set_medical(&WF, WATCHFIRE_MED_ALLERGIES,
                                    "penicillin (rash) \xe2\x80\x94 bees?", t0) == ZXM_OK &&
              watchfire_set_medical(&WF, WATCHFIRE_MED_NOTES, "Type 0 neg, I think", t0 + 1) ==
                  ZXM_OK,
          "three fields kept");
    CHECK(!strcmp(WF.med[WATCHFIRE_MED_ALLERGIES].text, "penicillin (rash) \xe2\x80\x94 bees?") &&
              !strcmp(WF.med[WATCHFIRE_MED_NOTES].text, "Type 0 neg, I think") &&
              WF.med[WATCHFIRE_MED_NOTES].stamp == t0 + 1,
          "byte for byte as typed, nothing read into it");
    CHECK(watchfire_set_medical(&WF, 0, "x", t0) == ZXM_E_ARG &&
              watchfire_set_medical(&WF, 7, "x", t0) == ZXM_E_ARG &&
              watchfire_set_medical(&WF, 2, "a\nb", t0) == ZXM_E_ARG &&
              watchfire_set_medical(&WF, 2, big, t0) == ZXM_E_ARG &&
              watchfire_set_medical(&WF, 2, "x", -1) == ZXM_E_ARG &&
              watchfire_set_medical(&WF, 2, "x", WATCHFIRE_STAMP_MAX + 1) == ZXM_E_ARG &&
              !WF.med[2].text[0],
          "medical: no such field, a control character, 256 bytes or a bad time: refused");
    big[WATCHFIRE_TEXT_MAX] = 0;
    CHECK(watchfire_set_medical(&WF, WATCHFIRE_MED_CONDITIONS, big, WATCHFIRE_STAMP_MAX) ==
                  ZXM_OK &&
              watchfire_set_medical(&WF, WATCHFIRE_MED_CONDITIONS, "", t0) == ZXM_OK &&
              !WF.med[WATCHFIRE_MED_CONDITIONS].text[0],
          "medical: 255 bytes at the latest time fit; an empty field clears it");

    /* contacts (WM1, WF5) */
    CHECK(watchfire_add_contact(&WF, "Ada", 7, WF_ADA_PHONE, t0) == ZXM_OK &&
              watchfire_add_contact(&WF, "Bo Lind", 8, "", t0) == ZXM_OK &&
              watchfire_add_contact(&WF, "Cy", 13, NULL, t0) == ZXM_OK && WF.ncontacts == 3 &&
              !strcmp(WF.contact[0].phone_id, WF_ADA_PHONE) && !WF.contact[1].phone_id[0],
          "three contacts");
    CHECK(watchfire_add_contact(&WF, "Again", 7, "", t0) == ZXM_E_STATE &&
              watchfire_add_contact(&WF, "Nobody", 0, "", t0) == ZXM_E_ARG &&
              watchfire_add_contact(&WF, "Past", 4294967296LL, "", t0) == ZXM_E_ARG &&
              watchfire_add_contact(&WF, "", 9, "", t0) == ZXM_E_ARG &&
              watchfire_add_contact(&WF, "a\tb", 9, "", t0) == ZXM_E_ARG &&
              watchfire_add_contact(&WF, "Dee", 9, "0123", t0) == ZXM_E_ARG &&
              watchfire_add_contact(&WF, "Dee", 9, "0123456789ABCDEF0123456789ABCDEF", t0) ==
                  ZXM_E_ARG &&
              WF.ncontacts == 3,
          "contacts: a second member, no member, past 2^32 - 1, no name or a bad phone: refused");
    watchfire_init(&WF2, &LINK);
    for (int64_t p = 1; p <= (int64_t) WATCHFIRE_CONTACTS; p++)
        CHECK(watchfire_add_contact(&WF2, "Friend", 100 + p, "", t0) == ZXM_OK, "friend %lld",
              (long long) p);
    CHECK(watchfire_add_contact(&WF2, "One more", 200, "", t0) == ZXM_E_STATE &&
              watchfire_remove_contact(&WF2, 105) == ZXM_OK && WF2.ncontacts == 8 &&
              WF2.contact[4].peer == 106 && watchfire_remove_contact(&WF2, 105) == ZXM_E_STATE,
          "nine at most; one removed, the rest move up");

    /* the lock-screen card */
    int32_t n = watchfire_lock_text(&WF, card, sizeof card);
    CHECK(n > 0 && (uint32_t) n == strlen(card) && strstr(card, "as its owner typed it") &&
              strstr(card, "Name: Mira Okafor\n") &&
              strstr(card, "Allergies: penicillin (rash) \xe2\x80\x94 bees?\n") &&
              strstr(card, "Emergency contacts: Ada, Bo Lind, Cy\n") &&
              strstr(card, WATCHFIRE_NO_SERVICES) && !strstr(card, "Medications"),
          "the card: %s", card);
    CHECK(watchfire_lock_text(&WF, card, 40) == ZXM_E_TOOBIG && !card[0] &&
              watchfire_lock_text(&WF, NULL, 40) == ZXM_E_ARG,
          "a card too small is refused whole");

    /* WM3: the window and the owner's opt-in */
    CHECK(watchfire_set_window(&WF, 9) == ZXM_E_ARG && watchfire_set_window(&WF, 61) == ZXM_E_ARG &&
              watchfire_set_window(&WF, 0) == ZXM_E_ARG && WF.window == 30 &&
              watchfire_set_window(&WF, 60) == ZXM_OK && watchfire_set_window(&WF, 10) == ZXM_OK &&
              watchfire_set_window(&WF, 15) == ZXM_OK && WF.window == 15,
          "window: 10 to 60 s, never 0");
    CHECK(watchfire_set_optin(&WF, true, false) == ZXM_E_STATE && !WF.optin &&
              strstr(WF.notice, "confirm"),
          "sharing does not change without the owner's confirmation");

    /* WM2: only the person starts it */
    zxvd_rpc_set_people_ops(&people);
    WF_STARTS = WF_LINE = 0;
    int before = sent;
    watchfire_t *E = &WF2;
    watchfire_init(E, &LINK);
    CHECK(watchfire_start(&WF, 1000, 0, 1) == ZXM_E_STATE &&
              watchfire_start(&WF, 1000, 1, 0) == ZXM_E_STATE &&
              watchfire_start(&WF, 1000, 1, 2) == ZXM_E_STATE &&
              watchfire_start(E, 1000, 1, 1) == ZXM_E_STATE &&
              watchfire_start(&WF, -1, 1, 1) == ZXM_E_ARG && WF.state == WATCHFIRE_IDLE &&
              sent == before,
          "not the person's own yes, or no contacts: nothing starts, nothing sent");
    CHECK(watchfire_start(&WF, 1000, 1, 1) == ZXM_OK && WF.state == WATCHFIRE_COUNTDOWN &&
              strstr(WF.notice, "in 15 s") && sent == before &&
              watchfire_start(&WF, 1001, 1, 1) == ZXM_E_STATE,
          "started: %s", WF.notice);
    CHECK(watchfire_left(&WF, 1000) == 15 && watchfire_left(&WF, 1014) == 1 &&
              watchfire_left(&WF, 1015) == 0 && watchfire_left(&WF, 5000) == 0 &&
              watchfire_left(&WF, 999) == WATCHFIRE_REFUSED,
          "the window counts down by the view's clock");
    CHECK(watchfire_set_window(&WF, 20) == ZXM_E_STATE &&
              watchfire_add_contact(&WF, "Late", 9, "", t0) == ZXM_E_STATE &&
              watchfire_remove_contact(&WF, 7) == ZXM_E_STATE &&
              watchfire_load(&WF) == ZXM_E_STATE && watchfire_finish(&WF) == ZXM_E_STATE,
          "nothing changes under a running window");
    CHECK(watchfire_ring(&WF, 1014) == ZXM_E_STATE && WF_STARTS == 0 && sent == before &&
              strstr(WF.notice, "in 1 s") && watchfire_ring(&WF, 999) == ZXM_E_ARG,
          "never skipped: a second left, nobody rings (%s)", WF.notice);
    uint32_t shared = 9;
    CHECK(watchfire_share_place(&WF, &shared) == ZXM_E_STATE && shared == 0 && sent == before,
          "nothing is shared before ringing");

    /* WM4: the window ran out */
    CHECK(watchfire_ring(&WF, 1015) == ZXM_OK && WF.state == WATCHFIRE_RINGING && WF_STARTS == 2 &&
              WF_PEERS[0] == 7 && WF_PEERS[1] == 8 && WF.rang == 2 && WF.contact[0].line == 0 &&
              WF.contact[1].line == 1 && WF.contact[2].line == -1 &&
              strstr(WF.notice, "Ringing 2 of 3 contacts (signalling only)"),
          "rung in order, Cy's line busy: %s", WF.notice);
    CHECK(watchfire_ring(&WF, 1016) == ZXM_E_STATE && WF_STARTS == 2 &&
              watchfire_cancel(&WF) == ZXM_E_STATE,
          "rung once; ringing is not cancelled here");

    /* WM5: where the machine is */
    CHECK(zpl_open(NULL) == 0, "places in memory");
    CHECK(wf_rpc("place.set_here", "{\"lat\":51507400,\"lon\":-127800,\"accuracy_m\":25}") ==
              ZXM_OK,
          "a location set: %s", LINK.err);
    CHECK(watchfire_share_place(&WF, &shared) == ZXM_OK && shared == 0 && !WF.contact[0].shared &&
              !WF.contact[0].share[0] && strstr(WF.notice, "private"),
          "not opted in: nobody is told (%s)", WF.notice);
    CHECK(watchfire_set_optin(&WF, true, true) == ZXM_OK && WF.optin, "opted in, confirmed");
    CHECK(watchfire_share_place(&WF, &shared) == ZXM_OK && shared == 0 &&
              strstr(WF.notice, "waits for your answer") &&
              strstr(LINK.resp, "\"answer\":\"pending\""),
          "opted in, no answer to the ask yet: nobody is told (%s)", WF.notice);
    CHECK(wf_rpc("place.permit", "{\"app\":\"watchfire\",\"mode\":\"always\"}") == ZXM_OK,
          "the owner allows Watchfire: %s", LINK.err);
    CHECK(watchfire_share_place(&WF, &shared) == ZXM_OK && shared == 3 && WF.contact[0].shared &&
              !strcmp(
                  WF.contact[0].share,
                  "For Ada: this machine is at 51.507400, -0.127800 (within 25 m, from owner).") &&
              strstr(WF.contact[2].share, "For Cy:") && strstr(WF.notice, "ready for 3"),
          "shared with the three listed: %s", WF.contact[0].share);
    CHECK(watchfire_set_optin(&WF, false, true) == ZXM_OK &&
              watchfire_share_place(&WF, &shared) == ZXM_OK && shared == 0 && !WF.contact[0].shared,
          "opted out again: nobody is told");

    /* place.here only says whether a location is set (WM5) */
    CHECK(watchfire_refresh_place(&WF) == ZXM_OK && WF.place_known && !WF.place_stale,
          "a fresh location is known");
    CHECK(wf_rpc("place.forget_here", "{}") == ZXM_OK && watchfire_refresh_place(&WF) == ZXM_OK &&
              !WF.place_known,
          "forgotten: not known");
    CHECK(watchfire_set_optin(&WF, true, true) == ZXM_OK &&
              watchfire_share_place(&WF, &shared) == ZXM_OK && shared == 0 &&
              strstr(WF.notice, "not shared"),
          "allowed, but no location set: nothing to share (%s)", WF.notice);
    zpl_close();

    /* a cancel inside the window: nobody is called */
    CHECK(watchfire_finish(&WF) == ZXM_OK && WF.state == WATCHFIRE_IDLE, "back to idle");
    WF_STARTS = WF_LINE = 0;
    CHECK(watchfire_start(&WF, 2000, 1, 1) == ZXM_OK && watchfire_cancel(&WF) == ZXM_OK &&
              WF.state == WATCHFIRE_CANCELLED &&
              !strcmp(WF.notice, "Cancelled. Nobody was called."),
          "cancelled in the window");
    CHECK(watchfire_ring(&WF, 2100) == ZXM_E_STATE && WF_STARTS == 0 &&
              watchfire_cancel(&WF) == ZXM_E_STATE &&
              watchfire_left(&WF, 2001) == WATCHFIRE_REFUSED,
          "a cancelled emergency never rings");
    CHECK(watchfire_finish(&WF) == ZXM_OK, "finished");

    /* without the call group, zxvd answers -32601 and nobody is marked rung */
    zxvd_rpc_set_people_ops(NULL);
    CHECK(watchfire_start(&WF, 3000, 1, 1) == ZXM_OK && watchfire_ring(&WF, 3015) == ZXM_E_REMOTE &&
              LINK.err_code == -32601 && WF.rang == 0 && WF.contact[0].line == -1,
          "no call group: -32601 (%s)", LINK.err);
    CHECK(watchfire_finish(&WF) == ZXM_OK, "finished");

    /* phone.list: which contacts' phones are paired */
    CHECK(watchfire_refresh_devices(&WF) == ZXM_E_REMOTE && LINK.err_code == -32601,
          "no phone link: -32601");
    zxvd_rpc_set_phone_ops(&phones);
    CHECK(watchfire_refresh_devices(&WF) == ZXM_OK && WF.contact[0].paired &&
              WF.contact[0].phones == 1 && !WF.contact[1].paired && !WF.contact[2].paired,
          "Ada's phone is paired and connected (%s)", LINK.err);
    zxvd_rpc_set_phone_ops(NULL);

    /* WM6: the file */
    CHECK(watchfire_save(&WF) == ZXM_OK, "saved: %s", LINK.err);
    CHECK(strstr(WF.file, "med 3 1700000000000 penicillin (rash) \xe2\x80\x94 bees?\n") &&
              strstr(WF.file, "contact 7 1700000000000 " WF_ADA_PHONE " Ada\n") &&
              strstr(WF.file, "contact 8 1700000000000 - Bo Lind\n") &&
              strstr(WF.file, "window 15\noptin 1\n"),
          "the file holds the words as typed: %s", WF.file);
    CHECK(watchfire_save(&WF) == ZXM_OK, "saved again over the first: %s", LINK.err);
    watchfire_init(&WF2, &LINK);
    CHECK(
        watchfire_load(&WF2) == ZXM_OK && WF2.ncontacts == 3 && WF2.window == 15 && WF2.optin &&
            !strcmp(WF2.contact[1].name, "Bo Lind") &&
            !strcmp(WF2.contact[0].phone_id, WF_ADA_PHONE) && WF2.contact[2].peer == 13 &&
            !strcmp(WF2.med[WATCHFIRE_MED_ALLERGIES].text, WF.med[WATCHFIRE_MED_ALLERGIES].text) &&
            WF2.med[WATCHFIRE_MED_NOTES].stamp == t0 + 1 && !WF2.med[2].text[0],
        "read back whole: %s", LINK.err);
    CHECK(wf_rpc("files.write",
                 "{\"path\":\"Watchfire/watchfire.txt\",\"text\":\"watchfire 1\\nwindow "
                 "5\\ncontact 9 1 - Zed\\n\"}") == ZXM_OK &&
              watchfire_load(&WF2) == ZXM_E_PROTOCOL && WF2.ncontacts == 3 && WF2.window == 15 &&
              strstr(WF2.notice, "damaged"),
          "a window of 5 s in the file: nothing changes");
    CHECK(wf_rpc("files.write",
                 "{\"path\":\"Watchfire/watchfire.txt\",\"text\":\"window 20\\n\"}") == ZXM_OK &&
              watchfire_load(&WF2) == ZXM_E_PROTOCOL && WF2.window == 15,
          "no header: nothing changes");
    CHECK(wf_rpc("files.write", "{\"path\":\"Watchfire/watchfire.txt\",\"text\":\"watchfire "
                                "1\\nwindow 20\\ncontact 9 1 - Zed\\n\"}") == ZXM_OK &&
              watchfire_load(&WF2) == ZXM_OK && WF2.window == 20 && WF2.ncontacts == 1 &&
              !strcmp(WF2.contact[0].name, "Zed") && !WF2.optin && !WF2.med[1].text[0],
          "a good file replaces the model whole");
}
