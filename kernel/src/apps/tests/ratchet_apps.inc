/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ratchet's model against zxvd's real dispatcher (LINK: sys.disk, and
 * power.status, which this in-process zxvd does not link), then against a
 * machine of known readings on a link of its own (RT_LINK), which answers
 * sys.info, sys.disk and power.status each with its own reply: a parts
 * tree, faults diagnosed, part 4 BOTH and inspected, a repair walked one
 * confirmed step at a time, wear, service and the job's cost. */
#include "ratchet_model.h"

static ratchet_t RT;
static zxm_link_t RT_LINK;
static const char *RT_POWER; /* the power.status result, or NULL to refuse */
static uint32_t rt_sent;

static int32_t rt_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = RT_POWER;
    (void) len;
    rt_sent++;
    if (strstr(req, "\"sys.info\""))
        r = "{\"time_ms\":1700000000000,\"os\":\"linux\",\"os_version\":\"6.1\",\"arch\":\"x86_"
            "64\","
            "\"host\":\"h\",\"cpus\":8,\"mem_total\":8000,\"mem_avail\":2000,\"uptime_s\":5,"
            "\"load\":[400,300,200],\"zxvd\":{\"pid\":7,\"cpu_user_us\":0,\"cpu_sys_us\":0,"
            "\"rss_bytes\":1,\"max_rss_bytes\":2}}";
    else if (strstr(req, "\"sys.disk\""))
        r = "{\"path\":\"/h\",\"total\":1000,\"free\":250,\"avail\":200}";
    if (!r)
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32095,"
                        "\"message\":\"power tracking is not running in this zxvd\"}}",
                        l->seq);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define RT_LEDGER                                                                                  \
    "{\"device\":\"laptop\",\"host\":\"linux\",\"interval_ms\":1000,\"total_uj\":14090000,"        \
    "\"total_mw\":14090,\"source\":\"metered\",\"lower_bound\":false,\"lifetime_uj\":1,"           \
    "\"posture\":\"balanced\",\"token_scale_permille\":618,\"battery\":{\"on_mains\":false,"       \
    "\"charging\":false,\"permille\":714,\"now_mwh\":35700,\"full_mwh\":50000,"                    \
    "\"design_mwh\":55000,\"health_permille\":909,\"rate_mw\":14090,\"cycles\":312,"               \
    "\"minutes_to_empty\":152,\"minutes_to_full\":0,\"temp_dc\":462},\"meters\":["                 \
    "{\"name\":\"pkg\",\"domain\":1,\"scope\":0,\"lifetime_uj\":5000,\"faults\":0},"               \
    "{\"name\":\"dram\",\"domain\":2,\"scope\":0,\"lifetime_uj\":7000,\"faults\":3},"              \
    "{\"name\":\"core\",\"domain\":1,\"scope\":1,\"lifetime_uj\":1000,\"faults\":0}],"             \
    "\"consumers\":[]}"

APPS_TEST(ratchet)
{
    /* the rules, as the blocks run them */
    {
        int64_t ab = 0, be = 0, n = 0, d = 0, h[2] = {10, 20}, ld[2] = {5000, 2500};
        CHECK(ratchet_margins(450, 0, 710, &ab, &be) && ab == 450 && be == 260 &&
                  ratchet_limits_ok(ab, be) && ratchet_margins(912, 0, 850, &ab, &be) &&
                  be == -62 && !ratchet_limits_ok(ab, be) && !ratchet_margins(1, 5, 4, &ab, &be) &&
                  !ratchet_margins(INT64_MAX, -1, 0, &ab, &be),
              "ratchet: margins inside a range, a fault outside, no range refused");
        CHECK(ratchet_repair_step(1, 0, 0, 0) == RATCHET_STEP_ISOLATE &&
                  ratchet_repair_step(0, 0, 0, 0) == RATCHET_STEP_NONE &&
                  ratchet_repair_step(2, 0, 0, 0) == RATCHET_REFUSED,
              "ratchet: the next repair step");
        CHECK(ratchet_wear(h, ld, 2) == 100000 &&
                  ratchet_service_left(1000, 100000, 5000, &n, &d) && n == 1980 && d == 1 &&
                  ratchet_service_left(500, 6000000, 3, &n, &d) && n == -1000000 && d == 3 &&
                  !ratchet_service_left(0, 0, 1, &n, &d),
              "ratchet: wear and hours to service, exact");
        CHECK(ratchet_line_cost(2, 1250, 90, 75) == 9250 &&
                  ratchet_line_cost(RATCHET_QTY_MAX + 1, 1, 0, 0) == RATCHET_REFUSED,
              "ratchet: a job line's cost");
    }

    /* the real dispatcher: sys.disk answers, power.status is not linked here, so the
     * refresh fails before sys.info and nothing is kept */
    ratchet_init(&RT, &LINK);
    CHECK(ratchet_add_part(&RT, 1, 0, RATCHET_KIND_MACHINE, "this machine", NULL, true) == ZXM_OK &&
              ratchet_add_part(&RT, 3, 1, RATCHET_KIND_DISK, "the Hoard's disk", NULL, true) ==
                  ZXM_OK &&
              ratchet_set_limit(&RT, 3, RATCHET_M_USED, 0, 9000, true) == ZXM_OK,
          "ratchet: a machine and its disk");
    int before = sent;
    CHECK(ratchet_refresh(&RT) == ZXM_E_REMOTE &&
              (LINK.err_code == -32601 || LINK.err_code == -32095) && sent == before + 2 &&
              !RT.have && RT.ngrid == 0 && !RT.p[1].has_reading[RATCHET_M_USED] &&
              tidewatch_count(&RT.tide) == 0,
          "ratchet: the real zxvd: sys.disk read, power.status refused (%d), nothing kept: %s",
          LINK.err_code, LINK.err);

    /* RM1, RM2: a machine of known readings, its tree on the owner's word */
    zxm_link_init(&RT_LINK, rt_send, &RT_LINK);
    ratchet_init(&RT, &RT_LINK);
    RT_POWER = RT_LEDGER;
    ratchet_map_t uart = {INT64_C(0x80000000), 0x1000, 4}, bad_irq = {0, 0x1000, 1000};
    CHECK(ratchet_add_part(&RT, 1, 0, RATCHET_KIND_MACHINE, "this machine", NULL, false) ==
                  ZXM_E_STATE &&
              RT.np == 0,
          "ratchet: no part without the owner's word, the tree as it was");
    CHECK(ratchet_add_part(&RT, 1, 0, RATCHET_KIND_MACHINE, "this machine", NULL, true) == ZXM_OK &&
              ratchet_add_part(&RT, 2, 1, RATCHET_KIND_CPU, "processor", &uart, true) == ZXM_OK &&
              ratchet_add_part(&RT, 3, 1, RATCHET_KIND_DISK, "disk", NULL, true) == ZXM_OK &&
              ratchet_add_part(&RT, 4, 1, RATCHET_KIND_OTHER, "fan motor", NULL, true) == ZXM_OK &&
              ratchet_add_part(&RT, 5, 1, RATCHET_KIND_BATTERY, "battery", NULL, true) == ZXM_OK &&
              ratchet_add_part(&RT, 6, 1, RATCHET_KIND_POWER, "power supply", NULL, true) ==
                  ZXM_OK &&
              RT.np == 6,
          "ratchet: six parts");
    CHECK(ratchet_add_part(&RT, 1000000, 1, RATCHET_KIND_OTHER, "x", NULL, true) == ZXM_E_ARG &&
              ratchet_add_part(&RT, 7, 99, RATCHET_KIND_OTHER, "x", NULL, true) == ZXM_E_ARG &&
              ratchet_add_part(&RT, 4, 1, RATCHET_KIND_OTHER, "x", NULL, true) == ZXM_E_ARG &&
              ratchet_add_part(&RT, 7, 1, 8, "x", NULL, true) == ZXM_E_ARG &&
              ratchet_add_part(&RT, 7, 1, RATCHET_KIND_OTHER, "", NULL, true) == ZXM_E_ARG &&
              ratchet_add_part(&RT, 7, 1, RATCHET_KIND_CPU, "x", &bad_irq, true) == ZXM_E_ARG &&
              RT.np == 6,
          "ratchet: a part past 6 digits, without its parent, twice, of no kind, unnamed or "
          "with a map record that does not fit (mockingbird.emu_device_ok): refused");
    CHECK(ratchet_set_limit(&RT, 4, RATCHET_M_VIBRATION, 0, 710, false) == ZXM_E_STATE &&
              !RT.p[3].has_limit[RATCHET_M_VIBRATION] &&
              ratchet_set_limit(&RT, 4, RATCHET_M_TEMP, 0, 1000000, true) == ZXM_E_ARG &&
              ratchet_set_limit(&RT, 4, RATCHET_MEASURES, 0, 1, true) == ZXM_E_ARG &&
              ratchet_set_limit(&RT, 9, RATCHET_M_TEMP, 0, 1, true) == ZXM_E_ARG,
          "ratchet: a limit without the owner's word, wider than 999999, of no measure or part: "
          "refused");
    CHECK(ratchet_set_limit(&RT, 4, RATCHET_M_VIBRATION, 0, 710, true) == ZXM_OK &&
              ratchet_set_limit(&RT, 4, RATCHET_M_TEMP, 0, 850, true) == ZXM_OK &&
              ratchet_set_limit(&RT, 2, RATCHET_M_LOAD, 0, 9000, true) == ZXM_OK &&
              ratchet_set_limit(&RT, 3, RATCHET_M_USED, 0, 9000, true) == ZXM_OK &&
              ratchet_set_limit(&RT, 5, RATCHET_M_TEMP, 0, 450, true) == ZXM_OK &&
              ratchet_set_limit(&RT, 6, RATCHET_M_FAULTS, 0, 0, true) == ZXM_OK,
          "ratchet: the owner's limits");

    /* RM3: this machine's readings */
    rt_sent = 0;
    CHECK(ratchet_refresh(&RT) == ZXM_OK && rt_sent == 3 && RT.have &&
              RT.hour == 1700000000000 / 3600000 && RT.p[1].reading[RATCHET_M_LOAD] == 5000 &&
              RT.p[2].reading[RATCHET_M_USED] == 7500 && RT.p[4].reading[RATCHET_M_TEMP] == 462 &&
              RT.p[5].reading[RATCHET_M_DRAW] == 14090 && RT.p[5].reading[RATCHET_M_FAULTS] == 3 &&
              RT.ngrid == 4 && tidewatch_count(&RT.tide) == 1,
          "ratchet: load 4.00 over 8 CPUs, the disk 75.00%% used, the battery 46.2 C, 3 meter "
          "faults: %s",
          RT_LINK.err);
    int64_t uj = 0;
    CHECK(ratchet_domain_energy(&RT, 1, &uj) == ZXM_OK && uj == 6000 &&
              ratchet_domain_energy(&RT, 2, &uj) == ZXM_OK && uj == 7000,
          "ratchet: a meter domain's energy (homestead.device_energy)");
    RT_POWER = NULL;
    rt_sent = 0;
    CHECK(ratchet_refresh(&RT) == ZXM_E_REMOTE && rt_sent == 2 && RT.ngrid == 4 &&
              tidewatch_count(&RT.tide) == 1,
          "ratchet: power.status refused: nothing kept");
    RT_POWER = RT_LEDGER;

    lpres_state_t s = LPRES_STATE_NEITHER;
    CHECK(ratchet_diagnose(&RT, 2, &s) == ZXM_OK && s == LPRES_STATE_TRUE &&
              ratchet_diagnose(&RT, 3, &s) == ZXM_OK && s == LPRES_STATE_TRUE &&
              ratchet_diagnose(&RT, 5, &s) == ZXM_OK && s == LPRES_STATE_FALSE &&
              ratchet_task_of(&RT, 5, RATCHET_TASK_REPAIR) >= 0 &&
              ratchet_diagnose(&RT, 1, &s) == ZXM_OK && s == LPRES_STATE_NEITHER,
          "ratchet: the processor and disk sound, the battery over its limit (a repair raised), "
          "the machine itself unread");

    /* RM4, the BOTH test: part 4 within its limits on vibration (450 of 710) and over them
     * on temperature (912 of 850): BOTH, an inspection raised, never ignored */
    CHECK(ratchet_reading(&RT, 4, RATCHET_M_VIBRATION, 450, RT.hour) == ZXM_OK &&
              ratchet_reading(&RT, 4, RATCHET_M_TEMP, 912, RT.hour) == ZXM_OK &&
              ratchet_reading(&RT, 4, RATCHET_MEASURES, 1, RT.hour) == ZXM_E_ARG &&
              ratchet_reading(&RT, 8, RATCHET_M_TEMP, 1, RT.hour) == ZXM_E_ARG,
          "ratchet: the person's readings of part 4");
    CHECK(ratchet_diagnose(&RT, 4, &s) == ZXM_OK && s == LPRES_STATE_BOTH &&
              s != LPRES_STATE_TRUE && RT.p[3].state == LPRES_STATE_BOTH,
          "ratchet: part 4 is BOTH");
    int32_t insp = ratchet_task_of(&RT, 4, RATCHET_TASK_INSPECT);
    CHECK(insp >= 0 && !RT.task[insp].approved && RT.task[insp].next == -1 &&
              strstr(RT.notice, "fan motor") && strstr(RT.notice, "inspection"),
          "ratchet: an inspection task is raised for part 4, waiting for the owner: %s", RT.notice);
    uint32_t tasks = RT.ntask;
    CHECK(ratchet_diagnose(&RT, 4, &s) == ZXM_OK && s == LPRES_STATE_BOTH && RT.ntask == tasks &&
              ratchet_task_of(&RT, 4, RATCHET_TASK_INSPECT) == insp,
          "ratchet: diagnosed again it is still BOTH, the one inspection kept");
    lpres_state_t m = ratchet_machine_state(&RT);
    CHECK(m != LPRES_STATE_TRUE && m != LPRES_STATE_NEITHER &&
              m == lpres_conjoin(lpres_conjoin(LPRES_STATE_TRUE, LPRES_STATE_BOTH),
                                 LPRES_STATE_FALSE),
          "ratchet: the machine with part 4 and the battery is never sound (%d)", (int) m);
    rt_sent = 0;
    CHECK(ratchet_approve_task(&RT, (uint32_t) insp, 600, false) == ZXM_E_STATE &&
              !RT.task[insp].approved && rt_sent == 0,
          "ratchet: no task approved without the owner's word, nothing sent");
    CHECK(ratchet_approve_task(&RT, (uint32_t) insp, 600, true) == ZXM_OK &&
              RT.task[insp].approved && RT.task[insp].next == 1440,
          "ratchet: the inspection approved, due at tomorrow's first run (treadle.next_run)");

    /* RM7: the repair walk, one confirmed step at a time */
    CHECK(ratchet_walk_start(&RT, 4) == ZXM_OK && RT.walk.step == RATCHET_STEP_ISOLATE &&
              !strncmp(RT.notice, "Isolate the power", 17),
          "ratchet: the walk starts by isolating the power: %s", RT.notice);
    rt_sent = 0;
    CHECK(ratchet_walk_confirm(&RT, false, NULL) == ZXM_E_STATE &&
              RT.walk.step == RATCHET_STEP_ISOLATE && RT.walk.isolated == 0 && rt_sent == 0,
          "ratchet: without the person's confirmation nothing moves");
    CHECK(ratchet_walk_confirm(&RT, true, NULL) == ZXM_OK && RT.walk.step == RATCHET_STEP_REPAIR &&
              !strncmp(ratchet_step_text(RT.walk.step), "Isolate the power", 17),
          "ratchet: power isolated; repair the part: %s", RT.notice);
    CHECK(ratchet_walk_confirm(&RT, true, NULL) == ZXM_OK && RT.walk.step == RATCHET_STEP_RESTORE &&
              !strncmp(RT.notice, "Isolate the power", 17),
          "ratchet: repaired; restore the power and run the self-check");
    {
        int64_t failed[MOCK_STAGES] = {0, 0, 3, -1, -1}, passed[MOCK_STAGES] = {0, 0, 0, 0, 0};
        CHECK(ratchet_walk_confirm(&RT, true, NULL) == ZXM_E_ARG &&
                  RT.walk.step == RATCHET_STEP_RESTORE,
              "ratchet: no self-check, no step");
        CHECK(ratchet_walk_confirm(&RT, false, passed) == ZXM_E_STATE &&
                  RT.walk.step == RATCHET_STEP_RESTORE && RT.walk.tested == 0,
              "ratchet: a self-check without the person's confirmation: nothing moves");
        CHECK(ratchet_walk_confirm(&RT, true, failed) == ZXM_OK && RT.walk.failed_stage == 2 &&
                  RT.walk.step == RATCHET_STEP_ISOLATE && RT.walk.fixed == 0,
              "ratchet: the self-check fails at the kernel stage: back to isolating the power");
        CHECK(ratchet_walk_confirm(&RT, true, NULL) == ZXM_OK &&
                  ratchet_walk_confirm(&RT, true, NULL) == ZXM_OK &&
                  ratchet_walk_confirm(&RT, true, passed) == ZXM_OK &&
                  RT.walk.step == RATCHET_STEP_RECORD && RT.walk.tested == 1 &&
                  RT.walk.restore_allowed && RT.walk.failed_stage == MOCK_STAGES,
              "ratchet: repaired again, the self-check passes (mockingbird.boot_passed); the "
              "restore command would be allowed (homestead.device_cmd_ok), never sent");
    }
    CHECK(ratchet_walk_confirm(&RT, true, NULL) == ZXM_OK && !RT.walk.open &&
              RT.p[3].state == LPRES_STATE_NEITHER && !RT.p[3].has_reading[RATCHET_M_TEMP] &&
              ratchet_task_of(&RT, 4, RATCHET_TASK_INSPECT) < 0 &&
              ratchet_walk_confirm(&RT, true, NULL) == ZXM_E_STATE,
          "ratchet: recorded: the walk closed, the inspection done, part 4 to be read again");
    CHECK(ratchet_walk_start(&RT, 2) == ZXM_OK && RT.walk.step == RATCHET_STEP_NONE &&
              ratchet_walk_confirm(&RT, true, NULL) == ZXM_OK && !RT.walk.open,
          "ratchet: a sound part needs no repair");

    /* RM5: wear and the hours to the next service */
    int64_t worn = 0, num = 0, den = 0;
    CHECK(ratchet_log_run(&RT, 4, 10, 5000) == ZXM_OK &&
              ratchet_log_run(&RT, 4, 20, 2500) == ZXM_OK &&
              ratchet_log_run(&RT, 4, 1, RATCHET_LOAD_FULL + 1) == ZXM_E_ARG &&
              ratchet_log_run(&RT, 4, RATCHET_HOURS_MAX + 1, 1) == ZXM_E_ARG && RT.nrun == 2 &&
              RT.p[3].hours == 30,
          "ratchet: two runs logged; a load past 100%% or too many hours refused");
    CHECK(ratchet_wear_of(&RT, 4, &worn) == ZXM_OK && worn == 100000 &&
              ratchet_wear_of(&RT, 2, &worn) == ZXM_OK && worn == 0,
          "ratchet: part 4 has worn 10 full-load hours");
    CHECK(ratchet_service_due(&RT, 4, 1000, 5000, &num, &den) == ZXM_OK && num == 1980 &&
              den == 1 && ratchet_service_due(&RT, 4, 1000, 0, &num, &den) == ZXM_E_ARG,
          "ratchet: 1980 hours to its service at half load");

    /* RM6: a service scheduled on the owner's word */
    int64_t next = 0;
    tasks = RT.ntask;
    CHECK(ratchet_schedule(&RT, 4, RATCHET_TASK_SERVICE, 480, 60, 481, 500, 1000, false, &next) ==
                  ZXM_E_STATE &&
              RT.ntask == tasks &&
              ratchet_schedule(&RT, 4, RATCHET_TASK_SERVICE, 480, 60, 481, 500, -1, true, &next) ==
                  ZXM_E_ARG &&
              ratchet_schedule(&RT, 4, RATCHET_TASK_SERVICE, 480, 0, 481, 500, 1000, true, &next) ==
                  ZXM_E_ARG &&
              RT.ntask == tasks,
          "ratchet: no schedule without the owner's word, over budget or with no period");
    CHECK(ratchet_schedule(&RT, 4, RATCHET_TASK_SERVICE, 480, 60, 481, 500, 1000, true, &next) ==
                  ZXM_OK &&
              next == 540 && RT.ntask == tasks + 1 &&
              RT.task[ratchet_task_of(&RT, 4, RATCHET_TASK_SERVICE)].approved,
          "ratchet: the service runs at 09:00 (treadle.next_run, budget_ok, task_ok)");

    /* RM8: the job's cost */
    {
        int64_t q[2] = {2, 1}, p[2] = {1250, 899}, mi[2] = {90, 0}, ra[2] = {75, 0}, total = 0;
        int64_t qb[1] = {RATCHET_QTY_MAX + 1};
        CHECK(ratchet_job_cost(q, p, mi, ra, 2, &total) == ZXM_OK && total == 10149 &&
                  ratchet_job_cost(qb, p, mi, ra, 1, &total) == ZXM_E_ARG && total == 10149,
              "ratchet: two parts and 90 minutes' labour, 101.49; a line past its picture refused");
    }

    /* RM3: the sensor grid, and its backpressure */
    {
        int64_t cell[RATCHET_GRID_ROWS];
        uint32_t n = 0;
        CHECK(ratchet_grid(&RT, cell, RATCHET_GRID_ROWS, &n) == ZXM_OK && n == RT.ngrid && n == 6 &&
                  cell[3] == 1 && cell[5] == 1,
              "ratchet: the grid's cells (%u)", n);
        uint32_t k = 0;
        while (ratchet_reading(&RT, 4, RATCHET_M_VIBRATION, 100, RT.hour) == ZXM_OK) k++;
        CHECK(RT.ngrid == RATCHET_GRID_ROWS && k == RATCHET_GRID_ROWS - 6 &&
                  ratchet_reading(&RT, 4, RATCHET_M_VIBRATION, 100, RT.hour) == ZXM_E_STATE &&
                  ratchet_refresh(&RT) == ZXM_E_STATE,
              "ratchet: a full grid refuses a reading, never overwrites one");
    }
}
