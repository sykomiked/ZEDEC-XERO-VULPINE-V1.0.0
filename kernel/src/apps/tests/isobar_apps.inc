/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Isobar's model against zxvd's real dispatcher: the network is a file in
 * the Hoard (files.read, files.write) and the stations' places are the
 * owner's places (place.here, place.list, place.add), all real. This
 * in-process zxvd links no device mesh and no peer network, so
 * devmesh.devices and net.peers are answered on a link of its own
 * (IB_LINK), which passes every other request to the real dispatcher. */
#include "isobar_model.h"

static isobar_t IB, IB2;
static zxm_link_t IB_LINK;
static int ib_sent; /* requests IB_LINK answered itself */
static const char *IB_DEVICES, *IB_PEERS;

static int32_t ib_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"devmesh.devices\"")) r = IB_DEVICES;
    if (strstr(req, "\"net.peers\"")) r = IB_PEERS;
    if (!r) return inproc(NULL, req, len, resp, cap);
    ib_sent++;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define IB_NODE_A "0123456789abcdef0123456789abcdef"
#define IB_NODE_B "fedcba9876543210fedcba9876543210"
#define IB_DEV(id, status, up)                                                                     \
    "{\"id\":\"" id "\",\"name\":\"mast\",\"role\":\"thin\",\"admin\":false,\"held\":false,"       \
    "\"status\":\"" status "\",\"self\":false,\"up\":" up ",\"since\":\"2\"}"
#define IB_T0 INT64_C(1792627200) /* a whole hour: 497952 hours since 1970 */

static bool ib_place(const char *name, int64_t lat, int64_t lon)
{
    zxm_begin(&IB_LINK, "place.add");
    zxm_p_str(&IB_LINK, "name", name);
    zxm_p_i64(&IB_LINK, "lat", lat);
    zxm_p_i64(&IB_LINK, "lon", lon);
    return zxm_call(&IB_LINK) == ZXM_OK;
}

static bool ib_file_has(const char *want)
{
    static uint8_t buf[ISOBAR_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(ISOBAR_FILE, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && strstr((const char *) buf, want) != NULL;
}

APPS_TEST(isobar)
{
    int64_t code = 0, high = 0, total = 0, week[3] = {0, 0, 0}, apart = 0, active = 9, revoked = 9;
    int32_t flags = -1;
    uint32_t dropped = 0;
    isobar_micro_t mc;

    /* the rules */
    CHECK(isobar_pressure_outlook(-30, 10102) == ISOBAR_OUT_RAIN &&
              isobar_pressure_outlook(0, 10250) == ISOBAR_OUT_FAIR &&
              isobar_pressure_outlook(501, 10132) == ISOBAR_REFUSED,
          "IR2: the table and its domain");

    CHECK(zxf_use_space(NULL) == 0, "home");
    CHECK(zpl_open(NULL) == 0, "places in memory");
    zxm_link_init(&IB_LINK, ib_send, &IB_LINK);

    /* nothing is written before the file is read */
    isobar_init(&IB, &IB_LINK);
    int before = sent;
    CHECK(isobar_add_site(&IB, 1, 0, 1, 0, "Valley") == ZXM_E_STATE &&
              isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0, 18) == ZXM_E_STATE && sent == before,
          "not loaded: nothing written");
    CHECK(isobar_add_site(&IB, 0, 0, 1, 0, "x") == ZXM_E_ARG &&
              isobar_add_site(&IB, 1, 0, 1, 0, "") == ZXM_E_ARG &&
              isobar_add_site(&IB, 1, 0, 1, 100000, "x") == ZXM_E_ARG &&
              isobar_set_sensor(&IB, 1, 6, 0, 1, 0) == ZXM_E_ARG &&
              isobar_set_sensor(&IB, 1, ISOBAR_TEMP, 5, 4, 0) == ZXM_E_ARG &&
              isobar_set_sensor(&IB, 1, ISOBAR_TEMP, -500000, 500000, 0) == ZXM_E_ARG &&
              isobar_set_sensor(&IB, 1, ISOBAR_TEMP, 0, 10, ISOBAR_STEP_MAX + 1) == ZXM_E_ARG &&
              isobar_set_heater(&IB, 1, 0, 10, 11) == ZXM_E_ARG && sent == before,
          "bad records are refused before anything is asked");

    /* a network not made yet is empty */
    CHECK(isobar_load(&IB) == ZXM_OK && IB.loaded && !IB.nsite && !IB.nst && !IB.nobs,
          "no file yet: %s", IB_LINK.err);

    /* IM2-IM5: the site tree, the stations, their sensors and a heater */
    CHECK(isobar_add_site(&IB, 1, 0, 1, 0, "Valley network") == ZXM_OK &&
              isobar_add_site(&IB, 2, 1, 2, 120, "Orchard") == ZXM_OK && IB.nsite == 2,
          "sites: %s", IB_LINK.err);
    CHECK(isobar_add_site(&IB, 3, 9, 2, 0, "Nowhere") == ZXM_E_ARG &&
              isobar_add_site(&IB, 3, 1, 1, 0, "Second root") == ZXM_E_ARG &&
              isobar_add_site(&IB, 2, 1, 2, 0, "Again") == ZXM_E_ARG,
          "a site under no kept node, a network with a parent, an id kept");
    CHECK(isobar_add_station(&IB, 2, 2, NULL, "South hollow") == ZXM_OK &&
              isobar_add_station(&IB, 1, 2, IB_NODE_A, "North gate") == ZXM_OK && IB.nst == 2 &&
              IB.st[0].id == 1 && !strcmp(IB.st[0].node, IB_NODE_A),
          "stations, ids rising: %s", IB_LINK.err);
    CHECK(isobar_add_station(&IB, 3, 1, NULL, "On the network") == ZXM_E_ARG &&
              isobar_add_station(&IB, 3, 2, "ABC", "Bad node") == ZXM_E_ARG &&
              isobar_add_station(&IB, 1, 2, NULL, "Again") == ZXM_E_ARG,
          "a station on a network, a node that is no device id, an id kept");
    CHECK(isobar_set_sensor(&IB, 1, ISOBAR_TEMP, -400, 500, 50) == ZXM_OK &&
              isobar_set_sensor(&IB, 2, ISOBAR_TEMP, -400, 500, 50) == ZXM_OK &&
              isobar_set_sensor(&IB, 1, ISOBAR_RAIN, 0, 500, 500) == ZXM_OK &&
              isobar_set_sensor(&IB, 1, ISOBAR_PRESSURE, 9000, 10800, 30) == ZXM_OK &&
              isobar_set_sensor(&IB, 1, ISOBAR_WIND, 0, 600, 200) == ZXM_OK &&
              isobar_set_sensor(&IB, 1, ISOBAR_WIND, 0, 600, 250) == ZXM_OK && IB.nsensor == 5 &&
              IB.sensor[1].kind == ISOBAR_PRESSURE && IB.sensor[2].step == 250,
          "sensors in order, one replaced: %s", IB_LINK.err);
    CHECK(isobar_set_sensor(&IB, 9, ISOBAR_TEMP, 0, 1, 0) == ZXM_E_ARG, "no such station");
    CHECK(isobar_set_heater(&IB, 1, -50, 100, 30) == ZXM_OK &&
              strstr(IB.notice, "changes only in Homestead, with the owner's approval") &&
              isobar_set_heater(&IB, 9, -50, 100, 30) == ZXM_E_ARG &&
              isobar_set_heater(&IB, 1, -50, 100, 101) == ZXM_E_ARG,
          "a heater kept as Homestead's record: %s", IB.notice);

    /* IM7: readings, accepted or kept as refused */
    CHECK(isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0, 18) == ZXM_OK &&
              isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 600, 21) == ZXM_OK &&
              strstr(IB.notice, "North gate: air temperature 2.1 C accepted."),
          "accepted: %s", IB.notice);
    CHECK(isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1200, 90) == ISOBAR_REFUSED_READING &&
              strstr(IB.notice, "9.0 C refused: it moved more than the sensor's step limit"),
          "a jump past the step: %s", IB.notice);
    CHECK(isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1300, 600) == ISOBAR_REFUSED_READING &&
              strstr(IB.notice, "above the sensor's range") &&
              isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1400, -500) == ISOBAR_REFUSED_READING &&
              strstr(IB.notice, "-50.0 C refused: below the sensor's range"),
          "outside the range: %s", IB.notice);
    CHECK(isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1400, 20) == ZXM_E_ARG &&
              isobar_take(&IB, 1, ISOBAR_HUMIDITY, IB_T0, 500) == ZXM_E_ARG &&
              isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1500, ISOBAR_RECORD_MAX + 1) == ZXM_E_ARG &&
              isobar_take(&IB, 3, ISOBAR_TEMP, IB_T0, 1) == ZXM_E_ARG,
          "a stamp not after the last, a sensor not kept, a reading no record holds");
    CHECK(isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1500, 20) == ZXM_OK && IB.nobs == 6 &&
              IB.obs[2].state == 1 && IB.obs[5].state == 0,
          "refused readings kept, and the step counted from the last accepted (2.1 C)");

    /* IR3 and IM10: the grid's cell, a range of hours' rain, the week */
    CHECK(isobar_hour_high(&IB, 1, ISOBAR_TEMP, IB_T0 / 3600, &high) == ZXM_OK && high == 21 &&
              isobar_hour_high(&IB, 1, ISOBAR_TEMP, IB_T0 / 3600 + 1, &high) == ZXM_E_STATE &&
              isobar_hour_high(&IB, 2, ISOBAR_PRESSURE, IB_T0 / 3600, &high) == ZXM_E_ARG,
          "the hour's high is 2.1 C, never the refused 60.0 C");
    CHECK(isobar_take(&IB, 1, ISOBAR_RAIN, IB_T0, 12) == ZXM_OK &&
              isobar_take(&IB, 1, ISOBAR_RAIN, IB_T0 + 3600, 30) == ZXM_OK &&
              isobar_take(&IB, 1, ISOBAR_RAIN, IB_T0 + 7200, 5) == ZXM_OK &&
              isobar_rain_total(&IB, 1, IB_T0 / 3600, IB_T0 / 3600 + 1, &total) == ZXM_OK &&
              total == 42 &&
              isobar_rain_total(&IB, 1, IB_T0 / 3600, IB_T0 / 3600 + 2, &total) == ZXM_OK &&
              total == 47 && isobar_rain_total(&IB, 1, 5, 4, &total) == ZXM_E_ARG,
          "rain over a range of hours (furrow.range_total): %s", IB_LINK.err);
    CHECK(isobar_week(&IB, 1, IB_T0 + 3600, week) == ZXM_OK && week[0] == 18 && week[1] == 21 &&
              week[2] == 19 && isobar_week(&IB, 2, IB_T0, week) == ZXM_E_STATE,
          "the week (weatherglass.week_temps): %lld %lld %lld", (long long) week[0],
          (long long) week[1], (long long) week[2]);
    CHECK(isobar_take(&IB, 1, ISOBAR_WIND, IB_T0 + 1500, 180) == ZXM_OK &&
              isobar_warnings(&IB, 1, IB_T0 + 1600, &flags) == ZXM_OK && flags == 4 &&
              strstr(IB.notice, "North gate: 2.0 C (35.6 F). Advisory: gale. Official warnings "
                                "always take precedence."),
          "warnings (weatherglass.warn_flags): %s", IB.notice);

    /* IM8: the outlook */
    CHECK(isobar_take(&IB, 1, ISOBAR_PRESSURE, IB_T0, 10132) == ZXM_OK &&
              isobar_outlook(&IB, 1, IB_T0 + 600, &code) == ZXM_E_STATE,
          "no pressure three hours back: no outlook");
    CHECK(isobar_take(&IB, 1, ISOBAR_PRESSURE, IB_T0 + 10800, 10102) == ZXM_OK &&
              isobar_outlook(&IB, 1, IB_T0 + 10800, &code) == ZXM_OK && code == ISOBAR_OUT_RAIN &&
              strstr(IB.notice, "North gate: pressure 1010.2 hPa, down 3.0 hPa in 3 hours: rain "
                                "likely. Advisory only") &&
              strstr(IB.notice, "official warnings always take precedence"),
          "a fall of 3.0 hPa: rain likely: %s", IB.notice);
    CHECK(isobar_outlook(&IB, 1, IB_T0 + 10800 + 3 * ISOBAR_TEND_SLACK, &code) == ZXM_E_STATE,
          "an old pressure gives no outlook");

    /* IM9, the BOTH test: two stations of one site 400 m apart read 2.1 C
     * and 7.4 C, both within range: a microclimate, both kept */
    CHECK(ib_place("North gate", 59913900, 10752200) &&
              ib_place("South hollow", 59910302, 10752200),
          "places: %s", IB_LINK.err);
    CHECK(isobar_take(&IB, 2, ISOBAR_TEMP, IB_T0 + 600, 70) == ZXM_OK &&
              isobar_take(&IB, 2, ISOBAR_TEMP, IB_T0 + 1200, 74) == ZXM_OK,
          "the hollow's readings");
    CHECK(isobar_microclimate(&IB, 1, 2, IB_T0 + 1300, &mc) == ZXM_E_STATE,
          "not placed yet: no distance");
    CHECK(isobar_places(&IB) == ZXM_OK && IB.st[0].placed && IB.st[1].placed && !IB.here_known &&
              isobar_apart(&IB, 1, 2, &apart) == ZXM_OK && apart == 400,
          "400 m apart: %lld (%s)", (long long) apart, IB_LINK.err);
    CHECK(isobar_take(&IB, 1, ISOBAR_TEMP, IB_T0 + 1550, 21) == ZXM_OK &&
              isobar_microclimate(&IB, 1, 2, IB_T0 + 1600, &mc) == ZXM_OK &&
              mc.state == LPRES_STATE_BOTH && mc.state == 3 && mc.a_reading == 21 &&
              mc.b_reading == 74 && mc.apart_m == 400 &&
              strstr(IB.notice, "Microclimate at Orchard: North gate reads 2.1 C and South "
                                "hollow 7.4 C, 400 m apart. Both readings are kept as they are, "
                                "never averaged."),
          "2.1 C and 7.4 C: BOTH, both kept: %s", IB.notice);
    CHECK(isobar_microclimate(&IB, 1, 2, IB_T0 + 1600 + 2 * 3600, &mc) == ZXM_OK &&
              mc.state == LPRES_STATE_NEITHER,
          "stale readings say nothing about now: NEITHER");
    CHECK(isobar_take(&IB, 2, ISOBAR_TEMP, IB_T0 + 1700, 28) == ZXM_OK &&
              isobar_microclimate(&IB, 1, 2, IB_T0 + 1800, &mc) == ZXM_OK &&
              mc.state == LPRES_STATE_TRUE && strstr(IB.notice, "Frost risk at Orchard"),
          "both at or below 3.0 C: TRUE: %s", IB.notice);
    CHECK(isobar_microclimate(&IB, 1, 1, IB_T0, &mc) == ZXM_E_ARG, "a station with itself");
    CHECK(zpl_set_here((zpl_point_t){59913900, 10752200}, 10, "owner") == 0 &&
              isobar_places(&IB) == ZXM_OK && IB.here_known && IB.here_lat == 59913900,
          "here: %s", IB_LINK.err);

    /* station nodes: devmesh.devices and net.peers */
    IB_DEVICES = "{\"roster_version\":\"4\",\"devices\":[" IB_DEV(
        IB_NODE_A, "active", "true") "," IB_DEV(IB_NODE_B, "revoked", "false") "]}";
    IB_PEERS = "{\"peers\":[{\"id\":\"aa\"},{\"id\":\"bb\"}],\"count\":2}";
    ib_sent = 0;
    CHECK(isobar_nodes(&IB, &active, &revoked) == ZXM_OK && active == 1 && revoked == 0 &&
              IB.st[0].up && !IB.st[1].up && IB.peers == 2 && ib_sent == 2,
          "the north gate's node is up (trellis.peers_in_state): %s", IB_LINK.err);
    IB_DEVICES =
        "{\"roster_version\":\"5\",\"devices\":[" IB_DEV(IB_NODE_A, "revoked", "true") "]}";
    CHECK(isobar_nodes(&IB, &active, &revoked) == ZXM_OK && active == 0 && revoked == 1 &&
              !IB.st[0].up,
          "its device revoked: not up");
    IB_DEVICES = "{\"roster_version\":\"5\",\"devices\":[" IB_DEV(IB_NODE_A, "lost", "true") "]}";
    CHECK(isobar_nodes(&IB, &active, &revoked) == ZXM_E_PROTOCOL, "a status that is none");

    /* the file in the Hoard, read back whole */
    CHECK(ib_file_has("isobar 1\nsite 1 0 1 0 Valley network\nsite 2 1 2 120 Orchard\n"
                      "station 1 2 " IB_NODE_A " North gate\nstation 2 2 - South hollow\n"
                      "sensor 1 1 -400 500 50\nsensor 1 2 9000 10800 30\n"
                      "sensor 1 4 0 600 250\nsensor 1 5 0 500 500\nsensor 2 1 -400 500 50\n"
                      "heater 1 -50 100 30\nobs 1 1792627200 1 18 0\n") &&
              ib_file_has("obs 1 1792628400 1 90 1\n"),
          "the network in the Hoard (IM1-IM6)");
    isobar_init(&IB2, &IB_LINK);
    CHECK(isobar_load(&IB2) == ZXM_OK && IB2.nsite == IB.nsite && IB2.nst == IB.nst &&
              IB2.nsensor == IB.nsensor && IB2.nheater == 1 && IB2.nobs == IB.nobs &&
              !memcmp(IB2.obs, IB.obs, IB.nobs * sizeof IB.obs[0]),
          "read back: %s", IB_LINK.err);

    /* IM7: a full table refuses, nothing dropped silently; pruning makes room */
    {
        bool ok = true;
        int64_t at = IB_T0 + 20000;
        while (ok && IB.nobs < ISOBAR_OBS)
            ok = isobar_take(&IB, 2, ISOBAR_TEMP, at++, 28) == ZXM_OK;
        uint32_t n = IB.nobs;
        CHECK(ok && n == ISOBAR_OBS && isobar_take(&IB, 2, ISOBAR_TEMP, at, 28) == ZXM_E_TOOBIG &&
                  IB.nobs == n && strstr(IB.notice, "full"),
              "full: refused: %s", IB_LINK.err);
        CHECK(isobar_prune(&IB, IB_T0 + 20000, &dropped) == ZXM_OK && dropped == 16 &&
                  IB.nobs == ISOBAR_OBS - 16 && isobar_take(&IB, 2, ISOBAR_TEMP, at, 28) == ZXM_OK,
              "pruned %u: room again", dropped);
    }

    /* a file that is not Isobar's is never overwritten (IM6) */
    uint64_t size;
    static const char *const bad[] = {
        "notes\n",
        "isobar 1\nsite 1 0 1 0 Valley",
        "isobar 1\nsite 1 0 2 0 Valley\n",
        "isobar 1\nsite 1 0 1 07 Valley\n",
        "isobar 1\nsite 1 0 1 -0 Valley\n",
        "isobar 1\nsite 1 0 1 0 Valley\nstation 1 1 - Mast\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 AB Mast\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 - M\nsensor 1 1 5 4 0\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 - M\nsensor 1 1 0 9 1\n"
        "obs 1 5 1 3 0\nobs 1 5 1 3 0\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 - M\nsensor 1 1 0 9 1\n"
        "obs 1 5 2 3 0\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 - M\nsensor 1 1 0 9 1\n"
        "obs 1 5 1 3 2\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 - M\nheater 1 0 9 10\n",
        "isobar 1\nsite 1 0 1 0 V\nsite 2 1 2 0 S\nstation 1 2 - M\nobs 1 5 1 3 0\n"
        "sensor 1 1 0 9 1\n",
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(zxf_write(ISOBAR_FILE, (const uint8_t *) bad[i], (uint32_t) strlen(bad[i]),
                        ZXF_W_REPLACE, false, &size) == 0 &&
                  isobar_load(&IB2) == ZXM_E_PROTOCOL && !IB2.loaded,
              "not Isobar's file: %u", i);
    CHECK(isobar_add_site(&IB2, 5, 0, 1, 0, "x") == ZXM_E_STATE &&
              ib_file_has("station 1 2 - M\nobs 1 5 1 3 0\nsensor 1 1 0 9 1\n"),
          "refused, left as it was");
}
