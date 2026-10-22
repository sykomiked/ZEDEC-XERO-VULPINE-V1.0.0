/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Homestead's model against zxvd's real dispatcher (LINK): devices, scenes
 * and readings are a file in the Banker read with files.read and written
 * with files.write; reachability is net.peers through view ops that list
 * the peers below (and none set: networking off). */
#include "homestead_model.h"

#define HS_NODE_A "aa00000000000000000000000000000000000000000000000000000000000001"
#define HS_NODE_B "bb00000000000000000000000000000000000000000000000000000000000002"

static homestead_t HS, HS2;
static const char *HS_PEERS;

static void hs_peers(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, HS_PEERS, strlen(HS_PEERS));
}

static bool hs_file_is(const char *want)
{
    static uint8_t buf[HOMESTEAD_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(HOMESTEAD_FILE, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool hs_same(const homestead_t *a, const homestead_t *b)
{
    if (a->ndev != b->ndev || a->nstep != b->nstep || a->nuse != b->nuse) return false;
    for (uint32_t i = 0; i < a->ndev; i++) {
        const homestead_device_t *x = &a->dev[i], *y = &b->dev[i];
        if (x->id != y->id || x->kind != y->kind || x->lo != y->lo || x->hi != y->hi ||
            x->setpoint != y->setpoint || strcmp(x->node, y->node) || strcmp(x->name, y->name))
            return false;
    }
    for (uint32_t i = 0; i < a->nstep; i++)
        if (a->step[i].scene != b->step[i].scene || a->step[i].device != b->step[i].device ||
            a->step[i].setpoint != b->step[i].setpoint)
            return false;
    for (uint32_t i = 0; i < a->nuse; i++)
        if (a->use[i].device != b->use[i].device || a->use[i].wh != b->use[i].wh) return false;
    return true;
}

APPS_TEST(homestead)
{
    static const zxvd_view_ops_t VIEW = {hs_peers, NULL, NULL};
    int64_t total = 0;

    /* the rules */
    CHECK(homestead_command_ok(0, 0, 0, 0) && homestead_command_ok(5, 5, -1, 0) &&
              !homestead_command_ok(-1, 5, 0, 0) && !homestead_command_ok(5, -1, 0, 0) &&
              !homestead_command_ok(5, 5, 1, 0) && !homestead_command_ok(5, 5, 0, 1) &&
              !homestead_command_ok(HOMESTEAD_MARGIN_MAX + 1, 5, 0, 0),
          "HR2: inside the range, nothing owed, not an AI's");
    CHECK(homestead_owed(HOMESTEAD_LOCK, 1, 0) == 1 && homestead_owed(HOMESTEAD_LOCK, 1, 1) == 0 &&
              homestead_owed(HOMESTEAD_HEATER, 0, 1) == 1 &&
              homestead_owed(HOMESTEAD_LIGHT, 0, 0) == 0 &&
              homestead_owed(HOMESTEAD_PLUG, 1, 1) == -1,
          "HR1: locks, heaters and ovens need warren.approval_ok");
    int64_t dv[4] = {1, 2, 1, 3}, wh[4] = {100, 7, 250, 9}, top[2] = {INT64_MAX - 1, 1},
            tdv[2] = {4, 4};
    CHECK(homestead_energy(dv, wh, 4, 1, &total) == 0 && total == 350 &&
              homestead_energy(dv, wh, 4, 9, &total) == 0 && total == 0 &&
              homestead_energy(tdv, top, 2, 4, &total) == 0 && total == INT64_MAX,
          "HR4: one device's exact total");
    top[1] = 2;
    CHECK(homestead_energy(tdv, top, 2, 4, &total) == HOMESTEAD_REFUSED &&
              homestead_energy(NULL, wh, 1, 1, &total) == HOMESTEAD_REFUSED,
          "HR4: past 64 bits refused");

    /* nothing is written before the file is read */
    CHECK(zxf_use_space(NULL) == 0, "home");
    homestead_init(&HS, &LINK);
    int before = sent;
    CHECK(homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 100, 0, NULL, "Hall light") ==
                  ZXM_E_STATE &&
              sent == before && !HS.loaded,
          "not loaded: nothing written");
    CHECK(homestead_add_device(&HS, 0, HOMESTEAD_LIGHT, 0, 100, 0, NULL, "x") == ZXM_E_ARG &&
              homestead_add_device(&HS, 1, 7, 0, 100, 0, NULL, "x") == ZXM_E_ARG &&
              homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 100, 0, 50, NULL, "x") == ZXM_E_ARG &&
              homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 100, 101, NULL, "x") == ZXM_E_ARG &&
              homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 100, 0, "AA", "x") == ZXM_E_ARG &&
              homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 100, 0, NULL, "") == ZXM_E_ARG &&
              homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 100, 0, NULL, "a\tb") == ZXM_E_ARG &&
              homestead_add_reading(&HS, 1, -1) == ZXM_E_ARG &&
              homestead_set_step(&HS, 0, 1, 0) == ZXM_E_ARG && sent == before,
          "bad devices are refused before anything is asked");

    /* a home not made yet is empty */
    CHECK(homestead_load(&HS) == ZXM_OK && HS.loaded && HS.ndev == 0 && HS.nstep == 0 &&
              HS.nuse == 0,
          "no file yet: %s", LINK.err);
    CHECK(
        homestead_add_device(&HS, 4, HOMESTEAD_LOCK, 0, 1, 1, HS_NODE_A, "Front door") == ZXM_OK &&
            homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 100, 0, NULL, "Hall light") ==
                ZXM_OK &&
            homestead_add_device(&HS, 6, HOMESTEAD_OVEN, 0, 2500, 0, HS_NODE_B, "Oven") == ZXM_OK &&
            homestead_add_device(&HS, 5, HOMESTEAD_HEATER, 50, 280, 180, NULL, "Café heater") ==
                ZXM_OK &&
            HS.ndev == 4 && HS.dev[0].id == 1 && HS.dev[3].id == 6,
        "devices kept, ids rising: %s", LINK.err);
    CHECK(homestead_add_device(&HS, 4, HOMESTEAD_PLUG, 0, 1, 0, NULL, "again") == ZXM_E_ARG,
          "an id kept already");

    /* HM6: commands */
    CHECK(homestead_command(&HS, 1, 40, 0, 0) == ZXM_OK && homestead_find(&HS, 1)->setpoint == 40 &&
              strstr(HS.notice, "Set Hall light (light) to 40."),
          "a light needs no approval: %s", HS.notice);
    CHECK(homestead_command(&HS, 1, 101, 1, 1) == ZXM_E_ARG &&
              homestead_find(&HS, 1)->setpoint == 40 &&
              strstr(HS.notice, "outside Hall light's range 0..100"),
          "outside its range: %s", HS.notice);
    before = sent;
    CHECK(homestead_command(&HS, 4, 0, 1, 0) == HOMESTEAD_OWNER &&
              homestead_command(&HS, 4, 0, 0, 1) == HOMESTEAD_OWNER &&
              homestead_command(&HS, 4, 0, 2, 1) == HOMESTEAD_OWNER &&
              homestead_find(&HS, 4)->setpoint == 1 && sent == before &&
              strstr(HS.notice, "owner's approval"),
          "a lock waits for the owner: nothing written: %s", HS.notice);
    CHECK(homestead_command(&HS, 4, 0, 1, 1) == ZXM_OK && homestead_find(&HS, 4)->setpoint == 0,
          "the owner's approval unlocks it");
    CHECK(homestead_command(&HS, 9, 0, 1, 1) == ZXM_E_ARG, "no such device");

    /* HM7: an AI's suggestion is held, never run */
    CHECK(homestead_accept(&HS, 1, 1) == ZXM_E_STATE && homestead_reject(&HS) == ZXM_E_STATE,
          "nothing waits yet");
    before = sent;
    CHECK(homestead_suggest(&HS, 6, 1800) == HOMESTEAD_HELD && HS.held.pending &&
              homestead_find(&HS, 6)->setpoint == 0 && sent == before &&
              strstr(HS.notice, "held: nothing runs until the owner accepts it"),
          "a suggestion for the oven is held: %s", HS.notice);
    CHECK(homestead_suggest(&HS, 6, 2600) == ZXM_E_ARG && HS.held.setpoint == 1800,
          "a suggestion outside the range is refused, the held one stays");
    CHECK(homestead_accept(&HS, 1, 0) == HOMESTEAD_OWNER && HS.held.pending &&
              homestead_find(&HS, 6)->setpoint == 0,
          "accepting it still needs the owner's approval for an oven");
    CHECK(homestead_accept(&HS, 1, 1) == ZXM_OK && !HS.held.pending &&
              homestead_find(&HS, 6)->setpoint == 1800,
          "accepted and approved: it runs");
    CHECK(homestead_suggest(&HS, 1, 10) == HOMESTEAD_HELD && homestead_reject(&HS) == ZXM_OK &&
              !HS.held.pending && homestead_find(&HS, 1)->setpoint == 40,
          "a rejected suggestion never runs, even for a light");

    /* HM3, HM8: scenes run whole or not at all */
    CHECK(homestead_set_step(&HS, 2, 1, 0) == ZXM_OK &&
              homestead_set_step(&HS, 2, 4, 1) == ZXM_OK &&
              homestead_set_step(&HS, 1, 1, 100) == ZXM_OK &&
              homestead_set_step(&HS, 1, 5, 200) == ZXM_OK &&
              homestead_set_step(&HS, 1, 5, 210) == ZXM_OK && HS.nstep == 4 &&
              HS.step[0].scene == 1 && HS.step[1].setpoint == 210,
          "steps kept in order, one replaced: %s", LINK.err);
    CHECK(homestead_set_step(&HS, 1, 1, 101) == ZXM_E_ARG &&
              homestead_set_step(&HS, 1, 9, 1) == ZXM_E_ARG &&
              homestead_set_step(&HS, 1000, 1, 1) == ZXM_E_ARG,
          "a step outside its device's range, of no device, or past scene 999");
    before = sent;
    CHECK(homestead_run_scene(&HS, 2, 0, 0) == HOMESTEAD_OWNER &&
              homestead_find(&HS, 1)->setpoint == 40 && homestead_find(&HS, 4)->setpoint == 0 &&
              sent == before,
          "a scene that locks the door waits: no step ran: %s", HS.notice);
    CHECK(homestead_run_scene(&HS, 2, 1, 1) == ZXM_OK && homestead_find(&HS, 1)->setpoint == 0 &&
              homestead_find(&HS, 4)->setpoint == 1 && strstr(HS.notice, "Scene 2 ran: 2 devices"),
          "approved, every step: %s", HS.notice);
    CHECK(homestead_run_scene(&HS, 7, 1, 1) == ZXM_E_ARG, "no such scene");

    /* HM4, HM9: energy */
    CHECK(homestead_add_reading(&HS, 6, 1200) == ZXM_OK &&
              homestead_add_reading(&HS, 5, 800) == ZXM_OK &&
              homestead_add_reading(&HS, 6, HOMESTEAD_WH_MAX) == ZXM_OK &&
              homestead_add_reading(&HS, 9, 1) == ZXM_E_ARG &&
              homestead_add_reading(&HS, 6, HOMESTEAD_WH_MAX + 1) == ZXM_E_ARG,
          "readings: %s", LINK.err);
    CHECK(homestead_device_energy(&HS, 6, &total) == ZXM_OK && total == 1200 + HOMESTEAD_WH_MAX &&
              homestead_device_energy(&HS, 1, &total) == ZXM_OK && total == 0 &&
              homestead_device_energy(&HS, 9, &total) == ZXM_E_ARG,
          "each device's energy, exact");

    CHECK(hs_file_is("homestead 1\n"
                     "device 1 1 0 100 0 - Hall light\n"
                     "device 4 4 0 1 1 " HS_NODE_A " Front door\n"
                     "device 5 5 50 280 180 - Café heater\n"
                     "device 6 6 0 2500 1800 " HS_NODE_B " Oven\n"
                     "step 1 1 100\nstep 1 5 210\nstep 2 1 0\nstep 2 4 1\n"
                     "use 6 1200\nuse 5 800\nuse 6 999999999\n"),
          "the file in the Banker (HM1-HM4)");
    homestead_init(&HS2, &LINK);
    CHECK(homestead_load(&HS2) == ZXM_OK && hs_same(&HS, &HS2), "read back: %s", LINK.err);

    /* bridges: networking off, then one of two nodes reachable */
    uint32_t count = 9;
    zxvd_rpc_set_view_ops(NULL);
    CHECK(homestead_bridges(&HS, &count) == ZXM_E_REMOTE && LINK.err_code == -32601 && count == 9,
          "no networking: zxvd says so: %s", LINK.err);
    HS_PEERS = "{\"peers\":[{\"id\":\"" HS_NODE_A "\",\"addr\":\"192.168.1.20:8723\",\"bucket\":"
               "3,\"ubh\":true,\"last_seen_ms\":\"1\",\"in_book\":false,\"member\":false,"
               "\"held\":\"0\",\"receivable\":\"0\",\"social\":\"0\",\"pending\":0}],\"count\":1}";
    zxvd_rpc_set_view_ops(&VIEW);
    CHECK(homestead_bridges(&HS, &count) == ZXM_OK && count == 1 &&
              homestead_find(&HS, 4)->reachable && !homestead_find(&HS, 6)->reachable &&
              !homestead_find(&HS, 1)->reachable,
          "the front door's node is reachable, the oven's is not: %s", LINK.err);
    HS_PEERS = "{\"peers\":[{\"id\":\"zz\"}],\"count\":1}";
    CHECK(homestead_bridges(&HS, &count) == ZXM_E_PROTOCOL, "a NodeID that is no NodeID");
    zxvd_rpc_set_view_ops(NULL);

    /* removing a device takes its steps and readings with it */
    CHECK(homestead_remove_device(&HS, 6) == ZXM_OK && HS.ndev == 3 && HS.nuse == 1 &&
              homestead_remove_device(&HS, 6) == ZXM_E_ARG &&
              homestead_remove_scene(&HS, 1) == ZXM_OK && HS.nstep == 2 &&
              homestead_remove_scene(&HS, 1) == ZXM_E_ARG,
          "removed: %s", LINK.err);

    /* a file that is not Homestead's is never overwritten (HM5) */
    uint64_t size;
    static const char *const bad[] = {
        "notes\n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall",
        "homestead 1\ndevice 1 1 0 100 07 - Hall\n",
        "homestead 1\ndevice 1 1 0 100 -0 - Hall\n",
        "homestead 1\ndevice 1 7 0 100 0 - Hall\n",
        "homestead 1\ndevice 1 1 100 0 50 - Hall\n",
        "homestead 1\ndevice 1 1 0 100 101 - Hall\n",
        "homestead 1\ndevice 2 1 0 100 0 - A\ndevice 1 1 0 100 0 - B\n",
        "homestead 1\ndevice 1 1 0 100 0 AA Hall\n",
        "homestead 1\ndevice 1 1 0 100 0 - \n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall\nuse 1 5\nstep 1 1 0\n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall\nstep 1 2 0\n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall\nstep 1 1 101\n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall\nstep 0 1 1\n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall\nuse 2 5\n",
        "homestead 1\ndevice 1 1 0 100 0 - Hall\nuse 1 1000000000\n",
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(zxf_write(HOMESTEAD_FILE, (const uint8_t *) bad[i], (uint32_t) strlen(bad[i]),
                        ZXF_W_REPLACE, false, &size) == 0 &&
                  homestead_load(&HS2) == ZXM_E_PROTOCOL && !HS2.loaded,
              "not Homestead's file: %u", i);
    CHECK(homestead_command(&HS2, 1, 5, 1, 1) == ZXM_E_STATE &&
              hs_file_is("homestead 1\ndevice 1 1 0 100 0 - Hall\nuse 1 1000000000\n"),
          "refused, left as it was");
    static const char good[] = "homestead 1\ndevice 3 5 -400 -100 -180 - Freezer\n";
    CHECK(zxf_write(HOMESTEAD_FILE, (const uint8_t *) good, (uint32_t) strlen(good), ZXF_W_REPLACE,
                    false, &size) == 0 &&
              homestead_load(&HS2) == ZXM_OK && HS2.ndev == 1 &&
              homestead_find(&HS2, 3)->setpoint == -180 && HS2.nstep == 0 && HS2.nuse == 0,
          "negative setpoints read");

    /* a full home: every list at its limit is still one file that reads back */
    homestead_init(&HS, &LINK);
    CHECK(zxf_write(HOMESTEAD_FILE, (const uint8_t *) "homestead 1\n", 12, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              homestead_load(&HS) == ZXM_OK,
          "an empty home");
    static char longest[HOMESTEAD_NAME_MAX + 2];
    memset(longest, 'n', HOMESTEAD_NAME_MAX + 1u);
    CHECK(homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 1, 0, NULL, longest) == ZXM_E_ARG,
          "a name past 63 bytes");
    longest[HOMESTEAD_NAME_MAX] = 0;
    bool ok = true;
    for (int64_t id = 1; ok && id <= (int64_t) HOMESTEAD_DEVICES; id++)
        ok = homestead_add_device(&HS, 9999 - id, HOMESTEAD_HEATER, -99999, 99999, -99999,
                                  HS_NODE_A, longest) == ZXM_OK;
    CHECK(ok && HS.ndev == HOMESTEAD_DEVICES &&
              homestead_add_device(&HS, 1, HOMESTEAD_LIGHT, 0, 1, 0, NULL, "x") == ZXM_E_TOOBIG,
          "full of devices: %s", LINK.err);
    for (uint32_t i = 0; ok && i < HOMESTEAD_STEPS; i++)
        ok = homestead_set_step(&HS, 999 - (int64_t) (i / HOMESTEAD_DEVICES),
                                HS.dev[i % HOMESTEAD_DEVICES].id, -99999) == ZXM_OK;
    for (uint32_t i = 0; ok && i < HOMESTEAD_READINGS; i++)
        ok = homestead_add_reading(&HS, HS.dev[i % HOMESTEAD_DEVICES].id, HOMESTEAD_WH_MAX) ==
             ZXM_OK;
    CHECK(ok && HS.nstep == HOMESTEAD_STEPS && HS.nuse == HOMESTEAD_READINGS &&
              homestead_add_reading(&HS, HS.dev[0].id, 1) == ZXM_E_TOOBIG,
          "full: %s", LINK.err);
    homestead_init(&HS2, &LINK);
    CHECK(homestead_load(&HS2) == ZXM_OK && hs_same(&HS, &HS2) &&
              homestead_device_energy(&HS2, HS2.dev[0].id, &total) == ZXM_OK &&
              total == (int64_t) (HOMESTEAD_READINGS / HOMESTEAD_DEVICES) * HOMESTEAD_WH_MAX,
          "a full home read back: %s", LINK.err);
}
