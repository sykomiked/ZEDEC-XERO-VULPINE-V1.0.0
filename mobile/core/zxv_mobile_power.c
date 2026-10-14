/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_mobile_power.c — see zxv_mobile_power.h. */
#include "zxv_mobile_power.h"
#include "power_ledger.h"

static pw_ledger_t g_l;
static int g_on, g_meter = -1, g_app = -1, g_have;
static uint64_t g_acc_uj, g_last_ms;
static uint64_t g_last_uw;

int zxv_power_start(int host, uint32_t cpus, uint64_t now_ms)
{
    if (host != ZXV_POWER_ANDROID && host != ZXV_POWER_IOS) return PW_E_ARG;
    pw_init(&g_l, PW_DEV_MOBILE, (pw_host_t) host, cpus, 0, now_ms);
    g_meter = pw_meter_add(&g_l, "battery-drain", PW_DOM_BATTERY, PW_SCOPE_WHOLE, 0, 0);
    g_app = pw_consumer_add(&g_l, PW_KIND_APP, "zxv-companion", PW_NONE, 1);
    g_have = 0;
    g_acc_uj = 0;
    g_on = g_meter >= 0 && g_app > 0;
    return g_on ? 0 : PW_E_FULL;
}

int zxv_power_battery(int level_permille, int charging, int plugged, int64_t current_ua,
                      int64_t voltage_mv, int64_t full_mwh, int64_t design_mwh, int cycles,
                      int temp_dc, uint64_t now_ms)
{
    if (!g_on) return PW_E_ARG;
    pw_battery_t b = {0};
    b.present = level_permille >= 0;
    b.on_mains = plugged != 0;
    b.charging = charging != 0;
    b.permille = (uint16_t) (level_permille < 0 ? 0 : level_permille);
    b.full_mwh = full_mwh > 0 ? (uint64_t) full_mwh : 0;
    b.design_mwh = design_mwh > 0 ? (uint64_t) design_mwh : 0;
    b.now_mwh = b.full_mwh ? b.full_mwh * b.permille / 1000u : 0;
    b.cycles = cycles > 0 ? (uint32_t) cycles : 0;
    b.temp_dc = temp_dc == -1 ? INT32_MIN : temp_dc;
    b.at_ms = now_ms;
    bool known = current_ua >= 0 && voltage_mv > 0;
    uint64_t uw = known ? pw_muldiv((uint64_t) current_ua, (uint64_t) voltage_mv, 1000u) : 0;
    if (known) b.rate_mw = b.charging ? -(int64_t) (uw / 1000u) : (int64_t) (uw / 1000u);
    pw_battery_set(&g_l, &b);
    if (!known || b.on_mains || b.charging) {
        g_have = 0; /* not draining, or not measurable: the meter rests */
        return 0;
    }
    if (g_have && now_ms > g_last_ms) {
        g_acc_uj += pw_muldiv((g_last_uw + uw) / 2u, now_ms - g_last_ms, 1000u);
    } else {
        g_l.meters[g_meter].have_last = false;
    }
    pw_meter_sample(&g_l, (uint32_t) g_meter, g_acc_uj, now_ms);
    g_have = 1;
    g_last_ms = now_ms;
    g_last_uw = uw;
    return 0;
}

int zxv_power_app_cpu(uint64_t cpu_ns)
{
    if (!g_on) return PW_E_ARG;
    return pw_note_activity(&g_l, (uint16_t) g_app, cpu_ns, 0);
}

int zxv_power_agent(uint64_t agent_id, const char *name, uint64_t cpu_ns, uint64_t gpu_ns)
{
    if (!g_on) return PW_E_ARG;
    int g = pw_consumer_find(&g_l, PW_KIND_AGENT, agent_id);
    if (g < 0) g = pw_consumer_add(&g_l, PW_KIND_AGENT, name, (uint16_t) g_app, agent_id);
    if (g < 0) return g;
    return pw_note_activity(&g_l, (uint16_t) g, cpu_ns, gpu_ns);
}

int zxv_power_settle(uint64_t now_ms)
{
    return g_on ? pw_settle(&g_l, now_ms) : PW_E_ARG;
}

int zxv_power_json(char *out, size_t cap)
{
    return g_on ? pw_render_json(&g_l, out, cap) : PW_E_ARG;
}

uint32_t zxv_power_token_scale(void)
{
    return pw_posture_token_scale(pw_posture(&g_l));
}
