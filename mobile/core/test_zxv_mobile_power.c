/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_mobile_power.c — the phone power hook: Android with a measured
 * drain split between the companion app and its agent, iOS with no current
 * (energy "none"), charging and the low-battery postures. */
#include <stdio.h>
#include <string.h>
#include "zxv_mobile_power.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

int main(void)
{
    static char j[16384];
    CHECK(zxv_power_battery(500, 0, 0, 1, 1, 1, 1, 1, 1, 0) < 0, "not started");
    CHECK(zxv_power_start(3, 8, 0) < 0, "phones only");
    CHECK(zxv_power_start(ZXV_POWER_ANDROID, 8, 0) == 0, "android");
    /* 500 mA at 4000 mV = 2 W while unplugged */
    CHECK(zxv_power_battery(800, 0, 0, 500000, 4000, 4000, 5000, 400, 300, 0) == 0, "base");
    CHECK(zxv_power_app_cpu(2000000000u) == 0, "app cpu");
    CHECK(zxv_power_agent(9, "phi", 1000000000u, 0) == 0, "agent");
    CHECK(zxv_power_battery(790, 0, 0, 500000, 4000, 4000, 5000, 400, 300, 1000) == 0, "1 s");
    CHECK(zxv_power_settle(1000) == 0, "settle");
    int n = zxv_power_json(j, sizeof(j));
    CHECK(n > 0, "json");
    CHECK(strstr(j, "\"device\":\"mobile\",\"host\":\"android\"") != NULL, "mobile android");
    CHECK(strstr(j, "\"total_uj\":2000000,\"total_mw\":2000,\"source\":\"metered\"") != NULL,
          "2 J measured");
    /* 8 cpus: app 2 s of 8 s = 500 mJ; agent half of it */
    CHECK(strstr(j, "\"name\":\"zxv-companion\",\"parent\":null,\"ext_id\":\"1\",\"uj\":250000") !=
              NULL,
          "companion keeps 250 mJ");
    CHECK(strstr(j, "\"name\":\"phi\",\"parent\":1,\"ext_id\":\"9\",\"uj\":250000") != NULL,
          "agent 250 mJ");
    CHECK(strstr(j, "\"rate_mw\":2000") != NULL && strstr(j, "\"permille\":790") != NULL,
          "battery");
    CHECK(strstr(j, "\"health_permille\":800") != NULL, "health");
    CHECK(strstr(j, "\"temp_dc\":300") != NULL, "temp");
    CHECK(zxv_power_token_scale() == 618, "balanced on battery");
    zxv_power_battery(150, 0, 0, 500000, 4000, 4000, 5000, 400, -1, 2000);
    CHECK(zxv_power_token_scale() == 382, "conserve at 15%");
    zxv_power_battery(40, 0, 0, 500000, 4000, 4000, 5000, 400, -1, 3000);
    CHECK(zxv_power_token_scale() == 0, "critical at 4%");
    zxv_power_battery(40, 1, 1, 800000, 4100, 4000, 5000, 400, -1, 4000);
    CHECK(zxv_power_token_scale() == 1000, "plugged in");
    zxv_power_settle(4000);
    zxv_power_json(j, sizeof(j));
    CHECK(strstr(j, "\"rate_mw\":-3280") != NULL, "charging rate negative");
    CHECK(strstr(j, "\"total_uj\":4000000") != NULL, "2 more seconds of 2 W, then rest");
    CHECK(strstr(j, "temp_dc") == NULL, "unknown temp");

    CHECK(zxv_power_start(ZXV_POWER_IOS, 6, 0) == 0, "ios");
    zxv_power_battery(640, 0, 0, -1, -1, -1, -1, -1, -1, 0);
    zxv_power_app_cpu(1000000000u);
    zxv_power_battery(630, 0, 0, -1, -1, -1, -1, -1, -1, 1000);
    zxv_power_settle(1000);
    zxv_power_json(j, sizeof(j));
    CHECK(strstr(j, "\"host\":\"ios\"") != NULL, "ios host");
    CHECK(strstr(j, "\"source\":\"none\"") != NULL, "no current, no energy");
    CHECK(strstr(j, "\"permille\":630") != NULL, "level still shown");
    CHECK(strstr(j, "\"cpu_ns\":1000000000") != NULL, "cpu still shown");
    CHECK(zxv_power_agent(1, "x", 1, 1) == 0, "agent on ios");
    printf("test_zxv_mobile_power: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
