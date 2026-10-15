/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Brazier's model against zxvd's real dispatcher (LINK: sys.info, and
 * power.status, which this in-process zxvd does not link), then against
 * a ledger of known readings on a link of its own (BZ_LINK), which answers
 * sys.info and power.status each with its own reply. */
#include "brazier_model.h"

static brazier_t BZ;
static zxm_link_t BZ_LINK;
static const char *BZ_POWER; /* the power.status result, or NULL to refuse */
static const char *BZ_SYS;   /* the sys.info result, or NULL to refuse */

static int32_t bz_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = strstr(req, "\"sys.info\"") ? BZ_SYS : BZ_POWER;
    (void) len;
    if (!r)
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32095,"
                        "\"message\":\"power tracking is not running in this zxvd\"}}",
                        l->seq);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define BZ_SYS_OK                                                                                  \
    "{\"time_ms\":1700000000000,\"os\":\"linux\",\"os_version\":\"6.1\",\"arch\":\"x86_64\","      \
    "\"host\":\"h\",\"cpus\":8,\"mem_total\":8000,\"mem_avail\":2000,\"uptime_s\":5,"              \
    "\"load\":[1,2,3],\"zxvd\":{\"pid\":7,\"cpu_user_us\":0,\"cpu_sys_us\":0,"                     \
    "\"rss_bytes\":1,\"max_rss_bytes\":2}}"
#define BZ_LEDGER(source, posture, battery)                                                        \
    "{\"device\":\"laptop\",\"host\":\"linux\",\"interval_ms\":1000,\"total_uj\":14090000,"        \
    "\"total_mw\":14090,\"source\":\"" source "\",\"lower_bound\":false,\"lifetime_uj\":1,"        \
    "\"posture\":\"" posture "\",\"token_scale_permille\":618,\"battery\":" battery                \
    ",\"meters\":[],\"consumers\":[]}"
#define BZ_BATTERY(mains, charging, now, rate, to_full, temp)                                      \
    "{\"on_mains\":" mains ",\"charging\":" charging ",\"permille\":714,\"now_mwh\":" now          \
    ",\"full_mwh\":50000,\"design_mwh\":55000,\"health_permille\":909,\"rate_mw\":" rate           \
    ",\"cycles\":312,\"minutes_to_empty\":0,\"minutes_to_full\":" to_full temp "}"

APPS_TEST(brazier)
{
    /* the rules */
    CHECK(brazier_level_x100(0, 50000) == 0 && brazier_level_x100(50000, 50000) == 10000 &&
              brazier_level_x100(35700, 50000) == 7140 && brazier_level_x100(1, 3) == 3333 &&
              brazier_level_x100(50001, 50000) == BRAZIER_UNKNOWN &&
              brazier_level_x100(-1, 50000) == BRAZIER_UNKNOWN &&
              brazier_level_x100(0, 0) == BRAZIER_UNKNOWN &&
              brazier_level_x100(INT64_MAX, INT64_MAX) == BRAZIER_UNKNOWN,
          "level x100: floored, refused past full, without a capacity or past 64 bits");
    CHECK(brazier_minutes_left(0, 1000) == 0 && brazier_minutes_left(1000, 1000) == 60 &&
              brazier_minutes_left(35700, 14090) == 152 &&
              brazier_minutes_left(999999999, 1) == 59999999940 &&
              brazier_minutes_left(1000000000, 1) == BRAZIER_UNKNOWN &&
              brazier_minutes_left(1000, 0) == BRAZIER_UNKNOWN &&
              brazier_minutes_left(1000, -5) == BRAZIER_UNKNOWN &&
              brazier_minutes_left(-1, 1000) == BRAZIER_UNKNOWN &&
              brazier_minutes_left(1, 1000000000) == BRAZIER_UNKNOWN,
          "minutes left: floored, refused with no draw, negative or past 9(9)");
    CHECK(brazier_heat_ok(0) && brazier_heat_ok(450) && !brazier_heat_ok(-1) &&
              !brazier_heat_ok(451) && brazier_heat_safe(-200) && brazier_heat_safe(600) &&
              !brazier_heat_safe(-201) && !brazier_heat_safe(601) &&
              brazier_heat(250) == BRAZIER_HEAT_OK && brazier_heat(451) == BRAZIER_HEAT_WARN &&
              brazier_heat(-1) == BRAZIER_HEAT_WARN && brazier_heat(601) == BRAZIER_HEAT_CRITICAL &&
              brazier_heat(-201) == BRAZIER_HEAT_CRITICAL &&
              brazier_heat(INT64_MIN) == BRAZIER_HEAT_CRITICAL,
          "heat: both limits' ends are inside");

    /* the real dispatcher: sys.info answers; this in-process zxvd is built
     * without the power group (zxvd_power.c), so power.status is not found
     * here (-32601; a zxvd with the group but power not open says -32095),
     * and nothing is kept (BZ3) */
    brazier_init(&BZ, &LINK);
    int before = sent;
    CHECK(brazier_refresh(&BZ) == ZXM_E_REMOTE &&
              (LINK.err_code == -32601 || LINK.err_code == -32095) && sent == before + 2 &&
              !BZ.have && BZ.bat.level_x100 == BRAZIER_UNKNOWN && !BZ.notice[0],
          "the real zxvd: sys.info read, power.status refused (%d): %s", LINK.err_code, LINK.err);

    /* a laptop on battery, 71.40%, warm: a warning, advice only */
    zxm_link_init(&BZ_LINK, bz_send, &BZ_LINK);
    brazier_init(&BZ, &BZ_LINK);
    BZ_SYS = BZ_SYS_OK;
    BZ_POWER = BZ_LEDGER("metered", "balanced",
                         BZ_BATTERY("false", "false", "35700", "14090", "0", ",\"temp_dc\":462"));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && BZ.have && BZ.cpus == 8 &&
              BZ.time_ms == 1700000000000ull && BZ.draw_mw == 14090 &&
              !strcmp(BZ.device, "laptop") && !strcmp(BZ.posture, "balanced") && BZ.bat.present &&
              !BZ.bat.on_mains && !BZ.bat.charging && BZ.bat.level_x100 == 7140 &&
              BZ.bat.minutes_left == 152 && BZ.bat.minutes_to_full == BRAZIER_UNKNOWN &&
              BZ.bat.cycles == 312 && BZ.bat.design_mwh == 55000 && BZ.bat.has_temp &&
              BZ.bat.temp_dc == 462 && BZ.bat.heat == BRAZIER_HEAT_WARN,
          "on battery: %s", BZ_LINK.err);
    CHECK(!strcmp(BZ.notice, "drawing 14090 mW; battery 71.40%, on battery, 152 minutes left; "
                             "battery temperature 46.2 C is above the usual 0.0 C to 45.0 C; "
                             "let it cool (advice only)"),
          "the warning: %s", BZ.notice);

    /* charging and cool: no warning */
    BZ_POWER = BZ_LEDGER("metered", "perform",
                         BZ_BATTERY("true", "true", "25000", "-12000", "125", ",\"temp_dc\":305"));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && BZ.bat.charging && BZ.bat.level_x100 == 5000 &&
              BZ.bat.minutes_left == BRAZIER_UNKNOWN && BZ.bat.minutes_to_full == 125 &&
              BZ.bat.heat == BRAZIER_HEAT_OK &&
              !strcmp(BZ.notice, "drawing 14090 mW; battery 50.00%, charging, full in 125 "
                                 "minutes; battery temperature 30.5 C is inside its usual range"),
          "charging: %s", BZ.notice);

    /* critical cold, and critical heat */
    BZ_POWER = BZ_LEDGER("metered", "conserve",
                         BZ_BATTERY("false", "false", "5", "1000", "0", ",\"temp_dc\":-205"));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && BZ.bat.heat == BRAZIER_HEAT_CRITICAL &&
              BZ.bat.level_x100 == 1 && BZ.bat.minutes_left == 0 &&
              strstr(BZ.notice, "battery 0.01%") &&
              strstr(BZ.notice, "battery temperature -20.5 C is below the critical -20.0 C; "
                                "let it warm now (advice only)"),
          "critical cold: %s", BZ.notice);
    BZ_POWER = BZ_LEDGER("metered", "balanced",
                         BZ_BATTERY("false", "false", "35700", "14090", "0", ",\"temp_dc\":-5"));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && BZ.bat.heat == BRAZIER_HEAT_WARN &&
              strstr(BZ.notice, "-0.5 C is below the usual 0.0 C to 45.0 C; let it warm before "
                                "charging (advice only)"),
          "cold: %s", BZ.notice);
    BZ_POWER = BZ_LEDGER("metered", "balanced",
                         BZ_BATTERY("false", "false", "35700", "14090", "0", ",\"temp_dc\":601"));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && BZ.bat.heat == BRAZIER_HEAT_CRITICAL &&
              strstr(BZ.notice, "60.1 C is above the critical 60.0 C; let it cool now"),
          "critical heat: %s", BZ.notice);

    /* no temperature reported: unknown, never guessed (BZ1) */
    BZ_POWER =
        BZ_LEDGER("metered", "balanced", BZ_BATTERY("false", "false", "35700", "0", "0", ""));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && !BZ.bat.has_temp &&
              BZ.bat.heat == BRAZIER_HEAT_UNKNOWN && BZ.bat.minutes_left == BRAZIER_UNKNOWN &&
              strstr(BZ.notice, "; no battery temperature reported") &&
              !strstr(BZ.notice, "minutes left"),
          "no temperature, idle: %s", BZ.notice);

    /* a desktop: no battery, nothing measured */
    BZ_POWER = BZ_LEDGER("none", "perform", "null");
    CHECK(brazier_refresh(&BZ) == ZXM_OK && !BZ.bat.present &&
              BZ.bat.level_x100 == BRAZIER_UNKNOWN &&
              !strcmp(BZ.notice, "no power reading; no battery"),
          "no battery: %s", BZ.notice);

    /* replies that are not the ledger, and refusals, keep the last reading (BZ3) */
    BZ_POWER = BZ_LEDGER("metered", "balanced",
                         BZ_BATTERY("false", "false", "35700", "14090", "0", ",\"temp_dc\":250"));
    CHECK(brazier_refresh(&BZ) == ZXM_OK && BZ.bat.level_x100 == 7140, "good");
    brazier_t keep = BZ;
    BZ_POWER = BZ_LEDGER("metered", "balanced", "5");
    CHECK(brazier_refresh(&BZ) == ZXM_E_PROTOCOL, "a battery that is not an object");
    BZ_POWER = BZ_LEDGER("metered", "sleepy", "null");
    CHECK(brazier_refresh(&BZ) == ZXM_E_PROTOCOL, "a posture the ledger has not");
    BZ_POWER = "{\"device\":\"laptop\",\"source\":\"metered\",\"posture\":\"balanced\","
               "\"battery\":null}";
    CHECK(brazier_refresh(&BZ) == ZXM_E_PROTOCOL, "no draw");
    BZ_POWER = BZ_LEDGER("metered", "balanced", "{\"on_mains\":false}");
    CHECK(brazier_refresh(&BZ) == ZXM_E_PROTOCOL, "a battery short of its readings");
    BZ_POWER = NULL;
    CHECK(brazier_refresh(&BZ) == ZXM_E_REMOTE && BZ_LINK.err_code == -32095, "power refused");
    BZ_SYS = NULL;
    CHECK(brazier_refresh(&BZ) == ZXM_E_REMOTE, "sys.info refused");
    BZ_SYS = "{\"time_ms\":1}";
    CHECK(brazier_refresh(&BZ) == ZXM_E_PROTOCOL, "sys.info short");
    CHECK(BZ.have && BZ.bat.level_x100 == keep.bat.level_x100 && BZ.bat.temp_dc == 250 &&
              BZ.bat.heat == BRAZIER_HEAT_OK && BZ.draw_mw == keep.draw_mw &&
              !strcmp(BZ.posture, keep.posture) && !strcmp(BZ.notice, keep.notice),
          "nothing kept from a bad reply: %s", BZ.notice);
    BZ_SYS = NULL;
}
