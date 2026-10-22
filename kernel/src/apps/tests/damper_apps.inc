/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Damper's model against zxvd's real dispatcher (LINK): log.tail is
 * zxvd's own event ring and place.here zxvd's places (zxv_place.c).
 * zxvd_people.c, which serves call.*, and zxvd_phone.c, which serves
 * phone.*, are not linked into this test, so call.start, call.hangup and
 * phone.list are served by small hosts set as zxvd's people and phone
 * ops (the requests still go through zxvd_rpc_handle_line); power.status,
 * which this in-process zxvd does not link, is answered on a link of its
 * own (DM_LINK). The building, its sensors and heat sources, the BOTH
 * test (sensor 5's smoke 1 and smoke 0 in one second: the alarm raised),
 * the way out, the battery's heat, zxvd's warnings, and the contacts rung
 * only after the person's word and the cancel window. */
#include "damper_model.h"

#define DM_ADA_PHONE "00112233445566778899aabbccddeeff"

static damper_t DP, DP2;
static zxm_link_t DM_LINK;
static uint32_t DM_STARTS, DM_HANGUPS, DM_LINE;

/* the number after "key": in the params, or -1 */
static long long dm_num(const char *ps, const char *key)
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

/* call.start on the next free line, call.hangup of a line in a call */
static int dm_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    char ps[512];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (!strcmp(method, "call.hangup")) {
        long long line = dm_num(ps, "line");
        if (line < 0 || line >= (long long) DM_LINE) {
            snprintf(msg, msg_cap, "line %lld has no call", line);
            return ZXVD_E_PEOPLE;
        }
        DM_HANGUPS++;
        zxvd_buf_printf(out,
                        "{\"line\":%lld,\"loopback\":false,\"state\":\"ended\",\"peer\":7,"
                        "\"outgoing\":true,\"call_id\":\"0000000000000001\",\"media\":\"audio\","
                        "\"end_reason\":\"hangup\",\"missed\":0}",
                        line);
        return 0;
    }
    if (strcmp(method, "call.start")) {
        snprintf(msg, msg_cap, "not here");
        return -32601;
    }
    long long peer = dm_num(ps, "peer");
    if (peer < 1 || peer > 4294967295LL || DM_LINE > 3u) {
        snprintf(msg, msg_cap, "busy");
        return ZXVD_E_PEOPLE;
    }
    DM_STARTS++;
    zxvd_buf_printf(out,
                    "{\"line\":%u,\"loopback\":false,\"state\":\"ringing_out\",\"peer\":%lld,"
                    "\"outgoing\":true,\"call_id\":\"00000000000000%02u\",\"media\":\"audio\","
                    "\"end_reason\":\"none\",\"missed\":0}",
                    DM_LINE, peer, DM_STARTS % 100u);
    DM_LINE++;
    return 0;
}

static const char *dm_link(bool on, int32_t port, const char *bind_ip, zxvd_buf_t *out)
{
    (void) on, (void) port, (void) bind_ip, (void) out;
    return "not here";
}

static const char *dm_pair(const char *name, bool allow_post, zxvd_buf_t *out)
{
    (void) name, (void) allow_post, (void) out;
    return "not here";
}

static const char *dm_revoke(const char *phone_id, zxvd_buf_t *out)
{
    (void) phone_id, (void) out;
    return "not here";
}

static void dm_list(zxvd_buf_t *out)
{
    zxvd_buf_printf(out, "{\"on\":true,\"port\":47410,\"phones\":[{\"phone_id\":\"" DM_ADA_PHONE
                         "\",\"name\":\"Ada's phone\",\"allow_post\":false,\"connected\":1}]}");
}

/* sys.info and power.status, each with its own reply: a laptop on battery at 46.2 C */
static int32_t dm_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    (void) len;
    if (strstr(req, "\"sys.info\""))
        return snprintf(
            resp, cap,
            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1700000000000,"
            "\"os\":\"linux\",\"os_version\":\"6.1\",\"arch\":\"x86_64\",\"host\":\"h\","
            "\"cpus\":4,\"mem_total\":8000,\"mem_avail\":2000,\"uptime_s\":5,"
            "\"load\":[1,2,3],\"zxvd\":{\"pid\":7,\"cpu_user_us\":0,\"cpu_sys_us\":0,"
            "\"rss_bytes\":1,\"max_rss_bytes\":2}}}",
            l->seq);
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"device\":\"laptop\",\"host\":"
                    "\"linux\",\"interval_ms\":1000,\"total_uj\":14090000,\"total_mw\":14090,"
                    "\"source\":\"metered\",\"lower_bound\":false,\"lifetime_uj\":1,\"posture\":"
                    "\"balanced\",\"token_scale_permille\":618,\"battery\":{\"on_mains\":false,"
                    "\"charging\":false,\"permille\":714,\"now_mwh\":35700,\"full_mwh\":50000,"
                    "\"design_mwh\":55000,\"health_permille\":909,\"rate_mw\":14090,\"cycles\":312,"
                    "\"minutes_to_empty\":0,\"minutes_to_full\":0,\"temp_dc\":462},\"meters\":[],"
                    "\"consumers\":[]}}",
                    l->seq);
}

APPS_TEST(damper)
{
    static const zxvd_people_ops_t people = {dm_people};
    static const zxvd_phone_ops_t phones = {dm_link, dm_pair, dm_list, dm_revoke};
    rat_t q = rat_zero();
    int64_t t3[3] = {200, 250, 300}, s3[3] = {0, 30, 60}, path[DAMPER_ROOMS], wh = 0;
    uint32_t n = 0, through = 0, added = 0;

    /* the rules, as the blocks run them */
    CHECK(damper_quiet(0, 0, 0) && damper_quiet(DAMPER_MARGIN_MAX, 0, 0) &&
              !damper_quiet(-1, 0, 0) && !damper_quiet(5, 1, 0) && !damper_quiet(5, 0, 1) &&
              !damper_quiet(DAMPER_MARGIN_MAX + 1, 0, 0),
          "damper: DR1, a sensor at its limit is quiet, a tenth over, smoke or BOTH is not");
    CHECK(damper_rise_excess(t3, s3, 3, 83, &q) && q.num == 17 && q.den == 1 &&
              !damper_rise_excess(t3, s3, 1, 83, &q) && !damper_rise_excess(t3, s3, 3, 9, &q),
          "damper: DR2, 10.0 C a minute is 1.7 C past a declared 8.3");
    CHECK(damper_limits_ok(570, 83) && !damper_limits_ok(399, 83) && !damper_limits_ok(570, 301),
          "damper: DR3, the limits");

    /* DM1: the building, only on the owner's word */
    damper_init(&DP, &LINK);
    int before = sent;
    CHECK(DP.flash == 3 && DP.vibrate == 3 && DP.fire == LPRES_STATE_NEITHER && !DP.raised,
          "damper: init, three flashes and three pulses, no alarm");
    CHECK(damper_add_room(&DP, 0, 3, 0, 2, false) == ZXM_E_STATE && DP.nroom == 0 &&
              strstr(DP.notice, "confirm") && sent == before,
          "damper: a room without the owner's word: nothing kept, nothing sent");
    CHECK(damper_add_room(&DP, 0, 3, 0, 0, true) == ZXM_E_ARG &&
              damper_add_room(&DP, 0, 3, 9, 1, true) == ZXM_E_ARG &&
              damper_add_room(&DP, 0, 1000, 0, 1, true) == ZXM_E_ARG &&
              damper_add_room(&DP, 100, 3, 0, 1, true) == ZXM_E_ARG &&
              damper_add_room(&DP, 0, 3, 0, 10, true) == ZXM_E_ARG && DP.nroom == 0,
          "damper: no door out, a way to no room, past the record's digits: refused");
    CHECK(damper_add_room(&DP, 0, 3, 0, 2, true) == ZXM_OK &&
              damper_add_room(&DP, 0, 7, 3, 0, true) == ZXM_OK &&
              damper_add_room(&DP, 1, 12, 7, 0, true) == ZXM_OK &&
              damper_add_room(&DP, 1, 12, 3, 0, true) == ZXM_E_ARG && DP.nroom == 3,
          "damper: the hall, the kitchen and a bedroom upstairs; a room twice refused");

    /* DM2, DM6: sensors and heat sources */
    CHECK(damper_add_sensor(&DP, 5, 12, 570, 83, false) == ZXM_E_STATE &&
              damper_add_sensor(&DP, 5, 13, 570, 83, true) == ZXM_E_ARG &&
              damper_add_sensor(&DP, 5, 12, 399, 83, true) == ZXM_E_ARG &&
              damper_add_sensor(&DP, 0, 12, 570, 83, true) == ZXM_E_ARG && DP.nsensor == 0,
          "damper: a sensor without the word, in no room, or with its limit past DR3: refused");
    CHECK(damper_add_sensor(&DP, 5, 12, 570, 83, true) == ZXM_OK &&
              damper_add_sensor(&DP, 2, 7, 570, 83, true) == ZXM_OK &&
              damper_add_sensor(&DP, 9, 3, 570, 83, true) == ZXM_OK && DP.nsensor == 3 &&
              damper_find_sensor(&DP, 2) == 1 && damper_find_sensor(&DP, 4) == -1,
          "damper: three sensors");
    CHECK(damper_add_source(&DP, 41, HOMESTEAD_HEATER, 50, 300, 200, 12, false) == ZXM_E_STATE &&
              damper_add_source(&DP, 41, HOMESTEAD_LIGHT, 0, 100, 50, 12, true) == ZXM_E_ARG &&
              damper_add_source(&DP, 41, HOMESTEAD_HEATER, 50, 300, 200, 99, true) == ZXM_E_ARG &&
              damper_add_source(&DP, 41, HOMESTEAD_HEATER, 300, 50, 200, 12, true) == ZXM_E_ARG &&
              damper_add_source(&DP, 41, HOMESTEAD_HEATER, 50, 300, 200, 12, true) == ZXM_OK &&
              damper_add_source(&DP, 42, HOMESTEAD_OVEN, 0, 2500, 0, 7, true) == ZXM_OK &&
              DP.nsource == 2,
          "damper: a heater upstairs and the oven; a light is no heat source");
    CHECK(damper_energy(&DP, 41, 400) == ZXM_OK && damper_energy(&DP, 42, 7) == ZXM_OK &&
              damper_energy(&DP, 41, 12) == ZXM_OK && damper_energy(&DP, 43, 1) == ZXM_E_ARG &&
              damper_energy(&DP, 41, -1) == ZXM_E_ARG &&
              damper_source_energy(&DP, 41, &wh) == ZXM_OK && wh == 412,
          "damper: the heater's energy, 412 Wh (homestead.device_energy)");

    /* DM4: the alarm's pattern and tone, on the owner's word (Conch's rules) */
    CHECK(damper_set_alert(&DP, 2, 1, false) == ZXM_E_STATE &&
              damper_set_alert(&DP, 4, 1, true) == ZXM_E_ARG && DP.flash == 3 &&
              damper_set_alert(&DP, 2, 1, true) == ZXM_OK && DP.flash == 2 && DP.vibrate == 1,
          "damper: four flashes a burst refused; two set");
    CHECK(damper_set_tone(&DP, 0, 850, 900, false) == ZXM_E_STATE &&
              damper_set_tone(&DP, 0, 901, 900, true) == ZXM_E_ARG && !DP.has_tone &&
              damper_set_tone(&DP, 0, 850, 900, true) == ZXM_OK && DP.has_tone && DP.total == 850,
          "damper: a tone past the owner's cap refused");

    /* DM3, DM4: no reading is no alarm */
    CHECK(damper_assess(&DP) == ZXM_OK && !DP.raised && DP.fire == LPRES_STATE_NEITHER &&
              strstr(DP.notice, "No alarm") && strstr(DP.notice, "not a certified smoke alarm"),
          "damper: nothing read yet: %s", DP.notice);
    CHECK(damper_reading(&DP, 4, 1000, 210, 0) == ZXM_E_ARG &&
              damper_reading(&DP, 5, 1000, 1501, 0) == ZXM_E_ARG &&
              damper_reading(&DP, 5, 1000, 210, 2) == ZXM_E_ARG &&
              damper_reading(&DP, 5, -1, 210, 0) == ZXM_E_ARG,
          "damper: no sensor, past 150.0 C, smoke 2 or a stamp before 1970: refused");

    /* the BOTH test: sensor 5 reports smoke 1 and smoke 0 in the same second */
    CHECK(damper_reading(&DP, 5, 1000, 210, 1) == ZXM_OK &&
              damper_reading(&DP, 5, 1000, 210, 0) == ZXM_OK &&
              damper_reading(&DP, 2, 1000, 220, 0) == ZXM_OK &&
              damper_reading(&DP, 9, 1000, 205, DAMPER_SMOKE_NONE) == ZXM_OK &&
              damper_reading(&DP, 5, 999, 210, 0) == ZXM_E_ARG,
          "damper: readings in; one before the last refused");
    CHECK(damper_assess(&DP) == ZXM_OK && DP.raised && DP.raised_n == 1 &&
              DP.sensor[0].fire == LPRES_STATE_BOTH && DP.sensor[0].split == 1 &&
              DP.sensor[0].has_smoke == 1 && DP.sensor[0].margin == 360 && !DP.sensor[0].quiet &&
              DP.fire == LPRES_STATE_BOTH && DP.sensor[1].quiet &&
              DP.sensor[1].fire == LPRES_STATE_FALSE && DP.sensor[2].quiet &&
              DP.sensor[0].room_raised == 1 && DP.sensor[1].room_raised == 0,
          "damper: sensor 5's reading is BOTH and the alarm is still raised");
    CHECK(strstr(DP.notice, "Fire alarm (advice): 1 of 3 sensors raised.") &&
              strstr(DP.notice, "Sensor 5, room 12, floor 1: two reports of one second disagree "
                                "(held as BOTH).") &&
              strstr(DP.notice, "Way out: room 12, room 7, room 3, outside.") &&
              strstr(DP.notice, "Device 41 here used 412 Wh") &&
              strstr(DP.notice, "2 flashes and 1 vibrations") &&
              strstr(DP.notice, "calls nobody by itself"),
          "damper: the notice: %s", DP.notice);
    CHECK(damper_way_out(&DP, 12, path, DAMPER_ROOMS, &n, &through) == ZXM_OK && n == 3 &&
              path[0] == 12 && path[1] == 7 && path[2] == 3 && through == 1 &&
              damper_way_out(&DP, 12, path, 2, &n, &through) == ZXM_E_TOOBIG &&
              damper_way_out(&DP, 99, path, DAMPER_ROOMS, &n, &through) == ZXM_E_ARG,
          "damper: the way out from the bedroom, through its own raised room");

    /* the next second agrees with itself: quiet again; sensor 2 rises 11.0 C a minute */
    CHECK(damper_reading(&DP, 5, 1001, 210, 0) == ZXM_OK &&
              damper_reading(&DP, 2, 1060, 330, 0) == ZXM_OK && damper_assess(&DP) == ZXM_OK &&
              !DP.raised && DP.fire == LPRES_STATE_FALSE && DP.rising_n == 1 &&
              DP.sensor[1].rising && !DP.sensor[0].rising &&
              strstr(DP.notice, "1 rising faster than their declared rate"),
          "damper: quiet, one sensor rising (advice): %s", DP.notice);
    /* over the limit by a tenth */
    CHECK(damper_reading(&DP, 9, 1100, 571, DAMPER_SMOKE_NONE) == ZXM_OK &&
              damper_assess(&DP) == ZXM_OK && DP.raised && DP.fire == LPRES_STATE_TRUE &&
              DP.sensor[2].margin == -1 && strstr(DP.notice, "over its temperature limit") &&
              strstr(DP.notice, "Way out: room 3, outside."),
          "damper: the hall at 57.1 C: %s", DP.notice);
    /* the window slides: eight readings kept */
    for (int64_t k = 0; k < 10; k++)
        CHECK(damper_reading(&DP, 9, 1200 + k, 200, 0) == ZXM_OK, "damper: reading %lld",
              (long long) k);
    CHECK(DP.sensor[2].n == DAMPER_WINDOW && DP.sensor[2].stamp[0] == 1202 &&
              DP.sensor[2].stamp[DAMPER_WINDOW - 1u] == 1209 && damper_assess(&DP) == ZXM_OK &&
              !DP.raised,
          "damper: the oldest readings left the window, the hall is quiet again");
    /* the energy list is bounded: full is refused, never overwritten */
    while (DP.nen < DAMPER_ENERGY) CHECK(damper_energy(&DP, 42, 1) == ZXM_OK, "damper: energy");
    CHECK(damper_energy(&DP, 42, 1) == ZXM_E_STATE && DP.nen == DAMPER_ENERGY &&
              damper_source_energy(&DP, 41, &wh) == ZXM_OK && wh == 412,
          "damper: a full energy list refuses the next reading");

    /* DM7: zxvd's warnings, through Wake */
    zxvd_log_reset();
    zxvd_log(ZXVD_LOG_INFO, "test", "boot");
    zxvd_log(ZXVD_LOG_WARN, "power", "battery warm");
    zxvd_log(ZXVD_LOG_WARN, "power", "battery hot");
    zxvd_log(ZXVD_LOG_ERROR, "net", "link down");
    CHECK(damper_follow(&DP, &added) == ZXM_OK && added == 4 && DP.warnings == 3,
          "damper: four events, three warnings and errors (wake.count): %u", DP.warnings);
    CHECK(damper_history(&DP, 0) == ZXM_OK && strstr(LINK.req, "\"journal.query\""),
          "damper: the journal asked (journal.query): %s", LINK.err);
    /* DM7: the battery, on a link of its own (power.status is not linked here) */
    CHECK(damper_refresh_power(&DP) == ZXM_E_REMOTE, "damper: no power group here");
    zxm_link_init(&DM_LINK, dm_send, &DM_LINK);
    damper_init(&DP2, &DM_LINK);
    CHECK(damper_refresh_power(&DP2) == ZXM_OK && DP2.power.bat.temp_dc == 462 &&
              DP2.power.bat.heat == BRAZIER_HEAT_WARN && DP2.power.bat.minutes_left == 152,
          "damper: the battery at 46.2 C warns (brazier.heat_ok), 152 minutes left: %s",
          DM_LINK.err);

    /* DM8: the contacts, on the owner's word; rung only after the person's yes and the window */
    zxvd_rpc_set_people_ops(&people);
    DM_STARTS = DM_HANGUPS = DM_LINE = 0;
    before = sent;
    CHECK(damper_add_contact(&DP, "Ada", 7, DM_ADA_PHONE, 1700000000000LL, false) == ZXM_E_STATE &&
              DP.contacts.ncontacts == 0 &&
              damper_add_contact(&DP, "Ada", 0, "", 1700000000000LL, true) == ZXM_E_ARG &&
              damper_add_contact(&DP, "Ada", 7, DM_ADA_PHONE, 1700000000000LL, true) == ZXM_OK &&
              damper_add_contact(&DP, "Bo", 8, "", 1700000000000LL, true) == ZXM_OK &&
              DP.contacts.ncontacts == 2 && sent == before,
          "damper: two evacuation contacts (watchfire.sos_contact_ok)");
    CHECK(damper_set_window(&DP, 10, false) == ZXM_E_STATE && DP.contacts.window == 30 &&
              damper_set_window(&DP, 0, true) == ZXM_E_ARG &&
              damper_set_window(&DP, 10, true) == ZXM_OK && DP.contacts.window == 10 &&
              damper_set_share(&DP, true, false) == ZXM_E_STATE && !DP.contacts.optin,
          "damper: the window never 0, set and shared only on the owner's word");
    CHECK(damper_call_contacts(&DP, 1000, 1, 0) == ZXM_E_STATE &&
              damper_call_contacts(&DP, 1000, 0, 1) == ZXM_E_STATE && DM_STARTS == 0 &&
              sent == before,
          "damper: not the person's own yes: nothing starts, nothing sent");
    CHECK(damper_call_contacts(&DP, 1000, 1, 1) == ZXM_OK && damper_call_left(&DP, 1004) == 6 &&
              damper_ring(&DP, 1009) == ZXM_E_STATE && DM_STARTS == 0 && sent == before &&
              strstr(DP.notice, "in 1 s"),
          "damper: the cancel window is never skipped: %s", DP.notice);
    CHECK(damper_stand_down(&DP, true) == ZXM_E_STATE && sent == before,
          "damper: nothing to stand down inside the window");
    CHECK(damper_ring(&DP, 1010) == ZXM_OK && DM_STARTS == 2 &&
              DP.contacts.state == WATCHFIRE_RINGING && DP.contacts.contact[1].line == 1 &&
              strstr(DP.notice, "Ringing 2 of 2 contacts"),
          "damper: the window ran out: both rung (call.start): %s", DP.notice);
    before = sent;
    CHECK(damper_stand_down(&DP, false) == ZXM_E_STATE && DM_HANGUPS == 0 && sent == before &&
              DP.contacts.state == WATCHFIRE_RINGING && DP.contacts.contact[0].line == 0,
          "damper: standing down without the owner's word: nothing sent, the calls stay");
    CHECK(damper_stand_down(&DP, true) == ZXM_OK && DM_HANGUPS == 2 && sent == before + 2 &&
              DP.contacts.state == WATCHFIRE_IDLE && DP.contacts.contact[0].line == -1 &&
              DP.contacts.contact[1].line == -1 && strstr(DP.notice, "Stood down"),
          "damper: stood down (call.hangup on each line): %s", DP.notice);
    CHECK(damper_call_contacts(&DP, 2000, 1, 1) == ZXM_OK && damper_cancel_call(&DP) == ZXM_OK &&
              damper_ring(&DP, 2100) == ZXM_E_STATE && DM_STARTS == 2 &&
              strstr(DP.notice, "Nobody was called") && damper_stand_down(&DP, true) == ZXM_OK &&
              DM_HANGUPS == 2 && DP.contacts.state == WATCHFIRE_IDLE,
          "damper: cancelled in the window: nobody rung, nothing to hang up");
    uint32_t shared = 9;
    CHECK(damper_share_place(&DP, &shared) == ZXM_E_STATE && shared == 0,
          "damper: where the machine is stays private while nobody is rung");
    zxvd_rpc_set_people_ops(NULL);

    /* DM8: phone.list and place.here, read only */
    zxvd_rpc_set_phone_ops(&phones);
    CHECK(zpl_open(NULL) == 0, "damper: places in memory");
    CHECK(damper_refresh_contacts(&DP) == ZXM_OK && DP.contacts.contact[0].paired &&
              !DP.contacts.contact[1].paired && !DP.contacts.place_known,
          "damper: Ada's phone paired (phone.list), no location set (place.here): %s", LINK.err);
    zpl_close();
    zxvd_rpc_set_phone_ops(NULL);
}
